/*
 * Replacements for the pieces libpsbc.orbis.a leaves out on purpose.
 *
 * The PS4 build of the shader compiler drops the x86 SIMD BLAKE3 assembly, the
 * S3TC texture packers and Mesa's logging helper. Code that links the library
 * still references them, so provide safe equivalents:
 *   - the BLAKE3 SIMD entry points forward to the portable implementation that
 *     IS in the library (same results, just slower);
 *   - the S3TC *packers* (texture compression) are never used for shader
 *     compilation and are no-ops;
 *   - Mesa log output is discarded.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BLAKE3_BLOCK_LEN 64

void blake3_compress_in_place_portable(uint32_t cv[8], const uint8_t block[BLAKE3_BLOCK_LEN],
									   uint8_t block_len, uint64_t counter, uint8_t flags);
void blake3_compress_xof_portable(const uint32_t cv[8], const uint8_t block[BLAKE3_BLOCK_LEN],
								  uint8_t block_len, uint64_t counter, uint8_t flags,
								  uint8_t out[64]);
void blake3_hash_many_portable(const uint8_t* const* inputs, size_t num_inputs, size_t blocks,
							   const uint32_t key[8], uint64_t counter, bool increment_counter,
							   uint8_t flags, uint8_t flags_start, uint8_t flags_end,
							   uint8_t* out);

#define COMPRESS_IN_PLACE(sfx)                                                          \
	void blake3_compress_in_place_##sfx(uint32_t cv[8], const uint8_t block[64],        \
										uint8_t block_len, uint64_t counter,            \
										uint8_t flags) {                                \
		blake3_compress_in_place_portable(cv, block, block_len, counter, flags);        \
	}
#define COMPRESS_XOF(sfx)                                                               \
	void blake3_compress_xof_##sfx(const uint32_t cv[8], const uint8_t block[64],       \
								   uint8_t block_len, uint64_t counter, uint8_t flags,  \
								   uint8_t out[64]) {                                   \
		blake3_compress_xof_portable(cv, block, block_len, counter, flags, out);        \
	}
#define HASH_MANY(sfx)                                                                  \
	void blake3_hash_many_##sfx(const uint8_t* const* inputs, size_t num_inputs,        \
								size_t blocks, const uint32_t key[8], uint64_t counter, \
								bool increment_counter, uint8_t flags,                  \
								uint8_t flags_start, uint8_t flags_end, uint8_t* out) { \
		blake3_hash_many_portable(inputs, num_inputs, blocks, key, counter,             \
								  increment_counter, flags, flags_start, flags_end,     \
								  out);                                                 \
	}

COMPRESS_IN_PLACE(sse2)
COMPRESS_IN_PLACE(sse41)
COMPRESS_IN_PLACE(avx512)
COMPRESS_XOF(sse2)
COMPRESS_XOF(sse41)
COMPRESS_XOF(avx512)
HASH_MANY(sse2)
HASH_MANY(sse41)
HASH_MANY(avx2)
HASH_MANY(avx512)

void blake3_xof_many_avx512(const uint32_t cv[8], const uint8_t block[64], uint8_t block_len,
							uint64_t counter, uint8_t flags, uint8_t* out, size_t outblocks) {
	for (size_t i = 0; i < outblocks; i += 1) {
		blake3_compress_xof_portable(cv, block, block_len, counter + i, flags, out + 64 * i);
	}
}

/* --- S3TC texture helpers: shader compilation never calls them ------------- */
/* x86-64 System V lets a callee ignore its arguments, so one signature does. */
void util_format_dxt1_rgb_fetch(void) {}
void util_format_dxt1_rgb_fetch_rgba(void) {}
void util_format_dxt1_rgb_fetch_rgba_8unorm(void) {}
void util_format_dxt1_rgb_pack_rgba_8unorm(void) {}
void util_format_dxt1_rgb_pack_rgba_float(void) {}
void util_format_dxt1_rgb_unpack_rgba_8unorm(void) {}
void util_format_dxt1_rgb_unpack_rgba_float(void) {}
void util_format_dxt1_rgba_fetch(void) {}
void util_format_dxt1_rgba_fetch_rgba(void) {}
void util_format_dxt1_rgba_fetch_rgba_8unorm(void) {}
void util_format_dxt1_rgba_pack_rgba_8unorm(void) {}
void util_format_dxt1_rgba_pack_rgba_float(void) {}
void util_format_dxt1_rgba_unpack_rgba_8unorm(void) {}
void util_format_dxt1_rgba_unpack_rgba_float(void) {}
void util_format_dxt1_srgb_fetch_rgba(void) {}
void util_format_dxt1_srgb_fetch_rgba_8unorm(void) {}
void util_format_dxt1_srgb_pack_rgba_8unorm(void) {}
void util_format_dxt1_srgb_pack_rgba_float(void) {}
void util_format_dxt1_srgb_unpack_rgba_8unorm(void) {}
void util_format_dxt1_srgb_unpack_rgba_float(void) {}
void util_format_dxt1_srgba_fetch_rgba(void) {}
void util_format_dxt1_srgba_fetch_rgba_8unorm(void) {}
void util_format_dxt1_srgba_pack_rgba_8unorm(void) {}
void util_format_dxt1_srgba_pack_rgba_float(void) {}
void util_format_dxt1_srgba_unpack_rgba_8unorm(void) {}
void util_format_dxt1_srgba_unpack_rgba_float(void) {}
void util_format_dxt3_rgba_fetch(void) {}
void util_format_dxt3_rgba_fetch_rgba(void) {}
void util_format_dxt3_rgba_fetch_rgba_8unorm(void) {}
void util_format_dxt3_rgba_pack_rgba_8unorm(void) {}
void util_format_dxt3_rgba_pack_rgba_float(void) {}
void util_format_dxt3_rgba_unpack_rgba_8unorm(void) {}
void util_format_dxt3_rgba_unpack_rgba_float(void) {}
void util_format_dxt3_srgba_fetch_rgba(void) {}
void util_format_dxt3_srgba_fetch_rgba_8unorm(void) {}
void util_format_dxt3_srgba_pack_rgba_8unorm(void) {}
void util_format_dxt3_srgba_pack_rgba_float(void) {}
void util_format_dxt3_srgba_unpack_rgba_8unorm(void) {}
void util_format_dxt3_srgba_unpack_rgba_float(void) {}
void util_format_dxt5_rgba_fetch(void) {}
void util_format_dxt5_rgba_fetch_rgba(void) {}
void util_format_dxt5_rgba_fetch_rgba_8unorm(void) {}
void util_format_dxt5_rgba_pack_rgba_8unorm(void) {}
void util_format_dxt5_rgba_pack_rgba_float(void) {}
void util_format_dxt5_rgba_unpack_rgba_8unorm(void) {}
void util_format_dxt5_rgba_unpack_rgba_float(void) {}
void util_format_dxt5_srgba_fetch_rgba(void) {}
void util_format_dxt5_srgba_fetch_rgba_8unorm(void) {}
void util_format_dxt5_srgba_pack_rgba_8unorm(void) {}
void util_format_dxt5_srgba_pack_rgba_float(void) {}
void util_format_dxt5_srgba_unpack_rgba_8unorm(void) {}
void util_format_dxt5_srgba_unpack_rgba_float(void) {}
void util_format_dxtn(void) {}
void util_format_dxtn_fetch_t(void) {}
void util_format_dxtn_pack(void) {}
void util_format_dxtn_pack_t(void) {}

/* --- Mesa logging: discard ------------------------------------------------ */
void _mesa_log_multiline(int level, const char* tag, const char* lines) {
	(void)level; (void)tag; (void)lines;
}
