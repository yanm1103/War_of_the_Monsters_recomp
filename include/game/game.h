#ifndef GAME_H
#define GAME_H

#include "engine.h"
#include "game/hud.h"
#include "game/weapons.h"
#include "game/stamina_meter.h"
#include "game/pad_flags.h"
#include "game/token_manager.h"
#include "hieri_types.h"

/* Partial class layouts recovered from usage. Unknown regions are padding until identified. */

class MonsterState;

class AiPath;
class PlayerDat;
class _fvector;
class ActAiNavigation;
class DbInteractive;
enum MonsterAnim { MonsterAnim_dummy };
enum MonsterReticleState { MonsterReticleState_dummy };
enum ePickupType { PICKUP_TYPE_0 };

class Camera {
public:
    enum CameraPOV { POV_0, POV_1, POV_2, POV_3 };
};

/* Monster (0x11190 bytes). Its MonsterStates live inside it at fixed offsets (from the constructor):
 *   0x7984 Idle, 0x79AC Run, 0x7A90 Dash, 0x7AD4 Jump, 0x7DA0 Block, 0x7DD8 Counter, 0x7E30 Recoil, 0x7EA0 KnockBack,
 *   0x8450 Death, 0x8470 Punch, 0xAE90 BatSwipe, 0xDC70 PickUp, 0xDCE0 Grapple, 0xDD54 GrappleLift, 0xDD74 GrappleThrow,
 *   0xDDA0 GrappleThrow1, 0xDDCC Grappled, 0xDE30 BeingSlammed, 0xE3E0 BeingThrown, 0xE990 GrappleBreak, 0xEF40 Throw,
 *   0xEFE0 Climb, 0xF0B0 Fly, 0xF3F0 CrowdControl, 0xF6F4 SonicRoar, 0xF974 FireBreath, 0xF9D8 BugAttack, 0xFA1C ButtSlam,
 *   0xFD40 RamAttack, 0xFE20 Taunt, 0xFE38 Javelin, 0xFE5C RobotSpecial, 0x102A8 RockSpecial, 0x1059C ZapAttack,
 *   0x10600 GrappleHook, 0x106E0 Impaled, 0x10714 Stunned, 0x1074C TwoHandedThrow, 0x10790 AirStrike, 0x10930 LavaBlast,
 *   0x10AE0 GrappleOHAttack, 0x10B34 MonkeyOHAttack, 0x10BA0 Shocked, 0x10BF0 Catch, 0x10C40 GetupAttack, 0x10CF0 StompAttack,
 *   0x10D50 TazerAttack, 0x10D90 ShieldAttack, 0x10E70 Victory, 0x10E90 GunAttack, 0x10EBC TopSpin, 0x10F60 CannonHands,
 *   0x11100 BigTakeHit, 0x11114 Countered, 0x11130 UltraTazer. MonsterDynamics is embedded at 0x100. */
