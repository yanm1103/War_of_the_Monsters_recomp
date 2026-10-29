#include "common.h"
#include "game/game.h"
#include "game/sound.h"
#include "game/hit_event.h"
#include "task_manager.h"
#include "vecmath.h"

/* Breakable level objects. Every DESTRUCTIBLE node (op 0x1F) of a level sits under an INTERACTIVE_STATE (op 0x27); the loader
 * hands both to Destructibles::AddDestruct, the state node first: it registers the next free slot in Interactives, the
 * DESTRUCTIBLE then fills that slot and takes it. A hit takes health, the health fraction picks the new state (at most one
 * state per hit), the state's actions run (debris, sounds...) and the drawing switches a couple of fields later (SwitchState
 * on the TaskManager). Format notes in docs/FORMATS.md ("Destructibles"). Virtual in retail (DbInteractive: vptr at 0x10,
 * takeHit in the vtable); the vtable, __tf and the inline DbInteractive methods emitted here stay asm. */

extern "C" float fabsf(float);

struct ActDebris;
struct ActHead {
    unsigned short type;    /* 0x0 */
    unsigned short pad2;
    float delay;            /* 0x4: seconds before the action runs */
};
struct ActTransform : ActHead {
    ActHead *act;           /* 0x8 */
    char padC[4];
    float mat[4][4];        /* 0x10 */
};
enum {
    ACT_TRANSFORM = 1,
    ACT_HATPICKUP = 10,
};

/* hier opcodes */
enum {
    HIER_GROUP = 1,
    HIER_SWITCH = 6,
    HIER_INTERACTIVE_STATE = 0x27,
};

void boundEulerAngle(float *p);
void mathfUnitMatrix(float (*m)[4]);
void mathfTransposeMatrix(float (*dst)[4], float (*src)[4]);
void sanFranSaveInteractive(int id1, int id2);
void threeMileStartVent(void);
void threeMileActivateStack(int id1);
void threeMileStackDestroyed(int id1);

class DbInteractive {
public:
    int m_kind;               /* 0x00: 1 monster, 3 destructible */
    unsigned short m_flags;   /* 0x04: AddDestruct sets bit 3 (cleared again for hitPointClass 1) */
    char pad6[6];
    _cs *m_cs;                /* 0x0C */
    void *m_vptr;             /* 0x10 */
};

class Interactives {
public:
    static int addInteractive(DbInteractive *p);
    static DbInteractive *getInteractive(int i);
};

class Destructible;
namespace ActionDispatch {
void addAction(ActHead *act, float (*m)[4], _fvector *pos, Destructible *d);
}

class Debris {
public:
    static void Activate(ActDebris *act, float (*m)[4], _fvector *pos, float (*mat)[4][4], float speed, _fvector *rot);
};

class Destructible : public DbInteractive {
public:
    float m_health;                   /* 0x14 */
    float m_maxHealth;                /* 0x18 */
    float m_minDamage;                /* 0x1C: a hit has to deal more than this */
    char pad20[4];
    HierDestructible *m_hier;         /* 0x24: the DESTRUCTIBLE node (states in child[]) */
    HierInteractiveState *m_istate;   /* 0x28: its parent; whichChild is the state being drawn */
    int m_id;                         /* 0x2C: slot in Interactives (also in m_istate->head.id2) */
    int m_hitType;                    /* 0x30: hitPointClass of the node */
    char pad34[0x40 - 0x34];
    FVECTOR m_pos;                    /* 0x40 */
    FMATRIX m_mat;                    /* 0x50: transposed placement matrix */
    int m_newState;                   /* 0x90: state asked for; SwitchState copies it to m_istate->whichChild */
    char pad94[0xA0 - 0x94];

