#include "common.h"

/* A tweak file (AI/monster parameters) that can be re-read or written back at run time. The vtable pointer comes
   last (gcc 2.95 layout); the ctors and the type info stay in asm. */
class resetcom {
public:
    char m_data[0xC000];      /* 0x0000 file contents */
    char m_path[0x40];        /* 0xC000 */
    char m_fileName[0x40];    /* 0xC040 */
    int m_flag;               /* 0xC080 */
    char *m_buf;              /* 0xC084 */
    int m_creatorKey;         /* 0xC088 */
    int m_forceRead;          /* 0xC08C */
    int m_forceWrite;         /* 0xC090 */
    float m_version;          /* 0xC094 */
    void *m_vtable;           /* 0xC098 */

    int writeKeys(char *buf);
    void write(char *buf);
    int readKey(char *buf);
    void read(char *buf);
    int oldStyleRead(char *buf);
    void init(void);
    void update(void);
    void getFileAndPathname(int which);
    int validFileAndExtension(int read);
    void forceRead(void);
    void forceRead(int on);
    void forceWrite(void);
    void forceWrite(int on);
    void getFileName(char **name);
    void setFileName(char *name, bool flag);
};

/* Same object seen with its virtual methods (vtable slots 1..3), so that calls dispatch through the vtable like the
   retail. Nothing here is defined, so this file emits no vtable for it. */
class resetcomVt {
public:
    char m_fields[0xC098];
    virtual int writeKeys(char *buf);
    virtual int readKey(char *buf);
    virtual int oldStyleRead(char *buf);
};

/* Reset file: a header, then keys {type, size, data} up to an end key (type 1). */
#define RESET_MAGIC 1263096448.0f /* (float)'KIRR' */
#define RESET_VERSION 2.0f

struct ResetHeader {
    float magic;
    float version;
    float creator;
};

struct ResetKey {
    int type;
    int size;
};

struct BgColorKey { /* type 2 */
    int type, size;
    int r, g, b, a;
};

struct AmbientKey { /* type 3 */
    int type, size;
    float r, g, b;
};

struct FogKey { /* type 4 */
    int type, size;
    float minR, maxR, maxV, farClip;
    int r, g, b;
};

struct ParaLightKey { /* type 5 */
    int type, size;
    float r[3], g[3], b[3], h[3], p[3];
};

struct _todInfo14;
struct TodKey { /* type 7 */
    int type, size;
    float speed;
    int tod;
    float time;
    char info[0xA00];
};

