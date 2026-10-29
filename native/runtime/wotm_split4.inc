// Tela dividida de 3 e 4 jogadores (NATIVO.md, "Tela dividida de 3 e 4 jogadores"). Incluido pelo wotm_overrides.cpp. Sempre instalada: age quando o Shell
// tem 3 ou 4 jogadores (m_numPlayers), postos pelo menu multijogador (wotm_padsel.inc) ou pela WOTM_MATCH com WOTM_PLAYERS=3 ou 4.
//
// O motor ja tem quase tudo para 4 vistas: viewInfo, worldCtx e viewDb com 5 entradas (a 4 e a da sombra), 4 cameras
// (Cameras::m_cameras), 4 HUDs, a viewPrevMat das particulas e os pontos de visada com 5, e o viewToggleSplitScreen dos cinemas
// nao mexe com 3 ou mais vistas. O que falta, por gancho:
//   - Vistas (saida do TheGame::gameReestablishViews nos modos 3, 6 e 7 com 3 ou 4 jogadores): quadrantes de 320 x 112 do GS, a
//     vista v na coluna v & 1 e na linha v >> 1. Com 3 jogadores a vista 2 e a linha de baixo inteira (640 x 112). Os viewDef do
//     retail sao 8 (cheio, metades, menus, unificada, sombra, carga); os dos quadrantes moram no fim da RAM de cima (kQuadDefs),
//     copiados da metade do mesmo lado com altura, origem e centro da linha e a distancia de tela da tela cheia pela metade (o FOV dos
//     640 x 224). A faixa de 640 x 112 vem do viewDef[0] com a mesma conta da altura e distancia de tela kStripFov (entre a dos
//     quadrantes, que daria ~118 graus na horizontal, e a da tela cheia, que cortaria o monstro pela metade na vertical).
//     O viewSetFov refaz planos e matrizes; a tesoura do viewDb vai junto (so o GS emulado a usa). viewInfo[v].viewport fica 1 ou 2:
//     o viewGetScreenDisplay continua dizendo "tela dividida" (as cameras usam a tabela de POV dela). m_numCameras = jogadores.
//   - viewGrappleConfig (fim do rtMain e troca da vista unificada) devolve a vista 0 ao viewDef[viewport]; na saida o quadrante volta.
//   - Sem vista unificada: o SetOkToUnify do comeco de cada quadro zera o m_camUnify de todos (o InitCCCamera* volta sem unificar; a
//     camera 2 e a do jogador 3). A pose de vitoria (StateVictory) ainda liga gUseUnifiedView com a camera 2 no vencedor; quando a
//     tela volta a dividir, as cameras 2 e 3 voltam aos jogadores 3 e 4 no POV 0. O ResetLevel poe a camera 2 no POV 3 (a unificada
//     do retail); aqui ela fica no POV que tinha.
//   - 4 jogadores: o SetAIMonster poe cada IA na camera 3 (gameInitCamera(3, ia), camera sem vista no retail). Aqui a camera 3 e a do
//     jogador 4: a IA so recebe m_cameraView 3, sem a camera (m_cameraFollows fica 0, o HUD 3 nao recebe as mensagens dela).
//   - Carga: o Shell::LoadMonstersDB monta o caminho dos jogadores 3 e 4 com o monstro do jogador 2, sem traje (peculiaridade do
//     retail). Do LoadLevelFiles ate a entrada do InitPlayers eles passam pelo caminho das IAs (m_monsterSel[4..], m_costume[2..],
//     o mesmo arquivo) e voltam a jogadores no InitPlayers; a ordem de carga e a da contagem de duplicatas dele (jogadores, depois IAs).
//     Os trajes dos jogadores 3 e 4 nao tem lugar no Shell: ficam em g_extraCostume, e o SetPlayerMonster os recebe no lugar do
//     m_costume[1] que o InitPlayers manda a todo jogador depois do 1.
//   - Motion blur: o buffer de pacotes do MotionBlurAA so tem lugar para 2 vistas; o init fica com 2 e as vistas 2 e 3 sem borrao.
//   - HUD: o Hud::initAfter e o Hud::update saem logo com indice >= 2. O HUD 2/3 vira uma copia do HUD da mesma coluna (as mensagens
//     dele tem onde morar); o update dele nao roda e o HUD das vistas 2 e 3 sai pela camada nativa (hudInitAfter, hudUpdate).
//   - Camera no POV 3 (estacionada): as cameras 2 e 3 nao tinham posicao de estacionar e iam para a origem (no inicio e na morte).
//   - Fim de rodada do modo 3: o retail so renasce com 1 ou 0 vivos (2 jogadores); aqui cada morto renasce como o que e. A meta de
//     vitorias abre o dialogo 5 (modal) como cauda do Evaluate, com o ra de quem o chamou.
//   - Fim da vitoria (LeaveUnifiedView): o caminho dos 2 jogadores (a camera 0 nao salta para a da vitoria).
//   - Renascimento de mais de 4 monstros (lista 3 com 4 pontos, "play again"): quem cai em cima de outro vai para a lista 2.
//   - Fim de arremesso de uma IA (StateThrowBack::handlePreemption): nao mexe na camera 3, que e a do jogador 4.
//   - Controles: o inputInit abre 2 portas x 4 slots (controle = porta + 2 * slot: o jogador 3 e o slot 1 da porta 0, o 4 o da porta
//     1), mas o stub do runtime so tem o slot 0. As scePad* com (porta, slot) recebem o slot 1 como 0 (estado, modo e vibracao sao
//     os da porta), o scePadPortOpen/Close do slot 1 so devolve sucesso, e o scePadRead dele le os dispositivos do jogador porta + 3
//     (wotmPadSetPortBase, wotm_pad.inc). Decidido na instalacao: o inputInit roda no boot.

void wotmPadSetPortBase(int base);   // wotm_pad.inc: o scePadRead em curso le os dispositivos da porta + base

#include "wotm_training.h"   // modo treino: HUD do boneco (trainingHud)

namespace wotm_split4
{
    static_assert(PS2_RAM_SIZE > PS2_GUEST_RAM_SIZE, "os viewDef dos quadrantes moram acima da RAM do jogo");

    constexpr uint32_t kShell = wotm::sym::shellInfo, kGame = wotm::sym::shellGame;
    constexpr uint32_t kMonsterSel = kShell + 0x2938, kNumPlayers = kShell + 0x2BB4, kNumAIs = kShell + 0x2BB8, kCostume = kShell + 0x2BE0;
    constexpr uint32_t kSlots = kGame + 0xB80, kSlotStride = 0x11190, kMonsters = kGame + 0x120380, kGameMode = kGame + 0x1203C8,
                       kNumSlots = kGame + 0x1203D4;
    constexpr uint32_t kPlayerNum = 0x18, kNumInits = 0x2C, kCameraView = 0x6CD8, kCamUnify = 0x7970;   // Monster
    constexpr uint32_t kViewInfo = wotm::sym::viewInfo, kViewDef = wotm::sym::viewDef, kViewDb = wotm::sym::viewDb,
                       kNumViews = wotm::sym::viewNumViews, kUnified = wotm::sym::gUseUnifiedView;
    constexpr uint32_t kCameras = wotm::sym::x_7Cameras_m_cameras, kNumCameras = wotm::sym::x_7Cameras_m_numCameras, kCamStride = 0xEB0,
                       kCamCs = 0xCF0, kCamMonster = 0xCF4, kCamPov = 0xCF8;
    constexpr uint32_t kDefSize = 0xC0, kDbSize = 0x360;
    constexpr uint32_t kDbScissor[4] = {0xC0, 0x140, 0x230, 0x2B0};
    constexpr uint32_t kQuadDefs = PS2_RAM_SIZE - 0x1000;   // 4 ViewDef dos quadrantes

    constexpr uint32_t kReestablish = wotm::fn::gameReestablishViews__7TheGame, kGrapple = wotm::fn::viewGrappleConfig__Fv,
                       kOkToUnify = wotm::fn::SetOkToUnify__7TheGame, kResetLevel = wotm::fn::ResetLevel__7TheGame,
                       kInitCamera = wotm::fn::gameInitCamera__7TheGameii, kLoadLevelFiles = wotm::fn::LoadLevelFiles__5Shell,
                       kInitPlayers = wotm::fn::InitPlayers__5Shell, kViewCreate = wotm::fn::viewCreate__F10_viewportsi,
                       kViewSetFov = wotm::fn::viewSetFov__Fiff, kFollow = wotm::fn::SetCameraToFollowMonster__7CamerasiP7Monster,
                       kSetPov = wotm::fn::SetCameraPOV__7CamerasiQ26Camera9CameraPOV;
    PS2Runtime::RecompiledFunction g_origReestablish = nullptr, g_origGrapple = nullptr, g_origResetLevel = nullptr, g_origInitCamera = nullptr,
                                   g_origLoadLevelFiles = nullptr, g_origInitPlayers = nullptr, g_viewCreate = nullptr, g_viewSetFov = nullptr,
                                   g_follow = nullptr, g_setPov = nullptr;

    int g_players = 0;              // teto de jogadores com os ganchos instalados (4); 0 = desligado
    int g_views = 0;                // vistas em quadrante montadas (0 = nenhuma: tela cheia ou metades do retail)
    bool g_wasUnified = false;      // a vista unificada (vitoria) estava ligada no quadro anterior
    bool g_cinemaFull = false;      // cinema em tela cheia (cinemaViews)
    int g_coopViews = 0;            // campanha co-op com jogadores fora (wotm_coop.inc): vistas com quem continua (1 a 3; 0 = a da largada)
    unsigned g_hudCalls[4] = {};    // diagnostico (WOTM_SPLITLOG): Hud::update de cada HUD desde a ultima linha
    int g_loadExtra = 0;            // jogadores 3 e 4 no caminho das IAs (do LoadLevelFiles ao InitPlayers)
    uint32_t g_extraCostume[2] = {0, 0};   // trajes dos jogadores 3 e 4 (o Shell nao tem lugar para eles)

