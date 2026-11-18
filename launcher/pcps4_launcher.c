/*
 * PCPS4 launcher: a controller-driven dashboard shown before Wine starts.
 *
 * Built as its own module (launcher.prx) so it can be replaced from
 * /data/pcps4/sce_module without reinstalling the package.  The bootstrap
 * calls pcps4_launcher_run():
 *    0  a game was chosen; /data/pcps4_cmdline.txt and /data/pcps4_env.txt
 *       were written for it -> start Wine
 *    1  the player chose to quit
 *   -1  no display -> start Wine with the existing settings
 *
 * The library is one folder per game under /data/pcps4/games:
 *    pcps4.txt   key=value lines:
 *                  name=  info=  exe=  arg= (repeatable)  env= (repeatable)  color=rrggbb
 *    cover.rgb   optional square cover art   ("PCI1", u16 w, u16 h, RGB bytes)
 *    hero.rgb    optional background art     (same format, usually 960x540)
 * Settings shared by every game live in /data/pcps4/env_common.txt, and the
 * order games were last played in /data/pcps4/recent.txt.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>

#include "launcher_font.h"

#define LAUNCHER_VERSION "0.2"

static void llog(const char *fmt, long a, long b)
{
    FILE *f = fopen("/data/pcps4/launcher.log", "a");
    if (f) { fprintf(f, fmt, a, b); fputc('\n', f); fclose(f); }
}

/* ---- console services ---------------------------------------------------- */
int32_t sceVideoOutOpen(int32_t user, int32_t bus, int32_t index, const void *param);
int32_t sceVideoOutClose(int32_t handle);
int32_t sceVideoOutRegisterBuffers(int32_t handle, int32_t start, void *const *addrs, int32_t count, const void *attr);
int32_t sceVideoOutSubmitFlip(int32_t handle, int32_t index, uint32_t mode, int64_t arg);
void    sceVideoOutSetBufferAttribute(void *attr, uint32_t fmt, uint32_t tiling, uint32_t aspect,
                                      uint32_t w, uint32_t h, uint32_t pitch);
int32_t sceVideoOutSetFlipRate(int32_t handle, int32_t rate);
int32_t sceVideoOutAddFlipEvent(void *eq, int32_t handle, void *udata);
int32_t sceKernelCreateEqueue(void **eq, const char *name);
int32_t sceKernelDeleteEqueue(void *eq);
int32_t sceKernelWaitEqueue(void *eq, void *ev, int32_t num, int32_t *out, uint32_t *timeout_us);
int32_t sceKernelAllocateDirectMemory(int64_t start, int64_t end, size_t len, size_t align, int32_t type, int64_t *off);
int32_t sceKernelMapDirectMemory(void **addr, size_t len, int32_t prot, int32_t flags, int64_t off, size_t align);
int32_t sceKernelReleaseDirectMemory(int64_t off, size_t len);
int32_t sceKernelMunmap(void *addr, size_t len);
size_t  sceKernelGetDirectMemorySize(void);
int32_t sceKernelUsleep(uint32_t us);
int32_t sceUserServiceInitialize(void *param);
int32_t sceUserServiceGetInitialUser(int32_t *user);
int32_t sceUserServiceGetUserName(int32_t user, char *name, size_t size);
int32_t scePadInit(void);
int32_t scePadOpen(int32_t user, int32_t type, int32_t index, void *param);
int32_t scePadClose(int32_t handle);

typedef struct { uint16_t year, month, day, hour, minute, second; uint32_t microsecond; } RtcTime;
int32_t sceKernelLoadStartModule(const char *path, size_t argc, const void *argv, uint32_t flags, const void *opt, int *res);
int32_t sceKernelDlsym(int32_t handle, const char *name, void **addr);

/* The clock library is loaded on demand: a module that imports it directly
 * is refused when the process has not loaded it yet. */
static int32_t sceRtcGetCurrentClockLocalTime(RtcTime *t)
{
    static int32_t (*fn)(RtcTime *);
    static int tried;
    if (!tried)
    {
        int res = 0;
        int32_t h = sceKernelLoadStartModule("libSceRtc.sprx", 0, NULL, 0, NULL, &res);
        tried = 1;
        if (h >= 0) sceKernelDlsym(h, "sceRtcGetCurrentClockLocalTime", (void **)&fn);
    }
    return fn ? fn(t) : -1;
}

typedef struct
{
    uint32_t buttons;
    uint8_t  lx, ly, rx, ry, l2, r2;
    uint16_t padding;
    uint8_t  rest[120];
} PadData;
int32_t scePadReadState(int32_t handle, PadData *data);

enum { B_UP = 0x10, B_RIGHT = 0x20, B_DOWN = 0x40, B_LEFT = 0x80, B_OPTIONS = 0x08,
       B_CROSS = 0x4000, B_CIRCLE = 0x2000, B_TRIANGLE = 0x1000, B_SQUARE = 0x8000,
       B_L1 = 0x400, B_R1 = 0x800 };

/* ---- screen ---------------------------------------------------------------- */
#define SW 1920
#define SH 1080
#define FB_BYTES ((size_t)SW * SH * 4)
#define DMEM_BYTES (((FB_BYTES * 2) + 0x1FFFFF) & ~(size_t)0x1FFFFF)

static int32_t video = -1;
static void *flip_queue;
static void *dmem;
static int64_t dmem_off = -1;
static uint32_t *fb[2];
static int fb_cur;
static uint32_t *canvas;
static uint32_t *backdrop, *backdrop_old;   /* composed background, current and fading out */

static void video_close(void)
{
    if (video >= 0) sceVideoOutClose(video);
    video = -1;
    if (flip_queue) sceKernelDeleteEqueue(flip_queue);
    flip_queue = NULL;
    if (dmem) sceKernelMunmap(dmem, DMEM_BYTES);
    dmem = NULL;
    if (dmem_off >= 0) sceKernelReleaseDirectMemory(dmem_off, DMEM_BYTES);
    dmem_off = -1;
}

