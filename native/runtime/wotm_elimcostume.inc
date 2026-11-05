// Trajes, skins e customs na eliminacao (modo 6; MODS.md, "Skins"). Incluido pelo wotm_overrides.cpp depois do wotm_custom.inc.
//
// O retail, no modo 6, carrega os 10 monstros jogaveis de uma vez e sempre no traje 0 (Shell::LoadMonstersDB, LoadResTexture e
// LoadTexture), e quem morre escolhe outro dos livres (dialogo 8). Cada monstro e de um jogador so, entao um traje por tipo basta:
//   - Na entrada do LoadMonstersDB (o primeiro dos tres) a tabela g_costume[tipo] recebe o traje do monstro inicial de cada jogador
//     (m_costume[0..1]; jogadores 3 e 4 pelo wotmMultiCostume). Os tipos que ninguem escolheu ficam no traje 0.
//   - Shell::formatFilename1 chamado de dentro dos tres carregadores troca o traje 0 pelo da tabela (o nome do arquivo e o do mod:
//     skin ou custom com "files" sao trajes 4..99), e o fileAddName do LoadMonstersDB registra o nome com o mesmo traje ("MONKEY4"):
//     o wotm_custom.inc acha o custom de um Monster pelo numero do fim desse nome.
//   - TheGame::SetPlayerMonster recebe o traje da tabela para o tipo (o retail manda o m_costume do jogador, que e o do monstro
//     inicial; depois do dialogo 8 o monstro e outro). Registrado por ultimo: as skins (wotm_skins.inc) ainda trocam a skin pelo
//     traje base do som.
// Sem mods nada muda: a tabela fica toda em 0, como o retail.

namespace wotm_elimcostume
{
constexpr uint32_t kShell = wotm::sym::shellInfo;
constexpr uint32_t kMode = kShell + 0x2C10, kNumPlayers = kShell + 0x2BB4, kMonsterSel = kShell + 0x2938, kCostume = kShell + 0x2BE0;
constexpr uint32_t kLoadMonsters = wotm::fn::LoadMonstersDB__5Shell, kLoadRes = wotm::fn::LoadResTexture__5Shell,
                   kLoadTex = wotm::fn::LoadTexture__5Shell;
constexpr uint32_t kLoadMonstersSize = 0x49C, kLoadResSize = 0x45C, kLoadTexSize = 0x41C;
constexpr uint32_t kFormat = wotm::fn::formatFilename1__5ShellPcPCciT2Q25Shell11_shFileType, kAddName = wotm::fn::fileAddName__FPc,
                   kSetPlayer = wotm::fn::SetPlayerMonster__7TheGameiiiii;
constexpr uint32_t kLongNames = wotm::sym::MonsterLongNames_006EF860, kLongNameLen = 8, kTypes = 17;
constexpr uint32_t kFilePlayer = 1;   // Shell::SH_FILE_PLAYER

PS2Runtime::RecompiledFunction g_origLoadMonsters = nullptr, g_origFormat = nullptr, g_origAddName = nullptr, g_origSetPlayer = nullptr;
uint32_t g_costume[kTypes] = {};

bool elimination(uint8_t *rdram) { return FAST_READ32(kMode) == 6u; }
bool inside(uint32_t ra, uint32_t fn, uint32_t size) { return ra >= fn && ra < fn + size; }
bool fromLoaders(uint32_t ra)
{
    return inside(ra, kLoadMonsters, kLoadMonstersSize) || inside(ra, kLoadRes, kLoadResSize) || inside(ra, kLoadTex, kLoadTexSize);
}

// Shell::LoadMonstersDB(this): monta a tabela na entrada
void loadMonsters(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == kLoadMonsters)
    {
        std::fill(std::begin(g_costume), std::end(g_costume), 0u);
        if (elimination(rdram))
        {
            const int n = std::clamp(static_cast<int32_t>(FAST_READ32(kNumPlayers)), 0, 4);
            for (int i = 0; i < n; ++i)
            {
                const uint32_t type = FAST_READ32(kMonsterSel + 4u * uint32_t(i)) >> 5;
                const uint32_t costume = i < 2 ? FAST_READ32(kCostume + 4u * uint32_t(i)) : wotmMultiCostume(i);
                if (type == 0 || type >= kTypes || costume > 99u)
                    continue;
                g_costume[type] = costume;
                if (costume)
                    std::fprintf(stderr, "[elim] jogador %d: monstro %u no traje %u\n", i + 1, type, costume);
            }
        }
    }
    g_origLoadMonsters(rdram, ctx, runtime);
}

