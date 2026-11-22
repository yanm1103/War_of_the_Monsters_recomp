// Sopro proprio de custom ("breathFx"; wotm_breathfx.hpp, MODS.md): simulacao das camadas na thread de apresentacao. A cada quadro
// desenhado le de wotm::breathfx::g_live (gravado pelo gancho do FireBreath::TestCollis, wotm_custom.inc) quem tem sopro proprio,
// e da foto da RAM do quadro a boca do monstro (+0x3E50, eixos +0x33C0/+0x33D0/+0x33E0, o +0x33D0 e a frente do bafo) e o
// FireBreath (Monster +0x68C0: estado 1 = soprando, +0x98 tempo restante em fields, +0x9C alcance). A simulacao anda pelos quadros
// do jogo (Snapshot::g, 60 por segundo): pausado, as particulas param.
namespace wotm_breathfx
{
    constexpr uint32_t kGameObj = wotm::sym::shellGame, kMonsters = 0x120380;   // TheGame::m_monsters[8]
    constexpr uint32_t kMuzzle = 0x3E50, kAxisS = 0x33C0, kAxisF = 0x33D0, kAxisU = 0x33E0;
    constexpr uint32_t kBreath = 0x68C0, kBreathState = 0x0, kBreathTime = 0x98, kBreathRange = 0x9C;

    struct Part { float age, life, size, rot; float p[3], v[3]; };   // p, v: (frente, lado, cima) da boca se relativa, senao mundo
    struct LayerState { std::vector<Part> parts; float acc = 0.f; bool fired = false; };
    struct Emitter
    {
        uint32_t m = 0;
        const wotm::breathfx::Config *cfg = nullptr;
        std::vector<LayerState> layers;
        uint32_t rng = 0x9E3779B9u;
    };
    Emitter g_em[8];
    uint32_t g_lastG = 0;

