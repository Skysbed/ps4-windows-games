#ifndef _PCPS4_MACHINE_SYSARCH_H
#define _PCPS4_MACHINE_SYSARCH_H
#define I386_GET_LDT 0
#define I386_SET_LDT 1
#define AMD64_GET_FSBASE 128
#define AMD64_SET_FSBASE 129
#define AMD64_GET_GSBASE 130
#define AMD64_SET_GSBASE 131
struct i386_ldt_args { unsigned int start; void *descs; unsigned int num; };
#ifdef __cplusplus
extern "C" {
#endif
int sysarch(int, void *);
#ifdef __cplusplus
}
#endif
#endif
#ifndef _PCPS4_AMD64_BASE_HELPERS
#define _PCPS4_AMD64_BASE_HELPERS
static inline int amd64_set_fsbase(void *b){ return sysarch(AMD64_SET_FSBASE,&b);}
static inline int amd64_get_fsbase(void *b){ return sysarch(AMD64_GET_FSBASE,b);}
static inline int amd64_set_gsbase(void *b){ return sysarch(AMD64_SET_GSBASE,&b);}
static inline int amd64_get_gsbase(void *b){ return sysarch(AMD64_GET_GSBASE,b);}
#endif
