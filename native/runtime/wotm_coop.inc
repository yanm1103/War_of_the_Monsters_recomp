// Campanha (aventura, modo 1) com 2 a 4 jogadores (NATIVE.md, "Campanha com até 4 jogadores"). Incluido pelo wotm_overrides.cpp depois
// do wotm_modes4.inc, cujos ganchos (e os do wotm_split4.inc) estes embrulham. Vale com o m_mode 1 do Shell e 2 a 4 jogadores
// (m_numPlayers), postos pela linha ADVENTURE da lista multijogador (wotm_mainmenu.inc, wotm_padsel.inc); a aventura de 1 jogador do
// retail nunca tem m_numPlayers maior que 1, entao fica como era.
//   - Tela: o TheGame::gameReestablishViews so divide nos modos 3, 6 e 7. Durante a chamada o m_gameMode vale 3: com 2 jogadores as
//     metades do retail (e o toggle dos cinemas, que olha o m_numMonsters 2, funciona como no versus), com 3 e 4 os quadrantes do
//     wotm_split4.inc (o splitPlayers aceita o modo 1 com 3 e 4). Sem vista unificada (a camera de combate proximo juntaria dois
//     aliados): com 2 jogadores o SetOkToUnify deixa o m_camUnify de todos em 0, como o split4 faz com 3 e 4.
//   - Largada: os jogadores 2 a 4 (e quem renasce) ao lado do P1 (ou do primeiro aliado na fase), na linha do lado dele e virados para o
//     mesmo lado, com o chao (hdHatTest) conferido. Sem casa: o ponto da fase (no modo 1 a lista 0, de um ponto so, e a cratera do 3-Mile
//     um ponto fixo; dois monstros no mesmo ponto: o empurrao da NaN), e quem cai a menos de 120 de outro vai a um ponto em volta.
//   - Cinemas: quando o roteiro posiciona ou anima o P1, os outros jogadores vao ao lado dele e fazem a mesma animacao.
//   - Vidas: cada jogador com as suas (Shell::m_lives[i], o InitPlayerLives ja enche as 4). TheGame::gameResolveLifeAndDeath (caso
//     1): morto (m_unkF6) com mais de uma vida renasce ali mesmo (playerInit, aviso de vidas no HUD da vista dele), sem voltar ao Shell
//     nem esmaecer a tela; na ultima vida, com outro jogador na fase, sai ate o fim dela (monstro escondido); o ultimo volta ao Shell
//     (rtReturnToShell 1). Vitoria (fases sem roteiro de fim) quando as IAs saem: a pose e do primeiro jogador vivo.
//     Shell::EvaluateOnePlayerStoryStatus com r = 1 faz o mesmo; ninguem na fase: dialogo 1 (game over, como o retail), como cauda.
//   - Tela com quem sobrou: a vista v mostra o v-esimo jogador que continua (camera, m_cameraView, m_viewSlot); 1 em tela cheia, 2 nas
//     metades, 3 no layout de 3. O HUD (posicao das barras, icones dos outros jogadores) e o pick-up verde (enableHighlights) veem os
//     m_monsters nessa ordem durante a chamada.
//   - Fase 1: sem as caixas de dica nem os power-ups de tutorial.
//   - Toquio e a cratera do 3-Mile: o fim, a abducao e a ida ao ponto fixo valem para o primeiro que continua e levam os outros.
//   - Pontos: TokenManager::grandTotal chamado pelo EvaluateOnePlayerStoryStatus (pontos da fase, battle tokens) devolve os tokens do
//     jogador 1 vezes 1,2 (2 jogadores), 1,3 (3) ou 1,4 (4).
//   - Shell::InitPlayers (comeco da fase, retry, etapa de chefe): todos de volta a fase.

namespace wotm_coop
{
using wotm_modes4::call;
using wotm_modes4::monster;
using wotm_modes4::rd32;
using wotm_modes4::rd8;
using wotm_modes4::wr32;
using wotm_split4::kGame;
using wotm_split4::kShell;
using wotm_split4::rdF;
using wotm_split4::wrF;
namespace fn = wotm::fn;

constexpr uint32_t kNumPlayers = kShell + 0x2BB4, kMode = kShell + 0x2C10, kLives = kShell + 0x2BBC, kInSession = kShell + 0x2BCC;
constexpr uint32_t kMonsters = kGame + 0x120380, kGameMode = kGame + 0x1203C8, kLevelId = kGame + 0x1203D0, kNumAIs = kGame + 0x1203E0,
                   kSlots = kGame + 0xB80, kSlotStride = 0x11190, kNumSlots = kGame + 0x1203D4;
constexpr uint32_t kHudStride = 0x2E0, kViewSlot = kGame + 0x1203E8;
// Monster
constexpr uint32_t kCs = 0xC, kState = 0x34, kDead = 0xE8, kOut = 0xF6, kVictorious = 0xF7, kHealth = 0x44C, kCameraView = 0x6CD8,
                   kCamUnify = 0x7970, kVictoryState = 0x10E70;

// Jogadores da campanha agora (2 a 4; 0 = nao e campanha co-op)
int players(uint8_t *rdram)
{
    const int32_t n = static_cast<int32_t>(rd32(rdram, kNumPlayers));
    return wotm_split4::g_players && rd32(rdram, kMode) == 1u && n >= 2 && n <= 4 ? n : 0;
}

bool g_out[4] = {};
bool g_dbgLog = false;                                // diagnostico (WOTM_COOPLOG), no fim do arquivo
void dbgSchedule(const char *tag, uint32_t frames);
void dbgTick(uint8_t *rdram);
// O que o initPlayersHook deixou para depois do InitPlayers (pausado no meio: initPlayersAfter no okToUnifyHook)
struct InitAfter
{
    bool on = false, remapped = false, was[4] = {};
} g_initAfter;
void initPlayersAfter(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, const bool was[4], bool remapped);
uint32_t dbgRd16(uint8_t *rdram, uint32_t a) { return uint32_t(rdram[a & 0x1FFFFFFFu]) | uint32_t(rdram[(a + 1) & 0x1FFFFFFFu]) << 8; }

int inFight(int n, int except)
{
    int k = 0;
    for (int j = 0; j < n; ++j)
        k += j != except && !g_out[j];
    return k;
}

// Tela com quem sobrou (g_views vistas; 0 = a tela da largada). Quando alguem sai, a vista v passa a mostrar o jogador g_map[v] (os
// que continuam, em ordem): a camera v o segue (o SetCameraToFollowMonster grava m_cameraView = v, entao os controles e as mensagens
// do HUD vao junto) e o HUD v mostra a barra dele (m_viewSlot[v]). 1 jogador: tela cheia (vista 0 no viewDef[0], viewport 0);
// 2: as metades do retail (viewDef[1] e [2], viewports 1 e 2); 3 (de 4): o layout de 3 do split4 (faixa embaixo).
int g_views = 0, g_map[4] = {};
uint32_t g_viewSlotSaved[4] = {};

uint32_t layoutDef(int v)
{
    if (g_views == 1)
        return wotm::sym::viewDef;
    if (g_views == 2)
        return wotm::sym::viewDef + wotm_split4::kDefSize * uint32_t(1 + v);
    return wotm_split4::kQuadDefs + wotm_split4::kDefSize * uint32_t(v);
}
uint32_t layoutViewport(int v) { return g_views == 1 ? 0u : 1u + (uint32_t(v) & 1u); }

// Por quadro (okToUnify) e depois de cada mudanca: cameras nos jogadores do mapa e vistas no layout (o endCinema, o
// viewToggleSplitScreen e o split4 as refazem do jeito deles). Fora no cinema e na vista unificada (com 1 jogador vale sempre).
void applyLayout(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (!g_views || wotm_split4::g_cinemaFull || (g_views > 1 && rd32(rdram, wotm::sym::gUseUnifiedView)))
        return;
    for (int v = 0; v < g_views; ++v)
    {
        const uint32_t m = monster(rdram, g_map[v]), cam = wotm_split4::camera(v);
        if (m && (rd32(rdram, cam + wotm_split4::kCamMonster) & 0x1FFFFFFFu) != m)
        {
            wr32(rdram, cam + wotm_split4::kCamPov, 0u);   // o SetCameraToFollowMonster recarrega o POV corrente
            call(rdram, ctx, runtime, fn::SetCameraToFollowMonster__7CamerasiP7Monster, {uint32_t(v), m});
        }
        if (m >= kSlots)
            wr32(rdram, kViewSlot + 4u * uint32_t(v), (m - kSlots) / kSlotStride);
    }
    const uint32_t vi = wotm::sym::viewInfo;
    bool same = rd32(rdram, wotm::sym::viewNumViews) == uint32_t(g_views);
    for (int v = 0; v < g_views && same; ++v)
        same = rd32(rdram, vi + 0x14u * uint32_t(v) + 4) == layoutDef(v) && rd32(rdram, vi + 0x14u * uint32_t(v) + 8) == layoutViewport(v);
    if (same)
        return;
    wr32(rdram, wotm::sym::viewNumViews, uint32_t(g_views));
    for (int v = 0; v < g_views; ++v)
    {
        wr32(rdram, vi + 0x14u * uint32_t(v) + 8, layoutViewport(v));
        wotm_split4::applyDef(rdram, ctx, runtime, uint32_t(v), layoutDef(v));
        if (g_dbgLog)
        {
            const uint32_t d = layoutDef(v);
            std::fprintf(stderr, "[cooplog] applyDef vista %d def %x: planos0 %.2f/%.2f/%.2f %.2f/%.2f/%.2f pc %x\n", v, d, rdF(rdram, d + 0x30),
                         rdF(rdram, d + 0x34), rdF(rdram, d + 0x38), rdF(rdram, d + 0x40), rdF(rdram, d + 0x44), rdF(rdram, d + 0x48), ctx->pc);
        }
    }
}

// Jogador i fora ate o fim da fase: monstro escondido e a tela remontada com quem continua
void eliminate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, int i, int n)
{
    g_out[i] = true;
    wr32(rdram, kLives + 4u * uint32_t(i), 0u);
    if (const uint32_t m = monster(rdram, i))
    {
        call(rdram, ctx, runtime, fn::hierSetCsDrawMe__FP3_csUc, {rd32(rdram, m + kCs), 0u});
        call(rdram, ctx, runtime, fn::setShadowOnOff__7Monsterb, {m, 0u});
    }
    int r = 0, map[4] = {};
    for (int j = 0; j < n; ++j)
        if (!g_out[j] && monster(rdram, j))
            map[r++] = j;
    std::fprintf(stderr, "[coop] jogador %d sem vidas: fora ate o fim da fase (sobram %d)\n", i + 1, r);
    if (r < 1)
        return;
    if (!g_views)
        for (int v = 0; v < 4; ++v)
            g_viewSlotSaved[v] = rd32(rdram, kViewSlot + 4u * uint32_t(v));
    g_views = r;
    std::copy(map, map + 4, g_map);
    wotm_split4::g_coopViews = r;
    if (r == 3)
        wotm_split4::writeQuadDefs(rdram, 3);
    wr32(rdram, wotm::sym::viewNumViews, 0u);   // forca o applyLayout a refazer as vistas
    applyLayout(rdram, ctx, runtime);
    std::fprintf(stderr, "[coop] tela com %d vista(s):", r);
    for (int v = 0; v < r; ++v)
        std::fprintf(stderr, " %d=jogador %d", v, map[v] + 1);
    std::fprintf(stderr, "\n");
    char tag[48];
    std::snprintf(tag, sizeof tag, "depois da saida do P%d", i + 1);
    dbgSchedule(tag, 90);
}

bool placeBeside(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t m, int who, float ax, float ay, float az, float rx,
                 float ry, uint32_t mat);
void sideOf(uint8_t *rdram, uint32_t mat, float &rx, float &ry);
int anchorFor(uint8_t *rdram, int who, int n);

// Renasce o jogador i (playerInit: vida cheia, no lugar onde caiu) sem voltar ao Shell nem esmaecer a tela, e o poe ao lado do
// primeiro aliado que continua na fase (sem casa com chao ali, fica onde o playerInit o deixou)
void revive(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, int i, int lives)
{
    const uint32_t m = monster(rdram, i);
    wr32(rdram, kLives + 4u * uint32_t(i), uint32_t(lives));
    call(rdram, ctx, runtime, fn::playerInit__7Monster, {m});
    if (const int a = anchorFor(rdram, i, players(rdram)); a >= 0)
    {
        const uint32_t acs = rd32(rdram, monster(rdram, a) + kCs) & 0x1FFFFFFFu;
        float rx, ry;
        sideOf(rdram, acs + 0x20, rx, ry);
        const bool ok = placeBeside(rdram, ctx, runtime, m, i, rdF(rdram, acs + 0x10), rdF(rdram, acs + 0x14), rdF(rdram, acs + 0x18), rx, ry,
                                    acs + 0x20);
        std::fprintf(stderr, "[coop] jogador %d renasce %s do %d\n", i + 1, ok ? "ao lado" : "longe (sem casa com chao)", a + 1);
    }
    applyLayout(rdram, ctx, runtime);   // o playerInit pode ter devolvido a camera i ao jogador i
    const uint32_t hud = g_views ? rd32(rdram, m + kCameraView) : uint32_t(i);   // o HUD da vista dele
    if (lives <= 2 && hud < 4u)
        call(rdram, ctx, runtime, fn::addMessage__3Hudii, {kGame + kHudStride * hud, lives == 1 ? 0x26u : 0x25u, 0u});
    if (rd32(rdram, wotm::sym::gUseUnifiedView))
        call(rdram, ctx, runtime, fn::LeaveUnifiedView__7CamerasUi, {0xCu});
    std::fprintf(stderr, "[coop] jogador %d volta (%d vidas)\n", i + 1, lives);
}

struct Hook
{
    uint32_t addr;
    PS2Runtime::RecompiledFunction orig;
};

Hook g_reestablish{fn::gameReestablishViews__7TheGame}, g_okToUnify{fn::SetOkToUnify__7TheGame},
    g_startPoint{fn::gameGetStartPoint__7TheGameP7Monster}, g_resolve{fn::gameResolveLifeAndDeath__7TheGame},
    g_evalStory{fn::EvaluateOnePlayerStoryStatus__5Shelli}, g_grandTotal{fn::grandTotal__12TokenManager}, g_initPlayers{fn::InitPlayers__5Shell};

