// wotm_snd.hpp: som nativo do War of the Monsters, sem IOP nem SPU2 emulados.
//
// O jogo fala com o driver 989snd (que roda no IOP) pelas funcoes `snd_*` da lib989snd do EE. O runtime troca essas funcoes
// por hooks (wotm_live.inc, sempre ligados) que chamam este motor: os bancos .BNK sao lidos do disco, os sons sao executados
// grain a grain como no 989snd e as vozes ADPCM do SPU2 sao mixadas aqui, a 48 kHz, numa AudioStream do raylib.
// Formatos e API em docs/FORMATS.md ("Som") e tools/snd989.py.
//
// O motor de efeitos (grains, envelope ADSR do SPU2, LFO, volume/pan) segue a reimplementacao do 989snd do OpenGOAL
// (github.com/open-goal/jak-project, game/sound/989snd e game/sound/common; Copyright 2021-2024 Ziemas, licenca ISC), adaptada
// ao formato SBlk versao 1 do WotM e reduzida ao que os bancos do jogo usam. Sem reverb.
//
// Uso: Engine::get().setReader(...) (leitura de setores por LSN), startOutput() depois do InitAudioDevice, e a API abaixo
// (espelho das snd_*). mix() pode ser chamado direto para gravar em arquivo (native/sndplay.cpp).
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace wotm::snd {

using u8 = uint8_t;
using s8 = int8_t;
using u16 = uint16_t;
using s16 = int16_t;
using u32 = uint32_t;
using s32 = int32_t;

constexpr int kRate = 48000;          // taxa do SPU2
constexpr int kTickSamples = 200;     // os sons avancam a 240 Hz (48000 / 240)
constexpr s32 kVolumeDontChange = 0x7fffffff;
constexpr s32 kPanReset = -1;
constexpr s32 kPanDontChange = -2;

inline u16 rd16(const u8 *p) { return u16(p[0] | (p[1] << 8)); }
inline u32 rd32(const u8 *p) { return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24); }

// ---------------------------------------------------------------- banco (.BNK = SBlk versao 1)

struct Tone {
    s8 priority, vol, centerNote, centerFine;
    s16 pan;
    s8 mapLow, mapHigh, pbLow, pbHigh;
    u16 adsr1, adsr2, flags;
    u32 sample;   // offset no bloco de amostras do banco
};

enum Grain_ : u32 {
    G_NULL = 0, G_TONE = 1, G_XREF_ID = 2, G_XREF_NUM = 3, G_LFO = 4, G_STARTCHILD = 5, G_STOPCHILD = 6, G_PLUGIN = 7,
    G_BRANCH = 8, G_TONE2 = 9, G_CONTROL_NULL = 20, G_LOOP_START = 21, G_LOOP_END = 22, G_LOOP_CONTINUE = 23, G_STOP = 24,
    G_RAND_PLAY = 25, G_RAND_DELAY = 26, G_RAND_PB = 27, G_PB = 28, G_ADD_PB = 29, G_SET_REG = 30, G_SET_REG_RAND = 31,
    G_INC_REG = 32, G_DEC_REG = 33, G_TEST_REG = 34, G_MARKER = 35, G_GOTO_MARKER = 36, G_GOTO_RANDOM_MARKER = 37,
    G_WAIT_ALL_VOICES = 38, G_PLAY_CYCLE = 39, G_ADD_REG = 40, G_KEY_OFF = 41, G_KILL_VOICES = 42, G_ON_STOP_MARKER = 43,
    G_COPY_REG = 44,
};

// Grain v1: 0x28 bytes = tipo, atraso (ticks de 240 Hz) e 0x20 bytes de parametros
struct Grain {
    u32 type = 0;
    s32 delay = 0;
    u8 raw[0x20] = {};
    s16 param(int i) const { return s16(rd16(raw + 2 * i)); }
    void setParam(int i, s16 v) { raw[2 * i] = u8(v); raw[2 * i + 1] = u8(u16(v) >> 8); }
    s32 i32(int o) const { return s32(rd32(raw + o)); }
    Tone tone() const {
        Tone t;
        t.priority = s8(raw[0]); t.vol = s8(raw[1]); t.centerNote = s8(raw[2]); t.centerFine = s8(raw[3]);
        t.pan = s16(rd16(raw + 4));
        t.mapLow = s8(raw[6]); t.mapHigh = s8(raw[7]); t.pbLow = s8(raw[8]); t.pbHigh = s8(raw[9]);
        t.adsr1 = rd16(raw + 10); t.adsr2 = rd16(raw + 12); t.flags = rd16(raw + 14);
        t.sample = rd32(raw + 16);
        return t;
    }
};

struct Sfx {
    s8 vol = 0, group = 0;
    s16 pan = 0;
    s8 instanceLimit = 0;
    u16 flags = 0;   // 1 = laco
    std::vector<Grain> grains;
};

struct Bank {
    std::string id;           // "SHEL", "GMON"... (BankID ao contrario)
    std::vector<Sfx> sounds;
    std::vector<u8> samples;  // ADPCM do SPU2 (o que o 989snd copia para a SRAM)

    // Arquivo: u32 tipo, u32 nChunks, {offset, tamanho} x n; chunk 0 = SBlk, chunk 1 = amostras
    static std::unique_ptr<Bank> parse(const u8 *d, size_t n) {
        if (n < 0x18 || rd32(d + 4) < 2) return nullptr;
        const u32 bOff = rd32(d + 8), bSize = rd32(d + 12), sOff = rd32(d + 16), sSize = rd32(d + 20);
        if (uint64_t(bOff) + bSize > n || uint64_t(sOff) + sSize > n || bSize < 0x3c) return nullptr;
        const u8 *b = d + bOff;
        if (std::memcmp(b, "SBlk", 4) != 0 || rd32(b + 4) != 1) return nullptr;
        auto bank = std::make_unique<Bank>();
        for (int i = 3; i >= 0; --i)
            if (b[12 + i]) bank->id += char(b[12 + i]);
        const int nSounds = s16(rd16(b + 0x16));
        const u32 firstSound = rd32(b + 0x1c), firstGrain = rd32(b + 0x20);
        bank->sounds.resize(std::max(nSounds, 0));
        for (int i = 0; i < nSounds; ++i) {
            const u32 o = firstSound + 12u * u32(i);
            if (o + 12 > bSize) return nullptr;
            Sfx &s = bank->sounds[size_t(i)];
            s.vol = s8(b[o]); s.group = s8(b[o + 1]); s.pan = s16(rd16(b + o + 2));
            const int ng = s8(b[o + 4]);
            s.instanceLimit = s8(b[o + 5]); s.flags = rd16(b + o + 6);
            const u32 fg = firstGrain + rd32(b + o + 8);
            for (int g = 0; g < ng; ++g) {
                const u32 go = fg + 0x28u * u32(g);
                if (go + 0x28 > bSize) return nullptr;
                Grain gr;
                gr.type = rd32(b + go);
                gr.delay = s32(rd32(b + go + 4));
                std::memcpy(gr.raw, b + go + 8, sizeof gr.raw);
                s.grains.push_back(gr);
            }
        }
        bank->samples.assign(d + sOff, d + sOff + sSize);
        return bank;
    }
};

