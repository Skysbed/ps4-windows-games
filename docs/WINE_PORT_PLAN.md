# Wine-on-OrbisOS port plan (data-backed, concrete)

Cross-references Wine's actual Unix host-interface (measured from
`dlls/ntdll/unix/*.c`) against the OrbisOS export surface
(see ORBISOS_HOST_SURFACE.md). Goal: reuse Wine's complete Win32/NT
implementation, pointing its host calls at OrbisOS's own exports — thin shim,
not a rewrite. Keep PcPs4's loader/vulkan-ps4; graphics unchanged.

## Wine's host-interface (from dlls/ntdll/unix, by call frequency)
~40 distinct host functions. Mapping to OrbisOS:

DIRECT (OrbisOS exports the same POSIX name — use as-is):
close, read, write, pread, pwrite, open, openat, fcntl, fstat, lseek, ioctl,
poll, mmap, munmap, mprotect, madvise, fork, _exit, getpid, sigaction,
clock_gettime, gettimeofday, nanosleep, socket, sysctl, mkdir, longjmp,
pthread_create, pthread_mutex_*, pthread_once, pthread_setspecific, pthread_self,
pthread_sigmask, pthread_attr_*.

TRIVIAL SHIM (OrbisOS has an equivalent under another name):
- stat        → fstatat(AT_FDCWD, …) or sceKernelStat
- memmove     → bcopy
- dlsym       → sceKernelDlsym
- getpagesize → sysconf(_SC_PAGESIZE)
- select      → poll
- pipe2/pipe  → sceKernel pipe equivalent (verify exact export)

BACKFILL from FreeBSD 9 libc source (short, verify against full export dump —
the quick NID scan has false negatives):
- malloc/vsnprintf/fread/kill — almost certainly present under exact names or a
  wrapper; confirm, else lift from FreeBSD 9.

## The one real research task: raw syscall numbers
`dlls/ntdll/unix` makes ~74 direct `syscall(SYS_xxx, …)` calls. OrbisOS is a
FreeBSD 9 fork so most SYS_* numbers match, but Sony renumbered/added some. Map
each SYS_* Wine uses to the OrbisOS number using:
- the decompiled OrbisOS native (`/Volumes/Untitled/PS4-decompiled-native/*.c`,
  esp. mini-syscore / libkernel) for the real syscall table, and
- libkernel.sprx, whose exported wrappers (sce* / POSIX) call the right numbers —
  so prefer calling libkernel's wrappers over raw `syscall()` wherever possible,
  which sidesteps most of the renumbering risk.

## Build shape
1. `winertl_host_shim.c`: the ~40-function shim forwarding to OrbisOS exports
   (MS-ABI not needed here; this is the Unix side). Most are 1-line forwards.
2. Select Wine's `__FreeBSD__` paths (37 blocks already exist) — define the
   FreeBSD host variant, add an `__ORBIS__` tweak only where OrbisOS differs.
3. Replace raw `syscall()` with libkernel wrapper calls where available; map the
   rest via the decompiled syscall table.
4. Link Wine's ntdll/kernelbase/etc. PE side through PcPs4's existing loader +
   resolver; winevulkan → vulkan-ps4.

## Stage 1 — BUILT (ntdll host shim + syscall mapping)
`compat/wine_host/pcps4_wine_host.{h,c}`. Compiles clean against the
OpenOrbis cross toolchain (`--target=x86_64-pc-freebsd12-elf`).

What it provides — the FreeBSD host primitives Wine's `__FreeBSD__` paths
reference but the toolchain headers don't declare, each backed by a
confirmed OrbisOS firmware export (NID-verified against libkernel*.sprx /
libc.sprx):
- **GS/FS base = the Windows TEB mechanism.** `sysarch` is exported;
  `amd64_{set,get}_{gs,fs}base()` are reimplemented over it exactly as
  FreeBSD libc does. This is what Wine's signal_x86_64.c uses to point
  GSBASE at the TEB (`amd64_set_gsbase(teb)` / `sysarch(X86_64_SET_GSBASE)`).
- **Thread id** — `thr_self()` routed through the exported
  `pthread_getthreadid_np()` (no raw-syscall dependency).
