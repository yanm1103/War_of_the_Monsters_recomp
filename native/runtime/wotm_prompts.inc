// wotm_prompts.inc: icones de botao 2D (Kenney "Input Prompts", CC0) no lugar dos objetos 3D do McButtons (X, bola, quadrado e
// triangulo da legenda dos menus e das paginas do memory card). Incluido por wotm_live.inc, depois do wotm_pad.inc e do
// wotm_native.hpp. O desenho nativo (Scene::drawButtonSprites) acha onde o jogo pos cada icone e chama icon() com o centro e o tamanho.
//
// Qual sprite: o botao do PS2 do icone vira o que o jogador aperta para ele, pelo mapa de teclas e botoes (wotm_settings.hpp, aba
// CONTROLS) e pela troca de confirmar dos menus (MenuConfirm, wotm_pad.inc). Estilo (opcao Prompts): automatico = o ultimo dispositivo
// usado (teclado: a tecla; gamepad: pelo nome dele, Xbox, PlayStation ou Nintendo), ou fixo; "original" deixa os 3D do jogo.
// O atlas (native/assets/prompts.png, tools/prompts_atlas.py) vem embutido nos executaveis (recurso WOTM_PROMPTS do native/wotm.rc).
// Face buttons no estilo PlayStation (e no "original", nos icones 2D): os do proprio jogo, a textura 163 do SHELL/SHELL.TEX do disco
// (T8 64x64, paleta tbp 519 do SHELL.RTX; bola, quadrado, triangulo e X em quadrantes de 32), no lugar dos do Kenney.
#include "../wotm_prompts.h"
#include "wotm_model.hpp"   // decodeT8, unzipIe
#include "wotm_assets.hpp"

#ifdef _WIN32
// Read the embedded Windows resource without including windows.h (which conflicts with raylib).
extern "C" __declspec(dllimport) void *__stdcall GetModuleHandleA(const char *);
extern "C" __declspec(dllimport) void *__stdcall FindResourceA(void *module, const char *name, const char *type);
extern "C" __declspec(dllimport) void *__stdcall LoadResource(void *module, void *res);
extern "C" __declspec(dllimport) void *__stdcall LockResource(void *data);
extern "C" __declspec(dllimport) unsigned long __stdcall SizeofResource(void *module, void *res);
#endif

namespace wotm_prompts
{
namespace st = wotm::settings;
namespace pr = wotm::prompts;

static Texture2D g_atlas{};
static bool g_tried = false;

static bool loadAtlas()
{
    if (g_tried) return g_atlas.id != 0;
    g_tried = true;
#ifdef _WIN32
    void *mod = GetModuleHandleA(nullptr);
    void *res = FindResourceA(mod, "WOTM_PROMPTS", reinterpret_cast<const char *>(static_cast<uintptr_t>(10)));   // RT_RCDATA
    void *h = res ? LoadResource(mod, res) : nullptr;
    const unsigned char *data = h ? static_cast<const unsigned char *>(LockResource(h)) : nullptr;
    const int size = res ? static_cast<int>(SizeofResource(mod, res)) : 0;
    Image img = data && size > 0 ? LoadImageFromMemory(".png", data, size) : Image{};
#else
    const std::string path = wotm::assets::file("prompts.png");
    Image img = LoadImage(path.c_str());
#endif
    if (!img.data)
    {
        std::fprintf(stderr, "[prompts] prompt atlas unavailable: game button icons will be used\n");
        return false;
    }
    g_atlas = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&g_atlas);
    SetTextureFilter(g_atlas, TEXTURE_FILTER_TRILINEAR);
    std::fprintf(stderr, "[prompts] atlas %dx%d\n", g_atlas.width, g_atlas.height);
    return true;
}

