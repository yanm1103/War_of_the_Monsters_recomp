// Imagens da lista de saves (NATIVE.md, "Save em JSON"): a esquerda do modal, a foto larga da fase do save e, por cima, o logo do
// monstro, como na escolha de fase e de monstro do menu. Saem do SHELL/SHELL.TEX e SHELL/SHELL.RTX do disco (o mesmo par que o menu
// carrega; depois da luta ele nao esta na memoria, por isso a leitura e direta do disco).
//
// - Logos (256x128, T8): texIds 121..129 e 52 (Raptros). Fotos largas (512x256): dois pedacos T8 de 256x256 lado a lado; cada fase
//   tem tambem uma foto quadrada no pedaco anterior, que aqui nao e usada.
// - Paletas: entradas CT32 16x16 do .RTX, alocadas na ordem das texturas (4 blocos por T8, 1 por T4); o "tbp" da tabela e o da entrada
//   no arquivo. Conferido no tools/tex.py: as do arquivo que o .NGP liga seguem a mesma ordem; as daqui o .NGP nao liga (o menu as troca
//   por switch).
// - As linhas da textura estao de baixo para cima (o jogo desenha com V invertido).
//
// O modal (wotm_save.inc, thread do EE) posta a lista a cada quadro (post); a janela desenha entre a lista de alta prioridade (a
// moldura do McBackground) e o texto (draw, em wotm_live.inc). Sem post recente (pagina fechada) nao desenha nada.
// WOTM_SAVEPICS_NO_DRAW antes do include: so a fila, sem o raylib (wotm_overrides.cpp).
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace wotm::savepics {

enum Kind : uint8_t { kLevel = 0, kLogo = 1, kMonster = 2 };

// Em coordenadas da tela do jogo (640 x 224, a faixa 4:3 da janela)
struct Pic {
    Kind kind;
    uint32_t id;   // fase (kLevels do wotm_save.inc), monstro (sel >> 5) ou, em kMonster, monstro | traje << 8
    float x0, y0, x1, y1;
    uint8_t a;     // 255 = opaco
};

struct Tile {
    uint16_t texId, palTbp;
};
// Fase -> foto larga (esquerda, direita). Indices de kLevels (Shell::m_levelNum): 1 central .. 11 capitol, 14 minisf, 15 volcano
// (as 12, 13 e 16 nao tem foto no SHELL.TEX: a selecao de fase mostra a de outra; wotm_levelsel.inc).
struct LevelPic {
    uint32_t level;
    Tile l, r;
};
constexpr LevelPic kLevelPics[] = {
    {1, {99, 308}, {100, 312}},    // Midtown Park
    {2, {118, 384}, {119, 388}},   // Gamblers Gulch
    {3, {102, 320}, {103, 324}},   // Rosdale Canyon
    {4, {108, 344}, {109, 348}},   // Metro City
    {5, {93, 284}, {94, 288}},     // Century Airfield
    {6, {84, 248}, {85, 252}},     // Atomic Island
    {7, {111, 356}, {112, 360}},   // Baytown
    {8, {105, 332}, {106, 336}},   // Club Caldera
    {9, {114, 368}, {115, 372}},   // Tsunopolis
    {10, {88, 264}, {89, 268}},    // UFO
    {11, {96, 296}, {97, 300}},    // Capitol
    {14, {86, 256}, {87, 260}},    // Mini Baytown
    {15, {90, 272}, {91, 276}},    // Volcano
};
// Monstro (indice de kMonsters) -> logo
struct LogoPic {
    uint32_t monster;
    Tile t;
};
constexpr LogoPic kLogoPics[] = {
    {1, {122, 400}},   // Congar
    {2, {127, 420}},   // Robo-47
    {3, {129, 428}},   // Togera
    {4, {128, 424}},   // Ultra-V
    {5, {126, 416}},   // Preytor
    {7, {123, 404}},   // Kineticlops
    {8, {125, 412}},   // Magmo
    {9, {52, 156}},    // Raptros
    {10, {124, 408}},  // Agamo
    {11, {121, 396}},  // Zorgulon
};

inline bool hasLevel(uint32_t level) {
    for (const LevelPic &p : kLevelPics)
        if (p.level == level) return true;
    return false;
}
inline bool hasLogo(uint32_t monster) {
    for (const LogoPic &p : kLogoPics)
        if (p.monster == monster) return true;
    return false;
}
// Monstro (indice de kMonsters) -> prefixo do arquivo em MON/ (MONKEY0.NGP...: o numero e o traje, Shell::m_costume)
inline const char *monsterFile(uint32_t monster) {
    static const char *const k[] = {nullptr, "MONKEY", "ROBOT", "LIZARD", "SHOGUN", "MANTIS", nullptr, "ENERGY", "LAVA", "DRAGON", "ROCK", "ALIEN",
                                           nullptr, "ASSBOSS", "JELLY", "FINAL"};   // 13 a 15: chefes, base de monstro custom (wotm_mods.hpp)
    return monster < std::size(k) ? k[monster] : nullptr;
}

// ---- fila do modal (thread do EE) para a janela ----
struct Channel {
    std::mutex m;
    std::vector<Pic> pics;
    std::chrono::steady_clock::time_point at{};
};
inline Channel &channel() {
    static Channel c;
    return c;
}
inline void post(std::vector<Pic> pics) {
    Channel &c = channel();
    std::lock_guard<std::mutex> lk(c.m);
    c.pics = std::move(pics);
    c.at = std::chrono::steady_clock::now();
}
inline void clear() {
    Channel &c = channel();
    std::lock_guard<std::mutex> lk(c.m);
    c.pics.clear();
}

} // namespace wotm::savepics

