// wotm_arcade.inc: modo Arcade (NATIVE.md, "Runtime: modo arcade"), incluido por wotm_overrides.cpp depois do wotm_levelreload.inc.
// Plano e decisoes do dono em docs/investigacoes/2026-11-13/modo_arcade_plano.md.
//  - 5 lutas seguidas sem passar pelos menus: m_inSession 0 com m_inMenus 1 repete a sessao (o RESTART do retail). As 4 primeiras sao
//    o desafio (m_mode 2, 1 jogador) ou o free for all com CPUs (m_mode 3, 2 a 4 jogadores) numa fase sorteada, com 1 a MAX CPUS
//    CPUs (ARCADE CPUS nas opcoes, nunca mais que 8 - jogadores) crescendo a cada luta; a 5a e o chefe, Goliath Prime (fase 3) ou
//    Cerebulon (fase 11), na fase dele da aventura (m_mode 1, a campanha co-op com 2 a 4).
//  - A luta e escrita no Shell na entrada do ResolveCommandLineArguments (o main monta a fase dali com o m_levelNum); o
//    InitPlayerLives da 2 vidas (1 renascimento) a cada jogador.
//  - Nas 4 primeiras o gameResolveLifeAndDeath do retail (que encerra a rodada no primeiro monstro fora) da lugar ao daqui: jogador fora
//    com vida renasce (wotm_coop::revive), sem vida sai (wotm_coop::eliminate: tela com quem sobrou); CPU fora sai de cena; todos os jogadores fora = derrota
//    (rtReturnToShell 1), todas as CPUs fora = a vitoria do primeiro jogador de pe (rtReturnToShell 0). O EvaluateOnePlayerChallenge
//    e o EvaluateMultiPlayerBattleStatusAI recebem esses codigos: vitoria soma os tokens e marca a proxima luta; derrota abre o
//    dialogo de derrota da aventura (CONTINUE repete a luta, QUIT vai ao menu com os tokens ganhos).
//  - Sem fogo amigo: jogador nao fere jogador, CPU nao fere CPU (takeDamage), e as CPUs nao se escolhem como alvo (Ai::s_attackOtherAi).
//  - Tokens (m_tokens, Shell+0x2B24): por luta, o grandTotal do TokenManager (x 1 + 0.1 n no co-op) + dif x {200, 400, 600, 800};
//    o chefe, dif x 1500 + 3000 + dif x 3000; dif = 10, 15 ou 20 pela dificuldade.
//  - WOTM_ARCADE=<semente>[,<luta 1-5>] comeca a corrida na primeira sessao (teste com a WOTM_MATCH, sem menu).
#include <random>
#include "wotm_arcade.h"

namespace wotm_arcade
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

constexpr uint32_t kMode = kShell + 0x2C10, kBattleMode = kShell + 0x2B54, kLevelNum = kShell + 0x2BB0, kNumPlayers = kShell + 0x2BB4,
                   kNumAIsShell = kShell + 0x2BB8, kLives = kShell + 0x2BBC, kTokens = kShell + 0x2B24, kLastScore = kShell + 0x2B28,
                   kScore = kShell + 0x2B2C, kFromSave = kShell + 0x2C18, kKillTarget = kShell + 0x2A44, kMonsterSel = kShell + 0x2938,
                   kCostume = kShell + 0x2BE0, kAdvAIs = kShell + 0x29E0, kRestart = kShell + 0x2BA8, kInMenus = kShell + 0x2BD0,
                   kInSession = kShell + 0x2BCC, kPaused = kShell + 0x2BA4;
constexpr uint32_t kGameMode = kGame + 0x1203C8, kDiff = kGame + 0x1203CC, kLevelId = kGame + 0x1203D0, kGameAIs = kGame + 0x1203E0,
                   kTokenMgr = kGame + 0x11C370;