    void takeHit(_fvector *dir, float dmg, int attackerId);
    void GenericTakeHit(_fvector *dir, float dmg, int attackerId, int unused);
    void ChangeState(int state, bool rebuild, bool reset);
    unsigned SwitchState(void);
    void DispatchAction(ActHead *act, float (*m)[4]);
    void doDebris(ActDebris *act, float (*m)[4]);
    void shedMonsters(void);
    static unsigned SwitchState(void *p);
    float getHealth(void);
    int CurrentState(void);
    int getHitType(void);
    int getId1(void);
};
typedef char _size_Destructible[sizeof(Destructible) == 0xA0 ? 1 : -1];

class Destructibles {
public:
    Destructible m_items[1500];       /* 0x0 */
    char pad3A980[0x3EFD4 - 0x3A980];
    int m_count;                      /* 0x3EFD4 */

    void Init(void);
    void AddDestruct(int id, _hierhead *h, _fvector *pos, float (*m)[4]);
    void Update(void);
    void rebuildCanyon2Pillars(void);
    void rebuildCapitolPillars(void);
};

extern HitEvent s_takeHitInfo __asm__("_8HitEvent$s_takeHitInfo");
#define HIT_ALIVE(e) (*(int *)((char *)&(e) + 0x30))   /* takeHit leaves 1 here while the target still has health */

/* the bss of this TU (local names lost in the ELF; the asm data files label it by address) */
extern int s_lastAttacker __asm__("D_006F8BE4");       /* interactive id of the last one that hurt a destructible */
extern int s_numPillars __asm__("D_006F8BE8");         /* one count shared by both pillar lists below */
extern int s_canyon2Pillars[4] __asm__("D_007059C0");  /* Destructibles slots of the canyon2 pillars (id1 0x2775..0x2777) */
extern int s_capitolPillars[8] __asm__("D_007059D0");  /* Destructibles slots of the capitol pillars (id1 0x27AE..0x27B3) */

#define MON_ROT(m) ((FVECTOR *)((char *)(m) + 0xA0))              /* monster rotation (euler, z = heading) */
/* the monster's climb state (embedded at 0xEFE0); only the fields read here */
class StateClimb {
public:
    char pad0[0xB0];
    _fvector m_shedDir;       /* 0xB0: push-off direction (away from the wall) */
    int m_climbingId;         /* 0xC0: interactive id of what it climbs */
};
#define MON_CLIMB(m) ((StateClimb *)((char *)(m) + 0xEFE0))
#define STATE_CLIMB 0x1F

void Destructibles::Init(void)
{
    m_count = 0;
    s_lastAttacker = 0;
    s_numPillars = 0;
}

/* The INTERACTIVE_STATE node only reserves the slot: the count goes up with the DESTRUCTIBLE that follows it. `id` is not
 * used. Health by hitPointClass: 0 from the node, 1 10 (no min damage, clears flag bit 3), 2..5 50/100/150/250, else 3000. */
