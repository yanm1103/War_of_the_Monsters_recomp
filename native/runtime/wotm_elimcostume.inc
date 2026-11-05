// Trajes, skins, customs e monstros repetidos na eliminacao (modo 6; MODS.md, "Skins"). Incluido pelo wotm_overrides.cpp depois do
// wotm_custom.inc.
//
// O retail, no modo 6, carrega os 10 monstros jogaveis de uma vez e sempre no traje 0 (Shell::LoadMonstersDB, LoadResTexture e
// LoadTexture), e quem morre escolhe outro dos livres (dialogo 8). Cada carga e uma instancia (um Monster, tipo e dupId); aqui cada
// jogador da largada tem a sua:
//   - Na entrada do Shell::LoadLevelFiles os jogadores sao lidos em ordem (m_monsterSel; traje em m_costume[0..1], 3 e 4 pelo
//     wotmMultiCostume). O primeiro de cada tipo fica com a carga do retail (dup 0) no traje dele; cada repetido vira uma carga a
//     mais pelo caminho das IAs (m_monsterSel[4 + j], m_costume[2 + j], m_numAIs; o mesmo arquivo), como os jogadores 3 e 4 do
//     wotm_split4.inc, e volta na entrada do InitPlayers. A contagem de duplicatas do InitPlayers (jogadores na ordem, antes das
//     IAs) da a cada repetido o dup da carga dele. O fileStatus tem 14 vagas (a fase e 12 monstros): no maximo kMaxExtra repetidos
//     (a selecao nao deixa os 4 no mesmo monstro). O monstro de cada um fica marcado como usado (vaga do traje 0, taken 0), como o
//     screenCharSelect2P faz na confirmacao: a selecao com repetidos a deixa livre para o outro jogador.
//   - Shell::formatFilename1 chamado de dentro dos tres carregadores troca o traje 0 da carga do retail pelo do primeiro jogador
//     daquele tipo (o nome do arquivo e o do mod: skin ou custom com "files" sao trajes 4..99), e o fileAddName do LoadMonstersDB
//     registra o nome com o mesmo traje ("MONKEY4"): o wotm_custom.inc acha o custom de um Monster pelo numero do fim desse nome.
//   - TheGame::SetPlayerMonster recebe o traje da instancia (tipo, dup). Fora do InitPlayers (dialogo 8, "play again") o retail
//     manda dup 0: vale a primeira instancia do tipo que nenhum outro jogador esta usando. Registrado por ultimo: as skins
//     (wotm_skins.inc) ainda trocam a skin pelo traje base do som.
// Sem mods e sem repetidos nada muda: so cargas do retail, todas no traje 0.