// Monster
constexpr uint32_t kCs = 0xC, kState = 0x34, kDead = 0xE8, kOut = 0xF6, kVictorious = 0xF7, kHealth = 0x44C, kVictoryState = 0x10E70;
constexpr uint32_t kPlayerNum = 0x18;
// TheGame: os monstros da luta (jogadores e CPUs) nas vagas m_slots; m_monsters[0..n) aponta os dos jogadores
using wotm_coop::kNumSlots;
using wotm_coop::kSlots;
using wotm_coop::kSlotStride;
constexpr int kMaxSlots = 16;
constexpr uint32_t kSlotSize = 0x2C;   // SelectSlot: taken +0, state +4; 4 trajes por coluna
constexpr int kFights = 5, kBoss = 4, kMaxCpus = 7, kWinTimeout = 300;   // quadros da vitoria sem o kVictorious ate forcar a saida

struct Fight
{
    int level = 0, cpus = 0;
    uint32_t sel[kMaxCpus] = {}, costume[kMaxCpus] = {};
};
struct Run
{
    uint32_t seed = 0;
    int players = 1, fight = 0, boss = 3, maxCpus = 3;
    Fight f[kFights];
};
Run g_run;
bool g_envDone = false;
// estado da sessao (zerado na entrada de cada uma)
bool g_humOut[4] = {}, g_aiOut[kMaxSlots] = {};
int g_winFrames = 0;
bool g_attackLogged = false, g_countLogged = false;

// ------------------------------------------------------------------ sorteio

bool columnOpen(uint8_t *rdram, int col)
{
    if (col < 1 || col > 10)
        return false;
    if (col == 1 || col == 10)   // a primeira e a ultima colunas destravam (initSelectSwitches)
        return rd32(rdram, wotm::sym::monsterSelectMode + uint32_t((col - 1) * 4) * kSlotSize + 4) == 1u;
    return true;
}
bool costumeUnlocked(uint8_t *rdram, int col, int c)
{
    return rd32(rdram, wotm::sym::monsterSelectMode + uint32_t((col - 1) * 4 + c) * kSlotSize + 4) == 1u;
}
int monsterOfCol(uint8_t *rdram, int col) { return int(rd32(rdram, wotm::sym::monsterNametomonsterId + 4u * uint32_t(col - 1)) >> 5); }
// levelUnlocked do jogo (wotm_levelsel.inc): 10, 11, 14 e 15 pela tabela de destrave
bool levelUnlocked(uint8_t *rdram, int level)
{
    uint32_t off = 0;
    switch (level)
    {
    case 10: off = 0x768; break;
    case 11: off = 0x73C; break;
    case 14: off = 0x710; break;
    case 15: off = 0x6E4; break;
    default: return true;
    }
    return rd32(rdram, wotm::sym::monsterSelectMode + off) != 0;
}
// A METRO sem .AIP proprio nao aguenta mais de uma CPU (wotm_levelsel.inc, skipNoAip)
bool metroBlocked(int mode, int cpus) { return (mode == 2 && cpus >= 2) || (mode == 3 && cpus > 0); }
int fightMode(int players) { return players == 1 ? 2 : 3; }
uint32_t playerCostume(uint8_t *rdram, int i)
{
    return i < 2 ? rd32(rdram, kCostume + 4u * uint32_t(i)) : wotm_split4::g_extraCostume[i - 2];
}

