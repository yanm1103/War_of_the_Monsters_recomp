// wotm_cursor.inc: cursor do mouse do jogo (native/assets/cursor.png) no lugar do cursor do sistema. O do sistema fica escondido sobre a
// janela (HideCursor) e a seta e desenhada por cima do quadro, na posicao do mouse, com a ponta (canto 0,0 da imagem) no ponto do clique.
// Tamanho: kHeight da altura da janela. Preso (camera moderna) o cursor ja some sozinho (DisableCursor). WOTM_CURSOR=0 deixa o do sistema.
// A imagem vem embutida nos executaveis (recurso WOTM_CURSOR do native/wotm.rc); no Linux, assets/cursor.png (wotm_assets.hpp).
// Incluido por wotm_live.inc, depois do wotm_prompts.inc (que declara as funcoes de recurso do Windows).
#include "wotm_assets.hpp"

namespace wotm_cursor
{
static Texture2D g_tex{};
static bool g_tried = false;
constexpr float kHeight = 0.025f;   // altura da seta, em fracao da altura da janela

static bool load()
{
    if (g_tried) return g_tex.id != 0;
    g_tried = true;
#ifdef _WIN32
    void *mod = GetModuleHandleA(nullptr);
    void *res = FindResourceA(mod, "WOTM_CURSOR", reinterpret_cast<const char *>(static_cast<uintptr_t>(10)));   // RT_RCDATA
    void *h = res ? LoadResource(mod, res) : nullptr;
    const unsigned char *data = h ? static_cast<const unsigned char *>(LockResource(h)) : nullptr;
    const int size = res ? static_cast<int>(SizeofResource(mod, res)) : 0;
    Image img = data && size > 0 ? LoadImageFromMemory(".png", data, size) : Image{};
#else
    const std::string path = wotm::assets::file("cursor.png");
    Image img = LoadImage(path.c_str());
#endif
    if (!img.data)
    {
        std::fprintf(stderr, "[cursor] image unavailable: the system cursor stays\n");
        return false;
    }
    g_tex = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&g_tex);
    SetTextureFilter(g_tex, TEXTURE_FILTER_TRILINEAR);
    std::fprintf(stderr, "[cursor] image %dx%d\n", g_tex.width, g_tex.height);
    return true;
}

// Thread da janela, no fim de cada apresentacao (depois do quadro e dos paineis de debug). Esconde o cursor do sistema e desenha a seta.
static void draw()
{
    static const bool off = [] { const char *e = std::getenv("WOTM_CURSOR"); return e && e[0] == '0'; }();
    if (off || !load()) return;
    if (wotm::mouse::g_captured.load(std::memory_order_relaxed)) return;   // preso: o cursor ja esta escondido (DisableCursor)
    if (!IsCursorOnScreen())
        return;   // fora da janela: o do sistema, que o HideCursor so esconde sobre a area do cliente
    if (!IsCursorHidden()) HideCursor();   // o EnableCursor (cursor solto da camera) o mostra de volta
    const Vector2 m = GetMousePosition();
    const float h = std::max(16.f, kHeight * float(GetScreenHeight()));
    const float w = h * float(g_tex.width) / float(g_tex.height);
    DrawTexturePro(g_tex, Rectangle{0.f, 0.f, float(g_tex.width), float(g_tex.height)}, Rectangle{std::floor(m.x), std::floor(m.y), w, h},
                   Vector2{0.f, 0.f}, 0.f, WHITE);
}
}
