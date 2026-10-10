#include "common.h"
#include "game/shell.h"
#include "game/monster_select.h"
extern int numModsLeft;
extern int unlocked_3092 __asm__("unlocked.3092");
extern int numSelectablesOne __asm__("numSelectablesOne.3096");
extern int numSelectablesTwo __asm__("numSelectablesTwo.3097");
extern int selBeginning __asm__("beginning.3098");
extern int selEnding __asm__("ending.3099");
extern int currentSelection[];
int modelsLeft(int group);
struct _hierswitch;
struct _hierobject;
struct _animHandle;
struct _SaveGameData;
void hierSetSwitch(_hierswitch *sw, int which);
void moviesReadyScreenObject(_hierobject *o, int a, int b);
int monsterIdToMonsterEnum(int id);
class AnimQueue {
public:
    void Push(_animHandle *h, int a, int b, int c);
};
class CMovie {
public:
    void Play(bool loop, char *name);
};
extern AnimQueue *animq;
extern CMovie myMovie;
extern char MainMovie[];
extern int continueDecoding;
extern int currScreen;
extern float targetAlpha;
extern int betweenScreens;
extern int on_bit[] __asm__("on_bit_006EEE78");
extern "C" int printf(const char *, ...);
int inputGetInput(int mask, int pad);
float screenGetFontAlpha(void);
void screenSetButtonAlpha(void);
void changeScreen(int to, int from, int dir, int sfx, bool b);
void updateSelectSwitches(int a, int b, int c, int d);
void updateSelectSwitches3P(int a, int b, int c, int d, int e, int f);
void updateSelectSwitches4P(int a, int b, int c, int d, int e, int f, int g, int h);
void initSelectSwitchesMG(void);
void initSelectSwitches(void);
int screenPrepareDefault(int a, int b);
class ShellSound {
public:
    void playShellSFXSound(int id);
};
extern int prevScreen;
int levelUnlocked(int level);
int screenGetInput(int padMask);
extern int screenFirstPass;
extern char *charSelectStringOne;
extern char promptString2960[] __asm__("promptString.2960");
extern int numSelectables2857 __asm__("numSelectables.2857");
extern void *g_levelList;
extern char PO_MPFreeForAll[];
extern char PO_MPElimination[];
int screenPrepareDefault(int a, int b) __asm__("screenPrepareDefault__Fii");
extern int numSelectables2948 __asm__("numSelectables.2948");
extern int numSelectables2952 __asm__("numSelectables.2952");
extern int numSelectables2959 __asm__("numSelectables.2959");