static int video_open(void)
{
    static uint8_t attr[64] __attribute__((aligned(16)));
    void *bufs[2];
    int32_t r;

    if ((video = sceVideoOutOpen(0xFF, 0, 0, NULL)) < 0) { llog("videoout open -> 0x%lx", (unsigned)video, 0); return -1; }
    if (sceKernelCreateEqueue(&flip_queue, "pcps4 launcher") < 0) { llog("equeue failed", 0, 0); goto fail; }
    sceVideoOutAddFlipEvent(flip_queue, video, NULL);
    if ((r = sceKernelAllocateDirectMemory(0, (int64_t)sceKernelGetDirectMemorySize(), DMEM_BYTES, 0x200000, 3, &dmem_off)) < 0)
    {
        llog("direct memory alloc -> 0x%lx (size %ld)", (unsigned)r, (long)DMEM_BYTES);
        dmem_off = -1;
        goto fail;
    }
    if ((r = sceKernelMapDirectMemory(&dmem, DMEM_BYTES, 0x33, 0, dmem_off, 0x200000)) < 0)
    {
        llog("direct memory map -> 0x%lx", (unsigned)r, 0);
        dmem = NULL;
        goto fail;
    }
    fb[0] = bufs[0] = dmem;
    fb[1] = bufs[1] = (uint8_t *)dmem + FB_BYTES;
    memset(dmem, 0, FB_BYTES * 2);
    /* 0x80000000: A8R8G8B8 sRGB; tiling 1: linear */
    sceVideoOutSetBufferAttribute(attr, 0x80000000u, 1, 0, SW, SH, SW);
    if ((r = sceVideoOutRegisterBuffers(video, 0, bufs, 2, attr)) < 0) { llog("register buffers -> 0x%lx", (unsigned)r, 0); goto fail; }
    sceVideoOutSetFlipRate(video, 0);
    return 0;
fail:
    video_close();
    return -1;
}

static void present(void)
{
    uint8_t ev[64];
    int32_t n = 0;
    uint32_t timeout = 100000;

    if (video < 0) return;
    memcpy(fb[fb_cur], canvas, FB_BYTES);
    sceVideoOutSubmitFlip(video, fb_cur, 1 /* vsync */, 0);
    sceKernelWaitEqueue(flip_queue, ev, 1, &n, &timeout);
    fb_cur ^= 1;
}

/* ---- colour ---------------------------------------------------------------- */
static inline uint32_t rgb(int r, int g, int b) { return 0xFF000000u | (r << 16) | (g << 8) | b; }
static inline int R(uint32_t c) { return (c >> 16) & 255; }
static inline int G(uint32_t c) { return (c >> 8) & 255; }
static inline int B(uint32_t c) { return c & 255; }

static inline uint32_t blend(uint32_t d, uint32_t c, int a)
{
    int ia = 255 - a;
    return rgb((R(c) * a + R(d) * ia) / 255, (G(c) * a + G(d) * ia) / 255, (B(c) * a + B(d) * ia) / 255);
}

static uint32_t scale_col(uint32_t c, int pct)
{
    return rgb(R(c) * pct / 100, G(c) * pct / 100, B(c) * pct / 100);
}

#define C_TEXT      rgb(242, 244, 248)
#define C_MUTED     rgb(168, 174, 188)
#define C_SURFACE   rgb(34, 37, 46)
#define C_SURFACE_2 rgb(48, 52, 64)
#define C_ACCENT    rgb(46, 160, 110)
#define C_INK       rgb(20, 22, 28)

/* ---- images ---------------------------------------------------------------- */
typedef struct { int w, h; uint8_t *px; } Image;   /* RGB, 3 bytes per pixel */

static int image_load(Image *img, const char *path)
{
    uint8_t hdr[8];
    FILE *f = fopen(path, "rb");
    size_t n;

    img->px = NULL;
    if (!f) return -1;
    if (fread(hdr, 1, 8, f) != 8 || memcmp(hdr, "PCI1", 4)) goto fail;
    img->w = hdr[4] | (hdr[5] << 8);
    img->h = hdr[6] | (hdr[7] << 8);
    if (img->w <= 0 || img->h <= 0 || img->w > 4096 || img->h > 4096) goto fail;
    n = (size_t)img->w * img->h * 3;
    if (!(img->px = malloc(n)) || fread(img->px, 1, n, f) != n) goto fail;
    fclose(f);
    return 0;
fail:
    free(img->px);
    img->px = NULL;
    fclose(f);
    return -1;
}

static inline uint32_t image_sample(const Image *img, int fx, int fy)   /* 16.16 fixed point, bilinear */
{
    int x = fx >> 16, y = fy >> 16, ax = (fx >> 8) & 255, ay = (fy >> 8) & 255;
    int x1 = x + 1 < img->w ? x + 1 : x, y1 = y + 1 < img->h ? y + 1 : y;
    const uint8_t *p00 = img->px + ((size_t)y * img->w + x) * 3, *p01 = img->px + ((size_t)y * img->w + x1) * 3;
    const uint8_t *p10 = img->px + ((size_t)y1 * img->w + x) * 3, *p11 = img->px + ((size_t)y1 * img->w + x1) * 3;
    int c[3];
    for (int i = 0; i < 3; i++)
    {
        int top = p00[i] * (256 - ax) + p01[i] * ax;
        int bot = p10[i] * (256 - ax) + p11[i] * ax;
        c[i] = (top * (256 - ay) + bot * ay) >> 16;
    }
    return rgb(c[0], c[1], c[2]);
}

