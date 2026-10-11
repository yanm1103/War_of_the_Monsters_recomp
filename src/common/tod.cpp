#include "common.h"
#include "hieri_types.h"

/* One time-of-day key, file format 1.4 (12 of them, one every two hours); todUpdate blends the keys around the current time. */
struct _todInfo14 {
    float time;                                    /* 0x00: hour */
    float ambientRed, ambientGreen, ambientBlue;   /* 0x04 */
    float fogMinRange, fogMaxRange, fogMaxVal;     /* 0x10 */
    float fogFarClip;                              /* 0x1C */
    float fogRed, fogGreen, fogBlue;               /* 0x20 */
    float bgRed, bgGreen, bgBlue;                  /* 0x2C: background (clear) color */
    float dirHeading;                              /* 0x38: sun light direction */
    float dirPitch;                                /* 0x3C */
    float dirRed, dirGreen, dirBlue;               /* 0x40 */
    float backIntensity;                           /* 0x4C: back light, percent of the sun (-1: own color) */
    float backRed, backGreen, backBlue;            /* 0x50 */
    int sky;                                       /* 0x5C: sky set */
    float skyFogBurnThru;                          /* 0x60 */
    float skyRedMod, skyGreenMod, skyBlueMod;      /* 0x64 */
    float cloudRedMod, cloudGreenMod, cloudBlueMod, cloudAlphaMod; /* 0x70 */
};

/* Older key layouts accepted by todSetTOD. */
struct _todInfo10 {
    float time;
    float ambientRed, ambientGreen, ambientBlue;
    float fogRed, fogGreen, fogBlue;
    float fogMinRange;
    int sky;
};
struct _todInfo11 {
    float time;
    float ambientRed, ambientGreen, ambientBlue;
    float fogRed, fogGreen, fogBlue;
    float fogMinRange;
    int sky;
    float unk24, unk28; /* not loaded */
    float skyFogBurnThru;
    float skyRedMod, skyGreenMod, skyBlueMod;
};
struct _todInfo12 {
    float time;
    float ambientRed, ambientGreen, ambientBlue;
    float fogMinRange, fogMaxRange, fogMaxVal, fogFarClip;
    float fogRed, fogGreen, fogBlue;
    float dirHeading;
    float dirPitch;
    float dirRed, dirGreen, dirBlue;
    int sky;
    float unk44, unk48; /* not loaded */
    float skyFogBurnThru;
    float skyRedMod, skyGreenMod, skyBlueMod;
};
struct _todInfo13 {
    float time;
    float ambientRed, ambientGreen, ambientBlue;
    float fogMinRange, fogMaxRange, fogMaxVal, fogFarClip;
    float fogRed, fogGreen, fogBlue;
    float dirHeading;
    float dirPitch;
    float dirRed, dirGreen, dirBlue;
    float backIntensity;
    int sky;
    float skyFogBurnThru;
    float skyRedMod, skyGreenMod, skyBlueMod;
    float cloudRedMod, cloudGreenMod, cloudBlueMod, cloudAlphaMod;
};
typedef char _size__todInfo10[sizeof(_todInfo10) == 0x24 ? 1 : -1];
typedef char _size__todInfo11[sizeof(_todInfo11) == 0x3C ? 1 : -1];
typedef char _size__todInfo12[sizeof(_todInfo12) == 0x5C ? 1 : -1];
typedef char _size__todInfo13[sizeof(_todInfo13) == 0x68 ? 1 : -1];
typedef char _size__todInfo14[sizeof(_todInfo14) == 0x80 ? 1 : -1];

/* Node of the time-of-day key list (indices into CTODLinkList::nodes, -1 = none). */
struct _LinkNode {
    int next;       /* 0x00: -1 while the node is free */
    int prev;       /* 0x04 */
    int index;      /* 0x08 */
    int time;       /* 0x0C */
    _todInfo14 *info; /* 0x10 */
};

class CTODLinkList {
public:
    int numEntries;      /* 0x00 */
    int head;            /* 0x04 */
    int tail;            /* 0x08 */
    _LinkNode nodes[12]; /* 0x0C */

    CTODLinkList(void);
    ~CTODLinkList(void);
    int AddEntry(void);
    void DeleteEntry(int);
    int GetNumEntries(void);
    _LinkNode *FindNodeByTime(float);
    int FindFreeNode(void);
    void SortList(void);
    void ClearNode(_LinkNode *node);
};

extern _todInfo14 todInfo[12];

/* One sky group (todSetSkyEntry): its first three objects' colors and UV scroll / fog burn-through. */
struct _todSky {
    HierHead *hier; /* 0x00 */
    int pad[3];
    struct {
        float red, green, blue, alpha;
    } layer[3]; /* 0x10 */
    struct {
        float u, v, fogBurnThru, zBufferFudge;
    } mod[3]; /* 0x40 */
};

