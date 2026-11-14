// wotm_lms.inc: modo Last Monster Standing (NATIVE.md, "Runtime: last monster standing"), incluido por wotm_overrides.cpp depois do
// wotm_arcade.inc (os ganchos daqui embrulham os do arcade).
//  - Entrada pela linha LAST MONSTER STANDING das telas 2 e 5 (wotm_mainmenu.inc arma o g_armed); o fluxo e o do free-for-all: m_mode 2
//    (1 jogador contra CPUs) ou 3 (2 a 4 jogadores, com ou sem CPUs). O InitPlayerLives da luta liga o modo.
//  - Todos contra todos. Cada monstro (jogador ou CPU) tem X renascimentos (LMS RESPAWNS nas opcoes, 0 a 9): jogador fora com vida
//    renasce no lugar (wotm_coop::revive), CPU fora com renascimento volta pelo aiInit; sem mais, sai de cena (jogador: wotm_coop::eliminate,
//    a tela com quem sobrou). O gameResolveLifeAndDeath do retail (que encerra a rodada no primeiro monstro fora) da lugar ao daqui.
//  - Fim: sobrou um jogador (a pose de vitoria dele, como no arcade) ou nao sobrou jogador nenhum (fim na hora; vence a CPU com mais
//    renascimentos sobrando, depois a com mais vida). O Evaluate da luta abre o dialogo 5 do free-for-all (PLAYER n WINS / AI WINS,
//    PLAY AGAIN, QUIT) com o vencedor em m_won[0]; o PLAY AGAIN refaz a luta na mesma sessao (InitPlayers e InitPlayerLives).
//  - O "vencer a luta" do menu de debug zera os renascimentos das CPUs antes de derruba-las (cheatWin).
//  - WOTM_LMS=<renascimentos> liga o modo na primeira luta (teste com a WOTM_MATCH, sem menu).
#include "wotm_lms.h"

