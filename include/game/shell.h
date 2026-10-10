#ifndef SHELL_H
#define SHELL_H

struct _hierhead;
enum _vramAddrs { VRAM_ADDRS_DUMMY };

/* Front-end shell: level names and file name formatting used by the point tools. */
class Shell {
public:
    char pad0[0x2938];
    int m_monsterSel[8];  /* 0x2938: per slot (players first, then AIs): (monster << 5) | variant; monster indexes MonsterLongNames */
    char pad2958[0x2A24 - 0x2958];
    int m_monsterFlags[3]; /* 0x2A24: one bit per monster index (onBitMonsters): [0] available, [1] locked, [2] chosen */
    char pad2A30[0x2A44 - 0x2A30];
    int m_killTarget;     /* 0x2A44: wins needed to take the match (0 = no target, rounds just restart) */
    char pad2A48[0x2B40 - 0x2A48];
    int m_continues[2];   /* 0x2B40: rounds each player can still lose in a two-player match (from m_elimination) */
    int m_elimination;    /* 0x2B48: elimination setting picked in the menu, plus one */
    int m_wins[2];        /* 0x2B4C: rounds won by each player in the current match (shown through Monster::m_winsThisGame) */
    int m_battleMode;     /* 0x2B54: battle type picked in the menu (3, 6, ...) */
    int m_survivor;       /* 0x2B58: player left standing when the other ran out of continues */
    int m_roundsPlayed;   /* 0x2B5C */
    char pad2B60[0x2BA8 - 0x2B60];
    int m_restart;        /* 0x2BA8: 1 = the level is being restarted (ResetLevel, not UnpauseLevel) and needs the loading background */
    char pad2BAC[0x2BB0 - 0x2BAC];
    int m_levelNum;       /* 0x2BB0: index into GameLevelNames (0 = the level named on the command line) */
    int m_numPlayers;     /* 0x2BB4 */
    int m_numAIs;         /* 0x2BB8 */
    int m_lives[4];       /* 0x2BBC: lives left per player (NUM_LIVES[mode] at the start, see InitPlayerLives) */
    int m_inSession;      /* 0x2BCC: a play session is running (rtMain loop) */
    int m_inMenus;        /* 0x2BD0: the boot -> menus -> session cycle keeps going */
    char pad2BD4[0x2BDC - 0x2BD4];
    int m_roundsDecided;  /* 0x2BDC: rounds that ended with exactly one monster standing */
    int m_costume[8];     /* 0x2BE0: costume number of the first two players, then of the AIs */
    char pad2C00[0x2C10 - 0x2C00];
    int m_mode; /* 0x2C10: 1 = normal play, 8 = bigshot, 9 = crush */

    enum _shFileType { SH_FILE_0, SH_FILE_PLAYER, SH_FILE_AI };

    char *GetLevelName(void);
    void LoadLevelFiles(void);
    int MonsterExists(int i);
    int MonsterIsChosen(int i);
    void BootInitUi(void);
    void BootInitUserint(void);
    void BootInitGame(void);
    void InitBeforeUiDbLoad(void);
    void InitBeforeUserintDbLoad(void);
    void LoadUserintDB(void);
    void LoadUserintTexture(void);
    void InitRTState(void);
    void FinishLoadBar(void);
    void FadeScreen(int a, bool b, unsigned char c, unsigned char d, unsigned char e, unsigned char f, unsigned char g, unsigned char h);
    void InitPlayers(void);
    void InitPlayerLives(void);
    void EvaluateGameStatus(int r);
    void EvaluateOnePlayerStoryStatus(int r);
    void EvaluateOnePlayerChallengeStatus(int r);
    void EvaluateTwoPlayerCoopStatus(int r);
    void EvaluateMultiPlayerBattleStatusNoAI(int r);
    void EvaluateMultiPlayerBattleStatusAI(int r);
    void EvaluateOnePlayerEnduranceStatus(int r);
    void EvaluateBigShotStatus(int r);
    void EvaluateCrushStatus(int r);
    void EvaluateDodgeBallStatus(int r);
    void EvaluateOnlineBattleStatus(int r);
    void DisplayLoadBackground(bool b);
    void GenesisMovie(void);
    void InitGS(short mode);
    int Use30HzMode(void);
    int MonsterIsLocked(int i);
    void SelectAI(void);
    void RandomlySelectAI(void);
    void SetMenuItemFlag(int menu, int item, int on);
    void EnableMonsterSelection(int sel);
    void ResetLevel(void);
    void InitialMemCardScreen(void);
    void LoadLevelDB(void);
    void LoadMonstersDB(void);
    void LoadResTexture(void);
    void LoadTexture(void);
    _vramAddrs getVramAddr(void);
    void AddEpNode(int i, _hierhead *h);
    void formatFilename1(char *dst, const char *name, int costume, const char *ext, _shFileType t);
    void formatFilename(char *dst, const char *level, const char *ext, _shFileType t);
    static void formatFilename(char *dst, const char *a, const char *b, const char *c);
};
extern Shell *shell;

#define SHELL_AT(f, off) typedef char _shell_at_##f[(unsigned)&((Shell *)0)->f == (off) ? 1 : -1]
SHELL_AT(m_monsterFlags, 0x2A24);
SHELL_AT(m_killTarget, 0x2A44);
SHELL_AT(m_continues, 0x2B40);
SHELL_AT(m_battleMode, 0x2B54);
SHELL_AT(m_lives, 0x2BBC);
SHELL_AT(m_inSession, 0x2BCC);
SHELL_AT(m_roundsDecided, 0x2BDC);
SHELL_AT(m_mode, 0x2C10);
#undef SHELL_AT

#endif