void draw(uint8_t *rdram, uint32_t seed, int startFight)
{
    Run r;
    r.seed = seed;
    r.players = std::clamp(int(rd32(rdram, kNumPlayers)), 1, 4);
    r.fight = std::clamp(startFight, 0, kFights - 1);
    r.maxCpus = std::clamp(std::min(wotm::settings::get(wotm::settings::ArcadeCpus), 8 - r.players), 1, kMaxCpus);
    std::mt19937 rng(seed);
    r.boss = rng() & 1u ? 11 : 3;
    const int mode = fightMode(r.players);
    int pm[4], pc[4];
    for (int i = 0; i < r.players; ++i)
        pm[i] = int(rd32(rdram, kMonsterSel + 4u * uint32_t(i)) >> 5), pc[i] = int(playerCostume(rdram, i));
    int open[10], nOpen = 0;
    for (int c = 1; c <= 10; ++c)
        if (columnOpen(rdram, c))
            open[nOpen++] = c;
    std::vector<int> used;
    for (int f = 0; f < kBoss; ++f)
    {
        Fight &fi = r.f[f];
        fi.cpus = std::max(1, ((f + 1) * r.maxCpus + 3) / 4);
        std::vector<int> pool;
        for (int l = 1; l <= 15; ++l)
            if (l != 12 && l != r.boss && levelUnlocked(rdram, l) && !(l == 4 && metroBlocked(mode, fi.cpus)) &&
                std::find(used.begin(), used.end(), l) == used.end())
                pool.push_back(l);
        fi.level = pool.empty() ? 1 : pool[rng() % pool.size()];
        used.push_back(fi.level);
        // CPUs: colunas abertas sem repetir monstro na luta enquanto der; traje destravado que ninguem do mesmo monstro tem
        std::vector<int> cols(open, open + nOpen);
        std::shuffle(cols.begin(), cols.end(), rng);
        int cm[kMaxCpus], cc[kMaxCpus];
        for (int c = 0; c < fi.cpus; ++c)
        {
            const int col = nOpen ? cols[size_t(c) % cols.size()] : 1, m = monsterOfCol(rdram, col);
            std::vector<int> opts;
            for (int k = 0; k < 4; ++k)
            {
                bool taken = !costumeUnlocked(rdram, col, k);
                for (int i = 0; i < r.players; ++i)
                    taken = taken || (pm[i] == m && pc[i] == k);
                for (int j = 0; j < c; ++j)
                    taken = taken || (cm[j] == m && cc[j] == k);
                if (!taken)
                    opts.push_back(k);
            }
            int costume = 0;
            if (!opts.empty())
                costume = opts[rng() % opts.size()];
            else
                for (int k = 0; k < 4; ++k)
                    if (costumeUnlocked(rdram, col, k))
                    {
                        costume = k;
                        break;
                    }
            cm[c] = m, cc[c] = costume;
            fi.sel[c] = uint32_t(m) << 5;
            fi.costume[c] = uint32_t(costume);
        }
    }
    r.f[kBoss].level = r.boss;
    g_run = r;
    std::fprintf(stderr, "[arcade] corrida (semente %u): %d jogador(es), ate %d CPUs, chefe na fase %d\n", seed, r.players, r.maxCpus, r.boss);
    for (int f = 0; f < kBoss; ++f)
    {
        std::fprintf(stderr, "[arcade]   luta %d: fase %d, %d CPU(s):", f + 1, r.f[f].level, r.f[f].cpus);
        for (int c = 0; c < r.f[f].cpus; ++c)
            std::fprintf(stderr, " %u:%u", r.f[f].sel[c] >> 5, r.f[f].costume[c]);
        std::fprintf(stderr, "\n");
    }
}