- **`_umtx_op`** + `struct _umtx_time` + op codes (low-level sync).
- **`kqueue`/`kevent`** + `struct kevent` + `EV_*`/`EVFILT_*` + `EV_SET`
  (Wine's FreeBSD signal/wait loop).

Syscall mapping: resolved, not open. The toolchain's `bits/syscall.h`
carries the stock FreeBSD-9 numbers unchanged (sysarch=165, thr_self=432,
_umtx_op=454, kqueue=362, kevent=363, sigaction=416, mmap=477), and the
decompiled OrbisOS native confirms real `sysarch` use. The shim calls the
**exported wrappers by name**, so Sony renumbering is a non-issue; the
number table is recorded in the `.c` for audit and any future raw fallback.

Verification: `pcps4_wine_host_selftest()` round-trips GS-base set/get,
reads the thread id, creates a kqueue + registers a kevent, and pokes
`_umtx_op` — to prove the primitives work on-device before Wine depends on
them. It is standalone (not yet wired into the loader — that's stage 2),
so nothing already working is touched.

## Stage 2 — BUILT (TEB bring-up + console PE), v82
Three pieces, all compiling against the real cross toolchain and the
mingw PE toolchain:

1. **Wine FreeBSD TEB bring-up on the shim** — `pcps4_wine_host_init()`
   mirrors signal_x86_64.c's `__FreeBSD__` thread init exactly:
   `amd64_set_gsbase(teb); amd64_get_fsbase(&pthread_teb)`, then verifies
   the GSBASE write and runs the self-test. It is ADDITIVE to PcPs4's
   existing instruction-patching TEB scheme (which rewrites `gs:` loads to
   RIP-relative): patched images are untouched, and any unpatched
   Wine-origin code that expects a real GSBASE now also resolves.

2. **Console path** — `GetStdHandle`/`SetStdHandle`/`WriteConsoleA` added
   to kernel32, and `WriteFile` wrapped so a std-handle sentinel routes to
   the PcPs4 runtime log instead of being dereferenced as a file handle.
   Real file handles still go down the existing path. Contracts from Wine
   kernelbase. This makes an ordinary Win32 console program produce
   visible output with no console subsystem.

3. **Trivial console PE** — `tests/win_console_hello.c`: `GetStdHandle ->
   WriteFile -> ExitProcess`, no graphics/DXVK/window. Built with mingw;
   imports resolve to the handlers above. Outcome is encoded in the
   ExitProcess code PcPs4 logs (0x5A00 = alive). It is the floor every
   real game stands on, independent of the GPU/vertex-fetch path.

On-device safety: PcPs4's runtime deliberately avoids GSBASE, and setting
it live risks the OrbisOS thread state, so `pcps4_wine_host_init()` (the
destructive GSBASE install) is NOT called on an ordinary boot. Only the
non-destructive self-test runs, and only when `PCPS4_WINE_SELFTEST=1`
(see pc_source/main.c). Build the console PE as the bundled game with:
`GAME_SRC=tests/win_console_hello.c GAME_LIBS=-lkernel32 GAME_DLLS=`.

## Stage 3 — VERIFIED ON HARDWARE (v83)
The console PE ran on the PS4 (v82): loader resolved GetStdHandle/WriteFile/
ExitProcess and the "[console] ... alive" line was logged. Then the forced
self-test (v83, -DPCPS4_WINE_SELFTEST_FORCE=1) proved the FreeBSD host
primitives on real hardware — all four green, zero failures:
    [wine-host] OK gsbase set/get roundtrip (saved=0)
    [wine-host] OK thr_self tid=102155
    [wine-host] OK kqueue+kevent
    [wine-host] OK _umtx_op reachable
`saved=0` is the key result: OrbisOS leaves the userland GS base at zero, so
pointing GSBASE at the Windows TEB clobbers nothing it relies on — the Wine
FreeBSD TEB mechanism is sound on PS4, de-risking the whole port.

## Stage 4 — IN PROGRESS (Win32 completeness, v84)
Replacing no-op import stubs with real behaviour, Wine contracts on libc/
OrbisOS primitives. Audited the registered surface (~184 handlers): critical
sections, condition variables, VirtualAlloc/Protect/Query, Multi/WideChar
conversion, waits/semaphores already real. Filled the startup/CRT-init gaps
a real game hits first: GetVersion/GetVersionEx, GetStartupInfo, GetACP,
GetCommandLineW, GetEnvironmentStringsW/Free, GetSystemTime/GetLocalTime,
GetNativeSystemInfo, GetModuleHandleExW, OutputDebugStringW, GetExitCodeThread,
SetThreadAffinityMask, SetUnhandledExceptionFilter, InitOnceExecuteOnce,
LocalAlloc/GlobalAlloc/GlobalFree. The v84 test PE calls a cross-section
(GetVersion, GetSystemInfo, GetSystemTime, LocalAlloc, GetACP,
GetModuleHandleExW) and logs pass/fail per check.

Still to backfill as real games reveal them: file mapping (CreateFileMapping/
MapViewOfFile/UnmapViewOfFile), and the per-game user32/input/audio surface.

Next: run the v84 surface test on-device; then rebuild the DXVK path on the
fuller Win32 surface and work a real title's import list with scan_game_imports.py.

## Scope honesty
This is a large, multi-stage port — not a one-shot package. But it is now
*scoped and de-risked*: both sides are measured, the host surface is ~complete,
the process model exists, and the only open unknown (syscall numbers) has a
direct reference in the decompiled OrbisOS. Build it incrementally (ntdll host
shim first → a trivial Win32 console PE → then DXVK games).
