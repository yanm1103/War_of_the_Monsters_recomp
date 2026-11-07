// wotm_levelpatch.inc: patches de fase (MODS.md, "Level patches"). Incluido por wotm_overrides.cpp depois do wotm_mods.inc.
// mods/<mod>/levels/<FASE>.json move, gira, apaga e copia objetos da fase. O gancho fica na entrada do dbInitDb chamado pelo
// Shell::LoadLevelFiles: a imagem da fase ja esta inflada em 0xA00000 com o TEX0 relocado, e nada foi registrado ainda (Destructibles,
// Interactives, Pickups, emissores e a grade de colisao copiam o que acham na travessia que vem logo depois). As regras sao as de
// FORMATS.md ("Stage objects", "Stage collision"); tools/lvl.py patch aplica o mesmo patch na imagem do disco, na mesma ordem e com
// as mesmas contas, e confere a colisao do resultado. Os nos novos (ROTATE no lugar de um TRANSLATE que gira, GROUP que segura o
// original e as copias, a espinha clonada de cada copia com o estado de animacao das partes animadas dela, acoes refeitas, o ANIM_MGR
// que registra as CHAR_INSTANCEs clonadas, caminhos de colisao e nos de lista das celulas) vao para a arena de 1 MB abaixo do MB dos ganchos (wotm_mods_hooks::kLevelArena), refeita a cada carga. Cada entrada
// confere antes de escrever o que pode tomar da arena e dos 1024 Interactives; a que nao cabe e pulada inteira.
// WOTM_LEVELPATCH_SHALLOW=1 faz copias rasas e WOTM_LEVELPATCH_BREAK quebra copias ou originais na luta (diagnostico, NATIVE.md).
// O JSON e procurado e lido a cada carga da fase (findPatch): o editor grava e a recarga (wotm_levelreload.inc, RESTART) ja o pega.
// WOTM_LEVELPATCH=0 desliga; =2 loga cada caminho refeito (a mesma linha do lvl.py patch).
// Ordem: "objects", "props" (wotm_levelprops.inc), "peds" (wotm_levelpeds.inc), cutNets com as plantas de todos.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdarg>
#include <filesystem>
#include <fstream>
#include <thread>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

extern std::atomic<bool> g_wotmLevelReloadKey;   // wotm_levelreload.inc: F10 pede a recarga da fase

namespace wotm_levelpatch
{
using wotm::mods::Registry;
namespace json = wotm::json;

constexpr uint32_t kBase = 0xA00000u, kMask = 0x0FFFFFFFu, kNgpCeiling = 0x1F10000u;
constexpr uint32_t kArena = wotm_mods_hooks::kLevelArena, kArenaEnd = kArena + wotm_mods_hooks::kLevelArenaSize;
constexpr uint32_t kDbInitDb = wotm::fn::dbInitDb__FP9_dbheader10_vramAddrs, kLoadLevelFiles = wotm::fn::LoadLevelFiles__5Shell,
                   kLoadLevelFilesSize = 0x248;   // wotm_syms.h
constexpr uint32_t kFsNames = wotm::sym::fileStatus + 0xB1;   // nome do arquivo 0 (a fase), 9 bytes
constexpr double kFromTol = 0.5, kPi = 3.14159265358979323846;
enum Op : uint32_t { OBJECT = 0, GROUP = 1, LOD = 2, TRANSLATE = 3, ROTATE = 4, SWITCH = 6, ANIM_MGR = 7, CONTROL = 8, NUL = 9,
                     COLL_VOL = 10, COLL_GRID = 15, ANIM_XFORM = 17, PART_EMITTER = 22, ACTION_DATA = 23, CHAR_INSTANCE = 25,
                     SCALE = 30, DESTRUCTIBLE = 31, SKEL_BONE = 35, ANIM_SCALE = 38, INTERACTIVE = 39, POLY_COLL = 42 };
const char *opName(uint32_t o)   // tools/ngp.py OPNAMES
{
    static const char *const names[] = {"OBJECT", "GROUP", "LOD", "TRANSLATE", "ROTATE", "LIGHT", "SWITCH", "ANIM_MGR", "CONTROL", "NULL",
                                        "COLL_VOL", "COLL_TRIG", "CHAR_ANIM", "TERRAIN", "TERRAIN_OBJ", "COLL_GRID", "PART_MGR",
                                        "ANIM_XFORM", "BSP", "PARTICLE", "PART_FIELD", "PART_VISUAL", "PART_EMITTER", "ACTION_DATA",
                                        "CHARACTER", "CHAR_INSTANCE", "op26", "op27", "op28", "op29", "SCALE", "DESTRUCTIBLE",
                                        "DESTRUCT_TABLE", "ANIM_CONTROL", "ANIM_BLEND", "SKEL_BONE", "ANIM_PLAYER", "CAMERA",
                                        "ANIM_SCALE", "INTERACTIVE", "ATTACHPT", "SCRIPT", "POLY_COLL", "ANIM_PROC"};
    return o < sizeof names / sizeof *names ? names[o] : "op?";
}
// Copia profunda (lvl.py: ANIM_OPS, ANIM_STATE_OPS, CB_OPS, PICKUP_IDS...): partes animadas; os nos animados que guardam estado por
// instancia (o CHAR_INSTANCE e os ANIM_XFORM/ANIM_SCALE dele); os ops pelos quais o dbsTraverse chama o dbProcInteractive quando tem id;
// ids de pickup (um por instancia), ignorados, e o "nao perde vida" do DESTRUCTIBLE
bool animOp(uint32_t o) { return o == CHAR_INSTANCE || o == ANIM_XFORM || o == SKEL_BONE || o == ANIM_SCALE; }
bool animStateOp(uint32_t o) { return o == ANIM_XFORM || o == ANIM_SCALE; }
constexpr uint32_t kId2Max = 0x7FF;            // ActAnim::ids.anim e _hierhead::id2 tem 11 bits
constexpr uint16_t kActAnimMonster = 0x1800;   // ACTANIM_PLAYER | ACTANIM_AI: o alvo e um monstro
bool cbOp(uint32_t o) { return o <= 4 || o == 6 || o == 11 || o == 17 || o == 30 || o == 40 || o == 41; }
constexpr uint32_t kPickupLo = 0x12C0, kPickupHi = 0x13EC, kExclLo = 0x7D0, kExclHi = 0x898, kKeepLo = 0x1A90, kKeepHi = 0x1B58;
constexpr int64_t kInteractives = 1024, kInteractiveSpare = 32;   // Interactives::s_dbInteractive, sem checagem; folga dos monstros etc.
// Acoes (ActionDispatch.cpp, lvl.py GLOBAL_ACTS/DOMINO_OFF): ANIM, RUNSCRIPT e GENERICEVENT agem sobre algo fixo da fase (a copia as tira);
// o tipo 6 o DelayAction::doIt ignora (vira isso o DOMINO cujo alvo foi apagado)
bool globalAct(uint32_t t) { return t == 2 || t == 12 || t == 14; }
constexpr uint16_t kDominoOff = 6;

PS2Runtime::RecompiledFunction g_orig = nullptr;
int g_verbose = 1;
bool g_shallow = false;   // WOTM_LEVELPATCH_SHALLOW=1: copia rasa (so a alca e o par de cima), para comparar

struct V3 { double v[3]; };
struct M4 { double m[4][4]; };
M4 identM() { M4 r{}; for (int i = 0; i < 4; ++i) r.m[i][i] = 1.0; return r; }
M4 translateM(double x, double y, double z) { M4 r = identM(); r.m[3][0] = x; r.m[3][1] = y; r.m[3][2] = z; return r; }
// vetores-linha (p' = p * M): mul(a, b) aplica a primeiro
M4 mul(const M4 &a, const M4 &b)
{
    M4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k) r.m[i][j] += a.m[i][k] * b.m[k][j];
    return r;
}
V3 xform(const M4 &m, const V3 &p)
{
    V3 r;
    for (int c = 0; c < 3; ++c) r.v[c] = p.v[0] * m.m[0][c] + p.v[1] * m.m[1][c] + p.v[2] * m.m[2][c] + m.m[3][c];
    return r;
}
bool invAffine(const M4 &m, M4 &out)
{
    const auto &a = m.m;
    const double det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
                       a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
    if (std::fabs(det) < 1e-12) return false;
    const double i[3][3] = {{(a[1][1] * a[2][2] - a[1][2] * a[2][1]) / det, (a[0][2] * a[2][1] - a[0][1] * a[2][2]) / det,
                             (a[0][1] * a[1][2] - a[0][2] * a[1][1]) / det},
                            {(a[1][2] * a[2][0] - a[1][0] * a[2][2]) / det, (a[0][0] * a[2][2] - a[0][2] * a[2][0]) / det,
                             (a[0][2] * a[1][0] - a[0][0] * a[1][2]) / det},
                            {(a[1][0] * a[2][1] - a[1][1] * a[2][0]) / det, (a[0][1] * a[2][0] - a[0][0] * a[2][1]) / det,
                             (a[0][0] * a[1][1] - a[0][1] * a[1][0]) / det}};
    out = identM();
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) out.m[r][c] = i[r][c];
    for (int c = 0; c < 3; ++c) out.m[3][c] = -(a[3][0] * i[0][c] + a[3][1] * i[1][c] + a[3][2] * i[2][c]);
    return true;
}
double maxScale(const M4 &m)
{
    double s = 0.0;
    for (int r = 0; r < 3; ++r) s = std::max(s, m.m[r][0] * m.m[r][0] + m.m[r][1] * m.m[r][1] + m.m[r][2] * m.m[r][2]);
    return std::sqrt(s);
}
double dist(const V3 &a, const V3 &b)
{
    return std::sqrt((a.v[0] - b.v[0]) * (a.v[0] - b.v[0]) + (a.v[1] - b.v[1]) * (a.v[1] - b.v[1]) + (a.v[2] - b.v[2]) * (a.v[2] - b.v[2]));
}
// menor esfera com as duas
void sphereUnion(V3 &c1, double &r1, const V3 &c2, double r2)
{
    const double d = dist(c1, c2);
    if (d + r2 <= r1) return;
    if (d + r1 <= r2) { c1 = c2; r1 = r2; return; }
    const double r = (d + r1 + r2) / 2, k = (r - r1) / d;
    for (int i = 0; i < 3; ++i) c1.v[i] += (c2.v[i] - c1.v[i]) * k;
    r1 = r;
}
struct Box { V3 mn, mx; };
bool aabb(const std::vector<V3> &pts, Box &b)
{
    if (pts.empty()) return false;
    b.mn = b.mx = pts[0];
    for (const V3 &p : pts)
        for (int i = 0; i < 3; ++i) b.mn.v[i] = std::min(b.mn.v[i], p.v[i]), b.mx.v[i] = std::max(b.mx.v[i], p.v[i]);
    return true;
}
std::vector<V3> corners(const Box &b)
{
    std::vector<V3> out;
    for (int x = 0; x < 2; ++x)
        for (int y = 0; y < 2; ++y)
            for (int z = 0; z < 2; ++z) out.push_back({{x ? b.mx.v[0] : b.mn.v[0], y ? b.mx.v[1] : b.mn.v[1], z ? b.mx.v[2] : b.mn.v[2]}});
    return out;
}

