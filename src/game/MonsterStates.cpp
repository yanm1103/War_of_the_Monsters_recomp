#include "common.h"
#include "game/game.h"
#include "game/monster_state.h"
#include "game/hit_event.h"
#include "game/pickup.h"
#include "game/level_pickups.h"
#include "game/shell.h"

int mathfRand(int lo, int hi);
extern int gUseUnifiedView;

/* States embedded in Monster (offsets from Monster's constructor). */
#define STATE_AT(m, off) ((MonsterState *)((char *)(m) + (off)))
#define ST_IDLE 0x7984
#define ST_COUNTER 0x7DD8
#define ST_RECOIL 0x7E30
#define ST_PUNCH 0x8470
#define ST_GRAPPLE 0xDCE0
#define ST_STUNNED 0x10714
#define STATE_ID_RECOIL 0x1C
/* virtual call through the retail vtable (entries are {this delta, 0, function}) */
#define VCALL_VOID(st, slot) \
    (((void (*)(void *))*(void **)((char *)(st)->vptr + (slot) + 4))((char *)(st) + *(short *)((char *)(st)->vptr + (slot))))
#define VT_TRANSITION_INTO 0x10

struct _hdResult;
class MonsterDynamics {
public:
    void updateTurn(bool b);
    void updateMove(bool b);
};
/* Counter attack (Monster+0x7DD8), entered from StateBlock. The counter animation is 0x40; counterSuccess (VU0, still
 * asm) marks `landed` when it connects. */
class StateCounter : public MonsterState {
public:
    char pad14[0x24 - 0x14];
    float blendTime;     /* 0x24: blend back into the block pose */
    char pad28[0x34 - 0x28];
    Monster *victim;     /* 0x34 */
    int anim;            /* 0x38: MonsterAnim playing (0x40 while countering) */
    int counterStart;    /* 0x3C */
    int counterEnd;      /* 0x40 */
    int landed;          /* 0x44: the counter hit `victim` */
    int landedHandled;   /* 0x48: the effects of the hit were applied */
    int stealsPickup;    /* 0x4C: the counter takes the victim's pickup */
    char pad50[0x58 - 0x50];

    int transitionOK(void);
    int transitionFeasible(void);
    void update(void);
    int acceptHit(HitEvent &e);
    void handleCollis(_hdResult &r);
    void handlePreemption(MonsterState *next);
};
typedef char _size_StateCounter[sizeof(StateCounter) == 0x58 ? 1 : -1];
class MonsterSound {
public:
    void playCounterAttackSound(void);
    void playObjectThrowSound(void);
    void playTauntSound(void);
    void playShockedSound(void);
    void playVictorySound(int anim);
    void playImpalerRemoveSound(void);
};
/* Animation overlay blend (layout still unknown; `active` is the byte cancelOverride checks). */
class AnimBlend {
public:
    char pad0[0xC];
    unsigned char active; /* 0x0C */

    void rampOut(float time);
};

/* Throwing the held pickup (Monster+0xEF40), animation 0x34. */
class StateThrow : public MonsterState {
public:
    char pad14[0x1C - 0x14];
    char blend[0x88 - 0x1C]; /* 0x1C: AnimBlend of the throw overlay */
    float rampOutTime;       /* 0x88: blend-out time of that overlay */
    char pad8C[0x90 - 0x8C];
    float blendTime;         /* 0x90: transition into the throw animation */
    float runFrames;         /* 0x94: length of the throw animation */
    float releasePercent;    /* 0x98: animation fraction at which the pickup leaves the hand */
    char pad9C[0xA0 - 0x9C];

    int transitionOK(void);
    void transitionInto(void);
    void update(void);
    void handlePreemption(MonsterState *next);
    void cancelOverride(void);
};
typedef char _size_StateThrow[sizeof(StateThrow) == 0xA0 ? 1 : -1];
#define THROW_BLEND(st) ((AnimBlend *)(st)->blend)

/* Taunting (Monster+0xFE20). Animation 0x5C, or 0x5D for the assault boss in its boss state 8. */
#define MONSTER_TYPE_ASSBOSS 0x1A0 /* (13 << 5): assboss in MonsterLongNames */
#define AI_OF(m) ((Ai *)(m)->m_ai)
#define GAME_STREAMING_SOUND ((StreamingSoundManager *)((char *)game + 0x1204C0)) /* TheGame member past game.h's layout */
class Ai {
public:
    int getAssBossState(void);
};
class StreamingSoundManager {
public:
    void updateTauntLocation(int monsterType, _fvector *pos);
};
class StateTaunt : public MonsterState {
public:
    float blendTime; /* 0x14 */

    int transitionOK(void);
    void transitionInto(void);
    void update(void);
};
typedef char _size_StateTaunt[sizeof(StateTaunt) == 0x18 ? 1 : -1];

/* On the receiving end of a counter (Monster+0x11114): animation 0x4C, attacks off while it plays. */
class StateCountered : public MonsterState {
public:
    float blendTime; /* 0x14 */
    float speed;     /* 0x18: playback speed of animation 0x4C */

    int transitionOK(void);
    void transitionInto(void);
    void update(void);
    void handlePreemption(MonsterState *next);
};
typedef char _size_StateCountered[sizeof(StateCountered) == 0x1C ? 1 : -1];
/* Big hit reaction (Monster+0x11100): animation 0x4B. */
class StateBigTakeHit : public MonsterState {
public:
    int transitionOK(void);
    void transitionInto(void);
    void update(void);
};
typedef char _size_StateBigTakeHit[sizeof(StateBigTakeHit) == 0x14 ? 1 : -1];

class StateGrappled : public MonsterState {
public:
    void detach(void);
};
#define ST_THROW 0xEF40
#define ST_GRAPPLED 0xDDCC
#define ST_SHOCKED 0x10BA0
#define ST_KNOCKBACK 0x7EA0
#define STATE_ID_SHOCKED 0x38
/* Monster's own vtable sits at +0x10 like the states'; slot 0x20 is getVel */
#define MONSTER_VEL(m) \
    ((float *)((void *(*)(void *))*(void **)(*(char **)((char *)(m) + 0x10) + 0x24))((char *)(m) + *(short *)(*(char **)((char *)(m) + 0x10) + 0x20)))
/* Electrocuted (Monster+0x10BA0): animation 0x4A looping, the monster frozen in place; a grapple partner is shocked
 * too and both let go. After `duration` frames it is knocked back. */
class StateShocked : public MonsterState {
public:
    unsigned duration;  /* 0x14: frames */
    int bigKnock;       /* 0x18: use the second (stronger) knockback once */
    float blendTime;    /* 0x1C */
    _fvector knockDir;  /* 0x20 */
    float knock[2];     /* 0x30: knockBack strengths */
    float bigKnockF[2]; /* 0x38: strengths when bigKnock */
    float damage;       /* 0x40: setDamage */
    char pad44[0x50 - 0x44];

    int transitionOK(void);
    void transitionInto(void);
    void update(void);
    void handlePreemption(MonsterState *next);
};
typedef char _size_StateShocked[sizeof(StateShocked) == 0x50 ? 1 : -1];
/* Throwing a type 7 pickup (javelin-like) (Monster+0xFE38), animation 0x36. */
class StateJavelin : public MonsterState {
public:
    char pad14[0x1C - 0x14];
    float blendTime;      /* 0x1C */
    float releasePercent; /* 0x20 */

