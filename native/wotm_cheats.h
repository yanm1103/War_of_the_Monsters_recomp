// Trapacas de debug do runtime: bits de g_wotmCheats (definido em native/runtime/wotm_overrides.cpp).
// A janela (menu F1 de wotm_live.inc) escreve; a thread do EE (hook de Monster::update/takeDamage) le e aplica nos jogadores.
#pragma once
#include <atomic>
#include <cstdint>

constexpr uint32_t kCheatInvincible = 1u << 0;    // takeDamage nao age no jogador
constexpr uint32_t kCheatStamina1 = 1u << 1;      // campo de 2 bits: 0 normal, 1 cheia, 2 furia
constexpr uint32_t kCheatStaminaMask = 3u << 1;
constexpr uint32_t kCheatSpecial = 1u << 3;       // especial sempre liberado
constexpr uint32_t kCheatCloak = 1u << 4;         // CLOAK sempre ligado
constexpr uint32_t kCheatGod = 1u << 5;           // m_godMode do proprio jogo

extern std::atomic<uint32_t> g_wotmCheats;
extern std::atomic<bool> g_wotmCheatWin;          // pedido de "vencer a luta" (consumido no quadro seguinte)
// Pedido de "vida baixa": mascara de jogadores (bit i = m_monsters[i], i 0..3; 0 = nenhum pedido). Consumido no quadro seguinte:
// a vida de cada jogador vivo da mascara vira 5% da maxima (minimo 1).
extern std::atomic<uint32_t> g_wotmCheatLowHp;
