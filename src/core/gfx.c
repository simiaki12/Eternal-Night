#include "gfx.h"
#include "font8x8_basic.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

int gfxWidth  = 0;
int gfxHeight = 0;

static HDC       g_backDC;
static HBITMAP   g_backBmp;
static uint32_t* g_pixels;
static int       g_winW = 0;
static int       g_winH = 0;
static HWND      g_hwnd = NULL;
static int       g_fullscreen = 1;
static int       g_logW = 0, g_logH = 0;

/* ── CRT post-processing ── */
int g_crtScanlines = 1; // Enable scanline effect
int g_crtBleed     = 1; // Enable color bleed effect
int g_crtBlur      = 1; // Enable blur effect
int g_crtVignette  = 1; // Enable vignette effect
int g_crtGrid      = 0; // Disable pixel grid for a cleaner look

static uint8_t  *g_vigBuf  = NULL; /* precomputed vignette factors [0..255] */
static uint32_t *g_tempBuf = NULL; /* scratch buffer for bleed/blur passes  */

/* Screen-space CRT overlay (scanlines + pixel grid) — rebuilt when anything changes */
static HBITMAP g_slBmp      = NULL;
static HDC     g_slDC       = NULL;
static int     g_slW        = 0;
static int     g_slH        = 0;
static int     g_slLastScan = -1;
static int     g_slLastGrid = -1;

/* Composite buffer — game stretch + overlay composed here before single blit */
static HBITMAP g_compBmp = NULL;
static HDC     g_compDC  = NULL;
static int     g_compW   = 0;
static int     g_compH   = 0;

static void crtInitBuffers(void) {
    int n = gfxWidth * gfxHeight;
    g_vigBuf  = malloc(n);
    g_tempBuf = malloc(n * sizeof(uint32_t));
    if (!g_vigBuf || !g_tempBuf) return;

    /* Precompute vignette: center=255, corners≈128 (quadratic falloff). */
    int cx = gfxWidth  / 2;
    int cy = gfxHeight / 2;
    for (int y = 0; y < gfxHeight; y++) {
        int dy = y - cy;
        for (int x = 0; x < gfxWidth; x++) {
            int dx = x - cx;
            float nx = (float)dx / cx;
            float ny = (float)dy / cy;
            float dist = sqrtf(nx*nx + ny*ny); // normalized radial distance [0..~1.4]

            float radius = 0.33f;   // inner "safe zone" (middle third-ish)
            float maxDist = 1.0f;   // where full vignette kicks in

            float t = (dist - radius) / (maxDist - radius);
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;

            // Shape the curve (stronger edges, flat center)
            t = t * t * t; // quadratic (try t*t*t for even harsher edges)

            int minBright = 200; // Increase this value to make the edges darker
            int f = (int)(255 - t * (255 - minBright));
            if (f < 0) f = 0;
            g_vigBuf[y * gfxWidth + x] = (uint8_t)f;
        }
    }
}

