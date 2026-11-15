/* Host preview: draws launcher screens to PPM files without a console. */
#include "pcps4_launcher.c"
int32_t sceVideoOutOpen(int32_t u, int32_t b, int32_t i, const void *p) { return -1; }
int32_t sceVideoOutClose(int32_t h) { return 0; }
int32_t sceVideoOutRegisterBuffers(int32_t h, int32_t s, void *const *a, int32_t c, const void *t) { return 0; }
int32_t sceVideoOutSubmitFlip(int32_t h, int32_t i, uint32_t m, int64_t a) { return 0; }
void sceVideoOutSetBufferAttribute(void *a, uint32_t f, uint32_t t, uint32_t s, uint32_t w, uint32_t h, uint32_t p) {}
int32_t sceVideoOutSetFlipRate(int32_t h, int32_t r) { return 0; }
int32_t sceVideoOutAddFlipEvent(void *e, int32_t h, void *u) { return 0; }
int32_t sceKernelCreateEqueue(void **e, const char *n) { return 0; }
int32_t sceKernelDeleteEqueue(void *e) { return 0; }
int32_t sceKernelWaitEqueue(void *e, void *v, int32_t n, int32_t *o, uint32_t *t) { return 0; }
int32_t sceKernelAllocateDirectMemory(int64_t s, int64_t e, size_t l, size_t a, int32_t t, int64_t *o) { return -1; }
int32_t sceKernelMapDirectMemory(void **a, size_t l, int32_t p, int32_t f, int64_t o, size_t al) { return -1; }
int32_t sceKernelReleaseDirectMemory(int64_t o, size_t l) { return 0; }
int32_t sceKernelMunmap(void *a, size_t l) { return 0; }
size_t sceKernelGetDirectMemorySize(void) { return 0; }
int32_t sceKernelUsleep(uint32_t u) { return 0; }
int32_t sceUserServiceInitialize(void *p) { return 0; }
int32_t sceUserServiceGetInitialUser(int32_t *u) { return -1; }
int32_t sceUserServiceGetUserName(int32_t u, char *n, size_t s) { return -1; }
int32_t scePadInit(void) { return 0; }
int32_t scePadOpen(int32_t u, int32_t t, int32_t i, void *p) { return -1; }
int32_t scePadClose(int32_t h) { return 0; }
int32_t scePadReadState(int32_t h, PadData *d) { return -1; }
int32_t sceKernelLoadStartModule(const char *p, size_t a, const void *v, uint32_t f, const void *o, int *r) { return -1; }
int32_t sceKernelDlsym(int32_t h, const char *n, void **a) { return -1; }

static void dump(const char *dir, const char *name)
{
    char path[512]; snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6 %d %d 255\n", SW, SH);
    for (int i = 0; i < SW * SH; i++) { unsigned char p[3] = { canvas[i] >> 16, canvas[i] >> 8, canvas[i] }; fwrite(p, 1, 3, f); }
    fclose(f);
}

int main(int argc, char **argv)
{
    static Game list[MAX_GAMES];
    const char *names[] = { "SuperTuxKart", "Direct3D 11 Test", "Minecraft", "Portal 2", "Half-Life 2", "Doom 3", "Quake III Arena" };
    const char *infos[] = { "64-bit  -  Vulkan  -  Controller", "64-bit  -  Direct3D 11 through DXVK", "", "", "", "", "" };
    games = list; ngames = 7;
    canvas = malloc(FB_BYTES); backdrop = malloc(FB_BYTES); backdrop_old = malloc(FB_BYTES);
    for (int i = 0; i < ngames; i++)
    {
        snprintf(games[i].name, sizeof(games[0].name), "%s", names[i]);
        snprintf(games[i].info, sizeof(games[0].info), "%s", infos[i]);
        games[i].color = palette[i] | 0xFF000000u;
    }
    snprintf(ui.user, sizeof(ui.user), "Player");
    ui.hero_for = -2;
    backdrop_plain(backdrop, games[0].color); ui.fade = FADE_STEPS;
    draw(); dump(argv[1], "1-home");
    ui.sel = 3; backdrop_plain(backdrop, games[3].color); ui.fade = FADE_STEPS; draw(); dump(argv[1], "2-home-moved");
    ui.tab = TAB_LIBRARY; ui.sel = 1; backdrop_plain(backdrop, games[1].color); ui.fade = FADE_STEPS; draw(); dump(argv[1], "3-library");
    ui.tab = TAB_ADD; ui.sel = 1; backdrop_plain(backdrop, rgb(40, 60, 70)); ui.fade = FADE_STEPS; draw(); dump(argv[1], "4-add");
    ui.tab = TAB_SETTINGS; ui.in_tabs = 1; draw(); dump(argv[1], "5-settings");
    ui.tab = TAB_HOME; ui.in_tabs = 0; ngames = 0; draw(); dump(argv[1], "6-empty");
    return 0;
}
