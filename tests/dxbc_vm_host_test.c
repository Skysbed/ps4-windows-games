/*
 * Host test for the DXBC interpreter (compat/pcps4_dxbc_vm.c).
 *
 * Shaders are assembled by hand as SM4/5 token streams in a minimal DXBC
 * container, covering the instruction forms real fxc output uses: masked and
 * swizzled operands, constant buffers, immediates, saturate, if/else, loops,
 * integer ops, relative indexing, and texture sampling through a callback.
 */

#include "../compat/pcps4_dxbc_vm.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        ++g_failures; \
    } \
} while (0)

#define CHECKF(got, want) do { \
    float g_ = (got), w_ = (want); \
    if (fabsf(g_ - w_) > 1e-5f) { \
        printf("FAIL %s:%d: %s = %g, want %g\n", __FILE__, __LINE__, #got, g_, w_); \
        ++g_failures; \
    } \
} while (0)

static uint32_t F(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

/* --- token builders ------------------------------------------------------ */
enum { T_TEMP = 0, T_INPUT = 1, T_OUTPUT = 2, T_XTEMP = 3, T_IMM = 4,
       T_SAMPLER = 6, T_RESOURCE = 7, T_CBV = 8, T_ICB = 9 };

#define OPC(op, len) ((uint32_t)(op) | ((uint32_t)(len) << 24))
#define SAT 0x2000u
#define TESTNZ 0x40000u

/* 4-component destination, write mask, 1 index dimension. */
#define DST(type, mask) (2u | ((uint32_t)(mask) << 4) | ((uint32_t)(type) << 12) | (1u << 20))
/* 4-component source, swizzle, 1 index dimension. */
#define SRC(type, swz) (2u | (1u << 2) | ((uint32_t)(swz) << 4) | ((uint32_t)(type) << 12) | (1u << 20))
/* Select-1 source (one component replicated). */
#define SEL1(type, comp) (2u | (2u << 2) | ((uint32_t)(comp) << 4) | ((uint32_t)(type) << 12) | (1u << 20))
/* 2-D indexed source (constant buffer slot, element). */
#define SRC2(type, swz) (2u | (1u << 2) | ((uint32_t)(swz) << 4) | ((uint32_t)(type) << 12) | (2u << 20))
/* 4-component immediate. */
#define IMM4 (2u | (1u << 2) | (0xE4u << 4) | (4u << 12))
#define XYZW 0xE4u
#define SWZ(x, y, z, w) ((x) | ((y) << 2) | ((z) << 4) | ((w) << 6))

enum { OP_ADD = 0, OP_BREAKC = 3, OP_DP4 = 17, OP_ELSE = 18, OP_ENDIF = 21,
       OP_ENDLOOP = 22, OP_IADD = 30, OP_IF = 31, OP_IGE = 33, OP_LOOP = 48,
       OP_LT = 49, OP_MAD = 50, OP_MOV = 54, OP_RET = 62, OP_SAMPLE = 69,
       OP_UTOF = 86, OP_UDIV = 78, OP_DCL_TEMPS = 104, OP_MOVC = 55,
       OP_SQRT = 75, OP_DISCARD = 13, OP_ITOF = 43 };

typedef struct {
    uint8_t bytes[4096];
    size_t size;
} blob_t;

/* Wrap a token body into a DXBC container with a single SHEX chunk. */
static void make_dxbc(blob_t *b, uint32_t program_type, const uint32_t *body,
                      size_t body_dwords)
{
    uint32_t hdr[9];
    uint32_t chunk_hdr[2];
    uint32_t ver = (program_type << 16) | (5u << 4);
    uint32_t len = (uint32_t)(body_dwords + 2);
    size_t off = 0;

    memset(b, 0, sizeof(*b));
    memcpy(hdr, "DXBC", 4);
    memset(&hdr[1], 0, 16);
    hdr[5] = 1;
    hdr[6] = 0; /* total size patched below */
    hdr[7] = 1; /* chunk count */
    hdr[8] = 36; /* chunk offset */
    memcpy(b->bytes + off, hdr, sizeof(hdr));
    off += sizeof(hdr);
    memcpy(chunk_hdr, "SHEX", 4);
    chunk_hdr[1] = (len) * 4;
    memcpy(b->bytes + off, chunk_hdr, 8);
    off += 8;
    memcpy(b->bytes + off, &ver, 4);
    off += 4;
    memcpy(b->bytes + off, &len, 4);
    off += 4;
    memcpy(b->bytes + off, body, body_dwords * 4);
    off += body_dwords * 4;
    b->size = off;
    {
        uint32_t total = (uint32_t)off;
        memcpy(b->bytes + 24, &total, 4);
    }
}

static pcps4_vm_t *build(uint32_t type, const uint32_t *body, size_t n, int *rc)
{
    blob_t b;
    make_dxbc(&b, type, body, n);
    return pcps4_vm_create(b.bytes, b.size, rc);
}

#define N(a) (sizeof(a) / sizeof((a)[0]))

/* --- tests --------------------------------------------------------------- */

static void test_mov_swizzle_mask(void)
{
    /* mov o0.xz, v0.wzyx ; ret */
    const uint32_t sh[] = {
        OPC(54, 5), DST(T_OUTPUT, 0x5), 0, SRC(T_INPUT, SWZ(3, 2, 1, 0)), 0,
        OPC(OP_RET, 1)
    };
    int rc;
    pcps4_vm_t *vm = build(1, sh, N(sh), &rc);
    CHECK(vm != NULL && rc == PCPS4_VM_OK);
    if (!vm) return;
    vm->in[0].f[0] = 1; vm->in[0].f[1] = 2; vm->in[0].f[2] = 3; vm->in[0].f[3] = 4;
    CHECK(pcps4_vm_run(vm) == PCPS4_VM_OK);
    CHECKF(vm->out[0].f[0], 4); /* x <- w */
    CHECKF(vm->out[0].f[1], 0); /* y unwritten */
    CHECKF(vm->out[0].f[2], 2); /* z <- y */
    CHECKF(vm->out[0].f[3], 0);
    pcps4_vm_destroy(vm);
}

static void test_dp4_constant_buffer(void)
{
    /* dp4 o0.x, v0, cb0[1] ; ret   (a matrix-row transform) */
    const uint32_t sh[] = {
        OPC(OP_DP4, 8), DST(T_OUTPUT, 0x1), 0,
            SRC(T_INPUT, XYZW), 0, SRC2(T_CBV, XYZW), 0, 1,
        OPC(OP_RET, 1)
    };
    static const pcps4_vm_reg_t cb[2] = {
        {{0, 0, 0, 0}}, {{1, 2, 3, 4}}
    };
    int rc;
    pcps4_vm_t *vm = build(1, sh, N(sh), &rc);
    CHECK(vm != NULL);
    if (!vm) return;
    vm->in[0].f[0] = 10; vm->in[0].f[1] = 20; vm->in[0].f[2] = 30; vm->in[0].f[3] = 40;
    vm->cb[0] = cb;
    vm->cb_size[0] = 2;
    CHECK(pcps4_vm_run(vm) == PCPS4_VM_OK);
    CHECKF(vm->out[0].f[0], 10 + 40 + 90 + 160);
    /* An out-of-range element must fail cleanly, not read garbage. */
    vm->cb_size[0] = 1;
    CHECK(pcps4_vm_run(vm) == PCPS4_VM_ERR_RUNTIME);
    pcps4_vm_destroy(vm);
}

static void test_mad_saturate_immediate(void)
{
    /* mad_sat o0, v0, l(2,2,2,2), l(0.5,0.5,0.5,0.5) ; ret */
    const uint32_t sh[] = {
        OPC(OP_MAD, 1 + 2 + 2 + 5 + 5) | SAT, DST(T_OUTPUT, 0xF), 0,
            SRC(T_INPUT, XYZW), 0,
            IMM4, F(2), F(2), F(2), F(2),
            IMM4, F(0.5f), F(0.5f), F(0.5f), F(0.5f),
        OPC(OP_RET, 1)
    };
    int rc;
    pcps4_vm_t *vm = build(0, sh, N(sh), &rc);
    CHECK(vm != NULL);
    if (!vm) return;
    vm->in[0].f[0] = 0.0f; vm->in[0].f[1] = 0.25f; vm->in[0].f[2] = 1.0f; vm->in[0].f[3] = -1.0f;
    CHECK(pcps4_vm_run(vm) == PCPS4_VM_OK);
    CHECKF(vm->out[0].f[0], 0.5f);
    CHECKF(vm->out[0].f[1], 1.0f);  /* 0.25*2+0.5 = 1.0 */
    CHECKF(vm->out[0].f[2], 1.0f);  /* 2.5 clamped */
    CHECKF(vm->out[0].f[3], 0.0f);  /* -1.5 clamped */
    pcps4_vm_destroy(vm);
}

static void test_if_else(void)
{
    /*
     * lt r0.x, v0.x, l(0.5)
     * if_nz r0.x ; mov o0, l(1,0,0,1) ; else ; mov o0, l(0,1,0,1) ; endif ; ret
     */
    const uint32_t sh[] = {
        OPC(OP_DCL_TEMPS, 2), 1,
        OPC(OP_LT, 1 + 2 + 2 + 5), DST(T_TEMP, 0x1), 0,
            SRC(T_INPUT, XYZW), 0, IMM4, F(0.5f), F(0.5f), F(0.5f), F(0.5f),
        OPC(OP_IF, 3) | TESTNZ, SEL1(T_TEMP, 0), 0,
            OPC(OP_MOV, 1 + 2 + 5), DST(T_OUTPUT, 0xF), 0,
                IMM4, F(1), F(0), F(0), F(1),
        OPC(OP_ELSE, 1),
            OPC(OP_MOV, 1 + 2 + 5), DST(T_OUTPUT, 0xF), 0,
                IMM4, F(0), F(1), F(0), F(1),
        OPC(OP_ENDIF, 1),
        OPC(OP_RET, 1)
    };
    int rc;
    pcps4_vm_t *vm = build(0, sh, N(sh), &rc);
    CHECK(vm != NULL && rc == PCPS4_VM_OK);
    if (!vm) return;
    vm->in[0].f[0] = 0.2f;
    CHECK(pcps4_vm_run(vm) == PCPS4_VM_OK);
    CHECKF(vm->out[0].f[0], 1); CHECKF(vm->out[0].f[1], 0);
    vm->in[0].f[0] = 0.9f;
    CHECK(pcps4_vm_run(vm) == PCPS4_VM_OK);
    CHECKF(vm->out[0].f[0], 0); CHECKF(vm->out[0].f[1], 1);
    pcps4_vm_destroy(vm);
}

static void test_loop_integer(void)
{
    /*
     * mov r0.x, l(0)
     * loop
     *   iadd r0.x, r0.x, l(1)
     *   ige  r0.y, r0.xxxx, l(5)
     *   breakc_nz r0.y
     * endloop
     * utof o0.x, r0.x ; ret       -> 5.0
     */
    const uint32_t sh[] = {
        OPC(OP_DCL_TEMPS, 2), 1,
        OPC(OP_MOV, 1 + 2 + 5), DST(T_TEMP, 0x1), 0, IMM4, 0, 0, 0, 0,
        OPC(OP_LOOP, 1),
          OPC(OP_IADD, 1 + 2 + 2 + 5), DST(T_TEMP, 0x1), 0,
              SRC(T_TEMP, XYZW), 0, IMM4, 1, 1, 1, 1,
          OPC(OP_IGE, 1 + 2 + 2 + 5), DST(T_TEMP, 0x2), 0,
              SEL1(T_TEMP, 0), 0, IMM4, 5, 5, 5, 5,
          OPC(OP_BREAKC, 3) | TESTNZ, SEL1(T_TEMP, 1), 0,
        OPC(OP_ENDLOOP, 1),
        OPC(OP_UTOF, 1 + 2 + 2), DST(T_OUTPUT, 0x1), 0, SEL1(T_TEMP, 0), 0,
        OPC(OP_RET, 1)
    };
    int rc;
    pcps4_vm_t *vm = build(1, sh, N(sh), &rc);
    CHECK(vm != NULL && rc == PCPS4_VM_OK);
    if (!vm) return;
    rc = pcps4_vm_run(vm);
    if (rc != PCPS4_VM_OK)
        printf("loop run rc=%d bad_opcode=%u\n", rc, vm->bad_opcode);
    CHECK(rc == PCPS4_VM_OK);
    CHECKF(vm->out[0].f[0], 5.0f);
    pcps4_vm_destroy(vm);
}

static void test_udiv_two_destinations(void)
{
    /* udiv o0.x, o0.y, l(17), l(5) ; ret  -> quotient 3, remainder 2 */
    const uint32_t sh[] = {
        OPC(OP_UDIV, 1 + 2 + 2 + 5 + 5), DST(T_OUTPUT, 0x1), 0, DST(T_OUTPUT, 0x2), 0,
            IMM4, 17, 17, 17, 17, IMM4, 5, 5, 5, 5,
        OPC(OP_RET, 1)
    };
    int rc;
    pcps4_vm_t *vm = build(1, sh, N(sh), &rc);
    CHECK(vm != NULL);
    if (!vm) return;
    CHECK(pcps4_vm_run(vm) == PCPS4_VM_OK);
    CHECK(vm->out[0].u[0] == 3);
    CHECK(vm->out[0].u[1] == 2);
    pcps4_vm_destroy(vm);
}

static void sample_cb(void *user, uint32_t tex, uint32_t smp, const float c[4],
                      float param, int mode, float out[4])
{
    int *calls = (int *)user;
    ++*calls;
    /* Encode what we were asked so the test can verify the plumbing. */
    out[0] = c[0] + (float)tex;
    out[1] = c[1] + (float)smp;
    out[2] = param;
    out[3] = (float)mode;
}

static void test_sample(void)
{
    /* sample o0.xyzw, v0, t3.xyzw, s2 ; ret */
    const uint32_t sh[] = {
        OPC(OP_SAMPLE, 1 + 2 + 2 + 2 + 2), DST(T_OUTPUT, 0xF), 0,
            SRC(T_INPUT, XYZW), 0, SRC(T_RESOURCE, XYZW), 3, SRC(T_SAMPLER, XYZW), 2,
        OPC(OP_RET, 1)
    };
    int rc, calls = 0;
    pcps4_vm_t *vm = build(0, sh, N(sh), &rc);
    CHECK(vm != NULL && rc == PCPS4_VM_OK);
    if (!vm) return;
    vm->sample = sample_cb;
    vm->sample_user = &calls;
    vm->in[0].f[0] = 0.25f; vm->in[0].f[1] = 0.5f;
    CHECK(pcps4_vm_run(vm) == PCPS4_VM_OK);
    CHECK(calls == 1);
    CHECKF(vm->out[0].f[0], 3.25f); /* u + texture slot */
    CHECKF(vm->out[0].f[1], 2.5f);  /* v + sampler slot */
    CHECKF(vm->out[0].f[3], (float)PCPS4_VM_SAMPLE_IMPLICIT);
    pcps4_vm_destroy(vm);
}

static void test_discard_and_unsupported(void)
{
    /* discard_nz l(1) -> pixel killed */
    const uint32_t sh[] = {
        OPC(OP_DISCARD, 1 + 5) | TESTNZ, IMM4, 1, 1, 1, 1,
        OPC(OP_RET, 1)
    };
    /* A real but unimplemented opcode (switch = 76) must be reported. */
    const uint32_t bad[] = { OPC(76, 1), OPC(OP_RET, 1) };
    int rc;
    pcps4_vm_t *vm = build(0, sh, N(sh), &rc);
    CHECK(vm != NULL);
    if (vm) {
        CHECK(pcps4_vm_run(vm) == PCPS4_VM_OK);
        CHECK(vm->discarded == 1);
        pcps4_vm_destroy(vm);
    }
    vm = build(0, bad, N(bad), &rc);
    CHECK(vm != NULL);
    if (vm) {
        CHECK(pcps4_vm_run(vm) == PCPS4_VM_ERR_UNSUPPORTED);
        CHECK(vm->bad_opcode == 76);
        pcps4_vm_destroy(vm);
    }
}

static void test_rejects_garbage(void)
{
    uint8_t junk[64] = {0};
    int rc = 0;
    CHECK(pcps4_vm_create(NULL, 0, &rc) == NULL);
    CHECK(pcps4_vm_create(junk, sizeof(junk), &rc) == NULL);
    CHECK(rc == PCPS4_VM_ERR_FORMAT);
    {
        /* Unbalanced if (no endif) must be rejected at load time. */
        const uint32_t sh[] = {
            OPC(OP_IF, 1 + 5) | TESTNZ, IMM4, 1, 1, 1, 1, OPC(OP_RET, 1)
        };
        CHECK(build(0, sh, N(sh), &rc) == NULL);
        CHECK(rc == PCPS4_VM_ERR_FORMAT);
    }
}

int main(void)
{
    test_mov_swizzle_mask();
    test_dp4_constant_buffer();
    test_mad_saturate_immediate();
    test_if_else();
    test_loop_integer();
    test_udiv_two_destinations();
    test_sample();
    test_discard_and_unsupported();
    test_rejects_garbage();

    if (g_failures != 0) {
        printf("dxbc_vm_host_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("dxbc_vm_host_test: all checks passed\n");
    return 0;
}
