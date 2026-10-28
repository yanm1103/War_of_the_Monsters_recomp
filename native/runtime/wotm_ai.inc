// IA (wotm_overrides.cpp o inclui): log das acoes (WOTM_AILOG=1) e os oponentes do jogador sob a IA na aventura (WOTM_AIVSAI, abaixo).
//   - "[ai] <field> m<slot> <Classe>": acao (AiActionTuple) que comeca, com a classe tirada da vtable (o m_name e vazio em todas).
//   - "... <Classe> durou N fields": acao que termina depois de mais de 2 s.
//   - "... counter acertou|falhou": fim de cada AiCounterReflex, pela vida do alvo no comeco e no fim.
//   - "... oponentes: m<slot>... trava N": a cada 10 s por IA, a lista de oponentes dela e o *m_gate (0 = qualquer acao comeca).

namespace wotm_ai
{
using wotm_modes4::rd32;
using wotm_split4::rdF;
namespace fn = wotm::fn;

constexpr uint32_t kSlots = wotm_split4::kGame + 0xB80, kSlotStride = 0x11190, kHealth = 0x44C;
// AiActionTuple
constexpr uint32_t kLastEnter = 0x20, kVptr = 0x44;
// AiCounterReflex
constexpr uint32_t kTarget = 0x48;
// Contador de fields que o timerGetFieldCount devolve (variavel local do timer.c, sem nome no ELF)
constexpr uint32_t kFieldCount = 0x006F8DBCu;

struct Hook
{
    uint32_t addr;
    PS2Runtime::RecompiledFunction orig;
};
Hook g_execute{fn::execute__13AiActionTupleR2Ai}, g_deactivate{fn::deactivate__13AiActionTupleR2Ai},
    g_counterEnter{fn::enterAction__15AiCounterReflexR2Ai}, g_counterExit{fn::exitAction__15AiCounterReflexR2Ai};

int field(uint8_t *rdram) { return static_cast<int32_t>(rd32(rdram, kFieldCount)); }

int slotOf(uint8_t *rdram, uint32_t ai)
{
    const uint32_t m = rd32(rdram, ai) & 0x1FFFFFFFu;
    return m >= kSlots ? int((m - kSlots) / kSlotStride) : -1;
}

const char *nameOf(uint8_t *rdram, uint32_t tuple)
{
    const wotm::symtab::Entry *e = wotm::symtab::find(rd32(rdram, tuple + kVptr));
    const char *name = e ? e->name : "?";
    if (std::strncmp(name, "_vt$", 4) == 0)   // _vt$<n><Classe>
        for (name += 4; *name >= '0' && *name <= '9';)
            ++name;
    return name;
}

void executeHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc != g_execute.addr)
    {
        g_execute.orig(rdram, ctx, runtime);
        return;
    }
    const uint32_t tuple = GPR_U32(ctx, 4) & 0x1FFFFFFFu, ai = GPR_U32(ctx, 5) & 0x1FFFFFFFu;
    const bool idle = static_cast<int32_t>(rd32(rdram, tuple + kLastEnter)) < 0;
    g_execute.orig(rdram, ctx, runtime);
    if (idle && static_cast<int32_t>(rd32(rdram, tuple + kLastEnter)) >= 0)
        std::fprintf(stderr, "[ai] %d m%d %s\n", field(rdram), slotOf(rdram, ai), nameOf(rdram, tuple));
}

void deactivateHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == g_deactivate.addr)
    {
        const uint32_t tuple = GPR_U32(ctx, 4) & 0x1FFFFFFFu, ai = GPR_U32(ctx, 5) & 0x1FFFFFFFu;
        const int32_t since = static_cast<int32_t>(rd32(rdram, tuple + kLastEnter));
        if (since >= 0 && field(rdram) - since > 120)
            std::fprintf(stderr, "[ai] %d m%d %s durou %d fields\n", field(rdram), slotOf(rdram, ai), nameOf(rdram, tuple), field(rdram) - since);
    }
    g_deactivate.orig(rdram, ctx, runtime);
}

