// Eliminacao e minigames com 3 e 4 jogadores (NATIVE.md, "Eliminacao e minigames com 3 e 4"). Incluido pelo wotm_overrides.cpp depois do
// wotm_split4.inc (tela em quadrantes, carga e controles dos jogadores 3 e 4), cujos ganchos estes embrulham. Age so com 3 ou 4
// jogadores no Shell (m_numPlayers), postos pelo menu multijogador (wotm_padsel.inc); com 1 ou 2 tudo e o retail.
//
// Eliminacao (modo 6). O retail guarda continues e vitorias em campos de 2 (Shell::m_continues[2], m_wins[2]) e le o do jogador
// pelo m_index (0 = jogador 1, outro = jogador 2). Aqui cada jogador tem os seus (g_cont, g_wins), e quem fica sem continues sai
// (g_out) enquanto houver mais de um na luta:
//   - StateDeath::transitionInto: o retail, com os continues do morto em 0, junta a tela na vitima e poe o matador em vitoria (fim
//     da partida). Durante a chamada os dois continues do Shell valem 0 so quando o morto nao tem continues e sobra um jogador; senao
//     1 (a luta segue). A morte credita uma vitoria ao matador jogador (o +1 do retail no m_winsThisGame vira o g_wins).
//   - StateVictory::transitionOK: a pose de vitoria so vale quando os outros ja sairam (os continues do Shell como acima).
//   - TheGame::gameResolveLifeAndDeath: morto (m_unkF6) com continues volta ao Shell (rtReturnToShell 1, como o retail); sem
//     continues e com mais de um na luta, sai (monstro escondido, a camera dele passa a seguir quem continua); o ultimo volta ao Shell
//     quando o matador acabou a vitoria.
//   - EvaluateMultiPlayerBattleStatusNoAI: cada morto com continues gasta um e escolhe outro monstro no dialogo 8 do retail
//     (selectNextMultPlayer, pelo controle do m_won[1]); depois SetPlayerMonster e playerInit como o retail. Sobrou um: dialogo 0xB
//     ("PLAYER N WINS", m_survivor); nenhum: dialogo 6. O displayDialog e modal (roda quadros e cede ao escalonador): e chamado como
//     cauda com o ra num trampolim (kTramp, um endereco que o jogo nunca executa) que faz o resto e volta ao ra de verdade, tanto
//     quando o dialogo volta direto quanto quando a thread foi retomada pelo despachante.
//   - finishedMultPlayElim ("play again"): o retail volta os monstros dos jogadores 1 e 2 aos do comeco (Shell+0x2B84/0x2B88) e os
//     marca usados (resetSelecteds); os dos jogadores 3 e 4 vem do g_start. InitPlayers zera o estado (continues = m_elimination).
//
// Dodgeball (modo 7, tela em quadrantes): o retail conta os acertos de cada jogador no inicio do DodgeBallLevel (2 inteiros pelo
// m_index; o 3o derruba). Os jogadores 3 e 4 escreveriam no contador da bola: os acertos deles ficam em g_hits (takeDamage com o
// contador e o m_index trocados durante a chamada). O update(vista) das vistas 2 e 3 so escreve "P3"/"P4" (o resto do update ja
// roda nas vistas 0 e 1). O fim continua o do retail: o terceiro acerto derruba e o autor ganha.
//
// Crush (modo 9, uma vista): placar de 4 (P3 e P4 em cima), vencedor pelo maior total dos 4 TokenManager (TheGame tem 4), empate no
// topo vai a morte subita com todos. A camera do crush enquadra um par: durante o crushUpdate o par e o dos jogadores mais distantes.
//
// Big Shot (modo 8, por turnos): o BigShotLevel tem duas vagas (monstro +0x6C/+0x70, pontos +0x9C/+0xA0, nome +0x74/+0x84) e o
// turno alterna entre elas (swapPlayers). Aqui os jogadores fazem rodizio: no fim de cada turno a vaga que entra recebe o proximo
// jogador e os pontos dele; sao 3 turnos por jogador (o retail termina no 6o) e no ultimo as vagas ficam com os dois melhores, que o
// retail compara (empate: o "king of the hill" entre eles). O initForReplay roda uma vez por jogador extra na vaga 2 (esconde e prepara
// como o jogador 2) e o placar de 4 substitui o "P1"/"P2".
//
// Os dialogos de fim dos minigames (finishedDodgeBall, finishedCrush, finishedBigShot) ganham as linhas "Player 3/4 - scored".

