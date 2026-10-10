#include "common.h"

#define FONT_COUNT 2

/* Positions and sizes are in 1/16 pixel (GS sub-pixels). */
struct Font {
    int unk0;                   /* 0x00 */
    unsigned short charWidth;   /* 0x04 */
    unsigned short unk6;        /* 0x06 */
    unsigned short width;       /* 0x08 character cell */
    unsigned short height;      /* 0x0A */
    unsigned short lineHeight;  /* 0x0C height + vertical spacing */
    unsigned short advance;     /* 0x0E width + horizontal spacing */
    unsigned short x;           /* 0x10 cursor */
    unsigned short y;           /* 0x12 */
    unsigned short lineX;       /* 0x14 x a new line returns to */
    unsigned short unk16;       /* 0x16 */
    unsigned char r, g, b, a;   /* 0x18 */
    int centered;               /* 0x1C */
    char accents[16];           /* 0x20 characters drawn over the previous one */
    float accentScale;          /* 0x30 */
    int unk34;                  /* 0x34 */
};

extern Font fonts[FONT_COUNT] __asm__("D_00735890");
extern unsigned long fontPacket[][2] __asm__("D_00735900"); /* GS packet, one quadword per entry */
extern int spriteTESTlocation;
extern int spriteFBAlocation;
extern int spritePRIMlocation;
__asm__("#SNFIX_SMALL spriteTESTlocation");
__asm__("#SNFIX_SMALL spriteFBAlocation");
__asm__("#SNFIX_SMALL spritePRIMlocation");

extern "C" {
char *strcpy(char *dst, const char *src);
char *strchr(const char *s, int c);
char *strpbrk(const char *s, const char *set);
unsigned int strlen(const char *s);
}
void fontSetColorGifTag(int font);
void fontSetSpacing(int font, int spacing);
int fontStringWidth(int font, char *str);
void fontSpritePrint(int font, char *str);
int viewGetCurView(void);
void viewGetCenter(int view, int *x, int *y);
void viewGetWH(int view, int *w, int *h);

