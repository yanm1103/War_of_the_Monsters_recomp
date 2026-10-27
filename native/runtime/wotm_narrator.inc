// wotm_narrator.inc: narrador das selecoes (incluido por wotm_live.inc, antes do wotm_padsel.inc). Ao confirmar um monstro (telas 3 e
// 6 nativas, selecao de 3 e 4 jogadores) ou uma fase (tela 8 do jogo: o changeScreen(8, 99) da confirmacao, gancho no wotm_padsel.inc)
// toca o MP3 com o nome dele, junto com o som de confirmar do jogo (nao no lugar).
// Opcao NARRATOR da aba FEATURES do "Recompiled options" (WOTM_ANNOUNCER): 0 OFF, 1 GODZILLA SAVE THE EARTH (padrao, os MP3), 2 MONSTER
// CRY: no lugar do nome do monstro, o grito dele (o som do cinema de entrada da luta, do banco do monstro; playMonsterCry no
// wotm_snd.inc); monstro sem grito (Goliath Prime) fica em silencio; as fases ficam mudas.
//
// Arquivos <nome>.mp3: monstros (congar, robo47, togera, ultrav, preytor, kineticlops, magmo, raptros, agamo, zorgulon,
// goliathprime, cerebulon) e fases (levelName: midtownpark, gamblersgulch, rosdalecanyon, metrocity, ...), procurados nesta ordem: mods/<mod>/narrator/ (pasta mods/ de sempre: um mod pode trocar a voz), narrator/ ao lado do
// ELF e narrator/ na raiz do repo (a do build). Arquivo que falta: silencio e uma linha no log.
// Toca pelo raylib (LoadSound/PlaySound, MP3 ligado no build do raylib), no volume mestre dos efeitos do jogo (grupo 0 do som nativo,
// a mesma curva quadratica do 989snd). Um narrador por vez: o nome novo corta o anterior.
#include <filesystem>
#include <map>
#include <mutex>
#include <string>