struct PatchError : std::runtime_error { using std::runtime_error::runtime_error; };
std::string fmt(const char *f, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

// ------------------------------------------------------------------ planta dos objetos (lvl.py hull2d, seg_in_poly)
struct P2 { double x, y; };
struct Footprint { std::vector<P2> poly; double zmin, zmax; };
// Casco convexo do XY, anti-horario (cadeia monotona)
std::vector<P2> hull2d(const std::vector<V3> &pts)
{
    std::vector<P2> p;
    for (const V3 &q : pts) p.push_back({std::round(q.v[0] * 1000.0) / 1000.0, std::round(q.v[1] * 1000.0) / 1000.0});
    std::sort(p.begin(), p.end(), [](const P2 &a, const P2 &b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    p.erase(std::unique(p.begin(), p.end(), [](const P2 &a, const P2 &b) { return a.x == b.x && a.y == b.y; }), p.end());
    if (p.size() < 3) return p;
    auto half = [](auto first, auto last) {
        std::vector<P2> out;
        for (auto it = first; it != last; ++it)
        {
            const P2 &q = *it;
            while (out.size() >= 2)
            {
                const P2 &a = out[out.size() - 2], &b = out.back();
                if ((b.x - a.x) * (q.y - a.y) - (b.y - a.y) * (q.x - a.x) > 0) break;
                out.pop_back();
            }
            out.push_back(q);
        }
        out.pop_back();
        return out;
    };
    std::vector<P2> lo = half(p.begin(), p.end()), up = half(p.rbegin(), p.rend());
    lo.insert(lo.end(), up.begin(), up.end());
    return lo;
}
// Trecho [t0, t1] do segmento a->b (XY) dentro do poligono convexo anti-horario crescido de `margin` (cada aresta empurrada)
bool segInPoly(const V3 &a, const V3 &b, const std::vector<P2> &poly, double margin, double &t0, double &t1)
{
    if (poly.size() < 3) return false;
    t0 = 0.0, t1 = 1.0;
    const double dx = b.v[0] - a.v[0], dy = b.v[1] - a.v[1];
    for (size_t i = 0; i < poly.size(); ++i)
    {
        const P2 &p = poly[i], &q = poly[(i + 1) % poly.size()];
        double nx = q.y - p.y, ny = p.x - q.x;
        const double ln = std::hypot(nx, ny);
        if (ln < 1e-9) continue;
        nx /= ln, ny /= ln;
        const double f = nx * (a.v[0] - p.x) + ny * (a.v[1] - p.y) - margin, g = nx * dx + ny * dy;   // dentro enquanto f + t*g <= 0
        if (std::fabs(g) < 1e-12)
        {
            if (f > 0) return false;
            continue;
        }
        const double t = -f / g;
        if (g > 0) t1 = std::min(t1, t);
        else t0 = std::max(t0, t);
        if (t0 >= t1) return false;
    }
    return true;
}

// ------------------------------------------------------------------ a fase na RAM
struct Stage
{
    uint8_t *ram;
    uint32_t hi = kNgpCeiling, arenaTop = kArena;
    std::vector<uint32_t> roots;
    std::unordered_map<uint32_t, std::vector<uint32_t>> parents;
    std::unordered_set<uint32_t> inTree;
    uint32_t grid = 0;
    int cols = 0, rows = 0;
    double cell = 200.0, minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
    std::map<uint32_t, std::set<int>> cells;   // caminho -> celulas em que esta listado
    std::vector<uint32_t> freeNodes;
    std::map<std::string, int> stats;
    std::vector<std::string> log, pathLines;
    std::vector<Footprint> footprints;   // cada caixa solida no lugar novo, para cortar as redes de carros e pedestres
    bool shallow = false;
    std::vector<uint32_t> wraps;           // GROUPs das copias: filho 0 e o original, os outros as copias
    std::array<int64_t, 3> interBefore{};  // Interactives antes do patch (carga + 1, destrocos com pickup, folga)
    int64_t interUsed = 0;
    uint32_t mgr = 0;                                    // raiz ANIM_MGR da fase (animationSetGlobal le a lista dela depois do gancho)
    std::map<uint32_t, std::set<uint32_t>> id2Used;      // id1 -> id2 das CHAR_INSTANCEs registradas e das clonadas
    std::vector<uint32_t> animAdded;                     // CHAR_INSTANCEs clonadas que ainda nao estao no ANIM_MGR

    uint32_t u32(uint32_t a) const { uint32_t v; std::memcpy(&v, ram + (a & PS2_RAM_MASK), 4); return v; }
    uint16_t u16(uint32_t a) const { uint16_t v; std::memcpy(&v, ram + (a & PS2_RAM_MASK), 2); return v; }
    int16_t s16(uint32_t a) const { return int16_t(u16(a)); }
    uint8_t u8(uint32_t a) const { return ram[a & PS2_RAM_MASK]; }
    double f32(uint32_t a) const { float v; std::memcpy(&v, ram + (a & PS2_RAM_MASK), 4); return v; }
    void w32(uint32_t a, uint32_t v) { std::memcpy(ram + (a & PS2_RAM_MASK), &v, 4); }
    void w16(uint32_t a, uint16_t v) { std::memcpy(ram + (a & PS2_RAM_MASK), &v, 2); }
    void wf32(uint32_t a, double v) { const float f = float(v); std::memcpy(ram + (a & PS2_RAM_MASK), &f, 4); }
    void copyBytes(uint32_t dst, uint32_t src, uint32_t n) { std::memmove(ram + (dst & PS2_RAM_MASK), ram + (src & PS2_RAM_MASK), n); }
    bool ok(uint32_t a, uint32_t n = 4) const { return (a >= kBase && a + n <= hi) || (a >= kArena && a + n <= arenaTop); }
    uint32_t op(uint32_t n) const { return u32(n) & 63; }
    uint32_t ident(uint32_t n) const { return u32(n) >> 18; }
    void setPtr(uint32_t slot, uint32_t target) { w32(slot, (u32(slot) & ~kMask) | target); }

    uint32_t alloc(uint32_t size, uint32_t align = 4)
    {
        const uint32_t a = (arenaTop + align - 1) & ~(align - 1);
        if (a + size > kArenaEnd) throw PatchError("a arena de 1 MB encheu");
        std::memset(ram + a, 0, size);
        arenaTop = a + size;
        stats["arena bytes"] = int(arenaTop - kArena);
        return a;
    }
    // Devolve o que a arena tomou desde que o topo era `top` (entrada recusada depois da primeira alocacao)
    void rollback(uint32_t top)
    {
        arenaTop = top;
        stats["arena bytes"] = int(arenaTop - kArena);
    }

    // Ponteiros de filho como o dbsTraverse desce, mais os estados do DESTRUCTIBLE e os dois filhos do CONTROL (lvl.py child_slots)
    std::vector<uint32_t> childSlots(uint32_t n) const
    {
        std::vector<uint32_t> out;
        if (!ok(n, 16)) return out;
        uint32_t off = 0, cnt = 0;
        switch (op(n))
        {
        case GROUP: off = 0x20, cnt = u16(n + 8); break;
        case TRANSLATE: off = 0x1C, cnt = u32(n + 8); break;
        case ROTATE: off = 0x50, cnt = u32(n + 8); break;
        case SCALE: off = 0x2C, cnt = u32(n + 8); break;
        case SWITCH: off = 0xC, cnt = u8(n + 0xB); break;
        case SKEL_BONE: off = 0xC, cnt = u32(n + 4); break;
        case CHAR_INSTANCE: off = 0x2C, cnt = u32(n + 0x28); break;
        case ANIM_XFORM: off = 0xCC, cnt = u32(n + 4); break;
        case ANIM_SCALE: off = 0x60, cnt = u32(n + 4); break;
        case LOD:
            for (uint32_t k = 0; k < std::min<uint32_t>(u32(n + 4), 16); ++k) out.push_back(n + 0x28 + 16 * k);
            return out;
        case CONTROL: return {n + 4, n + 8};
        case ACTION_DATA: return {n + 4};
        case INTERACTIVE: return {n + 0xC};
        case DESTRUCTIBLE:
            for (uint32_t k = 0; k < std::min<uint32_t>(u16(n + 0xA), 64); ++k) out.push_back(n + 0xC + 12 * k);
            return out;
        default: return out;
        }
        for (uint32_t k = 0; k < std::min<uint32_t>(cnt, 8192); ++k) out.push_back(n + off + 4 * k);
        return out;
    }
    std::vector<uint32_t> children(uint32_t n) const
    {
        std::vector<uint32_t> out;
        for (uint32_t s : childSlots(n))
            if (ok(s))
                if (const uint32_t c = u32(s) & kMask; c && ok(c)) out.push_back(c);
        return out;
    }

    bool load()
    {
        const uint32_t n = u32(kBase);
        if (n == 0 || n > 4096) return false;
        for (uint32_t i = 0; i < n; ++i)
            if (const uint32_t r = u32(kBase + 4 + 4 * i) & kMask; ok(r)) roots.push_back(r);
        for (uint32_t r : roots)
        {
            if (op(r) == COLL_GRID) grid = r;
            if (op(r) == ANIM_MGR && !mgr) mgr = r;
        }
        if (!grid) return false;
        cols = s16(grid + 4), rows = s16(grid + 6);
        cell = f32(grid + 8), minX = f32(grid + 0xC), minY = f32(grid + 0x10), maxX = f32(grid + 0x14), maxY = f32(grid + 0x18);
        if (cols <= 0 || rows <= 0 || cols * rows > 65536 || cell <= 0.0) return false;
        // fim da imagem: a grade fecha o bloco de colisao (FORMATS.md, "Stage collision") e so a raiz ANIM_MGR vem depois dela
        hi = grid + 0x20 + 4 * uint32_t(cols * rows);
        for (uint32_t r : roots) hi = std::max(hi, r + 0x400);
        for (uint32_t ci : instances()) id2Used[ident(ci)].insert((u32(ci) >> 7) & kId2Max);
        for (uint32_t r : roots)
        {
            std::vector<uint32_t> stack{r};
            while (!stack.empty())
            {
                const uint32_t x = stack.back();
                stack.pop_back();
                if (!inTree.insert(x).second) continue;
                for (uint32_t c : children(x))
                {
                    parents[c].push_back(x);
                    stack.push_back(c);
                }
            }
        }
        for (int i = 0; i < cols * rows; ++i)
        {
            uint32_t ln = u32(grid + 0x20 + 4 * i) & kMask;
            for (int guard = 0; ln && ok(ln, 8) && guard < 100000; ++guard)
            {
                cells[u32(ln + 4) & kMask].insert(i);
                ln = u32(ln) & kMask;
            }
        }
        return true;
    }

    uint32_t parentOf(uint32_t n) const
    {
        const auto it = parents.find(n);
        return it == parents.end() || it->second.empty() ? 0 : it->second[0];
    }
    std::vector<uint32_t> chain(uint32_t n) const
    {
        std::vector<uint32_t> out{n};
        std::unordered_set<uint32_t> seen{n};
        while (const uint32_t p = parentOf(out.back()))
        {
            if (!seen.insert(p).second) break;
            out.push_back(p);
        }
        std::reverse(out.begin(), out.end());
        return out;
    }
    std::unordered_set<uint32_t> subtree(uint32_t n) const
    {
        std::unordered_set<uint32_t> seen;
        std::vector<uint32_t> stack{n};
        while (!stack.empty())
        {
            const uint32_t x = stack.back();
            stack.pop_back();
            if (!seen.insert(x).second) continue;
            for (uint32_t c : children(x)) stack.push_back(c);
        }
        return seen;
    }
    bool local(uint32_t n, bool withScale, M4 &m) const
    {
        switch (op(n))
        {
        case TRANSLATE: m = translateM(f32(n + 0x10), f32(n + 0x14), f32(n + 0x18)); return true;
        case ROTATE:
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c) m.m[r][c] = f32(n + 0x10 + 16 * r + 4 * c);
            return true;
        case SCALE:
            m = translateM(f32(n + 0x10), f32(n + 0x14), f32(n + 0x18));
            if (withScale)
            {
                M4 s = identM();
                for (int i = 0; i < 3; ++i) s.m[i][i] = f32(n + 0x20 + 4 * i);
                m = mul(s, m);
            }
            return true;
        default: return false;
        }
    }
    M4 chainMatrix(const std::vector<uint32_t> &nodes, size_t count, bool withScale) const
    {
        M4 m = identM(), l;
        for (size_t i = 0; i < count && i < nodes.size(); ++i)
            if (local(nodes[i], withScale, l)) m = mul(l, m);
        return m;
    }
    M4 world(uint32_t n, bool withScale = true) const { const auto c = chain(n); return chainMatrix(c, c.size(), withScale); }
    M4 above(uint32_t n) const { const auto c = chain(n); return chainMatrix(c, c.size() - 1, true); }

    // ------------------------------------------------------------------ colisao
    struct Path { uint32_t addr = 0; Box box{}; int len = 0; std::vector<uint32_t> nodes; };
    Path path(uint32_t a) const
    {
        Path p;
        p.addr = a;
        for (int i = 0; i < 3; ++i) p.box.mn.v[i] = f32(a + 4 * i), p.box.mx.v[i] = f32(a + 0xC + 4 * i);
        p.len = int32_t(u32(a + 0x1C));
        for (int k = 0; k < std::clamp(p.len, 0, 256); ++k) p.nodes.push_back(u32(a + 0x24 + 4 * k) & kMask);
        return p;
    }
    std::vector<uint32_t> pathsThrough(uint32_t n) const
    {
        std::vector<uint32_t> out;
        for (const auto &[a, c] : cells)
        {
            const Path p = path(a);
            if (std::find(p.nodes.begin(), p.nodes.end(), n) != p.nodes.end()) out.push_back(a);
        }
        return out;
    }

    // Vertices do casco {p: n.p <= d} (sign +1) ou {n.p + d <= 0} (sign -1) pela intersecao de trios de planos (lvl.py _hull_corners)
    static std::vector<V3> hullCorners(std::vector<std::array<double, 4>> pl, int sign)
    {
        if (sign < 0)
            for (auto &q : pl) q[3] = -q[3];
        std::vector<V3> out;
        for (size_t i = 0; i < pl.size(); ++i)
            for (size_t j = i + 1; j < pl.size(); ++j)
                for (size_t k = j + 1; k < pl.size(); ++k)
                {
                    const auto &a = pl[i], &b = pl[j], &c = pl[k];
                    const double det = a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0]);
                    if (std::fabs(det) < 1e-9) continue;
                    const double dx = a[3] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[3] * c[2] - b[2] * c[3]) + a[2] * (b[3] * c[1] - b[1] * c[3]);
                    const double dy = a[0] * (b[3] * c[2] - b[2] * c[3]) - a[3] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[3] - b[3] * c[0]);
                    const double dz = a[0] * (b[1] * c[3] - b[3] * c[1]) - a[1] * (b[0] * c[3] - b[3] * c[0]) + a[3] * (b[0] * c[1] - b[1] * c[0]);
                    const V3 p{{dx / det, dy / det, dz / det}};
                    bool in = true;
                    for (const auto &q : pl)
                        if (q[0] * p.v[0] + q[1] * p.v[1] + q[2] * p.v[2] > q[3] + 1e-2 * std::max(1.0, std::fabs(q[3]))) { in = false; break; }
                    if (in) out.push_back(p);
                }
        return out;
    }
    // Pontos locais que limitam a folha: cantos do casco (COLL_VOL) ou os vertices que os triangulos usam (POLY_COLL); o COLL_TRIG e
    // uma BSP sem vertices (lvl.py leaf_points)
    std::vector<V3> leafPoints(uint32_t leaf) const
    {
        std::vector<V3> pts;
        if (op(leaf) == COLL_VOL)
        {
            const int n = s16(leaf + 4);
            if (n > 40 || !ok(leaf + 0x20, 16 * uint32_t(std::max(n, 0)))) return pts;
            std::vector<std::array<double, 4>> pl;
            for (int k = 0; k < std::max(n, 0); ++k)
                pl.push_back({f32(leaf + 0x20 + 16 * k), f32(leaf + 0x24 + 16 * k), f32(leaf + 0x28 + 16 * k), f32(leaf + 0x2C + 16 * k)});
            const double cx = f32(leaf + 0x10), cy = f32(leaf + 0x14), cz = f32(leaf + 0x18);
            int pos = 0, neg = 0;
            for (const auto &q : pl)
            {
                pos += q[0] * cx + q[1] * cy + q[2] * cz <= q[3] + 1e-3;
                neg += q[0] * cx + q[1] * cy + q[2] * cz + q[3] <= 1e-3;
            }
            return hullCorners(pl, pos >= neg ? 1 : -1);
        }
        if (op(leaf) == POLY_COLL)
            for (uint32_t k = 0; k < std::min<uint32_t>(u32(leaf + 4), 256); ++k)
            {
                const uint32_t o = u32(leaf + 8 + 4 * k) & kMask;
                if (!ok(o, 0x18)) return {};
                const uint32_t vp = u32(o) & kMask, npol = u16(o + 4), nbox = u16(o + 6), polys = o + 0x18 + 14 * nbox;
                if (!ok(polys, 8 * npol)) return {};
                std::set<uint32_t> used;
                for (uint32_t i = 0; i < npol; ++i)
                    for (uint32_t j = 0; j < 3; ++j) used.insert(u8(polys + 8 * i + j));
                for (uint32_t v : used)
                {
                    if (!ok(vp + 12 * v, 12)) return {};
                    pts.push_back({{f32(vp + 12 * v), f32(vp + 12 * v + 4), f32(vp + 12 * v + 8)}});
                }
            }
        return pts;
    }
    // Caixa do caminho pela regra do disco (os 8 cantos da caixa local da folha levados ao mundo pelos TRANSLATE/ROTATE do caminho,
    // sem SCALE); false nos caminhos animados e nas folhas sem pontos
    // Cantos de mundo da caixa local da folha, pela cadeia sem escala (lvl.py path_corners)
    bool pathCorners(const Path &p, std::vector<V3> &w) const
    {
        if (p.nodes.empty()) return false;
        for (uint32_t n : p.nodes)
            if (op(n) == ANIM_XFORM || op(n) == CHAR_INSTANCE) return false;
        Box lb;
        if (!aabb(leafPoints(p.nodes.back()), lb)) return false;
        const M4 m = chainMatrix(p.nodes, p.nodes.size() - 1, false);
        w.clear();
        for (const V3 &c : corners(lb)) w.push_back(xform(m, c));
        return true;
    }
    bool pathBox(const Path &p, Box &out) const
    {
        std::vector<V3> w;
        return pathCorners(p, w) && aabb(w, out);
    }
    std::set<int> cellsFor(const Box &b) const
    {
        const int c0 = std::max(0, int(std::floor((b.mn.v[0] - minX) / cell))), c1 = std::min(cols - 1, int(std::floor((b.mx.v[0] - minX) / cell)));
        const int r0 = std::max(0, int(std::floor((b.mn.v[1] - minY) / cell))), r1 = std::min(rows - 1, int(std::floor((b.mx.v[1] - minY) / cell)));
        std::set<int> out;
        for (int r = r0; r <= r1; ++r)
            for (int c = c0; c <= c1; ++c) out.insert(r * cols + c);
        return out;
    }
    uint32_t head(int c) const { return grid + 0x20 + 4 * uint32_t(c); }
    void unlink(int c, uint32_t pa)
    {
        uint32_t slot = head(c), ln = u32(slot) & kMask;
        for (int guard = 0; ln && guard < 100000; ++guard)
        {
            if ((u32(ln + 4) & kMask) == pa)
            {
                w32(slot, u32(ln));
                freeNodes.push_back(ln);
                return;
            }
            slot = ln, ln = u32(ln) & kMask;
        }
        throw PatchError(fmt("caminho %07x fora da celula %d", pa, c));
    }
    void link(int c, uint32_t pa)
    {
        uint32_t ln;
        if (!freeNodes.empty())
            ln = freeNodes.back(), freeNodes.pop_back();
        else
            ln = alloc(8), ++stats["new list nodes"];
        w32(ln, u32(head(c)));
        w32(ln + 4, pa);
        w32(head(c), ln);
    }
    void setCells(uint32_t pa, const std::set<int> &now)
    {
        const std::set<int> old = cells.count(pa) ? cells[pa] : std::set<int>{};
        for (int c : old)
            if (!now.count(c)) unlink(c, pa);
        for (int c : now)
            if (!old.count(c)) link(c, pa);
        if (now.empty())
            cells.erase(pa);
        else
            cells[pa] = now;
    }
    // Caixa e celulas novas: pela regra do disco quando a folha deixa, senao a caixa velha levada por `carry`
    void redo(uint32_t pa, const M4 &carry)
    {
        const Path p = path(pa);
        Box bb;
        const char *how = "rule";
        std::vector<V3> w;
        if (!pathCorners(p, w))
        {
            w.clear();
            for (const V3 &c : corners(p.box)) w.push_back(xform(carry, c));
            how = "carried";
        }
        aabb(w, bb);
        if (!(u32(pa + 0x20) & 1)) footprints.push_back({hull2d(w), bb.mn.v[2], bb.mx.v[2]});
        for (int i = 0; i < 3; ++i) wf32(pa + 4 * i, bb.mn.v[i]), wf32(pa + 0xC + 4 * i, bb.mx.v[i]);
        const std::set<int> cs = cellsFor(bb);
        setCells(pa, cs);
        ++stats["paths redone"];
        pathLines.push_back(fmt("caminho %07x  caixa (%.1f %.1f %.1f)-(%.1f %.1f %.1f)  %zu celulas  %s", pa, bb.mn.v[0], bb.mn.v[1],
                                bb.mn.v[2], bb.mx.v[0], bb.mx.v[1], bb.mx.v[2], cs.size(), how));
    }

    // ------------------------------------------------------------------ esferas do desenho
    // Esfera local do que pende da alca: os primeiros GROUPs abaixo dela, passando por INTERACTIVE_STATE e DESTRUCTIBLE
    bool objSphere(uint32_t h, V3 &c, double &r) const
    {
        std::vector<uint32_t> stack = children(h);
        bool any = false;
        while (!stack.empty())
        {
            const uint32_t x = stack.back();
            stack.pop_back();
            const uint32_t o = op(x);
            if (o == GROUP)
            {
                const V3 cc{{f32(x + 0x10), f32(x + 0x14), f32(x + 0x18)}};
                if (!any) c = cc, r = f32(x + 0x1C), any = true;
                else sphereUnion(c, r, cc, f32(x + 0x1C));
            }
            else if (o == INTERACTIVE || o == DESTRUCTIBLE)
                for (uint32_t k : children(x)) stack.push_back(k);
            else if (o != NUL)
                return false;
        }
        return any;
    }
    // O desenho pula o GROUP cuja esfera sai da vista (Scene::outOfView): as dos GROUPs acima de h crescem para cobrir o que pende dela
    void growSpheres(uint32_t h, const V3 &oldPos)
    {
        const M4 w = world(h);
        const std::vector<uint32_t> ch = chain(h);
        std::vector<uint32_t> groups;
        for (size_t i = 0; i + 1 < ch.size(); ++i)
            if (op(ch[i]) == GROUP) groups.push_back(ch[i]);
        if (groups.empty()) return;
        V3 co, lc;
        double ro, lr;
        if (objSphere(h, lc, lr))
            co = xform(w, lc), ro = lr * maxScale(w);
        else
        {   // nada para medir: a esfera do GROUP mais perto, levada do lugar antigo
            const uint32_t g = groups.back();
            const M4 m = above(g);
            const V3 cw = xform(m, {{f32(g + 0x10), f32(g + 0x14), f32(g + 0x18)}});
            co = {{w.m[3][0], w.m[3][1], w.m[3][2]}};
            ro = f32(g + 0x1C) * maxScale(m) + dist(cw, oldPos);
        }
        for (uint32_t g : groups)
        {
            const M4 m = above(g);
            const double k = maxScale(m);
            V3 c = xform(m, {{f32(g + 0x10), f32(g + 0x14), f32(g + 0x18)}});
            double r = f32(g + 0x1C) * k;
            const double rw = r;
            sphereUnion(c, r, co, ro);
            M4 inv;
            if (r <= rw + 1e-3 || !invAffine(m, inv)) continue;
            const V3 l = xform(inv, c);
            const double rl = r / k + 0.5;
            for (int i = 0; i < 3; ++i) wf32(g + 0x10 + 4 * i, l.v[i]);
            wf32(g + 0x1C, rl);
            wf32(g + 0xC, rl * rl);
            ++stats["spheres grown"];
        }
    }

    // ------------------------------------------------------------------ alcas
    // Poe `nw` onde `old` estava: na lista de filhos do pai e em cada caminho de colisao
    void replace(uint32_t old, uint32_t nw)
    {
        for (uint32_t pa : pathsThrough(old))
            for (int k = 0; k < path(pa).len; ++k)
                if ((u32(pa + 0x24 + 4 * k) & kMask) == old) setPtr(pa + 0x24 + 4 * k, nw);
        const uint32_t p = parentOf(old);
        for (uint32_t s : childSlots(p))
            if ((u32(s) & kMask) == old) setPtr(s, nw);
        parents[nw] = parents[old];
        parents[old].clear();
        for (uint32_t c : children(nw))
            for (uint32_t &x : parents[c])
                if (x == old) x = nw;
    }
    // ROTATE no lugar do TRANSLATE h (mesmos filhos, sem rotacao), para o objeto poder girar
    uint32_t toRotate(uint32_t h)
    {
        if (u32(h + 0xC)) throw PatchError(fmt("TRANSLATE %07x tem dados de acao (+0xC): nao vira ROTATE", h));
        const uint32_t n = u32(h + 8);
        const uint32_t r = alloc(0x50 + 4 * n, 16);
        w32(r, (u32(h) & ~63u) | ROTATE);
        w32(r + 4, u32(h + 4));
        w32(r + 8, n);
        for (int row = 0; row < 4; ++row)
            for (int c = 0; c < 4; ++c) wf32(r + 0x10 + 16 * row + 4 * c, row == c ? 1.0 : 0.0);
        for (int i = 0; i < 3; ++i) wf32(r + 0x40 + 4 * i, f32(h + 0x10 + 4 * i));
        for (uint32_t k = 0; k < n; ++k) w32(r + 0x50 + 4 * k, u32(h + 0x1C + 4 * k));
        replace(h, r);
        ++stats["TRANSLATE to ROTATE"];
        return r;
    }
    // Escreve a alca h para ela ficar na posicao `pos` do mundo com o rumo `heading` (graus; NaN = mantem). Devolve a alca: um ROTATE
    // novo quando um TRANSLATE tem de girar.
    uint32_t place(uint32_t h, const V3 &pos, double heading)
    {
        const M4 p = above(h), w = world(h);
        uint32_t o = op(h);
        double R[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        if (o == ROTATE)
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) R[r][c] = f32(h + 0x10 + 16 * r + 4 * c);
        double delta = std::isnan(heading) ? 0.0 : heading - std::atan2(w.m[0][1], w.m[0][0]) * 180.0 / kPi;
        delta = std::fmod(std::fmod(delta + 180.0, 360.0) + 360.0, 360.0) - 180.0;
        if (std::fabs(delta) > 1e-3)
        {
            const double cs = std::cos(delta * kPi / 180.0), sn = std::sin(delta * kPi / 180.0);
            for (auto &r : R)
            {
                const double x = r[0], y = r[1];
                r[0] = x * cs - y * sn, r[1] = x * sn + y * cs;
            }
            if (o == TRANSLATE) h = toRotate(h), o = ROTATE;
        }
        M4 inv;
        if (!invAffine(p, inv)) throw PatchError(fmt("matriz do pai de %07x sem inversa", h));
        const V3 t = xform(inv, pos);
        if (o == TRANSLATE)
            for (int i = 0; i < 3; ++i) wf32(h + 0x10 + 4 * i, t.v[i]);
        else
        {
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) wf32(h + 0x10 + 16 * r + 4 * c, R[r][c]);
            for (int i = 0; i < 3; ++i) wf32(h + 0x40 + 4 * i, t.v[i]);
        }
        return h;
    }
    static V3 pos3(const M4 &m) { return {{m.m[3][0], m.m[3][1], m.m[3][2]}}; }
    M4 carryFrom(const M4 &before, uint32_t h) const
    {
        M4 inv;
        if (!invAffine(before, inv)) throw PatchError("matriz da alca sem inversa");
        return mul(inv, world(h, false));
    }

    // ------------------------------------------------------------------ edicoes
    void move(uint32_t h, const V3 &pos, double heading)
    {
        const M4 before = world(h, false);
        const V3 oldPos = pos3(world(h));
        h = place(h, pos, heading);
        const M4 carry = carryFrom(before, h);
        for (uint32_t pa : pathsThrough(h)) redo(pa, carry);
        growSpheres(h, oldPos);
        ++stats["moved"];
    }
    void erase(uint32_t h)
    {
        for (uint32_t pa : pathsThrough(h)) setCells(pa, {}), ++stats["paths removed"];
        w32(h, (u32(h) & ~63u) | NUL);
        ++stats["deleted"];
    }
    // Os nos que o dbsTraverse alcanca a partir das raizes que ele anda (so esses o dbProcInteractive registra)
    std::unordered_set<uint32_t> reached() const
    {
        std::unordered_set<uint32_t> seen;
        std::vector<uint32_t> stack;
        for (uint32_t r : roots)
            if (op(r) != ANIM_MGR && seen.insert(r).second) stack.push_back(r);
        while (!stack.empty())
        {
            const uint32_t x = stack.back();
            stack.pop_back();
            for (uint32_t c : travKids(x))
                if (seen.insert(c).second) stack.push_back(c);
        }
        return seen;
    }
    // Depois dos apagados: DOMINO cujo alvo o dbsTraverse nao alcanca mais chamaria o ChangeState no Interactives 0 (NULL: o id2 do alvo
    // fica 0); o tipo dele vira kDominoOff, no lugar
    void cutIncoming()
    {
        const std::unordered_set<uint32_t> seen = reached();
        std::unordered_set<uint32_t> done;
        for (uint32_t n : seen)
            if (op(n) == DESTRUCTIBLE)
                for (const ActBlock &b : actBlocks(n))
                    if (b.type == 8 && !done.count(b.a) && !seen.count(u32(b.a + 0xC) & kMask))
                    {
                        done.insert(b.a);
                        w16(b.a, kDominoOff);
                        ++stats["incoming dominoes cut"];
                    }
    }
    struct Place { V3 pos; double heading; };
    // Copia de n e dos GROUP/SWITCH/TRANSLATE/ROTATE abaixo dele que nao tem outro pai; o que e compartilhado fica compartilhado
    uint32_t clonePrivate(uint32_t n, bool top)
    {
        uint32_t head, count;
        switch (op(n))
        {
        case GROUP: head = 0x20, count = u16(n + 8); break;
        case SWITCH: head = 0xC, count = u8(n + 0xB); break;
        case TRANSLATE: head = 0x1C, count = u32(n + 8); break;
        case ROTATE: head = 0x50, count = u32(n + 8); break;
        default: return n;
        }
        if (!top && (!parents.count(n) || parents[n].size() != 1)) return n;
        const uint32_t size = head + 4 * std::min<uint32_t>(count, 8192);
        const uint32_t a = alloc(size, 16);
        copyBytes(a, n, size);
        for (uint32_t s : childSlots(n))
            if (const uint32_t c = u32(s) & kMask; c && ok(c))
                if (const uint32_t nc = clonePrivate(c, false); nc != c) setPtr(a + (s - n), nc), parents[nc] = {a};
        ++stats["nodes cloned"];
        return a;
    }

    // ------------------------------------------------------------------ copia profunda (MODS.md, "Under the hood"; lvl.py, mesmos nomes)
    uint32_t arenaFree() const { return kArenaEnd - arenaTop; }
    // Bytes que a copia clona do no n; 0 = op sem tamanho conhecido (ou com mais filhos do que o childSlots anda)
    uint32_t nodeSize(uint32_t n) const
    {
        switch (op(n))
        {
        case GROUP: return 0x20 + 4 * u16(n + 8);
        case TRANSLATE:
        case ROTATE:
        case SCALE:
        {
            const uint32_t k = u32(n + 8);
            return k > 8192 ? 0 : (op(n) == TRANSLATE ? 0x1C : op(n) == ROTATE ? 0x50 : 0x2C) + 4 * k;
        }
        case SWITCH: return 0xC + 4 * u8(n + 0xB);
        case LOD: return u32(n + 4) > 16 ? 0 : 0x20 + 16 * u32(n + 4);
        case CONTROL: return std::max<uint32_t>(0x30, 0x20 + 4 * u8(n + 0xC));
        case INTERACTIVE: return 16;
        case DESTRUCTIBLE: return u16(n + 0xA) > 64 ? 0 : 0xC + 12 * u16(n + 0xA);
        case PART_EMITTER: return 0xB0;
        case CHAR_INSTANCE:
        case ANIM_XFORM:
        case ANIM_SCALE:
        {
            const uint32_t k = u32(n + (op(n) == CHAR_INSTANCE ? 0x28 : 4));
            return k > 8192 ? 0 : (op(n) == CHAR_INSTANCE ? 0x2C : op(n) == ANIM_XFORM ? 0xCC : 0x60) + 4 * k;
        }
        default: return 0;
        }
    }

    // ------------------------------------------------------------------ partes animadas (animation.cpp; lvl.py, mesmos nomes)
    // CHAR_INSTANCEs da lista do ANIM_MGR da fase, na ordem em que o animationGetHandle as procura
    std::vector<uint32_t> instances() const
    {
        std::vector<uint32_t> out;
        if (!mgr || !ok(mgr, 8)) return out;
        for (uint32_t k = 0; k < std::min<uint32_t>(u32(mgr + 4), 4096); ++k)
            if (const uint32_t ci = u32(mgr + 8 + 4 * k) & kMask; ok(ci, 0x2C)) out.push_back(ci);
        return out;
    }
    // A CHAR_INSTANCE que o animationGetHandle(id1, id2, anim) da fase acha (a primeira com os ids e uma animacao com esse id), ou 0
    uint32_t resolveAnim(uint32_t id1, uint32_t id2, uint32_t anim) const
    {
        for (uint32_t ci : instances())
        {
            if (ident(ci) != id1 || ((u32(ci) >> 7) & kId2Max) != id2) continue;
            const uint32_t chr = u32(ci + 0x20) & kMask;
            for (uint32_t k = 0; k < std::min<uint32_t>(u16(chr), 256); ++k)
                if (ident(u32(chr + 0xC + 4 * k) & kMask) == anim) return ci;
        }
        return 0;
    }
    // CHAR_INSTANCE que o ACT_ANIM a (de fase, nao de monstro) anima, ou 0
    uint32_t animTarget(uint32_t a) const
    {
        if (u16(a + 0xC) & kActAnimMonster) return 0;
        return resolveAnim(u32(a + 8), u16(a + 0xC) & kId2Max, u16(a + 0xE));
    }
    // Bytes do estado de animacao proprio de um clone da CHAR_INSTANCE ci (cloneAnimState), com o alinhamento de cada bloco
    int64_t animStateCost(uint32_t ci) const
    {
        const uint32_t chr = u32(ci + 0x20) & kMask, nan = u16(chr), nch = u16(chr + 2), ctx = u32(ci + 0x10) & kMask;
        int64_t c = 4 * nch + 15 + (nch >> 3) + 1 + 3 + 64 * u16(chr + 4) + 15 + 0x3C * nan + 15;
        for (uint32_t k = 0; k < nan; ++k)
            if (u32(ctx + 0x3C * k + 0x30) & kMask) c += 2 * u16((u32(ctx + 0x3C * k + 4) & kMask) + 0xC) + 3;
        return c;
    }
    // id2 que nenhuma CHAR_INSTANCE com esse id1 usa, de cima para baixo (o dupId dos monstros e pequeno)
    uint32_t freshId2(uint32_t id1)
    {
        std::set<uint32_t> &used = id2Used[id1];
        for (uint32_t v = kId2Max; v > 0; --v)
            if (used.insert(v).second) return v;
        throw PatchError(fmt("sem id2 livre para as CHAR_INSTANCEs de id1 %x", id1));
    }
    // O clone a da CHAR_INSTANCE n ganha estado de animacao proprio: saida (val, dirty, atMat), control nodes e o cache de chaves de
    // cada um, copiados do disco; activeTree 0 (o animationSetGlobal cria o AnimPlayer) e id2 novo (o ACT_ANIM da copia o procura por ele)
    void cloneAnimState(uint32_t n, uint32_t a)
    {
        const uint32_t chr = u32(n + 0x20) & kMask, nan = u16(chr), nch = u16(chr + 2);
        auto dup = [&](uint32_t field, uint32_t size, uint32_t align) {
            const uint32_t d = alloc(size, align);
            copyBytes(d, u32(n + field) & kMask, size);
            setPtr(a + field, d);
            return d;
        };
        const uint32_t val = dup(4, 4 * nch, 16);
        dup(8, (nch >> 3) + 1, 4);
        dup(0xC, 64 * u16(chr + 4), 16);   // uma matriz por ANIM_XFORM (matrixIdx)
        const uint32_t ctx = dup(0x10, 0x3C * nan, 16);
        for (uint32_t k = 0; k < nan; ++k)
            if (const uint32_t c = ctx + 0x3C * k, pk = u32(c + 0x30) & kMask; pk)
            {
                const uint32_t size = 2 * u16((u32(c + 4) & kMask) + 0xC), d = alloc(size, 4);
                copyBytes(d, pk, size);
                setPtr(c + 0x30, d);
            }
        if ((u32(n + 0x18) & kMask) == (u32(n + 4) & kMask)) setPtr(a + 0x18, val);   // animPktPtr = val nas fases (sem esqueleto)
        w32(a + 0x24, 0);
        w32(a, (u32(a) & ~(kId2Max << 7)) | (freshId2(ident(n)) << 7));
        animAdded.push_back(a);
        ++stats["anim instances cloned"];
    }
    // ANIM_MGR novo na arena com a lista antiga e as CHAR_INSTANCEs clonadas, no lugar da raiz antiga
    void registerInstances()
    {
        if (animAdded.empty()) return;
        const uint32_t n = std::min<uint32_t>(u32(mgr + 4), 4096), m = alloc(8 + 4 * (n + uint32_t(animAdded.size())), 16);
        copyBytes(m, mgr, 8 + 4 * n);
        const uint32_t hiBits = n ? u32(mgr + 8) & ~kMask : 0;
        for (size_t k = 0; k < animAdded.size(); ++k) w32(m + 8 + 4 * (n + uint32_t(k)), hiBits | animAdded[k]);
        w32(m + 4, n + uint32_t(animAdded.size()));
        for (uint32_t i = 0; i < u32(kBase); ++i)
            if ((u32(kBase + 4 + 4 * i) & kMask) == mgr) setPtr(kBase + 4 + 4 * i, m);
        std::replace(roots.begin(), roots.end(), mgr, m);
        mgr = m;
        animAdded.clear();
    }
    // Blocos de acao dos estados do DESTRUCTIBLE d, cada um uma vez: GROUP 0, TRANSFORM 1, DEBRIS 3, os outros 0x10 (ActionDispatch.cpp)
    struct ActBlock { uint32_t a, type, size; };
    void walkAct(uint32_t a, int depth, std::vector<ActBlock> &out, std::unordered_set<uint32_t> &seen) const
    {
        a &= kMask;
        if (!a || seen.count(a) || !ok(a, 8) || depth > 16) return;
        seen.insert(a);
        const uint32_t t = u16(a);
        if (t == 0)
        {
            const uint32_t n = std::min<uint32_t>(u32(a + 8), 256);
            out.push_back({a, t, 0xC + 4 * n});
            for (uint32_t k = 0; k < n; ++k) walkAct(u32(a + 0xC + 4 * k), depth + 1, out, seen);
        }
        else if (t == 1)
        {
            out.push_back({a, t, 0x50});
            walkAct(u32(a + 8), depth + 1, out, seen);
        }
        else if (t == 3)
            out.push_back({a, t, 0x10 + 0x30 * std::min<uint32_t>(u32(a + 8), 256)});
        else
            out.push_back({a, t, 0x10});
    }
    std::vector<ActBlock> actBlocks(uint32_t d) const
    {
        std::vector<ActBlock> out;
        std::unordered_set<uint32_t> seen;
        for (uint32_t k = 0; k < std::min<uint32_t>(u16(d + 0xA), 64); ++k) walkAct(u32(d + 0x10 + 12 * k), 0, out, seen);
        return out;
    }
    // Pedacos de destroco que viram pickup ao parar (cada um toma um Interactive na luta)
    int64_t debrisPickups(uint32_t d) const
    {
        int64_t n = 0;
        for (const ActBlock &b : actBlocks(d))
            if (b.type == 3)
                for (uint32_t k = 0; k < std::min<uint32_t>(u32(b.a + 8), 256); ++k) n += ((u16(b.a + 0x10 + 0x30 * k + 0xE) >> 5) & 31) != 0;
        return n;
    }
    bool registersId(uint32_t n) const { const uint32_t i = ident(n); return cbOp(op(n)) && i && !(i >= kExclLo && i < kExclHi); }
    // No que guarda estado por objeto: emissor (isActivated, matriz), SWITCH (whichChild), INTERACTIVE_STATE, DESTRUCTIBLE, id registrado
    bool mutableNode(uint32_t n) const
    {
        const uint32_t o = op(n);
        return o == PART_EMITTER || o == SWITCH || o == INTERACTIVE || o == DESTRUCTIBLE || registersId(n);
    }

    // Interactives que o dbsTraverse registra abaixo de cada no (uma vez por caminho de descida): na carga e pedacos-pickup no pior caso
    struct Weight { int64_t load = 0, deb = 0; bool done = false; };
    std::vector<uint32_t> travKids(uint32_t n) const { return op(n) == ANIM_SCALE ? std::vector<uint32_t>{} : children(n); }
    struct Frame { uint32_t n; std::vector<uint32_t> ks; size_t i; uint32_t owner = 0; };   // owner: CHAR_INSTANCE mais perto acima (spine)
    void weigh(uint32_t r, std::unordered_map<uint32_t, Weight> &memo) const
    {
        if (memo.count(r)) return;
        memo[r];
        std::vector<Frame> stack;
        stack.push_back({r, travKids(r), 0});
        while (!stack.empty())
        {
            Frame &top = stack.back();
            if (top.i < top.ks.size())
            {
                const uint32_t c = top.ks[top.i++];
                if (!memo.count(c))
                {
                    memo[c];
                    stack.push_back({c, travKids(c), 0});
                }
                continue;
            }
            const uint32_t n = top.n, o = op(n);
            Weight w;
            w.load = o == INTERACTIVE || (cbOp(o) && ident(n) >= kPickupLo && ident(n) < kPickupHi);
            w.deb = o == DESTRUCTIBLE ? debrisPickups(n) : 0;
            for (uint32_t c : top.ks)
                if (const Weight &k = memo[c]; k.done) w.load += k.load, w.deb += k.deb;
            w.done = true;
            memo[n] = w;
            stack.pop_back();
        }
    }
    // (tomados na carga com o slot 0, pedacos-pickup no pior caso, folga): a soma tem de caber em 1024
    std::array<int64_t, 3> interactives() const
    {
        std::unordered_map<uint32_t, Weight> memo;
        int64_t load = 1, deb = 0;
        for (uint32_t r : roots)
            if (op(r) != ANIM_MGR)
            {
                weigh(r, memo);
                load += memo[r].load, deb += memo[r].deb;
            }
        return {load, deb, kInteractiveSpare};
    }

    // Abaixo de um SKEL_BONE (esqueleto: matrizes de pele pelo animMatrixPtr) a copia nao clona; destrutivel la dentro e recusado
    void underBone(uint32_t h, uint32_t a, std::unordered_set<uint32_t> &seen, int &left) const
    {
        std::vector<uint32_t> stack{a};
        while (!stack.empty())
        {
            const uint32_t x = stack.back();
            stack.pop_back();
            if (!seen.insert(x).second) continue;
            if (op(x) == INTERACTIVE)
                throw PatchError(fmt("copia de %07x: o destrutivel %07x fica dentro de um esqueleto (%s %07x)", h, x, opName(op(a)), a));
            left += op(x) == PART_EMITTER;
            for (uint32_t c : children(x)) stack.push_back(c);
        }
    }
    // CHAR_INSTANCEs da subarvore de h que um ACT_ANIM dos destrutiveis dela anima
    std::unordered_set<uint32_t> animTargets(uint32_t h) const
    {
        const std::unordered_set<uint32_t> sub = subtree(h);
        std::unordered_set<uint32_t> out;
        for (uint32_t n : sub)
            if (op(n) == DESTRUCTIBLE)
                for (const ActBlock &b : actBlocks(n))
                    if (b.type == 2)
                        if (const uint32_t ci = animTarget(b.a); ci && sub.count(ci)) out.insert(ci);
        return out;
    }
    // Uma passada da espinha: no mutavel, CHAR_INSTANCE alvo de um ACT_ANIM da copia e, com `cis`, ANIM_XFORM/ANIM_SCALE de uma
    // CHAR_INSTANCE de `cis` (a mais perto acima dele: o desenho usa a saida dela) puxam o no e os de cima ate h
    std::vector<uint32_t> spineWalk(uint32_t h, int &left, const std::unordered_set<uint32_t> &targets, const std::unordered_set<uint32_t> *cis) const
    {
        std::unordered_map<uint32_t, int8_t> memo;   // 0 em curso, 1 nao, 2 sim
        std::unordered_set<uint32_t> seen;
        std::vector<uint32_t> order;
        std::vector<Frame> stack;
        left = 0;
        memo[h] = 0;
        stack.push_back({h, children(h), 0, 0});
        while (!stack.empty())
        {
            Frame &top = stack.back();
            if (top.i < top.ks.size())
            {
                const uint32_t c = top.ks[top.i++];
                if (!memo.count(c))
                {
                    memo[c] = 0;
                    if (op(c) == SKEL_BONE)
                    {
                        if (cis && cis->count(top.owner))
                            throw PatchError(fmt("copia de %07x: a CHAR_INSTANCE %07x tem esqueleto (SKEL_BONE %07x)", h, top.owner, c));
                        underBone(h, c, seen, left);
                        memo[c] = 1;
                    }
                    else
                    {
                        const uint32_t owner = op(c) == CHAR_INSTANCE ? c : top.owner;
                        stack.push_back({c, children(c), 0, owner});
                    }
                }
                continue;
            }
            const uint32_t n = top.n;
            bool need = n == h || mutableNode(n) || targets.count(n) || (cis && animStateOp(op(n)) && cis->count(top.owner));
            for (uint32_t c : top.ks) need = need || memo[c] == 2;
            stack.pop_back();
            memo[n] = need ? 2 : 1;
            if (!need) continue;
            if (!nodeSize(n)) throw PatchError(fmt("copia de %07x: %s %07x, no caminho do que ela clona, nao tem tamanho conhecido", h, opName(op(n)), n));
            order.push_back(n);
        }
        return order;
    }
    // O que a copia profunda da alca h clona, filhos antes: h e todo no da subarvore com um no mutavel nele ou abaixo dele. Uma
    // CHAR_INSTANCE entra quando um ACT_ANIM da copia a anima ou ha no mutavel abaixo dela; ai os ANIM_XFORM/ANIM_SCALE dela entram
    // tambem (guardam visivel, rotacao e bits de sujo da instancia). O que pende de um SKEL_BONE fica em comum (`left` = emissores la).
    std::vector<uint32_t> spine(uint32_t h, int &left) const
    {
        const std::unordered_set<uint32_t> targets = animTargets(h);
        std::vector<uint32_t> order = spineWalk(h, left, targets, nullptr);
        std::unordered_set<uint32_t> cis;
        for (uint32_t n : order)
            if (op(n) == CHAR_INSTANCE) cis.insert(n);
        return cis.empty() ? order : spineWalk(h, left, targets, &cis);
    }
    // A acao a para uma copia: DOMINO que mira o proprio destrutivel do original vai para o da copia, os outros saem (quebrariam o
    // vizinho do original); ANIM de uma CHAR_INSTANCE clonada vai para o clone, as outras globalAct saem; os GROUP/TRANSFORM acima de
    // um que mudou sao clonados. 0 = nada sobrou
    uint32_t rewriteAct(uint32_t a, const std::unordered_map<uint32_t, uint32_t> &twins, std::unordered_map<uint32_t, uint32_t> &memo, int depth)
    {
        if (!a || !ok(a, 8) || depth > 16) return a;
        if (const auto it = memo.find(a); it != memo.end()) return it->second;
        memo[a] = a;
        const uint32_t t = u16(a);
        uint32_t out = a;
        if (t == 8)
        {
            if (const auto it = twins.find(u32(a + 0xC) & kMask); it != twins.end())
            {
                out = alloc(0x10, 16);
                copyBytes(out, a, 0x10);
                setPtr(out + 0xC, it->second);
                ++stats["dominoes redirected"];
            }
            else
                out = 0, ++stats["dominoes dropped"];
        }
        else if (t == 2 && twins.count(animTarget(a)))
        {   // ANIM da parte animada da propria copia: vai para o clone, pelo id2 dele
            out = alloc(0x10, 16);
            copyBytes(out, a, 0x10);
            w16(out + 0xC, uint16_t((u16(a + 0xC) & ~kId2Max) | ((u32(twins.at(animTarget(a))) >> 7) & kId2Max)));
            ++stats["anim actions redirected"];
        }
        else if (globalAct(t))
            out = 0, ++stats["global actions dropped"];
        else if (t == 0)
        {
            std::vector<uint32_t> words, nw, keep;
            for (uint32_t k = 0; k < std::min<uint32_t>(u32(a + 8), 256); ++k) words.push_back(u32(a + 0xC + 4 * k));
            bool changed = false;
            for (uint32_t w : words)
            {
                nw.push_back(rewriteAct(w & kMask, twins, memo, depth + 1));
                changed = changed || nw.back() != (w & kMask);
            }
            if (changed)
            {
                for (size_t k = 0; k < words.size(); ++k)
                    if (nw[k]) keep.push_back(nw[k] == (words[k] & kMask) ? words[k] : nw[k]);
                out = alloc(0xC + 4 * uint32_t(keep.size()), 16);
                copyBytes(out, a, 8);
                w32(out + 8, uint32_t(keep.size()));
                for (size_t k = 0; k < keep.size(); ++k) w32(out + 0xC + 4 * uint32_t(k), keep[k]);
            }
        }
        else if (t == 1)
        {
            const uint32_t w = u32(a + 8);
            uint32_t x = rewriteAct(w & kMask, twins, memo, depth + 1);
            if (x != (w & kMask))
            {
                out = alloc(0x50, 16);   // alinhado a 16: o vu0CopyMatrix le a matriz com lq
                copyBytes(out, a, 0x50);
                if (!x) x = alloc(0xC, 16);   // GROUP vazio: o doTransform nao testa nulo
                w32(out + 8, x);
            }
        }
        memo[a] = out;
        return out;
    }
    // Clona `order` (filhos antes) na arena, twins = original -> clone: os ponteiros de filho vao para os clones, ids registrados uma vez
    // vao a 0, emissores comecam apagados, cada CHAR_INSTANCE ganha estado de animacao e id2 proprios e as acoes de cada DESTRUCTIBLE
    // sao refeitas
    void deepClone(const std::vector<uint32_t> &order, std::unordered_map<uint32_t, uint32_t> &twins)
    {
        for (uint32_t n : order)
        {
            const uint32_t size = nodeSize(n), a = alloc(size, 16);
            copyBytes(a, n, size);
            twins[n] = a;
            ++stats["nodes cloned"];
        }
        for (uint32_t n : order)
        {
            const uint32_t a = twins[n], o = op(n), id = ident(n);
            for (uint32_t s : childSlots(n))
                if (const auto it = twins.find(u32(s) & kMask); it != twins.end())
                {
                    setPtr(a + (s - n), it->second);
                    if (parents[it->second].empty()) parents[it->second] = {a};
                }
            if (o == INTERACTIVE)
                ++stats["destructibles copied"];
            else if (o == CHAR_INSTANCE)
                cloneAnimState(n, a);
            else if (o == PART_EMITTER)
            {
                w32(a + 0x5C, u32(n + 0x5C) & ~0x10u);   // isActivated
                ++stats["emitters cloned"];
                if (u32(n + 0x5C) & 8) ++stats["auto-start emitters cloned"];   // isAutoStart: um fx do fx[100] desde a carga e uma vaga do autoEmitters[128]
            }
            // id1 0: as pecas especiais da fase (chamines da 3-MILE, SANFRAN, colunas) e o que se registra uma vez ficam com o original;
            // id de pickup fica (o createPickup le o tipo), e o "nao perde vida" do DESTRUCTIBLE tambem
            const bool keep = (o == DESTRUCTIBLE && id >= kKeepLo && id < kKeepHi) ||
                              (o != INTERACTIVE && o != DESTRUCTIBLE && (!registersId(n) || (id >= kPickupLo && id < kPickupHi)));
            if (id && !keep) w32(a, u32(n) & 0x3FFFFu), ++stats["ids cleared"];
        }
        std::unordered_map<uint32_t, uint32_t> memo;
        for (uint32_t n : order)
            if (op(n) == DESTRUCTIBLE)
                for (uint32_t k = 0; k < std::min<uint32_t>(u16(n + 0xA), 64); ++k)
                {
                    const uint32_t slot = twins[n] + 0x10 + 12 * k, act = u32(slot) & kMask;
                    if (const uint32_t na = rewriteAct(act, twins, memo, 0); na != act) w32(slot, na);
                }
    }

    // O maximo que uma entrada pode tomar, conferido antes de escrever (entrada recusada nao deixa nada): celulas pela diagonal da caixa
    int64_t cellsBound(const Path &p) const
    {
        const int n = int(std::floor((std::hypot(p.box.mx.v[0] - p.box.mn.v[0], p.box.mx.v[1] - p.box.mn.v[1]) + 2.0) / cell)) + 2;
        return int64_t(std::min(n, cols)) * std::min(n, rows);
    }
    int64_t actsBound(uint32_t d) const
    {
        int64_t b = 0;
        for (const ActBlock &k : actBlocks(d))
            if (k.type == 0 || k.type == 1 || k.type == 2 || k.type == 8) b += k.size + 15 + (k.type == 1 ? 0xC + 15 : 0);
        return b;
    }
    int64_t privateCost(uint32_t n, bool top) const
    {
        uint32_t head, count;
        switch (op(n))
        {
        case GROUP: head = 0x20, count = u16(n + 8); break;
        case SWITCH: head = 0xC, count = u8(n + 0xB); break;
        case TRANSLATE: head = 0x1C, count = u32(n + 8); break;
        case ROTATE: head = 0x50, count = u32(n + 8); break;
        default: return 0;
        }
        if (const auto it = parents.find(n); !top && (it == parents.end() || it->second.size() != 1)) return 0;
        int64_t c = head + 4 * std::min<uint32_t>(count, 8192) + 15;
        for (uint32_t k : children(n)) c += privateCost(k, false);
        return c;
    }
    int64_t moveCost(uint32_t h, bool turns) const
    {
        int64_t c = op(h) == TRANSLATE && turns ? 0x50 + 4 * int64_t(u32(h + 8)) + 15 : 0;
        for (uint32_t pa : pathsThrough(h)) c += 8 * cellsBound(path(pa));
        return c;
    }
    int64_t copyCost(uint32_t h, size_t n, const std::vector<uint32_t> &order, bool pickup) const
    {
        int64_t per = 0, cis = 0;
        if (pickup)
            per = privateCost(h, true);
        else
            for (uint32_t x : order)
            {
                per += nodeSize(x) + 15 + (op(x) == DESTRUCTIBLE ? actsBound(x) : 0) + (op(x) == CHAR_INSTANCE ? animStateCost(x) : 0);
                cis += op(x) == CHAR_INSTANCE;
            }
        if (op(h) == TRANSLATE) per += 0x50 + 4 * int64_t(u32(h + 8)) + 15;   // toRotate, quando a copia gira
        for (uint32_t pa : pathsThrough(h))
        {
            const Path p = path(pa);
            per += 0x24 + 4 * p.len + 3 + 8 * cellsBound(p);
        }
        const int64_t mgrCost = cis ? 8 + 4 * (int64_t(instances().size()) + cis * int64_t(n)) + 15 : 0;   // registerInstances
        return 0x20 + 4 * int64_t(1 + n) + 15 + per * int64_t(n) + mgrCost;
    }

    // Copias do objeto da alca h, cada uma no seu lugar; um GROUP no lugar do original no pai segura o original e as copias. Pickup:
    // alca, GROUP do id, SWITCH e os GROUPs entre eles proprios (clonePrivate); o resto, copia profunda: alca propria e todo no que
    // guarda estado por objeto com os nos entre eles (spine), com malhas, colisao, contextos do GS e acoes em comum. Cada copia ganha os
    // seus caminhos de colisao; as CHAR_INSTANCEs clonadas entram num ANIM_MGR novo. `extra` e o que o movimento da mesma entrada toma
    // depois.
    void copy(uint32_t h, const std::vector<Place> &places, int64_t extra)
    {
        uint32_t ist = 0;
        bool pickup = false;
        for (uint32_t c : children(h))
        {
            if (op(c) == INTERACTIVE && !ist) ist = c;
            if (op(c) == GROUP && ident(c) >= kPickupLo && ident(c) < kPickupHi) pickup = true;
        }
        int left = 0;
        std::vector<uint32_t> order;
        if (pickup) {}
        else if (shallow)
        {
            for (uint32_t x : subtree(h))
                if (x != ist && op(x) == INTERACTIVE) throw PatchError(fmt("copia de %07x: tem destrutiveis dentro (a copia rasa nao copia)", h));
            if (ist) order = {u32(ist + 0xC) & kMask, ist};
            order.push_back(h);
        }
        else
            order = spine(h, left);
        if (!mgr && std::any_of(order.begin(), order.end(), [&](uint32_t x) { return op(x) == CHAR_INSTANCE; }))
            throw PatchError(fmt("copia de %07x: tem parte animada e a fase nao tem ANIM_MGR", h));
        const int64_t cost = copyCost(h, places.size(), order, pickup) + extra;
        if (cost > int64_t(arenaFree()))
            throw PatchError(fmt("copia de %07x: %zu copias levam ate %lld bytes, a arena tem %u livres", h, places.size(), (long long)cost, arenaFree()));
        std::unordered_map<uint32_t, Weight> wm;
        weigh(h, wm);
        const int64_t need = int64_t(places.size()) * (wm[h].load + wm[h].deb);
        if (interUsed + need > kInteractives)
            throw PatchError(fmt("copia de %07x: %zu copias levam %lld interactives, %lld de %lld ja tomados", h, places.size(), (long long)need,
                                 (long long)interUsed, (long long)kInteractives));
        interUsed += need;
        const uint32_t p = parentOf(h);
        const M4 before = world(h, false);
        const V3 oldPos = pos3(world(h));
        const std::vector<uint32_t> paths = pathsThrough(h);
        const uint32_t wrap = alloc(0x20 + 4 * uint32_t(1 + places.size()), 16);
        const uint32_t po = op(p);
        w32(wrap, GROUP);
        w32(wrap + 4, po == GROUP || po == TRANSLATE || po == ROTATE || po == SCALE ? u32(p + 4) : 0);
        w16(wrap + 8, 1);     // numKids sobe quando cada copia fica pronta
        w16(wrap + 0xA, 7);   // visual | collision | noFOV
        w32(wrap + 0x20, h);
        for (uint32_t s : childSlots(p))
            if ((u32(s) & kMask) == h) setPtr(s, wrap);
        parents[wrap] = {p};
        parents[h] = {wrap};
        wraps.push_back(wrap);
        for (size_t i = 0; i < places.size(); ++i)
        {
            std::unordered_map<uint32_t, uint32_t> twins;
            uint32_t hc;
            if (pickup)   // LevelPickups/Pickup::init escrevem no GROUP do id (id2, numKids) e no SWITCH (whichChild)
                hc = clonePrivate(h, true);
            else
                deepClone(order, twins), hc = twins[h];
            parents[hc] = {wrap};
            hc = place(hc, places[i].pos, places[i].heading);
            w32(wrap + 0x24 + 4 * uint32_t(i), hc);   // depois do place: o ROTATE novo toma o lugar do TRANSLATE
            twins[h] = hc;
            const M4 carry = carryFrom(before, hc);
            for (uint32_t pa : paths)
            {
                const Path q = path(pa);
                const uint32_t np = alloc(0x24 + 4 * uint32_t(q.len));
                copyBytes(np, pa, 0x24);
                w32(np + 0x18, 0);
                for (size_t k = 0; k < q.nodes.size(); ++k)
                {
                    const auto it = twins.find(q.nodes[k]);
                    w32(np + 0x24 + 4 * uint32_t(k), (u32(pa + 0x24 + 4 * uint32_t(k)) & ~kMask) | (it == twins.end() ? q.nodes[k] : it->second));
                }
                redo(np, carry);
                ++stats["paths added"];
            }
            growSpheres(hc, oldPos);
            w16(wrap + 8, uint16_t(2 + i));
            ++stats["copies"];
        }
        registerInstances();
        if (left) stats["emitters left shared"] += left * int(places.size());
    }

    static bool num3(const json::Value *v, V3 &out)
    {
        if (!v || v->type != json::Value::Arr || v->arr.size() != 3) return false;
        for (int i = 0; i < 3; ++i)
        {
            if (!v->arr[i].isInt()) return false;
            out.v[i] = v->arr[i].asNum();
        }
        return true;
    }
    static double headingOf(const json::Value &e)
    {
        const json::Value *h = e.get("heading");
        return h && h->isInt() ? h->asNum() : std::nan("");
    }
    // "grid": {"min": [x, y], "max": [x, y]}: grade de colisao maior, na arena (lvl.py LevelPatch.apply_grid, mesma conta e mesma ordem):
    // celulas do mesmo tamanho, colunas = piso(largura / celula) + 1, cada caminho listado de novo nas celulas da sua caixa, a raiz da
    // imagem apontando para a grade nova, os nos da lista velha reaproveitados. Valida tudo antes de escrever
    void applyGrid(const json::Value &g)
    {
        const auto xy = [](const json::Value *v, double (&o)[2]) {
            if (!v || v->type != json::Value::Arr || v->arr.size() != 2 || !v->arr[0].isInt() || !v->arr[1].isInt()) return false;
            o[0] = v->arr[0].asNum(), o[1] = v->arr[1].asNum();
            return std::isfinite(o[0]) && std::isfinite(o[1]);
        };
        double lo[2], hi2[2];
        if (g.type != json::Value::Obj || !xy(g.get("min"), lo) || !xy(g.get("max"), hi2)) throw PatchError("\"grid\" precisa de \"min\" e \"max\" como [x, y]");
        if (lo[0] > minX || lo[1] > minY || hi2[0] < maxX || hi2[1] < maxY)
            throw PatchError(fmt("\"grid\" (%.0f %.0f)-(%.0f %.0f) nao contem a da fase (%.0f %.0f)-(%.0f %.0f)", lo[0], lo[1], hi2[0], hi2[1], minX, minY, maxX, maxY));
        const double wc = std::floor((hi2[0] - lo[0]) / cell) + 1, wr = std::floor((hi2[1] - lo[1]) / cell) + 1;
        if (wc * wr > 65536.0) throw PatchError(fmt("\"grid\" de %.0f x %.0f celulas: no maximo 65536 celulas", wc, wr));
        const int nc = int(wc), nr = int(wr);
        const uint32_t need = 0x20 + 4 * uint32_t(nc * nr) + 15;
        if (need > arenaFree()) throw PatchError(fmt("\"grid\" de %d x %d celulas leva %u bytes, a arena tem %u livres", nc, nr, need, arenaFree()));
        const uint32_t old = grid;
        const int oc = cols, orow = rows;
        std::vector<uint32_t> nodes;   // os nos das listas velhas, celula a celula, cabeca primeiro
        for (int i = 0; i < cols * rows; ++i)
        {
            uint32_t ln = u32(old + 0x20 + 4 * i) & kMask;
            for (int guard = 0; ln && ok(ln, 8) && guard < 100000; ++guard) nodes.push_back(ln), ln = u32(ln) & kMask;
        }
        const uint32_t nu = alloc(0x20 + 4 * uint32_t(nc * nr), 16);
        copyBytes(nu, old, 0x20);
        w16(nu + 4, uint16_t(nc)), w16(nu + 6, uint16_t(nr));
        wf32(nu + 8, cell), wf32(nu + 0xC, lo[0]), wf32(nu + 0x10, lo[1]), wf32(nu + 0x14, hi2[0]), wf32(nu + 0x18, hi2[1]);
        const uint32_t nroots = u32(kBase);
        for (uint32_t i = 0; i < nroots && i < 4096; ++i)
            if ((u32(kBase + 4 + 4 * i) & kMask) == old) setPtr(kBase + 4 + 4 * i, nu);
        for (uint32_t &r : roots)
            if (r == old) r = nu;
        grid = nu, cols = nc, rows = nr;
        cell = f32(grid + 8), minX = f32(grid + 0xC), minY = f32(grid + 0x10), maxX = f32(grid + 0x14), maxY = f32(grid + 0x18);
        freeNodes.insert(freeNodes.begin(), nodes.rbegin(), nodes.rend());
        const int before = stats["new list nodes"];
        for (auto &kv : cells)
        {
            const std::set<int> cs = cellsFor(path(kv.first).box);
            for (int c : cs) link(c, kv.first);
            kv.second = cs;
        }
        stats["grid cells"] = nc * nr;
        log.push_back(fmt("grid: %d x %d celulas (eram %d x %d), x [%.0f, %.0f] y [%.0f, %.0f]; %zu caminhos listados de novo, %d nos novos", nc, nr, oc, orow,
                          lo[0], maxX, lo[1], maxY, cells.size(), stats["new list nodes"] - before));
    }
    void apply(const json::Value &patch)
    {
        if (const json::Value *gs = patch.get("grid"))
        {
            try
            {
                applyGrid(*gs);
            }
            catch (const PatchError &e)
            {   // nada foi escrito: os objetos seguem com a grade do disco
                log.push_back(fmt("grid pulada: %s", e.what()));
            }
        }
        const json::Value *objs = patch.get("objects");
        if (!objs) return;   // so "props" ou "peds" (o lvl.py le a lista que falta como vazia)
        if (objs->type != json::Value::Arr) throw PatchError("\"objects\" nao e uma lista");
        for (size_t k = 0; k < objs->arr.size(); ++k)
        {
            const json::Value &e = objs->arr[k];
            try
            {
                const json::Value *hv = e.get("handle");
                uint32_t h = 0;
                if (hv && hv->type == json::Value::Str) h = uint32_t(std::strtoul(hv->s.c_str(), nullptr, 0));
                else if (hv && hv->isInt()) h = uint32_t(hv->asInt());
                if (!inTree.count(h) || (op(h) != TRANSLATE && op(h) != ROTATE))
                    throw PatchError(fmt("a alca %07x nao e um TRANSLATE/ROTATE da fase", h));
                if (parents[h].size() != 1) throw PatchError(fmt("a alca %07x tem %zu pais", h, parents[h].size()));
                const V3 pos = pos3(world(h));
                V3 from;
                if (!num3(e.get("from"), from)) throw PatchError("sem \"from\" [x, y, z]");
                for (int i = 0; i < 3; ++i)
                    if (std::fabs(pos.v[i] - from.v[i]) > kFromTol)
                        throw PatchError(fmt("a alca %07x esta em (%.1f %.1f %.1f), nao no \"from\" (%.1f %.1f %.1f): outro .NGP?", h, pos.v[0],
                                             pos.v[1], pos.v[2], from.v[0], from.v[1], from.v[2]));
                if (children(h).size() > 1) log.push_back(fmt("item %zu: a alca %07x segura %zu objetos; vao todos", k, h, children(h).size()));
                const json::Value *del = e.get("delete");
                const bool moving = !(del && del->asBool()) && (e.get("pos") || e.get("heading"));
                const int64_t extra = moving ? moveCost(h, !std::isnan(headingOf(e))) : 0;
                if (const json::Value *cp = e.get("copies"); cp && cp->type == json::Value::Arr && !cp->arr.empty())
                {
                    std::vector<Place> places;
                    for (const json::Value &c : cp->arr)
                    {
                        Place pl;
                        if (!num3(c.get("pos"), pl.pos)) throw PatchError("copia sem \"pos\" [x, y, z]");
                        pl.heading = headingOf(c);
                        places.push_back(pl);
                    }
                    copy(h, places, extra);
                }
                else if (extra > int64_t(arenaFree()))
                    throw PatchError(fmt("alca %07x: o movimento leva ate %lld bytes, a arena tem %u livres", h, (long long)extra, arenaFree()));
                if (del && del->asBool())
                    erase(h);
                else if (moving)
                {
                    V3 to = pos;
                    if (e.get("pos") && !num3(e.get("pos"), to)) throw PatchError("\"pos\" nao e [x, y, z]");
                    move(h, to, headingOf(e));
                }
            }
            catch (const PatchError &err)
            {
                log.push_back(fmt("item %zu pulado: %s", k, err.what()));
                ++stats["skipped"];
            }
        }
        if (stats.count("deleted")) cutIncoming();
        if (!objs->arr.empty())
        {
            const std::array<int64_t, 3> b = interactives();
            log.push_back(fmt("interactives: %lld de %lld antes do patch, %lld depois (%lld na carga, %lld pedacos de destroco com pickup no "
                              "pior caso, %lld de folga)", (long long)(interBefore[0] + interBefore[1] + interBefore[2]), (long long)kInteractives,
                              (long long)(b[0] + b[1] + b[2]), (long long)b[0], (long long)b[1], (long long)b[2]));
        }
    }

    // Regras do disco sobre a grade inteira (lvl.py check_collision): cada caixa pela regra, cada caminho nas celulas da sua caixa
    std::string check() const
    {
        int boxOk = 0, boxBad = 0, cellOk = 0, skipped = 0;
        double worst = 0.0;
        for (const auto &[a, cs] : cells)
        {
            const Path p = path(a);
            if (cellsFor(p.box) == cs) ++cellOk;
            Box bb;
            if (!pathBox(p, bb)) { ++skipped; continue; }
            double e = 0.0;
            for (int i = 0; i < 3; ++i) e = std::max({e, std::fabs(bb.mn.v[i] - p.box.mn.v[i]), std::fabs(bb.mx.v[i] - p.box.mx.v[i])});
            worst = std::max(worst, e);
            (e < 1.01 ? boxOk : boxBad)++;
        }
        return fmt("regra da caixa: %d certas, %d fora, %d sem refazer (pior %.2f); regra das celulas: %d de %zu caminhos", boxOk, boxBad,
                   skipped, worst, cellOk, cells.size());
    }
};

