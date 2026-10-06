#ifndef PCPS4_SHADER_H
#define PCPS4_SHADER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PCPS4_DXBC_OK = 0,
    PCPS4_DXBC_ERR_ARGUMENT = -1,
    PCPS4_DXBC_ERR_MAGIC = -2,
    PCPS4_DXBC_ERR_TRUNCATED = -3,
    PCPS4_DXBC_ERR_RANGE = -4,
    PCPS4_DXBC_ERR_FORMAT = -5,
    PCPS4_DXBC_ERR_NO_SHADER_CHUNK = -6
};

typedef struct {
    uint32_t total_size;
    uint32_t chunk_count;
    uint32_t shader_chunk_count;
    uint32_t rdef_chunk_count;
    uint32_t input_signature_count;
    uint32_t output_signature_count;
    uint32_t has_debug_chunks;
} pcps4_dxbc_info_t;

int pcps4_dxbc_inspect(const void *data,
                       size_t size,
                       pcps4_dxbc_info_t *info);

#ifdef __cplusplus
}
#endif

#endif
