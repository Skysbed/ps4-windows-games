/*
 * DXBC (SM4/SM5) interpreter. See pcps4_dxbc_vm.h.
 *
 * Token layout (D3D10/11 tokenized program format):
 *   opcode token : [10:0] opcode, [13] saturate, [18] test-nonzero,
 *                  [30:24] length in dwords, [31] extended
 *   operand token: [1:0] components (0/1/4), [3:2] selection mode,
 *                  [11:4] mask / swizzle / select1, [19:12] register type,
 *                  [21:20] index dimension, [24:22][27:25][30:28] index
 *                  representation, [31] extended (modifiers)
 */

#include "pcps4_dxbc_vm.h"

#include <stdlib.h>
#include <string.h>

/* --- opcodes ------------------------------------------------------------ */
enum {
    OP_ADD = 0, OP_AND = 1, OP_BREAK = 2, OP_BREAKC = 3, OP_CONTINUE = 7,
    OP_CONTINUEC = 8, OP_DERIV_RTX = 11, OP_DERIV_RTY = 12, OP_DISCARD = 13,
    OP_DIV = 14, OP_DP2 = 15, OP_DP3 = 16, OP_DP4 = 17, OP_ELSE = 18,
    OP_ENDIF = 21, OP_ENDLOOP = 22, OP_EQ = 24, OP_EXP = 25, OP_FRC = 26,
    OP_FTOI = 27, OP_FTOU = 28, OP_GE = 29, OP_IADD = 30, OP_IF = 31,
    OP_IEQ = 32, OP_IGE = 33, OP_ILT = 34, OP_IMAD = 35, OP_IMAX = 36,
    OP_IMIN = 37, OP_IMUL = 38, OP_INE = 39, OP_INEG = 40, OP_ISHL = 41,
    OP_ISHR = 42, OP_ITOF = 43, OP_LD = 45, OP_LOG = 47, OP_LOOP = 48,
    OP_LT = 49, OP_MAD = 50, OP_MIN = 51, OP_MAX = 52, OP_CUSTOMDATA = 53,
    OP_MOV = 54, OP_MOVC = 55, OP_MUL = 56, OP_NE = 57, OP_NOP = 58,
    OP_NOT = 59, OP_OR = 60, OP_RET = 62, OP_RETC = 63, OP_ROUND_NE = 64,
    OP_ROUND_NI = 65, OP_ROUND_PI = 66, OP_ROUND_Z = 67, OP_RSQ = 68,
    OP_SAMPLE = 69, OP_SAMPLE_L = 72, OP_SAMPLE_B = 74, OP_SQRT = 75,
    OP_SINCOS = 77, OP_UDIV = 78, OP_ULT = 79, OP_UGE = 80, OP_UMUL = 81,
    OP_UMAD = 82, OP_UMAX = 83, OP_UMIN = 84, OP_USHR = 85, OP_UTOF = 86,
    OP_XOR = 87, OP_DCL_TEMPS = 104, OP_DCL_INDEXABLE_TEMP = 105,
    OP_RCP = 129,
    OP_FIRST_DECL = 88
};

/* --- register types ----------------------------------------------------- */
enum {
    REG_TEMP = 0, REG_INPUT = 1, REG_OUTPUT = 2, REG_XTEMP = 3, REG_IMM32 = 4,
    REG_SAMPLER = 6, REG_RESOURCE = 7, REG_CBV = 8, REG_ICB = 9,
    REG_DEPTH = 12, REG_NULL = 13
};

enum { MODE_MASK = 0, MODE_SWIZZLE = 1, MODE_SELECT1 = 2 };

#define MAX_OPERANDS 6
#define MAX_NEST 64
#define MAX_STEPS (1u << 22)

typedef struct {
    uint8_t type;
    uint8_t comps;     /* 0, 1 or 4 */
    uint8_t mode;
    uint8_t sel;       /* mask, swizzle or select1 component */
    uint8_t modifier;  /* 0 none, 1 neg, 2 abs, 3 -abs */
    uint8_t dim;
    uint8_t rel_present[3];
    uint8_t rel_comp[3];
    uint32_t rel_reg[3];
    uint32_t idx[3];
    uint32_t imm[4];
} vm_operand_t;

struct pcps4_vm_instr {
    uint16_t op;
    uint8_t saturate;
    uint8_t test_nz;
    uint8_t nops;
    uint32_t jump;
    vm_operand_t o[MAX_OPERANDS];
};

/* --- operand decoding --------------------------------------------------- */

