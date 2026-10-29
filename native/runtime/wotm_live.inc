// wotm_live.inc: desenho nativo ao vivo (incluido por wotm_scene.inc, no escopo global de ps2_runtime.cpp).
// Usa native/wotm_native.hpp (do repo WoTM; passe -DWOTM_NATIVE_DIR=<WoTM>/native ao CMake). Sem o header, nao faz nada.
// Filmes (FMV) nativos: native/wotm_fmv.hpp do repo WoTM, ligado com PS2X_FMV=1 (wotmFmvInstall, chamado do main.cpp).
// Substitui CMovie::Play/Update/End e playMpegMovieWithDB/updateMpegMovieDecoding (o decodificador do jogo espera a IPU, que o
// runtime nao emula) e toca o .PSS do disco (pasta cdRoot ou a ISO de PS2X_CD_IMAGE) com o FFmpeg. Sem o header ou sem o FFmpeg,
// PS2X_FMV so avisa.
#if __has_include("wotm_fmv.hpp") && __has_include(<libavcodec/avcodec.h>)
#include "wotm_fmv.hpp"
#include "wotm_syms.h"   // wotm::sym / wotm::fn: enderecos do ELF por nome (tools/native_syms.py --gen)
#define WOTM_HAVE_FMV 1
namespace wotm_fmv_hooks
{
inline void playWithDB(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
{
    setReturnS32(ctx, static_cast<int32_t>(wotm::fmv::hooks::playMpegMovieWithDB(rdram, getRegU32(ctx, 4), getRegU32(ctx, 5))));
}
inline void updateDecoding(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
{
    setReturnS32(ctx, static_cast<int32_t>(wotm::fmv::hooks::updateMpegMovieDecoding(rdram, getRegU32(ctx, 4))));
}
inline void cmoviePlay(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
{
    setReturnS32(ctx, static_cast<int32_t>(wotm::fmv::hooks::cmoviePlay(rdram, getRegU32(ctx, 4), getRegU32(ctx, 5), getRegU32(ctx, 6))));
}
inline void cmovieUpdate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
{
    setReturnS32(ctx, static_cast<int32_t>(wotm::fmv::hooks::cmovieUpdate(rdram, getRegU32(ctx, 4), getRegU32(ctx, 5))));
}
inline void cmovieEnd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
{
    setReturnS32(ctx, static_cast<int32_t>(wotm::fmv::hooks::cmovieEnd(rdram, getRegU32(ctx, 4))));
}
} // namespace wotm_fmv_hooks

void wotmFmvInstall(PS2Runtime &runtime)
{
    const PS2Runtime::IoPaths &io = PS2Runtime::getIoPaths();
    wotm::fmv::Player &p = wotm::fmv::Player::get();
    p.discRoot = io.cdRoot;
    p.isoPath = io.cdImage;
    runtime.registerFunction(wotm::fn::playMpegMovieWithDB__FPcb, hostLeaf<wotm_fmv_hooks::playWithDB>);     // playMpegMovieWithDB
    runtime.registerFunction(wotm::fn::updateMpegMovieDecoding__Fb, hostLeaf<wotm_fmv_hooks::updateDecoding>); // updateMpegMovieDecoding
    runtime.registerFunction(wotm::fn::Play__6CMoviebPc, hostLeaf<wotm_fmv_hooks::cmoviePlay>);     // CMovie::Play
    runtime.registerFunction(wotm::fn::End__6CMovie, hostLeaf<wotm_fmv_hooks::cmovieEnd>);      // CMovie::End
    runtime.registerFunction(wotm::fn::Update__6CMovieb, hostLeaf<wotm_fmv_hooks::cmovieUpdate>);   // CMovie::Update
    std::fprintf(stderr, "[fmv] filmes nativos ligados (disco: %s, iso: %s)\n", io.cdRoot.string().c_str(), io.cdImage.string().c_str());
}

// Uma vez por apresentacao (thread da janela, dentro do BeginDrawing): avanca os filmes, copia o do menu para a VRAM do GS e
// desenha o de tela cheia por cima.
static void wotmFmvPresent(uint8_t *gsVram)
{
    wotm::fmv::Player &p = wotm::fmv::Player::get();
    p.pump(gsVram, PS2_GS_VRAM_SIZE);
    p.drawFullscreen();
}
#else
void wotmFmvInstall(PS2Runtime &)
{
    std::fprintf(stderr, "[fmv] PS2X_FMV ignorado: falta native/wotm_fmv.hpp (WOTM_NATIVE_DIR) ou o FFmpeg (PS2X_ENABLE_FFMPEG)\n");
}
static void wotmFmvPresent(uint8_t *) {}
#endif

// Microcodigo do VU0 sem VIF0 (FRONTEIRA.md, passo 7). A cada quadro o hier chama o hierLoadVu0Ucode, que manda pelo canal 0
// do DMA uma cadeia fixa do ELF: so MPG, a imagem inteira dos microprogramas (sombras e particulas inclusas), nunca UNPACK.
// No executavel do jogo o gancho copia a imagem (wotmVu0CodeImage, gerada por tools/vu0native.py do mesmo pacote) direto na
// memoria de codigo do VU0, so quando ela muda (a geracao nova faz os microprogramas traduzidos conferirem o hash de novo); o
// VIF0 nao existe no wotm. No de teste o original continua, e a primeira carga confere que o VIF0 deixou a mesma imagem.
#if __has_include("wotm_vu0_native.h") && __has_include("wotm_syms.h")
#include "wotm_vu0_native.h"
#include "wotm_syms.h"
namespace wotm_vu0ucode
{
static_assert(PS2_VU0_CODE_SIZE == 4096, "imagem do vu0native.py tem 4096 bytes");
static PS2Runtime::RecompiledFunction g_orig = nullptr;
inline bool sameAsImage(PS2Runtime *rt)
{
    const uint8_t *img = wotmVu0CodeImage();
    const uint8_t *code = rt->memory().getVU0Code();
    return img && code && std::memcmp(code, img, PS2_VU0_CODE_SIZE) == 0;
}
inline void load(uint8_t *, R5900Context *, PS2Runtime *rt)
{
    if (sameAsImage(rt))
        return;
    static int s_loads = 0;
    if (++s_loads <= 3)
        std::fprintf(stderr, "[vu0] microcodigo copiado da imagem (#%d)\n", s_loads);
    std::memcpy(rt->memory().getVU0Code(), wotmVu0CodeImage(), PS2_VU0_CODE_SIZE);
    rt->memory().markVU0CodeModified();
}
// o hierInit manda a mesma cadeia do VIF0 por conta propria (boot), e os microprogramas rodam antes do primeiro hierLoadVu0Ucode
static PS2Runtime::RecompiledFunction g_origInit = nullptr;
inline void initThenLoad(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
{
    g_origInit(rdram, ctx, rt);
    load(rdram, ctx, rt);
}
// o DMA do VIF0 e aplicado depois (fila do PS2Memory): confere na segunda chamada, antes do original, o que a primeira deixou
inline void loadAndCheck(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
{
    static int s_calls = 0;
    if (++s_calls == 2)
        std::fprintf(stderr, "[vu0] microcodigo do VIF0 %s a imagem gerada (wotmVu0CodeImage)\n", sameAsImage(rt) ? "igual a" : "DIFERENTE da");
    g_orig(rdram, ctx, rt);
}
} // namespace wotm_vu0ucode

void wotmVu0UcodeInstall(PS2Runtime &runtime)
{
    if (!wotmVu0CodeImage())
    {
        std::fprintf(stderr, "[vu0] wotm_vu0_native.gen.inc sem a imagem do microcodigo: rode o build_runtime.bat (tools/vu0native.py)\n");
        return;
    }
#if PS2X_HW_EMU
    wotm_vu0ucode::g_orig = runtime.lookupFunction(wotm::fn::hierLoadVu0Ucode__Fv);
    if (wotm_vu0ucode::g_orig)
        runtime.registerFunction(wotm::fn::hierLoadVu0Ucode__Fv, hostLeaf<wotm_vu0ucode::loadAndCheck>);
#else
    runtime.registerFunction(wotm::fn::hierLoadVu0Ucode__Fv, hostLeaf<wotm_vu0ucode::load>);
    wotm_vu0ucode::g_origInit = runtime.lookupFunction(wotm::fn::hierInit__Fv);
    if (wotm_vu0ucode::g_origInit)
        runtime.registerFunction(wotm::fn::hierInit__Fv, hostLeaf<wotm_vu0ucode::initThenLoad>);
    else
        std::fprintf(stderr, "[vu0] hierInit nao encontrado: o microcodigo so sobe no primeiro hierLoadVu0Ucode\n");
#endif
}
#else
void wotmVu0UcodeInstall(PS2Runtime &) {}
#endif

// Rastro do boot: da abertura do processo ao primeiro filme ou quadro do jogo, cada etapa vai ao stderr com o tempo desde o inicio
// ("[boot]  3.21 s  disco: ...") e aparece escrita na tela preta. Etapa parada ha mais de 3 s repete no log com o PC do EE, para
// achar o gargalo em maquinas onde a tela preta demora. Chamado do main.cpp, de initialize/run, do sceCdSearchFile e do log do IOP.
#include <chrono>
#include <cstdarg>
#include <mutex>
namespace wotm_boot
{
inline const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
inline std::mutex mx;
inline std::string step = "iniciando";
inline double stepAt = 0.0;
inline std::atomic<bool> done{false}, presenting{false};
inline std::thread::id mainThread;
inline double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); }
// Consolas do Windows (carregada na thread da janela, na primeira vez); sem o arquivo, a fonte padrao do raylib
inline Font font()
{
    static Font f = [] {
        Font r = LoadFontEx("C:/Windows/Fonts/consola.ttf", 40, nullptr, 0);
        if (r.texture.id == 0)
            return GetFontDefault();
        SetTextureFilter(r.texture, TEXTURE_FILTER_BILINEAR);
        return r;
    }();
    return f;
}
inline void drawOverlay()
{
    std::string s;
    double at;
    {
        std::lock_guard<std::mutex> lk(mx);
        s = step;
        at = stepAt;
    }
    const double t = now();
    const float h = static_cast<float>(GetScreenHeight());
    DrawTextEx(font(), TextFormat("Loading... %.1f s", t), Vector2{16.0f, h - 54.0f}, 20.0f, 0.0f, LIGHTGRAY);
    DrawTextEx(font(), TextFormat("%s%s", s.c_str(), t - at >= 3.0 ? TextFormat("  (%.0f s in this step)", t - at) : ""), Vector2{16.0f, h - 28.0f},
               16.0f, 0.0f, GRAY);
}
} // namespace wotm_boot

void wotmBoot(const char *fmt, ...)
{
    if (wotm_boot::done.load(std::memory_order_relaxed))
        return;
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    for (size_t n = std::strlen(buf); n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' '); --n)
        buf[n - 1] = '\0';   // o printf do IOP vem com quebra de linha
    const double t = wotm_boot::now();
    {
        std::lock_guard<std::mutex> lk(wotm_boot::mx);
        if (wotm_boot::mainThread == std::thread::id())
            wotm_boot::mainThread = std::this_thread::get_id();
        wotm_boot::step = buf;
        wotm_boot::stepAt = t;
    }
    std::fprintf(stderr, "[boot] %6.2f s  %s\n", t, buf);
    std::fflush(stderr);
    // antes do laco de apresentacao (ELF, hooks) ninguem desenha: a thread principal desenha um quadro com a etapa
    if (!wotm_boot::presenting.load(std::memory_order_relaxed) && IsWindowReady() && std::this_thread::get_id() == wotm_boot::mainThread)
    {
        BeginDrawing();
        ClearBackground(BLACK);
        wotm_boot::drawOverlay();
        EndDrawing();
    }
}

// Uma vez por apresentacao, enquanto o boot nao termina: escreve a etapa na tela, repete no log a etapa parada (com o PC do EE) e
// encerra o rastro no primeiro filme; sem filme (WOTM_MATCH pula a abertura), quando a camada nativa carrega a fase (wotmBootDone).
// g_frame e world nao servem: os dois ja estao preenchidos antes da abertura.
static void wotmBootDone(const char *what)
{
    if (wotm_boot::done.load(std::memory_order_relaxed))
        return;
    wotmBoot("ready: %s", what);
    wotm_boot::done.store(true, std::memory_order_relaxed);
    if (const char *c = std::getenv("WOTM_CRASHTEST"); c && c[0] == '1')   // diagnostico: cai aqui de proposito (pilha da queda no log)
    {
        std::fprintf(stderr, "[wotm] WOTM_CRASHTEST: queda de proposito\n");
        std::fflush(stderr);
        *static_cast<volatile int *>(nullptr) = 1;
    }
}
// O vsync por segundo no aviso mostra se a espera e o relogio virtual andando devagar (o boot espera vsyncs).
static void wotmBootPresent(uint32_t eePc, uint64_t vsync)
{
    if (wotm_boot::done.load(std::memory_order_relaxed))
        return;
    wotm_boot::presenting.store(true, std::memory_order_relaxed);
#ifdef WOTM_HAVE_FMV
    const wotm::fmv::Player &fp = wotm::fmv::Player::get();
    if (fp.active(wotm::fmv::Player::Full) || fp.active(wotm::fmv::Player::Screen))
        return wotmBootDone("first movie");
#endif
    static double s_lastBeat = 0.0;
    static uint64_t s_lastVsync = 0;
    const double t = wotm_boot::now();
    if (s_lastBeat == 0.0)
    {
        s_lastBeat = t;
        s_lastVsync = vsync;
    }
    std::string s;
    double at;
    {
        std::lock_guard<std::mutex> lk(wotm_boot::mx);
        s = wotm_boot::step;
        at = wotm_boot::stepAt;
    }
    if (t - at >= 2.0 && t - s_lastBeat >= 2.0)
    {
        const double rate = double(vsync - s_lastVsync) / (t - s_lastBeat);
        std::fprintf(stderr, "[boot] %6.2f s  ... ainda em \"%s\" ha %.1f s (PC do EE 0x%06x, vsync %llu, %.0f/s)\n", t, s.c_str(), t - at,
                     eePc, (unsigned long long)vsync, rate);
        std::fflush(stderr);
        s_lastBeat = t;
        s_lastVsync = vsync;
    }
    wotm_boot::drawOverlay();
}

// Som nativo (native/wotm_snd.hpp), sempre ligado (wotmSndInstall, chamado do main.cpp)
#include "wotm_guestcall.h"   // termina as chamadas a codigo do jogo feitas de dentro de ganchos
#include "wotm_snd.inc"

// Controles e teclado por jogador (mapa porta -> dispositivos do ps2_pad.cpp), sempre ligado
#include "../wotm_settings.hpp"   // opcoes ao vivo (painel "recompiled options")
#include "../wotm_mouse.hpp"   // mouse na camera moderna (cursor preso, deslocamento para a orbita)
#include "wotm_pad.inc"

#if __has_include("wotm_native.hpp")
#include "wotm_native.hpp"
#include "wotm_msaa.hpp"   // MSAA do quadro (opcao ANTI-ALIASING)
#include "wotm_preview.hpp"   // previas das opcoes da aba VIDEO (wotm_mainmenu.inc)
#include "wotm_cheats.h"   // menu de debug (F1)
#include <filesystem>
#include "wotm_freecam.inc"   // camera livre de debug (F5)
extern std::atomic<float> g_wotmMechaReticle;   // wotm_overrides.cpp: peso da camera da torre do Mecha (0 = sem retícula)
#include "wotm_prompts.inc"   // icones de botao 2D (Kenney) no lugar dos 3D do jogo
#include "wotm_trainview.inc"   // modo treino: volumes, esferas dos golpes, painel de dano e numeros do HUD
#define WOTM_HAVE_NATIVE 1

namespace wotm_live
{
// Diagnostico de queda: sem windows.h (conflita com o raylib), espelha so o que o filtro precisa.
struct ExcRecord
{
    unsigned long code, flags;
    ExcRecord *next;
    void *address;
    unsigned long nparams;
    unsigned long long info[15];
};
struct ExcPointers
{
    ExcRecord *rec;
    void *ctx;
};
extern "C" __declspec(dllimport) void *__stdcall SetUnhandledExceptionFilter(long(__stdcall *)(ExcPointers *));
extern "C" __declspec(dllimport) void *__stdcall GetModuleHandleA(const char *);
inline const char *g_stage = "idle";   // etapa da camada nativa em andamento (para localizar a queda)
} // namespace wotm_live
void wotmCrashTrace(void *exceptionContext);   // wotm_standalone.cpp
namespace wotm_live
{
inline long __stdcall crashFilter(ExcPointers *e)
{
    const char *base = static_cast<const char *>(GetModuleHandleA(nullptr));
    std::fprintf(stderr, "[CRASH] code=0x%08lx addr=%p (exe+0x%llx) stage=%s", e->rec->code, e->rec->address,
                 (unsigned long long)(static_cast<const char *>(e->rec->address) - base), g_stage);
    if (e->rec->code == 0xC0000005)
        std::fprintf(stderr, " acesso=%s endereco=0x%llx", e->rec->info[0] == 1 ? "escrita" : (e->rec->info[0] == 8 ? "execucao" : "leitura"), e->rec->info[1]);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
    static bool traced = false;   // so a primeira queda: a pilha de uma segunda (dentro do dbghelp) nao ajuda
    if (!traced)
    {
        traced = true;
        wotmCrashTrace(e->ctx);   // pilha com nomes pelo wotm.pdb (wotm_standalone.cpp)
    }
    return 0; // EXCEPTION_CONTINUE_SEARCH
}
struct State
{
    wotm::Scene scene;
    RenderTexture2D rts[2]{};   // alternadas: a do quadro anterior e a textura do texId 0 (o jogo amostra o buffer em exibicao)
    RenderTexture2D vrts[4]{};  // tela dividida: uma por vista (metade ou quadrante do quadro), compostas em rts[cur]
    wotm::msaa::Target ms, vms[4];   // com MSAA: onde o quadro e as vistas sao desenhados antes de resolvidos em rts/vrts
    int cur = 0;
    uint32_t levelSig = 0;
    bool rtxChosen = false;
    std::string rtxPath;
    long frame = 0;
    wotm::Snapshot snap;   // ultimo quadro do jogo (desenhado ate o proximo)
    // Interpolacao de quadros (wotmDrawNative): o quadro anterior a snap (A; snap e B), o anterior a ele (P, sem a copia das
    // particulas: so as matrizes e a camera, para o instante antes de A quando B chega adiantado) e o relogio de exibicao
    wotm::Snapshot prev, prev2;
    double interpO = 0.0;      // relogio do host (s) em que o jogo estaria no field 0, filtrado (interpTarget)
    bool interpHaveO = false;
    float lastAlpha = -1.f;    // alfa do ultimo redesenho (-1: sem mistura)
};

// Fase carregada: so o cabecalho da imagem do nivel em 0xA00000 (N e os N ponteiros dos nos-raiz, docs/FORMATOS.md) e o endereco
// das texturas. Os nos logo depois sao estado vivo (o menu reescreve matrizes ali a cada quadro nas transicoes de tela, o que
// invalidava a cena inteira por quadro: tela a ~16/s).
inline uint32_t levelSignature(const wotm::Ram &ram)
{
    uint32_t h = ram.u32(wotm::addr::tempVramTexAddr) * 2654435761u;
    const uint32_t n = std::min<uint32_t>(ram.u32(0xA00000), 255u);
    for (uint32_t k = 0; k <= n; ++k)
        h = (h ^ ram.u32(0xA00000 + 4 * k)) * 16777619u;
    return h;
}

} // namespace wotm_live

// Desenha a fase lida da RAM (sem VU1/GS) por cima do quadro; devolve falso (e nao desenha) quando nao ha fase carregada.
namespace wotm_live
{
static bool g_hideFps = false;   // replay (--vu1cmp, wotm_vu1check.inc): imagem nativa sem o contador de FPS
static bool g_force43 = false;   // replay: quadro 4:3, como a imagem de referencia do GS
static bool g_localCollect = false;   // replay (--vu1cmp): sem jogo rodando, a apresentacao le a RAM do instantaneo ela mesma

// Widescreen (Hor+): o FOV vertical e o do jogo e a largura acompanha a proporcao. A opcao Aspect (WOTM_ASPECT=L:A, wotm_settings.hpp,
// ao vivo) fixa a proporcao (4:3 = tela original, com faixas nas laterais); dinamica, a proporcao segue a janela (entre 4:3 e 32:9),
// que abre em 16:9.
static float fixedAspect() { return wotm::settings::aspectRatio(); }
static float viewAspect()
{
    if (g_force43)
        return 4.f / 3.f;
    if (const float a = fixedAspect())
        return a;
    const float sh = static_cast<float>(GetScreenHeight());
    return sh > 0.f ? std::clamp(static_cast<float>(GetScreenWidth()) / sh, 4.f / 3.f, 32.f / 9.f) : 4.f / 3.f;
}
// Altura do quadro nativo em pixels (opcao Res, WOTM_RES=<linhas>, padrao 720, ao vivo; a tela do PS2 tem 448, 224 por campo; 0 = a
// altura da janela ou da tela cheia, em pixels) vezes a escala (opcao RenderScale, WOTM_RENDERSCALE=<%>: acima de 100 e supersampling,
// a apresentacao reduz com filtro bilinear); a largura sai da proporcao, com no maximo 16384. Mudou: o desenho refaz as texturas do
// quadro (wotmDrawNative, sizeOk). O replay (--vu1cmp) fica em 720, como as comparacoes ja gravadas.
static int baseHeight()
{
    const int res = wotm::settings::get(wotm::settings::Res);
    if (res > 0)
        return res;
    const int h = GetRenderHeight();
    static int logged = -1;
    if (h != logged)
    {
        logged = h;
        std::fprintf(stderr, "[native] quadro pela janela: %d linhas (janela %dx%d, framebuffer %dx%d)\n", h, GetScreenWidth(), GetScreenHeight(),
                     GetRenderWidth(), h);
    }
    return h > 0 ? h : 720;
}
static float viewAspect();
static int renderHeight()
{
    if (g_force43)
        return 720;
    const int scale = std::clamp(wotm::settings::get(wotm::settings::RenderScale), 25, 400);
    const int h = static_cast<int>(std::lround(baseHeight() * (scale / 100.0)));
    return std::clamp(h, 112, std::min(8640, static_cast<int>(16384.f / viewAspect())));
}
// Uma vez, na primeira apresentacao (wotmDrawScene, antes da abertura): WOTM_FULLSCREEN=1|2 vai para a tela cheia sem borda ou exclusiva (proporcao
// do monitor; com WOTM_ASPECT fixo o quadro fica com faixas); senao a janela de 960x720 do runtime vira o tamanho do quadro nativo
// (renderHeight na proporcao), reduzido para caber no monitor. O quadro nao depende da janela: so a ampliacao final muda.
// Taxa de apresentacao: WOTM_HZ=<n> (padrao: a do monitor da janela) com o vsync do monitor (WOTM_VSYNC=0 desliga e so limita pelo
// relogio). A logica do jogo continua a 60: a apresentacao so redesenha a cena quando chega quadro novo (wotmDrawNative, "redesenho")
// e nas outras reapresenta a ultima imagem. Ganho: ritmo regular (sem a batida entre os 60 do raylib e o monitor), sem tearing.
// O runtime abria com SetTargetFPS(60) fixo (ps2_runtime.cpp); aqui e trocado na primeira apresentacao.
// Opcoes Rate e Vsync (wotm_settings.hpp), ao vivo: applyVideoSettings chama de novo quando mudam.
static void initPresentRate()
{
    const int mon = GetCurrentMonitor(), monHz = GetMonitorRefreshRate(mon);
    int hz = wotm::settings::get(wotm::settings::Rate);
    if (hz <= 0)
        hz = monHz > 0 ? monHz : 60;
    hz = std::clamp(hz, 30, 1000);
    const bool vsync = wotm::settings::on(wotm::settings::Vsync);
    if (vsync)
        SetWindowState(FLAG_VSYNC_HINT);
    else
        ClearWindowState(FLAG_VSYNC_HINT);
    // com vsync na taxa do monitor quem segura e a troca de buffers; abaixo dela o limite por tempo divide (60 num de 180 = 1 em 3)
    SetTargetFPS(vsync && hz >= monHz ? 0 : hz);
    const int eff = vsync && monHz > 0 ? std::min(hz, monHz) : hz;
    wotm::settings::g_presentHz.store(eff, std::memory_order_relaxed);
    std::fprintf(stderr, "[native] apresentacao: %d Hz (monitor %d Hz), vsync %s (WOTM_HZ, WOTM_VSYNC)\n", eff, monHz, vsync ? "ligado" : "desligado");
}

// Janela no tamanho do quadro nativo (renderHeight na proporcao fixa, ou 16:9), reduzida para caber em 90% do monitor e centrada.
static void fitWindow(bool keepHeight = false)
{
    const float a = fixedAspect() ? fixedAspect() : 16.f / 9.f;
    const int res = wotm::settings::get(wotm::settings::Res);   // 0 (a da janela): 720, ou a altura de agora (keepHeight)
    float h = static_cast<float>(res > 0 ? res : keepHeight && GetScreenHeight() > 0 ? GetScreenHeight() : 720), w = h * a;
    const int mon = GetCurrentMonitor();
    const float mw = 0.9f * GetMonitorWidth(mon), mh = 0.9f * GetMonitorHeight(mon);
    if (mw > 0.f && mh > 0.f)
    {
        const float k = std::min({1.f, mw / w, mh / h});
        w *= k;
        h *= k;
    }
    SetWindowSize(static_cast<int>(std::lround(w)), static_cast<int>(std::lround(h)));
    if (mw > 0.f && mh > 0.f)
    {
        const Vector2 p = GetMonitorPosition(mon);
        SetWindowPosition(static_cast<int>(p.x + (mw / 0.9f - w) * 0.5f), static_cast<int>(p.y + (mh / 0.9f - h) * 0.5f));
    }
}

// Tipo de tela cheia (opcao Display, WOTM_FULLSCREEN): 1 = sem borda (janela do tamanho do monitor, o desktop nao muda de modo), 2 =
// exclusiva (o monitor troca para o modo da janela, na resolucao nativa dele). O F11 alterna entre a janela e o tipo escolhido (com a
// opcao em janela: sem borda); ele nao muda a opcao.
static int g_fullscreenKind = 1;

static bool inFullscreen()
{
    return IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE) || IsWindowFullscreen();
}

// Captura pelo OBS na tela cheia sem borda (WOTM_CAPTUREFIX, padrao 1). O GLFW faz a janela sem borda com WS_POPUP e o raylib a poe
// do tamanho exato do monitor: com o foco, o driver (OpenGL da NVIDIA) promove a janela a "tela cheia" e apresenta direto, fora da
// composicao do DWM (independent flip). Fora de foco a imagem passa pelo DWM e o OBS grava; com foco a captura nova (Windows Graphics
// Capture) congela na ultima imagem composta e a antiga (BitBlt) fica preta. 1 = tira o WS_POPUP (fica WS_OVERLAPPED sem moldura, como
// no relato do Processing/JOGL); 2 = isso e a janela 1 px mais alta que o monitor (o driver so promove a janela que cobre o monitor
// exato; a linha a mais fica fora da tela); 0 = como o raylib deixa. Sem windows.h (conflita com o raylib).
extern "C" __declspec(dllimport) long long __stdcall GetWindowLongPtrW(void *hwnd, int index);
extern "C" __declspec(dllimport) long long __stdcall SetWindowLongPtrW(void *hwnd, int index, long long value);
extern "C" __declspec(dllimport) int __stdcall SetWindowPos(void *hwnd, void *after, int x, int y, int cx, int cy, unsigned flags);
static void captureFriendlyBorderless()
{
    const char *e = std::getenv("WOTM_CAPTUREFIX");
    const int mode = e && *e ? std::atoi(e) : 1;
    void *hwnd = GetWindowHandle();
    if (mode <= 0 || !hwnd)
        return;
    constexpr int kGwlStyle = -16;
    constexpr long long kWsPopup = 0x80000000LL;
    constexpr unsigned kSwpNoSize = 0x1, kSwpNoMove = 0x2, kSwpNoZorder = 0x4, kSwpNoActivate = 0x10, kSwpFrameChanged = 0x20;
    const long long style = GetWindowLongPtrW(hwnd, kGwlStyle);
    SetWindowLongPtrW(hwnd, kGwlStyle, style & ~kWsPopup);   // WS_OVERLAPPED = 0: sem WS_CAPTION continua sem moldura
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, kSwpNoSize | kSwpNoMove | kSwpNoZorder | kSwpNoActivate | kSwpFrameChanged);
    if (mode >= 2)
    {
        const int mon = GetCurrentMonitor();
        const Vector2 p = GetMonitorPosition(mon);
        SetWindowPos(hwnd, nullptr, static_cast<int>(p.x), static_cast<int>(p.y), GetMonitorWidth(mon), GetMonitorHeight(mon) + 1,
                     kSwpNoZorder | kSwpNoActivate);
    }
    std::fprintf(stderr, "[native] tela cheia sem borda: estilo 0x%llx -> 0x%llx%s (WOTM_CAPTUREFIX=%d, para o OBS)\n",
                 static_cast<unsigned long long>(style), static_cast<unsigned long long>(style & ~kWsPopup), mode >= 2 ? ", +1 px de altura" : "",
                 mode);
}

// Tela cheia no monitor da janela (proporcao do monitor; com WOTM_ASPECT fixo o quadro fica com faixas) ou de volta a janela.
static void setFullscreen(bool on)
{
    if (on == inFullscreen())
        return;
    if (on && g_fullscreenKind == 2)
    {
        // o ToggleFullscreen do raylib 5.5 pede ao monitor o modo do tamanho atual da janela: antes, a janela vira a resolucao nativa
        const int mon = GetCurrentMonitor();
        SetWindowSize(GetMonitorWidth(mon), GetMonitorHeight(mon));
        ToggleFullscreen();
        return;
    }
    if (on)
    {
        ToggleBorderlessWindowed();
        ClearWindowState(FLAG_WINDOW_TOPMOST);   // o raylib 5.5 poe a janela sem borda sempre por cima (GLFW_FLOATING)
        captureFriendlyBorderless();
        return;
    }
    if (IsWindowFullscreen())
        ToggleFullscreen();   // volta com o tamanho do monitor
    else
        ToggleBorderlessWindowed();
    fitWindow();   // o raylib volta ao tamanho de antes, que e o 960x720 do runtime (ou o do monitor) quando a sessao abriu em tela cheia
}

// Opcoes de video ja aplicadas a janela (a thread da janela compara com as atuais a cada apresentacao)
static int g_appliedVideo[wotm::settings::kKeys] = {};

static void initWindowAspect()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    SetExitKey(KEY_NULL);   // o Esc do raylib fechava o jogo; fechar e com Alt+F4 ou o X da janela (o Esc volta nos menus)
    for (int k = 0; k < wotm::settings::kKeys; ++k)
        g_appliedVideo[k] = wotm::settings::get(wotm::settings::Key(k));
    // WOTM_MONITOR=<n>: a janela abre no monitor n (0 = o principal), para testar sem cobrir a tela de quem esta trabalhando; o
    // fitWindow e a tela cheia seguem o monitor em que a janela esta
    if (const char *m = std::getenv("WOTM_MONITOR"); m && *m && !g_localCollect)
    {
        const int mon = std::atoi(m);
        if (mon >= 0 && mon < GetMonitorCount())
        {
            const Vector2 p = GetMonitorPosition(mon);
            SetWindowPosition(static_cast<int>(p.x) + 40, static_cast<int>(p.y) + 40);
            std::fprintf(stderr, "[native] janela no monitor %d de %d (WOTM_MONITOR)\n", mon, GetMonitorCount());
        }
    }
    // PS2X_HOSTPROF_WIN=1 (com PS2X_HOSTPROF): o amostrador vigia esta thread (a da janela) em vez da do EE
    if (std::getenv("PS2X_HOSTPROF") && std::getenv("PS2X_HOSTPROF_WIN"))
    {
        void wotmHostProfWatchThisThread();
        wotmHostProfWatchThisThread();
    }
    if (!g_localCollect && !g_hideFps)   // replay (--vu1cmp): sem janela de verdade, fica como esta
        initPresentRate();
    const int disp = wotm::settings::get(wotm::settings::Display);
    if (disp == 2)
        g_fullscreenKind = 2;
    if (disp)
        setFullscreen(true);
    else
        fitWindow();
}

