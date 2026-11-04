// wotm_army.inc: o exercito nas lutas versus (opcao ARMY do painel "recompiled options", wotm_settings.hpp Army*).
//
// Os veiculos sao os da propria fase: tanque (TankPickup, tipo 10, no 0x1392), caminhao de missil (MissileTruckPickup, 9, 0x1391) e
// helicoptero militar (HeliPickup, 12/23, 0x1394/0x139F). O createPickup so os cria na aventura (m_gameMode 1): na carga de um
// versus com o exercito ligado, o gancho finge o modo 1 so para esses ids. No InitPlayers (pool pronto) cada um fica regeneravel com
// o contador no maximo (o kill guarda o cs e manda o no para a regenList, e o jogo nunca o traz de volta sozinho) e o turnOffMilitary
// os estaciona escondidos. Na luta, o TheGame::Update conta o tempo de luta valendo e o caos (ganchos de entrada no
// Destructible::ChangeState, LevelPickups::killPickup e pedsTurnOff); cada leva acorda veiculos estacionados ou mortos (regen +
// posicao num no da PathNet::fodder do tipo do veiculo + enterState(0)).
// Alvo: o getClosestPlayer (so os humanos) vira o monstro vivo mais proximo entre todos os slots. KO pelo exercito: o
// StateDeath::transitionInto tira 1 do placar da vitima quando nao ha assassino; o gancho devolve esse ponto (decisao do dono: KO
// sem dono, sem ponto perdido).
// Contexto e medidas: docs/investigacoes/2026-10-25/militares_spawn.md e docs/investigacoes/2026-11-04/militares_*.md.
namespace wotm_army
{
using wotm_coop::call;
using wotm_coop::callVirtual;
using wotm_coop::Hook;
using wotm_coop::inList;
using wotm_coop::kGame;
using wotm_coop::kGameMode;
using wotm_coop::kNumSlots;
using wotm_coop::kShell;
using wotm_coop::kSlots;
using wotm_coop::kSlotStride;
using wotm_coop::rd32;
using wotm_coop::rd8;
using wotm_coop::rdF;
using wotm_coop::snapList;
using wotm_coop::wr32;
using wotm_coop::wrF;
namespace fn = wotm::fn;
namespace sym = wotm::sym;
namespace st = wotm::settings;

constexpr uint32_t kVec = PS2_RAM_SIZE - 0x1440;   // _fvector da posicao (+0) e normal do hdHatTest (+0x10), abaixo da memoria dos mechas
constexpr uint32_t kActive = sym::x_12LevelPickups_s_activeList, kRegenList = sym::x_12LevelPickups_s_regenList;
constexpr uint32_t kFodder = sym::x_7PathNet_fodder, kFodderNode = 0x30;
constexpr uint32_t kLoaded = kGame + 0x120468;   // 1 depois do InitAfterDbLoad (o createPickup da luta vem do Debris)
constexpr uint32_t kPlayerMask = kGame + 0x120444, kFadeA = kGame + 0x120454, kFadeB = kGame + 0x120458;
constexpr uint32_t kInCinema = kShell + 0x2BA4, kInSession = kShell + 0x2BCC;
// Pickup
constexpr uint32_t kPkKind = 0x0, kPkFlags = 0x4, kPkCs = 0xC, kPkHealth = 0x48, kPkMaxHealth = 0x40, kPkRegenTimer = 0x4C, kPkBits = 0x50,
                   kPkType = 0xA0, kPkState = 0x174;
// cs
constexpr uint32_t kCsDrawMe = 0xC, kCsTestCollision = 0xD, kCsTrans = 0x10;
// Monster
constexpr uint32_t kMFlags = 0x4, kMPlayerNum = 0x18, kMMonsterNum = 0x28, kMWins = 0x3C, kMDead = 0xE8, kMCloaked = 0xEF, kMOut = 0xF6;
constexpr uint32_t kDeathOwner = 0xC, kDeathKiller = 0x1C;   // StateDeath

enum Kind { KTank, KTruck, KHeli, KNone };
constexpr const char *kKindName[] = {"tanque", "caminhao", "helicoptero"};
Kind kindOf(uint32_t type) { return type == 10u ? KTank : type == 9u ? KTruck : (type == 12u || type == 23u) ? KHeli : KNone; }
bool militaryId(uint32_t id) { return id == 0x1391u || id == 0x1392u || id == 0x1394u || id == 0x139Fu; }
// Por tipo: veiculo (Vehicle / HeliVehicle), modo de formacao, estado estacionado, no da PathNet que o navegador procura
constexpr uint32_t kVehicle[] = {0x1A0, 0x190, 0x190}, kMode[] = {0x2A0, 0x290, 0}, kParked[] = {7, 7, 5}, kNodeType[] = {3, 3, 1};
constexpr uint32_t kFocus = 0x170, kFormation = 0x178, kHeliNav = 0x200;
constexpr uint32_t kEnterState[] = {fn::enterState__10TankPickupQ214MilitaryPickup5State,
                                    fn::enterState__18MissileTruckPickupQ214MilitaryPickup5State,
                                    fn::enterState__10HeliPickupQ210HeliPickup5State};

Hook g_create{fn::createPickup__12LevelPickupsP9_hierheadR8_fvectorPA3_fb11ePickupType}, g_before{fn::InitBeforeDbLoad__7TheGame},
    g_initPlayers{fn::InitPlayers__5Shell}, g_update{fn::Update__7TheGame}, g_reset{fn::ResetLevel__7TheGame},
    g_closest{fn::getClosestPlayer__7TheGameR8_fvectorfRf}, g_changeState{fn::ChangeState__12DestructibleibT2},
    g_kill{fn::killPickup__12LevelPickupsGQ2t10LinkedList1ZP6Pickup8Iteratori}, g_peds{fn::pedsTurnOff__FP8_fvectorb},
    g_takeHit{fn::takeHit__7MonsterP8_fvectorfi}, g_death{fn::transitionInto__10StateDeath};

constexpr int kMaxPool = 48, kMaxSlots = 16;
struct Unit { uint32_t pk, node; Kind kind; };
Unit g_pool[kMaxPool];
int g_poolN = 0, g_created = 0;
bool g_ready = false;        // pool montado nesta carga (InitPlayers)
bool g_repark = false;       // depois de um ResetLevel: estacionar de novo no primeiro quadro
bool g_live = false;         // a ultima passada do TheGame::Update viu a luta valendo (o caos so conta com ela)
bool g_on = false;           // o exercito vale nesta luta (opcao, modo, treino), da ultima passada do Update
double g_time = 0.0;         // segundos de luta valendo nesta carga
double g_sinceWave = 0.0, g_retryAt = 0.0;
int g_chaos = 0, g_waves = 0;
double g_armyHitAt[kMaxSlots];   // instante do ultimo golpe do exercito em cada slot (-1 = o ultimo foi de outro)
int g_aimed[kMaxSlots];          // quadros em que cada slot foi o alvo escolhido desde a ultima leva (log)
uint32_t g_rng = 0x2545F491u;

uint32_t rnd() { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
uint16_t rd16(uint8_t *rdram, uint32_t a) { return uint16_t(rd8(rdram, a) | (rd8(rdram, a + 1) << 8)); }
void wr8(uint8_t *rdram, uint32_t a, uint8_t v) { FAST_WRITE8(a & 0x1FFFFFFFu, v); }
void wr16(uint8_t *rdram, uint32_t a, uint16_t v) { wr8(rdram, a, uint8_t(v)); wr8(rdram, a + 1, uint8_t(v >> 8)); }
uint32_t phys(uint32_t a) { return a & 0x1FFFFFFFu; }
void setRet(R5900Context *ctx, uint32_t v) { ctx->r[2] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(v))); }

