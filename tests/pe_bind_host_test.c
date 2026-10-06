/*
 * Host-side unit test for the PcPs4 import resolver and bind pass.
 *
 * Builds a PE32+ that imports two symbols from KERNEL32.dll (GetProcAddress by
 * name, and one by ordinal), maps it, registers only GetProcAddress in the
 * resolver, then binds. Checks that:
 *   - the resolved IAT slot now holds the registered handler pointer,
 *   - the ordinal (unregistered) slot is left NULL,
 *   - counts are resolved=1, unresolved=1.
 * Also exercises the resolver's register/replace/lookup directly.
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

#define SIZE_OF_IMAGE   0x2000u
#define SIZE_OF_HEADERS 0x200u
#define SEC_VA   0x1000u
#define SEC_RAW  0x200u

/* Distinct ILT and IAT arrays, as a real linker emits. */
#define DESC_RVA    0x1000u
#define ILT_RVA     0x1028u   /* name table */
#define IAT_RVA     0x1040u   /* bound at runtime */
#define HINT_RVA    0x1058u
#define DLLNAME_RVA 0x1070u

static size_t rva_to_file(uint32_t rva) { return SEC_RAW + (rva - SEC_VA); }

static void build_pe(uint8_t *b)
{
    memset(b, 0, IMG_SIZE);

    b[0] = 'M'; b[1] = 'Z';
    put32(b, 0x3C, PE_OFF);
    b[PE_OFF + 0] = 'P'; b[PE_OFF + 1] = 'E';

    put16(b, PE_OFF + 4 + 0, PCPS4_PE_MACHINE_AMD64);
    put16(b, PE_OFF + 4 + 2, 1);
    put16(b, PE_OFF + 4 + 16, OPT_SIZE);

    put16(b, OPT_OFF + 0, PCPS4_PE_MAGIC_PE32PLUS);
    put32(b, OPT_OFF + 16, SEC_VA);
    put64(b, OPT_OFF + 24, 0x140000000ull);
    put32(b, OPT_OFF + 56, SIZE_OF_IMAGE);
    put32(b, OPT_OFF + 60, SIZE_OF_HEADERS);
    put32(b, OPT_OFF + 108, PCPS4_PE_NUM_DATA_DIRECTORIES);
    put32(b, OPT_OFF + 112 + 1 * 8 + 0, DESC_RVA);
    put32(b, OPT_OFF + 112 + 1 * 8 + 4, 40);

    memcpy(b + SEC_OFF, ".idata", 6);
    put32(b, SEC_OFF + 8, 0x200);
    put32(b, SEC_OFF + 12, SEC_VA);
    put32(b, SEC_OFF + 16, 0x200);
    put32(b, SEC_OFF + 20, SEC_RAW);
    put32(b, SEC_OFF + 36, PCPS4_PE_SCN_MEM_READ | 0x40);

    /* Import descriptor. */
    put32(b, rva_to_file(DESC_RVA) + 0, ILT_RVA);
    put32(b, rva_to_file(DESC_RVA) + 12, DLLNAME_RVA);
    put32(b, rva_to_file(DESC_RVA) + 16, IAT_RVA);

    /* ILT: by-name, by-ordinal, terminator. */
    put64(b, rva_to_file(ILT_RVA) + 0, HINT_RVA);
    put64(b, rva_to_file(ILT_RVA) + 8, 0x8000000000000000ull | 7u);
    put64(b, rva_to_file(ILT_RVA) + 16, 0);

    /* IAT: starts as a copy of the ILT (pre-binding). */
    put64(b, rva_to_file(IAT_RVA) + 0, HINT_RVA);
    put64(b, rva_to_file(IAT_RVA) + 8, 0x8000000000000000ull | 7u);
    put64(b, rva_to_file(IAT_RVA) + 16, 0);

    put16(b, rva_to_file(HINT_RVA) + 0, 0);
    memcpy(b + rva_to_file(HINT_RVA) + 2, "GetProcAddress", 15);
    memcpy(b + rva_to_file(DLLNAME_RVA), "KERNEL32.dll", 13);
}

/* A stand-in handler; we only compare its address. */
static int fake_getprocaddress(void) { return 42; }