static uint32_t parse_operand(const uint32_t *t, uint32_t avail,
                              vm_operand_t *o, int nested)
{
    uint32_t tok, used = 1, ncomp, d;

    if (avail < 1)
        return 0;
    tok = t[0];
    memset(o, 0, sizeof(*o));

    ncomp = tok & 3u;
    if (ncomp == 3)
        return 0; /* N-component operands are not used by SM4/5 ALU code */
    o->comps = (uint8_t)(ncomp == 0 ? 0 : (ncomp == 1 ? 1 : 4));
    if (ncomp == 2) {
        o->mode = (uint8_t)((tok >> 2) & 3u);
        o->sel = (uint8_t)((tok >> 4) & 0xFFu);
    }
    o->type = (uint8_t)((tok >> 12) & 0xFFu);
    o->dim = (uint8_t)((tok >> 20) & 3u);

    if (tok & 0x80000000u) {
        uint32_t ext;
        do {
            if (used >= avail)
                return 0;
            ext = t[used++];
            if ((ext & 0x3Fu) == 1u)
                o->modifier = (uint8_t)((ext >> 6) & 0xFFu);
        } while (ext & 0x80000000u);
    }

    if (o->type == REG_IMM32) {
        uint32_t n = ncomp == 1 ? 1u : 4u, i;
        if (used + n > avail)
            return 0;
        for (i = 0; i < n; ++i)
            o->imm[i] = t[used + i];
        if (n == 1)
            for (i = 1; i < 4; ++i)
                o->imm[i] = o->imm[0];
        return used + n;
    }

    for (d = 0; d < o->dim; ++d) {
        uint32_t repr = (tok >> (22 + 3 * d)) & 7u;
        if (repr == 0 || repr == 3) {
            if (used >= avail)
                return 0;
            o->idx[d] = t[used++];
        }
        if (repr == 2 || repr == 3) {
            vm_operand_t r;
            uint32_t n;
            if (nested)
                return 0;
            n = parse_operand(t + used, avail - used, &r, 1);
            if (n == 0 || r.type != REG_TEMP)
                return 0;
            o->rel_present[d] = 1;
            o->rel_reg[d] = r.idx[0];
            o->rel_comp[d] = (uint8_t)(r.sel & 3u);
            used += n;
        } else if (repr != 0) {
            return 0; /* 64-bit indices unsupported */
        }
    }
    return used;
}

/* --- loading ------------------------------------------------------------ */

static const uint32_t *find_shader_chunk(const uint8_t *d, size_t size,
                                         uint32_t *dwords)
{
    uint32_t count, i;

    if (size < 36 || memcmp(d, "DXBC", 4) != 0)
        return NULL;
    memcpy(&count, d + 28, 4);
    if (count == 0 || count > 64 || 32 + (size_t)count * 4 > size)
        return NULL;
    for (i = 0; i < count; ++i) {
        uint32_t off, csize;
        memcpy(&off, d + 32 + i * 4, 4);
        if ((size_t)off + 8 > size)
            return NULL;
        if (memcmp(d + off, "SHEX", 4) == 0 || memcmp(d + off, "SHDR", 4) == 0) {
            memcpy(&csize, d + off + 4, 4);
            if ((size_t)off + 8 + csize > size || (off & 3u) != 0 ||
                csize < 8 || (csize & 3u) != 0)
                return NULL;
            *dwords = csize / 4;
            return (const uint32_t *)(const void *)(d + off + 8);
        }
    }
    return NULL;
}

typedef struct { uint8_t is_loop; uint32_t pos; uint32_t else_pos; uint8_t has_else; uint32_t id; } nest_t;