// ------------------------------------------------------------------ tela

void reestablishHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const bool on = ctx->pc == g_reestablish.addr && players(rdram) && rd32(rdram, kGameMode) == 1u;
    if (!on)
    {
        g_reestablish.orig(rdram, ctx, runtime);
        return;
    }
    const uint32_t ra = GPR_U32(ctx, 31);
    wr32(rdram, kGameMode, 3u);
    g_reestablish.orig(rdram, ctx, runtime);
    if (ctx->pc != ra)
        std::fprintf(stderr, "[coop] gameReestablishViews nao voltou ao chamador\n");
    wr32(rdram, kGameMode, 1u);
    static int s_logged = 0;
    if (s_logged != players(rdram))
    {
        s_logged = players(rdram);
        std::fprintf(stderr, "[coop] campanha de %d jogadores: tela dividida\n", s_logged);
    }
}

// Hud::update com a tela remontada. Ele escolhe a posicao das barras pelo m_numMonsters (2: metade da tela; outro: tela cheia, +0x100
// no HUD) e o addHealthIndicators poe um icone de vida para cada m_monsters[0 .. m_numMonsters) com m_playerNum != 0 que nao e o dono
// do HUD (os outros jogadores; as IAs vem de outra lista). Durante a chamada: m_numMonsters = vistas com 1 ou 2, m_monsters com quem
// continua na frente e quem saiu com m_playerNum 0 (sem icone). Se a chamada pausar no meio, tudo volta no comeco do quadro seguinte
// (okToUnify) ou antes dos ganchos de vida e morte.
constexpr uint32_t kNumMonsters = kGame + 0x1203D8, kPlayerNum = 0x18;
Hook g_hudUpdate{fn::update__3Hud};
struct HudSwap
{
    bool on = false;
    uint32_t numMonsters = 0, monsters[4] = {}, playerNum[4] = {};
} g_hudSwap;
void hudRestore(uint8_t *rdram)
{
    if (!g_hudSwap.on)
        return;
    g_hudSwap.on = false;
    wr32(rdram, kNumMonsters, g_hudSwap.numMonsters);
    for (uint32_t i = 0; i < 4; ++i)
        wr32(rdram, kMonsters + 4u * i, g_hudSwap.monsters[i]);
    for (uint32_t i = 0; i < 4; ++i)
        if (const uint32_t m = g_hudSwap.monsters[i] & 0x1FFFFFFFu; m >= kSlots)
            wr32(rdram, m + kPlayerNum, g_hudSwap.playerNum[i]);
}
void hudUpdateHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int n = ctx->pc == g_hudUpdate.addr && g_views ? players(rdram) : 0;
    if (!n)
    {
        g_hudUpdate.orig(rdram, ctx, runtime);
        return;
    }
    hudRestore(rdram);
    const uint32_t ra = GPR_U32(ctx, 31);
    g_hudSwap.on = true;
    g_hudSwap.numMonsters = rd32(rdram, kNumMonsters);
    for (uint32_t i = 0; i < 4; ++i)
    {
        g_hudSwap.monsters[i] = rd32(rdram, kMonsters + 4u * i);
        const uint32_t m = g_hudSwap.monsters[i] & 0x1FFFFFFFu;
        g_hudSwap.playerNum[i] = m >= kSlots ? rd32(rdram, m + kPlayerNum) : 0u;
    }
    if (g_views <= 2)
        wr32(rdram, kNumMonsters, uint32_t(g_views));
    int k = 0;
    for (int v = 0; v < g_views; ++v)
        wr32(rdram, kMonsters + 4u * uint32_t(k++), g_hudSwap.monsters[g_map[v]]);
    for (int j = 0; j < n; ++j)
        if (g_out[j])
        {
            wr32(rdram, kMonsters + 4u * uint32_t(k++), g_hudSwap.monsters[j]);
            if (const uint32_t m = g_hudSwap.monsters[j] & 0x1FFFFFFFu; m >= kSlots)
                wr32(rdram, m + kPlayerNum, 0u);
        }
    g_hudUpdate.orig(rdram, ctx, runtime);
    if (ctx->pc == ra)
        hudRestore(rdram);
}

// Hud::drawBossHealthBar(hud, pct): o addHealthIndicators de cada vista o chama com o HUD dela, e ele poe a barra (_cs em +0xFC do
// HUD) a (0, 170, -91) da camera 0 (na vista unificada, da 2). So o HUD 0 tem a barra (o +0xFC do HUD 1 e um _cs sem no). Na tela
// dividida ela tem a largura da tela cheia e sai pelas duas bordas da metade, e o desenho a descarta. Fora da vista unificada, durante
// a chamada o _cs da camera 0 vira o da camera da vista do HUD; depois a barra de verdade (o primeiro +0xFC com no) vai para onde a
// chamada pos a deste HUD, com a distancia ate a camera dobrada (metade do tamanho, mesma altura na tela), e acende ou apaga com ela.
// O rtMain desenha cada vista logo depois do addHealthIndicators dela.
constexpr uint32_t kCameras = wotm::sym::x_7Cameras_m_cameras, kCamStride = 0xEB0, kCamCs = 0xCF0, kHudBossCs = 0xFC;
Hook g_bossBar{fn::drawBossHealthBar__3Hudf};
void bossBarHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t hud = GPR_U32(ctx, 4) & 0x1FFFFFFFu;
    const int views = g_views ? g_views : std::min(players(rdram), 4);   // 0: a tela de todos os jogadores, sem ninguem fora
    const uint32_t v = ctx->pc == g_bossBar.addr && views > 1 && !rd32(rdram, wotm::sym::gUseUnifiedView) &&
                               hud >= kGame && hud < kGame + 4u * kHudStride ? rd32(rdram, hud) : ~0u;
    if (v >= uint32_t(views))
    {
        g_bossBar.orig(rdram, ctx, runtime);
        return;
    }
    const uint32_t cam0 = kCameras + kCamCs, camCs = rd32(rdram, kCameras + kCamStride * v + kCamCs) & 0x1FFFFFFFu, saved = rd32(rdram, cam0);
    if (camCs)
        wr32(rdram, cam0, camCs);
    g_bossBar.orig(rdram, ctx, runtime);
    wr32(rdram, cam0, saved);
    auto valid = [](uint32_t cs) { return cs >= 0x1000u && cs < 0x2000000u; };
    const uint32_t own = rd32(rdram, hud + kHudBossCs) & 0x1FFFFFFFu;
    uint32_t bar = 0;
    for (uint32_t j = 0; j < 4 && !bar; ++j)
        if (const uint32_t cs = rd32(rdram, kGame + kHudStride * j + kHudBossCs) & 0x1FFFFFFFu; valid(cs) && rd32(rdram, cs))
            bar = cs;
    if (!valid(own) || !bar || !camCs)
        return;
    for (uint32_t k = 0; k < 3; ++k)
    {
        const float eye = rdF(rdram, camCs + 0x10 + 4 * k);
        wrF(rdram, bar + 0x10 + 4 * k, eye + 2.f * (rdF(rdram, own + 0x10 + 4 * k) - eye));
    }
    if (bar != own)
    {
        std::memcpy(rdram + bar + 0x20, rdram + own + 0x20, 0x40);   // mat
        rdram[bar + 0xC] = rdram[own + 0xC];                          // drawMe
        rdram[own + 0xC] = 0;
    }
}

// Guarda do hdReparentCsGrid(cs, pos, raio): ele tira o cs das casas da caixa antiga (cs +0x98..+0xA4: x0, y0, x1, y1) que nao estao
// na nova, com um laco de x0 a x1. Caixa antiga com lixo ou posicao NaN/infinita trava o jogo nesse laco (fase 3 com 2 jogadores, o
// EE preso no hdReparentCsGrid). Na entrada: caixa absurda vira vazia e posicao invalida faz a chamada nao mexer em nada; as duas
// coisas dao uma linha [coop] com quem chamou (ate 16).
Hook g_reparent{fn::hdReparentCsGrid__FP3_csP8_fvectorf};
void reparentHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == g_reparent.addr)
    {
        static int s_logs = 0;
        const uint32_t cs = GPR_U32(ctx, 4) & 0x1FFFFFFFu, pos = GPR_U32(ctx, 5) & 0x1FFFFFFFu, ra = GPR_U32(ctx, 31);
        const float p[3] = {rdF(rdram, pos), rdF(rdram, pos + 4), rdF(rdram, pos + 8)};
        const int32_t b[4] = {int32_t(rd32(rdram, cs + 0x98)), int32_t(rd32(rdram, cs + 0x9C)), int32_t(rd32(rdram, cs + 0xA0)), int32_t(rd32(rdram, cs + 0xA4))};
        const bool badPos = !std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]) || std::fabs(p[0]) > 1e6f || std::fabs(p[1]) > 1e6f;
        const bool badBox = b[2] - int64_t(b[0]) > 256 || b[3] - int64_t(b[1]) > 256 || b[0] < -256 || b[1] < -256;
        if ((badPos || badBox) && s_logs++ < 16)
            std::fprintf(stderr, "[coop] grade: cs %x (ra %x) pos %g %g %g caixa %d %d %d %d%s\n", cs, ra, double(p[0]), double(p[1]), double(p[2]), b[0], b[1],
                         b[2], b[3], badPos ? ": posicao invalida, ignorado" : ": caixa refeita");
        if (badPos)
        {
            ctx->pc = ra;
            return;
        }
        if (badBox)
        {
            wr32(rdram, cs + 0x98, 1u); wr32(rdram, cs + 0x9C, 1u); wr32(rdram, cs + 0xA0, 0u); wr32(rdram, cs + 0xA4, 0u);
        }
    }
    g_reparent.orig(rdram, ctx, runtime);
}

// LevelPickups::update (0x181EC0), feito aqui com os mesmos passos. Os lacos do retail andam pela lista com o next guardado antes do
// update do pick-up (inUse e inFlight) ou lido depois (active): se o update de um tira outro no (ou o proprio) da lista (pegar,
// soltar, arremessar, matar), o laco segue pela outra lista e nunca volta a cabeca. Com mais de um jogador segurando e arremessando
// isso aconteceu na fase 3 (o EE preso no RigidPickup::updateActive). Aqui cada lista e fotografada antes e cada no so roda se ainda
// estiver nela na vez dele. Linha [coop] quando um no saiu da lista no meio da passada (ate 16).
constexpr uint32_t kPkActive = wotm::sym::x_12LevelPickups_s_activeList, kPkInUse = wotm::sym::x_12LevelPickups_s_inUseList,
                   kPkInFlight = wotm::sym::x_12LevelPickups_s_inFlightList, kPkRegen = wotm::sym::x_12LevelPickups_s_regenList;
Hook g_pickups{fn::update__12LevelPickups};
int snapList(uint8_t *rdram, uint32_t head, uint32_t *out, int max)
{
    int k = 0;
    for (uint32_t n = rd32(rdram, head + 4) & 0x1FFFFFFFu; n && n != head && k < max; n = rd32(rdram, n + 4) & 0x1FFFFFFFu)
        out[k++] = n;
    return k;
}
bool inList(uint8_t *rdram, uint32_t head, uint32_t node)
{
    int k = 0;
    for (uint32_t n = rd32(rdram, head + 4) & 0x1FFFFFFFu; n && n != head && k < 512; n = rd32(rdram, n + 4) & 0x1FFFFFFFu, ++k)
        if (n == node)
            return true;
    return false;
}
uint32_t callVirtual(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t obj, uint32_t slot)
{
    const uint32_t vt = rd32(rdram, obj + 0x10) & 0x1FFFFFFFu;
    const int16_t adj = int16_t(rd32(rdram, vt + slot - 4) & 0xFFFFu);
    return call(rdram, ctx, runtime, rd32(rdram, vt + slot), {obj + uint32_t(int32_t(adj))});
}
void pickupsHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc != g_pickups.addr)
    {
        g_pickups.orig(rdram, ctx, runtime);
        return;
    }
    static int s_logs = 0;
    auto gone = [&](const char *list, uint32_t n) {
        if (s_logs++ < 16)
            std::fprintf(stderr, "[coop] pick-ups: no %x saiu da lista %s no meio da passada\n", n, list);
    };
    uint32_t nodes[256];
    wr32(rdram, wotm::sym::x_12LevelPickups_s_militaryStillAlive, 0u);
    int k = snapList(rdram, kPkActive, nodes, 256);
    for (int i = 0; i < k; ++i)
    {
        if (!inList(rdram, kPkActive, nodes[i]))
        {
            gone("active", nodes[i]);
            continue;
        }
        if (!callVirtual(rdram, ctx, runtime, rd32(rdram, nodes[i]) & 0x1FFFFFFFu, 0x64))
            call(rdram, ctx, runtime, fn::killPickup__12LevelPickupsGQ2t10LinkedList1ZP6Pickup8Iteratori, {nodes[i], 1u});
    }
    for (const uint32_t head : {kPkInUse, kPkInFlight})
    {
        k = snapList(rdram, head, nodes, 256);
        for (int i = 0; i < k; ++i)
        {
            if (!inList(rdram, head, nodes[i]))
            {
                gone(head == kPkInUse ? "inUse" : "inFlight", nodes[i]);
                continue;
            }
            uint32_t p = rd32(rdram, nodes[i]) & 0x1FFFFFFFu;
            if (rdram[p + 0x50] & 4u)
                callVirtual(rdram, ctx, runtime, p, 0x64);
            p = rd32(rdram, nodes[i]) & 0x1FFFFFFFu;
            if (head == kPkInUse || rd32(rdram, p + 0xA0) != 27u)
                call(rdram, ctx, runtime, fn::updatePickupSound__11PickupSound, {p + 0x100u});
        }
    }
    k = snapList(rdram, kPkRegen, nodes, 256);
    for (int i = 0; i < k; ++i)
    {
        const uint32_t n = nodes[i];
        if (!inList(rdram, kPkRegen, n) || !call(rdram, ctx, runtime, fn::regenUpdate__6Pickup, {rd32(rdram, n)}))
            continue;
        const uint32_t next = rd32(rdram, n + 4), prev = rd32(rdram, n + 8);
        if (prev)
            wr32(rdram, (prev & 0x1FFFFFFFu) + 4, next);
        if (next)
            wr32(rdram, (next & 0x1FFFFFFFu) + 8, prev);
        wr32(rdram, n + 8, kPkActive);
        wr32(rdram, n + 4, rd32(rdram, kPkActive + 4));
        wr32(rdram, kPkActive + 4, n);
        if (const uint32_t nn = rd32(rdram, n + 4))
            wr32(rdram, (nn & 0x1FFFFFFFu) + 8, n);
        callVirtual(rdram, ctx, runtime, rd32(rdram, n) & 0x1FFFFFFFu, 0x3C);
    }
    ctx->pc = GPR_U32(ctx, 31);
}

