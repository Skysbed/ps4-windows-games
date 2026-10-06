/*
 * PcPs4 PE executor (PS4 target). See pcps4_loader.h.
 *
 * Flow:
 *   1. allocate RW memory sized for the image (sceKernelMmap, anonymous);
 *   2. parse + map + bind the PE into it (pcps4_pe_load), using the real
 *      allocation address as the load base so relocations are correct;
 *   3. refuse if any import is unresolved;
 *   4. add execute permission (sceKernelMprotect);
 *   5. call the entry point.
 *
 * Every kernel return code is checked; control is only transferred once all
 * steps have succeeded.
 */

#include "pcps4_loader.h"
#include "pcps4_status.h"
#include "pcps4_fault.h"  /* pcps4_fault_stage: crash breadcrumb */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mman.h>

/* OpenOrbis kernel memory APIs (declared with concrete signatures here;
 * libkernel exports the symbols). */
extern int32_t sceKernelMmap(void *addr, size_t len, int32_t prot,
                             int32_t flags, int32_t fd, off_t offset,
                             void **res);
extern int32_t sceKernelMprotect(const void *addr, size_t len, int prot);
extern int32_t sceKernelMunmap(void *addr, size_t len);

#if defined(__x86_64__) && (defined(__clang__) || defined(__GNUC__))
#define PCPS4_MS_ABI __attribute__((ms_abi))
#else
#define PCPS4_MS_ABI
#endif

typedef int (PCPS4_MS_ABI *pcps4_entry_fn)(void);

#define PCPS4_PAGE_SIZE 0x4000u

/* Round a size up to the 16 KiB PS4 page granule. */
static size_t pcps4_round_up_page(size_t n)
{
    const size_t page = 0x4000;
    size_t r = n + (page - 1);
    return r - (r % page);
}

static int pcps4_apply_image_protections(
    const pcps4_pe_image *image,
    uint8_t *mem,
    size_t map_size)
{
    size_t page_count;
    uint8_t *page_prot;
    uint16_t i;

    if (image == NULL || mem == NULL || map_size == 0 ||
        (map_size % PCPS4_PAGE_SIZE) != 0)
        return -1;

    page_count = map_size / PCPS4_PAGE_SIZE;
    page_prot = (uint8_t *)calloc(page_count, sizeof(*page_prot));
    if (page_prot == NULL)
        return -1;

    /* Headers are readable; section permissions may add execute on the first
     * page when a PE uses the usual 4 KiB section alignment. */
    {
        size_t header_end = pcps4_round_up_page(image->size_of_headers);
        if (header_end == 0 || header_end > map_size)
            header_end = PCPS4_PAGE_SIZE;
        if (header_end > map_size)
            header_end = map_size;
        for (size_t p = 0; p < header_end / PCPS4_PAGE_SIZE; ++p)
            page_prot[p] |= PROT_READ;
    }

    for (i = 0; i < image->section_count; ++i) {
        pcps4_pe_section s;
        size_t section_start;
        size_t span;
        size_t section_end;
        size_t first_page;
        size_t last_page;
        int prot = 0;

        if (pcps4_pe_get_section(image, i, &s) != PCPS4_PE_OK) {
            free(page_prot);
            return -1;
        }

        section_start = (size_t)s.virtual_address;
        span = (size_t)(s.virtual_size > s.raw_size
                    ? s.virtual_size : s.raw_size);
        if (span == 0)
            continue;

        if (section_start >= map_size ||
            span > map_size - section_start) {
            free(page_prot);
            return -1;
        }

        section_end = section_start + span;
        if ((s.characteristics & PCPS4_PE_SCN_MEM_READ) != 0)
            prot |= PROT_READ;
        if ((s.characteristics & PCPS4_PE_SCN_MEM_WRITE) != 0)
            prot |= PROT_WRITE;
        if ((s.characteristics & PCPS4_PE_SCN_MEM_EXECUTE) != 0)
            prot |= PROT_EXEC;

        first_page = section_start / PCPS4_PAGE_SIZE;
        last_page = (section_end + PCPS4_PAGE_SIZE - 1) /
                    PCPS4_PAGE_SIZE;

        if (last_page > page_count) {
            free(page_prot);
            return -1;
        }

        for (size_t p = first_page; p < last_page; ++p)
            page_prot[p] |= (uint8_t)prot;
    }

    if (sceKernelMprotect(mem, map_size, PROT_NONE) != 0) {
        free(page_prot);
        return -1;
    }

    size_t run_start = 0;
    while (run_start < page_count) {
        uint8_t prot = page_prot[run_start];
        size_t run_end = run_start + 1;

        while (run_end < page_count &&
               page_prot[run_end] == prot)
            ++run_end;

        if (sceKernelMprotect(
                mem + run_start * PCPS4_PAGE_SIZE,
                (run_end - run_start) * PCPS4_PAGE_SIZE,
                prot) != 0) {
            free(page_prot);
            return -1;
        }

        run_start = run_end;
    }

    free(page_prot);
    return 0;
}