pcps4_vm_t *pcps4_vm_create(const void *dxbc, size_t size, int *status)
{
    const uint32_t *t;
    uint32_t n, pos, count = 0, ii = 0, pass;
    pcps4_vm_t *vm;
    nest_t stack[MAX_NEST];
    int sp = 0;
    uint32_t loop_id = 1;
    struct { uint32_t instr; uint32_t loop; } breaks[MAX_NEST * 4];
    uint32_t nbreaks = 0;
    int rc = PCPS4_VM_OK;

    if (status) *status = PCPS4_VM_ERR_ARGUMENT;
    if (dxbc == NULL)
        return NULL;
    t = find_shader_chunk((const uint8_t *)dxbc, size, &n);
    if (t == NULL || n < 2) {
        if (status) *status = PCPS4_VM_ERR_FORMAT;
        return NULL;
    }
    if (t[1] > n) {
        if (status) *status = PCPS4_VM_ERR_FORMAT;
        return NULL;
    }
    n = t[1];

    vm = (pcps4_vm_t *)calloc(1, sizeof(*vm));
    if (vm == NULL) {
        if (status) *status = PCPS4_VM_ERR_LIMIT;
        return NULL;
    }
    vm->program_type = t[0] >> 16;
    vm->sm_major = (t[0] >> 4) & 0xFu;
    vm->sm_minor = t[0] & 0xFu;

    /* Pass 0 counts instructions, pass 1 decodes them. */
    for (pass = 0; pass < 2 && rc == PCPS4_VM_OK; ++pass) {
        pos = 2;
        ii = 0;
        while (pos < n && rc == PCPS4_VM_OK) {
            uint32_t tok = t[pos];
            uint32_t op = tok & 0x7FFu;
            uint32_t len = (tok >> 24) & 0x7Fu;
            uint32_t p;
            pcps4_vm_instr_t *ins;

            if (op == OP_CUSTOMDATA) {
                if (pos + 1 >= n) { rc = PCPS4_VM_ERR_FORMAT; break; }
                len = t[pos + 1];
                if (len < 2 || pos + len > n) { rc = PCPS4_VM_ERR_FORMAT; break; }
                if (pass == 1 && ((tok >> 11) & 0x1FFFFFu) == 3u) {
                    uint32_t cnt = (len - 2) / 4, i;
                    if (cnt > PCPS4_VM_MAX_ICB) { rc = PCPS4_VM_ERR_LIMIT; break; }
                    for (i = 0; i < cnt * 4; ++i)
                        vm->icb[i / 4].u[i % 4] = t[pos + 2 + i];
                    vm->icb_count = cnt;
                }
                pos += len;
                continue;
            }
            if (len == 0 || pos + len > n) { rc = PCPS4_VM_ERR_FORMAT; break; }

            if (op >= OP_FIRST_DECL && op != OP_RCP) {
                if (pass == 1) {
                    if (op == OP_DCL_TEMPS && len >= 2) {
                        vm->temp_count = t[pos + 1];
                        if (vm->temp_count > PCPS4_VM_MAX_TEMPS)
                            rc = PCPS4_VM_ERR_LIMIT;
                    }
                }
                pos += len;
                continue;
            }

            if (pass == 0) {
                ++count;
                pos += len;
                continue;
            }

            ins = &vm->instrs[ii];
            ins->op = (uint16_t)op;
            ins->saturate = (uint8_t)((tok >> 13) & 1u);
            ins->test_nz = (uint8_t)((tok >> 18) & 1u);

            p = pos + 1;
            if (tok & 0x80000000u) {
                uint32_t ext;
                do {
                    if (p >= pos + len) { rc = PCPS4_VM_ERR_FORMAT; break; }
                    ext = t[p++];
                } while (ext & 0x80000000u);
                if (rc != PCPS4_VM_OK) break;
            }
            while (p < pos + len) {
                uint32_t used;
                if (ins->nops >= MAX_OPERANDS) { rc = PCPS4_VM_ERR_UNSUPPORTED; break; }
                used = parse_operand(t + p, pos + len - p,
                                     &ins->o[ins->nops], 0);
                if (used == 0) { rc = PCPS4_VM_ERR_UNSUPPORTED; break; }
                ++ins->nops;
                p += used;
            }
            if (rc != PCPS4_VM_OK)
                break;

            /* Control-flow targets. */
            switch (op) {
            case OP_IF:
            case OP_LOOP:
                if (sp >= MAX_NEST) { rc = PCPS4_VM_ERR_LIMIT; break; }
                stack[sp].is_loop = (uint8_t)(op == OP_LOOP);
                stack[sp].pos = ii;
                stack[sp].has_else = 0;
                stack[sp].id = op == OP_LOOP ? loop_id++ : 0;
                ++sp;
                break;
            case OP_ELSE:
                if (sp == 0 || stack[sp - 1].is_loop) { rc = PCPS4_VM_ERR_FORMAT; break; }
                stack[sp - 1].has_else = 1;
                stack[sp - 1].else_pos = ii;
                vm->instrs[stack[sp - 1].pos].jump = ii + 1;
                break;
            case OP_ENDIF:
                if (sp == 0 || stack[sp - 1].is_loop) { rc = PCPS4_VM_ERR_FORMAT; break; }
                if (stack[sp - 1].has_else)
                    vm->instrs[stack[sp - 1].else_pos].jump = ii;
                else
                    vm->instrs[stack[sp - 1].pos].jump = ii;
                --sp;
                break;
            case OP_BREAK:
            case OP_BREAKC: {
                int k;
                for (k = sp - 1; k >= 0 && !stack[k].is_loop; --k) {}
                if (k < 0 || nbreaks >= MAX_NEST * 4) { rc = PCPS4_VM_ERR_FORMAT; break; }
                breaks[nbreaks].instr = ii;
                breaks[nbreaks].loop = stack[k].id;
                ++nbreaks;
                break;
            }
            case OP_ENDLOOP: {
                uint32_t b;
                if (sp == 0 || !stack[sp - 1].is_loop) { rc = PCPS4_VM_ERR_FORMAT; break; }
                ins->jump = stack[sp - 1].pos + 1;
                for (b = 0; b < nbreaks; ++b)
                    if (breaks[b].loop == stack[sp - 1].id)
                        vm->instrs[breaks[b].instr].jump = ii + 1;
                --sp;
                break;
            }
            default:
                break;
            }
            ++ii;
            pos += len;
        }

        if (pass == 0 && rc == PCPS4_VM_OK) {
            if (count == 0) { rc = PCPS4_VM_ERR_FORMAT; break; }
            vm->instrs = (pcps4_vm_instr_t *)calloc(count, sizeof(*vm->instrs));
            if (vm->instrs == NULL) { rc = PCPS4_VM_ERR_LIMIT; break; }
            vm->instr_count = count;
        }
    }

    if (rc == PCPS4_VM_OK && sp != 0)
        rc = PCPS4_VM_ERR_FORMAT;
    if (rc != PCPS4_VM_OK) {
        pcps4_vm_destroy(vm);
        if (status) *status = rc;
        return NULL;
    }
    if (status) *status = PCPS4_VM_OK;
    return vm;
}