// Opcoes de video mudadas ao vivo (painel "recompiled options", thread do EE): aplicadas aqui, na thread da janela, a cada
// apresentacao. Display: janela, sem borda ou exclusiva (troca de tipo passa pela janela); Rate/Vsync: o ritmo da apresentacao;
// Res/Aspect: o quadro se refaz sozinho no proximo desenho, e a janela (fora da tela cheia) volta ao tamanho do quadro.
static void applyVideoSettings()
{
    using namespace wotm::settings;
    if (g_localCollect || g_hideFps)
        return;
    int now[kKeys];
    for (int k = 0; k < kKeys; ++k)
        now[k] = get(Key(k));
    if (now[Display] != g_appliedVideo[Display])
    {
        if (inFullscreen())
            setFullscreen(false);
        g_fullscreenKind = now[Display] == 2 ? 2 : 1;
        if (now[Display])
            setFullscreen(true);
        std::fprintf(stderr, "[settings] tela: %s\n", now[Display] == 2 ? "cheia exclusiva" : now[Display] ? "cheia sem borda" : "janela");
    }
    if (now[Rate] != g_appliedVideo[Rate] || now[Vsync] != g_appliedVideo[Vsync])
        initPresentRate();
    if (((now[Res] != g_appliedVideo[Res] && now[Res] > 0) || now[Aspect] != g_appliedVideo[Aspect]) && !inFullscreen())
        fitWindow(true);
    if (now[Res] != g_appliedVideo[Res] || now[Aspect] != g_appliedVideo[Aspect] || now[RenderScale] != g_appliedVideo[RenderScale])
        std::fprintf(stderr, "[settings] quadro: %s linhas x %d%% = %d, proporcao %s\n", format(Res, now[Res]).c_str(), now[RenderScale], renderHeight(),
                     format(Aspect, now[Aspect]).c_str());
    if (now[Msaa] != g_appliedVideo[Msaa])
        std::fprintf(stderr, "[settings] MSAA: %d amostras (o driver aceita ate %d)\n", now[Msaa], wotm::msaa::maxSamples());
    for (int k = 0; k < kKeys; ++k)
        g_appliedVideo[k] = now[k];
}

// F11 alterna entre tela cheia e janela (a cada apresentacao, na thread da janela; vale no menu, nos filmes e na luta). Muda a
// opcao DISPLAY MODE (e o settings.ini), que o applyVideoSettings aplica: o painel e o arquivo acompanham. A tela cheia volta a
// ser a do ultimo tipo usado (sem borda ou exclusiva).
static void fullscreenKey()
{
    if (!g_localCollect && !g_hideFps && IsKeyPressed(KEY_F11))
    {
        namespace st = wotm::settings;
        st::set(st::Display, inFullscreen() ? 0 : g_fullscreenKind);
    }
    applyVideoSettings();
    if (!g_localCollect)
        wotm_pad::mouseFrame();   // mouse na camera moderna (wotm_mouse.hpp)
}

