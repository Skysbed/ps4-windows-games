/* Fake TEB and GS-relative load rewriting. See pcps4_teb.h. */

#include "pcps4_teb.h"

#include <string.h>

void pcps4_teb_init(uint8_t *page, const uint8_t *image_base)
{
    uint64_t *q = (uint64_t *)(void *)page;
    uint8_t *tls = page + 0x400;
    uint8_t *peb = page + 0x800;
    volatile uint8_t anchor = 0;
    uintptr_t here = (uintptr_t)&anchor;

    memset(page, 0, PCPS4_TEB_PAGE);

    q[0x00 / 8] = 0;                              /* ExceptionList */
    q[0x08 / 8] = (uint64_t)(here + 0x100000u);   /* StackBase (approximate) */
    q[0x10 / 8] = (uint64_t)(here - 0x100000u);   /* StackLimit (approximate) */
    q[0x30 / 8] = (uint64_t)(uintptr_t)page;      /* NT_TIB.Self == the TEB */
    q[0x40 / 8] = 1;                              /* ClientId.UniqueProcess */
    q[0x48 / 8] = 1;                              /* ClientId.UniqueThread */
    q[0x58 / 8] = (uint64_t)(uintptr_t)tls;       /* ThreadLocalStoragePointer */
    q[0x60 / 8] = (uint64_t)(uintptr_t)peb;       /* ProcessEnvironmentBlock */
    *(uint64_t *)(void *)(peb + 0x10) = (uint64_t)(uintptr_t)image_base; /* ImageBaseAddress */
}

uint32_t pcps4_teb_patch_image(const pcps4_pe_image *image, uint8_t *mem,
                               const uint8_t *teb_page)
{
    uint32_t patched = 0;
    uint16_t s;

    if (image == NULL || mem == NULL || teb_page == NULL)
        return 0;

    for (s = 0; s < image->section_count; ++s) {
        pcps4_pe_section sec;
        uint8_t *b;
        size_t n, i;

        if (pcps4_pe_get_section(image, s, &sec) != PCPS4_PE_OK)
            continue;
        if ((sec.characteristics & PCPS4_PE_SCN_MEM_EXECUTE) == 0)
            continue;

        b = mem + sec.virtual_address;
        n = sec.virtual_size > sec.raw_size ? sec.virtual_size : sec.raw_size;

        for (i = 0; i + 10 <= n; ++i) {
            size_t j;
            uint8_t rex = 0, modrm;
            size_t total, newlen;
            uint32_t off;
            int64_t disp;

            if (b[i] != 0x65) /* GS segment override */
                continue;
            j = i + 1;
            if ((b[j] & 0xF0) == 0x40) {
                rex = b[j];
                j += 1;
            }
            /* 8B /r with a [disp32] operand: ModRM mod=00, rm=100 (SIB), SIB=0x25 */
            if (b[j] != 0x8B)
                continue;
            modrm = b[j + 1];
            if ((modrm & 0xC7) != 0x04 || b[j + 2] != 0x25)
                continue;

            memcpy(&off, &b[j + 3], 4);
            if (off >= PCPS4_TEB_PAGE)
                continue;

            total = (j + 7) - i;                      /* 65 [rex] 8B modrm 25 disp32 */
            newlen = (rex ? 1u : 0u) + 1u + 1u + 4u;  /* [rex] 8B modrm' disp32' */

            disp = (int64_t)((uintptr_t)teb_page + off) -
                   (int64_t)((uintptr_t)&b[i] + newlen);
            if (disp > 0x7FFFFFFF || disp < -(int64_t)0x80000000)
                continue; /* out of RIP-relative reach: leave it alone */

            {
                size_t w = i;
                int32_t d32 = (int32_t)disp;
                if (rex)
                    b[w++] = rex;
                b[w++] = 0x8B;
                b[w++] = (uint8_t)((modrm & 0x38) | 0x05); /* same reg, mod=00 rm=101 (RIP+disp32) */
                memcpy(&b[w], &d32, 4);
                w += 4;
                while (w < i + total)
                    b[w++] = 0x90; /* NOP the leftover bytes */
            }
            patched += 1;
            i += total - 1;
        }
    }
    return patched;
}
