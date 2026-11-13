// Modo Arcade (NATIVE.md, "Runtime: modo arcade"): estado compartilhado entre o menu (wotm_mainmenu.inc e wotm_padsel.inc, que
// armam e desarmam) e os ganchos da luta (native/runtime/wotm_arcade.inc, incluido pelo wotm_overrides.cpp). Plano em
// docs/investigacoes/2026-11-13/modo_arcade_plano.md.
#pragma once
#include <atomic>
#include <cstdio>

namespace wotm_arcade
{
// Armado pela linha ARCADE das telas 2 e 5 (wotm_mainmenu.inc): o fluxo pula a selecao de fase (wotm_padsel.inc, changeScreenHook);
// a primeira sessao depois disso sorteia as 5 lutas e liga o g_on (wotm_arcade.inc). O menu principal desarma e desliga.
inline std::atomic<bool> g_armed{false};
inline std::atomic<bool> g_on{false};
inline void menuDisarm(const char *where)
{
    const bool a = g_armed.exchange(false), o = g_on.exchange(false);
    if (a || o)
        std::fprintf(stderr, "[arcade] desarmado (%s)\n", where);
}
}
