// "Eat civilians" mod, the feature itself (wotm_overrides.cpp includes it after wotm_eat.inc, which only logs).
//
// Double tap of Circle (the grab/throw button) with a monster that can eat (Togera by default):
//   1st tap: with nothing in the hands and a civilian within reach, the nearest one is "grabbed": it follows the monster's hand.
//   2nd tap: the civilian is eaten: it is turned off (position w = -1, the same mark pedsTurnOff leaves), the monster heals and
//            the player gets points. A civilian that is not eaten within a few seconds is let go.
// Everything is read and written in guest memory from a Monster::update hook, so no game function is replaced.
//
// Untested against a running game: the civilians were frozen in every source build when this was written, so the pedestrian
// plane (which two of the three coordinates are the ground) and the carry position are assumptions. The first one is switchable
// and both are logged (WOTM_EATPLANE, and the "[eatmod]" lines).
//
//   WOTM_EAT=1|all        off when unset. 1: Togera only. all: every monster
//   WOTM_EATPLANE=xz|xy   which monster coordinates line up with a civilian's (x, y): xz (default) or xy
//   WOTM_EATHEAL=<n>      health restored per civilian, percent of the monster's maximum (default 5)
//   WOTM_EATPOINTS=<n>    tokens credited per civilian (default 25)
//   WOTM_EATREACH=<n>     how close a civilian has to be to be grabbed, in game units (default 300)

