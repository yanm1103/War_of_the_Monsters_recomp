#include "common.h"

/* Matrices are float[4][4], row-major, translation in the fourth column. */
void mathfUnitMatrix(float (*m)[4]);
void mathfMulMatrix(float (*d)[4], float (*a)[4], float (*b)[4]);

INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfMulVec__FPA3_fP8_fvectorT1);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfMulVec4x4__FPA3_fP8_fvectorT1);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfMulTransVecOld__FPA3_fP8_fvectorT1);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfMulTransVec4x4__FPA3_fP8_fvectorT1);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfMulMatrix__FPA3_fN20);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfMulMatrixTP__FPA3_fN20);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfMulMatrix3x3__FPA3_fN20);
/* d = a * transpose(b) on the 3x3 part; the fourth row and column are copied from a. */
void mathfMulMatrixTP3x3(float (*d)[4], float (*a)[4], float (*b)[4])
{
    int i, j;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++)
            d[i][j] = a[i][0] * b[j][0] + a[i][1] * b[j][1] + a[i][2] * b[j][2];
    }
    for (i = 0; i < 4; i++) {
        d[i][3] = a[i][3];
        d[3][i] = a[3][i];
    }
}
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfAddMatrix3x3__FPA3_fN20);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfAddMatrixTP3x3__FPA3_fN20);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfSubMatrix3x3__FPA3_fN20);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfSubMatrixTP3x3__FPA3_fN20);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfScaleMatrix3x3__FPA3_fT0f);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfVectorCrossUp__FP8_fvectorT0);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfVectorCrossRight__FP8_fvectorT0);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfPlaneTest__FP6_planeP8_fvector);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfNormalizeColumns__FPA3_f);
void mathfTransposeMatrix(float (*d)[4], float (*s)[4])
{
    int i, j;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++)
            d[i][j] = s[j][i];
    }
}
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfInverseMatrix__FPA3_fT0);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfRotAxisToMatrix__FPA3_fP8_fvector);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfRotAxisToMatrix__FPA3_fP8_fvectorff);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfMatricesToRotAxis__FP8_fvectorPA3_fT1);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfMatrixToRotAxis__FP8_fvectorPA3_f);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfCopyVectorNotAligned__FP8_fvectorT0);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfScaleVectorNotAligned__FP8_fvectorT0f);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfCopyMatrixNotAligned__FPA3_fT0);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfCopyMatrixNoTrans__FPA3_fT0);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfUnitMatrix__FPA3_f);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfRotMatrixRPH__FPA3_fP8_fvector);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfRotMatrixPRH__FPA3_fP8_fvector);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfRotMatrixRPHNoTrans__FPA3_fP8_fvector);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfHPtoVector__FP8_fvectorff);
/* Builds the projection matrices, like sceVu0ViewScreenMatrix but with the translation in the fourth column:
 *   screen: view space to screen (scrz = eye-to-screen distance, ax/ay = axis scales, cx/cy = screen center,
 *           zmin..zmax = Z buffer range for depths nearz..farz)
 *   clip:   view space to the clip cube for a 1024-pixel half-width guard band
 *   fov:    view space to the clip cube for the visible halfW x halfH rectangle */