namespace wotm_modes4
{
using wotm_split4::kGame;
using wotm_split4::kShell;
using wotm_split4::rdF;
using wotm_split4::wrF;

constexpr uint32_t kMonsters = kGame + 0x120380, kGameMode = kGame + 0x1203C8, kWon0 = kGame + 0x12043C, kWon1 = kGame + 0x120440;
constexpr uint32_t kNumPlayers = kShell + 0x2BB4, kMonsterSel = kShell + 0x2938, kContinues = kShell + 0x2B40, kElimination = kShell + 0x2B48,
                   kSurvivor = kShell + 0x2B58, kRestart = kShell + 0x2BA8, kInSession = kShell + 0x2BCC, kInMenus = kShell + 0x2BD0,
                   kElimStart = kShell + 0x2B84;
// Monster
constexpr uint32_t kCs = 0xC, kIndex = 0x24, kWins = 0x3C, kDead = 0xE8, kSudden = 0xEC, kOut = 0xF6, kVictorious = 0xF7, kF9 = 0xF9,
                   kHealth = 0x44C, kCrushCam = 0x6728, kIdleState = 0x7984, kVictoryState = 0x10E70;
constexpr uint32_t kStateOwner = 0xC, kDeathKiller = 0x1C;   // MonsterState: dono; StateDeath: matador
constexpr uint32_t kTokens = kGame + 0x11C370, kTokenStride = 0x1004;
constexpr uint32_t kSelectMode = wotm::sym::monsterSelectMode, kSelectCol = 0xB0;   // SelectSlot (coluna 0xB0, traje 0x2C): taken +0
constexpr uint32_t kDodge = wotm::sym::x_14DodgeBallLevel_instance, kCrush = wotm::sym::x_10CrushLevel_instance,
                   kBigShot = wotm::sym::x_12BigShotLevel_instance;
constexpr uint32_t kScratch = PS2_RAM_SIZE - 0x800;   // textos para as funcoes de fonte (8 x 0x80; os ViewDef do split4 ficam abaixo)
constexpr uint32_t kTramp = wotm::fn::EvaluateOnlineBattleStatus__5Shelli + 4;   // modo 11 (online) nunca roda; o +4 e o nop do jr ra

namespace fn = wotm::fn;

uint8_t rd8(uint8_t *rdram, uint32_t a) { return FAST_READ8(a & 0x1FFFFFFFu); }
uint32_t rd32(uint8_t *rdram, uint32_t a) { return FAST_READ32(a & 0x1FFFFFFFu); }
void wr32(uint8_t *rdram, uint32_t a, uint32_t v) { FAST_WRITE32(a & 0x1FFFFFFFu, v); }
void wr8(uint8_t *rdram, uint32_t a, uint8_t v) { FAST_WRITE8(a & 0x1FFFFFFFu, v); }
uint32_t monster(uint8_t *rdram, int i) { return rd32(rdram, kMonsters + 4u * uint32_t(i)) & 0x1FFFFFFFu; }

// Jogadores agora (3 ou 4; 0 = nao vale)
int players(uint8_t *rdram)
{
    const int32_t n = static_cast<int32_t>(rd32(rdram, kNumPlayers));
    return wotm_split4::g_players && n >= 3 && n <= 4 ? n : 0;
}
int gameMode(uint8_t *rdram) { return static_cast<int32_t>(rd32(rdram, kGameMode)); }
// Jogador (0..3) dono do monstro m, ou -1
int playerOf(uint8_t *rdram, uint32_t m, int n)
{
    m &= 0x1FFFFFFFu;
    for (int i = 0; i < n && m; ++i)
        if (monster(rdram, i) == m)
            return i;
    return -1;
}

// Funcao do jogo com ate 6 argumentos inteiros (a0..a3, t0, t1); devolve o v0. O contexto volta como estava.
uint32_t call(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t addr, std::initializer_list<uint32_t> args)
{
    const PS2Runtime::RecompiledFunction f = runtime->lookupFunction(addr);
    if (!f)
        return 0;
    const R5900Context saved = *ctx;
    int r = 4;
    for (uint32_t a : args)
        ctx->r[r++] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(a)));
    ctx->r[31] = _mm_set_epi64x(0, 0);
    ctx->pc = addr;
    f(rdram, ctx, runtime);
    wotmFinishGuestCall(rdram, ctx, runtime);
    const uint32_t v0 = GPR_U32(ctx, 2);
    *ctx = saved;
    return v0;
}

// Texto pelas funcoes de fonte do jogo (o desenho nativo o pega como o do HUD): tamanho dos placares dos minigames
uint32_t scratchText(uint8_t *rdram, const char *s)
{
    static uint32_t next = 0;
    const uint32_t a = kScratch + 0x80u * (next++ & 7u);
    const size_t n = std::min<size_t>(std::strlen(s), 0x7F);
    std::memcpy(rdram + a, s, n);
    rdram[a + n] = 0;
    return a;
}
enum Align { Left, Right, Center };
void print(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, int x, int y, Align al, const char *s)
{
    const uint32_t f = al == Left ? fn::fontSpritePrintXY__FiiiPc : al == Right ? fn::fontSpritePrintRightXY__FiiiPc : fn::fontSpritePrintCenteredXY__FiiiPc;
    call(rdram, ctx, runtime, f, {0u, uint32_t(x), uint32_t(y), scratchText(rdram, s)});
}
void scoreFont(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, int r, int g, int b)
{
    call(rdram, ctx, runtime, fn::fontSetCharSizesInSubPixels__Fiiiii, {0u, 0x280u, 0x140u, uint32_t(-0x11), uint32_t(-0x15)});
    call(rdram, ctx, runtime, fn::fontSetColor__Fiiiii, {0u, uint32_t(r), uint32_t(g), uint32_t(b), 0x80u});
}
void defaultFont(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    call(rdram, ctx, runtime, fn::fontSetDefaultSize__Fi, {0u});
    call(rdram, ctx, runtime, fn::fontSetDefaultColor__Fi, {0u});
}
// Cores dos placares: P1 vermelho e P2 azul (as do retail), P3 verde, P4 amarelo
constexpr int kScoreRgb[4][3] = {{0xFF, 0, 0}, {0, 0, 0xFF}, {0, 0xC0, 0}, {0xE0, 0xE0, 0}};
// "P<i+1> <valor>" no canto do jogador i numa vista de 640 (P1/P3 a esquerda, P2/P4 a direita; 1 e 2 embaixo como o retail);
// dim: na metade da cor (quem espera a vez no big shot)
void cornerScore(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, int i, const char *value, int yTop, bool dim = false)
{
    char buf[48];
    std::snprintf(buf, sizeof buf, "P%d %s", i + 1, value);
    const int k = dim ? 2 : 1;
    scoreFont(rdram, ctx, runtime, kScoreRgb[i][0] / k, kScoreRgb[i][1] / k, kScoreRgb[i][2] / k);
    print(rdram, ctx, runtime, (i & 1) ? 0x271 : 0x20, i < 2 ? 0xC8 : yTop, (i & 1) ? Right : Left, buf);
}
// Linhas "Player N - scored X" dos jogadores 3 e 4 abaixo das do retail (dialogos de fim dos minigames)
void extraScoreLines(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, int n, const int *score)
{
    call(rdram, ctx, runtime, fn::setTitleFont__Fv, {});
    for (int i = 2; i < n; ++i)
    {
        char buf[48];
        std::snprintf(buf, sizeof buf, "Player %d - scored %d", i + 1, score[i]);
        print(rdram, ctx, runtime, 0x140, 0xAA + 10 * i, Center, buf);
    }
}

// Ganchos em geral: o original guardado na instalacao (embrulha o que ja estava registrado)
struct Hook
{
    uint32_t addr;
    PS2Runtime::RecompiledFunction orig;
};
void callOrig(const Hook &h, uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime) { h.orig(rdram, ctx, runtime); }
// O original de um gancho com argumentos, como o call (sem passar pelo proprio gancho)
void callOrigWith(const Hook &h, uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, std::initializer_list<uint32_t> args)
{
    const R5900Context saved = *ctx;
    int r = 4;
    for (uint32_t a : args)
        ctx->r[r++] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(a)));
    ctx->r[31] = _mm_set_epi64x(0, 0);
    ctx->pc = h.addr;
    h.orig(rdram, ctx, runtime);
    wotmFinishGuestCall(rdram, ctx, runtime);
    *ctx = saved;
}

