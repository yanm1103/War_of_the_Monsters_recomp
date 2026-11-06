// wotm_levelpatch.inc: patches de fase (MODS.md, "Level patches"). Incluido por wotm_overrides.cpp depois do wotm_mods.inc.
// mods/<mod>/levels/<FASE>.json move, gira, apaga e copia objetos da fase. O gancho fica na entrada do dbInitDb chamado pelo
// Shell::LoadLevelFiles: a imagem da fase ja esta inflada em 0xA00000 com o TEX0 relocado, e nada foi registrado ainda (Destructibles,
// Interactives, Pickups, emissores e a grade de colisao copiam o que acham na travessia que vem logo depois). As regras sao as de
// FORMATS.md ("Stage objects", "Stage collision"); tools/lvl.py patch aplica o mesmo patch na imagem do disco, na mesma ordem e com
// as mesmas contas, e confere a colisao do resultado. Os nos novos (ROTATE no lugar de um TRANSLATE que gira, GROUP que segura o
// original e as copias, copias da alca, do INTERACTIVE_STATE e do DESTRUCTIBLE, caminhos de colisao e nos de lista das celulas) vao
// para a arena de 1 MB abaixo do MB dos ganchos (wotm_mods_hooks::kLevelArena), refeita a cada carga.
// WOTM_LEVELPATCH=0 desliga; =2 loga cada caminho refeito (a mesma linha do lvl.py patch).
#include <array>
#include <cstdarg>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

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
enum Op : uint32_t { OBJECT = 0, GROUP = 1, LOD = 2, TRANSLATE = 3, ROTATE = 4, SWITCH = 6, CONTROL = 8, NUL = 9, COLL_VOL = 10,
                     COLL_GRID = 15, ANIM_XFORM = 17, ACTION_DATA = 23, CHAR_INSTANCE = 25, SCALE = 30, DESTRUCTIBLE = 31,
                     SKEL_BONE = 35, ANIM_SCALE = 38, INTERACTIVE = 39, POLY_COLL = 42 };

