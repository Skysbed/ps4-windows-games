/*
 * PS4 Unix half of Wine's xinput: the console's first controller, read with
 * scePad, presented as XInput controller 0.
 *
 * Improved version with better error handling, reconnection logic, and
 * proper axis scaling.
 *
 * Wine's PE xinput (built from the patched tree) loads "xinput1_3" by name
 * and calls pad_init / pad_state / pad_rumble through this table.
 */
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>

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
extern int32_t scePadGetHandle( int32_t user, int32_t type, int32_t index );
extern int32_t scePadClose( int32_t handle );
extern int32_t scePadReadState( int32_t handle, PadData *data );
extern int32_t scePadSetVibration( int32_t handle, const PadVibe *param );

static int32_t pad_handle = -1;
static uint32_t packet;
static struct xinput_pad_state last;
static pthread_mutex_t pad_lock = PTHREAD_MUTEX_INITIALIZER;

/* Deadzone threshold for sticks (0-255 scale) */
#define STICK_DEADZONE 10

/* Improved axis scaling: 0-255 -> -32768 to 32767 with deadzone */
static int16_t axis( uint8_t v, int invert )
{
    int32_t x = (int32_t)v - 128;
    
    /* Apply deadzone */
    if (x > 0 && x < STICK_DEADZONE)
        x = 0;
    else if (x < 0 && x > -STICK_DEADZONE)
        x = 0;
    
    /* Scale to full range: 128 +/- 127 -> 32767/-32768 */
    x = x * 257;  /* 257 * 127 = 32639, close enough to 32767 */
    
    if (invert) x = -x;
    if (x > 32767) x = 32767;
    if (x < -32768) x = -32768;
    return (int16_t)x;
}

/* Caller holds pad_lock. */
static void init_locked( void )
{
    int32_t user = 0;
    
    /* If already initialized and handle is valid, don't reinitialize */
    if (pad_handle >= 0) return;
    
    /* Close any existing handle first */
    if (pad_handle < -1)  /* Special invalid state */
    {
        scePadClose(pad_handle);
        pad_handle = -1;
    }
    
    /* Initialize user service */
    if (sceUserServiceInitialize(0) < 0)
    {
        /* Try to continue even if user service init fails */
    }
    
    /* Initialize pad system */
    scePadInit();
    
    /* Get initial user */
    if (sceUserServiceGetInitialUser(&user) < 0)
    {
        user = 0;  /* Fallback to user 0 */
    }
    
    /* Try to open the pad */
    pad_handle = scePadOpen(user, 0, 0, 0);
    if (pad_handle < 0)
    {
        /* Try to get existing handle */
        pad_handle = scePadGetHandle(user, 0, 0);
    }
}

/* Try to reconnect the controller */
static void try_reconnect(void)
{
    if (pad_handle >= 0) return;
    
    /* Close any invalid handle */
    if (pad_handle < -1)
    {
        scePadClose(pad_handle);
        pad_handle = -1;
    }
    
    /* Reinitialize */
    init_locked();
}

static NTSTATUS pad_init( void *args )
{
    (void)args;
    pthread_mutex_lock( &pad_lock );
    init_locked();
    pthread_mutex_unlock( &pad_lock );
    return STATUS_SUCCESS;                      /* controller 0 exists even if unplugged */
}

