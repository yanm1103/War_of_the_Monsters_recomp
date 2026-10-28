// Save em JSON (NATIVO.md, "Save em JSON"). Incluido pelo wotm_overrides.cpp.
//
// O jogo continua gravando e lendo os blocos binarios do memory card pelo stub do runtime (mc0/BASCUS-97197: as opcoes, 0x380
// bytes, e o savegame.dat, a aventura, 0x280; o cartao 2 em mc1/). Um JSON so ao lado da pasta do cartao (save.json) passa a ser a
// copia que vale. As opcoes e os destraves sao os mesmos nos dois cartoes; "saves" tem 20 slots, 0..9 no cartao 1 e 10..19 no 2.
//   - na entrada do MonsterMc::stdInit (comeco de toda sequencia de paginas do cartao: boot, opcoes, destrave, aventura) os
//     binarios dos dois cartoes sao refeitos a partir do JSON;
//   - na entrada do MonsterMc::stdExit (fim da sequencia), se o jogo gravou algum binario, o JSON e atualizado a partir deles.
// Sem JSON e com binarios (save antigo), o primeiro stdInit cria o JSON; se o JSON nao tem os slots de um cartao que tem aventuras
// gravadas, elas sao importadas em vez de apagadas. Chaves que o codigo nao conhece ficam no arquivo.
// WOTM_SAVEJSON=0 desliga (so o mc0/, como antes). Layout dos blocos em docs/FORMATOS.md, "Save do memory card"; tools/mcsave.py mostra o mesmo JSON.

#include "wotm_json.hpp"
#include "wotm_mods.hpp"   // skins de mod: traje do slot por nome
#define WOTM_SAVEPICS_NO_DRAW
#include "wotm_savepics.hpp"   // logo do monstro e foto da fase a esquerda da lista de saves (a janela desenha)

#include <algorithm>
#include <atomic>
#include <cmath>
#include <initializer_list>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// Cada gravacao do save.json soma 1 (thread do EE); o icone de salvamento (native/wotm_saveicon.hpp, thread da janela) le.
std::atomic<uint32_t> g_wotmSaveCount{0};

namespace wotm_save
{
    namespace fs = std::filesystem;
    using wotm::json::Value;

    constexpr uint32_t kMagic = 0x12345678u;
    constexpr size_t kSettingsSize = 0x380, kSettingsMagic = 0x34C;
    constexpr size_t kAdvSize = 0x280, kAdvMagic = 0x268, kAdvSlot0 = 0x10, kAdvStride = 0x3C;
    constexpr int kAdvSlots = 10;

    // Grupos do monsterSelectMode (12 x 4 SelectSlot). 0..9: monstros na ordem do aiMonsters de Shell::RandomlySelectAI, a entrada
    // j e o traje j. 10 e 11: fases do levelUnlocked (a entrada 47 sobra).
    const char *const kGroupMonsters[10] = {"raptros", "togera", "preytor", "congar", "robo-47", "agamo", "ultra-v", "magmo", "kineticlops", "zorgulon"};
    constexpr int kSweetToothSlot = 23;   // "Agamo Costume 4", ligado pelo McCheckForSweetTooth
    struct LevelSlot { int slot; const char *name; };
    const char *const kDifficultyNames[3] = {"easy", "medium", "hard"};   // options.difficulty (TheGame+0x1203CC)
    const LevelSlot kLevelSlots[] = {{40, "volcano"}, {41, "minisf"}, {42, "capitol"}, {43, "ufo"}, {44, "dball"}, {45, "bigshot"}, {46, "crush"}};
    // MonsterNewNames (indice em m_monsterSel >> 5) e GameLevelNames (m_levelNum).
    const char *const kMonsters[] = {"null", "congar", "robo-47", "togera", "ultra-v", "preytor", "spider", "kineticlops", "magmo", "raptros",
                                     "agamo", "zorgulon", "ogre", "assboss", "jelly", "final", "temp16"};
    const char *const kLevels[] = {"default", "central", "vegas", "canyon2", "metro", "airport", "3-Mile", "sanfran", "island", "tokyo",
                                   "ufo", "capitol", "training", "canyon1", "minisf", "volcano", "tokyodm", "dylan", "david", "lars",
                                   "jenn", "yeon", "jacob", "steve", "pierre", "dball", "bigshot", "crush"};

    template <size_t N> Value nameOf(const char *const (&t)[N], uint32_t i)
    {
        return i < N ? Value::string(t[i]) : Value::integer(i);
    }
    template <size_t N> uint32_t indexOf(const char *const (&t)[N], const Value *v, uint32_t def)
    {
        if (!v)
            return def;
        if (v->type == Value::Str)
        {
            for (size_t k = 0; k < N; ++k)
                if (v->s == t[k])
                    return static_cast<uint32_t>(k);
            return def;
        }
        return static_cast<uint32_t>(v->asInt(def));
    }

    uint32_t rd(const std::vector<uint8_t> &b, size_t o) { return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (uint32_t(b[o + 3]) << 24); }
    void wr(std::vector<uint8_t> &b, size_t o, uint32_t v)
    {
        b[o] = v & 0xFF, b[o + 1] = (v >> 8) & 0xFF, b[o + 2] = (v >> 16) & 0xFF, b[o + 3] = v >> 24;
    }
    // Campo inteiro do JSON, ou o que ja estava no bloco.
    void wrField(std::vector<uint8_t> &b, size_t o, const Value *v)
    {
        if (v && v->isInt())
            wr(b, o, static_cast<uint32_t>(v->asInt()));
    }