#ifndef WOTM_SAVEPICS_NO_DRAW
#include "wotm_disc.hpp"
#include "wotm_model.hpp"   // monstro 3D (kMonster)
#include "raylib.h"
#include <map>
#include <memory>

namespace wotm::savepics {

// ---- leitura do disco (thread da janela) ----
namespace detail {
// .TEX/.RTX num zip de um arquivo so com o magic "IE\3\4" (zipInflateAll)
inline bool unzipIe(std::vector<uint8_t> &d) { return model::unzipIe(d); }
struct Packet {
    size_t off;
    uint16_t texId, tbp;
    uint32_t psm, w, h;
};
// Cabecalho _texfile {numSizes, headerQw, ...}; pacotes de 16 bytes {size, pad, texId, tbp, bits} (tools/tex.py, parse)
inline std::vector<Packet> packets(const std::vector<uint8_t> &d) {
    std::vector<Packet> out;
    if (d.size() < 8) return out;
    uint32_t headerQw = 0;
    std::memcpy(&headerQw, d.data() + 4, 4);
    size_t off = size_t(headerQw) * 16;
    while (off + 16 <= d.size() && out.size() < 4096) {
        uint32_t size, bits;
        uint16_t texId, tbp;
        std::memcpy(&size, d.data() + off, 4);
        std::memcpy(&texId, d.data() + off + 8, 2);
        std::memcpy(&tbp, d.data() + off + 10, 2);
        std::memcpy(&bits, d.data() + off + 12, 4);
        out.push_back(Packet{off, texId, tbp, bits & 0x3F, 1u << ((bits >> 8) & 15), 1u << ((bits >> 12) & 15)});
        if (size == 0) break;
        const size_t next = off + size_t(size >> 2) * 16;
        if (next <= off || next > d.size()) break;
        off = next;
    }
    return out;
}
} // namespace detail

class Drawer {
public:
    // Thread da janela, FORA do BeginTextureMode do quadro (antes dele): desenha o monstro 3D da lista (kMonster) no RenderTexture
    // dele, animado no idle desde que a linha foi escolhida. Mesmos argumentos do draw.
    void prepare(const std::filesystem::path &discRoot, const std::string &iso, float w, float rh) {
        const Pic *mp = nullptr, *lp = nullptr;
        Pic pic{}, lev{};
        {
            Channel &c = channel();
            std::lock_guard<std::mutex> lk(c.m);
            if (!c.pics.empty() && std::chrono::steady_clock::now() - c.at <= std::chrono::milliseconds(250))
                for (const Pic &p : c.pics) {
                    if (p.kind == kMonster) { pic = p; mp = &pic; }
                    if (p.kind == kLevel) { lev = p; lp = &lev; }
                }
        }
        monReady_ = false;
        if (!mp) return;
        if (!model_ || monId_ != pic.id) {
            monId_ = pic.id;
            monT0_ = GetTime();
            model_.reset();
            const char *pre = monsterFile(pic.id & 0xFF);
            // traje (4+ = skin de mod: MON/<PREFIXO><n>.* vem de mods/, wotm_mods.hpp) e o traje do disco de reserva (o base da skin)
            const uint32_t costume = (pic.id >> 8) & 0xFF, base = (pic.id >> 16) & 3;
            if (pre) {
                auto m = std::make_unique<model::Model>();
                if (m->load(discRoot, iso, pre + std::to_string(costume)) || m->load(discRoot, iso, pre + std::to_string(base)) ||
                    m->load(discRoot, iso, std::string(pre) + "0"))
                    model_ = std::move(m), model_->computeFraming(model::kIdle);   // caixa fixa: a camera nao acompanha o idle
                else
                    std::fprintf(stderr, "[wotm] lista de saves: MON/%s%u.NGP nao achado; sem monstro 3D\n", pre, costume);
            }
        }
        if (!model_) return;
        const float frame = float((GetTime() - monT0_) * 60.0);
        if (!model_->pose(model::kIdle, frame)) model_->pose(-1, 0.f);
        // 2x o tamanho na janela (o DrawTexturePro reduz com filtro bilinear: bordas suaves)
        const float sx = w / 640.f, sy = rh / 224.f;
        const int W = std::clamp(int((pic.x1 - pic.x0) * sx * 2.f), 16, 1536), H = std::clamp(int((pic.y1 - pic.y0) * sy * 2.f), 16, 1536);
        // Fundo: o pedaco da foto da fase sob a caixa do monstro, opaco (o fade vem do tom no draw), para as misturas do GS que agem
        // sobre o destino (escurecer, subtrair) terem o que escurecer. Fora da foto o fundo fica transparente.
        const Texture2D *lt = nullptr;
        if (lp && load(discRoot, iso))
            for (size_t i = 0; i < std::size(kLevelPics); ++i)
                if (kLevelPics[i].level == lp->id && level_[i].id) lt = &level_[i];
        const float kx = float(W) / (pic.x1 - pic.x0), ky = float(H) / (pic.y1 - pic.y0);
        if (!fxTried_) {   // fogo do Agamo e lens flare: texturas de particula da fase (LVL/CENTRAL), uma vez
            fxTried_ = true;
            if (!fx_.load(discRoot, iso)) std::fprintf(stderr, "[wotm] lista de saves: LVL/CENTRAL.TEX/.RTX nao achado; sem fogo nem lens flare\n");
            renderer_.setFx(&fx_);
        }
        const Framing fr = framingOf((monId_ & 0xFF) | ((monId_ >> 16) & 3) << 8);   // enquadramento do traje do disco (o base)
        renderer_.shiftX = fr.shiftX;
        renderer_.shiftY = fr.shiftY;
        renderer_.fwd = fr.fwd;
        renderer_.render(*model_, W, H, kYaw, kPitch, fr.zoom, [&] {
            if (!lt) return;
            DrawTexturePro(*lt, Rectangle{0.f, 0.f, float(lt->width), float(lt->height)},
                           Rectangle{(lev.x0 - pic.x0) * kx, (lev.y0 - pic.y0) * ky, (lev.x1 - lev.x0) * kx, (lev.y1 - lev.y0) * ky},
                           Vector2{0.f, 0.f}, 0.f, WHITE);
        });
        monReady_ = true;
    }