// Modos: 2 e 3 free-for-all, 4 endurance, 6 eliminacao (o treino e o 2 com o wotm_training::g_on). Aventura, minigames e demo, nunca.
bool versusMode(uint32_t mode) { return mode == 2u || mode == 3u || mode == 4u || mode == 6u; }
bool poolWanted(uint32_t mode)
{
    if (!st::on(st::Army)) return false;
    switch (mode)
    {
    case 2: case 3: return st::on(st::ArmyFfa) || st::on(st::ArmyTrain);
    case 4: return st::on(st::ArmyEndur);
    case 6: return st::on(st::ArmyElim);
    default: return false;
    }
}
bool armyOn(uint32_t mode)
{
    if (!st::on(st::Army) || !versusMode(mode)) return false;
    if (wotm_training::g_on.load(std::memory_order_relaxed)) return st::on(st::ArmyTrain);
    return mode == 6u ? st::on(st::ArmyElim) : mode == 4u ? st::on(st::ArmyEndur) : st::on(st::ArmyFfa);
}

int slotOf(uint32_t m)
{
    m = phys(m);
    const uint32_t base = phys(kSlots);
    if (m < base || (m - base) % kSlotStride) return -1;
    const uint32_t i = (m - base) / kSlotStride;
    return i < uint32_t(kMaxSlots) ? int(i) : -1;
}
bool monsterAlive(uint8_t *rdram, uint32_t m)
{
    if (!rd32(rdram, m + kMPlayerNum) || rd8(rdram, m + kMCloaked) || rd8(rdram, m + kMDead) || rd8(rdram, m + kMOut)) return false;
    if (!(rd32(rdram, m + kMFlags) & 2u)) return false;   // m_flags (u16): o StateDeath apaga o bit 1
    const uint32_t cs = phys(rd32(rdram, m + 0xC));
    return cs && rd8(rdram, cs + kCsDrawMe);
}