namespace wotm_elimcostume
{
constexpr uint32_t kShell = wotm::sym::shellInfo, kGame = wotm::sym::shellGame;
constexpr uint32_t kMode = kShell + 0x2C10, kNumPlayers = kShell + 0x2BB4, kNumAIs = kShell + 0x2BB8, kMonsterSel = kShell + 0x2938,
                   kCostume = kShell + 0x2BE0;
constexpr uint32_t kMonsters = kGame + 0x120380, kTypeBits = 0x14, kDupId = 0x1C;   // TheGame::m_monsters[], Monster::m_typeBits/m_dupId
constexpr uint32_t kLoadLevelFiles = wotm::fn::LoadLevelFiles__5Shell, kInitPlayers = wotm::fn::InitPlayers__5Shell;
constexpr uint32_t kLoadMonsters = wotm::fn::LoadMonstersDB__5Shell, kLoadRes = wotm::fn::LoadResTexture__5Shell,
                   kLoadTex = wotm::fn::LoadTexture__5Shell;
constexpr uint32_t kLoadMonstersSize = 0x49C, kLoadResSize = 0x45C, kLoadTexSize = 0x41C;
// Fim dos dois lacos do modo 6 no LoadMonstersDB (ra do 2o fileAddName, 0x1A9D78): o fileAddName do laco das IAs (os repetidos) fica
// com o nome que ele montou
constexpr uint32_t kModeLoopsEnd = 0x18C;
constexpr uint32_t kFormat = wotm::fn::formatFilename1__5ShellPcPCciT2Q25Shell11_shFileType, kAddName = wotm::fn::fileAddName__FPc,
                   kSetPlayer = wotm::fn::SetPlayerMonster__7TheGameiiiii;
constexpr uint32_t kLongNames = wotm::sym::MonsterLongNames_006EF860, kLongNameLen = 8, kTypes = 17;
constexpr uint32_t kFilePlayer = 1;   // Shell::SH_FILE_PLAYER
constexpr int kMaxExtra = 2, kMaxDup = 4;
// SelectSlot do traje 0 de cada coluna (taken em +0; 0 = usado) e o m_monsterSel de cada coluna
constexpr uint32_t kSelectSlots = wotm::sym::monsterSelectMode, kSlotSize = 0x2C, kColumnSel = wotm::sym::monsterNametomonsterId;

PS2Runtime::RecompiledFunction g_origLoadLevelFiles = nullptr, g_origInitPlayers = nullptr, g_origFormat = nullptr,
                               g_origAddName = nullptr, g_origSetPlayer = nullptr;
uint32_t g_costume[kTypes][kMaxDup] = {};   // traje de cada instancia (tipo, dup)
int g_count[kTypes] = {};                   // instancias de jogador de cada tipo (0: so a do retail, no traje 0)
int g_extra = 0, g_ais = 0;                 // repetidos no caminho das IAs e as IAs que ja estavam la (ate o InitPlayers)
bool g_inInit = false;

bool elimination(uint8_t *rdram) { return FAST_READ32(kMode) == 6u; }
bool inside(uint32_t ra, uint32_t fn, uint32_t size) { return ra >= fn && ra < fn + size; }
bool fromLoaders(uint32_t ra)
{
    return inside(ra, kLoadMonsters, kLoadMonstersSize) || inside(ra, kLoadRes, kLoadResSize) || inside(ra, kLoadTex, kLoadTexSize);
}
uint32_t sel(int i) { return kMonsterSel + 4u * uint32_t(i); }
uint32_t costume(int i) { return kCostume + 4u * uint32_t(i); }

// Os repetidos voltam do caminho das IAs (m_monsterSel[4..], m_costume[2..]) e as IAs que havia voltam ao lugar
void toPlayLayout(uint8_t *rdram)
{
    for (int j = 0; j < g_ais; ++j)
    {
        FAST_WRITE32(sel(4 + j), FAST_READ32(sel(4 + j + g_extra)));
        FAST_WRITE32(costume(2 + j), FAST_READ32(costume(2 + j + g_extra)));
    }
    for (int k = 0; k < g_extra; ++k)
    {
        FAST_WRITE32(sel(4 + g_ais + k), 0u);
        FAST_WRITE32(costume(2 + g_ais + k), 0u);
    }
    FAST_WRITE32(kNumAIs, uint32_t(g_ais));
    g_extra = g_ais = 0;
}

// Shell::LoadLevelFiles(this): instancias dos jogadores e os repetidos no caminho das IAs
void loadLevelFiles(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == kLoadLevelFiles)
    {
        if (g_extra)   // a carga anterior nao chegou ao InitPlayers
            toPlayLayout(rdram);
        std::memset(g_costume, 0, sizeof g_costume);
        std::fill(std::begin(g_count), std::end(g_count), 0);
        if (elimination(rdram))
        {
            const int n = std::clamp(static_cast<int32_t>(FAST_READ32(kNumPlayers)), 0, 4);
            const int ais = std::clamp(static_cast<int32_t>(FAST_READ32(kNumAIs)), 0, 10 - kMaxExtra);
            uint32_t extraSel[kMaxExtra] = {}, extraCostume[kMaxExtra] = {};
            int extra = 0;
            for (int i = 0; i < n; ++i)
            {
                const uint32_t s = FAST_READ32(sel(i)), type = s >> 5;
                const uint32_t c = i < 2 ? FAST_READ32(costume(i)) : wotmMultiCostume(i);
                if (type == 0 || type >= kTypes || c > 99u)
                    continue;
                if (g_count[type] > 0)
                {
                    if (extra >= kMaxExtra || g_count[type] >= kMaxDup)
                    {
                        std::fprintf(stderr, "[elim] jogador %d: sem vaga para mais um monstro %u (fica com o do retail)\n", i + 1, type);
                        continue;
                    }
                    extraSel[extra] = s;
                    extraCostume[extra++] = c;
                }
                for (uint32_t col = 0; col < 10; ++col)   // monstro usado: fora do dialogo 8 (a selecao com repetidos deixa a vaga livre)
                    if (FAST_READ32(kColumnSel + 4u * col) >> 5 == type)
                        FAST_WRITE32(kSelectSlots + col * 4u * kSlotSize, 0u);
                g_costume[type][g_count[type]] = c;
                std::fprintf(stderr, "[elim] jogador %d: monstro %u no traje %u (instancia %d%s)\n", i + 1, type, c, g_count[type],
                             g_count[type] ? ", carga extra" : "");
                ++g_count[type];
            }
            if (extra)
            {
                for (int j = ais - 1; j >= 0; --j)   // as IAs andam para depois dos repetidos (a ordem das duplicatas e jogadores, IAs)
                {
                    FAST_WRITE32(sel(4 + j + extra), FAST_READ32(sel(4 + j)));
                    FAST_WRITE32(costume(2 + j + extra), FAST_READ32(costume(2 + j)));
                }
                for (int k = 0; k < extra; ++k)
                {
                    FAST_WRITE32(sel(4 + k), extraSel[k]);
                    FAST_WRITE32(costume(2 + k), extraCostume[k]);
                }
                FAST_WRITE32(kNumAIs, uint32_t(ais + extra));
                g_extra = extra;
                g_ais = ais;
            }
        }
    }
    g_origLoadLevelFiles(rdram, ctx, runtime);
}

