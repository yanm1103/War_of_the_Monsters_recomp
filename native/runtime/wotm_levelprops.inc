// wotm_levelprops.inc: a chave "props" do patch de fase (MODS.md, "Level patches"). Incluido por wotm_levelpatch.inc dentro do namespace
// wotm_levelpatch, depois do Stage e do childNoCase. tools/lvl.py (LevelPatch.apply_props) faz o mesmo na imagem do disco, nas mesmas
// alocacoes e na mesma ordem: os bytes da arena e as linhas "props:" batem.
// Um prop e uma mini-imagem pre-ligada em 0xA00000 (tools/lvlprop.py build: <MODELO>.NGP, <MODELO>.PTR e prop.json) em
// <mod do JSON>/props/<MODELO>/, lida do sistema de arquivos a cada carga (fora do Registry: um prop novo vale no F10). Cada modelo vai
// uma vez para a arena (alinhado a 0x80: os pacotes sao), relocado pela lista 1 do .PTR; a lista 2 (texIds) recebe o texId da fase do
// slot e a lista 3 (gsTexCtx, 0x30 bytes) o contexto vivo do primeiro OBJECT da fase, por endereco, com aquele texId (o TBP0 o
// texmActivateTexture remenda a cada upload). Uma textura propria (imagem do Blender) ja vem na imagem como pacote CT32 de .TEX e
// contexto proprio: ganha um texId depois dos carregados, a menor caixa de VRAM em que cabe e a entrada de texInfo; a cena nativa
// desenha o PNG ao lado no lugar dela (wotmPropTexture). Cada entrada e um TRANSLATE (sem rumo) ou ROTATE sobre a raiz comum do modelo, todas num
// GROUP novo (flags 7: visual, colisao, noFOV) no primeiro slot do GROUP do mundo, como os pedestres. Cada folha de colisao de cada
// entrada ganha um caminho [mundo, props, T|R, raiz, ..., folha] com a caixa pela regra dos 8 cantos, as celulas e uma planta para o
// cutNets. Recusado com uma linha e sem deixar nada na arena: modelo que falta ou de outra fase, texId fora do .TEX carregado ou sem
// OBJECT que desenhe com ele, imagem maior que todas as caixas, entrada com caixa fora da grade, modelo cujas entradas nao cabem na arena.

struct PropModel
{
    std::string name, stage;
    std::vector<uint8_t> ngp;
    std::array<std::vector<uint32_t>, 3> ptr;
    std::map<uint32_t, uint32_t> texOf;   // slot -> texId
    struct Own { uint32_t slot, packet, w, h; std::string png; };
    std::vector<Own> own;                 // texturas proprias: pacote CT32 na imagem, PNG ao lado
    uint32_t root = 0;                    // deslocamento da raiz na imagem
    std::vector<uint32_t> states;         // destrutivel: a raiz de cada estado (a primeira e a raiz); vazio num prop estatico
    std::vector<uint32_t> actions;        // destrutivel: o ActDebris de cada estado (0: nenhum), deslocamento na imagem
    uint32_t strength = 0;                // destrutivel: a classe de pontos de vida (1 a 5)
    std::map<uint32_t, std::vector<std::vector<uint8_t>>> smoke;   // destrutivel: estado -> os 0x60 bytes de cada emissor de fumaca
};
constexpr uint32_t kMaxPropStates = 4;
constexpr uint32_t kEmitterBytes = 0x60, kMaxEmitters = 8;     // lvlprop.EMITTER_SIZE, MAX_EMITTERS
constexpr uint32_t kStrengthHp[6] = {0, 10, 50, 100, 150, 250};

// Texturas proprias dos props da carga atual: texId -> PNG que a cena nativa desenha no lugar da copia de 128 do jogo
// (wotmPropTexture, lido pela thread da cena)
struct PropPng { std::string host, mod, rel; };
std::mutex g_propTexMu;
std::map<uint32_t, PropPng> g_propTex;