// ------------------------------------------------------------------ eliminacao

int g_cont[4] = {}, g_wins[4] = {};
bool g_out[4] = {};
uint32_t g_start[4] = {};   // monstro do comeco de cada jogador (o "play again" volta a ele)

bool elimActive(uint8_t *rdram) { return gameMode(rdram) == 6 && players(rdram); }

void refreshWins(uint8_t *rdram, int n)
{
    for (int i = 0; i < n; ++i)
        if (const uint32_t m = monster(rdram, i))
            wr32(rdram, m + kWins, uint32_t(g_wins[i]));
}

int monsterSlot(uint32_t sel)   // monsterIdtomonsterSlot (ShellFinished): coluna da selecao de cada tipo
{
    switch (sel)
    {
    case 0x120: return 0;
    case 0x60: return 1;
    case 0xA0: return 2;
    case 0x20: return 3;
    case 0x40: return 4;
    case 0x140: return 5;
    case 0x80: return 6;
    case 0x100: return 7;
    case 0xE0: return 8;
    case 0x160: return 9;
    default: return 10;
    }
}

// Os continues do Shell (lidos pelo m_index no retail) valendo `v` durante uma chamada
struct ShellContinues
{
    uint8_t *rdram;
    uint32_t saved[2];
    ShellContinues(uint8_t *r, uint32_t v) : rdram(r)
    {
        saved[0] = rd32(rdram, kContinues);
        saved[1] = rd32(rdram, kContinues + 4);
        wr32(rdram, kContinues, v);
        wr32(rdram, kContinues + 4, v);
    }
    ~ShellContinues()
    {
        wr32(rdram, kContinues, saved[0]);
        wr32(rdram, kContinues + 4, saved[1]);
    }
};

// Quantos continuam na luta alem de `except`
int inFight(int n, int except)
{
    int k = 0;
    for (int j = 0; j < n; ++j)
        k += j != except && !g_out[j];
    return k;
}

// Jogador i fora da partida: monstro escondido, a camera dele segue quem continua
void eliminate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, int i, int n)
{
    g_out[i] = true;
    const uint32_t m = monster(rdram, i);
    if (m)
    {
        call(rdram, ctx, runtime, fn::hierSetCsDrawMe__FP3_csUc, {rd32(rdram, m + kCs), 0u});
        call(rdram, ctx, runtime, fn::setShadowOnOff__7Monsterb, {m, 0u});
    }
    for (int j = 0; j < n; ++j)
        if (!g_out[j] && monster(rdram, j))
        {
            call(rdram, ctx, runtime, fn::SetCameraToFollowMonster__7CamerasiP7Monster, {uint32_t(i), monster(rdram, j)});
            break;
        }
    std::fprintf(stderr, "[modes4] eliminacao: jogador %d fora (sobram %d)\n", i + 1, inFight(n, -1));
}

Hook g_initPlayers{fn::InitPlayers__5Shell}, g_deathInto{fn::transitionInto__10StateDeath}, g_victoryOk{fn::transitionOK__12StateVictory},
    g_resolve{fn::gameResolveLifeAndDeath__7TheGame}, g_evalNoAI{fn::EvaluateMultiPlayerBattleStatusNoAI__5Shelli},
    g_finishedElim{fn::finishedMultPlayElim__FRi};

void initPlayersHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == g_initPlayers.addr && elimActive(rdram))
    {
        const int32_t c = std::max(0, static_cast<int32_t>(rd32(rdram, kElimination)));
        for (int i = 0; i < 4; ++i)
        {
            g_cont[i] = c;
            g_wins[i] = 0;
            g_out[i] = false;
        }
        std::fprintf(stderr, "[modes4] eliminacao de %d jogadores, %d continues cada\n", players(rdram), c);
    }
    callOrig(g_initPlayers, rdram, ctx, runtime);
}

void deathIntoHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t state = GPR_U32(ctx, 4) & 0x1FFFFFFFu;
    const int n = ctx->pc == g_deathInto.addr && elimActive(rdram) ? players(rdram) : 0;
    const int victim = n ? playerOf(rdram, rd32(rdram, state + kStateOwner), n) : -1;
    if (victim < 0)
    {
        callOrig(g_deathInto, rdram, ctx, runtime);
        return;
    }
    const int killer = playerOf(rdram, rd32(rdram, state + kDeathKiller), n);
    const bool ends = g_cont[victim] == 0 && inFight(n, victim) <= 1;
    const uint32_t ra = GPR_U32(ctx, 31);
    {
        ShellContinues sc(rdram, ends ? 0u : 1u);
        callOrig(g_deathInto, rdram, ctx, runtime);
    }
    if (ctx->pc != ra)
        return;
    if (killer >= 0 && killer != victim)
        ++g_wins[killer];
    refreshWins(rdram, n);
}

void victoryOkHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t self = GPR_U32(ctx, 4) & 0x1FFFFFFFu;
    const int n = ctx->pc == g_victoryOk.addr && elimActive(rdram) ? players(rdram) : 0;
    const int me = n ? playerOf(rdram, rd32(rdram, self + kStateOwner), n) : -1;
    if (me < 0)
    {
        callOrig(g_victoryOk, rdram, ctx, runtime);
        return;
    }
    bool done = true;   // os outros ja sairam (ou estao mortos sem continues)
    for (int j = 0; j < n; ++j)
        if (j != me && !g_out[j])
            done = done && g_cont[j] == 0 && !(rdF(rdram, monster(rdram, j) + kHealth) > 0.f);
    ShellContinues sc(rdram, done ? 0u : 1u);
    callOrig(g_victoryOk, rdram, ctx, runtime);
}

void resolveHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc != g_resolve.addr || !elimActive(rdram))
    {
        callOrig(g_resolve, rdram, ctx, runtime);
        return;
    }
    const int n = players(rdram);
    const uint32_t game = GPR_U32(ctx, 4), ra = GPR_U32(ctx, 31);
    for (int i = 0; i < n; ++i)
    {
        const uint32_t m = monster(rdram, i);
        if (!m || g_out[i] || !rd8(rdram, m + kOut))
            continue;
        if (g_cont[i] > 0)
            call(rdram, ctx, runtime, fn::rtReturnToShell__Fii, {1u, 0x1Eu});
        else if (inFight(n, i) >= 2)
            eliminate(rdram, ctx, runtime, i, n);
        else
        {
            const uint32_t k = rd32(rdram, m + wotm_split4::kKiller) & 0x1FFFFFFFu;
            if (!k)
                call(rdram, ctx, runtime, fn::rtReturnToShell__Fii, {1u, 0x1Eu});
            else if (rd8(rdram, k + kVictorious))
            {
                call(rdram, ctx, runtime, fn::rtReturnToShell__Fii, {1u, 0x1Eu});
                call(rdram, ctx, runtime, fn::fadeOut__7TheGamei, {game, 4u});
            }
        }
    }
    ctx->pc = ra;
}

