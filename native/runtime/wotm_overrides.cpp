// Correcoes especificas de War of the Monsters (SCUS_971.97) aplicadas por substituicao de funcoes do codigo recompilado.
//
// Limite do updateRate: `timerGetUpdateRate` (0x2236D8) devolve D_006F8DB8, quantos quadros por segundo o ultimo quadro
// do jogo "valeu" (60 = 1 vsync). A fisica integra por Euler com `dt = 1 / rate` (mola do pelo em BoneSpring::update,
// giro dos monstros em MonsterDynamics::updatePosition, veiculos, detritos...). No retail o rate e 60 ou 30; neste
// runtime o jogo roda a ~20 quadros logicos/s e o rate oscila quadro a quadro entre 60 e 8, ou seja, dt ate 7x maior
// que o previsto. Com esse dt os integradores ficam instaveis: o pelo do Congar estica pela cena toda e o controle de giro
// (amortecimento ~ -40.5 * velRot, estavel so com dt < 2/40.5, rate >= 21) inverte e amplifica a velocidade a cada quadro
// (monstro girando sozinho, a camera atras dele gira junto).
// A correcao devolve no minimo WOTM_RATEMIN (padrao 30; 0 desliga) para o jogo inteiro: nos quadros lentos o jogo anda
// um pouco mais devagar em vez de pular, e todo integrador fica na margem para a qual foi feito. Nada muda quando o rate
// ja e >= 30 (retail). Substitui os limites pontuais anteriores (WOTM_SPRINGMIN na mola, WOTM_TURNMIN no giro).
#include "ps2_runtime.h"
#include "wotm_menucpus.h"   // CPUs 4..7 da tela 12
#include "wotm_syms.h"   // wotm::sym / wotm::fn: enderecos do ELF por nome (tools/native_syms.py --gen)
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

extern std::atomic<unsigned long long> g_wotmIdleUs;
void wotmHostProfWatchThisThread();                            // ps2_hostprof.cpp (PS2X_HOSTPROF)
void wotmHostProfDump(long long t0, long long t1, const char *tag);
double wotmThreadCpuMs();
void wotmHostProfFrameStart();
extern bool g_wotmCycleProf;
extern uint64_t g_wotmEeCycle, g_wotmIdleCycles;
extern uint32_t g_wotmProfPc[4096];
extern uint64_t g_wotmProfCyc[4096];
extern uint32_t g_wotmFreeSpinPc;
void wotmCaptureFrame(const uint8_t *rdram);   // wotm_live.inc: copia o quadro pronto para o desenho nativo
void wotmCaptureScene(const uint8_t *rdram, int view);   // wotm_live.inc: cena (objetos, paletas, camera) na saida do hier(v, 0)
void wotmVu1SceneExit(const uint8_t *rdram);   // wotm_vu1check.inc: registro 'S' do instantaneo, no mesmo ponto
void wotmFlareCapture(const uint8_t *rdram);   // wotm_live.inc: lens flare projetado e testado
void wotmThreeMileFlash(const uint8_t *rdram);   // wotm_live.inc: clarao branco da usina da 3-MILE
void wotmFadeView(const uint8_t *rdram, int view, uint32_t alpha, bool queued);   // wotm_live.inc: fade preto da vista inteira, pelos argumentos do emissor
void wotmTheGameFade(const uint8_t *rdram, uint32_t self);   // wotm_live.inc: saida dos fades do TheGame
void wotmLetterbox(const uint8_t *rdram, float alpha);   // wotm_live.inc: faixas pretas do menu, pelo alfa do letterboxDmaData
void wotmGsPktQueueSent();   // wotm_live.inc: a fila do gsPkt foi despachada (DmaGsPktQueue)
void wotmFontSent();         // wotm_live.inc: o texto do quadro foi despachado (fontDmaFontData)
// wotm_live.inc: texto pelas chamadas da fonte (wotm::FontText), na entrada de cada uma
void wotmFontInitPacket(uint32_t vram);
void wotmFontSetTexId(const uint8_t *rdram, uint32_t node);
void wotmFontColor(const uint8_t *rdram, int font);
void wotmFontGlyph(const uint8_t *rdram, int font, int ch);
void wotmFontDma(const uint8_t *rdram);
void wotmShellFade(const uint8_t *rdram, int view, uint32_t r, uint32_t g, uint32_t b, int start, int end, int step, bool redraw);
void wotmShellFadeStep();       // wotm_live.inc: entrada de um sceGsSyncV do laco do Shell::FadeScreen
void wotmShellFadeStepDone();   // wotm_live.inc: volta dele
void wotmCaptureFadeStep(const uint8_t *rdram);   // wotm_live.inc: cena de um passo do Shell::FadeScreen com redesenho
void wotmLoadFreeze(bool on);   // wotm_live.inc: carga da fase (LoadLevelFiles ate o rtMain), a tela repete o ultimo quadro
bool wotmPauseBlock(PS2Runtime *rt);   // wotm_foto.inc: F7/F6, segura a thread do EE enquanto pausado
bool wotmPauseTookFrame();             // wotm_foto.inc: o quadro que acabou teve parada (pausa ou foto)
void wotmPickupHighlights(const uint8_t *rdram);   // wotm_live.inc: colorQuad dos pick-ups destacados, na saida do enableHighlights
void wotmScreenPoly(const uint8_t *rdram, int src, int x, int y, uint32_t pd);   // wotm_live.inc: um quadrado do ScreenPolys, na entrada do polyBuildPrim
bool wotmScreenPolyBox(const uint8_t *rdram, int src, int x, int y, uint32_t pd, float box[4]);   // wotm_live.inc: caixa do quadrado, sem monta-lo
void wotmBarRect(float x0, float y0, float x1, float y1, uint8_t r, uint8_t g, uint8_t b, uint8_t a);   // wotm_live.inc: retangulo das barras
void wotmBarTex(float x0, float y0, float x1, float y1, uint64_t tex0, float u0, float v0, float u1, float v1);   // wotm_live.inc: retrato
void wotmHudAnchor(const uint8_t *rdram, float cx, float cy, const float *world, bool bars);   // wotm_live.inc: icone de vida (modo foto)
void wotmHudAnchorEnd();
void wotmBarView(int view);   // wotm_live.inc: vista dos retangulos de barra que vem a seguir
void wotmBarHud(bool on);   // wotm_live.inc: os retangulos que vem a seguir sao do HUD nativo de um jogador (por cima das barras)
void wotmPlayerHud(uint8_t *rdram, uint32_t m, int view, float ox, float oy, float sx, float sy, bool mirror);   // HUD nativo de um jogador
float wotmHealthIconShift(const uint8_t *rdram, float rx);   // wotm_live.inc: x da barra com o limite da vista widescreen
void wotmDamagePopup(const uint8_t *rdram, const float p[3], float amount, int kind);   // wotm_live.inc: numero de dano (0 golpe, 1 arremesso, 2 especial)
void wotmPadSelInstall(PS2Runtime &runtime);   // wotm_padsel.inc: escolha de lados do 2 jogadores no telao do menu
void wotmLevelSelInstall(PS2Runtime &runtime);   // wotm_levelsel.inc: fases escondidas e RANDOM na selecao de fase
void wotmMainMenuInstall(PS2Runtime &runtime);   // wotm_mainmenu.inc: menu principal em texto

// Trapacas de debug: a janela (menu F1, wotm_live.inc) escreve, a thread do EE (monsterUpdateCheats) le. Bits em wotm_cheats.h.
#include "wotm_cheats.h"
#include "wotm_guestcall.h"   // termina as chamadas a codigo do jogo feitas de dentro de ganchos
#include "wotm_settings.hpp"   // opcoes ao vivo (painel "recompiled options" e launcher)
std::atomic<uint32_t> g_wotmCheats{0};
std::atomic<bool> g_wotmCheatWin{false};
std::atomic<uint32_t> g_wotmCheatKill{0};

// Camera livre de debug (F5): a janela (wotm_freecam.inc) move a pose, a thread do EE a poe no _cs da vista 0 (freeCamWrite).
#include "wotm_freecam.h"
#include "wotm_mouse.hpp"   // mouse na camera moderna (wotm_orbit.inc consome, a janela acumula)
std::atomic<bool> g_wotmFreeCam{false};
std::atomic<bool> g_wotmFreeCamPad{false};
std::atomic<float> g_wotmMechaReticle{0.f};   // retícula da camera da torre do Mecha (wotm_custom.inc grava, wotm_live.inc desenha)
std::mutex g_wotmFreeCamM;
WotmFreeCamPose g_wotmFreeCamPose;

// Save em JSON ao lado do mc0/ (NATIVO.md, "Save em JSON"): ganchos no MonsterMc::stdInit/stdExit.
#include "wotm_save.inc"
// "Press start" do menu principal: X vale como Start e o botao vale ja durante a transicao.
#include "wotm_menu.inc"
// Tela dividida de 3 e 4 jogadores (NATIVO.md, "Tela dividida de 3 e 4 jogadores"; WOTM_PLAYERS=3 ou 4): vistas, cameras, carga e controles 3 e 4.
#include "wotm_split4.inc"
// Eliminacao e minigames com 3 e 4 jogadores (por cima dos ganchos do wotm_split4.inc)
#include "wotm_modes4.inc"
// Campanha com 2 a 4 jogadores (TODO 13; por cima dos ganchos do wotm_split4.inc e do wotm_modes4.inc)
#include "wotm_coop.inc"
// Pasta mods/ que sobrepoe arquivos do disco (MODS.md): fileCdSearchFile e sceCdRead.
#include "wotm_mods.inc"
// Skins de mod na luta (item 8): sons do traje base e IAs sorteadas com skin
#include "wotm_skins.inc"
// Monstros custom (item 8, parte 2): Goliath Prime jogavel (visivel com IAs, toma dano delas)
#include "wotm_custom.inc"
// Modo treino (item 9): esferas dos golpes, dano exato e boneco (WOTM_TRAINING, menu F2)
#include "wotm_training.inc"
// Log das acoes da IA (WOTM_AILOG)
#include "wotm_ai.inc"

namespace
{
    constexpr uint32_t kTimerGetUpdateRate = wotm::fn::timerGetUpdateRate__Fv;
    constexpr uint32_t kUpdateRateVar = 0x6F8DB8u;   // D_006F8DB8, lido pelo gp

    int g_minRate = 30;
    unsigned long long g_clamped = 0;

    // Fields do quadro = vblanks que passaram desde o inicio dele (relogio do WotM, g_wotmFreeSpinPc != 0).
    // O jogo mede o quadro pelo timer 1: timer1Handler soma ceil(T1 / 9619) ao contador de fields (D_006F8DBC) e espera o
    // vsync; timerWaitForMinUpdateRate tira fields = D_006F8DBC - D_006F8DC0 e updateRate = 60 / fields. No PS2 o quadro
    // comeca logo apos um vblank, entao "a logica cruzou n vblanks" e "T1 >= n fields" sao a mesma coisa. Aqui o timer anda
    // pelo relogio virtual e o quadro comeca alguns ms (de host) depois do vblank: a logica cruza um vblank com T1 < 1 field,
    // o jogo conta 1 field num quadro de 2 vsyncs e anda mais devagar que o relogio (18% dos quadros, ~11% de lentidao
    // na luta). A correcao: na entrada de timerWaitForMinUpdateRate o contador passa a ser no minimo os vblanks desde o
    // inicio do quadro (marcado pelo timerGetCurTics no fim de timerWaitForMinUpdateRate / em timerSetFrameStart).
    constexpr uint32_t kTimerWaitForMinUpdateRate = wotm::fn::timerWaitForMinUpdateRate__FPUi;
    constexpr uint32_t kTimerGetCurTics = wotm::fn::timerGetCurTics__Fv;
    constexpr uint32_t kFrameStartRa[2] = {0x22369Cu, 0x223480u};   // retorno do jal em timerWaitForMinUpdateRate / timerSetFrameStart
    constexpr uint32_t kFieldCounter = 0x6F8DBCu, kFieldCounterAtStart = 0x6F8DC0u;
    PS2Runtime::RecompiledFunction g_origWaitMinRate = nullptr, g_origGetCurTics = nullptr;
    uint64_t g_frameStartVsync = 0;
    bool g_frameStartValid = false;
    std::chrono::steady_clock::time_point g_frameStartHost;   // [field] sv=: inicio do quadro no relogio do host
    double g_svAtMs = -1.0;                                   // [field] sv=: 1a chamada a sceGsSyncV (ms desde o inicio do quadro)
    int g_svVblanks = -1;                                     // vblanks entregues entre o inicio do quadro e essa chamada

    void getCurTicsMarked(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t ra = GPR_U32(ctx, 31);
        g_origGetCurTics(rdram, ctx, runtime);
        if (ra == kFrameStartRa[0] || ra == kFrameStartRa[1])
        {
            g_frameStartVsync = runtime->eeScheduler().currentVSyncTick();
            g_frameStartValid = true;
        }
    }

    // Relogio nativo do timer (FRONTEIRA.md, passo 4; WOTM_TIMER_EMU=1 volta ao timer 1 emulado). O jogo mede o tempo pelo timer 1
    // (timerInit: modo 0x82, BUSCLK/256 = 576 kHz; 9619 ticks por field, timerGetFrameTime divide por 576 para dar ms):
    // timerGetCurTics devolve D_006F8DC4 + T1, e timer1Handler soma ceil(T1 / 9619) fields (ate 16) ao contador D_006F8DBC, sendo T1
    // o tempo desde o ultimo zero (fim do timerWaitForMinUpdateRate, timerSetFrameStart ou o timer1Handler anterior). No runtime o
    // T1 anda pelo relogio virtual do EE, que so acompanha o de parede por remendos do escalonador. Aqui os dois passam a ler o
    // relogio do host: timerGetCurTics e trocado inteiro, e o timer1Handler continua rodando (espera o vsync), mas os fields que ele
    // soma saem do tempo do host. O resto do timer (timerSetFrameStart, timerEndOfRealTimeProcessing, timerGetFrameTime...) so ve o T1
    // pelo timerGetCurTics e fica no host sem mudanca. O vsync (sceGsSyncV) continua vindo do escalonador do runtime.
    constexpr uint32_t kTimer1Handler = wotm::fn::timer1Handler__FPUi;
    constexpr uint32_t kFieldsForAverage = 0x6F8DE4u;   // D_006F8DE4: fields desde o timerSetFrameStart (timerGetAverageFrameTime)
    constexpr int32_t kTicsPerField = 9619;
    constexpr uint32_t kT1Count = 0x10000800u;   // T1_COUNT: o timer1Handler o le e zera
    PS2Runtime::RecompiledFunction g_origTimer1 = nullptr;
    bool g_nativeTimer = true;
    const auto g_hostT0 = std::chrono::steady_clock::now();
    uint32_t g_t1Zero = 0;   // ticks do host no ultimo zero do T1

