// Modo Last Monster Standing (NATIVE.md, "Runtime: last monster standing"): estado compartilhado entre o menu (wotm_mainmenu.inc,
// que arma, e wotm_padsel.inc, que desarma no menu principal), o co-op (a tela com quem sobrou) e os ganchos da luta
// (native/runtime/wotm_lms.inc, incluido pelo wotm_overrides.cpp).
#pragma once
#include <atomic>
#include <cstdio>

namespace wotm_lms
{
// Armado pela linha LAST MONSTER STANDING das telas 2 e 5 (wotm_mainmenu.inc): o fluxo e o do free-for-all; o InitPlayerLives da
// luta liga o g_on (wotm_lms.inc). O menu principal desarma e desliga.
inline std::atomic<bool> g_armed{false};
inline std::atomic<bool> g_on{false};
inline bool on() { return g_on.load(std::memory_order_relaxed); }
inline void menuDisarm(const char *where)
{
    const bool a = g_armed.exchange(false), o = g_on.exchange(false);
    if (a || o)
        std::fprintf(stderr, "[lms] desarmado (%s)\n", where);
}
}