static void applyCRT(void) {
    int n = gfxWidth * gfxHeight;

    /* Color bleed: R smears from left neighbor, B from right (chromatic). */
    if (g_crtBleed && g_tempBuf) {
        memcpy(g_tempBuf, g_pixels, n * sizeof(uint32_t));
        for (int y = 0; y < gfxHeight; y++) {
            for (int x = 1; x < gfxWidth - 1; x++) {
                uint32_t c = g_tempBuf[y * gfxWidth + x];
                uint32_t l = g_tempBuf[y * gfxWidth + x - 1];
                uint32_t r = g_tempBuf[y * gfxWidth + x + 1];
                int rr = (((c>>16)&255)*3 + ((l>>16)&255)) >> 2;
                int gg =  ((c>> 8)&255);
                int bb = ((c&255)*3 + (r&255)) >> 2;
                g_pixels[y * gfxWidth + x] = ((uint32_t)rr<<16)|((uint32_t)gg<<8)|(uint32_t)bb;
            }
        }
    }

    /* Blur: gentle horizontal phosphor blur (2:1:1 weighted). */
    if (g_crtBlur && g_tempBuf) {
        memcpy(g_tempBuf, g_pixels, n * sizeof(uint32_t));
        for (int y = 0; y < gfxHeight; y++) {
            for (int x = 1; x < gfxWidth - 1; x++) {
                uint32_t c = g_tempBuf[y * gfxWidth + x];
                uint32_t l = g_tempBuf[y * gfxWidth + x - 1];
                uint32_t r = g_tempBuf[y * gfxWidth + x + 1];
                int cr = (c>>16)&255, cg = (c>>8)&255, cb = c&255;
                int lr = (l>>16)&255, lg = (l>>8)&255, lb = l&255;
                int rr_ = (r>>16)&255, rg = (r>>8)&255, rb = r&255;

                int rr = cr + (lr + rr_) / 6;
                int gg = cg + (lg + rg) / 6;
                int bb = cb + (lb + rb) / 6;

                if (rr > 255) rr = 255;
                if (gg > 255) gg = 255;
                if (bb > 255) bb = 255;
                g_pixels[y * gfxWidth + x] = ((uint32_t)rr<<16)|((uint32_t)gg<<8)|(uint32_t)bb;
            }
        }
    }

    /* Vignette: multiply each pixel by precomputed factor. */
    if (g_crtVignette && g_vigBuf) {
        for (int i = 0; i < n; i++) {
            uint32_t c = g_pixels[i];
            int f = g_vigBuf[i];
            int rr = ((c>>16)&255)*f>>8;
            int gg = ((c>> 8)&255)*f>>8;
            int bb = ( c     &255)*f>>8;
            g_pixels[i] = ((uint32_t)rr<<16)|((uint32_t)gg<<8)|(uint32_t)bb;
        }
    }
}

void gfxInit(HWND hwnd, int w, int h) {
    gfxWidth  = w;
    gfxHeight = h;
    g_winW    = w;
    g_winH    = h;
    g_hwnd    = hwnd;
    g_logW    = w;
    g_logH    = h;

    HDC hdc  = GetDC(hwnd);
    g_backDC = CreateCompatibleDC(hdc);
    ReleaseDC(hwnd, hdc);

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = w;
    bmi.bmiHeader.biHeight      = -h;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    g_backBmp = CreateDIBSection(g_backDC, &bmi, DIB_RGB_COLORS, (void**)&g_pixels, NULL, 0);
    SelectObject(g_backDC, g_backBmp);
    crtInitBuffers();
}