    uint32_t hostTics()
    {
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - g_hostT0).count();
        return static_cast<uint32_t>(static_cast<uint64_t>(us) * 576u / 1000u);   // da a volta em ~2 h, como o contador do jogo
    }

    void getCurTicsNative(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t ra = GPR_U32(ctx, 31), now = hostTics();
        SET_GPR_U32(ctx, 2, now);
        ctx->pc = ra;
        if (ra == kFrameStartRa[0] || ra == kFrameStartRa[1])   // o jogo zera o T1 ali: comeco do quadro
        {
            g_t1Zero = now;
            g_frameStartVsync = runtime->eeScheduler().currentVSyncTick();
            g_frameStartValid = true;
            g_frameStartHost = std::chrono::steady_clock::now();
        }
    }

    // A contagem e feita na entrada: o timer1Handler quase sempre bloqueia no sceGsSyncV, e a retomada entra direto no meio da funcao
    // recompilada, sem passar por este envoltorio (um "depois" aqui nao roda). O T1 emulado e zerado para a original somar 0 fields
    // (ela rele D_006F8DBC/D_006F8DE4 depois do vsync e grava de volta o que somamos).
    void timer1HandlerNative(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kTimer1Handler)
        {
            const uint32_t now = hostTics();
            const int32_t elapsed = static_cast<int32_t>(now - g_t1Zero);
            g_t1Zero = now;
            const uint32_t add = elapsed <= 0 ? 0u : static_cast<uint32_t>(std::min((elapsed + kTicsPerField - 1) / kTicsPerField, 16));
            WRITE32(kFieldCounter, READ32(kFieldCounter) + add);
            WRITE32(kFieldsForAverage, READ32(kFieldsForAverage) + add);
            runtime->memory().write32(kT1Count, 0u);
        }
        g_origTimer1(rdram, ctx, runtime);
    }


    // Partida direta, WOTM_MATCH="<monstro do jogador>[,<IA>...ate 7],<fase>" (indices de MonsterLongNames e GameLevelNames;
    // "3,1,1" = lizard contra monkey em central; "3,1,2,5,1" = lizard contra monkey, robot e mantis): userintMain (0x1D9868) nao abre
    // o menu e devolve 0 (comecar a sessao) com o shell como o menu o deixa em "1 jogador contra a CPU" (medido com dumps no fim do
    // menu do roteiro -Auto): desafio (m_mode 2), 1 jogador + 0 a 7 IAs (m_monsterSel[4 + i], m_costume[2 + i]), 3 vidas. Cada monstro
    // aceita o figurino como "<monstro>:<0-3>" ("3:2,1:1,1"); sem ":" ou com um figurino ja usado pelo mesmo monstro, fica com o menor
    // livre (o menu nao deixa dois iguais no mesmo figurino). A diferenca e m_killTarget 0 (o menu poe 1): sem
    // meta de vitorias, EvaluateOnePlayerChallengeStatus so reergue o monstro derrubado e a luta segue, sem o dialogo "PLAY AGAIN"
    // que esperaria o pad. O filme de abertura (uiMain, 0x1D93D8) vira no-op. Cada fim de rodada (entrada de
    // Shell::EvaluateGameStatus, 0x1A7960) sai no log como [partida] com a vida e as vitorias dos dois monstros.
    constexpr uint32_t kUserintMain = wotm::fn::userintMain__Fv, kUiMain = wotm::fn::uiMain__Fi, kEvaluateGameStatus = wotm::fn::EvaluateGameStatus__5Shelli;
    PS2Runtime::RecompiledFunction g_origEvaluate = nullptr;
    unsigned g_rounds = 0;
    constexpr uint32_t kShell = wotm::sym::shellInfo, kGame = wotm::sym::shellGame;   // shellInfo, shellGame
    // Ate 7 IAs (8 monstros): m_monsterSel[4 + i] passa do [8] do header e cai em pad2958 (livre; o Endurance do retail ja grava
    // m_monsterSel[8] com 5 IAs), m_costume[2 + i] vai ate o indice 8 (antes de m_mode em 0x2C10), m_monsters[4 + i] ate 10 (cabem 16),
    // e fase + 8 monstros ocupam 9 dos 13 arquivos do fileStatus. O teto pratico e a RAM: veja wotmCheckLoadEnd.
    constexpr int kMatchMaxAIs = 7;
    int g_matchMonsters[kMatchMaxAIs + 1] = {-1, -1, -1, -1, -1, -1, -1, -1};   // jogador, depois as IAs
    int g_matchCostumes[kMatchMaxAIs + 1] = {-1, -1, -1, -1, -1, -1, -1, -1};   // -1: o menor figurino livre
    int g_matchAIs = 0, g_matchLevel = -1;
    // WOTM_PLAYERS=2: os dois primeiros monstros da WOTM_MATCH sao os jogadores 1 e 2 (m_monsterSel[1], m_costume[1], m_numPlayers 2:
    // tela dividida, como o free for all de 2 jogadores do menu); com WOTM_AIVSAI=1 os dois ficam com a IA. WOTM_PLAYERS=3 ou 4: os
    // tres ou quatro primeiros, com a tela em quadrantes (wotm_split4.inc).
    int g_matchPlayers = 1;
    int g_matchMode = 0, g_matchContinues = 2;   // WOTM_MODE=<modo>[:<continues>] (6 eliminacao, 7 dodgeball, 8 big shot, 9 crush)
    unsigned g_matchCount = 0;
    // WOTM_AIVSAI: "1" = todos os jogadores sob a IA; lista "2,3,4" = so esses (bots nos slots dos jogadores, o resto humano).
    // Os bots da escolha de controles (wotm_menucpus::g_botMask, wotm_padsel.inc) entram na mesma mascara enquanto valem.
    bool g_aiVsAi = false;       // WOTM_AIVSAI no ambiente
    unsigned g_aiVsAiMask = 0;   // bit i: jogador i + 1 sob a IA (ambiente)
    unsigned aiVsAiMask() { return g_aiVsAiMask | wotm_menucpus::g_botMask.load(std::memory_order_relaxed); }
    bool aiVsAiSlot(int slot) { return slot >= 0 && slot < 4 && (aiVsAiMask() >> slot & 1u); }
    bool aiVsAiAll() { return aiVsAiMask() == 0xFu; }

    // WOTM_LIVES=1..9 (teste: sair sem vidas logo); 0 = sem a variavel (vidas do jogo)
    uint32_t livesOverride()
    {
        static const uint32_t s_lives = [] {
            const char *e = std::getenv("WOTM_LIVES");
            const int n = e ? std::atoi(e) : 0;
            return uint32_t(n >= 1 && n <= 9 ? n : 0);
        }();
        return s_lives;
    }

    // Shell::InitPlayerLives (folha: m_lives[0..3] = NUM_LIVES[m_mode], no comeco do jogo) com o WOTM_LIVES: grava N
    void initPlayerLivesFixed(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)runtime;
        for (uint32_t i = 0; i < 4; ++i)
            WRITE32(kShell + 0x2BBC + 4 * i, livesOverride());
        ctx->pc = GPR_U32(ctx, 31);
    }

    void userintMainDirect(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t storyLevel = READ32(kShell + 0x2BB0);   // fase que a historia deixou (aventura, WOTM_MODE=1)
        for (uint32_t i = 0; i < 16; ++i)
            WRITE32(kShell + 0x2938 + 4 * i, 0);                         // m_monsterSel (4 jogadores + ate 12 IAs, ate 0x2978)
        for (uint32_t i = 0; i < 12; ++i)
            WRITE32(kShell + 0x2BE0 + 4 * i, 0);                         // m_costume (ate 0x2C10, onde mora m_mode)
        // figurinos: primeiro os pedidos (o primeiro a pedir fica com ele), depois os automaticos no menor livre do mesmo monstro
        const int total = g_matchPlayers + g_matchAIs;
        int costumes[kMatchMaxAIs + 1];
        auto taken = [&](int i, int c) {
            for (int j = 0; j < total; ++j)
                if (j != i && costumes[j] == c && g_matchMonsters[j] == g_matchMonsters[i])
                    return true;
            return false;
        };
        for (int i = 0; i < total; ++i)
            costumes[i] = -1;
        for (int i = 0; i < total; ++i)
            if (g_matchCostumes[i] >= 0 && !taken(i, g_matchCostumes[i]))
                costumes[i] = g_matchCostumes[i];   // 4 ou mais: traje de mod (MON/<PREFIXO><n>.* servido por mods/)
        for (int i = 0; i < total; ++i)
            for (int c = g_matchMonsters[i] == 13 ? 1 : 0; costumes[i] < 0; ++c)   // Goliath: o traje 0 e um prototipo (wotm_custom.inc)
                if (c > 3 || !taken(i, c))
                    costumes[i] = c & 3;   // mais de 4 iguais: repete (o menu nao chega nisso)
        for (int i = 0; i < total; ++i)
        {
            const uint32_t costume = static_cast<uint32_t>(costumes[i]);
            const bool player = i < g_matchPlayers;
            const uint32_t ai = static_cast<uint32_t>(i - g_matchPlayers);
            const uint32_t slot = player ? uint32_t(i) : 4u + ai, costumeSlot = player ? uint32_t(i) : 2u + ai;
            WRITE32(kShell + 0x2938 + 4 * slot, static_cast<uint32_t>(g_matchMonsters[i] << 5));   // (monstro << 5) | variante
            if (player && i >= 2)   // jogadores 3 e 4: m_costume[2..] e das IAs (wotm_split4.inc os carrega pelo caminho delas)
                wotm_split4::g_extraCostume[i - 2] = costume;
            else
                WRITE32(kShell + 0x2BE0 + 4 * costumeSlot, costume);
        }
        WRITE32(kShell + 0x2A44, 0);                                      // m_killTarget
        WRITE32(kShell + 0x2BB0, static_cast<uint32_t>(g_matchLevel));   // m_levelNum
        WRITE32(kShell + 0x2BB4, static_cast<uint32_t>(g_matchPlayers));  // m_numPlayers
        WRITE32(kShell + 0x2BB8, static_cast<uint32_t>(g_matchAIs));     // m_numAIs
        for (uint32_t i = 0; i < 4; ++i)
            if (g_matchMode != 1 || !g_matchCount)
                WRITE32(kShell + 0x2BBC + 4 * i, livesOverride() ? livesOverride() : 3u);   // m_lives (na aventura, so na primeira partida)
        // m_mode: free for all de 1 jogador (2) ou batalha multijogador (3: o TheGame::gameReestablishViews cria as duas vistas da tela
        // dividida nos modos 3, 6 e 7; com o 2 a luta ficava numa vista so e as cenas de cinema, que alternam a divisao, a ligavam)
        // WOTM_MODE (com 2 ou mais jogadores): eliminacao (6, continues em m_elimination e nos dois m_continues, monstros do comeco
        // usados como a selecao do retail os deixa) ou um minigame (7 dodgeball, 8 big shot, 9 crush; a fase e a 25, 26 ou 27)
        const uint32_t mode = g_matchMode == 1 || (g_matchPlayers > 1 && g_matchMode) ? uint32_t(g_matchMode) : g_matchPlayers > 1 ? 3u : 2u;
        WRITE32(kShell + 0x2C10, mode);
        WRITE32(kShell + 0x2B54, mode);                                   // m_battleMode
        WRITE32(kGame + 0x1203C8, mode);                                  // TheGame::m_gameMode
        if (mode == 1u)
        {
            // Aventura (WOTM_MODE=1; com WOTM_PLAYERS, a campanha co-op do wotm_coop.inc): a fase da WOTM_MATCH na primeira partida; nas
            // seguintes, a que a historia deixou no m_levelNum (fase seguinte, etapa de chefe ou a mesma), com vidas e pontos dela. As
            // IAs sao as da fase (como a saida da selecao da campanha, adventureAis do wotm_padsel.inc); as da WOTM_MATCH nao valem.
            static int s_level = -1;
            const uint32_t level = s_level < 0 ? static_cast<uint32_t>(g_matchLevel) : storyLevel;
            if (s_level < 0)
            {
                WRITE32(kShell + 0x2B2C, 0);   // pontos acumulados
                WRITE32(kShell + 0x2C18, 0);   // veio de um save
            }
            s_level = static_cast<int>(level);
            WRITE32(kShell + 0x2BB0, level);
            for (uint32_t i = 4; i < 16; ++i)
                WRITE32(kShell + 0x2938 + 4 * i, 0);
            const uint32_t n = std::min(level < 17u ? READ32(kShell + 0x29E0 + 4 * level) : 0u, 4u);
            for (uint32_t i = 0; i < n; ++i)
            {
                WRITE32(kShell + 0x2938 + 4 * (4 + i), READ32(wotm::sym::levelMonsters + 48 * level + 4 * i));
                WRITE32(kShell + 0x2BE0 + 4 * (2 + i), READ32(wotm::sym::levelMonsterModels + 16 * level + 4 * i));
            }
            WRITE32(kShell + 0x2BB8, n);
            WRITE32(wotm::sym::continueDecoding, 0);
            g_matchAIs = static_cast<int>(n);
            for (int i = 0; i < static_cast<int>(n); ++i)
            {
                const uint32_t sel = READ32(kShell + 0x2938 + 4 * (4 + i));
                g_matchMonsters[g_matchPlayers + i] = static_cast<int>(sel >> 5);
                costumes[g_matchPlayers + i] = static_cast<int>(READ32(kShell + 0x2BE0 + 4 * (2 + i)));
            }
            g_matchLevel = static_cast<int>(level);
        }
        else if (mode == 6u)
        {
            WRITE32(kShell + 0x2B48, static_cast<uint32_t>(g_matchContinues));   // m_elimination
            WRITE32(kShell + 0x2B40, static_cast<uint32_t>(g_matchContinues));   // m_continues[0..1]
            WRITE32(kShell + 0x2B44, static_cast<uint32_t>(g_matchContinues));
            WRITE32(kShell + 0x2B4C, 0);                                          // m_wins[0..1]
            WRITE32(kShell + 0x2B50, 0);
            for (uint32_t c = 0; c < 10; ++c)   // sem o menu: os 10 monstros da eliminacao livres (SelectSlot traje 0: taken 1, state 1)
            {
                WRITE32(wotm::sym::monsterSelectMode + 0xB0u * c, 1);
                WRITE32(wotm::sym::monsterSelectMode + 0xB0u * c + 4, 1);
            }
            for (int i = 0; i < g_matchPlayers; ++i)
            {
                const uint32_t sel = static_cast<uint32_t>(g_matchMonsters[i] << 5);
                if (i < 2)
                    WRITE32(kShell + 0x2B84 + 4 * i, sel);
                wotmElimSetStart(i, sel);
                WRITE32(wotm::sym::monsterSelectMode + 0xB0u * static_cast<uint32_t>(wotm_modes4::monsterSlot(sel)), 0);   // taken: usado
                if (i < 2)
                    WRITE32(kShell + 0x2BE0 + 4 * i, 0);   // traje 0, como o retail na eliminacao
                else
                    wotm_split4::g_extraCostume[i - 2] = 0;
            }
        }
        std::fprintf(stderr, "[partida] %u: monstro %d:%d (jogador%s)", ++g_matchCount, g_matchMonsters[0], costumes[0], aiVsAiSlot(0) ? " sob IA" : "");
        for (int i = 1; i < g_matchPlayers; ++i)
            std::fprintf(stderr, " e %d:%d (jogador %d%s)", g_matchMonsters[i], costumes[i], i + 1, aiVsAiSlot(i) ? " sob IA" : "");
        std::fprintf(stderr, " %s", g_matchAIs ? "contra" : (g_matchPlayers > 1 ? "sem IA" : "sozinho"));
        for (int i = g_matchPlayers; i < g_matchPlayers + g_matchAIs; ++i)
            std::fprintf(stderr, " %d:%d", g_matchMonsters[i], costumes[i]);
        std::fprintf(stderr, "%s, fase %d\n", g_matchAIs ? " (IA)" : "", g_matchLevel);
        SET_GPR_S32(ctx, 2, 0);
        ctx->pc = GPR_U32(ctx, 31);
    }

    void uiMainSkipped(uint8_t *, R5900Context *ctx, PS2Runtime *) { ctx->pc = GPR_U32(ctx, 31); }

    // Aventura direta (WOTM_MODE=1): sem o "salvar?" da vitoria (displayDialog 2). O EvaluateOnePlayerStoryStatus ja passou a
    // historia para a fase seguinte antes dele; pular equivale a "nao salvar" e a partida seguinte segue sozinha.
    PS2Runtime::RecompiledFunction g_origDialogAdventure = nullptr;
    void dialogAdventure(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == wotm::fn::displayDialog__Fi && GPR_U32(ctx, 4) == 2u)
        {
            std::fprintf(stderr, "[partida] aventura: sem o dialogo de salvar, a seguir a fase %u\n", READ32(kShell + 0x2BB0));
            WRITE32(kShell + 0x2BCC, 0);   // m_inSession 0 e m_inMenus 1, como o wonLevelOverlay os deixa: o main carrega a fase seguinte
            WRITE32(kShell + 0x2BD0, 1);
            ctx->pc = GPR_U32(ctx, 31);
            return;
        }
        // WOTM_COOPRETRY=1 (teste do retry sem teclado): no fim de jogo (dialogo 1) um X sintetico aos 4 s e aos 8 s, pelo mesmo
        // pedido do clique do mouse nos menus (wotm_mouse.hpp, requestCross; a tela vale como tela do jogo com mouse por um instante)
        static const bool s_retry = [] { const char *e = std::getenv("WOTM_COOPRETRY"); return e && *e == '1'; }();
        if (s_retry && ctx->pc == wotm::fn::displayDialog__Fi && GPR_U32(ctx, 4) == 1u)
        {
            std::fprintf(stderr, "[partida] fim de jogo: X sintetico em 4 s (WOTM_COOPRETRY)\n");
            std::thread([] {
                for (int k = 0; k < 2; ++k)
                {
                    std::this_thread::sleep_for(std::chrono::seconds(4));
                    for (int i = 0; i < 6; ++i)
                    {
                        wotm::mouse::gameMenu();
                        if (i == 0)
                            wotm::mouse::requestCross(2);
                        std::this_thread::sleep_for(std::chrono::milliseconds(25));
                    }
                }
            }).detach();
        }
        g_origDialogAdventure(rdram, ctx, runtime);
    }

    void evaluateGameStatusLogged(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kEvaluateGameStatus)
        {
            std::fprintf(stderr, "[partida] rodada %u terminou (rtMain devolveu %d):", ++g_rounds, static_cast<int32_t>(GPR_U32(ctx, 5)));
            for (uint32_t slot = 0; slot < 4u + static_cast<uint32_t>(kMatchMaxAIs); ++slot)   // m_monsters[0..] (jogadores) e [4..] (IAs)
            {
                if (slot < 4u && slot >= static_cast<uint32_t>(std::max(g_matchPlayers, 1)))
                    continue;
                const uint32_t m = READ32(kGame + 0x120380 + 4 * slot);
                if (!m)
                    continue;
                const uint32_t hw = READ32(m + 0x44C);   // m_health
                float health;
                std::memcpy(&health, &hw, 4);
                std::fprintf(stderr, " monstro %u vida %.0f vitorias %d;", READ32(m + 0x14) >> 5, health, static_cast<int32_t>(READ32(m + 0x3C)));
            }
            std::fprintf(stderr, "\n");
        }
        g_origEvaluate(rdram, ctx, runtime);
    }

    // WOTM_AIVSAI=1: o jogador tambem e controlado pela IA do jogo. Monster::playerInit (0x165F10) salta para Monster::aiInit
    // (0x166038) com os mesmos registradores, como um `j`: m_playerNum fica 2 e Monster::update le as entradas de Ai::updateInputs.
    // Camera e HUD seguem no jogador (SetPlayerMonster). Retomada no meio cai direto no aiInit (o salto nao volta para ca).
    // TheGame::SetAIMonster (0x137B10) recalcula m_numAIs (0x1203E0) contando os slots com m_playerNum 2 (com o jogador, m_monsters[4 + i]
    // seria lido alem da ultima IA: bancos de som, pickups) e, na fase 2 da aventura, desliga todo Robo-47 com m_playerNum 2 (o da IA so
    // entra depois do Togera): o jogador de Robo-47 sumia. Durante ele os jogadores voltam a 1.
    constexpr uint32_t kPlayerInit = wotm::fn::playerInit__7Monster, kAiInit = wotm::fn::aiInit__7Monster, kSetAIMonster = wotm::fn::SetAIMonster__7TheGameiiii;
    PS2Runtime::RecompiledFunction g_aiInit = nullptr, g_origSetAIMonster = nullptr, g_origPlayerInit = nullptr;

    // Com a lista (WOTM_AIVSAI=2,3,4) so o slot marcado vai ao aiInit: o SetPlayerMonster grava o m_monsters[i] antes do playerInit,
    // e os renascimentos (wotm_coop.inc, wotm_split4.inc) chamam o playerInit com o monstro ja no slot
    // Sempre instalado (os bots do menu ligam na hora): sem jogador sob a IA, ou retomada no meio da original, e a original.
    void playerInitAsAi(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc != kPlayerInit || !aiVsAiMask())
        {
            g_origPlayerInit(rdram, ctx, runtime);
            return;
        }
        if (!aiVsAiAll())
        {
            const uint32_t m = GPR_U32(ctx, 4) & 0x1FFFFFFFu;
            int slot = -1;
            for (int i = 0; i < 4 && slot < 0; ++i)
                if (m && (READ32(kGame + 0x120380 + 4 * i) & 0x1FFFFFFFu) == m)   // m_monsters[i]
                    slot = i;
            if (!aiVsAiSlot(slot))
            {
                g_origPlayerInit(rdram, ctx, runtime);
                return;
            }
        }
        ctx->pc = kAiInit;
        g_aiInit(rdram, ctx, runtime);
    }

    void setAIMonsterCounted(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t flipped[4];
        int n = 0;
        if (ctx->pc == kSetAIMonster && aiVsAiMask())
        {
            const int32_t players = static_cast<int32_t>(READ32(kShell + 0x2BB4));
            for (int32_t i = 0; i < players && i < 4; ++i)
            {
                const uint32_t m = READ32(kGame + 0x120380 + 4 * i);   // m_monsters[i]
                if (m && READ32(m + 0x18) == 2u)                       // m_playerNum
                {
                    WRITE32(m + 0x18, 1u);
                    flipped[n++] = m;
                }
            }
        }
        g_origSetAIMonster(rdram, ctx, runtime);
        while (n > 0)
            WRITE32(flipped[--n] + 0x18, 2u);
    }

    // Fora do controle, o jogo ainda trata os slots de jogador como humanos onde o m_playerNum decide algo que nao e entrada:
    // TheGame::gameGetStartPoint (0x138D68; com m_playerNum 2 o jogador nascia num ponto da IA e a IA no do jogador) e
    // Cameras::Update (0x11C770; so chama Camera::checkCollision, que monta os pontos de visada dos predios "fantasma", para
    // camera de monstro com m_playerNum 1). Durante essas duas funcoes os jogadores voltam a 1. Nenhuma das duas espera.
    constexpr uint32_t kGameGetStartPoint = wotm::fn::gameGetStartPoint__7TheGameP7Monster, kCamerasUpdate = wotm::fn::Update__7Cameras;
    PS2Runtime::RecompiledFunction g_origStartPoint = nullptr, g_origCamerasUpdate = nullptr;

    void callAsHumans(PS2Runtime::RecompiledFunction orig, uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t flipped[4];
        int n = 0;
        const int32_t players = aiVsAiMask() ? static_cast<int32_t>(READ32(kShell + 0x2BB4)) : 0;   // sem jogador sob a IA: a original
        for (int32_t i = 0; i < players && i < 4; ++i)
        {
            const uint32_t m = READ32(kGame + 0x120380 + 4 * i);   // m_monsters[i]
            if (m && READ32(m + 0x18) == 2u)
            {
                WRITE32(m + 0x18, 1u);
                flipped[n++] = m;
            }
        }
        orig(rdram, ctx, runtime);
        while (n > 0)
            WRITE32(flipped[--n] + 0x18, 2u);
    }

    void startPointAsHumans(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime) { callAsHumans(g_origStartPoint, rdram, ctx, runtime); }
    void camerasUpdateAsHumans(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime) { callAsHumans(g_origCamerasUpdate, rdram, ctx, runtime); }

    // Free for all de 1 jogador com 0 a 7 IAs (mod, sempre ligado): screenFreeForAllOptions1P (tela 12) anda o cursor so por 1..3
    // IAs. O retail ja trata o item 0 na confirmacao (m_numAIs 0, pula o "first to" e vai direto a escolha do monstro, tela 3, que sem
    // IA segue para a fase; na luta EvaluateOnePlayerChallengeStatus so reergue o jogador), e o switch do numero (shell +0x43C) tem
    // 11 filhos, os digitos 0 a 10 do proprio jogo. Aqui o cursor da a volta por 0..7: depois da original, o passo que ela deu (que
    // so conhece 1..3) vira um passo de 1 em 0..7, e o switch recebe o mesmo valor (whichChild, como o hierSetSwitch faria). Com o
    // cursor em 4..7 a original roda com 3 (ela nao aceita mais e so loga um erro na confirmacao): a confirmacao grava 3 em m_numAIs
    // e vai ao "first to"; o numero pedido fica em wotm_menucpus::g_want, e o menu segue como com 3 CPUs ate a fase ser confirmada
    // (changeScreenHook do wotm_padsel.inc).
    constexpr uint32_t kFfaOptions1P = wotm::fn::screenFreeForAllOptions1P__Fv;
    constexpr uint32_t kFfaCursor = wotm::sym::currentSelection + 12 * 4;   // currentSelection[12]
    constexpr uint32_t kFfaNumAIs = kShell + 0x2BB8;                         // m_numAIs
    PS2Runtime::RecompiledFunction g_origFfaOptions1P = nullptr;
    uint32_t g_ffaRa = 0, g_ffaBefore = 0;
    bool g_ffaPending = false, g_ffaFake = false;

    void ffaOptions1PWithZero(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        constexpr int kMax = wotm_menucpus::kMax;
        if (ctx->pc == kFfaOptions1P)
        {
            g_ffaRa = GPR_U32(ctx, 31);
            g_ffaBefore = READ32(kFfaCursor);
            g_ffaPending = true;
            g_ffaFake = g_ffaBefore > 3u && g_ffaBefore <= uint32_t(kMax);
            if (g_ffaFake)
            {
                WRITE32(kFfaCursor, 3);
                WRITE32(kFfaNumAIs, 0);   // a confirmacao o poe em 3 (a tela nao le o m_numAIs; a volta o zera)
            }
        }
        g_origFfaOptions1P(rdram, ctx, runtime);
        if (!g_ffaPending || ctx->pc != g_ffaRa)
            return;   // parou no meio (retomada cai aqui de novo e termina o servico)
        g_ffaPending = false;
        const int before = int(g_ffaBefore), eff = g_ffaFake ? 3 : before, after = int(READ32(kFfaCursor));
        if (g_ffaFake && READ32(kFfaNumAIs) == 3u)
        {
            wotm_menucpus::g_want.store(before);
            std::fprintf(stderr, "[wotm] tela 12: %d CPUs (o menu segue com 3; a fase confirmada grava %d)\n", before, before);
        }
        else if (READ32(kFfaNumAIs) != 0u || after != eff)
            wotm_menucpus::g_want.store(0);   // confirmou 1..3 (ou mexeu no cursor)
        // passo da original (cima: >= 2 desce 1, senao vai a 3; baixo: < 3 sobe 1, senao vai a 1)
        const bool dec = (eff >= 2 && after == eff - 1) || (eff <= 1 && after == 3);
        const bool inc = (eff < 3 && after == eff + 1) || (eff >= 3 && after == 1);
        const int want = dec ? (before + kMax) % (kMax + 1) : inc ? (before + 1) % (kMax + 1) : before;
        if (want == after)
            return;
        WRITE32(kFfaCursor, uint32_t(want));
        const uint32_t sw = READ32(kShell + 0x43C);
        if (sw && (READ8(sw) & 0x3F) == 6 && READ8(sw + 0xB) > want)   // SWITCH_NODE, numKids
            WRITE8(sw + 8, uint8_t(want));                              // whichChild
    }

    // Free for all de 2 a 4 jogadores com 0 a 8 - N IAs (8 monstros): o mesmo da tela 12 na screenFreeForAllOptions2P (tela 13), que
    // anda o cursor por 0..2 (esquerda: > 0 desce 1, senao vai a 2; direita: < 2 sobe 1, senao vai a 0) e confirma 0..2 em m_numAIs,
    // indo ao "first to" (15). O switch do numero (shell +0x464) tem os mesmos 11 filhos. Com o cursor acima do que o jogo conhece
    // (2 com 2 jogadores; nenhuma com 3 e 4, que o retail nao tem: a selecao de 3 e 4 do wotm_padsel.inc poe as CPUs) a original roda
    // com esse maximo, e a confirmacao guarda o numero em wotm_menucpus::g_want.
    constexpr uint32_t kFfaOptions2P = wotm::fn::screenFreeForAllOptions2P__Fv;
    constexpr uint32_t kFfa2Cursor = wotm::sym::currentSelection + 13 * 4;   // currentSelection[13]
    PS2Runtime::RecompiledFunction g_origFfaOptions2P = nullptr;
    uint32_t g_ffa2Ra = 0, g_ffa2Before = 0;
    bool g_ffa2Pending = false, g_ffa2Fake = false;
    int g_ffa2Game = 2;

    void ffaOptions2PMore(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        constexpr int kGame = 2;   // o passo da original anda em 0..2
        const int players = std::clamp(wotm_menucpus::g_menuPlayers.load(), 2, 4), kMax = wotm_menucpus::maxFor(players);
        if (ctx->pc == kFfaOptions2P)
        {
            g_ffa2Ra = GPR_U32(ctx, 31);
            g_ffa2Before = std::min(READ32(kFfa2Cursor), uint32_t(kMax));   // o cursor que ficou de outro numero de jogadores
            WRITE32(kFfa2Cursor, g_ffa2Before);
            g_ffa2Pending = true;
            // o maior numero que o jogo confirma por si; com bot de jogador (2 com bot vao a selecao do telao, como 3 e 4) nenhum
            g_ffa2Game = players == 2 && !wotm_menucpus::g_botMask.load(std::memory_order_relaxed) ? kGame : 0;
            g_ffa2Fake = g_ffa2Before > uint32_t(g_ffa2Game) && g_ffa2Before <= uint32_t(kMax);
            if (g_ffa2Fake)
                WRITE32(kFfa2Cursor, uint32_t(g_ffa2Game));
        }
        g_origFfaOptions2P(rdram, ctx, runtime);
        if (!g_ffa2Pending || ctx->pc != g_ffa2Ra)
            return;
        g_ffa2Pending = false;
        const int before = int(g_ffa2Before), eff = g_ffa2Fake ? g_ffa2Game : before, after = int(READ32(kFfa2Cursor));
        const bool confirmed = READ32(wotm::sym::currScreen) == 15u;
        if (g_ffa2Fake && confirmed)
        {
            wotm_menucpus::g_want.store(before);
            std::fprintf(stderr, "[wotm] tela 13: %d jogadores e %d CPUs (o menu segue com %d)\n", players, before, g_ffa2Game);
        }
        else if (confirmed || after != eff)
            wotm_menucpus::g_want.store(0);
        const bool dec = (eff >= 1 && after == eff - 1) || (eff == 0 && after == kGame);
        const bool inc = (eff < kGame && after == eff + 1) || (eff == kGame && after == 0);
        const int want = dec ? (before + kMax) % (kMax + 1) : inc ? (before + 1) % (kMax + 1) : before;
        if (want == after)
            return;
        WRITE32(kFfa2Cursor, uint32_t(want));
        const uint32_t sw = READ32(kShell + 0x464);
        if (sw && (READ8(sw) & 0x3F) == 6 && READ8(sw + 0xB) > want)
            WRITE8(sw + 8, uint8_t(want));
    }

    // Trapacas de debug (menu F1 do wotm_live.inc, ou WOTM_CHEATS no inicio): aplicadas na thread do EE, na entrada de Monster::update
    // (0x15B488) de cada jogador (m_monsters[0 .. m_numPlayers-1]), antes da logica do quadro dele. A janela so escreve g_wotmCheats.
    //   invencivel: Monster::takeDamage (0x15FE40) volta na entrada quando a vitima e um jogador (nem o dano forcado passa).
    //   stamina:    1 = cheia (cur = max 100, sem exaustao); 2 = no nivel de furia (cur = maxLevel, 130).
    //   especial:   m_specialWeapon (+0xF3) = 1, a liberacao que o drainSpecial tira e o isSpecialAvailable le.
    //   cloak:      sem CLOAK (m_cloaked, +0xEF), chama Monster::setCloakOn (0x162CD0), como o power-up; acabou o tempo, liga de novo.
    //   deus:       m_godMode (+0xE9), o modo do proprio jogo: sem dano, vida, stamina e especial cheios a cada quadro.
    //   vencer:     acao de uma vez: takeDamage(100000, force, jogador) em cada IA viva (m_monsters[4 + i], i < m_numAIs).
    // WOTM_CLOAK=1 (teste do CLOAK na checagem VU1) e o mesmo que WOTM_CHEATS=cloak.
    constexpr uint32_t kMonsterUpdate = wotm::fn::update__7Monster, kSetCloakOn = wotm::fn::setCloakOn__7Monster,
                       kTakeDamage = wotm::fn::takeDamage__7MonsterfbP7Monster;
    PS2Runtime::RecompiledFunction g_origMonsterUpdate = nullptr, g_setCloakOn = nullptr, g_origTakeDamage = nullptr;
    uint32_t g_godApplied = 0;
    int g_autoWinSecs = 0;   // WOTM_CHEATS=vencer=N   // bits dos slots de jogador em que o m_godMode foi ligado por aqui (para desligar so esses)

    bool isPlayerMonster(uint8_t *rdram, uint32_t m, int *slot = nullptr)
    {
        int32_t players = static_cast<int32_t>(FAST_READ32(kShell + 0x2BB4));   // m_numPlayers
        if (players < 1 || players > 4)
            players = 1;
        for (int32_t i = 0; i < players; ++i)
            if (m && FAST_READ32(kGame + 0x120380 + 4 * i) == m)
            {
                if (slot)
                    *slot = i;
                return true;
            }
        return false;
    }

    void callTakeDamage(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t victim, float damage, uint32_t source)
    {
        const R5900Context saved = *ctx;
        ctx->r[4] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(victim)));
        ctx->r[5] = _mm_set_epi64x(0, 1);   // force
        ctx->r[6] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(source)));
        ctx->r[31] = _mm_set_epi64x(0, 0);
        ctx->f[12] = damage;
        ctx->pc = kTakeDamage;
        g_origTakeDamage(rdram, ctx, runtime);
        wotmFinishGuestCall(rdram, ctx, runtime);
        *ctx = saved;
    }

    // Derruba o jogador i (m_monsters[i], 0..3) vivo com um takeDamage do tamanho da vida; a fonte e a primeira IA viva (ou ele mesmo).
    // Devolve 1 se derrubou.
    int killPlayer(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, int i)
    {
        const int32_t players = static_cast<int32_t>(READ32(kShell + 0x2BB4));   // m_numPlayers
        const uint32_t p = i >= 0 && i < 4 && i < std::max(players, 1) ? READ32(kGame + 0x120380 + 4 * i) & 0x1FFFFFFFu : 0u;
        float hp;
        const uint32_t hw = p ? READ32(p + 0x44C) : 0u;
        std::memcpy(&hp, &hw, 4);
        if (!p || READ8(p + 0xE8) || !(hp > 0.f))   // m_dead
            return 0;
        uint32_t src = p;
        const int32_t ais = static_cast<int32_t>(READ32(kGame + 0x1203E0));   // m_numAIs
        for (int32_t k = 0; k < ais && k < 12 && src == p; ++k)
            if (const uint32_t ai = READ32(kGame + 0x120390 + 4 * k) & 0x1FFFFFFFu; ai && !READ8(ai + 0xE8))
                src = ai;
        if (g_origTakeDamage)
            callTakeDamage(rdram, ctx, runtime, p, hp + 1000.f, src);
        return 1;
    }

    void takeDamageCheat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kTakeDamage && (g_wotmCheats.load(std::memory_order_relaxed) & kCheatInvincible) && isPlayerMonster(rdram, GPR_U32(ctx, 4)))
        {
            static uint32_t blocked = 0;
            if (++blocked <= 3 || blocked % 100 == 0)
                std::fprintf(stderr, "[debug] invencivel: dano bloqueado no jogador (%u)\n", blocked);
            ctx->pc = GPR_U32(ctx, 31);
            return;
        }
        g_origTakeDamage(rdram, ctx, runtime);
    }

    // Modo deus sem reacao de golpe: no jogador com kCheatGod, Monster::takeHit (0x15F048, a entrada virtual de todo golpe),
    // knockBack (0x1601C8) e takeAdditiveRecoil (0x160058) voltam na entrada, e o enterNewState (0x1621C8) recusa os estados de
    // reacao, como o proprio jogo faz no modo 7 (empurroes de explosao e colisao chamam knockBack ou entram no estado por fora).
    constexpr uint32_t kTakeHit = wotm::fn::takeHit__7MonsterP8_fvectorfi, kKnockBack = wotm::fn::knockBack__7MonsterR8_fvectorff,
                       kAdditiveRecoil = wotm::fn::takeAdditiveRecoil__7MonsterR8_fvectorf, kEnterNewState = wotm::fn::enterNewState__7MonsterP12MonsterState;
    PS2Runtime::RecompiledFunction g_origTakeHit = nullptr, g_origKnockBack = nullptr, g_origAdditiveRecoil = nullptr, g_origEnterNewState = nullptr;
    // offsets dos estados de reacao dentro do Monster (game.h): Recoil, KnockBack, Grappled, BeingSlammed, BeingThrown, Impaled,
    // Stunned, Shocked, BigTakeHit, Countered
    constexpr uint32_t kHitStates[] = {0x7E30, 0x7EA0, 0xDDCC, 0xDE30, 0xE3E0, 0x106E0, 0x10714, 0x10BA0, 0x11100, 0x11114};

    bool godPlayer(uint8_t *rdram, uint32_t m)
    {
        return (g_wotmCheats.load(std::memory_order_relaxed) & kCheatGod) && isPlayerMonster(rdram, m);
    }

    void godBlocked(const char *what)
    {
        static uint32_t n = 0;
        if (++n <= 3 || n % 100 == 0)
            std::fprintf(stderr, "[debug] deus: %s recusado no jogador (%u)\n", what, n);
    }

    void takeHitCheat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kTakeHit && godPlayer(rdram, GPR_U32(ctx, 4)))
        {
            godBlocked("takeHit");
            ctx->pc = GPR_U32(ctx, 31);
            return;
        }
        g_origTakeHit(rdram, ctx, runtime);
    }

    void knockBackCheat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kKnockBack && godPlayer(rdram, GPR_U32(ctx, 4)))
        {
            godBlocked("knockBack");
            ctx->pc = GPR_U32(ctx, 31);
            return;
        }
        g_origKnockBack(rdram, ctx, runtime);
    }

    void additiveRecoilCheat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kAdditiveRecoil && godPlayer(rdram, GPR_U32(ctx, 4)))
        {
            ctx->pc = GPR_U32(ctx, 31);
            return;
        }
        g_origAdditiveRecoil(rdram, ctx, runtime);
    }

    void enterNewStateCheat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t m = GPR_U32(ctx, 4);
        if (ctx->pc == kEnterNewState && godPlayer(rdram, m))
        {
            const uint32_t off = GPR_U32(ctx, 5) - m;
            for (uint32_t s : kHitStates)
                if (off == s)
                {
                    char what[48];
                    std::snprintf(what, sizeof what, "estado +0x%X", s);
                    godBlocked(what);
                    ctx->pc = GPR_U32(ctx, 31);
                    return;
                }
        }
        g_origEnterNewState(rdram, ctx, runtime);
    }

    void monsterUpdateCheats(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t m = GPR_U32(ctx, 4);
        int slot = 0;
        if (ctx->pc == kMonsterUpdate && isPlayerMonster(rdram, m, &slot))
        {
            const uint32_t c = g_wotmCheats.load(std::memory_order_relaxed);
            const uint32_t st = (c & kCheatStaminaMask) / kCheatStamina1;
            if (st == 1u)
            {
                WRITE32(m + 0x470, READ32(m + 0x460));   // StaminaMeter (+0x460): cur = max
                WRITE32(m + 0x474, 0);                    // recoverPool
                WRITE32(m + 0x478, 0);                    // exhaustTimer
                WRITE32(m + 0x480, 0);                    // exhausted
            }
            else if (st == 2u)
            {
                WRITE32(m + 0x470, READ32(m + 0x468));   // cur = maxLevel (furia)
                WRITE32(m + 0x474, 0);
                WRITE32(m + 0x478, 0);
                WRITE32(m + 0x480, 0);
            }
            if (c & kCheatSpecial)
                WRITE8(m + 0xF3, 1);   // m_specialWeapon
            const uint32_t bit = 1u << slot;
            if (c & kCheatGod)
            {
                WRITE8(m + 0xE9, 1);   // m_godMode
                g_godApplied |= bit;
            }
            else if (g_godApplied & bit)
            {
                WRITE8(m + 0xE9, 0);
                g_godApplied &= ~bit;
            }
            if ((c & kCheatCloak) && g_setCloakOn && !READ8(m + 0xEF))   // m_cloaked
            {
                const R5900Context saved = *ctx;
                ctx->pc = kSetCloakOn;
                g_setCloakOn(rdram, ctx, runtime);
                *ctx = saved;
            }
            if (slot == 0 && g_autoWinSecs > 0)
            {   // WOTM_CHEATS=vencer=N: a cada N segundos de jogo (60 quadros do g_frame por segundo), o mesmo pedido da tecla 6
                static uint32_t last = 0;
                const uint32_t f = READ32(wotm::sym::g_frame);
                if (last == 0 || f < last)
                    last = f;
                else if (f - last >= static_cast<uint32_t>(g_autoWinSecs) * 60u)
                {
                    last = f;
                    g_wotmCheatWin = true;
                }
            }
            if (slot == 0 && g_wotmCheatWin.exchange(false))
            {
                const int32_t ais = static_cast<int32_t>(READ32(kGame + 0x1203E0));   // m_numAIs
                int killed = 0;
                uint32_t by = m;   // o golpe vem do primeiro jogador vivo: na campanha co-op o P1 pode ter saido da fase
                for (int32_t p = 0, np = std::min<int32_t>(static_cast<int32_t>(READ32(kShell + 0x2BB4)), 4); p < np; ++p)
                    if (const uint32_t pm = READ32(kGame + 0x120380 + 4 * p); pm && !READ8(pm + 0xE8))
                    {
                        by = pm;
                        break;
                    }
                for (int32_t i = 0; i < ais && i < 4; ++i)
                {
                    const uint32_t ai = READ32(kGame + 0x120390 + 4 * i);   // m_monsters[4 + i]
                    if (ai && !READ8(ai + 0xE8))                           // m_dead
                    {
                        callTakeDamage(rdram, ctx, runtime, ai, 100000.0f, by);
                        ++killed;
                    }
                }
                std::fprintf(stderr, "[debug] vencer a luta: %d IA(s) derrubada(s) de %d\n", killed, ais);
            }
            if (const uint32_t mask = g_wotmCheatKill.exchange(0))
            {   // matar (tecla 8 do menu F1, WOTM_COOPKILL): o primeiro jogador atualizado no quadro aplica em todos os da mascara
                int done = 0;
                for (int32_t i = 0; i < 4; ++i)
                    if (mask & (1u << i))
                        done += killPlayer(rdram, ctx, runtime, i);
                std::fprintf(stderr, "[debug] matar: mascara 0x%x, %d jogador(es) derrubado(s)\n", mask, done);
            }
        }
        g_origMonsterUpdate(rdram, ctx, runtime);
    }

    // WOTM_TURNLOG: uma linha [turn] por MonsterDynamics::updatePosition (cada monstro, cada quadro do jogo):
    // quadro (g_frame), monstro, rate, acelRot (+0x110, zerada pela funcao), velRot (+0x100) antes/depois e os angulos
    // (Monster+0xC -> +0x60, em radianos) depois da integracao. O rate mostrado e o da variavel (sem o limite).
    constexpr uint32_t kMonsterDynamicsUpdatePosition = wotm::fn::updatePosition__15MonsterDynamics;
    PS2Runtime::RecompiledFunction g_origUpdatePosition = nullptr;

    void updatePositionLogged(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        auto readF = [&](uint8_t *, uint32_t addr)
        {
            const uint32_t w = READ32(addr);
            float f;
            std::memcpy(&f, &w, 4);
            return f;
        };
        const uint32_t dyn = GPR_U32(ctx, 4);
        float acc[3], v0[3];
        for (int i = 0; i < 3; ++i)
        {
            acc[i] = readF(rdram, dyn + 0x110 + 4 * i);
            v0[i] = readF(rdram, dyn + 0x100 + 4 * i);
        }
        const int32_t rate = static_cast<int32_t>(READ32(kUpdateRateVar));
        g_origUpdatePosition(rdram, ctx, runtime);
        const uint32_t mon = READ32(dyn);
        const uint32_t cs = READ32(mon + 0xC);
        std::fprintf(stderr, "[turn] g=%u mon=%06x rate=%d acc=%+.3f,%+.3f,%+.3f vel=%+.3f,%+.3f,%+.3f -> %+.3f,%+.3f,%+.3f ang=%+.3f,%+.3f,%+.3f\n",
                     READ32(wotm::sym::g_frame), mon, rate, acc[0], acc[1], acc[2], v0[0], v0[1], v0[2], readF(rdram, dyn + 0x100), readF(rdram, dyn + 0x104),
                     readF(rdram, dyn + 0x108), readF(rdram, cs + 0x60), readF(rdram, cs + 0x64), readF(rdram, cs + 0x68));
    }

    // WOTM_FIELDLOG: uma linha [field] por quadro do jogo, detectado quando timerWaitForMinUpdateRate atualiza
    // D_006F8DC0 (contagem de fields no inicio do quadro). Mostra os fields que o jogo mediu pelo timer 1 (D_006F8DB4),
    // os ciclos virtuais do EE gastos (em fields: total, saltados no ocio e os 3 pcs de checkpoint que mais gastaram) e o
    // tempo real (total, ocioso esperando evento e esperando o relogio de parede para soltar um prazo).
    bool g_fieldLog = false;
    double g_hierMs[4] = {}, g_captureMs[4] = {};   // por vista: hier (os dois passos) e captura nativa; ver hierTapped
    unsigned g_hierViews = 0;                        // bits: vistas com hier(v, 0) no quadro
    // Chamadas a sceGsSyncV no quadro (cada uma espera 1 vsync) e o $ra das 4 primeiras: mostra quem gasta os fields.
    constexpr uint32_t kSceGsSyncV = wotm::fn::sceGsSyncV;
    PS2Runtime::RecompiledFunction g_origSyncV = nullptr;
    uint32_t g_syncVCalls = 0, g_syncVRa[4];
    // Laco do displayDialog (menu de pause, areYouSure, fim de luta do multiplayer...): monta o quadro sozinho (hier, particleDraw,
    // Hud::print, a funcao do dialogo, fontDmaFontData) e espera o vsync no sceGsSyncV do fim do laco, sem passar pelo
    // timerWaitForMinUpdateRate. Sem captura ali, a tela repetia o ultimo quadro de antes do dialogo, e a moldura do pause (o _cs
    // shell->m_cs610, que o pauseLevelOverlay liga) nunca aparecia. $ra desse jal: displayDialog + 0x7C4 (jal em 0x1ACB9C).
    // Na ultima volta ($s1 != 0, o beqz logo depois volta ao laco com 0) o Shell::FadeScreen acabou de escurecer a tela: capturar
    // ali desligaria o fade (wotmCaptureFrame) e mostraria a cena clara ate o proximo quadro. Fica sem captura, como antes.
    constexpr uint32_t kDialogLoopSyncVRa = wotm::fn::displayDialog__Fi + 0x7C4u;
    // Os dois lacos do Shell::FadeScreen (passo com hier(0, 0) quando b = true; jal sceGsSyncV em +0x2A4 e +0x40C): wotmCaptureFadeStep
    // publica a cena do passo, se houve hier, sem desligar o fade.
    constexpr uint32_t kFadeStepSyncVRa[2] = {wotm::fn::FadeScreen__5ShellibRUiT3UcUcUcUcUcUc + 0x2ACu,
                                              wotm::fn::FadeScreen__5ShellibRUiT3UcUcUcUcUcUc + 0x414u};
    void syncVTapped(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static uint32_t fadeRa = 0;
        if (ctx->pc == kSceGsSyncV)
        {
            wotm_save::poll(rdram);   // tokens e destraves que mudaram fora de uma gravacao vao para o save.json
            const uint32_t ra = GPR_U32(ctx, 31);
            if (g_fieldLog)
            {
                if (g_syncVCalls < 4)
                    g_syncVRa[g_syncVCalls] = ra;
                if (g_syncVCalls == 0 && g_frameStartValid)
                {
                    const auto t = std::chrono::steady_clock::now();
                    g_svAtMs = std::chrono::duration<double, std::milli>(t - g_frameStartHost).count();
                    g_svVblanks = static_cast<int>(runtime->eeScheduler().currentVSyncTick() - g_frameStartVsync);
                }
                ++g_syncVCalls;
            }
            if (ra == kDialogLoopSyncVRa && GPR_U32(ctx, 17) == 0u)
                wotmCaptureFrame(rdram);
            else if (ra == kFadeStepSyncVRa[0] || ra == kFadeStepSyncVRa[1])
            {
                wotmCaptureFadeStep(rdram);
                wotmShellFadeStep();
                fadeRa = ra;
            }
        }
        g_origSyncV(rdram, ctx, runtime);
        if (fadeRa && ctx->pc == fadeRa)
        {
            fadeRa = 0;
            wotmShellFadeStepDone();
        }
    }
    uint32_t g_lastFieldStart = 0;
    uint64_t g_lastCycle = 0, g_lastIdleUs = 0;
    std::chrono::steady_clock::time_point g_lastHost;
    bool g_hostProfWatch = false;

    void fieldLogFrame(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t start = READ32(0x6F8DC0u);
        if (start == g_lastFieldStart)
            return;
        g_lastFieldStart = start;
        const auto now = std::chrono::steady_clock::now();
        const uint64_t idleUs = g_wotmIdleUs.load(std::memory_order_relaxed);
        int top[3] = {-1, -1, -1};
        for (int i = 0; i < 4096; ++i)
        {
            if (!g_wotmProfCyc[i])
                continue;
            for (int k = 0; k < 3; ++k)
            {
                if (top[k] < 0 || g_wotmProfCyc[i] > g_wotmProfCyc[top[k]])
                {
                    for (int m = 2; m > k; --m)
                        top[m] = top[m - 1];
                    top[k] = i;
                    break;
                }
            }
        }
        const double field = 294912000.0 / 59.94;
        static uint32_t lastTics = 0;   // ticks do host (relogio nativo) ou D_006F8DC4, os do timer 1 emulado (9619 por field)
        const uint32_t tics = g_nativeTimer ? hostTics() : READ32(0x6F8DC4u);
        static double lastCpuMs = 0.0;   // CPU da thread do EE (usuario + kernel): quadro lento com pouca CPU = bloqueado fora do ocio
        const double cpuMs = wotmThreadCpuMs();
        // a linha numa escrita so: o stderr nao tem buffer, e cada fprintf seria uma chamada ao sistema na thread do EE
        char line[512];
        int len = 0;
        auto add = [&](const char *fmt, auto... a) {
            if (len < int(sizeof(line)))
                len += std::snprintf(line + len, sizeof(line) - size_t(len), fmt, a...);
        };
        add("[field] g=%u fields=%d rate=%d tics=%.2f ee=%.2f idle=%.2f host=%.1fms ocioso=%.1fms cpu=%.1fms top",
                     READ32(wotm::sym::g_frame), static_cast<int32_t>(READ32(0x6F8DB4u)), static_cast<int32_t>(READ32(kUpdateRateVar)),
                     (tics - lastTics) / 9619.0,
                     (g_wotmEeCycle - g_lastCycle) / field, g_wotmIdleCycles / field,
                     std::chrono::duration<double, std::milli>(now - g_lastHost).count(), (idleUs - g_lastIdleUs) / 1000.0,
                     cpuMs - lastCpuMs);
        lastCpuMs = cpuMs;
        for (int k = 0; k < 3; ++k)
            if (top[k] >= 0)
                add(" %06x=%.2f", g_wotmProfPc[top[k]], g_wotmProfCyc[top[k]] / field);
        add(" syncv=%u", g_syncVCalls);
        if (g_svAtMs >= 0.0)   // sv=<ms ate a 1a chamada>@<vblanks ja entregues>/<ms ate aqui, o fim do quadro>
            add(" sv=%.1f@%d/%.1f", g_svAtMs, g_svVblanks,
                         std::chrono::duration<double, std::milli>(now - g_frameStartHost).count());
        g_svAtMs = -1.0;
        for (uint32_t k = 0; k < g_syncVCalls && k < 4; ++k)
            add("%c%06x", k ? ',' : ':', g_syncVRa[k]);
        g_syncVCalls = 0;
        unsigned views = 0;
        for (unsigned v = 0; v < 4; ++v)
            views += (g_hierViews >> v) & 1u;
        add(" vistas=%u hier=%.2f/%.2f cap=%.2f/%.2f", views, g_hierMs[0], g_hierMs[1], g_captureMs[0], g_captureMs[1]);
        if (g_hierViews & 0xCu)   // 3 e 4 jogadores (wotm_split4.inc): vistas 2 e 3
            add(" hier23=%.2f/%.2f cap23=%.2f/%.2f", g_hierMs[2], g_hierMs[3], g_captureMs[2], g_captureMs[3]);
        len = std::min(len, int(sizeof(line)) - 2);
        line[len++] = 10;
        std::fwrite(line, 1, size_t(len), stderr);
        g_hierViews = 0;
        for (int v = 0; v < 4; ++v)
            g_hierMs[v] = g_captureMs[v] = 0.0;
        // com PS2X_HOSTPROF: onde a thread do EE esteve num quadro de mais de 100 ms (ps2_hostprof.cpp)
        if (!g_hostProfWatch && std::getenv("PS2X_HOSTPROF") && !std::getenv("PS2X_HOSTPROF_WIN"))
        {
            g_hostProfWatch = true;
            wotmHostProfWatchThisThread();
        }
        if (g_hostProfWatch && now - g_lastHost > std::chrono::milliseconds(100))
        {
            char tag[48];
            std::snprintf(tag, sizeof(tag), "quadro g=%u", READ32(wotm::sym::g_frame));
            wotmHostProfDump(g_lastHost.time_since_epoch().count(), now.time_since_epoch().count(), tag);
        }
        if (g_hostProfWatch)
            wotmHostProfFrameStart();
        std::memset(g_wotmProfPc, 0, sizeof(g_wotmProfPc));
        std::memset(g_wotmProfCyc, 0, sizeof(g_wotmProfCyc));
        g_wotmIdleCycles = 0;
        g_lastCycle = g_wotmEeCycle;
        g_lastHost = now;
        g_lastIdleUs = idleUs;
        lastTics = tics;
    }

    // 3 e 4 jogadores a 60: o jogo liga um "modo 30 Hz" com 3+ jogadores (Shell::Use30HzMode: m_numPlayers >= 3), e o
    // Shell::BootInitGame faz timerInit(30), que poe o teto do updateRate (D_006F8DD8) em 30: o timerWaitForMinUpdateRate espera
    // um vsync a mais em todo quadro que coube em 1 field. Aqui o timerInit desse ponto recebe 60; o resto do modo (layout da VRAM
    // do getVramAddr) fica. WOTM_30HZ=1 volta ao original.
    constexpr uint32_t kTimerInit = wotm::fn::timerInit__Fi;
    constexpr uint32_t kBootInitGameTimerRa = wotm::fn::BootInitGame__5Shell + 0x24u;   // retorno do jal timerInit
    PS2Runtime::RecompiledFunction g_origTimerInit = nullptr;
    void timerInit60(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kTimerInit && GPR_U32(ctx, 31) == kBootInitGameTimerRa && GPR_U32(ctx, 4) == 30u)
            SET_GPR_U32(ctx, 4, 60u);
        g_origTimerInit(rdram, ctx, runtime);
    }

    void waitMinRateByVblanks(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // so na entrada: retomada no meio (depois de um sceGsSyncV do laco) ja tem D_006F8DC0 atualizado
        if (ctx->pc == kTimerWaitForMinUpdateRate)
        {
            // [field] do quadro anterior: aqui roda em todo quadro (timerGetUpdateRate nao: no menu e no inicio da fase
            // ha quadros sem chamada, e a linha somava dois quadros)
            if (g_fieldLog)
                fieldLogFrame(rdram, ctx, runtime);
            // quadro pronto (update, hier, particleDraw) e o seguinte ainda nao comecou: o desenho nativo copia a cena aqui
            wotm_split4::trainingHudLate(rdram, ctx, runtime);   // modo treino: HUD do boneco se o update do HUD 0 foi pausado
            wotmCaptureFrame(rdram);
            // pausa (F7/F6/F8, wotm_foto.inc): o quadro pronto fica na tela e o seguinte nao comeca. O quadro com parada conta 1 field:
            // o tempo parado nao vira dt (sem isso o updateRate caia ao limite e o jogo dava um passo de 1/30 na volta)
            wotmPauseBlock(runtime);
            if (wotmPauseTookFrame())
                WRITE32(kFieldCounter, READ32(kFieldCounterAtStart) + 1u);
            else if ((g_nativeTimer || g_wotmFreeSpinPc != 0u) && g_frameStartValid)
            {
                const uint64_t vblanks = runtime->eeScheduler().currentVSyncTick() - g_frameStartVsync;
                const int32_t start = static_cast<int32_t>(READ32(kFieldCounterAtStart));
                const int32_t measured = static_cast<int32_t>(READ32(kFieldCounter)) - start;
                if (vblanks > 0u && vblanks < 16u && static_cast<int32_t>(vblanks) > measured)
                    WRITE32(kFieldCounter, static_cast<uint32_t>(start + static_cast<int32_t>(vblanks)));
            }
        }
        g_origWaitMinRate(rdram, ctx, runtime);
    }

    // Fades para o desenho nativo (wotm_live.inc, Scene::drawScreenRects): o jogo escurece a tela com um SPRITE preto do GS montado na
    // hora. O nativo o refaz pelos argumentos de cada emissor, sem ler o pacote: rtFadeScreen (entrada da luta) e TheGame::updateFadeOut/
    // updateFadeOutAndIn (fim de rodada, cinemas) vao pela fila do gsPkt; uiFadeScreen (menus) e letterboxDmaData mandam direto; o
    // Shell::FadeScreen e um laco que grava um alfa por vsync (wotmShellFade na entrada, wotmShellFadeStep no sceGsSyncV do laco).
    constexpr uint32_t kUiFadeScreen = wotm::fn::uiFadeScreen__FUi, kShellFadeScreen = wotm::fn::FadeScreen__5ShellibRUiT3UcUcUcUcUcUc;
    PS2Runtime::RecompiledFunction g_origUiFade = nullptr, g_origShellFade = nullptr;

    constexpr uint32_t kRtFadeScreen = wotm::fn::rtFadeScreen__FiUi, kTheGameFadeOut = wotm::fn::updateFadeOut__7TheGame,
                       kTheGameFadeOutAndIn = wotm::fn::updateFadeOutAndIn__7TheGame;
    PS2Runtime::RecompiledFunction g_origRtFade = nullptr, g_origGameFadeOut = nullptr, g_origGameFadeOutAndIn = nullptr;

    void rtFadeScreenTapped(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kRtFadeScreen)   // rtFadeScreen(vista, quanto): alfa 0x80 - quanto, pela fila do gsPkt
            wotmFadeView(rdram, static_cast<int>(GPR_U32(ctx, 4)), 0x80u - GPR_U32(ctx, 5), true);
        g_origRtFade(rdram, ctx, runtime);
    }
    // O nivel do fade so fica certo depois que a funcao o atualiza: le na volta.
    template <uint32_t Entry, PS2Runtime::RecompiledFunction *Orig> void theGameFadeTapped(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static uint32_t ra = 0, sp = 0, self = 0;
        if (ctx->pc == Entry)
        {
            ra = GPR_U32(ctx, 31);
            sp = GPR_U32(ctx, 29);
            self = GPR_U32(ctx, 4);
        }
        (*Orig)(rdram, ctx, runtime);
        if (ra && ctx->pc == ra && GPR_U32(ctx, 29) == sp)
        {
            ra = 0;
            wotmTheGameFade(rdram, self);
        }
    }

    // Ordem fila x texto: o rtMain manda o texto (fontDmaFontData) e depois a fila do gsPkt (DmaGsPktQueue), e o fade cobre o texto;
    // o ShellRtLoop::endField (laco dos dialogos de fim de fase, wonLevelOverlay e cia.) manda a fila antes do texto, e o dialogo de
    // tokens sai sobre a tela escurecida pelo TheGame::updateFadeOut. Marcado na entrada de cada um (ScreenRect::underText).
    constexpr uint32_t kDmaGsPktQueue = wotm::fn::DmaGsPktQueue__5GsPkt, kFontDmaFontData = wotm::fn::fontDmaFontData__Fv;
    PS2Runtime::RecompiledFunction g_origDmaGsPktQueue = nullptr, g_origFontDma = nullptr;

    void dmaGsPktQueueTapped(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kDmaGsPktQueue)
            wotmGsPktQueueSent();
        g_origDmaGsPktQueue(rdram, ctx, runtime);
    }

    void fontDmaTapped(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kFontDmaFontData)
        {
            wotmFontDma(rdram);
            wotmFontSent();
        }
        g_origFontDma(rdram, ctx, runtime);
    }

    // Texto pelas chamadas da fonte (wotm::FontText): o desenho nativo refaz os glifos sem ler o fontPacket.
    constexpr uint32_t kFontInitPacket = wotm::fn::fontInitPacket__FP6QwData10_vramAddrs, kFontSetTexId = wotm::fn::fontSetTexId__FP9_hierhead,
                       kFontSetColorGifTag = wotm::fn::fontSetColorGifTag__Fi, kFontBuildPrim = wotm::fn::fontBuildPrim__FicP6QwData;
    PS2Runtime::RecompiledFunction g_origFontInitPacket = nullptr, g_origFontSetTexId = nullptr, g_origFontColor = nullptr, g_origFontGlyph = nullptr;

    void fontInitPacketTapped(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kFontInitPacket)
            wotmFontInitPacket(GPR_U32(ctx, 5));
        g_origFontInitPacket(rdram, ctx, runtime);
    }
    void fontSetTexIdTapped(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kFontSetTexId)
            wotmFontSetTexId(rdram, GPR_U32(ctx, 4));
        g_origFontSetTexId(rdram, ctx, runtime);
    }
    void fontColorTapped(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kFontSetColorGifTag)
            wotmFontColor(rdram, static_cast<int>(GPR_U32(ctx, 4)));
        g_origFontColor(rdram, ctx, runtime);
    }
    void fontGlyphTapped(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kFontBuildPrim)
            wotmFontGlyph(rdram, static_cast<int>(GPR_U32(ctx, 4)), static_cast<int>(GPR_U32(ctx, 5) & 0xFF));
        g_origFontGlyph(rdram, ctx, runtime);
    }

    void uiFadeScreenTapped(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kUiFadeScreen)   // uiFadeScreen(quanto): vista 0, alfa 0x80 - quanto, direto
            wotmFadeView(rdram, 0, 0x80u - GPR_U32(ctx, 4), false);
        g_origUiFade(rdram, ctx, runtime);
    }

    // Clarao branco da 3-MILE: o threeMileDrawFlash monta um SPRITE de tela e o poe na fila do gsPkt (sem gancho, o nativo nao o via).
    constexpr uint32_t kThreeMileDrawFlash = wotm::fn::threeMileDrawFlash__Fv;
    PS2Runtime::RecompiledFunction g_origThreeMileFlash = nullptr;
    void threeMileFlashTapped(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kThreeMileDrawFlash)
            wotmThreeMileFlash(rdram);
        g_origThreeMileFlash(rdram, ctx, runtime);
    }

    // Faixas pretas do menu: o letterboxDmaData(alfa) monta o pacote em D_00731850 e o manda direto; lido depois de voltar.
    constexpr uint32_t kLetterboxDmaData = wotm::fn::letterboxDmaData__Ff;
    PS2Runtime::RecompiledFunction g_origLetterbox = nullptr;

    void letterboxTapped(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kLetterboxDmaData)   // letterboxDmaData(alfa em $f12)
            wotmLetterbox(rdram, ctx->f[12]);
        g_origLetterbox(rdram, ctx, runtime);
    }

    void shellFadeScreenTapped(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kShellFadeScreen)
        {
            // a0 = this, a1 = vista, a2 = redesenha, t1-t3 = r, g, b; na pilha inicio (+0), fim (+8) e passo (+0x10)
            const uint32_t sp = GPR_U32(ctx, 29);
            wotmShellFade(rdram, static_cast<int>(GPR_U32(ctx, 5)), GPR_U32(ctx, 9), GPR_U32(ctx, 10), GPR_U32(ctx, 11), READ8(sp), READ8(sp + 8),
                          READ8(sp + 0x10), (GPR_U32(ctx, 6) & 0xFF) != 0);
        }
        g_origShellFade(rdram, ctx, runtime);
    }

    // Carga da fase: da entrada do Shell::LoadLevelFiles (o nivel novo comeca a sobrescrever a RAM; o cartao LOAD continua na
    // hierarquia) ate a entrada do rtMain, a tela nativa fica no ultimo quadro, so com o Shell::FadeScreen por cima (wotmLoadFreeze).
    constexpr uint32_t kLoadLevelFiles = wotm::fn::LoadLevelFiles__5Shell, kRtMain = wotm::fn::rtMain__Fb;
    PS2Runtime::RecompiledFunction g_origLoadLevelFiles = nullptr, g_origRtMain = nullptr;

    unsigned g_loadGen = 0;   // cargas de fase (Shell::LoadLevelFiles) desde o boot: o que e guardado por arquivo carregado recomeca

    void loadLevelFilesTapped(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kLoadLevelFiles)
        {
            wotmLoadFreeze(true);
            ++g_loadGen;
        }
        g_origLoadLevelFiles(rdram, ctx, runtime);
    }
    // Fim da carga da fase: a fase e os monstros entram contiguos a partir de 0xA00000 (NGP, depois RTX e TEX, fileStatus) e nada
    // confere o teto. Acima de ~0x1F10000 ha dados vivos (e o buffer do zip em 0x1F7F840); medido em 2026-10-18, o bloco em 0x1B70000
    // e sobra do menu e pode ser sobrescrito (o Metro com 2 monstros vai ate 0x1D0C380). Loga quando muda e avisa perto do teto.
    constexpr uint32_t kFileStatus = wotm::sym::fileStatus, kLoadBase = 0xA00000u, kLoadCeiling = 0x1F10000u;
    // RAM acima de 32 MB (NATIVO.md, "RAM de 64 MB"): o runtime tem 64 MB (PS2_RAM_SIZE) e o jogo ve 32 (PS2_GUEST_RAM_SIZE: pilha, heap e
    // GetMemorySize iguais aos do PS2). Na entrada do Shell::LoadMonstersDB o proximo endereco de carga (fileStatus.ngpAddr[ngpLoaded],
    // logo depois do NGP da fase) pula para 0x2000000: os NGP dos monstros e, atras deles, os RTX e TEX da fase e dos monstros moram na
    // area de cima, e embaixo fica so o NGP da fase. A relocacao (dbsRelocateViaPtrListFile) soma "endereco - 0xA00000" em 32 bits, entao
    // vale em qualquer endereco. ngpAddr[1] (o fim da fase) vira 0x2000000; ninguem le esse valor como tamanho. WOTM_HIGHRAM=0 desliga.
    constexpr uint32_t kLoadMonstersDB = wotm::fn::LoadMonstersDB__5Shell, kHighBase = PS2_GUEST_RAM_SIZE;
    PS2Runtime::RecompiledFunction g_origLoadMonstersDB = nullptr;

    void loadMonstersDBHigh(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kLoadMonstersDB)
        {
            const uint32_t ngpN = READ8(kFileStatus + 0x74);
            const uint32_t slot = kFileStatus + 0x3C + 4 * ngpN;
            if (ngpN >= 1 && ngpN <= 13 && READ32(slot) < kHighBase)
                WRITE32(slot, kHighBase);
        }
        g_origLoadMonstersDB(rdram, ctx, runtime);
    }

    uint32_t g_lastLoadEnd = 0;

    void wotmCheckLoadEnd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t texN = READ8(kFileStatus + 0x38), ngpN = READ8(kFileStatus + 0x74), resN = READ8(kFileStatus + 0xB0);
        if (texN > 13 || ngpN > 13 || resN > 13)
            return;
        const uint32_t end = std::max({READ32(kFileStatus + 4 * texN), READ32(kFileStatus + 0x3C + 4 * ngpN), READ32(kFileStatus + 0x78 + 4 * resN)});
        if (end == g_lastLoadEnd || end < kLoadBase)
            return;
        g_lastLoadEnd = end;
        {
            // Tetos que nao dependem da RAM (item 8: skins e arquivos de mod maiores que o do disco): texInfo tem 1000 entradas e o
            // jogo so imprime "Texture count" ao passar (escreve depois do array); a VRAM residente (.RTX) estourada ja sai do jogo
            // com "TRYING TO LOAD TOO MUCH TEXTURE". fileStatus.maxTexId (0x130) e o maior texId de cada arquivo, acumulado.
            uint32_t maxTex = 0;
            for (uint32_t i = 0; i < std::max(texN, resN); ++i)
                maxTex = std::max<uint32_t>(maxTex, FAST_READ16(kFileStatus + 0x130 + 2 * i));
            std::fprintf(stderr, "[wotm] carga da fase: %u de 1000 texIds%s\n", maxTex,
                         maxTex >= 1000 ? " -- ALEM DO texInfo, espere corrupcao" : maxTex >= 900 ? " -- perto do teto do texInfo" : "");
            // VRAM (em palavras de 32 bits): as .RTX ficam embaixo e o cache das .TEX vai de startVramAddr (logo acima delas) ate
            // lastVramTexAddr. Uma .RTX maior encolhe o cache; o wotm.exe desenha da RAM, entao o cache curto so pesa no ps2EntryRunner.
            const uint32_t vStart = READ32(wotm::sym::startVramAddr), vLast = READ32(wotm::sym::lastVramTexAddr);
            if (vStart && vLast > vStart)
                std::fprintf(stderr, "[wotm] carga da fase: VRAM residente ate 0x%05X, cache de texturas com %u KB\n", vStart,
                             (vLast - vStart) * 4 / 1024);
        }
        if (end > kHighBase)
        {   // area de cima: embaixo fica so o NGP da fase (fileStatus.ngpAddr[0] ate o fim do arquivo, guardado no log da carga)
            const double used = (end - kHighBase) / 1048576.0, left = (int64_t(PS2_RAM_SIZE) - end) / 1048576.0;
            std::fprintf(stderr, "[wotm] carga da fase: %u arquivos, os monstros e as texturas terminam em 0x%07X (%.1f MB acima de 32 MB, %.1f MB ate o fim da RAM)%s\n",
                         ngpN, end, used, left, left < 0.5 ? " -- PERTO OU ALEM DO FIM, espere corrupcao" : "");
            return;
        }
        const double used = (end - kLoadBase) / 1048576.0, left = (int64_t(kLoadCeiling) - end) / 1048576.0;
        std::fprintf(stderr, "[wotm] carga da fase: %u arquivos, termina em 0x%07X (%.1f MB usados, %.1f MB ate o teto 0x%07X)%s\n", ngpN, end,
                     used, left, kLoadCeiling, left < 0.5 ? " -- PERTO OU ALEM DO TETO, espere corrupcao" : "");
    }

    bool g_damageNewFight = true;   // rtMain(first): os numeros de dano esquecem os monstros da luta anterior (monsterUpdateDamage)

    void rtMainTapped(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kRtMain)
        {
            wotmLoadFreeze(false);
            wotmCheckLoadEnd(rdram, ctx, runtime);
            if (GPR_U32(ctx, 4) && wotm_training::g_menuArmed.load() && !wotm_training::g_on.exchange(true))   // rtMain(first): a luta comecou
                std::fprintf(stderr, "[treino] modo treino ligado (luta do TRAINING)\n");
            wotm_training::g_inFight = true;   // tambem a volta do RESET POSITIONS (rtMain(0))
            if (GPR_U32(ctx, 4))
            {
                wotm_training::g_introMs = wotm_training::nowMs();   // a intro desta luta pode ser pulada (wotm_training.inc)
                g_damageNewFight = true;
            }
        }
        g_origRtMain(rdram, ctx, runtime);
    }

    // HUD do ScreenPolys (icone de vida do Hud::drawHealthIcon): um quadrado por polyBuildPrim(src, x, y, pd, pkt), refeito pelo
    // nativo a partir dos argumentos (wotm::screenPolyFromCall), sem ler s_polyBuffer.
    constexpr uint32_t kPolyBuildPrim = wotm::fn::polyBuildPrim__FiiiRQ211ScreenPolys8PolyDataP6QwData;
    PS2Runtime::RecompiledFunction g_origPolyBuild = nullptr;

    void polyBuildPrimTapped(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kPolyBuildPrim)
            wotmScreenPoly(rdram, static_cast<int>(GPR_U32(ctx, 4)), static_cast<int>(GPR_U32(ctx, 5)), static_cast<int>(GPR_U32(ctx, 6)), GPR_U32(ctx, 7));
        g_origPolyBuild(rdram, ctx, runtime);
    }

    // Saida do hier(0, 0): o desenho nativo copia a cena aqui (wotmCaptureScene), com o estado que a travessia acabou de mandar ao
    // VU1; o gameUpdate e o animationRunGlobal do rtMain, logo depois, ja escrevem o quadro seguinte. Na tela dividida a vista 1 sai
    // do mesmo jeito na saida do hier(1, 0) (reserva: entrada do TheGame::Update2, que o rtMain chama logo depois so na vista 1).
    constexpr uint32_t kHier = wotm::fn::hier__Fii;
    PS2Runtime::RecompiledFunction g_origHier = nullptr;

    // A funcao pode voltar no meio (troca de thread) e ser retomada depois, ja sem passar por aqui: so captura quando o pc chega ao
    // $ra da chamada. Quando a volta escapa (60% dos quadros da luta), a captura sai na entrada do TheGame::Update, que o rtMain chama
    // logo depois (so o RtLoopView::end no meio). Sem essa reserva a cena saia do fim do quadro, um quadro a frente do lens flare.
    uint32_t g_hierSceneRa = 0;   // $ra do hier(v, 0) em andamento (0 = nenhum)
    int g_hierSceneView = 0;      // a vista dele
    void hierSceneExit(uint8_t *rdram)
    {
        g_hierSceneRa = 0;
        wotmCaptureScene(rdram, g_hierSceneView);
        if (g_hierSceneView == 0)
            wotmVu1SceneExit(rdram);
    }
    // Custo por vista para o [field] (WOTM_FIELDLOG): tempo de host dentro do hier(v, passo) por vista, nos dois passos, e da captura
    // nativa (wotmCaptureScene) na saida do hier(v, 0). So conta as chamadas que voltam pelo envoltorio (uma que bloqueia e e
    // retomada no meio nao passa por aqui).
    // Vistas 2 e 3 (3 e 4 jogadores, wotm_split4.inc): o rtMain nao roda update entre o hier(v, 0) e o hier(v, 1) delas, entao a
    // reserva da captura e a entrada do hier(v, 1).
    void hierTapped(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const bool entry = ctx->pc == kHier;
        const uint32_t view = GPR_U32(ctx, 4), ra = GPR_U32(ctx, 31);
        if (entry && GPR_U32(ctx, 5) != 0u && g_hierSceneRa && g_hierSceneView >= 2)
            hierSceneExit(rdram);
        if (entry && view < 4u && GPR_U32(ctx, 5) == 0u)
        {
            if (g_hierSceneRa)   // a saida do anterior escapou sem reserva: captura antes de comecar este
                hierSceneExit(rdram);
            g_hierSceneRa = ra;
            g_hierSceneView = static_cast<int>(view);
            g_hierViews |= 1u << view;
        }
        const auto t0 = std::chrono::steady_clock::now();
        g_origHier(rdram, ctx, runtime);
        const auto t1 = std::chrono::steady_clock::now();
        if (entry && view < 4u && ctx->pc == ra)
            g_hierMs[view] += std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (g_hierSceneRa && ctx->pc == g_hierSceneRa)
        {
            const int v = g_hierSceneView;
            hierSceneExit(rdram);
            if (v >= 0 && v < 4)
                g_captureMs[v] += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
        }
    }

    // Lens flare: a lista fica pronta (projetada e testada) na saida do particleLensFlareRender. O main.cpp a copia depois do
    // particleDraw, mas no quadro armado pela checagem (F8, WOTM_VU1CHECK) o DMA roda de verdade, o particleDraw espera e volta no
    // meio, e aquela copia pegava a lista ainda em coordenadas de mundo (o quadro da foto saia sem flare). Esta vem depois e vale.
    constexpr uint32_t kLensFlareRender = wotm::fn::particleLensFlareRender__FP6QwDataiPPUi;
    PS2Runtime::RecompiledFunction g_origLensFlareRender = nullptr;
    void lensFlareRenderTapped(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t ra = ctx->pc == kLensFlareRender ? GPR_U32(ctx, 31) : 0u;
        g_origLensFlareRender(rdram, ctx, runtime);
        if (ra && ctx->pc == ra)
            wotmFlareCapture(rdram);
    }

    constexpr uint32_t kTheGameUpdate = wotm::fn::Update__7TheGame;
    PS2Runtime::RecompiledFunction g_origTheGameUpdate = nullptr;
    // WOTM_LOGICDELAY=<ms>: atraso de host de 0 a <ms> (sorteado, sequencia fixa) na entrada do TheGame::Update, para testar o
    // relogio e o vsync com quadros de 1 e 2 fields (FRONTEIRA.md, passo 4). Diagnostico; nunca ligado por padrao.
    int g_logicDelayMs = 0;
    uint32_t g_logicDelaySeed = 12345u;
    void theGameUpdateTapped(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kTheGameUpdate && g_hierSceneRa)
            hierSceneExit(rdram);
        if (ctx->pc == kTheGameUpdate && g_logicDelayMs > 0)
        {
            g_logicDelaySeed = g_logicDelaySeed * 1103515245u + 12345u;
            const uint32_t us = (g_logicDelaySeed >> 8) % (static_cast<uint32_t>(g_logicDelayMs) * 1000u + 1u);
            const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(us);
            while (std::chrono::steady_clock::now() < until)   // espera ativa: o sleep do Windows arredonda para ~1 ms ou mais
                ;
        }
        g_origTheGameUpdate(rdram, ctx, runtime);
    }
    constexpr uint32_t kTheGameUpdate2 = wotm::fn::Update2__7TheGame;
    PS2Runtime::RecompiledFunction g_origTheGameUpdate2 = nullptr;
    void theGameUpdate2Tapped(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kTheGameUpdate2 && g_hierSceneRa)
            hierSceneExit(rdram);
        g_origTheGameUpdate2(rdram, ctx, runtime);
    }

    // Tom verde do pick-up que o monstro pode pegar: o enableHighlights o grava no colorQuad do _cs antes do hier(v,0), e o
    // computeHighlight (Monster::update, depois do hier) o volta a 1 antes da captura. Na saida, wotmPickupHighlights anota o valor
    // (Scene::csColorAtDraw).
    constexpr uint32_t kEnableHighlights = wotm::fn::enableHighlights__12LevelPickupsi;
    PS2Runtime::RecompiledFunction g_origEnableHighlights = nullptr;

    void enableHighlightsTapped(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const bool entry = ctx->pc == kEnableHighlights;
        g_origEnableHighlights(rdram, ctx, runtime);
        if (entry)
            wotmPickupHighlights(rdram);
    }

    // Mesmo comportamento da original (`jr $ra; lw $v0, gp_rel(D_006F8DB8)`), exceto o limite acima.
    void timerGetUpdateRate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int32_t rate = static_cast<int32_t>(READ32(kUpdateRateVar));
        if (rate > 0 && rate < g_minRate)
        {
            rate = g_minRate;
            if ((++g_clamped % 20000ull) == 0 && std::getenv("PS2X_STATS"))
                std::fprintf(stderr, "[wotm] updateRate limitado a %d em %llu chamadas\n", g_minRate, g_clamped);
        }
        SET_GPR_S32(ctx, 2, rate);
        ctx->pc = GPR_U32(ctx, 31);
    }
}


