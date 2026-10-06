/*
 * Host-side unit test for the PcPs4 PE image mapper.
 *
 * Builds a PE32+ with a ".text" section holding a 64-bit self-pointer
 * (image_base + 0x1000) and a ".reloc" section with one DIR64 fixup for it,
 * then maps the image at a DIFFERENT load base and checks that:
 *   - headers and section bytes land at the right virtual addresses, and
 *   - the self-pointer was rebased by exactly (load_base - image_base).
 * Also checks that a too-small destination is rejected.
 */

#include "../compat/pcps4_pe.h"

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

static void put16(uint8_t *b, size_t off, uint16_t v)
{
    b[off + 0] = (uint8_t)(v & 0xFF);
    b[off + 1] = (uint8_t)((v >> 8) & 0xFF);
}
static void put32(uint8_t *b, size_t off, uint32_t v)
{
    b[off + 0] = (uint8_t)(v & 0xFF);
    b[off + 1] = (uint8_t)((v >> 8) & 0xFF);
    b[off + 2] = (uint8_t)((v >> 16) & 0xFF);
    b[off + 3] = (uint8_t)((v >> 24) & 0xFF);
}
static void put64(uint8_t *b, size_t off, uint64_t v)
{
    put32(b, off, (uint32_t)(v & 0xFFFFFFFFu));
    put32(b, off + 4, (uint32_t)(v >> 32));
}
static uint64_t get64(const uint8_t *b, size_t off)
{
    uint64_t v = 0;
    int i;
    for (i = 7; i >= 0; --i)
        v = (v << 8) | b[off + (size_t)i];
    return v;
}

#define IMG_SIZE 0x600
#define PE_OFF   0x80
#define OPT_OFF  (PE_OFF + 4 + 20)
#define OPT_SIZE 0xF0
#define SEC_OFF  (OPT_OFF + OPT_SIZE)

#define IMAGE_BASE 0x140000000ull
#define LOAD_BASE  0x200000000ull
#define SIZE_OF_IMAGE   0x3000u
#define SIZE_OF_HEADERS 0x200u

/* .text: RVA 0x1000, raw at 0x200. .reloc: RVA 0x2000, raw at 0x400. */
#define TEXT_VA   0x1000u
#define TEXT_RAW  0x200u
#define RELOC_VA  0x2000u
#define RELOC_RAW 0x400u

static void build_pe(uint8_t *b)
{
    size_t reloc_block;

    memset(b, 0, IMG_SIZE);

    b[0] = 'M'; b[1] = 'Z';
    put32(b, 0x3C, PE_OFF);

    b[PE_OFF + 0] = 'P'; b[PE_OFF + 1] = 'E';

    put16(b, PE_OFF + 4 + 0, PCPS4_PE_MACHINE_AMD64);
    put16(b, PE_OFF + 4 + 2, 2);         /* two sections */
    put16(b, PE_OFF + 4 + 16, OPT_SIZE);

    put16(b, OPT_OFF + 0, PCPS4_PE_MAGIC_PE32PLUS);
    put32(b, OPT_OFF + 16, TEXT_VA);     /* entry point */
    put64(b, OPT_OFF + 24, IMAGE_BASE);
    put32(b, OPT_OFF + 56, SIZE_OF_IMAGE);
    put32(b, OPT_OFF + 60, SIZE_OF_HEADERS);
    put32(b, OPT_OFF + 108, PCPS4_PE_NUM_DATA_DIRECTORIES);

    /* Data directory #5 = base relocations. One block = 8 + 2 bytes = 10,
     * padded to 12 with an ABSOLUTE entry for alignment. */
    put32(b, OPT_OFF + 112 + 5 * 8 + 0, RELOC_VA);
    put32(b, OPT_OFF + 112 + 5 * 8 + 4, 12);

    /* Section header 0: .text */
    memcpy(b + SEC_OFF, ".text", 5);
    put32(b, SEC_OFF + 8, 0x1000);
    put32(b, SEC_OFF + 12, TEXT_VA);
    put32(b, SEC_OFF + 16, 0x200);
    put32(b, SEC_OFF + 20, TEXT_RAW);
    put32(b, SEC_OFF + 36,
          PCPS4_PE_SCN_MEM_READ | PCPS4_PE_SCN_MEM_EXECUTE);

    /* Section header 1: .reloc */
    memcpy(b + SEC_OFF + 40, ".reloc", 6);
    put32(b, SEC_OFF + 40 + 8, 0x1000);
    put32(b, SEC_OFF + 40 + 12, RELOC_VA);
    put32(b, SEC_OFF + 40 + 16, 0x200);
    put32(b, SEC_OFF + 40 + 20, RELOC_RAW);
    put32(b, SEC_OFF + 40 + 36, PCPS4_PE_SCN_MEM_READ);

    /* .text content: a self-pointer at offset 0 = IMAGE_BASE + TEXT_VA. */
    put64(b, TEXT_RAW + 0, IMAGE_BASE + TEXT_VA);
    /* A marker byte further in, to confirm section placement. */
    b[TEXT_RAW + 0x10] = 0xAB;

    /* .reloc block: PageRVA = TEXT_VA, BlockSize = 12, one DIR64 entry at
     * offset 0, then one ABSOLUTE padding entry. */
    reloc_block = RELOC_RAW;
    put32(b, reloc_block + 0, TEXT_VA);
    put32(b, reloc_block + 4, 12);
    put16(b, reloc_block + 8, (uint16_t)((10u << 12) | 0x000u)); /* DIR64 @0 */
    put16(b, reloc_block + 10, (uint16_t)((0u << 12) | 0x000u)); /* ABSOLUTE */
}

static void test_map(void)
{
    uint8_t buf[IMG_SIZE];
    uint8_t dest[SIZE_OF_IMAGE];
    pcps4_pe_image img;

    build_pe(buf);

    CHECK(pcps4_pe_parse(buf, sizeof(buf), &img) == PCPS4_PE_OK);
    CHECK(img.size_of_headers == SIZE_OF_HEADERS);
    CHECK(img.section_count == 2);

    CHECK(pcps4_pe_map_image(&img, dest, sizeof(dest), LOAD_BASE)
          == PCPS4_PE_OK);

    /* Headers copied. */
    CHECK(dest[0] == 'M' && dest[1] == 'Z');
    /* .text placed at its RVA, marker intact. */
    CHECK(dest[TEXT_VA + 0x10] == 0xAB);
    /* Self-pointer rebased: image_base+0x1000 -> load_base+0x1000. */
    CHECK(get64(dest, TEXT_VA) == LOAD_BASE + TEXT_VA);

    /* Mapping at the preferred base needs no fixups and still works. */
    memset(dest, 0, sizeof(dest));
    CHECK(pcps4_pe_map_image(&img, dest, sizeof(dest), IMAGE_BASE)
          == PCPS4_PE_OK);
    CHECK(get64(dest, TEXT_VA) == IMAGE_BASE + TEXT_VA);
}

static void test_no_space(void)
{
    uint8_t buf[IMG_SIZE];
    uint8_t small[0x100];
    pcps4_pe_image img;

    build_pe(buf);
    CHECK(pcps4_pe_parse(buf, sizeof(buf), &img) == PCPS4_PE_OK);
    CHECK(pcps4_pe_map_image(&img, small, sizeof(small), LOAD_BASE)
          == PCPS4_PE_ERR_NO_SPACE);
}

int main(void)
{
    test_map();
    test_no_space();

    if (g_failures == 0) {
        printf("pe_map_host_test: all checks passed\n");
        return 0;
    }
    printf("pe_map_host_test: %d check(s) failed\n", g_failures);
    return 1;
}