void gfxPresent(HWND hwnd) {
    applyCRT();
    HDC hdc = GetDC(hwnd);

    /* Letterbox: scale logical buffer into window preserving aspect ratio */
    int dstW, dstH;
    if (g_winW * gfxHeight > g_winH * gfxWidth) {
        dstH = g_winH;
        dstW = gfxWidth * g_winH / gfxHeight;
    } else {
        dstW = g_winW;
        dstH = gfxHeight * g_winW / gfxWidth;
    }
    int offX = (g_winW - dstW) / 2;
    int offY = (g_winH - dstH) / 2;

    /* Composite buffer: resize when destination changes. */
    if (dstW != g_compW || dstH != g_compH) {
        if (g_compBmp) { DeleteObject(g_compBmp); g_compBmp = NULL; }
        if (g_compDC)  { DeleteDC(g_compDC);      g_compDC  = NULL; }
        g_compDC  = CreateCompatibleDC(hdc);
        g_compBmp = CreateCompatibleBitmap(hdc, dstW, dstH);
        SelectObject(g_compDC, g_compBmp);
        g_compW = dstW; g_compH = dstH;
    }

    /* 1. Stretch game buffer into composite. */
    SetStretchBltMode(g_compDC, COLORONCOLOR);
    StretchBlt(g_compDC, 0, 0, dstW, dstH, g_backDC, 0, 0, gfxWidth, gfxHeight, SRCCOPY);

    /* 2. Overlay: scanlines (odd rows) + pixel grid (odd columns). */
    if (g_crtScanlines || g_crtGrid) {
        if (dstW != g_slW || dstH != g_slH ||
            g_crtScanlines != g_slLastScan || g_crtGrid != g_slLastGrid) {
            if (g_slBmp) { DeleteObject(g_slBmp); g_slBmp = NULL; }
            if (g_slDC)  { DeleteDC(g_slDC);      g_slDC  = NULL; }
            g_slW = dstW; g_slH = dstH;
            g_slLastScan = g_crtScanlines; g_slLastGrid = g_crtGrid;

            BITMAPINFO bmi = {0};
            bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth       = dstW;
            bmi.bmiHeader.biHeight      = -dstH;
            bmi.bmiHeader.biPlanes      = 1;
            bmi.bmiHeader.biBitCount    = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            uint32_t *sl;
            g_slDC  = CreateCompatibleDC(hdc);
            g_slBmp = CreateDIBSection(g_slDC, &bmi, DIB_RGB_COLORS, (void**)&sl, NULL, 0);
            SelectObject(g_slDC, g_slBmp);

            const int SA = g_crtScanlines ? 89 : 0;
            const int GA = g_crtGrid      ? 89 : 0;
            for (int y = 0; y < dstH; y++) {
                int da = (y & 1) ? SA : 0;
                for (int x = 0; x < dstW; x++) {
                    int ca = (x & 1) ? GA : 0;
                    int a;
                    if      (!da) a = ca;
                    else if (!ca) a = da;
                    else          a = 255 - (255-da)*(255-ca)/255;
                    sl[y * dstW + x] = (uint32_t)a << 24;
                }
            }
        }
        BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        AlphaBlend(g_compDC, 0, 0, dstW, dstH, g_slDC, 0, 0, dstW, dstH, bf);
    }

    /* 3. Single blit of the fully-composed frame to the window. */
    HBRUSH black = GetStockObject(BLACK_BRUSH);
    if (offX > 0) {
        RECT r = {0, 0, offX, g_winH};              FillRect(hdc, &r, black);
        r = (RECT){offX+dstW, 0, g_winW, g_winH};   FillRect(hdc, &r, black);
    }
    if (offY > 0) {
        RECT r = {0, 0, g_winW, offY};              FillRect(hdc, &r, black);
        r = (RECT){0, offY+dstH, g_winW, g_winH};   FillRect(hdc, &r, black);
    }
    BitBlt(hdc, offX, offY, dstW, dstH, g_compDC, 0, 0, SRCCOPY);

    ReleaseDC(hwnd, hdc);
}

void gfxResize(int w, int h) {
    if (w <= 0 || h <= 0) return;
    g_winW = w;
    g_winH = h;
    /* Backbuffer stays at fixed logical size — no recreate needed */
}

int gfxIsFullscreen(void) { return g_fullscreen; }