static int pcps4_report_unresolved(
    void *user,
    const char *dll,
    int by_name,
    const char *name,
    uint16_t ordinal)
{
    (void)user;

    if (by_name)
        pcps4_status_log("[pcps4] unresolved import: %s!%s", dll, name);
    else
        pcps4_status_log("[pcps4] unresolved import: %s!#%u",
               dll, (unsigned)ordinal);

    return 0;
}

static int pcps4_run_tls_callbacks(
    const pcps4_pe_image *image,
    uint8_t *mem,
    size_t map_size)
{
    uint32_t tls_rva;

    typedef void (*pcps4_tls_callback_fn)(
        void *module,
        uint32_t reason,
        void *reserved);

    if (image == NULL || mem == NULL || map_size == 0)
        return -1;

    tls_rva = image->directory_rva[PCPS4_PE_DIRECTORY_TLS];
    if (tls_rva == 0 ||
        image->directory_size[PCPS4_PE_DIRECTORY_TLS] < 40u)
        return 0;

    if ((size_t)tls_rva + 40u > map_size)
        return -1;

    /*
     * PE32+ IMAGE_TLS_DIRECTORY64:
     *   0x00 StartAddressOfRawData
     *   0x08 EndAddressOfRawData
     *   0x10 AddressOfIndex
     *   0x18 AddressOfCallBacks
     *   0x20 SizeOfZeroFill
     *   0x24 Characteristics
     *
     * Relocations have already been applied to AddressOfCallBacks, so it is
     * an absolute runtime address inside this mapped image.
     */
    uint64_t callbacks_va =
        (uint64_t)mem[tls_rva + 0x18u] |
        ((uint64_t)mem[tls_rva + 0x19u] << 8) |
        ((uint64_t)mem[tls_rva + 0x1Au] << 16) |
        ((uint64_t)mem[tls_rva + 0x1Bu] << 24) |
        ((uint64_t)mem[tls_rva + 0x1Cu] << 32) |
        ((uint64_t)mem[tls_rva + 0x1Du] << 40) |
        ((uint64_t)mem[tls_rva + 0x1Eu] << 48) |
        ((uint64_t)mem[tls_rva + 0x1Fu] << 56);

    if (callbacks_va == 0)
        return 0;

    if (callbacks_va < (uint64_t)(uintptr_t)mem ||
        callbacks_va >= (uint64_t)(uintptr_t)mem + map_size)
        return -1;

    size_t offset =
        (size_t)(callbacks_va - (uint64_t)(uintptr_t)mem);

    for (uint32_t i = 0; i < 64u; ++i) {
        if (offset > map_size - 8u)
            return -1;

        uint64_t callback_va =
            (uint64_t)mem[offset + 0] |
            ((uint64_t)mem[offset + 1] << 8) |
            ((uint64_t)mem[offset + 2] << 16) |
            ((uint64_t)mem[offset + 3] << 24) |
            ((uint64_t)mem[offset + 4] << 32) |
            ((uint64_t)mem[offset + 5] << 40) |
            ((uint64_t)mem[offset + 6] << 48) |
            ((uint64_t)mem[offset + 7] << 56);

        if (callback_va == 0)
            return 0;

        if (callback_va < (uint64_t)(uintptr_t)mem ||
            callback_va >= (uint64_t)(uintptr_t)mem + map_size)
            return -1;

        pcps4_tls_callback_fn callback =
            (pcps4_tls_callback_fn)(uintptr_t)callback_va;

        callback(mem, 1u, NULL);
        offset += 8u;
    }

    return -1; /* malformed/cyclic callback table */
}

static int pcps4_entry_is_executable(
    const pcps4_pe_image *image)
{
    uint16_t i;

    if (image == NULL)
        return 0;

    for (i = 0; i < image->section_count; ++i) {
        pcps4_pe_section s;
        uint32_t span;

        if (pcps4_pe_get_section(image, i, &s) != PCPS4_PE_OK)
            return 0;

        span = s.virtual_size > s.raw_size ? s.virtual_size : s.raw_size;
        if (image->entry_point_rva >= s.virtual_address &&
            image->entry_point_rva - s.virtual_address < span &&
            (s.characteristics & PCPS4_PE_SCN_MEM_EXECUTE) != 0)
            return 1;
    }

    return 0;
}

