/*
 * PCPS4 PS4 audio driver, Unix side (wineps4.so, wineps4.prx on the console).
 *
 * mmdevapi loads it by name like any Wine audio driver: HKCU\Software\Wine\
 * Drivers\Audio = "ps4" (the in-process server's default on PS4). There is one
 * render endpoint and no capture. Each stream keeps mmdevapi's ring of frames
 * in the format its client chose; one Unix thread mixes every started stream
 * into a grain of the console's format and hands it to the title's audio sink
 * (pw_wine_audio_output), which returns once the console's port has taken it,
 * so the port is the clock.
 *
 * The stream, buffer, timing and WoW64 code follows Wine's OSS driver
 * (dlls/wineoss.drv/oss.c).
 */
#if 0
#pragma makedep unix
#endif

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>

#define NONAMELESSUNION
#define NONAMELESSSTRUCT
#include "ntstatus.h"
#include "winternl.h"
#include "initguid.h"
#include "audioclient.h"
#include "mmddk.h"

#include "wine/debug.h"
#include "wine/unixlib.h"

#include "unixlib.h"

WINE_DEFAULT_DEBUG_CHANNEL(ps4audio);

/* Audio format constants for PS4 */
#define PW_AUDIO_PS4_RATE 48000
#define PW_AUDIO_PS4_GRAIN 256
#define PW_AUDIO_PS4_CHANNELS 2

/* Default period in 100-nanosecond units (10ms) */
static const REFERENCE_TIME def_period = 100000;
static const REFERENCE_TIME min_period = 50000;
static const char device_name[] = "ps4";
static const WCHAR endpoint_name[] = {'P','S','4',0};

static ULONG_PTR zero_bits = 0;

/* Stream structure */
struct ps4_stream
{
    WAVEFORMATEX *fmt;
    AUDCLNT_SHAREMODE share;
    UINT flags;
    HANDLE event;
    BOOL playing;

    UINT64 written_frames, last_pos_frames;
    UINT32 period_frames, bufsize_frames, tmp_buffer_frames;
    UINT32 period_output_frames, since_event;
    REFERENCE_TIME period;

    BYTE *local_buffer, *tmp_buffer;
    INT32 getbuf_last;

    /* Mixing state */
    float gain[8];
    float left[8], right[8];
    uint64_t step, phase;
    float previous[2], current[2];
    
    struct ps4_stream *next;
};

/* Global state */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t started = PTHREAD_COND_INITIALIZER;
static struct ps4_stream *streams;
static HANDLE mixer_thread;

/* Audio output function - provided by ntdll.prx */
static int (*pw_wine_audio_output)(const int16_t *frames);

static NTSTATUS ps4_not_implemented(void *args)
{
    return STATUS_SUCCESS;
}

/* Simple integer multiplication and division */
static int muldiv(int a, int b, int c)
{
    LONGLONG ret;

    if (!c) return -1;

    if (c < 0)
    {
        a = -a;
        c = -c;
    }

    if ((a < 0 && b < 0) || (a >= 0 && b >= 0))
        ret = (((LONGLONG)a * b) + (c / 2)) / c;
    else
        ret = (((LONGLONG)a * b) - (c / 2)) / c;

    if (ret > 2147483647 || ret < -2147483647) return -1;
    return (int)ret;
}

static NTSTATUS unlock_result(HRESULT *result, HRESULT value)
{
    *result = value;
    pthread_mutex_unlock(&lock);
    return STATUS_SUCCESS;
}

static struct ps4_stream *handle_get_stream(stream_handle h)
{
    return (struct ps4_stream *)(UINT_PTR)h;
}

/* Find the audio output function from ntdll.prx */
static void find_audio_output(void)
{
    if (pw_wine_audio_output) return;
    pw_wine_audio_output = (int (*)(const int16_t *))dlsym(RTLD_DEFAULT, "pw_wine_audio_output");
    if (!pw_wine_audio_output)
    {
        TRACE("pw_wine_audio_output not found\n");
    }
}

