#ifndef PCPS4_GAME_H
#define PCPS4_GAME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Load a Windows PE32+ executable from the PS4 filesystem, bind the built-in
 * PcPs4 Win32/CRT import table, and transfer control to its PE entry point.
 * The binary path is expected to point at the game's own executable.
 */
int pcps4_game_run_file(const char *path, int32_t *out_entry_ret);

/* Convenience entry point for the package's staged /app0/game/game.exe. */
int pcps4_game_run_bundled(int32_t *out_entry_ret);

#if PCPS4_DXVK
/* Validate pixels emitted by the current DXVK run, not just PE exit status. */
int pcps4_game_validate_dxvk_scanout(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PCPS4_GAME_H */