void pcps4_vm_destroy(pcps4_vm_t *vm)
{
    if (vm == NULL)
        return;
    free(vm->instrs);
    free(vm);
}

void pcps4_vm_reset_io(pcps4_vm_t *vm)
{
    if (vm == NULL)
        return;
    memset(vm->in, 0, sizeof(vm->in));
    memset(vm->out, 0, sizeof(vm->out));
    memset(vm->cb, 0, sizeof(vm->cb));
    memset(vm->cb_size, 0, sizeof(vm->cb_size));
    vm->depth = 0.0f;
    vm->discarded = 0;
}

/* --- execution ---------------------------------------------------------- */

static uint32_t rel_index(const pcps4_vm_t *vm, const vm_operand_t *o, uint32_t d)
{
    uint32_t v = o->idx[d];
    if (o->rel_present[d] && o->rel_reg[d] < PCPS4_VM_MAX_TEMPS)
        v += vm->r[o->rel_reg[d]].u[o->rel_comp[d]];
    return v;
}

static int fetch_raw(pcps4_vm_t *vm, const vm_operand_t *o, pcps4_vm_reg_t *out)
{
    uint32_t i0, i1;

    switch (o->type) {
    case REG_TEMP:
        i0 = rel_index(vm, o, 0);
        if (i0 >= PCPS4_VM_MAX_TEMPS) return -1;
        *out = vm->r[i0];
        return 0;
    case REG_INPUT:
        i0 = rel_index(vm, o, 0);
        if (i0 >= PCPS4_VM_MAX_IO) return -1;
        *out = vm->in[i0];
        return 0;
    case REG_OUTPUT:
        i0 = rel_index(vm, o, 0);
        if (i0 >= PCPS4_VM_MAX_IO) return -1;
        *out = vm->out[i0];
        return 0;
    case REG_XTEMP:
        i0 = o->idx[0];
        i1 = rel_index(vm, o, 1);
        if (i0 >= PCPS4_VM_MAX_XTEMP_SLOTS || i1 >= PCPS4_VM_MAX_XTEMP) return -1;
        *out = vm->xr[i0][i1];
        return 0;
    case REG_IMM32:
        memcpy(out->u, o->imm, sizeof(o->imm));
        return 0;
    case REG_CBV:
        i0 = o->idx[0];
        i1 = rel_index(vm, o, 1);
        if (i0 >= PCPS4_VM_MAX_CB || vm->cb[i0] == NULL || i1 >= vm->cb_size[i0])
            return -1;
        *out = vm->cb[i0][i1];
        return 0;
    case REG_ICB:
        i0 = rel_index(vm, o, 0);
        if (i0 >= vm->icb_count) return -1;
        *out = vm->icb[i0];
        return 0;
    default:
        return -1;
    }
}