int monsterIdToMonsterEnum(int id)
{
    return id / 32;
}
#ifdef NON_MATCHING
/* Starts the main-menu background movie on the shell's screen object. */
void screenPrepare(void)
{
    moviesReadyScreenObject(*(_hierobject **)((char *)shell + 0x160), 8, 7);
    myMovie.Play(false, MainMovie);
    continueDecoding = 1;
    currScreen = 0x10;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/screen", screenPrepare__Fv);
#endif
#ifdef NON_MATCHING
/* Points the `count` reel switches of the shell (from `first`) at the model and monster enum of each saved entry (0x3C bytes apiece, from +0x10). */
void setReelSwitches(int first, int count, _SaveGameData *save)
{
    int i;

    for (i = 0; i < count; i++) {
        char *e = (char *)save + 0x10 + i * 0x3C;

        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x27D4 + first * 0x28 + i * 4), *(int *)e);
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x2784 + first * 0x28 + i * 4), monsterIdToMonsterEnum(*(int *)(e + 4)));
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/game/screen", setReelSwitches__FiiP13_SaveGameData);
#endif
#ifdef NON_MATCHING
/* Queues the animation of reel `reel`: `from` is stopped, `to` started. Each reel owns 0xA0 bytes of animation handles in the shell. */
void changeReel(int reel, int from, int to)
{
    char *base = (char *)shell + reel * 0xA0 + 0x2644;

    animq->Push((_animHandle *)(base + from * 0x10), 0, 0, 0);
    animq->Push((_animHandle *)(base + to * 0x10), 1, 1, 1);
}
#else
INCLUDE_ASM("asm/nonmatchings/game/screen", changeReel__Fiii);
#endif
INCLUDE_ASM("asm/nonmatchings/game/screen", screenMain__Fv);
INCLUDE_ASM("asm/nonmatchings/game/screen", updateSelectSwitches__Fiiii);
INCLUDE_ASM("asm/nonmatchings/game/screen", updateSelectSwitches3P__Fiiiiii);
INCLUDE_ASM("asm/nonmatchings/game/screen", updateSelectSwitches4P__Fiiiiiiii);
INCLUDE_ASM("asm/nonmatchings/game/screen", updateSelectSwitchesMG__Fiiii);
INCLUDE_ASM("asm/nonmatchings/game/screen", updateSelectSwitchesAI__Fii);
INCLUDE_ASM("asm/nonmatchings/game/screen", initSelectSwitches__Fv);
INCLUDE_ASM("asm/nonmatchings/game/screen", initSelectSwitchesMG__Fv);
INCLUDE_ASM("asm/nonmatchings/game/screen", screenGameModes1P__Fv);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EE2F0);
INCLUDE_ASM("asm/nonmatchings/game/screen", screenCharSelect1P__Fv);
INCLUDE_ASM("asm/nonmatchings/game/screen", screenSelect1AI__Fv);
INCLUDE_ASM("asm/nonmatchings/game/screen", screenCtlrCfg__Fv);
#ifdef NON_MATCHING
/* Two-player game modes: 1 = free for all (mode 3, level list PO_MPFreeForAll), 2 = elimination (mode 6, PO_MPElimination), 3 = minigames.
   Here the cursor moves with codes 1/2 (up/down) instead of 3/4. */
