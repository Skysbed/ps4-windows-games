# Building PCPS4 from source

This guide reproduces the current build: the PS4 Wine runtime (`ntdll`, `win32u`,
the in-process `wineserver`), the RADV Vulkan driver, the `eboot`, and the
installable PKG.

> **Status:** early homebrew. A successful build boots Wine's Unix side on the
> console and reaches the in-process wineserver; it does not run Windows games
> yet (see [WINE_PS4_STATUS.md](WINE_PS4_STATUS.md)). Build it to develop and test,
> not to play games.

Everything below uses placeholders — set them to paths on your machine:

```bash
export PS4SDK=$HOME/openorbis-toolchain      # OpenOrbis PS4 toolchain
export WORK=$HOME/pcps4                       # your build workspace
mkdir -p "$WORK" && cd "$WORK"
```

## 1. Host

A **Linux x86_64** machine. Wine's PE-side host tools need a Linux toolchain;
macOS cannot build them. Install (names vary by distro):

- `clang`, `lld`, `llvm` (objcopy/nm/readelf/symbolizer)
- `bison`, `flex` (real ones — not macOS stubs), `gperf`
- `mingw-w64` (builds the Windows-side DLLs)
- `python3`, `git`, GNU `make`, `pkg-config`
- `glslang` / `glslangValidator` (for the Mesa/RADV build)
- `dotnet` runtime deps for the packager, plus **OpenSSL 1.1** (`libssl.so.1.1`,
  `libcrypto.so.1.1`) — newer distros ship 3.x, so keep a 1.1 copy on
  `LD_LIBRARY_PATH` just for the packaging step.

## 2. Toolchain and support libraries

All of these are public (see Credits in the README):

```bash
# OpenOrbis PS4 toolchain (provides clang target, link.x, create-fself, PkgTool.Core)
git clone https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain "$PS4SDK"

# FreeBSD-9 libc compatibility layer every PS4 module links against
git clone https://github.com/orbis-ports/orbis-compat        "$WORK/orbis-compat"
git clone https://github.com/orbis-ports/orbis-porting-kit   "$WORK/orbis-porting-kit"

# RADV reference port (GPU driver) and the PS5 Wine stack this port mirrors
git clone -b orbis https://github.com/orbis-ports/mesa-ps4    "$WORK/mesa-ps4"
git clone https://github.com/mpereiraesaa/prospero-win        "$WORK/prospero-win"
```

Build orbis-compat first — it produces `liborbis-compat.a`, `crt1.o` (eboot) and
`crtlib.o` (modules):

```bash
cd "$WORK/orbis-compat" && OO_PS4_TOOLCHAIN="$PS4SDK" ./build.sh --no-check
```

## 3. Wine source + this port's patch

`prospero-win` pins a specific upstream Wine commit and applies its PS5 patch set;
this repo's `wine-ps4-all-changes.patch` applies the PS4-specific changes on top.

```bash
# Obtain the Wine tree prospero-win pins and let it apply its patches
#   (follow prospero-win/README for the exact commit + patch step), then:
cd "$WORK/wine"
git apply /path/to/ps4-windows-games/wine-ps4-all-changes.patch
```

## 4. Wine host (PE) tools — the Windows-side DLLs

Build these from a **clean** tree (not the module tree) with mingw enabled:

```bash
cd "$WORK/wine-host" && "$WORK/wine/configure" --enable-archs=i386,x86_64
make -j"$(nproc)" __tooldeps__      # winebuild etc. + the ~1200 PE .dll files
```

## 5. RADV Vulkan driver

```bash
cd "$WORK/mesa-ps4"
OO_PS4_TOOLCHAIN="$PS4SDK" ORBIS_COMPAT_DIR="$WORK/orbis-compat" \
  ORBIS_KIT_DIR="$WORK/orbis-porting-kit" build-support/orbis/build.sh
# -> build-orbis/.../libvulkan_radeon.a  (exports vk_icdGetInstanceProcAddr)
```

## 6. The PS4 Wine modules (ntdll / win32u / wineserver)

PS4 PRX modules cannot carry TLS and must be 16 KiB page-aligned, and the console
has no `SCM_RIGHTS` fd-passing — the pipeline in [`build-ps4-module/`](build-ps4-module)
handles all of this:

- `link_ps4mod.x` — linker script; collects `.data.*` so segments stay 16 KiB
  aligned (what the PS4 rtld requires).
- `ps4_tls_shim.c` — replaces musl's thread-local locale with a process-global C
  locale (modules can't have TLS).
- `ntdll_desc.c` / `wineserver_desc.c` — PRX export descriptors
  (via `prospero-win/tools/gen_prx_descriptor.py`) so the bootstrap can find
  `__wine_main` etc.
- `patch_tls_dyn.py` — strips the `PT_TLS` program header + nulls TLS relocations
  after `create-fself`.
- `segalign.py` / `cmp_self.py` — verify segment alignment / headers.
- `shim_sys_extattr.h` — one of the FreeBSD-9 gap shims.

Per module the recipe is: compile the Wine objects with
`--target=x86_64-pc-freebsd12-elf`, link `-shared` with `link.x`, `crtlib.o`, the
`prospero-win/wine/ps5` support objects, the `--wrap` path-function list, and the
module's `*_desc.o`; then `create-fself --lib` → `.prx`; then `patch_tls_dyn.py`.
The exact, current command lines are logged step by step in
[WINE_PS4_STATUS.md](WINE_PS4_STATUS.md).

## 7. libvulkan + eboot

```bash
# libvulkan.prx: wrap libvulkan_radeon.a so winevulkan can dlopen it
# eboot: build eboot-src/ (loads ntdll.prx and calls __wine_main) with
#        crt1.o + link.x, then create-fself --eboot.
```

## 8. Package the PKG

Stage `sce_module/` (`ntdll win32u wineserver libvulkan libc libSceFios2` + the
`nls/` locale files), the PE DLLs, a test `.exe`, and `sce_sys/`, then:

```bash
export OO_PS4_TOOLCHAIN="$PS4SDK" DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1
export LD_LIBRARY_PATH=/path/to/openssl-1.1/lib      # libssl/libcrypto 1.1
"$PS4SDK"/bin/linux/PkgTool.Core pkg_build build-ps4-module/pkg.gp4 out/
```

The CONTENT_ID must be 36 characters (`pkg.gp4` carries the one this project uses).

## 9. Install and run

See [INSTALL.md](INSTALL.md) — copy the PKG to the console over FTP, install via
GoldHEN, launch, and read the diagnostic log from `/data`.