// O selectNextMultPlayer procura o primeiro monstro livre (SelectSlot traje 0 com state 1 e taken 1) num laco sem fim: sem nenhum
// livre (partida sem o menu, ou todos ja usados) os 10 da eliminacao voltam a ficar livres, menos os que os outros estao usando.
void ensurePool(uint8_t *rdram, int who, int n)
{
    for (uint32_t c = 0; c <= 10; ++c)
    {
        const uint32_t s = kSelectMode + kSelectCol * c;
        if (rd32(rdram, s + 4) == 1u && rd32(rdram, s) == 1u)
            return;
    }
    for (uint32_t c = 0; c < 10; ++c)
    {
        wr32(rdram, kSelectMode + kSelectCol * c + 4, 1u);
        wr32(rdram, kSelectMode + kSelectCol * c, 1u);
    }
    for (int j = 0; j < n; ++j)
        if (j != who && !g_out[j])
            if (const int c = monsterSlot(rd32(rdram, kMonsterSel + 4u * uint32_t(j))); c < 10)
                wr32(rdram, kSelectMode + kSelectCol * uint32_t(c), 0u);
    std::fprintf(stderr, "[modes4] eliminacao: monstros livres de novo para o dialogo 8\n");
}

// Fila de jogadores que escolhem monstro novo (dialogo 8), um de cada vez
Hook g_playerInit{fn::playerInit__7Monster};   // so lido (nao e gancho)
int g_queue[4] = {}, g_queued = 0, g_cur = -1;
uint32_t g_contRa = 0;

void elimAfterDialog(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, int i)
{
    if (!rd32(rdram, kInSession))   // "quit" no dialogo
        return;
    const uint32_t sel = rd32(rdram, kMonsterSel + 4u * uint32_t(i));
    call(rdram, ctx, runtime, fn::SetPlayerMonster__7TheGameiiiii, {kGame, uint32_t(i), sel, 0u, uint32_t(i), i == 0 ? rd32(rdram, kShell + 0x2BE0) : 0u});
    // o playerInit do jogo, guardado na instalacao: o do WOTM_AIVSAI (aiInit) deixava sem estado o monstro trocado no meio da luta
    if (const uint32_t m = monster(rdram, i))
    {
        callOrigWith(g_playerInit, rdram, ctx, runtime, {m});
        wr32(rdram, m + 0x672C, 0);   // esquema de controle tipo A (o do jogo, para os jogadores 3 e 4, vinha do m_vibration; wotm_pausemenu.inc)
    }
    refreshWins(rdram, players(rdram));
    std::fprintf(stderr, "[modes4] eliminacao: jogador %d volta com o monstro %x (%d continues)\n", i + 1, sel, g_cont[i]);
}

void elimNext(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    while (g_queued > 0 && rd32(rdram, kInSession))
    {
        const int i = g_queue[0];
        for (int k = 1; k < g_queued; ++k)
            g_queue[k - 1] = g_queue[k];
        --g_queued;
        g_cur = i;
        --g_cont[i];
        ensurePool(rdram, i, players(rdram));
        wr32(rdram, kWon1, uint32_t(i));   // o selectNextMultPlayer le o controle e o "Player N" daqui
        ctx->r[4] = _mm_set_epi64x(0, 8);
        ctx->r[31] = _mm_set_epi64x(0, kTramp);
        ctx->pc = fn::displayDialog__Fi;
        runtime->lookupFunction(fn::displayDialog__Fi)(rdram, ctx, runtime);
        if (ctx->pc != kTramp)
            return;   // a thread cedeu no meio do dialogo: o trampolim continua daqui quando ele terminar
        g_cur = -1;
        elimAfterDialog(rdram, ctx, runtime, i);
    }
    g_queued = 0;
    g_cur = -1;
    wr32(rdram, kGame + 0x120460, 1u);   // como o retail no fim do modo 6
    wr32(rdram, kGame + 0x120458, 0u);
    ctx->pc = g_contRa;
}

void trampoline(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (g_cur >= 0)
    {
        const int i = g_cur;
        g_cur = -1;
        elimAfterDialog(rdram, ctx, runtime, i);
    }
    elimNext(rdram, ctx, runtime);
}

// Dialogo modal como cauda (volta ao ra de quem chamou o Evaluate)
void tailDialog(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, int which, uint32_t ra)
{
    ctx->r[4] = _mm_set_epi64x(0, which);
    ctx->r[31] = _mm_set_epi64x(0, ra);
    ctx->pc = fn::displayDialog__Fi;
    runtime->lookupFunction(fn::displayDialog__Fi)(rdram, ctx, runtime);
}

void evalNoAIHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc != g_evalNoAI.addr || !elimActive(rdram))
    {
        callOrig(g_evalNoAI, rdram, ctx, runtime);
        return;
    }
    const int n = players(rdram);
    const int32_t r = static_cast<int32_t>(GPR_U32(ctx, 5));
    const uint32_t ra = GPR_U32(ctx, 31);
    ctx->pc = ra;
    if (r == 3)
    {
        wr32(rdram, kInMenus, 0u);
        wr32(rdram, kInSession, 0u);
        return;
    }
    if (r != 1)
        return;
    // mortos sem continues saem; os outros mortos voltam
    g_queued = 0;
    for (int i = 0; i < n; ++i)
    {
        const uint32_t m = monster(rdram, i);
        if (!m || g_out[i] || rdF(rdram, m + kHealth) > 0.f)
            continue;
        if (g_cont[i] == 0)
            eliminate(rdram, ctx, runtime, i, n);
        else
            g_queue[g_queued++] = i;
    }
    int survivor = -1, left = 0;
    for (int i = 0; i < n; ++i)
        if (!g_out[i])
        {
            survivor = i;
            ++left;
        }
    if (left <= 1)
    {
        g_queued = 0;
        std::fprintf(stderr, "[modes4] eliminacao: fim, %s\n", left ? "sobrou um jogador" : "ninguem sobrou");
        if (left == 1)
            wr32(rdram, kSurvivor, uint32_t(survivor));
        tailDialog(rdram, ctx, runtime, left == 1 ? 0xB : 6, ra);
        return;
    }
    if (!g_queued)
        return;
    g_contRa = ra;
    elimNext(rdram, ctx, runtime);
}

