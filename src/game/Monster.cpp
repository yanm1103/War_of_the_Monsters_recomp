#include "common.h"
#include "game/game.h"
#include "game/fire_breath.h"
#include "game/power_ups.h"
#include "task_manager.h"
#include "game/level_pickups.h"
#include "game/enemy_info.h"
#include "vecmath.h"

class GamePad {
public:
    void clearInputs(void);
    void loadPadInputs(int i);
};
class HealthMeter {
public:
    void creditFull(void);
    void credit(float amount);
};
class Ai {
public:
    void updateInputs(void);
};
extern float cloaker __asm__("cloaker.2691");
struct Q16 {
    char b[16];
};
extern "C" float cosf(float);
extern float UpMatrix[4][4];
void *particleGetParticle(int h);
class AnimBlend {
public:
    void setPercent(float p);
    void rampOut(float t);
};
void animationTransitionInto(_animHandle &h, float t, int a, int b);
class StateDeath {
public:
    int transitionOK(void);
    void setKiller(Monster *m);
};
void mathfCopyMatrixNotAligned(float (*dst)[4], float (*src)[4]);
void mathfRPHFromMatrix(float (*m)[4], _fvector *out);
int viewInFOV(int view, _fvector *a, _fvector *b);
class AiPath;
class AiPathNet {
public:
    static AiPathNet monster;
    int isIn(_fvector &pos, AiPath &path);
    AiPath *getClosestPath(_fvector &pos, unsigned char n);
};
class Interactives {
public:
    static int *getInteractive(int i);
};
void mathfRotMatrixRPH(float (*m)[4], _fvector *v);
class MonsterDynamics {
public:
    void updateMove(bool b);
    void updateTurn(bool b);
};
extern int moviePlaying;
extern int movieAborted;
class DbInteractive;
class AnimPappy {
public:
    void update(DbInteractive &d);
};
class ActionDispatch {
public:
    static unsigned stopAllActiveActions(void) __asm__("stopAllActiveActions__14ActionDispatchv");
};
unsigned timerGetFieldCount(void);