namespace
{
    // Emissor de particulas que "teleporta" (desde 2026-10-21; achado com 8 monstros). Emissor com EmitMotionSense (0x1000000; o fogo
    // da cabeca do Agamo, fxId 141 IdolHeadFlame) soma ao cumulativeRate a distancia que andou desde o quadro anterior:
    // |ParticleToEmitVec - prevParticleToEmitVec| * 4 / maxSize. Num quadro a posicao veio de muito longe (Agamo com outros 7 tipos de
    // monstro no Centro e no Aeroporto), o acumulado foi a ~5e18 e ficou la (n = cvt.w.s satura em 0x7FFFFFFF e o resto nao desce);
    // com particulas vivas o numParticles + n do teto de maxParticles passa de 2^31, vira negativo, o teto nao pega e o laco de emissao
    // anda 2^31 vezes: o jogo trava no quadro 3. No PS2 nao havia como ter tantos monstros. Correcao: na saida do
    // utilUpdateEmitterMatrices(e, vento), um salto de mais de kJump unidades (uma fase tem ~3000) vira parado (anterior = atual); e,
    // na entrada do particleEmit, acumulado fora de [0, 2^16) volta a 0 e numParticles fora de [0, maxParticles] volta ao limite.
    // Fora desses casos nada muda. Uma linha [wotm] por correcao (ate 8).
    constexpr uint32_t kEmitMatrices = wotm::fn::utilUpdateEmitterMatrices__FP11EmitterTypeP8_fvector,
                       kParticleEmit = wotm::fn::particleEmit__FP13_ParticleTypeP8_fvectorT1;
    constexpr uint32_t kEmitVec = 0x0, kEmitPrevVec = 0x50, kEmitCumRate = 0x138, kEmitFxId = 0x13C;   // EmitterType
    constexpr uint32_t kPtEmit = 0x2CA0, kPtNum = 0x2ED0, kPtMax = 0x2ED4;                            // ParticleType
    constexpr float kJump = 100000.f;
    PS2Runtime::RecompiledFunction g_origEmitMatrices = nullptr, g_origParticleEmit = nullptr;
    int g_emitFixes = 0;