/* ---- shapes ---------------------------------------------------------------- */
/* Coverage (0..255) of a rounded rectangle at (x, y) relative to its corner. */
static inline int rr_cover(int x, int y, int w, int h, int rad)
{
    int cx = x < rad ? rad - x : (x >= w - rad ? x - (w - rad - 1) : 0);
    int cy = y < rad ? rad - y : (y >= h - rad ? y - (h - rad - 1) : 0);
    if (!cx || !cy) return 255;
    {
        int d2 = (cx - 1) * (cx - 1) + (cy - 1) * (cy - 1);
        int r2 = (rad - 1) * (rad - 1);
        if (d2 <= r2) return 255;
        if (d2 >= r2 + 2 * rad) return 0;
        return 255 * (r2 + 2 * rad - d2) / (2 * rad);
    }
}

static void fill_rr(int x0, int y0, int w, int h, int rad, uint32_t c, int alpha)
{
    for (int y = 0; y < h; y++)
    {
        int py = y0 + y;
        if (py < 0 || py >= SH) continue;
        for (int x = 0; x < w; x++)
        {
            int px = x0 + x, a;
            if (px < 0 || px >= SW) continue;
            a = rr_cover(x, y, w, h, rad) * alpha / 255;
            if (a) canvas[py * SW + px] = a >= 255 ? c : blend(canvas[py * SW + px], c, a);
        }
    }
}

/* Vertical gradient inside a rounded rectangle. */
static void grad_rr(int x0, int y0, int w, int h, int rad, uint32_t top, uint32_t bot)
{
    for (int y = 0; y < h; y++)
    {
        int py = y0 + y;
        uint32_t c = blend(top, bot, y * 255 / (h > 1 ? h - 1 : 1));
        if (py < 0 || py >= SH) continue;
        for (int x = 0; x < w; x++)
        {
            int px = x0 + x, a;
            if (px < 0 || px >= SW) continue;
            a = rr_cover(x, y, w, h, rad);
            if (a) canvas[py * SW + px] = a >= 255 ? c : blend(canvas[py * SW + px], c, a);
        }
    }
}

/* A ring of thickness t around a rounded rectangle. */
static void ring_rr(int x0, int y0, int w, int h, int rad, int t, uint32_t c)
{
    for (int y = 0; y < h; y++)
    {
        int py = y0 + y;
        if (py < 0 || py >= SH) continue;
        for (int x = 0; x < w; x++)
        {
            int px = x0 + x, outer, inner, a;
            if (px < 0 || px >= SW) continue;
            if (x >= t + rad && x < w - t - rad && y >= t && y < h - t) { x = w - t - rad - 1; continue; }
            outer = rr_cover(x, y, w, h, rad);
            inner = (x >= t && y >= t && x < w - t && y < h - t) ? rr_cover(x - t, y - t, w - 2 * t, h - 2 * t, rad > t ? rad - t : 1) : 0;
            a = outer - inner;
            if (a > 0) canvas[py * SW + px] = a >= 255 ? c : blend(canvas[py * SW + px], c, a);
        }
    }
}

/* Draws img scaled to w x h with rounded corners. */
static void blit_rr(const Image *img, int x0, int y0, int w, int h, int rad)
{
    int sx = (img->w << 16) / w, sy = (img->h << 16) / h;
    for (int y = 0; y < h; y++)
    {
        int py = y0 + y;
        if (py < 0 || py >= SH) continue;
        for (int x = 0; x < w; x++)
        {
            int px = x0 + x, a;
            if (px < 0 || px >= SW) continue;
            a = rr_cover(x, y, w, h, rad);
            if (a)
            {
                uint32_t c = image_sample(img, x * sx, y * sy);
                canvas[py * SW + px] = a >= 255 ? c : blend(canvas[py * SW + px], c, a);
            }
        }
    }
}

/* ---- text ------------------------------------------------------------------ */
static int text_width(const LFont *f, const char *s)
{
    int w = 0;
    for (; *s; s++) w += f->g[lfont_index((unsigned char)*s)].adv;
    return w;
}

/* Draws s with its baseline at y; returns the x after the last glyph. */
static int text(const LFont *f, int x, int y, const char *s, uint32_t c, int alpha)
{
    for (; *s; s++)
    {
        const LGlyph *g = &f->g[lfont_index((unsigned char)*s)];
        for (int gy = 0; gy < g->h; gy++)
        {
            int py = y + g->yoff + gy;
            if (py < 0 || py >= SH) continue;
            const unsigned char *row = f->atlas + (size_t)(g->y + gy) * f->atlas_w + g->x;
            for (int gx = 0; gx < g->w; gx++)
            {
                int px = x + g->xoff + gx, a = row[gx] * alpha / 255;
                if (!a || px < 0 || px >= SW) continue;
                canvas[py * SW + px] = blend(canvas[py * SW + px], c, a);
            }
        }
        x += g->adv;
    }
    return x;
}

static int text_fit(const LFont *f, int x, int y, const char *s, int max_w, uint32_t c)
{
    char out[256];
    snprintf(out, sizeof(out), "%s", s);
    if (text_width(f, out) > max_w)
        for (size_t n = strlen(out); n > 0; )
        {
            char tmp[300];
            out[--n] = 0;
            while (n && out[n - 1] == ' ') out[--n] = 0;
            snprintf(tmp, sizeof(tmp), "%s...", out);
            if (text_width(f, tmp) <= max_w) { snprintf(out, sizeof(out), "%s", tmp); break; }
        }
    return text(f, x, y, out, c, 255);
}

