// Fases custom (MODS.md, "Custom stages"; registro em native/wotm_mods.hpp). Incluido por wotm_overrides.cpp (WOTM_MATCH, userintMain)
// e pelo wotm_padsel.inc (changeScreen(8, 99)); thread do EE.
// A custom mora numa vaga 18..23 do indice de fases ate a confirmacao. Ai `activate` troca o m_levelNum pela base e escreve o id da
// custom em GameLevelNames[base]: os arquivos saem do nome (ResolveCommandLineArguments -> whichLevel -> LVL/, PTS/, RST/; o registro
// os aponta para os da base) e o codigo por fase sai do indice (InitRTState copia o m_levelNum para o m_levelId). Com isso o cartao de
// carga, o banco de som, a musica, a agua e os leitores nativos do m_levelNum veem a base, e o fileStatus (o patch, as texturas em PNG)
// ve o id. `restore` devolve a tabela na confirmacao seguinte e na volta aos menus (userintMain); RESTART, F10 e "jogar de novo" nao
// passam por ali e continuam na custom.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "wotm_mods.hpp"
#include "wotm_syms.h"

namespace wotm_stages
{
constexpr uint32_t kNames = wotm::sym::GameLevelNames_006EF748;   // char[28][10]
constexpr uint32_t kLevelNum = wotm::sym::shellInfo + 0x2BB0;     // Shell::m_levelNum

inline char g_orig[28][10] = {};
inline bool g_saved[28] = {};
inline const wotm::mods::Stage *g_active = nullptr;   // a custom da sessao (nullptr = fase do jogo)

inline uint8_t *at(uint8_t *rdram, uint32_t a) { return rdram + (a & 0x1FFFFFFu); }
inline uint32_t levelNum(uint8_t *rdram) { uint32_t v; std::memcpy(&v, at(rdram, kLevelNum), 4); return v; }

// Devolve as entradas de GameLevelNames que uma custom trocou
inline void restore(uint8_t *rdram)
{
    for (int i = 0; i < 28; ++i)
        if (g_saved[i] && std::memcmp(at(rdram, kNames + 10u * uint32_t(i)), g_orig[i], 10) != 0)
            std::memcpy(at(rdram, kNames + 10u * uint32_t(i)), g_orig[i], 10);
    g_active = nullptr;
}

// m_levelNum numa vaga de custom: passa a ser a base, e GameLevelNames[base] o id dela. Devolve a custom (nullptr = fase do jogo).
inline const wotm::mods::Stage *activate(uint8_t *rdram)
{
    const uint32_t lvl = levelNum(rdram);
    const wotm::mods::Stage *st = lvl < 28 ? wotm::mods::Registry::get().stageBySlot(int(lvl)) : nullptr;
    if (!st)
        return nullptr;
    const int base = st->base->level;
    if (!g_saved[base])   // o original e copiado na primeira escrita
    {
        std::memcpy(g_orig[base], at(rdram, kNames + 10u * uint32_t(base)), 10);
        g_saved[base] = true;
    }
    char name[10] = {};
    std::memcpy(name, st->id.c_str(), std::min<size_t>(st->id.size(), 9));
    std::memcpy(at(rdram, kNames + 10u * uint32_t(base)), name, 10);
    const uint32_t b = uint32_t(base);
    std::memcpy(at(rdram, kLevelNum), &b, 4);
    g_active = st;
    std::fprintf(stderr, "[fases] custom %s (\"%s\", %s): vaga %u -> base %s (%d)\n", st->id.c_str(), st->name.c_str(), st->mod.c_str(), lvl,
                 st->base->name, base);
    return st;
}
} // namespace wotm_stages
