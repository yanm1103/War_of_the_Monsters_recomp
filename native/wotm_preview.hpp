// wotm_preview.hpp: previas das opcoes da aba VIDEO do painel "Recompiled options" (wotm_mainmenu.inc). Cada opcao mostra a mesma
// cena pequena, gerada aqui (chao de ladrilhos, dois predios com janelas, um poste com fio, ceu de fim de tarde; nada do disco),
// desenhada com o valor que esta escolhido, e muda junto quando o valor muda:
//   - Sharpness (RESOLUTION, RENDER SCALE, ANTI-ALIASING): lupa de 4x num pedaco da tela com a quina de um predio e o fio contra o
//     ceu. O pedaco e desenhado com as linhas que ele teria no quadro nativo (resolucao x escala), com o MSAA escolhido, e levado as
//     linhas da janela com o bilinear da apresentacao (o DrawTexturePro do quadro no wotm_live.inc); a lupa amplia sem filtro.
//   - Filter (TEXTURE FILTER): o chao ate o horizonte, com o filtro escolhido (mipmaps e anisotropico como o Scene::applyTexFilter),
//     em lupa de 2x.
//   - Aspect (ASPECT RATIO): o monitor (a forma dele) com o quadro na proporcao escolhida, como na tela cheia (faixas pretas), e a
//     cena Hor+ (mais dos lados).
//   - Display (DISPLAY MODE): o monitor com a janela sobre a area de trabalho, ou o quadro na tela inteira.
//   - Vsync (VSYNC): a cena andando de lado; sem vsync a imagem rasga numa linha que muda de lugar (metade de cima e de baixo de
//     momentos diferentes).
//   - Interp (INTERPOLATE FRAMES): a cena andando de lado, mais rapido; desligada, a camera anda em degraus de 30 por segundo,
//     ligada, continua (ilustracao: no jogo e 60 contra a taxa do monitor; o menu redesenha a 60, que basta para ver a diferenca).
// Thread da janela: prepare() fora de qualquer BeginTextureMode (desenha nas texturas proprias), draw() dentro do do painel.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "raylib.h"
#include "rlgl.h"
#include "wotm_msaa.hpp"

namespace wotm::preview
{
enum Kind { None, Sharpness, Filter, Aspect, Display, Vsync, Interp };

struct Params
{
    int frameRows = 720;        // linhas do quadro nativo (resolucao x escala)
    int winRows = 720;          // linhas da janela, onde a apresentacao poe o quadro
    float boxScreenRows = 200;  // quantas linhas da janela a caixa da previa ocupa (aproximado: o painel esta no telao)
    int samples = 0;            // MSAA (0, 2, 4, 8)
    int filter = 0;             // TEXTURE FILTER: 0 bilinear, 1 trilinear, 2..16 anisotropico
    float winAspect = 16.f / 9.f;
    float fixedAspect = 0.f;    // ASPECT RATIO: 0 = dinamica (segue a janela)
    int display = 0;            // 0 janela, 1 sem borda, 2 exclusiva
    float monAspect = 16.f / 9.f;
    bool vsync = true;
    bool interp = false;        // INTERPOLATE FRAMES
    double time = 0.0;
};

class Renderer
{
public:
    static constexpr float kZoom = 4.f;         // lupa da nitidez, em pixels da janela
    static constexpr float kFilterZoom = 2.f;   // lupa do filtro de textura

    ~Renderer() = default;

