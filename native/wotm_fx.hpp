// Particulas e efeitos (poeira, fogo, faiscas, explosoes, rastros) lidos da RAM do EE e desenhados com raylib.
//
// No retail, `particlePrepDraw` (particleMove + o *PacketBuild de cada tipo) monta um pacote VIF por sistema e `particleDraw`
// (0x21B378; o laco que espera o VU1 fica em 0x21B8A0) manda esses pacotes para o microcodigo de particulas do VU1, que expande
// cada particula em sprite/linha/faixa. Aqui o corte e antes do VU1: le o estado de cada sistema (`_ParticleType`,
// include/hieri_types.h) e desenha direto. Detalhes e o que ainda e hipotese: docs/FORMATS.md ("Particulas").
//
// Confirmado no codigo da CPU (asm de common/particle e common/partdraw):
//   - fx[100] em 0x5BA280 (0x2EE0 bytes cada); lista ativa: fxActiveTop (0x6F88EC) -> fxList[i].next (+8, 12 bytes por entrada), -1 = fim
//   - por particula: pos[i] (FVECTOR; w = tamanho, raio: particleFOVcalcMaxRadius usa maxSize + expansionRate*lifetime),
//     vel[i] (FVECTOR), age[i] (int16, em campos); numParticles em +0x2ED0
//   - so desenha com info.attribs & 0x400 (pacote montado), numParticles >= 1 (sprine: >= 2) e tipo != 11
//   - tipo = vis.attribs (+0x2E90): 1 linha, 2 sweepTrail, 3 sprite, 4 texSprite, 5 stripe, 6 strite, 8 sprine, 9 kite,
//     13 frag, 15 dustTrail, 16 wake (tabela de saltos de particlePrepDraw)
//   - cor (loadColors): tabela RGBA em 0x256C10 (0x80 = 1,0), vis.numColors cores a partir de vis.colorIndex; t = age * colorsPerFrame,
//     cor = lerp(cor[t >> 12], cor[(t >> 12) + 1], (t & 0xFFF) / 4096); 0 cores = 0x80808080, 1 cor = constante
//   - textura: TextureInfo[vis.texInfoIndex] (0x5B9E80: numTextures, texIndex, du, dv); texId[texIndex] (0x6DFAB0, u16) indexa o
//     texInfo global e TEX0[texIndex] (0x6DF6B0, u64) e o registrador TEX0 (psm/CBP). texSprite escolhe o quadro por particula:
//     attribs&1 aleatorio, attribs&2 uvI[i] >> 2, senao (age * mapsPerFrame) >> 12 (attribs&4: trava no ultimo, senao modulo)
//   - mistura: vis.alphaContext e o registrador ALPHA do GS (0x44 = (Cs-Cd)*As+Cd, 0x48 = Cs*As+Cd aditivo)
//   - ordem: do mais longe ao mais perto pela profundidade de emit.ParticleToEmitVec (+0x2CA0) + vis.priority; opacos (alpha 0) antes
// Lido no microcodigo de particulas do VU1 (pacote de carga em 0x253060, contexto padrao em 0x256B90; sprite chama 0x59, linha 0x0B,
// strite 0x151):
//   - sprite: o VU1 manda um SPRITE do GS (retangulo alinhado a tela) de meio-lado = pos.w / 2 * foco / profundidade, limitado a
//     foco / 2 (medido pela checagem VU1 x nativo em sprite e texSprite: a leitura antiga, pos.w inteiro, dava o dobro); ou seja,
//     pos.w e o lado em unidades de mundo e o meio-lado nunca passa de metade da profundidade. Os bits baixos de pos.w indexam os cantos de
//     UV (3 normal, 1 espelha U, 2 espelha V, 0 os dois); a cor sai da tabela pela idade, como no loadColors
//   - linha: de pos ate pos - vel (a cauda e projetada com a matriz do quadro anterior: borrao de movimento da camera)
//   - strite: ver Particles::striteTriangle (cauda com a matriz do quadro anterior, prevViewMat; ST que inscreve o disco no triangulo e
//     alfa escalado pelo comprimento do rastro)
// Destroços (frag, 13) nao passam por aqui: Scene::collectParticles/drawSprites (atlas de celulas 8x8; pos.w = lado e bits baixos = celula/espelhamento).
// Sprine: Particles::sprineQuads (sprites ao longo da curva do microprograma). Ainda aproximado: as faixas (stripe, sweepTrail) ligam
// particulas consecutivas; kite vira sprite (wake e dustTrail seguem o microprograma: wakeVu, dustTrailVu);
// a cauda das linhas usa a matriz atual.
#pragma once
#include "wotm_syms.h"   // wotm::sym / wotm::fn: enderecos do ELF por nome (tools/native_syms.py --gen)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <memory>
#include <unordered_map>
#include <vector>

#include "raylib.h"
#include "rlgl.h"

// GL 1.1 (opengl32), so para ler a profundidade no teste de oclusao do lens flare; o raylib nao expoe leitura de profundidade
#ifdef _WIN32
extern "C" __declspec(dllimport) void __stdcall glReadPixels(int x, int y, int w, int h, unsigned format, unsigned type, void *data);
#else
extern "C" void glReadPixels(int x, int y, int w, int h, unsigned format, unsigned type, void *data);
#endif

namespace wotm {

// Antes do glReadPixels da profundidade (teste de oclusao do lens flare): com MSAA (wotm_msaa.hpp) o desenho vai a um framebuffer
// multiamostra, que o glReadPixels nao le; quem o usa poe aqui a resolucao da profundidade. Nulo: le o framebuffer em uso.
inline void (*g_beforeDepthRead)() = nullptr;

// Profundidade de uma vista lida sem a thread da janela esperar (tela dividida, Scene::drawFlaresFor; teste de oclusao do lens flare).
// Qualquer leitura do GL na thread da janela (glMapBuffer, glClientWaitSync, glGet*) a faz esperar o driver alcanca-la: no driver da
// NVIDIA, com a otimizacao em thread, ele anda ~6 ms atras com 4 vistas no Ryzen 5 5500, e cada espera desfaz o paralelismo entre a
// janela, o driver e a GPU (o redesenho de 4 vistas ia de ~16 para ~25 ms). Entao a janela so copia a profundidade da vista na GPU
// (glBlitFramebuffer para um renderbuffer proprio, dois por vista em rodizio), poe uma cerca e segue. Uma thread auxiliar, com um
// contexto GL compartilhado (janela GLFW escondida), espera a cerca na GPU, le a copia num PBO, espera a leitura e entrega o resultado;
// as esperas sao dela. O teste usa a copia pronta mais nova (1 ou 2 redesenhos atras), enquanto tiver no maximo kMaxAge pedidos de
// idade. Sem as funcoes do GL 3.2 ou sem o contexto auxiliar, ok() fica false e o flare volta a leitura direta.
extern "C" {
extern void *(*glad_glFenceSync)(unsigned, unsigned);
extern unsigned (*glad_glClientWaitSync)(void *, unsigned, unsigned long long);
extern void (*glad_glWaitSync)(void *, unsigned, unsigned long long);
extern void (*glad_glDeleteSync)(void *);
extern void (*glad_glFlush)(void);
extern void (*glad_glGenBuffers)(int, unsigned *);
extern void (*glad_glBindBuffer)(unsigned, unsigned);
extern void (*glad_glBufferData)(unsigned, ptrdiff_t, const void *, unsigned);
extern void *(*glad_glMapBuffer)(unsigned, unsigned);
extern unsigned char (*glad_glUnmapBuffer)(unsigned);
extern void (*glad_glGenFramebuffers)(int, unsigned *);
extern void (*glad_glBindFramebuffer)(unsigned, unsigned);
extern void (*glad_glGenRenderbuffers)(int, unsigned *);
extern void (*glad_glDeleteRenderbuffers)(int, const unsigned *);
extern void (*glad_glBindRenderbuffer)(unsigned, unsigned);
extern void (*glad_glRenderbufferStorage)(unsigned, unsigned, int, int);
extern void (*glad_glFramebufferRenderbuffer)(unsigned, unsigned, unsigned, unsigned);
extern void (*glad_glBlitFramebuffer)(int, int, int, int, int, int, int, int, unsigned, unsigned);
extern void (*glad_glDrawBuffer)(unsigned);
extern void (*glad_glReadBuffer)(unsigned);
// GLFW (estatico no raylib): o contexto auxiliar
void *glfwGetCurrentContext(void);
void glfwWindowHint(int, int);
void *glfwCreateWindow(int, int, const char *, void *, void *);
void glfwMakeContextCurrent(void *);
}
// O renderbuffer da copia tem de ter o formato da profundidade lida: o da render texture do raylib (GL_DEPTH_COMPONENT sem tamanho) ou,
// com MSAA, o resolvido pelo g_beforeDepthRead (GL_DEPTH_COMPONENT24). Quem liga o MSAA diz aqui se ele esta em uso.
inline bool (*g_depthReadIsMsaa)() = nullptr;

class AsyncDepth {
    struct Slot {
        unsigned rb = 0, fbo = 0;          // renderbuffer da copia e o framebuffer dele (contexto da janela)
        int w = 0, h = 0;
        unsigned fmt = 0;
        unsigned gen = 0;                  // muda quando o renderbuffer e refeito (a thread auxiliar refaz o framebuffer dela)
        std::atomic<int> busy{0};          // 1 = pedido na fila ou sendo lido pela thread auxiliar
    };
    struct Job { AsyncDepth *d; int slot; unsigned rb, gen; int w, h; void *fence; unsigned long serial; };

    // Thread auxiliar com o contexto compartilhado; uma para todas as vistas
    class Worker {
    public:
        static Worker &get() { static Worker w; return w; }
        bool ok() {   // na thread da janela, com o contexto dela corrente
            if (tried_) return ok_;
            tried_ = true;
            void *main = glfwGetCurrentContext();
            if (!main) return false;
            constexpr int kVisible = 0x00020004, kFocused = 0x00020001;
            glfwWindowHint(kVisible, 0);
            glfwWindowHint(kFocused, 0);
            ctx_ = glfwCreateWindow(16, 16, "wotm depth", nullptr, main);
            glfwWindowHint(kVisible, 1);
            glfwWindowHint(kFocused, 1);
            glfwMakeContextCurrent(main);
            if (!ctx_) {
                std::fprintf(stderr, "[flare] sem contexto GL auxiliar: oclusao do lens flare com leitura direta na tela dividida\n");
                return false;
            }
            std::thread(&Worker::run, this).detach();
            ok_ = true;
            return true;
        }
        void post(const Job &j) {
            { std::lock_guard<std::mutex> lk(m_); q_.push_back(j); }
            cv_.notify_one();
        }

    private:
        void run() {
            glfwMakeContextCurrent(ctx_);
            struct Fb { unsigned fbo = 0, rb = 0, gen = ~0u; };
            std::unordered_map<const void *, Fb> fbs;   // por (vista, slot)
            std::unordered_map<const void *, std::pair<unsigned, size_t>> pbos;
            for (;;) {
                Job j;
                {
                    std::unique_lock<std::mutex> lk(m_);
                    cv_.wait(lk, [&] { return !q_.empty(); });
                    j = q_.front();
                    q_.pop_front();
                }
                const void *key = &j.d->slots_[j.slot];
                Fb &fb = fbs[key];
                if (!fb.fbo) {
                    glad_glGenFramebuffers(1, &fb.fbo);
                    glad_glBindFramebuffer(kReadFb, fb.fbo);
                    glad_glDrawBuffer(0);
                    glad_glReadBuffer(0);
                }
                glad_glBindFramebuffer(kReadFb, fb.fbo);
                if (fb.rb != j.rb || fb.gen != j.gen) {
                    glad_glFramebufferRenderbuffer(kReadFb, kDepthAtt, kRenderbuffer, j.rb);
                    fb.rb = j.rb;
                    fb.gen = j.gen;
                }
                glad_glWaitSync(j.fence, 0, ~0ull);   // a copia da janela termina antes da leitura (na GPU)
                glad_glDeleteSync(j.fence);
                auto &pb = pbos[key];
                if (!pb.first) glad_glGenBuffers(1, &pb.first);
                const size_t bytes = size_t(j.w) * size_t(j.h) * 4;
                glad_glBindBuffer(kPack, pb.first);
                if (pb.second != bytes) {
                    glad_glBufferData(kPack, ptrdiff_t(bytes), nullptr, kStreamRead);
                    pb.second = bytes;
                }
                glReadPixels(0, 0, j.w, j.h, 0x1902 /* GL_DEPTH_COMPONENT */, 0x1406 /* GL_FLOAT */, nullptr);
                void *f2 = glad_glFenceSync(kGpuCommandsComplete, 0);
                glad_glFlush();
                unsigned r = 0;
                for (int tries = 0; tries < 50; ++tries) {
                    r = glad_glClientWaitSync(f2, kFlushBit, 100000000ull);   // 100 ms
                    if (r == kAlreadySignaled || r == kConditionSatisfied || r == kWaitFailed) break;
                }
                glad_glDeleteSync(f2);
                j.d->slots_[j.slot].busy = 0;   // leitura terminada: a janela pode reescrever a copia
                if (r == kAlreadySignaled || r == kConditionSatisfied) {
                    if (const void *p = glad_glMapBuffer(kPack, kReadOnly)) {
                        std::lock_guard<std::mutex> lk(j.d->m_);
                        j.d->res_.resize(bytes / 4);
                        std::memcpy(j.d->res_.data(), p, bytes);
                        j.d->resW_ = j.w;
                        j.d->resH_ = j.h;
                        j.d->resSerial_ = j.serial;
                    }
                    glad_glUnmapBuffer(kPack);
                }
                glad_glBindBuffer(kPack, 0);
                glad_glBindFramebuffer(kReadFb, 0);
            }
        }
        void *ctx_ = nullptr;
        bool tried_ = false, ok_ = false;
        std::mutex m_;
        std::condition_variable cv_;
        std::deque<Job> q_;
    };

public:
    bool ok() {
        if (!tried_) {
            tried_ = true;
            ok_ = glad_glFenceSync && glad_glClientWaitSync && glad_glWaitSync && glad_glDeleteSync && glad_glFlush && glad_glGenBuffers &&
                  glad_glBindBuffer && glad_glBufferData && glad_glMapBuffer && glad_glUnmapBuffer && glad_glGenFramebuffers &&
                  glad_glBindFramebuffer && glad_glGenRenderbuffers && glad_glDeleteRenderbuffers && glad_glBindRenderbuffer &&
                  glad_glRenderbufferStorage && glad_glFramebufferRenderbuffer && glad_glBlitFramebuffer && glad_glDrawBuffer &&
                  glad_glReadBuffer && Worker::get().ok();
            if (!ok_) std::fprintf(stderr, "[flare] sem as funcoes do GL 3.2: oclusao do lens flare com leitura direta na tela dividida\n");
        }
        return ok_;
    }
    // Pega em cpu a copia pronta mais nova (sem esperar); valid diz se ha uma recente
    void fetch(unsigned long redraw) {
        if (!ok()) { valid = false; return; }
        {
            std::lock_guard<std::mutex> lk(m_);
            if (resSerial_ > cpuSerial_ && resW_ > 0) {
                cpu.swap(res_);
                cw = resW_;
                ch = resH_;
                cpuSerial_ = resSerial_;
                resSerial_ = 0;
            }
        }
        valid = cpuSerial_ && redraw - cpuSerial_ <= kMaxAge;
    }
    // Copia na GPU a profundidade do framebuffer em que a vista desenha (`fbo`, w x h) e pede a leitura a thread auxiliar. Sem slot
    // livre (as duas copias anteriores ainda na fila), nao copia desta vez.
    void issue(int w, int h, unsigned fbo, unsigned long redraw) {
        if (!ok() || w <= 0 || h <= 0) return;
        Slot *s = nullptr;
        int k = 0;
        for (int i = 0; i < 2; ++i) {
            const int c = (next_ + i) & 1;
            if (!slots_[c].busy) { s = &slots_[c]; k = c; break; }
        }
        if (!s) return;
        next_ = k ^ 1;
        const bool msaa = g_depthReadIsMsaa && g_depthReadIsMsaa();
        const unsigned fmt = msaa ? kDepth24 : kDepthAny;
        if (!s->rb || s->w != w || s->h != h || s->fmt != fmt) {
            if (s->rb) glad_glDeleteRenderbuffers(1, &s->rb);
            glad_glGenRenderbuffers(1, &s->rb);
            glad_glBindRenderbuffer(kRenderbuffer, s->rb);
            glad_glRenderbufferStorage(kRenderbuffer, fmt, w, h);
            glad_glBindRenderbuffer(kRenderbuffer, 0);
            if (!s->fbo) glad_glGenFramebuffers(1, &s->fbo);
            glad_glBindFramebuffer(kFramebuffer, s->fbo);
            glad_glFramebufferRenderbuffer(kFramebuffer, kDepthAtt, kRenderbuffer, s->rb);
            glad_glDrawBuffer(0);
            glad_glReadBuffer(0);
            s->w = w;
            s->h = h;
            s->fmt = fmt;
            ++s->gen;
        }
        glad_glBindFramebuffer(kFramebuffer, fbo);
        if (g_beforeDepthRead) g_beforeDepthRead();   // MSAA: a profundidade resolvida fica como leitura
        glad_glBindFramebuffer(kDrawFb, s->fbo);
        glad_glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, kDepthBit, kNearest);
        glad_glBindFramebuffer(kFramebuffer, fbo);
        void *f = glad_glFenceSync(kGpuCommandsComplete, 0);
        glad_glFlush();   // a cerca chega ao outro contexto
        s->busy = 1;
        Worker::get().post({this, k, s->rb, s->gen, w, h, f, redraw});
    }
    std::vector<float> cpu;   // profundidade (0..1) de um redesenho recente, linhas de baixo para cima como o GL
    int cw = 0, ch = 0;
    bool valid = false;

private:
    static constexpr unsigned kPack = 0x88EB, kReadOnly = 0x88B8, kStreamRead = 0x88E1;   // GL_PIXEL_PACK_BUFFER, GL_READ_ONLY, GL_STREAM_READ
    static constexpr unsigned kGpuCommandsComplete = 0x9117, kFlushBit = 1, kAlreadySignaled = 0x911A, kConditionSatisfied = 0x911C,
                              kWaitFailed = 0x911D;
    static constexpr unsigned kFramebuffer = 0x8D40, kReadFb = 0x8CA8, kDrawFb = 0x8CA9, kRenderbuffer = 0x8D41, kDepthAtt = 0x8D00,
                              kDepthAny = 0x1902 /* GL_DEPTH_COMPONENT */, kDepth24 = 0x81A6, kDepthBit = 0x100, kNearest = 0x2600;
    static constexpr unsigned long kMaxAge = 4;   // redesenhos de idade da copia em cpu ainda usada
    bool tried_ = false, ok_ = false;
    Slot slots_[2];
    int next_ = 0;
    unsigned long cpuSerial_ = 0;
    std::mutex m_;                 // res_* (escritos pela thread auxiliar)
    std::vector<float> res_;
    int resW_ = 0, resH_ = 0;
    unsigned long resSerial_ = 0;
};

// Normal do vertice para o shader do Scene::draw: (fogBurnThru, 1, FGE + 2 * GS). O rlNormal3f normaliza; o shader le as razoes
// por y. FGE = neblina; GS = alfa cru do GS na textura e no vertice (As = At * Av / 16384). rlNormal3f(0, 0, 0) desliga os dois.
// PM (+4): a mistura e aditiva ou subtrativa e o shader devolve a cor ja vezes o As inteiro (gsBlend usa fator 1 na origem). O As do
// GS vai ate 255/128 (At * Av / 128 saturado em 255 pela funcao de textura); o fator SRC_ALPHA do GL o limitaria a 1. Na mistura comum
// ((Cs - Cd) As + Cd) o As > 1 pediria fator negativo no destino: passes extras (gsBlendOver1/2).
// Modo (+8 * modo): 1 = sai com alfa As / 2 e 2 = sai branco (os dois passes de GsBlendFactors::doubled); 3, 4 e 5 = os tres passes da
// mistura comum com As > 1 (gsBlendCommon).
inline bool g_premulShader = false;   // o shader do Scene::draw esta ativo (sem ele a cor iria sem multiplicar)
// O burn fica limitado a +-4096: o F satura (0 ou 255) muito antes, e o domo do ceu da AIRPORT tem fogBurnThru 1e7, que deixava o y
// normalizado abaixo do limite do shader (1e-6) e apagava todos os flags (o domo saia sem o modo GS, ~0,13 da cor).
inline void shaderNormalVals(float burn, bool fge, bool gs, bool pm, int mode, float out[3]) {
    out[0] = burn > 4096.f ? 4096.f : (burn < -4096.f ? -4096.f : burn);
    out[1] = 1.f;
    out[2] = (fge ? 1.f : 0.f) + (gs ? 2.f : 0.f) + (pm && g_premulShader ? 4.f : 0.f) + (g_premulShader ? 8.f * float(mode) : 0.f);
}
inline void shaderNormal(float burn, bool fge, bool gs, bool pm = false, int mode = 0) {
    float n[3];
    shaderNormalVals(burn, fge, gs, pm, mode, n);
    rlNormal3f(n[0], n[1], n[2]);
}
// Mistura do GS pelo byte baixo do registrador ALPHA: cor = (A - B) * C + D, campos de 2 bits A, B, D (0 Cs, 1 Cd, 2 zero) e C (0 As,
// 1 Ad, 2 FIX; Ad e FIX nao aparecem nos objetos medidos e ficam como As). Vira Cs * (as * As + ks) + Cd * (ad * As + kd) no GL:
// 0x44 = (Cs - Cd) As + Cd (comum), 0x48 = Cs As + Cd (aditivo), 0x42 = Cd - Cs As, 0x46 = Cd (1 - As) e 0x81 = (Cd - Cs) As (cores do
// Kineticlops). Fator 1 + As nao existe no GL e satura em 1. Com o fator As so na origem, o shader pre-multiplica (gsBlendPremul:
// shaderNormal com pm), e o As passa de 1 como no GS. Com D = 0 e o As no destino (0x81) nao da para pre-multiplicar, e o As do GS
// vai ate ~2 (textura 0xFF com vertice 0x80): doubled = desenhar com As / 2 (shaderNormal modo 1) e dobrar o destino num segundo
// passe da mesma geometria (gsBlendDouble, modo 2). Com D = Cd o As > 1 so zeraria o fator, como o GL ja faz.
struct GsBlendFactors { int src = RL_SRC_ALPHA, dst = RL_ONE_MINUS_SRC_ALPHA, eq = RL_FUNC_ADD; bool premul = false, doubled = false; };
inline GsBlendFactors gsBlendFactors(uint8_t alpha) {
    const int A = alpha & 3, B = (alpha >> 2) & 3, D = (alpha >> 6) & 3;
    struct Term { int f; bool neg; };
    auto term = [](int a, int k) -> Term {   // coeficiente a * As + k (a em -1..1, k em 0..1) -> fator do GL e sinal
        if (a == 0) return {k ? RL_ONE : RL_ZERO, false};
        if (a > 0) return {k ? RL_ONE : RL_SRC_ALPHA, false};   // 1 + As: satura
        return k ? Term{RL_ONE_MINUS_SRC_ALPHA, false} : Term{RL_SRC_ALPHA, true};
    };
    const Term s = term((A == 0) - (B == 0), D == 0), d = term((A == 1) - (B == 1), D == 1);
    GsBlendFactors r;
    r.src = s.f;
    r.dst = d.f;
    if (s.neg && d.neg) r.src = r.dst = RL_ZERO;   // so negativo: o GS satura em 0
    else if (s.neg) r.eq = RL_FUNC_REVERSE_SUBTRACT;
    else if (d.neg) r.eq = RL_FUNC_SUBTRACT;
    r.premul = r.src == RL_SRC_ALPHA && (r.dst == RL_ZERO || r.dst == RL_ONE);
    r.doubled = D == 2 && r.dst == RL_SRC_ALPHA;
    return r;
}
// Segundo passe do doubled: destino * 2 (saida branca vezes DST_COLOR, mais o destino)
inline void gsBlendDouble() {
    rlSetBlendFactors(RL_DST_COLOR, RL_ONE, RL_FUNC_ADD);
    rlSetBlendMode(RL_BLEND_CUSTOM);
}
// Mistura comum com As > 1 (textura 0xFF e vertice acima de 0x40: As = At * Av / 16384 ate 255/128). O GS faz Cs As - Cd (As - 1),
// limitado a 0..255; no GL o fator do destino seria negativo. Tres passes da mesma geometria, cada pixel decidindo no shader:
//  1. a mistura comum, com alfa 0 onde As > 1 (fica o destino);
//  2. (gsBlendOver1, shaderNormal modo 4) so onde As > 1: Cs * 1 - Cd * (1 - 1/As) = resultado / As (descarta os demais);
//  3. (gsBlendOver2, modo 5) destino * As: saida As - 1 vezes DST_COLOR, mais o destino (0 onde As <= 1).
// O framebuffer de 8 bits so arredonda o resultado / As (erro ate As / 2 < 1). Comum = fatores SRC_ALPHA e 1 - SRC_ALPHA, soma.
// O alfa do framebuffer (lido pelo quadro anterior) fica como na mistura comum com As 1: o 2o passe nao o toca e o 3o o poe em 1.
inline bool gsBlendCommon(uint8_t alpha) {
    const GsBlendFactors f = gsBlendFactors(alpha);
    return f.src == RL_SRC_ALPHA && f.dst == RL_ONE_MINUS_SRC_ALPHA && f.eq == RL_FUNC_ADD;
}
inline void gsBlendOver1() {
    rlSetBlendFactorsSeparate(RL_ONE, RL_SRC_ALPHA, RL_ZERO, RL_ONE, RL_FUNC_SUBTRACT, RL_FUNC_ADD);
    rlSetBlendMode(RL_BLEND_CUSTOM_SEPARATE);
}
inline void gsBlendOver2() {
    rlSetBlendFactorsSeparate(RL_DST_COLOR, RL_ONE, RL_ONE, RL_ONE, RL_FUNC_ADD, RL_FUNC_ADD);
    rlSetBlendMode(RL_BLEND_CUSTOM_SEPARATE);
}
inline bool gsBlendPremul(uint8_t alpha) { return gsBlendFactors(alpha).premul; }
inline void gsBlend(uint8_t alpha) {
    const GsBlendFactors f = gsBlendFactors(alpha);
    rlSetBlendFactors(f.premul && g_premulShader ? RL_ONE : f.src, f.dst, f.eq);
    rlSetBlendMode(RL_BLEND_CUSTOM);
}
inline constexpr uint8_t kGsAlphaNormal = 0x44;   // (Cs - Cd) As + Cd
inline constexpr uint8_t kGsAlphaAdd = 0x48;      // Cs As + Cd
inline constexpr uint8_t kGsNoAbe = 0x0A;         // (0 - 0) As + Cs = Cs: PRIM.ABE 0, o alfa so vale para o teste (TEST_1)

