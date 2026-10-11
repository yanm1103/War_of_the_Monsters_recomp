// Visualizador standalone da camada nativa: carrega um dump da RAM do EE (`ram_<quadro>.bin`, gerado pelo runtime com
// PS2X_DUMP_RAM) e desenha a cena com raylib, com camera livre. Serve para iterar sem o emulador.
//
//   viewer ram.bin [--vram vram.bin] [--shot saida.png] [--cam x y z yaw pitch] [--lod n] [--wire]
// Recursos: por padrao le LVL/MON de `disc/` (--disc <dir>) conforme fileStatus; --rtx <arquivo> forca um RTX de nivel.
// Sem --vram, procura vram_<n>.bin ao lado de ram_<n>.bin.
// Teclas: botao direito + mouse = olhar; WASD = mover (Shift = rapido); Q/E = descer/subir; Tab = arame; L = LOD; F12 = captura.
#include <cstdlib>
#include <unordered_map>
#include <string>

#include "wotm_native.hpp"

static std::vector<uint8_t> readFile(const char *path) {
    std::vector<uint8_t> v;
    if (FILE *f = std::fopen(path, "rb")) {
        std::fseek(f, 0, SEEK_END);
        long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        v.resize(size_t(n));
        if (std::fread(v.data(), 1, v.size(), f) != v.size()) v.clear();
        std::fclose(f);
    }
    return v;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "uso: viewer ram.bin [--shot saida.png] [--cam x y z yaw pitch] [--lod n] [--wire]\n");
        return 1;
    }
    std::vector<uint8_t> data = readFile(argv[1]);
    if (data.size() < 0x1000000) {
        std::fprintf(stderr, "dump invalido: %s (%zu bytes)\n", argv[1], data.size());
        return 1;
    }
    const char *shot = nullptr;
    std::string vramPath;
    const char *texdump = nullptr;
    std::string rtxPath, discRoot = "disc";
    bool wire = false, haveCam = false;
    int lod = -1;   // -1 = automatico por distancia
    bool useFree = false;
    wotm::Camera cam;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--shot" && i + 1 < argc) shot = argv[++i];
        else if (a == "--wire") wire = true;
        else if (a == "--free") useFree = true;
        else if (a == "--rtx" && i + 1 < argc) rtxPath = argv[++i];
        else if (a == "--disc" && i + 1 < argc) discRoot = argv[++i];
        else if (a == "--texdump" && i + 1 < argc) texdump = argv[++i];
        else if (a == "--vram" && i + 1 < argc) vramPath = argv[++i];
        else if (a == "--lod" && i + 1 < argc) lod = std::atoi(argv[++i]);
        else if (a == "--cam" && i + 5 < argc) {
            for (int k = 0; k < 3; ++k) cam.pos[k] = float(std::atof(argv[++i]));
            cam.yaw = float(std::atof(argv[++i])) * 3.14159265f / 180.f;
            cam.pitch = float(std::atof(argv[++i])) * 3.14159265f / 180.f;
            haveCam = true;
        }
    }
    if (vramPath.empty()) {   // ram_123.bin -> vram_123.bin
        std::string r = argv[1];
        const size_t k = r.rfind("ram_");
        if (k != std::string::npos) vramPath = r.substr(0, k) + "v" + r.substr(k);
    }
    std::vector<uint8_t> vram = vramPath.empty() ? std::vector<uint8_t>() : readFile(vramPath.c_str());
    std::fprintf(stderr, "vram: %s (%zu bytes)\n", vramPath.c_str(), vram.size());
    wotm::Ram ram{data.data(), data.size()};
    wotm::Scene scene;
    scene.vram = {vram.data(), vram.size()};
    if (rtxPath.empty()) {   // padrao: nivel + monstros do jogo, achados por fileStatus (nomes e bases das paletas) no disco extraido
        std::string log;
        const bool ok = scene.loadResources(ram, discRoot, &log);
        std::fprintf(stderr, "recursos (%s): %s -> %s (%zu paletas)\n", discRoot.c_str(), log.c_str(), ok ? "ok" : "falhou", scene.rtxPal.size());
    } else {
        const bool ok = scene.loadRtx(rtxPath.c_str(), ram.u32(wotm::addr::tempVramTexAddr) >> 6);
        std::fprintf(stderr, "rtx %s: %s (%zu paletas)\n", rtxPath.c_str(), ok ? "ok" : "falhou", scene.rtxPal.size());
    }

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1024, 768, "WotM nativo");
    SetTargetFPS(60);

    scene.collect(ram, lod);
    std::fprintf(stderr, "itens=%zu visitados=%u\n", scene.items.size(), scene.visited);
    if (std::getenv("WOTM_UVSTAT")) scene.dumpUv(ram);
    if (std::getenv("WOTM_UVISO")) scene.dumpUvIso(ram);
    scene.dumpSkinned(ram);
    if (!haveCam) {   // enquadra a cena (ignora o domo do ceu)
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (const auto &it : scene.items) {
            const float r = ram.f32(it.node + 8);
            if (!(r < 2000.f * 2000.f)) continue;
            for (int c = 0; c < 3; ++c) {
                lo[c] = std::min(lo[c], it.m.m[3][c]);
                hi[c] = std::max(hi[c], it.m.m[3][c]);
            }
        }
        if (lo[0] < hi[0]) {
            cam.pos[0] = (lo[0] + hi[0]) * 0.5f;
            cam.pos[1] = lo[1] - (hi[1] - lo[1]) * 0.6f;
            cam.pos[2] = hi[2] + (hi[1] - lo[1]) * 0.5f;
            cam.yaw = 3.14159265f * 0.5f;
            cam.pitch = -0.6f;
        }
    }

    if (texdump) {   // depuracao: grava as texturas decodificadas (TEX0 distintos) em <texdump>_<n>.png
        std::unordered_map<uint64_t, int> done;
        for (const auto &it : scene.items) {
            const uint64_t key = wotm::Scene::texKey(ram, it.node);
            if (!key || done.count(key) || done.size() >= 400) continue;
            std::vector<uint32_t> px;
            uint32_t w = 0, h = 0;
            const bool ok = scene.decodeTex(ram, it.node, px, w, h);
            const auto t = wotm::gs::Tex0::decode(wotm::Scene::objectTex0(ram, it.node));
            std::fprintf(stderr, "texId=%u psm=%02x %ux%u tw=%u th=%u tbp0=%u tbw=%u cbp=%u cpsm=%u csa=%u -> %s\n", unsigned(key >> 40) & 0xFFFF, t.psm, w, h, 1u << t.tw, 1u << t.th, t.tbp0, t.tbw, t.cbp, t.cpsm, t.csa, ok ? "ok" : "falhou");
            if (ok) {
                Image img{px.data(), int(w), int(h), 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
                ExportImage(img, TextFormat("%s_%02d.png", texdump, int(done.size())));
            }
            done[key] = 1;
        }
    }
    const wotm::GameCamera gcam = wotm::readGameCamera(ram, 0);
    bool useGame = gcam.ok && !useFree && !haveCam;
    std::fprintf(stderr, "camera do jogo: %s pos %.1f %.1f %.1f fovy %.1f\n", gcam.ok ? "ok" : "ausente", gcam.pos[0], gcam.pos[1], gcam.pos[2], gcam.fovy);
    int frame = 0;
    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            const Vector2 d = GetMouseDelta();
            cam.yaw -= d.x * 0.003f;
            cam.pitch = std::max(-1.5f, std::min(1.5f, cam.pitch - d.y * 0.003f));
        }
        const float fx = std::cos(cam.pitch) * std::cos(cam.yaw), fy = std::cos(cam.pitch) * std::sin(cam.yaw), fz = std::sin(cam.pitch);
        const float rx = std::sin(cam.yaw), ry = -std::cos(cam.yaw);
        const float sp = (IsKeyDown(KEY_LEFT_SHIFT) ? 600.f : 80.f) * dt;
        if (IsKeyDown(KEY_W)) { cam.pos[0] += fx * sp; cam.pos[1] += fy * sp; cam.pos[2] += fz * sp; }
        if (IsKeyDown(KEY_S)) { cam.pos[0] -= fx * sp; cam.pos[1] -= fy * sp; cam.pos[2] -= fz * sp; }
        if (IsKeyDown(KEY_D)) { cam.pos[0] += rx * sp; cam.pos[1] += ry * sp; }
        if (IsKeyDown(KEY_A)) { cam.pos[0] -= rx * sp; cam.pos[1] -= ry * sp; }
        if (IsKeyDown(KEY_E)) cam.pos[2] += sp;
        if (IsKeyDown(KEY_Q)) cam.pos[2] -= sp;
        if (IsKeyPressed(KEY_TAB)) wire = !wire;
        if (IsKeyPressed(KEY_L)) { lod = lod >= 3 ? -1 : lod + 1; scene.collect(ram, lod); }

        if (IsKeyPressed(KEY_C)) useGame = !useGame;
        Camera3D c3{};
        if (useGame && gcam.ok) {
            c3.position = {gcam.pos[0], gcam.pos[1], gcam.pos[2]};
            c3.target = {gcam.pos[0] + gcam.fwd[0], gcam.pos[1] + gcam.fwd[1], gcam.pos[2] + gcam.fwd[2]};
            c3.up = {gcam.up[0], gcam.up[1], gcam.up[2]};
            c3.fovy = gcam.fovy;
        } else {
            c3.position = {cam.pos[0], cam.pos[1], cam.pos[2]};
            c3.target = {cam.pos[0] + fx, cam.pos[1] + fy, cam.pos[2] + fz};
            c3.up = {0, 0, 1};
            c3.fovy = 60.f;
        }
        c3.projection = CAMERA_PERSPECTIVE;

        BeginDrawing();
        ClearBackground(Color{40, 60, 90, 255});
        rlSetClipPlanes(0.5, 30000.0);
        BeginMode3D(c3);
        scene.draw(ram, wire);
        EndMode3D();
        DrawText(TextFormat("itens %zu  lod %d  %s  cam %.0f %.0f %.0f", scene.items.size(), lod, wire ? "arame" : "solido", cam.pos[0], cam.pos[1], cam.pos[2]), 10, 10, 18, WHITE);
        EndDrawing();
        ++frame;
        if (IsKeyPressed(KEY_F12)) TakeScreenshot("viewer_shot.png");
        if (shot && frame == 3) {
            TakeScreenshot(shot);
            break;
        }
    }
    CloseWindow();
    return 0;
}
