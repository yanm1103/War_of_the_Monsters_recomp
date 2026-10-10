#include "common.h"
#include "hieri_types.h"

/* Evaluator signature: curve, time, cached key index. */
typedef float (*AnimCurveEvalFn)(AnimCurveHeader *curve, float t, unsigned short *key);

/* Evaluators by [curveType][dataType] (filled by the static initializer). */
extern AnimCurveEvalFn D_007356C8[6][3];

/* A static curve stores its single value right after the header. */
float evalStatic(AnimCurveHeader *curve, float, unsigned short *)
{
    return *(float *)(curve + 1);
}
float animCurveEvaluate(AnimCurveHeader *curve, float t, unsigned short *key)
{
    return D_007356C8[curve->curveType][curve->dataType](curve, t, key);
}
/* The evaluators below are function templates: ee-gcc emits every instance at the end of the TU, so they can
 * only replace the INCLUDE_ASM block all at once. Status of the 36 instances against retail:
 *   identical: evaluateBetweenKeyframes for Linear<f/us/uc>, Stepped<f/us/uc>, Hermite<f,f>, Smooth<f,f>,
 *              Broken<f,f>, Broken<uc,s>
 *   equivalent: every binarySearch (register allocation and the found/return paths), every animCurveEval
 *              (float register choice for start/range), evaluateBetweenKeyframes for Smooth<us/uc,s> and
 *              Broken<us,s> (scheduling) */
#ifdef NON_MATCHING
/* Keyframe layouts. T is the stored time/value type (float, or 8/16-bit values scaled by the curve's
 * AnimNormData); S is the tangent type (float, or a 16-bit code, see animCurveTangent). */
template <class T> struct AnimKeyLinear {
    T value;
};
template <class T> struct AnimKeyStepped {
    T time;
    T value;
};
template <class T, class S> struct AnimKeySmooth {
    T time;
    T value;
    S tangent;
};
template <class T, class S> struct AnimKeyBroken {
    T time;
    T value;
    S tangent[2]; /* start/end tangents of the segment to the next key */
};
template <class T, class S> struct AnimKeyHermite {
    T time;
    T value;
    S coef[3]; /* cubic, quadratic and linear coefficients of the segment */
};

/* Curve body after the header: key count, then the keys. */
template <class K> struct AnimCurveData {
    AnimCurveHeader header;
    unsigned short numKeys;
    K keys[1];
};

/* Quantized curves are preceded by their time/value scale (AnimNormData). */
template <class K> struct AnimNormCurve : AnimNormData, AnimCurveData<K> {
};

template <class K> inline AnimNormData *animCurveNorm(AnimCurveData<K> *data)
{
    return static_cast<AnimNormCurve<K> *>(data);
}

/* Stored time/value to float. */
template <class K> inline float animCurveTime(AnimCurveData<K> *, float v)
{
    return v;
}
template <class K> inline float animCurveTime(AnimCurveData<K> *data, unsigned short v)
{
    AnimNormData *norm = animCurveNorm(data);

    return (float)v * norm->deltaTime + norm->baseTime;
}
template <class K> inline float animCurveTime(AnimCurveData<K> *data, unsigned char v)
{
    AnimNormData *norm = animCurveNorm(data);

    return (float)v * norm->deltaTime + norm->baseTime;
}
template <class K> inline float animCurveValue(AnimCurveData<K> *, float v)
{
    return v;
}
template <class K> inline float animCurveValue(AnimCurveData<K> *data, unsigned short v)
{
    AnimNormData *norm = animCurveNorm(data);

    return (float)v * norm->deltaVal + norm->baseVal;
}
template <class K> inline float animCurveValue(AnimCurveData<K> *data, unsigned char v)
{
    AnimNormData *norm = animCurveNorm(data);

    return (float)v * norm->deltaVal + norm->baseVal;
}

