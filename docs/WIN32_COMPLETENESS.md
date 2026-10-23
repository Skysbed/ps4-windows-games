# Win32 completeness — using Wine as a reference (not a replacement)

## Decision
Keep PcPs4's architecture: our PE/DLL loader, the import resolver, the MS-ABI
bridge, and `vulkan-ps4`/GNM. Do **not** rip-and-replace with Wine. Instead use
Wine (and FreeBSD 9) as the **reference implementation** to progressively replace
our no-op stubs with correct behaviour, so real games work. OrbisOS is a FreeBSD 9
fork and we have it decompiled, so for each missing piece we can see what OrbisOS
already provides (libkernel/libSce exports) and what to backfill from FreeBSD 9 /
adapt from Wine.

## Where we are
- ~181 real Win32 handlers registered (compat/pcps4_winapi.c + pcps4_dxvk_support.c) —
  enough for DXVK's clear/triangle tests.
- Every other import binds to `pcps4_permissive_import_stub` → returns 0 (no-op).
  Great for getting a PE to load and reach the GPU path; fatal for a real game the
  moment a load-bearing API silently no-ops.

## What this does NOT fix
The current on-device blocker is vertex-attribute fetch in the GNM driver
(`vulkan-ps4`), which lives *below* Win32. Wine is irrelevant to that. Win32
completeness is the **next** axis — what gates real games once geometry renders.

## Methodology (per missing API)
1. From the decompiled OrbisOS + the `.sprx` exports, find the OrbisOS primitive
   that backs it (scePthread*, sceKernelMmap, sceKernel file I/O, sceKernelGetProcessTime, ...).
2. Take Wine's implementation of the Win32/NT function as the reference for the
   *contract* (args, return values, error codes, edge cases).
3. Implement it on top of the OrbisOS primitive (MS-ABI). Where OrbisOS lacks a
   libc routine Wine's code needs, backfill from FreeBSD 9 source.
4. Keep the permissive stub only as the last-resort fallback for genuinely
   unused imports — log every stub hit so real games reveal their real needs.

## Priority tiers (replace stubs with Wine-referenced real impls)
Tier 1 — a no-op here corrupts a real game immediately:
- Memory: VirtualAlloc/Free/Protect/Query, HeapAlloc/Free/ReAlloc (→ sceKernelMmap/ munmap, our heap). Wine: dlls/ntdll/unix/virtual.c, dlls/kernelbase/memory.c.
- File I/O: CreateFileW, ReadFile, WriteFile, SetFilePointer, GetFileSize, CloseHandle (→ sceKernel open/read/write/lseek/close). Wine: dlls/ntdll/unix/file.c, dlls/kernelbase/file.c.
- Sync: CreateEvent/SetEvent/ResetEvent, CreateMutex, WaitForSingleObject/MultipleObjects, InitializeCriticalSection/Enter/Leave, Sleep (→ scePthread mutex/cond, sceKernelUsleep). Wine: dlls/ntdll/unix/sync.c.
- Threads/TLS: CreateThread (already have an MS-ABI trampoline), TlsAlloc/Get/Set, GetCurrentThreadId. Wine: dlls/kernel32/thread.c, dlls/ntdll/thread.c.
- Time: QueryPerformanceCounter/Frequency, GetTickCount(64), GetSystemTimeAsFileTime (→ sceKernelGetProcessTime / TSC). Wine: dlls/ntdll/unix/sync.c, dlls/kernelbase/datetime.c.

Tier 2 — needed soon for most games:
- Module/proc: GetModuleHandleW, GetProcAddress, LoadLibraryW (we have loader hooks), GetModuleFileNameW.
- Strings/locale: lstrcmp, Multi/WideCharToMultiByte, CompareString.
- Error state: GetLastError/SetLastError (per-thread), FormatMessage.
- Env/cmdline: GetCommandLineW, GetEnvironmentVariableW, GetModuleHandleEx.

Tier 3 — windowing / input / audio (per-game, as they appear):
- user32 message loop: RegisterClass/CreateWindow (we have minimal), GetMessage/PeekMessage/DispatchMessage, Def WindowProc. Wine: dlls/user32.
- Input: XInput (xinput1_*.dll → PS4 pad via scePad), DirectInput. Wine: dlls/xinput1_3, dlls/dinput.
- Audio: XAudio2 / DirectSound → PS4 audio (sceAudioOut). Wine: dlls/xaudio2_7, dlls/dsound.

## Graphics coverage (separate, sits on vulkan-ps4)
The API→Vulkan translators all layer on our GNM Vulkan floor the way DXVK does:
- DXVK (D3D9/10/11) — in use.
- vkd3d-proton (D3D12) — add when a D3D12 title needs it.
- Zink (OpenGL) — add for GL titles.
These are not Win32 work; they're additional guest DLLs the loader bundles.

## Open architectural question
Wine normally relies on a separate `wineserver` process plus freely spawning
child processes, which OrbisOS's app sandbox restricts. We are NOT adopting that
model wholesale. We take Wine's *per-API logic* (single-process, our resolver),
not its process/server runtime — unless a specific game forces multi-process
semantics, which we'd evaluate against the decompiled OrbisOS then.