static NTSTATUS ps4_process_attach(void *args)
{
    find_audio_output();
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_test_connect(void *args)
{
    struct test_connect_params *params = args;

    find_audio_output();
    params->priority = pw_wine_audio_output ? Priority_Preferred : Priority_Unavailable;
    TRACE("priority %d\n", params->priority);
    return STATUS_SUCCESS;
}

static BOOL is_device(const char *device, EDataFlow flow)
{
    return flow == eRender && device && !strcmp(device, device_name);
}

static NTSTATUS ps4_get_endpoint_ids(void *args)
{
    struct get_endpoint_ids_params *params = args;
    unsigned int num = params->flow == eRender ? 1 : 0;
    unsigned int offset, needed;

    offset = needed = num * sizeof(*params->endpoints);
    if (num)
        needed += sizeof(endpoint_name) + ((sizeof(device_name) + 1) & ~1);

    params->num = num;
    params->default_idx = 0;
    if (needed > params->size)
    {
        params->size = needed;
        params->result = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        return STATUS_SUCCESS;
    }
    if (num)
    {
        params->endpoints[0].name = offset;
        memcpy((char *)params->endpoints + offset, endpoint_name, sizeof(endpoint_name));
        offset += sizeof(endpoint_name);
        params->endpoints[0].device = offset;
        memcpy((char *)params->endpoints + offset, device_name, sizeof(device_name));
    }
    params->result = S_OK;
    return STATUS_SUCCESS;
}

/* Check if a format is supported */
static HRESULT is_format_supported(const WAVEFORMATEX *fmt)
{
    if (fmt->wFormatTag != WAVE_FORMAT_PCM)
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    
    if (fmt->nChannels < 1 || fmt->nChannels > 8)
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    
    if (fmt->wBitsPerSample != 8 && fmt->wBitsPerSample != 16 && 
        fmt->wBitsPerSample != 24 && fmt->wBitsPerSample != 32)
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    
    return S_OK;
}

static WAVEFORMATEXTENSIBLE *clone_format(const WAVEFORMATEX *fmt)
{
    WAVEFORMATEXTENSIBLE *ret;
    size_t size = sizeof(WAVEFORMATEXTENSIBLE);

    ret = malloc(size);
    if (!ret)
        return NULL;

    memset(ret, 0, size);
    ret->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    ret->Format.nChannels = fmt->nChannels;
    ret->Format.nSamplesPerSec = fmt->nSamplesPerSec;
    ret->Format.wBitsPerSample = fmt->wBitsPerSample;
    ret->Format.nBlockAlign = fmt->nChannels * fmt->wBitsPerSample / 8;
    ret->Format.nAvgBytesPerSec = fmt->nSamplesPerSec * ret->Format.nBlockAlign;
    ret->Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    ret->Samples.wValidBitsPerSample = fmt->wBitsPerSample;
    ret->dwChannelMask = fmt->nChannels == 2 ? KSAUDIO_SPEAKER_STEREO : 0;
    
    if (fmt->wBitsPerSample == 8)
        ret->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    else if (fmt->wBitsPerSample == 16)
        ret->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    else if (fmt->wBitsPerSample == 24)
        ret->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    else if (fmt->wBitsPerSample == 32)
        ret->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    else
        ret->SubFormat = GUID_NULL;
    
    return ret;
}

static void free_stream(struct ps4_stream *stream)
{
    SIZE_T size = 0;

    if (stream->local_buffer)
        NtFreeVirtualMemory(GetCurrentProcess(), (void **)&stream->local_buffer, &size, MEM_RELEASE);
    size = 0;
    if (stream->tmp_buffer)
        NtFreeVirtualMemory(GetCurrentProcess(), (void **)&stream->tmp_buffer, &size, MEM_RELEASE);
    free(stream->fmt);
    free(stream);
}

static NTSTATUS ps4_create_stream(void *args)
{
    struct create_stream_params *params = args;
    WAVEFORMATEXTENSIBLE *fmtex;
    struct ps4_stream *stream;
    SIZE_T size;

    if (!is_device(params->device, params->flow))
    {
        params->result = AUDCLNT_E_DEVICE_INVALIDATED;
        return STATUS_SUCCESS;
    }
    if (FAILED(params->result = is_format_supported(params->fmt)))
        return STATUS_SUCCESS;

    stream = calloc(1, sizeof(*stream));
    if (!stream)
    {
        params->result = E_OUTOFMEMORY;
        return STATUS_SUCCESS;
    }

    fmtex = clone_format(params->fmt);
    if (!fmtex)
    {
        params->result = E_OUTOFMEMORY;
        goto exit;
    }
    stream->fmt = &fmtex->Format;

    stream->period = params->period;
    stream->period_frames = muldiv(params->fmt->nSamplesPerSec, params->period, 10000000);
    stream->period_output_frames = muldiv(PW_AUDIO_PS4_RATE, params->period, 10000000);
    if (stream->period_frames == 0 || stream->period_output_frames == 0)
    {
        params->result = E_INVALIDARG;
        goto exit;
    }

    stream->bufsize_frames = muldiv(params->duration, params->fmt->nSamplesPerSec, 10000000);
    if (params->share == AUDCLNT_SHAREMODE_EXCLUSIVE)
        stream->bufsize_frames -= stream->bufsize_frames % stream->period_frames;
    size = stream->bufsize_frames * params->fmt->nBlockAlign;
    if (!stream->bufsize_frames ||
        NtAllocateVirtualMemory(GetCurrentProcess(), (void **)&stream->local_buffer, zero_bits,
                                &size, MEM_COMMIT, PAGE_READWRITE))
    {
        params->result = E_OUTOFMEMORY;
        goto exit;
    }

    /* Initialize gain to unity */
    for (int i = 0; i < 8; i++)
        stream->gain[i] = 1.0f;
    
    /* Initialize channel mapping (simple stereo fold) */
    for (int i = 0; i < params->fmt->nChannels; i++)
    {
        stream->left[i] = (i == 0) ? 1.0f : 0.0f;  /* Left channel */
        stream->right[i] = (i == 1) ? 1.0f : (i == 0 && params->fmt->nChannels == 1) ? 1.0f : 0.0f;
    }
    for (int i = params->fmt->nChannels; i < 8; i++)
    {
        stream->left[i] = 0.0f;
        stream->right[i] = 0.0f;
    }

    stream->share = params->share;
    stream->flags = params->flags;

    pthread_mutex_lock(&lock);
    stream->next = streams;
    streams = stream;
    pthread_mutex_unlock(&lock);

    *params->channel_count = params->fmt->nChannels;
    *params->stream = (stream_handle)(UINT_PTR)stream;
    params->result = S_OK;
    return STATUS_SUCCESS;

exit:
    if (FAILED(params->result))
        free_stream(stream);
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_release_stream(void *args)
{
    struct release_stream_params *params = args;
    struct ps4_stream *stream = handle_get_stream(params->stream), **link;

    pthread_mutex_lock(&lock);
    for (link = &streams; *link; link = &(*link)->next)
        if (*link == stream)
        {
            *link = stream->next;
            break;
        }
    pthread_mutex_unlock(&lock);

    free_stream(stream);
    params->result = S_OK;
    return STATUS_SUCCESS;
}

/* Mixer thread: mixes all playing streams into stereo 16-bit output */
static void *mixer_loop(void *arg)
{
    int16_t output_grain[PW_AUDIO_PS4_CHANNELS * PW_AUDIO_PS4_GRAIN];
    struct ps4_stream *stream;
    BOOL playing;
    
    (void)arg;
    
    pthread_mutex_lock(&lock);
    for (;;)
    {
        playing = FALSE;
        for (stream = streams; stream; stream = stream->next)
            playing |= stream->playing;
        if (!playing)
        {
            pthread_cond_wait(&started, &lock);
            continue;
        }

        /* Simple mixing: just take the first stream for now */
        /* TODO: Implement proper mixing of all streams */
        for (stream = streams; stream; stream = stream->next)
        {
            if (!stream->playing) continue;
            
            /* For now, just output silence or copy from buffer */
            /* This is a placeholder - full mixing implementation needed */
        }
        
        /* Fill with silence for now */
        memset(output_grain, 0, sizeof(output_grain));
        
        pthread_mutex_unlock(&lock);

        if (pw_wine_audio_output)
            pw_wine_audio_output(output_grain);
        
        pthread_mutex_lock(&lock);
    }
    return NULL;
}

static NTSTATUS ps4_start(void *args)
{
    struct start_params *params = args;
    struct ps4_stream *stream = handle_get_stream(params->stream);
    static const WCHAR name[] = {'p','s','4','_','a','u','d','i','o','_','m','i','x','e','r',0};

    pthread_mutex_lock(&lock);

    if ((stream->flags & AUDCLNT_STREAMFLAGS_EVENTCALLBACK) && !stream->event)
        return unlock_result(&params->result, AUDCLNT_E_EVENTHANDLE_NOT_SET);

    if (stream->playing)
        return unlock_result(&params->result, AUDCLNT_E_NOT_STOPPED);

    if (!mixer_thread)
    {
        if (create_unix_thread(&mixer_thread, name, mixer_loop, NULL))
        {
            mixer_thread = NULL;
            return unlock_result(&params->result, E_OUTOFMEMORY);
        }
    }
    stream->playing = TRUE;
    stream->since_event = 0;
    pthread_cond_signal(&started);

    return unlock_result(&params->result, S_OK);
}

static NTSTATUS ps4_stop(void *args)
{
    struct stop_params *params = args;
    struct ps4_stream *stream = handle_get_stream(params->stream);

    pthread_mutex_lock(&lock);

    if (!stream->playing)
        return unlock_result(&params->result, S_FALSE);

    stream->playing = FALSE;

    return unlock_result(&params->result, S_OK);
}

static NTSTATUS ps4_reset(void *args)
{
    struct reset_params *params = args;
    struct ps4_stream *stream = handle_get_stream(params->stream);

    pthread_mutex_lock(&lock);

    if (stream->playing)
        return unlock_result(&params->result, AUDCLNT_E_NOT_STOPPED);

    if (stream->getbuf_last)
        return unlock_result(&params->result, AUDCLNT_E_BUFFER_OPERATION_PENDING);

    stream->written_frames = 0;
    stream->last_pos_frames = 0;
    stream->since_event = 0;
    
    /* Reset buffer */
    if (stream->local_buffer)
        memset(stream->local_buffer, 0, stream->bufsize_frames * stream->fmt->nBlockAlign);

    return unlock_result(&params->result, S_OK);
}

static void silence_buffer(struct ps4_stream *stream, BYTE *buffer, UINT32 frames)
{
    memset(buffer, 0, frames * stream->fmt->nBlockAlign);
}

static NTSTATUS ps4_get_render_buffer(void *args)
{
    struct get_render_buffer_params *params = args;
    struct ps4_stream *stream = handle_get_stream(params->stream);
    UINT32 write_pos, frames = params->frames;
    BYTE **data = params->data;
    SIZE_T size;

    pthread_mutex_lock(&lock);

    if (stream->getbuf_last)
        return unlock_result(&params->result, AUDCLNT_E_OUT_OF_ORDER);

    if (!frames)
        return unlock_result(&params->result, S_OK);

    if (stream->bufsize_frames && frames > stream->bufsize_frames)
        return unlock_result(&params->result, AUDCLNT_E_BUFFER_TOO_LARGE);

    write_pos = (UINT32)(stream->written_frames % stream->bufsize_frames);
    if (write_pos + frames > stream->bufsize_frames)
    {
        if (stream->tmp_buffer_frames < frames)
        {
            if (stream->tmp_buffer)
            {
                size = 0;
                NtFreeVirtualMemory(GetCurrentProcess(), (void **)&stream->tmp_buffer, &size, MEM_RELEASE);
                stream->tmp_buffer = NULL;
            }
            size = frames * stream->fmt->nBlockAlign;
            if (NtAllocateVirtualMemory(GetCurrentProcess(), (void **)&stream->tmp_buffer, zero_bits,
                                        &size, MEM_COMMIT, PAGE_READWRITE))
            {
                stream->tmp_buffer_frames = 0;
                return unlock_result(&params->result, E_OUTOFMEMORY);
            }
            stream->tmp_buffer_frames = frames;
        }
        *data = stream->tmp_buffer;
        stream->getbuf_last = -(int)frames;
    }
    else
    {
        *data = stream->local_buffer + write_pos * stream->fmt->nBlockAlign;
        stream->getbuf_last = (int)frames;
    }

    silence_buffer(stream, *data, frames);

    return unlock_result(&params->result, S_OK);
}

static void wrap_buffer(struct ps4_stream *stream, BYTE *buffer, UINT32 written_frames)
{
    UINT32 write_offs_frames = (UINT32)(stream->written_frames % stream->bufsize_frames);
    UINT32 write_offs_bytes = write_offs_frames * stream->fmt->nBlockAlign;
    UINT32 chunk_frames = stream->bufsize_frames - write_offs_frames;
    UINT32 chunk_bytes = chunk_frames * stream->fmt->nBlockAlign;
    UINT32 written_bytes = written_frames * stream->fmt->nBlockAlign;

    if (written_bytes <= chunk_bytes)
        memcpy(stream->local_buffer + write_offs_bytes, buffer, written_bytes);
    else
    {
        memcpy(stream->local_buffer + write_offs_bytes, buffer, chunk_bytes);
        memcpy(stream->local_buffer, buffer + chunk_bytes, written_bytes - chunk_bytes);
    }
}

static NTSTATUS ps4_release_render_buffer(void *args)
{
    struct release_render_buffer_params *params = args;
    struct ps4_stream *stream = handle_get_stream(params->stream);
    UINT32 written_frames = params->written_frames;
    BYTE *buffer;

    pthread_mutex_lock(&lock);

    if (!written_frames)
    {
        stream->getbuf_last = 0;
        return unlock_result(&params->result, S_OK);
    }

    if (!stream->getbuf_last)
        return unlock_result(&params->result, AUDCLNT_E_OUT_OF_ORDER);

    if (written_frames > (stream->getbuf_last >= 0 ? stream->getbuf_last : -stream->getbuf_last))
        return unlock_result(&params->result, AUDCLNT_E_INVALID_SIZE);

    if (stream->getbuf_last >= 0)
        buffer = stream->local_buffer + stream->fmt->nBlockAlign *
                 ((UINT32)(stream->written_frames % stream->bufsize_frames));
    else
        buffer = stream->tmp_buffer;

    if (params->flags & AUDCLNT_BUFFERFLAGS_SILENT)
        silence_buffer(stream, buffer, written_frames);

    if (stream->getbuf_last < 0)
        wrap_buffer(stream, buffer, written_frames);

    stream->written_frames += written_frames;
    stream->getbuf_last = 0;

    return unlock_result(&params->result, S_OK);
}

static NTSTATUS ps4_is_format_supported(void *args)
{
    struct is_format_supported_params *params = args;

    if (!is_device(params->device, params->flow))
        params->result = AUDCLNT_E_DEVICE_INVALIDATED;
    else
        params->result = is_format_supported(params->fmt_in);
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_get_mix_format(void *args)
{
    struct get_mix_format_params *params = args;
    WAVEFORMATEXTENSIBLE *fmt = params->fmt;

    if (!is_device(params->device, params->flow))
    {
        params->result = params->flow == eRender ? AUDCLNT_E_DEVICE_INVALIDATED : E_UNEXPECTED;
        return STATUS_SUCCESS;
    }

    fmt->Format.wFormatTag = WAVE_FORMAT_PCM;
    fmt->Format.nChannels = PW_AUDIO_PS4_CHANNELS;
    fmt->Format.nSamplesPerSec = PW_AUDIO_PS4_RATE;
    fmt->Format.wBitsPerSample = 16;
    fmt->Format.nBlockAlign = fmt->Format.nChannels * fmt->Format.wBitsPerSample / 8;
    fmt->Format.nAvgBytesPerSec = fmt->Format.nSamplesPerSec * fmt->Format.nBlockAlign;
    fmt->Format.cbSize = 0;
    fmt->Samples.wValidBitsPerSample = fmt->Format.wBitsPerSample;
    fmt->dwChannelMask = KSAUDIO_SPEAKER_STEREO;
    fmt->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

    params->result = S_OK;
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_get_device_period(void *args)
{
    struct get_device_period_params *params = args;

    if (params->def_period)
        *params->def_period = def_period;
    if (params->min_period)
        *params->min_period = min_period;

    params->result = S_OK;
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_get_buffer_size(void *args)
{
    struct get_buffer_size_params *params = args;
    struct ps4_stream *stream = handle_get_stream(params->stream);

    pthread_mutex_lock(&lock);

    *params->frames = stream->bufsize_frames;

    return unlock_result(&params->result, S_OK);
}

static NTSTATUS ps4_get_latency(void *args)
{
    struct get_latency_params *params = args;
    struct ps4_stream *stream = handle_get_stream(params->stream);

    pthread_mutex_lock(&lock);

    *params->latency = stream->period + (REFERENCE_TIME)PW_AUDIO_PS4_GRAIN * 10000000 / PW_AUDIO_PS4_RATE;

    return unlock_result(&params->result, S_OK);
}

static NTSTATUS ps4_get_current_padding(void *args)
{
    struct get_current_padding_params *params = args;
    struct ps4_stream *stream = handle_get_stream(params->stream);

    pthread_mutex_lock(&lock);

    *params->padding = (UINT32)(stream->written_frames % stream->bufsize_frames);

    return unlock_result(&params->result, S_OK);
}

static NTSTATUS ps4_get_next_packet_size(void *args)
{
    struct get_next_packet_size_params *params = args;
    struct ps4_stream *stream = handle_get_stream(params->stream);

    pthread_mutex_lock(&lock);

    *params->frames = stream->bufsize_frames < stream->period_frames ? 0 : stream->period_frames;

    return unlock_result(&params->result, S_OK);
}

static NTSTATUS ps4_get_frequency(void *args)
{
    struct get_frequency_params *params = args;
    struct ps4_stream *stream = handle_get_stream(params->stream);

    pthread_mutex_lock(&lock);

    if (stream->share == AUDCLNT_SHAREMODE_SHARED)
        *params->freq = (UINT64)stream->fmt->nSamplesPerSec * stream->fmt->nBlockAlign;
    else
        *params->freq = stream->fmt->nSamplesPerSec;

    return unlock_result(&params->result, S_OK);
}

static NTSTATUS ps4_get_position(void *args)
{
    struct get_position_params *params = args;
    struct ps4_stream *stream = handle_get_stream(params->stream);
    UINT64 *pos = params->pos, *qpctime = params->qpctime;

    if (params->device)
    {
        FIXME("Device position reporting not implemented\n");
        params->result = E_NOTIMPL;
        return STATUS_SUCCESS;
    }

    pthread_mutex_lock(&lock);

    *pos = stream->written_frames;
    if (*pos < stream->last_pos_frames)
        *pos = stream->last_pos_frames;
    stream->last_pos_frames = *pos;

    if (stream->share == AUDCLNT_SHAREMODE_SHARED)
        *pos *= stream->fmt->nBlockAlign;

    if (qpctime)
    {
        LARGE_INTEGER stamp, freq;
        NtQueryPerformanceCounter(&stamp, &freq);
        *qpctime = (stamp.QuadPart * (INT64)10000000) / freq.QuadPart;
    }

    return unlock_result(&params->result, S_OK);
}

static NTSTATUS ps4_set_volumes(void *args)
{
    struct set_volumes_params *params = args;
    struct ps4_stream *stream = handle_get_stream(params->stream);
    float master = params->master_volume;

    pthread_mutex_lock(&lock);
    for (unsigned int i = 0; i < stream->fmt->nChannels && i < 8; i++)
        stream->gain[i] = master * params->volumes[i] * params->session_volumes[i];
    pthread_mutex_unlock(&lock);

    return STATUS_SUCCESS;
}

static NTSTATUS ps4_set_event_handle(void *args)
{
    struct set_event_handle_params *params = args;
    struct ps4_stream *stream = handle_get_stream(params->stream);

    pthread_mutex_lock(&lock);

    if (!(stream->flags & AUDCLNT_STREAMFLAGS_EVENTCALLBACK))
        return unlock_result(&params->result, AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED);

    if (stream->event)
    {
        FIXME("called twice\n");
        return unlock_result(&params->result, HRESULT_FROM_WIN32(ERROR_INVALID_NAME));
    }

    stream->event = params->event;

    return unlock_result(&params->result, S_OK);
}

static NTSTATUS ps4_set_sample_rate(void *args)
{
    struct set_sample_rate_params *params = args;
    struct ps4_stream *stream = handle_get_stream(params->stream);

    pthread_mutex_lock(&lock);

    if (!(stream->flags & AUDCLNT_STREAMFLAGS_RATEADJUST))
        return unlock_result(&params->result, AUDCLNT_E_UNSUPPORTED_FORMAT);
    
    /* For now, just accept the rate change */
    /* TODO: Implement resampling */

    return unlock_result(&params->result, S_OK);
}

static NTSTATUS ps4_is_started(void *args)
{
    struct is_started_params *params = args;
    struct ps4_stream *stream = handle_get_stream(params->stream);

    pthread_mutex_lock(&lock);

    return unlock_result(&params->result, stream->playing ? S_OK : S_FALSE);
}

static NTSTATUS ps4_get_prop_value(void *args)
{
    struct get_prop_value_params *params = args;
    params->result = E_NOTIMPL;
    return STATUS_SUCCESS;
}

/* MIDI stubs - not supported on PS4 */
static NTSTATUS ps4_midi_init(void *args)
{
    struct midi_init_params *params = args;
    *params->err = DRV_SUCCESS;
    return STATUS_SUCCESS;
}

static UINT no_device(UINT msg, UINT count_msg)
{
    return msg == count_msg ? 0 : MMSYSERR_BADDEVICEID;
}

static NTSTATUS ps4_midi_out_message(void *args)
{
    struct midi_out_message_params *params = args;
    params->notify->send_notify = FALSE;
    *params->err = no_device(params->msg, MODM_GETNUMDEVS);
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_midi_in_message(void *args)
{
    struct midi_in_message_params *params = args;
    params->notify->send_notify = FALSE;
    *params->err = no_device(params->msg, MIDM_GETNUMDEVS);
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_midi_notify_wait(void *args)
{
    struct midi_notify_wait_params *params = args;
    *params->quit = TRUE;
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_aux_message(void *args)
{
    struct aux_message_params *params = args;
    *params->err = no_device(params->msg, AUXDM_GETNUMDEVS);
    return STATUS_SUCCESS;
}

/* Function table for 64-bit */
const unixlib_entry_t __wine_unix_call_funcs[] =
{
    ps4_process_attach,
    ps4_not_implemented,
    ps4_not_implemented,
    ps4_not_implemented,
    ps4_get_endpoint_ids,
    ps4_create_stream,
    ps4_release_stream,
    ps4_start,
    ps4_stop,
    ps4_reset,
    ps4_get_render_buffer,
    ps4_release_render_buffer,
    ps4_not_implemented,
    ps4_not_implemented,
    ps4_is_format_supported,
    ps4_not_implemented,
    ps4_get_mix_format,
    ps4_get_device_period,
    ps4_get_buffer_size,
    ps4_get_latency,
    ps4_get_current_padding,
    ps4_get_next_packet_size,
    ps4_get_frequency,
    ps4_get_position,
    ps4_set_volumes,
    ps4_set_event_handle,
    ps4_set_sample_rate,
    ps4_test_connect,
    ps4_is_started,
    ps4_get_prop_value,
    ps4_not_implemented,
    ps4_midi_init,
    ps4_not_implemented,
    ps4_midi_out_message,
    ps4_midi_in_message,
    ps4_midi_notify_wait,
    ps4_aux_message,
};

#ifdef _WIN64

/* WoW64 thunks */
typedef UINT PTR32;

static NTSTATUS ps4_wow64_process_attach(void *args)
{
    SYSTEM_BASIC_INFORMATION info;
    NtQuerySystemInformation(SystemEmulationBasicInformation, &info, sizeof(info), NULL);
    zero_bits = (ULONG_PTR)info.HighestUserAddress | 0x7fffffff;
    find_audio_output();
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_wow64_test_connect(void *args)
{
    struct
    {
        PTR32 name;
        enum driver_priority priority;
    } *params32 = args;
    struct test_connect_params params =
    {
        .name = ULongToPtr(params32->name),
    };
    ps4_test_connect(&params);
    params32->priority = params.priority;
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_wow64_get_endpoint_ids(void *args)
{
    struct
    {
        EDataFlow flow;
        PTR32 endpoints;
        unsigned int size;
        HRESULT result;
        unsigned int num;
        unsigned int default_idx;
    } *params32 = args;
    struct get_endpoint_ids_params params =
    {
        .flow = params32->flow,
        .endpoints = ULongToPtr(params32->endpoints),
        .size = params32->size
    };
    ps4_get_endpoint_ids(&params);
    params32->size = params.size;
    params32->result = params.result;
    params32->num = params.num;
    params32->default_idx = params.default_idx;
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_wow64_create_stream(void *args)
{
    struct
    {
        PTR32 name;
        PTR32 device;
        EDataFlow flow;
        AUDCLNT_SHAREMODE share;
        UINT flags;
        REFERENCE_TIME duration;
        REFERENCE_TIME period;
        PTR32 fmt;
        HRESULT result;
        PTR32 channel_count;
        PTR32 stream;
    } *params32 = args;
    struct create_stream_params params =
    {
        .name = ULongToPtr(params32->name),
        .device = ULongToPtr(params32->device),
        .flow = params32->flow,
        .share = params32->share,
        .flags = params32->flags,
        .duration = params32->duration,
        .period = params32->period,
        .fmt = ULongToPtr(params32->fmt),
        .channel_count = ULongToPtr(params32->channel_count),
        .stream = ULongToPtr(params32->stream)
    };
    ps4_create_stream(&params);
    params32->result = params.result;
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_wow64_release_stream(void *args)
{
    struct
    {
        stream_handle stream;
        HRESULT result;
    } *params32 = args;
    struct release_stream_params params =
    {
        .stream = params32->stream,
    };
    ps4_release_stream(&params);
    params32->result = params.result;
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_wow64_get_render_buffer(void *args)
{
    struct
    {
        stream_handle stream;
        UINT32 frames;
        HRESULT result;
        PTR32 data;
    } *params32 = args;
    BYTE *data = NULL;
    struct get_render_buffer_params params =
    {
        .stream = params32->stream,
        .frames = params32->frames,
        .data = &data
    };
    ps4_get_render_buffer(&params);
    params32->result = params.result;
    *(unsigned int *)ULongToPtr(params32->data) = PtrToUlong(data);
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_wow64_release_render_buffer(void *args)
{
    struct
    {
        stream_handle stream;
        UINT32 written_frames;
        UINT32 flags;
        HRESULT result;
    } *params32 = args;
    struct release_render_buffer_params params =
    {
        .stream = params32->stream,
        .written_frames = params32->written_frames,
        .flags = params32->flags
    };
    ps4_release_render_buffer(&params);
    params32->result = params.result;
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_wow64_is_format_supported(void *args)
{
    struct
    {
        PTR32 device;
        EDataFlow flow;
        AUDCLNT_SHAREMODE share;
        PTR32 fmt_in;
        HRESULT result;
    } *params32 = args;
    struct is_format_supported_params params =
    {
        .device = ULongToPtr(params32->device),
        .flow = params32->flow,
        .share = params32->share,
        .fmt_in = ULongToPtr(params32->fmt_in),
    };
    ps4_is_format_supported(&params);
    params32->result = params.result;
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_wow64_get_mix_format(void *args)
{
    struct
    {
        PTR32 device;
        EDataFlow flow;
        PTR32 fmt;
        HRESULT result;
    } *params32 = args;
    struct get_mix_format_params params =
    {
        .device = ULongToPtr(params32->device),
        .flow = params32->flow,
        .fmt = ULongToPtr(params32->fmt)
    };
    ps4_get_mix_format(&params);
    params32->result = params.result;
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_wow64_get_device_period(void *args)
{
    struct
    {
        PTR32 device;
        EDataFlow flow;
        HRESULT result;
        PTR32 def_period;
        PTR32 min_period;
    } *params32 = args;
    struct get_device_period_params params =
    {
        .device = ULongToPtr(params32->device),
        .flow = params32->flow,
        .def_period = ULongToPtr(params32->def_period),
        .min_period = ULongToPtr(params32->min_period),
    };
    ps4_get_device_period(&params);
    params32->result = params.result;
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_wow64_get_buffer_size(void *args)
{
    struct
    {
        stream_handle stream;
        HRESULT result;
        PTR32 frames;
    } *params32 = args;
    struct get_buffer_size_params params =
    {
        .stream = params32->stream,
        .frames = ULongToPtr(params32->frames)
    };
    ps4_get_buffer_size(&params);
    params32->result = params.result;
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_wow64_get_latency(void *args)
{
    struct
    {
        stream_handle stream;
        HRESULT result;
        PTR32 latency;
    } *params32 = args;
    struct get_latency_params params =
    {
        .stream = params32->stream,
        .latency = ULongToPtr(params32->latency)
    };
    ps4_get_latency(&params);
    params32->result = params.result;
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_wow64_get_current_padding(void *args)
{
    struct
    {
        stream_handle stream;
        HRESULT result;
        PTR32 padding;
    } *params32 = args;
    struct get_current_padding_params params =
    {
        .stream = params32->stream,
        .padding = ULongToPtr(params32->padding)
    };
    ps4_get_current_padding(&params);
    params32->result = params.result;
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_wow64_get_next_packet_size(void *args)
{
    struct
    {
        stream_handle stream;
        HRESULT result;
        PTR32 frames;
    } *params32 = args;
    struct get_next_packet_size_params params =
    {
        .stream = params32->stream,
        .frames = ULongToPtr(params32->frames)
    };
    ps4_get_next_packet_size(&params);
    params32->result = params.result;
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_wow64_get_frequency(void *args)
{
    struct
    {
        stream_handle stream;
        HRESULT result;
        PTR32 freq;
    } *params32 = args;
    struct get_frequency_params params =
    {
        .stream = params32->stream,
        .freq = ULongToPtr(params32->freq)
    };
    ps4_get_frequency(&params);
    params32->result = params.result;
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_wow64_get_position(void *args)
{
    struct
    {
        stream_handle stream;
        BOOL device;
        HRESULT result;
        PTR32 pos;
        PTR32 qpctime;
    } *params32 = args;
    struct get_position_params params =
    {
        .stream = params32->stream,
        .device = params32->device,
        .pos = ULongToPtr(params32->pos),
        .qpctime = ULongToPtr(params32->qpctime)
    };
    ps4_get_position(&params);
    params32->result = params.result;
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_wow64_set_volumes(void *args)
{
    struct
    {
        stream_handle stream;
        float master_volume;
        PTR32 volumes;
        PTR32 session_volumes;
    } *params32 = args;
    struct set_volumes_params params =
    {
        .stream = params32->stream,
        .master_volume = params32->master_volume,
        .volumes = ULongToPtr(params32->volumes),
        .session_volumes = ULongToPtr(params32->session_volumes),
    };
    return ps4_set_volumes(&params);
}

static NTSTATUS ps4_wow64_set_event_handle(void *args)
{
    struct
    {
        stream_handle stream;
        PTR32 event;
        HRESULT result;
    } *params32 = args;
    struct set_event_handle_params params =
    {
        .stream = params32->stream,
        .event = ULongToHandle(params32->event)
    };
    ps4_set_event_handle(&params);
    params32->result = params.result;
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_wow64_set_sample_rate(void *args)
{
    struct
    {
        stream_handle stream;
        float rate;
        HRESULT result;
    } *params32 = args;
    struct set_sample_rate_params params =
    {
        .stream = params32->stream,
        .rate = params32->rate
    };
    ps4_set_sample_rate(&params);
    params32->result = params.result;
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_wow64_is_started(void *args)
{
    struct
    {
        stream_handle stream;
        HRESULT result;
    } *params32 = args;
    struct is_started_params params =
    {
        .stream = params32->stream,
    };
    ps4_is_started(&params);
    params32->result = params.result;
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_wow64_get_prop_value(void *args)
{
    struct
    {
        PTR32 device;
        EDataFlow flow;
        PTR32 guid;
        PTR32 prop;
        HRESULT result;
        PTR32 value;
        PTR32 buffer;
        PTR32 buffer_size;
    } *params32 = args;

    params32->result = E_NOTIMPL;
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_wow64_midi_init(void *args)
{
    struct
    {
        PTR32 err;
    } *params32 = args;

    *(UINT *)ULongToPtr(params32->err) = DRV_SUCCESS;
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_wow64_midi_out_message(void *args)
{
    struct
    {
        UINT dev_id;
        UINT msg;
        UINT user;
        UINT param_1;
        UINT param_2;
        PTR32 err;
        PTR32 notify;
    } *params32 = args;

    *(BOOL *)ULongToPtr(params32->notify) = FALSE;
    *(UINT *)ULongToPtr(params32->err) = no_device(params32->msg, MODM_GETNUMDEVS);
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_wow64_midi_in_message(void *args)
{
    struct
    {
        UINT dev_id;
        UINT msg;
        UINT user;
        UINT param_1;
        UINT param_2;
        PTR32 err;
        PTR32 notify;
    } *params32 = args;

    *(BOOL *)ULongToPtr(params32->notify) = FALSE;
    *(UINT *)ULongToPtr(params32->err) = no_device(params32->msg, MIDM_GETNUMDEVS);
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_wow64_midi_notify_wait(void *args)
{
    struct
    {
        PTR32 quit;
        PTR32 notify;
    } *params32 = args;

    *(BOOL *)ULongToPtr(params32->quit) = TRUE;
    return STATUS_SUCCESS;
}

static NTSTATUS ps4_wow64_aux_message(void *args)
{
    struct
    {
        UINT dev_id;
        UINT msg;
        UINT user;
        UINT param_1;
        UINT param_2;
        PTR32 err;
    } *params32 = args;

    *(UINT *)ULongToPtr(params32->err) = no_device(params32->msg, AUXDM_GETNUMDEVS);
    return STATUS_SUCCESS;
}

/* WoW64 function table */
const unixlib_entry_t __wine_unix_call_wow64_funcs[] =
{
    ps4_wow64_process_attach,
    ps4_not_implemented,
    ps4_not_implemented,
    ps4_not_implemented,
    ps4_wow64_get_endpoint_ids,
    ps4_wow64_create_stream,
    ps4_wow64_release_stream,
    ps4_start,
    ps4_stop,
    ps4_reset,
    ps4_wow64_get_render_buffer,
    ps4_wow64_release_render_buffer,
    ps4_not_implemented,
    ps4_not_implemented,
    ps4_wow64_is_format_supported,
    ps4_not_implemented,
    ps4_wow64_get_mix_format,
    ps4_wow64_get_device_period,
    ps4_wow64_get_buffer_size,
    ps4_wow64_get_latency,
    ps4_wow64_get_current_padding,
    ps4_wow64_get_next_packet_size,
    ps4_wow64_get_frequency,
    ps4_wow64_get_position,
    ps4_wow64_set_volumes,
    ps4_wow64_set_event_handle,
    ps4_wow64_set_sample_rate,
    ps4_wow64_test_connect,
    ps4_wow64_is_started,
    ps4_wow64_get_prop_value,
    ps4_not_implemented,
    ps4_wow64_midi_init,
    ps4_not_implemented,
    ps4_wow64_midi_out_message,
    ps4_wow64_midi_in_message,
    ps4_wow64_midi_notify_wait,
    ps4_wow64_aux_message,
};

#endif /* _WIN64 */