    int transitionOK(void);
    void transitionInto(void);
    void update(void);
};
typedef char _size_StateJavelin[sizeof(StateJavelin) == 0x24 ? 1 : -1];

class FireBreath {
public:
    void ApplyMint(void); /* puts out the burning effect */
};
class MotionBlurAA {
public:
    static void setAlpha(float alpha, int view);
    static void setEnable(bool on);
};
extern char bigShotLevel[] __asm__("_12BigShotLevel$instance");
#define BIGSHOT_E8 (*(int *)(bigShotLevel + 0xE8))
/* Victory celebration (Monster+0x10E70): one of animations 0x82..0x84 (0x5C when missing), the camera on the
 * winner, invulnerable until it ends. */
class StateVictory : public MonsterState {
public:
    int anim;    /* 0x14: getVictoryAnim */
    float alpha; /* 0x18: motion blur fade */
    char pad1C[4];

    int transitionOK(void);
    void transitionInto(void);
    void update(void);
    int fade(void);
    int acceptHit(HitEvent &e);
    static int fade(void *self);
};
typedef char _size_StateVictory[sizeof(StateVictory) == 0x20 ? 1 : -1];
/* Impaled (Monster+0x106E0): stuck on an impaler pickup, the player mashes to break free. `hold` starts at 100, regains
 * regenRate per field and loses struggleCost per fresh press of any of the four PadEntry::face buttons; at 0 the monster
 * pulls the impaler out (animation 0x90) and ends up holding it. */
class StateImpaled : public MonsterState {
public:
    float blendIn;       /* 0x14: into the stuck loop (animation 0x8E) */
    float struggleBlend; /* 0x18: into a struggle animation (0x10F/0x110) */
    float pullBlend;     /* 0x1C: into the pull-out (0x90) */
    float regenRate;     /* 0x20: hold regained per field */
    float struggleCost;  /* 0x24: hold lost per press */
    int lastPressed;     /* 0x28 */
    int anim;            /* 0x2C */
    float hold;          /* 0x30 */

    int transitionOK(void);
    void transitionInto(void);
    void update(void);
    void handleCollis(_hdResult &r);
    void handlePreemption(MonsterState *next);
};
typedef char _size_StateImpaled[sizeof(StateImpaled) == 0x34 ? 1 : -1];
#define ST_COUNTERED 0x11114
#define ST_BLOCK 0x7DA0
#define VCALL_INT(st, slot) \
    (((int (*)(void *))*(void **)((char *)(st)->vptr + (slot) + 4))((char *)(st) + *(short *)((char *)(st)->vptr + (slot))))
#define VT_TRANSITION_FEASIBLE 0x28
class StatePunch : public MonsterState {
public:
    int transitionOK(void);
};
class StateGrapple : public MonsterState {
public:
    int transitionOK(void);
    void detach(void);
};
class StateStunned : public MonsterState {
public:
    void handleCollis(_hdResult &r);
};

/* Blocking (Monster+0x7DA0). The block pose depends on what the monster holds: animations 0x3C (bare), 0x3D (one-handed
 * pickup), 0x3E (two-handed pickup, or a locked target), 0x3F (pickup type 0x10), each only if the monster has it. While
 * blocking the damage taken is scaled by damageScale (damageScaleArmed when holding something or locked on). */
class StateBlock : public MonsterState {
public:
    float autoBlockRange;   /* 0x14: autoBlock looks for an attacking monster this close */
    float blendTime;        /* 0x18: animation blend into the block pose */
    float runFrames;        /* 0x1C: length of the block animation */
    float damageScale;      /* 0x20 */
    float damageScaleArmed; /* 0x24 */
    int unk28;
    unsigned raiseFrames;   /* 0x2C: frames with the button held before the block counts */
    int blocking;           /* 0x30: hits are being blocked (isBlocking) */
    int anim;               /* 0x34: MonsterAnim of the current pose */

    int transitionOK(void);
    void transitionInto(void);
    int chooseBlock(void);
    void update(void);
    void handleCollis(_hdResult &r);
    void handlePreemption(MonsterState *next);
    int autoBlock(void);
    int isBlockable(HitEvent &e);
};
typedef char _size_StateBlock[sizeof(StateBlock) == 0x38 ? 1 : -1];

INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", collisTestCloseRangeAttack__20PunchSwipeConfigBaseiP7MonsterR10HitHistoryi);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __12MonsterStateQ212MonsterState2IdUi);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", update__12MonsterState);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", handleCollis__12MonsterStateR9_hdResult);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __11AttackStateQ212MonsterState2IdUi);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", creditStaminaForAttack__11AttackStatef);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __9StateIdle);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionInto__9StateIdle);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", chooseBaseIdle__9StateIdle);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", chooseFancyIdle__9StateIdle);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", update__9StateIdle);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", startBaseAnim__9StateIdle11MonsterAnim);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", handlePreemption__9StateIdleP12MonsterState);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __13StateBatSwipe);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionOK__13StateBatSwipe);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionInto__13StateBatSwipe);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", update__13StateBatSwipe);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", startChain__13StateBatSwipeiiRQ213StateBatSwipe11SwipeConfig);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", handleCollis__13StateBatSwipeR9_hdResult);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", handlePreemption__13StateBatSwipeP12MonsterState);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", acceptHit__13StateBatSwipeR8HitEvent);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __10StateBlock);
/* Blocking needs the bare block animation, attacks enabled, the feet on something and the block button held. */
int StateBlock::transitionOK(void)
{
    Monster *m = owner;

    if (m->m_anims[0x3C].a == 0 || m->m_attacksEnabled == 0 || m->m_freeFalling != 0)
        return 0;
    return m->m_padFlags[0]->block != 0;
}
/* Coming out of a recoil (state 0x1C) the block is up at once, and recoil animation 0x4D blends in slowly. */
#ifdef NON_MATCHING
/* untuned: 23/56 words; tools/difftest.py 200/200 */
void StateBlock::transitionInto(void)
{
    float blend;

    frames = 0;
    anim = chooseBlock();
    blocking = 0;
    blend = blendTime;
    if (owner->m_prevState[0] == STATE_ID_RECOIL) {
        if (*(int *)((char *)STATE_AT(owner, ST_RECOIL) + 0x64) != 0)
            blocking = 1;
        if (*(int *)((char *)STATE_AT(owner, ST_RECOIL) + 0x38) == 0x4D)
            blend = 140.0f;
    }
    animationSetTotalRunFrames(owner->m_anims[anim], runFrames);
    animationTransitionInto(owner->m_anims[anim], blend, 0, 1);
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionInto__10StateBlock);
#endif
/* Picks the block pose and sets the damage scale for it. */
#ifdef NON_MATCHING
/* untuned: 8/39 words (movn vs branches); tools/difftest.py 200/200 */
int StateBlock::chooseBlock(void)
{
    Monster *m = owner;
    int pose = 0x3C;

    if (m->m_pickup != 0) {
        Pickup *p = *(Pickup **)m->m_pickup;

        if ((p->bits >> 1) & 1) {
            if (m->m_anims[0x3E].a != 0)
                pose = 0x3E;
        } else if (p->pickupType == 0x10) {
            if (m->m_anims[0x3F].a != 0)
                pose = 0x3F;
        } else if (m->m_anims[0x3D].a != 0) {
            pose = 0x3D;
        }
        m->m_damageModifier = damageScaleArmed;
    } else if (m->m_target != 0) {
        m->m_damageModifier = damageScaleArmed;
        if (m->m_anims[0x3E].a != 0)
            pose = 0x3E;
    } else {
        m->m_damageModifier = damageScale;
    }
    return pose;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", chooseBlock__10StateBlock);
#endif
/* Holding the button keeps blocking (after raiseFrames) and allows a counter; letting go punches, grapples or idles. */
#ifdef NON_MATCHING
/* untuned: 68/90 words; tools/difftest.py 200/200 */
void StateBlock::update(void)
{
    MonsterState::update();
    if (owner->m_unk1E8 < 0.0f)
        owner->m_unk1B8 = 1.5f;
    ((MonsterDynamics *)((char *)owner + 0x100))->updateTurn(true);
    ((MonsterDynamics *)((char *)owner + 0x100))->updateMove(false);
    owner->updateLock((MonsterReticleState)1);
    if (owner->m_padFlags[0]->block != 0) {
        if (frames >= raiseFrames)
            blocking = 1;
        if (((StateCounter *)STATE_AT(owner, ST_COUNTER))->transitionOK())
            owner->enterNewState(STATE_AT(owner, ST_COUNTER));
    } else if (((StatePunch *)STATE_AT(owner, ST_PUNCH))->transitionOK()) {
        owner->enterNewState(STATE_AT(owner, ST_PUNCH));
    } else if (((StateGrapple *)STATE_AT(owner, ST_GRAPPLE))->transitionOK()) {
        owner->enterNewState(STATE_AT(owner, ST_GRAPPLE));
    } else {
        owner->enterNewState(STATE_AT(owner, ST_IDLE));
    }
    /* lost both the target and the pickup while in the armed pose: start over with a new pose */
    if (owner->m_target == 0 && owner->m_pickup == 0 && anim == 0x3E)
        VCALL_VOID(this, VT_TRANSITION_INTO);
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", update__10StateBlock);
#endif
void StateBlock::handleCollis(_hdResult &r)
{
    ((StateStunned *)STATE_AT(owner, ST_STUNNED))->handleCollis(r);
}
void StateBlock::handlePreemption(MonsterState *next)
{
    owner->m_unk1B8 = 1.0f;
}
/* Used by the AI: block if the closest monster in range is in one of the attack states (3, 0x1A, 0x28, 0x29). */
int StateBlock::autoBlock(void)
{
    Monster *m = owner->getClosestMonster(autoBlockRange);

    if (m == 0)
        return 0;
    switch (m->m_state[0]) {
    case 3:
    case 0x1A:
    case 0x28:
    case 0x29:
        return 1;
    }
    return 0;
}
/* Sources 6 and 0x14 can't be blocked, nor source 5 with detail 0x40. */
#ifdef NON_MATCHING
/* untuned: 8/17 words; tools/difftest.py 200/200 */
int StateBlock::isBlockable(HitEvent &e)
{
    if (e.source == 6 || e.source == 0x14)
        return 0;
    if (e.source != 5)
        return 1;
    if (e.sourceArg == 0x40)
        return 0;
    return 1;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", isBlockable__10StateBlockR8HitEvent);
#endif
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __10StateCatch);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionOK__10StateCatch);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionFeasible__10StateCatch);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionInto__10StateCatch);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", update__10StateCatch);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", handlePreemption__10StateCatchP12MonsterState);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __12StateCounter);
/* A counter needs the counter button and stamina that is not exhausted. */
int StateCounter::transitionOK(void)
{
    int ok;

    if (owner->m_stamina.exhausted != 0)
        return 0;
    ok = 0;
    if (owner->m_padFlags[0]->counter != 0)
        ok = VCALL_INT(this, VT_TRANSITION_FEASIBLE) != 0;
    return ok;
}
/* Not while locked on a target, only with the counter animation, and not holding a two-handed pickup. */
#ifdef NON_MATCHING
/* untuned: 3/21 words (retail has three hazard nops after the first branch); tools/difftest.py 200/200 */
int StateCounter::transitionFeasible(void)
{
    Monster *m = owner;

    if (m->m_target != 0 || m->m_anims[0x40].a == 0)
        return 0;
    if (m->m_pickup == 0)
        return 1;
    return !(((*(Pickup **)m->m_pickup)->bits >> 1) & 1);
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionFeasible__12StateCounter);
#endif
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionInto__12StateCounter);
/* Once the counter lands: the victim is countered (and loses its pickup to us if stealsPickup and our hands are free),
 * HUD message 6 and the counter sound. When the counter animation ends without landing, back to the block pose (or
 * animation 0x5A); after a landed counter or any other animation, back to Block or Idle by the block button. */
#ifdef NON_MATCHING
/* untuned: 57/153 words; tools/difftest.py 200/200 */
void StateCounter::update(void)
{
    MonsterState::update();
    if (owner->m_unk1E8 < 0.0f)
        owner->m_unk1B8 = 1.5f;
    if (landed != 0 && landedHandled == 0 && victim != 0) {
        if (stealsPickup != 0) {
            int held = victim->m_pickup;

            if (held != 0) {
                if (owner->m_pickup != 0) {
                    victim->dropPickup();
                } else {
                    victim->dropPickup();
                    owner->m_pickup = held;
                    LevelPickups::grabPickup(*(PickupIter *)&held, owner->m_id);
                }
            }
        }
        victim->enterNewState(STATE_AT(victim, ST_COUNTERED));
        game->m_huds[owner->m_cameraView].addMessage(6, 0);
        ((MonsterSound *)owner->m_sound)->playCounterAttackSound();
        landedHandled = 1;
    }
    if (anim == 0x40) {
        if (!animationIsRunning(owner->m_anims[0x40])) {
            if (landed == 0) {
                int pose = 0x5A;

                if (owner->m_padFlags[0]->block != 0)
                    pose = ((StateBlock *)STATE_AT(owner, ST_BLOCK))->chooseBlock();
                anim = pose;
                animationTransitionInto(owner->m_anims[pose], blendTime, 1, 1);
            } else {
                owner->enterNewState(STATE_AT(owner, owner->m_padFlags[0]->block != 0 ? ST_BLOCK : ST_IDLE));
            }
        }
    } else if (!animationIsTransitioning(owner->m_anims[anim])) {
        owner->enterNewState(STATE_AT(owner, owner->m_padFlags[0]->block != 0 ? ST_BLOCK : ST_IDLE));
    }
    ((MonsterDynamics *)((char *)owner + 0x100))->updateTurn(true);
    ((MonsterDynamics *)((char *)owner + 0x100))->updateMove(false);
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", update__12StateCounter);
#endif
/* A counter can't be refused. */
int StateCounter::acceptHit(HitEvent &e)
{
    return 1;
}
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", counterSuccess__12StateCounterR7MonsterR8_fvector);
void StateCounter::handleCollis(_hdResult &r)
{
    ((StateStunned *)STATE_AT(owner, ST_STUNNED))->handleCollis(r);
}
void StateCounter::handlePreemption(MonsterState *next)
{
    owner->m_unk1B8 = 1.0f;
}
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __14StateCountered);
int StateCountered::transitionOK(void)
{
    return owner->m_anims[0x4C].a != 0;
}
void StateCountered::transitionInto(void)
{
    frames = 0;
    animationSetSpeed(owner->m_anims[0x4C], speed);
    animationTransitionInto(owner->m_anims[0x4C], blendTime, 1, 1);
    owner->m_attacksEnabled = 0;
}
void StateCountered::update(void)
{
    MonsterState::update();
    if (animationGetCurrentPercent(owner->m_anims[0x4C]) >= 1.0f)
        owner->enterNewState(STATE_AT(owner, ST_IDLE));
    ((MonsterDynamics *)((char *)owner + 0x100))->updateMove(false);
    ((MonsterDynamics *)((char *)owner + 0x100))->updateTurn(false);
}
void StateCountered::handlePreemption(MonsterState *next)
{
    owner->m_attacksEnabled = 1;
}
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __10StateDeath);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionOK__10StateDeath);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionInto__10StateDeath);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", update__10StateDeath);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", findVictor__10StateDeath);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", handlePreemption__10StateDeathP12MonsterState);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", setKiller__10StateDeathP7Monster);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __14StateGunAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionOK__14StateGunAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionInto__14StateGunAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", update__14StateGunAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __16StateGetupAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionOK__16StateGetupAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionInto__16StateGetupAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", update__16StateGetupAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", enterSubState__16StateGetupAttackQ216StateGetupAttack8SubState);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", acceptHit__16StateGetupAttackR8HitEvent);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __12StateImpaled);
/* Needs the stuck, struggle and pull-out animations (0x8E, 0x8F, 0x90). */
int StateImpaled::transitionOK(void)
{
    Monster *m = owner;

    if (m->m_anims[0x8E].a == 0 || m->m_anims[0x8F].a == 0 || m->m_anims[0x90].a == 0)
        return 0;
    return 1;
}
/* In the central level's normal mode player 1 gets the tutorial text box 0x1C; otherwise player 1 (if alive) gets HUD
 * message 0x24. */
