// wotm_mainmenu.inc: menus de itens do telao em texto (incluido por wotm_live.inc, depois do wotm_padsel.inc, de quem usa o desenho
// no telao e a fonte do jogo). Telas:
//   1 menu principal: single player, multiplayer (o "2 player"), options, recompiled options (nosso), load game, unlocks, quit game
//     (nosso: fecha o jogo na hora, sem confirmacao);
//   2 modos de 1 jogador: adventure, free-for-all, endurance, training (nosso: o free-for-all do jogo com o modo treino ligado,
//     wotm_training.h; outro modo ou a volta ao menu principal o desligam);
//   5 modos do multiplayer (depois da escolha de controles do wotm_padsel.inc): free-for-all, elimination, minigames.
//
// Os itens do jogo (shell->m_menuItemSw[tela][1..n]) sao quadros com a palavra em imagem. Aqui todos os objetos dos switches ficam com
// alfa 0 a cada quadro da tela (e no fade de saida dela) e a lista e desenhada com as letras das palavras do jogo (wotm_menufont.inc; sem o disco, a fonte da lista de saves) numa
// textura propria, posta no plano dos itens, do topo do primeiro ao pe do ultimo.
// O cursor continua sendo o currentSelection[tela] do jogo: o gancho do screenGetInput consome cima/baixo e move o currentSelection;
// o X e o triangulo seguem para a funcao da tela, que faz o que o item faz, como no retail. Na tela 1 a linha "recompiled options"
// fica fora do currentSelection (g_extraRow, como o "quit game"); o X nela abre o painel de opcoes do port no lugar da lista, sem trocar de tela, e o
// triangulo (ou O) fecha. Com o painel aberto (e 20 quadros depois de fechar) o jogo nao ve entrada nenhuma: o painel le os
// dispositivos direto (wotm_padsel::readDevice), na thread do EE, dentro do gancho do screenGetInput.
// Painel: abas VIDEO / FEATURES / CAMERA / CONTROLS (L1/R1), cima/baixo escolhem a linha, esquerda/direita/X
// trocam o valor, que vale na hora e e gravado no settings.ini (wotm_settings.hpp; o video e aplicado na thread da janela pelo
// applyVideoSettings do wotm_live.inc). Desenhado numa textura propria no telao inteiro (a faixa visivel abaixo da tarja do menu).
// Aba CONTROLS (rola): teclado separado, qual mapa a lista mostra (teclado ou gamepads), uma linha por botao do PS2 (X espera a tecla
// ou o botao novo, Esc ou 5 s cancelam; quadrado limpa) e a volta ao padrao (mapa em wotm_settings.hpp, lido pelo wotm_pad.inc).
// Logo do jogo no telao (os dois objetos irmaos do m_movieScreen): alfa 1 so na tela de "press start" (20), 0 nas outras, seguindo o
// targetAlpha nos fades de/para a 20 (logoFrame, a cada quadro antes da coleta da cena). Sem ele a lista fica centrada no telao.
// Tela 5: enquanto a escolha de controles esta no telao a lista fica escondida, e aparece junto com o fade que a escolha da aos itens.
// WOTM_TEXTMENU=0: itens do retail (e o rotulo "multiplayer" do wotm_padsel.inc).
#include "wotm_training.h"   // linha "training" da tela 2 (g_on)
// tela de opcoes do jogo (wotm_optscreen.inc, incluido depois): chamadas dos ganchos daqui
namespace wotm_optscreen
{
static void frame(uint8_t *rdram);
static uint32_t input(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t code);
constexpr uint32_t kScreen = 10;
}
namespace wotm_pausemenu   // wotm_pausemenu.inc (incluido depois)
{
static void install(PS2Runtime &runtime);
}