// ---------------------------------------------------------------- tabelas e conversoes do 989snd

struct VolPair { s16 left = 0, right = 0; };

inline const std::array<VolPair, 181> &panTable() {   // normalPanTable: 0x3fff * (cos, sen) de 0 a 90 graus
    static const std::array<VolPair, 181> t = [] {
        std::array<VolPair, 181> r{};
        for (int i = 0; i <= 180; ++i) {
            const double a = i * 3.14159265358979323846 / 360.0;
            r[size_t(i)] = {s16(std::lround(0x3fff * std::cos(a))), s16(std::lround(0x3fff * std::sin(a)))};
        }
        return r;
    }();
    return t;
}

inline const std::array<s16, 2048> &lfoSine() {
    static const std::array<s16, 2048> t = [] {
        std::array<s16, 2048> r{};
        for (int i = 0; i < 2048; ++i) r[size_t(i)] = s16(std::lround(32767 * std::cos(i * 2 * 3.14159265358979323846 / 2048)));
        return r;
    }();
    return t;
}

// Registro de pitch do SPU2 (0x1000 = taxa nativa de 48 kHz). CenterNote negativa = nota de PS2 (48 kHz); positiva = de PS1 (44,1 kHz).
// Como no sceSdNote2Pitch, o CenterFine SOMA ao fine (SHELLMUS: nota 60, centro -62/66 -> 44058 Hz, a taxa da amostra).
inline u16 note2Pitch(s8 centerNote, s8 centerFine, int note, int fine) {
    const bool ps1 = centerNote >= 0;
    const int cn = ps1 ? centerNote : -centerNote;
    const double semis = (note - cn) + (fine + centerFine) / 128.0;
    double p = 4096.0 * std::pow(2.0, semis / 12.0);
    if (ps1) p = p * 44100.0 / 48000.0;
    return u16(std::clamp(p, 0.0, 65535.0));
}

inline std::pair<int, int> pitchBend(const Tone &t, int pb, int pm, int note, int fine) {
    const s32 base = (note << 7) + fine + pm;
    const s32 v = pb >= 0 ? t.pbHigh * (pb << 7) / 0x7fff + base : t.pbLow * (pb << 7) / 0x8000 + base;
    return {v / 128, v % 128};
}

// ---------------------------------------------------------------- envelope ADSR do SPU2

struct Adsr {
    enum Phase { Attack, Decay, Sustain, Release, Stopped };
    u32 reg = 0;   // ADSR1 | ADSR2 << 16
    Phase phase = Stopped;
    s32 level = 0, target = 0;
    u32 counter = 0;
    u8 shift = 0;
    s8 step = 0;
    bool exp = false, decrease = false;

    u32 bits(int lo, int n) const { return (reg >> lo) & ((1u << n) - 1); }
    void stepEnv() {
        u32 cStep = 0x800000;
        const s32 sh = shift - 11;
        if (sh > 0) cStep >>= sh;
        s16 st = s16(step << std::max(0, 11 - shift));
        if (exp) {
            if (!decrease && level > 0x6000) cStep >>= 2;
            if (decrease) st = s16((st * level) >> 15);
        }
        counter += cStep;
        if (counter >= 0x800000) {
            counter = 0;
            level = std::clamp<s32>(level + st, 0, 0x7fff);
        }
    }
    void update() {
        switch (phase) {
        case Attack: exp = bits(15, 1); decrease = false; shift = u8(bits(10, 5)); step = s8(7 - bits(8, 2)); target = 0x7fff; break;
        case Decay: exp = true; decrease = true; shift = u8(bits(4, 4)); step = -8; target = s32(bits(0, 4) + 1) << 11; break;
        case Sustain:
            exp = bits(31, 1); decrease = bits(30, 1); shift = u8(bits(24, 5));
            step = decrease ? s8(-8 + int(bits(22, 2))) : s8(7 - int(bits(22, 2)));
            target = 0;
            break;
        case Release: exp = bits(21, 1); decrease = true; shift = u8(bits(16, 5)); step = -8; target = 0; break;
        default: break;
        }
    }
    void run() {
        if (phase == Stopped) return;
        stepEnv();
        if (phase == Sustain) return;
        if ((!decrease && level >= target) || (decrease && level <= target)) {
            phase = phase == Attack ? Decay : phase == Decay ? Sustain : Stopped;
            update();
        }
    }
    void attack() { phase = Attack; level = 0; counter = 0; update(); }
    void release() { if (phase != Stopped) { phase = Release; counter = 0; update(); } }
    void stop() { phase = Stopped; level = 0; }
};

// ---------------------------------------------------------------- decodificador ADPCM do SPU2

// Blocos de 16 bytes: (filtro << 4 | shift), flags (1 = fim, 2 = repete, 4 = inicio do laco), 14 bytes = 28 amostras de 4 bits
struct AdpcmReader {
    const u8 *data = nullptr;
    size_t size = 0, pos = 0, loop = 0;
    s32 h1 = 0, h2 = 0;
    bool ended = false;
    bool honorFlags = true;   // os .VPK nao marcam fim nem laco: param no fim dos dados

