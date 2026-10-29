// wotm_scene.inc (incluido por ps2_runtime.cpp; raylib e ps2_runtime.h ja estao incluidos)
#include <atomic>
#include "wotm_savepics.hpp"   // imagens da lista de saves (wotm_live.inc as desenha)
static std::atomic<bool> g_wotmShotKey{false};   // F8 na janela (wotm_foto.inc) -> um quadro da checagem VU1 x nativo agora (wotm_vu1check.inc)
#include "wotm_live.inc"
#include "wotm_foto.inc"   // F7 pausa, F6 avanca um quadro, F8 foto com nota (WOTM_FOTOS)
#include "wotm_syms.h"   // wotm::sym / wotm::fn: enderecos do ELF por nome (tools/native_syms.py --gen)
#include "wotm_saveicon.hpp"   // icone de salvamento automatico

extern bool g_wotmVu1Tap;   // ps2_memory.cpp (checagem VU1 x nativo)
static void wotmDrawScene(const uint8_t *rdram)
{
    if (!rdram) return;
    wotm_live::initWindowAspect();   // janela na proporcao do widescreen (uma vez, antes da abertura)
    wotm_live::fullscreenKey();      // F11: tela cheia <-> janela
    {
        // Sem VU1 ninguem conclui o pacote: o jogo espera `objsInPacket | objsInAlphaPacket` zerarem (rtWaitForVu1, e um laco
        // embutido em particleDraw). Cada apresentacao faz o papel da conclusao do VU1. No quadro amostrado pela checagem VU1 x
        // nativo nao: la o hierFlushObjQ dispara o DMA de verdade, e zerar aqui descartava a OT de alfa ainda nao despachada (os
        // objetos 0x8000/0x4000 saiam como "so o nativo desenha", pista 7 do VU1CHECK).
        static const bool s_noVu1 = std::getenv("PS2X_NO_VU1") != nullptr;
        if (s_noVu1 && !g_wotmVu1Tap)
        {
            const uint32_t zero = 0;
            std::memcpy(const_cast<uint8_t *>(rdram) + wotm::sym::objsInPacket, &zero, 4);
            std::memcpy(const_cast<uint8_t *>(rdram) + wotm::sym::objsInAlphaPacket, &zero, 4);
        }
    }
    wotmFotoKeys();
    const bool drew = wotmDrawNative(rdram); // camada nativa (native/wotm_native.hpp): fase lida da RAM, sem VU1/GS
    {
        // Sem cena nativa (carregamento, troca de tela) a janela ficava com o quadro do GS. Com PS2X_NO_GS o GS nao desenha nada: a
        // area exibida so tem os uploads de textura, e entre o pulo da abertura e o menu aparecia a VRAM crua (blocos de textura
        // embaralhados; tambem entre lutas). Aqui fica preto; WOTM_GSFRAME=1 volta a mostrar o quadro do GS (diagnostico).
        // Retangulo, nao ClearBackground: o quadro do GS ainda esta no lote do raylib, e o glClear imediato vinha antes dele.
        static const bool s_blackGap = std::getenv("PS2X_NO_GS") != nullptr && std::getenv("WOTM_GSFRAME") == nullptr;
        if (!drew && s_blackGap)
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), BLACK);
    }
    {
        // Icone de salvamento (NATIVE.md, "Save em JSON"): 1 s no canto a cada gravacao do save.json (wotm_save.inc)
        static const PS2Runtime::IoPaths &io = PS2Runtime::getIoPaths();
        static const std::filesystem::path card =
            (io.mcRoot.empty() ? (io.elfDirectory.empty() ? std::filesystem::current_path() : io.elfDirectory) / "mc0" : io.mcRoot) / "BASCUS-97197";
        wotm::saveicon::presenter().present(io.cdRoot, io.cdImage.string(), card);
    }
    wotmFotoOverlay(drew, rdram);
    {
        // WOTM_WINSHOTS=<s>[,<s>...]: a janela inteira (com o que vem por cima do quadro nativo) em win_<s>.png quando o relogio passa
        // de cada <s> segundos. O WOTM_SHOTS so grava quando a cena nativa e redesenhada; este vale tambem nos lacos do Shell (dialogo
        // depois da luta, cinemas) e com a tela preta de "sem cena".
        static std::vector<double> shots = [] {
            std::vector<double> v;
            if (const char *s = std::getenv("WOTM_WINSHOTS"))
                for (const char *p = s; *p;)
                {
                    char *e = nullptr;
                    const double t = std::strtod(p, &e);
                    if (e == p) break;
                    v.push_back(t);
                    p = (*e == ',') ? e + 1 : e;
                }
            return v;
        }();
        static size_t next = 0;
        if (next < shots.size() && GetTime() >= shots[next])
        {
            char name[64];
            std::snprintf(name, sizeof name, "win_%g.png", shots[next]);
            rlDrawRenderBatchActive();   // o lote do raylib ainda nao foi para o quadro
            Image img = LoadImageFromScreen();
            if (ExportImage(img, name))
                std::fprintf(stderr, "[shot] %s (janela, cena nativa %s)\n", name, drew ? "desenhada" : "ausente: preto");
            UnloadImage(img);
            ++next;
        }
    }
}
#include "wotm_vu1check.inc"