namespace wotm_mainmenu
{
using wotm_padsel::rd32;
constexpr uint32_t kShell = wotm::sym::shellInfo;
constexpr int kMaxRows = 7;
constexpr int kOptionsRow = -1, kQuitRow = -2, kTrainingRow = -3;   // itens que nao sao do jogo (item < 0)
constexpr int kFreeForAll = 2;   // tela 2: o item do jogo que o "training" escolhe

struct Row { int item; const char *label; };
struct ScreenDef
{
    int screen;
    uint32_t numSel;   // numSelectables da tela (os itens 1..n do jogo); 0 = todos
    int rows;
    Row row[kMaxRows];
};
constexpr ScreenDef kScreens[] = {
    {1, wotm::sym::numSelectables_2784, 7,
     {{1, "SINGLE PLAYER"}, {2, "MULTIPLAYER"}, {3, "OPTIONS"}, {kOptionsRow, "RECOMPILED OPTIONS"}, {4, "LOAD GAME"}, {5, "UNLOCKS"},
      {kQuitRow, "QUIT GAME"}}},
    {2, 0, 4, {{1, "ADVENTURE"}, {2, "FREE-FOR-ALL"}, {3, "ENDURANCE"}, {kTrainingRow, "TRAINING"}}},
    {5, wotm::sym::numSelectables_2857, 3, {{1, "FREE-FOR-ALL"}, {2, "ELIMINATION"}, {3, "MINIGAMES"}}},
};
static const ScreenDef *screenDef(uint32_t s)
{
    for (const ScreenDef &d : kScreens)
        if (uint32_t(d.screen) == s) return &d;
    return nullptr;
}

struct View
{
    bool on = false;
    int screen = 0;
    float alpha = 0.f;     // da tela (fades de entrada e saida)
    float panel = 0.f;     // 0 lista, 1 painel aberto
    int sel = 0;
    int rows = 0;
    Row row[kMaxRows] = {};
    uint32_t itemObj[kMaxRows][4] = {};   // objetos do filho em uso de cada item do jogo, na ordem dos itens; posicao da lista
    int items = 0;
    int tab = 0, opt = 0;                 // painel: aba e linha
    int top = 0;                          // painel: primeira linha visivel (aba CONTROLS rola)
    bool preview = true;                  // painel: previa da aba VIDEO visivel (quadrado / P)
    int bindMap = -1;                     // aba CONTROLS: mapa na lista (-1 teclado, 0..3 gamepads do jogador 1..4)
    int capture = -1;                     // botao do PS2 esperando a tecla ou o botao novo (-1: nenhum)
    uint32_t screenObj = 0;               // telao (objeto do filme, shell->m_movieScreen): onde o painel e desenhado
    bool training = false;                // pausa: o painel do modo treino (TRAINING OPTIONS) no lugar das abas
    bool moves = false;                   // pausa: a lista de golpes (MOVE LIST), tambem sem abas
};
static std::mutex g_m;
static View g_view;
// O que esta sob o mouse (etapa 9; janela escreve a cada quadro desenhado, o gancho da tela le), protegido por g_m
enum HoverPart { HNone, HTab, HPrevTab, HNextTab, HRow, HRowLeft, HRowRight };
struct Hover
{
    int screen = -1, row = -1;   // lista: tela e linha (indice em View::row)
    int part = HNone, tab = -1, prow = -1;   // painel: parte, aba (HTab) e linha (HRow*)
};
static Hover g_hover;
// Painel na pausa da luta (wotm_pausemenu.inc): a View publicada pela thread do EE e o que esta sob o mouse no painel em 2D. A parte
// da janela grava o g_pauseHover, nao o g_hover (o drawOnTelao grava este vazio a cada quadro da luta). Protegidos por g_m.
static View g_pauseView;
static Hover g_pauseHover;

// Thread do EE
static const bool g_enabled = [] { const char *e = std::getenv("WOTM_TEXTMENU"); return !(e && e[0] == '0'); }();
static int g_extraRow = 0;         // telas 1 e 2: cursor num item nosso (kOptionsRow, kQuitRow, kTrainingRow) ou 0 (no currentSelection)
static bool hasExtra(int screen) { return screen == 1 || screen == 2; }
static bool g_open = false;        // painel "recompiled options" aberto
static float g_panel = 0.f;
static int g_suppress = 0;         // quadros sem entrada do jogo depois de fechar o painel (o triangulo ainda apertado)
// O mesmo painel no menu de pausa da luta: estado proprio para abrir, animar e esperar soltar; aba, linha, mapa e captura sao os
// mesmos do menu (os dois nunca estao abertos juntos, e o painel lembra a ultima aba)
enum Where { InMenu, InPause };
static Where g_where = InMenu;     // de quem e o quadro do panelInput de agora (sons, mouse, trava do KEYBOARD)
static bool g_pauseOpen = false;
static float g_pausePanel = 0.f;
static int g_pauseSuppress = 0;

// ------------------------------------------------------------------ painel "recompiled options": abas e opcoes
// Cada opcao e uma chave do wotm_settings.hpp; mudar grava o settings.ini e vale na hora (video na thread da janela, mods e
// teclado na proxima leitura deles).
namespace st = wotm::settings;
enum Tab { TabVideo, TabFeatures, TabCamera, TabControls, kTabs };
constexpr const char *kTabNames[kTabs] = {"VIDEO", "FEATURES", "CAMERA", "CONTROLS"};
constexpr int kMaxOpts = 9;
struct Opt { st::Key key; const char *label; const char *hint; };
struct TabDef { int n; Opt opt[kMaxOpts]; };
constexpr TabDef kTabDefs[kTabs] = {
    {9,
     {{st::Res, "RESOLUTION", "HEIGHT OF THE PICTURE IN PIXELS. WINDOW FOLLOWS THE WINDOW OR SCREEN"},
      {st::RenderScale, "RENDER SCALE", "DRAWS BIGGER OR SMALLER, THEN FITS THE SCREEN. ABOVE 1X SMOOTHS EDGES"},
      {st::Aspect, "ASPECT RATIO", "DYNAMIC MATCHES THE SHAPE OF THE WINDOW"},
      {st::Display, "DISPLAY MODE", "WINDOW OR FULLSCREEN. F11 SWITCHES AT ANY TIME"},
      {st::Rate, "REFRESH RATE", "HOW OFTEN THE PICTURE IS DRAWN. THE GAME ITSELF ALWAYS RUNS AT 60"},
      {st::Vsync, "VSYNC", "WAITS FOR THE MONITOR BEFORE EACH FRAME, SO THE PICTURE NEVER TEARS"},
      {st::Interp, "INTERPOLATE FRAMES", "SMOOTHER MOTION ABOVE 60 HZ OR WHEN THE GAME DROPS TO 30. MAY ADD DELAY"},
      {st::Msaa, "ANTI-ALIASING", "MSAA SMOOTHS THE JAGGED EDGES OF THE 3D MODELS"},
      {st::TexFilter, "TEXTURE FILTER", "KEEPS TEXTURES SHARP FAR AWAY AND AT STEEP ANGLES"}}},
    {5,
     {{st::LockToggle, "LOCK-ON TOGGLE", "TAP R1 TO LOCK ON AND AGAIN TO LET GO. THE D-PAD PICKS ANOTHER TARGET"},
      {st::EnemyBars, "ENEMY BARS", "SHOWS THE HEALTH AND ENERGY OF EACH ENEMY ABOVE THEIR HEAD"},
      {st::DamagePop, "DAMAGE NUMBERS", "SHOWS HOW MUCH DAMAGE EACH HIT DOES"},
      {st::Announcer, "NARRATOR", "A VOICE CALLS OUT THE MONSTER AND THE LEVEL YOU PICK, OR ONLY THE MONSTER ROARS"},
      {st::SkipIntro, "SKIP INTRO MOVIES", "GOES STRAIGHT TO THE TITLE SCREEN. CHANGES TAKE EFFECT ON NEXT BOOT"}}},
    {5,
     {{st::Camera, "CAMERA STYLE", "MODERN TURNS WITH THE RIGHT STICK. CLASSIC IS THE CAMERA OF THE ORIGINAL GAME"},
      {st::CamSens, "SENSITIVITY", "HOW FAST THE RIGHT STICK TURNS THE CAMERA"},
      {st::CamInvertY, "INVERT Y", "FLIPS UP AND DOWN ON THE RIGHT STICK"},
      {st::CamRecenter, "AUTO CENTER", "WHILE YOU WALK, THE CAMERA SLOWLY SWINGS BACK BEHIND YOUR MONSTER"},
      {st::CloseCam, "CLOSE-UP CAMERA", "WHEN TWO MONSTERS FIGHT UP CLOSE, THE GAME CAN SWITCH TO A SHARED VIEW"}}},
    {4,
     {{st::KbSeparate, "KEYBOARD", "SHARE PLAYER 1 WITH THE FIRST GAMEPAD, OR PLAY AS A PLAYER OF ITS OWN"},
      {st::Prompts, "BUTTON ICONS", "AUTO SHOWS THE BUTTONS OF WHATEVER YOU PLAYED WITH LAST"},
      {st::MenuConfirm, "MENU CONFIRM", "WHICH GAMEPAD BUTTON ACCEPTS IN THE MENUS"},
      {st::Vibration, "VIBRATION", "THE GAMEPAD SHAKES WITH HITS AND CRASHES. XINPUT GAMEPADS ONLY"}}},   // e o mapa: ctlRows
};
static int g_tab = TabVideo;
static int g_optSel[kTabs] = {};
constexpr int kVisRows = 7;   // linhas do painel que cabem; a aba CONTROLS rola
// aba VIDEO: linhas mais baixas e juntas, as 9 ao lado da previa da opcao (panelLayout, wotm_preview.hpp)
static int visRows(int tab) { return tab == TabVideo ? 9 : kVisRows; }

// Aba CONTROLS: as opcoes da aba (teclado separado, icones, confirmar nos menus, vibracao), qual mapa a lista mostra (teclado ou gamepads), um
// botao do PS2 por linha e a volta ao padrao
constexpr int kCtlOpt = -10, kCtlDevice = -2, kCtlReset = -3;   // kCtlOpt - i: opcao i da aba
constexpr int kMaxCtlRows = kMaxOpts + 2 + st::kBinds;
constexpr const char *kBindLabels[st::kBinds] = {"D-PAD UP", "D-PAD DOWN", "D-PAD LEFT", "D-PAD RIGHT", "CROSS",     "CIRCLE",     "SQUARE",
                                                 "TRIANGLE", "L1",         "R1",         "L2",          "R2",        "L1 AND R1",  "START",
                                                 "SELECT",   "L3",         "R3",         "STICK UP",    "STICK DOWN", "STICK LEFT", "STICK RIGHT",
                                                 "LOCK-ON",  "BLOCK",      "STRAFE LEFT", "STRAFE RIGHT", "NEXT TARGET", "PREVIOUS TARGET",
                                                 "TRIANGLE AND CIRCLE", "CROSS AND SQUARE", "WALK"};
static int ctlRows(bool kb, int out[kMaxCtlRows])
{
    int n = 0;
    for (int i = 0; i < kTabDefs[TabControls].n; ++i) out[n++] = kCtlOpt - i;
    out[n++] = kCtlDevice;
    for (int b = 0; b < st::kBinds; ++b)
        if (kb || st::bindOnPad(st::Bind(b))) out[n++] = b;
    out[n++] = kCtlReset;
    return n;
}
static int g_bindMap = -1;   // -1 teclado, 0..3 gamepads do jogador 1..4 (um mapa por jogador)
static constexpr const char *kMapNames[st::kPadPlayers + 1] = {"KEYBOARD", "PLAYER 1 PAD", "PLAYER 2 PAD", "PLAYER 3 PAD", "PLAYER 4 PAD"};
static std::string mapPrefix(int map) { return map < 0 ? std::string("KB_") : st::padPrefix(map); }
static int g_capture = -1;   // botao do PS2 esperando a tecla/botao novo
static double g_capT0 = 0.0;
static bool g_capKeyPrev[std::size(st::kKeyNames)] = {};
static bool g_capPadPrev[4][st::kPadButtons] = {};
static int g_top[kTabs] = {};

// Painel do modo treino (item TRAINING OPTIONS da pausa, wotm_pausemenu.inc): as opcoes sao os atomicos do wotm_training.h (nao vao
// para o settings.ini), escritos so aqui, na thread do EE; os ganchos do treino e o desenho os leem
namespace tr = wotm_training;
enum TrainRow { TrSpheres, TrHulls, TrCapsules, TrDummy, TrDifficulty, TrHealth, TrMeters, TrInputs, TrPanel, TrMoves, TrClear, kTrainRows };
struct TrainDef { const char *label, *hint; };
constexpr TrainDef kTrainDefs[kTrainRows] = {
    {"ATTACK SPHERES", "SHOWS WHERE EACH ATTACK CHECKS FOR A HIT. RED WHEN IT CONNECTS"},
    {"BODY BOXES", "SHOWS THE PARTS OF EACH BODY THAT ATTACKS CAN HIT"},
    {"PUSH CAPSULES", "SHOWS THE SHAPES THAT KEEP MONSTERS FROM WALKING INTO EACH OTHER"},
    {"DUMMY", "WHAT THE OPPONENT DOES"},
    {"CPU DIFFICULTY", "HOW WELL THE CPU FIGHTS WHEN THE DUMMY IS SET TO CPU. GAME SETTING USES THE OPTIONS MENU"},
    {"DUMMY HEALTH", "HEALTH THE DUMMY GOES BACK TO AFTER A MOMENT WITHOUT DAMAGE. NO REFILL LETS ANYONE LOSE"},
    {"STAMINA AND SPECIAL", "CHANGE THE STAMINA OF EVERY MONSTER. FULL OR IN RAGE, WITH THE SPECIAL ATTACK READY"},
    {"INPUT DISPLAY", "DISPLAYS BUTTONS PRESSED BY YOU OR THE CPU, AND FOR HOW MANY FRAMES"},
    {"DAMAGE PANEL", "SHOWS DAMAGE, COMBOS AND FRAME DATA FOR EACH ATTACK"},
    {"PUNCH TABLE", "WINDOW, DAMAGE, RADIUS AND STUN OF ATTACKS. A FAINT SPHERE SHOWS WHERE THE NEXT HIT CHECKS"},
    {"CLEAR DAMAGE LOG", "EMPTIES THE DAMAGE LIST AND THE BEST COMBO"},
};
constexpr int kTrainHealth[] = {100, 75, 50, 25, 10};   // DUMMY HEALTH: 0 sem recarga, 1..5 estes %
static int g_trainSel = 0, g_trainTop = 0;
static bool g_pauseTraining = false;   // o painel aberto (ou fechando) e o do treino
// MOVE LIST da pausa: a lista do wotm_training::Shared (montada na thread do EE pelo wotm_training.inc), uma linha por golpe; o X
// favorita (ate kFavMax, por monstro, gravado pelo wotm_training.inc), e a caixa in-game passa a mostrar os favoritos
static bool g_pauseMoves = false;
static int g_moveSel = 0, g_moveTop = 0;
constexpr int kMoveVis = 11;   // linhas visiveis (o layout do treino)
static std::string moveLabel(const tr::Move &m)
{
    const char *t = m.type >= 0 && m.type < tr::kPunchTypes ? tr::kMoveTypeNames[m.type] : "?";
    if (m.kind == tr::MkOther) return m.name;
    if (m.kind == tr::MkCombo) return std::string("COMBO: ") + t + " X " + std::to_string(int(m.len));
    if (m.kind == tr::MkWeapon) return std::string("WEAPON: ") + t;
    return t;
}
static std::string moveHint(const tr::Move &m)
{
    char b[160];
    const char *eff = m.stun > 100.f ? ", STUNS" : m.kb != 0.f ? ", KNOCKS DOWN" : "";
    if (m.kind == tr::MkOther) return m.hint;
    if (m.kind == tr::MkCombo)
        std::snprintf(b, sizeof b, "%d HITS, %.0f DAMAGE IN TOTAL. LAST HIT: WINDOW %.0f-%.0f, RADIUS %.0f%s", int(m.len), m.dmg, m.ini * 100.f, m.fim * 100.f, m.radius, eff);
    else if (m.kind == tr::MkWeapon)
        std::snprintf(b, sizeof b, "X%.1f THE OBJECT DAMAGE. WINDOW %.0f-%.0f, RADIUS %.0f, STUN %.0f%s", m.dmg, m.ini * 100.f, m.fim * 100.f, m.radius, m.stun, eff);
    else
        std::snprintf(b, sizeof b, "DAMAGE %.0f. WINDOW %.0f-%.0f OF THE ANIMATION, RADIUS %.0f, STUN %.0f%s", m.dmg, m.ini * 100.f, m.fim * 100.f, m.radius, m.stun, eff);
    return b;
}
static bool isFav(const tr::Shared &sh, const tr::Move &m) { return std::find(sh.fav.begin(), sh.fav.end(), std::string(m.id)) != sh.fav.end(); }
// X na linha: entra ou sai dos favoritos (cheio: nao entra). Devolve 1 entrou, -1 saiu, 0 nada
static int toggleFav(int row)
{
    tr::Shared &sh = tr::shared();
    std::lock_guard<std::mutex> lk(sh.m);
    if (row < 0 || row >= int(sh.moveList.size())) return 0;
    const std::string id = sh.moveList[row].id;
    const auto it = std::find(sh.fav.begin(), sh.fav.end(), id);
    if (it != sh.fav.end()) { sh.fav.erase(it); sh.favDirty = true; return -1; }
    if (int(sh.fav.size()) >= tr::kFavMax) return 0;
    sh.fav.push_back(id);
    sh.favDirty = true;
    return 1;
}
static int moveRows()
{
    tr::Shared &sh = tr::shared();
    std::lock_guard<std::mutex> lk(sh.m);
    return int(sh.moveList.size());
}
static int trainHealthIndex()
{
    if (!(tr::g_opts.load() & tr::kRefill)) return 0;
    for (int i = 0; i < 5; ++i)
        if (kTrainHealth[i] == tr::g_dummyHealth.load()) return i + 1;
    return 1;
}
// Valor em texto (a fonte do jogo nao tem o sinal de porcento)
static std::string trainValue(int r)
{
    const uint32_t o = tr::g_opts.load();
    auto onOff = [&](uint32_t bit) { return std::string((o & bit) ? "ON" : "OFF"); };
    static const char *const kDummy[tr::DCount] = {"STAND STILL", "CPU", "ALWAYS BLOCK", "BLOCK AFTER FIRST HIT", "JUMP"};
    static const char *const kMeters[tr::MCount] = {"NORMAL", "ALWAYS FULL", "ALWAYS RAGE"};
    switch (r)
    {
    case TrSpheres: return onOff(tr::kShowAttacks);
    case TrHulls: return onOff(tr::kShowHulls);
    case TrCapsules: return onOff(tr::kShowCapsule);
    case TrDummy: return kDummy[std::clamp(tr::g_dummy.load(), 0, tr::DCount - 1)];
    case TrDifficulty: { static const char *const kDiff[4] = {"GAME SETTING", "EASY", "MEDIUM", "HARD"}; return kDiff[std::clamp(tr::g_cpuDiff.load() + 1, 0, 3)]; }
    case TrHealth: { const int i = trainHealthIndex(); return i ? std::to_string(kTrainHealth[i - 1]) + " PERCENT" : std::string("NO REFILL"); }
    case TrMeters: return kMeters[std::clamp(tr::g_meters.load(), 0, tr::MCount - 1)];
    case TrInputs: return onOff(tr::kShowInputs);
    case TrPanel: return onOff(tr::kPanel);
    case TrMoves: return onOff(tr::kMoveTable);
    default: return std::string();
    }
}
// Esquerda/direita (dir -1/+1) param nas pontas; X (wrap) da a volta, como o stepValue. Devolve se mudou.
static bool trainStep(int r, int dir, bool wrap)
{
    auto step = [&](int cur, int n) {
        const int v = cur + dir;
        return wrap ? (v + n) % n : std::clamp(v, 0, n - 1);
    };
    auto flag = [&](uint32_t bit) {
        const int cur = (tr::g_opts.load() & bit) ? 1 : 0, v = step(cur, 2);
        if (v == cur) return false;
        if (v) tr::g_opts.fetch_or(bit);
        else tr::g_opts.fetch_and(~bit);
        return true;
    };
    switch (r)
    {
    case TrSpheres: return flag(tr::kShowAttacks);
    case TrHulls: return flag(tr::kShowHulls);
    case TrCapsules: return flag(tr::kShowCapsule);
    case TrInputs: return flag(tr::kShowInputs);
    case TrPanel: return flag(tr::kPanel);
    case TrMoves: return flag(tr::kMoveTable);
    case TrDummy:
    {
        const int cur = std::clamp(tr::g_dummy.load(), 0, tr::DCount - 1), v = step(cur, tr::DCount);
        tr::g_dummy = v;
        return v != cur;
    }
    case TrDifficulty:
    {
        const int cur = std::clamp(tr::g_cpuDiff.load() + 1, 0, 3), v = step(cur, 4);
        tr::g_cpuDiff = v - 1;
        return v != cur;
    }
    case TrMeters:
    {
        const int cur = std::clamp(tr::g_meters.load(), 0, tr::MCount - 1), v = step(cur, tr::MCount);
        tr::g_meters = v;
        return v != cur;
    }
    case TrHealth:
    {
        const int cur = trainHealthIndex(), v = step(cur, 6);
        if (v == cur) return false;
        if (v == 0) tr::g_opts.fetch_and(~tr::kRefill);
        else
        {
            tr::g_dummyHealth = kTrainHealth[v - 1];
            tr::g_opts.fetch_or(tr::kRefill);
        }
        return true;
    }
    case TrClear:
        if (!wrap) return false;   // so o X
        tr::g_resetStats = true;
        return true;
    default: return false;
    }
}

static int tabRows(int tab)
{
    int r[kMaxCtlRows];
    return tab == TabControls ? ctlRows(g_bindMap < 0, r) : kTabDefs[tab].n;
}
static std::string bindValue(int b, int map)
{
    std::string s;
    if (map < 0)
    {
        const uint32_t v = st::kb(st::Bind(b));
        for (int i = 0; i < 2; ++i)
            if (const char *n = st::keyName(int((v >> (16 * i)) & 0xFFFF)))
                s += (s.empty() ? "" : " OR ") + std::string(n);   // a fonte do jogo nao tem "/"
        std::replace(s.begin(), s.end(), '_', ' ');   // MOUSE_LEFT, WHEEL_UP
    }
    else if (const int p = st::pad(st::Bind(b), map); p > 0 && p < st::kPadButtons)
    {
        s = st::kPadButtonNames[p];
        std::replace(s.begin(), s.end(), '_', ' ');
    }
    return s.empty() ? "NONE" : s;
}

// Valores que a opcao percorre, em ordem crescente
static int choices(st::Key k, int out[16])
{
    switch (k)
    {
    case st::Res: std::copy(std::begin(st::kResolutions), std::end(st::kResolutions), out); return int(std::size(st::kResolutions));
    case st::Rate: std::copy(std::begin(st::kRates), std::end(st::kRates), out); return int(std::size(st::kRates));
    case st::RenderScale: std::copy(std::begin(st::kRenderScales), std::end(st::kRenderScales), out); return int(std::size(st::kRenderScales));
    case st::Msaa: std::copy(std::begin(st::kMsaaLevels), std::end(st::kMsaaLevels), out); return int(std::size(st::kMsaaLevels));
    case st::TexFilter: std::copy(std::begin(st::kTexFilters), std::end(st::kTexFilters), out); return int(std::size(st::kTexFilters));
    case st::Aspect: for (int i = 0; i < st::kAspects; ++i) out[i] = i; return st::kAspects;
    case st::Display: for (int i = 0; i < 3; ++i) out[i] = i; return 3;
    case st::CamSens: for (int i = 0; i < 10; ++i) out[i] = i + 1; return 10;
    case st::Announcer: for (int i = 0; i < 3; ++i) out[i] = i; return 3;
    case st::Prompts:   // sem o PLAYSTATION: igual ao ORIGINAL (o AUTO ainda o usa com gamepad de PlayStation)
    {
        int n = 0;
        for (int i = 0; i < st::kPromptStyles; ++i)
            if (i != st::PromptPs) out[n++] = i;
        return n;
    }
    default: out[0] = 0; out[1] = 1; return 2;
    }
}
// Proximo valor no sentido dir (+1/-1); dois valores dao a volta, listas param nas pontas (wrap forca a volta). Um valor fora da
// lista (variavel de ambiente) vai ao vizinho da lista naquele sentido.
static int stepValue(st::Key k, int cur, int dir, bool wrap)
{
    int c[16];
    const int n = choices(k, c);
    int i = -1;
    for (int j = 0; j < n; ++j)
        if (c[j] == cur) i = j;
    if (i < 0)
    {
        if (dir > 0) { for (int j = 0; j < n; ++j) if (c[j] > cur) return c[j]; return c[n - 1]; }
        for (int j = n - 1; j >= 0; --j) if (c[j] < cur) return c[j];
        return c[0];
    }
    i += dir;
    if (n == 2 || wrap) i = (i + n) % n;
    return c[std::clamp(i, 0, n - 1)];
}
static void valueText(st::Key k, int v, char *buf, size_t sz)
{
    static const char *const kDisplay[3] = {"WINDOWED", "BORDERLESS", "EXCLUSIVE"};
    static const char *const kAspectUi[st::kAspects] = {"DYNAMIC", "4:3", "16:9", "16:10", "21:9", "32:9"};
    switch (k)
    {
    case st::Res: if (v > 0) std::snprintf(buf, sz, "%dP", v); else std::snprintf(buf, sz, "WINDOW"); break;
    case st::RenderScale:   // a fonte do jogo nao tem %: 1.5X
    {
        char num[16];
        std::snprintf(num, sizeof num, "%.2f", v / 100.0);
        size_t n = std::strlen(num);
        while (n && num[n - 1] == '0') num[--n] = 0;
        if (n && num[n - 1] == '.') num[--n] = 0;
        std::snprintf(buf, sz, "%sX", num);
        break;
    }
    case st::Msaa: if (v > 0) std::snprintf(buf, sz, "MSAA %dX", v); else std::snprintf(buf, sz, "OFF"); break;
    case st::TexFilter:
        if (v <= 0) std::snprintf(buf, sz, "BILINEAR");
        else if (v == 1) std::snprintf(buf, sz, "TRILINEAR");
        else std::snprintf(buf, sz, "%dX ANISO", v);
        break;
    case st::Aspect: std::snprintf(buf, sz, "%s", kAspectUi[v > 0 && v < st::kAspects ? v : 0]); break;
    case st::Display: std::snprintf(buf, sz, "%s", kDisplay[std::clamp(v, 0, 2)]); break;
    case st::Rate: if (v > 0) std::snprintf(buf, sz, "%d HZ", v); else std::snprintf(buf, sz, "MONITOR"); break;
    case st::KbSeparate: std::snprintf(buf, sz, "%s", v ? "OWN PLAYER" : "SAME AS PAD 1"); break;
    case st::Prompts:
    {
        static const char *const kNames[st::kPromptStyles] = {"AUTO", "ORIGINAL", "KEYBOARD", "XBOX", "PLAYSTATION", "NINTENDO"};
        std::snprintf(buf, sz, "%s", kNames[std::clamp(v, 0, st::kPromptStyles - 1)]);
        break;
    }
    case st::MenuConfirm: std::snprintf(buf, sz, "%s", v ? "RIGHT BUTTON" : "BOTTOM BUTTON"); break;
    case st::Camera: std::snprintf(buf, sz, "%s", v ? "MODERN" : "CLASSIC"); break;
    case st::CamSens: std::snprintf(buf, sz, "%d", v); break;
    case st::Announcer:   // a voz do narrador (narrator/) ou o grito do monstro escolhido
        std::snprintf(buf, sz, "%s", v == 2 ? "MONSTER CRY" : v ? "GODZILLA SAVE THE EARTH" : "OFF");
        break;
    default: std::snprintf(buf, sz, "%s", v ? "ON" : "OFF"); break;
    }
}

// Entrada do painel: direto dos dispositivos (todos), como a escolha de controles do wotm_padsel.inc, pelos botoes do PS2 do mapa
// de teclas e botoes, como o resto do jogo (direcional ou analogico, X ok, triangulo ou bola volta, L1/R1 abas, quadrado limpa; nos
// gamepads com a troca do MenuConfirm). Botao do PS2 sem tecla ou botao no mapa (limpo na aba CONTROLS) vale pelo padrao, para o
// painel nunca ficar sem saida. O roteiro de teste WOTM_PADSEL_SCRIPT vale aqui tambem (acoes l1 e r1 para as abas).
enum PanelAct { PUp, PDown, PLeft, PRight, POk, PBack, PPrev, PNext, PClear, PPreview, kPanelActs };
// Previa da aba VIDEO (wotm_preview.hpp): quadrado no controle, P no teclado (o quadrado do teclado e o clique esquerdo, que escolhe
// a linha). Volta a ligada toda vez que o painel abre.
static bool g_showPreview = true;
static bool g_panelPrev[kPanelActs] = {};
static bool panelKey(st::Bind b)
{
    // so teclas: o mouse no painel e pelo que esta sob ele (panelInput), nao pelo mapa (o esquerdo e o quadrado, que limparia)
    const uint32_t v = st::kb(b) ? st::kb(b) : st::kDefaultKb[b];
    const int a = int(v & 0xFFFF), c = int(v >> 16);
    return (a < st::kMouseBase && wotm_pad::inputDown(a)) || (c < st::kMouseBase && wotm_pad::inputDown(c));
}
static bool panelPad(int d, st::Bind b)
{
    const st::Bind src = wotm_pad::sourceBind(b);
    const int btn = st::pad(src, wotm_pad::playerOfPad(d));
    return IsGamepadButtonDown(d, btn > 0 ? btn : st::kDefaultPad[src]);
}
static void readPanel(bool out[kPanelActs])
{
    using namespace wotm_padsel;
    std::fill(out, out + kPanelActs, false);
    for (int d = 0; d < kDevs; ++d)
    {
        if (!present(d)) continue;
        bool a[kPanelActs] = {};
        if (d == kKb)
        {
            a[PUp] = panelKey(st::BUp) || panelKey(st::BStickUp) || wotm_pad::g_wheelHeld > 0;   // rodinha do mouse tambem
            a[PDown] = panelKey(st::BDown) || panelKey(st::BStickDown) || wotm_pad::g_wheelHeld < 0;
            a[PLeft] = panelKey(st::BLeft) || panelKey(st::BStickLeft);
            a[PRight] = panelKey(st::BRight) || panelKey(st::BStickRight);
            // fixas tambem (layout 2 do teclado, sem tecla no L1/R1): Enter confirma, Esc e Backspace voltam, Q/E e Tab (Shift+Tab
            // volta) trocam a aba, Delete limpa
            const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT), tab = IsKeyDown(KEY_TAB);
            a[POk] = panelKey(st::BCross) || IsKeyDown(KEY_ENTER) || IsKeyDown(KEY_KP_ENTER);
            a[PBack] = panelKey(st::BTriangle) || panelKey(st::BCircle) || IsKeyDown(KEY_ESCAPE) || IsKeyDown(KEY_BACKSPACE);
            a[PPrev] = panelKey(st::BL1) || IsKeyDown(KEY_Q) || (tab && shift);
            a[PNext] = panelKey(st::BR1) || IsKeyDown(KEY_E) || (tab && !shift);
            a[PClear] = panelKey(st::BSquare) || IsKeyDown(KEY_DELETE);
            a[PPreview] = IsKeyDown(KEY_P);
        }
        else if (IsGamepadAvailable(d))
        {
            const float lx = GetGamepadAxisMovement(d, GAMEPAD_AXIS_LEFT_X), ly = GetGamepadAxisMovement(d, GAMEPAD_AXIS_LEFT_Y);
            a[PUp] = panelPad(d, st::BUp) || ly < -0.6f;
            a[PDown] = panelPad(d, st::BDown) || ly > 0.6f;
            a[PLeft] = panelPad(d, st::BLeft) || lx < -0.6f;
            a[PRight] = panelPad(d, st::BRight) || lx > 0.6f;
            a[POk] = panelPad(d, st::BCross);
            a[PBack] = panelPad(d, st::BTriangle) || panelPad(d, st::BCircle);
            a[PPrev] = panelPad(d, st::BL1);
            a[PNext] = panelPad(d, st::BR1);
            a[PClear] = panelPad(d, st::BSquare);
            a[PPreview] = a[PClear];
        }
        // roteiro de teste (acoes do wotm_padsel.inc)
        const double now = GetTime();
        static constexpr int kFromScript[kActions] = {PLeft, PRight, POk, PBack, -1, PUp, PDown, PPrev, PNext, PPreview};
        for (const ScriptStep &sc : g_script)
            if (sc.dev == d && now >= sc.t0 && now < sc.t0 + sc.dur && kFromScript[sc.act] >= 0) a[kFromScript[sc.act]] = true;
        for (int k = 0; k < kPanelActs; ++k) out[k] |= a[k];
    }
}
static void openPanel()
{
    g_open = true;
    g_showPreview = true;
    readPanel(g_panelPrev);   // o X que abriu ainda esta apertado: so conta o proximo
}
// Sons do painel: os do Shell (2/4 cursor, 6 voltar, 8 ok), no menu e na pausa (decisao do dono). Na luta o jogo nao tem o banco do
// Shell (gSoundBanks[14], Shell_s.bnk): o som nativo carrega uma copia so para o painel (wotm_snd_hooks::panelShellBank)
constexpr uint32_t kShellBank = wotm::sym::gSoundBanks + 4u * 14u;
static void panelSfx(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, int id)
{
    if (g_where == InPause && !rd32(rdram, kShellBank))
    {
#ifdef WOTM_HAVE_SND
        wotm_snd_hooks::playPanelSfx(id);
#endif
        return;
    }
    wotm_padsel::sfx(rdram, ctx, runtime, id);
}
// X no RECOMPILED OPTIONS da pausa (wotm_pausemenu.inc, saida do checkInputForExit)
static void openPausePanel(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, bool training = false, bool moves = false)
{
    g_where = InPause;
    g_pauseOpen = true;
    g_pauseTraining = training;
    g_pauseMoves = moves;
    if (moves)
        g_moveSel = std::clamp(g_moveSel, 0, std::max(0, moveRows() - 1));
    g_pauseSuppress = 0;
    g_showPreview = true;
    readPanel(g_panelPrev);
    wotm::mouse::takeClick(wotm::mouse::g_clickL);   // o clique que abriu nao conta no painel
    wotm::mouse::g_crossReq.store(0, std::memory_order_relaxed);
    std::fprintf(stderr, "[pausa] painel aberto (jogador %u; banco de sons do Shell %s)\n", rd32(rdram, wotm::sym::gPlayerThatPaused) + 1,
                 rd32(rdram, kShellBank) ? "do jogo" : "fora da luta: copia do som nativo");
    panelSfx(rdram, ctx, runtime, 8);
}
// Espera da tecla (teclado) ou do botao (gamepads) novo do botao do PS2 g_capture: vale a primeira que passar a estar apertada.
// Esc ou 5 s sem nada cancelam. No fim, o que esta apertado agora nao conta como entrada do painel.
static void captureInput(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const st::Bind b = st::Bind(g_capture);
    const char *what = nullptr;
    bool done = false;
    if (g_bindMap < 0)
    {
        for (size_t i = 0; i < std::size(st::kKeyNames); ++i)
        {
            const bool down = wotm_pad::inputDown(st::kKeyNames[i].code);
            if (down && !g_capKeyPrev[i] && !done && st::kKeyNames[i].code != KEY_ESCAPE)   // Esc cancela
            {
                st::bindKey(b, st::kKeyNames[i].code);
                what = st::kKeyNames[i].name;
                done = true;
            }
            g_capKeyPrev[i] = down;
        }
    }
    else
    {
        for (int p = 0; p < 4; ++p)
            for (int k = 1; k < st::kPadButtons; ++k)
            {
                const bool down = IsGamepadAvailable(p) && IsGamepadButtonDown(p, k);
                if (down && !g_capPadPrev[p][k] && !done)
                {
                    st::bindPad(b, k, g_bindMap);   // qualquer gamepad serve: quem esta configurando
                    what = st::kPadButtonNames[k];
                    done = true;
                }
                g_capPadPrev[p][k] = down;
            }
    }
    const bool cancel = !done && (IsKeyDown(KEY_ESCAPE) || GetTime() - g_capT0 > 5.0);
    if (!done && !cancel)
        return;
    if (done)
        std::fprintf(stderr, "[settings] %s%s = %s\n", mapPrefix(g_bindMap).c_str(), st::kBindNames[b], what);
    g_capture = -1;
    wotm::mouse::takeClick(wotm::mouse::g_clickL);   // o clique que virou a tecla nova nao conta no painel
    wotm::mouse::takeClick(wotm::mouse::g_clickR);
    readPanel(g_panelPrev);
    panelSfx(rdram, ctx, runtime, done ? 8 : 6);
}