void gfxSetFullscreen(int full) {
    if (full == g_fullscreen || !g_hwnd) return;
    g_fullscreen = full;
    if (full) {
        int monW = GetSystemMetrics(SM_CXSCREEN);
        int monH = GetSystemMetrics(SM_CYSCREEN);
        SetWindowLongA(g_hwnd, GWL_STYLE, WS_POPUP);
        SetWindowPos(g_hwnd, HWND_TOP, 0, 0, monW, monH, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        gfxResize(monW, monH);
    } else {
        RECT wr = {0, 0, g_logW, g_logH};
        AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
        int ww = wr.right - wr.left;
        int wh = wr.bottom - wr.top;
        int ox = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2;
        int oy = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;
        SetWindowLongA(g_hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);
        SetWindowPos(g_hwnd, HWND_TOP, ox, oy, ww, wh, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        gfxResize(g_logW, g_logH);
    }
}

void gfxShutdown(void) {
    free(g_vigBuf);
    free(g_tempBuf);
    if (g_slBmp)   DeleteObject(g_slBmp);
    if (g_slDC)    DeleteDC(g_slDC);
    if (g_compBmp) DeleteObject(g_compBmp);
    if (g_compDC)  DeleteDC(g_compDC);
    DeleteObject(g_backBmp);
    DeleteDC(g_backDC);
}

uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

void fillRect(int x, int y, int w, int h, uint32_t color) {
    int x1 = x < 0 ? 0 : x;
    int y1 = y < 0 ? 0 : y;
    int x2 = (x + w) > gfxWidth  ? gfxWidth  : (x + w);
    int y2 = (y + h) > gfxHeight ? gfxHeight : (y + h);
    for (int py = y1; py < y2; py++)
        for (int px = x1; px < x2; px++)
            g_pixels[py * gfxWidth + px] = color;
}

void clearScreen(void) {
    memset(g_pixels, 0, (size_t)gfxWidth * gfxHeight * 4);
}

static void drawChar(int x, int y, char c, uint32_t color, int scale) {
    if ((unsigned char)c >= 128) return;
    const unsigned char* glyph = font8x8_basic[(unsigned char)c];
    for (int row = 0; row < 8; row++)
        for (int col = 0; col < 8; col++)
            if ((glyph[row] >> col) & 1)
                fillRect(x + col * scale, y + row * scale, scale, scale, color);
}

void drawSprite8(int dx, int dy, const uint8_t* data, const uint32_t* pal, int scale) {
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            uint8_t idx = data[row * 8 + col];
            if (idx == 0) continue;
            uint32_t color = pal[idx];
            int px = dx + col * scale;
            int py = dy + row * scale;
            int x1 = px < 0 ? 0 : px;
            int y1 = py < 0 ? 0 : py;
            int x2 = px + scale > gfxWidth  ? gfxWidth  : px + scale;
            int y2 = py + scale > gfxHeight ? gfxHeight : py + scale;
            for (int sy = y1; sy < y2; sy++)
                for (int sx = x1; sx < x2; sx++)
                    g_pixels[sy * gfxWidth + sx] = color;
        }
    }
}

/* Built-in palettes matching tools/img_conv.c */
static const uint8_t g_builtin_pal[][16][3] = {
    /* 1 — Gameboy DMG */
    { {15,56,15},{48,98,48},{139,172,15},{155,188,15} },
    /* 2 — 4-shade grayscale */
    { {0,0,0},{85,85,85},{170,170,170},{255,255,255} },
    /* 3 — CGA */
    { {0,0,0},{0,0,170},{0,170,0},{0,170,170},
      {170,0,0},{170,0,170},{170,170,0},{170,170,170},
      {85,85,85},{85,85,255},{85,255,85},{85,255,255},
      {255,85,85},{255,85,255},{255,255,85},{255,255,255} },
};
static const int g_builtin_pal_size[] = { 4, 4, 16 };
#define N_BUILTIN_PAL 3

static int bin_bits_needed(int n) {
    int b = 1;
    while ((1 << b) < n) b++;
    return b;
}

/* Returns 1 if (x,y) falls inside the tile diamond+walls for a tile of size w×h */
static int tile_in_bounds(int x, int y, int w, int h) {
    int dh = w/2, hdh = w/4, wh = h-dh, cx = w/2;
    int r = y+1;
    if (r >= 1 && r < dh) {
        int span = (r <= hdh ? r : dh-r) * (w/hdh);
        if (x >= cx-span/2 && x < cx+span/2) return 1;
    }
    if (wh > 0) {
        if (x < cx) { int ty = hdh+x/2; if (y >= ty && y < ty+wh) return 1; }
        else { int col=x-cx; int ty=dh-1-col/2; if (y >= ty && y < ty+wh) return 1; }
    }
    return 0;
}

/* Shared decode buffer — filled by binDecode, consumed by draw* functions. */
static uint32_t g_pixbuf[256 * 256];

/* Decode a .bin image into g_pixbuf (0xFFFFFFFF = transparent).
 * Returns 1 on success; fills *out_w, *out_h, *out_tile_mode. */