// ------------------------------------------------------------------ carga
void createHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc != g_create.addr)
    {
        g_create.orig(rdram, ctx, runtime);
        return;
    }
    const uint32_t ep = phys(GPR_U32(ctx, 4)), type = GPR_U32(ctx, 8);   // a0 = no, t0 (5o argumento) = tipo pedido
    const uint32_t id = type ? type + 0x1388u : rd32(rdram, ep) >> 18;
    const uint32_t mode = rd32(rdram, kGameMode);
    if (!militaryId(id) || mode == 1u || rd32(rdram, kLoaded) || !poolWanted(mode))
    {
        g_create.orig(rdram, ctx, runtime);
        return;
    }
    const uint32_t ra = GPR_U32(ctx, 31);
    wr32(rdram, kGameMode, 1u);
    g_create.orig(rdram, ctx, runtime);
    wotmFinishGuestCall(rdram, ctx, runtime, ra);   // o modo 1 vale ate o fim, mesmo com a funcao pausada no meio
    wr32(rdram, kGameMode, mode);
    if (GPR_U32(ctx, 2))
        ++g_created;
}

void resetFight()
{
    g_time = g_sinceWave = g_retryAt = 0.0;
    g_chaos = g_waves = 0;
    g_live = false;
    for (double &t : g_armyHitAt) t = -1.0;
    std::fill(std::begin(g_aimed), std::end(g_aimed), 0);
}

void beforeHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == g_before.addr)
    {
        g_poolN = g_created = 0;
        g_ready = g_repark = g_on = false;
        resetFight();
    }
    g_before.orig(rdram, ctx, runtime);
}

