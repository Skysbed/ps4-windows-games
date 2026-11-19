/* Host-side test: clang -pthread test_xinput_ps4_pad.c -o test-xinput */
#include <assert.h>
#include <string.h>
#include "xinput_ps4_pad.c"

static PadData sample;
static int open_result = -1, fallback_calls, read_result;
static PadVibe vibration;
int32_t sceUserServiceInitialize(void *p) { (void)p; return 0; }
int32_t sceUserServiceGetInitialUser(int32_t *u) { *u = 1; return 0; }
int32_t scePadInit(void) { return 0; }
int32_t scePadOpen(int32_t u, int32_t t, int32_t i, void *p)
{ (void)u; (void)t; (void)i; (void)p; return open_result; }
int32_t scePadGetHandle(int32_t u, int32_t t, int32_t i)
{ (void)u; (void)t; (void)i; fallback_calls++; return 7; }
int32_t scePadReadState(int32_t h, PadData *d)
{ assert(h == 7); *d = sample; return read_result; }
int32_t scePadSetVibration(int32_t h, const PadVibe *v)
{ assert(h == 7); vibration = *v; return 0; }
static void *poll_many(void *arg)
{
    struct xinput_pad_state s;
    (void)arg;
    for (int i = 0; i < 10000; i++) {
        pad_state(&s);
        assert(s.connected && s.buttons == 0x1000 && s.packet == 3);
    }
    return NULL;
}
int main(void)
{
    struct xinput_pad_state s;
    struct xinput_rumble_params rumble = { 0x1234, 0xabcd };
    pthread_t threads[4];
    sample.connected = 1; sample.lx = sample.ly = sample.rx = sample.ry = 128;
    pad_state(&s); assert(s.connected && s.packet == 1 && fallback_calls == 1);
    sample.padding = 42; pad_state(&s); assert(s.packet == 1);
    sample.buttons = 0x4000; pad_state(&s); assert(s.buttons == 0x1000 && s.packet == 2);
    pad_rumble(&rumble); assert(vibration.large == 0x12 && vibration.small == 0xab);
    read_result = -1; pad_state(&s); assert(!s.connected);
    read_result = 0; pad_state(&s); assert(s.connected && s.packet == 3);
    for (int i = 0; i < 4; i++) assert(!pthread_create(&threads[i], NULL, poll_many, NULL));
    for (int i = 0; i < 4; i++) assert(!pthread_join(threads[i], NULL));
    return 0;
}
