#ifndef PCPS4_TEB_H
#define PCPS4_TEB_H

/*
 * A stand-in for the Windows thread information block (TEB).
 *
 * Windows x64 code reads per-thread data through the GS segment register
 * (gs:[0x30] is the TEB itself, gs:[0x58] the TLS array, gs:[0x60] the PEB).
 * The PS4 has no such register setup, so the first such read dereferences a
 * bad address. As each image is loaded we rewrite those loads into ordinary
 * RIP-relative loads from a small fake TEB placed right after the image, so
 * the displacement always fits in 32 bits.
 */

#include <stddef.h>
#include <stdint.h>

#include "pcps4_pe.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Extra bytes to map after each image to hold the fake TEB/PEB/TLS array. */
#define PCPS4_TEB_PAGE 0x4000u

/* Fill the (zeroed, writable) page: TEB.Self, stack bounds, PEB, TLS array. */
void pcps4_teb_init(uint8_t *page, const uint8_t *image_base);

/*
 * Rewrite `mov reg, gs:[disp32]` (64- and 32-bit forms) inside executable
 * sections to `mov reg, [rip+disp]` reading the same offset of the fake TEB.
 * Call before the image is made executable. Returns the number rewritten.
 */
uint32_t pcps4_teb_patch_image(const pcps4_pe_image *image, uint8_t *mem,
                               const uint8_t *teb_page);

#ifdef __cplusplus
}
#endif

#endif