// ------------------------------------------------------------------ redes de carros e pedestres (lvl.py cut_nets)
// Veiculos e pedestres andam na PathNet::fodder e na ::ped sem olhar a colisao. Elas ja estao prontas aqui (InitBeforeDbLoad vem
// antes do LoadLevelFiles) e ninguem guarda ponteiro para elas ainda (os veiculos e os PedGroup nascem na travessia do dbInitDb).
// Link de veiculo cuja faixa (largura*10 de cada lado) cruza uma caixa nova sai nos dois sentidos, como no PathNode::removePath; o
// segmento de pedestre (dois nos ligados so um ao outro) fica com os trechos livres (os extras viram pares novos) ou sai, e os nos sem link saem
// da lista (o PedGroup le o paths[0] do no mais perto sem olhar a contagem). Nos: u16 numNodes em +0, 0x30 cada a partir de +0x10.
constexpr uint32_t kFodderNet = wotm::sym::x_7PathNet_fodder, kPedNet = wotm::sym::x_7PathNet_ped;
// Pedestres: so parede corta (caixa que sobe kPedWall acima do segmento; meio-fio e base baixa se pisam), a kPedMargin dela (os
// segmentos do disco ja passam rente as paredes); cada pedaco livre de kPedMinLen ou mais fica, o primeiro nos nos do segmento.
constexpr double kPedMinLen = 30.0, kPedMargin = 15.0, kPedWall = 40.0;
constexpr size_t kNetNodes = 255;
struct NetLink { uint16_t node; uint8_t width, drop; };
struct NetNode { std::array<uint8_t, 0x30> raw; V3 pos; uint8_t type; std::vector<NetLink> links; };