/* Tangent codes: the two top bits pick a linear range (s / 16384) or a reciprocal one (steep slopes). */
inline float animCurveTangent(float s)
{
    return s;
}
inline float animCurveTangent(short s)
{
    float f = s;

    switch (s >> 14) {
    default:
        return f * (1.0f / 16384.0f);
    case 1:
        return 16384.0f / (32768.0f - f);
    case -2:
        return 16384.0f / (-32768.0f - f);
    }
}

/* Time to compare against stored key times: the float itself, or its position on the quantized scale. */
template <class K> inline void animCurveSearchTime(AnimCurveData<K> *, float t, float &out)
{
    out = t;
}
template <class K> inline void animCurveSearchTime(AnimCurveData<K> *data, float t, int &out)
{
    AnimNormData *norm = animCurveNorm(data);

    out = (int)((t - norm->baseTime) / norm->deltaTime);
}

/* Finds the key at or before t, starting from the cached index. */
template <class K, class T> int binarySearch(AnimCurveData<K> *data, float t, unsigned short *cache)
{
    short n = data->numKeys;
    int i = *cache;
    int last = n - 1;
    int next;
    int lo;
    int hi;
    int mid;
    K *keys;
    T st;

    if (i >= last)
        i = n >> 1;
    animCurveSearchTime(data, t, st);
    keys = data->keys;
    next = i + 1;
    if (keys[next].time <= st) {
        lo = i + 2;
        if (st < keys[lo].time) {
            i = next;
        found:
            *cache = i;
            return i;
        }
        hi = last;
        if (i == 0)
            mid = n - 2;
        else
            mid = (lo + hi) >> 1;
    } else {
        if (!(st < keys[i].time))
            return i;
        hi = i - 1;
        if (keys[hi].time <= st) {
            i = hi;
            goto found;
        }
        lo = 0;
        if (i == n - 2)
            mid = 1;
        else
            mid = hi >> 1;
    }
    while (hi - lo >= 2) {
        if (st < keys[mid].time)
            hi = mid;
        else
            lo = mid;
        mid = (lo + hi) >> 1;
    }
    *cache = lo;
    return lo;
}
template <class K> inline int animCurveSearch(AnimCurveData<K> *data, float t, unsigned short *cache, float)
{
    return binarySearch<K, float>(data, t, cache);
}
template <class K> inline int animCurveSearch(AnimCurveData<K> *data, float t, unsigned short *cache, unsigned short)
{
    return binarySearch<K, int>(data, t, cache);
}
template <class K> inline int animCurveSearch(AnimCurveData<K> *data, float t, unsigned short *cache, unsigned char)
{
    return binarySearch<K, int>(data, t, cache);
}

/* Generic key access (keyed curves); linear curves have evenly spaced keys and overload these. */
template <class K> inline float keyTime(AnimCurveData<K> *data, int i)
{
    return animCurveTime(data, data->keys[i].time);
}
template <class K> inline float keyValue(AnimCurveData<K> *data, int i)
{
    return animCurveValue(data, data->keys[i].value);
}
template <class K> inline int findKey(AnimCurveData<K> *data, float t, unsigned short *cache)
{
    return animCurveSearch(data, t, cache, data->keys[0].time);
}

template <class T> inline float keyTime(AnimCurveData<AnimKeyLinear<T> > *data, int i)
{
    AnimNormData *norm = animCurveNorm(data);

    return (float)i * norm->deltaTime + norm->baseTime;
}
template <class T> inline int findKey(AnimCurveData<AnimKeyLinear<T> > *data, float t, unsigned short *)
{
    AnimNormData *norm = animCurveNorm(data);

    return (int)((t - norm->baseTime) / norm->deltaTime);
}

