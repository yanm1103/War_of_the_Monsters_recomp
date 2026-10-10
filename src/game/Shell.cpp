#include "common.h"
#include "memory_stack.h"
#include "game/shell.h"
#include "game/monster_select.h"

extern "C" int printf(const char *, ...);
extern "C" int sprintf(char *, const char *, ...);
extern "C" char *strcpy(char *, const char *);

struct sceCdlFILE {
    unsigned lsn;
    unsigned size;
    char name[16];
    char date[8];
};
struct _dbheader;

int zipInflateAll(char *name, void *dest);
void fileOnlyNgpFile(char *dest, int size);
char *fileTrimPath(char *path);
void fileAddName(char *name);
void fileCdSearchFile(sceCdlFILE *f, char *path);
char *fileGetTimeString(void);
void informProgressBar(float f);
void initProgressBar(int a, int b, int c);
void viewSetNumViews(int n);
enum _viewports { VIEWPORT_7 = 7 };
void viewCreate(_viewports vp, int i);
extern int doTweaks;
extern int gUseUnifiedView;
extern int gOkToDrawProgressBar;
extern int onBitMonsters[] __asm__("on_bit_006EF8E8");
extern int NUM_LIVES[];
#define GM(o) (*(int *)((char *)game + (o)))
#define SHI(o) (*(int *)((char *)this + (o)))
extern "C" int snd_StreamSafeCdSync(int mode);
extern "C" int sceCdSync(int mode);
extern "C" int sceCdDiskReady(int mode);
void timerInit(int hz);
struct _cs;
struct _fvector;
void hierSetTraversalCallback(void (*cb)(_cs *, unsigned, unsigned, float (&)[4][4], _fvector *));
void hierInit(void);
void viewInit(void);
void viewSetNumViews(int n);
void psBlockerInit(void);
class TagList {
public:
    void init(void);
};
struct Camera {
    enum CameraPOV { POV_0, POV_1, POV_2, POV_3 };
};
class Cameras {
public:
    static void LeaveUnifiedView(unsigned id);
    static void Init(void);
    static void SetCameraPOV(int view, Camera::CameraPOV pov);
};
extern int camerasNum __asm__("_7Cameras$m_numCameras");

void dbsRelocateFileZero(_vramAddrs v, bool b);
void dbInitDb(_dbheader *db, _vramAddrs v);

extern char D_00731500[];                 /* path of the file being loaded */
extern char whichLevel[];                 /* name of the command-line level */
extern char D_006F81D0[];                 /* "ngp" */
extern char D_006F81C0[];                 /* "rtx" */
extern char D_006F81C8[];                 /* "tex" */
extern char D_006F81E0[];                 /* disc root prefix */
extern char D_006F81E8[];                 /* name suffix */
extern char GameLevelNames_006EF748[][10];
extern char MonsterLongNames_006EF860[][8];
extern char D_006F81D8[];                 /* "%s%d" */
extern char D_006F81A8[];                 /* "%s" */
extern char D_006EF458[];                 /* "Unknown monster (%d) for player %d..." */
extern char D_006EF498[];                 /* "Unknown monster (%d) for ai %d..." */
int fileReadf(char *name, void *dest);
void fileAddNgpFile(char *addr, int size);
char *getNextNgpLoadAddr(void);
char *getNextResLoadAddr(void);
char *getNextTexLoadAddr(void);
void fileAddResFile(char *addr, int size);
void fileAddTexFile(char *addr, int size);
void fileOnlyResFile(char *dest, int size);
void fileOnlyTexFile(char *dest, int size);
struct QwData;
void texmResInit(_vramAddrs v, QwData *q);
void texmInit(_vramAddrs v);
extern int UseCommandLineLevel;
extern int gWhichMicroSection;
extern int gWhichMacroSection;


#define SH(o) (*(int *)((char *)shell + (o)))

/* Boot and session flow of the whole game:
   boot -> (optional intro/outro movie) -> front-end menus (userintMain) -> a play session: load the level, the monsters and their
   textures, build the world, create the players, then run rtMain frame by frame until the session ends, restarting the level on request. */
extern "C" void __main(void);
int mathfRand(int lo, int hi);
extern "C" int sceGsSyncPath(int mode, int timeout);
void displayDialog(int which);
class Destructibles;
class TheGame;
extern Destructibles *destructibles;
extern TheGame *game;
extern char shellDestructibles[];
extern char shellInfo[];
extern char shellGame[];
extern char resetObj[];
extern int exitFromAdvStory;
extern int currScreen;
extern int needIntro;
extern int needOutro;
extern int nextMovie;
extern int g_frame;
extern int g_onStartupFrame;
extern char whichLevel[];
extern char _10CrushLevel$instance[] __asm__("_10CrushLevel$instance");
extern char _12BigShotLevel$instance[] __asm__("_12BigShotLevel$instance");
extern char _14DodgeBallLevel$instance[] __asm__("_14DodgeBallLevel$instance");

class SoundManager {
public:
    void doOneTimeInit(void);
    void loadShellSoundBanks(void);
    void initSoundManager(void);
    void manageReverb(void);
    void loadRTSoundBanks(void);
    void initVagStreaming(void);
    void disableSoundForCinema(void);
};
class ShellSound {
public:
    void terminateShellSound(void);
    void resetShellSoundFlags(void);
};
class StreamingSoundManager {
public:
    void initStreamingSoundManager(void);
};
class Hud {
public:
    void initAfter(int i);
    void addMessage(int id, int arg);
};
class resetcom {
public:
    void setFileName(char *name, bool b);
};
class BigShotLevel {
public:
    void initAfterDbLoad(void);
};
class CrushLevel {
public:
    void initAfterDbLoad(void);
};
class DodgeBallLevel {
public:
    void initAfterDbLoad(void);
};
class TheGame {
public:
    void Init(void);
    void InitBeforeDbLoad(void);
    void InitAfterDbLoad(void);
    void ResetLevel(void);
    void UnpauseLevel(void);
    void UpdatePadTweaks(void);
    void gameReestablishViews(void);
    void SetPlayerMonster(int pIdx, int type, int dup, int view, int skin);
    void SetAIMonster(int aiIdx, int type, int dup, int skin);
    int GetNumAIsAlive(void);
};
class CsPool {
public:
    static void init(void);
};
void ResolveCommandLineArguments(int argc, char **argv);
void fileInitializeCd(void);
void inputInit(void);
void hierSetDmaIntHandler(void);
void inputSetInputMode(int m);
void uiInit(void);
void uiMain(int i);
int userintMain(void);
int rtMain(bool first);
void fontInit(_vramAddrs v, int i);
void threeMileInitAi(void);

