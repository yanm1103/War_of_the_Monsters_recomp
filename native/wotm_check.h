// Invariantes do port: o que nunca deveria acontecer numa luta sai no log como uma linha "[check] <chave> n=<vezes> | <detalhe>",
// para um script ler sem ninguem olhar a tela (tools/smoke.ps1; NATIVE.md, "Runtime invariants"). Cada chave sai na 1a, 10a,
// 100a... vez: o log nao enche e a ultima linha de uma chave diz a ordem de grandeza.
#pragma once

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace wotm::check {

struct Slot { char key[48]; uint32_t n; };
inline Slot g_slots[64];
inline std::mutex g_mutex;

// chave: nome fixo do invariante, com o que o distingue (o monstro, o preset); o detalhe e livre
inline void report(const char *key, const char *fmt, ...)
{
    uint32_t n = 0;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        for (Slot &s : g_slots)
        {
            if (!s.key[0])
                std::snprintf(s.key, sizeof s.key, "%s", key);
            if (std::strncmp(s.key, key, sizeof s.key - 1) == 0) { n = ++s.n; break; }
        }
    }
    if (!n)
        return;   // tabela cheia
    uint32_t p = 1;
    while (p < n && p < 1000000000u) p *= 10;
    if (p != n)
        return;
    char detail[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(detail, sizeof detail, fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "[check] %s n=%u | %s\n", key, n, detail);
}

inline bool finite3(const float v[3]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

}  // namespace wotm::check