    // Thread da janela, dentro do BeginTextureMode do quadro nativo. `x0`/`w`: a faixa 4:3 da tela do jogo em pixels (gsScreenRect);
    // `rh`: altura do quadro.
    void draw(const std::filesystem::path &discRoot, const std::string &iso, float x0, float w, float rh) {
        std::vector<Pic> pics;
        {
            Channel &c = channel();
            std::lock_guard<std::mutex> lk(c.m);
            if (c.pics.empty() || std::chrono::steady_clock::now() - c.at > std::chrono::milliseconds(250)) return;
            pics = c.pics;
        }
        if (!load(discRoot, iso)) return;
        const float sx = w / 640.f, sy = rh / 224.f;
        for (const Pic &p : pics) {
            const Texture2D *t = nullptr;
            if (p.kind == kMonster) {
                if (!monReady_) continue;
                // cor pre-multiplicada (Renderer): o fade escala cor e alfa juntos
                const Texture2D &mt = renderer_.target().texture;
                BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
                DrawTexturePro(mt, Rectangle{0.f, 0.f, float(mt.width), -float(mt.height)},
                               Rectangle{x0 + p.x0 * sx, p.y0 * sy, (p.x1 - p.x0) * sx, (p.y1 - p.y0) * sy}, Vector2{0.f, 0.f}, 0.f,
                               Color{p.a, p.a, p.a, p.a});
                EndBlendMode();
                continue;
            }
            if (p.kind == kLevel) {
                for (size_t i = 0; i < std::size(kLevelPics); ++i)
                    if (kLevelPics[i].level == p.id) t = &level_[i];
            } else {
                for (size_t i = 0; i < std::size(kLogoPics); ++i)
                    if (kLogoPics[i].monster == p.id) t = &logo_[i];
            }
            if (!t || !t->id) continue;
            DrawTexturePro(*t, Rectangle{0.f, 0.f, float(t->width), float(t->height)},
                           Rectangle{x0 + p.x0 * sx, p.y0 * sy, (p.x1 - p.x0) * sx, (p.y1 - p.y0) * sy}, Vector2{0.f, 0.f}, 0.f,
                           Color{255, 255, 255, p.a});
        }
    }