/* Word-wraps s into lines no wider than max_w; returns the y after the last line. */
static int text_wrap(const LFont *f, int x, int y, int line_h, const char *s, int max_w, uint32_t c)
{
    char line[400] = "", word[128], test[400];
    const char *p = s;
    for (;;)
    {
        size_t n = 0;
        while (*p == ' ') p++;
        while (*p && *p != ' ' && n < sizeof(word) - 1) word[n++] = *p++;
        word[n] = 0;
        if (!n) break;
        snprintf(test, sizeof(test), "%s%s%s", line, line[0] ? " " : "", word);
        if (line[0] && text_width(f, test) > max_w)
        {
            text(f, x, y, line, c, 255);
            y += line_h;
            snprintf(line, sizeof(line), "%s", word);
        }
        else snprintf(line, sizeof(line), "%s", test);
    }
    if (line[0]) { text(f, x, y, line, c, 255); y += line_h; }
    return y;
}

/* ---- library --------------------------------------------------------------- */
#define MAX_GAMES 48
#define MAX_LIST 16
#define LIB_DIR "/data/pcps4/games"

typedef struct
{
    char id[64], name[96], info[96], exe[256];
    char args[MAX_LIST][160];
    char envs[MAX_LIST][200];
    int nargs, nenvs, recent;
    uint32_t color;
    Image cover;
} Game;

static Game *games;
static int ngames;

static void trim(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
}

static const uint32_t palette[] = { 0x2e7d6b, 0x3a5ba0, 0x8a3b5c, 0x9a6a2a, 0x4b4f9c, 0x2f7f9a, 0x7a4aa0, 0x6b7a2e };

static void load_game(const char *id)
{
    char path[384], line[512];
    FILE *f;
    Game *g;

    if (ngames >= MAX_GAMES) return;
    snprintf(path, sizeof(path), "%s/%s/pcps4.txt", LIB_DIR, id);
    if (!(f = fopen(path, "r"))) return;
    g = &games[ngames];
    memset(g, 0, sizeof(*g));
    snprintf(g->id, sizeof(g->id), "%s", id);
    snprintf(g->name, sizeof(g->name), "%s", id);
    g->color = palette[ngames % (sizeof(palette) / sizeof(palette[0]))] | 0xFF000000u;
    g->recent = 1 << 20;
    while (fgets(line, sizeof(line), f))
    {
        char *eq = strchr(line, '=');
        trim(line);
        if (line[0] == '#' || !eq) continue;
        *eq++ = 0;
        if (!strcmp(line, "name")) snprintf(g->name, sizeof(g->name), "%s", eq);
        else if (!strcmp(line, "info")) snprintf(g->info, sizeof(g->info), "%s", eq);
        else if (!strcmp(line, "exe")) snprintf(g->exe, sizeof(g->exe), "%s", eq);
        else if (!strcmp(line, "arg") && g->nargs < MAX_LIST) snprintf(g->args[g->nargs++], sizeof(g->args[0]), "%s", eq);
        else if (!strcmp(line, "env") && g->nenvs < MAX_LIST) snprintf(g->envs[g->nenvs++], sizeof(g->envs[0]), "%s", eq);
        else if (!strcmp(line, "color")) g->color = 0xFF000000u | ((uint32_t)strtoul(eq, NULL, 16) & 0xFFFFFF);
    }
    fclose(f);
    if (!g->exe[0]) return;
    snprintf(path, sizeof(path), "%s/%s/cover.rgb", LIB_DIR, id);
    image_load(&g->cover, path);
    ngames++;
}

static int by_recent(const void *a, const void *b)
{
    const Game *x = a, *y = b;
    if (x->recent != y->recent) return x->recent - y->recent;
    return strcasecmp(x->name, y->name);
}

static void free_library(void)
{
    for (int i = 0; i < ngames; i++) free(games[i].cover.px);
    ngames = 0;
}

static void scan_library(void)
{
    DIR *d;
    struct dirent *e;
    FILE *f;
    char line[128];
    int rank = 0;

    free_library();
    if ((d = opendir(LIB_DIR)))
    {
        while ((e = readdir(d)))
            if (e->d_name[0] != '.') load_game(e->d_name);
        closedir(d);
    }
    if ((f = fopen("/data/pcps4/recent.txt", "r")))
    {
        while (fgets(line, sizeof(line), f))
        {
            trim(line);
            for (int i = 0; i < ngames; i++)
                if (!strcmp(games[i].id, line) && games[i].recent > rank) games[i].recent = rank;
            rank++;
        }
        fclose(f);
    }
    qsort(games, ngames, sizeof(Game), by_recent);
}

static void remember_played(const Game *g)
{
    char ids[MAX_GAMES][64], line[128];
    int n = 0;
    FILE *f = fopen("/data/pcps4/recent.txt", "r");
    if (f)
    {
        while (n < MAX_GAMES - 1 && fgets(line, sizeof(line), f))
        {
            trim(line);
            if (line[0] && strcmp(line, g->id)) snprintf(ids[n++], sizeof(ids[0]), "%s", line);
        }
        fclose(f);
    }
    if (!(f = fopen("/data/pcps4/recent.txt", "w"))) return;
    fprintf(f, "%s\n", g->id);
    for (int i = 0; i < n; i++) fprintf(f, "%s\n", ids[i]);
    fclose(f);
}

/* ---- state ----------------------------------------------------------------- */
enum { TAB_HOME, TAB_LIBRARY, TAB_ADD, TAB_SETTINGS, NTABS };
static const char *tab_names[NTABS] = { "Home", "Library", "Add games", "Settings" };
enum { SET_QUIT, NSETTINGS };

#define FADE_STEPS 8

static struct
{
    int tab;             /* page shown */
    int in_tabs;         /* focus is on the tab bar */
    int sel;             /* focused game (Home, Library) or item */
    int lib_scroll;      /* first visible row in Library */
    int hero_for;        /* game whose art the backdrop shows, -1 none */
    int fade;            /* backdrop crossfade, 0..FADE_STEPS */
    const char *toast;
    int toast_frames;
    char user[32];
} ui;

