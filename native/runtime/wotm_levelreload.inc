// wotm_levelreload.inc: recarga da fase no meio da luta (MODS.md, "Level patches"), incluido por wotm_overrides.cpp depois do
// wotm_training.inc. O patch da fase (wotm_levelpatch.inc) so entra na carga, e a carga refaz tudo o que ele mexe (registro dos
// objetos, grade de colisao, redes de carros e pedestres): recarregar e o RESTART do retail, que o main ja faz sem passar pelos menus
// (m_inSession 0 com m_inMenus 1 repete a sessao: LoadLevelFiles -> dbInitDb -> patch relido).
//  - Pedido: F10 na janela (wotm_live.inc liga g_wotmLevelReloadKey) ou WOTM_LEVELRELOAD=<arquivo>: uma thread do host olha o mtime a
//    cada 250 ms; um conteudo novo "<FASE> <contador>" pede a recarga, que so vale se a fase carregada for <FASE>. Com a pausa
//    aberta o rtMain nao roda: o pedido espera a volta a luta. Pedido feito antes de uma carga da fase (no menu, durante o LOAD)
//    cai, porque a carga ja leu o JSON (wotm_levelpatch::g_loads); o que o rtMain ve por kExpireMs sem um quadro de luta tambem.
//  - Ponto seguro: a entrada do rtTimeToReturnToShell, no fim de cada quadro do rtMain, com as condicoes do START no rtPauseRT
//    (sem pausa, sem filme, game+0x120454/0x120458 zerados) e gRtReturn livre. Ali faz o que o START faz (shell+0x2BA4 = 1,
//    rtReturnToShell(2, 0)): o rtMain sai neste quadro e o EvaluateGameStatus(2) chama o displayDialog(0).
//  - Esse displayDialog(0) nao abre a pausa: faz as escritas do item RESTART do pauseLevelOverlay e do "sim" do areYouSure (asm
//    game/ShellFinished; as mesmas do RESET POSITIONS do treino, wotm_pausemenu.inc) e volta. O EvaluateGameStatus mostra o fundo
//    de carga e zera o m_restart; o main recarrega a fase.
#include <cctype>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <thread>
#include "wotm_training.h"

std::atomic<bool> g_wotmLevelReloadKey{false};   // F10 na janela (wotm_live.inc grava, a thread do EE consome)

