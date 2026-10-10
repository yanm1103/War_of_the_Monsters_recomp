#include "common.h"

/* hieri_types.h has AnimPlayer as a plain struct (from the stabs); this file defines its methods, so it gets the
   class below instead. */
#define AnimPlayer AnimPlayer_stabs
#include "hieri_types.h"
#undef AnimPlayer
#include "memory_stack.h"

class AnimPlayer {
public:
    HierHead head;             /* 0x00 */
    _animCharInstance *animCI; /* 0x04 */
    HierHead *mainTree;        /* 0x08 */
    float *mainTreePureOut;    /* 0x0C */
    AnimBlendNode *activeList; /* 0x10 */
    int numNodes;              /* 0x14 */

    AnimPlayer(_animCharInstance *ci);
    void UpdateAnimations(void);
    int AddBlendToActiveList(AnimBlendNode *node);
    int RemoveBlendFromActiveList(AnimBlendNode *node);
    int IsBlendInActiveList(AnimBlendNode *node);
    HierHead *GetMainTreeNode(void);
    AnimBlendNode *GetActiveList(void);
    int GetNumNodesInActiveList(void);
};

/* What animationGetHandle finds: a character instance and one of its animations (with its control node). */
struct _animHandle {
    _animCharInstance *ci; /* 0x0 */
    AnimControlNode *ctrl; /* 0x4 */
    AnimProcNode *proc;    /* 0x8: procedural node (animationAddProcBlend) */
    short animIdx;         /* 0xC */
};

extern _animmgr *D_00735690[]; /* animation manager of each loaded NGP file */

/* Pool of blend nodes: a stack of free slot indices; numUsed is its top. */
class AnimBlendPool {
public:
    AnimBlendNode nodes[128];
    unsigned char freeList[128];
    unsigned char numUsed;

    void reset(void)
    {
        int i;

        for (i = 127; i >= 0; i--)
            freeList[i] = i;
        numUsed = 0;
    }

    /* Takes a free node (0 when all 128 are in use). */
    AnimBlendNode *alloc(void)
    {
        if (numUsed < 128)
            return &nodes[freeList[numUsed++]];
        return 0;
    }

    /* Gives a node back unless its index is already among the free ones. */
    void release(AnimBlendNode *node)
    {
        unsigned char n;
        unsigned char idx;
        unsigned char *p;

        if (numUsed == 0)
            return;
        idx = node - nodes;
        n = 128 - numUsed;
        p = &freeList[numUsed];
        for (; n; n--, p++)
            if (*p == idx)
                return;
        freeList[--numUsed] = idx;
    }
};

extern MemoryStack D_007329F0; /* scratchpad stack (0x70000000, 16 KB) */
extern AnimBlendPool D_00732A08;

int getNgpFilesLoaded(void);
int timerGetFieldCount(void);
extern "C" int printf(const char *fmt, ...);
void *operator new(unsigned, void *);
/* the dirty masks are read as unaligned 64-bit words (ldl/ldr) */
struct Packed64 { unsigned long v; } __attribute__((packed));

void animationCleanUpTree(HierHead *tree, _animCharInstance *ci);
void animationAddToActiveTree(_animHandle h);
void animationUpdate(HierAnimation *anim, _animCharInstance *ci);
void animationUpdateFromControlNode(AnimControlNode *ctrl, _animCharInstance *ci);
float animationGetCurrentTime(_animHandle h);
void animationPause(_animHandle h);
void animationResume(_animHandle h);
void animationStart(_animHandle h, bool loop);
void animationStartReverse(_animHandle h, bool loop);
void animationSetToBeginning(_animHandle h, bool update);
void animationSetToEnd(_animHandle h, bool update);
void animationSetDirection(_animHandle h, bool forward);
int animationGetDirection(_animHandle h);
void animationSetIterations(_animHandle h, unsigned short n);
void animationSetSpeed(_animHandle h, float speed);
void animationSetToPercent(_animHandle h, float percent, int update);
float animationGetCurrentPercent(_animHandle h);
void animationManager(_animmgr *mgr);
void animationUpdateActiveTree(HierHead *tree, _animCharInstance *ci);
void animationProcessActiveTree(HierHead *tree, _animCharInstance *ci);
float animCurveEvaluate(AnimCurveHeader *curve, float t, unsigned short *key);
void animationProcessTransitionBlend(AnimBlendNode *node, _animCharInstance *ci);
void animationProcessStaticBlend(AnimBlendNode *node, _animCharInstance *ci);
void animationProcessOverrideBlend(AnimBlendNode *node, _animCharInstance *ci);
void animationProcessAdditiveBlend(AnimBlendNode *node, _animCharInstance *ci);
void animationProcessProceeduralBlend(AnimBlendNode *node, _animCharInstance *ci);
void animationEvaluateFromNode(AnimBlendNode *node, _animCharInstance *ci, AnimationOutputBlock **out);
void animationEvaluateToNode(AnimBlendNode *node, _animCharInstance *ci, AnimationOutputBlock **out);
float mathfApproxCos2(float x);
void boundEulerAngle(float *p);
void animationCollapseBlend(AnimBlendNode *node, _animCharInstance *ci);
extern char D_006F3150[]; /* "Could not start a blend of type %d, ---Bad Character Instance(s)
" */
extern int okToBlend;
__asm__("#SNFIX_SMALL okToBlend");
extern int s_blendCurve;
__asm__("#SNFIX_SMALL s_blendCurve");

