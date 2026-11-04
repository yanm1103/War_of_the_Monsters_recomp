// wotm_army_files.hpp: os veiculos militares de VEGAS como um arquivo extra no formato de monstro (MON/ARMY.NGP, .PTR, .TEX, .RTX),
// para o exercito em qualquer fase (runtime/wotm_army.inc anexa o arquivo as listas do fileStatus na carga e o jogo o reloca e cria
// os pickups). Gerado do disco do usuario na primeira execucao, numa pasta de cache; nada do jogo vai para o repo.
//
// O que entra (formato em docs/FORMATS.md): as quatro faixas de VEGAS.NGP com os 17 veiculos (8 tanques, 4 caminhoes, 5 helicopteros:
// GROUP, TRANSLATE, malhas, colisao, curvas do rotor), sem nenhum ponteiro para fora delas. Raizes: um ANIM_MGR novo com o
// CHAR_INSTANCE do rotor (o de Vegas anima 90 instancias da fase) e os 17 TRANSLATE. Os ponteiros (lista 0 do VEGAS.PTR) sao
// remapeados para a imagem nova, pre-ligada em 0xA00000 como a de monstro; os texIds (77, 78, 79, 80, 87, 88) viram 1..6 e o CBP dos
// TEX0 o das 6 paletas no ARMY.RTX. Cada faixa mantem o alinhamento de 0x80 que tinha. Uma CRC confere cada faixa: disco diferente
// do NTSC-U (ou VEGAS.NGP trocado) nao gera nada.
// So host: nao depende do runtime.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "raylib.h"   // DecompressData (o zip "IE" das fases e deflate cru)
#include "wotm_disc.hpp"

namespace wotm::army_files {

constexpr uint32_t kBase = 0xA00000u;   // imagens pre-ligadas
constexpr int kVersion = 1;            // muda quando o formato gerado muda (a cache e refeita)
constexpr const char *kFiles[4] = {"ARMY.NGP", "ARMY.PTR", "ARMY.TEX", "ARMY.RTX"};

struct Range { uint32_t begin, end, crc; };
// Faixas do fecho dos 17 veiculos (deslocamentos na imagem inflada de VEGAS.NGP, 8 081 392 bytes)
inline constexpr Range kRanges[] = {{0x6E2DF0, 0x6E39F0, 0xC346E2E2u}, {0x6E3A40, 0x6E3F40, 0xC83A9A4Fu},
                                    {0x72B4F0, 0x736E80, 0xCF40A919u}, {0x746460, 0x748670, 0x6E40B0E3u}};
// TRANSLATE pai de cada GROUP de veiculo (raizes do ARMY), na ordem da imagem
inline constexpr uint32_t kTranslate[] = {0x6E2ED0, 0x6E2FD0, 0x6E30D0, 0x6E31D0, 0x6E32D0, 0x6E33D0, 0x6E34D0, 0x6E35D0, 0x6E36D0,
                                          0x6E37D0, 0x6E38D0, 0x6E39D0, 0x6E3B20, 0x6E3C20, 0x6E3D20, 0x6E3E20, 0x6E3F20};
constexpr uint32_t kRotor = 0x72C780;   // CHAR_INSTANCE do rotor do helicoptero
inline constexpr uint16_t kTexIds[] = {77, 78, 79, 80, 87, 88};          // VEGAS.TEX -> 1..6
inline constexpr uint16_t kPalettes[] = {265, 266, 267, 271, 287, 288};  // CBP no VEGAS.RTX

inline uint32_t crc32(const uint8_t *p, size_t n) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}
inline uint32_t rd32(const std::vector<uint8_t> &d, size_t o) { uint32_t v = 0; std::memcpy(&v, d.data() + o, 4); return v; }
inline uint16_t rd16(const std::vector<uint8_t> &d, size_t o) { uint16_t v = 0; std::memcpy(&v, d.data() + o, 2); return v; }
inline void wr32(std::vector<uint8_t> &d, size_t o, uint32_t v) { std::memcpy(d.data() + o, &v, 4); }
inline void wr16(std::vector<uint8_t> &d, size_t o, uint16_t v) { std::memcpy(d.data() + o, &v, 2); }