std::vector<NetNode> readNet(const uint8_t *rdram, uint32_t net)
{
    uint16_t n;
    std::memcpy(&n, rdram + net, 2);
    std::vector<NetNode> out(std::min<uint16_t>(n, 256));
    for (size_t i = 0; i < out.size(); ++i)
    {
        NetNode &d = out[i];
        std::memcpy(d.raw.data(), rdram + net + 0x10 + 0x30 * i, 0x30);
        float f[3];
        std::memcpy(f, d.raw.data(), 12);
        d.pos = {{f[0], f[1], f[2]}};
        d.type = d.raw[0x10];
        for (int j = 0; j < std::min<int>(d.raw[0x11], 6); ++j)
        {
            NetLink l;
            std::memcpy(&l.node, &d.raw[0x12 + 4 * j], 2);
            l.width = d.raw[0x14 + 4 * j], l.drop = d.raw[0x15 + 4 * j];
            d.links.push_back(l);
        }
    }
    return out;
}
void writeNet(uint8_t *rdram, uint32_t net, const std::vector<NetNode> &nodes)
{
    const uint16_t n = uint16_t(nodes.size());
    std::memcpy(rdram + net, &n, 2);
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        std::array<uint8_t, 0x30> r = nodes[i].raw;
        const float f[3] = {float(nodes[i].pos.v[0]), float(nodes[i].pos.v[1]), float(nodes[i].pos.v[2])};
        std::memcpy(r.data(), f, 12);
        r[0x11] = uint8_t(nodes[i].links.size());
        for (size_t j = 0; j < nodes[i].links.size(); ++j)
        {
            std::memcpy(&r[0x12 + 4 * j], &nodes[i].links[j].node, 2);
            r[0x14 + 4 * j] = nodes[i].links[j].width, r[0x15 + 4 * j] = nodes[i].links[j].drop;
        }
        std::memcpy(rdram + net + 0x10 + 0x30 * i, r.data(), 0x30);
    }
}