    float frand(uint32_t &s)
    {
        s = s * 1664525u + 1013904223u;
        return float(s >> 8) * (1.f / 16777216.f);
    }
    float dot3v(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
    void norm3(float *a)
    {
        const float l = std::sqrt(dot3v(a, a));
        if (l > 1e-6f)
            for (int k = 0; k < 3; ++k) a[k] /= l;
    }
    uint32_t gsColor(const wotm::breathfx::Layer &L, float t)
    {
        const wotm::breathfx::Poly *p[4] = {&L.r, &L.g, &L.b, &L.a};
        uint32_t out = 0;
        for (int k = 0; k < 4; ++k)
        {
            const float c = std::clamp(L.color[k] * p[k]->at(t), 0.f, 2.f);
            out |= uint32_t(std::min(255.f, c * 128.f + 0.5f)) << (8 * k);
        }
        return out;
    }

    // Boca do monstro: origem e base ortonormal (f = frente do bafo, s = lado, u = cima)
    struct Frame { float o[3], f[3], s[3], u[3]; };
    Frame frameOf(const wotm::Ram &ram, uint32_t m)
    {
        Frame fr;
        float s0[3], u0[3];
        for (int k = 0; k < 3; ++k)
        {
            fr.o[k] = ram.f32(m + kMuzzle + 4 * k);
            fr.f[k] = ram.f32(m + kAxisF + 4 * k);
            s0[k] = ram.f32(m + kAxisS + 4 * k);
            u0[k] = ram.f32(m + kAxisU + 4 * k);
        }
        norm3(fr.f);
        const float d = dot3v(s0, fr.f);
        for (int k = 0; k < 3; ++k) fr.s[k] = s0[k] - d * fr.f[k];
        norm3(fr.s);
        fr.u[0] = fr.s[1] * fr.f[2] - fr.s[2] * fr.f[1];
        fr.u[1] = fr.s[2] * fr.f[0] - fr.s[0] * fr.f[2];
        fr.u[2] = fr.s[0] * fr.f[1] - fr.s[1] * fr.f[0];
        if (dot3v(fr.u, u0) < 0.f)
            for (int k = 0; k < 3; ++k) fr.u[k] = -fr.u[k];
        return fr;
    }
    void toWorld(const Frame &fr, const float *l, float *w)
    {
        for (int k = 0; k < 3; ++k) w[k] = fr.o[k] + l[0] * fr.f[k] + l[1] * fr.s[k] + l[2] * fr.u[k];
    }

    void spawn(Emitter &e, const wotm::breathfx::Layer &L, LayerState &st, float scale, const Frame &fr, const float *hitPt, float age0)
    {
        Part q;
        q.life = L.lifeMin + (L.lifeMax - L.lifeMin) * frand(e.rng);
        q.size = (L.sizeMin + (L.sizeMax - L.sizeMin) * frand(e.rng)) * scale;
        q.rot = L.rotate ? frand(e.rng) * 6.2831853f : 0.f;
        float off[3];   // (frente, lado, cima)
        for (int k = 0; k < 3; ++k) off[k] = (2.f * frand(e.rng) - 1.f) * L.spread * scale;
        float vl[3] = {(L.speed + L.speedRandom * (2.f * frand(e.rng) - 1.f)) * scale, 0.f, 0.f};
        if (L.radial != 0.f)
        {
            float dir[3] = {off[0], off[1], off[2]};
            if (dot3v(dir, dir) < 1e-8f)
                for (int k = 0; k < 3; ++k) dir[k] = 2.f * frand(e.rng) - 1.f;
            norm3(dir);
            for (int k = 0; k < 3; ++k) vl[k] += dir[k] * L.radial * scale;
        }
        if (L.relative)
        {
            const float up[3] = {0.f, 0.f, 1.f};   // Z do mundo na base da boca
            const float upL[3] = {dot3v(up, fr.f), dot3v(up, fr.s), dot3v(up, fr.u)};
            for (int k = 0; k < 3; ++k)
            {
                q.p[k] = off[k];
                q.v[k] = vl[k] + upL[k] * L.up * scale;
            }
        }
        else
        {
            const float *base = L.hit && hitPt ? hitPt : fr.o;
            for (int k = 0; k < 3; ++k)
            {
                q.p[k] = base[k] + off[0] * fr.f[k] + off[1] * fr.s[k] + off[2] * fr.u[k];
                q.v[k] = vl[0] * fr.f[k] + vl[1] * fr.s[k] + vl[2] * fr.u[k];
            }
            q.v[2] += L.up * scale;
        }
        q.age = age0;
        for (int k = 0; k < 3; ++k) q.p[k] += q.v[k] * age0;
        st.parts.push_back(q);
    }

    // Uma camada em quadrados: o risco vai da cauda (o caminho dos ultimos instantes) a cabeca; as da boca param no limite da linha
    // de visada (o que o bafo atingiu)
    void emitQuads(const wotm::breathfx::Layer &L, const LayerState &st, const Frame &fr, float limit, wotm::Particles::BeamSystem &out)
    {
        for (const Part &q : st.parts)
        {
            const float t = std::clamp(q.age / q.life, 0.f, 1.f);
            wotm::Particles::BeamQuad bq{};
            bq.half = 0.5f * q.size * L.sizeMul.at(t);
            bq.rot = q.rot;
            bq.rgba = gsColor(L, t);
            bq.stretch = L.stretch;
            if (bq.half <= 0.f || (bq.rgba >> 24) == 0)
                continue;
            float head[3], tail[3];
            if (L.relative)
                toWorld(fr, q.p, head);
            else
                for (int k = 0; k < 3; ++k) head[k] = q.p[k];
            std::copy(head, head + 3, tail);
            if (L.stretch)
            {
                const float grow = std::max(L.grow, 1e-3f), shrink = std::clamp(L.shrink, grow, 1.f);
                float k = std::min(q.age, grow * q.life);
                if (t > shrink)
                    k *= shrink < 1.f ? std::max(0.f, (1.f - t) / (1.f - shrink)) : 0.f;
                float tl[3];
                for (int a = 0; a < 3; ++a) tl[a] = q.p[a] - q.v[a] * k;
                if (L.relative)
                    toWorld(fr, tl, tail);
                else
                    std::copy(tl, tl + 3, tail);
            }
            if (!L.hit && limit >= 0.f)
            {
                float dh[3], dt[3];
                for (int a = 0; a < 3; ++a) { dh[a] = head[a] - fr.o[a]; dt[a] = tail[a] - fr.o[a]; }
                const float ph = dot3v(dh, fr.f), pt = dot3v(dt, fr.f);
                if (std::min(ph, pt) >= limit)
                    continue;
                if (ph > limit && ph > pt)
                    for (int a = 0; a < 3; ++a) head[a] = tail[a] + (head[a] - tail[a]) * (limit - pt) / (ph - pt);
                else if (pt > limit && pt > ph)
                    for (int a = 0; a < 3; ++a) tail[a] = head[a] + (tail[a] - head[a]) * (limit - ph) / (pt - ph);
            }
            std::copy(head, head + 3, bq.b);
            std::copy(tail, tail + 3, bq.a);
            if (!L.stretch)
                std::copy(head, head + 3, bq.a);
            out.quads.push_back(bq);
        }
    }

    // ---- tiro proprio ("shotFx"): o projetil seguido pela foto da RAM (Weapons::m_weapons[i]: m_active +0x40, m_prevPos +0x90,
    // _cs +0x150 com a posicao em +0x10); as camadas de "shot" nascem nele enquanto ele voa, as de "launch" no primeiro quadro em
    // que ele aparece e as de "impact" onde o Weapon::DetonateWeapon o explodiu (wotm::breathfx::g_impacts)
    constexpr uint32_t kWeaponsArr = 0x112490 + 0xC00, kWeaponSize = 0x170, kWActive = 0x40, kWPrev = 0x90, kWCs = 0x150, kCsTrans = 0x10;
    struct ShotEmitter
    {
        uint32_t gen = 0;
        const wotm::breathfx::Config *cfg = nullptr;
        std::vector<LayerState> layers;
        bool seen = false, ended = false;   // ended: o projetil ja sumiu (a vaga pode voltar com outro Weapon na mesma gen)
        Frame fr{};
        float burst = 0.f;   // segundos desde a saida (camadas de "launch" sem "once")
        uint32_t rng = 0x85EBCA6Bu;
    };
    struct LooseEmitter   // uma explosao: as camadas de "impact" ate a ultima particula morrer
    {
        const wotm::breathfx::Config *cfg = nullptr;
        std::vector<LayerState> layers;
        Frame fr{};
        float t = 0.f;
        uint32_t rng = 0xC2B2AE35u;
    };
    ShotEmitter g_shotEm[100];
    std::vector<LooseEmitter> g_loose;
    uint32_t g_impactSeen = 0;

    Frame frameAt(const float *o, const float *dir)
    {
        Frame fr;
        float f[3] = {dir[0], dir[1], dir[2]};
        if (dot3v(f, f) < 1e-8f) { f[0] = 0.f; f[1] = 1.f; f[2] = 0.f; }
        norm3(f);
        const float up[3] = {0.f, 0.f, 1.f};
        float s[3] = {f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0]};
        if (dot3v(s, s) < 1e-8f) { s[0] = 1.f; s[1] = 0.f; s[2] = 0.f; }
        norm3(s);
        for (int k = 0; k < 3; ++k) { fr.o[k] = o[k]; fr.f[k] = f[k]; fr.s[k] = s[k]; }
        fr.u[0] = s[1] * f[2] - s[2] * f[1];
        fr.u[1] = s[2] * f[0] - s[0] * f[2];
        fr.u[2] = s[0] * f[1] - s[1] * f[0];
        return fr;
    }
    void ageLayer(LayerState &st, float dt)
    {
        for (Part &q : st.parts)
        {
            q.age += dt;
            for (int k = 0; k < 3; ++k) q.p[k] += q.v[k] * dt;
        }
        st.parts.erase(std::remove_if(st.parts.begin(), st.parts.end(), [](const Part &q) { return q.age >= q.life; }), st.parts.end());
    }
    void spawnAny(uint32_t &rng, const wotm::breathfx::Layer &L, LayerState &st, float scale, const Frame &fr, float age0 = 0.f)
    {
        Emitter tmp;
        tmp.rng = rng;
        spawn(tmp, L, st, scale, fr, nullptr, age0);
        rng = tmp.rng;
    }
    // Emite `L` neste passo: continua (rate por segundo) ou, com "once", a rajada de rate particulas uma vez
    template <class E>
    void emitLayer(E &e, const wotm::breathfx::Layer &L, LayerState &st, float scale, const Frame &fr, float dt)
    {
        if (L.once)
        {
            if (!st.fired)
                for (int n = 0; n < int(L.rate + 0.5f) && n < 512; ++n) spawnAny(e.rng, L, st, scale, fr);
            st.fired = true;
            return;
        }
        st.acc += L.rate * dt;
        while (st.acc >= 1.f && st.parts.size() < 2048)
        {
            st.acc -= 1.f;
            spawnAny(e.rng, L, st, scale, fr, L.rate > 0.f ? st.acc / L.rate : 0.f);
        }
    }
    void drawLayers(wotm::Particles &fx, const wotm::breathfx::Config &cfg, std::vector<LayerState> &layers, const Frame &fr)
    {
        for (size_t j = 0; j < cfg.layers.size() && j < layers.size(); ++j)
        {
            if (layers[j].parts.empty())
                continue;
            const wotm::breathfx::Layer &L = cfg.layers[j];
            wotm::Particles::BeamSystem bs;
            bs.texture = L.texture;
            bs.additive = L.additive;
            emitQuads(L, layers[j], fr, -1.f, bs);
            if (!bs.quads.empty())
                fx.beams.push_back(std::move(bs));
        }
    }
    void prepareShots(wotm::Particles &fx, const wotm::Ram &ram, float dt)
    {
        using wotm::breathfx::Site;
        for (uint32_t i = 0; i < 100; ++i)
        {
            const wotm::breathfx::ShotLive &sl = wotm::breathfx::g_shots[i];
            const uint32_t gen = sl.gen.load(std::memory_order_acquire);
            ShotEmitter &e = g_shotEm[i];
            if (gen != e.gen)
            {
                e = ShotEmitter{};
                e.gen = gen;
                e.cfg = sl.cfg.load(std::memory_order_relaxed);
                e.rng ^= gen * 2654435761u + i;
                if (e.cfg)
                    e.layers.resize(e.cfg->layers.size());
            }
            if (!e.cfg)
                continue;
            const uint32_t w = kGameObj + kWeaponsArr + kWeaponSize * i;
            const uint32_t cs = ram.u32(w + kWCs);
            if (e.seen && (ram.u32(w + kWActive) == 0 || !cs))
                e.ended = true;
            const bool active = !e.ended && ram.u32(w + kWActive) != 0 && cs;
            if (active)
            {
                float p[3], d[3];
                for (int k = 0; k < 3; ++k)
                {
                    p[k] = ram.f32(cs + kCsTrans + 4 * k);
                    d[k] = p[k] - ram.f32(w + kWPrev + 4 * k);
                }
                e.fr = frameAt(p, dot3v(d, d) > 1e-6f || !e.seen ? d : e.fr.f);
            }
            const bool first = active && !e.seen;
            if (first)
            {
                static int logged = 0;
                if (logged < 3 && ++logged)
                    std::fprintf(stderr, "[custom] tiro proprio: vaga %u saiu em (%.0f, %.0f, %.0f), indo para (%.2f, %.2f, %.2f)\n", i,
                                 e.fr.o[0], e.fr.o[1], e.fr.o[2], e.fr.f[0], e.fr.f[1], e.fr.f[2]);
            }
            e.seen |= active;
            if (e.seen && dt > 0.f)
                e.burst += dt;
            const wotm::breathfx::Config &cfg = *e.cfg;
            for (size_t j = 0; j < cfg.layers.size(); ++j)
            {
                const wotm::breathfx::Layer &L = cfg.layers[j];
                LayerState &st = e.layers[j];
                if (dt > 0.f || first)
                    ageLayer(st, dt);
                if (L.site == Site::Shot && active && dt > 0.f)
                    emitLayer(e, L, st, cfg.scale, e.fr, dt);
                else if (L.site == Site::Launch && e.seen && (first || (dt > 0.f && !L.once && e.burst <= L.duration)))
                    emitLayer(e, L, st, cfg.scale, e.fr, first && !L.once ? 1.f / 60.f : dt);
            }
            drawLayers(fx, cfg, e.layers, e.fr);
        }
        // explosoes novas
        const uint32_t seqNow = wotm::breathfx::g_impactSeq.load(std::memory_order_acquire);
        if (seqNow - g_impactSeen > 16)
            g_impactSeen = seqNow - 16;
        for (; g_impactSeen != seqNow; ++g_impactSeen)
        {
            const wotm::breathfx::ImpactLive &im = wotm::breathfx::g_impacts[(g_impactSeen + 1) % 16];
            if (im.seq.load(std::memory_order_acquire) != g_impactSeen + 1)
                continue;
            LooseEmitter le;
            le.cfg = im.cfg.load(std::memory_order_relaxed);
            if (!le.cfg)
                continue;
            const float p[3] = {im.p[0].load(), im.p[1].load(), im.p[2].load()}, d[3] = {0.f, 1.f, 0.f};
            le.fr = frameAt(p, d);
            le.layers.resize(le.cfg->layers.size());
            le.rng ^= (g_impactSeen + 1) * 2246822519u;
            for (size_t j = 0; j < le.cfg->layers.size(); ++j)
                if (le.cfg->layers[j].site == Site::Impact)
                    emitLayer(le, le.cfg->layers[j], le.layers[j], le.cfg->scale, le.fr, 1.f / 60.f);
            g_loose.push_back(std::move(le));
        }
        for (LooseEmitter &le : g_loose)
        {
            if (dt > 0.f)
                le.t += dt;
            bool any = false;
            for (size_t j = 0; j < le.cfg->layers.size(); ++j)
            {
                const wotm::breathfx::Layer &L = le.cfg->layers[j];
                LayerState &st = le.layers[j];
                if (dt > 0.f)
                {
                    ageLayer(st, dt);
                    if (L.site == Site::Impact && !L.once && le.t <= L.duration)
                        emitLayer(le, L, st, le.cfg->scale, le.fr, dt);
                }
                any |= !st.parts.empty() || (L.site == Site::Impact && !L.once && le.t <= L.duration);
            }
            if (!any)
                le.cfg = nullptr;
            else
                drawLayers(fx, *le.cfg, le.layers, le.fr);
        }
        g_loose.erase(std::remove_if(g_loose.begin(), g_loose.end(), [](const LooseEmitter &le) { return !le.cfg; }), g_loose.end());
        if (g_loose.size() > 32)
            g_loose.erase(g_loose.begin(), g_loose.begin() + (g_loose.size() - 32));
    }

