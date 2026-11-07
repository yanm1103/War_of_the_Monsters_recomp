// "Eat civilians" mod, the feature itself (wotm_overrides.cpp includes it after wotm_eat.inc, which only logs).
//
// Double tap of Circle (the grab/throw button) with a monster that can eat (Togera by default):
//   1st tap: with nothing in the hands and a civilian within reach, the nearest one is "grabbed": it follows the monster's hand.
//   2nd tap: the civilian is eaten: it is turned off (position w = -1, the same mark pedsTurnOff leaves), the monster heals and
//            the player gets points. A civilian that is not eaten within a few seconds is let go.
// Everything is read and written in guest memory from a Monster::update hook, so no game function is replaced.
// Toss and gulp: the 1st tap scoops the nearest civilian and holds it out; the 2nd tap (it waits for the grab clip to end) tosses it, the head
// goes back, the jaw opens and closes on it, and the eat (heal + points, the civilian gone) happens as the jaw closes.
// The grab/eat clips are played over whatever state the monster is in; when a clip is over (or the civilian is let go) and the state
// has not changed, the idle state is entered again so the game's own animation comes back.
//
// Pedestrian coordinates: x and y are the street, the third is the height (0 on the ground), the same axes as a monster's locators
// (z up). Grab and eat are confirmed in a fight; the carry (the height follows the hand) and the animations are not yet.
//
//   WOTM_EAT=1|all        off when unset. 1: Togera only. all: every monster
//   WOTM_EATPLANE=xy|xz   which monster coordinates line up with a civilian's (x, y): xy (default; z is up) or xz
//   WOTM_EATLIFT=0        the carried civilian stays on the ground; by default its height (third coordinate) follows the hand
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
    bool on = false, all = false, planeXZ = false, lift = true;
    float healPct = 5.f, reach = 300.f;
    int points = 25;
} g_cfg;