    float emitF(uint8_t *rdram, uint32_t a)
    {
        float v;
        std::memcpy(&v, rdram + a, 4);
        return v;
    }

    void emitMatricesGuarded(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t e = GPR_U32(ctx, 4) & 0x1FFFFFFFu, ra = ctx->pc == kEmitMatrices ? GPR_U32(ctx, 31) : 0u;
        g_origEmitMatrices(rdram, ctx, runtime);
        if (!ra || ctx->pc != ra || e < 0x1000u || e + 0x140u > PS2_RAM_SIZE)
            return;
        float d2 = 0.f;
        for (uint32_t k = 0; k < 3; ++k)
        {
            const float d = emitF(rdram, e + kEmitVec + 4 * k) - emitF(rdram, e + kEmitPrevVec + 4 * k);
            d2 += d * d;
        }
        if (d2 > kJump * kJump || !std::isfinite(d2))
        {
            std::memcpy(rdram + e + kEmitPrevVec, rdram + e + kEmitVec, 16);
            if (g_emitFixes < 8 && ++g_emitFixes)
                std::fprintf(stderr, "[wotm] emissor de particulas fxId %u saltou %.3g unidades: contado como parado\n", FAST_READ32(e + kEmitFxId),
                             std::sqrt(d2));
        }
    }