static int binDecode(const uint8_t *data, int *out_w, int *out_h, int *out_tile_mode) {
    if (!data) return 0;
    int w     = data[0];
    int h     = data[1];
    int8_t cc = (int8_t)data[2];
    int n_colors, tile_mode = 0;
    uint32_t pal[127];
    const uint8_t *src = data + 3;

    if (cc < 0) {
        int id = (int)(-cc) - 1;
        if (id < 0 || id >= N_BUILTIN_PAL) return 0;
        n_colors = g_builtin_pal_size[id];
        for (int ci = 0; ci < n_colors; ci++)
            pal[ci] = rgb(g_builtin_pal[id][ci][0],
                          g_builtin_pal[id][ci][1],
                          g_builtin_pal[id][ci][2]);
    } else if (cc == 0) {
        tile_mode = 1;
        n_colors  = (int)(*src++);
        for (int ci = 0; ci < n_colors; ci++, src += 3)
            pal[ci] = rgb(src[0], src[1], src[2]);
    } else {
        n_colors = (int)cc;
        for (int ci = 0; ci < n_colors; ci++, src += 3)
            pal[ci] = rgb(src[0], src[1], src[2]);
    }

    int bpp  = bin_bits_needed(n_colors + 1);
    int mask = (1 << bpp) - 1;
    int bit_pos = 0;
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            int val;
            if (tile_mode && !tile_in_bounds(col, row, w, h)) {
                val = n_colors;
            } else {
                val = 0;
                for (int b = bpp-1; b >= 0; b--) {
                    if ((src[bit_pos/8] >> (7-(bit_pos%8))) & 1)
                        val |= (1 << b);
                    bit_pos++;
                }
                val &= mask;
            }
            g_pixbuf[row * w + col] = (val == n_colors) ? 0xFFFFFFFFu : pal[val];
        }
    }

    *out_w = w; *out_h = h; *out_tile_mode = tile_mode;
    return 1;
}

void drawBin(int x, int y, const uint8_t *data, int scale, int rotate, uint8_t alpha) {
    int w, h, tile_mode;
    if (!binDecode(data, &w, &h, &tile_mode)) return;

    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            int sc, sr;
            switch (rotate & 3) {
                default:
                case 0: sc = col;           sr = row;           break;
                case 1: sc = 2 * row;       sr = (w-1-col) / 2; break; /* 90° CW  */
                case 2: sc = w-1-col;       sr = h-1-row;       break; /* 180°    */
                case 3: sc = (w-1)-2*row;   sr = col / 2;       break; /* 90° CCW */
            }
            if (sc < 0 || sc >= w) continue;
            if (sr < 0) sr = 0;
            if (sr >= h) sr = h - 1;
            if (rotate && !tile_in_bounds(col, row, w, h)) continue;
            uint32_t color = g_pixbuf[sr * w + sc];
            if (color == 0xFFFFFFFFu && rotate) {
                for (int fsr = sr - 1; fsr >= 0 && color == 0xFFFFFFFFu; fsr--)
                    color = g_pixbuf[fsr * w + sc];
            }
            if (color == 0xFFFFFFFFu) continue;

            int px = x + col * scale;
            int py = y + row * scale;
            int x1 = px < 0 ? 0 : px;
            int y1 = py < 0 ? 0 : py;
            int x2 = px + scale > gfxWidth  ? gfxWidth  : px + scale;
            int y2 = py + scale > gfxHeight ? gfxHeight : py + scale;
            if (alpha == 255) {
                for (int sy = y1; sy < y2; sy++)
                    for (int sx = x1; sx < x2; sx++)
                        g_pixels[sy * gfxWidth + sx] = color;
            } else {
                uint8_t ia = 255 - alpha;
                uint8_t cr = (color >> 16) & 0xFF;
                uint8_t cg = (color >>  8) & 0xFF;
                uint8_t cb =  color        & 0xFF;
                for (int sy = y1; sy < y2; sy++)
                    for (int sx = x1; sx < x2; sx++) {
                        uint32_t bg = g_pixels[sy * gfxWidth + sx];
                        uint8_t r = (cr * alpha + ((bg >> 16) & 0xFF) * ia) >> 8;
                        uint8_t g = (cg * alpha + ((bg >>  8) & 0xFF) * ia) >> 8;
                        uint8_t b = (cb * alpha + ( bg        & 0xFF) * ia) >> 8;
                        g_pixels[sy * gfxWidth + sx] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
                    }
            }
        }
    }
}

