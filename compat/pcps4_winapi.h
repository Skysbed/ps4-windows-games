#ifndef PCPS4_WINAPI_H
#define PCPS4_WINAPI_H

#include "pcps4_pe.h"

typedef struct pcps4_module_loader pcps4_module_loader;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Build the resolver used by the optional binary-execution path.
 *
 * The resolver intentionally exposes only APIs that have concrete PcPs4
 * implementations. Unsupported imports remain unresolved and are reported
 * by the loader before control is transferred to the Windows binary.
 */
void pcps4_winapi_build_resolver(pcps4_pe_resolver *resolver);
void pcps4_winapi_set_module_loader(pcps4_module_loader *loader);
void pcps4_winapi_set_main_module(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* PCPS4_WINAPI_H */