// Shell::formatFilename1(this, dst, nome, traje, ext, tipo de arquivo): a2 = nome (MonsterLongNames[tipo]), a3 = traje, t1 = tipo
void format(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == kFormat && GPR_U32(ctx, 7) == 0u && GPR_U32(ctx, 9) == kFilePlayer && fromLoaders(GPR_U32(ctx, 31)) &&
        elimination(rdram))
    {
        const uint32_t name = GPR_U32(ctx, 6);
        if (name >= kLongNames && name < kLongNames + kLongNameLen * kTypes && (name - kLongNames) % kLongNameLen == 0)
            if (const uint32_t c = g_costume[(name - kLongNames) / kLongNameLen])
                ctx->r[7] = _mm_set_epi64x(0, static_cast<int64_t>(c));
    }
    g_origFormat(rdram, ctx, runtime);
}

// fileAddName(nome): "MONKEY0" do LoadMonstersDB vira "MONKEY<traje>" (o fileAddName guarda ate 8 letras, como nos outros modos)
void addName(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == kAddName && inside(GPR_U32(ctx, 31), kLoadMonsters, kLoadMonstersSize) && elimination(rdram))
    {
        char *s = reinterpret_cast<char *>(rdram + (GPR_U32(ctx, 4) & 0x01FFFFFFu));
        for (uint32_t t = 1; t < kTypes; ++t)
        {
            const char *ln = reinterpret_cast<const char *>(rdram + kLongNames + kLongNameLen * t);
            const size_t len = strnlen(ln, kLongNameLen);
            if (!g_costume[t] || len == 0 || std::strncmp(s, ln, len) != 0 || std::strcmp(s + len, "0") != 0)
                continue;
            std::snprintf(s + len, 0x80 - len, "%u", g_costume[t]);   // o buffer do LoadMonstersDB tem 0x80 bytes
            break;
        }
    }
    g_origAddName(rdram, ctx, runtime);
}

// TheGame::SetPlayerMonster(this, jogador, tipo, dup, vista, traje): t1 = traje
void setPlayer(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == kSetPlayer && elimination(rdram))
    {
        const uint32_t type = GPR_U32(ctx, 6) >> 5;
        if (type < kTypes)
        {
            ctx->r[9] = _mm_set_epi64x(0, static_cast<int64_t>(g_costume[type]));
            std::fprintf(stderr, "[elim] jogador %u entra com o monstro %u no traje %u\n", GPR_U32(ctx, 5) + 1, type, g_costume[type]);
        }
    }
    g_origSetPlayer(rdram, ctx, runtime);
}
} // namespace wotm_elimcostume

// Depois do wotmSkinsInstall e do wotmCustomInstall: o SetPlayerMonster daqui embrulha os deles. So com skins ou customs nos mods.
void wotmElimCostumeInstall(PS2Runtime &runtime)
{
    using namespace wotm_elimcostume;
    const wotm::mods::Registry &r = wotm::mods::Registry::get();
    if (r.skins().empty() && r.customs().empty())
        return;
    if (!(g_origLoadMonsters = runtime.lookupFunction(kLoadMonsters)) || !(g_origFormat = runtime.lookupFunction(kFormat)) ||
        !(g_origAddName = runtime.lookupFunction(kAddName)) || !(g_origSetPlayer = runtime.lookupFunction(kSetPlayer)))
        return;
    runtime.registerFunction(kLoadMonsters, &loadMonsters);
    runtime.registerFunction(kFormat, &format);
    runtime.registerFunction(kAddName, &addName);
    runtime.registerFunction(kSetPlayer, &setPlayer);
    std::fprintf(stderr, "[elim] trajes de mod na eliminacao: um por monstro\n");
}
