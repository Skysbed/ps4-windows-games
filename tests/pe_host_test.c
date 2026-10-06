/*
 * Host-side unit test for the PcPs4 PE loader.
 *
 * Builds a minimal but spec-valid PE32+ (x86-64) image in memory, with one
 * ".idata" section importing two symbols from KERNEL32.dll (one by name, one
 * by ordinal), then checks that pcps4_pe_parse / section / RVA / import
 * walking all read it back correctly. Also checks that malformed inputs are
 * rejected rather than crashing.
 *
 * This compiles with the host compiler (not the PS4 toolchain) so CI can
 * actually execute it. The parser source itself is the same file that gets
 * compiled into the PS4 build.
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

#define IMG_SIZE 0x400
#define PE_OFF   0x80
#define OPT_OFF  (PE_OFF + 4 + 20)   /* after sig + COFF header */
#define OPT_SIZE 0xF0                 /* PE32+ opt header incl. 16 dirs */
#define SEC_OFF  (OPT_OFF + OPT_SIZE)
#define SEC_VA   0x1000u
#define SEC_RAW  0x200u

/* RVAs laid out inside the section. */
#define DESC_RVA   0x1000u           /* import descriptors */
#define ILT_RVA    0x1028u           /* import lookup table */
#define HINT_RVA   0x1040u           /* hint/name for GetProcAddress */
#define DLLNAME_RVA 0x1054u          /* "KERNEL32.dll" */

static size_t rva_to_file(uint32_t rva)
{
    return SEC_RAW + (rva - SEC_VA);
}

static void build_pe(uint8_t *b)
{
    memset(b, 0, IMG_SIZE);

    /* DOS header. */
    b[0] = 'M'; b[1] = 'Z';
    put32(b, 0x3C, PE_OFF);

    /* NT signature. */
    b[PE_OFF + 0] = 'P'; b[PE_OFF + 1] = 'E';
    b[PE_OFF + 2] = 0;   b[PE_OFF + 3] = 0;

    /* COFF file header. */
    put16(b, PE_OFF + 4 + 0, PCPS4_PE_MACHINE_AMD64); /* machine */
    put16(b, PE_OFF + 4 + 2, 1);                       /* num sections */
    put16(b, PE_OFF + 4 + 16, OPT_SIZE);               /* opt header size */
    put16(b, PE_OFF + 4 + 18, 0x0022);                 /* characteristics */

    /* Optional header (PE32+). */
    put16(b, OPT_OFF + 0, PCPS4_PE_MAGIC_PE32PLUS);
    put32(b, OPT_OFF + 16, SEC_VA);                    /* entry point RVA */
    put64(b, OPT_OFF + 24, 0x140000000ull);            /* image base */
    put32(b, OPT_OFF + 56, 0x2000);                    /* size of image */
    put32(b, OPT_OFF + 108, PCPS4_PE_NUM_DATA_DIRECTORIES);

    /* Data directory #1 = import. */
    put32(b, OPT_OFF + 112 + 1 * 8 + 0, DESC_RVA);
    put32(b, OPT_OFF + 112 + 1 * 8 + 4, 40);

    /* Section header ".idata". */
    memcpy(b + SEC_OFF, ".idata", 6);
    put32(b, SEC_OFF + 8, 0x200);                      /* virtual size */
    put32(b, SEC_OFF + 12, SEC_VA);                    /* virtual address */
    put32(b, SEC_OFF + 16, 0x200);                     /* raw size */
    put32(b, SEC_OFF + 20, SEC_RAW);                   /* raw offset */
    put32(b, SEC_OFF + 36,
          PCPS4_PE_SCN_MEM_READ | 0x40);               /* read + init data */

    /* Import descriptor [0]. */
    put32(b, rva_to_file(DESC_RVA) + 0, ILT_RVA);      /* OriginalFirstThunk */
    put32(b, rva_to_file(DESC_RVA) + 12, DLLNAME_RVA); /* Name */
    put32(b, rva_to_file(DESC_RVA) + 16, ILT_RVA);     /* FirstThunk */
    /* Descriptor [1] left zeroed = terminator. */

    /* Import lookup table: by-name, by-ordinal, terminator. */
    put64(b, rva_to_file(ILT_RVA) + 0, HINT_RVA);      /* by name (high bit 0) */
    put64(b, rva_to_file(ILT_RVA) + 8,
          0x8000000000000000ull | 7u);                 /* by ordinal 7 */
    put64(b, rva_to_file(ILT_RVA) + 16, 0);            /* terminator */

    /* Hint/name entry: 2-byte hint then the name. */
    put16(b, rva_to_file(HINT_RVA) + 0, 0);
    memcpy(b + rva_to_file(HINT_RVA) + 2, "GetProcAddress", 15);

    /* DLL name. */
    memcpy(b + rva_to_file(DLLNAME_RVA), "KERNEL32.dll", 13);
}