// "play again" do dialogo 0xB: os jogadores 3 e 4 tambem voltam ao monstro do comeco, marcado como usado
void finishedElimHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t ra = ctx->pc == g_finishedElim.addr ? GPR_U32(ctx, 31) : 0u;
    callOrig(g_finishedElim, rdram, ctx, runtime);
    const int n = players(rdram);
    if (!ra || ctx->pc != ra || !n || GPR_U32(ctx, 2) != 1u || rd32(rdram, kRestart) != 1u || rd32(rdram, kInSession) != 0u)
        return;
    for (int i = 2; i < n; ++i)
        if (g_start[i])
        {
            wr32(rdram, kMonsterSel + 4u * uint32_t(i), g_start[i]);
            wr32(rdram, kSelectMode + kSelectCol * uint32_t(monsterSlot(g_start[i])), 0u);
        }
    std::fprintf(stderr, "[modes4] eliminacao: play again com os monstros do comeco\n");
}

// ------------------------------------------------------------------ dodgeball

int g_hits[4] = {};
Hook g_takeDamage{fn::takeDamage__7MonsterfbP7Monster}, g_dodgeUpdate{fn::update__14DodgeBallLeveli},
    g_dodgeInit{fn::initAfterDbLoad__14DodgeBallLevel}, g_finishedDodge{fn::finishedDodgeBall__FRi};

void takeDamageHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t src = GPR_U32(ctx, 5) & 0x1FFFFFFFu;
    const int n = ctx->pc == g_takeDamage.addr && gameMode(rdram) == 7 && src ? players(rdram) : 0;
    const int32_t idx = n ? static_cast<int32_t>(rd32(rdram, src + kIndex)) : 0;
    if (idx < 2 || idx >= n)
    {
        callOrig(g_takeDamage, rdram, ctx, runtime);
        return;
    }
    // o contador do jogador 1 (inicio do DodgeBallLevel) com os acertos deste e o m_index 0 durante a chamada
    const uint32_t ra = GPR_U32(ctx, 31), hits0 = rd32(rdram, kDodge);
    wr32(rdram, kDodge, uint32_t(g_hits[idx]));
    wr32(rdram, src + kIndex, 0u);
    callOrig(g_takeDamage, rdram, ctx, runtime);
    if (ctx->pc != ra)
        std::fprintf(stderr, "[modes4] dodgeball: takeDamage nao voltou ao chamador\n");
    g_hits[idx] = static_cast<int32_t>(rd32(rdram, kDodge));
    wr32(rdram, kDodge, hits0);
    wr32(rdram, src + kIndex, uint32_t(idx));
}

void dodgeUpdateHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t view = GPR_U32(ctx, 5);
    if (ctx->pc != g_dodgeUpdate.addr || view < 2u || !players(rdram))
    {
        callOrig(g_dodgeUpdate, rdram, ctx, runtime);
        return;
    }
    ctx->pc = GPR_U32(ctx, 31);
    if (int(view) >= players(rdram))
        return;
    // como o retail nas vistas 0 e 1: P3 a esquerda, P4 a direita (x da vista de 320)
    char buf[24];
    std::snprintf(buf, sizeof buf, "P%u %d", view + 1u, g_hits[view]);
    scoreFont(rdram, ctx, runtime, kScoreRgb[view][0], kScoreRgb[view][1], kScoreRgb[view][2]);
    print(rdram, ctx, runtime, (view & 1u) ? 0x12C : 0x20, 0xC8, (view & 1u) ? Right : Left, buf);
    defaultFont(rdram, ctx, runtime);
}

void dodgeInitHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t ra = ctx->pc == g_dodgeInit.addr ? GPR_U32(ctx, 31) : 0u;
    callOrig(g_dodgeInit, rdram, ctx, runtime);
    const int n = players(rdram);
    if (!ra || ctx->pc != ra || !n)
        return;
    for (int i = 0; i < 4; ++i)
        g_hits[i] = 0;
    for (int i = 2; i < n; ++i)
        if (const uint32_t m = monster(rdram, i))
            call(rdram, ctx, runtime, fn::drainSpecial__7Monster, {m});
}

void finishedDodgeHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int n = players(rdram);
    callOrig(g_finishedDodge, rdram, ctx, runtime);
    if (n)
    {
        const R5900Context saved = *ctx;
        extraScoreLines(rdram, ctx, runtime, n, g_hits);
        *ctx = saved;
    }
}

// ------------------------------------------------------------------ crush

int g_crushTotal[4] = {};
int g_crushLoser = -1;
Hook g_crushUpdate{fn::updateNormal__10CrushLevel}, g_crushInit{fn::initAfterDbLoad__10CrushLevel},
    g_crushSudden{fn::initSuddenDeath__10CrushLevel}, g_crushCam{fn::crushUpdate__6Camera}, g_finishedCrush{fn::finishedCrush__FRi};

int tokens(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, int i)
{
    return static_cast<int32_t>(call(rdram, ctx, runtime, fn::grandTotal__12TokenManager, {kTokens + kTokenStride * uint32_t(i)}));
}

void winsText(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, int winner)
{
    const uint32_t m = monster(rdram, winner);
    const uint32_t type = m ? (rd32(rdram, m + 0x14) >> 5) : 0u;
    char name[16] = {};
    if (type < 17)
        std::memcpy(name, rdram + wotm::sym::MonsterNewNames_006E8D90 + 12u * type, 12);
    char buf[40];
    std::snprintf(buf, sizeof buf, "%s WINS\n", name);
    call(rdram, ctx, runtime, fn::fontSetColor__Fiiiii, {0u, 0x5Au, 0x5Au, 0x5Au, 0x80u});
    call(rdram, ctx, runtime, fn::fontSetCharSizesInSubPixels__Fiiiii, {0u, 0x280u, 0x140u, uint32_t(-0x11), uint32_t(-0x15)});
    print(rdram, ctx, runtime, 0x140, 0xE, Center, buf);
    defaultFont(rdram, ctx, runtime);
}

