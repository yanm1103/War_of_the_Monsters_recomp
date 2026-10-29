// Modo treino (NATIVE.md, "Runtime: modo treino"): estado compartilhado entre a thread do EE (ganchos em native/runtime/wotm_training.inc,
// incluido pelo wotm_overrides.cpp; captura dos volumes no wotmCaptureFrame do wotm_live.inc) e a janela (desenho no
// wotm_live.inc). Guia em docs/guides/NATIVE.md, "Modo treino"; investigacoes em git show 635fd3c:docs/investigacoes/2026-10-23/treino_*.md.
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace wotm_training
{
// opcoes (a janela escreve, a thread do EE le)
constexpr uint32_t kShowAttacks = 1u << 0;   // esferas testadas pelos golpes
constexpr uint32_t kShowHulls = 1u << 1;     // cascos de colisao do corpo (o que os golpes acertam)
constexpr uint32_t kShowCapsule = 1u << 2;   // capsula de empurrao monstro x monstro
constexpr uint32_t kRefill = 1u << 3;        // vida volta cheia depois de um tempo sem dano e ninguem morre
constexpr uint32_t kPanel = 1u << 4;         // painel de dano
constexpr uint32_t kShowInputs = 1u << 6;    // historico dos comandos (P1 e o boneco)
constexpr uint32_t kTrainHud = 1u << 7;      // HUD do boneco a direita e os numeros por cima das barras (P1 e boneco)
constexpr uint32_t kMoveTable = 1u << 8;     // tabela dos socos do P1 (configs do StatePunch) e a esfera antes da janela do golpe
constexpr uint32_t kDefault = kShowAttacks | kShowHulls | kRefill | kPanel | kShowInputs | kTrainHud | kMoveTable;

// Celulas do HUD do jogador 1 em unidades da tela do GS (640 x 224, faixa 4:3), medidas num print (investigacao 2026-10-23,
// treino_hud.md): o do boneco e o espelho (x' = 640 - x), como o jogo desenha o do jogador 2
namespace hud
{
struct Box
{
    float x0, y0, x1, y1;
};
constexpr Box kPortrait{17.0f, 10.0f, 61.7f, 30.6f};
constexpr Box kHealth{65.3f, 15.1f, 272.0f, 20.5f};
constexpr Box kSpecial{69.7f, 24.7f, 83.0f, 31.1f};
constexpr Box kStamina{18.0f, 32.7f, 29.0f, 88.2f};
constexpr Box mirror(const Box &b) { return Box{640.f - b.x1, b.y0, 640.f - b.x0, b.y1}; }
}

// Boneco (as IAs): parado (pad desligado), CPU (a IA do jogo), sempre bloqueia, bloqueia depois do 1o acerto (ate se recuperar e
// ficar 1 s sem apanhar). Nos dois de bloqueio a IA nao roda: o gancho do Ai::updateInputs segura o R2 do GamePad.
// Pula: X apertado em ciclos (agachar nao existe no jogo).
enum Dummy : int { DStand, DCpu, DBlock, DBlockAfter, DJump, DCount };
constexpr uint32_t kJumpPeriod = 50, kJumpHold = 4;   // quadros do ciclo do boneco que pula e quantos com o X apertado

inline std::atomic<bool> g_on{false};                 // WOTM_TRAINING=1, ou a luta que comecou com o treino armado
// Armado pela linha TRAINING da tela 2 (wotm_mainmenu.inc): o fluxo pula as telas 12 e 15 (wotm_padsel.inc, changeScreenHook), o
// rtMain(first) da luta liga o g_on (wotm_overrides.cpp) e o menu principal desarma e desliga (menuDisarm)
inline std::atomic<bool> g_menuArmed{false};
inline void menuDisarm(const char *where)
{
    if (!g_menuArmed.exchange(false))
        return;
    g_on = false;
    std::fprintf(stderr, "[treino] modo treino desarmado (%s)\n", where);
}
inline std::atomic<uint32_t> g_opts{kDefault};
inline std::atomic<int> g_dummy{DStand};
inline std::atomic<int> g_dummyHealth{100};           // % da vida maxima a que a recarga leva o boneco
// Medidores, em todos os monstros: desligado, stamina cheia, furia (stamina no maxLevel); nos dois ligados o especial fica liberado
enum Meters : int { MOff, MFull, MRage, MCount };
inline std::atomic<int> g_meters{MFull};
inline std::atomic<bool> g_resetStats{false};

// Dificuldade da CPU (TheGame +0x1203CC, 0 facil, 1 medio, 2 dificil; a IA a le a cada quadro): -1 = a do jogo. Escrita na luta pelo
// gancho do Monster::update (applyDifficulty); a do jogo e guardada e volta quando a opcao volta a -1, no "sim" do ARE YOU SURE e no
// menu principal (restoreDifficulty), para nao ir para o save.
constexpr uint32_t kDifficulty = 0x1203CCu;
inline std::atomic<int> g_cpuDiff{-1};
inline std::atomic<int> g_diffSaved{-1};
inline void restoreDifficulty(uint8_t *rdram, uint32_t addr)
{
    const int s = g_diffSaved.exchange(-1);
    if (s >= 0)
    {
        std::memcpy(rdram + addr, &s, 4);
        std::fprintf(stderr, "[treino] dificuldade do jogo de volta (%d)\n", s);
    }
}
inline void applyDifficulty(uint8_t *rdram, uint32_t addr)
{
    const int d = g_cpuDiff.load(std::memory_order_relaxed);
    if (d < 0)
    {
        restoreDifficulty(rdram, addr);
        return;
    }
    if (g_diffSaved.load(std::memory_order_relaxed) < 0)
    {
        int cur;
        std::memcpy(&cur, rdram + addr, 4);
        g_diffSaved = cur >= 0 && cur <= 2 ? cur : 1;
    }
    std::memcpy(rdram + addr, &d, 4);
}

// Atlas da fonte do jogo (tex0), do ultimo fontSetTexId (wotm_live.inc): o painel escreve com ele, ja que a luta do treino nao desenha
// texto do jogo (o tex0 do ultimo quadro com texto e o do menu ate o primeiro pause, e as janelas saiam vazias)
inline std::atomic<uint64_t> g_fontTex0{0};

// Pular a intro da luta (linha SKIP INTRO do painel): o rtMain(first) arma g_introMs; o gancho do Monster::updateCinema
// (wotm_training.inc) faz o caminho do START
inline std::atomic<bool> g_skipIntro{true};
inline std::atomic<int64_t> g_introMs{-1};

// Janelas do treino (painel, comandos, esferas e caixas) so com a luta rodando: o rtMain liga (wotm_overrides.cpp), o "sim" do ARE YOU
// SURE (QUIT ou RESET POSITIONS, wotm_pausemenu.inc) desliga na hora, e a pausa (displayDialog chamado ha pouco) esconde
inline std::atomic<bool> g_inFight{false};
inline std::atomic<int64_t> g_dialogMs{-100000};
inline int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline bool windowsHidden() { return !g_inFight.load(std::memory_order_relaxed) || nowMs() - g_dialogMs.load(std::memory_order_relaxed) < 200; }

// Tipo do teste de golpe que gravou a esfera (ou o cone). KFire: a linha do FireBreath::TestCollis (hdLosCSTest); KBody: as esferas
// do corpo na investida e no arremessado (hdBundledSphereTest do collisTestEnvironmentCollisions); KStomp e KGrab: a area de busca
// da vitima (getClosestMonsterToPunch no pisao, getClosestMonsterInFOV no agarrao) e a marca na vitima quando o golpe tira vida;
// KCone: o cone do StateSonicRoar::testConeCollis; KHook: a linha do gancho do Ultra-V (StateGrappleHook::testCollis, hdSphereTest
// de raio 0 do ponto anterior ao atual da ponta)
enum Kind : uint8_t { KPunch, KBat, KGetup, KWeapon, KShock, KHook, KOther, KFire, KBody, KStomp, KGrab, KCone, KKinds };

// Uma esfera varrida (de a ate b, raio r) que o jogo testou num quadro; a == b na esfera parada
struct Sweep
{
    float a[3], b[3];
    float r;
    uint32_t frame;   // g_frame
    uint8_t kind;
    int8_t owner;     // slot em m_monsters (-1 desconhecido)
    uint8_t hit;      // 0 nada, 1 cenario, 2 objeto ou monstro
};

// Um cone testado num quadro: vertice, eixo (unitario), alcance, cosseno do meio-angulo. flat: setor no plano perpendicular a `up`
// (o pisao: a busca do getClosestMonsterToPunch e no plano do chao)
struct Cone
{
    float apex[3], axis[3], up[3];
    float len, cosHalf;
    uint32_t frame;
    uint8_t kind;
    int8_t owner;
    uint8_t hit;   // 2: o teste tirou vida ou achou a vitima
    bool flat;
};

// Um dano aplicado (Monster::takeDamage): vida antes e depois
struct Hit
{
    uint32_t frame;
    int8_t victim, attacker;   // slots (-1 desconhecido)
    float base;                // dano pedido ao takeHit (antes do bloqueio e dos modificadores); < 0 sem takeHit (fogo, queda...)
    float applied;             // vida antes - depois
    float before, max;
    int source;                // HitEvent::source do takeHit (-1 sem)
};

// Um volume do corpo em mundo: segmentos (pares de pontos) de um casco, ou uma capsula
struct Hull
{
    int8_t owner;
    std::vector<float> seg;   // x0 y0 z0 x1 y1 z1 ...
};
struct Capsule
{
    int8_t owner;
    float a[3], b[3], r;
};

// Dados de quadro medidos (g_frame): um golpe do atacante (um estado de ataque, ou um passo do combo do StatePunch)
struct MoveRec
{
    int8_t owner;
    const char *name;     // nome do estado (tabela do game.h)
    int8_t chain, step;   // StatePunch/BatSwipe: cadeia e passo (configs do .CHR); -1 nos outros
    uint32_t start, end;  // quadros do estado (end exclusivo)
    int startup;          // quadro do primeiro teste de golpe, contando o primeiro do estado como 1 (0 = nao testou)
    int active;           // quadros com teste de golpe
    int8_t hitVictim;     // slot da primeira vitima com dano (-1 nenhuma)
};
// Uma reacao da vitima: quadros sem agir (recuo, atordoado, arremessado...)
struct StunRec
{
    int8_t victim;
    const char *name;
    uint32_t start, end;
    bool blocked = false;   // recuo de um golpe defendido (StateRecoil +0x64, o isBlocking do recuo)
};

// Combo: os golpes que uma vitima levou sem se recuperar (cada um chegou com ela ainda numa reacao, ou ate 3 quadros depois do outro)
struct Combo
{
    int8_t victim = -1, attacker = -1;
    int hits = 0;
    float damage = 0.f;
    uint32_t start = 0, last = 0;
};

// Comandos: uma entrada do historico (direcao e botoes iguais por `frames` quadros)
constexpr uint16_t kBtnL1 = 1, kBtnL2 = 2, kBtnR1 = 4, kBtnR2 = 8, kBtnX = 16, kBtnCircle = 32, kBtnTriangle = 64, kBtnSquare = 128;
struct PadRec
{
    uint8_t dir;      // notacao do teclado numerico, relativa ao monstro: 8 frente, 2 tras, 4 esquerda, 6 direita, 5 neutro
    uint16_t btn;     // kBtn*
    uint32_t frames;
};

// Tabela dos socos: uma PunchConfig do StatePunch (Monster +0x8470 +0x20 + pos*0x420 + tipo*0xB0; os valores do RST/<MONSTRO>.CHR,
// chave 0xFCC, que o tools/chr_dump.py tambem le). pos = posicao no combo (StatePunch +0x2988), tipo = qual golpe (+0x298C).
// Com um objeto na mao, uma SwipeConfig do StateBatSwipe (Monster +0xAE90 +0x20 + pos*0x480 + tipo*0xC0; pos +0x2D48, tipo +0x2D4C):
// o mesmo comeco de layout, e o dano e um multiplicador do dano do objeto (weapon = true, dmg = o multiplicador)
struct PunchCfg
{
    bool valid = false;
    bool weapon = false;         // SwipeConfig: dmg multiplica o dano do objeto (+0x90 com bastao, +0xB0 nos outros)
    int anim = -1;               // MonsterAnim (+0x00)
    float ini = 0.f, fim = 0.f;  // janela do teste de golpe, fracao da animacao (+0x0C, +0x10)
    float dmg = 0.f;             // dano pedido (+0x90)
    float radius = 0.f;          // raio da esfera do punho (+0x74; 0 = o padrao, StatePunch +0x2980)
    float stun = 0.f;            // atordoamento (+0x6C; > 100 atordoa)
    float kb = 0.f;              // forca de knockback (+0x64; != 0 derruba)
    int loc[2] = {-1, -1};       // localizadores testados (+0x7C, +0x80; < 0x7D0 nenhum; a SwipeConfig so tem o +0x7C)
};
// O soco em curso de um monstro, na saida do Monster::update: config, ponto da animacao e onde estao os localizadores agora
struct PunchLive
{
    uint32_t frame = 0;          // g_frame da leitura (0 = nunca)
    int8_t tier = -1, type = -1;
    float pct = -1.f;            // animationGetCurrentPercent da animacao do golpe
    bool inWindow = false;       // StatePunch +0x2A00 (inCollisionWindow; StateBatSwipe +0x2DC0)
    uint8_t nloc = 0;
    float loc[2][3] = {};
    PunchCfg cfg;
};
constexpr int kPunchTiers = 10, kPunchTypes = 6;
// nome de cada tipo (passo) de soco/golpe com objeto, na ordem das configs do .CHR
inline const char *const kMoveTypeNames[kPunchTypes] = {"HEAVY", "FORWARD + HEAVY", "BACK + HEAVY", "FORWARD + LIGHT", "BACK + LIGHT", "LIGHT"};

// Lista de golpes (MOVE LIST da pausa) do monstro do P1, montada das configs na RAM: os 6 socos da primeira posicao, as sequencias
// do mesmo botao (o proximo tipo e permitido pelo float +0x94 + tipo*4 da config e a casa seguinte existe) com o dano somado, e os 6
// golpes com objeto. id: "P<tipo>", "C<tipo>x<n>", "W<tipo>" (o que o arquivo de favoritos guarda)
// MkOther: os outros golpes (butt slam, investida, agarrao, levantar, crowd control, arremesso, especiais), com nome, dica pronta e o
// offset do estado no Monster (a linha acende quando o P1 esta nele); numeros lidos da RAM nos offsets de
// 80b3802:docs/investigacoes/2026-10-25/move_list_comuns.md e move_list_especiais.md
enum MoveKind : uint8_t { MkPunch, MkCombo, MkWeapon, MkOther };
struct Move
{
    uint8_t kind = MkPunch;
    int8_t type = -1;            // tipo do PadFlags (0 triangulo ... 5 quadrado), o botao de cada golpe da sequencia
    uint8_t len = 1;             // golpes na sequencia
    char id[12] = {};
    float dmg = 0.f;             // soma (combo); multiplicador do dano do objeto (objeto)
    float ini = 0.f, fim = 0.f;  // janela do ultimo golpe
    float radius = 0.f, stun = 0.f, kb = 0.f;   // do ultimo golpe
    uint32_t state = 0;          // MkOther: offset do estado no Monster
    char name[16] = {};          // MkOther: nome curto (caixa in-game e painel)
    char hint[200] = {};         // MkOther: comando e numeros (dica do painel)
    uint8_t btns = 0;            // MkOther: botoes do comando (bits kBtn*; os icones da linha do painel)
    int8_t dir = 5;              // MkOther: direcao do comando (teclado numerico: 5 nenhuma, 8 frente/cima, 2 tras)
    int8_t variant = 0;          // MkOther: 0 acende pelo estado; 1 so com a animacao do estado (+0x50) != 0xBA (golpe normal do agarrao), 2 so == 0xBA (especial)
};
// direcao e botao de cada tipo de soco (as linhas da tabela in-game e do painel): 1 e 3 sao frente + botao, 2 e 4 tras + botao
inline int moveTypeDir(int type) { return type == 1 || type == 3 ? 8 : type == 2 || type == 4 ? 2 : 5; }
inline uint8_t moveTypeBtn(int type) { return type <= 2 ? uint8_t(kBtnTriangle) : uint8_t(kBtnSquare); }
constexpr int kFavMax = 8;

struct Shared
{
    std::vector<Move> moveList;  // a lista do monstro do P1 (movesType = m_typeBits >> 5; -1 = ainda nao montada)
    int movesType = -1;
    std::vector<std::string> fav;   // ids favoritos do monstro movesType (ate kFavMax); a caixa in-game mostra estes
    bool favDirty = false;          // o painel mudou os favoritos: o wotm_training.inc grava o arquivo
    uint32_t p1State = 0;           // offset do estado do P1 no Monster (acende a linha MkOther da caixa)
    uint32_t p1StateAnim = 0;       // a palavra +0x50 do estado do P1 (a animacao, nos golpes do agarrao: Move::variant)
    std::mutex m;
    PunchLive live[16];          // o soco em curso por slot (frame antigo = nao esta socando)
    PunchCfg table[kPunchTypes]; // os 6 tipos da posicao do combo em que o P1 esta (0 fora do soco)
    int8_t tableTier = 0;
    bool tableWeapon = false;    // a tabela e a dos golpes com objeto (o P1 segura um; StateBatSwipe)
    std::vector<Sweep> sweeps;   // acumuladas; a janela descarta as velhas
    std::vector<Cone> cones;     // idem
    std::vector<Hit> hits;       // idem
    std::vector<MoveRec> moves;  // ultimos golpes terminados
    std::vector<StunRec> stuns;  // ultimas reacoes terminadas
    std::vector<Hull> hulls;     // do ultimo quadro capturado
    std::vector<Capsule> caps;
    uint32_t frame = 0;          // g_frame da ultima captura
    float health[16] = {}, maxHealth[16] = {};
    int8_t players = 1;          // m_numPlayers (slots 0..players-1 sao jogadores)
    Combo combo;                 // o ultimo (ainda aberto ou terminado)
    float bestCombo = 0.f;       // maior dano de combo desde o ultimo "limpar"
    int bestHits = 0;
    std::vector<PadRec> pad[2];  // historico dos comandos: [0] P1, [1] o primeiro boneco (CPU1); o mais novo no fim
    float stamina[16] = {}, staminaMax[16] = {};
    bool hudShown = false;       // o HUD do jogo esta na tela (gHudEnable, sem letterbox): os numeros das barras saem
};
inline Shared &shared()
{
    static Shared s;
    return s;
}
}
