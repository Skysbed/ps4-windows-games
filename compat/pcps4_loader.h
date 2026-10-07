#ifndef PCPS4_LOADER_H
#define PCPS4_LOADER_H

/*
 * PcPs4 PE executor (PS4 target).
 *
 * This is stage 4 of running a Windows binary: take a parsed+mapped PE image,
 * place it in executable memory on the PS4, and transfer control to its entry
 * point. It uses the OpenOrbis kernel memory APIs (sceKernelMmap /
 * sceKernelMprotect). Making RW memory executable requires a console that
 * permits it (homebrew / kernel access); on retail firmware the protection
 * change is refused and this reports an error rather than forcing anything.
 *
 * IMPORTANT: executing a real game's entry point also requires the Windows
 * runtime (Win32 + DirectX) its imports reference. This executor only runs
 * code whose imports are all resolved by the supplied resolver; the built-in
 * smoke test uses a self-contained binary with no imports to prove the
 * load-and-execute path itself works on hardware.
 */

#include <stddef.h>
#include <stdint.h>

#include "pcps4_pe.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum pcps4_loader_status {
    PCPS4_LOADER_OK = 0,
    PCPS4_LOADER_ERR_ALLOC = -1,     /* could not allocate image memory */
    PCPS4_LOADER_ERR_LOAD = -2,      /* parse/map/bind failed */
    PCPS4_LOADER_ERR_PROTECT = -3,   /* could not make memory executable */
    PCPS4_LOADER_ERR_UNRESOLVED = -4,/* binary needs APIs we don't provide */
    PCPS4_LOADER_ERR_NULL = -5,
    PCPS4_LOADER_ERR_PROCESS_EXIT = -6
} pcps4_loader_status;

/*
 * Load `exe` into fresh executable memory and call its entry point as
 * `int entry(void)`. Refuses to run if any import is unresolved (so we never
 * jump into code that will immediately call a missing API). On success the
 * entry's return value is written to `out_entry_ret`.
 *
 * The memory is left mapped (the entry may still reference it); this is a
 * smoke/bring-up primitive, not a process manager.
 */
pcps4_loader_status pcps4_loader_run(const uint8_t *exe, size_t exe_size,
                                     const pcps4_pe_resolver *resolver,
                                     int32_t *out_entry_ret);

/*
 * Windows ExitProcess: unwind back to pcps4_loader_run instead of killing
 * the whole PcPs4 process. Safe to call from an MS-ABI PE import.
 */
void pcps4_pe_exit_process(uint32_t code);

/*
 * Load a PE32+ DLL from a PS4 filesystem path. Dependencies are resolved
 * lazily through the supplied resolver. Successfully loaded modules remain
 * mapped for the life of the process so exported function addresses stay
 * valid for running game threads.
 */
void *pcps4_loader_load_library(
    const char *path,
    const pcps4_pe_resolver *resolver);

/* Find an already-loaded module by Windows-style module name. */
void *pcps4_loader_find_module(const char *name);

/* Resolve an export from a DLL loaded by pcps4_loader_load_library(). */
void *pcps4_loader_get_export(
    void *module,
    const char *name,
    uint16_t ordinal);

/* Release a module reference without unmapping live code. */
int pcps4_loader_free_library(void *module);

/* True when a module is a PcPs4-loaded Windows PE module base. */
int pcps4_loader_is_module(void *module);

/* Shared PE image-finalization helpers used by the DLL loader. */
int pcps4_loader_apply_image_protections(
    const pcps4_pe_image *image,
    uint8_t *mem,
    size_t map_size);
int pcps4_loader_run_tls_callbacks(
    const pcps4_pe_image *image,
    uint8_t *mem,
    size_t map_size);
int pcps4_loader_entry_is_executable(
    const pcps4_pe_image *image);

/*
 * Self-contained hardware smoke test: builds a tiny PE whose entry is
 * `mov eax, 0x1234; ret`, runs it through pcps4_loader_run, and verifies the
 * returned value. Prints what happened. Returns 0 on success, non-zero on
 * failure. Safe to call on any console: a refused protection change is
 * reported, not forced.
 */
int pcps4_pe_exec_smoke(void);

const char *pcps4_loader_status_str(pcps4_loader_status status);

#ifdef __cplusplus
}
#endif

#endif /* PCPS4_LOADER_H */
