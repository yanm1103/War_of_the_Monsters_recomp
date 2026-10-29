// Mais CPUs no free for all do que o menu do jogo deixa (NATIVE.md, "Mais CPUs pelo menu"): ate 8 monstros na luta, 0 a 7 CPUs com
// 1 jogador (tela 12) e 0 a 8 - N com N = 2 a 4 (tela 13). Os ganchos das telas (wotm_overrides.cpp) guardam o numero escolhido; o
// menu segue com o que o jogo conhece (3 CPUs com 1 jogador, 2 com 2, nenhuma com 3 e 4), e o wotm_padsel.inc poe as CPUs a mais na
// selecao e grava o numero de verdade quando a fase e confirmada (1 e 2 jogadores) ou na saida da selecao (3 e 4).
#pragma once
#include <atomic>

namespace wotm_menucpus
{
constexpr int kMonsters = 8;          // o teto da WOTM_MATCH (8 monstros na luta)
constexpr int kMax = kMonsters - 1;   // 1 jogador + 7 IAs
constexpr int maxFor(int players) { return kMonsters - players; }
inline std::atomic<int> g_want{0};          // CPUs pedidas acima do que o jogo conhece; 0: vale o m_numAIs
inline std::atomic<int> g_menuPlayers{2};   // jogadores do multijogador (2 a 4, escolha de controles): o maximo da tela 13
// Jogadores sob a IA escolhidos na escolha de controles (bit i: jogador i + 1; wotm_padsel.inc grava, os ganchos do WOTM_AIVSAI em
// wotm_overrides.cpp e wotm_ai.inc leem); 0 fora do multijogador
inline std::atomic<unsigned> g_botMask{0};
}