// A luta g_run.fight no Shell, antes do ResolveCommandLineArguments e do InitRTState
void writeFight(uint8_t *rdram)
{
    const Fight &fi = g_run.f[g_run.fight];
    for (uint32_t i = 4; i < 16; ++i)
        wr32(rdram, kMonsterSel + 4u * i, 0u);
    for (uint32_t i = 2; i < 12; ++i)
        wr32(rdram, kCostume + 4u * i, 0u);
    wr32(rdram, kKillTarget, 0u);
    wr32(rdram, kLevelNum, uint32_t(fi.level));
    if (g_run.fight < kBoss)
    {
        const uint32_t mode = uint32_t(fightMode(g_run.players));
        wr32(rdram, kMode, mode);
        wr32(rdram, kBattleMode, mode);
        for (int c = 0; c < fi.cpus; ++c)
        {
            wr32(rdram, kMonsterSel + 4u * uint32_t(4 + c), fi.sel[c]);
            wr32(rdram, kCostume + 4u * uint32_t(2 + c), fi.costume[c]);
        }
        wr32(rdram, kNumAIsShell, uint32_t(fi.cpus));
        std::fprintf(stderr, "[arcade] luta %d de %d: fase %d, %d CPU(s)\n", g_run.fight + 1, kFights, fi.level, fi.cpus);
        return;
    }
    // o chefe: a fase dele na aventura (m_mode 1), com as IAs da tabela da aventura (como a tela 3 e o adventureAis do co-op)
    const uint32_t level = uint32_t(fi.level), n = std::min(rd32(rdram, kAdvAIs + 4u * level), 4u);
    wr32(rdram, kMode, 1u);
    for (uint32_t i = 0; i < n; ++i)
    {
        wr32(rdram, kMonsterSel + 4u * (4u + i), rd32(rdram, wotm::sym::levelMonsters + 48u * level + 4u * i));
        wr32(rdram, kCostume + 4u * (2u + i), rd32(rdram, wotm::sym::levelMonsterModels + 16u * level + 4u * i));
    }
    wr32(rdram, kNumAIsShell, n);
    wr32(rdram, kScore, 0u);
    wr32(rdram, kFromSave, 0u);
    wr32(rdram, wotm::sym::continueDecoding, 0u);
    std::fprintf(stderr, "[arcade] luta %d de %d: o chefe na fase %u (%u IA(s))\n", kFights, kFights, level, n);
}

// ------------------------------------------------------------------ fim de luta

int dif(uint8_t *rdram)
{
    const int32_t d = static_cast<int32_t>(rd32(rdram, kDiff));
    return d <= 0 ? 10 : d == 1 ? 15 : 20;
}

void award(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, int bonus, const char *what)
{
    // TheGame::m_tokens[4] (stride 0x1004): cada jogador credita no seu (predios destruidos, itens usados)
    const int n = g_run.players;
    int64_t total = 0;
    for (int i = 0; i < n; ++i)
        total += static_cast<int32_t>(call(rdram, ctx, runtime, fn::grandTotal__12TokenManager, {kTokenMgr + 0x1004u * uint32_t(i)}));
    const int64_t fromFight = std::max<int64_t>(0, std::llround(double(total) * (n > 1 ? 1.0 + 0.1 * n : 1.0)));
    const int64_t sum = fromFight + bonus;
    const int64_t tokens = std::min<int64_t>(int64_t(rd32(rdram, kTokens)) + sum, 0x7FFFFFFF);
    wr32(rdram, kTokens, uint32_t(tokens));
    wr32(rdram, kLastScore, uint32_t(sum));
    std::fprintf(stderr, "[arcade] %s: %lld tokens (luta %lld + bonus %d); total %lld\n", what, (long long)sum, (long long)fromFight, bonus,
                 (long long)tokens);
}

// A proxima sessao sem passar pelos menus: as escritas do RESTART da pausa (wotm_levelreload.inc)
void nextSession(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t cont = rd32(rdram, kShell + 0x2B48);
    wr32(rdram, kShell + 0x2B40, cont);
    wr32(rdram, kShell + 0x2B44, cont);
    wr32(rdram, kShell + 0x2B4C, 0);
    wr32(rdram, kShell + 0x2B50, 0);
    wr32(rdram, kRestart, 1);
    wr32(rdram, kInMenus, 1);
    wr32(rdram, kInSession, 0);
    wr32(rdram, kPaused, 0);
    if (!rd32(rdram, kGame + 0x1204C0 + 0x109C))
        call(rdram, ctx, runtime, fn::snd_StopAllSounds, {0u});
    wr32(rdram, wotm::sym::gExitDialog, 0);
    wotm_training::g_inFight = false;
}

