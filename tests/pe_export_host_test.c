/*
 * Host-side export-table test.
 *
 * The PE export directory is defined by the Windows PE format. This test
 * builds a minimal PE32+ image with one named export and checks lookup by
 * both name and ordinal after mapping the image.
 */

#include "../compat/pcps4_pe.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(x) do {     if (!(x)) {         printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);         ++failures;     } } while (0)

static void put16(uint8_t *b, size_t o, uint16_t v)
{
    b[o] = (uint8_t)v;
    b[o + 1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *b, size_t o, uint32_t v)
{
    b[o] = (uint8_t)v;
    b[o + 1] = (uint8_t)(v >> 8);
    b[o + 2] = (uint8_t)(v >> 16);
    b[o + 3] = (uint8_t)(v >> 24);
}

static void put64(uint8_t *b, size_t o, uint64_t v)
{
    put32(b, o, (uint32_t)v);
    put32(b, o + 4, (uint32_t)(v >> 32));
}

#define IMG_SIZE 0x800
#define PE_OFF 0x80
#define OPT_OFF (PE_OFF + 4 + 20)
#define OPT_SIZE 0xF0
#define SEC_OFF (OPT_OFF + OPT_SIZE)
#define SEC_RAW 0x200u
#define SEC_VA  0x1000u

#define EAT_RVA       0x1040u
#define NAMES_RVA     0x1044u
#define ORDS_RVA      0x1048u
#define DLLNAME_RVA   0x104Au
#define FUNCNAME_RVA  0x1054u
#define FUNC_RVA      0x1180u
#define EXPORT_RVA    0x1000u

static size_t raw(uint32_t rva)
{
    return SEC_RAW + (rva - SEC_VA);
}

static void build_pe(uint8_t *b)
{
    memset(b, 0, IMG_SIZE);

    b[0] = 'M';
    b[1] = 'Z';
    put32(b, 0x3C, PE_OFF);
    b[PE_OFF + 0] = 'P';
    b[PE_OFF + 1] = 'E';

    put16(b, PE_OFF + 4 + 0, PCPS4_PE_MACHINE_AMD64);
    put16(b, PE_OFF + 4 + 2, 1);
    put16(b, PE_OFF + 4 + 16, OPT_SIZE);

    put16(b, OPT_OFF + 0, PCPS4_PE_MAGIC_PE32PLUS);
    put32(b, OPT_OFF + 16, FUNC_RVA);
    put64(b, OPT_OFF + 24, 0x140000000ull);
    put32(b, OPT_OFF + 56, 0x3000u);
    put32(b, OPT_OFF + 60, 0x200u);
    put32(b, OPT_OFF + 108, PCPS4_PE_NUM_DATA_DIRECTORIES);

    put32(b, OPT_OFF + 112 + PCPS4_PE_DIRECTORY_EXPORT * 8,
          EXPORT_RVA);
    put32(b, OPT_OFF + 112 + PCPS4_PE_DIRECTORY_EXPORT * 8 + 4,
          0x80u);

    memcpy(b + SEC_OFF, ".edata", 6);
    put32(b, SEC_OFF + 8, 0x200u);
    put32(b, SEC_OFF + 12, SEC_VA);
    put32(b, SEC_OFF + 16, 0x200u);
    put32(b, SEC_OFF + 20, SEC_RAW);
    put32(b, SEC_OFF + 36,
          PCPS4_PE_SCN_MEM_READ | PCPS4_PE_SCN_MEM_EXECUTE);

    /* IMAGE_EXPORT_DIRECTORY. */
    put32(b, raw(EXPORT_RVA) + 12, DLLNAME_RVA);
    put32(b, raw(EXPORT_RVA) + 16, 7u); /* ordinal base */
    put32(b, raw(EXPORT_RVA) + 20, 1u); /* one function */
    put32(b, raw(EXPORT_RVA) + 24, 1u); /* one name */
    put32(b, raw(EXPORT_RVA) + 28, EAT_RVA);
    put32(b, raw(EXPORT_RVA) + 32, NAMES_RVA);
    put32(b, raw(EXPORT_RVA) + 36, ORDS_RVA);

    put32(b, raw(EAT_RVA), FUNC_RVA);
    put32(b, raw(NAMES_RVA), FUNCNAME_RVA);
    put16(b, raw(ORDS_RVA), 0u);

    memcpy(b + raw(DLLNAME_RVA), "testdll.dll", 12);
    memcpy(b + raw(FUNCNAME_RVA), "GameStep", 9);

    /* mov eax, 0x2A ; ret */
    b[raw(FUNC_RVA) + 0] = 0xB8;
    b[raw(FUNC_RVA) + 1] = 0x2A;
    b[raw(FUNC_RVA) + 2] = 0;
    b[raw(FUNC_RVA) + 3] = 0;
    b[raw(FUNC_RVA) + 4] = 0;
    b[raw(FUNC_RVA) + 5] = 0xC3;
}

int main(void)
{
    uint8_t source[IMG_SIZE];
    uint8_t mapped[0x3000];
    pcps4_pe_image image;
    void *by_name;
    void *by_ordinal;

    build_pe(source);

    CHECK(pcps4_pe_parse(source, sizeof(source), &image) == PCPS4_PE_OK);
    CHECK(pcps4_pe_map_image(
        &image, mapped, sizeof(mapped), 0x200000000ull) == PCPS4_PE_OK);

    by_name = pcps4_pe_lookup_export(
        &image, mapped, sizeof(mapped), "GameStep", 0);
    by_ordinal = pcps4_pe_lookup_export(
        &image, mapped, sizeof(mapped), NULL, 7);

    CHECK(by_name == (void *)(mapped + FUNC_RVA));
    CHECK(by_ordinal == (void *)(mapped + FUNC_RVA));
    CHECK(pcps4_pe_lookup_export(
              &image, mapped, sizeof(mapped), "Missing", 0) == NULL);
    CHECK(pcps4_pe_lookup_export(
              &image, mapped, sizeof(mapped), NULL, 8) == NULL);

    if (failures == 0) {
        puts("pe_export_host_test: all checks passed");
        return 0;
    }

    printf("pe_export_host_test: %d check(s) failed\n", failures);
    return 1;
}