// Aba CONTROLS, da segunda linha em diante: mapa (teclado/gamepads), botoes do PS2 (X: espera a tecla nova; quadrado/Delete: limpa) e
// volta ao padrao
static void controlsInput(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, const bool e[kPanelActs])
{
    int r[kMaxCtlRows];
    const int n = ctlRows(g_bindMap < 0, r);
    int &sel = g_optSel[TabControls];
    const int row = r[std::clamp(sel, 0, n - 1)];
    if (row == kCtlDevice)
    {
        if (!(e[PLeft] || e[PRight] || e[POk])) return;
        g_bindMap = (g_bindMap + 1 + (e[PLeft] ? st::kPadPlayers : 1)) % (st::kPadPlayers + 1) - 1;
        sel = std::min(sel, ctlRows(g_bindMap < 0, r) - 1);
        panelSfx(rdram, ctx, runtime, e[POk] ? 8 : (e[PLeft] ? 2 : 4));
    }
    else if (row == kCtlReset)
    {
        if (!e[POk]) return;
        st::resetBinds(g_bindMap);
        std::fprintf(stderr, "[settings] mapa %s de volta ao padrao\n", kMapNames[g_bindMap + 1]);
        panelSfx(rdram, ctx, runtime, 8);
    }
    else if (row >= 0 && e[POk])
    {
        g_capture = row;
        g_capT0 = GetTime();
        for (size_t i = 0; i < std::size(st::kKeyNames); ++i) g_capKeyPrev[i] = wotm_pad::inputDown(st::kKeyNames[i].code);
        for (int p = 0; p < 4; ++p)
            for (int k = 1; k < st::kPadButtons; ++k) g_capPadPrev[p][k] = IsGamepadAvailable(p) && IsGamepadButtonDown(p, k);
        panelSfx(rdram, ctx, runtime, 8);
    }
    else if (row >= 0 && e[PClear])
    {
        st::clearBind(st::Bind(row), g_bindMap);
        std::fprintf(stderr, "[settings] %s%s = (nada)\n", mapPrefix(g_bindMap).c_str(), st::kBindNames[row]);
        panelSfx(rdram, ctx, runtime, 6);
    }
}

// KEYBOARD (teclado separado) na pausa: travado. Numa luta de 1 jogador, OWN PLAYER mandaria o teclado para o jogador 2, que nao
// existe, e quem joga no teclado perderia o monstro e a saida da pausa
static bool lockedOpt(st::Key k, bool inPause) { return inPause && k == st::KbSeparate; }

