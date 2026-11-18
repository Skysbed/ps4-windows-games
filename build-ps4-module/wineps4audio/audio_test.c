/* audio_test.exe - plays a 440 Hz tone for ~5 s through WASAPI shared mode and reports each step
 * as "AUDIOTEST ..." lines on stdout (which reaches the PS4 klog). */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
#include <stdio.h>
#include <math.h>

DEFINE_GUID(CLSID_MMDeviceEnumerator_, 0xBCDE0395, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
DEFINE_GUID(IID_IMMDeviceEnumerator_, 0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);
DEFINE_GUID(IID_IAudioClient_, 0x1CB9AD4C, 0xDBFA, 0x4C32, 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2);
DEFINE_GUID(IID_IAudioRenderClient_, 0xF294ACFC, 0x3146, 0x4483, 0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2);
DEFINE_GUID(SUBTYPE_FLOAT_, 0x00000003, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);

#define T(...) do { printf("AUDIOTEST " __VA_ARGS__); printf("\n"); fflush(stdout); } while (0)
#define CHECK(hr, what) do { if (FAILED(hr)) { T("FAIL %s hr=0x%08lx", what, (unsigned long)(hr)); return 1; } \
                             else T("ok %s", what); } while (0)

int main(void)
{
    IMMDeviceEnumerator *en = NULL; IMMDevice *dev = NULL; IAudioClient *ac = NULL;
    IAudioRenderClient *rc = NULL; WAVEFORMATEX *wf = NULL; UINT32 bufsize = 0, pad = 0;
    HRESULT hr; double phase = 0; int is_float = 0;

    T("start");
    CHECK(CoInitializeEx(NULL, COINIT_MULTITHREADED), "CoInitializeEx");
    CHECK(CoCreateInstance(&CLSID_MMDeviceEnumerator_, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator_, (void **)&en),
          "CoCreateInstance(MMDeviceEnumerator)");
    CHECK(IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eRender, eConsole, &dev), "GetDefaultAudioEndpoint");
    CHECK(IMMDevice_Activate(dev, &IID_IAudioClient_, CLSCTX_ALL, NULL, (void **)&ac), "Activate(IAudioClient)");
    CHECK(IAudioClient_GetMixFormat(ac, &wf), "GetMixFormat");
    if (wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) is_float = 1;
    if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        IsEqualGUID(&((WAVEFORMATEXTENSIBLE *)wf)->SubFormat, &SUBTYPE_FLOAT_)) is_float = 1;
    T("mix format: tag 0x%x, %u ch, %lu Hz, %u bits, %s", wf->wFormatTag, wf->nChannels,
      (unsigned long)wf->nSamplesPerSec, wf->wBitsPerSample, is_float ? "float" : "pcm");
    CHECK(IAudioClient_Initialize(ac, AUDCLNT_SHAREMODE_SHARED, 0, 1000000 /* 100 ms */, 0, wf, NULL), "Initialize(shared, 100 ms)");
    CHECK(IAudioClient_GetBufferSize(ac, &bufsize), "GetBufferSize");
    T("buffer %u frames", bufsize);
    CHECK(IAudioClient_GetService(ac, &IID_IAudioRenderClient_, (void **)&rc), "GetService(IAudioRenderClient)");
    CHECK(IAudioClient_Start(ac), "Start");

    DWORD t0 = GetTickCount(), last = 0; UINT64 written = 0;
    while (GetTickCount() - t0 < 5000)
    {
        BYTE *data; UINT32 avail;
        if (FAILED(hr = IAudioClient_GetCurrentPadding(ac, &pad))) { T("FAIL GetCurrentPadding hr=0x%08lx", (unsigned long)hr); break; }
        avail = bufsize - pad;
        if (avail)
        {
            if (FAILED(hr = IAudioRenderClient_GetBuffer(rc, avail, &data))) { T("FAIL GetBuffer hr=0x%08lx", (unsigned long)hr); break; }
            for (UINT32 i = 0; i < avail; i++)
            {
                double s = 0.25 * sin(phase); phase += 2 * 3.14159265358979 * 440.0 / wf->nSamplesPerSec;
                for (UINT32 c = 0; c < wf->nChannels; c++)
                {
                    if (is_float) ((float *)data)[i * wf->nChannels + c] = (float)s;
                    else if (wf->wBitsPerSample == 16) ((short *)data)[i * wf->nChannels + c] = (short)(s * 32767);
                }
            }
            if (FAILED(hr = IAudioRenderClient_ReleaseBuffer(rc, avail, 0))) { T("FAIL ReleaseBuffer hr=0x%08lx", (unsigned long)hr); break; }
            written += avail;
        }
        if (GetTickCount() - last >= 500)
        {
            IAudioClock *clk = NULL; UINT64 pos = 0, freq = 0;
            last = GetTickCount();
            if (SUCCEEDED(IAudioClient_GetService(ac, &IID_IAudioClock, (void **)&clk)))
            { IAudioClock_GetFrequency(clk, &freq); IAudioClock_GetPosition(clk, &pos, NULL); IAudioClock_Release(clk); }
            T("t=%lu ms padding %u written %llu frames, clock %llu / %llu Hz (=%.2f s played)",
              (unsigned long)(last - t0), pad, (unsigned long long)written, (unsigned long long)pos,
              (unsigned long long)freq, freq ? (double)pos / freq : 0.0);
        }
        Sleep(10);
    }
    IAudioClient_Stop(ac);
    T("done: wrote %llu frames (%.2f s of audio)", (unsigned long long)written, (double)written / wf->nSamplesPerSec);
    return 0;
}
