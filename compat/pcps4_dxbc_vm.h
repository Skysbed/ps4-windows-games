#ifndef PCPS4_DXBC_VM_H
#define PCPS4_DXBC_VM_H

/*
 * DXBC (Shader Model 4/5) interpreter.
 *
 * Executes the SHDR/SHEX token stream of a compiled D3D shader on the CPU, so
 * a game's own vertex and pixel shaders can run on the PS4 without a GPU
 * shader compiler. It is deliberately a straightforward interpreter: correct
 * first, fast later. Opcode numbers, register types and token layouts follow
 * the public D3D10/11 tokenized program format (cross-checked against
 * doitsujin/dxbc-spirv, MIT).
 *
 * Supported: float/int/uint ALU, comparisons, movc, dot products, rounding,
 * sincos, conversions, if/else/endif, loop/break/breakc, ret/retc, discard,
 * constant buffers, immediate constant buffers, indexable temps with relative
 * indexing, and sample/sample_l/sample_b through a texture callback.
 * Not yet supported (reported as PCPS4_VM_ERR_UNSUPPORTED when executed):
 * switch/call, resource loads, gather, UAV/atomics, geometry/tessellation/
 * compute stages.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PCPS4_VM_OK = 0,
    PCPS4_VM_ERR_ARGUMENT = -1,
    PCPS4_VM_ERR_FORMAT = -2,
    PCPS4_VM_ERR_UNSUPPORTED = -3,
    PCPS4_VM_ERR_LIMIT = -4,
    PCPS4_VM_ERR_RUNTIME = -5
};

#define PCPS4_VM_MAX_TEMPS 256
#define PCPS4_VM_MAX_IO 32
#define PCPS4_VM_MAX_CB 16
#define PCPS4_VM_MAX_ICB 1024
#define PCPS4_VM_MAX_XTEMP_SLOTS 4
#define PCPS4_VM_MAX_XTEMP 256

typedef union {
    float f[4];
    int32_t i[4];
    uint32_t u[4];
} pcps4_vm_reg_t;

/* How the sampler callback should treat `param`. */
enum {
    PCPS4_VM_SAMPLE_IMPLICIT = 0, /* sample */
    PCPS4_VM_SAMPLE_LOD = 1,      /* sample_l: param = explicit LOD */
    PCPS4_VM_SAMPLE_BIAS = 2      /* sample_b: param = LOD bias */
};

typedef void (*pcps4_vm_sample_fn)(void *user,
                                   uint32_t texture,
                                   uint32_t sampler,
                                   const float coord[4],
                                   float param,
                                   int mode,
                                   float out[4]);

typedef struct pcps4_vm_instr pcps4_vm_instr_t;

typedef struct pcps4_vm {
    /* Decoded program (owned). */
    pcps4_vm_instr_t *instrs;
    uint32_t instr_count;
    uint32_t program_type; /* 0 pixel, 1 vertex */
    uint32_t sm_major;
    uint32_t sm_minor;
    uint32_t temp_count;

    /* Inputs set by the caller before pcps4_vm_run. */
    pcps4_vm_reg_t in[PCPS4_VM_MAX_IO];
    /* Constant buffers: pointer to vec4 data and its size in vec4s. */
    const pcps4_vm_reg_t *cb[PCPS4_VM_MAX_CB];
    uint32_t cb_size[PCPS4_VM_MAX_CB];
    pcps4_vm_sample_fn sample;
    void *sample_user;

    /* Results. */
    pcps4_vm_reg_t out[PCPS4_VM_MAX_IO];
    float depth;
    int discarded;

    /* Diagnostics. */
    uint32_t bad_opcode;

    /* Internal state. */
    pcps4_vm_reg_t r[PCPS4_VM_MAX_TEMPS];
    pcps4_vm_reg_t xr[PCPS4_VM_MAX_XTEMP_SLOTS][PCPS4_VM_MAX_XTEMP];
    pcps4_vm_reg_t icb[PCPS4_VM_MAX_ICB];
    uint32_t icb_count;
} pcps4_vm_t;

/* Parse a DXBC container and prepare a program. Returns NULL on failure and
 * writes a PCPS4_VM_* code to *status when non-NULL. */
pcps4_vm_t *pcps4_vm_create(const void *dxbc, size_t size, int *status);
void pcps4_vm_destroy(pcps4_vm_t *vm);

/* Clear inputs, outputs and constant-buffer bindings (program is kept). */
void pcps4_vm_reset_io(pcps4_vm_t *vm);

/* Execute once with the current inputs. Outputs land in vm->out. */
int pcps4_vm_run(pcps4_vm_t *vm);

#ifdef __cplusplus
}
#endif

#endif /* PCPS4_DXBC_VM_H */
