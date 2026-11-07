// wotm_freecam.inc: camera livre de debug, lado da janela (incluido por wotm_live.inc; escopo global de ps2_runtime.cpp).
// Estado compartilhado e convencao da pose em native/wotm_freecam.h; o lado do jogo (pose no _cs da vista 0, pad em repouso) fica
// no wotm_overrides.cpp.
//
// Teclas (thread da apresentacao, a cada desenho, com o tempo real):
//   F5 liga/desliga (comeca onde a camera do jogo esta; ao desligar, a camera do jogo volta sozinha para o monstro)
//   W/S frente/tras (para onde olha), A/D lados, Q/E desce/sobe; Shift x5, Ctrl x0,2; roda do mouse muda a velocidade base
//   botao direito do mouse arrastando ou setas: olhar
//   Tab: devolve o pad ao jogo (o monstro anda, relativo a camera livre) e para a camera no teclado; o mouse continua olhando
// Com a camera ligada e o Tab desligado, o jogo recebe o pad em repouso (as teclas do pad tambem sao WASD/Q/Shift/setas).
// Pausado (F7) a thread do EE nao roda e o jogo nao refaz a vista: o desenho nativo usa a pose direto, sem o HUD do hier (que o
// jogo poe na frente da camera dele) e com o ceu levado junto com o olho. O resto do instantaneo fica como o jogo montou: LOD,
// particulas recortadas e predios "fantasma" seguem a camera do quadro pausado.
#include "wotm_freecam.h"

bool wotmPaused();   // wotm_foto.inc: a thread do EE esta segura pela pausa (F7)
bool wotmPhotoActive();   // wotm_photo.inc: modo foto ligado (F5 fica sem efeito)

