// wotm_orbit.inc: camera moderna (orbital); plano e etapas em
// 75f5be1:docs/investigacoes/2026-10-22/camera_orbital_plano.md. Incluido no wotm_overrides.cpp, que chama daqui os ganchos que ja tem:
//   - Cameras::Update (o embrulho da camera livre): orbitPre antes do original, orbitPost depois;
//   - PadFlags::interpretInputs (o do lock-on): orbitInputEnter na entrada, orbitInputLeave quando a funcao de fato voltou;
//   - Camera::shakeCamera: guarda o tremor do quadro, que a orbita soma a posicao dela;
//   - TheGame::SetOkToUnify: CloseCam = 0 zera o m_camUnify de todos (nunca entra na vista unificada), como o wotm_split4.inc.
//
// Etapa 1 (orbita basica). Vale para a camera com POV 0 (normalUpdate) que segue o monstro de um jogador humano na vista dele, fora
// da vista unificada (menos a camera 2 no POV 0, a do agarrao), do modo 9 (Crush), da camera livre e do WOTM_AIVSAI (lotes e corpus
// ficam com a camera do jogo). O jogo
// continua rodando o normalUpdate por baixo (o tremor dele e aproveitado); depois dele a orbita refaz a pose inteira:
//   - posicao: o deslocamento do POV (D10, so ele: o balanco e a inclinacao do jogo mudam com o estado do monstro e davam solavancos)
//     girado pelo nosso angulo em volta do monstro (az = rumo do monstro para a camera) em vez do rumo das costas dele; o analogico
//     direito gira az (X) e a elevacao (Y); o pivo segue o monstro com atraso curto e limitado;
//   - guinada e inclinacao: para um ponto ~120 acima do pivo (onde a camera do jogo mira), entao subir a camera inclina na medida
//     certa; travado, esse ponto anda para o alvo;
//   - recentralizar (CamRecenter): ~1,5 s sem mexer no analogico direito, com o monstro andando, az volta devagar para as costas;
//   - o jogo e neutralizado so no que briga com a orbita: trava do setor traseiro (E60 = 1), volta as costas (DA4 = DF4 = 0).
// Ligando (primeiro quadro, volta de outro POV, da vista unificada ou da camera livre) az sai da posicao que o outro modo deixou e a
// pose desliza da do jogo para a da orbita em ~20 quadros: sem salto. Corte do jogo (D98) com a orbita ja ligada (reinicio de
// rodada, renascer) recomeca az das costas.
//
// Controle: com a orbita dirigindo a vista do jogador, o interpretInputs roda com PadFlags +0x16E4/+0x16E8 = 1 (configuracao
// relativa a tela, updateInput*ConfigSR, que le o _cs desta camera) e os valores do jogo voltam na saida; os bytes do analogico
// direito (GamePad 0x10..0x13: esquerda, direita, cima, baixo) vao para a orbita e chegam zerados ao jogo (na CR girariam o monstro,
// na SR tambem, quando o esquerdo esta parado). Liga na hora, mesmo andando.
namespace
{
    namespace orbit
    {
        constexpr uint32_t kCameras = wotm::sym::x_7Cameras_m_cameras, kNumCameras = wotm::sym::x_7Cameras_m_numCameras, kCamStride = 0xEB0u,
                           kUnified = wotm::sym::gUseUnifiedView, kGameMode = kGame + 0x1203C8u, kSlots = kGame + 0xB80u,
                           kSlotStride = 0x11190u, kNumSlots = kGame + 0x1203D4u;
        // Camera (75f5be1:docs/investigacoes/2026-10-22/camera_normalupdate.md)
        constexpr uint32_t kCamCs = 0xCF0u, kCamMonster = 0xCF4u, kCamPov = 0xCF8u, kCamOffset = 0xD10u,
                           kCamIdle = 0xDA4u, kCamDrag = 0xDF4u, kCamCut = 0xD98u,
                           kCamSectorLock = 0xE60u;
        // Monster
        constexpr uint32_t kMonCs = 0xCu, kMonDead = 0xE8u, kMonPinTarget = 0x6C04u, kMonCameraView = 0x6CD8u, kMonCamUnify = 0x7970u;
        // PadFlags
        constexpr uint32_t kPfOwner = 0x1810u, kPfConfigActive = 0x16E4u, kPfConfigWanted = 0x16E8u, kPfRingIndex = 0x16F0u;
        constexpr int kMaxCams = 4;
        constexpr float kPi = 3.14159265f;
        constexpr float kLockRate = 0.15f;   // alvo travado: angulo e mira por quadro (~6 quadros; 0,25 era seco demais para o dono)

        struct Cam
        {
            bool active = false;     // a orbita escreveu a pose no quadro anterior
            uint32_t mon = 0;        // monstro seguido quando ligou
            float az = 0.f;          // rumo do monstro para a camera
            float elev = 0.f;        // elevacao somada pelo analogico (radianos)
            float pivot[3] = {};     // ponto em volta do qual gira (segue o monstro com atraso)
            float lastMon[3] = {};
            float blend = 0.f;       // 1 = pose do jogo, 0 = da orbita (entrada suave)
            int still = 0;           // quadros sem mexer no analogico direito
            bool cutSeen = false;    // D98 ligado na entrada deste quadro
            float shake[3] = {};     // deslocamento do shakeCamera neste quadro
            float nudge = 0.f;       // desvio do analogico direito com alvo travado
            bool locked = false;
            int away = 0;            // quadros seguidos com o monstro se afastando da camera
            float lockW = 0.f;       // peso da mira no alvo travado (0..1)
            float lockAim[3] = {};   // desvio da mira para o alvo (o ultimo, enquanto o peso cai ao soltar)
            float reach = 1.f;       // fracao da distancia livre (colisao: encolhe na hora, volta devagar)
            float lift = 0.f;        // altura somada para ficar acima do chao (suavizada na descida)
            uint32_t lastPin = 0;    // m_pinTarget visto no quadro anterior (log)
        } g_cam[kMaxCams];