pcps4_loader_status pcps4_loader_run(const uint8_t *exe, size_t exe_size,
                                     const pcps4_pe_resolver *resolver,
                                     int32_t *out_entry_ret)
{
    pcps4_pe_image probe;
    pcps4_pe_image image;
    uint32_t resolved = 0, unresolved = 0;
    size_t map_size;
    void *mem = NULL;
    int32_t krc;
    pcps4_pe_status prc;
    pcps4_entry_fn entry;

    if (exe == NULL)
        return PCPS4_LOADER_ERR_NULL;

    /* Parse once up front just to learn how much memory to reserve. */
    prc = pcps4_pe_parse(exe, exe_size, &probe);
    if (prc != PCPS4_PE_OK) {
        pcps4_status_log("[pcps4] load parse failed: %s", pcps4_pe_status_str(prc));
        return PCPS4_LOADER_ERR_LOAD;
    }

    map_size = pcps4_round_up_page(probe.size_of_image);
    if (map_size == 0)
        return PCPS4_LOADER_ERR_LOAD;

    /* Allocate anonymous RW memory for the image. */
    krc = sceKernelMmap(NULL, map_size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON, -1, 0, &mem);
    if (krc != 0 || mem == NULL) {
        pcps4_status_log("[pcps4] mmap failed: 0x%x", (unsigned)krc);
        return PCPS4_LOADER_ERR_ALLOC;
    }

    /* Map + bind at the real runtime address so relocations resolve. */
    prc = pcps4_pe_load(exe, exe_size, (uint8_t *)mem, map_size,
                        (uint64_t)(uintptr_t)mem, resolver,
                        &image, &resolved, &unresolved);
    if (prc != PCPS4_PE_OK) {
        pcps4_status_log("[pcps4] load failed: %s", pcps4_pe_status_str(prc));
        sceKernelMunmap(mem, map_size);
        return PCPS4_LOADER_ERR_LOAD;
    }

    pcps4_status_log("[pcps4] imports resolved=%u unresolved=%u",
           (unsigned)resolved, (unsigned)unresolved);

    if (unresolved != 0) {
        (void)pcps4_pe_report_unresolved_imports(
            &image, resolver, pcps4_report_unresolved, NULL);

        /* Never jump into code that will call a missing API. */
        pcps4_status_log("[pcps4] refusing to run: %u unresolved import(s)",
               (unsigned)unresolved);
        sceKernelMunmap(mem, map_size);
        return PCPS4_LOADER_ERR_UNRESOLVED;
    }

        if (!pcps4_entry_is_executable(&image)) {
        pcps4_status_log("[pcps4] refusing to run: PE entry point is not in an "
               "executable section");
        sceKernelMunmap(mem, map_size);
        return PCPS4_LOADER_ERR_PROTECT;
    }

    /*
     * Final section permissions. Writable .data/.bss must remain writable
     * after relocation while .text becomes executable. PE section alignment
     * is often smaller than the PS4 page size, so permissions are calculated
     * per PS4 page and unioned when sections share a page.
     */
    pcps4_status_log("[pcps4] loader: applying section protections (image=%p size=0x%zx)", mem, map_size);
    if (pcps4_apply_image_protections(
            &image, (uint8_t *)mem, map_size) != 0) {
        pcps4_status_log("[pcps4] mprotect(section permissions) failed "
               "(console may not permit executable memory)");
        sceKernelMunmap(mem, map_size);
        return PCPS4_LOADER_ERR_PROTECT;
    }

    pcps4_status_log("[pcps4] loader: protections ok, running TLS callbacks");
    if (pcps4_run_tls_callbacks(
            &image, (uint8_t *)mem, map_size) != 0) {
        pcps4_status_log("[pcps4] PE TLS callback table is invalid");
        sceKernelMunmap(mem, map_size);
        return PCPS4_LOADER_ERR_LOAD;
    }

    entry = (pcps4_entry_fn)(uintptr_t)
            ((uint8_t *)mem + image.entry_point_rva);

    pcps4_status_log("[pcps4] entering binary at %p", (void *)entry);
    pcps4_fault_stage("loader: PE entry (before WinMainCRTStartup)");
    {
        int ret = entry();
        pcps4_fault_stage("loader: PE returned");
        if (out_entry_ret)
            *out_entry_ret = (int32_t)ret;
        pcps4_status_log("[pcps4] binary returned %d (0x%x)", ret, (unsigned)ret);
    }

    return PCPS4_LOADER_OK;
}

/* --- built-in hardware smoke test -------------------------------------- */