namespace wotm_lms
{
using wotm_modes4::call;
using wotm_modes4::monster;
using wotm_modes4::rd32;
using wotm_modes4::rd8;
using wotm_modes4::wr32;
using wotm_split4::kGame;
using wotm_split4::kShell;
using wotm_split4::rdF;
namespace fn = wotm::fn;

constexpr uint32_t kMode = kShell + 0x2C10, kNumPlayers = kShell + 0x2BB4, kLives = kShell + 0x2BBC;
constexpr uint32_t kGameMode = kGame + 0x1203C8, kWon0 = kGame + 0x12043C;
// Monster
constexpr uint32_t kCs = 0xC, kPlayerNum = 0x18, kMonsterNum = 0x28, kState = 0x34, kDead = 0xE8, kOut = 0xF6, kVictorious = 0xF7,
                   kHealth = 0x44C, kVictoryState = 0x10E70;
using wotm_coop::kNumSlots;
using wotm_coop::kSlots;
using wotm_coop::kSlotStride;
constexpr int kMaxSlots = 16, kWinTimeout = 300;   // quadros da vitoria sem o kVictorious ate forcar a saida

// estado da luta (zerado no InitPlayerLives de cada uma, inclusive no PLAY AGAIN)
int g_respawns = 0;
bool g_humOut[4] = {}, g_aiOut[kMaxSlots] = {};
int g_aiLeft[kMaxSlots] = {};
int g_winFrames = 0;
bool g_ended = false, g_countLogged = false;
uint32_t g_winner = 0, g_lastOut = 0;
int g_envRespawns = -1;   // WOTM_LMS

struct Hook
{
    uint32_t addr;
    PS2Runtime::RecompiledFunction orig;
};
Hook g_lives{fn::InitPlayerLives__5Shell}, g_lifeDeath{fn::gameResolveLifeAndDeath__7TheGame},
    g_evalCh{fn::EvaluateOnePlayerChallengeStatus__5Shelli}, g_evalAi{fn::EvaluateMultiPlayerBattleStatusAI__5Shelli},
    g_evalNoAi{fn::EvaluateMultiPlayerBattleStatusNoAI__5Shelli}, g_userint{fn::userintMain__Fv}, g_viewCs{fn::viewGetViewCs__Fi};
uint32_t g_dialogCs = 0;   // o _cs que o dialogo do fim da luta recebe no viewGetViewCs (evalFight)
int g_dialogCsCalls = 0;

int players(uint8_t *rdram) { return std::clamp(static_cast<int32_t>(rd32(rdram, kNumPlayers)), 1, 4); }
int slotCount(uint8_t *rdram) { return std::min(static_cast<int>(rd32(rdram, kNumSlots)), kMaxSlots); }
uint32_t slot(int s) { return kSlots + kSlotStride * uint32_t(s); }
bool fightMode(uint8_t *rdram)
{
    const uint32_t gm = rd32(rdram, kGameMode);
    return gm == 2u || gm == 3u;
}

bool standing(uint8_t *rdram, uint32_t m)
{
    return m && !rd8(rdram, m + kOut) && !rd32(rdram, m + kDead) && rdF(rdram, m + kHealth) > 0.f;
}

void show(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t m, bool on)
{
    call(rdram, ctx, runtime, fn::hierSetCsDrawMe__FP3_csUc, {rd32(rdram, m + kCs), on ? 1u : 0u});
    call(rdram, ctx, runtime, fn::setShadowOnOff__7Monsterb, {m, on ? 1u : 0u});
}

void leave(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t game)
{
    if (rd32(rdram, wotm::sym::gRtReturn))
        return;
    call(rdram, ctx, runtime, fn::rtReturnToShell__Fii, {0u, 0x1Eu});
    call(rdram, ctx, runtime, fn::fadeOut__7TheGamei, {game, 4u});
}

// O "vencer a luta" do debug (wotm_overrides.cpp): as CPUs derrubadas ficam fora
void cheatWin()
{
    if (!on())
        return;
    std::fill(std::begin(g_aiLeft), std::end(g_aiLeft), 0);
    std::fprintf(stderr, "[lms] vencer a luta: CPUs sem renascimentos\n");
}

// Shell::InitPlayerLives (folha): X + 1 vidas por jogador, X renascimentos por CPU
void livesHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const bool entry = ctx->pc == g_lives.addr;
    const uint32_t ra = GPR_U32(ctx, 31);
    g_lives.orig(rdram, ctx, runtime);
    if (!entry || ctx->pc != ra)
        return;
    const uint32_t mode = rd32(rdram, kMode);
    if (g_envRespawns >= 0 && !g_armed && !on() && (mode == 2u || mode == 3u))
    {
        g_armed = true;   // WOTM_LMS: a primeira luta
        std::fprintf(stderr, "[lms] armado pela WOTM_LMS (%d renascimento(s))\n", g_envRespawns);
    }
    if (!g_armed || (mode != 2u && mode != 3u))
        return;
    g_on = true;   // o g_armed continua: o PLAY AGAIN passa de novo por aqui
    g_respawns = g_envRespawns >= 0 ? g_envRespawns : wotm::settings::get(wotm::settings::LmsRespawns);
    const int n = players(rdram);
    for (int i = 0; i < 4; ++i)
        wr32(rdram, kLives + 4u * uint32_t(i), i < n ? uint32_t(g_respawns + 1) : 0u);
    // m_killTarget (o "first to" da tela 15 do free-for-all): com ele > 0, o StateDeath::transitionInto do retail, quando as vitorias do
    // matador chegam nele, abre a vitoria do FFA (gUseUnifiedView, camera 2 na vitima com o POV 7) que o LMS nunca encerra; a camera
    // ficava parada na vitima. Aqui o fim e o do LMS.
    wr32(rdram, kShell + 0x2A44, 0u);
    // CPUs que sairam na luta anterior (PLAY AGAIN): o aiInit ja devolveu o m_playerNum 2; o desenho e a sombra voltam aqui
    for (int s = 0; s < kMaxSlots; ++s)
        if (g_aiOut[s] && s < slotCount(rdram))
            show(rdram, ctx, runtime, slot(s), true);
    std::fill(std::begin(g_humOut), std::end(g_humOut), false);
    std::fill(std::begin(g_aiOut), std::end(g_aiOut), false);
    std::fill(std::begin(g_aiLeft), std::end(g_aiLeft), g_respawns);
    g_winFrames = 0;
    g_ended = false;
    g_winner = g_lastOut = 0;
    g_countLogged = false;
    std::fprintf(stderr, "[lms] luta (m_mode %u): %d jogador(es), %d vaga(s), %d renascimento(s) cada\n", mode, n, slotCount(rdram), g_respawns);
}

