#include "common.h"
#include "memory_stack.h"
#include "hieri_types.h"
#include "vecmath.h"

extern "C" int printf(const char *, ...);
extern "C" int sprintf(char *, const char *, ...);
extern "C" int strcmp(const char *, const char *);
extern "C" void *memset(void *, int, unsigned);
extern "C" void *memcpy(void *, const void *, unsigned);

enum _vramAddrs { VRAM_ADDRS_DUMMY };

struct _hierhead;
struct _animCharInstance;
/* One level of the database traversal stack (0x60 bytes): the siblings still to visit and the transform to restore. */
struct _dbsstack {
    float x, y, z;               /* 0x00: translation (w is not kept) */
    float pad0C;
    float mat[4][4];             /* 0x10 */
    _hierhead **cur;             /* 0x50: next sibling to visit */
    unsigned remaining;          /* 0x54 */
    _animCharInstance *anim;     /* 0x58 */
    int pad5C;
};
static inline void copyMat(float (*dst)[4], float (*src)[4])
{
    int i;

    for (i = 0; i < 16; i++)
        ((float *)dst)[i] = ((float *)src)[i];
}
struct _HierCollisionGrid;
struct HierParticleEmitter;
void mathfUnitMatrix(float (*m)[4]);
void mathfTransposeMatrix(float (*dst)[4], float (*src)[4]);
void mathfMulMatrix(float (*d)[4], float (*a)[4], float (*b)[4]);
void mathfMulMatrix3x3(float (*d)[4], float (*a)[4], float (*b)[4]);
void mathfMulMatrixTP3x3(float (*d)[4], float (*a)[4], float (*b)[4]);
void mathfMulVec(float (*m)[4], _fvector *v, _fvector *out);
void mathfRotMatrixPRH(float (*m)[4], _fvector *rot);
void particleCreateModeledFx(_fvector *pos, HierParticleEmitter *e);
void hdSaveColGrid(_HierCollisionGrid *g);
extern unsigned short g_dbsDisableNodeMask;
/* v.x * row0 + v.y * row1 + v.z * row2 of the matrix (VU0 vmulax/vmadday/vmaddz in retail) */
static inline void mulVecRows(_fvector *out, _fvector *v, float (*m)[4])
{
    out->x = v->x * m[0][0] + v->y * m[1][0] + v->z * m[2][0];
    out->y = v->x * m[0][1] + v->y * m[1][1] + v->z * m[2][1];
    out->z = v->x * m[0][2] + v->y * m[1][2] + v->z * m[2][2];
}
extern _dbsstack dbsStack[];
extern int dbsStackIdx;

int fileReadf(char *name, void *dest);
char *getNextTexLoadAddr(void);
char *getNgpAddr(int i);
char *getGenericName(int i);
int getMaxTexId(int i);
int getMaxTexAddr(int i);
int getMaxResAddr(int i);

extern char D_006F85F0[]; /* "shell" */
extern char D_006F85F8[]; /* "SHELL" */
extern char D_006F8600[]; /* "load" */
extern char D_006F8608[]; /* "LOAD" */
extern char D_006F8610[]; /* "ui" */
extern char D_006F8618[]; /* "UI" */
extern char D_006F8620[]; /* "shella" */
extern char D_006F8628[]; /* "SHELLA" */

#ifdef NON_MATCHING
/* 3/34 words: the matrix copy is 4 lq/sq in retail */
void dbsPush(_hierhead **list, _fvector *pos, unsigned count, float (*m)[4], _animCharInstance *anim)
{
    _dbsstack *e = &dbsStack[dbsStackIdx];

    e->cur = list;
    e->remaining = count;
    e->x = pos->x;
    e->y = pos->y;
    e->anim = anim;
    e->z = pos->z;
    copyMat(e->mat, m);
    if (dbsStackIdx < 0x96)
        dbsStackIdx++;
}
#else
INCLUDE_ASM("asm/nonmatchings/common/dbs", dbsPush__FPP9_hierheadP8_fvectorUiPA3_fP17_animCharInstance);
#endif
#ifdef NON_MATCHING
/* 18/46 words: the matrix copy is 4 lq/sq in retail */
int dbsPop(_dbsstack *out, _hierhead **head)
{
    _dbsstack *e = &dbsStack[dbsStackIdx - 1];

    if (dbsStackIdx > 0) {
        *head = *e->cur;
        out->x = e->x;
        out->y = e->y;
        out->z = e->z;
        out->anim = e->anim;
        copyMat(out->mat, e->mat);
        if (--e->remaining == 0)
            dbsStackIdx--;
        else
            e->cur++;
    } else {
        dbsStackIdx--;
    }
    return dbsStackIdx;
}
#else
INCLUDE_ASM("asm/nonmatchings/common/dbs", dbsPop__FP9_dbsstackPP9_hierhead);
#endif
/* Walks a node tree of the loaded .NGP image depth first with an explicit stack (dbsPush/dbsPop), carrying the accumulated
   translation (st.x/y/z) and rotation (st.mat). `cb` is called for every node that has an object id. The layouts below are read
   straight from the retail code: the node opcode is the low 6 bits of its first word, the id the high 14 bits. */
