# PCPS4 — Windows games on your PS4

**Run PC (Windows) games on a jailbroken PS4.**

PCPS4 is Wine, ported to the PS4's Orbis OS. Orbis is built on FreeBSD 9, so Wine's
FreeBSD side runs nearly unmodified: the loader, ntdll, win32u, and an in-process
wineserver all live inside one PS4 app, talking to the console's GNM GPU through
RADV (Mesa's AMD Vulkan driver) behind a minimal native layer.

Everything here is **early, experimental homebrew**. Today, on real hardware,
**a Windows Direct3D 11 program draws a shaded triangle on the television**: Wine
boots, its in-process wineserver handshake completes, a Windows x64 executable and its
DLL import closure load and initialise (`kernel32`, `user32`, `gdi32`, `d3d11`, `dxgi`,
`wined3d`, …), Wine runs multiple Windows threads, `wined3d` creates a Vulkan device
on the console's GPU through RADV, translates the program's DXBC shaders, and presents
1920x1080 frames through `sceVideoOut`. The process then shuts down cleanly. This is a
small test program — **no commercial game has been tried yet**; input, audio, and a
broader set of DLLs are still to come.
Follow development in [WINE_PS4_STATUS.md](WINE_PS4_STATUS.md), which documents every
milestone, the kernel findings behind them, and the current blocker in detail.

## What this repository contains

| Path | What it is |
|---|---|
| `wine-ps4-all-changes.patch` | The complete diff against upstream Wine that makes it build and run for PS4 (113 files; includes bring-up instrumentation) |
| `build-ps4-module/` | Per-module build pipeline: linker script (16 KiB alignment), TLS stripping, PRX descriptor generation, packaging manifest, a stub Unix half for `opengl32`, the RADV/`win32u`/`winevulkan` link shims, an import-tag verifier, and a build-host HLSL compiler helper |
| `eboot-src/` | The tiny bootstrap that loads `ntdll.prx` and calls `__wine_main` |
| `wine-ps4-shims/` | Headers and shims for the gaps between Wine and OrbisOS/FreeBSD 9 |
| `WINE_PS4_STATUS.md` | Build recipe + status; start here to understand the port |
| `BUILD.md` | Step-by-step guide to build the whole thing from source |

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
8. **Graphics: Vulkan renderer selection, `win32u` Unix half, RADV** — current work
9. Ship a prepared Wine prefix (the console cannot create one — process creation is
   unavailable), so the runtime can leave bootstrap mode
10. A native presentation/input layer so frames can reach the screen, then real games.

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
