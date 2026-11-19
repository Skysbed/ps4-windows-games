/*
 * PS4 audio sink for Wine's audio driver.
 */
#ifndef PW_WINE_AUDIO_PS4_H
#define PW_WINE_AUDIO_PS4_H

#include <stdint.h>

/* Check if audio is available */
int pw_wine_audio_available(void);

/* Output a grain of audio samples (stereo 16-bit, 48 kHz) */
int pw_wine_audio_output(const int16_t *frames);

/* Clean up audio system */
void pw_wine_audio_cleanup(void);

#endif /* PW_WINE_AUDIO_PS4_H */
