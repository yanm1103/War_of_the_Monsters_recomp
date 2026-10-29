// Texturas de mod em PNG (MODS.md): o mod traz so a imagem e o desenho nativo a usa no lugar da textura do jogo.
// O jogo continua carregando o .TEX/.RTX do disco (VRAM, ids e paletas intactos); so o que a janela desenha muda.
// Nome do arquivo, em qualquer pasta do mod, como o tools/tex.py export (e o tools/modkit.py) os grava:
//   <arquivo>_t<texId>.png       textura do <arquivo>.TEX       (mods/meu_mod/MON/lizard0_t0004.png)
//   <arquivo>_rtx_t<texId>.png   imagem residente do <arquivo>.RTX (retratos, telas e fontes do menu)
// texId e o do arquivo no disco (antes da relocacao da carga). Qualquer resolucao: os UVs do desenho sao normalizados (u / 4096) e
// os quadrados de tela (fonte, retratos) escalam o trecho pela razao entre o PNG e a textura original. Vale para todas as paletas
// que usam a textura. Alfa do PNG: 255 = opaco (0x80 do GS); se a textura original passa de 0x80, 255 = 0xFF (loadPng). Nao vale na imagem do GS emulado do ps2EntryRunner (a referencia da
// checagem VU1) nem na poeira composta (montada na VRAM). Particulas (Scene::modParticle) e o monstro 3D das telas (wotm_model.hpp) usam o PNG.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "raylib.h"
#include "wotm_mods.hpp"

namespace wotm::texmods {

struct Png {
    std::string host;   // arquivo no PC
    std::string mod;    // pasta do mod
    std::string rel;    // caminho no mod (para o log)
};

class Index {
public:
    static Index &get() { static Index i; return i; }

    // (ARQUIVO, rtx, texId) -> PNG. Montado na primeira consulta, depois da varredura de mods/ (Registry::scan).
    const Png *find(const std::string &stem, bool rtx, uint32_t texId) {
        build();
        const auto it = m_png.find(Key{upper(stem), rtx, texId});
        return it == m_png.end() ? nullptr : &it->second;
    }
    bool empty() { build(); return m_png.empty(); }

    // "lizard0_t0004.png" -> (LIZARD0, false, 4); "shell_rtx_t0012.png" -> (SHELL, true, 12)
    static bool parseName(const std::string &file, std::string &stem, bool &rtx, uint32_t &id) {
        std::string n = upper(file);
        if (n.size() < 7 || n.compare(n.size() - 4, 4, ".PNG") != 0) return false;
        n.resize(n.size() - 4);
        static const std::string kGray = "_SEMPALETA";   // tools/tex.py export sem a paleta achada (sai em cinza); o nome vale igual
        if (n.size() > kGray.size() && n.compare(n.size() - kGray.size(), kGray.size(), kGray) == 0) n.resize(n.size() - kGray.size());
        const size_t t = n.rfind("_T");
        if (t == std::string::npos || t == 0 || t + 2 >= n.size()) return false;
        uint32_t v = 0;
        for (size_t i = t + 2; i < n.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(n[i])) || v > 99999) return false;
            v = v * 10 + uint32_t(n[i] - '0');
        }
        stem = n.substr(0, t);
        rtx = stem.size() > 4 && stem.compare(stem.size() - 4, 4, "_RTX") == 0;
        if (rtx) stem.resize(stem.size() - 4);
        id = v;
        return !stem.empty();
    }

private:
    struct Key {
        std::string stem; bool rtx; uint32_t id;
        bool operator<(const Key &o) const { return stem != o.stem ? stem < o.stem : rtx != o.rtx ? rtx < o.rtx : id < o.id; }
    };
    std::map<Key, Png> m_png;
    bool m_built = false;

    static std::string upper(std::string s) {
        for (char &c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
        return s;
    }
    void build() {
        const mods::Registry &reg = mods::Registry::get();
        if (m_built || !reg.ready()) return;
        m_built = true;
        for (const auto &[rel, f] : reg.files()) {
            const std::string base = rel.substr(rel.find_last_of('/') + 1);
            std::string stem; bool rtx = false; uint32_t id = 0;
            if (!parseName(base, stem, rtx, id)) continue;
            Png &p = m_png[Key{stem, rtx, id}];
            if (!p.host.empty())
                std::fprintf(stderr, "[mods] png %s: %s/%s vence %s/%s\n", base.c_str(), f.mod.c_str(), rel.c_str(), p.mod.c_str(), p.rel.c_str());
            p.host = f.host.string();
            p.mod = f.mod;
            p.rel = rel;
        }
        // PNGs das skins (mods/<mod>/skins/<pasta>/): o numero do nome vale, o arquivo e o do traje da skin. Aceita o nome do
        // export do traje base ("lizard1_t0004.png", "lizard1_rtx_t0011.png") ou so "t0004.png"/"rtx_t0011.png".
        for (const mods::SkinPng &sp : reg.skinPngs()) {
            const std::string base = sp.host.filename().string();
            std::string stem; bool rtx = false; uint32_t id = 0;
            if (!parseName(base, stem, rtx, id) && !parseName("X_" + base, stem, rtx, id)) {
                std::fprintf(stderr, "[mods] png %s: nome sem _tNNNN, ignorado\n", sp.rel.c_str());
                continue;
            }
            if (upper(base).rfind("RTX_T", 0) == 0) rtx = true;
            Png &p = m_png[Key{sp.stem, rtx, id}];
            p.host = sp.host.string();
            p.mod = sp.mod;
            p.rel = sp.rel;
        }
        // Retrato dos monstros custom ("portrait" do monster.json): a imagem do .RTX do traje que eles usam
        for (const mods::Custom &cu : reg.customs()) {
            if (cu.portrait.empty() || cu.portraitTex < 0) continue;
            Png &p = m_png[Key{upper(cu.prefix + std::to_string(cu.costume)), true, uint32_t(cu.portraitTex)}];
            p.host = cu.portrait.string();
            p.mod = cu.mod;
            std::error_code ec;   // caminho no mod, para o log
            p.rel = std::filesystem::relative(cu.portrait, cu.dir.parent_path().parent_path(), ec).generic_string();
            if (ec || p.rel.empty()) p.rel = cu.portrait.filename().string();
        }
        if (!m_png.empty()) std::fprintf(stderr, "[mods] %zu textura(s) em PNG\n", m_png.size());
    }
};