    // Desenha nas texturas da previa `k` para uma caixa de boxW x boxH (pixels da textura do painel)
    void prepare(Kind k, const Params &p, float boxW, float boxH)
    {
        if (k == None || boxW <= 0.f || boxH <= 0.f) return;
        ensureGround();
        const float boxAspect = boxW / boxH;
        switch (k)
        {
        case Sharpness:
        {
            // pedaco da janela que a lupa mostra, e o mesmo pedaco no quadro nativo
            const int winH = std::max(10, int(std::lround(p.boxScreenRows / kZoom)));
            const int winW = std::max(10, int(std::lround(winH * boxAspect)));
            const double k2 = double(p.frameRows) / double(std::max(1, p.winRows));
            const int frH = std::clamp(int(std::lround(winH * k2)), 4, 2048), frW = std::clamp(int(std::lround(winW * k2)), 4, 4096);
            ensureRt(frame_, frW, frH, TEXTURE_FILTER_BILINEAR);
            const bool ms = ms_.ensure(frW, frH, p.samples, "previa");
            BeginTextureMode(ms ? ms_.render() : frame_);
            drawWorld(edgeCamera(), 0);
            EndTextureMode();
            if (ms) ms_.resolve(frame_.id);
            // a apresentacao: o quadro levado as linhas da janela com bilinear
            ensureRt(win_, winW, winH, TEXTURE_FILTER_POINT);
            BeginTextureMode(win_);
            ClearBackground(BLACK);
            rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
            BeginBlendMode(BLEND_CUSTOM);
            DrawTexturePro(frame_.texture, Rectangle{0.f, 0.f, float(frW), -float(frH)}, Rectangle{0.f, 0.f, float(winW), float(winH)},
                           Vector2{0.f, 0.f}, 0.f, WHITE);
            EndBlendMode();
            EndTextureMode();
            break;
        }
        case Filter:
        {
            const int h = std::clamp(int(std::lround(p.boxScreenRows / kFilterZoom)), 32, 1024), w = int(std::lround(h * boxAspect));
            ensureRt(win_, w, h, TEXTURE_FILTER_POINT);
            applyFilter(p.filter);
            BeginTextureMode(win_);
            drawWorld(groundCamera(), 0);
            EndTextureMode();
            break;
        }
        case Aspect:
        case Display:
        {
            Rectangle mon, pic;
            layoutScreen(k, p, Rectangle{0.f, 0.f, boxW, boxH}, mon, pic);
            const int h = std::clamp(int(std::lround(p.boxScreenRows * 0.8f)), 32, 720), w = std::max(8, int(std::lround(h * pic.width / pic.height)));
            ensureRt(win_, w, h, TEXTURE_FILTER_BILINEAR);
            applyFilter(0);
            BeginTextureMode(win_);
            drawWorld(streetCamera(0.f), 0);
            EndTextureMode();
            break;
        }
        case Vsync:
        {
            const int h = std::clamp(int(std::lround(p.boxScreenRows * 0.8f)), 32, 720), w = std::max(8, int(std::lround(h * boxAspect)));
            ensureRt(win_, w, h, TEXTURE_FILTER_BILINEAR);
            ensureRt(prev_, w, h, TEXTURE_FILTER_BILINEAR);
            applyFilter(0);
            // a camera vai e volta de lado, com velocidade constante; a metade de baixo sem vsync e de um momento antes (exagerado
            // para se ver)
            auto pan = [](double t) {
                const double u = t * 0.25 - std::floor(t * 0.25);
                return float(u < 0.5 ? -12.0 + 48.0 * u : 36.0 - 48.0 * u);
            };
            BeginTextureMode(win_);
            drawWorld(streetCamera(pan(p.time)), 0);
            EndTextureMode();
            BeginTextureMode(prev_);
            drawWorld(streetCamera(pan(p.time - 0.15)), 0);
            EndTextureMode();
            break;
        }
        case Interp:
        {
            const int h = std::clamp(int(std::lround(p.boxScreenRows * 0.8f)), 32, 720), w = std::max(8, int(std::lround(h * boxAspect)));
            ensureRt(win_, w, h, TEXTURE_FILTER_BILINEAR);
            applyFilter(0);
            // vai e volta de lado, o dobro da velocidade da do VSYNC (o degrau aparece no movimento rapido); desligada, o instante
            // so muda 30 vezes por segundo
            const double t = p.interp ? p.time : std::floor(p.time * 30.0) / 30.0;
            const double u = t * 0.5 - std::floor(t * 0.5);
            BeginTextureMode(win_);
            drawWorld(streetCamera(float(u < 0.5 ? -12.0 + 48.0 * u : 36.0 - 48.0 * u)), 0);
            EndTextureMode();
            break;
        }
        default: break;
        }
    }