/* ---- backdrop -------------------------------------------------------------- */
static void backdrop_plain(uint32_t *dst, uint32_t accent)
{
    /* near-black with a soft wash of the game's colour from the top right */
    uint32_t base = rgb(16, 17, 22);
    for (int y = 0; y < SH; y++)
        for (int x = 0; x < SW; x++)
        {
            int dx = (SW - x) / 4, dy = y / 3, d = dx * dx + dy * dy;
            int a = d < 90000 ? (90000 - d) / 1100 : 0;
            dst[y * SW + x] = a ? blend(base, accent, a) : base;
        }
}

static void backdrop_for(int gi)
{
    char path[384];
    Image hero = { 0 };
    uint32_t *save = canvas;

    memcpy(backdrop_old, backdrop, FB_BYTES);
    ui.fade = 0;
    ui.hero_for = gi;
    if (gi < 0 || gi >= ngames) { backdrop_plain(backdrop, rgb(40, 60, 70)); return; }
    snprintf(path, sizeof(path), "%s/%s/hero.rgb", LIB_DIR, games[gi].id);
    if (image_load(&hero, path) < 0) { backdrop_plain(backdrop, games[gi].color); return; }
    /* the art full screen, darkened so text stays readable */
    canvas = backdrop;
    blit_rr(&hero, 0, 0, SW, SH, 1);
    for (int y = 0; y < SH; y++)
        for (int x = 0; x < SW; x++)
        {
            int a = 120 + (SW - x) * 80 / SW;
            if (y > 600) a += (y - 600) * 70 / 480;
            if (a > 235) a = 235;
            canvas[y * SW + x] = blend(canvas[y * SW + x], rgb(10, 11, 15), a);
        }
    canvas = save;
    free(hero.px);
}

static void compose_backdrop(void)
{
    if (ui.fade >= FADE_STEPS) { memcpy(canvas, backdrop, FB_BYTES); return; }
    {
        int a = 255 * (ui.fade + 1) / FADE_STEPS;
        for (size_t i = 0; i < (size_t)SW * SH; i++) canvas[i] = blend(backdrop_old[i], backdrop[i], a);
    }
    ui.fade++;
}

/* ---- widgets --------------------------------------------------------------- */
#define MARGIN 120

static void tile(const Game *g, int x, int y, int size, int focused)
{
    if (focused)
    {
        ring_rr(x - 10, y - 10, size + 20, size + 20, 26, 5, C_TEXT);
    }
    if (g->cover.px) blit_rr(&g->cover, x, y, size, size, 16);
    else
    {
        /* no art: the game's colour, lit from the top, with its name set in type */
        const LFont *f = size >= 400 ? &font_tile_big : &font_tile;
        int lines = text_width(f, g->name) > size - 48 ? 2 : 1;
        grad_rr(x, y, size, size, 16, g->color, scale_col(g->color, 50));
        text_wrap(f, x + 24, y + size - 32 - (lines - 1) * (f->size + 8), f->size + 8, g->name, size - 48, C_TEXT);
    }
}

static void button_hint(int *x, int y, int sym, const char *label)
{
    char s[2] = { (char)sym, 0 };
    fill_rr(*x, y - 31, 42, 42, 21, rgb(255, 255, 255), 235);
    text(&font_ui, *x + 21 - text_width(&font_ui, s) / 2, y - 3, s, C_INK, 255);
    *x = text(&font_body, *x + 54, y, label, C_TEXT, 255) + 44;
}

static void draw_top_bar(void)
{
    int x = MARGIN;
    RtcTime t;

    fill_rr(x, 52, 64, 64, 32, C_ACCENT, 255);
    {
        char c = ui.user[0] ? ui.user[0] : 'P', ini[2] = { (char)(c >= 'a' && c <= 'z' ? c - 32 : c), 0 };
        text(&font_ui, x + 32 - text_width(&font_ui, ini) / 2, 95, ini, C_TEXT, 255);
    }
    x += 88;
    for (int i = 0; i < NTABS; i++)
    {
        int w = text_width(&font_ui, tab_names[i]) + 52;
        int focused = ui.in_tabs && ui.tab == i;
        if (focused) fill_rr(x, 56, w, 56, 28, C_TEXT, 255);
        else if (ui.tab == i) fill_rr(x, 56, w, 56, 28, rgb(255, 255, 255), 36);
        text(&font_ui, x + 26, 95, tab_names[i], focused ? C_INK : (ui.tab == i ? C_TEXT : C_MUTED), 255);
        x += w + 12;
    }
    if (sceRtcGetCurrentClockLocalTime(&t) >= 0)
    {
        char clock[16];
        int h = t.hour % 12;
        snprintf(clock, sizeof(clock), "%d:%02d %s", h ? h : 12, t.minute, t.hour < 12 ? "AM" : "PM");
        text(&font_ui, SW - MARGIN - text_width(&font_ui, clock), 95, clock, C_TEXT, 255);
    }
}

static void draw_bottom_bar(int can_select)
{
    int x = MARGIN, y = SH - 52;
    if (can_select) button_hint(&x, y, 1, "Select");
    if (!ui.in_tabs) button_hint(&x, y, 2, "Back");
    if (ui.tab == TAB_HOME || ui.tab == TAB_LIBRARY) button_hint(&x, y, 3, "Refresh");
    text(&font_body, x, y, "L1 / R1  Switch tab", C_MUTED, 255);
    if (ui.toast && ui.toast_frames > 0)
        text(&font_body, SW - MARGIN - text_width(&font_body, ui.toast), y, ui.toast, C_TEXT, 255);
}

/* Home: the most recently played game large, then the rest in two rows. */
#define HOME_Y 268
#define BIG 476
#define SMALL 226
#define GAP 24