// Le o PNG em RGBA (r | g << 8 | b << 16 | a << 24). rawAlpha: alfa como o GS (255 -> 0x80, resto / 2), para o que o Scene::draw
// mistura como o GS; senao, 0..255. tcc = false: so RGB (TEX0.TCC = 0), alfa sempre 1,0. Loga a primeira carga de cada PNG.
// hiAlpha: a textura original usa alfa acima de 0x80 (a maioria das do jogo vai de 0 a 0xFF; o logo do titulo e desenhado com o
// vertice em meia opacidade e conta com o 0xFF). O PNG entao vale cru: 255 = 0xFF, como o tools/tex.py export o grava (fix_alpha).
inline bool loadPng(const Png &png, const std::string &what, uint32_t origW, uint32_t origH, bool rawAlpha, bool tcc,
                    std::vector<uint32_t> &px, uint32_t &w, uint32_t &h, bool hiAlpha = false) {
    Image img = LoadImage(png.host.c_str());
    if (!img.data || img.width <= 0 || img.height <= 0) {
        std::fprintf(stderr, "[mods] png %s (%s): nao abriu\n", png.rel.c_str(), png.mod.c_str());
        if (img.data) UnloadImage(img);
        return false;
    }
    if (img.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    w = uint32_t(img.width); h = uint32_t(img.height);
    px.resize(size_t(w) * h);
    std::memcpy(px.data(), img.data, px.size() * 4);
    UnloadImage(img);
    for (uint32_t &c : px) {
        uint32_t a = tcc ? c >> 24 : (hiAlpha && rawAlpha ? 0x80 : 255);
        if (hiAlpha) { if (tcc && !rawAlpha) a = std::min(255u, a * 2); }   // cru; 0..255 com 0x80 saturado, como o gs::fixAlpha
        else if (rawAlpha) a = a >= 255 ? 0x80 : a / 2;
        c = (c & 0x00FFFFFFu) | (a << 24);
    }
    const bool ratio = uint64_t(w) * origH == uint64_t(h) * origW;
    std::fprintf(stderr, "[mods] %s <- %s/%s (%ux%u; jogo %ux%u%s)%s\n", what.c_str(), png.mod.c_str(), png.rel.c_str(), w, h, origW, origH,
                 hiAlpha ? "; alfa cru, 255 = 0xFF" : "", ratio ? "" : " AVISO: proporcao diferente da original, a imagem sai esticada");
    return true;
}

// Pacotes de um .TEX/.RTX (ja descompactado): deslocamento do pacote no arquivo -> texId do disco. Mesmo laco do tools/tex.py parse.
inline std::map<uint32_t, uint32_t> packetIds(const std::vector<uint8_t> &d) {
    std::map<uint32_t, uint32_t> out;
    if (d.size() < 8) return out;
    uint32_t headerQw = 0;
    std::memcpy(&headerQw, d.data() + 4, 4);
    size_t off = size_t(headerQw) * 16;
    for (int n = 0; off + 16 <= d.size() && n < 4096; ++n) {
        uint32_t size = 0, idTbp = 0;
        std::memcpy(&size, d.data() + off, 4);
        std::memcpy(&idTbp, d.data() + off + 8, 4);
        out[uint32_t(off)] = idTbp & 0xFFFF;
        if (!size) break;
        const size_t next = off + size_t(size >> 2) * 16;
        if (next <= off) break;
        off = next;
    }
    return out;
}

// Arquivo do disco descompactado (zip com o magic "IE\3\4", como no Scene::loadRtx); sem o magic, o proprio arquivo
inline std::vector<uint8_t> unzip(std::vector<uint8_t> raw) {
    static const uint8_t kMagic[4] = {'I', 'E', 3, 4};
    if (raw.size() < 34 || std::memcmp(raw.data(), kMagic, 4) != 0) return raw;
    uint32_t csize = 0;
    uint16_t nlen = 0, xlen = 0;
    std::memcpy(&csize, raw.data() + 18, 4);
    std::memcpy(&nlen, raw.data() + 26, 2);
    std::memcpy(&xlen, raw.data() + 28, 2);
    const size_t off = 30 + size_t(nlen) + xlen;
    if (off + csize > raw.size()) return {};
    int outSize = 0;
    unsigned char *d = DecompressData(raw.data() + off, int(csize), &outSize);
    std::vector<uint8_t> out;
    if (d && outSize > 0) out.assign(d, d + outSize);
    if (d) MemFree(d);
    return out;
}

} // namespace wotm::texmods