// Goliath Prime (fase 3): o roteiro do chefe (Ai::updateAssBoss e os padroes que ele chama), o giro (StateTopSpin::update e
// acceptHit) e os misseis (StateCannonHands::update) miram game->m_monsters[0], e o Monster::takeHit do 0x1A0 so aceita golpe comum
// vindo dele (attackerId == m_monsters[0]->m_id). Alvo sorteado entre os jogadores vivos, de novo a cada Ai::setAssBossState do
// chefe (cada ataque) e quando o alvo morre ou sai; durante essas chamadas m_monsters[0] vale o alvo. No takeHit do chefe, golpe de
// qualquer jogador conta (o m_id do m_monsters[0] vale o do atacante durante a chamada). Linha [coop] Goliath quando o alvo muda.
constexpr uint32_t kId = 0x20, kStateOwner = 0xC;
Hook g_assBoss{fn::updateAssBoss__2Ai}, g_spinUpd{fn::update__12StateTopSpin}, g_spinHit{fn::acceptHit__12StateTopSpinR8HitEvent},
    g_cannon{fn::update__16StateCannonHands}, g_bossState{fn::setAssBossState__2Aii}, g_bossHit{fn::takeHit__7MonsterP8_fvectorfi};
int g_bossTarget = -1;
bool bossCoop(uint8_t *rdram, uint32_t owner)
{
    const uint32_t boss = rd32(rdram, wotm::sym::assBoss) & 0x1FFFFFFFu;
    return boss && (owner & 0x1FFFFFFFu) == boss && rd32(rdram, kGameMode) == 1u && players(rdram) > 1;
}
bool alive(uint8_t *rdram, int i)
{
    const uint32_t p = monster(rdram, i);
    return p && !g_out[i] && !rd8(rdram, p + kDead) && rdF(rdram, p + kHealth) > 0.f;
}
void rerollBossTarget(uint8_t *rdram)
{
    static uint32_t s_seed = 0x9E3779B9u;
    int live[4], k = 0;
    for (int i = 0, n = std::min(players(rdram), 4); i < n; ++i)
        if (alive(rdram, i))
            live[k++] = i;
    if (!k)
        return;
    s_seed ^= s_seed << 13, s_seed ^= s_seed >> 17, s_seed ^= s_seed << 5;
    const int t = live[s_seed % uint32_t(k)];
    static int s_logs = 0;
    if (t != g_bossTarget && s_logs++ < 64)
        std::fprintf(stderr, "[coop] Goliath: alvo jogador %d\n", t + 1);
    g_bossTarget = t;
}
uint32_t bossTarget(uint8_t *rdram)
{
    if (g_bossTarget < 0 || g_bossTarget >= players(rdram) || !alive(rdram, g_bossTarget))
        rerollBossTarget(rdram);
    return g_bossTarget >= 0 ? monster(rdram, g_bossTarget) : 0u;
}
void withBossTarget(Hook &h, uint32_t owner, uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t t = ctx->pc == h.addr && bossCoop(rdram, owner) ? bossTarget(rdram) : 0u, p1 = rd32(rdram, kMonsters);
    if (t && t != (p1 & 0x1FFFFFFFu))
        wr32(rdram, kMonsters, t);
    h.orig(rdram, ctx, runtime);
    if (t)
        wr32(rdram, kMonsters, p1);
}
void assBossHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    withBossTarget(g_assBoss, rd32(rdram, GPR_U32(ctx, 4) & 0x1FFFFFFFu), rdram, ctx, runtime);   // Ai::monster (+0)
}
void spinUpdHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    withBossTarget(g_spinUpd, rd32(rdram, (GPR_U32(ctx, 4) & 0x1FFFFFFFu) + kStateOwner), rdram, ctx, runtime);
}
void spinHitHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    withBossTarget(g_spinHit, rd32(rdram, (GPR_U32(ctx, 4) & 0x1FFFFFFFu) + kStateOwner), rdram, ctx, runtime);
}
void cannonHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    withBossTarget(g_cannon, rd32(rdram, (GPR_U32(ctx, 4) & 0x1FFFFFFFu) + kStateOwner), rdram, ctx, runtime);
}
void bossStateHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const bool on = ctx->pc == g_bossState.addr && bossCoop(rdram, rd32(rdram, GPR_U32(ctx, 4) & 0x1FFFFFFFu));
    g_bossState.orig(rdram, ctx, runtime);
    if (on)
        rerollBossTarget(rdram);
}
void bossHitHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t p1 = 0, oldId = 0;
    if (ctx->pc == g_bossHit.addr && bossCoop(rdram, GPR_U32(ctx, 4)))
    {
        const uint32_t attacker = GPR_U32(ctx, 6);
        for (int i = 0, n = std::min(players(rdram), 4); i < n && !p1; ++i)
            if (const uint32_t p = monster(rdram, i); p && rd32(rdram, p + kId) == attacker)
                p1 = rd32(rdram, kMonsters) & 0x1FFFFFFFu;
        if (p1)
        {
            oldId = rd32(rdram, p1 + kId);
            wr32(rdram, p1 + kId, attacker);
        }
    }
    g_bossHit.orig(rdram, ctx, runtime);
    if (p1)
        wr32(rdram, p1 + kId, oldId);
}

// Planta (3-Mile, fase 6): o PlantBoss::update e as pecas que ele chama (cabecas, raizes, base) miram game->m_monsters[0], e com o
// P1 morto ou fora o ciclo de ataques volta ao comeco a cada quadro (a planta para). Durante o update, m_monsters[0] vale o alvo:
// sorteado entre os jogadores vivos a cada passo do ciclo (m_14, m_18) e quando o alvo morre ou sai, nunca com uma cabeca segurando
// alguem. Raizes: cada uma nasce em volta de um jogador sorteado e segue esse jogador; na batida (estado 3) fere qualquer um perto
// da ponta (a mesma chamada, uma vez por jogador perto). Base: o tapa (estados 8 e 9) pega quem bateu nela. Mordida: o
// InitPlantBossCamera usa a camera 0; com o mordido em outra vista, as cameras 0 e a dele trocam de lugar durante a chamada, e a
// volta do POV 3 (0 ou 5, como o retail faz na camera 0) e feita aqui quando a cabeca o solta. Abertura e fim (m_08 > 0): sem troca,
// e o cleanUpForMovie do retail no P1 vale para todos. Linha [coop] planta.
constexpr uint32_t kPlant = wotm::sym::plantBoss, kPlantHeads = kPlant + 0x20, kHeadStride = 0x250, kPlantRoots = kPlant + 0x710,
                   kRootStride = 0xE0, kPlantBase = kPlant + 0xFD0;
Hook g_plantUpd{fn::update__9PlantBoss}, g_plantCam{fn::InitPlantBossCamera__7Cameras}, g_rootWake{fn::wakeUp__13PlantBossRooti},
    g_rootUpd{fn::update__13PlantBossRoot}, g_baseHit{fn::takeHit__13PlantBossBaseP8_fvectorfi}, g_baseUpd{fn::update__13PlantBossBase};
bool g_inPlant = false;
uint32_t g_plantP[4] = {};   // os jogadores durante o update (o m_monsters[0] esta trocado)
int g_plantN = 0, g_plantTarget = -1, g_rootOwner[10] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, g_smackee = -1, g_chompView = -1;
uint32_t g_chompMon = 0, g_plantCycle = ~0u;

uint32_t plantPlayer(uint8_t *rdram, int i) { return g_inPlant ? g_plantP[i] : monster(rdram, i); }
bool plantAlive(uint8_t *rdram, int i)
{
    const uint32_t p = i >= 0 && i < g_plantN ? g_plantP[i] : 0u;
    return p && !g_out[i] && !rd8(rdram, p + kDead) && rdF(rdram, p + kHealth) > 0.f;
}
int plantPick(uint8_t *rdram)
{
    static uint32_t s = 0x2545F491u;
    int live[4], k = 0;
    for (int i = 0; i < g_plantN; ++i)
        if (plantAlive(rdram, i))
            live[k++] = i;
    if (!k)
        return -1;
    s ^= s << 13, s ^= s >> 17, s ^= s << 5;
    return live[s % uint32_t(k)];
}
// Alguma cabeca com um jogador na boca (mordeu e ainda nao cuspiu)
bool plantHolding(uint8_t *rdram)
{
    for (uint32_t h = 0; h < 3; ++h)
    {
        const uint32_t p = kPlantHeads + kHeadStride * h, st = rd32(rdram, p + 0x18);
        if (rd32(rdram, p + 0x14) && rd32(rdram, p + 0x28) && ((st >= 2u && st <= 7u) || (st == 8u && rd32(rdram, p + 0x24) == 0u)))
            return true;
    }
    return false;
}
void swapCameras(uint8_t *rdram, uint32_t v)
{
    static uint8_t tmp[wotm_split4::kCamStride];
    uint8_t *a = rdram + wotm_split4::camera(0), *b = rdram + wotm_split4::camera(int(v));
    std::memcpy(tmp, a, sizeof tmp);
    std::memcpy(a, b, sizeof tmp);
    std::memcpy(b, tmp, sizeof tmp);
}

void plantUpdHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int n = ctx->pc == g_plantUpd.addr && !g_inPlant && rd32(rdram, kGameMode) == 1u && rd32(rdram, kPlant) ? players(rdram) : 0;
    if (n < 2)
    {
        g_plantUpd.orig(rdram, ctx, runtime);
        return;
    }
    g_plantN = n;
    for (int i = 0; i < 4; ++i)
        g_plantP[i] = i < n ? monster(rdram, i) : 0u;
    const int32_t m08 = static_cast<int32_t>(rd32(rdram, kPlant + 0x08));
    const bool movie = m08 > 0 && (rd32(rdram, kPlant + 0x0C) == 0u || static_cast<int32_t>(rd32(rdram, wotm::sym::numDeadHeads)) >= 2);
    const uint32_t cycle = rd32(rdram, kPlant + 0x14) * 64u + rd32(rdram, kPlant + 0x18);
    if (!plantHolding(rdram) && (cycle != g_plantCycle || !plantAlive(rdram, g_plantTarget)))
    {
        const int t = plantPick(rdram);
        static int s_logs = 0;
        if (t >= 0 && t != g_plantTarget && s_logs++ < 64)
            std::fprintf(stderr, "[coop] planta: alvo jogador %d\n", t + 1);
        if (t >= 0)
            g_plantTarget = t;
        g_plantCycle = cycle;
    }
    const uint32_t p1 = rd32(rdram, kMonsters), t = !movie && g_plantTarget >= 0 && g_plantTarget < n ? g_plantP[g_plantTarget] : 0u;
    if (t)
        wr32(rdram, kMonsters, t);
    g_inPlant = true;
    g_plantUpd.orig(rdram, ctx, runtime);
    g_inPlant = false;
    wr32(rdram, kMonsters, p1);
    if (movie)
        for (int i = 1; i < n; ++i)
            if (g_plantP[i] && !g_out[i])
                call(rdram, ctx, runtime, fn::cleanUpForMovie__7Monster, {g_plantP[i]});
    if (g_chompView > 0 && !plantHolding(rdram))
    {
        const uint32_t cam = wotm_split4::camera(g_chompView);
        if (rd32(rdram, cam + wotm_split4::kCamPov) == 3u)
        {
            const uint32_t st = rd32(rdram, g_chompMon + kState) & 0x1FFFFFFFu;
            const uint32_t pov = st >= 0x1000u && (rd32(rdram, st + 4) & 0x10u) ? 5u : 0u;
            call(rdram, ctx, runtime, fn::SetCameraPOV__7CamerasiQ26Camera9CameraPOV, {uint32_t(g_chompView), pov});
            wr32(rdram, cam + 0xD9C, 1u);
        }
        g_chompView = -1;
    }
}

void plantCamHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t m = rd32(rdram, kMonsters) & 0x1FFFFFFFu;
    const uint32_t v = ctx->pc == g_plantCam.addr && g_inPlant && m ? rd32(rdram, m + kCameraView) : 0u;
    if (v == 0u || v > 3u || v >= rd32(rdram, wotm_split4::kNumCameras))
    {
        g_plantCam.orig(rdram, ctx, runtime);
        return;
    }
    swapCameras(rdram, v);
    g_plantCam.orig(rdram, ctx, runtime);
    swapCameras(rdram, v);
    g_chompView = int(v);
    g_chompMon = m;
    std::fprintf(stderr, "[coop] planta: mordida na camera %u\n", v);
}

void rootWakeHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t idx = GPR_U32(ctx, 5);
    const int o = ctx->pc == g_rootWake.addr && g_inPlant && idx < 10u ? plantPick(rdram) : -1;
    if (idx < 10u)
        g_rootOwner[idx] = o;
    const uint32_t cur = rd32(rdram, kMonsters);
    if (o >= 0)
        wr32(rdram, kMonsters, g_plantP[o]);
    g_rootWake.orig(rdram, ctx, runtime);
    if (o >= 0)
        wr32(rdram, kMonsters, cur);
}

void rootUpdHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t self = GPR_U32(ctx, 4) & 0x1FFFFFFFu;
    if (ctx->pc != g_rootUpd.addr || !g_inPlant || self < kPlantRoots || self >= kPlantRoots + 10u * kRootStride)
    {
        g_rootUpd.orig(rdram, ctx, runtime);
        return;
    }
    const int o = g_rootOwner[(self - kPlantRoots) / kRootStride];
    const uint32_t cur = rd32(rdram, kMonsters), owner = plantAlive(rdram, o) ? g_plantP[o] : (cur & 0x1FFFFFFFu);
    const R5900Context saved = *ctx;
    wr32(rdram, kMonsters, owner);
    g_rootUpd.orig(rdram, ctx, runtime);
    for (int j = 0; j < g_plantN; ++j)
    {
        const uint32_t p = g_plantP[j];
        if (p == owner || !plantAlive(rdram, j) || rd32(rdram, self + 0x14) != 3u)
            continue;
        const uint32_t cs = rd32(rdram, p + kCs) & 0x1FFFFFFFu;
        const float dx = rdF(rdram, self + 0x80) - rdF(rdram, cs + 0x10), dy = rdF(rdram, self + 0x84) - rdF(rdram, cs + 0x14),
                    dz = rdF(rdram, self + 0x88) - rdF(rdram, cs + 0x18);
        if (dx * dx + dy * dy + dz * dz >= 100.f * 100.f)   // a chamada confere os 75 do retail
            continue;
        wr32(rdram, kMonsters, p);
        R5900Context c = saved;
        g_rootUpd.orig(rdram, &c, runtime);
    }
    wr32(rdram, kMonsters, cur);
}

void baseHitHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const bool on = ctx->pc == g_baseHit.addr && (GPR_U32(ctx, 4) & 0x1FFFFFFFu) == kPlantBase;
    const uint32_t attacker = GPR_U32(ctx, 6), before = rd32(rdram, kPlantBase + 0x14);
    g_baseHit.orig(rdram, ctx, runtime);
    const uint32_t after = rd32(rdram, kPlantBase + 0x14);
    if (!on || after == before || (after != 8u && after != 9u))
        return;
    g_smackee = -1;
    for (int i = 0, n = players(rdram); i < n; ++i)
        if (const uint32_t p = plantPlayer(rdram, i); p && rd32(rdram, p + kId) == attacker)
            g_smackee = i;
}

void baseUpdHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t st = rd32(rdram, kPlantBase + 0x14);
    const bool on = ctx->pc == g_baseUpd.addr && g_inPlant && (st == 8u || st == 9u) && g_smackee >= 0 && g_smackee < g_plantN &&
                    g_plantP[g_smackee];
    const uint32_t cur = rd32(rdram, kMonsters);
    if (on)
        wr32(rdram, kMonsters, g_plantP[g_smackee]);
    g_baseUpd.orig(rdram, ctx, runtime);
    if (on)
        wr32(rdram, kMonsters, cur);
}

// LevelPickups::enableHighlights(v): o pick-up verde e o do m_monsters[v] (e apaga o do m_monsters[o], o outro jogador da tela
// dividida). Durante a chamada m_monsters[v] e o jogador da vista v; com 1 vista, m_numMonsters 1 (nada do outro).
Hook g_highlights{fn::enableHighlights__12LevelPickupsi};
void highlightsHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int n = ctx->pc == g_highlights.addr && g_views ? players(rdram) : 0;
    if (!n)
    {
        g_highlights.orig(rdram, ctx, runtime);
        return;
    }
    hudRestore(rdram);
    const uint32_t ra = GPR_U32(ctx, 31), num = rd32(rdram, kNumMonsters);
    uint32_t saved[4];
    for (uint32_t i = 0; i < 4; ++i)
        saved[i] = rd32(rdram, kMonsters + 4u * i);
    for (int v = 0; v < g_views; ++v)
        wr32(rdram, kMonsters + 4u * uint32_t(v), saved[g_map[v]]);
    if (g_views == 1)
        wr32(rdram, kNumMonsters, 1u);
    g_highlights.orig(rdram, ctx, runtime);
    if (ctx->pc != ra)
        std::fprintf(stderr, "[coop] enableHighlights pausou no meio\n");
    for (uint32_t i = 0; i < 4; ++i)
        wr32(rdram, kMonsters + 4u * i, saved[i]);
    wr32(rdram, kNumMonsters, num);
}

void okToUnifyHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    hudRestore(rdram);
    if (ctx->pc == g_okToUnify.addr && g_initAfter.on)
    {
        g_initAfter.on = false;
        initPlayersAfter(rdram, ctx, runtime, g_initAfter.was, g_initAfter.remapped);
    }
    if (ctx->pc == g_okToUnify.addr)
        dbgTick(rdram);
    // Vista 0 de volta a camera 0 (fora da vista unificada). Com todos mortos o jogo junta a tela (a vista 0 empresta o _cs da 2), e
    // um viewCreate da vista 0 nesse meio tempo (a carga do retry) grava ownCs = cs emprestado: ao sair da vista unificada, o
    // viewGrappleConfig devolvia a vista 0 a camera 2 (parada, POV 3) e o P1 sumia da metade dele
    if (ctx->pc == g_okToUnify.addr && players(rdram) > 1 && !rd32(rdram, wotm::sym::gUseUnifiedView) &&
        !rd32(rdram, wotm::sym::moviePlaying))
    {
        const uint32_t vi = wotm::sym::viewInfo, cs0 = rd32(rdram, wotm_split4::camera(0) + wotm_split4::kCamCs);
        if (cs0 && (rd32(rdram, vi + 0x10) != cs0 || rd32(rdram, vi) != cs0))
        {
            std::fprintf(stderr, "[coop] vista 0: camera %x (propria %x) -> %x, a da camera 0\n", rd32(rdram, vi), rd32(rdram, vi + 0x10), cs0);
            wr32(rdram, vi + 0x10, cs0);
            wr32(rdram, vi, cs0);
        }
    }
    const int n = ctx->pc == g_okToUnify.addr ? players(rdram) : 0;
    if (n != 2)
    {
        const uint32_t ra = GPR_U32(ctx, 31);
        g_okToUnify.orig(rdram, ctx, runtime);
        if (n && ctx->pc == ra)
            applyLayout(rdram, ctx, runtime);
        return;
    }
    const uint32_t slots = std::min<uint32_t>(rd32(rdram, kNumSlots), 16u);
    for (uint32_t i = 0; i < slots; ++i)
        wr32(rdram, kSlots + kSlotStride * i + kCamUnify, 0u);
    applyLayout(rdram, ctx, runtime);
    ctx->pc = GPR_U32(ctx, 31);
}

// Capitolio (fase 11, o Cerebulon): o FinalBoss::updateMecha grava m_monster7C = m_monsters[0] no estado 1 e mira so ele (torre,
// laser, passo em y); o updateJelly e o StateUltraTazer::update da jelly (m_monster74) perseguem e chocam o m_monster7C /
// m_monsters[0]. Alvo sorteado entre os jogadores vivos ao entrar no passo (Mecha: estado 2, vindo do 1 ou do 8; jelly: estado 1,
// vindo da poca) e quando o alvo morre ou sai (menos no disparo do Mecha e no choque da jelly, que o proprio jogo encerra com o alvo
// morto). Durante o update do chefe e o choque da jelly, m_monsters[0] e m_monster7C valem o alvo; nos cinemas (Mecha 0 e 9, jelly
// 5, fase 3) nao ha troca e o cleanUpForMovie do retail vale para todos. Linha [coop] Cerebulon a cada sorteio.
constexpr uint32_t kFinal = wotm::sym::finalBoss, kFinalActive = kFinal + 0x18, kFinalPhase = kFinal + 0x1C, kFinalState = kFinal + 0x20,
                   kFinalTimer = kFinal + 0x28, kFinalJelly = kFinal + 0x74, kFinalTarget = kFinal + 0x7C;
Hook g_finalUpd{fn::update__9FinalBoss}, g_tazerUpd{fn::update__15StateUltraTazer};
int g_finalTarget = -1;
uint32_t g_finalStep = ~0u;   // fase * 16 + estado na entrada do update anterior

bool finalCoop(uint8_t *rdram)
{
    return rd32(rdram, kGameMode) == 1u && rd32(rdram, kFinalActive) && players(rdram) > 1;
}
// Cinema do chefe: o retail chama cleanUpForMovie no P1 (no m_monster7C na fase 3)
bool finalMovie(uint8_t *rdram)
{
    const uint32_t ph = rd32(rdram, kFinalPhase), st = rd32(rdram, kFinalState);
    return ph >= 3u || (ph == 0u && (st == 9u || (st == 0u && static_cast<int32_t>(rd32(rdram, kFinalTimer)) > 0))) || (ph == 1u && st == 5u);
}
void finalReroll(uint8_t *rdram, const char *why)
{
    static uint32_t s = 0x6C8E9CF5u;
    int live[4], k = 0;
    for (int i = 0, n = std::min(players(rdram), 4); i < n; ++i)
        if (alive(rdram, i))
            live[k++] = i;
    if (!k)
        return;
    s ^= s << 13, s ^= s >> 17, s ^= s << 5;
    const int t = live[s % uint32_t(k)];
    static int s_logs = 0;
    if (s_logs++ < 64)
        std::fprintf(stderr, "[coop] Cerebulon: alvo jogador %d (%s, fase %u estado %u)\n", t + 1, why, rd32(rdram, kFinalPhase),
                     rd32(rdram, kFinalState));
    g_finalTarget = t;
}

void finalUpdHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t ra = GPR_U32(ctx, 31);
    if (ctx->pc != g_finalUpd.addr || !finalCoop(rdram))
    {
        g_finalUpd.orig(rdram, ctx, runtime);
        return;
    }
    const int n = players(rdram);
    const uint32_t ph = rd32(rdram, kFinalPhase), st = rd32(rdram, kFinalState), step = ph * 16u + st;
    const bool movie = finalMovie(rdram);
    if (!movie && ph <= 1u)
    {
        const bool busy = (ph == 0u && st == 4u) || (ph == 1u && st == 2u);   // disparo / choque: o jogo encerra com o alvo morto
        if (g_finalTarget < 0 || g_finalTarget >= n)
            finalReroll(rdram, "inicio");
        else if (step != g_finalStep && ((ph == 0u && st == 2u) || (ph == 1u && st == 1u)))
            finalReroll(rdram, "ciclo");
        else if (!busy && !alive(rdram, g_finalTarget))
            finalReroll(rdram, "alvo caiu");
    }
    const uint32_t p1 = rd32(rdram, kMonsters), t = !movie && ph <= 1u && g_finalTarget >= 0 ? monster(rdram, g_finalTarget) : 0u;
    if (t)
    {
        wr32(rdram, kMonsters, t);
        if (ph == 1u || st >= 2u)   // no estado 1 o proprio Mecha grava m_monsters[0]
            wr32(rdram, kFinalTarget, t);
    }
    g_finalUpd.orig(rdram, ctx, runtime);
    if (t)
        wotmFinishGuestCall(rdram, ctx, runtime, ra);   // a troca vale ate o fim, mesmo com o update pausado no meio
    wr32(rdram, kMonsters, p1);
    g_finalStep = step;
    if (movie)
    {
        const uint32_t done = rd32(rdram, kFinalPhase) >= 3u ? rd32(rdram, kFinalTarget) & 0x1FFFFFFFu : p1 & 0x1FFFFFFFu;
        for (int i = 0; i < n; ++i)
            if (const uint32_t m = monster(rdram, i); m && m != done && !g_out[i])
                call(rdram, ctx, runtime, fn::cleanUpForMovie__7Monster, {m});
    }
}

// O choque da jelly do chefe (StateUltraTazer, dono = finalBoss.m_monster74) mira m_monsters[0]: durante ele, o m_monster7C
void tazerUpdHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t ra = GPR_U32(ctx, 31), owner = rd32(rdram, (GPR_U32(ctx, 4) & 0x1FFFFFFFu) + kStateOwner) & 0x1FFFFFFFu;
    const uint32_t jelly = rd32(rdram, kFinalJelly) & 0x1FFFFFFFu, target = rd32(rdram, kFinalTarget) & 0x1FFFFFFFu;
    const bool on = ctx->pc == g_tazerUpd.addr && jelly && owner == jelly && target && finalCoop(rdram);
    const uint32_t p1 = rd32(rdram, kMonsters);
    if (on)
        wr32(rdram, kMonsters, target);
    g_tazerUpd.orig(rdram, ctx, runtime);
    if (on)
    {
        wotmFinishGuestCall(rdram, ctx, runtime, ra);
        wr32(rdram, kMonsters, p1);
    }
}

// Toquio (fase 9): o tokyoUpdate so encerra a fase com as duas IAs fora e o m_monsters[0] fora da morte (+0xE8), e o cinema do fim
// (roteiro 0x2266) abduz so ele (quadros 260 a 630 da animacao do disco: sobe, gira, some no 600). Durante a chamada o
// m_monsters[0] vale o lider: o primeiro jogador vivo, fixo depois que o fim comeca. No cinema, os outros que continuam fazem a
// mesma subida que o lider fez no quadro (mesmo z, giro, matriz) e somem com ele (o cleanUpForMovie do lider vale para todos:
// cleanUpHook). Linha [coop] Toquio.
constexpr uint32_t kTokyoEnd = 0x006F8CF4u;   // static do tokyoUpdate (sem nome no ELF): o fim da fase comecou
Hook g_tokyoUpd{fn::tokyoUpdate__Fv};
int g_tokyoLead = -1;

void tokyoUpdHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t ra = GPR_U32(ctx, 31);
    const int n = ctx->pc == g_tokyoUpd.addr && rd32(rdram, kGameMode) == 1u ? players(rdram) : 0;
    if (!n)
    {
        g_tokyoUpd.orig(rdram, ctx, runtime);
        return;
    }
    const bool ending = rd32(rdram, kTokyoEnd) != 0u;
    if (!ending || g_tokyoLead < 0 || g_tokyoLead >= n)
    {
        int lead = -1;
        for (int i = 0; i < n && lead < 0; ++i)
            if (alive(rdram, i))
                lead = i;
        if (lead != g_tokyoLead && lead >= 0)
            std::fprintf(stderr, "[coop] Toquio: lider jogador %d\n", lead + 1);
        g_tokyoLead = lead;
    }
    const uint32_t p1 = rd32(rdram, kMonsters), lead = g_tokyoLead >= 0 ? monster(rdram, g_tokyoLead) : 0u;
    const uint32_t leadCs = lead ? rd32(rdram, lead + kCs) & 0x1FFFFFFFu : 0u;
    const float z0 = leadCs ? rdF(rdram, leadCs + 0x18) : 0.f, roll0 = leadCs ? rdF(rdram, leadCs + 0x68) : 0.f;
    const uint8_t draw0 = leadCs ? rd8(rdram, leadCs + 0xC) : 0u;
    if (lead)
        wr32(rdram, kMonsters, lead);
    g_tokyoUpd.orig(rdram, ctx, runtime);
    if (lead)
        wotmFinishGuestCall(rdram, ctx, runtime, ra);
    wr32(rdram, kMonsters, p1);
    if (!lead || !rd32(rdram, kTokyoEnd))
        return;
    if (!ending)
        std::fprintf(stderr, "[coop] Toquio: fim da fase com o jogador %d\n", g_tokyoLead + 1);
    const bool rise = rdF(rdram, leadCs + 0x68) != roll0, hide = draw0 && !rd8(rdram, leadCs + 0xC);
    const float dz = rdF(rdram, leadCs + 0x18) - z0;
    for (int i = 0; i < n; ++i)
    {
        const uint32_t m = monster(rdram, i);
        if (!m || i == g_tokyoLead || g_out[i])
            continue;
        const uint32_t cs = rd32(rdram, m + kCs) & 0x1FFFFFFFu;
        if (cs >= 0x1000u && rise)   // o passo da abducao do retail (tokyoUpdate, quadros 260 a 630)
        {
            wrF(rdram, cs + 0x18, rdF(rdram, cs + 0x18) + dz);
            const uint32_t vt = rd32(rdram, m + 0x10) & 0x1FFFFFFFu;
            const int16_t adj = static_cast<int16_t>(rd32(rdram, vt + 0x20) & 0xFFFFu);
            if (const uint32_t r = call(rdram, ctx, runtime, rd32(rdram, vt + 0x24), {m + static_cast<uint32_t>(int32_t(adj))}) & 0x1FFFFFFFu)
                wr32(rdram, r + 8, 0u);
            wrF(rdram, cs + 0x68, rdF(rdram, cs + 0x68) + 0.01f);
            call(rdram, ctx, runtime, fn::mathfRotMatrixRPH__FPA3_fP8_fvector, {cs + 0x20, cs + 0x60});
            wr32(rdram, m + 0x18, 0u);
        }
        if (cs >= 0x1000u && hide)
            wotm_modes4::wr8(rdram, cs + 0xC, 0u);
    }
}

// Vibracao do laser do Mecha: depois do takeDamage no alvo, o updateMecha chama inputSetActuator(0, ...), sempre o controle do P1;
// nessa chamada (pelo retorno) vai ao controle do alvo sorteado (inputGetPlayerPad). Linha [coop] Cerebulon: vibracao.
constexpr uint32_t kLaserRumbleRa = fn::updateMecha__9FinalBoss + 0xD3C;
Hook g_actuator{fn::inputSetActuator__FiP12ActuatorData};
void actuatorHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == g_actuator.addr && GPR_U32(ctx, 31) == kLaserRumbleRa && GPR_U32(ctx, 4) == 0u && g_finalTarget > 0 && finalCoop(rdram))
    {
        const uint32_t pad = call(rdram, ctx, runtime, fn::inputGetPlayerPad__Fi, {uint32_t(g_finalTarget)});
        ctx->r[4] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(pad)));
        static int s_logs = 0;
        if (s_logs++ < 8)
            std::fprintf(stderr, "[coop] Cerebulon: vibracao do laser no controle %u (jogador %d)\n", pad, g_finalTarget + 1);
    }
    g_actuator.orig(rdram, ctx, runtime);
}

// ------------------------------------------------------------------ largada

constexpr uint32_t kVec = PS2_RAM_SIZE - 0x3C0;   // _fvector para o setTrans e a normal do hdHatTest (os do split4 e do modes4 ficam em -0x400 e -0x3E0)

// hdHatTest(pos, normal, 5, false, 4096): distancia de pos ate o chao embaixo (o gameGetStartPoint o usa para assentar um ponto)
float hatTest(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t pos, uint32_t normal)
{
    const PS2Runtime::RecompiledFunction f = runtime->lookupFunction(fn::hdHatTest__FP8_fvectorT0fbf);
    if (!f)
        return -1.f;
    const R5900Context saved = *ctx;
    ctx->r[4] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(pos)));
    ctx->r[5] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(normal)));
    ctx->r[6] = _mm_set_epi64x(0, 0);
    ctx->r[31] = _mm_set_epi64x(0, 0);
    ctx->f[12] = 5.f;
    ctx->f[13] = 4096.f;
    ctx->pc = fn::hdHatTest__FP8_fvectorT0fbf;
    f(rdram, ctx, runtime);
    wotmFinishGuestCall(rdram, ctx, runtime);
    const float d = ctx->f[0];
    *ctx = saved;
    return d;
}

// Chamada com floats (f12..f14) alem dos inteiros; devolve o f0
float callF(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t addr, std::initializer_list<uint32_t> ints,
            std::initializer_list<float> floats)
{
    const PS2Runtime::RecompiledFunction f = runtime->lookupFunction(addr);
    if (!f)
        return 0.f;
    const R5900Context saved = *ctx;
    int r = 4, k = 12;
    for (uint32_t a : ints)
        ctx->r[r++] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(a)));
    for (float v : floats)
        ctx->f[k++] = v;
    ctx->r[31] = _mm_set_epi64x(0, 0);
    ctx->pc = addr;
    f(rdram, ctx, runtime);
    wotmFinishGuestCall(rdram, ctx, runtime);
    const float f0 = ctx->f[0];
    *ctx = saved;
    return f0;
}

// Poe o monstro m (jogador who) ao lado de (ax, ay, az), na linha (rx, ry) a kSide por casa: o P2 a direita, o P3 a esquerda, o P4 duas
// casas a direita; casa sem chao na altura (az +- 60, medido de 1000 acima) ou ocupada passa para a seguinte. mat: matriz do giro
// (setMat) ou 0. Devolve se achou casa livre com chao.
constexpr float kSide = 150.f;
bool placeBeside(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t m, int who, float ax, float ay, float az, float rx,
                 float ry, uint32_t mat)
{
    static const int kOrder[4][6] = {{1, -1, 2, -2, 3, -3}, {1, -1, 2, -2, 3, -3}, {-1, 1, -2, 2, -3, 3}, {2, -2, 1, -1, 3, -3}};
    const uint32_t cs = rd32(rdram, m + kCs) & 0x1FFFFFFFu;
    if (cs < 0x1000u)
        return false;
    for (int s = 0; s < 6; ++s)
    {
        const float off = kSide * float(kOrder[who & 3][s]), px = ax + rx * off, py = ay + ry * off;
        wrF(rdram, kVec, px);
        wrF(rdram, kVec + 4, py);
        wrF(rdram, kVec + 8, az + 1000.f);
        wrF(rdram, kVec + 12, 0.f);
        const float d = hatTest(rdram, ctx, runtime, kVec, kVec + 0x10);
        const float ground = az + 1000.f - d;
        if (!(d > 0.f && d < 2000.f) || std::fabs(ground - az) > 60.f)
            continue;
        wrF(rdram, kVec, px);
        wrF(rdram, kVec + 4, py);
        wrF(rdram, kVec + 8, ground + 20.f);
        wrF(rdram, kVec + 12, 0.f);
        call(rdram, ctx, runtime, fn::setTrans__7MonsterR8_fvector, {m, kVec});
        if (mat)
            call(rdram, ctx, runtime, fn::setMat__7MonsterRA3_A3_f, {m, mat});
        call(rdram, ctx, runtime, fn::hdReparentCsGrid__FP3_cs, {cs});
        if (!wotm_split4::nearOther(rdram, who))
            return true;
    }
    return false;
}

// Lado (horizontal, unitario) da matriz de giro em mat (linha 0; a linha 1, cs+0x30, e a frente)
void sideOf(uint8_t *rdram, uint32_t mat, float &rx, float &ry)
{
    rx = rdF(rdram, mat);
    ry = rdF(rdram, mat + 4);
    const float l = std::sqrt(rx * rx + ry * ry);
    if (l < 1e-3f)
        rx = 1.f, ry = 0.f;
    else
        rx /= l, ry /= l;
}

// Aliado que serve de ancora para o jogador who: o primeiro outro na fase, com _cs
int anchorFor(uint8_t *rdram, int who, int n)
{
    for (int j = 0; j < n; ++j)
        if (j != who && !g_out[j])
            if (const uint32_t o = monster(rdram, j); o && (rd32(rdram, o + kCs) & 0x1FFFFFFFu) >= 0x1000u)
                return j;
    return -1;
}

// Cratera do 3-Mile (fase 6): quando a contagem acaba, o threeMileUpdateCountDown liga o craterSwitched, baixa a agua e leva so o
// m_monsters[0] ao ponto fixo da cratera (setTrans, giro em cs+0x68 e o +0x1E8 em 500); os outros jogadores ficavam onde estavam e
// podiam cair no buraco. Durante a chamada o m_monsters[0] vale o primeiro que continua (cinemaLead); na troca, os outros vao para o
// lado dele, com o mesmo giro e o mesmo +0x1E8 (sem chao conferido, o normal na troca: a 150 por casa na altura dele, e caem ate o
// fundo como ele). Linha [coop] cratera.
Hook g_craterCount{fn::threeMileUpdateCountDown__Fv};
int cinemaLead(uint8_t *rdram, int n);

void craterCountHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t ra = GPR_U32(ctx, 31);
    const int n = ctx->pc == g_craterCount.addr && rd32(rdram, kGameMode) == 1u ? players(rdram) : 0;
    if (!n)
    {
        g_craterCount.orig(rdram, ctx, runtime);
        return;
    }
    const bool before = rd32(rdram, wotm::sym::craterSwitched) != 0u;
    const int lead = cinemaLead(rdram, n);
    const uint32_t p1 = rd32(rdram, kMonsters), lm = monster(rdram, lead);
    if (lead > 0)
        wr32(rdram, kMonsters, lm);
    g_craterCount.orig(rdram, ctx, runtime);
    if (lead > 0)
    {
        wotmFinishGuestCall(rdram, ctx, runtime, ra);
        wr32(rdram, kMonsters, p1);
    }
    if (before || !rd32(rdram, wotm::sym::craterSwitched))
        return;
    const uint32_t lcs = rd32(rdram, lm + kCs) & 0x1FFFFFFFu;
    if (lcs < 0x1000u)
        return;
    const float ax = rdF(rdram, lcs + 0x10), ay = rdF(rdram, lcs + 0x14), az = rdF(rdram, lcs + 0x18), yaw = rdF(rdram, lcs + 0x68);
    const uint32_t fall = rd32(rdram, lm + 0x1E8);
    // lado: a linha 0 da matriz do lider refeita com o giro novo (como o tokyoUpdate faz depois de mexer no cs+0x68)
    call(rdram, ctx, runtime, fn::mathfRotMatrixRPH__FPA3_fP8_fvector, {lcs + 0x20, lcs + 0x60});
    float rx, ry;
    sideOf(rdram, lcs + 0x20, rx, ry);
    for (int k = 0; k < n; ++k)
    {
        const uint32_t m = monster(rdram, k);
        if (!m || k == lead || g_out[k])
            continue;
        const uint32_t cs = rd32(rdram, m + kCs) & 0x1FFFFFFFu;
        if (cs < 0x1000u)
            continue;
        const bool ok = placeBeside(rdram, ctx, runtime, m, k, ax, ay, az, rx, ry, 0u);
        if (!ok)   // o chao da cratera ainda nao responde ao hdHatTest na troca: na altura do lider, que cai ate o fundo como ele
        {
            const float off = kSide * float(k == 2 ? -1 : k == 3 ? 2 : 1);
            wrF(rdram, kVec, ax + rx * off);
            wrF(rdram, kVec + 4, ay + ry * off);
            wrF(rdram, kVec + 8, az);
            wrF(rdram, kVec + 12, 0.f);
            call(rdram, ctx, runtime, fn::setTrans__7MonsterR8_fvector, {m, kVec});
            call(rdram, ctx, runtime, fn::hdReparentCsGrid__FP3_cs, {cs});
        }
        wrF(rdram, cs + 0x68, yaw);
        call(rdram, ctx, runtime, fn::mathfRotMatrixRPH__FPA3_fP8_fvector, {cs + 0x20, cs + 0x60});
        wr32(rdram, m + 0x1E8, fall);
        std::fprintf(stderr, "[coop] cratera: jogador %d ao lado do %d%s\n", k + 1, lead + 1, ok ? "" : " (sem chao conferido)");
    }
}