// Captura do quadro do jogo na thread do EE (wotmCaptureFrame, chamada pelo wotm_overrides.cpp na entrada de
// timerWaitForMinUpdateRate, ponto comum a rtMain, ShellRtLoop::endField e userintMain, e no sceGsSyncV do fim do laco do
// displayDialog, menu de pause e cia., que nao passa por ele): ali o quadro esta pronto (update, hier,
// particleDraw, HUD) e a logica do seguinte ainda nao comecou. A apresentacao roda em outra thread; lendo a RAM na hora em que
// via o g_frame mudar, pegava as vezes o quadro seguinte pela metade (o _cs do HUD fora do lugar, bloqueador de sombra sem
// marcar: HUD, reticula e sombra piscando).
struct Capture
{
    std::mutex m;
    wotm::Snapshot snap;   // ultimo quadro capturado, ainda nao consumido pela apresentacao
    bool fresh = false;
    std::atomic<bool> on{false};   // a apresentacao desenha nativo (liga na primeira chamada de wotmDrawNative)
    std::atomic<float> wide{1.f};  // Scene::wide da apresentacao (teste de visao das particulas)
};
static Capture &capture()
{
    static Capture c;
    return c;
}
// Interpolacao de quadros (opcao INTERPOLATE FRAMES, WOTM_INTERP; NATIVO.md, "Taxa de apresentacao"): relogio comum as
// duas threads (carimbo Snapshot::hostT) e a margem do corte por esfera, que a janela liga e a captura le (Scene::cullMargin)
static double hostNow()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
static std::atomic<bool> g_interpCull{false};
static constexpr double kFieldSec = 1.001 / 60.0;   // um field do NTSC (o relogio de exibicao segue a cadencia real pelo filtro)
// replay com varios arquivos numa execucao: o proximo wotmDrawNative recomeca a apresentacao do zero (cena, recursos, quadro anterior),
// como num processo novo; sem isso um arquivo herdava o estado do anterior quando a fase era a mesma
static bool g_replayFresh = false;
// replay: textura do filme do telao tirada da VRAM do instantaneo (sem tocador; wotm_vu1check.inc); 0 = nenhuma
static unsigned (*g_replayMovieTex)() = nullptr;
static State *g_state = nullptr;      // o da apresentacao (wotmDrawNative), para as fotos (wotm_foto.inc)
// Ultimo quadro nativo desenhado (o da janela, sem o contador de FPS por cima) num PNG
static bool exportFrame(const char *path)
{
    if (!g_state || g_state->rts[g_state->cur].id == 0)
        return false;
    Image img = LoadImageFromTexture(g_state->rts[g_state->cur].texture);
    ImageFlipVertical(&img);
    const bool ok = ExportImage(img, path);
    UnloadImage(img);
    return ok;
}
// Recursos carregados (nivel e monstros), como sai no log [native] recursos
static std::string resources() { return g_state ? g_state->rtxPath : std::string(); }
// Fades do quadro em montagem (thread do EE): os hooks dos emissores (wotm_overrides.cpp) leem o pacote recem-montado; o
// wotmCaptureFrame os passa ao instantaneo. O Shell::FadeScreen e um laco proprio (sem timerWaitForMinUpdateRate, logo sem
// captura): enquanto roda, a apresentacao le o pacote dele ao vivo (o laco so reescreve o alfa); a proxima captura o desliga.
// Sem redesenho (b = false), o laco pinta o fade de novo por cima do mesmo buffer a cada campo (dois buffers alternados): no buffer da
// vez a tela ja levou os alfas a - 2·passo, a - 4·passo... desde o inicio, e o escurecimento acumula (Scene::drawScreenRects recebe o
// alfa equivalente). g_shellFade: -1 = parado; senao inicio | passo << 8 | acumula << 16.
static std::vector<wotm::ScreenRect> g_pendingRects;
// Por fade pendente: 0 = mandado direto (uiFadeScreen), 1 = na fila do gsPkt, 2 = fila ja despachada (DmaGsPktQueue). No
// fontDmaFontData, os de estado 2 ficam sob o texto (ScreenRect::underText).
static std::vector<uint8_t> g_pendingState;
// HUD do ScreenPolys desde a ultima captura: um quadrado por polyBuildPrim (wotmScreenPoly, wotm::screenPolyFromCall); a captura o
// passa ao instantaneo.
static std::vector<wotm::TextQuad> g_pendingPolys;
// Barras de vida e energia sobre os inimigos (mod WOTM_ENEMYBARS, wotmBarRect) desde a ultima captura: vao para o comeco dos
// retangulos do quadro (os fades saem por cima).
static std::vector<wotm::ScreenRect> g_pendingBars;
// Icones de vida desde a ultima captura (wotmHudAnchor): o ponto do mundo e o centro na tela; g_barAnchor = o do icone cujas barras
// estao saindo agora (wotmBarRect/wotmBarTex o gravam em ScreenRect::anchor)
static std::vector<wotm::HudAnchor> g_pendingAnchors;
static int g_barAnchor = -1;
// colorQuad dos pick-ups destacados como o enableHighlights os deixou, antes do hier (Scene::csColorAtDraw); a captura os passa a coleta.
static std::vector<wotm::Scene::CsColorAtDraw> g_pendingCsColor;
static std::atomic<int> g_shellFade{-1};
// Retangulo e cor do Shell::FadeScreen em curso (wotmShellFade, na entrada) e o alfa que o laco gravou por ultimo (wotmShellFadeStep).
static std::mutex g_shellFadeM;
static wotm::ScreenRect g_shellFadeRect{};
static std::atomic<int> g_shellFadeA{0};
static int g_shellFadeStart = 0, g_shellFadeEnd = 0, g_shellFadeStep = 0, g_shellFadeDir = 1, g_shellFadeSteps = 0;   // thread do EE
// Texto do jogo pelos ganchos da fonte (wotm::FontText; thread do EE). O texto do ultimo fontDmaFontData vai para g_textSent, que a
// apresentacao le a cada desenho (como lia o fontPacket ao vivo: os lacos de dialogo que nao passam pela captura tambem atualizam).
static wotm::FontText g_font;
static std::mutex g_textM;
static std::vector<wotm::TextQuad> g_textSent;   // protegido por g_textM
static std::atomic<bool> g_textHooked{false};    // algum fontDmaFontData passou pelo gancho
// Texto ao vivo (ganchos); false no replay (--vu1cmp: sem jogo, o texto sai do fontPacket do instantaneo) ou antes do primeiro quadro.
static bool liveText(std::vector<wotm::TextQuad> &out)
{
    if (g_localCollect || !g_textHooked.load(std::memory_order_relaxed))
        return false;
    std::lock_guard<std::mutex> lk(g_textM);
    out = g_textSent;
    return true;
}
// Carga da fase (Shell::LoadLevelFiles ate a entrada do rtMain): no PS2 a tela fica parada no ultimo quadro do cartao LOAD (a barra
// de carga e desenhada direto no quadro, sem hier) e o Shell::FadeScreen sem redesenho so a escurece. O nativo nao pode redesenhar
// o cartao nesse meio tempo: a RAM ja tem o nivel novo, e o loadResources troca as texturas pelas dele (o cartao saia com uma
// textura qualquer do nivel, como o brilho verde do power-up no CENTRAL). Enquanto vale, a captura nao publica nada e a
// apresentacao repete o ultimo quadro desenhado, com o fade do Shell::FadeScreen por cima (wotmDrawFrozen).
static std::atomic<bool> g_loadFreeze{false};
// Media movel do tempo de um redesenho na apresentacao (ms, a partir de 0): a interpolacao se suspende se nao couber no periodo
static double g_drawMs = 0.0;

// Fade do Shell::FadeScreen em curso (g_shellFade): o retangulo da entrada com o alfa do passo atual; com acumulo, o alfa
// equivalente no buffer da vez.
static bool shellFadeRect(wotm::ScreenRect &r)
{
    const int sf = g_shellFade.load(std::memory_order_relaxed);
    if (sf < 0)
        return false;
    {
        std::lock_guard<std::mutex> lk(g_shellFadeM);
        r = g_shellFadeRect;
    }
    r.a = static_cast<uint8_t>(std::min(255, std::max(0, g_shellFadeA.load(std::memory_order_relaxed))));
    const int start = sf & 0xFF, step = (sf >> 8) & 0xFF;
    if ((sf >> 16) && step > 0)
    {
        float keep = 1.f;   // fracao da cena que sobra no buffer da vez
        for (int a = r.a; a >= start; a -= 2 * step)
            keep *= 1.f - std::min(a, 0x80) / 128.f;
        r.a = static_cast<uint8_t>(std::lround((1.f - keep) * 0x80));
    }
    return true;
}
} // namespace wotm_live

void wotmLoadFreeze(bool on)
{
    wotm_live::g_loadFreeze.store(on, std::memory_order_relaxed);
    if (on)
    {   // instantaneo ainda nao desenhado: le a RAM de antes da carga, mas seria desenhado depois dela
        wotm_live::Capture &c = wotm_live::capture();
        std::lock_guard<std::mutex> lk(c.m);
        c.fresh = false;
    }
}

// Saida do LevelPickups::enableHighlights (thread do EE): anota o colorQuad do _cs do pick-up destacado de cada um dos jogadores
// (os 4 primeiros m_monsters; highlightList[monstro+0x28] -> no -> Pickup -> cs +0xC). Todos de uma vez porque, na tela dividida, a
// chamada de um jogador devolve o pick-up do outro a 1; o ultimo valor ganha.
void wotmPickupHighlights(const uint8_t *rdram)
{
    if (!rdram)
        return;
    const wotm::Ram ram{rdram, PS2_RAM_SIZE};
    for (uint32_t j = 0; j < 4; ++j)
    {
        const uint32_t m = ram.u32(wotm::sym::shellGame + 0x120380 + 4 * j) & 0x1FFFFFFFu;   // m_monsters[j]
        if (!ram.ok(m, 0x2C))
            continue;
        const uint32_t idx = ram.u32(m + 0x28);
        if (idx >= 16)
            continue;
        const uint32_t node = ram.u32(wotm::sym::x_12LevelPickups_s_highlightPickup + 4 * idx) & 0x1FFFFFFFu;
        const uint32_t pk = ram.ok(node) ? ram.u32(node) & 0x1FFFFFFFu : 0;
        const uint32_t cs = ram.ok(pk, 0x10) ? ram.u32(pk + 0xC) & 0x1FFFFFFFu : 0;
        if (!ram.ok(cs, 0x80))
            continue;
        wotm::Scene::CsColorAtDraw c{cs, {ram.f32(cs + 0x70), ram.f32(cs + 0x74), ram.f32(cs + 0x78), ram.f32(cs + 0x7C)}};
        auto &v = wotm_live::g_pendingCsColor;
        auto it = std::find_if(v.begin(), v.end(), [&](const wotm::Scene::CsColorAtDraw &e) { return e.cs == cs; });
        if (it != v.end())
            *it = c;
        else if (v.size() < 8)
            v.push_back(c);
    }
}