    float rdF(uint8_t *rdram, uint32_t a) { float v; std::memcpy(&v, rdram + a, 4); return v; }
    void wrF(uint8_t *rdram, uint32_t a, float v) { std::memcpy(rdram + a, &v, 4); }
    uint32_t sel(int i) { return kMonsterSel + 4u * uint32_t(i); }
    uint32_t costume(int i) { return kCostume + 4u * uint32_t(i); }
    uint32_t camera(int v) { return kCameras + kCamStride * uint32_t(v); }

    // Chamada de uma funcao do jogo com o contexto salvo (como o callTakeDamage): a0, a1 inteiros, f12 e f13 float
    void callGuest(PS2Runtime::RecompiledFunction fn, uint32_t addr, uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t a0,
                   uint32_t a1 = 0, float f12 = 0.f, float f13 = 0.f)
    {
        if (!fn)
            return;
        const R5900Context saved = *ctx;
        ctx->r[4] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(a0)));
        ctx->r[5] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(a1)));
        ctx->r[31] = _mm_set_epi64x(0, 0);
        ctx->f[12] = f12;
        ctx->f[13] = f13;
        ctx->pc = addr;
        fn(rdram, ctx, runtime);
        wotmFinishGuestCall(rdram, ctx, runtime);
        *ctx = saved;
    }

    // Jogadores da tela em quadrantes agora (0 = nao vale): 3 ou 4 no Shell, num dos modos com tela dividida (3, 6 e 7) ou na campanha
    // co-op (modo 1, wotm_coop.inc)
    int splitPlayers(uint8_t *rdram)
    {
        const int32_t n = static_cast<int32_t>(FAST_READ32(kNumPlayers)), mode = static_cast<int32_t>(FAST_READ32(kGameMode));
        return g_players && n >= 3 && n <= 4 && (mode == 1 || mode == 3 || mode == 6 || mode == 7) ? n : 0;
    }

    constexpr float kStripFov = 2.f / 3.f;   // distancia de tela da faixa de 640 x 112 (3 jogadores), em fracao da tela cheia

    void writeQuadDefs(uint8_t *rdram, int n)
    {
        const float full = rdF(rdram, kViewDef);   // viewDef[0]: 384 em 640 de largura; o quadrante tem 320
        for (uint32_t v = 0; v < 4; ++v)
        {
            const bool strip = n == 3 && v == 2;   // 3 jogadores: a vista 2 ocupa a linha de baixo inteira
            const uint32_t src = strip ? kViewDef : kViewDef + kDefSize * (1u + (v & 1u)), dst = kQuadDefs + kDefSize * v;   // metade esquerda ou direita
            std::memcpy(rdram + dst, rdram + src, kDefSize);
            const int32_t h = FAST_READ16(src + 6) / 2, sign = (v >> 1) ? 1 : -1;
            FAST_WRITE16(dst + 6, static_cast<uint16_t>(h));                          // h
            FAST_WRITE16(dst + 0xE, static_cast<uint16_t>((v >> 1) * uint32_t(h)));   // y da tesoura
            wrF(rdram, dst + 0x14, rdF(rdram, src + 0x14) + float(sign * h / 2));    // offY: 2048 -/+ 56
            FAST_WRITE32(dst + 0x1C, static_cast<uint32_t>(static_cast<int32_t>(FAST_READ32(src + 0x1C)) + sign * h / 2));   // cy
            wrF(rdram, dst, strip ? full * kStripFov : full * 0.5f);
        }
    }

    // Vista v com o viewDef `def`: planos e matrizes (viewSetFov) e a tesoura do viewDb
    void applyDef(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t v, uint32_t def)
    {
        FAST_WRITE32(kViewInfo + 0x14u * v + 4, def);
        callGuest(g_viewSetFov, kViewSetFov, rdram, ctx, runtime, v, 0, rdF(rdram, def), rdF(rdram, def + 0x20));
        const uint64_t x = FAST_READ16(def + 0xC), y = FAST_READ16(def + 0xE), w = FAST_READ16(def + 4), h = FAST_READ16(def + 6);
        const uint64_t s = x | ((x + w - 1) << 16) | (y << 32) | ((y + h - 1) << 48);
        for (uint32_t off : kDbScissor)
            FAST_WRITE64(kViewDb + kDbSize * v + off, s);
    }
    // Vista v no seu quadrante
    void applyQuad(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t v)
    {
        applyDef(rdram, ctx, runtime, v, kQuadDefs + kDefSize * v);
    }

    // Cameras 2 e 3 de volta aos jogadores 3 e 4, no POV 0 (a vitoria as deixou no vencedor, POV 7)
    void refollow(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (g_coopViews)
            return;   // co-op com jogadores fora: o wotm_coop.inc poe cada camera no jogador da vista
        for (int v = 2; v < g_views; ++v)
        {
            const uint32_t m = FAST_READ32(kMonsters + 4u * uint32_t(v));
            if (!m)
                continue;
            if (FAST_READ32(camera(v) + kCamMonster) == m)
            {
                // o LeaveUnifiedView poe a camera 2 no POV 3 (repouso da unificada): com o jogador 3 vencedor ela ja o segue
                if (FAST_READ32(camera(v) + kCamPov) == 3u)
                    callGuest(g_setPov, kSetPov, rdram, ctx, runtime, uint32_t(v), 0u);
                continue;
            }
            FAST_WRITE32(camera(v) + kCamPov, 0);   // o SetCameraToFollowMonster recarrega o POV corrente
            callGuest(g_follow, kFollow, rdram, ctx, runtime, uint32_t(v), m);
        }
    }

    void reestablishViews(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t ra = ctx->pc == kReestablish ? GPR_U32(ctx, 31) : 0u;
        g_origReestablish(rdram, ctx, runtime);
        if (!ra || ctx->pc != ra)
            return;
        const int n = splitPlayers(rdram);
        if (!n)
        {
            g_views = 0;
            return;
        }
        writeQuadDefs(rdram, n);
        FAST_WRITE32(kNumViews, 4u);   // o viewCreate pinta o fundo de viewNumViews viewDb
        callGuest(g_viewCreate, kViewCreate, rdram, ctx, runtime, 1u, 2u);   // vista 2: o retail a deixa na viewport 5 (unificada)
        if (n == 4)
            callGuest(g_viewCreate, kViewCreate, rdram, ctx, runtime, 2u, 3u);   // vista 3: _cs e viewDb (o retail nao a cria)
        for (int v = 0; v < n; ++v)
            applyQuad(rdram, ctx, runtime, uint32_t(v));
        FAST_WRITE32(kNumViews, uint32_t(n));
        FAST_WRITE32(kNumCameras, uint32_t(n));
        if (n == 4)
            callGuest(g_setPov, kSetPov, rdram, ctx, runtime, 3u, FAST_READ32(camera(3) + kCamPov));   // o laco do original foi ate 3
        if (g_views != n)
            std::fprintf(stderr, "[split] %d jogadores: vistas em quadrantes de 320 x 112, %d cameras\n", n, n);
        g_views = n;
        g_cinemaFull = false;
        g_wasUnified = false;   // as cameras dos jogadores ja vem do InitPlayers (gameInitCamera(i, slot))
        // a camera 2 sai do InitBeforeDbLoad no POV 3 (repouso da unificada); com jogador para seguir, vai ao POV 0 como as outras
        for (int v = 2; v < n; ++v)
            if (FAST_READ32(camera(v) + kCamPov) == 3u && FAST_READ32(camera(v) + kCamMonster))
                callGuest(g_setPov, kSetPov, rdram, ctx, runtime, uint32_t(v), 0u);
    }

    // Cameras::SetCameraPOV(vista, pov): o POV 3 e a camera estacionada em +0xD20 (DetachCamera; TogglePOV alterna 0 e 3). As cameras
    // 2 e 3 do retail nunca recebem essa posicao (a 2 e a unificada, que o InitBeforeDbLoad e o ResetLevel poem no POV 3 so como
    // repouso): no POV 3 elas iam para a origem do mapa (quadrante so de neblina, no inicio e na morte do jogador). Com os quadrantes,
    // o POV 3 numa delas estaciona onde ela esta (_cs->trans), como o DetachCamera, que tambem poe a posicao no _cs antes.
    constexpr uint32_t kCamPark = 0xD20;
    PS2Runtime::RecompiledFunction g_origSetPov = nullptr;
    bool g_skipPov3 = false;   // throwPreemption de uma IA em curso: o SetCameraPOV da camera 3 (a do jogador 4) nao acontece
    void setCameraPov(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t v = GPR_U32(ctx, 4);
        if (ctx->pc == kSetPov && v == 3u && g_skipPov3)
        {
            ctx->pc = GPR_U32(ctx, 31);
            return;
        }
        if (ctx->pc == kSetPov && GPR_U32(ctx, 5) == 3u && v >= 2u && v < 4u && g_views > int(v))
        {
            const uint32_t c = camera(int(v)), cs = FAST_READ32(c + kCamCs) & 0x1FFFFFFFu;
            if (cs >= 0x1000u && cs + 0x20u <= PS2_RAM_SIZE)
                std::memcpy(rdram + c + kCamPark, rdram + cs + 0x10, 16);   // _cs->trans
        }
        g_origSetPov(rdram, ctx, runtime);
    }

    // Cameras::LeaveUnifiedView (fim da pose de vitoria, "play again", ResetLevel): com m_numMonsters 2 (dois jogadores) o retail so
    // volta a camera 2 ao POV 3 e esmaece (CCFadeOut); com outro numero (1 jogador) a camera 0 toma o lugar da 2 (a da vitoria) e
    // refaz a transicao a partir dali: com os quadrantes (m_numMonsters = jogadores e IAs) a camera do jogador 1 saltaria para onde
    // estava a vitoria. A chamada ve 2 e toma o caminho da tela dividida; o refollow devolve as cameras 2 e 3.
    constexpr uint32_t kLeaveUnified = wotm::fn::LeaveUnifiedView__7CamerasUi, kNumMonsters = kGame + 0x1203D8;
    PS2Runtime::RecompiledFunction g_origLeaveUnified = nullptr;
    void leaveUnified(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc != kLeaveUnified || !g_views || !splitPlayers(rdram))
        {
            g_origLeaveUnified(rdram, ctx, runtime);
            return;
        }
        const uint32_t n = FAST_READ32(kNumMonsters), ra = GPR_U32(ctx, 31);
        FAST_WRITE32(kNumMonsters, 2u);
        g_origLeaveUnified(rdram, ctx, runtime);
        if (ctx->pc == ra && FAST_READ32(kNumMonsters) == 2u)
            FAST_WRITE32(kNumMonsters, n);
    }

    // StateThrowBack::handlePreemption (fim de ser arremessado/derrubado): poe a camera m_cameraView do dono no POV 0 e zera +0xD98 e
    // +0xD9C dela, sem olhar se o dono e humano. Com 4 jogadores a IA tem m_cameraView 3 (initCamera) e mexia na camera do jogador 4
    // (no retail a camera 3 nao tem vista). Aqui, dono IA com a vista 3: o SetCameraPOV(3) nao acontece e os dois campos voltam.
    constexpr uint32_t kThrowPreempt = wotm::fn::handlePreemption__14StateThrowBackP12MonsterState, kStateOwner = 0xC;
    PS2Runtime::RecompiledFunction g_origThrowPreempt = nullptr;
    void throwPreemption(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t self = GPR_U32(ctx, 4) & 0x1FFFFFFFu;
        uint32_t owner = 0;
        if (ctx->pc == kThrowPreempt && self >= 0x1000u && self + 0x10u <= PS2_RAM_SIZE && splitPlayers(rdram) == 4)
            owner = FAST_READ32(self + kStateOwner) & 0x1FFFFFFFu;
        bool ai = owner >= 0x1000u && owner + kCameraView + 4u <= PS2_RAM_SIZE && FAST_READ32(owner + kCameraView) == 3u;
        for (uint32_t i = 0; i < 4 && ai; ++i)
            ai = (FAST_READ32(kMonsters + 4u * i) & 0x1FFFFFFFu) != owner;
        if (!ai)
        {
            g_origThrowPreempt(rdram, ctx, runtime);
            return;
        }
        const uint32_t c = camera(3), d98 = FAST_READ32(c + 0xD98), d9c = FAST_READ32(c + 0xD9C);
        g_skipPov3 = true;
        g_origThrowPreempt(rdram, ctx, runtime);
        g_skipPov3 = false;
        FAST_WRITE32(c + 0xD98, d98);
        FAST_WRITE32(c + 0xD9C, d9c);
    }

    // Cinema (ActionDispatch::doCinema, moviePlaying; a apresentacao dos monstros antes da luta): com 2 jogadores o doCinema chama o
    // viewToggleSplitScreen e a cena sai em tela cheia; com 3 ou mais vistas o toggle nao faz nada e so a vista 0 seguia o cinema, no
    // quadrante dela. Aqui, no comeco de cada quadro (okToUnify), o cinema ligado poe a tela numa vista so (viewNumViews 1, vista 0 com o
    // viewDef[0], 640 x 224) e o cinema desligado volta aos quadrantes; o rtMain le o numero de vistas no quadro seguinte. As cameras
    // das outras vistas seguem os monstros por baixo.
    constexpr uint32_t kMoviePlaying = wotm::sym::moviePlaying;
    void cinemaViews(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const bool movie = FAST_READ32(kMoviePlaying) != 0 && !FAST_READ32(kUnified);
        if (movie == g_cinemaFull)
            return;
        g_cinemaFull = movie;
        if (movie)
        {
            FAST_WRITE32(kNumViews, 1u);
            applyDef(rdram, ctx, runtime, 0u, kViewDef);
        }
        else
        {
            // o endCinema chama o viewToggleSplitScreen, que com 1 vista recria a 0 e a 1 como metades (viewCreate): todas de volta
            FAST_WRITE32(kNumViews, uint32_t(g_views));
            for (int v = 0; v < g_views; ++v)
            {
                FAST_WRITE32(kViewInfo + 0x14u * uint32_t(v) + 8, 1u + (uint32_t(v) & 1u));   // viewport: metade do mesmo lado
                applyQuad(rdram, ctx, runtime, uint32_t(v));
            }
        }
    }

    void grappleConfig(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t ra = ctx->pc == kGrapple ? GPR_U32(ctx, 31) : 0u;
        g_origGrapple(rdram, ctx, runtime);
        if (ra && ctx->pc == ra && g_views && !FAST_READ32(kUnified) && splitPlayers(rdram))
        {
            // co-op com 2 vistas: o original ja devolveu a vista 0 a metade da viewport dela
            if (g_cinemaFull || g_coopViews == 1)
                applyDef(rdram, ctx, runtime, 0u, kViewDef);
            else if (g_coopViews != 2)
                applyQuad(rdram, ctx, runtime, 0u);
        }
    }

    // Comeco de cada quadro do rtMain (gameSetOkToUnify): sem unificar; a camera volta aos jogadores quando a vitoria termina
    PS2Runtime::RecompiledFunction g_origOkToUnify = nullptr;
    void okToUnify(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc != kOkToUnify || !g_views || !splitPlayers(rdram))
        {
            g_origOkToUnify(rdram, ctx, runtime);
            return;
        }
        cinemaViews(rdram, ctx, runtime);
        static const bool log = std::getenv("WOTM_SPLITLOG") != nullptr;   // uma linha por segundo: camera de cada vista
        static unsigned frames = 0;
        if (log && ++frames % 60 == 0)
        {
            std::fprintf(stderr, "[split] cameras:");
            for (int v = 0; v < g_views; ++v)
            {
                const uint32_t cs = FAST_READ32(camera(v) + kCamCs), vcs = FAST_READ32(kViewInfo + 0x14u * uint32_t(v));
                std::fprintf(stderr, " %d: monstro %x (jogador %x) pov %d cs %x vista %x em %.0f,%.0f,%.0f;", v, FAST_READ32(camera(v) + kCamMonster),
                             FAST_READ32(kMonsters + 4u * uint32_t(v)), static_cast<int32_t>(FAST_READ32(camera(v) + kCamPov)), cs, vcs,
                             rdF(rdram, (vcs & 0x1FFFFFFFu) + 0x10), rdF(rdram, (vcs & 0x1FFFFFFFu) + 0x14), rdF(rdram, (vcs & 0x1FFFFFFFu) + 0x18));
            }
            std::fprintf(stderr, " hud.update %u/%u/%u/%u\n", g_hudCalls[0], g_hudCalls[1], g_hudCalls[2], g_hudCalls[3]);
            for (unsigned &n : g_hudCalls)
                n = 0;
        }
        const uint32_t slots = std::min<uint32_t>(FAST_READ32(kNumSlots), 16u);
        for (uint32_t i = 0; i < slots; ++i)
            FAST_WRITE32(kSlots + kSlotStride * i + kCamUnify, 0);
        if (FAST_READ32(kUnified))
            g_wasUnified = true;
        else if (g_wasUnified)
        {
            g_wasUnified = false;
            refollow(rdram, ctx, runtime);
        }
        ctx->pc = GPR_U32(ctx, 31);
    }

    void resetLevel(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const bool keep = ctx->pc == kResetLevel && g_views && splitPlayers(rdram);
        const uint32_t ra = GPR_U32(ctx, 31), pov = FAST_READ32(camera(2) + kCamPov);
        g_origResetLevel(rdram, ctx, runtime);
        if (keep && ctx->pc == ra && FAST_READ32(camera(2) + kCamPov) != pov)
            callGuest(g_setPov, kSetPov, rdram, ctx, runtime, 2u, pov);
    }

    // TheGame::gameInitCamera(view, slot): com 4 jogadores a camera 3 fica com o jogador 4 (a IA nao a leva)
    void initCamera(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kInitCamera && GPR_U32(ctx, 5) == 3u && splitPlayers(rdram) == 4)
        {
            // jogador = um dos m_monsters[0..3] (o SetPlayerMonster ja o gravou); o m_playerNum nao serve: com WOTM_AIVSAI o jogador
            // tambem tem 2
            const uint32_t slot = GPR_U32(ctx, 6);
            const uint32_t m = kSlots + kSlotStride * slot;
            bool player = false;
            for (uint32_t i = 0; i < 4; ++i)
                player = player || FAST_READ32(kMonsters + 4u * i) == m;
            if (slot < 16u && !player)
            {
                FAST_WRITE32(m + kCameraView, 3u);
                ctx->pc = GPR_U32(ctx, 31);
                return;
            }
        }
        g_origInitCamera(rdram, ctx, runtime);
    }

    // Jogadores 3 e 4 para o caminho das IAs (carga) e de volta (InitPlayers). As IAs andam extra posicoes; m_costume[2 + j] vai ate
    // o indice 11 (0x2C0C, antes do m_mode): no maximo 10 entre jogadores extras e IAs.
    void toLoadLayout(uint8_t *rdram)
    {
        const int n = static_cast<int32_t>(FAST_READ32(kNumPlayers)), extra = n - 2;
        int ais = std::clamp(static_cast<int32_t>(FAST_READ32(kNumAIs)), 0, 10 - extra);
        for (int j = ais - 1; j >= 0; --j)
        {
            FAST_WRITE32(sel(4 + j + extra), FAST_READ32(sel(4 + j)));
            FAST_WRITE32(costume(2 + j + extra), FAST_READ32(costume(2 + j)));
        }
        for (int k = 0; k < extra; ++k)
        {
            FAST_WRITE32(sel(4 + k), FAST_READ32(sel(2 + k)));
            FAST_WRITE32(costume(2 + k), g_extraCostume[k]);
        }
        FAST_WRITE32(kNumPlayers, 2u);
        FAST_WRITE32(kNumAIs, uint32_t(ais + extra));
        g_loadExtra = extra;
    }

    void toPlayLayout(uint8_t *rdram)
    {
        const int extra = g_loadExtra, ais = std::max(0, static_cast<int32_t>(FAST_READ32(kNumAIs)) - extra);
        for (int k = 0; k < extra; ++k)
            FAST_WRITE32(sel(2 + k), FAST_READ32(sel(4 + k)));
        for (int j = 0; j < ais; ++j)
        {
            FAST_WRITE32(sel(4 + j), FAST_READ32(sel(4 + j + extra)));
            FAST_WRITE32(costume(2 + j), FAST_READ32(costume(2 + j + extra)));
        }
        for (int k = 0; k < extra; ++k)
        {
            FAST_WRITE32(sel(4 + ais + k), 0u);
            FAST_WRITE32(costume(2 + ais + k), 0u);
        }
        FAST_WRITE32(kNumPlayers, uint32_t(2 + extra));
        FAST_WRITE32(kNumAIs, uint32_t(ais));
        g_loadExtra = 0;
    }

    void loadLevelFiles(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kLoadLevelFiles)
        {
            if (g_loadExtra)   // a carga anterior nao chegou ao InitPlayers
                toPlayLayout(rdram);
            const int32_t n = static_cast<int32_t>(FAST_READ32(kNumPlayers));
            if (n >= 3 && n <= 4 && FAST_READ32(kShell + 0x2C10) != 6u)   // m_mode 6 carrega os 10 monstros por conta propria
                toLoadLayout(rdram);
        }
        g_origLoadLevelFiles(rdram, ctx, runtime);
    }

    void initPlayers(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kInitPlayers && g_loadExtra)
            toPlayLayout(rdram);
        g_origInitPlayers(rdram, ctx, runtime);
    }

    // TheGame::SetPlayerMonster(jogador, tipo, dup, vista, traje): o InitPlayers manda m_costume[1] a todo jogador depois do 1; os
    // jogadores 3 e 4 recebem o deles (o do arquivo que a carga leu)
    constexpr uint32_t kSetPlayer = wotm::fn::SetPlayerMonster__7TheGameiiiii;
    PS2Runtime::RecompiledFunction g_origSetPlayer = nullptr;
    void setPlayerMonster(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t player = GPR_U32(ctx, 5);
        if (ctx->pc == kSetPlayer && player >= 2u && player < 4u && splitPlayers(rdram))
            ctx->r[9] = _mm_set_epi64x(0, static_cast<int64_t>(g_extraCostume[player - 2u]));   // t1
        g_origSetPlayer(rdram, ctx, runtime);
    }

    // Hud::update(hud): o retail sai logo no comeco com indice (+0) >= 2. O resto usa o indice para o monstro (m_viewSlot[indice]), a
    // camera (m_cameras[indice], _cs em +0xCF0), o lado do texto (0 a esquerda, os outros a direita) e o drawMe das barras de vida (os
    // _cs em +0xF4 dos HUDs 0 e 1: acende a do proprio indice e apaga a do outro; a do proprio HUD e posta na frente da camera). Com os
    // quadrantes, o HUD v = 2 ou 3 roda como o HUD p = v & 1 (mesma coluna): durante a chamada o indice dele vira p e o m_viewSlot[p],
    // o _cs da camera p e a barra do HUD p passam a ser os da vista v; tudo volta na saida. Depois, so a barra deste HUD fica acesa (o
    // rtMain chama o update de cada vista logo antes do hier dela). Com m_numMonsters != 2 a posicao das barras e a da tela cheia
    // (+0x100 no HUD), que e a forma do quadrante.
    constexpr uint32_t kHudUpdate = wotm::fn::update__3Hud, kHudStride = 0x2E0, kHudBarCs = 0xF4, kViewSlot = kGame + 0x1203E8;
    PS2Runtime::RecompiledFunction g_origHudUpdate = nullptr;
    uint32_t hudBar(uint8_t *rdram, uint32_t i)
    {
        const uint32_t cs = FAST_READ32(kGame + kHudStride * i + kHudBarCs) & 0x1FFFFFFFu;
        return cs >= 0x1000u && cs + 0x10u <= PS2_RAM_SIZE ? cs : 0u;
    }
    // Animacoes do HUD por vista. O HUD 2/3 e uma copia do HUD da coluna: os _animHandle (+0x130..+0x1F0) apontam para os mesmos
    // _animCharInstance, entao o que um comeca o outro mostra (o flash de furia h180 do jogador 3, posto pelo updateBoostAndRage, e o
    // fim dele aparecendo e sumindo no HUD do jogador 1). Cada HUD guarda aqui o estado das instancias da coluna: dos nos de controle
    // (animCtx[k] +0x14..+0x34: inicio, tempo, velocidade, ativo, laco, iteracoes, cache de chave), a arvore principal do AnimPlayer
    // (+0x8, so no de controle ou nada; o animationStart so troca esse ponteiro) e a saida (animOutput.val). Um so esta na RAM por
    // coluna (g_hudLive); o hudUse troca, marcando sujo (animOutput.dirty) cada canal cujo valor mudou, para o hier refazer as
    // matrizes que guardou do outro. As barras (AnimBlend da lista ativa) continuam divididas: cada update reaplica a porcentagem.
    // Ordem no quadro: update 0 (volta o estado 0), hier 0, animationRunGlobal (avalia o 0), update 1, hier 1, update 2 (guarda o 0,
    // poe o 2 e o avalia com o UpdateAnimations, como o animationRunGlobal faria), hier 2, update 3, hier 3.
    // Barra de furia (AnimBlend em +0x268, sobre o handle +0x170): o no de mistura mora dentro do HUD. O update faz rampIn quando o no
    // nao esta ativo (+0x274) e reaplica o tempo dele (toTime, +0x2A0: porcentagem da furia, ou velocidade 1 com ela cheia). Na copia do
    // HUD 2/3 o no e outro, mas o handle e o do HUD da coluna: o rampIn dela punha um segundo no na lista ativa do mesmo AnimPlayer, e
    // as duas vistas mostravam a camada de cima (a furia do jogador 3 na barra do 1 e vice-versa). O no da copia fica marcado ativo
    // (nunca entra na lista); o toTime dele vai para o no da coluna antes da avaliacao, e o do no da coluna entra no estado por HUD.
    constexpr uint32_t kHudHandles = 0x130, kHudHandleN = 13, kCtlState = 0x14, kCtlStateLen = 0x20, kCtlSize = 0x3C;
    constexpr uint32_t kHudRageTime = 0x268 + 0x8 + 0x30, kAnimTimeLen = 0x10;
    struct HudAnimState
    {
        uint32_t ci[4] = {};
        int n = 0;
        std::vector<uint8_t> data;
        uint8_t rageTime[kAnimTimeLen] = {};   // toTime do no de mistura da barra de furia da coluna
    };
    HudAnimState g_hudAnim[4];
    uint32_t g_hudLive[2] = {0, 1};   // HUD cujo estado esta na RAM, por coluna

    uint32_t ciOk(uint8_t *rdram, uint32_t ci)
    {
        ci &= 0x1FFFFFFFu;
        return ci >= 0x1000u && ci + 0x30u <= PS2_RAM_SIZE ? ci : 0u;
    }
    // Instancias distintas dos handles do HUD i
    int hudInstances(uint8_t *rdram, uint32_t hud, uint32_t out[4])
    {
        int n = 0;
        for (uint32_t k = 0; k < kHudHandleN; ++k)
        {
            const uint32_t ci = ciOk(rdram, FAST_READ32(hud + kHudHandles + 0x10u * k));
            if (!ci || std::find(out, out + n, ci) != out + n)
                continue;
            if (n == 4)
                break;
            out[n++] = ci;
        }
        return n;
    }
    // Campos de uma instancia: AnimPlayer, animCtx, val, dirty, animacoes e canais (0 em tudo se algo nao fizer sentido)
    struct CiFields { uint32_t player = 0, ctx = 0, val = 0, dirty = 0, anims = 0, chans = 0; };
    CiFields ciFields(uint8_t *rdram, uint32_t ci)
    {
        CiFields f;
        const uint32_t chr = ciOk(rdram, FAST_READ32(ci + 0x20));
        f.player = ciOk(rdram, FAST_READ32(ci + 0x24));
        f.ctx = ciOk(rdram, FAST_READ32(ci + 0x10));
        f.val = ciOk(rdram, FAST_READ32(ci + 4));
        f.dirty = ciOk(rdram, FAST_READ32(ci + 8));
        f.anims = chr ? FAST_READ16(chr) : 0u;
        f.chans = chr ? FAST_READ16(chr + 2) : 0u;
        if (!chr || !f.player || !f.ctx || !f.val || !f.dirty || f.anims > 64u || f.chans > 512u ||
            f.ctx + kCtlSize * f.anims > PS2_RAM_SIZE || f.val + 4u * f.chans > PS2_RAM_SIZE)
            f = CiFields{};
        return f;
    }
    size_t ciBytes(const CiFields &f) { return kCtlStateLen * f.anims + 4u + 4u * f.chans; }

    void hudAnimSave(uint8_t *rdram, uint32_t hud, HudAnimState &s)
    {
        s.n = hudInstances(rdram, hud, s.ci);
        std::memcpy(s.rageTime, rdram + hud + kHudRageTime, kAnimTimeLen);
        s.data.clear();
        for (int i = 0; i < s.n; ++i)
        {
            const CiFields f = ciFields(rdram, s.ci[i]);
            const size_t at = s.data.size();
            s.data.resize(at + ciBytes(f));
            uint8_t *d = s.data.data() + at;
            for (uint32_t k = 0; k < f.anims; ++k, d += kCtlStateLen)
                std::memcpy(d, rdram + f.ctx + kCtlSize * k + kCtlState, kCtlStateLen);
            if (f.player)
                std::memcpy(d, rdram + f.player + 8, 4);
            std::memcpy(d + 4, rdram + f.val, 4u * f.chans);
        }
    }
    // false: as instancias mudaram (outra fase); o estado guardado nao vale
    bool hudAnimLoad(uint8_t *rdram, uint32_t hud, const HudAnimState &s)
    {
        uint32_t ci[4] = {};
        const int n = hudInstances(rdram, hud, ci);
        if (n != s.n || !std::equal(ci, ci + n, s.ci))
            return false;
        std::memcpy(rdram + hud + kHudRageTime, s.rageTime, kAnimTimeLen);
        const uint8_t *d = s.data.data(), *end = d + s.data.size();
        for (int i = 0; i < n; ++i)
        {
            const CiFields f = ciFields(rdram, ci[i]);
            if (d + ciBytes(f) > end)
                return false;
            for (uint32_t k = 0; k < f.anims; ++k, d += kCtlStateLen)
                std::memcpy(rdram + f.ctx + kCtlSize * k + kCtlState, d, kCtlStateLen);
            if (!f.player)
            {
                d += 4u;
                continue;
            }
            // arvore principal: so no de controle desta instancia, ou nada (um no de mistura do pool ficaria em duas arvores)
            uint32_t main;
            std::memcpy(&main, d, 4);
            const uint32_t m = main & 0x1FFFFFFFu, cur = FAST_READ32(f.player + 8) & 0x1FFFFFFFu;
            const auto ctlNode = [&](uint32_t a) { return !a || (a >= f.ctx && a < f.ctx + kCtlSize * f.anims && (a - f.ctx) % kCtlSize == 0); };
            if (ctlNode(m) && ctlNode(cur))
                FAST_WRITE32(f.player + 8, main);
            for (uint32_t c = 0; c < f.chans; ++c)
            {
                const uint32_t a = f.val + 4u * c;
                if (std::memcmp(rdram + a, d + 4 + 4u * c, 4) != 0)
                {
                    std::memcpy(rdram + a, d + 4 + 4u * c, 4);
                    rdram[f.dirty + c / 8u] |= uint8_t(1u << (c & 7u));
                }
            }
            d += 4u + 4u * f.chans;
        }
        return true;
    }
    // Poe na RAM o estado das animacoes do HUD `who` (coluna who & 1). Primeira vez do HUD 2/3: parte do estado atual da coluna.
    void hudUse(uint8_t *rdram, uint32_t who)
    {
        const uint32_t col = who & 1u, hud = kGame + kHudStride * col, live = g_hudLive[col];
        if (live == who)
            return;
        hudAnimSave(rdram, hud, g_hudAnim[live]);
        HudAnimState &s = g_hudAnim[who];
        if (s.n == 0 || !hudAnimLoad(rdram, hud, s))
            s = g_hudAnim[live];   // estado novo = o da coluna (ja esta na RAM)
        g_hudLive[col] = who;
    }
    void hudAnimReset()
    {
        for (HudAnimState &s : g_hudAnim)
            s = HudAnimState{};
        g_hudLive[0] = 0;
        g_hudLive[1] = 1;
    }
    // Modo treino (wotm_training.h) na luta de 1 jogador: o HUD do boneco e o HUD 1 do proprio jogo (o do jogador 2, espelhado a
    // direita, que o banco do HUD ja traz e o initAfter ja ligou). Na saida do Hud::update do HUD 0, o update do HUD 1 com o
    // m_viewSlot[1] trocado pelo slot do boneco (o primeiro m_monsters[i >= 1] vivo) durante a chamada. Fora da vista unificada e no modo de entrada do
    // versus o update prende o HUD i ao _cs da camera i (m_cameras[i]); a 1 segue o boneco, entao a chamada usa o _cs da camera 0.
    void trainingHud(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (!wotm_training::g_on.load(std::memory_order_relaxed) || !(wotm_training::g_opts.load(std::memory_order_relaxed) & wotm_training::kTrainHud) ||
            FAST_READ32(kNumPlayers) != 1u || splitPlayers(rdram))
            return;
        uint32_t slot = ~0u;
        for (uint32_t i = 1; i < 16 && slot == ~0u; ++i)
        {
            const uint32_t m = FAST_READ32(kMonsters + 4u * i) & 0x1FFFFFFFu;
            if (m >= kSlots && (m - kSlots) % kSlotStride == 0 && (m - kSlots) / kSlotStride < 16u)
                slot = (m - kSlots) / kSlotStride;
        }
        if (slot == ~0u)
            return;
        static bool s_logged = false;
        if (!s_logged)
        {
            s_logged = true;
            std::fprintf(stderr, "[treino] HUD do boneco: HUD 1 do jogo no slot %u (cs %08X)\n", slot, FAST_READ32(kGame + kHudStride + kHudBarCs));
        }
        // o HUD i fica preso ao _cs da camera i; com 1 jogador a camera 1 nao e a da tela (segue o boneco), entao nesta chamada o
        // HUD 1 usa o _cs da camera 0, como os HUDs 2 e 3 na tela em quadrantes (hudUpdate)
        const uint32_t saved = FAST_READ32(kViewSlot + 4u), savedCs = FAST_READ32(camera(1) + kCamCs);
        FAST_WRITE32(kViewSlot + 4u, slot);
        FAST_WRITE32(camera(1) + kCamCs, FAST_READ32(camera(0) + kCamCs));
        // fora da vista unificada o update do HUD 1 age como na tela dividida: liga a barra dele e apaga a do HUD 0 (+0xC do _cs da
        // barra, o drawMe); a do HUD 0 volta como o update dele a deixou
        const uint32_t bar0 = FAST_READ32(kGame + kHudBarCs) & 0x1FFFFFFFu;
        const uint8_t draw0 = bar0 ? FAST_READ8(bar0 + 0xC) : 0;
        callGuest(g_origHudUpdate, kHudUpdate, rdram, ctx, runtime, kGame + kHudStride);
        if (bar0)
            FAST_WRITE8(bar0 + 0xC, draw0);
        FAST_WRITE32(kViewSlot + 4u, saved);
        FAST_WRITE32(camera(1) + kCamCs, savedCs);
    }

    // Modo treino: o update do HUD 0 pausado no meio (a fatia de tempo acabou dentro dele). A retomada nao passa por este gancho e,
    // no fim dela, o HUD 0 apaga a barra do HUD 1: o do boneco (trainingHud) fica para trainingHudLate, antes da captura do quadro.
    // Sem isso a barra do boneco sumia nesses quadros (piscava).
    bool g_trainPending = false;

    // Gancho do timerWaitForMinUpdateRate (wotm_overrides.cpp), antes do wotmCaptureFrame: o update do quadro ja terminou
    void trainingHudLate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (!g_trainPending)
            return;
        g_trainPending = false;
        trainingHud(rdram, ctx, runtime);
    }

    // HUD dos jogadores 3 e 4 (HUD 2 e 3) desenhado pela camada nativa (wotmPlayerHud, wotm_overrides.cpp). O Hud::update do retail
    // sai logo com indice >= 2, e a copia do HUD da coluna (hudInitAfter) divide com ele o _cs da barra e as instancias das animacoes:
    // rodar o update dela misturava os HUDs de cima e de baixo. Aqui o update do HUD 2/3 nao roda: a barra da coluna fica apagada no
    // hier desta vista (o update do HUD de cima a acende de novo no quadro seguinte, antes do hier dele), as mensagens saem pelo
    // updateMessage dele (com o indice da coluna, o lado do texto) e o "WINS n" do versus como o Hud::update o escreve. O HUD vai
    // para o canto de cima da vista a meia escala (o tamanho dos HUDs de cima no quadrante); na faixa de baixo dos 3 jogadores, no
    // mesmo lugar da janela que o da vista 0.
    constexpr uint32_t kUpdateMessage = wotm::fn::updateMessage__3Hud;
    PS2Runtime::RecompiledFunction g_updateMessage = nullptr;
    constexpr uint32_t kHudWinsBuf = kQuadDefs + 4u * kDefSize;   // texto do "WINS n" (acima dos 4 ViewDef dos quadrantes)

    // Chamada de uma funcao do jogo pelo endereco, com ate 5 argumentos inteiros (a0..t0); devolve o v0
    uint32_t callGuestN(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t addr, std::initializer_list<uint32_t> args)
    {
        const PS2Runtime::RecompiledFunction fn = runtime->lookupFunction(addr);
        if (!fn)
            return 0u;
        const R5900Context saved = *ctx;
        int r = 4;
        for (uint32_t a : args)
            ctx->r[r++] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(a)));
        ctx->r[31] = _mm_set_epi64x(0, 0);
        ctx->pc = addr;
        fn(rdram, ctx, runtime);
        wotmFinishGuestCall(rdram, ctx, runtime);
        const uint32_t v0 = GPR_U32(ctx, 2);
        *ctx = saved;
        return v0;
    }

    void nativeHud(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t self, uint32_t me)
    {
        const uint32_t p = me & 1u;
        for (uint32_t i = 0; i < 4u; ++i)
            if (const uint32_t b = hudBar(rdram, i))
                FAST_WRITE8(b + 0xC, 0);
        if (FAST_READ32(kUnified) || g_cinemaFull || !FAST_READ32(wotm::sym::gHudEnable) || FAST_READ32(wotm::sym::x_3Hud_m_letterbox + 0x2Cu))
            return;
        const uint32_t slot = FAST_READ32(kViewSlot + 4u * me);
        if (slot >= 16u)
            return;
        const uint32_t m = kSlots + kSlotStride * slot;
        const uint32_t def = FAST_READ32(kViewInfo + 0x14u * me + 4u) & 0x1FFFFFFFu;
        if (def < 0x1000u || def + 0x10u > PS2_RAM_SIZE)
            return;
        const float y0 = FAST_READ16(def + 0xE), sy = float(FAST_READ16(def + 6)) / 224.f;
        const bool strip = me == 2u && (g_coopViews ? g_coopViews : g_views) == 3;
        // na faixa (640 de largura, foco kStripFov) o x das barras sai a 1/4 da janela + (x - 320) * 2/3 da escala do quadrante: o
        // canto da vista 0 (x/2 no quadrante) fica em 80 + 3/4 x
        if (strip)
            wotmPlayerHud(rdram, m, int(me), 80.f, y0, 0.75f, sy, false);
        else
            wotmPlayerHud(rdram, m, int(me), 320.f * float(p), y0, 0.5f, sy, p != 0u);

        // mensagens (combos, energia ganha, vidas): o updateMessage le o lado do texto no indice
        FAST_WRITE32(self, p);
        callGuest(g_updateMessage, kUpdateMessage, rdram, ctx, runtime, self);
        FAST_WRITE32(self, me);

        // "WINS n" (Hud::update, modos 2 a 6, depois de 60 fields): cinza, letra 0x140 x 0xA0, a esquerda (indice 0) ou a direita
        const uint32_t mode = FAST_READ32(kGameMode);
        if (mode < 2u || mode > 6u)
            return;
        if (static_cast<int32_t>(callGuestN(rdram, ctx, runtime, wotm::fn::timerGetFieldCount__Fv, {})) < 0x3D)
            return;
        constexpr uint32_t kMonsterWins = 0x3C;
        const int n = std::snprintf(reinterpret_cast<char *>(rdram + kHudWinsBuf), 16, "WINS %d", static_cast<int32_t>(FAST_READ32(m + kMonsterWins)));
        if (n <= 0)
            return;
        // tela 1 do viewGetScreenDisplay fora da vista unificada: x 0x20 / 0x118; senao 0x23 / 0x258 (y 0xC8 nos dois)
        const bool narrow = callGuestN(rdram, ctx, runtime, wotm::fn::viewGetScreenDisplay__Fv, {}) == 1u;
        callGuestN(rdram, ctx, runtime, wotm::fn::fontSetColor__Fiiiii, {0u, 0x5Au, 0x5Au, 0x5Au, 0x80u});
        callGuestN(rdram, ctx, runtime, wotm::fn::fontSetCharSizesInSubPixels__Fiiiii,
                   {0u, 0x140u, 0xA0u, static_cast<uint32_t>(-0x11), static_cast<uint32_t>(-0x15)});
        if (p == 0u)
            callGuestN(rdram, ctx, runtime, wotm::fn::fontSpritePrintXY__FiiiPc, {0u, narrow ? 0x20u : 0x23u, 0xC8u, kHudWinsBuf});
        else
            callGuestN(rdram, ctx, runtime, wotm::fn::fontSpritePrintRightXY__FiiiPc, {0u, narrow ? 0x118u : 0x258u, 0xC8u, kHudWinsBuf});
        callGuestN(rdram, ctx, runtime, wotm::fn::fontSetDefaultColor__Fi, {0u});
        callGuestN(rdram, ctx, runtime, wotm::fn::fontSetDefaultSize__Fi, {0u});
    }

    void hudUpdate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t self = GPR_U32(ctx, 4) & 0x1FFFFFFFu;
        const bool entry = ctx->pc == kHudUpdate;
        const uint32_t me = self >= kGame && (self - kGame) % kHudStride == 0 ? (self - kGame) / kHudStride : 4u;
        if (entry && me < 4u)
            ++g_hudCalls[me];
        if (entry && me >= 2u && me < 4u && g_views && splitPlayers(rdram))
        {
            const uint32_t ra = GPR_U32(ctx, 31);
            nativeHud(rdram, ctx, runtime, self, me);
            ctx->pc = ra;
            return;
        }
        const uint32_t ra = GPR_U32(ctx, 31);
        g_origHudUpdate(rdram, ctx, runtime);
        if (entry && self == kGame)
        {
            g_trainPending = ctx->pc != ra;
            if (!g_trainPending)
                trainingHud(rdram, ctx, runtime);
        }
    }

    // Hud::initAfter(i) tambem nao faz nada com i >= 2 (sem _cs da barra nem animacoes; o no do HUD em +0xE4 so existe nos HUDs 0 e
    // 1). Na saida do initAfter do HUD 2 ou 3 ele vira uma copia do HUD da mesma coluna (i & 1), ja inicializado (o main os chama de
    // 0 a 3), com o indice dele de volta: os dois dividem o _cs e as animacoes da barra, que cada vista refaz no seu Hud::update
    // logo antes do hier dela.
    constexpr uint32_t kHudInitAfter = wotm::fn::initAfter__3Hudi;
    PS2Runtime::RecompiledFunction g_origHudInitAfter = nullptr;
    void hudInitAfter(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t self = GPR_U32(ctx, 4) & 0x1FFFFFFFu, i = GPR_U32(ctx, 5);
        const uint32_t ra = ctx->pc == kHudInitAfter ? GPR_U32(ctx, 31) : 0u;
        g_origHudInitAfter(rdram, ctx, runtime);
        if (ra && ctx->pc == ra && i == 0u)
            hudAnimReset();   // HUDs novos (o main os inicializa de 0 a 3): o estado guardado das animacoes nao vale mais
        if (!ra || ctx->pc != ra || i < 2u || i >= 4u || self != kGame + kHudStride * i || !g_players)
            return;
        const int32_t n = static_cast<int32_t>(FAST_READ32(kNumPlayers));
        if (n < 3 || static_cast<int32_t>(i) >= n)
            return;
        std::memcpy(rdram + self, rdram + kGame + kHudStride * (i & 1u), kHudStride);
        FAST_WRITE32(self, i);
    }

    // Monster::updateBoostAndRage (furia cheia: flash h180) e Monster::blowUpMonster (morte: h1D0) comecam uma animacao do HUD
    // m_cameraView do monstro seguido por camera. Os handles do HUD 2/3 sao os do HUD da coluna: o monstro das vistas 2 e 3 usa na
    // chamada o estado guardado dele (hudUse), e o HUD de cima nao pisca nem toca a morte do jogador de baixo. O HUD nativo das
    // vistas 2 e 3 nao le essas animacoes.
    constexpr uint32_t kBoostRage = wotm::fn::updateBoostAndRage__7Monster, kBlowUp = wotm::fn::blowUpMonster__7MonsterP7Monster;
    PS2Runtime::RecompiledFunction g_origBoostRage = nullptr, g_origBlowUp = nullptr;
    void lowerHudCall(PS2Runtime::RecompiledFunction orig, uint32_t addr, uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t m = GPR_U32(ctx, 4) & 0x1FFFFFFFu;
        const uint32_t view = ctx->pc == addr && m >= 0x1000u && m + kCameraView + 4u <= PS2_RAM_SIZE ? FAST_READ32(m + kCameraView) : 0u;
        if (view < 2u || view >= 4u || !g_views || !splitPlayers(rdram))
        {
            orig(rdram, ctx, runtime);
            return;
        }
        const uint32_t before = g_hudLive[view & 1u], ra = GPR_U32(ctx, 31);
        hudUse(rdram, view);
        orig(rdram, ctx, runtime);
        wotmFinishGuestCall(rdram, ctx, runtime, ra);
        hudUse(rdram, before);
    }
    void boostAndRage(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime) { lowerHudCall(g_origBoostRage, kBoostRage, rdram, ctx, runtime); }
    void blowUpMonster(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime) { lowerHudCall(g_origBlowUp, kBlowUp, rdram, ctx, runtime); }

    // Fim de rodada do modo 3 (Shell::EvaluateGameStatus -> EvaluateMultiPlayerBattleStatusNoAI ou ...AI, com r = 1 quando alguem
    // morre, 3 quando saem). O NoAI do retail so trata "sobrou 1 vivo" e "nenhum vivo": com 3 ou 4 jogadores uma morte deixa 2 ou 3
    // vivos, ninguem renasce e a rodada seguinte termina logo (o morto continua morto). O AI renasce os slots 0 e 1 como jogador e os
    // outros como IA (os jogadores 3 e 4 virariam IA). Aqui, com os quadrantes, os dois viram a regra do AI por m_monsters: com meta
    // de vitorias (m_killTarget) e um matador vivo que chegou nela, dialogo 5 (vencedor em m_won[0]); senao cada morto renasce como o
    // que e (jogadores 0..3 pelo playerInit, IAs pelo aiInit) e a sessao continua.
    constexpr uint32_t kEvalNoAI = wotm::fn::EvaluateMultiPlayerBattleStatusNoAI__5Shelli,
                       kEvalAI = wotm::fn::EvaluateMultiPlayerBattleStatusAI__5Shelli, kPlayerInit = wotm::fn::playerInit__7Monster,
                       kAiInit = wotm::fn::aiInit__7Monster, kDisplayDialog = wotm::fn::displayDialog__Fi;
    constexpr uint32_t kHealth = 0x44C, kDead = 0xE8, kKiller = 0x846C, kMonsterNum = 0x28, kWins = 0x3C;   // Monster
    constexpr uint32_t kInSession = kShell + 0x2BCC, kInMenus = kShell + 0x2BD0;
    PS2Runtime::RecompiledFunction g_origEvalNoAI = nullptr, g_origEvalAI = nullptr;
    void evaluateBattle(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const bool entry = ctx->pc == kEvalNoAI || ctx->pc == kEvalAI;
        if (!entry || !g_views || !splitPlayers(rdram) || FAST_READ32(kGameMode) != 3u)
        {
            (ctx->pc == kEvalAI ? g_origEvalAI : g_origEvalNoAI)(rdram, ctx, runtime);
            return;
        }
        const int32_t r = static_cast<int32_t>(GPR_U32(ctx, 5));
        const uint32_t ra = GPR_U32(ctx, 31);
        ctx->pc = ra;
        if (r == 3)
        {
            FAST_WRITE32(kInMenus, 0u);
            FAST_WRITE32(kInSession, 0u);
            return;
        }
        int32_t killer = -1;
        const uint32_t slots = std::min<uint32_t>(FAST_READ32(kNumSlots), 16u);
        for (uint32_t i = 0; i < slots && killer < 0; ++i)
        {
            const uint32_t s = kSlots + kSlotStride * i;
            const uint32_t by = FAST_READ32(s + kKiller) & 0x1FFFFFFFu;
            if (FAST_READ8(s + kDead) && by && !FAST_READ8(by + kDead))
                killer = static_cast<int32_t>(FAST_READ32(by + kMonsterNum));
        }
        const int32_t target = static_cast<int32_t>(FAST_READ32(kShell + 0x2A44));   // m_killTarget
        if (target > 0 && killer >= 0 && killer < 16 &&
            static_cast<int32_t>(FAST_READ32(kSlots + kSlotStride * uint32_t(killer) + kWins)) >= target)
        {
            // o displayDialog e modal (roda quadros e cede ao escalonador ate a escolha): chamado como cauda, com o ra de quem chamou o
            // Evaluate. Pelo callGuest (ra 0) a retomada dele voltava ao pc 0 no fim e a thread do jogo morria ("play again")
            FAST_WRITE32(kGame + 0x12043C, uint32_t(killer));
            ctx->r[4] = _mm_set_epi64x(0, 5);
            ctx->pc = kDisplayDialog;
            runtime->lookupFunction(kDisplayDialog)(rdram, ctx, runtime);
            return;
        }
        const int players = splitPlayers(rdram), ais = static_cast<int32_t>(FAST_READ32(kGame + 0x1203E0));
        for (int i = 0; i < 4 + ais; ++i)
        {
            if (i >= players && i < 4)
                continue;
            const uint32_t m = FAST_READ32(kMonsters + 4u * uint32_t(i)) & 0x1FFFFFFFu;
            if (!m || !(rdF(rdram, m + kHealth) <= 0.f))
                continue;
            const uint32_t fn = i < 4 ? kPlayerInit : kAiInit;   // o do WOTM_AIVSAI (playerInit -> aiInit) vale: busca a cada vez
            callGuest(runtime->lookupFunction(fn), fn, rdram, ctx, runtime, m);
        }
        FAST_WRITE32(kInMenus, 0u);
        FAST_WRITE32(kInSession, 1u);
    }

    // TheGame::gameGetStartPoint(m): nos modos 2 a 6, o primeiro nascimento de um humano (m_playerNum 1) usa a lista 1 do StartPoints
    // e o de uma IA a lista 2, em rodizio (getNextPoint), recusando ponto com algo a menos de 120; os renascimentos usam a lista 3. A
    // lista 1 tem os pontos dos 2 jogadores do retail (no Centro: 2; a 2 e a 3 tem 4, um em cada canto): os jogadores 3 e 4 pegavam o
    // ponto do 1 (ocupado) e caiam no do 2, os tres no mesmo lugar. Com os quadrantes, o jogador alem do numero de pontos da lista 1
    // nasce como uma IA (m_playerNum 2 durante a chamada): lista 2. Diagnostico com WOTM_SPLITLOG (as listas so uma vez).
    constexpr uint32_t kStartPoint = wotm::fn::gameGetStartPoint__7TheGameP7Monster, kPointTypes = wotm::sym::x_11StartPoints_s_pointTypes;
    PS2Runtime::RecompiledFunction g_origStartPoint = nullptr;
    void logPointLists(uint8_t *rdram)
    {
        for (uint32_t t = 0; t < 4; ++t)
        {
            const uint32_t pts = FAST_READ32(kPointTypes + 16u * t) & 0x1FFFFFFFu, n = FAST_READ32(kPointTypes + 16u * t + 4);
            std::fprintf(stderr, "[split] pontos tipo %u: %u (proximo %u):", t, n, FAST_READ32(kPointTypes + 16u * t + 12));
            for (uint32_t i = 0; i < n && i < 32 && pts >= 0x1000u; ++i)
                std::fprintf(stderr, " (%.0f,%.0f,%.0f)", rdF(rdram, pts + 0x20u * i), rdF(rdram, pts + 0x20u * i + 4), rdF(rdram, pts + 0x20u * i + 8));
            std::fprintf(stderr, "\n");
        }
    }
    // Algum outro dos m_monsters a menos de 120 (no plano) do m_monsters[who]
    bool nearOther(uint8_t *rdram, int who)
    {
        const uint32_t m = FAST_READ32(kMonsters + 4u * uint32_t(who)) & 0x1FFFFFFFu, cs = m ? FAST_READ32(m + 0xC) & 0x1FFFFFFFu : 0u;
        for (int j = 0; j < 12 && cs >= 0x1000u; ++j)
        {
            const uint32_t o = FAST_READ32(kMonsters + 4u * uint32_t(j)) & 0x1FFFFFFFu, ocs = o ? FAST_READ32(o + 0xC) & 0x1FFFFFFFu : 0u;
            if (j == who || ocs < 0x1000u)
                continue;
            const float dx = rdF(rdram, ocs + 0x10) - rdF(rdram, cs + 0x10), dy = rdF(rdram, ocs + 0x14) - rdF(rdram, cs + 0x14);
            if (dx * dx + dy * dy < 120.f * 120.f)
                return true;
        }
        return false;
    }
    // Ainda em cima de outro depois da lista 2 (o dodgeball, modo 7, so usa a lista 1, de 2 pontos, e a arena nao tem outras): o
    // ponto onde caiu girado em torno do centro dos pontos da lista 1, 90 graus para o jogador 3 e -90 para o 4 (ou 180), um pouco
    // acima; o Monster::setTrans e o hdReparentCsGrid como o gameGetStartPoint faz, e a gravidade assenta.
    constexpr uint32_t kStartVec = PS2_RAM_SIZE - 0x400;   // _fvector para o setTrans (abaixo dos textos do wotm_modes4.inc)
    void rotatedStart(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t m, int who, bool log)
    {
        const uint32_t pts = FAST_READ32(kPointTypes + 16u) & 0x1FFFFFFFu, n = FAST_READ32(kPointTypes + 16u + 4u);
        const uint32_t cs = FAST_READ32(m + 0xC) & 0x1FFFFFFFu;
        if (pts < 0x1000u || n == 0 || n > 32 || cs < 0x1000u)
            return;
        float cx = 0.f, cy = 0.f;
        for (uint32_t i = 0; i < n; ++i)
        {
            cx += rdF(rdram, pts + 0x20u * i);
            cy += rdF(rdram, pts + 0x20u * i + 4);
        }
        cx /= float(n);
        cy /= float(n);
        for (int k = 0; k < 3; ++k)   // 90, -90, 180 graus: o primeiro livre
        {
            const float s = k == 0 ? (who & 1 ? -1.f : 1.f) : k == 1 ? (who & 1 ? 1.f : -1.f) : 0.f, c = k == 2 ? -1.f : 0.f;
            const float x = rdF(rdram, cs + 0x10) - cx, y = rdF(rdram, cs + 0x14) - cy;
            wrF(rdram, kStartVec, cx + x * c - y * s);
            wrF(rdram, kStartVec + 4, cy + x * s + y * c);
            wrF(rdram, kStartVec + 8, rdF(rdram, cs + 0x18) + 50.f);
            wrF(rdram, kStartVec + 12, 0.f);
            callGuest(runtime->lookupFunction(wotm::fn::setTrans__7MonsterR8_fvector), wotm::fn::setTrans__7MonsterR8_fvector, rdram, ctx, runtime,
                      m, kStartVec);
            callGuest(runtime->lookupFunction(wotm::fn::hdReparentCsGrid__FP3_cs), wotm::fn::hdReparentCsGrid__FP3_cs, rdram, ctx, runtime, cs);
            if (!nearOther(rdram, who))
                break;
        }
        if (log)
            std::fprintf(stderr, "[split] largada: m_monsters[%d] no ponto girado em torno de %.0f,%.0f: %.0f,%.0f\n", who, cx, cy,
                         rdF(rdram, cs + 0x10), rdF(rdram, cs + 0x14));
    }

    void startPoint(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static const bool log = std::getenv("WOTM_SPLITLOG") != nullptr;
        const uint32_t m = GPR_U32(ctx, 5) & 0x1FFFFFFFu, ra = ctx->pc == kStartPoint ? GPR_U32(ctx, 31) : 0u, self = GPR_U32(ctx, 4);
        static bool listed = false;
        if (log && ra && !listed)
        {
            listed = true;
            logPointLists(rdram);
        }
        int who = -1;
        for (int i = 0; i < 12 && who < 0; ++i)
            if ((FAST_READ32(kMonsters + 4u * uint32_t(i)) & 0x1FFFFFFFu) == m)
                who = i;
        const int32_t humanPoints = static_cast<int32_t>(FAST_READ32(kPointTypes + 16u + 4u));   // lista 1
        // campanha co-op (modo 1): a lista 2 e a das IAs; o wotm_coop.inc poe os jogadores ao lado do 1
        const bool coop = FAST_READ32(kGameMode) == 1u;
        const bool asAi = ra && !coop && who >= humanPoints && who < 4 && splitPlayers(rdram) && FAST_READ32(m + kPlayerNum) == 1u;
        if (asAi)
            FAST_WRITE32(m + kPlayerNum, 2u);
        g_origStartPoint(rdram, ctx, runtime);
        if (asAi)
            FAST_WRITE32(m + kPlayerNum, 1u);
        // Renascimento (lista 3, 4 pontos) com mais de 4 monstros: no "play again" todos renascem juntos e o quinto e o sexto caem no
        // ponto de outro (todos ocupados: fica o ultimo tentado). Dois monstros no mesmo lugar: o empurrao entre eles dava NaN na posicao
        // (a camera do jogador ia junto). Monstro a menos de 120 de outro: de novo como o primeiro nascimento de uma IA (lista 2, os
        // cantos, com o mesmo teste de ponto livre).
        if (ra && ctx->pc == ra && who >= 0 && !coop && splitPlayers(rdram))
        {
            const uint32_t cs = FAST_READ32(m + 0xC) & 0x1FFFFFFFu;
            bool near = false;
            for (int j = 0; j < 12 && cs >= 0x1000u && !near; ++j)
            {
                const uint32_t o = FAST_READ32(kMonsters + 4u * uint32_t(j)) & 0x1FFFFFFFu, ocs = o ? FAST_READ32(o + 0xC) & 0x1FFFFFFFu : 0u;
                if (j == who || ocs < 0x1000u)
                    continue;
                const float dx = rdF(rdram, ocs + 0x10) - rdF(rdram, cs + 0x10), dy = rdF(rdram, ocs + 0x14) - rdF(rdram, cs + 0x14);
                near = dx * dx + dy * dy < 120.f * 120.f;
            }
            if (near)
            {
                const uint32_t inits = FAST_READ32(m + kNumInits), pn = FAST_READ32(m + kPlayerNum);
                FAST_WRITE32(m + kNumInits, 0u);
                FAST_WRITE32(m + kPlayerNum, 2u);
                callGuest(g_origStartPoint, kStartPoint, rdram, ctx, runtime, self, m);
                FAST_WRITE32(m + kNumInits, inits);
                FAST_WRITE32(m + kPlayerNum, pn);
                if (log)
                    std::fprintf(stderr, "[split] largada: m_monsters[%d] em cima de outro, de novo pela lista 2\n", who);
                if (nearOther(rdram, who))
                    rotatedStart(rdram, ctx, runtime, m, who, log);
            }
        }
        if (log && ra && ctx->pc == ra)
        {
            const uint32_t cs = FAST_READ32(m + 0xC) & 0x1FFFFFFFu;
            std::fprintf(stderr, "[split] largada: m_monsters[%d] (playerNum %d) em %.0f,%.0f,%.0f\n", who,
                         static_cast<int32_t>(FAST_READ32(m + kPlayerNum)), rdF(rdram, cs + 0x10), rdF(rdram, cs + 0x14), rdF(rdram, cs + 0x18));
        }
    }

    // MotionBlurAA::init(vistas, ...) monta um pacote de 0x100 bytes por vista a partir de D_007319F0 + 0x8D0, num bloco de 0xB30: so
    // cabem 2 (a terceira passaria do fim do bss). Com os quadrantes o init fica com 2 vistas e o drawOverlay(campo, impar, vista) das
    // vistas 2 e 3 nao manda nada (o nativo nao desenha o borrao; so o GS emulado o veria).
    constexpr uint32_t kBlurInit = wotm::fn::init__12MotionBlurAAiff, kBlurDraw = wotm::fn::drawOverlay__12MotionBlurAAiii;
    PS2Runtime::RecompiledFunction g_origBlurInit = nullptr, g_origBlurDraw = nullptr;
    void blurInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kBlurInit && static_cast<int32_t>(GPR_U32(ctx, 4)) > 2 && splitPlayers(rdram))
            ctx->r[4] = _mm_set_epi64x(0, 2);
        g_origBlurInit(rdram, ctx, runtime);
    }
    void blurDraw(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kBlurDraw && GPR_U32(ctx, 6) >= 2u && splitPlayers(rdram))
        {
            ctx->pc = GPR_U32(ctx, 31);
            return;
        }
        g_origBlurDraw(rdram, ctx, runtime);
    }

    // scePad* com (porta, slot): o slot 1 vira o 0 da mesma porta; o scePadRead dele le os dispositivos da porta + 2
    constexpr uint32_t kPadSlotFns[] = {wotm::fn::scePadRead,          wotm::fn::scePadGetState,      wotm::fn::scePadGetReqState,
                                        wotm::fn::scePadSetReqState,   wotm::fn::scePadInfoAct,       wotm::fn::scePadInfoMode,
                                        wotm::fn::scePadSetMainMode,   wotm::fn::scePadSetActDirect,  wotm::fn::scePadSetActAlign,
                                        wotm::fn::scePadGetButtonMask, wotm::fn::scePadSetButtonInfo, wotm::fn::scePadInfoPressMode,
                                        wotm::fn::scePadEnterPressMode, wotm::fn::scePadExitPressMode};
    constexpr size_t kPadSlotN = sizeof(kPadSlotFns) / sizeof(kPadSlotFns[0]);
    PS2Runtime::RecompiledFunction g_padOrig[kPadSlotN] = {}, g_origPortOpen = nullptr, g_origPortClose = nullptr;

    void padSlotCall(size_t i, uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (GPR_U32(ctx, 5) != 1u)
        {
            g_padOrig[i](rdram, ctx, runtime);
            return;
        }
        const bool read = kPadSlotFns[i] == wotm::fn::scePadRead;
        ctx->r[5] = _mm_set_epi64x(0, 0);
        if (read)
            wotmPadSetPortBase(2);
        g_padOrig[i](rdram, ctx, runtime);
        if (read)
            wotmPadSetPortBase(0);
        ctx->r[5] = _mm_set_epi64x(0, 1);
    }
    template <size_t I>
    void padSlotMirror(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime) { padSlotCall(I, rdram, ctx, runtime); }
    constexpr PS2Runtime::RecompiledFunction kPadSlotHooks[kPadSlotN] = {
        &padSlotMirror<0>, &padSlotMirror<1>, &padSlotMirror<2>, &padSlotMirror<3>,   &padSlotMirror<4>,   &padSlotMirror<5>,   &padSlotMirror<6>,
        &padSlotMirror<7>, &padSlotMirror<8>, &padSlotMirror<9>, &padSlotMirror<10>, &padSlotMirror<11>, &padSlotMirror<12>, &padSlotMirror<13>};

    void portOpen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (GPR_U32(ctx, 5) != 1u)
        {
            g_origPortOpen(rdram, ctx, runtime);
            return;
        }
        const uint32_t buf = GPR_U32(ctx, 6) & 0x1FFFFFFFu;   // scePadPortOpen(porta, slot, buffer de 32 bytes): o stub o zera
        if (buf >= 0x1000u && buf + 32u <= PS2_RAM_SIZE)
            std::memset(rdram + buf, 0, 32);
        SET_GPR_S32(ctx, 2, 1);
        ctx->pc = GPR_U32(ctx, 31);
    }

    void portClose(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (GPR_U32(ctx, 5) != 1u)
        {
            g_origPortClose(rdram, ctx, runtime);
            return;
        }
        SET_GPR_S32(ctx, 2, 1);
        ctx->pc = GPR_U32(ctx, 31);
    }

    // Antes dos ganchos do scePadRead da camera livre e do lock-on alternado: eles embrulham este e veem o slot de verdade
    void install(PS2Runtime &runtime)
    {
        constexpr int n = 4;   // teto de jogadores (com 1 ou 2 no Shell os ganchos deixam o jogo como no retail)
        g_viewCreate = runtime.lookupFunction(kViewCreate);
        g_viewSetFov = runtime.lookupFunction(kViewSetFov);
        g_follow = runtime.lookupFunction(kFollow);
        g_setPov = g_origSetPov = runtime.lookupFunction(kSetPov);
        g_origReestablish = runtime.lookupFunction(kReestablish);
        g_origGrapple = runtime.lookupFunction(kGrapple);
        g_origOkToUnify = runtime.lookupFunction(kOkToUnify);
        g_origResetLevel = runtime.lookupFunction(kResetLevel);
        g_origInitCamera = runtime.lookupFunction(kInitCamera);
        g_origLoadLevelFiles = runtime.lookupFunction(kLoadLevelFiles);
        g_origInitPlayers = runtime.lookupFunction(kInitPlayers);
        g_origSetPlayer = runtime.lookupFunction(kSetPlayer);
        g_origHudUpdate = runtime.lookupFunction(kHudUpdate);
        g_origStartPoint = runtime.lookupFunction(kStartPoint);
        g_origHudInitAfter = runtime.lookupFunction(kHudInitAfter);
        g_origEvalNoAI = runtime.lookupFunction(kEvalNoAI);
        g_origEvalAI = runtime.lookupFunction(kEvalAI);
        g_origBlurInit = runtime.lookupFunction(kBlurInit);
        g_origBlurDraw = runtime.lookupFunction(kBlurDraw);
        g_origBoostRage = runtime.lookupFunction(kBoostRage);
        g_origBlowUp = runtime.lookupFunction(kBlowUp);
        g_origThrowPreempt = runtime.lookupFunction(kThrowPreempt);
        g_origLeaveUnified = runtime.lookupFunction(kLeaveUnified);
        g_updateMessage = runtime.lookupFunction(kUpdateMessage);
        g_origPortOpen = runtime.lookupFunction(wotm::fn::scePadPortOpen);
        g_origPortClose = runtime.lookupFunction(wotm::fn::scePadPortClose);
        bool padsOk = g_origPortOpen && g_origPortClose;
        for (size_t i = 0; i < kPadSlotN; ++i)
            padsOk = padsOk && (g_padOrig[i] = runtime.lookupFunction(kPadSlotFns[i])) != nullptr;
        if (!g_viewCreate || !g_viewSetFov || !g_follow || !g_setPov || !g_origReestablish || !g_origGrapple || !g_origOkToUnify ||
            !g_origResetLevel || !g_origInitCamera || !g_origLoadLevelFiles || !g_origInitPlayers || !g_origSetPlayer || !g_origHudUpdate || !g_origStartPoint || !g_origHudInitAfter || !g_origEvalNoAI || !g_origEvalAI ||
            !g_origBlurInit || !g_origBlurDraw || !g_origBoostRage || !g_origBlowUp || !g_origThrowPreempt || !g_origLeaveUnified || !g_updateMessage || !padsOk)
        {
            std::fprintf(stderr, "[split] tela dividida de %d desligada: funcao do jogo ou scePad* sem funcao no runtime\n", n);
            return;
        }
        g_players = n;
        runtime.registerFunction(kReestablish, &reestablishViews);
        runtime.registerFunction(kGrapple, &grappleConfig);
        runtime.registerFunction(kSetPov, &setCameraPov);
        runtime.registerFunction(kOkToUnify, &okToUnify);
        runtime.registerFunction(kResetLevel, &resetLevel);
        runtime.registerFunction(kInitCamera, &initCamera);
        runtime.registerFunction(kLoadLevelFiles, &loadLevelFiles);
        runtime.registerFunction(kInitPlayers, &initPlayers);
        runtime.registerFunction(kSetPlayer, &setPlayerMonster);
        runtime.registerFunction(kHudUpdate, &hudUpdate);
        runtime.registerFunction(kStartPoint, &startPoint);
        runtime.registerFunction(kHudInitAfter, &hudInitAfter);
        runtime.registerFunction(kEvalNoAI, &evaluateBattle);
        runtime.registerFunction(kEvalAI, &evaluateBattle);
        runtime.registerFunction(kBlurInit, &blurInit);
        runtime.registerFunction(kBlurDraw, &blurDraw);
        runtime.registerFunction(kBoostRage, &boostAndRage);
        runtime.registerFunction(kBlowUp, &blowUpMonster);
        runtime.registerFunction(kThrowPreempt, &throwPreemption);
        runtime.registerFunction(kLeaveUnified, &leaveUnified);
        runtime.registerFunction(wotm::fn::scePadPortOpen, &portOpen);
        runtime.registerFunction(wotm::fn::scePadPortClose, &portClose);
        for (size_t i = 0; i < kPadSlotN; ++i)
            runtime.registerFunction(kPadSlotFns[i], kPadSlotHooks[i]);
        std::fprintf(stderr, "[split] ate %d jogadores: tela em quadrantes, controles 3 e 4 no slot 1 das portas\n", n);
    }
}

// Selecao de monstros do menu multijogador (wotm_padsel.inc): traje dos jogadores 3 e 4 (o Shell so guarda os dos jogadores 1 e 2)
void wotmMultiSetCostume(int player, uint32_t costume)
{
    if (player >= 2 && player < 4)
        wotm_split4::g_extraCostume[player - 2] = costume;
}
uint32_t wotmMultiCostume(int player)
{
    return player >= 2 && player < 4 ? wotm_split4::g_extraCostume[player - 2] : 0u;
}