        struct Input
        {
            bool driving = false;    // a orbita dirige a vista deste jogador (orbitPost)
            bool forced = false;     // configuracao SR imposta no interpretInputs em curso
            bool pending = false;    // forcado, mas a funcao nao voltou ainda (fatia de tempo): restaurar na proxima entrada
            uint32_t pf = 0, saved4 = 0, saved8 = 0;
            float stick[2] = {};     // analogico direito do ultimo quadro (x direita, y cima), -1..1
            float tilt = 1.f;        // inclinacao do analogico esquerdo (0..1) na entrada do interpretInputs forcado
        } g_in[4];

        float rdF(uint8_t *rdram, uint32_t a) { float v; std::memcpy(&v, rdram + a, 4); return v; }
        void wrF(uint8_t *rdram, uint32_t a, float v) { std::memcpy(rdram + a, &v, 4); }
        uint32_t camAddr(int v) { return kCameras + kCamStride * uint32_t(v); }
        bool okAddr(uint32_t a, uint32_t n) { return a >= 0x1000u && a + n <= PS2_RAM_SIZE; }
        float wrapPi(float a)
        {
            while (a > kPi) a -= 2.f * kPi;
            while (a < -kPi) a += 2.f * kPi;
            return a;
        }
        float headingTo(float dx, float dy) { return std::atan2(dx, dy); }   // 0 = +Y, positivo para +X

        bool enabled() { return wotm::settings::on(wotm::settings::Camera) && !aiVsAiAll() && !g_wotmFreeCam.load(std::memory_order_relaxed); }

        // Camera v elegivel neste quadro; mon = monstro, slot = jogador
        bool eligible(uint8_t *rdram, int v, uint32_t &mon, int &slot)
        {
            if (v >= kMaxCams || !enabled() || FAST_READ32(kGameMode) == 9u)
                return false;
            // vista unificada: so a camera 2 no POV 0, que e a do agarrao (StateGrappleLift: segue quem agarra). A de perto e o POV 1,
            // a da vitoria o 7: ficam com o jogo
            const bool unified = FAST_READ32(kUnified) != 0u;
            if (unified && v != 2)
                return false;
            const uint32_t c = camAddr(v);
            if (FAST_READ32(c + kCamPov) != 0u)
                return false;
            mon = FAST_READ32(c + kCamMonster) & 0x1FFFFFFFu;
            const uint32_t cs = FAST_READ32(c + kCamCs) & 0x1FFFFFFFu;
            if (!okAddr(mon, kMonCamUnify + 4u) || !okAddr(cs, 0x70u) || !isPlayerMonster(rdram, FAST_READ32(c + kCamMonster), &slot))
                return false;
            if (aiVsAiSlot(slot))
                return false;   // bot no slot de jogador (WOTM_AIVSAI=<lista>): camera do jogo
            if (!unified && static_cast<int32_t>(FAST_READ32(mon + kMonCameraView)) != v)
                return false;   // camera 2 seguindo quem agarra fora da unificada (1P sem a de perto etc.): a vista do jogador e outra
            const uint32_t mcs = FAST_READ32(mon + kMonCs) & 0x1FFFFFFFu;
            return okAddr(mcs, 0x70u);
        }

        // Teste sem controle: WOTM_ORBITSTICK="quadro:quadros:x:y[@jogador];..." poe o analogico direito em (x, y) (-1..1, y para cima) do
        // quadro dado (contado nas chamadas do Cameras::Update) por tantos quadros
        void testStick(unsigned frame, int slot, float &sx, float &sy)
        {
            struct Step { unsigned f0, n; float x, y; int player; };   // player 0 = todos
            static std::vector<Step> steps;
            static bool parsed = false;
            if (!parsed)
            {
                parsed = true;
                if (const char *e = std::getenv("WOTM_ORBITSTICK"))
                    for (const char *p = e; *p;)
                    {
                        Step s{};
                        if (std::sscanf(p, "%u:%u:%f:%f@%d", &s.f0, &s.n, &s.x, &s.y, &s.player) >= 4)
                            steps.push_back(s);
                        const char *semi = std::strchr(p, ';');
                        if (!semi)
                            break;
                        p = semi + 1;
                    }
            }
            for (const Step &s : steps)
                if (frame >= s.f0 && frame < s.f0 + s.n && (s.player == 0 || s.player == slot + 1))
                {
                    sx = s.x;
                    sy = s.y;
                }
        }

        // Teste sem mouse: WOTM_ORBITMOUSE="quadro:quadros:dx:dy;..." soma (dx, dy) pixels por quadro (y para baixo, como o mouse) ao
        // mouse do jogador do teclado, do quadro dado por tantos quadros
        void testMouse(unsigned frame, float &mx, float &my)
        {
            struct Step { unsigned f0, n; float x, y; };
            static std::vector<Step> steps;
            static bool parsed = false;
            if (!parsed)
            {
                parsed = true;
                if (const char *e = std::getenv("WOTM_ORBITMOUSE"))
                    for (const char *p = e; *p;)
                    {
                        Step s{};
                        if (std::sscanf(p, "%u:%u:%f:%f", &s.f0, &s.n, &s.x, &s.y) == 4)
                            steps.push_back(s);
                        const char *semi = std::strchr(p, ';');
                        if (!semi)
                            break;
                        p = semi + 1;
                    }
            }
            for (const Step &s : steps)
                if (frame >= s.f0 && frame < s.f0 + s.n)
                {
                    mx += s.x;
                    my += s.y;
                }
        }

        // Monster::hasPinTarget (m_pinTarget com o bit 2 do +4) e a posicao do alvo (o _cs fica no +0xC de monstro e predio)
        bool pinTarget(uint8_t *rdram, uint32_t mon, float tp[3])
        {
            const uint32_t t = FAST_READ32(mon + kMonPinTarget) & 0x1FFFFFFFu;
            if (!okAddr(t, 0x10u))
                return false;
            uint16_t flags;
            std::memcpy(&flags, rdram + t + 4, 2);
            if (!(flags & 2u))
                return false;
            const uint32_t cs = FAST_READ32(t + kMonCs) & 0x1FFFFFFFu;
            if (!okAddr(cs, 0x20u))
                return false;
            for (int i = 0; i < 3; ++i)
                tp[i] = rdF(rdram, cs + 0x10 + 4 * i);
            return true;
        }

