#include "common.h"
#include "hieri_types.h"
#include "cs_pool.h"

struct _todInfo14;

/* Screen layouts (only the first three are known: full screen and the two split-screen halves). */
enum _viewports { VIEWPORT_0, VIEWPORT_1, VIEWPORT_2 };

/* Screen rectangle and field of view of a viewport (0xC0 bytes). */
struct _viewDef {
    float fovH;            /* 0x00 */
    unsigned short width;  /* 0x04 */
    unsigned short height; /* 0x06 */
    short bufW;            /* 0x08: draw buffer size given to ieGsSetDefDBuffDc */
    short bufH;            /* 0x0A */
    unsigned short x;      /* 0x0C: top-left corner on screen */
    unsigned short y;      /* 0x0E */
    float screenParam[2];  /* 0x10: passed through to mathfViewScreenMatrix */
    int centerX;           /* 0x18 */
    int centerY;           /* 0x1C */
    float fovV;            /* 0x20 */
    char pad24[0x30 - 0x24];
    float fovNorms[2][4][4]; /* 0x30: frustum side planes in view space (two sets of four) */
    float fovDegrees;      /* 0xB0: horizontal field of view, set by viewSetFov */
    char padB4[0xC0 - 0xB4];
};
typedef char _size__viewDef[sizeof(_viewDef) == 0xC0 ? 1 : -1];

/* One view (player camera): its coordinate system and screen rectangle. */
struct _viewInfo {
    CS *cs;        /* 0x00 */
    _viewDef *def; /* 0x04 */
    int viewport;  /* 0x08: _viewports value */
    int prevViewport; /* 0x0C */
    CS *ownCs;     /* 0x10: the view's own camera (cs can point at another view's) */
};

/* GS RGBAQ register value (color bytes, Q = 1.0 in the high word). */
union GsRgbaq {
    unsigned long rgbaq;
    unsigned char c[8];
};

/* Per-view double-buffered draw state (mostly not decoded yet). */
struct _viewDb {
    char pad0[0xC0];
    unsigned long scissor0;  /* 0x0C0: GS SCISSOR, buffer 0 */
    char padC8[0x140 - 0xC8];
    unsigned long scissor0b; /* 0x140: GS SCISSOR, buffer 0, second register set */
    char pad148[0x1B0 - 0x148];
    GsRgbaq bgColor0; /* 0x1B0: clear color, buffer 0 */
    char pad1B8[0x230 - 0x1B8];
    unsigned long scissor1;  /* 0x230: GS SCISSOR, buffer 1 */
    char pad238[0x2B0 - 0x238];
    unsigned long scissor1b; /* 0x2B0: GS SCISSOR, buffer 1, second register set */
    char pad2B8[0x320 - 0x2B8];
    GsRgbaq bgColor1; /* 0x320: clear color, buffer 1 */
    char pad2[0x360 - 0x328];
};
/* GS SCISSOR register for a viewport: x0, x1 (inclusive), y0, y1 in 16-bit fields. */
#define VIEW_SCISSOR(d)                                                                                              \
    ((unsigned long)(d)->x | ((unsigned long)((d)->x + (d)->width - 1) << 16) | ((unsigned long)(d)->y << 32) |    \
     ((unsigned long)((d)->y + (d)->height - 1) << 48))

/* GS helper class from the engine library (hardware side, kept as asm). */
struct IncognitoEntertainmentGS {
    struct ieGsDBuffDc;
    static void ieGsInitAA(bool on);
    static void ieGsSetDefDBuffDc(ieGsDBuffDc *dc, short interlace, short w, short h, short psm, short ztest, short zpsm);
};
/* bgColor1 seen from bgColor0, as retail addresses it */
#define VIEW_BG_STRIDE ((0x320 - 0x1B0) / sizeof(GsRgbaq))
typedef char _size__viewDb[sizeof(_viewDb) == 0x360 ? 1 : -1];

/* A 4x4 matrix copied as one 16-byte aligned block. */
struct Matrix16 {
    FVECTOR row[4];
};