    // Na textura do painel (dentro do BeginTextureMode dele): a previa no retangulo `box`, com moldura
    void draw(Kind k, const Params &p, Rectangle box, unsigned char alpha)
    {
        if (k == None || win_.id == 0) return;
        const Color white{255, 255, 255, alpha};
        const Color frame{110, 96, 78, static_cast<unsigned char>(alpha * 0.8f)};
        auto blit = [&](const RenderTexture2D &rt, Rectangle src, Rectangle dst) {
            src.y = float(rt.texture.height) - src.y - src.height;   // render texture de cabeca para baixo
            src.height = -src.height;
            DrawTexturePro(rt.texture, src, dst, Vector2{0.f, 0.f}, 0.f, white);
        };
        const Rectangle all{0.f, 0.f, float(win_.texture.width), float(win_.texture.height)};
        switch (k)
        {
        case Sharpness:
        case Filter:
        case Interp:
            blit(win_, all, box);
            DrawRectangleLinesEx(Rectangle{box.x - 3.f, box.y - 3.f, box.width + 6.f, box.height + 6.f}, 3.f, frame);
            break;
        case Vsync:
        {
            if (p.vsync)
                blit(win_, all, box);
            else
            {
                // linha do rasgo: muda de lugar a cada ~1/15 s, como o quadro trocando no meio da varredura
                const double step = std::floor(p.time * 15.0);
                const float f = 0.2f + 0.6f * float(std::fmod(std::fabs(std::sin(step * 12.9898) * 43758.5453), 1.0));
                const float sy = std::round(all.height * f), dy = box.height * (sy / all.height);
                blit(win_, Rectangle{0.f, 0.f, all.width, sy}, Rectangle{box.x, box.y, box.width, dy});
                blit(prev_, Rectangle{0.f, sy, all.width, all.height - sy}, Rectangle{box.x, box.y + dy, box.width, box.height - dy});
            }
            DrawRectangleLinesEx(Rectangle{box.x - 3.f, box.y - 3.f, box.width + 6.f, box.height + 6.f}, 3.f, frame);
            break;
        }
        case Aspect:
        case Display:
        {
            Rectangle mon, pic;
            layoutScreen(k, p, box, mon, pic);
            const float bez = std::max(6.f, mon.width * 0.025f);
            DrawRectangleRounded(Rectangle{mon.x - bez, mon.y - bez, mon.width + 2.f * bez, mon.height + 2.f * bez}, 0.05f, 6,
                                 Color{58, 52, 46, alpha});
            DrawRectangleRec(mon, Color{0, 0, 0, alpha});
            // pe do monitor
            DrawRectangleRec(Rectangle{mon.x + mon.width * 0.45f, mon.y + mon.height + bez, mon.width * 0.1f, bez * 1.6f}, Color{58, 52, 46, alpha});
            DrawRectangleRec(Rectangle{mon.x + mon.width * 0.36f, mon.y + mon.height + bez * 2.6f, mon.width * 0.28f, bez * 0.8f},
                             Color{58, 52, 46, alpha});
            if (k == Display && p.display == 0)
            {
                // area de trabalho, barra de tarefas e a janela com a barra de titulo
                DrawRectangleGradientV(int(mon.x), int(mon.y), int(mon.width), int(mon.height), Color{36, 58, 92, alpha}, Color{20, 30, 52, alpha});
                const float tb = mon.height * 0.06f;
                DrawRectangleRec(Rectangle{mon.x, mon.y + mon.height - tb, mon.width, tb}, Color{14, 16, 22, alpha});
                const float th = std::max(6.f, pic.height * 0.075f);
                DrawRectangleRec(Rectangle{pic.x - 2.f, pic.y - th, pic.width + 4.f, th + pic.height + 2.f}, Color{200, 200, 204, alpha});
                for (int i = 0; i < 3; ++i)
                    DrawRectangleRec(Rectangle{pic.x + pic.width - th * (1.1f + 1.15f * float(i)), pic.y - th * 0.78f, th * 0.6f, th * 0.55f},
                                     Color{120, 120, 126, alpha});
            }
            blit(win_, all, pic);
            break;
        }
        default: break;
        }
    }

