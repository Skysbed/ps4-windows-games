#include "pcps4_shader.h"

#include <string.h>

static uint32_t read_u32(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int in_range(size_t offset, size_t length, size_t total)
{
    return offset <= total && length <= total - offset;
}

int pcps4_dxbc_inspect(const void *data,
                       size_t size,
                       pcps4_dxbc_info_t *info)
{
    const uint8_t *bytes;
    uint32_t total_size;
    uint32_t chunk_count;
    uint32_t i;
    int shader_chunks = 0;

    if (data == NULL || info == NULL)
        return PCPS4_DXBC_ERR_ARGUMENT;

    memset(info, 0, sizeof(*info));

    if (size < 32)
        return PCPS4_DXBC_ERR_TRUNCATED;

    bytes = (const uint8_t *)data;

    /*
     * DXBC magic is ASCII "DXBC".
     */
    if (bytes[0] != 'D' ||
        bytes[1] != 'X' ||
        bytes[2] != 'B' ||
        bytes[3] != 'C')
        return PCPS4_DXBC_ERR_MAGIC;

    total_size = read_u32(bytes + 24);
    chunk_count = read_u32(bytes + 28);

    if (total_size < 32 ||
        total_size > size)
        return PCPS4_DXBC_ERR_RANGE;

    /*
     * The offset table is part of the 32-byte header.
     * Bound the count so malformed data cannot cause huge iteration.
     */
    if (chunk_count == 0 || chunk_count > 4096)
        return PCPS4_DXBC_ERR_FORMAT;

    if (32u > total_size ||
        (size_t)chunk_count > (total_size - 32u) / 4u)
        return PCPS4_DXBC_ERR_TRUNCATED;

    info->total_size = total_size;
    info->chunk_count = chunk_count;

    for (i = 0; i < chunk_count; ++i) {
        size_t offset_pos = 32u + (size_t)i * 4u;
        uint32_t chunk_offset;
        uint32_t chunk_size;
        const uint8_t *chunk;

        if (!in_range(offset_pos, 4, total_size))
            return PCPS4_DXBC_ERR_TRUNCATED;

        chunk_offset = read_u32(bytes + offset_pos);

        if (!in_range(chunk_offset, 8, total_size))
            return PCPS4_DXBC_ERR_RANGE;

        chunk = bytes + chunk_offset;
        chunk_size = read_u32(chunk + 4);

        if (!in_range(chunk_offset + 8u, chunk_size, total_size))
            return PCPS4_DXBC_ERR_RANGE;

        if (memcmp(chunk, "SHDR", 4) == 0 ||
            memcmp(chunk, "SHEX", 4) == 0) {
            info->shader_chunk_count++;
            shader_chunks = 1;
        } else if (memcmp(chunk, "RDEF", 4) == 0) {
            info->rdef_chunk_count++;
        } else if (memcmp(chunk, "ISGN", 4) == 0 ||
                   memcmp(chunk, "ISG1", 4) == 0 ||
                   memcmp(chunk, "ISG2", 4) == 0) {
            info->input_signature_count++;
        } else if (memcmp(chunk, "OSGN", 4) == 0 ||
                   memcmp(chunk, "OSG1", 4) == 0 ||
                   memcmp(chunk, "OSG5", 4) == 0) {
            info->output_signature_count++;
        } else if (memcmp(chunk, "ILDN", 4) == 0 ||
                   memcmp(chunk, "SPDB", 4) == 0) {
            info->has_debug_chunks = 1;
        }
    }

    return shader_chunks
        ? PCPS4_DXBC_OK
        : PCPS4_DXBC_ERR_NO_SHADER_CHUNK;
}
