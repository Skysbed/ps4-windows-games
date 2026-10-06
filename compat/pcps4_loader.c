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

#include <stdio.h>
#include <string.h>

#include <sys/mman.h>

/* OpenOrbis kernel memory APIs (declared with concrete signatures here;
 * libkernel exports the symbols). */
extern int32_t sceKernelMmap(void *addr, size_t len, int32_t prot,
                             int32_t flags, int32_t fd, off_t offset,
                             void **res);
extern int32_t sceKernelMprotect(const void *addr, size_t len, int prot);
extern int32_t sceKernelMunmap(void *addr, size_t len);

typedef int (*pcps4_entry_fn)(void);

/* Round a size up to the 16 KiB PS4 page granule. */
static size_t pcps4_round_up_page(size_t n)
{
    const size_t page = 0x4000;
    size_t r = n + (page - 1);
    return r - (r % page);
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
        printf("[pcps4] load parse failed: %s\n", pcps4_pe_status_str(prc));
        return PCPS4_LOADER_ERR_LOAD;
    }

    map_size = pcps4_round_up_page(probe.size_of_image);
    if (map_size == 0)
        return PCPS4_LOADER_ERR_LOAD;

    /* Allocate anonymous RW memory for the image. */
    krc = sceKernelMmap(NULL, map_size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON, -1, 0, &mem);
    if (krc != 0 || mem == NULL) {
        printf("[pcps4] mmap failed: 0x%x\n", (unsigned)krc);
        return PCPS4_LOADER_ERR_ALLOC;
    }

    /* Map + bind at the real runtime address so relocations resolve. */
    prc = pcps4_pe_load(exe, exe_size, (uint8_t *)mem, map_size,
                        (uint64_t)(uintptr_t)mem, resolver,
                        &image, &resolved, &unresolved);
    if (prc != PCPS4_PE_OK) {
        printf("[pcps4] load failed: %s\n", pcps4_pe_status_str(prc));
        sceKernelMunmap(mem, map_size);
        return PCPS4_LOADER_ERR_LOAD;
    }

    printf("[pcps4] imports resolved=%u unresolved=%u\n",
           (unsigned)resolved, (unsigned)unresolved);

    if (unresolved != 0) {
        /* Never jump into code that will call a missing API. */
        printf("[pcps4] refusing to run: %u unresolved import(s)\n",
               (unsigned)unresolved);
        sceKernelMunmap(mem, map_size);
        return PCPS4_LOADER_ERR_UNRESOLVED;
    }

    /* Add execute permission. On retail firmware this is refused. */
    krc = sceKernelMprotect(mem, map_size, PROT_READ | PROT_EXEC);
    if (krc != 0) {
        printf("[pcps4] mprotect(EXEC) failed: 0x%x "
               "(console may not permit executable memory)\n", (unsigned)krc);
        sceKernelMunmap(mem, map_size);
        return PCPS4_LOADER_ERR_PROTECT;
    }

    entry = (pcps4_entry_fn)(uintptr_t)
            ((uint8_t *)mem + image.entry_point_rva);

    printf("[pcps4] entering binary at %p\n", (void *)entry);
    {
        int ret = entry();
        if (out_entry_ret)
            *out_entry_ret = (int32_t)ret;
        printf("[pcps4] binary returned %d (0x%x)\n", ret, (unsigned)ret);
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

    printf("[pcps4] PE exec smoke: loading self-contained test binary...\n");
    rc = pcps4_loader_run(exe, sizeof(exe), NULL, &ret);
    if (rc != PCPS4_LOADER_OK) {
        printf("[pcps4] PE exec smoke FAILED: %s\n",
               pcps4_loader_status_str(rc));
        return 1;
    }
    if (ret != 0x1234) {
        printf("[pcps4] PE exec smoke FAILED: got 0x%x, expected 0x1234\n",
               (unsigned)ret);
        return 1;
    }
    printf("[pcps4] PE exec smoke PASSED: executed PE returned 0x1234\n");
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