struct Hold
{
    bool active = false, prevCircle = false;
    uint32_t logged = 0;   // the monster whose animation handles were last logged
    uint32_t pos = 0;   // guest address of the held civilian's 16-byte position
    std::chrono::steady_clock::time_point since;
    // Our clip is the visible animation until the game's state machine starts another one (it only does so when the state changes), so
    // the clip's state is remembered: if it is still the monster's state when the clip is over, the idle state is entered again
    bool clip = false;
    uint32_t clipState = 0, clipId = 0;
    bool eating = false, queued = false, tossed = false;   // the eat clip is running; a tap came during the grab clip; the civilian left the hand
    std::chrono::steady_clock::time_point eatStart;
    float from[3] = {0, 0, 0};   // where the civilian left the hand
    std::chrono::steady_clock::time_point clipEnd;
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

constexpr uint32_t kState = 0x34, kIdleState = 0x7984;   // Monster::m_state; the StateIdle embedded in Monster
constexpr double kGrabSeconds = 82.0 / 60.0, kEatSeconds = 122.0 / 60.0;   // clip lengths (tools/eatanim.py: 81 and 121 frames at 60 fps)
// Frames of the eat clip (tools/eatanim.py TOSS_FRAME, ARRIVE_FRAME, EAT_FRAME): the hand lets go, the civilian reaches the open mouth, the jaw closes on it
constexpr double kTossT = 20.0 / 60.0, kArriveT = 34.0 / 60.0, kEatT = 50.0 / 60.0;
constexpr uint32_t kMouth = 0x3E50;   // locator 0x80B: monster + id * 16 - 0x4260
constexpr float kTossArc = 40.f;   // extra height of the toss at its top, in game units

bool playAnim(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, Hold &hold, uint32_t m, uint32_t id)
{
    const uint32_t h = m + kAnims + kAnimStride * id;
    if (!rd32(rdram, h))
    {
        static bool said = false;
        if (!said)
            std::fprintf(stderr, "[eatmod] este modelo nao tem a animacao %u (tools/eatanim.py + modkit monster anim import); segue sem animacao\n", id);
        said = true;
        return false;
    }
    wotm_coop::callF(rdram, ctx, runtime, fn::animationTransitionInto__FR11_animHandlefii, {h, 1u, 1u}, {kAnimBlend});
    hold.clip = true;
    hold.clipId = id;
    hold.clipState = rd32(rdram, m + kState);
    hold.clipEnd = std::chrono::steady_clock::now() +
                   std::chrono::milliseconds(int((id == kAnimGrab ? kGrabSeconds : kEatSeconds) * 1000.0));
    return true;
}

// Gives the animation back to the game: if the monster is still in the state it was in when the clip started, enter idle again (the game
// goes on to run/walk by itself next frame if the stick is held). A state that changed already started its own animation
void endClip(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, Hold &hold, uint32_t m)
{
    if (!hold.clip)
        return;
    hold.clip = false;
    if (rd32(rdram, m + kState) == hold.clipState)
        wotm_modes4::call(rdram, ctx, runtime, fn::enterNewState__7MonsterP12MonsterState, {m, m + kIdleState});
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

// Lets the civilian go back on the street (a carried one is up in the air when the grab ends without an eat)
void release(uint8_t *rdram, Hold &h)
{
    if (h.active && alive(rdram, h.pos) && g_cfg.lift && !g_cfg.planeXZ)
        wrF(rdram, h.pos + 8, 0.f);
    h.active = h.eating = h.queued = h.tossed = false;
}

// The carried civilian follows the right hand; a civilian's third coordinate is its height (0 on the street), so it goes up with the hand
void carry(uint8_t *rdram, uint32_t m, const Hold &h)
{
    float a, b;
    ground(rdram, m + kHandR, &a, &b);
    wrF(rdram, h.pos, a);
    wrF(rdram, h.pos + 4, b);
    if (g_cfg.lift && !g_cfg.planeXZ)
        wrF(rdram, h.pos + 8, rdF(rdram, m + kHandR + 8));
}

// The toss: from where the hand let go to the mouth locator, along an arc
void fly(uint8_t *rdram, uint32_t m, Hold &h, double t)
{
    if (!h.tossed)
    {
        h.tossed = true;
        h.from[0] = rdF(rdram, h.pos);
        h.from[1] = rdF(rdram, h.pos + 4);
        h.from[2] = rdF(rdram, h.pos + 8);
    }
    const float u = float(std::min(1.0, (t - kTossT) / (kArriveT - kTossT)));
    float a, b;
    ground(rdram, m + kMouth, &a, &b);
    wrF(rdram, h.pos, h.from[0] + (a - h.from[0]) * u);
    wrF(rdram, h.pos + 4, h.from[1] + (b - h.from[1]) * u);
    if (g_cfg.lift && !g_cfg.planeXZ)
    {
        const float z = rdF(rdram, m + kMouth + 8);
        wrF(rdram, h.pos + 8, h.from[2] + (z - h.from[2]) * u + kTossArc * 4.f * u * (1.f - u));
    }
}

// One Monster::update of a monster that can eat; player is its index in m_monsters[0..3]
void tick(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t m, int player)
{
    Hold &h = g_hold[player];
    const bool circle = (wotm_modes4::call(rdram, ctx, runtime, kPadButtons, {uint32_t(player)}) & kBtnCircle) != 0;
    const bool tap = circle && !h.prevCircle;
    if (tap && h.logged != m)
    {
        // Which model this is and which animation handles it has: the grab/eat clips only play if 298/299 are there
        h.logged = m;
        std::fprintf(stderr, "[eatmod] jogador %d: tipo %u traje do arquivo %d; m_anims[90]=%08x [96]=%08x [298]=%08x [299]=%08x\n", player + 1,
                     rd32(rdram, m + kTypeBits) >> 5, wotm_custom::monsterCostume(rdram, m), rd32(rdram, m + kAnims + kAnimStride * 90),
                     rd32(rdram, m + kAnims + kAnimStride * 96), rd32(rdram, m + kAnims + kAnimStride * kAnimEat),
                     rd32(rdram, m + kAnims + kAnimStride * kAnimGrab));
    }
    h.prevCircle = circle;

    const auto now = std::chrono::steady_clock::now();
    const uint32_t cs = rd32(rdram, m + kCs) & 0x1FFFFFFFu;
    if (!cs || rd8(rdram, m + kDead) != 0 || !(rdF(rdram, m + kHealth) > 0.f))
    {
        h.active = h.eating = h.queued = h.tossed = h.clip = false;
        return;
    }
    if (h.clip)
    {
        if (rd32(rdram, m + kState) != h.clipState)
            h.clip = false;   // the game's own state machine started another animation
        else if (!h.active && now > h.clipEnd)
            endClip(rdram, ctx, runtime, h, m);
    }
    if (h.active)
    {
        if (!alive(rdram, h.pos))
        {
            release(rdram, h);
            endClip(rdram, ctx, runtime, h, m);
            return;
        }
        if (h.eating)
        {
            const double t = std::chrono::duration<double>(now - h.eatStart).count();
            if (!h.clip)   // hit or otherwise sent to another state in the middle of the toss: the civilian drops
            {
                release(rdram, h);
                return;
            }
            if (t >= kEatT)
            {
                eat(rdram, ctx, runtime, m, player, h.pos);
                h.active = h.eating = h.queued = h.tossed = false;   // the clip plays on to its end
                return;
            }
            if (t < kTossT)
                carry(rdram, m, h);
            else
                fly(rdram, m, h, t);
            return;
        }
        const bool grabbing = h.clip && h.clipId == kAnimGrab && now <= h.clipEnd;
        if (std::chrono::duration<double>(now - h.since).count() > kHoldSeconds)
        {
            release(rdram, h);
            endClip(rdram, ctx, runtime, h, m);   // let go: out of the pose that held it
            return;
        }
        if (tap)
            h.queued = true;
        if (h.queued && !grabbing)   // the second press waits for the grab clip to finish
        {
            h.queued = false;
            if (playAnim(rdram, ctx, runtime, h, m, kAnimEat))
            {
                h.eating = true;
                h.tossed = false;
                h.eatStart = now;
            }
            else   // a model without the clips: eaten at once
            {
                eat(rdram, ctx, runtime, m, player, h.pos);
                h.active = false;
                return;
            }
        }
        carry(rdram, m, h);
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
    playAnim(rdram, ctx, runtime, h, m, kAnimGrab);
    h.active = true;
    h.eating = h.queued = h.tossed = false;
    h.pos = p;
    h.since = now;
    std::fprintf(stderr, "[eatmod] jogador %d pegou um civil a %.0f (%.1f %.1f)\n", player + 1, d, rdF(rdram, p), rdF(rdram, p + 4));
}

// Monster::updateLookAt turns the head toward a target (the nearest enemy, the locked or reticle target, a vehicle that can be picked up)
// with two animation layers (m_anims[154] in the AnimBlend at +0x2FE0 and [155] at +0x3048; 50% is the neutral pose). Over the grab and
// eat clips that tilts the head awkwardly, so while a clip is on the layers are put back to neutral right after the game's update
constexpr uint32_t kUpdateLookAt = fn::updateLookAt__7Monster, kLookBlendA = 0x2FE0, kLookBlendB = 0x3048;
PS2Runtime::RecompiledFunction g_origLookAt = nullptr;

void lookAtUpdate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t m = ctx->pc == kUpdateLookAt ? GPR_U32(ctx, 4) : 0;
    g_origLookAt(rdram, ctx, runtime);
    if (!m)
        return;
    for (uint32_t i = 0; i < 4; ++i)
        if (g_hold[i].clip && rd32(rdram, kGame + kMonsters + 4 * i) == m)
        {
            wotm_coop::callF(rdram, ctx, runtime, fn::setPercent__9AnimBlendf, {m + kLookBlendA}, {0.5f});
            wotm_coop::callF(rdram, ctx, runtime, fn::setPercent__9AnimBlendf, {m + kLookBlendB}, {0.5f});
            break;
        }
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
        g_cfg.planeXZ = std::strcmp(s, "xz") == 0;
    if (const char *s = std::getenv("WOTM_EATLIFT"))
        g_cfg.lift = *s != '0';
    if (const char *s = std::getenv("WOTM_EATHEAL"))
        g_cfg.healPct = std::max(0.f, float(std::atof(s)));
    if (const char *s = std::getenv("WOTM_EATPOINTS"))
        g_cfg.points = std::max(0, std::atoi(s));
    if (const char *s = std::getenv("WOTM_EATREACH"))
        g_cfg.reach = std::max(1.f, float(std::atof(s)));
    if (!(g_origUpdate = runtime.lookupFunction(kMonsterUpdate)))
        return;
    runtime.registerFunction(kMonsterUpdate, &monsterUpdate);
    if ((g_origLookAt = runtime.lookupFunction(kUpdateLookAt)))
        runtime.registerFunction(kUpdateLookAt, &lookAtUpdate);
    std::fprintf(stderr, "[eatmod] comer civis ligado (%s; plano %s; vida %.0f%%, %d pontos, alcance %.0f)\n", g_cfg.all ? "todos os monstros" : "Togera",
                 g_cfg.planeXZ ? "xz" : "xy", g_cfg.healPct, g_cfg.points, g_cfg.reach);
}
} // namespace wotm_eatmod