// Shell::InitPlayers(this): os repetidos voltam a jogadores antes da contagem de duplicatas
void initPlayers(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const bool entry = ctx->pc == kInitPlayers;
    if (entry && g_extra)
        toPlayLayout(rdram);
    if (entry)
        g_inInit = true;
    g_origInitPlayers(rdram, ctx, runtime);
    if (entry)
        g_inInit = false;
}

// Shell::formatFilename1(this, dst, nome, traje, ext, tipo de arquivo): a2 = nome (MonsterLongNames[tipo]), a3 = traje, t1 = tipo
void format(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == kFormat && GPR_U32(ctx, 7) == 0u && GPR_U32(ctx, 9) == kFilePlayer && fromLoaders(GPR_U32(ctx, 31)) &&
        elimination(rdram))
    {
        const uint32_t name = GPR_U32(ctx, 6);
        if (name >= kLongNames && name < kLongNames + kLongNameLen * kTypes && (name - kLongNames) % kLongNameLen == 0)
            if (const uint32_t c = g_costume[(name - kLongNames) / kLongNameLen][0])
                ctx->r[7] = _mm_set_epi64x(0, static_cast<int64_t>(c));
    }
    g_origFormat(rdram, ctx, runtime);
}

// fileAddName(nome): "MONKEY0" das cargas do retail no LoadMonstersDB vira "MONKEY<traje>" (o fileAddName guarda ate 8 letras, como
// nos outros modos)
void addName(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == kAddName && inside(GPR_U32(ctx, 31), kLoadMonsters, kModeLoopsEnd) && elimination(rdram))
    {
        char *s = reinterpret_cast<char *>(rdram + (GPR_U32(ctx, 4) & 0x01FFFFFFu));
        for (uint32_t t = 1; t < kTypes; ++t)
        {
            const char *ln = reinterpret_cast<const char *>(rdram + kLongNames + kLongNameLen * t);
            const size_t len = strnlen(ln, kLongNameLen);
            if (!g_costume[t][0] || len == 0 || std::strncmp(s, ln, len) != 0 || std::strcmp(s + len, "0") != 0)
                continue;
            std::snprintf(s + len, 0x80 - len, "%u", g_costume[t][0]);   // o buffer do LoadMonstersDB tem 0x80 bytes
            break;
        }
    }
    g_origAddName(rdram, ctx, runtime);
}