extern float WaterLevel;
__asm__("#SNFIX_SMALL WaterLevel");
void setWaterLevel(float v)
{
    WaterLevel = v;
}
float getWaterLevel(void)
{
    return WaterLevel;
}
bool isUnderwater(float y)
{
    return y < WaterLevel;
}
void Monster::recomputeDynamics(void)
{
    m_climbSpeed = m_climbSpeedBase * 0.024444444f * 60.0f;
    m_climbStrafeSpeed = m_climbStrafeBase * 0.024444444f * 60.0f;
    m_fd74 = m_fd70 * 0.024444444f * 60.0f;
}
void Monster::playerUpdateInputs(void)
{
    ((GamePad *)m_gamePad)->loadPadInputs(*(int *)m_playerInfo);
}
#ifdef NON_MATCHING
/* 10/341 words, untuned: written from the m2c draft; the retail clamps with min.s */
void Monster::update(void)
{
    GamePad *gp = (GamePad *)m_gamePad;
    PadFlags *pf;
    HealthMeter *health = (HealthMeter *)((char *)this + 0x448);

    m_frameTime = timerGetFieldsLastFrame();
    if (m_dead) {
        gp->clearInputs();
        updateDeathSequence();
    } else {
        if (m_godMode) {
            m_specialWeapon = 1;
            health->creditFull();
            if (m_unkF5 == 0)
                m_stamina.creditFull();
        } else if (m_unkEA == 0) {
            if (m_unkEB != 0) {
                m_specialWeapon = 1;
                if (m_unkF5 == 0)
                    m_stamina.creditFull();
            } else if (m_state != m_stateRef) {
                m_stamina.update(m_frameTime);
            }
        } else {
            health->creditFull();
            m_stamina.update(m_frameTime);
        }
        if (m_playerNum == 1) {
            if (m_unkF9 != 0)
                playerUpdateInputs();
            else
                gp->clearInputs();
            LevelPickups::computeHighlight(*this);
        } else if (m_playerNum == 2) {
            gp->clearInputs();
            if (m_unkF9 != 0) {
                ((Ai *)m_ai)->updateInputs();
                LevelPickups::computeHighlight(*this);
            }
        }
        pf = &m_padFlags;
        pf->interpretInputs(*gp);
        if (m_pinMode != 0) {
            if ((*pf)[0]->f32 != 0 && (*pf)[1]->f32 == 0)
                m_pinToggle ^= 1;
            if (m_pinToggle != 0) {
                unsigned short *a = (unsigned short *)((char *)this + 0x6648);
                float v;

                v = a[0x1A / 2] * m_padScale;
                (*pf)[0]->f22 = (v < 255.0f) ? v : 255.0f;
                v = a[0x1C / 2] * m_padScale;
                (*pf)[0]->f24 = (v < 255.0f) ? v : 255.0f;
                v = a[0x1E / 2] * m_padScale;
                (*pf)[0]->f26 = (v < 255.0f) ? v : 255.0f;
                v = a[0x20 / 2] * m_padScale;
                (*pf)[0]->f28 = (v < 255.0f) ? v : 255.0f;
                (*pf)[0]->f1C = (*pf)[0]->f18;
                (*pf)[0]->f1A = (*pf)[0]->f16;
                (*pf)[0]->f18 = 0;
                (*pf)[0]->f16 = 0;
                (*pf)[0]->f1E = 0;
                (*pf)[0]->f20 = 0;
            }
        }
        updateOnFire();
        updateBeingShocked();
        updateAirLegOverride();
        if (m_cloaked != 0) {
            m_cs->cloakWeight = smoothEasyIn(m_cs->cloakWeight, cloaker, 0.03f, 0.001f);
            m_cloakTime -= timerGetFieldsLastFrame();
            if (m_cloakTime <= 0)
                setCloakOff();
        }
        if ((*pf)[0]->f5C != 0)
            Cameras::TogglePOV(m_cameraView);
    }
    updateReticle();
    {
        char *st = (char *)m_state;
        char *vt = *(char **)(st + 0x10);

        (*(void (**)(void *, void *))(vt + 0x1C))(st + *(short *)(vt + 0x18), st);
    }
    updateLookAt();
    updateBoostAndRage();
    updateBoundingSphere();
    updateAnimContacts(true);
    if (m_x6874 != 0) {
        if (m_stamina.exhausted == 0 || m_dead != 0) {
            m_x6874[0xC] = 0;
        } else {
            m_x6874[0xC] = 1;
            *(QwData *)(m_x6874 + 0x10) = *(QwData *)((char *)this + 0x3E60);
        }
    }
    if (m_okToGlow != 0) {
        updatePowerUpGlow();
        if (m_unk49 != 0 || m_dead != 0) {
            m_cs->colorQuad.fVec[3] = 1.0f;
        } else {
            m_cs->colorQuad.fVec[3] = (timerGetFieldCount() % 6 >= 3) ? 0.0f : 1.0f;
        }
    }
    ((MonsterSound *)m_sound)->updateMonsterSound();
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Monster", update__7Monster);
#endif
void Monster::startCinema(void)
{
    ((AnimBlend *)m_cinemaBlendA)->setPercent(0.5f);
    ((AnimBlend *)m_cinemaBlendB)->setPercent(0.5f);
    if (m_cinemaBlendCOn) {
        ((AnimBlend *)m_cinemaBlendC)->rampOut(0.0f);
        m_cinemaBlendCOn = 0;
    }
    ((float *)((char *)m_cs + 0x70))[0] = 1.0f;
    ((float *)((char *)m_cs + 0x70))[1] = 1.0f;
    ((float *)((char *)m_cs + 0x70))[2] = 1.0f;
    ((float *)((char *)m_cs + 0x70))[3] = 1.0f;
    enterNewState((MonsterState *)((char *)this + 0x7984));
    if (game->m_gameMode != 1)
        animationTransitionInto(m_anims[0x5A], 96.0f, 1, 1);
}
#ifdef NON_MATCHING
/* 4/90 words, untuned: written from the m2c draft */
void Monster::updateCinema(void)
{
    float f = 0.0f;
    float *p = *(float **)(m_animPappy + 0x28);

    if (p)
        f = *p;
    if (f != 0.0f) {
        QwData *cs = (QwData *)((char *)m_cs + 0x20);
        QwData *d = (QwData *)((char *)this + 0x50);

        ((AnimPappy *)m_animPappy)->update(*(DbInteractive *)this);
        d[0] = cs[0];
        d[1] = cs[1];
        d[2] = cs[2];
        d[3] = cs[3];
        d[4] = *(QwData *)((char *)m_cs + 0x10);
    } else {
        updateReticle();
    }
    updateBoundingSphere();
    updateAnimContacts(false);
    if (moviePlaying != 0) {
        int aborted = movieAborted;

        if ((inputGetInput(8, 0) != 0 || inputGetInput(8, 1) != 0) && aborted == 0 && game->f120454 == 0 && game->f120458 == 0) {
            movieAborted = 1;
            game->fadeOutAndIn(4);
            TaskManager::global.add(ActionDispatch::stopAllActiveActions, 30);
        }
    }
    ((MonsterSound *)m_sound)->updateMonsterCinemaSound();
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Monster", updateCinema__7Monster);
#endif
void Monster::endCinema(void)
{
    enterNewState((MonsterState *)((char *)this + 0x7984));
}
INCLUDE_ASM("asm/nonmatchings/game/Monster", updateBoundingSphere__7Monster);
INCLUDE_ASM("asm/nonmatchings/game/Monster", updateReticle__7Monster);
void Monster::setReticles(int view)
{
    int i;
    int n = game->m_numMonsters;

    for (i = 0; i < n; i++) {
        Monster *m = game->m_monsters[i];

        if (view == i) {
            if (m->okToDrawReticle())
                *((char *)m->m_reticleCS + 0xC) = 1;
            {
                char *d = *(char **)m->m_reticleCS;

                if (d)
                    d[8] = m->m_reticleState;
            }
            if (m->m_stickyReticleOn) {
                char *s = (char *)m->m_stickyReticleCS;

                if (s)
                    s[0xC] = 1;
            }
        } else if (!gUseUnifiedView) {
            int mode = game->m_gameMode;

            if (mode != 9) {
                if (mode != 8) {
                    char *s;

                    if (m->m_reticleState == 2) {
                        char *d;

                        if (m->okToDrawReticle())
                            *((char *)m->m_reticleCS + 0xC) = 1;
                        d = *(char **)m->m_reticleCS;
                        if (d)
                            d[8] = m->m_reticleState;
                    } else {
                        *((char *)m->m_reticleCS + 0xC) = 0;
                    }
                    s = (char *)m->m_stickyReticleCS;
                    if (s)
                        s[0xC] = 0;
                }
            }
        }
    }
}
INCLUDE_ASM("asm/nonmatchings/game/Monster", setTrans__7MonsterR8_fvector);
void Monster::setRot(float a, float b, float c)
{
    char *src;
    float *rot;

    char *cs = (char *)m_cs;

    *(float *)(cs + 0x60) = b;
    rot = (float *)(cs + 0x60);
    rot[1] = c;
    rot[2] = a;
    mathfRotMatrixRPH((float (*)[4])((char *)m_cs + 0x20), (_fvector *)((char *)m_cs + 0x60));
    src = (char *)m_cs + 0x20;
    __asm__ volatile("lq $8, 0x0(%0)
	"
                     "lq $9, 0x10(%0)
	"
                     "lq $10, 0x20(%0)
	"
                     "lq $11, 0x30(%0)
	"
                     "sq $8, 0x50(%1)
	"
                     "sq $9, 0x60(%1)
	"
                     "sq $10, 0x70(%1)
	"
                     "sq $11, 0x80(%1)"
                     : : "r"(src), "r"(this) : "$8", "$9", "$10", "$11", "memory");
}
void Monster::setMat(float (&m)[4][4])
{
    mathfCopyMatrixNotAligned((float (*)[4])((char *)m_cs + 0x20), (float (*)[4])m);
    mathfCopyMatrixNotAligned((float (*)[4])((char *)this + 0x50), (float (*)[4])m);
    mathfRPHFromMatrix((float (*)[4])m, (_fvector *)((char *)m_cs + 0x60));
}
void Monster::setEnvMapping(void)
{
    m_cs->cloakMe = 1;
    m_cs->cloakWeight = -16.0f;
    m_flags &= 0xFFFD;
}
void Monster::clearEnvMapping(void)
{
    m_cs->cloakMe = 0;
    m_cs->cloakWeight = 16.0f;
    m_flags |= 2;
}
INCLUDE_ASM("asm/nonmatchings/game/Monster", updateShadow__7Monster);
signed char calcGlowIntensity(int a, int b, int c)
{
    if (c * 3 < b * 4) {
        int t = c - b;
        int u = t * 127;

        return a * u / c;
    }
    {
        int u = b * 42;

        return a * u / c;
    }
}
INCLUDE_ASM("asm/nonmatchings/game/Monster", updatePowerUpGlow__7Monster);
INCLUDE_ASM("asm/nonmatchings/game/Monster", updateBoostAndRage__7Monster);
INCLUDE_ASM("asm/nonmatchings/game/Monster", updateLock__7Monster19MonsterReticleState);
INCLUDE_ASM("asm/nonmatchings/game/Monster", updateLookAt__7Monster);
INCLUDE_ASM("asm/nonmatchings/game/Monster", updateAirLegOverride__7Monster);
INCLUDE_ASM("asm/nonmatchings/game/Monster", updateAnimContacts__7Monsterb);
float Monster::getStaminaGain(void)
{
    int type;

    if (!m_pickup)
        return 0.0f;
    type = *(int *)(*(char **)m_pickup + 0xA0);
    return m_puStaminaGainMod[type] * LevelPickups::s_info[type].staminaGain;
}
INCLUDE_ASM("asm/nonmatchings/game/Monster", getTarget__7MonsterR8_fvectorb);
void Monster::throwPickup(int a, float b)
{
    _fvector dir;
    DbInteractive *target = getTarget(dir, false);
    int *d;

    LevelPickups::throwPickup(*(PickupIter *)&m_pickup, dir, (DbInteractive *)this, target);
    d = (int *)((char *)this + 0x100);

    if (d[12] == 1)
        d[12] = 0;
    m_pickup = 0;
}
INCLUDE_ASM("asm/nonmatchings/game/Monster", dropPickup__7Monster);
INCLUDE_ASM("asm/nonmatchings/game/Monster", launchDefaultProjectile__7Monsteri);
INCLUDE_ASM("asm/nonmatchings/game/Monster", updateDefaultProjectile__7Monster);
INCLUDE_ASM("asm/nonmatchings/game/Monster", updatePosition__7Monster);
#ifdef NON_MATCHING
class HitEvent;
class StateRecoil {
public:
    int transitionOK(void);
};
class StateStunned {
public:
    int transitionOK(void);
};
class StateShocked {
public:
    int transitionOK(void);
};
class StateBlock {
public:
    int isBlockable(HitEvent &e);
};
class StateGrappled {
public:
    enum SubState { SUB_0, SUB_1, SUB_2 };
    void enterSubState(SubState s);
};
class LevelObjectSoundManager {
public:
    void playDestructibleSound(unsigned id, _fvector *pos);
};
struct ActuatorData {
    int w[8];
};
void inputSetActuator(int pad, ActuatorData *a);
int particleCreateFx(_fvector *pos, int id, float scale, float (*m)[4], float strength);
extern "C" float sqrtf(float);
extern char takeHitInfo[] __asm__("_8HitEvent$s_takeHitInfo");
extern int attackState;
extern int attackSuccessful;
extern int craterSwitched;

#define HI(o) (*(int *)(takeHitInfo + (o)))
#define HF(o) (*(float *)(takeHitInfo + (o)))
#define SI(p, o) (*(int *)((char *)(p) + (o)))
#define SF(p, o) (*(float *)((char *)(p) + (o)))
#define COPY_DIR() do { SF(self, 0x7E70) = d[0]; SF(self, 0x7E78) = d[2]; SF(self, 0x7E74) = d[1]; } while (0)
#define COPY_QUAD() do { SF(self, 0x7E80) = d[0]; SF(self, 0x7E84) = d[1]; SF(self, 0x7E88) = d[2]; SF(self, 0x7E8C) = d[3]; } while (0)
#define RUMBLE(off) do { ActuatorData a = *(ActuatorData *)((char *)game + (off)); inputSetActuator(SI(self, 0x24), &a); } while (0)
#define KNOCK(f12, f13) knockBack(*dir, (f12), (f13))

/* A hit lands on this monster. `dir` is the hit direction (normalised here, its w becomes the length), `dmg` the base damage and `attackerId` the
   interactive id of whoever hit (0 = none). The static HitEvent (takeHitInfo) describes the hit: +0xC the type (0 none, 1 normal -> recoil,
   2 blockable, 3 big hit -> recoil/additive recoil by +0x14 subtype, 5 shock, 7 stun, 8 knockback only, 10 burn), +0x10 a source kind (0xB = melee, 1 = ...),
   +0x24/+0x34/+0x38/+0x3C strengths copied into the recoil state. The monster's current state may refuse the hit (virtual at vtable+0x34); blocking
   absorbs or scales the damage; the damage finally goes through takeDamage, the attacker gains stamina on melee hits and the player's pad rumbles. */
void Monster::takeHit(_fvector *dir, float dmg, int attackerId)
{
    char *self = (char *)this;
    float damage = dmg;
    char *src;
    float *d = (float *)dir;
    unsigned type;
    int reacted = 0;

    if (*(signed char *)(self + 0x49) != 0) {
        char *st = *(char **)(self + 0x34);
        char *vt = *(char **)(st + 0x10);

        reacted = ((int (*)(void *, void *))*(void **)(vt + 0x34))(st + *(short *)(vt + 0x30), takeHitInfo);
    }
    if (!reacted) {
        char *st = *(char **)(self + 0x34);

        if (st == *(char **)(self + 0x7980)) {
            char *vt = *(char **)(st + 0x10);

            if (((int (*)(void *))*(void **)(vt + 0x4C))(st + *(short *)(vt + 0x48)) != 0) {
                if (attackerId != 0 && *Interactives::getInteractive(attackerId) == 1)
                    takeDamage(damage, false, (Monster *)Interactives::getInteractive(attackerId));
                else
                    takeDamage(damage, false, 0);
            }
        }
        return;
    }
    if (SI(self, 0x14) == 0x1A0) {
        if (attackState == 4 || attackState == 0xA) {
            if (attackerId != 0 && *Interactives::getInteractive(attackerId) == 1)
                takeDamage(damage, false, (Monster *)Interactives::getInteractive(attackerId));
            else
                takeDamage(damage, false, 0);
            return;
        }
        if (attackerId != SI(*(char **)((char *)game + 0x120380), 0x20))
            return;
    }
    if (*(int *)((char *)game + 0x1203C8) == 7 && HI(0xC) != 8)
        return;
    if (isBlocking() && HI(0xC) == 7) {
        if (SI(self, 0x68A4) != 0 || SI(self, 0x68B4) != 0)
            return;
    }
    src = 0;
    if (attackerId != 0 && *Interactives::getInteractive(attackerId) == 1)
        src = (char *)Interactives::getInteractive(attackerId);
    {
        float x = d[0], y = d[1], z = d[2];
        float len = eeSqrtf(x * x + y * y + z * z);
        float inv = 1.0f / len;

        d[3] = len;
        d[2] = z * inv;
        d[0] = x * inv;
        d[1] = y * inv;
    }
    if (src != 0) {
        if (*(signed char *)(src + 0xF4) != 0) {
            float f = SF(src, 0x48C);

            if (0.0f < f)
                damage *= f;
        }
        if (*(signed char *)(src + 0xF5) != 0) {
            float f = SF(src, 0x49C);

            if (0.0f < f)
                damage *= f;
        }
    }
    if (*(signed char *)(self + 0xE8) != 0)
        return;
    if (isBlocking() && HI(0xC) != 7 && ((StateBlock *)(self + 0x7DA0))->isBlockable(*(HitEvent *)takeHitInfo)) {
        int holding = SI(self, 0x68A4) != 0 || SI(self, 0x68B4) != 0;

        if (holding) {
            if (SI(self, 0x68A4) != 0) {
                char *p = **(char ***)(self + 0x68A4);
                char *vt = *(char **)(p + 0x10);

                ((void (*)(void *, float, _fvector *, int))*(void **)(vt + 0xC))(p + *(short *)(vt + 8), damage, dir, SI(self, 0x20));
                if (SF(**(char ***)(self + 0x68A4), 0x48) <= 0.0f) {
                    LevelPickups::killPickup(*(PickupIter *)(self + 0x68A4), 1);
                    SI(self, 0x68A4) = 0;
                    particleCreateFx((_fvector *)(self + 0x3E40), 0xB, 5.0f, 0, 0.0f);
                    ((LevelObjectSoundManager *)((char *)game + 0x121570))->playDestructibleSound(0x64, (_fvector *)(self + 0x3E40));
                    if (((StateRecoil *)(self + 0x7E30))->transitionOK()) {
                        COPY_DIR();
                        SF(self + 0x7E30, 0x4C) = 0.0f;
                        SI(self + 0x7E30, 0x30) = 0;
                        COPY_QUAD();
                        enterNewState((MonsterState *)(self + 0x7E30));
                    }
                }
            } else {
                ((Monster *)SI(self, 0x68B4))->takeDamage(damage, false, (Monster *)src);
            }
            damage = 0.0f;
        } else {
            if (craterSwitched != 0)
                SF(self, 0x69A8) = 1.0f;
            damage *= SF(self, 0x69A8);
        }
    }
    if (src != 0 && *(signed char *)(src + 0xEC) != 0)
        damage = 1000.0f;
    type = HI(0xC);
    if (type > 10)
        goto dmg;
    switch (type) {
    case 2:
        if (isBlocking() && ((StateBlock *)(self + 0x7DA0))->isBlockable(*(HitEvent *)takeHitInfo)) {
            char *sr = self + 0x7E30;

            if (((StateRecoil *)sr)->transitionOK()) {
                if (src != 0 && **(int **)(src + 0x34) == 3) {
                    char *cs = *(char **)(src + 0xC);
                    char *a1 = src + 0x8470;
                    int ia, ib;

                    SF(self, 0x7E70) = SF(cs, 0x30);
                    SF(self, 0x7E78) = SF(cs, 0x38);
                    SF(self, 0x7E74) = SF(cs, 0x34);
                    ia = SI(a1, 0x2988) * 0x420;
                    ib = SI(a1, 0x298C) * 0xB0;
                    SF(sr, 0x4C) = SF(a1, ia + 0x20 + ib + 0x44) * SF(a1, ia + ib + 0x70);
                    SF(sr, 0x28) = SF(a1, ib + ia + 0x90);
                    SI(sr, 0x30) = HI(0x24);
                    SF(sr, 0x1C) = SF(a1, ib + ia + 0x8C);
                } else {
                    COPY_DIR();
                }
                COPY_QUAD();
                SI(sr, 0x34) = HI(0xC);
                enterNewState((MonsterState *)sr);
            }
            goto dmg;
        }
        if (HI(0x10) == 0xB) {
            if (HI(0x28) != 0x10)
                KNOCK(0.0f, d[3] * 50.0f);
            else
                KNOCK(HF(0x38), HF(0x34));
            SI(self + 0x7EA0, 0x524) = (int)src;
        } else if (src != 0) {
            char *st2 = *(char **)(src + 0x34);
            int sid = *(int *)st2;

            if (sid == 3) {
                int ia = SI(st2, 0x2988) * 0x420;
                int ib = SI(st2, 0x298C) * 0xB0;

                SF(self + 0x7EA0, 0x53C) = SF(st2, ib + ia + 0x90);
                knockBack(*(_fvector *)(*(char **)(src + 0xC) + 0x30), HF(0x38), HF(0x34));
                SI(self + 0x7EA0, 0x524) = (int)src;
            } else if (sid == 0x23) {
                KNOCK(SF(src, 0xF738), SF(src, 0xF73C));
                SI(self + 0x7EA0, 0x524) = (int)src;
            } else if (sid == 0x28) {
                KNOCK(SF(src, 0xFD8C), SF(src, 0xFD90));
                SI(self + 0x7EA0, 0x524) = (int)src;
            } else {
                if (SI(src, 0x14) != 0x100 || HI(0x10) != 5)
                    KNOCK(HF(0x38), HF(0x34));
                else
                    KNOCK(SF(src, 0x10964), SF(src, 0x10968));
                SI(self + 0x7EA0, 0x524) = (int)src;
            }
        } else {
            KNOCK(HF(0x38), HF(0x34));
        }
        if (SI(self, 0x18) == 1)
            RUMBLE(0x1227E0);
        goto dmg;
    case 5:
        if (SI(self, 0x14) == 0x1A0)
            goto additive;
        {
            char *sh = self + 0x10BA0;

            if (((StateShocked *)sh)->transitionOK()) {
                if (*(char **)(self + 0x34) != sh) {
                    SF(self, 0x10BC0) = d[0];
                    SF(self, 0x10BC4) = d[1];
                    SF(self, 0x10BC8) = d[2];
                    SF(self, 0x10BCC) = d[3];
                }
                enterNewState((MonsterState *)sh);
                if (HI(0x14) == 0x13 && src != 0) {
                    SI(self, 0x10BB8) = 1;
                    SF(self, 0x10BD0) = SF(src, 0x105D8);
                    SF(self, 0x10BD4) = SF(src, 0x105DC);
                }
            }
        }
        if (SI(self, 0x18) == 1)
            RUMBLE(0x1227C0);
        goto dmg;
    case 1:
        if (SI(self, 0x14) == 0x1A0 || isHoldingLarge())
            goto additive;
        {
            char *sr = self + 0x7E30;

            if (((StateRecoil *)sr)->transitionOK()) {
                if (src != 0) {
                    int sid = **(int **)(src + 0x34);

                    if (sid == 3) {
                        char *cs = *(char **)(src + 0xC);
                        char *a1 = src + 0x8470;
                        int ia, ib;

                        SF(self, 0x7E70) = SF(cs, 0x30);
                        SF(self, 0x7E78) = SF(cs, 0x38);
                        SF(self, 0x7E74) = SF(cs, 0x34);
                        ia = SI(a1, 0x2988) * 0x420;
                        ib = SI(a1, 0x298C) * 0xB0;
                        SF(sr, 0x4C) = SF(a1, ia + 0x20 + ib + 0x44) * SF(a1, ia + ib + 0x70);
                        SF(sr, 0x28) = SF(a1, ib + ia + 0x90);
                        SI(sr, 0x30) = HI(0x24);
                        SF(sr, 0x1C) = SF(a1, ib + ia + 0x8C);
                        if (SI(src, 0x68A4) != 0) {
                            char *info = (char *)&LevelPickups::s_info[SI(**(char ***)(src + 0x68A4), 0xA0)];

                            SF(sr, 0x1C) = SF(info, 0x1C);
                        }
                    } else if (sid == 0x29) {
                        char *cs = *(char **)(src + 0xC);
                        char *a1 = src + 0xAE90;
                        int ia, ib;

                        SF(self, 0x7E70) = SF(cs, 0x30);
                        SF(self, 0x7E78) = SF(cs, 0x38);
                        SF(self, 0x7E74) = SF(cs, 0x34);
                        ia = SI(a1, 0x2D48) * 0x480;
                        ib = SI(a1, 0x2D4C) * 0xC0;
                        SF(sr, 0x4C) = SF(a1, ia + 0x20 + ib + 0x44) * SF(a1, ia + ib + 0x70);
                        SI(sr, 0x30) = HI(0x24);
                        SF(sr, 0x1C) = SF(a1, ib + ia + 0x8C);
                    } else {
                        COPY_DIR();
                        SF(sr, 0x1C) = HF(0x3C);
                    }
                } else {
                    COPY_DIR();
                    SF(sr, 0x1C) = HF(0x3C);
                }
                COPY_QUAD();
                enterNewState((MonsterState *)sr);
            } else if (**(int **)(self + 0x34) == 0x12) {
                ((StateGrappled *)(self + 0xDDCC))->enterSubState(StateGrappled::SUB_2);
            }
        }
        if (SI(self, 0x18) == 1)
            RUMBLE(0x1227C0);
        goto dmg;
    case 3:
        if (SI(self, 0x14) == 0x1A0 || isHoldingLarge())
            goto additive;
        if (HI(0x14) == 0x17) {
            char *sr = self + 0x7E30;

            if (((StateRecoil *)sr)->transitionOK()) {
                COPY_DIR();
                SI(sr, 0x4C) = 0;
                SI(sr, 0x30) = 0;
                COPY_QUAD();
                enterNewState((MonsterState *)sr);
            } else if (**(int **)(self + 0x34) == 0x12) {
                ((StateGrappled *)(self + 0xDDCC))->enterSubState(StateGrappled::SUB_2);
            }
        } else {
            switch (HI(0x14)) {
            case 28:
            case 30:
            case 32:
            case 36:
            case 38:
                break;
            default:
                takeAdditiveRecoil(*dir, 1.0f);
                break;
            }
        }
        if (SI(self, 0x18) == 1) {
            if (HI(0x10) == 1)
                RUMBLE(0x1227A0);
            else
                RUMBLE(0x1227E0);
        }
        goto dmg;
    case 7:
        if (SI(self, 0x14) == 0x1A0)
            goto additive;
        if ((SI(*(char **)(self + 0x34), 0x4) & 0x10) == 0) {
            if (**(int **)(self + 0x34) == 0x2E) {
                _fvector up;

                up.x = 0.0f;
                up.y = 0.0f;
                up.w = 0.0f;
                up.z = -1.0f;
                knockBack(up, 0.0f, 0.0f);
                if (src != 0)
                    SI(self + 0x7EA0, 0x524) = (int)src;
            } else {
                char *sn = self + 0x10714;

                if (((StateStunned *)sn)->transitionOK()) {
                    COPY_DIR();
                    SI(self + 0x7E30, 0x30) = 0;
                    SI(self + 0x7E30, 0x4C) = 0;
                    COPY_QUAD();
                    SI(sn, 0x24) = 1;
                    enterNewState((MonsterState *)sn);
                } else if (**(int **)(self + 0x34) == 0x12) {
                    ((StateGrappled *)(self + 0xDDCC))->enterSubState(StateGrappled::SUB_1);
                }
            }
        }
        if (SI(self, 0x18) == 1)
            RUMBLE(0x1227A0);
        goto dmg;
    case 8:
        KNOCK(HF(0x38), HF(0x34));
        goto dmg;
    case 10:
        if (SI(self, 0x14) == 0x1A0)
            goto additive;
        SF(self + 0xDE30, 0x48) = 40.0f;
        enterNewState((MonsterState *)(self + 0xDE30));
        goto dmg;
    case 0:
        if (src == 0 && (SI(*(char **)(self + 0x34), 0x4) & 0x10) != 0)
            src = *(char **)(*(char **)(self + 0x34) + 0x524);
        goto dmg;
    default:
        goto dmg;
    }
additive:
    takeAdditiveRecoil(*dir, 1.0f);
dmg:
    takeDamage(damage, false, (Monster *)src);
    if (src != 0 && HI(0x10) == 0xB)
        ((Monster *)src)->m_stamina.credit(((Monster *)src)->getStaminaGain());
    if (SI(self, 0x6CD4) != 0)
        SI((char *)game + SI(self, 0x6CD8) * 0x2E0, 0xBC) = 0x1E;
    if (src != 0 && SI(src, 0x14) == 0x1A0)
        attackSuccessful = 1;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Monster", takeHit__7MonsterP8_fvectorfi);
#endif
INCLUDE_ASM("asm/nonmatchings/game/Monster", takeDamage__7MonsterfbP7Monster);
INCLUDE_ASM("asm/nonmatchings/game/Monster", takeAdditiveRecoil__7MonsterR8_fvectorf);
INCLUDE_ASM("asm/nonmatchings/game/Monster", knockBack__7MonsterR8_fvectorff);
void Monster::blowUpMonster(Monster *killer)
{
    if (m_dead)
        return;
    ((StateDeath *)((char *)this + 0x8450))->setKiller(killer);
    updateDeathSequence();
    if (m_cameraFollows) {
        gameHud(m_cameraView)->f0D0 = 0;
        animationStart(gameHud(m_cameraView)->h1D0, true);
    }
}
void Monster::updateDeathSequence(void)
{
    if (*m_state != 0x21) {
        StateDeath *death = (StateDeath *)((char *)this + 0x8450);

        if (death->transitionOK())
            enterNewState((MonsterState *)death);
    }
}
void Monster::registerComboHit(Monster *m)
{
    if (m_playerNum == 1 && m_cameraFollows && !m->isBlocking())
        gameHud(m_cameraView)->registerComboHit();
}
#ifdef NON_MATCHING
/* 31/47 words, untuned: loop pointer hoisting */
Monster *Monster::getClosestMonster(float maxDist)
{
    Monster *best = 0;
    int j;
    int n = game->m_numSlots;
    Monster *m = &game->m_slots[0];

    for (j = 0; j < n; j++, m++) {
        if (m->m_playerNum != 0 && m != this && m != m_target) {
            EnemyInfo::Info *e = EnemyInfo::info(m_monsterNum, j);

            if (e->dist < maxDist) {
                maxDist = e->dist;
                best = m;
            }
        }
    }
    return best;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Monster", getClosestMonster__7Monsterf);
#endif
#ifdef NON_MATCHING
/* 31/47 words, untuned: loop pointer hoisting */
Monster *Monster::getClosestMonster2D(float maxDist)
{
    Monster *best = 0;
    int j;
    int n = game->m_numSlots;
    Monster *m = &game->m_slots[0];

    for (j = 0; j < n; j++, m++) {
        if (m->m_playerNum != 0 && m != this && m != m_target) {
            EnemyInfo::Info *e = EnemyInfo::info(m_monsterNum, j);

            if (e->dist2D < maxDist) {
                maxDist = e->dist2D;
                best = m;
            }
        }
    }
    return best;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Monster", getClosestMonster2D__7Monsterf);
#endif
#ifdef NON_MATCHING
/* 15/48 words, untuned: loop setup order */
Monster *Monster::getClosestMonster(int locA, int locB, float radius)
{
    _fvector *pa = (_fvector *)((char *)this + locA * 16 - 0x4260);
    Monster *best = 0;
    float bestSq = radius * radius;
    int n = game->m_numSlots;
    Monster *m = &game->m_slots[0];
    _fvector *pb = (_fvector *)((char *)game + (locB * 16 - 0x36E0));
    _fvector d;

    if (n) {
        do {
            if (m->m_playerNum != 0 && m != this && m != m_target) {
                float sq;

                vecSub(&d, pa, pb);
                sq = vecLenSq(&d);
                if (sq < bestSq) {
                    bestSq = sq;
                    best = m;
                }
            }
            pb = (_fvector *)((char *)pb + sizeof(Monster));
            m++;
        } while (--n);
    }
    return best;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Monster", getClosestMonster__7Monsteriif);
#endif
#ifdef NON_MATCHING
/* 34/50 words, untuned: loop pointer hoisting */
Monster *Monster::getClosestMonsterWithLos(float maxDist)
{
    Monster *best = 0;
    int j;
    int n = game->m_numSlots;
    Monster *m = &game->m_slots[0];

    for (j = 0; j < n; j++, m++) {
        if (m->m_playerNum != 0 && m != this && m != m_target) {
            EnemyInfo::Info *e = EnemyInfo::info(m_monsterNum, j);

            if (e->los && e->dist < maxDist) {
                maxDist = e->dist;
                best = m;
            }
        }
    }
    return best;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Monster", getClosestMonsterWithLos__7Monsterf);
#endif
#ifdef NON_MATCHING
/* 14/69 words, untuned: loop setup order */
Monster *Monster::getClosestMonsterToOrientation(float angle, float maxDist)
{
    Monster *best = 0;
    float c = cosf(angle);
    int j;
    int n = game->m_numSlots;
    Monster *m = &game->m_slots[0];

    for (j = 0; j < n; j++, m++) {
        if (m->m_playerNum != 0 && m != this && m != m_target) {
            EnemyInfo::Info *e = EnemyInfo::info(m_monsterNum, j);
            int ok = c < e->dot;

            if (ok) {
                if (e->dist < maxDist) {
                    c = e->dot;
                    maxDist = e->dist;
                    best = m;
                }
            }
        }
    }
    return best;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Monster", getClosestMonsterToOrientation__7Monsterff);
#endif
INCLUDE_ASM("asm/nonmatchings/game/Monster", getClosestMonsterToOrientation__7Monsteriiff);
INCLUDE_ASM("asm/nonmatchings/game/Monster", getClosestMonsterToPunch__7Monsterfff);
INCLUDE_ASM("asm/nonmatchings/game/Monster", getClosestMonsterInFOV__7MonsterR8_fvectorfT1fRf);
Monster *Monster::getClosestTargetable(unsigned short a, bool b, float x, float y)
{
    return getClosestTargetable(a, b, x, x, y);
}
INCLUDE_ASM("asm/nonmatchings/game/Monster", getClosestTargetable__7MonsterUsbfff);
INCLUDE_ASM("asm/nonmatchings/game/Monster", getLookAtTarget__7Monster);
INCLUDE_ASM("asm/nonmatchings/game/Monster", getDestructibleFromReticle__7Monsterf);
INCLUDE_ASM("asm/nonmatchings/game/Monster", addAttachment__7MonsterP9_hierhead);
void Monster::handleAction(ActAiNavigation *a)
{
}
INCLUDE_ASM("asm/nonmatchings/game/Monster", attachPickupImpaler__7MonsterGQ2t10LinkedList1ZP6Pickup8IteratorP8_fvector);
void Monster::detachPickupImpaler(bool b)
{
    if (m_impaler) {
        if (b)
            LevelPickups::killPickup(*(PickupIter *)&m_impaler, 1);
        m_impaler = 0;
    }
    if (m_reverseImpaler) {
        if (b)
            LevelPickups::killPickup(*(PickupIter *)&m_reverseImpaler, 1);
        m_reverseImpaler = 0;
    }
}
void Monster::dropPickupImpaler(void)
{
    if (m_impaler) {
        LevelPickups::dropPickup(*(PickupIter *)&m_impaler);
        m_impaler = 0;
    }
    if (m_reverseImpaler) {
        LevelPickups::dropPickup(*(PickupIter *)&m_reverseImpaler);
        m_reverseImpaler = 0;
    }
}
INCLUDE_ASM("asm/nonmatchings/game/Monster", handleLocator__7MonsterUiRA3_A3_fP8_fvector);
INCLUDE_ASM("asm/nonmatchings/game/Monster", enterNewState__7MonsterP12MonsterState);
void Monster::landingShake(void)
{
}
float Monster::getCollisionDamage(float m)
{
    return m_collisionBase + m * m_collisionScale;
}
#ifdef NON_MATCHING
/* 73/78 words, untuned: v0/v1 swap for the constant 1 */
void Monster::creditStamina(float amount, bool baseOnly)
{
    StaminaMeter *sm;
    int full;

    if (baseOnly) {
        sm = &m_stamina;
        if (m_cameraFollows && m_playerNum == 1 && amount > 1.0f) {
            if (sm->cur < m_stamina.max)
                gameHud(m_cameraView)->registerStaminaCredit((int)amount);
        }
        sm->creditBaseOnly(amount);
        return;
    }
    sm = &m_stamina;
    if (m_cameraFollows) {
        full = 1;
        if (m_playerNum == 1 && amount > 1.0f) {
            if (!(sm->cur >= sm->maxLevel))
                full = 0;
            if (full == 0)
                gameHud(m_cameraView)->registerStaminaCredit((int)amount);
        }
    }
    sm->credit(amount);
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Monster", creditStamina__7Monsterfb);
#endif
void Monster::creditHealth(float amount)
{
    ((HealthMeter *)((char *)this + 0x448))->credit(amount);
    if (m_cameraFollows && m_playerNum == 1)
        gameHud(m_cameraView)->registerHealthCredit((int)amount);
}
void Monster::breathFire(void)
{
    FireBreath *fb = (FireBreath *)m_fireBreath;

    if (!fb->state)
        fb->Activate(*(float *)((char *)this + 0xF990), *(float *)((char *)this + 0xF994), *(float *)((char *)this + 0xF998),
                     *(float *)((char *)this + 0xF99C), *(float *)((char *)this + 0xF9A4), *(float *)((char *)this + 0xF9A0),
                     *(float *)((char *)this + 0xF9A8));
}
void Monster::lightOnFire(float count, float damage, int source)
{
    if (m_typeBits == 0x120 && ((FireBreath *)m_fireBreath)->state)
        return;
    m_onFireCount = count;
    m_onFireDamage = damage;
    if (source) {
        if (*Interactives::getInteractive(source) == 1)
            m_fireSource = (Monster *)Interactives::getInteractive(source);
        else
            m_fireSource = 0;
    } else {
        m_fireSource = 0;
    }
}
void Monster::updateOnFire(void)
{
    if (m_onFireCount > 0.0f) {
        m_onFireCount = m_onFireCount - (float)timerGetFieldsLastFrame();
        if (m_fireFx == -1)
            m_fireFx = particleCreateFx((_fvector *)((char *)this + 0x3E30), (float (*)[4])((char *)this + 0x3340), 0x2C, 3.0f, 0, 0, false, 0.0f);
        takeDamage(m_onFireDamage / (float)(timerGetFieldsLastFrame() * 60), true, m_fireSource);
        ((FireSound *)m_sound)->updateFireSound((_fvector *)((char *)m_cs + 0x10));
    } else if (m_fireFx != -1) {
        particleKillFx(m_fireFx);
        ((FireSound *)m_sound)->terminateFireSound();
        m_fireSource = 0;
    }
}
void Monster::startShocking(float a, float b)
{
    m_beingShockedCount = a;
    m_beingShockedDamage = b;
}
INCLUDE_ASM("asm/nonmatchings/game/Monster", updateBeingShocked__7Monster);
void Monster::startBeingImpaled(float a, float b)
{
}
bool Monster::isHolding(void)
{
    return m_pickup != 0 || m_target != 0;
}
#ifdef NON_MATCHING
/* 6/15 words, untuned: bit extract and branch shape */
bool Monster::isHoldingLarge(void)
{
    bool r = false;

    if (m_pickup && ((int)(*(long *)(*(char **)m_pickup + 0x50) >> 1) & 1))
        r = true;
    else if (m_target)
        r = true;
    return r;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Monster", isHoldingLarge__7Monster);
#endif
#ifdef NON_MATCHING
/* 5/17 words, untuned */
bool Monster::isBlocking(void)
{
    int id = *m_state;

    if ((id == 0x1B && m_blockFlag1B != 0) || (id == 0x1C && m_blockFlag1C != 0))
        return true;
    return false;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Monster", isBlocking__7Monster);
#endif
#ifdef NON_MATCHING
/* 24/25 words, untuned: last compare branches the other way */
bool Monster::isIdle(unsigned t)
{
    if (*m_state == 0x1E && t < ((unsigned *)m_state)[2])
        return true;
    if (*m_state == 0x1F && ((int *)m_state)[0x1F] == 0 && t < ((unsigned *)m_state)[2])   /* the retail branch delay slot sets the result to 1 when t < [2] */
        return true;
    return false;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Monster", isIdle__7MonsterUi);
#endif
int Monster::inCameraFov(_fvector &a, _fvector &b)
{
    int view = 0;

    if (!gUseUnifiedView)
        view = m_cameraView;
    return viewInFOV(view, &a, &b);
}
bool Monster::hasPinTarget(void)
{
    char *pin = (char *)m_pinTarget;

    return pin && (*(unsigned short *)(pin + 4) & 2);
}
unsigned Monster::updateClosestPath(void)
{
    if (!AiPathNet::monster.isIn(*(_fvector *)((char *)m_cs + 0x10), *m_closestPath))
        m_closestPath = AiPathNet::monster.getClosestPath(*(_fvector *)((char *)m_cs + 0x10), 0xFF);
    return 0x1E;
}
INCLUDE_ASM("asm/nonmatchings/game/Monster", okToDrawReticle__7Monster);
void Monster::setCloakOn(void)
{
    if (m_cloaked == 0) {
        m_cloaked = 1;
        m_cloakTime = PowerUps::instance.getCloakTime();
        setEnvMapping();
        ((MonsterSound *)m_sound)->playCloakingSound();
    }
}
void Monster::setCloakOff(void)
{
    m_cloaked = 0;
    m_cloakTime = 0;
    clearEnvMapping();
}
void attachFxToHandle(int *handle, _fvector *pos, unsigned id)
{
    void *p;

    *handle = particleCreateFx(pos, (float (*)[4])UpMatrix, id, 1.0f, 0, 0, false, 0.0f);
    p = particleGetParticle(*handle);
    if (p)
        *(int **)((char *)p + 0x2ED8) = handle;
}
#ifdef NON_MATCHING
/* 13/57 words, untuned: lq scheduling around the y + 2 add */
void Monster::updateWaterWake(float y, bool on)
{
    char *vt = *(char **)((char *)this + 0x10);
    Q16 *pos = ((Q16 * (*)(void *))(*(void **)(vt + 0x14)))((char *)this + *(short *)(vt + 0x10));

    long t0, t1;

    __asm__ volatile("lq %0, %1" : "=r"(t0) : "m"(*(Q16 *)pos));
    y += 2.0f;
    __asm__ volatile("lq %1, %3
	"
                     "sq %0, %2
	"
                     "sq %1, %4"
                     : "+r"(t0), "=&r"(t1), "=m"(*(Q16 *)m_wakePos)
                     : "m"(*(Q16 *)((char *)this + 0x270)), "m"(*(Q16 *)m_wakeX6CA0));
    m_wakePos[3] = y;
    if (on) {
        *(int *)((char *)this + 0x6CB8) = 0;
        if (*&m_wakeFx == -1)
            attachFxToHandle(&m_wakeFx, (_fvector *)m_wakePos, 0x92);
        if (*&m_splashFx == -1)
            attachFxToHandle(&m_splashFx, (_fvector *)m_wakePos, 0x14);
    } else {
        if (*&m_wakeFx != -1)
            particleKillFx(*&m_wakeFx);
        if (*&m_splashFx != -1)
            particleKillFx(*&m_splashFx);
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Monster", updateWaterWake__7Monsterfb);
#endif
struct MovieCleanup {
    char pad0[0x30];
    char *p;
};
#ifdef NON_MATCHING
/* 11/29 words, untuned: delay slot of the ApplyMint call */
void Monster::cleanUpForMovie(void)
{
    setCloakOff();
    if (((FireBreath *)m_fireBreath)->state)
        ((FireBreath *)m_fireBreath)->ApplyMint();
    if (m_x6874)
        *(char *)(m_x6874 + 0xC) = 0;
    if (((MovieCleanup *)((char *)this + 0x10714))->p)
        *(char *)(((MovieCleanup *)((char *)this + 0x10714))->p + 0xC) = 0;
    if (*(char **)((char *)this + 0x6BF8))
        *(char *)(*(char **)((char *)this + 0x6BF8) + 0xC) = 0;
    if (*(char **)((char *)this + 0x6BFC))
        *(char *)(*(char **)((char *)this + 0x6BFC) + 0xC) = 0;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Monster", cleanUpForMovie__7Monster);
#endif
void Monster::setShadowOnOff(bool on)
{
    if (m_shadowCs != 0) {
        if (on) {
            m_shadowOff = 0;
            *(int *)(m_shadowCs + 0x10) = (int)m_cs;
            *(int *)(m_shadowCs + 0x18) = m_shadowSaved;
            return;
        }
        m_shadowOff = 1;
        *(int *)(m_shadowCs + 0x10) = 0;
        m_shadowSaved = *(int *)(m_shadowCs + 0x18);
        *(int *)(m_shadowCs + 0x18) = 0;
    }
}
#ifdef NON_MATCHING
/* 4/10 words, untuned: retail fills the branch-likely delay slot */
void Monster::setSecondaryShadowBlocker(_cs *c)
{
    if (m_shadowCs) {
        if (m_shadowOff)
            m_shadowSaved = (int)c;
        else
            *(_cs **)(m_shadowCs + 0x18) = c;
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Monster", setSecondaryShadowBlocker__7MonsterP3_cs);
#endif
#ifdef NON_MATCHING
/* 1/17 words, untuned */
void Monster::drainSpecial(void)
{
    m_specialWeapon = 0;
    if (m_stamina.max < m_stamina.cur)
        m_stamina.drain(m_stamina.cur - m_stamina.max, false, false);
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Monster", drainSpecial__7Monster);
#endif
#ifdef NON_MATCHING
/* 7/14 words, untuned */
bool Monster::isSpecialAvailable(void) const
{
    if (game->m_gameMode != 9)
        return m_specialWeapon != 0;
    return false;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Monster", isSpecialAvailable__C7Monster);
#endif
INCLUDE_ASM("asm/nonmatchings/game/Monster", func_00163000);
INCLUDE_ASM("asm/nonmatchings/game/Monster", func_00163020);
INCLUDE_ASM("asm/nonmatchings/game/Monster", _vt$7Monster);
INCLUDE_ASM("asm/nonmatchings/game/Monster", __tf7Monster);
INCLUDE_ASM("asm/nonmatchings/game/Monster", __7Monster);
void Monster::updateTurn(bool b)
{
    ((MonsterDynamics *)((char *)this + 0x100))->updateTurn(b);
}
void Monster::updateMove(bool b)
{
    ((MonsterDynamics *)((char *)this + 0x100))->updateMove(b);
}
void Monster::stopFireBreath(void)
{
    ((FireBreath *)m_fireBreath)->ApplyMint();
}
void Monster::putOutFire(void)
{
    m_onFireCount = 0;
}
void *Monster::getMotionRot(void)
{
    return (char *)this + 0xA0;
}
void *Monster::getPrevTrans(void)
{
    return (char *)this + 0x90;
}
void *Monster::getPrevMat(void)
{
    return (char *)this + 0x50;
}
void *Monster::getLocatorTrans(int i)
{
    return (char *)this + i * 16 - 0x4260;
}
void *Monster::getLocatorMat(int i)
{
    char *b = (char *)this + 0x3180;
    return b + (i * 64 - 0x20080);
}
void *Monster::getPinTrans(void)
{
    return (char *)this + 0x3E30;
}
void *Monster::getLookAtTrans(void)
{
    return (char *)this + 0x3E80;
}
void *Monster::getAnim(MonsterAnim a)
{
    return (char *)this + ((int)a * 16 + 0x1CF0);
}
void *Monster::getDynamics(void)
{
    return (char *)this + 0x100;
}
void *Monster::getVel(void)
{
    return (char *)this + 0x260;
}
int Monster::getPickup(void)
{
    return m_pickup;
}
int Monster::getImpaler(void)
{
    return m_impaler;
}
int Monster::getReverseImpaler(void)
{
    return m_reverseImpaler;
}
void * Monster::getGrapplee(void)
{
    return m_target;
}
Monster * Monster::getGrappler(void)
{
    return m_grappler;
}
Monster * Monster::getGrappleAttempt(void)
{
    return m_grappleAttempt;
}
Monster * Monster::getBeamVictim(void)
{
    return m_beamVictim;
}
#ifdef NON_MATCHING
/* 2/4 words, untuned: retail splits the offset as 0x8450 + 0x1C */
Monster *Monster::getKiller(void)
{
    return m_killer;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Monster", getKiller__7Monster);
#endif
int Monster::getPinTarget(void)
{
    return m_pinTarget;
}
float Monster::getPinTime(void)
{
    return m_pinTime;
}
PlayerDat * Monster::getPlayerInfo(void)
{
    return m_playerInfo;
}
void *Monster::getAi(void)
{
    return (char *)this + 0x4E0;
}
_cs * Monster::getReticleCS(void)
{
    return m_reticleCS;
}
_cs * Monster::getStickyReticleCS(void)
{
    return m_stickyReticleCS;
}
_cs *Monster::getShadow(void)
{
    return m_shadow;
}
void *Monster::getFireBreath(void)
{
    return (char *)this + 0x68C0;
}
void *Monster::getMonsterSound(void)
{
    return (char *)this + 0x1A7C;
}
void *Monster::getStaminaMeter(void)
{
    return (char *)this + 0x460;
}
void *Monster::getHealthMeter(void)
{
    return (char *)this + 0x448;
}
void *Monster::getLeadVec(void)
{
    return (char *)this + 0x6990;
}
int Monster::getAutoLeadMovesReticle(void)
{
    return m_autoLeadMovesReticle;
}
void *Monster::getReticleLosResult(void)
{
    return (char *)this + 0x6C50;
}
void *Monster::getShadowHDResult(void)
{
    return (char *)this + 0x1A40;
}
AiPath *Monster::getClosestPath(void)
{
    return m_closestPath;
}
int Monster::getReticleState(void) const
{
    return m_reticleState;
}
void *Monster::getEnemyInfo(Monster &m)
{
    return EnemyInfo::getInfo(*this, m);
}
int *Monster::getState(void)
{
    return m_state;
}
int Monster::getStateId(void)
{
    return *m_state;
}
int Monster::getPrevStateId(void)
{
    return *m_prevState;
}
void *Monster::getCameraData(int view, Camera::CameraPOV pov)
{
    return (char *)this + (view * 0x640 + 0x6CF0) + (int)pov * 0xA0;
}
int Monster::getType(void) const
{
    return m_playerNum;
}
int Monster::getIndex(void) const
{
    return m_index;
}
int Monster::getName(void) const
{
    return m_typeBits;
}
int Monster::getDupId(void) const
{
    return m_dupId;
}
int Monster::getNumInits(void) const
{
    return m_numInits;
}
int Monster::getMonsterNum(void) const
{
    return m_monsterNum;
}
int Monster::getSkinNum(void) const
{
    return m_skinNum;
}
float Monster::getSpeed(void)
{
    return m_speed;
}
float Monster::getHealth(void)
{
    return m_health;
}
float Monster::getMaxHealth(void)
{
    return m_maxHealth;
}
float Monster::getStamina(void)
{
    return m_stamina.cur;
}
float Monster::getHeight(void) const
{
    return m_bodyHeight + m_heightAboveCOG;
}
float Monster::getWidth(void) const
{
    return m_width;
}
int Monster::getCameraThatFollows(void) const
{
    return m_cameraView;
}
int Monster::getWinsThisGame(void) const
{
    return m_winsThisGame;
}
float Monster::getClimbSpeed(void) const
{
    return m_climbSpeed;
}
float Monster::getClimbStrafeSpeed(void) const
{
    return m_climbStrafeSpeed;
}
int Monster::getFallTime(void) const
{
    return m_fallTime;
}
float Monster::getGroundHeight(void) const
{
    return *(float *)((char *)m_shadow + 0x18);
}
float Monster::getHeightAboveCOG(void) const
{
    return m_heightAboveCOG;
}
float Monster::getRunTime(void) const
{
    return m_runTime;
}
int Monster::getPlayerAiOrFodderNum(void) const
{
    return m_index;
}
int Monster::getIsCameraFollowingThisMonster(void)
{
    return m_cameraFollows;
}
float Monster::puPunchDamageMod(ePickupType t) const
{
    return m_puPunchDamageMod[t];
}
float Monster::puLaunchDamageMod(ePickupType t) const
{
    return m_puLaunchDamageMod[t];
}
float Monster::puDurationMod(ePickupType t) const
{
    return m_puDurationMod[t];
}
float Monster::puSpeedMod(ePickupType t) const
{
    return m_puSpeedMod[t];
}
float Monster::getDpDamage(void) const
{
    return m_dpDamage;
}
float Monster::getDpDuration(void) const
{
    return m_dpDuration;
}
float Monster::getDpSpeed(void) const
{
    return m_dpSpeed;
}
float Monster::getDpHomingFactor(void) const
{
    return m_dpHomingFactor;
}
float Monster::getDpVertHomingFactor(void) const
{
    return m_dpVertHomingFactor;
}
float Monster::getDpHeadingBreak(void) const
{
    return m_dpHeadingBreak;
}
float Monster::getDpPitchBreak(void) const
{
    return m_dpPitchBreak;
}
int Monster::getCamIdleCircuitTime(void) const
{
    return m_camIdleCircuitTime;
}
float Monster::getCamIdleFactor(void) const
{
    return m_camIdleFactor;
}
float Monster::getLandingShakeAmp(void) const
{
    return m_landingShakeAmp;
}
float Monster::getLandingShakeFreq(void) const
{
    return m_landingShakeFreq;
}
float Monster::getLandingShakeDur(void) const
{
    return m_landingShakeDur;
}
float Monster::getLandingShakeFalloff(void) const
{
    return m_landingShakeFalloff;
}
float Monster::getLandingShakeMag(void) const
{
    return m_landingShakeMag;
}
float Monster::getPitchRate1(void) const
{
    return m_pitchRate1;
}
float Monster::getPitchRate2(void) const
{
    return m_pitchRate2;
}
float Monster::getMaxHeadingChange(void) const
{
    return m_maxHeadingChange;
}
float Monster::getAimPitch(void) const
{
    return m_aimPitch;
}
float Monster::getAimHeading(void) const
{
    return m_aimHeading;
}
float Monster::getRearOffset(void) const
{
    return m_rearOffset;
}
float Monster::getTargetingMod(void) const
{
    return m_targetingMod;
}
int Monster::getFallTimeBeforePitch(void) const
{
    return m_fallTimeBeforePitch;
}
float Monster::getPinMaxPitch(void) const
{
    return m_pinMaxPitch;
}
float Monster::getPinMuckingDist(void) const
{
    return m_pinMuckingDist;
}
float Monster::getOnFireCount(void) const
{
    return m_onFireCount;
}
float Monster::getOnFireDamage(void) const
{
    return m_onFireDamage;
}
float Monster::getBeingShockedCount(void) const
{
    return m_beingShockedCount;
}
float Monster::getBeingShockedDamage(void) const
{
    return m_beingShockedDamage;
}
int Monster::getLaunchCounter(void) const
{
    return m_launchCounter;
}
int Monster::getLaunchDelay(void) const
{
    return m_launchDelay;
}
bool Monster::isVulnerable(void) const
{
    return m_unk49 != 0;
}
bool Monster::isDead(void) const
{
    return m_dead != 0;
}
bool Monster::isCloaked(void) const
{
    return m_cloaked != 0;
}
bool Monster::isTurning(void) const
{
    return m_turning != 0;
}
bool Monster::isFalling(void) const
{
    return m_freeFalling != 0;
}
bool Monster::attacksEnabled(void) const
{
    return m_attacksEnabled != 0;
}
bool Monster::ranDeathSequence(void) const
{
    return m_unkF6 != 0;
}
bool Monster::ranVictorySequence(void) const
{
    return m_unkF7 != 0;
}
#ifdef NON_MATCHING
/* 6/9 words, untuned: branch shape */
bool Monster::isFullHealth(void)
{
    return m_maxHealth <= m_health;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Monster", isFullHealth__7Monster);
#endif
#ifdef NON_MATCHING
/* 7/10 words, untuned: branch shape */
bool Monster::isFullStamina(void)
{
    return m_stamina.maxLevel <= m_stamina.cur;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Monster", isFullStamina__7Monster);
#endif
#ifdef NON_MATCHING
/* 4/15 words, untuned */
bool Monster::isTargetPinning(void)
{
    if (m_pinMode == 0)
        return m_padFlags[0]->f32 != 0;
    return m_pinToggle != 0;
}
#else
INCLUDE_ASM("asm/nonmatchings/game/Monster", isTargetPinning__7Monster);
#endif
bool Monster::isOnFire(void)
{
    return m_onFireCount > 0.0f;
}
bool Monster::isBeingShocked(void)
{
    return m_beingShockedCount > 0.0f;
}
bool Monster::inSpecialState(void)
{
    return m_state == m_specialState;
}
void Monster::setCameraFollowsMonster(int view, bool follows)
{
    m_cameraView = view;
    m_cameraFollows = follows;
}
void Monster::setCs(_cs * v)
{
    m_cs = v;
}
void Monster::setGodMode(bool v)
{
    m_godMode = v;
}
void Monster::setWinsThisGame(int v)
{
    m_winsThisGame = v;
}
void Monster::setDamageModifier(float v)
{
    m_damageModifier = v;
}
void Monster::setRanDeathSequence(bool v)
{
    m_unkF6 = v;
}
void Monster::setRanVictorySequence(bool v)
{
    m_unkF7 = v;
}
void Monster::setDead(bool v)
{
    m_dead = v;
}
void Monster::setTurning(bool v)
{
    m_turning = v;
}
void Monster::setPadEnabled(bool v)
{
    m_unkF9 = v;
}
void setPickup__7MonsterGQ2t10LinkedList1ZP6Pickup8Iterator(void *self, int v) __asm__("setPickup__7MonsterGQ2t10LinkedList1ZP6Pickup8Iterator");
void setPickup__7MonsterGQ2t10LinkedList1ZP6Pickup8Iterator(void *self, int v)
{
    *(int *)((char *)self + 0x68A4) = v;
}
void Monster::setGrapplee(Monster * v)
{
    m_target = v;
}
void Monster::setGrappler(Monster * v)
{
    m_grappler = v;
}
void Monster::setGrappleAttempt(Monster * v)
{
    m_grappleAttempt = v;
}
void Monster::setBeamVictim(Monster * v)
{
    m_beamVictim = v;
}
void Monster::setFreeFalling(bool v)
{
    m_freeFalling = v;
}
void Monster::setFallTime(int v)
{
    m_fallTime = v;
}
void Monster::setDupId(int v)
{
    m_dupId = v;
}
void Monster::setTypeOfMonster(int v)
{
    m_playerNum = v;
}
void Monster::setMonsterNum(int v)
{
    m_monsterNum = v;
}
void Monster::setSkinNum(int v)
{
    m_skinNum = v;
}
void Monster::setPlayerAiOrFodderNum(int v)
{
    m_index = v;
}
void Monster::setPlayerInfo(PlayerDat * v)
{
    m_playerInfo = v;
}
void Monster::setInteractiveIndex(int v)
{
    m_id = v;
}
int Monster::getInteractiveIndex(void)
{
    return m_id;
}
void Monster::setName(int v)
{
    m_typeBits = v;
}
void Monster::setPitchRate1(float v)
{
    m_pitchRate1 = v;
}
void Monster::setPitchRate2(float v)
{
    m_pitchRate2 = v;
}
void Monster::setCamIdleCircuitTime(int v)
{
    m_camIdleCircuitTime = v;
}
void Monster::setCamIdleFactor(float v)
{
    m_camIdleFactor = v;
}
void Monster::setClosestPath(AiPath * v)
{
    m_closestPath = v;
}
void Monster::setVulnerable(bool v)
{
    m_unk49 = v;
}
void Monster::setInvulnerabilityDuration(int n)
{
    m_unk49 = 0;
    TaskManager::global.add(restoreVulnerability, this, n);
}
void Monster::setAttacksEnabled(bool v)
{
    m_attacksEnabled = v;
}
void Monster::setAttackDisableDuration(int n)
{
    m_attacksEnabled = 0;
    TaskManager::global.add(restoreAttacksEnabled, this, n);
}
void Monster::setAimHeadingEnabled(bool v)
{
    m_aimHeadingEnabled = v;
}
void Monster::setLookAtOverride(_fvector * v)
{
    m_lookAtOverride = v;
}
void Monster::drainStamina(float amount, bool b)
{
    m_stamina.drain(amount, b, false);
}
void Monster::enableStaminaRegen(bool b)
{
    m_stamina.enabled = b;
}
void Monster::enableSpecialWeapon(bool v)
{
    m_specialWeapon = v;
}
void Monster::startHealthPowerUpGlow(int a, int b)
{
    m_healthGlow[0] = a;
    m_healthGlow[2] = b;
    m_healthGlow[1] = b;
}
void Monster::startStaminaPowerUpGlow(int a, int b)
{
    m_staminaGlow[0] = a;
    m_staminaGlow[2] = b;
    m_staminaGlow[1] = b;
}
void Monster::startSpecialPowerUpGlow(int a)
{
    m_specialGlow[0] = a;
    m_specialGlow[2] = 0x7F;
    m_specialGlow[1] = 0;
}
void Monster::endSpecialPowerUpGlow(void)
{
    m_specialGlow[2] = 0;
}
void Monster::incrementWinsThisGame(int n)
{
    m_winsThisGame += n;
}
int Monster::getHudTexture(void)
{
    return m_hudTexture;
}
void Monster::okToGlow(bool v)
{
    m_okToGlow = v;
}
void Monster::okToUnify(bool v)
{
    m_camUnify = v;
}
unsigned Monster::updateClosestPath(void *p)
{
    return ((Monster *)p)->updateClosestPath();
}
unsigned Monster::restoreVulnerability(void *p)
{
    return ((Monster *)p)->restoreVulnerability();
}
int Monster::restoreVulnerability(void)
{
    m_unk49 = 1;
    return 0;
}
unsigned Monster::restoreAttacksEnabled(void *p)
{
    return ((Monster *)p)->restoreAttacksEnabled();
}
int Monster::restoreAttacksEnabled(void)
{
    m_attacksEnabled = 1;
    return 0;
}
void Monster::setShadow(_cs * v)
{
    m_shadow = v;
}
void Monster::setHudTexture(int v)
{
    m_hudTexture = v;
}
void Monster::setReticleCS(_cs * v)
{
    m_reticleCS = v;
}
void Monster::setStickyReticleCS(_cs * v)
{
    m_stickyReticleCS = v;
}
bool Monster::lyingOnGround(void)
{
    int *st = m_state;
    bool r = false;

    if (st[1] & 0x10)
        r = *(int *)((char *)st + 0x260) == 2;
    return r;
}
void *Monster::getFootHDResult(void)
{
    return (char *)this + 0x3B0;
}
INCLUDE_ASM("asm/nonmatchings/game/Monster", func_00163CB8);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC070);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC0A0);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC0D0);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC0F8);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC108);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC128);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC148);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC160);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC178);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC198);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC1C0);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC1E0);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC200);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC220);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC238);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC250);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC270);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC288);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC2A0);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC2C0);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC2E0);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC2F0);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC310);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC330);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC350);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC370);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC388);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC3A8);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC3C0);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC3D0);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC3F8);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC420);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC440);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC460);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC478);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC490);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC4A0);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC4B8);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC4D0);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC4E8);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC4F8);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC508);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC518);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC528);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC538);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC550);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC570);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC590);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC5A8);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC5D0);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC600);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC618);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC628);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC638);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC648);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC658);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC678);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC698);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC6A8);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC6C8);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC6F0);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC700);
INCLUDE_ASM("asm/nonmatchings/game/Monster", D_006EC710);
