# PCPS4 — Windows games on your PS4

**Run PC (Windows) games on a jailbroken PS4.**

PCPS4 is Wine, ported to the PS4's Orbis OS. Orbis is built on FreeBSD 9, so Wine's
FreeBSD side runs nearly unmodified: the loader, ntdll, win32u, and an in-process
wineserver all live inside one PS4 app, talking to the console's GNM GPU through
RADV (Mesa's AMD Vulkan driver) behind a minimal native layer.

Everything here is **early, experimental homebrew**. Today, on real hardware:

- **Minecraft: Java Edition 26.3 is playable** — Java 25 (Temurin) under Wine, the game's
  own **Vulkan renderer** on RADV, about 60 FPS in a world at 1920x1080, with the
  controller mapped to keyboard and mouse, and saving worlds. Chunk generation still
  causes short hitches.
- **SuperTuxKart 1.5** (official 64-bit Windows build, unmodified) is playable on Vulkan,
  and on Direct3D 9 through DXVK.
- **Direct3D 8/9/10/11 through DXVK 3.1** and **OpenGL 4.6 through Mesa's Zink** draw on
  the television.
- A console-style **launcher** lists the installed games, and every title closes
  cleanly back to it.

- **Horizon** (an unofficial Rust rewrite of Minecraft 26.3, run from its Windows build) is playable:
  real controller support, worlds that save, and a ~40 s load thanks to a RAM preload layer.
- **Audio** through the PS4's `sceAudioOut` (a Wine audio driver adapted from prospero-win's).
- **Real gamepad**: the DualShock 4 appears as an Xbox controller to XInput and
  Windows.Gaming.Input, alongside the optional keyboard/mouse mapping.
- **Networking**: DNS through Sony's resolver, TLS through a GnuTLS build for the PS4.

It is far from finished: Minecraft needs an offline account, TLS handshakes are slow, and
PS4 Pro mode is experimental.
Follow development in [WINE_PS4_STATUS.md](WINE_PS4_STATUS.md), which documents every
milestone, the kernel findings behind them, and the current blocker in detail.

## What this repository contains

| Path | What it is |
|---|---|
| `wine-ps4-all-changes.patch` | The complete diff against upstream Wine that makes it build and run for PS4 (133 files; includes bring-up instrumentation) |
| `mesa-radv-orbis-changes.patch` | Changes to the RADV ORBIS Mesa tree (base commit `1a59cc1`): splitting submissions larger than one console IB, video-out handover between swapchains, BGRA scan-out, copy-mode scan-out without overwriting queued frames, a bounded image acquire |
| `build-ps4-module/` | Per-module build pipeline: linker script (16 KiB alignment), TLS stripping, PRX descriptor generation, packaging manifest, the RADV/`win32u`/`winevulkan` link shims, an import-tag verifier, the `scePad` XInput module, the controller-as-keyboard/mouse module, the kernel-pipe fallback, the GPU submit keepalive, the PS4 direct-memory backend, and the module build scripts |
| `eboot-src/` | The bootstrap: runs the launcher, then loads `ntdll.prx` (from `/data/pcps4` if present) and calls `__wine_main`; returns to the launcher when a game exits |
| `launcher/` | The native PS4 game launcher (software-rendered to video-out, `scePad` input); `gen_font.py` builds its font header |
| `tools/` | On-console test programs (filesystem, clock and wait accuracy, OpenGL), a Java thread-dump agent, and the Minecraft download/arguments scripts |
| `wine-ps4-shims/` | Headers and shims for the gaps between Wine and OrbisOS/FreeBSD 9 |
| `WINE_PS4_STATUS.md` | Build recipe + status; start here to understand the port |
| `BUILD.md` | Step-by-step guide to build the whole thing from source |
| `tests/` | On-console tests and benchmarks for the driver and platform code |
| `CHANGES_2026-11-21.md` | Latest driver fixes, measurements and known problems |

## How it works

A PS4 app boots a small `eboot` that loads Wine's `ntdll` as a PS4 PRX module.
PRX modules can't carry TLS and must be 16 KiB page-aligned, so the build patches
the ELF: TLS is stripped and locale is served from a process-global shim. Wine
then creates its prefix on writable storage (`/data/wine`), starts the
wineserver **in-process** (the console forbids forks), and programs the console's
GPU through RADV with a GNM submission backend instead of Linux's amdgpu.

## Requirements

