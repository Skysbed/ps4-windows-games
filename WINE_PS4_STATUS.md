
## MILESTONE 2026-10-28: Wine core Unix side BUILDS for PS4
- dlls/ntdll/ntdll.so (4.0MB, ELF FreeBSD .so) -- signal/context via gregs mcontext + sysarch fsbase,
  PS5 __PROSPERO__ signal trampolines gated off for __ORBIS__ (falls to __FreeBSD__ asm arms).
- dlls/win32u/win32u.so (8.9MB)
- server/wineserver (4.7MB, ELF PIE exe)
Link flags: .so = -nostartfiles -nodefaultlibs -fuse-ld=lld + --start-group -lorbis-compat -lc -lkernel
 -lc++ -lc++abi -lunwind --end-group + --warn-unresolved-symbols. exe (wineserver) adds -Wl,-pie
 --script link.x --eh-frame-hdr crt1.o. Override LDFLAGS per-target at make time (configure LDFLAGS
 has crt1.o/-pie for conftests, conflicts with -shared).
Extra shims added this round: linux/filter.h (BPF) in ps4_wine_compat.h force-include; machine/segments.h
 force-included; amd64_{get,set}_{fs,gs}base inline-over-sysarch in machine/sysarch.h.
NEXT: PE modules (Windows DLLs via mingw-w64 -> needs sudo apt); then prospero-win native layer ported
 PS5->PS4 (VideoOut/audio/pad/wine host entry); then DXVK; then PKG. Signal path is compile-correct but
 UNVALIDATED on hardware (segment regs cosmetic; mc_* via gregs needs on-device test).

## MILESTONE 2026-10-29: Wine PE/Windows side BUILDS (complete Wine)
build-host-tools (clean source-clean tree) reconfigured WITH mingw-w64 -> `make` exit 0, 0 errors,
**1235 PE .dll** built incl ntdll/kernel32/kernelbase/user32/gdi32/ole32/msvcrt + d3d9/d3d11/d3d12/dxgi.
These are the Windows-side DLLs a .exe links against (arch-independent PE; same ones the PS4 Wine uses).
Now BOTH halves of Wine build for PS4: Unix side (ntdll.so/win32u.so/wineserver in build-ps4/) + PE side
(1235 dlls in build-host-tools/). ALL major blocks now compile: RADV driver, Wine (both halves), native layer.
NEXT = ASSEMBLY not compile: wine64_main host entry + wire win32u dlopen(libvulkan.so)->RADV libvulkan_radeon.a
+ DXVK (d3d->vulkan) + prospero-win launcher + package as OpenOrbis PKG (create-fself/create-gp4/PkgTool.Core)
+ on-device bring-up (signal path unvalidated). mingw now installed on laptop.

## ASSEMBLY PHASE started 2026-10-29
- native/wine64_main.c + pw_{pad,audio,videoout,hid}_ps5.c ALL compile against OpenOrbis as-is
  (-D__PROSPERO__ -D__ORBIS__). Only pw_agc_ps5.c is PS5-only -> DROP (RADV orbis winsys drives GPU).
- Vulkan bridge: libvulkan_radeon.a exposes `vk_icdGetInstanceProcAddr` (T). Wine's winevulkan dlopens
  libvulkan.so; prospero builds libvulkan.prx from the RADV archive via tools/link_radv_prx.sh +
  wine/ps5/pw_vk_radv_profile_wrap.c. PS4 plan: build a libvulkan module exposing vkGetInstanceProcAddr
  (wrap vk_icdGetInstanceProcAddr) from libvulkan_radeon.a, that win32u/winevulkan dlopen.