// Um quadro do painel aberto (no menu: screenGetInput da tela 1, que o jogo chama a cada quadro dela; na pausa: no lugar do
// pauseLevelOverlay). Devolve true quando o painel fecha (quem chamou fecha o seu: g_open ou g_pauseOpen).
static bool panelInput(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    bool now[kPanelActs], e[kPanelActs];
    readPanel(now);
    for (int i = 0; i < kPanelActs; ++i)
        e[i] = now[i] && !g_panelPrev[i];
    std::copy(now, now + kPanelActs, g_panelPrev);
    if (g_capture >= 0)
    {
        captureInput(rdram, ctx, runtime);
        return false;
    }
    // painel do treino (pausa): as linhas do kTrainDefs, sem abas
    const bool training = g_where == InPause && g_pauseTraining;
    const bool moves = g_where == InPause && g_pauseMoves;
    const int nMoves = moves ? moveRows() : 0;
    int &selRef = moves ? g_moveSel : training ? g_trainSel : g_optSel[g_tab];
    const int selRows = moves ? nMoves : training ? int(kTrainRows) : tabRows(g_tab);
    // mouse (etapa 9): passar por cima escolhe a linha; clique esquerdo na aba troca, em L1/R1 anda uma aba, na linha confirma (nas
    // setas do valor, diminui ou aumenta); clique direito volta. A rodinha vem como cima/baixo (readPanel).
    {
        static uint32_t s_seq = 0;
        const uint32_t seq = wotm::mouse::g_moveSeq.load(std::memory_order_relaxed);
        const bool moved = seq != s_seq;
        s_seq = seq;
        Hover hv;
        {
            std::lock_guard<std::mutex> lk(g_m);
            hv = g_where == InPause ? g_pauseHover : g_hover;
        }
        const bool click = wotm::mouse::takeClick(wotm::mouse::g_clickL);
        e[PBack] |= wotm::mouse::takeClick(wotm::mouse::g_clickR);
        const bool onRow = hv.part == HRow || hv.part == HRowLeft || hv.part == HRowRight;
        if (onRow && (moved || click) && hv.prow >= 0 && hv.prow < selRows && hv.prow != selRef)
        {
            selRef = hv.prow;
            if (!click)
                panelSfx(rdram, ctx, runtime, 4);
        }
        if (click && !training && !moves && hv.part == HTab && hv.tab >= 0 && hv.tab != g_tab)
        {
            g_tab = hv.tab;
            panelSfx(rdram, ctx, runtime, 4);
            return false;
        }
        if (click)
        {
            e[PPrev] |= hv.part == HPrevTab;
            e[PNext] |= hv.part == HNextTab;
            e[PLeft] |= hv.part == HRowLeft;
            e[PRight] |= hv.part == HRowRight;
            e[POk] |= hv.part == HRow;
        }
    }
    if (e[PBack])
    {
        panelSfx(rdram, ctx, runtime, 6);
        return true;
    }
    if (moves)
    {
        if (nMoves <= 0)
            return false;
        if (e[PUp] || e[PDown])
        {
            g_moveSel = (g_moveSel + (e[PDown] ? 1 : nMoves - 1)) % nMoves;
            if (g_moveSel < g_moveTop) g_moveTop = g_moveSel;
            if (g_moveSel >= g_moveTop + kMoveVis) g_moveTop = g_moveSel - kMoveVis + 1;
            panelSfx(rdram, ctx, runtime, e[PDown] ? 4 : 2);
        }
        else if (e[POk])
        {
            const int r = toggleFav(g_moveSel);
            std::fprintf(stderr, "[treino] move list: linha %d %s\n", g_moveSel, r > 0 ? "favoritada" : r < 0 ? "tirada dos favoritos" : "sem mudanca (favoritos cheios)");
            panelSfx(rdram, ctx, runtime, r ? 8 : 6);
        }
        return false;
    }
    if (training)
    {
        if (e[PUp] || e[PDown])
        {
            g_trainSel = (g_trainSel + (e[PDown] ? 1 : kTrainRows - 1)) % kTrainRows;
            const int vis = kTrainRows;   // panelLayout(.., training): as 11 cabem
            if (g_trainSel < g_trainTop) g_trainTop = g_trainSel;
            if (g_trainSel >= g_trainTop + vis) g_trainTop = g_trainSel - vis + 1;
            panelSfx(rdram, ctx, runtime, e[PDown] ? 4 : 2);
        }
        else if ((e[PLeft] || e[PRight] || e[POk]) && trainStep(g_trainSel, e[PLeft] ? -1 : 1, e[POk]))
        {
            std::fprintf(stderr, "[treino] %s = %s\n", kTrainDefs[g_trainSel].label, trainValue(g_trainSel).c_str());
            panelSfx(rdram, ctx, runtime, e[POk] ? 8 : (e[PLeft] ? 2 : 4));
        }
        return false;
    }
    if (e[PPrev] || e[PNext])
    {
        g_tab = (g_tab + (e[PNext] ? 1 : kTabs - 1)) % kTabs;
        panelSfx(rdram, ctx, runtime, e[PNext] ? 4 : 2);
        return false;
    }
    if (g_tab == TabVideo && e[PPreview])
    {
        g_showPreview = !g_showPreview;
        panelSfx(rdram, ctx, runtime, g_showPreview ? 4 : 2);
        return false;
    }
    const TabDef &t = kTabDefs[g_tab];
    int &sel = g_optSel[g_tab];
    const int n = tabRows(g_tab);
    if (e[PUp] || e[PDown])
    {
        sel = (sel + (e[PDown] ? 1 : n - 1)) % n;
        int &top = g_top[g_tab];
        if (sel < top) top = sel;
        if (sel >= top + visRows(g_tab)) top = sel - visRows(g_tab) + 1;
        panelSfx(rdram, ctx, runtime, e[PDown] ? 4 : 2);
        return false;
    }
    if (g_tab == TabControls && sel >= t.n)
    {
        controlsInput(rdram, ctx, runtime, e);
        return false;
    }
    if (e[PLeft] || e[PRight] || e[POk])
    {
        const st::Key k = t.opt[sel].key;
        if (lockedOpt(k, g_where == InPause))
            return false;
        const int cur = st::get(k), v = stepValue(k, cur, e[PLeft] ? -1 : 1, e[POk]);
        if (v == cur)
            return false;
        st::set(k, v);
        if (k == st::Vibration) wotm_rumble::poll();   // desligada: os motores param na hora
        char buf[32];
        valueText(k, v, buf, sizeof buf);
        std::fprintf(stderr, "[settings] %s = %s (%s=%s)\n", t.opt[sel].label, buf, st::kEnv[k], st::format(k, v).c_str());
        panelSfx(rdram, ctx, runtime, e[POk] ? 8 : (e[PLeft] ? 2 : 4));
    }
    return false;
}

// ------------------------------------------------------------------ painel na pausa da luta (thread do EE)
// Publica a View da pausa: o painel em 2D no centro do quadro (drawPausePanel2d), sem lista nem telao
static void publishPause()
{
    View v;
    v.on = g_pausePanel > 0.f;
    v.screen = -1;   // pausa (renderPanel: legenda translucida, KEYBOARD travado)
    v.alpha = 1.f;
    v.panel = g_pausePanel;
    v.tab = g_tab;
    v.opt = g_optSel[g_tab];
    v.top = g_top[g_tab];
    v.bindMap = g_bindMap;
    v.capture = g_capture;
    v.preview = g_showPreview;
    v.training = g_pauseTraining;
    v.moves = g_pauseMoves;
    if (v.training)
    {
        v.opt = g_trainSel;
        v.top = g_trainTop;
    }
    if (v.moves)
    {
        v.opt = g_moveSel;
        v.top = g_moveTop;
    }
    std::lock_guard<std::mutex> lk(g_m);
    g_pauseView = v;
}
// Entrada do pauseLevelOverlay, a cada quadro do laco do displayDialog (wotm_pausemenu.inc). Com o painel aberto ele roda aqui e a
// original e pulada (true): o jogo nao le o pad nem desenha a lista da pausa. Fechado ha pouco, a entrada do jogo fica bloqueada
// (pauseBlocked) ate soltarem o que fechou: Esc e Enter sao Start no mapa do teclado, e o X e o triangulo confirmam na pausa.
static bool pauseFrame(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (g_pauseOpen)
    {
        g_where = InPause;
        wotm_pad::markMenuFrame();   // troca do MENU CONFIRM e legenda como no menu; o modo foto (F9) fica de fora
        // a caixa 3D da pausa (shell->m_cs610, posta na frente da camera pelo overlay) sai de tras do painel; ao fechar, o overlay
        // a religa sozinho (hierSetCsDrawMe(cs, gDrawOverlay) a cada quadro)
        if (const uint32_t cs = rd32(rdram, kShell + 0x610))
            wotm_padsel::callGuest(rdram, ctx, runtime, wotm::fn::hierSetCsDrawMe__FP3_csUc, cs, 0u);
        if (panelInput(rdram, ctx, runtime))
        {
            g_pauseOpen = false;
            g_pauseSuppress = 20;
            std::fprintf(stderr, "[pausa] painel fechado\n");
        }
    }
    else if (g_pauseSuppress > 0)
    {
        bool now[kPanelActs];
        readPanel(now);
        const bool held = std::any_of(now, now + kPanelActs, [](bool b) { return b; });
        if (--g_pauseSuppress == 0 && held)
            g_pauseSuppress = 1;
    }
    g_pausePanel = std::clamp(g_pausePanel + (g_pauseOpen ? 0.1f : -0.1f), 0.f, 1.f);
    publishPause();
    return g_pauseOpen;
}
static bool pauseBlocked() { return g_pauseOpen || g_pauseSuppress > 0; }
// Saida do displayDialog: o painel nao deveria estar aberto (com ele o overlay nao sai), mas se estiver, fecha e some
static void pauseDialogEnd()
{
    if (g_pauseOpen)
        std::fprintf(stderr, "[pausa] aviso: a pausa terminou com o painel aberto; fechado\n");
    g_pauseOpen = false;
    g_pauseSuppress = 0;
    g_pausePanel = 0.f;
    g_where = InMenu;
    publishPause();
}
static PS2Runtime::RecompiledFunction g_origMain = nullptr, g_origModes1P = nullptr, g_origModes2P = nullptr, g_origTransition = nullptr,
                                      g_origGetInput = nullptr, g_origOptions = nullptr;
static uint32_t g_mainRa = 0, g_modes1Ra = 0, g_modes2Ra = 0, g_transRa = 0, g_inRa = 0, g_optionsRa = 0;
static bool g_mainPending = false, g_modes1Pending = false, g_modes2Pending = false, g_transPending = false, g_inPending = false,
            g_optionsPending = false;

static uint32_t itemSwitch(uint8_t *rdram, int screen, int item) { return rd32(rdram, kShell + 0x25C + uint32_t(screen * 10 + item) * 4); }
static uint32_t curSelAddr(int screen) { return wotm::sym::currentSelection + 4u * uint32_t(screen); }

// Linhas com os itens que o jogo oferece na tela (os itens 1..numSelectables)
static int buildRows(uint8_t *rdram, const ScreenDef &d, Row out[kMaxRows])
{
    int n = d.numSel ? static_cast<int>(rd32(rdram, d.numSel)) : d.rows;
    if (n < 1 || n > d.rows) n = d.rows;
    int rows = 0;
    for (int i = 0; i < d.rows; ++i)
        if (d.row[i].item < 0 || d.row[i].item <= n)
            out[rows++] = d.row[i];
    return rows;
}

// Linha do cursor: a do item em currentSelection[tela] (ou a do "recompiled options")
static int cursorRow(uint8_t *rdram, int screen, const Row *row, int rows)
{
    const int cs = static_cast<int>(rd32(rdram, curSelAddr(screen)));
    for (int r = 0; r < rows; ++r)
        if (hasExtra(screen) && g_extraRow ? row[r].item == g_extraRow : row[r].item == cs)
            return r;
    return 0;
}

// Tela cujos itens estao a mostra e o alfa deles: parada numa tela, ela com 1; nas transicoes, o targetAlpha do fade de entrada (a
// tela nova) ou de saida (a anterior)
static float screenAlpha(uint8_t *rdram, uint32_t &screen)
{
    const uint32_t cur = rd32(rdram, wotm::sym::currScreen), last = rd32(rdram, wotm::sym::lastScreen);
    screen = cur;
    if (!rd32(rdram, wotm::sym::betweenScreens))
        return 1.f;
    float t;
    std::memcpy(&t, rdram + wotm::sym::targetAlpha, 4);
    t = std::clamp(t, 0.f, 1.f);
    if (rd32(rdram, wotm::sym::fadingIn_2979)) return t;
    if (rd32(rdram, wotm::sym::fadingOut_2980) && last != cur) { screen = last; return t; }
    return 0.f;
}

// Tela 5: o que a escolha de controles deixa dos itens (escondidos durante ela, o fade dela depois)
static float padSelFactor()
{
    using namespace wotm_padsel;
    if (g_phase == Config || g_phase == Leaving) return 0.f;
    if (g_phase == Reveal) return std::clamp(g_reveal, 0.f, 1.f);
    return 1.f;
}

// ------------------------------------------------------------------ logo do telao so no "press start"
// O logo "War of the Monsters" do telao sao dois objetos (as duas metades), cada um num grupo de um filho; os dois grupos sao irmaos
// do objeto do filme (shell->m_movieScreen) num grupo de 3 filhos, alocado logo depois deles. Fora do "press start" (tela 20) o logo
// fica com alfa 0 e o telao inteiro e da lista e do painel; nas transicoes de/para a tela 20 ele acompanha o fade.
constexpr uint32_t kPressStart = 20;
static uint32_t g_logoTelao = 0;   // telao do ultimo achado (outro: procura de novo)
static uint32_t g_logoObj[4] = {};
static int g_logoN = 0;

static uint16_t rd16(const uint8_t *rdram, uint32_t a) { uint16_t v; std::memcpy(&v, rdram + (a & 0x1FFFFFFu), 2); return v; }

static void findLogo(uint8_t *rdram, uint32_t telao)
{
    g_logoTelao = telao;
    g_logoN = 0;
    for (uint32_t a = telao + 0x10; a < telao + 0x4000 && a + 0x40 < PS2_RAM_SIZE; a += 0x10)
    {
        if ((rdram[a] & 0x3F) != 1)   // GROUP_NODE: numKids (u16) +8, filhos +0x20
            continue;
        const uint32_t n = rd16(rdram, a + 8);
        if (n < 2 || n > 8)
            continue;
        bool has = false;
        for (uint32_t k = 0; k < n; ++k)
            has = has || (rd32(rdram, a + 0x20 + 4 * k) & 0x1FFFFFFFu) == telao;
        if (!has)
            continue;
        for (uint32_t k = 0; k < n; ++k)
        {
            const uint32_t c = rd32(rdram, a + 0x20 + 4 * k) & 0x1FFFFFFFu;
            if (c == telao || c < 0x1000u || c + 0x40 >= PS2_RAM_SIZE)
                continue;
            if ((rdram[c] & 0x3F) == 0 && g_logoN < 4)
                g_logoObj[g_logoN++] = c;
            else if ((rdram[c] & 0x3F) == 1)
                for (uint32_t g = 0, m = std::min<uint32_t>(rd16(rdram, c + 8), 4u); g < m; ++g)
                    if (const uint32_t o = rd32(rdram, c + 0x20 + 4 * g) & 0x1FFFFFFFu; o >= 0x1000u && (rdram[o] & 0x3F) == 0 && g_logoN < 4)
                        g_logoObj[g_logoN++] = o;
        }
        break;
    }
    std::fprintf(stderr, "[mainmenu] logo do telao: %d objetos%s\n", g_logoN, g_logoN ? " (some fora do press start)" : ", nao achado");
}