// Pool pronto: cada militar da activeList vira regeneravel (bit 0 de +0x50) com o contador no maximo, e todos estacionam
void initPlayersHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == g_initPlayers.addr && g_created && !g_ready)
    {
        uint32_t nodes[256];
        const int k = snapList(rdram, kActive, nodes, 256);
        int count[3] = {};
        for (int i = 0; i < k && g_poolN < kMaxPool; ++i)
        {
            const uint32_t p = phys(rd32(rdram, nodes[i]));
            const Kind kd = p ? kindOf(rd32(rdram, p + kPkType)) : KNone;
            if (kd == KNone || rd32(rdram, p + kPkKind) != 5u)
                continue;
            wr8(rdram, p + kPkBits, uint8_t(rd8(rdram, p + kPkBits) | 1u));
            wr32(rdram, p + kPkRegenTimer, 0x7FFFFFFFu);
            g_pool[g_poolN++] = Unit{p, nodes[i], kd};
            ++count[kd];
        }
        call(rdram, ctx, runtime, fn::turnOffMilitary__12LevelPickups, {});
        g_ready = true;
        int types[4] = {};
        for (uint32_t i = 0, n = rd16(rdram, kFodder); i < n && i < 256; ++i)
            ++types[std::min<int>(rd8(rdram, kFodder + 0x10 + kFodderNode * i + 0x10), 3)];
        std::fprintf(stderr, "[exercito] pool da fase %u: %d tanques, %d caminhoes, %d helicopteros (criados %d); rede: %d nos de tanque, %d de helicoptero\n",
                     rd32(rdram, kGame + 0x1203D0), count[KTank], count[KTruck], count[KHeli], g_created, types[3], types[1]);
    }
    g_initPlayers.orig(rdram, ctx, runtime);
}

void resetHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == g_reset.addr && g_ready)
    {
        g_repark = true;
        resetFight();
    }
    g_reset.orig(rdram, ctx, runtime);
}

// ------------------------------------------------------------------ levas
// No da PathNet do tipo pedido mais perto dos monstros entre os que ficam a pelo menos kMinDist de todos; sem nenhum assim, o mais
// afastado. Nos a menos de kSpread de um veiculo na rua ou ja acordado nesta leva ficam de fora. -1: nenhum no livre desse tipo (a
// fase nao tem rede para esse veiculo, ou estao todos ocupados: ele fica para a proxima leva).
int pickNode(uint8_t *rdram, uint32_t type, const float (*used)[3], int nUsed)
{
    constexpr float kMinDist = 600.f, kSpread = 250.f;
    float mons[kMaxSlots][3];
    int nm = 0;
    for (uint32_t i = 0, n = std::min<uint32_t>(rd32(rdram, kNumSlots), kMaxSlots); i < n; ++i)
    {
        const uint32_t m = phys(kSlots + kSlotStride * i);
        if (!monsterAlive(rdram, m)) continue;
        const uint32_t cs = phys(rd32(rdram, m + 0xC));
        for (int c = 0; c < 3; ++c) mons[nm][c] = rdF(rdram, cs + kCsTrans + 4u * c);
        ++nm;
    }
    int best = -1, far = -1;
    float bestD = 1e30f, farD = -1.f;
    for (uint32_t i = 0, n = rd16(rdram, kFodder); i < n && i < 256; ++i)
    {
        const uint32_t nd = kFodder + 0x10 + kFodderNode * i;
        if (rd8(rdram, nd + 0x10) != type) continue;
        const float x = rdF(rdram, nd), y = rdF(rdram, nd + 4);
        bool crowded = false;
        for (int u = 0; u < nUsed; ++u)
            crowded |= (x - used[u][0]) * (x - used[u][0]) + (y - used[u][1]) * (y - used[u][1]) < kSpread * kSpread;
        if (crowded) continue;
        float dmin = 1e30f;
        for (int k = 0; k < nm; ++k)
            dmin = std::min(dmin, std::sqrt((x - mons[k][0]) * (x - mons[k][0]) + (y - mons[k][1]) * (y - mons[k][1])));
        if (dmin >= kMinDist && dmin < bestD) { bestD = dmin; best = int(i); }
        if (dmin > farD) { farD = dmin; far = int(i); }
    }
    return best >= 0 ? best : far;
}

bool parked(uint8_t *rdram, const Unit &u)
{
    const uint32_t cs = phys(rd32(rdram, u.pk + kPkCs));
    return cs && rd32(rdram, u.pk + kPkState) == kParked[u.kind] && !rd8(rdram, cs + kCsDrawMe) && inList(rdram, kActive, u.node);
}