// Textura branca com alfa 0x80 (1,0 do GS): o "sem textura" no modo GS (a padrao do raylib, alfa 255, valeria 2,0).
inline unsigned gsWhiteTexture() {
    static unsigned id = 0;
    if (!id) {
        uint32_t px = 0x80FFFFFFu;
        Image img{&px, 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        id = LoadTextureFromImage(img).id;
    }
    return id;
}

// ---------------------------------------------------------------- vertices sem o modo imediato do rlgl
// O drawList emitia cada vertice com rlColor4ub, rlTexCoord2f, rlNormal3f (que normaliza com uma raiz) e rlVertex3f: quatro chamadas
// ao raylib por vertice, ~40 ns cada vertice no Ryzen 3 3200G, metade do tempo da travessia. O VtxStream guarda os mesmos atributos
// num vetor proprio, intercalados, e na descarga sobe tudo de uma vez (glBufferData, buffer novo a cada descarga, sem esperar a GPU)
// e desenha como o rlDrawRenderBatch: o mesmo shader (o que o chamador diz que o rlgl tem ligado), o MVP (modelview x projecao, a conta
// do rlMatrixMultiply), colDiffuse 1 e texture0 na unidade 0, os atributos nas posicoes do lote do rlgl, um glDrawArrays por comando
// (modo e textura). O estado do GL (mistura, mascara de Z, uniform do teste de alfa, CLAMP) vale no momento da descarga, como no rlgl:
// quem usa descarrega antes de muda-lo. Sem as funcoes do GL 3 (glad), ok() e false e o chamador volta ao rlgl.
extern "C" {
extern void (*glad_glGenVertexArrays)(int, unsigned *);
extern void (*glad_glBindVertexArray)(unsigned);
extern void (*glad_glGenBuffers)(int, unsigned *);
extern void (*glad_glBindBuffer)(unsigned, unsigned);
extern void (*glad_glBufferData)(unsigned, ptrdiff_t, const void *, unsigned);
extern void (*glad_glVertexAttribPointer)(unsigned, int, unsigned, unsigned char, int, const void *);
extern void (*glad_glEnableVertexAttribArray)(unsigned);
extern void (*glad_glUseProgram)(unsigned);
extern void (*glad_glUniformMatrix4fv)(int, int, unsigned char, const float *);
extern void (*glad_glUniform4f)(int, float, float, float, float);
extern void (*glad_glUniform1i)(int, int);
extern void (*glad_glActiveTexture)(unsigned);
extern void (*glad_glBindTexture)(unsigned, unsigned);
extern void (*glad_glDrawArrays)(unsigned, int, int);
extern void (*glad_glBufferSubData)(unsigned, ptrdiff_t, ptrdiff_t, const void *);
}
// Threads auxiliares do desenho: parallelFor(n, fn) chama fn(i) para i em [0, n) dividido entre elas e quem chama, e volta quando todas
// terminaram. fn so pode ler o que ninguem escreve durante a chamada (nada de GL nem dos caches da Scene). Quantas: WOTM_DRAWTHREADS
// (0 = nenhuma, tudo em quem chama; o padrao: as threads auxiliares baixam o clock do nucleo do EE e ele perde mais do que a janela ganha).
class JobPool {
public:
    static JobPool &get() { static JobPool *p = new JobPool; return *p; }   // nunca destruido: as threads morrem com o processo
    int workers() const { return int(th_.size()); }
    // Usar as threads agora? Com WOTM_DRAWTHREADS_AB=<s> (medida), alterna a cada s segundos entre com e sem, com uma linha
    // "[native] JobPool: com|sem threads" a cada troca, para comparar as duas metades da mesma luta
    bool active() {
        if (th_.empty()) return false;
        static const double ab = std::getenv("WOTM_DRAWTHREADS_AB") ? std::atof(std::getenv("WOTM_DRAWTHREADS_AB")) : 0.0;
        if (ab <= 0.0) return true;
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const bool on = (long long)(t / ab) % 2 == 0;
        if (on != abLast_) { abLast_ = on; std::fprintf(stderr, "[native] JobPool: %s threads\n", on ? "com" : "sem"); }
        return on;
    }
    template <class F> void parallelFor(int n, F &&fn) {
        if (n <= 0) return;
        if (th_.empty() || n == 1) { for (int i = 0; i < n; ++i) fn(i); return; }
        std::function<void(int)> f = std::forward<F>(fn);
        {
            std::lock_guard<std::mutex> lk(m_);
            fn_ = &f;
            n_ = n;
            next_.store(0, std::memory_order_relaxed);
            busy_ = int(th_.size());
            ++gen_;
        }
        cv_.notify_all();
        run();
        std::unique_lock<std::mutex> lk(m_);
        while (!done_.wait_for(lk, std::chrono::seconds(2), [&] { return busy_ == 0; }))
            std::fprintf(stderr, "[native] JobPool: esperando %d threads ha mais de 2 s (n %d, proximo %d, geracao %llu)\n", busy_, n_,
                         next_.load(), (unsigned long long)gen_);
        fn_ = nullptr;
    }
private:
    JobPool() {
        int k;
        if (const char *e = std::getenv("WOTM_DRAWTHREADS")) k = std::atoi(e);
        else k = 0;
        for (int i = 0; i < k; ++i) { th_.emplace_back([this] { loop(); }); th_.back().detach(); }
        if (k > 0) std::fprintf(stderr, "[native] desenho: %d threads auxiliares (WOTM_DRAWTHREADS)\n", k);
    }
    void run() {
        for (int i; (i = next_.fetch_add(1, std::memory_order_relaxed)) < n_;) (*fn_)(i);
    }
    void loop() {
        uint64_t seen = 0;
        for (;;) {
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [&] { return gen_ != seen; });
                seen = gen_;
                if (quit_) return;
            }
            run();
            std::lock_guard<std::mutex> lk(m_);
            if (--busy_ == 0) done_.notify_one();
        }
    }
    std::vector<std::thread> th_;
    std::mutex m_;
    std::condition_variable cv_, done_;
    std::function<void(int)> *fn_ = nullptr;
    int n_ = 0, busy_ = 0;
    std::atomic<int> next_{0};
    uint64_t gen_ = 0;
    bool quit_ = false;
    bool abLast_ = false;
};

class VtxStream {
public:
    struct V { float x, y, z, u, v, nx, ny, nz; uint8_t c[4]; };
    static_assert(sizeof(V) == 36, "layout intercalado: posicao, UV, normal, cor");

    bool ok() {
        if (!tried_) {
            tried_ = true;
            ok_ = glad_glGenVertexArrays && glad_glBindVertexArray && glad_glGenBuffers && glad_glBindBuffer && glad_glBufferData && glad_glBufferSubData &&
                  glad_glVertexAttribPointer && glad_glEnableVertexAttribArray && glad_glUseProgram && glad_glUniformMatrix4fv &&
                  glad_glUniform4f && glad_glUniform1i && glad_glActiveTexture && glad_glBindTexture && glad_glDrawArrays;
            if (ok_) {
                makeVao(vao_, vbo_);
            } else {
                std::fprintf(stderr, "[native] sem as funcoes do GL 3: vertices pelo modo imediato do rlgl\n");
            }
        }
        return ok_;
    }
    // Shader que o rlgl tem ligado agora (BeginShaderMode do chamador, ou o padrao): o dos comandos seguintes
    void setShader(unsigned id, const int *locs) { shader_ = id; locs_ = locs; }

    // Comeco de um comando (como rlBegin + rlSetTexture): RL_TRIANGLES ou RL_LINES. Guarda o shader e o MVP de agora (a descarga pode
    // vir depois de o chamador mudar as matrizes)
    void begin(int mode, unsigned tex) {
        const int m = curMvp();
        if (!cmds_.empty() && cmds_.back().kind == Cmd::Draw && cmds_.back().count == 0 && cmds_.back().job < 0 && !cmds_.back().res) {
            Cmd &c = cmds_.back();
            c.mode = mode; c.tex = tex; c.mvp = m; c.shader = shader_; c.locs = locs_;
            return;
        }
        cmds_.push_back({Cmd::Draw, mode, tex, int(v_.size()), 0, m, shader_, locs_, -1, -1});
    }
    // Mudanca de estado do GL (mistura, teste de alfa, mascara de Z, CLAMP) na ordem dos vertices: sem nada na fila, roda agora;
    // senao entra na fila e roda na descarga, entre os desenhos de antes e os de depois. Uma subida so por descarga em vez de uma por
    // mudanca de estado (centenas por vista na travessia).
    template <class F> void state(F &&fn) {
        if (empty()) { fn(); return; }
        ops_.emplace_back(std::forward<F>(fn));
        cmds_.push_back({Cmd::State, 0, 0, int(v_.size()), 0, -1, 0, nullptr, int(ops_.size()) - 1, -1});
    }
    // Pedaco adiado: os vertices deste ponto do desenho aberto saem depois, no buffer deferBuf(j) (preenchido por quem pediu, em
    // qualquer thread, antes da descarga); a descarga os poe neste lugar, na ordem. Devolve j.
    int defer() {
        if (cmds_.empty() || cmds_.back().kind != Cmd::Draw || cmds_.back().count != 0 || cmds_.back().job >= 0 || cmds_.back().res) reopen();
        const int j = nDefer_++;
        if (bufs_.size() < size_t(nDefer_)) bufs_.resize(size_t(nDefer_));
        bufs_[size_t(j)].clear();
        cmds_.back().job = j;
        return j;
    }
    std::vector<V> &deferBuf(int j) { return bufs_[size_t(j)]; }

    // Arena residente: vertices que ficam na GPU entre vistas e quadros (geometria que nao mudou; Scene::drawList). arenaAlloc da a
    // posicao de n vertices (-1 = cheia: o chamador zera com arenaReset, que orfana o buffer, fora de uma lista com desenhos da arena
    // na fila); arenaUpload os grava; resident desenha uma faixa no ponto atual da fila, com o comando aberto (textura, shader, MVP) e,
    // com cull, o GL_CULL_FACE ligado (a mesma regra do Scene::backFacing: frente no sentido anti-horario).
    long arenaAlloc(size_t n) {
        if (!ok_) return -1;
        if (!arenaVao_) {
            makeVao(arenaVao_, arenaVbo_);
            glad_glBindBuffer(kArrayBuffer, arenaVbo_);
            glad_glBufferData(kArrayBuffer, ptrdiff_t(kArenaCap * sizeof(V)), nullptr, kStaticDraw);
            glad_glBindBuffer(kArrayBuffer, 0);
        }
        if (arenaUsed_ + n > kArenaCap) return -1;
        const long off = long(arenaUsed_);
        arenaUsed_ += n;
        return off;
    }
    void arenaUpload(long off, const V *data, size_t n) {
        if (!n) return;
        glad_glBindBuffer(kArrayBuffer, arenaVbo_);
        glad_glBufferSubData(kArrayBuffer, ptrdiff_t(size_t(off) * sizeof(V)), ptrdiff_t(n * sizeof(V)), data);
        glad_glBindBuffer(kArrayBuffer, 0);
        arenaUpBytes += double(n * sizeof(V));
    }
    void arenaReset() {
        arenaUsed_ = 0;
        if (!arenaVbo_) return;
        glad_glBindBuffer(kArrayBuffer, arenaVbo_);
        glad_glBufferData(kArrayBuffer, ptrdiff_t(kArenaCap * sizeof(V)), nullptr, kStaticDraw);
        glad_glBindBuffer(kArrayBuffer, 0);
    }
    void resident(long off, int count, bool cull) {
        if (count <= 0) return;
        Cmd d{};
        for (size_t i = cmds_.size(); i-- > 0;)
            if (cmds_[i].kind == Cmd::Draw) { d = cmds_[i]; break; }
        d.kind = Cmd::Draw; d.first = int(off); d.count = count; d.job = -1; d.res = 1; d.cull = cull ? 1 : 0;
        cmds_.push_back(d);
        ++nRes_;
    }
    static constexpr size_t kArenaCap = size_t(1) << 20;   // vertices (36 MB)
    static inline double arenaUpBytes = 0.0;               // WOTM_DRAWSTATS: subida para a arena
    // Comandos de desenho com vertices desde o ultimo zerar (WOTM_DRAWSTATS)
    long draws() const { return draws_; }
    void resetDraws() { draws_ = 0; }
    void color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) { cur_.c[0] = r; cur_.c[1] = g; cur_.c[2] = b; cur_.c[3] = a; }
    void texcoord(float u, float v) { cur_.u = u; cur_.v = v; }
    void normal(float x, float y, float z) {   // como o rlNormal3f (sem transformacao: o modo 3D nao usa rlPushMatrix aqui)
        if (x == lastIn_[0] && y == lastIn_[1] && z == lastIn_[2]) return;   // a mesma de antes (quase sempre: e por item, nao por vertice)
        lastIn_[0] = x; lastIn_[1] = y; lastIn_[2] = z;
        const float length = sqrtf(x * x + y * y + z * z);
        if (length != 0.0f) { const float il = 1.0f / length; x *= il; y *= il; z *= il; }
        cur_.nx = x; cur_.ny = y; cur_.nz = z;
    }
    void vertex(float x, float y, float z) {
        cur_.x = x; cur_.y = y; cur_.z = z;
        if (cmds_.back().kind != Cmd::Draw || cmds_.back().job >= 0 || cmds_.back().res) reopen();   // depois de uma mudanca de estado, de um pedaco adiado ou da arena: o mesmo desenho continua
        v_.push_back(cur_);
        ++cmds_.back().count;
    }
    bool empty() const { return v_.empty() && nDefer_ == 0 && nRes_ == 0; }
    size_t vertices() const { return v_.size(); }

    // Sobe e desenha o que acumulou, com as mudancas de estado da fila no meio; zera a fila
    void flush() {
        const V *up = v_.data();   // o que sobe: v_, ou a juncao com os pedacos adiados (mbuf_)
        size_t upN = v_.size();
        if (nDefer_) {   // os pedacos adiados no lugar deles (os comandos de desenho estao na ordem dos vertices); a copia em paralelo
            copies_.clear();
            size_t total = 0;
            for (Cmd &c : cmds_) {
                if (c.kind != Cmd::Draw) { c.first = int(total); continue; }
                if (c.res) continue;   // faixa da arena: fora da juncao
                const V *src = c.job >= 0 ? bufs_[size_t(c.job)].data() : v_.data() + c.first;
                if (c.job >= 0) c.count = int(bufs_[size_t(c.job)].size());
                c.job = -1;
                c.first = int(total);
                if (c.count) copies_.push_back({src, size_t(c.count), total});
                total += size_t(c.count);
            }
            if (total > mcap_) { mcap_ = total + total / 4; mbuf_.reset(new V[mcap_]); }   // sem zerar (o vector zeraria ~10 MB)
            V *dst = mbuf_.get();
            JobPool::get().parallelFor(int(copies_.size()), [this, dst](int i) {
                const Copy &k = copies_[size_t(i)];
                std::memcpy(dst + k.dst, k.src, k.n * sizeof(V));
            });
            up = dst;
            upN = total;
            nDefer_ = 0;
        }
        if ((!upN && !nRes_) || !ok_) {   // sem vertices: so as mudancas de estado (na ordem)
            for (const Cmd &c : cmds_) if (c.kind == Cmd::State) ops_[size_t(c.op)]();
            v_.clear(); cmds_.clear(); ops_.clear(); mvps_.clear();
            nRes_ = 0;
            return;
        }
        rlDrawRenderBatchActive();   // o que o rlgl tiver na fila sai antes (ordem)
        const auto t0 = std::chrono::steady_clock::now();
        if (upN) {
        // buffer novo vazio (orfao: a GPU ainda pode ler o anterior) e os dados por glBufferSubData. Com o glBufferData ja com os
        // dados (varios MB por descarga), no driver da RX 570 os quadrantes 3 e 4 da tela dividida saiam sem a fase de vez em quando
        // (qualquer leitura de estado do GL, que sincroniza o driver, escondia o defeito)
        glad_glBindBuffer(kArrayBuffer, vbo_);
        glad_glBufferData(kArrayBuffer, ptrdiff_t(upN * sizeof(V)), nullptr, kStreamDraw);
        glad_glBufferSubData(kArrayBuffer, 0, ptrdiff_t(upN * sizeof(V)), up);
        glad_glBindBuffer(kArrayBuffer, 0);
        }
        const auto t1 = std::chrono::steady_clock::now();
        upBytes += double(upN * sizeof(V));
        // estado do GL que os desenhos daqui usam; uma mudanca de estado da fila (funcoes do rlgl) pode trocar o programa e o VAO
        bool bound = false;
        unsigned prog = 0, vao = 0;
        int mvp = -1;
        bool cullOn = false;   // o drawList desliga o GL_CULL_FACE; so as faixas da arena com cull o ligam
        for (const Cmd &c : cmds_) {
            if (c.kind == Cmd::State) {
                ops_[size_t(c.op)]();
                bound = false;
                continue;
            }
            if (!c.count) continue;
            const unsigned p = c.shader ? c.shader : rlGetShaderIdDefault();
            const int *locs = c.locs ? c.locs : rlGetShaderLocsDefault();
            if (!bound || p != prog) {
                glad_glUseProgram(p);
                if (p != prog) mvp = -1;
                prog = p;
                // (matProjection/matView/matModel/matNormal: os shaders do desenho nao os tem)
                glad_glUniform4f(locs[RL_SHADER_LOC_COLOR_DIFFUSE], 1.0f, 1.0f, 1.0f, 1.0f);
                glad_glUniform1i(locs[RL_SHADER_LOC_MAP_DIFFUSE], 0);
                vao = 0;
                glad_glActiveTexture(kTexture0);
                bound = true;
            }
            const unsigned wantVao = c.res ? arenaVao_ : vao_;
            if (vao != wantVao) { glad_glBindVertexArray(wantVao); vao = wantVao; }
            if (bool(c.cull) != cullOn) { cullOn = c.cull != 0; if (cullOn) rlEnableBackfaceCulling(); else rlDisableBackfaceCulling(); }
            if (c.mvp != mvp) {
                glad_glUniformMatrix4fv(locs[RL_SHADER_LOC_MATRIX_MVP], 1, 0, mvps_[size_t(c.mvp)].m);
                mvp = c.mvp;
            }
            glad_glBindTexture(kTexture2D, c.tex);
            glad_glDrawArrays(unsigned(c.mode), c.first, c.count);
            ++draws_;
        }
        if (cullOn) rlDisableBackfaceCulling();
        glad_glBindTexture(kTexture2D, 0);
        glad_glBindVertexArray(0);
        glad_glUseProgram(0);
        nRes_ = 0;
        const auto t2 = std::chrono::steady_clock::now();
        upMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
        drawMs += std::chrono::duration<double, std::milli>(t2 - t1).count();
        v_.clear();
        cmds_.clear();
        ops_.clear();
        mvps_.clear();
    }
    static inline double upMs = 0.0, drawMs = 0.0, upBytes = 0.0;   // WOTM_DRAWSTATS: subida e desenhos das descargas

private:
    struct Cmd {
        enum Kind { Draw, State } kind;
        int mode; unsigned tex; int first, count;
        int mvp;              // indice em mvps_
        unsigned shader; const int *locs;
        int op;               // indice em ops_ (State)
        int job;              // pedaco adiado (defer): indice em bufs_, -1 = vertices em v_
        uint8_t res = 0;      // faixa da arena residente (first na arena)
        uint8_t cull = 0;     // com GL_CULL_FACE (faixa da arena de secao com culling)
    };
    struct Mvp { float m[16]; };
    void reopen() {
        for (size_t i = cmds_.size(); i-- > 0;)
            if (cmds_[i].kind == Cmd::Draw) {
                Cmd d = cmds_[i];
                d.first = int(v_.size()); d.count = 0; d.job = -1; d.res = 0; d.cull = 0;
                cmds_.push_back(d);
                return;
            }
        begin(RL_TRIANGLES, rlGetTextureIdDefault());
    }
    // MVP de agora (rlMatrixMultiply(modelview, projection), na ordem do rlMatrixToFloat), reaproveitando o ultimo se for o mesmo
    int curMvp() {
        const Matrix a = rlGetMatrixModelview(), b = rlGetMatrixProjection();
        const Mvp x{{
            a.m0 * b.m0 + a.m1 * b.m4 + a.m2 * b.m8 + a.m3 * b.m12,     a.m0 * b.m1 + a.m1 * b.m5 + a.m2 * b.m9 + a.m3 * b.m13,
            a.m0 * b.m2 + a.m1 * b.m6 + a.m2 * b.m10 + a.m3 * b.m14,    a.m0 * b.m3 + a.m1 * b.m7 + a.m2 * b.m11 + a.m3 * b.m15,
            a.m4 * b.m0 + a.m5 * b.m4 + a.m6 * b.m8 + a.m7 * b.m12,     a.m4 * b.m1 + a.m5 * b.m5 + a.m6 * b.m9 + a.m7 * b.m13,
            a.m4 * b.m2 + a.m5 * b.m6 + a.m6 * b.m10 + a.m7 * b.m14,    a.m4 * b.m3 + a.m5 * b.m7 + a.m6 * b.m11 + a.m7 * b.m15,
            a.m8 * b.m0 + a.m9 * b.m4 + a.m10 * b.m8 + a.m11 * b.m12,   a.m8 * b.m1 + a.m9 * b.m5 + a.m10 * b.m9 + a.m11 * b.m13,
            a.m8 * b.m2 + a.m9 * b.m6 + a.m10 * b.m10 + a.m11 * b.m14,  a.m8 * b.m3 + a.m9 * b.m7 + a.m10 * b.m11 + a.m11 * b.m15,
            a.m12 * b.m0 + a.m13 * b.m4 + a.m14 * b.m8 + a.m15 * b.m12, a.m12 * b.m1 + a.m13 * b.m5 + a.m14 * b.m9 + a.m15 * b.m13,
            a.m12 * b.m2 + a.m13 * b.m6 + a.m14 * b.m10 + a.m15 * b.m14, a.m12 * b.m3 + a.m13 * b.m7 + a.m14 * b.m11 + a.m15 * b.m15}};
        if (!mvps_.empty() && std::memcmp(mvps_.back().m, x.m, sizeof x.m) == 0) return int(mvps_.size()) - 1;
        mvps_.push_back(x);
        return int(mvps_.size()) - 1;
    }
    void makeVao(unsigned &vao, unsigned &vbo) {
        glad_glGenVertexArrays(1, &vao);
        glad_glGenBuffers(1, &vbo);
        glad_glBindVertexArray(vao);
        glad_glBindBuffer(kArrayBuffer, vbo);
        // as posicoes fixas que o rlLoadShaderProgram amarra nos shaders (glBindAttribLocation): o shader padrao nao tem
        // normal (posicao -1) e a do alphaShader_ e a 2
        const int loc[4] = {RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION, RL_DEFAULT_SHADER_ATTRIB_LOCATION_TEXCOORD,
                            RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL, RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR};
        const int size[4] = {3, 2, 3, 4};
        const size_t off[4] = {offsetof(V, x), offsetof(V, u), offsetof(V, nx), offsetof(V, c)};
        for (int a = 0; a < 4; ++a) {
            if (loc[a] < 0) continue;
            glad_glVertexAttribPointer(unsigned(loc[a]), size[a], a == 3 ? kUByte : kFloat, a == 3 ? 1 : 0, int(sizeof(V)),
                                       reinterpret_cast<const void *>(off[a]));
            glad_glEnableVertexAttribArray(unsigned(loc[a]));
        }
        glad_glBindVertexArray(0);
        glad_glBindBuffer(kArrayBuffer, 0);
    }
    static constexpr unsigned kArrayBuffer = 0x8892, kStreamDraw = 0x88E0, kStaticDraw = 0x88E8, kFloat = 0x1406, kUByte = 0x1401, kTexture2D = 0x0DE1,
                              kTexture0 = 0x84C0;
    std::vector<V> v_;
    std::unique_ptr<V[]> mbuf_;   // juncao de v_ com os pedacos adiados (flush)
    size_t mcap_ = 0;
    std::vector<std::vector<V>> bufs_;   // pedacos adiados (defer), reaproveitados entre descargas
    struct Copy { const V *src; size_t n, dst; };
    std::vector<Copy> copies_;
    int nDefer_ = 0, nRes_ = 0;
    unsigned arenaVao_ = 0, arenaVbo_ = 0;
    size_t arenaUsed_ = 0;
    std::vector<Cmd> cmds_;
    std::vector<std::function<void()>> ops_;
    std::vector<Mvp> mvps_;
    long draws_ = 0;
    V cur_{};
    float lastIn_[3] = {0.f, 0.f, 0.f};   // normal de entrada que deu a cur_ (a inicial 0,0,0 da 0,0,0, como no rlgl)
    unsigned vao_ = 0, vbo_ = 0, shader_ = 0;
    const int *locs_ = nullptr;
    bool tried_ = false, ok_ = false;
};