    // Decodifica um bloco (28 amostras). Devolve false sem nada quando acabou.
    bool block(s16 out[28]) {
        static constexpr s32 k[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};
        if (ended || pos + 16 > size) { ended = true; return false; }
        const u8 *b = data + pos;
        const int shift = b[0] & 15, filt = std::min(b[0] >> 4, 4), fl = honorFlags ? b[1] : 0;
        if (fl & 4) loop = pos;
        for (int i = 0; i < 28; ++i) {
            const int nib = (b[2 + i / 2] >> ((i & 1) * 4)) & 15;
            s32 s = s16(u16(nib << 12)) >> shift;
            s += (k[filt][0] * h1 + k[filt][1] * h2) >> 6;
            s = std::clamp<s32>(s, -32768, 32767);
            h2 = h1; h1 = s;
            out[i] = s16(s);
        }
        pos += 16;
        if (fl & 1) {
            if (fl & 2) pos = loop;
            else ended = true;   // as 28 amostras deste bloco ainda tocam
        }
        return true;
    }
};

// Uma voz do SPU2: ADPCM -> reamostragem pelo pitch -> envelope -> volume esquerdo/direito
struct Voice {
    AdpcmReader src;
    Adsr adsr;
    std::array<s16, 64> fifo{};
    int head = 0, count = 0;
    u32 counter = 0;
    u16 pitch = 0x1000;
    s32 volL = 0, volR = 0;   // 0..0x7fff
    bool paused = false;
    bool envelope = true;     // streams: sem ADSR
    bool dead = false;

    s16 peek(int i) const { return fifo[size_t((head + i) & 63)]; }
    void refill() {
        while (count < 8 && !src.ended) {
            s16 b[28];
            if (!src.block(b)) break;
            for (s16 s : b) { fifo[size_t((head + count) & 63)] = s; ++count; }
        }
    }
    void keyOn() { adsr.attack(); counter = 0; head = count = 0; dead = false; refill(); }
    void keyOff() { if (envelope) adsr.release(); else dead = true; }

    // Uma amostra de saida a 48 kHz (acumula em l/r)
    void run(s32 &l, s32 &r) {
        if (dead || paused) return;
        refill();
        if (count < 4) {
            if (src.ended) { dead = true; return; }
        }
        // interpolacao cubica (Catmull-Rom) entre peek(1) e peek(2); o SPU2 usa uma gaussiana de 4 pontos
        const float t = float(counter) / 4096.0f;
        const float p0 = peek(0), p1 = peek(1), p2 = peek(2), p3 = peek(3);
        float v = p1 + 0.5f * t * (p2 - p0 + t * (2 * p0 - 5 * p1 + 4 * p2 - p3 + t * (3 * (p1 - p2) + p3 - p0)));
        s32 s = s32(std::clamp(v, -32768.0f, 32767.0f));
        counter += std::min<u32>(pitch, 0x3fff);
        int steps = int(counter >> 12);
        counter &= 0xfff;
        while (steps-- > 0 && count > 0) { head = (head + 1) & 63; --count; }
        if (envelope) {
            s = (s * adsr.level) >> 15;
            adsr.run();
            if (adsr.phase == Adsr::Stopped) dead = true;
        }
        l += (s * volL) >> 15;
        r += (s * volR) >> 15;
    }
};

class Engine;

// ---------------------------------------------------------------- sons (instancias) e streams

struct Sound {
    u32 handle = 0;
    int group = 0;
    bool paused = false;
    virtual ~Sound() = default;
    virtual bool tick() = 0;   // true quando terminou
    virtual void stop() = 0;
    virtual void setPause(bool p) = 0;
    virtual void setVolPan(s32 vol, s32 pan) = 0;
    virtual void setPMod(s32) {}
    virtual void setPBend(s32) {}
    virtual void refreshVolume() = 0;   // depois de mudar o volume mestre do grupo
    virtual const Bank *bank() const { return nullptr; }
};

struct BlockSound;

struct Lfo {
    enum Type { Off, Sine, Square, Triangle, Saw, Rand };
    enum Target { None, Volume, Pan, PMod, PBend };
    int type = Off, target = None;
    u8 setupFlags = 0;
    s16 depth = 0;
    s32 nextStep = 0, stepSize = 0, hold1 = 0, hold2 = 0, range = 0;
    u32 tick = 0;
    s32 get(s32 mult);
};

class Engine {
public:
    using Reader = std::function<bool(u32 lsn, u32 sectors, u8 *dst)>;

    static Engine &get() { static Engine e; return e; }

    void setReader(Reader r) { std::lock_guard<std::mutex> g(m_); reader_ = std::move(r); }

    // Banco pelo setor inicial (snd_BankLoadByLoc). 0 em caso de erro.
    u32 loadBank(u32 lsn, u32 offset) {
        std::vector<u8> head(2048);
        Reader rd;
        { std::lock_guard<std::mutex> g(m_); rd = reader_; }
        if (!rd || offset != 0 || !rd(lsn, 1, head.data())) return log("[snd] banco no LSN %u: leitura falhou\n", lsn), 0u;
        const u32 n = rd32(head.data() + 4);
        if (n < 2 || n > 8) return log("[snd] LSN %u nao e um banco\n", lsn), 0u;
        u32 total = 0;
        for (u32 i = 0; i < n; ++i) total = std::max(total, rd32(head.data() + 8 + 8 * i) + rd32(head.data() + 12 + 8 * i));
        std::vector<u8> file((total + 2047) / 2048 * 2048);
        if (!rd(lsn, u32(file.size() / 2048), file.data())) return log("[snd] banco no LSN %u: leitura falhou\n", lsn), 0u;
        return addBank(Bank::parse(file.data(), file.size()), lsn);
    }
    // forceGroup >= 0: todos os sons do banco tocam nesse grupo (volume mestre e pausa por grupo), nao no que o banco diz
    u32 loadBankData(const std::vector<u8> &file, int forceGroup = -1) {
        auto b = Bank::parse(file.data(), file.size());
        if (b && forceGroup >= 0)
            for (Sfx &s : b->sounds) s.group = s8(forceGroup);
        return addBank(std::move(b), 0);
    }
    void unloadBank(u32 h) {
        std::lock_guard<std::mutex> g(m_);
        auto it = banks_.find(h);
        if (it == banks_.end()) return;
        for (auto s = sounds_.begin(); s != sounds_.end();)
            s = s->second->bank() == it->second.get() ? sounds_.erase(s) : std::next(s);
        banks_.erase(it);
    }
    const Bank *bank(u32 h) const { auto it = banks_.find(h); return it == banks_.end() ? nullptr : it->second.get(); }

    u32 play(u32 bankHandle, int sound, s32 vol, s32 pan, s32 pm, s32 pb);
    u32 playStream(u32 lsn, s32 vol, s32 pan, int group);