/* Little-endian writers for building the test image. */
static void w16(uint8_t *b, size_t o, uint16_t v)
{
    b[o] = (uint8_t)(v & 0xFF);
    b[o + 1] = (uint8_t)((v >> 8) & 0xFF);
}
static void w32(uint8_t *b, size_t o, uint32_t v)
{
    b[o] = (uint8_t)(v & 0xFF);
    b[o + 1] = (uint8_t)((v >> 8) & 0xFF);
    b[o + 2] = (uint8_t)((v >> 16) & 0xFF);
    b[o + 3] = (uint8_t)((v >> 24) & 0xFF);
}
static void w64(uint8_t *b, size_t o, uint64_t v)
{
    w32(b, o, (uint32_t)(v & 0xFFFFFFFFu));
    w32(b, o + 4, (uint32_t)(v >> 32));
}

int pcps4_pe_exec_smoke(void)
{
    enum {
        IMG = 0x400,
        PE_OFF = 0x80,
        OPT_OFF = PE_OFF + 4 + 20,
        OPT_SIZE = 0xF0,
        SEC_OFF = OPT_OFF + OPT_SIZE,
        TEXT_VA = 0x1000,
        TEXT_RAW = 0x200
    };
    uint8_t exe[IMG];
    int32_t ret = 0;
    pcps4_loader_status rc;

    memset(exe, 0, sizeof(exe));

    exe[0] = 'M'; exe[1] = 'Z';
    w32(exe, 0x3C, PE_OFF);
    exe[PE_OFF + 0] = 'P'; exe[PE_OFF + 1] = 'E';

    w16(exe, PE_OFF + 4 + 0, PCPS4_PE_MACHINE_AMD64);
    w16(exe, PE_OFF + 4 + 2, 1);           /* one section */
    w16(exe, PE_OFF + 4 + 16, OPT_SIZE);

    w16(exe, OPT_OFF + 0, PCPS4_PE_MAGIC_PE32PLUS);
    w32(exe, OPT_OFF + 16, TEXT_VA);       /* entry point RVA */
    w64(exe, OPT_OFF + 24, 0x140000000ull);/* preferred image base */
    w32(exe, OPT_OFF + 56, 0x2000);        /* size of image */
    w32(exe, OPT_OFF + 60, 0x200);         /* size of headers */
    w32(exe, OPT_OFF + 108, PCPS4_PE_NUM_DATA_DIRECTORIES);

    /* .text, executable, no imports, no relocations. */
    memcpy(exe + SEC_OFF, ".text", 5);
    w32(exe, SEC_OFF + 8, 0x1000);
    w32(exe, SEC_OFF + 12, TEXT_VA);
    w32(exe, SEC_OFF + 16, 0x200);
    w32(exe, SEC_OFF + 20, TEXT_RAW);
    w32(exe, SEC_OFF + 36,
        PCPS4_PE_SCN_MEM_READ | PCPS4_PE_SCN_MEM_EXECUTE);

    /* Entry code: mov eax, 0x1234 ; ret
     * B8 34 12 00 00   C3 */
    exe[TEXT_RAW + 0] = 0xB8;
    exe[TEXT_RAW + 1] = 0x34;
    exe[TEXT_RAW + 2] = 0x12;
    exe[TEXT_RAW + 3] = 0x00;
    exe[TEXT_RAW + 4] = 0x00;
    exe[TEXT_RAW + 5] = 0xC3;

    pcps4_status_log("[pcps4] PE exec smoke: loading self-contained test binary...");
    rc = pcps4_loader_run(exe, sizeof(exe), NULL, &ret);
    if (rc != PCPS4_LOADER_OK) {
        pcps4_status_log("[pcps4] PE exec smoke FAILED: %s",
               pcps4_loader_status_str(rc));
        return 1;
    }
    if (ret != 0x1234) {
        pcps4_status_log("[pcps4] PE exec smoke FAILED: got 0x%x, expected 0x1234",
               (unsigned)ret);
        return 1;
    }
    pcps4_status_log("[pcps4] PE exec smoke PASSED: executed PE returned 0x1234");
    return 0;
}

const char *pcps4_loader_status_str(pcps4_loader_status status)
{
    switch (status) {
        case PCPS4_LOADER_OK:            return "ok";
        case PCPS4_LOADER_ERR_ALLOC:     return "memory allocation failed";
        case PCPS4_LOADER_ERR_LOAD:      return "image load failed";
        case PCPS4_LOADER_ERR_PROTECT:   return "could not make memory executable";
        case PCPS4_LOADER_ERR_UNRESOLVED:return "unresolved imports";
        case PCPS4_LOADER_ERR_NULL:      return "null argument";
        default:                         return "unknown error";
    }
}
