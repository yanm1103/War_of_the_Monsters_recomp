// wotm_army.inc: o exercito nas lutas versus (opcao ARMY do painel "recompiled options", wotm_settings.hpp Army*).
//
// Os veiculos sao os da propria fase: tanque (TankPickup, tipo 10, no 0x1392), caminhao de missil (MissileTruckPickup, 9, 0x1391) e
// helicoptero militar (HeliPickup, 12/23, 0x1394/0x139F). Fora de VEGAS os de VEGAS entram junto pelo MON/ARMY.* (gerado por
// wotm_army_files.hpp, carregado como o ultimo arquivo de monstro; secao "carga" abaixo), e a rede da fase troca o tipo de no que
// falta (remapType; buildHeliGrid para o helicoptero sem no nenhum). O createPickup so os cria na aventura (m_gameMode 1): na carga de um
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
// Por tipo: veiculo (Vehicle / HeliVehicle), modo de formacao, estado estacionado
constexpr uint32_t kVehicle[] = {0x1A0, 0x190, 0x190}, kMode[] = {0x2A0, 0x290, 0}, kParked[] = {7, 7, 5};
constexpr uint32_t kFocus = 0x170, kFormation = 0x178, kHeliNav = 0x200;
constexpr uint32_t kEnterState[] = {fn::enterState__10TankPickupQ214MilitaryPickup5State,
                                    fn::enterState__18MissileTruckPickupQ214MilitaryPickup5State,
                                    fn::enterState__10HeliPickupQ210HeliPickup5State};

Hook g_create{fn::createPickup__12LevelPickupsP9_hierheadR8_fvectorPA3_fb11ePickupType}, g_before{fn::InitBeforeDbLoad__7TheGame},
    g_initPlayers{fn::InitPlayers__5Shell}, g_update{fn::Update__7TheGame}, g_reset{fn::ResetLevel__7TheGame},
    g_closest{fn::getClosestPlayer__7TheGameR8_fvectorfRf}, g_changeState{fn::ChangeState__12DestructibleibT2},
    g_kill{fn::killPickup__12LevelPickupsGQ2t10LinkedList1ZP6Pickup8Iteratori}, g_peds{fn::pedsTurnOff__FP8_fvectorb},
    g_takeHit{fn::takeHit__7MonsterP8_fvectorfi}, g_death{fn::transitionInto__10StateDeath},
    g_netNode{fn::getClosestNode__7PathNetR8_fvectori}, g_netPath{fn::getClosestPath__7PathNetR8_fvectorRii};

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
// Rede da fase por veiculo (no InitPlayers): tipo de no em que ele nasce e anda (o pedido pelo navegador, ou o substituto quando a
// fase nao tem esse tipo); kNoNet = sem no utilizavel (o tanque e o caminhao nao saem; para o helicoptero, a primeira leva acrescenta
// uma grade de nos em volta dos monstros, buildHeliGrid)
constexpr int kNoNet = -1;
int g_netType[3] = {3, 3, 1};
bool g_gridBuilt = false;   // grade de nos do helicoptero ja acrescentada a rede nesta carga
double g_armyHitAt[kMaxSlots];   // instante do ultimo golpe do exercito em cada slot (-1 = o ultimo foi de outro)
int g_aimed[kMaxSlots];          // quadros em que cada slot foi o alvo escolhido desde a ultima leva (log)
int g_hits = 0;                  // golpes do exercito em monstros desde a ultima leva (log)
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

// ------------------------------------------------------------------ carga do MON/ARMY.* (os veiculos de VEGAS em qualquer fase)
// Gerado por wotm_army_files.hpp e servido pelo registro de mods. Entra como o ultimo arquivo das tres listas do fileStatus, no mesmo
// indice k: o NGP na entrada do Shell::LoadResTexture (depois dos NGP dos monstros, antes do .RTX da fase, que carrega em
// getNextNgpLoadAddr), o .RTX na entrada do texmResInit e o .TEX na do texmInit (os dois tambem rodam nas telas do menu: so com a
// carga marcada). O dbsRelocateViaPtrListFile le o MON/ARMY.PTR e reloca como o de um monstro, e o dbInitDb desce as raizes ate o
// createPickup (o createHook acima finge o modo 1).
constexpr uint32_t kFs = sym::fileStatus, kFsTexN = kFs + 0x38, kFsNgp = kFs + 0x3C, kFsNgpN = kFs + 0x74, kFsRes = kFs + 0x78,
                   kFsResN = kFs + 0xB0, kFsNames = kFs + 0xB1;