namespace wotm_levelreload
{
constexpr int64_t kExpireMs = 10000;
constexpr uint32_t kShell = wotm::sym::shellInfo, kGame = wotm::sym::shellGame;
constexpr uint32_t kPaused = 0x2BA4, kRestart = 0x2BA8, kInSession = 0x2BCC, kInMenus = 0x2BD0;   // Shell (src/game/Shell.cpp)
constexpr uint32_t kEvaluateGameStatus = wotm::fn::EvaluateGameStatus__5Shelli, kEvaluateGameStatusSize = 0x2A4;   // wotm_syms.h

PS2Runtime::RecompiledFunction g_origTime = nullptr, g_origDialog = nullptr;
// a thread do arquivo vive ate o fim do processo: o mutex e a string dela nunca sao destruidos
std::mutex &g_mu = *new std::mutex;
std::string &g_fileStage = *new std::string;   // fase do ultimo pedido do arquivo (vazio: nenhum)
// "Testar daqui" do editor: onde o jogador 1 nasce em vez de no ponto da fase (WOTM_SPAWN=x,y,z,heading na partida; no arquivo da
// recarga "<FASE> <contador> x y z heading"; sem os quatro numeros, os pontos da fase). Heading em graus, 0 = +Y, anti-horario (o do .SPT).
// Um quinto numero (ring, o raio) poe os outros monstros (jogadores 2 a 4 e IAs) numa roda de raio ring em volta do ponto, o primeiro
// a frente, todos de frente para o jogador 1; 0 deixa cada um no seu ponto da fase.
struct Spawn { bool on = false; float x = 0, y = 0, z = 0, heading = 0, ring = 0; };
Spawn &g_spawn = *new Spawn;   // sob g_mu: a thread do arquivo grava, a do EE le
unsigned g_fileLoads = 0;          // wotm_levelpatch::g_loads quando ele chegou
int64_t g_fileMs = 0, g_keyMs = 0;   // quando cada pedido chegou (thread do EE)
bool g_pending = false;            // rtReturnToShell(2) ja pedido: o proximo displayDialog(0) e a recarga

uint32_t rd32(const uint8_t *rdram, uint32_t a) { uint32_t v; std::memcpy(&v, rdram + (a & PS2_RAM_MASK), 4); return v; }
void wr32(uint8_t *rdram, uint32_t a, uint32_t v) { std::memcpy(rdram + (a & PS2_RAM_MASK), &v, 4); }

// Pode devolver o controle ao jogo agora, como o START no rtPauseRT. Com game+0x1204C0+0x109C ligado (volume abaixado), a saida
// do displayDialog do retail desfaz o volume (snd_SetMasterVolume de shell+0x2A30/0x2A34), e o gancho pula o displayDialog: espera.
bool fightFrame(const uint8_t *rdram)
{
    return rd32(rdram, kShell + kInSession) == 1 && rd32(rdram, wotm::sym::gRtReturn) == 0 && rd32(rdram, kShell + kPaused) == 0 &&
           rd32(rdram, wotm::sym::moviePlaying) == 0 && rd32(rdram, kGame + 0x120458) == 0 && rd32(rdram, kGame + 0x120454) == 0 &&
           rd32(rdram, kGame + 0x1204C0 + 0x109C) == 0;
}

void timeHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == wotm::fn::rtTimeToReturnToShell__Fv)
    {
        const int64_t now = wotm_training::nowMs();
        // o prazo conta so quadros do rtMain: um intervalo sem ele (pausa, menu) empurra os pedidos para frente
        static int64_t s_lastCall = 0;
        if (s_lastCall && now - s_lastCall > 1000)
        {
            if (g_keyMs) g_keyMs += now - s_lastCall;
            if (g_fileMs) g_fileMs += now - s_lastCall;
        }
        s_lastCall = now;
        if (g_pending)   // o displayDialog(0) da recarga nao veio (outro caminho saiu do rtMain): o pedido cai
        {
            std::fprintf(stderr, "[fase] recarga: o rtMain voltou sem o dialogo da pausa; pedido descartado\n");
            g_pending = false;
        }
        // um pedido feito antes da ultima carga da fase ja foi atendido por ela (o JSON e lido na carga)
        const unsigned loads = wotm_levelpatch::g_loads.load();
        static unsigned s_keyLoads = 0, s_fileLoads = 0;
        static std::string s_wantStage;
        if (g_wotmLevelReloadKey.exchange(false)) g_keyMs = now, s_keyLoads = loads;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            if (!g_fileStage.empty()) s_wantStage.swap(g_fileStage), g_fileStage.clear(), g_fileMs = now, s_fileLoads = g_fileLoads;
        }
        if (g_keyMs && s_keyLoads != loads) g_keyMs = 0;
        if (g_fileMs && s_fileLoads != loads)
        {
            std::fprintf(stderr, "[fase] recarga pedida (arquivo) antes da ultima carga da fase: ja atendida\n");
            g_fileMs = 0;
        }
        if (g_keyMs && now - g_keyMs > kExpireMs) std::fprintf(stderr, "[fase] recarga pedida (F10) sem quadro de luta em 10 s: descartada\n"), g_keyMs = 0;
        if (g_fileMs && now - g_fileMs > kExpireMs) std::fprintf(stderr, "[fase] recarga pedida (arquivo) sem quadro de luta em 10 s: descartada\n"), g_fileMs = 0;
        if (g_fileMs)
        {
            const std::string cur = wotm_levelpatch::stageName(rdram);
            if (cur != s_wantStage)
            {
                std::fprintf(stderr, "[fase] recarga pedida (arquivo) para %s, mas a fase e %s: ignorada\n", s_wantStage.c_str(), cur.c_str());
                g_fileMs = 0;
            }
        }
        if ((g_keyMs || g_fileMs) && fightFrame(rdram))
        {
            std::fprintf(stderr, "[fase] recarga pedida (%s): %s\n", g_fileMs ? "arquivo" : "F10", wotm_levelpatch::stageName(rdram).c_str());
            g_keyMs = g_fileMs = 0;
            wr32(rdram, kShell + kPaused, 1);
            wotm_modes4::call(rdram, ctx, runtime, wotm::fn::rtReturnToShell__Fii, {2u, 0u});
            g_pending = true;
        }
    }
    g_origTime(rdram, ctx, runtime);
}

void dialogHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (ctx->pc == wotm::fn::displayDialog__Fi && g_pending)
    {
        g_pending = false;
        const uint32_t ra = GPR_U32(ctx, 31);
        if (GPR_U32(ctx, 4) == 0 && ra >= kEvaluateGameStatus && ra < kEvaluateGameStatus + kEvaluateGameStatusSize)
        {
            // item 1 (RESTART) do pauseLevelOverlay e o "sim" do areYouSure
            const uint32_t cont = rd32(rdram, kShell + 0x2B48);
            wr32(rdram, kShell + 0x2B40, cont);
            wr32(rdram, kShell + 0x2B44, cont);
            wr32(rdram, kShell + 0x2B4C, 0);
            wr32(rdram, kShell + 0x2B50, 0);
            wr32(rdram, kShell + kRestart, 1);
            wr32(rdram, kShell + kInMenus, 1);
            wr32(rdram, kShell + kInSession, 0);
            wr32(rdram, kShell + kPaused, 0);
            if (!rd32(rdram, kGame + 0x1204C0 + 0x109C)) wotm_modes4::call(rdram, ctx, runtime, wotm::fn::snd_StopAllSounds, {0u});
            wr32(rdram, wotm::sym::gExitDialog, 0);
            wotm_training::g_inFight = false;
            ctx->pc = ra;
            return;
        }
        std::fprintf(stderr, "[fase] recarga: displayDialog(%u) fora do EvaluateGameStatus; a recarga nao foi feita\n", GPR_U32(ctx, 4));
    }
    g_origDialog(rdram, ctx, runtime);
}

// WOTM_LEVELRELOAD=<arquivo>: o conteudo novo pede a recarga (o que ja estava no boot nao)
void watch(std::filesystem::path file)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::file_time_type last = fs::last_write_time(file, ec);
    if (ec) last = {};
    std::string lastText;
    if (std::ifstream in(file, std::ios::binary); in)
        lastText.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    for (;;)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        const fs::file_time_type t = fs::last_write_time(file, ec);
        if (ec || t == last) continue;
        std::ifstream in(file, std::ios::binary);
        if (!in) continue;
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::string stage;
        for (char c : text)
        {
            if (std::isspace(static_cast<unsigned char>(c)))
            {
                if (!stage.empty()) break;
                continue;
            }
            stage += char(std::toupper(static_cast<unsigned char>(c)));
        }
        if (stage.empty()) continue;   // gravacao pela metade: o mtime e relido no proximo
        last = t;
        if (text == lastText) continue;
        lastText = text;
        Spawn sp;
        {
            std::istringstream is(text);
            std::string s0, s1;
            float v[5] = {0, 0, 0, 0, 0};
            is >> s0 >> s1;
            if (is >> v[0] >> v[1] >> v[2] >> v[3])
            {
                is >> v[4];   // opcional (sem ele fica 0)
                sp = Spawn{true, v[0], v[1], v[2], v[3], v[4] > 0.f ? v[4] : 0.f};
            }
        }
        std::lock_guard<std::mutex> lk(g_mu);
        g_spawn = sp;
        g_fileStage = stage;
        g_fileLoads = wotm_levelpatch::g_loads.load();
    }
}