#ifdef NON_MATCHING
/* 8/127, same size: float register allocation and scheduling */
void mathfViewScreenMatrix(float (*screen)[4], float (*clip)[4], float (*fov)[4], float scrz, float ax, float ay,
                           float cx, float cy, float zmin, float zmax, float nearz, float farz, float halfW, float halfH)
{
    float persp[4][4];
    float scale[4][4];
    float nf = farz * nearz;
    float az = nf * (-zmin + zmax) / (-nearz + farz);
    float cz = (-zmax * nearz + zmin * farz) / (-nearz + farz);
    float clipW = nearz * 1024.0f / scrz;
    float twoN = nearz + nearz;
    float fa, fb;

    mathfUnitMatrix(persp);
    persp[0][0] = scrz;
    persp[1][1] = scrz;
    persp[2][2] = 0.0f;
    persp[2][3] = 1.0f;
    persp[3][2] = 1.0f;
    persp[3][3] = 0.0f;
    mathfUnitMatrix(scale);
    scale[0][0] = ax;
    scale[1][1] = ay;
    scale[2][2] = az;
    scale[0][3] = cx;
    scale[1][3] = cy;
    scale[2][3] = cz;
    mathfMulMatrix(screen, scale, persp);

    fa = (farz + nearz) / (farz - nearz);
    fb = nf * -2.0f / (farz - nearz);
    mathfUnitMatrix(clip);
    clip[0][0] = twoN / (clipW + clipW);
    clip[1][1] = twoN / (clipW + clipW);
    clip[2][2] = fa;
    clip[2][3] = fb;
    clip[3][2] = 1.0f;
    clip[3][3] = 0.0f;

    mathfUnitMatrix(fov);
    fov[0][0] = twoN / (nearz * halfW / scrz + nearz * halfW / scrz);
    fov[1][1] = twoN / (nearz * halfH / scrz + nearz * halfH / scrz);
    fov[2][2] = fa;
    fov[2][3] = fb;
    fov[3][2] = 1.0f;
    fov[3][3] = 0.0f;
}
#else
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfViewScreenMatrix__FPA3_fN20fffffffffff);
#endif
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfDumpMatrix__FPA3_f);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfManhatDist__FP8_fvectorT0);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfManhatDist2D__FP8_fvectorT0);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfRPHFromMatrix__FPA3_fP8_fvector);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfPRHFromMatrix__FPA3_fP8_fvector);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfRotationFromPointToPoint__FP8_fvectorN20);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfMatrixFromPointToPoint__FPA3_fP8_fvectorT1);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfRotationFromVector__FP8_fvectorT0);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfHeadingFromPointToPoint__FP8_fvectorT0);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfHeadingFromVector__FP8_fvector);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfPitchFromPointToPoint__FP8_fvectorT0);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfPitchFromVector__FP8_fvector);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfMatrixFromNormal__FPA3_fP8_fvector);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfDecomposeVector__FP8_fvectorN30);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfClosestApproach__FR8_fvectorN30);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfClosestApproach__FR8_fvectorN30fRf);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfOrthonormalize__FPA3_f);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfOrthonormalizeOverTime__FPA3_fPi);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfRandfND__Fff);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfRandND__Fii);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfRandf__Fff);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfRand__Fii);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfRandVector__FP8_fvectorf);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfTransform3dTo2d__FiP8_fvectorT1);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfRandInit__Fii);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfRotAxisToQuaternion__FP8_fvectorT0f);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfRotAxisToQuaternion__FP8_fvectorT0);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfAxisAngleToQuaternion__FP8_fvectorT0);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfRPHToQuaternion__FP8_fvectorT0);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfRotationArcQuaternion__FP8_fvectorN20);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfNormalizeQuaternion__FP8_fvectorT0);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfNormalizeQuaternionFromW__FP8_fvectorT0);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfConcatQuaternions__FP8_fvectorN20);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfDeConcatQuaternions__FP8_fvectorN20);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfQuaternionToUpVector__FP8_fvectorT0);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfQuaternionToForwardVector__FP8_fvectorT0);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfQuaternionToMatrix4x4__FPA3_fP8_fvector);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfMatrixToQuaternion__FP8_fvectorPA3_f);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfUnitizeQuaternion__FP8_fvectorT0f);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfQuaternionMulVec__FP8_fvectorN20);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfQuaternionMulTransVec__FP8_fvectorN20);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfSlerpQuaternion__FP8_fvectorN20f);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfVectorMinMaxGetMac__FP8_fvectorT0);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfApproxSin__Ff);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfApproxSin2__Ff);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfApproxASin2__Ff);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfApproxCos__Ff);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfApproxCos2__Ff);
INCLUDE_ASM("asm/nonmatchings/common/mathf", mathfApproxACos2__Ff);
INCLUDE_ASM("asm/nonmatchings/common/mathf", __static_initialization_and_destruction_0_0020F130);
INCLUDE_ASM("asm/nonmatchings/common/mathf", _GLOBAL_$I$gRand);
