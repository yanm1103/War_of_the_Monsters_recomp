#include "common.h"
#include "game/game.h"
#include "game/monster_state.h"
#include "game/pad_flags.h"
#include "game/pickup.h"

/* Movement states: a state's tunables are blocks inside the object ("configs"); jumping and flying pick the heavy block while holding a monster or a two-handed pickup. */
class JumpFlyBase : public MonsterState {
public:
};
class StateFly : public JumpFlyBase {
public:
    int transitionOK(void);
    char *getRelevantConfig(void);
    float getJumpHeightGain(void);
    float getFlapHeightGain(void);
    float getFlapStaminaDrain(void);
    int transitionFeasible(void);
};
class StateJump : public JumpFlyBase {
public:
    int transitionOK(void);
    char *getRelevantConfig(void);
    float getJumpHeightGain(void);
    int transitionFeasible(void);
};
class StateRamAttack : public MonsterState {
public:
    void setMaxSpeedMPH(float mph);
    void handlePreemption(MonsterState *next);
};
class StateDash : public MonsterState {
public:
    int transitionOK(void);
    void handlePreemption(MonsterState *next);
};
class StateClimb : public MonsterState {
public:
    int transitionOK(void);
    void handlePreemption(MonsterState *next);
};
class MonsterSound {
public:
    void terminateDashSound(void);
};
void particleKillFx(int &handle);
/* Virtual transitionFeasible(): vtable slot {delta @0x28, function @0x2C}. */
#define VFEASIBLE(st) (((int (*)(void *))*(void **)((char *)(st)->vptr + 0x2C))((char *)(st) + *(short *)((char *)(st)->vptr + 0x28)))
#define PAD(m, n) ((char *)(*(PadFlags *)((char *)(m) + 0x5040))[n])
#define SCFG(p, o) (*(float *)((char *)(p) + (o)))