// Largada: os jogadores 2 a 4 ao lado do aliado-ancora (o P1, ou o primeiro que continua; quem renasce: revive), virados
// para o mesmo lado. Sem casa com chao ali, ou o P1 na largada: o ponto da fase e, se cair em cima de outro (lista 0 de um ponto, ou
// a cratera), um ponto em volta a 150 ou 260 com chao na altura de onde caiu (telhado de predio ou buraco recusam o ponto)
void startPointHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t m = GPR_U32(ctx, 5) & 0x1FFFFFFFu, ra = ctx->pc == g_startPoint.addr ? GPR_U32(ctx, 31) : 0u;
    const int n = ra ? players(rdram) : 0;
    const int who = n ? wotm_modes4::playerOf(rdram, m, n) : -1;
    g_startPoint.orig(rdram, ctx, runtime);
    if (who < 0 || ctx->pc != ra)
        return;
    const uint32_t cs = rd32(rdram, m + kCs) & 0x1FFFFFFFu;
    if (cs < 0x1000u)
        return;
    const float ox = rdF(rdram, cs + 0x10), oy = rdF(rdram, cs + 0x14), oz = rdF(rdram, cs + 0x18);
    if (const int a = who > 0 ? anchorFor(rdram, who, n) : -1; a >= 0)
    {
        const uint32_t acs = rd32(rdram, monster(rdram, a) + kCs) & 0x1FFFFFFFu;
        float rx, ry;
        sideOf(rdram, acs + 0x20, rx, ry);
        if (placeBeside(rdram, ctx, runtime, m, who, rdF(rdram, acs + 0x10), rdF(rdram, acs + 0x14), rdF(rdram, acs + 0x18), rx, ry, acs + 0x20))
        {
            std::fprintf(stderr, "[coop] largada: jogador %d ao lado do %d (%.0f,%.0f,%.0f)\n", who + 1, a + 1, rdF(rdram, cs + 0x10),
                         rdF(rdram, cs + 0x14), rdF(rdram, cs + 0x18));
            return;
        }
        {
            const float ax = rdF(rdram, acs + 0x10), ay = rdF(rdram, acs + 0x14), az = rdF(rdram, acs + 0x18);
            wrF(rdram, kVec, ax);
            wrF(rdram, kVec + 4, ay);
            wrF(rdram, kVec + 8, az + 1000.f);
            wrF(rdram, kVec + 12, 0.f);
            const float d = hatTest(rdram, ctx, runtime, kVec, kVec + 0x10);
            std::fprintf(stderr, "[coop] largada: sem casa ao lado do %d em (%.0f,%.0f,%.0f), chao dele a %.0f; ponto da fase do %d (%.0f,%.0f,%.0f)\n", a + 1,
                         ax, ay, az, double(az + 1000.f - d), who + 1, ox, oy, oz);
        }
        wrF(rdram, kVec, ox);   // sem casa: de volta ao ponto da fase
        wrF(rdram, kVec + 4, oy);
        wrF(rdram, kVec + 8, oz);
        wrF(rdram, kVec + 12, 0.f);
        call(rdram, ctx, runtime, fn::setTrans__7MonsterR8_fvector, {m, kVec});
        call(rdram, ctx, runtime, fn::hdReparentCsGrid__FP3_cs, {cs});
    }
    if (!wotm_split4::nearOther(rdram, who))
        return;
    const float x = rdF(rdram, cs + 0x10), y = rdF(rdram, cs + 0x14), z = rdF(rdram, cs + 0x18);
    auto place = [&](float px, float py, float pz) {
        wrF(rdram, kVec, px);
        wrF(rdram, kVec + 4, py);
        wrF(rdram, kVec + 8, pz);
        wrF(rdram, kVec + 12, 0.f);
        call(rdram, ctx, runtime, fn::setTrans__7MonsterR8_fvector, {m, kVec});
        call(rdram, ctx, runtime, fn::hdReparentCsGrid__FP3_cs, {cs});
    };
    bool ok = false;
    float fx = x, fy = y;
    bool haveFallback = false;
    for (int r = 0; r < 2 && !ok; ++r)
        for (int k = 0; k < 8 && !ok; ++k)
        {
            const float a = 0.785398f * float((k + 2 * who) % 8), rad = r ? 260.f : 150.f;   // cada jogador comeca por uma direcao
            const float px = x + rad * std::cos(a), py = y + rad * std::sin(a);
            wrF(rdram, kVec, px);
            wrF(rdram, kVec + 4, py);
            wrF(rdram, kVec + 8, z + 1000.f);
            wrF(rdram, kVec + 12, 0.f);
            const float d = hatTest(rdram, ctx, runtime, kVec, kVec + 0x10);
            const float ground = z + 1000.f - d;
            if (!(d > 0.f && d < 2000.f) || std::fabs(ground - z) > 40.f)
                continue;
            if (!haveFallback)
                fx = px, fy = py, haveFallback = true;
            place(px, py, ground + 20.f);
            ok = !wotm_split4::nearOther(rdram, who);
        }
    if (!ok)
        place(haveFallback ? fx : x + 150.f, haveFallback ? fy : y, z + 50.f);
    std::fprintf(stderr, "[coop] largada: jogador %d ao lado (%.0f,%.0f,%.0f)%s\n", who + 1, rdF(rdram, cs + 0x10), rdF(rdram, cs + 0x14),
                 rdF(rdram, cs + 0x18), ok ? "" : " sem chao conferido");
}

// ------------------------------------------------------------------ cinemas

// doSetPlayerPos(pos, mat, act): o roteiro poe o m_monsters[idx] (ou uma IA) em pos com o giro mat. Com o P1 (idx 0), os outros
// jogadores vao ao lado dele, virados para o mesmo lado, e aparecem na cena.
Hook g_setPlayerPos{fn::doSetPlayerPos__FP8_fvectorRA3_A3_fP12ActPlayerPos}, g_triggerAnim{fn::triggerAnimation__FP11DelayActionP7ActAnim};
// Quem faz o papel do P1 no roteiro: ele, ou, com ele fora da fase, o primeiro que continua (m_monsters[0] trocado durante a chamada)
int cinemaLead(uint8_t *rdram, int n)
{
    for (int k = 0; k < n; ++k)
        if (!g_out[k] && monster(rdram, k))
            return k;
    return 0;
}
void setPlayerPosHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int n = ctx->pc == g_setPlayerPos.addr ? players(rdram) : 0;
    const uint32_t pos = GPR_U32(ctx, 4) & 0x1FFFFFFFu, mat = GPR_U32(ctx, 5) & 0x1FFFFFFFu, act = GPR_U32(ctx, 6) & 0x1FFFFFFFu,
                   ra = GPR_U32(ctx, 31);
    const bool p1 = n && (rd32(rdram, act + 8) & 0x1FFFFu) == 0u;   // idx 0 e o bit da IA em 0
    const int lead = p1 ? cinemaLead(rdram, n) : 0;
    const uint32_t saved = rd32(rdram, kMonsters);
    if (lead > 0)
        wr32(rdram, kMonsters, monster(rdram, lead));
    g_setPlayerPos.orig(rdram, ctx, runtime);
    if (lead > 0)
    {
        wotmFinishGuestCall(rdram, ctx, runtime, ra);
        wr32(rdram, kMonsters, saved);
    }
    if (!p1 || ctx->pc != ra)
        return;
    const float ax = rdF(rdram, pos), ay = rdF(rdram, pos + 4), az = rdF(rdram, pos + 8);
    float rx, ry;
    sideOf(rdram, mat, rx, ry);
    for (int k = 1; k < n; ++k)
        if (const uint32_t m = monster(rdram, k); m && !g_out[k] && k != lead)
        {
            const bool ok = placeBeside(rdram, ctx, runtime, m, k, ax, ay, az, rx, ry, mat);
            std::fprintf(stderr, "[coop] cinema: jogador %d ao lado do %d%s\n", k + 1, std::max(lead, 0) + 1, ok ? "" : " (sem casa com chao)");
        }
}

// triggerAnimation(da, act): animacao do roteiro. A do P1 (ACTANIM_PLAYER, obj 0) toca tambem nos outros jogadores, no mesmo slot do
// m_anims deles (animationSetSpeed 1 e animationTransitionInto 200, como o retail faz no P1; o fim da acao segue o do P1)
constexpr uint32_t kAnims = 0x1CF0, kHandle = kVec + 0x20;   // Monster::m_anims (_animHandle, 0x10 cada); copia do handle passado por valor
void triggerAnimHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int n = ctx->pc == g_triggerAnim.addr ? players(rdram) : 0;
    const uint32_t act = GPR_U32(ctx, 5) & 0x1FFFFFFFu, ra = GPR_U32(ctx, 31);
    const uint32_t flags = n ? rd32(rdram, act + 0xC) : 0u;
    const bool p1 = n && (flags & 0x1800u) == 0x800u && rd32(rdram, act + 8) == 0u;
    const int lead = p1 ? cinemaLead(rdram, n) : 0;
    const uint32_t saved = rd32(rdram, kMonsters);
    if (lead > 0)
        wr32(rdram, kMonsters, monster(rdram, lead));
    g_triggerAnim.orig(rdram, ctx, runtime);
    if (lead > 0)
    {
        wotmFinishGuestCall(rdram, ctx, runtime, ra);
        wr32(rdram, kMonsters, saved);
    }
    if (!p1 || ctx->pc != ra)
        return;
    const uint32_t slot = flags >> 16;
    for (int k = 1; k < n; ++k)
        if (const uint32_t m = monster(rdram, k); m && !g_out[k] && k != lead)
        {
            const uint32_t h = m + kAnims + 0x10u * slot;
            if (!rd32(rdram, h))   // sem _animCharInstance
                continue;
            std::memcpy(rdram + kHandle, rdram + h, 0x10);
            callF(rdram, ctx, runtime, fn::animationSetSpeed__FG11_animHandlef, {kHandle}, {1.f});
            callF(rdram, ctx, runtime, fn::animationTransitionInto__FR11_animHandlefii, {h, 1u, 1u}, {200.f});
            std::fprintf(stderr, "[coop] cinema: animacao %u do jogador %d tambem no %d\n", slot, lead + 1, k + 1);
        }
}

// Cinemas com 3 e 4 jogadores (quadrantes do split4, na campanha e no versus): o Monster::updateCinema pula o filme com o START dos
// controles 0 e 1 (inputGetInput(8, 0) e (8, 1)); os jogadores 3 e 4 (controles 2 e 3) pulam pelo mesmo caminho (movieAborted,
// fadeOutAndIn 4 e o stopAllActiveActions pelo TaskManager em 30 fields). O endCinema poe o corte (+0xD90 e +0xD98) so nas cameras
// 0 e 1; as 2 e 3 recebem o mesmo, senao voltam do cinema deslizando de onde a cena as deixou. Linha [coop] cinema pulado.
constexpr uint32_t kCamReset = 0xD90, kCamCut = 0xD98;
Hook g_updCinema{fn::updateCinema__7Monster}, g_endCinema{fn::endCinema__FP11DelayActionP9ActCinema};

void updCinemaHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int n = ctx->pc == g_updCinema.addr ? wotm_split4::splitPlayers(rdram) : 0;
    g_updCinema.orig(rdram, ctx, runtime);
    if (n < 3 || !rd32(rdram, wotm::sym::moviePlaying) || rd32(rdram, wotm::sym::movieAborted) || rd32(rdram, kGame + 0x120454u) ||
        rd32(rdram, kGame + 0x120458u))
        return;
    for (int p = 2; p < n; ++p)
        if (call(rdram, ctx, runtime, fn::inputGetInput__Fii, {8u, uint32_t(p)}))
        {
            wr32(rdram, wotm::sym::movieAborted, 1u);
            call(rdram, ctx, runtime, fn::fadeOutAndIn__7TheGamei, {kGame, 4u});
            call(rdram, ctx, runtime, fn::add__11TaskManagerPFv_Uii,
                 {wotm::sym::x_11TaskManager_global, fn::stopAllActiveActions__14ActionDispatchv, 30u});
            std::fprintf(stderr, "[coop] cinema pulado pelo jogador %d\n", p + 1);
            return;
        }
}

// Monster::cleanUpForMovie (cloak desligado, fogo apagado, efeitos presos ao monstro escondidos): as fases (vegas, ufo, tokyo, canyon2,
// 3-Mile, planta, final) o chamam so no m_monsters[0] antes de um cinema. Chamado com ele (o P1, ou quem os ganchos poem no lugar
// dele), vale para todos os jogadores que continuam.
Hook g_cleanUp{fn::cleanUpForMovie__7Monster};
void cleanUpHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t m = GPR_U32(ctx, 4) & 0x1FFFFFFFu, ra = GPR_U32(ctx, 31);
    const int n = ctx->pc == g_cleanUp.addr ? players(rdram) : 0;
    g_cleanUp.orig(rdram, ctx, runtime);
    if (!n || m != (rd32(rdram, kMonsters) & 0x1FFFFFFFu))
        return;
    wotmFinishGuestCall(rdram, ctx, runtime, ra);
    for (int i = 0; i < n; ++i)
        if (const uint32_t o = monster(rdram, i); o && o != m && !g_out[i])
            call(rdram, ctx, runtime, fn::cleanUpForMovie__7Monster, {o});
}

void endCinemaHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int n = ctx->pc == g_endCinema.addr ? wotm_split4::splitPlayers(rdram) : 0;
    const bool ending = rd32(rdram, wotm::sym::s_endingActions) != 0u;   // so marca o fim; as cameras ficam para o endCinema seguinte
    const uint32_t ra = GPR_U32(ctx, 31);
    g_endCinema.orig(rdram, ctx, runtime);
    if (n < 3 || ending)
        return;
    wotmFinishGuestCall(rdram, ctx, runtime, ra);
    for (int c = 2; c < n; ++c)
    {
        wr32(rdram, kCameras + kCamStride * uint32_t(c) + kCamReset, 1u);
        wr32(rdram, kCameras + kCamStride * uint32_t(c) + kCamCut, 1u);
    }
}

// ------------------------------------------------------------------ vidas e vitoria

void resolveHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    hudRestore(rdram);
    const int n = ctx->pc == g_resolve.addr ? players(rdram) : 0;
    if (!n || rd32(rdram, kGameMode) != 1u)
    {
        g_resolve.orig(rdram, ctx, runtime);
        return;
    }
    const uint32_t game = GPR_U32(ctx, 4), ra = GPR_U32(ctx, 31);
    ctx->pc = ra;
    const int ais = static_cast<int32_t>(rd32(rdram, kNumAIs));
    bool aiLeft = ais == 0;
    for (int i = 0; i < ais; ++i)
        if (const uint32_t m = monster(rdram, 4 + i); m && !rd8(rdram, m + kOut))
            aiLeft = true;
    for (int i = 0; i < n; ++i)
    {
        const uint32_t m = monster(rdram, i);
        if (!m || g_out[i] || !rd8(rdram, m + kOut))
            continue;
        const int32_t lives = static_cast<int32_t>(rd32(rdram, kLives + 4u * uint32_t(i)));
        if (lives > 1)
            revive(rdram, ctx, runtime, i, lives - 1);   // sem voltar ao Shell: a tela nao esmaece para os outros
        else if (inFight(n, i) >= 1)
            eliminate(rdram, ctx, runtime, i, n);
        else
        {   // o ultimo: game over pelo Shell (EvaluateOnePlayerStoryStatus, dialogo 1)
            call(rdram, ctx, runtime, fn::rtReturnToShell__Fii, {1u, 0x1Eu});
            call(rdram, ctx, runtime, fn::fadeOut__7TheGamei, {game, 4u});
        }
    }
    const uint32_t level = rd32(rdram, kLevelId);
    if (aiLeft || level == 3u || level == 6u || level == 9u || level == 10u || level == 11u)
        return;
    // as IAs sairam: a pose de vitoria e do primeiro jogador vivo na fase
    for (int i = 0; i < n; ++i)
    {
        const uint32_t p = monster(rdram, i);
        if (!p || g_out[i] || rd8(rdram, p + kDead) || !(rdF(rdram, p + kHealth) > 0.f))
            continue;
        if (rd8(rdram, p + kVictorious))
        {
            call(rdram, ctx, runtime, fn::rtReturnToShell__Fii, {0u, 0x1Eu});
            call(rdram, ctx, runtime, fn::fadeOut__7TheGamei, {game, 4u});
        }
        else
        {
            const uint32_t st = rd32(rdram, p + kState) & 0x1FFFFFFFu;
            if (st >= 0x1000u && rd32(rdram, st) != 0x40u && call(rdram, ctx, runtime, fn::transitionOK__12StateVictory, {p + kVictoryState}))
                call(rdram, ctx, runtime, fn::enterNewState__7MonsterP12MonsterState, {p, p + kVictoryState});
        }
        return;
    }
}

void evalStoryHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    hudRestore(rdram);
    const int n = ctx->pc == g_evalStory.addr ? players(rdram) : 0;
    const int32_t r = static_cast<int32_t>(GPR_U32(ctx, 5));
    if (!n || r != 1)
    {
        g_evalStory.orig(rdram, ctx, runtime);
        return;
    }
    // sem IA viva a morte vira vitoria (como o retail; no 3-Mile depois da cratera, so com mais de 2 cabecas mortas): o original decide
    if (call(rdram, ctx, runtime, fn::GetNumAIsAlive__7TheGame, {kGame}) == 0u &&
        !(rd32(rdram, kLevelId) == 6u && rd32(rdram, wotm::sym::craterSwitched) && static_cast<int32_t>(rd32(rdram, wotm::sym::numDeadHeads)) <= 2))
    {
        g_evalStory.orig(rdram, ctx, runtime);
        return;
    }
    const uint32_t ra = GPR_U32(ctx, 31);
    ctx->pc = ra;
    for (int i = 0; i < n; ++i)
    {
        const uint32_t m = monster(rdram, i);
        if (!m || g_out[i] || rdF(rdram, m + kHealth) > 0.f)
            continue;
        const int32_t lives = static_cast<int32_t>(rd32(rdram, kLives + 4u * uint32_t(i))) - 1;
        if (lives <= 0)
        {
            eliminate(rdram, ctx, runtime, i, n);
            continue;
        }
        revive(rdram, ctx, runtime, i, lives);
    }
    if (inFight(n, -1) == 0)
    {
        std::fprintf(stderr, "[coop] todos sem vidas: fim de jogo\n");
        wotm_modes4::tailDialog(rdram, ctx, runtime, 1, ra);
        return;
    }
    if (rd32(rdram, wotm::sym::gUseUnifiedView))
        call(rdram, ctx, runtime, fn::LeaveUnifiedView__7CamerasUi, {0xCu});
    wr32(rdram, kInSession, 1u);
}

// Tokens do jogador 1 vezes 1 + 0,1 por jogador (so dentro do EvaluateOnePlayerStoryStatus)
void grandTotalHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t ra = GPR_U32(ctx, 31);
    const int n = ctx->pc == g_grandTotal.addr && ra > g_evalStory.addr && ra < g_evalStory.addr + 0x3CCu ? players(rdram) : 0;
    g_grandTotal.orig(rdram, ctx, runtime);
    if (!n || ctx->pc != ra)
        return;
    const int32_t t = static_cast<int32_t>(GPR_U32(ctx, 2)), out = static_cast<int32_t>(std::lround(double(t) * (1.0 + 0.1 * n)));
    SET_GPR_S32(ctx, 2, out);
    std::fprintf(stderr, "[coop] tokens do jogador 1: %d x %.1f = %d\n", t, 1.0 + 0.1 * n, out);
}

// Depois do InitPlayers (quem saiu volta a aparecer; no retry, cameras e tela da largada). Com o InitPlayers pausado no meio pelo
// escalonador (o retry no Capitolio), a retomada nao passa pelo gancho: fica pendente para o primeiro SetOkToUnify (okToUnifyHook)
void initPlayersAfter(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, const bool was[4], bool remapped)
{
    const int n = players(rdram);
    if (!n)
        return;
    for (int i = 0; i < n; ++i)
        if (const uint32_t m = monster(rdram, i); m && (was[i] || remapped))
        {
            if (was[i])
            {
                call(rdram, ctx, runtime, fn::hierSetCsDrawMe__FP3_csUc, {rd32(rdram, m + kCs), 1u});
                call(rdram, ctx, runtime, fn::setShadowOnOff__7Monsterb, {m, 1u});
            }
            wr32(rdram, wotm_split4::camera(i) + wotm_split4::kCamPov, 0u);
            call(rdram, ctx, runtime, fn::SetCameraToFollowMonster__7CamerasiP7Monster, {uint32_t(i), m});
        }
    if (remapped)
    {   // retry na mesma sessao: a tela da largada de volta (numa carga nova o gameReestablishViews ja a refez). O ra nao pode ser 0:
        // o gancho do split4 so monta os quadrantes quando a chamada volta ao ra; este e o de uma funcao vazia do retail
        const R5900Context saved = *ctx;
        ctx->r[4] = _mm_set_epi64x(0, static_cast<int64_t>(kGame));
        ctx->r[31] = _mm_set_epi64x(0, static_cast<int64_t>(fn::EvaluateTwoPlayerCoopStatus__5Shelli));
        ctx->pc = fn::gameReestablishViews__7TheGame;
        runtime->lookupFunction(fn::gameReestablishViews__7TheGame)(rdram, ctx, runtime);
        wotmFinishGuestCall(rdram, ctx, runtime, fn::EvaluateTwoPlayerCoopStatus__5Shelli);   // ate voltar ao ra (pausa no meio)
        *ctx = saved;
        dbgSchedule("depois do retry", 120);
    }
    std::fprintf(stderr, "[coop] fase %u com %d jogadores (%d vidas cada)\n", rd32(rdram, kShell + 0x2BB0), n,
                 static_cast<int32_t>(rd32(rdram, kLives)));
}

void initPlayersHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t ra = ctx->pc == g_initPlayers.addr ? GPR_U32(ctx, 31) : 0u;
    hudRestore(rdram);
    if (!ra)
    {
        g_initPlayers.orig(rdram, ctx, runtime);
        return;
    }
    InitAfter a;
    a.on = true;
    std::copy(g_out, g_out + 4, a.was);
    if (g_initAfter.on)   // o anterior ainda pendente: o que ele ia refazer continua valendo
    {
        for (int i = 0; i < 4; ++i)
            a.was[i] = a.was[i] || g_initAfter.was[i];
        a.remapped = g_initAfter.remapped;
    }
    std::fill(g_out, g_out + 4, false);
    a.remapped = a.remapped || g_views != 0;
    if (g_views)
        for (int v = 0; v < 4; ++v)
            wr32(rdram, kViewSlot + 4u * uint32_t(v), g_viewSlotSaved[v]);
    g_views = 0;
    wotm_split4::g_coopViews = 0;
    g_initAfter = a;
    g_initPlayers.orig(rdram, ctx, runtime);
    if (ctx->pc != ra)
    {
        std::fprintf(stderr, "[coop] InitPlayers pausado no meio: cameras e tela no proximo quadro de luta\n");
        return;
    }
    g_initAfter.on = false;
    initPlayersAfter(rdram, ctx, runtime, a.was, a.remapped);
}

// ------------------------------------------------------------------ sem tutorial na fase 1 (aventura de 1 a 4 jogadores)
// As caixas de dica (Hud::addTextBoxMessage: agarrao, energia, arma especial; todas no HUD 0) e os 3 power-ups de dica que o
// centralInitAfter poe no Midtown Park (tipos 10 a 12, que so abrem uma caixa) nao aparecem.
Hook g_textBox{fn::addTextBoxMessage__3Hudi}, g_createPowerUp{fn::CreatePowerUp__8PowerUpsiUcP8_fvector};

void textBoxHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == g_textBox.addr && rd32(rdram, kGameMode) == 1u && rd32(rdram, kLevelId) == 1u)
    {
        ctx->pc = GPR_U32(ctx, 31);
        return;
    }
    g_textBox.orig(rdram, ctx, runtime);
}

void createPowerUpHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t ra = GPR_U32(ctx, 31), type = GPR_U32(ctx, 5);
    if (ctx->pc == g_createPowerUp.addr && ra >= fn::centralInitAfter__Fv && ra < fn::centralInitAfter__Fv + 0x118u && type >= 10u && type <= 12u)
    {
        SET_GPR_S32(ctx, 2, -1);
        ctx->pc = ra;
        return;
    }
    g_createPowerUp.orig(rdram, ctx, runtime);
}

// ------------------------------------------------------------------ final: o filme de cada heroi
// O Shell::GenesisMovie poe na fila a cadeia do jogador 1 (nextMovie do "_p", que leva ao "_g" e dali aos creditos, 19). Com mais
// jogadores, o playMovie troca os creditos pelo "_p" do proximo parceiro (mesma tabela do retail, pelo monstro sem a variante);
// os creditos vem depois do ultimo. Heroi sem filme proprio ou com o mesmo filme de um anterior fica de fora.
Hook g_genesis{fn::GenesisMovie__5Shell}, g_playMovie{fn::playMovie__Fv};
std::vector<uint32_t> g_endings;

uint32_t endingMovie(uint32_t sel)
{
    switch (sel)
    {
    case 0x20: return 0x15;
    case 0x60: return 0x16;
    case 0xA0: return 0x17;
    case 0x40: return 0x19;
    case 0x140: return 0x1A;
    case 0x80: return 0x1B;
    case 0x100: return 0x1C;
    case 0xE0: return 0x1E;
    default: return 0;
    }
}

void genesisHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int n = ctx->pc == g_genesis.addr ? players(rdram) : 0;
    g_genesis.orig(rdram, ctx, runtime);
    g_endings.clear();
    std::vector<uint32_t> seen{rd32(rdram, wotm::sym::nextMovie)};
    for (int i = 1; i < n; ++i)
    {
        const uint32_t sel = rd32(rdram, wotm_modes4::kMonsterSel + 4u * uint32_t(i)), mv = endingMovie(sel & ~0x1Fu);
        if (!mv || std::find(seen.begin(), seen.end(), mv) != seen.end())
            continue;
        seen.push_back(mv);
        g_endings.push_back(mv);
        std::fprintf(stderr, "[coop] final: filme %u do jogador %d na fila\n", mv, i + 1);
    }
}

void playMovieHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t next = rd32(rdram, wotm::sym::nextMovie);
    if (next == 3u)
        g_endings.clear();
    if (!g_endings.empty() && next == 19u && rd32(rdram, wotm::sym::movieEnable) == 1u && rd32(rdram, wotm::sym::needOutro))
    {
        wr32(rdram, wotm::sym::nextMovie, g_endings.front());
        g_endings.erase(g_endings.begin());
    }
    g_playMovie.orig(rdram, ctx, runtime);
}

// ------------------------------------------------------------------ diagnostico (WOTM_COOPLOG, WOTM_COOPKILL)

// WOTM_COOPLOG=1: depois de cada saida (e no comeco da luta), linhas [cooplog] com o estado das vistas: viewInfo (cs, def, viewport,
// ownCs), a camera de cada vista (monstro, POV, cs), cada jogador (cs, drawMe, m_cameraView, m_playerNum, vida) e cada barra de HUD
// (+0xF4: drawMe e posicao). Posicao de um ponto numa vista: (direita, frente, cima) nos eixos do _cs da vista, a tela aproximada
// sx, sy (-1..1 = dentro da vista; distancia de tela do ViewDef sobre a meia largura/altura) e o teste de frustum do jogo
// (viewInFOV com extensao 0 contra os planos do worldCtx da vista, os do ultimo viewUpdate): "fov 0" com sx, sy dentro = corte errado.
// WOTM_COOPKILL=<ordem>[@<segundos>] (ex.: 213@6): derruba os jogadores nessa ordem (1 a 4), um a cada N segundos de luta fora de
// cinema (padrao 6), pelo mesmo pedido da tecla 8 do menu F1 (g_wotmCheatKill); liga o WOTM_COOPLOG.
struct DbgPt
{
    float r = 0, f = 0, u = 0, sx = 0, sy = 0;
    int fov = -1;
};
DbgPt dbgPoint(uint8_t *rdram, int v, const float p[3])
{
    DbgPt d;
    const uint32_t vi = wotm::sym::viewInfo + 0x14u * uint32_t(v), cs = rd32(rdram, vi) & 0x1FFFFFFFu, def = rd32(rdram, vi + 4) & 0x1FFFFFFFu;
    if (cs < 0x1000u || def < 0x1000u)
        return d;
    float l[3] = {};
    for (uint32_t r = 0; r < 3; ++r)
        for (uint32_t k = 0; k < 3; ++k)
            l[r] += rdF(rdram, cs + 0x20u + 0x10u * r + 4u * k) * (p[k] - rdF(rdram, cs + 0x10u + 4u * k));
    d.r = l[0], d.f = l[1], d.u = l[2];
    const float fov = rdF(rdram, def), aspect = rdF(rdram, def + 0x20);
    const float hw = float(dbgRd16(rdram, def + 4) >> 1), hh = float(dbgRd16(rdram, def + 6)) * 0.5f;
    if (d.f > 1.f && hw > 0.f && hh > 0.f)
    {
        d.sx = fov * d.r / d.f / hw;
        d.sy = fov * aspect * d.u / d.f / hh;
    }
    const uint32_t w = wotm::sym::worldCtx + 0x1F0u * uint32_t(v) + 0xF0u;   // fovNormsTP[2][4][4]
    d.fov = 1;
    for (uint32_t i = 0; i < 2 && d.fov; ++i)
        for (uint32_t j = 0; j < 3; ++j)
        {
            const uint32_t n = w + 0x40u * i + 0x10u * j;
            const float dot = rdF(rdram, n) * p[0] + rdF(rdram, n + 4) * p[1] + rdF(rdram, n + 8) * p[2];
            if (dot < rdF(rdram, w + 0x40u * i + 0x30u + 4u * j))
            {
                d.fov = 0;
                break;
            }
        }
    return d;
}

int dbgPlayerOf(uint8_t *rdram, uint32_t m)
{
    m &= 0x1FFFFFFFu;
    for (int i = 0; i < 4 && m; ++i)
        if (monster(rdram, i) == m)
            return i + 1;
    return 0;
}