    void stop(u32 h) { std::lock_guard<std::mutex> g(m_); if (Sound *s = find(h)) s->stop(); }
    void pause(u32 h, bool p) { std::lock_guard<std::mutex> g(m_); if (Sound *s = find(h)) s->setPause(p); }
    bool playing(u32 h) { std::lock_guard<std::mutex> g(m_); return find(h) != nullptr; }
    void setVolPan(u32 h, s32 vol, s32 pan) { std::lock_guard<std::mutex> g(m_); if (Sound *s = find(h)) s->setVolPan(vol, pan); }
    void setPMod(u32 h, s32 pm) { std::lock_guard<std::mutex> g(m_); if (Sound *s = find(h)) s->setPMod(pm); }
    void setPBend(u32 h, s32 pb) { std::lock_guard<std::mutex> g(m_); if (Sound *s = find(h)) s->setPBend(pb); }
    // snd_SetSoundParams: mascara 1 = volume, 4 = pan, 8 = pitch mod, 0x10 = pitch bend (deduzida das chamadas do jogo)
    void setParams(u32 h, u32 mask, s32 vol, s32 pan, s32 pm, s32 pb) {
        std::lock_guard<std::mutex> g(m_);
        Sound *s = find(h);
        if (!s) return;
        if (mask & 5) s->setVolPan((mask & 1) ? vol : kVolumeDontChange, (mask & 4) ? pan : kPanDontChange);
        if (mask & 8) s->setPMod(pm);
        if (mask & 0x10) s->setPBend(pb);
    }
    void stopAll() { std::lock_guard<std::mutex> g(m_); sounds_.clear(); }
    void stopAllStreams() {
        std::lock_guard<std::mutex> g(m_);
        for (auto s = sounds_.begin(); s != sounds_.end();) s = s->second->bank() ? std::next(s) : sounds_.erase(s);
    }
    void pauseGroups(u32 mask, bool p) {
        std::lock_guard<std::mutex> g(m_);
        for (auto &[h, s] : sounds_)
            if (s->group < 32 && (mask >> s->group) & 1) s->setPause(p);
    }
    void setMasterVolume(int group, s32 vol) {
        std::lock_guard<std::mutex> g(m_);
        if (group < 0 || group >= 32) return;
        master_[size_t(group)] = std::clamp<s32>(vol, 0, 0x400);
        for (auto &[h, s] : sounds_) if (s->group == group) s->refreshVolume();
    }
    s32 masterVolume(int group) const { return group >= 0 && group < 32 ? master_[size_t(group)] : 0; }
    void setMono(bool m) { std::lock_guard<std::mutex> g(m_); mono_ = m; }
    s32 sramFree() const {   // 2 MB do SPU2 menos o que esta "carregado" (so informativo; o jogo imprime)
        size_t used = 0;
        for (auto &[h, b] : banks_) used += b->samples.size();
        return s32(std::max<long long>(0, 0x1f0000ll - static_cast<long long>(used)));
    }

    // Mixagem: frames estereo s16 a 48 kHz (thread de audio)
    void mix(s16 *out, unsigned frames) {
        std::lock_guard<std::mutex> g(m_);
        for (unsigned i = 0; i < frames; ++i) {
            if (tickPhase_ == 0) tickAll();
            tickPhase_ = (tickPhase_ + 1) % kTickSamples;
            s32 l = 0, r = 0;
            for (auto &v : voices_) v->run(l, r);
            out[2 * i] = s16(std::clamp(l, -32768, 32767));
            out[2 * i + 1] = s16(std::clamp(r, -32768, 32767));
        }
        voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const std::shared_ptr<Voice> &v) { return v->dead && v.use_count() == 1; }),
                      voices_.end());
    }

    bool startOutput();   // AudioStream do raylib (precisa de InitAudioDevice antes)

    // --- usados pelos sons
    VolPair makeVolume(int vol1, int pan1, int vol2, int pan2, int vol3, int pan3) const {
        s32 vol = vol1 * 258;
        vol = vol * vol2 / 0x7f;
        vol = vol * vol3 / 0x7f;
        if (vol == 0) return {};
        if (mono_) return {s16(vol), s16(vol)};
        int p = (pan1 + pan2 + pan3) % 360;
        if (p < 0) p += 360;
        p = p >= 270 ? p - 270 : p + 90;
        const auto &t = panTable();
        if (p < 180) return {s16(t[size_t(p)].left * vol / 0x3fff), s16(t[size_t(p)].right * vol / 0x3fff)};
        return {s16(t[size_t(p - 180)].right * vol / 0x3fff), s16(t[size_t(p - 180)].left * vol / 0x3fff)};
    }
    s32 adjustToGroup(s32 v, int group) const {   // volume mestre do grupo, com a curva quadratica do 989snd
        if (group >= 15) return v;
        v = std::min(v, 0x7ffe);
        v = v * master_[size_t(group)] / 0x400;
        const int sign = v < 0 ? -1 : 1;
        return v * v / 0x7ffe * sign;
    }
    void addVoice(const std::shared_ptr<Voice> &v) { voices_.push_back(v); }
    u32 tickCount() const { return tick_; }
    std::array<s8, 32> blockReg{};   // registradores globais dos grains (negativos)
    u32 rnd() { rng_ = rng_ * 1103515245u + 12345u; return (rng_ >> 16) & 0x7fff; }
    std::unique_ptr<BlockSound> makeBlock(const Bank &bank, int sound, s32 vol, s32 pan, s32 appVol, s32 appPan, s32 pm, s32 pb,
                                          const std::array<s8, 4> *regs);
    template <class... A> static void log(const char *fmt, A... a) { std::fprintf(stderr, fmt, a...); }