// A cada quadro do jogo (wotmCaptureFrame, antes da coleta da cena)
static void logoFrame(uint8_t *rdram)
{
    if (!g_enabled)
        return;
    const uint32_t telao = rd32(rdram, wotm_padsel::kMovieScreen) & 0x1FFFFFFFu;
    if (telao < 0x1000u || telao + 0x40 >= PS2_RAM_SIZE)
        return;
    if (telao != g_logoTelao)
        findLogo(rdram, telao);
    const uint32_t cur = rd32(rdram, wotm::sym::currScreen), last = rd32(rdram, wotm::sym::lastScreen);
    float a = cur == kPressStart ? 1.f : 0.f;
    if (rd32(rdram, wotm::sym::betweenScreens))
    {
        float t;
        std::memcpy(&t, rdram + wotm::sym::targetAlpha, 4);
        t = std::clamp(t, 0.f, 1.f);
        if (rd32(rdram, wotm::sym::fadingIn_2979))
            a = cur == kPressStart ? t : 0.f;
        else if (rd32(rdram, wotm::sym::fadingOut_2980))
            a = last == kPressStart && last != cur ? t : 0.f;
    }
    for (int i = 0; i < g_logoN; ++i)
        std::memcpy(rdram + g_logoObj[i] + 0x4C, &a, 4);
}

// A cada quadro (saida das funcoes das telas e do screenTransition): esconde os itens do jogo e publica a lista
static void frame(uint8_t *rdram)
{
    // esquema de controle fixo no tipo A (o teclado e o lock-on alternado supoem ele; a pausa nao oferece mais a troca,
    // wotm_pausemenu.inc): m_ctlrCfg (1 = A; 0 = ainda nao escolhido, o jogo poe 1)
    for (uint32_t i = 0; i < 2; ++i)
        if (const uint32_t a = kShell + 0x2B8C + 4 * i; rd32(rdram, a) > 1) wotm_padsel::wr32(rdram, a, 1);
    uint32_t screen = 0;
    float alpha = screenAlpha(rdram, screen);
    const ScreenDef *d = screenDef(screen);
    View v;
    if (d && screen == 5) alpha *= padSelFactor();
    if (d)
    {
        v.screen = d->screen;
        v.rows = buildRows(rdram, *d, v.row);
        v.sel = cursorRow(rdram, d->screen, v.row, v.rows);
        for (int item = 1; item <= d->rows; ++item)
        {
            // posicao: os objetos do filho em uso (so ele esta na cena)
            int k = 0;
            wotm_padsel::forSwitchObjects(rdram, itemSwitch(rdram, d->screen, item), [&](uint32_t o, bool used) {
                const float zero = 0.f;
                std::memcpy(rdram + o + 0x4C, &zero, 4);
                if (used && k < 4 && v.items < kMaxRows) v.itemObj[v.items][k++] = o;
            });
            if (k) ++v.items;
        }
        // objetos soltos da tela (shell->m_menuObj[tela][..]): o brilho laranja atras do item escolhido da lista antiga
        int nObj = 0;
        for (uint32_t i = 0; i < 90; ++i)
        {
            const uint32_t o = rd32(rdram, kShell + 0x6DC + (uint32_t(d->screen) * 90u + i) * 4u) & 0x1FFFFFFFu;
            if (o < 0x1000u || o + 0x50 >= PS2_RAM_SIZE) break;
            const float zero = 0.f;
            std::memcpy(rdram + o + 0x4C, &zero, 4);
            ++nObj;
        }
        static int loggedObj = 0;
        if (!(loggedObj & (1 << d->screen)))
        {
            loggedObj |= 1 << d->screen;
            std::fprintf(stderr, "[mainmenu] tela %d: %d objetos soltos escondidos (m_menuObj)\n", d->screen, nObj);
        }
    }
    v.on = d && alpha > 0.f;
    if (rd32(rdram, wotm::sym::currScreen) != 1)
        g_open = false;
    g_panel = std::clamp(g_panel + (g_open ? 0.1f : -0.1f), 0.f, 1.f);
    v.alpha = alpha;
    v.panel = v.screen == 1 ? g_panel : 0.f;
    v.tab = g_tab;
    v.opt = g_optSel[g_tab];
    v.top = g_top[g_tab];
    v.bindMap = g_bindMap;
    v.capture = g_capture;
    v.preview = g_showPreview;
    v.screenObj = rd32(rdram, wotm_padsel::kMovieScreen) & 0x1FFFFFFFu;
    std::lock_guard<std::mutex> lk(g_m);
    g_view = v;
}

// Saida de uma funcao do jogo -> frame (mesmo padrao para as telas e o screenTransition)
#define WOTM_MM_EXIT_HOOK(name, fn, orig, ra, pending)                                \
    static void name(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)          \
    {                                                                                 \
        if (ctx->pc == (fn))                                                          \
        {                                                                             \
            ra = GPR_U32(ctx, 31);                                                    \
            pending = true;                                                           \
        }                                                                             \
        orig(rdram, ctx, runtime);                                                    \
        if (!pending || ctx->pc != ra)                                                \
            return;                                                                   \
        pending = false;                                                              \
        frame(rdram);                                                                 \
        wotm_optscreen::frame(rdram);                                                 \
    }
WOTM_MM_EXIT_HOOK(mainHook, wotm::fn::screenMain__Fv, g_origMain, g_mainRa, g_mainPending)
WOTM_MM_EXIT_HOOK(optionsHook, wotm::fn::screenOptions__Fv, g_origOptions, g_optionsRa, g_optionsPending)
WOTM_MM_EXIT_HOOK(modes1Hook, wotm::fn::screenGameModes1P__Fv, g_origModes1P, g_modes1Ra, g_modes1Pending)
WOTM_MM_EXIT_HOOK(modes2Hook, wotm::fn::screenGameModes2P__Fv, g_origModes2P, g_modes2Ra, g_modes2Pending)
WOTM_MM_EXIT_HOOK(transitionHook, wotm::fn::screenTransition__Fb, g_origTransition, g_transRa, g_transPending)
#undef WOTM_MM_EXIT_HOOK

// Telas da lista: cima/baixo movem o cursor (o currentSelection da tela, ou o "recompiled options" da 1); X e triangulo seguem para o
// jogo. No "recompiled options" o X abre o painel; com ele aberto, so o triangulo (fecha) conta.
static void getInputHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t cur = rd32(rdram, wotm::sym::currScreen);
    if (ctx->pc == wotm::fn::screenGetInput__Fi)
        wotm_pad::markMenuFrame();   // nos menus (MenuConfirm e icones, wotm_pad.inc)
    // tela de opcoes do jogo: so a leitura do controle 1 (screenGetInput(1)); a do 2 so troca vibracao e controles, que sairam
    if (ctx->pc == wotm::fn::screenGetInput__Fi &&
        (screenDef(cur) || ((cur == wotm_optscreen::kScreen || !wotm_gamescreens::handled(cur) || cur == 21) && GPR_U32(ctx, 4) == 1)))
    {
        g_inRa = GPR_U32(ctx, 31);
        g_inPending = true;
    }
    g_origGetInput(rdram, ctx, runtime);
    if (!g_inPending || ctx->pc != g_inRa)
        return;
    g_inPending = false;
    if (!screenDef(cur) && cur != wotm_optscreen::kScreen)
    {
        const uint32_t raw = GPR_U32(ctx, 2), nc = wotm_gamescreens::input(rdram, ctx, runtime, cur, raw & 0xFFFFu);
        if (nc != (raw & 0xFFFFu))   // mouse: o passo ate o item sob ele, ou o X nele
            ctx->r[2] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>((raw & ~0xFFFFu) | nc)));
        return;
    }
    if (cur == wotm_optscreen::kScreen)
    {
        const uint32_t raw = GPR_U32(ctx, 2), nc = wotm_optscreen::input(rdram, ctx, runtime, raw & 0xFFFFu);
        if (nc != (raw & 0xFFFFu))   // mouse: o codigo do item sob ele (wotm_optscreen.inc)
            ctx->r[2] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>((raw & ~0xFFFFu) | nc)));
        return;
    }
    const ScreenDef &d = *screenDef(cur);
    if (cur == 1)
    {   // de volta ao menu principal: o "training" da tela 2 ficou para tras
        if (g_extraRow == kTrainingRow)
            g_extraRow = 0;
        wotm_training::menuDisarm("menu principal");
    }
    Row row[kMaxRows];
    const int rows = buildRows(rdram, d, row);
    if (rows == 0)
        return;
    const uint32_t raw = GPR_U32(ctx, 2);
    uint32_t code = raw & 0xFFFFu, out = raw;
    // mouse (etapa 9): a linha sob o cursor vira a escolhida quando ele anda; o clique esquerdo nela confirma (como o X)
    static uint32_t s_seq = 0;
    const uint32_t seq = wotm::mouse::g_moveSeq.load(std::memory_order_relaxed);
    const bool moved = seq != s_seq;
    s_seq = seq;
    if (!(cur == 1 && (g_open || g_suppress > 0)))
    {
        Hover hv;
        {
            std::lock_guard<std::mutex> lk(g_m);
            hv = g_hover;
        }
        const bool click = wotm::mouse::takeClick(wotm::mouse::g_clickL);
        if (hv.screen == d.screen && hv.row >= 0 && hv.row < rows && (moved || click) && (code == 0 || click))
        {
            if (hv.row != cursorRow(rdram, d.screen, row, rows))
            {
                g_extraRow = hasExtra(d.screen) && row[hv.row].item < 0 ? row[hv.row].item : 0;
                if (row[hv.row].item > 0)
                    wotm_padsel::wr32(rdram, curSelAddr(d.screen), static_cast<uint32_t>(row[hv.row].item));
                else if (row[hv.row].item == kTrainingRow)
                    wotm_padsel::wr32(rdram, curSelAddr(d.screen), static_cast<uint32_t>(kFreeForAll));
                if (!click)
                    wotm_padsel::sfx(rdram, ctx, runtime, 4);
            }
            if (click)
            {
                code = 6;
                out = (raw & ~0xFFFFu) | 6u;
            }
        }
    }
    if (cur == 1 && (g_open || g_suppress > 0))
    {
        // painel aberto: o jogo nao ve nada (a entrada do painel vem dos dispositivos); fechado ha pouco, o triangulo que fechou
        // ainda nao pode virar o "voltar" do menu principal
        out = 0;
        g_where = InMenu;
        if (g_open && panelInput(rdram, ctx, runtime))
        {
            g_open = false;
            g_suppress = 20;
        }
        else if (!g_open)
            --g_suppress;
    }
    else if (code == 1 || code == 2)
    {
        const int r = (cursorRow(rdram, d.screen, row, rows) + (code == 1 ? rows - 1 : 1)) % rows;
        g_extraRow = hasExtra(d.screen) && row[r].item < 0 ? row[r].item : 0;
        if (row[r].item > 0)
            wotm_padsel::wr32(rdram, curSelAddr(d.screen), static_cast<uint32_t>(row[r].item));
        else if (row[r].item == kTrainingRow)
            wotm_padsel::wr32(rdram, curSelAddr(d.screen), static_cast<uint32_t>(kFreeForAll));
        wotm_padsel::sfx(rdram, ctx, runtime, code == 1 ? 2 : 4);
        out = 0;
    }
    else if (code == 6 && cur == 2)
    {   // X num modo de 1 jogador: o "training" segue como o free-for-all (o currentSelection ja e o dele) com o modo treino ligado
        const bool training = g_extraRow == kTrainingRow;
        if (training)
            wotm_padsel::wr32(rdram, curSelAddr(2), static_cast<uint32_t>(kFreeForAll));
        if (training)
        {
            wotm_training::g_menuArmed = true;
            std::fprintf(stderr, "[treino] modo treino armado pelo menu (liga quando a luta comecar)\n");
        }
        else
            wotm_training::menuDisarm("outro modo");
    }
    else if (code == 6 && cur == 1 && g_extraRow == kQuitRow)
    {
        runtime->requestStop();   // o mesmo caminho do X da janela
        out = 0;
    }
    else if (code == 6 && cur == 1 && g_extraRow == kOptionsRow)
    {
        g_where = InMenu;
        openPanel();
        wotm_padsel::sfx(rdram, ctx, runtime, 8);
        out = 0;
    }
    ctx->r[2] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(out)));
}


// ------------------------------------------------------------------ desenho (janela)

// textura da lista: kMaxRows linhas, cada uma kSpacing passos dos itens do jogo (um pouco mais folgada que a lista dele), e kPadRows
// passos de folga em cima e embaixo (o brilho do item escolhido passa da linha). kH acompanha kSteps para manter a densidade.
constexpr float kSpacing = 1.15f, kPadRows = 0.6f, kSteps = kMaxRows * kSpacing + 2.f * kPadRows;
constexpr int kW = 1200, kH = 1190;
static RenderTexture2D g_rt{};
static Font g_font{};
static bool g_fontTried = false;

static Font &fallbackFont()
{
    if (!g_fontTried)
    {
        g_fontTried = true;
        for (const char *path : {"C:/Windows/Fonts/bahnschrift.ttf", "C:/Windows/Fonts/arialbd.ttf"})
            if (FileExists(path) && (g_font = LoadFontEx(path, 96, nullptr, 0)).texture.id)
                break;
        if (!g_font.texture.id) g_font = GetFontDefault();
        SetTextureFilter(g_font.texture, TEXTURE_FILTER_BILINEAR);
    }
    return g_font;
}
static Color gsToRgb(Color c)   // cor na escala do GS (0x80 = 1,0) -> a do raylib
{
    auto c8 = [](unsigned char v) { return static_cast<unsigned char>(std::min(255, v * 2)); };
    return Color{c8(c.r), c8(c.g), c8(c.b), c8(c.a)};
}
// Passo de uma letra da fonte do jogo, em pixels de uma textura rw x rh (wotm_padsel::text: 1,6 x a altura em unidades da tela do
// jogo, mais 2 de espaco)
static float glyphAdvance(int rw, int rh, float h)
{
    const wotm::GsScreenRect gr = wotm::gsScreenRect(float(rw), float(rh));
    const float sx = 640.f / gr.w, sy = 224.f / float(rh);
    return (1.6f * h * sy + 2.f) / sx;
}
static float textWidth(uint64_t tex0, int rw, int rh, const char *s, float h)
{
    if (tex0)
        return glyphAdvance(rw, rh, h) * float(std::strlen(s));
    return MeasureTextEx(fallbackFont(), s, h, 1.f).x;
}

// Texto com a fonte do jogo (atlas `tex0`) centrado em (cx, cy), numa textura rw x rh; sem atlas, Bahnschrift do Windows (ou a do
// raylib). `c` na escala do GS (0x80 = 1,0). A fonte do jogo nao tem ':' (o "16:9"): sai como dois pontos desenhados.
static void label(std::vector<wotm::TextQuad> &quads, const wotm::Ram &ram, uint64_t tex0, const char *s, float cx, float cy, float h, Color c,
                  int rw = kW, int rh = kH)
{
    if (tex0)
    {
        char buf[96];
        std::snprintf(buf, sizeof buf, "%s", s);
        const size_t n = std::strlen(buf);
        const float adv = glyphAdvance(rw, rh, h), x0 = cx - adv * float(n) * 0.5f;
        for (size_t i = 0; i < n; ++i)
            if (buf[i] == ':')
            {
                buf[i] = ' ';
                const float d = h * 0.16f, x = x0 + adv * (float(i) + 0.5f) - d * 0.5f;
                DrawRectangleRec(Rectangle{x, cy - h * 0.28f, d, d}, gsToRgb(c));
                DrawRectangleRec(Rectangle{x, cy + h * 0.12f, d, d}, gsToRgb(c));
            }
        wotm_padsel::text(quads, ram, tex0, rw, rh, buf, cx, cy, h, c);
        return;
    }
    const Vector2 sz = MeasureTextEx(fallbackFont(), s, h, 1.f);
    DrawTextEx(fallbackFont(), s, Vector2{cx - sz.x * 0.5f, cy - sz.y * 0.5f}, h, 1.f, gsToRgb(c));
}
// O mesmo, alinhado a esquerda em x0
static void labelLeft(std::vector<wotm::TextQuad> &quads, const wotm::Ram &ram, uint64_t tex0, const char *s, float x0, float cy, float h, Color c,
                      int rw, int rh)
{
    label(quads, ram, tex0, s, x0 + textWidth(tex0, rw, rh, s, h) * 0.5f, cy, h, c, rw, rh);
}