static int read_src(pcps4_vm_t *vm, const vm_operand_t *o, int int_mode,
                    pcps4_vm_reg_t *out)
{
    pcps4_vm_reg_t raw;
    uint32_t c;

    if (fetch_raw(vm, o, &raw) != 0)
        return -1;

    if (o->comps == 4 && o->mode == MODE_SWIZZLE) {
        for (c = 0; c < 4; ++c)
            out->u[c] = raw.u[(o->sel >> (2 * c)) & 3u];
    } else if (o->comps == 4 && o->mode == MODE_SELECT1) {
        for (c = 0; c < 4; ++c)
            out->u[c] = raw.u[o->sel & 3u];
    } else if (o->comps == 1) {
        for (c = 0; c < 4; ++c)
            out->u[c] = raw.u[0];
    } else {
        *out = raw;
    }

    if (o->modifier & 2u) { /* abs */
        for (c = 0; c < 4; ++c) {
            if (int_mode)
                out->i[c] = out->i[c] < 0 ? -out->i[c] : out->i[c];
            else
                out->u[c] &= 0x7FFFFFFFu;
        }
    }
    if (o->modifier & 1u) { /* neg */
        for (c = 0; c < 4; ++c) {
            if (int_mode)
                out->i[c] = (int32_t)(0u - out->u[c]);
            else
                out->u[c] ^= 0x80000000u;
        }
    }
    return 0;
}

