#ifndef MONSTER_STATE_H
#define MONSTER_STATE_H

class Monster;

/* Base of the monster state objects (StateRun, StateJump, StateRecoil ...), embedded in Monster at fixed offsets. Virtual in retail: the vptr sits at 0x10
   (vtable entries are {this delta, 0, function}); the ctor and vtable stay as asm and the derived classes below declare their methods without `virtual`. */
class MonsterState {
public:
    int id;             /* 0x00 */
    int flags;          /* 0x04: bit 0x10 = the state is a reaction that keeps the last attacker (see takeHit) */
    unsigned frames;    /* 0x08: frames since transitionInto (zeroed there, compared against timing fields in update) */
    Monster *owner;     /* 0x0C */
    void *vptr;         /* 0x10 */

    void update(void); /* base update: frame counter and common bookkeeping */
};

#endif