/* Minimum tile opacity inside a radial window (~10%). Tiles never vanish completely. */
#define BIN_RADIAL_MIN_ALPHA 100

/* Alpha for one radial source at pixel centre (pcx, pcy). Quadratic ease-in. */
static inline uint8_t radialAlpha(int px, int py, int pcx, int pcy,
                                   float ri, float span, float ro_sq) {
    float dx = (float)(px - pcx);
    float dy = (float)(py - pcy);
    float d2 = dx*dx + dy*dy;
    if (d2 <= ri * ri) return BIN_RADIAL_MIN_ALPHA;
    if (d2 >= ro_sq)   return 255;
    float t = (sqrtf(d2) - ri) / span;
    t = t * t;
    return (uint8_t)(BIN_RADIAL_MIN_ALPHA + t * (255 - BIN_RADIAL_MIN_ALPHA));
}

/* Draw a .bin tile with per-pixel alpha based on screen-space distance from (pcx, pcy).
 * Within r_inner: BIN_RADIAL_MIN_ALPHA. Beyond r_outer: fully opaque.
 * Pass pcx2 >= 0 for a second radial source (e.g. an enemy); the more transparent
 * value wins per pixel so both circles are visible simultaneously. */
void drawBinRadial(int x, int y, const uint8_t *data, int scale, int rotate,
                   int pcx,  int pcy,  int r_inner,  int r_outer,
                   int pcx2, int pcy2, int r_inner2, int r_outer2) {
    int w, h, tile_mode;
    if (!binDecode(data, &w, &h, &tile_mode)) return;

    float span1  = (float)(r_outer  - r_inner);  if (span1  < 1.0f) span1  = 1.0f;
    float span2  = (float)(r_outer2 - r_inner2); if (span2  < 1.0f) span2  = 1.0f;
    float ri1    = (float)r_inner;
    float ro1_sq = (float)(r_outer  * r_outer);
    float ri2    = (float)r_inner2;
    float ro2_sq = (float)(r_outer2 * r_outer2);

    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            int sc, sr;
            switch (rotate & 3) {
                default:
                case 0: sc = col;           sr = row;           break;
                case 1: sc = 2 * row;       sr = (w-1-col) / 2; break;
                case 2: sc = w-1-col;       sr = h-1-row;       break;
                case 3: sc = (w-1)-2*row;   sr = col / 2;       break;
            }
            if (sc < 0 || sc >= w) continue;
            if (sr < 0) sr = 0;
            if (sr >= h) sr = h - 1;
            if (rotate && !tile_in_bounds(col, row, w, h)) continue;
            uint32_t color = g_pixbuf[sr * w + sc];
            if (color == 0xFFFFFFFFu && rotate) {
                for (int fsr = sr - 1; fsr >= 0 && color == 0xFFFFFFFFu; fsr--)
                    color = g_pixbuf[fsr * w + sc];
            }
            if (color == 0xFFFFFFFFu) continue;

            int px = x + col * scale;
            int py = y + row * scale;
            int pcx_px = px + scale / 2;
            int pcy_px = py + scale / 2;

            uint8_t alpha = radialAlpha(pcx_px, pcy_px, pcx, pcy, ri1, span1, ro1_sq);
            if (pcx2 >= 0) {
                uint8_t a2 = radialAlpha(pcx_px, pcy_px, pcx2, pcy2, ri2, span2, ro2_sq);
                if (a2 < alpha) alpha = a2;
            }

            int x1 = px < 0 ? 0 : px;
            int y1 = py < 0 ? 0 : py;
            int x2 = px + scale > gfxWidth  ? gfxWidth  : px + scale;
            int y2 = py + scale > gfxHeight ? gfxHeight : py + scale;
            if (alpha == 255) {
                for (int sy = y1; sy < y2; sy++)
                    for (int sx = x1; sx < x2; sx++)
                        g_pixels[sy * gfxWidth + sx] = color;
            } else {
                uint8_t ia = 255 - alpha;
                uint8_t cr = (color >> 16) & 0xFF;
                uint8_t cg = (color >>  8) & 0xFF;
                uint8_t cb =  color        & 0xFF;
                for (int sy = y1; sy < y2; sy++)
                    for (int sx = x1; sx < x2; sx++) {
                        uint32_t bg = g_pixels[sy * gfxWidth + sx];
                        uint8_t r = (cr * alpha + ((bg >> 16) & 0xFF) * ia) >> 8;
                        uint8_t g = (cg * alpha + ((bg >>  8) & 0xFF) * ia) >> 8;
                        uint8_t b = (cb * alpha + ( bg        & 0xFF) * ia) >> 8;
                        g_pixels[sy * gfxWidth + sx] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
                    }
            }
        }
    }
}