static float fclamp01(float x)
{
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

static int write_dst(pcps4_vm_t *vm, const vm_operand_t *o,
                     const pcps4_vm_reg_t *v, int sat)
{
    pcps4_vm_reg_t *dst;
    uint32_t mask, c, i0, i1;

    if (o->type == REG_NULL)
        return 0;
    if (o->type == REG_DEPTH) {
        vm->depth = sat ? fclamp01(v->f[0]) : v->f[0];
        return 0;
    }
    mask = o->mode == MODE_MASK ? (o->sel & 0xFu) : 0xFu;
    switch (o->type) {
    case REG_TEMP:
        i0 = rel_index(vm, o, 0);
        if (i0 >= PCPS4_VM_MAX_TEMPS) return -1;
        dst = &vm->r[i0];
        break;
    case REG_OUTPUT:
        i0 = rel_index(vm, o, 0);
        if (i0 >= PCPS4_VM_MAX_IO) return -1;
        dst = &vm->out[i0];
        break;
    case REG_XTEMP:
        i0 = o->idx[0];
        i1 = rel_index(vm, o, 1);
        if (i0 >= PCPS4_VM_MAX_XTEMP_SLOTS || i1 >= PCPS4_VM_MAX_XTEMP) return -1;
        dst = &vm->xr[i0][i1];
        break;
    default:
        return -1;
    }
    for (c = 0; c < 4; ++c)
        if (mask & (1u << c))
            dst->f[c] = sat ? fclamp01(v->f[c]) : v->f[c];
    return 0;
}

/* Writes a result that is not float data (never saturated). */
static int write_raw(pcps4_vm_t *vm, const vm_operand_t *o, const pcps4_vm_reg_t *v)
{
    return write_dst(vm, o, v, 0);
}

static int cond_true(const pcps4_vm_instr_t *ins, const pcps4_vm_reg_t *s)
{
    return (s->u[0] != 0) == (ins->test_nz != 0);
}

#define FLOP2(expr) do { for (c = 0; c < 4; ++c) { float a = s[0].f[c], b = s[1].f[c]; (void)a; (void)b; d.f[c] = (expr); } } while (0)
#define FLOP1(expr) do { for (c = 0; c < 4; ++c) { float a = s[0].f[c]; d.f[c] = (expr); } } while (0)
#define CMPF(expr) do { for (c = 0; c < 4; ++c) { float a = s[0].f[c], b = s[1].f[c]; d.u[c] = (expr) ? 0xFFFFFFFFu : 0u; } } while (0)
#define CMPI(expr) do { for (c = 0; c < 4; ++c) { int32_t a = s[0].i[c], b = s[1].i[c]; d.u[c] = (expr) ? 0xFFFFFFFFu : 0u; } } while (0)
#define CMPU(expr) do { for (c = 0; c < 4; ++c) { uint32_t a = s[0].u[c], b = s[1].u[c]; d.u[c] = (expr) ? 0xFFFFFFFFu : 0u; } } while (0)
#define INT2(expr) do { for (c = 0; c < 4; ++c) { int32_t a = s[0].i[c], b = s[1].i[c]; d.i[c] = (expr); } } while (0)
#define UINT2(expr) do { for (c = 0; c < 4; ++c) { uint32_t a = s[0].u[c], b = s[1].u[c]; d.u[c] = (expr); } } while (0)

static float vm_floor(float x)
{
    float t = (float)(int32_t)x;
    if (x > 2147483520.0f || x < -2147483520.0f)
        return x;
    return (t > x) ? t - 1.0f : t;
}

static float vm_trunc(float x)
{
    if (x > 2147483520.0f || x < -2147483520.0f)
        return x;
    return (float)(int32_t)x;
}

static float vm_round_ne(float x)
{
    float f = vm_floor(x), diff = x - f;
    if (diff < 0.5f) return f;
    if (diff > 0.5f) return f + 1.0f;
    return (((int32_t)f) & 1) ? f + 1.0f : f;
}

static int32_t vm_ftoi(float x)
{
    if (x != x) return 0;
    if (x >= 2147483648.0f) return 2147483647;
    if (x <= -2147483648.0f) return (int32_t)0x80000000u;
    return (int32_t)x;
}

static uint32_t vm_ftou(float x)
{
    if (x != x || x <= 0.0f) return 0;
    if (x >= 4294967296.0f) return 0xFFFFFFFFu;
    return (uint32_t)x;
}

int pcps4_vm_run(pcps4_vm_t *vm)
{
    uint32_t pc = 0, steps = 0, c;

    if (vm == NULL || vm->instrs == NULL)
        return PCPS4_VM_ERR_ARGUMENT;

    memset(vm->r, 0, sizeof(vm->r));
    memset(vm->xr, 0, sizeof(vm->xr));
    memset(vm->out, 0, sizeof(vm->out));
    vm->discarded = 0;
    vm->bad_opcode = 0;

    while (pc < vm->instr_count) {
        const pcps4_vm_instr_t *ins = &vm->instrs[pc];
        pcps4_vm_reg_t s[5], d, d2;
        int is_int = 0, sat = ins->saturate;
        uint32_t next = pc + 1;
        int ok = 0;

        if (++steps > MAX_STEPS)
            return PCPS4_VM_ERR_LIMIT;

        memset(&d, 0, sizeof(d));
        memset(&d2, 0, sizeof(d2));

        switch (ins->op) {
        case OP_IADD: case OP_IMUL: case OP_IMAD: case OP_IMAX: case OP_IMIN:
        case OP_INEG: case OP_ISHL: case OP_ISHR: case OP_IEQ: case OP_IGE:
        case OP_ILT: case OP_INE: case OP_ITOF:
            is_int = 1;
            break;
        default:
            break;
        }

        /* Operand layout: dst0 [dst1] then sources, except the flow and
         * compare-less forms handled below. Read sources lazily per op. */
#define NEED_SRC(k) do { if (read_src(vm, &ins->o[k], is_int, &s[(k) - base]) != 0) return PCPS4_VM_ERR_RUNTIME; } while (0)
        {
            int base = 1;
            switch (ins->op) {
            /* ---- two-destination ops: o0, o1, sources from o2 -------- */
            case OP_IMUL: case OP_UMUL: case OP_UDIV: case OP_SINCOS:
                base = 2; /* two destinations */
                break;
            case OP_SAMPLE: case OP_SAMPLE_L: case OP_SAMPLE_B:
                base = 99; /* resource/sampler operands are not registers */
                break;
            case OP_IF: case OP_BREAKC: case OP_RETC: case OP_DISCARD:
            case OP_CONTINUEC:
                base = 0;
                break;
            default:
                break;
            }
            {
                int k;
                for (k = base; k < ins->nops && (k - base) < 5; ++k)
                    NEED_SRC(k);
            }

            switch (ins->op) {
            case OP_NOP:
            case OP_LOOP:
            case OP_ENDIF:
                break;
            case OP_MOV: d = s[0]; ok = 1; break;
            case OP_MOVC:
                for (c = 0; c < 4; ++c)
                    d.u[c] = s[0].u[c] ? s[1].u[c] : s[2].u[c];
                ok = 1;
                break;
            case OP_ADD: FLOP2(a + b); ok = 1; break;
            case OP_MUL: FLOP2(a * b); ok = 1; break;
            case OP_DIV: FLOP2(a / b); ok = 1; break;
            case OP_MIN: FLOP2(a < b ? a : b); ok = 1; break;
            case OP_MAX: FLOP2(a > b ? a : b); ok = 1; break;
            case OP_MAD:
                for (c = 0; c < 4; ++c)
                    d.f[c] = s[0].f[c] * s[1].f[c] + s[2].f[c];
                ok = 1;
                break;
            case OP_DP2: case OP_DP3: case OP_DP4: {
                uint32_t n = ins->op == OP_DP2 ? 2 : (ins->op == OP_DP3 ? 3 : 4);
                float sum = 0.0f;
                for (c = 0; c < n; ++c)
                    sum += s[0].f[c] * s[1].f[c];
                for (c = 0; c < 4; ++c)
                    d.f[c] = sum;
                ok = 1;
                break;
            }
            case OP_SQRT: FLOP1(a <= 0.0f ? 0.0f : __builtin_sqrtf(a)); ok = 1; break;
            case OP_RSQ: FLOP1(a <= 0.0f ? 0.0f : 1.0f / __builtin_sqrtf(a)); ok = 1; break;
            case OP_RCP: FLOP1(1.0f / a); ok = 1; break;
            case OP_FRC: FLOP1(a - vm_floor(a)); ok = 1; break;
            case OP_ROUND_NE: FLOP1(vm_round_ne(a)); ok = 1; break;
            case OP_ROUND_NI: FLOP1(vm_floor(a)); ok = 1; break;
            case OP_ROUND_PI: FLOP1(-vm_floor(-a)); ok = 1; break;
            case OP_ROUND_Z: FLOP1(vm_trunc(a)); ok = 1; break;
            case OP_EXP: FLOP1(__builtin_exp2f(a)); ok = 1; break;
            case OP_LOG: FLOP1(__builtin_log2f(a)); ok = 1; break;
            case OP_DERIV_RTX: case OP_DERIV_RTY:
                /* No neighbouring pixels in a scalar interpreter. */
                ok = 1;
                break;
            case OP_EQ: CMPF(a == b); ok = 1; break;
            case OP_NE: CMPF(a != b); ok = 1; break;
            case OP_LT: CMPF(a < b); ok = 1; break;
            case OP_GE: CMPF(a >= b); ok = 1; break;
            case OP_IEQ: CMPI(a == b); ok = 1; break;
            case OP_INE: CMPI(a != b); ok = 1; break;
            case OP_ILT: CMPI(a < b); ok = 1; break;
            case OP_IGE: CMPI(a >= b); ok = 1; break;
            case OP_ULT: CMPU(a < b); ok = 1; break;
            case OP_UGE: CMPU(a >= b); ok = 1; break;
            case OP_IADD: UINT2(a + b); ok = 1; break;
            case OP_IMAX: INT2(a > b ? a : b); ok = 1; break;
            case OP_IMIN: INT2(a < b ? a : b); ok = 1; break;
            case OP_UMAX: UINT2(a > b ? a : b); ok = 1; break;
            case OP_UMIN: UINT2(a < b ? a : b); ok = 1; break;
            case OP_IMAD:
                for (c = 0; c < 4; ++c)
                    d.u[c] = s[0].u[c] * s[1].u[c] + s[2].u[c];
                ok = 1;
                break;
            case OP_UMAD:
                for (c = 0; c < 4; ++c)
                    d.u[c] = s[0].u[c] * s[1].u[c] + s[2].u[c];
                ok = 1;
                break;
            case OP_INEG:
                for (c = 0; c < 4; ++c)
                    d.u[c] = 0u - s[0].u[c];
                ok = 1;
                break;
            case OP_ISHL: UINT2(a << (b & 31u)); ok = 1; break;
            case OP_ISHR: INT2(a >> (b & 31)); ok = 1; break;
            case OP_USHR: UINT2(a >> (b & 31u)); ok = 1; break;
            case OP_AND: UINT2(a & b); ok = 1; break;
            case OP_OR: UINT2(a | b); ok = 1; break;
            case OP_XOR: UINT2(a ^ b); ok = 1; break;
            case OP_NOT:
                for (c = 0; c < 4; ++c)
                    d.u[c] = ~s[0].u[c];
                ok = 1;
                break;
            case OP_FTOI:
                for (c = 0; c < 4; ++c)
                    d.i[c] = vm_ftoi(s[0].f[c]);
                ok = 1;
                break;
            case OP_FTOU:
                for (c = 0; c < 4; ++c)
                    d.u[c] = vm_ftou(s[0].f[c]);
                ok = 1;
                break;
            case OP_ITOF:
                for (c = 0; c < 4; ++c)
                    d.f[c] = (float)s[0].i[c];
                ok = 1;
                break;
            case OP_UTOF:
                for (c = 0; c < 4; ++c)
                    d.f[c] = (float)s[0].u[c];
                ok = 1;
                break;
            case OP_IMUL: case OP_UMUL:
                for (c = 0; c < 4; ++c) {
                    if (ins->op == OP_IMUL) {
                        int64_t p = (int64_t)s[0].i[c] * (int64_t)s[1].i[c];
                        d.u[c] = (uint32_t)((uint64_t)p >> 32);
                        d2.u[c] = (uint32_t)((uint64_t)p & 0xFFFFFFFFu);
                    } else {
                        uint64_t p = (uint64_t)s[0].u[c] * (uint64_t)s[1].u[c];
                        d.u[c] = (uint32_t)(p >> 32);
                        d2.u[c] = (uint32_t)(p & 0xFFFFFFFFu);
                    }
                }
                if (write_raw(vm, &ins->o[0], &d) != 0 ||
                    write_raw(vm, &ins->o[1], &d2) != 0)
                    return PCPS4_VM_ERR_RUNTIME;
                break;
            case OP_UDIV:
                for (c = 0; c < 4; ++c) {
                    if (s[1].u[c] == 0) {
                        d.u[c] = 0xFFFFFFFFu;
                        d2.u[c] = 0xFFFFFFFFu;
                    } else {
                        d.u[c] = s[0].u[c] / s[1].u[c];
                        d2.u[c] = s[0].u[c] % s[1].u[c];
                    }
                }
                if (write_raw(vm, &ins->o[0], &d) != 0 ||
                    write_raw(vm, &ins->o[1], &d2) != 0)
                    return PCPS4_VM_ERR_RUNTIME;
                break;
            case OP_SINCOS:
                for (c = 0; c < 4; ++c) {
                    d.f[c] = __builtin_sinf(s[0].f[c]);
                    d2.f[c] = __builtin_cosf(s[0].f[c]);
                }
                if (write_dst(vm, &ins->o[0], &d, sat) != 0 ||
                    write_dst(vm, &ins->o[1], &d2, sat) != 0)
                    return PCPS4_VM_ERR_RUNTIME;
                break;

            case OP_SAMPLE: case OP_SAMPLE_L: case OP_SAMPLE_B: {
                /* o0 dst, o1 address, o2 resource, o3 sampler, [o4 lod/bias] */
                float tex[4] = {0, 0, 0, 0}, param = 0.0f;
                uint32_t tex_slot = ins->o[2].idx[0], smp_slot = ins->o[3].idx[0];
                pcps4_vm_reg_t addr, lodv;
                int mode = ins->op == OP_SAMPLE ? PCPS4_VM_SAMPLE_IMPLICIT
                         : (ins->op == OP_SAMPLE_L ? PCPS4_VM_SAMPLE_LOD
                                                   : PCPS4_VM_SAMPLE_BIAS);
                if (ins->nops < 4 || read_src(vm, &ins->o[1], 0, &addr) != 0)
                    return PCPS4_VM_ERR_RUNTIME;
                if (ins->nops >= 5 && read_src(vm, &ins->o[4], 0, &lodv) == 0)
                    param = lodv.f[0];
                if (vm->sample != NULL)
                    vm->sample(vm->sample_user, tex_slot, smp_slot,
                               addr.f, param, mode, tex);
                for (c = 0; c < 4; ++c)
                    d.f[c] = tex[(ins->o[2].sel >> (2 * c)) & 3u];
                if (write_dst(vm, &ins->o[0], &d, sat) != 0)
                    return PCPS4_VM_ERR_RUNTIME;
                break;
            }

            /* ---- control flow ---------------------------------------- */
            case OP_IF:
                if (!cond_true(ins, &s[0]))
                    next = ins->jump;
                break;
            case OP_ELSE:
                next = ins->jump;
                break;
            case OP_ENDLOOP:
                next = ins->jump;
                break;
            case OP_BREAK:
                next = ins->jump;
                break;
            case OP_BREAKC:
                if (cond_true(ins, &s[0]))
                    next = ins->jump;
                break;
            case OP_RET:
                return PCPS4_VM_OK;
            case OP_RETC:
                if (cond_true(ins, &s[0]))
                    return PCPS4_VM_OK;
                break;
            case OP_DISCARD:
                if (cond_true(ins, &s[0])) {
                    vm->discarded = 1;
                    return PCPS4_VM_OK;
                }
                break;
            default:
                vm->bad_opcode = ins->op;
                return PCPS4_VM_ERR_UNSUPPORTED;
            }

            if (ok) {
                if (write_dst(vm, &ins->o[0], &d, sat) != 0)
                    return PCPS4_VM_ERR_RUNTIME;
            }
        }
#undef NEED_SRC
        pc = next;
    }
    return PCPS4_VM_OK;
}