namespace wotm_live
{
static void pushRect(const wotm::ScreenRect &r, uint8_t state)
{
    if (g_pendingRects.size() >= 16)
        return;
    g_pendingRects.push_back(r);
    g_pendingState.push_back(state);
}
}
// Fade da vista inteira, preto, pelos argumentos do emissor: rtFadeScreen(vista, quanto) e uiFadeScreen(quanto) gravam alfa
// 0x80 - quanto (32 bits); os fades do TheGame, o nivel dele. queued: vai pela fila do gsPkt (AddCallToQueue), senao sai direto.
void wotmFadeView(const uint8_t *rdram, int view, uint32_t alpha, bool queued)
{
    wotm::ScreenRect r{};
    if (rdram && wotm::viewFadeRect(wotm::Ram{rdram, PS2_RAM_SIZE}, view, 0, 0, 0, alpha, r))
        wotm_live::pushRect(r, queued ? 1 : 0);
}
// Clarao da usina da 3-MILE (threeMileDrawFlash, chamado pelo threeMileUpdateCountDown e pelo threeMileUpdateTransition): SPRITE
// branco da vista atual (viewCurView), ALPHA_1 0x44, alfa = threeMileFlashAlpha (0x80 = 1,0), pela fila do gsPkt. Lido na entrada:
// o pacote leva o alfa de antes da atualizacao que vem depois da chamada.
void wotmThreeMileFlash(const uint8_t *rdram)
{
    if (!rdram)
        return;
    const wotm::Ram ram{rdram, PS2_RAM_SIZE};
    const uint32_t a = ram.u32(wotm::sym::threeMileFlashAlpha);
    // o retail desenha so na vista corrente (a 0: o threeMileUpdateCountDown roda uma vez por quadro); aqui em todas as vistas
    const int views = std::clamp(int32_t(ram.u32(wotm::sym::viewNumViews)), 1, 4);
    for (int v = 0; v < views && a; ++v)
        if (wotm::ScreenRect r{}; wotm::viewFadeRect(ram, v, 0xFF, 0xFF, 0xFF, a, r))
            wotm_live::pushRect(r, 1);
}
// Saida do TheGame::updateFadeOut / updateFadeOutAndIn: o nivel do fade (+0x120450, ja atualizado) em cada vista; da vista 1 em
// diante so sem filme e sem vista unificada, como o laco deles.
void wotmTheGameFade(const uint8_t *rdram, uint32_t self)
{
    if (!rdram)
        return;
    const wotm::Ram ram{rdram, PS2_RAM_SIZE};
    self &= 0x1FFFFFFFu;
    const uint32_t g = ram.u32(wotm::sym::game) & 0x1FFFFFFFu;
    if (!ram.ok(self + 0x120450) || !ram.ok(g + 0x1203D8))
        return;
    const uint32_t level = ram.u32(self + 0x120450);
    const int views = int32_t(ram.u32(g + 0x1203D8));
    // com os quadrantes (3 e 4 jogadores) o cinema sai em todas as vistas (wotm_split4.inc), entao o fade tambem
    const bool quad = wotm::readViewCtx(ram, 0).h < 224.f;
    for (int v = 0; v < views && v < 5;)
    {
        wotmFadeView(rdram, v, level, true);
        if (++v >= views || (ram.u32(wotm::sym::moviePlaying) != 0 && !quad) || ram.u32(wotm::sym::gUseUnifiedView) != 0)
            break;
    }
}
// Faixas pretas do menu (letterboxDmaData(alfa), mandado pelo userintMain entre o hier(0,0) e o hier(0,1)): dois SPRITEs pretos de 24
// linhas no topo e na base da vista 0, alfa fptoui(alfa * 128), sem A+D (ALPHA_1 em vigor). Ficam com os fades que saem antes do
// texto e da lista de alta prioridade (underText); cobrem a largura da vista, entao em widescreen vao de lado a lado da janela.
void wotmLetterbox(const uint8_t *rdram, float alpha)
{
    int cx, cy, w, h;
    if (!rdram || !wotm::viewGeom(wotm::Ram{rdram, PS2_RAM_SIZE}, 0, cx, cy, w, h))
        return;
    const float a = alpha * 128.f;
    const uint32_t ai = a > 0.f ? static_cast<uint32_t>(a) : 0u;
    const int x0 = cx - (w >> 1), top = cy - (h >> 1), bottom = cy + (h >> 1);
    wotm::ScreenRect r[2] = {wotm::fadeRect(x0, top, x0 + w, top + 24, 0, 0, 0, ai), wotm::fadeRect(x0, bottom - 24, x0 + w, bottom, 0, 0, 0, ai)};
    for (wotm::ScreenRect &x : r)
    {
        x.underText = true;
        wotm_live::pushRect(x, 3);   // ja decidido
    }
}
void wotmGsPktQueueSent()
{
    for (uint8_t &s : wotm_live::g_pendingState)
        if (s == 1) s = 2;
}
void wotmFontSent()
{
    for (size_t i = 0; i < wotm_live::g_pendingRects.size(); ++i)
        if (wotm_live::g_pendingState[i] == 2)
        {
            wotm_live::g_pendingRects[i].underText = true;
            wotm_live::g_pendingState[i] = 3;   // ja decidido
        }
}
void wotmFontInitPacket(uint32_t vram) { wotm_live::g_font.initPacket(vram); }
void wotmFontSetTexId(const uint8_t *rdram, uint32_t node)
{
    if (!rdram) return;
    wotm_live::g_font.setTexId(wotm::Ram{rdram, PS2_RAM_SIZE}, node);
    // atlas da fonte para o painel do treino, que o usa sem texto do jogo no quadro (a luta nao escreve nada)
    if (wotm_training::g_fontTex0.exchange(wotm_live::g_font.tex0) != wotm_live::g_font.tex0)
        std::fprintf(stderr, "[treino] atlas da fonte %016llX\n", static_cast<unsigned long long>(wotm_live::g_font.tex0));
}
void wotmFontColor(const uint8_t *rdram, int font)
{
    if (rdram) wotm_live::g_font.setColor(wotm::Ram{rdram, PS2_RAM_SIZE}, font);
}
static int g_lastFontIdx = 0;   // fonte do ultimo glifo do jogo (na luta, a do "WINS"): a dos numeros de dano
void wotmFontGlyph(const uint8_t *rdram, int font, int ch)
{
    if (rdram) wotm_live::g_font.glyph(wotm::Ram{rdram, PS2_RAM_SIZE}, font, ch);
    if (font == 0 || font == 1) g_lastFontIdx = font;
}
// Tela dividida: o rtMain manda o texto de cada vista no seu fontDmaFontData (vista 0, depois 1, e 2 e 3 com 3 e 4 jogadores). O
// publicado e o de todas: a vista 0 sai com o ultimo texto das outras (um quadro atras ate cada uma mandar o dela), assim a
// apresentacao nunca pega so parte do HUD. Uma vista que nao mandou texto desde a 0 anterior (dialogo: so a 0) fica sem.
void wotmFontDma(const uint8_t *rdram)
{
    std::vector<wotm::TextQuad> q;
    if (!rdram || !wotm_live::g_font.sent(wotm::Ram{rdram, PS2_RAM_SIZE}, q))
        return;
    static std::vector<wotm::TextQuad> text[4];   // ultimo texto de cada vista (tela dividida)
    static unsigned sentSince0 = 0;               // bits: vistas que mandaram texto desde o ultimo da 0
    const wotm::Ram ram{rdram, PS2_RAM_SIZE};
    const uint32_t view = ram.u32(wotm::sym::viewCurView);
    // Quadrantes (3 e 4 jogadores): o Hud poe o texto como numa vista de 224 linhas, a partir do topo do quadrante (a fonte soma a
    // origem da vista). O que cai na metade de baixo dessa vista (o "WINS" em y 200) sobe 224 - h, para o pe do proprio quadrante,
    // sem mudar de tamanho. So quando as outras vistas tambem mandam texto (laco do rtMain): o dialogo de pausa sai so pela vista 0,
    // com a tela inteira.
    static const bool splitLog = std::getenv("WOTM_SPLITLOG") != nullptr;
    static unsigned textLogN = 0;
    if (splitLog && view < 4 && ++textLogN % 240 < 4)
        std::fprintf(stderr, "[split] texto vista %u: %zu glifos\n", view, q.size());
    static bool multi = false;
    if (view == 0)
        multi = (sentSince0 & ~1u) != 0;
    else if (view < 4)
        multi = true;
    if (multi && view < 4)
        if (const wotm::ViewCtx vc = wotm::readViewCtx(ram, int(view)); vc.h < 224.f)
        {
            const float top = vc.cy - 2048.f + 112.f - 0.5f * vc.h;
            float lowX = 1e9f;   // x do texto da metade de baixo (o "WINS"), para o log
            for (wotm::TextQuad &t : q)
                if (std::min(t.y0, t.y1) - top >= vc.h)
                {
                    t.y0 -= 224.f - vc.h;
                    t.y1 -= 224.f - vc.h;
                    lowX = std::min(lowX, std::min(t.x0, t.x1));
                }
            static float lastLowX[4] = {-1.f, -1.f, -1.f, -1.f};
            if (splitLog && lowX < 1e9f && lowX != lastLowX[view])
            {
                std::fprintf(stderr, "[split] wins vista %u: x %.1f (antes %.1f)\n", view, lowX, lastLowX[view]);
                lastLowX[view] = lowX;
            }
        }
    if (!wotm::readViewCtx(ram, 0).full() && view < 4)
    {
        if (view == 0)
        {
            for (int v = 1; v < 4; ++v)
                if (!(sentSince0 & (1u << v)))
                    text[v].clear();
            sentSince0 = 0;
        }
        sentSince0 |= 1u << view;
        text[view] = q;
        q = text[0];
        for (int v = 1; v < 4; ++v)
            q.insert(q.end(), text[v].begin(), text[v].end());
    }
    else
    {
        for (auto &t : text)
            t.clear();
    }
    std::lock_guard<std::mutex> lk(wotm_live::g_textM);
    wotm_live::g_textSent.swap(q);
    wotm_live::g_textHooked.store(true, std::memory_order_relaxed);
}
void wotmScreenPoly(const uint8_t *rdram, int src, int x, int y, uint32_t pd)
{
    wotm::TextQuad t;
    if (rdram && wotm_live::g_pendingPolys.size() < 256 && wotm::screenPolyFromCall(wotm::Ram{rdram, PS2_RAM_SIZE}, src, x, y, pd, t))
        wotm_live::g_pendingPolys.push_back(t);
}
// Caixa (x0, y0, x1, y1, tela do jogo 640 x 224) do quadrado que addPoly(src, x, y, pd) montaria, sem monta-lo
bool wotmScreenPolyBox(const uint8_t *rdram, int src, int x, int y, uint32_t pd, float box[4])
{
    wotm::TextQuad t;
    if (!rdram || !wotm::screenPolyFromCall(wotm::Ram{rdram, PS2_RAM_SIZE}, src, x, y, pd, t))
        return false;
    box[0] = std::min(t.x0, t.x1);
    box[1] = std::min(t.y0, t.y1);
    box[2] = std::max(t.x0, t.x1);
    box[3] = std::max(t.y0, t.y1);
    return true;
}
static int g_barView = 0;   // vista da barra em montagem (wotmHealthIconShift)
static bool g_barHud = false;   // retangulos do HUD nativo de um jogador (wotmBarHud, wotmPlayerHud)
// Teto de retangulos de barra por quadro: ~25 por inimigo (moldura, celulas, retrato, faixas) e ate 3 inimigos por vista; com as 4
// vistas dos quadrantes os 512 de antes acabavam antes da vista 3, que ficava sem barras
constexpr size_t kMaxPendingBars = 2048;
void wotmBarRect(float x0, float y0, float x1, float y1, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (wotm_live::g_pendingBars.size() < kMaxPendingBars)
    {
        wotm::ScreenRect s{x0, y0, x1, y1, r, g, b, a, false, static_cast<int8_t>(g_barView)};
        s.hud = g_barHud;
        s.anchor = static_cast<int16_t>(wotm_live::g_barAnchor);
        wotm_live::g_pendingBars.push_back(s);
    }
}
// Icone de vida sobre um monstro (thread do EE, gancho do Hud::drawHealthIcon): ponto do mundo projetado (world, nullptr = sem) e
// centro do icone na tela do jogo. Com bars, as barras que saem a seguir levam o indice (ate wotmHudAnchor(..., false) de novo).
void wotmHudAnchor(const uint8_t *rdram, float cx, float cy, const float *world, bool bars)
{
    wotm_live::g_barAnchor = -1;
    if (!world || !std::isfinite(world[0]) || !std::isfinite(world[1]) || !std::isfinite(world[2]) || wotm_live::g_pendingAnchors.size() >= 64)
        return;
    wotm::HudAnchor h;
    h.x = cx;
    h.y = cy;
    for (int i = 0; i < 3; ++i) h.w[i] = world[i];
    h.view = static_cast<int8_t>(std::min<uint32_t>(wotm::Ram{rdram, PS2_RAM_SIZE}.u32(wotm::sym::viewCurView), 3u));
    if (bars) wotm_live::g_barAnchor = static_cast<int>(wotm_live::g_pendingAnchors.size());
    wotm_live::g_pendingAnchors.push_back(h);
}
void wotmHudAnchorEnd() { wotm_live::g_barAnchor = -1; }
// Vista dos retangulos de barra que vem a seguir (HUD nativo dos jogadores 3 e 4, wotmPlayerHud)
void wotmBarView(int view) { g_barView = std::clamp(view, 0, 3); }
// Os retangulos que vem a seguir sao do HUD nativo de um jogador (on) ou barras sobre os inimigos
void wotmBarHud(bool on) { g_barHud = on; }
// Retangulo com textura residente do .RTX (retrato do inimigo): tex0 da imagem e o trecho (u0, v0)-(u1, v1) em texels
void wotmBarTex(float x0, float y0, float x1, float y1, uint64_t tex0, float u0, float v0, float u1, float v1)
{
    if (wotm_live::g_pendingBars.size() < kMaxPendingBars)
    {
        wotm::ScreenRect s{x0, y0, x1, y1, 255, 255, 255, 0x80, false, static_cast<int8_t>(g_barView)};   // tinta branca, opaco
        s.hud = g_barHud;
        s.anchor = static_cast<int16_t>(wotm_live::g_barAnchor);
        s.tex0 = tex0;
        s.u0 = u0;
        s.v0 = v0;
        s.u1 = u1;
        s.v1 = v1;
        wotm_live::g_pendingBars.push_back(s);
    }
}
// Barras sobre o inimigo: o Hud::drawHealthIcon prende o x projetado (rx, pixels do GS) entre centro - w/2 + 8 e centro + w/2 - 16 da
// vista, a largura 4:3 dela; em widescreen a barra parava antes da borda (fotos 2026-10-19_132149). Devolve quanto o x muda com o
// mesmo limite na largura da janela (w/2 * wide) e grava a vista corrente para os retangulos que vem a seguir.
// Largura da janela sobre a da tela 4:3 do jogo (1 em 4:3; ~1,33 em 16:9)
float wotmScreenWide()
{
    return std::max(1.f, wotm_live::capture().wide.load(std::memory_order_relaxed));
}
float wotmHealthIconShift(const uint8_t *rdram, float rx)
{
    const wotm::Ram ram{rdram, PS2_RAM_SIZE};
    g_barView = static_cast<int>(std::min<uint32_t>(ram.u32(wotm::sym::viewCurView), 3u));
    g_barHud = false;
    const wotm::ViewCtx vc = wotm::readViewCtx(ram, g_barView);
    const float wide = std::max(1.f, wotm_live::capture().wide.load(std::memory_order_relaxed));
    if (!std::isfinite(rx)) return 0.f;
    const int x = static_cast<int>(rx > 0.f ? rx + 0.5f : rx - 0.5f);
    auto clampTo = [&](float hw) {
        const int lo = static_cast<int>(vc.cx - hw + 8.f), hi = static_cast<int>(vc.cx + hw - 16.f);
        return x < lo ? lo : (hi < x ? hi : x);
    };
    return static_cast<float>(clampTo(0.5f * vc.w * wide) - clampTo(0.5f * vc.w));
}
// Entrada do Shell::FadeScreen(vista, redesenha, ..., r, g, b, inicio, fim, passo): o laco grava inicio, inicio +/- passo... enquanto
// nao chega ao fim, um valor por sceGsSyncV (wotmShellFadeStep); inicio == fim nao desenha nada.
void wotmShellFade(const uint8_t *rdram, int view, uint32_t r, uint32_t g, uint32_t b, int start, int end, int step, bool redraw)
{
    wotm::ScreenRect rc{};
    if (!rdram || start == end || !wotm::viewFadeRect(wotm::Ram{rdram, PS2_RAM_SIZE}, view, r & 0xFF, g & 0xFF, b & 0xFF, 0, rc))
    {
        wotm_live::g_shellFade.store(-1, std::memory_order_relaxed);
        return;
    }
    {
        std::lock_guard<std::mutex> lk(wotm_live::g_shellFadeM);
        wotm_live::g_shellFadeRect = rc;
    }
    wotm_live::g_shellFadeStart = start;
    wotm_live::g_shellFadeEnd = end;
    wotm_live::g_shellFadeStep = step;
    wotm_live::g_shellFadeDir = start < end ? 1 : -1;
    wotm_live::g_shellFadeSteps = 0;
    wotm_live::g_shellFadeA.store(start, std::memory_order_relaxed);
    const bool accumulate = !redraw && start < end;   // sem redesenho, subindo: acumula
    wotm_live::g_shellFade.store((start & 0xFF) | ((step & 0xFF) << 8) | (accumulate ? 1 << 16 : 0), std::memory_order_relaxed);
}
// Entrada do k-esimo sceGsSyncV do laco do Shell::FadeScreen (k a partir de 0): o alfa na tela e o que o laco acabou de gravar,
// inicio +/- passo * k; o seguinte so e gravado na volta.
void wotmShellFadeStep()
{
    using namespace wotm_live;
    g_shellFadeA.store(g_shellFadeStart + g_shellFadeDir * g_shellFadeStep * g_shellFadeSteps, std::memory_order_relaxed);
    ++g_shellFadeSteps;
}
// Volta desse sceGsSyncV: se o laco continua, ele grava logo o valor seguinte.
void wotmShellFadeStepDone()
{
    using namespace wotm_live;
    const int next = g_shellFadeStart + g_shellFadeDir * g_shellFadeStep * g_shellFadeSteps;
    if (g_shellFadeDir > 0 ? next < g_shellFadeEnd : next > g_shellFadeEnd)
        g_shellFadeA.store(next, std::memory_order_relaxed);
}

// A cena (objetos, ceu, HUD, lista de alta prioridade, sombras, luz, camera e paletas) e coletada na saida do hier(0, 0)
// (wotmCaptureScene, gancho do wotm_overrides.cpp): o PS2 desenha os objetos com o estado da travessia, e o gameUpdate e o
// animationRunGlobal que vem depois ja deixam na RAM monstros, paletas e camera do quadro seguinte. Coletando tudo no fim do quadro,
// a malha saia um quadro a frente das luzes dos olhos (lens flare, cuja lista o hier monta) e do proprio PS2. As particulas, os
// flares, o texto e os fades continuam no fim do quadro (wotmCaptureFrame): o particleDraw do PS2 vem depois do gameUpdate.
// Tela dividida (dois jogadores, viewToggleSplitScreen: vistas 0 e 1 lado a lado, 320 x 224 cada): o rtMain roda o hier(v, 0) de
// cada vista no mesmo quadro (a vista 1 ja com o gameUpdate da 0 aplicado). Cada uma sai na saida do seu hier, com a camera, o corte
// e o ceu dela (Scene::vctx); as vistas 1 a 3 vao no instantaneo da 0 (Snapshot::views). Com 3 e 4 jogadores (wotm_split4.inc) sao
// 3 ou 4 vistas em quadrantes de 320 x 112. No agarrao (gUseUnifiedView) a vista 0 vira a tela cheia de novo (viewGrappleConfig,
// viewport 5) e a apresentacao volta a uma vista so.
namespace wotm_live
{
constexpr int kMaxViews = 4;
static wotm::Scene &captureScene(int view = 0)
{
    static wotm::Scene cap[kMaxViews];   // so coleta (RAM -> itens); texturas e malhas ficam com a Scene da apresentacao
    return cap[std::clamp(view, 0, kMaxViews - 1)];
}
static wotm::Snapshot g_sceneSnap[kMaxViews];   // cena do ultimo hier(v, 0) ainda nao publicada (thread do EE)
static bool g_sceneFresh[kMaxViews] = {};
}

void wotmCaptureScene(const uint8_t *rdram, int view)
{
    wotm_live::Capture &c = wotm_live::capture();
    if (!rdram || !c.on.load(std::memory_order_relaxed) || view < 0 || view >= wotm_live::kMaxViews)
        return;
    const wotm::Ram ram{rdram, PS2_RAM_SIZE};
    const uint32_t wc = ram.u32(wotm::addr::world);
    if (!ram.ok(wc, 0x40) || !ram.ok(ram.u32(wc)))
        return;
    wotm::Scene &cap = wotm_live::captureScene(view);
    cap.wide = c.wide.load(std::memory_order_relaxed);
    cap.vctx = wotm::readViewCtx(ram, view);
    static const bool splitLog = std::getenv("WOTM_SPLITLOG") != nullptr;   // diagnostico: vista capturada e geometria dela
    if (splitLog)
    {
        const wotm::GameCamera gc = wotm::readGameCamera(ram, view);
        std::fprintf(stderr, "[split] g=%u vista %d: def %08x viewport %d centro %.0f,%.0f tamanho %.0fx%.0f views=%u unificada=%u camera %d em %.0f,%.0f,%.0f fovy %.1f\n",
                     ram.u32(wotm::sym::g_frame), view, ram.u32(wotm::addr::viewInfo + 0x14u * uint32_t(view) + 4),
                     static_cast<int32_t>(ram.u32(wotm::addr::viewInfo + 0x14u * uint32_t(view) + 8)), cap.vctx.cx, cap.vctx.cy, cap.vctx.w,
                     cap.vctx.h, ram.u32(wotm::sym::viewNumViews), ram.u32(wotm::sym::gUseUnifiedView), gc.ok, gc.pos[0], gc.pos[1], gc.pos[2],
                     gc.fovy);
    }
    cap.csColorAtDraw = wotm_live::g_pendingCsColor;   // tom do pick-up gravado pelo enableHighlights deste quadro (o mesmo da RAM agora)
    cap.cullMargin = wotm_live::g_interpCull.load(std::memory_order_relaxed) ? 1.2f : 1.f;
    cap.collect(ram);
    wotm_live::g_sceneSnap[view] = cap.takeSnapshot(ram, ram.u32(wotm::sym::g_frame));
    wotm_live::g_sceneFresh[view] = true;
}

// Passo do Shell::FadeScreen com redesenho (b = true: hier(0, 0) e sceGsSyncV por passo, sem timerWaitForMinUpdateRate): publica a
// cena desse hier sem desligar o fade (g_shellFade, que a apresentacao desenha por cima). Sem isso o fade-in da tela de carregamento
// (DisplayLoadBackground) passava sem quadro nativo e o cartao LOAD so aparecia ja claro.
void wotmCaptureFadeStep(const uint8_t *rdram)
{
    if (!wotm_live::g_sceneFresh[0] || wotm_live::g_loadFreeze.load(std::memory_order_relaxed))
        return;
    for (bool &f : wotm_live::g_sceneFresh)
        f = false;
    wotm_live::Capture &c = wotm_live::capture();
    if (!rdram || !c.on.load(std::memory_order_relaxed))
        return;
    wotm::Snapshot sn = std::move(wotm_live::g_sceneSnap[0]);
    sn.fx.clear();
    sn.sprites.clear();
    sn.flares = {};
    sn.screenRects.clear();
    sn.screenPolys.clear();
    sn.g = wotm::Ram{rdram, PS2_RAM_SIZE}.u32(wotm::sym::g_frame);
    std::lock_guard<std::mutex> lk(c.m);
    c.snap = std::move(sn);
    c.fresh = true;
}

void wotmMainMenuLogoFrame(uint8_t *rdram);   // wotm_mainmenu.inc: logo do telao so no "press start"
void wotmCaptureFrame(const uint8_t *rdram)
{
    wotmMainMenuLogoFrame(const_cast<uint8_t *>(rdram));   // antes da coleta: o quadro ja sai sem o logo
    // a copia do lens flare vale so para este quadro: o seguinte sem particleDraw (menu, LOAD) sai sem flares
    struct ConsumeFlares { ~ConsumeFlares() { wotm::Particles::consumeFlares(); } } consumeFlares;
    wotm_live::Capture &c = wotm_live::capture();
    std::vector<wotm::ScreenRect> rects;
    rects.swap(wotm_live::g_pendingBars);
    rects.insert(rects.end(), wotm_live::g_pendingRects.begin(), wotm_live::g_pendingRects.end());
    wotm_live::g_pendingRects.clear();
    wotm_live::g_pendingState.clear();
    std::vector<wotm::TextQuad> polys;
    polys.swap(wotm_live::g_pendingPolys);
    std::vector<wotm::HudAnchor> anchors;
    anchors.swap(wotm_live::g_pendingAnchors);
    std::vector<wotm::Scene::CsColorAtDraw> csColor;
    csColor.swap(wotm_live::g_pendingCsColor);
    wotm_live::g_shellFade.store(-1, std::memory_order_relaxed);
    const bool haveScene = wotm_live::g_sceneFresh[0];
    bool haveView[wotm_live::kMaxViews] = {};   // vistas 1 a 3 deste quadro (tela dividida)
    for (int v = 1; v < wotm_live::kMaxViews; ++v)
        haveView[v] = haveScene && wotm_live::g_sceneFresh[v];
    for (bool &f : wotm_live::g_sceneFresh)
        f = false;
    if (!rdram || !c.on.load(std::memory_order_relaxed) || wotm_live::g_loadFreeze.load(std::memory_order_relaxed))
        return;
    const wotm::Ram ram{rdram, PS2_RAM_SIZE};
    const uint32_t wc = ram.u32(wotm::addr::world);
    if (!ram.ok(wc, 0x40) || !ram.ok(ram.u32(wc)))
        return;
    wotm::Scene &cap = wotm_live::captureScene();
    cap.wide = c.wide.load(std::memory_order_relaxed);
    wotm::Snapshot sn;
    std::vector<wotm::FxSystem> fxAll;   // sistemas de todas as vistas (uma copia de sombra so)
    bool split = false;
    if (haveScene) {   // cena do hier(0, 0) deste quadro; particulas de agora
        sn = std::move(wotm_live::g_sceneSnap[0]);
        cap.vctx = sn.vctx;
        cap.collectFx(ram);
        sn.fx = cap.fx.systems;
        sn.flares = cap.fx.flares;
        sn.sprites = cap.sprites;
        sn.screenPolys.clear();   // os quadrados do quadro chegam pelo gancho do polyBuildPrim (polys)
        sn.g = ram.u32(wotm::sym::g_frame);
        fxAll = sn.fx;
        for (int v = 1; v < wotm_live::kMaxViews && !sn.vctx.full(); ++v) {   // tela dividida: cada vista com as particulas cortadas pela dela
            if (!haveView[v])
                continue;
            auto vs = std::make_shared<wotm::Snapshot>(std::move(wotm_live::g_sceneSnap[v]));
            wotm::Scene &capv = wotm_live::captureScene(v);
            capv.wide = cap.wide;
            capv.vctx = vs->vctx;
            capv.collectFx(ram);
            vs->fx = capv.fx.systems;
            vs->flares = capv.fx.flares;
            vs->sprites = capv.sprites;
            vs->screenPolys.clear();
            vs->screenRects.clear();
            vs->g = sn.g;
            for (const wotm::FxSystem &s : vs->fx)
                if (std::none_of(fxAll.begin(), fxAll.end(), [&](const wotm::FxSystem &e) { return e.base == s.base; }))
                    fxAll.push_back(s);
            sn.views[v - 1] = std::move(vs);
            split = true;
        }
    } else {   // quadro sem hier(0, 0) (filme, carga): tudo de agora, como antes
        cap.csColorAtDraw = std::move(csColor);
        cap.vctx = wotm::readViewCtx(ram, 0);
        cap.collect(ram);
        sn = cap.takeSnapshot(ram, ram.u32(wotm::sym::g_frame));
        sn.screenPolys.clear();   // idem (o takeSnapshot le o s_polyBuffer, que e do replay)
    }
    // posicoes, idades e pacotes das particulas deste instante: a apresentacao desenha depois, com o particleMove seguinte ja
    // rodando (contagem do instantaneo com dados vivos = o fecho do pacote lido como particula; 09_magmo_impacto.md)
    // tela dividida: a matriz de cada vista no particleDraw anterior (viewPrevMat ja tem a deste quadro; a de antes fica guardada aqui)
    static float prevMats[wotm_live::kMaxViews][16];
    static bool havePrev = false;
    sn.fxRam = wotm::Particles::shadowRam(ram, split ? fxAll : sn.fx, split && havePrev ? &prevMats[0][0] : nullptr);
    sn.fxRamSize = sn.fxRam ? ram.size : 0;
    for (int k = 0; k < 16 * wotm_live::kMaxViews; ++k)
        prevMats[k / 16][k % 16] = ram.f32(wotm::fxaddr::viewPrevMat + 4u * uint32_t(k));
    havePrev = true;
    for (auto &vs : sn.views)
        if (vs)
        {
            vs->fxRam = sn.fxRam;
            vs->fxRamSize = sn.fxRamSize;
        }
    sn.screenRects = std::move(rects);
    sn.screenPolys.insert(sn.screenPolys.end(), polys.begin(), polys.end());
    sn.anchors = std::move(anchors);
    wotm_trainview::capture(ram);   // modo treino: cascos e capsulas do corpo e vida deste quadro
    sn.fields = ram.u32(wotm::addr::fieldsAtFrameStart);   // carimbo da interpolacao (Snapshot::fields/hostT)
    sn.hostT = wotm_live::hostNow();
    for (auto &vs : sn.views)
        if (vs)
        {
            vs->fields = sn.fields;
            vs->hostT = sn.hostT;
        }
    std::lock_guard<std::mutex> lk(c.m);
    c.snap = std::move(sn);
    c.fresh = true;
}

