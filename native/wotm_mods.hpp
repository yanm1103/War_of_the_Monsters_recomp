// Pasta mods/ (MODS.md): arquivos que sobrepoem os do disco, sem mexer na ISO.
// Cada subpasta de mods/ e um mod e repete a arvore do disco (mods/meu_mod/MON/CONGAR1.TEX troca o \MON\CONGAR1.TEX). Pastas
// que comecam com '_' ou '.' ficam desligadas. Com dois mods trocando o mesmo arquivo vence o ultimo em ordem alfabetica (como
// camadas: "10_base", "20_extra"); o log diz quem perdeu.
// O jogo acha os arquivos pelo fileCdSearchFile (setor e tamanho) e le por setor; cada arquivo de mod ganha uma faixa de
// setores falsos acima do fim de qualquer DVD (kFirstLsn), que os ganchos (runtime/wotm_mods.inc) e o leitor do som nativo
// atendem daqui. A camada nativa (wotm_disc.hpp: filmes, icone, fotos e monstro da lista de saves) procura aqui pelo nome.
// So host: nao depende do runtime. A varredura roda uma vez, antes do boot (scan); depois tudo e so leitura.
#pragma once
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "wotm_json.hpp"

namespace wotm::mods {

inline std::filesystem::path pathFromUtf8(const std::string &s)
{
#ifdef _WIN32
    return std::filesystem::u8path(s);
#else
    return std::filesystem::path(s);
#endif
}

// Monstros jogaveis: tipo (MonsterNewNames, m_monsterSel >> 5), nome do jogo e prefixo dos arquivos em MON/
struct MonsterInfo { int type; const char *name; const char *prefix; };
inline constexpr MonsterInfo kMonsters[] = {
    {1, "congar", "MONKEY"}, {2, "robo-47", "ROBOT"}, {3, "togera", "LIZARD"}, {4, "ultra-v", "SHOGUN"}, {5, "preytor", "MANTIS"},
    {7, "kineticlops", "ENERGY"}, {8, "magmo", "LAVA"}, {9, "raptros", "DRAGON"}, {10, "agamo", "ROCK"}, {11, "zorgulon", "ALIEN"},
};
// Pelo nome ("togera", "Robo-47", "robo47") ou pelo prefixo ("LIZARD")
inline const MonsterInfo *monsterInfo(std::string s) {
    auto norm = [](std::string t) {
        std::string o;
        for (char c : t) if (std::isalnum(static_cast<unsigned char>(c))) o += char(std::tolower(static_cast<unsigned char>(c)));
        return o;
    };
    s = norm(s);
    for (const MonsterInfo &m : kMonsters)
        if (s == norm(m.name) || s == norm(m.prefix)) return &m;
    return nullptr;
}
inline const MonsterInfo *monsterInfo(int type) {
    for (const MonsterInfo &m : kMonsters)
        if (m.type == type) return &m;
    return nullptr;
}

// Tipos que um monstro custom pode usar de base: os jogaveis e os chefes que o port ja deixa jogar (runtime/wotm_custom.inc)
inline constexpr MonsterInfo kCustomBases[] = {{13, "assboss", "ASSBOSS"}, {14, "jelly", "JELLY"}, {15, "final", "FINAL"}};
inline const MonsterInfo *customBase(const std::string &s) {
    if (const MonsterInfo *m = monsterInfo(s)) return m;
    for (const MonsterInfo &m : kCustomBases)
        if (s == m.name || s == m.prefix) return &m;
    return nullptr;
}

// Golpe especial de outro tipo ("special" do monster.json): vazio = serve; senao, o objeto do .NGP do dono que ele usa. O banco de
// dados registra esses modelos a partir do monstro que os traz (efeito, insetos, cabeca, disco, projeteis); num corpo de outro tipo
// eles nao existem e o golpe nao comeca, prende o monstro no preparo ou derruba o jogo.
// Um custom com arquivos proprios pode trazer copias dessas pecas com ids que o jogo ignora (tools/ngpgraft.py: modelo de arma
// 4000..4042 -> id + 0x100, SpecFxAnim -> id + 0x80); o port as registra so para ele (wotm_custom.inc). Ids que o .NGP proprio
// precisa ter para o especial do tipo valer; vazio = esse especial ainda nao sabe usar pecas copiadas.
inline std::vector<uint32_t> specialParts(int type) {
    switch (type) {
    case 2: return {4005 + 0x100, 0x11FF + 0x80};   // o missil (arma 13) e a cupula de choque que ele abre no impacto
    default: return {};
    }
}
// Ids dos nos de um .NGP de monstro (cru, pre-ligado em 0xA00000): raizes e GROUPs ate 4 niveis abaixo delas
inline std::vector<uint32_t> ngpNodeIds(const std::filesystem::path &p) {
    std::vector<uint32_t> out;
    std::ifstream in(p, std::ios::binary);
    if (!in) return out;
    const std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    constexpr uint32_t kBase = 0xA00000;
    auto u32 = [&](uint32_t a, uint32_t &v) {
        if (a < kBase || a - kBase + 4 > d.size()) return false;
        std::memcpy(&v, d.data() + (a - kBase), 4);
        return true;
    };
    auto walk = [&](auto &&self, uint32_t n, int depth) -> void {
        uint32_t w = 0, cnt = 0;
        if (depth > 4 || !u32(n & 0x0FFFFFFFu, w)) return;
        out.push_back(w >> 18);
        if ((w & 63) != 1 || !u32((n & 0x0FFFFFFFu) + 8, cnt)) return;
        for (uint32_t k = 0, c = 0; k < (cnt & 0xFFFFu) && k < 512; ++k)
            if (u32((n & 0x0FFFFFFFu) + 0x20 + 4 * k, c)) self(self, c, depth + 1);
    };
    uint32_t roots = 0;
    if (d.size() < 8 || !u32(kBase, roots)) return out;
    for (uint32_t i = 0, r = 0; i < roots && i < 16; ++i)
        if (u32(kBase + 4 + 4 * i, r)) walk(walk, r, 0);
    return out;
}
// O .NGP dos arquivos proprios ("files") traz todos os `ids`
inline bool filesHaveIds(const std::filesystem::path &files, const std::vector<uint32_t> &ids) {
    namespace fs = std::filesystem;
    std::error_code ec;
    for (auto it = fs::directory_iterator(files, ec); it != fs::directory_iterator(); it.increment(ec)) {
        if (ec) break;
        std::string ext = it->path().extension().string();
        for (char &ch : ext) ch = char(std::toupper(static_cast<unsigned char>(ch)));
        if (ext != ".NGP") continue;
        const std::vector<uint32_t> have = ngpNodeIds(it->path());
        if (std::all_of(ids.begin(), ids.end(), [&](uint32_t i) { return std::find(have.begin(), have.end(), i) != have.end(); }))
            return true;
    }
    return false;
}
inline const char *specialNeeds(int type) {
    switch (type) {
    case 2: return "o missil e a cupula de choque (arma 4005 e SpecFxAnim 0x11FF do ROBOT)";
    case 4: return "a bola do gancho (OgreMace, do SHOGUN)";
    case 5: return "os insetos (HomingBug, do MANTIS)";
    case 8: return "as rochas de lava (armas do LAVA)";
    case 10: return "a cabeca (pickup do ROCK)";
    case 11: return "o disco (anexo do ALIEN)";
    case 13: return "as balas do canhao (armas do ASSBOSS)";
    default: return "";
    }
}

constexpr uint32_t kFireSet = 0x01000000u;   // bit de "fireColor"/"shotColor" presente (a cor #000000 tambem vale)

// Monstro custom (mods/<mod>/monsters/<pasta>/monster.json; MODS.md, "Monstros custom"): um monstro novo na selecao (casa
// CUSTOM), que usa o codigo de um tipo do jogo (`base`) e o modelo e os arquivos dele, ou os proprios ("files": uma subpasta com
// .NGP+.PTR(+.TEX/.RTX), que vira um traje de mod so dele, como uma skin implicita).
struct Custom {
    std::string id;             // "goliath" (WOTM_MATCH=goliath, o save)
    std::string name;           // o que a selecao e a tela mostram ("GOLIATH")
    int type = 0;               // tipo do jogo (13 = assboss)
    std::string typeName;       // "assboss"
    std::string prefix;         // "ASSBOSS"
    int costume = 0;            // traje padrao: o do disco (0..3) ou, com "files", o traje de mod dos arquivos proprios (4..99)
    int diskCostume = 0;        // traje do disco do "costume" do JSON: base das skins dele e o que os arquivos proprios nao trazem
    std::filesystem::path files;   // pasta dos arquivos proprios ("files"); vazia = os do tipo
    // .CHR e .AIC da pasta "files": servidos como RST/<resetStem>.CHR e RST/<resetStem><dificuldade>.AIC (wotm_custom.inc troca
    // o nome no charreset/AiReset); vazio = os do tipo
    std::string resetStem;
    bool ownChr = false;
    bool ownAic = false;
    // .BNK da pasta "files": banco de som proprio, com os indices do banco do tipo (wotm_snd.inc o troca em volta dos sons desta
    // instancia); vazio = o do tipo
    std::filesystem::path bank;
    // .BNK da pasta "files" com o prefixo do dono do especial emprestado ("special"; ROBOT_S.BNK): os sons que o especial toca
    // do banco desse tipo saem dele (wotm_snd.inc); vazio = os do banco do jogo, que so existe com esse monstro na luta
    std::filesystem::path specialBank;
    // GENAMB_S.BNK (ou GENAM_LS.BNK) da pasta "files": o banco comum, com os indices dele; os sons que esta instancia toca de
    // gSoundBanks[BANK_AMBIENT] (o sprint, o ram, o arrasto...) saem dele (wotm_snd.inc); vazio = os do jogo
    std::filesystem::path ambientBank;
    std::string kind;       // regras proprias do port ("kind": "mecha" = escudo e laser, wotm_custom.inc); vazio = as do tipo
    int special = 0;          // tipo cujo golpe especial o custom usa ("special": "congar"; wotm_custom.inc); 0 = o do tipo
    bool specialRoar = false;   // "specialRoar": o especial comeca com o grito dele (o som do cinema de entrada; wotm_custom.inc)
    // "fallBlend": quadros em que a camada da subida do pulo (animacao 33) some depois do pico, para a queda (34) aparecer
    // inteira (0 = corte; -1 = como o jogo: peso pela velocidade vertical ate perto do chao; wotm_custom.inc)
    int fallBlend = -1;
    // "landOnTouch": a camada do pouso do pulo (animacao 35) so entra no toque no chao, sem pose de preparo no ar (wotm_custom.inc)
    bool landOnTouch = false;
    // "rootTurn": {"<animacao>": [graus...]}: quanto o personagem gira durante a animacao, acumulado e relativo ao primeiro quadro, uma
    // entrada por quadro (positivo para +x, como o heading do jogo). O STE gira o personagem inteiro num golpe de cauda; o jogo so
    // gira o monstro pela entrada, entao a animacao propria sai sem esse giro e o jogo gira o monstro por esta curva (wotm_custom.inc)
    std::map<int, std::vector<float>> rootTurn;
    std::string cameraFrom;   // MonsterLongNames do .CHR da camera ("lizard"); vazio = a do proprio tipo
    float cameraScale = 1.f;    // distancias da camera emprestada
    std::string narrator;       // narrator/<nome>.mp3 (vazio = o do tipo)
    std::string costumeName;    // nome do traje proprio na selecao (vazio = "FROM MOD <mod>")
    // "fireColor": cor do fogo do traje proprio e das skins sem a sua (kFireSet | 0xRRGGBB; 0 = a do jogo). Os efeitos de
    // particula do Togera (bafo, bola de fogo, explosao, crowd control) criados por este monstro trocam o verde por ela
    // (wotm_custom.inc marca, wotm_fx.hpp pinta)
    uint32_t fireColor = 0;
    // "shotColor": cor do tiro padrao (o projetil do tipo, Monster +0x6878) do traje proprio e das skins sem a sua: as particulas
    // da explosao dele e a luz que ele carrega (wotm_custom.inc)
    uint32_t shotColor = 0;
    bool ai = false;           // uma CPU pode usar (quadro de CPU da selecao)
    std::filesystem::path portrait;   // PNG do retrato do HUD (vazio = o do .RTX)
    int portraitTex = -1;       // texId do retrato no <PREFIXO><traje>.RTX
    std::filesystem::path logo;   // PNG 2:1 do logo (selecao e lista de saves; vazio = o nome em texto)
    std::string mod;
    std::filesystem::path dir;
};

// Traje de mod (mods/<mod>/skins/<pasta>/skin.json)
struct Skin {
    int monster = 0;            // tipo do monstro
    std::string monsterName;    // "togera" (numa skin de custom, o id dele)
    std::string custom;         // id do monstro custom dono da skin (vazio: skin de monstro do jogo); so aparece na lista dele
    std::string prefix;         // "LIZARD"
    std::string id;             // "green" (o save guarda "togera/green")
    std::string name;           // o que a selecao mostra
    int base = 0;               // traje do disco que da o que falta (e os sons do traje)
    bool ai = false;            // as IAs sorteadas podem usar
    std::string narrator;       // narrator/<nome>.mp3 ao confirmar a skin (vazio = o do custom ou o do tipo)
    uint32_t fireColor = 0;     // "fireColor": cor do fogo (kFireSet | 0xRRGGBB; 0 = a do custom ou a do jogo)
    uint32_t shotColor = 0;     // "shotColor": cor do tiro padrao (kFireSet | 0xRRGGBB; 0 = a do custom ou a do jogo)
    int index = 0;             // traje no jogo (4..99), so nesta execucao
    bool own = false;           // os arquivos proprios de um custom ("files"): e o traje padrao dele, nao uma skin da lista
    std::string mod;
    std::filesystem::path dir;
};
// PNG de uma skin: vale so para o arquivo do traje dela (stem "LIZARD4"), nunca para o traje base
struct SkinPng { std::string stem; std::filesystem::path host; std::string mod, rel; };

// Fases que uma fase custom pode usar de base (indice de GameLevelNames, nome dos arquivos) e os arquivos de PTS/ que cada uma tem no
// disco (todas tem os 4 de LVL/ e o RST/<NOME>.MRS). Fora: as 6 com script em todo modo (AIRPORT, 3-MILE, SANFRAN, ISLAND, TOKYO,
// VOLCANO) e a TRAINING (a fase do modo treino do port, sem .FOP/.PED).
struct StageBase { int level; const char *name; const char *pts; };
inline constexpr StageBase kStageBases[] = {
    {1, "CENTRAL", "SPT PUP FOP PED AIP"}, {2, "VEGAS", "SPT PUP FOP PED AIP"},  {3, "CANYON2", "SPT PUP FOP PED AIP"},
    {4, "METRO", "SPT PUP FOP PED"},       {10, "UFO", "SPT PUP FOP PED AIP"},   {11, "CAPITOL", "SPT PUP FOP PED AIP"},
    {13, "CANYON1", "SPT PUP AIP"},        {14, "MINISF", "SPT PUP FOP PED AIP"}, {16, "TOKYODM", "SPT PUP FOP PED AIP"},
};
inline const StageBase *stageBase(const std::string &s) {
    std::string u;
    for (char c : s) u += char(std::toupper(static_cast<unsigned char>(c)));
    for (const StageBase &b : kStageBases)
        if (u == b.name || u == std::to_string(b.level)) return &b;
    return nullptr;
}
// Os 28 nomes de GameLevelNames: o id de uma fase custom nao pode repetir nenhum
inline constexpr const char *kLevelFiles[28] = {
    "DEFAULT", "CENTRAL", "VEGAS", "CANYON2", "METRO", "AIRPORT", "3-MILE", "SANFRAN", "ISLAND", "TOKYO", "UFO", "CAPITOL", "TRAINING", "CANYON1",
    "MINISF", "VOLCANO", "TOKYODM", "DYLAN", "DAVID", "LARS", "JENN", "YEON", "JACOB", "STEVE", "PIERRE", "DBALL", "BIGSHOT", "CRUSH"};
constexpr int kFirstStageSlot = 18, kStageSlots = 6;   // vagas 18..23 do indice de fases (fases de teste da equipe, sem arquivos)

// Fase custom (MODS.md, "Custom stages"): mods/<mod>/stages/<pasta>/stage.json. Carrega os arquivos pelo id (alias para os da base,
// so do disco) e roda o codigo por fase da base (m_levelNum = base na confirmacao: runtime/wotm_stages.h).
struct Stage {
    std::string id;         // "mycity" (minusculas; ate 8: o fileStatus guarda 8)
    std::string name;       // "MY CITY" (ate 17: GameLevelLongNames, 18 bytes)
    const StageBase *base = nullptr;
    int slot = 0;           // vaga 18..23
    std::string narrator;   // narrator/<nome>.mp3 na confirmacao (vazio = calado)
    bool random = true;     // a RANDOM pode sortear
    bool ownAip = false;    // traz PTS/<ID>.AIP (a base METRO nao tem: fora da lista com CPUs sem ele)
    std::string mod;
    std::filesystem::path dir;
    std::string upperId() const {
        std::string u;
        for (char c : id) u += char(std::toupper(static_cast<unsigned char>(c)));
        return u;
    }
};

struct File {
    std::string rel;              // "MON/CONGAR1.TEX" (maiusculas, '/')
    std::filesystem::path host;   // arquivo no PC
    std::string mod;              // nome da pasta do mod
    uint64_t size = 0;
    uint32_t lsn = 0, sectors = 0;   // faixa falsa: [lsn, lsn + sectors)
    mutable bool logged = false;     // primeira busca do jogo ja saiu no log
};

class Registry {
public:
    static constexpr uint32_t kFirstLsn = 0x00800000u;   // 16 GB em setores de 2 KB: alem de qualquer DVD (~2,3 M setores)
    static constexpr uint64_t kMaxSize = 256ull << 20;

