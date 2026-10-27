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
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "wotm_json.hpp"

namespace wotm::mods {

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
    std::string kind;           // regras proprias do port ("kind": "mecha" = escudo e laser, wotm_custom.inc); vazio = as do tipo
    std::string cameraFrom;     // MonsterLongNames do .CHR da camera ("lizard"); vazio = a do proprio tipo
    float cameraScale = 1.f;    // distancias da camera emprestada
    std::string narrator;       // narrator/<nome>.mp3 (vazio = o do tipo)
    bool ai = false;            // uma CPU pode usar (quadro de CPU da selecao)
    std::filesystem::path portrait;   // PNG do retrato do HUD (vazio = o do .RTX)
    int portraitTex = -1;       // texId do retrato no <PREFIXO><traje>.RTX
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
    int index = 0;              // traje no jogo (4..99), so nesta execucao
    bool own = false;           // os arquivos proprios de um custom ("files"): e o traje padrao dele, nao uma skin da lista
    std::string mod;
    std::filesystem::path dir;
};
// PNG de uma skin: vale so para o arquivo do traje dela (stem "LIZARD4"), nunca para o traje base
struct SkinPng { std::string stem; std::filesystem::path host; std::string mod, rel; };

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

    // Le a pasta mods/ (uma vez). Devolve quantos arquivos entraram.
    size_t scan(const std::filesystem::path &dir) {
        namespace fs = std::filesystem;
        std::error_code ec;
        m_dir = dir;
        if (dir.empty() || !fs::is_directory(dir, ec)) { m_ready = true; return 0; }
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
        }
        // Faixas de setores na ordem dos nomes (estavel entre execucoes), com um setor de folga entre arquivos
        uint32_t next = kFirstLsn;
        for (auto &[rel, f] : m_files) {
            f.sectors = uint32_t((f.size + 2047) / 2048);
            f.lsn = next;
            next += std::max(f.sectors, 1u) + 1;
            m_byLsn.push_back(&f);
        }
        m_ready = true;
        return m_files.size() + m_alias.size();
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
    std::string hostPath(const std::string &rel) const {
        const File *f = find(aliasOf(rel));
        return f ? f->host.string() : std::string{};
    }

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
    std::vector<Skin> m_skins;
    std::vector<SkinPng> m_skinPngs;
    std::vector<Custom> m_customs;
    std::atomic<bool> m_ready{false};

    static std::string lower(std::string s) {
        for (char &c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
    // Filho de `dir` com esse nome, sem olhar maiusculas
    static std::filesystem::path child(const std::filesystem::path &dir, const std::string &name) {
        std::error_code ec;
        for (const auto &e : std::filesystem::directory_iterator(dir, ec))
            if (key(e.path().filename().string()) == key(name)) return e.path();
        return {};
    }

    // mods/<mod>/skins/<pasta>/skin.json (MODS.md, "Skins"): {"monster", "id", "name", "base", "ai"}. Cada skin vira o traje
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

    // mods/<mod>/monsters/<pasta>/monster.json: {"id", "name", "base", "costume", "camera": {"from", "scale" (%)}, "narrator", "ai"}.
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
                if (const json::Value *ki = v.get("kind"); ki && ki->type == json::Value::Str) c.kind = lower(ki->s);
                // "portrait": PNG do retrato do HUD (na pasta do monstro ou na raiz do mod), no lugar da imagem <PREFIXO><traje>.RTX
                // de texId "portraitTex" (padrao: o do ASSBOSS1, 6; a ultima entrada do .RTX, 128x64)
                if (const json::Value *po = v.get("portrait"); po && po->type == json::Value::Str && !po->s.empty()) {
                    fs::path p = s / fs::u8path(po->s);
                    if (!fs::is_regular_file(p, ec)) p = m / fs::u8path(po->s);
                    const json::Value *pt = v.get("portraitTex");
                    c.portraitTex = pt && pt->type == json::Value::Int ? int(pt->i) : (c.type == 13 && c.costume == 1 ? 6 : -1);
                    if (!fs::is_regular_file(p, ec))
                        std::fprintf(stderr, "[mods] monstro %s: \"portrait\" %s nao achado\n", where.c_str(), po->s.c_str());
                    else if (c.portraitTex < 0)
                        std::fprintf(stderr, "[mods] monstro %s: \"portrait\" sem \"portraitTex\" (texId do retrato no .RTX)\n", where.c_str());
                    else
                        c.portrait = p;
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