std::vector<std::string> cutNets(uint8_t *rdram, const std::vector<Footprint> &fps)
{
    std::vector<std::string> log;
    if (fps.empty()) return log;
    auto covered = [&](const V3 &a, const V3 &b, double margin, double wall = -1.0) {
        std::vector<std::pair<double, double>> out;
        for (const Footprint &fp : fps)
        {
            if (std::max(a.v[2], b.v[2]) < fp.zmin - 50.0 || std::min(a.v[2], b.v[2]) > fp.zmax) continue;
            if (wall >= 0.0 && fp.zmax - std::max(a.v[2], b.v[2]) < wall) continue;
            double t0, t1;
            if (segInPoly(a, b, fp.poly, margin, t0, t1)) out.push_back({t0, t1});
        }
        return out;
    };
    auto linked = [](const NetNode &n, size_t k) {
        return std::any_of(n.links.begin(), n.links.end(), [&](const NetLink &l) { return l.node == k; });
    };
    auto unlinkNodes = [](std::vector<NetNode> &nodes, size_t i, size_t k) {
        auto drop = [](NetNode &n, size_t other) {
            n.links.erase(std::remove_if(n.links.begin(), n.links.end(), [&](const NetLink &l) { return l.node == other; }), n.links.end());
        };
        drop(nodes[i], k), drop(nodes[k], i);
    };

    std::vector<NetNode> car = readNet(rdram, kFodderNet);
    std::vector<std::pair<size_t, size_t>> cut;
    for (size_t i = 0; i < car.size(); ++i)
    {
        if (car[i].type != 0 && car[i].type != 3) continue;   // 1 e 2 voam
        for (const NetLink &l : car[i].links)
            if (l.node < car.size() && (l.node > i || !linked(car[l.node], i)) && !covered(car[i].pos, car[l.node].pos, l.width * 10.0).empty())
                cut.push_back({i, l.node});
    }
    for (const auto &[i, k] : cut)
    {
        unlinkNodes(car, i, k);
        log.push_back(fmt("veiculos: link %zu-%zu cortado", i, k));
    }
    if (!cut.empty()) writeNet(rdram, kFodderNet, car);

    std::vector<NetNode> ped = readNet(rdram, kPedNet);
    bool pedChanged = false;
    const size_t pedCount = ped.size();   // os pares novos ja nascem fora de toda planta
    for (size_t i = 0; i < pedCount; ++i)
    {
        const std::vector<NetLink> links = ped[i].links;
        for (const NetLink &l : links)
        {
            const size_t k = l.node;
            if (k >= ped.size() || (k < i && linked(ped[k], i))) continue;
            const V3 a = ped[i].pos, b = ped[k].pos;
            auto ivs = covered(a, b, kPedMargin, kPedWall);
            if (ivs.empty()) continue;
            const bool pair = ped[i].links.size() == 1 && ped[k].links.size() == 1 && ped[k].links[0].node == i;
            const double seg = std::sqrt((b.v[0] - a.v[0]) * (b.v[0] - a.v[0]) + (b.v[1] - a.v[1]) * (b.v[1] - a.v[1]) +
                                         (b.v[2] - a.v[2]) * (b.v[2] - a.v[2]));
            std::sort(ivs.begin(), ivs.end());
            std::vector<std::pair<double, double>> gaps;
            double t = 0.0;
            auto gap = [&](double s, double e) { if (pair && (e - s) * seg >= kPedMinLen) gaps.push_back({s, e}); };
            for (const auto &[t0, t1] : ivs)
            {
                if (t0 > t) gap(t, t0);
                t = std::max(t, t1);
            }
            if (t < 1.0) gap(t, 1.0);
            pedChanged = true;
            if (gaps.empty())
            {
                unlinkNodes(ped, i, k);
                log.push_back(fmt("pedestres: segmento %zu-%zu tirado", i, k));
                continue;
            }
            auto at = [&](double s) { V3 r; for (int j = 0; j < 3; ++j) r.v[j] = a.v[j] + s * (b.v[j] - a.v[j]); return r; };
            ped[i].pos = at(gaps[0].first), ped[k].pos = at(gaps[0].second);
            std::string pieces = fmt("%.0f", (gaps[0].second - gaps[0].first) * seg);
            for (size_t g = 1; g < gaps.size() && ped.size() + 2 <= kNetNodes; ++g)
            {
                const uint16_t m = uint16_t(ped.size());
                NetNode na = ped[i], nb = ped[k];
                na.pos = at(gaps[g].first), nb.pos = at(gaps[g].second);
                na.links = {{uint16_t(m + 1), l.width, l.drop}}, nb.links = {{m, l.width, l.drop}};
                ped.push_back(na), ped.push_back(nb);
                pieces += fmt(" + %.0f (novo %u-%u)", (gaps[g].second - gaps[g].first) * seg, unsigned(m), unsigned(m + 1));
            }
            log.push_back(fmt("pedestres: segmento %zu-%zu (%.0f) encurtado para %s", i, k, seg, pieces.c_str()));
        }
    }
    std::vector<size_t> keep;
    for (size_t i = 0; i < ped.size(); ++i)
        if (!ped[i].links.empty()) keep.push_back(i);
    if (keep.size() < ped.size())
    {
        log.push_back(fmt("pedestres: %zu nos tirados, %zu ficam", ped.size() - keep.size(), keep.size()));
        std::unordered_map<size_t, uint16_t> remap;
        for (size_t j = 0; j < keep.size(); ++j) remap[keep[j]] = uint16_t(j);
        std::vector<NetNode> out;
        for (size_t i : keep)
        {
            NetNode n = ped[i];
            std::vector<NetLink> ls;
            for (NetLink l : n.links)
                if (auto it = remap.find(l.node); it != remap.end()) l.node = it->second, ls.push_back(l);
            n.links = ls;
            out.push_back(n);
        }
        ped = out;
    }
    if (pedChanged) writeNet(rdram, kPedNet, ped);
    return log;
}