    void release()
    {
        for (RenderTexture2D *r : {&frame_, &win_, &prev_})
            if (r->id) { UnloadRenderTexture(*r); *r = RenderTexture2D{}; }
        if (ground_.id) { UnloadTexture(ground_); ground_ = Texture2D{}; }
        ms_.release();
    }

private:
    RenderTexture2D frame_{}, win_{}, prev_{};
    wotm::msaa::Target ms_;
    Texture2D ground_{};
    int filter_ = -1;
    bool anisoSet_ = false;

    static void ensureRt(RenderTexture2D &rt, int w, int h, int filter)
    {
        if (rt.id && (rt.texture.width != w || rt.texture.height != h)) { UnloadRenderTexture(rt); rt = RenderTexture2D{}; }
        if (!rt.id)
        {
            rt = LoadRenderTexture(w, h);
            SetTextureWrap(rt.texture, TEXTURE_WRAP_CLAMP);
        }
        SetTextureFilter(rt.texture, filter);
    }

    // Ladrilhos de 64 texels com rejunte escuro, alternando duas cores: contraste alto, que serrilha e faz moire de longe sem mipmaps
    void ensureGround()
    {
        if (ground_.id) return;
        const int N = 256;
        std::vector<uint32_t> px(size_t(N) * N);
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x)
            {
                const int tx = x / 32, ty = y / 32, lx = x % 32, ly = y % 32;
                uint32_t r, g, b;
                if (lx < 2 || ly < 2) { r = 34; g = 30; b = 28; }
                else if ((tx + ty) & 1) { r = 196; g = 184; b = 160; }
                else { r = 120; g = 104; b = 92; }
                // um pouco de grao para nao ficar liso
                const uint32_t n = (uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u) % 13;
                r = std::min(255u, r + n); g = std::min(255u, g + n); b = std::min(255u, b + n);
                px[size_t(y) * N + x] = r | (g << 8) | (b << 16) | 0xFF000000u;
            }
        Image img{px.data(), N, N, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        ground_ = LoadTextureFromImage(img);
        GenTextureMipmaps(&ground_);
        SetTextureWrap(ground_, TEXTURE_WRAP_REPEAT);
        filter_ = -1;
    }

    // O mesmo do Scene::applyTexFilter: 0 bilinear sem mipmaps, 1 trilinear, 2..16 anisotropico com trilinear
    void applyFilter(int f)
    {
        if (!ground_.id || f == filter_) return;
        filter_ = f;
        if (f <= 0)
        {
            rlTextureParameters(ground_.id, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_LINEAR);
            rlTextureParameters(ground_.id, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_LINEAR);
            if (anisoSet_) rlTextureParameters(ground_.id, RL_TEXTURE_FILTER_ANISOTROPIC, 1);
            return;
        }
        rlTextureParameters(ground_.id, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_MIP_LINEAR);
        rlTextureParameters(ground_.id, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_LINEAR);
        if (f >= 2 || anisoSet_)
        {
            rlTextureParameters(ground_.id, RL_TEXTURE_FILTER_ANISOTROPIC, f >= 2 ? f : 1);
            anisoSet_ = true;
        }
    }