        int numCams(uint8_t *rdram) { return std::clamp(static_cast<int32_t>(FAST_READ32(kNumCameras)), 0, kMaxCams); }

        // Colisao (etapa 3) pelas funcoes do proprio jogo, chamadas daqui com os dados numa area abaixo da pilha atual:
        //   hdLosTest(from, to, _hdResult *res, bool) -> v0 = 1 livre; res: hit +0, surface +2, ids +4, point +0x10, normal +0x20
        //   hdHatTest(pos, normal, $f12 up, bool, $f13 down) -> $f0 = pos.z - chao (0 sem chao)
        struct Probe
        {
            uint8_t *rdram;
            R5900Context *ctx;
            PS2Runtime *rt;
            uint32_t base = 0;   // 0x100 bytes de dados; a pilha das chamadas comeca logo abaixo
            bool ok = false;
            Probe(uint8_t *r, R5900Context *c, PS2Runtime *p) : rdram(r), ctx(c), rt(p)
            {
                const uint32_t sp = GPR_U32(ctx, 29) & 0x1FFFFFFFu;
                base = (sp - 0x1000u) & ~0xFu;
                ok = rt && sp > 0x10000u && okAddr(base - 0x1000u, 0x1100u);
            }
            bool call(uint32_t fn, std::initializer_list<uint32_t> a, std::initializer_list<float> f, uint32_t &v0, float &f0)
            {
                const PS2Runtime::RecompiledFunction fp = ok ? rt->lookupFunction(fn) : nullptr;
                if (!fp)
                    return false;
                const R5900Context saved = *ctx;
                int r = 4;
                for (uint32_t x : a)
                    ctx->r[r++] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(x)));
                int fr = 12;
                for (float x : f)
                    ctx->f[fr++] = x;
                ctx->r[29] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(base - 0x10u)));
                ctx->r[31] = _mm_set_epi64x(0, 0);
                ctx->pc = fn;
                fp(rdram, ctx, rt);
                wotmFinishGuestCall(rdram, ctx, rt);
                const bool back = ctx->pc == 0u;
                v0 = GPR_U32(ctx, 2);
                f0 = ctx->f[0];
                *ctx = saved;
                return back;
            }
            void put(uint32_t a, const float v[3])
            {
                for (int i = 0; i < 3; ++i)
                    wrF(rdram, a + 4 * i, v[i]);
                wrF(rdram, a + 12, 0.f);
            }
            // segmento a -> b; true se bateu (pt, nrm, ids, surface do primeiro contato)
            bool los(const float a[3], const float b[3], float pt[3], float nrm[3], uint32_t &ids, uint32_t &surf)
            {
                put(base, a);
                put(base + 0x10, b);
                std::memset(rdram + base + 0x20, 0, 0x30);
                uint32_t v0 = 1;
                float f0 = 0.f;
                if (!call(wotm::fn::hdLosTest__FP8_fvectorT0P9_hdResultb, {base, base + 0x10, base + 0x20, 0}, {}, v0, f0) || v0 != 0u ||
                    !rdram[base + 0x20])
                    return false;
                for (int i = 0; i < 3; ++i)
                {
                    pt[i] = rdF(rdram, base + 0x30 + 4 * i);
                    nrm[i] = rdF(rdram, base + 0x40 + 4 * i);
                }
                uint16_t s16;
                std::memcpy(&s16, rdram + base + 0x22, 2);
                surf = s16;
                ids = FAST_READ32(base + 0x24);
                return true;
            }
            // altura de p acima do chao logo abaixo (procura de `up` acima a 4096 abaixo); false sem chao
            bool hat(const float p[3], float up, float &above)
            {
                put(base, p);
                uint32_t v0 = 0;
                float f0 = 0.f;
                if (!call(wotm::fn::hdHatTest__FP8_fvectorT0fbf, {base, base + 0x10, 0}, {up, 4096.f}, v0, f0))
                    return false;
                above = f0;
                return f0 != 0.f || rdF(rdram, base + 0x18) != 1.f;   // sem chao: 0 com a normal (0, 0, 1)
            }
        };

        void pre(uint8_t *rdram)
        {
            const int n = numCams(rdram);
            for (int v = 0; v < n; ++v)
            {
                Cam &s = g_cam[v];
                uint32_t mon = 0;
                int slot = 0;
                s.cutSeen = false;
                std::memset(s.shake, 0, sizeof s.shake);
                if (!eligible(rdram, v, mon, slot))
                    continue;
                const uint32_t c = camAddr(v);
                s.cutSeen = FAST_READ32(c + kCamCut) != 0u;
                FAST_WRITE32(c + kCamSectorLock, 1);       // sem a trava do setor traseiro
                FAST_WRITE32(c + kCamIdle, 0);             // sem a volta as costas
                FAST_WRITE32(c + kCamDrag, 0);
            }
        }

        // Chao e predios (etapa 3). pos = onde a orbita quer a camera; o ponto de visada e o pivo 120 acima (a mira)
        void collide(Probe &pr, Cam &s, int v, float pos[3], bool logNow)
        {
            static const int mode = [] { const char *e = std::getenv("WOTM_CAMCOLLIDE"); return e ? std::atoi(e) : 2; }();
            if (!mode || !pr.ok)
                return;
            const float a[3] = {s.pivot[0], s.pivot[1], s.pivot[2] + 120.f};
            float pt[3], nrm[3], rpt[3], rnrm[3];
            uint32_t ids = 0, surf = 0, rids = 0, rsurf = 0;
            const bool fwd = pr.los(a, pos, pt, nrm, ids, surf);
            const bool rev = fwd && pr.los(pos, a, rpt, rnrm, rids, rsurf);
            float d[3], len2 = 0.f;
            for (int i = 0; i < 3; ++i)
            {
                d[i] = pos[i] - a[i];
                len2 += d[i] * d[i];
            }
            const float len = std::sqrt(std::max(len2, 1.f));
            float tHit = 1.f, tRev = 0.f;
            if (fwd)
                tHit = ((pt[0] - a[0]) * d[0] + (pt[1] - a[1]) * d[1] + (pt[2] - a[2]) * d[2]) / (len * len);
            if (rev)
                tRev = ((rpt[0] - a[0]) * d[0] + (rpt[1] - a[1]) * d[1] + (rpt[2] - a[2]) * d[2]) / (len * len);
            float above = 0.f;
            const bool ground = pr.hat(pos, 60.f, above);
            if (logNow || (fwd && std::getenv("WOTM_ORBITLOG") && std::atoi(std::getenv("WOTM_ORBITLOG")) >= 3))
                std::fprintf(stderr, "[orbit] camera %d: colisao ida %d t %.2f n %.2f,%.2f,%.2f ids %08X sup %04X | volta %d t %.2f ids %08X | chao %d %.0f\n",
                             v, fwd, tHit, nrm[0], nrm[1], nrm[2], ids, surf, rev, tRev, rids, ground, above);
            if (mode < 2)
                return;   // =1 so mede (temporario, ate decidir com o log)
            // Dentro de algo: a ida bate e a volta (da camera para a mira) nao sai de nada depois do ponto da ida. Atras de um predio,
            // com a camera do lado de fora, a volta bate na face de tras (t maior que o da ida): fica, e o jogo deixa o predio
            // transparente (hierSetCameraLosPoint). Dentro de predio ou abaixo do chao: encolhe ate antes do contato.
            const bool inside = fwd && (!rev || tRev <= tHit + 0.01f);
            const float want = inside ? std::max((tHit * len - 25.f) / len, 0.15f) : 1.f;
            s.reach = want < s.reach ? want : s.reach + (want - s.reach) * 0.05f;   // encolhe na hora, volta devagar
            for (int i = 0; i < 3; ++i)
                pos[i] = a[i] + d[i] * s.reach;
            // chao: pelo menos 30 acima; desce devagar quando o chao baixa
            float above2 = 0.f;
            const float need = pr.hat(pos, 60.f, above2) ? std::max(0.f, 30.f - above2) : 0.f;
            s.lift = std::max(need, s.lift - 3.f);
            pos[2] += s.lift;
        }

        // Medidor de salto (etapa 4, com WOTM_ORBITLOG): a pose final de cada vista, quadro a quadro; um pulo grande (posicao ou
        // guinada) vira uma linha com o POV, a orbita e a vista unificada antes e depois. Vale tambem com a camera classica (base).
        void jumpMeter(uint8_t *rdram, int n, unsigned frames)
        {
            struct Last { bool ok = false; float pos[3] = {}, heading = 0.f; uint32_t pov = 0; bool orbit = false; uint32_t unified = 0; };
            static Last last[kMaxCams];
            for (int v = 0; v < kMaxCams; ++v)
            {
                Last &l = last[v];
                if (v >= n)
                {
                    l.ok = false;
                    continue;
                }
                const uint32_t c = camAddr(v), cs = FAST_READ32(c + kCamCs) & 0x1FFFFFFFu;
                if (!okAddr(cs, 0x70u))
                {
                    l.ok = false;
                    continue;
                }
                float pos[3];
                for (int i = 0; i < 3; ++i)
                    pos[i] = rdF(rdram, cs + 0x10 + 4 * i);
                const float heading = rdF(rdram, cs + 0x68);
                const uint32_t pov = FAST_READ32(c + kCamPov), unified = FAST_READ32(kUnified);
                const bool orbit = g_cam[v].active;
                if (l.ok)
                {
                    const float dx = pos[0] - l.pos[0], dy = pos[1] - l.pos[1], dz = pos[2] - l.pos[2];
                    const float d = std::sqrt(dx * dx + dy * dy + dz * dz), dh = std::fabs(wrapPi(heading - l.heading)) * 57.2958f;
                    if (d > 60.f || dh > 20.f)
                        std::fprintf(stderr, "[orbit] camera %d: salto quadro %u: %.0f de distancia, %.0f graus (pov %d->%d, orbita %d->%d, unificada %d->%d, corte %d)\n",
                                     v, frames, d, dh, static_cast<int32_t>(l.pov), static_cast<int32_t>(pov), l.orbit, orbit, l.unified, unified,
                                     FAST_READ32(c + kCamCut) != 0u);
                }
                l.ok = true;
                std::memcpy(l.pos, pos, sizeof pos);
                l.heading = heading;
                l.pov = pov;
                l.orbit = orbit;
                l.unified = unified;
            }
        }

        void post(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
        {
            static const bool log = std::getenv("WOTM_ORBITLOG") != nullptr;
            static const unsigned logEvery = log && std::atoi(std::getenv("WOTM_ORBITLOG")) >= 2 ? 15u : 60u;   // =2: quatro por segundo
            static unsigned frames = 0;
            ++frames;
            Probe probe(rdram, ctx, rt);
            const int n = numCams(rdram);
            bool driving[4] = {};
            bool wasActive[kMaxCams];
            for (int v = 0; v < kMaxCams; ++v)
                wasActive[v] = g_cam[v].active;
            // mouse: o cursor fica preso enquanto alguma camera segue o monstro do jogador do teclado (inclusive as do jogo: queda,
            // arremesso), fora da pausa
            if (enabled() && FAST_READ32(wotm::sym::isPaused) == 0u)
                for (int v = 0; v < n && v < kMaxCams; ++v)
                    if (int ks = -1; isPlayerMonster(rdram, FAST_READ32(camAddr(v) + kCamMonster), &ks) &&
                                     ks == wotm::mouse::g_kbPlayer.load(std::memory_order_relaxed))
                        wotm::mouse::want();
            for (int v = 0; v < kMaxCams; ++v)
            {
                Cam &s = g_cam[v];
                uint32_t mon = 0;
                int slot = 0;
                if (v >= n || !eligible(rdram, v, mon, slot))
                {
                    if (s.active && log)
                        std::fprintf(stderr, "[orbit] camera %d: desligada (pov %d, unificada %d)\n", v,
                                     v < n ? static_cast<int32_t>(FAST_READ32(camAddr(v) + kCamPov)) : -1, FAST_READ32(kUnified));
                    s.active = false;
                    continue;
                }
                const uint32_t c = camAddr(v), cs = FAST_READ32(c + kCamCs) & 0x1FFFFFFFu, mcs = FAST_READ32(mon + kMonCs) & 0x1FFFFFFFu;
                float mt[3], game[3];
                for (int i = 0; i < 3; ++i)
                {
                    mt[i] = rdF(rdram, mcs + 0x10 + 4 * i);
                    game[i] = rdF(rdram, cs + 0x10 + 4 * i);
                }
                const float monHeading = rdF(rdram, mcs + 0x68);
                Input &in = g_in[slot];
                const bool restart = !s.active || s.mon != mon;
                // troca de camera com o mesmo monstro (agarrao: vista 0 -> camera 2 da unificada e de volta): a orbita continua com o
                // estado da outra, sem entrada suave nem recomeco
                int from = -1;
                if (restart)
                    for (int u = 0; u < kMaxCams; ++u)
                        if (u != v && wasActive[u] && g_cam[u].mon == mon)
                            from = u;
                if (from >= 0)
                {
                    const bool cut = s.cutSeen;
                    float shake[3];
                    std::memcpy(shake, s.shake, sizeof shake);
                    s = g_cam[from];
                    s.cutSeen = cut;
                    std::memcpy(s.shake, shake, sizeof shake);
                    g_cam[from].active = false;
                    if (log)
                        std::fprintf(stderr, "[orbit] camera %d: continua da camera %d (az %.1f)\n", v, from, s.az * 57.2958f);
                }
                else if (restart || s.cutSeen)
                {
                    // de onde o jogo (ou o corte dele) deixou a camera
                    const float dx = game[0] - mt[0], dy = game[1] - mt[1];
                    s.az = dx * dx + dy * dy > 1.f ? headingTo(dx, dy) : wrapPi(monHeading + kPi);
                    s.elev = 0.f;
                    std::memcpy(s.pivot, mt, sizeof mt);
                    std::memcpy(s.lastMon, mt, sizeof mt);
                    s.blend = restart && !s.cutSeen ? 1.f : 0.f;
                    s.still = 0;
                    s.nudge = 0.f;
                    s.locked = false;
                    s.reach = 1.f;
                    s.lift = 0.f;
                    s.mon = mon;
                    in.stick[0] = in.stick[1] = 0.f;
                    if (log)
                        std::fprintf(stderr, "[orbit] camera %d: ligada (jogador %d, az %.1f, rumo do monstro %.1f%s)\n", v, slot + 1,
                                     s.az * 57.2958f, monHeading * 57.2958f, s.cutSeen ? ", corte do jogo" : "");
                }
                s.active = true;
                driving[slot] = true;

                // analogico direito: ~170 graus/s no maximo, a 60 quadros/s
                float sx = in.stick[0], sy = in.stick[1];
                testStick(frames, slot, sx, sy);
                const bool stickMoved = std::fabs(sx) > 0.05f || std::fabs(sy) > 0.05f;
                s.still = stickMoved ? 0 : s.still + 1;
                // painel, aba CAMERA: sensibilidade (5 = 1x, cada ponto 20%) e inverter o eixo vertical
                const float sens = float(std::clamp(wotm::settings::get(wotm::settings::CamSens), 1, 10)) * 0.2f;
                sx *= sens;
                sy *= wotm::settings::on(wotm::settings::CamInvertY) ? -sens : sens;
                s.elev = std::clamp(s.elev - sy * 0.012f, -0.6f, 0.9f);   // ~40 graus/s (a horizontal e ~170)

                // mouse (etapa 7, wotm_mouse.hpp): so na orbita do jogador do teclado. Deslocamento direto (angulo por pixel, ~1000
                // pixels por 140 graus na sensibilidade 5), somado depois do analogico; para cima olha para cima, como o analogico.
                float mx = 0.f, my = 0.f;
                if (slot == wotm::mouse::g_kbPlayer.load(std::memory_order_relaxed))
                {
                    if (FAST_READ32(wotm::sym::isPaused) == 0u)
                        wotm::mouse::want();
                    wotm::mouse::take(mx, my);
                    testMouse(frames, mx, my);
                    mx *= 0.0025f * sens;
                    my *= 0.0025f * (wotm::settings::on(wotm::settings::CamInvertY) ? -sens : sens);
                    if (mx != 0.f || my != 0.f)
                    {
                        s.still = 0;
                        s.elev = std::clamp(s.elev + my, -0.6f, 0.9f);
                    }
                }
                const bool moved = stickMoved || mx != 0.f || my != 0.f;

                // alvo travado (etapa 2): a camera vai depressa para tras do monstro na linha do alvo (os dois na tela); o analogico
                // direito so desloca um pouco (ate ~35 graus) e volta ao soltar. Solto, o angulo continua de onde estava.
                float tp[3];
                const bool locked = pinTarget(rdram, mon, tp) && (tp[0] - mt[0]) * (tp[0] - mt[0]) + (tp[1] - mt[1]) * (tp[1] - mt[1]) > 1.f;
                if (locked)
                {
                    s.nudge = moved ? std::clamp(s.nudge + sx * 0.04f + mx, -0.6f, 0.6f) : s.nudge * 0.9f;
                    const float want = wrapPi(headingTo(mt[0] - tp[0], mt[1] - tp[1]) + s.nudge);
                    s.az = wrapPi(s.az + wrapPi(want - s.az) * kLockRate);
                }
                else
                {
                    s.nudge = 0.f;
                    s.az = wrapPi(s.az + sx * 0.05f + mx);
                }
                if (locked != s.locked && log)
                    std::fprintf(stderr, "[orbit] camera %d: %s\n", v, locked ? "alvo travado" : "alvo solto");
                s.locked = locked;
                if (const uint32_t pin = FAST_READ32(mon + kMonPinTarget); pin != s.lastPin)
                {
                    if (log)
                        std::fprintf(stderr, "[orbit] camera %d: alvo 0x%x -> 0x%x (unificada %d, travado %d)\n", v, s.lastPin, pin,
                                     FAST_READ32(kUnified), locked);
                    s.lastPin = pin;
                }

                // recentralizar: so com o monstro se afastando da camera (andando para a frente da tela). Andando de lado ou para
                // tras o monstro se vira para onde anda; levar a camera para as costas dele mudaria o "para tras" e ele giraria sem
                // parar (era a camera doida ao soltar o lock-on andando para tras, e o giro sozinho andando de lado)
                const float vx = mt[0] - s.lastMon[0], vy = mt[1] - s.lastMon[1], speed = std::sqrt(vx * vx + vy * vy);
                std::memcpy(s.lastMon, mt, sizeof mt);
                if (!locked && wotm::settings::on(wotm::settings::CamRecenter) && s.still > 90 && speed > 0.5f)
                {
                    const float align = -(vx * std::sin(s.az) + vy * std::cos(s.az)) / speed;   // 1 = para longe da camera (az e o rumo do monstro para ela)
                    // andando de frente (rumo do monstro com o do deslocamento) e seguido: a virada do monstro tem deslocamento proprio
                    // (comecar a andar de lado vindo de frente para a camera escorrega ~1 s para longe dela, a 2,5 por quadro)
                    const float facing = (vx * std::sin(monHeading) + vy * std::cos(monHeading)) / speed;
                    s.away = align > 0.5f && facing > 0.7f ? s.away + 1 : 0;
                    if (s.away > 20)
                    {
                        const float back = headingTo(-vx, -vy);
                        const float rate = 0.025f * (align - 0.5f) / 0.5f;
                        s.az = wrapPi(s.az + wrapPi(back - s.az) * rate);
                        s.elev -= s.elev * rate;
                    }
                }
                else
                    s.away = 0;

                // pivo: segue o monstro com atraso curto, limitado (altura mais devagar: o pulo nao sacode a camera)
                for (int i = 0; i < 3; ++i)
                {
                    const float rate = i < 2 ? 0.3f : 0.12f, lim = i < 2 ? 40.f : 90.f;
                    s.pivot[i] += (mt[i] - s.pivot[i]) * rate;
                    s.pivot[i] = std::clamp(s.pivot[i], mt[i] - lim, mt[i] + lim);
                }

                // deslocamento do jogo no referencial do monstro (x direita, y frente, z cima): so o D10 do POV (tela cheia ou
                // dividida). Sem o balanco nem a inclinacao do jogo: os dois mudam com o estado do monstro (virando no lugar e
                // comecando a andar para tras o jogo mira direto no monstro; travar/soltar refaz uma rampa; travado, o balanco subia
                // a camera ate em cima do monstro), e com a camera em outro angulo viravam solavancos
                const float offY = rdF(rdram, c + kCamOffset + 4), offZ = rdF(rdram, c + kCamOffset + 8);
                const float back = -offY, len = std::sqrt(back * back + offZ * offZ);
                const float e0 = std::atan2(offZ, back), e = std::clamp(e0 + s.elev, -0.25f, 1.45f);
                const float horiz = len * std::cos(e), height = len * std::sin(e);

                float pos[3] = {s.pivot[0] + std::sin(s.az) * horiz, s.pivot[1] + std::cos(s.az) * horiz, s.pivot[2] + height};
                collide(probe, s, v, pos, log && frames % logEvery == 0);
                if (s.blend > 0.001f)
                {
                    for (int i = 0; i < 3; ++i)
                        pos[i] += (game[i] - pos[i]) * s.blend;
                    s.blend *= 0.85f;
                }
                else
                    s.blend = 0.f;
                for (int i = 0; i < 3; ++i)
                    pos[i] += s.shake[i];

                WotmFreeCamPose p;
                std::memcpy(p.pos, pos, sizeof pos);
                // Mira: o pivo, ~120 acima do pe (a camera do jogo, 231 atras e 160 acima olhando 10 graus para baixo, mira ai), entao
                // subir a camera inclina na medida certa e o monstro fica no mesmo lugar da tela. Travado, a mira anda para o alvo (30%
                // do caminho, ate 120 no chao e 80 na altura; com o alvo longe o monstro saia pela base da tela); o peso entra e sai em
                // ~6 quadros, sem salto ao travar e soltar
                s.lockW = locked ? s.lockW + (1.f - s.lockW) * kLockRate : s.lockW * (1.f - kLockRate);
                if (locked)
                {
                    const float dx = tp[0] - mt[0], dy = tp[1] - mt[1], d = std::sqrt(dx * dx + dy * dy);
                    const float f = d > 1.f ? std::min(0.3f * d, 120.f) / d : 0.f;
                    s.lockAim[0] = dx * f;
                    s.lockAim[1] = dy * f;
                    s.lockAim[2] = std::clamp((tp[2] - mt[2]) * 0.3f, -80.f, 80.f);
                }
                const float aim[3] = {s.pivot[0] + s.lockAim[0] * s.lockW, s.pivot[1] + s.lockAim[1] * s.lockW,
                                      s.pivot[2] + 120.f + s.lockAim[2] * s.lockW};
                const float adx = aim[0] - pos[0], ady = aim[1] - pos[1];
                p.heading = headingTo(adx, ady);
                p.pitch = std::clamp(std::atan2(aim[2] - pos[2], std::max(std::sqrt(adx * adx + ady * ady), 1.f)), -1.5f, 1.5f);
                float ax[3][3];
                wotmFreeCamAxes(p, ax[0], ax[1], ax[2]);
                for (int i = 0; i < 3; ++i)
                {
                    wrF(rdram, cs + 0x10 + 4 * i, p.pos[i]);
                    for (int r = 0; r < 3; ++r)
                        wrF(rdram, cs + 0x20 + 0x10 * r + 4 * i, ax[r][i]);
                }
                for (int r = 0; r < 3; ++r)
                    wrF(rdram, cs + 0x20 + 0x10 * r + 0xC, 0.f);
                wrF(rdram, cs + 0x60, p.pitch);
                wrF(rdram, cs + 0x64, 0.f);
                wrF(rdram, cs + 0x68, p.heading);
                if (log && frames % logEvery == 0)
                    std::fprintf(stderr, "[orbit] camera %d: quadro %u az %.1f (costas %.1f) elev %.1f dist %.0f alt %.0f analogico %.2f,%.2f parado %d alcance %.2f chao +%.0f monstro %.0f,%.0f\n", v, frames,
                                 s.az * 57.2958f, wrapPi(monHeading + kPi) * 57.2958f, s.elev * 57.2958f, horiz, height, sx, sy, s.still, s.reach, s.lift, mt[0], mt[1]);
            }
            for (int i = 0; i < 4; ++i)
                g_in[i].driving = driving[i];
            if (log)
                jumpMeter(rdram, n, frames);
        }

        // Camera::shakeCamera (a0 = camera): o tremor do quadro e o que ele somou ao cs->trans
        PS2Runtime::RecompiledFunction g_origShake = nullptr;
        void shakeHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t c = GPR_U32(ctx, 4) & 0x1FFFFFFFu;
            int v = -1;
            uint32_t cs = 0;
            if (c >= kCameras && c < kCameras + kCamStride * kMaxCams && (c - kCameras) % kCamStride == 0u)
            {
                v = static_cast<int>((c - kCameras) / kCamStride);
                cs = FAST_READ32(c + kCamCs) & 0x1FFFFFFFu;
                if (!okAddr(cs, 0x20u))
                    v = -1;
            }
            float before[3] = {};
            if (v >= 0)
                for (int i = 0; i < 3; ++i)
                    before[i] = rdF(rdram, cs + 0x10 + 4 * i);
            const uint32_t ra = GPR_U32(ctx, 31);
            g_origShake(rdram, ctx, runtime);
            if (v >= 0 && ctx->pc == ra)
                for (int i = 0; i < 3; ++i)
                    g_cam[v].shake[i] = rdF(rdram, cs + 0x10 + 4 * i) - before[i];
        }

        // Entrada do interpretInputs (a0 = PadFlags, a1 = GamePad). Devolve o jogador forcado (ou -1)
        int inputEnter(uint8_t *rdram, uint32_t pf, uint32_t gp)
        {
            for (Input &in : g_in)
                if (in.pending && in.pf == pf)
                {
                    // a chamada anterior nao voltou por este gancho: devolve os valores do jogo se ninguem os trocou
                    if (FAST_READ32(pf + kPfConfigActive) == 1u && FAST_READ32(pf + kPfConfigWanted) == 1u)
                    {
                        FAST_WRITE32(pf + kPfConfigActive, in.saved4);
                        FAST_WRITE32(pf + kPfConfigWanted, in.saved8);
                    }
                    in.pending = in.forced = false;
                }
            if (!okAddr(pf, kPfOwner + 4u) || !okAddr(gp, 0x14u))
                return -1;
            int slot = -1;
            const uint32_t owner = FAST_READ32(pf + kPfOwner);
            if (!isPlayerMonster(rdram, owner, &slot) || slot < 0 || slot > 3)
                return -1;
            Input &in = g_in[slot];
            uint8_t *b = rdram + gp;
            // mouse (wotm_mouse.hpp): o cursor fica preso durante a luta inteira do jogador do teclado (fora da pausa), mesmo quando
            // outra camera do jogo assume por um instante (queda, arremesso); sem a orbita dirigindo, o deslocamento e descartado
            if (enabled() && slot == wotm::mouse::g_kbPlayer.load(std::memory_order_relaxed))
            {
                // na pausa do jogo o interpretInputs e o Cameras::Update param: sem aviso, a janela solta o cursor em ~150 ms
                if (FAST_READ32(wotm::sym::isPaused) == 0u)
                    wotm::mouse::want();
                if (!in.driving)
                    wotm::mouse::clear();
            }
            if (!in.driving || !enabled())
            {
                in.forced = false;
                in.stick[0] = in.stick[1] = 0.f;
                // com a camera moderna o analogico direito nao gira o monstro: na camera de perto (e nos outros POVs, em que a
                // orbita descansa) o jogo usa o X para girar; o Y (mexer a cabeca) continua indo para o jogo
                if (enabled())
                    b[0x10] = b[0x11] = 0;
                return -1;
            }
            // analogico direito para a orbita, zerado para o jogo
            in.stick[0] = (float(b[0x11]) - float(b[0x10])) / 255.f;
            in.stick[1] = (float(b[0x12]) - float(b[0x13])) / 255.f;
            {
                // zona morta radial: o controle do jogador 2 (parado) subia a camera sozinho com a folga do analogico
                const float mag = std::sqrt(in.stick[0] * in.stick[0] + in.stick[1] * in.stick[1]);
                const float scale = mag > 0.2f ? std::min(1.f, (mag - 0.2f) / 0.8f) / mag : 0.f;
                in.stick[0] *= scale;
                in.stick[1] *= scale;
            }
            b[0x10] = b[0x11] = b[0x12] = b[0x13] = 0;
            const uint32_t owner2 = owner & 0x1FFFFFFFu;
            const uint32_t e4 = FAST_READ32(pf + kPfConfigActive), e8 = FAST_READ32(pf + kPfConfigWanted);
            const bool dead = okAddr(owner2, kMonDead + 1u) && rdram[owner2 + kMonDead] != 0;
            if (dead || e4 != 0u || e8 != 0u)
            {
                in.forced = false;   // o jogo ja pediu a SR (Crush, fim do planeio) ou esta trocando: fica com ele
                return -1;
            }
            // liga na hora, mesmo andando: esperar o analogico parado deixava o controle de tanque ao sair da camera de perto andando
            // (o InitCCMonsters volta o jogo a CR) e o monstro girava em vez de andar ate parar
            {
                // inclinacao do analogico esquerdo (bytes 0xC..0xF: esquerda, direita, cima, baixo): a SR normaliza a direcao e a perde
                const float lx = (float(b[0xD]) - float(b[0xC])) / 255.f, ly = (float(b[0xE]) - float(b[0xF])) / 255.f;
                in.tilt = std::min(1.f, std::sqrt(lx * lx + ly * ly));
            }
            in.forced = true;
            in.pending = true;
            in.pf = pf;
            in.saved4 = e4;
            in.saved8 = e8;
            FAST_WRITE32(pf + kPfConfigActive, 1);
            FAST_WRITE32(pf + kPfConfigWanted, 1);
            return slot;
        }

        // Saida do interpretInputs (so quando a funcao voltou): devolve a configuracao do jogo
        void inputLeave(uint8_t *rdram, int slot)
        {
            if (slot < 0)
                return;
            Input &in = g_in[slot];
            if (!in.pending)
                return;
            if (FAST_READ32(in.pf + kPfConfigActive) == 1u && FAST_READ32(in.pf + kPfConfigWanted) == 1u)
            {
                FAST_WRITE32(in.pf + kPfConfigActive, in.saved4);
                FAST_WRITE32(in.pf + kPfConfigWanted, in.saved8);
                // analogico pela metade (gamepad, ou Alt no teclado): a SR grava frente/tras e os lados com a direcao normalizada (sempre
                // o curso todo); o computeTargetSpeed pesa a velocidade por eles, como na CR. Aqui voltam a seguir a inclinacao.
                if (in.tilt < 0.98f)
                {
                    const uint32_t ring = FAST_READ32(in.pf + kPfRingIndex);
                    if (ring < 60u)
                    {
                        const uint32_t e = in.pf + ring * 0x5Eu;
                        for (const uint32_t off : {0x12u, 0x14u, 0x1Au, 0x1Cu})
                        {
                            uint16_t v;
                            std::memcpy(&v, rdram + e + off, 2);
                            v = static_cast<uint16_t>(std::lround(float(v) * in.tilt));
                            std::memcpy(rdram + e + off, &v, 2);
                        }
                    }
                }
            }
            else
                in.forced = false;   // o jogo mudou a configuracao no meio (agarrao, morte): vale a dele
            in.pending = false;
        }

        // TheGame::SetOkToUnify (comeco de cada quadro): CloseCam = 0 -> ninguem unifica
        PS2Runtime::RecompiledFunction g_origOkToUnify = nullptr;
        void okToUnifyHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t ra = GPR_U32(ctx, 31);
            g_origOkToUnify(rdram, ctx, runtime);
            if (ctx->pc != ra || wotm::settings::on(wotm::settings::CloseCam))
                return;
            const uint32_t slots = std::min<uint32_t>(FAST_READ32(kNumSlots), 16u);
            for (uint32_t i = 0; i < slots; ++i)
                FAST_WRITE32(kSlots + kSlotStride * i + kMonCamUnify, 0);
        }

        // Cameras::LeaveUnifiedTask (a0 = Cameras): com o par separado (1P, nenhuma IA perto), o jogo agenda a saida da camera de
        // perto, mas a tarefa so sai com o monstro parado (isIdle) ou travado: trocar para a camera normal andando mudaria o controle
        // de relativo a tela para tanque. Com a camera moderna o controle continua relativo a tela, entao sai na hora (fora de queda
        // livre), pelo proprio LeaveUnifiedView, como a tarefa faria (com o +0xE68 zerado ele nao remove a tarefa que esta rodando)
        PS2Runtime::RecompiledFunction g_origLeaveTask = nullptr;
        void leaveTaskHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t ra = GPR_U32(ctx, 31), self = GPR_U32(ctx, 4);
            g_origLeaveTask(rdram, ctx, runtime);
            if (ctx->pc != ra || GPR_U32(ctx, 2) == 0u || !enabled() || !FAST_READ32(kUnified))
                return;
            const uint32_t c2 = camAddr(2), mon = FAST_READ32(c2 + kCamMonster) & 0x1FFFFFFFu;
            if (!okAddr(mon, 0x284u) || rdram[mon + 0x280] != 0)   // m_freeFalling
                return;
            constexpr uint32_t kReason = wotm::sym::x_7Cameras_m_leaveReason, kAlpha = wotm::sym::x_7Cameras_m_currentFadeAlpha;
            const uint32_t reason = FAST_READ32(kReason), alpha = FAST_READ32(kAlpha), task = FAST_READ32(c2 + 0xE68u);
            FAST_WRITE32(c2 + 0xE68u, 0);
            Probe pr(rdram, ctx, runtime);
            uint32_t v0 = 0;
            float f0 = 0.f;
            const bool back = pr.call(wotm::fn::LeaveUnifiedView__7CamerasUi, {self, 0xBu}, {}, v0, f0);
            FAST_WRITE32(kReason, reason);
            FAST_WRITE32(kAlpha, alpha);
            if (!back || FAST_READ32(kUnified))
            {
                FAST_WRITE32(c2 + 0xE68u, task);   // nao saiu (modo 8): a tarefa continua esperando
                return;
            }
            FAST_WRITE32(wotm::sym::x_7Cameras_m_leaveTriggered, 0);
            ctx->r[2] = _mm_set_epi64x(0, 0);   // tarefa terminada
            static const bool log = std::getenv("WOTM_ORBITLOG") != nullptr;
            if (log)
                std::fprintf(stderr, "[orbit] camera de perto: saida sem esperar o monstro parar\n");
        }

        void install(PS2Runtime &runtime)
        {
            constexpr uint32_t kShake = wotm::fn::shakeCamera__6Camera, kOkToUnify = wotm::fn::SetOkToUnify__7TheGame,
                               kLeaveTask = wotm::fn::LeaveUnifiedTask__7Cameras;
            if ((g_origShake = runtime.lookupFunction(kShake)))
                runtime.registerFunction(kShake, &shakeHook);
            if ((g_origOkToUnify = runtime.lookupFunction(kOkToUnify)))
                runtime.registerFunction(kOkToUnify, &okToUnifyHook);
            if ((g_origLeaveTask = runtime.lookupFunction(kLeaveTask)))
                runtime.registerFunction(kLeaveTask, &leaveTaskHook);
            std::fprintf(stderr, "[wotm] camera %s (WOTM_CAMERA), recentralizar %s, camera de perto %s (WOTM_CLOSECAM), sensibilidade %d, eixo Y %s%s\n",
                         wotm::settings::on(wotm::settings::Camera) ? "moderna" : "classica",
                         wotm::settings::on(wotm::settings::CamRecenter) ? "ligado" : "desligado",
                         wotm::settings::on(wotm::settings::CloseCam) ? "ligada" : "desligada",
                         wotm::settings::get(wotm::settings::CamSens), wotm::settings::on(wotm::settings::CamInvertY) ? "invertido" : "normal",
                         aiVsAiAll() ? "; com WOTM_AIVSAI fica a classica" : (g_aiVsAi ? "; classica nos bots do WOTM_AIVSAI" : ""));
        }
    }
}