#include "wotm_levelpeds.inc"

std::string stageName(const uint8_t *rdram)
{
    std::string s;
    for (int c = 0; c < 8 && rdram[kFsNames + c]; ++c) s += char(std::toupper(rdram[kFsNames + c]));
    return s;
}

// ------------------------------------------------------------------ WOTM_LEVELPATCH_BREAK=<s>[:copies|originals][:<estado>][,<s>[:...]]
// Diagnostico da copia profunda (NATIVE.md): <s> segundos depois do primeiro TheGame::Update da carga, cada destrutivel das copias (ou
// dos originais) do patch vai ao estado final (ou ao <estado>) pelo Destructible::ChangeState, como uma pancada faria (so as acoes do
// estado de destino rodam: o desabamento animado de um predio pode estar num estado do meio); 4 s depois uma linha conta os
// emissores e quantos ja acenderam de cada lado (a fumaca de um estado so acende quando o desenho passa por ela: a camera tem de ver) e
// quais destrutiveis de fora do lado quebrado mudaram de estado nesses 4 s (um domino que escapou da copia; ou uma pancada da luta).
// As CHAR_INSTANCEs proprias de cada lado (as que o outro nao alcanca) contam quantas tocam (AnimPlayer com arvore principal) antes da
// quebra e 4 s depois: o ACT_ANIM do destrutivel quebrado liga a do mesmo lado.
struct BreakStep
{
    double at;
    bool copies, done, reported;
    std::unordered_map<uint32_t, uint32_t> before;
    std::unordered_set<uint32_t> side;
    std::array<size_t, 2> playing{};   // [copias, originais] tocando na hora da quebra
    int state = -1;                    // estado de destino (-1 = o ultimo); o ChangeState so roda as acoes dele
};
std::vector<BreakStep> g_breakSteps;
std::vector<uint32_t> g_breakWraps;   // GROUPs das copias da ultima carga
std::chrono::steady_clock::time_point g_breakStart;
bool g_breakStarted = false;
PS2Runtime::RecompiledFunction g_origUpdate = nullptr;
constexpr uint32_t kTheGameUpdate = wotm::fn::Update__7TheGame, kChangeState = wotm::fn::ChangeState__12DestructibleibT2,
                   kInteractivesTab = wotm::sym::x_12Interactives_s_dbInteractive;

