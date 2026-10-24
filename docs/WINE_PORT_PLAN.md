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

## Scope honesty
This is a large, multi-stage port — not a one-shot package. But it is now
*scoped and de-risked*: both sides are measured, the host surface is ~complete,
the process model exists, and the only open unknown (syscall numbers) has a
direct reference in the decompiled OrbisOS. Build it incrementally (ntdll host
shim first → a trivial Win32 console PE → then DXVK games).