class Monster {
public:
    void enterNewState(MonsterState *state);
    void takeDamage(float dmg, bool b, Monster *src);
    void takeHit(_fvector *dir, float dmg, int attackerId);
    void knockBack(_fvector &dir, float a, float b);
    void takeAdditiveRecoil(_fvector &dir, float f);
    void initAfterDbLoad(void);
    bool attacksEnabled(void) const;
    float getHeight(void) const;
    float getGroundHeight(void) const;
    Monster *getKiller(void);
    int getPrevStateId(void);
    int getStateId(void);
    void incrementWinsThisGame(int n);
    bool isCloaked(void) const;
    bool isDead(void) const;
    bool isFalling(void) const;
    bool isTurning(void) const;
    bool isVulnerable(void) const;
    bool ranDeathSequence(void) const;
    bool ranVictorySequence(void) const;
    float puDurationMod(ePickupType t) const;
    float puLaunchDamageMod(ePickupType t) const;
    float puPunchDamageMod(ePickupType t) const;
    float puSpeedMod(ePickupType t) const;
    int restoreAttacksEnabled(void);
    static unsigned restoreAttacksEnabled(void *p);
    int restoreVulnerability(void);
    static unsigned restoreVulnerability(void *p);
    void setCameraFollowsMonster(int view, bool follows);
    void startHealthPowerUpGlow(int a, int b);
    void startShocking(float a, float b);
    void startStaminaPowerUpGlow(int a, int b);
    bool isFullHealth(void);
    bool isFullStamina(void);
    bool isOnFire(void);
    bool isBeingShocked(void);
    bool isHolding(void);
    bool lyingOnGround(void);
    bool hasPinTarget(void);
    bool inSpecialState(void);
    void stopFireBreath(void);
    void setCloakOff(void);
    void clearEnvMapping(void);
    static void setReticles(int view);
    int okToDrawReticle(void);
    void updateWaterWake(float y, bool on);
    Monster *getClosestMonsterToOrientation(float angle, float maxDist);
    void creditStamina(float amount, bool baseOnly);
    void startCinema(void);
    Monster *getClosestMonster(float maxDist);
    void updateLock(MonsterReticleState state);
    Monster *getClosestMonster2D(float maxDist);
    Monster *getClosestMonsterWithLos(float maxDist);
    Monster *getClosestMonster(int locA, int locB, float radius);
    bool isIdle(unsigned t);
    int inCameraFov(_fvector &a, _fvector &b);
    void setMat(float (&m)[4][4]);
    Monster *getClosestTargetable(unsigned short a, bool b, float x, float y);
    Monster *getClosestTargetable(unsigned short a, bool b, float x, float y, float z);
    void blowUpMonster(Monster *killer);
    bool isHoldingLarge(void);
    void cleanUpForMovie(void);
    void lightOnFire(float count, float damage, int source);
    void breathFire(void);
    void dropPickupImpaler(void);
    void dropPickup(void);
    void throwPickup(int a, float b);
    void detachPickupImpaler(bool b);
    void registerComboHit(Monster *m);
    DbInteractive *getTarget(_fvector &v, bool b);
    void *getCameraData(int view, Camera::CameraPOV pov);
    float getStaminaGain(void);
    void creditHealth(float amount);
    void setRot(float a, float b, float c);
    void setTrans(_fvector &p);
    void *getLocatorTrans(int i);
    void *getLocatorMat(int i);
    void *getAnim(MonsterAnim a);
    unsigned updateClosestPath(void);
    static unsigned updateClosestPath(void *p);
    void setAttackDisableDuration(int n);
    void setSecondaryShadowBlocker(_cs *c);
    void enableStaminaRegen(bool b);
    void endSpecialPowerUpGlow(void);
    float getStamina(void);
    void putOutFire(void);
    void landingShake(void);
    void startBeingImpaled(float a, float b);
    void handleAction(ActAiNavigation *a);
    float getCollisionDamage(float m);
    void drainStamina(float amount, bool b);
    void endCinema(void);
    void *getEnemyInfo(Monster &m);
    void updateMove(bool b);
    void updateTurn(bool b);
    void startSpecialPowerUpGlow(int a);
    void updateDeathSequence(void);
    void playerUpdateInputs(void);
    void updateOnFire(void);
    void updateBeingShocked(void);
    void updateAirLegOverride(void);
    void updateReticle(void);
    void updateLookAt(void);
    void updateBoostAndRage(void);
    void updateBoundingSphere(void);
    void updateAnimContacts(bool b);
    void updatePowerUpGlow(void);
    void setCloakOn(void);
    void recomputeDynamics(void);
    bool isTargetPinning(void);
    void setShadowOnOff(bool on);
    void setEnvMapping(void);
    bool isSpecialAvailable(void) const;
    void setInvulnerabilityDuration(int n);
    void drainSpecial(void);
    bool isBlocking(void);
    void update(void);
    void updateCinema(void);
    void updatePosition(void);
    _cs * getShadow(void);
    float getAimHeading(void) const;
    float getAimPitch(void) const;
    float getBeingShockedCount(void) const;
    float getBeingShockedDamage(void) const;
    float getCamIdleFactor(void) const;
    float getClimbSpeed(void) const;
    float getClimbStrafeSpeed(void) const;
    float getDpDamage(void) const;
    float getDpDuration(void) const;
    float getDpHeadingBreak(void) const;
    float getDpHomingFactor(void) const;
    float getDpPitchBreak(void) const;
    float getDpSpeed(void) const;
    float getDpVertHomingFactor(void) const;
    float getHealth(void);
    float getHeightAboveCOG(void) const;
    float getLandingShakeAmp(void) const;
    float getLandingShakeDur(void) const;
    float getLandingShakeFalloff(void) const;
    float getLandingShakeFreq(void) const;
    float getLandingShakeMag(void) const;
    float getMaxHeadingChange(void) const;
    float getMaxHealth(void);
    float getOnFireCount(void) const;
    float getOnFireDamage(void) const;
    float getPinMaxPitch(void) const;
    float getPinMuckingDist(void) const;
    float getPinTime(void);
    float getPitchRate1(void) const;
    float getPitchRate2(void) const;
    float getRearOffset(void) const;
    float getRunTime(void) const;
    float getSpeed(void);
    float getTargetingMod(void) const;
    float getWidth(void) const;
    int * getState(void);
    int getAutoLeadMovesReticle(void);
    Monster * getBeamVictim(void);
    int getCamIdleCircuitTime(void) const;
    int getCameraThatFollows(void) const;
    AiPath * getClosestPath(void);
    int getDupId(void) const;
    int getFallTime(void) const;
    int getFallTimeBeforePitch(void) const;
    Monster * getGrappleAttempt(void);
    Monster * getGrappler(void);
    int getHudTexture(void);
    int getImpaler(void);
    int getIndex(void) const;
    int getInteractiveIndex(void);
    int getIsCameraFollowingThisMonster(void);
    int getLaunchCounter(void) const;
    int getLaunchDelay(void) const;
    int getMonsterNum(void) const;
    int getName(void) const;
    int getNumInits(void) const;
    int getPickup(void);
    int getPinTarget(void);
    int getPlayerAiOrFodderNum(void) const;
    PlayerDat * getPlayerInfo(void);
    _cs * getReticleCS(void);
    int getReticleState(void) const;
    int getReverseImpaler(void);
    int getSkinNum(void) const;
    _cs * getStickyReticleCS(void);
    int getType(void) const;
    int getWinsThisGame(void) const;
    void * getGrapplee(void);
    void enableSpecialWeapon(bool v);
    void okToGlow(bool v);
    void okToUnify(bool v);
    void setAimHeadingEnabled(bool v);
    void setAttacksEnabled(bool v);
    void setCamIdleCircuitTime(int v);
    void setCamIdleFactor(float v);
    void setDamageModifier(float v);
    void setDead(bool v);
    void setDupId(int v);
    void setFallTime(int v);
    void setFreeFalling(bool v);
    void setGodMode(bool v);
    void setHudTexture(int v);
    void setInteractiveIndex(int v);
    void setMonsterNum(int v);
    void setName(int v);
    void setPadEnabled(bool v);
    void setPitchRate1(float v);
    void setPitchRate2(float v);
    void setPlayerAiOrFodderNum(int v);
    void setRanDeathSequence(bool v);
    void setRanVictorySequence(bool v);
    void setSkinNum(int v);
    void setTurning(bool v);
    void setTypeOfMonster(int v);
    void setVulnerable(bool v);
    void setWinsThisGame(int v);
    void *getAi(void);
    void *getDynamics(void);
    void *getFireBreath(void);
    void *getFootHDResult(void);
    void *getHealthMeter(void);
    void *getLeadVec(void);
    void *getLookAtTrans(void);
    void *getMonsterSound(void);
    void *getMotionRot(void);
    void *getPinTrans(void);
    void *getPrevMat(void);
    void *getPrevTrans(void);
    void *getReticleLosResult(void);
    void *getShadowHDResult(void);
    void *getStaminaMeter(void);
    void *getVel(void);
    void setBeamVictim(Monster * v);
    void setClosestPath(AiPath * v);
    void setCs(_cs * v);
    void setGrappleAttempt(Monster * v);
    void setGrapplee(Monster * v);
    void setGrappler(Monster * v);
    void setLookAtOverride(_fvector * v);
    void setPlayerInfo(PlayerDat * v);
    void setReticleCS(_cs * v);
    void setShadow(_cs * v);
    void setStickyReticleCS(_cs * v);
    void playerInit(void);
    void aiInit(void);
    void init(void);
    void initBeforeDbLoad(void);
    void initDynamics(void);
    void collisInitPoints(void);
    void collisResolveCsToCsCollisions(void);
    void collisTestForCollisions(void);
    void addAttachment(_hierhead *h);

