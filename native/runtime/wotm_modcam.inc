// wotm_modcam.inc: camera moderna (mod, WOTM_MODCAM), lado da janela (incluido por wotm_live.inc; escopo global de ps2_runtime.cpp).
// Estado compartilhado em native/wotm_modcam.h; a pose, o analogico direito e a volta para tras do monstro ficam na thread do EE
// (wotm_overrides.cpp, modCamApply). Aqui, uma vez por apresentacao: enquanto a camera moderna esta no comando (luta, POV normal),
// com a janela em foco, sem pausa (F7) e sem a camera livre (F5), o cursor fica preso e escondido e o movimento do mouse e somado
// para o EE ler no proximo normalUpdate. Fora disso (menu, pausa do jogo, corpo a corpo, cinema) o cursor volta.
#include "wotm_modcam.h"

namespace wotm_modcam
{
static void present()
{
    static bool locked = false, skip = false;
    const bool want = wotmModCamDriving() && IsWindowFocused() && !wotmPaused() && !g_wotmFreeCam.load(std::memory_order_relaxed);
    if (want != locked)
    {
        if (want)
            DisableCursor();
        else
            EnableCursor();
        locked = want;
        skip = true;   // o primeiro delta depois da troca traz o salto do cursor para o centro
        return;
    }
    if (!locked)
        return;
    const Vector2 d = GetMouseDelta();
    if (skip)
    {
        skip = false;
        return;
    }
    if (d.x == 0.f && d.y == 0.f)
        return;
    std::lock_guard<std::mutex> lk(g_wotmModCamM);
    g_wotmModCamMouse.dx += d.x;
    g_wotmModCamMouse.dy += d.y;
}
} // namespace wotm_modcam