// Zip "IE\3\4" (um arquivo em deflate cru) ou o arquivo como veio
inline bool unzip(std::vector<uint8_t> &d) {
    static const uint8_t kMagic[4] = {'I', 'E', 3, 4};
    if (d.size() < 30 || std::memcmp(d.data(), kMagic, 4) != 0) return !d.empty();
    uint32_t csize = 0;
    uint16_t nlen = 0, xlen = 0;
    std::memcpy(&csize, d.data() + 18, 4);
    std::memcpy(&nlen, d.data() + 26, 2);
    std::memcpy(&xlen, d.data() + 28, 2);
    const size_t off = 30 + nlen + xlen;
    if (off + csize > d.size()) return false;
    int n = 0;
    unsigned char *u = DecompressData(d.data() + off, int(csize), &n);
    if (!u || n <= 0) return false;
    d.assign(u, u + n);
    MemFree(u);
    return true;
}

// Pacotes de um .TEX/.RTX: {inicio, fim} (o ultimo, de tamanho 0, vai ate o fim do arquivo)
struct Pkt { size_t off, end; uint16_t texId, tbp; uint32_t bits; };
inline std::vector<Pkt> packets(const std::vector<uint8_t> &d) {
    std::vector<Pkt> out;
    if (d.size() < 16) return out;
    size_t off = size_t(rd32(d, 4)) * 16;
    while (off + 16 <= d.size() && out.size() < 4096) {
        const uint32_t size = rd32(d, off);
        const size_t end = size ? off + size_t(size >> 2) * 16 : d.size();
        if (end <= off || end > d.size()) break;
        out.push_back({off, end, rd16(d, off + 8), rd16(d, off + 10), rd32(d, off + 12)});
        if (!size) break;
        off = end;
    }
    return out;
}

struct Out { std::vector<uint8_t> ngp, ptr, tex, rtx; };