void breakArm(const std::vector<uint32_t> &wraps)
{
    g_breakWraps = wraps;
    g_breakStarted = false;
    for (BreakStep &s : g_breakSteps) s.done = s.reported = false;
}
// Funcao do jogo com ate 4 argumentos inteiros; o contexto volta como estava (wotm_padsel::callGuest)
void callGuest(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt, uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const PS2Runtime::RecompiledFunction f = rt->lookupFunction(fn);
    if (!f) return;
    const R5900Context saved = *ctx;
    const uint32_t a[4] = {a0, a1, a2, a3};
    for (int i = 0; i < 4; ++i) ctx->r[4 + i] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(a[i])));
    ctx->r[31] = _mm_set_epi64x(0, 0);
    ctx->pc = fn;
    f(rdram, ctx, rt);
    wotmFinishGuestCall(rdram, ctx, rt);
    *ctx = saved;
}
// INTERACTIVE_STATEs, emissores e CHAR_INSTANCEs de um lado (filho 0 dos GROUPs das copias = original, os outros = copias), sem descer
// nos esqueletos (o que a copia profunda deixa em comum)
void breakSide(uint8_t *rdram, bool copies, std::vector<uint32_t> &ists, std::vector<uint32_t> &emitters, std::vector<uint32_t> *cis = nullptr)
{
    Stage s{rdram};
    s.arenaTop = kArenaEnd;
    std::unordered_set<uint32_t> seen;
    for (uint32_t w : g_breakWraps)
    {
        const std::vector<uint32_t> kids = s.children(w);
        for (size_t k = copies ? 1 : 0; k < (copies ? kids.size() : std::min<size_t>(1, kids.size())); ++k)
        {
            std::vector<uint32_t> stack{kids[k]};
            while (!stack.empty())
            {
                const uint32_t x = stack.back();
                stack.pop_back();
                if (!seen.insert(x).second) continue;
                if (s.op(x) == INTERACTIVE) ists.push_back(x);
                if (s.op(x) == PART_EMITTER) emitters.push_back(x);
                if (s.op(x) == CHAR_INSTANCE && cis) cis->push_back(x);
                if (s.op(x) != SKEL_BONE)
                    for (uint32_t c : s.children(x)) stack.push_back(c);
            }
        }
    }
}
// CHAR_INSTANCEs de um lado que o outro nao alcanca (as clonadas, do lado das copias) e quantas tocam: AnimPlayer (activeTree) com
// arvore principal (+8), que o animationTransitionInto do ACT_ANIM liga
std::pair<size_t, size_t> ownAnims(uint8_t *rdram, bool copies)
{
    std::vector<uint32_t> ists, em, mine, other;
    breakSide(rdram, copies, ists, em, &mine);
    breakSide(rdram, !copies, ists, em, &other);
    const std::unordered_set<uint32_t> o(other.begin(), other.end());
    auto rd = [&](uint32_t a) { uint32_t v; std::memcpy(&v, rdram + (a & PS2_RAM_MASK), 4); return v; };
    size_t own = 0, playing = 0;
    for (uint32_t ci : mine)
        if (!o.count(ci))
        {
            ++own;
            const uint32_t p = rd(ci + 0x24) & kMask;
            playing += p && (rd(p + 8) & kMask);
        }
    return {own, playing};
}
void breakTick(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const auto now = std::chrono::steady_clock::now();
    if (!g_breakStarted) g_breakStart = now, g_breakStarted = true;
    const double t = std::chrono::duration<double>(now - g_breakStart).count();
    auto rd = [&](uint32_t a) { uint32_t v; std::memcpy(&v, rdram + (a & PS2_RAM_MASK), 4); return v; };
    for (BreakStep &st : g_breakSteps)
    {
        if (!st.done && t >= st.at)
        {
            st.done = true;
            std::vector<uint32_t> ists, em;
            breakSide(rdram, st.copies, ists, em);
            st.before.clear();
            st.side = std::unordered_set<uint32_t>(ists.begin(), ists.end());
            for (uint32_t i = 1; i < kInteractives; ++i)   // whichChild de todo destrutivel registrado, antes da quebra
            {
                const uint32_t d = rd(kInteractivesTab + 4 * i) & kMask;
                if (d && rd(d) == 3) st.before[rd(d + 0x28) & kMask] = rd((rd(d + 0x28) & kMask) + 4);
            }
            st.playing = {ownAnims(rdram, true).second, ownAnims(rdram, false).second};
            int broke = 0;
            for (uint32_t ist : ists)
            {
                const uint32_t id2 = (rd(ist) >> 7) & 0x7FFu, d = id2 ? rd(kInteractivesTab + 4 * id2) & kMask : 0;
                if (!d || rd(d) != 3 || (rd(d + 0x28) & kMask) != ist) continue;   // Destructible: m_kind 3, m_istate
                const uint32_t hd = rd(d + 0x24) & kMask, states = rd(hd + 8) >> 16;   // HierDestructible numKids (+0xA)
                if (states < 2) continue;
                const uint32_t to = st.state < 0 ? states - 1 : std::min<uint32_t>(uint32_t(st.state), states - 1);
                callGuest(rdram, ctx, runtime, kChangeState, d, to, 0, 0);
                ++broke;
            }
            std::fprintf(stderr, "[fase] quebra (%s, %.0f s): %d de %zu destrutiveis levados ao estado %s\n", st.copies ? "copias" : "originais",
                         st.at, broke, ists.size(), st.state < 0 ? "final" : fmt("%d (ou o final, se menor)", st.state).c_str());
        }
        if (st.done && !st.reported && t >= st.at + 4.0)
        {
            st.reported = true;
            std::string line;
            for (bool side : {true, false})
            {
                std::vector<uint32_t> ists, em;
                breakSide(rdram, side, ists, em);
                const size_t lit = std::count_if(em.begin(), em.end(), [&](uint32_t e) { return (rd(e + 0x5C) & 0x10u) != 0; });
                line += fmt("%s%s: %zu emissores, %zu acesos", side ? "" : "; ", side ? "copias" : "originais", em.size(), lit);
            }
            std::vector<uint32_t> moved;
            for (const auto &[ist, wc] : st.before)
                if (!st.side.count(ist) && rd(ist + 4) != wc) moved.push_back(ist);
            std::sort(moved.begin(), moved.end());
            line += fmt("; fora do lado quebrado mudaram de estado: %zu", moved.size());
            for (size_t i = 0; i < moved.size() && i < 8; ++i) line += fmt("%s%07x", i ? " " : " (", moved[i]);
            if (!moved.empty()) line += moved.size() > 8 ? " ...)" : ")";
            for (bool side : {true, false})
            {
                const auto [own, playing] = ownAnims(rdram, side);
                line += fmt("; animacoes proprias das %s: %zu, tocando %zu (%zu na quebra)", side ? "copias" : "originais", own, playing,
                            st.playing[side ? 0 : 1]);
            }
            std::fprintf(stderr, "[fase] quebra +4 s: %s\n", line.c_str());
        }
    }
}
void updateHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == kTheGameUpdate && !g_breakWraps.empty()) breakTick(rdram, ctx, runtime);
    g_origUpdate(rdram, ctx, runtime);
}

