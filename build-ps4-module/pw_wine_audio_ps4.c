/*
 * PS4 audio sink for Wine's audio driver.
 * 
 * This module provides the pw_wine_audio_output function that the wineps4
 * driver calls to output audio grains to the PS4's audio system.
 */
#include <stdint.h>
#include <string.h>
#include <pthread.h>

/* SCE AudioOut declarations */
extern int sceAudioOutInit(void);
extern int sceAudioOutOpen(int user, int type, int index, uint32_t grain, uint32_t rate, uint32_t format);
extern int sceAudioOutSetVolume(int handle, int flags, const int32_t *volumes);
extern int sceAudioOutOutput(int handle, const void *samples);
extern int sceAudioOutClose(int handle);

/* Audio format constants */
#define PW_AUDIO_RATE 48000
#define PW_AUDIO_CHANNELS 2
#define PW_AUDIO_GRAIN 256
#define PW_AUDIO_FORMAT_S16_STEREO 1

/* User and port constants */
#define PW_AUDIO_USER_SYSTEM 0xff
#define PW_AUDIO_PORT_MAIN 0
#define PW_AUDIO_PORT_INDEX 0

/* Volume constants */
#define PW_AUDIO_VOLUME_FLAGS 3
#define PW_AUDIO_VOLUME_0DB 0x8000

/* Global state */
static int audio_handle = -1;
static pthread_mutex_t audio_lock = PTHREAD_MUTEX_INITIALIZER;
static int initialized = 0;

/* Initialize the audio system */
static int audio_init(void)
{
    int result;
    int32_t volumes[8];
    
    if (initialized)
        return audio_handle >= 0 ? 0 : -1;
    
    initialized = 1;
    
    /* Initialize the audio library */
    result = sceAudioOutInit();
    if (result < 0)
        return result;
    
    /* Open the main audio port */
    audio_handle = sceAudioOutOpen(PW_AUDIO_USER_SYSTEM, PW_AUDIO_PORT_MAIN,
                                   PW_AUDIO_PORT_INDEX, PW_AUDIO_GRAIN,
                                   PW_AUDIO_RATE, PW_AUDIO_FORMAT_S16_STEREO);
    
    if (audio_handle < 0)
        return audio_handle;
    
    /* Set unity volume for all channels */
    for (int i = 0; i < 8; i++)
        volumes[i] = PW_AUDIO_VOLUME_0DB;
    
    result = sceAudioOutSetVolume(audio_handle, PW_AUDIO_VOLUME_FLAGS, volumes);
    if (result < 0)
    {
        sceAudioOutClose(audio_handle);
        audio_handle = -1;
        return result;
    }
    
    return 0;
}

/* Check if audio is available */
int pw_wine_audio_available(void)
{
    int result;
    pthread_mutex_lock(&audio_lock);
    result = audio_init() == 0 && audio_handle >= 0;
    pthread_mutex_unlock(&audio_lock);
    return result;
}

/* Output a grain of audio samples */
int pw_wine_audio_output(const int16_t *frames)
{
    int result;
    
    pthread_mutex_lock(&audio_lock);
    
    if (audio_handle < 0)
    {
        if (audio_init() < 0)
        {
            pthread_mutex_unlock(&audio_lock);
            return -1;
        }
    }
    
    result = sceAudioOutOutput(audio_handle, frames);
    
    pthread_mutex_unlock(&audio_lock);
    
    return result;
}

/* Clean up audio system */
void pw_wine_audio_cleanup(void)
{
    pthread_mutex_lock(&audio_lock);
    
    if (audio_handle >= 0)
    {
        sceAudioOutClose(audio_handle);
        audio_handle = -1;
    }
    
    pthread_mutex_unlock(&audio_lock);
}
