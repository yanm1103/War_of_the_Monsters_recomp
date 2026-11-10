// Modo treino, lado do desenho (incluido pelo wotm_live.inc): volumes do corpo lidos na thread do EE (capture, chamada pelo
// wotmCaptureFrame), esferas dos golpes e dano vindos dos ganchos (native/runtime/wotm_training.inc), desenho por cima da cena (sem
// teste de profundidade), painel de dano e numeros das barras do HUD. Estado compartilhado em native/wotm_training.h.

#include "wotm_training.h"

namespace wotm_padsel   // wotm_padsel.inc (incluido depois): glifos da fonte do jogo
{
static void text(std::vector<wotm::TextQuad> &out, const wotm::Ram &ram, uint64_t tex0, int rw, int rh, const char *s, float cx, float cy, float h,
                 Color c, float spacing);
}

namespace wotm_trainview
{
using namespace wotm_training;
constexpr uint32_t kSlots = 0x120380u;
constexpr float kPanelTop = 0.43f;   // topo do painel e dos comandos (fracao da altura): abaixo dos dois HUDs, que vao ate y 91 de 224

// ---- cascos de colisao do corpo (nos op 10 do hier do monstro): planos {n, d} no espaco do no, dentro = n.p <= d

void hullSegments(const wotm::Ram &ram, uint32_t node, const wotm::M4 &m, std::vector<float> &seg)
{
    const int np = int16_t(ram.u16(node + 4));
    if (np < 4 || np > 24 || !ram.ok(node + 0x20, 16u * uint32_t(np)))
        return;
    float n[24][3], d[24];
    for (int i = 0; i < np; ++i)
    {
        for (int k = 0; k < 3; ++k)
            n[i][k] = ram.f32(node + 0x20 + 16 * i + 4 * k);
        d[i] = ram.f32(node + 0x20 + 16 * i + 12);
    }
    for (int sign = 1; sign >= -1; sign -= 2)   // se nenhum vertice fica dentro com n.p <= d, tenta a outra convencao
    {
        struct V { float p[3]; uint32_t tight; };
        std::vector<V> vs;
        for (int i = 0; i < np; ++i)
            for (int j = i + 1; j < np; ++j)
                for (int k = j + 1; k < np; ++k)
                {
                    const float *a = n[i], *b = n[j], *c = n[k];
                    const float bc[3] = {b[1] * c[2] - b[2] * c[1], b[2] * c[0] - b[0] * c[2], b[0] * c[1] - b[1] * c[0]};
                    const float det = a[0] * bc[0] + a[1] * bc[1] + a[2] * bc[2];
                    if (std::fabs(det) < 1e-6f)
                        continue;
                    const float ca[3] = {c[1] * a[2] - c[2] * a[1], c[2] * a[0] - c[0] * a[2], c[0] * a[1] - c[1] * a[0]};
                    const float ab[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
                    V v{};
                    for (int q = 0; q < 3; ++q)
                        v.p[q] = (d[i] * bc[q] + d[j] * ca[q] + d[k] * ab[q]) / det;
                    bool in = std::isfinite(v.p[0] + v.p[1] + v.p[2]);
                    for (int t = 0; t < np && in; ++t)
                    {
                        const float s = n[t][0] * v.p[0] + n[t][1] * v.p[1] + n[t][2] * v.p[2] - d[t];
                        if (sign * s > 0.05f)
                            in = false;
                        else if (std::fabs(s) <= 0.05f)
                            v.tight |= 1u << t;
                    }
                    if (!in)
                        continue;
                    bool dup = false;
                    for (V &o : vs)
                        if (std::fabs(o.p[0] - v.p[0]) + std::fabs(o.p[1] - v.p[1]) + std::fabs(o.p[2] - v.p[2]) < 0.05f)
                        {
                            o.tight |= v.tight;
                            dup = true;
                            break;
                        }
                    if (!dup && vs.size() < 64)
                        vs.push_back(v);
                }
        if (vs.size() < 4)
            continue;
        for (size_t i = 0; i < vs.size(); ++i)
            for (size_t j = i + 1; j < vs.size(); ++j)
            {
                const uint32_t both = vs[i].tight & vs[j].tight;
                if ((both & (both - 1)) == 0)   // menos de dois planos em comum: nao e aresta
                    continue;
                float a[3], b[3];
                wotm::xform(m, vs[i].p[0], vs[i].p[1], vs[i].p[2], a);
                wotm::xform(m, vs[j].p[0], vs[j].p[1], vs[j].p[2], b);
                seg.insert(seg.end(), {a[0], a[1], a[2], b[0], b[1], b[2]});
            }
        return;
    }
}

// Percurso so do que leva aos cascos (o mesmo caminho de matrizes do Scene::walk, sem LOD por distancia: o filho 0)
void hullWalk(const wotm::Ram &ram, uint32_t node, const wotm::M4 &m, uint32_t atMat, int depth, int &budget, std::vector<float> &seg)
{
    using namespace wotm;
    if (depth > 48 || --budget < 0 || !ram.ok(node, 0x10))
        return;
    auto kids = [&](uint32_t arr, uint32_t n, const M4 &mm, uint32_t at) {
        n = std::min<uint32_t>(n, 256);
        for (uint32_t i = 0; i < n; ++i)
            if (const uint32_t c = ram.u32(arr + 4 * i); ram.ok(c))
                hullWalk(ram, c, mm, at, depth + 1, budget, seg);
    };
    switch (ram.u32(node) & 0x3F)
    {
    case 10:   // COLLISION_VOLUME
        hullSegments(ram, node, m, seg);
        break;
    case 1:    // GROUP: visual (bit 0) e colisao (bit 1)
        if (ram.u16(node + 0xA) & 3)
            kids(node + 0x20, ram.u16(node + 8), m, atMat);
        break;
    case 2:    // LOD: o mais detalhado
        if (ram.u32(node + 4) > 0 && ram.u32(node + 4) < 16)
            if (const uint32_t c = ram.u32(node + 0x28); ram.ok(c))
                hullWalk(ram, c, m, atMat, depth + 1, budget, seg);
        break;
    case 3:
        kids(node + 0x1C, ram.u32(node + 8), mul(translateM(ram.f32(node + 0x10), ram.f32(node + 0x14), ram.f32(node + 0x18)), m), atMat);
        break;
    case 4:
        kids(node + 0x50, ram.u32(node + 8), mul(readM4(ram, node + 0x10), m), atMat);
        break;
    case 30:
        kids(node + 0x2C, ram.u32(node + 8),
             mul(mul(scaleM(ram.f32(node + 0x20), ram.f32(node + 0x24), ram.f32(node + 0x28)),
                     translateM(ram.f32(node + 0x10), ram.f32(node + 0x14), ram.f32(node + 0x18))), m), atMat);
        break;
    case 6:
        if (const uint32_t w = ram.u8(node + 8); w < ram.u8(node + 0xB))
            if (const uint32_t c = ram.u32(node + 0xC + 4 * w); ram.ok(c))
                hullWalk(ram, c, m, atMat, depth + 1, budget, seg);
        break;
    case 23:
        if (const uint32_t c = ram.u32(node + 4); ram.ok(c))
            hullWalk(ram, c, m, atMat, depth + 1, budget, seg);
        break;
    case 35:
        kids(node + 0xC, ram.u32(node + 4), m, atMat);
        break;
    case 25:   // CHAR_INSTANCE: matrizes da animacao
        kids(node + 0x2C, ram.u32(node + 0x28), m, ram.u32(node + 0xC));
        break;
    case 17: { // ANIM_XFORM
        if (ram.f32(node + 0xC) == 0.f)
            break;
        M4 local = ident();
        const uint32_t idx = ram.u32(node + 0xC4);
        if (atMat && idx < 4096 && ram.ok(atMat + idx * 64, 64))
            local = readM4(ram, atMat + idx * 64);
        local.m[0][3] = local.m[1][3] = local.m[2][3] = 0.f;
        local.m[3][3] = 1.f;
        kids(node + 0xCC, ram.u32(node + 4), mul(local, m), atMat);
        break;
    }
    case 38: { // ANIM_SCALE
        const float sx = ram.f32(node + 0x20), sy = ram.f32(node + 0x24), sz = ram.f32(node + 0x28);
        if (sx == 0.f && sy == 0.f && sz == 0.f)
            break;
        const M4 pre = translateM(ram.f32(node + 0x40), ram.f32(node + 0x44), ram.f32(node + 0x48));
        const M4 post = translateM(ram.f32(node + 0x30) + ram.f32(node + 0x50), ram.f32(node + 0x34) + ram.f32(node + 0x54),
                                   ram.f32(node + 0x38) + ram.f32(node + 0x58));
        kids(node + 0x60, ram.u32(node + 4), mul(mul(mul(pre, scaleM(sx, sy, sz)), post), m), atMat);
        break;
    }
    default:
        break;
    }
}

// ---- caixas de colisao da planta (WOTM_PLANTBOX=1): os cascos dos dois _cs de cada cabeca, sem precisar do modo treino. O golpe so acerta o
// _cs do caule (testCollision ligado); o da cabeca e so desenho. Verde: a cabeca esta no estado em que o takeHit tira vida (2 a 4);
// amarelo: estado 1 (so depois de metade da animacao); magenta: qualquer outro (o golpe colide e e descartado). Cinza: _cs sem teste.
struct PlantBox
{
    uint8_t head, kind;   // kind: 0 caule (o alvo), 1 cabeca (so desenho)
    uint32_t state;
    bool test;            // testCollision
    float pos[3];         // trans do _cs
    std::vector<float> seg;
};
inline bool plantBoxOn()
{
    static const bool on = [] { const char *e = std::getenv("WOTM_PLANTBOX"); return e && *e && *e != '0'; }();
    return on;
}
inline std::mutex g_plantMu;
inline std::vector<PlantBox> g_plantBoxes;

void capturePlant(const wotm::Ram &ram)
{
    if (!plantBoxOn())
        return;
    constexpr uint32_t kPlant = wotm::sym::plantBoss, kHeads = kPlant + 0x20, kStride = 0x250;
    std::vector<PlantBox> out;
    if (ram.ok(kPlant, 0x10B0))
        for (uint32_t i = 0; i < 3; ++i)
        {
            const uint32_t h = kHeads + kStride * i;
            if (!ram.u32(h + 0x14))   // m_awake
                continue;
            const uint32_t state = ram.u32(h + 0x18);
            for (uint32_t kind = 0; kind < 2; ++kind)
            {
                const uint32_t cs = ram.u32(h + (kind ? 0x38u : 0x40u)) & 0x1FFFFFFFu;   // m_headCs +0x38, m_stemCs +0x40
                if (!ram.ok(cs, 0xB0))
                    continue;
                wotm::M4 m = wotm::readM4(ram, cs + 0x20);
                m.m[3][0] = ram.f32(cs + 0x10);
                m.m[3][1] = ram.f32(cs + 0x14);
                m.m[3][2] = ram.f32(cs + 0x18);
                m.m[0][3] = m.m[1][3] = m.m[2][3] = 0.f;
                m.m[3][3] = 1.f;
                if (ram.u8(cs + 0xF))   // scaleMe
                    m = wotm::mul(wotm::scaleM(ram.f32(cs + 0x80), ram.f32(cs + 0x84), ram.f32(cs + 0x88)), m);
                PlantBox b{};
                b.head = uint8_t(i);
                b.kind = uint8_t(kind);
                b.state = state;
                b.test = ram.u8(cs + 0xD) != 0;
                b.pos[0] = m.m[3][0];
                b.pos[1] = m.m[3][1];
                b.pos[2] = m.m[3][2];
                int budget = 4000;
                if (const uint32_t ep = ram.u32(cs) & 0x1FFFFFFFu; ram.ok(ep))
                    hullWalk(ram, ep, m, 0, 0, budget, b.seg);
                out.push_back(std::move(b));
            }
        }
    std::lock_guard<std::mutex> lk(g_plantMu);
    g_plantBoxes = std::move(out);
}

// Na thread do EE (wotmCaptureFrame): cascos, capsulas e vida de cada monstro deste quadro
void capture(const wotm::Ram &ram)
{
    capturePlant(ram);
    if (!g_on.load(std::memory_order_relaxed))
        return;
    const uint32_t opts = g_opts.load(std::memory_order_relaxed);
    std::vector<Hull> hulls;
    std::vector<Capsule> caps;
    float health[16] = {}, maxHealth[16] = {}, stamina[16] = {}, staminaMax[16] = {};
    for (uint32_t i = 0; i < 16; ++i)
    {
        const uint32_t mon = ram.u32(wotm::sym::shellGame + kSlots + 4 * i) & 0x1FFFFFFFu;
        if (!mon || !ram.ok(mon, 0x450))
            continue;
        health[i] = ram.f32(mon + 0x44C);
        maxHealth[i] = ram.f32(mon + 0x448);
        stamina[i] = ram.f32(mon + 0x470);      // StaminaMeter (+0x460): cur
        staminaMax[i] = ram.f32(mon + 0x460);   // max (a furia passa dele ate o maxLevel, +0x468)
        const uint32_t cs = ram.u32(mon + 0xC) & 0x1FFFFFFFu;
        if (!ram.ok(cs, 0xB0) || !ram.u8(cs + 0xC))   // drawMe: monstro escondido (fora da luta, desligado)
            continue;
        wotm::M4 m = wotm::readM4(ram, cs + 0x20);
        m.m[3][0] = ram.f32(cs + 0x10);
        m.m[3][1] = ram.f32(cs + 0x14);
        m.m[3][2] = ram.f32(cs + 0x18);
        m.m[0][3] = m.m[1][3] = m.m[2][3] = 0.f;
        m.m[3][3] = 1.f;
        if (opts & kShowCapsule)
        {
            // capsula do collisResolveCsToCsCollisions: ponto +0x3F0 no espaco do cs, eixo = z do cs, comprimento +0x400, raio +0x404
            Capsule c{};
            c.owner = int8_t(i);
            wotm::xform(m, ram.f32(mon + 0x3F0), ram.f32(mon + 0x3F4), ram.f32(mon + 0x3F8), c.a);
            const float len = ram.f32(mon + 0x400);
            for (int k = 0; k < 3; ++k)
                c.b[k] = c.a[k] + m.m[2][k] * len;
            c.r = ram.f32(mon + 0x404);
            if (std::isfinite(c.r + len) && c.r > 0.f && c.r < 2000.f)
                caps.push_back(c);
        }
        if (opts & kShowHulls)
        {
            if (ram.u8(cs + 0xF))   // scaleMe
                m = wotm::mul(wotm::scaleM(ram.f32(cs + 0x80), ram.f32(cs + 0x84), ram.f32(cs + 0x88)), m);
            Hull h{};
            h.owner = int8_t(i);
            int budget = 4000;
            const uint32_t ep = ram.u32(cs) & 0x1FFFFFFFu;
            if (ram.ok(ep))
                hullWalk(ram, ep, m, 0, 0, budget, h.seg);
            if (!h.seg.empty())
                hulls.push_back(std::move(h));
        }
    }
    int32_t players = static_cast<int32_t>(ram.u32(wotm::sym::shellInfo + 0x2BB4));
    if (players < 1 || players > 4)
        players = 1;
    Shared &sh = shared();
    std::lock_guard<std::mutex> lk(sh.m);
    sh.hulls = std::move(hulls);
    sh.caps = std::move(caps);
    sh.frame = ram.u32(wotm::sym::g_frame);
    std::memcpy(sh.health, health, sizeof health);
    std::memcpy(sh.maxHealth, maxHealth, sizeof maxHealth);
    std::memcpy(sh.stamina, stamina, sizeof stamina);
    std::memcpy(sh.staminaMax, staminaMax, sizeof staminaMax);
    sh.players = int8_t(players);
    sh.hudShown = ram.u32(wotm::sym::gHudEnable) && !ram.u32(wotm::sym::x_3Hud_m_letterbox + 0x2Cu);
}

// ---- janela

struct View
{
    std::vector<Sweep> sweeps;
    std::vector<Cone> cones;
    std::vector<Hit> hits;
    std::vector<Hull> hulls;
    std::vector<Capsule> caps;
    std::vector<MoveRec> moves;
    std::vector<StunRec> stuns;
    uint32_t frame = 0;
    float health[16] = {}, maxHealth[16] = {}, stamina[16] = {}, staminaMax[16] = {};
    int players = 1;
    bool hudShown = false;
    Combo combo;
    float bestCombo = 0.f;
    int bestHits = 0;
    std::vector<PadRec> pad[2];
    PunchLive live[16];
    PunchCfg table[kPunchTypes];
    int tableTier = 0;
    bool tableWeapon = false;
    std::vector<Move> fav;   // golpes favoritos da MOVE LIST, na ordem em que foram marcados (vazio: a caixa mostra os 6 da posicao)
    uint32_t p1State = 0;    // offset do estado do P1 (acende a linha MkOther)
    uint32_t p1StateAnim = 0;   // +0x50 do estado (Move::variant dos golpes do agarrao)
};

// copia o estado compartilhado (descartando as esferas velhas e guardando os ultimos danos)
View snapshot()
{
    constexpr uint32_t kSweepLife = 10, kHitsKept = 64;
    Shared &sh = shared();
    View v;
    std::lock_guard<std::mutex> lk(sh.m);
    const uint32_t f = sh.frame;
    if (g_resetStats.exchange(false))
    {
        sh.hits.clear();
        sh.moves.clear();
        sh.stuns.clear();
        sh.combo = Combo{};
        sh.bestCombo = 0.f;
        sh.bestHits = 0;
    }
    v.moves = sh.moves;
    v.stuns = sh.stuns;
    sh.sweeps.erase(std::remove_if(sh.sweeps.begin(), sh.sweeps.end(),
                                   [&](const Sweep &s) { return s.frame + kSweepLife < f || s.frame > f + 600; }),
                    sh.sweeps.end());
    sh.cones.erase(std::remove_if(sh.cones.begin(), sh.cones.end(),
                                  [&](const Cone &c) { return c.frame + kSweepLife < f || c.frame > f + 600; }),
                   sh.cones.end());
    v.cones = sh.cones;
    if (sh.hits.size() > kHitsKept)
        sh.hits.erase(sh.hits.begin(), sh.hits.end() - kHitsKept);
    sh.hits.erase(std::remove_if(sh.hits.begin(), sh.hits.end(), [&](const Hit &h) { return h.frame > f + 600; }), sh.hits.end());
    v.sweeps = sh.sweeps;
    v.hits = sh.hits;
    v.hulls = sh.hulls;
    v.caps = sh.caps;
    v.frame = f;
    std::memcpy(v.health, sh.health, sizeof v.health);
    std::memcpy(v.maxHealth, sh.maxHealth, sizeof v.maxHealth);
    std::memcpy(v.stamina, sh.stamina, sizeof v.stamina);
    std::memcpy(v.staminaMax, sh.staminaMax, sizeof v.staminaMax);
    v.players = sh.players;
    v.hudShown = sh.hudShown;
    v.combo = sh.combo;
    v.bestCombo = sh.bestCombo;
    v.bestHits = sh.bestHits;
    v.pad[0] = sh.pad[0];
    v.pad[1] = sh.pad[1];
    std::copy(std::begin(sh.live), std::end(sh.live), std::begin(v.live));
    std::copy(std::begin(sh.table), std::end(sh.table), std::begin(v.table));
    v.tableTier = sh.tableTier;
    v.tableWeapon = sh.tableWeapon;
    for (const std::string &id : sh.fav)
        for (const Move &m : sh.moveList)
            if (id == m.id) { v.fav.push_back(m); break; }
    v.p1State = sh.p1State;
    v.p1StateAnim = sh.p1StateAnim;
    return v;
}

Color ownerColor(int owner, int players, unsigned char a)
{
    if (owner >= 0 && owner < players)
        return Color{80, 200, 255, a};   // jogador: azul claro
    if (owner >= 0)
        return Color{255, 150, 40, a};   // IA: laranja
    return Color{220, 220, 220, a};
}

// Cone de golpe em arame: arestas, aro e a calota (o alcance e uma distancia do vertice). flat: setor no plano do chao (perpendicular
// a k.up), com o eixo projetado nele
void drawCone(const Cone &k, Color c)
{
    auto v3 = [](float x, float y, float z) { return Vector3{x, y, z}; };
    auto add = [](Vector3 p, Vector3 q, float s) { return Vector3{p.x + q.x * s, p.y + q.y * s, p.z + q.z * s}; };
    auto cross = [](Vector3 p, Vector3 q) { return Vector3{p.y * q.z - p.z * q.y, p.z * q.x - p.x * q.z, p.x * q.y - p.y * q.x}; };
    auto norm = [](Vector3 p) {
        const float l = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        return l > 1e-6f ? Vector3{p.x / l, p.y / l, p.z / l} : Vector3{0.f, 0.f, 0.f};
    };
    const Vector3 apex = v3(k.apex[0], k.apex[1], k.apex[2]);
    Vector3 ax = v3(k.axis[0], k.axis[1], k.axis[2]), up = norm(v3(k.up[0], k.up[1], k.up[2]));
    const float th = std::acos(k.cosHalf);
    if (k.flat)
    {
        const float d = ax.x * up.x + ax.y * up.y + ax.z * up.z;
        ax = norm(add(ax, up, -d));
        const Vector3 side = norm(cross(up, ax));
        if (side.x == 0.f && side.y == 0.f && side.z == 0.f)
            return;
        constexpr int kSeg = 16;
        Vector3 prev{};
        for (int i = 0; i <= kSeg; ++i)
        {
            const float t = -th + 2.f * th * float(i) / kSeg;
            const Vector3 p = add(add(apex, ax, k.len * std::cos(t)), side, k.len * std::sin(t));
            if (i == 0 || i == kSeg)
                DrawLine3D(apex, p, c);
            if (i)
                DrawLine3D(prev, p, c);
            prev = p;
        }
        return;
    }
    Vector3 ref = std::fabs(ax.z) < 0.9f ? Vector3{0.f, 0.f, 1.f} : Vector3{1.f, 0.f, 0.f};
    const Vector3 u = norm(cross(ax, ref)), w = cross(ax, u);
    constexpr int kRim = 20, kEdges = 8, kCap = 6;
    const float rc = k.len * std::cos(th), rs = k.len * std::sin(th);
    auto onSphere = [&](float t, float phi) {   // ponto a angulo t do eixo, phi em volta dele, a distancia len do vertice
        const float s = k.len * std::sin(t), cc = k.len * std::cos(t);
        return add(add(add(apex, ax, cc), u, s * std::cos(phi)), w, s * std::sin(phi));
    };
    Vector3 prev{};
    for (int i = 0; i <= kRim; ++i)
    {
        const float phi = 6.2831853f * float(i) / kRim;
        const Vector3 p = add(add(add(apex, ax, rc), u, rs * std::cos(phi)), w, rs * std::sin(phi));
        if (i)
            DrawLine3D(prev, p, c);
        prev = p;
    }
    for (int e = 0; e < kEdges; ++e)
    {
        const float phi = 6.2831853f * float(e) / kEdges;
        DrawLine3D(apex, onSphere(th, phi), c);
        if (e % 2)
            continue;
        Vector3 q = onSphere(0.f, phi);   // meridiano da calota, do eixo ate o aro
        for (int j = 1; j <= kCap; ++j)
        {
            const Vector3 p = onSphere(th * float(j) / kCap, phi);
            DrawLine3D(q, p, c);
            q = p;
        }
    }
}

void drawPlant()
{
    if (!plantBoxOn() || windowsHidden())
        return;
    std::vector<PlantBox> boxes;
    {
        std::lock_guard<std::mutex> lk(g_plantMu);
        boxes = g_plantBoxes;
    }
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    rlDisableBackfaceCulling();
    for (const PlantBox &b : boxes)
    {
        Color c = Color{150, 150, 150, 200};   // sem teste de colisao
        if (b.test)
            c = (b.state >= 2 && b.state <= 4) ? Color{60, 255, 120, 255} : b.state == 1 ? Color{255, 230, 40, 255} : Color{255, 60, 220, 255};
        for (size_t i = 0; i + 5 < b.seg.size(); i += 6)
            DrawLine3D(Vector3{b.seg[i], b.seg[i + 1], b.seg[i + 2]}, Vector3{b.seg[i + 3], b.seg[i + 4], b.seg[i + 5]}, c);
        // a origem do _cs: o pino do caule (quadrado) ou o ponto da cabeca (esfera)
        DrawSphereWires(Vector3{b.pos[0], b.pos[1], b.pos[2]}, b.kind ? 6.f : 3.f, 4, 8, c);
    }
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthTest();
}

// Dentro do BeginMode3D da vista cheia, depois da cena
void draw3d()
{
    drawPlant();
    if (!g_on.load(std::memory_order_relaxed) || windowsHidden())
        return;
    const uint32_t opts = g_opts.load(std::memory_order_relaxed);
    const View v = snapshot();
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    rlDisableBackfaceCulling();
    if (opts & kShowHulls)
        for (const Hull &h : v.hulls)
        {
            const Color c = h.owner < v.players ? Color{60, 255, 120, 230} : Color{255, 60, 220, 230};
            for (size_t i = 0; i + 5 < h.seg.size(); i += 6)
                DrawLine3D(Vector3{h.seg[i], h.seg[i + 1], h.seg[i + 2]}, Vector3{h.seg[i + 3], h.seg[i + 4], h.seg[i + 5]}, c);
        }
    if (opts & kShowCapsule)
        for (const Capsule &c : v.caps)
            DrawCapsuleWires(Vector3{c.a[0], c.a[1], c.a[2]}, Vector3{c.b[0], c.b[1], c.b[2]}, c.r, 10, 3, Color{200, 200, 200, 120});
    if (opts & kShowAttacks)
        for (const Sweep &s : v.sweeps)
        {
            const uint32_t age = v.frame >= s.frame ? v.frame - s.frame : 0;
            const unsigned char a = static_cast<unsigned char>(255 - std::min<uint32_t>(age, 10) * 20);
            Color c = s.hit == 2 ? Color{255, 40, 40, a} : s.hit == 1 ? Color{255, 230, 40, a} : ownerColor(s.owner, v.players, a);
            const Vector3 pa{s.a[0], s.a[1], s.a[2]}, pb{s.b[0], s.b[1], s.b[2]};
            const float r = std::max(s.r, 2.f);
            if (s.a[0] == s.b[0] && s.a[1] == s.b[1] && s.a[2] == s.b[2])
                DrawSphereWires(pa, r, 6, 10, c);
            else if (s.r <= 0.f)   // linha de visada (fogo, cone): sem raio
                DrawLine3D(pa, pb, c);
            else
                DrawCapsuleWires(pa, pb, r, 10, 3, c);
        }
    // o soco que ainda vai testar: a esfera do punho em cada localizador, apagada, ate a janela abrir (dai saem as esferas do teste)
    if (opts & kMoveTable)
        for (int i = 0; i < 16; ++i)
        {
            const PunchLive &p = v.live[i];
            if (!p.frame || v.frame - p.frame > 1u || !p.cfg.valid || p.inWindow || p.pct > p.cfg.ini)
                continue;
            const Color c = ownerColor(i, v.players, 110);
            for (int k = 0; k < p.nloc; ++k)
                DrawSphereWires(Vector3{p.loc[k][0], p.loc[k][1], p.loc[k][2]}, std::max(p.cfg.radius, 2.f), 4, 8, c);
        }
    if (opts & kShowAttacks)
        for (const Cone &k : v.cones)
        {
            const uint32_t age = v.frame >= k.frame ? v.frame - k.frame : 0;
            const unsigned char a = static_cast<unsigned char>(220 - std::min<uint32_t>(age, 10) * 18);
            drawCone(k, k.hit == 2 ? Color{255, 40, 40, a} : ownerColor(k.owner, v.players, a));
        }
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthTest();
}

const char *sourceName(int s)
{
    switch (s)
    {
    // HitEvent::source (s_takeHitInfo +0x10), os vistos no treino e nas investigacoes
    case -1: return "OVER TIME";
    case 2: case 11: return "MELEE";
    case 3: return "OBJECT";
    case 1: return "PROJECTILE";
    case 7: return "BUTT SLAM";
    case 8: case 13: return "THROW";
    case 5: case 6: case 12: case 14: case 20: return "SPECIAL";
    default: return "OTHER";
    }
}

std::string slotName(int s, int players)
{
    if (s < 0)
        return "?";
    char b[16];
    if (s < players)
        std::snprintf(b, sizeof b, "P%d", s + 1);
    else if (s >= 4)   // as IAs ficam em m_monsters[4 + i]
        std::snprintf(b, sizeof b, "CPU%d", s - 3);
    else
        std::snprintf(b, sizeof b, "#%d", s);
    return b;
}

// Seta da direcao (notacao do teclado numerico; 5 = neutro, um ponto) num quadrado de lado `sz` centrado em (cx, cy)
void drawDir(int dir, float cx, float cy, float sz, Color c)
{
    const float dx = float((dir - 1) % 3 - 1), dy = -float((dir - 1) / 3 - 1);   // tela: y para baixo
    if (dx == 0.f && dy == 0.f)
    {
        DrawCircleV(Vector2{cx, cy}, sz * 0.12f, Color{c.r, c.g, c.b, 140});
        return;
    }
    const float l = std::sqrt(dx * dx + dy * dy), ux = dx / l, uy = dy / l, h = sz * 0.42f;
    const Vector2 tail{cx - ux * h, cy - uy * h}, tip{cx + ux * h, cy + uy * h};
    const Vector2 base{tip.x - ux * sz * 0.35f, tip.y - uy * sz * 0.35f};
    DrawLineEx(tail, base, sz * 0.14f, c);
    const Vector2 p1{base.x - uy * sz * 0.28f, base.y + ux * sz * 0.28f}, p2{base.x + uy * sz * 0.28f, base.y - ux * sz * 0.28f};
    // o raylib so desenha um dos sentidos (o outro e descartado): manda os dois
    DrawTriangle(tip, p1, p2, c);
    DrawTriangle(tip, p2, p1, c);
}

// Texto com a fonte do jogo (os glifos do atlas do quadro, wotm_padsel::text) num quadro rw x rh: cor do raylib (255 = 1,0), contorno
// preto opcional. Sem atlas (nenhum texto do jogo ainda), a fonte do raylib.
struct GameText
{
    const wotm::Ram &ram;
    uint64_t tex0;
    int rw, rh;
    std::vector<wotm::TextQuad> quads;

    // A fonte do jogo deixa uma letra de vao (o passo do wotm_padsel::text e a celula inteira, 1,6 x a altura): aqui a celula e
    // estreitada para 78% e o passo e esse (celulas sobrepostas se cortam no drawQuads). Sem '/', '>' e '+' no atlas: tracos desenhados.
    static constexpr float kTight = 0.78f;
    float wv(float h) const { return 1.6f * h * 224.f / float(rh); }   // largura da celula em unidades da tela do jogo
    float adv(float h) const
    {
        const wotm::GsScreenRect gr = wotm::gsScreenRect(float(rw), float(rh));
        return kTight * wv(h) * gr.w / 640.f;
    }
    float width(const char *s, float h) const { return tex0 ? adv(h) * float(std::strlen(s)) : float(MeasureText(s, int(h))); }
    // os sinais que faltam no atlas, numa celula de largura a (centro cx, cy), altura h
    static void glyphLines(char ch, float cx, float cy, float a, float h, float t, Color c)
    {
        const float hw = a * 0.32f, hh = h * 0.38f;
        if (ch == '/')
            DrawLineEx(Vector2{cx - hw, cy + hh}, Vector2{cx + hw, cy - hh}, t, c);
        else if (ch == '>')
        {
            DrawLineEx(Vector2{cx - hw, cy - hh * 0.8f}, Vector2{cx + hw, cy}, t, c);
            DrawLineEx(Vector2{cx + hw, cy}, Vector2{cx - hw, cy + hh * 0.8f}, t, c);
        }
        else if (ch == '+')
        {
            DrawLineEx(Vector2{cx - hw, cy}, Vector2{cx + hw, cy}, t, c);
            DrawLineEx(Vector2{cx, cy - hw}, Vector2{cx, cy + hw}, t, c);
        }
    }
    // texto com o canto esquerdo em x0 e o meio da altura em cy
    void put(const char *s, float x0, float cy, float h, Color c, bool outline = false)
    {
        if (!tex0)
        {
            DrawText(s, int(x0), int(cy - h * 0.5f), int(h), c);
            return;
        }
        char buf[128];
        std::snprintf(buf, sizeof buf, "%s", s);
        const float a = adv(h), t = std::max(1.5f, h * 0.13f), o = std::max(1.f, h * 0.09f);
        for (size_t i = 0; buf[i]; ++i)
            if (buf[i] == '/' || buf[i] == '>' || buf[i] == '+')
            {
                const float gx = x0 + a * (float(i) + 0.5f);
                if (outline)
                    glyphLines(buf[i], gx, cy, a, h, t + 2.f * o, Color{0, 0, 0, c.a});
                glyphLines(buf[i], gx, cy, a, h, t, c);
                buf[i] = ' ';
            }
        const float cx = x0 + width(buf, h) * 0.5f, sp = (kTight - 1.f) * wv(h);
        auto gs = [](Color k) { return Color{uint8_t(k.r / 2), uint8_t(k.g / 2), uint8_t(k.b / 2), uint8_t(k.a / 2)}; };
        const size_t first = quads.size();
        if (outline)
            for (int k = 0; k < 4; ++k)
                wotm_padsel::text(quads, ram, tex0, rw, rh, buf, cx + (k & 1 ? o : -o), cy + (k & 2 ? o : -o), h, gs(Color{0, 0, 0, c.a}), sp);
        wotm_padsel::text(quads, ram, tex0, rw, rh, buf, cx, cy, h, gs(c), sp);
        for (size_t i = first; i < quads.size(); ++i)
            quads[i].x1 = quads[i].x0 + kTight * wv(h);   // celula estreitada, sem sobrepor a vizinha
    }
    void center(const char *s, float cx, float cy, float h, Color c, bool outline = false) { put(s, cx - width(s, h) * 0.5f, cy, h, c, outline); }
    void flush(wotm::Scene &scene)
    {
        if (!quads.empty())
            scene.drawQuads(quads, float(rw), float(rh));
        quads.clear();
    }
};

// linhas do historico dos comandos: 12, ou 8 com a tabela dos socos embaixo (as duas cabem na altura)
inline int inputRows() { return (g_opts.load(std::memory_order_relaxed) & kMoveTable) ? 8 : 12; }

// Historico dos comandos (P1 e o primeiro boneco), no lado esquerdo da faixa 4:3, abaixo do HUD, de cima para baixo do mais novo para
// o mais velho: quadros segurados, direcao (relativa ao monstro: 8 frente) e botoes, nos icones do dispositivo do jogador
// (wotm_prompts.inc)
void drawInputs(wotm::Scene &scene, GameText &gt, const View &v)
{
    namespace st = wotm::settings;
    const int rw = gt.rw, rh = gt.rh;
    const float fh = std::max(12.f, float(rh) / 50.f);
    const float isz = fh * 1.1f, rowH = isz + 4.f;
    const wotm::GsScreenRect gr = wotm::gsScreenRect(float(rw), float(rh));
    struct B { uint16_t bit; st::Bind bind; const char *txt; };
    static constexpr B kB[] = {{kBtnSquare, st::BSquare, "LT"}, {kBtnTriangle, st::BTriangle, "HV"}, {kBtnX, st::BCross, "JP"},
                               {kBtnCircle, st::BCircle, "GR"}, {kBtnL1, st::BL1, "LK"}, {kBtnR1, st::BR1, "LK"},
                               {kBtnL2, st::BL2, "CAM"}, {kBtnR2, st::BR2, "BLK"}};
    const int rows = inputRows();
    float x = gr.x0 + fh;
    const float top = float(rh) * kPanelTop;
    const float numW = gt.width("999", fh);
    for (int k = 0; k < 2; ++k)
    {
        const std::vector<PadRec> &h = v.pad[k];
        if (k == 1 && (h.empty() || g_dummy.load() == DStand))
            continue;
        const char *title = k == 0 ? "P1 INPUTS" : "DUMMY INPUTS";
        const float w = std::max(gt.width(title, fh), numW + isz * 6.f);
        const int n = std::min<int>(rows, int(h.size()));
        DrawRectangle(int(x) - 6, int(top) - 6, int(w) + 12, int(rowH * float(n + 1)) + 12, Color{0, 0, 0, 130});
        gt.put(title, x, top + isz * 0.5f, fh, Color{120, 255, 160, 255});
        for (int i = 0; i < n; ++i)
        {
            const PadRec &r = h[h.size() - 1 - size_t(i)];
            const float y = top + rowH * float(i + 1);
            const unsigned char a = static_cast<unsigned char>(255 - i * 12);
            char fr[8];
            std::snprintf(fr, sizeof fr, "%u", r.frames);
            gt.put(fr, x + numW - gt.width(fr, fh), y + isz * 0.5f, fh, Color{210, 210, 210, a});
            float bx = x + numW + 8.f;
            drawDir(r.dir, bx + isz * 0.5f, y + isz * 0.5f, isz, Color{255, 255, 255, a});
            bx += isz + 4.f;
            for (const B &b : kB)
                if (r.btn & b.bit)
                {
                    if (!wotm_prompts::drawCell2d(wotm_prompts::cellFor(b.bind), bx + isz * 0.5f, y + isz * 0.5f, isz, float(a) / 255.f))
                        gt.put(b.txt, bx, y + isz * 0.5f, fh, Color{255, 230, 120, a});
                    bx += isz + 2.f;
                }
        }
        x += w + fh * 1.5f;
    }
    gt.flush(scene);
}

// Tabela dos socos do P1, no lado esquerdo abaixo dos comandos: os 6 tipos da posicao do combo em que ele esta (HIT 1 fora do soco),
// com a janela do teste (% da animacao), o dano pedido, o raio da esfera e o atordoamento (> 100 atordoa; K: derruba). Tipos pelo
// PadFlags::curMappedAction que o StatePunch::transitionOK aceita: 5 quadrado, 3 frente + quadrado, 4 tras + quadrado, 0 triangulo,
// 1 frente + triangulo, 2 tras + triangulo. Embaixo, o soco em curso: a janela na barra e o ponto da animacao.
// Com um objeto na mao, a tabela e a dos golpes com objeto (StateBatSwipe, mesmos botoes): WEAPON HIT n, e a coluna do dano traz o
// multiplicador do dano do objeto ("x1.5").
void drawMoveTable(wotm::Scene &scene, GameText &gt, const View &v, float top)
{
    namespace st = wotm::settings;
    const int rh = gt.rh;
    const float fh = std::max(11.f, float(rh) / 50.f) * 0.9f, isz = fh * 1.2f, rowH = isz + 3.f;
    const wotm::GsScreenRect gr = wotm::gsScreenRect(float(gt.rw), float(rh));
    const float x = gr.x0 + fh * 1.25f;
    struct Row { int type, dir; st::Bind bind; const char *txt; };
    static constexpr Row kRows[kPunchTypes] = {{5, 5, st::BSquare, "LT"}, {3, 8, st::BSquare, "LT"}, {4, 2, st::BSquare, "LT"},
                                               {0, 5, st::BTriangle, "HV"}, {1, 8, st::BTriangle, "HV"}, {2, 2, st::BTriangle, "HV"}};
    const bool favs = !v.fav.empty();
    const float a = gt.adv(fh), cIcons = x, cWin = x + isz * 2.f + 8.f + (favs ? a * 2.6f : 0.f);   // com favoritos, lugar do "x n"/"W"
    // os textos das 4 colunas de cada linha antes de desenhar: a largura de cada coluna e a do maior texto dela (os favoritos trazem
    // alcances de 4 algarismos e danos com decimal, que nao cabem numa largura fixa)
    struct Cell { char win[16], dmg[16], rad[16], stun[16]; PunchCfg cfg; };
    Cell cells[16];
    const int nFav = favs ? std::min(int(v.fav.size()), 16) : kPunchTypes;
    for (int r = 0; r < nFav; ++r)
    {
        Cell &ce = cells[r];
        const Move *mv = favs ? &v.fav[r] : nullptr;
        if (mv)
        {
            ce.cfg.valid = true; ce.cfg.weapon = mv->kind == MkWeapon; ce.cfg.ini = mv->ini; ce.cfg.fim = mv->fim; ce.cfg.dmg = mv->dmg;
            ce.cfg.radius = mv->radius; ce.cfg.stun = mv->stun; ce.cfg.kb = mv->kb;
        }
        else
            ce.cfg = v.table[kRows[r].type];
        const PunchCfg &c = ce.cfg;
        const bool otherMv = mv && mv->kind == MkOther;
        if (!c.valid) { std::snprintf(ce.win, sizeof ce.win, "-"); ce.dmg[0] = ce.rad[0] = ce.stun[0] = 0; continue; }
        if (otherMv && !(c.fim > 0.f)) std::snprintf(ce.win, sizeof ce.win, "-");   // janela so quando existe (levantar)
        else std::snprintf(ce.win, sizeof ce.win, "%.0f-%.0f", c.ini * 100.f, c.fim * 100.f);
        if (c.weapon) std::snprintf(ce.dmg, sizeof ce.dmg, "x%.1f", c.dmg);
        else if (otherMv && !(c.dmg > 0.f)) std::snprintf(ce.dmg, sizeof ce.dmg, "-");
        else std::snprintf(ce.dmg, sizeof ce.dmg, c.dmg < 10.f && c.dmg != float(int(c.dmg)) ? "%.1f" : "%.0f", c.dmg);
        if (otherMv && !(c.radius > 0.f)) std::snprintf(ce.rad, sizeof ce.rad, "-");
        else std::snprintf(ce.rad, sizeof ce.rad, "%.0f", c.radius);
        std::snprintf(ce.stun, sizeof ce.stun, "%.0f%s", c.stun, c.kb != 0.f ? " K" : "");
    }
    float wWin = gt.width("WIN", fh), wDmg = gt.width("DMG", fh), wRad = gt.width("RAD", fh), wStun = gt.width("STUN", fh);
    for (int r = 0; r < nFav; ++r)
    {
        wWin = std::max(wWin, gt.width(cells[r].win, fh)); wDmg = std::max(wDmg, gt.width(cells[r].dmg, fh));
        wRad = std::max(wRad, gt.width(cells[r].rad, fh)); wStun = std::max(wStun, gt.width(cells[r].stun, fh));
    }
    const float gap = a * 1.3f;
    const float cDmg = cWin + wWin + gap, cRad = cDmg + wDmg + gap, cStun = cRad + wRad + gap, right = cStun + wStun + gap * 0.5f;
    const PunchLive &p = v.live[0];
    const bool now = p.frame && v.frame - p.frame <= 1u && p.cfg.valid;
    // com favoritos da MOVE LIST, a caixa mostra esses golpes (icone do botao, "x n" nos combos, "W" nos de objeto) no lugar dos 6
    // tipos da posicao; a linha acende quando o golpe em curso e esse botao (no combo, enquanto a posicao nao passar do fim dele)
    const int nRows = favs ? int(v.fav.size()) : kPunchTypes;
    const int rows = nRows + 2;
    DrawRectangle(int(x) - 6, int(top) - 6, int(right - x) + 12, int(rowH * float(rows) + fh * 1.2f) + 12, Color{0, 0, 0, 130});
    char b[48];
    if (favs)
        std::snprintf(b, sizeof b, "FAVORITES");
    else
        std::snprintf(b, sizeof b, "%s  HIT %d", v.tableWeapon ? "WEAPON" : "PUNCHES", v.tableTier + 1);
    gt.put(b, x, top + isz * 0.5f, fh, Color{120, 255, 160, 255});
    const Color kHead{150, 150, 150, 255};
    const float hy = top + rowH + isz * 0.5f;
    gt.put("WIN", cWin, hy, fh, kHead);
    gt.put("DMG", cDmg, hy, fh, kHead);
    gt.put("RAD", cRad, hy, fh, kHead);
    gt.put("STUN", cStun, hy, fh, kHead);
    auto rowOf = [&](int type) -> const Row & {
        for (const Row &r : kRows)
            if (r.type == type) return r;
        return kRows[0];
    };
    for (int r = 0; r < nRows; ++r)
    {
        const Move *mv = favs ? &v.fav[r] : nullptr;
        const bool otherMv = mv && mv->kind == MkOther;
        const Row &row = mv ? rowOf(otherMv ? 5 : mv->type) : kRows[r];
        const Cell &ce = cells[r];
        const PunchCfg &c = ce.cfg;
        const float y = top + rowH * float(r + 2), cy = y + isz * 0.5f;
        // MkOther: o estado do P1 e, nos golpes do agarrao (mesmo estado), a animacao +0x50 (0xBA = o especial, GRAB: HEAVY)
        bool cur = otherMv ? (v.p1State != 0 && v.p1State == mv->state && (mv->variant == 0 || (mv->variant == 2) == (v.p1StateAnim == 0xBAu)))
                           : (now && p.type == row.type);
        if (mv && !otherMv && cur)
            cur = mv->kind == MkWeapon ? p.cfg.weapon : (!p.cfg.weapon && (mv->kind == MkCombo ? p.tier < int(mv->len) : p.tier == 0));
        const unsigned char al = c.valid ? 255 : 90;
        if (cur)
            DrawRectangle(int(x) - 4, int(y) - 1, int(right - x) + 8, int(rowH), Color{255, 230, 120, 50});
        if (otherMv)
        {   // nome curto no lugar dos icones (letras menores para caber ate a coluna da janela)
            const float nw = gt.width(mv->name, fh), avail = cWin - cIcons - 6.f, nh = nw > avail ? fh * avail / nw : fh * 0.9f;
            gt.put(mv->name, cIcons, cy, nh, Color{255, 230, 120, al});
        }
        else
        {
            drawDir(row.dir, cIcons + isz * 0.5f, cy, isz, Color{255, 255, 255, al});
            if (!wotm_prompts::drawCell2d(wotm_prompts::cellFor(row.bind), cIcons + isz * 1.5f + 2.f, cy, isz, float(al) / 255.f))
                gt.put(row.txt, cIcons + isz + 2.f, cy, fh, Color{255, 230, 120, al});
        }
        if (mv && !otherMv && mv->kind != MkPunch)
        {
            char tag[8];
            if (mv->kind == MkCombo) std::snprintf(tag, sizeof tag, "x%d", int(mv->len));
            else std::snprintf(tag, sizeof tag, "W");
            gt.put(tag, cIcons + isz * 2.f + 6.f, cy, fh * 0.8f, Color{255, 230, 120, al});
        }
        if (!c.valid)
        {
            gt.put("-", cWin, cy, fh, Color{150, 150, 150, al});
            continue;
        }
        const Color t = cur ? Color{255, 240, 150, 255} : Color{235, 235, 235, 255};
        gt.put(ce.win, cWin, cy, fh, t);
        gt.put(ce.dmg, cDmg, cy, fh, t);
        gt.put(ce.rad, cRad, cy, fh, t);
        gt.put(ce.stun, cStun, cy, fh, c.stun > 100.f ? Color{255, 150, 40, 255} : t);
    }
    // barra do soco em curso: a janela (azul) e o ponto da animacao (branco); vazia fora do soco
    const float by = top + rowH * float(rows) + fh * 0.2f, bw = right - x, bh = fh * 0.6f;
    DrawRectangle(int(x), int(by), int(bw), int(bh), Color{70, 70, 70, 200});
    if (now)
    {
        const float w0 = std::clamp(p.cfg.ini, 0.f, 1.f), w1 = std::clamp(p.cfg.fim, 0.f, 1.f);
        DrawRectangle(int(x + bw * w0), int(by), std::max(1, int(bw * (w1 - w0))), int(bh),
                      p.inWindow ? Color{255, 60, 60, 230} : Color{80, 200, 255, 200});
        if (p.pct >= 0.f)
            DrawRectangle(int(x + bw * std::clamp(p.pct, 0.f, 1.f)) - 1, int(by) - 2, 3, int(bh) + 4, RAYWHITE);
    }
    gt.flush(scene);
}

// Numeros por cima das barras do HUD (o do jogador 1 e o do boneco, o HUD 1 do jogo: trainingHud no wotm_split4.inc): vida "atual / maxima"
// no meio da barra e, na stamina vertical, o valor atual com os algarismos empilhados dentro dela; contorno preto para ler sobre a cor
void drawHudNumbers(GameText &gt, const View &v)
{
    if (!(g_opts.load() & kTrainHud) || v.players != 1 || !v.hudShown)
        return;
    namespace th = wotm_training::hud;
    const wotm::GsScreenRect gr = wotm::gsScreenRect(float(gt.rw), float(gt.rh));
    const float sx = gr.w / 640.f, sy = float(gt.rh) / 224.f;
    auto mid = [&](const th::Box &b) { return Vector2{gr.x0 + 0.5f * (b.x0 + b.x1) * sx, 0.5f * (b.y0 + b.y1) * sy}; };
    const float hh = 5.2f * sy, sh = 4.4f * sy;   // altura das letras: a barra de vida tem 5,4 de 224; a stamina, 11 de 640 de largura
    int cpu = -1;
    for (int i = 1; i < 16 && cpu < 0; ++i)
        if (v.maxHealth[i] > 0.f)
            cpu = i;
    for (int i : {0, cpu})
    {
        if (i < 0 || !(v.maxHealth[i] > 0.f))
            continue;
        const bool mir = i != 0;
        char b[48];
        std::snprintf(b, sizeof b, "%.0f / %.0f", std::max(0.f, std::ceil(v.health[i])), v.maxHealth[i]);
        const Vector2 hc = mid(mir ? th::mirror(th::kHealth) : th::kHealth);
        gt.center(b, hc.x, hc.y, hh, RAYWHITE, true);
        std::snprintf(b, sizeof b, "%.0f", std::max(0.f, v.stamina[i]));
        const Vector2 sc = mid(mir ? th::mirror(th::kStamina) : th::kStamina);
        const int n = int(std::strlen(b));
        const float step = sh * 1.2f;
        for (int k = 0; k < n; ++k)
        {
            const char d[2] = {b[k], 0};
            gt.center(d, sc.x, sc.y + (float(k) - 0.5f * float(n - 1)) * step, sh, RAYWHITE, true);
        }
    }
}

// Painel de dano, dentro do quadro nativo (rw x rh; sai nas fotos do WOTM_SHOTS, some no modo foto, na pausa e fora da luta); tambem
// os comandos e os numeros do HUD. Linhas curtas: a fonte do jogo e larga.
void drawPanel(wotm::Scene &scene, const wotm::Ram &ram, uint64_t frameTex0, int rw, int rh)
{
    // atlas do fontSetTexId (a luta nao escreve texto do jogo); sem ele ainda, o do ultimo quadro com texto
    const uint64_t known = g_fontTex0.load(std::memory_order_relaxed), tex0 = known ? known : frameTex0;
    if (!g_on.load() || windowsHidden() || !tex0)
        return;
    const View v = snapshot();
    GameText gt{ram, tex0, rw, rh, {}};
    drawHudNumbers(gt, v);
    gt.flush(scene);
    if (g_opts.load() & kShowInputs)
        drawInputs(scene, gt, v);
    if (g_opts.load() & kMoveTable)
    {
        // abaixo da caixa dos comandos (as linhas todas, cheia), ou no lugar dela
        const float ifh = std::max(12.f, float(rh) / 50.f);
        float top = float(rh) * kPanelTop;
        if (g_opts.load() & kShowInputs)
            top += (ifh * 1.1f + 4.f) * float(inputRows() + 1) + 12.f + ifh;
        drawMoveTable(scene, gt, v, top);
    }
    if (!(g_opts.load() & kPanel))
        return;
    const float fh = std::max(12.f, float(rh) / 50.f), lineH = fh * 1.35f, pad = fh * 0.6f;
    const Color kTitle{120, 255, 160, 255}, kText{235, 235, 235, 255}, kDimText{150, 150, 150, 255};
    struct Line { std::string s; Color c; };
    std::vector<Line> lines;
    lines.push_back({"TRAINING", kTitle});
    char b[128];
    if (!v.hits.empty())
    {
        const Hit &last = v.hits.back();
        // o dano pedido pelo golpe (base) so aparece quando o aplicado foi outro (bloqueio, furia, dificuldade)
        if (last.base >= 0.f && std::fabs(last.base - last.applied) > 0.05f)
            std::snprintf(b, sizeof b, "LAST %.1f %s  BASE %.1f", last.applied, sourceName(last.source), last.base);
        else
            std::snprintf(b, sizeof b, "LAST %.1f %s", last.applied, sourceName(last.source));
        lines.push_back({b, kText});
        // combo: o da thread do EE (golpes que a vitima levou sem se recuperar, wotm_training.inc, takeDamage); terminou quando a reacao
        // depois do ultimo golpe fechou (o StunRec so entra no fim dela) ou 3 s sem golpe
        const Combo &c = v.combo;
        bool ended = v.frame - c.last > 180u;
        for (const StunRec &s : v.stuns)
            if (s.victim == c.victim && s.end > c.last + 1)
                ended = true;
        std::snprintf(b, sizeof b, "COMBO %d HIT%s %.1f", c.hits, c.hits == 1 ? "" : "S", c.damage);
        lines.push_back({b, ended ? kDimText : kText});
        std::snprintf(b, sizeof b, "BEST %.1f  %d HIT%s", v.bestCombo, v.bestHits, v.bestHits == 1 ? "" : "S");
        lines.push_back({b, kText});
        lines.push_back({"", kText});
        int shown = 0;
        for (auto it = v.hits.rbegin(); it != v.hits.rend() && shown < 4; ++it, ++shown)
        {
            std::snprintf(b, sizeof b, "%5.1f %s > %s %s", it->applied, slotName(it->attacker, v.players).c_str(),
                          slotName(it->victim, v.players).c_str(), sourceName(it->source));
            lines.push_back({b, kDimText});
        }
    }
    // dados de quadro: os ultimos golpes (inicio = primeiro quadro com teste de golpe; ativo = quadros com teste; total = o estado inteiro)
    // e, no que acertou, quanto a vitima ficou sem agir (reacoes seguidas somadas) e a vantagem (fim da reacao - fim do golpe)
    if (!v.moves.empty())
    {
        lines.push_back({"", kText});
        lines.push_back({"FRAME DATA", kTitle});
        int shown = 0;
        for (auto it = v.moves.rbegin(); it != v.moves.rend() && shown < 3; ++it, ++shown)
        {
            const MoveRec &mv = *it;
            if (mv.chain >= 0)   // cadeia = posicao na sequencia (0-based), passo = tipo do soco (tabela kMoveTypeNames)
                std::snprintf(b, sizeof b, "%s %s HIT %d %s", slotName(mv.owner, v.players).c_str(), mv.name, mv.chain + 1,
                              mv.step >= 0 && mv.step < kPunchTypes ? kMoveTypeNames[mv.step] : "?");
            else
                std::snprintf(b, sizeof b, "%s %s", slotName(mv.owner, v.players).c_str(), mv.name);
            lines.push_back({b, kText});
            char st[8];
            if (mv.startup > 0)
                std::snprintf(st, sizeof st, "%d", mv.startup);
            else
                std::snprintf(st, sizeof st, "-");
            std::snprintf(b, sizeof b, "  STARTUP %s ACTIVE %d TOTAL %u", st, mv.active, mv.end - mv.start);
            lines.push_back({b, kDimText});
            if (mv.hitVictim >= 0)
            {
                uint32_t s0 = 0, s1 = 0;
                const char *sname = nullptr;
                for (const StunRec &s : v.stuns)
                {
                    if (s.victim != mv.hitVictim)
                        continue;
                    if (!sname && s.start >= mv.start && s.start <= mv.end + 1)
                    {
                        s0 = s.start;
                        s1 = s.end;
                        sname = s.name;
                    }
                    else if (sname && s.start == s1)   // reacao emendada (recuo -> derrubado)
                        s1 = s.end;
                }
                if (sname)
                    std::snprintf(b, sizeof b, "  %s %u  ADV %+d", sname, s1 - s0, int(s1) - int(mv.end));
                else
                    std::snprintf(b, sizeof b, "  HIT %s", slotName(mv.hitVictim, v.players).c_str());
                lines.push_back({b, kDimText});
            }
        }
    }
    float w = 0.f;
    for (const Line &l : lines)
        w = std::max(w, gt.width(l.s.c_str(), fh));
    // a direita, dentro da faixa 4:3 do jogo, abaixo do HUD do boneco (que desce ate ~40% da altura)
    const wotm::GsScreenRect gr = wotm::gsScreenRect(float(rw), float(rh));
    const float x = gr.x0 + gr.w - w - 2.f * pad - fh, top = float(rh) * kPanelTop;
    DrawRectangle(int(x - pad), int(top - pad), int(w + 2.f * pad), int(float(lines.size()) * lineH + 2.f * pad), Color{0, 0, 0, 150});
    for (size_t i = 0; i < lines.size(); ++i)
        if (!lines[i].s.empty())
            gt.put(lines[i].s.c_str(), x, top + lineH * (float(i) + 0.5f), fh, lines[i].c);
    gt.flush(scene);
}
}