private:
    Engine() { master_.fill(0x400); }
    Sound *find(u32 h) { auto it = sounds_.find(h); return it == sounds_.end() ? nullptr : it->second.get(); }
    u32 newHandle() {
        do { nextHandle_ = (nextHandle_ + 1) & 0x7fffffff; } while (nextHandle_ == 0 || sounds_.count(nextHandle_));
        return nextHandle_;
    }
    u32 addBank(std::unique_ptr<Bank> b, u32 lsn) {
        if (!b) return log("[snd] banco invalido (LSN %u)\n", lsn), 0u;
        std::lock_guard<std::mutex> g(m_);
        const u32 h = 0x10000u + nextBank_++;
        log("[snd] banco %s carregado (LSN %u): %zu sons, %zu bytes de amostras -> %#x\n", b->id.c_str(), lsn, b->sounds.size(),
            b->samples.size(), h);
        banks_[h] = std::move(b);
        return h;
    }
    void tickAll() {
        ++tick_;
        for (auto it = sounds_.begin(); it != sounds_.end();) it = it->second->tick() ? sounds_.erase(it) : std::next(it);
    }

    std::mutex m_;
    Reader reader_;
    std::map<u32, std::unique_ptr<Bank>> banks_;
    std::map<u32, std::unique_ptr<Sound>> sounds_;
    std::vector<std::shared_ptr<Voice>> voices_;
    std::array<s32, 32> master_{};
    bool mono_ = false;
    u32 nextHandle_ = 0, nextBank_ = 1, tick_ = 0, rng_ = 0x989u;
    int tickPhase_ = 0;
};

// ---------------------------------------------------------------- som de banco: executa os grains

struct BlockSound : Sound {
    Engine &eng;
    const Bank &bk;
    const Sfx *sfx;
    s32 origVol = 0, origPan = 0, curVol = 0, curPan = 0, curPb = 0, curPm = 0;
    s32 appVol = 0, appPan = 0, appPb = 0, appPm = 0;
    s32 lfoVol = 0, lfoPan = 0, lfoPm = 0, lfoPb = 0;
    int note = 60, fine = 0;
    std::array<s8, 4> regs{};
    std::array<Lfo, 4> lfo{};
    struct V { std::shared_ptr<Voice> v; Tone tone; s32 gVol, gPan; VolPair base; };
    std::vector<V> voices;
    std::vector<std::unique_ptr<BlockSound>> children;
    int nextGrain = 0, countdown = 0;
    bool done = false, skipGrains = false;
    int grainsToPlay = 0, grainsToSkip = 0;

    BlockSound(Engine &e, const Bank &b, const Sfx &s) : eng(e), bk(b), sfx(&s) { group = s.group; }
    ~BlockSound() override { for (auto &v : voices) v.v->dead = true; }   // as vozes apontam para as amostras do banco
    const Bank *bank() const override { return &bk; }

    void start(s32 sfxVol, s32 sfxPan, s32 vol, s32 pan, s32 pm, s32 pb) {
        if (sfxVol == -1) sfxVol = sfx->vol;
        if (sfxPan == -1) sfxPan = sfx->pan;
        if (vol == kVolumeDontChange) vol = 1024;
        if (pan == kPanReset || pan == kPanDontChange) pan = sfxPan;
        origVol = sfxVol; origPan = sfxPan;
        curVol = std::min((sfxVol * vol) >> 10, 127);
        curPan = pan; curPb = pb; curPm = pm;
        appVol = vol; appPan = pan; appPb = pb; appPm = pm;
        if (sfx->grains.empty()) { done = true; return; }
        countdown = sfx->grains[0].delay;
        while (countdown <= 0 && !done) doGrain();
    }

    void cleanVoices() { voices.erase(std::remove_if(voices.begin(), voices.end(), [](const V &v) { return v.v->dead; }), voices.end()); }

    bool tick() override {
        cleanVoices();
        for (auto &l : lfo) tickLfo(l);
        for (auto it = children.begin(); it != children.end();) it = (*it)->tick() ? children.erase(it) : std::next(it);
        if (done && children.empty()) return voices.empty();
        if (paused) return false;
        --countdown;
        while (countdown <= 0 && !done) doGrain();
        return false;
    }
    void stop() override {
        done = true;
        for (auto &c : children) c->stop();
        for (auto &v : voices) v.v->keyOff();
    }
    void setPause(bool p) override {
        paused = p;
        for (auto &c : children) c->setPause(p);
        for (auto &v : voices) v.v->paused = p;
    }
    void applyVoice(V &v) {
        const VolPair vp = eng.makeVolume(127, 0, curVol, curPan, v.gVol, v.gPan);
        v.v->volL = eng.adjustToGroup(vp.left, group);
        v.v->volR = eng.adjustToGroup(vp.right, group);
    }
    void refreshVolume() override {
        for (auto &v : voices) applyVoice(v);
        for (auto &c : children) c->refreshVolume();
    }
    void setVolPan(s32 vol, s32 pan) override {
        if (vol >= 0) { if (vol != kVolumeDontChange) appVol = vol; }
        else appVol = -1024 * vol / 127;
        if (pan == kPanReset) appPan = sfx->pan;
        else if (pan != kPanDontChange) appPan = pan;
        const s32 nv = std::clamp(((appVol * origVol) >> 10) + lfoVol, 0, 127);
        s32 np = (appPan + lfoPan) % 360;
        if (np < 0) np += 360;
        if (np == curPan && nv == curVol) return;
        curVol = nv; curPan = np;
        for (auto &c : children) c->setVolPan(appVol * origVol / 127, pan);
        for (auto &v : voices) applyVoice(v);
    }
    void updatePitch() {
        curPm = appPm + lfoPm;
        curPb = std::clamp<s32>(appPb + lfoPb, -32768, 32767);
        for (auto &v : voices) {
            const auto n = pitchBend(v.tone, curPb, curPm, note, fine);
            v.v->pitch = note2Pitch(v.tone.centerNote, v.tone.centerFine, n.first, n.second);
        }
    }
    void setPMod(s32 m) override { for (auto &c : children) c->setPMod(m); appPm = m; updatePitch(); }
    void setPBend(s32 b) override { for (auto &c : children) c->setPBend(b); appPb = b; updatePitch(); }

    s32 regValue(s32 v, bool asPan) {   // parametros negativos: -1..-4 registradores do som, -5 aleatorio, -6.. globais
        if (v >= 0) return v;
        s32 r;
        if (v >= -4) r = regs[size_t(-v - 1)];
        else if (v == -5) return asPan ? s32(eng.rnd() % 360) : s32(eng.rnd() % 0x7f);
        else r = eng.blockReg[size_t(std::min(-v - 6, 31))];
        return asPan ? 360 * std::min(std::abs(r), 127) / 127 : r;
    }
    s8 &reg(int r) { return r < 0 ? eng.blockReg[size_t(std::min(-r - 1, 31))] : regs[size_t(r & 3)]; }

