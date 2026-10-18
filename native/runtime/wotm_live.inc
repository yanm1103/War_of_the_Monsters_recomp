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

// Rastro do boot: da abertura do processo ao primeiro filme ou quadro do jogo, cada etapa vai ao stderr com o tempo desde o inicio
// ("[boot]  3.21 s  disco: ...") e aparece escrita na tela preta. Etapa parada ha mais de 3 s repete no log com o PC do EE, para
// achar o gargalo em maquinas onde a tela preta demora. Chamado do main.cpp, de initialize/run, do sceCdSearchFile e do log do IOP.
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
    DrawTextEx(font(), TextFormat("Carregando... %.1f s", t), Vector2{16.0f, h - 54.0f}, 20.0f, 0.0f, LIGHTGRAY);
    DrawTextEx(font(), TextFormat("%s%s", s.c_str(), t - at >= 3.0 ? TextFormat("  (%.0f s nesta etapa)", t - at) : ""), Vector2{16.0f, h - 28.0f},
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
    wotmBoot("pronto: %s", what);
    wotm_boot::done.store(true, std::memory_order_relaxed);
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
        return wotmBootDone("primeiro filme");
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
#include "wotm_snd.inc"

#if __has_include("wotm_native.hpp")
#include "wotm_native.hpp"
#include "wotm_cheats.h"   // menu de debug (F1)
#include <filesystem>
#include "wotm_freecam.inc"   // camera livre de debug (F5)
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
inline long __stdcall crashFilter(ExcPointers *e)
{
    const char *base = static_cast<const char *>(GetModuleHandleA(nullptr));
    std::fprintf(stderr, "[CRASH] code=0x%08lx addr=%p (exe+0x%llx) stage=%s", e->rec->code, e->rec->address,
                 (unsigned long long)(static_cast<const char *>(e->rec->address) - base), g_stage);
    if (e->rec->code == 0xC0000005)
        std::fprintf(stderr, " acesso=%s endereco=0x%llx", e->rec->info[0] == 1 ? "escrita" : (e->rec->info[0] == 8 ? "execucao" : "leitura"), e->rec->info[1]);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
    return 0; // EXCEPTION_CONTINUE_SEARCH
}
struct State
{
    wotm::Scene scene;
    RenderTexture2D rts[2]{};   // alternadas: a do quadro anterior e a textura do texId 0 (o jogo amostra o buffer em exibicao)
    int cur = 0;
    uint32_t levelSig = 0;
    bool rtxChosen = false;
    std::string rtxPath;
    long frame = 0;
    wotm::Snapshot snap;   // ultimo quadro do jogo (desenhado ate o proximo)
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

// Widescreen (Hor+): o FOV vertical e o do jogo e a largura acompanha a proporcao. WOTM_ASPECT=L:A fixa a proporcao (4:3 = tela
// original, com faixas nas laterais); sem ela a proporcao segue a janela (entre 4:3 e 32:9), que abre em 16:9.
static float fixedAspect()
{
    static const float a = []() {
        const char *s = std::getenv("WOTM_ASPECT");
        float w = 0.f, h = 0.f;
        if (s && std::sscanf(s, "%f:%f", &w, &h) == 2 && w > 0.f && h > 0.f)
            return std::clamp(w / h, 4.f / 3.f, 32.f / 9.f);
        return 0.f;
    }();
    return a;
}
static float viewAspect()
{
    if (g_force43)
        return 4.f / 3.f;
    if (const float a = fixedAspect())
        return a;
    const float sh = static_cast<float>(GetScreenHeight());
    return sh > 0.f ? std::clamp(static_cast<float>(GetScreenWidth()) / sh, 4.f / 3.f, 32.f / 9.f) : 4.f / 3.f;
}
// Altura do quadro nativo em pixels (WOTM_RES=<linhas>, padrao 720; a tela do PS2 tem 448, 224 por campo); a largura sai da proporcao.
// O replay (--vu1cmp) fica em 720, como as comparacoes ja gravadas.
static int renderHeight()
{
    static const int h = []() {
        const char *s = std::getenv("WOTM_RES");
        const int v = s ? std::atoi(s) : 0;
        return v > 0 ? std::clamp(v, 224, 4320) : 720;
    }();
    return g_force43 ? 720 : h;
}
// Uma vez, na primeira apresentacao (wotmDrawScene, antes da abertura): WOTM_FULLSCREEN=1 vai para a tela cheia sem borda (proporcao
// do monitor; com WOTM_ASPECT fixo o quadro fica com faixas); senao a janela de 960x720 do runtime vira o tamanho do quadro nativo
// (renderHeight na proporcao), reduzido para caber no monitor. O quadro nao depende da janela: so a ampliacao final muda.
static void initWindowAspect()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    if (const char *s = std::getenv("WOTM_FULLSCREEN"); s && *s && *s != '0')
    {
        ToggleBorderlessWindowed();
        ClearWindowState(FLAG_WINDOW_TOPMOST);   // o raylib 5.5 poe a janela sem borda sempre por cima (GLFW_FLOATING)
        return;
    }
    const float a = fixedAspect() ? fixedAspect() : 16.f / 9.f;
    float h = static_cast<float>(renderHeight()), w = h * a;
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
static bool g_localCollect = false;   // replay (--vu1cmp): sem jogo rodando, a apresentacao le a RAM do instantaneo ela mesma
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
// HUD do ScreenPolys do quadro em montagem: lido na entrada do ScreenPolys::dmaData (wotmScreenPolys), que zera numPolys; a captura
// o junta ao instantaneo (o takeSnapshot na captura ja nao o ve).
static std::vector<wotm::TextQuad> g_pendingPolys;
// colorQuad dos pick-ups destacados como o enableHighlights os deixou, antes do hier (Scene::csColorAtDraw); a captura os passa a coleta.
static std::vector<wotm::Scene::CsColorAtDraw> g_pendingCsColor;
static std::atomic<int> g_shellFade{-1};
// Carga da fase (Shell::LoadLevelFiles ate a entrada do rtMain): no PS2 a tela fica parada no ultimo quadro do cartao LOAD (a barra
// de carga e desenhada direto no quadro, sem hier) e o Shell::FadeScreen sem redesenho so a escurece. O nativo nao pode redesenhar
// o cartao nesse meio tempo: a RAM ja tem o nivel novo, e o loadResources troca as texturas pelas dele (o cartao saia com uma
// textura qualquer do nivel, como o brilho verde do power-up no CENTRAL). Enquanto vale, a captura nao publica nada e a
// apresentacao repete o ultimo quadro desenhado, com o fade do Shell::FadeScreen por cima (wotmDrawFrozen).
static std::atomic<bool> g_loadFreeze{false};

// Fade do Shell::FadeScreen em curso (g_shellFade), lido ao vivo do pacote dele; com acumulo, o alfa equivalente no buffer da vez.
static bool shellFadeRect(const wotm::Ram &ram, wotm::ScreenRect &r)
{
    const int sf = g_shellFade.load(std::memory_order_relaxed);
    if (sf < 0 || !wotm::readScreenRect(ram, wotm::addr::shellFadePacket, r))
        return false;
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

// Saida do LevelPickups::enableHighlights (thread do EE): anota o colorQuad do _cs do pick-up destacado de cada um dos dois
// primeiros monstros (highlightList[monstro+0x28] -> no -> Pickup -> cs +0xC). Os dois de uma vez porque, na tela dividida, a
// chamada de um jogador devolve o pick-up do outro a 1; o ultimo valor ganha.
void wotmPickupHighlights(const uint8_t *rdram)
{
    if (!rdram)
        return;
    const wotm::Ram ram{rdram, PS2_RAM_SIZE};
    for (uint32_t j = 0; j < 2; ++j)
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

void wotmScreenRect(const uint8_t *rdram, uint32_t pkt, bool queued)
{
    wotm::ScreenRect r{};
    if (rdram && wotm_live::g_pendingRects.size() < 16 && wotm::readScreenRect(wotm::Ram{rdram, PS2_RAM_SIZE}, pkt, r))
    {
        wotm_live::g_pendingRects.push_back(r);
        wotm_live::g_pendingState.push_back(queued ? 1 : 0);
    }
}
// Faixas pretas do menu (letterboxDmaData, mandado pelo userintMain entre o hier(0,0) e o hier(0,1)): dois SPRITEs de 24 linhas no
// topo e na base da vista, sem A+D (ALPHA_1 em vigor). Ficam com os fades que saem antes do texto e da lista de alta prioridade
// (underText); cobrem a largura da vista, entao em widescreen vao de lado a lado da janela.
void wotmLetterbox(const uint8_t *rdram)
{
    wotm::ScreenRect r[2];
    const int n = rdram ? wotm::readScreenRects(wotm::Ram{rdram, PS2_RAM_SIZE}, wotm::addr::letterboxPacket, r, 2, false) : 0;
    for (int i = 0; i < n && wotm_live::g_pendingRects.size() < 16; ++i)
    {
        r[i].underText = true;
        wotm_live::g_pendingRects.push_back(r[i]);
        wotm_live::g_pendingState.push_back(3);   // ja decidido
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
void wotmScreenPolys(const uint8_t *rdram)
{
    if (rdram && wotm_live::g_pendingPolys.size() < 256)
        wotm::readScreenPolys(wotm::Ram{rdram, PS2_RAM_SIZE}, wotm_live::g_pendingPolys);
}
void wotmShellFade(int start, int step, bool accumulate)
{
    wotm_live::g_shellFade.store((start & 0xFF) | ((step & 0xFF) << 8) | (accumulate ? 1 << 16 : 0), std::memory_order_relaxed);
}

// A cena (objetos, ceu, HUD, lista de alta prioridade, sombras, luz, camera e paletas) e coletada na saida do hier(0, 0)
// (wotmCaptureScene, gancho do wotm_overrides.cpp): o PS2 desenha os objetos com o estado da travessia, e o gameUpdate e o
// animationRunGlobal que vem depois ja deixam na RAM monstros, paletas e camera do quadro seguinte. Coletando tudo no fim do quadro,
// a malha saia um quadro a frente das luzes dos olhos (lens flare, cuja lista o hier monta) e do proprio PS2. As particulas, os
// flares, o texto e os fades continuam no fim do quadro (wotmCaptureFrame): o particleDraw do PS2 vem depois do gameUpdate.
namespace wotm_live
{
static wotm::Scene &captureScene()
{
    static wotm::Scene cap;   // so coleta (RAM -> itens); texturas e malhas ficam com a Scene da apresentacao
    return cap;
}
static wotm::Snapshot g_sceneSnap;   // cena do ultimo hier(0, 0) ainda nao publicada (thread do EE)
static bool g_sceneFresh = false;
}

void wotmCaptureScene(const uint8_t *rdram)
{
    wotm_live::Capture &c = wotm_live::capture();
    if (!rdram || !c.on.load(std::memory_order_relaxed))
        return;
    const wotm::Ram ram{rdram, PS2_RAM_SIZE};
    const uint32_t wc = ram.u32(wotm::addr::world);
    if (!ram.ok(wc, 0x40) || !ram.ok(ram.u32(wc)))
        return;
    wotm::Scene &cap = wotm_live::captureScene();
    cap.wide = c.wide.load(std::memory_order_relaxed);
    cap.csColorAtDraw = wotm_live::g_pendingCsColor;   // tom do pick-up gravado pelo enableHighlights deste quadro (o mesmo da RAM agora)
    cap.collect(ram);
    wotm_live::g_sceneSnap = cap.takeSnapshot(ram, ram.u32(wotm::sym::g_frame));
    wotm_live::g_sceneFresh = true;
}

// Passo do Shell::FadeScreen com redesenho (b = true: hier(0, 0) e sceGsSyncV por passo, sem timerWaitForMinUpdateRate): publica a
// cena desse hier sem desligar o fade (g_shellFade, que a apresentacao desenha por cima). Sem isso o fade-in da tela de carregamento
// (DisplayLoadBackground) passava sem quadro nativo e o cartao LOAD so aparecia ja claro.
void wotmCaptureFadeStep(const uint8_t *rdram)
{
    if (!wotm_live::g_sceneFresh || wotm_live::g_loadFreeze.load(std::memory_order_relaxed))
        return;
    wotm_live::g_sceneFresh = false;
    wotm_live::Capture &c = wotm_live::capture();
    if (!rdram || !c.on.load(std::memory_order_relaxed))
        return;
    wotm::Snapshot sn = std::move(wotm_live::g_sceneSnap);
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

void wotmCaptureFrame(const uint8_t *rdram)
{
    // a copia do lens flare vale so para este quadro: o seguinte sem particleDraw (menu, LOAD) sai sem flares
    struct ConsumeFlares { ~ConsumeFlares() { wotm::Particles::consumeFlares(); } } consumeFlares;
    wotm_live::Capture &c = wotm_live::capture();
    std::vector<wotm::ScreenRect> rects;
    rects.swap(wotm_live::g_pendingRects);
    wotm_live::g_pendingState.clear();
    std::vector<wotm::TextQuad> polys;
    polys.swap(wotm_live::g_pendingPolys);
    std::vector<wotm::Scene::CsColorAtDraw> csColor;
    csColor.swap(wotm_live::g_pendingCsColor);
    wotm_live::g_shellFade.store(-1, std::memory_order_relaxed);
    const bool haveScene = wotm_live::g_sceneFresh;
    wotm_live::g_sceneFresh = false;
    if (!rdram || !c.on.load(std::memory_order_relaxed) || wotm_live::g_loadFreeze.load(std::memory_order_relaxed))
        return;
    const wotm::Ram ram{rdram, PS2_RAM_SIZE};
    const uint32_t wc = ram.u32(wotm::addr::world);
    if (!ram.ok(wc, 0x40) || !ram.ok(ram.u32(wc)))
        return;
    wotm::Scene &cap = wotm_live::captureScene();
    cap.wide = c.wide.load(std::memory_order_relaxed);
    wotm::Snapshot sn;
    if (haveScene) {   // cena do hier(0, 0) deste quadro; particulas de agora
        sn = std::move(wotm_live::g_sceneSnap);
        cap.collectFx(ram);
        sn.fx = cap.fx.systems;
        sn.flares = cap.fx.flares;
        sn.sprites = cap.sprites;
        sn.screenPolys.clear();   // o hier vem antes do ScreenPolys::dmaData: os quadrados do quadro chegam pelo gancho (polys)
        wotm::readScreenPolys(ram, sn.screenPolys);
        sn.g = ram.u32(wotm::sym::g_frame);
    } else {   // quadro sem hier(0, 0) (filme, carga): tudo de agora, como antes
        cap.csColorAtDraw = std::move(csColor);
        cap.collect(ram);
        sn = cap.takeSnapshot(ram, ram.u32(wotm::sym::g_frame));
    }
    // posicoes, idades e pacotes das particulas deste instante: a apresentacao desenha depois, com o particleMove seguinte ja
    // rodando (contagem do instantaneo com dados vivos = o fecho do pacote lido como particula; 09_magmo_impacto.md)
    sn.fxRam = wotm::Particles::shadowRam(ram, sn.fx);
    sn.screenRects = std::move(rects);
    sn.screenPolys.insert(sn.screenPolys.end(), polys.begin(), polys.end());
    std::lock_guard<std::mutex> lk(c.m);
    c.snap = std::move(sn);
    c.fresh = true;
}

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
        if (wotm::ScreenRect r{}; wotm_live::shellFadeRect(ram, r))
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
    }
    // Quadro novo do jogo: o instantaneo capturado na thread do EE (wotmCaptureFrame). No replay (--vu1cmp) nao ha jogo rodando e a
    // RAM do instantaneo e fixa: a coleta e aqui mesmo. Os desenhos seguintes repetem o instantaneo.
    wotm::Snapshot sn;
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
        if (sn.items.size() < 20 || !sn.cam.ok)   // sem fase de verdade (carregando, tela vazia): fica o quadro do GS
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
                wotmBootDone("fase na tela");
            }
        }
        S.snap = std::move(sn);
    }
    // Camera livre com o jogo pausado (F7): a coleta da captura recortou pela vista congelada. Com a thread do EE parada a RAM nao
    // muda, entao a apresentacao coleta de novo, uma vez por quadro pausado, sem o recorte (o resto do instantaneo fica o mesmo).
    {
        static uint32_t uncullG = ~0u;
        if (S.snap.valid && g_wotmFreeCam.load(std::memory_order_relaxed) && wotmPaused() && S.snap.g != uncullG)
        {
            uncullG = S.snap.g;
            static wotm::Scene rc;
            rc.wide = cap.wide.load(std::memory_order_relaxed);
            rc.fovCull = false;
            wotm_live::g_stage = "collect (camera livre)";
            rc.collect(ram);
            wotm::Snapshot un = rc.takeSnapshot(ram, S.snap.g);
            un.screenRects = S.snap.screenRects;
            un.screenPolys = S.snap.screenPolys;
            un.flares = S.snap.flares;
            un.cam = S.snap.cam;
            S.snap = std::move(un);
        }
    }
    if (!S.snap.valid)
        return false;
    if (!S.snap.cam.ok)
        return false;
    // camera livre pausada: o show ja recebe a pose (olho das faces de tras, neblina, sprites), e a cena perde o que segue a camera do jogo
    const wotm_freecam::View freeView = wotm_freecam::update(S.snap.cam);
    const wotm::GameCamera gameCam = S.snap.cam;
    if (freeView.override)
        S.snap.cam = wotm_freecam::camera(gameCam, freeView.pose);
    const wotm::GameCamera gc = S.scene.show(S.snap);
    S.snap.cam = gameCam;
    if (freeView.override)
        wotm_freecam::applyToScene(S.scene, gameCam, freeView.pose);
    if (wotm::ScreenRect r{}; wotm_live::shellFadeRect(ram, r))
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

    BeginTextureMode(rt);
    ClearBackground(BLACK);
    rlSetClipPlanes(0.5, 30000.0);
    Camera3D c3{};
    c3.position = {gc.pos[0], gc.pos[1], gc.pos[2]};
    c3.target = {gc.pos[0] + gc.fwd[0], gc.pos[1] + gc.fwd[1], gc.pos[2] + gc.fwd[2]};
    c3.up = {gc.up[0], gc.up[1], gc.up[2]};
    c3.fovy = gc.fovy;
    c3.projection = CAMERA_PERSPECTIVE;
    BeginMode3D(c3);
    wotm_live::g_stage = "draw";
    S.scene.draw(ram, false);
    wotm_live::g_stage = "idle";
    EndMode3D();
    S.scene.updateText(ram);   // texto do jogo (menu de pausa, HUD): sprites do GS lidos de fontPacket
    S.scene.drawQuads(S.scene.screenPolys, static_cast<float>(RW), static_cast<float>(RH));   // HUD do ScreenPolys (icone de vida)
    S.scene.drawScreenRects(static_cast<float>(RW), static_cast<float>(RH), true);   // fades despachados antes do texto
    BeginMode3D(c3);   // lista de alta prioridade: depois desses fades (ShellRtLoop::endField), antes do texto
    S.scene.drawHp(ram);
    EndMode3D();
    S.scene.drawQuads(S.scene.text, static_cast<float>(RW), static_cast<float>(RH));
    S.scene.drawScreenRects(static_cast<float>(RW), static_cast<float>(RH));   // os demais fades, por cima de tudo
    EndTextureMode();

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

    // quadro na janela, mantendo a proporcao (faixas so se a janela sair de 4:3..32:9 ou com WOTM_ASPECT fixo)
    const float sw = static_cast<float>(GetScreenWidth()), sh = static_cast<float>(GetScreenHeight());
    const float k = std::min(sw / RW, sh / RH);
    const Rectangle src{0.f, 0.f, static_cast<float>(RW), -static_cast<float>(RH)};
    const Rectangle dst{(sw - RW * k) * 0.5f, (sh - RH * k) * 0.5f, RW * k, RH * k};
    // Sem mistura: o alfa do quadro e o que as misturas do GS deixaram (o comum baixa, o aditivo sobe para 1,0), nao opacidade. Com
    // BLEND_ALPHA sobre o preto a cena saia escurecida pelo alfa, e os quadrados aditivos (halo dos power-ups) viravam retangulos claros.
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM);
    DrawTexturePro(rt.texture, src, dst, Vector2{0.f, 0.f}, 0.f, WHITE);
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
            std::snprintf(line, sizeof line, "jogo %.1f fps | tela %.1f fps", gameFps, screenFps);
            DrawText(line, 11, 11, 20, BLACK);
            DrawText(line, 10, 10, 20, gameFps >= 55.f ? GREEN : (gameFps >= 30.f ? YELLOW : RED));
        }
    }

    {   // menu de debug (F1 abre/fecha; com ele aberto, 1..6 alternam). Setas e Enter sao do pad (ps2_pad.cpp): nao servem aqui.
        // So escreve g_wotmCheats; quem aplica e a thread do EE (monsterUpdateCheats, wotm_overrides.cpp).
        static bool open = false;
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
            if (n != c)
            {
                g_wotmCheats = n;
                std::fprintf(stderr, "[debug] trapacas 0x%x -> 0x%x\n", c, n);
                c = n;
            }
        }
        const uint32_t s2 = (c & kCheatStaminaMask) / kCheatStamina1;
        static const char *const stName[3] = {"normal", "cheia", "furia (130)"};
        const int fs = 20, x = 10, top = 40;
        if (open)
        {
            char lines[8][64];
            int nl = 0;
            std::snprintf(lines[nl++], 64, "DEBUG (F1 fecha)");
            std::snprintf(lines[nl++], 64, "1  Invencivel        %s", (c & kCheatInvincible) ? "LIGADO" : "-");
            std::snprintf(lines[nl++], 64, "2  Stamina           %s", stName[s2]);
            std::snprintf(lines[nl++], 64, "3  Especial pronto   %s", (c & kCheatSpecial) ? "LIGADO" : "-");
            std::snprintf(lines[nl++], 64, "4  Cloak (invisivel) %s", (c & kCheatCloak) ? "LIGADO" : "-");
            std::snprintf(lines[nl++], 64, "5  Modo deus do jogo %s", (c & kCheatGod) ? "LIGADO" : "-");
            std::snprintf(lines[nl++], 64, "6  Vencer a luta (derruba as IAs)");
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
            std::snprintf(line, sizeof line, "debug:%s%s%s%s%s", (c & kCheatInvincible) ? " INV" : "", s2 == 1 ? " STA" : (s2 == 2 ? " FURIA" : ""),
                          (c & kCheatSpecial) ? " ESP" : "", (c & kCheatCloak) ? " CLOAK" : "", (c & kCheatGod) ? " DEUS" : "");
            DrawText(line, x + 1, top + 1, fs, BLACK);
            DrawText(line, x, top, fs, ORANGE);
        }
    }

    wotm_freecam::overlay(freeView);   // linha de ajuda da camera livre (F5)

    if (++S.frame % 300 == 0 && std::getenv("PS2X_STATS"))
        std::fprintf(stderr, "[native] frame %ld itens=%zu ceu=%zu rtx=%s\n", S.frame, S.scene.items.size(), S.scene.sky.size(),
                     S.rtxPath.c_str());
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
static bool exportFrame(const char *) { return false; }
static std::string resources() { return {}; }
}
static bool wotmDrawNative(const uint8_t *) { return false; }
void wotmCaptureFrame(const uint8_t *) {}
void wotmCaptureScene(const uint8_t *) {}
void wotmFlareCapture(const uint8_t *) {}
void wotmScreenRect(const uint8_t *, uint32_t, bool) {}
void wotmLetterbox(const uint8_t *) {}
void wotmGsPktQueueSent() {}
void wotmFontSent() {}
void wotmScreenPolys(const uint8_t *) {}
void wotmShellFade(int, int, bool) {}
void wotmLoadFreeze(bool) {}
void wotmCaptureFadeStep(const uint8_t *) {}
void wotmPickupHighlights(const uint8_t *) {}
#endif
