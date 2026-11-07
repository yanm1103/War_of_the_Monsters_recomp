// ABOUTME: Pedestres do patch de fase ("peds" em mods/<mod>/levels/<FASE>.json, MODS.md "Level patches"): rede de calcadas trocada,
// ABOUTME: grupos do disco movidos, recontados ou apagados, grupos novos. Espelho de tools/lvl.py (LevelPatch.apply_peds e ped_report).
//
// Incluido por wotm_levelpatch.inc dentro do namespace wotm_levelpatch, depois de cutNets (usa Stage, NetNode, readNet e writeNet).
// Ordem no applyFor, a mesma do lvl.py patch: Stage::apply (objetos), applyPeds, cutNets, pedReport. Um grupo e um TRANSLATE raiz de
// id 0xBB8 (cabeca 0xBB8<<18 | tamanho<<7 | 3, um filho NULL); o construtor do PedGroup, na travessia do dbInitDb, anda no link 0 do
// no da PathNet::ped mais perto dele (FORMATS.md, ids de no). Aqui a rede ja esta na RAM e nenhum PedGroup existe ainda.

constexpr uint32_t kPedGroupId = 0xBB8;
constexpr size_t kPedMaxGroups = 20, kPedMaxSegments = kNetNodes / 2;

struct PedGroupInfo { uint32_t node; V3 pos; unsigned count; };

std::vector<PedGroupInfo> pedGroups(const Stage &st, const std::vector<uint32_t> &extra = {})
{
    std::vector<PedGroupInfo> out;
    auto add = [&](uint32_t r) {
        const uint32_t w = st.u32(r);
        if (w >> 18 == kPedGroupId && (w & 63) == TRANSLATE)
            out.push_back({r, {{st.f32(r + 0x10), st.f32(r + 0x14), st.f32(r + 0x18)}}, (w >> 7) & 0xFF});
    };
    for (uint32_t r : st.roots) add(r);
    for (uint32_t r : extra) add(r);
    return out;
}

// Sem .PED o exportPoints monta 2 nos em (-100, 0, 250) e (100, 0, 250): para o patch, a fase nao tem rede de pedestres
bool isDefaultPedNet(const std::vector<NetNode> &net)
{
    auto at = [](const NetNode &n, double x) { return std::fabs(n.pos.v[0] - x) < 0.01 && std::fabs(n.pos.v[1]) < 0.01 && std::fabs(n.pos.v[2] - 250.0) < 0.01; };
    return net.size() == 2 && at(net[0], -100.0) && at(net[1], 100.0);
}

// O que o construtor do PedGroup anda: o no mais perto (distancia 3D, qualquer tipo, o primeiro dos iguais) e o seu link 0
bool pedBind(const std::vector<NetNode> &net, const V3 &pos, size_t &a, size_t &b)
{
    size_t best = net.size();
    double bd = 0.0;
    for (size_t i = 0; i < net.size(); ++i)
        if (const double d = dist(net[i].pos, pos); best == net.size() || d < bd) best = i, bd = d;
    if (best == net.size() || net[best].links.empty()) return false;
    a = best, b = net[best].links[0].node;
    return true;
}