Hook g_loadRes{fn::LoadResTexture__5Shell}, g_resInit{fn::texmResInit__F10_vramAddrsP6QwData}, g_texInit{fn::texmInit__F10_vramAddrs};
int g_armyFile = -1;   // indice k do ARMY na carga em andamento (-1: nao entrou)

bool readArmy(const char *name, std::vector<uint8_t> &out)
{
    const std::string p = wotm::mods::Registry::get().hostPath(std::string("MON/") + name);
    std::ifstream f(p, std::ios::binary);
    if (p.empty() || !f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !out.empty();
}
// Copia o arquivo para o proximo endereco de carga da lista e chama o fileAdd*File do jogo
bool appendArmy(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, const char *name, uint32_t table, uint32_t countAddr, uint32_t addFn)
{
    std::vector<uint8_t> d;
    if (!readArmy(name, d)) return false;
    const uint32_t k = rd8(rdram, countAddr), dst = phys(rd32(rdram, table + 4 * k));
    const uint32_t ceiling = dst >= PS2_GUEST_RAM_SIZE ? uint32_t(PS2_RAM_SIZE) - 0x100000u : 0x1F10000u;
    if (k > 12 || dst < 0xA00000u || dst + d.size() > ceiling)
    {
        std::fprintf(stderr, "[exercito] %s fora: sem espaco (arquivo %u em %#x, %zu bytes)\n", name, k, dst, d.size());
        return false;
    }
    std::memcpy(rdram + dst, d.data(), d.size());
    call(rdram, ctx, runtime, addFn, {dst, uint32_t(d.size())});
    return true;
}
bool armyLoadWanted(uint8_t *rdram)
{
    char lvl[9] = {};
    for (int c = 0; c < 8; ++c) lvl[c] = char(std::toupper(rd8(rdram, kFsNames + c)));
    return poolWanted(rd32(rdram, kGameMode)) && std::strcmp(lvl, "VEGAS") != 0 && lvl[0];
}
void loadResHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == g_loadRes.addr)
    {
        g_armyFile = -1;
        const uint32_t k = rd8(rdram, kFsNgpN);
        if (armyLoadWanted(rdram) &&
            appendArmy(rdram, ctx, runtime, "ARMY.NGP", kFsNgp, kFsNgpN, fn::fileAddNgpFile__FPci))
        {
            for (int c = 0; c < 9; ++c) wr8(rdram, kFsNames + 9 * k + c, uint8_t("ARMY"[std::min(c, 4)]));
            g_armyFile = int(k);
            std::fprintf(stderr, "[exercito] MON/ARMY.NGP carregado como arquivo %u em %#x\n", k, phys(rd32(rdram, kFsNgp + 4 * k)));
        }
    }
    g_loadRes.orig(rdram, ctx, runtime);
}
// Volta o NGP anexado (o .RTX ou o .TEX nao entrou no mesmo indice): sem ele o jogo carrega como sempre
void dropArmy(uint8_t *rdram, const char *why)
{
    const uint32_t k = uint32_t(g_armyFile);
    wr8(rdram, kFsNgpN, uint8_t(k));
    for (int c = 0; c < 9; ++c) wr8(rdram, kFsNames + 9 * k + c, 0);
    std::fprintf(stderr, "[exercito] MON/ARMY fora desta carga: %s\n", why);
    g_armyFile = -1;
}
void resInitHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == g_resInit.addr && g_armyFile >= 0)
    {
        if (rd8(rdram, kFsResN) != uint32_t(g_armyFile))
            dropArmy(rdram, "indice do .RTX diferente do NGP");
        else if (!appendArmy(rdram, ctx, runtime, "ARMY.RTX", kFsRes, kFsResN, fn::fileAddResFile__FPci))
            dropArmy(rdram, "MON/ARMY.RTX nao entrou");
    }
    g_resInit.orig(rdram, ctx, runtime);
}
void texInitHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == g_texInit.addr && g_armyFile >= 0)
    {
        if (rd8(rdram, kFsTexN) != uint32_t(g_armyFile) || rd8(rdram, kFsResN) != uint32_t(g_armyFile) + 1u)
            dropArmy(rdram, "indice do .TEX diferente do NGP");   // o .RTX ja subiu sem uso: so ocupa a VRAM
        else if (!appendArmy(rdram, ctx, runtime, "ARMY.TEX", kFs, kFsTexN, fn::fileAddTexFile__FPci))
            dropArmy(rdram, "MON/ARMY.TEX nao entrou");
        g_armyFile = -1;
    }
    g_texInit.orig(rdram, ctx, runtime);
}