    // Monitor (proporcao da janela, ou do monitor no DISPLAY MODE) dentro da caixa, e o quadro dentro dele
    static void layoutScreen(Kind k, const Params &p, Rectangle box, Rectangle &mon, Rectangle &pic)
    {
        // o monitor: em janela, a proporcao fixa tambem muda a forma da janela; o que ela muda de verdade aparece na tela cheia
        const float ma = p.monAspect;
        const float mw = std::min(box.width * 0.86f, box.height * 0.74f * ma), mh = mw / ma;
        mon = Rectangle{box.x + (box.width - mw) * 0.5f, box.y + box.height * 0.06f, mw, mh};
        if (k == Display)
        {
            if (p.display == 0)
            {
                const float ww = mw * 0.62f, wh = ww / p.winAspect;
                pic = Rectangle{mon.x + (mw - ww) * 0.5f, mon.y + (mh * 0.94f - wh) * 0.5f + mh * 0.02f, ww, wh};
            }
            else
                pic = mon;
            return;
        }
        const float a = p.fixedAspect > 0.f ? p.fixedAspect : ma;
        float pw = mw, ph = mw / a;
        if (ph > mh) { ph = mh; pw = mh * a; }
        pic = Rectangle{mon.x + (mw - pw) * 0.5f, mon.y + (mh - ph) * 0.5f, pw, ph};
    }

    // ------------------------------------------------------------------ cena
    static Camera3D camera(Vector3 pos, Vector3 target, float fovy, float roll = 0.f)
    {
        Camera3D c{};
        c.position = pos;
        c.target = target;
        c.up = Vector3{std::sin(roll), std::cos(roll), 0.f};
        c.fovy = fovy;
        c.projection = CAMERA_PERSPECTIVE;
        return c;
    }
    // quina do predio da esquerda e o fio contra o ceu, de perto (camera um pouco inclinada: as bordas ficam na diagonal)
    static Camera3D edgeCamera() { return camera(Vector3{2.f, 4.f, 2.f}, Vector3{-3.2f, 21.3f, -27.f}, 14.f, 0.22f); }
    // rente ao chao, olhando a rua ate o horizonte
    static Camera3D groundCamera() { return camera(Vector3{0.f, 1.4f, 4.f}, Vector3{0.f, -2.2f, -30.f}, 50.f); }
    // a rua inteira, de lado `dx`
    static Camera3D streetCamera(float dx) { return camera(Vector3{dx, 5.f, 10.f}, Vector3{dx, 8.f, -30.f}, 55.f); }

    // caixa com faces sombreadas (sem a de baixo) e janelas em duas faces; centro da base em (x, z)
    static void building(float x, float z, float w, float h, float d, float yaw, Color c, unsigned seed)
    {
        rlPushMatrix();
        rlTranslatef(x, 0.f, z);
        rlRotatef(yaw, 0.f, 1.f, 0.f);
        const float hw = w * 0.5f, hd = d * 0.5f;
        auto shade = [&](float k) { return Color{static_cast<unsigned char>(c.r * k), static_cast<unsigned char>(c.g * k), static_cast<unsigned char>(c.b * k), 255}; };
        auto quad = [](Color k, Vector3 a, Vector3 b, Vector3 cc, Vector3 dd) {
            rlColor4ub(k.r, k.g, k.b, k.a);
            rlVertex3f(a.x, a.y, a.z); rlVertex3f(b.x, b.y, b.z); rlVertex3f(cc.x, cc.y, cc.z); rlVertex3f(dd.x, dd.y, dd.z);
        };
        rlBegin(RL_QUADS);
        quad(shade(1.f), {-hw, h, -hd}, {-hw, h, hd}, {hw, h, hd}, {hw, h, -hd});                // topo
        quad(shade(0.75f), {-hw, 0, hd}, {hw, 0, hd}, {hw, h, hd}, {-hw, h, hd});                // frente (+z)
        quad(shade(0.55f), {hw, 0, hd}, {hw, 0, -hd}, {hw, h, -hd}, {hw, h, hd});                // direita (+x)
        quad(shade(0.45f), {-hw, 0, -hd}, {-hw, 0, hd}, {-hw, h, hd}, {-hw, h, -hd});            // esquerda
        quad(shade(0.4f), {hw, 0, -hd}, {-hw, 0, -hd}, {-hw, h, -hd}, {hw, h, -hd});             // fundo
        // janelas: grade nas faces da frente e da direita; algumas acesas
        unsigned s = seed;
        auto rnd = [&]() { s = s * 1664525u + 1013904223u; return (s >> 16) & 0xFF; };
        const float wy = 1.6f, wx = 1.6f, ww = 0.8f, wh = 1.0f, eps = 0.03f;
        for (float y = 2.f; y + wh < h - 1.f; y += wy)
        {
            for (float u = -hw + 1.f; u + ww < hw - 0.6f; u += wx)
            {
                const Color k = rnd() < 90 ? Color{255, 214, 140, 255} : Color{30, 34, 44, 255};
                quad(k, {u, y, hd + eps}, {u + ww, y, hd + eps}, {u + ww, y + wh, hd + eps}, {u, y + wh, hd + eps});
            }
            for (float u = -hd + 1.f; u + ww < hd - 0.6f; u += wx)
            {
                const Color k = rnd() < 70 ? Color{255, 200, 120, 255} : Color{24, 28, 36, 255};
                quad(k, {hw + eps, y, hd - u}, {hw + eps, y, hd - u - ww}, {hw + eps, y + wh, hd - u - ww}, {hw + eps, y + wh, hd - u});
            }
        }
        rlEnd();
        rlPopMatrix();
    }