#ifdef NON_MATCHING
/* untuned: 51/79 words (retail lays out the early return differently); tools/difftest.py 200/200 */
void StateImpaled::transitionInto(void)
{
    frames = 0;
    ((StateThrow *)STATE_AT(owner, ST_THROW))->cancelOverride();
    owner->m_attacksEnabled = 0;
    anim = 0x8E;
    animationLoop(owner->m_anims[0x8E], true);
    animationTransitionInto(owner->m_anims[anim], blendIn, 1, 1);
    hold = 100.0f;
    if (game->m_gameMode == 1 && game->m_levelId == 1 && owner->m_playerNum == 1) {
        game->m_huds[0].addTextBoxMessage(0x1C);
        return;
    }
    if (owner->m_playerNum == 1 && owner->m_health > 0.0f)
        game->m_huds[owner->m_cameraView].addMessage(0x24, 0);
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionInto__12StateImpaled);
#endif
#ifdef NON_MATCHING
/* untuned: 113/220 words; tools/difftest.py 200/200 */
void StateImpaled::update(void)
{
    PadEntry *p;
    int pressed;
    int done;

    MonsterState::update();
    if (owner->m_unk1E8 < 0.0f)
        owner->m_unk1B8 = 1.5f;
    ((MonsterDynamics *)((char *)owner + 0x100))->updateTurn(false);
    ((MonsterDynamics *)((char *)owner + 0x100))->updateMove(false);
    if (anim == 0x90) {
        if (animationGetCurrentPercent(owner->m_anims[0x90]) >= 0.99f)
            owner->enterNewState(STATE_AT(owner, ST_IDLE));
        return;
    }
    hold = hold + (float)timerGetFieldsLastFrame() * regenRate;
    if (hold > 100.0f)
        hold = 100.0f;
    pressed = 0;
    if (owner->m_padFlags[0]->face[2] != 0 || owner->m_padFlags[0]->face[3] != 0 ||
        owner->m_padFlags[0]->face[0] != 0 || owner->m_padFlags[0]->face[1] != 0)
        pressed = 1;
    if (pressed && lastPressed == 0) {
        hold = hold - struggleCost;
        if (anim == 0x8E) {
            anim = mathfRand(0, 1) + 0x10F;
            animationSetSpeed(owner->m_anims[anim], 1.5f);
            animationTransitionInto(owner->m_anims[anim], struggleBlend, 1, 1);
        }
    } else {
        done = 0;
        if (animationGetCurrentPercent(owner->m_anims[anim]) >= 1.0f)
            done = !animationIsRunning(owner->m_anims[0x8E]);
        if (done) {
            anim = 0x8E;
            animationTransitionInto(owner->m_anims[0x8E], blendIn, 0, 1);
        }
    }
    lastPressed = pressed;
    if (hold <= 0.0f) {
        anim = 0x90;
        animationTransitionInto(owner->m_anims[0x90], pullBlend, 1, 1);
        owner->m_pickup = owner->m_impaler != 0 ? owner->m_impaler : owner->m_reverseImpaler;
        LevelPickups::grabPickup(*(PickupIter *)&owner->m_pickup, owner->m_id);
        owner->detachPickupImpaler(false);
        ((MonsterSound *)owner->m_sound)->playImpalerRemoveSound();
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", update__12StateImpaled);
#endif
void StateImpaled::handleCollis(_hdResult &r)
{
    ((StateStunned *)STATE_AT(owner, ST_STUNNED))->handleCollis(r);
}
/* Leaving for anything but states 0x2D and 0x21 lets go of the impaler. */
void StateImpaled::handlePreemption(MonsterState *next)
{
    owner->m_attacksEnabled = 1;
    owner->m_unk1B8 = 1.0f;
    if (next->id != 0x2D && next->id != 0x21)
        owner->detachPickupImpaler(true);
}
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __12StateJavelin);
/* Attacks enabled, the javelin animation, a fresh press of the action button and a type 7 pickup in hand. */
#ifdef NON_MATCHING
/* untuned: 17/37 words; tools/difftest.py 200/200 */
int StateJavelin::transitionOK(void)
{
    Monster *m = owner;
    int isJavelin;

    if (m->m_attacksEnabled == 0)
        return 0;
    if (m->m_anims[0x36].a == 0 || m->m_padFlags[0]->action == 0)
        return 0;
    if (owner->m_padFlags[1]->action != 0)
        return 0;
    isJavelin = 0;
    if (owner->m_pickup != 0)
        isJavelin = (*(Pickup **)owner->m_pickup)->pickupType == 7;
    if (isJavelin)
        return 1;
    return 0;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionOK__12StateJavelin);
#endif
void StateJavelin::transitionInto(void)
{
    frames = 0;
    animationTransitionInto(owner->m_anims[0x36], blendTime, 1, 1);
    owner->m_unk49 = 1;
    ((MonsterSound *)owner->m_sound)->playObjectThrowSound();
}
/* The pickup leaves the hand (throwPickup 0x80A) once the animation passes releasePercent; at the end back to Idle, or
 * to the state m_stateRef points at when falling. */
#ifdef NON_MATCHING
/* untuned: 33/65 words; tools/difftest.py 200/200 */
void StateJavelin::update(void)
{
    float pct;

    MonsterState::update();
    ((MonsterDynamics *)((char *)owner + 0x100))->updateTurn(true);
    ((MonsterDynamics *)((char *)owner + 0x100))->updateMove(false);
    owner->updateLock((MonsterReticleState)1);
    pct = animationGetCurrentPercent(owner->m_anims[0x36]);
    if (pct >= 1.0f) {
        if (owner->m_freeFalling != 0)
            owner->enterNewState((MonsterState *)owner->m_stateRef);
        else
            owner->enterNewState(STATE_AT(owner, ST_IDLE));
        return;
    }
    if (owner->m_pickup != 0 && releasePercent <= pct)
        owner->throwPickup(0x80A, 0.0f);
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", update__12StateJavelin);
#endif
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __10StatePunch);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionOK__10StatePunch);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionInto__10StatePunch);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", update__10StatePunch);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", startChain__10StatePunchiiRQ210StatePunch11PunchConfig);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", playPunchEffect__10StatePunchR8_fvector);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", handleCollis__10StatePunchR9_hdResult);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", handlePreemption__10StatePunchP12MonsterState);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", acceptHit__10StatePunchR8HitEvent);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", getFistSize__10StatePunch);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __11StatePickUp);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionOK__11StatePickUp);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionFeasible__11StatePickUp);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionInto__11StatePickUp);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", update__11StatePickUp);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", getAttachRegion__11StatePickUpR8_fvectorT1);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", handlePreemption__11StatePickUpP12MonsterState);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __11StateRecoil);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", init__11StateRecoilP7Monster);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", getRecoilAnim__11StateRecoil);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionOK__11StateRecoil);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionInto__11StateRecoil);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", update__11StateRecoil);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", acceptHit__11StateRecoilR8HitEvent);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", handlePreemption__11StateRecoilP12MonsterState);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __12StateStunned);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionOK__12StateStunned);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionInto__12StateStunned);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", update__12StateStunned);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", handlePreemption__12StateStunnedP12MonsterState);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", handleCollis__12StateStunnedR9_hdResult);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", setStarsCs__12StateStunnedP3_cs);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", updateStars__12StateStunned);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __10StateTaunt);
/* A taunt starts when one was requested (m_wantsTaunt) and the monster has the taunt animation; plays the taunt sound. */
int StateTaunt::transitionOK(void)
{
    if (owner->m_anims[0x5C].a == 0 || owner->m_wantsTaunt != 1)
        return 0;
    ((MonsterSound *)owner->m_sound)->playTauntSound();
    owner->m_wantsTaunt = 0;
    return 1;
}
void StateTaunt::transitionInto(void)
{
    frames = 0;
    if (owner->m_typeBits == MONSTER_TYPE_ASSBOSS) {
        if (AI_OF(owner)->getAssBossState() == 8)
            animationTransitionInto(owner->m_anims[0x5D], blendTime, 1, 1);
        else
            animationTransitionInto(owner->m_anims[0x5C], blendTime, 1, 1);
    } else {
        animationTransitionInto(owner->m_anims[0x5C], blendTime, 1, 1);
    }
}
/* The boss keeps turning towards its target while taunting; everyone else stands. Back to Idle when the animation ends;
 * the taunt sound follows the monster. */