    void particleEmitGuarded(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t p = GPR_U32(ctx, 4) & 0x1FFFFFFFu;
        if (ctx->pc == kParticleEmit && p >= 0x1000u && p + kPtMax + 4 <= PS2_RAM_SIZE)
        {
            const float cum = emitF(rdram, p + kPtEmit + kEmitCumRate);
            const int32_t num = static_cast<int32_t>(FAST_READ32(p + kPtNum)), mx = static_cast<int32_t>(FAST_READ32(p + kPtMax));
            const bool badCum = !(cum >= 0.f && cum < 65536.f), badNum = num < 0 || (mx >= 0 && num > mx);
            if (badCum)
            {
                const float zero = 0.f;
                std::memcpy(rdram + p + kPtEmit + kEmitCumRate, &zero, 4);
            }
            if (badNum)
                FAST_WRITE32(p + kPtNum, static_cast<uint32_t>(num < 0 ? 0 : mx));
            if ((badCum || badNum) && g_emitFixes < 8 && ++g_emitFixes)
                std::fprintf(stderr, "[wotm] particleEmit fxId %u: acumulado %g e %d/%d particulas corrigidos\n", FAST_READ32(p + kPtEmit + kEmitFxId), cum,
                             num, mx);
        }
        g_origParticleEmit(rdram, ctx, runtime);
    }

    // Raio em cadeia do Kineticlops que trava com mais de 4 monstros (desde 2026-10-21; Metro com 8). No StateZapAttack::update, o
    // laco que passa o raio do alvo para os outros monstros (i de 0 a numMonsters) e o laco que desenha os arcos de cada um (de 0 a
    // numArcs, float @0x50 do estado) usam a mesma variavel ($s4) no retail: depois de encadear no monstro k, i fica em numArcs + 1.
    // Com 4 monstros isso so encurta o laco (i passa do fim; o raio pega um monstro a mais por quadro). Com 8 e k >= numArcs + 1,
    // i volta para tras, o laco encadeia de novo no mesmo k e nunca termina. Correcao: guarda i na chamada do hdLosCSTest do laco
    // (inicio do teste de cada monstro) e, na primeira chamada depois do laco dos arcos (mathfRand), i = max(i, guardado). Onde o
    // retail nao trava (i nao volta para tras) o resultado e o mesmo. Uma linha [wotm] por correcao (ate 8).
    constexpr uint32_t kZapUpdate = wotm::fn::update__14StateZapAttack, kLosCs = wotm::fn::hdLosCSTest__FP8_fvectorT0P9_hdResulti,
                       kMathfRand = wotm::fn::mathfRand__Fii;
    constexpr uint32_t kZapRaLos = kZapUpdate + 0xB60, kZapRaAfterArcs = kZapUpdate + 0xC4C;   // retornos das duas chamadas no laco
    constexpr int kRegS4 = 20;
    PS2Runtime::RecompiledFunction g_origLosCs = nullptr, g_origMathfRand = nullptr;
    int32_t g_zapIndex = -1;
    int g_zapFixes = 0;

    void losCsTapped(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kLosCs && GPR_U32(ctx, 31) == kZapRaLos)
            g_zapIndex = static_cast<int32_t>(GPR_U32(ctx, kRegS4));
        g_origLosCs(rdram, ctx, runtime);
    }

    void mathfRandTapped(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kMathfRand && GPR_U32(ctx, 31) == kZapRaAfterArcs && g_zapIndex >= 0)
        {
            const int32_t i = static_cast<int32_t>(GPR_U32(ctx, kRegS4));
            if (i < g_zapIndex)
            {
                SET_GPR_U32(ctx, kRegS4, static_cast<uint32_t>(g_zapIndex));
                if (g_zapFixes < 8 && ++g_zapFixes)
                    std::fprintf(stderr, "[wotm] raio em cadeia (StateZapAttack): o laco dos arcos levou o indice dos monstros de %d para %d; mantido em %d\n",
                                 g_zapIndex, i, g_zapIndex);
            }
            g_zapIndex = -1;
        }
        g_origMathfRand(rdram, ctx, runtime);
    }
}
#include "wotm_orbit.inc"   // camera moderna (orbital): ganchos chamados pelos da camera livre e do lock-on, abaixo
namespace
{
    // Camera livre (F5, wotm_freecam.h). O _cs da vista 0 (viewInfo[0], o mesmo que a camera do jogo move) recebe a pose em dois
    // pontos: depois de Cameras::Update (o resto da logica do quadro, como os scripts da fase, o som e a IA, ja ve a camera nova) e na
    // entrada de viewUpdate(0), que monta worldToScreenMat e worldCtx[0] a partir de cs->trans e cs->mat (ganha de quem mexer no
    // _cs entre os dois, como a camera da introducao da luta). A camera do jogo continua rodando por baixo: ao desligar, ela sai da
    // pose livre e volta sozinha para o monstro. Com ela ligada (e sem o Tab), o scePadRead devolve o pad em repouso.
    constexpr uint32_t kViewUpdate = wotm::fn::viewUpdate__Fi, kScePadRead = wotm::fn::scePadRead;
    constexpr uint32_t kViewInfo = wotm::sym::viewInfo;   // 5 x 0x14: [0] = _cs* da vista
    PS2Runtime::RecompiledFunction g_origViewUpdate = nullptr, g_origCamerasUpdateFree = nullptr, g_origPadRead = nullptr;

    void putF(uint8_t *rdram, uint32_t a, float v) { std::memcpy(rdram + a, &v, 4); }

    void freeCamWrite(uint8_t *rdram)
    {
        if (!g_wotmFreeCam.load(std::memory_order_relaxed))
            return;
        uint32_t cs = 0;
        std::memcpy(&cs, rdram + kViewInfo, 4);
        cs &= 0x1FFFFFFFu;
        if (cs < 0x1000u || cs + 0x70u > PS2_RAM_SIZE)
            return;
        WotmFreeCamPose p;
        {
            std::lock_guard<std::mutex> lk(g_wotmFreeCamM);
            p = g_wotmFreeCamPose;
        }
        float ax[3][3];
        wotmFreeCamAxes(p, ax[0], ax[1], ax[2]);
        for (int i = 0; i < 3; ++i)
        {
            putF(rdram, cs + 0x10 + 4 * i, p.pos[i]);   // trans
            for (int r = 0; r < 3; ++r)
                putF(rdram, cs + 0x20 + 0x10 * r + 4 * i, ax[r][i]);   // mat: direita, frente, cima
        }
        for (int r = 0; r < 3; ++r)
            putF(rdram, cs + 0x20 + 0x10 * r + 0xC, 0.f);
        putF(rdram, cs + 0x60, p.pitch);   // rot (RPH): a camera do jogo parte daqui ao desligar
        putF(rdram, cs + 0x64, 0.f);
        putF(rdram, cs + 0x68, p.heading);
    }

    void camerasUpdateFree(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t ra = GPR_U32(ctx, 31);
        orbit::pre(rdram);   // camera moderna (wotm_orbit.inc)
        g_origCamerasUpdateFree(rdram, ctx, runtime);
        if (ctx->pc == ra)
        {
            orbit::post(rdram, ctx, runtime);
            wotm_custom::laserCam(rdram);   // camera da torre do Mecha no laser (wotm_custom.inc)
        }
        freeCamWrite(rdram);
    }

    void viewUpdateFree(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (GPR_U32(ctx, 4) == 0u)
            freeCamWrite(rdram);
        g_origViewUpdate(rdram, ctx, runtime);
    }

    void padReadFree(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t data = GPR_U32(ctx, 6) & 0x1FFFFFFFu;   // scePadRead(port, slot, rdata)
        g_origPadRead(rdram, ctx, runtime);
        if (!g_wotmFreeCam.load(std::memory_order_relaxed) || g_wotmFreeCamPad.load(std::memory_order_relaxed))
            return;
        if (GPR_U32(ctx, 2) == 0u || data < 0x1000u || data + 32u > PS2_RAM_SIZE)
            return;
        uint8_t *d = rdram + data;
        d[2] = d[3] = 0xFF;                // botoes (ativos em 0)
        std::memset(d + 4, 0x80, 4);       // analogicos no centro
        std::memset(d + 8, 0, 12);         // pressao dos botoes
    }
}

namespace
{
    // Lock-on alternado (mod, WOTM_LOCKTOGGLE=1; grupo "QoL features" do launcher). O lock-on do quadro (PadEntry +0x32) e decidido em
    // updateInput{0,1,2}Config{CR,SR} a partir dos contadores de quadros segurados que o updateHistory{0,1,2} da mesma
    // configuracao acabou de atualizar (PadFlags +0x1658 e +0x165E: ++ segurado, 0 solto). Na configuracao 0 trava com os dois
    // acima de PadFlags +0x16C4 (um so anda de lado, PadEntry +0x1A/+0x1C); na 1 e na 2, com +0x1658 diferente de zero. A propria
    // configuracao le o +0x32 logo em seguida para escolher o mapeamento do analogico e do direcional; por isso regravar so o
    // +0x32 depois do interpretInputs deixava o boneco travado com o movimento de solto. Aqui, na saida do updateHistory, um
    // toque novo no lock-on liga ou desliga um estado por jogador humano (dono em PadFlags +0x1810), e os contadores passam a
    // valer "segurando" (ligado) ou "nao segurando" (desligado): o resto da configuracao, inclusive as combinacoes com o lock-on,
    // ve exatamente o que veria com o botao segurado ou solto. Os contadores reais ficam guardados e voltam antes do proximo
    // updateHistory, que conta a partir deles. Alvo travado morto desliga o estado (segurando, o updateLock pularia para o
    // monstro mais proximo). A IA nao muda.
    //
    // Botoes do mod na configuracao 0 (a padrao; PadFlags +0x16EC): o GamePad que o loadPadInputs monta tem um byte por botao
    // (0 L1, 1 L2, 3 R1, 4 R2, 0xC/0xD esquerda/direita do direcional ou do analogico). Na configuracao 0 o jogo usa L1+R1 para
    // o lock-on, L1 ou R1 sozinho para andar de lado (o valor do byte vai para PadEntry +0x1A/+0x1C), R2 para a defesa (e o
    // contra-ataque) e L2 para a camera. Na entrada do interpretInputs os bytes sao trocados: L2 fisico -> byte 0 e R2 -> byte 3
    // (andar de lado), L1 -> byte 4 (defesa), L2 do jogo fica solto; o R1 fisico so alterna o lock-on (lido aqui, nao pelo jogo).
    // Com o lock-on ligado, esquerda/direita do direcional (lidas cruas no scePadRead, porta = jogador) trocam o alvo
    // (m_pinTarget: o updateLock mantem o alvo enquanto ele continua alvejavel) para o proximo monstro vivo a esquerda ou a
    // direita na tela, e nao movem o boneco; o L2/R2 entra como esquerda/direita, que travado o jogo ja transforma em andar de
    // lado (PadEntry +0x16/+0x18 -> +0x1A/+0x1C, so sem L2/R2 do jogo, o byte 4 = defesa). Os bytes originais voltam na saida. As configuracoes 1 e 2 ficam com os botoes do jogo.
    constexpr uint32_t kUpdateHistory[3] = {wotm::fn::updateHistory0__8PadFlagsR7GamePad, wotm::fn::updateHistory1__8PadFlagsR7GamePad,
                                            wotm::fn::updateHistory2__8PadFlagsR7GamePad};
    constexpr uint32_t kInterpretInputs = wotm::fn::interpretInputs__8PadFlagsR7GamePad;
    constexpr uint32_t kPadFlagsLockA = 0x1658u, kPadFlagsLockB = 0x165Eu, kPadFlagsHoldTweak = 0x16C4u, kPadFlagsConfig = 0x16ECu,
                       kPadFlagsOwner = 0x1810u, kPadFlagsScreenRel = 0x16E4u;
    // m_monsters (TheGame +0x120380): 4 lugares de jogador e as IAs a partir do [4]; com a luta de ate 8 a IA 7 fica no [10], e o
    // TheGame tem lugar para 16. Percorrer so 8 deixava as IAs 5 a 7 fora do lock-on.
    constexpr uint32_t kMonsterSlots = 16u;
    // m_playerNum (+0x18) 0 = lugar alocado mas fora da luta: no Endurance as IAs da fila ficam em m_monsters desde o comeco,
    // paradas e "vivas", e so a que entrou tem m_playerNum; o getClosestMonster do jogo pula as de 0 (o lock-on travava nelas).
    constexpr uint32_t kMonsterCs = 0xCu, kMonsterPlayerNum = 0x18u, kMonsterPinTarget = 0x6C04u, kMonsterDead = 0xE8u,
                       kMonsterDying = 0xEFu, kMonsterCameraView = 0x6CD8u;
    constexpr uint32_t kPadL1 = 0x0u, kPadL2 = 0x1u, kPadR1 = 0x3u, kPadR2 = 0x4u, kPadLeft = 0xCu, kPadRight = 0xDu, kPadUp = 0xEu,
                       kPadDown = 0xFu;
    PS2Runtime::RecompiledFunction g_origUpdateHistory[3] = {}, g_origInterpretInputs = nullptr, g_origPadReadLock = nullptr;
    struct LockToggle
    {
        uint32_t pf = 0;         // PadFlags do jogador (0 = livre)
        uint32_t owner = 0;      // monstro dono (outro dono no mesmo PadFlags zera o estado)
        bool held = false;       // lock-on fisicamente segurado no quadro anterior (borda)
        bool on = false;         // estado alternado
        bool forged = false;     // contadores regravados (realA/realB voltam antes do proximo updateHistory)
        bool remap = false;      // botoes do mod neste quadro (configuracao 0): o lock-on e o R1 fisico (lockHeld)
        bool lockHeld = false;
        bool dirHeld[2] = {};    // esquerda/direita do direcional no quadro anterior (borda da troca de alvo)
        bool screenRel = false;  // PadFlags +0x16E4 neste quadro: updateInput0ConfigSR (o jogo ja faz o analogico relativo a camera)
        uint16_t realA = 0, realB = 0, fakeA = 0, fakeB = 0;
    } g_lockToggle[4];
    uint8_t g_padDir[4] = {};    // por controle (porta + 2 * slot, o do jogador): bit 0 esquerda, bit 1 direita do direcional (ultimo scePadRead)

    bool ramOk(uint32_t a, uint32_t n) { return a >= 0x1000u && a + n <= PS2_RAM_SIZE; }
    uint16_t rd16(uint8_t *rdram, uint32_t a) { uint16_t v; std::memcpy(&v, rdram + a, 2); return v; }
    void wr16(uint8_t *rdram, uint32_t a, uint16_t v) { std::memcpy(rdram + a, &v, 2); }

    LockToggle *lockToggleFind(uint32_t pf, bool create)
    {
        for (LockToggle &e : g_lockToggle)
            if (e.pf == pf)
                return &e;
        if (create)
            for (LockToggle &e : g_lockToggle)
                if (!e.pf)
                {
                    e = LockToggle{};
                    e.pf = pf;
                    return &e;
                }
        return nullptr;
    }

    // O alvo travado (m_pinTarget, Monster +0x6C04, do quadro anterior) e um dos m_monsters do TheGame com m_dead (+0xE8) ou que
    // saiu da luta (m_playerNum 0)?
    bool lockTargetDead(uint8_t *rdram, uint32_t owner)
    {
        const uint32_t target = FAST_READ32(owner + kMonsterPinTarget);
        if (!target)
            return false;
        for (uint32_t i = 0; i < kMonsterSlots; ++i)
            if (FAST_READ32(kGame + 0x120380 + 4 * i) == target)
                return ramOk(target, kMonsterDead + 1u) &&
                       (static_cast<int8_t>(rdram[target + kMonsterDead]) != 0 || FAST_READ32(target + kMonsterPlayerNum) == 0u);
        return false;   // predio, peca de chefe etc.
    }

    // Estado do jogador humano dono deste PadFlags (nullptr para a IA)
    LockToggle *lockToggleFor(uint8_t *rdram, uint32_t pf, int *slot)
    {
        const uint32_t owner = FAST_READ32(pf + kPadFlagsOwner);
        // So jogador humano (m_playerNum 1). A IA (2, tambem o jogador sob o WOTM_AIVSAI) mira com L1+R1 (Ai::targetPin): remapeada,
        // o L1 virava defesa e o R1 travava e soltava, e ela ficava defendendo e dando counter sem parar
        if (!isPlayerMonster(rdram, owner, slot) || FAST_READ32(owner + 0x18u) != 1u)
            return nullptr;
        LockToggle *t = lockToggleFind(pf, true);
        if (t && t->owner != owner)
        {
            *t = LockToggle{};
            t->pf = pf;
            t->owner = owner;
        }
        return t;
    }

    float rdF(uint8_t *rdram, uint32_t a) { float v; std::memcpy(&v, rdram + a, 4); return v; }

    // Posicao (cs->trans) de um monstro ou de outro alvejavel (o _cs fica no +0xC dos dois)
    bool targetPos(uint8_t *rdram, uint32_t obj, float p[3])
    {
        if (!ramOk(obj, 0x10u))
            return false;
        const uint32_t cs = FAST_READ32(obj + kMonsterCs) & 0x1FFFFFFFu;
        if (!ramOk(cs, 0x20u))
            return false;
        for (int i = 0; i < 3; ++i)
            p[i] = rdF(rdram, cs + 0x10 + 4 * i);
        return true;
    }

    // m_monsters[i] serve de alvo para o dono: vivo e alvejavel como no getClosestTargetable(6); posicao em tp
    bool lockCandidate(uint8_t *rdram, uint32_t m, uint32_t owner, float tp[3])
    {
        if (!m || m == owner || !ramOk(m, kMonsterDying + 1u) || rdram[m + kMonsterDead] || rdram[m + kMonsterDying] ||
            FAST_READ32(m + kMonsterPlayerNum) == 0u)
            return false;
        return (rd16(rdram, m + 4) & 6u) == 6u && targetPos(rdram, m, tp);
    }

    // Ao travar: o alvo e o monstro vivo mais perto (distancia 3D). O getClosestTargetable do updateLock pesa tambem a direcao para
    // onde o monstro olha; com o m_pinTarget ja preenchido e alvejavel, o updateLock o mantem.
    void lockNearestTarget(uint8_t *rdram, uint32_t owner)
    {
        float me[3], tp[3];
        if (!targetPos(rdram, owner, me))
            return;
        uint32_t best = 0;
        float bestD = 0.f;
        for (uint32_t i = 0; i < kMonsterSlots; ++i)
        {
            const uint32_t m = FAST_READ32(kGame + 0x120380 + 4 * i);   // m_monsters[i]
            if (!lockCandidate(rdram, m, owner, tp))
                continue;
            const float d = (tp[0] - me[0]) * (tp[0] - me[0]) + (tp[1] - me[1]) * (tp[1] - me[1]) + (tp[2] - me[2]) * (tp[2] - me[2]);            if (!best || d < bestD)
            {
                best = m;
                bestD = d;
            }
        }
        FAST_WRITE32(owner + kMonsterPinTarget, best);   // sem candidato, 0: o updateLock escolhe (antes ficava o alvo velho)
    }

    // Troca o alvo travado para o monstro vivo mais proximo a esquerda (dir < 0) ou a direita (dir > 0) do atual na tela: angulo
    // horizontal visto da camera do jogador (eixos direita e frente do _cs da vista), dando a volta no fim.
    // Eixos direita e frente (no plano XY) da camera do jogador: _cs da vista dele (viewInfo[vista], mat em +0x20/+0x30)
    bool lockCameraAxes(uint8_t *rdram, uint32_t owner, float right[2], float fwd[2])
    {
        int32_t players = static_cast<int32_t>(FAST_READ32(kShell + 0x2BB4));   // m_numPlayers
        const int32_t view = players > 1 ? std::clamp<int32_t>(static_cast<int32_t>(FAST_READ32(owner + kMonsterCameraView)), 0, 4) : 0;
        const uint32_t cam = FAST_READ32(kViewInfo + 0x14u * view) & 0x1FFFFFFFu;   // _cs da vista
        if (!ramOk(cam, 0x40u))
            return false;
        for (int i = 0; i < 2; ++i)
        {
            right[i] = rdF(rdram, cam + 0x20 + 4 * i);
            fwd[i] = rdF(rdram, cam + 0x30 + 4 * i);
        }
        // olhando quase de cima, a frente no chao some: o "cima" da camera (+0x40) aponta para onde a tela sobe
        if (fwd[0] * fwd[0] + fwd[1] * fwd[1] < 0.09f)
            for (int i = 0; i < 2; ++i)
                fwd[i] = rdF(rdram, cam + 0x40 + 4 * i);
        return std::isfinite(right[0] + right[1] + fwd[0] + fwd[1]);
    }

    void lockCycleTarget(uint8_t *rdram, uint32_t owner, int slot, int dir)
    {
        const uint32_t cur = FAST_READ32(owner + kMonsterPinTarget);
        float me[3], tp[3], right[2], fwd[2];
        if (!cur || !targetPos(rdram, owner, me) || !lockCameraAxes(rdram, owner, right, fwd))
            return;
        auto screenAngle = [&](const float p[3]) {
            const float dx = p[0] - me[0], dy = p[1] - me[1];
            return std::atan2(dx * right[0] + dy * right[1], dx * fwd[0] + dy * fwd[1]);
        };
        const float curA = targetPos(rdram, cur, tp) ? screenAngle(tp) : 0.f;
        uint32_t best = 0, wrap = 0;
        float bestD = 1e9f, wrapA = 0.f;
        for (uint32_t i = 0; i < kMonsterSlots; ++i)
        {
            const uint32_t m = FAST_READ32(kGame + 0x120380 + 4 * i);   // m_monsters[i]
            if (m == cur || !lockCandidate(rdram, m, owner, tp))
                continue;
            const float a = screenAngle(tp), d = (a - curA) * static_cast<float>(dir);
            if (d > 0.f && d < bestD)
            {
                bestD = d;
                best = m;
            }
            if (!wrap || a * -static_cast<float>(dir) > wrapA * -static_cast<float>(dir))
            {
                wrap = m;   // o mais do lado oposto: a volta
                wrapA = a;
            }
        }
        if (!best)
            best = wrap;
        if (!best)
            return;
        FAST_WRITE32(owner + kMonsterPinTarget, best);
        std::fprintf(stderr, "[lockon] jogador %d: alvo a %s (0x%x)\n", slot + 1, dir < 0 ? "esquerda" : "direita", best);
    }