extern _viewInfo viewInfo[5];
extern _worldctx worldCtx[5];
extern _viewDb viewDb[5];
extern int viewNumViews;
extern int viewCurView;
extern int gUseUnifiedView;
extern float s_viewAmbientVolBlend[4];
extern float s_viewAmbientVolRed[4];
extern float s_viewAmbientVolGreen[4];
extern float s_viewAmbientVolBlue[4];
extern int viewBgR, viewBgG, viewBgB, viewBgA;
extern int viewTODBgR, viewTODBgG, viewTODBgB, viewTODBgA;
extern FMATRIX worldToScreenMat[5];
extern FMATRIX viewScreenMats[5];
extern _viewDef viewDef[];
extern float D_0025B238[]; /* lighting block shared with tod: ambient color at [12..14] */
extern FMATRIX viewShadWorldToScrMat;
extern int viewRearLargeActive;
extern int viewRearViewConfig;
extern int viewAmbientChanged;
extern float viewFovH;
extern float viewAmbientRed, viewAmbientGreen, viewAmbientBlue;
extern float viewAnimAmbientRed, viewAnimAmbientGreen, viewAnimAmbientBlue;
extern int viewOddScan __asm__("D_006F8DF0");
int todActive(void);
void todInit(void);
void viewSetFov(int view, float h, float v);
void viewCreate(_viewports vp, int view);
_viewDef *viewGetDef(int view);
void todSetSkyEntry(HierHead *node);
__asm__("#SNFIX_SMALL viewCurView");
__asm__("#SNFIX_SMALL gUseUnifiedView");

void todGetTOD(int *on, float *minutesPerSecond, _todInfo14 *out);
void todSetTOD(int on, float minutesPerSecond, void *data, float version);
void mathfRotMatrixRPH(float (*mat)[4], _fvector *rph);
void mathfMulMatrix(float (*dst)[4], float (*a)[4], float (*b)[4]);
void vu0MulMatrixTP3x3(float (*dst)[4], float (*a)[4], float (*b)[4]);
void viewApplySwap(float (*dst)[4], float (*src)[4]);
void viewStoreNorms1InVu0(void);
void viewStoreNorms2InVu0(void);
extern FMATRIX viewShadRecMat;
int viewInFOV(int view, _fvector *a, _fvector *b);
float fogGetFarClipRange(void);
_lightenv *lightGetEnv(int i);
extern "C" float atanf(float);
void viewComputeNormal(_fvector *out, _fvector *p0, _fvector *p1, _fvector *p2);
void mathfTransposeMatrix(float (*dst)[4], float (*src)[4]);
void mathfViewScreenMatrix(float (*screen)[4], float (*clip)[4], float (*fov)[4], float scrz, float ax, float ay,
                           float p0, float p1, float nearZ, float zMax, float f1, float f2, float cx, float cy);
extern float viewDegrees;
extern FMATRIX viewClipMats[5];
extern FMATRIX viewFovMats[5];
void viewGetBgColor(int &r, int &g, int &b, int &a);
void viewSetBgColor(unsigned long r, unsigned long g, unsigned long b, unsigned long a);

/* Puts a view on a screen layout: gives it a camera if it has none, sets up its double buffer and scissor, and
 * applies the viewport's field of view. */
void viewCreate(_viewports vp, int view)
{
    _viewInfo *info = &viewInfo[view];
    _viewDef *def;
    _viewDb *db;
    int r, g, b, a;

    info->prevViewport = info->viewport;
    if (info->cs == 0)
        info->cs = CsPool::csActivate();
    info->viewport = vp;
    info->ownCs = info->cs;
    info->def = def = &viewDef[vp];
    worldCtx[view].lightEnv = lightGetEnv(0);
    db = &viewDb[view];
    IncognitoEntertainmentGS::ieGsInitAA(false);
    viewGetBgColor(r, g, b, a);
    IncognitoEntertainmentGS::ieGsSetDefDBuffDc((IncognitoEntertainmentGS::ieGsDBuffDc *)db, vp != 4, def->bufW,
                                                def->bufH, 2, 0x31, 1);
    viewSetBgColor(r, g, b, a);
    db->scissor0 = VIEW_SCISSOR(def);
    db->scissor1 = VIEW_SCISSOR(def);
    db->scissor0b = VIEW_SCISSOR(def);
    db->scissor1b = VIEW_SCISSOR(def);
    viewSetFov(view, def->fovH, def->fovV);
}
INCLUDE_ASM("asm/nonmatchings/common/view", viewUpdate__Fi);
INCLUDE_ASM("asm/nonmatchings/common/view", viewInFOV__FiP8_fvectorT1);
/* viewInFOV with the far plane pulled in so that a sphere of the given radius must fit before the fog's far clip. */
int viewInFOV(int view, _fvector *a, _fvector *b, float radius)
{
    _worldctx *w = &worldCtx[view];
    float farDist = w->fovNormsTP[1][3][2]; /* distance term of the far plane */
    int in;

    w->fovNormsTP[1][3][2] += fogGetFarClipRange() - radius;
    in = viewInFOV(view, a, b);
    w->fovNormsTP[1][3][2] = farDist;
    return in;
}
/* Rebuilds a view's world-space matrices from its camera: the world-to-eye matrix, the frustum planes in world
 * space (also loaded into VU0) and the world-to-screen matrices for the view and for the shadow pass. */