INCLUDE_ASM("asm/nonmatchings/game/MovementStates", __13StateButtSlam);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", transitionOK__13StateButtSlam);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", transitionFeasible__13StateButtSlam);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", transitionInto__13StateButtSlam);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", update__13StateButtSlam);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", handleCollis__13StateButtSlamR9_hdResult);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", acceptHit__13StateButtSlamR8HitEvent);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", handlePreemption__13StateButtSlamP12MonsterState);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", __10StateClimb);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", init__10StateClimbP7Monster);
#ifdef NON_MATCHING
/* Climbing starts on a fresh press of the climb button (pad field 0x2E set now, clear one frame ago). */
int StateClimb::transitionOK(void)
{
    if (*(unsigned short *)(PAD(owner, 0) + 0x2E) != 0) {
        if (*(unsigned short *)(PAD(owner, 1) + 0x2E) != 0)
            return 0;
        return VFEASIBLE(this) != 0;
    }
    return 0;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", transitionOK__10StateClimb);
#endif
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", transitionFeasible__10StateClimb);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", transitionInto__10StateClimb);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", update__10StateClimb);
#ifdef NON_MATCHING
void StateClimb::handlePreemption(MonsterState *next)
{
    float *q = (float *)((char *)this + 0x90);

    *(float *)((char *)owner + 0x1B8) = 1.0f;
    *(unsigned char *)((char *)owner + 0x280) = 1; /* retail: sb (a word store would zero owner+0x281..0x283) */
    q[0] = 0.0f;
    q[1] = 0.0f;
    q[2] = 0.0f;
    q[3] = 1.0f;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", handlePreemption__10StateClimbP12MonsterState);
#endif
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", enterSubState__10StateClimbQ210StateClimb8SubState);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", checkWallContact__10StateClimb);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", getMinContact__10StateClimbP9_hdResultib);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", __9StateDash);
#ifdef NON_MATCHING
/* Dashing needs the dash ability (0x2D80), the dash button held, enough stamina (this+0x24 <= owner+0x470) and nothing heavy in hand. */
int StateDash::transitionOK(void)
{
    char *m = (char *)owner;

    if (*(int *)(m + 0x2D80) == 0)
        return 0;
    if (*(short *)(PAD(m, 0) + 0x3E) == 0)
        return 0;
    if (!(*(float *)((char *)this + 0x24) <= *(float *)(m + 0x470)))
        return 0;
    if (*(int *)(m + 0x68B4) != 0)
        return 0;
    if (*(int *)(m + 0x68A4) == 0 || ((**(Pickup ***)(m + 0x68A4))->bits >> 1 & 1) == 0)
        return 1;
    return 0;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", transitionOK__9StateDash);
#endif
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", transitionInto__9StateDash);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", update__9StateDash);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", enterSubState__9StateDashQ29StateDash8SubState);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", handleCollis__9StateDashR9_hdResult);
void StateDash::handlePreemption(MonsterState *next)
{
    ((MonsterSound *)owner->m_sound)->terminateDashSound();
    *(int *)((char *)owner + 0x484) = 1;
}
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", transitionInto__11JumpFlyBase);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", update__11JumpFlyBase);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", handleStateTransitions__11JumpFlyBase);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", __8StateFly);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", init__8StateFlyP7Monster);
#ifdef NON_MATCHING
/* Height reached by the jump launch speed (config +0xC) against gravity: v^2 / (2g), g being negative. */
float StateFly::getJumpHeightGain(void)
{
    float v = SCFG(getRelevantConfig(), 0xC);

    return -(v * v) / (2.0f * game->m_gravity);
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", getJumpHeightGain__8StateFly);
#endif
#ifdef NON_MATCHING
float StateFly::getFlapHeightGain(void)
{
    float v = SCFG(getRelevantConfig(), 0x14);

    return -(v * v) / (2.0f * game->m_gravity);
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", getFlapHeightGain__8StateFly);
#endif
float StateFly::getFlapStaminaDrain(void)
{
    return SCFG(getRelevantConfig(), 0x10);
}
#ifdef NON_MATCHING
char *StateFly::getRelevantConfig(void)
{
    char *m = (char *)owner;

    if (*(int *)(m + 0x68B4) == 0) {
        int heavy = 0;

        if (*(int *)(m + 0x68A4) != 0)
            heavy = (int)((**(Pickup ***)(m + 0x68A4))->bits >> 1) & 1;
        if (heavy)
            return (char *)this + 0x7C;
        return (char *)this + 0x44;
    }
    return (char *)this + 0x7C;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", getRelevantConfig__8StateFly);
#endif
#ifdef NON_MATCHING
/* Flying needs the monster able to fly (flags at 0x1ED0, 0x2400, 0x2410) and either no fly press / not feasible but enough stamina state (0x288 >= 6). */
int StateFly::transitionOK(void)
{
    char *m = (char *)owner;

    if (*(int *)(m + 0x1ED0) == 0 || *(int *)(m + 0x2400) == 0 || *(int *)(m + 0x2410) == 0)
        return 0;
    if (*(unsigned short *)(PAD(m, 0) + 0x2A) == 0 || VFEASIBLE(this) == 0)
        return *(int *)(m + 0x288) >= 6;
    return 1;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", transitionOK__8StateFly);
#endif
#ifdef NON_MATCHING
/* Jumping/flying is only possible while the pad entry one frame back has its field 0x2A at 0. */
int StateFly::transitionFeasible(void)
{
    return *(short *)((char *)(*(PadFlags *)((char *)owner + 0x5040))[1] + 0x2A) == 0;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", transitionFeasible__8StateFly);
#endif
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", transitionInto__8StateFly);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", update__8StateFly);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", enterSubState__8StateFlyQ211JumpFlyBase8SubState);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", handleCollis__8StateFlyR9_hdResult);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", handlePreemption__8StateFlyP12MonsterState);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", __9StateJump);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", init__9StateJumpP7Monster);
#ifdef NON_MATCHING
/* Height reached by the jump launch speed (config +0xC) against gravity: v^2 / (2g), g being negative. */
float StateJump::getJumpHeightGain(void)
{
    float v = SCFG(getRelevantConfig(), 0xC);

    return -(v * v) / (2.0f * game->m_gravity);
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", getJumpHeightGain__9StateJump);
#endif
#ifdef NON_MATCHING
char *StateJump::getRelevantConfig(void)
{
    char *m = (char *)owner;

    if (*(int *)(m + 0x68B4) == 0) {
        int heavy = 0;

        if (*(int *)(m + 0x68A4) != 0)
            heavy = (int)((**(Pickup ***)(m + 0x68A4))->bits >> 1) & 1;
        if (heavy)
            return (char *)this + 0x68;
        return (char *)this + 0x38;
    }
    return (char *)this + 0x68;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", getRelevantConfig__9StateJump);
#endif
#ifdef NON_MATCHING
int StateJump::transitionOK(void)
{
    char *m = (char *)owner;

    if (*(int *)(m + 0x14) == 0x1A0 && *(int *)(m + 0x68B4) != 0)
        return 0;
    if (*(int *)(m + 0x1ED0) == 0)
        return 0;
    if (*(unsigned short *)(PAD(m, 0) + 0x2A) == 0 || VFEASIBLE(this) == 0)
        return *(int *)(m + 0x288) >= 6;
    return 1;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", transitionOK__9StateJump);
#endif
#ifdef NON_MATCHING
/* Jumping/flying is only possible while the pad entry one frame back has its field 0x2A at 0. */
int StateJump::transitionFeasible(void)
{
    return *(short *)((char *)(*(PadFlags *)((char *)owner + 0x5040))[1] + 0x2A) == 0;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", transitionFeasible__9StateJump);
#endif
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", transitionInto__9StateJump);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", update__9StateJump);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", enterSubState__9StateJumpQ211JumpFlyBase8SubState);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", handleCollis__9StateJumpR9_hdResult);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", handlePreemption__9StateJumpP12MonsterState);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", __14StateRamAttack);
void StateRamAttack::setMaxSpeedMPH(float mph)
{
    SCFG(this, 0x30) = mph;
    owner->recomputeDynamics();
}
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", transitionOK__14StateRamAttack);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", transitionInto__14StateRamAttack);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", update__14StateRamAttack);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", handleCollis__14StateRamAttackR9_hdResult);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", enterSubState__14StateRamAttackQ214StateRamAttack8SubState);
/* Another state takes over: the ram's three effects stop. */
void StateRamAttack::handlePreemption(MonsterState *next)
{
    *(int *)((char *)owner + 0x484) = 1;
    particleKillFx(*(int *)((char *)this + 0xB0));
    particleKillFx(*(int *)((char *)this + 0xB4));
    particleKillFx(*(int *)((char *)this + 0xB8));
}
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", acceptHit__14StateRamAttackR8HitEvent);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", __8StateRun);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", init__8StateRunP7Monster);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", transitionOK__8StateRun);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", transitionInto__8StateRun);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", update__8StateRun);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", enterSubState__8StateRunQ28StateRun8SubState);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", handlePreemption__8StateRunP12MonsterState);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", getAnimSpeed__8StateRunf);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", func_001769B8);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", func_001769C0);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", func_001769C8);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", D_006ED4B0);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", D_006ED4F0);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", _vt$8StateRun);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", _vt$14StateRamAttack);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", _vt$9StateJump);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", _vt$8StateFly);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", _vt$11JumpFlyBase);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", _vt$9StateDash);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", _vt$10StateClimb);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", _vt$13StateButtSlam);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", __tf13StateButtSlam);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", __tf10StateClimb);
int getSubState__10StateClimb(void *self) __asm__("getSubState__10StateClimb");
int getSubState__10StateClimb(void *self)
{
    return *(int *)((char *)self + 0x7C);
}
void * getClimbMotion__10StateClimb(void *self) __asm__("getClimbMotion__10StateClimb");
void * getClimbMotion__10StateClimb(void *self)
{
    return (char *)self + 0x90;
}
void * getHuckVelocity__10StateClimb(void *self) __asm__("getHuckVelocity__10StateClimb");
void * getHuckVelocity__10StateClimb(void *self)
{
    return (char *)self + 0x60;
}
void * getContactNormal__10StateClimb(void *self) __asm__("getContactNormal__10StateClimb");
void * getContactNormal__10StateClimb(void *self)
{
    return (char *)self + 0xB0;
}
int getContactInteractive__10StateClimb(void *self) __asm__("getContactInteractive__10StateClimb");
int getContactInteractive__10StateClimb(void *self)
{
    return *(int *)((char *)self + 0xC0);
}
int getContactFlags__10StateClimb(void *self) __asm__("getContactFlags__10StateClimb");
int getContactFlags__10StateClimb(void *self)
{
    return *(int *)((char *)self + 0xA0);
}
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", __tf9StateDash);
unsigned char getSubState__9StateDash(void *self) __asm__("getSubState__9StateDash");
unsigned char getSubState__9StateDash(void *self)
{
    return *(unsigned char *)((char *)self + 0x3C);
}
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", __tf11JumpFlyBase);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", __11JumpFlyBaseQ212MonsterState2IdQ212MonsterState4Caps);
void setFootPref__11JumpFlyBaseQ211JumpFlyBase8FootPref(void *self, int v) __asm__("setFootPref__11JumpFlyBaseQ211JumpFlyBase8FootPref");
void setFootPref__11JumpFlyBaseQ211JumpFlyBase8FootPref(void *self, int v)
{
    *(int *)((char *)self + 0x1C) = v;
}
unsigned char getSubState__11JumpFlyBase(void *self) __asm__("getSubState__11JumpFlyBase");
unsigned char getSubState__11JumpFlyBase(void *self)
{
    return *(unsigned char *)((char *)self + 0x18);
}
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", getApexHeight__11JumpFlyBase);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", getHeightGain__11JumpFlyBase);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", D_006ED800);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", __tf8StateFly);
unsigned char getSubState__8StateFly(void *self) __asm__("getSubState__8StateFly");
unsigned char getSubState__8StateFly(void *self)
{
    return *(unsigned char *)((char *)self + 0x18);
}
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", __tf9StateJump);
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", __tf14StateRamAttack);
void setRamDuration__14StateRamAttackf(void *self, float v) __asm__("setRamDuration__14StateRamAttackf");
void setRamDuration__14StateRamAttackf(void *self, float v)
{
    *(float *)((char *)self + 0x38) = v;
}
void setAirDeccel__14StateRamAttackf(void *self, float v) __asm__("setAirDeccel__14StateRamAttackf");
void setAirDeccel__14StateRamAttackf(void *self, float v)
{
    *(float *)((char *)self + 0x2C) = v;
}
void setAcceleration__14StateRamAttackf(void *self, float v) __asm__("setAcceleration__14StateRamAttackf");
void setAcceleration__14StateRamAttackf(void *self, float v)
{
    *(float *)((char *)self + 0x24) = v;
}
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", __tf8StateRun);
void setLandMomentum__8StateRunf(void *self, float v) __asm__("setLandMomentum__8StateRunf");
void setLandMomentum__8StateRunf(void *self, float v)
{
    *(float *)((char *)self + 0xDC) = v;
}
int getAnimIndex__8StateRun(void *self) __asm__("getAnimIndex__8StateRun");
int getAnimIndex__8StateRun(void *self)
{
    return *(int *)((char *)self + 0x60);
}
INCLUDE_ASM("asm/nonmatchings/game/MovementStates", func_00176DA0);