INCLUDE_ASM("asm/nonmatchings/common/animation", D_006F3150);
void animationInitModifierBlends(_animCharInstance *ci)
{
    AnimPlayer *player = (AnimPlayer *)ci->activeTree;
    char *mem = (char *)(((int)MemoryStack::global.low + 15) & ~15);

    MemoryStack::global.low = mem + (unsigned short)(ci->character->numChannels * 4);
    player->mainTreePureOut = (float *)mem;
}
void animationTransitionInto(_animHandle &h, float time, int reset, int type)
{
    AnimControlNode *target;
    AnimPlayer *player;
    HierHead *cur;
    AnimBlendNode *node;

    if (h.ci == 0 || h.ctrl == 0)
        return;
    target = &h.ci->animCtx[h.animIdx];
    player = (AnimPlayer *)h.ci->activeTree;
    cur = player->GetMainTreeNode();
    if (type != BLEND_TRANSITION && type != BLEND_FREEZETRANS)
        return;
    if (cur == 0 || h.ci->bits.disableCI) {
        animationStart(h, reset);
        return;
    }
    if ((HierHead *)h.ctrl == cur && ((AnimControlNode *)cur)->active)
        return;
    node = D_00732A08.alloc();
    if (node)
        new (node) AnimBlendNode;
    node->blendType = type;
    node->head.opcode = 0x22;
    if (reset)
        animationSetToBeginning(h, 0);
    if (type == BLEND_FREEZETRANS) {
        node->blendValue = animationGetCurrentPercent(h);
        animationSetToPercent(h, node->blendValue, 0);
    }
    animationResume(h);
    if (cur->opcode == 0x21) {
        AnimControlNode *ctrl = (AnimControlNode *)cur;

        if (!ctrl->active) {
            ctrl->active = 1;
            ctrl->startField = timerGetFieldCount() - ctrl->startField;
        }
        node->fromTime.startField = ctrl->startField;
        node->fromTime.startTime = ctrl->startTime;
        node->fromTime.deltaTime = ctrl->deltaTime;
    } else if (cur->opcode == 0x22) {
        AnimBlendNode *blend = (AnimBlendNode *)cur;

        blend->blendTime *= 0.4f;
        blend->blendStartField = ((float)timerGetFieldCount() - blend->blendStartField) * -0.4f + (float)timerGetFieldCount();
        blend->parent = node;
        node->fromTime.startField = blend->toTime.startField;
        node->fromTime.startTime = blend->toTime.startTime;
        node->fromTime.deltaTime = blend->toTime.deltaTime;
    }
    node->blendFrom = cur;
    node->blendTo = (HierHead *)target;
    node->head.id1 = target->head.id1;
    node->head.id2 = target->head.id2;
    node->blendTime = time;
    node->weight = 0.0f;
    node->blendStartField = (float)timerGetFieldCount();
    node->parent = 0;
    node->active = 1;
    node->toTime.startField = target->startField;
    node->toTime.startTime = target->startTime;
    node->toTime.deltaTime = target->deltaTime;
    target->iterations = 0;
    player->mainTree = (HierHead *)node;
    okToBlend = 0;
}
int animationIsTransitioning(_animHandle &h)
{
    if (h.ci) {
        HierHead *tree = ((AnimPlayer *)h.ci->activeTree)->GetMainTreeNode();
        if (tree && tree->opcode == 0x22)
            return 1;
    }
    return 0;
}
int animationIsTargetAnim(_animHandle &h)
{
    HierHead *node;

    if (h.ci) {
        node = ((AnimPlayer *)h.ci->activeTree)->mainTree;
        while (node) {
            if (node->opcode == 0x21)
                return node == (HierHead *)h.ctrl;
            else if (node->opcode == 0x22)
                node = ((AnimBlendNode *)node)->blendTo;
            else
                break;
        }
    }
    return 0;
}
AnimBlendNode *animationAddBlend(_animHandle h, AnimBlendNode *node, int type, float weight, float time, short priority, int reset)
{
    AnimControlNode *ctrl;
    AnimPlayer *player;

    if (h.ci == 0) {
        printf(D_006F3150, type);
        return 0;
    }
    ctrl = h.ctrl;
    if (ctrl == 0)
        return 0;
    player = (AnimPlayer *)h.ci->activeTree;
    if (player->IsBlendInActiveList(node))
        player->RemoveBlendFromActiveList(node);
    if (reset)
        animationSetToBeginning(h, 0);
    animationResume(h);
    node->blendType = type;
    node->head.opcode = 0x22;
    node->blendFrom = 0;
    node->blendTo = (HierHead *)ctrl;
    node->head.id1 = ctrl->head.id1;
    node->head.id2 = ctrl->head.id2;
    node->weight = weight;
    node->blendTime = time;
    node->blendStartField = (float)timerGetFieldCount();
    node->priority = priority;
    node->parent = 0;
    node->active = 1;
    node->next = 0;
    node->prev = 0;
    node->toTime.startField = ctrl->startField;
    node->toTime.startTime = ctrl->startTime;
    node->toTime.deltaTime = ctrl->deltaTime;
    ctrl->iterations = 0;
    player->AddBlendToActiveList(node);
    return node;
}
void animationRemoveBlend(_animHandle h, AnimBlendNode *node, float)
{
    if (h.ci) {
        AnimPlayer *player = (AnimPlayer *)h.ci->activeTree;

        if (player && player->IsBlendInActiveList(node))
            player->RemoveBlendFromActiveList(node);
        node->active = 0;
    }
}
AnimBlendNode *animationAddProcBlend(_animHandle h, AnimBlendNode *node, int type, float weight, short priority)
{
    AnimProcNode *proc;
    AnimPlayer *player;

    if (h.ci == 0) {
        printf(D_006F3150, type);
        return 0;
    }
    proc = h.proc;
    if (proc == 0)
        return 0;
    player = (AnimPlayer *)h.ci->activeTree;
    if (player->IsBlendInActiveList(node))
        player->RemoveBlendFromActiveList(node);
    node->blendType = type;
    node->head.opcode = 0x22;
    node->blendFrom = 0;
    node->blendTo = (HierHead *)proc;
    node->head.id1 = proc->head.id1;
    node->head.id2 = proc->head.id2;
    node->weight = weight;
    node->blendTime = 0.0f;
    node->blendStartField = (float)timerGetFieldCount();
    node->priority = priority;
    node->parent = 0;
    node->next = 0;
    node->prev = 0;
    node->toTime.startField = 0;
    node->toTime.startTime = 0.0f;
    node->toTime.deltaTime = 0.0f;
    node->active = 1;
    player->AddBlendToActiveList(node);
    return node;
}
#ifdef NON_MATCHING
/* 28% of words; inner loop reloads ci->character each pass in retail (ours hoists it and strength-reduces animations[k]) */
void animationGetHandle(_animHandle *h, unsigned id1, unsigned id2, unsigned animId)
{
    int i;
    unsigned n;
    unsigned k;
    _animCharInstance **pp;

    h->animIdx = -1;
    h->ci = 0;
    h->ctrl = 0;
    for (i = 0; i < getNgpFilesLoaded(); i++) {
        _animmgr *mgr = D_00735690[i];
        if (mgr == 0)
            return;
        pp = mgr->charInstance;
        for (n = mgr->numCharInstances; n != 0; n--, pp++) {
            _animCharInstance *ci = *pp;
            if (ci->head.id1 != id1 || ci->head.id2 != id2)
                continue;
            for (k = 0; k < ci->character->numAnims; k++) {
                if (ci->character->animations[k]->head.id1 == animId) {
                    h->ci = ci;
                    h->animIdx = k;
                    h->ctrl = &ci->animCtx[k];
                    return;
                }
            }
        }
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/common/animation", animationGetHandle__FP11_animHandleUiUiUi);
#endif
void animationGetNullHandle(_animHandle *h, unsigned id1, unsigned id2)
{
    int i;
    unsigned n;
    _animCharInstance **pp;

    h->animIdx = -1;
    h->ci = 0;
    h->ctrl = 0;
    for (i = 0; i < getNgpFilesLoaded(); i++) {
        _animmgr *mgr = D_00735690[i];
        if (mgr == 0)
            return;
        pp = mgr->charInstance;
        for (n = mgr->numCharInstances; n != 0; n--, pp++) {
            _animCharInstance *ci = *pp;
            if (ci->head.id1 == id1 && ci->head.id2 == id2) {
                h->ci = ci;
                return;
            }
        }
    }
}
float animationGetCurrentTime(_animHandle h)
{
    float t;

    if (h.ci == 0 || h.ctrl == 0)
        return 0.0f;
    if (h.ctrl->active)
        t = h.ctrl->deltaTime * (float)(int)(timerGetFieldCount() - h.ctrl->startField);
    else
        t = (float)(int)h.ctrl->startField * h.ctrl->deltaTime;
    return t + h.ctrl->startTime;
}
#ifdef NON_MATCHING
/* 61% of words; retail fills the bc1t delay slot with the anim load */
float animationCalculatePlayTime(_animHandle h)
{
    float t = 0.0f;

    if (h.ci && h.ctrl && h.ctrl->deltaTime != t)
        t = (h.ctrl->anim->endTime - h.ctrl->anim->startTime) / __builtin_fabsf(h.ctrl->deltaTime);
    return t;
}
#else
INCLUDE_ASM("asm/nonmatchings/common/animation", animationCalculatePlayTime__FG11_animHandle);
#endif
void animationResetCharacter(_animHandle &h)
{
    _animCharInstance *ci = h.ci;
    AnimPlayer *player;
    HierHead *tree;
    AnimBlendNode *node;

    if (ci == 0)
        return;
    player = (AnimPlayer *)ci->activeTree;
    tree = player->GetMainTreeNode();
    node = player->GetActiveList();
    if (tree) {
        animationCleanUpTree(tree, ci);
        player->mainTree = 0;
    }
    while (node) {
        player->RemoveBlendFromActiveList(node);
        node = player->GetActiveList();
    }
}
void animationStart(_animHandle h, bool restart)
{
    HierAnimation *anim;
    float t;

    if (h.ci == 0)
        return;
    anim = h.ci->character->animations[h.animIdx];
    animationAddToActiveTree(h);
    animationResume(h);
    animationSetDirection(h, 1);
    t = animationGetCurrentTime(h);
    if (restart || t < anim->startTime)
        animationSetToBeginning(h, 0);
    animationSetIterations(h, 0);
}
void animationStartReverse(_animHandle h, bool restart)
{
    HierAnimation *anim;
    float t;

    if (h.ci == 0)
        return;
    anim = h.ci->character->animations[h.animIdx];
    animationAddToActiveTree(h);
    animationResume(h);
    animationSetDirection(h, 0);
    t = animationGetCurrentTime(h);
    if (restart || anim->endTime < t)
        animationSetToEnd(h, 0);
    animationSetIterations(h, 0);
}
void animationSetToFrame(_animHandle h, float frame, int update)
{
    HierAnimation *anim;

    if (h.ci == 0 || h.ctrl == 0)
        return;
    anim = h.ci->character->animations[h.animIdx];
    animationResume(h);
    h.ctrl->startField = timerGetFieldCount();
    if (anim->endTime < frame)
        h.ctrl->startTime = anim->endTime;
    else if (frame < anim->startTime)
        h.ctrl->startTime = anim->startTime;
    else
        h.ctrl->startTime = frame;
    if (update)
        animationUpdate(anim, h.ci);
    animationPause(h);
}
float animationGetCurrentFrame(_animHandle h)
{
    float frame = -1.0f;

    if (h.ci)
        frame = animationGetCurrentTime(h);
    return frame;
}
void animationSetToPercent(_animHandle h, float percent, int update)
{
    AnimControlNode *ctrl;
    HierAnimation *anim;
    float t;

    if (h.ci == 0 || (ctrl = h.ctrl) == 0)
        return;
    anim = h.ci->character->animations[h.animIdx];
    t = (anim->endTime - anim->startTime) * percent + anim->startTime;
    ctrl->startField = ctrl->active ? timerGetFieldCount() : 0;
    h.ctrl->startTime = t;
    if (update) {
        if (!h.ctrl->active)
            animationResume(h);
        animationUpdate(anim, h.ci);
    }
}
float animationGetCurrentPercent(_animHandle h)
{
    HierAnimation *anim;
    float len;
    float percent = -1.0f;

    if (h.ci) {
        anim = h.ci->character->animations[h.animIdx];
        len = anim->endTime - anim->startTime;
        percent = (animationGetCurrentTime(h) - anim->startTime) / len;
    }
    return percent;
}
void animationPause(_animHandle h)
{
    if (h.ci == 0 || h.ctrl == 0)
        return;
    if (h.ctrl->active) {
        h.ctrl->startField = timerGetFieldCount() - h.ctrl->startField;
        h.ctrl->active = 0;
    }
}
void animationResume(_animHandle h)
{
    if (h.ci == 0 || h.ctrl == 0)
        return;
    if (!h.ctrl->active) {
        h.ctrl->startField = timerGetFieldCount() - h.ctrl->startField;
        h.ctrl->active = 1;
    }
    if (h.ci->activeTree == 0) {
        if (animationGetDirection(h))
            animationStart(h, 0);
        else
            animationStartReverse(h, 0);
    }
}
void animationLoop(_animHandle h, bool loop)
{
    if (h.ci && h.ctrl)
        h.ctrl->loop = loop;
}
#ifdef NON_MATCHING
/* 91% of words; v0/v1 swapped around the active test */
void animationSetSpeed(_animHandle h, float speed)
{
    float mag = __builtin_fabsf(speed);
    AnimControlNode *ctrl;
    float t;
    int now;

    if (h.ci == 0 || h.ctrl == 0)
        return;
    t = animationGetCurrentTime(h);
    now = timerGetFieldCount();
    ctrl = h.ctrl;
    ctrl->startTime = t;
    if (!ctrl->active)
        h.ctrl->startField = 0;
    else
        h.ctrl->startField = now;
    if (h.ctrl->deltaTime == 0.0f)
        h.ctrl->deltaTime = speed;
    else
        h.ctrl->deltaTime = 0.0f < h.ctrl->deltaTime ? mag : -mag;
}
#else
INCLUDE_ASM("asm/nonmatchings/common/animation", animationSetSpeed__FG11_animHandlef);
#endif
void animationResetSpeed(_animHandle h)
{
    if (h.ci)
        animationSetSpeed(h, 1.0f);
}
void animationSetToBeginning(_animHandle h, bool update)
{
    HierAnimation *anim;

    if (h.ci == 0 || h.ctrl == 0)
        return;
    anim = h.ci->character->animations[h.animIdx];
    if (update) {
        animationSetToFrame(h, anim->startTime, 1);
        return;
    }
    if (h.ctrl->active)
        h.ctrl->startField = timerGetFieldCount();
    else
        h.ctrl->startField = 0;
    h.ctrl->startTime = anim->startTime;
}
void animationSetToEnd(_animHandle h, bool update)
{
    HierAnimation *anim;

    if (h.ci == 0 || h.ctrl == 0)
        return;
    anim = h.ci->character->animations[h.animIdx];
    if (update) {
        animationSetToFrame(h, anim->endTime, 1);
        return;
    }
    if (h.ctrl->active)
        h.ctrl->startField = timerGetFieldCount();
    else
        h.ctrl->startField = 0;
    h.ctrl->startTime = anim->endTime;
}
#ifdef NON_MATCHING
/* 55% of words; retail joins both direction tests on one bc1f */
void animationSetDirection(_animHandle h, bool forward)
{
    int now;
    float t;

    if (h.ci == 0 || h.ctrl == 0)
        return;
    now = timerGetFieldCount();
    t = (float)(now - h.ctrl->startField) * h.ctrl->deltaTime + h.ctrl->startTime;
    if (forward ? h.ctrl->deltaTime < 0.0f : 0.0f < h.ctrl->deltaTime) {
        if (h.ctrl->active) {
            h.ctrl->startTime = t;
            h.ctrl->startField = now;
        }
        h.ctrl->deltaTime = -h.ctrl->deltaTime;
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/common/animation", animationSetDirection__FG11_animHandleb);
#endif
int animationGetDirection(_animHandle h)
{
    if (h.ci && h.ctrl && h.ctrl->deltaTime < 0.0f)
        return 0;
    return 1;
}
float animationGetLastFrame(_animHandle h)
{
    if (h.ci)
        return h.ci->character->animations[h.animIdx]->endTime;
    return 0.0f;
}
int animationIsRunning(_animHandle h)
{
    if (h.ci && h.ctrl && h.ctrl->active)
        return 1;
    return 0;
}
void animationSetTotalRunFrames(_animHandle h, float frames)
{
    HierAnimation *anim;

    if (h.ci && h.ctrl) {
        anim = h.ci->character->animations[h.animIdx];
        h.ctrl->deltaTime = (anim->endTime - anim->startTime) / frames;
    }
}
float animationGetTotalRunFrames(_animHandle h)
{
    HierAnimation *anim;
    float frames = 0.0f;

    if (h.ci && h.ctrl) {
        anim = h.ci->character->animations[h.animIdx];
        frames = (anim->endTime - anim->startTime) / h.ctrl->deltaTime;
    }
    return frames;
}
float animationGetFrameAtPercent(_animHandle h, float percent)
{
    HierAnimation *anim;
    float frame = -1.0f;

    if (h.ci) {
        anim = h.ci->character->animations[h.animIdx];
        frame = (anim->endTime - anim->startTime) * percent;
    }
    return frame;
}
void animationSetIterations(_animHandle h, unsigned short n)
{
    if (h.ci && h.ctrl)
        h.ctrl->iterations = n;
}
unsigned short animationGetIterations(_animHandle h)
{
    if (h.ci && h.ctrl)
        return h.ctrl->iterations;
    return 0;
}
#define ANIM_DIRTY(ci, idx) (((ci)->animOutput.dirty[(idx) / 8] >> ((idx) % 8)) & 1)
#define ANIM_CLEAN(ci, idx) ((ci)->animOutput.dirty[(idx) / 8] &= ~(1 << ((idx) % 8)))
void animationCheckDirty(_animCharInstance *ci, _animtransform *xf)
{
    int dirty = 0;
    int idx;

    if ((idx = xf->rXidx) >= 0)
        dirty = ANIM_DIRTY(ci, idx);
    if ((idx = xf->rYidx) >= 0)
        dirty |= ANIM_DIRTY(ci, idx);
    if ((idx = xf->rZidx) >= 0)
        dirty |= ANIM_DIRTY(ci, idx);
    if (dirty)
        *(unsigned long *)&xf->dirty |= 2;
    dirty = 0;
    if ((idx = xf->tXidx) >= 0)
        dirty = ANIM_DIRTY(ci, idx);
    if ((idx = xf->tYidx) >= 0)
        dirty |= ANIM_DIRTY(ci, idx);
    if ((idx = xf->tZidx) >= 0)
        dirty |= ANIM_DIRTY(ci, idx);
    if (dirty)
        *(unsigned long *)&xf->dirty |= 1;
}
extern "C" void animationClearDirty(_animCharInstance *ci, _animtransform *xf)
{
    int idx;

    if ((idx = xf->rXidx) >= 0)
        ANIM_CLEAN(ci, idx);
    if ((idx = xf->rYidx) >= 0)
        ANIM_CLEAN(ci, idx);
    if ((idx = xf->rZidx) >= 0)
        ANIM_CLEAN(ci, idx);
    if ((idx = xf->tXidx) >= 0)
        ANIM_CLEAN(ci, idx);
    if ((idx = xf->tYidx) >= 0)
        ANIM_CLEAN(ci, idx);
    if ((idx = xf->tZidx) >= 0)
        ANIM_CLEAN(ci, idx);
}
int animationGetChannelIdx(int id, AnimChannelIdMap *map)
{
    int lo, hi, mid;

    if (map) {
        lo = 0;
        hi = map->nIds;
        while (lo <= hi) {
            mid = (lo + hi) / 2;
            if (map->entries[mid].id < id)
                lo = mid + 1;
            else if (id < map->entries[mid].id)
                hi = mid - 1;
            else
                return map->entries[mid].chanIdx;
        }
    }
    return -1;
}
float *animationGetChannelVal(int id, _animHandle &h)
{
    _animCharInstance *ci = h.ci;
    int idx;

    if (ci == 0)
        return 0;
    idx = animationGetChannelIdx(id, ci->character->animChannelIdMap);
    if (idx < 0)
        return 0;
    return &ci->animOutput.val[idx];
}
float animationUpdateHandle(_animHandle &h)
{
    float t = animationGetCurrentTime(h);
    AnimControlNode *ctrl = h.ctrl;
    HierAnimation *anim = ctrl->anim;

    if (anim->endTime < t && 0.0f < ctrl->deltaTime) {
        if (!ctrl->loop) {
            if (((AnimPlayer *)h.ci->activeTree)->mainTree == (HierHead *)ctrl)
                animationPause(h);
            animationSetToEnd(h, 0);
            t = animationGetCurrentTime(h);
        } else {
            ctrl->iterations++;
            animationSetToBeginning(h, 0);
            t = animationGetCurrentTime(h);
        }
    } else if (t < anim->startTime && ctrl->deltaTime < 0.0f) {
        if (!ctrl->loop) {
            if (((AnimPlayer *)h.ci->activeTree)->mainTree == (HierHead *)ctrl)
                animationPause(h);
            animationSetToBeginning(h, 0);
            t = animationGetCurrentTime(h);
        } else {
            ctrl->iterations++;
            animationSetToEnd(h, 0);
            t = animationGetCurrentTime(h);
        }
    }
    return t;
}
void animationUpdateFromControlNode(AnimControlNode *ctrl, _animCharInstance *ci)
{
    _animHandle h;
    HierAnimation *anim;
    AnimCurveHeader **chan;
    uint16 *key;
    int count;
    float t;
    int idx;

    h.ci = ci;
    h.proc = 0;
    h.ctrl = ctrl;
    anim = ctrl->anim;
    chan = anim->channels;
    h.animIdx = anim->animIdx;
    t = animationUpdateHandle(h);
    key = ctrl->prevKey;
    for (count = anim->numChannels; count != 0; count--, chan++, key++) {
        ctrl->animOutput.val[(*chan)->dataIdx] = animCurveEvaluate(*chan, t, key);
        idx = (*chan)->dataIdx;
        ctrl->animOutput.dirty[idx / 8] |= 1 << (idx % 8);
    }
}
void animationUpdateActiveTree(HierHead *tree, _animCharInstance *ci)
{
    if (tree->opcode == 0x21) {
        ((AnimControlNode *)tree)->animOutput.val = ci->animOutput.val;
        ((AnimControlNode *)tree)->animOutput.dirty = ci->animOutput.dirty;
        ((AnimControlNode *)tree)->animOutput.atMat = ci->animOutput.atMat;
        animationProcessActiveTree(tree, ci);
    } else if (tree->opcode == 0x22) {
        ((AnimBlendNode *)tree)->animOutput.val = ci->animOutput.val;
        ((AnimBlendNode *)tree)->animOutput.dirty = ci->animOutput.dirty;
        ((AnimBlendNode *)tree)->animOutput.atMat = ci->animOutput.atMat;
        animationProcessActiveTree(tree, ci);
    } else if (tree->opcode == 0x2B) {
        ((AnimProcNode *)tree)->animOutput.val = ci->animOutput.val;
        ((AnimProcNode *)tree)->animOutput.dirty = ci->animOutput.dirty;
        ((AnimProcNode *)tree)->animOutput.atMat = ci->animOutput.atMat;
        animationProcessActiveTree(tree, ci);
    }
}
void animationProcessActiveTree(HierHead *tree, _animCharInstance *ci)
{
    switch (tree->opcode) {
    case 0x21:
        animationUpdateFromControlNode((AnimControlNode *)tree, ci);
        break;
    case 0x2B:
        ((void (*)(HierHead *, _animCharInstance *))((AnimProcNode *)tree)->procCallback)(tree, ci);
        break;
    case 0x22:
        D_007329F0.pushMark();
        switch (((AnimBlendNode *)tree)->blendType) {
        case BLEND_TRANSITION:
        case BLEND_FREEZETRANS:
            animationProcessTransitionBlend((AnimBlendNode *)tree, ci);
            break;
        case BLEND_STATIC:
            animationProcessStaticBlend((AnimBlendNode *)tree, ci);
            break;
        case BLEND_OVERRIDE:
            animationProcessOverrideBlend((AnimBlendNode *)tree, ci);
            break;
        case BLEND_ADDITIVE:
            animationProcessAdditiveBlend((AnimBlendNode *)tree, ci);
            break;
        case BLEND_PROCEEDURAL:
            animationProcessProceeduralBlend((AnimBlendNode *)tree, ci);
            break;
        default:
            ((AnimBlendNode *)tree)->weight = 0.0f;
            break;
        }
        D_007329F0.popMark();
        break;
    }
}
#ifdef NON_MATCHING
/* 284/300: the angle temporaries sit 8 bytes higher in the frame; the pool index is divided with sra instead of srl (see AnimBlendPool::release) */
void animationProcessTransitionBlend(AnimBlendNode *node, _animCharInstance *ci)
{
    AnimationOutputBlock *out[2];
    float v[3];
    float t;

    t = ((float)timerGetFieldCount() - node->blendStartField) * 16.66667f;
    out[0] = 0;
    out[1] = 0;
    if (t < node->blendTime) {
        HierAnimCharacter *chr;
        unsigned char *angular;
        int i;

        animationEvaluateFromNode(node, ci, &out[0]);
        animationEvaluateToNode(node, ci, &out[1]);
        {
            float r = t / node->blendTime;
            float f;

            switch (s_blendCurve) {
            case 0:
            default:
                f = r;
                break;
            case 1:
                f = (mathfApproxCos2(r + -1.0f) + 1.0f) * 0.5f;
                break;
            case 2:
                f = r * r;
                break;
            case 3:
                f = r * (2.0f - r);
                break;
            }
            node->weight = f;
        }
        chr = ci->character;
        if (chr->numChannels <= 16)
            angular = chr->angularChannelBits.bytes;
        else
            angular = (unsigned char *)chr + chr->angularChannelBits.offset;
        for (i = 0; i < chr->numChannels; i++) {
            int fromDirty = out[0] ? (out[0]->dirty[i / 8] >> (i % 8)) & 1 : 0;
            int toDirty = out[1] ? (out[1]->dirty[i / 8] >> (i % 8)) & 1 : 0;

            if (fromDirty) {
                if (toDirty) {
                    v[0] = out[0]->val[i];
                    v[1] = out[1]->val[i];
                    if ((angular[i / 8] >> (i % 8)) & 1) {
                        boundEulerAngle(&v[0]);
                        boundEulerAngle(&v[1]);
                    }
                    v[2] = v[1] - v[0];
                    if ((angular[i / 8] >> (i % 8)) & 1)
                        boundEulerAngle(&v[2]);
                    node->animOutput.val[i] = v[0] + v[2] * node->weight;
                    node->animOutput.dirty[i / 8] |= 1 << (i % 8);
                } else {
                    if ((angular[i / 8] >> (i % 8)) & 1)
                        boundEulerAngle(&out[0]->val[i]);
                    node->animOutput.val[i] = out[0]->val[i];
                    node->animOutput.dirty[i / 8] |= 1 << (i % 8);
                }
            } else if (toDirty) {
                if ((angular[i / 8] >> (i % 8)) & 1)
                    boundEulerAngle(&out[1]->val[i]);
                node->animOutput.val[i] = out[1]->val[i];
                node->animOutput.dirty[i / 8] |= 1 << (i % 8);
            } else {
                node->animOutput.dirty[i / 8] &= ~(1 << (i % 8));
            }
        }
    } else {
        animationCollapseBlend(node, ci);
        out[1] = &node->animOutput;
        animationEvaluateToNode(node, ci, &out[1]);
        if (node->blendType == BLEND_FREEZETRANS) {
            AnimControlNode *ctrl = (AnimControlNode *)node->blendTo;

            ctrl->deltaTime = node->toTime.deltaTime;
            ctrl->startField = timerGetFieldCount();
            ctrl->active = 1;
        }
        D_00732A08.release(node);
        okToBlend = 1;
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/common/animation", animationProcessTransitionBlend__FP14_animBlendNodeP17_animCharInstance);
#endif
#ifdef NON_MATCHING
/* 169/185: the dirty-word counter (words - 1) is kept in a different register/slot */
void animationProcessStaticBlend(AnimBlendNode *node, _animCharInstance *ci)
{
    AnimationOutputBlock *from;
    AnimationOutputBlock *to;
    float d;
    float t;
    float w;

    t = ((float)timerGetFieldCount() - node->blendStartField) * 16.66667f;
    from = 0;
    to = 0;
    if (t < node->blendTime) {
        float r = t / node->blendTime;
        float f;

        switch (s_blendCurve) {
        case 0:
        default:
            f = r;
            break;
        case 1:
            f = (mathfApproxCos2(r + -1.0f) + 1.0f) * 0.5f;
            break;
        case 2:
            f = r * r;
            break;
        case 3:
            f = r * (2.0f - r);
            break;
        }
        w = f * node->weight;
    } else {
        w = node->weight;
    }
    if (w < 1e-10f)
        return;
    animationEvaluateFromNode(node, ci, &from);
    animationEvaluateToNode(node, ci, &to);
    {
        HierAnimCharacter *chr = ci->character;
        unsigned char *angular;
        int count;
        int idx;
        int words;

        unsigned long *mask;

        if (chr->numChannels <= 16)
            angular = chr->angularChannelBits.bytes;
        else
            angular = (unsigned char *)chr + chr->angularChannelBits.offset;
        count = chr->numChannels;
        idx = 0;
        mask = (unsigned long *)to->dirty;
        words = (count >> 6) + 1;
        for (; words; words--) {
            unsigned long bits = ((Packed64 *)mask++)->v;
            int n = words > 1 ? 64 : count & 0x3F;

            for (; n; n--, idx++) {
                int b = bits & 1;

                if (b) {
                    float from = node->animOutput.val[idx];

                    d = to->val[idx];

                    if ((angular[idx / 8] >> (idx % 8)) & 1)
                        boundEulerAngle(&d);
                    d -= from;
                    if ((angular[idx / 8] >> (idx % 8)) & 1)
                        boundEulerAngle(&d);
                    node->animOutput.val[idx] = from + d * w;
                    node->animOutput.dirty[idx / 8] |= 1 << (idx % 8);
                }
                bits >>= 1;
            }
        }
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/common/animation", animationProcessStaticBlend__FP14_animBlendNodeP17_animCharInstance);
#endif
#ifdef NON_MATCHING
/* 123/134: idx=0 and the word counter land in different registers */
void animationProcessOverrideBlend(AnimBlendNode *node, _animCharInstance *ci)
{
    AnimationOutputBlock *out[2];
    float t;
    float w;

    t = ((float)timerGetFieldCount() - node->blendStartField) * 16.66667f;
    out[0] = 0;
    out[1] = 0;
    if (t < node->blendTime) {
        float r = t / node->blendTime;
        float f;

        switch (s_blendCurve) {
        case 0:
        default:
            f = r;
            break;
        case 1:
            f = (mathfApproxCos2(r + -1.0f) + 1.0f) * 0.5f;
            break;
        case 2:
            f = r * r;
            break;
        case 3:
            f = r * (2.0f - r);
            break;
        }
        w = f * node->weight;
    } else {
        w = node->weight;
    }
    if (w < 1e-10f)
        return;
    animationEvaluateFromNode(node, ci, &out[0]);
    animationEvaluateToNode(node, ci, &out[1]);
    {
        int count = ci->character->numChannels;
        int idx = 0;
        int words = (count >> 6) + 1;
        unsigned long *mask = (unsigned long *)out[1]->dirty;

        for (; words; words--) {
            unsigned long bits = ((Packed64 *)mask++)->v;
            int n = words > 1 ? 64 : count & 0x3F;

            for (; n; n--, idx++) {
                int b = bits & 1;

                if (b) {
                    node->animOutput.val[idx] = out[1]->val[idx] * w;
                    node->animOutput.dirty[idx / 8] |= 1 << (idx % 8);
                }
                bits >>= 1;
            }
        }
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/common/animation", animationProcessOverrideBlend__FP14_animBlendNodeP17_animCharInstance);
#endif
void animationProcessAdditiveBlend(AnimBlendNode *node, _animCharInstance *ci)
{
    AnimControlNode *ctrl = (AnimControlNode *)node->blendTo;
    HierAnimation *anim;
    float t;
    float w;

    if (ctrl->head.opcode != 0x21)
        return;
    anim = ctrl->anim;
    t = ((float)timerGetFieldCount() - node->blendStartField) * 16.66667f;
    if (t < node->blendTime) {
        float r = t / node->blendTime;
        float f;

        switch (s_blendCurve) {
        case 0:
        default:
            f = r;
            break;
        case 1:
            f = (mathfApproxCos2(r + -1.0f) + 1.0f) * 0.5f;
            break;
        case 2:
            f = r * r;
            break;
        case 3:
            f = r * (2.0f - r);
            break;
        }
        w = f * node->weight;
    } else {
        w = node->weight;
    }
    {
        _animHandle h;
        HierAnimCharacter *chr;
        unsigned char *angular;
        AnimCurveHeader **chan;
        unsigned short *key;
        int count;
        float at;

        h.proc = 0;
        h.ci = ci;
        h.ctrl = ctrl;
        h.animIdx = anim->animIdx;
        at = animationUpdateHandle(h);
        chr = ci->character;
        if (chr->numChannels <= 16)
            angular = chr->angularChannelBits.bytes;
        else
            angular = (unsigned char *)chr + chr->angularChannelBits.offset;
        chan = anim->channels;
        key = ctrl->prevKey;
        for (count = anim->numChannels; count != 0; count--, chan++, key++) {
            int idx = (*chan)->dataIdx;

            node->animOutput.val[idx] += w * animCurveEvaluate(*chan, at, key);
            if ((angular[idx / 8] >> (idx % 8)) & 1)
                boundEulerAngle(&node->animOutput.val[idx]);
            node->animOutput.dirty[idx / 8] |= 1 << (idx % 8);
        }
    }
}
void animationProcessProceeduralBlend(AnimBlendNode *, _animCharInstance *)
{
}
void animationEvaluateFromNode(AnimBlendNode *node, _animCharInstance *ci, AnimationOutputBlock **out)
{
    HierHead *from = node->blendFrom;
    _animHandle h;
    AnimationOutputBlock *blk;
    unsigned char *dirty;
    unsigned char *p;
    int n;

    h.proc = 0;
    if (from == 0)
        return;
    if (from->opcode == 0x21) {
        AnimControlNode *ctrl = (AnimControlNode *)from;

        blk = &ctrl->animOutput;
        ctrl->animOutput.val = (float *)D_007329F0.alloc16(ci->character->numChannels * 4);
        n = ci->character->numChannels;
        n = (n >> 3) + 1;
        p = dirty = (unsigned char *)D_007329F0.alloc16(n);
        for (; n != 0; n--)
            *p++ = 0;
        ctrl->animOutput.dirty = dirty;
        ctrl->animOutput.atMat = ci->animOutput.atMat;
        h.ci = ci;
        h.ctrl = ctrl;
        h.animIdx = ctrl->anim->animIdx;
        animationResume(h);
        ctrl->startField = node->fromTime.startField;
        ctrl->startTime = node->fromTime.startTime;
        ctrl->deltaTime = node->fromTime.deltaTime;
        animationProcessActiveTree(node->blendFrom, ci);
        ctrl->deltaTime = node->fromTime.deltaTime;
        node->fromTime.startField = ctrl->startField;
        node->fromTime.startTime = ctrl->startTime;
    } else if (from->opcode == 0x2B) {
        AnimProcNode *proc = (AnimProcNode *)from;

        blk = &proc->animOutput;
        proc->animOutput.val = (float *)D_007329F0.alloc16(ci->character->numChannels * 4);
        n = ci->character->numChannels;
        n = (n >> 3) + 1;
        p = dirty = (unsigned char *)D_007329F0.alloc16(n);
        for (; n != 0; n--)
            *p++ = 0;
        proc->animOutput.dirty = dirty;
        proc->animOutput.atMat = ci->animOutput.atMat;
        animationProcessActiveTree(node->blendFrom, ci);
        *out = blk;
        return;
    } else if (from->opcode == 0x22) {
        AnimBlendNode *blend = (AnimBlendNode *)from;

        blk = &blend->animOutput;
        blend->animOutput.val = (float *)D_007329F0.alloc16(ci->character->numChannels * 4);
        n = ci->character->numChannels;
        n = (n >> 3) + 1;
        p = dirty = (unsigned char *)D_007329F0.alloc16(n);
        for (; n != 0; n--)
            *p++ = 0;
        blend->animOutput.dirty = dirty;
        blend->animOutput.atMat = ci->animOutput.atMat;
        animationProcessActiveTree(node->blendFrom, ci);
        node->fromTime.startField = blend->toTime.startField;
        node->fromTime.startTime = blend->toTime.startTime;
        node->fromTime.deltaTime = blend->toTime.deltaTime;
    } else {
        return;
    }
    *out = blk;
}
#ifdef NON_MATCHING
/* 190/205: out and the result block pointer swap callee-saved registers (s3/s4) */
void animationEvaluateToNode(AnimBlendNode *node, _animCharInstance *ci, AnimationOutputBlock **out)
{
    HierHead *to = node->blendTo;
    _animHandle h;
    AnimationOutputBlock *blk;
    unsigned char *dirty;
    unsigned char *p;
    int n;

    h.proc = 0;
    if (to == 0)
        return;
    if (to->opcode == 0x21) {
        AnimControlNode *ctrl = (AnimControlNode *)to;

        blk = &ctrl->animOutput;
        if (*out == 0) {
            ctrl->animOutput.atMat = ci->animOutput.atMat;
            ctrl->animOutput.val = (float *)D_007329F0.alloc16(ci->character->numChannels * 4);
            n = ci->character->numChannels;
            n = (n >> 3) + 1;
            p = dirty = (unsigned char *)D_007329F0.alloc16(n);
            for (; n != 0; n--)
                *p++ = 0;
            ctrl->animOutput.dirty = dirty;
        } else {
            ctrl->animOutput = **out;
        }
        h.ci = ci;
        h.ctrl = ctrl;
        h.animIdx = ctrl->anim->animIdx;
        animationResume(h);
        ctrl->startField = node->toTime.startField;
        ctrl->startTime = node->toTime.startTime;
        if (node->blendType == BLEND_FREEZETRANS)
            ctrl->deltaTime = 0.0f;
        else
            ctrl->deltaTime = node->toTime.deltaTime;
        animationProcessActiveTree(node->blendTo, ci);
        node->toTime.startField = ctrl->startField;
        node->toTime.startTime = ctrl->startTime;
    } else if (to->opcode == 0x2B) {
        AnimProcNode *proc = (AnimProcNode *)to;

        blk = &proc->animOutput;
        if (*out == 0) {
            proc->animOutput.val = (float *)D_007329F0.alloc16(ci->character->numChannels * 4);
            n = ci->character->numChannels;
            n = (n >> 3) + 1;
            p = dirty = (unsigned char *)D_007329F0.alloc16(n);
            for (; n != 0; n--)
                *p++ = 0;
            proc->animOutput.dirty = dirty;
            proc->animOutput.atMat = ci->animOutput.atMat;
        } else {
            proc->animOutput = **out;
        }
        animationProcessActiveTree(node->blendTo, ci);
        *out = blk;
        return;
    } else if (to->opcode == 0x22) {
        AnimBlendNode *blend = (AnimBlendNode *)to;

        blk = &blend->animOutput;
        if (*out == 0) {
            blend->animOutput.val = (float *)D_007329F0.alloc16(ci->character->numChannels * 4);
            n = ci->character->numChannels;
            n = (n >> 3) + 1;
            p = dirty = (unsigned char *)D_007329F0.alloc16(n);
            for (; n != 0; n--)
                *p++ = 0;
            blend->animOutput.dirty = dirty;
            blend->animOutput.atMat = ci->animOutput.atMat;
        } else {
            blend->animOutput = **out;
        }
        animationProcessActiveTree(node->blendTo, ci);
        node->toTime.startField = blend->toTime.startField;
        node->toTime.startTime = blend->toTime.startTime;
        node->toTime.deltaTime = blend->toTime.deltaTime;
    } else {
        return;
    }
    *out = blk;
}
#else
INCLUDE_ASM("asm/nonmatchings/common/animation", animationEvaluateToNode__FP14_animBlendNodeP17_animCharInstancePP21_animationOutputBlock);
#endif
void animationCollapseBlend(AnimBlendNode *node, _animCharInstance *ci)
{
    node->active = 0;
    if (node->parent) {
        node->parent->blendFrom = node->blendTo;
        node->parent->fromTime.startField = node->toTime.startField;
        node->parent->fromTime.startTime = node->toTime.startTime;
        node->parent->fromTime.deltaTime = node->toTime.deltaTime;
    } else {
        ((AnimPlayer *)ci->activeTree)->mainTree = node->blendTo;
    }
    animationCleanUpTree(node->blendFrom, ci);
}
#ifdef NON_MATCHING
/* 67/68: the pool index (node - nodes) is divided with sra; retail has srl (see AnimBlendPool::release) */
void animationCleanUpTree(HierHead *tree, _animCharInstance *ci)
{
    if (tree == 0 || ci == 0)
        return;
    switch (tree->opcode) {
    case 0x21: {
        _animHandle h;
        h.ci = ci;
        h.proc = 0;
        h.ctrl = (AnimControlNode *)tree;
        h.animIdx = ((AnimControlNode *)tree)->anim->animIdx;
        animationPause(h);
        break;
    }
    case 0x22:
        animationCleanUpTree(((AnimBlendNode *)tree)->blendFrom, ci);
        animationCleanUpTree(((AnimBlendNode *)tree)->blendTo, ci);
        D_00732A08.release((AnimBlendNode *)tree);
        break;
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/common/animation", animationCleanUpTree__FP9_hierheadP17_animCharInstance);
#endif
void animationAddToActiveTree(_animHandle h)
{
    _animCharInstance *ci = h.ci;
    AnimPlayer *player;
    HierHead *tree;

    if (ci == 0 || h.ctrl == 0)
        return;
    player = (AnimPlayer *)ci->activeTree;
    tree = player->GetMainTreeNode();
    if (tree) {
        if (tree->opcode == 0x21) {
            _animHandle old;
            old.ci = ci;
            old.proc = 0;
            old.ctrl = (AnimControlNode *)tree;
            old.animIdx = ((AnimControlNode *)tree)->anim->animIdx;
            animationPause(old);
        } else if (tree->opcode == 0x22) {
            animationCleanUpTree(tree, ci);
        }
    }
    player->mainTree = (HierHead *)h.ctrl;
}
void animationUpdate(HierAnimation *anim, _animCharInstance *ci)
{
    AnimControlNode *ctrl = &ci->animCtx[anim->animIdx];

    ctrl->animOutput = ci->animOutput;
    animationUpdateFromControlNode(ctrl, ci);
}
#ifdef NON_MATCHING
/* 84% of words; i = 0 scheduled before the count test in retail */
void animationManager(_animmgr *mgr)
{
    unsigned i;

    for (i = 0; i < mgr->numCharInstances; i++) {
        _animCharInstance *ci = mgr->charInstance[i];
        if (!ci->bits.lazyEvaluate && ci->activeTree)
            ((AnimPlayer *)ci->activeTree)->UpdateAnimations();
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/common/animation", animationManager__FP8_animmgr);
#endif
void animationSetInstance(_animmgr *mgr, int instance)
{
    unsigned i;

    for (i = 0; i < mgr->numCharInstances; i++)
        mgr->charInstance[i]->head.id2 = instance;
}
void animationRunGlobal(void)
{
    int i;

    for (i = 0; i < getNgpFilesLoaded(); i++) {
        if (D_00735690[i])
            animationManager(D_00735690[i]);
    }
}
void animationInitialize(HierAnimation *anim, _animCharInstance *ci, int idx)
{
    AnimControlNode *ctrl;
    _animHandle h;

    anim->animIdx = idx;
    ctrl = &ci->animCtx[idx];
    ctrl->head.id1 = ctrl->anim->head.id1;
    ctrl->head.id2 = ctrl->anim->head.id2;
    if (ctrl->active) {
        h.ci = ci;
        h.proc = 0;
        h.ctrl = ctrl;
        h.animIdx = ctrl->anim->animIdx;
        ctrl->startField = timerGetFieldCount();
        ctrl->startTime = anim->startTime;
        animationAddToActiveTree(h);
    } else {
        h.ci = ci;
        h.proc = 0;
        h.ctrl = ctrl;
        h.animIdx = ctrl->anim->animIdx;
        ctrl->startField = 0;
        ctrl->startTime = anim->startTime;
    }
}
#ifdef NON_MATCHING
/* 81% of words; retail walks the free list with a decrementing pointer */
void animationInitGlobal(void)
{
    int i;

    D_007329F0.clear();
    D_00732A08.reset();
    for (i = 0; i < 14; i++)
        D_00735690[i] = 0;
}
#else
INCLUDE_ASM("asm/nonmatchings/common/animation", animationInitGlobal__Fv);
#endif
void animationSetGlobal(HierHead *head, int idx)
{
    unsigned i;
    unsigned j;

    D_00735690[idx] = (_animmgr *)head;
    for (i = 0; i < D_00735690[idx]->numCharInstances; i++) {
        _animCharInstance *ci = D_00735690[idx]->charInstance[i];
        if (ci->activeTree == 0) {
            char *mem = (char *)(((int)MemoryStack::global.low + 15) & ~15);
            MemoryStack::global.low = mem + sizeof(AnimPlayer);
            ci->activeTree = (HierHead *)new (mem) AnimPlayer(ci);
        }
        for (j = 0; j < ci->character->numAnims; j++)
            animationInitialize(ci->character->animations[j], ci, j);
    }
}
void animationInitTweakers(void)
{
}
void animationUpdateTweakers(int, void *)
{
}
AnimPlayer::AnimPlayer(_animCharInstance *ci)
{
    head.opcode = 0x24;
    animCI = ci;
    mainTree = 0;
    mainTreePureOut = 0;
    activeList = 0;
    numNodes = 0;
}
void AnimPlayer::UpdateAnimations(void)
{
    _animCharInstance *ci = animCI;
    AnimBlendNode *n;

    if (ci->bits.disableCI)
        return;
    if (mainTree && (mainTree->opcode != 0x21 || ((AnimControlNode *)mainTree)->active))
        animationUpdateActiveTree(mainTree, ci);
    if (activeList == 0)
        return;
    if (mainTreePureOut) {
        float *pure = mainTreePureOut;
        float *val = animCI->animOutput.val;
        uint8 *dirty = animCI->animOutput.dirty;
        int i;

        for (i = 0; i < animCI->character->numChannels; i++, val++, pure++) {
            if ((dirty[i / 8] >> (i % 8)) & 1)
                *pure = *val;
            else
                *val = *pure;
        }
    }
    n = activeList;
    while (n) {
        animationUpdateActiveTree((HierHead *)n, animCI);
        if (n == n->next) {
            printf("Animation blend list is broke.  Next pointer equals itself.\n");
            break;
        }
        n = n->next;
    }
}
int AnimPlayer::AddBlendToActiveList(AnimBlendNode *node)
{
    int inserted = 0;
    AnimBlendNode *n = activeList;

    if (n) {
        while (n && !inserted) {
            if (n->prev == 0 && n != activeList)
                printf("STOP HERE(3)\n");
            if (node->priority < n->priority) {
                if (n->prev == 0) {
                    activeList = node;
                    node->next = n;
                    n->prev = node;
                    if (node == n)
                        printf("AddBlendToActiveList doubly inserting a node(1)\n");
                    inserted = 1;
                } else {
                    if (node == n->prev || node == n)
                        printf("AddBlendToActiveList doubly inserting a node(2)\n");
                    inserted = 1;
                    n->prev->next = node;
                    node->prev = n->prev;
                    node->next = n;
                    n->prev = node;
                }
            } else if (n->next == 0) {
                if (node == n)
                    printf("AddBlendToActiveList doubly inserting a node(3)\n");
                n->next = node;
                inserted = 1;
                node->prev = n;
            } else {
                n = n->next;
            }
        }
    } else {
        inserted = 1;
        activeList = node;
        if (mainTreePureOut) {
            float *dst = mainTreePureOut;
            float *src = animCI->animOutput.val;
            unsigned short count = animCI->character->numChannels;
            int i;

            for (i = 0; i < count; i++)
                *dst++ = *src++;
        }
    }
    numNodes++;
    return inserted;
}
int AnimPlayer::RemoveBlendFromActiveList(AnimBlendNode *node)
{
    AnimBlendNode *n;

    for (n = activeList; n; n = n->next) {
        if (n->prev == 0 && n != activeList)
            printf("STOP HERE(1)\n");
        if (n == node) {
            if (activeList == n)
                activeList = n->next;
            if (n->prev)
                n->prev->next = n->next;
            if (n->next)
                n->next->prev = n->prev;
            n->prev = 0;
            n->next = 0;
            numNodes--;
            return 1;
        }
    }
    return 0;
}
int AnimPlayer::IsBlendInActiveList(AnimBlendNode *node)
{
    AnimBlendNode *prev = 0;
    AnimBlendNode *n = activeList;

    while (n) {
        if (n->prev == 0 && n != activeList) {
            n->prev = prev;
            printf("STOP HERE(2)\n");
        }
        if (n == node)
            return 1;
        if (n == n->next)
            break;
        prev = n;
        n = n->next;
    }
    return 0;
}
/* retail rodata keeps two empty strings after the last literal of this TU */
__asm__(".section .rodata
	.word 0
	.word 0
	.text");
HierHead *AnimPlayer::GetMainTreeNode(void)
{
    return mainTree;
}
AnimBlendNode *AnimPlayer::GetActiveList(void)
{
    return activeList;
}
int AnimPlayer::GetNumNodesInActiveList(void)
{
    return numNodes;
}
INCLUDE_ASM("asm/nonmatchings/common/animation", __static_initialization_and_destruction_0_001F1FC8);
INCLUDE_ASM("asm/nonmatchings/common/animation", _GLOBAL_$I$animationInitModifierBlends__FP17_animCharInstance);
INCLUDE_ASM("asm/nonmatchings/common/animation", _GLOBAL_$D$animationInitModifierBlends__FP17_animCharInstance);