// Monta os quatro arquivos a partir de VEGAS.{NGP,PTR,TEX,RTX} ja lidos (NGP, TEX e RTX inflados). false + why: nao deu.
inline bool build(const std::vector<uint8_t> &ngp, const std::vector<uint8_t> &ptrFile, const std::vector<uint8_t> &tex,
                  const std::vector<uint8_t> &rtx, Out &o, std::string &why) {
    for (const Range &r : kRanges)
        if (r.end > ngp.size() || crc32(ngp.data() + r.begin, r.end - r.begin) != r.crc) {
            char b[96];
            std::snprintf(b, sizeof b, "VEGAS.NGP diferente do esperado na faixa %#x..%#x", r.begin, r.end);
            why = b;
            return false;
        }
    // Listas do .PTR: {n, n deslocamentos} x 3
    std::vector<uint32_t> lists[3];
    {
        size_t w = 0;
        const size_t nw = ptrFile.size() / 4;
        for (auto &l : lists) {
            if (w >= nw) { why = "VEGAS.PTR curto"; return false; }
            const uint32_t n = rd32(ptrFile, 4 * w++);
            if (w + n > nw) { why = "VEGAS.PTR curto"; return false; }
            for (uint32_t i = 0; i < n; ++i) l.push_back(rd32(ptrFile, 4 * w++));
        }
    }
    // Disposicao: cabecalho (N + 18 raizes), ANIM_MGR em 0x50, depois as faixas, cada uma com o mesmo alinhamento de 0x80
    constexpr uint32_t kRoots = 1 + uint32_t(std::size(kTranslate)), kAnimMgr = 0x50;
    uint32_t newStart[std::size(kRanges)];
    uint32_t cur = kAnimMgr + 12;
    for (size_t i = 0; i < std::size(kRanges); ++i) {
        uint32_t s = (cur & ~0x7Fu) | (kRanges[i].begin & 0x7Fu);
        if (s < cur) s += 0x80;
        newStart[i] = s;
        cur = s + (kRanges[i].end - kRanges[i].begin);
    }
    o.ngp.assign((cur + 15) & ~15u, 0);
    auto rangeOf = [](uint32_t off) {
        for (size_t i = 0; i < std::size(kRanges); ++i)
            if (off >= kRanges[i].begin && off < kRanges[i].end) return int(i);
        return -1;
    };
    auto map = [&](uint32_t off) { const int i = rangeOf(off); return newStart[i] + (off - kRanges[i].begin); };
    for (size_t i = 0; i < std::size(kRanges); ++i)
        std::memcpy(o.ngp.data() + newStart[i], ngp.data() + kRanges[i].begin, kRanges[i].end - kRanges[i].begin);
    wr32(o.ngp, 0, kRoots);
    wr32(o.ngp, 4, kBase + kAnimMgr);
    for (size_t i = 0; i < std::size(kTranslate); ++i) wr32(o.ngp, 8 + 4 * i, kBase + map(kTranslate[i]));
    wr32(o.ngp, kAnimMgr, 7u);                    // op ANIM_MGR
    wr32(o.ngp, kAnimMgr + 4, 1u);                // numCharInstances
    wr32(o.ngp, kAnimMgr + 8, kBase + map(kRotor));
    // Lista 0: ponteiros dentro das faixas, remapeados (os bits altos, se houver, ficam)
    std::vector<uint32_t> out0{kAnimMgr + 8}, out1, out2;
    for (const uint32_t off : lists[0]) {
        if (rangeOf(off) < 0) continue;
        const uint32_t v = rd32(ngp, off), t = (v & 0x0FFFFFFFu) - kBase;
        if ((v & 0x0FFFFFFFu) < kBase || rangeOf(t) < 0) {
            char b[96];
            std::snprintf(b, sizeof b, "ponteiro em %#x sai das faixas (%#x)", off, v);
            why = b;
            return false;
        }
        wr32(o.ngp, map(off), (v & 0xF0000000u) | (kBase + map(t)));
        out0.push_back(map(off));
    }
    // Lista 1: texIds de 16 bits
    for (const uint32_t off : lists[1]) {
        if (!off || rangeOf(off) < 0) continue;
        const uint16_t id = rd16(ngp, off);
        if (!id) continue;
        const auto it = std::find(std::begin(kTexIds), std::end(kTexIds), id);
        if (it == std::end(kTexIds)) { why = "texId inesperado " + std::to_string(id); return false; }
        wr16(o.ngp, map(off), uint16_t(1 + (it - std::begin(kTexIds))));
        out1.push_back(map(off));
    }
    // Paletas do ARMY.RTX em sequencia (8x2 = 1 bloco, 16x16 = 4)
    std::vector<Pkt> rp = packets(rtx);
    uint16_t newTbp[std::size(kPalettes)];
    const Pkt *palPkt[std::size(kPalettes)] = {};
    {
        uint16_t next = 0;
        for (size_t i = 0; i < std::size(kPalettes); ++i) {
            for (const Pkt &p : rp)
                if ((p.bits & 0x3F) == 0 && p.tbp == kPalettes[i] && p.end - p.off > 16) { palPkt[i] = &p; break; }
            if (!palPkt[i]) { why = "paleta " + std::to_string(kPalettes[i]) + " nao achada no VEGAS.RTX"; return false; }
            newTbp[i] = next;
            const uint32_t tw = (palPkt[i]->bits >> 8) & 15, th = (palPkt[i]->bits >> 12) & 15;
            next = uint16_t(next + std::max(1u, ((1u << tw) << th) * 4u / 256u));
        }
    }
    // Lista 2: TEX0 (CBP nos bits 5..18 da palavra alta)
    for (const uint32_t off : lists[2]) {
        if (!off || rangeOf(off) < 0) continue;
        const uint32_t hi = rd32(ngp, off + 4), cbp = (hi >> 5) & 0x3FFF;
        const auto it = std::find(std::begin(kPalettes), std::end(kPalettes), cbp);
        if (it == std::end(kPalettes)) { why = "CBP inesperado " + std::to_string(cbp); return false; }
        wr32(o.ngp, map(off) + 4, (hi & ~(0x3FFFu << 5)) | (uint32_t(newTbp[it - std::begin(kPalettes)]) << 5));
        out2.push_back(map(off));
    }
    o.ptr.clear();
    for (const auto *l : {&out0, &out1, &out2}) {
        const size_t at = o.ptr.size();
        o.ptr.resize(at + 4 * (1 + l->size()));
        wr32(o.ptr, at, uint32_t(l->size()));
        for (size_t i = 0; i < l->size(); ++i) wr32(o.ptr, at + 4 + 4 * i, (*l)[i]);
    }
    // ARMY.RTX: as 6 paletas, tbp novo, a ultima com tamanho 0
    o.rtx.clear();
    for (size_t i = 0; i < std::size(kPalettes); ++i) {
        const Pkt &p = *palPkt[i];
        const size_t at = o.rtx.size(), n = p.end - p.off;
        o.rtx.insert(o.rtx.end(), rtx.begin() + long(p.off), rtx.begin() + long(p.end));
        wr32(o.rtx, at, i + 1 < std::size(kPalettes) ? uint32_t(n / 16 * 4) : 0u);
        wr16(o.rtx, at + 10, newTbp[i]);
    }
    // ARMY.TEX: cabecalho de 0x80 (faixas de tamanho de bloco das 6, na ordem em que aparecem) e os 6 pacotes com texId 1..6
    std::vector<Pkt> tp = packets(tex);
    if (tex.size() < 16) { why = "VEGAS.TEX vazio"; return false; }
    const uint32_t vegasSizes = rd32(tex, 0);
    std::vector<std::pair<uint32_t, uint32_t>> sizes;   // {count, blkSize}
    o.tex.assign(0x80, 0);
    for (size_t i = 0; i < std::size(kTexIds); ++i) {
        const auto p = std::find_if(tp.begin(), tp.end(), [&](const Pkt &q) { return q.texId == kTexIds[i]; });
        if (p == tp.end()) { why = "textura " + std::to_string(kTexIds[i]) + " nao achada no VEGAS.TEX"; return false; }
        const uint32_t bin = p->bits >> 27;
        if (bin >= vegasSizes || 12 + 8 * bin >= tex.size()) { why = "faixa de tamanho invalida no VEGAS.TEX"; return false; }
        const uint32_t blk = rd32(tex, 12 + 8 * bin);
        auto s = std::find_if(sizes.begin(), sizes.end(), [&](const auto &e) { return e.second == blk; });
        if (s == sizes.end()) s = sizes.insert(sizes.end(), {0u, blk});
        ++s->first;
        const size_t at = o.tex.size(), n = p->end - p->off;
        o.tex.insert(o.tex.end(), tex.begin() + long(p->off), tex.begin() + long(p->end));
        wr32(o.tex, at, i + 1 < std::size(kTexIds) ? uint32_t(n / 16 * 4) : 0u);
        wr16(o.tex, at + 8, uint16_t(i + 1));
        wr32(o.tex, at + 12, (p->bits & 0x07FFFFFFu) | (uint32_t(s - sizes.begin()) << 27));
    }
    wr32(o.tex, 0, uint32_t(sizes.size()));
    wr32(o.tex, 4, 8u);   // 8 qwords de cabecalho, como os de MON/
    for (size_t i = 0; i < sizes.size(); ++i) {
        wr32(o.tex, 8 + 8 * i, sizes[i].first);
        wr32(o.tex, 12 + 8 * i, sizes[i].second);
    }
    return true;
}

