/*
 * PS4 Unix half of Wine's xinput: the console's first controller, read with
 * scePad, presented as XInput controller 0.
 *
 * Wine's PE xinput (built from the patched tree) loads "xinput1_3" by name
 * and calls pad_init / pad_state / pad_rumble through this table.
 */
#include <stdint.h>

typedef int NTSTATUS;
typedef NTSTATUS (*unixlib_entry_t)( void *args );
#define STATUS_SUCCESS        ((NTSTATUS)0)

struct xinput_pad_state           /* dlls/xinput1_3/unixlib.h */
{
    uint32_t connected;
    uint32_t packet;
    uint16_t buttons;
    uint8_t  left_trigger;
    uint8_t  right_trigger;
    int16_t  thumb_lx, thumb_ly, thumb_rx, thumb_ry;
};
struct xinput_rumble_params { uint32_t left, right; };

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
typedef struct { uint8_t large, small; } PadVibe;

extern int32_t sceUserServiceInitialize( void * );
extern int32_t sceUserServiceGetInitialUser( int32_t * );
extern int32_t scePadInit( void );
extern int32_t scePadOpen( int32_t user, int32_t type, int32_t index, void *param );
extern int32_t scePadReadState( int32_t handle, PadData *data );
extern int32_t scePadSetVibration( int32_t handle, const PadVibe *param );

static int32_t pad_handle = -1;
static uint32_t packet;
static PadData last;

static int16_t axis( uint8_t v, int invert )
{
    int32_t x = ((int32_t)v - 128) * 258;       /* 0..255 -> about -33024..32766 */
    if (invert) x = -x - 1;
    if (x > 32767) x = 32767;
    if (x < -32768) x = -32768;
    return (int16_t)x;
}

static NTSTATUS pad_init( void *args )
{
    int32_t user = 0;
    (void)args;
    if (pad_handle >= 0) return STATUS_SUCCESS;
    sceUserServiceInitialize( 0 );              /* already initialised is fine */
    scePadInit();
    if (sceUserServiceGetInitialUser( &user ) < 0) return STATUS_SUCCESS;
    pad_handle = scePadOpen( user, 0, 0, 0 );
    return STATUS_SUCCESS;                      /* controller 0 exists even if unplugged */
}

static NTSTATUS pad_state( void *args )
{
    struct xinput_pad_state *s = args;
    PadData d;
    uint16_t b = 0;
    uint32_t i;
    const uint8_t *p = (const uint8_t *)&d, *q = (const uint8_t *)&last;

    for (i = 0; i < sizeof(*s); i++) ((uint8_t *)s)[i] = 0;
    if (pad_handle < 0) { pad_init( 0 ); if (pad_handle < 0) return STATUS_SUCCESS; }
    for (i = 0; i < sizeof(d); i++) ((uint8_t *)&d)[i] = 0;
    if (scePadReadState( pad_handle, &d ) < 0 || !d.connected) return STATUS_SUCCESS;

    if (d.buttons & 0x0010) b |= 0x0001;        /* up */
    if (d.buttons & 0x0040) b |= 0x0002;        /* down */
    if (d.buttons & 0x0080) b |= 0x0004;        /* left */
    if (d.buttons & 0x0020) b |= 0x0008;        /* right */
    if (d.buttons & 0x0008) b |= 0x0010;        /* OPTIONS  -> START */
    if (d.buttons & 0x100000) b |= 0x0020;      /* touchpad -> BACK */
    if (d.buttons & 0x0002) b |= 0x0040;        /* L3 */
    if (d.buttons & 0x0004) b |= 0x0080;        /* R3 */
    if (d.buttons & 0x0400) b |= 0x0100;        /* L1 */
    if (d.buttons & 0x0800) b |= 0x0200;        /* R1 */
    if (d.buttons & 0x4000) b |= 0x1000;        /* cross    -> A */
    if (d.buttons & 0x2000) b |= 0x2000;        /* circle   -> B */
    if (d.buttons & 0x8000) b |= 0x4000;        /* square   -> X */
    if (d.buttons & 0x1000) b |= 0x8000;        /* triangle -> Y */

    /* XInput's packet number changes only when the state does. */
    for (i = 0; i < 12; i++) if (p[i] != q[i]) { packet++; break; }
    last = d;

    s->connected = 1;
    s->packet = packet;
    s->buttons = b;
    s->left_trigger = d.l2;
    s->right_trigger = d.r2;
    s->thumb_lx = axis( d.lx, 0 );
    s->thumb_ly = axis( d.ly, 1 );
    s->thumb_rx = axis( d.rx, 0 );
    s->thumb_ry = axis( d.ry, 1 );
    return STATUS_SUCCESS;
}

static NTSTATUS pad_rumble( void *args )
{
    const struct xinput_rumble_params *r = args;
    PadVibe v;
    if (pad_handle < 0) return STATUS_SUCCESS;
    v.large = (uint8_t)(r->left >> 8);
    v.small = (uint8_t)(r->right >> 8);
    scePadSetVibration( pad_handle, &v );
    return STATUS_SUCCESS;
}

__attribute__((visibility("default"))) const unixlib_entry_t __wine_unix_call_funcs[] = { pad_init, pad_state, pad_rumble };
__attribute__((visibility("default"))) const unixlib_entry_t __wine_unix_call_wow64_funcs[] = { pad_init, pad_state, pad_rumble };