    void doGrain() {
        Grain &g = const_cast<Grain &>(sfx->grains[size_t(nextGrain)]);   // RAND_PLAY/PLAY_CYCLE guardam estado no grain, como no 989snd
        const s32 ret = runGrain(g);
        if (skipGrains && --grainsToPlay == 0) { nextGrain += grainsToSkip; skipGrains = false; }
        if (++nextGrain >= int(sfx->grains.size()) || nextGrain < 0) { done = true; return; }
        countdown = sfx->grains[size_t(nextGrain)].delay + ret;
    }

    int findGrain(u32 type, int from, int dir, int mark = -1) const {
        for (int i = from; i >= 0 && i < int(sfx->grains.size()); i += dir)
            if (sfx->grains[size_t(i)].type == type && (mark < 0 || sfx->grains[size_t(i)].param(0) == mark)) return i;
        return -1;
    }

    s32 runGrain(Grain &g);
    void tickLfo(Lfo &l);
};

inline s32 Lfo::get(s32 mult) {
    const s32 step = (nextStep >> 16) & 2047;
    nextStep += mult * stepSize;
    if (nextStep > 0x7ffffff) nextStep -= 0x8000000;
    s32 r = 0;
    switch (type) {
    case Sine: r = lfoSine()[size_t(step)]; break;
    case Square: r = step >= hold1 ? -32767 : 32767; break;
    case Triangle: r = step < 512 ? 0x7fff * step / 512 : step >= 1536 ? 0x7fff * (step - 1536) / 512 - 0x7fff : 0x7fff - 65534 * (step - 512) / 1024; break;
    case Saw: r = step >= 1024 ? 0x7fff * (step - 1024) / 1024 - 0x7fff : 0x7fff * step / 1023; break;
    case Rand: r = hold1; break;   // o valor aleatorio e sorteado em tickLfo
    default: break;
    }
    return (setupFlags & 1) ? -r : r;
}

inline s32 BlockSound::runGrain(Grain &g) {
    switch (g.type) {
    case G_TONE:
    case G_TONE2: {
        curVol = std::clamp(((appVol * origVol) >> 10) + lfoVol, 0, 127);
        curPan = (appPan + lfoPan) % 360;
        if (curPan < 0) curPan += 360;
        const Tone t = g.tone();
        if (t.flags & 8) return 0;   // ruido: nao usado pelo WotM
        if (t.sample >= bk.samples.size()) return 0;
        V v;
        v.tone = t;
        v.gVol = std::max(regValue(t.vol, false), 0);
        v.gPan = regValue(t.pan, true) % 360;
        v.v = std::make_shared<Voice>();
        v.v->src.data = bk.samples.data();
        v.v->src.size = bk.samples.size();
        v.v->src.pos = v.v->src.loop = t.sample;
        v.v->adsr.reg = u32(t.adsr1) | (u32(t.adsr2) << 16);
        const auto n = pitchBend(t, curPb, curPm, note, fine);
        v.v->pitch = note2Pitch(t.centerNote, t.centerFine, n.first, n.second);
        v.v->paused = paused;
        applyVoice(v);
        v.v->keyOn();
        eng.addVoice(v.v);
        voices.push_back(v);
        return 0;
    }
    case G_LFO: {
        const u8 which = g.raw[0] & 3;
        Lfo &l = lfo[which];
        l = Lfo{};
        l.target = g.raw[1];
        if (l.target == Lfo::None || l.target > Lfo::PBend) { l.type = Lfo::Off; return 0; }
        l.type = g.raw[3];
        l.setupFlags = u8(rd16(g.raw + 8));
        l.depth = s16(rd16(g.raw + 6));
        l.stepSize = s32(rd32(g.raw + 12));
        if (l.type == Lfo::Square) l.hold1 = rd16(g.raw + 4);
        l.nextStep = (l.setupFlags & 2) ? s32((eng.rnd() & 0x7ff) << 16) : s32(rd16(g.raw + 10)) << 16;
        if (l.type == Lfo::Rand) { l.hold1 = -s32(eng.rnd() & 0x7fff) * s32(eng.rnd() & 1); l.hold2 = 1; }
        switch (l.target) {
        case Lfo::Volume: l.range = (sfx->vol * l.depth) >> 10; break;
        case Lfo::Pan: l.range = (180 * l.depth) >> 10; break;
        case Lfo::PMod: l.range = (6096 * l.depth) >> 10; break;
        case Lfo::PBend: l.range = (0x7fff * l.depth) >> 10; break;
        }
        tickLfo(l);
        return 0;
    }
    case G_STARTCHILD: {
        const s32 idx = g.i32(12);
        if (idx < 0 || idx >= s32(bk.sounds.size())) return 0;
        const s32 vol = std::clamp(std::abs(regValue(g.i32(0), false)), 0, 127);
        const s32 pan = regValue(g.i32(4), true);
        if (auto c = eng.makeBlock(bk, idx, vol, pan, appVol * origVol / 127, appPan, appPm, appPb, &regs)) children.push_back(std::move(c));
        return 0;
    }
    case G_STOPCHILD: {
        const s32 idx = g.i32(12);
        if (idx < 0 || idx >= s32(bk.sounds.size())) return 0;
        for (auto it = children.begin(); it != children.end();) it = (*it)->sfx == &bk.sounds[size_t(idx)] ? children.erase(it) : std::next(it);
        return 0;
    }
    case G_BRANCH: {
        const s32 idx = g.i32(12);
        if (idx < 0 || idx >= s32(bk.sounds.size())) return 0;
        for (auto &v : voices) { v.v->keyOff(); v.v->volL = v.v->volR = 0; }
        sfx = &bk.sounds[size_t(idx)];
        origVol = sfx->vol; origPan = sfx->pan;
        curVol = (appVol * sfx->vol) >> 10;
        group = sfx->group;
        nextGrain = -1;
        skipGrains = false; grainsToPlay = grainsToSkip = 0;
        return 0;   // doGrain soma o atraso do grain 0 do novo som
    }
    case G_LOOP_END: { const int i = findGrain(G_LOOP_START, nextGrain - 1, -1); if (i >= 0) nextGrain = i - 1; return 0; }
    case G_LOOP_CONTINUE: { const int i = findGrain(G_LOOP_END, nextGrain + 1, 1); if (i >= 0) nextGrain = i; return 0; }
    case G_STOP: done = true; return 0;
    case G_RAND_PLAY: {
        const int options = std::max<int>(g.param(0), 1), cnt = g.param(1);
        int r = int(eng.rnd() % u32(options));
        if (r == g.param(2) && ++r >= options) r = 0;
        g.setParam(2, s16(r));
        nextGrain += r * cnt;
        grainsToPlay = cnt + 1;
        grainsToSkip = (options - 1 - r) * cnt;
        skipGrains = true;
        return 0;
    }
    case G_RAND_DELAY: { const s32 a = g.i32(0) + 0; return a > 0 ? s32(eng.rnd() % u32(a)) : 0; }
    case G_RAND_PB: {
        const s32 pb = g.param(0);
        setPBend(pb * ((0xffff * s32(eng.rnd() % 0x7fff)) / 0x7fff - 0x8000) / 100);
        return 0;
    }
    case G_ADD_PB: setPBend(std::clamp<s32>(curPb + 0x7fff * g.param(0) / 127, -32768, 32767)); return 0;
    case G_PB: { const s32 pb = g.param(0); setPBend(pb >= 0 ? 0x7fff * pb / 127 : -0x8000 * pb / -128); return 0; }
    case G_SET_REG: reg(g.param(0)) = s8(g.param(1)); return 0;
    case G_SET_REG_RAND: {
        const s32 lo = g.param(1), range = std::max(g.param(2) - lo + 1, 1);
        reg(g.param(0)) = s8(s32(eng.rnd() % u32(range)) + lo);
        return 0;
    }
    case G_INC_REG: { s8 &r = reg(g.param(0)); r = s8(std::clamp(r + 1, -128, 127)); return 0; }
    case G_DEC_REG: { s8 &r = reg(g.param(0)); r = s8(std::clamp(r - 1, -128, 127)); return 0; }
    case G_ADD_REG: { s8 &r = reg(g.param(1)); r = s8(std::clamp(r + g.param(0), -128, 127)); return 0; }
    case G_COPY_REG: { const s8 v = reg(g.param(0)); reg(g.param(1)) = v; return 0; }
    case G_TEST_REG: {
        const s32 v = reg(g.param(0)), action = g.param(1), cmp = g.param(2);
        if ((action == 0 && v >= cmp) || (action == 1 && v != cmp) || (action >= 2 && cmp >= v)) ++nextGrain;
        return 0;
    }
    case G_GOTO_MARKER: { const int i = findGrain(G_MARKER, 0, 1, g.param(0)); if (i >= 0) nextGrain = i - 1; return 0; }
    case G_GOTO_RANDOM_MARKER: {
        const s32 lo = g.param(0), range = std::max(g.param(1) - lo + 1, 1);
        const int i = findGrain(G_MARKER, 0, 1, s32(eng.rnd() % u32(range)) + lo);
        if (i >= 0) nextGrain = i - 1;
        return 0;
    }
    case G_WAIT_ALL_VOICES:
        cleanVoices();
        if (!voices.empty()) { --nextGrain; return 1; }
        return 0;
    case G_PLAY_CYCLE: {
        const int size = std::max<int>(g.param(0), 1), cnt = g.param(1);
        const int a = g.param(2);
        g.setParam(2, s16((a + 1) % size));
        nextGrain += cnt * a;
        grainsToPlay = cnt + 1;
        grainsToSkip = (size - 1 - a) * cnt;
        skipGrains = true;
        return 0;
    }
    case G_KEY_OFF: for (auto &v : voices) v.v->keyOff(); return 0;
    case G_KILL_VOICES: for (auto &v : voices) { v.v->keyOff(); v.v->volL = v.v->volR = 0; } return 0;
    case G_ON_STOP_MARKER: nextGrain = int(sfx->grains.size()) - 1; return 0;
    default: return 0;   // NULL, XREF, PLUGIN, CONTROL_NULL, LOOP_START, MARKER
    }
}