// A pose de vitoria do jogador p (o modelo do modo 1 do retail; o StateVictory::transitionOK nos m_mode 2 e 3 pede o m_killTarget)
void tryPose(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t p)
{
    const uint32_t st = rd32(rdram, p + kState) & 0x1FFFFFFFu;
    if (st < 0x1000u || rd32(rdram, st) == 0x40u)
        return;
    const uint32_t gm = rd32(rdram, kGameMode);
    wr32(rdram, kGameMode, 1u);
    const bool ok = call(rdram, ctx, runtime, fn::transitionOK__12StateVictory, {p + kVictoryState}) != 0;
    wr32(rdram, kGameMode, gm);
    if (ok)
    {
        call(rdram, ctx, runtime, fn::enterNewState__7MonsterP12MonsterState, {p, p + kVictoryState});
        std::fprintf(stderr, "[lms] pose de vitoria (quadro %d)\n", g_winFrames);
    }
}

// TheGame::gameResolveLifeAndDeath (m_mode 2 e 3): quem cai renasce enquanto tiver renascimentos; a luta acaba com um so de pe
void lifeDeathHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc != g_lifeDeath.addr || !on() || !fightMode(rdram))
    {
        g_lifeDeath.orig(rdram, ctx, runtime);
        return;
    }
    const uint32_t game = GPR_U32(ctx, 4);
    ctx->pc = GPR_U32(ctx, 31);
    if (rd32(rdram, wotm::sym::gRtReturn))
        return;
    const int n = players(rdram);
    if (g_ended)
    {   // o vencedor ja saiu: a pose (so jogador) e a saida em kWinTimeout quadros
        const bool posed = rd8(rdram, g_winner + kVictorious) != 0;
        if (++g_winFrames > kWinTimeout || posed)
        {
            std::fprintf(stderr, "[lms] fim da luta (%s, quadro %d)\n", posed ? "pose" : "sem pose", g_winFrames);
            leave(rdram, ctx, runtime, game);
            return;
        }
        if (wotm_modes4::playerOf(rdram, g_winner, n) >= 0 && standing(rdram, g_winner))
            tryPose(rdram, ctx, runtime, g_winner);
        return;
    }
    int humans = 0, outNow = 0, outList[4] = {};
    uint32_t lastHuman = 0;
    for (int i = 0; i < n; ++i)
    {
        const uint32_t m = monster(rdram, i);
        if (!m || g_humOut[i])
            continue;
        if (rd8(rdram, m + kOut))
        {
            const int lives = static_cast<int32_t>(rd32(rdram, kLives + 4u * uint32_t(i)));
            if (lives > 1)
                wotm_coop::revive(rdram, ctx, runtime, i, lives - 1);
            else
            {
                g_humOut[i] = true;
                g_lastOut = m;
                outList[outNow++] = i;
                std::fprintf(stderr, "[lms] jogador %d fora da luta\n", i + 1);
                continue;
            }
        }
        ++humans;
        lastHuman = m;
    }
    // Monstro escondido e a tela com quem sobrou; o ultimo jogador a cair fica na tela dele ate o dialogo (como o game over do coop)
    for (int k = 0; k < outNow; ++k)
        if (humans)
            wotm_coop::eliminate(rdram, ctx, runtime, outList[k], n);
        else
            wr32(rdram, kLives + 4u * uint32_t(outList[k]), 0u);
    int cpus = 0;
    uint32_t best = 0;
    for (int s = 0, slots = slotCount(rdram); s < slots; ++s)
    {
        const uint32_t m = slot(s);
        if (g_aiOut[s] || wotm_modes4::playerOf(rdram, m, n) >= 0)
            continue;
        if (rd8(rdram, m + kOut))
        {
            if (g_aiLeft[s] > 0)
            {
                --g_aiLeft[s];
                call(rdram, ctx, runtime, fn::aiInit__7Monster, {m});
                std::fprintf(stderr, "[lms] CPU (vaga %d) renasce (%d sobrando)\n", s, g_aiLeft[s]);
            }
            else
            {
                g_aiOut[s] = true;
                g_lastOut = m;
                show(rdram, ctx, runtime, m, false);
                wr32(rdram, m + kPlayerNum, 0u);   // como o endurance: fora das contas de alvo e do HUD
                std::fprintf(stderr, "[lms] CPU (vaga %d) fora da luta\n", s);
                continue;
            }
        }
        ++cpus;
        if (!best)
            best = m;
        else
        {
            const int sb = static_cast<int>((best - kSlots) / kSlotStride);
            const float hm = rdF(rdram, m + kHealth), hb = rdF(rdram, best + kHealth);
            if (g_aiLeft[s] > g_aiLeft[sb] || (g_aiLeft[s] == g_aiLeft[sb] && hm > hb))
                best = m;
        }
    }
    if (!g_countLogged)
        std::fprintf(stderr, "[lms] em campo: %d jogador(es), %d CPU(s) (%d vagas)\n", humans, cpus, slotCount(rdram));
    g_countLogged = true;
    if (humans + cpus > 1 && humans)
        return;
    // acabou: um jogador sobrou (pose), ou nenhum jogador sobrou (vence a melhor CPU, ou o ultimo a cair no empate)
    g_ended = true;
    g_winFrames = 0;
    g_winner = humans ? lastHuman : best ? best : g_lastOut;
    std::fprintf(stderr, "[lms] vencedor: %s (vaga %u), %d jogador(es) e %d CPU(s) de pe\n",
                 wotm_modes4::playerOf(rdram, g_winner, n) >= 0 ? "jogador" : "CPU", g_winner ? rd32(rdram, g_winner + kMonsterNum) : 0u,
                 humans, cpus);
    if (!humans)
        leave(rdram, ctx, runtime, game);   // so CPUs: sem esperar a pose
}