// Tempo de GPU do redesenho (WOTM_DRAWSTATS): consulta GL_TIME_ELAPSED em volta dele, lida 3 redesenhos depois (ja pronta)
extern "C" {
extern void (*glad_glGenQueries)(int, unsigned *);
extern void (*glad_glBeginQuery)(unsigned, unsigned);
extern void (*glad_glEndQuery)(unsigned);
extern void (*glad_glGetQueryObjectui64v)(unsigned, unsigned, unsigned long long *);
}
struct GpuTimer {
    static GpuTimer &get() { static GpuTimer g; return g; }
    double ms = 0.0;   // soma desde o ultimo zerar
    long n = 0;
    void begin() {
        if (!glad_glGenQueries || !glad_glBeginQuery || !glad_glEndQuery || !glad_glGetQueryObjectui64v) return;
        if (!q_[0]) glad_glGenQueries(4, q_);
        if (used_[k_]) {
            unsigned long long ns = 0;
            glad_glGetQueryObjectui64v(q_[k_], 0x8866 /* GL_QUERY_RESULT */, &ns);
            ms += double(ns) * 1e-6;
            ++n;
        }
        glad_glBeginQuery(0x88BF /* GL_TIME_ELAPSED */, q_[k_]);
        open_ = true;
    }
    void end() {
        if (!open_) return;
        glad_glEndQuery(0x88BF);
        used_[k_] = true;
        k_ = (k_ + 1) & 3;
        open_ = false;
    }
    struct Scope {
        bool on;
        Scope() : on(enabled()) { if (on) get().begin(); }
        static bool enabled() { static const bool e = std::getenv("WOTM_DRAWSTATS") != nullptr; return e; }
        ~Scope() { if (on) get().end(); }
    };
private:
    unsigned q_[4] = {};
    bool used_[4] = {};
    int k_ = 0;
    bool open_ = false;
};

// EndMode3D dentro de um BeginTextureMode. O do raylib pergunta ao GL o framebuffer ativo (rlGetActiveFramebuffer, um glGetIntegerv)
// para aplicar a escala da tela no framebuffer 0; no driver da NVIDIA, com a otimizacao em thread, a leitura espera o driver esvaziar
// a fila: ~1,5 ms por vista (6 ms por redesenho de 4 vistas no Ryzen 5 5500). Numa render texture a escala nao vale.
inline void endMode3DInTexture() {
    rlDrawRenderBatchActive();
    rlMatrixMode(RL_PROJECTION);
    rlPopMatrix();
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    rlDisableDepthTest();
}

// Tela do GS (640 x 224, exibida em 4:3) dentro do quadro nativo de fbw x fbh: a faixa central com a altura toda. Em widescreen
// (Hor+: o FOV vertical do jogo fica, a largura cresce) as sobras sao as laterais; o que o jogo poe em pixels do GS (texto, lens
// flare, quadro anterior) fica nessa faixa.
struct GsScreenRect { float x0, w, h; };
// Na tela dividida (ViewScope de uma vista de 320 x 224) a faixa e a da vista: 2:3 no quadro da metade.
inline GsScreenRect gsScreenRect(float fbw, float fbh) {
    const float w = std::min(fbw, fbh * curView().aspect());
    return {(fbw - w) * 0.5f, w, fbh};
}

namespace fxaddr {
constexpr uint32_t fx = wotm::sym::fx;           // _ParticleType fx[100]
constexpr uint32_t stride = 0x2EE0;           // sizeof(_ParticleType)
constexpr uint32_t count = 100;
constexpr uint32_t fxList = wotm::sym::fxList;       // 12 bytes por sistema: next em +8
constexpr uint32_t fxActiveTop = wotm::sym::fxActiveTop;  // primeiro ativo (-1 = nenhum)
constexpr uint32_t textureInfo = wotm::sym::TextureInfo;  // TextureInfoType[128]: numTextures s16, texIndex u16, du u16, dv u16
constexpr uint32_t texId = wotm::sym::texId;        // u16[128]: indice no texInfo global
constexpr uint32_t tex0 = wotm::sym::TEX0;         // u64[128]: TEX0 de cada textura de particula
constexpr uint32_t colors = 0x00256C10;       // RGBA (0x80 = 1,0), indexado por vis.colorIndex
// D_0078F5A0 (particle.bss): float[5][16], a matriz da vista no particleDraw(v) anterior. O particleDraw copia a da vista v para o
// pacote de cada sistema (fx + 0x2EC0) e no fim grava a atual (D_00256B90) nela
constexpr uint32_t viewPrevMat = 0x0078F5A0;
}

// Offsets dentro de _ParticleType (include/hieri_types.h)
namespace fxoff {
constexpr uint32_t uvI = 0x2B40;          // u8[256]
constexpr uint32_t emitVec = 0x2CA0;      // emit.ParticleToEmitVec
constexpr uint32_t fxId = 0x2DDC;         // emit.fxId (parece ser o indice do preset de particleInitFx; nao confirmado em dump)
constexpr uint32_t infoAttribs = 0x2E30;  // info.attribs (0x400 = pacote montado neste quadro)
constexpr uint32_t minSize = 0x2E38;      // info.minSize (pedestres: largura do carimbo)
constexpr uint32_t maxSize = 0x2E3C;      // info.maxSize (pedestres: altura do carimbo)
constexpr uint32_t expansionRate = 0x2E44; // info.expansionRate
constexpr uint32_t lifetime = 0x2E4A;     // info.lifetime (s16, campos)
constexpr uint32_t useThisColor = 0x2E58; // info.useThisColor: vetor (R, G, B 0..255; w = escala do alfa) com o bit 0x200 do attribs
constexpr uint32_t alphaFadeIn = 0x2E60;  // info.alphaFadeIn (dustTrail: fracao da vida)
constexpr uint32_t alphaFadeOut = 0x2E64;
constexpr uint32_t minAlpha = 0x2E68;
constexpr uint32_t maxAlpha = 0x2E6C;
constexpr uint32_t preAgeFactor = 0x2D88; // emit.info.preAgeFactor
constexpr uint32_t mapsPerFrame = 0x2E74; // info.mapsPerFrame (4.12)
constexpr uint32_t colorsPerFrame = 0x2E78;
constexpr uint32_t type = 0x2E90;         // vis.attribs
constexpr uint32_t texMask = 0x2E94;      // vis.texMask (numero de quadros do texSprite)
constexpr uint32_t texInfoIndex = 0x2E98;
constexpr uint32_t numColors = 0x2E9C;
constexpr uint32_t colorIndex = 0x2EA0;
constexpr uint32_t priority = 0x2EA4;
constexpr uint32_t alphaContext = 0x2EA8;
constexpr uint32_t pos = 0x2EB0;          // FVECTOR*
constexpr uint32_t vel = 0x2EB4;          // FVECTOR*
constexpr uint32_t age = 0x2EB8;          // int16*
constexpr uint32_t numParticles = 0x2ED0;
constexpr uint32_t maxParticles = 0x2ED4;
}

enum FxType : uint32_t {
    FX_LINE = 1, FX_SWEEPTRAIL = 2, FX_SPRITE = 3, FX_TEXSPRITE = 4, FX_STRIPE = 5, FX_STRITE = 6, FX_ARC = 7, FX_SPRINE = 8, FX_KITE = 9,
    FX_CHAIN = 12, FX_FRAG = 13, FX_PEDS = 14, FX_DUSTTRAIL = 15, FX_WAKE = 16, FX_ARC2 = 17,
};

inline const char *fxTypeName(uint32_t t) {
    switch (t) {
    case FX_LINE: return "line";
    case FX_SWEEPTRAIL: return "sweepTrail";
    case FX_SPRITE: return "sprite";
    case FX_TEXSPRITE: return "texSprite";
    case FX_STRIPE: return "stripe";
    case FX_STRITE: return "strite";
    case FX_SPRINE: return "sprine";
    case FX_KITE: return "kite";
    case FX_FRAG: return "frag";
    case FX_DUSTTRAIL: return "dustTrail";
    case FX_WAKE: return "wake";
    case FX_ARC: return "arco";
    case FX_ARC2: return "arco17";
    case FX_CHAIN: return "corrente";
    case FX_PEDS: return "pedestre";
    default: return "?";
    }
}

// Um sistema de particulas pronto para desenhar
struct FxSystem {
    uint32_t base = 0;     // endereco do _ParticleType
    uint32_t index = 0;    // indice em fx[]
    uint32_t type = 0;
    int n = 0;             // numParticles
    float key = 0;         // ordem de desenho (maior = mais longe = antes)
};

// Eixos da camera para montar os quadrados: right/up no plano da tela, back = -frente, profundidade(p) = depth . (p, 1)
// ox/oy: coordenadas x e y de vista (ox . (p, 1)), na mesma base de right/up.
// Neblina (Particles::setFog): W(p) = fogW . (p, 1) (linha 3 do worldToScreenMat, com o olho), F = fogFA + fogFB / W; fogFB 0 = sem neblina.
struct FxView { float right[3], up[3], back[3], depth[4], ox[4], oy[4]; float fogW[4] = {0, 0, 0, 0}, fogFA = 0.f, fogFB = 0.f; };

class Particles {
public:
    // texture(texId, tex0) -> id de textura do raylib (0 = sem textura); fornecido pela Scene (paletas do .RTX)
    using TexFn = std::function<unsigned(uint32_t texId, uint64_t tex0)>;

    std::vector<FxSystem> systems;
    float wide = 1.f;   // largura da tela / largura 4:3 (Scene::wide): teste de visao com os planos laterais da tela larga
    bool fovCull = true;     // collect recorta pela vista do jogo (inViewFov); false = camera livre pausada
    bool frameGate = true;   // collect sem particulas em quadro sem particleDraw (flareSlot); false = recoleta da camera livre pausada
    // Sprites que o jogo pausado nao tem (modo foto, wotm_photo.inc: fogo do Agamo simulado com o wotm_flame.hpp quando o emissor
    // estava fora da tela na pausa). Mesmo desenho de um sistema sprite (tipo 3): meio-lado size / 2 com teto de 1/4 da profundidade,
    // cor RGBA com 0x80 = 1, UV espelhada por sel (os 2 bits baixos de pos.w), textura do texInfo, mistura do alphaContext.
    struct SynthSprite { float p[3]; float size; uint32_t rgba; uint8_t sel; };
    struct SynthSystem { uint32_t texInfo = 0, alphaContext = 0; std::vector<SynthSprite> sprites; };
    std::vector<SynthSystem> synth;

    // Textura composta de poeira/fumaca: TEX0 com TBP 0x3F60 (VRAM 0xFD800, CT32 64x64) e sem pixels na RAM (texId 0xFFFF; o jogo a
    // compoe na VRAM, Scene::dustTexture). O TexFn a recebe com texId = kDustTexId. Quatro celulas de 32x32; o sprite (VisualInfo
    // 105) e o dustTrail (1, 2 e 118) sobem a tabela de UV das celulas; o sprite escolhe a entrada pelos bits baixos de pos.w e o
    // dustTrail pelo byte 2 da palavra de idade (dustTrailVu).
    static constexpr uint32_t kDustTexId = 0xFFFF;
    static bool isDustTex(uint64_t tex0) { return (tex0 & 0x3FFF) == 0x3F60; }
    static uint64_t systemTex0(const Ram &ram, uint32_t b, uint32_t frame = 0) {
        const uint32_t k = (ram.u16(fxaddr::textureInfo + 8 * (ram.u32(b + fxoff::texInfoIndex) & 127) + 2) + frame) & 127;
        return uint64_t(ram.u32(fxaddr::tex0 + 8 * k)) | (uint64_t(ram.u32(fxaddr::tex0 + 8 * k + 4)) << 32);
    }

    // Teste de visao do particleDraw: viewInFOV(0, &FOVsphere (+0x2C80), &FOVextent (+0x2C90)), a caixa (centro, meia-extensao) contra
    // os 6 planos de worldCtx[0] (fovNormsTP em +0xF0, fovSigns em +0x170; src/common/view.cpp). Falso = o retail nao desenha o sistema.
    // wide > 1 (widescreen: largura da tela / largura 4:3): os planos laterais (planes[0][1] e [0][2] do viewSetFov, os de +/- w/2)
    // saem da worldToScreenMat com a borda em |X - 2048| = 320 * wide pixels do GS; perto, cima, baixo e longe continuam os do jogo.
    static bool inViewFov(const Ram &ram, uint32_t b, float wide = 1.f) {
        float c[3], e[3];
        for (int k = 0; k < 3; ++k) { c[k] = ram.f32(b + 0x2C80 + 4 * k); e[k] = ram.f32(b + 0x2C90 + 4 * k); }
        return inViewVolume(ram, c, e, 0.f, wide);
    }

    // Caixa (centro c, meia-extensao e) mais esfera de raio r em volta dela, em mundo, contra os 6 planos de worldCtx[0]: o viewInFOV
    // (r = 0) e o programa 0x0 do VU0 que o hierTraverseAsm roda em cada GROUP e OBJECT (e = 0; vf30 = fovNorms . (centro + eo) e
    // vf29 = fovNorms1 . (centro + eo) contra -raio, o plano 1.z com o farClip da memoria 0 do VU0, como o fovNormsTP[1][3][2]).
    // Planos unitarios; o termo sum |n_k| e_k e o fovSigns do jogo.
    static bool inViewVolume(const Ram &ram, const float c[3], const float e[3], float r, float wide = 1.f) {
        const uint32_t w = viewCtxAddr();   // worldCtx[vista]
        const bool ws = wide > 1.001f;
        for (uint32_t i = 0; i < 2; ++i)
            for (uint32_t j = 0; j < 3; ++j) {
                if (ws && i == 0 && j > 0) continue;
                const uint32_t n = w + 0xF0 + 64 * i + 16 * j;
                float d = r;
                for (uint32_t k = 0; k < 3; ++k) d += ram.f32(n + 4 * k) * c[k] + std::fabs(ram.f32(n + 4 * k)) * e[k];
                if (d < ram.f32(w + 0xF0 + 64 * i + 48 + 4 * j)) return false;
            }
        if (!ws) return true;
        // X = (M0 . d) / (M3 . d), d = (p - olho, 1). Dentro da borda direita: X - 2048 <= h  <=>  (h M3 - (M0 - 2048 M3)) . d >= 0;
        // esquerda com o sinal trocado. Para a caixa, o canto mais favoravel: n . (c - olho) + n3 + sum |n_k| e_k >= 0.
        const uint32_t kW2S = viewW2S();
        float M0[4], M3[4], eye[3];
        for (int k = 0; k < 4; ++k) { M0[k] = ram.f32(kW2S + 4 * k); M3[k] = ram.f32(kW2S + 48 + 4 * k); }
        for (int k = 0; k < 3; ++k) eye[k] = -ram.f32(w + 0x20 + 4 * k);   // worldCtx[0].eo = -olho
        const float h = 0.5f * curView().w * wide, xc = curView().cx;   // meia-largura e centro da vista no GS
        for (float sgn : {1.f, -1.f}) {
            float n[4];
            for (int k = 0; k < 4; ++k) n[k] = h * M3[k] - sgn * (M0[k] - xc * M3[k]);
            float d = n[3] + r * std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            for (int k = 0; k < 3; ++k) d += n[k] * (c[k] - eye[k]) + std::fabs(n[k]) * e[k];
            if (!(d >= 0.f)) return false;
        }
        return true;
    }

    // Focos da worldToScreenMat M (pixels do GS por unidade de vista a W = 1): parte das linhas 0 e 1 perpendicular a linha 3 (frente).
    // 384 e 180,48 na luta.
    static bool screenFoci(const float M[4][4], float &fx, float &fy) {
        float n3 = 0.f, d03 = 0.f, d13 = 0.f;
        for (int k = 0; k < 3; ++k) { n3 += M[3][k] * M[3][k]; d03 += M[0][k] * M[3][k]; d13 += M[1][k] * M[3][k]; }
        if (!(n3 > 1e-6f)) return false;
        fx = 0.f; fy = 0.f;
        for (int k = 0; k < 3; ++k) {
            const float a0 = M[0][k] - d03 / n3 * M[3][k], a1 = M[1][k] - d13 / n3 * M[3][k];
            fx += a0 * a0; fy += a1 * a1;
        }
        fx = std::sqrt(fx); fy = std::sqrt(fy);
        return fx > 1e-6f && fy > 1e-6f;
    }

    // prevViewMat do pacote (fx + 0x2EC0: colunas x/y/z/translacao em mundo) para a cauda das linhas e dos strites. O particleDraw so a
    // grava (a matriz da vista, logo antes do pacote) nos sistemas que passam no teste de visao 4:3; o nativo em widescreen desenha
    // tambem os que ficam so na faixa lateral, e neles a matriz e de um quadro antigo ou lixo do linePacketInit ((63, 0, 0, 0) num
    // sistema novo): a cauda virava um risco de milhares de pixels (riscos amarelos na 3-MILE, foto 2026-10-18_232542/001 e dump de
    // 2026-10-19). Fora da visao 4:3 a cauda usa a vista atual (sem o borrao da camera) e o vento fica de fora (o linePacketBuild
    // tambem nao rodou). Falso se a matriz lida for toda zero.
    // Tela dividida: o pacote e um so por sistema e o particleDraw da vista 1 grava a matriz dela por cima da da vista 0 (as linhas do
    // tiro do Robo-47 riscavam a vista da esquerda, foto 2026-10-19_130401). Ai a matriz sai de fxaddr::viewPrevMat[vista], que a
    // captura (shadowRam) preenche com a do quadro anterior de cada vista.
    static bool prevViewMat(const Ram &ram, uint32_t b, const float M[4][4], const float eye[3], float P[16], float wind[3]) {
        const uint32_t pm = ram.u32(b + 0x2EC0);
        if (!inViewFov(ram, b)) {
            for (int r = 0; r < 4; ++r) {
                P[r] = M[r][0]; P[4 + r] = M[r][1]; P[8 + r] = M[r][2];
                P[12 + r] = M[r][3] - (M[r][0] * eye[0] + M[r][1] * eye[1] + M[r][2] * eye[2]);
            }
            if (wind) wind[0] = wind[1] = wind[2] = 0.f;
            return true;
        }
        if (!ram.ok(pm - 16, 80)) return false;
        bool any = false;
        if (curView().view > 0 || !curView().full()) {   // a faixa de baixo dos 3 jogadores (vista 2) tem 640 de largura
            const uint32_t vm = fxaddr::viewPrevMat + 64u * uint32_t(std::clamp(curView().view, 0, 3));
            for (int k = 0; k < 16; ++k) { P[k] = ram.f32(vm + 4 * uint32_t(k)); any = any || P[k] != 0.f; }
        }
        if (!any)
            for (int k = 0; k < 16; ++k) { P[k] = ram.f32(pm + 4 * uint32_t(k)); any = any || P[k] != 0.f; }
        if (wind) for (int a = 0; a < 3; ++a) wind[a] = ram.f32(pm - 16 + 4 * uint32_t(a));
        return any;
    }

    // Strite como o microprograma do VU1 (entrada 0xA88; conferido pela checagem, erro < 0,1 px). Em pixels do GS:
    //   H = M (pos) com a worldToScreenMat atual; T = M' (pos - vel) com a prevViewMat (fx + 0x2EC0: 4 qwords no pacote, colunas
    //   x/y/z/translacao em coordenadas de mundo): a cauda sai onde estava na tela do quadro anterior (borrao de movimento).
    //   A = 3T - 2H (ponta), B = 3H - 2T (base), divididos pelo proprio w. Direcao m = (B - A) normalizada no espaco dividido pelos focos
    //   (fx, fy da matriz: 384 e 180,48 na luta); ponta recua 0,5 pos.w, base avanca 0,25 pos.w e a meia-largura e 0,433 pos.w
    //   (0,866 / 2), em unidades de vista: pixel = foco * deslocamento * pos.w / w.
    // Os tres pontos voltam ao mundo pela matriz atual, cada um na sua profundidade. c[0] = c[3] = ponta, c[1]/c[2] = base.
    // alphaK: o microprograma multiplica o alfa da cor por K / (len + K) (0x0DC0-0x0E30: ESQRT, DIV Q = vf24w / vf10x, MULq.w), com
    // len = |B/w - A/w| na tela (Y escalado por fx/fy, vf24z) e K = 4 vf27x pos.w / B.w = 2 fx pos.w / B.w (vf27x = fx / 2; medido contra
    // o VU1 na foto 2026-10-17_200206): rastro longo fica mais fraco.
    static bool striteTriangle(const Ram &ram, uint32_t b, const float p[4], const float v[3], float c[4][3], float *alphaK = nullptr) {
        const uint32_t kW2S = viewW2S(), kEo = viewCtxAddr() + 0x20;   // worldToScreenMat[vista], worldCtx[vista].eo (= -olho)
        float M[4][4], eye[3];
        for (int r = 0; r < 4; ++r)
            for (int k = 0; k < 4; ++k) M[r][k] = ram.f32(kW2S + 16 * r + 4 * k);
        for (int k = 0; k < 3; ++k) eye[k] = -ram.f32(kEo + 4 * k);
        float P[16];
        if (!prevViewMat(ram, b, M, eye, P, nullptr)) return false;
        float H[4], T[4];
        const float d[3] = {p[0] - eye[0], p[1] - eye[1], p[2] - eye[2]}, t[3] = {p[0] - v[0], p[1] - v[1], p[2] - v[2]};
        for (int r = 0; r < 4; ++r) {
            H[r] = M[r][0] * d[0] + M[r][1] * d[1] + M[r][2] * d[2] + M[r][3];
            T[r] = P[12 + r] + P[r] * t[0] + P[4 + r] * t[1] + P[8 + r] * t[2];
        }
        float A[4], B[4];
        for (int r = 0; r < 4; ++r) { A[r] = 3.f * T[r] - 2.f * H[r]; B[r] = 3.f * H[r] - 2.f * T[r]; }
        if (!(A[3] > 1e-3f) || !(B[3] > 1e-3f)) return false;
        float fx, fy;
        if (!screenFoci(M, fx, fy)) return false;
        const float ax = A[0] / A[3], ay = A[1] / A[3], bx = B[0] / B[3], by = B[1] / B[3];
        float mx = (bx - ax) / fx, my = (by - ay) / fy;
        const float ml = std::sqrt(mx * mx + my * my);
        if (!(ml > 1e-9f)) return false;
        if (alphaK) {
            const float len = ml * fx, k = 2.f * fx * p[3] / B[3];
            *alphaK = len + k > 0.f ? k / (len + k) : 1.f;
        }
        mx /= ml; my /= ml;
        const float w = p[3], ka = 0.5f * w / A[3], kb = w / B[3];
        const float px[3] = {ax - ka * mx * fx, bx + kb * (0.25f * mx * fx - 0.4330127f * my * fx), bx + kb * (0.25f * mx * fx + 0.4330127f * my * fx)};
        const float py[3] = {ay - ka * my * fy, by + kb * (0.25f * my * fy + 0.4330127f * mx * fy), by + kb * (0.25f * my * fy - 0.4330127f * mx * fy)};
        const float pw[3] = {A[3], B[3], B[3]};
        // volta ao mundo: linhas 0, 1 e 3 de M aplicadas a (q - olho) dao (x w, y w, w)
        const float m00 = M[0][0], m01 = M[0][1], m02 = M[0][2], m10 = M[1][0], m11 = M[1][1], m12 = M[1][2], m30 = M[3][0], m31 = M[3][1], m32 = M[3][2];
        const float det = m00 * (m11 * m32 - m12 * m31) - m01 * (m10 * m32 - m12 * m30) + m02 * (m10 * m31 - m11 * m30);
        if (!(std::fabs(det) > 1e-12f)) return false;
        for (int k = 0; k < 3; ++k) {
            const float r0 = px[k] * pw[k] - M[0][3], r1 = py[k] * pw[k] - M[1][3], r3 = pw[k] - M[3][3];
            const float q0 = (r0 * (m11 * m32 - m12 * m31) - m01 * (r1 * m32 - m12 * r3) + m02 * (r1 * m31 - m11 * r3)) / det;
            const float q1 = (m00 * (r1 * m32 - m12 * r3) - r0 * (m10 * m32 - m12 * m30) + m02 * (m10 * r3 - r1 * m30)) / det;
            const float q2 = (m00 * (m11 * r3 - r1 * m31) - m01 * (m10 * r3 - r1 * m30) + r0 * (m10 * m31 - m11 * m30)) / det;
            const int dst = k == 0 ? 0 : k;
            c[dst][0] = eye[0] + q0; c[dst][1] = eye[1] + q1; c[dst][2] = eye[2] + q2;
        }
        for (int a = 0; a < 3; ++a) c[3][a] = c[0][a];
        return true;
    }