    static Registry &get() { static Registry r; return r; }

    static std::string key(std::string rel) {
        std::replace(rel.begin(), rel.end(), '\\', '/');
        while (!rel.empty() && rel[0] == '/') rel.erase(0, 1);
        for (char &c : rel) c = char(std::toupper(static_cast<unsigned char>(c)));
        return rel;
    }
    // Nome como o jogo o monta ("\LVL\CENTRAL.NGP;1", "cdrom0:\SND\SHELL_S.BNK;1") -> "LVL/CENTRAL.NGP"
    static std::string keyFromGame(std::string name) {
        if (const auto c = name.find(':'); c != std::string::npos) name = name.substr(c + 1);
        if (const auto sc = name.find(';'); sc != std::string::npos) name = name.substr(0, sc);
        return key(name);
    }

    // Arquivo gerado pelo port (ex.: MON/ARMY.*, wotm_army_files.hpp), antes do scan: entra como um mod; um mod de verdade com o
    // mesmo nome vence
    void addGenerated(const std::string &rel, const std::filesystem::path &host, const std::string &mod) {
        std::error_code ec;
        File &f = m_files[key(rel)];
        f.rel = key(rel);
        f.host = host;
        f.mod = mod;
        f.size = std::filesystem::file_size(host, ec);
    }