// Fim de rodada (o codigo 0 vem do lifeDeathHook): o dialogo 5 do free-for-all com o vencedor
void evalFight(const Hook &h, uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc != h.addr || !on() || !g_ended || GPR_U32(ctx, 5) != 0u)
    {
        h.orig(rdram, ctx, runtime);
        return;
    }
    const uint32_t ra = GPR_U32(ctx, 31), w = g_winner ? rd32(rdram, g_winner + kMonsterNum) : 0u;
    wr32(rdram, kWon0, w);
    std::fprintf(stderr, "[lms] dialogo: vencedor na vaga %u (m_playerNum %u)\n", w, rd32(rdram, slot(int(w)) + kPlayerNum));
    // O displayDialog copia para a vista unificada a camera da vista m_won[0] (viewGetViewCs), que no free-for-all e o indice do
    // jogador; aqui m_won[0] e a vaga, e a vaga de uma CPU aponta para uma vista parada ou inexistente (fundo preto). A camera e a da
    // vista 0: a do vencedor, ou a do ultimo jogador a cair
    g_dialogCs = rd32(rdram, wotm::sym::viewInfo);
    g_dialogCsCalls = 2;   // translacao e rotacao
    if (wotm_coop::retryTest())
        wotm_coop::retryCross();   // WOTM_COOPRETRY=1: PLAY AGAIN sem teclado
    wotm_modes4::tailDialog(rdram, ctx, runtime, 5, ra);
}
void evalChHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime) { evalFight(g_evalCh, rdram, ctx, runtime); }
void evalAiHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime) { evalFight(g_evalAi, rdram, ctx, runtime); }
void evalNoAiHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime) { evalFight(g_evalNoAi, rdram, ctx, runtime); }

// viewGetViewCs (folha): as duas chamadas do displayDialog depois do evalFight recebem o g_dialogCs
void viewCsHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc != g_viewCs.addr || g_dialogCsCalls <= 0 || !g_dialogCs)
    {
        g_viewCs.orig(rdram, ctx, runtime);
        return;
    }
    --g_dialogCsCalls;
    ctx->r[2] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(g_dialogCs)));
    ctx->pc = GPR_U32(ctx, 31);
}

// userintMain: os menus abriram (QUIT); o modo desliga (o g_armed fica ate a proxima escolha no menu)
void userintHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == g_userint.addr && on())
    {
        g_on = false;
        g_envRespawns = -1;
        std::fprintf(stderr, "[lms] de volta aos menus\n");
    }
    g_userint.orig(rdram, ctx, runtime);
}
}

void wotmLmsInstall(PS2Runtime &runtime)
{
    using namespace wotm_lms;
    const std::pair<Hook *, PS2Runtime::RecompiledFunction> hooks[] = {
        {&g_lives, &livesHook},   {&g_lifeDeath, &lifeDeathHook}, {&g_evalCh, &evalChHook},
        {&g_evalAi, &evalAiHook}, {&g_evalNoAi, &evalNoAiHook},   {&g_userint, &userintHook},
        {&g_viewCs, &viewCsHook}};
    for (const auto &[h, f] : hooks)
    {
        if (!(h->orig = runtime.lookupFunction(h->addr)))
        {
            std::fprintf(stderr, "[lms] indisponivel: funcao 0x%08X sem gancho no runtime\n", h->addr);
            return;
        }
    }
    for (const auto &[h, f] : hooks)
        runtime.registerFunction(h->addr, f);
    if (const char *e = std::getenv("WOTM_LMS"); e && *e)
    {
        g_envRespawns = std::clamp(std::atoi(e), 0, 9);
        std::fprintf(stderr, "[lms] na primeira luta (WOTM_LMS=%d)\n", g_envRespawns);
    }
}