    char pad0[0x4 - 0x0];
    unsigned short m_flags;   /* 0x4 */
    char pad6[0xC - 0x6];
    _cs * m_cs;   /* 0xC */
    char pad10[0x14 - 0x10];
    int m_typeBits;   /* 0x14 */
    int m_playerNum;   /* 0x18 */
    int m_dupId;   /* 0x1C */
    int m_id;   /* 0x20 */
    int m_index;   /* 0x24 */
    int m_monsterNum;   /* 0x28 */
    int m_numInits;   /* 0x2C */
    int m_skinNum;   /* 0x30 */
    int * m_state;   /* 0x34 */
    int * m_prevState;   /* 0x38 */
    int m_winsThisGame;   /* 0x3C */
    float m_runTime;   /* 0x40 */
    int m_frameTime;   /* 0x44 */
    char pad48[0x49 - 0x48];
    signed char m_unk49;   /* 0x49 */
    signed char m_attacksEnabled;   /* 0x4A */
    char pad4B[0xB0 - 0x4B];
    float m_maxHeadingChange;   /* 0xB0 */
    float m_bodyHeight;   /* 0xB4 */
    float m_heightAboveCOG;   /* 0xB8 */
    char padBC[0xC0 - 0xBC];
    float m_rearOffset;   /* 0xC0 */
    float m_width;   /* 0xC4 */
    char padC8[0xDC - 0xC8];
    float m_pitchRate1;   /* 0xDC */
    float m_pitchRate2;   /* 0xE0 */
    char padE4[0xE8 - 0xE4];
    signed char m_dead;   /* 0xE8 */
    unsigned char m_godMode;   /* 0xE9 */
    signed char m_unkEA;   /* 0xEA */
    signed char m_unkEB;   /* 0xEB */
    unsigned char m_unkEC;   /* 0xEC */
    char padED[0xEF - 0xED];
    signed char m_cloaked;   /* 0xEF */
    signed char m_wantsTaunt;   /* 0xF0: a taunt was requested; StateTaunt::transitionOK consumes it */
    signed char m_turning;   /* 0xF1 */
    char padF2[0xF3 - 0xF2];
    unsigned char m_specialWeapon;   /* 0xF3 */
    char padF4[0xF5 - 0xF4];
    signed char m_unkF5;   /* 0xF5 */
    signed char m_unkF6;   /* 0xF6 */
    signed char m_unkF7;   /* 0xF7: victory shown (StateVictory sets it at 85% of the celebration; blocks another one) */
    char padF8[0xF9 - 0xF8];
    unsigned char m_unkF9;   /* 0xF9 */
    char padFA[0x1B8 - 0xFA];
    float m_unk1B8;   /* 0x1B8: MonsterDynamics (embedded at 0x100) +0xB8; set back to 1.0 by the handlePreemption of several states (Block, Death, Punch, Counter, Climb, Grapple) */
    char pad1BC[0x1E8 - 0x1BC];
    float m_unk1E8;   /* 0x1E8: MonsterDynamics (embedded at 0x100) +0xE8; StateBlock::update sets m_unk1B8 to 1.5 while it is negative */
    char pad1EC[0x250 - 0x1EC];
    float m_speed;   /* 0x250 */
    char pad254[0x280 - 0x254];
    signed char m_freeFalling;   /* 0x280 */
    char pad281[0x284 - 0x281];
    int m_fallTimeBeforePitch;   /* 0x284 */
    int m_fallTime;   /* 0x288 */
    int m_camIdleCircuitTime;   /* 0x28C */
    float m_camIdleFactor;   /* 0x290 */
    float m_climbSpeed;   /* 0x294 */
    float m_climbSpeedBase;   /* 0x298 */
    float m_climbStrafeSpeed;   /* 0x29C */
    float m_climbStrafeBase;   /* 0x2A0 */
    char pad2A4[0x440 - 0x2A4];
    float m_collisionBase;   /* 0x440 */
    float m_collisionScale;   /* 0x444 */
    float m_maxHealth;   /* 0x448 */
    float m_health;   /* 0x44C */
    char pad450[0x460 - 0x450];
    StaminaMeter m_stamina;   /* 0x460 */
    char pad48C[0x4AC - 0x48C];
    unsigned char m_okToGlow;   /* 0x4AC */
    char pad4AD[0x4B0 - 0x4AD];
    int m_healthGlow[3];   /* 0x4B0 */
    int m_staminaGlow[3];   /* 0x4BC */
    int m_specialGlow[3];   /* 0x4C8 */
    char pad4D4[0x4D8 - 0x4D4];
    PlayerDat * m_playerInfo;   /* 0x4D8 */
    char pad4DC[0x4E0 - 0x4DC];
    char m_ai[0x1A10 - 0x4E0];   /* 0x4E0: the embedded Ai (ai.h) that drives an AI monster's inputs; its exact size is not known, this runs up to the next named field */
    AiPath * m_closestPath;   /* 0x1A10 */
    char pad1A14[0x1A3C - 0x1A14];
    _cs * m_shadow;   /* 0x1A3C */
    char pad1A40[0x1A70 - 0x1A40];
    int m_shadowOff;   /* 0x1A70 */
    char * m_shadowCs;   /* 0x1A74 */
    int m_shadowSaved;   /* 0x1A78 */
    char m_sound[0x1CF0 - 0x1A7C];   /* 0x1A7C: the embedded MonsterSound (fire_breath.h); FireSound calls use the same address */
    _animHandle m_anims[0x12C];   /* 0x1CF0: one handle per MonsterAnim (getAnim); a = 0 when the monster has no such animation */
    char m_animPappy[0x2FE0 - 0x2FB0];   /* 0x2FB0: AnimPappy; updateCinema reads the float pointer at +0x28 (0x2FD8) */
    char m_cinemaBlendA[0x3048 - 0x2FE0];   /* 0x2FE0: AnimBlend set to 50% by startCinema */
    char m_cinemaBlendB[0x30B4 - 0x3048];   /* 0x3048: AnimBlend set to 50% by startCinema */
    char m_cinemaBlendC[0x311C - 0x30B4];   /* 0x30B4: AnimBlend that startCinema ramps out when m_cinemaBlendCOn is set */
    int m_cinemaBlendCOn;   /* 0x311C */
    _fvector * m_lookAtOverride;   /* 0x3120 */
    char pad3124[0x5024 - 0x3124];
    char m_gamePad[0x5040 - 0x5024];   /* 0x5024: the embedded GamePad (loadPadInputs / clearInputs) that m_padFlags interprets */
    PadFlags m_padFlags;   /* 0x5040 */
    char pad6854[0x6868 - 0x6854];
    int m_hudTexture;   /* 0x6868 */
    char pad686C[0x6874 - 0x686C];
    char * m_x6874;   /* 0x6874 */
    char pad6878[0x68A0 - 0x6878];
    int m_reticleState;   /* 0x68A0 */
    int m_pickup;   /* 0x68A4 */
    int m_impaler;   /* 0x68A8 */
    int m_reverseImpaler;   /* 0x68AC */
    Monster * m_grappler;   /* 0x68B0 */
    void * m_target;   /* 0x68B4 */
    Monster * m_grappleAttempt;   /* 0x68B8 */
    Monster * m_beamVictim;   /* 0x68BC */
    char m_fireBreath[0x697C - 0x68C0];   /* 0x68C0: the embedded FireBreath (fire_breath.h); its first word (state) is nonzero while breathing */
    int m_launchDelay;   /* 0x697C */
    int m_launchCounter;   /* 0x6980 */
    char pad6984[0x69A0 - 0x6984];
    float m_padScale;   /* 0x69A0 */
    char pad69A4[0x69A8 - 0x69A4];
    float m_damageModifier;   /* 0x69A8 */
    float m_dpDamage;   /* 0x69AC */
    float m_dpDuration;   /* 0x69B0 */
    float m_dpSpeed;   /* 0x69B4 */
    float m_dpHomingFactor;   /* 0x69B8 */
    float m_dpVertHomingFactor;   /* 0x69BC */
    float m_dpHeadingBreak;   /* 0x69C0 */
    float m_dpPitchBreak;   /* 0x69C4 */
    float m_puPunchDamageMod[28];   /* 0x69C8 */
    float m_puLaunchDamageMod[28];   /* 0x6A38 */
    float m_puStaminaGainMod[28];   /* 0x6AA8 */
    float m_puDurationMod[28];   /* 0x6B18 */
    float m_puSpeedMod[28];   /* 0x6B88 */
    _cs * m_reticleCS;   /* 0x6BF8 */
    _cs * m_stickyReticleCS;   /* 0x6BFC */
    int m_stickyReticleOn;   /* 0x6C00: draw m_stickyReticleCS too */
    int m_pinTarget;   /* 0x6C04 */
    float m_pinTime;   /* 0x6C08 */
    char pad6C0C[0x6C14 - 0x6C0C];
    float m_targetingMod;   /* 0x6C14 */
    char pad6C18[0x6C20 - 0x6C18];
    float m_pinMaxPitch;   /* 0x6C20 */
    float m_pinMuckingDist;   /* 0x6C24 */
    char pad6C28[0x6C30 - 0x6C28];
    int m_autoLeadMovesReticle;   /* 0x6C30 */
    int m_pinToggle;   /* 0x6C34 */
    int m_pinMode;   /* 0x6C38 */
    int m_aimHeadingEnabled;   /* 0x6C3C */
    float m_aimHeading;   /* 0x6C40 */
    float m_aimPitch;   /* 0x6C44 */
    char pad6C48[0x6C80 - 0x6C48];
    int m_wakeFx;   /* 0x6C80: water wake particle fx (WaterWake), -1 when off (updateWaterWake) */
    int m_splashFx;   /* 0x6C84: water splash particle fx (WaterSplash), -1 when off */
    char pad6C88[0x6C90 - 0x6C88];
    float m_wakePos[4];   /* 0x6C90: where both fx are attached; [3] gets the y argument of updateWaterWake + 2 */
    float m_wakeX6CA0[4];   /* 0x6CA0: copied from this + 0x270 together with m_wakePos (meaning unknown) */
    char pad6CB0[0x6CB4 - 0x6CB0];
    int m_fireFx;   /* 0x6CB4 */
    float m_onFireCount;   /* 0x6CB8 */
    float m_onFireDamage;   /* 0x6CBC */
    Monster * m_fireSource;   /* 0x6CC0 */
    float m_beingShockedCount;   /* 0x6CC4 */
    float m_beingShockedDamage;   /* 0x6CC8 */
    int m_cloakTime;   /* 0x6CCC */
    char pad6CD0[0x6CD4 - 0x6CD0];
    int m_cameraFollows;   /* 0x6CD4 */
    int m_cameraView;   /* 0x6CD8 */
    float m_landingShakeAmp;   /* 0x6CDC */
    float m_landingShakeFreq;   /* 0x6CE0 */
    float m_landingShakeDur;   /* 0x6CE4 */
    float m_landingShakeFalloff;   /* 0x6CE8 */
    float m_landingShakeMag;   /* 0x6CEC */
    char pad6CF0[0x7970 - 0x6CF0];
    int m_camUnify;   /* 0x7970 */
    char pad7974[0x7978 - 0x7974];
    int * m_stateRef;   /* 0x7978 */
    char pad797C[0x7980 - 0x797C];
    int * m_specialState;   /* 0x7980 */
    char pad7984[0x7DD0 - 0x7984];
    int m_blockFlag1B;   /* 0x7DD0 */
    char pad7DD4[0x7E94 - 0x7DD4];
    int m_blockFlag1C;   /* 0x7E94 */
    char pad7E98[0x846C - 0x7E98];
    Monster * m_killer;   /* 0x846C */
    char pad8470[0xFD70 - 0x8470];
    float m_fd70;   /* 0xFD70 */
    float m_fd74;   /* 0xFD74 */
    char padFD78[0x10E70 - 0xFD78];
    char m_victoryState[1];   /* 0x10E70 */
    char pad10E71[0x11190 - 0x10E71];
};
typedef char _size_Monster[sizeof(Monster) == 0x11190 ? 1 : -1];
#define MONSTER_AT(f, off) typedef char _monster_at_##f[(unsigned)&((Monster *)0)->f == (off) ? 1 : -1]
MONSTER_AT(m_ai, 0x4E0);
MONSTER_AT(m_sound, 0x1A7C);
MONSTER_AT(m_animPappy, 0x2FB0);
MONSTER_AT(m_cinemaBlendA, 0x2FE0);
MONSTER_AT(m_cinemaBlendB, 0x3048);
MONSTER_AT(m_cinemaBlendC, 0x30B4);
MONSTER_AT(m_cinemaBlendCOn, 0x311C);
MONSTER_AT(m_gamePad, 0x5024);
MONSTER_AT(m_fireBreath, 0x68C0);
MONSTER_AT(m_stickyReticleOn, 0x6C00);
MONSTER_AT(m_wakeFx, 0x6C80);
MONSTER_AT(m_wakePos, 0x6C90);
MONSTER_AT(m_wakeX6CA0, 0x6CA0);

