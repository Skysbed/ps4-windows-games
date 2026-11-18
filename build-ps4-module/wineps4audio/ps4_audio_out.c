/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * ps4_audio_out.c - the PS4 sink for the wineps5 audio driver.
 *
 * prospero-win's driver (wineps5/unix.c) mixes every WASAPI stream into
 * 256-frame grains of 48 kHz interleaved stereo S16 and hands each grain to
 * a sink that blocks until the console has taken it, so the console's
 * audio port is the clock. On the PS5 the title provides that sink through
 * ntdll; here the driver owns it: libSceAudioOut is loaded at runtime (as
 * ps4_pad_input.c loads libScePad) and one MAIN port is opened for the
 * system user, the way OpenOrbis' audio sample does.
 *
 * Opened lazily on the first availability check, once per process. A
 * failure is reported once and the driver then reports itself unavailable,
 * so mmdevapi lists no device rather than a broken one.
 */
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <pthread.h>

int sceKernelLoadStartModule(const char *path, size_t argc, const void *argv, uint32_t flags,
                             void *opt, int *res);
int sceKernelDlsym(int32_t handle, const char *name, void **address);

enum { PS4_AUDIO_USER_SYSTEM = 0xff, PS4_AUDIO_PORT_MAIN = 0, PS4_AUDIO_S16_STEREO = 1,
       PS4_AUDIO_GRAIN = 256, PS4_AUDIO_RATE = 48000, PS4_AUDIO_VOLUME_0DB = 32768,
       PS4_AUDIO_VOLUME_LR = 3 };

static int (*audio_init)(void);
static int (*audio_open)(int32_t user, int32_t type, int32_t index, uint32_t len, uint32_t freq,
                         uint32_t param);
static int (*audio_output)(int32_t handle, const void *ptr);
static int (*audio_set_volume)(int32_t handle, int32_t flag, int32_t *vol);

static pthread_once_t once = PTHREAD_ONCE_INIT;
static int port = -1;

static void *sym(int handle, const char *name)
{
    void *p = NULL;
    if (handle < 0 || sceKernelDlsym(handle, name, &p) != 0) return NULL;
    return p;
}

static void open_port(void)
{
    int res = 0, lib, rc;
    int32_t vol[8];

    lib = sceKernelLoadStartModule("libSceAudioOut.sprx", 0, NULL, 0, NULL, &res);
    audio_init = sym(lib, "sceAudioOutInit");
    audio_open = sym(lib, "sceAudioOutOpen");
    audio_output = sym(lib, "sceAudioOutOutput");
    audio_set_volume = sym(lib, "sceAudioOutSetVolume");
    if (!audio_init || !audio_open || !audio_output)
    {
        fprintf(stderr, "wine-ps4 audio: libSceAudioOut not usable (module 0x%x) - no audio device\n",
                (unsigned)lib);
        return;
    }
    rc = audio_init();   /* an "already initialised" error is fine: the open decides */
    port = audio_open(PS4_AUDIO_USER_SYSTEM, PS4_AUDIO_PORT_MAIN, 0, PS4_AUDIO_GRAIN,
                      PS4_AUDIO_RATE, PS4_AUDIO_S16_STEREO);
    if (port < 0)
    {
        fprintf(stderr, "wine-ps4 audio: sceAudioOutOpen -> 0x%08x (init 0x%08x) - no audio device\n",
                (unsigned)port, (unsigned)rc);
        port = -1;
        return;
    }
    if (audio_set_volume)
    {
        for (int i = 0; i < 8; i++) vol[i] = PS4_AUDIO_VOLUME_0DB;
        (void)audio_set_volume(port, PS4_AUDIO_VOLUME_LR, vol);
    }
    fprintf(stderr, "wine-ps4 audio: sceAudioOut main port 0x%x open - 48 kHz stereo S16, %d-frame grains\n",
            (unsigned)port, PS4_AUDIO_GRAIN);
}

/* 1 when the console's port is open (opening it on first use). */
int ps4_audio_available(void)
{
    pthread_once(&once, open_port);
    return port >= 0;
}

/* Plays one 256-frame stereo grain; blocks until the console takes it.
 * 0 on success, as the driver expects of its sink. */
int ps4_audio_output(const int16_t *frames)
{
    static unsigned failed;
    int rc;

    if (port < 0 || !frames) return -1;
    rc = audio_output(port, frames);
    if (rc < 0)
    {
        if (failed++ < 4) fprintf(stderr, "wine-ps4 audio: sceAudioOutOutput -> 0x%08x\n", (unsigned)rc);
        return -1;
    }
    return 0;
}
