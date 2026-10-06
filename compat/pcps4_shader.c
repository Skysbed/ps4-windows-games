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
    info->program_type = PCPS4_DXBC_PROGRAM_UNKNOWN;

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
            /* Decode the shader model from the first shader chunk's version
             * token (the first dword of the chunk body). */
            if (info->shader_chunk_count == 0 && chunk_size >= 4) {
                uint32_t token = read_u32(chunk + 8);
                info->program_type = (token >> 16) & 0xFFFFu;
                info->sm_major = (token >> 4) & 0xFu;
                info->sm_minor = token & 0xFu;
            }
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

/* Locate a chunk whose fourcc matches any in `fourccs` (NULL-terminated list).
 * On success sets *out_body to the chunk payload and *out_body_size to its
 * size (both bounded to the DXBC total size). Returns 1 if found, 0 if not. */
static int dxbc_find_chunk(const uint8_t *bytes, uint32_t total_size,
                           uint32_t chunk_count,
                           const char *const *fourccs,
                           const uint8_t **out_body, uint32_t *out_body_size)
{
    uint32_t i;
    for (i = 0; i < chunk_count; ++i) {
        size_t offset_pos = 32u + (size_t)i * 4u;
        uint32_t chunk_offset, chunk_size;
        const uint8_t *chunk;
        int k;

        if (!in_range(offset_pos, 4, total_size))
            return 0;
        chunk_offset = read_u32(bytes + offset_pos);
        if (!in_range(chunk_offset, 8, total_size))
            return 0;
        chunk = bytes + chunk_offset;
        chunk_size = read_u32(chunk + 4);
        if (!in_range(chunk_offset + 8u, chunk_size, total_size))
            return 0;

        for (k = 0; fourccs[k] != NULL; ++k) {
            if (memcmp(chunk, fourccs[k], 4) == 0) {
                *out_body = chunk + 8;
                *out_body_size = chunk_size;
                return 1;
            }
        }
    }
    return 0;
}

int pcps4_dxbc_extract_signature(const void *data,
                                 size_t size,
                                 int which,
                                 pcps4_dxbc_signature_t *out)
{
    static const char *const input_fourccs[] =
        { "ISGN", "ISG1", "ISG2", NULL };
    static const char *const output_fourccs[] =
        { "OSGN", "OSG1", "OSG5", NULL };

    const uint8_t *bytes;
    uint32_t total_size, chunk_count;
    const uint8_t *body = NULL;
    uint32_t body_size = 0;
    uint32_t element_count, i;

    if (data == NULL || out == NULL)
        return PCPS4_DXBC_ERR_ARGUMENT;

    memset(out, 0, sizeof(*out));

    if (size < 32)
        return PCPS4_DXBC_ERR_TRUNCATED;
    bytes = (const uint8_t *)data;
    if (bytes[0] != 'D' || bytes[1] != 'X' ||
        bytes[2] != 'B' || bytes[3] != 'C')
        return PCPS4_DXBC_ERR_MAGIC;

    total_size = read_u32(bytes + 24);
    chunk_count = read_u32(bytes + 28);
    if (total_size < 32 || total_size > size)
        return PCPS4_DXBC_ERR_RANGE;
    if (chunk_count == 0 || chunk_count > 4096)
        return PCPS4_DXBC_ERR_FORMAT;

    if (!dxbc_find_chunk(bytes, total_size, chunk_count,
                         which == 0 ? input_fourccs : output_fourccs,
                         &body, &body_size))
        return PCPS4_DXBC_ERR_NO_SIGNATURE;

    /* Signature chunk body: [u32 element count][u32 table offset=8][elements].
     * Each element is 24 bytes; name strings follow, referenced by an offset
     * relative to the start of the chunk body. */
    if (body_size < 8)
        return PCPS4_DXBC_ERR_TRUNCATED;
    element_count = read_u32(body);
    if (element_count > PCPS4_DXBC_MAX_SIGNATURE_ELEMENTS)
        element_count = PCPS4_DXBC_MAX_SIGNATURE_ELEMENTS;

    /* Elements start at body + 8. */
    if ((size_t)element_count * 24u > body_size - 8u)
        return PCPS4_DXBC_ERR_TRUNCATED;

    for (i = 0; i < element_count; ++i) {
        const uint8_t *e = body + 8u + (size_t)i * 24u;
        uint32_t name_off = read_u32(e + 0);
        pcps4_dxbc_signature_element_t *dst = &out->elements[i];
        uint32_t n;

        dst->semantic_index = read_u32(e + 4);
        dst->system_value   = read_u32(e + 8);
        dst->component_type = read_u32(e + 12);
        dst->reg            = read_u32(e + 16);
        dst->mask           = e[20];
        dst->rw_mask        = e[21];

        /* Copy the semantic name, bounded by the chunk and the field size. */
        if (name_off < body_size) {
            for (n = 0; n + 1 < PCPS4_DXBC_SEMANTIC_NAME_MAX &&
                        (size_t)name_off + n < body_size; ++n) {
                char c = (char)body[name_off + n];
                if (c == '\0')
                    break;
                dst->semantic_name[n] = c;
            }
            dst->semantic_name[n] = '\0';
        }
    }

    out->count = element_count;
    return PCPS4_DXBC_OK;
}
