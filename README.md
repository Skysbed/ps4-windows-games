# PCPS4 — Windows games on your PS4

**Run PC (Windows) games on a jailbroken PS4.**

PCPS4 is Wine, ported to the PS4's Orbis OS. Orbis is built on FreeBSD 9, so Wine's
FreeBSD side runs nearly unmodified: the loader, ntdll, win32u, and an in-process
wineserver all live inside one PS4 app, talking to the console's GNM GPU through
RADV (Mesa's AMD Vulkan driver) behind a minimal native layer.

Everything here is **early, experimental homebrew**. Today the runtime reaches
Wine's own startup on real hardware (ntdll loads, the prefix is created, the
wineserver module starts); games do not run yet. Follow development in
[WINE_PS4_STATUS.md](WINE_PS4_STATUS.md), which documents every milestone and the
current blocker in detail.

## What this repository contains

| Path | What it is |
|---|---|
| `wine-ps4-all-changes.patch` | The complete diff against upstream Wine that makes it build and run for PS4 (72 files) |
| `build-ps4-module/` | Per-module build pipeline: linker script (16 KiB alignment), TLS stripping, PRX descriptor generation, packaging manifest |
| `eboot-src/` | The tiny bootstrap that loads `ntdll.prx` and calls `__wine_main` |
| `wine-ps4-shims/` | Headers and shims for the gaps between Wine and OrbisOS/FreeBSD 9 |
| `WINE_PS4_STATUS.md` | Build recipe + status; start here to understand the port |

## How it works, in one paragraph

A PS4 app boots a small `eboot` that loads Wine's `ntdll` as a PS4 PRX module.
PRX modules can't carry TLS and must be 16 KiB page-aligned, so the build patches
the ELF: TLS is stripped and locale is served from a process-global shim. Wine
then creates its prefix on writable storage (`/data/wine`), starts the
wineserver **in-process** (the console forbids forks), and programs the console's
GPU through RADV with a GNM submission backend instead of Linux's amdgpu.

## Hardware and setup needed

- A **jailbroken PS4** ([GoldHEN](https://github.com/GoldHEN/GoldHEN)) with the
  FTP and klog plugins enabled.
- On your PC: OpenOrbis toolchain + orbis-compat, host Wine build tools
  (see `WINE_PS4_STATUS.md` for the exact recipe).
- The console address is read from `PCPS4_CONSOLE_IP` — nothing here hardcodes
  a machine-specific IP:

  ```bash
  export PCPS4_CONSOLE_IP=192.168.1.n   # your PS4's LAN address
  python3 build-ps4-module/klog_capture.py klog.txt 60
  ```

## Status and roadmap

1. ~~Wine Unix-side builds for PS4~~ ✅
2. ~~Module pipeline (PRX, TLS, alignment, descriptors)~~ ✅
3. ~~Runtime boots on hardware; prefix created; wineserver loads~~ ✅
4. **Now: NLS locale file open from read-only `/app0` PFS** ← current blocker
5. Next: signal-path validation on hardware, then a trivial `.exe`, then DXVK
   over RADV, then real games.

## Credits and standing on shoulders

Built on [Wine](https://gitlab.winehq.org/wine/wine) (LGPL-2.1),
[Mesa/RADV](https://gitlab.freedesktop.org/mesa/mesa),
[DXVK](https://github.com/doitsujin/dxvk), the
[OpenOrbis](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain) homebrew
toolchain, [orbis-compat](https://github.com/sumanj/ps4-openorbis-compat),
[GoldHEN](https://github.com/GoldHEN/GoldHEN), and the PS5-side explorer work at
[prospero-win](https://github.com/mpereiraesaa/prospero-win) /
[PS5_Mesa](https://github.com/mpereiraesaa/PS5_Mesa), which
proved most of this stack on the PS5 first.

## License

Wine is LGPL-2.1-or-later; this port preserves it (`LICENSE`). Everything else
here follows the same terms.