extern "C" float cosf(float);

extern int todTimeOfDayOn;
extern int todSetTimeOfDay;
extern float todTimeOfDayMinutesPerSecond;
extern float todTimeOfDay;
extern int todFrame;
extern int todAddFrame;
extern int todDeleteFrame;
__asm__("#SNFIX_SMALL todTimeOfDayOn");
__asm__("#SNFIX_SMALL todSetTimeOfDay");
__asm__("#SNFIX_SMALL todTimeOfDayMinutesPerSecond");
__asm__("#SNFIX_SMALL todTimeOfDay");
__asm__("#SNFIX_SMALL todFrame");
__asm__("#SNFIX_SMALL todAddFrame");
__asm__("#SNFIX_SMALL todDeleteFrame");
extern float todAmbientRed;
extern float todAmbientGreen;
extern float todAmbientBlue;
extern int todNumSkys;
extern int todSkySetFound;
extern _todSky todSky[10];
extern int s_todPlightsEnabled[];

int todActive(void);
float todCalculateValue(int i, float lo, float hi);

/* todUpdate's function statics: the clock (starting at 6:00) and its init flag. */
extern float todStartTime __asm__("startTOD.144");
extern int todClockInit __asm__("_$tmp_0.146");
extern float todClock __asm__("D_006F8DE8");
__asm__("#SNFIX_SMALL startTOD.144");
__asm__("#SNFIX_SMALL _$tmp_0.146");
__asm__("#SNFIX_SMALL D_006F8DE8");
extern float D_0025B238[]; /* ambient color at [12..14] (lighting block of another TU) */

extern float todDirHeading;
extern float todDirPitch;
extern float todDirRed;
extern float todDirGreen;
extern float todDirBlue;
extern float todBackHeading;
extern float todBackPitch;
extern float todBackIntensity;
extern float todBackRed;
extern float todBackGreen;
extern float todBackBlue;
extern float todFogMinRange;
extern float todFogMaxRange;
extern float todFogMaxVal;
extern float todFogFarClip;
extern int todFogRed;
extern int todFogGreen;
extern int todFogBlue;
extern int todBackgroundRed;
extern int todBackgroundGreen;
extern int todBackgroundBlue;
extern float todSkyRedMod;
extern float todSkyGreenMod;
extern float todSkyBlueMod;
extern float todSkyFogBurnThru;
extern float todCloudRedMod;
extern float todCloudGreenMod;
extern float todCloudBlueMod;
extern float todCloudAlphaMod;

void todBoundAngle(float *angle);
void todSetSkyObjects(int which, float alpha, float zBufferFudge, float fogBurnThru, float red, float green, float blue);
void fogSetChanged(void);
void fogSetTODFogParms(float minRange, float maxRange, float maxVal, float farClip, int red, int green, int blue);
void plightUpdateParaLights(void);
void plightSetTODParaLight(int which, float red, float green, float blue, float heading, float pitch);
void viewSetSkyNode(HierHead *node, int layer);
void viewSetTODBgColor(unsigned long red, unsigned long green, unsigned long blue, unsigned long alpha);
void mathfHPtoVector(_fvector *v, float heading, float pitch);
void mathfRotationFromVector(_fvector *rot, _fvector *v);