/* Import-collection state for the callback. */
typedef struct {
    int count;
    int saw_getprocaddress;
    int saw_ordinal7;
    char last_dll[32];
} collect_t;

static int collect_cb(void *user, const char *dll, int by_name,
                      const char *name, uint16_t ordinal)
{
    collect_t *c = (collect_t *)user;
    ++c->count;
    strncpy(c->last_dll, dll, sizeof(c->last_dll) - 1);
    if (by_name && name && strcmp(name, "GetProcAddress") == 0)
        c->saw_getprocaddress = 1;
    if (!by_name && ordinal == 7)
        c->saw_ordinal7 = 1;
    return 0;
}

static void test_valid_image(void)
{
    uint8_t buf[IMG_SIZE];
    pcps4_pe_image img;
    pcps4_pe_section sec;
    uint32_t off = 0;
    collect_t c;

    build_pe(buf);

    CHECK(pcps4_pe_parse(buf, sizeof(buf), &img) == PCPS4_PE_OK);
    CHECK(img.machine == PCPS4_PE_MACHINE_AMD64);
    CHECK(img.section_count == 1);
    CHECK(img.image_base == 0x140000000ull);
    CHECK(img.entry_point_rva == SEC_VA);

    CHECK(pcps4_pe_get_section(&img, 0, &sec) == PCPS4_PE_OK);
    CHECK(strcmp(sec.name, ".idata") == 0);
    CHECK(sec.virtual_address == SEC_VA);
    CHECK(sec.raw_offset == SEC_RAW);

    CHECK(pcps4_pe_rva_to_offset(&img, SEC_VA, &off) == PCPS4_PE_OK);
    CHECK(off == SEC_RAW);
    CHECK(pcps4_pe_rva_to_offset(&img, DLLNAME_RVA, &off) == PCPS4_PE_OK);
    CHECK(off == rva_to_file(DLLNAME_RVA));

    memset(&c, 0, sizeof(c));
    CHECK(pcps4_pe_iterate_imports(&img, collect_cb, &c) == PCPS4_PE_OK);
    CHECK(c.count == 2);
    CHECK(c.saw_getprocaddress == 1);
    CHECK(c.saw_ordinal7 == 1);
    CHECK(strcmp(c.last_dll, "KERNEL32.dll") == 0);
}

static void test_rejections(void)
{
    uint8_t buf[IMG_SIZE];
    pcps4_pe_image img;

    /* Null arguments. */
    CHECK(pcps4_pe_parse(NULL, IMG_SIZE, &img) == PCPS4_PE_ERR_NULL);

    /* Too small. */
    CHECK(pcps4_pe_parse(buf, 8, &img) == PCPS4_PE_ERR_TOO_SMALL);

    /* Missing MZ. */
    build_pe(buf);
    buf[0] = 'X';
    CHECK(pcps4_pe_parse(buf, sizeof(buf), &img) == PCPS4_PE_ERR_NOT_MZ);

    /* Missing PE signature. */
    build_pe(buf);
    buf[PE_OFF] = 'Q';
    CHECK(pcps4_pe_parse(buf, sizeof(buf), &img) == PCPS4_PE_ERR_NOT_PE);

    /* 32-bit optional header magic (PE32, not PE32+). */
    build_pe(buf);
    put16(buf, OPT_OFF + 0, 0x10b);
    CHECK(pcps4_pe_parse(buf, sizeof(buf), &img) == PCPS4_PE_ERR_NOT_PE32PLUS);

    /* Wrong machine. */
    build_pe(buf);
    put16(buf, PE_OFF + 4 + 0, 0x014c); /* i386 */
    CHECK(pcps4_pe_parse(buf, sizeof(buf), &img) == PCPS4_PE_ERR_BAD_MACHINE);
}

int main(void)
{
    test_valid_image();
    test_rejections();

    if (g_failures == 0) {
        printf("pe_host_test: all checks passed\n");
        return 0;
    }
    printf("pe_host_test: %d check(s) failed\n", g_failures);
    return 1;
}
