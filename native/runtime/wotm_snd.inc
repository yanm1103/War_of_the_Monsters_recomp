// wotm_snd.inc: som nativo (incluido por wotm_live.inc, no escopo global de ps2_runtime.cpp). Sempre ligado.
// Troca as funcoes snd_* da lib989snd do EE (que mandam comandos por RPC ao 989snd no IOP) pelo motor de native/wotm_snd.hpp
// (repo WoTM): bancos lidos do disco, sons e streams mixados no host. Com isso os IRX de som (LIBSD, 989SND, 989ERR) nao
// sao carregados no IOP emulado. Formatos e API em docs/FORMATS.md ("Som").
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
#include <iterator>
#include <map>
#define WOTM_HAVE_SND 1

namespace wotm_snd_hooks
{
using wotm::snd::Engine;

static bool g_log = false;
static PS2Runtime::RecompiledFunction g_origLoadModule = nullptr;
void preloadOwnBanks();   // banco de som proprio dos customs (abaixo)

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
    const char *mute = std::getenv("WOTM_MUTE");
    if (ok && mute && *mute && *mute != '0')
        ::SetMasterVolume(0.f);   // tudo o que sai pelo raylib: jogo, narrador e filmes
    std::fprintf(stderr, "[snd] snd_StartSoundSystem: som nativo %s%s\n", ok ? "ligado (48 kHz)" : "SEM saida de audio (InitAudioDevice falhou?)",
                 ok && mute && *mute && *mute != '0' ? ", mudo (WOTM_MUTE)" : "");
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
    preloadOwnBanks();
    const uint32_t h = Engine::get().loadBank(arg(ctx, 0), arg(ctx, 1));
    setReturnU32(ctx, h);
}
void bankLoadByLocCB(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
{
    dropCryBank();
    preloadOwnBanks();
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
// (docs/guides/BOUNDARY.md). WOTM_IRX=1 volta a carregar os IRX que nao sao de som no IOP emulado, so no executavel de teste: o do
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
// Motor do tanque fora de Vegas (exercito em qualquer fase, wotm_army.inc): o TankSound::updateTankSound toca o som 0 de
// gSoundBanks[12], o banco da fase, que so em Vegas (VEGAS_S.BNK) tem o motor. Nas outras fases o gancho troca o banco 12 por uma
// copia do VEGAS_S.BNK so no motor nativo (lida na primeira vez e mantida, como a do Shell do painel) durante a chamada.
static PS2Runtime::RecompiledFunction g_origTankSound = nullptr;
inline uint32_t vegasBank()
{
    static uint32_t s_h = 0;
    static bool s_tried = false;
    if (!s_tried)
    {
        s_tried = true;
        const PS2Runtime::IoPaths &io = PS2Runtime::getIoPaths();
        std::vector<uint8_t> f;
        if (wotm::disc::readFile(io.cdRoot, io.cdImage.string(), "SND/VEGAS_S.BNK", f))
            s_h = Engine::get().loadBankData(f);
        std::fprintf(stderr, "[snd] motor do tanque fora de Vegas: %s\n", s_h ? "copia do SND/VEGAS_S.BNK carregada" : "VEGAS_S.BNK falhou (sem motor)");
    }
    return s_h;
}
void tankSound(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
{
    constexpr uint32_t kBank12 = wotm::sym::gSoundBanks + 4u * 12u, kLevelId = wotm::sym::shellGame + 0x1203D0u, kVegas = 2u;
    uint32_t level = 0, bank = 0;
    std::memcpy(&level, rdram + (kLevelId & PS2_RAM_MASK), 4);
    const uint32_t h = level != kVegas ? vegasBank() : 0u;
    if (ctx->pc != wotm::fn::updateTankSound__9TankSoundP10TankPickupi || !h)
    {
        g_origTankSound(rdram, ctx, rt);
        return;
    }
    const uint32_t ra = getRegU32(ctx, 31);
    std::memcpy(&bank, rdram + (kBank12 & PS2_RAM_MASK), 4);
    std::memcpy(rdram + (kBank12 & PS2_RAM_MASK), &h, 4);
    g_origTankSound(rdram, ctx, rt);
    wotmFinishGuestCall(rdram, ctx, rt, ra);   // a troca vale ate o fim, mesmo com a funcao pausada no meio
    std::memcpy(rdram + (kBank12 & PS2_RAM_MASK), &bank, 4);
}

// Banco de som proprio de um custom (um .BNK na pasta "files", com os indices do banco do tipo; MODS.md, "Custom monsters"). Os
// sons de um monstro saem de gSoundBanks[<tipo> - 1] (BANK_MECH no Goliath, BANK_FINAL no Jelly e na forma final), e quem os toca
// sabe de qual Monster e: o MonsterSound (this +0xC), o WeaponSound do projetil (this = Weapon +0x64, dono pelo id em +0x120) e o
// SuckerBugSound do inseto do Preytor (this = HomingBug +0x14, dono em +0xB0). Os ganchos trocam a entrada do tipo pelo banco do
// custom durante a chamada, e um Monster do jogo do mesmo tipo chamado no meio dela volta a ver o do jogo. O banco fica so no
// motor nativo, carregado uma vez e mantido; o _LS de baixa resolucao tem os mesmos indices do _S, entao o mesmo banco serve nas
// lutas de 3 e 4.
}   // namespace wotm_snd_hooks
const wotm::mods::Custom *wotmCustomOwnFiles(uint8_t *rdram, uint32_t monster);   // wotm_overrides.cpp (wotm_custom.inc)
namespace wotm_snd_hooks
{
constexpr uint32_t kMsMonster = 0xC, kWeaponSound = 0x64, kWeaponOwner = 0x120, kBugSound = 0x14, kBugOwner = 0xB0;
constexpr uint32_t kTypeBits = 0x14, kMonsterId = 0x20, kMonsters = wotm::sym::shellGame + 0x120380u;   // TheGame::m_monsters[8]
constexpr uint32_t kTakeHit = wotm::fn::playTakeHit__12MonsterSoundP7Monster;
static uint32_t g_ownBankTypes = 0;   // bit por tipo de jogo com algum custom de banco proprio
static uint32_t g_realBank[32];       // gSoundBanks[i] do jogo enquanto alguma troca esta de pe
static int g_swapDepth[32];
inline uint32_t rdU32(const uint8_t *rdram, uint32_t a) { uint32_t v; std::memcpy(&v, rdram + (a & PS2_RAM_MASK), 4); return v; }
inline void wrU32(uint8_t *rdram, uint32_t a, uint32_t v) { std::memcpy(rdram + (a & PS2_RAM_MASK), &v, 4); }
// Entrada de gSoundBanks dos sons de um tipo (loadMonsterBank; o 6 nao tem banco)
inline int monsterBankSlot(uint32_t type)
{
    if (type >= 1 && type <= 11 && type != 6)
        return int(type) - 1;
    return type == 13 ? 22 : type == 14 || type == 15 ? 24 : -1;   // BANK_MECH, BANK_FINAL
}
uint32_t ownBank(const wotm::mods::Custom &cu)
{
    static std::map<std::string, uint32_t> s_banks;   // id do custom -> handle (0 = nao carregou)
    if (auto it = s_banks.find(cu.id); it != s_banks.end())
        return it->second;
    std::vector<uint8_t> f;
    std::ifstream in(cu.bank, std::ios::binary);
    if (in)
        f.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    const uint32_t h = f.empty() ? 0u : Engine::get().loadBankData(f);
    std::fprintf(stderr, "[snd] %s: banco de som proprio %s %s %#x\n", cu.id.c_str(), cu.bank.filename().string().c_str(),
                 h ? "no handle" : "NAO carregou (fica o do tipo)", h);
    return s_banks[cu.id] = h;
}
// Carrega de antemao os bancos dos customs (no carregamento de um banco do jogo, entre telas), para o primeiro som da luta nao
// esperar o disco
void preloadOwnBanks()
{
    static bool s_done = false;
    if (s_done)
        return;
    s_done = true;
    for (const wotm::mods::Custom &cu : wotm::mods::Registry::get().customs())
        if (!cu.bank.empty() && monsterBankSlot(uint32_t(cu.type)) >= 0 && ownBank(cu))
            g_ownBankTypes |= 1u << cu.type;
}
struct BankSwap { int slot = -1; uint32_t prev = 0; };
// Poe em gSoundBanks o banco que o Monster `m` usa: o proprio (custom com .BNK) ou, dentro da troca de outro Monster do mesmo
// tipo, o do jogo de volta. false = nada a trocar.
bool swapBankFor(uint8_t *rdram, uint32_t m, BankSwap &s)
{
    if (!m || !g_ownBankTypes)
        return false;
    const uint32_t type = rdU32(rdram, m + kTypeBits) >> 5;
    const int slot = type < 32 && (g_ownBankTypes >> type & 1u) ? monsterBankSlot(type) : -1;
    if (slot < 0)
        return false;
    const wotm::mods::Custom *cu = wotmCustomOwnFiles(rdram, m);
    uint32_t want = cu && !cu->bank.empty() ? ownBank(*cu) : 0u;
    if (!want && g_swapDepth[slot])
        want = g_realBank[slot];
    if (!want)
        return false;
    const uint32_t a = wotm::sym::gSoundBanks + 4u * uint32_t(slot);
    s.slot = slot;
    s.prev = rdU32(rdram, a);
    if (!g_swapDepth[slot]++)
        g_realBank[slot] = s.prev;
    wrU32(rdram, a, want);
    return true;
}
void unswapBank(uint8_t *rdram, const BankSwap &s)
{
    if (s.slot < 0)
        return;
    wrU32(rdram, wotm::sym::gSoundBanks + 4u * uint32_t(s.slot), s.prev);
    --g_swapDepth[s.slot];
}
uint32_t monsterById(uint8_t *rdram, uint32_t id)
{
    for (uint32_t i = 0; i < 8; ++i)
        if (const uint32_t m = rdU32(rdram, kMonsters + 4 * i); m && rdU32(rdram, m + kMonsterId) == id)
            return m;
    return 0;
}
enum SoundOwner { kOwnerMonsterSound, kOwnerWeapon, kOwnerBug };
template <uint32_t A>
PS2Runtime::RecompiledFunction g_soundOrig = nullptr;
template <uint32_t A, int Owner>
void ownBankHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
{
    if (ctx->pc != A || !g_ownBankTypes)
    {
        g_soundOrig<A>(rdram, ctx, rt);
        return;
    }
    const uint32_t self = arg(ctx, 0);
    uint32_t m = 0, attacker = 0;
    if constexpr (Owner == kOwnerMonsterSound)
    {
        m = rdU32(rdram, self + kMsMonster);
        // o impacto do golpe do Preytor e do Magmo sai do banco de quem bate (o this e quem apanha)
        if (A == kTakeHit && arg(ctx, 1))
        {
            const uint32_t t = rdU32(rdram, arg(ctx, 1) + kTypeBits);
            attacker = t == 0xA0u || t == 0x100u ? arg(ctx, 1) : 0u;
        }
    }
    else if constexpr (Owner == kOwnerWeapon)
        m = monsterById(rdram, rdU32(rdram, self - kWeaponSound + kWeaponOwner));
    else
        m = rdU32(rdram, self - kBugSound + kBugOwner);
    BankSwap s1, s2;
    const bool a = swapBankFor(rdram, m, s1), b = swapBankFor(rdram, attacker, s2);
    if (!a && !b)
    {
        g_soundOrig<A>(rdram, ctx, rt);
        return;
    }
    const uint32_t ra = getRegU32(ctx, 31);
    g_soundOrig<A>(rdram, ctx, rt);
    wotmFinishGuestCall(rdram, ctx, rt, ra);
    unswapBank(rdram, s2);
    unswapBank(rdram, s1);
}
template <uint32_t A, int Owner>
void hookOwnBank(PS2Runtime &runtime)
{
    if ((g_soundOrig<A> = runtime.lookupFunction(A)))
        runtime.registerFunction(A, &ownBankHook<A, Owner>);
}
void installOwnBankHooks(PS2Runtime &runtime)
{
    using namespace wotm::fn;
    // os do MonsterSound que tocam ou param sons (fora o construtor, o init e os terminate*)
#define WOTM_MS(f) hookOwnBank<f, kOwnerMonsterSound>(runtime)
    WOTM_MS(play206Sound__12MonsterSound); WOTM_MS(playAlienRaygunDrawSound__12MonsterSound);
    WOTM_MS(playAssBossWeaponDrawSound__12MonsterSound); WOTM_MS(playBatWhooshSound__12MonsterSound);
    WOTM_MS(playBodyImpactSound__12MonsterSound); WOTM_MS(playButtSlamSound__12MonsterSound); WOTM_MS(playCloakingSound__12MonsterSound);
    WOTM_MS(playCounterAttackSound__12MonsterSound); WOTM_MS(playCrowdControlSound__12MonsterSound); WOTM_MS(playDeathSound__12MonsterSoundi);
    WOTM_MS(playEnterCounterBlockSound__12MonsterSound); WOTM_MS(playFootstepSound__12MonsterSound);
    WOTM_MS(playGeneratorTazerSound__12MonsterSound); WOTM_MS(playGrappleAttackSound__12MonsterSound);
    WOTM_MS(playGrapplePickupSound__12MonsterSound); WOTM_MS(playGrappleSwing__12MonsterSoundi); WOTM_MS(playGrappleThrowSound__12MonsterSound);
    WOTM_MS(playImpalerRemoveSound__12MonsterSound); WOTM_MS(playJellyTazerOneShot__12MonsterSound); WOTM_MS(playJumpSound__12MonsterSound);
    WOTM_MS(playLandingSound__12MonsterSound); WOTM_MS(playLavaSpecialInhale__12MonsterSound); WOTM_MS(playObjectThrowSound__12MonsterSound);
    WOTM_MS(playPedSquishSound__12MonsterSound); WOTM_MS(playPickupSound__12MonsterSound); WOTM_MS(playPunchSound__12MonsterSound);
    WOTM_MS(playRamSound__12MonsterSound); WOTM_MS(playRoboJetOneShot__12MonsterSound); WOTM_MS(playRobotHeadSound__12MonsterSound);
    WOTM_MS(playRockmanSpecialBounce__12MonsterSoundP8_fvector); WOTM_MS(playRockmanSpecialExplode__12MonsterSoundP8_fvector);
    WOTM_MS(playRockmanSpecialLaunch__12MonsterSound); WOTM_MS(playScrape__12MonsterSound); WOTM_MS(playShockedSound__12MonsterSound);
    WOTM_MS(playShogunSpecialImpactSound__12MonsterSound); WOTM_MS(playShogunSpecialOneShot__12MonsterSound);
    WOTM_MS(playSignatureSound__12MonsterSound); WOTM_MS(playSonicRoarSound__12MonsterSound); WOTM_MS(playSoundFromAnimation__12MonsterSoundf);
    WOTM_MS(playStompAttackSound__12MonsterSound); WOTM_MS(playSuckerBugLaunch__12MonsterSound); WOTM_MS(playSwing__12MonsterSoundii);
    WOTM_MS(playTakeBatHitSound__12MonsterSound); WOTM_MS(playTakeHitSound__12MonsterSound); WOTM_MS(playTakeHit__12MonsterSoundP7Monster);
    WOTM_MS(playTauntSound__12MonsterSound); WOTM_MS(playVictorySound__12MonsterSoundi); WOTM_MS(playWaterFootstepSound__12MonsterSound);
    WOTM_MS(playWaterSplashSound__12MonsterSound); WOTM_MS(playWingFlap__12MonsterSound);
    WOTM_MS(updateAlienSound__12MonsterSound); WOTM_MS(updateAlienSpecial__12MonsterSoundi); WOTM_MS(updateAssBossRotorSound__12MonsterSound);
    WOTM_MS(updateAssBossSpinSound__12MonsterSound); WOTM_MS(updateClimbingSound__12MonsterSound11MonsterAnim);
    WOTM_MS(updateDashSound__12MonsterSound); WOTM_MS(updateElectricShockSound__12MonsterSound); WOTM_MS(updateEvilEye__12MonsterSound);
    WOTM_MS(updateFireBreath__12MonsterSound); WOTM_MS(updateFlightGlideSound__12MonsterSoundi);
    WOTM_MS(updateGeneralMonsterSounds__12MonsterSound); WOTM_MS(updateJellyTazer__12MonsterSound); WOTM_MS(updateLavaSound__12MonsterSound);
    WOTM_MS(updateLavaSpecialInhaleChargeup__12MonsterSound); WOTM_MS(updateMantisSound__12MonsterSound);
    WOTM_MS(updateMonsterCinemaSound__12MonsterSound); WOTM_MS(updateMonsterSound__12MonsterSound); WOTM_MS(updateOgreSound__12MonsterSound);
    WOTM_MS(updateRobotReticleSound__12MonsterSound); WOTM_MS(updateRobotSound__12MonsterSound); WOTM_MS(updateShogunSound__12MonsterSound);
    WOTM_MS(updateShogunSpecialSound__12MonsterSound); WOTM_MS(updateSpiderMagnoSpecial__12MonsterSound);
    WOTM_MS(updateZapAttackSound__12MonsterSoundiP7Monster);
#undef WOTM_MS
    hookOwnBank<playWeaponLaunchSound__11WeaponSoundP8_fvectori, kOwnerWeapon>(runtime);
    hookOwnBank<playWeaponExplosion__11WeaponSoundP8_fvectorii, kOwnerWeapon>(runtime);
    hookOwnBank<playRockSpecialImpact__11WeaponSoundP8_fvector, kOwnerWeapon>(runtime);
    hookOwnBank<updateWeaponSound__11WeaponSoundP6Weapon, kOwnerWeapon>(runtime);
    hookOwnBank<updateSuckerBugSound__14SuckerBugSoundP8_fvectori, kOwnerBug>(runtime);
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
    if ((g_origTankSound = runtime.lookupFunction(wotm::fn::updateTankSound__9TankSoundP10TankPickupi)))
        runtime.registerFunction(wotm::fn::updateTankSound__9TankSoundP10TankPickupi, &tankSound);
    installOwnBankHooks(runtime);
    std::fprintf(stderr, "[snd] som nativo: %zu funcoes do 989snd trocadas\n", sizeof hooks / sizeof hooks[0]);
}
#else
void wotmSndInstall(PS2Runtime &)
{
    std::fprintf(stderr, "[snd] som nativo desligado: falta native/wotm_snd.hpp (WOTM_NATIVE_DIR)\n");
}
#endif