namespace wotm_freecam
{
float speed = 150.f;   // unidades do mundo por segundo (roda do mouse)

struct View {
    bool on = false;
    bool override = false;   // o desenho usa a pose (jogo pausado); fora da pausa o jogo ja desenha com ela
    WotmFreeCamPose pose;
    float zoom = 1.f;        // modo foto: tan(fov/2) da camera livre sobre o do jogo (< 1 aproxima)
    bool photo = false;      // vista do modo foto (wotm_photo.inc): o HUD segue a camera em vez de sumir
    bool hideHud = false;    // modo foto com o HUD escondido
};

static View update(const wotm::GameCamera &gc)
{
    View v;
    bool on = g_wotmFreeCam.load(std::memory_order_relaxed);
    // Teste sem teclado (WOTM_FREECAM=<s>): liga s segundos depois da primeira fase na tela, sobe e gira sozinha por 10 s e desliga.
    // Com pose (WOTM_FREECAM=<s>@x,y,z,rumo,inclinacao; graus, rumo 0 olha para +Y e cresce para +X): liga ali, fica parada e nao desliga
    static const double autoAt = std::getenv("WOTM_FREECAM") ? std::atof(std::getenv("WOTM_FREECAM")) : -1.0;
    static float fixedPose[5];
    static const bool fixed = [] {
        const char *e = std::getenv("WOTM_FREECAM");
        const char *at = e ? std::strchr(e, '@') : nullptr;
        return at && std::sscanf(at + 1, "%f,%f,%f,%f,%f", &fixedPose[0], &fixedPose[1], &fixedPose[2], &fixedPose[3], &fixedPose[4]) == 5;
    }();
    static double firstT = -1.0;
    if (firstT < 0.0) firstT = GetTime();
    const double autoT = autoAt >= 0.0 ? GetTime() - firstT - autoAt : -1.0;
    static int autoStage = 0;   // 0 esperando, 1 andando, 2 terminado
    bool toggle = IsKeyPressed(KEY_F5) && !wotmPhotoActive();
    if (autoAt >= 0.0 && ((autoStage == 0 && autoT >= 0.0) || (autoStage == 1 && autoT >= 10.0 && !fixed)))
    {
        toggle = on == (autoStage == 1);
        std::fprintf(stderr, "[freecam] teste automatico: %s (t=%.1f s)\n", autoStage == 0 ? "liga" : "desliga", GetTime() - firstT);
        ++autoStage;
    }
    if (toggle)
    {
        on = !on;
        if (on)
        {
            std::lock_guard<std::mutex> lk(g_wotmFreeCamM);
            WotmFreeCamPose &p = g_wotmFreeCamPose;
            for (int i = 0; i < 3; ++i) p.pos[i] = gc.pos[i];
            p.heading = std::atan2(gc.fwd[0], gc.fwd[1]);   // mathfRotationFromVector
            p.pitch = std::atan2(gc.fwd[2], std::sqrt(gc.fwd[0] * gc.fwd[0] + gc.fwd[1] * gc.fwd[1]));
            if (fixed && autoStage == 1)
            {
                for (int i = 0; i < 3; ++i) p.pos[i] = fixedPose[i];
                p.heading = fixedPose[3] * 3.14159265f / 180.f;
                p.pitch = fixedPose[4] * 3.14159265f / 180.f;
            }
            g_wotmFreeCamPad = false;
        }
        g_wotmFreeCam = on;
        std::fprintf(stderr, "[freecam] %s\n", on ? "ligada" : "desligada");
    }
    if (!on)
        return v;
    if (IsKeyPressed(KEY_TAB))
    {
        g_wotmFreeCamPad = !g_wotmFreeCamPad.load(std::memory_order_relaxed);
        std::fprintf(stderr, "[freecam] pad %s\n", g_wotmFreeCamPad ? "com o jogo" : "bloqueado");
    }
    const bool pad = g_wotmFreeCamPad.load(std::memory_order_relaxed);
    const float dt = std::min(GetFrameTime(), 0.1f);
    const float wheel = GetMouseWheelMove();
    if (wheel != 0.f)
        speed = std::clamp(speed * std::pow(1.25f, wheel), 5.f, 20000.f);

    std::lock_guard<std::mutex> lk(g_wotmFreeCamM);
    WotmFreeCamPose &p = g_wotmFreeCamPose;
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
    {
        const Vector2 d = GetMouseDelta();
        p.heading += d.x * 0.003f;   // heading positivo gira para +X (para a direita, visto de cima)
        p.pitch -= d.y * 0.003f;
    }
    if (!pad)
    {
        const float turn = 1.5f * dt;
        if (IsKeyDown(KEY_LEFT)) p.heading -= turn;
        if (IsKeyDown(KEY_RIGHT)) p.heading += turn;
        if (IsKeyDown(KEY_UP)) p.pitch += turn;
        if (IsKeyDown(KEY_DOWN)) p.pitch -= turn;
    }
    p.pitch = std::clamp(p.pitch, -1.55f, 1.55f);
    p.heading = std::remainder(p.heading, 6.2831853f);
    if (!pad)
    {
        float r[3], f[3], u[3];
        wotmFreeCamAxes(p, r, f, u);
        float k = speed * dt;
        if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) k *= 5.f;
        if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) k *= 0.2f;
        const float mf = float(IsKeyDown(KEY_W)) - float(IsKeyDown(KEY_S));
        const float mr = float(IsKeyDown(KEY_D)) - float(IsKeyDown(KEY_A));
        const float mu = float(IsKeyDown(KEY_E)) - float(IsKeyDown(KEY_Q));
        for (int i = 0; i < 3; ++i)
            p.pos[i] += k * (mf * f[i] + mr * r[i]);
        p.pos[2] += k * mu;   // Z do mundo
    }
    if (autoStage == 1 && !fixed)
    {
        p.pos[2] += 30.f * dt;
        p.heading += 0.25f * dt;
    }
    v.on = true;
    v.pose = p;
    v.override = wotmPaused();
    return v;
}

// Pausado: tira da cena o que o jogo prende na frente da camera dele. O ceu fica onde esta: o jogo o desenha em skyCs->trans, fixo
// no mundo, nao no olho.
static void applyToScene(wotm::Scene &scene, const wotm::GameCamera &gc, const WotmFreeCamPose &p)
{
    (void)gc;
    (void)p;
    scene.hud.clear();
    scene.hp.clear();
    scene.hpOverlay.clear();
    scene.overlay.clear();
}

static wotm::GameCamera camera(const wotm::GameCamera &gc, const WotmFreeCamPose &p, float zoom = 1.f)
{
    wotm::GameCamera c = gc;
    float r[3];
    wotmFreeCamAxes(p, r, c.fwd, c.up);
    for (int i = 0; i < 3; ++i) c.pos[i] = p.pos[i];
    if (zoom != 1.f)
        c.fovy = 2.f * std::atan(zoom * std::tan(gc.fovy * 3.14159265f / 360.f)) * 180.f / 3.14159265f;
    return c;
}

} // namespace wotm_freecam
// Os controles aparecem na legenda de icones de baixo (wotmFreeCamLegend, wotm_photo.inc)