    void lockCameraStick(uint8_t *rdram, uint32_t owner, uint8_t *b);

    void interpretInputsLockToggle(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t pf = GPR_U32(ctx, 4) & 0x1FFFFFFFu, gp = GPR_U32(ctx, 5) & 0x1FFFFFFFu;
        // camera moderna (wotm_orbit.inc): configuracao relativa a tela e analogico direito para a orbita, antes do lock-on ler o +0x16E4
        if (ctx->pc == kInterpretInputs && ramOk(pf, kPadFlagsOwner + 4u))   // Mecha no laser: analogicos e mouse viram mira
            wotm_custom::laserInput(rdram, FAST_READ32(pf + kPadFlagsOwner), gp);
        const int orbitSlot = ctx->pc == kInterpretInputs ? orbit::inputEnter(rdram, pf, gp) : -1;
        // troca de alvo pedida pelo teclado (Tab, R, rodinha; wotm_mouse.hpp): com alvo travado, com ou sem o lock-on alternado
        if (int ps = -1; ctx->pc == kInterpretInputs && ramOk(pf, kPadFlagsOwner + 4u) &&
                         isPlayerMonster(rdram, FAST_READ32(pf + kPadFlagsOwner), &ps) && ps >= 0 && ps < 4)
            if (const int req = wotm::mouse::g_targetReq[ps].exchange(0, std::memory_order_relaxed); req != 0)
            {
                const uint32_t owner = FAST_READ32(pf + kPadFlagsOwner);
                if (FAST_READ32(owner + kMonsterPinTarget))
                    lockCycleTarget(rdram, owner, ps, req > 0 ? 1 : -1);   // proximo = o da direita na tela
            }
        LockToggle *t = nullptr;
        int slot = 0;
        if (ctx->pc == kInterpretInputs && wotm::settings::on(wotm::settings::LockToggle) && ramOk(pf, kPadFlagsOwner + 4u) && ramOk(gp, 0x18u))
            t = lockToggleFor(rdram, pf, &slot);
        if (t)
        {
            t->remap = FAST_READ32(pf + kPadFlagsConfig) == 0u;
            t->screenRel = FAST_READ32(pf + kPadFlagsScreenRel) != 0u;
        }
        if (!t || !t->remap)
        {
            const uint32_t ra0 = GPR_U32(ctx, 31);
            g_origInterpretInputs(rdram, ctx, runtime);
            if (ctx->pc == ra0)
                orbit::inputLeave(rdram, orbitSlot);
            return;
        }
        uint8_t *b = rdram + gp, saved[0x10];
        std::memcpy(saved, b, sizeof saved);
        t->lockHeld = saved[kPadR1] != 0;
        b[kPadL1] = saved[kPadL2];                // andar de lado: L2 e R2
        b[kPadR1] = saved[kPadR2];
        b[kPadR2] = saved[kPadL1] ? 0xFF : 0;     // defesa: L1
        b[kPadL2] = 0;
        const uint8_t dirs = slot < 4 ? g_padDir[slot] : 0;
        for (int i = 0; i < 2; ++i)
        {
            const bool held = (dirs >> i) & 1u;
            if (t->on && held)
            {
                b[kPadLeft] = b[kPadRight] = 0;   // travado, as setas trocam o alvo e nao movem
                if (!t->dirHeld[i])
                    lockCycleTarget(rdram, t->owner, slot, i ? 1 : -1);
            }
            t->dirHeld[i] = held;
        }
        if (t->on)
        {
            if (!t->screenRel)
                lockCameraStick(rdram, t->owner, b);
            // travado, o jogo anda de lado pela esquerda/direita (PadEntry +0x16/+0x18 -> +0x1A/+0x1C) e ignora os bytes 0/3
            if (saved[kPadL2])
                b[kPadLeft] = 0xFF;
            if (saved[kPadR2])
                b[kPadRight] = 0xFF;
        }
        const uint32_t ra = GPR_U32(ctx, 31);
        g_origInterpretInputs(rdram, ctx, runtime);
        if (ctx->pc == ra)   // fatia de tempo acabou no meio: o resto ainda le os bytes trocados (o loadPadInputs refaz todos)
        {
            std::memcpy(b, saved, sizeof saved);
            orbit::inputLeave(rdram, orbitSlot);
        }
    }

    // So na configuracao relativa ao monstro (updateInput0ConfigCR, PadFlags +0x16E4 = 0). Na relativa a tela (ConfigSR, +0x16E4 != 0;
    // so na vista unificada e no Crush: na vista normal o humano fica na CR, 75f5be1:docs/investigacoes/2026-10-22/camera_controle_cr_sr.md) o proprio jogo, travado, ja projeta o analogico da
    // camera (PadFlags +0x17E0) na direcao do alvo e na perpendicular; converter aqui antes fazia a conversao duas vezes, e o erro
    // era o angulo entre a camera e a direcao do alvo (para cima podia ir para tras ou de lado com a camera fora das costas).
    // Travado, o jogo move o monstro no referencial dele, virado para o alvo (cima/baixo = para o alvo e para tras, esquerda/direita =
    // de lado). Para o analogico (e o direcional) valer em relacao a camera, como solto: a direcao que ele aponta na tela vira um vetor
    // no chao pelos eixos da camera e e reescrita nos bytes cima/baixo/esquerda/direita do GamePad no referencial do monstro (frente =
    // para o alvo; direita com o mesmo sentido da direita da camera em relacao a frente dela). O tamanho se mantem.
    void lockCameraStick(uint8_t *rdram, uint32_t owner, uint8_t *b)
    {
        const float sx = (float(b[kPadRight]) - float(b[kPadLeft])) / 255.f, sy = (float(b[kPadUp]) - float(b[kPadDown])) / 255.f;
        if (std::fabs(sx) < 0.01f && std::fabs(sy) < 0.01f)
            return;
        const uint32_t target = FAST_READ32(owner + kMonsterPinTarget);
        float me[3], tp[3], cr[2], cf[2];
        if (!target || !targetPos(rdram, owner, me) || !targetPos(rdram, target, tp) || !lockCameraAxes(rdram, owner, cr, cf))
            return;
        auto norm = [](float v[2]) {
            const float l = std::sqrt(v[0] * v[0] + v[1] * v[1]);
            if (l < 1e-4f) return false;
            v[0] /= l;
            v[1] /= l;
            return true;
        };
        float mf[2] = {tp[0] - me[0], tp[1] - me[1]};
        if (!norm(cr) || !norm(cf) || !norm(mf))
            return;
        // perp(v) = (v.y, -v.x); a direita da camera e +perp(frente) ou -perp(frente) conforme o sentido do mundo
        const float hand = cr[0] * cf[1] - cr[1] * cf[0] >= 0.f ? 1.f : -1.f;
        const float mr[2] = {hand * mf[1], -hand * mf[0]};
        const float w[2] = {sx * cr[0] + sy * cf[0], sx * cr[1] + sy * cf[1]};
        const float lx = std::clamp(w[0] * mr[0] + w[1] * mr[1], -1.f, 1.f), ly = std::clamp(w[0] * mf[0] + w[1] * mf[1], -1.f, 1.f);
        b[kPadRight] = static_cast<uint8_t>(std::lround(std::max(lx, 0.f) * 255.f));
        b[kPadLeft] = static_cast<uint8_t>(std::lround(std::max(-lx, 0.f) * 255.f));
        b[kPadUp] = static_cast<uint8_t>(std::lround(std::max(ly, 0.f) * 255.f));
        b[kPadDown] = static_cast<uint8_t>(std::lround(std::max(-ly, 0.f) * 255.f));
    }

    void padReadLockToggle(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t port = GPR_U32(ctx, 4), slot = GPR_U32(ctx, 5), data = GPR_U32(ctx, 6) & 0x1FFFFFFFu;   // scePadRead(port, slot, rdata)
        g_origPadReadLock(rdram, ctx, runtime);
        if (port < 2 && slot < 2 && GPR_U32(ctx, 2) != 0u && ramOk(data, 32u))
        {
            const uint8_t btn = static_cast<uint8_t>(~rdram[data + 2]);   // ativos em 0: 0x80 esquerda, 0x20 direita
            g_padDir[port + 2 * slot] = static_cast<uint8_t>(((btn >> 7) & 1u) | (((btn >> 5) & 1u) << 1));
        }
    }

    void lockToggleApply(uint8_t *rdram, uint32_t pf, int config)
    {
        int slot = 0;
        LockToggle *t = lockToggleFor(rdram, pf, &slot);
        if (!t)
            return;
        const uint32_t owner = t->owner;
        const uint16_t a = rd16(rdram, pf + kPadFlagsLockA), b = rd16(rdram, pf + kPadFlagsLockB);
        const int32_t tweak = static_cast<int32_t>(FAST_READ32(pf + kPadFlagsHoldTweak));
        const uint16_t over = static_cast<uint16_t>(std::clamp<int32_t>(tweak + 1, 1, 0xFFFF));   // acima do limiar: trava
        const uint16_t under = static_cast<uint16_t>(std::clamp<int32_t>(tweak, 0, 0xFFFF));
        const bool remap = config == 0 && t->remap;
        const bool held = remap ? t->lockHeld : config == 0 ? (a > tweak && b > tweak) : a != 0;
        if (t->on && lockTargetDead(rdram, owner))
        {
            t->on = false;   // segurando, o updateLock passaria para o mais proximo; alternado, solta
            std::fprintf(stderr, "[lockon] jogador %d: solto (alvo morreu)\n", slot + 1);
        }
        if (held && !t->held)
        {
            t->on = !t->on;
            if (t->on)
                lockNearestTarget(rdram, owner);
            std::fprintf(stderr, "[lockon] jogador %d: %s (alvo 0x%x)\n", slot + 1, t->on ? "travado" : "solto", FAST_READ32(owner + kMonsterPinTarget));
        }
        t->held = held;
        t->realA = a;
        t->realB = b;
        if (config == 0)
        {
            if (t->on)
            {
                wr16(rdram, pf + kPadFlagsLockA, std::max(a, over));
                wr16(rdram, pf + kPadFlagsLockB, std::max(b, over));
            }
            else if (remap ? (a > tweak && b > tweak) : held)   // com os botoes do mod: L2+R2 juntos tambem nao travam
            {
                wr16(rdram, pf + kPadFlagsLockA, std::min(a, under));   // os dois juntos, abaixo do limiar: nem trava nem anda de lado
                wr16(rdram, pf + kPadFlagsLockB, std::min(b, under));
            }
        }
        else if (t->on)
            wr16(rdram, pf + kPadFlagsLockA, std::max<uint16_t>(a, 1));
        else
            wr16(rdram, pf + kPadFlagsLockA, 0);
        t->fakeA = rd16(rdram, pf + kPadFlagsLockA);
        t->fakeB = rd16(rdram, pf + kPadFlagsLockB);
        t->forged = t->fakeA != a || t->fakeB != b;
    }

    template <int N>
    void updateHistoryLockToggle(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t pf = GPR_U32(ctx, 4) & 0x1FFFFFFFu, ra = GPR_U32(ctx, 31);
        const bool ok = ctx->pc == kUpdateHistory[N] && ramOk(pf, kPadFlagsOwner + 4u);
        if (ok)
            if (LockToggle *t = lockToggleFind(pf, false); t && t->forged)
            {
                // o updateHistory conta a partir dos contadores reais (se alguem os zerou no meio, fica o que ele gravou)
                if (rd16(rdram, pf + kPadFlagsLockA) == t->fakeA && rd16(rdram, pf + kPadFlagsLockB) == t->fakeB)
                {
                    wr16(rdram, pf + kPadFlagsLockA, t->realA);
                    wr16(rdram, pf + kPadFlagsLockB, t->realB);
                }
                t->forged = false;
            }
        g_origUpdateHistory[N](rdram, ctx, runtime);
        if (ok && ctx->pc == ra)
        {
            if (wotm::settings::on(wotm::settings::LockToggle))
                lockToggleApply(rdram, pf, N);
            else if (LockToggle *t = lockToggleFind(pf, false); t && (t->on || t->held))
            {
                t->on = t->held = false;   // opcao desligada no meio da luta: solta e volta aos botoes do jogo
                t->remap = false;
            }
        }
    }
}

namespace
{
    // Barras de vida e energia sobre os inimigos (mod, WOTM_ENEMYBARS=1; grupo "QoL features" do launcher), no lugar do icone de vida (a bolinha
    // com o numero do jogador, que muda de cor com a vida). O Hud::drawHealthIcon(monstro, n) projeta a cabeca do monstro e manda dois
    // quadrados ao ScreenPolys::addPoly no mesmo ponto: o numero (branco) e o anel (cor do +0x686C/+0x6870). Aqui o gancho no
    // drawHealthIcon guarda o monstro, e o do addPoly, quando chamado de dentro dele, troca os dois quadrados por duas barras sem
    // textura (wotmBarRect, desenhadas com os retangulos de tela) centradas onde o primeiro ficaria: vida (m_health/m_maxHealth,
    // +0x44C/+0x448, com o dano recente em vermelho) e, embaixo, energia (StaminaMeter em +0x460: cur/max; cinza enquanto esgotada),
    // no estilo das barras do HUD do jogador. O resto do HUD nao muda.
    constexpr uint32_t kDrawHealthIcon = wotm::fn::drawHealthIcon__3HudP7Monsteri, kDrawHealthIconSize = 0x4F8u,
                       kAddPoly = wotm::fn::addPoly__11ScreenPolysiiiRQ211ScreenPolys8PolyData;
    constexpr uint32_t kMonsterMaxHealth = 0x448u, kMonsterHealth = 0x44Cu, kMonsterStamina = 0x460u;   // StaminaMeter: max +0, cur +0x10, exhausted +0x20
    constexpr uint32_t kXform2d = wotm::fn::mathfTransform3dTo2d__FiP8_fvectorT1;
    PS2Runtime::RecompiledFunction g_origDrawHealthIcon = nullptr, g_origAddPolyBars = nullptr, g_origXform2dBars = nullptr;
    uint32_t g_barMonster = 0;   // monstro do ultimo drawHealthIcon (0 = barras desligadas: o icone do jogo fica)
    bool g_barDone = false;      // as barras dele ja sairam (o segundo quadrado so some)
    float g_barRawX = 0.f;       // x projetado da cabeca (pixels do GS), antes do limite do drawHealthIcon
    bool g_barRawOk = false;
    float g_barWorld[3] = {};    // o ponto do mundo que o drawHealthIcon projetou (modo foto: barras e icone seguem a camera)
    bool g_anchorDone = true;    // o icone deste drawHealthIcon ja foi anotado (wotmHudAnchor)

    float barFrac(uint8_t *rdram, uint32_t cur, uint32_t max)
    {
        float c, m;
        std::memcpy(&c, rdram + cur, 4);
        std::memcpy(&m, rdram + max, 4);
        return m > 0.f && std::isfinite(c) ? std::clamp(c / m, 0.f, 1.f) : 0.f;
    }

    // Estilo do HUD do jogador (medido num print 720p): moldura preta, cinza (94) e preta; dentro, faixas de cor no sentido da
    // espessura (escuro nas bordas, brilho claro perto do meio). Vida verde com o dano recente em vermelho (brilho embaixo), energia
    // azul; o vazio escurece o fundo.
    constexpr uint8_t kHealthProf[][3] = {{20, 58, 2},    {43, 101, 16},  {81, 170, 39},  {96, 181, 53},  {169, 191, 148},
                                          {146, 191, 109}, {85, 178, 42},  {68, 146, 31},  {31, 79, 9}};
    constexpr uint8_t kLagProf[][3] = {{80, 28, 26}, {118, 40, 38}, {160, 56, 53}, {160, 56, 53}, {187, 103, 104}, {184, 95, 95}};
    constexpr uint8_t kEnergyProf[][3] = {{13, 23, 67},    {37, 55, 133},  {46, 67, 153},  {112, 127, 153}, {146, 150, 153},
                                          {85, 105, 153},  {46, 67, 153},  {28, 43, 112},  {7, 15, 51}};
    // Energia acima do maximo (ate o maxLevel, a furia): rosa por cima do azul (perfil medido num print da barra do jogador); com a furia
    // cheia (m_boostFull) a barra toda pisca entre roxo e rosa. Ritmo medido na barra do jogador (gravacao a 60 quadros/s com
    // WOTM_CHEATS=furia): ciclo de ~6 quadros, ~2 deles em rosa.
    constexpr uint8_t kOverProf[][3] = {{33, 4, 19},     {108, 19, 70},  {182, 33, 118}, {223, 40, 145}, {255, 56, 201},
                                        {255, 50, 175},  {182, 33, 118}, {119, 21, 77},  {38, 6, 24}};
    constexpr uint8_t kFuryDimProf[][3] = {{16, 10, 28},  {42, 22, 91},  {59, 35, 116}, {98, 43, 157}, {98, 55, 197},
                                           {87, 42, 178}, {51, 31, 115}, {39, 28, 82},  {14, 11, 30}};
    constexpr uint32_t kFuryBlinkPeriod = 6u, kFuryBlinkOn = 2u;   // quadros
    constexpr uint8_t kExhaustedProf[][3] = {{30, 30, 34},  {60, 60, 68},   {80, 80, 90},   {120, 120, 128}, {140, 140, 146},
                                             {100, 100, 110}, {80, 80, 90},  {55, 55, 62},   {25, 25, 30}};

    template <size_t N>
    void gradBar(float x0, float y0, float x1, float y1, const uint8_t (&prof)[N][3])
    {
        if (x1 <= x0)
            return;
        const float h = (y1 - y0) / static_cast<float>(N);
        for (size_t i = 0; i < N; ++i)
            wotmBarRect(x0, y0 + h * i, x1, y0 + h * (i + 1), prof[i][0], prof[i][1], prof[i][2], 0x80);
    }

    // Moldura de uma barra em volta do miolo (x0, y0)-(x1, y1); 1 unidade = 1,5 px em x e 3,2 px em y a 720p
    // Anel de espessura (tx, ty) por fora de (x0, y0)-(x1, y1)
    void barRing(float x0, float y0, float x1, float y1, float tx, float ty, uint8_t c)
    {
        wotmBarRect(x0 - tx, y0 - ty, x1 + tx, y0, c, c, c, 0x80);
        wotmBarRect(x0 - tx, y1, x1 + tx, y1 + ty, c, c, c, 0x80);
        wotmBarRect(x0 - tx, y0, x0, y1, c, c, c, 0x80);
        wotmBarRect(x1, y0, x1 + tx, y1, c, c, c, 0x80);
    }

    // Dano recente (o trecho vermelho): segura ~0,5 s e escoa ate a vida atual, por monstro
    struct BarLag
    {
        uint32_t m = 0;
        float lag = 0.f;
        int hold = 0;
    } g_barLag[16];

    float barLag(uint32_t m, float health)
    {
        BarLag *e = nullptr;
        for (BarLag &b : g_barLag)
            if (b.m == m)
                e = &b;
        if (!e)
            for (BarLag &b : g_barLag)
                if (!b.m)
                {
                    e = &b;
                    *e = BarLag{m, health, 0};
                    break;
                }
        if (!e)
            return health;
        if (health >= e->lag)
        {
            e->lag = health;
            e->hold = 0;
        }
        else if (++e->hold > 30)
            e->lag = std::max(health, e->lag - 0.006f);
        return e->lag;
    }

    // Retrato do HUD de cada monstro: a ultima entrada do .RTX dele (T8H 128x64; a cara normal na metade esquerda, a da furia na
    // direita). O TEX0 ja relocado mora no NGP do proprio monstro (objetos do retrato: TBP0 = fileStatus.maxResAddr do arquivo anterior,
    // CBP = maxTexAddr anterior + a paleta do retrato, que muda de lugar por monstro). O arquivo do monstro sai do no raiz dele
    // (Monster::m_cs -> _cs::epNode), que fica dentro do NGP; o TEX0 e procurado uma vez por arquivo carregado.
    struct PortraitCache
    {
        uint32_t lo = 0, hi = 0;
        uint64_t tex0 = 0;
    } g_portraits[16];
    unsigned g_portraitGen = 0;

    uint64_t monsterPortrait(uint8_t *rdram, uint32_t m)
    {
        if (g_portraitGen != g_loadGen)
        {
            for (PortraitCache &c : g_portraits)
                c = PortraitCache{};
            g_portraitGen = g_loadGen;
        }
        if (!ramOk(m, 0x10u))
            return 0;
        const uint32_t cs = FAST_READ32(m + 0xCu) & 0x1FFFFFFFu;
        if (!ramOk(cs, 4u))
            return 0;
        const uint32_t ep = FAST_READ32(cs) & 0x1FFFFFFFu;
        const uint32_t n = FAST_READ8(kFileStatus + 0x74);
        for (uint32_t i = 1; i < n && i < 13; ++i)
        {
            const uint32_t lo = FAST_READ32(kFileStatus + 0x3C + 4 * i), hi = FAST_READ32(kFileStatus + 0x3C + 4 * (i + 1));
            if (ep < lo || ep >= hi || !ramOk(lo, hi - lo))
                continue;
            for (const PortraitCache &c : g_portraits)
                if (c.lo == lo && c.hi == hi)
                    return c.tex0;
            const uint32_t resBase = FAST_READ16(kFileStatus + 0x168 + 2 * (i - 1));
            uint64_t found = 0;
            for (uint32_t a = (lo + 7u) & ~7u; a + 8u <= hi && !found; a += 8u)
            {
                uint64_t v;
                std::memcpy(&v, rdram + a, 8);
                const uint32_t psm = uint32_t(v >> 20) & 0x3F, tw = uint32_t(v >> 26) & 15, th = uint32_t(v >> 30) & 15;
                if (psm == 0x1B && tw == 7 && th == 6 && ((v >> 51) & 15) == 0 && (v & 0x3FFF) == resBase)
                    found = v;
            }
            PortraitCache *slot = &g_portraits[0];
            for (PortraitCache &c : g_portraits)
                if (!c.lo || c.lo >= c.hi)
                {
                    slot = &c;
                    break;
                }
            *slot = PortraitCache{lo, hi, found};
            std::fprintf(stderr, "[wotm] retrato do arquivo %u (NGP 0x%07X): %s\n", i, lo, found ? "achado" : "nao achado");
            return found;
        }
        return 0;
    }

    // Botao do especial ligado: o vermelho do icone do HUD do jogador (medido num print com WOTM_CHEATS=esp), em faixas como as barras
    constexpr uint8_t kSpecialProf[][3] = {{106, 36, 34}, {156, 53, 50}, {196, 68, 65}, {222, 91, 88}, {235, 112, 111},
                                           {222, 91, 88}, {196, 68, 65}, {156, 53, 50}, {106, 36, 34}};

