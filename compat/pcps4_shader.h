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
    PCPS4_DXBC_ERR_NO_SHADER_CHUNK = -6,
    PCPS4_DXBC_ERR_NO_SIGNATURE = -7
};

/* Shader program type, from the shader chunk's version token. */
enum {
    PCPS4_DXBC_PROGRAM_PIXEL = 0,
    PCPS4_DXBC_PROGRAM_VERTEX = 1,
    PCPS4_DXBC_PROGRAM_GEOMETRY = 2,
    PCPS4_DXBC_PROGRAM_HULL = 3,
    PCPS4_DXBC_PROGRAM_DOMAIN = 4,
    PCPS4_DXBC_PROGRAM_COMPUTE = 5,
    PCPS4_DXBC_PROGRAM_UNKNOWN = 0xFFFF
};

/* D3D register component types (ComponentType field of a signature element). */
enum {
    PCPS4_DXBC_COMPONENT_UNKNOWN = 0,
    PCPS4_DXBC_COMPONENT_UINT32 = 1,
    PCPS4_DXBC_COMPONENT_SINT32 = 2,
    PCPS4_DXBC_COMPONENT_FLOAT32 = 3
};

typedef struct {
    uint32_t total_size;
    uint32_t chunk_count;
    uint32_t shader_chunk_count;
    uint32_t rdef_chunk_count;
    uint32_t input_signature_count;
    uint32_t output_signature_count;
    uint32_t has_debug_chunks;
    /* Decoded from the first shader (SHDR/SHEX) chunk. */
    uint32_t program_type;   /* one of PCPS4_DXBC_PROGRAM_* */
    uint32_t sm_major;
    uint32_t sm_minor;
} pcps4_dxbc_info_t;

int pcps4_dxbc_inspect(const void *data,
                       size_t size,
                       pcps4_dxbc_info_t *info);

/* --- signature extraction ---------------------------------------------- */

#define PCPS4_DXBC_MAX_SIGNATURE_ELEMENTS 32
#define PCPS4_DXBC_SEMANTIC_NAME_MAX 32

typedef struct {
    char     semantic_name[PCPS4_DXBC_SEMANTIC_NAME_MAX];
    uint32_t semantic_index;
    uint32_t system_value;   /* D3D_NAME */
    uint32_t component_type;  /* PCPS4_DXBC_COMPONENT_* */
    uint32_t reg;             /* input/output register index */
    uint8_t  mask;            /* component mask declared */
    uint8_t  rw_mask;         /* read (input) or write (output) mask */
} pcps4_dxbc_signature_element_t;

typedef struct {
    pcps4_dxbc_signature_element_t elements[PCPS4_DXBC_MAX_SIGNATURE_ELEMENTS];
    uint32_t count;
} pcps4_dxbc_signature_t;

/*
 * Extract a shader's input (ISGN) or output (OSGN) signature: the list of
 * per-register semantics, types and masks. `which` selects the signature:
 * 0 = input, 1 = output. Returns PCPS4_DXBC_ERR_NO_SIGNATURE if the requested
 * signature chunk is absent.
 */
int pcps4_dxbc_extract_signature(const void *data,
                                 size_t size,
                                 int which,
                                 pcps4_dxbc_signature_t *out);

#ifdef __cplusplus
}
#endif

#endif