static void test_resolver_api(void)
{
    pcps4_pe_resolver r;
    void *p = (void *)fake_getprocaddress;

    pcps4_pe_resolver_init(&r);
    CHECK(r.count == 0);
    CHECK(pcps4_pe_resolver_lookup(&r, "KERNEL32.dll", "GetProcAddress")
          == NULL);

    CHECK(pcps4_pe_resolver_register(&r, "KERNEL32.dll", "GetProcAddress", p)
          == PCPS4_PE_OK);
    CHECK(r.count == 1);
    /* DLL match is case-insensitive. */
    CHECK(pcps4_pe_resolver_lookup(&r, "kernel32.DLL", "GetProcAddress") == p);
    /* Function name match is case-sensitive. */
    CHECK(pcps4_pe_resolver_lookup(&r, "KERNEL32.dll", "getprocaddress")
          == NULL);

    /* Re-registering replaces, does not grow. */
    CHECK(pcps4_pe_resolver_register(&r, "KERNEL32.dll", "GetProcAddress",
                                     NULL) == PCPS4_PE_OK);
    CHECK(r.count == 1);
    CHECK(pcps4_pe_resolver_lookup(&r, "KERNEL32.dll", "GetProcAddress")
          == NULL);
}

static void test_bind(void)
{
    uint8_t buf[IMG_SIZE];
    uint8_t dest[SIZE_OF_IMAGE];
    pcps4_pe_image img;
    pcps4_pe_resolver r;
    uint32_t resolved = 99, unresolved = 99;
    void *p = (void *)fake_getprocaddress;

    build_pe(buf);
    CHECK(pcps4_pe_parse(buf, sizeof(buf), &img) == PCPS4_PE_OK);
    CHECK(pcps4_pe_map_image(&img, dest, sizeof(dest), 0x140000000ull)
          == PCPS4_PE_OK);

    pcps4_pe_resolver_init(&r);
    CHECK(pcps4_pe_resolver_register(&r, "KERNEL32.dll", "GetProcAddress", p)
          == PCPS4_PE_OK);

    CHECK(pcps4_pe_bind_imports(&img, dest, sizeof(dest), &r,
                                &resolved, &unresolved) == PCPS4_PE_OK);
    CHECK(resolved == 1);
    CHECK(unresolved == 1);

    /* The by-name slot now holds our handler. */
    CHECK(get64(dest, IAT_RVA + 0) == (uint64_t)(uintptr_t)p);
    /* The by-ordinal slot is left NULL (no name-based handler). */
    CHECK(get64(dest, IAT_RVA + 8) == 0);
}

static void test_load_unified(void)
{
    uint8_t buf[IMG_SIZE];
    uint8_t dest[SIZE_OF_IMAGE];
    pcps4_pe_image img;
    pcps4_pe_resolver r;
    uint32_t resolved = 0, unresolved = 0;
    void *p = (void *)fake_getprocaddress;

    build_pe(buf);
    pcps4_pe_resolver_init(&r);
    CHECK(pcps4_pe_resolver_register(&r, "KERNEL32.dll", "GetProcAddress", p)
          == PCPS4_PE_OK);

    /* One call does parse + map + bind. */
    CHECK(pcps4_pe_load(buf, sizeof(buf), dest, sizeof(dest),
                        0x140000000ull, &r, &img,
                        &resolved, &unresolved) == PCPS4_PE_OK);
    CHECK(resolved == 1);
    CHECK(unresolved == 1);
    CHECK(get64(dest, IAT_RVA + 0) == (uint64_t)(uintptr_t)p);

    /* Resolver NULL skips binding but still maps. */
    memset(dest, 0, sizeof(dest));
    CHECK(pcps4_pe_load(buf, sizeof(buf), dest, sizeof(dest),
                        0x140000000ull, NULL, &img, NULL, NULL)
          == PCPS4_PE_OK);
    CHECK(dest[0] == 'M' && dest[1] == 'Z');
}

int main(void)
{
    test_resolver_api();
    test_bind();
    test_load_unified();

    if (g_failures == 0) {
        printf("pe_bind_host_test: all checks passed\n");
        return 0;
    }
    printf("pe_bind_host_test: %d check(s) failed\n", g_failures);
    return 1;
}
