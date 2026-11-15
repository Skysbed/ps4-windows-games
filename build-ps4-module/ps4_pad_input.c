/* PS4: the controller as keyboard and mouse, for PC games with no gamepad
 * support (Minecraft Java).
 *
 * Enabled per game by PCPS4_PAD_INPUT=minecraft in /data/pcps4_env.txt (the
 * launcher writes the game's env= line there).  A thread reads the pad with
 * scePad and posts key, mouse-move and mouse-button events into the port's
 * input queue (pw_wine_post_input), which the win32u driver delivers to the
 * foreground window as hardware input - the same path a real keyboard and
 * mouse take.  The mouse wheel travels as the unassigned virtual keys 0x0E
 * (up) and 0x0F (down); the driver turns them into wheel clicks.
 *
 * The driver reports whether the game shows a cursor: shown means a menu
 * (d-pad arrows, Cross Enter, Circle Escape), hidden means play (left stick
 * WASD, right stick looks). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

typedef struct PwWineInput { uint32_t type, code; int32_t x, y; uint32_t down; } PwWineInput;
int pw_wine_post_input(const PwWineInput *event);

int sceKernelLoadStartModule(const char *path, size_t argc, const void *argv, uint32_t flags,
                             const void *option, int *result);
int sceKernelDlsym(int32_t handle, const char *name, void **address);
int sceKernelUsleep(uint32_t usec);
int sceKernelGetProcessTime(void);    /* not used for timing; see now_us */
uint64_t sceKernelGetProcessTimeCounter(void);
uint64_t sceKernelGetProcessTimeCounterFrequency(void);

typedef struct { float x, y, z, w; } vec4;
typedef struct { float x, y, z; } vec3;
typedef struct { uint16_t x, y; uint8_t finger, pad[3]; } PadTouch;
typedef struct { uint8_t fingers, pad1[3]; uint32_t pad2; PadTouch touch[2]; } PadTouchData;
typedef struct
{
    uint32_t buttons;
    uint8_t  lx, ly, rx, ry;
    uint8_t  l2, r2;
    uint16_t padding;
    vec4 quat; vec3 vel; vec3 accel;
    PadTouchData touch;
    uint8_t  connected;
    uint64_t timestamp;
    uint8_t  ext[16];
    uint8_t  count;
    uint8_t  unknown[15];
} PadData;

enum
{
    B_L3 = 0x2, B_R3 = 0x4, B_OPTIONS = 0x8, B_UP = 0x10, B_RIGHT = 0x20, B_DOWN = 0x40, B_LEFT = 0x80,
    B_L1 = 0x400, B_R1 = 0x800, B_TRIANGLE = 0x1000, B_CIRCLE = 0x2000, B_CROSS = 0x4000,
    B_SQUARE = 0x8000, B_TOUCHPAD = 0x100000,
};
enum
{
    VK_TAB_ = 0x09, VK_RETURN_ = 0x0D, VK_WHEEL_UP = 0x0E, VK_WHEEL_DOWN = 0x0F, VK_ESCAPE_ = 0x1B,
    VK_SPACE_ = 0x20, VK_LEFT_ = 0x25, VK_UP_ = 0x26, VK_RIGHT_ = 0x27, VK_DOWN_ = 0x28,
    VK_F5_ = 0x74, VK_LSHIFT_ = 0xA0, VK_LCONTROL_ = 0xA2,
};

static int (*pad_init)(void);
static int (*pad_open)(int32_t user, int32_t type, int32_t index, void *param);
static int (*pad_get_handle)(int32_t user, int32_t type, int32_t index);
static int (*pad_read_state)(int32_t handle, PadData *data);
static int (*user_init)(void *);
static int (*user_initial)(int32_t *);

static volatile int cursor_shown;     /* set by the driver */
static int started;

void pcps4_pad_cursor_visible(int visible) { cursor_shown = visible != 0; }

static uint64_t now_us(void)
{
    static uint64_t freq;
    if (!freq) freq = sceKernelGetProcessTimeCounterFrequency();
    return freq ? sceKernelGetProcessTimeCounter() * 1000000ull / freq : 0;
}

static void *sym(int handle, const char *name)
{
    void *p = NULL;
    if (handle < 0 || sceKernelDlsym(handle, name, &p) != 0) return NULL;
    return p;
}