/* Value between key i and key i + 1. */
template <class T> float evaluateBetweenKeyframes(AnimCurveData<AnimKeyLinear<T> > *data, float t, int i)
{
    AnimNormData *norm = animCurveNorm(data);
    float a = animCurveValue(data, data->keys[i].value);
    float b = animCurveValue(data, data->keys[i + 1].value);

    return a + (b - a) * ((t - norm->baseTime) / norm->deltaTime - (float)i);
}
template <class T> float evaluateBetweenKeyframes(AnimCurveData<AnimKeyStepped<T> > *data, float, int i)
{
    return animCurveValue(data, data->keys[i].value);
}
template <class T, class S> float evaluateBetweenKeyframes(AnimCurveData<AnimKeyHermite<T, S> > *data, float t, int i)
{
    AnimKeyHermite<T, S> *k = &data->keys[i];
    float s = t - data->keys[i].time;

    return s * (s * (s * k->coef[0] + k->coef[1]) + k->coef[2]) + data->keys[i].value;
}
template <class T, class S> float evaluateBetweenKeyframes(AnimCurveData<AnimKeySmooth<T, S> > *data, float t, int i)
{
    float t0 = animCurveTime(data, data->keys[i].time);
    float t1 = animCurveTime(data, data->keys[i + 1].time);
    float v0 = animCurveValue(data, data->keys[i].value);
    float v1 = animCurveValue(data, data->keys[i + 1].value);
    float dv = v1 - v0;
    float dt = t1 - t0;
    float tan0 = animCurveTangent(data->keys[i].tangent);
    float tan1 = animCurveTangent(data->keys[i + 1].tangent);
    float inv = 1.0f / dt;
    float m0 = tan0 * dt;
    float m1 = tan1 * dt;
    float inv2 = inv * inv;
    float s = t - t0;

    return s * (s * (s * ((m0 + m1 - (dv + dv)) * (inv2 * inv)) + (dv + dv + dv - (m0 + m0 + m1)) * inv2) + tan0) + v0;
}
template <class T, class S> float evaluateBetweenKeyframes(AnimCurveData<AnimKeyBroken<T, S> > *data, float t, int i)
{
    float t0 = animCurveTime(data, data->keys[i].time);
    float t1 = animCurveTime(data, data->keys[i + 1].time);
    float v0 = animCurveValue(data, data->keys[i].value);
    float v1 = animCurveValue(data, data->keys[i + 1].value);
    float dt = t1 - t0;
    float dv = v1 - v0;
    float m0 = animCurveTangent(data->keys[i].tangent[0]);
    float m1 = animCurveTangent(data->keys[i].tangent[1]);
    float u = (t - t0) / dt;

    return u * (u * (u * (m0 + m1 - (dv + dv)) + (dv + dv + dv - (m0 + m0 + m1))) + m0) + v0;
}

/* Evaluates a curve at t, applying its pre/post infinity outside the keyed range. */
template <class K> float animCurveEval(AnimCurveData<K> *data, float t, unsigned short *cache)
{
    float start = keyTime(data, 0);
    int last = (short)data->numKeys - 1;
    float end = keyTime(data, last);
    float offset = 0.0f;
    float range;
    float d;
    long n;

    if (t < start) {
        d = start - t;
        range = end - start;
        switch (data->header.preInfinity) {
        case kConstant:
            return keyValue(data, 0);
        case kLinear:
            return 0.0f;
        case kCyclePlusOffset:
            n = (long)(d / range) + 1;
            t += (float)n * range;
            offset = (keyValue(data, 0) - keyValue(data, last)) * (float)n;
            break;
        case kCycle:
            t += (float)((long)(d / range) + 1) * range;
            break;
        case kOscillate:
            n = (long)(d / range);
            if ((int)(n & 1))
                t += (float)(n + 1) * range;
            else
                t = d - (float)n * range;
            break;
        }
    } else if (end <= t) {
        range = end - start;
        d = t - end;
        switch (data->header.postInfinity) {
        case kConstant:
            return keyValue(data, last);
        case kLinear:
            return 0.0f;
        case kCyclePlusOffset:
            n = (long)(d / range) + 1;
            t -= (float)n * range;
            offset = (keyValue(data, last) - keyValue(data, 0)) * (float)n;
            break;
        case kCycle:
            t -= (float)((long)(d / range) + 1) * range;
            break;
        case kOscillate:
            n = (long)(d / range);
            if ((int)(n & 1))
                t -= (float)(n + 1) * range;
            else
                t = (end - t) + (float)n * range;
            break;
        }
    }
    return evaluateBetweenKeyframes(data, t, findKey(data, t, cache)) + offset;
}