// CrushLevel::updateNormal (antes da morte subita) com 3 e 4: o original desenha o tempo e P1/P2 enquanto ha tempo; o fim e nosso
void crushUpdateHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int n = ctx->pc == g_crushUpdate.addr ? players(rdram) : 0;
    if (!n)
    {
        callOrig(g_crushUpdate, rdram, ctx, runtime);
        return;
    }
    const uint32_t ra = GPR_U32(ctx, 31);
    const int32_t timer = static_cast<int32_t>(rd32(rdram, kCrush)), finished = static_cast<int32_t>(rd32(rdram, kCrush + 4));
    const int32_t dt = static_cast<int32_t>(call(rdram, ctx, runtime, fn::timerGetFieldsLastFrame__Fv, {}));
    if (!finished && timer - dt > 0)
    {
        callOrig(g_crushUpdate, rdram, ctx, runtime);
        if (ctx->pc != ra)
            return;
        for (int i = 2; i < n; ++i)
        {
            char v[16];
            std::snprintf(v, sizeof v, "%d", tokens(rdram, ctx, runtime, i));
            cornerScore(rdram, ctx, runtime, i, v, 0x1E);
        }
        defaultFont(rdram, ctx, runtime);
        return;
    }
    ctx->pc = ra;
    wr32(rdram, kCrush, uint32_t(timer - dt));
    if (!finished)
    {
        int best = 0, worst = 0, ties = 0;
        for (int i = 0; i < n; ++i)
        {
            g_crushTotal[i] = tokens(rdram, ctx, runtime, i);
            if (g_crushTotal[i] > g_crushTotal[best])
                best = i;
            if (g_crushTotal[i] < g_crushTotal[worst])
                worst = i;
        }
        for (int i = 0; i < n; ++i)
            ties += g_crushTotal[i] == g_crushTotal[best];
        wr32(rdram, kCrush + 8, uint32_t(g_crushTotal[0]));   // m_total do retail (o dialogo de fim mostra P1 e P2 daqui)
        wr32(rdram, kCrush + 0xC, uint32_t(g_crushTotal[1]));
        std::fprintf(stderr, "[modes4] crush: fim do tempo, %d/%d/%d/%d\n", g_crushTotal[0], g_crushTotal[1], g_crushTotal[2],
                     n > 3 ? g_crushTotal[3] : 0);
        if (ties > 1)
        {
            call(rdram, ctx, runtime, fn::initSuddenDeath__10CrushLevel, {kCrush});
            return;
        }
        for (int i = 0; i < n; ++i)
            if (i != best)
                wr8(rdram, monster(rdram, i) + kF9, 0);
        const uint32_t w = monster(rdram, best);
        wr8(rdram, w + kVictorious, 0);
        call(rdram, ctx, runtime, fn::enterNewState__7MonsterP12MonsterState, {w, w + kVictoryState});
        wr32(rdram, kWon0, uint32_t(best));
        wr32(rdram, kWon1, uint32_t(worst));
        g_crushLoser = worst;
        wr32(rdram, kCrush + 4, 1u);
    }
    winsText(rdram, ctx, runtime, static_cast<int32_t>(rd32(rdram, kWon0)));
}

void crushInitHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t ra = ctx->pc == g_crushInit.addr ? GPR_U32(ctx, 31) : 0u;
    callOrig(g_crushInit, rdram, ctx, runtime);
    const int n = players(rdram);
    if (!ra || ctx->pc != ra || !n)
        return;
    g_crushLoser = -1;
    for (int i = 2; i < n; ++i)
        if (const uint32_t m = monster(rdram, i))
        {
            wr32(rdram, m + kCrushCam, 1u);   // como o InitCrushMonsters faz com os dois
            call(rdram, ctx, runtime, fn::drainSpecial__7Monster, {m});
        }
}

void crushSuddenHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t ra = ctx->pc == g_crushSudden.addr ? GPR_U32(ctx, 31) : 0u;
    callOrig(g_crushSudden, rdram, ctx, runtime);
    const int n = players(rdram);
    if (!ra || ctx->pc != ra || !n)
        return;
    for (int i = 2; i < n; ++i)
        if (const uint32_t m = monster(rdram, i))
        {
            wr8(rdram, m + kSudden, 1);
            call(rdram, ctx, runtime, fn::drainSpecial__7Monster, {m});
        }
}

// Camera::crushUpdate enquadra o par em +0xE0C/+0xE10: com 3 e 4, o par dos jogadores mais distantes durante a chamada
void crushCamHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t cam = GPR_U32(ctx, 4) & 0x1FFFFFFFu;
    const int n = ctx->pc == g_crushCam.addr ? players(rdram) : 0;
    if (!n || gameMode(rdram) != 9)
    {
        callOrig(g_crushCam, rdram, ctx, runtime);
        return;
    }
    int a = 0, b = 1;
    float far = -1.f;
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
        {
            const uint32_t ci = rd32(rdram, monster(rdram, i) + kCs) & 0x1FFFFFFFu, cj = rd32(rdram, monster(rdram, j) + kCs) & 0x1FFFFFFFu;
            if (ci < 0x1000u || cj < 0x1000u)
                continue;
            const float dx = rdF(rdram, ci + 0x10) - rdF(rdram, cj + 0x10), dy = rdF(rdram, ci + 0x14) - rdF(rdram, cj + 0x14),
                        dz = rdF(rdram, ci + 0x18) - rdF(rdram, cj + 0x18);
            const float d = dx * dx + dy * dy + dz * dz;
            if (d > far)
            {
                far = d;
                a = i;
                b = j;
            }
        }
    const uint32_t pa = rd32(rdram, cam + 0xE0C), pb = rd32(rdram, cam + 0xE10), ra = GPR_U32(ctx, 31);
    wr32(rdram, cam + 0xE0C, monster(rdram, a));
    wr32(rdram, cam + 0xE10, monster(rdram, b));
    callOrig(g_crushCam, rdram, ctx, runtime);
    if (ctx->pc != ra)
        return;
    wr32(rdram, cam + 0xE0C, pa);
    wr32(rdram, cam + 0xE10, pb);
}

void finishedCrushHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int n = players(rdram);
    if (n && g_crushLoser < 0)   // morte subita: quem caiu (o gameResolveLifeAndDeath poe "o outro do jogador 1")
        for (int i = 0; i < n && g_crushLoser < 0; ++i)
            if (const uint32_t m = monster(rdram, i); m && !(rdF(rdram, m + kHealth) > 0.f))
                g_crushLoser = i;
    if (n && g_crushLoser >= 0)
        wr32(rdram, kWon1, uint32_t(g_crushLoser));
    callOrig(g_finishedCrush, rdram, ctx, runtime);
    if (n)
    {
        const R5900Context saved = *ctx;
        extraScoreLines(rdram, ctx, runtime, n, g_crushTotal);
        *ctx = saved;
    }
}

// ------------------------------------------------------------------ big shot

constexpr uint32_t kBsSlot = kBigShot + 0x6C, kBsName = kBigShot + 0x74, kBsTurn = kBigShot + 0x94, kBsScore = kBigShot + 0x9C,
                   kBsPoints = kBigShot + 0xA4, kBsEnd = kBigShot + 0xC4, kBsWinnerSet = kBigShot + 0xC8, kBsThrows = kBigShot + 0xD0,
                   kBsTimer = kBigShot + 0xD4, kBsFade = kBigShot + 0xDC, kBsState = kBigShot + 0xE8;
constexpr int kBsTurnsEach = 3;   // o retail termina no 6o turno (3 de cada)
int g_bsOcc[2] = {0, 1}, g_bsNext = 1, g_bsTurns = 0;
float g_bsScore[4] = {};
int g_bsWinner = -1, g_bsLoser = -1;
uint8_t g_bsWait[16] = {};                          // posicao de espera (a do jogador 2 depois do initForReplay)
constexpr uint32_t kBsVec = PS2_RAM_SIZE - 0x3E0;   // _fvector para o setTrans (o do split4 fica em -0x400)
Hook g_bsReplay{fn::initForReplay__12BigShotLevel}, g_bsTakeTurns{fn::takeTurns__12BigShotLevel}, g_bsUpdate{fn::updateNormal__12BigShotLevel},
    g_finishedBs{fn::finishedBigShot__FRi};

void bsSetSlot(uint8_t *rdram, int slot, int player, float score)
{
    g_bsOcc[slot] = player;
    const uint32_t m = monster(rdram, player);
    wr32(rdram, kBsSlot + 4u * uint32_t(slot), m);
    wrF(rdram, kBsScore + 4u * uint32_t(slot), score);
    const uint32_t type = m ? (rd32(rdram, m + 0x14) >> 5) : 0u;   // nome (MonsterNewNames, como o initAfterDbLoad)
    std::memset(rdram + kBsName + 0x10u * uint32_t(slot), 0, 0x10);
    if (type < 17)
        std::memcpy(rdram + kBsName + 0x10u * uint32_t(slot), rdram + wotm::sym::MonsterNewNames_006E8958 + 12u * type, 12);
}

void bsReplayHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int n = ctx->pc == g_bsReplay.addr ? players(rdram) : 0;
    if (!n)
    {
        callOrig(g_bsReplay, rdram, ctx, runtime);
        return;
    }
    // o original prepara a vaga 1 escondida: uma vez com cada jogador extra, a ultima (a que fica) com o jogador 2
    const uint32_t self = GPR_U32(ctx, 4);
    for (int i = 0; i < n; ++i)
        if (const uint32_t m = monster(rdram, i))
            call(rdram, ctx, runtime, fn::hierSetCsDrawMe__FP3_csUc, {rd32(rdram, m + kCs), 1u});
    for (int i = 2; i < n; ++i)
    {
        wr32(rdram, kBsSlot + 4, monster(rdram, i));
        callOrigWith(g_bsReplay, rdram, ctx, runtime, {self});
    }
    wr32(rdram, kBsSlot + 4, monster(rdram, 1));
    callOrig(g_bsReplay, rdram, ctx, runtime);
    if (const uint32_t m = monster(rdram, 1), cs = m ? rd32(rdram, m + kCs) & 0x1FFFFFFFu : 0u; cs >= 0x1000u)
        std::memcpy(g_bsWait, rdram + cs + 0x10, 16);   // onde o original deixa quem espera a vez
    g_bsOcc[0] = 0;
    g_bsOcc[1] = 1;
    g_bsNext = 1;
    g_bsTurns = 0;
    for (float &s : g_bsScore)
        s = 0.f;
    g_bsWinner = g_bsLoser = -1;
    std::fprintf(stderr, "[modes4] big shot de %d jogadores: %d turnos cada\n", n, kBsTurnsEach);
}

void bsTakeTurnsHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int n = ctx->pc == g_bsTakeTurns.addr ? players(rdram) : 0;
    if (!n || static_cast<int32_t>(rd32(rdram, kBsTimer)) != 1 || rd32(rdram, kBsState) != 0u)
    {
        callOrig(g_bsTakeTurns, rdram, ctx, runtime);
        return;
    }
    // fim do turno neste quadro (o original troca as vagas no swapPlayers, ou compara no ultimo)
    const int t = rd32(rdram, kBsTurn) ? 1 : 0, p = g_bsOcc[t];
    const bool last = ++g_bsTurns >= kBsTurnsEach * n;
    if (!last)
    {
        const int q = g_bsNext;
        g_bsNext = (g_bsNext + 1) % n;
        bsSetSlot(rdram, t ^ 1, q, g_bsScore[q]);
        wr32(rdram, kBsThrows, 0u);   // so o ultimo turno tem 6 (o fim no original)
        const uint32_t ra = GPR_U32(ctx, 31);
        callOrig(g_bsTakeTurns, rdram, ctx, runtime);
        if (ctx->pc == ra)
            g_bsScore[p] = rdF(rdram, kBsScore + 4u * uint32_t(t));
        return;
    }
    // ultimo turno: o ponto deste quadro (o original o soma com pontos a correr e o timer impar) e os dois melhores nas vagas
    g_bsScore[p] = rdF(rdram, kBsScore + 4u * uint32_t(t)) + (rdF(rdram, kBsPoints) > 0.f ? 1.f : 0.f);
    wrF(rdram, kBsPoints, 0.f);
    int order[4] = {0, 1, 2, 3};
    std::stable_sort(order, order + n, [](int a, int b) { return g_bsScore[a] > g_bsScore[b]; });
    bsSetSlot(rdram, 0, order[0], g_bsScore[order[0]]);
    bsSetSlot(rdram, 1, order[1], g_bsScore[order[1]]);
    g_bsLoser = order[n - 1];
    // fora da disputa: no ponto de espera, parados e escondidos (o ultimo a jogar seguia caindo, e a queda no "king of the hill"
    // conta como derrota: decidia a morte subita)
    for (int i = 0; i < n; ++i)
        if (i != order[0] && i != order[1])
            if (const uint32_t m = monster(rdram, i))
            {
                std::memcpy(rdram + kBsVec, g_bsWait, 16);
                call(rdram, ctx, runtime, fn::setTrans__7MonsterR8_fvector, {m, kBsVec});
                call(rdram, ctx, runtime, fn::enterNewState__7MonsterP12MonsterState, {m, m + kIdleState});
                call(rdram, ctx, runtime, fn::setShadowOnOff__7Monsterb, {m, 0u});
                wr8(rdram, m + kF9, 0);
                call(rdram, ctx, runtime, fn::hierSetCsDrawMe__FP3_csUc, {rd32(rdram, m + kCs), 0u});
            }
    wr32(rdram, kBsThrows, 6u);
    std::fprintf(stderr, "[modes4] big shot: fim, %.0f/%.0f/%.0f/%.0f\n", g_bsScore[0], g_bsScore[1], g_bsScore[2], n > 3 ? g_bsScore[3] : 0.f);
    callOrig(g_bsTakeTurns, rdram, ctx, runtime);
}

void bsUpdateHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int n = ctx->pc == g_bsUpdate.addr ? players(rdram) : 0;
    if (!n)
    {
        callOrig(g_bsUpdate, rdram, ctx, runtime);
        return;
    }
    const uint32_t ra = GPR_U32(ctx, 31);
    const bool playing = rd32(rdram, kBsEnd) == 0u, full = rd32(rdram, kBsFade) == 0x80u, decided = rd32(rdram, kBsWinnerSet) != 0u;
    if (playing && full)
        wr32(rdram, kBsFade, 0x7Fu);   // sem o "P1"/"P2" do original (com o timer em 0 ele refaz o fade em 1/128 e volta a 0x80)
    callOrig(g_bsUpdate, rdram, ctx, runtime);
    if (ctx->pc != ra)
        return;
    if (playing && full && rd32(rdram, kBsFade) == 0x7Fu)
        wr32(rdram, kBsFade, 0x80u);
    if (!decided && rd32(rdram, kBsWinnerSet) != 0u)   // o original escolheu a vaga vencedora (m_won pelas vagas)
    {
        g_bsWinner = g_bsOcc[rd32(rdram, kWon0) ? 1 : 0];
        if (g_bsLoser == g_bsWinner || g_bsLoser < 0)
            g_bsLoser = g_bsOcc[rd32(rdram, kWon0) ? 0 : 1];
    }
    if (!playing || !full)
        return;
    const int active = g_bsOcc[rd32(rdram, kBsTurn) ? 1 : 0];
    for (int i = 0; i < n; ++i)
    {
        float s = g_bsScore[i];
        for (int k = 0; k < 2; ++k)
            if (g_bsOcc[k] == i)
                s = rdF(rdram, kBsScore + 4u * uint32_t(k));
        char v[24];
        std::snprintf(v, sizeof v, "%.0f", s);
        cornerScore(rdram, ctx, runtime, i, v, 0xB4, i != active);
    }
    defaultFont(rdram, ctx, runtime);
}

void finishedBsHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int n = players(rdram);
    if (n && g_bsWinner >= 0)   // fim pelos pontos: m_won e os pontos de P1/P2 pelos jogadores (a morte subita ja os poe assim)
    {
        wr32(rdram, kWon0, uint32_t(g_bsWinner));
        wr32(rdram, kWon1, uint32_t(g_bsLoser));
        wrF(rdram, kBsScore, g_bsScore[0]);
        wrF(rdram, kBsScore + 4, g_bsScore[1]);
    }
    callOrig(g_finishedBs, rdram, ctx, runtime);
    if (n)
    {
        int s[4] = {};
        for (int i = 0; i < 4; ++i)
            s[i] = int(g_bsScore[i] + 0.5f);
        const R5900Context saved = *ctx;
        extraScoreLines(rdram, ctx, runtime, n, s);
        *ctx = saved;
    }
}

// ------------------------------------------------------------------ instalacao

void install(PS2Runtime &runtime)
{
    if (!wotm_split4::g_players)
        return;
    struct H { Hook *h; PS2Runtime::RecompiledFunction fn; };
    const H hooks[] = {
        {&g_initPlayers, &initPlayersHook},   {&g_deathInto, &deathIntoHook},     {&g_victoryOk, &victoryOkHook},
        {&g_resolve, &resolveHook},           {&g_evalNoAI, &evalNoAIHook},       {&g_finishedElim, &finishedElimHook},
        {&g_takeDamage, &takeDamageHook},     {&g_dodgeUpdate, &dodgeUpdateHook}, {&g_dodgeInit, &dodgeInitHook},
        {&g_finishedDodge, &finishedDodgeHook}, {&g_crushUpdate, &crushUpdateHook}, {&g_crushInit, &crushInitHook},
        {&g_crushSudden, &crushSuddenHook},   {&g_crushCam, &crushCamHook},       {&g_finishedCrush, &finishedCrushHook},
        {&g_bsReplay, &bsReplayHook},         {&g_bsTakeTurns, &bsTakeTurnsHook}, {&g_bsUpdate, &bsUpdateHook},
        {&g_finishedBs, &finishedBsHook},
    };
    for (const H &h : hooks)
        if (!(h.h->orig = runtime.lookupFunction(h.h->addr)))
        {
            std::fprintf(stderr, "[modes4] desligado: funcao %x sem funcao no runtime\n", h.h->addr);
            return;
        }
    if (!(g_playerInit.orig = runtime.lookupFunction(g_playerInit.addr)))
        return;
    for (const H &h : hooks)
        runtime.registerFunction(h.h->addr, h.fn);
    runtime.registerFunction(kTramp, &trampoline);
    std::fprintf(stderr, "[modes4] eliminacao e minigames de 3 e 4 jogadores\n");
}
} // namespace wotm_modes4

// Selecao de monstros da eliminacao (wotm_padsel.inc): monstro do comeco de cada jogador (o "play again" volta a ele)
void wotmElimSetStart(int player, uint32_t sel)
{
    if (player >= 0 && player < 4)
        wotm_modes4::g_start[player] = sel;
}