static int profile_enabled(void)
{
    char line[512];
    int on = 0;
    FILE *f = fopen("/data/pcps4_env.txt", "r");
    if (!f) return 0;
    while (fgets(line, sizeof(line), f))
        if (!strncmp(line, "PCPS4_PAD_INPUT=", 16) && line[16] && line[16] != '0' && line[16] != '\n') on = 1;
    fclose(f);
    return on;
}

static int post(uint32_t type, uint32_t code, int32_t x, int32_t y, uint32_t down)
{
    static unsigned said;
    PwWineInput e = { type, code, x, y, down };
    int r = pw_wine_post_input(&e);
    if (said < 12 && type != 2) { said++; fprintf(stderr, "pcps4-pad: post type %u code 0x%x down %u -> %d\n", type, code, down, r); }
    return r;
}

/* keys: 1 key, 3 mouse button (code 0 left, 1 right) */
static uint8_t key_down[256], button_down[3];

static void set_key(int vk, int down)
{
    if (key_down[vk] == down) return;
    if (!post(1, (uint32_t)vk, 0, 0, (uint32_t)down)) key_down[vk] = (uint8_t)down;   /* else retried next poll */
}

static void set_button(int b, int down)
{
    if (button_down[b] == down) return;
    if (!post(3, (uint32_t)b, 0, 0, (uint32_t)down)) button_down[b] = (uint8_t)down;
}

static void wheel(int vk)
{
    post(1, (uint32_t)vk, 0, 0, 1);
    post(1, (uint32_t)vk, 0, 0, 0);
}

static float axis(uint8_t v)
{
    float f = ((int)v - 128) / 127.0f;
    return f < -1 ? -1 : f > 1 ? 1 : f;
}

/* stick to look speed: deadzone, then a curve for fine aim */
static float shape(float v, float dead)
{
    float a = v < 0 ? -v : v;
    if (a < dead) return 0;
    a = (a - dead) / (1 - dead);
    a = a * a;
    return v < 0 ? -a : a;
}

