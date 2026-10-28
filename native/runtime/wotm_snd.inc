// wotm_snd.inc: som nativo (incluido por wotm_live.inc, no escopo global de ps2_runtime.cpp). Sempre ligado.
// Troca as funcoes snd_* da lib989snd do EE (que mandam comandos por RPC ao 989snd no IOP) pelo motor de native/wotm_snd.hpp
// (repo WoTM): bancos lidos do disco, sons e streams mixados no host. Com isso os IRX de som (LIBSD, 989SND, 989ERR) nao
// sao carregados no IOP emulado. Formatos e API em docs/FORMATOS.md ("Som").
// PS2X_SND_LOG=1: uma linha por chamada (menos as por quadro: volume/pan, "ainda tocando").
#if __has_include("wotm_snd.hpp")
#include "raylib.h"   // antes do motor: liga Engine::startOutput (AudioStream)
#include "wotm_syms.h"   // wotm::sym / wotm::fn: enderecos do ELF por nome (tools/native_syms.py --gen)
#include "wotm_snd.hpp"
#include "wotm_mods.hpp"   // LSN falsos dos arquivos de mods/
#include "wotm_disc.hpp"   // banco do Shell do painel na pausa (panelShellBank)
#include <cctype>
#include <cstring>
#include "Kernel/Stubs/CD.h"
#include <fstream>
#define WOTM_HAVE_SND 1

namespace wotm_snd_hooks
{
using wotm::snd::Engine;

static bool g_log = false;
static PS2Runtime::RecompiledFunction g_origLoadModule = nullptr;

inline uint32_t arg(R5900Context *ctx, int i) { return getRegU32(ctx, 4 + i); }   // a0..a3, t0..t3 (EABI: 8 em registradores)
inline int32_t sarg(R5900Context *ctx, int i) { return static_cast<int32_t>(getRegU32(ctx, 4 + i)); }
inline uint32_t stackArg(uint8_t *rdram, R5900Context *ctx, uint32_t off)
{
    uint32_t v = 0;
    std::memcpy(&v, rdram + ((getRegU32(ctx, 29) + off) & PS2_RAM_MASK), 4);
    return v;
}

// Callback do jogo (quase sempre soundPlayCB: *user = valor). O 989snd chama no snd_FlushSoundCommands seguinte; aqui e na hora.
inline void callGuest(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt, uint32_t fn, uint32_t a0, uint32_t a1)
{
    if (!fn)
        return;
    PS2Runtime::RecompiledFunction f = rt->lookupFunction(fn);
    if (!f)
        return;
    const R5900Context saved = *ctx;
    ctx->r[4] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(a0)));
    ctx->r[5] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(a1)));
    ctx->r[31] = _mm_set_epi64x(0, 0);
    ctx->pc = fn;
    f(rdram, ctx, rt);
    wotmFinishGuestCall(rdram, ctx, rt);
    *ctx = saved;
}

// LSN -> bytes: arquivos soltos (cdRoot, LBNs do runtime) ou a ISO de PS2X_CD_IMAGE
inline bool readLsnRaw(uint32_t lsn, uint32_t sectors, uint8_t *dst, const char *&from)
{
    from = "discroot";
    const size_t bytes = size_t(sectors) * 2048;
    if (wotm::mods::Registry::isModLsn(lsn))   // banco ou musica de mods/ (wotm_mods.inc devolveu o LSN falso no fileCdSearchFile)
    {
        from = "mods";
        return wotm::mods::Registry::get().read(lsn, sectors, dst, bytes);
    }
    std::memset(dst, 0, bytes);
    const ps2_stubs::CdDebugSnapshot snap = ps2_stubs::getCdDebugSnapshot();
    std::filesystem::path path;
    uint64_t offset = 0;
    for (const auto &f : snap.files)
        if (lsn >= f.baseLbn && lsn < f.baseLbn + f.sectors)
        {
            path = f.hostPath;
            offset = uint64_t(lsn - f.baseLbn) * 2048;
            break;
        }
    if (path.empty())
    {
        if (snap.cdImage.empty())
            return false;
        path = snap.cdImage;
        offset = uint64_t(lsn) * 2048;
        from = "ISO";
    }
    std::ifstream in(path, std::ios::binary);
    if (!in.seekg(static_cast<std::streamoff>(offset)))
        return false;
    in.read(reinterpret_cast<char *>(dst), static_cast<std::streamsize>(bytes));
    return in.gcount() > 0;
}
inline bool readLsn(uint32_t lsn, uint32_t sectors, uint8_t *dst)
{
    const char *from = "?";
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = readLsnRaw(lsn, sectors, dst, from);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    wotmBoot("sound: LSN %u, %u sectors (%s) in %.1f ms%s", lsn, sectors, from, ms, ok ? "" : " FAILED");
    return ok;
}