static void home_pos(int i, int *x, int *y, int *size)
{
    if (i == 0) { *x = MARGIN; *y = HOME_Y; *size = BIG; return; }
    *x = MARGIN + BIG + GAP + (i - 1) / 2 * (SMALL + GAP);
    *y = HOME_Y + (i - 1) % 2 * (SMALL + GAP);
    *size = SMALL;
}

static void draw_home(void)
{
    int sx, sy, ss, scroll;

    text(&font_ui, MARGIN, 226, ngames ? "Jump back in" : "Welcome", C_TEXT, 255);
    if (!ngames)
    {
        text_wrap(&font_title, MARGIN, 420, 80, "Your games will show up here.", 1100, C_TEXT);
        text_wrap(&font_body, MARGIN, 520, 40, "Open Add games to see how to put a Windows game on this console.", 1000, C_MUTED);
        return;
    }
    home_pos(ui.sel, &sx, &sy, &ss);
    scroll = sx + ss > SW - MARGIN ? sx + ss - (SW - MARGIN) : 0;
    for (int i = 0; i < ngames; i++)
    {
        int x, y, s;
        home_pos(i, &x, &y, &s);
        x -= scroll;
        if (x > SW || x + s < 0 || (i == ui.sel && !ui.in_tabs)) continue;
        tile(&games[i], x, y, s, 0);
    }
    if (!ui.in_tabs) tile(&games[ui.sel], sx - scroll, sy, ss, 1);
    text_fit(&font_title, MARGIN, 860, games[ui.sel].name, SW - 2 * MARGIN, C_TEXT);
    text_fit(&font_body, MARGIN + 2, 912, games[ui.sel].info[0] ? games[ui.sel].info : "Windows game", SW - 2 * MARGIN, C_MUTED);
}

/* Library: every game in a grid. */
#define LIB_COLS 6
#define LIB_SIZE 240
#define LIB_GAP 32
#define LIB_Y 236
#define LIB_ROW_H (LIB_SIZE + 92)

static void draw_library(void)
{
    char buf[64];
    snprintf(buf, sizeof(buf), ngames == 1 ? "1 game" : "%d games", ngames);
    text(&font_ui, MARGIN, 196, "All games", C_TEXT, 255);
    text(&font_body, MARGIN + text_width(&font_ui, "All games") + 24, 196, buf, C_MUTED, 255);
    if (!ngames)
    {
        text_wrap(&font_body, MARGIN, 300, 40, "Nothing here yet. Open Add games to see how to add one.", 1100, C_MUTED);
        return;
    }
    {
        int row = ui.sel / LIB_COLS;
        if (row < ui.lib_scroll) ui.lib_scroll = row;
        if (row > ui.lib_scroll + 1) ui.lib_scroll = row - 1;
    }
    for (int i = 0; i < ngames; i++)
    {
        int row = i / LIB_COLS - ui.lib_scroll, col = i % LIB_COLS;
        int x = MARGIN + col * (LIB_SIZE + LIB_GAP), y = LIB_Y + row * LIB_ROW_H;
        int focused = !ui.in_tabs && i == ui.sel;
        if (row < 0 || row > 2) continue;
        tile(&games[i], x, y, LIB_SIZE, focused);
        text_fit(&font_small, x, y + LIB_SIZE + 44, games[i].name, LIB_SIZE, focused ? C_TEXT : C_MUTED);
    }
}

static void card(int x, int y, int w, int h, const char *title, const char *badge, const char *const *lines, int focused)
{
    int ty;
    fill_rr(x, y, w, h, 20, C_SURFACE, 240);
    if (focused) ring_rr(x - 9, y - 9, w + 18, h + 18, 28, 5, C_TEXT);
    {
        int tx = text(&font_head, x + 44, y + 84, title, C_TEXT, 255);
        if (badge)
        {
            int bw = text_width(&font_small, badge) + 32;
            fill_rr(tx + 20, y + 50, bw, 42, 21, C_SURFACE_2, 255);
            text(&font_small, tx + 36, y + 79, badge, C_MUTED, 255);
        }
    }
    ty = y + 160;
    for (int i = 0; lines[i]; i++) ty = text_wrap(&font_body, x + 44, ty, 42, lines[i], w - 88, C_MUTED) + 14;
}

static void draw_add(void)
{
    static const char *const copy[] = {
        "1.  Put the game in its own folder.",
        "2.  Add a text file named pcps4.txt with two lines: name=The Game and exe= its .exe path.",
        "3.  Copy the folder to /data/pcps4/games on this PS4 over FTP.",
        "4.  On Home, press Triangle to refresh.", NULL };
    static const char *const steam[] = {
        "Sign in to Steam and download games you own straight to this PS4.",
        "Not available yet.", NULL };
    text(&font_ui, MARGIN, 196, "Add games", C_TEXT, 255);
    card(MARGIN, 236, 820, 560, "Copy a game", NULL, copy, !ui.in_tabs && ui.sel == 0);
    card(MARGIN + 860, 236, 820, 560, "Steam", "Coming soon", steam, !ui.in_tabs && ui.sel == 1);
}

static void draw_settings(void)
{
    char buf[160];
    int focused = !ui.in_tabs && ui.sel == SET_QUIT;
    text(&font_ui, MARGIN, 196, "Settings", C_TEXT, 255);
    fill_rr(MARGIN, 236, 820, 100, 18, focused ? C_TEXT : C_SURFACE, 240);
    text(&font_ui, MARGIN + 40, 300, "Quit to PS4 home", focused ? C_INK : C_TEXT, 255);
    snprintf(buf, sizeof(buf), "PCPS4 launcher %s", LAUNCHER_VERSION);
    text(&font_ui, MARGIN, 440, buf, C_TEXT, 255);
    text_wrap(&font_body, MARGIN, 496, 42,
              "Runs Windows games through Wine on this PS4. Games are kept in /data/pcps4/games. "
              "When a game ends you come back here.", 820, C_MUTED);
}

