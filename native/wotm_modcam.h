// Camera moderna (mod, WOTM_MODCAM=1; caixa "Mods" do launcher). Na camera normal da luta (POV 0, Camera::normalUpdate) o rumo da
// camera e do jogador e so do analogico direito: o monstro anda e vira sem levar a camera junto. Distancia e altura continuam as que
// o jogo escolhe para cada monstro; corpo a corpo, nocaute, vitoria, cinemas e tela unificada seguem com a camera original. Tudo na
// thread do EE (wotm_overrides.cpp): a pose vai para o _cs da camera depois do normalUpdate e a do jogo volta antes do seguinte.
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>

extern std::atomic<bool> g_wotmModCam;               // mod ligado (WOTM_MODCAM)
extern std::atomic<int64_t> g_wotmModCamDrivingMs;   // relogio (steady_clock, ms) do ultimo quadro em que a pose foi nossa

inline int64_t wotmModCamNowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// A camera moderna esta no comando agora (o ultimo normalUpdate com a pose nossa foi ha pouco): o analogico direito e dela
inline bool wotmModCamDriving()
{
    return g_wotmModCam.load(std::memory_order_relaxed) && wotmModCamNowMs() - g_wotmModCamDrivingMs.load(std::memory_order_relaxed) < 250;
}
