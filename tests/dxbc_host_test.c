/*
 * Host-side unit test for the DXBC shader inspector and signature extractor.
 *
 * Builds a minimal but structurally valid DXBC container holding an ISGN input
 * signature (POSITION0, TEXCOORD0) and a SHEX shader chunk tagged as a vertex
 * shader, model 5.0. Checks that inspect() decodes the shader model and that
 * extract_signature() reads back the element semantics, registers and types.
 */

#include "../compat/pcps4_shader.h"

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

static void w32(uint8_t *b, size_t o, uint32_t v)
{
    b[o + 0] = (uint8_t)(v & 0xFF);
    b[o + 1] = (uint8_t)((v >> 8) & 0xFF);
    b[o + 2] = (uint8_t)((v >> 16) & 0xFF);
    b[o + 3] = (uint8_t)((v >> 24) & 0xFF);
}

/*
 * Layout:
 *   [0]   DXBC header (32 bytes): magic, 16-byte digest, version, totalSize,
 *         chunkCount
 *   [32]  chunk offset table: 2 x u32
 *   ISGN chunk: "ISGN" + size + body
 *   SHEX chunk: "SHEX" + size + body
 */

#define DXBC_SIZE 256

/* ISGN body: 2 elements (POSITION0, TEXCOORD0). */
static uint32_t build_isgn_body(uint8_t *b, size_t at)
{
    size_t names;
    /* header */
    w32(b, at + 0, 2);   /* element count */
    w32(b, at + 4, 8);   /* offset to elements */

    /* Names placed after the two 24-byte elements: body+8 + 2*24 = body+56. */
    names = 8 + 2 * 24;

    /* element 0: POSITION, index 0, float, register 0, mask 0x0F */
    w32(b, at + 8 + 0, (uint32_t)names);        /* name offset */
    w32(b, at + 8 + 4, 0);                       /* semantic index */
    w32(b, at + 8 + 8, 0);                       /* system value */
    w32(b, at + 8 + 12, PCPS4_DXBC_COMPONENT_FLOAT32);
    w32(b, at + 8 + 16, 0);                      /* register */
    b[at + 8 + 20] = 0x0F;                       /* mask xyzw */
    b[at + 8 + 21] = 0x0F;                       /* rw mask */

    /* element 1: TEXCOORD, index 0, float, register 1, mask 0x03 */
    w32(b, at + 32 + 0, (uint32_t)(names + 9));  /* name offset ("POSITION\0"=9) */
    w32(b, at + 32 + 4, 0);
    w32(b, at + 32 + 8, 0);
    w32(b, at + 32 + 12, PCPS4_DXBC_COMPONENT_FLOAT32);
    w32(b, at + 32 + 16, 1);
    b[at + 32 + 20] = 0x03;
    b[at + 32 + 21] = 0x03;

    memcpy(b + at + names, "POSITION", 8);
    b[at + names + 8] = '\0';
    memcpy(b + at + names + 9, "TEXCOORD", 8);
    b[at + names + 9 + 8] = '\0';

    return (uint32_t)(names + 9 + 9); /* body size */
}

static size_t g_total;

static void build_dxbc(uint8_t *b)
{
    size_t isgn_off, shex_off;
    uint32_t isgn_body_size, shex_body_size;
    uint32_t version_token;

    memset(b, 0, DXBC_SIZE);
    memcpy(b, "DXBC", 4);

    /* Two chunks; offset table at byte 32. */
    isgn_off = 32 + 2 * 4;

    /* ISGN chunk. */
    memcpy(b + isgn_off, "ISGN", 4);
    isgn_body_size = build_isgn_body(b, isgn_off + 8);
    w32(b, isgn_off + 4, isgn_body_size);

    shex_off = isgn_off + 8 + isgn_body_size;
    /* SHEX chunk: version token + length dword. */
    memcpy(b + shex_off, "SHEX", 4);
    /* program type 1 (vertex) in high 16 bits, major 5, minor 0. */
    version_token = (1u << 16) | (5u << 4) | 0u;
    w32(b, shex_off + 8 + 0, version_token);
    w32(b, shex_off + 8 + 4, 2); /* length in dwords */
    shex_body_size = 8;
    w32(b, shex_off + 4, shex_body_size);

    g_total = shex_off + 8 + shex_body_size;

    /* Offset table. */
    w32(b, 32 + 0, (uint32_t)isgn_off);
    w32(b, 32 + 4, (uint32_t)shex_off);

    /* Header totals. */
    w32(b, 24, (uint32_t)g_total);
    w32(b, 28, 2);
}

static void test_inspect(void)
{
    uint8_t b[DXBC_SIZE];
    pcps4_dxbc_info_t info;

    build_dxbc(b);

    CHECK(pcps4_dxbc_inspect(b, g_total, &info) == PCPS4_DXBC_OK);
    CHECK(info.chunk_count == 2);
    CHECK(info.shader_chunk_count == 1);
    CHECK(info.input_signature_count == 1);
    CHECK(info.program_type == PCPS4_DXBC_PROGRAM_VERTEX);
    CHECK(info.sm_major == 5);
    CHECK(info.sm_minor == 0);
}

static void test_signature(void)
{
    uint8_t b[DXBC_SIZE];
    pcps4_dxbc_signature_t sig;

    build_dxbc(b);

    CHECK(pcps4_dxbc_extract_signature(b, g_total, 0, &sig) == PCPS4_DXBC_OK);
    CHECK(sig.count == 2);

    CHECK(strcmp(sig.elements[0].semantic_name, "POSITION") == 0);
    CHECK(sig.elements[0].reg == 0);
    CHECK(sig.elements[0].component_type == PCPS4_DXBC_COMPONENT_FLOAT32);
    CHECK(sig.elements[0].mask == 0x0F);

    CHECK(strcmp(sig.elements[1].semantic_name, "TEXCOORD") == 0);
    CHECK(sig.elements[1].reg == 1);
    CHECK(sig.elements[1].mask == 0x03);

    /* No output signature present -> reported, not crashed. */
    CHECK(pcps4_dxbc_extract_signature(b, g_total, 1, &sig)
          == PCPS4_DXBC_ERR_NO_SIGNATURE);
}

int main(void)
{
    test_inspect();
    test_signature();

    if (g_failures == 0) {
        printf("dxbc_host_test: all checks passed\n");
        return 0;
    }
    printf("dxbc_host_test: %d check(s) failed\n", g_failures);
    return 1;
}