namespace wotm_narrator
{
namespace fs = std::filesystem;

static std::mutex g_m;
static std::map<std::string, Sound> g_sounds;   // por nome do arquivo; Sound vazio (frameCount 0) = nao achado
static Sound *g_playing = nullptr;

// Arquivos de cada monstro (indice do jogo, m_monsterSel >> 5), na ordem de preferencia
static const char *const *namesOf(int monster)
{
    static const char *const kNone[] = {nullptr};
    static const char *const kNames[16][3] = {
        {nullptr},           {"congar", nullptr},  {"robo47", nullptr},      {"togera", nullptr},
        {"ultrav", nullptr}, {"preytor", nullptr}, {nullptr},                {"kineticlops", nullptr},
        {"magmo", nullptr},  {"raptros", nullptr}, {"agamo", nullptr},       {"zorgulon", nullptr},
        {nullptr},           {"goliathprime", "assboss", nullptr},           {nullptr},
        {"cerebulon", nullptr}};
    return monster >= 0 && monster < 16 ? kNames[monster] : kNone;
}

static fs::path findFile(const std::string &name)
{
    std::error_code ec;
    const std::string file = name + ".mp3";
    const std::string host = wotm::mods::Registry::get().hostPath("NARRATOR/" + file);
    if (!host.empty())
        return host;
    const fs::path modsDir = wotm::mods::Registry::get().dir();
    const fs::path dirs[] = {modsDir.empty() ? fs::path{} : modsDir.parent_path() / "narrator",
                             fs::path(__FILE__).parent_path().parent_path().parent_path() / "narrator"};   // native/runtime -> raiz do repo
    for (const fs::path &d : dirs)
        if (!d.empty() && fs::is_regular_file(d / file, ec))
            return d / file;
    return {};
}

static Sound *soundOf(const std::string &name)
{
    auto it = g_sounds.find(name);
    if (it == g_sounds.end())
    {
        Sound s{};
        const fs::path p = findFile(name);
        if (!p.empty())
            s = LoadSound(p.string().c_str());
        if (s.frameCount == 0)
            std::fprintf(stderr, "[narrador] %s.mp3 %s\n", name.c_str(), p.empty() ? "nao achado" : "ilegivel");
        else
            std::fprintf(stderr, "[narrador] %s\n", p.string().c_str());
        it = g_sounds.emplace(name, s).first;
    }
    return it->second.frameCount ? &it->second : nullptr;
}

// Volume mestre dos efeitos (0..0x400) -> 0..1, com a curva do 989snd
static float effectsVolume()
{
#if defined(WOTM_HAVE_SND)
    const float f = float(wotm::snd::Engine::get().masterVolume(0)) / float(0x400);
    return std::clamp(f * f, 0.f, 1.f);
#else
    return 1.f;
#endif
}

// Arquivo de cada fase (Shell::m_levelNum, GameLevelNames; GLOSSARIO.md, "Fases"). As de teste da equipe (17..24) nao tem.
static const char *levelName(int level)
{
    static const char *const kLevels[28] = {
        nullptr,         "midtownpark",     "gamblersgulch", "rosdalecanyon", "metrocity",  "centuryairfield", "atomicisland",
        "baytown",       "clubcaldera",     "tsunopolis",    "ufo",           "capitol",    "training",        "lakepowell",
        "minibaytown",   "volcano",         "tokyodeathmatch", nullptr,       nullptr,      nullptr,           nullptr,
        nullptr,         nullptr,           nullptr,         nullptr,         "dodgeball",  "bigshot",         "crushorama"};
    return level >= 0 && level < 28 ? kLevels[level] : nullptr;
}

// Toca o primeiro arquivo achado da lista (terminada em nullptr), so na opcao GODZILLA SAVE THE EARTH. Chamado da thread do EE.
static void play(const char *const *names)
{
    if (wotm::settings::get(wotm::settings::Announcer) != 1 || !IsAudioDeviceReady())
        return;
    std::lock_guard<std::mutex> lk(g_m);
    Sound *s = nullptr;
    for (const char *const *n = names; *n && !s; ++n)
        s = soundOf(*n);
    if (!s)
        return;
    if (g_playing && IsSoundPlaying(*g_playing))
        StopSound(*g_playing);
    SetSoundVolume(*s, effectsVolume());
    PlaySound(*s);
    g_playing = s;
}

// MONSTER CRY: o grito do monstro no lugar do nome (corta o MP3 que estiver tocando; sem grito, silencio). false = outra opcao, vale o MP3
static bool cry(int monster)
{
    if (wotm::settings::get(wotm::settings::Announcer) != 2)
        return false;
    std::lock_guard<std::mutex> lk(g_m);
    if (g_playing && IsSoundPlaying(*g_playing))
        StopSound(*g_playing);
#if defined(WOTM_HAVE_SND)
    wotm_snd_hooks::playMonsterCry(monster);
#else
    (void)monster;
#endif
    return true;
}

// Nome do monstro (indice do jogo, m_monsterSel >> 5)
static void announce(int monster)
{
    if (!cry(monster))
        play(namesOf(monster));
}
// Monstro custom: o "narrator" do monster.json e, sem o arquivo, o do tipo (com MONSTER CRY, o grito do tipo)
static void announce(const std::string &name, int monster)
{
    if (cry(monster))
        return;
    const char *const *t = namesOf(monster);
    const char *names[4] = {name.empty() ? nullptr : name.c_str(), nullptr, nullptr, nullptr};
    for (int i = 0, k = names[0] ? 1 : 0; t[i] && k < 3; ++i) names[k++] = t[i];
    play(names);
}

// Nome da fase (m_levelNum), na confirmacao da selecao de fase
static void announceLevel(int level)
{
    const char *const names[] = {levelName(level), nullptr};
    if (names[0])
        play(names);
}
} // namespace wotm_narrator