void resetFight()
{
    g_time = g_sinceWave = g_retryAt = 0.0;
    g_chaos = g_waves = 0;
    g_live = false;
    for (double &t : g_armyHitAt) t = -1.0;
    std::fill(std::begin(g_aimed), std::end(g_aimed), 0);
    g_hits = 0;
}

void beforeHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == g_before.addr)
    {
        g_poolN = g_created = 0;
        g_ready = g_repark = g_on = g_gridBuilt = false;
        g_netType[KTank] = g_netType[KTruck] = 3;
        g_netType[KHeli] = 1;
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
        // Rede: o tanque e o caminhao andam nos nos do tipo 3 (so o VEGAS.FOP os tem) ou, sem eles, nos das ruas dos carros (0); o
        // helicoptero nos do tipo 1, ou nos do 2 (os do disco voador). Fase sem .FOP: a reserva de 2 nos do exportPoints, sem tipo.
        int types[256] = {};
        const uint32_t nn = rd16(rdram, kFodder);
        for (uint32_t i = 0; i < nn && i < 256; ++i)
            ++types[rd8(rdram, kFodder + 0x10 + kFodderNode * i + 0x10)];
        const bool net = nn > 2;
        if (!net)   // a reserva nao escreve o tipo: sobra o da fase anterior
            for (uint32_t i = 0; i < 2; ++i) wr8(rdram, kFodder + 0x10 + kFodderNode * i + 0x10, 0u);
        g_netType[KTank] = g_netType[KTruck] = !net ? kNoNet : types[3] ? 3 : types[0] ? 0 : kNoNet;
        g_netType[KHeli] = !net ? kNoNet : types[1] ? 1 : types[2] ? 2 : kNoNet;
        std::fprintf(stderr, "[exercito] pool da fase %u: %d tanques, %d caminhoes, %d helicopteros (criados %d); rede de %u nos: tanque no tipo %d, "
                             "helicoptero no tipo %d (-1: sem rede)\n",
                     rd32(rdram, kGame + 0x1203D0), count[KTank], count[KTruck], count[KHeli], g_created, nn, g_netType[KTank], g_netType[KHeli]);
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

// Fase sem no de helicoptero (nem os do 1 nem os do disco voador, 2): o HeliVehicleNav so voa de no em no pelas ligacoes da rede, e
// so atira pairando a menos de 450 do alvo no plano e 150 na altura (HeliPickup, estado 2). Acrescenta a fodder uma grade de nos do
// tipo 1 de kN x kN, passo kStep, centrada nos monstros, cada no kAlt acima do chao mais alto entre o dele e o dos 8 vizinhos (hdHatTest
// de cima para baixo; nunca abaixo do monstro mais baixo), para o voo reto entre nos nao bater na encosta; um ponto sem chao (fora do
// mapa) fica de fora. Os nos sobre parede, rocha ou predio ficam tambem, mais altos: o seek (getSeekPath) escolhe a ligacao mais
// alinhada com o alvo e para quando ela volta ao no de onde veio, entao um buraco na grade prende o helicoptero. Cada no liga aos
// vizinhos da grade com {id, kWidth, kDrop}: o followPath da o trecho por chegado a kWidth*10 do fim e desvia ate kWidth*10-50 da
// linha, e o elevateTo desce ate kDrop*10 abaixo dela atras do alvo (as ligacoes do helicoptero em VEGAS.FOP sao {id, 10..20, 26}; as
// {id, 250, 250} da reserva do exportPoints dao cada trecho por chegado na hora e o helicoptero nao sai do lugar). Os nos da fase
// ficam como estao.
void buildHeliGrid(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    constexpr int kN = 9;
    constexpr uint8_t kWidth = 10, kDrop = 40;
    constexpr float kStep = 400.f, kAlt = 150.f, kProbe = 3000.f, kHigh = 400.f;   // kProbe: acima dos predios mais altos
    g_gridBuilt = true;
    const uint32_t nn = rd16(rdram, kFodder), base = nn > 2 ? nn : 2;
    float cx = 0.f, cy = 0.f, low = 1e30f;
    int k = 0;
    for (uint32_t i = 0, n = std::min<uint32_t>(rd32(rdram, kNumSlots), kMaxSlots); i < n; ++i)
    {
        const uint32_t m = phys(kSlots + kSlotStride * i);
        if (!monsterAlive(rdram, m)) continue;
        const uint32_t cs = phys(rd32(rdram, m + 0xC));
        cx += rdF(rdram, cs + kCsTrans);
        cy += rdF(rdram, cs + kCsTrans + 4);
        low = std::min(low, rdF(rdram, cs + kCsTrans + 8));
        ++k;
    }
    if (!k) return;
    cx /= float(k);
    cy /= float(k);
    int idx[kN][kN], none = 0, high = 0;
    bool has[kN][kN];
    float ground[kN][kN];
    auto at = [&](int g) { return kStep * float(g - kN / 2); };
    for (int gy = 0; gy < kN; ++gy)
        for (int gx = 0; gx < kN; ++gx)
        {
            wrF(rdram, kVec, cx + at(gx));
            wrF(rdram, kVec + 4, cy + at(gy));
            wrF(rdram, kVec + 8, low + kProbe);
            wrF(rdram, kVec + 12, 1.f);
            const float d = wotm_coop::hatTest(rdram, ctx, runtime, kVec, kVec + 0x10);
            has[gy][gx] = d >= 0.f && d < 4096.f;   // sem chao: fora do mapa
            ground[gy][gx] = std::max(low + kProbe - d, low);
            none += !has[gy][gx];
            high += has[gy][gx] && ground[gy][gx] > low + kHigh;
        }
    uint32_t next = base;
    float zMin = 1e30f, zMax = -1e30f;
    for (int gy = 0; gy < kN; ++gy)
        for (int gx = 0; gx < kN; ++gx)
        {
            idx[gy][gx] = -1;
            if (next >= 256 || !has[gy][gx]) continue;
            // a altura cobre o chao dos vizinhos: a ligacao reta entre dois nos nao entra na encosta
            float top = ground[gy][gx];
            for (int ny = std::max(gy - 1, 0); ny <= std::min(gy + 1, kN - 1); ++ny)
                for (int nx = std::max(gx - 1, 0); nx <= std::min(gx + 1, kN - 1); ++nx)
                    if (has[ny][nx]) top = std::max(top, ground[ny][nx]);
            const float z = top + kAlt;
            const uint32_t nd = kFodder + 0x10 + kFodderNode * next;
            for (uint32_t b = 0; b < kFodderNode; b += 4) wr32(rdram, nd + b, 0u);
            wrF(rdram, nd, cx + at(gx));
            wrF(rdram, nd + 4, cy + at(gy));
            zMin = std::min(zMin, z);
            zMax = std::max(zMax, z);
            wrF(rdram, nd + 8, z);
            wrF(rdram, nd + 12, 1.f);
            wr8(rdram, nd + 0x10, 1u);
            idx[gy][gx] = int(next++);
        }
    auto link = [&](int from, int to) {
        if (from < 0 || to < 0) return;
        const uint32_t nd = kFodder + 0x10 + kFodderNode * uint32_t(from), np = rd8(rdram, nd + 0x11);
        if (np >= 6) return;
        wr16(rdram, nd + 0x12 + 4 * np, uint16_t(to));
        wr8(rdram, nd + 0x14 + 4 * np, kWidth);
        wr8(rdram, nd + 0x15 + 4 * np, kDrop);
        wr8(rdram, nd + 0x11, uint8_t(np + 1));
    };
    for (int gy = 0; gy < kN; ++gy)
        for (int gx = 0; gx < kN; ++gx)
        {
            if (idx[gy][gx] < 0) continue;
            if (gx > 0) link(idx[gy][gx], idx[gy][gx - 1]);
            if (gx + 1 < kN) link(idx[gy][gx], idx[gy][gx + 1]);
            if (gy > 0) link(idx[gy][gx], idx[gy - 1][gx]);
            if (gy + 1 < kN) link(idx[gy][gx], idx[gy + 1][gx]);
        }
    if (next - base < 2)
    {
        std::fprintf(stderr, "[exercito] fase sem no de helicoptero e sem chao livre em volta dos monstros: helicopteros nao saem\n");
        return;
    }
    wr16(rdram, kFodder, uint16_t(next));
    g_netType[KHeli] = 1;
    std::fprintf(stderr, "[exercito] fase sem no de helicoptero: grade de %u nos (%u a %u; %d sobre obstaculo, %d pontos sem chao) em volta de (%.0f, %.0f), altura %.0f "
                         "a %.0f (monstro mais baixo em %.0f)\n", next - base, base, next - 1, high, none, cx, cy, zMin, zMax, low);
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
        if (u.kind == KHeli && g_netType[KHeli] == kNoNet && !g_gridBuilt)
            buildHeliGrid(rdram, ctx, runtime);
        if (g_netType[u.kind] == kNoNet) continue;   // a fase nao tem rede para esse veiculo
        const int node = pickNode(rdram, uint32_t(g_netType[u.kind]), used, nUsed);
        if (node < 0) continue;
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
                std::fprintf(stderr, "[exercito] leva %d aos %.0f s (%s; caos %d): %d veiculos; desde a anterior, %d golpes do exercito e alvos:%s\n",
                             g_waves + 1, g_time, byTime && byChaos ? "tempo e caos" : byTime ? "tempo" : "caos", g_chaos, n, g_hits,
                             *aimed ? aimed : " nenhum");
                std::fill(std::begin(g_aimed), std::end(g_aimed), 0);
                g_hits = 0;
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
                g_armyHitAt[slot] = g_time, ++g_hits;
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

// ------------------------------------------------------------------ rede: o tipo de no que o navegador pede vira o da fase
// O VehicleNavigator::init pede o tipo 3 para o tanque e o caminhao (getClosestPath, a3) e o HeliVehicleNav::init o 1
// (getClosestNode, a2); fora da fodder, ou com o tipo pedido presente, nada muda.
int remapType(uint8_t *rdram, uint32_t self, int32_t type)
{
    if (!g_ready || phys(self) != phys(kFodder)) return type;
    const int to = type == 3 ? g_netType[KTank] : type == 1 ? g_netType[KHeli] : type;
    return to == kNoNet ? type : to;
}
void netNodeHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == g_netNode.addr)
        ctx->r[6] = _mm_set_epi64x(0, remapType(rdram, GPR_U32(ctx, 4), int32_t(GPR_U32(ctx, 6))));
    g_netNode.orig(rdram, ctx, runtime);
}
void netPathHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == g_netPath.addr)
        ctx->r[7] = _mm_set_epi64x(0, remapType(rdram, GPR_U32(ctx, 4), int32_t(GPR_U32(ctx, 7))));
    g_netPath.orig(rdram, ctx, runtime);
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
        {&g_death, &deathHook, "StateDeath::transitionInto"}, {&g_netNode, &netNodeHook, "PathNet::getClosestNode"},
        {&g_netPath, &netPathHook, "PathNet::getClosestPath"}, {&g_loadRes, &loadResHook, "Shell::LoadResTexture"},
        {&g_resInit, &resInitHook, "texmResInit"},     {&g_texInit, &texInitHook, "texmInit"},
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