static void *pad_thread(void *arg)
{
    int32_t user = 0, handle = -1;
    uint64_t last = now_us(), l1_at = 0, r1_at = 0;
    float fx = 0, fy = 0;
    int menu = -1, triggers_l = 0, triggers_r = 0;
    uint32_t prev = 0;
    PadData d;
    (void)arg;

    if (user_init) user_init(NULL);
    if (pad_init) pad_init();
    if (!user_initial || user_initial(&user) < 0) { fprintf(stderr, "pcps4-pad: no user\n"); return NULL; }
    for (int tries = 0; handle < 0 && tries < 50; tries++)
    {
        handle = pad_open(user, 0, 0, NULL);
        if (handle < 0 && pad_get_handle) handle = pad_get_handle(user, 0, 0);
        if (handle < 0) sceKernelUsleep(200000);
    }
    fprintf(stderr, "pcps4-pad: controller as keyboard and mouse (handle 0x%x)\n", (unsigned)handle);
    if (handle < 0) return NULL;

    for (;;)
    {
        uint64_t t;
        float dt, lx, ly, rx, ry, speed;
        uint32_t b, pressed;
        int now_menu;

        sceKernelUsleep(8000);
        t = now_us(); dt = (t - last) / 1000000.0f; last = t;
        if (dt > 0.1f) dt = 0.1f;
        memset(&d, 0, sizeof(d));
        {
            static unsigned fails, said_ok;
            int r = pad_read_state(handle, &d);
            if (r < 0 || !d.connected)
            {
                if (++fails <= 3 || fails % 1000 == 0)
                    fprintf(stderr, "pcps4-pad: pad read failed 0x%x connected %d (%u)\n", (unsigned)r, d.connected, fails);
                continue;
            }
            if (!said_ok++) fprintf(stderr, "pcps4-pad: reading pad, buttons 0x%x sticks %u,%u %u,%u\n",
                                   d.buttons, d.lx, d.ly, d.rx, d.ry);
        }
        b = d.buttons; pressed = b & ~prev; prev = b;
        {
            static unsigned said;
            if (pressed && said < 12) { said++; fprintf(stderr, "pcps4-pad: pressed 0x%x, menu mode %d\n", pressed, cursor_shown); }
        }
        lx = axis(d.lx); ly = axis(d.ly); rx = axis(d.rx); ry = axis(d.ry);

        now_menu = cursor_shown;
        if (now_menu != menu)
        {
            /* let go of everything the other mode held */
            for (int k = 0; k < 256; k++) if (key_down[k]) set_key(k, 0);
            for (int k = 0; k < 3; k++) if (button_down[k]) set_button(k, 0);
            menu = now_menu; triggers_l = triggers_r = 0;
        }

        /* triggers with hysteresis: mouse buttons in both modes */
        triggers_r = triggers_r ? d.r2 > 60 : d.r2 > 110;
        triggers_l = triggers_l ? d.l2 > 60 : d.l2 > 110;
        set_button(0, triggers_r);
        set_button(1, triggers_l);

        /* L1 / R1: hotbar or scroll, repeating while held */
        if (pressed & B_L1) { wheel(VK_WHEEL_UP); l1_at = t + 400000; }
        else if ((b & B_L1) && t >= l1_at) { wheel(VK_WHEEL_UP); l1_at = t + 120000; }
        if (pressed & B_R1) { wheel(VK_WHEEL_DOWN); r1_at = t + 400000; }
        else if ((b & B_R1) && t >= r1_at) { wheel(VK_WHEEL_DOWN); r1_at = t + 120000; }

        if (menu)
        {
            set_key(VK_UP_, !!(b & B_UP));
            set_key(VK_DOWN_, !!(b & B_DOWN));
            set_key(VK_LEFT_, !!(b & B_LEFT));
            set_key(VK_RIGHT_, !!(b & B_RIGHT));
            set_key(VK_RETURN_, !!(b & B_CROSS));
            set_key(VK_ESCAPE_, !!(b & (B_CIRCLE | B_OPTIONS)));
            set_key(VK_TAB_, !!(b & B_TOUCHPAD));
            set_key('E', !!(b & B_TRIANGLE));
            set_key('Q', !!(b & B_SQUARE));
            set_key(VK_LSHIFT_, !!(b & B_R3));
            /* either stick moves the pointer in menus */
            speed = 1100.0f;
            fx += (shape(rx, 0.15f) + shape(lx, 0.25f)) * speed * dt;
            fy += (shape(ry, 0.15f) + shape(ly, 0.25f)) * speed * dt;
        }
        else
        {
            set_key('W', ly < -0.40f);
            set_key('S', ly > 0.40f);
            set_key('A', lx < -0.40f);
            set_key('D', lx > 0.40f);
            set_key(VK_SPACE_, !!(b & B_CROSS));
            set_key(VK_LSHIFT_, !!(b & (B_CIRCLE | B_R3)));
            set_key(VK_LCONTROL_, !!(b & B_L3));
            set_key('E', !!(b & B_TRIANGLE));
            set_key('Q', !!(b & B_SQUARE));
            set_key(VK_ESCAPE_, !!(b & B_OPTIONS));
            set_key(VK_TAB_, !!(b & B_TOUCHPAD));
            set_key(VK_F5_, !!(b & B_UP));
            speed = 1600.0f;
            fx += shape(rx, 0.12f) * speed * dt;
            fy += shape(ry, 0.12f) * speed * dt;
        }
        {
            int32_t mx = (int32_t)fx, my = (int32_t)fy;
            if ((mx || my) && !post(2, 0, mx, my, 0)) { fx -= mx; fy -= my; }
            if (fx > 400 || fx < -400) fx = 0;   /* nobody is taking input: do not wind up */
            if (fy > 400 || fy < -400) fy = 0;
        }
    }
    return NULL;
}

void pcps4_pad_start(void)
{
    pthread_t thread;
    int handle, res = 0;

    if (__atomic_exchange_n(&started, 1, __ATOMIC_ACQ_REL)) return;
    if (!profile_enabled()) { fprintf(stderr, "pcps4-pad: off for this game\n"); return; }
    fprintf(stderr, "pcps4-pad: starting\n");
    handle = sceKernelLoadStartModule("libScePad.sprx", 0, NULL, 0, NULL, &res);
    pad_init = sym(handle, "scePadInit");
    pad_open = sym(handle, "scePadOpen");
    pad_get_handle = sym(handle, "scePadGetHandle");
    pad_read_state = sym(handle, "scePadReadState");
    handle = sceKernelLoadStartModule("libSceUserService.sprx", 0, NULL, 0, NULL, &res);
    user_init = sym(handle, "sceUserServiceInitialize");
    user_initial = sym(handle, "sceUserServiceGetInitialUser");
    if (!pad_open || !pad_read_state)
    {
        fprintf(stderr, "pcps4-pad: scePad not available\n");
        return;
    }
    if (pthread_create(&thread, NULL, pad_thread, NULL) != 0)
        fprintf(stderr, "pcps4-pad: no thread\n");
}