// Numeros de dano (mod WOTM_DAMAGEPOP): a thread do EE manda cada dano (wotmDamagePopup: ponto no mundo, valor, tipo) e a janela os
// projeta com a camera da cena nativa, subindo e sumindo em ~1 s. Os digitos sao glifos da fonte do jogo (a do "WINS" da luta, a do
// ultimo fontBuildPrim): na chegada, a thread do EE copia a celula e o tamanho dessa fonte (fonts[2], 0x38 cada), o modo do atlas e o
// TEX0 em vigor, e a janela monta os quadrados com a mesma conta do FontText::glyph, tingidos pela cor do GS (0x80 = 1,0): golpe
// branco, objeto ou monstro arremessado (1) laranja, especial (2) ciano e um pouco maior.
namespace wotm_live
{
struct DamageFont
{
    uint64_t tex0 = 0;
    bool rtx = false;              // atlas do .RTX (16 colunas a partir de 0) ou do boot (8 colunas a partir de '!')
    uint16_t cellU = 0, cellV = 0; // celula no atlas (+0/+2), 1/16 de texel
    uint16_t gw = 0, gh = 0;       // glifo no atlas (+4/+6)
    uint16_t sw = 0, sh = 0;       // glifo na tela (+8/+0xA), 1/16 de pixel do GS
};
struct DamagePopup
{
    float p[3];
    float amount;
    int kind;
    float dx;      // desvio lateral, para numeros seguidos nao se cobrirem
    double t0;     // relogio da janela ao chegar (GetTime)
    DamageFont font;
};
static std::mutex g_damageM;
static std::vector<DamagePopup> g_damageNew;   // da thread do EE, ainda sem t0
static std::vector<DamagePopup> g_damageLive;  // so da janela
}
void wotmDamagePopup(const uint8_t *rdram, const float p[3], float amount, int kind)
{
    static uint32_t n = 0;
    if (!rdram)
        return;
    const wotm::Ram ram{rdram, PS2_RAM_SIZE};
    wotm_live::DamageFont f;
    const uint32_t fa = wotm::FontText::kFonts + 0x38u * uint32_t(g_lastFontIdx);
    f.tex0 = wotm_live::g_font.tex0;
    f.rtx = ram.u32(wotm::FontText::kAtlasMode) != 0;
    f.cellU = ram.u16(fa);
    f.cellV = ram.u16(fa + 2);
    f.gw = ram.u16(fa + 4);
    f.gh = ram.u16(fa + 6);
    f.sw = ram.u16(fa + 8);
    f.sh = ram.u16(fa + 0xA);
    if (!f.tex0 || !f.sw || !f.sh)
        return;
    std::lock_guard<std::mutex> lk(wotm_live::g_damageM);
    if (wotm_live::g_damageNew.size() < 64)
        wotm_live::g_damageNew.push_back(wotm_live::DamagePopup{{p[0], p[1], p[2]}, amount, kind, float(int(n++ % 5) - 2), 0.0, f});
}
double wotmDamageIdleSeconds();   // wotm_overrides.cpp: tempo desde o ultimo Monster::update
bool wotmPaused();                // wotm_foto.inc: a thread do EE esta segura pelo F7 (ou modo foto)
// O relogio dos numeros so anda enquanto o jogo roda os Monster::update: na pausa da luta (o menu nao liga o isPaused), nos dialogos
// e no F7 eles param no ar e seguem de onde estavam na volta. No menu de pausa e nos dialogos ficam escondidos (cobririam a lista);
// no F7 continuam a vista, para a foto.
// Na tela dividida, uma chamada por vista: a camera dela e o retangulo dela na janela (vw); o tamanho segue a janela inteira.
static void drawDamagePopups(wotm::Scene &scene, const Camera3D &cam, int rw, int rh, Rectangle vw)
{
    const bool frozen = wotmDamageIdleSeconds() > 0.1;
    using namespace wotm_live;
    static double s_clock = 0.0, s_real = -1.0;
    const double real = GetTime();
    if (s_real >= 0.0 && !frozen)
        s_clock += std::min(real - s_real, 0.1);
    s_real = real;
    const double now = s_clock;
    {
        std::lock_guard<std::mutex> lk(g_damageM);
        for (DamagePopup &d : g_damageNew)
        {
            d.t0 = now;
            g_damageLive.push_back(d);
        }
        g_damageNew.clear();
    }
    constexpr double kLife = 1.1;
    g_damageLive.erase(std::remove_if(g_damageLive.begin(), g_damageLive.end(), [&](const DamagePopup &d) { return now - d.t0 > kLife; }),
                       g_damageLive.end());
    if (g_damageLive.empty() || (frozen && !wotmPaused()))
        return;
    const wotm::GsScreenRect gr = wotm::gsScreenRect(float(rw), float(rh));
    std::vector<wotm::TextQuad> quads;
    for (const DamagePopup &d : g_damageLive)
    {
        // atras da camera: nao desenha
        const float v[3] = {d.p[0] - cam.position.x, d.p[1] - cam.position.y, d.p[2] - cam.position.z};
        const float f[3] = {cam.target.x - cam.position.x, cam.target.y - cam.position.y, cam.target.z - cam.position.z};
        if (v[0] * f[0] + v[1] * f[1] + v[2] * f[2] <= 0.f)
            continue;
        Vector2 s = GetWorldToScreenEx(Vector3{d.p[0], d.p[1], d.p[2]}, cam, int(vw.width), int(vw.height));
        if (s.x < 0.f || s.y < 0.f || s.x > vw.width || s.y > vw.height)
            continue;
        s.x += vw.x;
        s.y += vw.y;
        const float age = float(now - d.t0), k = age / float(kLife);
        // entra grande e assenta (0,15 s), sobe ~7% da altura, some no ultimo terco
        const float pop = age < 0.15f ? 1.f + 0.6f * (1.f - age / 0.15f) : 1.f;
        const float alpha = k < 0.66f ? 1.f : std::max(0.f, (1.f - k) / 0.34f);
        char txt[16];
        if (d.amount >= 9.95f || std::fabs(d.amount - std::round(d.amount)) < 0.05f)   // inteiro sem ",0"
            std::snprintf(txt, sizeof txt, "%d", int(d.amount + 0.5f));
        else
            std::snprintf(txt, sizeof txt, "%.1f", d.amount);
        // digito visivel com ~2,6% da altura do quadro (x1,25 no especial): 80% do texto anterior (medido a 720p); o glifo ocupa ~70%
        // do quadrado (WINS)
        const float hPx = float(rh) * (0.033f / 0.7f) * pop * (d.kind == 2 ? 1.25f : 1.f);
        const float gy = hPx * 224.f / float(rh), scale = gy / (float(d.font.sh) / 16.f);
        const float gw = float(d.font.sw) / 16.f * scale;   // largura de um glifo (unidades de 640)
        const float advance = gw * 0.8f;                    // a fonte e larga (contorno): glifos encostados como no HUD
        const int n = int(std::strlen(txt));
        const float cx = (s.x - gr.x0) * 640.f / gr.w + d.dx * gw * 0.6f, cy = s.y * 224.f / float(rh) - 224.f * 0.07f * k;
        float x = cx - 0.5f * (advance * float(n - 1) + gw);
        const float y = cy - 0.5f * gy;
        // cor do GS (0x80 = 1,0; acima clareia o glifo): golpe como o texto do HUD, arremesso laranja, especial ciano
        const uint8_t r = d.kind == 1 ? 0xFF : d.kind == 2 ? 0x50 : 0x80, g = d.kind == 1 ? 0x90 : d.kind == 2 ? 0xD0 : 0x80,
                      b = d.kind == 1 ? 0x20 : d.kind == 2 ? 0xFF : 0x80, a = uint8_t(0x80 * alpha);
        for (int i = 0; i < n; ++i, x += advance)
        {
            int c = int8_t(txt[i]), row, column, base;
            if (d.font.rtx) { row = c >> 4; column = c & 15; base = 0x10; }
            else { const int q = c - 0x21; row = q >> 3; column = q & 7; base = 8; }
            const float u0 = float(d.font.cellU * column + base) / 16.f, v0 = float(d.font.cellV * row + base) / 16.f;
            wotm::TextQuad t{};
            t.u0 = u0; t.v0 = v0; t.u1 = u0 + float(d.font.gw) / 16.f; t.v1 = v0 + float(d.font.gh) / 16.f;
            t.x0 = x; t.y0 = y; t.x1 = x + gw; t.y1 = y + gy;
            t.r = r; t.g = g; t.b = b; t.a = a;
            t.tex0 = d.font.tex0;
            quads.push_back(t);
        }
    }
    scene.drawQuads(quads, float(rw), float(rh));
}

#include "wotm_narrator.inc"   // nome do monstro escolhido na selecao (antes do wotm_padsel.inc, que o chama)
namespace wotm_levelsel { bool resolveRandom(uint8_t *rdram); }   // wotm_levelsel.inc: RANDOM da selecao de fase (o padsel a chama)
// Escolha de lados do 2 jogadores no telao do menu (ganchos do menu + desenho; wotmPadSelInstall, chamado do wotm_overrides.cpp)
#include "wotm_padsel.inc"
#include "wotm_levelsel.inc"   // selecao de fase com as fases escondidas e a RANDOM (depois do wotm_padsel.inc: plano do telao)
// Menu principal em texto, com o "Recompiled options" (wotmMainMenuInstall, chamado do wotm_overrides.cpp)
#include "wotm_gamescreens.inc"   // mouse nas telas do menu que continuam as do jogo (antes do wotm_mainmenu.inc, que o chama)
#include "wotm_menufont.inc"   // palavras da lista do menu com as letras do jogo (antes do wotm_mainmenu.inc)
#include "wotm_rumble.inc"   // vibracao dos gamepads por XInput (antes do wotm_mainmenu.inc, que a instala)
#include "wotm_mainmenu.inc"
#include "wotm_optscreen.inc"   // tela de opcoes do jogo com o texto redesenhado (depois do wotm_mainmenu.inc)
#include "wotm_pausemenu.inc"   // menu de pausa: sem o CONTROL SCHEME, com mouse
#include "wotm_legend.inc"   // legenda de baixo e dica grande dos menus nativos (depois do wotm_optscreen.inc: fonte)
#include "wotm_photo.inc"    // modo foto (F9 ou L3 + R3; depois do wotm_legend.inc: legenda)

double wotmThreadCpuMs();   // ps2_hostprof.cpp: CPU da thread atual (usuario + kernel)

