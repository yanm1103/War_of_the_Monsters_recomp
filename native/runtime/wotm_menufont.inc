// wotm_menufont.inc: as palavras da lista do menu (minusculas brancas desenhadas a mao, com contorno preto) montadas com as letras das
// proprias imagens do jogo (incluido por wotm_live.inc, antes do wotm_mainmenu.inc, que desenha com ele).
//
// O jogo grava cada item do menu como uma palavra pronta nas texturas T4 81, 142 e 162 do SHELL/SHELL.TEX (paletas tbp 239, 480 e 518
// do SHELL.RTX). As palavras que o jogo tem ("options", "load game", "unlocks", "adventure", "free-for-all", "minigames") saem
// inteiras; as nossas ("single player", "multiplayer", "recompiled options", "quit game", e "endurance"/"elimination", que no jogo so
// existem em cinza) saem de trechos dessas palavras: em cada passo, o maior trecho de alguma palavra que casa com o que falta (as
// frases "sudden death" e "unlimited play", que o jogo apertou, so quando a letra nao existe em outra). A fonte
// nao tem b, j, q, x nem z; o q e o p espelhado.
//
// kLines: cada palavra (ou par de palavras) das texturas, ja virada para leitura: textura, faixa de y, linha de base, altura do x e o
// x de cada letra (so o branco: o contorno preto encosta na letra do lado). Um recorte vai ate o meio do vao para cada lado (no maximo
// 4 px alem do branco), assim os recortes juntos ficam com o espacamento do jogo. Medidas feitas nas texturas exportadas (sem
// nenhum pixel do jogo no repo).
namespace wotm_menufont
{
struct Line
{
    int tex;           // indice em kTexIds
    int y0, y1;        // faixa da linha (com o contorno)
    float base, xh;    // linha de base e altura do x (mediana das letras sem haste)
    const char *text;  // as letras, na ordem (espaco separa palavras e nao conta como letra)
    int seg[14][2];    // x do branco de cada letra
    bool narrow = false;   // frase apertada pelo jogo (letras mais estreitas)
};
constexpr uint16_t kTexIds[3] = {81, 142, 162}, kPalTbp[3] = {239, 480, 518};
constexpr Line kLines[] = {
    {0, 5, 54, 50.0f, 29.0f, "free-for-all", {{6, 20}, {27, 41}, {48, 65}, {73, 90}, {97, 112}, {118, 132}, {138, 155}, {163, 177}, {182, 197}, {204, 221}, {229, 236}, {244, 251}}},
    {0, 61, 119, 105.0f, 31.0f, "load game", {{18, 25}, {32, 49}, {56, 73}, {81, 100}, {130, 149}, {157, 174}, {182, 215}, {222, 239}}},
    {0, 121, 175, 160.0f, 30.0f, "minigames", {{16, 49}, {57, 63}, {71, 91}, {97, 103}, {111, 130}, {138, 155}, {163, 196}, {203, 220}, {228, 243}}},
    {1, 122, 179, 164.0f, 29.0f, "options", {{13, 31}, {38, 57}, {63, 80}, {88, 94}, {101, 119}, {125, 146}, {152, 168}}},
    {1, 189, 248, 231.0f, 29.0f, "player", {{14, 33}, {40, 48}, {54, 72}, {78, 103}, {109, 128}, {135, 150}}},
    {2, 3, 51, 47.0f, 29.0f, "adventure", {{16, 33}, {41, 60}, {67, 89}, {95, 112}, {121, 141}, {147, 163}, {171, 192}, {200, 214}, {221, 238}}},
    {2, 67, 115, 111.0f, 29.0f, "sudden death", {{7, 20}, {26, 44}, {50, 66}, {71, 87}, {94, 109}, {114, 131}, {150, 166}, {173, 187}, {192, 207}, {212, 226}, {231, 247}}, true},
    {2, 132, 192, 176.0f, 29.0f, "unlimited play", {{7, 23}, {29, 44}, {49, 55}, {61, 65}, {71, 96}, {102, 107}, {112, 125}, {130, 144}, {149, 164}, {182, 197}, {202, 208}, {213, 226}, {231, 250}}, true},
    {2, 195, 243, 238.0f, 29.0f, "unlocks", {{13, 36}, {42, 63}, {69, 77}, {84, 102}, {108, 125}, {131, 153}, {160, 176}}},
};
// Algarismos 0..9 da textura 142 (os do "first to" e do numero de CPUs), em duas fileiras: x do branco, faixa de y e linha de base
struct Digit { int x0, x1, row; };
constexpr Digit kDigits[10] = {{10, 27, 0}, {50, 57, 0}, {80, 97, 0}, {120, 136, 0}, {159, 180, 0},
                               {202, 219, 0}, {13, 32, 1}, {57, 77, 1}, {102, 121, 1}, {146, 166, 1}};
constexpr int kDigitY[2][2] = {{11, 57}, {62, 113}};
constexpr float kDigitBase[2] = {51.f, 107.f};
constexpr float kRefXh = 30.f;   // unidades das pecas: altura do x = kRefXh
constexpr float kWordGap = 14.f;

static Texture2D g_tex[3] = {};
static bool g_tried = false, g_ok = false;

// Thread da janela
static bool load()
{
    if (g_tried) return g_ok;
    g_tried = true;
    const PS2Runtime::IoPaths &io = PS2Runtime::getIoPaths();
    std::vector<uint8_t> tex, rtx;
    if (!wotm::disc::readFile(io.cdRoot, io.cdImage.string(), "SHELL/SHELL.TEX", tex) ||
        !wotm::disc::readFile(io.cdRoot, io.cdImage.string(), "SHELL/SHELL.RTX", rtx) || !wotm::model::unzipIe(tex) || !wotm::model::unzipIe(rtx))
    {
        std::fprintf(stderr, "[menufont] SHELL.TEX/RTX nao lidos: a lista fica na fonte de maiusculas\n");
        return false;
    }
    for (int i = 0; i < 3; ++i)
    {
        wotm::model::TexImage im;
        if (!wotm::model::decodeT4(tex, rtx, kTexIds[i], kPalTbp[i], im) || im.w != 256 || im.h != 256)
        {
            std::fprintf(stderr, "[menufont] textura %d do SHELL.TEX nao lida: a lista fica na fonte de maiusculas\n", kTexIds[i]);
            for (int j = 0; j < i; ++j) UnloadTexture(g_tex[j]);
            return false;
        }
        // gravada de baixo para cima: a tabela esta na ordem de leitura (o ImageFlipVertical do raylib liberaria o buffer do vector)
        for (int y = 0; y < im.h / 2; ++y)
            std::swap_ranges(im.rgba.begin() + y * im.w, im.rgba.begin() + (y + 1) * im.w, im.rgba.begin() + (im.h - 1 - y) * im.w);
        Image img{im.rgba.data(), im.w, im.h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        g_tex[i] = LoadTextureFromImage(img);
        SetTextureFilter(g_tex[i], TEXTURE_FILTER_BILINEAR);
    }
    std::fprintf(stderr, "[menufont] palavras do menu com as letras do jogo (SHELL.TEX 81/142/162)\n");
    return g_ok = true;
}

struct Piece
{
    int tex;
    Rectangle src;   // na textura (largura negativa: espelhado)
    float x, top, w, h;   // em unidades de kRefXh; top relativo a linha de base
    bool flipY = false;   // virado de cabeca para baixo (o w: o m de "minigames" girado)
};

static int letters(const Line &l) { int n = 0; for (const char *c = l.text; *c; ++c) n += *c != ' '; return n; }

// Recorte das letras a..b (indices sem os espacos) de uma linha
static Rectangle cut(const Line &l, int a, int b)
{
    const int n = letters(l);
    const float x0 = a > 0 ? std::max((l.seg[a - 1][1] + l.seg[a][0]) * 0.5f, l.seg[a][0] - 4.f) : l.seg[a][0] - 4.f;
    const float x1 = b + 1 < n ? std::min((l.seg[b][1] + l.seg[b + 1][0]) * 0.5f, l.seg[b][1] + 4.f) : l.seg[b][1] + 4.f;
    return Rectangle{x0, float(l.y0), x1 - x0, float(l.y1 - l.y0)};
}

// Monta `s` (minusculas, algarismos, '-' e espaco); false se alguma letra nao existe. Com `rotW`, o w e o m de "minigames" girado (a tela dos
// renascimentos do last monster standing; as listas seguem sem w)
static bool compose(const char *s, std::vector<Piece> &out, float &width, bool rotW = false)
{
    out.clear();
    float x = 0.f;
    for (size_t k = 0; s[k];)
    {
        if (s[k] == ' ') { x += kWordGap; ++k; continue; }
        if (s[k] >= '0' && s[k] <= '9')
        {   // na altura de origem: as letras das texturas tem altura do x ~30 = kRefXh
            const Digit &d = kDigits[s[k] - '0'];
            const Rectangle r{float(d.x0 - 4), float(kDigitY[d.row][0]), float(d.x1 - d.x0 + 9), float(kDigitY[d.row][1] - kDigitY[d.row][0])};
            out.push_back(Piece{1, r, x, float(kDigitY[d.row][0]) - kDigitBase[d.row], r.width, r.height});
            x += r.width;
            ++k;
            continue;
        }
        int bestL = -1, bestA = 0, bestN = 0;
        // primeiro as linhas de letra normal; "sudden death" e "unlimited play" (frases que o jogo apertou: o e tem 14 px, nas outras 17)
        // so para letra que nao existe nelas
        for (int pass = 0; pass < 2 && bestL < 0; ++pass)
        for (int li = 0; li < int(std::size(kLines)); ++li)
        {
            if (kLines[li].narrow != (pass == 1)) continue;
            // trechos de uma palavra so da linha; a = indice da letra sem contar espacos
            const char *t = kLines[li].text;
            int idx = 0;
            for (size_t i = 0; t[i]; ++i)
            {
                if (t[i] == ' ') continue;
                int n = 0;
                while (t[i + n] && t[i + n] != ' ' && s[k + n] && s[k + n] == t[i + n]) ++n;
                if (n > bestN) { bestL = li; bestA = idx; bestN = n; }
                ++idx;
            }
        }
        bool mirror = false;
        if (bestL < 0 && s[k] == 'q')   // q: o p de "options" espelhado
        {
            bestL = 3; bestA = 1; bestN = 1; mirror = true;
        }
        if (bestL < 0 && s[k] == 'w' && rotW)
        {
            const Line &l = kLines[2];
            const Rectangle r = cut(l, 0, 0);
            const float sc = kRefXh / l.xh, w = r.width * sc * 0.85f;   // mais estreito que o m
            // girado: a faixa do x (base - xh .. base) cai em y1 - base .. do topo da caixa; a caixa sobe para ela ficar na linha de base
            out.push_back(Piece{l.tex, Rectangle{r.x, r.y, -r.width, -r.height}, x, (l.base - l.xh - float(l.y1)) * sc, w, r.height * sc, true});
            x += w;
            ++k;
            continue;
        }
        if (bestL < 0) return false;
        const Line &l = kLines[bestL];
        Rectangle r = cut(l, bestA, bestA + bestN - 1);
        const float sc = kRefXh / l.xh;
        out.push_back(Piece{l.tex, r, x, (float(l.y0) - l.base) * sc, r.width * sc, r.height * sc});
        if (mirror) out.back().src.width = -r.width;
        x += r.width * sc;
        k += size_t(bestN);
    }
    width = x;
    return !out.empty();
}

// Largura de `s` (em unidades de kRefXh); < 0 se nao da para montar
static float width(const char *s, bool rotW = false)
{
    std::vector<Piece> p;
    float w = 0.f;
    return load() && compose(s, p, w, rotW) ? w : -1.f;
}

// Desenha `s` centrado em cx, linha de base em `base`, altura do x `xh` (pixels do alvo); `c` ja em RGB do raylib
static bool draw(const char *s, float cx, float base, float xh, Color c, bool rotW = false)
{
    std::vector<Piece> p;
    float w = 0.f;
    if (!load() || !compose(s, p, w, rotW)) return false;
    const float sc = xh / kRefXh, x0 = cx - w * sc * 0.5f;
    for (const Piece &pc : p)
        DrawTexturePro(g_tex[pc.tex], pc.src, Rectangle{x0 + pc.x * sc, base + pc.top * sc, pc.w * sc, pc.h * sc}, Vector2{0, 0}, 0.f, c);
    return true;
}
} // namespace wotm_menufont