void viewShadowUpdate(int view)
{
    _viewInfo *info = &viewInfo[view];
    _worldctx *w = &worldCtx[view];
    float (*camMat)[4] = info->cs->mat;

    viewApplySwap(w->weMat, camMat);
    vu0MulMatrixTP3x3(w->fovNorms[0], info->def->fovNorms[0], camMat);
    viewStoreNorms1InVu0();
    vu0MulMatrixTP3x3(w->fovNorms[1], info->def->fovNorms[1], camMat);
    viewStoreNorms2InVu0();
    mathfMulMatrix(worldToScreenMat[view], viewScreenMats[view], w->weMat);
    mathfMulMatrix(viewShadWorldToScrMat, viewShadRecMat, w->weMat);
}
INCLUDE_ASM("asm/nonmatchings/common/view", viewStoreNorms1InVu0__Fv);
INCLUDE_ASM("asm/nonmatchings/common/view", viewStoreNorms2InVu0__Fv);
FMATRIX *viewGetWorld2ScreenMat(int view)
{
    return &worldToScreenMat[view];
}
FMATRIX *viewGetShadWorldScreenMat(void)
{
    return &viewShadWorldToScrMat;
}
void viewGetTOD(int *on, float *minutesPerSecond, _todInfo14 *info)
{
    todGetTOD(on, minutesPerSecond, info);
}
void viewSetTOD(int on, float minutesPerSecond, void *data, float version)
{
    todSetTOD(on, minutesPerSecond, data, version);
}
FVECTOR *viewGetRot(int view)
{
    return &viewInfo[view].cs->rot;
}
FMATRIX_16 *viewGetMat(int view)
{
    return &viewInfo[view].cs->mat;
}
FVECTOR *viewGetTrans(int view)
{
    return &viewInfo[view].cs->trans;
}
void viewSetRot(_fvector *rot, int view)
{
    CS *cs = viewInfo[view].cs;

    cs->rot.x = rot->x;
    cs->rot.y = rot->y;
    cs->rot.z = rot->z;
    mathfRotMatrixRPH(cs->mat, &cs->rot);
}
void viewSetRot(float (*mat)[4][4], int view)
{
    CS *cs = viewInfo[view].cs;

    __asm__ volatile("lq $8, %4\n"
                     "lq $9, %5\n"
                     "lq $10, %6\n"
                     "lq $11, %7\n"
                     "sq $8, %0\n"
                     "sq $9, %1\n"
                     "sq $10, %2\n"
                     "sq $11, %3"
                     : "=m"(cs->mat[0][0]), "=m"(cs->mat[1][0]), "=m"(cs->mat[2][0]), "=m"(cs->mat[3][0])
                     : "m"((*mat)[0][0]), "m"((*mat)[1][0]), "m"((*mat)[2][0]), "m"((*mat)[3][0])
                     : "$8", "$9", "$10", "$11");
}
void viewSetTrans(_fvector *trans, int view)
{
    viewInfo[view].cs->trans.x = trans->x;
    viewInfo[view].cs->trans.y = trans->y;
    viewInfo[view].cs->trans.z = trans->z;
}
int viewGetCurView(void)
{
    return viewCurView;
}
CS *viewGetViewCs(int view)
{
    return viewInfo[view].cs;
}
_worldctx *viewGetWorldCtx(int view)
{
    return &worldCtx[view];
}
void viewSetWorldEpNode(HierHead *ep)
{
    int i;

    for (i = 4; i >= 0; i--)
        worldCtx[i].ep = ep;
}
void viewSetAmbientVol(int vol, float blend, float red, float green, float blue)
{
    s_viewAmbientVolBlend[vol] = blend;
    s_viewAmbientVolRed[vol] = red;
    s_viewAmbientVolGreen[vol] = green;
    s_viewAmbientVolBlue[vol] = blue;
}
FVECTOR *viewGetWeTrans(int view)
{
    return &worldCtx[view].eo;
}
FMATRIX *viewGetWeMat(int view)
{
    return &worldCtx[view].weMat;
}
/* Sky groups with id 0 go in the sky slot (and clear the second one), id 4 is the cloud layer. */
void viewSetSkyEntry(HierHead *node, int view)
{
    _worldctx *w = &worldCtx[view];

    if (!w->skyCs)
        w->skyCs = CsPool::csActivate();
    if (!w->skyCs2)
        w->skyCs2 = CsPool::csActivate();
    if (!w->skyClouds)
        w->skyClouds = CsPool::csActivate();
    if (node->id2 == 0) {
        w->skyCs->epNode = node;
        w->skyCs2->epNode = 0;
    }
    if (node->id2 == 4)
        w->skyClouds->epNode = node;
    if (view == 0)
        todSetSkyEntry(node);
}
CS *viewGetSky(int view)
{
    return worldCtx[view].skyCs;
}
void viewSetSkyTrans(_fvector *trans, int view)
{
    _worldctx *w = &worldCtx[view];

    if (w->skyCs) {
        w->skyCs->trans.x = trans->x;
        w->skyCs->trans.y = trans->y;
        w->skyCs->trans.z = trans->z;
    }
    if (w->skyCs2) {
        w->skyCs2->trans.x = trans->x;
        w->skyCs2->trans.y = trans->y;
        w->skyCs2->trans.z = trans->z;
    }
    if (w->skyClouds) {
        w->skyClouds->trans.x = trans->x;
        w->skyClouds->trans.y = trans->y;
        w->skyClouds->trans.z = trans->z;
    }
}
/* layer 0: sky, 1: second sky (TOD cross-fade), 2: clouds; set for every view. */
void viewSetSkyNode(HierHead *node, int layer)
{
    int i;

    if (layer == 0) {
        for (i = 0; i < 5; i++) {
            if (worldCtx[i].skyCs)
                worldCtx[i].skyCs->epNode = node;
        }
    } else if (layer == 1) {
        for (i = 0; i < 5; i++) {
            if (worldCtx[i].skyCs2)
                worldCtx[i].skyCs2->epNode = node;
        }
    } else if (layer == 2) {
        for (i = 0; i < 5; i++) {
            if (worldCtx[i].skyClouds)
                worldCtx[i].skyClouds->epNode = node;
        }
    }
}
/* Matrix slots inside the VU1 packets built at boot (row-major copies of the matrices, transposed for VU1). */
extern float D_0025B1A8[4][4];
extern float D_0025B1F0[4][4];
extern float D_0025B298[4][4];
extern float D_0025B798[4][4];
extern float D_0025B7E0[4][4];
#define VIEW_TRANSPOSE(d, s)                                                                                         \
    (d)[0][0] = (s)[0][0], (d)[0][1] = (s)[1][0], (d)[0][2] = (s)[2][0], (d)[0][3] = (s)[3][0];                      \
    (d)[1][0] = (s)[0][1], (d)[1][1] = (s)[1][1], (d)[1][2] = (s)[2][1], (d)[1][3] = (s)[3][1];                      \
    (d)[2][0] = (s)[0][2], (d)[2][1] = (s)[1][2], (d)[2][2] = (s)[2][2], (d)[2][3] = (s)[3][2];                      \
    (d)[3][0] = (s)[0][3], (d)[3][1] = (s)[1][3], (d)[3][2] = (s)[2][3], (d)[3][3] = (s)[3][3]