- A **jailbroken PS4** ([GoldHEN](https://github.com/GoldHEN/GoldHEN)) with the
  FTP and klog plugins enabled.
- Build tools on a host machine: the OpenOrbis toolchain, orbis-compat, and
  host Wine build tools (see `WINE_PS4_STATUS.md` for the exact recipe).
- The console address is read from `PCPS4_CONSOLE_IP` at runtime; nothing here
  hardcodes an address:

  ```bash
  export PCPS4_CONSOLE_IP=<the PS4's address on your local network>
  python3 build-ps4-module/klog_capture.py klog.txt 120
  ```

## Status and roadmap

**Want to run it on a console?** See [INSTALL.md](INSTALL.md) for installing
the current build and collecting diagnostic logs.

1. ~~Wine Unix-side builds for PS4~~ ✅
2. ~~Module pipeline (PRX, TLS, alignment, descriptors)~~ ✅
3. ~~Runtime boots on hardware; prefix created; wineserver loads~~ ✅
4. ~~In-process wineserver connects; NLS loads; `SCM_RIGHTS` fd-passing replaced
   with in-process descriptors; client/server handshake works~~ ✅
5. ~~Wine's first server request (`init_first_thread`) completes on hardware;
   process and first thread created~~ ✅
6. ~~Process-parameter setup; the Windows executable and the PE `ntdll.dll` are
   mapped; Windows syscalls dispatch into Wine; the PE loader loads DLLs~~ ✅
7. ~~The program's full import closure loads and initialises; a Windows program
   runs to `ExitProcess`~~ ✅
8. ~~Graphics: Vulkan, Direct3D (wined3d and DXVK) and OpenGL (Zink) reach the screen~~ ✅
9. ~~Real games: SuperTuxKart, then Minecraft: Java Edition on Vulkan~~ ✅
10. ~~Launcher, controller input, clean exit back to the launcher~~ ✅
11. ~~Audio, real gamepad, networking (DNS + TLS)~~ ✅
12. **PS4 Pro mode** (driver describes the Pro GPU in NEO mode) — in testing
13. Faster TLS, fewer frame-time spikes, 32-bit (WoW64) games, Steam integration

## Credits and standing on shoulders

This project is assembled from, and impossible without, these projects:

- [Wine](https://gitlab.winehq.org/wine/wine) (LGPL-2.1-or-later) — the whole
  Windows compatibility core
- [Mesa](https://gitlab.freedesktop.org/mesa/mesa) — its RADV driver is the GPU
  backend behind the libvulkan bridge
- [orbis-ports/mesa-ps4](https://github.com/orbis-ports/mesa-ps4) — the
  reference for cross-building Mesa for Orbis OS
- [orbis-ports/orbis-compat](https://github.com/orbis-ports/orbis-compat) and
  [orbis-ports/orbis-porting-kit](https://github.com/orbis-ports/orbis-porting-kit)
  — the FreeBSD-9 libc compatibility layer every PS4 module here links against
- [OpenOrbis PS4 Toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain)
  — the homebrew SDK (linker scripts, `create-fself`, `create-gp4`, PKG tools)
- [PS4-OpenGNM](https://github.com/PS4-OpenGNM) —
  [opengnm](https://github.com/PS4-OpenGNM/opengnm) and
  [opengnm-psbc](https://github.com/PS4-OpenGNM/opengnm-psbc) for GNM command
  submission and the shader compiler; its earlier
  [vulkan-ps4](https://github.com/PS4-OpenGNM/vulkan-ps4) driver taught us most
  of the DXBC/GNM lessons before RADV took over
- [prospero-win](https://github.com/mpereiraesaa/prospero-win) and
  [PS5_Mesa](https://github.com/mpereiraesaa/PS5_Mesa) — the PS5-side ports that
  proved this exact Wine + DXVK + RADV stack on PS5 hardware first; most of the
  in-process server architecture carries over from their work
- [DXVK](https://github.com/doitsujin/dxvk) — D3D→Vulkan layers, as on PC and PS5
- [GoldHEN](https://github.com/GoldHEN/GoldHEN) — the jailbreak environment
  (FTP for package install, klog for kernel-level logs)

### See also

- [playden-ps5](https://github.com/leolobato/playden-ps5) — Steam + Wine on a
  jailbroken PS5, built on the same underlying stack; a PS4 equivalent is the
  gap this project targets.

## License

Wine is LGPL-2.1-or-later; this port preserves it (`LICENSE`). Everything else
here follows the same terms.