bool readHostFile(const std::filesystem::path &p, std::vector<uint8_t> &out)
{
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

// prop.json, .NGP e .PTR da pasta do modelo; "" ou o motivo da recusa
std::string loadPropModel(const std::filesystem::path &dir, PropModel &m)
{
    std::vector<uint8_t> raw;
    const std::filesystem::path pj = childNoCase(dir, "prop.json");
    if (pj.empty() || !readHostFile(pj, raw)) return "sem prop.json";
    json::Value meta;
    std::string err;
    if (!json::parse(std::string(raw.begin(), raw.end()), meta, &err) || meta.type != json::Value::Obj) return "prop.json invalido: " + err;
    if (const json::Value *f = meta.get("format"); !f || f->asInt() != 1) return "prop.json sem \"format\": 1";
    const json::Value *model = meta.get("model"), *stage = meta.get("stage"), *tex = meta.get("textures"), *files = meta.get("files");
    if (!model || model->type != json::Value::Str || !stage || stage->type != json::Value::Str) return "prop.json sem \"model\" ou \"stage\"";
    m.name = model->s, m.stage = stage->s;
    for (char &c : m.stage) c = char(std::toupper(static_cast<unsigned char>(c)));
    if (tex && tex->type == json::Value::Arr)
        for (const json::Value &t : tex->arr)
        {
            const json::Value *s = t.get("slot"), *id = t.get("texId"), *pk = t.get("packet"), *gm = t.get("game"), *png = t.get("png");
            if (!s || !s->isInt()) return "textura sem \"slot\"";
            if (id && id->isInt())
            {
                m.texOf[uint32_t(s->asInt())] = uint32_t(id->asInt());
                continue;
            }
            // textura propria (lvlprop.own_packet): "packet" (deslocamento na imagem), "game" [w, h] e "png"
            if (!pk || pk->type != json::Value::Str || !gm || gm->type != json::Value::Arr || gm->arr.size() != 2 || !gm->arr[0].isInt() ||
                !gm->arr[1].isInt() || !png || png->type != json::Value::Str)
                return "textura sem \"texId\" nem \"packet\", \"game\" e \"png\"";
            PropModel::Own o{uint32_t(s->asInt()), uint32_t(std::strtoul(pk->s.c_str(), nullptr, 16)), uint32_t(gm->arr[0].asInt()),
                             uint32_t(gm->arr[1].asInt()), png->s};
            if (o.w < 8 || o.w > 1024 || (o.w & (o.w - 1)) || o.h < 8 || o.h > 1024 || (o.h & (o.h - 1)))
                return fmt("textura %u com tamanho %ux%u", o.slot, o.w, o.h);
            m.own.push_back(o);
        }
    auto fileName = [&](const char *key, const char *ext) {
        const json::Value *v = files ? files->get(key) : nullptr;
        return v && v->type == json::Value::Str ? v->s : m.name + ext;
    };
    const std::filesystem::path ngpP = childNoCase(dir, fileName("ngp", ".NGP")), ptrP = childNoCase(dir, fileName("ptr", ".PTR"));
    if (ngpP.empty() || !readHostFile(ngpP, m.ngp)) return "sem o " + fileName("ngp", ".NGP");
    if (ptrP.empty() || !readHostFile(ptrP, raw)) return "sem o " + fileName("ptr", ".PTR");
    auto rd = [](const std::vector<uint8_t> &d, size_t o) { uint32_t v = 0; if (o + 4 <= d.size()) std::memcpy(&v, d.data() + o, 4); return v; };
    if (m.ngp.size() < 16 || rd(m.ngp, 0) != 1) return "a palavra 0 da imagem nao e 1";
    m.root = rd(m.ngp, 4) - kBase;
    if (m.root >= m.ngp.size()) return "raiz fora da imagem";
    if (const json::Value *d = meta.get("destructible"); d && d->type == json::Value::Obj)
    {
        const json::Value *cls = d->get("class"), *sts = d->get("states");
        if (!cls || !cls->isInt() || !sts || sts->type != json::Value::Arr) return "\"destructible\" sem \"class\" e \"states\"";
        for (const json::Value &x : sts->arr)
        {
            if (x.type != json::Value::Str) return "\"destructible\": estado que nao e um endereco";
            const uint32_t a = uint32_t(std::strtoul(x.s.c_str(), nullptr, 16));
            if (a < kBase || a - kBase >= m.ngp.size()) return "\"destructible\": raiz de estado fora da imagem";
            m.states.push_back(a - kBase);
        }
        m.actions.assign(m.states.size(), 0);
        if (const json::Value *ac = d->get("actions"); ac && ac->type == json::Value::Arr)
        {
            if (ac->arr.size() != m.states.size()) return "\"destructible\": \"actions\" nao tem um item por estado";
            for (size_t i = 0; i < ac->arr.size(); ++i)
            {
                if (ac->arr[i].type == json::Value::Null) continue;
                const uint32_t a = ac->arr[i].type == json::Value::Str ? uint32_t(std::strtoul(ac->arr[i].s.c_str(), nullptr, 16)) : 0;
                if (a < kBase || a - kBase + 0x10 > m.ngp.size()) return "\"destructible\": acao fora da imagem";
                m.actions[i] = a - kBase;
            }
        }
        if (const json::Value *sm = d->get("smoke"); sm && sm->type == json::Value::Obj)
            for (const auto &kv : sm->obj)
            {
                const uint32_t si = uint32_t(std::strtoul(kv.first.c_str(), nullptr, 10));
                if (kv.second.type != json::Value::Arr || si < 1 || si >= m.states.size() || kv.second.arr.empty() || kv.second.arr.size() > kMaxEmitters)
                    return "\"destructible\": \"smoke\" com um estado fora da faixa, sem emissor ou com emissores demais";
                for (const json::Value &e : kv.second.arr)
                {
                    const json::Value *hex = e.get("node");
                    if (!hex || hex->type != json::Value::Str || hex->s.size() != 2 * kEmitterBytes)
                        return "\"destructible\": emissor de fumaca sem \"node\" de 0x60 bytes";
                    std::vector<uint8_t> b;
                    for (size_t i = 0; i < hex->s.size(); i += 2) b.push_back(uint8_t(std::strtoul(hex->s.substr(i, 2).c_str(), nullptr, 16)));
                    if (b[0] != 22) return "\"destructible\": o \"node\" de fumaca nao e um emissor (op 22)";
                    m.smoke[si].push_back(std::move(b));
                }
            }
        m.strength = uint32_t(cls->asInt());
        if (m.states.size() < 2 || m.states.size() > kMaxPropStates || m.states[0] != m.root || m.strength < 1 || m.strength > 5)
            return fmt("\"destructible\": %zu estados (2 a %u, o primeiro e a raiz), classe %u (1 a 5)", m.states.size(), kMaxPropStates, m.strength);
    }
    for (const PropModel::Own &o : m.own)
    {
        const uint32_t bits = rd(m.ngp, size_t(o.packet) + 0xC);
        if (o.packet % 0x80 || size_t(o.packet) + 0x80 + 4 * size_t(o.w) * o.h > m.ngp.size() || (bits & 0x3F) != 0 ||
            (1u << ((bits >> 8) & 0xF)) != o.w || (1u << ((bits >> 12) & 0xF)) != o.h)
            return fmt("textura %u: o pacote em 0x%X nao e CT32 %ux%u", o.slot, o.packet, o.w, o.h);
    }
    size_t o = 0;
    for (int k = 0; k < 3; ++k)
    {
        if (o + 4 > raw.size()) return "o .PTR acaba antes das tres listas";
        const uint32_t n = rd(raw, o);
        if (n > 1000000 || o + 4 + 4 * size_t(n) > raw.size()) return "lista do .PTR maior que o arquivo";
        for (uint32_t i = 0; i < n; ++i)
        {
            const uint32_t off = rd(raw, o + 4 + 4 * size_t(i));
            if (size_t(off) + (k == 1 ? 2 : k == 2 ? 0x30 : 4) > m.ngp.size()) return fmt("ajuste %d em 0x%X fora da imagem", k, off);
            m.ptr[k].push_back(off);
        }
        o += 4 + 4 * size_t(n);
    }
    return "";
}

// texIds do .TEX da fase carregado (fileStatus.texAddr[0] ate texAddr[1]): o primeiro arquivo nao e relocado (texmRelocate soma so
// nos seguintes), entao o id no pacote e o do disco (FORMATS.md, ".TEX and .RTX files")
std::set<uint32_t> loadedTexIds(const Stage &st)
{
    std::set<uint32_t> out;
    constexpr uint32_t kFs = wotm::sym::fileStatus;
    if (st.u8(kFs + 0x38) < 1) return out;
    const uint32_t a = st.u32(kFs) & kMask, b = st.u32(kFs + 4) & kMask;
    if (!a || b <= a || b - a > 0x1000000u) return out;
    uint32_t off = a + 16 * st.u32(a + 4);
    for (int n = 0; off + 16 <= b && n < 4096; ++n)
    {
        out.insert(st.u16(off + 8));
        const uint32_t size = st.u32(off);
        if (!size) break;
        const uint32_t next = off + (size >> 2) * 16;
        if (next <= off) break;
        off = next;
    }
    return out;
}

// Maior texId dos pacotes carregados: todos os .TEX (fase e monstros, ja relocados) e .RTX de fileStatus. Uma textura propria
// fica depois dele em texInfo, onde nada da carga a sobrescreve
uint32_t maxLoadedTexId(const Stage &st)
{
    constexpr uint32_t kFs = wotm::sym::fileStatus;
    uint32_t mx = 0;
    for (const bool rtx : {false, true})
    {
        const uint32_t tab = rtx ? kFs + 0x78 : kFs;
        const int n = std::min<int>(st.u8(rtx ? kFs + 0xB0 : kFs + 0x38), 14);
        for (int i = 0; i < n; ++i)
        {
            const uint32_t a = st.u32(tab + 4 * uint32_t(i)) & kMask, nx = st.u32(tab + 4 * uint32_t(i + 1)) & kMask;
            if (!a) continue;
            const uint32_t b = nx > a && nx - a <= 0x1000000u ? nx : a + 0x1000000u;
            uint32_t off = a + 16 * st.u32(a + 4);
            for (int k = 0; off + 16 <= b && k < 4096; ++k)
            {
                mx = std::max<uint32_t>(mx, st.u16(off + 8));
                const uint32_t size = st.u32(off);
                if (!size) break;
                const uint32_t next = off + (size >> 2) * 16;
                if (next <= off) break;
                off = next;
            }
        }
    }
    return mx;
}

// Cadeias da raiz do prop ate cada folha de colisao (op 10, 11, 42), em pre-ordem na ordem dos slots (lvl.py _prop_chains)
void propChains(const Stage &st, uint32_t n, std::vector<uint32_t> &chain, std::vector<std::vector<uint32_t>> &out, int depth)
{
    if (depth > 16) return;
    const uint32_t o = st.op(n);
    chain.push_back(n);
    if (o == COLL_VOL || o == 11 || o == POLY_COLL)
        out.push_back(chain);
    else
        for (uint32_t c : st.children(n)) propChains(st, c, chain, out, depth + 1);
    chain.pop_back();
}

struct PropMade
{
    uint32_t tr, root;
    std::vector<std::vector<uint32_t>> chains;   // da raiz (ou do INTERACTIVE_STATE, no destrutivel) a cada folha
    uint32_t ist = 0, destr = 0;                 // destrutivel: os nos desta instancia
    std::vector<uint32_t> sroots;                // destrutivel: a raiz de cada estado, comum as instancias
};

// Os slots dos domos do ceu: todo OBJECT debaixo de um no de ceu (id 2) fora das nuvens (o ramo CHAR_INSTANCE, op 25), como [pai, slot] na
// ordem da arvore (lvl.py sky_plan). PatchError se a fase nao tem nenhum (CANYON1, MINISF).
std::vector<std::pair<uint32_t, uint32_t>> skyPlan(const Stage &st)
{
    std::vector<std::pair<uint32_t, uint32_t>> slots;
    std::set<uint32_t> seenSlots, seen;
    std::function<void(uint32_t)> walk = [&](uint32_t n) {
        if (!seen.insert(n).second) return;
        for (uint32_t s : st.childSlots(n))
        {
            if (!st.ok(s)) continue;
            const uint32_t c = st.u32(s) & kMask;
            if (!c || !st.ok(c)) continue;
            if (st.op(c) == OBJECT)
            {
                if (seenSlots.insert(s).second) slots.push_back({n, s});
            }
            else if (st.op(c) != CHAR_INSTANCE)
                walk(c);
        }
    };
    std::vector<uint32_t> all(st.inTree.begin(), st.inTree.end());
    std::sort(all.begin(), all.end());
    for (uint32_t n : all)
        if (st.ident(n) == 2 && st.op(n) == GROUP) walk(n);
    if (slots.empty()) throw PatchError("o ceu da fase nao tem OBJECT de domo para trocar");
    return slots;
}

// O que uma entrada de props troca no ceu (lvl.py sky_kind): 1 = domo ("sky": true), 2 = nuvens ("sky": "clouds"), 0 = poe um prop.
int skyKind(const json::Value &e)
{
    const json::Value *s = e.get("sky");
    if (!s) return 0;
    if (s->type == json::Value::Bool && s->b) return 1;
    return s->type == json::Value::Str && s->s == "clouds" ? 2 : 0;
}

// Os slots da camada de nuvens, como [pai, slot]: os OBJECTs debaixo do primeiro CHAR_INSTANCE de um no de ceu (id 2), pelo ANIM_XFORM
// dele, que continua girando o que pendurar ali (lvl.py sky_clouds_plan). PatchError se a fase nao tem.
std::vector<std::pair<uint32_t, uint32_t>> skyCloudsPlan(const Stage &st)
{
    std::vector<std::pair<uint32_t, uint32_t>> slots;
    std::set<uint32_t> seen;
    std::function<void(uint32_t)> walk = [&](uint32_t n) {
        if (!seen.insert(n).second) return;
        for (uint32_t s : st.childSlots(n))
        {
            if (!st.ok(s)) continue;
            const uint32_t c = st.u32(s) & kMask;
            if (!c || !st.ok(c)) continue;
            if (st.op(c) == OBJECT) slots.push_back({n, s});
            else walk(c);
        }
    };
    std::function<void(uint32_t)> find = [&](uint32_t n) {
        if (!slots.empty() || !seen.insert(n).second) return;
        for (uint32_t s : st.childSlots(n))
        {
            if (!st.ok(s)) continue;
            const uint32_t c = st.u32(s) & kMask;
            if (!c || !st.ok(c)) continue;
            if (st.op(c) == CHAR_INSTANCE) walk(c);
            else find(c);
            if (!slots.empty()) return;
        }
    };
    std::vector<uint32_t> all(st.inTree.begin(), st.inTree.end());
    std::sort(all.begin(), all.end());
    for (uint32_t n : all)
        if (slots.empty() && st.ident(n) == 2 && st.op(n) == GROUP) find(n);
    if (slots.empty()) throw PatchError("o ceu da fase nao tem OBJECT de camada de nuvens para trocar");
    return slots;
}

// O domo (ou, com `clouds`, a camada de nuvens) vira o modelo (lvl.py sky_swap): cada OBJECT do modelo recebe a primeira palavra, as flags
// (menos a contagem de vertices) e os campos de +0x20 a +0x4F do primeiro; o slot do primeiro aponta para a raiz do modelo e os dos outros
// domos para um GROUP vazio.
void skySwap(Stage &st, const std::vector<std::pair<uint32_t, uint32_t>> &slots, uint32_t root, const std::string &name, bool clouds = false)
{
    const uint32_t tpl = st.u32(slots[0].second) & kMask, vmask = 0x1FFFu << 16;
    std::vector<uint32_t> objs, stack{root};
    std::set<uint32_t> seen;
    while (!stack.empty())
    {
        const uint32_t n = stack.back();
        stack.pop_back();
        if (!seen.insert(n).second) continue;
        if (st.op(n) == OBJECT) objs.push_back(n);
        for (uint32_t c : st.children(n)) stack.push_back(c);
    }
    for (uint32_t o : objs)
    {
        st.w32(o, st.u32(tpl));
        st.w32(o + 0xC, (st.u32(tpl + 0xC) & ~vmask) | (st.u32(o + 0xC) & vmask));
        for (uint32_t off = 0x20; off < 0x50; off += 4) st.w32(o + off, st.u32(tpl + off));
    }
    uint32_t empty = 0;
    for (size_t i = 0; i < slots.size(); ++i)
    {
        const auto &[p, s] = slots[i];
        if (i == 0)
        {
            st.setPtr(s, root);
            st.parents[root].push_back(p);
        }
        else
        {
            if (!empty)
            {
                empty = st.alloc(0x20, 16);
                st.w32(empty, GROUP);
            }
            st.setPtr(s, empty);
            st.parents[empty].push_back(p);
        }
    }
    st.stats[clouds ? "sky clouds replaced" : "sky domes replaced"] += int64_t(slots.size());
    st.log.push_back(fmt("sky: modelo %s (%zu objetos) troca %zu %s%s (estilo do OBJECT %07x)", name.c_str(), objs.size(), slots.size(),
                         clouds ? "camada de nuvens" : "domo", slots.size() == 1 ? "" : "s", tpl));
}

void applyProps(Stage &st, uint8_t *rdram, const json::Value &patch, const std::filesystem::path &modDir, const std::string &stageName)
{
    const json::Value *props = patch.get("props");
    if (!props || props->type != json::Value::Arr || props->arr.empty()) return;
    std::string base = stageName;   // fase custom: o prop e da base (MYCITY -> CENTRAL)
    if (const std::string al = Registry::get().aliasOf("LVL/" + stageName + ".NGP"); al.size() > 8 && al.rfind("LVL/", 0) == 0)
        base = al.substr(4, al.size() - 8);
    const uint32_t start = st.arenaTop;
    struct Ent { size_t k; const json::Value *e; V3 pos; double hd, pitch, roll; };
    std::map<std::string, std::vector<Ent>> models;
    std::vector<std::string> order;
    for (size_t k = 0; k < props->arr.size(); ++k)
    {
        const json::Value &e = props->arr[k];
        try
        {
            if (e.type != json::Value::Obj) throw PatchError("nao e um objeto");
            std::string name;
            if (const json::Value *m = e.get("model"); m && m->type == json::Value::Str) name = m->s;
            for (char &c : name) c = char(std::toupper(static_cast<unsigned char>(c)));
            Ent en{k, &e, {}, 0.0, 0.0, 0.0};
            if (!skyKind(e) && !Stage::num3(e.get("pos"), en.pos))   // a entrada do ceu nao tem lugar
                throw PatchError("sem \"pos\" [x, y, z]");
            if (const json::Value *h = e.get("heading"); h && h->isInt()) en.hd = h->asNum();
            if (const json::Value *h = e.get("pitch"); h && h->isInt()) en.pitch = h->asNum();
            if (const json::Value *h = e.get("roll"); h && h->isInt()) en.roll = h->asNum();
            if (!models.count(name)) order.push_back(name);
            models[name].push_back(en);
        }
        catch (const PatchError &err)
        {
            st.log.push_back(fmt("prop %zu pulado: %s", k, err.what()));
            ++st.stats["skipped"];
        }
    }
    bool haveTex = false;
    std::set<uint32_t> texIds;
    uint32_t nextOwn = 0;                            // proximo texId de textura propria
    std::array<std::pair<uint32_t, uint32_t>, 15> bins{};   // texAllBins: (quantas, bytes) de cada caixa de VRAM da carga
    std::vector<uint32_t> objs;
    std::vector<PropMade> made;
    std::vector<std::string> summary;
    constexpr uint32_t kReserveGroup = 0x20 + 4 + 15;
    for (const std::string &name : order)
    {
        const std::vector<Ent> &ents = models[name];
        const uint32_t top = st.arenaTop;
        const int64_t iu0 = st.interUsed;
        try
        {
            const std::filesystem::path pdir = childNoCase(modDir, "props"), dir = pdir.empty() ? pdir : childNoCase(pdir, name);
            if (dir.empty() || childNoCase(dir, "prop.json").empty())
                throw PatchError(fmt("modelo %s fora de %s/props", name.c_str(), modDir.filename().string().c_str()));
            PropModel pm;
            if (const std::string e = loadPropModel(dir, pm); !e.empty()) throw PatchError(fmt("modelo %s: %s", name.c_str(), e.c_str()));
            if (pm.stage != stageName && pm.stage != base)
                throw PatchError(fmt("modelo %s e um prop de %s, nao de %s", name.c_str(), pm.stage.c_str(), stageName.c_str()));
            if (!haveTex)
            {
                haveTex = true;
                texIds = loadedTexIds(st);
                for (uint32_t n : st.inTree)
                    if (st.op(n) == OBJECT) objs.push_back(n);
                std::sort(objs.begin(), objs.end());
                st.log.push_back(fmt("props: o .TEX da fase tem %zu texturas", texIds.size()));
                nextOwn = maxLoadedTexId(st) + 1;
                for (uint32_t b = 0; b < 15; ++b)
                    bins[b] = {st.u32(wotm::sym::texAllBins + 8 * b), st.u32(wotm::sym::texAllBins + 8 * b + 4)};
            }
            // texturas proprias: o proximo texId livre e a menor caixa de VRAM em que a copia CT32 cabe (lvl.py apply_props)
            std::map<uint32_t, uint32_t> ownId, ownBin;   // slot -> texId, slot -> caixa
            std::vector<std::string> ownLines;
            uint32_t ownNext = nextOwn;
            for (const PropModel::Own &o : pm.own)
            {
                int bin = -1;
                for (uint32_t b = 0; b < 15; ++b)
                    if (bins[b].first && bins[b].second >= 4 * o.w * o.h && (bin < 0 || bins[b].second < bins[bin].second)) bin = int(b);
                if (bin < 0)
                    throw PatchError(fmt("modelo %s: imagem %s (%ux%u) maior que todas as caixas de textura da fase", name.c_str(), o.png.c_str(),
                                         o.w, o.h));
                if (ownNext >= 999) throw PatchError(fmt("modelo %s: sem texId livre para %s (o 999 e o do filme)", name.c_str(), o.png.c_str()));
                ownId[o.slot] = ownNext++;
                ownBin[o.slot] = uint32_t(bin);
                ownLines.push_back(fmt("props: %s %s: textura propria %ux%u, tex%u, caixa de %u bytes", name.c_str(), o.png.c_str(), o.w, o.h,
                                       ownId[o.slot], bins[bin].second));
            }
            std::map<uint32_t, std::pair<uint32_t, uint32_t>> ctxFrom;   // texId -> (OBJECT, seu gsTexCtx com ele) (lvlprop.stage_ctx)
            for (const auto &[slot, tid] : pm.texOf)
            {
                if (!texIds.count(tid)) throw PatchError(fmt("modelo %s: tex%u nao esta no %s.TEX", name.c_str(), tid, stageName.c_str()));
                for (size_t i = 0; i < objs.size() && !ctxFrom.count(tid); ++i)
                    for (uint32_t k = 0, o = objs[i]; k < std::min<uint32_t>(st.u16(o + 0x5A), 5); ++k)
                        if (st.u16(o + 0x50 + 2 * k) == tid)
                        {
                            ctxFrom[tid] = {o, o + 0x70 + 0x30 * k};
                            break;
                        }
                if (!ctxFrom.count(tid)) throw PatchError(fmt("modelo %s: nenhum OBJECT da fase desenha com tex%u", name.c_str(), tid));
            }
            for (int k = 1; k < 3; ++k)
                for (uint32_t off : pm.ptr[k])
                {
                    uint32_t slot = 0;
                    std::memcpy(&slot, pm.ngp.data() + off, k == 1 ? 2 : 4);
                    if (!pm.texOf.count(slot) && (k == 2 || !ownId.count(slot)))
                        throw PatchError(fmt("modelo %s: slot %u sem textura no prop.json", name.c_str(), slot));
                }
            // a imagem na arena: ponteiros relocados, slots trocados pelo texId e o contexto vivo
            const uint32_t at = st.alloc(uint32_t(pm.ngp.size()), 0x80), delta = at - kBase;
            std::memcpy(rdram + (at & PS2_RAM_MASK), pm.ngp.data(), pm.ngp.size());
            st.w32(at + 4, st.u32(at + 4) + delta);
            for (uint32_t off : pm.ptr[0]) st.w32(at + off, st.u32(at + off) + delta);
            for (uint32_t off : pm.ptr[1])
            {
                const uint32_t slot = st.u16(at + off);
                st.w16(at + off, uint16_t(pm.texOf.count(slot) ? pm.texOf[slot] : ownId[slot]));
            }
            for (uint32_t off : pm.ptr[2]) st.copyBytes(at + off, ctxFrom[pm.texOf[st.u32(at + off)]].second, 0x30);
            // textura propria: o pacote ganha o texId e a caixa, e o texInfo dele aponta para o pacote, fora da VRAM (o primeiro
            // desenho a sobe, como as do .TEX: texmActivateTexture)
            for (const PropModel::Own &o : pm.own)
            {
                const uint32_t pk = at + o.packet, id = ownId[o.slot], bin = ownBin[o.slot];
                st.w16(pk + 8, uint16_t(id));
                st.w32(pk + 0xC, (st.u32(pk + 0xC) & 0x07FFFFFFu) | bin << 27);
                const uint32_t ti = wotm::sym::texInfo + 16 * id;
                st.w32(ti, pk);
                st.w32(ti + 4, (o.w <= 64 ? 1u : o.w / 64) << 14 | bin << 20);
                st.w32(ti + 8, 0);
                st.w32(ti + 12, 0);
            }
            const uint32_t root = at + pm.root;
            const bool dstr = !pm.states.empty();
            std::vector<uint32_t> sroots;
            std::vector<std::vector<uint32_t>> chains;
            std::vector<uint32_t> chain;
            if (dstr)
                for (uint32_t o : pm.states) sroots.push_back(at + o);
            else
                sroots.push_back(root);
            for (uint32_t r : sroots) propChains(st, r, chain, chains, 0);   // um estado por vez (a unica raiz de um prop estatico)
            const uint32_t nSt = uint32_t(sroots.size());
            std::vector<PropMade> mine;
            std::vector<std::pair<std::vector<std::pair<uint32_t, uint32_t>>, bool>> skyJobs;   // (slots, nuvens?)
            int64_t need = 0;
            for (const Ent &en : ents)
            {
                if (const int sk = skyKind(*en.e))
                {
                    try
                    {
                        if (dstr) throw PatchError("um modelo de ceu nao tem estados destrutiveis");
                        for (const auto &j : skyJobs)
                            if (j.second != (sk == 2)) throw PatchError("um modelo nao pode ser o domo e as nuvens");
                        skyJobs.push_back({sk == 2 ? skyCloudsPlan(st) : skyPlan(st), sk == 2});
                    }
                    catch (const PatchError &err)
                    {
                        st.log.push_back(fmt("sky pulado: modelo %s: %s", name.c_str(), err.what()));
                        ++st.stats["skipped"];
                    }
                    continue;
                }
                const uint32_t tTop = st.arenaTop;
                const bool turn = std::fabs(std::remainder(en.hd, 360.0)) > 1e-6 || std::fabs(std::remainder(en.pitch, 360.0)) > 1e-6 ||
                                  std::fabs(std::remainder(en.roll, 360.0)) > 1e-6;
                const uint32_t t = st.alloc(turn ? 0x54 : 0x20, 16);
                uint32_t ist = 0, destr = 0;
                std::vector<uint32_t> instRoots = sroots;
                std::vector<std::vector<uint32_t>> instChains = chains;
                int64_t smokeCost = 0;
                if (dstr && !pm.smoke.empty())
                {   // um emissor comeca o efeito uma vez, na primeira vez que e desenhado, e anota isso no proprio no: cada copia do prop
                    // precisa dos seus, num grupo de estado proprio (as malhas continuam compartilhadas)
                    for (const auto &[si, blobs] : pm.smoke)
                    {
                        const std::vector<uint32_t> kids = st.children(sroots[si]);
                        std::vector<uint32_t> all = kids;
                        for (const std::vector<uint8_t> &b : blobs)
                        {
                            const uint32_t en = st.alloc(0xB0, 16);
                            for (size_t o = 0; o < b.size(); o += 4)
                            {
                                uint32_t w;
                                std::memcpy(&w, b.data() + o, 4);
                                st.w32(en + uint32_t(o), w);
                            }
                            all.push_back(en);
                        }
                        const uint32_t gr = st.alloc(0x20 + 4 * uint32_t(all.size()), 16);
                        for (uint32_t o = 0; o < 0x20; o += 4) st.w32(gr + o, st.u32(sroots[si] + o));
                        st.w16(gr + 8, uint16_t(all.size()));
                        for (size_t j = 0; j < all.size(); ++j) st.w32(gr + 0x20 + 4 * uint32_t(j), all[j]);
                        instRoots[si] = gr;
                        st.stats["smoke emitters"] += int64_t(blobs.size());
                        smokeCost += 0x20 + 4 * int64_t(all.size()) + 15 + int64_t(blobs.size()) * (0xB0 + 15);
                    }
                    instChains.clear();
                    std::vector<uint32_t> c0;
                    for (uint32_t r : instRoots) propChains(st, r, c0, instChains, 0);
                }
                if (dstr)
                {   // T|R -> INTERACTIVE_STATE -> DESTRUCTIBLE -> uma raiz por estado (FORMATS.md); o limiar de cada estado e a vida que resta
                    ist = st.alloc(0x10, 16);
                    destr = st.alloc(0xC + 12 * nSt, 16);
                    st.w32(ist, INTERACTIVE);
                    st.w32(ist + 0xC, destr);
                    st.w32(destr, DESTRUCTIBLE);
                    st.w16(destr + 4, uint16_t(pm.strength));
                    st.w16(destr + 0xA, uint16_t(nSt));
                    for (uint32_t i = 0; i < nSt; ++i)
                    {
                        st.w32(destr + 0xC + 12 * i, instRoots[i]);
                        st.w16(destr + 0xC + 12 * i + 8, i + 1 < nSt ? uint16_t(std::lround(65535.0 * (nSt - 1 - i) / nSt)) : 0);
                        if (pm.actions[i]) st.w32(destr + 0xC + 12 * i + 4, at + pm.actions[i]);   // o ActDebris do estado, na imagem do prop
                    }
                }
                const uint32_t below = ist ? ist : root;
                if (turn)
                {
                    // Rx(pitch) * Ry(roll) * Rz(heading), em linhas (lvl.py prop_matrix)
                    const double cp = std::cos(en.pitch * kPi / 180.0), sp = std::sin(en.pitch * kPi / 180.0);
                    const double cr = std::cos(en.roll * kPi / 180.0), sr = std::sin(en.roll * kPi / 180.0);
                    const double cs = std::cos(en.hd * kPi / 180.0), sn = std::sin(en.hd * kPi / 180.0);
                    const double tilt[3][3] = {{cr, 0, -sr}, {sp * sr, cp, sp * cr}, {cp * sr, -sp, cp * cr}};
                    double rows[4][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {en.pos.v[0], en.pos.v[1], en.pos.v[2], 1}};
                    for (int r = 0; r < 3; ++r)
                    {
                        rows[r][0] = tilt[r][0] * cs - tilt[r][1] * sn;
                        rows[r][1] = tilt[r][0] * sn + tilt[r][1] * cs;
                        rows[r][2] = tilt[r][2];
                    }
                    st.w32(t, ROTATE);
                    st.w32(t + 8, 1);
                    for (int r = 0; r < 4; ++r)
                        for (int c = 0; c < 4; ++c) st.wf32(t + 0x10 + 16 * r + 4 * c, rows[r][c]);
                    st.w32(t + 0x50, below);
                }
                else
                {
                    st.w32(t, TRANSLATE);
                    st.w32(t + 8, 1);
                    for (int i = 0; i < 3; ++i) st.wf32(t + 0x10 + 4 * i, en.pos.v[i]);
                    st.w32(t + 0x1C, below);
                }
                int64_t cost = 4;
                std::string bad;
                if (dstr)
                {
                    cost += 0x10 + 15 + 0xC + 12 * nSt + 15 + smokeCost;
                    if (st.interUsed + 1 > kInteractives)
                        bad = fmt("a fase nao tem vaga de Interactives (%lld de %lld ocupadas)", (long long)st.interUsed, (long long)kInteractives);
                }
                std::vector<std::vector<uint32_t>> full;
                for (const std::vector<uint32_t> &c0 : instChains)
                {
                    std::vector<uint32_t> c1;
                    if (dstr) c1 = {ist, destr};
                    c1.insert(c1.end(), c0.begin(), c0.end());
                    full.push_back(std::move(c1));
                }
                for (const std::vector<uint32_t> &ch : full)
                {
                    if (!bad.empty()) break;
                    Box lb, bb;
                    if (!aabb(st.leafPoints(ch.back()), lb)) { bad = fmt("folha %07x sem pontos", ch.back()); break; }
                    std::vector<uint32_t> nodes{t};
                    nodes.insert(nodes.end(), ch.begin(), ch.end());
                    const M4 m = st.chainMatrix(nodes, nodes.size() - 1, false);
                    std::vector<V3> w;
                    for (const V3 &c : corners(lb)) w.push_back(xform(m, c));
                    aabb(w, bb);
                    if (bb.mn.v[0] < st.minX || bb.mn.v[1] < st.minY || bb.mx.v[0] > st.maxX || bb.mx.v[1] > st.maxY)
                    {
                        bad = fmt("caixa (%.1f %.1f)-(%.1f %.1f) fora da grade (%.1f %.1f)-(%.1f %.1f)", bb.mn.v[0], bb.mn.v[1], bb.mx.v[0],
                                  bb.mx.v[1], st.minX, st.minY, st.maxX, st.maxY);
                        break;
                    }
                    cost += 0x24 + 4 * int64_t(ch.size() + 3) + 3 + 8 * int64_t(st.cellsFor(bb).size());
                }
                if (!bad.empty())
                {
                    st.rollback(tTop);
                    const json::Value *id = en.e->get("id");
                    st.log.push_back(fmt("prop %zu (%s) pulado: %s", en.k, id && id->type == json::Value::Str && !id->s.empty() ? id->s.c_str() : name.c_str(),
                                         bad.c_str()));
                    ++st.stats["skipped"];
                    continue;
                }
                mine.push_back({t, root, full, ist, destr, dstr ? instRoots : std::vector<uint32_t>{}});
                need += cost;
                if (dstr) ++st.interUsed;
            }
            if (mine.empty() && skyJobs.empty())
            {
                st.rollback(top);
                st.interUsed = iu0;
                continue;
            }
            if (need + (made.empty() ? kReserveGroup : 0) + (skyJobs.empty() ? 0 : 0x20 + 15) > int64_t(st.arenaFree()))
            {
                st.rollback(top);
                st.interUsed = iu0;
                throw PatchError(fmt("modelo %s: %zu entradas levam ate %lld bytes a mais, a arena tem %u livres", name.c_str(), mine.size(),
                                     (long long)need, st.arenaFree()));
            }
            for (const auto &[job, clouds] : skyJobs) skySwap(st, job, root, name, clouds);   // por ultimo, quando nada mais pode falhar: os slots passam a apontar para a arena
            made.insert(made.end(), mine.begin(), mine.end());
            summary.push_back(fmt("%s x%zu (%zu bytes)", name.c_str(), mine.size(), pm.ngp.size()));
            if (dstr)
                st.log.push_back(fmt("props: %s e destrutivel: %u estados, classe de vida %u (%u pontos)", name.c_str(), nSt, pm.strength,
                                     kStrengthHp[pm.strength]));
            for (const auto &[si, blobs] : pm.smoke)
                st.log.push_back(fmt("props: %s tem fumaca: %zu emissores no estado %u", name.c_str(), blobs.size(), si));
            ++st.stats["prop models"];
            for (const auto &[slot, tid] : pm.texOf) st.log.push_back(fmt("props: %s tex%u <- contexto do OBJECT %07x", name.c_str(), tid, ctxFrom[tid].first));
            st.log.insert(st.log.end(), ownLines.begin(), ownLines.end());
            nextOwn = ownNext;
            {
                const std::lock_guard<std::mutex> lk(g_propTexMu);
                for (const PropModel::Own &o : pm.own)
                    if (const std::filesystem::path png = childNoCase(dir, o.png); !png.empty())
                        g_propTex[ownId[o.slot]] = {png.string(), modDir.filename().string(), "props/" + name + "/" + o.png};
                    else
                        st.log.push_back(fmt("props: %s sem o %s: fica a copia %ux%u do jogo", name.c_str(), o.png.c_str(), o.w, o.h));
            }
        }
        catch (const PatchError &err)
        {
            if (st.arenaTop != top) st.rollback(top);
            st.interUsed = iu0;
            st.log.push_back(fmt("props do modelo %s pulados (%zu): %s", name.empty() ? "?" : name.c_str(), ents.size(), err.what()));
            st.stats["skipped"] += int(ents.size());
        }
    }
    if (made.empty()) return;
    uint32_t world = 0;
    for (uint32_t r : st.roots)
        if (st.ident(r) == 1 && st.op(r) == GROUP) world = r;
    const std::vector<uint32_t> slots = world ? st.childSlots(world) : std::vector<uint32_t>{};
    if (slots.empty())
    {   // o lvl.py nao tem este caso (le o GROUP do mundo de toda fase do disco); fica a imagem na arena, sem pendurar nada
        st.log.push_back(fmt("props: %zu entradas sem lugar (a fase nao tem GROUP do mundo com filho)", made.size()));
        return;
    }
    const uint32_t slot = slots[0], old = st.u32(slot) & kMask;
    const uint32_t wrap = st.alloc(0x20 + 4 * uint32_t(1 + made.size()), 16);
    st.w32(wrap, GROUP);
    st.w32(wrap + 4, st.u32(world + 4));
    st.w16(wrap + 8, uint16_t(1 + made.size()));
    st.w16(wrap + 0xA, 7);   // visual | collision | noFOV
    st.w32(wrap + 0x20, old);
    for (size_t i = 0; i < made.size(); ++i) st.w32(wrap + 0x24 + 4 * uint32_t(i), made[i].tr);
    st.setPtr(slot, wrap);
    if (!g_breakSteps.empty()) g_breakWraps.push_back(wrap);   // WOTM_LEVELPATCH_BREAK:copies quebra tambem os destrutiveis dos props
    st.parents[wrap] = {world};
    st.parents[old] = {wrap};
    for (const PropMade &pm : made)
    {
        st.parents[pm.tr] = {wrap};
        if (pm.ist)
        {
            st.parents[pm.ist] = {pm.tr};
            st.parents[pm.destr] = {pm.ist};
            for (uint32_t r : pm.sroots) st.parents[r].push_back(pm.destr);
        }
        else
            st.parents[pm.root].push_back(pm.tr);
        for (const std::vector<uint32_t> &ch : pm.chains)
        {
            std::vector<uint32_t> nodes{world, wrap, pm.tr};
            nodes.insert(nodes.end(), ch.begin(), ch.end());
            const uint32_t pa = st.alloc(0x24 + 4 * uint32_t(nodes.size()));
            st.w32(pa + 0x1C, uint32_t(nodes.size()));
            for (size_t i = 0; i < nodes.size(); ++i) st.w32(pa + 0x24 + 4 * uint32_t(i), nodes[i]);
            std::vector<V3> w;
            Box bb;
            st.pathCorners(st.path(pa), w);
            aabb(w, bb);
            st.footprints.push_back({hull2d(w), bb.mn.v[2], bb.mx.v[2]});
            for (int i = 0; i < 3; ++i) st.wf32(pa + 4 * i, bb.mn.v[i]), st.wf32(pa + 0xC + 4 * i, bb.mx.v[i]);
            const std::set<int> cs = st.cellsFor(bb);
            st.setCells(pa, cs);
            ++st.stats["prop paths"];
            st.pathLines.push_back(fmt("caminho %07x  caixa (%.1f %.1f %.1f)-(%.1f %.1f %.1f)  %zu celulas  prop", pa, bb.mn.v[0], bb.mn.v[1],
                                       bb.mn.v[2], bb.mx.v[0], bb.mx.v[1], bb.mx.v[2], cs.size()));
        }
        ++st.stats["props"];
    }
    std::string sum;
    for (const std::string &s : summary) sum += (sum.empty() ? "" : ", ") + s;
    st.log.push_back(fmt("props: %s; %u bytes da arena", sum.c_str(), st.arenaTop - start));
    // Folga da arena e das cargas altas (monstros, texturas e .PTR acima de 32 MB param no comeco da arena, kHighCeiling): o fim da carga
    // e o maior endereco das tres listas do fileStatus (.TEX, .NGP, .RTX), como no wotm_mods_hooks::ptrRelocate
    constexpr uint32_t kFs = wotm::sym::fileStatus;
    const uint32_t texN = st.u8(kFs + 0x38), ngpN = st.u8(kFs + 0x74), resN = st.u8(kFs + 0xB0);
    uint32_t end = ngpN <= 13 ? st.u32(kFs + 0x3C + 4 * ngpN) & 0x1FFFFFFFu : 0;
    if (texN <= 13) end = std::max(end, st.u32(kFs + 4 * texN) & 0x1FFFFFFFu);
    if (resN <= 13) end = std::max(end, st.u32(kFs + 0x78 + 4 * resN) & 0x1FFFFFFFu);
    st.log.push_back(fmt("props: arena %u de %u bytes usados; a carga vai ate 0x%07X (%s, %u KB abaixo da arena)", st.arenaTop - kArena,
                         kArenaEnd - kArena, end, end > PS2_GUEST_RAM_SIZE ? "acima de 32 MB" : "abaixo de 32 MB",
                         end < kArena ? (kArena - end) / 1024 : 0));
}