class TheGame {
public:
    Hud m_huds[4];                   /* 0x000: one per view, stride 0x2E0 */
    Monster m_slots[16];             /* 0xB80: monster array, stride 0x11190; m_numSlots are in use */
    char pad112480[0x11C370 - 0x112480]; /* includes the Weapons spawner at 0x112490 */
    TokenManager m_tokens[2];        /* 0x11C370 */
    char pad11E378[0x120380 - 0x11E378];
    Monster *m_monsters[8];          /* 0x120380: indexed up to m_numMonsters; vegas also reads [4] and [5] */
    char pad1203A0[0x1203C4 - 0x1203A0];
    float m_gravity;                 /* 0x1203C4 */
    int m_gameMode;                  /* 0x1203C8: 1 = the mode AiScript3Mile/central set up special levels for */
    int m_matchMode;                 /* 0x1203CC: 0 or 1 selects the two-player-style AI in AiGrappleAttack */
    int m_levelId;                   /* 0x1203D0: level id (1 central, 2 vegas, 3 canyon2, 5 airport, 6 threemile, 7 sanfran, 8/15 island, 9 tokyo, 10 ufo, 11 final boss, 26 bigshot, 27 crush) */
    int m_numSlots;                  /* 0x1203D4: loop bound over the per-player blocks (gameSlotBase) */
    int m_numMonsters;               /* 0x1203D8: loop bound over m_monsters */
    char pad1203DC[0x1203E0 - 0x1203DC];
    int m_numAIs;                    /* 0x1203E0: AI monsters, stored in m_monsters[4..] */
    int m_numAIsAlive;               /* 0x1203E4: cached by GetNumAIsAlive */
    int m_viewSlot[2];               /* 0x1203E8: monster slot each view follows (gameInitCamera); [0] also selects the level data block */
    char pad1203F0[0x1203FC - 0x1203F0];
    int m_slotInteractive[16];       /* 0x1203FC: Interactives index of each monster slot (AddMonster) */
    int m_won[2];                    /* 0x12043C */
    int m_playerMask;                /* 0x120444: bit 0 = player 1 active, bit 1 = player 2 active (inferred) */
    int m_curDupId;                  /* 0x120448: duplicate id of the monster model being parsed (MonsterParse) */
    char pad12044C[4];
    int f120450, f120454, f120458, f12045C; /* zeroed by InitAfterDbLoad */
    int f120460;                     /* set to 1 by InitAfterDbLoad */
    char pad120464[4];
    int f120468;                     /* set to 1 by InitAfterDbLoad */
    struct PadTweaks {               /* 0x12046C: copied into every monster's PadFlags by UpdatePadTweaks */
        int t16C4;
        int t16D0;
        int t16D4;
        int t16C8;
        int t16F4;
        int t16F8;
        int t16FC;
        int t17AC;
        int t17B0;
        int t16DC;
        int t16E0;
    } m_padTweaks;
    int m_actuator[8];               /* 0x120498: per-pad rumble actuator enable flags */