// A cada 600 fields por IA: oponentes (Ai::m_opponents, slots) e a trava (*m_gate; 0 = todas as acoes podem comecar)
Hook g_brain{fn::update__7AiBrainR2Ai};
void brainHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == g_brain.addr)
    {
        const uint32_t ai = GPR_U32(ctx, 5) & 0x1FFFFFFFu;
        const int s = slotOf(rdram, ai), now = field(rdram);
        static int s_last[16] = {};
        if (s >= 0 && s < 16 && now - s_last[s] >= 600)
        {
            s_last[s] = now;
            char buf[96];
            int k = 0;
            for (uint32_t i = 0; i < 16; ++i)
            {
                const uint32_t o = rd32(rdram, ai + 4 + 4 * i) & 0x1FFFFFFFu;
                if (!o)
                    break;
                k += std::snprintf(buf + k, sizeof buf - k, " m%d", o >= kSlots ? int((o - kSlots) / kSlotStride) : -1);
                if (k >= int(sizeof buf) - 8)
                    break;
            }
            buf[k] = 0;
            const uint32_t gate = rd32(rdram, ai + 0x2E0) & 0x1FFFFFFFu;
            std::fprintf(stderr, "[ai] %d m%d oponentes:%s trava %d\n", now, s, k ? buf : " nenhum",
                         gate ? static_cast<int32_t>(rd32(rdram, gate)) : -1);
        }
    }
    g_brain.orig(rdram, ctx, runtime);
}

// WOTM_AIVSAI na aventura (modo 1). Com o Ai::s_attackOtherAi desligado (aventura), a IA so ataca monstro com m_playerNum 1 (jogador):
// o Ai::updateOpponentList e as acoes (AiSeekMonster::getBestTarget e outras) pulam os de 2, e o soco de um monstro com 2
// (PunchSwipeConfigBase::collisTestCloseRangeAttack) nao acerta outro com 2. Sob o WOTM_AIVSAI os jogadores tambem tem 2: ninguem
// tinha alvo e, com alvo, soco nao dava dano entre jogador e IA da fase (objeto arremessado, sim). Durante o Ai::updateInputs (a IA
// inteira de um monstro no quadro) e o teste do soco:
//   - de um jogador: s_attackOtherAi ligado (as IAs da fase viram alvo); na IA, os outros jogadores (aliados) saem da lista de oponentes;
//   - de uma IA da fase: os jogadores com m_playerNum 1.
constexpr uint32_t kPlayerNum = 0x18, kNumPlayers = wotm_split4::kShell + 0x2BB4, kMode = wotm_split4::kShell + 0x2C10,
                   kMonsters = wotm_split4::kGame + 0x120380, kAttackOtherAi = wotm::sym::x_2Ai_s_attackOtherAi;
Hook g_updateInputs{fn::updateInputs__2Ai}, g_opponents{fn::updateOpponentList__2Ai},
    g_punch{fn::collisTestCloseRangeAttack__20PunchSwipeConfigBaseiP7MonsterR10HitHistoryi};
uint32_t g_allies[4] = {};
int g_numAllies = 0;   // > 0: dentro do updateInputs de um jogador
bool g_inSides = false;

// Chama a original com os lados da aventura vistos pelo monstro self (allies: preenche g_allies quando self e jogador)
void runAsSides(Hook &h, uint32_t self, bool allies, uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t n = std::min<uint32_t>(rd32(rdram, kNumPlayers), 4u);
    if (ctx->pc != h.addr || rd32(rdram, kMode) != 1u || !n || g_inSides)
    {
        h.orig(rdram, ctx, runtime);
        return;
    }
    uint32_t mon[4] = {}, num[4] = {};
    bool selfPlayer = false;
    for (uint32_t i = 0; i < n; ++i)
        selfPlayer |= (mon[i] = rd32(rdram, kMonsters + 4 * i) & 0x1FFFFFFFu) == self;
    g_inSides = true;
    if (selfPlayer)
    {
        const uint32_t saved = rd32(rdram, kAttackOtherAi);
        if (allies)
        {
            std::copy(mon, mon + 4, g_allies);
            g_numAllies = int(n);
        }
        FAST_WRITE32(kAttackOtherAi, 1u);
        h.orig(rdram, ctx, runtime);
        FAST_WRITE32(kAttackOtherAi, saved);
        g_numAllies = 0;
    }
    else
    {
        for (uint32_t i = 0; i < n; ++i)
            if (mon[i] >= kSlots && (num[i] = rd32(rdram, mon[i] + kPlayerNum)) == 2u)
                FAST_WRITE32(mon[i] + kPlayerNum, 1u);
        h.orig(rdram, ctx, runtime);
        for (uint32_t i = 0; i < n; ++i)
            if (num[i] == 2u)
                FAST_WRITE32(mon[i] + kPlayerNum, 2u);
    }
    g_inSides = false;
}

void updateInputsHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    runAsSides(g_updateInputs, rd32(rdram, GPR_U32(ctx, 4) & 0x1FFFFFFFu) & 0x1FFFFFFFu, true, rdram, ctx, runtime);
}

void punchHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    runAsSides(g_punch, GPR_U32(ctx, 6) & 0x1FFFFFFFu, false, rdram, ctx, runtime);
}

// Lista de oponentes de um jogador (dentro do updateInputsHook): sem os aliados
void opponentsHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t a0 = GPR_U32(ctx, 4), ai = a0 & 0x1FFFFFFFu, ra = GPR_U32(ctx, 31);
    const bool on = ctx->pc == g_opponents.addr && g_numAllies > 0;
    g_opponents.orig(rdram, ctx, runtime);
    if (!on || ctx->pc != ra)
        return;
    uint32_t k = 0;
    for (uint32_t i = 0; i < 16; ++i)
    {
        const uint32_t o = rd32(rdram, ai + 4 + 4 * i);
        if (!o)
            break;
        if (std::find(g_allies, g_allies + g_numAllies, o & 0x1FFFFFFFu) == g_allies + g_numAllies)
            FAST_WRITE32(ai + 4 + 4 * k++, o);
    }
    FAST_WRITE32(ai + 4 + 4 * k, 0u);
    ctx->r[2] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(a0 + 4 + 4 * k)));   // fim da lista
}

// Alvo e vida dele no comeco de cada counter, por tuple (cada Ai tem o seu)
struct Counter
{
    uint32_t tuple = 0, target = 0;
    float health = 0.f;
};
Counter g_counters[16];
Counter &counterOf(uint32_t tuple)
{
    for (Counter &c : g_counters)
        if (c.tuple == tuple || !c.tuple)
        {
            c.tuple = tuple;
            return c;
        }
    static int s_next = 0;
    Counter &c = g_counters[s_next++ & 15];
    c = Counter{tuple};
    return c;
}

void counterEnterHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == g_counterEnter.addr)
    {
        Counter &c = counterOf(GPR_U32(ctx, 4) & 0x1FFFFFFFu);
        c.target = rd32(rdram, c.tuple + kTarget) & 0x1FFFFFFFu;
        c.health = c.target >= kSlots ? rdF(rdram, c.target + kHealth) : 0.f;
    }
    g_counterEnter.orig(rdram, ctx, runtime);
}

void counterExitHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == g_counterExit.addr)
    {
        const Counter &c = counterOf(GPR_U32(ctx, 4) & 0x1FFFFFFFu);
        const bool hit = c.target >= kSlots && rdF(rdram, c.target + kHealth) < c.health;
        std::fprintf(stderr, "[ai] %d m%d counter %s\n", field(rdram), slotOf(rdram, GPR_U32(ctx, 5) & 0x1FFFFFFFu), hit ? "acertou" : "falhou");
    }
    g_counterExit.orig(rdram, ctx, runtime);
}

void install(PS2Runtime &runtime)
{
    if (const char *e = std::getenv("WOTM_AIVSAI"); e && *e && *e != '0')
    {
        if ((g_opponents.orig = runtime.lookupFunction(g_opponents.addr)) && (g_updateInputs.orig = runtime.lookupFunction(g_updateInputs.addr)))
        {
            runtime.registerFunction(g_opponents.addr, &opponentsHook);
            runtime.registerFunction(g_updateInputs.addr, &updateInputsHook);
        }
        if ((g_punch.orig = runtime.lookupFunction(g_punch.addr)))
            runtime.registerFunction(g_punch.addr, &punchHook);
    }
    if (const char *e = std::getenv("WOTM_AILOG"); !e || !*e || *e == '0')
        return;
    struct H { Hook *h; PS2Runtime::RecompiledFunction fn; };
    const H hooks[] = {
        {&g_execute, &executeHook}, {&g_deactivate, &deactivateHook}, {&g_brain, &brainHook}, {&g_counterEnter, &counterEnterHook}, {&g_counterExit, &counterExitHook},
    };
    for (const H &h : hooks)
        if (!(h.h->orig = runtime.lookupFunction(h.h->addr)))
        {
            std::fprintf(stderr, "[ai] desligado: funcao %x sem funcao no runtime\n", h.h->addr);
            return;
        }
    for (const H &h : hooks)
        runtime.registerFunction(h.h->addr, h.fn);
    std::fprintf(stderr, "[ai] log das acoes da IA\n");
}
} // namespace wotm_ai
