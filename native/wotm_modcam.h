// Camera moderna (mod, WOTM_MODCAM=1; caixa "Mods" do launcher). Na camera normal da luta (POV 0, Camera::normalUpdate) o jogador
// gira a camera em volta do proprio monstro com o mouse ou o analogico direito; parado o comando por um tempo e com o monstro andando,
// ela volta devagar para tras dele. Distancia e altura continuam as que o jogo escolhe para cada monstro; corpo a corpo, nocaute,
// vitoria, cinemas e tela unificada seguem com a camera original. Dois lados:
//  - thread do EE (wotm_overrides.cpp): poe a pose no _cs da camera depois do normalUpdate e devolve a do jogo antes do seguinte;
//  - janela (wotm_modcam.inc, thread da apresentacao): prende o cursor enquanto a camera esta no comando e soma o movimento do mouse.
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>

struct WotmModCamMouse {
    float dx = 0.f, dy = 0.f;   // pixels desde a ultima leitura da thread do EE
};

extern std::atomic<bool> g_wotmModCam;               // mod ligado (WOTM_MODCAM)
extern std::atomic<int64_t> g_wotmModCamDrivingMs;   // relogio (steady_clock, ms) do ultimo quadro em que a pose foi nossa
extern std::mutex g_wotmModCamM;
extern WotmModCamMouse g_wotmModCamMouse;            // protegido por g_wotmModCamM

inline int64_t wotmModCamNowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// A camera moderna esta no comando agora (o ultimo normalUpdate com a pose nossa foi ha pouco)
inline bool wotmModCamDriving()
{
    return g_wotmModCam.load(std::memory_order_relaxed) && wotmModCamNowMs() - g_wotmModCamDrivingMs.load(std::memory_order_relaxed) < 250;
}