PS2Runtime::RecompiledFunction g_orig = nullptr;
int g_verbose = 1;

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
    double cell = 200.0, minX = 0.0, minY = 0.0;
    std::map<uint32_t, std::set<int>> cells;   // caminho -> celulas em que esta listado
    std::vector<uint32_t> freeNodes;
    std::map<std::string, int> stats;
    std::vector<std::string> log, pathLines;

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
            if (op(r) == COLL_GRID) grid = r;
        if (!grid) return false;
        cols = s16(grid + 4), rows = s16(grid + 6);
        cell = f32(grid + 8), minX = f32(grid + 0xC), minY = f32(grid + 0x10);
        if (cols <= 0 || rows <= 0 || cols * rows > 65536 || cell <= 0.0) return false;
        // fim da imagem: a grade fecha o bloco de colisao (FORMATS.md, "Stage collision") e so a raiz ANIM_MGR vem depois dela
        hi = grid + 0x20 + 4 * uint32_t(cols * rows);
        for (uint32_t r : roots) hi = std::max(hi, r + 0x400);
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
    bool pathBox(const Path &p, Box &out) const
    {
        if (p.nodes.empty()) return false;
        for (uint32_t n : p.nodes)
            if (op(n) == ANIM_XFORM || op(n) == CHAR_INSTANCE) return false;
        Box lb;
        if (!aabb(leafPoints(p.nodes.back()), lb)) return false;
        const M4 m = chainMatrix(p.nodes, p.nodes.size() - 1, false);
        std::vector<V3> w;
        for (const V3 &c : corners(lb)) w.push_back(xform(m, c));
        return aabb(w, out);
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
        if (!pathBox(p, bb))
        {
            std::vector<V3> w;
            for (const V3 &c : corners(p.box)) w.push_back(xform(carry, c));
            aabb(w, bb);
            how = "carried";
        }
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
    struct Place { V3 pos; double heading; };
    // Copias do objeto da alca h: alca, INTERACTIVE_STATE e DESTRUCTIBLE novos para cada uma, com os estados de baixo em comum; um
    // GROUP no lugar do original no pai segura o original e as copias. Cada copia ganha os seus caminhos de colisao.
    void copy(uint32_t h, const std::vector<Place> &places)
    {
        uint32_t ist = 0;
        for (uint32_t c : children(h))
        {
            if (op(c) == INTERACTIVE && !ist) ist = c;
            if (op(c) == GROUP && ident(c) >= 0x12C0 && ident(c) <= 0x13EB) throw PatchError(fmt("copia de %07x: pickup ainda nao copia", h));
        }
        for (uint32_t x : subtree(h))
            if (x != ist && op(x) == INTERACTIVE) throw PatchError(fmt("copia de %07x: tem destrutiveis dentro (ainda nao copia)", h));
        const uint32_t p = parentOf(h);
        const M4 before = world(h, false);
        const V3 oldPos = pos3(world(h));
        const std::vector<uint32_t> paths = pathsThrough(h);
        const uint32_t wrap = alloc(0x20 + 4 * uint32_t(1 + places.size()), 16);
        const uint32_t po = op(p);
        w32(wrap, GROUP);
        w32(wrap + 4, po == GROUP || po == TRANSLATE || po == ROTATE || po == SCALE ? u32(p + 4) : 0);
        w16(wrap + 8, uint16_t(1 + places.size()));
        w16(wrap + 0xA, 7);   // visual | collision | noFOV
        w32(wrap + 0x20, h);
        for (uint32_t s : childSlots(p))
            if ((u32(s) & kMask) == h) setPtr(s, wrap);
        parents[wrap] = {p};
        parents[h] = {wrap};
        const uint32_t size = (op(h) == TRANSLATE ? 0x1C : 0x50) + 4 * u32(h + 8);
        for (size_t i = 0; i < places.size(); ++i)
        {
            uint32_t hc = alloc(size, 16);
            copyBytes(hc, h, size);
            parents[hc] = {wrap};
            w32(wrap + 0x24 + 4 * uint32_t(i), hc);
            std::unordered_map<uint32_t, uint32_t> twins;
            if (ist)
            {
                const uint32_t d0 = u32(ist + 0xC) & kMask, dsize = 0xC + 12 * u16(d0 + 0xA);
                const uint32_t isc = alloc(16), dc = alloc(dsize);
                copyBytes(isc, ist, 16);
                copyBytes(dc, d0, dsize);
                w32(isc, u32(ist) & 0x3FFFFu);   // id1 0: os ids especiais (colunas, SANFRAN) ficam com o original
                setPtr(isc + 0xC, dc);
                for (uint32_t s : childSlots(hc))
                    if ((u32(s) & kMask) == ist) setPtr(s, isc);
                parents[isc] = {hc};
                parents[dc] = {isc};
                twins[ist] = isc;
                twins[d0] = dc;
            }
            hc = place(hc, places[i].pos, places[i].heading);
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
            ++stats["copies"];
        }
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
    void apply(const json::Value &patch)
    {
        const json::Value *objs = patch.get("objects");
        if (!objs || objs->type != json::Value::Arr) throw PatchError("sem a lista \"objects\"");
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
                    copy(h, places);
                }
                if (const json::Value *d = e.get("delete"); d && d->asBool())
                    erase(h);
                else if (e.get("pos") || e.get("heading"))
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

std::string stageName(const uint8_t *rdram)
{
    std::string s;
    for (int c = 0; c < 8 && rdram[kFsNames + c]; ++c) s += char(std::toupper(rdram[kFsNames + c]));
    return s;
}

void applyFor(uint8_t *rdram)
{
    const std::string name = stageName(rdram);
    if (name.empty()) return;
    const std::string rel = "LEVELS/" + name + ".JSON";
    const wotm::mods::File *f = Registry::get().find(rel);
    if (!f) return;
    std::ifstream in(f->host, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    json::Value v;
    std::string err;
    if (!json::parse(text, v, &err) || v.type != json::Value::Obj)
    {
        std::fprintf(stderr, "[fase] %s (%s): JSON invalido, patch ignorado: %s\n", rel.c_str(), f->mod.c_str(), err.c_str());
        return;
    }
    Stage st{rdram};
    if (!st.load())
    {
        std::fprintf(stderr, "[fase] %s: imagem da fase sem grade de colisao em 0xA00000, patch ignorado\n", name.c_str());
        return;
    }
    const std::string before = st.check();
    try
    {
        st.apply(v);
    }
    catch (const PatchError &e)
    {   // so a arena cheia chega aqui: o que ja foi escrito fica
        st.log.push_back(fmt("parou no meio: %s", e.what()));
    }
    std::string summary;
    for (const auto &[k, n] : st.stats) summary += (summary.empty() ? "" : ", ") + k + " " + std::to_string(n);
    std::fprintf(stderr, "[fase] %s <- %s (%s): %s\n", name.c_str(), f->mod.c_str(), rel.c_str(), summary.empty() ? "nada mudou" : summary.c_str());
    for (const std::string &l : st.log) std::fprintf(stderr, "[fase]   %s\n", l.c_str());
    if (g_verbose >= 2)
        for (const std::string &l : st.pathLines) std::fprintf(stderr, "[fase]   %s\n", l.c_str());
    std::fprintf(stderr, "[fase]   antes: %s\n[fase]   depois: %s\n", before.c_str(), st.check().c_str());
}

void dbInitDbHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == kDbInitDb)
    {
        const uint32_t ra = GPR_U32(ctx, 31);
        if (ra >= kLoadLevelFiles && ra < kLoadLevelFiles + kLoadLevelFilesSize) applyFor(rdram);
    }
    g_orig(rdram, ctx, runtime);
}
} // namespace wotm_levelpatch

void wotmLevelPatchInstall(PS2Runtime &runtime)
{
    using namespace wotm_levelpatch;
    if (const char *e = std::getenv("WOTM_LEVELPATCH")) g_verbose = std::atoi(e);
    if (g_verbose <= 0)
    {
        std::fprintf(stderr, "[fase] patches de fase desligados (WOTM_LEVELPATCH=0)\n");
        return;
    }
    int n = 0;
    for (const auto &[rel, f] : Registry::get().files())
        if (rel.rfind("LEVELS/", 0) == 0 && rel.size() > 5 && rel.substr(rel.size() - 5) == ".JSON")
        {
            std::fprintf(stderr, "[fase] patch %s (%s)\n", rel.c_str(), f.mod.c_str());
            ++n;
        }
    if (!n) return;   // sem patch: nenhum gancho
    if (!(g_orig = runtime.lookupFunction(kDbInitDb)))
    {
        std::fprintf(stderr, "[fase] dbInitDb sem funcao no runtime: patches de fase desligados\n");
        return;
    }
    runtime.registerFunction(kDbInitDb, &dbInitDbHook);
}
