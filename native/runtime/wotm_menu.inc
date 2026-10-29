// wotm_menu.inc: "press start" do menu principal (tela 20, screenWaitForStart). Incluido pelo wotm_overrides.cpp.
//
// No retail a tela so aceita Start, e so depois que a transicao que a abre termina: na entrada (16 -> 20) a cena clareia do preto
// enquanto a camera recua do telao (animacao 1, 120 quadros), e o screenGetInput devolve 0 enquanto betweenScreens != 0, ou seja,
// ~2 s de botao ignorado. Aqui:
//  - X vale como Start: na tela 20, o codigo 6 (X) do screenGetInput vira 0x10 (Start);
//  - Start/X ja valem durante a transicao para a tela 20: o proprio screenWaitForStart roda na hora (a preparacao da primeira
//    passada, o changeScreen(20 -> 1) e o som), com o Start injetado. A animacao 1 continua (a camera termina o movimento) e a
//    transicao 20 -> 1 usa a animacao 0, como no caminho normal, entao as opcoes aparecem no fade de sempre (40 quadros).
//    O aperto so conta depois de o botao ter sido solto dentro da transicao (o X que pulou o filme nao entra no menu) e so age
//    depois do fade-out da tela anterior (20 quadros); antes disso fica guardado.
// WOTM_MENULOG=1: log das telas, da transicao e dos apertos.
namespace wotm_menu
{
constexpr uint32_t kGetInput = wotm::fn::screenGetInput__Fi;
constexpr uint32_t kTransition = wotm::fn::screenTransition__Fb;
constexpr uint32_t kWaitStart = wotm::fn::screenWaitForStart__Fv;
constexpr uint32_t kPressStart = 20;   // currScreen do "press start"
constexpr uint32_t kCodeStart = 0x10, kCodeX = 6;   // codigos de acao do screenGetInput

static PS2Runtime::RecompiledFunction g_origGetInput = nullptr, g_origTransition = nullptr;
static uint32_t g_inRa = 0;
static bool g_inPending = false;
static bool g_force = false;     // o proximo screenGetInput devolve Start (pulo pelo screenWaitForStart)
static bool g_probing = false;   // leitura do pad feita pelo gancho da transicao
static bool g_armed = false;     // o botao ja foi solto dentro da transicao para a tela 20
static bool g_queued = false;    // aperto guardado ate o fim do fade-out da tela anterior
static bool g_log = false, g_readyLogged = false;
static uint32_t g_realignUntil = 0;   // g_frame ate o qual os icones da legenda seguem a camera (Start antecipado)

static uint32_t rd(const uint8_t *rdram, uint32_t a)
{
    uint32_t v;
    std::memcpy(&v, rdram + (a & 0x1FFFFFFu), 4);
    return v;
}
static void wr(uint8_t *rdram, uint32_t a, uint32_t v) { std::memcpy(rdram + (a & 0x1FFFFFFu), &v, 4); }

// Chama a funcao do jogo em `fn` (a cadeia de ganchos atual) e devolve o $v0; o contexto volta como estava
static uint32_t callGuest(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt, uint32_t fn, uint32_t a0)
{
    const PS2Runtime::RecompiledFunction f = rt->lookupFunction(fn);
    if (!f) return 0;
    const R5900Context saved = *ctx;
    ctx->r[4] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(a0)));
    ctx->r[31] = _mm_set_epi64x(0, 0);
    ctx->pc = fn;
    f(rdram, ctx, rt);
    wotmFinishGuestCall(rdram, ctx, rt);
    const uint32_t v0 = GPR_U32(ctx, 2);
    *ctx = saved;
    return v0;
}

// O showButtons alinha os icones da legenda a camera so nos quadros do fade. Com o Start antecipado a camera (animacao 1) ainda
// anda depois que o fade acaba, e os icones ficariam presos no lugar onde ela estava: aqui eles sao realinhados a cada quadro
// ate a animacao terminar, com os mesmos valores do showButtons (val1_x para o m_buttonCs[3], val2_x para o [1]).
static void realignButtons(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
{
    if (rd(rdram, wotm::sym::g_frame) > g_realignUntil)
        return;
    const PS2Runtime::RecompiledFunction f = rt->lookupFunction(wotm::fn::uiAlignItemWithCamera__FP3_csfff);
    const uint32_t sh = rd(rdram, wotm::sym::shell);
    if (!f || !sh)
        return;
    const uint32_t cs[2] = {rd(rdram, sh + 0x60 + 3 * 4), rd(rdram, sh + 0x60 + 1 * 4)};
    const uint32_t xs[2] = {wotm::sym::val1_x_3061, wotm::sym::val2_x_3064};
    for (int i = 0; i < 2; i++)
    {
        if (!cs[i] || !rdram[(cs[i] + 0xC) & 0x1FFFFFFu])   // drawMe
            continue;
        const R5900Context saved = *ctx;
        ctx->r[4] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(cs[i])));
        std::memcpy(&ctx->f[12], rdram + wotm::sym::val_z_3063, 4);
        std::memcpy(&ctx->f[13], rdram + xs[i], 4);
        std::memcpy(&ctx->f[14], rdram + wotm::sym::val_y_3062, 4);
        ctx->r[31] = _mm_set_epi64x(0, 0);
        ctx->pc = wotm::fn::uiAlignItemWithCamera__FP3_csfff;
        f(rdram, ctx, rt);
        wotmFinishGuestCall(rdram, ctx, rt);
        *ctx = saved;
    }
}