/* Writes the camera matrices into the VU1 packets (a and b go to two packets each); the fourth matrix and the view
 * index are unused. */
void viewSetVUPacketMat(float (*a)[4], float (*b)[4], float (*c)[4], float (*)[4], int)
{
    VIEW_TRANSPOSE(D_0025B1A8, a);
    VIEW_TRANSPOSE(D_0025B1F0, b);
    VIEW_TRANSPOSE(D_0025B298, c);
    VIEW_TRANSPOSE(D_0025B798, a);
    VIEW_TRANSPOSE(D_0025B7E0, b);
}
/* Copies a matrix exchanging its second and third rows (the new second row negated). */
void viewApplySwap(float (*dst)[4], float (*src)[4])
{
    dst[0][0] = src[0][0];
    dst[0][1] = src[0][1];
    dst[0][2] = src[0][2];
    dst[0][3] = src[0][3];
    dst[1][0] = -src[2][0];
    dst[1][1] = -src[2][1];
    dst[1][2] = -src[2][2];
    dst[1][3] = -src[2][3];
    dst[2][0] = src[1][0];
    dst[2][1] = src[1][1];
    dst[2][2] = src[1][2];
    dst[2][3] = src[1][3];
    dst[3][0] = src[3][0];
    dst[3][1] = src[3][1];
    dst[3][2] = src[3][2];
    dst[3][3] = src[3][3];
}
/* Clear color; written into each view's GS RGBAQ (both buffers) unless time of day drives it. */
void viewSetBgColor(unsigned long r, unsigned long g, unsigned long b, unsigned long a)
{
    int i;

    if (!todActive()) {
        for (i = 0; i < viewNumViews; i++) {
            GsRgbaq *bg = &viewDb[i].bgColor0;

            bg[0].rgbaq = r | g << 8 | b << 16 | a << 24 | 0x3F80000000000000UL;
            bg[VIEW_BG_STRIDE].rgbaq = r | g << 8 | b << 16 | a << 24 | 0x3F80000000000000UL;
        }
    }
    viewBgR = r;
    viewBgG = g;
    viewBgB = b;
    viewBgA = a;
}
void viewSetTODBgColor(unsigned long r, unsigned long g, unsigned long b, unsigned long a)
{
    int i;

    if (todActive()) {
        for (i = 0; i < viewNumViews; i++) {
            GsRgbaq *bg = &viewDb[i].bgColor0;

            bg[0].rgbaq = r | g << 8 | b << 16 | a << 24 | 0x3F80000000000000UL;
            bg[VIEW_BG_STRIDE].rgbaq = r | g << 8 | b << 16 | a << 24 | 0x3F80000000000000UL;
        }
    }
    viewTODBgR = r;
    viewTODBgG = g;
    viewTODBgB = b;
    viewTODBgA = a;
}
void viewGetBgColor(int &r, int &g, int &b, int &a)
{
    r = viewDb[0].bgColor0.c[0];
    g = viewDb[0].bgColor0.c[1];
    b = viewDb[0].bgColor0.c[2];
    a = viewDb[0].bgColor0.c[3];
}
_viewDb *viewGetDb(int view)
{
    return &viewDb[view];
}
int viewGetNumViews(void)
{
    return gUseUnifiedView ? 1 : viewNumViews;
}
void viewSetNumViews(int n)
{
    viewNumViews = n;
}
void viewGetCenter(int view, int *x, int *y)
{
    *x = viewInfo[view].def->centerX;
    *y = viewInfo[view].def->centerY;
}
void viewGetWH(int view, int *w, int *h)
{
    *w = viewInfo[view].def->width;
    *h = viewInfo[view].def->height;
}
FMATRIX *viewGetWorldToScreenMat(int view)
{
    return &worldToScreenMat[view];
}
void viewGetZBuffParams(int view, float *a, float *b)
{
    float (*m)[4] = viewScreenMats[view];

    *a = m[2][2];
    *b = m[2][3];
}
void viewInit(void)
{
    int i;

    for (i = 0; i < 5; i++) {
        viewInfo[i].cs = 0;
        viewInfo[i].def = 0;
        viewInfo[i].viewport = 0;
        viewInfo[i].prevViewport = 0;
    }
    todInit();
    viewRearLargeActive = 0;
    viewRearViewConfig = 1;
    viewAmbientChanged = 0;
}
void viewTweakInit(void)
{
}
void viewTweakSetFov(void)
{
    int i;

    for (i = 0; i < viewNumViews; i++)
        viewSetFov(i, viewFovH, viewGetDef(i)->fovV);
}
void viewSetAmbient(float r, float g, float b)
{
    float *light = D_0025B238;

    viewAmbientRed = r;
    viewAmbientGreen = g;
    viewAmbientBlue = b;
    viewAnimAmbientRed = r * 0.03125f;
    viewAnimAmbientGreen = g * 0.03125f;
    viewAnimAmbientBlue = b * 0.03125f;
    light[12] = r;
    light[13] = g;
    light[14] = b;
    if (r > 0.0f || g > 0.0f || b > 0.0f)
        viewAmbientChanged = 1;
    else
        viewAmbientChanged = 0;
}
void viewGetAmbient(float *r, float *g, float *b)
{
    *r = viewAmbientRed;
    *g = viewAmbientGreen;
    *b = viewAmbientBlue;
}
int viewGetAnimAmbient(float *r, float *g, float *b)
{
    *r = viewAnimAmbientRed;
    *g = viewAnimAmbientGreen;
    *b = viewAnimAmbientBlue;
    return viewAmbientChanged;
}
void viewGetFov(int view, float *h, float *v)
{
    float (*m)[4] = viewScreenMats[view];

    *h = viewInfo[view].def->fovH;
    *v = m[1][1] / *h;
}
/* Sets a view's projection: dist is the eye-to-screen distance in pixels (fovH), aspect the vertical scale (fovV).
 * Rebuilds the frustum side planes from the viewport size (stored transposed, as hierTraverseAsm reads them),
 * the screen/clip/FOV matrices and, for the shadow viewport (6), the shadow projection. */