#ifdef NON_MATCHING
/* 200/202 words, untuned: the three health stores at the end come out in another order (0x1C, 0x14, 0x18 in retail) */
void Destructibles::AddDestruct(int id, _hierhead *h, _fvector *pos, float (*m)[4])
{
    Destructible *d;
    HierDestructible *hd;
    float health;
    float minDamage;

    if (m_count >= 1500)
        return;
    d = &m_items[m_count];
    if (h->opcode == HIER_INTERACTIVE_STATE) {
        d->m_istate = (HierInteractiveState *)h;
        h->id2 = Interactives::addInteractive(d);
        d->m_id = h->id2;
        if (h->id1 == 0x275D || h->id1 == 0x275E || h->id1 == 0x275F || h->id1 == 0x2760 || h->id1 == 0x2761 ||
            h->id1 == 0x2762)
            sanFranSaveInteractive(h->id1, h->id2);
        if (h->id1 == 0x2775 || h->id1 == 0x2776 || h->id1 == 0x2777)
            s_canyon2Pillars[s_numPillars++] = m_count;
        if (h->id1 == 0x27AE || h->id1 == 0x27AF || h->id1 == 0x27B0 || h->id1 == 0x27B1 || h->id1 == 0x27B2 ||
            h->id1 == 0x27B3)
            s_capitolPillars[s_numPillars++] = m_count;
        return;
    }
    hd = (HierDestructible *)h;
    d->m_kind = 3;
    d->m_flags |= 8;
    d->m_hier = hd;
    d->m_newState = 0;
    d->m_pos.x = pos->x;
    d->m_pos.y = pos->y;
    d->m_pos.z = pos->z;
    mathfTransposeMatrix(d->m_mat, m);
    d->m_hitType = hd->hitPointClass;
    switch (hd->hitPointClass) {
    case 0:
        health = hd->hitPoints;
        minDamage = hd->hpThreshold;
        break;
    case 1:
        d->m_flags &= ~8;
        health = 10.0f;
        minDamage = 0.0f;
        break;
    case 2:
        health = 50.0f;
        minDamage = 5.0f;
        break;
    case 3:
        health = 100.0f;
        minDamage = 5.0f;
        break;
    case 4:
        health = 150.0f;
        minDamage = 5.0f;
        break;
    case 5:
        health = 250.0f;
        minDamage = 5.0f;
        break;
    default:
        health = 3000.0f;
        minDamage = 5.0f;
        break;
    }
    d->m_minDamage = minDamage;
    d->m_health = d->m_maxHealth = health;
    m_count++;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Destructible", AddDestruct__13DestructiblesiP9_hierheadP8_fvectorPA3_f);
#endif

void Destructibles::Update(void)
{
}

void Destructibles::rebuildCanyon2Pillars(void)
{
    int i;
    for (i = 0; i < 3; i++) {
        Destructible *d = &m_items[s_canyon2Pillars[i]];
        d->m_newState = 0;
        d->m_health = d->m_maxHealth;
        d->ChangeState(0, true, false);
    }
}

void Destructibles::rebuildCapitolPillars(void)
{
    int i;
    for (i = 0; i < 6; i++) {
        Destructible *d = &m_items[s_capitolPillars[i]];
        d->m_newState = 0;
        d->m_health = d->m_maxHealth;
        d->ChangeState(0, true, false);
    }
}

/* Retail quirks kept:
 * - ids 0x1A90..0x1B57 never lose health; retail still works out a direction (the attacker's facing, or the normalized hit
 *   direction) into locals and drops them (leftover of a removed call), and leaves HIT_ALIVE alone for them.
 * - Three Mile (level 6): the vent (0x2712) and the stacks (0x2715..0x2718) ignore monsters whose m_playerNum is not 1.
 * - San Francisco (level 7): 0x2762 only takes a hit of exactly 100.
 * - The vent/stack follow-ups run on any level and look at m_newState even when this hit did no damage.
 * - GenericTakeHit gets the new health as its (unused) damage argument. */
#ifdef NON_MATCHING
/* 151/251 words, untuned: retail takes &m->rot once into a register and compares id1 == 0x2762 with a shift, not a mask (branch offsets follow) */
void Destructible::takeHit(_fvector *dir, float dmg, int attackerId)
{
    HitEvent *info = &s_takeHitInfo;
    _hierhead head = m_hier->head;
    unsigned id1;

    if ((unsigned)(head.id1 - 0x1A90) < 200) {
        FVECTOR rot;
        FVECTOR d;
        if (attackerId != 0 && Interactives::getInteractive(attackerId)->m_kind == 1 && (unsigned)info->type < 3) {
            Monster *m = (Monster *)Interactives::getInteractive(attackerId);
            if (m->m_speed >= 0.0f) {
                rot = *MON_ROT(m);
                d = *(FVECTOR *)dir;
            } else {
                rot.x = -MON_ROT(m)->x;
                rot.y = MON_ROT(m)->y;
                rot.z = MON_ROT(m)->z + 3.1415927f;
                boundEulerAngle(&rot.x);
                boundEulerAngle(&rot.z);
                vecScale(&d, dir, -1.0f);
            }
        } else {
            float inv = 1.0f / eeSqrtf(vecDot(dir, dir));
            d.x = dir->x * inv;
            d.y = dir->y * inv;
            d.z = dir->z * inv;
        }
        return;
    }

    if (game->m_levelId == 6 &&
        (head.id1 == 0x2712 || head.id1 == 0x2715 || head.id1 == 0x2716 || head.id1 == 0x2717 || head.id1 == 0x2718) &&
        attackerId != 0 && Interactives::getInteractive(attackerId)->m_kind == 1 &&
        ((Monster *)Interactives::getInteractive(attackerId))->m_playerNum != 1)
        return;
    if (game->m_levelId == 7 && m_hier->head.id1 == 0x2762 && dmg != 100.0f)
        return;

    if (m_minDamage < dmg || (info->source == 5 && (unsigned)(info->subtype - 15) < 2)) {
        m_health = m_health - dmg;
        GenericTakeHit(dir, m_health, attackerId, -1);
    }

    id1 = m_hier->head.id1;
    switch (id1) {
    case 0x2712:
        if (m_minDamage < dmg && m_newState > 0)
            threeMileStartVent();
        break;
    case 0x2715:
    case 0x2716:
    case 0x2717:
    case 0x2718:
        if (m_newState > 0) {
            threeMileActivateStack(id1);
            gStackDestroyed[m_hier->head.id1 - 0x2715] = m_newState;
            if (m_newState == 1)
                ((LevelObjectSoundManager *)((char *)game + 0x121570))->m_threeMile.resetReactorSoundInfo(m_hier->head.id1 - 0x2715);
            if (m_newState >= 2)
                threeMileStackDestroyed(m_hier->head.id1);
        }
        break;
    }

    if (m_health > 0.0f)
        HIT_ALIVE(*info) = 1;
    else
        HIT_ALIVE(*info) = 0;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Destructible", takeHit__12DestructibleP8_fvectorfi);
#endif

/* q is the health left in 1/65535; the new state is the first one whose threshold is below q, but the search stops at the
 * last state and one past the state being drawn, so a hit moves at most one state. */
void Destructible::GenericTakeHit(_fvector *dir, float dmg, int attackerId, int unused)
{
    int i = 0;
    int done = 0;
    int health = (int)m_health;
    int maxHealth = (int)m_maxHealth;
    HierDestructible *hd = m_hier;

    s_lastAttacker = attackerId;
    while (!done) {
        if (health * 0xFFFF / maxHealth > hd->child[i].damageThreshold) {
            done = 1;
        } else {
            i++;
            if (i >= hd->numKids - 1)
                done = 1;
            if (i == (int)m_istate->whichChild + 1)
                done = 1;
        }
    }
    ChangeState(i, false, false);
}

/* Going up: run the state's actions now and switch the drawing 2 fields later (SwitchState); the last attacker gets 50
 * "Destructibles" tokens if it is a monster with m_playerNum 1. Going down only happens with `rebuild` and switches at once.
 * `reset` turns on the SWITCH at the top of the new state (whichChild = 1) and, if it still has health, puts the health at
 * that state's threshold minus 0.001. */
void Destructible::ChangeState(int state, bool rebuild, bool reset)
{
    int cur = m_istate->whichChild;

    if (cur < state) {
        HierDestrState *st = &m_hier->child[state];
        if (reset) {
            HierHead *c = st->child;
            if (c->opcode == HIER_GROUP) {
                HierHead *k = *(HierHead **)((char *)c + 0x20);   /* first kid of the group */
                if (k->opcode == HIER_SWITCH)
                    ((HierSwitch *)k)->whichChild = 1;
            } else if (c->opcode == HIER_SWITCH) {
                ((HierSwitch *)c)->whichChild = 1;
            }
        }
        if (m_newState < state) {
            float m[4][4];
            m_newState = state;
            TaskManager::global.add(SwitchState, this, 2);
            mathfUnitMatrix(m);
            DispatchAction(st->actions, m);
            shedMonsters();
            if (s_lastAttacker != 0 && Interactives::getInteractive(s_lastAttacker)->m_kind == 1) {
                Monster *mon = (Monster *)Interactives::getInteractive(s_lastAttacker);
                if (mon->m_playerNum == 1)
                    game->m_tokens[mon->m_index].credit("Destructibles", 50);
            }
        }
    } else if (state < cur && rebuild) {
        m_istate->whichChild = state;
    }

    if (reset && 0.0f < m_health)
        m_health = m_hier->child[state].damageThreshold * m_maxHealth / 65535.0f - 0.001f;
}

unsigned Destructible::SwitchState(void)
{
    m_istate->whichChild = m_newState;
    return 0;
}

/* Hat pickups that a destructible drops always wait 1/30 s, whatever the database says. */
void Destructible::DispatchAction(ActHead *act, float (*m)[4])
{
    if (act == 0)
        return;
    switch (act->type) {
    case ACT_TRANSFORM:
        DispatchAction(((ActTransform *)act)->act, ((ActTransform *)act)->mat);
        break;
    case ACT_HATPICKUP:
        act->delay = 1.0f / 30.0f;
    default:
        ActionDispatch::addAction(act, m, &m_pos, this);
        break;
    }
}

/* The debris flies along the facing of the monster that broke it (turned around when it moves backwards) at its speed;
 * with no monster (or a big hit, type >= 3) Debris gets speed 0 and no direction. */
#ifdef NON_MATCHING
/* 34/72 words, untuned: retail computes &mon->rot in the delay slot of the speed test, so the registers shift */
void Destructible::doDebris(ActDebris *act, float (*m)[4])
{
    FVECTOR rot;
    _fvector *dir;
    float speed;
    HitEvent *info = &s_takeHitInfo;

    if (s_lastAttacker != 0 && Interactives::getInteractive(s_lastAttacker)->m_kind == 1 && (unsigned)info->type < 3) {
        Monster *mon = (Monster *)Interactives::getInteractive(s_lastAttacker);
        FVECTOR *r;
        speed = mon->m_speed;
        r = MON_ROT(mon);
        if (0.0f <= speed) {
            rot = *r;
        } else {
            speed = fabsf(speed);
            rot.x = -r->x;
            rot.y = r->y;
            rot.z = r->z + 3.1415927f;
            boundEulerAngle(&rot.x);
            boundEulerAngle(&rot.z);
        }
        dir = &rot;
    } else {
        speed = 0.0f;
        dir = 0;
    }
    Debris::Activate(act, m, &m_pos, &m_mat, speed, dir);
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Destructible", doDebris__12DestructibleP9ActDebrisPA3_f);
#endif

/* Monsters climbing this object are pushed off. */
#ifdef NON_MATCHING
/* 19/63 words, untuned: retail reloads game at the top of every iteration; ours carries it from the loop test */
void Destructible::shedMonsters(void)
{
    int i;
    for (i = 0; i < game->m_numSlots; i++) {
        Monster *m = gameSlotBase(i);
        if (m->m_playerNum != 0 && *m->m_state == STATE_CLIMB) {
            StateClimb *climb = MON_CLIMB(m);
            if (climb->m_climbingId == m_id)
                m->knockBack(climb->m_shedDir, 250.0f, 350.0f);
        }
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Destructible", shedMonsters__12Destructible);
#endif
INCLUDE_ASM("asm/nonmatchings/game/Destructible", func_00131038);
INCLUDE_ASM("asm/nonmatchings/game/Destructible", func_00131048);
INCLUDE_ASM("asm/nonmatchings/game/Destructible", getVel__13DbInteractive);
INCLUDE_ASM("asm/nonmatchings/game/Destructible", _vt$12Destructible);
INCLUDE_ASM("asm/nonmatchings/game/Destructible", __tf12Destructible);

unsigned Destructible::SwitchState(void *p)
{
    return ((Destructible *)p)->SwitchState();
}

float Destructible::getHealth(void)
{
    return m_health;
}

int Destructible::CurrentState(void)
{
    return m_istate->whichChild;
}

int Destructible::getHitType(void)
{
    return m_hitType;
}

int Destructible::getId1(void)
{
    return m_hier->head.id1;
}
INCLUDE_ASM("asm/nonmatchings/game/Destructible", func_00131120);