// ------------------------------------------------------------------ painel (textura propria, no telao inteiro)
constexpr int kPW = 1800, kPH = 1100;   // ~1,64:1, a proporcao do telao (204 x 124)
static RenderTexture2D g_prt{};

// Medidas das linhas de uma aba (desenho e mouse). Na VIDEO as linhas sao mais baixas e juntas (as 9 cabem sem rolar) e ficam a
// esquerda; a direita, a previa da opcao selecionada (wotm_preview.hpp).
struct PanelLayout
{
    float step, rowH, labX, valX, dx, rowRight;   // passo entre linhas, altura das letras, nome, valor (+- dx: setas), fim da faixa
    int vis;                                      // linhas visiveis
    bool preview;
    Rectangle box;                                // caixa da previa
    float y0 = float(kPH) / 8.9f * 2.2f;          // centro da primeira linha (abaixo das abas)
};
constexpr float kVideoStep = 0.76f;   // passo das linhas da VIDEO, em alturas de linha das outras abas
static PanelLayout panelLayout(int tab, bool preview, bool training = false)
{
    const float W = float(kPW), H = float(kPH), lineH = H / 8.9f;
    if (training)   // sem abas: o titulo e as 11 linhas, mais juntas, comecando mais acima
        return {lineH * 0.63f, 46.f, W * 0.08f, W * 0.66f, W * 0.20f, W * 0.97f, int(kTrainRows), false, {}, lineH * 1.85f};
    if (tab == TabVideo && !preview)   // previa escondida: as 9 linhas com as colunas das outras abas
        return {lineH * kVideoStep, 46.f, W * 0.10f, W * 0.66f, W * 0.19f, W * 0.97f, visRows(tab), false, {}};
    if (tab == TabVideo)
    {
        // caixa em 4:3, centrada na altura das 9 linhas
        const float bw = W * 0.30f, bh = bw * 0.75f, mid = lineH * 2.2f + lineH * kVideoStep * 4.f;
        return {lineH * kVideoStep, 46.f, W * 0.06f, W * 0.51f, W * 0.12f, W * 0.645f, visRows(tab), true,
                Rectangle{W * 0.665f, mid - bh * 0.5f - 20.f, bw, bh}};
    }
    return {lineH, 54.f, W * 0.10f, W * 0.66f, W * 0.19f, W * 0.97f, visRows(tab), false, {}};
}

// Previa da opcao (so na aba VIDEO): qual desenho cada opcao usa
static wotm::preview::Renderer g_preview;
static wotm::preview::Kind previewKind(st::Key k)
{
    using namespace wotm::preview;
    switch (k)
    {
    case st::Res: case st::RenderScale: case st::Msaa: return Sharpness;
    case st::TexFilter: return Filter;
    case st::Aspect: return Aspect;
    case st::Display: return Display;
    case st::Vsync: return Vsync;
    case st::Interp: return Interp;
    default: return None;   // REFRESH RATE: o jogo continua a 60, nao ha o que mostrar
    }
}
constexpr float kTelaoFrac = 0.55f, kPauseFrac = 0.60f;   // fracao da altura da janela que o painel ocupa: telao, pausa (2D)
static wotm::preview::Params previewParams(const Rectangle &box, float frac)
{
    wotm::preview::Params p;
    p.frameRows = wotm_live::renderHeight();
    p.winRows = std::max(1, GetScreenHeight());
    // o painel ocupa ~55% da altura da janela no telao (camera do painel, frameCamera), 60% na pausa
    p.boxScreenRows = box.height / float(kPH) * frac * float(p.winRows);
    p.samples = std::min(st::get(st::Msaa), wotm::msaa::maxSamples());
    p.filter = st::get(st::TexFilter);
    p.winAspect = GetScreenHeight() > 0 ? float(GetScreenWidth()) / float(GetScreenHeight()) : 16.f / 9.f;
    p.fixedAspect = st::aspectRatio();
    p.display = st::get(st::Display);
    const int mon = GetCurrentMonitor();
    p.monAspect = GetMonitorHeight(mon) > 0 ? float(GetMonitorWidth(mon)) / float(GetMonitorHeight(mon)) : 16.f / 9.f;
    p.vsync = st::on(st::Vsync);
    p.interp = st::on(st::Interp);
    p.time = GetTime();
    return p;
}
constexpr Color kLit{121, 118, 111, 0x80}, kDim{88, 56, 34, 0x80};   // escala do GS