// A corrida acabou (vitoria sobre o chefe): de volta aos menus
void finish(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    wr32(rdram, kInSession, 0);
    wr32(rdram, kInMenus, 0);
    wr32(rdram, kPaused, 0);
    if (!rd32(rdram, kGame + 0x1204C0 + 0x109C))
        call(rdram, ctx, runtime, fn::snd_StopAllSounds, {0u});
    wr32(rdram, wotm::sym::gExitDialog, 0);
    wotm_training::g_inFight = false;
    g_on = false;
    std::fprintf(stderr, "[arcade] corrida vencida; tokens %u\n", rd32(rdram, kTokens));
}

// Os filmes do fim, como no fim da aventura: o Shell::GenesisMovie (com o genesisHook do co-op, o filme de cada parceiro) poe o
// final do heroi do jogador 1 na fila (needOutro), que leva aos creditos; a volta dos menus os toca antes da tela 1. Heroi sem
// filme proprio (custom, chefe) vai direto aos creditos.
void ending(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    call(rdram, ctx, runtime, fn::GenesisMovie__5Shell, {uint32_t(kShell)});
    if (!rd32(rdram, wotm::sym::nextMovie))
        wr32(rdram, wotm::sym::nextMovie, 19u);
    std::fprintf(stderr, "[arcade] final: filme %u e os creditos\n", rd32(rdram, wotm::sym::nextMovie));
}

// ------------------------------------------------------------------ ganchos

struct Hook
{
    uint32_t addr;
    PS2Runtime::RecompiledFunction orig;
};
Hook g_resolveArgs{fn::ResolveCommandLineArguments__FiPPc}, g_lives{fn::InitPlayerLives__5Shell}, g_aiGlobal{fn::globalInit__2Ai},
    g_lifeDeath{fn::gameResolveLifeAndDeath__7TheGame}, g_evalCh{fn::EvaluateOnePlayerChallengeStatus__5Shelli},
    g_evalAi{fn::EvaluateMultiPlayerBattleStatusAI__5Shelli}, g_evalStory{fn::EvaluateOnePlayerStoryStatus__5Shelli},
    g_takeDamage{fn::takeDamage__7MonsterfbP7Monster}, g_userint{fn::userintMain__Fv};

bool on() { return g_on.load(std::memory_order_relaxed); }

// WOTM_ARCADE=<semente>[,<luta 1-5>]: a corrida da primeira sessao (uma vez)
bool envStart(uint32_t &seed, int &fight)
{
    if (g_envDone)
        return false;
    g_envDone = true;
    const char *e = std::getenv("WOTM_ARCADE");
    if (!e || !*e)
        return false;
    char *end = nullptr;
    seed = uint32_t(std::strtoul(e, &end, 10));
    fight = end && *end == ',' ? std::atoi(end + 1) - 1 : 0;
    return true;
}

void resolveArgsHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    // so a chamada de cada sessao; a primeira e a do boot (main, antes dos menus) e fica de fora
    static bool booted = false;
    const bool session = booted;
    booted = true;
    if (ctx->pc == g_resolveArgs.addr && session)
    {
        uint32_t seed = 0;
        int start = 0;
        if (!on() && g_armed.exchange(false))
        {
            seed = uint32_t(std::chrono::steady_clock::now().time_since_epoch().count());
            draw(rdram, seed, 0);
            g_on = true;
        }
        else if (!on() && envStart(seed, start))
        {
            draw(rdram, seed, start);
            g_on = true;
        }
        if (on())
        {
            std::fill(std::begin(g_humOut), std::end(g_humOut), false);
            std::fill(std::begin(g_aiOut), std::end(g_aiOut), false);
            g_winFrames = 0;
            g_attackLogged = false;
            g_countLogged = false;
            writeFight(rdram);
        }
    }
    g_resolveArgs.orig(rdram, ctx, runtime);
}

