#ifndef ENGINE_H
#define ENGINE_H

/* Engine-level API shared by common/ and game/: math, timers, animation handles, particles, scene graph.
   Only declarations that more than one translation unit needed live here; signatures are what retail mangles to. */

#include "hieri_types.h"

/* ---- animation ---- */
struct _animHandle {
    int a, b, c, d;
};
void animationGetHandle(_animHandle *h, unsigned a, unsigned b, unsigned c);
void animationStart(_animHandle h, bool loop);
void animationStartReverse(_animHandle h, bool loop);
void animationPause(_animHandle h);
void animationLoop(_animHandle h, bool b);
void animationSetSpeed(_animHandle h, float speed);
void animationSetTotalRunFrames(_animHandle h, float frames);
int animationIsRunning(_animHandle h);
int animationIsTransitioning(_animHandle &h);
void animationTransitionInto(_animHandle &h, float time, int reset, int type);
void animationSetToBeginning(_animHandle h, bool b);
float animationGetCurrentPercent(_animHandle h);
void animationRunGlobal(void);

/* ---- math ---- */
int mathfRand(int lo, int hi);
float mathfRandf(float lo, float hi);
float mathfHeadingFromPointToPoint(_fvector *from, _fvector *to);
void mathfNormalizeQuaternion(_fvector *dst, _fvector *src);
void mathfQuaternionToMatrix4x4(float (*m)[4], _fvector *q);
void mathfRotAxisToQuaternion(_fvector *dst, _fvector *axis, float angle);
void mathfConcatQuaternions(_fvector *dst, _fvector *a, _fvector *b);
float smoothEasyIn(float cur, float target, float rate, float eps);
float smoothEasyInTC(float cur, float target, float rate, float eps);

/* ---- input ---- */
int inputGetInput(int pad, int button);
void inputUseActuator(int pad, int enable) __asm__("inputUseActuator__Fib"); /* retail passes the raw int, not a normalised bool */

/* ---- timers ---- */
int timerGetFieldsLastFrame(void);
int timerGetUpdateRate(void);

/* ---- particles / scene graph ---- */
void particleInitAfter(void);
void hdInit(void);
void hierSetTraversalCallback(void (*cb)(_cs *, unsigned, unsigned, float (&)[4][4], _fvector *));
int timerGetMinUpdateRate(void);
extern int m_minUpdateRate;
void particleKillFx(int &handle);
int particleCreateFx(_fvector *pos, float (*m)[4], int type, float f, _fvector *pos2, float (*m2)[4], bool b, float f2);
void hierSetCsEpNode(_cs *cs, _hierhead *ep);
void hdReparentCsGrid(_cs *cs);

#endif