void screenGameModes2P(void)
{
    int input, sel;
    int *selSw;

    if (screenFirstPass != 0) {
        numSelectables2857 = screenPrepareDefault(5, currentSelection[currScreen]);
        *(int *)((char *)shell + 0x2948) = 0;
        shell->m_numAIs = 0;
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x5D0), 0);
        moviesReadyScreenObject(*(_hierobject **)((char *)shell + 0x160), 8, 7);
        myMovie.Play(false, MainMovie);
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x70), 0);
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x74), 0);
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x78), 0);
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x414), currentSelection[11]);
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x464), currentSelection[13]);
        continueDecoding = 1;
        initSelectSwitches();
        currentSelection[6] = *(int *)((char *)shell + 0x2B9C) + 1;
        currentSelection[28] = *(int *)((char *)shell + 0x2BA0);
        currentSelection[18] = *(int *)((char *)shell + 0x2B9C) + 1;
        currentSelection[40] = *(int *)((char *)shell + 0x2BA0);
        currentSelection[19] = *(int *)((char *)shell + 0x2B9C) + 1;
        screenFirstPass = 0;
        currentSelection[41] = *(int *)((char *)shell + 0x2BA0);
    }
    myMovie.Play(false, MainMovie);
    input = screenGetInput(1) & 0xFFFF;
    switch (input) {
    case 1:
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x324 + currentSelection[currScreen] * 4), 0);
        sel = currentSelection[currScreen];
        currentSelection[currScreen] = sel >= 2 ? sel - 1 : numSelectables2857;
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x324 + currentSelection[currScreen] * 4), 1);
        ((ShellSound *)((char *)shell + 0x2918))->playShellSFXSound(2);
        return;
    case 2:
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x324 + currentSelection[currScreen] * 4), 0);
        sel = currentSelection[currScreen];
        currentSelection[currScreen] = sel < numSelectables2857 ? sel + 1 : 1;
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x324 + currentSelection[currScreen] * 4), 1);
        ((ShellSound *)((char *)shell + 0x2918))->playShellSFXSound(4);
        return;
    case 6:
        sel = currentSelection[currScreen];
        if (sel == 1) {
            shell->m_mode = 3;
            shell->m_battleMode = 3;
            g_levelList = PO_MPFreeForAll;
            hierSetSwitch(*(_hierswitch **)((char *)shell + 0x5CC), 0);
            changeScreen(5, 0xD, 1, 2, true);
            initSelectSwitches();
        } else if (sel == 2) {
            shell->m_mode = 6;
            shell->m_battleMode = 6;
            shell->m_killTarget = 0;
            g_levelList = PO_MPElimination;
            shell->m_wins[0] = 0;
            shell->m_wins[1] = 0;
            hierSetSwitch(*(_hierswitch **)((char *)shell + 0x5CC), 0);
            changeScreen(5, 0xB, 1, 2, true);
            initSelectSwitches();
        } else if (sel == 3) {
            changeScreen(5, 0xE, 1, 8, true);
            hierSetSwitch(*(_hierswitch **)((char *)shell + 0x48C), currentSelection[14]);
            shell->m_killTarget = 0;
        } else {
            printf("Two Player Game Modes Screen has screwed up item indexing! - We think we selected #%i
", sel);
        }
        ((ShellSound *)((char *)shell + 0x2918))->playShellSFXSound(8);
        return;
    case 5:
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x5D0), 0);
        changeScreen(5, prevScreen, -1, 0, true);
        ((ShellSound *)((char *)shell + 0x2918))->playShellSFXSound(6);
        shell->m_numPlayers = 0;
        break;
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/game/screen", screenGameModes2P__Fv);
#endif
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EE460);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EE488);
INCLUDE_ASM("asm/nonmatchings/game/screen", screenCharSelect3P__Fv);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EE4D8);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EE508);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EE530);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EE558);
INCLUDE_ASM("asm/nonmatchings/game/screen", jtbl_006EE590);
INCLUDE_ASM("asm/nonmatchings/game/screen", jtbl_006EE5B0);
INCLUDE_ASM("asm/nonmatchings/game/screen", screenCharSelect2P__Fv);
INCLUDE_ASM("asm/nonmatchings/game/screen", screenMinigames2P__Fv);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EE698);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EE6D0);
INCLUDE_ASM("asm/nonmatchings/game/screen", jtbl_006EE720);
INCLUDE_ASM("asm/nonmatchings/game/screen", jtbl_006EE740);
INCLUDE_ASM("asm/nonmatchings/game/screen", jtbl_006EE770);
INCLUDE_ASM("asm/nonmatchings/game/screen", jtbl_006EE7A0);
INCLUDE_ASM("asm/nonmatchings/game/screen", jtbl_006EE7D0);
INCLUDE_ASM("asm/nonmatchings/game/screen", jtbl_006EE7F0);
INCLUDE_ASM("asm/nonmatchings/game/screen", jtbl_006EE820);
INCLUDE_ASM("asm/nonmatchings/game/screen", jtbl_006EE850);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EE878);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EE890);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EE8F8);
#ifndef NON_MATCHING /* the C++ screenElimOptions2P needs no table */
INCLUDE_ASM("asm/nonmatchings/game/screen", jtbl_006EE960);
#endif
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EE978);
INCLUDE_ASM("asm/nonmatchings/game/screen", jtbl_006EE9D0);
INCLUDE_ASM("asm/nonmatchings/game/screen", jtbl_006EE9F0);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EEA48);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EEA58);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EEA88);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EEA98);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EEAA8);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EEAB8);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EEAD8);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EEAF0);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EEB00);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EEB30);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EEB48);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EEB60);
INCLUDE_ASM("asm/nonmatchings/game/screen", jtbl_006EEB80);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EEB98);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EEBC0);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EEBE8);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EEC10);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EEC40);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EEC70);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EECA0);
INCLUDE_ASM("asm/nonmatchings/game/screen", jtbl_006EECD0);
INCLUDE_ASM("asm/nonmatchings/game/screen", jtbl_006EECF0);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EED08);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EED30);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EED40);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EED50);
INCLUDE_ASM("asm/nonmatchings/game/screen", D_006EED68);
INCLUDE_ASM("asm/nonmatchings/game/screen", jtbl_006EED80);
INCLUDE_ASM("asm/nonmatchings/game/screen", jtbl_006EEDD0);
#ifndef NON_MATCHING /* the C++ levelUnlocked builds its own table */
INCLUDE_ASM("asm/nonmatchings/game/screen", jtbl_006EEE30);
#endif
INCLUDE_ASM("asm/nonmatchings/game/screen", on_bit_006EEE78);
INCLUDE_ASM("asm/nonmatchings/game/screen", screenLevelSelect__Fv);
INCLUDE_ASM("asm/nonmatchings/game/screen", screenLoadSave__Fv);
#ifdef NON_MATCHING
/* Moves the knob of slider `which` (0 or 1) along its bar; `v` is clamped to 0..1. */
void setSlider(int which, float v)
{
    if (v < 0.0f)
        v = 0.0f;
    if (v > 1.0f)
        v = 1.0f;
    switch (which) {
    case 0: {
        float *k = *(float **)((char *)shell + 0x604);

        k[4] = v * -1.565f;
        k[5] = v * 1.564f;
        k[6] = v * -0.003f;
        break;
    }
    case 1: {
        float *k = *(float **)((char *)shell + 0x608);

        k[4] = v * -1.553f;
        k[5] = v * 1.555f;
        k[6] = v * -0.012f;
        break;
    }
    default:
        printf("Trying to set a slider on a bar that doesn't exist!!
");
        break;
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/game/screen", setSlider__Fif);
#endif
INCLUDE_ASM("asm/nonmatchings/game/screen", screenOptions__Fv);
#ifdef NON_MATCHING
/* One-player free-for-all options: how many AIs (1..3) fight the player. */
void screenFreeForAllOptions1P(void)
{
    int input, sel, i;

    if (screenFirstPass != 0) {
        numSelectables2948 = 3;
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x43C), currentSelection[currScreen]);
        screenFirstPass = 0;
        for (i = 2; i >= 0; i--)
            *(int *)((char *)shell + 0x2948 + i * 4) = 0;
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x1D0), 0);
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x1D4), 0);
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x1D8), 0);
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x1C4), 0);
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x1C8), 0);
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x1E0), 0);
        updateSelectSwitches(0, 0, 1, 1);
        updateSelectSwitches3P(0, 0, 0, 1, 1, 1);
        updateSelectSwitches4P(0, 0, 0, 0, 1, 1, 1, 1);
    }
    hierSetSwitch(*(_hierswitch **)((char *)shell + 0x43C), currentSelection[currScreen]);
    myMovie.Play(false, MainMovie);
    input = screenGetInput(1) & 0xFFFF;
    switch (input) {
    case 3:
        sel = currentSelection[currScreen];
        currentSelection[currScreen] = sel >= 2 ? sel - 1 : numSelectables2948;
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x43C), currentSelection[currScreen]);
        ((ShellSound *)((char *)shell + 0x2918))->playShellSFXSound(2);
        return;
    case 4:
        sel = currentSelection[currScreen];
        currentSelection[currScreen] = sel < numSelectables2948 ? sel + 1 : 1;
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x43C), currentSelection[currScreen]);
        ((ShellSound *)((char *)shell + 0x2918))->playShellSFXSound(4);
        return;
    case 6:
        sel = currentSelection[currScreen];
        if (sel == 0) {
            shell->m_numAIs = 0;
            printf("Number of AI set to %i
", 0);
            changeScreen(0xC, 3, 1, 0, true);
        } else if (sel >= 1 && sel <= 3) {
            shell->m_numAIs = sel;
            printf("Number of AI set to %i
", sel);
            changeScreen(0xC, 0xF, 1, 0, true);
        } else {
            printf("One Player Free for All Options Screen has screwed up item indexing! - We think we selected #%i
", sel);
        }
        ((ShellSound *)((char *)shell + 0x2918))->playShellSFXSound(8);
        return;
    case 5:
        shell->m_numAIs = 0;
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x5D0), 0);
        changeScreen(0xC, prevScreen, -1, 2, true);
        ((ShellSound *)((char *)shell + 0x2918))->playShellSFXSound(6);
        break;
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/game/screen", screenFreeForAllOptions1P__Fv);
#endif
#ifdef NON_MATCHING
/* Two-player free-for-all options: how many AIs join (0, 1 or 2). Input codes from screenGetInput: 3/4 move the cursor, 5 goes back, 6 confirms. */
void screenFreeForAllOptions2P(void)
{
    int *cur = &currentSelection[currScreen];
    int input, sel;

    if (screenFirstPass != 0) {
        numSelectables2952 = 2;
        updateSelectSwitches(0, 0, 1, 1);
        updateSelectSwitches3P(0, 0, 0, 1, 1, 1);
        updateSelectSwitches4P(0, 0, 0, 0, 1, 1, 1, 1);
        screenFirstPass = 0;
    }
    hierSetSwitch(*(_hierswitch **)((char *)shell + 0x5CC), 0);
    hierSetSwitch(*(_hierswitch **)((char *)shell + 0x464), currentSelection[currScreen]);
    myMovie.Play(false, MainMovie);
    input = screenGetInput(1) & 0xFFFF;
    switch (input) {
    case 3:
        cur = &currentSelection[currScreen];
        *cur = *cur > 0 ? *cur - 1 : numSelectables2952;
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x464), currentSelection[currScreen]);
        ((ShellSound *)((char *)shell + 0x2918))->playShellSFXSound(2);
        return;
    case 4:
        cur = &currentSelection[currScreen];
        if (*cur < numSelectables2952)
            *cur = *cur + 1;
        else
            *cur = 0;
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x464), currentSelection[currScreen]);
        ((ShellSound *)((char *)shell + 0x2918))->playShellSFXSound(4);
        return;
    case 6:
        sel = currentSelection[currScreen];
        if (sel == 0) {
            shell->m_numAIs = 0;
            changeScreen(0xD, 0xF, 1, 0, true);
        } else if (sel == 1 || sel == 2) {
            shell->m_numAIs = sel;
            changeScreen(0xD, 0xF, 1, 0, true);
        } else {
            printf("Two Player Free for All Options Screen has screwed up item indexing! - We think we selected #%i
", sel);
        }
        ((ShellSound *)((char *)shell + 0x2918))->playShellSFXSound(8);
        return;
    case 5:
        shell->m_numAIs = 0;
        changeScreen(0xD, 5, -1, 2, true);
        ((ShellSound *)((char *)shell + 0x2918))->playShellSFXSound(6);
        break;
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/game/screen", screenFreeForAllOptions2P__Fv);
#endif
#ifdef NON_MATCHING
/* Two-player elimination options: the cursor (0..4) picks the elimination setting, stored (+1) in shell->m_elimination and copied to both m_continues. */
void screenElimOptions2P(void)
{
    int input, sel;

    if (screenFirstPass != 0) {
        int a = *(int *)((char *)shell + 0x2B9C);
        int b = *(int *)((char *)shell + 0x2BA0);

        if (a != 0 || b == 0) {
            initSelectSwitches();
            a = *(int *)((char *)shell + 0x2B9C);
            if (!(a < currentSelection[6] && currentSelection[28] < *(int *)((char *)shell + 0x2BA0))) {
                currentSelection[6] = a + 1;
                currentSelection[28] = *(int *)((char *)shell + 0x2BA0) + 1;
            }
        }
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x1DC), currentSelection[6]);
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x1E0), currentSelection[28]);
        screenPrepareDefault(0xB, currentSelection[currScreen]);
        screenFirstPass = 0;
    }
    myMovie.Play(false, MainMovie);
    input = screenGetInput(1) & 0xFFFF;
    switch (input) {
    case 3:
        sel = currentSelection[currScreen];
        currentSelection[currScreen] = sel > 0 ? sel - 1 : 4;
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x414), currentSelection[currScreen]);
        ((ShellSound *)((char *)shell + 0x2918))->playShellSFXSound(2);
        return;
    case 4:
        sel = currentSelection[currScreen];
        currentSelection[currScreen] = sel < 4 ? sel + 1 : 0;
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x414), currentSelection[currScreen]);
        ((ShellSound *)((char *)shell + 0x2918))->playShellSFXSound(4);
        return;
    case 6:
        sel = currentSelection[currScreen];
        if (sel >= 0 && sel <= 4) {
            shell->m_elimination = sel + 1;
            changeScreen(0xB, 6, 1, 0, true);
            initSelectSwitches();
        }
        shell->m_continues[1] = shell->m_elimination;
        shell->m_continues[0] = shell->m_elimination;
        ((ShellSound *)((char *)shell + 0x2918))->playShellSFXSound(8);
        return;
    case 5:
        changeScreen(0xB, prevScreen, -1, 2, true);
        ((ShellSound *)((char *)shell + 0x2918))->playShellSFXSound(6);
        break;
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/game/screen", screenElimOptions2P__Fv);
#endif
#ifdef NON_MATCHING
/* Minigame choice: slot 1..3 picks level 25/26/27 and game mode 7/8/9, each only once its unlock flag is set. Input codes as in screenFreeForAllOptions2P. */
void screenMGSelect(void)
{
    int input, sel;

    if (screenFirstPass != 0) {
        initSelectSwitchesMG();
        currentSelection[7] = *(int *)((char *)shell + 0x2B9C) + 1;
        currentSelection[29] = *(int *)((char *)shell + 0x2BA0);
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x1EC), currentSelection[7]);
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x1F0), currentSelection[29]);
        charSelectStringOne = promptString2960 + 0x30;
        screenFirstPass = 0;
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x48C), currentSelection[currScreen]);
    }
    sel = currentSelection[currScreen];
    if ((sel == 1 && levelUnlocked(25) == 1) || (sel == 2 && levelUnlocked(26) == 1) || (sel == 3 && levelUnlocked(27) == 1))
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x600), 0);
    else
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x600), 1);
    input = screenGetInput(1) & 0xFFFF;
    switch (input) {
    case 3:
        sel = currentSelection[currScreen];
        currentSelection[currScreen] = sel >= 2 ? sel - 1 : numSelectables2959;
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x48C), currentSelection[currScreen]);
        ((ShellSound *)((char *)shell + 0x2918))->playShellSFXSound(2);
        return;
    case 4:
        sel = currentSelection[currScreen];
        currentSelection[currScreen] = sel < numSelectables2959 ? sel + 1 : 1;
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x48C), currentSelection[currScreen]);
        ((ShellSound *)((char *)shell + 0x2918))->playShellSFXSound(4);
        return;
    case 6: {
        int level = 0, mode = 0;

        sel = currentSelection[currScreen];
        if (sel == 1 && *(int *)((char *)monsterSelectMode + 0x794) == 1) {
            level = 25;
            mode = 7;
        } else if (sel == 2 && *(int *)((char *)monsterSelectMode + 0x7C0) == 1) {
            level = 26;
            mode = 8;
        } else if (sel == 3 && *(int *)((char *)monsterSelectMode + 0x7EC) == 1) {
            level = 27;
            mode = 9;
        }
        if (level != 0) {
            shell->m_levelNum = level;
            shell->m_battleMode = mode;
            shell->m_mode = mode;
            hierSetSwitch(*(_hierswitch **)((char *)shell + 0x48C), 4);
            changeScreen(0xE, 7, 1, 0, true);
        }
        ((ShellSound *)((char *)shell + 0x2918))->playShellSFXSound(3);
        return;
    }
    case 5:
        hierSetSwitch(*(_hierswitch **)((char *)shell + 0x5D0), 0);
        changeScreen(0xE, prevScreen, -1, 8, true);
        ((ShellSound *)((char *)shell + 0x2918))->playShellSFXSound(6);
        break;
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/game/screen", screenMGSelect__Fv);
#endif
INCLUDE_ASM("asm/nonmatchings/game/screen", screenBestOf__Fv);
INCLUDE_ASM("asm/nonmatchings/game/screen", screenMemoryCardInit__Fv);
INCLUDE_ASM("asm/nonmatchings/game/screen", screenTransition__Fb);
INCLUDE_ASM("asm/nonmatchings/game/screen", screenUnlocker__Fv);
INCLUDE_ASM("asm/nonmatchings/game/screen", screenCharSelect4P__Fv);
INCLUDE_ASM("asm/nonmatchings/game/screen", screenWaitForStart__Fv);
INCLUDE_ASM("asm/nonmatchings/game/screen", changeScreen__Fiiiib);
#ifdef NON_MATCHING
/* Polls the pads allowed by `padMask` (on_bit[pad]) and returns (pad << 16) | action, 0 when nothing is pressed or a screen change is running.
   The 16 pad masks are tried in the order of `map`, each giving its action code 1..0x10. */