#define WI(p, o) (*(int *)((char *)(p) + (o)))
#define WU(p, o) (*(unsigned *)((char *)(p) + (o)))
#define WH(p, o) (*(short *)((char *)(p) + (o)))
#define WUH(p, o) (*(unsigned short *)((char *)(p) + (o)))
#define WF(p, o) (*(float *)((char *)(p) + (o)))
#define WP(p, o) ((char *)(p) + (o))

#ifdef NON_MATCHING
/* 18/645 words: untuned, from the m2c draft + asm */
void dbsTraverse(_hierhead **roots, void (*cb)(_hierhead *, _fvector *, float (*)[4]), _fvector *pos)
{
    _dbsstack st;
    float unit[4][4];
    _hierhead *node;
    float (*mat)[4] = st.mat;
    _fvector *sp = (_fvector *)&st;
    char *n;

    dbsStackIdx = 0;
    mathfUnitMatrix(unit);
    dbsPush(roots, pos, 1, unit, 0);
    while (dbsPop(&st, &node) >= 0) {
    again:
        n = (char *)node;
        switch (WU(n, 0) & 0x3F) {
        case 41:
            mathfTransposeMatrix((float (*)[4])WP(n, 0x10), mat);
            *(QwData *)WP(n, 0x50) = *(QwData *)&st;
            if (WU(n, 0) & 0xFFFC0000)
                cb(node, sp, mat);
            break;
        case 40:
            if (WU(n, 0) & 0xFFFC0000)
                cb(node, sp, mat);
            break;
        case 0:
            if ((WH(n, 0x30) >= 0 || WH(n, 0x32) >= 0 || WH(n, 0x34) >= 0 || WH(n, 0x36) >= 0 || WH(n, 0x38) >= 0 || WH(n, 0x3A) >= 0 || WH(n, 0x3C) >= 0) && st.anim == 0) {
                printf("^^^^^^^ Oh no!!! we're in trouble!!!! ^^^^^^^^^\n");
                printf("           Object: %p %d:%d has no character instance context. %d\n", n, (WU(n, 0) >> 18) & 0x3FFF, (WU(n, 0) >> 7) & 0x7FF, 0);
            }
            if (WH(n, 0x30) >= 0 || WH(n, 0x32) >= 0)
                WU(n, 0xC) |= 1u << 11;
            else
                WU(n, 0xC) &= ~(1u << 11);
            if (WU(n, 0) & 0xFFFC0000)
                cb(node, sp, mat);
            break;
        case 1:
            if (g_dbsDisableNodeMask & WUH(n, 0xA)) {
                WU(n, 0) = (WU(n, 0) & ~0x3F) | 9;
            } else if (WUH(n, 8) != 0) {
                unsigned cnt;

                if (WU(n, 0) & 0xFFFC0000)
                    cb(node, sp, mat);
                cnt = WUH(n, 8) - 1;
                node = (_hierhead *)WI(n, 0x20);
                if (cnt != 0)
                    dbsPush((_hierhead **)WP(n, 0x24), sp, cnt, mat, st.anim);
                goto again;
            }
            break;
        case 2: {
            int cnt = WI(n, 4);

            if (cnt != 0) {
                if (WU(n, 0) & 0xFFFC0000)
                    cb(node, sp, mat);
                node = (_hierhead *)WI(n, 0x28);
                if (cnt > 1) {
                    char *p = WP(n, 0x38);
                    int k;

                    for (k = cnt - 1; k != 0; k--, p += 0x10)
                        dbsPush((_hierhead **)p, sp, 1, mat, st.anim);
                }
                goto again;
            }
            break;
        }
        case 3: {
            int cnt;

            /* TRANSLATE: adds `trans` (+0x10) to the accumulated translation before the callback */
            st.x += WF(n, 0x10);
            st.y += WF(n, 0x14);
            st.z += WF(n, 0x18);
            if (WU(n, 0) & 0xFFFC0000)
                cb(node, sp, mat);
            cnt = WI(n, 8);
            if (cnt != 0) {
                node = (_hierhead *)WI(n, 0x1C);
                if (cnt - 1 != 0)
                    dbsPush((_hierhead **)WP(n, 0x20), sp, cnt - 1, mat, st.anim);
                goto again;
            }
            break;
        }
        case 4: {
            _fvector t;
            float save[4][4];
            int cnt;

            t.x = st.x + WF(n, 0x40);
            t.y = st.y + WF(n, 0x44);
            t.z = st.z + WF(n, 0x48);
            mathfMulVec(mat, &t, sp);
            copyMat(save, mat);
            mathfMulMatrixTP3x3(mat, save, (float (*)[4])WP(n, 0x10));
            if (WU(n, 0) & 0xFFFC0000)
                cb(node, sp, mat);
            cnt = WI(n, 8);
            if (cnt != 0) {
                node = (_hierhead *)WI(n, 0x50);
                if (cnt - 1 != 0)
                    dbsPush((_hierhead **)WP(n, 0x54), sp, cnt - 1, mat, st.anim);
                goto again;
            }
            break;
        }
        case 35: {
            int cnt = WI(n, 4);

            if (cnt != 0)
                dbsPush((_hierhead **)WP(n, 0xC), sp, cnt, mat, st.anim);
            break;
        }
        case 17: {
            /* rotation node driven by an animation instance: up to 6 channels pick values from the instance's value table */
            int a3 = -1;
            unsigned char mask = 0;
            int bit = 1;
            float *vals = *(float **)((char *)st.anim + 4);
            float (*mm)[4] = (float (*)[4])(*(char **)((char *)st.anim + 0xC) + (WI(n, 0xC4) << 6));
            short ch;
            _fvector neg;
            float save[4][4];
            unsigned long long flags;
            float *tr;
            int cnt;

            ch = WH(n, 0x12);
            if (ch >= 0) {
                a3 = ch;
                mask = 1;
                bit = 2;
                WF(n, 0x20) = vals[ch];
            }
            ch = WH(n, 0x14);
            if (ch >= 0) {
                a3 = (a3 <= -1) ? ch : a3;
                mask = (mask | bit) & 0xFF;
                bit *= 2;
                WF(n, 0x24) = vals[ch];
            }
            ch = WH(n, 0x16);
            if (ch >= 0) {
                a3 = (a3 <= -1) ? ch : a3;
                mask = (mask | bit) & 0xFF;
                bit *= 2;
                WF(n, 0x28) = vals[ch];
            }
            ch = WH(n, 0x18);
            if (ch >= 0) {
                mask = (mask | bit) & 0xFF;
                a3 = (a3 <= -1) ? ch : a3;
                bit *= 2;
            }
            ch = WH(n, 0x1A);
            if (ch >= 0) {
                mask = (mask | bit) & 0xFF;
                a3 = (a3 <= -1) ? ch : a3;
                bit *= 2;
            }
            ch = WH(n, 0x1C);
            if (ch >= 0) {
                mask = (mask | bit) & 0xFF;
                a3 = (a3 <= -1) ? ch : a3;
            }
            WH(n, 0x1E) = a3;
            *(unsigned char *)WP(n, 0xC1) = mask;
            neg.x = -WF(n, 0x20);
            neg.y = -WF(n, 0x24);
            neg.z = -WF(n, 0x28);
            mathfRotMatrixPRH(mm, &neg);
            flags = *(unsigned long long *)WP(n, 0xC0);
            if (flags & 0x10000) {
                if (flags & 0x20000) {
                    mathfMulMatrix(save, (float (*)[4])WP(n, 0x40), mm);
                    mathfMulMatrix(mm, save, (float (*)[4])WP(n, 0x80));
                } else {
                    mathfMulMatrix(save, (float (*)[4])WP(n, 0x40), mm);
                    copyMat(mm, save);
                }
            } else if (flags & 0x20000) {
                copyMat(save, mm);
                mathfMulMatrix(mm, save, (float (*)[4])WP(n, 0x80));
            }
            tr = (float *)mm + 12;
            vecAdd((_fvector *)tr, (_fvector *)tr, (_fvector *)WP(n, 0x30));
            mathfMulVec(mat, (_fvector *)tr, sp);
            copyMat(save, mat);
            mathfMulMatrixTP3x3(mat, save, mm);
            if (WU(n, 0) & 0xFFFC0000)
                cb(node, sp, mat);
            cnt = WI(n, 4);
            if (cnt != 0) {
                node = (_hierhead *)WI(n, 0xCC);
                if (cnt - 1 != 0)
                    dbsPush((_hierhead **)WP(n, 0xD0), sp, cnt - 1, mat, st.anim);
                goto again;
            }
            break;
        }
        case 6: {
            int cnt;

            if (WU(n, 0) & 0xFFFC0000)
                cb(node, sp, mat);
            node = (_hierhead *)WI(n, 0xC);
            cnt = *(unsigned char *)WP(n, 0xB) - 1;
            if (cnt != 0)
                dbsPush((_hierhead **)WP(n, 0x10), sp, cnt, mat, st.anim);
            goto again;
        }
        case 39:
            cb(node, sp, mat);
            dbsPush((_hierhead **)WP(n, 0xC), sp, 1, mat, st.anim);
            break;
        case 31: {
            int k;

            cb(node, sp, mat);
            if (WUH(n, 0xA) != 0) {
                char *p = WP(n, 0xC);

                k = 0;
                do {
                    k++;
                    dbsPush((_hierhead **)p, sp, 1, mat, st.anim);
                    p += 0xC;
                } while (k < (int)WUH(n, 0xA));
            }
            break;
        }
        case 8: {
            _hierhead **sub = (_hierhead **)WP(n, 8);

            node = (_hierhead *)WI(n, 4);
            dbsPush(sub, sp, 1, mat, st.anim);
            goto again;
        }
        case 13:
            printf("TERRAIN NODES NO LONGER SUPPORTED\n");
            break;
        case 15:
            hdSaveColGrid((_HierCollisionGrid *)node);
            break;
        case 23:
            node = (_hierhead *)WI(n, 4);
            goto again;
        case 25: {
            int cnt = WI(n, 0x28);

            if (cnt != 0)
                dbsPush((_hierhead **)WP(n, 0x2C), sp, cnt, mat, st.anim);
            break;
        }
        case 22:
            if (*(unsigned long long *)WP(n, 0x58) & (1ULL << 35)) {
                _fvector p;
                _fvector world;

                vecAdd(&p, sp, (_fvector *)WP(n, 0x10));
                mulVecRows(&world, &p, mat);
                particleCreateModeledFx(&world, (HierParticleEmitter *)node);
                *(unsigned long long *)WP(n, 0x58) |= 1ULL << 36;
            }
            break;
        case 30: {
            float scale[4][4];
            float save[4][4];
            int cnt;

            mathfUnitMatrix(scale);
            scale[0][0] = WF(n, 0x20);
            scale[1][1] = WF(n, 0x24);
            scale[2][2] = WF(n, 0x28);
            copyMat(save, mat);
            mathfMulMatrix3x3(mat, scale, save);
            if (WU(n, 0) & 0xFFFC0000)
                cb(node, sp, mat);
            cnt = WI(n, 8);
            if (cnt != 0) {
                node = (_hierhead *)WI(n, 0x2C);
                if (cnt - 1 > 0)
                    dbsPush((_hierhead **)WP(n, 0x30), sp, cnt - 1, mat, st.anim);
                goto again;
            }
            break;
        }
        case 11:
            if (WU(n, 0) & 0xFFFC0000)
                cb(node, sp, mat);
            break;
        default:
            break;
        }
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/common/dbs", dbsTraverse__FPP9_hierheadPFP9_hierheadP8_fvectorPA3_f_vP8_fvector);
#endif
/* Relocates the monster image `idx` (mon/<name>.ptr, loaded to a temporary buffer on the memory stack). Images are linked for address
   0xA00000. The pointer file holds four lists, each a count followed by byte offsets into the image (0 = unused):
   0. the image header: words 1..N (N = word 0) are pointers that move by (load address - 0xA00000);
   1. words that are pointers (same shift);
   2. 16-bit texture ids, shifted by the highest id of the previous image;
   3. GS words: TEX0.TBP0 moves by the previous image's texture end; words tagged 0x1B/0x2C/0x24 also move their low 14 bits by its resource end. */
#ifdef NON_MATCHING
/* 48/206 words: untuned, from the m2c draft + asm */
void dbsRelocateViaPtrListFile(int idx, _vramAddrs vram)
{
    char name[0x18];
    int *file;
    char *img;
    int base;
    int size;
    int n1;
    int n2;
    int n3;
    int k;

    if (idx == 0)
        return;
    MemoryStack::global.pushMark();
    file = (int *)(((int)MemoryStack::global.low + 0xF) & ~0xF);
    MemoryStack::global.low = (char *)file + 0xD40;
    memset(name, 0, 0x18);
    base = (int)getNgpAddr(idx) + (int)0xFF600000;
    sprintf(name, "mon/%s.ptr", getGenericName(idx));
    size = fileReadf(name, file);
    if (size < 0)
        printf("Could not read pointer file \"%s\"!!\n", name);
    if (size > 0x30D40)
        printf("Pointer file too big.  Prepare for crash...\n");
    img = getNgpAddr(idx);
    n1 = *(int *)img;
    for (k = 1; k <= n1; k++)
        ((int *)img)[k] += base;
    n1 = file[0];
    for (k = 0; k < n1; k++)
        *(int *)(img + file[1 + k]) += base;
    printf("Finished ptrs\n");
    n2 = file[1 + n1];
    for (k = 0; k < n2; k++) {
        int off = file[2 + n1 + k];
        unsigned short *id = (unsigned short *)(img + off);

        if (off != 0 && *id != 0)
            *id += getMaxTexId(idx - 1);
    }
    printf("Finished texIds\n");
    n3 = file[2 + n1 + n2];
    for (k = 0; k < n3; k++) {
        int off = file[3 + n1 + n2 + k];
        unsigned *w = (unsigned *)(img + off);

        if (off != 0) {
            unsigned lo;

            w[1] = (w[1] & ~(0x3FFFu << 5)) | ((((w[1] >> 5) & 0x3FFF) + (getMaxTexAddr(idx - 1) & 0xFFFF)) & 0x3FFF) << 5;
            lo = w[0] & 0x03F00000;
            if (lo == 0x01B00000 || lo == 0x02C00000 || lo == 0x02400000)
                w[0] = (w[0] & ~0x3FFFu) | (((w[0] & 0x3FFF) + (getMaxResAddr(idx - 1) & 0xFFFF)) & 0x3FFF);
        }
    }
    MemoryStack::global.low = (char *)MemoryStack::global.mark;
    MemoryStack::global.mark = *(int *)MemoryStack::global.mark;
}
#else
INCLUDE_ASM("asm/nonmatchings/common/dbs", dbsRelocateViaPtrListFile__Fi10_vramAddrs);
#endif
INCLUDE_ASM("asm/nonmatchings/common/dbs", D_006F3A58);
/* Relocates the texture base pointers (TEX0.TBP0, 14 bits at bit 37) of the loaded .NGP image of file zero by `vram` / 64.
   The pointer file (lvl/<name>.PTR, shell/<x>.ptr) holds three lists, each a count followed by that many words; the first two
   are skipped here, the third lists byte offsets into the image (0 = unused). */
#ifdef NON_MATCHING
/* 2/196 words: untuned, from the m2c draft + asm (TBP0 patch done on the high word) */
void dbsRelocateFileZero(_vramAddrs vram, bool b)
{
    char name[0x18] = "";
    int *list = (int *)getNextTexLoadAddr();
    char *ngp = getNgpAddr(0);
    int *p;
    int n;

    if (strcmp(getGenericName(0), D_006F85F0) == 0 || strcmp(getGenericName(0), D_006F85F8) == 0)
        sprintf(name, "shell/shell.ptr");
    else if (strcmp(getGenericName(0), D_006F8600) == 0 || strcmp(getGenericName(0), D_006F8608) == 0)
        sprintf(name, "shell/load.ptr");
    else if (strcmp(getGenericName(0), D_006F8610) == 0 || strcmp(getGenericName(0), D_006F8618) == 0)
        sprintf(name, "shell/ui.ptr");
    else if (strcmp(getGenericName(0), D_006F8620) == 0 || strcmp(getGenericName(0), D_006F8628) == 0)
        sprintf(name, "shell/shella.ptr");
    else if (strcmp(getGenericName(0), "preshell") == 0 || strcmp(getGenericName(0), "PRESHELL") == 0)
        sprintf(name, "shell/preshell.ptr");
    else
        sprintf(name, "lvl/%s.PTR", getGenericName(0));
    if (fileReadf(name, list) < 0)
        printf("dbsRelocateFileZero::Could not read pointer file (%s)!!\n", name);
    p = list + list[0] + 1;
    p = p + p[0] + 1;
    n = p[0];
    p++;
    if (n > 0) {
        int delta = (int)vram / 64;

        for (; n != 0; n--, p++) {
            if (*p != 0) {
                unsigned *hi = (unsigned *)(ngp + *p + 4); /* TBP0 is bits 5..18 of the high word of the doubleword */

                *hi = (*hi & ~(0x3FFFu << 5)) | ((((*hi >> 5) & 0x3FFF) + delta) & 0x3FFF) << 5;
            }
        }
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/common/dbs", dbsRelocateFileZero__F10_vramAddrsb);
#endif