#ifdef NON_MATCHING
/* untuned: 53/101 words; tools/difftest.py 200/200 (which handle goes to animationGetCurrentPercent is not visible to it: checked in the asm) */
void StateTaunt::update(void)
{
    float pct;

    MonsterState::update();
    if (owner->m_typeBits == MONSTER_TYPE_ASSBOSS) {
        ((MonsterDynamics *)((char *)owner + 0x100))->updateTurn(true);
        owner->updateLock((MonsterReticleState)1);
    } else {
        ((MonsterDynamics *)((char *)owner + 0x100))->updateTurn(false);
    }
    ((MonsterDynamics *)((char *)owner + 0x100))->updateMove(false);
    if (owner->m_typeBits == MONSTER_TYPE_ASSBOSS) {
        if (AI_OF(owner)->getAssBossState() == 8)
            pct = animationGetCurrentPercent(owner->m_anims[0x5D]);
        else
            pct = animationGetCurrentPercent(owner->m_anims[0x5C]);
        if (pct >= 1.0f)
            owner->enterNewState(STATE_AT(owner, ST_IDLE));
    } else if (animationGetCurrentPercent(owner->m_anims[0x5C]) >= 1.0f) {
        owner->enterNewState(STATE_AT(owner, ST_IDLE));
    }
    GAME_STREAMING_SOUND->updateTauntLocation(owner->m_typeBits, (_fvector *)((char *)owner->m_cs + 0x10));
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", update__10StateTaunt);
#endif
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __10StateThrow);
/* Throwing needs the throw animation, a pickup that is neither type 7 nor two-handed, attacks enabled and a fresh press
 * of the action button (plays the throw sound). */
#ifdef NON_MATCHING
/* untuned: 14/51 words; tools/difftest.py 200/200 */
int StateThrow::transitionOK(void)
{
    Monster *m = owner;
    int cant;

    if (m->m_anims[0x34].a == 0)
        return 0;
    cant = 0;
    if (m->m_pickup == 0) {
        cant = 1;
    } else {
        Pickup *p = *(Pickup **)m->m_pickup;

        if (p->pickupType == 7 || ((p->bits >> 1) & 1))
            cant = 1;
    }
    if (cant)
        return 0;
    if (owner->m_attacksEnabled == 0 || owner->m_padFlags[0]->action == 0)
        return 0;
    if (owner->m_padFlags[1]->action != 0)
        return 0;
    ((MonsterSound *)owner->m_sound)->playObjectThrowSound();
    return 1;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionOK__10StateThrow);
#endif
/* Blends out the carry overlay (if the monster has animation 0x32) and starts the throw. */
void StateThrow::transitionInto(void)
{
    frames = 0;
    if (owner->m_anims[0x32].a != 0)
        THROW_BLEND(this)->rampOut(rampOutTime);
    animationSetTotalRunFrames(owner->m_anims[0x34], runFrames);
    animationTransitionInto(owner->m_anims[0x34], blendTime, 1, 7);
    owner->m_unk49 = 1;
}
/* The pickup is released (throwPickup 0x80A) once the animation passes releasePercent; at the end back to Idle, or to
 * the state m_stateRef points at when falling. */
