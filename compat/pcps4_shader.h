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

/* --- resource bindings (RDEF) ------------------------------------------ */

/* D3D_SHADER_INPUT_TYPE values. */
enum {
    PCPS4_DXBC_INPUT_CBUFFER = 0,
    PCPS4_DXBC_INPUT_TBUFFER = 1,
    PCPS4_DXBC_INPUT_TEXTURE = 2,
    PCPS4_DXBC_INPUT_SAMPLER = 3,
    PCPS4_DXBC_INPUT_UAV_RWTYPED = 4,
    PCPS4_DXBC_INPUT_STRUCTURED = 5,
    PCPS4_DXBC_INPUT_UAV_RWSTRUCTURED = 6,
    PCPS4_DXBC_INPUT_BYTEADDRESS = 7,
    PCPS4_DXBC_INPUT_UAV_RWBYTEADDRESS = 8
};

#define PCPS4_DXBC_MAX_BINDINGS 32
#define PCPS4_DXBC_MAX_CBUFFERS 16
#define PCPS4_DXBC_RESOURCE_NAME_MAX 48

typedef struct {
    char     name[PCPS4_DXBC_RESOURCE_NAME_MAX];
    uint32_t input_type;   /* PCPS4_DXBC_INPUT_* */
    uint32_t return_type;
    uint32_t dimension;    /* D3D_SRV_DIMENSION (texture shape) */
    uint32_t bind_point;   /* register slot: b#, t#, s#, u# */
    uint32_t bind_count;
} pcps4_dxbc_binding_t;

typedef struct {
    char     name[PCPS4_DXBC_RESOURCE_NAME_MAX];
    uint32_t size;            /* bytes */
    uint32_t variable_count;
} pcps4_dxbc_cbuffer_t;

typedef struct {
    pcps4_dxbc_binding_t bindings[PCPS4_DXBC_MAX_BINDINGS];
    uint32_t binding_count;
    pcps4_dxbc_cbuffer_t cbuffers[PCPS4_DXBC_MAX_CBUFFERS];
    uint32_t cbuffer_count;
} pcps4_dxbc_resources_t;

/*
 * Read a shader's resource definitions (RDEF): every bound resource (constant
 * buffers, textures, samplers, UAVs) with its register slot, plus each
 * constant buffer's size. Returns PCPS4_DXBC_ERR_NO_SIGNATURE if the shader
 * has no RDEF chunk (e.g. it was stripped).
 */
int pcps4_dxbc_extract_resources(const void *data,
                                 size_t size,
                                 pcps4_dxbc_resources_t *out);

#ifdef __cplusplus
}
#endif

#endif