// Gera (ou acha pronta) a cache em dir: true quando os quatro arquivos estao la. disc/iso: a pasta do disco extraido e a ISO.
inline bool ensure(const std::filesystem::path &dir, const std::filesystem::path &disc, const std::string &iso, std::string &log) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path ver = dir / "ARMY.VER";
    {
        std::ifstream f(ver);
        int v = 0;
        bool all = f >> v && v == kVersion;
        for (const char *n : kFiles) all = all && fs::is_regular_file(dir / n, ec) && fs::file_size(dir / n, ec) > 0;
        if (all) { log = "cache pronta"; return true; }
    }
    std::vector<uint8_t> in[4];
    const char *src[4] = {"LVL/VEGAS.NGP", "LVL/VEGAS.PTR", "LVL/VEGAS.TEX", "LVL/VEGAS.RTX"};
    for (int i = 0; i < 4; ++i)
        if (!disc::readFile(disc, iso, src[i], in[i]) || !unzip(in[i])) { log = std::string(src[i]) + " nao lido"; return false; }
    Out o;
    std::string why;
    if (!build(in[0], in[1], in[2], in[3], o, why)) { log = why; return false; }
    fs::create_directories(dir, ec);
    const std::vector<uint8_t> *data[4] = {&o.ngp, &o.ptr, &o.tex, &o.rtx};
    for (int i = 0; i < 4; ++i) {
        std::ofstream f(dir / kFiles[i], std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char *>(data[i]->data()), std::streamsize(data[i]->size()));
        if (!f) { log = "nao gravou " + (dir / kFiles[i]).string(); return false; }
    }
    std::ofstream(ver) << kVersion << "\n";
    char b[160];
    std::snprintf(b, sizeof b, "gerada de LVL/VEGAS.* (NGP %zu, PTR %zu, TEX %zu, RTX %zu bytes)", o.ngp.size(), o.ptr.size(), o.tex.size(),
                  o.rtx.size());
    log = b;
    return true;
}

} // namespace wotm::army_files