// Acorda um veiculo estacionado ou morto (regenList) no no da PathNet
bool wake(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, const Unit &u, uint32_t nodeIdx)
{
    const uint32_t p = u.pk, cs = phys(rd32(rdram, p + kPkCs));
    if (!cs) return false;
    if (inList(rdram, kRegenList, u.node))
    {
        // da regenList para a cabeca da activeList, como o LevelPickups::update (pickupsHook, wotm_coop.inc)
        const uint32_t n = u.node, next = rd32(rdram, n + 4), prev = rd32(rdram, n + 8);
        if (prev) wr32(rdram, phys(prev) + 4, next);
        if (next) wr32(rdram, phys(next) + 8, prev);
        wr32(rdram, n + 8, kActive);
        wr32(rdram, n + 4, rd32(rdram, kActive + 4));
        wr32(rdram, kActive + 4, n);
        if (const uint32_t nn = rd32(rdram, n + 4)) wr32(rdram, phys(nn) + 8, n);
        callVirtual(rdram, ctx, runtime, p, 0x3C);   // regen: vida cheia, drawMe, casa
    }
    else
    {
        call(rdram, ctx, runtime, fn::hierSetCsDrawMe__FP3_csUc, {cs, 1u});
        wrF(rdram, p + kPkHealth, rdF(rdram, p + kPkMaxHealth));
    }
    wr32(rdram, p + kPkRegenTimer, 0x7FFFFFFFu);
    wr8(rdram, cs + kCsTestCollision, 1u);
    wr16(rdram, p + kPkFlags, uint16_t(rd16(rdram, p + kPkFlags) | (u.kind == KHeli ? 2u : 0xAu)));
    const uint32_t nd = kFodder + 0x10 + kFodderNode * nodeIdx;
    float x = rdF(rdram, nd), y = rdF(rdram, nd + 4), z = rdF(rdram, nd + 8);
    wrF(rdram, kVec, x);
    wrF(rdram, kVec + 4, y);
    wrF(rdram, kVec + 8, z + (u.kind == KHeli ? 0.f : 200.f));
    wrF(rdram, kVec + 12, 1.f);
    const uint32_t veh = p + kVehicle[u.kind];
    if (u.kind == KHeli)
    {
        call(rdram, ctx, runtime, fn::setPos__11HeliVehicleR8_fvector, {veh, kVec});
        call(rdram, ctx, runtime, fn::hdReparentCsGrid__FP3_cs, {cs});
        call(rdram, ctx, runtime, fn::init__14HeliVehicleNav, {p + kHeliNav});
    }
    else
    {
        const float d = wotm_coop::hatTest(rdram, ctx, runtime, kVec, kVec + 0x10);   // assenta no chao, como o gameGetStartPoint
        if (d >= 0.f && d < 2000.f) wrF(rdram, kVec + 8, z + 200.f - d);
        else wrF(rdram, kVec + 8, z);
        z = rdF(rdram, kVec + 8);
        wr32(rdram, p + kMode[u.kind], 0u);
        wr32(rdram, p + kFormation, 0u);
        wr32(rdram, p + kFocus, 0u);
        call(rdram, ctx, runtime, fn::setPos__7VehicleR8_fvector, {veh, kVec});
        wr32(rdram, veh + 0x20, 0u);   // velocidade (+0x28 e a maxima)
        const uint32_t vt = phys(rd32(rdram, p + 0x10));
        const int16_t adj = int16_t(rd32(rdram, vt + 0x6C - 4) & 0xFFFFu);
        call(rdram, ctx, runtime, rd32(rdram, vt + 0x6C), {p + uint32_t(int32_t(adj)), kVec});   // setTrans: cs, grade, navegador
    }
    call(rdram, ctx, runtime, kEnterState[u.kind], {p, 0u});
    std::fprintf(stderr, "[exercito] %s acordado no no %u (%.0f, %.0f, %.0f)\n", kKindName[u.kind], nodeIdx, x, y, z);
    return true;
}