#ifdef NON_MATCHING
/* 33/356 words: untuned, from the m2c draft + asm */
extern "C" int main(int argc, char **argv)
{
    int i;

    __main();
    destructibles = (Destructibles *)shellDestructibles;
    SH(0x2BB0) = 0;
    shell = (Shell *)shellInfo;
    game = (TheGame *)shellGame;
    ResolveCommandLineArguments(argc, argv);
    fileInitializeCd();
    inputInit();
    CsPool::init();
    ((TheGame *)game)->Init();
    *(int *)(shellGame + 0x1203CC) = 0;
    hierSetDmaIntHandler();
    ((SoundManager *)((char *)shell + 0x2C30))->doOneTimeInit();
    needIntro = 1;
    needOutro = 0;
    g_frame = 0;
    g_onStartupFrame = 0;
    shell->InitialMemCardScreen();
    for (;;) {
        ((SoundManager *)((char *)shell + 0x2C30))->loadShellSoundBanks();
        uiInit();
        do {
            printf("Entering movie selection\n");
            shell->m_inMenus = 1;
            if (needIntro != 0 || needOutro != 0) {
                shell->m_mode = 0x3F;
                printf("Intro or Outro needed\n");
                *(int *)((char *)game + 0x1203C8) = 0x3F;
                shell->BootInitUi();
                inputSetInputMode(0);
                shell->InitBeforeUiDbLoad();
                if (needIntro != 0) {
                    nextMovie = 3;
                    printf("Playing Intro movie\n");
                    uiMain(0);
                }
                if (needOutro != 0) {
                    printf("Playing Outro movie\n");
                    uiMain(1);
                }
                needIntro = 0;
                needOutro = 0;
            } else {
                ((TheGame *)game)->Init();
            }
            shell->m_mode = 0x40;
            *(int *)((char *)game + 0x1203C8) = 0x40;
            shell->BootInitUserint();
            inputSetInputMode(0);
            shell->InitBeforeUserintDbLoad();
            shell->LoadUserintDB();
            shell->LoadUserintTexture();
            dbsRelocateFileZero(shell->getVramAddr(), UseCommandLineLevel);
            dbInitDb((_dbheader *)0xA00000, shell->getVramAddr());
            if (exitFromAdvStory == 1) {
                printf("Just came out of adventure mode Story\n");
                currScreen = exitFromAdvStory;
            }
        } while (userintMain() != 0);
        ((ShellSound *)((char *)shell + 0x2918))->terminateShellSound();
        ((SoundManager *)((char *)shell + 0x2C30))->initSoundManager();
        if (shell->m_inMenus == 0)
            continue;
        do {
            shell->m_inSession = 1;
            ResolveCommandLineArguments(argc, argv);
            ((resetcom *)resetObj)->setFileName(whichLevel, UseCommandLineLevel);
            shell->InitRTState();
            ((SoundManager *)((char *)shell + 0x2C30))->disableSoundForCinema();
            shell->BootInitGame();
            inputSetInputMode(1);
            fontInit((_vramAddrs)0x69840, 1);
            ((TheGame *)game)->InitBeforeDbLoad();
            shell->LoadLevelFiles();
            shell->FinishLoadBar();
            shell->FadeScreen(0, false, 0, 0, 0, 0, 0x80, 2);
            ((TheGame *)game)->InitAfterDbLoad();
            shell->InitPlayers();
            ((SoundManager *)((char *)shell + 0x2C30))->manageReverb();
            for (i = 0; i < 4; i++)
                ((Hud *)((char *)game + i * 0x2E0))->initAfter(i);
            switch (shell->m_mode) {
            case 8:
                ((BigShotLevel *)_12BigShotLevel$instance)->initAfterDbLoad();
                break;
            case 9:
                ((CrushLevel *)_10CrushLevel$instance)->initAfterDbLoad();
                break;
            case 7:
                ((DodgeBallLevel *)_14DodgeBallLevel$instance)->initAfterDbLoad();
                break;
            }
            if (shell->m_levelNum == 6 && shell->m_mode == 1)
                threeMileInitAi();
            ((TheGame *)game)->UpdatePadTweaks();
            ((SoundManager *)((char *)shell + 0x2C30))->initVagStreaming();
            ((SoundManager *)((char *)shell + 0x2C30))->loadRTSoundBanks();
            ((TheGame *)game)->gameReestablishViews();
            ((StreamingSoundManager *)((char *)game + 0x1204C0))->initStreamingSoundManager();
            SH(0x2B60) = 0;
            SH(0x2B64) = 0;
            if (shell->m_inSession != 0) {
                bool first = true;

                do {
                    int r;

                    SH(0x2BA4) = 0;
                    r = rtMain(first);
                    first = false;
                    shell->EvaluateGameStatus(r);
                    if (shell->m_inSession != 0) {
                        if (shell->m_restart != 0)
                            ((TheGame *)game)->ResetLevel();
                        else
                            ((TheGame *)game)->UnpauseLevel();
                    }
                } while (shell->m_inSession != 0);
            }
            ((ShellSound *)((char *)shell + 0x2918))->resetShellSoundFlags();
        } while (shell->m_inMenus != 0);
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Shell", main);
#endif
void Shell::SelectAI(void)
{
}
INCLUDE_ASM("asm/nonmatchings/game/Shell", D_006EF180);
#ifdef NON_MATCHING
extern int randomAiPick __asm__("temp.2832"); /* last monster drawn (a function-local static in the original) */

/* untuned: 8/87 words (register allocation); tools/difftest.py 260/260 with mathfRand draws injected */
/* Gives every AI slot a distinct random monster among those still selectable, as (monster << 5) codes. */
void Shell::RandomlySelectAI(void)
{
    int aiMonsters[10] = {9 << 5, 3 << 5, 5 << 5, 1 << 5, 2 << 5, 10 << 5, 4 << 5, 8 << 5, 7 << 5, 11 << 5};
    SelectSlot *s;
    int i = 0;

    while (i < m_numAIs) {
        randomAiPick = mathfRand(0, 9);
        s = &monsterSelectMode[randomAiPick * 4];
        if (s->state == 1 && s->taken == 1) {
            s->taken = 0;
            m_monsterSel[4 + i] = aiMonsters[randomAiPick];
            printf("Assigning Monster %i Model %i to AI Slot #%i\n", aiMonsters[randomAiPick], m_costume[2 + randomAiPick], i);
            i++;
        }
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Shell", RandomlySelectAI__5Shell);
#endif
#ifdef NON_MATCHING
struct SeenType {
    int type;
    int count;
};

class Destructibles {
public:
    void rebuildCanyon2Pillars(void);
    void rebuildCapitolPillars(void);
};
class HealthMeter {
public:
    void creditFull(void);
};
struct _cs;
void hierSetCsDrawMe(_cs *cs, unsigned char v);
extern int canyon2LevelProgression;
extern int craterSwitched;
extern int numDeadHeads;
extern char plantBoss[];
extern char *assBoss;
extern float assBossTier1Health;
extern float assBossTier2Health;
extern char finalBoss[];
extern char D_006F81A0[]; /* "AI %d\n" */

/* Instances the AI monsters of the level. `seen` is the table of monster types already placed (players first): every further monster
   of the same type gets the next duplicate number, which selects the model copy (see TheGame::GetMonsterFromName). */
static void spawnAIs(Shell *sh, SeenType *seen, int *nSeen, bool log)
{
    int j;
    int sel;

    for (j = 0; j < *(int *)((char *)sh + 0x2BB8); j++) {
        int *monsterSel = (int *)((char *)sh + 0x2948);
        int k;
        int found = 0;
        int at = 0;
        int dup;

        sel = monsterSel[j];
        for (k = 0; k < *nSeen; k++) {
            if (seen[k].type == sel) {
                at = k;
                found = 1;
            }
        }
        if (found) {
            dup = ++seen[at].count;
        } else {
            dup = 0;
            seen[*nSeen].type = sel;
            (*nSeen)++;
        }
        game->SetAIMonster(j, sel, dup, *(int *)((char *)sh + 0x2BE8 + j * 4));
        if (log)
            printf("set ai monster %d %d\n", sel, dup);
    }
}

void Shell::InitPlayers(void)
{
    SeenType seen[16];
    int n = 0;
    int i;
    int k;

    printf("num play: %d num AI: %d\n", m_numPlayers, m_numAIs);
    for (i = 0; i < m_numPlayers; i++)
        printf("PLAYER %d\n", m_monsterSel[i]);
    for (i = 0; i < m_numAIs; i++)
        printf(D_006F81A0, m_monsterSel[4 + i]);
    for (i = 0; i < 16; i++)
        seen[i].count = 0;
    *(int *)((char *)game + 0x1203D8) = m_numPlayers;
    for (i = 0; i < m_numPlayers; i++) {
        int sel = m_monsterSel[i];
        int found = 0;
        int at = 0;
        int dup;

        for (k = 0; k < n; k++) {
            if (seen[k].type == sel) {
                at = k;
                found = 1;
            }
        }
        if (found) {
            dup = ++seen[at].count;
        } else {
            dup = 0;
            seen[n].type = sel;
            n++;
        }
        game->SetPlayerMonster(i, sel, dup, i, m_costume[i != 0]);
        printf("set player monster %d %d\n", sel, dup);
        *(int *)((char *)(*(char **)((char *)game + 0x120380 + i * 4)) + 0x3C) = 0;
    }
    if (m_levelNum == 3 && m_mode == 1) {
        InitPlayerLives();
        if (canyon2LevelProgression == 0) {
            *(int *)((char *)game + 0x1203E0) = m_numAIs;
            spawnAIs(this, seen, &n, false);
        } else if (canyon2LevelProgression == 3) {
            if (assBoss != 0) {
                *(float *)(assBoss + 0x44C) = *(float *)(assBoss + 0x448);
                ((Destructibles *)destructibles)->rebuildCanyon2Pillars();
            }
        } else if (canyon2LevelProgression == 6) {
            if (assBoss != 0) {
                float max = *(float *)(assBoss + 0x448);
                float tier;

                if (assBossTier2Health < *(float *)(assBoss + 0x44C) / max)
                    tier = assBossTier1Health - 0.01f;
                else
                    tier = assBossTier2Health - 0.01f;
                *(float *)(assBoss + 0x44C) = max * tier;
                ((Destructibles *)destructibles)->rebuildCanyon2Pillars();
            }
        }
    } else if (m_levelNum == 6 && m_mode == 1) {
        InitPlayerLives();
        if (craterSwitched != 0) {
            char *p;

            numDeadHeads = 0;
            for (p = plantBoss; p < plantBoss + 0x6F0; p += 0x250) {
                if (*(float *)(p + 0x3C) <= 0.0f)
                    *(int *)(p + 0x38) = 0xE;
                *(int *)(p + 0x34) = 1;
                *(float *)(p + 0x3C) = 20.0f;
            }
        } else {
            *(int *)((char *)game + 0x1203E0) = m_numAIs;
            spawnAIs(this, seen, &n, false);
        }
    } else if (m_levelNum == 11 && m_mode == 1) {
        InitPlayerLives();
        if (*(int *)(finalBoss + 0x1C) == 0) {
            ((Destructibles *)destructibles)->rebuildCapitolPillars();
            *(float *)(finalBoss + 0x14) = (float)*(int *)((char *)game + 0x1203CC) * 75.0f + 100.0f;
            *(int *)((char *)game + 0x1203E0) = m_numAIs;
            spawnAIs(this, seen, &n, false);
        } else if (*(int *)(finalBoss + 0x1C) == 1) {
            ((HealthMeter *)(*(char **)(finalBoss + 0x74) + 0x448))->creditFull();
        } else if (*(int *)(finalBoss + 0x1C) == 2) {
            ((HealthMeter *)(*(char **)(finalBoss + 0x78) + 0x448))->creditFull();
        }
    } else {
        *(int *)((char *)game + 0x1203E0) = m_numAIs;
        spawnAIs(this, seen, &n, true);
        if (m_mode == 4 || m_mode == 6) {
            for (i = 0; i < m_numAIs; i++) {
                if (m_mode == 4 || m_mode == 6) {
                    char *m = *(char **)((char *)game + 0x120390 + i * 4);

                    if (i != 0) {
                        *(int *)(m + 0x18) = 0;
                        hierSetCsDrawMe(*(_cs **)(m + 0xC), 0);
                    } else {
                        *(int *)(m + 0x18) = 2;
                        hierSetCsDrawMe(*(_cs **)(m + 0xC), 1);
                    }
                }
            }
            *(int *)((char *)this + 0x2A70) = 0;
        }
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Shell", InitPlayers__5Shell);
#endif
#ifdef NON_MATCHING
void Shell::InitBeforeUiDbLoad(void)
{
    SHI(0x2C14) = 0x2710;
    SHI(0x2C18) = 0;
    SHI(0x2BB4) = 0;
    m_roundsDecided = 0;
    hierInit();
    viewInit();
    viewSetNumViews(1);
    viewCreate((_viewports)4, 0);
    camerasNum = 1;
    Cameras::Init();
    Cameras::SetCameraPOV(0, Camera::POV_0);
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Shell", InitBeforeUiDbLoad__5Shell);
#endif
void Shell::InitBeforeUserintDbLoad(void)
{
    hierInit();
    viewInit();
    psBlockerInit();
    viewSetNumViews(1);
    viewCreate((_viewports)3, 0);
    camerasNum = 1;
    Cameras::Init();
    Cameras::SetCameraPOV(0, Camera::POV_0);
    MemoryStack::global.clear();
    ((TagList *)this)->init();
}
INCLUDE_ASM("asm/nonmatchings/game/Shell", __5Shell);
INCLUDE_ASM("asm/nonmatchings/game/Shell", _$_5Shell);
/* Hands the shell's choices (level, mode, player count) to TheGame before the realtime loop starts. */
void Shell::InitRTState(void)
{
    char *g;

    InitPlayerLives();
    g = (char *)game + 0x120000;
    *(int *)(g + 0x3D0) = m_levelNum;
    *(int *)(g + 0x3C8) = m_mode;
    *(int *)(g + 0x3D8) = m_numPlayers;
}
#ifdef NON_MATCHING
extern int levelMonsters[][12];
extern int levelMonsterModels[][4];
extern "C" void snd_StopAllSounds(void);

/* Decides what happens after rtMain returned `r` (2 = dialog dismissed, 5 = quit the session, otherwise the level ended) by asking the game mode's
   own evaluator. Modes: 0 demo (random level and monster), 1 story, 2 challenge, 3 free for all (with or without AIs), 4 endurance, 5 co-op,
   6 elimination, 7 dodgeball, 8 bigshot, 9 crush, 11 online battle. */
void Shell::EvaluateGameStatus(int r)
{
    if (r == 2) {
        *(char *)(*(char **)((char *)game + 0x98) + 0xC) = 0;
        displayDialog(0);
    } else if (r == 5) {
        m_inSession = 0;
        m_inMenus = 0;
        *(int *)((char *)game + 0x1204C0 + 0x109C) = 0;
        snd_StopAllSounds();
        return;
    } else {
        switch (m_mode) {
        case 0: {
            int i, n;

            m_inSession = 0;
            m_inMenus = 1;
            m_levelNum = mathfRand(1, 0xB);
            m_monsterSel[0] = mathfRand(1, 0xC) << 5;
            if (m_monsterSel[0] == 0xC0 || m_monsterSel[0] == 0x180)
                m_monsterSel[0] = 0x20;
            ((TheGame *)game)->SetPlayerMonster(0, m_monsterSel[0], 0, 0, m_costume[0]);
            n = *(int *)((char *)this + 0x29E0 + m_levelNum * 4);
            m_numAIs = n;
            for (i = 0; i < n; i++) {
                m_monsterSel[4 + i] = levelMonsters[m_levelNum][i];
                m_costume[2 + i] = levelMonsterModels[m_levelNum][i];
            }
            DisplayLoadBackground(false);
            break;
        }
        case 1:
            EvaluateOnePlayerStoryStatus(r);
            break;
        case 2:
            EvaluateOnePlayerChallengeStatus(r);
            break;
        case 4:
            EvaluateOnePlayerEnduranceStatus(r);
            break;
        case 5:
            EvaluateTwoPlayerCoopStatus(r);
            break;
        case 7:
            EvaluateDodgeBallStatus(r);
            break;
        case 3:
            if (*(int *)((char *)game + 0x1203E0) != 0) {
                EvaluateMultiPlayerBattleStatusAI(r);
                break;
            }
            /* fall through */
        case 6:
            EvaluateMultiPlayerBattleStatusNoAI(r);
            break;
        case 8:
            EvaluateBigShotStatus(r);
            break;
        case 9:
            EvaluateCrushStatus(r);
            break;
        case 11:
            EvaluateOnlineBattleStatus(r);
            break;
        default:
            printf("ERROR - Shell::EvaluateGameStatus does not recognize %i as a Game Mode!!!!
", m_mode);
            break;
        }
    }
    if (m_restart == 1) {
        DisplayLoadBackground(false);
        m_restart = 0;
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Shell", EvaluateGameStatus__5Shelli);
#endif
#ifdef NON_MATCHING
class Monster {
public:
    void playerInit(void);
    void aiInit(void);
};

#define SLOT(i) ((Monster *)((char *)game + 0xB80 + (i) * 0x11190))
#define HEALTH(m) (*(float *)((char *)(m) + 0x44C))

class TokenManager {
public:
    int grandTotal(void);
};
extern int craterSwitched;
extern int numDeadHeads;


/* Story mode, after rtMain returned `r`: 0 = level cleared, 1 = the player died, 3 = quit, 4 = campaign finished.
   A death with no AI left alive counts as a clear (on level 6 only once more than 2 dead heads are down after the crater switched).
   A clear pays a per-level bonus (0xC8, 0x190, 0x258, 0x190, 0x190, 0x5DC, 0x190, 0x190, 0x190, 0x1F4 for levels 1..10) times the
   difficulty (10/15/20 from the match mode), then loads the next level; the last level (11) and r == 4 end the campaign with 0xBB8 * difficulty. */
void Shell::EvaluateOnePlayerStoryStatus(int r)
{
    int diff = 0;
    int finale = 0;

    SHI(0x2B34) = 0;
    SHI(0x2B38) = 0;
    SHI(0x2B3C) = 0;
    switch (GM(0x1203CC)) {
    case 0:
        diff = 10;
        break;
    case 1:
        diff = 15;
        break;
    case 2:
        diff = 20;
        break;
    }
    if (r == 1) {
        if (((TheGame *)game)->GetNumAIsAlive() == 0) {
            r = 0;
            if (GM(0x1203D0) == 6 && craterSwitched != 0)
                r = numDeadHeads > 2 ? 0 : 1;
        }
        if (r == 1) {
            if (--m_lives[0] > 0) {
                SLOT(GM(0x1203E8))->playerInit();
                if (gUseUnifiedView != 0)
                    Cameras::LeaveUnifiedView(0xC);
                m_inSession = 1;
                if (m_lives[0] == 1)
                    ((Hud *)game)->addMessage(0x26, 0);
                else if (m_lives[0] == 2)
                    ((Hud *)game)->addMessage(0x25, 0);
            } else {
                displayDialog(1);
                if (m_restart == 1) {
                    DisplayLoadBackground(false);
                    m_restart = 0;
                }
            }
            return;
        }
    }
    if (r == 0) {
        static const int bonus[10] = { 0xC8, 0x190, 0x258, 0x190, 0x190, 0x5DC, 0x190, 0x190, 0x190, 0x1F4 };
        int lvl = m_levelNum;

        if (lvl == 11) {
            finale = 1;
        } else {
            int i, n, score;

            if (lvl >= 1 && lvl <= 10)
                SHI(0x2B34) = diff * bonus[lvl - 1];
            m_levelNum = ++lvl;
            n = SHI(0x29E0 + lvl * 4);
            m_numAIs = n;
            for (i = 0; i < n; i++) {
                m_monsterSel[4 + i] = levelMonsters[m_levelNum][i];
                m_costume[2 + i] = levelMonsterModels[m_levelNum][i];
            }
            score = ((TokenManager *)((char *)game + 0x11C370))->grandTotal() + SHI(0x2B34);
            SHI(0x2B28) = score;
            SHI(0x2B24) += score;
            displayDialog(2);
            DisplayLoadBackground(false);
            return;
        }
    } else if (r == 4) {
        finale = 1;
    }
    if (finale) {
        m_inSession = 0;
        SHI(0x2B34) = diff * 0xBB8;
        SHI(0x2B28) = ((TokenManager *)((char *)game + 0x11C370))->grandTotal() + SHI(0x2B34);
        if (SHI(0x2C18) == 0)
            SHI(0x2B28) = SHI(0x2B28) + SHI(0x2C14);
        SHI(0x2B24) += SHI(0x2B28);
        displayDialog(3);
        GenesisMovie();
    } else if (r == 3) {
        m_inMenus = 0;
        m_inSession = 0;
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Shell", EvaluateOnePlayerStoryStatus__5Shelli);
#endif
#ifdef NON_MATCHING
/* Brings the slot back: slot 0 is the player, the others AIs. */
static void reinitSlot(int idx)
{
    if (idx == 0)
        SLOT(0)->playerInit();
    else
        SLOT(idx)->aiInit();
}

/* Challenge mode: m_killTarget is the kill target (0 = none, the round just restarts). `deadIdx` is the last dead slot found, `killer` the monster
   number of a living monster that killed one; reaching the target (or, for target 1, any win) ends the match with the winners dialog (5). */
void Shell::EvaluateOnePlayerChallengeStatus(int r)
{
    int killer = -1;
    int deadIdx = 0;
    int target;
    int i;

    if (r == 3) {
        m_inMenus = 0;
        m_inSession = 0;
        return;
    }
    target = m_killTarget;
    for (i = 0; i < GM(0x1203D4); i++) {
        char *slot = (char *)SLOT(i);
        char *by;

        if (slot[0xE8] != 0) {
            deadIdx = i;
            by = *(char **)(slot + 0x846C);
            if (by != 0 && by[0xE8] == 0) {
                killer = *(int *)(by + 0x28);
                break;
            }
        }
    }
    if (target == 0) {
        reinitSlot(deadIdx);
        shell->m_inSession = 1;
        shell->m_inMenus = 0;
    } else if (target == 1) {
        if (killer == -1) {
            reinitSlot(deadIdx);
            m_inMenus = 0;
            m_inSession = 1;
        } else if (*(int *)((char *)SLOT(killer) + 0x3C) > 0) {
            GM(0x12043C) = killer;
            GM(0x120440) = deadIdx;
            displayDialog(5);
        } else {
            reinitSlot(deadIdx);
            m_inMenus = 0;
            m_inSession = 1;
        }
    } else if (killer == -1) {
        reinitSlot(deadIdx);
        m_inMenus = 0;
        m_inSession = 1;
    } else if (*(int *)((char *)SLOT(killer) + 0x3C) >= target) {
        GM(0x12043C) = killer;
        displayDialog(5);
    } else {
        reinitSlot(deadIdx);
    }
    if (gUseUnifiedView != 0 && deadIdx == 0)
        Cameras::LeaveUnifiedView(0x1E);
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Shell", EvaluateOnePlayerChallengeStatus__5Shelli);
#endif
void Shell::EvaluateTwoPlayerCoopStatus(int r)
{
}
#ifdef NON_MATCHING
/* TheGame and Monster fields by offset (this TU has its own partial TheGame; names as in game/game.h). */
#define GAME_MONSTER(i) (*(Monster **)((char *)game + 0x120380 + (i) * 4)) /* TheGame::m_monsters */
#define VIEW_SLOT(i) GM(0x1203E8 + (i) * 4)                                /* TheGame::m_viewSlot */
#define GAME_MODE GM(0x1203C8)                                             /* TheGame::m_gameMode */
#define ROUND_WINNER GM(0x12043C)                                          /* TheGame::m_won[0] */
#define ROUND_LOSER GM(0x120440)                                           /* TheGame::m_won[1]: the player who respawns */
#define WINS(m) (*(int *)((char *)(m) + 0x3C))                             /* Monster::m_winsThisGame */
#define KILLER(m) (*(Monster **)((char *)(m) + 0x846C))                    /* Monster::m_killer */

/* untuned: 18/364 words (game reloads and scheduling); tools/difftest.py 300/300 on a two-player state */
/* Two-player battle without AIs, called when a round ends (`r` 1) or the players quit (`r` 3).
 * Each player whose monster died loses a life; with exactly one monster standing:
 *   mode 6 (elimination): the winner scores if the loser was killed by someone, and the loser comes back with the same
 *          monster while it has continues left; with none left the match is over (dialog 0xB, m_survivor = winner).
 *   mode 3: first to m_killTarget wins (dialog 5), otherwise the loser comes back.
 * With both monsters down, both come back (game mode 6 spends a continue each; out of continues ends the match). */
void Shell::EvaluateMultiPlayerBattleStatusNoAI(int r)
{
    int winner = 0;
    int alive = 0;
    int i;

    if (r == 1) {
        for (i = 0; i < shell->m_numPlayers; i++) {
            if (HEALTH(SLOT(VIEW_SLOT(i))) <= 0.0f && m_lives[i] > 0)
                m_lives[i]--;
            if (HEALTH(SLOT(VIEW_SLOT(i))) > 0.0f) {
                winner = i;
                alive++;
                ROUND_LOSER = winner == 0;
            }
        }
        m_roundsPlayed++;
        if (alive == 1) {
            m_roundsDecided++;
            if (m_mode == 6) {
                if (KILLER(GAME_MONSTER(ROUND_LOSER)) != 0) {
                    if (winner != 0)
                        m_wins[1]++;
                    else
                        m_wins[0]++;
                    WINS(GAME_MONSTER(0)) = m_wins[0];
                    WINS(GAME_MONSTER(1)) = m_wins[1];
                }
                if (ROUND_LOSER == 0 && m_continues[0] == 0) {
                    m_survivor = 1;
                    displayDialog(0xB);
                } else if (ROUND_LOSER == 1 && m_continues[1] == 0) {
                    m_survivor = 0;
                    displayDialog(0xB);
                } else {
                    displayDialog(8);
                    if (ROUND_LOSER == 0)
                        m_continues[0]--;
                    else
                        m_continues[1]--;
                    i = ROUND_LOSER;
                    game->SetPlayerMonster(i, m_monsterSel[i], 0, i, i != 0 ? m_costume[1] : m_costume[0]);
                    GAME_MONSTER(ROUND_LOSER)->playerInit();
                    WINS(GAME_MONSTER(0)) = m_wins[0];
                    WINS(GAME_MONSTER(1)) = m_wins[1];
                }
                GM(0x120460) = 1;
                GM(0x120458) = 0;
            } else if (m_mode == 3) {
                ROUND_WINNER = winner;
                if (m_killTarget != 0 && WINS(GAME_MONSTER(winner)) >= m_killTarget) {
                    displayDialog(5);
                    return;
                }
                GAME_MONSTER(ROUND_LOSER)->playerInit();
                m_inSession = 1;
                m_inMenus = 1;
            }
        } else if (alive == 0) {
            if (GAME_MODE == 6) {
                if (m_continues[0] == 0 || m_continues[1] == 0) {
                    if (m_continues[0] == 0 && m_continues[1] == 0) {
                        displayDialog(6);
                    } else {
                        m_survivor = m_continues[0] == 0;
                        displayDialog(0xB);
                    }
                    return;
                }
                ROUND_LOSER = 0;
                displayDialog(8);
                m_continues[0]--;
                game->SetPlayerMonster(0, m_monsterSel[0], 0, 0, m_costume[0]);
                GAME_MONSTER(0)->playerInit();
                WINS(GAME_MONSTER(0)) = m_wins[0];
                if (shell->m_inSession == 0)
                    return;
                ROUND_LOSER = 1;
                displayDialog(8);
                m_continues[1]--;
                game->SetPlayerMonster(1, m_monsterSel[1], 0, 1, m_costume[1]);
                GAME_MONSTER(1)->playerInit();
                WINS(GAME_MONSTER(1)) = m_wins[1];
            } else if (GAME_MODE == 3) {
                GAME_MONSTER(0)->playerInit();
                GAME_MONSTER(1)->playerInit();
                if (gUseUnifiedView != 0)
                    Cameras::LeaveUnifiedView(0x11);
                m_inSession = 1;
                m_inMenus = 1;
            }
        }
    } else if (r == 3) {
        m_inMenus = 0;
        m_inSession = 0;
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Shell", EvaluateMultiPlayerBattleStatusNoAI__5Shelli);
#endif
#ifdef NON_MATCHING
/* Brings every slot with no health left back: slots 0 and 1 are players, the rest AIs. */
static void respawnDead(void)
{
    int i;

    for (i = 0; i < GM(0x1203D4); i++) {
        Monster *m = SLOT(i);

        if (HEALTH(m) <= 0.0f) {
            if (i < 2)
                m->playerInit();
            else
                m->aiInit();
        }
    }
}

/* Free-for-all with AIs, called when a round ends. `r` 3 = quit. Otherwise find who is still alive and whether a living monster killed a dead one:
   with a kill target in m_killTarget the first monster to reach it wins (m_won[0], dialog 5), else everyone dead comes back and the round restarts. */
void Shell::EvaluateMultiPlayerBattleStatusAI(int r)
{
    int killer = -1;
    int alive = 0;
    int i;

    if (r == 3) {
        m_inMenus = 0;
        m_inSession = 0;
        return;
    }
    for (i = 0; i < GM(0x1203D8); i++)
        if (HEALTH(*(Monster **)((char *)game + 0x120380 + i * 4)) > 0.0f)
            alive++;
    for (i = 0; i < GM(0x1203D4); i++) {
        char *slot = (char *)SLOT(i);
        char *by;

        if (slot[0xE8] != 0 && (by = *(char **)(slot + 0x846C)) != 0 && by[0xE8] == 0) {
            killer = *(int *)(by + 0x28);
            break;
        }
    }
    if (alive != 0 || killer != -1) {
        int target = m_killTarget;

        if (target == 0 || killer == -1) {
            respawnDead();
        } else if (*(int *)((char *)SLOT(killer) + 0x3C) >= target) {
            GM(0x12043C) = killer;
            displayDialog(5);
            return;
        } else {
            respawnDead();
        }
    } else {
        SLOT(0)->playerInit();
        SLOT(1)->playerInit();
        for (i = 0; i < GM(0x1203E0); i++) {
            Monster *m = *(Monster **)((char *)game + 0x120390 + i * 4);

            if (HEALTH(m) <= 0.0f)
                m->aiInit();
        }
        if (gUseUnifiedView != 0)
            Cameras::LeaveUnifiedView(0x12);
    }
    m_inMenus = 0;
    m_inSession = 1;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Shell", EvaluateMultiPlayerBattleStatusAI__5Shelli);
#endif
/* 1 = show the endurance dialog, 3 = the player quit; anything else is unexpected and also ends the session. */
void Shell::EvaluateOnePlayerEnduranceStatus(int r)
{
    if (r == 1) {
        sceGsSyncPath(0, 0);
        displayDialog(9);
        return;
    }
    if (r == 3) {
        m_inSession = 0;
        m_inMenus = 0;
        *(int *)((char *)game + 0xBBC) = 0;
        return;
    }
    printf("An endurance mode game ended for reasons other than player dying or quitting!! AAAhhh
");
    m_inSession = 0;
    m_inMenus = 0;
}
void Shell::EvaluateBigShotStatus(int r)
{
    displayDialog(0xD);
    if (m_restart == 1) {
        DisplayLoadBackground(false);
        m_restart = 0;
    }
}
void Shell::EvaluateCrushStatus(int r)
{
    displayDialog(0xE);
    if (m_restart == 1) {
        DisplayLoadBackground(false);
        m_restart = 0;
    }
}
void Shell::EvaluateDodgeBallStatus(int r)
{
    displayDialog(0xF);
    if (m_restart == 1) {
        DisplayLoadBackground(false);
        m_restart = 0;
    }
}
void Shell::EvaluateOnlineBattleStatus(int r)
{
}

/* Every player starts with the lives of the current game mode (NUM_LIVES[mode]) in m_lives. */
void Shell::InitPlayerLives(void)
{
    int i;
    int *p = m_lives;

    for (i = 3; i >= 0; i--)
        *p++ = NUM_LIVES[m_mode];
}
/* Boot of the front-end graphics side: GS setup (mode 0), TheGame::Init, 60 Hz timer and the shell font at VRAM 0x78840. */
void Shell::BootInitUi(void)
{
    InitGS(0);
    ((TheGame *)game)->Init();
    timerInit(0x3C);
    fontInit((_vramAddrs)0x78840, 0);
    hierSetTraversalCallback(0);
}
/* Waits for the disc, inflates SHELL.RTX into 0x1B7FFF0 and sets up the GS for the userint (mode 1) with the font at VRAM 0x69880. */
void Shell::BootInitUserint(void)
{
    snd_StreamSafeCdSync(0);
    sceCdSync(0);
    sceCdDiskReady(0);
    zipInflateAll("\\SHELL\\SHELL.RTX;1", (void *)0x01B7FFF0);
    InitGS(1);
    timerInit(0x3C);
    fontInit((_vramAddrs)0x69880, 1);
    doTweaks = 0;
    hierSetTraversalCallback(0);
}
INCLUDE_ASM("asm/nonmatchings/game/Shell", BootInitUserint1__5Shell);
#ifdef NON_MATCHING
void Shell::BootInitGame(void)
{
    timerInit(Use30HzMode() == 0 ? 0x3C : 0x1E);
    fontInit(getVramAddr(), 1);
    doTweaks = 0;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Shell", BootInitGame__5Shell);
#endif
extern int g_noScripts;
extern int NonStandardNameSelected;
extern "C" int strcmp(const char *, const char *);
extern "C" int atoi(const char *);
#ifdef NON_MATCHING
/* untuned: 32/93 words (retail duplicates the "false" tail for argc < 2 and for a level already picked);
 * tools/difftest.py 300/300 */
/* Command line: `<level name> [level number]` loads that level (whichLevel) instead of GameLevelNames[m_levelNum], and
 * `noscripts` turns the level scripts off. Only read when no level was picked yet (m_levelNum 0). */
void ResolveCommandLineArguments(int argc, char **argv)
{
    if (argc >= 2 && shell->m_levelNum == 0) {
        if (strcmp(argv[1], "noscripts") == 0) {
            printf("No scripts.\n");
            g_noScripts = 1;
            return;
        }
        UseCommandLineLevel = 1;
        printf("UseCommandLineLevel set to true\n");
        NonStandardNameSelected = 1;
        sprintf(whichLevel, D_006F81A8, argv[1]);
        if (argc == 3)
            shell->m_levelNum = atoi(argv[2]);
        return;
    }
    UseCommandLineLevel = 0;
    printf("UseCommandLineLevel set to false\n");
    if (NonStandardNameSelected == 0)
        sprintf(whichLevel, D_006F81A8, GameLevelNames_006EF748[shell->m_levelNum]);
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Shell", ResolveCommandLineArguments__FiPPc);
#endif
INCLUDE_ASM("asm/nonmatchings/game/Shell", InitGS__5Shells);
INCLUDE_ASM("asm/nonmatchings/game/Shell", LoadUserintTexture__5Shell);
INCLUDE_ASM("asm/nonmatchings/game/Shell", LoadUserintTexture1__5Shell);
INCLUDE_ASM("asm/nonmatchings/game/Shell", LoadUserintDB__5Shell);
INCLUDE_ASM("asm/nonmatchings/game/Shell", LoadUserintDB1__5Shell);
#ifdef NON_MATCHING
/* 97/279 words: untuned, from the m2c draft + asm */
void Shell::LoadResTexture(void)
{
    char path[0x80];
    int i;
    int j;

    if (m_levelNum == 0 || UseCommandLineLevel != 0)
        formatFilename(path, whichLevel, D_006F81C0, SH_FILE_0);
    else if (m_levelNum < 0x1D)
        formatFilename(path, GameLevelNames_006EF748[m_levelNum], D_006F81C0, SH_FILE_0);
    else
        sprintf(path, "host0:monster.rtx");
    informProgressBar(0.0f);
    char *levelAddr = getNextNgpLoadAddr();
    fileOnlyResFile(levelAddr, zipInflateAll(path, levelAddr));
    gWhichMicroSection++;
    informProgressBar(0.0f);
    if (m_mode == 6) {
        for (i = 1; i < 6; i++) {
            informProgressBar(0.0f);
            formatFilename1(path, MonsterLongNames_006EF860[i], 0, D_006F81C0, SH_FILE_PLAYER);
            int sz = fileReadf(path, getNextResLoadAddr());
            fileAddResFile(getNextResLoadAddr(), sz);
            gWhichMicroSection++;
            informProgressBar(0.0f);
        }
        for (i = 7; i < 12; i++) {
            informProgressBar(0.0f);
            formatFilename1(path, MonsterLongNames_006EF860[i], 0, D_006F81C0, SH_FILE_PLAYER);
            int sz = fileReadf(path, getNextResLoadAddr());
            fileAddResFile(getNextResLoadAddr(), sz);
            gWhichMicroSection++;
            informProgressBar(0.0f);
        }
    } else {
        for (i = 0; i < m_numPlayers; i++) {
            if ((m_monsterSel[i] >> 5) < 0x10) {
                informProgressBar(0.0f);
                if (i == 0)
                    formatFilename1(path, MonsterLongNames_006EF860[m_monsterSel[0] >> 5], m_costume[0], D_006F81C0, SH_FILE_PLAYER);
                else if (i == 1)
                    formatFilename1(path, MonsterLongNames_006EF860[m_monsterSel[1] >> 5], m_costume[1], D_006F81C0, SH_FILE_PLAYER);
                else
                    formatFilename(path, MonsterLongNames_006EF860[m_monsterSel[i] >> 5], D_006F81C0, SH_FILE_PLAYER);
                int sz = fileReadf(path, getNextResLoadAddr());
                fileAddResFile(getNextResLoadAddr(), sz);
                gWhichMicroSection++;
                informProgressBar(0.0f);
            } else {
                printf(D_006EF458, m_monsterSel[i], i);
            }
        }
    }
    for (j = 0; j < m_numAIs; j++) {
        int sel = m_monsterSel[4 + j];

        if ((sel >> 5) < 0x10) {
            informProgressBar(0.0f);
            formatFilename1(path, MonsterLongNames_006EF860[sel >> 5], m_costume[2 + j], D_006F81C0, SH_FILE_AI);
            int sz = fileReadf(path, getNextResLoadAddr());
            fileAddResFile(getNextResLoadAddr(), sz);
            gWhichMicroSection++;
            informProgressBar(0.0f);
        } else {
            printf(D_006EF498, sel, j);
        }
    }
    texmResInit(getVramAddr(), (QwData *)levelAddr);
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Shell", LoadResTexture__5Shell);
#endif
INCLUDE_ASM("asm/nonmatchings/game/Shell", D_006EF458);
INCLUDE_ASM("asm/nonmatchings/game/Shell", D_006EF498);
#ifdef NON_MATCHING
/* 11/267 words: untuned, from the m2c draft + asm */
void Shell::LoadTexture(void)
{
    char path[0x80];
    int i;
    int j;

    if (m_levelNum == 0 || UseCommandLineLevel != 0)
        formatFilename(path, whichLevel, D_006F81C8, SH_FILE_0);
    else if (m_levelNum < 0x1D)
        formatFilename(path, GameLevelNames_006EF748[m_levelNum], D_006F81C8, SH_FILE_0);
    else
        sprintf(path, "host0:monster.tex");
    informProgressBar(0.0f);
    int size = zipInflateAll(path, getNextNgpLoadAddr());
    fileOnlyTexFile(getNextNgpLoadAddr(), size);
    gWhichMicroSection++;
    informProgressBar(0.0f);
    if (m_mode == 6) {
        for (i = 1; i < 6; i++) {
            informProgressBar(0.0f);
            formatFilename1(path, MonsterLongNames_006EF860[i], 0, D_006F81C8, SH_FILE_PLAYER);
            int sz = fileReadf(path, getNextTexLoadAddr());
            fileAddTexFile(getNextTexLoadAddr(), sz);
            gWhichMicroSection++;
            informProgressBar(0.0f);
        }
        for (i = 7; i < 12; i++) {
            informProgressBar(0.0f);
            formatFilename1(path, MonsterLongNames_006EF860[i], 0, D_006F81C8, SH_FILE_PLAYER);
            int sz = fileReadf(path, getNextTexLoadAddr());
            fileAddTexFile(getNextTexLoadAddr(), sz);
            gWhichMicroSection++;
            informProgressBar(0.0f);
        }
    } else {
        for (i = 0; i < m_numPlayers; i++) {
            if ((m_monsterSel[i] >> 5) < 0x10) {
                if (i == 0)
                    formatFilename1(path, MonsterLongNames_006EF860[m_monsterSel[0] >> 5], m_costume[0], D_006F81C8, SH_FILE_PLAYER);
                else if (i == 1)
                    formatFilename1(path, MonsterLongNames_006EF860[m_monsterSel[1] >> 5], m_costume[1], D_006F81C8, SH_FILE_PLAYER);
                else
                    formatFilename(path, MonsterLongNames_006EF860[m_monsterSel[i] >> 5], D_006F81C8, SH_FILE_PLAYER);
                int sz = fileReadf(path, getNextTexLoadAddr());
                fileAddTexFile(getNextTexLoadAddr(), sz);
                gWhichMicroSection++;
                informProgressBar(0.0f);
            }
        }
    }
    for (j = 0; j < m_numAIs; j++) {
        int sel = m_monsterSel[4 + j];

        if ((sel >> 5) < 0x10) {
            informProgressBar(0.0f);
            formatFilename1(path, MonsterLongNames_006EF860[sel >> 5], m_costume[2 + j], D_006F81C8, SH_FILE_AI);
            int sz = fileReadf(path, getNextTexLoadAddr());
            fileAddTexFile(getNextTexLoadAddr(), sz);
            gWhichMicroSection++;
            informProgressBar(0.0f);
        } else {
            printf(D_006EF498, sel, j);
        }
    }
    texmInit(getVramAddr());
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Shell", LoadTexture__5Shell);
#endif
#ifdef NON_MATCHING
/* 14/80 words: untuned, written from the m2c draft + asm */
void Shell::LoadLevelDB(void)
{
    char *path = D_00731500;

    if (m_levelNum == 0 || UseCommandLineLevel != 0) {
        formatFilename(D_00731500, whichLevel, D_006F81D0, SH_FILE_0);
    } else if (m_levelNum < 0x1D) {
        formatFilename(D_00731500, GameLevelNames_006EF748[m_levelNum], D_006F81D0, SH_FILE_0);
        strcpy(whichLevel, GameLevelNames_006EF748[m_levelNum]);
    } else {
        sprintf(D_00731500, "host0:monster.ngp");
    }
    informProgressBar(0.0f);
    fileOnlyNgpFile((char *)0xA00000, zipInflateAll(path, (void *)0xA00000));
    fileAddName(fileTrimPath(path));
    gWhichMicroSection++;
    informProgressBar(0.0f);
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Shell", LoadLevelDB__5Shell);
#endif
#ifdef NON_MATCHING
/* 17/295 words: untuned, from the m2c draft + asm */
void Shell::LoadMonstersDB(void)
{
    char path[0x40];
    char name[0x80];
    int i;
    int j;

    if (m_mode == 6) {
        for (i = 1; i < 6; i++) {
            informProgressBar(0.0f);
            formatFilename1(path, MonsterLongNames_006EF860[i], 0, D_006F81D0, SH_FILE_PLAYER);
            int size = fileReadf(path, getNextNgpLoadAddr());
            fileAddNgpFile(getNextNgpLoadAddr(), size);
            sprintf(name, D_006F81D8, MonsterLongNames_006EF860[i], 0);
            fileAddName(name);
            gWhichMicroSection++;
            informProgressBar(0.0f);
        }
        for (i = 7; i < 12; i++) {
            informProgressBar(0.0f);
            formatFilename1(path, MonsterLongNames_006EF860[i], 0, D_006F81D0, SH_FILE_PLAYER);
            int size = fileReadf(path, getNextNgpLoadAddr());
            fileAddNgpFile(getNextNgpLoadAddr(), size);
            sprintf(name, D_006F81D8, MonsterLongNames_006EF860[i], 0);
            fileAddName(name);
            gWhichMicroSection++;
            informProgressBar(0.0f);
        }
    } else {
        for (i = 0; i < m_numPlayers; i++) {
            if ((m_monsterSel[i] >> 5) < 0x10) {
                informProgressBar(0.0f);
                if (i == 0) {
                    formatFilename1(path, MonsterLongNames_006EF860[m_monsterSel[0] >> 5], m_costume[0], D_006F81D0, SH_FILE_PLAYER);
                    sprintf(name, D_006F81D8, MonsterLongNames_006EF860[m_monsterSel[0] >> 5], m_costume[0]);
                } else if (i == 1) {
                    formatFilename1(path, MonsterLongNames_006EF860[m_monsterSel[1] >> 5], m_costume[1], D_006F81D0, SH_FILE_PLAYER);
                    sprintf(name, D_006F81D8, MonsterLongNames_006EF860[m_monsterSel[1] >> 5], m_costume[1]);
                } else {
                    /* retail quirk: the path of player 3/4 is built from player 2's monster, the registered name from their own */
                    formatFilename(path, MonsterLongNames_006EF860[m_monsterSel[1] >> 5], D_006F81D0, SH_FILE_PLAYER);
                    sprintf(name, D_006F81A8, MonsterLongNames_006EF860[m_monsterSel[i] >> 5]);
                }
                int size = fileReadf(path, getNextNgpLoadAddr());
                fileAddNgpFile(getNextNgpLoadAddr(), size);
                fileAddName(name);
                gWhichMicroSection++;
                informProgressBar(0.0f);
            } else {
                printf(D_006EF458, m_monsterSel[i], i);
            }
        }
    }
    for (j = 0; j < m_numAIs; j++) {
        int sel = m_monsterSel[4 + j];

        informProgressBar(0.0f);
        if ((sel >> 5) < 0x10) {
            formatFilename1(path, MonsterLongNames_006EF860[sel >> 5], m_costume[2 + j], D_006F81D0, SH_FILE_AI);
            int size = fileReadf(path, getNextNgpLoadAddr());
            fileAddNgpFile(getNextNgpLoadAddr(), size);
            informProgressBar(0.0f);
            sprintf(name, D_006F81D8, MonsterLongNames_006EF860[m_monsterSel[4 + j] >> 5], m_costume[2 + j]);
            fileAddName(name);
            gWhichMicroSection++;
            informProgressBar(0.0f);
        } else {
            printf(D_006EF498, sel, j);
        }
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Shell", LoadMonstersDB__5Shell);
#endif
INCLUDE_ASM("asm/nonmatchings/game/Shell", AddEpNode__5ShelliP9_hierhead);
#ifdef NON_MATCHING
/* untuned: 5/68 words; tools/difftest.py 300/300 (all three file types) */
/* Disc path of a level file (\LVL\name.ext) or of a monster model (\MON\name.ext). AI models get a 0/1 suffix so that
 * an AI using the same monster as player 1 loads the other skin; with that clash and player 1 on a costume other
 * than 0 nothing is written. D_006F81E0 is the disc root prefix (empty in the retail build). */
void Shell::formatFilename(char *dst, const char *name, const char *ext, _shFileType type)
{
    if (type == SH_FILE_PLAYER) {
        sprintf(dst, "%s\\MON\\%s.%s", D_006F81E0, name, ext);
    } else if (type == SH_FILE_AI) {
        if ((m_mode == 1 || m_mode == 0) && m_levelNum == 3) {
            sprintf(dst, "%s\\MON\\%s1.%s", D_006F81E0, name, ext);
        } else if (m_mode != 1) {
            sprintf(dst, "%s\\MON\\%s0.%s", D_006F81E0, name, ext);
        } else if (m_monsterSel[0] == m_monsterSel[4] || m_monsterSel[0] == m_monsterSel[5]) {
            if (m_costume[0] == 0)
                sprintf(dst, "%s\\MON\\%s1.%s", D_006F81E0, name, ext);
        } else {
            sprintf(dst, "%s\\MON\\%s0.%s", D_006F81E0, name, ext);
        }
    } else {
        sprintf(dst, "%s\\LVL\\%s.%s", D_006F81E0, name, ext);
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Shell", formatFilename__5ShellPcPCcT2Q25Shell11_shFileType);
#endif
INCLUDE_ASM("asm/nonmatchings/game/Shell", D_006EF598);
#ifdef NON_MATCHING
/* untuned: 1/38 words (the retail AI branch reads m_mode/m_levelNum but every path uses the same format);
 * tools/difftest.py 300/300, MON paths 150/150 each with all six sprintf args */
/* Like formatFilename, with the costume number in the monster file name (\MON\name<costume>.ext). */
void Shell::formatFilename1(char *dst, const char *name, int costume, const char *ext, _shFileType type)
{
    if (type == SH_FILE_PLAYER || type == SH_FILE_AI)
        sprintf(dst, "%s\\MON\\%s%d.%s", D_006F81E0, name, costume, ext);
    else
        sprintf(dst, "%s\\LVL\\%s.%s", D_006F81E0, name, ext);
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Shell", formatFilename1__5ShellPcPCciT2Q25Shell11_shFileType);
#endif
/* Host path "dir/name.ext" (the point tools' files). */
void Shell::formatFilename(char *dst, const char *dir, const char *name, const char *ext)
{
    char fmt[32] = "%s/%s.%s";

    sprintf(dst, fmt, dir, name, ext);
}
INCLUDE_ASM("asm/nonmatchings/game/Shell", getVramAddr__5Shell);
int Shell::Use30HzMode(void)
{
    return m_numPlayers >= 3;
}
char *Shell::GetLevelName(void)
{
    return whichLevel;
}
extern int offBitMonsters[] __asm__("off_bit"); /* ~onBitMonsters, one entry per bit */
/* Sets (on) or clears bit item >> 5 of one of the monster flag words (0 available, 1 locked, 2 chosen). */
void Shell::SetMenuItemFlag(int menu, int item, int on)
{
    switch (menu) {
    case 0:
        if (on)
            m_monsterFlags[0] |= onBitMonsters[item >> 5];
        else
            m_monsterFlags[0] &= offBitMonsters[item >> 5];
        break;
    case 1:
        if (on)
            m_monsterFlags[1] |= onBitMonsters[item >> 5];
        else
            m_monsterFlags[1] &= offBitMonsters[item >> 5];
        break;
    case 2:
        if (on)
            m_monsterFlags[2] |= onBitMonsters[item >> 5];
        else
            m_monsterFlags[2] &= offBitMonsters[item >> 5];
        break;
    default:
        printf("Trying to set bit flag %i, it does not exist!!\n", menu);
        break;
    }
}
/* Makes a monster selectable: available on, locked off. Retail quirk: it passes sel >> 5 and SetMenuItemFlag shifts
 * again, so for every monster id below 0x400 this touches the bit of monster index 0. */
void Shell::EnableMonsterSelection(int sel)
{
    SetMenuItemFlag(0, sel >> 5, 1);
    SetMenuItemFlag(1, sel >> 5, 0);
}

/* Monster ids are (index << 5) | variant; m_monsterFlags[2] holds one bit per monster index that is already picked. */
int Shell::MonsterIsChosen(int i)
{
    return (m_monsterFlags[2] & onBitMonsters[i >> 5]) != 0;
}
int Shell::MonsterExists(int i)
{
    return 0;
}
INCLUDE_ASM("asm/nonmatchings/game/Shell", DisplayLoadBackground__5Shellb);
/* m_monsterFlags[1] holds one bit per monster index that is still locked. */
int Shell::MonsterIsLocked(int i)
{
    return (m_monsterFlags[1] & onBitMonsters[i >> 5]) != 0;
}
INCLUDE_ASM("asm/nonmatchings/game/Shell", init__7TagList);
#ifdef NON_MATCHING
/* 118/148 words: untuned */
void Shell::LoadLevelFiles(void)
{
    sceCdlFILE f;
    char name[0x80];
    int ngpSize;

    viewSetNumViews(1);
    viewCreate(VIEWPORT_7, 0);
    sprintf(name, "%s\\LVL\\%s.NGP;1%s", D_006F81E0, GetLevelName(), D_006F81E8);
    fileCdSearchFile(&f, name);
    ngpSize = f.size;
    sprintf(name, "%s\\LVL\\%s.TEX;1%s", D_006F81E0, GetLevelName(), D_006F81E8);
    fileCdSearchFile(&f, name);
    initProgressBar(m_numAIs + m_numPlayers, ngpSize, f.size);
    informProgressBar(0.0f);
    printf(" =+= Starting     time = %s
", fileGetTimeString());
    LoadLevelDB();
    gWhichMicroSection = 0;
    gWhichMacroSection++;
    informProgressBar(0.0f);
    printf(" =+= Levels Done  time = %s
", fileGetTimeString());
    LoadMonstersDB();
    gWhichMicroSection = 0;
    gWhichMacroSection++;
    informProgressBar(0.0f);
    printf(" =+= Monsters Done    time = %s
", fileGetTimeString());
    LoadResTexture();
    gWhichMicroSection = 0;
    gWhichMacroSection++;
    informProgressBar(0.0f);
    printf(" =+= ResTex Done  time = %s
", fileGetTimeString());
    LoadTexture();
    gWhichMicroSection = 0;
    gWhichMacroSection++;
    informProgressBar(0.0f);
    printf(" =+= Texture Done time = %s
", fileGetTimeString());
    dbsRelocateFileZero(getVramAddr(), UseCommandLineLevel);
    dbInitDb((_dbheader *)0xA00000, getVramAddr());
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Shell", LoadLevelFiles__5Shell);
#endif
/* Ends the loading bar: next macro section, back to one view with viewport 0. */
void Shell::FinishLoadBar(void)
{
    gWhichMacroSection++;
    gWhichMicroSection = 0;
    informProgressBar(0.0f);
    printf(" =+= Finished     time = %s
", fileGetTimeString());
    gOkToDrawProgressBar = 0;
    viewSetNumViews(1);
    viewCreate((_viewports)0, 0);
}
void Shell::ResetLevel(void)
{
    ((TheGame *)game)->ResetLevel();
    if (gUseUnifiedView != 0)
        Cameras::LeaveUnifiedView(0xA);
}
#ifdef NON_MATCHING
extern int genesisMov __asm__("mov.2987");

/* After the last story level: remember which hero (m_monsterSel[0]) finished and queue his ending movie. */
void Shell::GenesisMovie(void)
{
    int hero = m_monsterSel[0];

    exitFromAdvStory = 1;
    genesisMov = hero;
    needOutro = 1;
    switch (hero) {
    case 0x20:
        nextMovie = 0x15;
        break;
    case 0x60:
        nextMovie = 0x16;
        break;
    case 0xA0:
        nextMovie = 0x17;
        break;
    case 0x120:
    case 0x160:
        nextMovie = 0x13;
        break;
    case 0x40:
        nextMovie = 0x19;
        break;
    case 0x140:
        nextMovie = 0x1A;
        break;
    case 0x80:
        nextMovie = 0x1B;
        break;
    case 0x100:
        nextMovie = 0x1C;
        break;
    case 0xE0:
        nextMovie = 0x1E;
        break;
    default:
        nextMovie = 0;
        break;
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Shell", GenesisMovie__5Shell);
#endif
INCLUDE_ASM("asm/nonmatchings/game/Shell", FadeScreen__5ShellibUcUcUcUcUcUc);
INCLUDE_ASM("asm/nonmatchings/game/Shell", FadeScreen__5ShellibRUiT3UcUcUcUcUcUc);
INCLUDE_ASM("asm/nonmatchings/game/Shell", InitialMemCardScreen__5Shell);
INCLUDE_ASM("asm/nonmatchings/game/Shell", __3Hud);
INCLUDE_ASM("asm/nonmatchings/game/Shell", __7TheGame);
INCLUDE_ASM("asm/nonmatchings/game/Shell", __static_initialization_and_destruction_0_001AC240);
INCLUDE_ASM("asm/nonmatchings/game/Shell", __7TagList);
INCLUDE_ASM("asm/nonmatchings/game/Shell", _GLOBAL_$I$g_noScripts);
INCLUDE_ASM("asm/nonmatchings/game/Shell", _GLOBAL_$D$g_noScripts);
INCLUDE_ASM("asm/nonmatchings/game/Shell", GameLevelNames_006EF748);
INCLUDE_ASM("asm/nonmatchings/game/Shell", MonsterLongNames_006EF860);
INCLUDE_ASM("asm/nonmatchings/game/Shell", on_bit_006EF8E8);
INCLUDE_ASM("asm/nonmatchings/game/Shell", off_bit);