void drawBWAt(int x, int y, int dstW, int dstH,
              const uint8_t *data, uint32_t size, uint32_t color, uint32_t color2) {
    if (!data || size < 4) return;
    int imgW = (int)(data[0] | (data[1] << 8));
    int imgH = (int)(data[2] | (data[3] << 8));
    if (imgW <= 0 || imgH <= 0 || dstW <= 0 || dstH <= 0) return;

    int total = imgW * imgH;
    uint8_t *bits = malloc(total);
    if (!bits) return;

    int pixel = 0;
    uint8_t bit = 0;
    uint32_t pos = 4;
    while (pos < size && pixel < total) {
        int run = data[pos++];
        for (int i = 0; i < run && pixel < total; i++)
            bits[pixel++] = bit;
        bit ^= 1;
    }

    for (int py = 0; py < dstH; py++) {
        int gy = y + py;
        if (gy < 0 || gy >= gfxHeight) continue;
        int sy = py * imgH / dstH;
        for (int px = 0; px < dstW; px++) {
            int gx = x + px;
            if (gx < 0 || gx >= gfxWidth) continue;
            int sx = px * imgW / dstW;
            g_pixels[gy * gfxWidth + gx] = bits[sy * imgW + sx] ? color : color2;
        }
    }

    free(bits);
}

void drawBW(const uint8_t *data, uint32_t size, uint32_t color) {
    if (!data || size < 4) return;
    int imgW = (int)(data[0] | (data[1] << 8));
    int imgH = (int)(data[2] | (data[3] << 8));
    if (imgW <= 0 || imgH <= 0) return;

    /* Letterbox: largest fit preserving aspect ratio */
    int dstW, dstH;
    if (gfxWidth * imgH > gfxHeight * imgW) {
        dstH = gfxHeight;
        dstW = imgW * gfxHeight / imgH;
    } else {
        dstW = gfxWidth;
        dstH = imgH * gfxWidth / imgW;
    }
    int offX = (gfxWidth  - dstW) / 2;
    int offY = (gfxHeight - dstH) / 2;

    drawBWAt(offX, offY, dstW, dstH, data, size, color,0xFFFFFF);
}

void drawText(int x, int y, const char* text, uint32_t color, int scale) {
    for (int i = 0; text[i]; i++)
        drawChar(x + i * 8 * scale, y, text[i], color, scale);
}

void drawTextOutlined(int x, int y, const char *text, uint32_t color, int scale) {
    uint32_t black = rgb(0, 0, 0);
    drawText(x - 1, y,     text, black, scale);
    drawText(x + 1, y,     text, black, scale);
    drawText(x,     y - 1, text, black, scale);
    drawText(x,     y + 1, text, black, scale);
    drawText(x,     y,     text, color, scale);
}