namespace wotm_eatmod
{
using wotm_modes4::rd32;
using wotm_modes4::rd8;
using wotm_modes4::wr32;
using wotm_split4::rdF;
namespace fn = wotm::fn;

constexpr uint32_t kGame = wotm::sym::shellGame;
constexpr uint32_t kMonsters = 0x120380, kTokens = 0x11C370, kTokenStride = 0x1004;   // TheGame::m_monsters[8], m_tokens[4]
constexpr uint32_t kTypeBits = 0x14, kPlayerNum = 0x18, kCs = 0xC, kCsTrans = 0x10, kDead = 0xE8;
constexpr uint32_t kHealth = 0x44C, kHealthMax = 0x448, kPickup = 0x68A4, kTarget = 0x68B4;   // Monster::m_pickup, m_target
constexpr uint32_t kHandL = 0x3DC0, kHandR = 0x3DD0;   // locators 0x802 / 0x803: monster + id * 16 - 0x4260
constexpr uint32_t kPadButtons = fn::inputGetPadButtons__Fi, kMonsterUpdate = fn::update__7Monster, kTokenCredit = fn::credit__12TokenManagerPci;
constexpr uint32_t kBtnCircle = 0x2000;   // libpad; inputGetPadButtons already reports pressed as 1
constexpr uint32_t kNumGroups = wotm::sym::x_8PedGroup_s_numPedGroups, kAllGroups = wotm::sym::x_8PedGroup_s_allPedGroups;
constexpr uint32_t kGroupPos = 0x168, kGroupCount = 0x17C;   // PedGroup: pointer to the pedestrians' 16-byte positions, how many
constexpr int kMaxGroups = 20;
constexpr uint32_t kTogera = 3;   // monster index (type bits >> 5): lizard

struct Config
{
    bool on = false, all = false, planeXZ = true;
    float healPct = 5.f, reach = 300.f;
    int points = 25;
} g_cfg;

struct Hold
{
    bool active = false, prevCircle = false;
    uint32_t pos = 0;   // guest address of the held civilian's 16-byte position
    std::chrono::steady_clock::time_point since;
} g_hold[4];

constexpr double kHoldSeconds = 6.0;

bool finite(uint32_t bits) { return (bits & 0x7F800000u) != 0x7F800000u; }
bool inRam(uint32_t a) { a &= 0x1FFFFFFFu; return a >= 0x100000u && a + 16u <= 0x2000000u; }

// Alive pedestrian: all three coordinates finite and w not negative (w < 0 as an int means gone). The runtime builds with
// /fp:fast, so everything is tested on the bits
bool alive(uint8_t *rdram, uint32_t p)
{
    return finite(rd32(rdram, p)) && finite(rd32(rdram, p + 4)) && finite(rd32(rdram, p + 8)) && static_cast<int32_t>(rd32(rdram, p + 12)) >= 0;
}

// Ground coordinates of a monster-side vector (cs translation or a locator), in the civilians' plane
void ground(uint8_t *rdram, uint32_t v, float *a, float *b)
{
    *a = rdF(rdram, v);
    *b = rdF(rdram, v + (g_cfg.planeXZ ? 8u : 4u));
}

// Nearest living civilian to (a, b) within `reach`; the position's guest address, or 0. *dist gets the distance found
uint32_t nearestPed(uint8_t *rdram, float a, float b, float reach, float *dist)
{
    uint32_t best = 0;
    float bestD = reach * reach;
    const uint32_t groups = std::min<uint32_t>(rd32(rdram, kNumGroups), kMaxGroups);
    for (uint32_t g = 0; g < groups; ++g)
    {
        const uint32_t grp = rd32(rdram, kAllGroups + 4 * g) & 0x1FFFFFFFu;
        if (!inRam(grp))
            continue;
        const uint32_t pos = rd32(rdram, grp + kGroupPos) & 0x1FFFFFFFu, n = std::min<uint32_t>(rd32(rdram, grp + kGroupCount), 256u);
        if (!pos || pos + 16u * n > 0x2000000u)
            continue;
        for (uint32_t k = 0; k < n; ++k)
        {
            const uint32_t p = pos + 16u * k;
            if (!alive(rdram, p))
                continue;
            const float dx = rdF(rdram, p) - a, dy = rdF(rdram, p + 4) - b, d = dx * dx + dy * dy;
            if (d < bestD)
                bestD = d, best = p;
        }
    }
    if (dist)
        *dist = std::sqrt(bestD);
    return best;
}

// Plays animation `id` of the monster (Monster::m_anims[id]; tools/eatanim.py adds 299 = grab and 298 = eat to a model). A model
// without that animation has no character instance in the handle, and nothing happens
constexpr uint32_t kAnims = 0x1CF0, kAnimStride = 0x10, kAnimGrab = 299, kAnimEat = 298;
constexpr float kAnimBlend = 20.f;

void playAnim(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t m, uint32_t id)
{
    const uint32_t h = m + kAnims + kAnimStride * id;
    if (!rd32(rdram, h))
        return;
    wotm_coop::callF(rdram, ctx, runtime, fn::animationTransitionInto__FR11_animHandlefii, {h, 1u, 1u}, {kAnimBlend});
}

bool canEat(uint32_t m)
{
    return g_cfg.all || ((m >> 5) == kTogera);
}

void wrF(uint8_t *rdram, uint32_t a, float v)
{
    uint32_t b;
    std::memcpy(&b, &v, 4);
    wr32(rdram, a, b);
}

// Health back, up to the maximum (the HUD reads Monster+0x44C every frame)
void heal(uint8_t *rdram, uint32_t m)
{
    const float max = rdF(rdram, m + kHealthMax), hp = rdF(rdram, m + kHealth);
    if (!(max > 0.f) || !(hp > 0.f))
        return;
    wrF(rdram, m + kHealth, std::min(max, hp + max * g_cfg.healPct * 0.01f));
}

void eat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t m, int player, uint32_t p)
{
    wr32(rdram, p + 12, 0xFFFFFFFFu);   // w = -1: what pedsTurnOff leaves, so the group stops drawing and moving it
    heal(rdram, m);
    if (g_cfg.points > 0)
        wotm_modes4::call(rdram, ctx, runtime, kTokenCredit,
                          {kGame + kTokens + kTokenStride * uint32_t(player), wotm_modes4::scratchText(rdram, "Civilians"), uint32_t(g_cfg.points)});
    std::fprintf(stderr, "[eatmod] jogador %d comeu um civil (%+.0f%% de vida, %d pontos)\n", player + 1, g_cfg.healPct, g_cfg.points);
}

