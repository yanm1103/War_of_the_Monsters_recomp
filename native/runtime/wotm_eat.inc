// "Eat civilians" mod (wotm_overrides.cpp includes it, after wotm_ai.inc). Phase 0: probe only. It logs what the game does with the
// pedestrians (PedGroup) so the real hooks can be placed from evidence. Nothing in the game changes, and with WOTM_EATLOG unset no
// hook is installed.
//   WOTM_EATLOG=1  one line per call into PedGroup, capped per function:
//                  "[eat] <field> <function> a0..a3 f12..f15 [pos=(x y z w) fx=<n> slot=<k>] ra=<caller>"
//                  (a0..a3 are the integer argument registers and f12..f15 the float ones, printed raw because the calling
//                  convention of each signature is not confirmed yet; pos is the _fvector the call points at, fx/slot say which
//                  particle system and which pedestrian inside it that vector is)
//   WOTM_EATLOG=2  also dumps each PedGroup (0x190 bytes) at its 1st, 120th and 600th update and decodes every word that points
//                  into fx[] (draw type, particle count, first positions), to map the layout of the group
// What the log is meant to answer: who calls squish/squishGlobal (stomp, thrown object, vehicle), how a group refers to its particle
// system, and where one pedestrian's position lives, so a pedestrian can be picked, carried and turned off (pedsTurnOff).