// Aplica o "peds" do patch. Devolve os grupos novos (para o relatorio). Mexe na PathNet::ped da RAM quando ha "segments".
std::vector<uint32_t> applyPeds(Stage &st, uint8_t *rdram, const json::Value &patch)
{
    std::vector<uint32_t> made;
    const json::Value *peds = patch.get("peds");
    if (!peds || peds->type != json::Value::Obj) return made;
    std::vector<NetNode> net = readNet(rdram, kPedNet);
    bool hasNet = !net.empty() && !isDefaultPedNet(net);
    if (const json::Value *segs = peds->get("segments"); segs && segs->type == json::Value::Arr)
    {
        NetNode tmpl{};
        if (!net.empty()) tmpl = net[0];
        else { const float one = 1.0f; std::memcpy(&tmpl.raw[0xC], &one, 4); }
        tmpl.raw[0x10] = 0;   // tipo 0
        size_t n = segs->arr.size();
        if (n > kPedMaxSegments)
        {
            st.log.push_back(fmt("pedestres: %zu segmentos, ficam %zu (256 nos)", n, kPedMaxSegments));
            n = kPedMaxSegments;
        }
        std::vector<NetNode> out;
        size_t used = 0;
        for (size_t k = 0; k < n; ++k)
        {
            const json::Value &s = segs->arr[k];
            V3 a, b;
            if (!Stage::num3(s.get("a"), a) || !Stage::num3(s.get("b"), b))
            {
                st.log.push_back(fmt("segmento de pedestres %zu pulado: sem \"a\"/\"b\" [x, y, z]", k));
                ++st.stats["skipped"];
                continue;
            }
            const json::Value *wv = s.get("width");
            const uint8_t w = uint8_t(std::clamp<int64_t>(wv && wv->isInt() ? int64_t(wv->asNum()) : 4, 1, 255));
            const uint16_t m = uint16_t(out.size());
            NetNode na = tmpl, nb = tmpl;
            na.pos = a, nb.pos = b, na.type = nb.type = 0;
            na.links = {{uint16_t(m + 1), w, 0}}, nb.links = {{m, w, 0}};
            out.push_back(na), out.push_back(nb);
            ++used;
        }
        net = out;
        hasNet = !net.empty();
        writeNet(rdram, kPedNet, net);
        st.stats["ped segments"] = int(used);
    }

    std::vector<PedGroupInfo> disk = pedGroups(st);
    std::set<uint32_t> alive;
    for (const PedGroupInfo &g : disk) alive.insert(g.node);
    const json::Value *offv = peds->get("off");
    const bool off = offv && offv->asBool();
    auto clear = [&](uint32_t n) { st.w32(n, st.u32(n) & 0x3FFFFu); alive.erase(n); };   // id 0: um TRANSLATE comum
    auto check = [&](const V3 &pos, int64_t count) {
        if (count < 1 || count > 255) throw PatchError(fmt("tamanho %lld fora de 1..255", (long long)count));
        if (!hasNet) throw PatchError("a fase nao tem rede de pedestres");
        size_t a, b;
        if (!pedBind(net, pos, a, b))
            throw PatchError(fmt("sem calcada perto de (%.0f %.0f %.0f): o no mais perto nao tem link", pos.v[0], pos.v[1], pos.v[2]));
    };
    auto countOf = [](const json::Value &e, int64_t def) {
        const json::Value *c = e.get("count");
        return c && c->isInt() ? int64_t(c->asNum()) : def;
    };

    if (off)
    {
        for (const PedGroupInfo &g : disk) clear(g.node);
        st.stats["ped groups off"] = int(disk.size());
    }
    std::vector<std::pair<size_t, const json::Value *>> fresh;
    const json::Value *groups = peds->get("groups");
    for (size_t k = 0; groups && groups->type == json::Value::Arr && k < groups->arr.size(); ++k)
    {
        const json::Value &e = groups->arr[k];
        try
        {
            const json::Value *hv = e.get("handle");
            if (!hv || hv->type == json::Value::Null)
            {
                if (!off) fresh.push_back({k, &e});
                continue;
            }
            uint32_t h = 0;
            if (hv->type == json::Value::Str) h = uint32_t(std::strtoul(hv->s.c_str(), nullptr, 0));
            else if (hv->isInt()) h = uint32_t(hv->asInt());
            auto it = std::find_if(disk.begin(), disk.end(), [&](const PedGroupInfo &g) { return g.node == h; });
            if (it == disk.end()) throw PatchError(fmt("%07x nao e um grupo de pedestres da fase", h));
            V3 from;
            if (!Stage::num3(e.get("from"), from)) throw PatchError("sem \"from\" [x, y, z]");
            for (int i = 0; i < 3; ++i)
                if (std::fabs(it->pos.v[i] - from.v[i]) > kFromTol)
                    throw PatchError(fmt("o grupo %07x esta em (%.1f %.1f %.1f), nao no \"from\" (%.1f %.1f %.1f): outro .NGP?", h, it->pos.v[0],
                                         it->pos.v[1], it->pos.v[2], from.v[0], from.v[1], from.v[2]));
            const json::Value *del = e.get("delete");
            if (off || (del && del->asBool()))
            {
                if (alive.count(h)) clear(h), ++st.stats["ped groups deleted"];
                continue;
            }
            V3 pos = it->pos;
            if (e.get("pos") && !Stage::num3(e.get("pos"), pos)) throw PatchError("\"pos\" nao e [x, y, z]");
            const int64_t count = countOf(e, it->count);
            check(pos, count);
            for (int i = 0; i < 3; ++i) st.wf32(h + 0x10 + 4 * i, pos.v[i]);
            st.w32(h, (st.u32(h) & ~(0xFFu << 7)) | (uint32_t(count) << 7));
            ++st.stats["ped groups changed"];
        }
        catch (const PatchError &err)
        {
            st.log.push_back(fmt("grupo de pedestres %zu pulado: %s", k, err.what()));
            ++st.stats["skipped"];
        }
    }
    for (const auto &[k, e] : fresh)
    {
        try
        {
            if (alive.size() + made.size() >= kPedMaxGroups) throw PatchError(fmt("no maximo %zu grupos", kPedMaxGroups));
            V3 pos;
            if (!Stage::num3(e->get("pos"), pos)) throw PatchError("sem \"pos\" [x, y, z]");
            const int64_t count = countOf(*e, 50);
            check(pos, count);
            const uint32_t nul = st.alloc(0x10, 16);
            st.w32(nul, NUL);
            const uint32_t t = st.alloc(0x20, 16);
            st.w32(t, (kPedGroupId << 18) | (uint32_t(count) << 7) | TRANSLATE);
            st.w32(t + 8, 1);
            for (int i = 0; i < 3; ++i) st.wf32(t + 0x10 + 4 * i, pos.v[i]);
            st.w32(t + 0x1C, nul);
            made.push_back(t);
        }
        catch (const PatchError &err)
        {
            st.log.push_back(fmt("grupo de pedestres %zu pulado: %s", k, err.what()));
            ++st.stats["skipped"];
        }
    }
    if (!made.empty())
    {
        // Os novos ficam pendurados no GROUP do mundo (id 1): um GROUP novo no seu primeiro slot segura o filho antigo e eles
        uint32_t world = 0;
        for (uint32_t r : st.roots)
            if (st.ident(r) == 1 && st.op(r) == GROUP) world = r;
        const std::vector<uint32_t> slots = world ? st.childSlots(world) : std::vector<uint32_t>{};
        if (slots.empty())
        {
            st.log.push_back(fmt("pedestres: %zu grupos novos sem lugar (a fase nao tem GROUP do mundo com filho)", made.size()));
            st.stats["skipped"] += int(made.size());
            made.clear();
            return made;
        }
        const uint32_t slot = slots[0], old = st.u32(slot) & kMask;
        const uint32_t wrap = st.alloc(0x20 + 4 * uint32_t(1 + made.size()), 16);
        st.w32(wrap, GROUP);
        st.w32(wrap + 4, st.u32(world + 4));
        st.w16(wrap + 8, uint16_t(1 + made.size()));
        st.w16(wrap + 0xA, 7);   // visual | collision | noFOV
        st.w32(wrap + 0x20, old);
        for (size_t i = 0; i < made.size(); ++i) st.w32(wrap + 0x24 + 4 * uint32_t(i), made[i]);
        st.setPtr(slot, wrap);
        st.stats["ped groups added"] = int(made.size());
    }
    return made;
}