    // Um bloco por inimigo: retrato a esquerda na altura toda; a direita, vida em cima e, embaixo, o especial e a energia (mais fina e
    // mais curta). Cada celula tem o proprio contorno preto; a faixa cinza entre duas celulas e uma so (o vao entre elas e exatamente
    // preto + cinza + preto), e um contorno preto fecha o bloco. Unidades da tela do jogo, 640 x 224 (1 em y = 2,14 em x na tela).
    void emitBars(uint8_t *rdram, uint32_t m, const float box[4])
    {
        if (!ramOk(m, kMonsterStamina + 0x24u))
            return;
        const float health = barFrac(rdram, m + kMonsterHealth, m + kMonsterMaxHealth);
        const float energy = barFrac(rdram, m + kMonsterStamina + 0x10u, m + kMonsterStamina);
        const bool exhausted = FAST_READ32(m + kMonsterStamina + 0x20u) != 0u;
        const bool fury = static_cast<int8_t>(FAST_READ8(m + 0xF4u)) != 0;   // m_boostFull: o Hud::update troca a cara do retrato por ele
        const bool special = FAST_READ8(m + 0xF3u) != 0 && FAST_READ32((FAST_READ32(wotm::sym::game) & 0x1FFFFFFFu) + 0x1203C8u) != 9u;   // Monster::isSpecialAvailable (TheGame::m_gameMode)
        const float lag = barLag(m, health);
        constexpr float kOx = 0.7f, kOy = 0.3f, kGx = 1.3f, kGy = 0.6f, kIx = 1.0f, kIy = 0.45f;   // contorno: preto, cinza, preto
        constexpr float kDx = 2.f * kIx + kGx, kDy = 2.f * kIy + kGy;                              // vao entre duas celulas
        constexpr float kAspect = 480.f / 224.f;                                                    // 1 unidade em y em unidades de x
        constexpr float kW = 44.f, kHealthH = 2.6f, kEnergyH = 1.6f;
        constexpr float kRowsH = kHealthH + kDy + kEnergyH, kPortraitW = kRowsH * kAspect, kSpecialW = kEnergyH * kAspect;
        const float cx = 0.5f * (box[0] + box[2]), cy = 0.5f * (box[1] + box[3]);
        const float bx0 = cx - 0.5f * (kPortraitW + kDx + kW), ty0 = cy - 0.5f * kRowsH;
        struct Cell { float x0, y0, x1, y1; };
        const float hx0 = bx0 + kPortraitW + kDx, sy0 = ty0 + kHealthH + kDy;
        const Cell portrait{bx0, ty0, bx0 + kPortraitW, ty0 + kRowsH};
        const Cell hp{hx0, ty0, hx0 + kW, ty0 + kHealthH};
        const Cell sp{hx0, sy0, hx0 + kSpecialW, sy0 + kEnergyH};
        const Cell st{hx0 + kSpecialW + kDx, sy0, hx0 + kW, sy0 + kEnergyH};
        const Cell cells[4] = {portrait, hp, sp, st};
        barRing(bx0 - kIx - kGx, ty0 - kIy - kGy, hx0 + kW + kIx + kGx, ty0 + kRowsH + kIy + kGy, kOx, kOy, 0);
        for (const Cell &c : cells)   // o cinza de todas antes do preto: o cinza de uma nao cobre o contorno da vizinha
            barRing(c.x0 - kIx, c.y0 - kIy, c.x1 + kIx, c.y1 + kIy, kGx, kGy, 94);
        for (const Cell &c : cells)
        {
            barRing(c.x0, c.y0, c.x1, c.y1, kIx, kIy, 0);
            wotmBarRect(c.x0, c.y0, c.x1, c.y1, 0, 0, 0, 0x40);   // vazio: o fundo escurecido
        }
        if (const uint64_t t = monsterPortrait(rdram, m))
            wotmBarTex(portrait.x0, portrait.y0, portrait.x1, portrait.y1, t, fury ? 64.f : 0.f, 64.f, fury ? 128.f : 64.f, 0.f);   // V de baixo para cima, como o polyBuildPrim (0x80 - v)
        gradBar(hp.x0 + kW * health, hp.y0, hp.x0 + kW * lag, hp.y1, kLagProf);
        gradBar(hp.x0, hp.y0, hp.x0 + kW * health, hp.y1, kHealthProf);
        if (special)
            gradBar(sp.x0, sp.y0, sp.x1, sp.y1, kSpecialProf);
        const float stW = st.x1 - st.x0;
        if (exhausted)
            gradBar(st.x0, st.y0, st.x0 + stW * energy, st.y1, kExhaustedProf);
        else
        {
            gradBar(st.x0, st.y0, st.x0 + stW * energy, st.y1, kEnergyProf);
            // o que passa do maximo, como no Hud::update: (cur - max) / (maxLevel - max)
            float cur, max, top;
            std::memcpy(&max, rdram + m + kMonsterStamina, 4);
            std::memcpy(&top, rdram + m + kMonsterStamina + 0x8u, 4);
            std::memcpy(&cur, rdram + m + kMonsterStamina + 0x10u, 4);
            const float over = top > max && std::isfinite(cur) ? std::clamp((cur - max) / (top - max), 0.f, 1.f) : 0.f;
            if (fury)
                gradBar(st.x0, st.y0, st.x1, st.y1,
                        FAST_READ32(wotm::sym::g_frame) % kFuryBlinkPeriod < kFuryBlinkOn ? kOverProf : kFuryDimProf);
            else if (over > 0.f)
                gradBar(st.x0, st.y0, st.x0 + stW * over, st.y1, kOverProf);
        }
    }

    void drawHealthIconBars(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kDrawHealthIcon)
        {
            // com WOTM_ENEMYBARS sempre; sem ela, so na luta de 3 ou 4 jogadores (tela em quadrantes, wotm_split4.inc)
            // opcao ligada: barras em toda luta (sem ela, so no multijogador de 3 e 4)
            const bool on = wotm::settings::on(wotm::settings::EnemyBars) || wotm_split4::splitPlayers(rdram);
            g_barMonster = on ? GPR_U32(ctx, 5) & 0x1FFFFFFFu : 0u;
            g_barDone = false;
            g_barRawOk = false;
            g_anchorDone = false;
        }
        g_origDrawHealthIcon(rdram, ctx, runtime);
        g_anchorDone = true;
        wotmHudAnchorEnd();
    }

    // mathfTransform3dTo2d(vista, ponto, saida) chamado do drawHealthIcon: guarda o x antes do limite da vista 4:3
    void xform2dBars(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t ra = GPR_U32(ctx, 31), out = GPR_U32(ctx, 6) & 0x1FFFFFFFu, in = GPR_U32(ctx, 5) & 0x1FFFFFFFu;
        const bool mine = ctx->pc == kXform2d && ra >= kDrawHealthIcon && ra < kDrawHealthIcon + kDrawHealthIconSize && ramOk(out, 8u) &&
                          ramOk(in, 12u);
        float world[3] = {};
        if (mine)
            std::memcpy(world, rdram + in, 12);   // antes da chamada: a saida pode ser o mesmo vetor
        g_origXform2dBars(rdram, ctx, runtime);
        if (mine && ctx->pc == ra)
        {
            std::memcpy(&g_barRawX, rdram + out, 4);
            std::memcpy(g_barWorld, world, 12);
            g_barRawOk = true;
        }
    }

    void addPolyBars(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t ra = GPR_U32(ctx, 31);
        const bool fromIcon = ctx->pc == kAddPoly && ra >= kDrawHealthIcon && ra < kDrawHealthIcon + kDrawHealthIconSize;
        if (!fromIcon || !g_barMonster)
        {
            // icone do jogo (sem barras): anota o centro e o ponto do mundo, para o modo foto o levar junto com a camera
            float box[4];
            if (fromIcon && !g_anchorDone && g_barRawOk &&
                wotmScreenPolyBox(rdram, static_cast<int>(GPR_U32(ctx, 4)), static_cast<int>(GPR_U32(ctx, 5)), static_cast<int>(GPR_U32(ctx, 6)),
                                  GPR_U32(ctx, 7), box))
                wotmHudAnchor(rdram, 0.5f * (box[0] + box[2]), 0.5f * (box[1] + box[3]), g_barWorld, false);
            if (fromIcon) g_anchorDone = true;
            g_origAddPolyBars(rdram, ctx, runtime);
            return;
        }
        float box[4];
        if (!g_barDone && wotmScreenPolyBox(rdram, static_cast<int>(GPR_U32(ctx, 4)), static_cast<int>(GPR_U32(ctx, 5)),
                                            static_cast<int>(GPR_U32(ctx, 6)), GPR_U32(ctx, 7), box))
        {
            // limite do x na largura widescreen da vista (e a vista dos retangulos); sem o x cru fica o do jogo. Nos quadrantes o x cru
            // vem fora da vista (abaixo) e o deslocamento saia errado: fica so a vista dos retangulos (o desenho nativo poe a coluna)
            const float shift = wotmHealthIconShift(rdram, g_barRawOk ? g_barRawX : std::nanf(""));
            const float dx = wotm_split4::splitPlayers(rdram) ? 0.f : shift;
            box[0] += dx;
            box[2] += dx;
            // tela em quadrantes: nas vistas de baixo o y do icone vem com o deslocamento do quadrante duas vezes (como o x da vista 1, que
            // o desenho nativo compensa), abaixo da tela (~228 de 224): volta a altura do quadrante. Depois o bloco (~4,2 linhas para
            // cada lado do centro) fica dentro da faixa da vista (viewDef: y +0xE, altura +6), como o icone do jogo na borda
            if (wotm_split4::splitPlayers(rdram))
            {
                const uint32_t view = std::min<uint32_t>(FAST_READ32(wotm::sym::viewCurView), 3u);
                const uint32_t def = FAST_READ32(wotm::sym::viewInfo + 0x14u * view + 4u) & 0x1FFFFFFFu;
                if (ramOk(def, 0x10u))
                {
                    constexpr float kHalf = 5.f;
                    const float y0 = FAST_READ16(def + 0xE), y1 = y0 + FAST_READ16(def + 6);
                    const float cy = 0.5f * (box[1] + box[3]) - y0;   // y0 = 0 em cima, a altura do quadrante embaixo
                    const float dy = std::clamp(cy, y0 + kHalf, y1 - kHalf) - 0.5f * (box[1] + box[3]);
                    box[1] += dy;
                    box[3] += dy;
                    // o x da coluna da direita vem com os 320 da coluna duas vezes tambem (~805): volta a coluna (x da vista, +0xC)
                    const float x0 = FAST_READ16(def + 0xC);
                    box[0] -= x0;
                    box[2] -= x0;
                }
            }
            wotmHudAnchor(rdram, 0.5f * (box[0] + box[2]), 0.5f * (box[1] + box[3]), g_barRawOk ? g_barWorld : nullptr, true);
            g_anchorDone = true;
            emitBars(rdram, g_barMonster, box);
            wotmHudAnchorEnd();
        }
        g_barDone = true;
        ctx->pc = ra;   // o quadrado do icone nao e montado
    }

    // Faixas de cor no sentido da largura (barra em pe)
    template <size_t N>
    void gradBarV(float x0, float y0, float x1, float y1, const uint8_t (&prof)[N][3])
    {
        if (y1 <= y0)
            return;
        const float w = (x1 - x0) / static_cast<float>(N);
        for (size_t i = 0; i < N; ++i)
            wotmBarRect(x0 + w * i, y0, x0 + w * (i + 1), y1, prof[i][0], prof[i][1], prof[i][2], 0x80);
    }
}

// HUD de um jogador desenhado pela camada nativa (jogadores 3 e 4 da tela em quadrantes, wotm_split4.inc, hudUpdate), no desenho do
// HUD do jogo: retrato (a cara da furia com m_boostFull) e, a direita, a vida (com o dano recente em vermelho) e o botao do especial;
// embaixo do retrato, a stamina em pe, de baixo para cima (o que passa do maximo em rosa; com a furia cheia pisca). As caixas sao as
// do HUD do P1 em tela cheia (wotm_training::hud, 640 x 224) levadas a (ox + x * sx, oy + y * sy), espelhadas (mirror: retrato a
// direita, como o HUD do P2) antes da escala. `view` e a vista dos retangulos (a metade ou a faixa da janela em que saem).
void wotmPlayerHud(uint8_t *rdram, uint32_t m, int view, float ox, float oy, float sx, float sy, bool mirror)
{
    namespace h = wotm_training::hud;
    if (!ramOk(m, kMonsterStamina + 0x24u))
        return;
    wotmBarView(view);
    wotmHudAnchorEnd();
    wotmBarHud(true);
    struct HudOff { ~HudOff() { wotmBarHud(false); } } hudOff;   // em todas as saidas
    struct Cell { float x0, y0, x1, y1; };
    const auto map = [&](float x0, float y0, float x1, float y1) {
        if (mirror)
        {
            const float t = 640.f - x0;
            x0 = 640.f - x1;
            x1 = t;
        }
        return Cell{ox + x0 * sx, oy + y0 * sy, ox + x1 * sx, oy + y1 * sy};
    };
    const auto cell = [&](const h::Box &b) { return map(b.x0, b.y0, b.x1, b.y1); };
    // contorno de cada caixa: preto, cinza e preto, cada um com ~1,3 x 0,6 unidades da tela cheia
    const float tx = 1.3f * sx, ty = 0.6f * sy;
    const Cell boxes[4] = {cell(h::kPortrait), cell(h::kHealth), cell(h::kSpecial), cell(h::kStamina)};
    for (const Cell &c : boxes)
    {
        barRing(c.x0 - 2.f * tx, c.y0 - 2.f * ty, c.x1 + 2.f * tx, c.y1 + 2.f * ty, tx, ty, 0);
        barRing(c.x0 - tx, c.y0 - ty, c.x1 + tx, c.y1 + ty, tx, ty, 94);
        barRing(c.x0, c.y0, c.x1, c.y1, tx, ty, 0);
        wotmBarRect(c.x0, c.y0, c.x1, c.y1, 0, 0, 0, 0x50);   // vazio: o fundo escurecido
    }
    const float health = barFrac(rdram, m + kMonsterHealth, m + kMonsterMaxHealth);
    const float energy = barFrac(rdram, m + kMonsterStamina + 0x10u, m + kMonsterStamina);
    const bool exhausted = FAST_READ32(m + kMonsterStamina + 0x20u) != 0u;
    const bool fury = static_cast<int8_t>(FAST_READ8(m + 0xF4u)) != 0;   // m_boostFull
    const bool special = FAST_READ8(m + 0xF3u) != 0 && FAST_READ32((FAST_READ32(wotm::sym::game) & 0x1FFFFFFFu) + 0x1203C8u) != 9u;   // Monster::isSpecialAvailable
    const float lag = barLag(m, health);
    if (const uint64_t t = monsterPortrait(rdram, m))
    {
        const Cell &p = boxes[0];
        wotmBarTex(p.x0, p.y0, p.x1, p.y1, t, fury ? 64.f : 0.f, 64.f, fury ? 128.f : 64.f, 0.f);
    }
    // vida: cheia a partir do lado do retrato
    const float hw = h::kHealth.x1 - h::kHealth.x0;
    {
        const Cell l = map(h::kHealth.x0 + hw * health, h::kHealth.y0, h::kHealth.x0 + hw * lag, h::kHealth.y1);
        if (lag > health)
            gradBar(l.x0, l.y0, l.x1, l.y1, kLagProf);
        const Cell f = map(h::kHealth.x0, h::kHealth.y0, h::kHealth.x0 + hw * health, h::kHealth.y1);
        if (health > 0.f)
            gradBar(f.x0, f.y0, f.x1, f.y1, kHealthProf);
    }
    if (special)
        gradBar(boxes[2].x0, boxes[2].y0, boxes[2].x1, boxes[2].y1, kSpecialProf);
    // stamina: de baixo para cima
    const float sh = h::kStamina.y1 - h::kStamina.y0;
    const auto stamina = [&](float frac) { return map(h::kStamina.x0, h::kStamina.y1 - sh * frac, h::kStamina.x1, h::kStamina.y1); };
    if (exhausted)
    {
        const Cell c = stamina(energy);
        gradBarV(c.x0, c.y0, c.x1, c.y1, kExhaustedProf);
        return;
    }
    const Cell e = stamina(energy);
    gradBarV(e.x0, e.y0, e.x1, e.y1, kEnergyProf);
    float cur, max, top;
    std::memcpy(&max, rdram + m + kMonsterStamina, 4);
    std::memcpy(&top, rdram + m + kMonsterStamina + 0x8u, 4);
    std::memcpy(&cur, rdram + m + kMonsterStamina + 0x10u, 4);
    const float over = top > max && std::isfinite(cur) ? std::clamp((cur - max) / (top - max), 0.f, 1.f) : 0.f;
    if (fury)
    {
        const Cell c = stamina(1.f);
        gradBarV(c.x0, c.y0, c.x1, c.y1, FAST_READ32(wotm::sym::g_frame) % kFuryBlinkPeriod < kFuryBlinkOn ? kOverProf : kFuryDimProf);
    }
    else if (over > 0.f)
    {
        const Cell c = stamina(over);
        gradBarV(c.x0, c.y0, c.x1, c.y1, kOverProf);
    }
}

namespace
{
    // Numeros de dano (mod, WOTM_DAMAGEPOP=1; grupo "QoL features" do launcher). A vida de cada monstro (m_health, +0x44C) e conferida na
    // entrada do Monster::update: o que caiu desde a anterior vira um numero (wotmDamagePopup), o que pega tambem fogo, choque e
    // queda, que nao passam pelo takeHit. O tipo vem do ultimo Monster::takeHit da vitima (ate 2 quadros antes), pelo
    // HitEvent::s_takeHitInfo que o atacante montou: source 1, 5, 6, 12, 14 e 0x14 = especial (Weapon, FireBreath, StateZapAttack,
    // StateSonicRoar, StateGrappleHook, StateUltraTazer, StateTazerAttack, StateTopSpin; 6 e 0x14 sao os que o StateBlock trata junto
    // do 5, e o especial do Togera chega com 6), 8 e 13 = arremesso (MonsterState::handleCollis de objeto em voo, StateThrowBack de
    // monstro jogado; vale ate 45 quadros, o dano da batida no cenario vem depois), o resto = golpe. Lugar: o ponto do golpe
    // (s_takeHitInfo.pos) quando houver, senao o monstro um pouco acima. Nada sai durante uma cena (ActCinema: shell->m_inCinema,
    // moviePlaying) nem nos primeiros 30 updates de cada monstro na luta, quando o jogo ainda acerta a vida inicial; o rtMain(first)
    // esquece os monstros da luta anterior (o g_frame nao serve: o rtMain o zera tambem na volta da pausa).
    constexpr uint32_t kMonsterUpdateDmg = wotm::fn::update__7Monster, kTakeHitDmg = wotm::fn::takeHit__7MonsterP8_fvectorfi;
    constexpr uint32_t kTakeHitInfo = wotm::sym::x_8HitEvent_s_takeHitInfo;   // HitEvent: pos* +0, source +0x10
    PS2Runtime::RecompiledFunction g_origMonsterUpdateDmg = nullptr, g_origTakeHitDmg = nullptr;
    struct DamageTrack
    {
        uint32_t m = 0;
        float last = 0.f;
        int kind = 0;
        uint32_t hitFrame = 0;
        bool havePos = false;
        float pos[3] = {};
        uint32_t seen = 0;   // updates desde que o monstro entrou na fase
    } g_damageTrack[16];
    std::atomic<int64_t> g_damageUpdateMs{0};   // relogio (steady_clock) do ultimo Monster::update: parado = jogo pausado

    // Um dos m_monsters do TheGame (lugar para 16)
    bool isMonsterSlot(uint8_t *rdram, uint32_t m)
    {
        for (uint32_t i = 0; i < 16; ++i)
            if (m && (FAST_READ32(kGame + 0x120380 + 4 * i) & 0x1FFFFFFFu) == m)
                return true;
        return false;
    }

    DamageTrack *damageTrack(uint8_t *rdram, uint32_t m)
    {
        for (DamageTrack &d : g_damageTrack)
            if (d.m == m)
                return &d;
        for (DamageTrack &d : g_damageTrack)
            if (!d.m || !isMonsterSlot(rdram, d.m))
            {
                d = DamageTrack{};
                d.m = m;
                std::memcpy(&d.last, rdram + m + 0x44Cu, 4);
                return &d;
            }
        return nullptr;
    }

    int damageKind(int source)
    {
        switch (source)
        {
        case 1: case 5: case 6: case 12: case 14: case 20: return 2;
        case 8: case 13: return 1;
        default: return 0;
        }
    }

    void takeHitDamage(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t m = GPR_U32(ctx, 4) & 0x1FFFFFFFu;
        if (ctx->pc == kTakeHitDmg && ramOk(m, 0x450u) && isMonsterSlot(rdram, m))
            if (DamageTrack *d = damageTrack(rdram, m))
            {
                d->kind = damageKind(static_cast<int32_t>(FAST_READ32(kTakeHitInfo + 0x10u)));
                d->hitFrame = FAST_READ32(wotm::sym::g_frame);
                const uint32_t p = FAST_READ32(kTakeHitInfo) & 0x1FFFFFFFu;
                d->havePos = ramOk(p, 0x10u);
                if (d->havePos)
                    for (int i = 0; i < 3; ++i)
                        d->pos[i] = rdF(rdram, p + 4 * i);
                d->havePos = d->havePos && std::isfinite(d->pos[0]) && std::isfinite(d->pos[1]) && std::isfinite(d->pos[2]);
            }
        g_origTakeHitDmg(rdram, ctx, runtime);
    }

    void monsterUpdateDamage(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t m = GPR_U32(ctx, 4) & 0x1FFFFFFFu;
        if (ctx->pc == kMonsterUpdateDmg && ramOk(m, 0x450u) && isMonsterSlot(rdram, m))
        {
            if (g_damageNewFight)
            {
                g_damageNewFight = false;
                for (DamageTrack &t : g_damageTrack)
                    t = DamageTrack{};
            }
            g_damageUpdateMs.store(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(),
                                   std::memory_order_relaxed);
            if (DamageTrack *d = damageTrack(rdram, m))
            {
                const uint32_t sh = FAST_READ32(wotm::sym::shell) & 0x1FFFFFFFu;
                const bool cinema = FAST_READ32(wotm::sym::moviePlaying) != 0u || (ramOk(sh, 0x2BA8u) && FAST_READ32(sh + 0x2BA4u) != 0u);
                const bool quiet = cinema || d->seen < 30u;
                if (d->seen < 30u)
                    ++d->seen;
                float h;
                std::memcpy(&h, rdram + m + 0x44Cu, 4);
                if (std::isfinite(h))
                {
                    const float lost = d->last - h;
                    if (lost >= 0.05f)
                    {
                        const uint32_t g = FAST_READ32(wotm::sym::g_frame);
                        // o arremessado leva o dano ao bater no cenario, alguns quadros depois do takeHit que o lancou
                        const bool recent = d->hitFrame && g - d->hitFrame <= (d->kind == 1 ? 45u : 2u);
                        float p[3];
                        if (recent && d->havePos)
                            std::memcpy(p, d->pos, sizeof p);
                        else if (targetPos(rdram, m, p))
                            p[2] += 60.f;   // sem ponto de golpe: um pouco acima da origem do monstro
                        else
                            p[0] = p[1] = p[2] = 0.f;
                        if (!quiet && (p[0] != 0.f || p[1] != 0.f || p[2] != 0.f) &&
                            (wotm::settings::on(wotm::settings::DamagePop) || wotm_training::g_on.load(std::memory_order_relaxed)))
                            wotmDamagePopup(rdram, p, lost, recent ? d->kind : 0);
                    }
                    d->last = h;
                }
            }
        }
        g_origMonsterUpdateDmg(rdram, ctx, runtime);
    }
}

// Janela (drawDamagePopups): ha quanto tempo o jogo nao roda um Monster::update. Na pausa da luta, nos dialogos e no F7 eles param.
double wotmDamageIdleSeconds()
{
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    return double(now - g_damageUpdateMs.load(std::memory_order_relaxed)) / 1000.0;
}