static void getInputHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == kGetInput)
    {
        if (!g_probing && !g_force)
            realignButtons(rdram, ctx, runtime);
        if (g_force)
        {
            g_force = false;
            ctx->r[2] = _mm_set_epi64x(0, kCodeStart);
            ctx->pc = GPR_U32(ctx, 31);
            return;
        }
        if (rd(rdram, wotm::sym::currScreen) == kPressStart)
        {
            g_inRa = GPR_U32(ctx, 31);
            g_inPending = true;
            if (g_log && !g_probing && !g_readyLogged && rd(rdram, wotm::sym::betweenScreens) == 0)
            {
                g_readyLogged = true;
                std::fprintf(stderr, "[menu] g_frame %u: press start aceita entrada\n", rd(rdram, wotm::sym::g_frame));
            }
        }
    }
    g_origGetInput(rdram, ctx, runtime);
    if (!g_inPending || ctx->pc != g_inRa)
        return;
    g_inPending = false;
    const uint32_t v = GPR_U32(ctx, 2);
    if (g_log && !g_probing && (v & 0xFFFFu))
        std::fprintf(stderr, "[menu] g_frame %u: press start, codigo %u\n", rd(rdram, wotm::sym::g_frame), v & 0xFFFFu);
    if ((v & 0xFFFFu) == kCodeX)
        ctx->r[2] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>((v & ~0xFFFFu) | kCodeStart)));
}

// Start/X durante a transicao para a tela 20. Devolve true se o "press start" foi atendido (a transicao deste quadro nao roda).
static bool pressDuringTransition(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (rd(rdram, wotm::sym::screenFirstPass))   // primeira passada: o botao ainda apertado (do filme, por exemplo) nao conta
    {
        g_armed = false;
        g_queued = false;
    }
    if (!g_queued)
    {
        // o screenGetInput so le o pad fora da transicao (betweenScreens == 0)
        g_probing = true;
        wr(rdram, wotm::sym::betweenScreens, 0);
        const uint32_t code = callGuest(rdram, ctx, runtime, kGetInput, 1) & 0xFFFFu;
        wr(rdram, wotm::sym::betweenScreens, 1);
        g_probing = false;
        if (code == 0)
            g_armed = true;
        else if (g_armed && code == kCodeStart)   // o X ja chega como Start pelo getInputHook
            g_queued = true;
        if (g_queued && g_log)
            std::fprintf(stderr, "[menu] g_frame %u: Start/X na transicao para o press start\n", rd(rdram, wotm::sym::g_frame));
    }
    if (!g_queued || rd(rdram, wotm::sym::fadingOut_2980))
        return false;
    g_queued = false;
    // o screenWaitForStart como no fim da transicao: primeira passada (selecoes, controles, esconde o m_menuCs), filme do telao,
    // e o Start injetado faz o changeScreen(20 -> 1) e o som
    wr(rdram, wotm::sym::betweenScreens, 0);
    wr(rdram, wotm::sym::screenFirstPass, 1);
    g_force = true;
    callGuest(rdram, ctx, runtime, kWaitStart, 0);
    g_force = false;
    // transicao 20 -> 1 com a animacao 0, como quando a anterior termina; a animacao 1 (camera) segue sozinha
    wr(rdram, wotm::sym::currAnimationIndex, 0);
    g_realignUntil = rd(rdram, wotm::sym::g_frame) + 150;   // folga sobre os 120 quadros da animacao 1
    if (g_log)
        std::fprintf(stderr, "[menu] g_frame %u: press start antecipado, tela %u\n", rd(rdram, wotm::sym::g_frame), rd(rdram, wotm::sym::currScreen));
    return true;
}

static void transitionHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == kTransition)
    {
        if (g_log)
        {
            static uint32_t lastKey = ~0u;
            const uint32_t cur = rd(rdram, wotm::sym::currScreen), last = rd(rdram, wotm::sym::lastScreen);
            const uint32_t fo = rd(rdram, wotm::sym::fadingOut_2980), fi = rd(rdram, wotm::sym::fadingIn_2979);
            const uint32_t key = cur << 16 | last << 8 | fo << 1 | fi;
            if (key != lastKey)
            {
                lastKey = key;
                g_readyLogged = false;
                float a;
                std::memcpy(&a, rdram + wotm::sym::targetAlpha, 4);
                std::fprintf(stderr, "[menu] g_frame %u: transicao %u -> %u, anim %u, fadeOut %u fadeIn %u, alfa %.2f\n", rd(rdram, wotm::sym::g_frame), last,
                             cur, rd(rdram, wotm::sym::currAnimationIndex), fo, fi, a);
            }
        }
        if (rd(rdram, wotm::sym::currScreen) == kPressStart && rd(rdram, wotm::sym::betweenScreens) &&
            pressDuringTransition(rdram, ctx, runtime))
        {
            ctx->pc = GPR_U32(ctx, 31);
            return;
        }
    }
    g_origTransition(rdram, ctx, runtime);
    realignButtons(rdram, ctx, runtime);
}

static void install(PS2Runtime &runtime)
{
    g_log = std::getenv("WOTM_MENULOG") != nullptr;
    if ((g_origGetInput = runtime.lookupFunction(kGetInput)))
        runtime.registerFunction(kGetInput, &getInputHook);
    if ((g_origTransition = runtime.lookupFunction(kTransition)) && runtime.lookupFunction(kWaitStart))
        runtime.registerFunction(kTransition, &transitionHook);
}
} // namespace wotm_menu
