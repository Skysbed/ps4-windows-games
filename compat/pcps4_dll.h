#ifndef PCPS4_DLL_H
#define PCPS4_DLL_H

#include <stddef.h>
#include <stdint.h>

#include "pcps4_pe.h"

#ifdef __cplusplus
extern "C" {
#endif

void *pcps4_dll_load(
    const char *path,
    const pcps4_pe_resolver *resolver);

void *pcps4_dll_find(const char *name);

void *pcps4_dll_get_export(
    void *module,
    const char *name,
    uint16_t ordinal);

int pcps4_dll_free(void *module);

int pcps4_dll_is_module(void *module);

#ifdef __cplusplus
}
#endif

#endif