// Instancia (tipo, dup) com outro jogador que nao `self`
bool held(uint8_t *rdram, uint32_t self, uint32_t typeBits, uint32_t dup)
{
    const uint32_t n = std::min<uint32_t>(FAST_READ32(kNumPlayers), 4u);
    for (uint32_t j = 0; j < n; ++j)
        if (j != self)
            if (const uint32_t m = FAST_READ32(kMonsters + 4u * j))
                if (FAST_READ32(m + kTypeBits) == typeBits && FAST_READ32(m + kDupId) == dup)
                    return true;
    return false;
}

// TheGame::SetPlayerMonster(this, jogador, tipo, dup, vista, traje): a3 = dup, t1 = traje
void setPlayer(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == kSetPlayer && elimination(rdram))
    {
        const uint32_t player = GPR_U32(ctx, 5), typeBits = GPR_U32(ctx, 6), type = typeBits >> 5;
        uint32_t dup = GPR_U32(ctx, 7);
        if (type < kTypes)
        {
            const uint32_t count = uint32_t(std::max(g_count[type], 1));
            if (!g_inInit && (dup >= count || held(rdram, player, typeBits, dup)))   // dialogo 8, "play again": uma instancia livre
                for (uint32_t d = 0; d < count; ++d)
                    if (!held(rdram, player, typeBits, d))
                    {
                        dup = d;
                        break;
                    }
            const uint32_t c = dup < uint32_t(kMaxDup) ? g_costume[type][dup] : 0u;
            ctx->r[7] = _mm_set_epi64x(0, static_cast<int64_t>(dup));
            ctx->r[9] = _mm_set_epi64x(0, static_cast<int64_t>(c));
            std::fprintf(stderr, "[elim] jogador %u entra com o monstro %u (instancia %u) no traje %u\n", player + 1, type, dup, c);
        }
    }
    g_origSetPlayer(rdram, ctx, runtime);
}
} // namespace wotm_elimcostume

// Depois do wotmSkinsInstall e do wotmCustomInstall: o SetPlayerMonster daqui embrulha os deles. WOTM_SEL_REPEAT=0 (cada monstro com
// um jogador so, como o retail) e sem skins nem customs nos mods: nao instala.
void wotmElimCostumeInstall(PS2Runtime &runtime)
{
    using namespace wotm_elimcostume;
    const wotm::mods::Registry &r = wotm::mods::Registry::get();
    const char *rep = std::getenv("WOTM_SEL_REPEAT");
    if (r.skins().empty() && r.customs().empty() && rep && std::strcmp(rep, "0") == 0)
        return;
    if (!(g_origLoadLevelFiles = runtime.lookupFunction(kLoadLevelFiles)) || !(g_origInitPlayers = runtime.lookupFunction(kInitPlayers)) ||
        !(g_origFormat = runtime.lookupFunction(kFormat)) || !(g_origAddName = runtime.lookupFunction(kAddName)) ||
        !(g_origSetPlayer = runtime.lookupFunction(kSetPlayer)))
        return;
    runtime.registerFunction(kLoadLevelFiles, &loadLevelFiles);
    runtime.registerFunction(kInitPlayers, &initPlayers);
    runtime.registerFunction(kFormat, &format);
    runtime.registerFunction(kAddName, &addName);
    runtime.registerFunction(kSetPlayer, &setPlayer);
    std::fprintf(stderr, "[elim] eliminacao: um monstro por jogador (trajes de mod, repetidos)\n");
}