static void renderPanel(wotm::Scene &scene, const wotm::Ram &ram, uint64_t tex0, const View &v)
{
    if (g_prt.id == 0)
    {
        g_prt = LoadRenderTexture(kPW, kPH);
        SetTextureFilter(g_prt.texture, TEXTURE_FILTER_BILINEAR);
    }
    const float W = float(kPW), H = float(kPH), lineH = H / 8.9f;   // abas, fio e kVisRows linhas
    const PanelLayout L = panelLayout(std::clamp(v.tab, 0, kTabs - 1), v.preview, v.training || v.moves);
    // previa da opcao selecionada, nas texturas dela (antes do BeginTextureMode do painel)
    wotm::preview::Kind pk = wotm::preview::None;
    wotm::preview::Params pp;
    if (L.preview && !v.training && !v.moves && v.panel > 0.f && v.opt >= 0 && v.opt < kTabDefs[TabVideo].n)
    {
        pk = previewKind(kTabDefs[TabVideo].opt[v.opt].key);
        pp = previewParams(L.box, v.screen < 0 ? kPauseFrac : kTelaoFrac);
        g_preview.prepare(pk, pp, L.box.width, L.box.height);
    }
    BeginTextureMode(g_prt);
    ClearBackground(Color{0, 0, 0, 0});
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    std::vector<wotm::TextQuad> q;
    DrawRectangleRounded(Rectangle{4.f, 4.f, W - 8.f, H - 8.f}, 0.06f, 12, Color{0, 0, 0, 175});

    // abas: L1  VIDEO  FEATURES  CAMERA  CONTROLS  R1
    const float tabY = lineH * 0.7f, tabH = 50.f;
    const float tabX[kTabs] = {W * 0.22f, W * 0.40f, W * 0.59f, W * 0.78f};
    if (v.training || v.moves)   // treino: so o titulo
        label(q, ram, tex0, v.moves ? "MOVE LIST" : "TRAINING OPTIONS", W * 0.5f, tabY, tabH, kLit, kPW, kPH);
    for (int t = 0; t < kTabs && !v.training && !v.moves; ++t)
    {
        const bool on = t == v.tab;
        label(q, ram, tex0, kTabNames[t], tabX[t], tabY, tabH, on ? kLit : kDim, kPW, kPH);
        if (on)
        {
            const float w = textWidth(tex0, kPW, kPH, kTabNames[t], tabH);
            DrawRectangleRec(Rectangle{tabX[t] - w * 0.5f, tabY + tabH * 0.75f, w, 5.f}, gsToRgb(kLit));
        }
    }
    // L1 e R1 com os icones do estilo escolhido (BUTTON ICONS), como a legenda; sem icone (tecla sem desenho), o nome numa moldura
    auto badge = [&](st::Bind b, float cx) {
        if (wotm_prompts::drawCell2d(wotm_prompts::cellFor(b), cx, tabY, 92.f, 1.f)) return;
        const char *s = st::keyName(int(st::kb(b) & 0xFFFF));
        if (!s) s = b == st::BL1 ? "L1" : "R1";
        const float bh = 76.f, bw = std::max(130.f, textWidth(tex0, kPW, kPH, s, 42.f) + 50.f);
        DrawRectangleRoundedLinesEx(Rectangle{cx - bw * 0.5f, tabY - bh * 0.5f, bw, bh}, 0.4f, 8, 3.f, gsToRgb(kDim));
        label(q, ram, tex0, s, cx, tabY, 42.f, kDim, kPW, kPH);
    };
    if (!v.training && !v.moves)
    {
        badge(st::BL1, W * 0.08f);
        badge(st::BR1, W * 0.92f);
    }
    DrawRectangleRec(Rectangle{W * 0.04f, lineH * 1.4f, W * 0.92f, 3.f}, Color{90, 80, 66, 120});

    // linhas da aba: nome, valor, dica e se o valor tem setas (escolha na lista)
    enum { KOpt, KDevice, KReset, KBind, KCapture };
    // mdir/mbtn/mtail: linha da MOVE LIST: direcao (teclado numerico) e botoes do comando saem como icones, mtail e o texto depois deles
    struct PRow { std::string label, value; const char *hint; bool arrows; int kind; int bind = -1; bool locked = false; int8_t mdir = 0; uint8_t mbtn = 0; std::string mtail; };
    PRow rows[kMaxCtlRows];
    int n = 0;
    const int tab = std::clamp(v.tab, 0, kTabs - 1);
    const TabDef &t = kTabDefs[tab];
    const bool inPause = v.screen < 0;
    auto optRow = [&](const Opt &o) {
        char buf[32];
        valueText(o.key, st::get(o.key), buf, sizeof buf);
        if (lockedOpt(o.key, inPause))   // apagada, sem setas
            return PRow{o.label, buf, "CAN ONLY BE CHANGED IN THE MAIN MENU", false, KOpt, -1, true};
        return PRow{o.label, buf, o.hint, true, KOpt};
    };
    std::vector<std::string> moveHints;   // as dicas da MOVE LIST (o PRow guarda so o ponteiro)
    if (v.moves)
    {
        tr::Shared &sh = tr::shared();
        std::lock_guard<std::mutex> lk(sh.m);
        moveHints.reserve(sh.moveList.size() + 1);
        for (const tr::Move &m : sh.moveList)
        {
            if (n >= kMaxCtlRows) break;
            moveHints.push_back(moveHint(m));
            PRow &pr = rows[n++];
            pr = PRow{moveLabel(m), isFav(sh, m) ? "*" : "", moveHints.back().c_str(), false, KReset};   // "*": estrela na borda direita
            if (m.kind == tr::MkOther) { pr.mdir = m.dir; pr.mbtn = m.btns; pr.mtail = m.name; }
            else
            {
                pr.mdir = int8_t(tr::moveTypeDir(m.type)); pr.mbtn = tr::moveTypeBtn(m.type);
                const std::string btn = m.type <= 2 ? "HEAVY" : "LIGHT";   // a direcao ja esta na seta
                pr.mtail = m.kind == tr::MkCombo ? btn + " X " + std::to_string(int(m.len)) : m.kind == tr::MkWeapon ? btn + " WITH OBJECT" : btn;
            }
        }
        if (n == 0)
        {
            moveHints.push_back("THE LIST IS BUILT FROM YOUR MONSTER ONCE THE FIGHT STARTS");
            rows[n++] = PRow{"NO MOVES YET", "", moveHints.back().c_str(), false, KReset};
        }
    }
    else if (v.training)
        for (int i = 0; i < kTrainRows; ++i)
            rows[n++] = i == TrClear ? PRow{kTrainDefs[i].label, "", kTrainDefs[i].hint, false, KReset}
                                     : PRow{kTrainDefs[i].label, trainValue(i), kTrainDefs[i].hint, true, KOpt};
    else if (tab != TabControls)
        for (int i = 0; i < t.n; ++i) rows[n++] = optRow(t.opt[i]);
    else
    {
        int r[kMaxCtlRows];
        const int m = ctlRows(v.bindMap < 0, r);
        const bool blink = std::fmod(GetTime(), 0.8) < 0.5;
        for (int i = 0; i < m; ++i)
        {
            if (r[i] <= kCtlOpt) rows[n++] = optRow(t.opt[kCtlOpt - r[i]]);
            else if (r[i] == kCtlDevice)
                rows[n++] = {"BUTTON MAP", kMapNames[v.bindMap + 1], "WHOSE CONTROLS TO CHANGE. EACH PLAYER HAS A GAMEPAD LAYOUT OF THEIR OWN", true, KDevice};
            else if (r[i] == kCtlReset)
                rows[n++] = {"RESTORE DEFAULTS", "", (v.bindMap < 0) ? "PUTS EVERY KEY BACK THE WAY IT CAME" : "PUTS EVERY BUTTON BACK THE WAY IT CAME", false, KReset};
            else if (r[i] == v.capture)
                rows[n++] = {kBindLabels[r[i]], blink ? ((v.bindMap < 0) ? "PRESS A KEY" : "PRESS A BUTTON") : "",
                             (v.bindMap < 0) ? "PRESS THE KEY YOU WANT FOR THIS BUTTON" : "PRESS THE BUTTON YOU WANT, ON ANY GAMEPAD", false, KCapture, r[i]};
            else
                rows[n++] = {kBindLabels[r[i]], bindValue(r[i], v.bindMap),
                             (v.bindMap < 0) ? "THE KEY YOU PRESS FOR THIS BUTTON" : "THE GAMEPAD BUTTON YOU PRESS FOR THIS ONE", false, KBind, r[i]};
        }
    }
    // nome a esquerda, valor a direita (setas na selecionada); so kVisRows linhas a partir de v.top
    const float rowH = L.rowH, valX = L.valX, labX = L.labX, dx = L.dx;   // o telao e inclinado: valor perto do nome
    const int top = std::clamp(v.top, 0, std::max(0, n - L.vis));
    for (int i = top; i < std::min(n, top + L.vis); ++i)
    {
        const PRow &pr = rows[i];
        const float cy = L.y0 + L.step * float(i - top);
        const bool on = i == v.opt;
        if (on)
            DrawRectangleRounded(Rectangle{W * 0.03f, cy - L.step * 0.42f, L.rowRight - W * 0.03f, L.step * 0.84f}, 0.5f, 8, Color{60, 50, 40, 130});
        // botao do PS2: o icone dele no estilo do PS2 (os face buttons do jogo); "L1 AND R1" com os dois; sem icone (analogico), o nome
        float labW = 0.f;
        if (pr.mbtn)
        {   // MOVE LIST: a direcao (seta, ponto quando nao ha) e os botoes do comando nos icones do dispositivo do jogador, como na
            // tabela in-game; depois, o resto do nome. Sem icone (atlas ausente), o nome inteiro em texto
            const float S = 64.f, ia = on ? 1.f : 0.6f;   // menores que os da aba de controles: ate 3 botoes + seta antes do texto
            static constexpr struct { uint16_t bit; st::Bind b; } kIc[] = {{tr::kBtnR2, st::BR2}, {tr::kBtnSquare, st::BSquare}, {tr::kBtnTriangle, st::BTriangle},
                                                                           {tr::kBtnX, st::BCross}, {tr::kBtnCircle, st::BCircle}};
            float bx = labX;
            bool drew = true;
            for (const auto &ic : kIc)
                if ((pr.mbtn & ic.bit) && (wotm_prompts::cellFor(ic.b) < 0 || !wotm_prompts::loadAtlas())) { drew = false; break; }
            if (drew)
            {
                wotm_trainview::drawDir(pr.mdir, bx + S * 0.4f, cy, S * 0.8f, Color{255, 255, 255, static_cast<unsigned char>(ia * 255.f)});
                bx += S * 0.85f;
                for (const auto &ic : kIc)
                    if (pr.mbtn & ic.bit)
                    {
                        wotm_prompts::drawCell2d(wotm_prompts::cellFor(ic.b), bx + S * 0.5f, cy, S, ia);
                        bx += S * 1.02f;
                    }
                if (!pr.mtail.empty())
                {
                    bx += 14.f;
                    labelLeft(q, ram, tex0, pr.mtail.c_str(), bx, cy, rowH, on ? kLit : kDim, kPW, kPH);
                    bx += textWidth(tex0, kPW, kPH, pr.mtail.c_str(), rowH);
                }
                labW = bx - labX;
            }
        }
        else if (pr.bind >= 0)
        {
            const float S = 86.f, ia = on ? 1.f : 0.6f;
            if (pr.bind == st::BL1R1)
            {
                if (wotm_prompts::drawCell2d(wotm_prompts::padCell(st::kDefaultPad[st::BL1], st::PromptPs), labX + S * 0.5f, cy, S, ia) &&
                    wotm_prompts::drawCell2d(wotm_prompts::padCell(st::kDefaultPad[st::BR1], st::PromptPs), labX + S * 1.6f, cy, S, ia))
                    labW = S * 2.1f;
            }
            else if (wotm_prompts::drawCell2d(wotm_prompts::padCell(st::kDefaultPad[pr.bind], st::PromptPs), labX + S * 0.5f, cy, S, ia))
                labW = S;
        }
        if (labW <= 0.f)
        {
            labelLeft(q, ram, tex0, pr.label.c_str(), labX, cy, rowH, on ? kLit : kDim, kPW, kPH);
            labW = textWidth(tex0, kPW, kPH, pr.label.c_str(), rowH);
        }
        // fio do nome ao valor: com a inclinacao do telao, guia o olho pela linha
        const float lx1 = labX + labW + 24.f, lx2 = valX - dx - 36.f;
        if (lx2 > lx1 && !pr.value.empty() && !v.moves)
            DrawRectangleRec(Rectangle{lx1, cy - 1.f, lx2 - lx1, 2.f}, Color{110, 96, 78, static_cast<unsigned char>(on ? 140 : 70)});
        // valor longo ("LSHIFT / RSHIFT"): letras menores para caber entre as setas
        const float vw = textWidth(tex0, kPW, kPH, pr.value.c_str(), rowH), maxW = 2.f * dx - 70.f;
        const float vh = vw > maxW ? rowH * maxW / vw : rowH;
        // botao do gamepad: o icone dele no estilo escolhido (no teclado, o do PS2)
        const int phys = pr.kind == KBind && v.bindMap >= 0 ? st::pad(st::Bind(pr.bind), v.bindMap) : 0;
        const int style = wotm_prompts::currentStyle();
        const bool valIcon = phys > 0 && wotm_prompts::drawCell2d(wotm_prompts::padCell(phys, style == st::PromptKeyboard ? st::PromptPs : style), valX, cy,
                                                                  86.f, on ? 1.f : 0.6f);
        if (v.moves && pr.value == "*")
        {   // favorito da MOVE LIST: estrela de cinco pontas alinhada a direita (a fonte do jogo nao tem o simbolo)
            const float cx = L.rowRight - 40.f, R = rowH * 0.55f, r = R * 0.42f;
            const Color sc = on ? Color{255, 220, 90, 255} : Color{230, 190, 70, 220};
            Vector2 pts[10];
            for (int k = 0; k < 10; ++k)
            {
                const float ang = -PI * 0.5f + float(k) * PI / 5.f, rad = (k & 1) ? r : R;
                pts[k] = Vector2{cx + std::cos(ang) * rad, cy + std::sin(ang) * rad};
            }
            for (int k = 0; k < 10; ++k)
            {   // nas duas ordens: o raylib descarta a face de tras
                DrawTriangle(Vector2{cx, cy}, pts[(k + 1) % 10], pts[k], sc);
                DrawTriangle(Vector2{cx, cy}, pts[k], pts[(k + 1) % 10], sc);
            }
        }
        else if (!pr.value.empty() && !valIcon)
            label(q, ram, tex0, pr.value.c_str(), valX, cy, vh, on && !pr.locked ? kLit : kDim, kPW, kPH);
        if (on && pr.arrows)
        {
            const Color a = gsToRgb(kLit);
            const float s = 20.f;
            DrawTriangle(Vector2{valX - dx - s, cy}, Vector2{valX - dx + s * 0.7f, cy + s}, Vector2{valX - dx + s * 0.7f, cy - s}, a);
            DrawTriangle(Vector2{valX + dx + s, cy}, Vector2{valX + dx - s * 0.7f, cy - s}, Vector2{valX + dx - s * 0.7f, cy + s}, a);
        }
    }
    // rolagem: setinhas na borda direita quando ha linhas acima ou abaixo
    {
        const Color a = gsToRgb(kDim);
        const float x = W * 0.955f, s = 16.f;
        if (top > 0)
        {
            const float y = lineH * 1.75f;
            DrawTriangle(Vector2{x, y - s}, Vector2{x - s, y + s * 0.6f}, Vector2{x + s, y + s * 0.6f}, a);
        }
        if (top + L.vis < n)
        {
            const float y = L.y0 + L.step * (float(L.vis) - 0.45f);
            DrawTriangle(Vector2{x, y + s}, Vector2{x + s, y - s * 0.6f}, Vector2{x - s, y - s * 0.6f}, a);
        }
    }
    // previa da opcao selecionada (aba VIDEO), com uma legenda curta embaixo na lupa
    if (pk != wotm::preview::None)
    {
        g_preview.draw(pk, pp, L.box, 255);
        if (pk == wotm::preview::Sharpness || pk == wotm::preview::Filter)
            label(q, ram, tex0, pk == wotm::preview::Sharpness ? "4X ZOOM" : "2X ZOOM", L.box.x + L.box.width * 0.5f, L.box.y + L.box.height + 40.f,
                  34.f, kDim, kPW, kPH);
    }
    // dica da opcao selecionada (grande, sobre os carros) e os botoes (legenda de baixo, no lugar da do jogo): wotm_legend.inc, pelo
    // mapa, como o readPanel le
    using wotm_legend::Icon;
    const float la = v.alpha * v.panel;
    const float bar = inPause ? 0.55f : 1.f;   // na pausa, faixa translucida (a luta continua atras, como no modo foto)
    const int kind = v.opt >= 0 && v.opt < n ? rows[v.opt].kind : KOpt;
    if (v.opt >= 0 && v.opt < n)
        wotm_legend::hint(rows[v.opt].hint, la);
    using wotm_legend::bind;
    Icon tabs1 = bind(st::BL1), tabs2 = bind(st::BR1);
    const Icon left = bind(st::BLeft), right = bind(st::BRight);
    if (!tabs1.key) tabs1.key = "Q";   // L1/R1 sem tecla (layout 2): o painel troca a aba com Q/E (readPanel)
    if (!tabs2.key) tabs2.key = "E";
    const Icon ok = bind(st::BCross), back = bind(st::BTriangle), clear = bind(st::BSquare);
    if (v.moves)
        wotm_legend::post({{ok, {}, "FAVORITE"}, {back, {}, "BACK"}}, la, bar);
    else if (v.training)
    {
        if (kind == KReset) wotm_legend::post({{ok, {}, "CLEAR"}, {back, {}, "BACK"}}, la, bar);
        else wotm_legend::post({{left, right, "CHANGE"}, {back, {}, "BACK"}}, la, bar);
    }
    else if (kind == KCapture)
    {
        if (v.bindMap < 0) wotm_legend::post({{Icon{0, "ESC"}, {}, "CANCEL"}}, la, bar);
        else wotm_legend::post({{{}, {}, "WAIT 5 SECONDS TO CANCEL"}}, la, bar);
    }
    else if (kind == KBind)
        wotm_legend::post({{tabs1, tabs2, "TABS"}, {ok, {}, v.bindMap < 0 ? "NEW KEY" : "NEW BUTTON"}, {clear, {}, "CLEAR"}, {back, {}, "BACK"}}, la, bar);
    else if (kind == KReset)
        wotm_legend::post({{tabs1, tabs2, "TABS"}, {ok, {}, "RESTORE"}, {back, {}, "BACK"}}, la, bar);
    else if (v.opt >= 0 && v.opt < n && rows[v.opt].locked)
        wotm_legend::post({{tabs1, tabs2, "TABS"}, {back, {}, "BACK"}}, la, bar);
    else if (v.tab == TabVideo)
    {
        Icon pv = clear;   // quadrado no controle, P no teclado (readPanel)
        pv.key = "P";
        pv.key2 = nullptr;
        wotm_legend::post({{tabs1, tabs2, "TABS"}, {left, right, "CHANGE"}, {pv, {}, v.preview ? "HIDE PREVIEW" : "SHOW PREVIEW"}, {back, {}, "BACK"}}, la, bar);
    }
    else
        wotm_legend::post({{tabs1, tabs2, "TABS"}, {left, right, "CHANGE"}, {back, {}, "BACK"}}, la, bar);
    scene.drawQuads(q, W, H);
    EndBlendMode();
    EndTextureMode();
}

static void renderUi(wotm::Scene &scene, const wotm::Ram &ram, uint64_t tex0)
{
    View v;
    {
        std::lock_guard<std::mutex> lk(g_m);
        v = g_view;
    }
    if (!v.on || v.alpha <= 0.f || v.rows == 0)
        return;
    if (g_rt.id == 0)
    {
        g_rt = LoadRenderTexture(kW, kH);
        SetTextureFilter(g_rt.texture, TEXTURE_FILTER_BILINEAR);
    }
    constexpr float step = float(kH) / kSteps, rowH = step * kSpacing;   // passo dos itens do jogo e o das nossas linhas
    // cores dos itens do telao: aceso creme, apagado laranja terroso (escala do GS)
    constexpr Color lit{121, 118, 111, 0x80}, dim{88, 56, 34, 0x80};
    BeginTextureMode(g_rt);
    ClearBackground(Color{0, 0, 0, 0});
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    std::vector<wotm::TextQuad> quads;
    const float list = 1.f - v.panel;
    // altura unica das letras: a maior em que a linha mais longa cabe (cada letra da fonte do jogo tem ~0,75 x a altura de largura,
    // nesta textura, mais o espaco)
    size_t longest = 1;
    for (int r = 0; r < v.rows; ++r)
        longest = std::max(longest, std::strlen(v.row[r].label));
    const float textH = std::min(step * 0.5f, kW * 0.9f / (0.8f * float(longest)));
    // com as letras do jogo (wotm_menufont.inc): minusculas, altura do x unica, a maior em que a linha mais larga cabe
    char low[kMaxRows][32] = {};
    bool own = true;
    float widest = 0.f;
    for (int r = 0; r < v.rows && own; ++r)
    {
        std::snprintf(low[r], sizeof low[r], "%s", v.row[r].label);
        for (char *c = low[r]; *c; ++c) *c = static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
        const float w = wotm_menufont::width(low[r]);
        own = w > 0.f;
        widest = std::max(widest, w);
    }
    const float xh = own ? std::min(step * 0.56f, kW * 0.9f * wotm_menufont::kRefXh / widest) : 0.f;
    if (list > 0.f)
    {
        const float top = (kH - rowH * float(v.rows)) * 0.5f;
        // brilho atras do item escolhido, como o objeto solto do jogo (m_menuObj, textura 194): miolo com alfa de vertice 19 (x2: ~0,3)
        // na cor dos itens apagados, caindo a 0 na borda; miolo com ~0,55 do passo dos itens de altura, borda com 1,5 passo de altura e
        // meio passo de cada lado; miolo da largura do texto (o do jogo tem uma so, a de uma palavra curta)
        if (own && v.sel >= 0 && v.sel < v.rows)
        {
            const float cy = top + rowH * (float(v.sel) + 0.5f), w = wotm_menufont::width(low[v.sel]) * xh / wotm_menufont::kRefXh * 0.95f;
            const float x0 = kW * 0.5f - w * 0.5f, x1 = kW * 0.5f + w * 0.5f, y0 = cy - step * 0.27f, y1 = cy + step * 0.27f;
            const float mx = step * 0.5f, my = step * 0.48f;
            const Color in = gsToRgb(Color{67, 41, 28, static_cast<unsigned char>(19 * 2 * list)}), out{in.r, in.g, in.b, 0};
            const float xs[4] = {x0 - mx, x0, x1, x1 + mx}, ys[4] = {y0 - my, y0, y1, y1 + my};
            rlSetTexture(rlGetTextureIdDefault());
            rlBegin(RL_QUADS);
            for (int j = 0; j < 3; ++j)
                for (int i = 0; i < 3; ++i)
                {
                    // vertices na ordem do DrawRectangle (cima-esq, baixo-esq, baixo-dir, cima-dir); so os do miolo com alfa
                    const int vi[4][2] = {{i, j}, {i, j + 1}, {i + 1, j + 1}, {i + 1, j}};
                    for (const auto &p : vi)
                    {
                        const Color &k = p[0] >= 1 && p[0] <= 2 && p[1] >= 1 && p[1] <= 2 ? in : out;
                        rlColor4ub(k.r, k.g, k.b, k.a);
                        rlTexCoord2f(0.f, 0.f);
                        rlVertex2f(xs[p[0]], ys[p[1]]);
                    }
                }
            rlEnd();
            rlSetTexture(0);
        }
        for (int r = 0; r < v.rows; ++r)
        {
            const float cy = top + rowH * (float(r) + 0.5f);
            const bool on = r == v.sel;
            if (own)
            {
                // cores dos itens do jogo (cor de vertice dos objetos, escala do GS; alfa x2, como a textura deles): o escolhido cinza
                // opaco, os outros marrom com ~40% de opacidade
                Color c = on ? Color{64, 64, 64, 66} : Color{67, 41, 28, 26};
                c.a = static_cast<unsigned char>(std::min(0x80, c.a * 2) * list);
                if (wotm_menufont::draw(low[r], kW * 0.5f, cy + xh * 0.5f, xh, gsToRgb(c)))
                    continue;
            }
            if (on)
                DrawRectangleRounded(Rectangle{kW * 0.04f, cy - rowH * 0.42f, kW * 0.92f, rowH * 0.84f}, 0.5f, 8,
                                     Color{0, 0, 0, static_cast<unsigned char>(110 * list)});
            Color c = on ? lit : dim;
            c.a = static_cast<unsigned char>(0x80 * list);
            const char *s = v.row[r].label;
            label(quads, ram, tex0, s, kW * 0.5f, cy, textH, c);
        }
    }
    scene.drawQuads(quads, float(kW), float(kH));
    EndBlendMode();
    EndTextureMode();
    if (v.panel > 0.f)
        renderPanel(scene, ram, tex0, v);
}