// Depois do cutNets: em que segmento cada grupo vivo anda, e os segmentos sem ninguem
std::vector<std::string> pedReport(const Stage &st, const uint8_t *rdram, const std::vector<uint32_t> &made)
{
    std::vector<std::string> lines;
    const std::vector<NetNode> net = readNet(rdram, kPedNet);
    const bool deflt = net.empty() || isDefaultPedNet(net);
    const std::vector<PedGroupInfo> groups = pedGroups(st, made);
    std::set<std::pair<size_t, size_t>> used;
    for (const PedGroupInfo &g : groups)
    {
        size_t a, b;
        if (!pedBind(net, g.pos, a, b) || b >= net.size())
        {
            lines.push_back(fmt("pedestres: grupo %07x (%u) em (%.1f %.1f %.1f) sem calcada", g.node, g.count, g.pos.v[0], g.pos.v[1], g.pos.v[2]));
            continue;
        }
        used.insert({std::min(a, b), std::max(a, b)});
        lines.push_back(fmt("pedestres: grupo %07x (%u) em (%.1f %.1f %.1f) anda em %zu-%zu (%.0f de comprimento)%s", g.node, g.count, g.pos.v[0],
                            g.pos.v[1], g.pos.v[2], a, b, dist(net[a].pos, net[b].pos), deflt ? " na rede padrao" : ""));
    }
    std::string empty;
    std::set<std::pair<size_t, size_t>> seen;
    for (size_t i = 0; i < net.size(); ++i)
    {
        if (net[i].links.empty()) continue;
        const size_t k = net[i].links[0].node;
        const std::pair<size_t, size_t> key{std::min(i, k), std::max(i, k)};
        if (k >= net.size() || !seen.insert(key).second) continue;
        if (!used.count(key)) empty += fmt("%s%zu-%zu", empty.empty() ? "" : ", ", i, k);
    }
    if (!groups.empty() && !empty.empty()) lines.push_back("pedestres: ninguem anda em " + empty);
    return lines;
}
