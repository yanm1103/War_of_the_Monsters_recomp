#ifndef PAD_FLAGS_H
#define PAD_FLAGS_H

#include "hieri_types.h"

class GamePad;

/* One per-frame snapshot of the pad state (94 bytes); only the fields seen so far are named. */
struct PadEntry {
    char data[0xA];
    unsigned short face[4]; /* 0x0A..0x10: four buttons, probably the face buttons; a fresh press of any of them is a struggle in StateImpaled */
    char pad12[0x16 - 0x12];
    unsigned short f16, f18, f1A, f1C, f1E, f20;
    short f22, f24, f26, f28;
    char pad2A[0x2E - 0x2A];
    unsigned short action;  /* 0x2E: grab/throw button (StateThrow; StateClimb starts on a fresh press too) */
    char pad30[0x32 - 0x30];
    unsigned short f32;
    unsigned short block;   /* 0x34: block button held (StateBlock) */
    char pad36[0x3E - 0x36];
    short f3E;
    char pad40[0x46 - 0x40];
    unsigned short counter; /* 0x46: counter button pressed (StateCounter) */
    char pad48[0x5C - 0x48];
    signed char f5C; /* retail reads it with lb */
    char pad5D;
};
typedef char _size_PadEntry[sizeof(PadEntry) == 0x5E ? 1 : -1];
typedef char _at_PadEntry_face[(unsigned)&((PadEntry *)0)->face == 0xA ? 1 : -1];
class Monster;

enum ButtonActions { BUTTON_ACTION_NONE = 20 };
enum MappedActions { MAPPED_ACTION_NONE = 0 };

/* Input interpretation state of one monster (embedded in Monster at 0x5040): a ring of 60 per-frame snapshots,
   a queue of pending button actions, combo/secret-code tracking and the pad tweak values copied from TheGame
   by UpdatePadTweaks. Offsets are relative to the PadFlags object (sizeof == 0x1814). */
class PadFlags {
public:
    enum { RING_SIZE = 60, ENTRY_SIZE = 0x5E };

    /* Action queue node (12 bytes), linked into the used list (sentinel at 0x1794) or the free list (sentinel at 0x17A0). */
    struct ActionNode {
        struct Record {
            int button;
            int mapped;
            int time;
        };
        Record *action; /* 0x0 */
        ActionNode *next; /* 0x4 */
        ActionNode *prev; /* 0x8 */
    };

    char ring[RING_SIZE * ENTRY_SIZE];  /* 0x0000 */
    char pad15F8[0x16C4 - RING_SIZE * ENTRY_SIZE];
    int tweak16C4;                      /* 0x16C4..0x16FC: tweaks copied from TheGame (see UpdatePadTweaks) */
    int tweak16C8;
    char pad16CC[4];
    float tweak16D0;
    float tweak16D4;
    char pad16D8[4];
    int tweak16DC;
    int tweak16E0;                      /* 0x16E0: movement threshold used by okToChangeMap */
    int f16E4;
    int f16E8;
    int f16EC;
    int ringIndex;                      /* 0x16F0: current slot in `ring`, wraps at 60 */
    int tweak16F4;
    int tweak16F8;
    int tweak16FC;
    int modifier1700;                   /* 0x1700..0x170C cleared by clearModifiers */
    int modifier1704;
    int modifier1708;
    int modifier170C;
    int curButtonAction;                /* 0x1710 */
    int curMappedAction;                /* 0x1714 */
    int curActionTime;                  /* 0x1718 */
    ActionNode nodes[10];               /* 0x171C */
    ActionNode usedList;                /* 0x1794: sentinel of pending actions (newest first) */
    ActionNode freeList;                /* 0x17A0 */
    int tweak17AC;                      /* 0x17AC: delay before an action is superseded */
    int tweak17B0;                      /* 0x17B0: delay before an action expires */
    int actionsStarted;                 /* 0x17B4 */
    char pad17B8[0x17E0 - 0x17B8];
    int f17E0, f17E4, f17E8;
    char pad17EC[4];
    int f17F0, f17F4, f17F8, f17FC;     /* combo tracking, cleared by clearCombo */
    char pad1800[4];
    int f1804, f1808, f180C;
    Monster *owner;                     /* 0x1810 */

    PadFlags();
    void init(Monster *m);
    void clear(int idx, int value);
    void saveAndClear(int value);
    void clearModifiers(void);
    PadEntry *operator[](int back);         /* ring entry `back` frames before the current one */
    void setCurrentAction(ButtonActions button, MappedActions mapped);
    void pushAction(ButtonActions button, MappedActions mapped);
    ButtonActions nextButtonAction(void);
    MappedActions nextMappedAction(void);
    void popAction(void);
    void interpretInputs(GamePad &pad);
    void computeMotionVec(_fvector &v);
    void computeMotionRot(void);
    void updateViewChanges(GamePad &pad);
    int okToChangeMap(void);
    int dashDoubleTap(GamePad &pad);
    void checkCombos(GamePad &pad);
    void checkTaunt(void);
    void clearSecretCode(void);
    void clearCombo(void);
};
typedef char _size_PadFlags[sizeof(PadFlags) == 0x1814 ? 1 : -1];

#endif
