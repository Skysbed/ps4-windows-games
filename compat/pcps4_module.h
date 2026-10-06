#ifndef PCPS4_MODULE_H
#define PCPS4_MODULE_H

#include <stddef.h>
#include <stdint.h>

#include "pcps4_pe.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PCPS4_MODULE_MAX_COUNT 32u

typedef enum pcps4_module_status {
    PCPS4_MODULE_OK = 0,
    PCPS4_MODULE_NOT_FOUND = 1,
    PCPS4_MODULE_ERR_NULL = -1,
    PCPS4_MODULE_ERR_IO = -2,
    PCPS4_MODULE_ERR_PARSE = -3,
    PCPS4_MODULE_ERR_ALLOC = -4,
    PCPS4_MODULE_ERR_IMPORTS = -5,
    PCPS4_MODULE_ERR_PROTECT = -6,
    PCPS4_MODULE_ERR_INIT = -7
} pcps4_module_status;

typedef struct pcps4_loaded_module {
    char name[64];
    uint8_t *memory;
    size_t map_size;
    pcps4_pe_image image;
    uint8_t loading;
    uint8_t loaded;
} pcps4_loaded_module;

typedef struct pcps4_module_loader {
    pcps4_pe_resolver *resolver;
    pcps4_loaded_module modules[PCPS4_MODULE_MAX_COUNT];
    uint32_t count;
} pcps4_module_loader;

void pcps4_module_loader_init(
    pcps4_module_loader *loader, pcps4_pe_resolver *resolver);

/*
 * Load a DLL from the bundled /app0/game directory. DLL names are matched
 * case-insensitively like Windows. A package-local dependency is recursively
 * loaded before its imports are bound.
 */
pcps4_module_status pcps4_module_loader_load(
    pcps4_module_loader *loader, const char *name);

/*
 * Scan an already-parsed PE's imports and recursively preload package-local
 * DLLs that are not already supplied by the system resolver. Missing local
 * files are deliberately ignored so the main PE loader can report those
 * imports as unresolved.
 */
pcps4_module_status pcps4_module_loader_preload_imports(
    pcps4_module_loader *loader, const pcps4_pe_image *image);

/* Find an already-loaded package-local module by its Windows DLL name. */
const pcps4_loaded_module *pcps4_module_loader_find(
    const pcps4_module_loader *loader, const char *name);

const char *pcps4_module_status_str(pcps4_module_status status);

#ifdef __cplusplus
}
#endif

#endif