// LFO: anda a cada 2 ticks de 240 Hz e empurra o volume/pan/pitch deste som
inline void BlockSound::tickLfo(Lfo &l) {
    ++l.tick;
    if (l.target == Lfo::None || l.type == Lfo::Off || (l.tick & 1) == 0) return;
    if (l.type == Lfo::Rand) {
        const s32 step = (l.nextStep >> 16) & 2047;
        if (step >= 1024 && l.hold2 == 1) { l.hold2 = 0; l.hold1 = 2 * (s32(eng.rnd() & 0x7fff) - 0x3fff); }
        else if (step < 1024 && l.hold2 == 0) { l.hold2 = 1; l.hold1 = -s32(eng.rnd() & 0x7fff) * s32(eng.rnd() & 1); }
    }
    const s32 v = l.get(2);
    switch (l.target) {
    case Lfo::Volume: { const s32 x = (l.range * (v - 0x7fff)) >> 16; if (x != lfoVol) { lfoVol = x; setVolPan(kVolumeDontChange, kPanDontChange); } break; }
    case Lfo::Pan: { const s32 x = (l.range * v) >> 15; if (x != lfoPan) { lfoPan = x; setVolPan(kVolumeDontChange, kPanDontChange); } break; }
    case Lfo::PMod: { const s32 x = (v * l.range) >> 15; if (x != lfoPm) { lfoPm = x; updatePitch(); } break; }
    case Lfo::PBend: { const s32 x = (v * l.range) >> 15; if (x != lfoPb) { lfoPb = x; updatePitch(); } break; }
    default: break;
    }
}

// ---------------------------------------------------------------- stream .VPK/.VAG (musica, vozes, cinemas)

struct Stream : Sound {
    Engine &eng;
    std::vector<u8> file;
    struct Ch { std::shared_ptr<Voice> v; s32 vol = 127, pan = 0; std::vector<u8> data; };
    std::vector<Ch> ch;
    s32 vol = 0x400, pan = 0;

    explicit Stream(Engine &e) : eng(e) {}
    ~Stream() override { for (auto &c : ch) if (c.v) c.v->dead = true; }
    void applyVolume() {
        const s32 v127 = std::clamp(vol * 127 / 0x400, 0, 127);
        for (auto &c : ch) {
            const VolPair vp = eng.makeVolume(127, 0, v127, pan, c.vol, c.pan);
            c.v->volL = eng.adjustToGroup(vp.left, group);
            c.v->volR = eng.adjustToGroup(vp.right, group);
        }
    }
    bool tick() override {
        for (auto &c : ch) if (!c.v->dead) return false;
        return true;
    }
    void stop() override { for (auto &c : ch) c.v->dead = true; }
    void setPause(bool p) override { paused = p; for (auto &c : ch) c.v->paused = p; }
    void setVolPan(s32 v, s32 p) override {
        if (v != kVolumeDontChange) vol = v >= 0 ? v : -1024 * v / 127;
        if (p != kPanDontChange && p != kPanReset) pan = p;
        else if (p == kPanReset) pan = 0;
        applyVolume();
    }
    void refreshVolume() override { applyVolume(); }

