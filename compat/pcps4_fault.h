#ifndef PCPS4_FAULT_H
#define PCPS4_FAULT_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Installs the PcPs4 user-space fault boundary.
 *
 * Fatal process faults are intentionally not recovered from. Continuing
 * after stack/memory corruption is less safe than terminating the app.
 */
int pcps4_fault_install(void);

#ifdef __cplusplus
}
#endif

#endif