// Face buttons do jogo (thread da janela). Celulas virtuais kFaceBase + 0..3 (triangulo, bola, X, quadrado); o quadrante de cada um
// na textura como o disco a guarda (linhas de baixo para cima: o recorte vira na hora de desenhar).
constexpr int kFaceBase = 1000;
static Texture2D g_face{};
static bool g_faceTried = false;
static constexpr int kFaceQuad[4][2] = {{0, 32}, {0, 0}, {32, 32}, {32, 0}};
static bool loadFace()
{
    if (g_faceTried) return g_face.id != 0;
    g_faceTried = true;
    const PS2Runtime::IoPaths &io = PS2Runtime::getIoPaths();
    std::vector<uint8_t> tex, rtx;
    wotm::model::TexImage im;
    if (!wotm::disc::readFile(io.cdRoot, io.cdImage.string(), "SHELL/SHELL.TEX", tex) ||
        !wotm::disc::readFile(io.cdRoot, io.cdImage.string(), "SHELL/SHELL.RTX", rtx) || !wotm::model::unzipIe(tex) ||
        !wotm::model::unzipIe(rtx) || !wotm::model::decodeT8(tex, rtx, 163, 519, im) || im.w != 64 || im.h != 64)
    {
        std::fprintf(stderr, "[prompts] face buttons do jogo (SHELL.TEX 163) nao lidos: ficam os do Kenney\n");
        return false;
    }
    Image img{im.rgba.data(), im.w, im.h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    g_face = LoadTextureFromImage(img);
    SetTextureFilter(g_face, TEXTURE_FILTER_BILINEAR);
    std::fprintf(stderr, "[prompts] face buttons do jogo carregados (SHELL.TEX 163)\n");
    return true;
}
// Recorte do face button `cell` (kFaceBase + i) na textura, para o DrawTexturePro: altura negativa vira a imagem (o recorte fica no
// mesmo lugar)
static Rectangle faceSrc(int cell)
{
    const int *q = kFaceQuad[cell - kFaceBase];
    return Rectangle{float(q[0]), float(q[1]), 32.f, -32.f};
}

// Ultimo dispositivo usado (thread da janela, uma vez por quadro): teclado ou o gamepad n
static int g_lastPad = -1;   // -1 = teclado
static double g_lastPoll = -1.0;
static void pollDevice()
{
    const double now = GetTime();
    if (now == g_lastPoll) return;
    g_lastPoll = now;
    for (const st::KeyName &k : st::kKeyNames)
        if (wotm_pad::inputDown(k.code)) { g_lastPad = -1; return; }   // teclas e botoes do mouse
    for (int p = 0; p < 4; ++p)
    {
        if (!IsGamepadAvailable(p)) continue;
        bool any = false;
        for (int b = 1; b < st::kPadButtons && !any; ++b) any = IsGamepadButtonDown(p, b);
        for (int a = 0; a < 4 && !any; ++a) any = std::fabs(GetGamepadAxisMovement(p, a)) > 0.5f;
        if (any) { g_lastPad = p; return; }
    }
    // sem uso ainda: o primeiro gamepad conectado, se houver
    static bool first = true;
    if (first)
    {
        first = false;
        for (int p = 0; p < 4; ++p)
            if (IsGamepadAvailable(p)) { g_lastPad = p; break; }
    }
}

static int styleOfPad(int pad)
{
    std::string n = pad >= 0 && IsGamepadAvailable(pad) ? GetGamepadName(pad) : "";
    for (char &c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto has = [&](const char *s) { return n.find(s) != std::string::npos; };
    if (has("playstation") || has("dualshock") || has("dualsense") || has("ps3") || has("ps4") || has("ps5") || has("sony") ||
        has("wireless controller"))
        return st::PromptPs;
    if (has("nintendo") || has("switch") || has("pro controller") || has("joy-con"))
        return st::PromptNintendo;
    return st::PromptXbox;
}

// Icone do McButtons -> botao do PS2. Indice: 0x1F51.. direto; 0x240E.. pela tabela do initButton. Ordem dos indices: a das letras do
// s_charMap do drawButtonsForString (' ^ ` $), que e a dos bits do scePadRead (triangulo 0x1000, bola 0x2000, X 0x4000, quadrado
// 0x8000). Conferido: "press the ' button to cancel" / "press the ` button to continue" no memory card; no Shell o 0x240F (SELECT)
// sai X e o 0x240E (BACK) triangulo.
static constexpr st::Bind kIndexBind[4] = {st::BTriangle, st::BCircle, st::BCross, st::BSquare};
static int buttonIndex(uint16_t id)
{
    if (id >= 0x1F51 && id <= 0x1F54) return id - 0x1F51;
    static constexpr int k240E[4] = {0, 2, 3, 1};
    if (id >= 0x240E && id <= 0x2411) return k240E[id - 0x240E];
    return -1;
}

// Estilo dos icones agora: o automatico resolvido pelo ultimo dispositivo; o "original" (3D do jogo) vira o do PS2 nos icones 2D que o
// jogo nao tem (legenda de baixo, wotm_legend.inc)
static int currentStyle()
{
    pollDevice();
    const int s = st::get(st::Prompts);
    if (s == st::PromptAuto) return g_lastPad < 0 ? st::PromptKeyboard : styleOfPad(g_lastPad);
    return s == st::PromptOriginal ? st::PromptPs : s;
}
static int keyCell(const char *name)
{
    if (name)
        for (const pr::KeyCell &k : pr::kKeys)
            if (!std::strcmp(k.name, name)) return k.cell;
    return -1;
}
static int padCell(int phys, int style)
{
    if (phys <= 0 || phys >= pr::kPadCells) return -1;   // 18 e 19: analogico esquerdo e direito (so na legenda, kStickL/kStickR)
    // face buttons (raylib 5..8: cima, direita, baixo, esquerda) no estilo do PS2: os do jogo
    if (style == st::PromptPs && phys >= 5 && phys <= 8 && loadFace())
    {
        static constexpr int kFace[4] = {0, 1, 2, 3};   // triangulo, bola, X, quadrado
        return kFaceBase + kFace[phys - 5];
    }
    const int *table = style == st::PromptNintendo ? pr::kNintendo : style == st::PromptXbox ? pr::kXbox : pr::kPs;
    return table[phys];
}

// Celula do atlas para o botao do PS2 b, no estilo de agora (gamepad: pelo mapa do jogador do ultimo gamepad usado)
static int cellFor(st::Bind b)
{
    const int style = currentStyle();
    if (style == st::PromptKeyboard)
    {
        const char *name = st::keyName(int(st::kb(b) & 0xFFFF));
        // botao do meio e rodinha: o icone da rodinha; L1/R1 sem tecla (layout 2): Q/E, as abas do painel
        if (name && (!std::strcmp(name, "MOUSE_MIDDLE") || !std::strncmp(name, "WHEEL", 5))) name = "MOUSE_SCROLL";
        if (!name && (b == st::BL1 || b == st::BR1)) name = b == st::BL1 ? "Q" : "E";
        const int c = keyCell(name);
        if (c >= 0) return c;   // sem tecla (ou tecla sem desenho): o simbolo do PS2
    }
    return padCell(st::pad(wotm_pad::sourceBind(b), wotm_pad::playerOfPad(g_lastPad)), style == st::PromptKeyboard ? st::PromptPs : style);
}

static void icon(uint16_t id, const float mid[3], const float right[3], const float up[3], float size, float alpha)
{
    const int idx = buttonIndex(id);
    static uint32_t logged = 0;
    if (idx >= 0 && !(logged & (1u << idx)) && std::getenv("WOTM_MENULOG"))
    {
        logged |= 1u << idx;
        std::fprintf(stderr, "[prompts] icone id=0x%X indice=%d -> %s\n", unsigned(id), idx, st::kBindNames[kIndexBind[idx]]);
    }
    const int cell = idx >= 0 ? cellFor(kIndexBind[idx]) : -1;
    if (cell < 0 || !loadAtlas()) return;
    const bool face = cell >= kFaceBase;
    // a arte do Kenney tem folga na celula (o circulo ocupa ~3/4): o lado do quadrado e maior que o do icone do jogo; o do jogo enche
    // o quadrante
    const float h = size * 0.5f * (face ? 1.f : 128.f / 96.f);
    float u0, v0, u1, v1;
    if (face)
    {
        const Rectangle r = faceSrc(cell);
        u0 = r.x / 64.f; u1 = (r.x + r.width) / 64.f; v0 = (r.y + 32.f) / 64.f; v1 = r.y / 64.f;   // virado: a linha de baixo em cima
    }
    else
    {
        u0 = float(cell % pr::kCols) / float(pr::kCols); v0 = float(cell / pr::kCols) / float(pr::kRows);
        u1 = u0 + 1.f / float(pr::kCols); v1 = v0 + 1.f / float(pr::kRows);
    }
    // o texto da legenda comeca logo depois do icone do jogo; a arte 2D encosta nele, entao o icone anda um pouco para a esquerda
    const float shift = -size * 0.16f;
    auto corner = [&](float sr, float su, float out[3]) {
        for (int k = 0; k < 3; ++k) out[k] = mid[k] + right[k] * (sr * h + shift) + up[k] * su * h;
    };
    float a[3], b[3], c[3], d[3];
    corner(-1, 1, a); corner(-1, -1, b); corner(1, -1, c); corner(1, 1, d);
    BeginBlendMode(BLEND_ALPHA);
    rlDisableBackfaceCulling();
    rlSetTexture(face ? g_face.id : g_atlas.id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, static_cast<unsigned char>(std::clamp(alpha, 0.f, 1.f) * 255.f));
    rlTexCoord2f(u0, v0); rlVertex3f(a[0], a[1], a[2]);
    rlTexCoord2f(u0, v1); rlVertex3f(b[0], b[1], b[2]);
    rlTexCoord2f(u1, v1); rlVertex3f(c[0], c[1], c[2]);
    rlTexCoord2f(u1, v0); rlVertex3f(d[0], d[1], d[2]);
    rlEnd();
    rlSetTexture(0);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    EndBlendMode();
}

static bool on() { return st::get(st::Prompts) != st::PromptOriginal && loadAtlas(); }

// Celula na tela (no alvo atual), quadrado de lado `size` centrado em (cx, cy); a arte tem folga na celula, como no icon()
static bool drawCell2d(int cell, float cx, float cy, float size, float alpha)
{
    if (cell < 0 || !loadAtlas()) return false;
    if (cell >= kFaceBase)
    {
        DrawTexturePro(g_face, faceSrc(cell), Rectangle{cx - size * 0.5f, cy - size * 0.5f, size, size}, Vector2{0.f, 0.f}, 0.f,
                       Color{255, 255, 255, static_cast<unsigned char>(std::clamp(alpha, 0.f, 1.f) * 255.f)});
        return true;
    }
    const float h = size * (128.f / 96.f), k = float(pr::kCell);
    DrawTexturePro(g_atlas, Rectangle{float(cell % pr::kCols) * k, float(cell / pr::kCols) * k, k, k}, Rectangle{cx - h * 0.5f, cy - h * 0.5f, h, h},
                   Vector2{0.f, 0.f}, 0.f, Color{255, 255, 255, static_cast<unsigned char>(std::clamp(alpha, 0.f, 1.f) * 255.f)});
    return true;
}

static const bool s_installed = (wotm::g_buttonIconFn = &icon, wotm::g_buttonIconsOn = &on, true);
} // namespace wotm_prompts

// Legenda de baixo (na faixa preta, no lugar da do jogo) e dica grande sobre a cena: quem as quer (escolha de controles do
// wotm_padsel.inc, painel do wotm_mainmenu.inc) as publica a cada quadro no renderUi dele; o wotm_legend.inc as desenha no quadro e
// as esvazia. Tudo na thread da janela.
namespace wotm_legend
{
namespace st = wotm::settings;
// Um icone: o botao do raylib (gamepad, 0 = nenhum) e a tecla (nome do wotm_settings.hpp; sem celula no atlas, sai o nome numa
// moldura); key2 = a segunda tecla do mesmo botao (o mapa do teclado tem duas por botao), lado a lado com a primeira
struct Icon { int pad = 0; const char *key = nullptr; const char *key2 = nullptr; };
struct Item { Icon a, b; const char *text = ""; };
constexpr int kStickL = 18, kStickR = 19;   // Icon::pad dos analogicos (celulas extras do atlas, wotm_prompts.h)
constexpr int kMax = 10;
static Item g_items[kMax];
static int g_n = 0;
static float g_alpha = 0.f;
static float g_barAlpha = 1.f;   // opacidade da faixa preta (o modo foto a deixa translucida sobre a cena)
static std::string g_hint;
static float g_hintAlpha = 0.f;

static void post(std::initializer_list<Item> items, float alpha, float barAlpha = 1.f)
{
    g_n = 0;
    for (const Item &i : items)
        if (g_n < kMax) g_items[g_n++] = i;
    g_alpha = alpha;
    g_barAlpha = barAlpha;
}
static void hint(const char *s, float alpha)
{
    g_hint = s ? s : "";
    g_hintAlpha = alpha;
}
// O botao do PS2 b como o jogo o le: a tecla e o botao do mapa (aba CONTROLS), com a troca de confirmar dos menus
static Icon bind(st::Bind b)
{
    const uint32_t k = st::kb(b);
    const char *k1 = st::keyName(int(k & 0xFFFF)), *k2 = st::keyName(int(k >> 16));
    if (!k1) { k1 = k2; k2 = nullptr; }
    return Icon{st::pad(wotm_pad::sourceBind(b), wotm_pad::playerOfPad(wotm_prompts::g_lastPad)), k1, k2};
}
} // namespace wotm_legend