#ifdef NON_MATCHING
/* 36/245, same size: register allocation and scheduling */
void viewSetFov(int view, float dist, float aspect)
{
    _viewInfo *info = &viewInfo[view];
    _viewDef *def;
    FVECTOR origin;
    FVECTOR a;
    FVECTOR b;
    Matrix16 tmp;
    float deg;

    a.x = 0.0f;
    a.y = 0.0f;
    a.z = 200.0f;
    a.w = 0.0f;
    origin.x = 0.0f;
    origin.y = 0.0f;
    origin.z = 0.0f;
    origin.w = 0.0f;
    def = info->def;
    def->fovH = dist;
    if (info->viewport == 6)
        dist = 1024.0f;

    def->fovNorms[0][0][0] = 0.0f;
    def->fovNorms[0][0][1] = 1.0f;
    def->fovNorms[0][0][2] = 0.0f;
    def->fovNorms[0][0][3] = 0.0f;
    /* right and left planes */
    b.x = def->width >> 1;
    b.y = dist;
    b.z = 0.0f;
    viewComputeNormal((_fvector *)def->fovNorms[0][1], &origin, &a, &b);
    b.x = -(def->width >> 1);
    b.y = dist;
    b.z = 0.0f;
    viewComputeNormal((_fvector *)def->fovNorms[0][2], &origin, &b, &a);
    /* top and bottom planes */
    a.x = -(def->width >> 1);
    a.y = 0.0f;
    a.z = 0.0f;
    b.x = 0.0f;
    b.y = dist;
    b.z = def->height;
    viewComputeNormal((_fvector *)def->fovNorms[1][0], &origin, &a, &b);
    a.x = -(def->width >> 1);
    a.y = 0.0f;
    a.z = 0.0f;
    b.x = 0.0f;
    b.y = dist;
    b.z = -def->height;
    viewComputeNormal((_fvector *)def->fovNorms[1][1], &origin, &b, &a);
    def->fovNorms[1][2][0] = 0.0f;
    def->fovNorms[1][2][1] = -1.0f;
    def->fovNorms[1][2][2] = 0.0f;
    def->fovNorms[1][2][3] = 0.0f;

    if (info->viewport != 6) {
        deg = atanf((def->width >> 1) / dist) * 57.29578f * 2.0f;
        viewDegrees = deg;
        def->fovDegrees = deg;
        viewFovH = dist;
    }
    mathfViewScreenMatrix(viewScreenMats[view], viewClipMats[view], viewFovMats[view], def->fovH, 1.0f, aspect,
                          def->screenParam[0], def->screenParam[1], 0.1f, 16777210.0f, 1.0f, 16384.0f,
                          def->width >> 1, def->height + 16);

    *(Matrix16 *)&tmp = *(Matrix16 *)def->fovNorms[0];
    mathfTransposeMatrix(def->fovNorms[0], (float (*)[4])&tmp);
    *(Matrix16 *)&tmp = *(Matrix16 *)def->fovNorms[1];
    mathfTransposeMatrix(def->fovNorms[1], (float (*)[4])&tmp);

    if (info->viewport == 6) {
        viewShadRecMat[0][0] = def->fovH * (1.0f / 128.0f);
        viewShadRecMat[0][1] = 0.0f;
        viewShadRecMat[0][2] = 0.5f;
        viewShadRecMat[0][3] = 0.0f;
        viewShadRecMat[1][0] = 0.0f;
        viewShadRecMat[1][1] = def->fovH * (1.0f / 128.0f);
        viewShadRecMat[1][2] = 0.5f;
        viewShadRecMat[1][3] = 0.0f;
        viewShadRecMat[2][0] = 0.0f;
        viewShadRecMat[2][1] = 0.0f;
        viewShadRecMat[2][2] = 1.0f;
        viewShadRecMat[2][3] = 0.0f;
        viewShadRecMat[3][0] = 0.0f;
        viewShadRecMat[3][1] = 0.0f;
        viewShadRecMat[3][2] = 1.0f;
        viewShadRecMat[3][3] = 0.0f;
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/common/view", viewSetFov__Fiff);
#endif
INCLUDE_ASM("asm/nonmatchings/common/view", viewComputeNormal__FP8_fvectorN30);
void viewToggleSplitScreen(bool, bool)
{
    if (viewNumViews < 3) {
        if (viewNumViews == 1) {
            viewNumViews = 2;
            viewCreate(VIEWPORT_1, 0);
            viewCreate(VIEWPORT_2, 1);
        } else {
            viewNumViews = 1;
            viewCreate(VIEWPORT_0, 0);
        }
    }
}
#ifdef NON_MATCHING
/* code identical; its switch jump table lands at a different .rodata offset than retail */
int viewGetScreenDisplay(void)
{
    switch (viewInfo[0].viewport) {
    case 0:
    case 3:
    case 4:
    case 5:
    case 6:
        return 0;
    case 1:
    case 2:
        return 1;
    }
    return 0;
}
#else
INCLUDE_ASM("asm/nonmatchings/common/view", viewGetScreenDisplay__Fv);
#endif
/* Grapple camera: view 0 shows view 2's camera on the unified layout, or goes back to its own camera and viewport. */
#ifdef NON_MATCHING
/* 88/103: scheduling only, the viewport load is hoisted above the cs store in the non-unified branch */
void viewGrappleConfig(void)
{
    _viewDef *def;

    if (gUseUnifiedView) {
        def = &viewDef[5];
        viewInfo[0].cs = viewInfo[2].cs;
        viewInfo[0].def = def;
    } else {
        viewInfo[0].cs = viewInfo[0].ownCs;
        viewInfo[0].def = &viewDef[viewInfo[0].viewport];
        def = viewInfo[0].def;
    }
    viewSetFov(0, def->fovH, def->fovV);
    viewDb[0].scissor0 = VIEW_SCISSOR(def);
    viewDb[0].scissor1 = VIEW_SCISSOR(def);
    viewDb[0].scissor0b = VIEW_SCISSOR(def);
    viewDb[0].scissor1b = VIEW_SCISSOR(def);
}
#else
INCLUDE_ASM("asm/nonmatchings/common/view", viewGrappleConfig__Fv);
#endif
_viewDef *viewGetDef(int view)
{
    return viewInfo[view].def;
}
_viewDef *viewGetDef(_viewports vp)
{
    return &viewDef[vp];
}
void viewSetOddEven(int odd)
{
    viewOddScan = odd != 0;
}
int viewIsOddScan(void)
{
    return viewOddScan;
}