// Shell::InitPlayerLives (folha): 2 vidas por jogador, 1 renascimento por luta
void livesHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == g_lives.addr && on())
    {
        for (uint32_t i = 0; i < 4; ++i)
            wr32(rdram, kLives + 4u * i, 2u);
        ctx->pc = GPR_U32(ctx, 31);
        return;
    }
    g_lives.orig(rdram, ctx, runtime);
}

// Ai::globalInit: as CPUs das 4 primeiras lutas nao se escolhem como alvo (o AiSeek pula as de m_playerNum 2)
void aiGlobalHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const bool entry = ctx->pc == g_aiGlobal.addr;
    const uint32_t ra = GPR_U32(ctx, 31);
    g_aiGlobal.orig(rdram, ctx, runtime);
    if (entry && ctx->pc == ra && on() && g_run.fight < kBoss)
    {
        wr32(rdram, wotm::sym::x_2Ai_s_attackOtherAi, 0u);
        if (!g_attackLogged)
            std::fprintf(stderr, "[arcade] CPUs so contra os jogadores\n");
        g_attackLogged = true;
    }
}

bool standing(uint8_t *rdram, uint32_t m)
{
    return m && !rd8(rdram, m + kOut) && !rd32(rdram, m + kDead) && rdF(rdram, m + kHealth) > 0.f;
}

void hide(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t m)
{
    call(rdram, ctx, runtime, fn::hierSetCsDrawMe__FP3_csUc, {rd32(rdram, m + kCs), 0u});
    call(rdram, ctx, runtime, fn::setShadowOnOff__7Monsterb, {m, 0u});
}

void leave(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t game, int code)
{
    if (rd32(rdram, wotm::sym::gRtReturn))
        return;
    call(rdram, ctx, runtime, fn::rtReturnToShell__Fii, {uint32_t(code), 0x1Eu});
    call(rdram, ctx, runtime, fn::fadeOut__7TheGamei, {game, 4u});
}

