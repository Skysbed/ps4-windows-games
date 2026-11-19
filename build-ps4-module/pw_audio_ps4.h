/*
 * PS4 audio platform interface for Wine's audio driver.
 * The console's main audio port, for a caller that mixes and clocks
 * itself: Wine's audio driver (wineps4) hands the title one grain of
 * 16-bit stereo at a time, and each output call returns when the port has
 * taken it.
 */
#ifndef PW_AUDIO_PS4_H
#define PW_AUDIO_PS4_H

#include <stdint.h>

/* Audio format constants for PS4 */
enum { PW_AUDIO_PS4_RATE = 48000, PW_AUDIO_PS4_GRAIN = 256, PW_AUDIO_PS4_CHANNELS = 2 };

/* Platform operations structure */
typedef struct PwAudioPs4Ops {
    int (*init)(void);
    int (*open)(int user, int type, int index, uint32_t grain, uint32_t rate, uint32_t format);
    int (*volume)(int handle, int flags, const int32_t *volumes);
    int (*output)(int handle, const void *samples);
    int (*close)(int handle);
} PwAudioPs4Ops;

/* Initialize the platform operations structure */
int pw_audio_ps4_platform_ops(PwAudioPs4Ops *ops);

/* Open the main audio port for a caller that mixes and clocks itself */
int pw_audio_ps4_open_port(const PwAudioPs4Ops *ops, int *handle);

#endif /* PW_AUDIO_PS4_H */