    bool readFile(const fs::path &p, std::vector<uint8_t> &out)
    {
        std::ifstream f(p, std::ios::binary);
        if (!f)
            return false;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return true;
    }
    bool writeFile(const fs::path &p, const void *data, size_t n)
    {
        // Grava num temporario e troca: um JSON pela metade nunca fica no lugar do bom.
        const fs::path tmp = p.string() + ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f)
                return false;
            f.write(static_cast<const char *>(data), static_cast<std::streamsize>(n));
            if (!f)
                return false;
        }
        std::error_code ec;
        fs::rename(tmp, p, ec);
        if (ec)
        {
            fs::remove(tmp, ec);
            return false;
        }
        return true;
    }

    // ---- bloco -> JSON (sobre o JSON que ja existe, para manter o que nao e nosso) ----

    void settingsToJson(const std::vector<uint8_t> &b, Value &j)
    {
        Value &opt = j.at("options");
        opt.at("difficulty") = Value::integer(rd(b, 0x00));    // TheGame+0x1203CC (m_matchMode)
        opt.at("sfxVolume") = Value::integer(rd(b, 0x04));     // Shell+0x2A30, 0..0x400
        opt.at("musicVolume") = Value::integer(rd(b, 0x08));   // Shell+0x2A34
        opt.at("mono") = Value::boolean(rd(b, 0x10) != 0);     // Shell+0x2C20
        Value ctl = Value::array(), vib = Value::array();       // Shell+0x2B8C, Shell+0x2B94
        ctl.arr = {Value::integer(rd(b, 0x14)), Value::integer(rd(b, 0x18))};
        vib.arr = {Value::integer(rd(b, 0x1C)), Value::integer(rd(b, 0x20))};
        opt.at("controllerConfig") = ctl;
        opt.at("vibration") = vib;
        j.at("battleTokens") = Value::integer(rd(b, 0x24));    // Shell+0x2B24

        Value &un = j.at("unlocks");
        Value &mons = un.at("monsters");
        for (int g = 0; g < 10; ++g)
        {
            Value skins = Value::array();
            for (int k = 0; k < 4; ++k)
                skins.arr.push_back(Value::boolean(rd(b, 0x28 + (g * 4 + k) * 4) != 0));
            mons.at(kGroupMonsters[g]).at("skins") = skins;
        }
        Value &lv = un.at("levels");
        for (const auto &l : kLevelSlots)
            lv.at(l.name) = Value::boolean(rd(b, 0x28 + l.slot * 4) != 0);
        // Mecha Sweet Tooth (Agamo, traje 4): no PS2 vinha do save do Twisted Metal: Black no cartao (McCheckForSweetTooth). Aqui e
        // uma chave do usuario; so e criada, com o estado atual, quando falta.
        if (!un.get("mechaSweetTooth"))
            un.at("mechaSweetTooth") = Value::boolean(rd(b, 0x28 + kSweetToothSlot * 4) != 0);

        // Recorde do endurance (monstros derrotados pelo jogador 1), um por dificuldade: TheGame+0x1204B0..B8. O jogo grava e nunca
        // mostra (8498ce0:docs/investigacoes/2026-10-24/save_campos_abertos.md). Antes ficava em unknown.game1204B0, que sai na reescrita.
        Value &end = j.at("records").at("enduranceBest");
        for (int k = 0; k < 3; ++k)
            end.at(kDifficultyNames[k]) = Value::integer(rd(b, 0xE8 + k * 4));

        // Campos que o jogo grava e ninguem usa: guardados como estao (0x0C: opcao cortada do menu de opcoes, 50 no construtor).
        Value &unk = j.at("unknown");
        unk.at("shell2A40") = Value::integer(rd(b, 0x0C));
        unk.at("slot47") = Value::integer(rd(b, 0x28 + 47 * 4));
        auto &uo = unk.obj;
        for (size_t k = 0; k < uo.size(); ++k)
            if (uo[k].first == "game1204B0")
            {
                uo.erase(uo.begin() + static_cast<std::ptrdiff_t>(k));
                break;
            }
    }

    bool isNull(const Value &v) { return v.type == Value::Null; }
    uint32_t savesIntOf(const Value &s, const char *k)
    {
        const Value *v = s.get(k);
        return v && v->isInt() ? static_cast<uint32_t>(v->asInt()) : 0u;
    }

    // ---- traje do slot da aventura (MODS.md, "Skins"; NATIVO.md, "Save em JSON") ----
    // Trajes do disco (0..3) ficam como inteiro; skin de mod, como "monstro/id" ("togera/green"). O indice de uma skin (4, 5...) so
    // vale nesta execucao (wotm_mods.hpp) e nunca vai para o JSON. Um indice que nao existe nesta sessao nunca vai para o binario nem
    // para o m_costume (o jogo pediria um MON/<PREFIXO>N.* que nao existe e relocaria com lixo): sem a skin, traje 0.
    struct Costume
    {
        uint32_t index = 0;
        std::string skin;   // "togera/green" quando o slot traz uma skin de mod (achada ou nao)
        bool missing = false;
    };
    Costume resolveCostume(uint32_t monster, const Value *v, bool log)
    {
        Costume c;
        if (!v)
            return c;
        const wotm::mods::Registry &reg = wotm::mods::Registry::get();
        if (v->type == Value::Str)
        {
            c.skin = v->s;
            if (const wotm::mods::Skin *k = reg.skin(static_cast<int>(monster), v->s))
                c.index = static_cast<uint32_t>(k->index);
            else
                c.missing = true;
        }
        else if (v->isInt())
        {
            const int64_t n = v->asInt();
            if (n >= 0 && n <= 3)
                c.index = static_cast<uint32_t>(n);
            else if (const wotm::mods::Skin *k = reg.skinByIndex(static_cast<int>(monster), static_cast<int>(n)))
                c.index = static_cast<uint32_t>(n), c.skin = k->monsterName + "/" + k->id;   // save de antes do nome no JSON
            else
                c.missing = true;
        }
        if (c.missing && log)
            std::fprintf(stderr, "[wotm] save: traje %s nao achado (mod ausente?); usando traje 0\n",
                         c.skin.empty() ? std::to_string(v->asInt()).c_str() : c.skin.c_str());
        return c;
    }
    // Indice do jogo -> valor do JSON
    Value costumeValue(uint32_t monster, uint32_t index)
    {
        if (index <= 3)
            return Value::integer(index);
        if (const wotm::mods::Skin *k = wotm::mods::Registry::get().skinByIndex(static_cast<int>(monster), static_cast<int>(index)))
            return Value::string(k->monsterName + "/" + k->id);
        return Value::integer(0);
    }
    // Traje do disco que da o modelo de reserva (e o enquadramento) do monstro 3D da lista: o base da skin
    uint32_t modelBase(uint32_t monster, uint32_t index)
    {
        if (index <= 3)
            return index;
        const wotm::mods::Skin *k = wotm::mods::Registry::get().skinByIndex(static_cast<int>(monster), static_cast<int>(index));
        return k ? static_cast<uint32_t>(k->base) & 3u : 0u;
    }
    // Slot carregado com uma skin que nao existe nesta sessao (jogado com o traje 0): ao gravar a mesma aventura, o nome volta ao
    // JSON no lugar do 0 (o mod pode voltar). Esquecido quando uma tela de selecao de monstro abre (poll) ou outro slot carrega.
    struct MissingSkin { bool valid = false; uint32_t monster = 0; std::string skin; };
    MissingSkin g_missing;
    Value savedCostume(uint32_t monster, uint32_t index)
    {
        if (index == 0 && g_missing.valid && g_missing.monster == monster)
            return Value::string(g_missing.skin);
        return costumeValue(monster, index);
    }

    // Slots do cartao `card` (10 a partir de card * 10) em j.saves; o array termina no ultimo slot nao nulo. Um slot do JSON que ja
    // diz o mesmo que o binario fica como esta (com as chaves que nao sao nossas e o nome da skin).
    void adventureToJson(const std::vector<uint8_t> &b, Value &j, int card)
    {
        Value &saves = j.at("saves");
        if (saves.type != Value::Arr)
            saves = Value::array();
        const size_t base = static_cast<size_t>(card) * kAdvSlots;
        if (saves.arr.size() < base + kAdvSlots)
            saves.arr.resize(base + kAdvSlots);
        const uint32_t n = std::min<uint32_t>(rd(b, 0), kAdvSlots);
        for (uint32_t i = 0; i < kAdvSlots; ++i)
        {
            Value &dst = saves.arr[base + i];
            const size_t o = kAdvSlot0 + i * kAdvStride;
            const uint32_t lvl = i < n ? rd(b, o) : 0, sel = i < n ? rd(b, o + 4) : 0, lives = i < n ? rd(b, o + 8) : 0,
                           score = i < n ? rd(b, o + 12) : 0, cos = i < n ? rd(b, o + 16) : 0;
            if (i >= n || (!lvl && !sel && !lives && !score && !cos))
            {
                dst = Value();   // slot alem do contador ou nunca gravado: null
                continue;
            }
            if (dst.type == Value::Obj)
            {
                const Value *var = dst.get("variant");
                const uint32_t mon = indexOf(kMonsters, dst.get("monster"), 0);
                if (indexOf(kLevels, dst.get("level"), 0) == lvl &&
                    ((mon << 5) | (var ? static_cast<uint32_t>(var->asInt()) & 31 : 0)) == sel && savesIntOf(dst, "lives") == lives &&
                    savesIntOf(dst, "score") == score && resolveCostume(mon, dst.get("costume"), false).index == cos)
                    continue;   // o jogo regravou o mesmo slot
            }
            dst = Value::object();
            dst.at("level") = nameOf(kLevels, lvl);            // Shell+0x2BB0
            dst.at("monster") = nameOf(kMonsters, sel >> 5);   // Shell+0x2938 (m_monsterSel[0])
            dst.at("variant") = Value::integer(sel & 31);
            dst.at("lives") = Value::integer(lives);           // Shell+0x2BBC
            dst.at("score") = Value::integer(score);           // Shell+0x2B28 + 0x2B2C
            dst.at("costume") = savedCostume(sel >> 5, cos);   // Shell+0x2BE0
        }
        while (!saves.arr.empty() && isNull(saves.arr.back()))
            saves.arr.pop_back();
    }

    // ---- JSON -> bloco (sobre o bloco que ja existe; o que o JSON nao diz fica como estava) ----

    void jsonToSettings(const Value &j, std::vector<uint8_t> &b)
    {
        if (b.size() != kSettingsSize)
            b.assign(kSettingsSize, 0);
        // 0xF4..0x34B e depois da marca: o jogo grava ali lixo da pilha e nunca le. Vai zero.
        std::fill(b.begin() + 0xF4, b.end(), 0);
        if (const Value *opt = j.get("options"))
        {
            wrField(b, 0x00, opt->get("difficulty"));
            wrField(b, 0x04, opt->get("sfxVolume"));
            wrField(b, 0x08, opt->get("musicVolume"));
            wrField(b, 0x10, opt->get("mono"));
            if (const Value *c = opt->get("controllerConfig"))
                for (size_t k = 0; k < 2 && k < c->arr.size(); ++k)
                    wrField(b, 0x14 + k * 4, &c->arr[k]);
            if (const Value *v = opt->get("vibration"))
                for (size_t k = 0; k < 2 && k < v->arr.size(); ++k)
                    wrField(b, 0x1C + k * 4, &v->arr[k]);
        }
        wrField(b, 0x24, j.get("battleTokens"));
        if (const Value *un = j.get("unlocks"))
        {
            if (const Value *mons = un->get("monsters"))
                for (int g = 0; g < 10; ++g)
                    if (const Value *m = mons->get(kGroupMonsters[g]))
                        if (const Value *sk = m->get("skins"))
                            for (size_t k = 0; k < 4 && k < sk->arr.size(); ++k)
                                wr(b, 0x28 + (g * 4 + k) * 4, sk->arr[k].asBool() ? 1u : 0u);
            if (const Value *lv = un->get("levels"))
                for (const auto &l : kLevelSlots)
                    if (const Value *v = lv->get(l.name))
                        wr(b, 0x28 + l.slot * 4, v->asBool() ? 1u : 0u);
            // Vale mais que agamo.skins[3]: e a chave que o usuario liga e desliga.
            if (const Value *st = un->get("mechaSweetTooth"))
                wr(b, 0x28 + kSweetToothSlot * 4, st->asBool() ? 1u : 0u);
        }
        if (const Value *unk = j.get("unknown"))
        {
            wrField(b, 0x0C, unk->get("shell2A40"));
            wrField(b, 0x28 + 47 * 4, unk->get("slot47"));
            if (const Value *g4 = unk->get("game1204B0"))   // nome antigo do recorde do endurance
                for (size_t k = 0; k < 3 && k < g4->arr.size(); ++k)
                    wrField(b, 0xE8 + k * 4, &g4->arr[k]);
        }
        if (const Value *rec = j.get("records"))
            if (const Value *end = rec->get("enduranceBest"))
                for (int k = 0; k < 3; ++k)
                    wrField(b, 0xE8 + k * 4, end->get(kDifficultyNames[k]));
        wr(b, kSettingsMagic, kMagic);
    }

    // Slots do cartao `card` no bloco; o contador e o ultimo slot nao nulo + 1, como o jogo deixa.
    void jsonToAdventure(const Value &j, std::vector<uint8_t> &b, int card)
    {
        b.assign(kAdvSize, 0);
        const Value *saves = j.get("saves");
        const size_t base = static_cast<size_t>(card) * kAdvSlots;
        size_t n = 0;
        if (saves && saves->type == Value::Arr)
            for (size_t i = 0; i < kAdvSlots && base + i < saves->arr.size(); ++i)
                if (saves->arr[base + i].type == Value::Obj)
                    n = i + 1;
        wr(b, 0, static_cast<uint32_t>(n));
        for (size_t i = 0; i < n; ++i)
        {
            const Value &s = saves->arr[base + i];
            if (s.type != Value::Obj)
                continue;
            const size_t o = kAdvSlot0 + i * kAdvStride;
            wr(b, o, indexOf(kLevels, s.get("level"), 0));
            const Value *var = s.get("variant");
            const uint32_t mon = indexOf(kMonsters, s.get("monster"), 0);
            wr(b, o + 4, (mon << 5) | (var ? static_cast<uint32_t>(var->asInt()) & 31 : 0));
            wrField(b, o + 8, s.get("lives"));
            wrField(b, o + 12, s.get("score"));
            wr(b, o + 16, resolveCostume(mon, s.get("costume"), true).index);   // skin de mod ausente: traje 0
        }
        wr(b, kAdvMagic, kMagic);
    }

    // ---- arquivos ----

    struct Stamp { bool exists = false; fs::file_time_type t{}; uintmax_t size = 0; };
    bool operator!=(const Stamp &a, const Stamp &b) { return a.exists != b.exists || a.t != b.t || a.size != b.size; }
    Stamp stampOf(const fs::path &p)
    {
        Stamp s;
        std::error_code ec;
        s.t = fs::last_write_time(p, ec);
        if (ec)
            return Stamp{};
        s.size = fs::file_size(p, ec);
        s.exists = !ec;
        return s;
    }

    struct Card
    {
        fs::path dir;
        Stamp settings, adventure;   // como ficaram os binarios depois do ultimo stdInit
    };
    Card g_cards[2];
    fs::path g_json;
    bool g_badJson = false;   // o JSON nao abriu: nao mexer nos binarios; antes de reescreve-lo, guardar o do usuario em .bad
    bool g_on = false;

    // Mesma regra do stub (Kernel/Stubs/MemoryCard.cpp, getMcRootPath): mcRoot, ou <pasta do ELF>/mc0; a porta 1 e o mc1 ao lado.
    void initPaths()
    {
        const PS2Runtime::IoPaths &io = PS2Runtime::getIoPaths();
        fs::path root = io.mcRoot;
        if (root.empty())
            root = !io.elfDirectory.empty() ? io.elfDirectory / "mc0" : fs::current_path() / "mc0";
        root = root.lexically_normal();
        const fs::path parent = root.parent_path();
        g_cards[0].dir = root / "BASCUS-97197";
        g_cards[1].dir = parent / "mc1" / "BASCUS-97197";
        g_json = parent / "save.json";
    }

    bool loadJson(Value &j)
    {
        std::vector<uint8_t> raw;
        if (!readFile(g_json, raw))
            return false;
        std::string err;
        if (!wotm::json::parse(std::string(raw.begin(), raw.end()), j, &err) || j.type != Value::Obj)
        {
            if (!g_badJson)
                std::fprintf(stderr, "[wotm] save: %s nao abriu (%s); o jogo usa o cartao como esta\n", g_json.string().c_str(),
                             err.empty() ? "nao e um objeto" : err.c_str());
            g_badJson = true;
            return false;
        }
        g_badJson = false;
        return true;
    }

    void saveJson(const Value &j, const char *why)
    {
        const std::string text = wotm::json::dump(j);
        if (writeFile(g_json, text.data(), text.size()))
            std::fprintf(stderr, "[wotm] save: %s atualizado (%s)\n", g_json.string().c_str(), why);
        else
            std::fprintf(stderr, "[wotm] save: nao consegui gravar %s\n", g_json.string().c_str());
    }

    bool readSettings(const Card &c, std::vector<uint8_t> &s)
    {
        return readFile(c.dir / "BASCUS-97197", s) && s.size() == kSettingsSize && rd(s, kSettingsMagic) == kMagic;
    }
    bool readAdventure(const Card &c, std::vector<uint8_t> &a)
    {
        return readFile(c.dir / "savegame.dat", a) && a.size() == kAdvSize && rd(a, kAdvMagic) == kMagic;
    }
    bool hasSlots(const std::vector<uint8_t> &a) { return rd(a, 0) != 0; }

    void restamp()
    {
        for (Card &c : g_cards)
        {
            c.settings = stampOf(c.dir / "BASCUS-97197");
            c.adventure = stampOf(c.dir / "savegame.dat");
        }
    }

    // Binarios dos cartoes em `cards` (bits) -> JSON. Com o JSON quebrado, guarda-o como .bad antes de escrever o novo.
    void binToJson(unsigned cards, const char *why)
    {
        Value j;
        std::error_code ec;
        if (fs::exists(g_json, ec) && !loadJson(j))
        {
            fs::copy_file(g_json, g_json.string() + ".bad", fs::copy_options::overwrite_existing, ec);
            std::fprintf(stderr, "[wotm] save: o JSON quebrado ficou em %s.bad\n", g_json.string().c_str());
            j = Value();
            g_badJson = false;
        }
        if (j.type != Value::Obj)
            j = Value::object();
        if (!j.get("format"))
            j.at("format") = Value::integer(1);
        bool any = false;
        // Opcoes: as do cartao que mudou (as dos dois sao as mesmas; com os dois, vale o 1)
        for (int c = 1; c >= 0; --c)
        {
            std::vector<uint8_t> s;
            if ((cards >> c & 1) && readSettings(g_cards[c], s))
                settingsToJson(s, j), any = true;
        }
        for (int c = 0; c < 2; ++c)
        {
            std::vector<uint8_t> a;
            if ((cards >> c & 1) && readAdventure(g_cards[c], a))
                adventureToJson(a, j, c), any = true;
        }
        if (any)
            saveJson(j, why);
    }

    // JSON -> binarios dos dois cartoes, antes de o jogo abrir o cartao.
    void beforeMc()
    {
        std::error_code ec;
        if (!fs::exists(g_json, ec))
        {
            binToJson(3, "criado a partir dos cartoes");
            restamp();
            return;
        }
        Value j;
        if (!loadJson(j))
        {
            restamp();
            return;
        }
        const bool hasSettings = j.get("options") || j.get("unlocks") || j.get("battleTokens");
        bool imported = false;
        // JSON de antes da chave mechaSweetTooth: cria com o estado do cartao 1
        if (const Value *un = j.get("unlocks"); un && !un->get("mechaSweetTooth"))
        {
            std::vector<uint8_t> s;
            j.at("unlocks").at("mechaSweetTooth") = Value::boolean(readSettings(g_cards[0], s) && rd(s, 0x28 + kSweetToothSlot * 4) != 0);
            imported = true;
        }
        const Value *saves = j.get("saves");
        for (int c = 0; c < 2; ++c)
        {
            Card &card = g_cards[c];
            const size_t base = static_cast<size_t>(c) * kAdvSlots;
            const bool sliceInJson = saves && saves->type == Value::Arr && saves->arr.size() > base;
            std::vector<uint8_t> a;
            const bool cardHasSlots = readAdventure(card, a) && hasSlots(a);
            // Cartao 2 sem pasta e sem slots no JSON: nao cria (o jogo veria um cartao com dados que nao existia).
            if (c > 0 && !sliceInJson && !fs::exists(card.dir, ec))
                continue;
            fs::create_directories(card.dir, ec);
            if (hasSettings)
            {
                std::vector<uint8_t> b;
                readFile(card.dir / "BASCUS-97197", b);
                jsonToSettings(j, b);
                if (!writeFile(card.dir / "BASCUS-97197", b.data(), b.size()))
                    std::fprintf(stderr, "[wotm] save: nao consegui gravar as opcoes em %s\n", card.dir.string().c_str());
            }
            if (!sliceInJson && cardHasSlots)
            {
                // O JSON nao tem os slots deste cartao e ele tem aventuras: importa em vez de apagar.
                adventureToJson(a, j, c);
                saves = j.get("saves");
                imported = true;
                std::fprintf(stderr, "[wotm] save: aventuras do cartao %d importadas para os slots %zu..%zu\n", c + 1, base, base + kAdvSlots - 1);
                continue;
            }
            if (saves || c == 0)
            {
                std::vector<uint8_t> b;
                jsonToAdventure(j, b, c);
                if (!writeFile(card.dir / "savegame.dat", b.data(), b.size()))
                    std::fprintf(stderr, "[wotm] save: nao consegui gravar a aventura em %s\n", card.dir.string().c_str());
            }
        }
        if (imported)
            saveJson(j, "chaves novas ou aventuras importadas do cartao");
        restamp();
    }

    uint32_t ramU32(const uint8_t *rdram, uint32_t a)
    {
        uint32_t v;
        std::memcpy(&v, rdram + (a & 0x1FFFFFFu), 4);
        return v;
    }

    void afterMc(const uint8_t *rdram)
    {
        // O que o jogo tem agora (confere que o JSON chegou: depois do boot, os tokens e a dificuldade do save.json)
        const uint32_t sh = ramU32(rdram, wotm::sym::shell), gm = ramU32(rdram, wotm::sym::game);
        if (sh && gm)
            std::fprintf(stderr, "[wotm] save: no jogo, %u tokens, dificuldade %u, Mecha Sweet Tooth %s\n", ramU32(rdram, sh + 0x2B24),
                         ramU32(rdram, gm + 0x1203CC), ramU32(rdram, wotm::sym::monsterSelectMode + kSweetToothSlot * 0x2C + 4) ? "liberado" : "travado");
        unsigned changed = 0;
        for (int c = 0; c < 2; ++c)
            if (stampOf(g_cards[c].dir / "BASCUS-97197") != g_cards[c].settings || stampOf(g_cards[c].dir / "savegame.dat") != g_cards[c].adventure)
                changed |= 1u << c;
        if (changed)
            binToJson(changed, changed == 3 ? "o jogo gravou nos dois cartoes" : changed == 1 ? "o jogo gravou no cartao 1" : "o jogo gravou no cartao 2");
        restamp();
    }

    // ---- gravacao direta, sem as paginas do cartao (NATIVO.md, "Save em JSON") ----
    //
    // Boot, opcoes, destrave, endurance e fim da historia nao abrem mais o cartao: o MonsterMc::update* de cada um (chamado num laco
    // ate MonsterMc+4) vira uma copia direta entre a RAM e o JSON e termina na mesma chamada. A aventura (updateStoryModeSave/Load)
    // continua com as paginas e os binarios (item 5). Os tokens que o jogo soma fora dessas gravacoes (fim de fase da historia, em
    // Shell::EvaluateOnePlayerStoryStatus) sao pegos pela vigia por quadro (poll).

    void w32(uint8_t *rdram, uint32_t a, uint32_t v) { std::memcpy(rdram + (a & 0x1FFFFFFu), &v, 4); }
    constexpr uint32_t kSelStride = 0x2C, kSelState = 4, kSelCost = 8;   // SelectSlot (12 grupos de 4, 0xB0 por grupo)
    constexpr uint32_t kGameMatchMode = 0x1203CC, kGameUnk = 0x1204B0;
    constexpr uint32_t kShellSfx = 0x2A30, kShellMusic = 0x2A34, kShell2A40 = 0x2A40, kShellMono = 0x2C20, kShellCtl = 0x2B8C,
                       kShellVib = 0x2B94, kShellTokens = 0x2B24;

    // MonsterMc::getSettingsData: RAM -> bloco de 0x380 (o resto zero, com a marca)
    bool ramToBlock(const uint8_t *rdram, std::vector<uint8_t> &b)
    {
        const uint32_t sh = ramU32(rdram, wotm::sym::shell), gm = ramU32(rdram, wotm::sym::game);
        if (!sh || !gm)
            return false;
        b.assign(kSettingsSize, 0);
        wr(b, 0x00, ramU32(rdram, gm + kGameMatchMode));
        wr(b, 0x04, ramU32(rdram, sh + kShellSfx));
        wr(b, 0x08, ramU32(rdram, sh + kShellMusic));
        wr(b, 0x0C, ramU32(rdram, sh + kShell2A40));
        wr(b, 0x10, ramU32(rdram, sh + kShellMono));
        for (uint32_t k = 0; k < 2; ++k)
        {
            wr(b, 0x14 + k * 4, ramU32(rdram, sh + kShellCtl + k * 4));
            wr(b, 0x1C + k * 4, ramU32(rdram, sh + kShellVib + k * 4));
        }
        wr(b, 0x24, ramU32(rdram, sh + kShellTokens));
        for (uint32_t i = 0; i < 48; ++i)
            wr(b, 0x28 + i * 4, ramU32(rdram, wotm::sym::monsterSelectMode + i * kSelStride + kSelState));
        for (uint32_t k = 0; k < 3; ++k)
            wr(b, 0xE8 + k * 4, ramU32(rdram, gm + kGameUnk + k * 4));
        wr(b, kSettingsMagic, kMagic);
        return true;
    }

    // MonsterMc::setSettingsData: bloco -> RAM (mono vira 0/1, como no jogo)
    void blockToRam(uint8_t *rdram, const std::vector<uint8_t> &b)
    {
        const uint32_t sh = ramU32(rdram, wotm::sym::shell), gm = ramU32(rdram, wotm::sym::game);
        if (!sh || !gm || b.size() != kSettingsSize)
            return;
        w32(rdram, gm + kGameMatchMode, rd(b, 0x00));
        w32(rdram, sh + kShellSfx, rd(b, 0x04));
        w32(rdram, sh + kShellMusic, rd(b, 0x08));
        w32(rdram, sh + kShell2A40, rd(b, 0x0C));
        w32(rdram, sh + kShellMono, rd(b, 0x10) != 0);
        for (uint32_t k = 0; k < 2; ++k)
        {
            w32(rdram, sh + kShellCtl + k * 4, rd(b, 0x14 + k * 4));
            w32(rdram, sh + kShellVib + k * 4, rd(b, 0x1C + k * 4));
        }
        w32(rdram, sh + kShellTokens, rd(b, 0x24));
        for (uint32_t i = 0; i < 48; ++i)
            w32(rdram, wotm::sym::monsterSelectMode + i * kSelStride + kSelState, rd(b, 0x28 + i * 4));
        for (uint32_t k = 0; k < 3; ++k)
            w32(rdram, gm + kGameUnk + k * 4, rd(b, 0xE8 + k * 4));
    }

    // Tokens e destraves da ultima gravacao (a vigia compara com a RAM)
    struct Snap
    {
        bool valid = false;
        uint32_t v[49] = {};
        bool operator==(const Snap &o) const { return valid == o.valid && std::memcmp(v, o.v, sizeof v) == 0; }
    };
    Snap g_snap;
    // cada gravacao do JSON soma g_wotmSaveCount (definido antes do namespace; o icone de salvamento, wotm_saveicon.hpp, le)

    Snap snapOf(const uint8_t *rdram)
    {
        Snap s;
        const uint32_t sh = ramU32(rdram, wotm::sym::shell);
        if (!sh)
            return s;
        s.valid = true;
        s.v[0] = ramU32(rdram, sh + kShellTokens);
        for (uint32_t i = 0; i < 48; ++i)
            s.v[1 + i] = ramU32(rdram, wotm::sym::monsterSelectMode + i * kSelStride + kSelState);
        return s;
    }

    // RAM -> JSON. Sem `options`, so tokens e destraves (as opcoes do arquivo ficam: o menu de opcoes pode estar com valores ainda
    // nao aceitos).
    void ramToJson(const uint8_t *rdram, bool options, const char *why)
    {
        std::vector<uint8_t> b;
        if (!ramToBlock(rdram, b))
            return;
        Value j;
        std::error_code ec;
        if (fs::exists(g_json, ec) && !loadJson(j))
        {
            fs::copy_file(g_json, g_json.string() + ".bad", fs::copy_options::overwrite_existing, ec);
            std::fprintf(stderr, "[wotm] save: o JSON quebrado ficou em %s.bad\n", g_json.string().c_str());
            j = Value();
            g_badJson = false;
        }
        if (j.type != Value::Obj)
            j = Value::object();
        if (!j.get("format"))
            j.at("format") = Value::integer(1);
        const Value *oldOpt = j.get("options");
        const bool keepOpt = !options && oldOpt;
        const Value kept = keepOpt ? *oldOpt : Value();
        settingsToJson(b, j);
        if (keepOpt)
            j.at("options") = kept;
        saveJson(j, why);
        g_snap = snapOf(rdram);
        g_wotmSaveCount.fetch_add(1, std::memory_order_relaxed);
    }

    // JSON -> RAM (boot e "load settings"). Sem JSON, cria-o dos cartoes antigos se houver (beforeMc); sem nada, o jogo fica com
    // os valores do construtor do Shell, como num cartao vazio.
    void jsonToRam(uint8_t *rdram)
    {
        beforeMc();
        Value j;
        std::vector<uint8_t> b;
        if (loadJson(j) && (j.get("options") || j.get("unlocks") || j.get("battleTokens")) && ramToBlock(rdram, b))
        {
            jsonToSettings(j, b);
            blockToRam(rdram, b);
        }
        g_snap = snapOf(rdram);
        afterMc(rdram);   // so a linha do log; os binarios nao mudaram
    }

    // A compra da tela de destrave so e efetivada dentro do McSaveSettings::update (g_monsterToUnlock/g_skinToUnlock: liga o
    // state e desconta o preco); a tela poe -1 nos dois logo depois do laco.
    void applyPendingUnlock(uint8_t *rdram)
    {
        const int32_t m = static_cast<int32_t>(ramU32(rdram, wotm::sym::g_monsterToUnlock));
        const int32_t s = static_cast<int32_t>(ramU32(rdram, wotm::sym::g_skinToUnlock));
        const uint32_t sh = ramU32(rdram, wotm::sym::shell);
        if (m < 0 || s < 0 || !sh)
            return;
        const uint32_t e = wotm::sym::monsterSelectMode + (static_cast<uint32_t>(m) * 4 + static_cast<uint32_t>(s)) * kSelStride;
        if (ramU32(rdram, e + kSelState))
            return;
        w32(rdram, sh + kShellTokens, ramU32(rdram, sh + kShellTokens) - ramU32(rdram, e + kSelCost));
        w32(rdram, e + kSelState, 1);
    }

    // Fim do endurance: o McSaveSettings::update do retail (com a flag +0x280, que so o updateEndureSave liga) grava o
    // m_winsThisGame do jogador 1 no recorde da dificuldade se for maior. A troca da sequencia pulava isso e o recorde ficava no valor
    // do boot. Aqui o maior vai tambem para a RAM (o retail nao a atualizava: uma 2a corrida pior na mesma sessao sobrescrevia a 1a).
    constexpr uint32_t kGameMonsters = 0x120380, kMonsterWins = 0x3C;   // TheGame::m_monsters[0], Monster::m_winsThisGame
    void updateEnduranceRecord(uint8_t *rdram)
    {
        const uint32_t gm = ramU32(rdram, wotm::sym::game);
        if (!gm)
            return;
        const uint32_t d = ramU32(rdram, gm + kGameMatchMode);
        const uint32_t m = ramU32(rdram, gm + kGameMonsters);
        if (d > 2 || !m)
            return;
        const int32_t wins = static_cast<int32_t>(ramU32(rdram, m + kMonsterWins));
        const uint32_t a = gm + kGameUnk + d * 4;
        const int32_t best = static_cast<int32_t>(ramU32(rdram, a));
        if (wins > best)
        {
            w32(rdram, a, static_cast<uint32_t>(wins));
            std::fprintf(stderr, "[wotm] save: recorde do endurance (%s): %d -> %d\n", kDifficultyNames[d], best, wins);
        }
    }

    enum class Seq { BootLoad, SettingsLoad, SettingsSave, UnlockSave, EndOfStorySave, EndureSave };
    struct SeqHook { uint32_t fn; Seq seq; const char *why; };
    const SeqHook kSeqHooks[] = {
        {wotm::fn::updateBootLoad__9MonsterMc, Seq::BootLoad, "boot"},
        {wotm::fn::updateSettingsLoad__9MonsterMc, Seq::SettingsLoad, "load settings"},
        {wotm::fn::updateSettingsSave__9MonsterMc, Seq::SettingsSave, "opcoes"},
        {wotm::fn::updateUnlockSave__9MonsterMc, Seq::UnlockSave, "destrave"},
        {wotm::fn::updateEndOfStorySave__9MonsterMc, Seq::EndOfStorySave, "fim da historia"},
        {wotm::fn::updateEndureSave__9MonsterMc, Seq::EndureSave, "endurance"},
    };

    template <size_t I> void seqReplaced(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
    {
        const SeqHook &h = kSeqHooks[I];
        const uint32_t self = GPR_U32(ctx, 4);
        if (h.seq == Seq::BootLoad || h.seq == Seq::SettingsLoad)
            jsonToRam(rdram);
        else
        {
            applyPendingUnlock(rdram);
            if (h.seq == Seq::EndureSave)
                updateEnduranceRecord(rdram);
            ramToJson(rdram, true, h.why);
        }
        // Como o stdExit: MonsterMc+0 (montar na proxima) e +4 (pronto, o laco do chamador sai). McPageMgr+0x130: a sequencia chegou
        // a ultima pagina (a tela de opcoes so volta com ele ligado).
        w32(rdram, self + 0, 1);
        w32(rdram, self + 4, 1);
        w32(rdram, wotm::sym::x_9McPageMgr_global + 0x130, 1);
        ctx->pc = GPR_U32(ctx, 31);
    }
    template <size_t... I> void registerSeqs(PS2Runtime &runtime, std::index_sequence<I...>)
    {
        (runtime.registerFunction(kSeqHooks[I].fn, &seqReplaced<I>), ...);
    }

    // ---- lista de saves da aventura, sem caixas nem cartao (NATIVO.md, "Save em JSON") ----
    //
    // Carregar: updateStoryModeLoad leva a camera do menu ate as caixas (McPrepBoxScreen) e abre o McLoadGameBoxes (menu "slot 1 /
    // slot 2"). O update dessa pagina vira um modal com a lista do save.json (ate 99 slots): a esquerda, o save escolhido (monstro,
    // fase, vidas, progresso da aventura); a direita, a lista. X carrega (MonsterMc::setSaveData de um slot montado aqui e a
    // sequencia termina com sucesso, como depois da fita escolhida); triangulo/circulo e o "voltar" da pagina (McPage::onBack: sai
    // da tela, como antes).
    // Gravar: updateStoryModeSave, depois do "salvar?" (McAskToSave), abre o McChooseSlot (escolha do cartao); o update dele vira o
    // mesmo modal: X num slot vazio grava ali, num ocupado pede um segundo X (sobrescrever); o slot vai direto para o JSON e a
    // sequencia termina com sucesso. "Voltar" volta ao "salvar?".
    // O desenho e o do jogo: a moldura do McBackground e o texto pelo McTextBox (fontes do McPage), montados a cada quadro como o
    // McPage::drawMenu faz; o McPageMgr::update desenha depois da pagina. Nada aqui espera quadro.
    constexpr uint32_t kMgr = wotm::sym::x_9McPageMgr_global, kTextBox = kMgr + 0x13C, kBackground = kMgr + 0xDDC;
    constexpr uint32_t kInputPressed = kMgr + 0x1158;   // McPageMgr::Input: {mascara, apertado} x 10
    enum : uint32_t { kBtnUp = 0, kBtnDown = 1, kBtnX = 2, kBtnTriangle = 3, kBtnCircle = 4, kBtnLeft = 8, kBtnRight = 9 };
    constexpr uint32_t kBoxesUpdate = wotm::fn::update__15McLoadGameBoxesR7McState, kChooseUpdate = wotm::fn::update__12McChooseSlotR7McState;
    // Memoria do jogo que o modal usa: o buffer do savegame.dat do McSelectReel (pagina que nao abre mais): o slot do setSaveData e
    // as fontes (o McTextBox guarda o ponteiro da fonte de cada linha ate desenhar).
    constexpr uint32_t kScratch = wotm::sym::x_12McSelectReel_global + 0x280, kSlotBuf = kScratch, kFontsBuf = kScratch + 0x100,
                       kStrBuf = kScratch + 0x1D0;   // 0x280 + 0x1D0 + 0x40 cabe nos 0x540 do McSelectReel
    constexpr int kMaxSlots = 99, kVisibleRows = 10;
    constexpr int kStoryLevels = 11;   // Midtown Park (1) .. Capitol (11)
    const char *const kMonsterTitle[] = {"", "Congar", "Robo-47", "Togera", "Ultra-V", "Preytor", "Spider", "Kineticlops", "Magmo",
                                         "Raptros", "Agamo", "Zorgulon", "Ogre", "Goliath Prime", "Jelly", "Final", "Temp16"};

    // Funcao do jogo que nao bloqueia, com ate 8 argumentos inteiros (a0..a3, t0..t3) e ate 4 float ($f12..$f15); o contexto volta
    // como estava. `sp`: pilha da chamada (0 = a atual).
    void guestCall(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt, uint32_t fn, std::initializer_list<uint32_t> args, uint32_t sp = 0,
                   std::initializer_list<float> fargs = {})
    {
        const PS2Runtime::RecompiledFunction f = rt->lookupFunction(fn);
        if (!f)
            return;
        const R5900Context saved = *ctx;
        int r = 4;
        for (uint32_t a : args)
            ctx->r[r++] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(a)));
        int fr = 12;
        for (float a : fargs)
            ctx->f[fr++] = a;
        if (sp)
            ctx->r[29] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(sp)));
        ctx->r[31] = _mm_set_epi64x(0, 0);
        ctx->pc = fn;
        f(rdram, ctx, rt);
        wotmFinishGuestCall(rdram, ctx, rt);
        *ctx = saved;
    }

    struct Row
    {
        int slot = 0;
        bool used = false;
        uint32_t level = 0, sel = 0, lives = 0, score = 0, costume = 0;
        std::string skin;           // skin de mod do slot ("togera/green"), achada ou nao
        bool skinMissing = false;   // a skin nao existe nesta sessao: carrega com o traje 0
    };
    struct Modal
    {
        bool open = false, saving = false;
        uint32_t page = 0;   // a pagina (McLoadGameBoxes / McChooseSlot) que o modal ocupa
        std::vector<Row> rows;
        int cur = 0, top = 0;
        bool confirm = false;   // gravar: segundo X sobrescreve
        bool confirmByMouse = false;   // o primeiro X foi um clique (o texto pede outro clique)
        uint64_t openedAt = 0;  // g_vsyncs da abertura
        Row pending;            // gravar: o que vai ser gravado (so para o desenho dos primeiros quadros)
        uint64_t seen = 0;      // g_vsyncs do ultimo update: um buraco quer dizer que a pagina foi fechada e aberta de novo
    };
    Modal g_modal;
    uint64_t g_vsyncs = 0;   // sceGsSyncV contados pelo poll

    // Linhas do modal: os slots do JSON ate o ultimo ocupado, mais um vazio no fim (ate 99).
    void buildRows(Modal &m)
    {
        m.rows.clear();
        Value j;
        const Value *saves = loadJson(j) ? j.get("saves") : nullptr;
        int last = -1;
        if (saves && saves->type == Value::Arr)
            for (size_t i = 0; i < saves->arr.size() && i < kMaxSlots; ++i)
                if (saves->arr[i].type == Value::Obj)
                    last = static_cast<int>(i);
        const int n = std::min(kMaxSlots, last + 2);
        for (int i = 0; i < n; ++i)
        {
            Row r;
            r.slot = i;
            if (saves && i <= last && saves->arr[static_cast<size_t>(i)].type == Value::Obj)
            {
                const Value &s = saves->arr[static_cast<size_t>(i)];
                r.used = true;
                r.level = indexOf(kLevels, s.get("level"), 0);
                const Value *var = s.get("variant");
                r.sel = (indexOf(kMonsters, s.get("monster"), 0) << 5) | (var ? static_cast<uint32_t>(var->asInt()) & 31 : 0);
                r.lives = savesIntOf(s, "lives");
                r.score = savesIntOf(s, "score");
                const Costume c = resolveCostume(r.sel >> 5, s.get("costume"), false);
                r.costume = c.index, r.skin = c.skin, r.skinMissing = c.missing;
            }
            m.rows.push_back(r);
        }
    }

    // Gravar: o slot com o que o MonsterMc::getSaveData poria (fase, monstro, vidas, pontos da campanha, traje).
    Row rowFromRam(const uint8_t *rdram)
    {
        Row r;
        const uint32_t sh = ramU32(rdram, wotm::sym::shell);
        if (!sh)
            return r;
        r.used = true;
        r.level = ramU32(rdram, sh + 0x2BB0);
        r.sel = ramU32(rdram, sh + 0x2938);
        r.lives = ramU32(rdram, sh + 0x2BBC);
        r.score = ramU32(rdram, sh + 0x2B28) + ramU32(rdram, sh + 0x2B2C);
        r.costume = ramU32(rdram, sh + 0x2BE0);
        return r;
    }

    std::string levelTitle(const uint8_t *rdram, uint32_t lvl)
    {
        if (lvl >= std::size(kLevels))
            return "?";
        char b[19] = {};
        std::memcpy(b, rdram + ((wotm::sym::GameLevelLongNames_006ED120 + lvl * 18) & 0x1FFFFFFu), 18);   // GameLevelLongNames[28][18]
        return b;
    }
    std::string rowTitle(const uint8_t *rdram, const Row &r)
    {
        const uint32_t mon = r.sel >> 5;
        return std::string(mon < std::size(kMonsterTitle) ? kMonsterTitle[mon] : "?") + " at " + levelTitle(rdram, r.level);
    }
    // A fase do save e a proxima a jogar (Shell::EvaluateOnePlayerStoryStatus soma 1 antes de perguntar): concluidas = fase - 1.
    int levelsDone(const Row &r) { return std::clamp(static_cast<int>(r.level) - 1, 0, kStoryLevels); }

    void sfx(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt, uint32_t id)
    {
        if (const uint32_t sh = ramU32(rdram, wotm::sym::shell))
            guestCall(rdram, ctx, rt, wotm::fn::playShellSFXSound__10ShellSoundi, {sh + 0x2918, id});
    }

    // Uma linha no McTextBox (ele copia o texto).
    void text(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt, const std::string &s, int x, int y, uint32_t font, bool centered = false)
    {
        // Num buffer fixo da RAM, nao na pilha: na fase a pilha do EE fica no scratchpad (0x70000000), fora do rdram
        const uint32_t str = kStrBuf;
        const size_t n = std::min<size_t>(s.size(), 0x3F);
        std::memcpy(rdram + (str & 0x1FFFFFFu), s.data(), n);
        rdram[(str + n) & 0x1FFFFFFu] = 0;
        guestCall(rdram, ctx, rt, wotm::fn::addTextString__9McTextBoxPciibPQ26McPage8FontInfo,
                  {kTextBox, str, static_cast<uint32_t>(x), static_cast<uint32_t>(y), centered ? 1u : 0u, font});
    }

    void modalOpen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt, uint32_t page, bool saving)
    {
        Modal &m = g_modal;
        m = Modal{};
        m.open = true;
        m.saving = saving;
        m.page = page;
        m.openedAt = g_vsyncs;
        buildRows(m);
        // mouse (etapa 9): o clique que abriu (o "YES" do "salvar?") nao vale aqui
        wotm::mouse::takeClick(wotm::mouse::g_clickL);
        wotm::mouse::takeClick(wotm::mouse::g_clickR);
        wotm::mouse::g_crossReq.store(0, std::memory_order_relaxed);
        // Comeco: carregar, no primeiro ocupado; gravar, no vazio do fim
        m.cur = 0;
        if (saving)
            m.cur = static_cast<int>(m.rows.size()) - 1;
        else
            for (size_t i = 0; i < m.rows.size(); ++i)
                if (m.rows[i].used)
                {
                    m.cur = static_cast<int>(i);
                    break;
                }
        m.top = std::max(0, m.cur - kVisibleRows + 1);
        w32(rdram, kTextBox + 0xC94, 1);   // McTextBox::setDontFade(true): o texto troca sem o cruzamento (como no McAskToSave)
        guestCall(rdram, ctx, rt, wotm::fn::setDestDims__12McBackgroundfff, {kBackground}, 0, {1.0f, 1.0f, 0.0f});
        if (saving)
        {
            // Depois da luta (wonLevelOverlay) o timerGetFieldsLastFrame nao anda: a moldura nao cresce sozinha ate o tamanho novo, e o
            // McPageMgr::update so le o pad e roda a pagina quando ela chega la. Vai direto (McBackground +0xC/+0x10 = +0x14/+0x18).
            std::memcpy(rdram + ((kBackground + 0xC) & 0x1FFFFFFu), rdram + ((kBackground + 0x14) & 0x1FFFFFFu), 8);
        }
        std::fprintf(stderr, "[wotm] save: lista de saves aberta (%s, %zu linhas, pilha 0x%08X)\n", saving ? "gravar" : "carregar",
                     m.rows.size(), GPR_U32(ctx, 29));
    }
    void modalClose(uint8_t *rdram)
    {
        g_modal.open = false;
        wotm::savepics::clear();
        w32(rdram, kTextBox + 0xC94, 0);   // setDontFade(false)
    }

    void modalDraw(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt, const Row *saving)
    {
        const Modal &m = g_modal;
        // A fonte do menu nao tem '%', ':' nem '/' (saem em branco). Maior linha: "99. Kineticlops at Century Airfield". Fontes do McPage menores (a lista e longa): copias em kFontsBuf com os tamanhos do makeCurrentFont (+0, +4, +8, +0xC, em
        // subpixels) escalados
        // Na fase (gravar depois da luta) a moldura sai mais baixa que no menu: no menu ela vai de y ~25 a ~194 da tela do jogo, na
        // fase de ~56 a ~179 (o tamanho vem da animacao do McBackground, nao do setDestDims). As alturas sao comprimidas para caber.
        const bool inLevel = m.saving;
        const float fk = inLevel ? 0.8f : 1.0f;
        auto Y = [&](int y) { return inLevel ? static_cast<int>(std::lround(58 + (y - 25) * 0.7f)) : y; };
        auto scaled = [&](int idx, uint32_t src, float k) {
            const uint32_t dst = kFontsBuf + static_cast<uint32_t>(idx) * 0x28;
            std::memcpy(rdram + (dst & 0x1FFFFFFu), rdram + (src & 0x1FFFFFFu), 0x28);
            for (uint32_t o : {0u, 4u, 8u, 0xCu})
                w32(rdram, dst + o, static_cast<uint32_t>(static_cast<int32_t>(std::lround(static_cast<int32_t>(ramU32(rdram, src + o)) * k * fk))));
            return dst;
        };
        const uint32_t fTitle = scaled(0, wotm::sym::x_6McPage_s_titleFont, 0.7f), fText = scaled(1, wotm::sym::x_6McPage_s_font, 0.8f);
        const uint32_t fName = scaled(4, wotm::sym::x_6McPage_s_titleFont, 0.55f);   // titulo da esquerda: cabe na metade
        const uint32_t fSel = scaled(2, wotm::sym::x_6McPage_s_selectedFont, 0.52f), fUnsel = scaled(3, wotm::sym::x_6McPage_s_unselectedFont, 0.7f);
        // Esquerda: o save da linha escolhida (gravar num vazio: o que vai ser gravado)
        const Row *show = nullptr;
        if (!m.rows.empty())
            show = m.rows[static_cast<size_t>(m.cur)].used ? &m.rows[static_cast<size_t>(m.cur)] : saving;
        constexpr int kLeftX = 30, kRightX = 318, kLeftCenter = 164;
        std::vector<wotm::savepics::Pic> pics;
        if (m.confirm)
        {
            text(rdram, ctx, rt, "Overwrite save file " + std::to_string(m.cur + 1) + "?", kLeftX, Y(40), fName);
            // o botao de confirmar do dispositivo de agora (a janela publica o nome: wotm_live.inc), ou o clique
            const std::string again = m.confirmByMouse ? "Click again to overwrite" : "Press " + wotm::mouse::confirmLabel() + " again to overwrite";
            text(rdram, ctx, rt, again, kLeftX, Y(70), fText);
        }
        else if (show)
        {
            // Centrados na metade esquerda, de cima para baixo: o logo do monstro, "on <fase>", a foto larga da fase (depois o monstro
            // 3D vai por cima dela), os pontos e o resto. Na tela do jogo um pixel e 1,5 de largura por 720/224 de altura: textura 2:1
            // na janela = altura de largura * aspect / 2. Na fase a moldura e mais baixa: tudo encolhe junto com o Y. As imagens entram
            // devagar depois que a moldura abre (o texto nao: setDontFade).
            const uint32_t mon = show->sel >> 5;
            const float k = inLevel ? 0.7f : 1.0f, aspect = 1.5f / (720.f / 224.f);
            const uint8_t a = static_cast<uint8_t>(255.f * std::clamp((static_cast<float>(g_vsyncs - m.openedAt) - 6.f) / 12.f, 0.f, 1.f));
            const float lw = 170.f * k, lh = lw * aspect * 0.5f, ly0 = static_cast<float>(Y(24));
            if (wotm::savepics::hasLogo(mon))
                pics.push_back({wotm::savepics::kLogo, mon, kLeftCenter - lw * 0.5f, ly0, kLeftCenter + lw * 0.5f, ly0 + lh, a});
            else
                text(rdram, ctx, rt, mon < std::size(kMonsterTitle) ? kMonsterTitle[mon] : "?", kLeftCenter, Y(40), fName, true);
            text(rdram, ctx, rt, "on " + levelTitle(rdram, show->level), kLeftCenter, Y(66), fName, true);
            const float pw = 220.f * k, ph = pw * aspect * 0.5f, px0 = kLeftCenter - pw * 0.5f, py0 = static_cast<float>(Y(82));
            if (wotm::savepics::hasLevel(show->level))
                pics.push_back({wotm::savepics::kLevel, show->level, px0, py0, px0 + pw, py0 + ph, a});
            if (wotm::savepics::monsterFile(mon))
            {
                // Monstro 3D em idle por cima da foto, com os pes perto da borda de baixo dela. Quadrado na janela: largura da tela
                // do jogo = altura * (720 / 224) / 1,5.
                const float mh = ph * 1.32f, mw = mh / aspect, mcx = kLeftCenter, my1 = py0 + ph + 3.f * k;
                pics.push_back({wotm::savepics::kMonster, mon | (show->costume & 0xFF) << 8 | modelBase(mon, show->costume) << 16, mcx - mw * 0.5f, my1 - mh, mcx + mw * 0.5f, my1, a});
            }
            text(rdram, ctx, rt, "Score " + std::to_string(show->score), kLeftX, Y(140), fText);
            text(rdram, ctx, rt, std::to_string(show->lives) + (show->lives == 1 ? " life" : " lives") + (show->skinMissing ? "  -  skin missing" : ""),
                 kLeftX, Y(154), fText);
            text(rdram, ctx, rt, "Adventure progress", kLeftX, Y(168), fText);
            text(rdram, ctx, rt, std::to_string(levelsDone(*show)) + " of " + std::to_string(kStoryLevels) + " levels", kLeftX, Y(182), fText);
        }
        else
            text(rdram, ctx, rt, "No saved adventures", kLeftX, Y(40), fName);
        wotm::savepics::post(std::move(pics));
        // Direita: a lista
        text(rdram, ctx, rt, "Save files", kRightX, Y(40), fTitle);
        for (int i = m.top; i < std::min<int>(m.top + kVisibleRows, static_cast<int>(m.rows.size())); ++i)
        {
            const Row &r = m.rows[static_cast<size_t>(i)];
            const std::string s = r.used ? std::to_string(i + 1) + ". " + rowTitle(rdram, r) : "-- Empty save file --";
            text(rdram, ctx, rt, s, kRightX, Y(60 + (i - m.top) * 14), i == m.cur ? fSel : fUnsel);
        }
    }

    // Anda na lista (carregar pula os vazios); devolve true se mexeu
    // `wrap`: passar da ponta da a volta (cima/baixo); sem ele para na ponta (esquerda/direita, de 10 em 10).
    bool modalMove(Modal &m, int step, bool wrap)
    {
        const int n = static_cast<int>(m.rows.size());
        int i = m.cur;
        for (int k = 0; k < n; ++k)
        {
            i += step > 0 ? 1 : -1;
            if (i < 0 || i >= n)
            {
                if (!wrap)
                    return false;
                i = i < 0 ? n - 1 : 0;
            }
            if (m.saving || m.rows[static_cast<size_t>(i)].used)
                break;
        }
        if (i == m.cur || !(m.saving || m.rows[static_cast<size_t>(i)].used))
            return false;
        m.cur = i;
        m.top = std::clamp(m.top, std::max(0, m.cur - kVisibleRows + 1), m.cur);
        return true;
    }

    // Carregar: o slot escolhido vira o slot 0 de um savegame.dat na memoria e o MonsterMc::setSaveData o aplica.
    void loadRow(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt, const Row &r)
    {
        std::memset(rdram + (kSlotBuf & 0x1FFFFFFu), 0, kAdvSlot0 + kAdvStride);
        w32(rdram, kSlotBuf, 1);
        w32(rdram, kSlotBuf + kAdvSlot0 + 0, r.level);
        w32(rdram, kSlotBuf + kAdvSlot0 + 4, r.sel);
        w32(rdram, kSlotBuf + kAdvSlot0 + 8, r.lives);
        w32(rdram, kSlotBuf + kAdvSlot0 + 12, r.score);
        w32(rdram, kSlotBuf + kAdvSlot0 + 16, r.costume);   // ja resolvido: skin ausente = traje 0
        g_missing = MissingSkin{r.skinMissing, r.sel >> 5, r.skin};
        if (r.skinMissing)
            std::fprintf(stderr, "[wotm] save: traje %s nao achado (mod ausente?); usando traje 0\n", r.skin.c_str());
        guestCall(rdram, ctx, rt, wotm::fn::setSaveData__9MonsterMcR13_SaveGameDatai, {kSlotBuf, 0});
        std::fprintf(stderr, "[wotm] save: aventura do slot %d carregada (%s)\n", r.slot + 1, rowTitle(rdram, r).c_str());
    }

    // Gravar: o slot vai direto para o save.json (o resto do arquivo fica)
    void saveRow(const uint8_t *rdram, int slot, const Row &r)
    {
        Value j;
        std::error_code ec;
        if (fs::exists(g_json, ec) && !loadJson(j))
        {
            fs::copy_file(g_json, g_json.string() + ".bad", fs::copy_options::overwrite_existing, ec);
            j = Value();
            g_badJson = false;
        }
        if (j.type != Value::Obj)
            j = Value::object();
        if (!j.get("format"))
            j.at("format") = Value::integer(1);
        Value &saves = j.at("saves");
        if (saves.type != Value::Arr)
            saves = Value::array();
        if (saves.arr.size() <= static_cast<size_t>(slot))
            saves.arr.resize(static_cast<size_t>(slot) + 1);
        Value s = Value::object();
        s.at("level") = nameOf(kLevels, r.level);
        s.at("monster") = nameOf(kMonsters, r.sel >> 5);
        s.at("variant") = Value::integer(r.sel & 31);
        s.at("lives") = Value::integer(r.lives);
        s.at("score") = Value::integer(r.score);
        s.at("costume") = savedCostume(r.sel >> 5, r.costume);
        saves.arr[static_cast<size_t>(slot)] = s;
        const std::string why = "aventura no slot " + std::to_string(slot + 1) + " (" + rowTitle(rdram, r) + ")";
        saveJson(j, why.c_str());
        g_wotmSaveCount.fetch_add(1, std::memory_order_relaxed);
    }

    // A sequencia de paginas termina (McPageMgr+0x128) com sucesso ou nao (+0x130), como na ultima pagina / no "voltar" da primeira.
    void finish(uint8_t *rdram, bool ok)
    {
        w32(rdram, kMgr + 0x128, 1);
        w32(rdram, kMgr + 0x130, ok ? 1u : 0u);
    }

    void modalUpdate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt, uint32_t self, bool saving)
    {
        Modal &m = g_modal;
        if (!m.open || m.page != self || m.saving != saving || g_vsyncs - m.seen > 10)
            modalOpen(rdram, ctx, rt, self, saving);
        m.seen = g_vsyncs;
        // O X que respondeu a pagina anterior ("salvar?") ainda chega aqui se for rapido: 100 ms sem ler o pad depois de abrir
        if (g_vsyncs - m.openedAt < 6)
        {
            modalDraw(rdram, ctx, rt, saving ? &(m.pending = rowFromRam(rdram)) : nullptr);
            return;
        }
        // mouse (etapa 9): a linha da lista sob ele (texto do McTextBox: x 318, y do topo 60 + 14 por linha, comprimido na fase);
        // passar por cima escolhe, clique esquerdo vale X nela; direito e rodinha chegam pelo pad (wotm_pad.inc, gameMenu)
        wotm::mouse::gameMenu();
        wotm::mouse::claim();
        wotm::mouse::saveList(m.confirm ? 3 : saving ? 2 : 1);   // legenda de baixo (wotm_live.inc)
        bool mouseX = false;
        {
            static uint32_t s_seq = 0;
            const uint32_t seq = wotm::mouse::g_moveSeq.load(std::memory_order_relaxed);
            const bool moved = seq != s_seq;
            s_seq = seq;
            const bool click = wotm::mouse::takeClick(wotm::mouse::g_clickL);
            auto Y = [&](int y) { return saving ? static_cast<int>(std::lround(58 + (y - 25) * 0.7f)) : y; };
            float gx, gy;
            int hover = -1;
            if (wotm::mouse::gsPointer(gx, gy) && gx >= 310.f && gx < 630.f)
                for (int i = m.top; i < std::min<int>(m.top + kVisibleRows, static_cast<int>(m.rows.size())); ++i)
                {
                    const float y0 = static_cast<float>(Y(60 + (i - m.top) * 14)), y1 = static_cast<float>(Y(60 + (i - m.top + 1) * 14));
                    if (gy >= y0 - 1.f && gy < y1 - 1.f) hover = i;
                }
            const bool pickable = hover >= 0 && (saving || m.rows[static_cast<size_t>(hover)].used);
            if (pickable && (moved || click) && hover != m.cur)
            {
                const bool down = hover > m.cur;
                m.cur = hover;
                m.confirm = false;
                if (!click) sfx(rdram, ctx, rt, down ? 2 : 4);
            }
            mouseX = click && pickable;
        }
        auto pressed = [&](uint32_t b) { return ramU32(rdram, kInputPressed + b * 8) != 0 || (b == kBtnX && mouseX); };
        const Row current = saving ? rowFromRam(rdram) : Row{};
        if (pressed(kBtnTriangle) || pressed(kBtnCircle))
        {
            if (m.confirm)
                m.confirm = false;
            else
            {
                sfx(rdram, ctx, rt, 6);
                modalClose(rdram);
                guestCall(rdram, ctx, rt, wotm::fn::onBack__6McPage, {self});   // como o "voltar" da pagina
                return;
            }
        }
        else if (pressed(kBtnUp) || pressed(kBtnDown) || pressed(kBtnLeft) || pressed(kBtnRight))
        {
            const bool down = pressed(kBtnDown) || pressed(kBtnRight);
            const int steps = pressed(kBtnLeft) || pressed(kBtnRight) ? kVisibleRows : 1;
            bool moved = false;
            for (int k = 0; k < steps; ++k)
                moved = modalMove(m, down ? 1 : -1, steps == 1) || moved;
            if (moved)
            {
                m.confirm = false;
                sfx(rdram, ctx, rt, down ? 2 : 4);
            }
        }
        else if (pressed(kBtnX) && !m.rows.empty())
        {
            const Row &r = m.rows[static_cast<size_t>(m.cur)];
            if (!saving && r.used)
            {
                sfx(rdram, ctx, rt, 8);
                loadRow(rdram, ctx, rt, r);
                modalClose(rdram);
                finish(rdram, true);
                return;
            }
            if (saving && r.used && !m.confirm)
            {
                m.confirm = true;
                m.confirmByMouse = mouseX;
                sfx(rdram, ctx, rt, 4);
            }
            else if (saving)
            {
                sfx(rdram, ctx, rt, 8);
                saveRow(rdram, r.slot, current);
                modalClose(rdram);
                finish(rdram, true);
                return;
            }
        }
        modalDraw(rdram, ctx, rt, saving ? &current : nullptr);
    }

    // ---- mouse nas paginas do cartao com menu (McPage: o "salvar?" depois de vencer na aventura) ----
    // Saida do McPage::updateMenuPos (a0 = pagina): itens pg+0x21C, escolhido pg+0x224, item i escolhivel se a palavra em
    // pg + 0x14 + 0x24 i nao e 0; centrados em x 320, topo em pg+0x24C + i * pg+0x250 mais o +0x10 da fonte (a do escolhido ou a dos
    // outros), altura = +0x04 da fonte / 16. Passar por cima escolhe, clique escreve o X do McPageMgr (+0x1168, lido pelo
    // checkForSelection do mesmo quadro). 75f5be1:docs/investigacoes/2026-10-22/mouse_telas_save.md, secao 4
    PS2Runtime::RecompiledFunction g_origMenuPos = nullptr;
    uint32_t g_menuPosRa = 0, g_menuPosPage = 0;
    bool g_menuPosPending = false;
    void pageMouse(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt, uint32_t pg)
    {
        static uint32_t s_page = 0, s_seq = 0;
        static uint64_t s_last = 0;
        const bool fresh = pg != s_page || g_vsyncs - s_last > 10;
        s_page = pg;
        s_last = g_vsyncs;
        wotm::mouse::gameMenu();
        wotm::mouse::claim();
        const uint32_t seq = wotm::mouse::g_moveSeq.load(std::memory_order_relaxed);
        const bool moved = seq != s_seq;
        s_seq = seq;
        if (fresh)   // o clique que abriu a pagina nao vale nela
        {
            wotm::mouse::takeClick(wotm::mouse::g_clickL);
            wotm::mouse::g_crossReq.store(0, std::memory_order_relaxed);
            return;
        }
        const int n = static_cast<int>(ramU32(rdram, pg + 0x21C)), cur = static_cast<int>(ramU32(rdram, pg + 0x224));
        if (n <= 0 || n > 10)
            return;
        const int y0 = static_cast<int>(ramU32(rdram, pg + 0x24C)), dy = static_cast<int>(ramU32(rdram, pg + 0x250));
        float gx = -1.f, gy = -1.f;
        int hover = -1;
        if (wotm::mouse::gsPointer(gx, gy) && std::fabs(gx - 320.f) < 140.f)
            for (int i = 0; i < n; ++i)
            {
                if (!ramU32(rdram, pg + 0x14 + 0x24 * static_cast<uint32_t>(i)))
                    continue;
                const uint32_t f = i == cur ? wotm::sym::x_6McPage_s_selectedFont : wotm::sym::x_6McPage_s_unselectedFont;
                const int y = y0 + i * dy + static_cast<int>(ramU32(rdram, f + 0x10));
                const int h = std::max(8, static_cast<int>(ramU32(rdram, f + 4)) / 16);
                if (gy >= static_cast<float>(y) - 2.f && gy < static_cast<float>(y + h) + 1.f) hover = i;
            }
        static const bool log = std::getenv("WOTM_MENUMOUSELOG") != nullptr;
        static int s_logged = -2;
        if (log && hover != s_logged)
        {
            std::fprintf(stderr, "[menumouse] pagina do cartao 0x%08X: %d itens, topo %d passo %d, cursor %.0f,%.0f, item %d\n", pg, n, y0, dy,
                         gx, gy, hover);
            s_logged = hover;
        }
        if (hover < 0)
            return;
        if (moved && hover != cur)
        {
            w32(rdram, pg + 0x224, static_cast<uint32_t>(hover));
            sfx(rdram, ctx, rt, hover > cur ? 2 : 4);
        }
        if (wotm::mouse::takeClick(wotm::mouse::g_clickL))
        {
            w32(rdram, pg + 0x224, static_cast<uint32_t>(hover));
            w32(rdram, kMgr + 0x1168, 1);
        }
    }
    void menuPosTapped(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (ctx->pc == wotm::fn::updateMenuPos__6McPageR7McState)
        {
            g_menuPosRa = GPR_U32(ctx, 31);
            g_menuPosPage = GPR_U32(ctx, 4);
            g_menuPosPending = true;
        }
        g_origMenuPos(rdram, ctx, rt);
        if (!g_menuPosPending || ctx->pc != g_menuPosRa)
            return;
        g_menuPosPending = false;
        pageMouse(rdram, ctx, rt, g_menuPosPage);
    }

    void boxesReplaced(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        modalUpdate(rdram, ctx, rt, GPR_U32(ctx, 4), false);
        ctx->pc = GPR_U32(ctx, 31);
    }
    void chooseReplaced(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        // Como o original: +0x274 e o que o McChooseSlot::stateIsOk devolve (sem ele o McPageMgr::update acha a pagina invalida e a
        // refaz a cada quadro: modal vazio e travado) e McPageMgr+0x114C = 2
        w32(rdram, GPR_U32(ctx, 4) + 0x274, 1);
        w32(rdram, kMgr + 0x114C, 2);
        modalUpdate(rdram, ctx, rt, GPR_U32(ctx, 4), true);
        ctx->pc = GPR_U32(ctx, 31);
    }

    // Uma vez por quadro (sceGsSyncV): tokens ou destraves mudaram fora de uma gravacao -> JSON (so eles).
    void poll(const uint8_t *rdram)
    {
        ++g_vsyncs;
        // Selecao de monstro (telas 3, 6, 7, 17, 18, 19): outra aventura vai comecar; o nome da skin ausente do slot carregado sai
        if (g_missing.valid)
            switch (ramU32(rdram, wotm::sym::currScreen))
            {
            case 3: case 6: case 7: case 17: case 18: case 19:
                g_missing = MissingSkin{};
                break;
            default:
                break;
            }
        if (!g_snap.valid)
            return;
        const Snap now = snapOf(rdram);
        if (now.valid && !(now == g_snap))
        {
            const bool tokens = now.v[0] != g_snap.v[0];
            ramToJson(rdram, false, tokens ? "tokens" : "destraves");
        }
    }

    constexpr uint32_t kStdInit = wotm::fn::stdInit__9MonsterMc, kStdExit = wotm::fn::stdExit__9MonsterMc;
    PS2Runtime::RecompiledFunction g_origStdInit = nullptr, g_origStdExit = nullptr;

    void stdInitTapped(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kStdInit)
            beforeMc();
        g_origStdInit(rdram, ctx, runtime);
    }
    void stdExitTapped(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kStdExit)
            afterMc(rdram);
        g_origStdExit(rdram, ctx, runtime);
    }

    void install(PS2Runtime &runtime)
    {
        const char *e = std::getenv("WOTM_SAVEJSON");
        if (e && e[0] == '0')
        {
            std::fprintf(stderr, "[wotm] save so no cartao mc0/, sem JSON (WOTM_SAVEJSON=0)\n");
            return;
        }
        if (!(g_origStdInit = runtime.lookupFunction(kStdInit)) || !(g_origStdExit = runtime.lookupFunction(kStdExit)))
        {
            std::fprintf(stderr, "[wotm] save em JSON indisponivel: MonsterMc::stdInit/stdExit sem funcao no runtime\n");
            return;
        }
        initPaths();
        runtime.registerFunction(kStdInit, &stdInitTapped);
        runtime.registerFunction(kStdExit, &stdExitTapped);
        g_on = true;
        registerSeqs(runtime, std::make_index_sequence<std::size(kSeqHooks)>{});
        if ((g_origMenuPos = runtime.lookupFunction(wotm::fn::updateMenuPos__6McPageR7McState)))
            runtime.registerFunction(wotm::fn::updateMenuPos__6McPageR7McState, &menuPosTapped);   // mouse no "salvar?"
        runtime.registerFunction(kBoxesUpdate, &boxesReplaced);   // lista de saves no lugar das caixas (carregar)
        runtime.registerFunction(kChooseUpdate, &chooseReplaced); // e no lugar da escolha do cartao (gravar)
        std::fprintf(stderr, "[wotm] save em JSON: %s (WOTM_SAVEJSON=0 desliga); opcoes, destraves e tokens gravados sozinhos, sem as "
                     "telas do cartao\n", g_json.string().c_str());
    }
}