    void Init(void);
    void InitBeforeDbLoad(void);
    void InitAfterDbLoad(void);
    void Update(void);
    void Update2(void);
    void ResetLevel(void);
    void UnpauseLevel(void);
    void gameResolveCollisions(void);
    void gameResolveLifeAndDeath(void);
    void gameCheckForCloseCombat(void);
    void SetGravity(float g);
    void SetOkToUnify(void);
    int GetNumAIsAlive(void);
    void gameGetStartPoint(Monster *m);
    Monster *getClosestMonster(_fvector &pos, float maxDist, float &distSq);
    Monster *getClosestPlayer(_fvector &pos, float maxDist, float &distSq);
    float GetCameraMaxHeight(_fvector *pos);
    void gameInitCamera(int view, int slot);
    Monster *GetMonsterFromName(int type, int dup);
    void AddMonster(_cs *cs, int type, int dup);
    void MonsterParse(_hierhead *h, _fvector *pos);
    void SetPlayerMonster(int pIdx, int type, int dup, int view, int skin);
    void SetAIMonster(int aiIdx, int type, int dup, int skin);
    void fadeOutAndIn(int n);
    void fadeOut(int n);
    void updateFadeOutAndIn(void);
    void updateFadeOut(void);
    static void updateFadeOutAndIn(void *p);
    static void updateFadeOut(void *p);
    void UpdatePadTweaks(void);
    static void traversalCallback(_cs *cs, unsigned a, unsigned b, float (&m)[4][4], _fvector *eo);
    static void genericEventHandler(unsigned event);