// <mod>/levels/<FASE>.json procurado agora, a cada carga (um mod ou arquivo criado ou apagado depois do boot vale): as pastas de
// Registry::dir() como no scan (sem '_'/'.' no comeco, em ordem de Registry::key, o ultimo vence), sem olhar maiusculas
struct Found { std::filesystem::path host; std::string mod; };
std::filesystem::path childNoCase(const std::filesystem::path &dir, const std::string &name)
{
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (Registry::key(it->path().filename().string()) == Registry::key(name)) return it->path();
    return {};
}
bool findPatch(const std::string &name, Found &out)
{
    namespace fs = std::filesystem;
    const fs::path &dir = Registry::get().dir();
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return false;
    std::vector<fs::path> mods;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        const std::string n = it->path().filename().string();
        if (!n.empty() && n[0] != '_' && n[0] != '.' && it->is_directory(ec)) mods.push_back(it->path());
    }
    std::sort(mods.begin(), mods.end(), [](const fs::path &a, const fs::path &b) { return Registry::key(a.filename().string()) < Registry::key(b.filename().string()); });
    bool any = false;
    for (const fs::path &m : mods)
    {
        const fs::path lv = childNoCase(m, "levels");
        if (lv.empty()) continue;
        const fs::path f = childNoCase(lv, name + ".json");
        if (f.empty() || !fs::is_regular_file(f, ec)) continue;
        if (any) std::fprintf(stderr, "[fase] LEVELS/%s.JSON: %s vence %s\n", name.c_str(), m.filename().string().c_str(), out.mod.c_str());
        out = {f, m.filename().string()};
        any = true;
    }
    return any;
}

#include "wotm_levelprops.inc"

void applyFor(uint8_t *rdram)
{
    {
        const std::lock_guard<std::mutex> lk(g_propTexMu);
        g_propTex.clear();
    }
    breakArm({});
    const std::string name = stageName(rdram);
    if (name.empty()) return;
    const std::string rel = "LEVELS/" + name + ".JSON";
    Found f;
    static std::string s_lastPatched;   // fase cuja ultima carga teve patch: "sem patch" so diz algo nela (ou com WOTM_LEVELPATCH=2)
    if (!findPatch(name, f))
    {
        if (g_verbose >= 2 || s_lastPatched == name) std::fprintf(stderr, "[fase] %s: sem patch\n", name.c_str());
        s_lastPatched.clear();
        return;
    }
    s_lastPatched = name;
    json::Value v;
    std::string err;
    // O editor pode estar gravando o arquivo agora: um JSON que nao fecha e lido de novo uma vez, um pouco depois
    for (int attempt = 0;; ++attempt)
    {
        std::ifstream in(f.host, std::ios::binary);
        if (!in)
        {
            std::fprintf(stderr, "[fase] %s: sem patch (%s sumiu)\n", name.c_str(), f.host.string().c_str());
            return;
        }
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        err.clear();
        if (json::parse(text, v, &err) && v.type == json::Value::Obj) break;
        if (attempt == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }
        std::fprintf(stderr, "[fase] %s (%s): JSON invalido, patch ignorado: %s\n", rel.c_str(), f.mod.c_str(), err.c_str());
        return;
    }
    Stage st{rdram};
    if (!st.load())
    {
        std::fprintf(stderr, "[fase] %s: imagem da fase sem grade de colisao em 0xA00000, patch ignorado\n", name.c_str());
        return;
    }
    const std::string before = st.check();
    st.shallow = g_shallow;
    st.interBefore = st.interactives();
    st.interUsed = st.interBefore[0] + st.interBefore[1] + st.interBefore[2];
    try
    {
        st.apply(v);
    }
    catch (const PatchError &e)
    {   // so a arena cheia chega aqui: o que ja foi escrito fica
        st.log.push_back(fmt("parou no meio: %s", e.what()));
    }
    breakArm(st.wraps);
    try
    {
        applyProps(st, rdram, v, f.host.parent_path().parent_path(), name);
    }
    catch (const PatchError &e)
    {   // so a arena cheia chega aqui
        st.log.push_back(fmt("props: parou no meio: %s", e.what()));
    }
    std::vector<uint32_t> pedsMade;
    try
    {
        pedsMade = applyPeds(st, rdram, v);
    }
    catch (const PatchError &e)
    {   // so a arena cheia chega aqui
        st.log.push_back(fmt("pedestres: parou no meio: %s", e.what()));
    }
    for (std::string &l : cutNets(rdram, st.footprints)) st.log.push_back(std::move(l));
    if (v.get("peds") || g_verbose >= 2)
        for (std::string &l : pedReport(st, rdram, pedsMade)) st.log.push_back(std::move(l));
    std::string summary;
    for (const auto &[k, n] : st.stats) summary += (summary.empty() ? "" : ", ") + k + " " + std::to_string(n);
    std::fprintf(stderr, "[fase] %s <- %s (%s): %s\n", name.c_str(), f.mod.c_str(), rel.c_str(), summary.empty() ? "nada mudou" : summary.c_str());
    for (const std::string &l : st.log) std::fprintf(stderr, "[fase]   %s\n", l.c_str());
    if (g_verbose >= 2)
        for (const std::string &l : st.pathLines) std::fprintf(stderr, "[fase]   %s\n", l.c_str());
    std::fprintf(stderr, "[fase]   antes: %s\n[fase]   depois: %s\n", before.c_str(), st.check().c_str());
}

// Cargas da fase desde o boot, contadas antes de o JSON ser lido: um pedido de recarga (wotm_levelreload.inc) feito antes dela ja foi
// atendido por ela
std::atomic<unsigned> g_loads{0};
// Cargas com o patch ja aplicado: a levelSignature da cena nativa (wotm_live.inc) a soma, porque a recarga da mesma fase (F10)
// reescreve a imagem e a arena com os mesmos enderecos e o cache de malhas so confere tag e duas palavras do pacote (packetSig)
std::atomic<unsigned> g_applied{0};

void dbInitDbHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == kDbInitDb)
    {
        const uint32_t ra = GPR_U32(ctx, 31);
        if (ra >= kLoadLevelFiles && ra < kLoadLevelFiles + kLoadLevelFilesSize)
        {
            ++g_loads;
            g_wotmLevelReloadKey.store(false);   // F10 de antes desta carga (no menu, durante o LOAD): ja atendido
            applyFor(rdram);
            ++g_applied;
        }
    }
    g_orig(rdram, ctx, runtime);
}
} // namespace wotm_levelpatch

unsigned wotmLevelPatchLoads() { return wotm_levelpatch::g_applied.load(); }

// PNG da textura propria de um prop da carga atual (wotm_levelprops.inc); a cena nativa o desenha no lugar da copia do jogo
bool wotmPropTexture(uint32_t texId, std::string &host, std::string &mod, std::string &rel)
{
    using namespace wotm_levelpatch;
    const std::lock_guard<std::mutex> lk(g_propTexMu);
    const auto it = g_propTex.find(texId);
    if (it == g_propTex.end()) return false;
    host = it->second.host, mod = it->second.mod, rel = it->second.rel;
    return true;
}

void wotmLevelPatchInstall(PS2Runtime &runtime)
{
    using namespace wotm_levelpatch;
    if (const char *e = std::getenv("WOTM_LEVELPATCH")) g_verbose = std::atoi(e);
    if (g_verbose <= 0)
    {
        std::fprintf(stderr, "[fase] patches de fase desligados (WOTM_LEVELPATCH=0)\n");
        return;
    }
    for (const auto &[rel, f] : Registry::get().files())
        if (rel.rfind("LEVELS/", 0) == 0 && rel.size() > 5 && rel.substr(rel.size() - 5) == ".JSON")
            std::fprintf(stderr, "[fase] patch %s (%s)\n", rel.c_str(), f.mod.c_str());
    if (const char *e = std::getenv("WOTM_LEVELPATCH_SHALLOW"); e && std::atoi(e) > 0)
    {
        g_shallow = true;
        std::fprintf(stderr, "[fase] copias rasas (WOTM_LEVELPATCH_SHALLOW=1)\n");
    }
    if (const char *e = std::getenv("WOTM_LEVELPATCH_BREAK"); e && *e)
    {
        std::string s = e;
        for (size_t at = 0; at < s.size();)
        {
            const size_t comma = std::min(s.find(',', at), s.size());
            const std::string item = s.substr(at, comma - at);
            // <s>[:copies|originals][:<estado>]
            const size_t colon = item.find(':'), colon2 = colon == std::string::npos ? colon : item.find(':', colon + 1);
            const std::string who = colon == std::string::npos ? "copies" : item.substr(colon + 1, colon2 == std::string::npos ? colon2 : colon2 - colon - 1);
            if (const double sec = std::atof(item.c_str()); sec > 0.0)
            {
                BreakStep b{sec, who != "originals", false, false};
                if (colon2 != std::string::npos) b.state = std::atoi(item.c_str() + colon2 + 1);
                g_breakSteps.push_back(b);
            }
            at = comma + 1;
        }
        if (!g_breakSteps.empty() && (g_origUpdate = runtime.lookupFunction(kTheGameUpdate)))
        {
            runtime.registerFunction(kTheGameUpdate, &updateHook);
            std::fprintf(stderr, "[fase] WOTM_LEVELPATCH_BREAK: %zu quebras marcadas\n", g_breakSteps.size());
        }
    }
    // o gancho fica mesmo sem patch no boot: o JSON e procurado a cada carga (findPatch)
    if (!(g_orig = runtime.lookupFunction(kDbInitDb)))
    {
        std::fprintf(stderr, "[fase] dbInitDb sem funcao no runtime: patches de fase desligados\n");
        return;
    }
    runtime.registerFunction(kDbInitDb, &dbInitDbHook);
}