void wotmInstallOverrides(PS2Runtime &runtime, const std::string &elfPath)
{
    const size_t slash = elfPath.find_last_of("/\\");
    const std::string name = slash == std::string::npos ? elfPath : elfPath.substr(slash + 1);
    if (name.find("SCUS_971.97") == std::string::npos)
        return;

    wotmModsInstall(runtime, elfPath);
    if (const char *e = std::getenv("WOTM_RATEMIN"))
        g_minRate = std::atoi(e);
    if (std::getenv("WOTM_TURNLOG") && (g_origUpdatePosition = runtime.lookupFunction(kMonsterDynamicsUpdatePosition)))
    {
        runtime.registerFunction(kMonsterDynamicsUpdatePosition, &updatePositionLogged);
        std::fprintf(stderr, "[wotm] log da rotacao dos monstros ligado (WOTM_TURNLOG)\n");
    }
    if (std::getenv("WOTM_FIELDLOG"))
    {
        g_fieldLog = g_wotmCycleProf = true;
        g_lastHost = std::chrono::steady_clock::now();
        std::fprintf(stderr, "[wotm] log de fields por quadro ligado (WOTM_FIELDLOG)\n");
    }
    if (g_minRate > 0 || g_fieldLog)
        runtime.registerFunction(kTimerGetUpdateRate, &timerGetUpdateRate);
    if ((g_origSyncV = runtime.lookupFunction(kSceGsSyncV)))
        runtime.registerFunction(kSceGsSyncV, &syncVTapped);
    g_nativeTimer = std::getenv("WOTM_TIMER_EMU") == nullptr;
#if !PS2X_HW_EMU
    if (!g_nativeTimer)   // o wotm.exe nao tem os timers do EE (FRONTEIRA.md, passo 8): o T1 emulado ficaria parado
    {
        std::fprintf(stderr, "[wotm] WOTM_TIMER_EMU ignorada: o wotm.exe nao tem os timers do EE (use o ps2EntryRunner)\n");
        g_nativeTimer = true;
    }
#endif
    if (std::getenv("WOTM_30HZ") == nullptr && (g_origTimerInit = runtime.lookupFunction(kTimerInit)))
        runtime.registerFunction(kTimerInit, &timerInit60);
    if ((g_origWaitMinRate = runtime.lookupFunction(kTimerWaitForMinUpdateRate)) &&
        (g_origGetCurTics = runtime.lookupFunction(kTimerGetCurTics)) &&
        (g_origTimer1 = runtime.lookupFunction(kTimer1Handler)))
    {
        runtime.registerFunction(kTimerWaitForMinUpdateRate, &waitMinRateByVblanks);
        if (g_nativeTimer)
        {
            runtime.registerFunction(kTimerGetCurTics, &getCurTicsNative);
            runtime.registerFunction(kTimer1Handler, &timer1HandlerNative);
            std::fprintf(stderr, "[wotm] timer do jogo pelo relogio do host (WOTM_TIMER_EMU=1 volta ao timer 1 emulado)\n");
        }
        else
            runtime.registerFunction(kTimerGetCurTics, &getCurTicsMarked);
    }
    else
        g_nativeTimer = false;
    if ((g_origRtFade = runtime.lookupFunction(kRtFadeScreen)))
        runtime.registerFunction(kRtFadeScreen, &rtFadeScreenTapped);
    if ((g_origGameFadeOut = runtime.lookupFunction(kTheGameFadeOut)))
        runtime.registerFunction(kTheGameFadeOut, &theGameFadeTapped<kTheGameFadeOut, &g_origGameFadeOut>);
    if ((g_origGameFadeOutAndIn = runtime.lookupFunction(kTheGameFadeOutAndIn)))
        runtime.registerFunction(kTheGameFadeOutAndIn, &theGameFadeTapped<kTheGameFadeOutAndIn, &g_origGameFadeOutAndIn>);
    if ((g_origDmaGsPktQueue = runtime.lookupFunction(kDmaGsPktQueue)))
        runtime.registerFunction(kDmaGsPktQueue, &dmaGsPktQueueTapped);
    if ((g_origFontDma = runtime.lookupFunction(kFontDmaFontData)))
        runtime.registerFunction(kFontDmaFontData, &fontDmaTapped);
    if ((g_origFontInitPacket = runtime.lookupFunction(kFontInitPacket)))
        runtime.registerFunction(kFontInitPacket, &fontInitPacketTapped);
    if ((g_origFontSetTexId = runtime.lookupFunction(kFontSetTexId)))
        runtime.registerFunction(kFontSetTexId, &fontSetTexIdTapped);
    if ((g_origFontColor = runtime.lookupFunction(kFontSetColorGifTag)))
        runtime.registerFunction(kFontSetColorGifTag, &fontColorTapped);
    if ((g_origFontGlyph = runtime.lookupFunction(kFontBuildPrim)))
        runtime.registerFunction(kFontBuildPrim, &fontGlyphTapped);
    if ((g_origUiFade = runtime.lookupFunction(kUiFadeScreen)))
        runtime.registerFunction(kUiFadeScreen, &uiFadeScreenTapped);
    if ((g_origThreeMileFlash = runtime.lookupFunction(kThreeMileDrawFlash)))
        runtime.registerFunction(kThreeMileDrawFlash, &threeMileFlashTapped);
    if ((g_origLetterbox = runtime.lookupFunction(kLetterboxDmaData)))
        runtime.registerFunction(kLetterboxDmaData, &letterboxTapped);
    if ((g_origShellFade = runtime.lookupFunction(kShellFadeScreen)))
        runtime.registerFunction(kShellFadeScreen, &shellFadeScreenTapped);
    if ((g_origLoadLevelFiles = runtime.lookupFunction(kLoadLevelFiles)) && (g_origRtMain = runtime.lookupFunction(kRtMain)))
    {
        runtime.registerFunction(kLoadLevelFiles, &loadLevelFilesTapped);
        runtime.registerFunction(kRtMain, &rtMainTapped);
    }
    {
        const char *h = std::getenv("WOTM_HIGHRAM");
        if (!(h && h[0] == '0') && (g_origLoadMonstersDB = runtime.lookupFunction(kLoadMonstersDB)))
            runtime.registerFunction(kLoadMonstersDB, &loadMonstersDBHigh);
        else
            std::fprintf(stderr, "[wotm] monstros carregados embaixo de 32 MB, como no PS2 (WOTM_HIGHRAM=0)\n");
    }
    {   // opcoes do jogador (wotm_settings.hpp): settings.ini ao lado do save.json (mesma pasta que o wotm_save::initPaths acha)
        const PS2Runtime::IoPaths &io = PS2Runtime::getIoPaths();
        std::filesystem::path root = io.mcRoot;
        if (root.empty())
            root = !io.elfDirectory.empty() ? io.elfDirectory / "mc0" : std::filesystem::current_path() / "mc0";
        wotm::settings::load(root.lexically_normal().parent_path());
    }
    wotm_save::install(runtime);
    if ((g_origEmitMatrices = runtime.lookupFunction(kEmitMatrices)))   // emissor que teleporta (8 monstros com o Agamo)
        runtime.registerFunction(kEmitMatrices, &emitMatricesGuarded);
    if ((g_origParticleEmit = runtime.lookupFunction(kParticleEmit)))
        runtime.registerFunction(kParticleEmit, &particleEmitGuarded);
    if ((g_origLosCs = runtime.lookupFunction(kLosCs)) && (g_origMathfRand = runtime.lookupFunction(kMathfRand)))   // raio em cadeia com 8
    {
        runtime.registerFunction(kLosCs, &losCsTapped);
        runtime.registerFunction(kMathfRand, &mathfRandTapped);
    }
    wotm_split4::install(runtime);   // antes dos ganchos do scePadRead (camera livre, lock-on): eles embrulham o dela
    wotm_modes4::install(runtime);   // embrulha o fim de rodada e o InitPlayers do split4; o takeDamage das trapacas embrulha o dele
    wotm_ai::install(runtime);
    wotm_coop::install(runtime);     // embrulha as vistas, a largada, o InitPlayers e o fim de rodada do split4 e do modes4
    if ((g_origPolyBuild = runtime.lookupFunction(kPolyBuildPrim)))
        runtime.registerFunction(kPolyBuildPrim, &polyBuildPrimTapped);
    if ((g_origHier = runtime.lookupFunction(kHier)))
        runtime.registerFunction(kHier, &hierTapped);
    if (const char *d = std::getenv("WOTM_LOGICDELAY"))
    {
        g_logicDelayMs = std::max(0, std::atoi(d));
        std::fprintf(stderr, "[wotm] atraso de 0 a %d ms na entrada do TheGame::Update (WOTM_LOGICDELAY)\n", g_logicDelayMs);
    }
    if (g_origHier && (g_origTheGameUpdate = runtime.lookupFunction(kTheGameUpdate)))
        runtime.registerFunction(kTheGameUpdate, &theGameUpdateTapped);
    if (g_origHier && (g_origTheGameUpdate2 = runtime.lookupFunction(kTheGameUpdate2)))
        runtime.registerFunction(kTheGameUpdate2, &theGameUpdate2Tapped);
    if ((g_origLensFlareRender = runtime.lookupFunction(kLensFlareRender)))
        runtime.registerFunction(kLensFlareRender, &lensFlareRenderTapped);
    wotmPadSelInstall(runtime);
    wotmLevelSelInstall(runtime);
    wotmMainMenuInstall(runtime);
    wotm_menu::install(runtime);
    if ((g_origEnableHighlights = runtime.lookupFunction(kEnableHighlights)))
        runtime.registerFunction(kEnableHighlights, &enableHighlightsTapped);
    if (const char *e = std::getenv("WOTM_MATCH"))
    {
        int v[kMatchMaxAIs + 2], c[kMatchMaxAIs + 2], n = 0;
        const char *p = e;
        for (char *end; n < kMatchMaxAIs + 2; p = end + 1)
        {
            c[n] = -1;
            v[n++] = static_cast<int>(std::strtol(p, &end, 10));
            if (end == p && std::isalpha(static_cast<unsigned char>(*p)))   // "<id do monstro custom>" (mods/<mod>/monsters/, wotm_custom.inc)
            {
                const char *t = p;
                while (*t && *t != ',' && *t != ':')
                    ++t;
                const std::string id(p, t);
                if (const wotm::mods::Custom *cu = wotm::mods::Registry::get().custom(id))
                {
                    v[n - 1] = cu->type;
                    c[n - 1] = cu->costume;
                    end = const_cast<char *>(t);
                }
                else
                    std::fprintf(stderr, "[wotm] WOTM_MATCH: monstro custom \"%s\" nao achado em mods/\n", id.c_str());
            }
            if (end != p && *end == ':')   // "<monstro>:<figurino>"
            {
                const char *q = end + 1;
                if (*q && !std::isdigit(static_cast<unsigned char>(*q)))   // "<monstro>:<skin>": traje de mod pelo id (mods/<mod>/skins/)
                {
                    const char *t = q;
                    while (*t && *t != ',')
                        ++t;
                    const std::string id(q, t);
                    const wotm::mods::Skin *sk = wotm::mods::Registry::get().skin(v[n - 1], id);
                    if (!sk)
                        std::fprintf(stderr, "[wotm] WOTM_MATCH: skin \"%s\" do monstro %d nao achada em mods/; traje 0\n", id.c_str(), v[n - 1]);
                    c[n - 1] = sk ? sk->index : 0;
                    end = const_cast<char *>(t);
                }
                else
                    c[n - 1] = static_cast<int>(std::strtol(q, &end, 10));
                if (end == q || c[n - 1] < 0 || c[n - 1] > 99)   // 4..99: trajes de mod (nome do arquivo ate 8 letras)
                {
                    p = nullptr;
                    break;
                }
            }
            if (end == p || *end != ',')
            {
                p = end == p ? nullptr : end;
                break;
            }
        }
        if (const char *pl = std::getenv("WOTM_PLAYERS"); pl && std::atoi(pl) >= 2 && std::atoi(pl) <= 4)
            g_matchPlayers = std::atoi(pl);   // 3 e 4: tela em quadrantes (wotm_split4.inc)
        if (const char *md = std::getenv("WOTM_MODE"))   // "<modo>[:<continues>]" (wotm_modes4.inc)
        {
            const int m = std::atoi(md);
            g_matchMode = m == 1 || (m >= 6 && m <= 9) ? m : 0;
            if (const char *k = std::strchr(md, ':'))
                g_matchContinues = std::clamp(std::atoi(k + 1), 0, 9);
        }
        // Tipos sem arquivos em MON/ (0 "null", 6 spider, 12 ogre, 16 e 17): a carga le arquivos que nao existem, sem erro, e o
        // texmResRelocate percorre lixo (com 3 jogadores e o 0 nao sai mais do laco).
        for (int i = 0; p && *p == '\0' && i < n - 1; ++i)
            if (v[i] < 1 || v[i] > 15 || v[i] == 6 || v[i] == 12)
            {
                std::fprintf(stderr, "[wotm] WOTM_MATCH: monstro %d nao existe no disco (use 1-5, 7-11, 13-15)\n", v[i]);
                p = nullptr;
            }
        if (p && *p == '\0' && n >= 1 + g_matchPlayers)   // "<monstro>,<fase>" = sozinho na fase, sem IA (como o free for all de 0 IAs do menu)
        {
            g_matchAIs = n - 1 - g_matchPlayers;
            for (int i = 0; i < n - 1; ++i)
                g_matchMonsters[i] = v[i], g_matchCostumes[i] = c[i];
            g_matchLevel = v[n - 1];
        }
        if (g_matchLevel >= 0)
        {
            runtime.registerFunction(kUserintMain, &userintMainDirect);
            runtime.registerFunction(kUiMain, &uiMainSkipped);
            if ((g_origEvaluate = runtime.lookupFunction(kEvaluateGameStatus)))
                runtime.registerFunction(kEvaluateGameStatus, &evaluateGameStatusLogged);
            std::fprintf(stderr, "[wotm] partida direta: %d jogador(es), monstro %d, %d IA(s), fase %d (WOTM_MATCH)\n", g_matchPlayers,
                         g_matchMonsters[0], g_matchAIs, g_matchLevel);
        }
        else
            std::fprintf(stderr, "[wotm] WOTM_MATCH invalido (\"%s\"; use <monstro>[:<figurino 0-3>][,<IA>[:<figurino>]...ate 7],<fase>)\n", e);
    }
    if (const char *e = std::getenv("WOTM_AIVSAI"); e && *e && std::strcmp(e, "0") != 0)
    {
        g_aiVsAiMask = 0xFu;   // "1" = todos os jogadores
        if (std::strchr(e, ','))   // lista: "2,3,4" = esses jogadores; "1," = so o jogador 1
        {
            g_aiVsAiMask = 0;
            for (const char *p = e; *p; ++p)
                if (*p >= '1' && *p <= '4')
                    g_aiVsAiMask |= 1u << (*p - '1');
        }
    }
    if (livesOverride())
    {
        runtime.registerFunction(wotm::fn::InitPlayerLives__5Shell, &initPlayerLivesFixed);
        std::fprintf(stderr, "[wotm] %u vida(s) por jogador (WOTM_LIVES)\n", livesOverride());
    }
    // Sempre instalados: sem WOTM_AIVSAI so agem com bots escolhidos no menu (wotm_menucpus::g_botMask)
    if ((g_aiInit = runtime.lookupFunction(kAiInit)) && (g_origSetAIMonster = runtime.lookupFunction(kSetAIMonster)) &&
        (g_origPlayerInit = runtime.lookupFunction(kPlayerInit)))
    {
        g_aiVsAi = g_aiVsAiMask != 0;
        runtime.registerFunction(kPlayerInit, &playerInitAsAi);
        runtime.registerFunction(kSetAIMonster, &setAIMonsterCounted);
        if ((g_origStartPoint = runtime.lookupFunction(kGameGetStartPoint)))
            runtime.registerFunction(kGameGetStartPoint, &startPointAsHumans);
        if ((g_origCamerasUpdate = runtime.lookupFunction(kCamerasUpdate)))
            runtime.registerFunction(kCamerasUpdate, &camerasUpdateAsHumans);
        if (g_aiVsAi)
        {
            std::fprintf(stderr, "[wotm] jogadores sob a IA (WOTM_AIVSAI):");
            for (int i = 0; i < 4; ++i)
                if (aiVsAiSlot(i))
                    std::fprintf(stderr, " %d", i + 1);
            std::fprintf(stderr, "%s\n", aiVsAiAll() ? " (todos)" : "");
        }
    }
    if ((g_origFfaOptions1P = runtime.lookupFunction(kFfaOptions1P)))
        runtime.registerFunction(kFfaOptions1P, &ffaOptions1PWithZero);
    if ((g_origFfaOptions2P = runtime.lookupFunction(kFfaOptions2P)))
        runtime.registerFunction(kFfaOptions2P, &ffaOptions2PMore);
    // Trapacas de debug (menu F1): sempre instaladas; WOTM_CHEATS=inv,stam,furia,esp,cloak,deus liga desde o inicio
    if ((g_origMonsterUpdate = runtime.lookupFunction(kMonsterUpdate)) && (g_origTakeDamage = runtime.lookupFunction(kTakeDamage)))
    {
        g_setCloakOn = runtime.lookupFunction(kSetCloakOn);
        runtime.registerFunction(kMonsterUpdate, &monsterUpdateCheats);
        runtime.registerFunction(kTakeDamage, &takeDamageCheat);
        if ((g_origTakeHit = runtime.lookupFunction(kTakeHit)))
            runtime.registerFunction(kTakeHit, &takeHitCheat);
        if ((g_origKnockBack = runtime.lookupFunction(kKnockBack)))
            runtime.registerFunction(kKnockBack, &knockBackCheat);
        if ((g_origAdditiveRecoil = runtime.lookupFunction(kAdditiveRecoil)))
            runtime.registerFunction(kAdditiveRecoil, &additiveRecoilCheat);
        if ((g_origEnterNewState = runtime.lookupFunction(kEnterNewState)))
            runtime.registerFunction(kEnterNewState, &enterNewStateCheat);        uint32_t c = std::getenv("WOTM_CLOAK") ? kCheatCloak : 0u;
        if (const char *e = std::getenv("WOTM_CHEATS"))
        {
            const std::string s = std::string(",") + e + ",";
            if (s.find(",inv,") != std::string::npos) c |= kCheatInvincible;
            if (s.find(",stam,") != std::string::npos) c = (c & ~kCheatStaminaMask) | kCheatStamina1;
            if (s.find(",furia,") != std::string::npos) c = (c & ~kCheatStaminaMask) | 2u * kCheatStamina1;
            if (s.find(",esp,") != std::string::npos) c |= kCheatSpecial;
            if (s.find(",cloak,") != std::string::npos) c |= kCheatCloak;
            if (s.find(",deus,") != std::string::npos) c |= kCheatGod;
            if (const size_t w = s.find(",vencer="); w != std::string::npos)
                g_autoWinSecs = std::atoi(s.c_str() + w + 8);
        }
        g_wotmCheats = c;
        if (c || g_autoWinSecs > 0)
            std::fprintf(stderr, "[wotm] trapacas de debug ligadas desde o inicio: 0x%x, vencer a cada %d s (WOTM_CHEATS/WOTM_CLOAK)\n", c, g_autoWinSecs);
    }
    // Camera livre (F5): depois dos outros hooks, para o de Cameras::Update embrulhar o do WOTM_AIVSAI quando ele existe
    if ((g_origCamerasUpdateFree = runtime.lookupFunction(kCamerasUpdate)) && (g_origViewUpdate = runtime.lookupFunction(kViewUpdate)) &&
        (g_origPadRead = runtime.lookupFunction(kScePadRead)))
    {
        runtime.registerFunction(kCamerasUpdate, &camerasUpdateFree);
        runtime.registerFunction(kViewUpdate, &viewUpdateFree);
        runtime.registerFunction(kScePadRead, &padReadFree);
    }
    else
        std::fprintf(stderr, "[wotm] camera livre indisponivel: Cameras::Update, viewUpdate ou scePadRead sem funcao no runtime\n");
    // Lock-on alternado (mod, WOTM_LOCKTOGGLE=1, "QoL features"): um toque trava, outro solta. Ganchos sempre instalados; a opcao
    // (wotm_settings.hpp) vale ao vivo, desligada eles so passam adiante
    {
        constexpr PS2Runtime::RecompiledFunction kHooks[3] = {&updateHistoryLockToggle<0>, &updateHistoryLockToggle<1>,
                                                              &updateHistoryLockToggle<2>};
        for (int i = 0; i < 3; i++)
            g_origUpdateHistory[i] = runtime.lookupFunction(kUpdateHistory[i]);
        if (g_origUpdateHistory[0] && g_origUpdateHistory[1] && g_origUpdateHistory[2])
        {
            for (int i = 0; i < 3; i++)
                runtime.registerFunction(kUpdateHistory[i], kHooks[i]);
            std::fprintf(stderr, "[wotm] lock-on alternado (WOTM_LOCKTOGGLE, agora %s): um toque trava, outro solta\n",
                         wotm::settings::on(wotm::settings::LockToggle) ? "ligado" : "desligado");
            // botoes do mod na configuracao 0 (depois da camera livre: o scePadRead daqui embrulha o dela)
            if ((g_origInterpretInputs = runtime.lookupFunction(kInterpretInputs)) && (g_origPadReadLock = runtime.lookupFunction(kScePadRead)))
            {
                runtime.registerFunction(kInterpretInputs, &interpretInputsLockToggle);
                runtime.registerFunction(kScePadRead, &padReadLockToggle);
                std::fprintf(stderr, "[wotm] lock-on no R1, defesa no L1, andar de lado no L2/R2, setas trocam o alvo (configuracao 0)\n");
            }
        }
        else
            std::fprintf(stderr, "[wotm] lock-on alternado indisponivel: PadFlags::updateHistory{0,1,2} sem funcao no runtime\n");
    }
    // Camera moderna (wotm_orbit.inc): o Cameras::Update e o interpretInputs ja estao embrulhados acima; aqui o shakeCamera e o
    // SetOkToUnify (depois do wotm_split4, cujo gancho este embrulha)
    orbit::install(runtime);
    // Numeros de dano (mod, WOTM_DAMAGEPOP=1): depois das trapacas, que tambem embrulham o Monster::update e o takeHit. Sempre
    // instalados (o acompanhamento da vida e barato); a opcao so decide se o numero sai
    {
        if ((g_origMonsterUpdateDmg = runtime.lookupFunction(kMonsterUpdateDmg)) && (g_origTakeHitDmg = runtime.lookupFunction(kTakeHitDmg)))
        {
            runtime.registerFunction(kMonsterUpdateDmg, &monsterUpdateDamage);
            runtime.registerFunction(kTakeHitDmg, &takeHitDamage);
            std::fprintf(stderr, "[wotm] numeros de dano (WOTM_DAMAGEPOP, agora %s): golpe branco, arremesso laranja, especial ciano\n",
                         wotm::settings::on(wotm::settings::DamagePop) ? "ligados" : "desligados");
        }
        else
            std::fprintf(stderr, "[wotm] numeros de dano indisponiveis: Monster::update ou Monster::takeHit sem funcao no runtime\n");
    }
    // Barras de vida e energia sobre os inimigos (mod, WOTM_ENEMYBARS=1): no lugar do icone de vida do Hud::drawHealthIcon. Ganchos
    // sempre instalados: sem a variavel, as barras valem so na luta de 3 ou 4 jogadores (o HUD do multijogador)
    {
        if ((g_origDrawHealthIcon = runtime.lookupFunction(kDrawHealthIcon)) && (g_origAddPolyBars = runtime.lookupFunction(kAddPoly)))
        {
            runtime.registerFunction(kDrawHealthIcon, &drawHealthIconBars);
            runtime.registerFunction(kAddPoly, &addPolyBars);
            if ((g_origXform2dBars = runtime.lookupFunction(kXform2d)))
                runtime.registerFunction(kXform2d, &xform2dBars);
            if (wotm::settings::on(wotm::settings::EnemyBars))
                std::fprintf(stderr, "[wotm] barras de vida e energia sobre os inimigos (WOTM_ENEMYBARS)\n");
        }
        else
            std::fprintf(stderr, "[wotm] barras sobre os inimigos indisponiveis: Hud::drawHealthIcon ou ScreenPolys::addPoly sem funcao no runtime\n");
    }
    extern void wotmVu1CheckInstall(PS2Runtime &);   // wotm_vu1check.inc (WOTM_VU1CHECK: VU1 x nativo por objeto)
    wotmVu1CheckInstall(runtime);
    extern void wotmVu0UcodeInstall(PS2Runtime &);   // wotm_live.inc: microcodigo do VU0 sem VIF0 no executavel do jogo
    wotmVu0UcodeInstall(runtime);
    wotmSkinsInstall(runtime);   // por ultimo: embrulha os ganchos do SetPlayerMonster/SetAIMonster (split4, WOTM_AIVSAI)
    if (g_matchMode == 1 && g_matchLevel >= 0 && (g_origDialogAdventure = runtime.lookupFunction(wotm::fn::displayDialog__Fi)))
        runtime.registerFunction(wotm::fn::displayDialog__Fi, &dialogAdventure);   // depois do da pausa (wotm_pausemenu.inc)
    wotmCustomInstall(runtime);  // depois das skins: embrulha o SetAIMonster delas
    wotmTrainingInstall(runtime);   // por ultimo: o gancho mais de fora do update/takeHit/takeDamage (ve o dano que as trapacas deixam)
    if (g_minRate > 0)
        std::fprintf(stderr, "[wotm] updateRate >= %d (dt <= 1/%d) para o jogo inteiro (WOTM_RATEMIN)\n", g_minRate, g_minRate);
    else
        std::fprintf(stderr, "[wotm] updateRate sem limite (WOTM_RATEMIN=0)\n");
}