    void prepare(wotm::Particles &fx, const wotm::Ram &ram, uint32_t g)
    {
        fx.beams.clear();
        uint32_t steps = g - g_lastG;
        if (g_lastG == 0 || steps > 30)
            steps = g_lastG == g ? 0 : 1;
        g_lastG = g;
        const float dt = float(steps) / 60.f;
        for (uint32_t i = 0; i < 8; ++i)
        {
            const wotm::breathfx::Live &l = wotm::breathfx::g_live[i];
            const uint32_t m = l.m.load(std::memory_order_acquire);
            const wotm::breathfx::Config *cfg = l.cfg.load(std::memory_order_relaxed);
            Emitter &e = g_em[i];
            const bool valid = m && cfg && ram.u32(kGameObj + kMonsters + 4 * i) == m;
            if (!valid || m != e.m || cfg != e.cfg)
            {
                e.layers.clear();
                e.m = valid ? m : 0;
                e.cfg = valid ? cfg : nullptr;
                e.rng ^= m * 2654435761u;
                if (!valid)
                    continue;
                e.layers.resize(cfg->layers.size());
            }
            Frame fr = frameOf(ram, m);
            if (l.aimed.load(std::memory_order_relaxed))   // o eixo que o gancho virou para o alvo ("aim"), o mesmo que o jogo testou
            {
                const float ax[3] = {l.axis[0].load(std::memory_order_relaxed), l.axis[1].load(std::memory_order_relaxed),
                                     l.axis[2].load(std::memory_order_relaxed)};
                fr = frameAt(fr.o, ax);
            }
            const bool on = ram.u32(m + kBreath + kBreathState) == 1u && ram.f32(m + kBreath + kBreathTime) > 0.f;
            const float range = ram.f32(m + kBreath + kBreathRange);
            float limit = l.limit.load(std::memory_order_relaxed);
            if (limit < 0.f)
                limit = range;
            const bool blocked = l.blocked.load(std::memory_order_relaxed);
            float hitPt[3];
            for (int k = 0; k < 3; ++k) hitPt[k] = fr.o[k] + fr.f[k] * limit;
            for (size_t j = 0; j < cfg->layers.size(); ++j)
            {
                const wotm::breathfx::Layer &L = cfg->layers[j];
                LayerState &st = e.layers[j];
                if (dt > 0.f)
                {
                    for (Part &q : st.parts)
                    {
                        q.age += dt;
                        for (int k = 0; k < 3; ++k) q.p[k] += q.v[k] * dt;
                    }
                    st.parts.erase(std::remove_if(st.parts.begin(), st.parts.end(), [](const Part &q) { return q.age >= q.life; }), st.parts.end());
                    const bool emit = on && (!L.hit || blocked);
                    if (!emit)
                    {
                        st.acc = 0.f;
                        st.fired = false;
                    }
                    else if (L.once)
                    {
                        if (!st.fired)
                            for (int n = 0; n < int(L.rate + 0.5f) && n < 512; ++n) spawn(e, L, st, cfg->scale, fr, hitPt, 0.f);
                        st.fired = true;
                    }
                    else
                    {
                        st.acc += L.rate * dt;
                        while (st.acc >= 1.f && st.parts.size() < 2048)
                        {
                            st.acc -= 1.f;
                            spawn(e, L, st, cfg->scale, fr, hitPt, L.rate > 0.f ? st.acc / L.rate : 0.f);
                        }
                    }
                }
                if (st.parts.empty())
                    continue;
                wotm::Particles::BeamSystem bs;
                bs.texture = L.texture;
                bs.additive = L.additive;
                emitQuads(L, st, fr, limit, bs);
                if (!bs.quads.empty())
                    fx.beams.push_back(std::move(bs));
            }
        }
        prepareShots(fx, ram, dt);
    }
} // namespace wotm_breathfx