/* Retail emission order (the evaluators are referenced by linearChar, smoothChar and the evaluator table). */
template float animCurveEval<AnimKeyLinear<unsigned char> >(AnimCurveData<AnimKeyLinear<unsigned char> > *, float, unsigned short *);
template float animCurveEval<AnimKeySmooth<unsigned char, short> >(AnimCurveData<AnimKeySmooth<unsigned char, short> > *, float, unsigned short *);
template float animCurveEval<AnimKeyHermite<float, float> >(AnimCurveData<AnimKeyHermite<float, float> > *, float, unsigned short *);
template float animCurveEval<AnimKeyBroken<float, float> >(AnimCurveData<AnimKeyBroken<float, float> > *, float, unsigned short *);
template float animCurveEval<AnimKeyBroken<unsigned short, short> >(AnimCurveData<AnimKeyBroken<unsigned short, short> > *, float, unsigned short *);
template float animCurveEval<AnimKeyBroken<unsigned char, short> >(AnimCurveData<AnimKeyBroken<unsigned char, short> > *, float, unsigned short *);
template float animCurveEval<AnimKeyLinear<float> >(AnimCurveData<AnimKeyLinear<float> > *, float, unsigned short *);
template float animCurveEval<AnimKeyLinear<unsigned short> >(AnimCurveData<AnimKeyLinear<unsigned short> > *, float, unsigned short *);
template float animCurveEval<AnimKeySmooth<float, float> >(AnimCurveData<AnimKeySmooth<float, float> > *, float, unsigned short *);
template float animCurveEval<AnimKeySmooth<unsigned short, short> >(AnimCurveData<AnimKeySmooth<unsigned short, short> > *, float, unsigned short *);
template float animCurveEval<AnimKeyStepped<float> >(AnimCurveData<AnimKeyStepped<float> > *, float, unsigned short *);
template float animCurveEval<AnimKeyStepped<unsigned short> >(AnimCurveData<AnimKeyStepped<unsigned short> > *, float, unsigned short *);
template float animCurveEval<AnimKeyStepped<unsigned char> >(AnimCurveData<AnimKeyStepped<unsigned char> > *, float, unsigned short *);
#else
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", binarySearch__H2Zt14AnimKeyStepped1ZfZf_Pt13AnimCurveData1ZX01fPUs_i);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", evaluateBetweenKeyframes__H1ZUc_Pt13AnimCurveData1Zt13AnimKeyLinear1ZX01fi_f);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", animCurveEval__H1Zt13AnimKeyLinear1ZUc_Pt13AnimCurveData1ZX01fPUs_f);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", binarySearch__H2Zt13AnimKeySmooth2ZUcZsZi_Pt13AnimCurveData1ZX01fPUs_i);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", evaluateBetweenKeyframes__H2ZUcZs_Pt13AnimCurveData1Zt13AnimKeySmooth2ZX01ZX11fi_f);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", animCurveEval__H1Zt13AnimKeySmooth2ZUcZs_Pt13AnimCurveData1ZX01fPUs_f);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", binarySearch__H2Zt14AnimKeyHermite2ZfZfZf_Pt13AnimCurveData1ZX01fPUs_i);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", evaluateBetweenKeyframes__H2ZfZf_Pt13AnimCurveData1Zt14AnimKeyHermite2ZX01ZX11fi_f);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", animCurveEval__H1Zt14AnimKeyHermite2ZfZf_Pt13AnimCurveData1ZX01fPUs_f);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", binarySearch__H2Zt13AnimKeyBroken2ZfZfZf_Pt13AnimCurveData1ZX01fPUs_i);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", evaluateBetweenKeyframes__H2ZfZf_Pt13AnimCurveData1Zt13AnimKeyBroken2ZX01ZX11fi_f);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", animCurveEval__H1Zt13AnimKeyBroken2ZfZf_Pt13AnimCurveData1ZX01fPUs_f);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", binarySearch__H2Zt13AnimKeyBroken2ZUsZsZi_Pt13AnimCurveData1ZX01fPUs_i);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", evaluateBetweenKeyframes__H2ZUsZs_Pt13AnimCurveData1Zt13AnimKeyBroken2ZX01ZX11fi_f);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", animCurveEval__H1Zt13AnimKeyBroken2ZUsZs_Pt13AnimCurveData1ZX01fPUs_f);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", binarySearch__H2Zt13AnimKeyBroken2ZUcZsZi_Pt13AnimCurveData1ZX01fPUs_i);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", evaluateBetweenKeyframes__H2ZUcZs_Pt13AnimCurveData1Zt13AnimKeyBroken2ZX01ZX11fi_f);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", animCurveEval__H1Zt13AnimKeyBroken2ZUcZs_Pt13AnimCurveData1ZX01fPUs_f);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", evaluateBetweenKeyframes__H1Zf_Pt13AnimCurveData1Zt13AnimKeyLinear1ZX01fi_f);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", animCurveEval__H1Zt13AnimKeyLinear1Zf_Pt13AnimCurveData1ZX01fPUs_f);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", evaluateBetweenKeyframes__H1ZUs_Pt13AnimCurveData1Zt13AnimKeyLinear1ZX01fi_f);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", animCurveEval__H1Zt13AnimKeyLinear1ZUs_Pt13AnimCurveData1ZX01fPUs_f);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", binarySearch__H2Zt13AnimKeySmooth2ZfZfZf_Pt13AnimCurveData1ZX01fPUs_i);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", evaluateBetweenKeyframes__H2ZfZf_Pt13AnimCurveData1Zt13AnimKeySmooth2ZX01ZX11fi_f);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", animCurveEval__H1Zt13AnimKeySmooth2ZfZf_Pt13AnimCurveData1ZX01fPUs_f);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", binarySearch__H2Zt13AnimKeySmooth2ZUsZsZi_Pt13AnimCurveData1ZX01fPUs_i);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", evaluateBetweenKeyframes__H2ZUsZs_Pt13AnimCurveData1Zt13AnimKeySmooth2ZX01ZX11fi_f);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", animCurveEval__H1Zt13AnimKeySmooth2ZUsZs_Pt13AnimCurveData1ZX01fPUs_f);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", evaluateBetweenKeyframes__H1Zf_Pt13AnimCurveData1Zt14AnimKeyStepped1ZX01fi_f);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", animCurveEval__H1Zt14AnimKeyStepped1Zf_Pt13AnimCurveData1ZX01fPUs_f);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", binarySearch__H2Zt14AnimKeyStepped1ZUsZi_Pt13AnimCurveData1ZX01fPUs_i);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", evaluateBetweenKeyframes__H1ZUs_Pt13AnimCurveData1Zt14AnimKeyStepped1ZX01fi_f);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", animCurveEval__H1Zt14AnimKeyStepped1ZUs_Pt13AnimCurveData1ZX01fPUs_f);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", binarySearch__H2Zt14AnimKeyStepped1ZUcZi_Pt13AnimCurveData1ZX01fPUs_i);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", evaluateBetweenKeyframes__H1ZUc_Pt13AnimCurveData1Zt14AnimKeyStepped1ZX01fi_f);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", animCurveEval__H1Zt14AnimKeyStepped1ZUc_Pt13AnimCurveData1ZX01fPUs_f);
#endif
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", __static_initialization_and_destruction_0_001F6070);
INCLUDE_ASM("asm/nonmatchings/common/AnimCurve", _GLOBAL_$I$linearChar);