static void draw_starting(const Game *g)
{
    int s = 320, x = SW / 2 - s / 2, y = 260;
    memcpy(canvas, backdrop, FB_BYTES);
    tile(g, x, y, s, 0);
    text(&font_title, SW / 2 - text_width(&font_title, g->name) / 2, y + s + 120, g->name, C_TEXT, 255);
    text(&font_body, SW / 2 - text_width(&font_body, "Starting") / 2, y + s + 176, "Starting", C_MUTED, 255);
}

static void draw(void)
{
    int can_select = 1;
    compose_backdrop();
    draw_top_bar();
    switch (ui.tab)
    {
    case TAB_HOME: draw_home(); can_select = ngames > 0 || ui.in_tabs; break;
    case TAB_LIBRARY: draw_library(); can_select = ngames > 0 || ui.in_tabs; break;
    case TAB_ADD: draw_add(); can_select = ui.in_tabs; break;
    case TAB_SETTINGS: draw_settings(); break;
    }
    draw_bottom_bar(can_select);
}

/* ---- launch ---------------------------------------------------------------- */
static int write_launch_files(const Game *g)
{
    FILE *out, *in;
    char line[512];

    if (!(out = fopen("/data/pcps4_cmdline.txt", "w"))) return -1;
    fprintf(out, "%s\n", g->exe);
    for (int i = 0; i < g->nargs; i++) fprintf(out, "%s\n", g->args[i]);
    fclose(out);

    if (!(out = fopen("/data/pcps4_env.txt", "w"))) return -1;
    if ((in = fopen("/data/pcps4/env_common.txt", "r")))
    {
        while (fgets(line, sizeof(line), in)) fputs(line, out);
        fclose(in);
        fputc('\n', out);
    }
    for (int i = 0; i < g->nenvs; i++) fprintf(out, "%s\n", g->envs[i]);
    fclose(out);
    remember_played(g);
    return 0;
}

/* An external controller (for example the development host over FTP) can
 * request a launch without synthesising pad input by writing one game folder
 * id to this file.  The id is matched against the already-scanned library;
 * it is never treated as a path.  Invalid requests are deliberately left in
 * place so a typo cannot start anything or be mistaken for a successful
 * command. */
#define AUTO_LAUNCH_FILE "/data/pcps4/auto_launch.txt"

static int poll_auto_launch(const Game **out)
{
    char id[sizeof(games[0].id)];
    FILE *f;

    *out = NULL;
    if (!(f = fopen(AUTO_LAUNCH_FILE, "r"))) return 0;
    if (!fgets(id, sizeof(id), f))
    {
        fclose(f);
        return 0;
    }
    fclose(f);
    trim(id);
    if (!id[0]) return 0;

    for (int i = 0; i < ngames; i++)
        if (!strcmp(games[i].id, id))
        {
            if (write_launch_files(&games[i]) < 0) return 0;
            /* Consume only after the normal launch files were written. */
            if (remove(AUTO_LAUNCH_FILE) < 0) llog("auto launch consume failed", 0, 0);
            *out = &games[i];
            return 1;
        }
    return 0;
}

static int launch_game(const Game *g)
{
    if (write_launch_files(g) < 0) return -1;
    draw_starting(g);
    present();
    present();
    return 0;
}

/* ---- input ----------------------------------------------------------------- */
static void focus_game(int i)
{
    if (i < 0 || i >= ngames) return;
    ui.sel = i;
    if (ui.hero_for != i) backdrop_for(i);
}

static void set_tab(int tab)
{
    ui.tab = (tab + NTABS) % NTABS;
    ui.sel = 0;
    ui.lib_scroll = 0;
    if ((ui.tab == TAB_HOME || ui.tab == TAB_LIBRARY) && ngames) { if (ui.hero_for != 0) backdrop_for(0); }
    else if (ui.hero_for != -1) backdrop_for(-1);
}

static void toast(const char *s)
{
    ui.toast = s;
    ui.toast_frames = 150;
}

/* Returns -1 to keep going, 0 to launch games[ui.sel], 1 to quit. */
static int handle(uint32_t pressed)
{
    if (pressed & B_L1) { set_tab(ui.tab - 1); ui.in_tabs = 1; return -1; }
    if (pressed & B_R1) { set_tab(ui.tab + 1); ui.in_tabs = 1; return -1; }

    if (ui.in_tabs)
    {
        if (pressed & B_LEFT) set_tab(ui.tab - 1);
        if (pressed & B_RIGHT) set_tab(ui.tab + 1);
        if ((pressed & (B_DOWN | B_CROSS)) && (ui.tab >= TAB_ADD || ngames))
        {
            ui.in_tabs = 0;
            ui.sel = 0;
            if (ui.tab <= TAB_LIBRARY) focus_game(0);
        }
    }
    else if (ui.tab == TAB_HOME && ngames)
    {
        int row = ui.sel == 0 ? 0 : (ui.sel - 1) % 2;
        if (pressed & B_RIGHT) focus_game(ui.sel == 0 ? 1 : ui.sel + 2 < ngames ? ui.sel + 2 : ui.sel);
        if (pressed & B_LEFT) focus_game(ui.sel <= 2 ? 0 : ui.sel - 2);
        if ((pressed & B_DOWN) && ui.sel > 0 && row == 0 && ui.sel + 1 < ngames) focus_game(ui.sel + 1);
        if (pressed & B_UP) { if (ui.sel > 0 && row == 1) focus_game(ui.sel - 1); else ui.in_tabs = 1; }
        if (pressed & B_CROSS) return 0;
    }
    else if (ui.tab == TAB_LIBRARY && ngames)
    {
        if ((pressed & B_RIGHT) && ui.sel + 1 < ngames && (ui.sel + 1) % LIB_COLS) focus_game(ui.sel + 1);
        if ((pressed & B_LEFT) && ui.sel % LIB_COLS) focus_game(ui.sel - 1);
        if ((pressed & B_DOWN) && ui.sel + LIB_COLS < ngames) focus_game(ui.sel + LIB_COLS);
        if (pressed & B_UP) { if (ui.sel >= LIB_COLS) focus_game(ui.sel - LIB_COLS); else ui.in_tabs = 1; }
        if (pressed & B_CROSS) return 0;
    }
    else if (ui.tab == TAB_ADD)
    {
        if (pressed & B_RIGHT) ui.sel = 1;
        if (pressed & B_LEFT) ui.sel = 0;
        if (pressed & B_UP) ui.in_tabs = 1;
        if ((pressed & B_CROSS) && ui.sel == 1) toast("Steam isn't available yet");
    }
    else if (ui.tab == TAB_SETTINGS)
    {
        if (pressed & B_UP) ui.in_tabs = 1;
        if ((pressed & B_CROSS) && ui.sel == SET_QUIT) return 1;
    }

    if ((pressed & B_CIRCLE) && !ui.in_tabs) ui.in_tabs = 1;
    if ((pressed & B_TRIANGLE) && ui.tab <= TAB_LIBRARY)
    {
        scan_library();
        ui.sel = 0;
        backdrop_for(ngames ? 0 : -1);
        toast(ngames == 1 ? "1 game found" : "Library refreshed");
    }
    return -1;
}