int spawnWave(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, int want)
{
    int avail[kMaxPool], na = 0;
    for (int i = 0; i < g_poolN; ++i)
        if (parked(rdram, g_pool[i]) || inList(rdram, kRegenList, g_pool[i].node))
            avail[na++] = i;
    for (int i = na - 1; i > 0; --i) std::swap(avail[i], avail[rnd() % uint32_t(i + 1)]);
    // os que ja estao na rua contam como ocupados (nao acordar um em cima do outro)
    float used[kMaxPool + 16][3];
    int nUsed = 0, woke = 0;
    for (int i = 0; i < g_poolN; ++i)
    {
        const uint32_t cs = phys(rd32(rdram, g_pool[i].pk + kPkCs));
        if (!cs || !rd8(rdram, cs + kCsDrawMe) || !inList(rdram, kActive, g_pool[i].node)) continue;
        for (int c = 0; c < 3; ++c) used[nUsed][c] = rdF(rdram, cs + kCsTrans + 4u * c);
        ++nUsed;
    }
    for (int j = 0; j < na && woke < want; ++j)
    {
        const Unit &u = g_pool[avail[j]];
        const int node = pickNode(rdram, kNodeType[u.kind], used, nUsed);
        if (node < 0) continue;   // a fase nao tem rede para esse veiculo
        const uint32_t nd = kFodder + 0x10 + kFodderNode * uint32_t(node);
        if (nUsed < kMaxPool + 16)
        {
            used[nUsed][0] = rdF(rdram, nd);
            used[nUsed][1] = rdF(rdram, nd + 4);
            used[nUsed][2] = rdF(rdram, nd + 8);
            ++nUsed;
        }
        woke += wake(rdram, ctx, runtime, u, uint32_t(node));
    }
    return woke;
}

// ------------------------------------------------------------------ quadro da luta
bool fightLive(uint8_t *rdram)
{
    const uint32_t rt = sym::gRtReturn;
    return !rd32(rdram, sym::moviePlaying) && !rd32(rdram, kInCinema) && (rd32(rdram, kPlayerMask) & 7u) == 7u && !rd32(rdram, rt) &&
           !rd32(rdram, kFadeA) && !rd32(rdram, kFadeB) && rd32(rdram, kInSession);
}

void updateHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc != g_update.addr || !g_ready || !g_poolN)
    {
        g_on = g_live = false;
        g_update.orig(rdram, ctx, runtime);
        return;
    }
    const uint32_t mode = rd32(rdram, kGameMode);
    g_on = armyOn(mode);
    g_live = g_on && fightLive(rdram);
    if (g_repark)
    {
        call(rdram, ctx, runtime, fn::turnOffMilitary__12LevelPickups, {});
        g_repark = false;
    }
    for (int i = 0; i < g_poolN; ++i)
    {
        const Unit &u = g_pool[i];
        if (inList(rdram, kRegenList, u.node))
            wr32(rdram, u.pk + kPkRegenTimer, 0x7FFFFFFFu);   // o jogo nunca o regenera sozinho
        else if (g_on && rd32(rdram, u.pk + kPkState) == kParked[u.kind] && phys(rd32(rdram, u.pk + kPkCs)) &&
                 rd8(rdram, phys(rd32(rdram, u.pk + kPkCs)) + kCsDrawMe) && inList(rdram, kActive, u.node))
            call(rdram, ctx, runtime, kEnterState[u.kind], {u.pk, 0u});   // pego e solto por um monstro: o grab o deixou estacionado
    }
    if (g_live)
    {
        const int rate = int(call(rdram, ctx, runtime, fn::timerGetUpdateRate__Fv, {}));
        const double dt = 1.0 / double(rate > 0 && rate <= 240 ? rate : 60);
        g_time += dt;
        g_sinceWave += dt;
        const int trig = st::get(st::ArmyTrigger);
        const bool byTime = trig != 1 && g_sinceWave >= st::get(st::ArmyEvery);
        const bool byChaos = trig != 0 && g_chaos >= st::get(st::ArmyChaos);
        if ((byTime || byChaos) && g_time >= g_retryAt)
        {
            const int n = spawnWave(rdram, ctx, runtime, st::get(st::ArmyWave));
            if (n > 0)
            {
                char aimed[128] = "";
                for (int s = 0, len = 0; s < kMaxSlots && len < 110; ++s)
                    if (g_aimed[s]) len += std::snprintf(aimed + len, sizeof aimed - len, " slot%d=%d", s, g_aimed[s]);
                std::fprintf(stderr, "[exercito] leva %d aos %.0f s (%s; caos %d): %d veiculos; alvos desde a anterior:%s\n", g_waves + 1,
                             g_time, byTime && byChaos ? "tempo e caos" : byTime ? "tempo" : "caos", g_chaos, n, *aimed ? aimed : " nenhum");
                std::fill(std::begin(g_aimed), std::end(g_aimed), 0);
                ++g_waves;
                g_sinceWave = 0.0;
                g_chaos = 0;
            }
            else
                g_retryAt = g_time + 3.0;   // o pool inteiro ja esta na rua: a leva sai quando um voltar
        }
    }
    g_update.orig(rdram, ctx, runtime);
}