    // Foto larga da fase (512x256, de cima para baixo) ou nullptr; o mosaico da RANDOM da selecao de fase (wotm_levelsel.inc)
    const Texture2D *levelTexture(const std::filesystem::path &discRoot, const std::string &iso, uint32_t level) {
        if (!load(discRoot, iso)) return nullptr;
        for (size_t i = 0; i < std::size(kLevelPics); ++i)
            if (kLevelPics[i].level == level && level_[i].id) return &level_[i];
        return nullptr;
    }

private:
    bool load(const std::filesystem::path &discRoot, const std::string &iso) {
        if (tried_) return ok_;
        tried_ = true;
        std::vector<uint8_t> tex, rtx;
        if (!disc::readFile(discRoot, iso, "SHELL/SHELL.TEX", tex) || !disc::readFile(discRoot, iso, "SHELL/SHELL.RTX", rtx) ||
            !detail::unzipIe(tex) || !detail::unzipIe(rtx)) {
            std::fprintf(stderr, "[wotm] lista de saves: SHELL/SHELL.TEX ou .RTX nao achado; sem logos nem fotos das fases\n");
            return false;
        }
        texPk_ = detail::packets(tex);
        rtxPk_ = detail::packets(rtx);
        int n = 0;
        for (size_t i = 0; i < std::size(kLevelPics); ++i) {
            std::vector<uint32_t> px(512 * 256, 0);
            const bool a = tile(tex, rtx, kLevelPics[i].l, px, 512, 0), b = tile(tex, rtx, kLevelPics[i].r, px, 512, 256);
            if (a && b) level_[i] = upload(px, 512, 256), ++n;
        }
        for (size_t i = 0; i < std::size(kLogoPics); ++i) {
            std::vector<uint32_t> px(256 * 128, 0);
            if (tile(tex, rtx, kLogoPics[i].t, px, 256, 0)) logo_[i] = upload(px, 256, 128), ++n;
        }
        std::fprintf(stderr, "[wotm] lista de saves: %d de %zu imagens do SHELL.TEX\n", n, std::size(kLevelPics) + std::size(kLogoPics));
        ok_ = n > 0;
        return ok_;
    }

