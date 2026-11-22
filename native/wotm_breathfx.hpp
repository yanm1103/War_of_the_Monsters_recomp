// Sopro proprio de um monstro custom ("breathFx" do monster.json; MODS.md, "Custom monsters"): camadas de particulas desenhadas
// pelo port no lugar do bafo do Togera (FireBreath). O jogo continua dono do golpe (alcance, dano, duracao, som); o port so troca o
// visual: esconde os sistemas do bafo que o FireBreath::Activate cria (presets "hide") e desenha as camadas a partir da boca do
// monstro enquanto o FireBreath esta ligado.
//
// Fios: o gancho do FireBreath::TestCollis (thread do EE, runtime/wotm_custom.inc) grava em g_live[vaga do monstro] o monstro, a
// config e onde a linha de visada do bafo parou (cenario ou monstro); a apresentacao (runtime/wotm_breathfx.inc) le dali, le a boca
// e o estado do FireBreath da foto da RAM do quadro, simula as particulas e entrega quadrados ao Particles (wotm_fx.hpp, beams).
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace wotm::breathfx {

// Polinomio na idade normalizada da particula (0 no nascimento, 1 na morte): c0 + c1 t + c2 t^2 ...; vazio = 1
struct Poly {
    std::vector<float> c;
    float at(float t) const {
        if (c.empty()) return 1.f;
        float v = 0.f, p = 1.f;
        for (float k : c) { v += k * p; p *= t; }
        return v;
    }
};

// Uma camada: um emissor de particulas. Unidades do mundo do jogo e segundos.
struct Layer {
    std::string texture;        // PNG (caminho no PC); vazio = mancha redonda
    bool hit = false;           // "at": "hit" = nasce onde o sopro bate (so enquanto bate em algo); "mouth" = na boca
    bool stretch = false;       // "mode": "stretch" = risco esticado na direcao da velocidade; "sprite" = quadrado de frente
    bool relative = true;       // as particulas andam com a boca (a cabeca vira e o sopro acompanha); padrao: so as da boca
    bool once = false;          // "once": solta "rate" particulas de uma vez quando a camada liga, e nada mais
    bool additive = true;       // mistura aditiva (Cs As + Cd); false = comum
    bool rotate = false;        // giro inicial sorteado (sprite)
    float rate = 10.f;          // particulas por segundo
    float speed = 0.f, speedRandom = 0.f;   // para a frente da boca (de +-speedRandom)
    float radial = 0.f;         // para fora do ponto de nascimento (espalha a partir do centro da area)
    float up = 0.f;             // para cima (Z do mundo)
    float lifeMin = 1.f, lifeMax = 1.f;
    float sizeMin = 10.f, sizeMax = 10.f;   // lado do quadrado (no risco: a largura)
    float spread = 0.f;         // meio-lado do cubo onde nasce
    float color[4] = {1.f, 1.f, 1.f, 1.f};
    Poly r, g, b, a, sizeMul;
    float grow = 0.2f, shrink = 0.6f;   // risco: cresce ate grow da vida e encolhe a partir de shrink (fracoes)
};

struct Config {
    std::vector<Layer> layers;
    // presets do jogo que somem quando o FireBreath deste monstro os cria (Activate e TestCollis, com o que o acerto cria dentro
    // dele): bafo, miolo e faiscas do Togera; 80/81 = clarao e faiscas do acerto (para quem traz camadas de "hit")
    std::vector<int> hide{8, 51, 148};
    float scale = 1.f;                   // tamanhos, velocidades e area de todas as camadas
    bool hides(uint32_t preset) const {
        for (int p : hide)
            if (uint32_t(p) == preset) return true;
        return false;
    }
};

// Ligacao EE -> apresentacao, uma vaga por TheGame::m_monsters[i]. stamp sobe a cada FireBreath::TestCollis do monstro.
struct Live {
    std::atomic<uint32_t> m{0};
    std::atomic<const Config *> cfg{nullptr};
    std::atomic<float> limit{-1.f};   // distancia da boca ate onde a linha de visada parou (-1 = sem teste ainda)
    std::atomic<bool> blocked{false}; // a linha parou em algo (cenario ou monstro) antes do alcance
    std::atomic<uint32_t> stamp{0};
};
inline Live g_live[8];
// fx[i] criado por um monstro com breathFx, preset escondido: preset + 1 (0 = desenha). Vale enquanto o preset de fx[i] for o
// gravado (Particles::hideTags)
inline std::atomic<uint32_t> g_hide[100];

}  // namespace wotm::breathfx