// ------------------------------------------------------------------ alvo: o monstro vivo mais proximo
void closestHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc != g_closest.addr || !g_on)
    {
        g_closest.orig(rdram, ctx, runtime);
        return;
    }
    const uint32_t pos = phys(GPR_U32(ctx, 5)), out = phys(GPR_U32(ctx, 6));
    const float px = rdF(rdram, pos), py = rdF(rdram, pos + 4), pz = rdF(rdram, pos + 8);
    float best = ctx->f[12] * ctx->f[12];
    uint32_t who = 0;
    for (uint32_t i = 0, n = std::min<uint32_t>(rd32(rdram, kNumSlots), kMaxSlots); i < n; ++i)
    {
        const uint32_t m = phys(kSlots + kSlotStride * i);
        if (!monsterAlive(rdram, m)) continue;
        const uint32_t cs = phys(rd32(rdram, m + 0xC));
        const float dx = rdF(rdram, cs + kCsTrans) - px, dy = rdF(rdram, cs + kCsTrans + 4) - py, dz = rdF(rdram, cs + kCsTrans + 8) - pz;
        const float d = dx * dx + dy * dy + dz * dz;
        if (d < best) { best = d; who = kSlots + kSlotStride * i; }
    }
    if (out) wrF(rdram, out, best);
    if (who) ++g_aimed[slotOf(who)];
    setRet(ctx, who);
    ctx->pc = GPR_U32(ctx, 31);
}

// ------------------------------------------------------------------ caos (so na entrada: a volta de uma funcao retomada pode nao passar aqui)
void changeStateHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == g_changeState.addr && g_live)
    {
        const uint32_t d = phys(GPR_U32(ctx, 4));
        const int32_t want = int32_t(GPR_U32(ctx, 5));
        const uint32_t istate = phys(rd32(rdram, d + 0x28)), hier = phys(rd32(rdram, d + 0x24));
        const int32_t cur = istate ? int32_t(rd32(rdram, istate + 4)) : 0, pending = int32_t(rd32(rdram, d + 0x90));
        if (hier && want > cur && want > pending)
        {
            const int kids = rd16(rdram, hier + 0xA);
            g_chaos += rd8(rdram, hier + 4) == 1u ? 1 : want >= kids - 1 ? 8 : 3;   // objeto pequeno, predio caido, andar de predio
        }
    }
    g_changeState.orig(rdram, ctx, runtime);
}

void killHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == g_kill.addr && g_live)
    {
        const uint32_t p = phys(rd32(rdram, phys(GPR_U32(ctx, 4))));
        if (p && rd32(rdram, p + kPkCs))
            switch (rd32(rdram, p + kPkType))
            {
            case 2: case 15: case 26: g_chaos += 3; break;   // carro
            case 3: g_chaos += 4; break;                     // caminhao-tanque
            case 11: case 27: g_chaos += 5; break;           // helicoptero civil, metro
            case 9: case 10: case 12: case 23:
                std::fprintf(stderr, "[exercito] %s destruido\n", kKindName[kindOf(rd32(rdram, p + kPkType))]);
                break;
            default: break;
            }
    }
    g_kill.orig(rdram, ctx, runtime);
}

void pedsHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == g_peds.addr && g_live)
        ++g_chaos;
    g_peds.orig(rdram, ctx, runtime);
}

// ------------------------------------------------------------------ KO pelo exercito: sem ponto perdido
void takeHitHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == g_takeHit.addr && g_on)
    {
        const int slot = slotOf(GPR_U32(ctx, 4));
        const uint32_t id = GPR_U32(ctx, 6);
        if (slot >= 0 && id < rd32(rdram, sym::x_12Interactives_s_numInteractives) && id < 0x400u)
        {
            const uint32_t src = phys(rd32(rdram, sym::x_12Interactives_s_dbInteractive + 4u * id));
            const uint32_t kind = src ? rd32(rdram, src + kPkKind) : 0u;
            if (kind == 5u && kindOf(rd32(rdram, src + kPkType)) != KNone)
                g_armyHitAt[slot] = g_time;
            else if (kind == 1u)
                g_armyHitAt[slot] = -1.0;
        }
    }
    g_takeHit.orig(rdram, ctx, runtime);
}

void deathHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == g_death.addr && g_on)
    {
        const uint32_t sd = phys(GPR_U32(ctx, 4)), owner = phys(rd32(rdram, sd + kDeathOwner));
        const int slot = owner ? slotOf(owner) : -1;
        if (slot >= 0 && !rd32(rdram, sd + kDeathKiller) && g_armyHitAt[slot] >= 0.0 && g_time - g_armyHitAt[slot] < 4.0)
        {
            // o transitionInto tira 1 de m_slots[m_monsterNum].m_winsThisGame sem assassino: soma antes
            const uint32_t s = kSlots + kSlotStride * rd32(rdram, owner + kMMonsterNum);
            wr32(rdram, s + kMWins, rd32(rdram, s + kMWins) + 1u);
            g_armyHitAt[slot] = -1.0;
            std::fprintf(stderr, "[exercito] KO pelo exercito no slot %d: sem ponto perdido\n", slot);
        }
    }
    g_death.orig(rdram, ctx, runtime);
}
} // namespace wotm_army

void wotmArmyInstall(PS2Runtime &runtime)
{
    using namespace wotm_army;
    struct H { Hook *h; PS2Runtime::RecompiledFunction fn; const char *name; };
    const H hooks[] = {
        {&g_create, &createHook, "createPickup"},      {&g_before, &beforeHook, "InitBeforeDbLoad"},
        {&g_initPlayers, &initPlayersHook, "InitPlayers"}, {&g_update, &updateHook, "TheGame::Update"},
        {&g_reset, &resetHook, "ResetLevel"},          {&g_closest, &closestHook, "getClosestPlayer"},
        {&g_changeState, &changeStateHook, "Destructible::ChangeState"}, {&g_kill, &killHook, "killPickup"},
        {&g_peds, &pedsHook, "pedsTurnOff"},           {&g_takeHit, &takeHitHook, "Monster::takeHit"},
        {&g_death, &deathHook, "StateDeath::transitionInto"},
    };
    for (const H &h : hooks)
        if (!(h.h->orig = runtime.lookupFunction(h.h->addr)))
        {
            std::fprintf(stderr, "[exercito] desligado: %s sem funcao no runtime\n", h.name);
            return;
        }
    for (const H &h : hooks)
        runtime.registerFunction(h.h->addr, h.fn);
    std::fprintf(stderr, "[exercito] %s (WOTM_ARMY; aba ARMY do painel)\n", st::on(st::Army) ? "ligado" : "desligado");
}