    void drawWorld(const Camera3D &cam, int)
    {
        ClearBackground(Color{230, 140, 90, 255});
        // ceu de fim de tarde (como o do menu)
        DrawRectangleGradientV(0, 0, GetRenderWidthOfTarget(), GetRenderHeightOfTarget(), Color{38, 48, 86, 255}, Color{236, 146, 92, 255});
        BeginMode3D(cam);
        rlDisableBackfaceCulling();
        // chao: um quadrado grande com os ladrilhos repetidos (1 ladrilho = 1 unidade)
        rlSetTexture(ground_.id);
        rlBegin(RL_QUADS);
        rlColor4ub(255, 255, 255, 255);
        rlNormal3f(0.f, 1.f, 0.f);
        const float S = 400.f, T = S / 8.f;   // a textura tem 8 x 8 ladrilhos
        rlTexCoord2f(-T, -T); rlVertex3f(-S, 0.f, -S);
        rlTexCoord2f(-T, T); rlVertex3f(-S, 0.f, S);
        rlTexCoord2f(T, T); rlVertex3f(S, 0.f, S);
        rlTexCoord2f(T, -T); rlVertex3f(S, 0.f, -S);
        rlEnd();
        rlSetTexture(0);
        building(-10.f, -32.f, 10.f, 24.f, 10.f, 30.f, Color{138, 128, 150, 255}, 7u);
        building(15.f, -46.f, 9.f, 17.f, 9.f, -22.f, Color{156, 140, 122, 255}, 11u);
        building(-2.f, -70.f, 12.f, 30.f, 10.f, 8.f, Color{112, 118, 142, 255}, 23u);
        // poste e o fio ate o predio
        const Color dark{18, 16, 18, 255};
        DrawCylinderEx(Vector3{4.f, 0.f, -14.f}, Vector3{4.f, 11.f, -14.f}, 0.16f, 0.12f, 8, dark);
        DrawCylinderEx(Vector3{4.f, 10.6f, -14.f}, Vector3{-6.4f, 21.f, -28.f}, 0.04f, 0.04f, 6, dark);
        DrawCylinderEx(Vector3{4.f, 10.2f, -14.f}, Vector3{-5.f, 23.4f, -30.f}, 0.04f, 0.04f, 6, dark);
        rlEnableBackfaceCulling();
        EndMode3D();
    }

    // tamanho do alvo em uso (o raylib so expoe o da tela; o BeginTextureMode ajusta o viewport, e o 2D usa estas medidas)
    static int GetRenderWidthOfTarget() { return rlGetFramebufferWidth(); }
    static int GetRenderHeightOfTarget() { return rlGetFramebufferHeight(); }
};
} // namespace wotm::preview