INCLUDE_ASM("asm/nonmatchings/common/font", fontInit__F10_vramAddrsi);
INCLUDE_ASM("asm/nonmatchings/common/font", fontDmaFontData__Fv);
INCLUDE_ASM("asm/nonmatchings/common/font", fontSetColorGifTag__Fi);
void fontDimColor(int font)
{
    fonts[font].r >>= 1;
    fonts[font].g >>= 1;
    fonts[font].b >>= 1;
    fonts[font].a = 0x80;
    fontSetColorGifTag(font);
}
void fontSetHilightColor(int font)
{
    fonts[font].b = 0x80;
    fonts[font].r = 0xFF;
    fonts[font].g = 0xC0;
    fonts[font].a = 0x80;
    fontSetColorGifTag(font);
}
void fontSetColor(int font, int r, int g, int b, int a)
{
    fonts[font].r = r;
    fonts[font].g = g;
    fonts[font].b = b;
    fonts[font].a = a;
    fontSetColorGifTag(font);
}
void fontClearCutOut(void)
{
    fontPacket[spriteTESTlocation][0] = 0x3008D;
    fontPacket[spriteFBAlocation][0] = 0;
    fontPacket[spritePRIMlocation][0] = 0x156;
}
void fontSetCutOut(int ref)
{
    fontPacket[spriteTESTlocation][0] = ((unsigned long)ref << 4) | 0x3000D;
    fontPacket[spriteFBAlocation][0] = 1;
    fontPacket[spritePRIMlocation][0] = 0x116;
}
void fontSetDefaultColor(int font)
{
    fontSetColor(font, 0x80, 0x80, 0x40, 0x80);
}
void fontSetSize(int font, int size)
{
    if (font < FONT_COUNT) {
        if (font >= 0) {
            fonts[font].height = size * 8;
            fonts[font].width = size * 64 / 5;
        }
    }
}
void fontSetCharWidth(int font, int w)
{
    fonts[font].charWidth = w;
}
void fontSetDefaultSize(int font)
{
    switch (font) {
    case 0:
        fontSetSize(0, 16);
        fontSetSpacing(0, 2);
        break;
    case 1:
        fontSetSize(1, 12);
        fontSetSpacing(1, 1);
        break;
    }
}
void fontSetAccentCharacters(int font, char *chars, float scale)
{
    Font *f = &fonts[font];

    f->accentScale = scale;
    if (!chars)
        f->accents[0] = 0;
    else
        strcpy(f->accents, chars);
}
void fontSetSpacing(int font, int spacing)
{
    fonts[font].advance = fonts[font].width + spacing * 16;
}
int fontStringWidth(int font, char *str)
{
    Font *f = &fonts[font];
    int skip = 0;
    char *end, *p;
    int len;

    end = strchr(str, '\n');
    if (!end)
        end = str + strlen(str);
    len = end - str;
    /* 0x08 starts a two-byte control code that is not drawn */
    for (p = strchr(str, 8); p && p < end; p = strchr(p + 1, 8))
        skip += 2;
    /* accents are drawn over the previous character */
    if (f->accents[0])
        for (p = strpbrk(str, f->accents); p && p < end; p = strpbrk(p + 1, f->accents))
            skip++;
    return (len - skip) * fonts[font].advance;
}
void fontSpritePrintXY(int font, int x, int y, char *str)
{
    Font *f = &fonts[font];

    x <<= 4;
    f->lineX = x;
    f->x = x;
    f->y = y << 4;
    f->centered = 0;
    fontSpritePrint(font, str);
}
#ifdef NON_MATCHING
/* 21/36 words, same size: x and y parameters get swapped saved registers */
void fontSpritePrintRightXY(int font, int x, int y, char *str)
{
    Font *f = &fonts[font];
    int w = fontStringWidth(font, str);

    x = (x << 4) - w;
    y <<= 4;
    f->y = y;
    f->x = x;
    f->lineX = x;
    f->centered = 0;
    fontSpritePrint(font, str);
}
#else
INCLUDE_ASM("asm/nonmatchings/common/font", fontSpritePrintRightXY__FiiiPc);
#endif
#ifdef NON_MATCHING
/* 31/38 words, same size: x and y parameters get swapped saved registers */
void fontSpritePrintCenteredXY(int font, int x, int y, char *str)
{
    Font *f = &fonts[font];
    int w;

    f->centered = 1;
    w = fontStringWidth(font, str);
    x <<= 4;
    y <<= 4;
    f->lineX = x;
    f->x = x - (w >> 1);
    f->y = y;
    fontSpritePrint(font, str);
}
#else
INCLUDE_ASM("asm/nonmatchings/common/font", fontSpritePrintCenteredXY__FiiiPc);
#endif
void fontSpritePrintCentered(int font, char *str)
{
    Font *f = &fonts[font];

    f->centered = 1;
    f->x = f->lineX - (fontStringWidth(font, str) >> 1);
    fontSpritePrint(font, str);
}
#ifdef NON_MATCHING
/* 58/61 words: retail loads the view width before the divide-by-zero check */
void fontSetCharSizesToFitScreen(int font, int cols, int rows, float scaleX, float scaleY)
{
    Font *f = &fonts[font];
    int cx, cy, w, h;
    int advance, lineHeight;

    viewGetCenter(viewGetCurView(), &cx, &cy);
    viewGetWH(viewGetCurView(), &w, &h);
    advance = (w << 4) / cols;
    lineHeight = (h << 4) / rows;
    f->advance = advance;
    f->lineHeight = lineHeight;
    f->width = (int)(advance * scaleX);
    f->height = (int)(lineHeight * scaleY);
}
#else
INCLUDE_ASM("asm/nonmatchings/common/font", fontSetCharSizesToFitScreen__Fiiiff);
#endif
void fontSetCharSizesInPixels(int font, int w, int h, int spaceX, int spaceY)
{
    fonts[font].width = w << 4;
    fonts[font].height = h << 4;
    fonts[font].advance = (w + spaceX) << 4;
    fonts[font].lineHeight = (h + spaceY) << 4;
}
void fontSetCharSizesInSubPixels(int font, int w, int h, int spaceX, int spaceY)
{
    fonts[font].width = w;
    fonts[font].height = h;
    fonts[font].advance = w + spaceX;
    fonts[font].lineHeight = h + spaceY;
}
void fontSetCursorAtColumnRow(int font, int col, int row)
{
    Font *f = &fonts[font];

    col *= f->advance;
    row *= f->lineHeight;
    f->lineX = col;
    f->y = row;
    f->x = col;
}
void fontSetCursorAtRowColumn(int font, int row, int col)
{
    Font *f = &fonts[font];

    col *= f->advance;
    row *= f->lineHeight;
    f->lineX = col;
    f->y = row;
    f->x = col;
}
void fontSetCursorAtPixel(int font, int x, int y)
{
    fonts[font].lineX = x << 4;
    fonts[font].x = x << 4;
    fonts[font].y = y << 4;
}
void fontSetCursorAtSubPixel(int font, int x, int y)
{
    fonts[font].lineX = x;
    fonts[font].x = x;
    fonts[font].y = y;
}
INCLUDE_ASM("asm/nonmatchings/common/font", fontSpritePrint__FiPc);
INCLUDE_ASM("asm/nonmatchings/common/font", fontBuildPrim__FicP6QwData);
INCLUDE_ASM("asm/nonmatchings/common/font", fontInitPacket__FP6QwData10_vramAddrs);
INCLUDE_ASM("asm/nonmatchings/common/font", fontSetTexId__FP9_hierhead);