#ifdef NON_MATCHING
/* untuned: 45/67 words; tools/difftest.py 200/200 */
void StateThrow::update(void)
{
    float pct;

    MonsterState::update();
    ((MonsterDynamics *)((char *)owner + 0x100))->updateTurn(true);
    ((MonsterDynamics *)((char *)owner + 0x100))->updateMove(false);
    owner->updateLock((MonsterReticleState)1);
    pct = animationGetCurrentPercent(owner->m_anims[0x34]);
    if (pct >= 1.0f) {
        if (owner->m_freeFalling != 0)
            owner->enterNewState((MonsterState *)owner->m_stateRef);
        else
            owner->enterNewState(STATE_AT(owner, ST_IDLE));
        return;
    }
    if (owner->m_pickup != 0 && releasePercent <= pct) {
        owner->m_attacksEnabled = 1;
        owner->throwPickup(0x80A, 0.0f);
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", update__10StateThrow);
#endif
void StateThrow::handlePreemption(MonsterState *next)
{
    cancelOverride();
}
void StateThrow::cancelOverride(void)
{
    AnimBlend *b = THROW_BLEND(this);

    if (b->active) {
        b->rampOut(rampOutTime);
        owner->m_attacksEnabled = 1;
    }
}
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __19StateTwoHandedThrow);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionOK__19StateTwoHandedThrow);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionFeasible__19StateTwoHandedThrow);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionInto__19StateTwoHandedThrow);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", update__19StateTwoHandedThrow);
void handlePreemption__19StateTwoHandedThrowP12MonsterState(void *self) __asm__("handlePreemption__19StateTwoHandedThrowP12MonsterState");
void handlePreemption__19StateTwoHandedThrowP12MonsterState(void *self)
{
}
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __12StateShocked);
/* Needs the shocked animation, not already shocked, and m_unk49 set. */
int StateShocked::transitionOK(void)
{
    Monster *m = owner;

    if (m->m_anims[0x4A].a == 0)
        return 0;
    if (m->m_state[0] == STATE_ID_SHOCKED)
        return 0;
    return m->m_unk49 != 0;
}
void StateShocked::transitionInto(void)
{
    Monster *other;

    frames = 0;
    ((StateThrow *)STATE_AT(owner, ST_THROW))->cancelOverride();
    owner->m_attacksEnabled = 0;
    animationLoop(owner->m_anims[0x4A], true);
    animationTransitionInto(owner->m_anims[0x4A], blendTime, 1, 1);
    damage = 0.0f;
    other = (Monster *)owner->m_target;
    if (other != 0) {
        /* we were grappling: the victim is shocked too */
        ((StateGrappled *)STATE_AT(other, ST_GRAPPLED))->detach();
        other->enterNewState(STATE_AT(other, ST_SHOCKED));
        ((StateGrapple *)STATE_AT(owner, ST_GRAPPLE))->detach();
    } else if ((other = owner->m_grappler) != 0) {
        /* we were being grappled: so is the grappler */
        ((StateGrappled *)STATE_AT(owner, ST_GRAPPLED))->detach();
        ((StateGrapple *)STATE_AT(other, ST_GRAPPLE))->detach();
        other->enterNewState(STATE_AT(other, ST_SHOCKED));
    }
    ((MonsterSound *)owner->m_sound)->playShockedSound();
}
void StateShocked::update(void)
{
    MonsterState::update();
    ((MonsterDynamics *)((char *)owner + 0x100))->updateTurn(false);
    ((MonsterDynamics *)((char *)owner + 0x100))->updateMove(false);
    MONSTER_VEL(owner)[0] = 0.0f;
    MONSTER_VEL(owner)[1] = 0.0f;
    MONSTER_VEL(owner)[2] = 0.0f;
    owner->m_unk1B8 = 0.5f;
    if (frames > duration) {
        owner->m_unk1B8 = 1.0f;
        if ((char *)owner->m_state == (char *)STATE_AT(owner, ST_KNOCKBACK))
            return;
        if (bigKnock != 0) {
            bigKnock = 0;
            owner->knockBack(knockDir, bigKnockF[0], bigKnockF[1]);
        } else {
            owner->knockBack(knockDir, knock[0], knock[1]);
        }
    }
}
void StateShocked::handlePreemption(MonsterState *next)
{
    owner->m_attacksEnabled = 1;
    owner->m_unk1B8 = 1.0f;
}
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __16StateStompAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionOK__16StateStompAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionFeasible__16StateStompAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionInto__16StateStompAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", update__16StateStompAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", startChain__16StateStompAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __16StateTazerAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionOK__16StateTazerAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionInto__16StateTazerAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", update__16StateTazerAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", launchProjectile__16StateTazerAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __17StateShieldAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionOK__17StateShieldAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionFeasible__17StateShieldAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionInto__17StateShieldAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", update__17StateShieldAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __12StateVictory);
/* When a monster may celebrate. Never in game mode 4; in modes 2 and 3 only once its wins reach the shell's kill
 * target; in mode 6 only when the other player has no continues left. Not twice, not in the air, not in the middle of
 * a reaction (other than its sub-state 4). Starting it also puts out a fire on the monster. */
#ifdef NON_MATCHING
/* untuned: 12/92 words; tools/difftest.py 200/200 */
int StateVictory::transitionOK(void)
{
    Monster *m;
    int mode = game->m_gameMode;
    int notYet;

    if (mode == 4)
        return 0;
    if (mode == 3 || mode == 2) {
        if (shell->m_killTarget == 0)
            return 0;
        m = owner;
        notYet = game->m_slots[m->m_monsterNum].m_winsThisGame < shell->m_killTarget;
        if (notYet)
            return 0;
    } else {
        m = owner;
        if (mode == 6) {
            notYet = m->m_index != 0 ? shell->m_continues[0] : shell->m_continues[1];
            if (notYet)
                return 0;
        }
    }
    if (m->m_unkF7 != 0)
        return 0;
    if (m->m_onFireCount > 0.0f)
        m->m_onFireCount = 0.0f;
    if (*(int *)owner->m_fireBreath != 0)
        ((FireBreath *)owner->m_fireBreath)->ApplyMint();
    m = owner;
    if ((((MonsterState *)m->m_state)->flags & 0x10) && *(int *)((char *)m->m_state + 0x260) != 4)
        return 0;
    if (m->m_state[0] == 0x1F) {
        m->enterNewState((MonsterState *)m->m_stateRef);
        return 0;
    }
    if (m->m_freeFalling != 0)
        return 0;
    return m->m_anims[0x5C].a != 0;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionOK__12StateVictory);
