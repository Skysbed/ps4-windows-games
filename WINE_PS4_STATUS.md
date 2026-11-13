
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
- REMAINING ASSEMBLY (needs a focused session + the console (PCPS4_CONSOLE_IP env)):
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
NEXT (title link + PKG; link offline; install needs the console (PCPS4_CONSOLE_IP env)):
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
  (c) PKG via create-gp4 + PkgTool.Core (36-char CONTENT_ID), FTP/bgft install to the PS4 (PCPS4_CONSOLE_IP env).
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
PkgTool.Core (36-char CID). Install FTP/bgft -> the console (PCPS4_CONSOLE_IP env), klog. Then iterate Wine init (signal
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

## MAJOR 2026-10-29: Wine __wine_main RUNS on PS4; prefix created on-device
Bootstrap fully green (boot log: wine_start_load=0 stage=6 environment=0 run=0). __wine_main runs on its
thread. Walked Wine init forward by fixing, in order:
- ntdll.prx ENOEXEC: rtld "segment not page aligned". ROOT: orphan .data.* (static libc++/libunwind)
  landed mid-page; create-fself starts the data PT_LOAD at firstDataSection. FIX: link_ps4mod.x = link.x
  with `.data : ALIGN(0x4000) { *(.data .data.*) }` so orphans collect after the page-aligned
  .data.sce_module_param. ALL LOAD/RELRO segs now 16K-aligned. (TLS also stripped: PT_TLS->PT_NULL +
  DTPMOD/DTPOFF reloc types->R_X86_64_NONE via patch_tls_dyn.py; PS4 rtld rejects any PT_TLS in a PRX.)
- stage 3 DESCRIPTOR (-3): module lacked prospero PRXDESC1 export descriptor. FIX: gen_prx_descriptor.py
  -> ntdll_desc.c (only ntdll-DEFINED symbols: __wine_main + dlerror/pw_wine_heap_stats/pw_wine_sink*/
  pw_cwd_set/pw_wine_*_display), llvm-objcopy --localize-symbol=module_start/module_stop (crtlib provides
  the real SELF entry), link ntdll_desc_loc.o.
- Wine init crashes (env NOT shared: eboot & ntdll each static-link orbis-compat musl -> separate environ,
  so eboot setenv never reaches ntdll getenv). Patched dlls/ntdll/unix/loader.c:
  init_paths ntdll_dir fallback "/app0/sce_module"; set_config_dir WINEPREFIX fallback "/data/wine";
  set_home_dir home fallback "/data" (was strdup(NULL)->strlen(NULL) SIGSEGV).
- reexec_loader: made a no-op on __PROSPERO__ (in-process loader, never re-exec; was SIGSYS do_execve).
- __musl_current_locale: __nl_langinfo SIGSEGV because we stripped the TLS. FIX: ps4_tls_shim.c overrides
  __tls_get_addr to return a process-global zeroed __locale_struct (=C locale). Linked FIRST so it beats
  libc.a's. Single-locale is fine for a game.
=> Wine then created '/data/wine' and asked for the in-process wineserver.
NEXT (in progress): build+package wineserver.prx (prospero in-process server). server.c already patched
(WINE_PS5_NTDLL_DIR fallback "/app0/sce_module"). Needs: link server/*.o as a .so (link_prx wineserver
recipe) + support (pw_wine_compat{,_libc}.o pw_wine_threads{,_libc}.o pw_wine_cwd{,_libc}.o heap +
wineserver_desc.o exposing pw_wineserver_connect/pw_wine_thread_{register,unregister}/
pw_wineserver_call_direct/try_fast_mutex/mutex_backend/sync_backend/pw_cwd_share_changes), crtlib,
link_ps4mod.x, create-fself --lib, TLS-strip, add to pkg.gp4. Then it dlopens /app0/sce_module/wineserver.so
(pw_wine_dl maps .so->.prx). Module recipe + all patches now saved: build-ps4-module/ + wine patch.

## 2026-10-29 (wk-limit checkpoint): wineserver loads+runs; blocked on server NLS open
Chain working on-device: eboot -> ntdll (aligned, descriptor, dl-shims, locale __tls_get_addr shim,
loader.c PS4 default paths) -> prefix /data/wine created -> wineserver.prx dlopen'd via pw_wine_dl ->
pw_wineserver_connect -> create_server_dir (prefix defaulted /data/wine, dev/ino chdir check #ifndef'd
out on PS4) -> load_intl_file. CURRENT BLOCKER: wineserver load_intl_file() "failed to load l_intl.nls"
then SIGSYS (fatal_error). NLS files ARE packaged (76 at sce_module/nls/, verified via pkg_extract) and
inprocess_nls_dir=/app0/sce_module/nls is correct, but open_fd of /app0/sce_module/nls/l_intl.nls fails.
pw_wine_cwd returns absolute paths as-is, so it is the underlying open of a read-only /app0 PFS file from
the server that fails. A diagnostic (raw open(path) + errno log) is linked into the current wineserver.prx
-> NEXT SESSION: relaunch, read klog "pcps4: nls try [path] rawopen=.. errno=..". If raw open works but
open_fd fails, the issue is server/fd.c open_fd (flags/fstatfs on PFS); if raw open also fails, it is the
sceKernel open of /app0 files from the server (path form / O_RDONLY on read-only PFS). Likely fixes: copy
NLS to /data/wine at boot, or adjust open flags. Build recipe for every module + all patches are in
wine-ps4-all-changes.patch + build-ps4-module/. Modules rebuilt via: make dlls/ntdll/ntdll.so (or
server/wineserver) with LDFLAGS using link_ps4mod.x + crtlib + shim objs + *_desc_loc.o; create-fself
--lib; patch_tls_dyn.py; pkg via PkgTool.Core pkg_build pkg.gp4; FTP to /data/pkg/pcps4-wine.pkg.

## Update: in-process wineserver fully connects; Wine reaches init_first_thread

Progress since the NLS-open blocker above. All of this is verified on real hardware
(diagnostics written to a log file on the console's `/data`, read back over FTP):

- **NLS loads.** The server's first NLS directory is pinned to `/app0/sce_module/nls`
  (where the 76 locale files are packaged); `l_intl.nls` now opens. `KUSER_SHARED_DATA`
  maps via `shm_open(SHM_ANON)`. The read-only `/app0` section mapping of the NLS file is
  non-fatal and skipped.
- **Server directory + registry.** `create_server_dir` and `__wrap_fchdir` compared a
  path `stat` against an `fstat` by dev/ino; the console's filesystem returns those
  inconsistently, so that cross-check is disabled on the console. The server's prefix
  defaults to `/data/wine` (the server has its own empty environment).
- **Server thread starts** (`pthread_create` succeeds) and enters its main loop.
- **No SCM_RIGHTS.** The console's `AF_UNIX` sockets reject `SCM_RIGHTS` ancillary
  fd-passing (`sendmsg` → `EINVAL`). Because the in-process client and server share one
  file-descriptor table, the descriptor number is already valid on both sides, so all of
  Wine's fd-passing (`connect_inprocess_client`/`receive_inprocess_client`,
  `wine_server_send_fd`/`wine_server_receive_fd`, server `send_client_fd`/`receive_fd`) was
  changed to send the raw fd integer as plain data on the console. For the same reason the
  server no longer closes its copy of a descriptor it just handed to the client
  (e.g. `request_pipe[1]`), and `SIGPIPE` is ignored before the first such write.
- **Verified end-to-end:** the server accepts the connection, creates the process and
  first thread, the version handshake passes (protocol 961), and both the reply and wait
  pipes are exchanged in each direction — `init_thread_pipe` completes.

**Current blocker:** the first real request, `init_first_thread`. Its server handler runs
`init_thread(reply_fd, wait_fd)` and then hands back an `inproc_device` descriptor. The
next step is to narrow which line faults (the request round-trip vs. the `inproc_device`
hand-off) and apply the same in-process fd handling if needed. After that: load the
PE-side `ntdll` and run a first `.exe`.

## Update: `init_first_thread` resolved — the handshake completes; process-parameter setup is next

All of the following is verified on real hardware, with diagnostics written to a log
file on the console's `/data` and read back over FTP.

### The `init_first_thread` blocker was a non-blocking-I/O restriction

The server handler reached `init_thread()` and the process then exited with no
message. The cause was a single call:

```c
if (fcntl( reply_fd, F_SETFL, O_NONBLOCK ) == -1) goto error;   /* server/thread.c */
```

On the console this returns **`EACCES`**. Upstream's `error:` path then does
`close(reply_fd); close(wait_fd)` — and because the in-process client and server share
one file-descriptor table, those are the *client's* descriptors. The client was blocked
in `read()` on its reply pipe; closing the write end made that read return `0`, which
upstream treats as a protocol error and exits the process. The handler never got to log
anything, so it looked like a segfault.

### Why `fcntl(F_SETFL)` fails, from the kernel

Reading the decompiled 13.02 kernel gave the exact mechanism:

- `kern_fcntl`'s `F_SETFL` case calls `fp->f_ops->fo_ioctl(fp, FIONBIO, …)` and returns
  its result verbatim. `kern_ioctl` dispatches to the **same** `fo_ioctl` slot — so
  `ioctl(fd, FIONBIO, &one)` is *not* a workaround; it fails identically.
- The refusal is at the top of the socket's `soo_ioctl`, before any command handling:
  a credential check, and on failure it returns `EACCES`.
- That check reads a 64-bit field in the process credential and tests **one capability
  bit**. Ordinary homebrew does not have it. Stock FreeBSD's `soo_ioctl` has no such
  check and `FIONBIO` never returns `EACCES`, so this is a vendor modification.

**What does work**, also confirmed in the kernel:

- **Use a `pipe()` rather than a `socketpair()`.** `pipe_ioctl` treats `FIONBIO` as a
  no-op returning success — no capability check — and `pipe_read` honours the
  `FNONBLOCK` flag directly. So plain `fcntl(F_SETFL, O_NONBLOCK)` works on a pipe.
- **Or pass `MSG_DONTWAIT` per call.** `soreceive` blocks unless the socket has
  `SS_NBIO` *or* the per-call flags request non-blocking, and that path is not gated.
- `SOCK_NONBLOCK` at creation time does **not** exist in this kernel.

The current build treats the failure as non-fatal and continues with a blocking reply
socket, which is correct for the first thread because the client is always blocked
reading the reply when the server writes it. Switching the reply channel to a `pipe()`
is the proper fix.

### Correction: `SCM_RIGHTS` is not simply absent

An earlier section above states that the console's `AF_UNIX` sockets reject
`SCM_RIGHTS`. That is too strong, and this is a correction. The kernel's
`unp_externalize` — the receive half — **does** implement `SCM_RIGHTS`: it tests
`cmsg_level == SOL_SOCKET && cmsg_type == SCM_RIGHTS` and allocates a descriptor per
entry. What is actually established is narrower: *our* `sendmsg` with an `SCM_RIGHTS`
control message returned `EINVAL`. The send-side `unp_internalize` has not been located
(the `AF_UNIX` protocol handlers are reached through a dispatch table, so there are no
textual cross-references to follow), so whether `SCM_RIGHTS` *send* is supported,
inlined, or removed is **undetermined**. The raw-descriptor scheme this port uses works
regardless, but it rests on a narrower fact than previously written here.

Related: **`dup()` returns `EPERM`** on the console, so duplicating a received
descriptor to restore ordinary ownership semantics is not available through that call.
`fcntl(fd, F_DUPFD, 0)` is a different `kern_fcntl` case that does not route through the
gated `fo_ioctl`, and is the next thing to try.

### The handshake now completes

With the above, the first server request succeeds end to end:

```
ifth: init_thread ok / affinity ok / base_priority ok
ifth: pid=… tid=… info_size=0 nmach=2
siproc: wsc ret=0
wine startup: server connected
```

The process and its first thread exist, two machine architectures are reported, and
`server_init_process()` returns.

### Current work: process-parameter setup

Past the handshake, `start_main_thread()` runs
`virtual_map_user_shared_data → init_cpu_info → init_files → init_startup_info`. With no
parent process, `init_startup_info()` calls `build_initial_params()`, and two further
console-specific faults turned up there:

- **`environ` is NULL.** Each PS4 module statically links its own libc, so `ntdll`'s
  `environ` is never populated by the bootstrap. `get_initial_environment()` walks it
  unguarded and dereferences NULL on the first iteration. It now treats a NULL `environ`
  as an empty environment; Wine then builds the environment from the registry and its own
  defaults, which is the right behaviour on a console anyway.
- **The NLS casemap table was never loaded in `ntdll`.** `init_environment()` does:

  ```c
  if ((case_table = read_nls_file( "l_intl.nls" ))) { uctable = …; lctable = …; }
  ```

  `read_nls_file()` builds its path from Wine's data directory, which does not exist on
  the console, so it silently returned NULL and **`uctable`/`lctable` stayed NULL**.
  `ntdll_towupper()` indexes those tables with no guard, so the first case-insensitive
  comparison in the boot path — `find_env_var()` — faulted (the console's crash report
  gives exception code 11). The loader now reads NLS from where the package stages it,
  the result is logged either way, and both inlines return the character unchanged if the
  table is missing, so a future gap degrades to case-sensitive comparison instead of a
  fault. This is the same class of bug as the earlier wineserver NLS failure, in the
  other half of the runtime.

Also fixed while here: Wine's `set_env_var()` does not check the result of `realloc`,
and this port's allocator can legitimately return NULL; the unchecked result was then
written through. It is now checked.

### Packaging note

Wine looks for its PE builtins in `<module dir>/x86_64-windows/`. The package manifest
must stage the Windows-side DLLs there rather than at the application root, and must
declare that nested directory in `<rootdir>` — the packaging tool fails on an
undeclared subdirectory, exactly as it does for `nls/`.

## Update: Windows code runs — the PE loader loads eleven DLLs on hardware

Verified on real hardware. Wine's own debug channels now reach a log file on the
console, so the lines quoted below are Wine's output, not inferred.

```
trace:loaddll:build_module Loaded L"unix\\app0\\test.exe" at 0000000140000000: native
trace:loaddll:build_ntdll_module Loaded L"C:\\windows\\system32\\ntdll.dll" at 00006FFFFFBD0000: builtin
trace:loaddll:build_module Loaded … kernelbase.dll, kernel32.dll, ucrtbase.dll, msvcrt.dll,
                                  sechost.dll, advapi32.dll, win32u.dll, gdi32.dll, user32.dll: builtin
err:module:import_dll Library opengl32.dll (which is needed by wined3d.dll) not found
```

A Windows x64 executable is mapped at its preferred base, the Windows-side `ntdll.dll`
is mapped and relocated, the process heap is created, system calls from Windows code are
dispatched into Wine, and the PE loader resolves imports. It currently stops on a
packaging gap (one DLL not staged), not a fault.

The test program is a small freestanding D3D11 diagnostic linked with 16 KiB section
alignment, matching the console's page size.

### What had to be fixed to get here

Each of these was found on the console, most by narrowing with probes and then reading
the kernel or Wine source for the cause.

- **`run_wineboot` cannot run.** It initialises a prefix by spawning `wineboot.exe`
  through `NtCreateUserProcess`; a console title cannot create processes, and the path
  died with `SIGSYS`. It is skipped on the console. **But** the same function is also
  where `virtual_init_user_shared_data()` is called, and skipping that had a striking
  effect (next item).
- **Windows syscalls were reaching the console's kernel.** Wine's x86-64 syscall thunk is
  `testb $1, 0x7ffe0308 ; jne dispatcher ; syscall`. With the shared data page
  uninitialised, `KUSER_SHARED_DATA.SystemCall` was 0, so the thunk executed a real
  `syscall` instruction. `NtAllocateVirtualMemory` is syscall `0x18`, which on the
  console's FreeBSD-derived kernel is `getuid()` — it returned the process uid, and Wine
  reported `Could not allocate 0x10000 bytes, status 0x1`. The shared data page is now
  initialised on the console without the spawn.
- **Stay in prefix bootstrap mode.** With no `wineboot`, the prefix has no placeholder
  files in `C:\windows\system32`. Wine only loads a builtin DLL without a matching file
  while in bootstrap mode, so the console stays in it until a prepared prefix is shipped.
- **`dup()` and `fcntl(F_DUPFD)` both fail** (`EPERM` and `EINVAL`), measured with `errno`
  captured immediately. The kernel's `do_dup` has no `EPERM` path, so the refusal comes
  from elsewhere and is not yet explained. Two consequences, both fixed:
  - *Server side:* `dup_fd_object()` could not duplicate a file's descriptor to back a
    section, which surfaced as `STATUS_ACCESS_DENIED` from `NtCreateSection`. For a named
    file the server now reopens the path instead, which gives an independent descriptor
    on the same file.
  - *Client side:* a descriptor the client obtains from the in-process server **is** the
    server's own (one shared fd table, no `dup`). The client used to close it after use,
    destroying the server's copy — the second mapping of the shared data section then
    failed. On the console the client treats such descriptors as borrowed and never closes
    them; the server remains the owner.
- **`realpath()` fails on the console's filesystem**, which left every server-side file
  descriptor without a recorded path (and so nothing to reopen). The server keeps the
  absolute path it was given when `realpath` fails.
- **`st_dev` and `st_ino` are reported as 0.** Wine records an unpopulated DOS drive slot
  as dev 0 / ino 0, so a real directory "matched" the first empty slot and
  `/app0/test.exe` was mapped to a non-existent drive `A:`. Empty slots are no longer
  eligible; with no real match Wine falls back to its `\??\unix\` path form, which needs
  no drive letters.
- **A read-only `argv`.** `find_drive_nt_root()` edits its argument in place. The
  bootstrap passes the program path as a string literal in read-only data, so the first
  write faulted. It is given a writable copy.
- **`get_image_address()`** walked a Linux-style auxiliary vector via `getauxval()`,
  which does not yield a usable program-header pointer here. It reports "unknown" on the
  console; the value only feeds an optional field for debuggers.
- **`apisetschema.dll`** must be staged beside the other PE builtins.

### Working with the runtime

Two additions make the test loop much shorter:

- Wine's debug output is also appended to a log file on the console's writable storage,
  so it can be read back over FTP rather than captured live from the kernel log.
- A console title has no environment in which to set `WINEDEBUG`. The same syntax is read
  from a small text file on writable storage instead, so debug channels can be changed by
  replacing one file — no rebuild and no reinstall, only a relaunch.

### Known gaps

- No prepared prefix yet (registry, `system32` contents); the runtime runs in bootstrap
  mode. `locale_init` reports a failure for the same reason.
- `opengl32.dll` is not staged, which is what currently stops `wined3d` → `dxgi` →
  `d3d11` from loading.
- The cause of the `dup()` refusal is unknown.
- `wine-ps4-all-changes.patch` in this repository reflects the current tree, **including
  bring-up instrumentation** (the file-logging probes). It omits two large
  machine-generated Vulkan thunk sources; regenerate those with the upstream generator.

## Update: a Windows program runs to completion on hardware

Verified on real hardware, quoting Wine's own log. A freestanding Windows x64 test
program — a D3D11 diagnostic that first probes heap and loader APIs — now runs from
its entry point to `ExitProcess`:

```
process_attach (d3d11.dll) - END                          all 15 modules initialised
Loaded L"unix\\app0\\winprobe.dll" at 00006FFFFBF00000: native
MODULE_InitDLL (winprobe.dll, PROCESS_ATTACH) - RETURN 1
LdrUnloadDll (winprobe.dll) … unloading
trace:dxgi:CreateDXGIFactory1 …
warn:d3d:wined3d_init Failed to create adapter.
warn:dxgi:dxgi_factory_create Failed to initialize factory, hr 0x887a0004
trace:module:LdrShutdownProcess ()
MODULE_InitDLL (… , PROCESS_DETACH) - RETURN 1            every module, in order
```

What that covers:

- the program's full import closure — 14 DLLs — loads, and every `DllMain` returns success
- `HeapCreate` / `HeapAlloc` / `HeapValidate` / `HeapSize` / `HeapReAlloc` / `HeapFree` /
  `HeapDestroy` on both a private heap and the process heap
- `LoadLibraryA` of a native DLL beside the executable, `GetProcAddress` through its
  export table, a call into it, and `FreeLibrary`
- `CreateDXGIFactory1` reaching `wined3d`, which reports "no adapter" and returns a
  proper `DXGI_ERROR_UNSUPPORTED` — there is no GPU backend wired up yet
- `ExitProcess` and an orderly `PROCESS_DETACH` through all modules, with no fault

### Fixes since the previous update

- **Signal frames were read with the wrong layout.** The port had selected Wine's Linux
  context macros (`uc_mcontext.gregs[]`) for the console, but the kernel delivers a
  FreeBSD-style frame. A raw dump taken in the handler on hardware gave the real layout:
  a 16-byte signal mask, 48 bytes of padding, then the machine context at **offset
  0x40** — `mc_trapno` 0xc0, `mc_addr` 0xc8, `mc_err` 0xd8, `mc_rip` 0xe0, `mc_rsp` 0xf8,
  the FXSAVE area at 0x140, `mc_len` 0x480. Until then Wine read the fault *address* as
  the trap number, so every fault was reported as "unexpected trap N". The macros now use
  the real layout, with compile-time assertions on those offsets. This is what makes
  copy-on-write pages, guard pages and structured exceptions work.
- **Locale data.** `get_nls_file_path()` and `NtInitializeNlsFiles()` built their paths
  from Wine's data directory, like the casemap loader fixed earlier. With `locale.nls`
  unavailable, `kernelbase` read through a NULL pointer in its initialiser. They now use
  the packaged location.
- **`opengl32` has no Unix half on the console** (there is no OpenGL), yet `wined3d.dll`
  links against `opengl32.dll`, whose `DllMain` refuses to continue unless its Unix
  library loads. `build-ps4-module/opengl32_ps4_stub.c` is a stand-in: it lets the DLL
  initialise and answers every GL entry point with `STATUS_NOT_SUPPORTED`. It is built
  like any other module and needs a PRX export descriptor naming
  `__wine_unix_call_funcs` (`gen_prx_descriptor.py`).
- **Staging.** The program's import closure was computed on the build host
  (`objdump -p`, recursively) and staged in one pass rather than one missing DLL per run.

### A note on testing against real Windows semantics

The test program originally allocated from a heap immediately after `HeapDestroy` and
expected `NULL`. A loader that tracks heap handles can return that; Windows unmaps the
heap, so the access faults — and Wine reproduces Windows here. That check is compiled out
for this run. It is a useful reminder that Wine brings Windows' actual behaviour,
including its undefined behaviour.

### Next: graphics

Everything remaining for this program is on the graphics path:

1. Select `wined3d`'s Vulkan renderer (it currently tries its OpenGL adapter).
2. Give the Unix half of `win32u` a PRX export descriptor; without it no window or
   graphics call can reach the Unix side
   (`load_builtin_unixlib … win32u.so: module has no export descriptor`).
3. Resolve the remaining unresolved imports in `libvulkan.prx` so RADV loads.
4. Finish the RADV submission backend, which still has unimplemented paths.
5. Link the native presentation/input/audio layer into the bootstrap so frames can reach
   the screen.

## Update: graphics bring-up — a Vulkan device through Direct3D 11

The test program's `D3D11CreateDeviceAndSwapChain` now gets as far as creating the swap
chain on the console's video output. On hardware, in order:

- `win32u`'s Unix half loads as a PRX and binds its 131 imports from `ntdll`.
- RADV (`libvulkan.prx`, Mesa's "orbis" platform) loads, runs its constructors, and
  `vkCreateInstance` succeeds. The physical device enumerates as
  `PlayStation 4 Liverpool (RADV ORBIS)`, `1002:9920`, Vulkan 1.4.
- Wine registers a GPU and a monitor, `wined3d` selects its Vulkan renderer, and the
  `winevulkan` PE-to-Unix thunks work.
- `wined3d`'s Vulkan adapter initialises and **`vkCreateDevice` succeeds**.
- Wine creates a second Windows thread (`wined3d`'s command stream) and it runs.
- A window surface is created, a surface format is selected, and
  `vkCreateSwapchainKHR` reaches `sceVideoOutRegisterBuffers`.

**Current blocker:** `sceVideoOutRegisterBuffers` returns `0x80290005` (invalid
resolution). The test window's client area is 118x2 and the swap chain inherits it;
video-out only accepts real display modes.

### What had to be fixed

- **The linker script dropped prioritised constructors.** `link_ps4mod.x` only kept
  `*(.init_array)`, so every `.init_array.<N>` section was orphaned and never ran. It
  now keeps `SORT_BY_INIT_PRIORITY(.init_array.*)` first. This silently broke anything
  relying on `__attribute__((constructor(N)))`.
- **`create-fself` can tag an import with the wrong library** when more than one input
  library mentions the symbol (a library that merely *imports* a name can be chosen).
  The console then reports `PRX_NOT_RESOLVED_FUNCTION` in klog. `verify_imports.py`
  checks every import's tag against the libraries that actually define it; convert each
  module against a directory containing only the libraries it should import from
  (`win32u_stub.c` is the stand-in used when converting `winevulkan`).
- **Self-imports are mis-bound.** A module that imports a symbol it also defines can be
  bound to the wrong object by the console's loader. RADV is linked `-Bsymbolic`.
- **`dlopen`/`dlsym`/`dlclose` bind to the libkernel stub.** `win32u` reaches them
  through `--wrap` into the port's own module loader (`ps4_dl_forward.c`).
- **RADV needs real per-thread TLS.** `ps4_tls_block_shim.c` gives each thread its own
  block from a static pool and serves `__tls_get_addr` from it.
- **`libvulkan_shim.c`** exports the loader entry points RADV's ICD does not
  (`vkGetInstanceProcAddr`, `vkGetDeviceProcAddr`), supplies a program name (Mesa calls
  `strlen` on it), and replaces `regexec`, which faults on the console for a reason not
  yet understood (driconf application matching is skipped as a result).
- **Presentation is `VK_EXT_headless_surface`.** RADV on this console has no
  `VK_KHR_display`; its headless surface type is wired to `sceVideoOut`. Wine's console
  display driver now maps `VK_KHR_win32_surface` to it instead of to a display plane.
- **Only R-first surface formats exist** (the scan-out buffer is A8B8G8R8). `wined3d`
  falls back from `B8G8R8A8` to `R8G8B8A8` and converts on present.
- **Thread creation with a shared descriptor table.** Because `dup()` is refused, the
  in-process server uses the client's descriptor numbers directly. The client must not
  close the new thread's request pipe or reply pipe after handing them over, and the
  server tolerates `F_SETFL` being refused on the request pipe.
- **Signal frames use the FreeBSD/PS4 `mcontext` layout**, not Linux's `gregs[]`.
- `HKCU\Software\Wine\Direct3D\renderer` defaults to `vulkan`.

### Next

1. Give the swap chain a resolution video-out accepts (present the window through a
   display-sized swap chain).
2. First presented frame.
3. Relink the remaining modules with the corrected linker script.