namespace wotm_eat
{
using wotm_modes4::rd32;
using wotm_split4::rdF;
namespace fn = wotm::fn;

constexpr uint32_t kFieldCount = 0x006F8DBCu;   // same counter wotm_ai.inc reads (local of timer.c, no symbol)
constexpr uint32_t kFx = wotm::sym::fx, kFxStride = 0x2EE0, kFxCount = 100;
constexpr uint32_t kFxAttribs = 0x2E90, kFxPos = 0x2EB0, kFxNum = 0x2ED0;   // docs/FORMATS.md, "Particles and effects"
constexpr uint32_t kNumGroups = wotm::sym::x_8PedGroup_s_numPedGroups, kAllGroups = wotm::sym::x_8PedGroup_s_allPedGroups;
constexpr uint32_t kGroupSize = 0x190;   // docs/FORMATS.md, object id 0xBB8
constexpr int kMaxGroups = 20;           // s_allPedGroups is 0x50 bytes

struct Hook
{
    uint32_t addr;
    const char *name;
    int posArg;   // integer argument (0..3) that points at an _fvector worth printing; -1 none
    int cap;      // lines logged at most
    int seen;
    PS2Runtime::RecompiledFunction orig;
};

enum { kSquishGlobal, kSquish, kFleeGlobal, kFlee, kFindInRange, kTurnOff, kUpdate, kHooks };
Hook g_hooks[kHooks] = {
    {fn::squishGlobal__8PedGroupR8_fvectorfP7Monster, "PedGroup::squishGlobal", 0, 400},
    {fn::squish__8PedGroupR8_fvectorfP7Monster, "PedGroup::squish", 1, 400},
    {fn::fleeGlobal__8PedGroupR8_fvectorf, "PedGroup::fleeGlobal", 0, 100},
    {fn::flee__8PedGroupR8_fvectorfT1, "PedGroup::flee", 1, 100},
    {fn::findInRange__8PedGroupPUcUiR8_fvectorf, "PedGroup::findInRange", 3, 100},
    {fn::pedsTurnOff__FP8_fvectorb, "pedsTurnOff", 0, 400},
    {fn::update__8PedGroup, "PedGroup::update", -1, 4},
};
int g_level = 0;

int field(uint8_t *rdram) { return static_cast<int32_t>(rd32(rdram, kFieldCount)); }
bool inRam(uint32_t a) { a &= 0x1FFFFFFFu; return a >= 0x100000u && a + 16u <= 0x2000000u; }
int fxIndex(uint32_t a)
{
    a &= 0x1FFFFFFFu;
    return a >= kFx && a < kFx + kFxCount * kFxStride ? int((a - kFx) / kFxStride) : -1;
}

void logCall(uint8_t *rdram, R5900Context *ctx, const Hook &h)
{
    const uint32_t a[4] = {GPR_U32(ctx, 4), GPR_U32(ctx, 5), GPR_U32(ctx, 6), GPR_U32(ctx, 7)}, ra = GPR_U32(ctx, 31);
    char extra[160] = "";
    if (h.posArg >= 0 && inRam(a[h.posArg]))
    {
        const uint32_t p = a[h.posArg] & 0x1FFFFFFFu;
        int k = std::snprintf(extra, sizeof extra, " pos=(%.1f %.1f %.1f %.1f)", rdF(rdram, p), rdF(rdram, p + 4), rdF(rdram, p + 8), rdF(rdram, p + 12));
        if (const int i = fxIndex(p); i >= 0)
        {
            const uint32_t base = rd32(rdram, kFx + uint32_t(i) * kFxStride + kFxPos) & 0x1FFFFFFFu;
            if (base && p >= base)
                std::snprintf(extra + k, sizeof extra - k, " fx=%d slot=%u", i, (p - base) / 16u);
            else
                std::snprintf(extra + k, sizeof extra - k, " fx=%d", i);
        }
    }
    const wotm::symtab::Entry *e = wotm::symtab::find(ra);
    std::fprintf(stderr, "[eat] %d %s a0=%08x a1=%08x a2=%08x a3=%08x f12=%.2f f13=%.2f f14=%.2f f15=%.2f%s ra=%08x %s\n", field(rdram), h.name,
                 a[0], a[1], a[2], a[3], ctx->f[12], ctx->f[13], ctx->f[14], ctx->f[15], extra, ra, e ? e->name : "?");
}

// Raw words of a PedGroup, then every word that lands inside fx[] decoded
void dumpGroup(uint8_t *rdram, uint32_t g, int n)
{
    g &= 0x1FFFFFFFu;
    std::fprintf(stderr, "[eat] %d group %08x update #%d, %u groups, table:", field(rdram), g, n, rd32(rdram, kNumGroups));
    for (int i = 0; i < kMaxGroups; ++i)
        if (const uint32_t p = rd32(rdram, kAllGroups + 4u * uint32_t(i)))
            std::fprintf(stderr, " %08x", p);
    std::fprintf(stderr, "\n");
    for (uint32_t o = 0; o < kGroupSize; o += 32)
    {
        std::fprintf(stderr, "[eat]   +%03x:", o);
        for (uint32_t w = 0; w < 32 && o + w < kGroupSize; w += 4)
            std::fprintf(stderr, " %08x", rd32(rdram, g + o + w));
        std::fprintf(stderr, "\n");
    }
    for (uint32_t o = 0; o < kGroupSize; o += 4)
    {
        const uint32_t w = rd32(rdram, g + o);
        const int i = fxIndex(w);
        if (i < 0)
            continue;
        const uint32_t fx = kFx + uint32_t(i) * kFxStride, inside = (w & 0x1FFFFFFFu) - fx, pos = rd32(rdram, fx + kFxPos) & 0x1FFFFFFFu;
        const uint32_t attribs = rd32(rdram, fx + kFxAttribs), num = rd32(rdram, fx + kFxNum);
        std::fprintf(stderr, "[eat]   +%03x -> fx[%d]+%x attribs=%u numParticles=%u pos=%08x", o, i, inside, attribs, num, pos);
        if (attribs == 14 && inRam(pos))   // pedestrians: first positions (w < 0 means gone)
            for (uint32_t k = 0; k < std::min<uint32_t>(num, 3u); ++k)
                std::fprintf(stderr, " [%u](%.1f %.1f %.1f %.1f)", k, rdF(rdram, pos + 16u * k), rdF(rdram, pos + 16u * k + 4),
                             rdF(rdram, pos + 16u * k + 8), rdF(rdram, pos + 16u * k + 12));
        std::fprintf(stderr, "\n");
    }
}

void onUpdate(uint8_t *rdram, uint32_t g)
{
    static uint32_t s_group[kMaxGroups] = {};
    static int s_calls[kMaxGroups] = {};
    int s = 0;
    while (s < kMaxGroups && s_group[s] && s_group[s] != g)
        ++s;
    if (s == kMaxGroups)
        return;
    s_group[s] = g;
    const int n = ++s_calls[s];
    if (n == 1 || n == 120 || n == 600)
        dumpGroup(rdram, g, n);
}

// One instance per hooked function: the resume of a function paused in the middle comes back here with pc inside it, and only the
// entry (pc == addr) is logged
template <int I>
void hook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    Hook &h = g_hooks[I];
    if (ctx->pc == h.addr)
    {
        if (h.seen < h.cap)
        {
            ++h.seen;
            logCall(rdram, ctx, h);
            if (h.seen == h.cap)
                std::fprintf(stderr, "[eat] %s: line limit reached (%d)\n", h.name, h.cap);
        }
        if (I == kUpdate && g_level >= 2)
            onUpdate(rdram, GPR_U32(ctx, 4));
    }
    h.orig(rdram, ctx, runtime);
}

void install(PS2Runtime &runtime)
{
    if (const char *e = std::getenv("WOTM_EATLOG"); !e || !*e || *e == '0')
        return;
    else
        g_level = std::atoi(e) >= 2 ? 2 : 1;
    const PS2Runtime::RecompiledFunction hooks[kHooks] = {&hook<kSquishGlobal>, &hook<kSquish>, &hook<kFleeGlobal>, &hook<kFlee>,
                                                          &hook<kFindInRange>, &hook<kTurnOff>, &hook<kUpdate>};
    for (const Hook &h : g_hooks)
        if (!runtime.lookupFunction(h.addr))
        {
            std::fprintf(stderr, "[eat] off: %s (%x) has no function in the runtime\n", h.name, h.addr);
            return;
        }
    for (int i = 0; i < kHooks; ++i)
    {
        g_hooks[i].orig = runtime.lookupFunction(g_hooks[i].addr);
        runtime.registerFunction(g_hooks[i].addr, hooks[i]);
    }
    std::fprintf(stderr, "[eat] PedGroup log on (level %d)\n", g_level);
}
} // namespace wotm_eat