#define SND_LOG(...) do { if (g_log) std::fprintf(stderr, "[snd] " __VA_ARGS__); } while (0)

// --- sistema e bancos
void startSoundSystem(uint8_t *, R5900Context *ctx, PS2Runtime *)
{
    const bool ok = Engine::get().startOutput();
    std::fprintf(stderr, "[snd] snd_StartSoundSystem: som nativo %s\n", ok ? "ligado (48 kHz)" : "SEM saida de audio (InitAudioDevice falhou?)");
    setReturnS32(ctx, 1);
}
void stopSoundSystem(uint8_t *, R5900Context *, PS2Runtime *) { Engine::get().stopAll(); }
void ret0(uint8_t *, R5900Context *ctx, PS2Runtime *) { setReturnS32(ctx, 0); }   // FlushSoundCommands, ResolveBankXREFS...
void getTick(uint8_t *, R5900Context *ctx, PS2Runtime *) { setReturnU32(ctx, Engine::get().tickCount()); }
// Banco do grito do narrador (playMonsterCry, abaixo): fora dos do jogo, sai quando o jogo carrega um banco dele (o jogo confere a
// SRAM livre, que conta todos os bancos do motor, antes de carregar os da luta)
static uint32_t g_cryBank = 0;
static std::string g_cryFile;
static void dropCryBank()
{
    if (!g_cryBank) return;
    Engine::get().unloadBank(g_cryBank);
    g_cryBank = 0;
    g_cryFile.clear();
}
void bankLoadByLoc(uint8_t *, R5900Context *ctx, PS2Runtime *)
{
    dropCryBank();
    const uint32_t h = Engine::get().loadBank(arg(ctx, 0), arg(ctx, 1));
    setReturnU32(ctx, h);
}
void bankLoadByLocCB(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
{
    dropCryBank();
    const uint32_t h = Engine::get().loadBank(arg(ctx, 0), arg(ctx, 1));
    callGuest(rdram, ctx, rt, arg(ctx, 2), h, arg(ctx, 3));
}
void unloadBank(uint8_t *, R5900Context *ctx, PS2Runtime *) { SND_LOG("UnloadBank %#x\n", arg(ctx, 0)); Engine::get().unloadBank(arg(ctx, 0)); }
void unloadBankCB(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
{
    SND_LOG("UnloadBank_CB %#x\n", arg(ctx, 0));
    Engine::get().unloadBank(arg(ctx, 0));
    callGuest(rdram, ctx, rt, arg(ctx, 1), 0, arg(ctx, 2));
}
void sramFree(uint8_t *, R5900Context *ctx, PS2Runtime *) { setReturnS32(ctx, Engine::get().sramFree()); }

// --- volume mestre e modos
void setMasterVolume(uint8_t *, R5900Context *ctx, PS2Runtime *)
{
    SND_LOG("SetMasterVolume grupo %d = %#x\n", sarg(ctx, 0), sarg(ctx, 1));
    Engine::get().setMasterVolume(sarg(ctx, 0), sarg(ctx, 1));
}
void getMasterVolume(uint8_t *, R5900Context *ctx, PS2Runtime *) { setReturnS32(ctx, Engine::get().masterVolume(sarg(ctx, 0))); }
void setPlaybackMode(uint8_t *, R5900Context *ctx, PS2Runtime *) { Engine::get().setMono(sarg(ctx, 0) == 1); }

// --- efeitos: snd_PlaySoundVolPanPMPB(banco, som, vol, pan, pitchMod, pitchBend) [, cb, user]
inline uint32_t playFromArgs(R5900Context *ctx)
{
    const uint32_t h = Engine::get().play(arg(ctx, 0), sarg(ctx, 1), sarg(ctx, 2), sarg(ctx, 3), sarg(ctx, 4), sarg(ctx, 5));
    SND_LOG("Play banco %#x som %d vol %#x pan %d pm %d pb %d -> %u\n", arg(ctx, 0), sarg(ctx, 1), sarg(ctx, 2), sarg(ctx, 3),
            sarg(ctx, 4), sarg(ctx, 5), h);
    return h;
}
void playSound(uint8_t *, R5900Context *ctx, PS2Runtime *) { setReturnU32(ctx, playFromArgs(ctx)); }
void playSoundCB(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
{
    const uint32_t cb = arg(ctx, 6), user = arg(ctx, 7);
    const uint32_t h = playFromArgs(ctx);
    callGuest(rdram, ctx, rt, cb, h, user);
}
void stopSound(uint8_t *, R5900Context *ctx, PS2Runtime *) { SND_LOG("Stop %u\n", arg(ctx, 0)); Engine::get().stop(arg(ctx, 0)); }
void pauseSound(uint8_t *, R5900Context *ctx, PS2Runtime *) { Engine::get().pause(arg(ctx, 0), true); }
void continueSound(uint8_t *, R5900Context *ctx, PS2Runtime *) { Engine::get().pause(arg(ctx, 0), false); }
void stopAllSounds(uint8_t *, R5900Context *, PS2Runtime *) { SND_LOG("StopAllSounds\n"); Engine::get().stopAll(); }
void pauseGroups(uint8_t *, R5900Context *ctx, PS2Runtime *) { SND_LOG("PauseAllSoundsInGroup %#x\n", arg(ctx, 0)); Engine::get().pauseGroups(arg(ctx, 0), true); }
void continueGroups(uint8_t *, R5900Context *ctx, PS2Runtime *) { SND_LOG("ContinueAllSoundsInGroup %#x\n", arg(ctx, 0)); Engine::get().pauseGroups(arg(ctx, 0), false); }
// "ainda tocando": devolve o proprio handle, ou 0 quando acabou
void isPlaying(uint8_t *, R5900Context *ctx, PS2Runtime *) { setReturnU32(ctx, Engine::get().playing(arg(ctx, 0)) ? arg(ctx, 0) : 0u); }
void isPlayingCB(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
{
    const uint32_t h = arg(ctx, 0);
    callGuest(rdram, ctx, rt, arg(ctx, 1), Engine::get().playing(h) ? h : 0u, arg(ctx, 2));
}
void setVolPan(uint8_t *, R5900Context *ctx, PS2Runtime *) { Engine::get().setVolPan(arg(ctx, 0), sarg(ctx, 1), sarg(ctx, 2)); }
void setPitchBend(uint8_t *, R5900Context *ctx, PS2Runtime *) { Engine::get().setPBend(arg(ctx, 0), sarg(ctx, 1)); }
void setPitchMod(uint8_t *, R5900Context *ctx, PS2Runtime *) { Engine::get().setPMod(arg(ctx, 0), sarg(ctx, 1)); }
// snd_SetSoundParams(handle, mascara, vol, pan, pitchMod, pitchBend) [, cb, user]
void setParams(uint8_t *, R5900Context *ctx, PS2Runtime *)
{
    Engine::get().setParams(arg(ctx, 0), arg(ctx, 1), sarg(ctx, 2), sarg(ctx, 3), sarg(ctx, 4), sarg(ctx, 5));
}
void setParamsCB(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
{
    setParams(rdram, ctx, rt);
    callGuest(rdram, ctx, rt, arg(ctx, 6), 0, arg(ctx, 7));
}

// --- streams .VPK/.VAG: snd_PlayVAGStreamByLoc(lsn, lsn2, off1, off2, vol, pan, grupo, fila, [pilha] flags) [, cb, user na pilha]
inline uint32_t playStreamFromArgs(uint8_t *rdram, R5900Context *ctx)
{
    const uint32_t h = Engine::get().playStream(arg(ctx, 0), sarg(ctx, 4), sarg(ctx, 5), sarg(ctx, 6));
    if (arg(ctx, 1) || arg(ctx, 2) || arg(ctx, 3) || arg(ctx, 7))
        std::fprintf(stderr, "[snd] PlayVAGStream com parametros nao tratados: lsn2 %u off %u/%u fila %u\n", arg(ctx, 1), arg(ctx, 2),
                     arg(ctx, 3), arg(ctx, 7));
    SND_LOG("PlayVAGStream lsn %u vol %#x pan %d grupo %d flags %#x -> %u\n", arg(ctx, 0), sarg(ctx, 4), sarg(ctx, 5), sarg(ctx, 6),
            stackArg(rdram, ctx, 0), h);
    return h;
}
void playStream(uint8_t *rdram, R5900Context *ctx, PS2Runtime *) { setReturnU32(ctx, playStreamFromArgs(rdram, ctx)); }
void playStreamCB(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
{
    const uint32_t cb = stackArg(rdram, ctx, 8), user = stackArg(rdram, ctx, 0x10);
    const uint32_t h = playStreamFromArgs(rdram, ctx);
    callGuest(rdram, ctx, rt, cb, h, user);
}
void stopAllStreams(uint8_t *, R5900Context *, PS2Runtime *) { SND_LOG("StopAllStreams\n"); Engine::get().stopAllStreams(); }
// Inicializacao do streaming: nao liga gStreamingInited da lib, entao snd_StreamSafeCd* continuam indo direto ao sceCdRead
void initVagStreaming(uint8_t *, R5900Context *ctx, PS2Runtime *) { setReturnS32(ctx, 1); }
void isStreamBufferedCB(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt) { callGuest(rdram, ctx, rt, arg(ctx, 1), 1, arg(ctx, 2)); }
void ret1(uint8_t *, R5900Context *ctx, PS2Runtime *) { setReturnS32(ctx, 1); }

// Funil de todos os comandos ao IOP: so chega aqui o que nenhum hook acima cobriu
void unhandledCommand(uint8_t *, R5900Context *ctx, PS2Runtime *)
{
    static uint64_t seen[2] = {};
    const uint32_t cmd = arg(ctx, 0);
    if (cmd < 128 && !(seen[cmd >> 6] & (1ull << (cmd & 63))))
    {
        seen[cmd >> 6] |= 1ull << (cmd & 63);
        std::fprintf(stderr, "[snd] comando 0x%02x do 989snd sem hook (ignorado)\n", cmd);
    }
    setReturnS32(ctx, 0);
}

// sceSifLoadModule: nenhum IRX sobe para o IOP emulado. Os de som sobram com o som nativo; os outros (SIO2MAN, PADMAN, MCMAN,
// MCSERV) tambem, porque o lado do EE ja e atendido pelos stubs do runtime (scePad* le o teclado e o roteiro de pad, sceMc* grava
// em arquivos do host, sceCd* le a ISO). Conferido em 2026-10-18: menu pelo roteiro de pad ate a luta e luta IA x IA, a 60 q/s
// (docs/passos/FRONTEIRA.md). WOTM_IRX=1 volta a carregar os IRX que nao sao de som no IOP emulado, so no executavel de teste: o do
// jogo (wotm, PS2X_HW_EMU=0) nao tem IOP emulado desde o passo 6 e ignora a variavel.
void loadModule(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
{
    const char *path = reinterpret_cast<const char *>(rdram + (arg(ctx, 0) & PS2_RAM_MASK));
    std::string p(path, strnlen(path, 128));
    for (char &c : p) c = char(std::toupper(static_cast<unsigned char>(c)));
#if PS2X_HW_EMU
    static const bool irx = std::getenv("WOTM_IRX") != nullptr;
#else
    static const bool irx = [] {
        if (std::getenv("WOTM_IRX"))
            std::fprintf(stderr, "[iop] WOTM_IRX ignorada: o wotm.exe nao tem IOP emulado (use o ps2EntryRunner)\n");
        return false;
    }();
#endif
    const bool sound = p.find("989SND") != std::string::npos || p.find("LIBSD") != std::string::npos || p.find("989ERR") != std::string::npos;
    if (!irx || sound)
    {
        std::fprintf(stderr, "[iop] %s nao carregado (%s)\n", path, sound ? "som nativo" : "stubs do runtime");
        setReturnS32(ctx, 0x100);   // id de modulo positivo = sucesso
        return;
    }
    g_origLoadModule(rdram, ctx, rt);
}
#undef SND_LOG
// Banco de efeitos do Shell (SND/SHELL_S.BNK) so no motor nativo, fora dos gSoundBanks do jogo: o painel do port na pausa da luta
// (wotm_mainmenu.inc) toca os sons do menu, e na luta o jogo nao tem esse banco carregado. Lido do disco (ou de mods/) na primeira
// vez e mantido; o jogo nao o ve nem o descarrega. Os sons dele vao para o grupo 2, o dos sons da pausa (playPauseMenuSound, banco
// de ambiente): na pausa o jogo baixa o volume mestre dos grupos 0 e 1 a zero (SoundManager::updatePauseAllSounds), e o Shell e todo
// do grupo 0.
constexpr int kPanelGroup = 2;
inline uint32_t panelShellBank()
{
    static uint32_t s_h = 0;
    static bool s_tried = false;
    if (!s_tried)
    {
        s_tried = true;
        const PS2Runtime::IoPaths &io = PS2Runtime::getIoPaths();
        std::vector<uint8_t> f;
        if (wotm::disc::readFile(io.cdRoot, io.cdImage.string(), "SND/SHELL_S.BNK", f))
            s_h = Engine::get().loadBankData(f, kPanelGroup);
        std::fprintf(stderr, "[snd] banco do Shell para o painel na pausa: %s\n", s_h ? "carregado (SND/SHELL_S.BNK)" : "falhou");
    }
    return s_h;
}
inline void playPanelSfx(int sound)
{
    if (const uint32_t h = panelShellBank())
    {
        const uint32_t v = Engine::get().play(h, sound, 0x400, 0, 0, 0);   // como o ShellSound::playShellSFXSound
        if (g_log)
            std::fprintf(stderr, "[snd] painel: som %d do Shell -> %u (grupo %d, volume mestre %#x)\n", sound, v, kPanelGroup,
                         Engine::get().masterVolume(kPanelGroup));
    }
}
// Grito do monstro `monster` (indice do jogo, tipo >> 5) para o narrador (opcao MONSTER CRY, wotm_narrator.inc): o som do
// MonsterSound::playSignatureSound (o do cinema de entrada da luta), do banco do proprio monstro (o do SoundManager::loadMonsterBank;
// o Final_s.bnk e o banco da fase final, carregado pelo loadRTSoundBanks). Trajes com outro som no jogo (Preytor 1, Agamo 3) usam o
// do traje 0. Um banco por vez, no grupo 0 (efeitos: o volume mestre do menu); false = sem grito (Goliath Prime, 13, nao tem).
inline bool playMonsterCry(int monster)
{
    struct Cry { const char *bank; int sound; };
    static const Cry kCry[16] = {{nullptr, 0},       {"SND/APE_S.BNK", 0},    {"SND/ROBOT_S.BNK", 4},   {"SND/LIZARD_S.BNK", 2},
                                 {"SND/SHOGUN_S.BNK", 7}, {"SND/MANTIS_S.BNK", 5}, {nullptr, 0},           {"SND/ENERGY_S.BNK", 0},
                                 {"SND/LAVA_S.BNK", 6},  {"SND/DRAGON_S.BNK", 4}, {"SND/ROKMAN_S.BNK", 0}, {"SND/ALIEN_S.BNK", 6},
                                 {nullptr, 0},       {nullptr, 0},            {"SND/FINAL_S.BNK", 8},   {"SND/FINAL_S.BNK", 0x11}};
    if (monster < 0 || monster >= 16 || !kCry[monster].bank)
        return false;
    const Cry &c = kCry[monster];
    if (g_cryFile != c.bank)
    {
        dropCryBank();   // corta o grito anterior
        const PS2Runtime::IoPaths &io = PS2Runtime::getIoPaths();
        std::vector<uint8_t> f;
        if (wotm::disc::readFile(io.cdRoot, io.cdImage.string(), c.bank, f))
            g_cryBank = Engine::get().loadBankData(f, 0);
        if (!g_cryBank)
        {
            std::fprintf(stderr, "[narrador] grito: banco %s nao carregou\n", c.bank);
            return false;
        }
        g_cryFile = c.bank;
    }
    const uint32_t v = Engine::get().play(g_cryBank, c.sound, 0x400, 0, 0, 0);
    std::fprintf(stderr, "[narrador] grito do monstro %d: som %d de %s%s\n", monster, c.sound, c.bank, v ? "" : " (nao tocou)");
    return v != 0;
}
} // namespace wotm_snd_hooks

void wotmSndInstall(PS2Runtime &runtime)
{
    using namespace wotm_snd_hooks;
    g_log = std::getenv("PS2X_SND_LOG") != nullptr;
    Engine::get().setReader(&readLsn);
    struct H { uint32_t addr; PS2Runtime::RecompiledFunction fn; };
    // enderecos de config/status.csv (lib989snd/snd)
    const H hooks[] = {
        {wotm::fn::snd_StartSoundSystem, hostLeaf<startSoundSystem>}, {wotm::fn::snd_FlushSoundCommands, hostLeaf<ret0> /* FlushSoundCommands */}, {wotm::fn::snd_StopSoundSystem, hostLeaf<stopSoundSystem>}, {wotm::fn::snd_GetTick, hostLeaf<getTick>},
        {wotm::fn::snd_BankLoadByLoc, hostLeaf<bankLoadByLoc>}, {wotm::fn::snd_BankLoadByLoc_CB, hostLeaf<bankLoadByLocCB>}, {wotm::fn::snd_ResolveBankXREFS, hostLeaf<ret0> /* ResolveBankXREFS */},
        {wotm::fn::snd_UnloadBank, hostLeaf<unloadBank>}, {wotm::fn::snd_UnloadBank_CB, hostLeaf<unloadBankCB>},
        {wotm::fn::snd_SetMasterVolume, hostLeaf<setMasterVolume>}, {wotm::fn::snd_GetMasterVolume, hostLeaf<getMasterVolume>}, {wotm::fn::snd_SetPlaybackMode, hostLeaf<setPlaybackMode>}, {wotm::fn::snd_SetGroupVoiceRange, hostLeaf<ret0> /* SetGroupVoiceRange */},
        {wotm::fn::snd_PlaySoundVolPanPMPB, hostLeaf<playSound>}, {wotm::fn::snd_PlaySoundVolPanPMPB_A, hostLeaf<playSound> /* _A */}, {wotm::fn::snd_PlaySoundVolPanPMPB_CB, hostLeaf<playSoundCB>},
        {wotm::fn::snd_StopSound, hostLeaf<stopSound>}, {wotm::fn::snd_PauseSound, hostLeaf<pauseSound>}, {wotm::fn::snd_ContinueSound, hostLeaf<continueSound>}, {wotm::fn::snd_StopAllSounds, hostLeaf<stopAllSounds>},
        {wotm::fn::snd_PauseAllSoundsInGroup, hostLeaf<pauseGroups>}, {wotm::fn::snd_ContinueAllSoundsInGroup, hostLeaf<continueGroups>}, {wotm::fn::snd_SoundIsStillPlaying, hostLeaf<isPlaying>}, {wotm::fn::snd_SoundIsStillPlaying_CB, hostLeaf<isPlayingCB>},
        {wotm::fn::snd_SetSoundVolPan, hostLeaf<setVolPan>}, {wotm::fn::snd_SetSoundPitchBend, hostLeaf<setPitchBend>}, {wotm::fn::snd_SetSoundPitchModifier, hostLeaf<setPitchMod>},
        {wotm::fn::snd_SetSoundParams, hostLeaf<setParams>}, {wotm::fn::snd_SetSoundParams_A, hostLeaf<setParams> /* _A */}, {wotm::fn::snd_SetSoundParams_CB, hostLeaf<setParamsCB>},
        {wotm::fn::snd_SendIOPCommandAndWait, hostLeaf<unhandledCommand> /* SendIOPCommandAndWait */}, {wotm::fn::snd_SendIOPCommandNoWait, hostLeaf<unhandledCommand> /* SendIOPCommandNoWait */},
        {wotm::fn::snd_InitVAGStreamingEx, hostLeaf<initVagStreaming>}, {wotm::fn::snd_StopVAGStream, hostLeaf<stopSound> /* StopVAGStream */}, {wotm::fn::snd_StopAllStreams, hostLeaf<stopAllStreams>},
        {wotm::fn::snd_CloseVAGStreaming, hostLeaf<stopAllStreams> /* CloseVAGStreaming */}, {wotm::fn::snd_PlayVAGStreamByLoc, hostLeaf<playStream>}, {wotm::fn::snd_PlayVAGStreamByLoc_A, hostLeaf<playStream> /* _A */},
        {wotm::fn::snd_PlayVAGStreamByLoc_CB, hostLeaf<playStreamCB>}, {wotm::fn::snd_PauseVAGStream, hostLeaf<pauseSound> /* PauseVAGStream */}, {wotm::fn::snd_ContinueVAGStream, hostLeaf<continueSound> /* ContinueVAGStream */},
        {wotm::fn::snd_IsVAGStreamBuffered, hostLeaf<ret1> /* IsVAGStreamBuffered */}, {wotm::fn::snd_IsVAGStreamBuffered_CB, hostLeaf<isStreamBufferedCB>},
        {wotm::fn::snd_SetReverbType, hostLeaf<ret0> /* SetReverbType */}, {wotm::fn::snd_SetReverbDepth, hostLeaf<ret0> /* SetReverbDepth */}, {wotm::fn::snd_SetReverbEx, hostLeaf<ret0> /* SetReverbEx */},
        {wotm::fn::snd_SRAMGetFreeMem, hostLeaf<sramFree>},
        {wotm::fn::snd_InitMovieSound, hostLeaf<ret0> /* InitMovieSound (os filmes tem audio proprio em wotm_fmv.hpp) */}, {wotm::fn::snd_ResetMovieSound, hostLeaf<ret0>}, {wotm::fn::snd_PauseMovieSound, hostLeaf<ret0>},
        {wotm::fn::snd_CloseMovieSound, hostLeaf<ret0>}, {wotm::fn::snd_StartMovieSound, hostLeaf<ret0>}, {wotm::fn::snd_GetTransStatus, hostLeaf<ret0> /* GetTransStatus */},
    };
    for (const H &h : hooks)
        runtime.registerFunction(h.addr, h.fn);
    g_origLoadModule = runtime.lookupFunction(wotm::fn::sceSifLoadModule);   // sceSifLoadModule (stub do runtime)
    if (g_origLoadModule)
        runtime.registerFunction(wotm::fn::sceSifLoadModule, hostLeaf<loadModule>);
    std::fprintf(stderr, "[snd] som nativo: %zu funcoes do 989snd trocadas\n", sizeof hooks / sizeof hooks[0]);
}
#else
void wotmSndInstall(PS2Runtime &)
{
    std::fprintf(stderr, "[snd] som nativo desligado: falta native/wotm_snd.hpp (WOTM_NATIVE_DIR)\n");
}
#endif