// TheGame::gameResolveLifeAndDeath nas 4 primeiras lutas (m_mode 2 e 3): a rodada so acaba quando um dos lados inteiro cai
void lifeDeathHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int32_t gm = static_cast<int32_t>(rd32(rdram, kGameMode));
    if (ctx->pc != g_lifeDeath.addr || !on() || g_run.fight >= kBoss || (gm != 2 && gm != 3))
    {
        g_lifeDeath.orig(rdram, ctx, runtime);
        return;
    }
    const uint32_t game = GPR_U32(ctx, 4);
    ctx->pc = GPR_U32(ctx, 31);
    if (rd32(rdram, wotm::sym::gRtReturn))
        return;
    const int n = g_run.players;
    int humans = 0;
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
                // monstro escondido e a tela remontada com quem continua (1 em tela cheia, 2 nas metades, 3 no layout de 3), como
                // na aventura; o Shell::InitPlayers da proxima luta volta a tela da largada
                g_humOut[i] = true;
                wotm_coop::eliminate(rdram, ctx, runtime, i, n);
                std::fprintf(stderr, "[arcade] jogador %d fora da luta\n", i + 1);
                continue;
            }
        }
        ++humans;
    }
    if (!humans)
    {
        std::fprintf(stderr, "[arcade] luta %d perdida\n", g_run.fight + 1);
        leave(rdram, ctx, runtime, game, 1);
        return;
    }
    int cpus = 0;
    const int slots = std::min(int(rd32(rdram, kNumSlots)), kMaxSlots);
    for (int s = 0; s < slots; ++s)
    {
        const uint32_t m = kSlots + kSlotStride * uint32_t(s);
        if (g_aiOut[s] || wotm_modes4::playerOf(rdram, m, n) >= 0)
            continue;
        if (rd8(rdram, m + kOut))
        {
            g_aiOut[s] = true;
            hide(rdram, ctx, runtime, m);
            wr32(rdram, m + kPlayerNum, 0u);   // como o endurance: fora das contas de alvo e do HUD
            std::fprintf(stderr, "[arcade] CPU (vaga %d) fora da luta\n", s);
            continue;
        }
        ++cpus;
    }
    if (!g_countLogged)
        std::fprintf(stderr, "[arcade] em campo: %d jogador(es), %d CPU(s) (%d vagas)\n", humans, cpus, slots);
    g_countLogged = true;
    if (cpus)
    {
        g_winFrames = 0;
        return;
    }
    // vitoria: a pose do primeiro jogador de pe (o modelo do modo 1 do retail); sem ela em kWinTimeout quadros, sai assim mesmo
    uint32_t p = 0;
    for (int i = 0; i < n && !p; ++i)
        if (!g_humOut[i] && standing(rdram, monster(rdram, i)))
            p = monster(rdram, i);
    if (++g_winFrames > kWinTimeout || (p && rd8(rdram, p + kVictorious)))
    {
        std::fprintf(stderr, "[arcade] luta %d vencida (%s, quadro %d)\n", g_run.fight + 1,
                     p && rd8(rdram, p + kVictorious) ? "pose" : "sem pose", g_winFrames);
        leave(rdram, ctx, runtime, game, 0);
        return;
    }
    if (p)
    {
        // o StateVictory::transitionOK nos m_mode 2 e 3 so libera com o m_killTarget do Shell atingido (o arcade o deixa 0): o
        // teste roda com o m_mode 1, o da aventura
        const uint32_t st = rd32(rdram, p + kState) & 0x1FFFFFFFu;
        bool ok = false;
        if (st >= 0x1000u && rd32(rdram, st) != 0x40u)
        {
            wr32(rdram, kGameMode, 1u);
            ok = call(rdram, ctx, runtime, fn::transitionOK__12StateVictory, {p + kVictoryState}) != 0;
            wr32(rdram, kGameMode, uint32_t(gm));
        }
        if (ok)
        {
            call(rdram, ctx, runtime, fn::enterNewState__7MonsterP12MonsterState, {p, p + kVictoryState});
            std::fprintf(stderr, "[arcade] pose de vitoria (quadro %d)\n", g_winFrames);
        }
    }
}

// Fim de rodada das 4 primeiras lutas (os codigos vem do lifeDeathHook): 0 vitoria, 1 derrota
void evalFight(const Hook &h, uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int32_t r = static_cast<int32_t>(GPR_U32(ctx, 5));
    if (ctx->pc != h.addr || !on() || g_run.fight >= kBoss || (r != 0 && r != 1))
    {
        h.orig(rdram, ctx, runtime);
        return;
    }
    const uint32_t ra = GPR_U32(ctx, 31);
    if (r == 0)
    {
        static constexpr int kBonus[kBoss] = {200, 400, 600, 800};
        char what[32];
        std::snprintf(what, sizeof what, "luta %d", g_run.fight + 1);
        award(rdram, ctx, runtime, dif(rdram) * kBonus[g_run.fight], what);
        ++g_run.fight;
        nextSession(rdram, ctx, runtime);
        ctx->pc = ra;
        return;
    }
    // derrota: o dialogo da aventura (CONTINUE recarrega a fase 1 da aventura, que o resolveArgsHook troca por esta luta; QUIT vai ao
    // menu, onde o userintHook encerra a corrida)
    wr32(rdram, kMode, 1u);
    wr32(rdram, kLevelId, 1u);
    std::fprintf(stderr, "[arcade] derrota na luta %d: CONTINUE ou QUIT\n", g_run.fight + 1);
    if (wotm_coop::retryTest())
        wotm_coop::retryCross();   // WOTM_COOPRETRY=1: CONTINUE sem teclado
    wotm_modes4::tailDialog(rdram, ctx, runtime, 1, ra);
}
void evalChHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime) { evalFight(g_evalCh, rdram, ctx, runtime); }
void evalAiHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime) { evalFight(g_evalAi, rdram, ctx, runtime); }