    Weapons *getWeapons(void) { return (Weapons *)((char *)this + 0x112490); }
};

extern TheGame *game;

/* The Hud array sits at the start of TheGame (stride 0x2E0); the weapon spawner at 0x112490. */
static inline Hud *gameHud(int i)
{
    return (Hud *)((char *)game + i * 0x2E0);
}
static inline Weapons *gameWeapons(void)
{
    return (Weapons *)((char *)game + 0x112490);
}

/* Per-level data block: game + idx * 0x11190 + 0xB80. Kept as one offset so the add order matches retail. */
static inline Monster *gameSlotBase(int idx)
{
    return (Monster *)((char *)game + (idx * 0x11190 + 0xB80));
}
extern int gHudEnable;
extern int gUseUnifiedView;


class Cameras {
public:
    char pad0[0x2A58];
    int m_state;              /* 0x2A58: 7 = cinema */

    static Cameras m_cameras;
    static void InitCrushMonsters(Monster *a, Monster *b);
    static void Update(void);
    static void SetCameraToFollowMonster(int view, Monster *m);
    static void SetCameraPOV(int view, Camera::CameraPOV pov);
    static void SetCameraMonster(int view, Monster *m);
    static void TogglePOV(int view);
    static float GetUnifiedTime(void);
};

void fontSetColor(int font, int r, int g, int b, int a);
void fontSetCharSizesInSubPixels(int font, int w, int h, int spacing, int unk);
void fontSpritePrintXY(int font, int x, int y, char *str);
void fontSpritePrintCenteredXY(int font, int x, int y, char *str);
void fontSpritePrintRightXY(int font, int x, int y, char *str);
void fontSetDefaultColor(int font);
void fontSetDefaultSize(int font);
extern "C" int sprintf(char *, const char *, ...);

#endif