- REMAINING ASSEMBLY (needs focused session + the console at PCPS4_CONSOLE_IP):
  1. libvulkan from RADV (expose vkGetInstanceProcAddr).
  2. link the PS4 title: wine64_main + native layer + Wine .so (ntdll/win32u/wineserver) + RADV, bundle the
     1235 PE DLLs as /app0 data, Wine prefix setup (native/pw_wine_prefix.c).
  3. reconfigure Wine build-ps4 WITH vulkan (currently --without-vulkan) so winevulkan.so builds.
  4. DXVK: prebuilt or mingw-built d3d*.dll over the vulkan path (or start with Wine's built-in wined3d).
  5. package as OpenOrbis PKG (create-fself/create-gp4/PkgTool.Core in ~/openorbis-toolchain/bin/linux);
     36-char CONTENT_ID, FTP/bgft install per reference_pcps4_build_test_loop.
  6. on-device bring-up: validate the signal path (gregs mcontext, untested), then a trivial .exe, then a game.

## ASSEMBLY 2026-10-29: graphics bridge DONE
- winevulkan.so built (reconfigured build-ps4 WITH vulkan: ac_cv_lib_soname_vulkan=libvulkan.so).
- libvulkan.so BUILT at ~/pcps4-work/libvulkan-ps4/libvulkan.so (ELF FreeBSD .so, exports
  vkGetInstanceProcAddr -> RADV vk_icdGetInstanceProcAddr). Link: shim.o + --whole-archive
  libvulkan_radeon.a + build-orbis zlib libz.a + -lSceGnmDriver -lSceVideoOut -lkernel -lc -lc++
  -lc++abi -lunwind, -shared -nostartfiles -nodefaultlibs --warn-unresolved-symbols.
  => Wine winevulkan dlopen(libvulkan.so) -> RADV -> GNM/sceVideoOut -> PS4 GPU. BRIDGE COMPLETE.
ALL binary artifacts now exist for PS4: libvulkan.so(RADV), Wine Unix .so (ntdll/win32u/wineserver/
winevulkan), 1235 PE DLLs, native layer objects.
NEXT (title link + PKG; link offline, install needs console PCPS4_CONSOLE_IP):
  1. link the eboot: wine64_main + native/*_ps4 objects + Wine Unix .so + libvulkan.so, via OpenOrbis
     link.x/crt1.o; how prospero loads Wine .so = pw_wine_dl (dlopen of .so laid beside eboot). Lay
     ntdll.so/win32u.so/wineserver/winevulkan.so/libvulkan.so + 1235 PE dlls under /app0 (sce_sys data).
  2. Wine prefix bootstrap (native/pw_wine_prefix.c) + a trivial test .exe (mingw hello).
  3. DXVK later (start with Wine built-in wined3d / or stub d3d) - first goal is ANY .exe running.
  4. create-fself + create-gp4 + PkgTool.Core -> PKG (36-char CONTENT_ID); FTP/bgft install.

## ASSEMBLY 2026-10-29: PKG payload staged (~/pcps4-work/pkg-stage/app0)
- wine/lib: ntdll.so win32u.so winevulkan.so libvulkan.so ; wine/bin: wineserver ; wine/dll: 1235 PE DLLs
  (flat copy collided i386/x86_64 same-name -> 621 unique; PACKAGING TODO: split system32=x86_64,
  syswow64=i386 into the Wine prefix). Payload ~681M.
- EVERYTHING BUILDABLE OFFLINE IS DONE. Remaining is console-dependent:
  (a) link the eboot: prospero-win native/*_ps4 + its src/ launcher (pw_wine_start/launch/game_profile/
      display/data_mount/diagnostics) + the Wine .so; replace PS5 prospero-lld PRX + `self --sign`
      with OpenOrbis lld + link.x + create-fself. The launcher src/ needs a PS5->PS4 port pass
      (it references ps5log, lapy elevation, PS5 module APIs). A MINIMAL custom eboot (init VideoOut
      via RADV wsi + pw_wine_start a .exe) may be faster than porting the whole launcher for first light.
  (b) Wine prefix bootstrap (native/pw_wine_prefix.c) + trivial mingw hello.exe.
  (c) PKG via create-gp4 + PkgTool.Core (36-char CONTENT_ID), FTP/bgft install to PS4 PCPS4_CONSOLE_IP.
  (d) FIRST on-device test: does the signal path (gregs mcontext, untested) survive; does ntdll init;
      does a .exe reach WinMain. Then DXVK + a real game.

## ON-DEVICE BRING-UP 2026-10-29: reached the module-packaging frontier
- test.exe (mingw PE32+, returns 0x42) staged at pkg-stage/app0/test.exe.
- PS4 reachable from laptop: FTP:2121 + klog:3232 OPEN (GoldHEN).
- ntdll.so module-import cleanup DONE: compiled wine/ps5 support objs (pw_wine_cwd/cwd_libc/sink/heap/
  heap_libc/dmem/dmem_ps5 all compile for PS4 as-is) + ps4_module_stubs.o (thr_self via
  scePthreadGetthreadid, getmntinfo=0, thr_set_name no-op, weak pw_wine_menu, weak __eh_frame_*) +
  -Wl,--wrap for 33 libc path fns (access chdir chmod close fchdir fopen fstatat getcwd link lstat
  mkdir* open* opendir readlink realpath rename* rmdir stat statfs symlink* truncate unlink* utimes).
  ntdll.so now links clean with support.
- BLOCKER: OpenOrbis create-fself --lib CRASHES (Go nil ptr @ OELFGenProgramHeaders.go:151) converting
  ntdll.so -> .prx. OpenOrbis samples never build .prx libs from source (ship prebuilt libc.prx etc).
  prospero-win avoided SDK tools entirely: uses prospero-lld + its OWN native/{elf_object,sce_module_writer}.cpp
  to write PS5 modules. OPTIONS for PS4: (1) port prospero's sce_module_writer.cpp to emit OpenOrbis PRX;
  (2) patch create-fself (find why program headers nil); (3) link ntdll.so in create-fself's expected layout
  (match OpenOrbis module ELF: specific PT_ segments). Likely (1) or (3).
- AFTER module packaging: minimal eboot (PwWineStartOps: load_start=sceKernelLoadStartModule, set_env=setenv,
  start_thread=scePthread) runs pw_wine_start -> __wine_main on /app0/test.exe; PKG via create-gp4+PkgTool.Core;
  install FTP/bgft; klog. Then iterate Wine init (signal path untested). Then DXVK + real game.

## DEFINITIVE 2026-10-29: create-fself --lib cannot make Wine modules
Tested create-fself --lib on a TRIVIAL .so and on ntdll.so, with/without .got.plt, with/without
OpenOrbis link.x: ALL fail (trivial: ".got.plt section must exist"; with link.x/.got.plt present:
Go nil-ptr panic OELFGenProgramHeaders.go:151). Conclusion: OpenOrbis create-fself's --lib (.prx from
source) path is not usable. OpenOrbis is built around ONE eboot (-pie fself) + prebuilt system .prx.
=> ARCHITECTURAL DECISION NEEDED for the eboot, pick one:
  A. STATIC: link all of Wine (ntdll/win32u/wineserver as .a) + libvulkan_radeon.a + native into ONE
     eboot fself. Wine is designed around dlopen'd modules + a separate wineserver process, so this needs
     Wine built with --disable-shared-ish / the modules as archives and an in-process server (prospero
     patch 0110 already makes wineserver in-process!). Investigate Wine static/single-binary build.
  B. CUSTOM MODULE WRITER: port prospero-win native/{elf_object,sce_module_writer}.cpp to emit OpenOrbis
     PRX from a .so (bypasses create-fself). ~the proven path (prospero does exactly this on PS5).
  C. Patch create-fself's OELFGenProgramHeaders.go nil-deref + the .got.plt handling.
Recommend B (proven) or A (simpler if Wine cooperates). Everything else is built & staged; this is the gate.

## BREAKTHROUGH 2026-10-29: module pipeline SOLVED, ntdll.prx built
FIX: create-fself --lib needs .data.sce_module_param -> link the .so WITH OpenOrbis crtlib.o
(~/src-ps4/orbis-compat/build/crt/crtlib.o) + -Wl,--script=link.x. Then create-fself --lib works.
PROVEN: ntdll.prx BUILT (1.5MB) at pkg-stage/app0/sce_module/ntdll.prx. Recipe per module:
  clang -shared -nostartfiles -nodefaultlibs -fuse-ld=lld -Wl,--script=$TC/link.x $crtlib.o \
    <module objs/.so's objects> -L$TC/lib -L$COMPAT/build --start-group -lorbis-compat -lc -lkernel \
    -lc++ -lc++abi -lunwind --end-group --warn-unresolved-symbols ; then create-fself -in=.so --lib=.prx.
REMAINING per-module (next session, methodical):
  - win32u.prx / winevulkan.prx: they DT_NEEDED ntdll.so; create-fself tries to open the needed lib
    ("open /ntdll.so"). Provide the needed modules as stubs at conversion (prospero uses import-stubs/),
    OR strip DT_NEEDED and let runtime resolve. Also their `make` needs the support LDFLAGS right.
  - libvulkan.prx: unresolved orbis_sysconf -> add -Wl,--whole-archive liborbis-compat.a (so its
    overrides are pulled) or otherwise define orbis_sysconf.
THEN: minimal eboot (-pie exe via link.x+crt1.o -> create-fself --eboot): wine64_main + native/*_ps4
objs + PwWineStartOps(sceKernelLoadStartModule/setenv/scePthread) -> pw_wine_start -> __wine_main on
/app0/test.exe. PKG: create-gp4 (eboot.bin + sce_module/*.prx + libc.prx + /app0 PE DLLs + test.exe) +
PkgTool.Core (36-char CID). Install FTP/bgft -> PCPS4_CONSOLE_IP, klog. Then iterate Wine init (signal
path UNTESTED), then DXVK + game. THE MODULE-FORMAT GATE IS CLEARED.

## MILESTONE 2026-10-29 06:32: INSTALLABLE PKG BUILT + on the console
IV0000-PCPS40001_00-WINEPS4000000001.pkg (83.5MB) built @ ~/pcps4-work/pkg-build/ and FTP-uploaded to
PS4 /data/pcps4-wine.pkg. Contains: eboot.bin (Wine bootstrap) + sce_module/{ntdll,win32u,winevulkan,
libvulkan,libc}.prx + 16 core Windows DLLs (flat at root - LibOrbisPkg only declares sce_sys/sce_module/
assets dirs, a custom dll/ dir crashes it; put DLLs flat or in assets) + test.exe (returns 0x42) + sce_sys.
PKG recipe fixes: DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1 + libssl1.1 on LD_LIBRARY_PATH (~/pcps4-work/
ssl11/lib) for PkgTool.Core; full sce_sys incl about/right.sprx; CONTENT_ID IV0000-PCPS40001_00-
WINEPS4000000001 (36). ps4debug(744) CLOSED -> no automated bgft install; user installs from GoldHEN
Package Installer (/data/pcps4-wine.pkg) OR enable ps4debug. Then LAUNCH "PcPs4 Wine FirstLight"
(PCPS40001); read /data/pcps4_wine_boot.txt (FTP) + klog:3232 for pw_wine_start_load/stage/status.
Expect first-light issues: WINEDLLPATH not set in eboot (DLLs flat at /app0 -> may need WINEDLLPATH=/app0),
prefix bootstrap, untested signal path. Iterate from the boot log.

## FIRST LIGHT 2026-10-29: "cannot continue using this application / corrupted" at LAUNCH
PKG installed (moved to /data/pkg/pcps4-wine.pkg next to FPKGi) but launching shows the PS4 "application
is corrupted" error. Diagnostics so far: eboot.elf DT_NEEDED = only libkernel.so (clean); all 5 .prx
present in sce_module/; param.sfo matches a working sample exactly (same entries). So NOT a missing-dep or
sfo issue -> it's an fself/eboot-format rejection at load.
LIKELY CAUSES / NEXT (needs ps4debug OR bisect):
  1. The eboot is a complex -pie with STATIC libc++/libc++abi/libunwind + orbis-compat (lots of relocs/TLS);
     the PS4 loader may reject a section/segment. ISOLATE: build a TRIVIAL eboot (hello -> /data log) with
     the EXACT same create-fself(--eboot --paid 0x3800000000000011)+create-gp4+PkgTool recipe. If trivial
     ALSO "corrupted" -> recipe bug (fself sdkver/type). If trivial launches -> bisect the real eboot
     (drop static c++, trim, etc.).
  2. fself --paid / sdkver / --fw: try matching an OpenOrbis sample's exact create-fself args; consider
     --paid and a lower SDK/system version. readoelf on eboot.bin to inspect the fself header.
  3. Enable ps4debug (port 744) in GoldHEN -> klog/kernel shows the real SELF load failure reason.
Everything builds & packages; this is a launch-format gate, the last thing between us and seeing Wine run.

## FIX 2026-10-29: duplicate PT_INTERP was the "corrupted" cause
Compared my eboot vs a known-WORKING PcPs4 eboot (~/pcps4_build/PcPs4/eboot.bin) by parsing SELF+inner-ELF
program headers. Working had 1 INTERP; MINE had TWO (my -pie link emitted PT_INTERP AND create-fself adds
its own). PS4 loader rejects dup INTERP -> "application is corrupted". FIX: add -Wl,--no-dynamic-linker to
the eboot link so my link emits NO INTERP; create-fself then adds the single correct one. Verified:
eboot.elf INTERP=0, eboot.bin inner INTERP=1. PKG rebuilt + re-uploaded to /data/pkg/pcps4-wine.pkg.
(Decrypted 13.02 dump confirms format: real eboot inner e_type=0xfe10 ET_SCE_DYNEXEC, sprx=0xfe18; mine match.)
NEXT: user reinstalls (may need to DELETE old "PcPs4 Wine FirstLight" first - same CONTENT_ID) + launches;
read /data/pcps4_wine_boot.txt + klog:3232. If it launches past the eboot, we see pw_wine_start_load result.