void todBoundAngle(float *angle)
{
    while (*angle >= 180.0f)
        *angle -= 360.0f;
    while (*angle <= -180.0f)
        *angle += 360.0f;
}
/* Default key i of 12 (every two hours): cosine ramp from lo (midday) to hi (midnight). */
float todCalculateValue(int i, float lo, float hi)
{
    return lo + cosf(((float)i / 5.5f - 1.0f) * 1.5707964f) * (hi - lo);
}
void todInit(void)
{
    int i;

    todNumSkys = 0;
    todTimeOfDayMinutesPerSecond = 30.0f;
    todSkySetFound = 0;
    todTimeOfDayOn = 0;
    todSetTimeOfDay = 0;
    todTimeOfDay = 0.0f;
    todFrame = 0;
    todAddFrame = 0;
    todDeleteFrame = 0;
    for (i = 9; i >= 0; i--)
        todSky[i].hier = 0;
    for (i = 0; i < 12; i++) {
        todInfo[i].ambientRed = todCalculateValue(i, 10.0f, 30.0f);
        todInfo[i].ambientGreen = todCalculateValue(i, 10.0f, 30.0f);
        todInfo[i].ambientBlue = todCalculateValue(i, 10.0f, 30.0f);
        todInfo[i].fogMinRange = 3000.0f;
        todInfo[i].fogMaxRange = 8000.0f;
        todInfo[i].fogMaxVal = 255.0f;
        todInfo[i].fogFarClip = 8000.0f;
        todInfo[i].fogRed = todCalculateValue(i, 20.0f, 50.0f);
        todInfo[i].fogGreen = todCalculateValue(i, 20.0f, 50.0f);
        todInfo[i].fogBlue = todCalculateValue(i, 20.0f, 50.0f);
        todInfo[i].bgRed = todCalculateValue(i, 20.0f, 90.0f);
        todInfo[i].bgGreen = todCalculateValue(i, 20.0f, 90.0f);
        todInfo[i].bgBlue = todCalculateValue(i, 20.0f, 90.0f);
        todInfo[i].dirHeading = 0.0f;
        todInfo[i].dirRed = todCalculateValue(i, 70.0f, 70.0f);
        todInfo[i].dirGreen = todCalculateValue(i, 70.0f, 200.0f);
        todInfo[i].dirBlue = todCalculateValue(i, 70.0f, 200.0f);
        todInfo[i].backIntensity = 20.0f;
        todInfo[i].backRed = 128.0f;
        todInfo[i].backGreen = 128.0f;
        todInfo[i].backBlue = 128.0f;
        todInfo[i].skyFogBurnThru = 0.0f;
        todInfo[i].skyRedMod = 1.0f;
        todInfo[i].skyGreenMod = 1.0f;
        todInfo[i].skyBlueMod = 1.0f;
        todInfo[i].cloudRedMod = 1.0f;
        todInfo[i].cloudGreenMod = 1.0f;
        todInfo[i].cloudBlueMod = 1.0f;
        todInfo[i].cloudAlphaMod = 1.0f;
    }
    todInfo[0].time = 0.0f;
    todInfo[1].time = 2.0f;
    todInfo[2].time = 4.0f;
    todInfo[3].time = 6.0f;
    todInfo[4].time = 8.0f;
    todInfo[5].time = 10.0f;
    todInfo[6].time = 12.0f;
    todInfo[7].time = 14.0f;
    todInfo[8].time = 16.0f;
    todInfo[9].time = 18.0f;
    todInfo[10].time = 20.0f;
    todInfo[11].time = 22.0f;
    todInfo[0].sky = 2;
    todInfo[1].sky = 2;
    todInfo[2].sky = 2;
    todInfo[3].sky = 3;
    todInfo[4].sky = 3;
    todInfo[5].sky = 0;
    todInfo[6].sky = 0;
    todInfo[7].sky = 0;
    todInfo[8].sky = 0;
    todInfo[9].sky = 1;
    todInfo[10].sky = 1;
    todInfo[11].sky = 2;
    todInfo[0].dirPitch = 270.0f;
    todInfo[1].dirPitch = 300.0f;
    todInfo[2].dirPitch = 330.0f;
    todInfo[3].dirPitch = 0.0f;
    todInfo[4].dirPitch = 30.0f;
    todInfo[5].dirPitch = 60.0f;
    todInfo[6].dirPitch = 90.0f;
    todInfo[7].dirPitch = 120.0f;
    todInfo[8].dirPitch = 150.0f;
    todInfo[9].dirPitch = 180.0f;
    todInfo[10].dirPitch = 210.0f;
    todInfo[11].dirPitch = 240.0f;
}
void todInitStats(void)
{
}
/* Advances the clock and blends the two keys around it into the ambient/sun/back lights, fog, clear color and
 * sky objects. Players other than the first (which > 0) only refresh their parallel lights. */