#endif
#ifdef NON_MATCHING
/* untuned: 89/91 words (two registers swapped); tools/difftest.py 200/200 */
void StateVictory::transitionInto(void)
{
    int drop;
    int mode;

    frames = 0;
    anim = mathfRand(0, 2) + 0x82;
    if (owner->m_anims[anim].a == 0)
        anim = 0x5C;
    animationTransitionInto(owner->m_anims[anim], 100.0f, 1, 1);
    drop = 0;
    if (owner->m_pickup != 0)
        drop = game->m_gameMode != 7;
    if (drop) {
        *((char *)(*(Pickup **)owner->m_pickup)->cs + 0xC) = 1; /* cs->drawMe */
        owner->dropPickup();
    }
    mode = game->m_gameMode;
    if (mode == 9 || (mode == 8 && BIGSHOT_E8 == 0)) {
        Cameras::SetCameraMonster(0, owner);
        Cameras::SetCameraPOV(0, (Camera::CameraPOV)7);
    } else {
        gUseUnifiedView = 1;
        Cameras::SetCameraMonster(2, owner);
        Cameras::SetCameraPOV(2, (Camera::CameraPOV)7);
        owner->m_unkF9 = 1;
    }
    *((char *)owner->m_cs + 0xC) = 1; /* cs->drawMe */
    ((MonsterSound *)owner->m_sound)->playVictorySound(anim);
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionInto__12StateVictory);
#endif
/* At 85% the victory counts as shown; at the end back to Idle (and the camera back to normal in modes 8/9). */
#ifdef NON_MATCHING
/* untuned: 14/69 words; tools/difftest.py 200/200 */
void StateVictory::update(void)
{
    float pct;
    int mode;

    MonsterState::update();
    ((MonsterDynamics *)((char *)owner + 0x100))->updateTurn(false);
    ((MonsterDynamics *)((char *)owner + 0x100))->updateMove(false);
    pct = animationGetCurrentPercent(owner->m_anims[anim]);
    if (pct >= 0.85f)
        owner->m_unkF7 = 1;
    if (pct >= 1.0f) {
        mode = game->m_gameMode;
        if (mode == 9 || (mode == 8 && BIGSHOT_E8 == 1))
            Cameras::SetCameraPOV(0, (Camera::CameraPOV)0);
        owner->enterNewState(STATE_AT(owner, ST_IDLE));
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", update__12StateVictory);
#endif
/* Fades the motion blur out by 4 per call; returns whether it is still visible. */
int StateVictory::fade(void)
{
    MotionBlurAA::setAlpha(alpha, owner->m_index);
    alpha = alpha - 4.0f;
    if (alpha > 0.0f) {
        MotionBlurAA::setEnable(true);
        return 1;
    }
    MotionBlurAA::setEnable(false);
    return 0;
}
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __15StateBigTakeHit);
int StateBigTakeHit::transitionOK(void)
{
    return owner->m_anims[0x4B].a != 0;
}
/* Retail quirk: the transition is started on a copy of the handle, not on the monster's own one. */
void StateBigTakeHit::transitionInto(void)
{
    _animHandle h;

    frames = 0;
    h = owner->m_anims[0x4B];
    animationTransitionInto(h, 5.0f, 1, 1);
}
/* Back to Idle at 99% of the animation. */
void StateBigTakeHit::update(void)
{
    MonsterState::update();
    if (animationGetCurrentPercent(owner->m_anims[0x4B]) >= 0.99f)
        owner->enterNewState(STATE_AT(owner, ST_IDLE));
    ((MonsterDynamics *)((char *)owner + 0x100))->updateTurn(false);
    ((MonsterDynamics *)((char *)owner + 0x100))->updateMove(false);
}
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", damageToPickup__20PunchSwipeConfigBase);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", damageCaused__20PunchSwipeConfigBase);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tf12MonsterState);
void init__12MonsterStateP7Monster(void *self, int v) __asm__("init__12MonsterStateP7Monster");
void init__12MonsterStateP7Monster(void *self, int v)
{
    *(int *)((char *)self + 0xC) = v;
}
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionInto__12MonsterState);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionOK__12MonsterState);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", transitionFeasible__12MonsterState);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", acceptHit__12MonsterStateR8HitEvent);
void handlePreemption__12MonsterStateP12MonsterState(void *self) __asm__("handlePreemption__12MonsterStateP12MonsterState");
void handlePreemption__12MonsterStateP12MonsterState(void *self)
{
}
int getFieldsInState__12MonsterState(void *self) __asm__("getFieldsInState__12MonsterState");
int getFieldsInState__12MonsterState(void *self)
{
    return *(int *)((char *)self + 0x8);
}
int getStateId__12MonsterState(void *self) __asm__("getStateId__12MonsterState");
int getStateId__12MonsterState(void *self)
{
    return *(int *)((char *)self + 0x0);
}
int getCaps__12MonsterState(void *self) __asm__("getCaps__12MonsterState");
int getCaps__12MonsterState(void *self)
{
    return *(int *)((char *)self + 0x4);
}
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", setCaps__12MonsterStateUs);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", clearCaps__12MonsterStateUs);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", hasCaps__12MonsterStateUi);
int getMon__12MonsterState(void *self) __asm__("getMon__12MonsterState");
int getMon__12MonsterState(void *self)
{
    return *(int *)((char *)self + 0xC);
}
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", func_0015A100);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", func_0015A170);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", func_0015A178);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$12StateVictory);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$15StateBigTakeHit);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$17StateShieldAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$16StateTazerAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$16StateStompAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$12StateShocked);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$19StateTwoHandedThrow);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$10StateThrow);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$10StateTaunt);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$12StateStunned);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$11StateRecoil);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$10StatePunch);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$Q210StatePunch11PunchConfig);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$11StatePickUp);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$12StateJavelin);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$12StateImpaled);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$14StateGunAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$16StateGetupAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$10StateDeath);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$14StateCountered);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$12StateCounter);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$10StateCatch);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$10StateBlock);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$13StateBatSwipe);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$Q213StateBatSwipe11SwipeConfig);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$9StateIdle);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$11AttackState);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", _vt$12MonsterState);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", D_006EBB68);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", D_006EBB78);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tf9StateIdle);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tfQ213StateBatSwipe11SwipeConfig);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tf13StateBatSwipe);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", damageToPickup__Q213StateBatSwipe11SwipeConfig);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", getAttackTrans__13StateBatSwipe);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", getRecvrTransMod__13StateBatSwipe);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", getRecoilFields__13StateBatSwipe);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", getHitPause__13StateBatSwipe);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", getSphereRadius__13StateBatSwipe);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", getDamageMod__13StateBatSwipe);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", getCurrentAnim__13StateBatSwipe);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", getCurrentConfig__13StateBatSwipe);
int getCurrentSwipe__13StateBatSwipe(void *self) __asm__("getCurrentSwipe__13StateBatSwipe");
int getCurrentSwipe__13StateBatSwipe(void *self)
{
    return *(int *)((char *)self + 0x2D4C);
}
int getFieldsInConfig__13StateBatSwipe(void *self) __asm__("getFieldsInConfig__13StateBatSwipe");
int getFieldsInConfig__13StateBatSwipe(void *self)
{
    return *(int *)((char *)self + 0x2DAC);
}
int getCounterStartField__13StateBatSwipe(void *self) __asm__("getCounterStartField__13StateBatSwipe");
int getCounterStartField__13StateBatSwipe(void *self)
{
    return *(int *)((char *)self + 0x2DB0);
}
int getCounterEndField__13StateBatSwipe(void *self) __asm__("getCounterEndField__13StateBatSwipe");
int getCounterEndField__13StateBatSwipe(void *self)
{
    return *(int *)((char *)self + 0x2DB4);
}
int getTier__13StateBatSwipe(void *self) __asm__("getTier__13StateBatSwipe");
int getTier__13StateBatSwipe(void *self)
{
    return *(int *)((char *)self + 0x2D48);
}
int isWindingDown__13StateBatSwipe(void *self) __asm__("isWindingDown__13StateBatSwipe");
int isWindingDown__13StateBatSwipe(void *self)
{
    return *(int *)((char *)self + 0x2D5C);
}
int inCollisionWindow__13StateBatSwipe(void *self) __asm__("inCollisionWindow__13StateBatSwipe");
int inCollisionWindow__13StateBatSwipe(void *self)
{
    return *(int *)((char *)self + 0x2DC0);
}
int inCounterableWindow__13StateBatSwipe(void *self) __asm__("inCounterableWindow__13StateBatSwipe");
int inCounterableWindow__13StateBatSwipe(void *self)
{
    return *(int *)((char *)self + 0x2DC4);
}
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", inWindUp__13StateBatSwipe);
int inWindDown__13StateBatSwipe(void *self) __asm__("inWindDown__13StateBatSwipe");
int inWindDown__13StateBatSwipe(void *self)
{
    return *(int *)((char *)self + 0x2D5C);
}
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", isStunHit__13StateBatSwipe);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", isKnockbackHit__13StateBatSwipe);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tf10StateBlock);
int isBlocking__10StateBlock(void *self) __asm__("isBlocking__10StateBlock");
int isBlocking__10StateBlock(void *self)
{
    return *(int *)((char *)self + 0x30);
}
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tf10StateCatch);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tf12StateCounter);
int getCounterStartField__12StateCounter(void *self) __asm__("getCounterStartField__12StateCounter");
int getCounterStartField__12StateCounter(void *self)
{
    return *(int *)((char *)self + 0x3C);
}
int getCounterEndField__12StateCounter(void *self) __asm__("getCounterEndField__12StateCounter");
int getCounterEndField__12StateCounter(void *self)
{
    return *(int *)((char *)self + 0x40);
}
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tf14StateCountered);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tf10StateDeath);
int getKiller__10StateDeath(void *self) __asm__("getKiller__10StateDeath");
int getKiller__10StateDeath(void *self)
{
    return *(int *)((char *)self + 0x1C);
}
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tf16StateGetupAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tf14StateGunAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tf12StateImpaled);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tf12StateJavelin);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tf11StatePickUp);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tfQ210StatePunch11PunchConfig);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tf10StatePunch);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", damageCaused__Q210StatePunch11PunchConfig);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", getAttackTrans__10StatePunch);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", getRecvrTransMod__10StatePunch);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", getRecoilFields__10StatePunch);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", getHitPause__10StatePunch);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", getSphereRadius__10StatePunch);
int getTier__10StatePunch(void *self) __asm__("getTier__10StatePunch");
int getTier__10StatePunch(void *self)
{
    return *(int *)((char *)self + 0x2988);
}
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", getCurrentAnim__10StatePunch);
int getCurrentPunch__10StatePunch(void *self) __asm__("getCurrentPunch__10StatePunch");
int getCurrentPunch__10StatePunch(void *self)
{
    return *(int *)((char *)self + 0x298C);
}
int getFieldsInConfig__10StatePunch(void *self) __asm__("getFieldsInConfig__10StatePunch");
int getFieldsInConfig__10StatePunch(void *self)
{
    return *(int *)((char *)self + 0x29EC);
}
int getCounterStartField__10StatePunch(void *self) __asm__("getCounterStartField__10StatePunch");
int getCounterStartField__10StatePunch(void *self)
{
    return *(int *)((char *)self + 0x29F0);
}
int getCounterEndField__10StatePunch(void *self) __asm__("getCounterEndField__10StatePunch");
int getCounterEndField__10StatePunch(void *self)
{
    return *(int *)((char *)self + 0x29F4);
}
void setPunchEffect__10StatePunch4FxId(void *self, int v) __asm__("setPunchEffect__10StatePunch4FxId");
void setPunchEffect__10StatePunch4FxId(void *self, int v)
{
    *(int *)((char *)self + 0x2960) = v;
}
int getPunchEffect__10StatePunch(void *self) __asm__("getPunchEffect__10StatePunch");
int getPunchEffect__10StatePunch(void *self)
{
    return *(int *)((char *)self + 0x2960);
}
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", isStunHit__10StatePunch);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", isKnockbackHit__10StatePunch);
int isWindingDown__10StatePunch(void *self) __asm__("isWindingDown__10StatePunch");
int isWindingDown__10StatePunch(void *self)
{
    return *(int *)((char *)self + 0x299C);
}
int inCollisionWindow__10StatePunch(void *self) __asm__("inCollisionWindow__10StatePunch");
int inCollisionWindow__10StatePunch(void *self)
{
    return *(int *)((char *)self + 0x2A00);
}
int inCounterableWindow__10StatePunch(void *self) __asm__("inCounterableWindow__10StatePunch");
int inCounterableWindow__10StatePunch(void *self)
{
    return *(int *)((char *)self + 0x2A04);
}
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", inWindUp__10StatePunch);
int inWindDown__10StatePunch(void *self) __asm__("inWindDown__10StatePunch");
int inWindDown__10StatePunch(void *self)
{
    return *(int *)((char *)self + 0x299C);
}
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", getCurrentConfig__10StatePunch);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tf11StateRecoil);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", setTransDirection__11StateRecoilR8_fvector);
void setTransDelta__11StateRecoilf(void *self, float v) __asm__("setTransDelta__11StateRecoilf");
void setTransDelta__11StateRecoilf(void *self, float v)
{
    *(float *)((char *)self + 0x4C) = v;
}
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", setHitDirection__11StateRecoilR8_fvector);
void setRecoilFields__11StateRecoilf(void *self, float v) __asm__("setRecoilFields__11StateRecoilf");
void setRecoilFields__11StateRecoilf(void *self, float v)
{
    *(float *)((char *)self + 0x1C) = v;
}
void setHitPause__11StateRecoilf(void *self, float v) __asm__("setHitPause__11StateRecoilf");
void setHitPause__11StateRecoilf(void *self, float v)
{
    *(float *)((char *)self + 0x28) = v;
}
void setHitSource__11StateRecoili(void *self, int v) __asm__("setHitSource__11StateRecoili");
void setHitSource__11StateRecoili(void *self, int v)
{
    *(int *)((char *)self + 0x30) = v;
}
void setHitType__11StateRecoili(void *self, int v) __asm__("setHitType__11StateRecoili");
void setHitType__11StateRecoili(void *self, int v)
{
    *(int *)((char *)self + 0x34) = v;
}
int isBlocking__11StateRecoil(void *self) __asm__("isBlocking__11StateRecoil");
int isBlocking__11StateRecoil(void *self)
{
    return *(int *)((char *)self + 0x64);
}
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", isStaggering__11StateRecoil);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", getBlendTime__11StateRecoil);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tf12StateStunned);
int getStarsCs__12StateStunned(void *self) __asm__("getStarsCs__12StateStunned");
int getStarsCs__12StateStunned(void *self)
{
    return *(int *)((char *)self + 0x30);
}
void setRecoilFirst__12StateStunnedb(void *self, int v) __asm__("setRecoilFirst__12StateStunnedb");
void setRecoilFirst__12StateStunnedb(void *self, int v)
{
    *(int *)((char *)self + 0x24) = v;
}
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tf10StateTaunt);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tf10StateThrow);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tf19StateTwoHandedThrow);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tf12StateShocked);
void setDamage__12StateShockedf(void *self, float v) __asm__("setDamage__12StateShockedf");
void setDamage__12StateShockedf(void *self, float v)
{
    *(float *)((char *)self + 0x40) = v;
}
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tf16StateStompAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tf16StateTazerAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", launchProjectile__16StateTazerAttackPv);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tf17StateShieldAttack);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tf15StateBigTakeHit);
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tf12StateVictory);
/* Invulnerable while celebrating. */
int StateVictory::acceptHit(HitEvent &e)
{
    return 0;
}
int getVictoryAnim__12StateVictory(void *self) __asm__("getVictoryAnim__12StateVictory");
int getVictoryAnim__12StateVictory(void *self)
{
    return *(int *)((char *)self + 0x14);
}
/* Callback form of fade. */
int StateVictory::fade(void *self)
{
    return ((StateVictory *)self)->fade();
}
INCLUDE_ASM("asm/nonmatchings/game/MonsterStates", __tf20PunchSwipeConfigBase);