// Fim de rodada do chefe (aventura): a vitoria encerra a corrida; o resto (renascer, o dialogo de derrota) e o do retail e do co-op
void evalStoryHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == g_evalStory.addr && on() && g_run.fight == kBoss)
    {
        const int32_t r = static_cast<int32_t>(GPR_U32(ctx, 5));
        const bool won = r == 0 || r == 4 ||
                         (r == 1 && call(rdram, ctx, runtime, fn::GetNumAIsAlive__7TheGame, {uint32_t(kGame)}) == 0u);
        if (won)
        {
            const uint32_t ra = GPR_U32(ctx, 31);
            const int d = dif(rdram);
            award(rdram, ctx, runtime, d * 1500 + 3000 + d * 3000, "chefe");
            finish(rdram, ctx, runtime);
            ending(rdram, ctx, runtime);
            ctx->pc = ra;
            return;
        }
    }
    g_evalStory.orig(rdram, ctx, runtime);
}

// Monster::takeDamage(this, float, bool, Monster *src): sem fogo amigo (jogador x jogador, CPU x CPU)
// lado do monstro m: 0 jogador, 1 CPU, -1 outro (veiculo, objeto da fase)
int sideOf(uint8_t *rdram, uint32_t m)
{
    if (wotm_modes4::playerOf(rdram, m, g_run.players) >= 0)
        return 0;
    const uint32_t slots = std::min<uint32_t>(rd32(rdram, kNumSlots), kMaxSlots);
    return m >= kSlots && m < kSlots + kSlotStride * slots && (m - kSlots) % kSlotStride == 0 ? 1 : -1;
}
void takeDamageHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == g_takeDamage.addr && on())
    {
        const uint32_t victim = GPR_U32(ctx, 4) & 0x1FFFFFFFu, src = GPR_U32(ctx, 6) & 0x1FFFFFFFu;
        if (src && victim != src)
        {
            const int v = sideOf(rdram, victim), s = sideOf(rdram, src);
            if (v >= 0 && v == s)
            {
                ctx->pc = GPR_U32(ctx, 31);
                return;
            }
        }
    }
    g_takeDamage.orig(rdram, ctx, runtime);
}

// userintMain: os menus abriram (QUIT, o fim da corrida); a corrida acabou
void userintHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == g_userint.addr && on())
    {
        g_on = false;
        std::fprintf(stderr, "[arcade] de volta aos menus na luta %d; tokens %u\n", g_run.fight + 1, rd32(rdram, kTokens));
    }
    g_userint.orig(rdram, ctx, runtime);
}
}

void wotmArcadeInstall(PS2Runtime &runtime)
{
    using namespace wotm_arcade;
    const std::pair<Hook *, PS2Runtime::RecompiledFunction> hooks[] = {
        {&g_resolveArgs, &resolveArgsHook}, {&g_lives, &livesHook},       {&g_aiGlobal, &aiGlobalHook},
        {&g_lifeDeath, &lifeDeathHook},     {&g_evalCh, &evalChHook},     {&g_evalAi, &evalAiHook},
        {&g_evalStory, &evalStoryHook},     {&g_takeDamage, &takeDamageHook}, {&g_userint, &userintHook}};
    for (const auto &[h, f] : hooks)
    {
        if (!(h->orig = runtime.lookupFunction(h->addr)))
        {
            std::fprintf(stderr, "[arcade] indisponivel: funcao 0x%08X sem gancho no runtime\n", h->addr);
            return;
        }
    }
    for (const auto &[h, f] : hooks)
        runtime.registerFunction(h->addr, f);
    if (const char *e = std::getenv("WOTM_ARCADE"); e && *e)
        std::fprintf(stderr, "[arcade] corrida na primeira sessao (WOTM_ARCADE=%s)\n", e);
}