/* ---- entry ----------------------------------------------------------------- */
static void launcher_init(void)
{
    char name[32];
    memcpy(name, ui.user, sizeof(name));
    memset(&ui, 0, sizeof(ui));
    memcpy(ui.user, name, sizeof(name));
    ui.hero_for = -2;
    scan_library();
    backdrop_plain(backdrop, rgb(40, 60, 70));
    if (ngames) focus_game(0);
    else ui.in_tabs = 1;
    ui.fade = FADE_STEPS;
}

__attribute__((visibility("default"))) int pcps4_launcher_run(void)
{
    int32_t user = 0, pad = -1;
    int result = -1, hold = 0;
    uint32_t prev = ~0u;

    remove("/data/pcps4/launcher.log");
    llog("launcher %ld start", 2, 0);
    if (!(games = calloc(MAX_GAMES, sizeof(Game)))) { llog("calloc games failed (%ld bytes)", (long)(MAX_GAMES * sizeof(Game)), 0); return -1; }
    if (!(canvas = malloc(FB_BYTES)) || !(backdrop = malloc(FB_BYTES)) || !(backdrop_old = malloc(FB_BYTES)))
    {
        llog("malloc of 3 x %ld bytes failed", (long)FB_BYTES, 0);
        goto out;
    }
    if (video_open() < 0) goto out;
    llog("video up", 0, 0);

    sceUserServiceInitialize(NULL);
    scePadInit();
    {
        int32_t r = sceUserServiceGetInitialUser(&user);
        if (r >= 0)
        {
            pad = scePadOpen(user, 0, 0, NULL);
            sceUserServiceGetUserName(user, ui.user, sizeof(ui.user) - 1);
        }
        llog("user 0x%lx, pad open -> 0x%lx", r >= 0 ? (unsigned)user : (unsigned)r, (unsigned)pad);
    }
    launcher_init();

    for (int frame = 0;; frame++)
    {
        PadData pd;
        const Game *auto_game;
        uint32_t now = 0, pressed;
        const uint32_t dirs = B_LEFT | B_RIGHT | B_UP | B_DOWN;

        /* Poll at roughly 4 Hz so the launcher remains responsive while an
         * FTP client publishes the opt-in control file. */
        if (frame % 15 == 0 && poll_auto_launch(&auto_game))
        {
            draw_starting(auto_game);
            present();
            present();
            result = 0;
            break;
        }

        memset(&pd, 0, sizeof(pd));
        /* Started again straight after a game, the console may not have
         * released the controller yet: keep trying to open it. */
        if (pad < 0 && frame % 30 == 0)
        {
            if (!user) sceUserServiceGetInitialUser(&user);
            pad = scePadOpen(user, 0, 0, NULL);
            if (frame % 300 == 0 || pad >= 0) llog("pad retry at frame %ld -> 0x%lx", frame, (unsigned)pad);
        }
        if (pad >= 0 && scePadReadState(pad, &pd) >= 0)
        {
            now = pd.buttons;
            if (pd.lx < 50) now |= B_LEFT;
            if (pd.lx > 205) now |= B_RIGHT;
            if (pd.ly < 50) now |= B_UP;
            if (pd.ly > 205) now |= B_DOWN;
        }
        if (prev == ~0u) prev = now;   /* buttons held at start (the Cross that opened the app) don't count */
        pressed = now & ~prev;
        if (now & dirs) { if (++hold > 22 && hold % 6 == 0) pressed |= now & dirs; }
        else hold = 0;
        prev = now;

        if (pressed)
        {
            int r = handle(pressed);
            if (r == 1) { result = 1; break; }
            if (r == 0 && ngames)
            {
                if (launch_game(&games[ui.sel]) == 0)
                {
                    result = 0;
                    break;
                }
                toast("Couldn't save the launch settings");
            }
        }
        if (ui.toast_frames > 0) ui.toast_frames--;

        /* redraw while something animates, on input, and once a second for the clock */
        if (pressed || ui.fade < FADE_STEPS || ui.toast_frames == 1 || frame % 60 == 0)
        {
            draw();
            present();
        }
        else sceKernelUsleep(16000);
    }

out:
    llog("launcher exit, result %ld", result, 0);
    if (pad >= 0) scePadClose(pad);
    video_close();
    free_library();
    free(canvas);
    free(backdrop);
    free(backdrop_old);
    free(games);
    canvas = backdrop = backdrop_old = NULL;
    games = NULL;
    return result;
}
