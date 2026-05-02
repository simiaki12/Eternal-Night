#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

extern int gfxWidth, gfxHeight;

/* CRT post-processing toggles (0 = off, 1 = on) */
extern int g_crtScanlines;
extern int g_crtBleed;
extern int g_crtBlur;
extern int g_crtVignette;
extern int g_crtGrid;

void     gfxInit(HWND hwnd, int w, int h);
void     gfxResize(int w, int h);
void     gfxPresent(HWND hwnd);
void     gfxShutdown(void);
int      gfxIsFullscreen(void);
void     gfxSetFullscreen(int full);

uint32_t rgb(uint8_t r, uint8_t g, uint8_t b);
void     fillRect(int x, int y, int w, int h, uint32_t color);
void     clearScreen(void);
void     drawText(int x, int y, const char* text, uint32_t color, int scale);
void     drawTextOutlined(int x, int y, const char *text, uint32_t color, int scale);
void     drawSprite8(int x, int y, const uint8_t* data, const uint32_t* pal, int scale);
void     drawBin(int x, int y, const uint8_t *data, int scale, int rotate, uint8_t alpha);
void     drawBW(const uint8_t *data, uint32_t size, uint32_t color);
void     drawBWAt(int x, int y, int dstW, int dstH, const uint8_t *data, uint32_t size, uint32_t color, uint32_t color2);
void     drawIsoTile(int cx, int cy, uint32_t top, uint32_t side, int wall_h);