// One Monster::update of a monster that can eat; player is its index in m_monsters[0..3]
void tick(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t m, int player)
{
    Hold &h = g_hold[player];
    const bool circle = (wotm_modes4::call(rdram, ctx, runtime, kPadButtons, {uint32_t(player)}) & kBtnCircle) != 0;
    const bool tap = circle && !h.prevCircle;
    h.prevCircle = circle;

    const uint32_t cs = rd32(rdram, m + kCs) & 0x1FFFFFFFu;
    if (!cs || rd8(rdram, m + kDead) != 0 || !(rdF(rdram, m + kHealth) > 0.f))
    {
        h.active = false;
        return;
    }
    if (h.active)
    {
        const bool timedOut = std::chrono::duration<double>(std::chrono::steady_clock::now() - h.since).count() > kHoldSeconds;
        if (timedOut || !alive(rdram, h.pos))
        {
            h.active = false;
            return;
        }
        if (tap)
        {
            playAnim(rdram, ctx, runtime, m, kAnimEat);
            eat(rdram, ctx, runtime, m, player, h.pos);
            h.active = false;
            return;
        }
        // carried: it follows the right hand on the ground plane (a civilian's third coordinate stays 0)
        float a, b;
        ground(rdram, m + kHandR, &a, &b);
        wrF(rdram, h.pos, a);
        wrF(rdram, h.pos + 4, b);
        return;
    }
    if (!tap || rd32(rdram, m + kPickup) != 0 || rd32(rdram, m + kTarget) != 0)   // something in the hands: the game's own grab/throw
        return;
    float a, b, d = 0.f;
    ground(rdram, cs + kCsTrans, &a, &b);
    const uint32_t p = nearestPed(rdram, a, b, g_cfg.reach, &d);
    if (!p)
    {
        // Not found: the log says how far the nearest one is under this plane, to tell a wrong plane from no civilians around
        float far = 0.f;
        nearestPed(rdram, a, b, 1e6f, &far);
        std::fprintf(stderr, "[eatmod] jogador %d: nenhum civil a %.0f (o mais perto a %.0f; plano %s; monstro em %.1f %.1f %.1f)\n", player + 1,
                     g_cfg.reach, far, g_cfg.planeXZ ? "xz" : "xy", rdF(rdram, cs + kCsTrans), rdF(rdram, cs + kCsTrans + 4), rdF(rdram, cs + kCsTrans + 8));
        return;
    }
    playAnim(rdram, ctx, runtime, m, kAnimGrab);
    h.active = true;
    h.pos = p;
    h.since = std::chrono::steady_clock::now();
    std::fprintf(stderr, "[eatmod] jogador %d pegou um civil a %.0f (%.1f %.1f)\n", player + 1, d, rdF(rdram, p), rdF(rdram, p + 4));
}

PS2Runtime::RecompiledFunction g_origUpdate = nullptr;

void monsterUpdate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t m = ctx->pc == kMonsterUpdate ? GPR_U32(ctx, 4) : 0;
    g_origUpdate(rdram, ctx, runtime);
    if (!m || !canEat(rd32(rdram, m + kTypeBits)))
        return;
    int player = -1;
    for (uint32_t i = 0; i < 4 && player < 0; ++i)
        if (rd32(rdram, kGame + kMonsters + 4 * i) == m)
            player = int(i);
    if (player >= 0 && rd32(rdram, m + kPlayerNum) != 2u)   // a person's monster, not the AI's
        tick(rdram, ctx, runtime, m, player);
}

void install(PS2Runtime &runtime)
{
    const char *e = std::getenv("WOTM_EAT");
    if (!e || !*e || *e == '0')
        return;
    g_cfg.on = true;
    g_cfg.all = std::strcmp(e, "all") == 0;
    if (const char *s = std::getenv("WOTM_EATPLANE"))
        g_cfg.planeXZ = std::strcmp(s, "xy") != 0;
    if (const char *s = std::getenv("WOTM_EATHEAL"))
        g_cfg.healPct = std::max(0.f, float(std::atof(s)));
    if (const char *s = std::getenv("WOTM_EATPOINTS"))
        g_cfg.points = std::max(0, std::atoi(s));
    if (const char *s = std::getenv("WOTM_EATREACH"))
        g_cfg.reach = std::max(1.f, float(std::atof(s)));
    if (!(g_origUpdate = runtime.lookupFunction(kMonsterUpdate)))
        return;
    runtime.registerFunction(kMonsterUpdate, &monsterUpdate);
    std::fprintf(stderr, "[eatmod] comer civis ligado (%s; plano %s; vida %.0f%%, %d pontos, alcance %.0f)\n", g_cfg.all ? "todos os monstros" : "Togera",
                 g_cfg.planeXZ ? "xz" : "xy", g_cfg.healPct, g_cfg.points, g_cfg.reach);
}
} // namespace wotm_eatmod
