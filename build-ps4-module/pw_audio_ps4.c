/*
 * PS4 audio platform interface for Wine's audio driver.
 * The console's main audio port, for a caller that mixes and clocks
 * itself: Wine's audio driver (wineps4) hands the title one grain of
 * 16-bit stereo at a time, and each output call returns when the port has
 * taken it.
 */
#include <stdint.h>

/*
 * SCE AudioOut parameters for PS4.
 * User: 0xff (system user)
 * Type: 0 (main out)
 * Index: 0 (first port)
 * Format: 1 (S16 stereo)
 */
enum { PW_AUDIO_PS4_USER_SYSTEM = 0xff, PW_AUDIO_PS4_PORT_MAIN = 0,
       PW_AUDIO_PS4_PORT_INDEX = 0, PW_AUDIO_PS4_FORMAT_S16_STEREO = 1,
       PW_AUDIO_PS4_VOLUME_FLAGS = 3, PW_AUDIO_PS4_VOLUME_0DB = 0x8000 };

enum { PW_AUDIO_PS4_RATE = 48000, PW_AUDIO_PS4_GRAIN = 256, PW_AUDIO_PS4_CHANNELS = 2 };

/* SCE AudioOut external declarations */
extern int sceAudioOutInit(void);
extern int sceAudioOutOpen(int user, int type, int index, uint32_t grain, uint32_t rate, uint32_t format);
extern int sceAudioOutSetVolume(int handle, int flags, const int32_t *volumes);
extern int sceAudioOutOutput(int handle, const void *samples);
extern int sceAudioOutClose(int handle);

/*
 * Platform operations structure for PS4 audio.
 */
typedef struct PwAudioPs4Ops {
    int (*init)(void);
    int (*open)(int user, int type, int index, uint32_t grain, uint32_t rate, uint32_t format);
    int (*volume)(int handle, int flags, const int32_t *volumes);
    int (*output)(int handle, const void *samples);
    int (*close)(int handle);
} PwAudioPs4Ops;

/*
 * Initialize the platform operations structure.
 * sceAudioOutInit initialises the library for the whole process; a second
 * call fails, so the first result is kept and returned to later callers.
 */
static int platform_init(void)
{
    static int initialized = 0, result = -1;
    if (!initialized) {
        result = sceAudioOutInit();
        initialized = 1;
    }
    return result;
}

static int platform_open(int user, int type, int index, uint32_t grain, uint32_t rate, uint32_t format)
{
    return sceAudioOutOpen(user, type, index, grain, rate, format);
}

static int platform_volume(int handle, int flags, const int32_t *volumes)
{
    return sceAudioOutSetVolume(handle, flags, volumes);
}

static int platform_output(int handle, const void *samples)
{
    return sceAudioOutOutput(handle, samples);
}

static int platform_close(int handle)
{
    return sceAudioOutClose(handle);
}

/*
 * Fill the operations structure with PS4 platform functions.
 * Returns 0 on success, negative on error.
 */
int pw_audio_ps4_platform_ops(PwAudioPs4Ops *ops)
{
    if (!ops) return -1;
    
    ops->init = platform_init;
    ops->open = platform_open;
    ops->volume = platform_volume;
    ops->output = platform_output;
    ops->close = platform_close;
    
    return 0;
}

/*
 * Open the main audio port for a caller that mixes and clocks itself.
 * Returns the port handle on success, negative on error.
 */
int pw_audio_ps4_open_port(const PwAudioPs4Ops *ops, int *handle)
{
    int32_t volumes[8];
    int port;
    
    if (!ops || !handle || !ops->init || !ops->open || !ops->volume || !ops->close)
        return -1;
    
    if (ops->init() < 0)
        return -1;
    
    port = ops->open(PW_AUDIO_PS4_USER_SYSTEM, PW_AUDIO_PS4_PORT_MAIN,
                     PW_AUDIO_PS4_PORT_INDEX, PW_AUDIO_PS4_GRAIN,
                     PW_AUDIO_PS4_RATE, PW_AUDIO_PS4_FORMAT_S16_STEREO);
    
    if (port < 0)
        return -1;
    
    /* Set unity volume for all channels */
    for (unsigned i = 0; i < 8; i++)
        volumes[i] = PW_AUDIO_PS4_VOLUME_0DB;
    
    if (ops->volume(port, PW_AUDIO_PS4_VOLUME_FLAGS, volumes) < 0) {
        (void)ops->close(port);
        return -1;
    }
    
    *handle = port;
    return 0;
}