static NTSTATUS pad_state( void *args )
{
    struct xinput_pad_state *s = args;
    PadData d;
    uint16_t b = 0;
    uint32_t i;
    static unsigned consecutive_failures = 0;
    const unsigned max_consecutive_failures = 100;

    for (i = 0; i < sizeof(*s); i++) ((uint8_t *)s)[i] = 0;
    
    pthread_mutex_lock( &pad_lock );
    
    /* If handle is invalid, try to reconnect */
    if (pad_handle < 0)
    {
        if (consecutive_failures < max_consecutive_failures || 
            consecutive_failures % 100 == 0)  /* Try every 100 calls after max */
        {
            init_locked();
        }
        consecutive_failures++;
        goto disconnected;
    }
    
    for (i = 0; i < sizeof(d); i++) ((uint8_t *)&d)[i] = 0;
    
    if (scePadReadState(pad_handle, &d) < 0 || !d.connected)
    {
        consecutive_failures++;
        
        /* If we have too many consecutive failures, mark as disconnected */
        if (consecutive_failures >= max_consecutive_failures)
        {
            /* Close and invalidate the handle to force reconnection */
            scePadClose(pad_handle);
            pad_handle = -2;  /* Mark as needing reconnection */
        }
        
        goto disconnected;
    }
    
    consecutive_failures = 0;
    
    /* Button mapping from PS4 to XInput */
    if (d.buttons & 0x0010) b |= 0x0001;        /* up */
    if (d.buttons & 0x0040) b |= 0x0002;        /* down */
    if (d.buttons & 0x0080) b |= 0x0004;        /* left */
    if (d.buttons & 0x0020) b |= 0x0008;        /* right */
    if (d.buttons & 0x0008) b |= 0x0010;        /* OPTIONS -> START */
    if (d.buttons & 0x100000) b |= 0x0020;      /* touchpad -> BACK */
    if (d.buttons & 0x0002) b |= 0x0040;        /* L3 */
    if (d.buttons & 0x0004) b |= 0x0080;        /* R3 */
    if (d.buttons & 0x0400) b |= 0x0100;        /* L1 */
    if (d.buttons & 0x0800) b |= 0x0200;        /* R1 */
    if (d.buttons & 0x4000) b |= 0x1000;        /* cross -> A */
    if (d.buttons & 0x2000) b |= 0x2000;        /* circle -> B */
    if (d.buttons & 0x8000) b |= 0x4000;        /* square -> X */
    if (d.buttons & 0x1000) b |= 0x8000;        /* triangle -> Y */

    s->connected = 1;
    s->buttons = b;
    s->left_trigger = d.l2;
    s->right_trigger = d.r2;
    s->thumb_lx = axis(d.lx, 0);
    s->thumb_ly = axis(d.ly, 1);
    s->thumb_rx = axis(d.rx, 0);
    s->thumb_ry = axis(d.ry, 1);
    
    /* Increment packet number if state changed */
    if (!last.connected || s->buttons != last.buttons ||
        s->left_trigger != last.left_trigger || s->right_trigger != last.right_trigger ||
        s->thumb_lx != last.thumb_lx || s->thumb_ly != last.thumb_ly ||
        s->thumb_rx != last.thumb_rx || s->thumb_ry != last.thumb_ry)
    {
        packet++;
    }
    s->packet = packet;
    last = *s;
    
    pthread_mutex_unlock(&pad_lock);
    return STATUS_SUCCESS;

disconnected:
    last.connected = 0;
    pthread_mutex_unlock(&pad_lock);
    return STATUS_SUCCESS;
}

static NTSTATUS pad_rumble( void *args )
{
    const struct xinput_rumble_params *r = args;
    PadVibe v;
    
    pthread_mutex_lock(&pad_lock);
    
    /* Try to reconnect if handle is invalid */
    if (pad_handle < 0) 
    {
        init_locked();
        if (pad_handle < 0)
        {
            pthread_mutex_unlock(&pad_lock);
            return STATUS_SUCCESS;
        }
    }
    
    v.large = (uint8_t)(r->left >> 8);
    v.small = (uint8_t)(r->right >> 8);
    scePadSetVibration(pad_handle, &v);
    
    pthread_mutex_unlock(&pad_lock);
    return STATUS_SUCCESS;
}

__attribute__((visibility("default"))) const unixlib_entry_t __wine_unix_call_funcs[] = { pad_init, pad_state, pad_rumble };
__attribute__((visibility("default"))) const unixlib_entry_t __wine_unix_call_wow64_funcs[] = { pad_init, pad_state, pad_rumble };