    // Um pedaco T8 com a paleta do .RTX, em `px` (linhas de `stride` pixels) a partir da coluna `col`, ja de cima para baixo
    bool tile(const std::vector<uint8_t> &tex, const std::vector<uint8_t> &rtx, Tile t, std::vector<uint32_t> &px, uint32_t stride, uint32_t col) {
        const detail::Packet *p = nullptr, *pal = nullptr;
        for (const detail::Packet &k : texPk_)
            if (k.texId == t.texId) p = &k;
        for (const detail::Packet &k : rtxPk_)
            if (k.tbp == t.palTbp && k.psm == 0 && k.w == 16 && k.h == 16) pal = &k;
        if (!p || !pal || p->psm != 0x13 || p->off + 0x80 + size_t(p->w) * p->h > tex.size() || pal->off + 0x10 + 1024 > rtx.size())
            return false;
        uint32_t clut[256];
        for (uint32_t i = 0; i < 256; ++i) {
            const uint32_t j = (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1);   // CSM1: bits 3 e 4 trocados
            uint32_t c;
            std::memcpy(&c, rtx.data() + pal->off + 0x10 + 4 * j, 4);
            const uint32_t a = c >> 24;
            clut[i] = (c & 0xFFFFFFu) | (a >= 0x80 ? 255u : a * 2u) << 24;   // alfa do GS: 0x80 = 1,0
        }
        const uint8_t *src = tex.data() + p->off + 0x80;
        for (uint32_t y = 0; y < p->h; ++y)
            for (uint32_t x = 0; x < p->w && col + x < stride; ++x)
                px[size_t(p->h - 1 - y) * stride + col + x] = clut[src[size_t(y) * p->w + x]];
        return true;
    }

    static Texture2D upload(std::vector<uint32_t> &px, int w, int h) {
        Image img{px.data(), w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        Texture2D t = LoadTextureFromImage(img);
        SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(t, TEXTURE_WRAP_CLAMP);
        return t;
    }

    std::vector<detail::Packet> texPk_, rtxPk_;
    Texture2D level_[std::size(kLevelPics)]{}, logo_[std::size(kLogoPics)]{};
    bool tried_ = false, ok_ = false;

public:   // enquadramento tambem usado pela selecao de monstros de 3 e 4 jogadores (wotm_padsel.inc)
    // Monstro 3D: virado 3/4 para a direita da tela (yaw 0 = de frente), camera um pouco acima
    static constexpr float kYaw = 35.f, kPitch = 8.f;
    // Correcao do enquadramento por monstro e traje (costume 0-3; -1 = todos). O automatico usa a maior medida da caixa, entao a cauda
    // do Togera (3) e as asas do Raptros (9) deixavam os dois mais longe; a cauda do Togera tambem tira o centro da caixa do corpo:
    // `fwd` anda o centro para a frente do monstro (model::Renderer::fwd), o que vale em qualquer angulo e proporcao de quadro.
    // O fogo do Magmo traje 3 (o quarto, contando de 1) e o da cabeca do Agamo (10) aumentam a caixa: aproxima e sobe um pouco para os
    // pes nao sairem por baixo. shiftX/shiftY em fracao do quadro (+ = direita/cima).
    struct Framing {
        uint32_t mon;
        int costume;
        float zoom, shiftX, shiftY, fwd = 0.f;
    };
    static constexpr Framing kFraming[] = {
        {3, -1, 1.35f, 0.f, 0.f, 0.18f},   // Togera
        {8, 3, 1.25f, 0.f, 0.08f},         // Magmo, traje de fogo
        {9, -1, 1.6f, 0.f, 0.f},           // Raptros
        {10, -1, 1.15f, 0.f, 0.04f},       // Agamo
    };
    static Framing framingOf(uint32_t id) {
        const uint32_t mon = id & 0xFF;
        const int costume = int((id >> 8) & 3);
        for (const Framing &f : kFraming)
            if (f.mon == mon && (f.costume < 0 || f.costume == costume)) return f;
        return Framing{mon, -1, 1.f, 0.f, 0.f};
    }

private:
    std::unique_ptr<model::Model> model_;
    model::Renderer renderer_;
    model::FxTextures fx_;
    bool fxTried_ = false;
    uint32_t monId_ = ~0u;
    double monT0_ = 0.0;
    bool monReady_ = false;
};

inline Drawer &drawer() {
    static Drawer d;
    return d;
}

} // namespace wotm::savepics
#endif