    // " KPV": u32 bytes por canal, u32 inicio dos dados, u32 tamanho do par de blocos (todos os canais), u32 Hz, u32 canais,
    // {u16 vol, u16 pan} por canal.
    // "VAGp" (big-endian): mono, dados a partir de 0x30. Os canais intercalados sao separados aqui (sem flags de fim nos blocos).
    bool open() {
        u32 perCh, start, inter, rate, nch;
        if (file.size() >= 0x30 && std::memcmp(file.data(), " KPV", 4) == 0) {
            perCh = rd32(&file[4]); start = rd32(&file[8]); inter = rd32(&file[12]); rate = rd32(&file[16]); nch = rd32(&file[20]);
            if (nch < 1 || nch > 2 || inter == 0 || inter % (16 * nch)) return false;
            inter /= nch;   // o campo e o tamanho do par esquerdo+direito (0xA000): cada canal tem blocos de 0x5000
        } else if (file.size() >= 0x30 && std::memcmp(file.data(), "VAGp", 4) == 0) {
            auto be = [&](size_t o) { return (u32(file[o]) << 24) | (u32(file[o + 1]) << 16) | (u32(file[o + 2]) << 8) | file[o + 3]; };
            start = 0x30; nch = 1; rate = be(0x10);
            perCh = u32(file.size() - start); inter = perCh;
            if (rate == 0) rate = 44100;
        } else {
            return false;
        }
        ch.resize(nch);
        for (u32 c = 0; c < nch; ++c) {
            Ch &k = ch[c];
            if (nch == 2) { k.vol = std::min<s32>(rd16(&file[0x18 + 4 * c]), 127); k.pan = rd16(&file[0x1a + 4 * c]); }
            for (u32 o = 0; o < perCh; o += inter) {
                const size_t at = size_t(start) + (size_t(o / inter) * nch + c) * inter;
                if (at >= file.size()) break;
                const size_t n = std::min<size_t>({inter, perCh - o, file.size() - at});
                k.data.insert(k.data.end(), file.begin() + ptrdiff_t(at), file.begin() + ptrdiff_t(at + n));
            }
            k.v = std::make_shared<Voice>();
            k.v->envelope = false;
            k.v->src.data = k.data.data();
            k.v->src.size = k.data.size();
            k.v->src.honorFlags = false;
            k.v->pitch = u16(std::min<u32>(rate * 4096u / kRate, 0x3fff));
        }
        file.clear();
        file.shrink_to_fit();
        return true;
    }
};

// ---------------------------------------------------------------- Engine: criacao de sons

inline std::unique_ptr<BlockSound> Engine::makeBlock(const Bank &bank, int sound, s32 vol, s32 pan, s32 appVol, s32 appPan, s32 pm,
                                                     s32 pb, const std::array<s8, 4> *regs) {
    if (sound < 0 || sound >= int(bank.sounds.size())) return nullptr;
    auto b = std::make_unique<BlockSound>(*this, bank, bank.sounds[size_t(sound)]);
    if (regs) b->regs = *regs;
    b->start(vol, pan, appVol, appPan, pm, pb);
    return b;
}

// snd_PlaySoundVolPanPMPB: vol 0..0x400 (0x400 = volume do banco), pan em graus (-1 = o do banco), pm/pb = pitch mod/bend
inline u32 Engine::play(u32 bankHandle, int sound, s32 vol, s32 pan, s32 pm, s32 pb) {
    std::lock_guard<std::mutex> g(m_);
    auto it = banks_.find(bankHandle);
    if (it == banks_.end()) return 0;
    auto b = makeBlock(*it->second, sound, -1, -1, vol, pan, pm, pb, nullptr);
    if (!b) return 0;
    const u32 h = newHandle();
    b->handle = h;
    sounds_[h] = std::move(b);
    return h;
}

inline u32 Engine::playStream(u32 lsn, s32 vol, s32 pan, int group) {
    Reader rd;
    { std::lock_guard<std::mutex> g(m_); rd = reader_; }
    std::vector<u8> head(2048);
    if (!rd || !rd(lsn, 1, head.data())) return log("[snd] stream no LSN %u: leitura falhou\n", lsn), 0u;
    size_t size;
    if (std::memcmp(head.data(), " KPV", 4) == 0) size = size_t(rd32(&head[8])) + size_t(rd32(&head[4])) * rd32(&head[20]);
    else if (std::memcmp(head.data(), "VAGp", 4) == 0) size = 0x30 + ((size_t(head[12]) << 24) | (size_t(head[13]) << 16) | (size_t(head[14]) << 8) | head[15]);
    else return log("[snd] LSN %u nao e .VPK/.VAG\n", lsn), 0u;
    size = std::min<size_t>(size, 64u << 20);
    auto s = std::make_unique<Stream>(*this);
    s->file.resize((size + 2047) / 2048 * 2048);
    if (!rd(lsn, u32(s->file.size() / 2048), s->file.data()) || !s->open()) return log("[snd] stream no LSN %u invalido\n", lsn), 0u;
    std::lock_guard<std::mutex> g(m_);
    s->group = group;
    s->vol = vol;
    s->pan = pan;
    s->applyVolume();
    for (auto &c : s->ch) { c.v->keyOn(); voices_.push_back(c.v); }
    const u32 h = newHandle();
    s->handle = h;
    sounds_[h] = std::move(s);
    return h;
}

}  // namespace wotm::snd

// Saida pelo raylib: so quando o raylib ja foi incluido (o sndplay.cpp grava em arquivo sem ele)
#ifdef RAYLIB_H
namespace wotm::snd {
inline bool Engine::startOutput() {
    if (!IsAudioDeviceReady()) return false;
    static AudioStream stream{};
    SetAudioStreamBufferSizeDefault(1024);   // ~21 ms
    stream = LoadAudioStream(kRate, 16, 2);
    if (!IsAudioStreamValid(stream)) return false;
    SetAudioStreamCallback(stream, [](void *buf, unsigned int frames) { Engine::get().mix(static_cast<s16 *>(buf), frames); });
    PlayAudioStream(stream);
    return true;
}
}  // namespace wotm::snd
#endif