int screenGetInput(int padMask)
{
    static const struct {
        int mask;
        int action;
    } map[16] = {
        { 0x10, 1 }, { 0x40, 2 }, { 0x80, 3 }, { 0x20, 4 },
        { 0x2000, 8 }, { 0x8000, 7 }, { 0x4000, 6 }, { 0x1000, 5 },
        { 0x400, 9 }, { 0x800, 0xC }, { 0x100, 0xA }, { 0x200, 0xD },
        { 2, 0xB }, { 4, 0xE }, { 8, 0x10 }, { 1, 0xF },
    };
    int pad, i;

    if (betweenScreens == 0) {
        for (pad = 0; pad < 4; pad++) {
            if (!(on_bit[pad] & padMask))
                continue;
            for (i = 0; i < 16; i++)
                if (inputGetInput(map[i].mask, pad))
                    return (pad << 16) | map[i].action;
        }
    }
    return 0;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/screen", screenGetInput__Fi);
#endif
INCLUDE_ASM("asm/nonmatchings/game/screen", screenPrepareDefault__Fii);
INCLUDE_ASM("asm/nonmatchings/game/screen", screenWriteInstructions__Fif);
#ifdef NON_MATCHING
float screenGetFontAlpha(void)
{
    return targetAlpha;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/screen", screenGetFontAlpha__Fv);
#endif
#ifdef NON_MATCHING
void setButtonAlpha(float a)
{
    targetAlpha = a;
    screenSetButtonAlpha();
}
#else
INCLUDE_ASM("asm/nonmatchings/game/screen", setButtonAlpha__Ff);
#endif
INCLUDE_ASM("asm/nonmatchings/game/screen", screenSetButtonAlpha__Fv);
INCLUDE_ASM("asm/nonmatchings/game/screen", showButtons__Fi);
INCLUDE_ASM("asm/nonmatchings/game/screen", killButtons__Fv);
INCLUDE_ASM("asm/nonmatchings/game/screen", saveUnlocks__Fv);
INCLUDE_ASM("asm/nonmatchings/game/screen", saveSettings__Fv);
INCLUDE_ASM("asm/nonmatchings/game/screen", loadSettings__Fv);
#ifdef NON_MATCHING
/* 7/19 words: address math scheduling */
int isSlotOpen(int group, int slot)
{
    SelectSlot *s = (SelectSlot *)((char *)monsterSelectMode + slot * 0x2C + group * 0xB0);

    if (s->state == 1 && s->taken == s->state)
        return 1;
    return 0;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/screen", isSlotOpen__Fii);
#endif
#ifdef NON_MATCHING
/* Every one of the 12 groups of 4 select slots becomes available again. */
void resetSelectables(void)
{
    int g, i;

    for (g = 0; g < 12; g++)
        for (i = 3; i >= 0; i--)
            monsterSelectMode[g * 4 + i].taken = 1;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/screen", resetSelectables__Fv);
#endif
void resetGameMode(void)
{
    int mode = shell->m_battleMode;

    if (mode != 0)
        shell->m_mode = mode;
}

#ifdef NON_MATCHING
/* 6/26 words: numModsLeft not gp-relative on first store */
int modelsLeft(int group)
{
    int i = 3;
    SelectSlot *s = (SelectSlot *)((char *)monsterSelectMode + (group - 1) * 0xB0);

    numModsLeft = 4;
    do {
        if (s->taken == 0 || s->state == 0)
            numModsLeft--;
        s = (SelectSlot *)((char *)s + 0x2C);
    } while (--i >= 0);
    return numModsLeft;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/screen", modelsLeft__Fi);
#endif

/* Levels 10, 11, 14, 15, 25, 26 and 27 are locked until their flag in the select table is set; every other level is open. */
#ifdef NON_MATCHING
/* 9/56 words: retail keeps a jump table */
int levelUnlocked(int level)
{
    switch (level) {
    case 10:
        unlocked_3092 = *(int *)((char *)monsterSelectMode + 0x768) != 0;
        break;
    case 11:
        unlocked_3092 = *(int *)((char *)monsterSelectMode + 0x73C) != 0;
        break;
    case 14:
        unlocked_3092 = *(int *)((char *)monsterSelectMode + 0x710) != 0;
        break;
    case 15:
        unlocked_3092 = *(int *)((char *)monsterSelectMode + 0x6E4) != 0;
        break;
    case 25:
        unlocked_3092 = *(int *)((char *)monsterSelectMode + 0x794) != 0;
        break;
    case 26:
        unlocked_3092 = *(int *)((char *)monsterSelectMode + 0x7C0) != 0;
        break;
    case 27:
        unlocked_3092 = *(int *)((char *)monsterSelectMode + 0x7EC) != 0;
        break;
    default:
        unlocked_3092 = 1;
        break;
    }
    return unlocked_3092;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/screen", levelUnlocked__Fi);
#endif
#ifdef NON_MATCHING
/* Steps player `player`'s choice in list `list` by `dir` (wrapping inside the selectable range) until a model with a free copy is found.
   The range depends on the two unlock flags: 1..9 or 2..9 by default, up to 10 once the bonus monster is open. */
void findAvailableMonster(int dir, int player, int list)
{
    int *cur;
    int a = *(int *)((char *)monsterSelectMode + 4);
    int b = *(int *)((char *)monsterSelectMode + 0x634);

    if (a == 0 && b == 0) {
        selBeginning = 2;
        selEnding = 9;
        numSelectablesOne = 9;
        numSelectablesTwo = 9;
    } else if (a == 1 && b == 0) {
        selBeginning = a;
        selEnding = 9;
        numSelectablesOne = 9;
        numSelectablesTwo = 9;
    } else if (a == 0 && b == 1) {
        numSelectablesTwo = 9;
        selBeginning = 2;
        selEnding = 10;
        numSelectablesOne = 9;
    } else if (a == 1 && b == a) {
        selBeginning = b;
        selEnding = 10;
        numSelectablesOne = 10;
        numSelectablesTwo = 10;
    }
    cur = (int *)((char *)currentSelection + player * 0x58 + list * 4);
    do {
        *cur += dir;
        if (*cur < selBeginning)
            *cur = selEnding;
        if (selEnding < *cur)
            *cur = selBeginning;
    } while (modelsLeft(*cur) == 0);
}
#else
INCLUDE_ASM("asm/nonmatchings/game/screen", findAvailableMonster__Fiii);
#endif
#ifdef NON_MATCHING
/* Clears the ten model choices kept in the shell (0x2BE8..0x2C0C). */
void resetModels(void)
{
    int i;

    for (i = 9; i >= 0; i--)
        *(int *)((char *)shell + 0x2BE8 + i * 4) = 0;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/screen", resetModels__Fv);
#endif
INCLUDE_ASM("asm/nonmatchings/game/screen", __static_initialization_and_destruction_0_001A6668);
INCLUDE_ASM("asm/nonmatchings/game/screen", _GLOBAL_$I$NamecardDistance);