    // Linha (FX_LINE) como o microprograma do VU1 (MPG 0x0000; linePacketInit/Build em asm/common/partdraw.s):
    //  - ponta = pos com a camera atual; cauda = M' (pos - vel - vento), M' = prevViewMat (fx + 0x2EC0, 4 qwords do pacote, colunas
    //    x/y/z/translacao) e vento = q537 (logo antes dela: field.wind.xyz + Gusts * field.wind.w, gravado pelo linePacketBuild). A cauda
    //    volta ao mundo pela matriz atual, na sua profundidade (como o striteTriangle): fica onde a cauda estava na tela do quadro anterior;
    //  - cor = lerp da tabela em vel.w * (numColors - 1) / lifetime, + 0,5, truncada (nao pela idade);
    //  - alfa = alfa da cor * pos.w * (fogFA + fogFB / W) / 255, teto 255: a neblina entra no alfa (o PRIM 0x49 nao tem FGE). Some com
    //    alfa < 1. A cauda leva o RGB com alfa 0.
    // Conferido na checagem (ultrav_corda2: cauda a 0,05 px do VU1 no ajuste offline).
    static bool lineVu(const Ram &ram, uint32_t b, const FxView &view, int i, float head[3], float tail[3], uint32_t &col) {
        const uint32_t kW2S = viewW2S(), kEo = viewCtxAddr() + 0x20;
        const uint32_t pos = ram.u32(b + fxoff::pos), vel = ram.u32(b + fxoff::vel);
        if (!ram.ok(pos + 16 * uint32_t(i), 16) || !ram.ok(vel + 16 * uint32_t(i), 16)) return false;
        float p[4], v[4], wind[3], P[16], M[4][4], eye[3];
        for (int a = 0; a < 4; ++a) { p[a] = ram.f32(pos + 16 * uint32_t(i) + 4 * uint32_t(a)); v[a] = ram.f32(vel + 16 * uint32_t(i) + 4 * uint32_t(a)); }
        if (!std::isfinite(p[0]) || !std::isfinite(v[0])) return false;
        for (int r = 0; r < 4; ++r)
            for (int k = 0; k < 4; ++k) M[r][k] = ram.f32(kW2S + 16 * r + 4 * k);
        for (int k = 0; k < 3; ++k) eye[k] = -ram.f32(kEo + 4 * k);
        if (!prevViewMat(ram, b, M, eye, P, wind)) return false;
        // cor: q520 + indice (ate 16 entradas; as que passam de numColors tem alfa 0)
        const uint32_t nc = ram.u32(b + fxoff::numColors), tab = fxaddr::colors + 4 * ram.u32(b + fxoff::colorIndex);
        const int life = int(ram.s16(b + fxoff::lifetime));
        const float rate = life != 0 && nc > 0 ? float(int(nc) - 1) / float(life) : 0.f;
        const float cp = v[3] * rate;
        if (!std::isfinite(cp)) return false;
        int k0 = int(cp);   // FTOI0: trunca
        const float fr = cp - float(k0);
        const int nEnt = int(std::min<uint32_t>(nc + 1, 16));
        auto entry = [&](int k, int sh) -> float {
            if (k < 0 || k >= nEnt) return 0.f;
            if (sh == 24 && k >= int(nc)) return 0.f;
            return float((ram.u32(tab + 4 * uint32_t(k)) >> sh) & 255);
        };
        float c[4];
        for (int a = 0; a < 4; ++a) c[a] = entry(k0, 8 * a) * (1.f - fr) + entry(k0 + 1, 8 * a) * fr + 0.5f;
        // profundidade da ponta (W) para a neblina no alfa
        float W = 0.f;
        for (int k = 0; k < 3; ++k) W += M[3][k] * (p[k] - eye[k]);
        W += M[3][3];
        if (!(W > 1e-3f)) return false;
        const float A = std::min(c[3] * p[3] * (view.fogFA + view.fogFB / W) / 255.f, 255.f);
        if (!(A >= 1.f)) return false;
        auto ub = [](float x) { return uint32_t(std::clamp(x, 0.f, 255.f)); };
        col = ub(c[0]) | ub(c[1]) << 8 | ub(c[2]) << 16 | ub(A) << 24;
        // cauda: T = P (pos - vel - vento), de volta ao mundo pela matriz atual na profundidade T.w
        const float t[3] = {p[0] - v[0] - wind[0], p[1] - v[1] - wind[1], p[2] - v[2] - wind[2]};
        float T[4];
        for (int r = 0; r < 4; ++r) T[r] = P[12 + r] + P[r] * t[0] + P[4 + r] * t[1] + P[8 + r] * t[2];
        if (!(T[3] > 1e-3f)) return false;
        const float m00 = M[0][0], m01 = M[0][1], m02 = M[0][2], m10 = M[1][0], m11 = M[1][1], m12 = M[1][2], m30 = M[3][0], m31 = M[3][1], m32 = M[3][2];
        const float det = m00 * (m11 * m32 - m12 * m31) - m01 * (m10 * m32 - m12 * m30) + m02 * (m10 * m31 - m11 * m30);
        if (!(std::fabs(det) > 1e-12f)) return false;
        const float r0 = T[0] - M[0][3], r1 = T[1] - M[1][3], r3 = T[3] - M[3][3];   // (x w, y w, w) = T.x, T.y, T.w
        const float q0 = (r0 * (m11 * m32 - m12 * m31) - m01 * (r1 * m32 - m12 * r3) + m02 * (r1 * m31 - m11 * r3)) / det;
        const float q1 = (m00 * (r1 * m32 - m12 * r3) - r0 * (m10 * m32 - m12 * m30) + m02 * (m10 * r3 - r1 * m30)) / det;
        const float q2 = (m00 * (m11 * r3 - r1 * m31) - m01 * (m10 * r3 - r1 * m30) + r0 * (m10 * m31 - m11 * m30)) / det;
        tail[0] = eye[0] + q0; tail[1] = eye[1] + q1; tail[2] = eye[2] + q2;
        for (int a = 0; a < 3; ++a) head[a] = p[a];
        return true;
    }

    // Reune os sistemas desenhaveis (mesmos testes de particleDraw). A ordem sai no draw (camera atual).
    void collect(const Ram &ram) {
        systems.clear();
        uint32_t i = ram.u32(fxaddr::fxActiveTop);
        for (uint32_t guard = 0; i < fxaddr::count && guard < fxaddr::count; ++guard) {
            const uint32_t b = fxaddr::fx + i * fxaddr::stride;
            const uint32_t next = ram.u32(fxaddr::fxList + 12 * i + 8);
            FxSystem s;
            s.base = b;
            s.index = i;
            s.type = ram.u32(b + fxoff::type);
            s.n = int32_t(ram.u32(b + fxoff::numParticles));
            const int maxN = int32_t(ram.u32(b + fxoff::maxParticles));
            const bool built = (ram.u32(b + fxoff::infoAttribs) & 0x400) != 0;
            const int minN = s.type == FX_SPRINE ? 2 : 1;
            // O particleDraw so monta o pacote (0x400) dos sistemas na vista. Na recoleta da camera livre pausada (fovCull falso) os
            // sprites fora da vista da pausa entram tambem (fogo do Agamo atras da camera): eles so leem pos, idade e cores, nao o
            // pacote; os tipos com cabecalho no pacote (wake, arco, sprine...) continuam so com ele montado
            const bool plain = s.type == FX_SPRITE || s.type == FX_TEXSPRITE;
            if ((built || (!fovCull && plain)) && s.type != 11 && s.type != FX_FRAG && s.n >= minN && s.n <= std::max(maxN, 1) && s.n <= 1024 && ram.ok(ram.u32(b + fxoff::pos), 16 * uint32_t(s.n)) &&
                (!fovCull || inViewFov(ram, b, wide)))
                systems.push_back(s);
            i = next;
        }
        {   // ao vivo, quadro sem particleDraw (menu, carga): nada de particulas, como no PS2, onde o pacote so sai do particleDraw.
            // fx[] continua com os sistemas da ultima luta (fx 81/86/90/93 no menu depois da campanha: quadrados verdes, foto 007);
            // o captureFlares (logo depois de cada particleDraw) marca o quadro como o lens flare
            FlareSlot &s = flareSlot();
            std::lock_guard<std::mutex> g(s.m);
            // a recoleta da camera livre pausada (wotm_live.inc) roda depois do consumeFlares do quadro: la a RAM parada e a do
            // quadro com particleDraw, e o corte apagava todas as particulas de fx[] (pedestres, fogo do Agamo) so na pausa
            if (frameGate && s.used && !s.fresh) systems.clear();
        }
        static const bool list = std::getenv("WOTM_FXLIST") != nullptr;
        if (list) dump(ram);
        collectFlares(ram);
    }

    // Lens flare (tipo 11): fora do particleDraw comum. Dois fx[] tipo 11 (lensFlarePacket[vista], 0x6F8908; particleInitAfter) cujo
    // pos[] (fx + 0x2EB0) e a lista HierLensFlareList[74] (48 bytes) de lensFlareList[vista] (0x6F8900), com lensFlareIndex[vista]
    // (0x6F88F8) entradas. A lista e alimentada pelo hierLightNode (luzes com hasLensFlare: pos no mundo, flags = as da luz, intensity,
    // cor, dotProd/beamWidth do feixe) e pelo particleAddLensFlareToList (sistemas com vis.lightColor & 0x1000000: flags 3, Zfudge,
    // intensity = lightIntensity^2, cor = lightColor / 255).
    //  - particleLensFlareTestZbuffer (no fim do particleDraw, depois do VU1 desenhar a cena): projeta cada entrada, descarta as fora da
    //    tela ou com W < 1 (compacta a lista), grava pos = (X, Y, Z do GS), distance = W (100 com lightAtInfinity, bit 7 das flags) e le
    //    do GS o Z 4x4 em (X - 2, Y) (sceGsStoreImage PSMZ32; origem X - 0x6C2, Y - 0x790).
    //  - particleLensFlareRender: amostra visivel = Z <= limiar. Luz no infinito: as 16, limiar fptoui(1,03125 Z) + 1; local: as 4
    //    centrais, limiar fptoui(a + b / (W - max(Zfudge; 1,5))) (a, b = viewGetZBuffParams). intensity *= visiveis / amostras; pos +=
    //    centroide das visiveis - (2 ou 1); com 0 < ID < 0x7FF (so luzes no infinito: particleLensFlareAddToDeBounceList), intensity =
    //    media com a do quadro anterior (D_0078FF00); abaixo de 1/256 zera o byte das flags. Depois o lensFlarePacketBuild grava q44 =
    //    (centro X, centro Y, 18, n) e o UNPACK de 3n qwords da lista em q45.
    // No runtime o GS emulado nao tem a cena (sem VU1); aqui o teste e refeito contra a profundidade do nativo (drawFlares), e o centroide
    // e a fracao do GS emulado sao desfeitos supondo visibilidade total la (ver o WOTM_FLARELOG para conferir).
    // O microprograma 0x2B20 (build/vu1/particle.txt), por entrada (q45 + 3i):
    //  - so com flags & 1; Q = 1/distance; brilho = I Q Q (o segundo MULq le o Q antigo: o DIV 1/beamWidth ainda nao terminou; com o Q
    //    novo as particulas, sem beamWidth, sairiam com lixo), tamanho = sqrt(I) Q. Com flags & 8 (direcional): os dois vezes
    //    f / beamWidth, f = max(dotProd, 0) + beamWidth - 1 (|dotProd| com flags & 0x10, bidirecional); some se brilho f <= 0.
    //  - 18 SPRITEs (tabelas no pacote, q8-q25: TEX0, t, meio-lado com o seletor de UV nos 2 bits baixos; q26-q43: R, G, B, A). Sprite k
    //    no ponto C + t_k (S - C) da linha do centro da tela C ate a fonte S; cantos +/- (h, h/2) px do GS. k = 0: h = meio-lado tamanho A,
    //    cor min(255, RGB rgb). k > 0: h = meio-lado, cor min(32, RGB rgb A brilho). Alfa 0x80, ALPHA 0x48 (aditivo), UV de (0,5; 0,5) no
    //    canto + ate uvMax no canto - (uvMax = 31,5, 63,5 ou 127,5 texels pelo seletor: 0x1F8, 0x3F8, 0x7F8 em 1/16). Depois de um sprite
    //    com as tres cores < 1, a entrada sai dos seguintes.
    // Conferido pela checagem VU1 x nativo (entrada lida da memoria do VU1 no MSCAL do 0x2B20): cantos, cor e UV de todos os sprites.
    struct FlareSprite { uint64_t tex0 = 0; uint32_t texId = 0; float t = 0.f, half = 0.f, uvMax = 0.f, rgba[4] = {0, 0, 0, 0}; };
    struct Flare { float x = 0, y = 0, z = 0, w = 0, zfudge = 0, dot = 0, bw = 0, rgb[3] = {0, 0, 0}, intensity = 0; uint32_t flags = 0, id = 0; };
    struct FlareFrame {
        float cx = 2048.f, cy = 2048.f, vw = 640.f, vh = 224.f, zScale = 0.f, zOff = 0.f;
        int nspr = 0;
        FlareSprite spr[18];
        std::vector<Flare> e;
    };
    FlareFrame flares;
    const AsyncDepth *depthAsync = nullptr;   // drawFlares: profundidade assincrona da vista (tela dividida); nullptr = glReadPixels direto

    // A lista so esta pronta (projetada, com a fracao visivel) entre o fim do particleDraw e o hier do quadro seguinte. A apresentacao
    // roda em outra thread e quase sempre a pega ja refeita pelo hier (em coordenadas de mundo, sem os flares de particula, que o
    // particleAddLensFlareToList so acrescenta dentro do particleDraw). O runtime chama captureFlares na thread do jogo logo depois do
    // particleDraw; o collect usa essa copia. Sem captura (viewer sobre dump) le a RAM e so desenha se a lista estiver projetada.
    // A copia vale so para o quadro do particleDraw que a fez (fresh; o wotmCaptureFrame a consome com consumeFlares). Quadro sem
    // particleDraw (menu, tela de carregamento) sai sem flares, como no PS2, onde o pacote so existe se o particleDraw o montou; antes
    // a lista da ultima luta voltava na tela LOAD seguinte (luz verde/vermelha no centro na entrada da partida).
    // Uma copia por vista (lensFlareIndex/lensFlarePacket[vista & 1]; na tela dividida o particleDraw(1) refaz a lista da vista 1, e
    // com 3 e 4 jogadores o das vistas 2 e 3 refaz a das 0 e 1). O captureFlares roda dentro do particleDraw da vista (viewCurView),
    // antes da vista seguinte reaproveitar a lista; a coleta pega a da vista corrente (curView).
    struct FlareSlot { std::mutex m; FlareFrame f; bool used = false, fresh = false; };
    static FlareSlot &flareSlot(int view = curView().view) { static FlareSlot s[4]; return s[std::clamp(view, 0, 3)]; }
    static void captureFlares(const Ram &ram) {
        const int v = int(std::min<uint32_t>(ram.u32(wotm::sym::viewCurView), 3u));
        FlareFrame f;
        {
            ViewScope vs(readViewCtx(ram, v));
            readFlares(ram, f);
        }
        FlareSlot &s = flareSlot(v);
        std::lock_guard<std::mutex> g(s.m);
        s.f = std::move(f);
        s.used = true;
        s.fresh = true;
    }
    static void consumeFlares() {
        for (int v = 0; v < 4; ++v) {
            FlareSlot &s = flareSlot(v);
            std::lock_guard<std::mutex> g(s.m);
            s.fresh = false;
        }
    }
    void resetFlareDebounce() { flarePrev_.clear(); }

    // Copia de sombra do que o draw le da RAM (docs/investigacoes/2026-10-17/09_magmo_impacto.md). O instantaneo guarda so a contagem
    // de cada sistema; posicoes, idades e cabecalhos de pacote eram lidos da RAM viva na apresentacao, e se o particleMove do quadro
    // seguinte ja tinha matado particulas, pos[n] era o fecho do pacote (0, 0, 0, w enorme): um quadrado gigante na origem do mapa.
    // Chamada so na thread do EE (wotmCaptureFrame), com os sistemas recem-coletados. Devolve um buffer do tamanho da RAM com as
    // faixas copiadas (o resto fica com lixo de usos anteriores, que o draw nunca le); nullptr = desligado (WOTM_FXLIVE=1).
    // prevMats (tela dividida): float[4][16], a matriz de cada vista (0 a 3) no quadro anterior, gravada em fxaddr::viewPrevMat
    // (prevViewMat); sem ela a faixa fica zerada e vale o pacote
    static std::shared_ptr<const uint8_t> shadowRam(const Ram &ram, const std::vector<FxSystem> &systems, const float *prevMats = nullptr) {
        static const bool live = std::getenv("WOTM_FXLIVE") != nullptr;   // diagnostico: le a RAM viva, como antes
        if (live || !ram.p || systems.empty()) return nullptr;
        // pool: buffer livre = so o pool o segura (o instantaneo publicado, o da apresentacao e o da Scene seguram os outros)
        static std::vector<std::shared_ptr<uint8_t>> pool;
        std::shared_ptr<uint8_t> buf;
        for (auto &p : pool)
            if (p.use_count() == 1) { buf = p; break; }
        if (!buf) {
            if (pool.size() >= 6) return nullptr;
            pool.emplace_back(new uint8_t[ram.size], std::default_delete<uint8_t[]>());
            buf = pool.back();
        }
        uint8_t *dst = buf.get();
        copyRanges(dst, ram, systems);
        if (prevMats) std::memcpy(dst + fxaddr::viewPrevMat, prevMats, 4 * 64);
        else std::memset(dst + fxaddr::viewPrevMat, 0, 4 * 64);
        return std::shared_ptr<const uint8_t>(buf, buf.get());
    }

    // As faixas que o draw le (shadowRam), copiadas de src (a RAM viva ou outra copia de sombra) para dst, do mesmo tamanho
    static void copyRanges(uint8_t *dst, const Ram &ram, const std::vector<FxSystem> &systems) {
        auto copy = [&](uint32_t a, uint32_t n) {
            if (a < 0x1000 || a >= ram.size) return;
            n = uint32_t(std::min<uint64_t>(n, ram.size - a));
            std::memcpy(dst + a, ram.p + a, n);
        };
        copy(fxaddr::fx, fxaddr::count * fxaddr::stride);   // pos/vel/idade e pacotes (cabecalhos do wake, arco, sprine) ficam dentro de fx[i]
        copy(fxaddr::textureInfo, 128 * 8);
        copy(fxaddr::texId, 128 * 2);
        copy(fxaddr::tex0, 128 * 8);
        copy(addr::worldToScreenMat, 5 * 0x40);
        copy(addr::worldCtx, 5 * 0x1F0);
        copy(addr::vu1FogParams, 16);
        copy(fxaddr::viewPrevMat, 4 * 64);
        copy(wotm::sym::Gusts, 16);   // rajadas do vento (stepBack)
        const uint32_t lo = fxaddr::fx, hi = fxaddr::fx + fxaddr::count * fxaddr::stride;
        auto outside = [&](uint32_t a, uint32_t n) { return a < lo || uint64_t(a) + n > hi; };
        for (const FxSystem &s : systems) {
            const uint32_t b = s.base, n = uint32_t(std::max(s.n, 0)) + 1;
            copy(fxaddr::colors + 4 * ram.u32(b + fxoff::colorIndex), 4 * std::min<uint32_t>(ram.u32(b + fxoff::numColors), 256));
            // info.useThisColor (bit 0x200: rastro de poeira, frag) aponta para fora de fx[] (trailColoring da fase, 0x5B4F60 na VEGAS);
            // sem a copia o stripe lia lixo/zeros ao vivo (cor preta, alfa 0) e o rastro sumia, embora o replay (RAM inteira) o desenhasse
            if (ram.u32(b + fxoff::infoAttribs) & 0x200) copy(ram.u32(b + fxoff::useThisColor), 16);
            // ponteiros que apontem para fora de fx[] (nao visto ate hoje): a faixa que o draw le em volta deles
            const uint32_t pos = ram.u32(b + fxoff::pos), vel = ram.u32(b + fxoff::vel), age = ram.u32(b + fxoff::age), pm = ram.u32(b + 0x2EC0);
            if (outside(pos - 0x400, 0x400 + 16 * n)) copy(pos - 0x400, 0x400 + 16 * n);
            if (outside(vel, 16 * n)) copy(vel, 16 * n);
            if (outside(age, 4 * n)) copy(age, 4 * n);
            if (pm && outside(pm, 64)) copy(pm, 64);
        }
    }