static bool wotmDrawNative(const uint8_t *rdram)
{
    static wotm_live::State S;
    if (wotm_live::g_replayFresh)
    {
        wotm_live::g_replayFresh = false;
        S.scene.invalidate();
        for (RenderTexture2D &r : S.rts)
            if (r.id != 0)
                UnloadRenderTexture(r);
        for (RenderTexture2D &r : S.vrts)
            if (r.id != 0)
                UnloadRenderTexture(r);
        S.ms.release();
        for (wotm::msaa::Target &t : S.vms)
            t.release();
        S.~State();
        new (&S) wotm_live::State();
    }
    wotm_live::g_state = &S;
    S.scene.movieTexture = []() -> unsigned {   // objeto do menu com texId 999
#ifdef WOTM_HAVE_FMV
        if (const unsigned id = wotm::fmv::Player::get().screenTexture()) return id;
#endif
        return wotm_live::g_localCollect && wotm_live::g_replayMovieTex ? wotm_live::g_replayMovieTex() : 0u;
    };
    S.scene.screenTexture = []() { return S.rts[S.cur ^ 1].texture.id; };   // texId 0: quadro anterior
    // quadro nativo: renderHeight de altura (WOTM_RES) na proporcao da tela (wotm_live::viewAspect); a tela do GS e a faixa 4:3 central
    const int RH = wotm_live::renderHeight(), RW = static_cast<int>(std::lround(RH * wotm_live::viewAspect()));
    S.scene.wide = static_cast<float>(RW) / (RH * 4.f / 3.f);
    wotm_live::Capture &cap = wotm_live::capture();
    cap.wide.store(S.scene.wide, std::memory_order_relaxed);
    cap.on.store(true, std::memory_order_relaxed);
    static bool filterInstalled = false;
    if (!filterInstalled)
    {
        filterInstalled = true;
        wotm_live::SetUnhandledExceptionFilter(wotm_live::crashFilter);
    }
    const wotm::Ram ram{rdram, PS2_RAM_SIZE};
    // Carga da fase (g_loadFreeze): repete o ultimo quadro desenhado (S.rts[S.cur], intocado) com o fade do Shell::FadeScreen por cima,
    // no outro buffer; nada de instantaneo nem de troca de recursos ate o rtMain comecar.
    if (wotm_live::g_loadFreeze.load(std::memory_order_relaxed) && !wotm_live::g_localCollect && S.rts[S.cur].id != 0)
    {
        {
            std::lock_guard<std::mutex> lk(cap.m);
            cap.fresh = false;
        }
        const RenderTexture2D &base = S.rts[S.cur];
        RenderTexture2D &out = S.rts[S.cur ^ 1];
        if (out.id != 0 && (out.texture.width != base.texture.width || out.texture.height != base.texture.height))
        {
            UnloadRenderTexture(out);
            out = RenderTexture2D{};
        }
        if (out.id == 0)
        {
            out = LoadRenderTexture(base.texture.width, base.texture.height);
            SetTextureFilter(out.texture, TEXTURE_FILTER_BILINEAR);
            SetTextureWrap(out.texture, TEXTURE_WRAP_CLAMP);
        }
        const float bw = static_cast<float>(base.texture.width), bh = static_cast<float>(base.texture.height);
        BeginTextureMode(out);
        rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
        BeginBlendMode(BLEND_CUSTOM);
        DrawTexturePro(base.texture, Rectangle{0.f, 0.f, bw, -bh}, Rectangle{0.f, 0.f, bw, bh}, Vector2{0.f, 0.f}, 0.f, WHITE);
        EndBlendMode();
        S.scene.screenRects.clear();
        if (wotm::ScreenRect r{}; wotm_live::shellFadeRect(r))
            S.scene.screenRects.push_back(r);
        S.scene.drawScreenRects(bw, bh);
        EndTextureMode();
        const float sw = static_cast<float>(GetScreenWidth()), sh = static_cast<float>(GetScreenHeight());
        const float k = std::min(sw / bw, sh / bh);
        rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
        BeginBlendMode(BLEND_CUSTOM);
        DrawTexturePro(out.texture, Rectangle{0.f, 0.f, bw, -bh}, Rectangle{(sw - bw * k) * 0.5f, (sh - bh * k) * 0.5f, bw * k, bh * k},
                       Vector2{0.f, 0.f}, 0.f, WHITE);
        EndBlendMode();
        return true;
    }
    const uint32_t wc = ram.u32(wotm::addr::world);
    if (!ram.ok(wc, 0x40) || !ram.ok(ram.u32(wc)))
        return false;

    const uint32_t sig = wotm_live::levelSignature(ram);
    if (sig != S.levelSig)
    {
        S.scene.invalidate();
        S.levelSig = sig;
        S.rtxChosen = false;
        S.snap = wotm::Snapshot{};
        S.prev = wotm::Snapshot{};
        S.prev2 = wotm::Snapshot{};
    }
    // Quadro novo do jogo: o instantaneo capturado na thread do EE (wotmCaptureFrame). No replay (--vu1cmp) nao ha jogo rodando e a
    // RAM do instantaneo e fixa: a coleta e aqui mesmo. Os desenhos seguintes repetem o instantaneo.
    wotm::Snapshot sn;
    bool fresh = false;   // instantaneo novo nesta apresentacao (redesenho, abaixo)
    if (wotm_live::g_localCollect)
    {
        const uint32_t gNow = ram.u32(wotm::sym::g_frame);
        if (!S.snap.valid || gNow != S.snap.g)
        {
            wotm_live::g_stage = "collect";
            S.scene.collect(ram);
            sn = S.scene.takeSnapshot(ram, gNow);
        }
    }
    else
    {
        std::lock_guard<std::mutex> lk(cap.m);
        if (cap.fresh)
        {
            sn = std::move(cap.snap);
            cap.fresh = false;
        }
    }
    if (sn.valid)
    {
        // Sem fase de verdade (carregando, tela vazia): fica o quadro do GS. Com dialogo na lista de alta prioridade (moldura do
        // McBackground: "salvar?", lista de saves) vale mesmo com a fase quase vazia: em Tsunopolis, depois do disco voador levar o
        // monstro na cena final, sobram 18 itens e a pergunta de salvar saia toda preta.
        // A conta inclui o que o collect recortou pela vista (Snapshot::culled): olhando para o espaco no UFO, ou com a camera
        // colada numa parede, sobravam menos de 20 itens visiveis e a luta inteira (HUD junto) ficava preta.
        if ((sn.items.size() + sn.culled < 20 && sn.hp.empty()) || !sn.cam.ok)
        {            S.snap = wotm::Snapshot{};
            return false;
        }
        {
            // nivel + monstros: nomes e bases das paletas vem de fileStatus (idempotente; refaz so quando mudam)
            std::string log;
            wotm_live::g_stage = "loadResources";
            S.rtxChosen = S.scene.loadResources(ram, "", &log);
            if (!log.empty() && log != S.rtxPath)
            {
                S.rtxPath = log;
                std::fprintf(stderr, "[native] recursos: %s\n", log.c_str());
                wotmBootDone("stage on screen");
            }
        }
        S.prev2 = std::move(S.prev);   // P e A da interpolacao
        S.prev2.fxRam.reset();         // devolve a copia das particulas ao pool (Particles::shadowRam)
        for (auto &vs : S.prev2.views)
            if (vs)
                vs.reset();
        S.prev = std::move(S.snap);
        S.snap = std::move(sn);
        fresh = true;
        if (S.snap.hostT > 0.0)
        {
            // relogio de exibicao: O = hostT - fields * periodo, filtrado (a captura varia alguns ms com o trabalho do EE no quadro)
            const double target = S.snap.hostT - double(S.snap.fields) * wotm_live::kFieldSec;
            if (!S.interpHaveO || std::fabs(target - S.interpO) > 2.0 * wotm_live::kFieldSec)
                S.interpO = target;
            else
                S.interpO += 0.05 * (target - S.interpO);
            S.interpHaveO = true;
        }
    }
    // Camera livre com o jogo pausado (F7): a coleta da captura recortou pela vista congelada. Com a thread do EE parada a RAM nao
    // muda, entao a apresentacao coleta de novo, uma vez por quadro pausado, sem o recorte (o resto do instantaneo fica o mesmo).
    {
        static uint32_t uncullG = ~0u;
        if (S.snap.valid && (g_wotmFreeCam.load(std::memory_order_relaxed) || wotmPhotoActive()) && wotmPaused() && S.snap.g != uncullG)
        {
            uncullG = S.snap.g;
            static wotm::Scene rc;
            rc.wide = cap.wide.load(std::memory_order_relaxed);
            rc.fovCull = false;
            rc.fx.fovCull = false;
            rc.fx.frameGate = false;   // a RAM parada e a do quadro capturado, que teve particleDraw
            wotm_live::g_stage = "collect (camera livre)";
            rc.collect(ram);
            wotm::Snapshot un = rc.takeSnapshot(ram, S.snap.g);
            un.screenRects = S.snap.screenRects;
            un.screenPolys = S.snap.screenPolys;
            un.flares = S.snap.flares;
            un.anchors = S.snap.anchors;
            un.cam = S.snap.cam;
            S.snap = std::move(un);
            fresh = true;
        }
    }
    if (!S.snap.valid)
        return false;
    if (!S.snap.cam.ok)
        return false;
    // camera livre pausada: o show ja recebe a pose (olho das faces de tras, neblina, sprites), e a cena perde o que segue a camera do jogo
    // modo foto (F9, L3 + R3) no lugar da camera livre de debug enquanto estiver ligado
    wotm_freecam::View freeView = wotm_photo::update(S.snap.cam);
    if (!freeView.on)
        freeView = wotm_freecam::update(S.snap.cam);
    wotmFreeCamLegend(freeView);   // F5: controles na legenda de icones
    const wotm::GameCamera gameCam = S.snap.cam;
    if (freeView.override)
        S.snap.cam = wotm_freecam::camera(gameCam, freeView.pose, freeView.zoom);
    const wotm::GameCamera freeCam = S.snap.cam;
    // WOTM_NOHUD=1: sempre sem HUD nem texto do jogo, como o modo foto com o HUD escondido (fotos de fase com WOTM_SHOTS)
    static const bool noHud = [] { const char *e = std::getenv("WOTM_NOHUD"); return e && e[0] == '1'; }();
    const bool photoHide = (freeView.override && freeView.photo && freeView.hideHud) || noHud;
    // Redesenho: so com quadro novo do jogo (ou instantaneo novo da camera livre), fade do Shell::FadeScreen mudando, camera livre
    // pausada ou janela redimensionada; parado sem quadro novo (pausa, lacos sem captura) volta a redesenhar a ~60/s. Nas outras
    // apresentacoes (monitor acima de 60 Hz, initPresentRate) a ultima imagem e so reapresentada: o custo fica o de 60 e o texId 0
    // (quadro anterior, mapa de ambiente/CLOAK) continua sendo o desenho anterior, nao uma copia do mesmo quadro.
    const double nowT = GetTime();
    static double lastFreshT = 0.0, lastRenderT = 0.0;
    static long s_renders = 0, s_blends = 0;   // redesenhos e, deles, os misturados pela interpolacao (PS2X_STATS, linha [native])
    static double s_drawMs[5] = {}, s_drawCpu[5] = {};   // redesenhos com 1 a 4 vistas desde a ultima linha [native]: parede e CPU da thread
    static long s_drawN[5] = {};
    int nViews = 1;
    if (fresh)
        lastFreshT = nowT;
    wotm::ScreenRect fadeNow{};
    const bool hasFade = wotm_live::shellFadeRect(fadeNow);
    static wotm::ScreenRect lastFade{};
    static bool lastHasFade = false;
    const bool fadeChanged = hasFade != lastHasFade || (hasFade && std::memcmp(&fadeNow, &lastFade, offsetof(wotm::ScreenRect, underText)) != 0);
    const RenderTexture2D &shown = S.rts[S.cur];
    const bool sizeOk = shown.id != 0 && shown.texture.width == RW && shown.texture.height == RH;
    // WOTM_INTERP=redraw: redesenha em toda apresentacao (etapa 0 da interpolacao: mede o custo de redesenhar na taxa do monitor)
    static const bool s_redrawAll = [] {
        const char *e = std::getenv("WOTM_INTERP");
        return e && std::strstr(e, "redraw") != nullptr;
    }();
    // Interpolacao de quadros (opcao INTERPOLATE FRAMES, ligada por padrao; NATIVO.md, "Taxa de apresentacao"): com a
    // apresentacao acima de 60 Hz (ou ate 60 com o jogo a 30: slowGame), cada apresentacao desenha a mistura de A (S.prev) e B
    // (S.snap) no instante D do relogio de exibicao, um quadro
    // do jogo atras do mais novo (mais uma margem, para a captura atrasada ainda chegar antes do alfa bater em 1). Sem quadro novo o
    // alfa para em 1 (B; sem extrapolar). Fora: replay, camera livre ou foto, pausa, A sem carimbo, buraco de fields ou de relogio
    // (dialogo, carga), e no treino com esferas ou contornos (desenhados com a pose de B).
    float alpha = 1.f;
    bool interpOn = false, interpOld = false;   // interpOld: o par e (P, A), nao (A, B)
    {
        static const float fixedAlpha = [] { const char *e = std::getenv("WOTM_INTERP_ALPHA"); return e && *e ? std::clamp(float(std::atof(e)), 0.f, 1.f) : -1.f; }();
        static const double marginSec = [] { const char *e = std::getenv("WOTM_INTERP_DELAY"); return (e && *e ? std::atof(e) : 3.0) / 1000.0; }();
        // Jogo a 30 (quadros de 2 fields): metade ou mais dos ultimos 64 pares com vao >= 2 liga, ate 1/8 desliga. Com a apresentacao
        // ate 60 Hz a mistura so vale nesse caso (com o jogo a 60 ela so somaria um quadro de atraso).
        static bool slowGame = false;
        static uint64_t gapBits = 0;   // 1 = par com vao >= 2, um bit por quadro novo
        static uint32_t lastGapG = 0;
        if (S.snap.g != lastGapG && S.prev.valid && S.snap.fields > S.prev.fields)
        {
            lastGapG = S.snap.g;
            gapBits = (gapBits << 1) | (S.snap.fields - S.prev.fields >= 2 ? 1u : 0u);
            int slow = 0;
            for (uint64_t b = gapBits; b; b &= b - 1)
                ++slow;
            if (!slowGame && slow >= 32)
                slowGame = true;
            else if (slowGame && slow <= 8)
                slowGame = false;
        }
        const int presentHz = wotm::settings::g_presentHz.load(std::memory_order_relaxed);
        // Desenho que nao cabe no periodo da apresentacao (wotm_live::g_drawMs, media do redesenho): com a mistura o vsync cairia para a
        // metade, pior que sem ela. Acima de 85% do periodo suspende; volta abaixo de 60%.
        static bool tooSlow = false;
        if (presentHz > 0)
        {
            const double periodMs = 1000.0 / presentHz, ms = wotm_live::g_drawMs;
            const bool was = tooSlow;
            if (!tooSlow && ms > 0.85 * periodMs)
                tooSlow = true;
            else if (tooSlow && ms < 0.6 * periodMs)
                tooSlow = false;
            if (tooSlow != was)
                std::fprintf(stderr, "[interp] redesenho de %.2f ms para um periodo de %.2f ms: mistura %s\n", ms, periodMs, tooSlow ? "suspensa" : "de volta");
        }
        const bool want = wotm::settings::on(wotm::settings::Interp) && (presentHz > 60 || (presentHz > 0 && slowGame)) && !tooSlow &&
                          !wotm_live::g_localCollect;
        wotm_live::g_interpCull.store(want, std::memory_order_relaxed);
        const bool trainOverlay = wotm_training::g_on.load(std::memory_order_relaxed) &&
                                  (wotm_training::g_opts.load(std::memory_order_relaxed) &
                                   (wotm_training::kShowAttacks | wotm_training::kShowHulls | wotm_training::kShowCapsule));
        // par (x, y) misturavel: os dois carimbados, vao de 1 a 4 fields, sem buraco de relogio
        auto pairOk = [](const wotm::Snapshot &x, const wotm::Snapshot &y) {
            const int gap = int(y.fields - x.fields);
            return x.valid && y.valid && x.hostT > 0.0 && y.hostT > 0.0 && gap > 0 && gap <= 4 && y.hostT - x.hostT < 0.1;
        };
        const wotm::Snapshot &A = S.prev, &B = S.snap;
        if (want && !freeView.override && !wotmPaused() && !trainOverlay && S.interpHaveO && pairOk(A, B))
        {
            interpOn = true;
            // instante exibido, em fields: um quadro (o vao de A a B) atras do relogio do jogo, menos a margem
            const int gap = int(B.fields - A.fields);
            const double D = (wotm_live::hostNow() - S.interpO - marginSec) / wotm_live::kFieldSec - double(gap);
            if (fixedAlpha >= 0.f)
                alpha = fixedAlpha;
            else if (D < double(A.fields) && !B.views[0] && !B.views[1] && !B.views[2] && pairOk(S.prev2, A) && D >= double(S.prev2.fields))
            {
                // B chegou adiantado: o instante ainda e entre P e A
                interpOld = true;
                alpha = float((D - double(S.prev2.fields)) / double(A.fields - S.prev2.fields));
            }
            else
                alpha = float(std::clamp((D - double(A.fields)) / double(gap), 0.0, 1.0));
        }
        static const bool interpLog = std::getenv("WOTM_INTERPLOG") != nullptr;
        if (interpLog && want)
            std::fprintf(stderr, "[interp] t=%.4f g=%u fA=%u fB=%u alfa=%.3f%s%s%s\n", wotm_live::hostNow(), B.g, interpOld ? S.prev2.fields : A.fields,
                         interpOld ? A.fields : B.fields, alpha, interpOn ? "" : " (desligada)", interpOld ? " (P-A)" : "", fresh ? " novo" : "");
    }
    const bool rerender = s_redrawAll || fresh || fadeChanged || freeView.override || !sizeOk || wotm_live::g_localCollect ||
                          (interpOn && alpha - (interpOld ? 2.f : 0.f) != S.lastAlpha) || (nowT - lastFreshT >= 0.04 && nowT - lastRenderT >= 1.0 / 60.0 - 0.001);
    if (rerender)
    {
        S.lastAlpha = interpOn ? alpha - (interpOld ? 2.f : 0.f) : -1.f;   // o par (P, A) fica abaixo de -1
        s_blends += interpOn && alpha < 1.f;
    }
    wotm::GameCamera gc = S.snap.cam;
    if (rerender)
    {
        lastRenderT = nowT;
        // tempo do redesenho (CPU, ate o fim deste bloco), media movel: a trava da interpolacao (tooSlow) o le
        struct DrawTimer
        {
            double t0 = wotm_live::hostNow();
            ~DrawTimer()
            {
                // cada amostra vale no maximo dois periodos: um redesenho isolado (carga de texturas, troca de fase) nao suspende sozinho
                const int hz = wotm::settings::g_presentHz.load(std::memory_order_relaxed);
                const double ms = std::min((wotm_live::hostNow() - t0) * 1000.0, hz > 0 ? 2000.0 / hz : 1000.0);
                wotm_live::g_drawMs += (ms - wotm_live::g_drawMs) * 0.05;   // parte de 0: o primeiro redesenho (texturas) nao pesa sozinho
            }
        } drawTimer;
        ++s_renders;
        const double tDraw0 = GetTime(), cpuDraw0 = wotmThreadCpuMs();   // custo do redesenho por numero de vistas (PS2X_STATS)
        lastFade = fadeNow;
        lastHasFade = hasFade;
        // Tela dividida (Snapshot::views): cada vista e desenhada numa textura da metade do quadro, com a camera dela, como se fosse a
        // tela inteira (o que mede o framebuffer, como os flares e a proporcao da camera, vale sem mudar), e as duas sao compostas lado
        // a lado. Com 3 e 4 jogadores (vistas de 320 x 112, wotm_split4.inc) as texturas sao quadrantes: a vista v na coluna v & 1 e
        // na linha v >> 1; com 3, a vista 2 e a linha de baixo inteira (viewDef de 640 x 112, strip). A camera livre (F5) mostra so a
        // vista 0 em tela cheia.
        bool viewOk[4] = {true, false, false, false};
        bool split = false;
        for (int v = 1; v < 4; ++v)
        {
            const auto &vs = S.snap.views[v - 1];
            viewOk[v] = vs && vs->valid && vs->cam.ok && !S.snap.vctx.full() && !freeView.override;
            split = split || viewOk[v];
            nViews += viewOk[v];
        }
        const bool quad = split && S.snap.vctx.h < 224.f;
        {
            static const bool splitLog = std::getenv("WOTM_SPLITLOG") != nullptr;
            if (splitLog)
                std::fprintf(stderr, "[split] desenho g=%u vistas %d%d%d full=%d -> %d%s\n", S.snap.g, viewOk[1], viewOk[2], viewOk[3],
                             S.snap.vctx.full(), split, quad ? " (quadrantes)" : "");
        }
        const bool strip = quad && viewOk[2] && !viewOk[3] && S.snap.views[1]->vctx.w >= 640.f;
        const int VW = RW / 2, VH = quad ? RH / 2 : RH;
        auto viewW = [&](int v) { return strip && v == 2 ? RW : VW; };   // largura da textura da vista v
        // barras sobre os inimigos: no draw de cada vista, antes do HUD (Scene::barPass); o drawBar ja quer os cortes da tela dividida
        S.scene.splitEdges = split;
        S.scene.stripView = strip ? 2 : -1;
        S.scene.barsUnderHud = true;
        // MSAA (opcao ANTI-ALIASING, WOTM_MSAA): desenho num framebuffer multiamostra, resolvido na textura de sempre (wotm_msaa.hpp)
        const int msaaSamples = wotm_live::g_force43 ? 0 : wotm::settings::get(wotm::settings::Msaa);
        wotm::g_beforeDepthRead = wotm::msaa::readDepthIfMsaa;
        wotm::g_depthReadIsMsaa = [] { return wotm::msaa::g_current != nullptr; };
        S.scene.setTexFilter(wotm_live::g_force43 ? 0 : wotm::settings::get(wotm::settings::TexFilter));
        if (msaaSamples <= 1)
            for (wotm::msaa::Target &t : S.vms)
                if (t.on())
                    t.release();
        if (wotm::drawProfOn())
            ++wotm::drawProf().redraws;
        wotm::ProfScope psFrame(wotm::DrawProf::RFrame);
        wotm::GpuTimer::Scope gpuScope;
        // interpolacao na tela dividida: as barras e o icone de vida do jogo (no instantaneo da vista 0) e os flares de cada vista vao
        // para a camera misturada dela (Scene::blendAnchors, blendFlares no showBlend), numa copia feita vista a vista antes do desenho
        // dela; o icone (polys da janela inteira) volta a Scene depois da vista 0
        static std::vector<wotm::ScreenRect> barsMix;
        static std::vector<wotm::TextQuad> polysMix;
        static wotm::Particles::FlareFrame flaresMix[4];
        const bool splitMix = interpOn && split;
        if (splitMix)
        {
            barsMix = S.snap.screenRects;
            polysMix = S.snap.screenPolys;
        }
        Camera3D popCam[4]{};   // camera de cada vista (numeros de dano na tela dividida)
        auto drawView = [&](RenderTexture2D &vrt, const wotm::GameCamera &vc) {
            wotm::ProfScope psView(wotm::DrawProf::RView);
            const int W = viewW(static_cast<int>(&vrt - S.vrts));
            if (vrt.id != 0 && (vrt.texture.width != W || vrt.texture.height != VH))
            {
                UnloadRenderTexture(vrt);
                vrt = RenderTexture2D{};
            }
            if (vrt.id == 0)
            {
                vrt = LoadRenderTexture(W, VH);
                SetTextureFilter(vrt.texture, TEXTURE_FILTER_BILINEAR);
                SetTextureWrap(vrt.texture, TEXTURE_WRAP_CLAMP);
            }
            wotm::msaa::Target &vms = S.vms[&vrt - S.vrts];
            const bool vmsOn = vms.ensure(W, VH, msaaSamples);
            BeginTextureMode(vmsOn ? vms.render() : vrt);
            wotm::msaa::g_current = vmsOn ? &vms : nullptr;
            ClearBackground(BLACK);
            rlSetClipPlanes(0.5, 30000.0);
            Camera3D vc3{};
            vc3.position = {vc.pos[0], vc.pos[1], vc.pos[2]};
            vc3.target = {vc.pos[0] + vc.fwd[0], vc.pos[1] + vc.fwd[1], vc.pos[2] + vc.fwd[2]};
            vc3.up = {vc.up[0], vc.up[1], vc.up[2]};
            vc3.fovy = vc.fovy;
            vc3.projection = CAMERA_PERSPECTIVE;
            popCam[&vrt - S.vrts] = vc3;
            BeginMode3D(vc3);
            wotm_live::g_stage = "draw (vista)";
            {
                // barras desta vista (estao no instantaneo da vista 0), no canto da textura dela na janela
                const int v = static_cast<int>(&vrt - S.vrts);
                S.scene.barPass = {splitMix ? &barsMix : &S.snap.screenRects, static_cast<float>(RW), static_cast<float>(RH), v,
                                   static_cast<float>((v & 1) * (RW - VW)), static_cast<float>((v >> 1) * (RH - VH)),
                                   static_cast<float>(W), static_cast<float>(VH)};
            }
            S.scene.draw(ram, false);
            S.scene.barPass = {};
            wotm::endMode3DInTexture();
            BeginMode3D(vc3);   // lista de alta prioridade da vista (no rtMain, o hier(v, 1) dela vem antes do texto)
            S.scene.drawHp(ram);
            wotm::endMode3DInTexture();
            wotm_live::g_stage = "idle";
            EndTextureMode();
            wotm::msaa::g_current = nullptr;
            if (vmsOn)
                vms.resolve(vrt.id);
        };
        // tela dividida: os flares de cada vista depois de todas desenhadas (Scene::deferFlares; o teste de oclusao espera a GPU)
        wotm::GameCamera viewCam[4]{};
        S.scene.deferFlares = split;
        ++S.scene.redrawId;
        // profundidade de cada vista sem esperar a GPU (a copia pronta mais nova, AsyncDepth; Scene::drawFlaresFor pede a desta vez).
        // Na tela cheia, a da vista 0 serve ao flare do Scene::draw (Scene::fullDepth).
        static wotm::AsyncDepth depth[4];
        static const bool flareSync = std::getenv("WOTM_FLARESYNC") != nullptr;   // a leitura direta, para comparar
        {
            wotm::ProfScope ps(wotm::DrawProf::RFlares);
            for (int v = 0; v < (split ? 4 : 1); ++v)
                if (viewOk[v])
                    depth[v].fetch(S.scene.redrawId);   // sem flare tambem: o valid acompanha a idade
        }
        wotm_levelsel::prepare(S.scene, ram);   // foto propria das fases sem arte no SHELL (Scene::nodeTexture); fora do menu, nenhuma
        for (int v = 1; v < 4; ++v)
            if (viewOk[v])
            {
                {
                    wotm::ProfScope ps(wotm::DrawProf::RShow);
                    const auto &va = S.prev.views[v - 1];
                    viewCam[v] = interpOn && va && va->valid ? S.scene.showBlend(*va, *S.snap.views[v - 1], alpha)
                                                             : S.scene.show(*S.snap.views[v - 1]);
                    if (splitMix)
                    {
                        S.scene.blendAnchors(S.snap.anchors, &S.prev.anchors, barsMix, &polysMix);
                        flaresMix[v] = S.scene.fx.flares;
                    }
                }
                drawView(S.vrts[v], viewCam[v]);
            }
        {
            wotm::ProfScope ps(wotm::DrawProf::RShow);
            static long s_cuts = 0;
            bool cut = false;
            gc = !interpOn ? S.scene.show(S.snap) : interpOld ? S.scene.showBlend(S.prev2, S.prev, alpha, &cut) : S.scene.showBlend(S.prev, S.snap, alpha, &cut);
            static const bool interpLog = std::getenv("WOTM_INTERPLOG") != nullptr;
            if (cut && interpLog)
                std::fprintf(stderr, "[interp] corte de camera g=%u (%ld)\n", S.snap.g, ++s_cuts);
            if (splitMix)
            {
                if (!interpOld)   // no par (P, A) as ancoras de A nao casam com as barras de B
                {
                    S.scene.blendAnchors(S.snap.anchors, &S.prev.anchors, barsMix, &polysMix);
                    S.scene.screenPolys = polysMix;
                }
                flaresMix[0] = S.scene.fx.flares;
            }
        }
        wotm_levelsel::prepare(S.scene, ram);   // com os objetos deste quadro
        viewCam[0] = gc;
        S.snap.cam = gameCam;
        if (freeView.override && freeView.photo)
            wotm_photo::applyToScene(S.scene, ram, gameCam, freeCam, freeView, S.snap.anchors, !S.snap.vctx.full());
        else if (freeView.override)
            wotm_freecam::applyToScene(S.scene, gameCam, freeView.pose);
        if (noHud)
        {
            S.scene.hud.clear();
            S.scene.hp.clear();
            S.scene.hpOverlay.clear();
            S.scene.overlay.clear();
            S.scene.screenPolys.clear();
            auto &rects = S.scene.screenRects;
            rects.erase(std::remove_if(rects.begin(), rects.end(), [](const wotm::ScreenRect &r) { return r.barView >= 0; }), rects.end());
        }
        if (wotm::ScreenRect r{}; wotm_live::shellFadeRect(r))
            S.scene.screenRects.push_back(r);

        S.cur ^= 1;
        for (RenderTexture2D &r : S.rts)
            if (r.id != 0 && (r.texture.width != RW || r.texture.height != RH))   // proporcao mudou (janela redimensionada): refaz os dois
            {
                UnloadRenderTexture(r);
                r = RenderTexture2D{};
            }
        for (RenderTexture2D &r : S.rts)
            if (r.id == 0)
            {
                r = LoadRenderTexture(RW, RH);
                SetTextureFilter(r.texture, TEXTURE_FILTER_BILINEAR);
                // texId 0 (quadro anterior): o mapa de ambiente de video/CLOAK usa REGION_CLAMP na tela 640x224 do GS, que e a
                // textura inteira em 4:3 e a faixa central em widescreen (Scene::envUv, gsScreenRect)
                SetTextureWrap(r.texture, TEXTURE_WRAP_CLAMP);
            }
        RenderTexture2D &rt = S.rts[S.cur];

        if (split)
        {
            drawView(S.vrts[0], gc);
            wotm::ProfScope psFl(wotm::DrawProf::RFlares);
            for (int v = 0; v < 4; ++v)
            {
                if (!viewOk[v])
                    continue;
                const wotm::Snapshot &sn = v ? *S.snap.views[v - 1] : S.snap;
                const wotm::GameCamera &vc = viewCam[v];
                // com MSAA a profundidade da vista so existe no multiamostra: os flares vao nele e a cor e resolvida de novo
                const bool vmsOn = S.vms[v].on();
                BeginTextureMode(vmsOn ? S.vms[v].render() : S.vrts[v]);
                wotm::msaa::g_current = vmsOn ? &S.vms[v] : nullptr;
                rlSetClipPlanes(0.5, 30000.0);
                Camera3D vc3{};
                vc3.position = {vc.pos[0], vc.pos[1], vc.pos[2]};
                vc3.target = {vc.pos[0] + vc.fwd[0], vc.pos[1] + vc.fwd[1], vc.pos[2] + vc.fwd[2]};
                vc3.up = {vc.up[0], vc.up[1], vc.up[2]};
                vc3.fovy = vc.fovy;
                vc3.projection = CAMERA_PERSPECTIVE;
                BeginMode3D(vc3);
                S.scene.drawFlaresFor(ram, splitMix ? flaresMix[v] : sn.flares, sn.vctx, flareSync ? nullptr : &depth[v], vmsOn ? S.vms[v].render().id : S.vrts[v].id);
                wotm::endMode3DInTexture();
                EndTextureMode();
                wotm::msaa::g_current = nullptr;
                if (vmsOn)
                    S.vms[v].resolve(S.vrts[v].id);
            }
        }
        S.scene.deferFlares = false;
        // textura da escolha de lados (fora do BeginTextureMode do quadro); a fonte e o atlas do texto do quadro anterior
        wotm_padsel::renderUi(S.scene, ram, S.scene.text.empty() ? 0ull : S.scene.text.front().tex0);
        wotm_mainmenu::renderUi(S.scene, ram, S.scene.text.empty() ? 0ull : S.scene.text.front().tex0);
        // painel na pausa da luta: o atlas do ultimo quadro com texto (com o painel aberto o jogo nao escreve a lista da pausa)
        wotm_mainmenu::renderPauseUi(S.scene, ram, wotm_photo::fontTex0(S.scene.text));
        wotm_optscreen::renderUi();
        if (wotm_legend::g_n)   // legenda nativa no lugar da do jogo (wotm_legend.inc): sem os icones de botao dele
        {
            S.scene.overlay.clear();
            S.scene.hpOverlay.clear();
        }
        {   // monstro 3D da lista de saves (wotm_savepics.hpp): no RenderTexture dele, antes do quadro; o draw abaixo o cola
            static const PS2Runtime::IoPaths &io = PS2Runtime::getIoPaths();
            const wotm::GsScreenRect gr = wotm::gsScreenRect(static_cast<float>(RW), static_cast<float>(RH));
            wotm::savepics::drawer().prepare(io.cdRoot, io.cdImage.string(), gr.w, static_cast<float>(RH));
        }
        const bool msOn = S.ms.ensure(RW, RH, msaaSamples);
        BeginTextureMode(msOn ? S.ms.render() : rt);
        wotm::msaa::g_current = msOn ? &S.ms : nullptr;
        ClearBackground(BLACK);
        rlSetClipPlanes(0.5, 30000.0);
        Camera3D c3{};
        c3.position = {gc.pos[0], gc.pos[1], gc.pos[2]};
        c3.target = {gc.pos[0] + gc.fwd[0], gc.pos[1] + gc.fwd[1], gc.pos[2] + gc.fwd[2]};
        c3.up = {gc.up[0], gc.up[1], gc.up[2]};
        c3.fovy = gc.fovy;
        c3.projection = CAMERA_PERSPECTIVE;
        if (!split)
        {
            wotm_mainmenu::frameCamera(S.scene, ram, c3, static_cast<float>(RW) / static_cast<float>(RH));   // painel aberto: telao de frente
            wotm_padsel::frameCamera(S.scene, ram, c3, static_cast<float>(RW) / static_cast<float>(RH));     // selecao de 3 e 4: idem
        }
        if (split)
        {
            // as metades (ou quadrantes) copiadas como estao (alfa do GS incluido, como na apresentacao) e a faixa preta do
            // Hud::buildSeparator: 6 px do GS na borda direita da vista 0, ate o centro da tela; nos quadrantes, tambem uma faixa de
            // 3 linhas do GS entre as duas linhas de vistas
            rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
            BeginBlendMode(BLEND_CUSTOM);
            for (int v = 0; v < (quad ? 4 : 2); ++v)
                if (viewOk[v])
                    DrawTexturePro(S.vrts[v].texture, Rectangle{0.f, 0.f, static_cast<float>(viewW(v)), -static_cast<float>(VH)},
                                   Rectangle{static_cast<float>((v & 1) * (RW - VW)), static_cast<float>((v >> 1) * (RH - VH)),
                                             static_cast<float>(viewW(v)), static_cast<float>(VH)},
                                   Vector2{0.f, 0.f}, 0.f, WHITE);
            EndBlendMode();
            const wotm::GsScreenRect gr = wotm::gsScreenRect(static_cast<float>(RW), static_cast<float>(RH));
            const float sepW = std::max(1.f, 6.f * gr.w / 640.f);
            DrawRectangleRec(Rectangle{gr.x0 + gr.w * 0.5f - sepW, 0.f, sepW, static_cast<float>(strip ? RH / 2 : RH)}, BLACK);
            if (quad)
            {
                const float sepH = std::max(1.f, 3.f * static_cast<float>(RH) / 224.f);
                DrawRectangleRec(Rectangle{0.f, 0.5f * static_cast<float>(RH) - 0.5f * sepH, static_cast<float>(RW), sepH}, BLACK);
            }
        }
        else
        {
            BeginMode3D(c3);
            wotm_live::g_stage = "draw";
            S.scene.fullDepth = flareSync ? nullptr : &depth[0];
            S.scene.fullDepthFbo = msOn ? S.ms.render().id : rt.id;
            S.scene.barPass = {&S.scene.screenRects, static_cast<float>(RW), static_cast<float>(RH), -1, 0.f, 0.f,
                               static_cast<float>(RW), static_cast<float>(RH)};   // ja com o deslocamento do modo foto
            S.scene.draw(ram, false);
            S.scene.barPass = {};
            S.scene.fullDepth = nullptr;
            wotm_padsel::drawOnTelao(S.scene, ram, c3);   // escolha de lados do 2 jogadores (so no menu, durante a escolha)
            wotm_mainmenu::drawOnTelao(S.scene, ram, c3);   // lista do menu principal em texto
            wotm_gamescreens::frame(S.scene, ram, c3, RW, RH);   // mouse nas telas do jogo: retangulo de cada item
            wotm_optscreen::drawOnBoard(S.scene, ram, c3);   // tela de opcoes do jogo
            wotm_trainview::draw3d();   // modo treino: por cima da cena, sem teste de profundidade
            wotm_live::g_stage = "idle";
            wotm::endMode3DInTexture();
        }
        S.scene.splitEdges = split;
        S.scene.stripView = strip ? 2 : -1;
        if (!wotm_live::liveText(S.scene.text))   // texto do jogo (menu de pausa, HUD): ganchos da fonte (wotm::FontText)
            S.scene.updateText(ram);              // replay: sprites do GS lidos do fontPacket do instantaneo
        const uint64_t fontTex0 = wotm_photo::fontTex0(S.scene.text);   // a legenda escreve com ele mesmo sem texto no quadro
        if (photoHide)
            S.scene.text.clear();   // modo foto com o HUD escondido
        S.scene.drawQuads(S.scene.screenPolys, static_cast<float>(RW), static_cast<float>(RH));   // HUD do ScreenPolys (icone de vida)
        S.scene.drawScreenRects(static_cast<float>(RW), static_cast<float>(RH), true);   // fades despachados antes do texto
        if (!split)
        {
            BeginMode3D(c3);   // lista de alta prioridade: depois desses fades (ShellRtLoop::endField), antes do texto
            S.scene.drawHp(ram);
            wotm::endMode3DInTexture();
        }
        {
            // Logo do monstro e foto da fase na lista de saves (wotm_savepics.hpp): sobre a moldura, sob o texto
            static const PS2Runtime::IoPaths &io = PS2Runtime::getIoPaths();
            const wotm::GsScreenRect gr = wotm::gsScreenRect(static_cast<float>(RW), static_cast<float>(RH));
            wotm::savepics::drawer().draw(io.cdRoot, io.cdImage.string(), gr.x0, gr.w, static_cast<float>(RH));
        }
        wotm_pausemenu::drawBox2d(RW, RH, !S.scene.text.empty());   // caixa da lista da pausa (sob o texto dela)
        S.scene.drawQuads(S.scene.text, static_cast<float>(RW), static_cast<float>(RH));
        wotm_levelsel::drawBadge(S.scene.text, ram, RW, RH);   // selo UNUSED ao lado do nome das fases que o retail nao oferece
        wotm_mainmenu::drawPausePanel2d(RW, RH);   // painel "recompiled options" na pausa da luta (wotm_pausemenu.inc)
        wotm_padsel::drawBar2d(RW, RH);            // barra de monstros da selecao nativa (acima da faixa preta de baixo)
        {
            // Lista de saves (wotm_save.inc, na thread do EE): o nome do botao de confirmar no dispositivo de agora, para o "press ...
            // again to overwrite", e a legenda de baixo ao carregar (no menu; ao gravar, na fase, a faixa preta cobriria a luta)
            namespace st = wotm::settings;
            using wotm_legend::bind;
            const wotm_legend::Icon ok = bind(st::BCross);
            const int style = wotm_prompts::currentStyle();
            std::string lbl = "X";
            if (style == st::PromptKeyboard && ok.key)
            {
                lbl = ok.key;
                std::replace(lbl.begin(), lbl.end(), '_', ' ');
            }
            else if (ok.pad >= GAMEPAD_BUTTON_RIGHT_FACE_UP && ok.pad <= GAMEPAD_BUTTON_RIGHT_FACE_LEFT)
            {
                static const char *const kNames[3][4] = {{"Y", "B", "A", "X"}, {"TRIANGLE", "CIRCLE", "CROSS", "SQUARE"}, {"X", "A", "B", "Y"}};
                lbl = kNames[style == st::PromptXbox ? 0 : style == st::PromptNintendo ? 2 : 1][ok.pad - GAMEPAD_BUTTON_RIGHT_FACE_UP];
            }
            {
                std::lock_guard<std::mutex> lk(wotm::mouse::g_lblM);
                wotm::mouse::g_confirmLabel = lbl;
            }
            if (wotm::mouse::saveListMode() == 1)
                wotm_legend::post({{bind(st::BUp), bind(st::BDown), "SELECT"}, {ok, {}, "LOAD"}, {bind(st::BTriangle), {}, "BACK"}}, 1.f);
        }
        wotm_legend::draw(S.scene, ram, fontTex0, RW, RH);   // legenda e dica dos menus nativos e do modo foto
        S.scene.drawScreenRects(static_cast<float>(RW), static_cast<float>(RH));   // os demais fades, por cima de tudo
        if (!photoHide && !split)   // numeros de dano (mod WOTM_DAMAGEPOP; vazio sem ele)
            drawDamagePopups(S.scene, c3, RW, RH, Rectangle{0.f, 0.f, static_cast<float>(RW), static_cast<float>(RH)});
        else if (!photoHide)
            for (int v = 0; v < (quad ? 4 : 2); ++v)
                if (viewOk[v])
                    drawDamagePopups(S.scene, popCam[v], RW, RH,
                                     Rectangle{static_cast<float>((v & 1) * (RW - VW)), static_cast<float>((v >> 1) * (RH - VH)),
                                               static_cast<float>(viewW(v)), static_cast<float>(VH)});
        if (const float rw = g_wotmMechaReticle.load(std::memory_order_relaxed); !split && !photoHide && rw > 0.f)
        {
            // retícula da camera da torre do Mecha (wotm_custom.inc, laserCam): o centro da tela e para onde o laser vai
            const float cx = RW * 0.5f, cy = RH * 0.5f, r = RH * 0.022f, g = r * 0.45f, t = std::max(2.f, RH / 540.f);
            const Color col = {255, 90, 40, static_cast<unsigned char>(220.f * rw)}, dark = {0, 0, 0, static_cast<unsigned char>(140.f * rw)};
            for (int pass = 0; pass < 2; ++pass)
            {
                const Color c = pass ? col : dark;
                const float th = pass ? t : t + 2.f;
                DrawRing({cx, cy}, r - th * 0.5f, r + th * 0.5f, 0.f, 360.f, 48, c);
                DrawLineEx({cx - r - g * 2.f, cy}, {cx - r + g, cy}, th, c);
                DrawLineEx({cx + r - g, cy}, {cx + r + g * 2.f, cy}, th, c);
                DrawLineEx({cx, cy - r - g * 2.f}, {cx, cy - r + g}, th, c);
                DrawLineEx({cx, cy + r - g}, {cx, cy + r + g * 2.f}, th, c);
            }
            DrawCircleV({cx, cy}, t, col);
        }
        if (!photoHide)
            wotm_trainview::drawPanel(S.scene, ram, fontTex0, RW, RH);   // modo treino: painel de dano (vazio desligado)
        EndTextureMode();
        wotm::msaa::g_current = nullptr;
        if (msOn)
            S.ms.resolve(rt.id);
        wotm_photo::afterRender();   // modo foto: grava o quadro pedido (saiu sem a legenda)
        s_drawMs[nViews] += (GetTime() - tDraw0) * 1000.0;
        s_drawCpu[nViews] += wotmThreadCpuMs() - cpuDraw0;   // passos de 15,6 ms no Windows: so vale na media
        ++s_drawN[nViews];

        {   // WOTM_SHOTS=<s>[,<s>...]: grava o quadro nativo em shot_<s>.png (pasta atual) quando o relogio da janela passa de cada <s>
            // segundos; para conferir telas ao vivo com um roteiro de pad (PS2X_PAD), sem tecla
            static std::vector<double> shots = [] {
                std::vector<double> v;
                if (const char *s = std::getenv("WOTM_SHOTS"))
                    for (const char *p = s; *p;) {
                        char *e = nullptr;
                        const double t = std::strtod(p, &e);
                        if (e == p) break;
                        v.push_back(t);
                        p = (*e == ',') ? e + 1 : e;
                    }
                return v;
            }();
            static size_t next = 0;
            if (next < shots.size() && GetTime() >= shots[next]) {
                char name[64];
                std::snprintf(name, sizeof name, "shot_%g.png", shots[next]);
                if (wotm_live::exportFrame(name)) std::fprintf(stderr, "[shot] %s (g_frame %u)\n", name, ram.u32(0x006F7E38));
                ++next;
            }
        }
    }

    // quadro na janela, mantendo a proporcao (faixas so se a janela sair de 4:3..32:9 ou com WOTM_ASPECT fixo)
    const float sw = static_cast<float>(GetScreenWidth()), sh = static_cast<float>(GetScreenHeight());
    const float k = std::min(sw / RW, sh / RH);
    const Rectangle src{0.f, 0.f, static_cast<float>(RW), -static_cast<float>(RH)};
    const Rectangle dst{(sw - RW * k) * 0.5f, (sh - RH * k) * 0.5f, RW * k, RH * k};
    wotm_pad::pointerFrame(dst, RW, RH);   // mouse nos menus: posicao no quadro nativo, cliques e rodinha (wotm_mouse.hpp)
    {   // e em unidades da tela do PS2 (texto do jogo), para a thread do EE
        const wotm::mouse::Pointer &p = wotm::mouse::g_ptr;
        const wotm::GsScreenRect gr = wotm::gsScreenRect(static_cast<float>(RW), static_cast<float>(RH));
        wotm::mouse::publishGs(p.in, (p.x - gr.x0) * 640.f / gr.w, p.y * 224.f / static_cast<float>(RH));
    }
    // Sem mistura: o alfa do quadro e o que as misturas do GS deixaram (o comum baixa, o aditivo sobe para 1,0), nao opacidade. Com
    // BLEND_ALPHA sobre o preto a cena saia escurecida pelo alfa, e os quadrados aditivos (halo dos power-ups) viravam retangulos claros.
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM);
    DrawTexturePro(S.rts[S.cur].texture, src, dst, Vector2{0.f, 0.f}, 0.f, WHITE);
    EndBlendMode();

    {   // contador de FPS (desligado ao abrir; F3 liga/desliga): "jogo" = quadros do jogo por segundo (g_frame, 0x6F7E38); "tela" = desenhos nativos por segundo
        static bool show = false;
        static uint32_t lastG = 0;
        static double lastT = 0.0;
        static int drawn = 0;
        static float gameFps = 0.f, screenFps = 0.f;
        if (IsKeyPressed(KEY_F3))
            show = !show;
        ++drawn;
        const double now = GetTime();
        static const bool camLog = std::getenv("WOTM_CAMLOG") != nullptr;   // diagnostico: serie temporal da camera lida (um [cam] por desenho)
        if (camLog)
        {
            // _cs das vistas 0 e 1 (ponteiros) e a frente da worldToScreenMat (linha 3), lidos agora: separam troca de camera de leitura rasgada
            const uint32_t wm = wotm::addr::worldToScreenMat;
            const uint32_t cs = ram.u32(wotm::addr::viewInfo);
            float csp[3] = {0, 0, 0}, csu[3] = {0, 0, 0};
            if (ram.ok(cs, 0x60))
                for (int i = 0; i < 3; ++i) { csp[i] = ram.f32(cs + 0x10 + 4 * i); csu[i] = ram.f32(cs + 0x40 + 4 * i); }
            std::fprintf(stderr, "[cam] t=%.4f g=%u fov=%.2f pos=%.2f,%.2f,%.2f fwd=%.4f,%.4f,%.4f cs=%x,%x wm3=%.4f,%.4f,%.4f csp=%.2f,%.2f,%.2f "
                                 "up=%.4f,%.4f,%.4f csu=%.4f,%.4f,%.4f\n",
                         now, ram.u32(wotm::sym::g_frame), gc.fovy, gc.pos[0], gc.pos[1], gc.pos[2], gc.fwd[0], gc.fwd[1], gc.fwd[2], cs,
                         ram.u32(wotm::addr::viewInfo + 0x14), ram.f32(wm + 0x30), ram.f32(wm + 0x34), ram.f32(wm + 0x38), csp[0], csp[1], csp[2],
                         gc.up[0], gc.up[1], gc.up[2], csu[0], csu[1], csu[2]);
        }
        if (lastT == 0.0)
        {
            lastT = now;
            lastG = ram.u32(wotm::sym::g_frame);
        }
        else if (now - lastT >= 0.5)
        {
            const uint32_t g = ram.u32(wotm::sym::g_frame);
            gameFps = static_cast<float>(g - lastG) / static_cast<float>(now - lastT);
            screenFps = static_cast<float>(drawn) / static_cast<float>(now - lastT);
            lastG = g;
            drawn = 0;
            lastT = now;
        }
        if (show && !wotm_live::g_hideFps)
        {
            char line[128];
            std::snprintf(line, sizeof line, "game %.1f fps | screen %.1f fps", gameFps, screenFps);
            DrawText(line, 11, 11, 20, BLACK);
            DrawText(line, 10, 10, 20, gameFps >= 55.f ? GREEN : (gameFps >= 30.f ? YELLOW : RED));
        }
    }

    {   // menu de debug (F1 abre/fecha; com ele aberto, 1..8 alternam ou disparam). Setas e Enter sao do pad (ps2_pad.cpp): nao servem aqui.
        // So escreve g_wotmCheats; quem aplica e a thread do EE (monsterUpdateCheats, wotm_overrides.cpp).
        static bool open = false;
        static int killWho = 1;    // alvo da tecla 8: 0 todos os jogadores, 1..4 so P1..P4 (a tecla 7 alterna)
        if (IsKeyPressed(KEY_F1))
            open = !open;
        uint32_t c = g_wotmCheats.load(std::memory_order_relaxed);
        const uint32_t st = (c & kCheatStaminaMask) / kCheatStamina1;
        if (open)
        {
            uint32_t n = c;
            if (IsKeyPressed(KEY_ONE)) n ^= kCheatInvincible;
            if (IsKeyPressed(KEY_TWO)) n = (n & ~kCheatStaminaMask) | ((st + 1u) % 3u) * kCheatStamina1;
            if (IsKeyPressed(KEY_THREE)) n ^= kCheatSpecial;
            if (IsKeyPressed(KEY_FOUR)) n ^= kCheatCloak;
            if (IsKeyPressed(KEY_FIVE)) n ^= kCheatGod;
            if (IsKeyPressed(KEY_SIX)) g_wotmCheatWin = true;
            if (IsKeyPressed(KEY_SEVEN)) killWho = (killWho + 1) % 5;
            if (IsKeyPressed(KEY_EIGHT)) g_wotmCheatKill = killWho ? (1u << (killWho - 1)) : 0xFu;
            if (n != c)
            {
                g_wotmCheats = n;
                std::fprintf(stderr, "[debug] trapacas 0x%x -> 0x%x\n", c, n);
                c = n;
            }
        }
        const uint32_t s2 = (c & kCheatStaminaMask) / kCheatStamina1;
        static const char *const stName[3] = {"normal", "full", "rage (130)"};
        const int fs = 20, x = 10, top = 40;
        if (open)
        {
            char lines[12][64];
            int nl = 0;
            std::snprintf(lines[nl++], 64, "DEBUG (F1 closes)");
            std::snprintf(lines[nl++], 64, "1  Invincible        %s", (c & kCheatInvincible) ? "ON" : "-");
            std::snprintf(lines[nl++], 64, "2  Stamina           %s", stName[s2]);
            std::snprintf(lines[nl++], 64, "3  Special ready     %s", (c & kCheatSpecial) ? "ON" : "-");
            std::snprintf(lines[nl++], 64, "4  Cloak (invisible) %s", (c & kCheatCloak) ? "ON" : "-");
            std::snprintf(lines[nl++], 64, "5  Game's god mode   %s", (c & kCheatGod) ? "ON" : "-");
            std::snprintf(lines[nl++], 64, "6  Win the fight (knocks out the AIs)");
            static const char *const whoName[5] = {"all players", "P1", "P2", "P3", "P4"};
            std::snprintf(lines[nl++], 64, "7  Kill target       %s", whoName[killWho]);
            std::snprintf(lines[nl++], 64, "8  Kill (loses a life)");
            int w = 0;
            for (int i = 0; i < nl; ++i)
                w = std::max(w, MeasureText(lines[i], fs));
            DrawRectangle(x - 6, top - 6, w + 12, nl * (fs + 4) + 8, Color{0, 0, 0, 170});
            for (int i = 0; i < nl; ++i)
                DrawText(lines[i], x, top + i * (fs + 4), fs, i == 0 ? YELLOW : RAYWHITE);
        }
        else if (c)
        {   // fechado: lembrete curto do que esta ligado
            char line[96];
            std::snprintf(line, sizeof line, "debug:%s%s%s%s%s", (c & kCheatInvincible) ? " INV" : "", s2 == 1 ? " STA" : (s2 == 2 ? " RAGE" : ""),
                          (c & kCheatSpecial) ? " SPC" : "", (c & kCheatCloak) ? " CLOAK" : "", (c & kCheatGod) ? " GOD" : "");
            DrawText(line, x + 1, top + 1, fs, BLACK);
            DrawText(line, x, top, fs, ORANGE);
        }
    }


    if (++S.frame % 300 == 0 && std::getenv("PS2X_STATS"))
        std::fprintf(stderr, "[native] frame %ld redesenhos=%ld misturados=%ld t=%.1f itens=%zu ceu=%zu rtx=%s\n", S.frame, s_renders, s_blends, GetTime(),
                     S.scene.items.size(), S.scene.sky.size(), S.rtxPath.c_str());
    if (S.frame % 300 == 0 && std::getenv("PS2X_STATS"))
    {
        std::fprintf(stderr, "[native] redesenho:");
        for (int n = 1; n <= 4; ++n)
            if (s_drawN[n])
                std::fprintf(stderr, " %d vista%s %.2f ms (CPU %.2f) x %ld;", n, n > 1 ? "s" : "", s_drawMs[n] / s_drawN[n], s_drawCpu[n] / s_drawN[n], s_drawN[n]);
        std::fprintf(stderr, "\n");
        for (int n = 0; n < 5; ++n)
        {
            s_drawMs[n] = s_drawCpu[n] = 0.0;
            s_drawN[n] = 0;
        }
        if (wotm::drawProfOn())   // WOTM_DRAWSTATS: media por vista desenhada de cada fase do Scene::draw
        {
            wotm::DrawProf &p = wotm::drawProf();
            const double k = p.views ? 1.0 / double(p.views) : 0.0;
            std::fprintf(stderr, "[native] desenho por vista (x %ld): ceu %.2f travessia %.2f OT %.2f sombras %.2f (silhuetas %.2f) sprites %.2f "
                         "particulas %.2f HUD %.2f ms (descargas de lote %.2f, vertices nas threads %.2f); %.0f lotes (%.0f desenhos), %.0f vertices (%.0f emitidos, %.0f da arena, %.0f subidos a ela); silhuetas %ld refeitas, %ld reaproveitadas\n", p.views, p.ms[wotm::DrawProf::Sky] * k, p.ms[wotm::DrawProf::Trav] * k,
                         p.ms[wotm::DrawProf::AlphaOT] * k, p.ms[wotm::DrawProf::Shadows] * k, p.ms[wotm::DrawProf::ShadowTex] * k,
                         p.ms[wotm::DrawProf::Sprites] * k, p.ms[wotm::DrawProf::Fx] * k, p.ms[wotm::DrawProf::Hud] * k, p.ms[wotm::DrawProf::Flush] * k, p.ms[wotm::DrawProf::Jobs] * k,
                         double(p.batches) * k, double(p.draws) * k,
                         double(p.verts) * k, double(p.emitted) * k, double(p.resident) * k, double(p.resUp) * k, p.shadowMiss, p.shadowHit);
            const double r = p.redraws ? 1.0 / double(p.redraws) : 0.0;
            {
                wotm::GpuTimer &g = wotm::GpuTimer::get();
                std::fprintf(stderr, "[native] GPU por redesenho: %.2f ms (x %ld)\n", g.n ? g.ms / double(g.n) : 0.0, g.n);
                g.ms = 0.0;
                g.n = 0;
                std::fprintf(stderr, "[native] VtxStream por redesenho: subida %.2f ms (%.2f MB), desenhos e estados %.2f ms\n", wotm::VtxStream::upMs * r,
                             wotm::VtxStream::upBytes * r / 1048576.0, wotm::VtxStream::drawMs * r);
                wotm::VtxStream::upMs = wotm::VtxStream::drawMs = wotm::VtxStream::upBytes = 0.0;
            }
            std::fprintf(stderr, "[native] por redesenho (x %ld): todo %.2f, show %.2f, vistas %.2f, flares adiados %.2f, resto %.2f ms\n", p.redraws,
                         p.ms[wotm::DrawProf::RFrame] * r, p.ms[wotm::DrawProf::RShow] * r, p.ms[wotm::DrawProf::RView] * r,
                         p.ms[wotm::DrawProf::RFlares] * r,
                         (p.ms[wotm::DrawProf::RFrame] - p.ms[wotm::DrawProf::RShow] - p.ms[wotm::DrawProf::RView] - p.ms[wotm::DrawProf::RFlares]) * r);
            p = wotm::DrawProf{};
        }
    }
    return true;
}
// Chamado na thread do jogo logo depois do particleDraw (hook_particleDraw, main.cpp): copia a lista do lens flare ja projetada
// (Particles::captureFlares); a apresentacao chega tarde demais (o hier do quadro seguinte ja a refez).
void wotmFlareCapture(const uint8_t *rdram)
{
    if (rdram)
        wotm::Particles::captureFlares(wotm::Ram{rdram, PS2_RAM_SIZE});
}
#else
namespace wotm_live
{
static void initWindowAspect() {}
static void fullscreenKey() {}
static bool exportFrame(const char *) { return false; }
static std::string resources() { return {}; }
}
static bool wotmDrawNative(const uint8_t *) { return false; }
void wotmDamagePopup(const uint8_t *, const float *, float, int) {}
void wotmCaptureFrame(const uint8_t *) {}
void wotmCaptureScene(const uint8_t *) {}
void wotmFlareCapture(const uint8_t *) {}
void wotmFadeView(const uint8_t *, int, uint32_t, bool) {}
void wotmTheGameFade(const uint8_t *, uint32_t) {}
void wotmThreeMileFlash(const uint8_t *) {}
void wotmLetterbox(const uint8_t *, float) {}
void wotmGsPktQueueSent() {}
void wotmFontSent() {}
void wotmFontInitPacket(uint32_t) {}
void wotmFontSetTexId(const uint8_t *, uint32_t) {}
void wotmFontColor(const uint8_t *, int) {}
void wotmFontGlyph(const uint8_t *, int, int) {}
void wotmFontDma(const uint8_t *) {}
void wotmScreenPoly(const uint8_t *, int, int, int, uint32_t) {}
bool wotmScreenPolyBox(const uint8_t *, int, int, int, uint32_t, float *) { return false; }
void wotmBarRect(float, float, float, float, uint8_t, uint8_t, uint8_t, uint8_t) {}
void wotmBarTex(float, float, float, float, uint64_t, float, float, float, float) {}
float wotmHealthIconShift(const uint8_t *, float) { return 0.f; }
void wotmHudAnchor(const uint8_t *, float, float, const float *, bool) {}
void wotmHudAnchorEnd() {}
void wotmBarView(int) {}
void wotmBarHud(bool) {}
void wotmShellFade(const uint8_t *, int, uint32_t, uint32_t, uint32_t, int, int, int, bool) {}
void wotmShellFadeStep() {}
void wotmShellFadeStepDone() {}
void wotmLoadFreeze(bool) {}
void wotmCaptureFadeStep(const uint8_t *) {}
void wotmPickupHighlights(const uint8_t *) {}
void wotmPadSelInstall(PS2Runtime &) {}
void wotmLevelSelInstall(PS2Runtime &) {}
void wotmMainMenuInstall(PS2Runtime &) {}
#endif