    // Le a pasta mods/ (uma vez). Devolve quantos arquivos entraram (com os gerados).
    size_t scan(const std::filesystem::path &dir) {
        namespace fs = std::filesystem;
        std::error_code ec;
        m_dir = dir;
        if (dir.empty() || !fs::is_directory(dir, ec)) { assignLsns(); return m_files.size(); }
        std::vector<fs::path> mods;
        for (const auto &e : fs::directory_iterator(dir, ec)) {
            const std::string n = e.path().filename().string();
            if (e.is_directory(ec) && !n.empty() && n[0] != '_' && n[0] != '.') mods.push_back(e.path());
        }
        std::sort(mods.begin(), mods.end(), [](const fs::path &a, const fs::path &b) { return key(a.filename().string()) < key(b.filename().string()); });
        for (const fs::path &m : mods) {
            m_mods.push_back(m.filename().string());
            for (auto it = fs::recursive_directory_iterator(m, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (ec) break;
                if (!it->is_regular_file(ec)) continue;
                const std::string rel = key(fs::relative(it->path(), m, ec).generic_string());
                const uint64_t size = it->file_size(ec);
                if (rel.empty() || ec) continue;
                if (rel.rfind("SKINS/", 0) == 0) continue;   // skins/<id>/: trajes novos (scanSkins), nao sobrepoem o disco
                if (rel.rfind("MONSTERS/", 0) == 0) continue;   // monsters/<id>/: monstros custom (scanCustoms)
                if (rel.rfind("STAGES/", 0) == 0) continue;   // stages/<pasta>/: fases custom (scanStages)
                if (size > kMaxSize) { std::fprintf(stderr, "[mods] %s/%s ignorado: maior que 256 MB\n", m_mods.back().c_str(), rel.c_str()); continue; }
                File &f = m_files[rel];
                if (!f.mod.empty()) std::fprintf(stderr, "[mods] %s: %s vence %s\n", rel.c_str(), m_mods.back().c_str(), f.mod.c_str());
                f.rel = rel;
                f.host = it->path();
                f.mod = m_mods.back();
                f.size = size;
            }
        }
        scanCustoms(mods);   // antes das skins: uma skin pode ser de um custom
        scanSkins(mods);
        for (Custom &c : m_customs) {   // "files": o traje padrao passa a ser o dos arquivos proprios
            if (c.files.empty()) continue;
            const auto k = std::find_if(m_skins.begin(), m_skins.end(), [&](const Skin &s) { return s.own && s.custom == c.id; });
            if (k == m_skins.end()) {
                std::fprintf(stderr, "[mods] monstro %s: \"files\" nao entrou (veja acima); usando o traje %d do disco\n", c.id.c_str(), c.diskCostume);
                c.files.clear();
                continue;
            }
            c.costume = k->index;
            scanResetFiles(c);
        }
        scanStages(mods);   // depois dos customs e das skins: o id de uma fase nao pode repetir o deles
        assignLsns();
        return m_files.size() + m_alias.size();
    }
    // Faixas de setores na ordem dos nomes (estavel entre execucoes), com um setor de folga entre arquivos
    void assignLsns() {
        uint32_t next = kFirstLsn;
        m_byLsn.clear();
        for (auto &[rel, f] : m_files) {
            f.sectors = uint32_t((f.size + 2047) / 2048);
            f.lsn = next;
            next += std::max(f.sectors, 1u) + 1;
            m_byLsn.push_back(&f);
        }
        m_ready = true;
    }

    bool ready() const { return m_ready; }
    const std::filesystem::path &dir() const { return m_dir; }
    const std::vector<std::string> &modNames() const { return m_mods; }
    size_t count() const { return m_files.size(); }
    const std::map<std::string, File> &files() const { return m_files; }

    const File *find(const std::string &rel) const {
        if (!m_ready || m_files.empty()) return nullptr;
        const auto it = m_files.find(key(rel));
        return it == m_files.end() ? nullptr : &it->second;
    }
    // Arquivo do traje base que um traje de mod usa no lugar do que a pasta da skin nao trouxe ("MON/LIZARD4.NGP" ->
    // "MON/LIZARD1.NGP"); senao o proprio nome. O que se le do disco e sempre aliasOf(rel).
    std::string aliasOf(const std::string &rel) const {
        if (!m_ready || m_alias.empty()) return key(rel);
        const auto it = m_alias.find(key(rel));
        return it == m_alias.end() ? key(rel) : it->second;
    }
    // Alias de fase custom ("LVL/MYCITY.NGP" -> "LVL/CENTRAL.NGP"): o arquivo e sempre o do disco, nunca o de outro mod
    bool discAlias(const std::string &rel) const { return m_ready && !m_discAlias.empty() && m_discAlias.count(key(rel)) != 0; }
    // Arquivo de mod que atende `rel` (o do alias, menos nos de fase custom); nullptr = o do disco
    const File *served(const std::string &rel) const { return discAlias(rel) ? nullptr : find(aliasOf(rel)); }
    std::string hostPath(const std::string &rel) const {
        const File *f = served(rel);
        return f ? f->host.string() : std::string{};
    }

    // ---- fases custom ----
    const std::vector<Stage> &stages() const { return m_stages; }
    const Stage *stage(const std::string &id) const {
        for (const Stage &s : m_stages)
            if (s.id == lower(id)) return &s;
        return nullptr;
    }
    const Stage *stageBySlot(int slot) const {
        for (const Stage &s : m_stages)
            if (s.slot == slot) return &s;
        return nullptr;
    }
    // PNGs da pasta de uma fase custom (stem = o id em maiusculas)
    const std::vector<SkinPng> &stagePngs() const { return m_stagePngs; }

    // ---- skins de mod (trajes 4 em diante) ----
    const std::vector<Skin> &skins() const { return m_skins; }
    // Skin pelo tipo do monstro (MonsterNewNames) e pelo id ("green") ou "monstro/id"; nullptr se nao ha.
    // Com "monstro/id", o monstro tem de bater (o mesmo id pode existir no Togera e num custom de base Togera).
    const Skin *skin(int monster, std::string id) const {
        std::string owner;
        if (const auto s = id.find('/'); s != std::string::npos) owner = lower(id.substr(0, s)), id = id.substr(s + 1);
        for (const Skin &k : m_skins)
            if (k.monster == monster && k.id == lower(id) && (owner.empty() || owner == k.monsterName)) return &k;
        return nullptr;
    }
    // Trajes de um monstro custom: o do monster.json e as skins dele, nessa ordem
    std::vector<int> customCostumes(const Custom &c) const {
        std::vector<int> v{c.costume};
        for (const Skin &k : m_skins)
            if (k.custom == c.id && !k.own) v.push_back(k.index);
        return v;
    }
    const Skin *skinByIndex(int monster, int index) const {
        for (const Skin &k : m_skins)
            if (k.monster == monster && k.index == index) return &k;
        return nullptr;
    }
    const std::vector<SkinPng> &skinPngs() const { return m_skinPngs; }

    // ---- monstros custom ----
    const std::vector<Custom> &customs() const { return m_customs; }
    const Custom *custom(const std::string &id) const {
        for (const Custom &c : m_customs)
            if (c.id == lower(id)) return &c;
        return nullptr;
    }
    // O custom que usa esse tipo e traje (o primeiro), para os ganchos da luta
    const Custom *customByType(int type, int costume = -1) const {
        for (const Custom &c : m_customs)
            if (c.type == type && (costume < 0 || c.costume == costume)) return &c;
        return nullptr;
    }
    static bool isModLsn(uint32_t lsn) { return lsn >= kFirstLsn; }
    const File *findLsn(uint32_t lsn) const {
        if (!isModLsn(lsn)) return nullptr;
        auto it = std::upper_bound(m_byLsn.begin(), m_byLsn.end(), lsn, [](uint32_t l, const File *f) { return l < f->lsn; });
        if (it == m_byLsn.begin()) return nullptr;
        const File *f = *(it - 1);
        return lsn < f->lsn + std::max(f->sectors, 1u) ? f : nullptr;
    }

    // Le `sectors` setores a partir do LSN falso; o que passa do fim do arquivo vem zerado. false: LSN fora dos mods.
    bool read(uint32_t lsn, uint32_t sectors, uint8_t *dst, size_t bytes) const {
        const File *f = findLsn(lsn);
        if (!f) return false;
        std::memset(dst, 0, bytes);
        const uint64_t off = uint64_t(lsn - f->lsn) * 2048;
        if (off >= f->size) return true;
        const size_t n = size_t(std::min<uint64_t>({uint64_t(sectors) * 2048, uint64_t(bytes), f->size - off}));
        std::ifstream in(f->host, std::ios::binary);
        if (!in || !in.seekg(static_cast<std::streamoff>(off))) return false;
        in.read(reinterpret_cast<char *>(dst), static_cast<std::streamsize>(n));
        return in.gcount() == static_cast<std::streamsize>(n);
    }

private:
    std::filesystem::path m_dir;
    std::vector<std::string> m_mods;
    std::map<std::string, File> m_files;
    std::vector<const File *> m_byLsn;   // em ordem de LSN (o map ja vem em ordem de nome, e os LSN seguem essa ordem)
    std::map<std::string, std::string> m_alias;   // "MON/LIZARD4.NGP" -> "MON/LIZARD1.NGP"
    std::set<std::string> m_discAlias;   // aliases de fase custom: so do disco
    std::vector<Stage> m_stages;
    std::vector<SkinPng> m_stagePngs;
    std::vector<Skin> m_skins;
    std::vector<SkinPng> m_skinPngs;
    std::vector<Custom> m_customs;
    std::atomic<bool> m_ready{false};

    static std::string lower(std::string s) {
        for (char &c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
    // "fireColor"/"shotColor": "#RRGGBB" -> kFireSet | 0xRRGGBB (0 = sem cor propria)
    static uint32_t color(const json::Value &v, const char *key, const std::string &where) {
        const json::Value *fc = v.get(key);
        if (!fc) return 0;
        const std::string s = fc->type == json::Value::Str ? fc->s : std::string();
        if (s.size() == 7 && s[0] == '#' && s.find_first_not_of("0123456789abcdefABCDEF", 1) == std::string::npos)
            return kFireSet | uint32_t(std::stoul(s.substr(1), nullptr, 16));
        std::fprintf(stderr, "[mods] %s: \"%s\" tem de ser \"#RRGGBB\"; ignorado\n", where.c_str(), key);
        return 0;
    }
    // Filho de `dir` com esse nome, sem olhar maiusculas
    static std::filesystem::path child(const std::filesystem::path &dir, const std::string &name) {
        std::error_code ec;
        for (const auto &e : std::filesystem::directory_iterator(dir, ec))
            if (key(e.path().filename().string()) == key(name)) return e.path();
        return {};
    }

    // mods/<mod>/skins/<pasta>/skin.json (MODS.md, "Skins"): {"monster", "id", "name", "base", "ai", "narrator"}. Cada skin vira o traje
    // <PREFIXO><n> (n = 4, 5, ... por monstro, na ordem mod/id; so nesta execucao: o save guarda "monstro/id"). O que a pasta traz
    // (.NGP+.PTR, .TEX, .RTX, PNGs) e dela; o que falta e o arquivo do traje base (m_alias).
    void scanSkins(const std::vector<std::filesystem::path> &mods) {
        namespace fs = std::filesystem;
        std::error_code ec;
        std::map<std::string, Skin> byKey;   // "togera/green"
        for (const fs::path &m : mods) {
            const fs::path dir = child(m, "skins");
            if (dir.empty() || !fs::is_directory(dir, ec)) continue;
            std::vector<fs::path> subs;
            for (const auto &e : fs::directory_iterator(dir, ec))
                if (e.is_directory(ec)) subs.push_back(e.path());
            std::sort(subs.begin(), subs.end());
            for (const fs::path &s : subs) {
                const std::string where = m.filename().string() + "/skins/" + s.filename().string();
                const fs::path js = child(s, "skin.json");
                std::string text;
                if (!js.empty()) {
                    std::ifstream in(js, std::ios::binary);
                    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                }
                json::Value v;
                std::string err;
                if (js.empty() || !json::parse(text, v, &err) || v.type != json::Value::Obj) {
                    std::fprintf(stderr, "[mods] skin %s ignorada: %s\n", where.c_str(), js.empty() ? "sem skin.json" : ("skin.json invalido: " + err).c_str());
                    continue;
                }
                Skin k;
                const json::Value *mon = v.get("monster");
                const MonsterInfo *mi = mon && mon->type == json::Value::Str ? monsterInfo(mon->s) : nullptr;
                const Custom *cu = !mi && mon && mon->type == json::Value::Str ? custom(mon->s) : nullptr;
                if (!mi && !cu) {
                    std::fprintf(stderr, "[mods] skin %s ignorada: \"monster\" nao e um monstro jogavel (togera, congar, ...) nem o id de um monstro custom\n", where.c_str());
                    continue;
                }
                if (cu) {   // skin de custom: um traje do tipo base, que so a lista do custom oferece
                    k.monster = cu->type;
                    k.monsterName = cu->id;
                    k.custom = cu->id;
                    k.prefix = cu->prefix;
                } else {
                    k.monster = mi->type;
                    k.monsterName = mi->name;
                    k.prefix = mi->prefix;
                }
                const json::Value *id = v.get("id");
                k.id = lower(id && id->type == json::Value::Str ? id->s : s.filename().string());
                if (k.id.empty() || k.id.size() > 32 || k.id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos) {
                    std::fprintf(stderr, "[mods] skin %s ignorada: \"id\" so com letras, digitos e _ (ate 32)\n", where.c_str());
                    continue;
                }
                const json::Value *nm = v.get("name");
                k.name = nm && nm->type == json::Value::Str && !nm->s.empty() ? nm->s : k.id;
                const json::Value *base = v.get("base");
                k.base = base ? int(base->asInt(0)) : cu ? cu->diskCostume : 0;
                if (k.base < 0 || k.base > 3) {
                    std::fprintf(stderr, "[mods] skin %s: \"base\" %d fora de 0..3, usando 0\n", where.c_str(), k.base);
                    k.base = 0;
                }
                const json::Value *ai = v.get("ai");
                k.ai = ai && ai->asBool(false);
                if (const json::Value *na = v.get("narrator"); na && na->type == json::Value::Str) k.narrator = lower(na->s);
                k.fireColor = color(v, "fireColor", "skin " + where);
                k.shotColor = color(v, "shotColor", "skin " + where);
                k.mod = m.filename().string();
                k.dir = s;
                Skin &slot = byKey[k.monsterName + "/" + k.id];
                if (!slot.id.empty())
                    std::fprintf(stderr, "[mods] skin %s/%s: %s vence %s\n", k.monsterName.c_str(), k.id.c_str(), k.mod.c_str(), slot.mod.c_str());
                slot = k;
            }
        }
        for (const Custom &cu : m_customs) {   // "files" de um custom: skin implicita com o id dele
            if (cu.files.empty()) continue;
            Skin k;
            k.monster = cu.type;
            k.monsterName = k.custom = k.id = cu.id;
            k.prefix = cu.prefix;
            k.name = cu.name;
            k.base = cu.diskCostume;
            k.ai = cu.ai;
            k.own = true;
            k.mod = cu.mod;
            k.dir = cu.files;
            Skin &slot = byKey[k.monsterName + "/" + k.id];
            if (!slot.id.empty())
                std::fprintf(stderr, "[mods] monstro %s: os arquivos de \"files\" vencem a skin %s/%s de %s\n", cu.id.c_str(), k.monsterName.c_str(), k.id.c_str(), slot.mod.c_str());
            slot = k;
        }
        for (auto &[name, k] : byKey) m_skins.push_back(k);
        std::stable_sort(m_skins.begin(), m_skins.end(), [](const Skin &a, const Skin &b) {
            return a.monster != b.monster ? a.monster < b.monster : key(a.mod) != key(b.mod) ? key(a.mod) < key(b.mod) : a.id < b.id;
        });
        std::map<int, int> next;
        std::vector<Skin> kept;
        for (Skin &k : m_skins) {
            int &n = next.emplace(k.monster, 4).first->second;
            const std::string stem = k.prefix + std::to_string(n);
            if (n > 99 || stem.size() > 8) {   // o fileStatus guarda nomes de ate 8 letras
                std::fprintf(stderr, "[mods] skin %s/%s ignorada: trajes demais para %s\n", k.monsterName.c_str(), k.id.c_str(), k.monsterName.c_str());
                continue;
            }
            std::map<std::string, fs::path> own;   // extensao -> arquivo da pasta
            std::vector<fs::path> pngs;
            for (auto it = fs::recursive_directory_iterator(k.dir, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (ec) break;
                if (!it->is_regular_file(ec)) continue;
                const std::string ext = key(it->path().extension().string());
                if (ext == ".PNG") pngs.push_back(it->path());
                else if (ext == ".NGP" || ext == ".PTR" || ext == ".TEX" || ext == ".RTX") {
                    auto &o = own[ext.substr(1)];
                    if (!o.empty()) std::fprintf(stderr, "[mods] skin %s/%s: dois .%s, usando %s\n", k.monsterName.c_str(), k.id.c_str(), ext.c_str() + 1, o.filename().string().c_str());
                    else o = it->path();
                }
            }
            if (own.count("NGP") != own.count("PTR")) {
                std::fprintf(stderr, "[mods] skin %s/%s ignorada: .NGP e .PTR andam juntos (o .PTR relocava o .NGP)\n", k.monsterName.c_str(), k.id.c_str());
                continue;
            }
            k.index = n++;
            std::string brought;
            for (const char *ext : {"NGP", "PTR", "TEX", "RTX"}) {
                const std::string rel = "MON/" + stem + "." + ext;
                if (const auto o = own.find(ext); o != own.end()) {
                    File &f = m_files[rel];
                    f.rel = rel;
                    f.host = o->second;
                    f.mod = k.mod;
                    f.size = fs::file_size(o->second, ec);
                    brought += std::string(brought.empty() ? "" : " ") + ext;
                } else
                    m_alias[rel] = "MON/" + k.prefix + std::to_string(k.base) + "." + ext;
            }
            for (const fs::path &p : pngs) m_skinPngs.push_back(SkinPng{stem, p, k.mod, k.monsterName + "/" + k.id + "/" + p.filename().string()});
            std::fprintf(stderr, "[mods] skin %s/%s (\"%s\", %s) = traje %d (%s); base %d%s%s, %zu png\n", k.monsterName.c_str(), k.id.c_str(),
                         k.name.c_str(), k.mod.c_str(), k.index, stem.c_str(), k.base, brought.empty() ? "" : "; da pasta: ", brought.c_str(), pngs.size());
            kept.push_back(k);
        }
        m_skins = std::move(kept);
    }

    // mods/<mod>/stages/<pasta>/stage.json (MODS.md, "Custom stages"): {"id", "name", "base", "narrator", "random"}. Cada fase ganha
    // uma vaga 18..23 do indice de fases, na ordem mod/id. Os arquivos sao os da base pelo id (LVL/<ID>.*, PTS/<ID>.*, RST/<ID>.MRS ->
    // os da base, so do disco: m_discAlias), menos os PTS e o .MRS que a pasta traz. .NGP/.PTR/.TEX/.RTX proprios ainda nao entram (o
    // .PTR reloca o .NGP e o .TEX/.RTX levam os texIds dele: geometria propria e um grupo de tudo ou nada, fora desta versao).
    void scanStages(const std::vector<std::filesystem::path> &mods) {
        namespace fs = std::filesystem;
        std::error_code ec;
        const auto reserved = [&](const std::string &up) -> const char * {
            for (const char *n : kLevelFiles)
                if (up == n) return "nome de uma fase do jogo";
            for (const char *n : {"MONSTER", "SHELL", "SHELL2", "PRESHELL", "LOAD", "UI", "ARMY", "CRATER", "QUAKE", "MONSTERS", "OGRE", "SPIDER"})
                if (up == n) return "nome de arquivo do jogo";
            const auto prefixed = [&](const char *p) {   // MON/<PREFIXO><n>, RST/<PREFIXO>.CHR: monstros e trajes (de mod tambem)
                const size_t n = std::strlen(p);
                return up.compare(0, n, p) == 0 && up.find_first_not_of("0123456789", n) == std::string::npos;
            };
            for (const MonsterInfo &m : kMonsters)
                if (prefixed(m.prefix)) return "nome de arquivo de monstro";
            for (const MonsterInfo &m : kCustomBases)
                if (prefixed(m.prefix)) return "nome de arquivo de monstro";
            for (const Custom &c : m_customs)
                if (key(c.id) == up) return "id de um monstro custom";
            return nullptr;
        };
        std::vector<Stage> found;
        for (const fs::path &m : mods) {
            const fs::path dir = child(m, "stages");
            if (dir.empty() || !fs::is_directory(dir, ec)) continue;
            std::vector<fs::path> subs;
            for (const auto &e : fs::directory_iterator(dir, ec))
                if (e.is_directory(ec)) subs.push_back(e.path());
            std::sort(subs.begin(), subs.end());
            for (const fs::path &s : subs) {
                const std::string where = m.filename().string() + "/stages/" + s.filename().string();
                const fs::path js = child(s, "stage.json");
                std::string text;
                if (!js.empty()) {
                    std::ifstream in(js, std::ios::binary);
                    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                }
                json::Value v;
                std::string err;
                if (js.empty() || !json::parse(text, v, &err) || v.type != json::Value::Obj) {
                    std::fprintf(stderr, "[mods] fase %s ignorada: %s\n", where.c_str(), js.empty() ? "sem stage.json" : ("stage.json invalido: " + err).c_str());
                    continue;
                }
                Stage st;
                const json::Value *id = v.get("id");
                st.id = lower(id && id->type == json::Value::Str ? id->s : s.filename().string());
                if (st.id.empty() || st.id.size() > 8 || !std::isalpha(static_cast<unsigned char>(st.id[0])) ||
                    st.id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789") != std::string::npos) {
                    std::fprintf(stderr, "[mods] fase %s ignorada: \"id\" \"%s\" tem de ter de 1 a 8 letras e digitos, comecando por letra\n", where.c_str(), st.id.c_str());
                    continue;
                }
                if (const char *why = reserved(st.upperId())) {
                    std::fprintf(stderr, "[mods] fase %s ignorada: o id \"%s\" e %s\n", where.c_str(), st.id.c_str(), why);
                    continue;
                }
                const json::Value *base = v.get("base");
                st.base = base ? stageBase(base->type == json::Value::Str ? base->s : std::to_string(base->asInt(0))) : nullptr;
                if (!st.base) {
                    std::fprintf(stderr, "[mods] fase %s ignorada: \"base\" tem de ser CENTRAL, VEGAS, CANYON2, METRO, UFO, CAPITOL, CANYON1, MINISF ou TOKYODM\n", where.c_str());
                    continue;
                }
                const json::Value *nm = v.get("name");
                const std::string raw = nm && nm->type == json::Value::Str && !nm->s.empty() ? nm->s : st.upperId();
                for (unsigned char c : raw)   // a fonte do menu so tem ASCII
                    if (c >= 0x20 && c < 0x7F && st.name.size() < 17) st.name += char(c);
                if (st.name != raw)
                    std::fprintf(stderr, "[mods] fase %s: \"name\" vira \"%s\" (ate 17 caracteres ASCII)\n", where.c_str(), st.name.c_str());
                if (const json::Value *na = v.get("narrator"); na && na->type == json::Value::Str) st.narrator = lower(na->s);
                if (const json::Value *r = v.get("random")) st.random = r->asBool(true);
                st.mod = m.filename().string();
                st.dir = s;
                const auto dup = std::find_if(found.begin(), found.end(), [&](const Stage &o) { return o.id == st.id; });
                if (dup != found.end()) {
                    std::fprintf(stderr, "[mods] fase %s: o id \"%s\" ja e de %s/stages; %s vence\n", where.c_str(), st.id.c_str(), dup->mod.c_str(), st.mod.c_str());
                    *dup = st;
                } else
                    found.push_back(st);
            }
        }
        std::stable_sort(found.begin(), found.end(), [](const Stage &a, const Stage &b) { return key(a.mod) != key(b.mod) ? key(a.mod) < key(b.mod) : a.id < b.id; });
        int slot = kFirstStageSlot;
        for (Stage &st : found) {
            if (slot >= kFirstStageSlot + kStageSlots) {
                std::fprintf(stderr, "[mods] fase %s (%s) ignorada: o jogo tem lugar para %d fases custom\n", st.id.c_str(), st.mod.c_str(), kStageSlots);
                continue;
            }
            const std::string up = st.upperId(), bn = st.base->name;
            std::map<std::string, fs::path> own;   // "PTS/MYCITY.SPT" -> arquivo da pasta
            std::vector<fs::path> pngs;
            std::string refused;
            for (auto it = fs::recursive_directory_iterator(st.dir, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (ec) break;
                if (!it->is_regular_file(ec)) continue;
                const std::string ext = key(it->path().extension().string());
                std::string rel;
                if (ext == ".SPT" || ext == ".PUP" || ext == ".FOP" || ext == ".PED" || ext == ".AIP") rel = "PTS/" + up + ext;
                else if (ext == ".MRS") rel = "RST/" + up + ext;
                else if (ext == ".PNG") pngs.push_back(it->path());
                else if (ext == ".NGP" || ext == ".PTR" || ext == ".TEX" || ext == ".RTX") refused += (refused.empty() ? "" : " ") + ext.substr(1);
                if (rel.empty()) continue;
                fs::path &o = own[rel];
                if (!o.empty()) std::fprintf(stderr, "[mods] fase %s: dois %s, usando %s\n", st.id.c_str(), ext.c_str(), o.filename().string().c_str());
                else o = it->path();
            }
            if (!refused.empty())
                std::fprintf(stderr, "[mods] fase %s: %s proprio(s) ignorado(s): a geometria e as texturas sao as da base %s (PNGs valem)\n", st.id.c_str(), refused.c_str(), bn.c_str());
            st.slot = slot++;
            std::string brought, disc, none;
            const auto alias = [&](const std::string &rel, const std::string &target) {
                m_alias[rel] = target;
                m_discAlias.insert(rel);
                disc += (disc.empty() ? "" : " ") + rel.substr(rel.size() - 3);
            };
            for (const char *ext : {"NGP", "PTR", "TEX", "RTX"}) alias("LVL/" + up + "." + ext, "LVL/" + bn + "." + ext);
            for (const char *ext : {"SPT", "PUP", "FOP", "PED", "AIP", "MRS"}) {
                const std::string rel = (std::strcmp(ext, "MRS") == 0 ? "RST/" : "PTS/") + up + "." + ext;
                if (const auto o = own.find(rel); o != own.end()) {
                    File &f = m_files[rel];
                    f.rel = rel;
                    f.host = o->second;
                    f.mod = st.mod;
                    f.size = fs::file_size(o->second, ec);
                    brought += (brought.empty() ? "" : " ") + std::string(ext);
                    if (std::strcmp(ext, "AIP") == 0) st.ownAip = true;
                } else if (std::strcmp(ext, "MRS") == 0 || std::strstr(st.base->pts, ext))
                    alias(rel, (std::strcmp(ext, "MRS") == 0 ? "RST/" : "PTS/") + bn + "." + ext);
                else
                    none += (none.empty() ? "" : " ") + std::string(ext);
            }
            for (const fs::path &p : pngs) m_stagePngs.push_back(SkinPng{up, p, st.mod, "stages/" + st.dir.filename().string() + "/" + p.filename().string()});
            std::fprintf(stderr, "[mods] fase %s (\"%s\", %s) = vaga %d, base %s (%d); proprios: %s; do disco: %s%s%s; %zu png\n", st.id.c_str(),
                         st.name.c_str(), st.mod.c_str(), st.slot, bn.c_str(), st.base->level, brought.empty() ? "nenhum" : brought.c_str(),
                         disc.c_str(), none.empty() ? "" : "; a base nao tem: ", none.c_str(), pngs.size());
            m_stages.push_back(st);
        }
    }

    // .CHR e .AIC da pasta "files" de um custom (numeros da luta e IA proprios): entram como RST/C_<ID>.CHR e RST/C_<ID><d>.AIC
    // (d = dificuldade 0..2). Um .AIC terminado em 0, 1 ou 2 vale para essa dificuldade; sem numero, para as que nao tem o seu;
    // dificuldade sem nenhum dos dois fica com o de outra. Um .BNK e o banco de som proprio (Custom::bank).
    void scanResetFiles(Custom &c) {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path chr, aicAny, aic[3];
        c.bank.clear();
        c.specialBank.clear();
        c.ambientBank.clear();
        const MonsterInfo *sp = c.special ? monsterInfo(c.special) : nullptr;
        for (auto it = fs::recursive_directory_iterator(c.files, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            const std::string ext = key(it->path().extension().string()), stem = key(it->path().stem().string());
            fs::path *slot = nullptr;
            if (ext == ".CHR") slot = &chr;
            else if (ext == ".BNK")
                slot = stem.rfind("GENAM", 0) == 0 ? &c.ambientBank : sp && stem.rfind(sp->prefix, 0) == 0 ? &c.specialBank : &c.bank;
            else if (ext == ".AIC") slot = !stem.empty() && stem.back() >= '0' && stem.back() <= '2' ? &aic[stem.back() - '0'] : &aicAny;
            if (!slot) continue;
            if (!slot->empty())
                std::fprintf(stderr, "[mods] monstro %s: dois %s, usando %s\n", c.id.c_str(), ext.c_str(), slot->filename().string().c_str());
            else
                *slot = it->path();
        }
        c.resetStem = "C_" + key(c.id);
        const auto add = [&](const std::string &rel, const fs::path &host) {
            File &f = m_files[rel];
            f.rel = rel;
            f.host = host;
            f.mod = c.mod;
            f.size = fs::file_size(host, ec);
        };
        std::string what;
        if (!chr.empty()) {
            add("RST/" + c.resetStem + ".CHR", chr);
            c.ownChr = true;
            what = ".CHR " + chr.filename().string();
        }
        for (int d = 0; d < 3; ++d) {
            const fs::path &p = !aic[d].empty() ? aic[d] : !aicAny.empty() ? aicAny : !aic[1].empty() ? aic[1] : !aic[0].empty() ? aic[0] : aic[2];
            if (p.empty()) continue;
            add("RST/" + c.resetStem + std::to_string(d) + ".AIC", p);
            c.ownAic = true;
            what += std::string(what.empty() ? "" : ", ") + ".AIC " + std::to_string(d) + " " + p.filename().string();
        }
        if (!c.bank.empty())
            what += std::string(what.empty() ? "" : ", ") + "som " + c.bank.filename().string();
        if (!c.specialBank.empty())
            what += std::string(what.empty() ? "" : ", ") + "som do especial " + c.specialBank.filename().string();
        if (!c.ambientBank.empty())
            what += std::string(what.empty() ? "" : ", ") + "banco comum " + c.ambientBank.filename().string();
        if (!what.empty())
            std::fprintf(stderr, "[mods] monstro %s: %s (RST/%s)\n", c.id.c_str(), what.c_str(), c.resetStem.c_str());
    }

    // mods/<mod>/monsters/<pasta>/monster.json: {"id", "name", "base", "costume", "camera": {"from", "scale" (%)}, "narrator", "costumeName", "logo", "ai",
    // "special", ...}.
    // Mesmo id em dois mods: vence o ultimo em ordem alfabetica, como os arquivos.
    void scanCustoms(const std::vector<std::filesystem::path> &mods) {
        namespace fs = std::filesystem;
        std::error_code ec;
        std::map<std::string, Custom> byId;
        for (const fs::path &m : mods) {
            const fs::path dir = child(m, "monsters");
            if (dir.empty() || !fs::is_directory(dir, ec)) continue;
            std::vector<fs::path> subs;
            for (const auto &e : fs::directory_iterator(dir, ec))
                if (e.is_directory(ec)) subs.push_back(e.path());
            std::sort(subs.begin(), subs.end());
            for (const fs::path &s : subs) {
                const std::string where = m.filename().string() + "/monsters/" + s.filename().string();
                const fs::path js = child(s, "monster.json");
                std::string text;
                if (!js.empty()) {
                    std::ifstream in(js, std::ios::binary);
                    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                }
                json::Value v;
                std::string err;
                if (js.empty() || !json::parse(text, v, &err) || v.type != json::Value::Obj) {
                    std::fprintf(stderr, "[mods] monstro %s ignorado: %s\n", where.c_str(), js.empty() ? "sem monster.json" : ("monster.json invalido: " + err).c_str());
                    continue;
                }
                Custom c;
                const json::Value *base = v.get("base");
                const MonsterInfo *mi = base && base->type == json::Value::Str ? customBase(lower(base->s)) : nullptr;
                if (!mi) {
                    std::fprintf(stderr, "[mods] monstro %s ignorado: \"base\" nao e um tipo aceito (togera, congar, ..., assboss, jelly, final)\n", where.c_str());
                    continue;
                }
                c.type = mi->type;
                c.typeName = mi->name;
                c.prefix = mi->prefix;
                const json::Value *id = v.get("id");
                c.id = lower(id && id->type == json::Value::Str ? id->s : s.filename().string());
                if (c.id.empty() || c.id.size() > 32 || c.id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos ||
                    std::isdigit(static_cast<unsigned char>(c.id[0]))) {
                    std::fprintf(stderr, "[mods] monstro %s ignorado: \"id\" so com letras, digitos e _ (ate 32, comeca com letra)\n", where.c_str());
                    continue;
                }
                const json::Value *nm = v.get("name");
                c.name = nm && nm->type == json::Value::Str && !nm->s.empty() ? nm->s : c.id;
                const json::Value *co = v.get("costume");
                c.costume = co ? int(co->asInt(0)) : 0;
                if (c.costume < 0 || c.costume > 3) c.costume = 0;
                c.diskCostume = c.costume;
                // "files": subpasta com .NGP+.PTR(+.TEX/.RTX) proprios (true = "files"); scanSkins a registra como traje de mod
                if (const json::Value *fi = v.get("files"); fi && (fi->type == json::Value::Str ? !fi->s.empty() : fi->asBool(false))) {
                    const fs::path p = child(s, fi->type == json::Value::Str ? fi->s : std::string("files"));
                    if (p.empty() || !fs::is_directory(p, ec))
                        std::fprintf(stderr, "[mods] monstro %s: pasta de \"files\" nao achada; usando o traje %d do disco\n", where.c_str(), c.costume);
                    else
                        c.files = p;
                }
                if (const json::Value *cam = v.get("camera"); cam && cam->type == json::Value::Obj) {
                    if (const json::Value *f = cam->get("from"); f && f->type == json::Value::Str)
                        if (const MonsterInfo *b = customBase(lower(f->s))) c.cameraFrom = kLongNames[b->type];
                    if (const json::Value *sc = cam->get("scale"); sc && sc->type == json::Value::Int && sc->i >= 10 && sc->i <= 1000)   // em %
                        c.cameraScale = float(sc->i) / 100.f;
                }
                if (const json::Value *na = v.get("narrator"); na && na->type == json::Value::Str) c.narrator = lower(na->s);
                if (const json::Value *cn = v.get("costumeName"); cn && cn->type == json::Value::Str) c.costumeName = cn->s;
                c.fireColor = color(v, "fireColor", "monstro " + where);
                c.shotColor = color(v, "shotColor", "monstro " + where);
                if (const json::Value *ki = v.get("kind"); ki && ki->type == json::Value::Str) c.kind = lower(ki->s);
                if (const json::Value *sr = v.get("specialRoar")) c.specialRoar = sr->asBool(false);
                if (const json::Value *fb = v.get("fallBlend"); fb && fb->type == json::Value::Int && fb->i >= 0 && fb->i <= 120)
                    c.fallBlend = int(fb->i);
                if (const json::Value *lt = v.get("landOnTouch")) c.landOnTouch = lt->asBool(false);
                if (const json::Value *rt = v.get("rootTurn"); rt && rt->type == json::Value::Obj)
                    for (const auto &kv : rt->obj) {
                        const int id = std::atoi(kv.first.c_str());
                        if (id <= 0 || id >= 300 || kv.second.type != json::Value::Arr || kv.second.arr.size() < 2) {
                            std::fprintf(stderr, "[mods] monstro %s: \"rootTurn\" %s ignorado (animacao 1..299 e ao menos 2 quadros)\n", where.c_str(), kv.first.c_str());
                            continue;
                        }
                        std::vector<float> &t = c.rootTurn[id];
                        for (const json::Value &x : kv.second.arr) t.push_back(float(x.asNum(0.0)));
                    }
                if (const json::Value *sp = v.get("special"); sp && sp->type == json::Value::Str) {
                    const MonsterInfo *b = customBase(lower(sp->s));
                    const std::vector<uint32_t> parts = b ? specialParts(b->type) : std::vector<uint32_t>{};
                    if (b && b->type != c.type && *specialNeeds(b->type) && !parts.empty() && !c.files.empty() && filesHaveIds(c.files, parts)) {
                        c.special = b->type;
                        std::fprintf(stderr, "[mods] monstro %s: especial do %s com as pecas dos arquivos proprios\n", where.c_str(), b->name);
                    } else if (b && b->type != c.type && *specialNeeds(b->type))
                        std::fprintf(stderr, "[mods] monstro %s: o especial do %s usa %s, que so o modelo dele traz%s; fica o do tipo\n",
                                     where.c_str(), b->name, specialNeeds(b->type),
                                     parts.empty() ? "" : " (copia nos arquivos proprios: tools/ngpgraft.py)");
                    else if (b && b->type != c.type)
                        c.special = b->type;
                    else if (!b)
                        std::fprintf(stderr, "[mods] monstro %s: \"special\" %s nao e um monstro (togera, congar, ..., assboss, jelly, final); fica o do tipo\n",
                                     where.c_str(), sp->s.c_str());
                }
                // "portrait": PNG do retrato do HUD (na pasta do monstro ou na raiz do mod), no lugar da imagem <PREFIXO><traje>.RTX
                // de texId "portraitTex" (padrao: o do ASSBOSS1, 6; a ultima entrada do .RTX, 128x64)
                if (const json::Value *po = v.get("portrait"); po && po->type == json::Value::Str && !po->s.empty()) {
                    fs::path p = s / pathFromUtf8(po->s);
                    if (!fs::is_regular_file(p, ec)) p = m / pathFromUtf8(po->s);
                    const json::Value *pt = v.get("portraitTex");
                    c.portraitTex = pt && pt->type == json::Value::Int ? int(pt->i) : (c.type == 13 && c.costume == 1 ? 6 : -1);
                    if (!fs::is_regular_file(p, ec))
                        std::fprintf(stderr, "[mods] monstro %s: \"portrait\" %s nao achado\n", where.c_str(), po->s.c_str());
                    else if (c.portraitTex < 0)
                        std::fprintf(stderr, "[mods] monstro %s: \"portrait\" sem \"portraitTex\" (texId do retrato no .RTX)\n", where.c_str());
                    else
                        c.portrait = p;
                }
                // "logo": PNG 2:1 com o nome desenhado (como os 256x128 do SHELL.TEX), na pasta do monstro ou na raiz do mod
                if (const json::Value *lo = v.get("logo"); lo && lo->type == json::Value::Str && !lo->s.empty()) {
                    fs::path p = s / fs::u8path(lo->s);
                    if (!fs::is_regular_file(p, ec)) p = m / fs::u8path(lo->s);
                    if (fs::is_regular_file(p, ec))
                        c.logo = p;
                    else
                        std::fprintf(stderr, "[mods] monstro %s: \"logo\" %s nao achado\n", where.c_str(), lo->s.c_str());
                }
                const json::Value *ai = v.get("ai");
                c.ai = ai && ai->asBool(false);
                c.mod = m.filename().string();
                c.dir = s;
                Custom &slot = byId[c.id];
                if (!slot.id.empty())
                    std::fprintf(stderr, "[mods] monstro %s: %s vence %s\n", c.id.c_str(), c.mod.c_str(), slot.mod.c_str());
                slot = c;
            }
        }
        for (auto &[id, c] : byId) {
            std::fprintf(stderr, "[mods] monstro custom %s (\"%s\", %s): base %s traje %d%s%s\n", c.id.c_str(), c.name.c_str(), c.mod.c_str(),
                         c.typeName.c_str(), c.costume, c.cameraFrom.empty() ? "" : ", camera do ", c.cameraFrom.c_str());
            m_customs.push_back(c);
        }
    }
    // MonsterLongNames (prefixo do .CHR em RST/ e dos arquivos em MON/, minusculas), indice = tipo
    static constexpr const char *kLongNames[] = {"null", "monkey", "robot", "lizard", "shogun", "mantis", "spider", "energy", "lava",
                                                 "dragon", "rock", "alien", "ogre", "assboss", "jelly", "final", nullptr};
};

} // namespace wotm::mods