// Plano do texto de um item (de varios objetos, o de menor altura: o texto, nao o brilho)
static bool itemPlane(wotm::Scene &scene, const wotm::Ram &ram, const Camera3D &cam, const uint32_t objs[4], wotm_padsel::Plane &out)
{
    bool got = false;
    wotm_padsel::Plane p;
    for (int i = 0; i < 4 && objs[i]; ++i)
        if (wotm_padsel::objectPlane(scene, ram, cam, objs[i], p) && (!got || p.y1 - p.y0 < out.y1 - out.y0))
        {
            out = p;
            got = true;
        }
    return got;
}

static void panelHit(float u, float w, const View &v, Hover &hv);
// O painel no plano do telao, no telao inteiro (o logo so aparece no "press start"), centrado
static void drawPanelOnTelao(wotm::Scene &scene, const wotm::Ram &ram, const Camera3D &cam, const View &v, Hover &hv)
{
    wotm_padsel::Plane pl;
    if (!v.on || v.panel <= 0.f || v.alpha <= 0.f || g_prt.id == 0 || !v.screenObj || !wotm_padsel::objectPlane(scene, ram, cam, v.screenObj, pl))
        return;
    const float fyTop = 0.03f, fyBottom = 0.97f;   // a camera do painel (frameCamera) mostra o telao inteiro
    const float hy = (pl.y1 - pl.y0) * (fyBottom - fyTop), wx = std::min(hy * float(kPW) / float(kPH), (pl.x1 - pl.x0) * 0.94f);
    const float h = wx * float(kPH) / float(kPW);   // largura limitada: a altura acompanha (proporcao da textura)
    const float left = (pl.x0 + pl.x1) * 0.5f - wx * 0.5f, top = pl.y1 - (pl.y1 - pl.y0) * fyTop - (hy - h) * 0.5f;
    wotm_padsel::drawOnPlane(pl, g_prt.texture, left, top, wx, h, v.alpha * v.panel, 0.003f * (pl.x1 - pl.x0));
    float u, w;
    if (v.panel >= 1.f && wotm_padsel::hitOnPlane(cam, pl, left, top, wx, h, u, w))
        panelHit(u, w, v, hv);
}

// Painel da pausa: reto no centro do quadro (rw x rh), com um veu escuro leve (a luta fica visivel para comparar as opcoes de video).
// Altura 0,60 do quadro, topo em 0,09: livres embaixo a dica (0,74..0,89) e a legenda (0,893). Dentro do BeginTextureMode do quadro,
// depois do texto do jogo e antes do wotm_legend::draw; grava o que esta sob o mouse no g_pauseHover.
static void drawPausePanel2d(int rw, int rh)
{
    View v;
    {
        std::lock_guard<std::mutex> lk(g_m);
        v = g_pauseView;
    }
    Hover hv;
    if (v.on && v.panel > 0.f && g_prt.id != 0)
    {
        const float RW = float(rw), RH = float(rh);
        float h = kPauseFrac * RH, w = h * float(kPW) / float(kPH);
        if (w > 0.96f * RW)
        {
            w = 0.96f * RW;
            h = w * float(kPH) / float(kPW);
        }
        const float left = (RW - w) * 0.5f, top = 0.09f * RH + (kPauseFrac * RH - h) * 0.5f;
        rlDrawRenderBatchActive();
        BeginBlendMode(BLEND_ALPHA);
        DrawRectangleRec(Rectangle{0.f, 0.f, RW, RH}, Color{0, 0, 0, static_cast<unsigned char>(60.f * v.panel)});
        DrawTexturePro(g_prt.texture, Rectangle{0.f, 0.f, float(kPW), -float(kPH)}, Rectangle{left, top, w, h}, Vector2{0.f, 0.f}, 0.f,
                       Color{255, 255, 255, static_cast<unsigned char>(255.f * std::clamp(v.panel, 0.f, 1.f))});
        EndBlendMode();
        wotm::mouse::claim();   // o clique esquerdo e do painel (nao um quadrado do jogo)
        const wotm::mouse::Pointer &p = wotm::mouse::g_ptr;
        if (v.panel >= 1.f && p.in)
            panelHit((p.x - left) / w, (p.y - top) / h, v, hv);
    }
    std::lock_guard<std::mutex> lk(g_m);
    g_pauseHover = hv;
}
// Textura do painel da pausa (fora do BeginTextureMode do quadro, junto do renderUi)
static void renderPauseUi(wotm::Scene &scene, const wotm::Ram &ram, uint64_t tex0)
{
    View v;
    {
        std::lock_guard<std::mutex> lk(g_m);
        v = g_pauseView;
    }
    if (v.on && v.panel > 0.f)
        renderPanel(scene, ram, tex0, v);
}

// Parte do painel sob o mouse, em coordenadas da textura (u, w de 0 a 1): as mesmas medidas do renderPanel (abas, L1/R1, linhas e as
// setas do valor). No telao e na pausa.
static void panelHit(float u, float w, const View &v, Hover &hv)
{
    if (u < 0.f || u > 1.f || w < 0.f || w > 1.f)
        return;
    const float W = float(kPW), H = float(kPH), lineH = H / 8.9f, X = u * W, Y = w * H;
    if (!v.training && !v.moves && std::fabs(Y - lineH * 0.7f) < lineH * 0.5f)
    {
        const float tabX[kTabs] = {W * 0.22f, W * 0.40f, W * 0.59f, W * 0.78f};
        if (X < W * 0.14f) hv.part = HPrevTab;
        else if (X > W * 0.86f) hv.part = HNextTab;
        else
            for (int t = 0; t < kTabs; ++t)
                if (std::fabs(X - tabX[t]) < W * 0.09f) { hv.part = HTab; hv.tab = t; }
        return;
    }
    const int tab = std::clamp(v.tab, 0, kTabs - 1);
    const PanelLayout L = panelLayout(tab, v.preview, v.training || v.moves);
    const int n = v.moves ? std::max(1, moveRows()) : v.training ? int(kTrainRows) : tabRows(tab), first = std::clamp(v.top, 0, std::max(0, n - L.vis));
    const int i = int(std::floor((Y - L.y0) / L.step + 0.5f));
    if (i < 0 || i >= L.vis || first + i >= n || X < W * 0.03f || X > L.rowRight)
        return;
    hv.prow = first + i;
    const float valX = L.valX, dx = L.dx;
    hv.part = X > valX - dx - 70.f && X < valX - dx * 0.45f ? HRowLeft : X > valX + dx * 0.45f && X < valX + dx + 70.f ? HRowRight : HRow;
}

// A lista no plano dos itens do jogo: do topo do primeiro ao pe do ultimo, centrada na coluna deles. Dentro do BeginMode3D do quadro.
static void drawListOnTelao(wotm::Scene &scene, const wotm::Ram &ram, const Camera3D &cam, const View &v, Hover &hv)
{
    if (!v.on || v.alpha <= 0.f || v.items == 0 || g_rt.id == 0)
        return;
    wotm_padsel::Plane base, p;
    if (!itemPlane(scene, ram, cam, v.itemObj[0], base))
        return;
    float top = -1e30f, bottom = 1e30f, cx = 0.f, itemH = 0.f;
    int n = 0;
    for (int i = 0; i < v.items; ++i)
    {
        if (!itemPlane(scene, ram, cam, v.itemObj[i], p))
            continue;
        const Vector3 d = wotm_padsel::vsub(p.c, base.c);
        const float y = wotm_padsel::vdot(d, base.up), h = p.y1 - p.y0;
        top = std::max(top, y + h * 0.5f);
        bottom = std::min(bottom, y - h * 0.5f);
        cx += wotm_padsel::vdot(d, base.right);
        itemH = std::max(itemH, h);
        ++n;
    }
    if (!n || top <= bottom)
        return;
    cx /= float(n);
    // cada uma das kMaxRows vagas da textura tem a altura do passo dos itens do jogo (o vao dividido por eles); as linhas ficam
    // centradas no vao
    const float span = top - bottom, hy = span * kSteps / float(n), wx = hy * float(kW) / float(kH);
    float mid = (top + bottom) * 0.5f;
    // sem o logo (logoFrame), a lista vai para o centro do telao
    if (wotm_padsel::Plane tl; g_logoN && v.screenObj && wotm_padsel::objectPlane(scene, ram, cam, v.screenObj, tl))
    {
        const Vector3 d = wotm_padsel::vsub(tl.c, base.c);
        mid = wotm_padsel::vdot(d, base.up) - (tl.y1 - tl.y0) * 0.06f;   // centro da parte visivel (o topo some atras da faixa preta)
        cx = wotm_padsel::vdot(d, base.right);
    }
    static int logged = 0;
    if (!(logged & (1 << v.screen)))
    {
        logged |= 1 << v.screen;
        std::fprintf(stderr, "[mainmenu] tela %d em texto: %d itens do jogo, vao %.1f, linha %.1f\n", v.screen, n, span, itemH);
    }
    wotm_padsel::drawOnPlane(base, g_rt.texture, cx - wx * 0.5f, mid + hy * 0.5f, wx, hy, v.alpha, 0.004f * (base.x1 - base.x0), nullptr, WHITE,
                             true);
    // mouse: a linha sob ele (as vagas do renderUi), no miolo da coluna
    wotm::mouse::claim();
    float u, w;
    if (v.panel > 0.f || !wotm_padsel::hitOnPlane(cam, base, cx - wx * 0.5f, mid + hy * 0.5f, wx, hy, u, w) || u < 0.12f || u > 0.88f)
        return;
    const float step = float(kH) / kSteps, rowH = step * kSpacing, y = w * float(kH) - (kH - rowH * float(v.rows)) * 0.5f;
    const int r = int(std::floor(y / rowH));
    if (y >= 0.f && r >= 0 && r < v.rows)
    {
        hv.screen = v.screen;
        hv.row = r;
    }
}

static void drawOnTelao(wotm::Scene &scene, const wotm::Ram &ram, const Camera3D &cam)
{
    View v;
    {
        std::lock_guard<std::mutex> lk(g_m);
        v = g_view;
    }
    Hover hv;
    drawListOnTelao(scene, ram, cam, v, hv);
    drawPanelOnTelao(scene, ram, cam, v, hv);
    if (v.on && v.panel > 0.f) wotm::mouse::claim();
    static const bool log = std::getenv("WOTM_MENUMOUSELOG") != nullptr;   // uma linha por mudanca do que esta sob o mouse
    static Hover last;
    if (log && (hv.screen != last.screen || hv.row != last.row || hv.part != last.part || hv.tab != last.tab || hv.prow != last.prow))
    {
        const wotm::mouse::Pointer &p = wotm::mouse::g_ptr;
        std::fprintf(stderr, "[menumouse] t=%.2f cursor %.0f,%.0f (janela %.0f,%.0f) tela %d linha %d painel parte %d aba %d linha %d\n", GetTime(), p.x, p.y, GetMousePosition().x,
                     GetMousePosition().y, hv.screen, hv.row, hv.part, hv.tab, hv.prow);
        last = hv;
    }
    std::lock_guard<std::mutex> lk(g_m);
    g_hover = hv;
}

// Camera do quadro com o painel aberto: de frente para o telao, que ocupa a parte de cima do espaco entre as faixas pretas; embaixo
// ficam os carros, onde vai a dica grande (wotm_legend.inc). v.panel (0..1, a animacao de abrir e fechar) leva da camera do jogo a
// essa e de volta. Antes do BeginMode3D do quadro; so muda a vista (a cena esta em coordenadas do mundo).
static void frameCamera(wotm::Scene &scene, const wotm::Ram &ram, Camera3D &cam, float aspect)
{
    View v;
    {
        std::lock_guard<std::mutex> lk(g_m);
        v = g_view;
    }
    // ida e volta em ~0,45 s, no relogio da janela (o v.panel anda 0,1 por quadro do jogo: rapido demais para a camera)
    static float s_t = 0.f;
    static double s_last = 0.0;
    const double now = GetTime();
    const float dt = std::clamp(float(now - s_last), 0.f, 0.1f);
    s_last = now;
    const bool want = v.on && v.alpha > 0.f && v.panel > 0.f && g_open;
    s_t = std::clamp(s_t + (want ? dt : -dt) / 0.45f, 0.f, 1.f);
    wotm_padsel::Plane pl;
    if (!v.on || s_t <= 0.f || v.alpha <= 0.f || !v.screenObj || !wotm_padsel::objectPlane(scene, ram, cam, v.screenObj, pl))
    {
        if (!v.on) s_t = 0.f;
        return;
    }
    // os icones de botao do jogo seguem a camera dele (ficariam soltos na cena com esta): fora enquanto ela nao e a do jogo
    scene.overlay.clear();
    scene.hpOverlay.clear();
    wotm_padsel::telaoCamera(cam, pl, aspect, s_t);
}
} // namespace wotm_mainmenu

bool wotmTextMenuOn() { return wotm_mainmenu::g_enabled; }

void wotmMainMenuInstall(PS2Runtime &runtime)
{
    using namespace wotm_mainmenu;
    wotm_pausemenu::install(runtime);   // com ou sem os menus em texto
    wotm_rumble::install(runtime);
    if (!g_enabled)
        return;
    struct H { uint32_t addr; PS2Runtime::RecompiledFunction *orig; PS2Runtime::RecompiledFunction fn; };
    const H hooks[] = {
        {wotm::fn::screenMain__Fv, &g_origMain, &mainHook},
        {wotm::fn::screenGameModes1P__Fv, &g_origModes1P, &modes1Hook},
        {wotm::fn::screenGameModes2P__Fv, &g_origModes2P, &modes2Hook},
        {wotm::fn::screenTransition__Fb, &g_origTransition, &transitionHook},
        {wotm::fn::screenGetInput__Fi, &g_origGetInput, &getInputHook},
        {wotm::fn::screenOptions__Fv, &g_origOptions, &optionsHook},
    };
    for (const H &h : hooks)
        if ((*h.orig = runtime.lookupFunction(h.addr)))
            runtime.registerFunction(h.addr, h.fn);
    std::fprintf(stderr, "[mainmenu] menus de itens em texto: principal, modos de 1 jogador e do multiplayer (WOTM_TEXTMENU=0 volta aos do jogo)\n");
}

void wotmMainMenuLogoFrame(uint8_t *rdram) { wotm_mainmenu::logoFrame(rdram); }