// Fim do TheGame::gameGetStartPoint(m) (o gancho dos jogadores sob IA, wotm_overrides.cpp): o jogador 1 vai para o ponto pedido, de pe no
// chao embaixo dele (20 acima do chao achado de 1000 acima do ponto; sem chao, no ponto), de frente para o heading. Vale em todo
// nascimento dele, o renascimento tambem.
void placeAtSpawn(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t m)
{
    Spawn sp;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        sp = g_spawn;
    }
    m &= PS2_RAM_MASK;
    if (!sp.on || m < 0x1000u) return;
    int k = -1;   // indice em m_monsters: 0 a 3 os jogadores, 4 em diante as IAs
    for (int i = 0; i < 16 && k < 0; ++i)
        if ((rd32(rdram, kGame + 0x120380 + 4 * i) & PS2_RAM_MASK) == m) k = i;
    if (k < 0 || (k > 0 && sp.ring <= 0.f)) return;
    const uint32_t cs = rd32(rdram, m + 0xC) & PS2_RAM_MASK;
    if (cs < 0x1000u) return;
    if (k > 0)
    {
        // os outros numa roda em volta do jogador 1, o primeiro a frente dele, todos olhando para ele
        const int players = int(rd32(rdram, kShell + 0x2BB4)), ais = int(rd32(rdram, kShell + 0x2BB8));
        const int total = players - 1 + ais, t = k < 4 ? k - 1 : players - 1 + (k - 4);
        if (t < 0 || t >= total) return;
        const float a = (sp.heading + 360.f * float(t) / float(total)) * 0.017453292f;
        const float x = sp.x + sp.ring * std::sin(a), y = sp.y + sp.ring * std::cos(a);
        sp.heading = std::atan2(sp.x - x, sp.y - y) * 57.29578f;
        sp.x = x;
        sp.y = y;
    }
    using wotm_coop::kVec;
    wotm_coop::wrF(rdram, kVec, sp.x);
    wotm_coop::wrF(rdram, kVec + 4, sp.y);
    wotm_coop::wrF(rdram, kVec + 8, sp.z + 1000.f);
    wotm_coop::wrF(rdram, kVec + 12, 0.f);
    const float d = wotm_coop::hatTest(rdram, ctx, runtime, kVec, kVec + 0x10);
    const float z = d > 0.f && d < 2000.f ? sp.z + 1000.f - d + 20.f : sp.z;
    wotm_coop::wrF(rdram, kVec + 8, z);
    wotm_modes4::call(rdram, ctx, runtime, wotm::fn::setTrans__7MonsterR8_fvector, {m, kVec});
    wotm_coop::callF(rdram, ctx, runtime, wotm::fn::setRot__7Monsterfff, {m}, {sp.heading * 0.017453292f, 0.f, 0.f});
    wotm_modes4::call(rdram, ctx, runtime, wotm::fn::hdReparentCsGrid__FP3_cs, {cs});
    std::fprintf(stderr, "[fase] largada pedida: %s em (%.0f, %.0f, %.0f), heading %.0f%s\n",
                 k == 0 ? "jogador 1" : k < 4 ? "jogador 2 a 4" : "IA", sp.x, sp.y, z, sp.heading,
                 d > 0.f && d < 2000.f ? "" : " (sem chao embaixo do ponto)");
}
} // namespace wotm_levelreload

void wotmLevelReloadInstall(PS2Runtime &runtime)
{
    using namespace wotm_levelreload;
    if (const char *s = std::getenv("WOTM_SPAWN"); s && *s)
    {
        std::istringstream is(s);
        std::string tok;
        float v[5] = {0, 0, 0, 0, 0};
        int n = 0;
        while (n < 5 && std::getline(is, tok, ',')) v[n++] = float(std::atof(tok.c_str()));
        if (n == 4 || n == 5)
        {
            g_spawn = Spawn{true, v[0], v[1], v[2], v[3], v[4] > 0.f ? v[4] : 0.f};
            std::fprintf(stderr, "[fase] largada do jogador 1 em (%.0f, %.0f, %.0f), heading %.0f%s (WOTM_SPAWN)\n", v[0], v[1], v[2], v[3],
                         g_spawn.ring > 0.f ? ", os outros numa roda em volta" : "");
        }
        else
            std::fprintf(stderr, "[fase] WOTM_SPAWN invalido (\"%s\"; use x,y,z,heading[,raio])\n", s);
    }
    if (const char *e = std::getenv("WOTM_LEVELPATCH"); e && std::atoi(e) <= 0) return;
    if (!(g_origTime = runtime.lookupFunction(wotm::fn::rtTimeToReturnToShell__Fv)) || !(g_origDialog = runtime.lookupFunction(wotm::fn::displayDialog__Fi)))
    {
        std::fprintf(stderr, "[fase] rtTimeToReturnToShell ou displayDialog sem funcao no runtime: sem recarga da fase\n");
        return;
    }
    runtime.registerFunction(wotm::fn::rtTimeToReturnToShell__Fv, &timeHook);
    runtime.registerFunction(wotm::fn::displayDialog__Fi, &dialogHook);   // por fora do da pausa e do dialogAdventure
    if (const char *f = std::getenv("WOTM_LEVELRELOAD"); f && *f)
    {
        std::thread(watch, std::filesystem::path(f)).detach();
        std::fprintf(stderr, "[fase] recarga da fase: F10 ou o arquivo %s (\"<FASE> <contador>\")\n", f);
    }
    else
        std::fprintf(stderr, "[fase] recarga da fase: F10\n");
}