    // Um passo do particleMove ao contrario (interpolacao de quadros, Scene::showBlend). O particleMove faz, com f = fields do passo,
    // pos_B = pos_A + v f + vento f, e so depois soma a gravidade a velocidade (v_B = v + (0, 0, gravidade f)); o arrasto ja esta em
    // v_B. Entao pos_A = pos_B - f (v_B + vento - (0, 0, gravidade f)). Por sistema: vento por field (field.wind; com o bit 0x400 do
    // field.attribs, xyz + Gusts xyz * wind.w, senao so o z), gravidade (field.gravity; poeira, wake e pipa andam pelo dustMove e pelo
    // wakeAndKiteMove, sem ela), quantas andaram (as primeiras n - newParticles: as do fim nasceram no passo e ficam onde estao) e z
    // preso ao nivel da agua (wake e pipa com o bit 0x8000000 do emit.info.attribs). Falso = o sistema nao anda assim: arcos, sprine,
    // corrente e rastro (pontos postos pelo jogo) e field.attribs 0x1000 (parado). O emissor que se move (fogo preso ao monstro) nao
    // entra: a particula recua so pela propria velocidade.
    struct StepBack { float wind[3] = {0, 0, 0}; float grav = 0.f; int moved = 0; bool keepZ = false; };
    static bool stepBack(const Ram &ram, uint32_t b, int n, StepBack &o) {
        constexpr uint32_t kField = 0x2DE0, kEmitAttribs = 0x2D80, kNewParticles = 0x2EDE;
        const uint32_t type = ram.u32(b + fxoff::type), fa = ram.u32(b + kField);
        if (type == FX_SWEEPTRAIL || type == FX_ARC || type == FX_ARC2 || type == FX_SPRINE || type == FX_CHAIN || (fa & 0x1000)) return false;
        const float ww = ram.f32(b + kField + 0x1C);
        o.wind[0] = o.wind[1] = 0.f;
        o.wind[2] = ram.f32(b + kField + 0x18);
        if (fa & 0x400)
            for (int k = 0; k < 3; ++k) o.wind[k] = ram.f32(b + kField + 0x10 + 4 * uint32_t(k)) + ram.f32(wotm::sym::Gusts + 4 * uint32_t(k)) * ww;
        const bool own = type == FX_DUSTTRAIL || type == FX_WAKE || type == FX_KITE;
        o.grav = own ? 0.f : ram.f32(b + kField + 4);
        o.keepZ = (type == FX_WAKE || type == FX_KITE) && (ram.u32(b + kEmitAttribs) & 0x8000000);
        o.moved = std::clamp(n - int(ram.s16(b + kNewParticles)), 0, n);
        for (float v : {o.wind[0], o.wind[1], o.wind[2], o.grav})
            if (!std::isfinite(v)) return false;
        return true;
    }
    // Deslocamento do passo (pos_B - pos_A) da particula de velocidade v
    static void stepOffset(const StepBack &o, const float v[3], float f, float out[3]) {
        for (int k = 0; k < 3; ++k) out[k] = f * (v[k] + o.wind[k]);
        out[2] -= o.grav * f * f;
        if (o.keepZ) out[2] = 0.f;
    }

    // Copia de sombra no instante entre A e B: as faixas de B (copyRanges) com as posicoes recuadas por back (1 - alfa) passos de
    // `fields`. So xyz: pos.w (tamanho, com os bits baixos que escolhem a UV; idade na poeira e no wake) e a idade ficam as de B.
    // Recuo maior que 400 unidades (velocidade lixo) fica em B.
    static void blendRam(uint8_t *dst, const Ram &B, const std::vector<FxSystem> &systems, float back, float fields) {
        copyRanges(dst, B, systems);
        const Ram out{dst, B.size};
        for (const FxSystem &s : systems) {
            StepBack o;
            if (!stepBack(out, s.base, s.n, o) || !o.moved) continue;
            const uint32_t pos = out.u32(s.base + fxoff::pos), vel = out.u32(s.base + fxoff::vel);
            if (!out.ok(pos, 16 * uint32_t(o.moved)) || !out.ok(vel, 16 * uint32_t(o.moved))) continue;
            for (int i = 0; i < o.moved; ++i) {
                float v[3], d[3];
                std::memcpy(v, dst + vel + 16 * uint32_t(i), 12);
                stepOffset(o, v, fields, d);
                if (!std::isfinite(d[0] + d[1] + d[2]) || d[0] * d[0] + d[1] * d[1] + d[2] * d[2] > 400.f * 400.f) continue;
                float *p = reinterpret_cast<float *>(dst + pos + 16 * uint32_t(i));
                for (int k = 0; k < 3; ++k) p[k] -= back * d[k];
            }
        }
    }

    void collectFlares(const Ram &ram) {
        {
            FlareSlot &s = flareSlot();
            std::lock_guard<std::mutex> g(s.m);
            if (s.used) {   // ao vivo: sem particleDraw neste quadro, sem flares
                if (s.fresh) flares = s.f;
                else { flares.e.clear(); flares.nspr = 0; }
                logFlares();
                return;
            }
        }
        readFlares(ram, flares);
        logFlares();
    }
    void logFlares() const {
        static const bool log = std::getenv("WOTM_FLARELOG") != nullptr;
        if (log)
            for (const Flare &fl : flares.e)
                std::fprintf(stderr, "[flare] n %zu centro %.0f %.0f | %.1f %.1f Z %.0f W %.1f id %u flags %02x dot %.3f bw %.3f rgb %.3f %.3f %.3f I %.4g\n",
                             flares.e.size(), flares.cx, flares.cy, fl.x, fl.y, fl.z, fl.w, fl.id, fl.flags & 0xFF, fl.dot, fl.bw, fl.rgb[0], fl.rgb[1],
                             fl.rgb[2], fl.intensity);
    }

    static void readFlares(const Ram &ram, FlareFrame &f) {
        const uint32_t view = uint32_t(std::clamp(curView().view, 0, 3)), v = view & 1u;   // lensFlareIndex/lensFlarePacket tem 2 entradas
        const uint32_t kIndex = wotm::sym::lensFlareIndex + 4 * v, kPacket = wotm::sym::lensFlarePacket + 4 * v, kViewDef = viewInfoAddr() + 4,
                       kScreenMats = wotm::sym::viewScreenMats + 0x40 * view;
        f.e.clear();
        f.nspr = 0;
        const uint32_t p = ram.u32(kPacket);
        if (!ram.ok(p, fxaddr::stride)) return;
        // q44 (centro X, centro Y, 18, n) e o que o lensFlarePacketBuild grava a cada quadro: aqui vem da origem (viewGetCenter da vista
        // 0, lensFlareIndex[0]), sem depender do pacote do quadro; as tabelas dos sprites (q8-q43, 18 em q44 + 8) sao do lensFlarePacketInit.
        const uint32_t pos = ram.u32(p + fxoff::pos), q44 = pos - 0x18, q8 = q44 - 36 * 16;
        const int n = int32_t(ram.u32(kIndex));
        const uint32_t def = ram.u32(kViewDef) & 0x0FFFFFFF;
        // n = 0: so a tabela dos sprites e a moldura (o modo foto refaz a lista com a camera livre)
        if (n < 0 || n > 74 || !ram.ok(q8, 37 * 16 + 48 * uint32_t(n)) || !ram.ok(def, 0x20)) return;
        f.cx = float(int32_t(ram.u32(def + 0x18)));
        f.cy = float(int32_t(ram.u32(def + 0x1C)));
        f.nspr = std::min<int>(18, std::max<int>(0, int32_t(ram.u32(q44 + 8))));
        if (f.nspr == 0) f.nspr = 18;   // lensFlarePacketBuild ainda nao rodou (fase sem flare ate aqui): a tabela do Init ja esta la
        if (ram.u16(def + 4) && ram.u16(def + 6)) { f.vw = ram.u16(def + 4); f.vh = ram.u16(def + 6); }
        f.zScale = ram.f32(kScreenMats + 0x28);   // viewScreenMats[0][2][2] e [2][3]
        f.zOff = ram.f32(kScreenMats + 0x2C);
        const uint32_t gif = (p + 0xC + 15) & ~15u;   // lensFlarePacketInit: FLUSHA, UNPACK de 4 qwords em 0x110 (tag A+D, ALPHA, TEX0, tag SPRITE)
        for (int k = 0; k < f.nspr; ++k) {
            FlareSprite &s = f.spr[k];
            const uint32_t a = q8 + 16 * uint32_t(k), c = a + 18 * 16;
            s.tex0 = uint64_t(ram.u32(a)) | (uint64_t(ram.u32(a + 4)) << 32);
            s.t = ram.f32(a + 8);
            s.half = ram.f32(a + 12);
            // o UNPACK leva os 4 qwords a partir de gif (= VU1 0x110, conferido no instantaneo); com um qword a mais o uvMax saia o
            // do registro seguinte (127,5 numa textura de 64): a textura so cobria metade do sprite e o brilho caia fora do olho
            s.uvMax = float(ram.u32(gif + 16 * (ram.u32(a + 12) & 3) + 12) & 0xFFFF) / 16.f;
            for (int j = 0; j < 4; ++j) s.rgba[j] = ram.f32(c + 4 * uint32_t(j));
            s.texId = ram.u16(p + 0x2C40 + 2 * uint32_t(k));
        }
        const float x0 = f.cx - 0.5f * f.vw, x1 = f.cx + 0.5f * f.vw, y0 = f.cy - 0.5f * f.vh, y1 = f.cy + 0.5f * f.vh;
        for (int i = 0; i < n; ++i) {
            const uint32_t e = pos + 48 * uint32_t(i);
            Flare fl;
            fl.x = ram.f32(e); fl.y = ram.f32(e + 4); fl.z = ram.f32(e + 8); fl.id = ram.u32(e + 0xC);
            fl.flags = ram.u32(e + 0x10); fl.zfudge = ram.f32(e + 0x10); fl.w = ram.f32(e + 0x14);
            fl.dot = ram.f32(e + 0x18); fl.bw = ram.f32(e + 0x1C);
            for (int j = 0; j < 3; ++j) fl.rgb[j] = ram.f32(e + 0x20 + 4 * uint32_t(j));
            fl.intensity = ram.f32(e + 0x2C);
            // fora da tela = lista ainda em coordenadas de mundo (o hier ja a refez e o teste de Z nao rodou): nada a desenhar
            if (!(fl.x >= x0 - 3.f && fl.x <= x1 + 3.f && fl.y >= y0 - 3.f && fl.y <= y1 + 3.f)) { f.e.clear(); break; }
            f.e.push_back(fl);
        }
    }

    // Entrada como o VU1 a recebe: posicao na tela do GS e intensity depois do teste de Z
    struct FlareIn { float x, y, intensity; const Flare *fl; };
    // Sprite do GS (px do GS): canto + (xa, ya) com UV (u0, v0), canto - (xb, yb) com (u1, v1); k = linha da tabela, entry = indice em `in`
    struct FlareQuad { int k; size_t entry; float xa, ya, xb, yb, u0, v0, u1, v1; uint8_t rgb[3]; };

    // Microprograma 0x2B20 sobre a lista (sem o teste de Z): o desenho e a checagem VU1 x nativo usam o mesmo
    template <class E> static void flareSprites(const FlareFrame &f, const std::vector<FlareIn> &in, E &&emit) {
        struct V { float bright, size; bool on; };
        std::vector<V> v(in.size());
        for (size_t i = 0; i < in.size(); ++i) {   // laco por entrada
            const Flare &fl = *in[i].fl;
            const float I = in[i].intensity;
            v[i].on = (fl.flags & 1) && fl.w != 0.f;
            if (!v[i].on) continue;
            const float q = 1.f / fl.w;
            v[i].bright = I * q * q;
            v[i].size = std::sqrt(std::max(I, 0.f)) * q;
            if (fl.flags & 8) {
                const float k = ((fl.flags & 0x10) ? std::fabs(fl.dot) : std::max(fl.dot, 0.f)) + fl.bw - 1.f;
                if (!(v[i].bright * k > 0.f) || fl.bw == 0.f) { v[i].on = false; continue; }
                v[i].bright = v[i].bright * k / fl.bw;
                v[i].size = v[i].size * k / fl.bw;
            }
        }
        auto ftoi4 = [](float a) { return std::trunc(a * 16.f) / 16.f; };
        for (int k = 0; k < f.nspr; ++k) {   // laco por sprite da tabela, entradas por dentro
            const FlareSprite &s = f.spr[k];
            const float tw = float(1u << ((s.tex0 >> 26) & 15)), th = float(1u << ((s.tex0 >> 30) & 15));
            for (size_t i = 0; i < in.size(); ++i) {
                if (!v[i].on) continue;
                const Flare &fl = *in[i].fl;
                const float px = f.cx + s.t * (in[i].x - f.cx), py = f.cy + s.t * (in[i].y - f.cy);
                float h, col[3];
                for (int j = 0; j < 3; ++j) col[j] = s.rgba[j] * fl.rgb[j];
                if (k == 0) h = s.half * v[i].size * s.rgba[3];
                else {
                    h = s.half;
                    for (float &c : col) c *= s.rgba[3] * v[i].bright;
                }
                if (col[0] < 1.f && col[1] < 1.f && col[2] < 1.f) v[i].on = false;   // os sprites seguintes nao desenham mais esta
                const float cap = k == 0 ? 255.f : 32.f;
                FlareQuad q{k, i, ftoi4(px + h), ftoi4(py + 0.5f * h), ftoi4(px - h), ftoi4(py - 0.5f * h),
                            0.5f / tw, 0.5f / th, s.uvMax / tw, s.uvMax / th, {0, 0, 0}};
                for (int j = 0; j < 3; ++j) q.rgb[j] = uint8_t(std::clamp(col[j], 0.f, cap));
                emit(q);
            }
        }
    }

