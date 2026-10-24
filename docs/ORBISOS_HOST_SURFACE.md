# OrbisOS host surface vs Wine's FreeBSD backend — feasibility (data-backed)

Measured from the real 13.02 firmware libraries (SD card `PS4-OrbisOS-13.02/system/common/lib`):
libSceLibcInternal.sprx (3032 exports), libc.sprx (2567), libkernel.sprx (1053),
libkernel_sys.sprx (1170), libkernel_web.sprx (1053). Exports matched by computing
the Sony NID of candidate names and checking the dynamic symbol tables.

## Conclusion
OrbisOS is a FreeBSD 9 fork and still ships the host surface Wine's Unix backend
needs, so **Wine's FreeBSD-9 code maps onto OrbisOS almost directly** — this is
the "use Wine for FreeBSD" path, validated with data, not a guess. We keep
PcPs4's architecture and bring in Wine's Win32/NT implementation, pointing its
host calls at OrbisOS's own (sce/POSIX) exports.

## Confirmed PRESENT (direct NID match — reliable)
- Process model (the make-or-break for Wine's wineserver/child processes):
  **fork, vfork, execve, _exit, exit, wait4, getpid, getppid, sigaction,
  sigprocmask, sigaltstack, setjmp, longjmp** — all exported.
- File I/O: open, openat, read, write, pread, pwrite, close, lseek, fstat, lstat,
  fstatat, sceKernelStat, unlink, rmdir, rename, readlink, fcntl, dup, dup2,
  ioctl, poll, access, chmod, ftruncate, getcwd, chdir, getdents.
- Memory: mmap, munmap, mprotect, madvise, mlock, sceKernelMmap/Munmap.
- Threads/sync: pthread_create, pthread_mutex_lock, scePthread* (Join, MutexInit,
  CondInit, KeyCreate, Self — Self confirmed by linking it on-device this
  session), nanosleep, clock_gettime, gettimeofday, sched_yield.
- Sockets: socket, bind, connect, listen, send, recv, sendto, recvfrom, setsockopt.
- libc: free, calloc, realloc, memcpy, memset, bcopy (= memmove), strlen, strcpy,
  strcmp, strncmp, snprintf, printf, fprintf, vfprintf, fopen, fwrite, fclose,
  fflush, qsort, getenv, setenv, plus ~3000 more.
- Dynamic linking: sceKernelLoadStartModule, sceKernelDlsym (= dlsym), dlopen,
  dlclose, sceKernelGetModuleInfo.

## Gaps to backfill from FreeBSD 9 source (short list)
The quick scan flags these as missing; note it has FALSE NEGATIVES (scePthreadSelf
was flagged yet is clearly exported), so verify each against a full export dump /
the decompiled native before treating as a real gap. Candidates: malloc (odd,
since free/calloc/realloc are present — likely a wrapper/alias), vsnprintf, fread,
kill, pipe/pipe2, select, getaddrinfo, sbrk, getpagesize. Each is small and either
(a) already present under an exact name, (b) trivially implemented on an existing
primitive (e.g. memmove→bcopy, getpagesize→sysconf, select→poll), or (c) lifted
from FreeBSD 9's libc source.

## What this means for the build
- Wine's host-interface (ntdll's unixlib) is overwhelmingly satisfiable by direct
  calls into OrbisOS's exports. The adaptation is a thin shim mapping Wine's
  expected libc/syscall names to the sce/POSIX names OrbisOS exports, not a
  rewrite.
- The process/server model is viable (fork/execve/sigaction present), so we are
  not forced to collapse Wine into one process — though we may still choose to.
- Graphics stays on our vulkan-ps4/GNM floor; Wine's winevulkan forwards to it,
  DXVK/vkd3d/Zink layer on top (unchanged from DXVK_STATUS/WIN32_COMPLETENESS).

## Next concrete step
Pull Wine's source, isolate its FreeBSD `#ifdef __FreeBSD__` host paths in
dlls/ntdll/unix, and build the name-mapping shim against the confirmed OrbisOS
exports above — backfilling only the short real-gap list from FreeBSD 9 source.
The decompiled native (`PS4-decompiled-native/*.c`) is the reference for any
OrbisOS primitive whose exact contract isn't obvious from its name.