void dbgDump(uint8_t *rdram, const char *tag)
{
    const int n = players(rdram), nv = std::min<int>(int(rd32(rdram, wotm::sym::viewNumViews)), 4);
    std::fprintf(stderr, "[cooplog] %s: g_frame %u, vistas %d (g_views %d:", tag, rd32(rdram, wotm::sym::g_frame), nv, g_views);
    for (int v = 0; v < g_views; ++v)
        std::fprintf(stderr, " %d=P%d", v, g_map[v] + 1);
    std::fprintf(stderr, "), unificada %u, cinema %d\n", rd32(rdram, wotm::sym::gUseUnifiedView), int(wotm_split4::g_cinemaFull));
    for (int v = 0; v < 4; ++v)
    {
        const uint32_t vi = wotm::sym::viewInfo + 0x14u * uint32_t(v), cam = wotm_split4::camera(v);
        const uint32_t vcs = rd32(rdram, vi) & 0x1FFFFFFFu, ccs = rd32(rdram, cam + kCamCs) & 0x1FFFFFFFu;
        int owner = -1;   // camera cujo _cs a vista usa
        for (int c = 0; c < 4 && owner < 0; ++c)
            if (vcs && (rd32(rdram, wotm_split4::camera(c) + kCamCs) & 0x1FFFFFFFu) == vcs)
                owner = c;
        const uint32_t def = rd32(rdram, vi + 4) & 0x1FFFFFFFu;
        std::fprintf(stderr, "[cooplog]   vista %d%s: cs %x (camera %d) own %x def %x (w %u h %u fov %.0f cx %d cy %d) vp %d | camera %d: P%d pov %d cs %x\n", v,
                     v < nv ? "" : " (fora)", vcs, owner, rd32(rdram, vi + 0x10) & 0x1FFFFFFFu, def, def ? dbgRd16(rdram, def + 4) : 0u,
                     def ? dbgRd16(rdram, def + 6) : 0u, def ? rdF(rdram, def) : 0.f, def ? int32_t(rd32(rdram, def + 0x18)) : 0,
                     def ? int32_t(rd32(rdram, def + 0x1C)) : 0, int32_t(rd32(rdram, vi + 8)), v,
                     dbgPlayerOf(rdram, rd32(rdram, cam + wotm_split4::kCamMonster)), int32_t(rd32(rdram, cam + wotm_split4::kCamPov)), ccs);
        if (v < nv && vcs)
        {   // o que o ultimo viewUpdate(v) gravou: olho (-eo) contra o _cs da vista, e os planos (normal e limiar) no worldCtx
            const uint32_t w = wotm::sym::worldCtx + 0x1F0u * uint32_t(v);
            std::fprintf(stderr, "[cooplog]     worldCtx %d: olho (%.0f,%.0f,%.0f) cs (%.0f,%.0f,%.0f); planos", v, -rdF(rdram, w + 0x20), -rdF(rdram, w + 0x24),
                         -rdF(rdram, w + 0x28), rdF(rdram, vcs + 0x10), rdF(rdram, vcs + 0x14), rdF(rdram, vcs + 0x18));
            for (uint32_t i = 0; i < 2; ++i)
                for (uint32_t j = 0; j < 3; ++j)
                {
                    const uint32_t nn = w + 0xF0u + 0x40u * i + 0x10u * j;
                    std::fprintf(stderr, " [%.2f %.2f %.2f >= %.0f]", rdF(rdram, nn), rdF(rdram, nn + 4), rdF(rdram, nn + 8), rdF(rdram, w + 0xF0u + 0x40u * i + 0x30u + 4u * j));
                }
            std::fprintf(stderr, "; def planos");
            for (uint32_t k = 0; k < 8; ++k)
                std::fprintf(stderr, " %.2f/%.2f/%.2f", rdF(rdram, def + 0x30 + 0x10 * k), rdF(rdram, def + 0x34 + 0x10 * k), rdF(rdram, def + 0x38 + 0x10 * k));
            std::fprintf(stderr, "\n");
        }
    }
    for (int i = 0; i < n; ++i)
    {
        const uint32_t m = monster(rdram, i), cs = m ? rd32(rdram, m + kCs) & 0x1FFFFFFFu : 0u;
        if (!cs)
            continue;
        const float p[3] = {rdF(rdram, cs + 0x10), rdF(rdram, cs + 0x14), rdF(rdram, cs + 0x18)};
        std::fprintf(stderr, "[cooplog]   P%d: cs %x drawMe %u cameraView %d playerNum %d vida %.0f fora %d em (%.0f,%.0f,%.0f)", i + 1, cs,
                     rdram[cs + 0xC], int32_t(rd32(rdram, m + kCameraView)), int32_t(rd32(rdram, m + kPlayerNum)), rdF(rdram, m + kHealth),
                     int(g_out[i]), p[0], p[1], p[2]);
        for (int v = 0; v < nv; ++v)
        {
            const DbgPt d = dbgPoint(rdram, v, p);
            std::fprintf(stderr, " | v%d f %.0f tela (%.2f,%.2f) fov %d", v, d.f, d.sx, d.sy, d.fov);
        }
        std::fprintf(stderr, "\n");
    }
    for (int h = 0; h < 4; ++h)
    {
        const uint32_t hud = kGame + kHudStride * uint32_t(h), bar = rd32(rdram, hud + 0xF4) & 0x1FFFFFFFu;
        if (bar < 0x1000u || bar + 0x60u > PS2_RAM_SIZE)
            continue;
        const float p[3] = {rdF(rdram, bar + 0x10), rdF(rdram, bar + 0x14), rdF(rdram, bar + 0x18)};
        const int v = int32_t(rd32(rdram, hud));
        std::fprintf(stderr, "[cooplog]   HUD %d (vista %d, viewSlot %u -> P%d): barra %x drawMe %u", h, v, rd32(rdram, kViewSlot + 4u * uint32_t(h)),
                     dbgPlayerOf(rdram, kSlots + kSlotStride * rd32(rdram, kViewSlot + 4u * uint32_t(h))), bar, rdram[bar + 0xC]);
        if (v >= 0 && v < nv)
        {
            const DbgPt d = dbgPoint(rdram, v, p);
            std::fprintf(stderr, " | na vista %d: (dir %.1f, frente %.1f, cima %.1f) tela (%.2f,%.2f) fov %d", v, d.r, d.f, d.u, d.sx, d.sy, d.fov);
        }
        std::fprintf(stderr, "\n");
    }
}


std::string g_killOrder;
int g_killSecs = 6, g_killNext = 0;
uint32_t g_fightFrames = 0, g_lastFrame = 0, g_dumpAt = 0, g_killAt = 0;
char g_dumpTag[48] = {};

void dbgSchedule(const char *tag, uint32_t frames)
{
    if (!g_dbgLog)
        return;
    std::snprintf(g_dumpTag, sizeof g_dumpTag, "%s", tag);
    g_dumpAt = g_fightFrames + frames;
}

// Por quadro (comeco do rtMain, okToUnify): conta os quadros de luta fora de cinema, dispara as mortes do roteiro e os dumps
void dbgTick(uint8_t *rdram)
{
    if (!g_dbgLog || !players(rdram))
        return;
    const uint32_t f = rd32(rdram, wotm::sym::g_frame);
    if (f == g_lastFrame)
        return;
    const bool fight = !rd32(rdram, wotm::sym::moviePlaying) && !rd32(rdram, wotm::sym::gUseUnifiedView) && rd32(rdram, kInSession);
    g_lastFrame = f;
    if (!fight)
        return;
    if (++g_fightFrames == 60)
        dbgSchedule("comeco da luta", 0);
    if (g_dumpAt && g_fightFrames >= g_dumpAt)
    {
        g_dumpAt = 0;
        dbgDump(rdram, g_dumpTag);
    }
    if (!g_killOrder.empty())
    {   // roteiro: so sai quem o roteiro derruba (a vida dos outros jogadores fica cheia; o inv nao pega toda morte)
        const int n = players(rdram);
        for (int i = 0; i < n; ++i)
        {
            const uint32_t m = monster(rdram, i);
            if (m && !g_out[i] && g_killOrder.find(char('1' + i)) >= size_t(g_killNext) && rdF(rdram, m + kHealth) > 0.f)
                wrF(rdram, m + kHealth, rdF(rdram, m + 0x448));   // vida maxima
        }
        // e a das IAs inimigas: os bots nao terminam a fase antes do fim do roteiro
        for (int i = 0, ais = static_cast<int32_t>(rd32(rdram, kNumAIs)); i < ais && i < 4; ++i)
            if (const uint32_t m = monster(rdram, 4 + i); m && rdF(rdram, m + kHealth) > 0.f)
                wrF(rdram, m + kHealth, rdF(rdram, m + 0x448));
    }
    if (g_killNext < int(g_killOrder.size()) && g_fightFrames >= uint32_t(g_killSecs) * 60u * uint32_t(g_killNext + 1))
    {
        const int who = g_killOrder[size_t(g_killNext++)] - '1';
        if (who >= 0 && who < 4)
        {
            g_wotmCheatKill = 1u << who;
            std::fprintf(stderr, "[cooplog] roteiro: derruba o P%d (passo %d de %zu)\n", who + 1, g_killNext, g_killOrder.size());
            char tag[48];
            std::snprintf(tag, sizeof tag, "passo %d (P%d derrubado)", g_killNext, who + 1);
            dbgSchedule(tag, 150);
        }
    }
}

// Com o log: quem marca a morte de um monstro (setDead, StateDeath::transitionInto, volume de morte), com o quadro e o ra
Hook g_dbgSetDead{fn::setDead__7Monsterb}, g_dbgDeathInto{fn::transitionInto__10StateDeath},
    g_dbgDeathVol{fn::doDeathVolume__FP7MonsterP11TriggerHead}, g_dbgEnter{fn::enterNewState__7MonsterP12MonsterState};
void dbgDeathHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t pc = ctx->pc, a0 = GPR_U32(ctx, 4) & 0x1FFFFFFFu, a1 = GPR_U32(ctx, 5), ra = GPR_U32(ctx, 31);
    const Hook &h = pc == g_dbgSetDead.addr ? g_dbgSetDead : pc == g_dbgDeathInto.addr ? g_dbgDeathInto : pc == g_dbgEnter.addr ? g_dbgEnter : g_dbgDeathVol;
    const char *what = pc == g_dbgSetDead.addr ? "setDead" : pc == g_dbgDeathInto.addr ? "StateDeath::transitionInto" :
                       pc == g_dbgEnter.addr ? "enterNewState(morte)" : "doDeathVolume";
    bool log = pc != g_dbgSetDead.addr || (a1 & 0xFFu);
    if (pc == g_dbgEnter.addr)
    {   // so a entrada no estado de morte (vtable do StateDeath nos primeiros campos do estado)
        log = false;
        const uint32_t st = a1 & 0x1FFFFFFFu;
        for (uint32_t o = 0; o < 0x40u && st >= 0x1000u && st + o + 4u <= PS2_RAM_SIZE && !log; o += 4u)
            log = rd32(rdram, st + o) == wotm::sym::x_vt_10StateDeath;
    }
    int slot = -1;
    for (int i = 0; i < 9 && slot < 0; ++i)
        if (const uint32_t m = monster(rdram, i); m && a0 >= m && a0 < m + 0x11000u)
            slot = i;
    if (log)
        std::fprintf(stderr, "[cooplog] %s: monstro %d (a0 %x a1 %x) ra %x g_frame %u\n", what, slot, a0, a1, ra,
                     rd32(rdram, wotm::sym::g_frame));
    h.orig(rdram, ctx, runtime);
}

void install(PS2Runtime &runtime)
{
    if (!wotm_split4::g_players)
        return;
    if (const char *e = std::getenv("WOTM_COOPKILL"); e && *e)
    {
        for (const char *p = e; *p && *p != '@'; ++p)
            if (*p >= '1' && *p <= '4')
                g_killOrder += *p;
        if (const char *at = std::strchr(e, '@'))
            g_killSecs = std::clamp(std::atoi(at + 1), 2, 120);
        g_dbgLog = true;
        std::fprintf(stderr, "[cooplog] roteiro de mortes: %s, uma a cada %d s de luta\n", g_killOrder.c_str(), g_killSecs);
    }
    if (const char *e = std::getenv("WOTM_COOPLOG"); e && *e && *e != '0')
        g_dbgLog = true;
    struct H { Hook *h; PS2Runtime::RecompiledFunction fn; };
    const H hooks[] = {
        {&g_reestablish, &reestablishHook}, {&g_okToUnify, &okToUnifyHook}, {&g_startPoint, &startPointHook}, {&g_resolve, &resolveHook},
        {&g_evalStory, &evalStoryHook},     {&g_grandTotal, &grandTotalHook}, {&g_initPlayers, &initPlayersHook},
        {&g_textBox, &textBoxHook},         {&g_createPowerUp, &createPowerUpHook}, {&g_hudUpdate, &hudUpdateHook},
        {&g_highlights, &highlightsHook}, {&g_setPlayerPos, &setPlayerPosHook}, {&g_triggerAnim, &triggerAnimHook}, {&g_bossBar, &bossBarHook}, {&g_reparent, &reparentHook}, {&g_pickups, &pickupsHook},
        {&g_assBoss, &assBossHook}, {&g_spinUpd, &spinUpdHook}, {&g_spinHit, &spinHitHook}, {&g_cannon, &cannonHook}, {&g_bossState, &bossStateHook},
        {&g_bossHit, &bossHitHook}, {&g_genesis, &genesisHook}, {&g_playMovie, &playMovieHook},
        {&g_plantUpd, &plantUpdHook}, {&g_plantCam, &plantCamHook}, {&g_rootWake, &rootWakeHook}, {&g_rootUpd, &rootUpdHook},
        {&g_baseHit, &baseHitHook}, {&g_baseUpd, &baseUpdHook}, {&g_finalUpd, &finalUpdHook}, {&g_tazerUpd, &tazerUpdHook},
        {&g_tokyoUpd, &tokyoUpdHook}, {&g_craterCount, &craterCountHook}, {&g_updCinema, &updCinemaHook}, {&g_endCinema, &endCinemaHook},
        {&g_cleanUp, &cleanUpHook}, {&g_actuator, &actuatorHook},
    };
    for (const H &h : hooks)
        if (!(h.h->orig = runtime.lookupFunction(h.h->addr)))
        {
            std::fprintf(stderr, "[coop] desligado: funcao %x sem funcao no runtime\n", h.h->addr);
            return;
        }
    for (const H &h : hooks)
        runtime.registerFunction(h.h->addr, h.fn);
    if (g_dbgLog)
        for (Hook *h : {&g_dbgSetDead, &g_dbgDeathInto, &g_dbgDeathVol, &g_dbgEnter})
            if ((h->orig = runtime.lookupFunction(h->addr)))
                runtime.registerFunction(h->addr, &dbgDeathHook);
    std::fprintf(stderr, "[coop] campanha de 2 a 4 jogadores\n");
}
} // namespace wotm_coop