void todUpdate(int which)
{
    int cur;
    int next;
    float nextTime;
    float f;
    float a;
    float b;
    float d;
    _fvector dir;
    _fvector rot;
    int on;
    float *light;
    float none;

    if (!todClockInit) {
        todClockInit = 1;
        todClock = todStartTime + 0.0f;
    }
    light = D_0025B238;
    if (todSetTimeOfDay) {
        if (!todTimeOfDayOn)
            todTimeOfDayOn = 1;
    } else if (todTimeOfDayOn) {
        todTimeOfDayOn = 0;
        fogSetChanged();
        plightUpdateParaLights();
        todSetSkyObjects(0, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);
        viewSetSkyNode(todSky[0].hier, 0);
        viewSetSkyNode(0, 1);
        if (todSky[4].hier)
            todSetSkyObjects(4, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    }
    if (!todTimeOfDayOn)
        return;

    todClock += todTimeOfDayMinutesPerSecond / 60.0f / 60.0f;
    if (todClock > 24.0f)
        todClock -= 24.0f;
    todTimeOfDay = todClock;
    cur = (int)(todClock * 0.5f);
    next = cur + 1;
    if (next >= 12)
        next = 0;
    if (next == 0)
        nextTime = 24.0f;
    else
        nextTime = todInfo[next].time;
    f = (todClock - todInfo[cur].time) / (nextTime - todInfo[cur].time);

    light[12] = todAmbientRed = todInfo[cur].ambientRed + f * (todInfo[next].ambientRed - todInfo[cur].ambientRed);
    light[13] = todAmbientGreen = todInfo[cur].ambientGreen + f * (todInfo[next].ambientGreen - todInfo[cur].ambientGreen);
    light[14] = todAmbientBlue = todInfo[cur].ambientBlue + f * (todInfo[next].ambientBlue - todInfo[cur].ambientBlue);

    none = -1.0f;
    a = todInfo[cur].dirHeading;
    b = todInfo[next].dirHeading;
    todBoundAngle(&a);
    todBoundAngle(&b);
    d = b - a;
    todBoundAngle(&d);
    todDirHeading = a + f * d;
    a = todInfo[cur].dirPitch;
    b = todInfo[next].dirPitch;
    todBoundAngle(&a);
    todBoundAngle(&b);
    d = b - a;
    todBoundAngle(&d);
    todDirPitch = a + f * d;

    /* the back light points the opposite way */
    mathfHPtoVector(&dir, todDirHeading, todDirPitch);
    dir.x = -dir.x;
    dir.y = -dir.y;
    dir.z = -dir.z;
    mathfRotationFromVector(&rot, &dir);
    todBackIntensity = (todInfo[cur].backIntensity + f * (todInfo[next].backIntensity - todInfo[cur].backIntensity)) / 100.0f;
    todDirRed = todInfo[cur].dirRed + f * (todInfo[next].dirRed - todInfo[cur].dirRed);
    todDirGreen = todInfo[cur].dirGreen + f * (todInfo[next].dirGreen - todInfo[cur].dirGreen);
    todDirBlue = todInfo[cur].dirBlue + f * (todInfo[next].dirBlue - todInfo[cur].dirBlue);
    todBackHeading = rot.z * 57.29578f;
    todBackPitch = rot.x * 57.29578f;
    plightSetTODParaLight(0, todDirRed, todDirGreen, todDirBlue, todDirHeading, todDirPitch);

    if (todInfo[cur].backIntensity != none && todInfo[next].backIntensity != none) {
        todBackRed = todDirRed * todBackIntensity;
        todBackGreen = todDirGreen * todBackIntensity;
        todBackBlue = todDirBlue * todBackIntensity;
    } else if (todInfo[cur].backIntensity != -1.0f && todInfo[next].backIntensity == -1.0f) {
        float k = todInfo[cur].backIntensity / 100.0f;

        todDirRed = todInfo[cur].dirRed * k;
        todDirGreen = todInfo[cur].dirGreen * k;
        todDirBlue = todInfo[cur].dirBlue * k;
        todBackRed = todDirRed + f * (todInfo[next].backRed - todDirRed);
        todBackGreen = todDirGreen + f * (todInfo[next].backGreen - todDirGreen);
        todBackBlue = todDirBlue + f * (todInfo[next].backBlue - todDirBlue);
    } else if (todInfo[cur].backIntensity == -1.0f && todInfo[next].backIntensity != -1.0f) {
        float k = todInfo[next].backIntensity / 100.0f;

        todDirRed = todInfo[next].dirRed * k;
        todDirGreen = todInfo[next].dirGreen * k;
        todDirBlue = todInfo[next].dirBlue * k;
        todBackRed = todInfo[cur].backRed + f * (todDirRed - todInfo[cur].backRed);
        todBackGreen = todInfo[cur].backGreen + f * (todDirGreen - todInfo[cur].backGreen);
        todBackBlue = todInfo[cur].backBlue + f * (todDirBlue - todInfo[cur].backBlue);
    } else {
        todBackRed = todInfo[cur].backRed + f * (todInfo[next].backRed - todInfo[cur].backRed);
        todBackGreen = todInfo[cur].backGreen + f * (todInfo[next].backGreen - todInfo[cur].backGreen);
        todBackBlue = todInfo[cur].backBlue + f * (todInfo[next].backBlue - todInfo[cur].backBlue);
    }
    plightSetTODParaLight(1, todBackRed, todBackGreen, todBackBlue, todBackHeading, todBackPitch);
    plightSetTODParaLight(2, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    on = todTimeOfDayOn;
    todTimeOfDayOn = s_todPlightsEnabled[which];
    s_todPlightsEnabled[which] = 1;
    plightUpdateParaLights();
    todTimeOfDayOn = on;
    if (which > 0)
        return;

    todFogMinRange = todInfo[cur].fogMinRange + f * (todInfo[next].fogMinRange - todInfo[cur].fogMinRange);
    todFogMaxRange = todInfo[cur].fogMaxRange + f * (todInfo[next].fogMaxRange - todInfo[cur].fogMaxRange);
    todFogMaxVal = todInfo[cur].fogMaxVal + f * (todInfo[next].fogMaxVal - todInfo[cur].fogMaxVal);
    todFogFarClip = todInfo[cur].fogFarClip + f * (todInfo[next].fogFarClip - todInfo[cur].fogFarClip);
    todFogRed = (int)(todInfo[cur].fogRed + f * (todInfo[next].fogRed - todInfo[cur].fogRed));
    todFogGreen = (int)(todInfo[cur].fogGreen + f * (todInfo[next].fogGreen - todInfo[cur].fogGreen));
    todFogBlue = (int)(todInfo[cur].fogBlue + f * (todInfo[next].fogBlue - todInfo[cur].fogBlue));
    fogSetTODFogParms(todFogMinRange, todFogMaxRange, todFogMaxVal, todFogFarClip, todFogRed, todFogGreen, todFogBlue);
    todBackgroundRed = (int)(todInfo[cur].bgRed + f * (todInfo[next].bgRed - todInfo[cur].bgRed));
    todBackgroundGreen = (int)(todInfo[cur].bgGreen + f * (todInfo[next].bgGreen - todInfo[cur].bgGreen));
    todBackgroundBlue = (int)(todInfo[cur].bgBlue + f * (todInfo[next].bgBlue - todInfo[cur].bgBlue));
    viewSetTODBgColor(todBackgroundRed, todBackgroundGreen, todBackgroundBlue, 0);

    if (todSkySetFound == 1) {
        int curSky;
        int nextSky;

        todSkyRedMod = todInfo[cur].skyRedMod + f * (todInfo[next].skyRedMod - todInfo[cur].skyRedMod);
        todSkyGreenMod = todInfo[cur].skyGreenMod + f * (todInfo[next].skyGreenMod - todInfo[cur].skyGreenMod);
        todSkyBlueMod = todInfo[cur].skyBlueMod + f * (todInfo[next].skyBlueMod - todInfo[cur].skyBlueMod);
        todSkyFogBurnThru = todInfo[cur].skyFogBurnThru + f * (todInfo[next].skyFogBurnThru - todInfo[cur].skyFogBurnThru);
        todCloudRedMod = todInfo[cur].cloudRedMod + f * (todInfo[next].cloudRedMod - todInfo[cur].cloudRedMod);
        todCloudGreenMod = todInfo[cur].cloudGreenMod + f * (todInfo[next].cloudGreenMod - todInfo[cur].cloudGreenMod);
        todCloudBlueMod = todInfo[cur].cloudBlueMod + f * (todInfo[next].cloudBlueMod - todInfo[cur].cloudBlueMod);
        todCloudAlphaMod = todInfo[cur].cloudAlphaMod + f * (todInfo[next].cloudAlphaMod - todInfo[cur].cloudAlphaMod);
        todSetSkyObjects(4, todCloudAlphaMod, 0.0f, 1.0f, todCloudRedMod, todCloudGreenMod, todCloudBlueMod);
        curSky = todInfo[cur].sky;
        nextSky = todInfo[next].sky;
        if (curSky == nextSky) {
            todSetSkyObjects(nextSky, 1.0f, 0.0f, todSkyFogBurnThru, todSkyRedMod, todSkyGreenMod, todSkyBlueMod);
            if (todSky[nextSky].hier) {
                viewSetSkyNode(todSky[nextSky].hier, 0);
                viewSetSkyNode(0, 1);
            }
        } else {
            /* cross-fade the two sky sets */
            float fadeOut = 1.0f - f;
            float fadeIn;

            if (fadeOut < 0.0f)
                fadeOut = 0.0f;
            fadeIn = f;
            if (fadeIn > 1.0f)
                fadeIn = 1.0f;
            if (todSky[curSky].hier) {
                todSetSkyObjects(curSky, fadeOut, 2.98e-7f, todSkyFogBurnThru, todSkyRedMod, todSkyGreenMod, todSkyBlueMod);
                viewSetSkyNode(todSky[curSky].hier, 0);
            }
            if (todSky[nextSky].hier) {
                todSetSkyObjects(nextSky, fadeIn, 0.0f, todSkyFogBurnThru, todSkyRedMod, todSkyGreenMod, todSkyBlueMod);
                viewSetSkyNode(todSky[nextSky].hier, 1);
            }
        }
    } else if (todNumSkys > 0) {
        int i;

        for (i = 0; i < 10; i++) {
            if (todSky[i].hier) {
                viewSetSkyNode(todSky[i].hier, 0);
                viewSetSkyNode(0, 1);
                i = 10;
            }
        }
    }
}
int todActive(void)
{
    return todTimeOfDayOn;
}
#ifdef NON_MATCHING
/* 98% (16 differing words incl. relocs): the spilled loop counter is reloaded before the compare in retail */
void todGetTOD(int *on, float *minutesPerSecond, _todInfo14 *out)
{
    int i;

    *on = todTimeOfDayOn;
    *minutesPerSecond = todTimeOfDayMinutesPerSecond;
    for (i = 0; i < 12; i++, out++) {
        out->time = todInfo[i].time;
        out->ambientRed = todInfo[i].ambientRed;
        out->ambientGreen = todInfo[i].ambientGreen;
        out->ambientBlue = todInfo[i].ambientBlue;
        out->fogMinRange = todInfo[i].fogMinRange;
        out->fogMaxRange = todInfo[i].fogMaxRange;
        out->fogMaxVal = todInfo[i].fogMaxVal;
        out->fogFarClip = todInfo[i].fogFarClip;
        out->fogRed = todInfo[i].fogRed;
        out->fogGreen = todInfo[i].fogGreen;
        out->fogBlue = todInfo[i].fogBlue;
        out->bgRed = todInfo[i].bgRed;
        out->bgGreen = todInfo[i].bgGreen;
        out->bgBlue = todInfo[i].bgBlue;
        out->dirHeading = todInfo[i].dirHeading;
        out->dirPitch = todInfo[i].dirPitch;
        out->dirRed = todInfo[i].dirRed;
        out->dirGreen = todInfo[i].dirGreen;
        out->dirBlue = todInfo[i].dirBlue;
        out->backIntensity = todInfo[i].backIntensity;
        out->backRed = todInfo[i].backRed;
        out->backGreen = todInfo[i].backGreen;
        out->backBlue = todInfo[i].backBlue;
        out->sky = todInfo[i].sky;
        out->skyFogBurnThru = todInfo[i].skyFogBurnThru;
        out->skyRedMod = todInfo[i].skyRedMod;
        out->skyGreenMod = todInfo[i].skyGreenMod;
        out->skyBlueMod = todInfo[i].skyBlueMod;
        out->cloudRedMod = todInfo[i].cloudRedMod;
        out->cloudGreenMod = todInfo[i].cloudGreenMod;
        out->cloudBlueMod = todInfo[i].cloudBlueMod;
        out->cloudAlphaMod = todInfo[i].cloudAlphaMod;
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/common/tod", todGetTOD__FPiPfP10_todInfo14);
#endif
/* Loads the time-of-day settings of a level: 12 keys in file format 1.0 to 1.4 (key times keep their defaults). */
#ifdef NON_MATCHING
/* 175/394: same per-version copies; the 1.3 and 1.4 loops strength-reduce different todInfo field addresses (register pressure) */
void todSetTOD(int on, float minutesPerSecond, void *data, float version)
{
    int i;

    if ((unsigned)on < 2)
        todSetTimeOfDay = on;
    else
        todSetTimeOfDay = 0;
    if (minutesPerSecond <= 60.0f && 0.0f <= minutesPerSecond)
        todTimeOfDayMinutesPerSecond = minutesPerSecond;
    else
        todTimeOfDayMinutesPerSecond = 30.0f;
    if (version == 1.0f) {
        _todInfo10 *src = (_todInfo10 *)data;

        for (i = 0; i < 12; i++, src++) {
            todInfo[i].ambientRed = src->ambientRed;
            todInfo[i].ambientGreen = src->ambientGreen;
            todInfo[i].ambientBlue = src->ambientBlue;
            todInfo[i].fogMinRange = src->fogMinRange;
            todInfo[i].fogRed = src->fogRed;
            todInfo[i].fogGreen = src->fogGreen;
            todInfo[i].fogBlue = src->fogBlue;
            todInfo[i].sky = src->sky;
        }
    }
    if (version == 1.1f) {
        _todInfo11 *src = (_todInfo11 *)data;

        for (i = 0; i < 12; i++, src++) {
            todInfo[i].ambientRed = src->ambientRed;
            todInfo[i].ambientGreen = src->ambientGreen;
            todInfo[i].ambientBlue = src->ambientBlue;
            todInfo[i].fogRed = src->fogRed;
            todInfo[i].fogGreen = src->fogGreen;
            todInfo[i].fogBlue = src->fogBlue;
            todInfo[i].fogMinRange = src->fogMinRange;
            todInfo[i].sky = src->sky;
            todInfo[i].skyFogBurnThru = src->skyFogBurnThru;
            todInfo[i].skyRedMod = src->skyRedMod;
            todInfo[i].skyGreenMod = src->skyGreenMod;
            todInfo[i].skyBlueMod = src->skyBlueMod;
        }
    }
    if (version == 1.2f) {
        _todInfo12 *src = (_todInfo12 *)data;

        for (i = 0; i < 12; i++, src++) {
            todInfo[i].ambientRed = src->ambientRed;
            todInfo[i].ambientGreen = src->ambientGreen;
            todInfo[i].ambientBlue = src->ambientBlue;
            todInfo[i].fogMinRange = src->fogMinRange;
            todInfo[i].fogMaxRange = src->fogMaxRange;
            todInfo[i].fogMaxVal = src->fogMaxVal;
            todInfo[i].fogFarClip = src->fogFarClip;
            todInfo[i].fogRed = src->fogRed;
            todInfo[i].fogGreen = src->fogGreen;
            todInfo[i].fogBlue = src->fogBlue;
            todInfo[i].dirHeading = src->dirHeading;
            todInfo[i].dirPitch = src->dirPitch;
            todInfo[i].dirRed = src->dirRed;
            todInfo[i].dirGreen = src->dirGreen;
            todInfo[i].dirBlue = src->dirBlue;
            todInfo[i].sky = src->sky;
            todInfo[i].skyFogBurnThru = src->skyFogBurnThru;
            todInfo[i].skyRedMod = src->skyRedMod;
            todInfo[i].skyGreenMod = src->skyGreenMod;
            todInfo[i].skyBlueMod = src->skyBlueMod;
        }
    }
    if (version == 1.3f) {
        _todInfo13 *src = (_todInfo13 *)data;

        for (i = 0; i < 12; i++, src++) {
            todInfo[i].ambientRed = src->ambientRed;
            todInfo[i].ambientGreen = src->ambientGreen;
            todInfo[i].ambientBlue = src->ambientBlue;
            todInfo[i].fogMinRange = src->fogMinRange;
            todInfo[i].fogMaxRange = src->fogMaxRange;
            todInfo[i].fogMaxVal = src->fogMaxVal;
            todInfo[i].fogFarClip = src->fogFarClip;
            todInfo[i].fogRed = src->fogRed;
            todInfo[i].fogGreen = src->fogGreen;
            todInfo[i].fogBlue = src->fogBlue;
            todInfo[i].dirHeading = src->dirHeading;
            todInfo[i].dirPitch = src->dirPitch;
            todInfo[i].dirRed = src->dirRed;
            todInfo[i].dirGreen = src->dirGreen;
            todInfo[i].dirBlue = src->dirBlue;
            todInfo[i].backIntensity = src->backIntensity;
            todInfo[i].sky = src->sky;
            todInfo[i].skyFogBurnThru = src->skyFogBurnThru;
            todInfo[i].skyRedMod = src->skyRedMod;
            todInfo[i].skyGreenMod = src->skyGreenMod;
            todInfo[i].skyBlueMod = src->skyBlueMod;
            todInfo[i].cloudRedMod = src->cloudRedMod;
            todInfo[i].cloudGreenMod = src->cloudGreenMod;
            todInfo[i].cloudBlueMod = src->cloudBlueMod;
            todInfo[i].cloudAlphaMod = src->cloudAlphaMod;
        }
    }
    if (version == 1.4f) {
        _todInfo14 *src = (_todInfo14 *)data;

        for (i = 0; i < 12; i++, src++) {
            todInfo[i].ambientRed = src->ambientRed;
            todInfo[i].ambientGreen = src->ambientGreen;
            todInfo[i].ambientBlue = src->ambientBlue;
            todInfo[i].fogMinRange = src->fogMinRange;
            todInfo[i].fogMaxRange = src->fogMaxRange;
            todInfo[i].fogMaxVal = src->fogMaxVal;
            todInfo[i].fogFarClip = src->fogFarClip;
            todInfo[i].fogRed = src->fogRed;
            todInfo[i].fogGreen = src->fogGreen;
            todInfo[i].fogBlue = src->fogBlue;
            todInfo[i].bgRed = src->bgRed;
            todInfo[i].bgGreen = src->bgGreen;
            todInfo[i].bgBlue = src->bgBlue;
            todInfo[i].dirHeading = src->dirHeading;
            todInfo[i].dirPitch = src->dirPitch;
            todInfo[i].dirRed = src->dirRed;
            todInfo[i].dirGreen = src->dirGreen;
            todInfo[i].dirBlue = src->dirBlue;
            todInfo[i].backIntensity = src->backIntensity;
            todInfo[i].backRed = src->backRed;
            todInfo[i].backGreen = src->backGreen;
            todInfo[i].backBlue = src->backBlue;
            todInfo[i].sky = src->sky;
            todInfo[i].skyFogBurnThru = src->skyFogBurnThru;
            todInfo[i].skyRedMod = src->skyRedMod;
            todInfo[i].skyGreenMod = src->skyGreenMod;
            todInfo[i].skyBlueMod = src->skyBlueMod;
            todInfo[i].cloudRedMod = src->cloudRedMod;
            todInfo[i].cloudGreenMod = src->cloudGreenMod;
            todInfo[i].cloudBlueMod = src->cloudBlueMod;
            todInfo[i].cloudAlphaMod = src->cloudAlphaMod;
        }
    }
}
#else
INCLUDE_ASM("asm/nonmatchings/common/tod", todSetTOD__FifPvf);
#endif
float todGetCurrentTOD(void)
{
    if (todActive())
        return todTimeOfDay;
    return -1.0f;
}
void todGetAmbient(float *r, float *g, float *b)
{
    *r = todAmbientRed;
    *g = todAmbientGreen;
    *b = todAmbientBlue;
}
/* Registers a sky group (its id selects the slot); its first three objects give the layer colors and UV/fog. */
void todSetSkyEntry(HierHead *node)
{
    if (todNumSkys < 10 && node->opcode == GROUP_NODE) {
        _hiergroup *group = (_hiergroup *)node;
        _todSky *sky = &todSky[node->id2];
        int i;

        sky->hier = node;
        for (i = 0; i < group->numKids; i++) {
            _hierobject *obj = (_hierobject *)group->child[i];

            if (group->child[i]->opcode == OBJECT_NODE && i < 3) {
                /* retail indexes the entry as rows of four floats: row i + 1 is layer[i], row i + 4 is mod[i] */
                float (*row)[4] = (float (*)[4])sky;

                row[i + 1][0] = obj->redAnim;
                row[i + 1][1] = obj->greenAnim;
                row[i + 1][2] = obj->blueAnim;
                row[i + 1][3] = obj->alphaAnim;
                row[i + 4][0] = obj->uOffset;
                row[i + 4][1] = obj->vOffset;
                row[i + 4][2] = obj->fogBurnThru;
                row[i + 4][3] = obj->zBufferFudge;
            }
        }
        todNumSkys++;
    }
    if (todSky[0].hier && todSky[1].hier && todSky[2].hier && todSky[3].hier)
        todSkySetFound = 1;
}
/* Applies a sky's stored layer values to its objects: colors scaled, z fudge / fog burn-through offset. */
void todSetSkyObjects(int which, float alpha, float zBufferFudge, float fogBurnThru, float red, float green, float blue)
{
    _todSky *sky = &todSky[which];
    _hiergroup *group = (_hiergroup *)sky->hier;
    float (*row)[4] = (float (*)[4])sky;
    int i;

    if (group == 0 || group->head.opcode != GROUP_NODE)
        return;
    for (i = 0; i < group->numKids; i++) {
        _hierobject *obj = (_hierobject *)group->child[i];

        if (group->child[i]->opcode == OBJECT_NODE) {
            obj->fogBurnThru = row[i + 4][2] + fogBurnThru;
            obj->zBufferFudge = row[i + 4][3] + zBufferFudge;
            obj->redAnim = row[i + 1][0] * red;
            obj->greenAnim = row[i + 1][1] * green;
            obj->blueAnim = row[i + 1][2] * blue;
            obj->alphaAnim = row[i + 1][3] * alpha;
        }
    }
}
void todSetPlightsActive(int which, int on)
{
    s_todPlightsEnabled[which] = on;
}
CTODLinkList::CTODLinkList(void)
{
    int i;

    numEntries = 0;
    head = -1;
    tail = -1;
    for (i = 0; i < 12; i++) {
        ClearNode(&nodes[i]);
        nodes[i].info = &todInfo[i];
    }
}
CTODLinkList::~CTODLinkList(void)
{
}
int CTODLinkList::AddEntry(void)
{
    if (numEntries < 12) {
        int idx = FindFreeNode();

        if (idx != -1 && head == -1) {
            tail = head = idx;
            numEntries++;
        }
    }
    return 0;
}
void CTODLinkList::DeleteEntry(int)
{
}
int CTODLinkList::GetNumEntries(void)
{
    return numEntries;
}
_LinkNode *CTODLinkList::FindNodeByTime(float)
{
    return 0;
}
int CTODLinkList::FindFreeNode(void)
{
    int i;

    for (i = 0; i < 12; i++) {
        if (nodes[i].next == -1)
            return i;
    }
    return -1;
}
void CTODLinkList::SortList(void)
{
}
void CTODLinkList::ClearNode(_LinkNode *node)
{
    node->next = -1;
    node->prev = -1;
    node->index = -1;
    node->time = -1;
    node->info = 0;
}
INCLUDE_ASM("asm/nonmatchings/common/tod", __static_initialization_and_destruction_0_00225688);
INCLUDE_ASM("asm/nonmatchings/common/tod", _GLOBAL_$I$todLink);
INCLUDE_ASM("asm/nonmatchings/common/tod", _GLOBAL_$D$todLink);