    // Desenha os flares do ultimo quadro do jogo em espaco de tela (dentro de BeginMode3D, depois das particulas; o pacote do lens flare
    // sai depois delas no particleDraw). Le a profundidade ja desenhada para o teste de oclusao (o particleLensFlareRender).
    void drawFlares(const TexFn &texture) {
        static const bool off = std::getenv("WOTM_NOFX") != nullptr || std::getenv("WOTM_NOFLARE") != nullptr;
        static const bool log0 = std::getenv("WOTM_FLARELOG") != nullptr;
        if (log0 && !flares.e.empty()) std::fprintf(stderr, "[flare] drawFlares: %zu entradas, %d sprites\n", flares.e.size(), flares.nspr);
        if (off || flares.e.empty() || flares.nspr == 0) return;
        const FlareFrame &f = flares;
        rlDrawRenderBatchActive();
        if (g_beforeDepthRead) g_beforeDepthRead();
        const int fbw = rlGetFramebufferWidth(), fbh = rlGetFramebufferHeight();
        if (fbw <= 0 || fbh <= 0) return;
        const GsScreenRect gr = gsScreenRect(float(fbw), float(fbh));   // px do GS -> px do nativo (faixa 4:3 central em widescreen)
        const float sx = gr.w / f.vw, sy = gr.h / f.vh, x0 = gr.x0;
        const double zn = rlGetCullDistanceNear(), zf = rlGetCullDistanceFar();
        // Z do GS (24 bits) que o retail leria no pixel (gx, gy) da tela do GS: a + b / W da profundidade nativa; vazio (fundo) = 0
        // tela dividida: profundidade do redesenho anterior, sem esperar a GPU (AsyncDepth); senao a leitura direta
        const AsyncDepth *ad = depthAsync && depthAsync->valid && depthAsync->cw == fbw && depthAsync->ch == fbh ? depthAsync : nullptr;
        auto gsZ = [&](int gx, int gy) -> uint32_t {
            const int px = std::clamp(int(x0 + (float(gx) + 0.5f) * sx), 0, fbw - 1), py = std::clamp(int((float(gy) + 0.5f) * sy), 0, fbh - 1);
            float d = 1.f;
            if (ad)
                d = ad->cpu[size_t(fbh - 1 - py) * size_t(fbw) + size_t(px)];
            else
                glReadPixels(px, fbh - 1 - py, 1, 1, 0x1902 /* GL_DEPTH_COMPONENT */, 0x1406 /* GL_FLOAT */, &d);
            if (d >= 1.f) return 0u;
            const double ndc = 2.0 * d - 1.0, W = 2.0 * zn * zf / (zf + zn - ndc * (zf - zn));
            const double z = double(f.zScale) + double(f.zOff) / std::max(W, 1e-3);
            return uint32_t(std::clamp(z, 0.0, 16777215.0));
        };
        auto fptoui = [](float v) -> uint32_t { return v <= 0.f ? 0u : v >= 4294967040.f ? 0xFFFFFFFFu : uint32_t(v); };

        std::vector<FlareIn> vis;
        static const bool log = std::getenv("WOTM_FLARELOG") != nullptr;
        for (const Flare &fl : f.e) {
            if (!(fl.flags & 0xFF)) continue;
            const bool inf = (fl.flags & 0x80) != 0;
            // pixel do canto do bloco 4x4: ((int) X - 0x6C2, (int) Y - 0x790), ou seja colunas X-2..X+1 e linhas Y..Y+3 (o retail supoe
            // centro 2048 e tela 640 x 224; TRXPOS conferido no instantaneo). Com o -2 tambem na vertical, as amostras de luz local caiam
            // duas linhas acima (sobrancelha do Congar, borda da lente do Robo-47) e o flare dos olhos sumia.
            const int bx = int(fptoui(fl.x)) - int(f.cx - 0.5f * f.vw) - 2, by = int(fptoui(fl.y)) - int(f.cy - 0.5f * f.vh);
            uint32_t thr;
            if (inf) thr = fptoui(fl.z * 1.03125f) + 1;
            else {
                const float fud = fl.zfudge > 1.5f ? fl.zfudge : 1.5f, d = fl.w - fud;
                thr = fptoui((f.zScale * d + f.zOff) / d);
            }
            const int lo = inf ? 0 : 1, hi = inf ? 4 : 3;
            int cnt = 0, sumC = 0, sumR = 0;
            uint32_t zMax = 0;   // amostra mais perto (WOTM_FLARELOG)
            for (int r = lo; r < hi; ++r)
                for (int c = lo; c < hi; ++c) {
                    const uint32_t z = gsZ(bx + c, by + r);
                    zMax = std::max(zMax, z);
                    if (z <= thr) { ++cnt; sumC += c; sumR += r; }
                }
            float frac = float(cnt) / float((hi - lo) * (hi - lo));
            if (fl.id && fl.id < 0x7FF) {   // debounce por ID (D_0078FF00): media com a fracao do quadro anterior
                float &prev = flarePrev_[fl.id];
                const float raw = frac;
                frac = 0.5f * (prev + raw);
                prev = raw;
            }
            const float I = fl.intensity * frac;
            if (log)
                std::fprintf(stderr, "[flare] desenho %.1f %.1f visiveis %d limiar %u I %.4g | W da luz %.2f, W do mais perto %.2f | flags %02x dot %.3f bw %.3f\n", fl.x, fl.y, cnt, thr,
                             I, fl.w, zMax && float(zMax) != f.zScale ? f.zOff / (float(zMax) - f.zScale) : 0.f, fl.flags & 0xFF, fl.dot, fl.bw);
            if (cnt == 0 || I < 1.f / 256.f) continue;
            // o retail ja somou o centroide do GS emulado (visibilidade total: 1,5 - 2 ou 1,5 - 1); troca pelo daqui
            const float shift = 1.5f - (inf ? 2.f : 1.f);
            vis.push_back({fl.x - shift + (float(sumC) / float(cnt) - (inf ? 2.f : 1.f)), fl.y - shift + (float(sumR) / float(cnt) - (inf ? 2.f : 1.f)), I, &fl});
        }
        std::vector<FlareQuad> quads;
        float big = 0.f;   // maior meio-lado do sprite 0 (diagnostico WOTM_FLARESHOT)
        flareSprites(f, vis, [&](const FlareQuad &q) {
            quads.push_back(q);
            if (q.k == 0) big = std::max(big, std::fabs(q.xa - q.xb) * 0.5f);
        });
        if (quads.empty()) return;

        // Tela em pixels: a projecao vai para a pilha; a modelview e guardada a parte (o rlPushMatrix em modelview liga a matriz de
        // transformacao do rlgl, que so desliga quando a pilha esvazia em modelview; empilhar as duas e desempilhar a projecao primeiro
        // trocava as matrizes e o HUD desenhado depois sumia)
        const Matrix mv3d = rlGetMatrixModelview();
        rlMatrixMode(RL_PROJECTION);
        rlPushMatrix();
        rlLoadIdentity();
        rlOrtho(0.0, double(fbw), double(fbh), 0.0, -1.0, 1.0);
        rlMatrixMode(RL_MODELVIEW);
        rlLoadIdentity();
        rlDisableDepthTest();
        rlDisableBackfaceCulling();
        gsBlend(kGsAlphaAdd);
        shaderNormal(0.f, false, true, true);
        const float ox = f.cx - 0.5f * f.vw, oy = f.cy - 0.5f * f.vh;
        int curK = -1;
        for (const FlareQuad &q : quads) {
            if (q.k != curK) {   // um lote por sprite da tabela (textura propria)
                if (curK >= 0) { rlEnd(); rlDrawRenderBatchActive(); }
                curK = q.k;
                const FlareSprite &s = f.spr[q.k];
                unsigned tex = s.texId ? texture(s.texId, s.tex0) : 0;
                rlBegin(RL_QUADS);
                rlSetTexture(tex ? tex : blobTexture());
            }
            const float xa = x0 + (q.xa - ox) * sx, ya = (q.ya - oy) * sy, xb = x0 + (q.xb - ox) * sx, yb = (q.yb - oy) * sy;
            rlColor4ub(q.rgb[0], q.rgb[1], q.rgb[2], 0x80);
            rlTexCoord2f(q.u0, q.v0); rlVertex2f(xa, ya);
            rlTexCoord2f(q.u0, q.v1); rlVertex2f(xa, yb);
            rlTexCoord2f(q.u1, q.v1); rlVertex2f(xb, yb);
            rlTexCoord2f(q.u1, q.v0); rlVertex2f(xb, ya);
        }
        rlEnd();
        rlDrawRenderBatchActive();
        rlSetTexture(0);
        rlNormal3f(0.f, 0.f, 0.f);
        gsBlend(kGsAlphaNormal);
        rlEnableBackfaceCulling();
        rlEnableDepthTest();
        rlMatrixMode(RL_PROJECTION);
        rlPopMatrix();
        rlMatrixMode(RL_MODELVIEW);
        rlSetMatrixModelview(mv3d);
        // diagnostico: WOTM_FLARESHOT=<prefixo> grava ate 8 quadros (>= 1 s entre eles) com um flare de meio-lado >= 8 px do GS
        static const char *shot = std::getenv("WOTM_FLARESHOT");
        static int shots = 0;
        static double lastShot = -10.0;
        if (shot && shots < 8 && big >= 8.f && GetTime() - lastShot >= 1.0) {
            lastShot = GetTime();
            unsigned char *px = rlReadScreenPixels(fbw, fbh);
            for (int i = 0; i < fbw * fbh; ++i) px[4 * i + 3] = 255;   // o alfa do quadro e o das misturas, nao opacidade
            Image img{px, fbw, fbh, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            char name[512];
            std::snprintf(name, sizeof name, "%s_%d.png", shot, shots++);
            ExportImage(img, name);
            RL_FREE(px);
            std::fprintf(stderr, "[flare] captura %s (meio-lado %.1f px, %zu entradas)\n", name, big, vis.size());
        }
    }

    // Diagnostico (WOTM_FXLIST=1): um sistema por linha e a primeira particula
    void dump(const Ram &ram) const {
        std::fprintf(stderr, "[fx] %zu sistemas desenhaveis\n", systems.size());
        for (const FxSystem &s : systems) {
            const uint32_t b = s.base, p = ram.u32(b + fxoff::pos), a = ram.u32(b + fxoff::age);
            const uint32_t ti = ram.u32(b + fxoff::texInfoIndex);
            const uint32_t tinfo = fxaddr::textureInfo + 8 * (ti & 127);
            std::fprintf(stderr, "[fx] #%u preset %u %s n=%d alpha=%02x cores=%u@%u texInfo=%u (n=%d idx=%u) cpf=%u vida=%d | p0 %.1f %.1f %.1f tam %.2f idade %d\n",
                         s.index, ram.u32(b + fxoff::fxId), fxTypeName(s.type), s.n, ram.u32(b + fxoff::alphaContext),
                         ram.u32(b + fxoff::numColors), ram.u32(b + fxoff::colorIndex), ti, ram.s16(tinfo), ram.u16(tinfo + 2),
                         ram.u32(b + fxoff::colorsPerFrame), ram.s16(b + fxoff::lifetime),
                         ram.f32(p), ram.f32(p + 4), ram.f32(p + 8), ram.f32(p + 12), ram.s16(a));
        }
    }

    // Desenha (dentro de BeginMode3D, depois dos objetos opacos e translucidos). Testa profundidade, nao escreve.
    // eye: olho da camera (o W da neblina e medido a partir dele, como no VU1)
    void draw(const Ram &ram, const TexFn &texture, const float eye[3]) {
        static const bool off = std::getenv("WOTM_NOFX") != nullptr;
        if (off || (systems.empty() && synth.empty())) return;
        const Matrix mv = rlGetMatrixModelview();
        FxView view{{mv.m0, mv.m4, mv.m8}, {mv.m1, mv.m5, mv.m9}, {mv.m2, mv.m6, mv.m10}, {-mv.m2, -mv.m6, -mv.m10, -mv.m14},
                    {mv.m0, mv.m4, mv.m8, mv.m12}, {mv.m1, mv.m5, mv.m9, mv.m13}};
        float row3[4];
        for (int k = 0; k < 4; ++k) row3[k] = ram.f32(viewW2S() + 48 + 4 * k);
        setFog(ram, view, row3, eye);
        // particleDraw: do mais longe ao mais perto pela profundidade de emit.ParticleToEmitVec + vis.priority; opacos (alpha 0) antes
        for (FxSystem &s : systems) {
            if (ram.u32(s.base + fxoff::alphaContext) == 0) { s.key = 50000.f + ram.f32(s.base + fxoff::priority); continue; }
            const float x = ram.f32(s.base + fxoff::emitVec), y = ram.f32(s.base + fxoff::emitVec + 4), z = ram.f32(s.base + fxoff::emitVec + 8);
            s.key = -(mv.m2 * x + mv.m6 * y + mv.m10 * z + mv.m14) + ram.f32(s.base + fxoff::priority);
        }
        std::stable_sort(systems.begin(), systems.end(), [](const FxSystem &a, const FxSystem &b) { return a.key > b.key; });
        rlDrawRenderBatchActive();
        rlDisableDepthMask();
        rlDisableBackfaceCulling();
        for (const FxSystem &s : systems) drawSystem(ram, s, texture, view);
        for (const SynthSystem &s : synth) drawSynth(ram, s, texture, view);
        rlDrawRenderBatchActive();
        rlNormal3f(0.f, 0.f, 0.f);   // sem neblina para o que vem depois (overlay)
        rlSetBlendMode(BLEND_ALPHA);
        rlEnableDepthMask();
        rlEnableBackfaceCulling();
    }

    // Cor RGBA (0x80 = 1,0) da particula com idade `age`, como loadColors
    static uint32_t colorAt(const Ram &ram, uint32_t b, int age) {
        const uint32_t nc = ram.u32(b + fxoff::numColors);
        const uint32_t tab = fxaddr::colors + 4 * ram.u32(b + fxoff::colorIndex);
        if (nc == 0) return 0x80808080u;
        if (nc == 1) return ram.u32(tab);
        const uint32_t t = uint32_t(std::max(age, 0)) * ram.u32(b + fxoff::colorsPerFrame);
        uint32_t k = t >> 12, f = t & 0xFFF;
        if (k >= nc - 1) { k = nc - 2; f = 0x1000; }   // o retail nao trava (a particula morre antes); aqui so por seguranca
        const uint32_t c0 = ram.u32(tab + 4 * k), c1 = ram.u32(tab + 4 * (k + 1));
        uint32_t out = 0;
        for (int sh = 0; sh < 32; sh += 8) {
            const int a = int((c0 >> sh) & 255), z = int((c1 >> sh) & 255);
            out |= uint32_t((a + (((z - a) * int(f)) >> 12)) & 255) << sh;
        }
        return out;
    }


    // dustTrail (tipo 15), microprograma 0x800 com o cabecalho do dustTrailPacketInit em 0x20C-0x20E. A palavra de idade (32 bits por
    // particula, particleEmit is15) e a cor: RGB nos bytes 0-2 (os 2 bits baixos do byte 2 escolhem a celula de UV) e o sorteio de
    // tamanho a no byte 3 (de min*255/max a 255); pos.w e a idade normalizada (0..1). Por particula, com W = profundidade:
    //  - meio-lado = exp*vida*w/2 + (min + (max-min)*a/255) * (1 - k + k*a/255) / 2, k = emit.preAgeFactor (+0x2D88); o limite de
    //    0,375 * W fica no forEachQuad;
    //  - alfa = min(W - 128, minAlpha + (maxAlpha-minAlpha)*a/255) * min(1, w/alphaFadeIn, (1-w)/alphaFadeOut), truncado;
    //  - descarta com W <= 128 (MAC Sw|Zw de W - 128) ou alfa < 8 (o F <= 0 e os cantos fora da banda ficam com o fogged e a tela:
    //    com o limite de tamanho, um centro na tela nunca tem canto fora da banda de 4096).
    struct DustTrailVu { bool visible; float half; uint32_t cell; uint32_t rgba; };
    static DustTrailVu dustTrailVu(const Ram &ram, uint32_t b, int i, float w, float W) {
        DustTrailVu d{false, 0.f, 0, 0};
        if (!(W > 128.f)) return d;
        const uint32_t word = ram.u32(ram.u32(b + fxoff::age) + 4 * uint32_t(i));
        const float a = float(word >> 24) / 255.f;
        const float mn = ram.f32(b + fxoff::minSize), mx = ram.f32(b + fxoff::maxSize), k = ram.f32(b + fxoff::preAgeFactor);
        d.half = 0.5f * ram.f32(b + fxoff::expansionRate) * float(ram.s16(b + fxoff::lifetime)) * w + 0.5f * (mn + (mx - mn) * a) * (1.f - k + k * a);
        const float fin = ram.f32(b + fxoff::alphaFadeIn), fout = ram.f32(b + fxoff::alphaFadeOut);
        const float amin = ram.f32(b + fxoff::minAlpha), amax = ram.f32(b + fxoff::maxAlpha);
        const float fade = std::min({1.f, fin > 0.f ? w / fin : 1.f, fout > 0.f ? (1.f - w) / fout : 1.f});   // DIV por 0 no VU: maximo
        const float alpha = std::min(W - 128.f, amin + (amax - amin) * a) * fade;
        if (!(alpha >= 8.f)) return d;
        d.visible = true;
        d.cell = (word >> 16) & 3;
        d.rgba = (word & 0x00FFFFFFu) | (uint32_t(std::min(alpha, 255.f)) << 24);
        return d;
    }

    // wake (16): wakePacketInit e o microprograma 0x3470. Um triangulo por particula com os dados do pacote, que fica logo antes de
    // pos[] (UNPACK de 25 qwords em q264; q264 = pos - 0x19C):
    //  - q264-q279: 8 conjuntos de ST (STQ) de um triangulo equilatero que circunscreve o quadrado [0,1]^2 da textura (vertices
    //    (-0,366; 0), (1,366; 0), (0,5; 1,5) em ordens diferentes): v0 = q[264+k].xy, v1 = q[264+k].zw, v2 = q[272+k].xy, k = bits & 7;
    //  - q282: cor RGB (inteiros, 0x80 = 1,0) de colors[colorIndex], a mesma nos tres vertices (o fxId 0x93 a troca a cada quadro);
    //  - q283-q285: curvas de tamanho (x, z) e de alfa (y, w) pela idade w de pos.w (0..1): ate a transicao (q283.z / q283.w) uma
    //    parabola q285 + q284*w + q283*w^2, depois a reta q285.zw + q284.zw*w (tamanho maxSize + expansao; alfa de minAlpha subindo
    //    128/alphaFadeIn por vida ate a reta que desce 128/alphaFadeOut e chega a maxAlpha em w = 1);
    //  - q287, q288: eixos A = 0,5 * linha 0 e B = -0,25 * linha 0 + 0,433 * linha 2 da StaticMat do emissor (fx+0x2D20; com
    //    info.attribs & 0x4000, X e Y do mundo). Vertices pos + S*A, pos + S*B e pos - S*(A + B).
    // Os 12 bits baixos de pos.w sao fixos por particula (wakeEmit; wakeAndKiteMove preserva a mascara 0xFFF): bits 3-11 escalam o
    // tamanho, S = (bits & 0xFF8) / 4096 * tamanho(w); bits 0-2 escolhem o conjunto de ST. Alfa = min(alfa(w), 128) truncado (o GS
    // pega o byte baixo). Descarta com W > 4000 (MAC Sw|Zw de W - 4000).
    struct WakeVu { bool visible; float c[3][3]; float uv[3][2]; uint32_t rgba; };
    static WakeVu wakeVu(const Ram &ram, uint32_t b, int i, float W) {
        WakeVu r{};
        const uint32_t pos = ram.u32(b + fxoff::pos), q264 = pos - 0x19C, pw = pos + 16 * uint32_t(i);
        if (!(W <= 4000.f) || !ram.ok(q264, 0x19C) || !ram.ok(pw, 16)) return r;
        auto q = [&](int k, int c) { return ram.f32(q264 + 16 * uint32_t(k - 264) + 4 * uint32_t(c)); };
        const float w = ram.f32(pw + 12);
        const uint32_t bits = ram.u32(pw + 12) & 0xFFFF;
        const float w2 = w * w;
        const float size = q(283, 2) - w < 0.f ? q(285, 2) + q(284, 2) * w : q(285, 0) + q(284, 0) * w + q(283, 0) * w2;
        const float alpha = q(283, 3) - w < 0.f ? q(285, 3) + q(284, 3) * w : q(285, 1) + q(284, 1) * w + q(283, 1) * w2;
        const float S = float(bits & 0xFF8) / 4096.f * size;
        float A[3], B[3];
        for (int a = 0; a < 3; ++a) { A[a] = q(287, a) * S; B[a] = q(288, a) * S; }
        for (int a = 0; a < 3; ++a) {
            const float p = ram.f32(pw + 4 * uint32_t(a));
            r.c[0][a] = p + A[a]; r.c[1][a] = p + B[a]; r.c[2][a] = p - A[a] - B[a];
        }
        const int k = int(bits & 7);
        r.uv[0][0] = q(264 + k, 0); r.uv[0][1] = q(264 + k, 1);
        r.uv[1][0] = q(264 + k, 2); r.uv[1][1] = q(264 + k, 3);
        r.uv[2][0] = q(272 + k, 0); r.uv[2][1] = q(272 + k, 1);
        const float am = std::min(alpha, 128.f);
        const int ai = std::isfinite(am) ? int(am) : 0;
        const uint32_t q282 = q264 + 16 * 18;
        r.rgba = (ram.u32(q282) & 255) | ((ram.u32(q282 + 4) & 255) << 8) | ((ram.u32(q282 + 8) & 255) << 16) | (uint32_t(ai & 255) << 24);
        r.visible = std::isfinite(S);
        return r;
    }

    static bool isStrip(uint32_t type) { return type == FX_STRIPE || type == FX_SWEEPTRAIL; }

    // Neblina das particulas: o contexto comum dos microprogramas (qwords 0-7, de 0x256B90) leva fogFA em q4.w e fogFB em q5.w, os
    // mesmos de vu1FogParams; o VU1 calcula F = min(fogFA + fogFB / W, 255) e descarta a particula com F < 1 (CLIP), com ou sem FGE.
    // Medido na METRO (fogFA -107,9, fogFB 326.600: W > ~3.000): os pedestres somem ali (VU1CHECK.md, "Explicadas").
    static void setFog(const Ram &ram, FxView &v, const float row3[4], const float eye[3]) {
        v.fogFA = ram.f32(addr::vu1FogParams);
        v.fogFB = ram.f32(addr::vu1FogParams + 12);
        v.fogW[3] = row3[3];
        for (int a = 0; a < 3; ++a) { v.fogW[a] = row3[a]; v.fogW[3] -= row3[a] * eye[a]; }
    }
    // Ponto (mundo) todo na neblina: F < 1
    static bool foggedAt(const FxView &v, const float p[3]) {
        if (v.fogFB == 0.f) return false;
        const float w = v.fogW[0] * p[0] + v.fogW[1] * p[1] + v.fogW[2] * p[2] + v.fogW[3];
        return w > 1e-3f && v.fogFA + v.fogFB / w < 1.f;
    }
    // Particula toda na neblina (no centro do quadrado)
    static bool fogged(const FxView &v, const float c[4][3]) {
        float p[3];
        for (int a = 0; a < 3; ++a) p[a] = 0.25f * (c[0][a] + c[1][a] + c[2][a] + c[3][a]);
        return foggedAt(v, p);
    }

    // Gerador do VU (RINIT/RNEXT, como o PCSX2): R em [1, 2) com 23 bits de LFSR
    static uint32_t vuRinit(uint32_t bits) { return 0x3F800000u | (bits & 0x7FFFFFu); }
    static float vuRnext(uint32_t &R) {
        const uint32_t x = (R >> 4) & 1, y = (R >> 22) & 1;
        R = (((R << 1) ^ x ^ y) & 0x7FFFFFu) | 0x3F800000u;
        float f;
        std::memcpy(&f, &R, 4);
        return f;
    }

    // Arco eletrico (tipos 7 e 17; electricArcPacketInit/Build/Update, microprograma 0x19F8, lido em build/vu1/particle.txt).
    // Pacote: pos[] (fx +0x2EB0) em cadeias (cabeca: w baixo = 1 + elos; elo: w baixo = 5 << (nivel - 1), ou 0 = reto; cabeca 0
    // encerra); antes dele o cabecalho (pos - 0x28: minSize, semente, segInitU, 138 + CLUToffset, expansionRate, jag, segDeltaU,
    // numColors - 1) e 16 cores RGBA em float 0..255 (pos - 0x13C), que o electricArcPacketUpdate reescreve.
    //  - RINIT(semente) uma vez por pacote. Elo de nivel L de a a b: 2^L sub-trechos; amp = jag * |b - a|, metade a cada nivel; em cada
    //    vao, meio = 0,5a + r * amp - 1,5 * amp + 0,5b, r = (rx, ry, rz) numa janela deslizante de RNEXT (tres na entrada do elo; depois
    //    de cada meio, rx = ry, ry = rz, rz = RNEXT).
    //  - Fita (sub-rotina 0x1F70) nos S + 1 pontos, em coordenadas de tela isotropicas com y para baixo (o VU1 corrige o y pela razao
    //    dos focos): fantasmas 2P0 - P1 e 2PS - PS-1 (homogeneos), t_in e t_out unitarios, n = (t.y, -t.x); v = n_in + n_out +
    //    (t_out - t_in) se t_in x t_out < 0, senao + (t_in - t_out) (sem renormalizar). Vertices p +/- s_k / 2 * v no plano de vista,
    //    s_k = minSize + exp * k / S; A (+) com V = 1, B (-) com V = 0; U_k = segInitU + segDeltaU * (k - S / 2) (perspectiva, STQ);
    //    cor = lerp da tabela em clut + k * (nc - 1) / S, + 0,5, truncada (a mesma nos dois vertices).
    //  - Recorte (0x2210-0x2288): CLIP de cada vertice da tira, A e B, contra a banda (x e y do GS em [0, 4096], z = W >= 1), e o ADC
    //    vem das flags dos ultimos 4 vertices. Resultado: some cada triangulo da tira (A_k, B_k, A_k+1) e (B_k, A_k+1, B_k+1) com algum
    //    vertice fora. O F < 1 fica no forEachQuad (pelo centro).
    // g(k, cantos[4][3] (A_k, B_k, B_k+1, A_k+1), uv[4][2], cor[2] (RGBA 0x80 = 1,0, em k e k + 1), mascara): bit 0 = triangulo
    // (A_k, B_k, A_k+1) = cantos 0, 1, 3; bit 1 = (B_k, A_k+1, B_k+1) = cantos 1, 3, 2 (a diagonal da tira e B_k - A_k+1).
    // Corrente (tipo 12; chainCreate/chainPacketInit/Build, microprograma 0x1D00, que chama a mesma fita 0x1F70): o mesmo cabecalho
    // em outro lugar (pos - 0x4C: minSize ja multiplicado por 0,5 pelo EE, -, segInitU, clut 642; pos - 0x3C: expansionRate, -,
    // segDeltaU, numColors - 1) e 2 cores em pos - 0x28. Cada corrente: cabeca com o numero de pontos no w (inteiro); sem
    // subdivisao (os pontos vao direto para a fita). Analise: git show 75f7fd4:docs/investigacoes/2026-10-17/01_corda_ultrav.md.
    template <class G> static void arcQuads(const Ram &ram, const FxSystem &s, const FxView &view, G &&g) {
        const uint32_t pos = ram.u32(s.base + fxoff::pos);
        const bool chain = s.type == FX_CHAIN;
        const uint32_t hdr = chain ? 0x4C : 0x28, cols = chain ? 0x28 : 0x13C;
        const int lastCol = chain ? 1 : 15;
        if (s.n < 3 || !ram.ok(pos - std::max(hdr, cols), std::max(hdr, cols) + 16 * uint32_t(s.n))) return;
        const float minSize = ram.f32(pos - hdr), segInitU = ram.f32(pos - hdr + 8), clut = chain ? 0.f : ram.f32(pos - 0x1C) - 138.f;
        const float expRate = ram.f32(pos - hdr + 0x10), jag = chain ? 0.f : ram.f32(pos - 0x14), segDeltaU = ram.f32(pos - hdr + 0x18),
                    ncm1 = ram.f32(pos - hdr + 0x1C);
        uint32_t R = vuRinit(chain ? 0u : ram.u32(pos - 0x24));
        struct V3 { float x, y, z; };
        auto at = [&](int i) { return V3{ram.f32(pos + 16 * uint32_t(i)), ram.f32(pos + 16 * uint32_t(i) + 4), ram.f32(pos + 16 * uint32_t(i) + 8)}; };
        auto w16 = [&](int i) { return int(ram.u16(pos + 16 * uint32_t(i) + 12)); };
        auto color = [&](float cpos) {   // cor RGBA (0x80 = 1,0) na posicao cpos da tabela (lerp, + 0,5, truncada)
            const int k0 = std::clamp(int(cpos), 0, lastCol), k1 = std::min(k0 + 1, lastCol);
            const float fr = cpos - float(int(cpos));
            uint32_t out = 0;
            for (int c = 0; c < 4; ++c) {
                const float a = ram.f32(pos - cols + 16 * uint32_t(k0) + 4 * uint32_t(c)), b = ram.f32(pos - cols + 16 * uint32_t(k1) + 4 * uint32_t(c));
                const float v = a + 0.5f - a * fr + b * fr;
                out |= uint32_t(std::clamp(int(v), 0, 255)) << (8 * c);
            }
            return out;
        };
        // tela isotropica (y para baixo) e W do VU1 (linha 3 do worldToScreenMat; sem camera do jogo, a profundidade)
        auto proj = [&](const V3 &p, float &x, float &y) {
            const float z = view.depth[0] * p.x + view.depth[1] * p.y + view.depth[2] * p.z + view.depth[3];
            const float iz = 1.f / (std::fabs(z) > 1e-6f ? z : 1e-6f);
            x = (view.ox[0] * p.x + view.ox[1] * p.y + view.ox[2] * p.z + view.ox[3]) * iz;
            y = -(view.oy[0] * p.x + view.oy[1] * p.y + view.oy[2] * p.z + view.oy[3]) * iz;
        };
        auto wOf = [&](const V3 &p) {
            if (view.fogW[0] == 0.f && view.fogW[1] == 0.f && view.fogW[2] == 0.f)
                return view.depth[0] * p.x + view.depth[1] * p.y + view.depth[2] * p.z + view.depth[3];
            return view.fogW[0] * p.x + view.fogW[1] * p.y + view.fogW[2] * p.z + view.fogW[3];
        };
        // banda de recorte do VU1 na tela do GS (worldToScreenMat com o olho, como o striteTriangle)
        float M[4][4], eye[3];
        for (int r = 0; r < 4; ++r)
            for (int k = 0; k < 4; ++k) M[r][k] = ram.f32(viewW2S() + 16 * uint32_t(r) + 4 * uint32_t(k));
        for (int k = 0; k < 3; ++k) eye[k] = -ram.f32(viewCtxAddr() + 0x20 + 4 * uint32_t(k));   // worldCtx[vista].eo = -olho
        auto outOfBand = [&](const float c[3]) {
            const float d[3] = {c[0] - eye[0], c[1] - eye[1], c[2] - eye[2]};
            float h[4];
            for (int r = 0; r < 4; ++r) h[r] = M[r][0] * d[0] + M[r][1] * d[1] + M[r][2] * d[2] + M[r][3];
            if (!(h[3] >= 1.f)) return true;
            const float x = h[0] / h[3], y = h[1] / h[3];
            return !(x >= 0.f && x <= 4096.f && y >= 0.f && y <= 4096.f);
        };
        std::vector<V3> P;
        int i = 0;
        while (i < s.n) {
            const int links = w16(i) - 1;
            if (links < 0 || i + links >= s.n) break;   // cabeca 0 (fim) ou cadeia truncada
            P.clear();
            P.push_back(at(i));
            for (int l = 1; l <= links; ++l) {
                const V3 b = at(i + l);
                const int wl = chain ? 0 : w16(i + l);   // corrente: o w dos elos nao e lido
                if (wl == 0) { P.push_back(b); continue; }
                int h = wl / 5;   // meia-distancia (em pontos) do primeiro nivel: 2^(L - 1)
                if (h < 1 || h > 64) { P.push_back(b); continue; }
                const size_t base = P.size() - 1, end = base + size_t(2 * h);
                P.resize(end + 1);
                P[end] = b;
                const V3 a = P[base];
                const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
                float amp = jag * std::sqrt(dx * dx + dy * dy + dz * dz);
                float rx = vuRnext(R), ry = vuRnext(R), rz = vuRnext(R);
                for (; h >= 1; h /= 2, amp *= 0.5f)
                    for (size_t j = base; j < end; j += size_t(2 * h)) {
                        const V3 &pa = P[j], &pb = P[j + size_t(2 * h)];
                        V3 &m = P[j + size_t(h)];
                        m.x = 0.5f * pa.x + rx * amp - 1.5f * amp + 0.5f * pb.x;
                        m.y = 0.5f * pa.y + ry * amp - 1.5f * amp + 0.5f * pb.y;
                        m.z = 0.5f * pa.z + rz * amp - 1.5f * amp + 0.5f * pb.z;
                        rx = ry; ry = rz; rz = vuRnext(R);
                    }
            }
            i += links + 1;
            const int S = int(P.size()) - 1;
            if (S < 1) continue;
            // pontos na tela, com os fantasmas nas pontas
            const size_t np = P.size();
            std::vector<float> sx(np + 2), sy(np + 2);
            const V3 g0{2.f * P[0].x - P[1].x, 2.f * P[0].y - P[1].y, 2.f * P[0].z - P[1].z};
            const V3 gn{2.f * P[np - 1].x - P[np - 2].x, 2.f * P[np - 1].y - P[np - 2].y, 2.f * P[np - 1].z - P[np - 2].z};
            proj(g0, sx[0], sy[0]);
            for (size_t k = 0; k < np; ++k) proj(P[k], sx[k + 1], sy[k + 1]);
            proj(gn, sx[np + 1], sy[np + 1]);
            std::vector<float> ox(np), oy(np);   // v de cada ponto (tela isotropica, y para baixo)
            std::vector<char> ok(np);
            // corrente: o 0x1D00 percorre de P[n-1] a P[0] quando W(P[0]) < W(P[n-1]) (comeca pela ponta mais longe; visto no rastreio
            // do VU1 do ultrav_corda); aqui so muda o ramo do produto zero, abaixo
            const bool rev = chain && wOf(P[0]) < wOf(P[np - 1]);
            for (size_t k = 0; k < np; ++k) {
                float tix = sx[k + 1] - sx[k], tiy = sy[k + 1] - sy[k], tox = sx[k + 2] - sx[k + 1], toy = sy[k + 2] - sy[k + 1];
                const float li = std::sqrt(tix * tix + tiy * tiy), lo = std::sqrt(tox * tox + toy * toy);
                // corrente: tangente de comprimento zero vira o vetor zero (no VU, 0 * RSQRT(0) = 0; os pontos do fim coincidem)
                ok[k] = (chain ? li > 1e-12f || lo > 1e-12f : li > 1e-12f && lo > 1e-12f) && wOf(P[k]) >= 1.f;
                if (!ok[k]) continue;
                const float ri = li > 1e-12f ? 1.f / li : 0.f, ro = lo > 1e-12f ? 1.f / lo : 0.f;
                tix *= ri; tiy *= ri; tox *= ro; toy *= ro;
                const float cross = tix * toy - tox * tiy;
                // no zero exato (tangente zero na ponta da corrente) o VU ve +0, "nao negativo": deste lado e o ramo de cross < 0
                // quando a corrente e percorrida na mesma ordem e o outro quando ao contrario (rev)
                const float sgn = cross < 0.f ? 1.f : cross > 0.f ? -1.f : (chain && !rev ? 1.f : -1.f);
                ox[k] = tiy + toy + sgn * (tox - tix);
                oy[k] = -tix - tox + sgn * (toy - tiy);            }
            float cs[4][3], uv[4][2];
            const float u0 = segInitU - segDeltaU * 0.5f * float(S);
            auto corner = [&](size_t k, float side, float out[3]) {
                // o VU1 desloca v * s * (metade do foco, q5.xy) / W pixels: no plano de vista, s / 2 (medido: razao 0,5 contra s)
                // corrente: metade disso (medido na foto 2026-10-17_224038/002: com o q265.x = 10 do pacote, o VU1 da largura 0,25 * 10 * v)
                const float sz = (chain ? 0.25f : 0.5f) * (minSize + expRate * float(k) / float(S));
                const float wx = side * sz * ox[k], wy = side * sz * oy[k];   // no plano de vista: y da tela para baixo = -up
                const float p[3] = {P[k].x, P[k].y, P[k].z};
                for (int a = 0; a < 3; ++a) out[a] = p[a] + view.right[a] * wx - view.up[a] * wy;
            };
            for (size_t k = 0; k + 1 < np; ++k) {
                if (!ok[k] || !ok[k + 1]) continue;
                corner(k, 1.f, cs[0]); corner(k, -1.f, cs[1]); corner(k + 1, -1.f, cs[2]); corner(k + 1, 1.f, cs[3]);
                const bool o[4] = {outOfBand(cs[0]), outOfBand(cs[1]), outOfBand(cs[2]), outOfBand(cs[3])};
                const int mask = (!o[0] && !o[1] && !o[3] ? 1 : 0) | (!o[1] && !o[3] && !o[2] ? 2 : 0);
                if (!mask) continue;
                const float ua = u0 + segDeltaU * float(k), ub = u0 + segDeltaU * float(k + 1);
                uv[0][0] = ua; uv[0][1] = 1.f; uv[1][0] = ua; uv[1][1] = 0.f;
                uv[2][0] = ub; uv[2][1] = 0.f; uv[3][0] = ub; uv[3][1] = 1.f;
                const uint32_t col[2] = {color(clut + float(k) * ncm1 / float(S)), color(clut + float(k + 1) * ncm1 / float(S))};
                g(int(k), cs, uv, col, mask);
            }
        }
    }

    // Sprine (tipo 8; sprinePacketInit/Build, microprograma 0x2770 em build/vu1/particle.txt). O pacote fica antes de pos[] (qword 8):
    // cores RGBA (V4-8, q264-295), UV (V4-16 em 1/16 de texel, q296-299: u0, v0, u1, v1), e q300-q306 num UNPACK de 7 qwords que termina
    // em pos - 4: q300.x = preAgeFactor (pa); q301 = (h (nc - 1) / vida, h exp, -, f6); q302 = (minSize, N, (N + 1) >> 1, sprites por
    // lote; os tres inteiros); q303 e q304 = pesos das diferencas avancadas (x h^2); q305 = M2 (linha 2 da ParticleToEmitMat);
    // q306 = ((n - 1) N, idade[n - 1] N, n - 1; inteiro, float, inteiro). h = 1/N.
    //  - Um trecho por par A = pos[i], B = pos[i - 1] (i de n - 1 a 1), com Pm = pos[i + 1] e Pn = pos[i - 2]; fantasmas: no 1o trecho
    //    Pm = B - 2 M2 ((B - A) . M2) (reflexo de B no plano de A normal a M2), no ultimo Pn = 2 pos[0] - pos[1].
    //  - D1 = q303 . (Pm, A, B, Pn), D2 = q304 . (...), D2' = q304 invertido (w, z, y, x) . (...). p = A; a cada passo: emite em p,
    //    p += D1, D1 += D2; se depois disso faltam (N + 1) >> 1 passos (e N > 2), D1 += c (D2' - D2), D2 = D2' (c = 0,5 com N par,
    //    0,125 impar). N passos por trecho; pos[0] nao e emitido.
    //  - Passo global s (aN = q306.y + s): r = valor corrente do gerador; j = 1 + pa (2 - r); lado = (minSize + q301.y aN) (1 + (r - 1) f6);
    //    cor = tabela em min(31, q301.x aN j) (lerp, + 0,5, truncada; no 1o passo de um trecho que nao comeca lote, j entra duas vezes).
    //    Depois RNEXT, e o UV do passo e q296 + (bits de r & 3). RINIT(pos[n - 1].w) e um RNEXT no comeco; RINIT(pos[i].w) em cada trecho
    //    (so reinicia o gerador; o r corrente segue).
    //  - SPRITE alinhado a tela: centro p +/- (192, 90,24) lado / W px, ou seja meio-lado lado / 2 no plano de vista. Some com W < 1 ou
    //    F < 1 (o F no forEachQuad).
    // g(passo, cantos[4][3] ((-,-), (+,-), (+,+), (-,+) na tela), uv[4][2], cor RGBA (0x80 = 1,0)).
    template <class G> static void sprineQuads(const Ram &ram, const FxSystem &s, const FxView &view, G &&g) {
        const uint32_t pos = ram.u32(s.base + fxoff::pos);
        const uint32_t q300 = pos - 0x74;
        if (s.n < 2 || !ram.ok(q300 - 0x24 - 0x80 - 4, 0x200) || !ram.ok(pos, 16 * uint32_t(s.n))) return;
        if ((ram.u32(q300 - 4) & 0x7F0003FFu) != (0x6C000000u | 300u) || (ram.u32(pos - 4) & 0x3FF) != 8) return;   // UNPACK de q300 e de pos
        const uint32_t uvAt = q300 - 4 - 0x20, colAt = uvAt - 4 - 0x80;   // dados dos UNPACK de q296 (4 x V4-16) e q264 (32 x V4-8)
        if ((ram.u32(uvAt - 4) & 0x3FF) != 296 || (ram.u32(colAt - 4) & 0x3FF) != 264) return;
        auto q = [&](int k, int c) { return ram.f32(q300 + 16 * uint32_t(k - 300) + 4 * uint32_t(c)); };
        auto qi = [&](int k, int c) { return int(ram.u32(q300 + 16 * uint32_t(k - 300) + 4 * uint32_t(c)) & 0xFFFF); };
        const float pa = q(300, 0), cPos = q(301, 0), cExp = q(301, 1), f6 = q(301, 3), minSize = q(302, 0);
        const int N = qi(302, 1), half = qi(302, 2), batch = qi(302, 3), total = qi(306, 0);
        if (N < 2 || N > 64 || batch < 1 || total != (s.n - 1) * N) return;
        float W3[4], W4[4];
        for (int c = 0; c < 4; ++c) { W3[c] = q(303, c); W4[c] = q(304, c); }
        const float M2[3] = {q(305, 0), q(305, 1), q(305, 2)};
        const float cc = (N & 1) ? 0.125f : 0.5f;
        const uint64_t t0 = systemTex0(ram, s.base);
        const float tw = 16.f * float(1u << ((t0 >> 26) & 15)), th = 16.f * float(1u << ((t0 >> 30) & 15));
        struct V3 { float v[3]; };
        auto P = [&](int i) { return V3{{ram.f32(pos + 16 * uint32_t(i)), ram.f32(pos + 16 * uint32_t(i) + 4), ram.f32(pos + 16 * uint32_t(i) + 8)}}; };
        auto wbits = [&](int i) { return ram.u32(pos + 16 * uint32_t(i) + 12); };
        auto color = [&](float cpos) {
            const int k0 = std::clamp(int(cpos), 0, 31), k1 = std::min(k0 + 1, 31);
            const float fr = cpos - float(k0);
            uint32_t out = 0;
            for (int c = 0; c < 4; ++c) {
                const float a = ram.u8(colAt + 4 * uint32_t(k0) + uint32_t(c)), b = ram.u8(colAt + 4 * uint32_t(k1) + uint32_t(c));
                out |= uint32_t(std::clamp(int(a + 0.5f - a * fr + b * fr), 0, 255)) << (8 * c);
            }
            return out;
        };
        auto wOf = [&](const float p[3]) {
            if (view.fogW[0] == 0.f && view.fogW[1] == 0.f && view.fogW[2] == 0.f)
                return view.depth[0] * p[0] + view.depth[1] * p[1] + view.depth[2] * p[2] + view.depth[3];
            return view.fogW[0] * p[0] + view.fogW[1] * p[1] + view.fogW[2] * p[2] + view.fogW[3];
        };
        uint32_t R = vuRinit(wbits(s.n - 1));
        float r = vuRnext(R);
        float aN = q(306, 1);
        int step = 0;
        V3 Pm, A = P(s.n - 1), B = P(s.n - 2), Pn;
        {
            float d = 0.f;
            for (int a = 0; a < 3; ++a) d += (B.v[a] - A.v[a]) * M2[a];
            for (int a = 0; a < 3; ++a) Pm.v[a] = B.v[a] - M2[a] * (2.f * d);
        }
        for (int i = s.n - 1; i >= 1; --i) {
            if (i >= 2) Pn = P(i - 2);
            else for (int a = 0; a < 3; ++a) Pn.v[a] = 2.f * B.v[a] - A.v[a];
            float D1[3], D2[3], D2b[3], p[3];
            for (int a = 0; a < 3; ++a) {
                D1[a] = Pm.v[a] * W3[0] + A.v[a] * W3[1] + B.v[a] * W3[2] + Pn.v[a] * W3[3];
                D2[a] = Pm.v[a] * W4[0] + A.v[a] * W4[1] + B.v[a] * W4[2] + Pn.v[a] * W4[3];
                D2b[a] = Pm.v[a] * W4[3] + A.v[a] * W4[2] + B.v[a] * W4[1] + Pn.v[a] * W4[0];
                p[a] = A.v[a];
            }
            R = vuRinit(wbits(i));
            const bool twiceJ = step % batch != 0;   // 1o passo de trecho que nao comeca lote
            for (int k = 0; k < N; ++k, ++step) {
                const float j = 1.f + pa * (2.f - r);
                const float side = (minSize + cExp * aN) * (1.f + (r - 1.f) * f6);
                const float cpos = std::min(31.f, cPos * aN * j * (k == 0 && twiceJ ? j : 1.f));
                r = vuRnext(R);
                const uint32_t uvq = uvAt + 8 * (R & 3);
                const float u0 = ram.u16(uvq) / tw, v0 = ram.u16(uvq + 2) / th, u1 = ram.u16(uvq + 4) / tw, v1 = ram.u16(uvq + 6) / th;
                if (wOf(p) >= 1.f) {
                    const float h = 0.5f * side;
                    float cs[4][3];
                    for (int a = 0; a < 3; ++a) {
                        const float rx = view.right[a] * h, uy = view.up[a] * h;   // y da tela para baixo = -up
                        cs[0][a] = p[a] - rx + uy; cs[1][a] = p[a] + rx + uy;
                        cs[2][a] = p[a] + rx - uy; cs[3][a] = p[a] - rx - uy;
                    }
                    const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
                    g(step, cs, uv, color(cpos));
                }
                aN += 1.f;
                for (int a = 0; a < 3; ++a) { p[a] += D1[a]; D1[a] += D2[a]; }
                if (N > 2 && N - 1 - k == half)
                    for (int a = 0; a < 3; ++a) { D1[a] = D1[a] - cc * D2[a] + cc * D2b[a]; D2[a] = D2b[a]; }
            }
            Pm = A; A = B; B = Pn;
        }
    }

    // Geometria do sistema em coordenadas de mundo, um quadrado por vez: f(i, cantos[4][3], uv[4][2]), i = particula (nas faixas,
    // a ponta de i a i + 1; cantos 0 e 1 em i, 2 e 3 em i + 1). Linhas (FX_LINE) nao passam por aqui. Serve ao draw e a checagem VU1.
    template <class F> static void forEachQuad(const Ram &ram, const FxSystem &s, const FxView &view, F &&emit) {
        auto f = [&](int i, const float c[4][3], const float uv[4][2]) { if (!fogged(view, c)) emit(i, c, uv); };
        const uint32_t b = s.base;
        const uint32_t pos = ram.u32(b + fxoff::pos), vel = ram.u32(b + fxoff::vel);
        const bool haveVel = ram.ok(vel, 16 * uint32_t(s.n));
        const float *back = view.back;
        auto P = [&](int i, float out[4]) { for (int c = 0; c < 4; ++c) out[c] = ram.f32(pos + 16 * uint32_t(i) + 4 * uint32_t(c)); };
        auto V = [&](int i, float out[3]) { for (int c = 0; c < 3; ++c) out[c] = haveVel ? ram.f32(vel + 16 * uint32_t(i) + 4 * uint32_t(c)) : 0.f; };
        float c[4][3], uv[4][2];
        if (s.type == FX_LINE) return;
        if (s.type == FX_ARC || s.type == FX_ARC2 || s.type == FX_CHAIN) {
            // so os quadrados com os dois triangulos (no VU1 um triangulo sem par conta como solto)
            arcQuads(ram, s, view, [&](int k, const float cc[4][3], const float uu[4][2], const uint32_t *, int mask) { if (mask == 3) f(k, cc, uu); });
            return;
        }
        if (s.type == FX_SPRINE) {
            sprineQuads(ram, s, view, [&](int k, const float cc[4][3], const float uu[4][2], uint32_t) { f(k, cc, uu); });
            return;
        }
        if (isStrip(s.type)) {
            // Faixa ligando particulas consecutivas, de meia-largura |pos.w|. Como no VU1 (medido pela checagem em stripe: a aresta de
            // cada ponta sai perpendicular a faixa na tela), a ponta i abre perpendicular, na tela, a direcao entre as vizinhas
            // (i - 1 e i + 1), com a meia-largura no plano de vista (projeta em |pos.w| * foco / profundidade).
            // stripe: o stripePacketBuild manda ao VU1 a idade da particula mais nova (n - 1), e a largura sai descontada do quanto ela
            // ja cresceu: com o emissor parado a faixa inteira afina. Medido pela checagem (preset 106): meia-largura =
            // |pos.w| - |pos.w[n - 1]| + minSize (com o emissor ativo, a mais nova tem idade ~0 e da |pos.w|).
            // Polilinha das particulas (stripe, sweepTrail), cada ponto com a meia-largura e a particula (para a cor no draw).
            struct Pt { float p[3], hw, u; int seg; };
            std::vector<Pt> pts;
            pts.reserve(size_t(s.n));
            {
                float wHead = 0.f;
                if (s.type == FX_STRIPE) { float ph[4]; P(s.n - 1, ph); wHead = std::fabs(ph[3]) - ram.f32(b + fxoff::minSize); }
                // stripe: U do cabecalho do pacote (pos - 0x28, stripePacketInit/Build): +0x8 = U da particula mais nova (o build o
                // faz rolar a cada quadro), +0x18 = passo por particula (segmentDeltaU / rate); U_i = U0 + passo (n - 1 - i), fora de
                // 0..1 (o GS repete). Conferido na foto 2026-10-18_025733/001 (fx 16: -0,580 + 8/64 na particula 60 de 69).
                const uint32_t posA = ram.u32(b + fxoff::pos);
                const bool vuU = s.type == FX_STRIPE && ram.ok(posA - 0x28, 0x28);
                const float u0 = vuU ? ram.f32(posA - 0x20) : 0.f, du = vuU ? ram.f32(posA - 0x10) : 0.f;
                for (int i = 0; i < s.n; ++i) {
                    float q[4];
                    P(i, q);
                    if (!std::isfinite(q[0])) continue;
                    const float u = vuU ? u0 + du * float(s.n - 1 - i) : float(i) / float(std::max(s.n - 1, 1));
                    pts.push_back({{q[0], q[1], q[2]}, std::max(std::fabs(q[3]) - wHead, 0.f), u, i});
                }
            }
            auto scr = [&](const float *q, float &x, float &y) {
                const float z = view.depth[0] * q[0] + view.depth[1] * q[1] + view.depth[2] * q[2] + view.depth[3];
                const float iz = 1.f / std::max(z, 1e-3f);
                x = (view.ox[0] * q[0] + view.ox[1] * q[1] + view.ox[2] * q[2] + view.ox[3]) * iz;
                y = (view.oy[0] * q[0] + view.oy[1] * q[1] + view.oy[2] * q[2] + view.oy[3]) * iz;
            };
            auto off = [&](size_t i, float o[3]) {
                if (s.type == FX_STRIPE) {
                    // Junta do microprograma do stripe (0x1708, rotina 0x1F70/0x2118), no plano da tela com o y corrigido pela proporcao
                    // (o mesmo que x/z, y/z da vista): d1 = atual - anterior, d2 = proximo - atual (nas pontas, o vizinho que falta e o
                    // reflexo 2*ponta - vizinho), u = d/|d|, p(u) = (u.y, -u.x) e, pelo lado da curva (sinal de d1 x d2),
                    //   v = p(u1) + p(u2) + (u1 - u2)   se d1.x*d2.y - d2.x*d1.y >= 0, senao   p(u1) + p(u2) - (u1 - u2)
                    // e a ponta abre v * meia-largura / 2 (numa reta, |v| = 2). Numa curva fechada a ponta abre quase ao longo do
                    // caminho; o nativo usava a perpendicular a (proximo - anterior), e os rastros em zigue-zague do ExploSmokeTrail
                    // saiam deslocados ate ~16 px (checagem VU1 x nativo: 88% dos quadrados casados).
                    // O fantasma das pontas (2 * ponta - vizinho) e feito nas coordenadas homogeneas, antes da divisao por W: como a
                    // projecao e afim antes dela, e o reflexo no mundo, projetado (refletir na tela mudava direcao e largura da ponta).
                    const size_t last = pts.size() - 1;
                    if (last == 0) return false;
                    float xc, yc, xp, yp, xn, yn;
                    scr(pts[i].p, xc, yc);
                    auto ghost = [&](size_t end, size_t nb, float &gx, float &gy) {
                        float g[3];
                        for (int a = 0; a < 3; ++a) g[a] = 2.f * pts[end].p[a] - pts[nb].p[a];
                        scr(g, gx, gy);
                    };
                    if (i > 0) scr(pts[i - 1].p, xp, yp); else ghost(0, 1, xp, yp);
                    if (i < last) scr(pts[i + 1].p, xn, yn); else ghost(last, last - 1, xn, yn);
                    const float d1x = xc - xp, d1y = yc - yp, d2x = xn - xc, d2y = yn - yc;
                    const float l1 = std::sqrt(d1x * d1x + d1y * d1y), l2 = std::sqrt(d2x * d2x + d2y * d2y);
                    if (!(l1 > 1e-9f) || !(l2 > 1e-9f)) return false;
                    const float u1x = d1x / l1, u1y = d1y / l1, u2x = d2x / l2, u2y = d2y / l2;
                    const float sg = (d1x * d2y - d2x * d1y) >= 0.f ? 1.f : -1.f;
                    const float vx = u1y + u2y + sg * (u1x - u2x), vy = -u1x - u2x + sg * (u1y - u2y);
                    // mesmo sentido da formula antiga numa reta (cantos 0/3 de um lado, 1/2 do outro)
                    const float nx = -0.5f * vx * pts[i].hw, ny = -0.5f * vy * pts[i].hw;
                    for (int a = 0; a < 3; ++a) o[a] = view.right[a] * nx + view.up[a] * ny;
                    return true;
                }
                float xa, ya, xb, yb;
                scr(pts[i > 0 ? i - 1 : 0].p, xa, ya); scr(pts[std::min(i + 1, pts.size() - 1)].p, xb, yb);
                float nx = -(yb - ya), ny = xb - xa;
                const float l = std::sqrt(nx * nx + ny * ny);
                if (!(l > 1e-9f)) return false;
                nx *= pts[i].hw / l; ny *= pts[i].hw / l;
                for (int a = 0; a < 3; ++a) o[a] = view.right[a] * nx + view.up[a] * ny;
                return true;
            };
            for (size_t i = 0; i + 1 < pts.size(); ++i) {
                const Pt &a0 = pts[i], &a1 = pts[i + 1];
                if (a1.seg != a0.seg + 1) continue;   // particula invalida no meio: sem ponte
                float o0[3], o1[3];
                if (!off(i, o0) || !off(i + 1, o1)) continue;
                for (int a = 0; a < 3; ++a) {
                    c[0][a] = a0.p[a] - o0[a]; c[1][a] = a0.p[a] + o0[a];
                    c[2][a] = a1.p[a] + o1[a]; c[3][a] = a1.p[a] - o1[a];
                }
                uv[0][0] = a0.u; uv[0][1] = 0.f; uv[1][0] = a0.u; uv[1][1] = 1.f;
                uv[2][0] = a1.u; uv[2][1] = 1.f; uv[3][0] = a1.u; uv[3][1] = 0.f;
                f(a0.seg, c, uv);
            }
            return;
        }
        if (s.type == FX_PEDS) {
            // Pedestres (pedsCreate/stampPacketInit, PedGroup): carimbo em pe no Z do mundo, base em pos, largura info.minSize na
            // horizontal da camera e altura info.maxSize (medido pela checagem VU1 x nativo: a inclinacao na tela segue a perspectiva
            // de um eixo vertical e o centro fica meia altura acima de pos). pos.w nao e tamanho: e float(variante de cor) (scatter)
            // com a meia-palavra baixa trocada pelo quadro da animacao (update). A folha tem os quadros em U (texMask + 1) e as
            // variantes em V (vis.numColors), du/dv do TextureInfo em 1/16 de texel.
            const float W = ram.f32(b + fxoff::minSize), H = ram.f32(b + fxoff::maxSize);
            float rh[3] = {view.right[0], view.right[1], 0.f};
            const float rl = std::sqrt(rh[0] * rh[0] + rh[1] * rh[1]);
            if (rl < 1e-6f) return;
            rh[0] /= rl; rh[1] /= rl;
            const uint32_t tinfo = fxaddr::textureInfo + 8 * (ram.u32(b + fxoff::texInfoIndex) & 127);
            const uint32_t k = ram.u16(tinfo + 2) & 127;
            const uint64_t t0 = uint64_t(ram.u32(fxaddr::tex0 + 8 * k)) | (uint64_t(ram.u32(fxaddr::tex0 + 8 * k + 4)) << 32);
            const float texW = float(1u << ((t0 >> 26) & 15)), texH = float(1u << ((t0 >> 30) & 15));
            const uint32_t frames = ram.u32(b + fxoff::texMask) + 1, rows = std::max<uint32_t>(ram.u32(b + fxoff::numColors), 1);
            const float du = float(ram.u16(tinfo + 4) >> 4) / float(frames) / texW, dv = float(ram.u16(tinfo + 6) >> 4) / float(rows) / texH;
            for (int i = 0; i < s.n; ++i) {
                float p[4];
                P(i, p);
                uint32_t wbits;
                std::memcpy(&wbits, &p[3], 4);
                if (int32_t(wbits) < 0 || !std::isfinite(p[0])) continue;   // pedsTurnOff: w = -1
                const uint32_t vbits = wbits & 0xFFFF0000u;
                float variant;
                std::memcpy(&variant, &vbits, 4);
                const float u0 = float((wbits & 0xFFFF) % frames) * du, v0 = float(uint32_t(std::max(variant, 0.f)) % rows) * dv;
                for (int a = 0; a < 3; ++a) {
                    c[0][a] = p[a] - rh[a] * W * 0.5f; c[1][a] = p[a] + rh[a] * W * 0.5f;
                    c[2][a] = c[1][a]; c[3][a] = c[0][a];
                }
                c[2][2] += H; c[3][2] += H;
                // VU1 (prog 0x1373): V cresce para cima (pe em v0, cabeca em v0 + dv; a folha guarda os bonecos de cabeca
                // para baixo), com o recuo de meio texel do stampPacketInit (0.5/(du>>4); aqui a regiao e a textura inteira)
                const float hu = 0.5f / texW, hv = 0.5f / texH;
                uv[0][0] = u0 + hu;      uv[0][1] = v0 + hv;
                uv[1][0] = u0 + du - hu; uv[1][1] = v0 + hv;
                uv[2][0] = u0 + du - hu; uv[2][1] = v0 + dv - hv;
                uv[3][0] = u0 + hu;      uv[3][1] = v0 + dv - hv;
                f(i, c, uv);
            }
            return;
        }
        // Sprites (sprite, texSprite, dustTrail, frag, kite, wake, strite): um quad por particula
        const bool dust = isDustTex(systemTex0(ram, b));
        for (int i = 0; i < s.n; ++i) {
            float p[4];
            P(i, p);
            if (!std::isfinite(p[0]) || !std::isfinite(p[3])) continue;
            // microcodigo de sprites (0x2C8) e texSprite (0xF18): meio-lado na tela = vf27.xy * pos.w / W, com vf27.xy = metade do foco
            // (192; 90,24), limitado por MINI a vf17.xy = vf27.xy / 2 (96; 45,12 px). Em mundo: meio-lado = pos.w / 2 e teto = profundidade
            // / 4 (com teto / 2 a explosao do Magmo e o WhiteExplosionFx saiam ate o dobro do tamanho; checagem: todos os quadrados do VU1
            // com 192 x 90,25 px). Os outros tipos que caem aqui continuam com pos.w inteiro e teto / 2 (nao conferidos).
            const float depth = view.depth[0] * p[0] + view.depth[1] * p[1] + view.depth[2] * p[2] + view.depth[3];
            if (s.type == FX_WAKE) {   // microprograma 0x3470 (wakeVu): o triangulo e o ST do pacote; o quad degenera (canto 3 = canto 0)
                const WakeVu wv = wakeVu(ram, b, i, depth);
                if (!wv.visible) continue;
                for (int k = 0; k < 4; ++k) {
                    const int v = k < 3 ? k : 0;
                    for (int a = 0; a < 3; ++a) c[k][a] = wv.c[v][a];
                    uv[k][0] = wv.uv[v][0]; uv[k][1] = wv.uv[v][1];
                }
                f(i, c, uv);
                continue;
            }
            const bool spr = s.type == FX_SPRITE || s.type == FX_TEXSPRITE;
            float half = (spr ? 0.5f : 1.f) * std::fabs(p[3]), cap = spr ? 0.25f : 0.5f;
            uint32_t dustCell = 0;
            if (s.type == FX_DUSTTRAIL) {
                const DustTrailVu d = dustTrailVu(ram, b, i, p[3], depth);
                if (!d.visible) continue;
                half = d.half;
                dustCell = d.cell;
                cap = 0.375f;
            }
            const float r = depth > 0.f ? std::min(half, cap * depth) : half;
            float ax[3] = {view.right[0] * r, view.right[1] * r, view.right[2] * r}, ay[3] = {view.up[0] * r, view.up[1] * r, view.up[2] * r};
            float cpos[3] = {p[0], p[1], p[2]};
            if (s.type == FX_STRITE) {
                // microcodigo do strite: cabeca pos e cauda pos - vel projetadas, e a faixa esticada 2x para cada lado
                // (de pos + 2*vel a pos - 3*vel); a largura sai de pos.w. Aqui no plano da tela, centrada em pos - vel/2.
                float v[3];
                V(i, v);
                const float vd = v[0] * back[0] + v[1] * back[1] + v[2] * back[2];
                float vs[3] = {v[0] - back[0] * vd, v[1] - back[1] * vd, v[2] - back[2] * vd};
                const float vl = std::sqrt(vs[0] * vs[0] + vs[1] * vs[1] + vs[2] * vs[2]);
                if (vl > 1e-4f) {
                    for (int a = 0; a < 3; ++a) vs[a] /= vl;
                    const float side[3] = {vs[1] * back[2] - vs[2] * back[1], vs[2] * back[0] - vs[0] * back[2], vs[0] * back[1] - vs[1] * back[0]};
                    const float len = 2.5f * vl + r * 0.5f;
                    for (int a = 0; a < 3; ++a) { ax[a] = vs[a] * len; ay[a] = side[a] * r * 0.5f; cpos[a] = p[a] - 0.5f * v[a]; }
                }
            }
            // conjunto de UV: os bits baixos de pos.w indexam os cantos que o spritePacketInit sobe (VU 0x168): 3 normal, 1 espelha U,
            // 2 espelha V, 0 os dois. Com info.attribs & 0x2000 sao 16 conjuntos (giros tambem); aqui so os 4 espelhamentos.
            uint32_t wbits;
            std::memcpy(&wbits, &p[3], 4);
            const bool fu = (wbits & 2) == 0, fv = (wbits & 1) == 0;
            float u0 = fu ? 1.f : 0.f, u1 = fu ? 0.f : 1.f, v0 = fv ? 1.f : 0.f, v1 = fv ? 0.f : 1.f;
            if (s.type == FX_DUSTTRAIL) {
                // dustTrailPacketInit: tabela de 4 celulas (cantos (9, 9) a (du/2 - 9, du/2 - 9) mais du/2 em U pela celula & 1 e em V
                // pela celula >> 1, em 1/16 de texel; du do TextureInfo nos dois eixos); o VU escolhe pelo byte 2 da palavra de idade & 3
                const uint32_t tinfo = fxaddr::textureInfo + 8 * (ram.u32(b + fxoff::texInfoIndex) & 127);
                const uint64_t t0 = systemTex0(ram, b);
                const float du = float(ram.u16(tinfo + 4)), tw = 16.f * float(1u << ((t0 >> 26) & 15)), th = 16.f * float(1u << ((t0 >> 30) & 15));
                const float h = std::floor(du * 0.5f);   // celula 0: 9 a h - 9; celula com o bit: h + 9 a du - 9
                u0 = ((dustCell & 1) ? h + 9.f : 9.f) / tw; u1 = ((dustCell & 1) ? du - 9.f : h - 9.f) / tw;
                v0 = ((dustCell >> 1) ? h + 9.f : 9.f) / th; v1 = ((dustCell >> 1) ? du - 9.f : h - 9.f) / th;
            } else if (dust) {   // celula wbits & 3 da textura composta: (9, 9, 503, 503) + 512 * (k & 1, k >> 1), em 1/16 de texel de 64
                const float cu = float(wbits & 1) * 0.5f, cv = float((wbits >> 1) & 1) * 0.5f;
                u0 = cu + 9.f / 1024.f; u1 = cu + 503.f / 1024.f; v0 = cv + 9.f / 1024.f; v1 = cv + 503.f / 1024.f;
            }
            const float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
            for (int k = 0; k < 4; ++k)
                for (int a = 0; a < 3; ++a) c[k][a] = cpos[a] + ax[a] * sx[k] + ay[a] * sy[k];
            // O VU1 manda strite e wake como UM triangulo por particula (checagem VU1 x nativo); aqui o quad degenera (canto 3 = canto 0).
            uv[0][0] = u0; uv[0][1] = v1; uv[1][0] = u1; uv[1][1] = v1;
            uv[2][0] = u1; uv[2][1] = v0; uv[3][0] = u0; uv[3][1] = v0;
            if (s.type == FX_STRITE) {
                float v[3];
                V(i, v);
                if (striteTriangle(ram, b, p, v, c)) {
                    // ST do microprograma (checagem, foto 2026-10-17_200206): ponta (0,5; 1,5), base (1,366; 0) e (-0,366; 0). O triangulo
                    // equilatero circunscreve o quadrado da textura: o disco fica inscrito (raio 0,5) e o resto cai fora de 0..1 (texturas de particula em CLAMP)
                    uv[0][0] = uv[3][0] = 0.5f; uv[0][1] = uv[3][1] = 1.5f;
                    uv[1][0] = 1.3660254f; uv[1][1] = 0.f;
                    uv[2][0] = -0.3660254f; uv[2][1] = 0.f;
                } else {   // sem a camera do jogo: ponta no meio dos cantos 0 e 3 (aproximacao antiga)
                    for (int a = 0; a < 3; ++a) c[0][a] = c[3][a] = cpos[a] - ax[a];
                }
            }
            f(i, c, uv);
        }
    }

private:
    unsigned blob_ = 0;   // textura de reserva (disco suave) quando a do sistema nao decodifica
    std::unordered_map<uint32_t, float> flarePrev_;   // debounce do lens flare: fracao visivel do quadro anterior por ID

    unsigned blobTexture() {
        if (blob_) return blob_;
        const int n = 64;
        std::vector<uint32_t> px(size_t(n * n));
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                const float dx = (x + 0.5f) / n * 2.f - 1.f, dy = (y + 0.5f) / n * 2.f - 1.f;
                const float r = std::sqrt(dx * dx + dy * dy);
                const float a = std::max(0.f, 1.f - r);
                px[size_t(y * n + x)] = 0x00FFFFFFu | (uint32_t(a * a * 128.f) << 24);   // alfa do GS: 0x80 = 1,0
            }
        Image img{px.data(), n, n, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        const Texture2D t = LoadTextureFromImage(img);
        if (t.id) { SetTextureFilter(t, TEXTURE_FILTER_BILINEAR); SetTextureWrap(t, TEXTURE_WRAP_CLAMP); }   // CLAMP como as do jogo (strite usa UV fora de 0..1)
        blob_ = t.id;
        return blob_;
    }

    // Registrador ALPHA do GS do sistema (vis.alphaContext; gsBlend). 0 = sistema opaco (desenhado antes, ver draw): fica a mistura comum.
    static uint8_t blendFor(uint32_t alpha) { return alpha ? uint8_t(alpha & 0xFF) : kGsAlphaNormal; }

    // RGBAQ do GS (0x80 = 1,0) cru: o modo GS do shader (shaderNormal) faz texel * Cv / 128 e At * Av / 16384
    static void vcolor(uint32_t c) { rlColor4ub(uint8_t(c & 255), uint8_t((c >> 8) & 255), uint8_t((c >> 16) & 255), uint8_t(c >> 24)); }

    // Quadro de textura do texSprite (texSpritePacketBuild)
    static uint32_t texFrame(const Ram &ram, uint32_t b, int i, int age) {
        const uint32_t frames = ram.u32(b + fxoff::texMask), attr = ram.u32(b + fxoff::infoAttribs);
        if (frames == 0) return 0;
        if (attr & 1) return uint32_t(i * 2654435761u >> 16) % frames;   // retail: rand() a cada quadro; aqui fixo por particula
        if (attr & 2) return (ram.u8(b + fxoff::uvI + uint32_t(i)) >> 2) % frames;
        const uint32_t f = (uint32_t(std::max(age, 0)) * ram.u32(b + fxoff::mapsPerFrame)) >> 12;
        if (attr & 4) return std::min(f, frames - 1);
        return f % frames;
    }

public:
    // Cor (RGBAQ do GS, 0x80 = 1,0) do quadrado i de forEachQuad como o drawSystem a desenha: col[0] nos cantos 0 e 1, col[1] nos 2 e 3
    // (so as faixas mudam de uma ponta para a outra). Nao serve a arco, corrente e sprine (a cor sai de arcQuads/sprineQuads).
    static void quadColor(const Ram &ram, const FxSystem &s, const FxView &view, int i, uint32_t col[2]) {
        const uint32_t b = s.base;
        const uint32_t pos = ram.u32(b + fxoff::pos), vel = ram.u32(b + fxoff::vel), age = ram.u32(b + fxoff::age);
        const bool haveVel = ram.ok(vel, 16 * uint32_t(s.n)), haveAge = ram.ok(age, 2 * uint32_t(s.n));
        auto A = [&](int k) { return haveAge ? int(ram.s16(age + 2 * uint32_t(k))) : 0; };
        // pedestres: vis.colorIndex e a propria cor RGBA (stampPacketInit a sobe byte a byte); numColors conta as variantes da folha
        uint32_t c0 = s.type == FX_PEDS ? ram.u32(b + fxoff::colorIndex) : colorAt(ram, b, A(i));
        if (s.type == FX_DUSTTRAIL) {   // cor e alfa do microprograma (dustTrailVu): a palavra de idade, nao a tabela de cores
            float p[3];
            for (int a = 0; a < 3; ++a) p[a] = ram.f32(pos + 16 * uint32_t(i) + 4 * uint32_t(a));
            const float W = view.depth[0] * p[0] + view.depth[1] * p[1] + view.depth[2] * p[2] + view.depth[3];
            c0 = dustTrailVu(ram, b, i, ram.f32(pos + 16 * uint32_t(i) + 12), W).rgba;
        } else if (s.type == FX_WAKE) {   // cor fixa do pacote e alfa pela curva do microprograma (wakeVu)
            float p[3];
            for (int a = 0; a < 3; ++a) p[a] = ram.f32(pos + 16 * uint32_t(i) + 4 * uint32_t(a));
            c0 = wakeVu(ram, b, i, view.depth[0] * p[0] + view.depth[1] * p[1] + view.depth[2] * p[2] + view.depth[3]).rgba;
        } else if (s.type == FX_STRITE && haveVel) {
            // cor pela posicao vel.w na tabela (microprograma 0xA88: indice FTOI0(vel.w), lerp, + 0,5; vel.w e a idade com espalhamento
            // por particula, nao idade * colorsPerFrame) e alfa escalado pelo comprimento do rastro (striteTriangle, alphaK)
            float p[4], v[4], tri[4][3], k = 1.f;
            for (int a = 0; a < 4; ++a) p[a] = ram.f32(pos + 16 * uint32_t(i) + 4 * uint32_t(a));
            for (int a = 0; a < 4; ++a) v[a] = ram.f32(vel + 16 * uint32_t(i) + 4 * uint32_t(a));
            const uint32_t nc = ram.u32(b + fxoff::numColors), tab = fxaddr::colors + 4 * ram.u32(b + fxoff::colorIndex);
            float cf[4] = {128.5f, 128.5f, 128.5f, 128.5f};
            if (nc > 0 && std::isfinite(v[3]) && v[3] >= 0.f) {
                const int k0 = std::min(int(v[3]), int(nc) - 1), k1 = std::min(k0 + 1, int(nc) - 1);   // so numColors entradas no pacote
                const float fr = std::min(v[3] - float(k0), 1.f);
                const uint32_t e0 = ram.u32(tab + 4 * uint32_t(k0)), e1 = ram.u32(tab + 4 * uint32_t(k1));
                for (int a = 0; a < 4; ++a) cf[a] = float((e0 >> (8 * a)) & 255) * (1.f - fr) + float((e1 >> (8 * a)) & 255) * fr + 0.5f;
            }
            if (!striteTriangle(ram, b, p, v, tri, &k)) k = 1.f;
            auto ub = [](float x) { return uint32_t(std::clamp(x, 0.f, 255.f)); };   // FTOI0 trunca
            c0 = ub(cf[0]) | ub(cf[1]) << 8 | ub(cf[2]) << 16 | ub(cf[3] * k) << 24;
        }
        col[0] = c0;
        col[1] = isStrip(s.type) ? colorAt(ram, b, A(i + 1)) : c0;
        // stripePacketInit: com info.attribs & 0x200 o RGB de todas as cores do pacote vem do vetor info.useThisColor (trailColoring da
        // fase no rastro de poeira) e o alfa e o da tabela vezes o w do vetor. So o stripe (o frag tem a sua versao no Scene).
        if (s.type == FX_STRIPE && (ram.u32(b + fxoff::infoAttribs) & 0x200)) {
            const uint32_t cp = ram.u32(b + fxoff::useThisColor);
            if (ram.ok(cp, 16)) {
                auto ub = [](float v) { return uint32_t(std::clamp(v + 0.5f, 0.f, 255.f)); };   // lerp de valores iguais + 0,5, FTOI0
                const uint32_t rgb = ub(ram.f32(cp)) | ub(ram.f32(cp + 4)) << 8 | ub(ram.f32(cp + 8)) << 16;
                const float w = ram.f32(cp + 12);
                for (int k = 0; k < 2; ++k) col[k] = rgb | ub(float(col[k] >> 24) * w - 0.5f) << 24;
            }
        }
    }

    // forEachQuad com a cor que o desenho usa em cada quadrado: emit(i, cantos, uv, cor dos cantos 0 e 1, cor dos cantos 2 e 3).
    // Mesma geometria e mesmo descarte do forEachQuad (arco e corrente so com os dois triangulos). Serve a checagem VU1.
    template <class F> static void forEachQuadColored(const Ram &ram, const FxSystem &s, const FxView &view, F &&emit) {
        if (s.type == FX_LINE) return;
        if (s.type == FX_ARC || s.type == FX_ARC2 || s.type == FX_CHAIN) {
            arcQuads(ram, s, view, [&](int k, const float c[4][3], const float uv[4][2], const uint32_t *col, int mask) {
                if (mask == 3 && !fogged(view, c)) emit(k, c, uv, col[0], col[1]);
            });
            return;
        }
        if (s.type == FX_SPRINE) {
            sprineQuads(ram, s, view, [&](int k, const float c[4][3], const float uv[4][2], uint32_t col) { if (!fogged(view, c)) emit(k, c, uv, col, col); });
            return;
        }
        forEachQuad(ram, s, view, [&](int i, const float c[4][3], const float uv[4][2]) {
            uint32_t col[2];
            quadColor(ram, s, view, i, col);
            emit(i, c, uv, col[0], col[1]);
        });
    }

private:
    // Sistema sintetico (synth): o caminho dos sprites do drawSystem/forEachQuad com as particulas daqui
    void drawSynth(const Ram &ram, const SynthSystem &s, const TexFn &texture, const FxView &view) {
        if (s.sprites.empty()) return;
        const uint32_t tinfo = fxaddr::textureInfo + 8 * (s.texInfo & 127);
        const uint32_t k = ram.u16(tinfo + 2) & 127;
        const uint32_t id = ram.u16(fxaddr::texId + 2 * k);
        const uint64_t t0 = uint64_t(ram.u32(fxaddr::tex0 + 8 * k)) | (uint64_t(ram.u32(fxaddr::tex0 + 8 * k + 4)) << 32);
        unsigned tex = (ram.s16(tinfo) > 0 && id) ? texture(id, t0) : 0;
        if (!tex) tex = blobTexture();
        rlDrawRenderBatchActive();
        const uint8_t blend = blendFor(s.alphaContext);
        gsBlend(blend);
        shaderNormal(0.f, true, true, gsBlendPremul(blend));
        rlBegin(RL_QUADS);
        rlSetTexture(tex);
        for (const SynthSprite &q : s.sprites) {
            const float depth = view.depth[0] * q.p[0] + view.depth[1] * q.p[1] + view.depth[2] * q.p[2] + view.depth[3];
            const float half = 0.5f * std::fabs(q.size), r = depth > 0.f ? std::min(half, 0.25f * depth) : half;
            float c[4][3];
            const float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
            for (int k = 0; k < 4; ++k)
                for (int a = 0; a < 3; ++a) c[k][a] = q.p[a] + r * (view.right[a] * sx[k] + view.up[a] * sy[k]);
            if (fogged(view, c)) continue;
            const bool fu = (q.sel & 2) == 0, fv = (q.sel & 1) == 0;
            const float u0 = fu ? 1.f : 0.f, u1 = 1.f - u0, v0 = fv ? 1.f : 0.f, v1 = 1.f - v0;
            const float uv[4][2] = {{u0, v1}, {u1, v1}, {u1, v0}, {u0, v0}};
            for (int j = 0; j < 4; ++j) {
                vcolor(q.rgba);
                rlTexCoord2f(uv[j][0], uv[j][1]);
                rlVertex3f(c[j][0], c[j][1], c[j][2]);
            }
        }
        rlEnd();
        rlSetTexture(0);
    }

    void drawSystem(const Ram &ram, const FxSystem &s, const TexFn &texture, const FxView &view) {
        const uint32_t b = s.base;
        const uint32_t pos = ram.u32(b + fxoff::pos), vel = ram.u32(b + fxoff::vel), age = ram.u32(b + fxoff::age);
        const uint32_t ti = ram.u32(b + fxoff::texInfoIndex) & 127;
        const uint32_t tinfo = fxaddr::textureInfo + 8 * ti;
        const int16_t numTex = ram.s16(tinfo);
        const uint32_t texIndex = ram.u16(tinfo + 2);
        const bool haveVel = ram.ok(vel, 16 * uint32_t(s.n));
        const bool haveAge = ram.ok(age, 2 * uint32_t(s.n));

        auto texFor = [&](uint32_t frame) -> unsigned {
            const uint32_t k = (texIndex + frame) & 127;
            const uint32_t id = ram.u16(fxaddr::texId + 2 * k);
            const uint64_t t0 = uint64_t(ram.u32(fxaddr::tex0 + 8 * k)) | (uint64_t(ram.u32(fxaddr::tex0 + 8 * k + 4)) << 32);
            unsigned gl = (numTex > 0 && id) ? texture(id, t0) : isDustTex(t0) ? texture(kDustTexId, t0) : 0;
            return gl ? gl : blobTexture();
        };

        rlDrawRenderBatchActive();
        const uint8_t blend = blendFor(ram.u32(b + fxoff::alphaContext));
        gsBlend(blend);
        // neblina no shader do Scene::draw: o XGKICK das particulas leva PRIM.FGE em todos os tipos medidos pela checagem VU1 (sprite,
        // texSprite, stripe, strite, arco, sprine, frag, pedestre, dustTrail); alfa do GS (texturas com alfa cru); o draw zera no fim
        shaderNormal(0.f, true, true, gsBlendPremul(blend));
        auto A = [&](int i) { return haveAge ? int(ram.s16(age + 2 * uint32_t(i))) : 0; };

        if (s.type == FX_LINE) {
            shaderNormal(0.f, false, true, gsBlendPremul(blend));   // PRIM 0x49 sem FGE: a neblina ja vem no alfa (lineVu)
            rlBegin(RL_LINES);
            rlSetTexture(gsWhiteTexture());   // sem textura no modo GS: alfa 0x80
            for (int i = 0; i < s.n; ++i) {
                float h[3], t[3];
                uint32_t c;
                if (!lineVu(ram, b, view, i, h, t, c)) continue;
                vcolor(c);
                rlVertex3f(h[0], h[1], h[2]);
                vcolor(c & 0x00FFFFFFu);
                rlVertex3f(t[0], t[1], t[2]);
            }
            rlEnd();
            return;
        }

        if (s.type == FX_ARC || s.type == FX_ARC2 || s.type == FX_CHAIN) {   // fita com a cor de cada ponto (arcQuads), em triangulos com a diagonal da tira
            rlBegin(RL_TRIANGLES);   // rlBegin antes de rlSetTexture (ver a armadilha em wotm_native.hpp)
            const unsigned tex = texFor(0);
            rlSetTexture(tex);
            // corrente: o U vai de segInitU - 0,15 (n - 1) a segInitU (fora de 0..1) e o GS repete a ChainTex (a corda torcida da
            // referencia); as texturas de particula ficam em CLAMP, entao REPEAT no S so durante este sistema
            const bool repS = s.type == FX_CHAIN;
            if (repS) rlTextureParameters(tex, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_REPEAT);
            arcQuads(ram, s, view, [&](int, const float c[4][3], const float uv[4][2], const uint32_t col[2], int mask) {
                if (fogged(view, c)) return;
                static const int tri[2][3] = {{0, 1, 3}, {1, 3, 2}};
                for (int t = 0; t < 2; ++t) {
                    if (!(mask & (1 << t))) continue;
                    for (int k : tri[t]) {
                        vcolor(k < 2 ? col[0] : col[1]);
                        rlTexCoord2f(uv[k][0], uv[k][1]);
                        rlVertex3f(c[k][0], c[k][1], c[k][2]);
                    }
                }
            });
            rlEnd();
            if (repS) { rlDrawRenderBatchActive(); rlTextureParameters(tex, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP); }
            rlSetTexture(0);
            return;
        }

        if (s.type == FX_SPRINE) {   // sprites da curva com a cor de cada passo (sprineQuads)
            rlBegin(RL_QUADS);
            rlSetTexture(texFor(0));
            sprineQuads(ram, s, view, [&](int, const float c[4][3], const float uv[4][2], uint32_t col) {
                if (fogged(view, c)) return;
                for (int k = 0; k < 4; ++k) {
                    vcolor(col);
                    rlTexCoord2f(uv[k][0], uv[k][1]);
                    rlVertex3f(c[k][0], c[k][1], c[k][2]);
                }
            });
            rlEnd();
            rlSetTexture(0);
            return;
        }

        // faixas: uma textura e a cor de cada ponta; sprites: textura (quadro do texSprite) e cor por particula
        const bool strip = isStrip(s.type), perFrame = s.type == FX_TEXSPRITE;
        const unsigned stripTex = strip ? texFor(0) : 0;
        const bool repS = s.type == FX_STRIPE && stripTex != 0;   // U fora de 0..1 (cabecalho do pacote): REPEAT no S so durante este sistema
        if (repS) rlTextureParameters(stripTex, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_REPEAT);
        unsigned cur = ~0u;
        bool open = false;
        forEachQuad(ram, s, view, [&](int i, const float c[4][3], const float uv[4][2]) {
            const int ag = A(i);
            const unsigned tex = strip ? stripTex : texFor(perFrame ? texFrame(ram, b, i, ag) : 0);
            if (tex != cur || !open) {
                if (open) { rlEnd(); rlDrawRenderBatchActive(); }
                rlBegin(RL_QUADS);   // rlBegin antes de rlSetTexture (ver a armadilha em wotm_native.hpp)
                rlSetTexture(tex);
                cur = tex;
                open = true;
            }
            uint32_t col[2];
            quadColor(ram, s, view, i, col);
            for (int k = 0; k < 4; ++k) {
                vcolor(col[k < 2 ? 0 : 1]);
                rlTexCoord2f(uv[k][0], uv[k][1]);
                rlVertex3f(c[k][0], c[k][1], c[k][2]);
            }
        });
        if (open) rlEnd();
        if (repS) { rlDrawRenderBatchActive(); rlTextureParameters(stripTex, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP); }
        rlSetTexture(0);
    }
};

}  // namespace wotm