extern "C" {
char *strcpy(char *dst, const char *src);
char *strcat(char *dst, const char *src);
int printf(const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
}
int fileWritef(char *name, void *buf, int size);
int fileReadf(char *name, void *dest);
void viewGetBgColor(int &r, int &g, int &b, int &a);
void viewSetBgColor(unsigned long r, unsigned long g, unsigned long b, unsigned long a);
void viewGetAmbient(float *r, float *g, float *b);
void viewSetAmbient(float r, float g, float b);
void fogGetFogParms(float *minR, float *maxR, float *maxV, float *farClip, int *r, int *g, int *b);
void fogSetFogParms(float minR, float maxR, float maxV, float farClip, int r, int g, int b);
void plightGetParaLight(int i, float *r, float *g, float *b, float *h, float *p);
void plightSetParaLight(int i, float r, float g, float b, float h, float p);
void plightUpdateParaLights(void);
void viewGetTOD(int *tod, float *time, _todInfo14 *info);
void viewSetTOD(int tod, float time, void *info, float speed);

extern char D_006F8940[]; /* "%d" */
extern char D_006F8948[]; /* "" */
extern char D_006F8950[]; /* "%s\\%s;1" */

INCLUDE_ASM("asm/nonmatchings/common/resetcom", __8resetcom);
INCLUDE_ASM("asm/nonmatchings/common/resetcom", __8resetcom11CREATOR_KEY);
int resetcom::writeKeys(char *buf)
{
    int off;
    int i;
    BgColorKey *bg;
    AmbientKey *amb;
    FogKey *fog;
    ParaLightKey *pl;
    TodKey *tod;

    bg = (BgColorKey *)buf;
    bg->type = 2;
    bg->size = 0x18;
    viewGetBgColor(bg->r, bg->g, bg->b, bg->a);
    off = bg->size;

    amb = (AmbientKey *)(buf + off);
    amb->type = 3;
    amb->size = 0x18;
    viewGetAmbient(&amb->r, &amb->g, &amb->b);
    off += amb->size;

    fog = (FogKey *)(buf + off);
    fog->type = 4;
    fog->size = 0x40;
    fogGetFogParms(&fog->minR, &fog->maxR, &fog->maxV, &fog->farClip, &fog->r, &fog->g, &fog->b);
    off += fog->size;

    pl = (ParaLightKey *)(buf + off);
    pl->type = 5;
    pl->size = 0x84;
    for (i = 0; i < 3; i++)
        plightGetParaLight(i, &pl->r[i], &pl->g[i], &pl->b[i], &pl->h[i], &pl->p[i]);
    off += pl->size;

    tod = (TodKey *)(buf + off);
    tod->type = 7;
    tod->size = 0xA14;
    tod->speed = 1.4f;
    viewGetTOD(&tod->tod, &tod->time, (_todInfo14 *)tod->info);
    return off + tod->size;
}
void resetcom::write(char *buf)
{
    ResetHeader *hdr = (ResetHeader *)buf;
    ResetKey *end;
    int size;

    hdr->magic = RESET_MAGIC;
    hdr->version = RESET_VERSION;
    hdr->creator = m_creatorKey;
    m_version = RESET_VERSION;
    size = ((resetcomVt *)this)->writeKeys(buf + sizeof(ResetHeader)) + sizeof(ResetHeader);
    end = (ResetKey *)(buf + size);
    end->type = 1;
    end->size = 8;
    size += 8;
    printf("Writing Reset File: Version %f, Creator = %f, Size = %d\n", m_version, hdr->creator, size);
}
int resetcom::readKey(char *buf)
{
    ResetKey *key = (ResetKey *)buf;
    int size = -1;
    int i;

    switch (key->type) {
    case 2: {
        BgColorKey *bg = (BgColorKey *)buf;
        viewSetBgColor(bg->r, bg->g, bg->b, bg->a);
        size = key->size;
        break;
    }
    case 3: {
        AmbientKey *amb = (AmbientKey *)buf;
        viewSetAmbient(amb->r, amb->g, amb->b);
        size = key->size;
        break;
    }
    case 4: {
        FogKey *fog = (FogKey *)buf;
        fogSetFogParms(fog->minR, fog->maxR, fog->maxV, fog->farClip, fog->r, fog->g, fog->b);
        size = key->size;
        break;
    }
    case 5: {
        ParaLightKey *pl = (ParaLightKey *)buf;
        for (i = 0; i < 3; i++)
            plightSetParaLight(i, pl->r[i], pl->g[i], pl->b[i], pl->h[i], pl->p[i]);
        plightUpdateParaLights();
        size = key->size;
        break;
    }
    case 7: {
        TodKey *tod = (TodKey *)buf;
        viewSetTOD(tod->tod, tod->time, tod->info, tod->speed);
        size = key->size;
        break;
    }
    }
    return size;
}
void resetcom::read(char *buf)
{
    ResetHeader *hdr = (ResetHeader *)buf;
    ResetKey *key;
    int numKeys = 0;
    int numUnknown = 0;
    int size = sizeof(ResetHeader);

    if (hdr->magic == RESET_MAGIC) {
        if (hdr->version >= 1.0f && hdr->version < RESET_VERSION) {
            ((resetcomVt *)this)->oldStyleRead(buf);
        } else if (hdr->version == RESET_VERSION) {
            key = (ResetKey *)(buf + size);
            while (key->type != 1) {
                if (((resetcomVt *)this)->readKey(buf + size) > 0)
                    numKeys++;
                else
                    numUnknown++;
                size += key->size;
                key = (ResetKey *)(buf + size);
            }
            size += key->size;
        } else {
            printf("Found an illegal version of the reset file, please rebuild it.\n");
        }
    } else {
        printf("This is not a valid reset file, please rebuild it.\n");
    }
    m_version = hdr->version;
    printf("Reading a Reset File, Version: %f, Creator: %f, Size: %d, Num Keys: %d, Num Unknown: %d\n", m_version,
           hdr->creator, size, numKeys, numUnknown);
}
int resetcom::oldStyleRead(char *)
{
    return 0;
}
void resetcom::init(void)
{
}
void resetcom::update(void)
{
    m_buf = m_data;
    if (m_forceRead) {
        getFileAndPathname(m_forceRead);
        if (fileReadf(m_path, m_buf) <= 0)
            printf("ACCESS_DENIED to the reset file %s.\n", m_path);
        else
            read(m_buf);
        m_forceRead = 0;
        if (m_forceWrite)
            m_forceWrite = 0;
    } else if (m_forceWrite) {
        getFileAndPathname(m_forceWrite);
        printf("Saving reset file named : %s\n", m_path);
        write(m_buf);
        fileWritef(m_path, m_buf, 0xC000);
        m_forceWrite = 0;
    }
}
void resetcom::getFileAndPathname(int which)
{
    char name[64];
    char num[16];
    char *root;

    strcpy(name, m_fileName);
    if (m_creatorKey == 8 && which >= 2) {
        sprintf(num, D_006F8940, which);
        strcat(name, num);
    }
    root = D_006F8948;
    switch (m_creatorKey) {
    case 2:
        sprintf(m_path, "%s\\RST\\%s.MDR;1", root, name);
        break;
    case 4:
        sprintf(m_path, "%s\\RST\\%s.MRS;1", root, name);
        break;
    case 5:
        sprintf(m_path, "%s\\RST\\%s.CHR;1", root, name);
        break;
    case 6:
        sprintf(m_path, "%s\\RST\\%s.GBL;1", root, name);
        break;
    case 7:
        sprintf(m_path, D_006F8950, root, name);
        break;
    case 9:
        sprintf(m_path, "\\RST\\%s.RRS;1", name);
        break;
    default:
        sprintf(m_path, "\\RST\\%s.RST;1", name);
        break;
    }
}
int resetcom::validFileAndExtension(int read)
{
    int ok = 1;

    m_buf = m_data;
    getFileAndPathname(read);
    if (read) {
        if (fileReadf(m_path, m_buf) <= 0)
            ok = 0;
    }
    return ok;
}
void resetcom::forceRead(void)
{
    forceRead(1);
}
void resetcom::forceRead(int on)
{
    m_forceRead = on;
    m_forceWrite = 0;
    update();
}
void resetcom::forceWrite(void)
{
    forceWrite(1);
}
void resetcom::forceWrite(int on)
{
    m_forceWrite = on;
    m_forceRead = 0;
    update();
}
void resetcom::getFileName(char **name)
{
    *name = m_fileName;
}
void resetcom::setFileName(char *name, bool flag)
{
    char tmp[64];
    int i, j;

    strcpy(tmp, name);
    /* spaces are dropped */
    j = 0;
    for (i = 0; i < 64; i++)
        if (tmp[i] != ' ')
            m_fileName[j++] = tmp[i];
    m_fileName[j] = 0;
    m_flag = flag;
}
INCLUDE_ASM("asm/nonmatchings/common/resetcom", _vt$8resetcom);
INCLUDE_ASM("asm/nonmatchings/common/resetcom", __tf8resetcom);
