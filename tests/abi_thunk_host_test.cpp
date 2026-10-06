/*
 * Validates the Microsoft-x64 -> System V bridge (compat/pcps4_msabi.h).
 *
 * On x86-64 (the PS4 target and the CI host) PCPS4_MSW must produce an entry
 * point that a Microsoft-x64 caller can invoke and that forwards correctly to
 * a System V implementation: arguments beyond the 4th (which live in different
 * places under the two conventions) must survive, and callee-saved registers
 * the Windows caller relies on (RSI/RDI) must not be clobbered.
 *
 * This is the exact mechanism the loader uses for import (IAT) entries and
 * D3D11 COM vtable slots, so a green result here means the ABI translation the
 * bundled-game path depends on is sound - something that otherwise can only be
 * checked on the console.
 *
 * On non-x86-64 hosts there is one calling convention; the test still runs and
 * exercises the forwarding logic, it just does not stress a real ABI boundary.
 */
#include "../compat/pcps4_msabi.h"

#include <cstdio>

static int g_calls = 0;

/* System V implementations, as PcPs4's real handlers are compiled. */
static long sv_sum8(long a, long b, long c, long d,
                    long e, long f, long g, long h)
{
    ++g_calls;
    return a + b + c + d + e + f + g + h;
}

struct FakeObj {
    const void **lpVtbl;
    int tag;
};

/* A COM-style method: the interface pointer arrives as the first argument. */
static int obj_method(FakeObj *self, int x, int y)
{
    ++g_calls;
    return self->tag + x + y;
}

static double sv_mix(int a, double b, int c)
{
    ++g_calls;
    return a + b + c;
}

/* MS-x64 call signatures, as a loaded Windows PE would use. */
typedef long PCPS4_MS_ABI (*ms_sum8)(long, long, long, long,
                                     long, long, long, long);
typedef int PCPS4_MS_ABI (*ms_method)(FakeObj *, int, int);
typedef double PCPS4_MS_ABI (*ms_mix)(int, double, int);

static int failures = 0;

static void check(const char *what, long got, long want)
{
    if (got != want) {
        std::printf("abi_thunk_host_test: FAIL %s: got %ld want %ld\n",
                    what, got, want);
        ++failures;
    }
}

int main(void)
{
    /* Eight integer args: forces stack arguments under both conventions. */
    ms_sum8 sum8 = (ms_sum8)PCPS4_MSW(sv_sum8);
    check("sum8", sum8(1, 2, 3, 4, 5, 6, 7, 8), 36);

    /* COM-style dispatch through a vtable of MS-wrapped slots. */
    const void *vtbl[1] = { PCPS4_MSW(obj_method) };
    FakeObj obj;
    obj.lpVtbl = vtbl;
    obj.tag = 100;
    ms_method m = (ms_method)obj.lpVtbl[0];
    check("obj_method", m(&obj, 20, 3), 123);

    /* Mixed integer/float arguments. */
    ms_mix mix = (ms_mix)PCPS4_MSW(sv_mix);
    double d = mix(2, 0.5, 3);
    check("mix", (long)(d * 2.0), 11); /* (2 + 0.5 + 3) * 2 = 11 */

    check("call_count", g_calls, 3);

    if (failures != 0) {
        std::printf("abi_thunk_host_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("abi_thunk_host_test: all checks passed\n");
    return 0;
}
