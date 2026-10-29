// Camada nativa do WotM: le a cena direto da RAM do EE e desenha com raylib (sem VU1/GS).
//
// Usada de dois jeitos: pelo runtime recompilado (wotm_scene.inc, a cada quadro, sobre a RDRAM viva)
// e pelo visualizador standalone (native/viewer.cpp, sobre um dump `ram_<quadro>.bin`).
// Formatos: docs/FORMATOS.md. Este arquivo nao contem dados do jogo.
//
// Convencoes do jogo (PS2): vetores-linha (p' = p * M), mundo com Z para cima, float32 little-endian.
#pragma once
#include "wotm_syms.h"   // wotm::sym / wotm::fn: enderecos do ELF por nome (tools/native_syms.py --gen)

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cctype>
#include <map>
#include <set>
#include <cstring>
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#if defined(_M_X64) || defined(__SSE4_1__)
#include <smmintrin.h>   // dustTexture: rasterizacao com SSE4.1
#endif

#include "raylib.h"
#include "rlgl.h"
#include "wotm_gs.hpp"
#include "wotm_mods.hpp"   // paletas dos .RTX: mods/ vence o disco (loadResources)
#include "wotm_texmods.hpp"   // texturas de mod em PNG (textureId, residentTexture)

namespace wotm {

// ---------------------------------------------------------------- RAM

struct Ram {
    const uint8_t *p = nullptr;
    size_t size = 0;

    bool ok(uint32_t a, uint32_t n = 4) const { return p && a >= 0x1000 && uint64_t(a) + n <= size; }
    uint32_t u32(uint32_t a) const { uint32_t v = 0; if (ok(a)) std::memcpy(&v, p + a, 4); return v; }
    uint16_t u16(uint32_t a) const { uint16_t v = 0; if (ok(a, 2)) std::memcpy(&v, p + a, 2); return v; }
    int16_t s16(uint32_t a) const { return int16_t(u16(a)); }
    uint8_t u8(uint32_t a) const { return ok(a, 1) ? p[a] : 0; }
    int8_t s8(uint32_t a) const { return int8_t(u8(a)); }
    float f32(uint32_t a) const { float v = 0; if (ok(a)) std::memcpy(&v, p + a, 4); return v; }
};

// Enderecos fixos do retail NTSC-U (SCUS_971.97): symbol_addrs.txt
namespace addr {
constexpr uint32_t world = wotm::sym::world;            // _worldctx *world (ep@0, skyCs@4, eo@0x20, weMat@0x30)
constexpr uint32_t worldCtx = wotm::sym::worldCtx;         // _worldctx worldCtx[5] (0x1F0 cada)
constexpr uint32_t g_doFOVGroups = wotm::sym::g_doFOVGroups; // int: o hierTraverseAsm corta GROUP por esfera (Scene::outOfView)
constexpr uint32_t viewInfo = wotm::sym::viewInfo;         // 5 x 0x14: [0] = _cs* da camera, [1] = viewport*
constexpr uint32_t worldToScreenMat = wotm::sym::worldToScreenMat; // 5 x 0x40
constexpr uint32_t paraL = wotm::sym::paraL;            // float[4][4]: linhas x, y, z; colunas = luzes paralelas 0-2 e vetor-meio do especular, x32
constexpr uint32_t vu1FogParams = 0x0025B280;     // float[4] (D_0025B280, qword 21 do VU1): fogFA, fogMaxVal, 1, fogFB (fogUpdate)
constexpr uint32_t fogColorCtx = wotm::sym::fogColorCtx;      // u64[6]: pacote do FOGCOL que o fogUpdate manda ao GS; [4] = R | G << 8 | B << 16
constexpr uint32_t vu1LightColor = 0x0025B238;    // float[4][4] (D_0025B238): cor/128 das luzes 0-2 (linhas); a linha 3 e o ambiente/especular
constexpr uint32_t fieldsAtFrameStart = 0x006F8DC0;   // u32 (D_006F8DC0): contagem de fields no inicio do quadro (timerWaitForMinUpdateRate)
constexpr uint32_t csActiveList = wotm::sym::x_6CsPool_m_activeList;     // CsPool::m_activeList (CsNode: cs@0, next@4)
constexpr uint32_t csHPActiveList = wotm::sym::x_6CsPool_m_HPActiveList;   // CsPool::m_HPActiveList
constexpr uint32_t tempVramTexAddr = wotm::sym::tempVramTexAddr;  // fim dos recursos (.RTX) na VRAM, em palavras; >> 6 = bloco base das paletas
constexpr uint32_t fileStatus = wotm::sym::fileStatus;      // FileStatus: resAddr@0x78, resLoaded@0xB0, names[14][9]@0xB1, maxTexAddr[14]@0x14C, maxResAddr[14]@0x168
constexpr uint32_t texInfo = wotm::sym::texInfo;         // texInfo[1000] x 16: [0] = pacote de upload da textura (RAM), [4] = residencia/VRAM
constexpr uint32_t fontPacket = 0x00735900;      // pacote GIF do texto do quadro (fontBuildPrim/fontDmaFontData): cabecalho A+D, depois corridas de sprites
constexpr uint32_t psBlkLst = wotm::sym::psBlkLst;        // _pshadowblocker[10] x 0x30: cs@0x10, cs2@0x18, &origem@0x1C, &raio@0x20, fator@0x24, emUso u16@0x2E
constexpr uint32_t psLitDirWs = wotm::sym::psLitDirWs;      // direcao da luz das sombras (da cena para a luz), mundo
constexpr uint32_t psShadowIntens = wotm::sym::psShadowIntens;  // float, 80 = 0,625 (escala do GS)
constexpr uint32_t psShadowOn = 0x006F8D7C;      // psSetShadowOnOff
}

// Vista do jogo que a coleta e o desenho em curso seguem (tela dividida: vistas 0 e 1, metades esquerda e direita de 320 x 224 do
// GS; src/common/view.cpp; com 3 e 4 jogadores, vistas 0 a 3 em quadrantes de 320 x 112, wotm_split4.inc). Por thread: a coleta roda na thread do EE e o desenho na da janela, cada um liga a sua com ViewScope. Sem
// ViewScope vale a vista 0 com a tela cheia (640 x 224, centro 2048), como era antes da tela dividida. cx/cy/w/h: centro e tamanho
// da vista em pixels do GS (viewDef da vista: cx/cy em +0x18/+0x1C, w/h em +4/+6).
struct ViewCtx {
    int view = 0;
    float cx = 2048.f, cy = 2048.f, w = 640.f, h = 224.f;
    // Proporcao com que a vista aparece na tela (640 x 224 = 4:3; a metade 320 x 224 = 2:3)
    float aspect() const { return (w / 640.f) * (4.f / 3.f) / (h / 224.f); }
    bool full() const { return w >= 640.f; }
};
inline ViewCtx &curView() { static thread_local ViewCtx v; return v; }
struct ViewScope {
    ViewCtx prev;
    explicit ViewScope(const ViewCtx &v) : prev(curView()) { curView() = v; }
    ~ViewScope() { curView() = prev; }
    ViewScope(const ViewScope &) = delete;
    ViewScope &operator=(const ViewScope &) = delete;
};

// Medida do desenho (WOTM_DRAWSTATS=1, com PS2X_STATS): tempo de cada fase do Scene::draw, lotes descarregados e vertices emitidos,
// somados na thread da janela; o wotm_live.inc imprime a media por vista desenhada na linha [native] e zera
struct DrawProf {
    enum Phase { Sky, Trav, AlphaOT, Shadows, ShadowTex, Sprites, Fx, Hud, Flush, Jobs,   // Flush e Jobs (vertices nas threads auxiliares): dentro das outras
                 RShow, RView, RFlares, RFrame, kN };   // por redesenho (wotmDrawNative): Scene::show, drawView inteiro, flares adiados, o redesenho todo
    long redraws = 0;
    double ms[kN] = {};
    long batches = 0, verts = 0, views = 0;   // batches: descargas (uma subida de vertices cada)
    long draws = 0;                           // desenhos (glDrawArrays) das descargas do VtxStream
    long shadowHit = 0, shadowMiss = 0;   // silhuetas reaproveitadas e refeitas (Scene::shadowTexture)
    long emitted = 0;                     // vertices de fato emitidos pelo drawList (depois do corte de faces e dos slots)
    long resident = 0, resUp = 0;         // vertices desenhados da arena residente e subidos para ela (Scene::residentDraw)
};
inline DrawProf &drawProf() { static DrawProf p; return p; }
inline bool drawProfOn() { static const bool on = std::getenv("WOTM_DRAWSTATS") != nullptr; return on; }
inline double profNowMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
// Soma o tempo do escopo na fase `ph` (so com WOTM_DRAWSTATS)
struct ProfScope {
    DrawProf::Phase ph;
    double t0;
    explicit ProfScope(DrawProf::Phase p) : ph(p), t0(drawProfOn() ? profNowMs() : 0.0) {}
    ~ProfScope() { if (drawProfOn()) drawProf().ms[ph] += profNowMs() - t0; }
};
// Enderecos da vista corrente: worldToScreenMat[v], worldCtx[v], viewInfo[v]
inline uint32_t viewW2S() { return addr::worldToScreenMat + 0x40u * uint32_t(curView().view); }
inline uint32_t viewCtxAddr() { return addr::worldCtx + 0x1F0u * uint32_t(curView().view); }
inline uint32_t viewInfoAddr() { return addr::viewInfo + 0x14u * uint32_t(curView().view); }
// Geometria da vista `view` como o viewDef dela esta agora (fora da RAM ou sem tamanho: tela cheia)
inline ViewCtx readViewCtx(const Ram &ram, int view) {
    ViewCtx v;
    v.view = view;
    const uint32_t def = ram.u32(addr::viewInfo + 0x14u * uint32_t(view) + 4) & 0x1FFFFFFFu;
    if (ram.ok(def, 0x20) && ram.u16(def + 4) && ram.u16(def + 6)) {
        v.cx = float(int32_t(ram.u32(def + 0x18)));
        v.cy = float(int32_t(ram.u32(def + 0x1C)));
        v.w = ram.u16(def + 4);
        v.h = ram.u16(def + 6);
    }
    return v;
}

// Pacote de upload de uma textura (`texInfo[id]`): o `texmSetupTexture` monta em +0x20 o `sceGsLoadImage` do SDK
// (`sceGsSetDefLoadImage`): BITBLTBUF em +0x30 (DBW nos bits 48..53, DPSM 56..61), TRXREG em +0x50 (RRW 0..11, RRH 32..43); pixels
// lineares a partir de +0x80. (Ate 2026-10-15 o stub do runtime gravava ali uma struct propria, com largura em +0x24, altura em +0x26 e
// psm em +0x2B; dumps de RAM daquela epoca nao servem mais.)
struct UploadHdr {
    uint32_t w, h, psm, bw;
    static UploadHdr read(const Ram &ram, uint32_t ptr) {
        return {ram.u16(ptr + 0x50) & 0xFFFu, ram.u16(ptr + 0x54) & 0xFFFu, ram.u8(ptr + 0x37) & 0x3Fu, ram.u8(ptr + 0x36) & 0x3Fu};
    }
};

}  // namespace wotm

// Particulas e efeitos: precisa de Ram (acima); a Scene os desenha depois dos translucidos
#include "wotm_fx.hpp"

namespace wotm {

// ---------------------------------------------------------------- matrizes (vetores-linha)

struct M4 {
    float m[4][4];
};

inline M4 ident() {
    M4 r{};
    for (int i = 0; i < 4; ++i) r.m[i][i] = 1.f;
    return r;
}

// a primeiro, depois b
inline M4 mul(const M4 &a, const M4 &b) {
    M4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.m[i][k] * b.m[k][j];
            r.m[i][j] = s;
        }
    return r;
}

inline M4 translateM(float x, float y, float z) {
    M4 r = ident();
    r.m[3][0] = x; r.m[3][1] = y; r.m[3][2] = z;
    return r;
}

inline M4 scaleM(float x, float y, float z) {
    M4 r = ident();
    r.m[0][0] = x; r.m[1][1] = y; r.m[2][2] = z;
    return r;
}

inline M4 readM4(const Ram &ram, uint32_t a) {
    M4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) r.m[i][j] = ram.f32(a + 16 * i + 4 * j);
    return r;
}

inline void xform(const M4 &m, float x, float y, float z, float out[3]) {
    out[0] = x * m.m[0][0] + y * m.m[1][0] + z * m.m[2][0] + m.m[3][0];
    out[1] = x * m.m[0][1] + y * m.m[1][1] + z * m.m[2][1] + m.m[3][1];
    out[2] = x * m.m[0][2] + y * m.m[1][2] + z * m.m[2][2] + m.m[3][2];
}

// ---------------------------------------------------------------- malha (polyPkt)

// Um HierObject::polyPkt e uma sub-cadeia de DMA (tag QWC/ID=ret + 8 bytes) com um fluxo VIF para o microcodigo do VU1:
//   UNPACK V3-32 @0xB5   posicoes (ate 190)        UNPACK V3-8 @0x175   normais (signed /127)
//   por secao de strip (terminada por MSCNT): tag GIF em H (V4-32, NLOOP = n vertices), depois 3 qwords por vertice
//   (ST, RGBAQ, XYZF): S-8 @H+1 indice da posicao, V4-8 @H+2 RGBA (0x80 = 1.0), V2-16 @H+3 UV (4096 = 1.0).
//   STMASK 0xBFBFBFBF + S-8 @H+3+3k marca ADC (nao desenha) no vertice k: reinicia o strip.
constexpr uint8_t kSlotNoTex = 31;   // Vtx::slot de secao com PRIM.TME 0 na tag GIF: o GS desenha so a cor de vertice

struct Vtx {
    float x, y, z;
    float u, v;
    uint8_t r, g, b, a;
    float w = 1.f;                    // peso do osso de x (so objetos skinned)
    uint8_t cx = 0, cy = 0, cz = 0;   // codigos de osso (endereco de qword no VU1: slot = (codigo & 0x7F) / 4)
    uint8_t slot = 0;                 // indice do gsTexCtx/texId da secao (palavra 3 da tag GIF = 1 + 3*slot; 0 = slot 0); kSlotNoTex = sem textura
    uint8_t sub = 0;                  // indice do sub-pacote da cadeia (diagnostico)
    uint8_t isAdc = 0;                // triangulo normalmente descartado pelo ADC (so com WOTM_SHOWADC)
    uint8_t cull = 0;                 // secao com culling (bit 15 do seletor): o VU1 descarta a face de costas (Scene::backFacing)
    int8_t nx = 0, ny = 0, nz = 0;    // normal (V3-8 @0x175, mesmo indice da posicao; modulo ~127): luz por vertice (Scene::vertexRgb)
    uint8_t fge = 0;                  // PRIM.FGE da secao: o GS aplica a neblina (Scene::draw, shader)
};

// OBJECT sprite (objType 2, flags +0xC bits 5-6): secao com tag GIF SPRITE em H e 6 qwords por sprite, sem indices: ST0 @H+1,
// RGBA @H+2 (V4-8, passo 6), centro local + lado em w @H+3 (V4-32, passo 6), ST1 @H+4 (V2-16, passo 3 com o ST0). O microprograma
// (0x2E58, `pktAddVu1ObjAsm` pela tabela +0x158) projeta o centro e emite os cantos centro -/+ (512, 256) * lado / W pixels do GS,
// com ST0/ST1 (/4096, + uvOff), cor do pacote vezes q5 (limitada a 255, sem luz) e o F da neblina no centro. Halo do power-up.
struct ObjSprite {
    float x, y, z, size;          // centro no espaco do objeto; lado (o w do V4-32)
    float u0, v0, u1, v1;         // ST dos cantos (-, -) e (+, +) da tela do GS (4096 = 1,0)
    uint8_t r, g, b, a;           // RGBA cru do GS (0x80 = 1,0)
    uint8_t slot = 0, fge = 0;    // como Vtx::slot / Vtx::fge
};

struct MeshData {
    std::vector<Vtx> tris;   // lista de triangulos (3 vertices por triangulo)
    std::vector<ObjSprite> sprites;   // secoes SPRITE (ObjSprite): voltadas para a camera, montadas no desenho
    uint32_t qwc = 0;
    uint32_t bytes = 0;      // skinned: do polyPkt ao fim do ultimo sub-pacote decodificado (o pacote do Goliath passa de 128 KB)
    uint32_t sig = 0;       // assinatura barata para detectar RAM reaproveitada por outro nivel
    uint16_t texId = 0;
    bool valid = false;
    bool lowAlpha = false;   // algum vertice com alfa baixo (< 0x3C; 0x40 = opaco): nevoa/sombras, desenhado no passe translucido
    bool skinned = false;    // polyPkt e uma cadeia de sub-pacotes com V4-32 (codigos de osso + peso)
    bool abe = false;        // PRIM.ABE da tag GIF: so entao o alfa de vertice vale (blend); senao opaco
    uint8_t maxA = 0;        // maior alfa de vertice (tris e sprites, cru: 0x80 = 1,0); com o da textura diz se o As passa de 1
    uint32_t slotMask = 0;   // slots de textura usados pelas secoes (bit i = gsTexCtx[i])
    bool patched = false;    // copia alterada por Scene::patchMesh (o decodificado de novo volta sem)
    uint8_t curSub = 0;   // sub-pacote em decodificacao (diagnostico)
    uint8_t curSlot = 0;  // textura corrente do GS: seletor 0 na tag = nao troca o contexto (herda o da secao anterior)
    uint8_t curTme = 1;   // PRIM.TME em vigor (tag GIF com PRE; sem PRE o GS mantem o PRIM anterior)
    uint8_t curFge = 0;   // PRIM.FGE em vigor (neblina; mesma regra do TME)
    uint8_t curSprite = 0;   // PRIM do tipo SPRITE em vigor (ObjSprite; mesma regra do TME)
    uint32_t posTot = 0, posUsed = 0;   // posicoes carregadas / usadas por algum indice (diagnostico)
    uint32_t dropAdc = 0, dropDegen = 0;   // triangulos descartados por ADC / degenerados (diagnostico)
    uint32_t secTot = 0, secBad = 0, secBadEx = 0;   // secoes de tira / com nloop != indices (diagnostico)
    uint32_t oobIdx = 0, totIdx = 0;   // indices de vertice fora de `pos` (viram (0,0,0)) / total: diagnostico de decodificacao
};

// Pula os qwords zerados entre sub-pacotes.
inline uint32_t skipZeroQwords(const Ram &ram, uint32_t pp) {
    uint32_t skip = 0;
    while (skip < 0x400 && ram.ok(pp + skip, 16) && ram.u32(pp + skip) == 0 && ram.u32(pp + skip + 4) == 0) skip += 16;
    return pp + skip;
}

// Assinatura barata do pacote em `pp` (ja sem os qwords zerados): tag, primeira e ultima palavra (sem a ultima nos skinned, cujo
// fim so se acha decodificando). Scene::mesh revalida o cache com ela; uma formula diferente da do decodePacket fazia a malha ser
// decodificada de novo a cada chamada, e o ponteiro devolvido antes ficava solto.
inline uint32_t packetSig(const Ram &ram, uint32_t pp) {
    const uint32_t tag = ram.u32(pp), nwords = ((tag & 0xFFFF) + 1) * 4 - 2;
    return tag ^ ram.u32(pp + 8) ^ (ram.u32(pp + 12) * 31u) ^ ((tag & 0x10000) ? 0u : ram.u32(pp + 4 * (nwords + 1)) * 17u);
}

// Decodifica um sub-pacote em `md`; devolve o endereco do proximo (cadeia skinned) ou 0.
inline uint32_t decodePacket(const Ram &ram, uint32_t pp, MeshData &md, bool first) {
    pp = skipZeroQwords(ram, pp);
    if (!ram.ok(pp, 16)) return 0;
    const uint32_t tag = ram.u32(pp);
    if (((tag >> 28) & 7) != 6) return 0;
    const bool skinnedPkt = (tag & 0x10000) != 0;   // bit 16: vertices V4-32 com codigos de osso
    const uint32_t qwc = tag & 0xFFFF;
    uint32_t nwords = (qwc + 1) * 4 - 2;
    const uint32_t base = pp + 8;
    if (skinnedPkt) nwords = ram.size > base ? uint32_t(std::min<size_t>(60000, (ram.size - base) / 4)) : 0;   // fim = dois NOPs seguidos
    if (nwords == 0 || (!skinnedPkt && !ram.ok(base, nwords * 4))) return 0;
    if (skinnedPkt) md.skinned = true;
    if (first) {
        md.qwc = qwc;
        md.sig = packetSig(ram, pp);
        md.valid = true;
    }
    uint32_t endPp = 0;

    struct P3 { float x, y, z, w; uint8_t cx, cy, cz; };
    std::vector<P3> pos;
    std::vector<std::array<int8_t, 3>> nrm;   // normais (V3-8 @0x175), pelo indice da posicao
    std::vector<char> usedPos;   // diagnostico: posicao referenciada por algum indice
    uint32_t hdrAddr = 0, nloop = 0, hdrSel = 0;
    bool haveHdr = false;
    std::vector<uint8_t> idx;
    std::vector<std::array<uint8_t, 4>> rgba;
    std::vector<std::array<int16_t, 2>> uv;
    std::vector<uint32_t> adc;
    std::vector<std::array<float, 4>> spos;   // centros + lado das secoes SPRITE
    uint32_t mask = 0;

    auto flush = [&]() {
        if (haveHdr && md.curSprite) {
            const uint32_t sel = hdrSel & 0x7FFF;
            if (sel >= 1 && sel <= 13) md.curSlot = uint8_t((sel - 1) / 3);
            const uint8_t slot = md.curTme ? md.curSlot : kSlotNoTex;
            const size_t n = std::min<size_t>({nloop, spos.size(), rgba.size(), uv.size() / 2});
            for (size_t k = 0; k < n; ++k) {
                ObjSprite s;
                s.x = spos[k][0]; s.y = spos[k][1]; s.z = spos[k][2]; s.size = spos[k][3];
                s.u0 = uv[2 * k][0] / 4096.f; s.v0 = uv[2 * k][1] / 4096.f; s.u1 = uv[2 * k + 1][0] / 4096.f; s.v1 = uv[2 * k + 1][1] / 4096.f;
                s.r = rgba[k][0]; s.g = rgba[k][1]; s.b = rgba[k][2]; s.a = rgba[k][3];
                s.slot = slot; s.fge = md.curFge;
                md.sprites.push_back(s);
                md.slotMask |= 1u << slot;
            }
        } else if (haveHdr && !idx.empty()) {
            const uint32_t n = std::min<uint32_t>(nloop, uint32_t(idx.size()));
            ++md.secTot;
            if (nloop != idx.size() || rgba.size() < n || uv.size() < n) { ++md.secBad; if (md.secBadEx < 4) { ++md.secBadEx; std::fprintf(stderr, "   secao: nloop=%u idx=%zu rgba=%zu uv=%zu" "%c", nloop, idx.size(), rgba.size(), uv.size(), 10); } }
            std::vector<bool> restart(n + 1, false);
            for (uint32_t k : adc) if (k < n) restart[k] = true;
            { static const bool hs = std::getenv("WOTM_HDRSEL") != nullptr; if (hs) std::fprintf(stderr, "[hdrsel] tex=%u sel=%u n=%u\n", md.texId, hdrSel, n); }   // diagnostico
            std::vector<Vtx> strip;
            // ADC (bit de GIF no vertice k): o GS NAO desenha o triangulo que termina em k, mas a tira continua (a convencao de
            // "novo strip" e ADC nos dois primeiros vertices). Tratar como reinicio perdia um triangulo por reinicio (furos).
            auto emit = [&]() {
                for (size_t k = 2; k < strip.size(); ++k) {
                    static const bool showAdc = std::getenv("WOTM_SHOWADC") != nullptr;
                    const bool adcTri = k < restart.size() && restart[k];
                    if (adcTri) { ++md.dropAdc; if (!showAdc) continue; }
                    const Vtx &a = strip[(k & 1) ? k - 1 : k - 2];
                    const Vtx &b = strip[(k & 1) ? k - 2 : k - 1];
                    const Vtx &c = strip[k];
                    // descarta triangulos degenerados (juncao de strips por vertice repetido)
                    auto same = [](const Vtx &p, const Vtx &q) { return p.x == q.x && p.y == q.y && p.z == q.z; };
                    if (same(a, b) || same(b, c) || same(a, c)) { ++md.dropDegen; continue; }
                    { Vtx ta = a, tb = b, tc = c; ta.isAdc = tb.isAdc = tc.isAdc = adcTri ? 1 : 0; md.tris.push_back(ta); md.tris.push_back(tb); md.tris.push_back(tc); }
                }
                strip.clear();
            };
            for (uint32_t k = 0; k < n; ++k) {
                const uint32_t j = idx[k];
                ++md.totIdx; if (j >= pos.size()) ++md.oobIdx;
                if (j < pos.size()) { if (usedPos.size() < pos.size()) usedPos.resize(pos.size(), 0); usedPos[j] = 1; }
                const P3 p = j < pos.size() ? pos[j] : P3{0, 0, 0, 1.f, 0, 0, 0};
                Vtx v{};
                v.x = p.x; v.y = p.y; v.z = p.z; v.w = p.w; v.cx = p.cx; v.cy = p.cy; v.cz = p.cz;
                if (j < nrm.size()) { v.nx = nrm[j][0]; v.ny = nrm[j][1]; v.nz = nrm[j][2]; }
                const uint32_t sel = hdrSel & 0x7FFF;   // bit 15: culling da secao (os predios usam 0x8001, 0x8004...)
                if (sel >= 1 && sel <= 13) md.curSlot = uint8_t((sel - 1) / 3);   // 0 = mantem a textura anterior
                v.slot = md.curTme ? md.curSlot : kSlotNoTex;
                v.fge = md.curFge;
                v.cull = uint8_t((hdrSel >> 15) & 1);
                v.sub = md.curSub;
                md.slotMask |= 1u << v.slot;
                if (k < rgba.size()) { v.r = rgba[k][0]; v.g = rgba[k][1]; v.b = rgba[k][2]; v.a = rgba[k][3]; }
                else { v.r = v.g = v.b = v.a = 128; }
                if (k < uv.size()) { v.u = uv[k][0] / 4096.f; v.v = uv[k][1] / 4096.f; }
                strip.push_back(v);
            }
            emit();
        }
        haveHdr = false; idx.clear(); rgba.clear(); uv.clear(); adc.clear(); spos.clear();
    };

    uint32_t i = 0;
    while (i < nwords) {
        const uint32_t w = ram.u32(base + 4 * i);
        const uint32_t cmd = (w >> 24) & 0x7F, num = (w >> 16) & 0xFF, imm = w & 0xFFFF;
        const uint32_t a = base + 4 * (i + 1);
        if (cmd >= 0x60) {
            const uint32_t vn = (cmd >> 2) & 3, vl = cmd & 3, n = num ? num : 256, addr = imm & 0x3FF;
            static const uint32_t comps[4] = {1, 2, 3, 4}, bits[4] = {32, 16, 8, 5};
            const uint32_t nw = vl == 3 ? (n * 16 + 31) / 32 : (n * bits[vl] * comps[vn] + 31) / 32;
            if (vn == 2 && vl == 0 && addr == 0xB5) {                        // V3-32 posicoes
                if (haveHdr) flush();
                pos.resize(n);
                for (uint32_t k = 0; k < n; ++k) pos[k] = {ram.f32(a + 12 * k), ram.f32(a + 12 * k + 4), ram.f32(a + 12 * k + 8), 1.f, 0, 0, 0};
            } else if (vn == 3 && vl == 0 && addr == 0xB5 && n > 1) {         // V4-32 (skinned): codigos de osso nos 8 bits baixos de x/y/z, peso em w
                if (haveHdr) flush();
                pos.resize(n);
                for (uint32_t k = 0; k < n; ++k) {
                    const uint32_t o = a + 16 * k;
                    pos[k] = {ram.f32(o), ram.f32(o + 4), ram.f32(o + 8), ram.f32(o + 12),
                              uint8_t(ram.u32(o) & 0xFF), uint8_t(ram.u32(o + 4) & 0xFF), uint8_t(ram.u32(o + 8) & 0xFF)};
                }
            } else if (vn == 2 && vl == 2 && addr == 0x175) {                 // V3-8 normais (chegam junto das posicoes)
                nrm.resize(n);
                for (uint32_t k = 0; k < n; ++k) nrm[k] = {int8_t(ram.u8(a + 3 * k)), int8_t(ram.u8(a + 3 * k + 1)), int8_t(ram.u8(a + 3 * k + 2))};
            } else if (haveHdr && md.curSprite && vn == 3 && vl == 0 && addr == hdrAddr + 3) {   // V4-32 centros + lado (SPRITE)
                spos.resize(n);
                for (uint32_t k = 0; k < n; ++k) spos[k] = {ram.f32(a + 16 * k), ram.f32(a + 16 * k + 4), ram.f32(a + 16 * k + 8), ram.f32(a + 16 * k + 12)};
            } else if (haveHdr && md.curSprite && vn == 1 && vl == 1 && addr == hdrAddr + 1) {   // V2-16 ST0/ST1 (SPRITE)
                uv.resize(n);
                for (uint32_t k = 0; k < n; ++k) uv[k] = {ram.s16(a + 4 * k), ram.s16(a + 4 * k + 2)};
            } else if (vn == 3 && vl == 0 && n == 1) {                        // V4-32: tag GIF
                hdrAddr = addr; nloop = ram.u32(a) & 0x7FFF; haveHdr = true;
                hdrSel = ram.u32(a + 12);   // palavra alta de REGS: o jogo a usa como seletor de textura (1 + 3*slot)
                if ((ram.u32(a + 4) >> 14) & 1) md.curTme = uint8_t((ram.u32(a + 4) >> 19) & 1);   // PRE: PRIM nos bits 47-57 (TME = bit 4)
                if ((ram.u32(a + 4) >> 14) & 1) md.curFge = uint8_t((ram.u32(a + 4) >> 20) & 1);   // FGE = bit 5 do PRIM
                if ((ram.u32(a + 4) >> 14) & 1) md.curSprite = uint8_t(((ram.u32(a + 4) >> 15) & 7) == 6);   // tipo 6 = SPRITE
                if (((ram.u32(a + 4) >> 15) >> 6) & 1) md.abe = true;
            } else if (haveHdr && vn == 0 && vl == 2 && (mask & 0xFF) == 0xBF) {   // S-8 sob mascara: ADC
                for (uint32_t k = 0; k < n; ++k) {
                    const int e = int(addr) - int(hdrAddr) - 3 + 3 * int(k);
                    if (e >= 0 && e % 3 == 0) adc.push_back(uint32_t(e / 3));
                }
            } else if (haveHdr && vn == 0 && vl == 2 && addr == hdrAddr + 1) {      // S-8 indices
                idx.resize(n);
                for (uint32_t k = 0; k < n; ++k) idx[k] = ram.u8(a + k);
            } else if (haveHdr && vn == 3 && vl == 2 && addr == hdrAddr + 2) {      // V4-8 RGBA
                rgba.resize(n);
                for (uint32_t k = 0; k < n; ++k) rgba[k] = {ram.u8(a + 4 * k), ram.u8(a + 4 * k + 1), ram.u8(a + 4 * k + 2), ram.u8(a + 4 * k + 3)};
            } else if (haveHdr && vn == 1 && vl == 1 && addr == hdrAddr + 3) {      // V2-16 UV
                uv.resize(n);
                for (uint32_t k = 0; k < n; ++k) uv[k] = {ram.s16(a + 4 * k), ram.s16(a + 4 * k + 2)};
            }
            i += 1 + nw;
            continue;
        }
        uint32_t extra = 0;
        if (cmd == 0x20) { mask = ram.u32(a); extra = 1; }
        else if (cmd == 0x30 || cmd == 0x31) extra = 4;
        else if (cmd == 0x4A) extra = (num ? num : 256) * 2;
        else if (cmd == 0x50 || cmd == 0x51) extra = (imm ? imm : 65536) * 4;
        else if (cmd == 0x14 || cmd == 0x15 || cmd == 0x17) flush();   // MSCAL/MSCALF/MSCNT: fim da secao
        else if (cmd == 0 && skinnedPkt && i > 8 && ram.u32(a) == 0) {  // dois NOPs seguidos: fim do sub-pacote skinned
            endPp = (base + 4 * (i + 2) + 15) & ~15u;
            break;
        }
        i += 1 + extra;
    }
    flush();
    md.posTot += uint32_t(pos.size());
    for (char c : usedPos) md.posUsed += c ? 1u : 0u;
    return endPp;
}

inline MeshData decodeObject(const Ram &ram, uint32_t node) {
    MeshData md;
    uint32_t pp = ram.u32(node + 4) & 0x0FFFFFFF;
    if (!ram.ok(pp, 16)) return md;
    md.texId = ram.u16(node + 0x50);
    const uint32_t start = pp;
    for (int guard = 0; pp && guard < 256; ++guard) {
        md.curSub = uint8_t(guard);
        pp = decodePacket(ram, pp, md, guard == 0);
        if (pp) md.bytes = pp - start;
    }
    for (const Vtx &v : md.tris) if (v.a < 0x3C) { md.lowAlpha = true; break; }
    for (const Vtx &v : md.tris) md.maxA = std::max(md.maxA, v.a);
    for (const ObjSprite &s : md.sprites) md.maxA = std::max(md.maxA, s.a);
    return md;
}

// ---------------------------------------------------------------- cena

// Teste de alfa do shader do Scene::draw quando nada traz outro (TEST_1 0x8D: alfa > 8). Na travessia dos objetos o estado inicial vem
// do ctxDef do jogo (Scene::collect: 0x5040C, sem teste); este limiar fica para as passadas fora do hier (sombras, destrocos, HUD).
// kGsAlphaNoTest: TEST_1 com ATE = 0 (sem teste: o GS desenha todo pixel, inclusive As = 0; os brilhos aditivos 0x5040C dependem disso).
inline constexpr int16_t kGsAlphaRef = 8, kGsAlphaNoTest = -1;

// Icones de botao 2D no lugar dos objetos 3D do McButtons (Scene::drawButtonSprites; o runtime instala, runtime/wotm_prompts.inc). id = o do
// objeto (0x1F51.. ou 0x240E..); mid = centro no mundo; right/up = eixos da camera; size = lado no mundo; alpha = fade. Desenha dentro
// do modo 3D do quadro. g_buttonIconsOn: se os 2D valem agora (opcao do jogador); sem funcao, os 3D do jogo.
using ButtonIconFn = void (*)(uint16_t id, const float mid[3], const float right[3], const float up[3], float size, float alpha);
inline ButtonIconFn g_buttonIconFn = nullptr;
inline bool (*g_buttonIconsOn)() = nullptr;

struct DrawItem {
    M4 m;
    uint32_t node;   // HierObject
    uint32_t animPkt = 0;   // CHAR_INSTANCE::animPktPtr (paleta de matrizes dos objetos skinned)
    int lodIdx = 0;         // indice do LOD escolhido (diagnostico; a cadeia da paleta e animPktPtr[packetPtrIdx], Scene::palette)
    std::shared_ptr<std::vector<M4>> pal;   // copia das 31 matrizes da paleta (preenchida por takeSnapshot; vazia = ler da RAM)
    uint8_t blend = kGsAlphaNormal;   // byte baixo do ALPHA_1 do no mais proximo com ctx (GROUP, TRANSLATE...; Scene::CtxScope, gsBlend)
    float fade = 1.f;       // cross-fade de LOD (hierLod): multiplica o alfa; < 1 vai para o passe translucido
    float uvOff[2] = {0, 0};   // deslocamento do UV (qword 4 do cabecalho do VU1, somado ao UV normalizado): Scene::objectUvOffset
    float color[4] = {1, 1, 1, 1};   // multiplicador da cor do vertice (qword 5, vf31; sem o fade, que fica em `fade`): Scene::objectColor
    float spec = 0.f;  // especular do VU1 (qword 20) que vale para este objeto: Scene::walk (herdado do ultimo com specLightMe)
    // Luzes paralelas no espaco do objeto (x32; colunas 0-2 e vetor-meio em [3]) quando o objeto fica abaixo de um SCALE que corrige a
    // luz (Scene::lightScale): o hier nao gira o paraL, usa a tabela lightDir do nivel (q6-q8 do VU1). Sem isso, ownLight = false.
    bool ownLight = false;
    float lightObj[4][3] = {};
    uint8_t env = 0;   // mapa de ambiente (Scene::objectEnv): 0 = UV do pacote, 1 = mapa comum, 2 = video/quadro anterior, 3 = CLOAK
    float envK = 0.f;  // peso da normal no UV do mapa de ambiente (envMapWeight ou o do CLOAK)
    uint16_t hudId = 0;   // so em Scene::hud: id do elemento de HUD (do epNode do _cs)
    uint16_t btnId = 0;   // so em overlay/hpOverlay: id do icone de botao (Scene::drawButtonSprites)
    uint32_t cs = 0;      // _cs cuja travessia emitiu o objeto (0 na fase, como o cs_): receptor de sombra (psObjectCull)
    int16_t aref = kGsAlphaRef;   // teste de alfa do TEST_1 do GROUP que contem o objeto: descarta As * 128 <= aref (Scene::groupState; kGsAlphaNoTest = sem teste)
    bool zmsk = false;   // ZBUF_1.ZMSK do GROUP que contem o objeto: nao grava profundidade (so vale no passe translucido; Scene::groupState)
    bool lodX = false;   // dentro de um cross-fade de LOD (os dois LODs, inclusive o de fade 1): com o bit 0x4000 vai para a OT de alfa
    // Luzes pontuais que tocam a esfera do objeto (Scene::objectPointLights, programa 0x748 do VU0): ptN > 0 troca o programa de luz
    // pelo 0x15D8 do VU1. ptRel = -(posicao da luz no espaco do objeto), ptCol = cor, ptFall = falloff (PointLight +0x2C).
    uint8_t ptN = 0;
    float ptRel[3][3] = {};
    float ptCol[3][3] = {};
    float ptFall[3] = {};
};

struct Camera {
    float pos[3] = {0, 0, 0};
    float yaw = 0, pitch = 0;   // radianos; yaw 0 olha para +X, Z para cima
};

// Camera do jogo para uma vista: o que o viewUpdate gravou (worldToScreenMat e worldCtx[view].eo). Sem matriz valida (menus), o
// `_cs` de viewInfo[view] (posicao em +0x10, matriz em +0x20: as linhas sao os eixos do olho em coordenadas de mundo = direita,
// FRENTE, cima). worldToScreenMat tem foco 384 sobre 640 px (HFOV ~79,6 graus) e, em 4:3, o VFOV e ~63,7 graus (o eixo de
// profundidade do olho e o Y).
struct GameCamera {
    bool ok = false;
    float pos[3] = {0, 0, 0};
    float fwd[3] = {0, 1, 0};
    float up[3] = {0, 0, 1};
    float fovy = 63.7f;   // graus
};

inline GameCamera readGameCamera(const Ram &ram, int view = 0) {
    GameCamera c;
    const uint32_t cs = ram.u32(addr::viewInfo + 0x14 * view);
    if (!ram.ok(cs, 0x60)) return c;
    for (int i = 0; i < 3; ++i) {
        c.pos[i] = ram.f32(cs + 0x10 + 4 * i);
        c.fwd[i] = ram.f32(cs + 0x20 + 16 * 1 + 4 * i);
        c.up[i] = ram.f32(cs + 0x20 + 16 * 2 + 4 * i);
    }
    // worldToScreenMat NAO guarda o foco em elementos fixos: a linha 0 e 384*direita + 2048*frente e a linha 1 e -180,48*cima + 2048*frente
    // (o jogo embute os eixos da camera). Ler [0][0]/[1][2] cru dava um FOV que mudava com a orientacao da camera (zoom in/out ao girar).
    const uint32_t wm = addr::worldToScreenMat + 0x40 * view;
    // So a propria matriz: linha 3 = frente (unitaria); o foco vertical e a parte da linha 1 perpendicular a ela. Misturar com o _cs lido
    // em outro instante dava picos de FOV de 1-2 graus em leituras rasgadas (o jogo reescreve a matriz e o _cs em momentos diferentes).
    float r1[3], r3[3], d13 = 0.f, n3 = 0.f;
    for (int i = 0; i < 3; ++i) { r1[i] = ram.f32(wm + 0x10 + 4 * i); r3[i] = ram.f32(wm + 0x30 + 4 * i); d13 += r1[i] * r3[i]; n3 += r3[i] * r3[i]; }
    float fv = 0.f;
    const float f0 = n3 > 0.5f ? 1.f : 0.f;
    if (n3 > 0.5f) {
        for (int i = 0; i < 3; ++i) { const float p = r1[i] - d13 / n3 * r3[i]; fv += p * p; }
        fv = std::sqrt(fv);
    }
    if (f0 > 0.5f && fv > 1.f) {
        // altura da vista em linhas do campo: 224, salvo nos quadrantes de 3 e 4 jogadores (112), cujo viewDef mora acima dos 32 MB
        // do jogo (wotm_split4.inc). Os viewDef do retail ficam com 224 (o do menu tem 448 e o da carga 6, e a conta e a de sempre)
        const uint32_t def = ram.u32(addr::viewInfo + 0x14 * view + 4) & 0x1FFFFFFFu;
        const float vh = def >= 0x2000000u && ram.ok(def, 8) && ram.u16(def + 6) ? float(ram.u16(def + 6)) : 224.f;
        c.fovy = 2.f * std::atan(vh / (2.f * fv)) * 180.f / 3.14159265f;
        // Olho e eixos do que o viewUpdate gravou junto com a matriz (worldCtx[view].eo = -cs->trans, em +0x20), nao do _cs: na
        // introducao da luta o jogo poe outra camera nesse _cs por um instante no meio do quadro, e a leitura pegava esse instante
        // (inteiro ou pela metade: a tela cortava por um quadro para outro canto do mapa).
        const uint32_t eo = addr::worldCtx + 0x1F0 * view + 0x20;
        float un = 0.f;
        for (int i = 0; i < 3; ++i) {
            c.pos[i] = -ram.f32(eo + 4 * i);
            c.fwd[i] = r3[i] / std::sqrt(n3);
            c.up[i] = -(r1[i] - d13 / n3 * r3[i]);   // linha 1 = -180,48*cima + 2048*frente
            un += c.up[i] * c.up[i];
        }
        for (int i = 0; i < 3; ++i) c.up[i] /= std::sqrt(un);
    }
    c.ok = std::isfinite(c.pos[0]) && std::isfinite(c.fwd[0]) && (std::fabs(c.fwd[0]) + std::fabs(c.fwd[1]) + std::fabs(c.fwd[2])) > 0.1f;
    return c;
}

// ---------------------------------------------------------------- interpolacao de quadros (Scene::showBlend)
// A logica roda a 60 e a apresentacao na taxa do monitor: entre dois quadros do jogo o desenho mistura os dois ultimos instantaneos
// (A anterior, B mais novo). Matriz: escala por linha (a convencao do codigo: scaleM antes), rotacao por slerp, translacao linear.
namespace interp {
struct Quat { float w, x, y, z; };

// Linhas 0-2 = eixos com escala. Falso se as linhas normalizadas nao forem ortogonais (cisalhamento): ai o chamador faz lerp simples.
// det < 0 (espelho): a linha 2 entra com escala negativa.
inline bool decompose(const M4 &m, Quat &q, float s[3]) {
    float R[3][3];
    for (int i = 0; i < 3; ++i) {
        s[i] = std::sqrt(m.m[i][0] * m.m[i][0] + m.m[i][1] * m.m[i][1] + m.m[i][2] * m.m[i][2]);
        if (!(s[i] > 1e-6f)) return false;
        for (int j = 0; j < 3; ++j) R[i][j] = m.m[i][j] / s[i];
    }
    for (int i = 0; i < 3; ++i) {
        const int j = (i + 1) % 3;
        if (std::fabs(R[i][0] * R[j][0] + R[i][1] * R[j][1] + R[i][2] * R[j][2]) > 0.02f) return false;
    }
    const float det = R[0][0] * (R[1][1] * R[2][2] - R[1][2] * R[2][1]) - R[0][1] * (R[1][0] * R[2][2] - R[1][2] * R[2][0]) +
                      R[0][2] * (R[1][0] * R[2][1] - R[1][1] * R[2][0]);
    if (det < 0.f) {
        s[2] = -s[2];
        for (int j = 0; j < 3; ++j) R[2][j] = -R[2][j];
    }
    // A = R^T (matriz-coluna): A[r][c] = R[c][r]
    auto A = [&](int r, int c) { return R[c][r]; };
    const float tr = A(0, 0) + A(1, 1) + A(2, 2);
    if (tr > 0.f) { const float t = std::sqrt(tr + 1.f) * 2.f; q = {0.25f * t, (A(2, 1) - A(1, 2)) / t, (A(0, 2) - A(2, 0)) / t, (A(1, 0) - A(0, 1)) / t}; }
    else if (A(0, 0) > A(1, 1) && A(0, 0) > A(2, 2)) { const float t = std::sqrt(1.f + A(0, 0) - A(1, 1) - A(2, 2)) * 2.f; q = {(A(2, 1) - A(1, 2)) / t, 0.25f * t, (A(0, 1) + A(1, 0)) / t, (A(0, 2) + A(2, 0)) / t}; }
    else if (A(1, 1) > A(2, 2)) { const float t = std::sqrt(1.f + A(1, 1) - A(0, 0) - A(2, 2)) * 2.f; q = {(A(0, 2) - A(2, 0)) / t, (A(0, 1) + A(1, 0)) / t, 0.25f * t, (A(1, 2) + A(2, 1)) / t}; }
    else { const float t = std::sqrt(1.f + A(2, 2) - A(0, 0) - A(1, 1)) * 2.f; q = {(A(1, 0) - A(0, 1)) / t, (A(0, 2) + A(2, 0)) / t, (A(1, 2) + A(2, 1)) / t, 0.25f * t}; }
    return true;
}

inline Quat slerp(Quat a, Quat b, float t) {
    float d = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
    if (d < 0.f) { b = {-b.w, -b.x, -b.y, -b.z}; d = -d; }
    float ka, kb;
    if (d > 0.9995f) { ka = 1.f - t; kb = t; }
    else { const float th = std::acos(d), sn = std::sin(th); ka = std::sin((1.f - t) * th) / sn; kb = std::sin(t * th) / sn; }
    Quat r{ka * a.w + kb * b.w, ka * a.x + kb * b.x, ka * a.y + kb * b.y, ka * a.z + kb * b.z};
    const float n = std::sqrt(r.w * r.w + r.x * r.x + r.y * r.y + r.z * r.z);
    if (n > 0.f) { r.w /= n; r.x /= n; r.y /= n; r.z /= n; }
    return r;
}

inline M4 lerpM4(const M4 &a, const M4 &b, float t) {
    M4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) r.m[i][j] = a.m[i][j] + (b.m[i][j] - a.m[i][j]) * t;   // translacao, coluna 3 e o caso sem decomposicao
    Quat qa, qb;
    float sa[3], sb[3];
    if (decompose(a, qa, sa) && decompose(b, qb, sb) && (sa[2] < 0.f) == (sb[2] < 0.f)) {
        const Quat q = slerp(qa, qb, t);
        const float x = q.x, y = q.y, z = q.z, w = q.w;
        const float A[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
                               {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
                               {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}};
        for (int i = 0; i < 3; ++i) {
            const float s = sa[i] + (sb[i] - sa[i]) * t;
            for (int j = 0; j < 3; ++j) r.m[i][j] = A[j][i] * s;   // volta para vetores-linha
        }
    }
    return r;
}

inline void orthonormal(float fwd[3], float up[3]) {
    float n = std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
    if (n > 1e-6f) for (int i = 0; i < 3; ++i) fwd[i] /= n;
    const float d = up[0] * fwd[0] + up[1] * fwd[1] + up[2] * fwd[2];
    for (int i = 0; i < 3; ++i) up[i] -= d * fwd[i];
    n = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
    if (n > 1e-6f) for (int i = 0; i < 3; ++i) up[i] /= n;
}

// Corte de camera entre A e B (sem mistura): teleporte, giro brusco, FOV mudando
inline bool cameraCut(const GameCamera &a, const GameCamera &b) {
    if (!a.ok || !b.ok) return true;
    const float dx = b.pos[0] - a.pos[0], dy = b.pos[1] - a.pos[1], dz = b.pos[2] - a.pos[2];
    const float dotf = a.fwd[0] * b.fwd[0] + a.fwd[1] * b.fwd[1] + a.fwd[2] * b.fwd[2];
    return dx * dx + dy * dy + dz * dz > 400.f * 400.f || dotf < 0.7f || std::fabs(a.fovy - b.fovy) > 5.f;
}

// Posicao e FOV lineares, eixos por nlerp e reortonormalizados (basta para o giro de um quadro)
inline GameCamera lerpCamera(const GameCamera &a, const GameCamera &b, float t) {
    GameCamera r = b;
    for (int i = 0; i < 3; ++i) {
        r.pos[i] = a.pos[i] + (b.pos[i] - a.pos[i]) * t;
        r.fwd[i] = a.fwd[i] + (b.fwd[i] - a.fwd[i]) * t;
        r.up[i] = a.up[i] + (b.up[i] - a.up[i]) * t;
    }
    orthonormal(r.fwd, r.up);
    r.fovy = a.fovy + (b.fovy - a.fovy) * t;
    return r;
}

// Camera -> mundo (vetores-linha): linhas = direita, frente, cima, olho
inline M4 cameraToWorld(const GameCamera &c) {
    float f[3] = {c.fwd[0], c.fwd[1], c.fwd[2]}, u[3] = {c.up[0], c.up[1], c.up[2]};
    orthonormal(f, u);
    M4 m = ident();
    const float r[3] = {f[1] * u[2] - f[2] * u[1], f[2] * u[0] - f[0] * u[2], f[0] * u[1] - f[1] * u[0]};
    for (int i = 0; i < 3; ++i) { m.m[0][i] = r[i]; m.m[1][i] = f[i]; m.m[2][i] = u[i]; m.m[3][i] = c.pos[i]; }
    return m;
}

// Inversa de uma matriz rigida (rotacao ortonormal + translacao, vetores-linha)
inline M4 rigidInverse(const M4 &m) {
    M4 r = ident();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.m[i][j] = m.m[j][i];
    for (int j = 0; j < 3; ++j) r.m[3][j] = -(m.m[3][0] * r.m[0][j] + m.m[3][1] * r.m[1][j] + m.m[3][2] * r.m[2][j]);
    return r;
}

// Foco da projecao do jogo: px do GS (horizontal) e linhas (vertical) por unidade de tangente. Da worldToScreenMat (16 floats, linhas):
// linha 0 = fh*direita + c*frente, linha 1 = -fv*cima + c*frente, linha 3 = frente; o foco e a parte perpendicular a linha 3.
struct Focus { float fh = 384.f, fv = 180.48f; };
inline bool focusOf(const float *M, Focus &f) {
    const float *r3 = M + 12;
    const float n3 = r3[0] * r3[0] + r3[1] * r3[1] + r3[2] * r3[2];
    if (!(n3 > 0.5f) || !std::isfinite(n3)) return false;
    auto perp = [&](const float *r) {
        const float k = (r[0] * r3[0] + r[1] * r3[1] + r[2] * r3[2]) / n3;
        float s = 0.f;
        for (int i = 0; i < 3; ++i) s += (r[i] - k * r3[i]) * (r[i] - k * r3[i]);
        return std::sqrt(s);
    };
    const float fh = perp(M), fv = perp(M + 4);
    if (!(fh > 1.f && fv > 1.f) || !std::isfinite(fh) || !std::isfinite(fv)) return false;
    f.fh = fh;
    f.fv = fv;
    return true;
}
// mundo -> camera (direita, frente, cima) pela cameraToWorld C; point = com a translacao
inline void toCam(const M4 &C, const float p[3], bool point, float q[3]) {
    const float d[3] = {p[0] - (point ? C.m[3][0] : 0.f), p[1] - (point ? C.m[3][1] : 0.f), p[2] - (point ? C.m[3][2] : 0.f)};
    for (int k = 0; k < 3; ++k) q[k] = d[0] * C.m[k][0] + d[1] * C.m[k][1] + d[2] * C.m[k][2];
}
inline void fromCam(const M4 &C, const float q[3], bool point, float p[3]) {
    for (int i = 0; i < 3; ++i) p[i] = (point ? C.m[3][i] : 0.f) + q[0] * C.m[0][i] + q[1] * C.m[1][i] + q[2] * C.m[2][i];
}
// Ponto do mundo -> deslocamento do centro da tela (px do GS, linhas); falso atras da camera
inline bool project(const M4 &C, const Focus &f, const float w[3], float &x, float &y) {
    float q[3];
    toCam(C, w, true, q);
    if (q[1] < 1.f) return false;
    x = f.fh * q[0] / q[1];
    y = -f.fv * q[2] / q[1];
    return true;
}
}   // namespace interp

// Particula desenhada como sprite voltado para a camera (destroços, poeira...). Lida de fx[] (0x5BA280, 98 x 12000 bytes).
struct Sprite {
    float pos[3] = {0, 0, 0};
    float size = 1.f;       // lado do quadrado em unidades do mundo
    float rot = 0.f;        // radianos, em torno do eixo de visada
    float rgba[4] = {1, 1, 1, 1};
    uint32_t texId = 0;     // indice em texInfo (0 = circulo suave procedural)
    uint64_t tex0 = 0;      // TEX0 do efeito (psm, CBP da paleta)
    float uv[4] = {0, 0, 1, 1};   // u0, v0, u1, v1 normalizados ((u0, v0) no canto de cima a esquerda da tela)
    uint32_t gsCol = 0;     // destroço: RGBA do GS (0x80 = 1,0) do RGBAQ do pacote; 0 = usa rgba
    int16_t aref = 8;       // destroço: limiar do teste de alfa (Scene::setAlphaRef); kGsAlphaRef = padrao
    bool opaque = false;    // destroço sem ABE com alphaContext 0: grava profundidade (ZBUF do setOpeningContext)
    uint32_t key = 0;       // fx * 4096 + indice: casa a mesma particula entre dois quadros
    // Passo do particleMove (Particles::stepBack): pos_B - pos_A = f (step - (0, 0, grav f)) em f fields; zero na que nasceu no passo
    float step[3] = {0, 0, 0};
    float grav = 0.f;
};

// Glifo do texto do jogo (fontSpritePrint*): um sprite do GS. UV em texels do atlas, XY em pixels da tela do jogo (640 x 224, campo).
struct TextQuad {
    float u0, v0, u1, v1;
    float x0, y0, x1, y1;
    uint8_t r, g, b, a;   // RGBAQ do GS: 0x80 = 1,0 (a cor modula a textura)
    uint64_t tex0;        // TEX0 em vigor (atlas T8H 512x512 do .RTX da fase + paleta CBP)
};

// Retangulo de tela sem textura (fade): SPRITE do GS com ABE e ALPHA_1 0x44 ((Cs - Cd)·As + Cd, As = a/0x80) sobre a vista, montado
// como pacote GIF por rtFadeScreen, TheGame::updateFadeOut/updateFadeOutAndIn, uiFadeScreen e Shell::FadeScreen. XY na tela do jogo
// (640 x 224, campo), como o texto.
struct ScreenRect {
    float x0, y0, x1, y1;
    uint8_t r, g, b, a;   // RGBAQ do GS: a = 0x80 cobre tudo
    // Saiu ao GS antes do texto do quadro (fila do gsPkt despachada antes do fontDmaFontData, como no ShellRtLoop::endField dos
    // dialogos de fim de fase): o texto vai por cima. No rtMain a fila sai depois do texto e o fade cobre tudo (false).
    bool underText = false;
    // Barra de vida sobre o inimigo (wotmBarRect): vista em que foi montada (-1 = nao e barra). Na tela dividida vai para a metade da
    // janela dessa vista e fica cortada nela, sem esticar ate a borda como o fade e as faixas de cinema
    int8_t barView = -1;
    // Retangulo do HUD nativo de um jogador (jogadores 3 e 4, wotmPlayerHud): sai depois das barras dos inimigos (por cima delas) e, na
    // faixa de baixo dos 3 jogadores, com a escala em que as caixas dele foram medidas (barSpan)
    bool hud = false;
    // Barra com textura residente (retrato do inimigo, wotmBarTex): TEX0 da imagem do .RTX (0 = cor lisa) e o trecho em texels
    uint64_t tex0 = 0;
    float u0 = 0.f, v0 = 0.f, u1 = 0.f, v1 = 0.f;
    // Barra: indice em Snapshot::anchors do icone de vida que ela substitui (-1 = nenhum); o modo foto a leva junto com a cabeca
    int16_t anchor = -1;
};

// Icone de vida sobre um monstro (Hud::drawHealthIcon): o ponto do mundo que o jogo projetou (cabeca) e o centro do icone na tela do
// jogo (640 x 224) como saiu. O modo foto (wotm_photo.inc) reprojeta o ponto com a camera dele e desloca as barras e o icone.
struct HudAnchor {
    float x = 0.f, y = 0.f;   // centro na tela do jogo
    float w[3] = {0.f, 0.f, 0.f};
    int8_t view = 0;
};

// Luz com lens flare (HierLight, op 5, isOn e hasLensFlare) achada na coleta, no mundo: p = position . matriz do no, beam = beamNormal .
// a mesma matriz (so a rotacao). O hierLightNode faz igual com a pilha de matrizes e o olho do quadro; o modo foto (wotm_photo.inc)
// refaz a lista do lens flare com elas pela camera livre, porque o jogo pausado so tem as que estavam na tela.
struct WorldLight {
    float p[3] = {0, 0, 0}, beam[3] = {0, 0, 0};
    uint32_t node = 0, cs = 0;    // no HierLight e _cs em que a coleta o achou (diagnostico)
    uint32_t flags = 0, id = 0;   // flags da luz (byte baixo: 1 isOn, 2 hasLensFlare, 8 isDirectional, 0x10 bidirecional, 0x80 no infinito)
    float intensity = 0.f, rgb[3] = {0, 0, 0}, bw = 0.f;
};
// Emissor de particula (HierParticleEmitter, op 22) percorrido na coleta: preset e base no mundo, como o Model::emitters da previa
// (e = position . matriz; r0/r2 = linhas 0 e 2 do StaticMat = mathfMatrixFromNormal(n) levadas pela matriz).
struct WorldEmitter { uint32_t node = 0, preset = 0; float e[3] = {0, 0, 0}, r0[3] = {0, 0, 0}, r2[3] = {0, 0, 0}; };

// Centro (pixels do GS, viewGetCenter) e tamanho (viewGetWH) da vista `view`, do viewDef dela.
inline bool viewGeom(const Ram &ram, int view, int &cx, int &cy, int &w, int &h) {
    const uint32_t def = view >= 0 && view < 5 ? ram.u32(addr::viewInfo + 0x14 * uint32_t(view) + 4) & 0x1FFFFFFFu : 0;
    if (!ram.ok(def, 0x20)) return false;
    cx = int(ram.u32(def + 0x18)); cy = int(ram.u32(def + 0x1C)); w = ram.u16(def + 4); h = ram.u16(def + 6);
    return true;
}
// Retangulo chapado de um fade como o emissor o grava no pacote (XYZ2 em 1/16 com 16 bits, RGBAQ de 32 bits por canal, cor
// limitada a 255): cantos em pixels do GS, convertidos para a tela do jogo (640 x 224) como o texto.
inline ScreenRect fadeRect(int x0, int y0, int x1, int y1, uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    constexpr float kOx = 2048.f - 320.f, kOy = 2048.f - 112.f;
    auto px = [](int v, float o) { return float(uint32_t(v * 16) & 0xFFFF) / 16.f - o; };
    ScreenRect o{};
    o.x0 = px(x0, kOx); o.y0 = px(y0, kOy); o.x1 = px(x1, kOx); o.y1 = px(y1, kOy);
    o.r = uint8_t(std::min(255u, r)); o.g = uint8_t(std::min(255u, g)); o.b = uint8_t(std::min(255u, b)); o.a = uint8_t(std::min(255u, a));
    return o;
}
// O fade da vista inteira que rtFadeScreen, uiFadeScreen, TheGame::updateFadeOut/updateFadeOutAndIn e Shell::FadeScreen montam:
// canto (cx - w/2, cy - h/2), lado w x h.
inline bool viewFadeRect(const Ram &ram, int view, uint32_t r, uint32_t g, uint32_t b, uint32_t a, ScreenRect &out) {
    int cx, cy, w, h;
    if (!viewGeom(ram, view, cx, cy, w, h)) return false;
    const int x0 = cx - (w >> 1), y0 = cy - (h >> 1);
    out = fadeRect(x0, y0, x0 + w, y0 + h, r, g, b, a);
    return true;
}


// Quadrados de tela do ScreenPolys (HUD fora do hier: o icone de vida do Hud::drawHealthIcon). Cada fonte de s_sourceObjects (0x20
// bytes: pkt, end, numPolys, headerQw, curQw, obj, rgba, fx) tem um pacote GIF em s_polyBuffer: o polyInitPacket poe o A+D com o TEX0
// do objeto em pkt + 0x30 (ALPHA_1 0x44, TEST_1 0x8D: alfa > 8) e o polyBuildPrim acrescenta, a partir de pkt + 16·headerQw, 5 qwords
// por quadrado: RGBAQ, UV, XYZ2, UV, XYZ2 (SPRITE com textura, UV em texels 12.4). O dmaData fecha as tags, manda e zera numPolys:
// le-se com numPolys > 0, isto e, na entrada do dmaData (hook) ou antes dele (a coleta do replay, na entrada do hier, ja os tem: o
// Hud::addHealthIndicators roda antes). XY como o texto: tela do jogo 640 x 224, centro do GS em 2048.
inline void readScreenPolys(const Ram &ram, std::vector<TextQuad> &out) {
    auto u64 = [&](uint32_t a) { return uint64_t(ram.u32(a)) | (uint64_t(ram.u32(a + 4)) << 32); };
    const int n = int(ram.u32(sym::s_numSourceObjects));
    for (int s = 0; s < n && s < 8; ++s) {   // s_sourceObjects: 0x100 bytes = 8 fontes
        const uint32_t so = sym::s_sourceObjects + 0x20 * uint32_t(s);
        const uint32_t pkt = ram.u32(so), polys = ram.u32(so + 8), hdr = ram.u32(so + 0xC);
        if (!polys || polys > 256 || !ram.ok(pkt + 16 * (hdr + 5 * polys)) || (u64(pkt + 0x38) & 0xFF) != 0x06) continue;
        const uint64_t tex0 = u64(pkt + 0x30);
        for (uint32_t i = 0; i < polys; ++i) {
            const uint32_t q = pkt + 16 * (hdr + 5 * i);
            constexpr float kOx = 2048.f - 320.f, kOy = 2048.f - 112.f;
            TextQuad t{};
            t.r = uint8_t(std::min(255u, ram.u32(q))); t.g = uint8_t(std::min(255u, ram.u32(q + 4)));
            t.b = uint8_t(std::min(255u, ram.u32(q + 8))); t.a = uint8_t(std::min(255u, ram.u32(q + 12)));
            t.u0 = float(ram.u32(q + 16) & 0x3FFF) / 16.f; t.v0 = float(ram.u32(q + 20) & 0x3FFF) / 16.f;
            t.x0 = float(ram.u32(q + 32) & 0xFFFF) / 16.f - kOx; t.y0 = float(ram.u32(q + 36) & 0xFFFF) / 16.f - kOy;
            t.u1 = float(ram.u32(q + 48) & 0x3FFF) / 16.f; t.v1 = float(ram.u32(q + 52) & 0x3FFF) / 16.f;
            t.x1 = float(ram.u32(q + 64) & 0xFFFF) / 16.f - kOx; t.y1 = float(ram.u32(q + 68) & 0xFFFF) / 16.f - kOy;
            t.tex0 = tex0;
            out.push_back(t);
        }
    }
}

// Um quadrado do ScreenPolys pela chamada que o monta, sem ler s_polyBuffer: na entrada do polyBuildPrim(src, x, y, pd, pkt) refaz o
// resolveFX (fx da fonte e do quadrado: centro, espelho em x a partir da borda direita da vista, troca de u e de v; posicao relativa
// ao canto da vista corrente) e o sprite que ele grava (cor da fonte; V = 0x80 - v). Fonte invalida ou pd fora da RAM: false.
inline bool screenPolyFromCall(const Ram &ram, int src, int x, int y, uint32_t pd, TextQuad &t) {
    if (src < 0 || src >= int(ram.u32(sym::s_numSourceObjects)) || src >= 8 || !ram.ok(pd & 0x1FFFFFFFu, 0x24)) return false;
    pd &= 0x1FFFFFFFu;
    const uint32_t so = sym::s_sourceObjects + 0x20u * uint32_t(src);
    const uint32_t view = ram.u32(sym::viewCurView);
    const uint32_t def = view < 5 ? ram.u32(addr::viewInfo + 0x14 * view + 4) & 0x1FFFFFFFu : 0;
    if (!ram.ok(def, 0x20)) return false;
    const int cx = int(ram.u32(def + 0x18)), cy = int(ram.u32(def + 0x1C)), w = ram.u16(def + 4), h = ram.u16(def + 6);
    auto in = [&](int i) { return int(ram.u32(pd + 4 * uint32_t(i))); };   // x0, y0, x1, y1, u0, v0, u1, v1
    const int sfx = ram.u16(so + 0x1C), pfx = ram.u16(pd + 0x20);
    const int fx = (sfx & (0x2 | 0x8)) ^ ((sfx & 0x10) | pfx);
    if (fx & 0x10) { x -= (in(2) - in(0)) / 2; y -= (in(3) - in(1)) / 2; }
    const int ox = (fx & 0x8) ? cx + w / 2 - (x + (in(2) - in(0))) : x + (cx - w / 2), oy = y + (cy - h / 2);
    const int u0 = (fx & 0x2) ? in(6) : in(4), u1 = (fx & 0x2) ? in(4) : in(6);
    const int v0 = (fx & 0x4) ? in(7) : in(5), v1 = (fx & 0x4) ? in(5) : in(7);
    const uint32_t gu0 = uint32_t(u0 * 16), gv0 = uint32_t((0x80 - v0) * 16), gu1 = uint32_t(u1 * 16), gv1 = uint32_t((0x80 - v1) * 16);
    const uint32_t gx0 = uint32_t((in(0) + ox) * 16), gy0 = uint32_t((in(1) + oy) * 16), gx1 = uint32_t((in(2) + ox) * 16), gy1 = uint32_t((in(3) + oy) * 16);
    constexpr float kOx = 2048.f - 320.f, kOy = 2048.f - 112.f;   // como readScreenPolys
    t = TextQuad{};
    t.r = ram.u8(so + 0x18); t.g = ram.u8(so + 0x19); t.b = ram.u8(so + 0x1A); t.a = ram.u8(so + 0x1B);
    t.u0 = float(gu0 & 0x3FFF) / 16.f; t.v0 = float(gv0 & 0x3FFF) / 16.f;
    t.u1 = float(gu1 & 0x3FFF) / 16.f; t.v1 = float(gv1 & 0x3FFF) / 16.f;
    t.x0 = float(gx0 & 0xFFFF) / 16.f - kOx; t.y0 = float(gy0 & 0xFFFF) / 16.f - kOy;
    t.x1 = float(gx1 & 0xFFFF) / 16.f - kOx; t.y1 = float(gy1 & 0xFFFF) / 16.f - kOy;
    const uint32_t obj = ram.u32(so + 0x14) & 0x1FFFFFFFu;   // _hierobject: TEX0 em +0x70 (o polyInitPacket o copia para o A+D)
    t.tex0 = ram.ok(obj, 0x78) ? uint64_t(ram.u32(obj + 0x70)) | (uint64_t(ram.u32(obj + 0x74)) << 32) : 0;
    return true;
}

// Texto do jogo pelas chamadas da fonte (common/font), sem ler o fontPacket: os ganchos (wotm_overrides.cpp) seguem o que o jogo
// manda montar e refazem cada glifo com a mesma conta do fontBuildPrim, a partir do estado da fonte (fonts[2], 0x38 cada: celula do
// atlas em +0/+2, tamanho do glifo no atlas em +4/+6, tamanho na tela em +8/+0xA, cursor em +0x10/+0x12, cor em +0x18) e da vista
// corrente (centro e tamanho do viewDef). Posicoes e UV em 1/16 (subpixel do GS), XY com o centro do GS em 2048 como no pacote.
//  - fontInitPacket(pkt, vram): TEX0 do atlas do boot (vram - 0x840 em TBP0, vram - 0x40 em CBP, T8H 512x512) e cor padrao.
//  - fontSetTexId(no): TEX0 do atlas do .RTX da fase (no + 0x70) e modo de atlas 1 (D_006F8D38).
//  - fontSetColorGifTag(fonte): a cor da fonte passa a valer para os glifos seguintes, de qualquer fonte (a tag de cor e do pacote).
//    Chamada com o pacote vazio (logo depois do despacho), ela reescreve a cor do cabecalho, que vale no comeco de cada quadro.
//  - fontBuildPrim(fonte, c): um glifo. Atlas 0: 8 colunas a partir de '!', minusculas viram maiusculas; atlas 1: 16 colunas.
//  - fontDmaFontData: fecha o quadro (o texto mandado ao GS) e volta a cor a do cabecalho.
struct FontText {
    static constexpr uint32_t kFonts = 0x00735890;      // D_00735890: Font fonts[2]
    static constexpr uint32_t kAtlasMode = 0x006F8D38;  // D_006F8D38: 0 = atlas do boot, 1 = atlas do .RTX (fontSetTexId)
    static constexpr uint32_t kPktState = 0x006F8D3C;   // D_006F8D3C: 0 = sem pacote, 1 = ultima tag e de cor, 2 = de sprites
    static constexpr uint32_t kPktCount = 0x006F8D30, kPktHeader = 0x006F8D44;   // qwords no pacote; tamanho do cabecalho
    uint64_t tex0 = 0;
    uint8_t col[4] = {0x80, 0x80, 0x40, 0x80}, headerCol[4] = {0x80, 0x80, 0x40, 0x80};
    std::vector<TextQuad> building;   // glifos desde o ultimo fontDmaFontData

    void initPacket(uint32_t vram) {
        const uint64_t tbp = uint16_t(int32_t((vram - 0x840u) << 10) >> 16), cbp = uint16_t(int32_t((vram - 0x40u) << 10) >> 16);
        tex0 = tbp | (cbp << 37) | (0xBBA8ull << 19) | 0x8000ull | (0x8000ull << 46);
        static constexpr uint8_t kDef[4] = {0x80, 0x80, 0x40, 0x80};
        std::memcpy(col, kDef, 4);
        std::memcpy(headerCol, kDef, 4);
        building.clear();
    }
    void setTexId(const Ram &ram, uint32_t node) {
        node &= 0x1FFFFFFFu;
        if (ram.ok(node, 0x78) && !(ram.u32(node) & 0x3F)) tex0 = uint64_t(ram.u32(node + 0x70)) | (uint64_t(ram.u32(node + 0x74)) << 32);
    }
    // Na entrada do fontSetColorGifTag (a cor ja esta na fonte; o estado do pacote ainda e o de antes).
    void setColor(const Ram &ram, int font) {
        const uint32_t f = kFonts + 0x38u * uint32_t(font) + 0x18;
        if (font < 0 || font > 1) return;
        for (int i = 0; i < 4; ++i) col[i] = ram.u8(f + i);
        if (ram.u32(kPktState) == 1 && ram.u32(kPktCount) == ram.u32(kPktHeader)) std::memcpy(headerCol, col, 4);
    }
    // Na entrada do fontBuildPrim.
    void glyph(const Ram &ram, int font, int ch) {
        if (font < 0 || font > 1 || building.size() >= 4096) return;
        const uint32_t f = kFonts + 0x38u * uint32_t(font);
        const uint32_t view = ram.u32(sym::viewCurView);
        const uint32_t def = view < 5 ? ram.u32(addr::viewInfo + 0x14 * view + 4) & 0x1FFFFFFFu : 0;
        if (!ram.ok(def, 0x20)) return;
        const int cx = int(ram.u32(def + 0x18)), cy = int(ram.u32(def + 0x1C)), w = ram.u16(def + 4), h = ram.u16(def + 6);
        int c = int8_t(ch);
        const bool rtx = ram.u32(kAtlasMode) != 0;
        if (!rtx && unsigned(c - 0x61) < 0x1Au) c = int8_t(c - 0x20);
        int row, column, base;
        if (rtx) { row = c >> 4; column = c & 15; base = 0x10; }
        else { const int a = c - 0x21; row = a >> 3; column = a & 7; base = 8; }
        const uint16_t u0 = uint16_t(ram.u16(f) * column + base), v0 = uint16_t(ram.u16(f + 2) * row + base);
        const uint16_t u1 = uint16_t(ram.u16(f + 4) + u0), v1 = uint16_t(ram.u16(f + 6) + v0);
        const uint16_t x0 = uint16_t(ram.u16(f + 0x10) + cx * 16 - w * 8), y0 = uint16_t(ram.u16(f + 0x12) + cy * 16 - h * 8);
        const uint16_t x1 = uint16_t(ram.u16(f + 8) + x0), y1 = uint16_t(ram.u16(f + 0xA) + y0);
        constexpr float kOx = 2048.f - 320.f, kOy = 2048.f - 112.f;   // como o Scene::updateText
        TextQuad t{};
        t.u0 = float(u0 & 0x3FFF) / 16.f; t.v0 = float(v0 & 0x3FFF) / 16.f;
        t.u1 = float(u1 & 0x3FFF) / 16.f; t.v1 = float(v1 & 0x3FFF) / 16.f;
        t.x0 = float(x0) / 16.f - kOx; t.y0 = float(y0) / 16.f - kOy;
        t.x1 = float(x1) / 16.f - kOx; t.y1 = float(y1) / 16.f - kOy;
        t.r = col[0]; t.g = col[1]; t.b = col[2]; t.a = col[3];
        t.tex0 = tex0;
        building.push_back(t);
    }
    // Na entrada do fontDmaFontData: true = o quadro foi fechado (glifos em `out`).
    bool sent(const Ram &ram, std::vector<TextQuad> &out) {
        if (ram.u32(kPktState) == 0) return false;
        out.swap(building);
        building.clear();
        std::memcpy(col, headerCol, 4);
        return true;
    }
};

// Bloqueador de sombra ativo (Scene::collectShadows / drawShadows): entrada de psBlkLst e os objetos que projetam a silhueta.
struct ShadowBlocker {
    int index = 0;               // entrada de psBlkLst
    float origin[3] = {0, 0, 0}; // cs->trans
    float extent = 0;            // meio lado do quadro de luz (raio do no * 1,1)
    uint32_t shadowCs = 0;       // entrada +0x14 (psBlockerSetShadowCs): so os objetos desse _cs recebem a sombra (psObjectCull)
    std::vector<DrawItem> casters;
    std::vector<uint32_t> casterNodes;
};
struct ParaLights { float dir[4][3] = {}; float col[3][3] = {}; };   // luzes paralelas (Scene::readParaLights)
struct Fog { float fa = 0.f, maxVal = 255.f, fb = 0.f; float col[3] = {0, 0, 0}; };   // neblina (Scene::readFog, Scene::fogF)

// Tudo o que o desenho le do quadro do jogo, copiado da RAM de uma vez (itens com a paleta copiada, particulas, sombras, luzes,
// neblina). Na luta e no menu e tirado na thread do EE na entrada de timerWaitForMinUpdateRate, com o quadro pronto
// (wotmCaptureFrame); os desenhos ate o proximo quadro do jogo repetem este, sem ler a RAM que o jogo esta reescrevendo.
struct Snapshot {
    std::vector<Sprite> sprites;
    std::vector<DrawItem> items, sky, overlay, hud, hp, hpOverlay;
    std::vector<FxSystem> fx;
    std::shared_ptr<const uint8_t> fxRam;  // copia de sombra da RAM das particulas (Particles::shadowRam, ao vivo); nullptr = ler a RAM passada ao draw
    size_t fxRamSize = 0;                  // tamanho do buffer de fxRam (o da RAM)
    Particles::FlareFrame flares;       // lens flare do quadro (Particles::collectFlares); sem ele o lado que desenha nao tem a lista
    std::vector<ShadowBlocker> shadows;
    std::vector<ScreenRect> screenRects;   // fades do quadro (wotmScreenRect, nos hooks dos emissores; nao vem do collect)
    std::vector<TextQuad> screenPolys;     // HUD do ScreenPolys (readScreenPolys: no takeSnapshot e no hook do dmaData)
    std::vector<HudAnchor> anchors;        // ponto do mundo de cada icone de vida do quadro (barras e icone seguem a camera no modo foto)
    std::vector<WorldLight> worldLights;   // luzes com lens flare percorridas pela coleta (modo foto)
    std::vector<WorldEmitter> emitters;    // emissores de particula percorridos pela coleta (modo foto: fogo do Agamo)
    ParaLights lights;
    Fog fog;
    GameCamera cam;
    ViewCtx vctx;       // vista do jogo deste instantaneo (tela dividida: 0 = esquerda, 1 = direita; quadrantes: v & 1 coluna, v >> 1 linha)
    uint32_t g = 0;     // contador de quadros do jogo
    // Carimbo para a interpolacao (Scene::showBlend): contagem de fields do inicio do quadro (D_006F8DC0; quadro de 2 fields = vao de 2)
    // e relogio do host na captura (segundos, steady_clock). fields = 0: sem carimbo (replay, coleta local)
    uint32_t fields = 0;
    double hostT = 0.0;
    uint32_t culled = 0;  // OBJECTs e GROUPs que o collect recortou pela vista (a fase existe mesmo com poucos itens na tela)
    bool valid = false;
    // Tela dividida: as vistas 1 a 3 do mesmo quadro (coletadas na saida do hier(v, 0)); views[v - 1] = nullptr: sem a vista v
    std::shared_ptr<Snapshot> views[3];
};

class Scene {
public:
    // Largura do quadro nativo / largura 4:3 da mesma altura (widescreen Hor+; 1 = tela original). Preenche: wotm_live.inc
    float wide = 1.f;
    // Corte por esfera contra a vista do jogo, como o hierTraverseAsm (Scene::outOfView); o viewer com camera livre o desliga.
    bool fovCull = true;
    // Raio da esfera no corte multiplicado por isto: com a interpolacao a vista desenhada fica ate um quadro atras da do corte (a de
    // B), e o que B cortou na borda sumiria durante um giro (wotm_live.inc liga 1,2 com a interpolacao)
    float cullMargin = 1.f;
    // Vista do jogo que collect segue e que draw desenha (tela dividida; ViewCtx). A coleta a recebe de quem chama; o show a copia do
    // instantaneo.
    ViewCtx vctx;
    std::vector<DrawItem> items;
    std::vector<DrawItem> sky;   // ceu / nuvens (desenhados antes, sem profundidade, em skyCs->trans)
    std::vector<DrawItem> overlay;   // icones de botao (McButtons): por cima de tudo, sem teste de profundidade
    std::vector<DrawItem> hud;       // elementos de HUD (ids 0x1C20..0x2133): por cima da cena, sem profundidade, depois dos icones (DrawItem::hudId = id do objeto)
    // Lista de alta prioridade (CsPool::m_HPActiveList; hier(v, 1) -> hierTraceHPCsList), fora o HUD: moldura dos dialogos (shell->m_cs610,
    // pauseLevelOverlay e cia.) e icones de botao deles. O ShellRtLoop::endField a desenha depois da fila do gsPkt (fade) e antes do
    // texto; no rtMain e no displayDialog vem antes dos dois. drawHp, chamado pelo lado que apresenta depois dos fades sob o texto.
    std::vector<DrawItem> hp, hpOverlay;   // hpOverlay: os icones de botao dela (fadeByNode)
    std::vector<Sprite> sprites;   // particulas (destroços, poeira) lidas de fx[]; desenhadas como sprites voltados para a camera
    std::vector<TextQuad> text;    // glifos do quadro (updateText), desenhados por drawQuads depois da cena 3D e do screenPolys
    std::vector<ScreenRect> screenRects;   // fades (Snapshot::screenRects), desenhados por drawScreenRects por ultimo
    std::vector<TextQuad> screenPolys;     // HUD do ScreenPolys (Snapshot::screenPolys), desenhado por drawScreenPolys antes do texto
    std::vector<WorldLight> worldLights;   // Snapshot::worldLights (o walk as acha nos nos op 5)
    std::vector<WorldEmitter> emitters;    // Snapshot::emitters (nos op 22)
    uint32_t visited = 0;
    uint32_t culled = 0;   // OBJECTs e GROUPs fora da vista no collect (Snapshot::culled)
    uint32_t byOp[64] = {};

    gs::Vram vram;   // so para depuracao: a VRAM do dump guarda paletas velhas (restos do menu)

    // Paletas da fase, lidas do `.RTX` do disco: entradas de `(palavra0 >> 2) * 16` bytes com cabecalho de 16 bytes (descritor GS
    // em +8: DBP relativo no meia-palavra +0xA, TW/TH nos bits 40..47) e o corpo em ordem logica de paleta (entrada i = palavra i):
    // 16x16 CT32 = 256 cores, 8x2 CT32 = 16 cores. O TEX0.CBP do objeto e `base + DBP`, com base = tempVramTexAddr >> 6.
    // A chave e o CBP ABSOLUTO (base do arquivo + DBP). Base do nivel = tempVramTexAddr >> 6; a do monstro i = fileStatus.maxTexAddr[i-1]
    // (texmResInit empilha os arquivos .RTX na ordem em que o Shell os carregou: nivel, jogadores, IAs).
    std::unordered_map<uint32_t, std::vector<uint32_t>> rtxPal;
    // Imagens residentes do .RTX (PSMT8H/4HL/4HH: telas, fontes, logos do menu, retratos do HUD): chave (psm << 16) | DBP. Os pixels
    // da RAM em `texInfo[id]` nao servem (o ponteiro aponta para outra coisa); o TBP0 do TEX0 e o DBP da entrada coincidem. Nos
    // arquivos de monstro o DBP e relativo: o texmResRelocate soma o fim da area residente do arquivo anterior (`resBase`).
    std::unordered_map<uint32_t, std::vector<uint8_t>> rtxImg;

    void clearRtx() { rtxPal.clear(); rtxImg.clear(); rtxImgSrc_.clear(); }

    // ---- texturas de mod em PNG (wotm_texmods.hpp)
    struct ImgSrc { std::string stem; uint32_t texId; };
    std::unordered_map<uint32_t, ImgSrc> rtxImgSrc_;   // chave do rtxImg -> arquivo e texId do disco
    std::string discRoot_;                              // raiz do disco extraido, com a barra (loadResources)
    std::map<std::string, std::map<uint32_t, uint32_t>> texPackets_;   // ARQUIVO -> deslocamento do pacote no .TEX -> texId do disco

    // Caminho do disco ("MON/LIZARD0.RTX"): o arquivo de mods/ (wotm_mods.hpp) se houver, senao o da raiz
    std::string discPath(const std::string &rel) const {
        std::string p = mods::Registry::get().hostPath(rel);
        return p.empty() ? discRoot_ + mods::Registry::get().aliasOf(rel) : p;   // traje de mod: o arquivo do traje base
    }

    // PNG de mod no lugar da textura do objeto (texId do texInfo global + TEX0). Residentes (T8H/4HL/4HH): pelo DBP, como o
    // decodeResident. As outras: o pacote de upload em texInfo[texId] esta dentro do .TEX carregado em fileStatus.texAddr[i]; o
    // deslocamento nele da o texId do disco (o pacote na RAM ja tem o id relocado). O nome do arquivo i e fileStatus.names[i].
    // hiAlpha: a textura original passa de 0x80 (texmods::loadPng)
    bool modTexture(const Ram &ram, uint32_t texId, const gs::Tex0 &t0, bool tcc, bool rawAlpha, std::vector<uint32_t> &px, uint32_t &w, uint32_t &h,
                    bool hiAlpha) {
        texmods::Index &ix = texmods::Index::get();
        if (ix.empty()) return false;
        char what[64];
        if (t0.psm == gs::T8H || t0.psm == gs::T4HL || t0.psm == gs::T4HH) {
            const auto it = rtxImgSrc_.find((t0.psm << 16) | t0.tbp0);
            if (it == rtxImgSrc_.end()) return false;
            const texmods::Png *png = ix.find(it->second.stem, true, it->second.texId);
            std::snprintf(what, sizeof what, "%s.RTX t%u", it->second.stem.c_str(), it->second.texId);
            return png && texmods::loadPng(*png, what, 1u << t0.tw, 1u << t0.th, rawAlpha, tcc, px, w, h, hiAlpha);
        }
        if (!texId || texId >= 1000) return false;
        const uint32_t fs = addr::fileStatus;
        const uint32_t ptr = ram.u32(addr::texInfo + 16 * texId) & 0x0FFFFFFF;
        const int nNames = ram.u8(fs + 0x74);
        // .TEX (texAddr @0x00, texLoaded @0x38) e, para as texturas comuns que moram no .RTX, resAddr @0x78 / resLoaded @0xB0
        for (const bool rtx : {false, true}) {
            const uint32_t tab = rtx ? fs + 0x78 : fs;
            const int n = std::min<int>(ram.u8(rtx ? fs + 0xB0 : fs + 0x38), std::min(nNames, 14));
            for (int i = 0; i < n; ++i) {
                const uint32_t a = ram.u32(tab + 4 * uint32_t(i)) & 0x0FFFFFFF, b = ram.u32(tab + 4 * uint32_t(i + 1)) & 0x0FFFFFFF;
                if (ptr < a || ptr >= b) continue;
                std::string stem;
                for (int c = 0; c < 8; ++c) {
                    const char ch = char(ram.u8(fs + 0xB1 + 9 * uint32_t(i) + uint32_t(c)));
                    if (!ch) break;
                    stem += char(std::toupper(static_cast<unsigned char>(ch)));
                }
                if (stem.empty()) return false;
                const std::string file = stem + (rtx ? ".RTX" : ".TEX");
                auto pk = texPackets_.find(file);
                if (pk == texPackets_.end()) {
                    std::vector<uint8_t> d;
                    for (const char *dir : {"LVL/", "MON/", "SHELL/", ""}) {
                        d = texmods::readUnzipped(discPath(*dir ? dir + file : stem + "/" + file));
                        if (!d.empty()) break;
                    }
                    pk = texPackets_.emplace(file, texmods::packetIds(d)).first;
                }
                const auto id = pk->second.find(ptr - a);
                if (id == pk->second.end()) return false;
                const texmods::Png *png = ix.find(stem, rtx, id->second);
                if (!png) return false;
                const UploadHdr hd = UploadHdr::read(ram, ptr);
                std::snprintf(what, sizeof what, "%s t%u", file.c_str(), id->second);
                return texmods::loadPng(*png, what, hd.w, hd.h, rawAlpha, tcc, px, w, h, hiAlpha);
            }
        }
        return false;
    }
    // Trecho em texels (u0, v0, largura, altura) da textura original na textura carregada, que pode ser um PNG de outro tamanho
    static Rectangle texelRect(const Texture2D &t, uint64_t tex0, Rectangle r) {
        const gs::Tex0 t0 = gs::Tex0::decode(tex0);
        const float kx = float(t.width) / float(1u << t0.tw), ky = float(t.height) / float(1u << t0.th);
        return Rectangle{r.x * kx, r.y * ky, r.width * kx, r.height * ky};
    }

    // Carrega o nivel e os monstros do jogo atual a partir do disco extraido (`root` contem LVL/ e MON/). Idempotente: so refaz quando
    // os nomes/bases em fileStatus mudam. Devolve verdadeiro se achou ao menos o arquivo do nivel.
    bool loadResources(const Ram &ram, const std::string &rootArg, std::string *log = nullptr) {
        const uint32_t fs = addr::fileStatus;
        const int n = ram.u8(fs + 0xB0);
        if (n < 1 || n > 14) return false;
        uint32_t sig = ram.u32(addr::tempVramTexAddr) * 2654435761u;
        std::vector<std::string> names;
        names.resize(size_t(n));
        for (int i = 0; i < n; ++i) {
            for (int c = 0; c < 8; ++c) {
                const char ch = char(ram.u8(fs + 0xB1 + 9 * i + c));
                if (!ch) break;
                names[size_t(i)] += char(std::toupper(static_cast<unsigned char>(ch)));
            }
            sig = (sig ^ ram.u16(fs + 0x14C + 2 * i)) * 16777619u;
            for (char ch : names[size_t(i)]) sig = (sig ^ uint8_t(ch)) * 16777619u;
        }
        if (resSig_ == sig && !rtxPal.empty()) return true;
        if (names[0].empty()) return false;
        // Sem raiz explicita: WOTM_DISC (como em wotm_fmv.hpp), senao o diretorio atual.
        std::string root = rootArg;
        if (root.empty())
            if (const char *d = std::getenv("WOTM_DISC")) root = d;
        std::string sep = root.empty() || root.back() == '/' || root.back() == '\\' ? "" : "/";
        discRoot_ = root + sep;
        auto path = [&](const std::string &rel) { return discPath(rel); };
        rtxPal.clear(); rtxImg.clear(); rtxImgSrc_.clear();
        const uint32_t base0 = ram.u32(addr::tempVramTexAddr) >> 6;
        bool lvl = loadRtx(path("LVL/" + names[0] + ".RTX").c_str(), base0);
        // O menu (SHELL) mora em SHELL/SHELL.RTX, nao em LVL/.
        if (!lvl) lvl = loadRtx(path(names[0] + "/" + names[0] + ".RTX").c_str(), base0);
        if (!lvl) lvl = loadRtx(path("SHELL/" + names[0] + ".RTX").c_str(), base0);   // LOAD, UI, SHELL2, PRESHELL
        if (log) *log = names[0] + (lvl ? "" : " (nao achado)");
        for (int i = 1; i < n; ++i) {
            if (names[size_t(i)].empty()) continue;
            const bool ok = loadRtx(path("MON/" + names[size_t(i)] + ".RTX").c_str(), ram.u16(fs + 0x14C + 2 * (i - 1)), true,
                                    ram.u16(fs + 0x168 + 2 * (i - 1)), names[size_t(i)]);   // maxTexAddr e maxResAddr do arquivo anterior
            if (log) *log += " " + names[size_t(i)] + (ok ? "" : "(?)");
        }
        invalidate();
        resSig_ = sig;
        return lvl;
    }

    // base: soma no DBP das paletas (base do arquivo); resBase: soma no DBP das imagens residentes (0 na fase e no menu; num monstro,
    // fileStatus.maxResAddr do arquivo anterior, como no texmResRelocate). name: o nome que o jogo carregou ("LIZARD4"); vazio = o do
    // arquivo. Num traje de mod o caminho e o do traje base (LIZARD2.RTX) ou o do arquivo do mod (MECHA.RTX), e os PNGs da skin e o
    // retrato do custom sao do nome do jogo.
    bool loadRtx(const char *path, uint32_t base, bool append = false, uint32_t resBase = 0, const std::string &name = {}) {
        if (!append) { rtxPal.clear(); rtxImg.clear(); rtxImgSrc_.clear(); }
        std::string stem = name.empty() ? std::string(path) : name;   // "LIZARD0": nome das imagens residentes de mod (texmods, <arquivo>_rtx_t<id>.png)
        stem = stem.substr(stem.find_last_of("/\\") + 1);
        stem = stem.substr(0, stem.find('.'));
        for (char &c : stem) c = char(std::toupper(static_cast<unsigned char>(c)));
        std::vector<uint8_t> raw;
        if (FILE *f = std::fopen(path, "rb")) {
            std::fseek(f, 0, SEEK_END);
            raw.resize(size_t(std::ftell(f)));
            std::fseek(f, 0, SEEK_SET);
            if (std::fread(raw.data(), 1, raw.size(), f) != raw.size()) raw.clear();
            std::fclose(f);
        }
        if (raw.size() < 34) return false;
        std::vector<uint8_t> data;
        static const uint8_t kMagic[4] = {'I', 'E', 3, 4};
        if (std::memcmp(raw.data(), kMagic, 4) == 0) {   // zip com o magic trocado, um arquivo em deflate cru
            uint32_t csize = 0;
            uint16_t nlen = 0, xlen = 0;
            std::memcpy(&csize, raw.data() + 18, 4);
            std::memcpy(&nlen, raw.data() + 26, 2);
            std::memcpy(&xlen, raw.data() + 28, 2);
            const size_t off = 30 + nlen + xlen;
            if (off + csize > raw.size()) return false;
            int outSize = 0;
            unsigned char *d = DecompressData(raw.data() + off, int(csize), &outSize);
            if (!d || outSize <= 0) return false;
            data.assign(d, d + outSize);
            MemFree(d);
        } else {
            data = raw;
        }
        size_t off = 0, added = 0;
        // A entrada com tamanho 0 e a ultima e tambem vale (vai ate o fim do arquivo): nos monstros e o retrato do HUD (T8H 128x64).
        bool last = false;
        while (!last && off + 16 <= data.size()) {
            uint32_t w[4];
            std::memcpy(w, data.data() + off, 16);
            last = w[0] == 0;
            const size_t sz = last ? data.size() - off : size_t(w[0] >> 2) * 16;
            if (sz < 16 || off + sz > data.size()) break;
            const uint64_t desc = uint64_t(w[2]) | (uint64_t(w[3]) << 32);
            const uint32_t psm = uint32_t(desc >> 32) & 0x3F, tw = uint32_t(desc >> 40) & 15, th = uint32_t(desc >> 44) & 15;
            if (psm == 0 && ((tw == 4 && th == 4) || (tw == 3 && th == 1))) {
                std::vector<uint32_t> p((sz - 16) / 4);
                std::memcpy(p.data(), data.data() + off + 16, p.size() * 4);
                rtxPal[base + ((w[2] >> 16) & 0xFFFF)] = std::move(p);
                ++added;
            } else if ((psm == gs::T8H || psm == gs::T4HL || psm == gs::T4HH) && sz > 0x80) {
                const uint32_t key = (psm << 16) | ((resBase + ((w[2] >> 16) & 0xFFFF)) & 0xFFFF);
                rtxImg[key].assign(data.begin() + long(off) + 0x80, data.begin() + long(off + sz));
                rtxImgSrc_[key] = {stem, w[2] & 0xFFFF};
            }
            off += sz;
        }
        return added > 0;
    }

    // Filtro das texturas do jogo (opcao TEXTURE FILTER, WOTM_TEXFILTER): 0 bilinear sem mipmaps (o do GS), 1 trilinear (mipmaps), 2..16
    // anisotropico com trilinear. Vale para as que existem e para as que vierem; o desenho chama a cada quadro e so age quando muda.
    void setTexFilter(int f) {
        if (f == texFilter_) return;
        texFilter_ = f;
        for (auto &kv : tex_) applyTexFilter(kv.second);
        std::fprintf(stderr, "[native] filtro de texturas: %s (%zu texturas)\n", f <= 0 ? "bilinear" : f == 1 ? "trilinear" : "anisotropico", tex_.size());
    }

    void invalidate() {
        cache_.clear();
        for (auto &kv : tex_) if (kv.second.id) UnloadTexture(kv.second);
        tex_.clear();
        for (auto &kv : fontTex_) if (kv.second.id) UnloadTexture(kv.second);
        fontTex_.clear();
        for (DustSrc &s : dustSrc_) s.ok = false;
        for (uint64_t &h : dustHash_) h = 0;
        fx.resetFlareDebounce();   // fracao visivel do quadro anterior e do nivel velho
    }

    // TEX0 do objeto (gsTexCtx[slot]); 0 quando o objeto nao tem textura ou a secao e kSlotNoTex
    static uint64_t objectTex0(const Ram &ram, uint32_t node, uint32_t slot = 0) {
        const uint32_t n = slot == kSlotNoTex ? 0 : ram.u16(node + 0x5A);
        if (n == 0) return 0;
        if (slot >= n || slot >= 5) slot = 0;
        return uint64_t(ram.u32(node + 0x70 + 0x30 * slot)) | (uint64_t(ram.u32(node + 0x74 + 0x30 * slot)) << 32);
    }
    // texId do slot (texId[5] em +0x50); slot invalido cai no 0; kSlotNoTex = 0
    static uint32_t objectTexId(const Ram &ram, uint32_t node, uint32_t slot = 0) {
        if (slot == kSlotNoTex) return 0;
        if (slot >= ram.u16(node + 0x5A) || slot >= 5) slot = 0;
        return ram.u16(node + 0x50 + 2 * slot);
    }

    // Chave de textura de um objeto: id no `texInfo` + paleta (CBP/CSA/CPSM do TEX0) + TCC = 0 (bit 30; decodeTex). 0 = sem textura.
    // texId 0 com TEX0 = o buffer em exibicao, isto e, o quadro anterior (rtMain -> texmPrepTexInfoZero; texmGetTextureAddrs monta
    // PSMCT24 640 de largura, janela 1024x256): chave kScreenKey, textura de screenTexture.
    static constexpr uint64_t kScreenKey = 1ull << 63;
    static uint64_t texKey(const Ram &ram, uint32_t node, uint32_t slot = 0) {
        const uint64_t t0 = objectTex0(ram, node, slot);
        const uint32_t tid = objectTexId(ram, node, slot);
        if (!t0) return 0;
        if (!tid) return kScreenKey;
        const gs::Tex0 t = gs::Tex0::decode(t0);
        const uint64_t noTcc = (t0 >> 34) & 1 ? 0 : 1ull << 30;
        return (uint64_t(tid) << 40) | noTcc | (uint64_t(t.cbp) << 12) | (uint64_t(t.csa) << 4) | t.cpsm | (1ull << 62);
    }
    // No CLOAK (DrawItem::env 3) o pktBldEnvMapDma troca o TEX0 de cada slot com textura pelo do texId 0 (texmGetTextureAddrs).
    static uint64_t texKey(const Ram &ram, const DrawItem &it, uint32_t slot) {
        const uint64_t k = texKey(ram, it.node, slot);
        return k && it.env == 3 ? kScreenKey : k;
    }

    // Preenche a paleta de 256 entradas. O GS le CLUTs de 256 cores em CSM1 com os bits 3 e 4 do indice trocados (entradas 8-15 e
    // 16-23 de cada grupo de 32); o .RTX guarda o upload cru, entao a leitura tem que aplicar a troca (sem ela: granulado em degrades).
    // rawAlpha: alfa cru do GS (0x80 = 1,0), para o que o Scene::draw mistura como o GS; senao, 0..255 com 0x80 saturado (fixAlpha).
    static void fillPal(uint32_t pal[256], const std::vector<uint32_t> &src, bool is8bit, bool rawAlpha = false) {
        for (size_t i = 0; i < 256 && i < src.size(); ++i) {
            const size_t k = (is8bit && src.size() >= 256) ? ((i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1)) : i;
            pal[i] = rawAlpha ? src[k] : gs::fixAlpha(src[k]);
        }
    }

    // Textura do objeto: os pixels vem do pacote de upload apontado por `texInfo[texId]` (pixels lineares a partir de +0x80,
    // cabecalho em UploadHdr); a paleta vem do .RTX (carregado junto com a fase).
    // TEX0.TCC = 0 (so RGB): o GS usa o alfa do vertice e ignora o da textura; aqui o texel sai com alfa 1,0 (a tela do drive-in do
    // menu, tex 76, com vertices de alfa 0 e 0x80 por cima do logo).
    bool decodeTex(const Ram &ram, uint32_t node, std::vector<uint32_t> &px, uint32_t &w, uint32_t &h, uint32_t slot = 0, bool rawAlpha = false) const {
        const uint64_t t0v = objectTex0(ram, node, slot);
        if (!t0v) return false;
        if (!decodeTexRgba(ram, node, t0v, px, w, h, slot, rawAlpha)) return false;
        if (!((t0v >> 34) & 1)) {
            const uint32_t one = rawAlpha ? 0x80000000u : 0xFF000000u;
            for (uint32_t &c : px) c = (c & 0x00FFFFFFu) | one;
        }
        return true;
    }
    bool decodeTexRgba(const Ram &ram, uint32_t node, uint64_t t0v, std::vector<uint32_t> &px, uint32_t &w, uint32_t &h, uint32_t slot, bool rawAlpha) const {
        const gs::Tex0 t0 = gs::Tex0::decode(t0v);
        const uint32_t ptr = ram.u32(addr::texInfo + 16 * objectTexId(ram, node, slot)) & 0x0FFFFFFF;
        if (t0.psm == gs::T8H || t0.psm == gs::T4HL || t0.psm == gs::T4HH) {
            uint32_t pal[256] = {};
            if (decodeResident(t0, px, w, h, rawAlpha, pal)) return true;
            // Paleta sem a imagem no .RTX: a RAM em `ptr` (nao confiavel).
            if (rtxPal.find(t0.cbp) == rtxPal.end() || !ram.ok(ptr + 0x80)) return false;
            return gs::decodeUpload(ram.p + ptr, ram.size - ptr, w, h, t0.psm == gs::T8H ? gs::T8 : gs::T4, t0.csa, pal, px);
        }
        return decodeUploadTex(ram, ptr, t0, px, w, h, false, rawAlpha);
    }

    // Texturas residentes em PSMT8H/4HL/4HH (telas e logos do menu, fontes, retratos, icones do HUD): `texInfo[id]` aponta para outra
    // coisa; os pixels vem do .RTX do disco, entrada com DBP == TEX0.TBP0 e mesmo psm (4HL e 4HH repartem o DBP: sao os dois nibbles).
    // 8 bits, ou 4 bits empacotados (nibble baixo primeiro); tamanho = TW/TH do TEX0. `pal` sai preenchida se a paleta existe.
    bool decodeResident(const gs::Tex0 &t0, std::vector<uint32_t> &px, uint32_t &w, uint32_t &h, bool rawAlpha, uint32_t pal[256]) const {
        if (t0.psm != gs::T8H && t0.psm != gs::T4HL && t0.psm != gs::T4HH) return false;
        const auto it = rtxPal.find(t0.cbp);
        if (it == rtxPal.end()) return false;
        fillPal(pal, it->second, t0.psm == gs::T8H, rawAlpha);
        w = 1u << t0.tw; h = 1u << t0.th;
        const auto im = rtxImg.find((t0.psm << 16) | t0.tbp0);
        return im != rtxImg.end() &&
               gs::decodeUpload(im->second.data(), im->second.size(), w, h, t0.psm == gs::T8H ? gs::T8 : gs::T4, t0.csa, pal, px);
    }

    // Pixels do pacote de upload em `ptr` (= texInfo[texId]; cabecalho em UploadHdr, pixels lineares a partir de +0x80) com a paleta do .RTX pelo CBP do TEX0. Serve aos objetos (decodeTex) e as particulas (particleTexture).
    bool decodeUploadTex(const Ram &ram, uint32_t ptr, const gs::Tex0 &t0, std::vector<uint32_t> &px, uint32_t &w, uint32_t &h,
                         bool grayFallback = false, bool rawAlpha = false) const {
        if (!ram.ok(ptr + 0x80)) return false;
        uint32_t pal[256] = {};
        const UploadHdr hd = UploadHdr::read(ram, ptr);
        const uint32_t psm = hd.psm;
        if (psm == gs::T8 || psm == gs::T4) {
            // A paleta vem do .RTX carregado (loadRtx); sem ela a textura cai para a cor de vertice.
            const auto it = rtxPal.find(t0.cbp);
            if (it != rtxPal.end()) fillPal(pal, it->second, psm == gs::T8, rawAlpha);
            else if (grayFallback) {
                // Particulas: muitas texturas sao mascaras de luminancia/alfa; sem a paleta, rampa cinza com alfa = indice (1,0 no fim)
                const uint32_t n = psm == gs::T8 ? 256 : 16;
                for (uint32_t i = 0; i < n; ++i) {
                    const uint32_t v = i * 255 / (n - 1), a = rawAlpha ? i * 128 / (n - 1) : v;
                    pal[i] = v | (v << 8) | (v << 16) | (a << 24);
                }
            } else return false;
        }
        w = hd.w; h = hd.h;
        static const bool t4swz = std::getenv("WOTM_T4SWZ") != nullptr;   // diagnostico: pixels T4 do pacote como memoria do GS (nibbles embaralhados)
        if (t4swz && psm == gs::T4 && w && h && w <= 512 && h <= 512) {
            px.assign(size_t(w) * h, 0);
            const uint32_t bw = std::max<uint32_t>(1, hd.bw);
            for (uint32_t y = 0; y < h; ++y)
                for (uint32_t x = 0; x < w; ++x) {
                    const uint32_t n = gs::nibT4(0, bw, x, y);
                    const uint8_t b = ram.u8(ptr + 0x80 + n / 2);
                    px[size_t(y) * w + x] = pal[(t0.csa * 16 + ((n & 1) ? (b >> 4) : (b & 15))) & 255];
                }
            return true;
        }
        return gs::decodeUpload(ram.p + ptr + 0x80, ram.size - ptr - 0x80, w, h, psm, t0.csa, pal, px, rawAlpha);
    }

    // Textura de particula: texId (indice do texInfo global) + TEX0 da tabela de particulas (psm/CBP). 0 = nao decodificou.
    // Particles::kDustTexId = a textura composta de poeira/fumaca (dustTexture).
    // WOTM_FXNOGRAY=1: sem a rampa cinza quando a paleta nao esta nos .RTX carregados (diagnostico).
    unsigned particleTexture(const Ram &ram, uint32_t texId, uint64_t tex0) {
        if (texId == Particles::kDustTexId) return dustTexture(ram);
        if (!texId || texId >= 1000) return 0;
        const gs::Tex0 t0 = gs::Tex0::decode(tex0);
        const uint64_t key = (uint64_t(texId) << 40) | (uint64_t(t0.cbp) << 12) | (uint64_t(t0.csa) << 4) | t0.cpsm | (1ull << 61);
        const auto it = tex_.find(key);
        if (it != tex_.end()) return it->second.id;
        static const bool gray = std::getenv("WOTM_FXNOGRAY") == nullptr;
        const uint32_t ptr = ram.u32(addr::texInfo + 16 * texId) & 0x0FFFFFFF;
        std::vector<uint32_t> px;
        uint32_t w = 0, h = 0;
        Texture2D t{};
        bool ok = decodeUploadTex(ram, ptr, t0, px, w, h, gray, true) && w && h && px.size() >= size_t(w) * h;
        modParticle(ram, texId, tex0, ok, px, w, h);
        if (ok) {
            Image img{px.data(), int(w), int(h), 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            t = LoadTextureFromImage(img);
            if (t.id) { SetTextureFilter(t, TEXTURE_FILTER_BILINEAR); SetTextureWrap(t, TEXTURE_WRAP_CLAMP); }
        }
        applyTexFilter(t);
        tex_[key] = t;
        return t.id;
    }

    // Textura composta de poeira/fumaca (VRAM 0xFD800, TEX0 com TBP 0x3F60, CT32 64x64, quatro celulas de 32x32), que o jogo refaz
    // na VRAM a cada particleDraw com algum sistema (texDrawDustPacketBuild, pacote em 0x78F6E0). Aqui a mesma conta na CPU, refeita
    // quando os parametros mudam (a fase anda 1/256 por quadro; depende da camera). docs/passos/VU1CHECK.md, "Corrigido".
    //  - Passe 1 (microprograma 0x2E98): um gerador h = frac(c + h * b) sorteia 70 normais x 4 celulas; cada uma vira um SPRITE de
    //    meio-lado 5 + 4t texels (t = frac(fase + h.w)) no centro da celula + A * n * t, com A das colunas do worldToScreenMat (q0-q3)
    //    e o foco (q5); z = min(72 - 1000 t (frente . n), 128), a = min(t, 1 - t) z, emitido so com a >= 1 (hemisferio voltado para a
    //    camera), ate 199 sprites. Desenha a fonte 1 (TextureInfo[32]) com RGBA (1, 106, 1, a) aditivo so RGB: a densidade fica no G.
    //  - Metade do RGB e A = G & 0x7F (copyGreenChannelToAlpha, o truque do CT16).
    //  - Passe 2 (MSCNT, 0x31C0): fundo e sprites com cor = ambiente + 0,5 + soma das luzes da fase (dustLighting) pela normal, e
    //    A = a * 1,20755, com a fonte 2 (TextureInfo[33]) em (Cs - Cd) * As + Cd so RGB.
    // Aritmetica em float comum (o VU trunca); a distribuicao e a mesma do dustsim.py da investigacao. Na METRO: cor ~(57, 57, 40),
    // alfa ate 127 (fumaca escura).
    unsigned dustTexture(const Ram &ram) {
        constexpr uint32_t kPkt = 0x0078F6E0u, kParams = 0x006F8DA4u, kNormals = 0x006F8D9Cu, kColors = 0x006F8DA0u, kCtx = 0x00256B90u;
        const uint32_t pa = ram.u32(kParams), na = ram.u32(kNormals), ca = ram.u32(kColors);
        if (!ram.ok(pa - 16, 48) || !ram.ok(na, 48) || !ram.ok(ca, 80) || !ram.ok(kPkt + 0xB4, 128)) return dustTex_[dustLast_].id;
        float P[8], L[3][4], C[5][3], ctx[8][4];
        for (int i = 0; i < 8; ++i) P[i] = ram.f32(pa + 4 * uint32_t(i));
        for (int r = 0; r < 3; ++r) for (int i = 0; i < 4; ++i) L[r][i] = ram.f32(na + 16 * uint32_t(r) + 4 * uint32_t(i));
        for (int k = 0; k < 5; ++k) for (int i = 0; i < 3; ++i) C[k][i] = ram.f32(ca + 16 * uint32_t(k) + 4 * uint32_t(i));
        for (int q = 0; q < 8; ++q) for (int i = 0; i < 4; ++i) ctx[q][i] = ram.f32(kCtx + 16 * uint32_t(q) + 4 * uint32_t(i));
        const uint32_t nloop = ram.u32(pa - 16), seed = ram.u32(pa - 4);
        uint64_t hash = 1469598103934665603ull;
        auto mix = [&](const void *d, size_t n) { for (size_t i = 0; i < n; ++i) hash = (hash ^ static_cast<const uint8_t *>(d)[i]) * 1099511628211ull; };
        mix(P, sizeof P); mix(L, sizeof L); mix(C, sizeof C); mix(ctx, sizeof ctx); mix(&nloop, 4); mix(&seed, 4);
        for (int i = 0; i < kDustSlots; ++i)
            if (dustTex_[i].id && hash == dustHash_[i]) return dustTex_[dustLast_ = i].id;

        auto source = [&](uint32_t ti, DustSrc &s) {   // decodificada uma vez por (texId, TEX0)
            const uint32_t k = ram.u16(fxaddr::textureInfo + 8 * ti + 2) & 127, id = ram.u16(fxaddr::texId + 2 * k);
            const uint64_t t0 = uint64_t(ram.u32(fxaddr::tex0 + 8 * k)) | (uint64_t(ram.u32(fxaddr::tex0 + 8 * k + 4)) << 32);
            if (!id || id >= 1000) return false;
            if (s.ok && s.id == id && s.tex0 == t0) return true;
            const uint32_t ptr = ram.u32(addr::texInfo + 16 * id) & 0x0FFFFFFF;
            s.id = id; s.tex0 = t0;
            s.ok = decodeUploadTex(ram, ptr, gs::Tex0::decode(t0), s.px, s.w, s.h, false, true) && s.w && s.h && s.px.size() >= size_t(s.w) * s.h;
            return s.ok;
        };
        DustSrc &src1 = dustSrc_[0], &src2 = dustSrc_[1];
        if (!source(32, src1) || !source(33, src2)) return dustTex_[dustLast_].id;
        const double startTime = GetTime();   // diagnostico (WOTM_DUSTLOG): custo da conta, sem a decodificacao das fontes

        // ---- passe 1: lista de sprites
        auto fbits = [](uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; };
        auto fbitsOf = [](float f) { uint32_t b; std::memcpy(&b, &f, 4); return b; };
        uint32_t rnd = 0x3F800000u | (seed & 0x7FFFFFu);   // RINIT / RNEXT do VU
        auto rnext = [&]() { const uint32_t x = rnd & 0x7FFFFFu; rnd = ((((x << 1) | (((x >> 4) ^ (x >> 22)) & 1u)) & 0x7FFFFFu) | 0x3F800000u); return fbits(rnd); };
        float h[4];
        for (float &v : h) v = rnext();
        const float hc[4] = {fbits(0x3f3659ad), fbits(0x3f1ec458), fbits(0x3eb64807), fbits(0x3f16d60a)};
        const float hb[4] = {fbits(0x3f7e2a20), fbits(0x3f7cd6ea), fbits(0x3f66b74f), fbits(0x3f7e6ea7)};
        const float sx = P[2] / ctx[5][0], sy = P[2] / ctx[5][1];
        float col[3][3];
        for (int i = 0; i < 3; ++i) {
            const float *c = ctx[i];
            col[i][0] = (c[0] + ctx[6][0] * c[3]) * sx; col[i][1] = (c[1] + ctx[6][1] * c[3]) * sy; col[i][2] = P[6] * c[3];
        }
        const float fwd[3] = {ctx[0][3], ctx[1][3], ctx[2][3]};
        static const float kCenter[4][2] = {{2032, 2032}, {2064, 2032}, {2032, 2064}, {2064, 2064}};   // celulas na tela do GS (2016 = texel 0)
        int16_t uvtab[16][4];   // UNPACK V4-16 do pacote: 4 sub-imagens x 4 espelhamentos da fonte, em 1/16 de texel
        for (uint32_t k = 0; k < 16; ++k)
            for (uint32_t c = 0; c < 4; ++c) uvtab[k][c] = ram.s16(kPkt + 0xB4 + 8 * k + 2 * c);
        struct DSpr { int x0, y0, x1, y1, a1; const int16_t *uv; float n[3]; int rgb2[3], a2; };
        std::vector<DSpr> spr;
        float r21[3] = {h[0] - 1.5f, h[1] - 1.5f, h[2] - 1.5f};
        int left = int(nloop), vi5 = 29;
        bool full = false;
        while (left > 0 && !full) {
            float t = P[3] + h[3];
            t -= t >= 0.f ? std::floor(t) : std::trunc(t);
            for (int i = 0; i < 4; ++i) { h[i] = hc[i] + h[i] * hb[i]; h[i] -= std::trunc(h[i]); }
            const float r2 = r21[0] * r21[0] + r21[1] * r21[1] + r21[2] * r21[2];
            const float half = (1.f + P[0]) + t * P[1];
            std::rotate(h, h + 1, h + 4);   // MR32
            const float rs = 1.f / std::sqrt(r2);
            float n[3] = {r21[0] * rs, r21[1] * rs, r21[2] * rs};
            for (int i = 0; i < 3; ++i) r21[i] = h[i] - 0.5f;
            for (int k = 0; k < 4; ++k) {
                const uint32_t idx = fbitsOf(h[0]) & 15;
                std::rotate(h, h + 1, h + 4);
                --left;
                if (vi5 >= 0x3FC) { full = true; break; }
                const float d[3] = {n[0] * t, n[1] * t, n[2] * t};
                const float cx = col[0][0] * d[0] + col[1][0] * d[1] + col[2][0] * d[2] + kCenter[k][0];
                const float cy = col[0][1] * d[0] + col[1][1] * d[1] + col[2][1] * d[2] + kCenter[k][1];
                const float cz = std::min(col[0][2] * d[0] + col[1][2] * d[1] + col[2][2] * d[2] + P[7], 128.f);
                const float a = std::min(1.f - t, t) * cz;
                if (a >= 1.f) {
                    DSpr s{int((cx - half) * 16.f) - 2016 * 16, int((cy - half) * 16.f) - 2016 * 16, int((cx + half) * 16.f) - 2016 * 16,
                           int((cy + half) * 16.f) - 2016 * 16, int(a), uvtab[idx], {n[0], n[1], n[2]}, {0, 0, 0}, 0};
                    spr.push_back(s);
                    vi5 += 5;
                }
                const float n0 = n[0];
                n[0] = -n[1]; n[1] = n[2]; n[2] = n0;   // a normal gira de celula para celula: (x, y, z) -> (-y, z, x)
            }
        }

        // ---- passe 2: luz
        auto light = [&](const float nv[3], int out[3]) {
            float dots[4];
            for (int j = 0; j < 4; ++j) dots[j] = std::max(0.f, L[0][j] * nv[0] + L[1][j] * nv[1] + L[2][j] * nv[2]);
            for (int i = 0; i < 3; ++i) {
                float v = C[4][i] + 0.5f;
                for (int j = 0; j < 4; ++j) v += C[j][i] * dots[j];
                out[i] = std::min(255, std::max(0, int(v)));
            }
        };
        int bg[3];
        light(fwd, bg);
        for (DSpr &s : spr) { light(s.n, s.rgb2); s.a2 = int(float(s.a1) * P[5]); }

        // ---- rasterizacao como o GS (bilinear com CLAMP na fonte; regra do canto superior esquerdo)
        // A amostra bilinear separa em coluna e linha: indice ja com o CLAMP e peso de cada lado, calculados uma vez por coluna e
        // por linha do sprite com as mesmas contas em float (o resultado e o mesmo da amostra feita por pixel).
        struct Tap { int i0, i1; float w0, w1; };
        auto taps = [](float u, int n) {
            u -= 0.5f;
            const int i = int(std::floor(u));
            const float f = u - float(i);
            return Tap{std::min(std::max(i, 0), n - 1), std::min(std::max(i + 1, 0), n - 1), 1.f - f, f};
        };
        std::vector<std::array<int, 4>> fb(64 * 64, std::array<int, 4>{0, 0, 0, 0});
        // greenOnly (passe 1): so G e A da fonte contam; R e B do quadro sao trocados pelo fundo antes do passe 2
        auto draw = [&](const DSpr &s, const DustSrc &src, const int rgb[3], int a, bool additive, bool greenOnly) {
            const float u0 = s.uv[0] / 16.f, v0 = s.uv[1] / 16.f, u1 = s.uv[2] / 16.f, v1 = s.uv[3] / 16.f;
            if (s.x1 <= s.x0 || s.y1 <= s.y0) return;
            const int pxa = std::max(0, (s.x0 + 15) >> 4), pxb = std::min(64, (s.x1 + 15) >> 4);
            const int pya = std::max(0, (s.y0 + 15) >> 4), pyb = std::min(64, (s.y1 + 15) >> 4);
            if (pxa >= pxb || pya >= pyb) return;
            Tap cx[64];
            for (int px = pxa; px < pxb; ++px)
                cx[px] = taps(u0 + (u1 - u0) * float(px * 16 - s.x0) / float(s.x1 - s.x0), int(src.w));
            const int c0 = greenOnly ? 1 : 0, c1 = greenOnly ? 2 : 3;
            for (int py = pya; py < pyb; ++py) {
                const Tap ry = taps(v0 + (v1 - v0) * float(py * 16 - s.y0) / float(s.y1 - s.y0), int(src.h));
                const uint32_t *row[2] = {src.px.data() + size_t(ry.i0) * src.w, src.px.data() + size_t(ry.i1) * src.w};
                const float wy[2] = {ry.w0, ry.w1};
#if defined(_M_X64) || defined(__SSE4_1__)
                // os 4 canais de uma vez (RGBA em lanes): mesma soma e mesmas contas inteiras da versao escalar abaixo
                const __m128i vrgb = _mm_setr_epi32(rgb[0], rgb[1], rgb[2], 0), v255 = _mm_set1_epi32(255), vzero = _mm_setzero_si128();
                (void)c0; (void)c1;
                auto texel = [](uint32_t p) { return _mm_cvtepi32_ps(_mm_cvtepu8_epi32(_mm_cvtsi32_si128(int(p)))); };
                for (int px = pxa; px < pxb; ++px) {
                    const Tap &tx = cx[px];
                    __m128 acc = _mm_setzero_ps();
                    for (int dj = 0; dj < 2; ++dj) {
                        acc = _mm_add_ps(acc, _mm_mul_ps(texel(row[dj][tx.i0]), _mm_set1_ps(tx.w0 * wy[dj])));
                        acc = _mm_add_ps(acc, _mm_mul_ps(texel(row[dj][tx.i1]), _mm_set1_ps(tx.w1 * wy[dj])));
                    }
                    const __m128i t = _mm_cvttps_epi32(acc);
                    const __m128i as = _mm_set1_epi32((a * _mm_extract_epi32(t, 3)) >> 7);
                    int *dp = fb[size_t(py) * 64 + size_t(px)].data();
                    const __m128i d = _mm_loadu_si128(reinterpret_cast<const __m128i *>(dp));
                    const __m128i cs = _mm_srai_epi32(_mm_mullo_epi32(vrgb, t), 7);
                    __m128i r;
                    if (additive)
                        r = _mm_min_epi32(v255, _mm_add_epi32(_mm_srai_epi32(_mm_mullo_epi32(cs, as), 7), d));
                    else
                        r = _mm_max_epi32(vzero, _mm_min_epi32(v255, _mm_add_epi32(_mm_srai_epi32(_mm_mullo_epi32(_mm_sub_epi32(cs, d), as), 7), d)));
                    r = _mm_blend_epi16(r, d, 0xC0);   // o alfa do quadro nao muda
                    _mm_storeu_si128(reinterpret_cast<__m128i *>(dp), r);
                }
#else
                for (int px = pxa; px < pxb; ++px) {
                    const Tap &tx = cx[px];
                    float acc[4] = {0, 0, 0, 0};
                    for (int dj = 0; dj < 2; ++dj) {   // mesma ordem de soma da amostra por pixel
                        const float wa = tx.w0 * wy[dj], wb = tx.w1 * wy[dj];
                        const uint32_t pa = row[dj][tx.i0], pb = row[dj][tx.i1];
                        for (int c = c0; c < c1; ++c) acc[c] += float((pa >> (8 * c)) & 255) * wa;
                        acc[3] += float(pa >> 24) * wa;
                        for (int c = c0; c < c1; ++c) acc[c] += float((pb >> (8 * c)) & 255) * wb;
                        acc[3] += float(pb >> 24) * wb;
                    }
                    const int as = (a * int(acc[3])) >> 7;
                    auto &d = fb[size_t(py) * 64 + size_t(px)];
                    for (int c = c0; c < c1; ++c) {
                        const int cs = (rgb[c] * int(acc[c])) >> 7;
                        d[c] = additive ? std::min(255, ((cs * as) >> 7) + d[c]) : std::max(0, std::min(255, (((cs - d[c]) * as) >> 7) + d[c]));
                    }
                }
#endif
            }
        };
        const int rgb1[3] = {1, int(P[4]), 1};
        for (const DSpr &s : spr) draw(s, src1, rgb1, s.a1, true, true);
        for (auto &p : fb) {
            for (int c = 0; c < 3; ++c) p[c] >>= 1;          // sprite (0, 0, 0, 64) em (Cs - Cd) * As + Cd: ((-Cd * 64) >> 7) + Cd
            p[3] = p[1] & 0x7F;                            // copyGreenChannelToAlpha (o bit 15 do CT16 sai 0)
            for (int c = 0; c < 3; ++c) p[c] = bg[c];      // passe 2: fundo
        }
        for (const DSpr &s : spr) draw(s, src2, s.rgb2, s.a2, false, false);

        std::vector<uint32_t> px(64 * 64);   // alfa cru do GS (0x80 = 1,0), como as outras texturas de particula
        for (size_t i = 0; i < px.size(); ++i)
            px[i] = uint32_t(fb[i][0]) | (uint32_t(fb[i][1]) << 8) | (uint32_t(fb[i][2]) << 16) | (uint32_t(fb[i][3]) << 24);
        const double mathEnd = GetTime();
        const int slot = dustNext_;
        dustNext_ = (dustNext_ + 1) % kDustSlots;
        Texture2D &dt = dustTex_[slot];
        if (!dt.id) {
            Image img{px.data(), 64, 64, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            dt = LoadTextureFromImage(img);
            if (dt.id) { SetTextureFilter(dt, TEXTURE_FILTER_BILINEAR); SetTextureWrap(dt, TEXTURE_WRAP_CLAMP); }
        } else {
            UpdateTexture(dt, px.data());
        }
        dustHash_[slot] = hash;
        dustLast_ = slot;
        {   // diagnostico: WOTM_DUSTLOG=1 imprime o resumo a cada regeneracao
            static const bool lg = std::getenv("WOTM_DUSTLOG") != nullptr;
            if (lg) {
                int amax = 0; double asum = 0, cw[3] = {0, 0, 0};
                for (const auto &p : fb) { amax = std::max(amax, p[3]); asum += p[3]; for (int c = 0; c < 3; ++c) cw[c] += double(p[c]) * p[3]; }
                std::fprintf(stderr, "[dust] fase %.4f sprites %zu fundo %d %d %d alfa max %d media %.1f cor ponderada %.0f %.0f %.0f (%.2f ms, envio %.2f ms)\n", P[3], spr.size(), bg[0], bg[1], bg[2],
                             amax, asum / 4096.0, asum ? cw[0] / asum : 0.0, asum ? cw[1] / asum : 0.0, asum ? cw[2] / asum : 0.0, (mathEnd - startTime) * 1000.0, (GetTime() - mathEnd) * 1000.0);
            }
        }
        return dt.id;
    }
    struct DustSrc { std::vector<uint32_t> px; uint32_t w = 0, h = 0, id = 0; uint64_t tex0 = 0; bool ok = false; };
    DustSrc dustSrc_[2];   // fontes 1 e 2 (TextureInfo[32] e [33]) decodificadas
    // uma por vista (a conta depende da camera): cada vista desenha com a sua, e o redesenho do mesmo quadro do jogo reaproveita
    static constexpr int kDustSlots = 4;
    Texture2D dustTex_[kDustSlots]{};
    uint64_t dustHash_[kDustSlots]{};
    int dustNext_ = 0, dustLast_ = 0;

    Particles fx;   // sistemas de particulas do quadro (collect junto com a cena, draw depois dos translucidos)

    // ------------------------------------------------------------ sombras projetadas (common/ps)
    // No retail, cada bloqueador ativo (ate 10 em psBlkLst: monstros e afins, registrados por psBlockerAdd) e desenhado de uma camera
    // posta na direcao da luz (vista 4, a `radius * fator` da origem) para uma textura pequena, filtrada e reprojetada sobre os objetos
    // proximos (psBldBlkRObjs). Aqui: a silhueta e rasterizada na CPU num quadro ortografico no espaco da luz (sem trocar de
    // framebuffer dentro do BeginMode3D, o que quebraria o render texture do runtime) e os triangulos dos objetos proximos sao
    // redesenhados com essa textura projetada. docs/passos/NATIVO.md ("Sombras").
    std::vector<ShadowBlocker> shadows;

    // Bloqueadores ativos, como psFindActiveBlockers: sombras ligadas (psSetShadowOnOff), entrada em uso, cs desenhavel e visivel na vista 0
    void collectShadows(const Ram &ram) {
        shadows.clear();
        static const bool off = std::getenv("WOTM_NOSHADOW") != nullptr;
        if (off || !ram.u32(addr::psShadowOn)) return;
        for (int i = 0; i < 10; ++i) {
            const uint32_t e = addr::psBlkLst + 0x30 * uint32_t(i);
            const uint32_t cs = ram.u32(e + 0x10);
            if (ram.u16(e + 0x2E) != 1 || !ram.ok(cs, 0xB0) || !ram.u8(cs + 0xC) || !(ram.u8(cs + 0xE) & 1)) continue;
            ShadowBlocker b;
            b.index = i;
            for (int c = 0; c < 3; ++c) b.origin[c] = ram.f32(cs + 0x10 + 4 * c);
            const uint32_t rp = ram.u32(e + 0x20);   // &raio da esfera do no raiz (GROUP) do cs; 0 se o no nao e GROUP
            const float r = rp ? ram.f32(rp) : 0.f;
            b.extent = (r > 1.f && r < 5000.f ? r : 80.f) * 1.1f;
            b.shadowCs = ram.u32(e + 0x14);
            std::vector<DrawItem> keep;
            keep.swap(items);
            // LOD como o hierShadow: a distancia fixa do bloqueador (lodRangeSq em +0x28, 1200^2 no psBlockerAdd; o chefe final usa
            // 20000^2), nao a do olho da vista; e sem o corte pela vista (o jogo percorre pela camera da luz, que abrange o bloqueador).
            // Assim a silhueta e a mesma em todas as vistas da tela dividida (Scene::shadowTexture a reaproveita) e um monstro meio fora
            // da vista nao perde parte da sombra. Sem valor util na entrada: o olho, como antes.
            const float lodD2 = ram.f32(e + 0x28);
            static const bool eyeLod = std::getenv("WOTM_SHADOWEYELOD") != nullptr;   // diagnostico: LOD e corte pelo olho (como antes)
            shadowLodD2_ = !eyeLod && std::isfinite(lodD2) && lodD2 > 0.f ? lodD2 : -1.f;
            drawCs(ram, cs, false);
            const uint32_t cs2 = ram.u32(e + 0x18);   // segundo cs (psBlockerSet2ndCs: o que o monstro segura, por exemplo)
            if (ram.ok(cs2, 0xB0) && ram.u8(cs2 + 0xC)) drawCs(ram, cs2, false);
            shadowLodD2_ = -1.f;
            b.casters.swap(items);
            items.swap(keep);
            for (const DrawItem &it : b.casters) b.casterNodes.push_back(it.node);   // todos (receptor excluido), antes do filtro
            // dontCastShadow (bit 29 dos bits do OBJECT em +0xC): o psBldBlkRObjs sai sem emitir o objeto. No Kineticlops so o olho
            // projeta; o corpo translucido tem o bit.
            constexpr uint32_t kObjDontCastShadow = 0x20000000;
            b.casters.erase(std::remove_if(b.casters.begin(), b.casters.end(),
                                           [&](const DrawItem &it) { return (ram.u32(it.node + 0xC) & kObjDontCastShadow) != 0; }),
                            b.casters.end());
            std::sort(b.casterNodes.begin(), b.casterNodes.end());
            if (!b.casters.empty()) shadows.push_back(std::move(b));
        }
    }

    // Desenha as sombras sobre os objetos ja desenhados (depois dos opacos, antes dos translucidos).
    void drawShadows(const Ram &ram) {
        if (shadows.empty()) return;
        float L[3] = {ram.f32(addr::psLitDirWs), ram.f32(addr::psLitDirWs + 4), ram.f32(addr::psLitDirWs + 8)};   // da cena para a luz
        const float ll = std::sqrt(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
        if (!(ll > 1e-3f)) return;
        for (float &c : L) c /= ll;
        // base ortonormal do quadro de luz
        const float hint[3] = {std::fabs(L[2]) < 0.9f ? 0.f : 1.f, 0.f, std::fabs(L[2]) < 0.9f ? 1.f : 0.f};
        float R[3] = {hint[1] * L[2] - hint[2] * L[1], hint[2] * L[0] - hint[0] * L[2], hint[0] * L[1] - hint[1] * L[0]};
        const float rl = std::sqrt(R[0] * R[0] + R[1] * R[1] + R[2] * R[2]);
        for (float &c : R) c /= rl;
        const float U[3] = {L[1] * R[2] - L[2] * R[1], L[2] * R[0] - L[0] * R[2], L[0] * R[1] - L[1] * R[0]};
        static const float kShadowPull = std::getenv("WOTM_SHADOWPULL") ? float(std::atof(std::getenv("WOTM_SHADOWPULL"))) : 0.002f;   // diagnostico
        const float kShadowPullMax = kShadowPull * 1000.f;
        float inten = ram.f32(addr::psShadowIntens);   // 80 no psVarInit (escala do GS: 0x80 = 1,0)
        inten = (inten > 0.f && inten < 1000.f) ? std::min(1.f, inten / 128.f) : 0.625f;

        rlDrawRenderBatchActive();
        rlDisableDepthMask();
        rlDisableBackfaceCulling();
        rlSetBlendMode(BLEND_ALPHA);
        for (size_t k = 0; k < shadows.size(); ++k) {
            const ShadowBlocker &b = shadows[k];
            static const bool shadowLog = std::getenv("WOTM_SHADOWLOG") != nullptr;   // uma vez: getenv e lento no Windows
            if (shadowLog) {
                std::fprintf(stderr, "[shadow] bloqueador %d origem %.1f %.1f %.1f extent %.1f luz %.3f %.3f %.3f cs receptor %x (0 = fase), %zu projetam:\n", b.index,
                             b.origin[0], b.origin[1], b.origin[2], b.extent, L[0], L[1], L[2], b.shadowCs, b.casters.size());
                for (const DrawItem &it : b.casters) {
                    const MeshData *md = mesh(ram, it.node);
                    std::fprintf(stderr, "[shadow]   no %x tex %u bits %08x lod %d fade %.2f pos %.1f %.1f %.1f tris %zu skinned %d\n", it.node, unsigned(ram.u16(it.node + 0x50)),
                                 ram.u32(it.node + 0xC), it.lodIdx, it.fade, it.m.m[3][0], it.m.m[3][1], it.m.m[3][2], md ? md->tris.size() / 3 : size_t(0), md ? int(md->skinned) : -1);
                }
            }
            unsigned tex = 0;
            {
                ProfScope ps(DrawProf::ShadowTex);
                tex = shadowTexture(ram, k, b, R, U);
            }
            if (!tex) continue;
            const float E = b.extent;
            auto lightUV = [&](const float p[3], float &u, float &v, float &h) {
                const float d[3] = {p[0] - b.origin[0], p[1] - b.origin[1], p[2] - b.origin[2]};
                u = (d[0] * R[0] + d[1] * R[1] + d[2] * R[2]) / E;
                v = (d[0] * U[0] + d[1] * U[1] + d[2] * U[2]) / E;
                h = (d[0] * L[0] + d[1] * L[1] + d[2] * L[2]) / E;   // > 0: entre o bloqueador e a luz
            };
            rlBegin(RL_TRIANGLES);
            rlSetTexture(tex);
            auto receive = [&](const std::vector<DrawItem> &list) {
                for (const DrawItem &it : list) {
                    if (std::binary_search(b.casterNodes.begin(), b.casterNodes.end(), it.node)) continue;
                    // so recebe sombra o OBJECT sem 0x200 nem 0x60 nas flags: o hierTraverseAsm so chama o psObjectCull (teste do
                    // receptor) nesses. No luta_hud recebem so os pisos (flags 0x90); o vidro, as arvores e os brilhos nao
                    if (ram.u16(it.node + 0xC) & 0x260) continue;
                    // e so o do _cs que o bloqueador indica (psObjectCull compara a entrada +0x14 com o _cs em travessia). Quase sempre
                    // e 0, a fase; o Kineticlops aponta o _cs de um quadrado no chao (tex 324) e so ele recebe: o nativo projetava
                    // tambem no piso, e a sombra saia dupla
                    static const bool anyCs = std::getenv("WOTM_SHADOWANYCS") != nullptr;   // diagnostico: qualquer _cs recebe (como antes)
                    if (it.cs != b.shadowCs && !anyCs) continue;
                    // esfera do objeto (+0x10 centro, +0x1C raio) contra o cilindro de sombra
                    float c[3];
                    xform(it.m, ram.f32(it.node + 0x10), ram.f32(it.node + 0x14), ram.f32(it.node + 0x18), c);
                    const float rad = std::fabs(ram.f32(it.node + 0x1C));
                    float u, v, h;
                    lightUV(c, u, v, h);
                    const float rr = rad / E;
                    if (!(std::isfinite(u) && std::isfinite(rr)) || u * u + v * v > (1.5f + rr) * (1.5f + rr) || h - rr > 0.5f) continue;
                    static const bool slog = std::getenv("WOTM_SHADOWLOG") != nullptr;
                    if (slog)
                        std::fprintf(stderr, "[shadow] bloqueador %d recebe (cs %x) no %x tex %u flags %04x centro %.1f %.1f %.1f raio %.1f (u %.2f v %.2f h %.2f)\n", b.index,
                                     it.cs, it.node, unsigned(ram.u16(it.node + 0x50)), unsigned(ram.u16(it.node + 0xC)), c[0], c[1], c[2], rad, u, v, h);
                    // zBufferFudge do receptor, como no drawList: o quadrado de sombra do Kineticlops fica rente ao piso e so passa
                    // na frente dele pelo fudge (1,0122). So nos receptores de cs proprio: as camadas do piso da fase (fudge 1,0012)
                    // com fudge escurecem a sombra alem da referencia (congar_olhos)
                    const float fz = ram.f32(it.node + 0x28);
                    const bool fudge = it.cs != 0 && eyeOk_ && fz > 0.5f && fz < 2.f && fz != 1.f;
                    if (fudge && slog) std::fprintf(stderr, "[shadow]   fudge %.4f no %x\n", fz, it.node);
                    forEachTriPos(ram, it, [&](const float p[3][3]) {
                        float tu[3], tv[3], th[3];
                        for (int i = 0; i < 3; ++i) lightUV(p[i], tu[i], tv[i], th[i]);
                        if ((tu[0] < -1 && tu[1] < -1 && tu[2] < -1) || (tu[0] > 1 && tu[1] > 1 && tu[2] > 1) ||
                            (tv[0] < -1 && tv[1] < -1 && tv[2] < -1) || (tv[0] > 1 && tv[1] > 1 && tv[2] > 1) ||
                            (th[0] > 0.5f && th[1] > 0.5f && th[2] > 0.5f))
                            return;
                        for (int i = 0; i < 3; ++i) {
                            // some acima do bloqueador (lado da luz); puxa o vertice para o olho para nao brigar com a superficie
                            const float fade = th[i] < 0.15f ? 1.f : std::max(0.f, 1.f - (th[i] - 0.15f) / 0.35f);
                            float q[3] = {p[i][0], p[i][1], p[i][2]};
                            if (fudge) for (int a = 0; a < 3; ++a) q[a] = eye_[a] + (q[a] - eye_[a]) / fz;
                            if (eyeOk_) {
                                const float dx = eye_[0] - q[0], dy = eye_[1] - q[1], dz = eye_[2] - q[2];
                                const float dl = std::sqrt(dx * dx + dy * dy + dz * dz);
                                // 0,2% da distancia (ate 2); WOTM_SHADOWPULL troca (no luta_hud 0 a 0,002 da o mesmo)
                                if (dl > 1e-3f) { const float s = std::min(kShadowPullMax, dl * kShadowPull) / dl; q[0] += dx * s; q[1] += dy * s; q[2] += dz * s; }
                            }
                            rlColor4ub(255, 255, 255, uint8_t(255.f * inten * fade));
                            rlTexCoord2f(tu[i] * 0.5f + 0.5f, tv[i] * 0.5f + 0.5f);
                            rlVertex3f(q[0], q[1], q[2]);
                        }
                    });
                }
            };
            receive(items);
            rlEnd();
            rlDrawRenderBatchActive();
        }
        rlSetTexture(0);
        rlEnableDepthMask();
        rlEnableBackfaceCulling();
    }

    // Face de costas vista de `eye`: o VU1 (microprograma dos objetos) poe ADC nos triangulos de area positiva na tela do GS
    // (y para baixo), so nas secoes com o bit 15 do seletor (Vtx::cull). Em mundo: normal (b - a) x (c - a) para longe do olho.
    static bool backFacing(const float p[3][3], const float eye[3]) {
        const float u[3] = {p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]};
        const float v[3] = {p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2]};
        const float n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
        return n[0] * (p[0][0] - eye[0]) + n[1] * (p[0][1] - eye[1]) + n[2] * (p[0][2] - eye[2]) > 0.f;
    }

    // Triangulos de um item em coordenadas de mundo (com skinning), todos os slots: f(p, v, rgb) com os tres vertices decodificados (UV,
    // cor, slot de textura e culling da secao), a cor de cada um nas unidades do GS (vertexRgb: com a luz do VU1 nos iluminados) e a
    // normal no mundo do mapa de ambiente (envNormal; zero sem DrawItem::env)
    template <class F> void forEachTri(const Ram &ram, const DrawItem &it, F &&f) {
        const MeshData *md = mesh(ram, it.node);
        if (!md || !md->valid) return;
        const uint32_t pal = md->skinned ? palette(ram, it) : 0;
        const ObjLight ol = objectLight(ram, it);
        const auto &t = md->tris;
        std::vector<M4> pm;   // paleta (osso x da normal nos iluminados e no mapa de ambiente)
        if (pal && (ol.lit || it.env))
            for (uint32_t s = 0; s < 32; ++s) pm.push_back(readM4(ram, pal + 0x10 + 0x40 * s));
        float p[3][3], nw[3][3] = {};
        uint8_t rgb[3][3];
        for (size_t k = 0; k + 2 < t.size(); k += 3) {
            for (int c = 0; c < 3; ++c) {
                const Vtx &v = t[k + c];
                const M4 *bone = pm.empty() ? nullptr : &pm[(v.cx & 0x7F) / 4];
                if (pal) skinVertex(ram, pal, it.m, v, p[c]);
                else xform(it.m, v.x, v.y, v.z, p[c]);
                vertexRgb(ol, it.color, bone, it.m, v, rgb[c]);
                if (it.env) envNormal(v, bone, it.m, nw[c]);
            }
            f(p, &t[k], rgb, nw);
        }
    }

    // So as posicoes no mundo (com skinning) dos triangulos de um item, sem cor nem normal: o que as sombras usam. A paleta e a do
    // instantaneo (DrawItem::pal, a mesma do drawList); sem ela, a da RAM.
    template <class F> void forEachTriPos(const Ram &ram, const DrawItem &it, F &&f) {
        const MeshData *md = mesh(ram, it.node);
        if (!md || !md->valid) return;
        std::vector<M4> tmp;
        const M4 *pm = nullptr;
        if (md->skinned) {
            if (it.pal && it.pal->size() >= 32) pm = it.pal->data();
            else if (const uint32_t pal = palette(ram, it)) {
                for (uint32_t s = 0; s < 32; ++s) tmp.push_back(readM4(ram, pal + 0x10 + 0x40 * s));
                pm = tmp.data();
            }
        }
        const auto &t = md->tris;
        float p[3][3];
        for (size_t k = 0; k + 2 < t.size(); k += 3) {
            for (int c = 0; c < 3; ++c) {
                if (pm) skinVertexPM(pm, it.m, t[k + c], p[c]);
                else xform(it.m, t[k + c].x, t[k + c].y, t[k + c].z, p[c]);
            }
            f(p);
        }
    }

    // Chave da silhueta: tudo o que entra nela (bloqueador, quadro de luz e, de cada um que projeta, no, matriz e paleta), com as
    // posicoes em 1/4 de unidade e as rotacoes em 1/1024. O hier de cada vista reavalia a pose e os ultimos bits das matrizes mudam de
    // uma vista para a outra (mesmo quadro do jogo, mesmos numeros a vista); a silhueta tem ~2 unidades por texel, entao isso nao a
    // muda. As vistas 1 a 3 da tela dividida passam a dividir a textura; a 0 (antes do TheGame::Update) cai na dela quando ha movimento.
    static uint64_t shadowKey(const ShadowBlocker &b, const float R[3], const float U[3]) {
        uint64_t h = 1469598103934665603ull;
        auto mixI = [&](int64_t v) {
            for (int i = 0; i < 8; ++i) h = (h ^ uint8_t(v >> (8 * i))) * 1099511628211ull;
        };
        auto q = [](float f, float s) { return std::isfinite(f) ? int64_t(std::llround(double(f) * s)) : INT64_MIN; };
        auto mixM = [&](const M4 &m) {
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c) mixI(q(m.m[r][c], r == 3 ? 4.f : 1024.f));
        };
        mixI(b.index);
        for (int c = 0; c < 3; ++c) mixI(q(b.origin[c], 4.f));
        mixI(q(b.extent, 4.f));
        for (int c = 0; c < 3; ++c) mixI(q(R[c], 1024.f)), mixI(q(U[c], 1024.f));
        for (const DrawItem &it : b.casters) {
            mixI(it.node);
            mixM(it.m);
            if (it.pal) for (const M4 &m : *it.pal) mixM(m);
            else mixI(it.animPkt);   // sem copia (viewer): le a RAM, sem cache entre vistas
        }
        return h;
    }

    // Silhueta guardada (Scene::shadowCache_)
    struct ShadowCacheEntry {
        uint64_t key = 0, used = 0, redraw = ~0ull;
        Texture2D tex{};
        int index = -1;
        float origin[3] = {}, extent = 0.f, R[3] = {};
        std::vector<uint32_t> nodes;
        std::vector<float> pos;   // translacao (m[3]) de cada no que projeta
    };

    // A silhueta de outra vista do mesmo redesenho serve para esta? Mesmo bloqueador, mesma luz, mesmos nos projetando, e origem,
    // extensao e posicao de cada no dentro do que a silhueta (~2 unidades por texel) nao mostra. O hier de cada vista reavalia a pose,
    // e os membros andam algumas decimas de unidade de uma vista para outra no mesmo quadro: a chave exata quase nunca batia.
    bool shadowNear(const ShadowCacheEntry &e, const ShadowBlocker &b, const float R[3]) const {
        if (e.redraw != redrawId || e.index != b.index || e.nodes.size() != b.casters.size()) return false;
        if (std::fabs(e.extent - b.extent) > 0.02f * b.extent) return false;
        for (int c = 0; c < 3; ++c)
            if (std::fabs(e.origin[c] - b.origin[c]) > kShadowTol || std::fabs(e.R[c] - R[c]) > 1e-3f) return false;
        for (size_t i = 0; i < b.casters.size(); ++i) {
            const DrawItem &it = b.casters[i];
            if (e.nodes[i] != it.node) return false;
            for (int c = 0; c < 3; ++c)
                if (std::fabs(e.pos[3 * i + c] - it.m.m[3][c]) > kShadowTol) return false;
        }
        return true;
    }

    // Silhueta do bloqueador no quadro de luz: rasterizada na CPU (kShadowRes^2), borrada (o retail filtra com sprites) e enviada
    // como textura preta com alfa = cobertura. Reaproveitada pela chave das entradas (shadowKey: um redesenho sem quadro novo) ou de
    // outra vista do mesmo redesenho (shadowNear: tela dividida). Os itens sem copia da paleta (viewer) refazem sempre.
    unsigned shadowTexture(const Ram &ram, size_t slot, const ShadowBlocker &b, const float R[3], const float U[3]) {
        constexpr int N = kShadowRes;
        bool cacheable = true;
        for (const DrawItem &it : b.casters) cacheable = cacheable && (!it.animPkt || it.pal);
        const uint64_t key = cacheable ? shadowKey(b, R, U) : 0;
        ++shadowTick_;
        static const bool slogTex = std::getenv("WOTM_SHADOWLOG") != nullptr;
        static const bool noReuse = std::getenv("WOTM_SHADOWNOCACHE") != nullptr;   // diagnostico: refaz toda silhueta (como antes)
        if (cacheable && !slogTex && !noReuse)
            for (ShadowCacheEntry &e : shadowCache_)
                if (e.tex.id && (e.key == key || shadowNear(e, b, R))) {
                    e.used = shadowTick_;
                    if (drawProfOn()) ++drawProf().shadowHit;
                    return e.tex.id;
                }
        if (drawProfOn()) ++drawProf().shadowMiss;
        static const bool keyLog = std::getenv("WOTM_SHADOWKEYLOG") != nullptr;   // diagnostico: o que entra na chave de cada silhueta
        if (keyLog) {
            std::fprintf(stderr, "[shadowkey] bloqueador %d chave %016llx origem %.3f %.3f %.3f extent %.3f casters", b.index, (unsigned long long)key,
                         b.origin[0], b.origin[1], b.origin[2], b.extent);
            for (const DrawItem &it : b.casters) {
                ShadowBlocker one;
                one.casters.push_back(it);
                const uint64_t hm = shadowKey(one, R, U);   // so este (matriz e paleta)
                one.casters.back().pal.reset();
                std::fprintf(stderr, " %x(lod %d, pal %d, x %.3f, m+pal %04llx, m %04llx)", it.node, it.lodIdx, it.pal ? 1 : 0, it.m.m[3][0],
                             (unsigned long long)(hm & 0xFFFF), (unsigned long long)(shadowKey(one, R, U) & 0xFFFF));
            }
            std::fprintf(stderr, "\n");
        }
        (void)slot;
        std::vector<float> &cov = shadowCov_;
        cov.assign(size_t(N * N), 0.f);
        const float E = b.extent;
        for (const DrawItem &it : b.casters) {
            forEachTriPos(ram, it, [&](const float p[3][3]) {
                float x[3], y[3];
                for (int i = 0; i < 3; ++i) {
                    const float d[3] = {p[i][0] - b.origin[0], p[i][1] - b.origin[1], p[i][2] - b.origin[2]};
                    x[i] = ((d[0] * R[0] + d[1] * R[1] + d[2] * R[2]) / E * 0.5f + 0.5f) * N;
                    y[i] = ((d[0] * U[0] + d[1] * U[1] + d[2] * U[2]) / E * 0.5f + 0.5f) * N;
                }
                const float area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
                if (!(std::fabs(area) > 1e-6f)) return;
                const int x0 = std::max(1, int(std::floor(std::min({x[0], x[1], x[2]})))), x1 = std::min(N - 2, int(std::ceil(std::max({x[0], x[1], x[2]}))));
                const int y0 = std::max(1, int(std::floor(std::min({y[0], y[1], y[2]})))), y1 = std::min(N - 2, int(std::ceil(std::max({y[0], y[1], y[2]}))));
                for (int py = y0; py <= y1; ++py)
                    for (int px = x0; px <= x1; ++px) {
                        const float cx = px + 0.5f, cy = py + 0.5f;
                        const float w0 = (x[1] - cx) * (y[2] - cy) - (x[2] - cx) * (y[1] - cy);
                        const float w1 = (x[2] - cx) * (y[0] - cy) - (x[0] - cx) * (y[2] - cy);
                        const float w2 = (x[0] - cx) * (y[1] - cy) - (x[1] - cx) * (y[0] - cy);
                        if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)) cov[size_t(py * N + px)] = 1.f;
                    }
            });
        }
        for (int pass = 0; pass < 2; ++pass) {   // caixa 3x3 duas vezes; a borda fica em 0 (CLAMP nao estende sombra)
            std::vector<float> &o = shadowTmp_;
            o.assign(cov.size(), 0.f);
            for (int y = 1; y < N - 1; ++y)
                for (int x = 1; x < N - 1; ++x) {
                    float s = 0;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) s += cov[size_t((y + dy) * N + x + dx)];
                    o[size_t(y * N + x)] = s / 9.f;
                }
            cov.swap(o);
        }
        std::vector<uint32_t> &px = shadowPx_;
        px.resize(cov.size());
        for (size_t i = 0; i < cov.size(); ++i) px[i] = uint32_t(std::min(1.f, cov[i]) * 255.f) << 24;
        if (slogTex) {   // diagnostico: silhueta em shadow_<slot>.png (branco = sombra) no diretorio atual
            std::vector<uint32_t> vis(px.size());
            for (size_t i = 0; i < px.size(); ++i) { const uint32_t a = px[i] >> 24; vis[i] = 0xFF000000u | a | (a << 8) | (a << 16); }
            Image img{vis.data(), N, N, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            char name[64];
            std::snprintf(name, sizeof name, "shadow_%zu.png", slot);
            ExportImage(img, name);
        }
        // entrada a reusar: a menos usada recentemente (no maximo kShadowCacheN texturas; 10 bloqueadores x 4 vistas cabem)
        ShadowCacheEntry *e = nullptr;
        if (shadowCache_.size() < kShadowCacheN) e = &shadowCache_.emplace_back();
        else
            for (ShadowCacheEntry &c : shadowCache_)
                if (!e || c.used < e->used) e = &c;
        e->key = cacheable ? key : 0;
        e->used = shadowTick_;
        e->redraw = cacheable ? redrawId : ~0ull;
        e->index = b.index;
        e->extent = b.extent;
        std::copy(b.origin, b.origin + 3, e->origin);
        std::copy(R, R + 3, e->R);
        e->nodes.clear();
        e->pos.clear();
        for (const DrawItem &it : b.casters) {
            e->nodes.push_back(it.node);
            for (int c = 0; c < 3; ++c) e->pos.push_back(it.m.m[3][c]);
        }
        Texture2D &t = e->tex;
        if (!t.id) {
            Image img{px.data(), N, N, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            t = LoadTextureFromImage(img);
            if (t.id) { SetTextureFilter(t, TEXTURE_FILTER_BILINEAR); SetTextureWrap(t, TEXTURE_WRAP_CLAMP); }
        } else {
            UpdateTexture(t, px.data());
        }
        return t.id;
    }

    // PNG de mod numa textura de particula (alfa cru do GS, como a original). Os UVs das particulas sao normalizados pelo TEX0, entao
    // qualquer resolucao serve. Sem a original (paleta ausente), o PNG entra sozinho, com o alfa na escala 0..0x80.
    void modParticle(const Ram &ram, uint32_t texId, uint64_t tex0, bool &ok, std::vector<uint32_t> &px, uint32_t &w, uint32_t &h) {
        if (texmods::Index::get().empty()) return;
        uint32_t amax = 0;
        if (ok) for (uint32_t c : px) amax = std::max(amax, c >> 24);
        std::vector<uint32_t> mpx;
        uint32_t mw = 0, mh = 0;
        if (modTexture(ram, texId, gs::Tex0::decode(tex0), (tex0 >> 34) & 1, true, mpx, mw, mh, amax > 0x80)) {
            px.swap(mpx); w = mw; h = mh; ok = true;
        }
    }

    // Textura de efeito (particulas): `texInfo[id]` guarda o pacote (como nos objetos) e o TEX0 do efeito (fx[i].TEX0ptr) traz psm e CBP da paleta.
    bool decodeFxTex(const Ram &ram, uint32_t id, uint64_t tex0, std::vector<uint32_t> &px, uint32_t &w, uint32_t &h) const {
        const gs::Tex0 t0 = gs::Tex0::decode(tex0);
        const uint32_t ptr = ram.u32(addr::texInfo + 16 * id) & 0x0FFFFFFF;
        if (!ram.ok(ptr + 0x80)) return false;
        uint32_t pal[256] = {};
        const UploadHdr hd = UploadHdr::read(ram, ptr);
        const uint32_t psm = hd.psm;
        if (psm == gs::T8 || psm == gs::T4) {
            const auto it = rtxPal.find(t0.cbp);
            if (it == rtxPal.end()) return false;
            fillPal(pal, it->second, psm == gs::T8, true);
        }
        w = hd.w; h = hd.h;
        return w && h && gs::decodeUpload(ram.p + ptr + 0x80, ram.size - ptr - 0x80, w, h, psm, t0.csa, pal, px, true);
    }

    // diagnostico: WOTM_TEXFROM=<de>:<para> desenha os vertices do slot <de> com a textura do slot <para>
    static uint32_t texRemap(uint32_t slot) {
        static int from = -1, to = -1; static bool init = false;
        if (!init) { init = true; if (const char *e = std::getenv("WOTM_TEXFROM")) std::sscanf(e, "%d:%d", &from, &to); }
        return (from >= 0 && int(slot) == from) ? uint32_t(to) : slot;
    }

    unsigned (*movieTexture)() = nullptr;   // textura do filme (texId 999); 0 = sem quadro. Quem toca o filme preenche (wotm_live.inc)
    // quadro anterior (texId 0), render texture de cabeca para baixo com a tela de 640x224 do GS inteira; 0 = nao ha. Preenche: wotm_live.inc
    unsigned (*screenTexture)() = nullptr;
    unsigned screenId_ = 0;   // ultimo id devolvido para kScreenKey (o desenho converte o UV dele)

    unsigned textureId(const Ram &ram, const DrawItem &it, uint32_t slot) {
        if (texKey(ram, it, slot) == kScreenKey) return screenId_ = screenTexture ? screenTexture() : 0;
        return textureId(ram, it.node, slot);
    }
    // textura no lugar da do objeto `node` (slot de textura), com alfa cru e linhas de baixo para cima como as do jogo; 0 = a do
    // objeto. Preenche: wotm_levelsel.inc (foto das fases sem arte no SHELL)
    unsigned (*nodeTexture)(uint32_t node, uint32_t slot) = nullptr;

    unsigned textureId(const Ram &ram, uint32_t node, uint32_t slot = 0) {
        if (nodeTexture)
            if (const unsigned id = nodeTexture(node & 0x1FFFFFFFu, slot)) return id;
        // filme do menu: moviesSetScreenObject poe texId 999 no objeto (wotm_fmv.hpp); a textura vem do tocador, quadro a quadro
        if (movieTexture && objectTexId(ram, node, slot) == 999)
            if (const unsigned id = movieTexture()) return id;
        const uint64_t key = texKey(ram, node, slot);
        if (!key) return 0;
        if (key == kScreenKey) return screenId_ = screenTexture ? screenTexture() : 0;
        auto it = tex_.find(key);
        if (it != tex_.end()) return it->second.id;
        Texture2D t{};
        std::vector<uint32_t> px;
        uint32_t w = 0, h = 0;
        const uint64_t t0v = objectTex0(ram, node, slot);
        // alfa cru do GS: o shader do draw mistura As = At * Av / 16384. A original vem antes: o PNG de mod le o alfa na escala dela
        const bool orig = decodeTex(ram, node, px, w, h, slot, true);
        uint32_t origAmax = 0;
        if (orig)
            for (uint32_t c : px) origAmax = std::max(origAmax, c >> 24);
        std::vector<uint32_t> mpx;
        uint32_t mw = 0, mh = 0;
        const bool fromMod = modTexture(ram, objectTexId(ram, node, slot), gs::Tex0::decode(t0v), (t0v >> 34) & 1, true, mpx, mw, mh,
                                        origAmax > 0x80);
        if (fromMod) { px.swap(mpx); w = mw; h = mh; }
        if (fromMod || orig) {
            if (std::getenv("WOTM_ALPHASTAT")) {
                size_t a0 = 0, am = 0;
                for (uint32_t c : px) { const uint32_t a = c >> 24; if (a == 0) ++a0; else if (a < 255) ++am; }
                if (a0 || am) std::fprintf(stderr, "alpha tex=%u %ux%u transparente=%.0f%% parcial=%.0f%%\n", ram.u16(node + 0x50), w, h, 100.0 * a0 / px.size(), 100.0 * am / px.size());
            }
            {
                // Texels com alfa 0 guardam uma cor qualquer (escura); o filtro bilinear a mistura nas bordas e faz uma franja.
                // Espalha a cor dos vizinhos visiveis para dentro dos transparentes (2 passos de 3x3).
                size_t partial = 0, clear = 0;
                // alfa cru: semitransparente = abaixo de ~1,0 com o vertice em 0x80 (0x7C); 0x80..0xFF e opaco
                for (uint32_t c : px) { const uint32_t a = c >> 24; if (a == 0) ++clear; else if (a < 0x7C) ++partial; }
                partial_[key] = partial * 100 > px.size();   // mais de 1% dos texels semitransparentes
                uint32_t amax = 0;
                for (uint32_t c : px) amax = std::max(amax, c >> 24);
                texAmax_[key] = uint8_t(amax);
                if (clear && clear < px.size()) {
                    std::vector<uint8_t> solid(px.size());
                    for (size_t i = 0; i < px.size(); ++i) solid[i] = (px[i] >> 24) != 0;
                    for (int pass = 0; pass < 2; ++pass) {
                        std::vector<uint32_t> np = px;
                        std::vector<uint8_t> ns = solid;
                        for (uint32_t y = 0; y < h; ++y)
                            for (uint32_t x = 0; x < w; ++x) {
                                if (solid[y * w + x]) continue;
                                uint32_t r = 0, g = 0, b = 0, n = 0;
                                for (int dy = -1; dy <= 1; ++dy)
                                    for (int dx = -1; dx <= 1; ++dx) {
                                        const size_t q = size_t((y + h + uint32_t(dy)) % h) * w + (x + w + uint32_t(dx)) % w;
                                        if (!solid[q]) continue;
                                        r += px[q] & 255; g += (px[q] >> 8) & 255; b += (px[q] >> 16) & 255; ++n;
                                    }
                                if (n) { np[y * w + x] = (r / n) | ((g / n) << 8) | ((b / n) << 16); ns[y * w + x] = 1; }
                            }
                        px.swap(np);
                        solid.swap(ns);
                    }
                }
            }
            Image img{};
            img.data = px.data(); img.width = int(w); img.height = int(h); img.mipmaps = 1;
            img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
            t = LoadTextureFromImage(img);
            if (t.id && fromMod) { GenTextureMipmaps(&t); SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR); }   // PNG em alta: sem serrilhado de longe
            else if (t.id) SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
            if (t.id) SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
        }
        applyTexFilter(t);
        tex_[key] = t;
        return t.id;
    }

    // colorQuad (+0x70) de _cs que o jogo muda entre o hier(v,0) e o fim do quadro, como o valor estava quando o hier desenhou. Caso
    // conhecido: o pick-up que o monstro pode pegar. O rtMain chama LevelPickups::enableHighlights (pulsa o verde no colorQuad do _cs
    // do pick-up destacado) antes do hier(v,0), e o Monster::update, dentro do TheGame::Update depois dele, chama computeHighlight, que
    // volta o colorQuad a 1. A captura no fim do quadro so via o 1 (o carro nunca ficava verde). Preenchido pela captura ao vivo
    // (hook do enableHighlights no runtime); vazio no viewer e no replay, que leem a RAM.
    struct CsColorAtDraw { uint32_t cs; float q[4]; };
    std::vector<CsColorAtDraw> csColorAtDraw;

    // Reune os objetos visiveis a partir de world->ep e das listas de CS.
    // maxLod < 0: LOD automatico por distancia ao olho da camera do jogo (como hierLod); >= 0 forca esse indice (0 = mais grosseiro).
    void collect(const Ram &ram, int maxLod = -1) {
        ViewScope vs(vctx);
        lights_ = readParaLights(ram);
        pts_ = readPointLights(ram);
        fog_ = readFog(ram);
        items.clear();
        sky.clear();
        overlay.clear();
        hud.clear();
        hp.clear();
        hpOverlay.clear();
        sprites.clear();
        worldLights.clear();
        emitters.clear();
        atMat_ = 0;
        animVal_ = 0;
        cs_ = 0;
        blend_ = kGsAlphaNormal;
        aref_ = kGsAlphaRef;
        zmsk_ = false;
        {   // a travessia comeca no ctx padrao do jogo (ctxGetDefaultGSCtx = ctxDef, montado pelo ctxSetUpGSCtx: ALPHA_1 0x44,
            // TEST_1 0x5040C = sem teste de alfa, ZBUF_1 gravando). Sem ele o nativo supunha o 0x8D (alfa > 8) e descartava o alto do
            // domo do ceu do drive-in (alfa de vertice 0), que o GS desenha.
            int b, r, z;
            groupState(ram, sym::ctxDef, b, r, z);
            if (b >= 0) blend_ = uint8_t(b);
            if (r != kRefAbsent) aref_ = int16_t(r);
            if (z >= 0) zmsk_ = z != 0;
        }
        lodX_ = false;
        spec_ = 0.f;
        lightOwn_ = false;
        visited = 0;
        culled = 0;
        std::memset(destrHist_, 0, sizeof destrHist_);
        std::memset(byOp, 0, sizeof byOp);
        seen_.clear();
        lod_ = maxLod;
        eyeOk_ = false;
        {
            const GameCamera gc = readGameCamera(ram, vctx.view);
            cam_ = gc;
            eyeOk_ = gc.ok;
            for (int c = 0; c < 3; ++c) eye_[c] = gc.pos[c];
        }
        const uint32_t wc = ram.u32(addr::world);
        if (ram.ok(wc, 0x40)) {
            const uint32_t ep = ram.u32(wc);
            if (ram.ok(ep)) walk(ram, ep, ident(), 0);
        }
        // ceu da vista: skyCs/skyCs2/skyClouds de worldCtx[vista] (+4/+8/+0xC). O hierTraceSky usa o trans do _cs como qualquer objeto
        // (hierCsUpdate), nao o olho: o trans so muda pelo viewSetSkyTrans, que o hierSetCamera chama depois de passar por um no de
        // camera do hier. Nos menus isso e todo quadro (trans = olho); na luta o ceu fica onde a ultima camera de locator o deixou e a
        // camera do jogo anda em volta dele (paralaxe do anel de montanhas, tex 108; docs/investigacoes/2026-10-17/05_tex108.md).
        {
            std::vector<DrawItem> world;
            world.swap(items);
            for (uint32_t off : {4u, 8u, 0xCu}) drawCs(ram, ram.u32(viewCtxAddr() + off), false);
            sky.swap(items);
            items.swap(world);
        }
        csList(ram, addr::csActiveList);
        hpList_ = true;
        csList(ram, addr::csHPActiveList);
        hpList_ = false;
        fx.wide = wide;
        fx.collect(ram);          // particulas com cor/textura/mistura do retail (wotm_fx.hpp); destroços (13) ficam com collectParticles
        collectShadows(ram);
        collectParticles(ram);
        static const bool ds = std::getenv("WOTM_DESTRSTAT") != nullptr;
        if (ds) { std::fprintf(stderr, "[destr] estados:"); for (int i = 0; i < 8; ++i) std::fprintf(stderr, " %u", destrHist_[i]); std::fprintf(stderr, "\n"); }
    }

    // So as particulas (fx[] e destroços), sem a travessia: o desenho ao vivo coleta a cena na saida do hier(0, 0) e as particulas no
    // fim do quadro (o particleDraw do PS2 vem depois do gameUpdate; wotm_live.inc, wotmCaptureScene/wotmCaptureFrame).
    void collectFx(const Ram &ram) {
        ViewScope vs(vctx);
        sprites.clear();
        fx.wide = wide;
        fx.collect(ram);
        collectParticles(ram);
    }

    // Copia o estado atual da cena (depois de collect) para um instantaneo: itens, ceu, camera e as paletas de matrizes dos skinned
    // (a RAM e reescrita a cada quadro do jogo; o instantaneo precisa guardar os valores).
    Snapshot takeSnapshot(const Ram &ram, uint32_t g) {
        Snapshot sn;
        sn.items = items;
        sn.sky = sky;
        sn.overlay = overlay;
        sn.hud = hud;
        sn.hp = hp;
        sn.hpOverlay = hpOverlay;
        sn.sprites = sprites;
        sn.fx = fx.systems;
        sn.flares = fx.flares;
        sn.worldLights = worldLights;
        sn.emitters = emitters;
        sn.shadows = shadows;
        sn.lights = lights_;
        sn.fog = fog_;
        sn.cam = readGameCamera(ram, vctx.view);
        sn.vctx = vctx;
        sn.culled = culled;
        readScreenPolys(ram, sn.screenPolys);
        sn.g = g;
        sn.valid = true;
        std::unordered_map<uint32_t, std::shared_ptr<std::vector<M4>>> byBlock;
        std::vector<std::vector<DrawItem> *> lists = {&sn.items, &sn.sky};
        for (ShadowBlocker &b : sn.shadows) lists.push_back(&b.casters);   // a silhueta com a mesma pose do monstro desenhado
        for (std::vector<DrawItem> *list : lists)
            for (DrawItem &it : *list) {
                if (!it.animPkt) continue;
                const uint32_t blk = palette(ram, it);
                if (!blk) continue;
                auto &sp = byBlock[blk];
                if (!sp) {
                    sp = std::make_shared<std::vector<M4>>();
                    sp->reserve(32);
                    for (uint32_t k = 0; k < 32; ++k) sp->push_back(readM4(ram, blk + 0x10 + 0x40 * k));
                }
                it.pal = sp;
            }
        return sn;
    }

    // Desenha o instantaneo do ultimo quadro do jogo como esta (sem interpolar: a logica roda a 60/s) e devolve a sua camera.
    GameCamera show(const Snapshot &s) {
        items = s.items;
        sky = s.sky;
        overlay = s.overlay;
        hud = s.hud;
        hp = s.hp;
        hpOverlay = s.hpOverlay;
        sprites = s.sprites;
        fx.systems = s.fx;
        fxRam_ = s.fxRam;
        fx.flares = s.flares;
        fx.synth.clear();   // sprites do modo foto: refeitos a cada quadro por ele
        worldLights = s.worldLights;
        emitters = s.emitters;
        shadows = s.shadows;
        screenRects = s.screenRects;
        screenPolys = s.screenPolys;
        lights_ = s.lights;
        fog_ = s.fog;
        cam_ = s.cam;
        vctx = s.vctx;
        eyeOk_ = s.cam.ok;
        for (int i = 0; i < 3; ++i) eye_[i] = s.cam.pos[i];
        hudMix_.on = false;
        return s.cam;
    }

    // Interpolacao de quadros (etapa 1: camera, objetos, paleta de ossos, ceu e sombras; wotm_live.inc, "interpolacao"): o show de B
    // com as matrizes misturadas com as de A em alpha (0 = A, 1 = B). Casamento por (cs, no, ocorrencia); salto de translacao grande
    // ou LOD diferente = B sem mistura. HUD, icones e lista de alta prioridade, que o jogo poe na frente da camera a cada quadro, nao
    // sao misturados: vao para a camera misturada pela posicao relativa a camera de B (misturar a matriz deles tremia contra a cena).
    // Particulas e destroços recuam pela velocidade (etapa 2; Particles::stepBack). Lens flare, barras sobre o inimigo e icone de vida,
    // que o jogo projetou com a camera de B, vao para a misturada (etapa 3; blendFlares, blendAnchors); o alfa dos fades mistura com o
    // de A quando o retangulo e o mesmo. Texto fica o de B. Corte de camera = show(B). Devolve a camera desenhada; cut diz se houve corte.
    GameCamera showBlend(const Snapshot &A, const Snapshot &B, float alpha, bool *cut = nullptr) {
        const GameCamera cb = show(B);
        if (cut) *cut = false;
        if (alpha >= 1.f) return cb;
        alpha = std::max(alpha, 0.f);
        if (interp::cameraCut(A.cam, B.cam) || !(A.vctx.view == B.vctx.view && A.vctx.w == B.vctx.w && A.vctx.h == B.vctx.h)) {
            if (cut) *cut = true;
            return cb;
        }
        const GameCamera c = interp::lerpCamera(A.cam, B.cam, alpha);
        std::map<std::pair<const void *, const void *>, std::shared_ptr<std::vector<M4>>> palCache;
        // presos a camera: M' = M_B . (camera B -> mundo)^-1 . (camera misturada -> mundo)
        const M4 invA = interp::rigidInverse(interp::cameraToWorld(A.cam)), invB = interp::rigidInverse(interp::cameraToWorld(B.cam));
        const M4 rebase = mul(invB, interp::cameraToWorld(c));
        // Objeto com a mesma pose relativa a camera em A e em B (o jogo o pos na frente dela: paineis do menu, objetos do AlignCsWithView
        // fora das listas do HUD): vai com a camera misturada, como o HUD, em vez de misturar a matriz no mundo
        attached_ = 0;
        auto sameRel = [&](const M4 &ma, const M4 &mb) {
            const M4 ra = mul(ma, invA), rb = mul(mb, invB);
            for (int i = 0; i < 4; ++i) {
                const float n = std::fabs(rb.m[i][0]) + std::fabs(rb.m[i][1]) + std::fabs(rb.m[i][2]);
                const float tol = i < 3 ? 1e-3f * (n + 1.f) : 0.02f + 1e-4f * n;
                for (int j = 0; j < 3; ++j)
                    if (!(std::fabs(ra.m[i][j] - rb.m[i][j]) <= tol)) return false;
            }
            return true;
        };
        auto mix = [&](const std::vector<DrawItem> &la, std::vector<DrawItem> &out) {
            // A: cadeia por (cs, no) na ordem da lista; B anda a cadeia na mesma ordem (ocorrencia)
            std::unordered_map<uint64_t, int> head;
            head.reserve(la.size() * 2);
            std::vector<int> next(la.size(), -1);
            for (int i = int(la.size()) - 1; i >= 0; --i) {
                const uint64_t k = (uint64_t(la[size_t(i)].cs) << 32) | la[size_t(i)].node;
                auto [it, ins] = head.try_emplace(k, i);
                if (!ins) { next[size_t(i)] = it->second; it->second = i; }
            }
            for (DrawItem &r : out) {
                const auto f = head.find((uint64_t(r.cs) << 32) | r.node);
                if (f == head.end() || f->second < 0) continue;
                const DrawItem &a = la[size_t(f->second)];
                f->second = next[size_t(f->second)];
                if (a.lodIdx != r.lodIdx) continue;
                const float dx = r.m.m[3][0] - a.m.m[3][0], dy = r.m.m[3][1] - a.m.m[3][1], dz = r.m.m[3][2] - a.m.m[3][2];
                if (dx * dx + dy * dy + dz * dz > 150.f * 150.f) continue;   // teleporte, _cs reaproveitado
                if (std::memcmp(&a.m, &r.m, sizeof(M4)) != 0) {
                    if (sameRel(a.m, r.m)) {
                        r.m = mul(r.m, rebase);
                        ++attached_;
                    } else
                        r.m = interp::lerpM4(a.m, r.m, alpha);
                }
                if (a.pal && r.pal && a.pal != r.pal && a.pal->size() == r.pal->size()) {
                    auto &sp = palCache[{a.pal.get(), r.pal.get()}];
                    if (!sp) {
                        sp = std::make_shared<std::vector<M4>>(*r.pal);
                        for (size_t k = 0; k < sp->size(); ++k)
                            if (std::memcmp(&(*a.pal)[k], &(*r.pal)[k], sizeof(M4)) != 0) (*sp)[k] = interp::lerpM4((*a.pal)[k], (*r.pal)[k], alpha);
                    }
                    r.pal = sp;
                }
                r.fade = a.fade + (r.fade - a.fade) * alpha;
            }
        };
        mix(A.items, items);
        mix(A.sky, sky);
        for (ShadowBlocker &sb : shadows)
            for (const ShadowBlocker &sa : A.shadows)
                if (sa.index == sb.index) {
                    for (int i = 0; i < 3; ++i) sb.origin[i] = sa.origin[i] + (sb.origin[i] - sa.origin[i]) * alpha;
                    mix(sa.casters, sb.casters);
                    break;
                }
        for (std::vector<DrawItem> *l : {&hud, &overlay, &hp, &hpOverlay})
            for (DrawItem &it : *l) it.m = mul(it.m, rebase);
        // Particulas: recuadas pela velocidade (1 - alpha) do passo de B (Particles::stepBack); as que nasceram no passo ficam em B
        const uint32_t gap = B.fields - A.fields;
        static const bool fxOff = std::getenv("WOTM_INTERP_NOFX") != nullptr;   // diagnostico: particulas de B, como na etapa 1
        if (!fxOff && A.fields && gap >= 1 && gap <= 4) {
            const float f = float(gap), back = 1.f - alpha;
            static const bool log = std::getenv("WOTM_INTERPLOG") != nullptr;
            static uint32_t loggedG = 0;
            if (log && B.g != loggedG && A.fxRam && A.fxRamSize && B.fxRam && B.fxRamSize) {   // uma vez por par
                loggedG = B.g;
                logStepBack(A, B, f);
            }
            for (Sprite &sp : sprites) {   // destroços
                const float d[3] = {f * sp.step[0], f * sp.step[1], f * (sp.step[2] - sp.grav * f)};
                if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] > 400.f * 400.f) continue;
                for (int k = 0; k < 3; ++k) sp.pos[k] -= back * d[k];
            }
            if (B.fxRam && B.fxRamSize) {
                if (!fxMix_ || fxMixSize_ != B.fxRamSize) {
                    fxMix_.reset(new uint8_t[B.fxRamSize], std::default_delete<uint8_t[]>());
                    fxMixSize_ = B.fxRamSize;
                }
                uint8_t *dst = fxMix_.get();
                Particles::blendRam(dst, Ram{B.fxRam.get(), B.fxRamSize}, fx.systems, back, f);
                // worldToScreenMat da vista com a camera misturada (strite, linha e neblina a leem): os eixos de B trocados pelos
                // misturados, M' = M_B C_B^T C_I nas colunas 0-2 (C = linhas direita, frente, cima; d = p - olho); o olho em worldCtx.eo
                const uint32_t v = uint32_t(std::clamp(B.vctx.view, 0, 4));
                const M4 CB = interp::cameraToWorld(B.cam), CI = interp::cameraToWorld(c);
                float T[3][3];
                for (int j = 0; j < 3; ++j)
                    for (int k = 0; k < 3; ++k) T[j][k] = CB.m[0][j] * CI.m[0][k] + CB.m[1][j] * CI.m[1][k] + CB.m[2][j] * CI.m[2][k];
                float M[16], eo[3];
                std::memcpy(M, dst + addr::worldToScreenMat + 0x40 * v, sizeof M);
                for (int r = 0; r < 4; ++r) {
                    float row[3];
                    for (int k = 0; k < 3; ++k) row[k] = M[4 * r] * T[0][k] + M[4 * r + 1] * T[1][k] + M[4 * r + 2] * T[2][k];
                    for (int k = 0; k < 3; ++k) M[4 * r + k] = row[k];
                }
                std::memcpy(dst + addr::worldToScreenMat + 0x40 * v, M, sizeof M);
                for (int k = 0; k < 3; ++k) eo[k] = -c.pos[k];
                std::memcpy(dst + addr::worldCtx + 0x1F0 * v + 0x20, eo, sizeof eo);
                fxRam_ = fxMix_;
            }
        }
        // 2D presos ao 3D: da tela de B para a da camera misturada (foco de B da worldToScreenMat da vista; o FOV misturado o escala)
        static const bool hudOff = std::getenv("WOTM_INTERP_NOHUD") != nullptr;   // diagnostico: flares, barras e fades de B
        if (!hudOff) {
            hudMix_.on = true;
            hudMix_.view = B.vctx.view;
            hudMix_.alpha = alpha;
            hudMix_.CB = interp::cameraToWorld(B.cam);
            hudMix_.CI = interp::cameraToWorld(c);
            interp::Focus fb;
            float M[16];
            const uint32_t wm = addr::worldToScreenMat + 0x40 * uint32_t(std::clamp(B.vctx.view, 0, 4));
            if (B.fxRam && B.fxRamSize >= wm + sizeof M) {
                std::memcpy(M, B.fxRam.get() + wm, sizeof M);
                if (!interp::focusOf(M, fb)) fb = {};
            } else {   // sem a copia de sombra (WOTM_FXLIVE): pelo FOV, na vista de 224 linhas
                fb.fv = 112.f / std::tan(B.cam.fovy * 3.14159265f / 360.f);
                fb.fh = fb.fv * (384.f / 180.48f);
            }
            auto scaled = [&](const GameCamera &o) {   // foco de outra camera: o de B escalado pelo FOV
                float k = std::tan(B.cam.fovy * 3.14159265f / 360.f) / std::tan(o.fovy * 3.14159265f / 360.f);
                if (!(std::isfinite(k) && k > 0.f)) k = 1.f;
                return interp::Focus{fb.fh * k, fb.fv * k};
            };
            hudMix_.CA = interp::cameraToWorld(A.cam);
            hudMix_.fb = fb;
            hudMix_.fa = scaled(A.cam);
            hudMix_.fi = scaled(c);
            static const bool log = std::getenv("WOTM_INTERPLOG") != nullptr;
            static uint32_t loggedG = 0;
            if (log && B.g != loggedG) {   // uma vez por par
                loggedG = B.g;
                logHudBack(A, B);
            }
            blendFlares(fx.flares);
            blendAnchors(B.anchors, &A.anchors, screenRects, &screenPolys);
            // fades: o k-esimo retangulo que nao e barra, mesmo lugar e cor em A e B
            size_t ia = 0;
            for (ScreenRect &r : screenRects) {
                if (r.barView >= 0) continue;
                while (ia < A.screenRects.size() && A.screenRects[ia].barView >= 0) ++ia;
                if (ia >= A.screenRects.size()) break;
                const ScreenRect &a = A.screenRects[ia++];
                if (a.x0 == r.x0 && a.y0 == r.y0 && a.x1 == r.x1 && a.y1 == r.y1 && a.r == r.r && a.g == r.g && a.b == r.b && a.tex0 == r.tex0 &&
                    a.underText == r.underText)
                    r.a = static_cast<uint8_t>(std::lround(a.a + (float(r.a) - a.a) * alpha));
            }
        }
        cam_ = c;
        eyeOk_ = c.ok;
        for (int i = 0; i < 3; ++i) eye_[i] = c.pos[i];
        return c;
    }

    // Diagnostico da etapa 3 (WOTM_INTERPLOG): flares e icones de vida de B levados para a camera de A (alfa 0) contra os que o jogo
    // projetou em A (o mais proximo, com o mesmo id no flare); "sem" = a mesma distancia sem reprojetar. Em px do GS. Icones: so os
    // reprojetados (os presos na borda da tela vao pela posicao na tela de A e B, sem erro a medir), e quantos foram pela tela.
    void logHudBack(const Snapshot &A, const Snapshot &B) {
        const BlendView keep = hudMix_;
        hudMix_.CI = hudMix_.CA;
        hudMix_.fi = hudMix_.fa;
        hudMix_.alpha = 0.f;
        Particles::FlareFrame fr = B.flares;
        blendFlares(fr);
        auto nearest = [](float x, float y, const auto &list, auto same) {
            float best = 1e30f;
            for (const auto &o : list)
                if (same(o)) best = std::min(best, std::hypot(o.x - x, o.y - y));
            return best;
        };
        int nf = 0;
        double ef = 0, sf = 0;
        for (const Particles::Flare &fl : fr.e) {
            auto same = [&](const Particles::Flare &o) { return o.id == fl.id; };
            const float e = nearest(fl.x, fl.y, A.flares.e, same);
            const Particles::Flare *orig = nullptr;
            for (const Particles::Flare &o : B.flares.e)
                if (o.id == fl.id) { orig = &o; break; }
            if (e > 60.f || !orig) continue;   // sem par em A
            ++nf;
            ef += e;
            sf += nearest(orig->x, orig->y, A.flares.e, same);
        }
        int na = 0, ns = 0;
        double ea = 0, sa = 0;
        for (const HudAnchor &b : B.anchors) {
            float dx, dy;
            int mode = 0;
            if (b.view != hudMix_.view || !anchorShift(b, &A.anchors, dx, dy, &mode)) continue;
            if (mode == 2) { ++ns; continue; }
            auto same = [&](const HudAnchor &o) { return o.view == b.view; };
            const float e = nearest(b.x + dx, b.y + dy, A.anchors, same);
            if (e > 60.f) continue;
            ++na;
            ea += e;
            sa += nearest(b.x, b.y, A.anchors, same);
        }
        std::fprintf(stderr, "[interp] hud g=%u: %d flares, erro medio %.2f px (sem reprojetar %.2f); %d icones de vida, %.2f (sem %.2f), %d pela tela; "
                     "%d objetos presos a camera\n",
                     B.g, nf, nf ? ef / nf : 0., nf ? sf / nf : 0., na, na ? ea / na : 0., na ? sa / na : 0., ns, attached_);
        hudMix_ = keep;
    }

    // Interpolacao (etapa 3): lens flare da tela de B para a da camera misturada do ultimo showBlend (o do infinito so pela direcao;
    // a profundidade W e o Z do GS refeitos). Sai o que ficou atras da camera ou fora da tela. Sem showBlend em vigor, nada.
    void blendFlares(Particles::FlareFrame &fr) const {
        if (!hudMix_.on) return;
        const interp::Focus &fb = hudMix_.fb, &fi = hudMix_.fi;
        const float x0 = -0.5f * fr.vw - 3.f, x1 = 0.5f * fr.vw + 3.f, y0 = -0.5f * fr.vh - 3.f, y1 = 0.5f * fr.vh + 3.f;
        std::vector<Particles::Flare> out;
        out.reserve(fr.e.size());
        for (Particles::Flare fl : fr.e) {
            const bool inf = (fl.flags & 0x80) != 0;
            const float dx = (fl.x - fr.cx) / fb.fh, dy = (fl.y - fr.cy) / fb.fv;
            float w[3], q[3];
            if (inf) {
                const float qb[3] = {dx, 1.f, -dy};
                interp::fromCam(hudMix_.CB, qb, false, w);
                interp::toCam(hudMix_.CI, w, false, q);
                if (q[1] <= 1e-4f) continue;
            } else {
                if (!(fl.w > 0.f)) { out.push_back(fl); continue; }
                const float qb[3] = {dx * fl.w, fl.w, -dy * fl.w};
                interp::fromCam(hudMix_.CB, qb, true, w);
                interp::toCam(hudMix_.CI, w, true, q);
                if (q[1] < 1.f) continue;
                fl.w = q[1];
                fl.z = fr.zScale + fr.zOff / fl.w;
            }
            const float sx = fi.fh * q[0] / q[1], sy = -fi.fv * q[2] / q[1];
            if (sx < x0 || sx > x1 || sy < y0 || sy > y1) continue;
            fl.x = fr.cx + sx;
            fl.y = fr.cy + sy;
            out.push_back(fl);
        }
        fr.e.swap(out);
    }

    // Deslocamento na tela de um icone de vida de B no instante misturado do ultimo showBlend (falso = fica onde B o pos). Casa com o
    // icone de A mais proximo no mundo (prev; mesma vista, ate 100 unidades: o monstro anda entre A e B). Se o icone fica a mesma
    // distancia da projecao do seu ponto em A e em B (o jogo o pos sobre o monstro), o ponto misturado vai pela camera misturada
    // (mode 1); senao (o jogo o prendeu na borda da tela) mistura a posicao na tela de A e B (mode 2).
    bool anchorShift(const HudAnchor &b, const std::vector<HudAnchor> *prev, float &dx, float &dy, int *mode = nullptr) const {
        const HudAnchor *a = nullptr;
        float bd = 100.f * 100.f;
        if (prev)
            for (const HudAnchor &o : *prev) {
                const float d = (o.w[0] - b.w[0]) * (o.w[0] - b.w[0]) + (o.w[1] - b.w[1]) * (o.w[1] - b.w[1]) + (o.w[2] - b.w[2]) * (o.w[2] - b.w[2]);
                if (o.view == b.view && d < bd) { bd = d; a = &o; }
            }
        if (!a) return false;
        const float t = hudMix_.alpha;
        float xb, yb, xa, ya, xi, yi, w[3];
        for (int k = 0; k < 3; ++k) w[k] = a->w[k] + (b.w[k] - a->w[k]) * t;
        if (interp::project(hudMix_.CB, hudMix_.fb, b.w, xb, yb) && interp::project(hudMix_.CA, hudMix_.fa, a->w, xa, ya) &&
            std::fabs((b.x - xb) - (a->x - xa)) < 3.f && std::fabs((b.y - yb) - (a->y - ya)) < 3.f &&
            interp::project(hudMix_.CI, hudMix_.fi, w, xi, yi)) {
            dx = xi - xb;
            dy = yi - yb;
            if (mode) *mode = 1;
        } else {
            dx = (a->x - b.x) * (1.f - t);
            dy = (a->y - b.y) * (1.f - t);
            if (mode) *mode = 2;
        }
        return std::fabs(dx) < 200.f && std::fabs(dy) < 200.f;
    }

    // Interpolacao (etapa 3): barras sobre o inimigo (ScreenRect::anchor) e icone de vida (polys centrados no ponto anotado) da vista do
    // ultimo showBlend, levados para o instante misturado pelo anchorShift do ponto do mundo que o jogo projetou (HudAnchor; prev = os
    // de A).
    void blendAnchors(const std::vector<HudAnchor> &anchors, const std::vector<HudAnchor> *prev, std::vector<ScreenRect> &rects,
                      std::vector<TextQuad> *polys) const {
        if (!hudMix_.on || anchors.empty()) return;
        std::vector<std::array<float, 2>> shift(anchors.size(), {NAN, NAN});
        bool any = false;
        for (size_t i = 0; i < anchors.size(); ++i) {
            float dx, dy;
            if (anchors[i].view == hudMix_.view && anchorShift(anchors[i], prev, dx, dy)) {
                shift[i] = {dx, dy};
                any = true;
            }
        }
        if (!any) return;
        for (ScreenRect &r : rects) {
            if (r.barView != hudMix_.view || r.anchor < 0 || size_t(r.anchor) >= shift.size() || std::isnan(shift[size_t(r.anchor)][0])) continue;
            const auto &sh = shift[size_t(r.anchor)];
            r.x0 += sh[0]; r.x1 += sh[0]; r.y0 += sh[1]; r.y1 += sh[1];
        }
        if (!polys) return;
        for (TextQuad &t : *polys) {
            const float cx = 0.5f * (t.x0 + t.x1), cy = 0.5f * (t.y0 + t.y1);
            for (size_t i = 0; i < anchors.size(); ++i)
                if (!std::isnan(shift[i][0]) && std::fabs(anchors[i].x - cx) < 1.5f && std::fabs(anchors[i].y - cy) < 1.5f) {
                    t.x0 += shift[i][0]; t.x1 += shift[i][0]; t.y0 += shift[i][1]; t.y1 += shift[i][1];
                    break;
                }
        }
    }

    // Diagnostico do recuo (WOTM_INTERPLOG): cada particula que andou, recuada um passo inteiro (alfa 0), contra a mais proxima do
    // mesmo sistema em A; "sem" = a mesma distancia sem recuo. Destroços pelo passo guardado no Sprite, contra os do mesmo fx em A.
    static void logStepBack(const Snapshot &A, const Snapshot &B, float f) {
        const Ram ra{A.fxRam.get(), A.fxRamSize}, rb{B.fxRam.get(), B.fxRamSize};
        double eR = 0, eS = 0;
        long cnt = 0;
        auto nearest = [](const float p[3], const std::vector<std::array<float, 3>> &pts) {
            float best = 1e30f;
            for (const auto &q : pts) best = std::min(best, (p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]));
            return std::sqrt(best);
        };
        for (const FxSystem &s : B.fx) {
            if (std::none_of(A.fx.begin(), A.fx.end(), [&](const FxSystem &x) { return x.index == s.index; })) continue;
            Particles::StepBack o;
            if (!Particles::stepBack(rb, s.base, s.n, o) || !o.moved) continue;
            const int nA = int32_t(ra.u32(s.base + fxoff::numParticles));
            const uint32_t pa = ra.u32(s.base + fxoff::pos), pb = rb.u32(s.base + fxoff::pos), vb = rb.u32(s.base + fxoff::vel);
            if (nA <= 0 || nA > 1024 || !ra.ok(pa, 16 * uint32_t(nA)) || !rb.ok(pb, 16 * uint32_t(o.moved)) || !rb.ok(vb, 16 * uint32_t(o.moved))) continue;
            std::vector<std::array<float, 3>> pts(static_cast<size_t>(nA));
            for (int j = 0; j < nA; ++j)
                for (int k = 0; k < 3; ++k) pts[size_t(j)][size_t(k)] = ra.f32(pa + 16 * uint32_t(j) + 4 * uint32_t(k));
            for (int i = 0; i < o.moved; ++i) {
                float p[3], v[3], d[3], r[3];
                for (int k = 0; k < 3; ++k) { p[k] = rb.f32(pb + 16 * uint32_t(i) + 4 * uint32_t(k)); v[k] = rb.f32(vb + 16 * uint32_t(i) + 4 * uint32_t(k)); }
                Particles::stepOffset(o, v, f, d);
                for (int k = 0; k < 3; ++k) r[k] = p[k] - d[k];
                eR += nearest(r, pts);
                eS += nearest(p, pts);
                ++cnt;
            }
        }
        double sR = 0, sS = 0;
        long sc = 0;
        for (const Sprite &sp : B.sprites) {
            if (sp.step[0] == 0.f && sp.step[1] == 0.f && sp.step[2] == 0.f) continue;
            std::vector<std::array<float, 3>> pts;
            for (const Sprite &a : A.sprites)
                if ((a.key >> 12) == (sp.key >> 12)) pts.push_back({a.pos[0], a.pos[1], a.pos[2]});
            if (pts.empty()) continue;
            const float r[3] = {sp.pos[0] - f * sp.step[0], sp.pos[1] - f * sp.step[1], sp.pos[2] - f * (sp.step[2] - sp.grav * f)};
            sR += nearest(r, pts);
            sS += nearest(sp.pos, pts);
            ++sc;
        }
        // worldToScreenMat refeita com a camera de A (a do alfa 0, como no showBlend) contra a gravada em A: maior diferenca relativa
        float matErr = 0.f;
        {
            const uint32_t v = uint32_t(std::clamp(B.vctx.view, 0, 4)), wa = addr::worldToScreenMat + 0x40 * v;
            const M4 CB = interp::cameraToWorld(B.cam), CA = interp::cameraToWorld(A.cam);
            float T[3][3], scale = 0.f;
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k) T[j][k] = CB.m[0][j] * CA.m[0][k] + CB.m[1][j] * CA.m[1][k] + CB.m[2][j] * CA.m[2][k];
            for (int r = 0; r < 4; ++r)
                for (int k = 0; k < 3; ++k) {
                    const float m = rb.f32(wa + 16 * uint32_t(r)) * T[0][k] + rb.f32(wa + 16 * uint32_t(r) + 4) * T[1][k] + rb.f32(wa + 16 * uint32_t(r) + 8) * T[2][k];
                    const float a = ra.f32(wa + 16 * uint32_t(r) + 4 * uint32_t(k));
                    matErr = std::max(matErr, std::fabs(m - a));
                    scale = std::max(scale, std::fabs(a));
                }
            matErr = scale > 0.f ? matErr / scale : 0.f;
        }
        if (cnt || sc)
            std::fprintf(stderr, "[interp] recuo g=%u f=%.0f: %ld particulas, erro medio %.2f (sem recuo %.2f); %ld destroços, %.2f (sem %.2f); matriz %.5f\n", B.g, f, cnt,
                         cnt ? eR / double(cnt) : 0.0, cnt ? eS / double(cnt) : 0.0, sc, sc ? sR / double(sc) : 0.0, sc ? sS / double(sc) : 0.0, matErr);
    }

    // ---- Particulas (fx[], 0x5BA280: 98 x 0x2EE0 bytes; o VU1 monta os quadrados, aqui viram sprites voltados para a camera) ----
    // Por efeito: vis.attribs & 0xFF = tipo (3 sprite, 4 texsprite, 13 destroço, 14 pedestre, 15 poeira...), numParticles +0x2ED0, pos[] (+0x2EB0,
    // 16 bytes: xyz mundo e w = rotacao nos destroços), age[] (+0x2EB8, int16), info.lifetime (+0x2E4A), tamanho min/max (+0x2E38/+0x2E3C),
    // texLoad[0] (+0x2C40) = indice em texInfo e TEX0ptr (+0x2EBC) = TEX0 (psm, CBP da paleta). So destroços (13), com o atlas FragTex em
    // celulas de 8x8; destroço sem atlas vira circulo suave. Os outros tipos (inclusive a poeira composta, dustTexture) ficam com `fx`.
    void collectParticles(const Ram &ram) {
        static const bool off = std::getenv("WOTM_NOFX") != nullptr;   // diagnostico: sem particulas
        if (off) return;
        constexpr uint32_t kFx = wotm::sym::fx, kStride = 12000u;
        FxView fog{};   // o VU1 descarta a particula toda na neblina (F < 1; Particles::setFog)
        float row3[4];
        for (int k = 0; k < 4; ++k) row3[k] = ram.f32(viewW2S() + 48 + 4 * k);
        Particles::setFog(ram, fog, row3, eye_);
        for (uint32_t i = 1; i < fxaddr::count && sprites.size() < 6000; ++i) {   // fx[100]: o VU1 desenha destroços em fx[98] e fx[99]
            const uint32_t b = kFx + i * kStride;
            const uint32_t np = ram.u32(b + 0x2ED0);
            const uint32_t type = ram.u32(b + 0x2E90) & 0xFF;
            if (!np || np > 1024 || type != 13 || !Particles::inViewFov(ram, b, wide)) continue;   // so destroços; os outros tipos ficam com fx (wotm_fx.hpp: cor, textura e mistura do retail)
            const uint32_t pos = ram.u32(b + 0x2EB0), age = ram.u32(b + 0x2EB8);
            if (!ram.ok(pos, 16 * np) || !ram.ok(age, 2 * np)) continue;
            Particles::StepBack sb;   // interpolacao (showBlend): o passo de cada destroço
            const uint32_t velp = ram.u32(b + fxoff::vel);
            const bool haveStep = Particles::stepBack(ram, b, int(np), sb) && ram.ok(velp, 16 * uint32_t(sb.moved));
            const int life = int(int16_t(ram.u16(b + 0x2E30 + 0x1A)));
            const float minS = ram.f32(b + 0x2E30 + 8), maxS = ram.f32(b + 0x2E30 + 0xC);
            const uint32_t texLoad = ram.u16(b + 0x2C40);
            const uint32_t t0p = ram.u32(b + 0x2EBC);
            uint64_t tex0 = 0;
            if (ram.ok(t0p, 8)) tex0 = uint64_t(ram.u32(t0p)) | (uint64_t(ram.u32(t0p + 4)) << 32);
            // fragPacketInit: TextureInfo[+0x2E98] da o atlas (largura e altura em 1/16 de texel; celulas de 8x8 = 0x80), uma
            // cor RGBAQ por sistema e o contexto (alphaContext 0: TEST_1 0x5040B, alfa >= 0x40, e ZBUF gravando)
            const uint32_t ti = wotm::sym::TextureInfo + 8 * (ram.u32(b + 0x2E98) & 127);
            const uint32_t tw16 = ram.u16(ti + 4), th16 = ram.u16(ti + 6);
            const uint32_t cols = tw16 >> 7, rows = th16 >> 7;
            const bool atlas = type == 13 && texLoad != 0xFFFF && tex0 && cols && rows;
            uint32_t gsCol = 0x80808080u;
            if (ram.u32(b + 0x2E30) & 0x200) {   // cor em floats (0..255; alfa 0..1)
                const uint32_t cp = ram.u32(b + 0x2E58);
                if (ram.ok(cp, 16)) {
                    auto ub = [](float v) { return uint32_t(std::clamp(v, 0.f, 255.f)); };
                    gsCol = ub(ram.f32(cp)) | ub(ram.f32(cp + 4)) << 8 | ub(ram.f32(cp + 8)) << 16 | ub(ram.f32(cp + 12) * 128.f) << 24;
                }
            } else {
                gsCol = (ram.u32(fxaddr::colors + 4 * ram.u32(b + 0x2EA0)) & 0x00FFFFFFu) | 0x80000000u;
            }
            const bool opaque = ram.u32(b + 0x2EA8) == 0;
            const float counter = float(int32_t(ram.u32(b + 0x2DC0)));   // fragPacketBuild: +4 por quadro, vai ao q330.y
            for (uint32_t k = 0; k < np; ++k) {
                Sprite s;
                for (int c = 0; c < 3; ++c) s.pos[c] = ram.f32(pos + 16 * k + 4 * c);
                if (!std::isfinite(s.pos[0]) || !std::isfinite(s.pos[1]) || !std::isfinite(s.pos[2])) continue;
                if (Particles::foggedAt(fog, s.pos)) continue;
                const int a = int(int16_t(ram.u16(age + 2 * k)));
                if (a < 0 || (life > 0 && a >= life)) continue;
                const float t = life > 0 ? float(a) / float(life) : 0.f;
                s.key = i * 4096u + k;
                if (haveStep && int(k) < sb.moved) {
                    for (int c = 0; c < 3; ++c) s.step[c] = ram.f32(velp + 16 * k + 4 * uint32_t(c)) + sb.wind[c];
                    s.grav = sb.grav;
                    if (sb.keepZ) { s.step[2] = 0.f; s.grav = 0.f; }
                }
                if (atlas) {   // destroço: SPRITE do GS alinhado a tela (fragPacketInit/fragPacketBuild, microprograma 0x1180)
                    // pos.w (particleEmit, is13): float em [-2, 2) que o particleMove faz variar com vel.w, com os bits baixos sorteados.
                    //  - lado = max(minSize, |w| maxSize / 2) (q330 = (minSize, contador, 128, maxSize / 2)), sem giro;
                    //  - entrada de UV = bits(w) & (linhas * 4 - 1) (w da tag em q328): 4 por linha do atlas (normal, espelha V,
                    //    espelha U, os dois), u0 = 0x08, u1 = 0x88 em 1/16 de texel, v = 0x80 * linha + idem;
                    //  - coluna: FTOI4(contador * w) & ((colunas - 1) * 0x80) somado aos dois U (q329.w): o destroço passa pelas
                    //    celulas da linha conforme o contador anda (4 por quadro);
                    //  - PRIM 0x136 (SPRITE, TME, FGE, FST, sem ABE): opaco, com a cor do sistema; some quando o particleMove o tira.
                    const float w = ram.f32(pos + 16 * k + 12);
                    const uint32_t e = ram.u32(pos + 16 * k + 12) & (rows * 4 - 1);
                    const int32_t colOff = int32_t(counter * w * 16.f) & int32_t((cols - 1) * 0x80);
                    const uint32_t vRow = 0x80 * (e >> 2);
                    const uint32_t lo = 0x08, hi = 0x88;
                    const bool fu = (e & 3) == 2 || (e & 3) == 3, fv = (e & 3) == 1 || (e & 3) == 3;
                    const float uA = float((fu ? hi : lo) + colOff), uB = float((fu ? lo : hi) + colOff);
                    const float vA = float((fv ? hi : lo) + vRow), vB = float((fv ? lo : hi) + vRow);
                    s.uv[0] = uA / float(tw16); s.uv[1] = vA / float(th16); s.uv[2] = uB / float(tw16); s.uv[3] = vB / float(th16);
                    s.texId = texLoad; s.tex0 = tex0;
                    s.size = std::max(minS, std::fabs(w) * maxS * 0.5f);
                    s.rot = 0.f;
                    s.gsCol = gsCol;
                    s.opaque = opaque;
                    s.aref = opaque ? int16_t(0x40 - 1) : kGsAlphaRef;   // TEST_1 0x5040B: GEQUAL 0x40
                } else {       // poeira/fumaça: circulo suave que cresce e esvanece
                    s.size = minS + (maxS > minS ? (maxS - minS) * t : 0.f);
                    if (s.size < 1.f) s.size = 8.f;
                    s.rgba[0] = 0.78f; s.rgba[1] = 0.72f; s.rgba[2] = 0.65f;
                    s.rgba[3] = 0.55f * (1.f - t);
                }
                sprites.push_back(s);
            }
        }
    }

    unsigned fxTexture(const Ram &ram, uint32_t texId, uint64_t tex0, int &w, int &h) {
        static Texture2D soft{};
        if (!texId || !tex0) {
            if (!soft.id) {   // circulo suave (alfa radial)
                std::vector<uint32_t> px(64 * 64);
                for (int y = 0; y < 64; ++y)
                    for (int x = 0; x < 64; ++x) {
                        const float d = std::sqrt(float((x - 32) * (x - 32) + (y - 32) * (y - 32))) / 32.f;
                        const float al = d >= 1.f ? 0.f : (1.f - d) * (1.f - d);
                        px[size_t(y) * 64 + x] = 0x00FFFFFFu | (uint32_t(al * 255.f) << 24);
                    }
                Image img{px.data(), 64, 64, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
                soft = LoadTextureFromImage(img);
                SetTextureFilter(soft, TEXTURE_FILTER_BILINEAR);
                SetTextureWrap(soft, TEXTURE_WRAP_CLAMP);
            }
            w = h = 64;
            return soft.id;
        }
        const uint64_t key = (0xF0ull << 56) | (uint64_t(texId) << 32) | uint32_t(gs::Tex0::decode(tex0).cbp);
        auto it = tex_.find(key);
        if (it != tex_.end()) { w = it->second.width; h = it->second.height; return it->second.id; }
        Texture2D t{};
        std::vector<uint32_t> px;
        uint32_t tw = 0, th = 0;
        bool ok = decodeFxTex(ram, texId, tex0, px, tw, th);
        modParticle(ram, texId, tex0, ok, px, tw, th);
        if (ok) {
            Image img{px.data(), int(tw), int(th), 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            t = LoadTextureFromImage(img);
            // BILINEAR como o GS (as gotas do Plantboss saiam em blocos de texel e pareciam maiores: foto 2026-10-19_003247/001)
            if (t.id) { SetTextureFilter(t, TEXTURE_FILTER_BILINEAR); SetTextureWrap(t, TEXTURE_WRAP_CLAMP); }
        }
        applyTexFilter(t);
        tex_[key] = t;
        w = t.width; h = t.height;
        return t.id;
    }

    // Sprites ordenados do mais longe ao mais perto, sem escrever profundidade; um lote por textura.
    // Cantos (esq-baixo, dir-baixo, dir-cima, esq-cima) do destroço virado para a camera (R/U: eixos da tela), girado por s.rot.
    // Serve ao drawSprites e a checagem VU1.
    static void spriteCorners(const Sprite &s, const float R[3], const float U[3], float c[4][3]) {
        const float cs = std::cos(s.rot), sn = std::sin(s.rot), hs = s.size * 0.5f;
        const float cx[4] = {-1, 1, 1, -1}, cy[4] = {-1, -1, 1, 1};
        for (int a = 0; a < 3; ++a) {
            const float ax = (R[a] * cs + U[a] * sn) * hs, ay = (-R[a] * sn + U[a] * cs) * hs;   // eixos "direita" e "cima" girados
            for (int k = 0; k < 4; ++k) c[k][a] = s.pos[a] + ax * cx[k] + ay * cy[k];
        }
    }

    // Camera para os ObjSprite: R/U = eixos direita/cima da camera do raylib (a mesma do desenho), M = worldToScreenMat, focos fx/fy.
    struct SpriteView {
        bool ok = false;
        float R[3], U[3], eye[3], M[4][4], fx = 0.f, fy = 0.f;
        static SpriteView make(const Ram &ram, const float eye[3]) {
            SpriteView v;
            const Matrix mv = rlGetMatrixModelview();
            const float R[3] = {mv.m0, mv.m4, mv.m8}, U[3] = {mv.m1, mv.m5, mv.m9};
            for (int a = 0; a < 3; ++a) { v.R[a] = R[a]; v.U[a] = U[a]; v.eye[a] = eye[a]; }
            for (int r = 0; r < 4; ++r)
                for (int k = 0; k < 4; ++k) v.M[r][k] = ram.f32(viewW2S() + 16 * r + 4 * k);
            v.ok = Particles::screenFoci(v.M, v.fx, v.fy);
            return v;
        }
    };

    // Cantos do ObjSprite no mundo como o microprograma 0x2E58: centro c = m * (x, y, z), W = linha 3 de M * (c - olho), cantos
    // c -/+ (512, 256) * lado / W pixels do GS. No plano de vista do centro (mesmo W), o deslocamento e 512 * lado / fx ao longo de R e
    // 256 * lado / fy ao longo de -U (o y do GS desce). Ordem: (-, -), (+, -), (+, +), (-, +) do GS. Falso com W < 1 (o CLIP descarta).
    static bool objSpriteCorners(const SpriteView &v, const M4 &m, const ObjSprite &s, float c[4][3]) {
        if (!v.ok) return false;
        float p[3];
        xform(m, s.x, s.y, s.z, p);
        float w = v.M[3][3];
        for (int a = 0; a < 3; ++a) w += v.M[3][a] * (p[a] - v.eye[a]);
        if (!(w >= 1.f)) return false;
        const float hx = 512.f * s.size / v.fx, hy = 256.f * s.size / v.fy;
        const float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
        for (int k = 0; k < 4; ++k)
            for (int a = 0; a < 3; ++a) c[k][a] = p[a] + v.R[a] * hx * sx[k] - v.U[a] * hy * sy[k];
        return true;
    }

    void drawSprites(const Ram &ram) {
        if (sprites.empty() || !cam_.ok) return;
        float R[3] = {cam_.fwd[1] * cam_.up[2] - cam_.fwd[2] * cam_.up[1], cam_.fwd[2] * cam_.up[0] - cam_.fwd[0] * cam_.up[2], cam_.fwd[0] * cam_.up[1] - cam_.fwd[1] * cam_.up[0]};
        const float rn = std::sqrt(R[0] * R[0] + R[1] * R[1] + R[2] * R[2]);
        if (rn < 1e-6f) return;
        for (float &v : R) v /= rn;
        const float *U = cam_.up;
        std::vector<std::pair<float, uint32_t>> order;
        order.reserve(sprites.size());
        for (uint32_t i = 0; i < sprites.size(); ++i) {
            const Sprite &s = sprites[i];
            const float dx = s.pos[0] - cam_.pos[0], dy = s.pos[1] - cam_.pos[1], dz = s.pos[2] - cam_.pos[2];
            if (dx * cam_.fwd[0] + dy * cam_.fwd[1] + dz * cam_.fwd[2] < 1.f) continue;   // atras da camera
            order.push_back({-(dx * dx + dy * dy + dz * dz), i});   // mais longe primeiro
        }
        std::sort(order.begin(), order.end());
        rlDisableBackfaceCulling();
        rlDisableDepthMask();
        shaderNormal(0.f, true, true);   // neblina (FGE) e alfa do GS no shader do draw, como o XGKICK dos destroços
        unsigned cur = ~0u;
        bool open = false, curOpaque = false;
        int16_t curRef = kGsAlphaRef;
        gsBlend(kGsAlphaNormal);
        for (const auto &o : order) {
            const Sprite &s = sprites[o.second];
            int tw = 0, th = 0;
            const unsigned id = fxTexture(ram, s.texId, s.tex0, tw, th);
            if (!id) continue;
            if (id != cur || !open || s.opaque != curOpaque || s.aref != curRef) {
                if (open) { rlEnd(); rlDrawRenderBatchActive(); }
                if (s.opaque != curOpaque) {   // destroço opaco: sem ABE (PRIM 0x136) e gravando profundidade
                    gsBlend(s.opaque ? kGsNoAbe : kGsAlphaNormal);
                    if (s.opaque) rlEnableDepthMask(); else rlDisableDepthMask();
                    curOpaque = s.opaque;
                }
                if (s.aref != curRef) { setAlphaRef(s.aref); curRef = s.aref; }
                rlBegin(RL_TRIANGLES);
                rlSetTexture(id);
                cur = id; open = true;
            }
            const float u0 = s.uv[0], v0 = s.uv[1], u1 = s.uv[2], v1 = s.uv[3];
            // cor e alfa no modo GS do shader: 0x80 = 1,0 (a textura traz o alfa cru)
            auto ch = [](float v) { return uint8_t(128.f * std::max(0.f, std::min(1.f, v))); };
            unsigned char cr = ch(s.rgba[0]), cg = ch(s.rgba[1]), cb = ch(s.rgba[2]), ca = ch(s.rgba[3]);
            if (s.gsCol) { cr = uint8_t(s.gsCol); cg = uint8_t(s.gsCol >> 8); cb = uint8_t(s.gsCol >> 16); ca = uint8_t(s.gsCol >> 24); }
            float c[4][3];
            spriteCorners(s, R, U, c);
            const float cu[4] = {u0, u1, u1, u0}, cv[4] = {v1, v1, v0, v0};
            const int tri[6] = {0, 1, 2, 0, 2, 3};
            for (int q = 0; q < 6; ++q) {
                const int k = tri[q];
                rlColor4ub(cr, cg, cb, ca);
                rlTexCoord2f(cu[k], cv[k]);
                rlVertex3f(c[k][0], c[k][1], c[k][2]);
            }
        }
        if (open) { rlEnd(); rlDrawRenderBatchActive(); }
        if (curRef != kGsAlphaRef) setAlphaRef(kGsAlphaRef);
        gsBlend(kGsAlphaNormal);
        rlNormal3f(0.f, 0.f, 0.f);
        rlSetTexture(0);
        rlEnableDepthMask();
        rlEnableBackfaceCulling();
    }

    // Diagnostico: faixa de UV por item texturizado (agrupada por tamanho da faixa).
    void dumpUv(const Ram &ram) {
        int shown = 0;
        for (const DrawItem &it : items) {
            const MeshData *md = mesh(ram, it.node);
            if (!md || md->tris.empty() || !texKey(ram, it.node)) continue;
            float u0 = 1e9f, u1 = -1e9f, v0 = 1e9f, v1 = -1e9f;
            for (const Vtx &v : md->tris) { u0 = std::min(u0, v.u); u1 = std::max(u1, v.u); v0 = std::min(v0, v.v); v1 = std::max(v1, v.v); }
            if ((u1 - u0 > 1.01f || v1 - v0 > 1.01f || u0 < -0.01f || v0 < -0.01f) && shown++ < 12)
                std::fprintf(stderr, "uv node=%06x tex=%u u[%.2f %.2f] v[%.2f %.2f] tris=%zu\n", it.node, ram.u16(it.node + 0x50), u0, u1, v0, v1, md->tris.size() / 3);
        }
        std::fprintf(stderr, "uv fora de [0,1]: %d itens (mostrados ate 12)\n", shown);
    }

    // Diagnostico (WOTM_UVISO): razao |grad u| / |grad v| por tamanho de textura. Deu 1,0 ate em 128x64: so mostra que as texturas
    // 128x64 sao arte quadrada comprimida. NAO prova UV em texels: renderizar com texel=uv/16 gera ruido; o UV normalizado (4096 = 1,0) esta certo.
    void dumpUvIso(const Ram &ram) {
        std::map<std::pair<uint32_t, uint32_t>, std::vector<float>> byDim;
        for (const DrawItem &it : items) {
            const MeshData *md = mesh(ram, it.node);
            if (!md || md->skinned || md->tris.empty() || !texKey(ram, it.node)) continue;
            const uint32_t ptr = ram.u32(addr::texInfo + 16 * ram.u16(it.node + 0x50)) & 0x0FFFFFFF;
            const UploadHdr hd = UploadHdr::read(ram, ptr);
            const uint32_t w = hd.w, h = hd.h;
            if (!w || !h) continue;
            for (size_t i = 0; i + 2 < md->tris.size(); i += 3) {
                const Vtx &a = md->tris[i], &b = md->tris[i + 1], &c = md->tris[i + 2];
                const double e1[3] = {b.x - a.x, b.y - a.y, b.z - a.z}, e2[3] = {c.x - a.x, c.y - a.y, c.z - a.z};
                const double g11 = e1[0]*e1[0] + e1[1]*e1[1] + e1[2]*e1[2], g22 = e2[0]*e2[0] + e2[1]*e2[1] + e2[2]*e2[2];
                const double g12 = e1[0]*e2[0] + e1[1]*e2[1] + e1[2]*e2[2];
                const double det = g11 * g22 - g12 * g12;
                if (det < 1e-3 * g11 * g22 || g11 < 1e-4) continue;   // degenerado/fino demais
                // |grad f|^2 = [d1 d2] G^-1 [d1 d2]^T, com d = diferencas de f ao longo de e1/e2
                auto gmag = [&](double d1, double d2) { return std::sqrt((g22 * d1 * d1 - 2 * g12 * d1 * d2 + g11 * d2 * d2) / det); };
                const double gu = gmag(b.u - a.u, c.u - a.u), gv = gmag(b.v - a.v, c.v - a.v);
                if (gu < 1e-7 || gv < 1e-7) continue;
                byDim[{w, h}].push_back(float(gu / gv));
            }
            {   // TW/TH do TEX0 (2^n) vs descritor, uma vez por textura
                static std::set<uint32_t> seenTex;
                const uint32_t tid = ram.u16(it.node + 0x50);
                if (seenTex.insert(tid).second) {
                    const gs::Tex0 t0 = gs::Tex0::decode(objectTex0(ram, it.node));
                    float u0 = 1e9f, u1 = -1e9f, v0 = 1e9f, v1 = -1e9f;
                    for (const Vtx &v : md->tris) { u0 = std::min(u0, v.u); u1 = std::max(u1, v.u); v0 = std::min(v0, v.v); v1 = std::max(v1, v.v); }
                    std::fprintf(stderr, "uviso-tex id=%u desc=%ux%u tex0 TW=%u TH=%u (=%ux%u) tfx=%u uv[%.2f %.2f][%.2f %.2f]\n", tid, w, h, t0.tw, t0.th, 1u << t0.tw, 1u << t0.th, t0.tfx, u0, u1, v0, v1);
                }
            }
        }
        for (auto &kv : byDim) {
            auto &v = kv.second;
            if (v.size() < 20) continue;
            std::sort(v.begin(), v.end());
            std::fprintf(stderr, "uviso tex %ux%u: tris=%zu razao |gu|/|gv| p25=%.3f med=%.3f p75=%.3f (h/w=%.3f)\n", kv.first.first, kv.first.second, v.size(),
                         v[v.size() / 4], v[v.size() / 2], v[v.size() * 3 / 4], double(kv.first.second) / kv.first.first);
        }
    }

    // Diagnostico (WOTM_SKINFIT="noA,noB,pal" em hex): compara variantes de skinning do no B com o no A (outro LOD do mesmo monstro,
    // tomado como referencia) pela distancia de Chamfer media entre as nuvens de vertices skinned.
    void fitSkin(const Ram &ram, uint32_t nodeA, uint32_t nodeB, uint32_t pal) {
        const MeshData *ma = mesh(ram, nodeA), *mb = mesh(ram, nodeB);
        if (!ma || !mb || ma->tris.empty() || mb->tris.empty()) { std::fprintf(stderr, "skinfit: malha vazia\n"); return; }
        auto cloud = [&](const MeshData *md, int variant) {
            std::vector<std::array<float, 3>> out;
            for (const Vtx &v : md->tris) {
                const int mask = (variant == 1) ? 0xFF : 0x7F;
                const float w = std::min(1.f, std::max(0.f, v.w));
                auto slotOf = [&](uint8_t c) { return int((c & mask) / 4); };
                float a[3], b[3];
                const int sa = slotOf(v.cx), sb = slotOf(v.cy);
                if (sa > 30 || sb > 30) { out.push_back({1e6f, 1e6f, 1e6f}); continue; }
                xform(readM4(ram, pal + 0x10 + 0x40 * sa), v.x, v.y, v.z, a);
                xform(readM4(ram, pal + 0x10 + 0x40 * sb), v.x, v.y, v.z, b);
                float ww = w;
                if (variant == 2) ww = 1.f - w;          // pesos trocados
                if (variant == 3) ww = 1.f;              // so o osso de x
                if (variant == 4) { ww = 0.f; }          // so o osso de y
                out.push_back({a[0] * ww + b[0] * (1 - ww), a[1] * ww + b[1] * (1 - ww), a[2] * ww + b[2] * (1 - ww)});
            }
            return out;
        };
        auto chamfer = [](const std::vector<std::array<float, 3>> &x, const std::vector<std::array<float, 3>> &y) {
            std::vector<double> d;
            const size_t step = std::max<size_t>(1, x.size() / 1500);
            for (size_t i = 0; i < x.size(); i += step) {
                float best = 1e30f;
                for (const auto &q : y) {
                    const float dx = x[i][0] - q[0], dy = x[i][1] - q[1], dz = x[i][2] - q[2];
                    best = std::min(best, dx * dx + dy * dy + dz * dz);
                }
                d.push_back(std::sqrt(double(best)));
            }
            std::sort(d.begin(), d.end());
            auto pc = [&](double q) { return d.empty() ? 0.0 : d[std::min(d.size() - 1, size_t(q * double(d.size())))]; };
            struct R { double med, p90, p99, mx; };
            return R{pc(0.5), pc(0.9), pc(0.99), d.empty() ? 0.0 : d.back()};
        };
        const auto ref = cloud(ma, 0);
        const char *names[5] = {"atual (mask 0x7F, w no osso x)", "mask 0xFF", "pesos trocados", "so osso x", "so osso y"};
        for (int v = 0; v < 5; ++v) {
            const auto c = cloud(mb, v);
            { const auto r1 = chamfer(c, ref), r2 = chamfer(ref, c); std::fprintf(stderr, "skinfit v%d (%s): B->A med %.1f p90 %.1f p99 %.1f max %.1f | A->B med %.1f p90 %.1f p99 %.1f max %.1f\n", v, names[v], r1.med, r1.p90, r1.p99, r1.mx, r2.med, r2.p90, r2.p99, r2.mx); }
        }
    }

    // Diagnostico (WOTM_BONECODES="no"): histograma dos codigos de osso (bit 0x80 e slot) de um no skinned.
    void boneCodes(const Ram &ram, uint32_t node) {
        const MeshData *md = mesh(ram, node);
        if (!md) return;
        size_t hi[3] = {0, 0, 0}, tot = 0, wlt1 = 0;
        std::map<int, size_t> slots[3];
        for (const Vtx &v : md->tris) {
            const uint8_t c[3] = {v.cx, v.cy, v.cz};
            for (int i = 0; i < 3; ++i) { if (c[i] & 0x80) ++hi[i]; ++slots[i][(c[i] & 0x7F) / 4]; }
            ++tot;
            if (v.w < 0.999f) ++wlt1;
        }
        std::fprintf(stderr, "bonecodes no=%06x vertices=%zu | com bit 0x80: x=%zu y=%zu z=%zu | w<1: %zu | slots usados em x: %zu, y: %zu\n", node, tot, hi[0], hi[1], hi[2], wlt1, slots[0].size(), slots[1].size());
        std::string row = "   slots x: ";
        for (auto &kv : slots[0]) row += std::to_string(kv.first) + "(" + std::to_string(kv.second) + ") ";
        std::fprintf(stderr, "%s\n", row.c_str());
    }

    // Diagnostico (WOTM_COVER="ref1,ref2,..:teste1,teste2"): cobertura na pose de referencia (posicoes cruas, sem skinning).
    // Amostra pontos na superficie dos nos de referencia e mede a fracao a menos de `tol` de algum triangulo dos nos de teste.
    void coverage(const Ram &ram, const std::vector<uint32_t> &refs, const std::vector<uint32_t> &tests, float tol) {
        auto gather = [&](const std::vector<uint32_t> &ns) {
            std::vector<std::array<float, 9>> t;
            for (uint32_t n : ns) {
                const MeshData *md = mesh(ram, n);
                if (!md) continue;
                static const uint32_t coverPal = std::getenv("WOTM_COVERPAL") ? uint32_t(std::strtoul(std::getenv("WOTM_COVERPAL"), nullptr, 16)) : 0;   // com paleta: skinned
                for (size_t k = 0; k + 2 < md->tris.size(); k += 3) {
                    if (coverPal) {
                        float q[3][3];
                        const M4 id = ident();
                        for (int c = 0; c < 3; ++c) skinVertex(ram, coverPal, id, md->tris[k + c], q[c]);
                        t.push_back({q[0][0], q[0][1], q[0][2], q[1][0], q[1][1], q[1][2], q[2][0], q[2][1], q[2][2]});
                    } else {
                        const Vtx &a = md->tris[k], &b = md->tris[k + 1], &c = md->tris[k + 2];
                        t.push_back({a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z});
                    }
                }
            }
            return t;
        };
        const auto R = gather(refs), T = gather(tests);
        // distancia ponto-triangulo (Ericson)
        auto dist2 = [](const float *p, const std::array<float, 9> &tr) {
            auto sub = [](const float *a, const float *b, float *o) { for (int i = 0; i < 3; ++i) o[i] = a[i] - b[i]; };
            auto dot = [](const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
            const float *a = &tr[0], *b = &tr[3], *c = &tr[6];
            float ab[3], ac[3], ap[3]; sub(b, a, ab); sub(c, a, ac); sub(p, a, ap);
            const float d1 = dot(ab, ap), d2 = dot(ac, ap);
            auto pt = [&](float u, float v, float w) { float q[3]; for (int i = 0; i < 3; ++i) q[i] = u * a[i] + v * b[i] + w * c[i]; float d[3]; sub(p, q, d); return dot(d, d); };
            if (d1 <= 0 && d2 <= 0) return pt(1, 0, 0);
            float bp[3]; sub(p, b, bp); const float d3 = dot(ab, bp), d4 = dot(ac, bp);
            if (d3 >= 0 && d4 <= d3) return pt(0, 1, 0);
            const float vc = d1 * d4 - d3 * d2;
            if (vc <= 0 && d1 >= 0 && d3 <= 0) { const float v = d1 / (d1 - d3); return pt(1 - v, v, 0); }
            float cp[3]; sub(p, c, cp); const float d5 = dot(ab, cp), d6 = dot(ac, cp);
            if (d6 >= 0 && d5 <= d6) return pt(0, 0, 1);
            const float vb = d5 * d2 - d1 * d6;
            if (vb <= 0 && d2 >= 0 && d6 <= 0) { const float w = d2 / (d2 - d6); return pt(1 - w, 0, w); }
            const float va = d3 * d6 - d5 * d4;
            if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) { const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6)); return pt(0, 1 - w, w); }
            const float den = 1.f / (va + vb + vc), v = vb * den, w = vc * den;
            return pt(1 - v - w, v, w);
        };
        size_t n = 0, ok = 0;
        uint32_t rng = 12345;
        auto rnd = [&]() { rng = rng * 1664525u + 1013904223u; return float(rng >> 8) / 16777216.f; };
        for (size_t s = 0; s < 4000 && !R.empty(); ++s) {
            const auto &tr = R[size_t(rnd() * float(R.size())) % R.size()];
            float u = rnd(), v = rnd(); if (u + v > 1) { u = 1 - u; v = 1 - v; }
            float p[3]; for (int i = 0; i < 3; ++i) p[i] = tr[i] * (1 - u - v) + tr[3 + i] * u + tr[6 + i] * v;
            float best = 1e30f;
            for (const auto &tt : T) best = std::min(best, dist2(p, tt));
            ++n; if (best < tol * tol) ++ok;
        }
        std::fprintf(stderr, "cover: %zu triangulos de teste cobrem %.1f%% da superficie de referencia (tol %.1f; %zu amostras)%c", T.size(), n ? 100.0 * double(ok) / double(n) : 0.0, tol, n, 10);
    }

    // Diagnostico (WOTM_SKINSAN=1): sanidade do skinning. Por triangulo, razao entre o perimetro skinned e o da pose de referencia;
    // vertices em triangulos com razao absurda sao "explodidos": agrupa por slot de osso e peso para achar o padrao.
    void dumpSkinSanity(const Ram &ram) {
        for (const DrawItem &it : items) {
            const MeshData *md = mesh(ram, it.node);
            if (!md || !md->skinned || md->tris.size() < 60) continue;
            const uint32_t pal = palette(ram, it);
            if (!pal) { std::fprintf(stderr, "skinsan no=%06x sem paleta\n", it.node); continue; }
            const M4 id = ident();
            size_t bad = 0, total = 0;
            std::map<std::string, size_t> why;
            for (size_t k = 0; k + 2 < md->tris.size(); k += 3) {
                float p[3][3];
                double o = 0, sk = 0;
                for (int c = 0; c < 3; ++c) skinVertex(ram, pal, id, md->tris[k + c], p[c]);
                for (int c = 0; c < 3; ++c) {
                    const Vtx &a = md->tris[k + c], &b = md->tris[k + (c + 1) % 3];
                    o += std::sqrt(double((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)));
                    const float *pa = p[c], *pb = p[(c + 1) % 3];
                    sk += std::sqrt(double((pa[0] - pb[0]) * (pa[0] - pb[0]) + (pa[1] - pb[1]) * (pa[1] - pb[1]) + (pa[2] - pb[2]) * (pa[2] - pb[2])));
                }
                ++total;
                if (o < 1e-4) continue;
                const double r = sk / o;
                if (r > 2.5 || r < 0.4) {
                    ++bad;
                    for (int c = 0; c < 3; ++c) {
                        const Vtx &v = md->tris[k + c];
                        char key[96];
                        std::snprintf(key, sizeof key, "cx=%02x(slot %d) cy=%02x(slot %d) cz=%02x w=%.2f", v.cx, (v.cx & 0x7F) / 4, v.cy, (v.cy & 0x7F) / 4, v.cz, v.w);
                        ++why[key];
                    }
                }
            }
            {   // fracao de triangulos com aresta mais longa que 3x a mediana (pontes espurias entre tiras)
                std::vector<float> longest;
                for (size_t k = 0; k + 2 < md->tris.size(); k += 3) {
                    float q[3][3];
                    for (int c = 0; c < 3; ++c) skinVertex(ram, pal, id, md->tris[k + c], q[c]);
                    float mx = 0;
                    for (int c = 0; c < 3; ++c) {
                        const float *a2 = q[c], *b2 = q[(c + 1) % 3];
                        mx = std::max(mx, std::sqrt((a2[0] - b2[0]) * (a2[0] - b2[0]) + (a2[1] - b2[1]) * (a2[1] - b2[1]) + (a2[2] - b2[2]) * (a2[2] - b2[2])));
                    }
                    longest.push_back(mx);
                }
                std::vector<float> sorted = longest;
                std::sort(sorted.begin(), sorted.end());
                const float med = sorted.empty() ? 0.f : sorted[sorted.size() / 2];
                size_t nl = 0;
                for (float l : longest) if (l > 3.f * med) ++nl;
                std::fprintf(stderr, "skinsan-longedge no=%06x mediana %.2f, triangulos > 3x: %zu de %zu (%.1f%%)\n", it.node, med, nl, longest.size(), longest.empty() ? 0.0 : 100.0 * nl / longest.size());
            }
            std::fprintf(stderr, "skinsan no=%06x tris=%zu ruins=%zu (%.1f%%)\n", it.node, total, bad, total ? 100.0 * bad / total : 0.0);
            std::vector<std::pair<size_t, std::string>> v;
            for (auto &kv : why) v.push_back({kv.second, kv.first});
            std::sort(v.rbegin(), v.rend());
            for (size_t i = 0; i < v.size() && i < 8; ++i) std::fprintf(stderr, "   %5zu x %s\n", v[i].first, v[i].second.c_str());
        }
    }

    // Diagnostico: lista os itens skinned (no, triangulos, posicao no mundo, paleta achada?).
    void dumpSkinned(const Ram &ram) {
        if (std::getenv("WOTM_LISTRIGID")) {   // diagnostico: partes rigidas dos personagens (itens nao skinned com animPkt): no, textura, raio, posicao no mundo, blend
            for (const DrawItem &it : items) {
                const MeshData *md = mesh(ram, it.node);
                if (!it.animPkt || (md && md->skinned)) continue;
                std::fprintf(stderr, "[rigid] pkt %x node %x tex %u tris %zu raio %.1f pos %.1f %.1f %.1f blend %d f30 %08x %08x\n", it.animPkt, it.node, unsigned(ram.u16(it.node + 0x50)),
                             md ? md->tris.size() / 3 : size_t(0), ram.f32(it.node + 8), it.m.m[3][0], it.m.m[3][1], it.m.m[3][2], int(it.blend), ram.u32(it.node + 0x30), ram.u32(it.node + 0x34));
            }
        }
        for (const DrawItem &it : items) {
            const MeshData *md = mesh(ram, it.node);
            if (!md || !md->skinned) continue;
            std::fprintf(stderr, "   m: [%.3f %.3f %.3f %.3f] [%.3f %.3f %.3f %.3f] [%.3f %.3f %.3f %.3f] [%.3f %.3f %.3f %.3f]%c", it.m.m[0][0], it.m.m[0][1], it.m.m[0][2], it.m.m[0][3], it.m.m[1][0], it.m.m[1][1], it.m.m[1][2], it.m.m[1][3], it.m.m[2][0], it.m.m[2][1], it.m.m[2][2], it.m.m[2][3], it.m.m[3][0], it.m.m[3][1], it.m.m[3][2], it.m.m[3][3], 10);
            std::fprintf(stderr, "skinned no=%06x tris=%zu pos=(%.0f %.0f %.0f) animPkt=%06x lod=%d pal=%06x\n", it.node, md->tris.size() / 3,
                         it.m.m[3][0], it.m.m[3][1], it.m.m[3][2], it.animPkt, it.lodIdx, palette(ram, it));
            {
                float u0 = 1e9f, u1 = -1e9f, v0 = 1e9f, v1 = -1e9f;
                for (const Vtx &v : md->tris) { u0 = std::min(u0, v.u); u1 = std::max(u1, v.u); v0 = std::min(v0, v.v); v1 = std::max(v1, v.v); }
                std::fprintf(stderr, "   uv u[%.2f %.2f] v[%.2f %.2f] abe=%d\n", u0, u1, v0, v1, int(md->abe));
                std::fprintf(stderr, "   lowAlpha=%d abe=%d (lowAlpha manda o objeto para o passe translucido, sem profundidade)\n", int(md->lowAlpha), int(md->abe));
                {   // histograma do alfa de vertice (a*4 efetivo) e media da cor: distingue camada opaca de camada de brilho/aditiva
                    unsigned h[8] = {0}; double sr = 0, sg = 0, sb = 0;
                    for (const Vtx &v : md->tris) { ++h[std::min(7, v.a * 4 / 32)]; sr += v.r; sg += v.g; sb += v.b; }
                    const double n = md->tris.empty() ? 1.0 : double(md->tris.size());
                    std::fprintf(stderr, "   alfa vtx (a*4 em oitavos de 256): %u %u %u %u %u %u %u %u | cor media rgb %.0f %.0f %.0f\n", h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7], sr / n, sg / n, sb / n);
                }
                std::fprintf(stderr, "   indices: %u total, %u fora de pos; descartados: %u por ADC, %u degenerados; emitidos %zu; posicoes %u, usadas %u\n", md->totIdx, md->oobIdx, md->dropAdc, md->dropDegen, md->tris.size() / 3, md->posTot, md->posUsed);
            }
            const uint64_t t0v = objectTex0(ram, it.node);
            const gs::Tex0 t0 = gs::Tex0::decode(t0v);
            const uint32_t ptr = ram.u32(addr::texInfo + 16 * ram.u16(it.node + 0x50)) & 0x0FFFFFFF;
            std::fprintf(stderr, "   texId=%u tex0=%016llx cbp=%u csa=%u base=%u psm=%u ptr=%06x hasPal=%d\n", ram.u16(it.node + 0x50),
                         (unsigned long long)t0v, t0.cbp, t0.csa, ram.u32(addr::tempVramTexAddr) >> 6, UploadHdr::read(ram, ptr).psm, ptr,
                         int(rtxPal.count(t0.cbp)));
        }
    }

    const MeshData *mesh(const Ram &ram, uint32_t node) {
        const uint32_t pp = ram.u32(node + 4) & 0x0FFFFFFF;
        auto it = cache_.find(pp);
        if (it != cache_.end()) {
            // revalida a assinatura (a RAM do nivel pode ter sido reaproveitada)
            if (it->second->sig == packetSig(ram, skipZeroQwords(ram, pp))) { lastMesh_ = it->second; return it->second.get(); }
        }
        auto md = std::make_shared<MeshData>(decodeObject(ram, node));
        cache_[pp] = md;
        lastMesh_ = md;
        return md.get();
    }
    // Troca a malha do objeto no cache por uma copia alterada por f (uma vez por decodificacao). Copia nova, nao a mesma alterada: a
    // arena residente guarda os vertices pelo ponteiro da malha. Usado pelo wotm_levelsel.inc (UV da foto das fases sem arte no SHELL).
    template <class F> bool patchMesh(const Ram &ram, uint32_t node, F &&f) {
        if (!mesh(ram, node)) return false;
        auto it = cache_.find(ram.u32(node + 4) & 0x0FFFFFFF);
        if (it == cache_.end()) return false;
        if (it->second->patched) return true;
        auto md = std::make_shared<MeshData>(*it->second);
        f(*md);
        md->patched = true;
        it->second = md;
        lastMesh_ = md;
        return true;
    }
    std::shared_ptr<const MeshData> lastMesh_;   // a malha do ultimo mesh() (drawList: o pedaco adiado a segura)

    // Limiar do teste de alfa no shader do draw (DrawItem::aref, unidades do alfa do GS: 0x80 = 1,0). Descarrega o lote antes: o
    // uniform vale para o que for desenhado depois.
    void setAlphaRef(int16_t r) {
        if (alphaRefLoc_ < 0) return;
        rlDrawRenderBatchActive();
        const float f = float(r);
        SetShaderValue(alphaShader_, alphaRefLoc_, &f, SHADER_UNIFORM_FLOAT);
    }

    // Desenha com o raylib (deve estar dentro de BeginMode3D).
    // Liga o shader do GS (neblina, alfa) para uma passada de objetos; devolve se ligou (quem chama faz EndShaderMode e zera
    // g_premulShader no fim).
    bool beginShader(const Ram &ram, bool wire) {
        // Teste de alfa: pixels transparentes (sombras, helices, grades) nao podem escrever profundidade nem cor.
        // A normal do vertice leva (fogBurnThru, 1, FGE + 2 * GS) (shaderNormal; o rlNormal3f normaliza, entao o shader le as razoes
        // por y); quem desenha a preenche e a zera no fim (normal 0 = sem neblina, alfa comum).
        //  - FGE: neblina (ver Fog).
        //  - GS: textura e vertice trazem cor e alfa crus do GS (0x80 = 1,0) e o shader modula como o GS (TFX MODULATE):
        //    cor = texel * Cv / 128 (Cv > 0x80 clareia) e As = min(At * Av / 128, 255) / 128 (ate 1,99: a funcao de textura satura
        //    em 255). Na mistura comum o As acima de 1 sai em tres passes (modos 3 a 5, gsBlendCommon);
        //    sem o modo GS, o produto comum (sombras, depuracao).
        //  - PM: mistura aditiva/subtrativa (gsBlend 1 e 2); a saida e a cor vezes o As inteiro, e a mistura usa fator 1.
        //  - modo (+8, +16): os dois passes de GsBlendFactors::doubled (alfa As / 2; saida branca).
        if (!alphaShaderTried_) {
            alphaShaderTried_ = true;
            static const char *vs =
                "#version 330\n"
                "in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal; in vec4 vertexColor;\n"
                "uniform mat4 mvp; uniform vec3 fogEye; uniform vec4 fogRow3; uniform vec3 fogParams;\n"
                "out vec2 fragTexCoord; out vec4 fragColor; out float fragFog; out float fragGsA; out float fragPm; out float fragMode; out float fragFge;\n"
                "void main() {\n"
                "  fragTexCoord = vertexTexCoord; fragColor = vertexColor;\n"
                // normal = (burn, 1, FGE + 2 * GS + 4 * PM) normalizada pelo rlNormal3f: as razoes por y sobrevivem; y = 0 = sem nada
                "  float burn = 0.0, fl = 0.0;\n"
                "  if (vertexNormal.y > 1e-6) { burn = vertexNormal.x / vertexNormal.y; fl = floor(vertexNormal.z / vertexNormal.y + 0.5); }\n"
                "  fragGsA = mod(floor(fl / 2.0), 2.0) > 0.5 ? 1.0 : 0.0;\n"
                "  fragPm = mod(floor(fl / 4.0), 2.0) > 0.5 ? 1.0 : 0.0;\n"
                "  fragMode = floor(fl / 8.0);\n"
                "  fragFge = mod(fl, 2.0) > 0.5 ? 1.0 : 0.0;\n"
                "  float w = dot(fogRow3.xyz, vertexPosition - fogEye) + fogRow3.w;\n"
                "  float f = 255.0;\n"
                "  if (mod(fl, 2.0) > 0.5 && w > 0.001) f = clamp(burn + fogParams.x + fogParams.z / w, 0.0, fogParams.y);\n"
                "  fragFog = f / 255.0;\n"
                "  gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
                "}\n";
            static const char *fs =
                "#version 330\n"
                "in vec2 fragTexCoord; in vec4 fragColor; in float fragFog; in float fragGsA; in float fragPm; in float fragMode; in float fragFge;\n"
                "uniform sampler2D texture0; uniform vec4 colDiffuse; uniform vec3 fogColor; uniform float fogSplit; uniform float alphaRef; uniform float gsTrunc; out vec4 finalColor;\n"
                "void main() { vec4 t = texture(texture0, fragTexCoord) * fragColor * colDiffuse;\n"
                // GS: cor = texel * cor do vertice / 128 (o vertice vem cru, 0x80 = 1,0; acima disso clareia), limitada a 255;
                // As = At * Av / 128, saturado em 255 pela funcao de textura (como a cor), / 128: ate 255/128
                "  float as = t.a;\n"
                // padrao (WOTM_GSTRUNC=0 desliga): as etapas inteiras do GS (texel inteiro, cor do DDA truncada, (Ct * Cv) >> 7 e (At * Av) >> 7 limitados a
                // 255); em float o nativo fica ~2 niveis mais claro (pista no VU1CHECK.md)
                "  if (fragGsA > 0.5 && gsTrunc > 0.5) {\n"
                "    vec4 tx = floor(texture(texture0, fragTexCoord) * 255.0 + 0.5);\n"
                "    vec4 cv = floor(fragColor * colDiffuse * 255.0 + 0.001);\n"
                "    t.rgb = min(floor(tx.rgb * cv.rgb / 128.0), 255.0) / 255.0;\n"
                "    as = min(floor(tx.a * cv.a / 128.0), 255.0) / 128.0; t.a = min(as, 1.0);\n"
                "  } else if (fragGsA > 0.5) { t.rgb = min(t.rgb * 1.9922, vec3(1.0)); as = min(t.a * 3.9690, 1.9922); t.a = min(as, 1.0); }\n"
                // teste de alfa do GS (TEST_1 do GROUP, DrawItem::aref; padrao kGsAlphaRef = alfa > 8)
                "  if (as * 128.0 <= alphaRef) discard;\n"
                "  if (fragMode > 1.5 && fragMode < 2.5) { finalColor = vec4(1.0); return; }\n"   // 2o passe do doubled (gsBlendDouble)
                // mistura comum com As > 1 (gsBlendCommon): 3o passe, destino * As (fora disso, sem efeito); 2o passe so onde As > 1
                "  if (fragMode > 4.5) { finalColor = as > 1.0 ? vec4(vec3(as - 1.0), 1.0) : vec4(0.0); return; }\n"
                "  if (fragMode > 3.5 && as <= 1.0) discard;\n"

                "  if (fogSplit < -1.5) { finalColor = vec4(vec3(1.0 - fragFog), 1.0); return; }\n"   // WOTM_FOGSPLIT=2: mascara da neblina
                "  float f = gl_FragCoord.x < fogSplit ? 1.0 : fragFog;\n"
                "  vec3 c = mix(fogColor, t.rgb, f);\n"
                // GS: F inteiro truncado; (F * C) >> 8 + ((255 - F) * Cf) >> 8 (com F = 255 sai C - 1); so com FGE
                "  if (fragGsA > 0.5 && gsTrunc > 0.5 && fragFge > 0.5 && gl_FragCoord.x >= fogSplit) {\n"
                "    float F = floor(f * 255.0 + 0.001);\n"
                "    c = (floor(F * floor(t.rgb * 255.0 + 0.5) / 256.0) + floor((255.0 - F) * floor(fogColor * 255.0 + 0.5) / 256.0)) / 255.0;\n"
                "  }\n"
                // aditivo/subtrativo: a cor ja vezes o As inteiro (a mistura usa fator 1; gsBlend)
                // doubled (modo 1): alfa As / 2, sem o limite de 1 (o 2o passe dobra o destino)
                // As > 1 na mistura comum: 1o passe sem efeito (alfa 0); 2o passe Cs - Cd (1 - 1/As)
                "  finalColor = fragPm > 0.5 ? vec4(c * as, 1.0) : fragMode > 3.5 ? vec4(c, 1.0 - 1.0 / as)\n"
                "             : fragMode > 2.5 ? vec4(c, as > 1.0 ? 0.0 : as) : vec4(c, fragMode > 0.5 ? as * 0.5 : t.a); }\n";
            alphaShader_ = LoadShaderFromMemory(vs, fs);
            fogLoc_[0] = GetShaderLocation(alphaShader_, "fogEye");
            fogLoc_[1] = GetShaderLocation(alphaShader_, "fogRow3");
            fogLoc_[2] = GetShaderLocation(alphaShader_, "fogParams");
            fogLoc_[3] = GetShaderLocation(alphaShader_, "fogColor");
            fogLoc_[4] = GetShaderLocation(alphaShader_, "fogSplit");
            alphaRefLoc_ = GetShaderLocation(alphaShader_, "alphaRef");
            setAlphaRef(kGsAlphaRef);
            // contas inteiras do GS no shader (modulacao, Gouraud, neblina), ligado por padrao: o chao do VEGAS no PCSX2 software sai
            // 2 niveis abaixo do float; WOTM_GSTRUNC=0 volta ao float
            const int gsTruncLoc = GetShaderLocation(alphaShader_, "gsTrunc");
            const float gsTrunc = (std::getenv("WOTM_GSTRUNC") && std::atoi(std::getenv("WOTM_GSTRUNC")) == 0) ? 0.0f : 1.0f;
            if (gsTruncLoc >= 0) SetShaderValue(alphaShader_, gsTruncLoc, &gsTrunc, SHADER_UNIFORM_FLOAT);
        }
        const bool useShader = !wire && alphaShader_.id > 0;
        if (useShader) {
            float row3[4];
            for (int k = 0; k < 4; ++k) row3[k] = ram.f32(viewW2S() + 48 + 4 * k);
            const float params[3] = {fog_.fa, fog_.maxVal, fog_.fb};
            if (fogLoc_[0] >= 0) SetShaderValue(alphaShader_, fogLoc_[0], eye_, SHADER_UNIFORM_VEC3);
            if (fogLoc_[1] >= 0) SetShaderValue(alphaShader_, fogLoc_[1], row3, SHADER_UNIFORM_VEC4);
            if (fogLoc_[2] >= 0) SetShaderValue(alphaShader_, fogLoc_[2], params, SHADER_UNIFORM_VEC3);
            if (fogLoc_[3] >= 0) SetShaderValue(alphaShader_, fogLoc_[3], fog_.col, SHADER_UNIFORM_VEC3);
            // diagnostico: WOTM_FOGSPLIT=1 tira a neblina da metade esquerda da tela (comparacao no mesmo quadro); =2 desenha a mascara
            // da neblina (branco = so FOGCOL) e imprime os parametros a cada ~2 s
            static const int split = std::getenv("WOTM_FOGSPLIT") ? std::atoi(std::getenv("WOTM_FOGSPLIT")) : 0;
            // =3: neblina so nos quadros pares (duas capturas seguidas = com e sem, quase a mesma cena)
            static uint32_t drawCount = 0;
            const bool oddFrame = (drawCount++ & 1) != 0;
            const float splitX = split == 1 ? 0.5f * float(rlGetFramebufferWidth()) : split == 2 ? -2.f
                               : (split == 3 && oddFrame) ? 1e9f : -1.f;
            static int fogLog = 0;
            if (split && fogLog++ % 120 == 0)
                std::fprintf(stderr, "[fog] FA %.3f FB %.1f max %.1f cor %.0f %.0f %.0f row3 %.3f %.3f %.3f %.3f\n", fog_.fa, fog_.fb, fog_.maxVal,
                             fog_.col[0] * 255.f, fog_.col[1] * 255.f, fog_.col[2] * 255.f, row3[0], row3[1], row3[2], row3[3]);
            if (fogLoc_[4] >= 0) SetShaderValue(alphaShader_, fogLoc_[4], &splitX, SHADER_UNIFORM_FLOAT);
            BeginShaderMode(alphaShader_);
            g_premulShader = true;
        }
        vs_.setShader(useShader ? alphaShader_.id : 0u, useShader ? alphaShader_.locs : nullptr);   // o do rlgl agora (0 = o padrao)
        return useShader;
    }

    void draw(const Ram &ram, bool wire = false) {
        ViewScope vs(vctx);
        if (drawProfOn()) ++drawProf().views;
        const bool useShader = beginShader(ram, wire);
        rlDisableDepthTest();   // o ceu e desenhado primeiro, sem profundidade
        {
            ProfScope ps(DrawProf::Sky);
            drawList(ram, sky, wire);
            rlDrawRenderBatchActive();
        }
        rlEnableDepthTest();
        static const bool onePass = std::getenv("WOTM_ONEPASS") != nullptr;   // diagnostico: tudo num passe so (sem separar translucidos)
        // Como o hier: fora da OT de alfa, opacos e translucidos juntos na ordem da travessia (gravando Z conforme o ZMSK do GROUP); a
        // OT de alfa (passe 1) depois. Com dois passes (opacos primeiro), a previa da fase (tex 102, As ~ 1) saia antes do logo do
        // telao (tex 140/141, sem gravar Z) e o logo ficava por cima; no GS a previa vem depois e o cobre.
        {
            ProfScope ps(DrawProf::Trav);
            drawList(ram, items, wire, (wire || onePass) ? -1 : 2);
            rlDrawRenderBatchActive();
        }
        if (!wire && !onePass) {
            ProfScope ps(DrawProf::AlphaOT);
            rlDisableDepthMask();   // translucidos: o drawList grava profundidade item a item pelo ZBUF_1.ZMSK (DrawItem::zmsk)
            drawList(ram, items, wire, 1);
            rlDrawRenderBatchActive();
            rlEnableDepthMask();
        }
        // sombras projetadas depois da OT de alfa: no XGKICK os receptores redesenhados com a textura da sombra vem depois dela
        if (!wire) { ProfScope ps(DrawProf::Shadows); drawShadows(ram); }
        // particulas por ultimo (testam profundidade, nao escrevem): destroços (drawSprites), depois os demais tipos na ordem de particleDraw
        if (!wire) { ProfScope ps(DrawProf::Sprites); drawSprites(ram); }
        {
            ProfScope ps(DrawProf::Fx);
            // Teste de alfa das particulas: os pacotes translucidos nao mexem no TEST e herdam o 0x7000D (As > 0) do restoreScreenDrawEnv;
            // os destroços trazem o proprio TEST e fecham com o setClosingContext (0x5004B: As >= 4). Medido pela checagem (TEST do XGKICK).
            if (!wire) setAlphaRef(sprites.empty() ? 0 : 3);
            // particulas com a RAM do instante da captura (fxRam_), nao a viva que o jogo ja reescreve; texturas seguem da RAM viva
            if (!wire) fx.draw(fxRam_ ? Ram{fxRam_.get(), ram.size} : ram, [&](uint32_t id, uint64_t t0) { return particleTexture(ram, id, t0); }, eye_);
            if (!wire) setAlphaRef(kGsAlphaRef);
            if (!wire && !deferFlares && !fx.flares.e.empty()) {   // lens flare: tela, depois das particulas
                if (fullDepth) {   // profundidade sem esperar a GPU, como na tela dividida (drawFlaresFor)
                    rlDrawRenderBatchActive();
                    fullDepth->issue(rlGetFramebufferWidth(), rlGetFramebufferHeight(), fullDepthFbo, redrawId);
                    fx.depthAsync = fullDepth;
                }
                fx.drawFlares([&](uint32_t id, uint64_t t0) { return particleTexture(ram, id, t0); });
                fx.depthAsync = nullptr;
            }
            rlDrawRenderBatchActive();
        }
        ProfScope psHud(DrawProf::Hud);
        if (!wire && barPass.rects) {   // barras sobre os inimigos sob o HUD do jogador (barPass), fora do shader do GS
            rlDrawRenderBatchActive();
            if (useShader) { EndShaderMode(); g_premulShader = false; vs_.setShader(0u, nullptr); }
            drawBarPass();
            if (useShader) { BeginShaderMode(alphaShader_); g_premulShader = true; vs_.setShader(alphaShader_.id, alphaShader_.locs); }
            gsBlend(kGsAlphaNormal);   // o drawList comeca supondo a mistura comum do GS (o EndBlendMode deixou a do raylib)
        }
        rlDisableDepthTest();   // icones de botao e HUD por cima de tudo (z perto de 0: o teste de profundidade os esconde sob o chao)
        const bool sprites2d = !wire && g_buttonIconFn && (!g_buttonIconsOn || g_buttonIconsOn());
        if (!sprites2d) drawList(ram, overlay, wire, -1, true);
        drawList(ram, hud, wire, -1);   // HUD por ultimo, na ordem da travessia
        rlDrawRenderBatchActive();
        rlEnableDepthTest();
        if (useShader) EndShaderMode();
        g_premulShader = false;
        if (sprites2d) drawButtonSprites(ram, overlay);
    }

    // Lens flare adiado (deferFlares): o teste de oclusao le a profundidade ja desenhada (glReadPixels), e cada leitura espera a GPU
    // terminar o que recebeu. Na tela dividida, com o flare dentro do draw de cada vista, a CPU parava a cada vista (redesenho de 3
    // vistas a ~60 ms). Com o adiamento, todas as vistas sao desenhadas antes e os flares de cada uma vem depois, na textura dela, com
    // a lista e o contexto da vista (`fl`, `vc`): uma espera so por redesenho. Dentro do BeginMode3D da vista.
    bool deferFlares = false;
    // Tela cheia ao vivo: o flare do Scene::draw usa a profundidade de um redesenho recente (AsyncDepth; o chamador faz o fetch no
    // comeco do redesenho), copiada do framebuffer `fullDepthFbo` em que o quadro desenha. Nulo (replay, corpus): a leitura direta.
    AsyncDepth *fullDepth = nullptr;
    unsigned fullDepthFbo = 0;
    uint64_t redrawId = 0;   // redesenho em curso (o chamador incrementa antes das vistas): silhuetas de sombra divididas entre elas
    // `depth`: profundidade da vista sem esperar a GPU (AsyncDepth; o chamador fez o fetch no comeco do redesenho); aqui vai o pedido
    // da copia desta vez, do framebuffer `depthFbo` em que a vista desenha
    void drawFlaresFor(const Ram &ram, const Particles::FlareFrame &fl, const ViewCtx &vc, AsyncDepth *depth = nullptr, unsigned depthFbo = 0) {
        if (fl.e.empty()) return;
        if (depth) {
            rlDrawRenderBatchActive();
            depth->issue(rlGetFramebufferWidth(), rlGetFramebufferHeight(), depthFbo, redrawId);
        }
        fx.depthAsync = depth;
        struct ResetDepth { Particles &p; ~ResetDepth() { p.depthAsync = nullptr; } } resetDepth{fx};
        const Particles::FlareFrame keepFl = fx.flares;
        const ViewCtx keepVc = vctx;
        fx.flares = fl;
        vctx = vc;
        {
            ViewScope vs(vctx);
            const bool useShader = beginShader(ram, false);
            rlEnableDepthTest();
            setAlphaRef(kGsAlphaRef);
            fx.drawFlares([&](uint32_t id, uint64_t t0) { return particleTexture(ram, id, t0); });
            rlDrawRenderBatchActive();
            if (useShader) EndShaderMode();
            g_premulShader = false;
        }
        fx.flares = keepFl;
        vctx = keepVc;
    }

    // Icones de botao 2D (g_buttonIconFn) no lugar dos objetos 3D do McButtons: um por _cs, no centro dos triangulos dele, do tamanho
    // do maior lado na tela (eixos da camera), com o fade do screenSetButtonAlpha (+0x4C do objeto, como o fadeByNode). Dentro do modo
    // 3D do quadro, sem profundidade.
    void drawButtonSprites(const Ram &ram, const std::vector<DrawItem> &list) {
        if (list.empty()) return;
        const Matrix mv = rlGetMatrixModelview();
        const float right[3] = {mv.m0, mv.m4, mv.m8}, up[3] = {mv.m1, mv.m5, mv.m9};
        struct Acc { uint32_t cs; uint16_t id; float lo[2], hi[2], c[3]; int n; float alpha; };
        std::vector<Acc> acc;
        for (const DrawItem &it : list) {
            const MeshData *md = mesh(ram, it.node);
            if (!md || md->tris.empty()) continue;
            Acc *a = nullptr;
            for (Acc &x : acc) if (x.cs == it.cs) a = &x;
            if (!a) { acc.push_back({it.cs, it.btnId, {1e30f, 1e30f}, {-1e30f, -1e30f}, {0, 0, 0}, 0, 0.f}); a = &acc.back(); }
            a->alpha = std::max(a->alpha, std::min(1.f, std::max(0.f, ram.f32(it.node + 0x4C))) * it.fade);
            for (const Vtx &v : md->tris) {
                float w[3];
                xform(it.m, v.x, v.y, v.z, w);
                const float pr = w[0] * right[0] + w[1] * right[1] + w[2] * right[2], pu = w[0] * up[0] + w[1] * up[1] + w[2] * up[2];
                a->lo[0] = std::min(a->lo[0], pr); a->hi[0] = std::max(a->hi[0], pr);
                a->lo[1] = std::min(a->lo[1], pu); a->hi[1] = std::max(a->hi[1], pu);
                for (int k = 0; k < 3; ++k) a->c[k] += w[k];
                ++a->n;
            }
        }
        rlDisableDepthTest();
        for (const Acc &a : acc) {
            if (!a.n || a.alpha <= 0.f) continue;
            const float c[3] = {a.c[0] / float(a.n), a.c[1] / float(a.n), a.c[2] / float(a.n)};
            // centro: o meio da caixa nos eixos da camera, na profundidade media
            const float cr = c[0] * right[0] + c[1] * right[1] + c[2] * right[2], cu = c[0] * up[0] + c[1] * up[1] + c[2] * up[2];
            const float dr = (a.lo[0] + a.hi[0]) * 0.5f - cr, du = (a.lo[1] + a.hi[1]) * 0.5f - cu;
            const float mid[3] = {c[0] + right[0] * dr + up[0] * du, c[1] + right[1] * dr + up[1] * du, c[2] + right[2] * dr + up[2] * du};
            const float size = std::max(a.hi[0] - a.lo[0], a.hi[1] - a.lo[1]);
            g_buttonIconFn(a.id, mid, right, up, size, a.alpha);
        }
        rlDrawRenderBatchActive();
        rlEnableDepthTest();
    }

    // Lista de alta prioridade (`hp`), com a camera 3D do quadro ligada: na frente da camera, por cima do que ja esta na tela, sem
    // profundidade (como os icones de botao).
    void drawHp(const Ram &ram, bool wire = false) {
        if (hp.empty() && hpOverlay.empty()) return;
        ViewScope vs(vctx);
        const bool useShader = beginShader(ram, wire);
        rlDisableDepthTest();
        drawList(ram, hp, wire, -1);
        const bool sprites2d = !wire && g_buttonIconFn && (!g_buttonIconsOn || g_buttonIconsOn());
        if (!sprites2d) drawList(ram, hpOverlay, wire, -1, true);
        rlDrawRenderBatchActive();
        rlEnableDepthTest();
        if (useShader) EndShaderMode();
        g_premulShader = false;
        if (sprites2d) drawButtonSprites(ram, hpOverlay);
    }

    // Translucido: textura com alfa parcial ou vertices de alfa baixo. Esses vao no segundo passe (tras para frente, sem escrever profundidade).
    bool translucent(const Ram &ram, const DrawItem &it) {
        const MeshData *md = mesh(ram, it.node);
        if (it.fade < 1.f) return true;   // cross-fade de LOD
        if (md && md->lowAlpha) return true;
        if (md && !md->sprites.empty()) return true;   // ObjSprite: brilho (alphaSort no halo do power-up)
        if (md && md->abe && it.blend != kGsAlphaNormal) return true;   // mistura aditiva/escurecedora precisa do passe translucido
        for (uint32_t sl = 0; sl < 5; ++sl) {
            if (md && md->slotMask && !(md->slotMask & (1u << sl))) continue;
            const uint64_t k = texKey(ram, it, sl);
            if (!k) continue;
            textureId(ram, it, sl);
            const auto f = partial_.find(k);
            if (f != partial_.end() && f->second) return true;
        }
        return false;
    }

    // Faixa da OT de alfa (pktLinkAlphaObjAsm): z = vf30.x do programa 0x0 do VU0, a distancia do centro da esfera do no (+0x10) ao
    // plano 0 da vista (o de perto, fovNorms; em mundo, o mesmo de Particles::inViewVolume), no espaco do no (dividida pela escala).
    // z <= 8 -> faixa 1; senao int(min(z, 1016) / 8) + 1, ate 128. pktInitReverseAlphaOT encadeia da faixa 128 para a 0.
    // OBJECT com o bit 0x8000 das flags (+0xC): o hierTraverseAsm o liga na OT de alfa (pktLinkAlphaObjAsm), que sai depois de
    // todas as cadeias normais do hier; os demais saem na ordem da travessia.
    // Bit 0x4000: so dentro de um cross-fade de LOD (o hierTraverseAsm testa flags & 0xC000; sem o 0x8000, exige o peso do LOD
    // herdado > 0). No luta_hud as colunas da tex 234 saem no meio da OT (programa 1001 de 936-1214).
    static bool inAlphaOT(const Ram &ram, const DrawItem &it) {
        const uint16_t f = ram.u16(it.node + 0xC);
        return (f & 0x8000) || ((f & 0x4000) && it.lodX);
    }

    static int alphaBin(const Ram &ram, const DrawItem &it) {
        const uint32_t w = viewCtxAddr();   // worldCtx[vista]: plano 0 em +0xF0, limiar em +0x120
        const M4 &m = it.m;
        float c[3], s2 = 0.f, z = -ram.f32(w + 0xF0 + 48);
        xform(m, ram.f32(it.node + 0x10), ram.f32(it.node + 0x14), ram.f32(it.node + 0x18), c);
        for (int k = 0; k < 3; ++k) z += ram.f32(w + 0xF0 + 4 * k) * c[k];
        for (int i = 0; i < 3; ++i) s2 = std::max(s2, m.m[i][0] * m.m[i][0] + m.m[i][1] * m.m[i][1] + m.m[i][2] * m.m[i][2]);
        if (s2 > 1e-12f) z /= std::sqrt(s2);
        if (!(z > 8.f)) return 1;
        return int(std::min(z, 1016.f) * 0.125f) + 1;
    }

    // pass: -1 tudo; 0 so opacos/recortados; 1 so a OT de alfa (inAlphaOT; alphaBin, empate LIFO); 2 tudo fora da OT, na ordem da
    // travessia (lotes so entre itens seguidos com a mesma textura e o mesmo estado)
    // fadeByNode: multiplica o alfa pelo float em +0x4C do objeto (fade dos botoes: screenSetButtonAlpha grava `targetAlpha` ali)
    void drawList(const Ram &ram, const std::vector<DrawItem> &list, bool wire, int pass = -1, bool fadeByNode = false) {
        rlDisableBackfaceCulling();
        {   // diagnostico: WOTM_CMP=1 compara, por triangulo, as arestas de dois itens skinned com a mesma malha (ex.: jogador x inimigo)
            static const bool cmp = std::getenv("WOTM_CMP") != nullptr;
            static bool done = false;
            if (cmp && !done) {
                const DrawItem *A = nullptr, *B = nullptr;
                for (const DrawItem &it : list) {
                    const MeshData *md = mesh(ram, it.node);
                    if (!md || !md->skinned || md->tris.size() != 4428 || !it.animPkt) continue;
                    if (!A) A = &it; else if (!B && it.animPkt != A->animPkt) B = &it;
                }
                if (A && B) {
                    done = true;
                    const MeshData *md = mesh(ram, A->node);
                    const uint32_t pa = palette(ram, *A), pb = palette(ram, *B);
                    int slotBad[32] = {0}, slotAll[32] = {0}, nbad = 0, ntri = 0;
                    for (size_t k = 0; k + 2 < md->tris.size(); k += 3) {
                        float qa[3][3], qb[3][3];
                        for (int c = 0; c < 3; ++c) { skinVertex(ram, pa, M4{}, md->tris[k + c], qa[c]); skinVertex(ram, pb, M4{}, md->tris[k + c], qb[c]); }
                        auto mx = [](float q[3][3]) { float m = 0; for (int c = 0; c < 3; ++c) { const float *a = q[c], *b = q[(c + 1) % 3]; m = std::max(m, std::sqrt((a[0]-b[0])*(a[0]-b[0]) + (a[1]-b[1])*(a[1]-b[1]) + (a[2]-b[2])*(a[2]-b[2]))); } return m; };
                        const float ea = mx(qa), eb = mx(qb);
                        ++ntri;
                        auto area = [](float q[3][3]) { const float u[3] = {q[1][0]-q[0][0], q[1][1]-q[0][1], q[1][2]-q[0][2]}, w[3] = {q[2][0]-q[0][0], q[2][1]-q[0][1], q[2][2]-q[0][2]};
                            const float cx = u[1]*w[2]-u[2]*w[1], cy = u[2]*w[0]-u[0]*w[2], cz = u[0]*w[1]-u[1]*w[0]; return 0.5f * std::sqrt(cx*cx + cy*cy + cz*cz); };
                        const float aa = area(qa), ab = area(qb);
                        const bool bad = ab < 0.15f * aa && aa > 4.f;   // triangulo colapsado no B (area < 15%% da do A)
                        if (bad) ++nbad;
                        for (int c = 0; c < 3; ++c) {
                            const Vtx &v = md->tris[k + c];
                            const int sl[3] = {(v.cx & 0x7F) / 4, (v.cy & 0x7F) / 4, (v.cz & 0x7F) / 4};
                            for (int z = 0; z < 3; ++z) if (sl[z] < 32) { ++slotAll[sl[z]]; if (bad) ++slotBad[sl[z]]; }
                        }
                    }
                    std::fprintf(stderr, "[cmp] tris %d, colapsados no B (area < 15%% da do A): %d\n", ntri, nbad);
                    for (int z = 0; z < 31; ++z) std::fprintf(stderr, "[cmp] slot %2d: usos %4d, em rasgados %4d (%.0f%%)\n", z, slotAll[z], slotBad[z], slotAll[z] ? 100.0 * slotBad[z] / slotAll[z] : 0.0);
                }
            }
        }
        struct Ent { uint64_t key; uint32_t item; uint32_t slot; };
        constexpr uint32_t kSlotDouble = 0x80000000u;   // Ent::slot: 2o passe de GsBlendFactors::doubled (destino * 2)
        constexpr uint32_t kSlotOver1 = 0x40000000u;    // Ent::slot: 2o e 3o passes da mistura comum com As > 1 (gsBlendCommon)
        constexpr uint32_t kSlotOver2 = 0x20000000u;
        constexpr uint32_t kSlotPass = kSlotDouble | kSlotOver1 | kSlotOver2;
        // Mistura comum em que o As pode passar de 1: maior alfa de vertice (com o fade) vezes o maior da textura acima de 128 * 128.
        // Sem textura, o branco 0x80; quadro anterior e filme vao sem o modo GS (As ate 1).
        // O ceu fica de fora: e o primeiro desenho do quadro, e a referencia nao mostra o ganho no domo (o destino sob ele e o quadro
        // anterior, o mesmo ceu, e Cs As - Cd (As - 1) converge para Cs, o mesmo que As limitado a 1). No nativo o destino e a limpeza
        // preta, e os passes deixavam o domo As vezes mais claro (x1,027 no corpus).
        static const bool idcOpaqueOver = std::getenv("WOTM_IDCOLOR") != nullptr;
        static const bool noOver = std::getenv("WOTM_NOOVER") != nullptr;   // diagnostico: As limitado a 1 na mistura comum (como antes)
        const bool skyList = &list == &sky;
        auto over = [&](const DrawItem &it, const MeshData *m, uint32_t sl) {
            if (wire || noOver || skyList || idcOpaqueOver || !m || !m->abe || !gsBlendCommon(it.blend)) return false;
            const float av = float(m->maxA) * it.fade * std::max(0.f, it.color[3]);
            if (av * 255.f <= 16384.f) return false;
            const uint32_t ts = texRemap(sl);
            const uint64_t key = texKey(ram, it, ts);
            if (key == kScreenKey || (movieTexture && objectTexId(ram, it.node, ts) == 999)) return false;
            uint32_t at = 0x80;
            if (key && textureId(ram, it.node, ts)) { const auto f = texAmax_.find(key); if (f != texAmax_.end()) at = f->second; }
            return av * float(at) > 16384.f;
        };
        std::vector<Ent> order;   // (chave de textura, indice do item, slot de textura)
        order.reserve(list.size());
        const bool far2near = pass == 1 && !wire;   // OT de alfa: faixas e LIFO
        const bool trav = pass == 2 && !wire;       // fora da OT: ordem da travessia, como as cadeias normais do hier
        for (uint32_t i = 0; i < list.size(); ++i) {
            if (pass >= 0 && !wire) {
                const bool sel = pass == 0 ? !translucent(ram, list[i]) : pass == 1 ? inAlphaOT(ram, list[i]) : !inAlphaOT(ram, list[i]);
                if (!sel) continue;
            }
            {   // diagnostico: WOTM_ONLYTEX=<id[,id..]> desenha so esses texId (bisseccao de defeitos)
                static const char *only = std::getenv("WOTM_ONLYTEX");
                if (only) {   // com '-' na frente: desenha tudo menos esses
                    const bool skip = *only == '-';
                    const std::string s = std::string(",") + (skip ? only + 1 : only) + ",";
                    if ((s.find("," + std::to_string(ram.u16(list[i].node + 0x50)) + ",") == std::string::npos) != skip) continue;
                }
            }
            {   // diagnostico: WOTM_ONLYNODE=<hex[,hex..]> desenha so esses nos (isola um personagem do resto da cena)
                static const char *onlyN = std::getenv("WOTM_ONLYNODE");
                if (onlyN) {
                    char b[16]; std::snprintf(b, sizeof b, ",%x,", list[i].node);
                    if ((std::string(",") + onlyN + ",").find(b) == std::string::npos) continue;
                }
                static const char *skipN = std::getenv("WOTM_SKIPNODE");   // =<hex[,hex..]>: desenha tudo menos esses nos
                if (skipN) {
                    char b[16]; std::snprintf(b, sizeof b, ",%x,", list[i].node);
                    if ((std::string(",") + skipN + ",").find(b) != std::string::npos) continue;
                }
            }
            const MeshData *mdi = mesh(ram, list[i].node);
            {   // diagnostico: WOTM_LISTITEMS=1 lista os itens de cada passe (no, texId, zBufferFudge, translucido, posicao, triangulos)
                static const bool li = std::getenv("WOTM_LISTITEMS") != nullptr;
                if (li && !wire)
                    std::fprintf(stderr, "[item] pass %d no %x tex %u fz %.4f tris %zu pos %.1f %.1f %.1f blend %02x abe %d zmsk %d fade %.2f\n", pass, list[i].node,
                                 unsigned(ram.u16(list[i].node + 0x50)), ram.f32(list[i].node + 0x28), mdi ? mdi->tris.size() / 3 : size_t(0), list[i].m.m[3][0],
                                 list[i].m.m[3][1], list[i].m.m[3][2], list[i].blend, mdi ? int(mdi->abe) : -1, int(list[i].zmsk), list[i].fade);
            }
            const uint32_t smask = (mdi && mdi->slotMask) ? mdi->slotMask : 1u;
            for (uint32_t sl = 0; sl < 32; ++sl) {   // slots 0-4 e kSlotNoTex
            if (!(smask & (1u << sl))) continue;
            { static const int onlySl = std::getenv("WOTM_ONLYSLOT") ? std::atoi(std::getenv("WOTM_ONLYSLOT")) : -1; if (onlySl >= 0 && int(sl) != onlySl) continue; }   // diagnostico: so este slot de textura
            uint64_t key = wire ? 0 : texKey(ram, list[i], texRemap(sl));
            if (far2near) key = uint64_t(2 + 128 - alphaBin(ram, list[i]));   // faixa mais longe primeiro (nunca 0 nem 1)
            order.push_back({key, i, sl});
            if (!wire && mdi && mdi->abe && gsBlendFactors(list[i].blend).doubled) order.push_back({key, i, sl | kSlotDouble});   // 2o passe, logo depois
            if (over(list[i], mdi, sl)) { order.push_back({key, i, sl | kSlotOver1}); order.push_back({key, i, sl | kSlotOver2}); }
            }
        }
        // Passe translucido na ordem do retail: os objetos fora da OT de alfa saem nas cadeias normais, na ordem da travessia; depois
        // a OT (inAlphaOT), das faixas mais longe para as mais perto e, dentro da faixa, o ultimo da travessia primeiro
        // (pktLinkAlphaObjAsm insere no comeco da lista). Os slots de um item e o 2o passe ficam juntos.
        if (!trav)   // na travessia a ordem ja e a da lista (itens crescentes, slots do item juntos)
            std::stable_sort(order.begin(), order.end(), [far2near](const Ent &a, const Ent &b) {
                if (a.key != b.key) return a.key < b.key;
                return far2near && a.item > b.item;   // OT: o ultimo da travessia primeiro
            });
        {   // diagnostico: WOTM_ALPHAORDER=1 lista o passe translucido na ordem em que sai (no, texId, faixa da OT de alfa, ALPHA_1)
            static const bool ao = std::getenv("WOTM_ALPHAORDER") != nullptr;
            if (ao && far2near)
                for (const Ent &e : order)
                    std::fprintf(stderr, "[alpha] no %x tex %u faixa %d item %u slot %x blend %02x\n", list[e.item].node, unsigned(ram.u16(list[e.item].node + 0x50)),
                                 130 - int(e.key), e.item, e.slot, list[e.item].blend);
        }

        SpriteView sprView;   // eixos e focos para os ObjSprite (montado no primeiro item que tiver)
        EnvView envV;         // camera do UV do mapa de ambiente (idem, no primeiro item com DrawItem::env)
        const unsigned white = gsWhiteTexture();   // secoes sem textura (PRIM.TME 0): alfa 0x80 = 1,0 do GS
        uint64_t cur = ~0ull;
        bool curClamp = false;     // lote aberto com DrawItem::env 1 (textura em CLAMP)
        unsigned clampedId = 0;    // textura posta em CLAMP no lote aberto; volta a REPEAT depois do flush
        bool open = false;
        uint8_t curBlend = kGsAlphaNormal;   // DrawItem::blend ativo no GL (o lote anterior ja foi descarregado quando isto muda)
        bool curPremul = false;              // gsBlendPremul(curBlend): cor pre-multiplicada no shader (shaderNormal com pm)
        constexpr uint8_t kBlendDouble = 0xFF;   // curBlend no 2o passe do doubled (A = 3 nao existe: nunca e um ALPHA)
        uint32_t curPass = 0;  // passe do lote aberto (0 ou kSlotDouble/kSlotOver1/kSlotOver2)
        int curMode = 0;       // modo do shaderNormal no lote aberto (1 e 2: passes do doubled; 3 a 5: As > 1 na mistura comum)
        uint8_t curItBlend = 0;
        int16_t curItRef = 0;   // estado do item do lote aberto (passe da travessia: lote so com o mesmo estado)
        bool curItZ = false, curItAbe = false;
        // Vertices pelo VtxStream (o mesmo que o rlgl mandaria, sem as quatro chamadas por vertice); o arame e WOTM_NOVTXSTREAM=1
        // usam o modo imediato do rlgl. No stream, um lote novo que so troca a textura vira outro comando da mesma subida; a fila e
        // descarregada so antes de uma mudanca de estado do GL (mistura, teste de alfa, mascara de Z, CLAMP), que vale na descarga.
        static const bool noStream = std::getenv("WOTM_NOVTXSTREAM") != nullptr;
        const bool stream = !wire && !noStream && vs_.ok();
        // no stream, a conta dos vertices de cada pedaco (transformacao, ossos, luz) vai para as threads auxiliares (JobPool): o
        // estado e os comandos saem aqui, na ordem, e os vertices entram no lugar deles na descarga
        const bool deferVerts = stream && JobPool::get().active();
        if (resReset_ && stream && vs_.empty()) {   // arena cheia: tudo de novo (orfana o buffer; os desenhos ja enviados seguem com o antigo)
            vs_.arenaReset();
            resChunks_.clear();
            resReset_ = false;
        }
        auto flushStream = [&] {
            if (!stream || vs_.empty()) return;
            ProfScope ps(DrawProf::Flush);
            vs_.flush();
            if (drawProfOn()) { ++drawProf().batches; drawProf().draws += vs_.draws(); }
            vs_.resetDraws();
        };
        // mudanca de estado do GL: no stream entra na fila do VtxStream (na ordem dos vertices, sem descarregar); senao agora
        auto glState = [&](auto &&fn) { if (stream) vs_.state(fn); else fn(); };
        auto setBlend = [&](uint8_t b) {
            if (b == curBlend) return;
            curBlend = b;
            curPremul = gsBlendPremul(b);
            glState([b] { gsBlend(b); });
        };
        int16_t curRef = kGsAlphaRef;   // DrawItem::aref no uniform do shader (idem)
        auto setRef = [&](int16_t r) {
            if (r == curRef) return;
            curRef = r;
            glState([this, r] { setAlphaRef(r); });
        };
        int curZm = -1;   // mascara de Z posta aqui (-1: a do chamador)
        auto setZm = [&](bool on) {
            if (int(on) == curZm) return;
            curZm = int(on);
            glState([on] { if (on) rlEnableDepthMask(); else rlDisableDepthMask(); });
        };
        auto vColor = [&](uint8_t r, uint8_t g, uint8_t b, uint8_t a) { if (stream) vs_.color(r, g, b, a); else rlColor4ub(r, g, b, a); };
        auto vTex = [&](float u, float v) { if (stream) vs_.texcoord(u, v); else rlTexCoord2f(u, v); };
        auto vNormal = [&](float burn, bool fge, bool gs, bool pm, int mode) {
            if (!stream) { shaderNormal(burn, fge, gs, pm, mode); return; }
            float n[3];
            shaderNormalVals(burn, fge, gs, pm, mode, n);
            vs_.normal(n[0], n[1], n[2]);
        };
        auto vVertex = [&](float x, float y, float z) { if (stream) vs_.vertex(x, y, z); else rlVertex3f(x, y, z); };
        for (const auto &o : order) {
            const DrawItem &it = list[o.item];
            const MeshData *md = mesh(ram, it.node);
            std::shared_ptr<const MeshData> mdKeep = lastMesh_;   // o pedaco adiado segura a malha (o cache pode troca-la antes da descarga)
            {   // diagnostico: WOTM_LISTSKIN=1 lista os itens skinned desenhados (no, LOD, animPkt, posicao do objeto)
                static const bool ls = std::getenv("WOTM_LISTSKIN") != nullptr;
                if (ls && md && md->skinned && !wire)
                    std::fprintf(stderr, "[skin] node %x lod %d animPkt %x pos %.1f %.1f %.1f tris %zu slot %u\n", it.node, it.lodIdx, it.animPkt, it.m.m[3][0], it.m.m[3][1], it.m.m[3][2], md->tris.size() / 3, o.slot);
                if (ls && md && md->skinned && !wire) {
                    uint64_t h = 1469598103934665603ull;   // FNV-1a da geometria decodificada (sem cor): compara meshes de monstros diferentes
                    for (const Vtx &v : md->tris) {
                        const float f[6] = {v.x, v.y, v.z, v.u, v.v, v.w};
                        const uint8_t *q = reinterpret_cast<const uint8_t *>(f);
                        for (int b = 0; b < 24; ++b) h = (h ^ q[b]) * 1099511628211ull;
                        const uint8_t c[4] = {v.cx, v.cy, v.cz, v.slot};
                        for (int b = 0; b < 4; ++b) h = (h ^ c[b]) * 1099511628211ull;
                    }
                    int perSlot[8] = {0};
                    for (const Vtx &v : md->tris) ++perSlot[v.slot & 7];
                    std::fprintf(stderr, "   slotMask %x verts/slot %d %d %d %d %d texIds %u %u %u\n", md->slotMask, perSlot[0], perSlot[1], perSlot[2], perSlot[3], perSlot[4],
                                 unsigned(ram.u16(it.node + 0x50)), unsigned(ram.u16(it.node + 0x52)), unsigned(ram.u16(it.node + 0x54)));
                    std::fprintf(stderr, "   geomhash %016llx nverts %zu\n", (unsigned long long)h, md->tris.size());
                    // arestas apos o skinning (unidades do mundo): malha rasgada tem cauda longa; compara modelos de peso e paletas
                    const uint32_t palD = palette(ram, it);
                    std::vector<float> el;
                    for (size_t k = 0; palD && k + 2 < md->tris.size(); k += 3) {
                        float q[3][3];
                        for (int c = 0; c < 3; ++c) skinVertex(ram, palD, it.m, md->tris[k + c], q[c]);
                        float mx = 0;
                        for (int c = 0; c < 3; ++c) {
                            const float *a = q[c], *b = q[(c + 1) % 3];
                            mx = std::max(mx, std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2])));
                        }
                        el.push_back(mx);
                    }
                    if (!el.empty()) {
                        std::sort(el.begin(), el.end());
                        std::fprintf(stderr, "   maxedge med %.2f p90 %.2f p99 %.2f max %.2f\n", el[el.size() / 2], el[el.size() * 9 / 10], el[el.size() * 99 / 100], el.back());
                    }
                }
                if (ls && md && md->skinned && !wire) { for (int r = 0; r < 3; ++r) std::fprintf(stderr, "   m[%d] %.3f %.3f %.3f %.3f\n", r, it.m.m[r][0], it.m.m[r][1], it.m.m[r][2], it.m.m[r][3]); }
            }
            if (!md || !md->valid || (md->tris.empty() && md->sprites.empty())) continue;
            const uint32_t passK = o.slot & kSlotPass;
            const bool dbl = passK == kSlotDouble;
            const uint32_t slot = o.slot & ~kSlotPass;
            const bool multi = md->slotMask & (md->slotMask - 1);   // mais de um slot usado
            const bool envClamp = it.env == 1 && !wire;   // mapa de ambiente comum: CLAMP_1 = 5 (as texturas de objeto repetem)
            // na travessia o lote continua so com a mesma textura e o mesmo estado do GS (mistura, teste de alfa, ZMSK)
            const bool travState = trav && open && (it.blend != curItBlend || it.aref != curItRef || it.zmsk != curItZ || md->abe != curItAbe);
            if (o.key != cur || !open || far2near || travState || envClamp != curClamp || passK != curPass) {
                curItBlend = it.blend; curItRef = it.aref; curItZ = it.zmsk; curItAbe = md->abe;
                if (open && !stream) {   // um lote por textura (evita o alinhamento automatico do rlgl)
                    ProfScope ps(DrawProf::Flush);
                    rlEnd();
                    rlDrawRenderBatchActive();
                    if (drawProfOn()) ++drawProf().batches;
                }
                if (clampedId) {
                    const unsigned t = clampedId;
                    glState([t] { rlTextureParameters(t, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_REPEAT); rlTextureParameters(t, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_REPEAT); });
                    clampedId = 0;
                }
                if (!wire && dbl) { glState([] { gsBlendDouble(); }); curBlend = kBlendDouble; curPremul = false; }
                else if (!wire && passK == kSlotOver1) { glState([] { gsBlendOver1(); }); curBlend = kBlendDouble; curPremul = false; }
                else if (!wire && passK == kSlotOver2) { glState([] { gsBlendOver2(); }); curBlend = kBlendDouble; curPremul = false; }
                // sem ABE o GS grava a cor da fonte sem misturar (o alfa, com o fade do LOD, so vale para o teste): o predio 133 do
                // luta_hud em cross-fade saia meio transparente e deixava ver o predio de tras
                else if (!wire) {
                    static const bool idcOpaque = std::getenv("WOTM_IDCOLOR") && std::atoi(std::getenv("WOTM_IDCOLOR")) == 2;
                    setBlend(md->abe && !idcOpaque ? it.blend : kGsNoAbe);
                }
                curPass = passK;
                if (!wire) setRef(it.aref);
                // passe translucido: grava profundidade como o GS, a menos que o GROUP ligue o ZMSK (o chamador volta a mascara ao fim)
                if (far2near || trav) setZm(!it.zmsk);
                cur = o.key;
                curClamp = envClamp;
                const unsigned id = wire ? 0 : textureId(ram, it, texRemap(slot));
                if (envClamp && id && id != screenId_) {
                    glState([id] { rlTextureParameters(id, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP); rlTextureParameters(id, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP); });
                    clampedId = id;
                }
                static const bool idc = std::getenv("WOTM_IDCOLOR") != nullptr;   // diagnostico: cor = texId do objeto (leia o pixel para achar o id)
                if (stream) vs_.begin(RL_TRIANGLES, (id && !idc) ? id : white);
                else {
                    rlBegin(wire ? RL_LINES : RL_TRIANGLES);   // rlBegin com outro modo reseta a textura do lote: setar depois
                    rlSetTexture((id && !idc) ? id : white);
                }
                open = true;
            }
            // modo por item (vai no vertice): um item com As > 1 pode cair num lote aberto por outro sem
            curMode = dbl ? 2 : passK == kSlotOver1 ? 4 : passK == kSlotOver2 ? 5
                    : (!wire && md->abe && gsBlendFactors(it.blend).doubled) ? 1 : over(it, md, slot) ? 3 : 0;
            static const bool idcMode = std::getenv("WOTM_IDCOLOR") != nullptr;   // uma vez: getenv por vertice e lento no Windows
            static const bool idcNode = idcMode && std::atoi(std::getenv("WOTM_IDCOLOR")) == 2;   // =2: no >> 4 em RGB, sem mistura nem neblina
            const unsigned tid = (wire || !cur) ? 0 : textureId(ram, it, texRemap(slot));
            // alfa como o GS (textura com alfa cru, ver textureId), menos no quadro anterior (render texture) e no filme
            const bool gsA = !(tid && (tid == screenId_ || (movieTexture && objectTexId(ram, it.node, texRemap(slot)) == 999)));
            const auto &t = md->tris;
            if (drawProfOn()) drawProf().verts += long(t.size());
            {   // diagnostico: WOTM_TEXINFO=<id> imprime (ate WOTM_TEXINFO_N vezes, padrao 6) os objetos com esse texId: mistura, cor, fudge, textura e vertices
                static const int ti = std::getenv("WOTM_TEXINFO") ? std::atoi(std::getenv("WOTM_TEXINFO")) : -1;
                static int printed = 0;
                static const int tiMax = std::getenv("WOTM_TEXINFO_N") ? std::atoi(std::getenv("WOTM_TEXINFO_N")) : 6;
                if (printed < tiMax && ((ti >= 0 && int(objectTexId(ram, it.node, slot)) == ti) || (ti == -2 && !md->sprites.empty()) || (ti == -3 && it.blend != kGsAlphaNormal && !t.empty()) || (ti == -4 && it.aref == kGsAlphaNoTest) || (ti == -5 && it.color[1] > 1.05f && it.color[0] < 0.95f))) {   // -2: todos os OBJECT sprite; -3: malhas fora da mistura comum; -4: sem teste de alfa; -5: cor esverdeada (pick-up destacado)
                    ++printed;
                    std::fprintf(stderr, "[texinfo] tex %u node %x blend %d abe %d color %.3f %.3f %.3f %.3f fade %.3f gsA %d key %llx tris %zu sprites %zu tex0 %llx aref %d\n", objectTexId(ram, it.node, slot), it.node, it.blend,
                                 int(md->abe), it.color[0], it.color[1], it.color[2], it.color[3], it.fade, int(gsA), (unsigned long long)o.key, t.size(),
                                 md->sprites.size(), (unsigned long long)objectTex0(ram, it.node, slot), int(it.aref));
                    std::fprintf(stderr, "[texinfo]   fudge %.4f burn %.3f flags %04x pos %.1f %.1f %.1f\n", ram.f32(it.node + 0x28), ram.f32(it.node + 0x2C),
                                 ram.u16(it.node + 0xC), it.m.m[3][0], it.m.m[3][1], it.m.m[3][2]);
                    std::vector<uint32_t> tp;
                    uint32_t tw = 0, th = 0;
                    if (decodeTex(ram, it.node, tp, tw, th, slot, true)) {
                        double s[4] = {0, 0, 0, 0};
                        uint32_t amin = 255, amax = 0;
                        for (uint32_t c : tp) { for (int k = 0; k < 4; ++k) s[k] += (c >> (8 * k)) & 255; amin = std::min(amin, c >> 24); amax = std::max(amax, c >> 24); }
                        std::fprintf(stderr, "[texinfo]   textura %ux%u media %.0f %.0f %.0f %.0f alfa %u..%u texKey %llx\n", tw, th, s[0] / tp.size(), s[1] / tp.size(),
                                     s[2] / tp.size(), s[3] / tp.size(), amin, amax, (unsigned long long)texKey(ram, it.node, slot));
                        if (const char *dir = std::getenv("WOTM_TEXINFO_PNG")) {   // grava <dir>/texinfo_<texKey>.png (alfa cru do GS, 0x80 = 1,0)
                            char name[1024];
                            std::snprintf(name, sizeof name, "%s/texinfo_%llx.png", dir, (unsigned long long)texKey(ram, it.node, slot));
                            Image im{tp.data(), int(tw), int(th), 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
                            ExportImage(im, name);
                        }
                        for (uint32_t y = 0; y < th; y += th / 8) {
                            std::fprintf(stderr, "[texinfo]   ");
                            for (uint32_t x = 0; x < tw; x += tw / 8) std::fprintf(stderr, "%08x ", tp[y * tw + x]);
                            std::fprintf(stderr, "\n");
                        }
                    }
                    for (size_t q = 0; q < t.size() && q < 6; ++q)
                        std::fprintf(stderr, "[texinfo]   v %.1f %.1f %.1f uv %.3f %.3f rgba %u %u %u %u fge %d\n", t[q].x, t[q].y, t[q].z, t[q].u, t[q].v,
                                     t[q].r, t[q].g, t[q].b, t[q].a, int(t[q].fge));
                    for (size_t q = 0; q < md->sprites.size() && q < 6; ++q) {   // OBJECT sprite (objType 2)
                        const ObjSprite &s = md->sprites[q];
                        std::fprintf(stderr, "[texinfo]   sprite uv %.3f %.3f - %.3f %.3f rgba %u %u %u %u fge %d\n", s.u0, s.v0, s.u1, s.v1,
                                     unsigned(s.r), unsigned(s.g), unsigned(s.b), unsigned(s.a), int(s.fge));
                    }
                }
            }
            static const bool forceOpaqueEnv = std::getenv("WOTM_OPAQUE") != nullptr;   // diagnostico: alfa de vertice ignorado em skinned
            const bool forceOpaque = forceOpaqueEnv && md->skinned;
            const float fade = (fadeByNode ? std::min(1.f, std::max(0.f, ram.f32(it.node + 0x4C))) : 1.f) * it.fade * std::max(0.f, it.color[3]);
            const ObjLight ol = objectLight(ram, it);
            const float fogBurn = ram.f32(it.node + 0x2C);   // fogBurnThru (somado ao F da neblina)
            static const bool noVcol = std::getenv("WOTM_NOVCOL") != nullptr;
            static const bool noSkin = std::getenv("WOTM_NOSKIN") != nullptr;   // diagnostico: pose de referencia, sem skinning
            static const uint32_t palOverride = std::getenv("WOTM_PALBLOCK") ? uint32_t(std::strtoul(std::getenv("WOTM_PALBLOCK"), nullptr, 16)) : 0;   // diagnostico
            const uint32_t pal = (md->skinned && !noSkin) ? (palOverride ? palOverride : palette(ram, it)) : 0;
            std::vector<M4> pmTmp;   // paleta lida da RAM (item sem copia: viewer, ou WOTM_PALBLOCK)
            const M4 *pm = nullptr;
            if (md->skinned && !noSkin) {
                if (!palOverride && it.pal && it.pal->size() >= 31) pm = it.pal->data();
                else if (pal) { for (uint32_t kk = 0; kk < 32; ++kk) pmTmp.push_back(readM4(ram, pal + 0x10 + 0x40 * kk)); pm = pmTmp.data(); }
            }
            M4 skinM = it.m;
            {   // diagnostico: WOTM_SKINM1=<animPkt hex> usa a matriz do objeto desse item para todos os skinned (isola matriz x paleta)
                static const uint32_t refPkt = std::getenv("WOTM_SKINM1") ? uint32_t(std::strtoul(std::getenv("WOTM_SKINM1"), nullptr, 16)) : 0;
                if (refPkt && md->skinned)
                    for (const DrawItem &r : list) if (r.animPkt == refPkt) { skinM = r.m; break; }
            }   // diagnostico: WOTM_SKINPOS=x,y,z poe todos os itens skinned nessa posicao (testa dependencia de posicao)
            { static float sp[3]; static const int have = std::getenv("WOTM_SKINPOS") ? std::sscanf(std::getenv("WOTM_SKINPOS"), "%f,%f,%f", sp, sp + 1, sp + 2) : 0; if (have == 3 && md->skinned) for (int c = 0; c < 3; ++c) skinM.m[3][c] = sp[c]; }
            // zBufferFudge (objeto +0x28): multiplicador da profundidade (1,0 = nada). Escalar o vertice ao longo do raio a partir do
            // olho muda so a profundidade, nao a posicao na tela: resolve o z-fighting de camadas coplanares.
            const float fz = ram.f32(it.node + 0x28);
            const bool fudge = !wire && eyeOk_ && fz > 0.5f && fz < 2.f && fz != 1.f;
            const float fzScale = 1.f / fz;   // zBufferFudge > 1 = mais perto (no raio do olho)
            float rowLen[3];   // linhas da matriz da luz (vertexRgb), uma vez por item
            rowLengths(pm ? skinM : it.m, rowLen);
            {   // diagnostico: WOTM_SKINBOX=1 imprime a caixa (mundo) dos vertices skinned de cada no, por osso x
                static const bool sb = std::getenv("WOTM_SKINBOX") != nullptr;
                if (sb && pm && md->skinned && !wire) {
                    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f}, rlo[3] = {1e30f, 1e30f, 1e30f}, rhi[3] = {-1e30f, -1e30f, -1e30f};
                    for (const Vtx &v : t) {
                        float p[3];
                        skinVertexPM(pm, skinM, v, p);
                        const float r[3] = {v.x, v.y, v.z};
                        for (int c = 0; c < 3; ++c) { lo[c] = std::min(lo[c], p[c]); hi[c] = std::max(hi[c], p[c]); rlo[c] = std::min(rlo[c], r[c]); rhi[c] = std::max(rhi[c], r[c]); }
                    }
                    for (int s = 0; s < 31; ++s) {   // maior |elemento| de cada matriz da paleta (detecta lixo/NaN)
                        float mx = 0.f; bool bad = false;
                        for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) { const float e = pm[s].m[r][c]; if (!(e == e) || std::fabs(e) > 1e6f) bad = true; mx = std::max(mx, std::fabs(e)); }
                        if (bad || mx > 500.f) std::fprintf(stderr, "[skinbox]   osso %d: max|e| %.3g%s\n", s, double(mx), bad ? " (LIXO)" : "");
                    }
                    std::fprintf(stderr, "[skinbox] node %x mundo x[%.1f %.1f] y[%.1f %.1f] z[%.1f %.1f] | cru x[%.1f %.1f] y[%.1f %.1f] z[%.1f %.1f]\n", it.node, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], rlo[0], rhi[0], rlo[1], rhi[1], rlo[2], rhi[2]);
                }
            }
            // camera do mapa de ambiente e eixos dos sprites: uma vez por lista, aqui (lidos da RAM), antes dos vertices
            if (it.env && tid && !envV.ok && eyeOk_) {
                float W[4][4];
                for (int r = 0; r < 4; ++r)
                    for (int c = 0; c < 4; ++c) W[r][c] = ram.f32(viewW2S() + 16 * r + 4 * c);
                envV = envView(ram, W, eye_);
            }
            if (!md->sprites.empty() && !sprView.ok) sprView = SpriteView::make(ram, eye_);
            EmitIn in{&it, md, slot, multi, pm, skinM, fudge, fzScale, {rowLen[0], rowLen[1], rowLen[2]}, ol, fade, fogBurn, tid, gsA,
                      forceOpaque, curPremul, curMode, wire};
            // Geometria parada (sem ossos, mapa de ambiente, zBufferFudge nem sprites; a mesma chave de um redesenho
            // anterior): os vertices ficam na arena da GPU e as vistas e quadros seguintes so desenham a faixa (residentDraw)
            if (stream && !pm && !md->skinned && !fudge && !it.env && md->sprites.empty() && gsA && tid != screenId_ &&
                mdKeep.get() == md && residentDraw(in, mdKeep))
                continue;
            if (deferVerts && mdKeep.get() == md) {   // os vertices deste pedaco saem nas threads auxiliares (abaixo), no lugar dele no VtxStream
                if (nJobs_ == jobs_.size()) jobs_.emplace_back();
                EmitJob &j = jobs_[nJobs_++];
                j.keep = mdKeep;
                if (!pmTmp.empty()) { j.pmOwn.swap(pmTmp); in.pm = j.pmOwn.data(); }
                j.in = in;
                j.buf = vs_.defer();
            } else {
                struct Direct {
                    decltype(vColor) &c; decltype(vTex) &t; decltype(vNormal) &n; decltype(vVertex) &v;
                    void color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) { c(r, g, b, a); }
                    void tex(float u, float w) { t(u, w); }
                    void shaderN(float burn, bool fge, bool gs, bool pm, int mode) { n(burn, fge, gs, pm, mode); }
                    void vertex(float x, float y, float z) { v(x, y, z); if (drawProfOn()) ++drawProf().emitted; }
                    void tri(bool) {}
                } sink{vColor, vTex, vNormal, vVertex};
                emitVerts(ram, in, envV, sprView, sink);
            }
        }
        if (nJobs_) {   // os pedacos adiados, em paralelo; cada um no seu buffer
            ProfScope ps(DrawProf::Jobs);
            JobPool::get().parallelFor(int(nJobs_), [&](int i) {
                EmitJob &j = jobs_[size_t(i)];
                BufSink sink{vs_.deferBuf(j.buf)};
                emitVerts(ram, j.in, envV, sprView, sink);
            });
            if (drawProfOn()) for (size_t i = 0; i < nJobs_; ++i) drawProf().emitted += long(vs_.deferBuf(jobs_[i].buf).size());
            for (size_t i = 0; i < nJobs_; ++i) { jobs_[i].keep.reset(); jobs_[i].pmOwn.clear(); }
            nJobs_ = 0;
        }
        if (open) {   // descarrega o ultimo lote ANTES de religar o culling (o estado GL vale no flush)
            if (stream) flushStream();
            else { rlEnd(); rlDrawRenderBatchActive(); }
        }
        if (clampedId) { rlTextureParameters(clampedId, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_REPEAT); rlTextureParameters(clampedId, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_REPEAT); }
        rlNormal3f(0.f, 0.f, 0.f);   // o que vier depois (sombras, particulas) sai sem neblina
        setBlend(kGsAlphaNormal);
        setRef(kGsAlphaRef);
        rlSetTexture(0);
        if (trav) rlEnableDepthMask();   // a travessia muda a mascara item a item; quem vem depois espera gravar
        rlEnableBackfaceCulling();
    }

    // Texto do jogo (menu de pausa, HUD): o jogo monta o quadro em `fontPacket` (um pacote GIF para o GS: cabecalho A+D com o TEX0 do atlas,
    // depois corridas de sprites [UV, XYZ3, UV, XYZ2] x n separadas por [PRIM, RGBAQ] que trocam a cor) e o despacha em fontDmaFontData, que
    // zera o contador (D_006F8D30 volta a D_006F8D44 = tamanho do cabecalho) e deixa o pacote na RAM. Contador != cabecalho = o jogo esta
    // montando o quadro: mantem o texto anterior. Num quadro sem texto o terminador (EOP) cai logo depois do cabecalho e o texto some.
    void updateText(const Ram &ram) {
        if (ram.u32(0x006F8D30) != ram.u32(0x006F8D44)) return;
        std::vector<TextQuad> q;
        uint32_t p = addr::fontPacket;
        uint64_t tex0 = 0;
        uint8_t col[4] = {0x80, 0x80, 0x80, 0x80};
        auto u64 = [&](uint32_t a) { return uint64_t(ram.u32(a)) | (uint64_t(ram.u32(a + 4)) << 32); };
        for (int guard = 0; guard < 4096 && ram.ok(p, 16); ++guard) {
            const uint64_t tag = u64(p), regs = u64(p + 8);
            const uint32_t nloop = uint32_t(tag & 0x7FFF), flg = uint32_t(tag >> 58) & 3, nreg = (uint32_t(tag >> 60) & 15) ? (uint32_t(tag >> 60) & 15) : 16;
            p += 16;
            if (flg == 0 && regs == 0xE) {   // A+D: endereco do registrador em hi, dado em lo
                for (uint32_t i = 0; i < nloop && ram.ok(p, 16); ++i, p += 16)
                    if ((u64(p + 8) & 0xFF) == 0x06) tex0 = u64(p);   // TEX0_1
            } else if (flg == 1 && nreg == 2 && (regs & 0xFF) == 0x10) {   // [PRIM, RGBAQ]: cor da proxima corrida
                for (uint32_t i = 0; i < nloop && ram.ok(p, 16); ++i, p += 16) {
                    const uint32_t c = ram.u32(p + 8);
                    col[0] = uint8_t(c); col[1] = uint8_t(c >> 8); col[2] = uint8_t(c >> 16); col[3] = uint8_t(c >> 24);
                }
            } else if (flg == 1 && nreg == 4 && (regs & 0xFFFF) == 0x53D3) {   // sprites: UV, XYZ3, UV, XYZ2 (12.4, centro do GS em 2048)
                for (uint32_t i = 0; i < nloop && ram.ok(p, 32); ++i, p += 32) {
                    constexpr float kOx = 2048.f - 320.f, kOy = 2048.f - 112.f;   // viewport cheio: 640 x 224 (campo), centro 2048
                    TextQuad t{};
                    t.u0 = float(ram.u16(p) & 0x3FFF) / 16.f; t.v0 = float(ram.u16(p + 2) & 0x3FFF) / 16.f;
                    t.x0 = float(ram.u16(p + 8)) / 16.f - kOx; t.y0 = float(ram.u16(p + 10)) / 16.f - kOy;
                    t.u1 = float(ram.u16(p + 16) & 0x3FFF) / 16.f; t.v1 = float(ram.u16(p + 18) & 0x3FFF) / 16.f;
                    t.x1 = float(ram.u16(p + 24)) / 16.f - kOx; t.y1 = float(ram.u16(p + 26)) / 16.f - kOy;
                    t.r = col[0]; t.g = col[1]; t.b = col[2]; t.a = col[3];
                    t.tex0 = tex0;
                    q.push_back(t);
                }
            } else {
                break;   // terminador (EOP, A+D de FBA) ou lixo
            }
            if (tag & 0x8000) break;   // EOP
        }
        text.swap(q);
    }

    // Desenha quadrados de tela (texto, HUD do ScreenPolys) em coordenadas da tela do jogo (640 x 224) escaladas para a faixa 4:3
    // central de `rw` x `rh` (fora do modo 3D, por cima da cena). Ordem do rtMain: screenPolys (ScreenPolys::dmaData) e depois text
    // (fontDmaFontData).
    void drawQuads(const std::vector<TextQuad> &quads, float rw, float rh) {
        if (quads.empty()) return;
        if (!textShaderTried_) {
            // O GS faz textura * cor / 0x80 (0x80 = 1,0) e satura em 1,0: cores acima de 0x80 clareiam o glifo. O raylib so multiplica por
            // <= 1; o shader dobra o RGB (a cor do vertice entra como r/255, ou seja r/0x80 = 2 * r/255).
            textShaderTried_ = true;
            static const char *fs =
                "#version 330\n"
                "in vec2 fragTexCoord; in vec4 fragColor;\n"
                "uniform sampler2D texture0; uniform vec4 colDiffuse; out vec4 finalColor;\n"
                "void main() { vec4 t = texture(texture0, fragTexCoord) * fragColor * colDiffuse; finalColor = vec4(min(t.rgb * 2.0, 1.0), t.a); }\n";
            textShader_ = LoadShaderFromMemory(nullptr, fs);
        }
        const GsScreenRect gr = gsScreenRect(rw, rh);
        const float sx = gr.w / 640.f, sy = gr.h / 224.f;
        if (textShader_.id > 0) BeginShaderMode(textShader_);
        for (const TextQuad &t : quads) {
            const Texture2D tex = residentTexture(t.tex0);
            if (!tex.id) continue;
            const bool gain = textShader_.id > 0;   // sem o shader a cor fica em <= 1,0 (dobra aqui)
            const int k = gain ? 1 : 2;
            const Color tint{uint8_t(std::min(255, t.r * k)), uint8_t(std::min(255, t.g * k)), uint8_t(std::min(255, t.b * k)), uint8_t(std::min(255, t.a * 2))};
            // UV invertido (u1 < u0, v1 < v0: o ScreenPolys espelha e o polyBuildPrim grava V = 0x80 - v): o GS anda de u0 para u1. O
            // DrawTexturePro com largura negativa espelha a faixa [x, x + |w|], entao a origem e o menor dos dois.
            DrawTexturePro(tex, texelRect(tex, t.tex0, Rectangle{std::min(t.u0, t.u1), std::min(t.v0, t.v1), t.u1 - t.u0, t.v1 - t.v0}),
                           Rectangle{gr.x0 + t.x0 * sx, t.y0 * sy, (t.x1 - t.x0) * sx, (t.y1 - t.y0) * sy}, Vector2{0.f, 0.f}, 0.f, tint);
        }
        if (textShader_.id > 0) EndShaderMode();
    }

    // Fades na ordem do GS: underText = true antes do texto (ShellRtLoop::endField despacha a fila do gsPkt antes do
    // fontDmaFontData: o dialogo de tokens sai sobre a tela escura), false por cima de tudo (rtMain manda a fila depois do texto;
    // uiFadeScreen sai depois do fontDmaFontData; Shell::FadeScreen). Retangulo da largura da tela do jogo cobre o quadro inteiro (em
    // widescreen o fade escurece a janela toda); um menor fica na faixa 4:3 central, como o texto. Na tela dividida (splitEdges) cada
    // borda vale por si: o fade e as faixas de cinema de uma vista (0..320 ou 320..640) vao ate a borda da janela do lado dela.
    bool splitEdges = false;
    int stripView = -1;   // tela dividida de 3 jogadores: a vista que ocupa a linha de baixo inteira (2), -1 = nenhuma
    // Barras sobre os inimigos sob o HUD do jogador: com `rects`, o draw as desenha em 2D logo antes da lista `hud`, na textura em que
    // desenha (tw x th, com o canto (ox, oy) da janela de rw x rh), so as da vista `view` (-1 = todas, tela cheia); com barsUnderHud o
    // drawScreenRects as pula. Sem `rects` (replay, corpus) elas saem no drawScreenRects, por cima de tudo.
    struct BarPass {
        const std::vector<ScreenRect> *rects = nullptr;
        float rw = 0.f, rh = 0.f;
        int view = -1;
        float ox = 0.f, oy = 0.f, tw = 0.f, th = 0.f;
    };
    BarPass barPass;
    bool barsUnderHud = false;

    void drawBarPass() {
        const BarPass &p = barPass;
        auto mine = [&](const ScreenRect &r) { return r.barView >= 0 && (p.view < 0 || r.barView == p.view); };
        if (!p.rects || std::none_of(p.rects->begin(), p.rects->end(), mine)) return;
        rlDrawRenderBatchActive();
        rlDisableDepthTest();
        // matrizes trocadas e devolvidas direto: a pilha do rlgl e uma so para as duas (e o push da modelview liga a transformacao
        // na CPU), e o BeginMode3D do chamador ja empilhou a projecao. Ortografica da textura (tw x th), com a janela deslocada de (ox, oy)
        const Matrix keepProj = rlGetMatrixProjection(), keepView = rlGetMatrixModelview();
        Matrix ortho{};
        ortho.m0 = 2.f / p.tw;
        ortho.m5 = -2.f / p.th;
        ortho.m10 = -2.f;
        ortho.m12 = -1.f - 2.f * p.ox / p.tw;
        ortho.m13 = 1.f + 2.f * p.oy / p.th;
        ortho.m14 = -1.f;
        ortho.m15 = 1.f;
        Matrix ident{};
        ident.m0 = ident.m5 = ident.m10 = ident.m15 = 1.f;
        rlSetMatrixProjection(ortho);
        rlSetMatrixModelview(ident);
        BeginBlendMode(BLEND_ALPHA);
        const GsScreenRect gr = gsScreenRect(p.rw, p.rh);
        // O jogo prende o centro do icone a 8-16 px da borda da vista, e a barra e mais larga que o icone: em vez de cortada na borda,
        // a barra inteira (todos os retangulos do mesmo icone, ScreenRect::anchor) e empurrada para dentro da area dela
        struct Span { float x0 = 1e30f, x1 = -1e30f, lo = 0.f, hi = 0.f; };
        std::vector<Span> spans;
        for (const ScreenRect &r : *p.rects) {
            if (!mine(r) || r.anchor < 0) continue;
            if (size_t(r.anchor) >= spans.size()) spans.resize(size_t(r.anchor) + 1);
            Span &sp = spans[size_t(r.anchor)];
            float x0, x1;
            barSpan(gr, p.rw, p.rh, r, x0, x1, sp.lo, sp.hi);
            sp.x0 = std::min(sp.x0, x0);
            sp.x1 = std::max(sp.x1, x1);
        }
        for (const bool hud : {false, true})   // as barras dos inimigos, depois o HUD nativo dos jogadores por cima delas
            for (const ScreenRect &r : *p.rects) {
                if (!mine(r) || r.hud != hud) continue;
                float nudge = 0.f;
                if (r.anchor >= 0 && size_t(r.anchor) < spans.size()) {
                    const Span &sp = spans[size_t(r.anchor)];
                    if (sp.x0 < sp.lo) nudge = sp.lo - sp.x0;
                    else if (sp.x1 > sp.hi) nudge = std::max(sp.hi - sp.x1, sp.lo - sp.x0);
                }
                drawBar(gr, p.rw, p.rh, r, nudge);
            }
        EndBlendMode();
        rlDrawRenderBatchActive();
        rlSetMatrixProjection(keepProj);
        rlSetMatrixModelview(keepView);
    }

    // Barra sobre o inimigo: x na janela (rw de largura) antes do corte e a area em que ela sai (lo, hi: a metade da vista na tela
    // dividida, a janela fora dela)
    void barSpan(const GsScreenRect &gr, float rw, float rh, const ScreenRect &r, float &x0, float &x1, float &lo, float &hi) const {
        // px da janela por px do GS: a faixa da vista (gsScreenRect, na proporcao dela) sobre a largura dela, com a altura da vista na
        // conta (quadrantes: 112 linhas na faixa da janela inteira). Na tela dividida a faixa e a da metade (2:3, 320 px do GS)
        const ViewCtx &cv = curView();
        const float k = cv.w > 0.f && cv.h > 0.f ? gr.w / cv.w * (cv.h / 224.f) : gr.w / 640.f;
        x0 = gr.x0 + r.x0 * k;
        x1 = gr.x0 + r.x1 * k;
        lo = 0.f;
        hi = rw;
        if (r.barView >= 0 && r.barView == stripView && !r.hud) {
            // faixa de baixo dos 3 jogadores (640 x 112 na janela inteira): as linhas tem a escala dos quadrantes, e o px do GS tambem
            // (a do quadrante, a faixa 4:3 da tela cheia / 640), centrado na janela; a conta de cima dava a metade e as barras so iam
            // ate o meio da tela
            const float ks = std::min(rw, rh * 4.f / 3.f) / 640.f, c = 0.5f * rw;
            x0 = c + (r.x0 - 320.f) * ks;
            x1 = c + (r.x1 - 320.f) * ks;
        }
        if (splitEdges && r.barView != stripView) {
            // cada vista sai na sua metade com a camera dela (drawView): o centro da vista no jogo (160 ou 480) vai para o centro da
            // metade, e nao para onde a faixa 4:3 da janela inteira o poria (ficava 1/4 da sobra do widescreen para o lado). Nos
            // quadrantes vale a coluna (v & 1); a altura ja e a do quadrante no jogo. A faixa de baixo dos 3 jogadores (stripView) tem
            // a largura da tela, como fora da tela dividida
            const float half = 0.5f * rw, v = float(r.barView & 1);
            const float shift = v * half + 0.5f * half - (160.f + 320.f * v) * k - gr.x0;
            x0 += shift;
            x1 += shift;
            lo = v * half;
            hi = (v + 1.f) * half;
        }
    }

    // Uma barra sobre o inimigo, em pixels da janela de rw x rh; nudge = deslocamento da barra inteira para caber na area (drawBarPass)
    void drawBar(const GsScreenRect &gr, float rw, float rh, const ScreenRect &r, float nudge = 0.f) {
        const uint8_t a = uint8_t(std::min(255, int(r.a) * 255 / 0x80));
        if (!a) return;
        const float sy = rh / 224.f;
        float x0, x1, lo, hi;
        barSpan(gr, rw, rh, r, x0, x1, lo, hi);
        x0 += nudge;
        x1 += nudge;
        const float fx0 = x0, fw = x1 - x0;   // antes do corte (o retrato tira dai o trecho da textura)
        x0 = std::max(x0, lo);
        x1 = std::min(x1, hi);
        if (x1 <= x0) return;
        if (r.tex0) {   // retrato: o trecho da textura que sobra depois do corte
            const Texture2D t = residentTexture(r.tex0);
            if (!t.id || fw <= 0.f) return;
            const float du = (r.u1 - r.u0) / fw;
            const float u0 = r.u0 + (x0 - fx0) * du, u1 = u0 + (x1 - x0) * du;
            DrawTexturePro(t, texelRect(t, r.tex0, Rectangle{u0, std::min(r.v0, r.v1), u1 - u0, r.v1 - r.v0}), Rectangle{x0, r.y0 * sy, x1 - x0, (r.y1 - r.y0) * sy},
                           Vector2{0.f, 0.f}, 0.f, Color{r.r, r.g, r.b, a});
            return;
        }
        DrawRectangleRec(Rectangle{x0, r.y0 * sy, x1 - x0, (r.y1 - r.y0) * sy}, Color{r.r, r.g, r.b, a});
    }

    void drawScreenRects(float rw, float rh, bool underText = false) {
        const GsScreenRect gr = gsScreenRect(rw, rh);
        const float sy = rh / 224.f;
        for (const ScreenRect &r : screenRects) {
            if (r.underText != underText) continue;
            const uint8_t a = uint8_t(std::min(255, int(r.a) * 255 / 0x80));
            if (!a) continue;
            if (r.barView >= 0) {   // barra sobre o inimigo (com barsUnderHud ja saiu no draw, sob o HUD)
                if (!barsUnderHud) drawBar(gr, rw, rh, r);
                continue;
            }
            const bool full = r.x0 <= 0.f && r.x1 >= 640.f;
            const bool left = full || (splitEdges && r.x0 <= 0.f), right = full || (splitEdges && r.x1 >= 640.f);
            const float x0 = left ? 0.f : gr.x0 + r.x0 * gr.w / 640.f, x1 = right ? rw : gr.x0 + r.x1 * gr.w / 640.f;
            DrawRectangleRec(Rectangle{x0, r.y0 * sy, x1 - x0, (r.y1 - r.y0) * sy}, Color{r.r, r.g, r.b, a});
        }
    }

private:
    std::unordered_map<uint64_t, Texture2D> fontTex_;   // texturas residentes de drawQuads por TEX0 (id 0 = nao decodificado)
    Shader textShader_{};
    bool textShaderTried_ = false;

    // Textura residente de um quadrado de tela (decodeResident): atlas da fonte (T8H 512x512 do .RTX da fase), icones do HUD (T4HL).
    Texture2D residentTexture(uint64_t tex0) {
        const auto it = fontTex_.find(tex0);
        if (it != fontTex_.end()) return it->second;
        Texture2D t{};
        std::vector<uint32_t> px;
        uint32_t pal[256] = {}, w = 0, h = 0;
        const gs::Tex0 t0 = gs::Tex0::decode(tex0);
        uint32_t origAmax = 0;   // alfa cru da original: o PNG de mod le o alfa na escala dela (texmods::loadPng)
        if (!texmods::Index::get().empty() && decodeResident(t0, px, w, h, true, pal))
            for (uint32_t c : px) origAmax = std::max(origAmax, c >> 24);
        if (modTexture(Ram{}, 0, t0, (tex0 >> 34) & 1, false, px, w, h, origAmax > 0x80) || decodeResident(t0, px, w, h, false, pal)) {
            Image img{px.data(), int(w), int(h), 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            t = LoadTextureFromImage(img);
            if (t.id) { GenTextureMipmaps(&t); SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR); SetTextureWrap(t, TEXTURE_WRAP_CLAMP); }
        }
        fontTex_[tex0] = t;
        return t;
    }

    std::unordered_map<uint32_t, std::shared_ptr<MeshData>> cache_;
    std::unordered_map<uint64_t, Texture2D> tex_;
    int texFilter_ = 0;
    bool anisoSet_ = false;   // algum anisotropico ja foi pedido (sem ele, nada a desfazer; sem a extensao o raylib avisa a cada chamada)
    // Filtro atual numa textura do tex_ (feita ou refeita): mipmaps na primeira vez que o filtro pede
    void applyTexFilter(Texture2D &t) {
        if (!t.id) return;
        if (texFilter_ <= 0) {
            rlTextureParameters(t.id, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_LINEAR);
            rlTextureParameters(t.id, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_LINEAR);
            if (anisoSet_) rlTextureParameters(t.id, RL_TEXTURE_FILTER_ANISOTROPIC, 1);
            return;
        }
        if (t.mipmaps <= 1) GenTextureMipmaps(&t);
        rlTextureParameters(t.id, RL_TEXTURE_MIN_FILTER, t.mipmaps > 1 ? RL_TEXTURE_FILTER_MIP_LINEAR : RL_TEXTURE_FILTER_LINEAR);
        rlTextureParameters(t.id, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_LINEAR);
        if (texFilter_ >= 2 || anisoSet_) {
            rlTextureParameters(t.id, RL_TEXTURE_FILTER_ANISOTROPIC, texFilter_ >= 2 ? texFilter_ : 1);
            anisoSet_ = true;
        }
    }
    std::vector<uint32_t> seen_;   // nos ja visitados neste quadro (ciclos / DAG)
    int lod_ = -1;
    float shadowLodD2_ = -1.f;   // >= 0: travessia de uma sombra (collectShadows): LOD por esta distancia ao quadrado e sem o corte da vista
    uint32_t destrHist_[8] = {};   // diagnostico (WOTM_DESTRSTAT): destrutiveis por estado atual
    Shader alphaShader_{};
    VtxStream vs_;   // vertices do drawList sem o modo imediato do rlgl (wotm_fx.hpp)
    int alphaRefLoc_ = -1;   // uniform do teste de alfa (DrawItem::aref) no alphaShader_
    int fogLoc_[5] = {-1, -1, -1, -1, -1};   // uniforms da neblina no alphaShader_: fogEye, fogRow3, fogParams, fogColor, fogSplit
    bool alphaShaderTried_ = false;
    std::unordered_map<uint64_t, bool> partial_;   // textura (texKey) com alfa semitransparente
    std::unordered_map<uint64_t, uint8_t> texAmax_;   // maior alfa cru da textura (texKey): 0x80 = 1,0, ate 0xFF
    static constexpr size_t kShadowCacheN = 48;
    static constexpr float kShadowTol = 1.5f;   // unidades do mundo (a silhueta tem ~2 por texel)
    std::vector<ShadowCacheEntry> shadowCache_;   // silhuetas das sombras pela chave das entradas (shadowKey), a menos usada sai
    uint64_t shadowTick_ = 0;
    std::vector<float> shadowCov_, shadowTmp_;    // cobertura e borrao da silhueta (reusados)
    std::vector<uint32_t> shadowPx_;
    static constexpr int kShadowRes = 128;
    uint32_t resSig_ = 0;   // assinatura dos recursos em fileStatus ja carregados (loadResources)
    GameCamera cam_;   // camera do ultimo collect/blend (eixos para os sprites)
    bool eyeOk_ = false;
    float eye_[3] = {0, 0, 0};
    std::shared_ptr<const uint8_t> fxRam_;   // Snapshot::fxRam do quadro mostrado (ou fxMix_ na interpolacao)
    std::shared_ptr<uint8_t> fxMix_;         // copia de sombra das particulas no instante misturado (showBlend, Particles::blendRam)
    // cameras de A, B e a misturada do ultimo showBlend (sem corte), com o foco de cada uma: blendFlares e blendAnchors; o show desliga
    int attached_ = 0;   // objetos que o ultimo showBlend levou com a camera (pose relativa igual em A e B)
    struct BlendView { bool on = false; int view = 0; float alpha = 1.f; M4 CA, CB, CI; interp::Focus fa, fb, fi; } hudMix_;
    size_t fxMixSize_ = 0;
    uint32_t atMat_ = 0;   // animOutput.atMat do CHAR_INSTANCE que contem o no atual
    uint32_t animPkt_ = 0; // animPktPtr do CHAR_INSTANCE que contem o no atual
    uint32_t animVal_ = 0; // animOutput.val (floats dos canais de animacao) do CHAR_INSTANCE que contem o no atual
    uint32_t cs_ = 0;      // _cs em desenho (drawCs; 0 no mundo e no ceu): colorQuad em objectColor
    int lodIdx_ = 0;       // LOD escolhido mais recentemente na descida
    int16_t aref_ = kGsAlphaRef;       // DrawItem::aref herdado do no mais proximo com ctx de TEST_1 (CtxScope)
    bool zmsk_ = false;                // DrawItem::zmsk herdado do no mais proximo com ctx de ZBUF_1 (CtxScope)
    uint8_t blend_ = kGsAlphaNormal;   // DrawItem::blend herdado do no mais proximo com ctx de ALPHA_1 (CtxScope)
    float fade_ = 1.f;     // DrawItem::fade (cross-fade de LOD do hierLod) na descida
    bool lodX_ = false;    // DrawItem::lodX na descida
    float spec_ = 0.f;     // DrawItem::spec: o qword 20 que esta na memoria do VU1 neste ponto da ordem do hier

    // Tabela de luz do nivel (lightDir, scratchpad 0x70001300 + 64 * nivel; q6-q8 do VU1): linhas = eixos do objeto, colunas = luzes
    // 0-2 e vetor-meio, x32. Fora de um SCALE ela e o paraL levado pela matriz (o microprograma 0x1A8 do VU0 faz filho = M * pai), e o
    // vertexRgb a reproduz com as linhas da matriz normalizadas. O hierScaleNode (flag +0xC = 0) e o hierAnimScaleNode (escala nao
    // uniforme: |sx - sy| ou |sx - sz| >= 0,001) gravam outra coisa: diag(1/sx, 1/sy, 1/sz) * pai (mathfMulMatrix3x3, que so escreve
    // xyz e deixa o w da matriz unitaria: vetor-meio 0, sem especular), cada coluna normalizada (mathfNormalizeColumns; norma < 1e-7
    // vira (0, 0, 1)) e vezes 32. Com escala (1; 4; 5), as duas pecas 0x311 do Togera saiam 15 a 20 mais claras no nativo. Abaixo de
    // um desses nos, lightOwn_ leva a tabela (lightObj_[luz][eixo]) e cada matriz local a gira como o 0x1A8.
    bool lightOwn_ = false;
    float lightObj_[4][3] = {};
    struct LightScope {   // restaura a tabela de luz na saida do no
        Scene &s;
        bool own;
        float l[4][3];
        explicit LightScope(Scene &sc) : s(sc), own(sc.lightOwn_) { std::memcpy(l, sc.lightObj_, sizeof l); }
        ~LightScope() { s.lightOwn_ = own; std::memcpy(s.lightObj_, l, sizeof l); }
    };
    void lightRotate(const M4 &local) {   // filho = local * pai (linhas de local = eixos do filho no espaco do pai)
        if (!lightOwn_) return;
        float o[4][3];
        for (int i = 0; i < 4; ++i)
            for (int r = 0; r < 3; ++r)
                o[i][r] = local.m[r][0] * lightObj_[i][0] + local.m[r][1] * lightObj_[i][1] + local.m[r][2] * lightObj_[i][2];
        std::memcpy(lightObj_, o, sizeof o);
    }
    void lightScale(const M4 &m, float sx, float sy, float sz) {   // m = matriz do pai (mundo)
        float base[4][3];
        if (lightOwn_) std::memcpy(base, lightObj_, sizeof base);
        else   // a tabela do pai: paraL no espaco do pai (linhas de m normalizadas, como o vertexRgb)
            for (int r = 0; r < 3; ++r) {
                const float len = std::sqrt(m.m[r][0] * m.m[r][0] + m.m[r][1] * m.m[r][1] + m.m[r][2] * m.m[r][2]);
                for (int i = 0; i < 4; ++i)
                    base[i][r] = len > 1e-6f ? (m.m[r][0] * lights_.dir[i][0] + m.m[r][1] * lights_.dir[i][1] + m.m[r][2] * lights_.dir[i][2]) / len : 0.f;
            }
        const float s[3] = {sx, sy, sz};
        for (int i = 0; i < 3; ++i) {
            float v[3], n2 = 0.f;
            for (int r = 0; r < 3; ++r) { v[r] = base[i][r] / s[r]; n2 += v[r] * v[r]; }
            const float n = std::sqrt(n2);
            if (!(n >= 1e-7f)) { v[0] = v[1] = 0.f; v[2] = 1.f; }
            else for (float &x : v) x /= n;
            for (int r = 0; r < 3; ++r) lightObj_[i][r] = 32.f * v[r];
        }
        lightObj_[3][0] = lightObj_[3][1] = lightObj_[3][2] = 0.f;
        lightOwn_ = true;
    }

    // O ctx de um GROUP (+4) e um pacote de estado do GS: tag A+D (NLOOP em +0x10, um registrador A+D) e os pares (dado, endereco) a
    // partir de +0x20; medidos: ALPHA_1 (0x42), TEST_1 (0x47) e ZBUF_1. Traduz para o estado dos filhos (-1 = ctx sem o registrador,
    // herda o do pai):
    //  - alpha: byte baixo do ALPHA_1 (A,B,C,D = bits 0-1, 2-3, 4-5, 6-7; ver gsBlend) para DrawItem::blend. Medidos: 0x44 comum, 0x48
    //    aditivo (Kineticlops), 0x42 escurece e muda o tom, 0x46 e 0x81 (Kineticlops em outra cor);
    //  - aref: limiar do teste de alfa do TEST_1 para DrawItem::aref. Sem ATE (bit 0): sem teste (kGsAlphaNoTest; o halo dos
    //    power-ups e o brilho dos postes, 0x5040C com alfa de vertice 6 a 38, perdiam o degrade no limiar 8 e viravam disco de borda
    //    dura) e ALWAYS (ATST 1) tambem. GREATER (6) passa com A > AREF (bits 4-11), GEQUAL (5) com A >= AREF; os outros ATST nao
    //    aparecem e ficam no padrao do shader (kGsAlphaRef).
    //    O 0x81 do Kineticlops vem com 0x5040D (A > 0x40): (Cd - Cs) As nao soma o fundo, e sem o teste o corpo inteiro sairia preto.
    //  - zmsk: ZBUF_1.ZMSK (bit 32) para DrawItem::zmsk. O logo do telao do drive-in (tex 140/141, GROUP com ZTST GEQUAL e ZMSK) fica
    //    atras do telao (e8da50, sem ctx: grava Z) e o GS o esconde; sem o Z do telao, o nativo o mostrava na selecao de fase.
    static constexpr int kRefAbsent = -1000;   // groupState: ctx sem TEST_1 (herda o aref do pai)
    static void groupState(const Ram &ram, uint32_t ctx, int &alpha, int &aref, int &zmsk) {
        alpha = zmsk = -1;
        aref = kRefAbsent;
        if (!ctx || !ram.ok(ctx, 0x20) || ram.u32(ctx + 0x14) != 0x10000000 || ram.u32(ctx + 0x18) != 0xE) return;
        const uint32_t n = std::min<uint32_t>(ram.u32(ctx + 0x10) & 0x7FFF, 8);
        if (!ram.ok(ctx, 0x20 + 16 * n)) return;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t d = ram.u32(ctx + 0x20 + 16 * i), reg = ram.u32(ctx + 0x28 + 16 * i) & 0xFF;
            if (reg == 0x42) alpha = int(d & 0xFF);
            else if (reg == 0x47) {
                const uint32_t atst = (d >> 1) & 7, ref = (d >> 4) & 0xFF;
                static const bool oldAte = std::getenv("WOTM_ATEOLD") != nullptr;   // diagnostico: ATE 0 com o limiar padrao (antes de 2026-10-17)
                aref = (!(d & 1) || atst == 1) ? (oldAte ? kGsAlphaRef : kGsAlphaNoTest) : atst == 6 ? int(ref) : atst == 5 ? std::max(int(ref) - 1, 0) : kGsAlphaRef;
            } else if (reg == 0x4E) zmsk = int(ram.u32(ctx + 0x24 + 16 * i) & 1);
        }
    }

    // Estado do GS herdado na descida. O hierTraverseAsm troca o ctx atual ($s4, que comeca no ctxGetDefaultGSCtx) em todo no que
    // carrega um ctx nao nulo, nao so no GROUP: TRANSLATE, ROTATE e SCALE em +4; ANIM_XFORM e ANIM_SCALE em +8 (contagem em +4). Os
    // filhos herdam e o pai volta ao sair (o $s4 vai na pilha da travessia). A esfera da explosao (tex 146) esta abaixo de um desses
    // com ALPHA_1 0x48 (aditivo), sem GROUP com ctx; o nativo a desenhava com a mistura comum. O mesmo com o halo dos power-ups (tex 70):
    // saia como um disco escuro em vez de clarear.
    struct CtxScope {
        Scene &s;
        uint8_t blend;
        int16_t aref;
        bool zmsk;
        CtxScope(Scene &sc, const Ram &ram, uint32_t ctx) : s(sc), blend(sc.blend_), aref(sc.aref_), zmsk(sc.zmsk_) {
            int b, r, z;
            groupState(ram, ctx & 0x0FFFFFFF, b, r, z);
            if (b >= 0) s.blend_ = uint8_t(b);
            if (r != kRefAbsent) s.aref_ = int16_t(r);
            if (z >= 0) s.zmsk_ = z != 0;
        }
        ~CtxScope() { s.blend_ = blend; s.aref_ = aref; s.zmsk_ = zmsk; }
    };

    // Deslocamento do UV do OBJECT, como o hierTraverseAsm monta o qword 4 do cabecalho do VU1 (vf19.xy, que os programas de desenho somam
    // ao UV normalizado): uOffset/vOffset (+0x20/+0x24) e, com uvScroll (bit 0x800 das flags) dentro de um CHAR_INSTANCE, os canais de
    // animacao uOffIdx/vOffIdx (+0x30/+0x32; indice negativo = 0).
    void objectUvOffset(const Ram &ram, uint32_t node, float out[2]) const {
        out[0] = ram.f32(node + 0x20);
        out[1] = ram.f32(node + 0x24);
        if (!(ram.u16(node + 0xC) & 0x800) || !animVal_) return;
        for (int k = 0; k < 2; ++k) {
            const int16_t idx = ram.s16(node + 0x30 + 2 * k);
            out[k] = idx >= 0 ? ram.f32(animVal_ + 4 * uint32_t(idx)) : 0.f;
        }
    }

    // Multiplicador da cor do vertice do OBJECT, como o hierTraverseAsm monta o qword 5 do cabecalho do VU1 (vf31, que todos os programas
    // de desenho aplicam a cor do vertice): redAnim..alphaAnim (+0x40) ou, dentro de um CHAR_INSTANCE, os canais rAnimIdx..aAnimIdx
    // (+0x34; indice negativo = o do no), vezes o colorQuad (+0x70) do CS em desenho. O retail tambem multiplica o alfa pelo fade do
    // no (aqui em DrawItem::fade). Detalhes do asm (0x2085C8-0x2086D0):
    //  - o canal azul nunca vem da animacao: no lugar do lwc1 do valor animado ha um swc1 do blueAnim (fica o +0x48);
    //  - o colorQuad so entra com CsColor::isEnabled() (D_007B3040, copiado no scratchpad +0x8C pelo pktCacheForAsm);
    //  - o asm ainda multiplica o RGB pela cor ambiente animada (viewAnimAmbientRed/Green/Blue = ambiente / 32 do viewSetAmbient)
    //    quando o flag do scratchpad +0x84 (viewAmbientChanged, gravado pelo pktCacheForAsm) esta ligado, mas o q5 que chega ao
    //    VU1 nunca o traz: na UFO (ambiente 15, 0,46875, flag ligado na RAM) os 1.309 cabecalhos da foto 2026-10-18_232542/002
    //    tem q5 = (1, 1, 1, alfa), e o PCSX2 mostra a fase clara. Aplicado aqui, a UFO saia 2,13 vezes mais escura. Nao entra.
    void objectColor(const Ram &ram, uint32_t node, float out[4]) const {
        const bool quad = cs_ && ram.u32(0x007B3040) != 0;
        const float *atDraw = nullptr;   // colorQuad da hora do hier (csColorAtDraw), no lugar do que sobrou na RAM
        if (quad)
            for (const CsColorAtDraw &c : csColorAtDraw)
                if (c.cs == cs_) { atDraw = c.q; break; }
        for (int k = 0; k < 4; ++k) {
            out[k] = ram.f32(node + 0x40 + 4 * k);
            const int16_t idx = ram.s16(node + 0x34 + 2 * k);
            if (animVal_ && idx >= 0 && k != 2) out[k] = ram.f32(animVal_ + 4 * uint32_t(idx));
            if (quad) out[k] *= atDraw ? atDraw[k] : ram.f32(cs_ + 0x70 + 4 * k);
            if (!std::isfinite(out[k])) out[k] = 1.f;
        }
    }

    // Mapa de ambiente: o pktAddVu1ObjAsm chama o pktBldEnvMapDma quando as flags (+0xC) tem envMapped (0x2), envMapIsVideo (0x4) ou
    // envMapCloakFx (0x8, que ele liga quando o _cs em desenho tem cloakMe, +0xAA). Ramo 2 (video, quadro anterior ou CLOAK) com
    // envMapIsVideo, texId[0] (+0x50) = 0 ou CLOAK; senao ramo 1. k = envMapWeight (+0x5C; 0 = 16); no CLOAK, cloakWeight (+0xAC) + j/64,
    // j = 3 - (count & 7) com count & 8, senao (count & 7) - 3 (count = contador de quadros 0x6F88E0). O CLOAK (env 3) tambem poe
    // ALPHA_1 0x44 (kGsAlphaNormal), troca o multiplicador de cor q5 por (2,59375; 2,59375; 2,59375; 1) e a textura de cada slot pela do
    // quadro anterior (texKey com DrawItem).
    void objectEnv(const Ram &ram, uint32_t node, DrawItem &it) const {
        const uint16_t f = ram.u16(node + 0xC);
        const bool cloak = cs_ && ram.s16(cs_ + 0xAA) != 0;
        if (!(f & 0x6) && !cloak) return;
        it.env = cloak ? 3 : ((f & 0x4) || ram.u16(node + 0x50) == 0) ? 2 : 1;
        it.envK = ram.f32(node + 0x5C);
        if (it.envK == 0.f) it.envK = 16.f;
        if (cloak) {
            const uint32_t n = ram.u32(wotm::sym::count_006F88E0);
            const int j = (n & 8) ? 3 - int(n & 7) : int(n & 7) - 3;
            it.envK = ram.f32(cs_ + 0xAC) + float(j) / 64.f;
            it.blend = kGsAlphaNormal;
            for (int c = 0; c < 3; ++c) it.color[c] = 2.59375f;
            it.color[3] = 1.f;
        }
    }

    // Paleta de matrizes de um objeto skinned: animPktPtr[i] e uma cadeia DMA cujo 1o tag (ref) aponta o bloco da paleta: 1 qword
    // de VIF (UNPACK V4-32 para o qword 0x358 do VU1) e as matrizes, 4 qwords cada, a partir de +0x10. Em geral 0x7d qwords (31
    // matrizes); o corpo do Dragon usa uma paleta propria de 0x41 (16 matrizes), mandada antes de cada sub-pacote. Como o destino no
    // VU1 e o mesmo, o codigo de osso indexa as duas do mesmo jeito. O indice i e o do OBJECT em +0x68, como no pktAddVu1ObjAsm
    // (negativo: o jogo manda o pacote do +4, sem paleta); o indice do LOD so coincide quando o personagem tem um objeto skinned por
    // LOD (o Dragon tem corpo e asas: com o do LOD, o corpo pegava a paleta de outro objeto). O chefe final (CAPITOL) usa 0x81
    // qwords (32 matrizes, a paleta inteira que o codigo de osso alcanca); com o limite antigo de 0x7D, tres das quatro partes dele
    // ficavam sem paleta e saiam na pose crua (pernas para o ceu, domo fora do lugar). Devolve 0 se nao achar.
    static uint32_t palette(const Ram &ram, const DrawItem &it) {
        if (!it.animPkt) return 0;
        int idx = ram.ok(it.node + 0x68, 4) ? int(int32_t(ram.u32(it.node + 0x68))) : -1;
        if (idx < 0) idx = it.lodIdx;   // sem indice no OBJECT: o comportamento antigo
        if (idx < 0 || idx > 63) return 0;
        const uint32_t chain = ram.u32(it.animPkt + 4 * uint32_t(idx)) & 0x0FFFFFFF;
        if (!ram.ok(chain, 8) || ((ram.u32(chain) >> 28) & 7) != 3) return 0;
        const uint32_t qwc = ram.u32(chain) & 0xFFFF;
        if (qwc < 5 || qwc > 0x81 || (qwc - 1) % 4 != 0) return 0;
        const uint32_t block = ram.u32(chain + 4) & 0x0FFFFFFF;
        return ram.ok(block, 0x10 + 0x40 * 32) ? block : 0;
    }

    // Luz por vertice, como o programa do VU1 em 0x1010 (OBJECT com dirLight, bit 0x100 das flags em +0xC): com a normal crua n (modulo
    // ~127) girada pelo osso x (skinned) e pelo objeto, d_i = max(0, n.L_i / 4096) para as tres luzes paralelas e o vetor-meio do
    // especular (colunas de paraL, x32; o hier as leva ao espaco de cada objeto em lightDir) e
    //   cor = min(255, cor do vertice * (soma de cor_i * d_i) + especular * d_3^16)
    // com cor_i em vu1LightColor (D_0025B238, cor/128; qwords 17-19 do cabecalho) e especular = specIntensity (+0x6C) com specLightMe
    // (bit 0x2000), senao 0 (qword 20). O qword 5 do cabecalho, que tambem multiplica a cor do vertice, vale 1.
    static ParaLights readParaLights(const Ram &ram) {
        ParaLights l;
        for (int i = 0; i < 4; ++i)
            for (int a = 0; a < 3; ++a) l.dir[i][a] = ram.f32(addr::paraL + 16 * a + 4 * i);
        for (int i = 0; i < 3; ++i)
            for (int c = 0; c < 3; ++c) l.col[i][c] = ram.f32(addr::vu1LightColor + 16 * i + 4 * c);
        return l;
    }
    ParaLights lights_;   // luzes paralelas do ultimo collect (vertexRgb)

    // Luzes pontuais (PointLights em pointLights 0x6DFCE0): o hier chama GetClosestPts, que poe na memoria do VU0 as ate 8 luzes ativas
    // mais perto da camera (closest[] em +0x800, na ordem; posicao xyz + alcance em w a partir do q30, cor rgb + falloff em w a partir
    // do q38) e o hierCacheForAsm grava a posicao da camera (viewGetTrans) no q28 e a contagem (+0x820) no scratchpad.
    struct PointLightList { int n = 0; float pos[8][4] = {}; float col[8][4] = {}; float eye[3] = {}; };
    static PointLightList readPointLights(const Ram &ram) {
        PointLightList l;
        constexpr uint32_t pl = wotm::sym::pointLights;
        if (!ram.ok(pl, 0x8A0) || ram.u32(pl + 0x820) == 0) return l;
        const uint32_t cs = ram.u32(viewInfoAddr());
        if (!ram.ok(cs, 0x20)) return l;
        for (int a = 0; a < 3; ++a) l.eye[a] = ram.f32(cs + 0x10 + 4 * a);
        static const bool log = std::getenv("WOTM_PTLOG") != nullptr;
        if (log) {
            std::fprintf(stderr, "[ptl] count %u closest", ram.u32(pl + 0x820));
            for (int k = 0; k < 8; ++k) std::fprintf(stderr, " %d", int32_t(ram.u32(pl + 0x800 + 4 * k)));
            std::fprintf(stderr, " eye (%g %g %g)\n", l.eye[0], l.eye[1], l.eye[2]);
            for (int i = 0; i < 32; ++i) {
                const uint32_t p = pl + 0x40 * uint32_t(i);
                if (!ram.u32(p)) continue;
                std::fprintf(stderr, "[ptl] luz %d f38 %u f30 %u pos (%g %g %g) alcance %g cor (%g %g %g) falloff %g int %g\n", i, ram.u32(p + 0x38),
                             ram.u32(p + 0x30), ram.f32(p + 0x10), ram.f32(p + 0x14), ram.f32(p + 0x18), ram.f32(p + 0x1C), ram.f32(p + 0x20),
                             ram.f32(p + 0x24), ram.f32(p + 0x28), ram.f32(p + 0x2C), ram.f32(p + 0x34));
            }
        }
        // closest[0] < 0: o GetClosestPts grava contagem 0 e sai sem limpar closest[1..7] (restos de quadros anteriores)
        if (int32_t(ram.u32(pl + 0x800)) < 0) return l;
        for (int k = 0; k < 8; ++k) {
            const int32_t i = int32_t(ram.u32(pl + 0x800 + 4 * k));
            if (i < 0 || i >= 32) continue;
            const uint32_t p = pl + 0x40 * uint32_t(i);
            for (int a = 0; a < 4; ++a) {
                l.pos[l.n][a] = ram.f32(p + 0x10 + 4 * a);
                l.col[l.n][a] = ram.f32(p + 0x20 + 4 * a);
            }
            ++l.n;
        }
        return l;
    }
    PointLightList pts_;   // do ultimo collect (objectPointLights)

    // Luzes pontuais de um OBJECT, como o hierTraverseAsm e o programa 0x748 do VU0: so OBJECT comum (objType 0) com pointLight (0x80),
    // com luzes no quadro e, dentro de um _cs, com ptLightMe (+0xA9). Com R = linhas 0-2 da matriz e T = linha 3 menos a camera (o hier
    // empilha as matrizes relativas a camera), para cada luz da lista, na ordem, ate 3:
    //   v = R^T (L - camera);  toca se |T + centro - v|^2 < alcance^2 + raio^2;  ptRel = T - v
    // A conta mistura espacos (T nao e girado), como no jogo; com R = identidade e a posicao da luz no espaco do objeto.
    void objectPointLights(const Ram &ram, uint32_t node, DrawItem &it) const {
        it.ptN = 0;
        if (!pts_.n) return;
        const uint16_t f = ram.u16(node + 0xC);
        if (!(f & 0x80) || (f & 0x60)) return;
        if (cs_ && ram.s8(cs_ + 0xA9) == 0) return;
        const M4 &m = it.m;
        float T[3], c[3];
        for (int a = 0; a < 3; ++a) {
            T[a] = m.m[3][a] - pts_.eye[a];
            c[a] = T[a] + ram.f32(node + 0x10 + 4 * a);
        }
        const float r = ram.f32(node + 0x1C);
        for (int k = 0; k < pts_.n && it.ptN < 3; ++k) {
            float d[3], v[3], s = 0.f;
            for (int a = 0; a < 3; ++a) d[a] = pts_.pos[k][a] - pts_.eye[a];
            for (int a = 0; a < 3; ++a) {
                v[a] = m.m[a][0] * d[0] + m.m[a][1] * d[1] + m.m[a][2] * d[2];
                s += (c[a] - v[a]) * (c[a] - v[a]);
            }
            static const bool log = std::getenv("WOTM_PTLOG") != nullptr;   // diagnostico: cada teste de esfera
            if (log)
                std::fprintf(stderr, "[ptl] no %x luz %d v (%g %g %g) T (%g %g %g) d2 %g lim %g\n", node, k, v[0], v[1], v[2], T[0], T[1], T[2], s,
                             pts_.pos[k][3] * pts_.pos[k][3] + r * r);
            if (!(s - (pts_.pos[k][3] * pts_.pos[k][3] + r * r) < 0.f)) continue;
            const int j = it.ptN++;
            for (int a = 0; a < 3; ++a) {
                it.ptRel[j][a] = T[a] - v[a];
                it.ptCol[j][a] = pts_.col[k][a];
            }
            it.ptFall[j] = pts_.col[k][3];
        }
    }

public:
    // Neblina, como os programas de desenho do VU1 e o GS: por vertice das secoes com PRIM.FGE,
    //   F = clamp(fogBurnThru do OBJECT (+0x2C) + fogFA + fogFB / W, 0, fogMaxVal)   (W = linha 3 do worldToScreenMat . (p - olho))
    // e o GS mistura (F * cor + (255 - F) * FOGCOL) / 255 (F = 255: sem neblina). Parametros de vu1FogParams; FOGCOL de fogColorCtx[4].
    static Fog readFog(const Ram &ram) {
        Fog f;
        f.fa = ram.f32(addr::vu1FogParams);
        f.maxVal = ram.f32(addr::vu1FogParams + 4);
        f.fb = ram.f32(addr::vu1FogParams + 12);
        const uint32_t c = ram.u32(addr::fogColorCtx + 0x20);
        for (int k = 0; k < 3; ++k) f.col[k] = float((c >> (8 * k)) & 0xFF) / 255.f;
        return f;
    }
    Fog fog_;   // do ultimo collect
    // F de um ponto (mundo) de uma secao com FGE; mesma conta do shader do draw. row3/eye: linha 3 do worldToScreenMat e olho.
    float fogF(const float p[3], float burn, const float row3[4], const float eye[3]) const {
        const float w = row3[0] * (p[0] - eye[0]) + row3[1] * (p[1] - eye[1]) + row3[2] * (p[2] - eye[2]) + row3[3];
        if (!(w > 1e-3f)) return fog_.maxVal;
        return std::min(fog_.maxVal, std::max(0.f, burn + fog_.fa + fog_.fb / w));
    }

    // UV do mapa de ambiente (DrawItem::env), como o pktBldEnvMapDma monta q997-q999 e o programa do VU1 (0x0A78; 0x0C08 com recorte)
    // calcula ST = R . n + (S + desloc) * escala, Q = 1. n = normal crua / 4096 (ITOF12; nos skinned, girada pelo osso x e truncada
    // como em vertexRgb) levada ao mundo pelas linhas da matriz do objeto (com a escala do _cs); e = (direita . n, baixo . n) com os
    // eixos do olho (weMat de worldCtx, iguais aos tirados da worldToScreenMat); S = (X, Y) do vertice na tela do GS. Com o ViewDef da
    // vista 0 (viewInfo[0].def: w, h em +4/+6 e offX/offY em +0x10/+0x14):
    //   ramo 1: s = 0,5 + k e.x + f (X - offX) / w,  t = 0,5 - k e.y - f (Y - offY) / h,  f = 1 - 0,05 |k|
    //   ramo 2: s = k e.x + (X - offX + w/2) / 1024,  t = k e.y + (Y - offY + h/2) / 256   (texels da tela 1024 x 256; sem o
    //           meio texel do campo impar, viewIsOddScan, nem os +320 das vistas > 0)
    struct EnvView { float W[4][4]; float eye[3]; float right[3], down[3]; float offX = 2048.f, offY = 2048.f, w = 640.f, h = 224.f; bool ok = false; };
    static EnvView envView(const Ram &ram, const float W[4][4], const float eye[3]) {
        EnvView ev;
        std::memcpy(ev.W, W, sizeof ev.W);
        std::memcpy(ev.eye, eye, sizeof ev.eye);
        float rl = 0.f, dl = 0.f;
        const float ocx = curView().cx, ocy = curView().cy;   // centro da vista no GS (linha 0 = foco * direita + cx * frente)
        for (int a = 0; a < 3; ++a) {
            ev.right[a] = W[0][a] - ocx * W[3][a];
            ev.down[a] = W[1][a] - ocy * W[3][a];
            rl += ev.right[a] * ev.right[a];
            dl += ev.down[a] * ev.down[a];
        }
        if (!(rl > 0.f && dl > 0.f)) return ev;
        rl = std::sqrt(rl); dl = std::sqrt(dl);
        for (int a = 0; a < 3; ++a) { ev.right[a] /= rl; ev.down[a] /= dl; }
        const uint32_t def = ram.u32(viewInfoAddr() + 4) & 0x0FFFFFFF;   // viewInfo[vista].def
        if (ram.ok(def, 0x18) && ram.u16(def + 4) && ram.u16(def + 6)) {
            ev.w = ram.u16(def + 4); ev.h = ram.u16(def + 6);
            ev.offX = ram.f32(def + 0x10); ev.offY = ram.f32(def + 0x14);
        }
        ev.ok = true;
        return ev;
    }
    // Normal do vertice no mundo para o envUv. bone: osso x (skinned) ou nulo; m: matriz do objeto.
    static void envNormal(const Vtx &v, const M4 *bone, const M4 &m, float out[3]) {
        float n[3] = {float(v.nx), float(v.ny), float(v.nz)};
        if (bone) {
            const float b[3] = {n[0], n[1], n[2]};
            for (int c = 0; c < 3; ++c) n[c] = std::trunc(b[0] * bone->m[0][c] + b[1] * bone->m[1][c] + b[2] * bone->m[2][c]);
        }
        for (int c = 0; c < 3; ++c) out[c] = (n[0] * m.m[0][c] + n[1] * m.m[1][c] + n[2] * m.m[2][c]) / 4096.f;
    }
    // Falso atras da camera (fica o UV do pacote).
    static bool envUv(uint8_t env, float k, const EnvView &ev, const float p[3], const float nw[3], float &u, float &v) {
        if (!env || !ev.ok) return false;
        const float d[3] = {p[0] - ev.eye[0], p[1] - ev.eye[1], p[2] - ev.eye[2]};
        const float q = ev.W[3][0] * d[0] + ev.W[3][1] * d[1] + ev.W[3][2] * d[2] + ev.W[3][3];
        if (!(q > 1e-3f)) return false;
        const float X = (ev.W[0][0] * d[0] + ev.W[0][1] * d[1] + ev.W[0][2] * d[2] + ev.W[0][3]) / q;
        const float Y = (ev.W[1][0] * d[0] + ev.W[1][1] * d[1] + ev.W[1][2] * d[2] + ev.W[1][3]) / q;
        const float ex = k * (ev.right[0] * nw[0] + ev.right[1] * nw[1] + ev.right[2] * nw[2]);
        const float ey = k * (ev.down[0] * nw[0] + ev.down[1] * nw[1] + ev.down[2] * nw[2]);
        if (env == 1) {
            const float f = 1.f - 0.05f * std::fabs(k);
            u = 0.5f + ex + f * (X - ev.offX) / ev.w;
            v = 0.5f - ey - f * (Y - ev.offY) / ev.h;
        } else {
            u = ex + (X - ev.offX + ev.w * 0.5f) / 1024.f;
            v = ey + (Y - ev.offY + ev.h * 0.5f) / 256.f;
        }
        return true;
    }
private:

    // obj: luzes no espaco do objeto (DrawItem::lightObj) quando ele as tem; nulo = paraL girado pela matriz do objeto
    struct ObjLight { bool lit = false; float spec = 0.f; const DrawItem *pt = nullptr; const float (*obj)[3] = nullptr; };

    // Entrada da conta dos vertices de um pedaco do drawList (item + slot + passe), tudo ja resolvido na thread da janela
    struct EmitIn {
        const DrawItem *it; const MeshData *md; uint32_t slot; bool multi;
        const M4 *pm; M4 skinM; bool fudge; float fzScale; float rowLen[3];
        ObjLight ol; float fade, fogBurn; unsigned tid; bool gsA, forceOpaque, premul; int mode; bool wire;
        bool gpuCull = false;   // todas as faces, com Sink::tri marcando as de culling (a GPU corta: arena residente)
    };
    struct EmitJob { EmitIn in; std::shared_ptr<const MeshData> keep; std::vector<M4> pmOwn; int buf = -1; };
    std::deque<EmitJob> jobs_;   // pedacos adiados do drawList em curso (reaproveitados)
    size_t nJobs_ = 0;
    // Destino dos vertices de um pedaco adiado: o mesmo que o VtxStream faria (cor, UV e normal correntes; a normal normalizada como o
    // rlNormal3f), num buffer proprio
    struct BufSink {
        std::vector<VtxStream::V> &out;
        VtxStream::V cur{};
        float last[3] = {0.f, 0.f, 0.f};
        void color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) { cur.c[0] = r; cur.c[1] = g; cur.c[2] = b; cur.c[3] = a; }
        void tex(float u, float v) { cur.u = u; cur.v = v; }
        void shaderN(float burn, bool fge, bool gs, bool pm, int mode) {
            float n[3];
            shaderNormalVals(burn, fge, gs, pm, mode, n);
            if (n[0] == last[0] && n[1] == last[1] && n[2] == last[2]) return;
            last[0] = n[0]; last[1] = n[1]; last[2] = n[2];
            const float length = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (length != 0.0f) { const float il = 1.0f / length; n[0] *= il; n[1] *= il; n[2] *= il; }
            cur.nx = n[0]; cur.ny = n[1]; cur.nz = n[2];
        }
        void vertex(float x, float y, float z) { cur.x = x; cur.y = y; cur.z = z; out.push_back(cur); }
        void tri(bool) {}
    };

    // ---- arena residente (VtxStream::arenaAlloc): pedacos do drawList que nao mudam entre vistas e quadros
    // Tudo o que entra na conta dos vertices de um pedaco parado (emitVerts sem ossos, mapa de ambiente, fudge nem
    // sprites). Bytes zerados antes de preencher (comparado com memcmp).
    struct ResKey {
        const MeshData *md; uint32_t sig, node, slot; int32_t mode;
        float m[16], color[4], fade, fogBurn, uvOff[2], spec;
        float lobj[4][3], ldir[4][3], lcol[3][3];
        float ptRel[3][3], ptCol[3][3], ptFall[3];   // luzes pontuais (pointRgb)
        uint8_t lit, ownLight, forceOpaque, premul, premulShader, texOn, multi, ptN;
    };
    struct ResRun { uint32_t start, n; bool cull; };
    struct ResChunk { ResKey key; long off = 0; std::vector<ResRun> runs; std::shared_ptr<const MeshData> keep; };
    // Sink da arena: como o BufSink, com as faixas de faces com e sem culling na ordem
    struct ResSink : BufSink {
        std::vector<ResRun> &runs;
        ResSink(std::vector<VtxStream::V> &o, std::vector<ResRun> &r) : BufSink{o}, runs(r) {}
        void tri(bool cull) {
            if (runs.empty() || runs.back().cull != cull) runs.push_back({uint32_t(out.size()), 0, cull});
            runs.back().n += 3;
        }
    };
    std::unordered_map<uint64_t, ResChunk> resChunks_;
    std::unordered_map<uint64_t, uint64_t> resSeen_;   // chave -> redesenho em que apareceu (pedaco so depois de repetir num redesenho seguinte)
    std::vector<VtxStream::V> resTmp_;
    bool resReset_ = false;   // arena cheia: zera no comeco do proximo drawList (com a fila vazia)
    // Desenha o pedaco pela arena (true) ou deixa para o caminho comum (false: chave nova, arena cheia, WOTM_RESIDENT=0)
    bool residentDraw(const EmitIn &in, const std::shared_ptr<const MeshData> &keep) {
        // WOTM_RESIDENT=0 desliga; =2 poe na arena ja na primeira vez (replay e corpus, que nao repetem redesenhos)
        static const int mode = [] { const char *e = std::getenv("WOTM_RESIDENT"); return e ? std::atoi(e) : 1; }();
        static const bool diag = std::getenv("WOTM_IDCOLOR") || std::getenv("WOTM_NOVCOL") || std::getenv("WOTM_SUBPKT") || std::getenv("WOTM_UVNEG");
        if (mode == 0 || diag) return false;
        ResKey k;
        std::memset(&k, 0, sizeof k);
        const DrawItem &it = *in.it;
        k.md = in.md; k.sig = in.md->sig; k.node = it.node; k.slot = in.slot; k.mode = in.mode;
        std::memcpy(k.m, &it.m, sizeof k.m);
        std::memcpy(k.color, it.color, sizeof k.color);
        k.fade = in.fade; k.fogBurn = in.fogBurn; k.uvOff[0] = it.uvOff[0]; k.uvOff[1] = it.uvOff[1];
        k.lit = in.ol.lit; k.forceOpaque = in.forceOpaque; k.premul = in.premul; k.premulShader = g_premulShader;
        k.texOn = in.tid != 0; k.multi = in.multi;
        if (in.ol.pt) {
            const DrawItem &p = *in.ol.pt;
            k.ptN = p.ptN;
            std::memcpy(k.ptRel, p.ptRel, sizeof k.ptRel);
            std::memcpy(k.ptCol, p.ptCol, sizeof k.ptCol);
            std::memcpy(k.ptFall, p.ptFall, sizeof k.ptFall);
        } else if (in.ol.lit) {
            k.spec = in.ol.spec;
            if (in.ol.obj) { k.ownLight = 1; std::memcpy(k.lobj, in.ol.obj, sizeof k.lobj); }
            else { std::memcpy(k.ldir, lights_.dir, sizeof k.ldir); std::memcpy(k.lcol, lights_.col, sizeof k.lcol); }
        }
        static_assert(sizeof(ResKey) % 8 == 0, "ResKey em palavras de 8 bytes");
        uint64_t h = 0x243F6A8885A308D3ull;   // de 8 em 8 bytes
        for (size_t i = 0; i < sizeof k; i += 8) {
            uint64_t w;
            std::memcpy(&w, reinterpret_cast<const uint8_t *>(&k) + i, 8);
            h = (h ^ w) * 0x9E3779B97F4A7C15ull;
            h ^= h >> 29;
        }
        auto f = resChunks_.find(h);
        if (f == resChunks_.end() || std::memcmp(&f->second.key, &k, sizeof k) != 0) {
            auto s = resSeen_.find(h);
            if (mode != 2 && (s == resSeen_.end() || s->second == redrawId)) {   // primeira vez (ou so neste redesenho): caminho comum
                if (s == resSeen_.end()) {
                    if (resSeen_.size() > 200000) resSeen_.clear();
                    resSeen_.emplace(h, redrawId);
                }
                return false;
            }
            resTmp_.clear();
            ResChunk c;
            c.key = k;
            c.keep = keep;
            EmitIn ein = in;
            ein.gpuCull = true;
            ResSink sink(resTmp_, c.runs);
            emitVerts(Ram{}, ein, EnvView{}, SpriteView{}, sink);
            c.off = vs_.arenaAlloc(resTmp_.size());
            if (c.off < 0) { resReset_ = true; return false; }
            vs_.arenaUpload(c.off, resTmp_.data(), resTmp_.size());
            if (drawProfOn()) drawProf().resUp += long(resTmp_.size());
            f = resChunks_.insert_or_assign(h, std::move(c)).first;
        }
        for (const ResRun &r : f->second.runs) {
            vs_.resident(f->second.off + long(r.start), int(r.n), r.cull);
            if (drawProfOn()) drawProf().resident += long(r.n);
        }
        return true;
    }
    // Vertices de um pedaco do drawList (triangulos do slot e sprites do objeto). So le: roda nas threads auxiliares (JobPool)
    template <class Sink> void emitVerts(const Ram &ram, const EmitIn &in, const EnvView &envV, const SpriteView &sprView, Sink &sink) const {
        const DrawItem &it = *in.it;
        const MeshData *md = in.md;
        const auto &t = md->tris;
        const uint32_t slot = in.slot;
        const M4 *pm = in.pm;
        const M4 &skinM = in.skinM;
        const bool wire = in.wire, gsA = in.gsA;
        const unsigned tid = in.tid;
        static const bool idcMode = std::getenv("WOTM_IDCOLOR") != nullptr;   // uma vez: getenv por vertice e lento no Windows
        static const bool idcNode = idcMode && std::atoi(std::getenv("WOTM_IDCOLOR")) == 2;   // =2: no >> 4 em RGB, sem mistura nem neblina
        static const bool noVcol = std::getenv("WOTM_NOVCOL") != nullptr;
        static const int onlySub = std::getenv("WOTM_SUBPKT") ? std::atoi(std::getenv("WOTM_SUBPKT")) : -1;
        static const char *un = std::getenv("WOTM_UVNEG");   // diagnostico: WOTM_UVNEG=u|v|uv inverte o sinal do UV
        for (size_t k = 0; k + 2 < t.size(); k += 3) {
            if (in.multi && t[k].slot != slot) continue;   // so as secoes deste slot
            if (onlySub >= 0 && md->skinned && t[k].sub != onlySub) continue;
            float p[3][3];
            for (int c = 0; c < 3; ++c) {
                if (pm) skinVertexPM(pm, skinM, t[k + c], p[c]);
                else xform(it.m, t[k + c].x, t[k + c].y, t[k + c].z, p[c]);
                if (in.fudge) for (int a = 0; a < 3; ++a) p[c][a] = eye_[a] + (p[c][a] - eye_[a]) * in.fzScale;
            }
            if (t[k].cull && !wire && !in.gpuCull && backFacing(p, eye_)) continue;
            sink.tri(t[k].cull != 0);
            float eu[3], ev[3];   // UV do mapa de ambiente por vertice
            bool envOk[3] = {false, false, false};
            if (it.env && tid) {
                for (int c = 0; c < 3; ++c) {
                    float nw[3];
                    envNormal(t[k + c], pm ? &pm[(t[k + c].cx & 0x7F) / 4] : nullptr, pm ? skinM : it.m, nw);
                    envOk[c] = envUv(it.env, it.envK, envV, p[c], nw, eu[c], ev[c]);
                }
            }
            for (int c = 0; c < 3; ++c) {
                const int cs[2] = {c, (c + 1) % 3};
                for (int e = 0; e < (wire ? 2 : 1); ++e) {
                    const Vtx &v = t[k + cs[e]];
                    bool gsV = gsA;   // modo GS do shader neste vertice (as cores de depuracao vao sem ele)
                    if (idcNode) { gsV = false; const uint32_t n = it.node >> 4; sink.color(uint8_t(n), uint8_t(n >> 8), uint8_t(n >> 16), 255); }
                    else if (idcMode) { gsV = false; const uint32_t tx = objectTexId(ram, it.node, slot); sink.color(uint8_t(tx & 255), uint8_t(tx >> 8), 128, 255); }
                    else if (v.isAdc) { gsV = false; sink.color(255, 0, 0, 255); }   // WOTM_SHOWADC
                    else if (noVcol && md->skinned) { gsV = false; sink.color(255, 255, 255, 255); }   // diagnostico
                    else {
                        uint8_t rgb[3];
                        vertexRgb(in.ol, it.color, pm ? &pm[(v.cx & 0x7F) / 4] : nullptr, pm ? skinM : it.m, v, rgb, in.rowLen);
                        // cor e alfa crus no modo GS (o shader faz texel * Cv / 128 e At * Av / 16384); no quadro anterior e no filme,
                        // o antigo (cor dobrada e limitada a 1,0, alfa 4a/255)
                        const float va = v.a * in.fade * (gsA ? 1.f : 4.f);
                        const int kk = gsA ? 1 : 2;
                        sink.color(uint8_t(std::min(255, rgb[0] * kk)), uint8_t(std::min(255, rgb[1] * kk)), uint8_t(std::min(255, rgb[2] * kk)),
                                   in.forceOpaque ? uint8_t(255) : uint8_t(std::min(255.f, va)));
                    }
                    const float tu = envOk[cs[e]] ? eu[cs[e]] : v.u + it.uvOff[0], tv = envOk[cs[e]] ? ev[cs[e]] : v.v + it.uvOff[1];
                    // texels do GS na tela 640x224, que em widescreen e a faixa 4:3 central do quadro anterior (gsScreenRect)
                    if (tid && tid == screenId_) sink.tex(0.5f - 0.5f / wide + tu * (1024.f / 640.f) / wide, 1.f - tv * (256.f / 224.f));
                    else if (tid && envOk[cs[e]]) sink.tex(tu, tv);
                    else if (tid) { const float su = (un && std::strchr(un, 'u')) ? -1.f : 1.f, sv = (un && std::strchr(un, 'v')) ? -1.f : 1.f; sink.tex(su * (v.u + it.uvOff[0]), sv * (v.v + it.uvOff[1])); }
                    sink.shaderN(in.fogBurn, v.fge && !idcNode, gsV, in.premul, in.mode);   // neblina e modulacao do GS no shader do draw
                    sink.vertex(p[cs[e]][0], p[cs[e]][1], p[cs[e]][2]);
                }
            }
        }
        for (const ObjSprite &s : md->sprites) {
            if (in.multi && s.slot != slot) continue;
            float c[4][3];
            if (!objSpriteCorners(sprView, it.m, s, c)) continue;
            if (in.fudge) for (auto &q : c) for (int a = 0; a < 3; ++a) q[a] = eye_[a] + (q[a] - eye_[a]) * in.fzScale;
            const uint8_t cr = uint8_t(std::min(255.f, s.r * it.color[0])), cg = uint8_t(std::min(255.f, s.g * it.color[1])),
                          cb = uint8_t(std::min(255.f, s.b * it.color[2])), ca = uint8_t(std::min(255.f, s.a * in.fade));
            // ST como o GS (repete): desloca o par para >= 0, para nao depender do modo de repeticao da textura do raylib
            const float u0 = s.u0 + it.uvOff[0], u1 = s.u1 + it.uvOff[0], v0 = s.v0 + it.uvOff[1], v1 = s.v1 + it.uvOff[1];
            const float du = std::floor(std::min(u0, u1)), dv = std::floor(std::min(v0, v1));
            const float cu[4] = {u0 - du, u1 - du, u1 - du, u0 - du}, cv[4] = {v0 - dv, v0 - dv, v1 - dv, v1 - dv};
            static const int triIdx[6] = {0, 1, 2, 0, 2, 3}, edgeIdx[8] = {0, 1, 1, 2, 2, 3, 3, 0};
            const int *ix = wire ? edgeIdx : triIdx;
            for (int q = 0; q < (wire ? 8 : 6); ++q) {
                const int k = ix[q];
                sink.color(cr, cg, cb, ca);
                if (tid) sink.tex(cu[k], cv[k]);
                sink.shaderN(in.fogBurn, s.fge, gsA, in.premul, in.mode);
                sink.vertex(c[k][0], c[k][1], c[k][2]);
            }
        }
    }
    // O programa do mapa de ambiente (DrawItem::env) nao aplica a luz paralela, mesmo com dirLight. O pktAddVu1ObjAsm escolhe o programa
    // das luzes pontuais (0x15D8, tabela +0x16C) antes do da luz paralela: com luz pontual, a paralela nao entra.
    static ObjLight objectLight(const Ram &ram, const DrawItem &it) {
        const uint16_t f = ram.u16(it.node + 0xC);
        if (it.env) return {};
        if (it.ptN) return {false, 0.f, &it};
        return {(f & 0x100) != 0, it.spec, nullptr, it.ownLight ? it.lightObj : nullptr};
    }

    // Cor com luzes pontuais, como o programa 0x15D8 do VU1: p = posicao do vertice no espaco do objeto, n = normal crua / 128, e para
    // cada luz k, d_k = p + ptRel_k (vertice menos luz) e
    //   f_k = min(0,5; max(0, -d_k . n) * falloff_k / |d_k|^2)
    //   cor = min(255, cor do vertice * vf31 + soma de cor_k * f_k)
    // Sem luz paralela nem especular.
    static void pointRgb(const DrawItem &it, const float vc[3], const Vtx &v, uint8_t out[3]) {
        const float n[3] = {float(v.nx) / 128.f, float(v.ny) / 128.f, float(v.nz) / 128.f};
        const float p[3] = {v.x, v.y, v.z};
        float acc[3] = {vc[0], vc[1], vc[2]};
        for (int k = 0; k < it.ptN; ++k) {
            float d[3], d2 = 0.f, dot = 0.f;
            for (int a = 0; a < 3; ++a) {
                d[a] = p[a] + it.ptRel[k][a];
                d2 += d[a] * d[a];
                dot -= d[a] * n[a];
            }
            if (!(d2 > 0.f)) continue;
            const float fk = std::min(0.5f, std::max(0.f, dot) * it.ptFall[k] / d2);
            for (int c = 0; c < 3; ++c) acc[c] += it.ptCol[k][c] * fk;
        }
        for (int c = 0; c < 3; ++c) out[c] = uint8_t(std::min(255.f, std::max(0.f, acc[c])));
    }

    // Cor do vertice nas unidades do GS (0x80 = 1,0): a do pacote vezes o multiplicador do objeto (DrawItem::color, vf31) ou, nos
    // iluminados, a luz do VU1. bone: osso x do vertice (skinned) ou nulo.
    // Comprimento de cada linha 0-2 de m (o vertexRgb normaliza as linhas com ele): por item, fora do laco dos vertices
    static void rowLengths(const M4 &m, float len[3]) {
        for (int r = 0; r < 3; ++r) len[r] = std::sqrt(m.m[r][0] * m.m[r][0] + m.m[r][1] * m.m[r][1] + m.m[r][2] * m.m[r][2]);
    }
    // rowLen: rowLengths(m) ja calculado (mesmo resultado; sem ele, calcula aqui)
    void vertexRgb(const ObjLight &ol, const float mul[4], const M4 *bone, const M4 &m, const Vtx &v, uint8_t out[3], const float *rowLen = nullptr) const {
        const float vc[3] = {v.r * mul[0], v.g * mul[1], v.b * mul[2]};
        if (ol.pt) {
            pointRgb(*ol.pt, vc, v, out);
            return;
        }
        if (!ol.lit) {
            for (int c = 0; c < 3; ++c) out[c] = uint8_t(std::min(255.f, std::max(0.f, vc[c])));
            return;
        }
        float n[3] = {float(v.nx), float(v.ny), float(v.nz)};
        if (bone) {   // o laco de skinning do VU1 (0x3D0) grava a normal girada com FTOI12 (trunca a 1/4096) e a luz le com ITOF12
            const float b[3] = {n[0], n[1], n[2]};
            for (int c = 0; c < 3; ++c) n[c] = std::trunc(b[0] * bone->m[0][c] + b[1] * bone->m[1][c] + b[2] * bone->m[2][c]);
        }
        float d[4];
        if (ol.obj) {   // luzes ja no espaco do objeto (abaixo de um SCALE: Scene::lightScale), contra a normal do objeto
            for (int i = 0; i < 4; ++i) d[i] = std::max(0.f, (n[0] * ol.obj[i][0] + n[1] * ol.obj[i][1] + n[2] * ol.obj[i][2]) / 4096.f);
        } else {
            float w[3] = {0, 0, 0};   // no mundo, com as linhas do objeto normalizadas (sem SCALE no caminho, a matriz e uma rotacao)
            for (int r = 0; r < 3; ++r) {
                const float len = rowLen ? rowLen[r] : std::sqrt(m.m[r][0] * m.m[r][0] + m.m[r][1] * m.m[r][1] + m.m[r][2] * m.m[r][2]);
                if (len > 1e-6f)
                    for (int c = 0; c < 3; ++c) w[c] += n[r] * m.m[r][c] / len;
            }
            for (int i = 0; i < 4; ++i) d[i] = std::max(0.f, (w[0] * lights_.dir[i][0] + w[1] * lights_.dir[i][1] + w[2] * lights_.dir[i][2]) / 4096.f);
        }
        float s = d[3];
        for (int k = 0; k < 4; ++k) s *= s;   // ^16
        for (int c = 0; c < 3; ++c) {
            const float x = vc[c] * (lights_.col[0][c] * d[0] + lights_.col[1][c] * d[1] + lights_.col[2][c] * d[2]) + ol.spec * s;
            out[c] = uint8_t(std::min(255.f, std::max(0.f, x)));
        }
    }

    // Skinning, como o microcodigo do VU1 (desmontado com tools/vu1dis.py; laco em 0x3C8): tres ossos por vertice.
    // Os codigos de osso (8 bits baixos de x, y, z; & 0x7F = endereco de qword da matriz na paleta) escolhem 3 matrizes e os pesos
    // saem do float w: w1 = w, w2 = frac(w * 2048) (mantissa baixa), w3 = 1 - w1 - w2. pos = w1*Mx*p + w2*My*p + w3*Mz*p.
    // Slot da paleta = (codigo & 0x7F) / 4. A normal so gira pelo osso x (vertexRgb).
    static void skinVertexPM(const M4 *pm, const M4 &m, const Vtx &v, float out[3]) {
        float w1 = v.w, w2 = 0.f, w3 = 0.f;
        { const float s = v.w * 2048.f; w2 = s - std::trunc(s); w3 = 1.f - w1 - w2; }
        const uint8_t codes[3] = {v.cx, v.cy, v.cz};
        const float ws[3] = {w1, w2, w3};
        float q[3] = {0, 0, 0};
        for (int k = 0; k < 3; ++k) {
            if (ws[k] == 0.f) continue;
            float a[3];
            xform(pm[(codes[k] & 0x7F) / 4], v.x, v.y, v.z, a);
            for (int c = 0; c < 3; ++c) q[c] += a[c] * ws[k];
        }
        xform(m, q[0], q[1], q[2], out);
    }

    static void skinVertex(const Ram &ram, uint32_t pal, const M4 &m, const Vtx &v, float out[3]) {
        float w1 = v.w, w2 = 0.f, w3 = 0.f;
        {   // 3 ossos: w1 = w, w2 = frac(w * 2048), w3 = o resto (microcodigo do VU1)
            const float s = v.w * 2048.f;
            w2 = s - std::trunc(s);
            w3 = 1.f - w1 - w2;
        }
        const uint8_t codes[3] = {v.cx, v.cy, v.cz};
        const float ws[3] = {w1, w2, w3};
        float q[3] = {0, 0, 0};
        for (int k = 0; k < 3; ++k) {
            if (ws[k] == 0.f) continue;
            float a[3];
            xform(readM4(ram, pal + 0x10 + 0x40 * ((codes[k] & 0x7F) / 4)), v.x, v.y, v.z, a);
            for (int c = 0; c < 3; ++c) q[c] += a[c] * ws[k];
        }
        xform(m, q[0], q[1], q[2], out);
    }

    // ---- CONTROL (hierCtrlNode, 0x2048F0): predios com versao "transparente" quando bloqueiam a visao do monstro ----
    // No: +4 child1, +8 child2, +0xC numVolumes, +0xE volType, +0x10 esfera (centro, raio em +0x1C), +0x20 volumes BSP (ponteiros).
    // volType 0: se o olho NAO esta no ar (folha != 0x200) em algum volume -> child2 (camera dentro do predio). volType != 0: para cada
    // ponto de visada (g_cameraLosPoints: direcao unitaria da camera ate o monstro + distancia em w) o segmento olho->ponto nao pode
    // atravessar solido (folha != 0x200) de nenhum volume; se atravessar -> child2 (que pode ser nulo = o predio some).
    static constexpr uint32_t kLosPoints = wotm::sym::g_cameraLosPoints, kLosPerView = wotm::sym::g_cameraLosPointsPerView;

    static void toLocal(const M4 &m, const float w[3], float out[3]) {
        for (int c = 0; c < 3; ++c) {
            const float *r = m.m[c];
            const float n2 = r[0] * r[0] + r[1] * r[1] + r[2] * r[2];
            out[c] = n2 > 1e-9f ? ((w[0] - m.m[3][0]) * r[0] + (w[1] - m.m[3][1]) * r[1] + (w[2] - m.m[3][2]) * r[2]) / n2 : 0.f;
        }
    }

    // Folha onde o ponto cai (0x200 = ar), igual a bspPointTest.
    static int bspPoint(const Ram &ram, uint32_t b, const float p[3]) {
        int ret = 0x200;
        for (int guard = 0; b && ram.ok(b, 32) && guard < 64; ++guard) {
            const float d = ram.f32(b + 0x10) * p[0] + ram.f32(b + 0x14) * p[1] + ram.f32(b + 0x18) * p[2] - ram.f32(b + 0x1C);
            const bool neg = d < 0.f;
            ret = neg ? ram.u16(b + 0xE) : ram.u16(b + 0xC);
            b = neg ? ram.u32(b + 4) : ram.u32(b);
        }
        return ret;
    }

    // Verdadeiro se o segmento a->c atravessa alguma folha que nao seja ar.
    static bool bspSegHit(const Ram &ram, uint32_t b, const float a[3], const float c[3], int depth = 0) {
        if (depth > 64) return false;
        if (!b || !ram.ok(b, 32)) return false;
        const float n[3] = {ram.f32(b + 0x10), ram.f32(b + 0x14), ram.f32(b + 0x18)}, dd = ram.f32(b + 0x1C);
        const float d0 = n[0] * a[0] + n[1] * a[1] + n[2] * a[2] - dd, d1 = n[0] * c[0] + n[1] * c[1] + n[2] * c[2] - dd;
        const uint32_t front = ram.u32(b), back = ram.u32(b + 4);
        auto side = [&](bool neg, const float p[3], const float q[3]) -> bool {
            const uint32_t kid = neg ? back : front;
            if (kid) return bspSegHit(ram, kid, p, q, depth + 1);
            return (neg ? ram.u16(b + 0xE) : ram.u16(b + 0xC)) != 0x200;   // folha
        };
        if (d0 >= 0.f && d1 >= 0.f) return side(false, a, c);
        if (d0 < 0.f && d1 < 0.f) return side(true, a, c);
        const float t = d0 / (d0 - d1);
        const float mid[3] = {a[0] + (c[0] - a[0]) * t, a[1] + (c[1] - a[1]) * t, a[2] + (c[2] - a[2]) * t};
        return d0 >= 0.f ? (side(false, a, mid) || side(true, mid, c)) : (side(true, a, mid) || side(false, mid, c));
    }

    uint32_t ctrlChild(const Ram &ram, uint32_t node, const M4 &m) const {
        const uint32_t c1 = ram.u32(node + 4), c2 = ram.u32(node + 8);
        if (!eyeOk_) return c1;
        const uint32_t nvol = std::min<uint32_t>(ram.u8(node + 0xC), 16), vtype = ram.u8(node + 0xE);
        if (!nvol || !ram.ok(node, 0x20 + 4 * nvol)) return c1;
        float eyeL[3];
        toLocal(m, eye_, eyeL);
        bool visible = true;
        if (vtype == 0) {
            for (uint32_t i = 0; i < nvol && visible; ++i)
                if (bspPoint(ram, ram.u32(node + 0x20 + 4 * i), eyeL) != 0x200) visible = false;
        } else {
            const uint32_t v = uint32_t(curView().view);
            const int nlos = std::min<int>(int(ram.u32(kLosPerView + 4 * v)), 2);
            for (int k = 0; k < nlos && visible; ++k) {
                const uint32_t pa = kLosPoints + 0x20 * v + 16 * k;
                const float len = ram.f32(pa + 12);
                if (!(len > 0.f) || len > 1e6f) continue;
                const float pw[3] = {eye_[0] + ram.f32(pa) * len, eye_[1] + ram.f32(pa + 4) * len, eye_[2] + ram.f32(pa + 8) * len};
                float pl[3];
                toLocal(m, pw, pl);
                for (uint32_t i = 0; i < nvol && visible; ++i)
                    if (bspSegHit(ram, ram.u32(node + 0x20 + 4 * i), eyeL, pl)) visible = false;
            }
        }
        return visible ? c1 : c2;
    }

    // Corte por esfera do hierTraverseAsm: o programa 0x0 do VU0 testa a esfera do no (+0x10 centro, +0x1C raio) contra os planos
    // da vista (Particles::inViewVolume); fora de algum plano, o OBJECT nao vai ao VU1 (a nao ser com nofov, bit 0 de +0xC) e o GROUP
    // pula os filhos (com g_doFOVGroups e sem noFOV). O jogo testa no espaco do no com os planos de colunas normalizadas; em mundo o
    // raio vai pela maior escala da matriz.
    bool outOfView(const Ram &ram, uint32_t node, const M4 &m) const {
        if (!fovCull) return false;
        float c[3], s2 = 0.f;
        xform(m, ram.f32(node + 0x10), ram.f32(node + 0x14), ram.f32(node + 0x18), c);
        for (int i = 0; i < 3; ++i) s2 = std::max(s2, m.m[i][0] * m.m[i][0] + m.m[i][1] * m.m[i][1] + m.m[i][2] * m.m[i][2]);
        static constexpr float kNoBox[3] = {0.f, 0.f, 0.f};
        return !Particles::inViewVolume(ram, c, kNoBox, ram.f32(node + 0x1C) * std::sqrt(s2) * cullMargin, wide);
    }

    bool already(uint32_t n) {
        // lista pequena e ordenada por insercao; o jogo tem ate alguns milhares de nos
        for (uint32_t s : seen_) if (s == n) return true;
        seen_.push_back(n);
        return false;
    }

    void kids(const Ram &ram, uint32_t arr, uint32_t n, const M4 &m, int depth) {
        if (n > 4096) n = 4096;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t c = ram.u32(arr + 4 * i);
            if (ram.ok(c)) walk(ram, c, m, depth + 1);
        }
    }

    void walk(const Ram &ram, uint32_t node, const M4 &m, int depth) {
        if (depth > 64 || visited > 200000 || !ram.ok(node)) return;
        // O mesmo no pode aparecer com matrizes diferentes (instancias): so corta ciclos profundos
        if (depth > 24 && already(node)) return;
        ++visited;
        const uint32_t op = ram.u32(node) & 0x3F;
        ++byOp[op];
        switch (op) {
        case 0:                                                                                                 // OBJECT
            if (!(ram.u16(node + 0xC) & 1) && shadowLodD2_ < 0.f && outOfView(ram, node, m)) { ++culled; break; }
            items.push_back({m, node, animPkt_, lodIdx_, nullptr, blend_, fade_});
            items.back().cs = cs_;
            // O pktAddVu1ObjAsm so grava o qword 20 (especular, specIntensity +0x6C) com specLightMe (0x2000); sem ele o programa de
            // luz le o que o ultimo objeto deixou na memoria do VU1. As partes do Togera sem o bit (corpo skinned, flags 0x310) herdam
            // o especular da parte anterior do proprio monstro (checagem: ~10% dos vertices dele saiam mais escuros, so no cinza).
            if (ram.u16(node + 0xC) & 0x2000) spec_ = ram.f32(node + 0x6C);
            items.back().spec = spec_;
            if (lightOwn_) {
                items.back().ownLight = true;
                std::memcpy(items.back().lightObj, lightObj_, sizeof lightObj_);
            }
            items.back().aref = aref_;
            items.back().zmsk = zmsk_;
            items.back().lodX = lodX_;
            objectUvOffset(ram, node, items.back().uvOff);
            objectColor(ram, node, items.back().color);
            objectEnv(ram, node, items.back());
            objectPointLights(ram, node, items.back());
            break;
        case 1: {                                                                           // GROUP (ctx em +4 = estado do GS: ALPHA_1 e TEST_1 dos filhos)
            const uint16_t gflags = ram.u16(node + 0xA);                                    // visual (bit 0), noFOV (bit 2)
            if (!(gflags & 1)) break;
            if (!(gflags & 4) && ram.u32(addr::g_doFOVGroups) && shadowLodD2_ < 0.f && outOfView(ram, node, m)) { ++culled; break; }
            const CtxScope cs(*this, ram, ram.u32(node + 4));
            kids(ram, node + 0x20, ram.u16(node + 8), m, depth);
            break;
        }
        case 2: {                                                                           // LOD
            const uint32_t n = ram.u32(node + 4);
            if (n > 0 && n < 16) {
                int pick = -1;
                if (lod_ >= 0) pick = int(std::min<uint32_t>(uint32_t(lod_), n - 1));
                else {
                    // hierLod: o MAIOR indice cujo switchOutDis (distancia ao quadrado) > distancia ao centro do LOD; nenhum = nao desenha.
                    // Na travessia da sombra (collectShadows) a distancia e a fixa do bloqueador (shadowLodRangeSq do jogo)
                    float d2 = 0.f;
                    if (shadowLodD2_ >= 0.f) d2 = shadowLodD2_;
                    else if (eyeOk_) {
                        float c[3];
                        xform(m, ram.f32(node + 0x10), ram.f32(node + 0x14), ram.f32(node + 0x18), c);
                        d2 = (c[0] - eye_[0]) * (c[0] - eye_[0]) + (c[1] - eye_[1]) * (c[1] - eye_[1]) + (c[2] - eye_[2]) * (c[2] - eye_[2]);
                    }
                    for (int k = int(n) - 1; k >= 0; --k)
                        if (d2 < ram.f32(node + 0x20 + 16 * k + 4)) { pick = k; break; }
                }
                if (pick < 0) break;
                const uint32_t i = uint32_t(pick);
                const uint32_t c = ram.u32(node + 0x20 + 16 * i + 8);
                const int savedLod = lodIdx_;
                const float savedFade = fade_;
                // Cross-fade (hierLod): passada a distancia `fade` (+0x20 + 16i, ao quadrado como switchOutDis), o jogo desenha o LOD i e
                // o i - 1 juntos, com t = 1 - (d - fade) / (switchOutDis - fade): t > 0,5 -> i inteiro e i - 1 com alfa (1 - t) * 2;
                // senao i - 1 inteiro e i com alfa 2t; sem i - 1, i com alfa t. Sem isso o nativo trocava de LOD de uma vez (checagem
                // VU1, pista 7: o VU1 desenhava uma instancia "a mais", o LOD vizinho no mesmo lugar).
                float fi = 1.f, fprev = 0.f;
                const float fd = lod_ < 0 && eyeOk_ && shadowLodD2_ < 0.f ? ram.f32(node + 0x20 + 16 * i) : 0.f;
                const bool savedX = lodX_;
                if (fd > 0.f) {
                    float d2 = 0.f, cc[3];
                    xform(m, ram.f32(node + 0x10), ram.f32(node + 0x14), ram.f32(node + 0x18), cc);
                    for (int a = 0; a < 3; ++a) d2 += (cc[a] - eye_[a]) * (cc[a] - eye_[a]);
                    if (fd < d2) {
                        const float t = 1.f - (d2 - fd) / (ram.f32(node + 0x20 + 16 * i + 4) - fd);
                        if (i >= 1) { if (t > 0.5f) fprev = (1.f - t) * 2.f; else { fprev = 1.f; fi = t * 2.f; } }
                        else fi = t;
                        lodX_ = true;   // o hierTraverseAsm guarda o peso (!= 0) e manda os OBJECTs 0x4000 para a OT de alfa
                    }
                }
                // ordem do hierTraverseAsm: no cross-fade ele segue direto o filho do LOD i - 1 e empilha o do LOD i (com o peso),
                // que sai depois. Com os translucidos gravando Z na travessia, o LOD i quase transparente na frente fazia o i - 1
                // solido falhar no teste (luta_hud: predio d51400 sob o d51c40 de fade 0,21, o horizonte aparecia)
                if (fprev > 0.f) {
                    const uint32_t c2 = ram.u32(node + 0x20 + 16 * (i - 1) + 8);
                    lodIdx_ = int(i - 1);
                    fade_ = savedFade * fprev;
                    if (ram.ok(c2)) walk(ram, c2, m, depth + 1);
                }
                lodIdx_ = int(i);
                fade_ = savedFade * fi;
                if (ram.ok(c) && fade_ > 0.f) walk(ram, c, m, depth + 1);
                fade_ = savedFade;
                lodX_ = savedX;
                lodIdx_ = savedLod;
            }
            break;
        }
        case 3: {                                                                           // TRANSLATE
            const CtxScope cs(*this, ram, ram.u32(node + 4));
            kids(ram, node + 0x1C, ram.u32(node + 8),
                 mul(translateM(ram.f32(node + 0x10), ram.f32(node + 0x14), ram.f32(node + 0x18)), m), depth);
            break;
        }
        case 4: {                                                                           // ROTATE
            const CtxScope cs(*this, ram, ram.u32(node + 4));
            const LightScope ls(*this);
            const M4 local = readM4(ram, node + 0x10);
            lightRotate(local);
            kids(ram, node + 0x50, ram.u32(node + 8), mul(local, m), depth);
            break;
        }
        case 30: {                                                                          // SCALE
            const CtxScope cs(*this, ram, ram.u32(node + 4));
            const LightScope ls(*this);
            const float sx = ram.f32(node + 0x20), sy = ram.f32(node + 0x24), sz = ram.f32(node + 0x28);
            if (ram.u32(node + 0xC) == 0) lightScale(m, sx, sy, sz);   // hierScaleNode: com +0xC != 0 copia a tabela do pai
            kids(ram, node + 0x2C, ram.u32(node + 8),
                 mul(mul(scaleM(sx, sy, sz), translateM(ram.f32(node + 0x10), ram.f32(node + 0x14), ram.f32(node + 0x18))), m), depth);
            break;
        }
        case 6: {                                                                           // SWITCH
            const uint32_t which = ram.u8(node + 8), n = ram.u8(node + 0xB);
            if (which < n) { const uint32_t c = ram.u32(node + 0xC + 4 * which); if (ram.ok(c)) walk(ram, c, m, depth + 1); }
            break;
        }
        case 8: {   // CONTROL: child1 (predio solido) ou child2 (versao "transparente"/ausente) conforme o volume bloqueia a visao do monstro
            const uint32_t c = ctrlChild(ram, node, m);
            if (c && ram.ok(c)) walk(ram, c, m, depth + 1);
            break;
        }
        case 23: { const uint32_t c = ram.u32(node + 4); if (ram.ok(c)) walk(ram, c, m, depth + 1); break; } // ACTION_DATA
        case 35: kids(ram, node + 0xC, ram.u32(node + 4), m, depth); break;                 // contêiner de ANIM_XFORM (partes rigidas do monstro: maos, cabeca...): contagem em +4, filhos em +0xC (dbsWalk caso 35)
        case 25: {                                                                          // CHAR_INSTANCE
            // As partes do corpo sao nos ANIM_XFORM (hierarquia rigida, sem skinning). O proprio jogo calcula a matriz local de
            // cada no a cada quadro (hierAnimTransNode) em animOutput.atMat[matrixIdx]; basta le-la.
            const uint32_t saved = atMat_, savedPkt = animPkt_, savedVal = animVal_;
            atMat_ = ram.u32(node + 0xC);
            animPkt_ = ram.u32(node + 0x18);
            animVal_ = ram.u32(node + 4);
            kids(ram, node + 0x2C, ram.u32(node + 0x28), m, depth);
            atMat_ = saved;
            animPkt_ = savedPkt;
            animVal_ = savedVal;
            break;
        }
        case 17: {                                                                          // ANIM_XFORM
            if (ram.f32(node + 0xC) == 0.f) break;                                          // visible
            M4 local = ident();
            const uint32_t idx = ram.u32(node + 0xC4);
            if (atMat_ && idx < 4096 && ram.ok(atMat_ + idx * 64, 64)) local = readM4(ram, atMat_ + idx * 64);
            local.m[0][3] = local.m[1][3] = local.m[2][3] = 0.f; local.m[3][3] = 1.f;
            const CtxScope cs(*this, ram, ram.u32(node + 8));
            const LightScope ls(*this);
            lightRotate(local);
            kids(ram, node + 0xCC, ram.u32(node + 4), mul(local, m), depth);
            break;
        }
        case 38: {                                                                          // ANIM_SCALE: escala e depois translacao
            {   // hierAnimScaleNode nao grava +0xC: a visibilidade e o canal val[vidx] da saida de animacao do CHAR_INSTANCE (-1 = sempre visivel)
                const int vidx = int16_t(ram.u16(node + 0x10));
                const float vis = (vidx >= 0 && animVal_ && vidx < 4096) ? ram.f32(animVal_ + 4 * uint32_t(vidx)) : (vidx >= 0 ? ram.f32(node + 0xC) : 1.f);
                if (vis == 0.f) break;
            }
            // hierAnimScaleNode: o olho vai ao espaco do filho por (eo + translate + postScale) / scale + preScale, ou seja, o ponto do
            // filho p vai ao pai em scale * (p + preScale) + translate + postScale (preScale +0x40 e postScale +0x50; ignora-los
            // deslocava itens presos aos monstros, checagem VU1 pista 7).
            // Escala zero achata o filho: o jogo nao desenha nada (feixes de luz da arvore do Plantboss, tex 297, com o canal da
            // escala em 0 fora do golpe; o nativo trocava 0 por 1 e os desenhava inteiros, foto 2026-10-19_004009/002).
            float sx = ram.f32(node + 0x20), sy = ram.f32(node + 0x24), sz = ram.f32(node + 0x28);
            if (sx == 0.f && sy == 0.f && sz == 0.f) break;
            const M4 pre = translateM(ram.f32(node + 0x40), ram.f32(node + 0x44), ram.f32(node + 0x48));
            const M4 post = translateM(ram.f32(node + 0x30) + ram.f32(node + 0x50), ram.f32(node + 0x34) + ram.f32(node + 0x54),
                                       ram.f32(node + 0x38) + ram.f32(node + 0x58));
            const CtxScope cs(*this, ram, ram.u32(node + 8));
            const LightScope ls(*this);
            if (std::fabs(sx - sy) >= 0.001f || std::fabs(sx - sz) >= 0.001f) lightScale(m, sx, sy, sz);   // uniforme: copia a do pai
            kids(ram, node + 0x60, ram.u32(node + 4), mul(mul(mul(pre, scaleM(sx, sy, sz)), post), m), depth);
            break;
        }
        case 39: {                                                                          // INTERACTIVE_STATE (+4 whichChild, +0xC filho)
            // O filho de um destrutivel e um DESTRUCTIBLE: o jogo escolhe o estado (0 = intacto, depois danificado/ruina...) em
            // `whichChild` deste no pai (docs/FORMATOS.md, "Destrutiveis"); a travessia desce so para child[whichChild].child.
            const uint32_t c = ram.u32(node + 0xC);
            if (!ram.ok(c)) break;
            if ((ram.u32(c) & 63) == 31) {
                const uint32_t n = ram.u16(c + 0xA);
                uint32_t w = ram.u32(node + 4);
                ++destrHist_[std::min<uint32_t>(w, 7)];
                if (w >= n) w = 0;
                if (!n) break;
                const uint32_t ch = ram.u32(c + 0xC + 12 * w);
                if (ram.ok(ch)) walk(ram, ch, m, depth + 1);
            } else walk(ram, c, m, depth + 1);
            break;
        }
        case 5: {                                                                           // LIGHT (HierLight): so as com lens flare
            const uint32_t fl = ram.u32(node + 4);
            if ((fl & 3) != 3 || worldLights.size() >= 256) break;
            // o collectShadows percorre os monstros de novo (silhueta da sombra): a mesma luz no mesmo _cs so conta uma vez
            if (std::any_of(worldLights.begin(), worldLights.end(), [&](const WorldLight &o) { return o.node == node && o.cs == cs_; })) break;
            WorldLight l;
            xform(m, ram.f32(node + 0x10), ram.f32(node + 0x14), ram.f32(node + 0x18), l.p);
            const float bx = ram.f32(node + 0x20), by = ram.f32(node + 0x24), bz = ram.f32(node + 0x28);
            for (int k = 0; k < 3; ++k) l.beam[k] = bx * m.m[0][k] + by * m.m[1][k] + bz * m.m[2][k];
            l.flags = fl & 0xFF;
            l.node = node;
            l.cs = cs_;
            l.id = (ram.u32(node) >> 7) & 0x7FF;   // head.id2 (bits 7..17)
            l.intensity = ram.f32(node + 8);
            for (int k = 0; k < 3; ++k) l.rgb[k] = ram.f32(node + 0x30 + 4 * uint32_t(k));
            l.bw = ram.f32(node + 0x3C);
            worldLights.push_back(l);
            break;
        }
        case 22: {                                                                          // PARTICLE_EMITTER (HierParticleEmitter)
            if (emitters.size() >= 256) break;
            WorldEmitter em;
            em.node = node;
            em.preset = ram.u32(node + 4);
            xform(m, ram.f32(node + 0x10), ram.f32(node + 0x14), ram.f32(node + 0x18), em.e);
            if (std::any_of(emitters.begin(), emitters.end(), [&](const WorldEmitter &o) {   // de novo pelo collectShadows
                    return o.node == node && std::fabs(o.e[0] - em.e[0]) + std::fabs(o.e[1] - em.e[1]) + std::fabs(o.e[2] - em.e[2]) < 0.01f;
                }))
                break;
            const float h = ram.f32(node + 0x24), pt = ram.f32(node + 0x28);
            const float nl[3] = {std::cos(pt) * std::sin(h), std::cos(pt) * std::cos(h), std::sin(pt)};
            float a0[3];   // mathfMatrixFromNormal: com n vertical, linha 0 = +X; no geral, X horizontal perpendicular a n
            if (std::fabs(nl[2]) > 0.999f) { a0[0] = 1.f; a0[1] = 0.f; a0[2] = 0.f; }
            else { const float l = std::sqrt(nl[0] * nl[0] + nl[1] * nl[1]); a0[0] = nl[1] / l; a0[1] = -nl[0] / l; a0[2] = 0.f; }
            const float a2[3] = {a0[1] * nl[2] - a0[2] * nl[1], a0[2] * nl[0] - a0[0] * nl[2], a0[0] * nl[1] - a0[1] * nl[0]};
            for (int k = 0; k < 3; ++k) {
                em.r0[k] = a0[0] * m.m[0][k] + a0[1] * m.m[1][k] + a0[2] * m.m[2][k];
                em.r2[k] = a2[0] * m.m[0][k] + a2[1] * m.m[1][k] + a2[2] * m.m[2][k];
            }
            emitters.push_back(em);
            break;
        }
        case 31: {                                                                          // DESTRUCTIBLE: estado 0 (intacto)
            if (ram.u16(node + 0xA)) { const uint32_t c = ram.u32(node + 0xC); if (ram.ok(c)) walk(ram, c, m, depth + 1); }
            break;
        }
        default: break;   // colisao, luz, particulas, animacao...: proximos marcos
        }
    }

    bool hpList_ = false;   // collect percorrendo CsPool::m_HPActiveList (drawCs manda para `hp`)
    void csList(const Ram &ram, uint32_t head) {
        uint32_t n = ram.u32(head + 4);
        for (int guard = 0; ram.ok(n) && n != head && guard < 4096; ++guard, n = ram.u32(n + 4)) {
            drawCs(ram, ram.u32(n), true);
        }
    }

    // Icones de botao (X, O, quadrado, triangulo) de `McButtons`: ids que `McButtons::initButton` aceita (0x1F51..0x1F54 e 0x240E..0x2411).
    // `drawButtonsForString` liga o `drawMe` e posiciona cada um com `AlignCsWithView` (alguns passos a frente da camera, orientado como ela),
    // entao nao sao HUD de espaco de tela. Ficam em z perto de 0 (podem cair abaixo do chao do cenario): o jogo os desenha por cima.
    static bool isButtonId(uint32_t id) { return (id >= 0x1F51 && id <= 0x1F54) || (id >= 0x240E && id <= 0x2411); }

    // Um `_cs` (instancia com matriz e posicao proprias): epNode em +0, drawMe +0xC, trans +0x10, mat +0x20, scaleMe +0xF, scale +0x80
    void drawCs(const Ram &ram, uint32_t cs, bool needDrawMe, const float *at = nullptr) {
        if (!ram.ok(cs, 0xB0) || (needDrawMe && !ram.u8(cs + 0xC))) return;
        const uint32_t ep = ram.u32(cs);
        if (!ram.ok(ep)) return;
        // id do objeto (14 bits altos); 0x1C20..0x2133 = elementos de HUD. O Hud::update poe o _cs do HUD a cada quadro na frente da camera
        // (trans = olho + R * gHudPos, mat = rotacao da vista), entao ele projeta como o resto; vai para `hud`, desenhado por cima de tudo.
        const uint32_t id = ram.u32(ep) >> 18;
        const bool isHud = needDrawMe && id >= 0x1C20 && id <= 0x2133 && !isButtonId(id);
        M4 m = readM4(ram, cs + 0x20);
        m.m[3][0] = ram.f32(cs + 0x10); m.m[3][1] = ram.f32(cs + 0x14); m.m[3][2] = ram.f32(cs + 0x18); m.m[3][3] = 1.f;
        m.m[0][3] = m.m[1][3] = m.m[2][3] = 0.f;
        if (at) { m.m[3][0] = at[0]; m.m[3][1] = at[1]; m.m[3][2] = at[2]; }
        else if (eyeOk_) {
            // O hier leva o olho ao espaco do objeto com a propria mat (hierCsUpdate: mat * (eo + trans)) e volta com ela, nao com a
            // inversa: o VU1 desenha em olho + (trans - olho) * R^T * R (R = 3x3 da mat). Com R ortonormal nao muda nada; nos destrocos
            // que encolhem (linhas com norma < 1) o objeto sai puxado para a camera (ate ~37 unidades; checagem VU1, pista 3).
            float a[3];
            for (int i = 0; i < 3; ++i)
                a[i] = (m.m[3][0] - eye_[0]) * m.m[i][0] + (m.m[3][1] - eye_[1]) * m.m[i][1] + (m.m[3][2] - eye_[2]) * m.m[i][2];
            for (int k = 0; k < 3; ++k) m.m[3][k] = eye_[k] + a[0] * m.m[0][k] + a[1] * m.m[1][k] + a[2] * m.m[2][k];
        }
        if (ram.u8(cs + 0xF)) {   // scaleMe
            const float sx = ram.f32(cs + 0x80), sy = ram.f32(cs + 0x84), sz = ram.f32(cs + 0x88);
            m = mul(scaleM(sx, sy, sz), m);
        }
        const size_t before = items.size();
        const uint32_t savedCs = cs_;
        cs_ = cs;
        walk(ram, ep, m, 0);
        cs_ = savedCs;
        if (hpList_ && needDrawMe && !isHud) {   // lista de alta prioridade: passada propria (ver `hp`)
            std::vector<DrawItem> &dst = isButtonId(id) ? hpOverlay : hp;
            if (isButtonId(id))
                for (size_t i = before; i < items.size(); ++i) items[i].btnId = uint16_t(id);
            dst.insert(dst.end(), std::make_move_iterator(items.begin() + before), std::make_move_iterator(items.end()));
            items.resize(before);
        } else if (needDrawMe && isButtonId(id)) {   // icones de botao: por cima da cena, sem profundidade (ver `overlay`)
            for (size_t i = before; i < items.size(); ++i) items[i].btnId = uint16_t(id);
            overlay.insert(overlay.end(), std::make_move_iterator(items.begin() + before), std::make_move_iterator(items.end()));
            items.resize(before);
        } else if (isHud) {   // HUD: por cima da cena (ver `hud`), com o id para a checagem
            for (size_t i = before; i < items.size(); ++i) items[i].hudId = uint16_t(id);
            hud.insert(hud.end(), std::make_move_iterator(items.begin() + before), std::make_move_iterator(items.end()));
            items.resize(before);
        }
    }
};

}  // namespace wotm
