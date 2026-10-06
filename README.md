# PcPs4

PcPs4 is a compatibility layer for bringing PC (Windows) software to PS4
homebrew. It has two halves:

- **Source translation** — C/C++ written against Win32/MSVC/D3D11 is rewritten
  onto PcPs4 compatibility calls and built with the OpenOrbis PS4 toolchain into
  an installable `.pkg`.
- **Binary loading** — a PE (Windows `.exe`) loader that parses a 64-bit
  Windows binary, maps it into memory, binds its imports to PcPs4-provided
  handlers, and executes it on the console.

## Verified on real hardware

On a jailbroken PS4 (GoldHEN), the current package:

| Test | Result |
|------|--------|
| App launches (package contents correct) | ✅ |
| Translated Win32 source runs (timing, atomics, memory, process info) | ✅ |
| A PE binary is loaded into executable memory and run, returning the expected value | ✅ |
| Interactive D3D11/XInput demo builds into the installable package | ✅ |
| Interactive D3D11/XInput demo verified on PS4 hardware | ⏳ pending hardware test |

## Status screen and log

When launched, PcPs4 shows its own results on the TV:

- one bar per test — **yellow** while running, **green** pass, **red** fail;
- a top banner that turns fully green or red when all tests finish;
- the app stays on the result screen (close it from the PS button menu).

Each step is also written, flushed line by line, to `/data/pcps4_log.txt`
(retrievable over FTP). If the app ever crashes, the last line shows which step
it reached.

## Components

| Path | What it does |
|------|--------------|
| `tools/pcps4_translate.py` | Source translator: rewrites Win32/MSVC/D3D11 usage, reports unsupported subsystems |
| `compat/pcps4_sys.[ch]` | Win32 OS façade on OpenOrbis libkernel (files, threads, mutexes, virtual memory, timing) |
| `compat/pcps4_compat.h` | Win32/MSVC types and API surface used by translated code |
| `compat/pcps4_pe.[ch]` | PE32+ loader: parse, map + base relocations, import resolver and IAT binding |
| `compat/pcps4_loader.[ch]` | Executes a loaded PE in executable memory on the console; refuses if any import is unresolved |
| `compat/pcps4_shader.[ch]` | DXBC (D3D shader bytecode) reader: container validation, shader model, input/output signatures, resource bindings (RDEF) |
| `compat/pcps4_d3d11.[h/cpp]` | D3D11-style façade: device, context, buffers, textures, render targets, swap chain, input layouts |
| `compat/pcps4_xinput.[h/c]` | XInput compatibility bridge mapped onto OpenOrbis DualShock 4 input and rumble |
| `compat/pcps4_gpu.[ch]` | GPU resource layer behind the D3D11 façade |
| `compat/pcps4_gfx.[ch]` | VideoOut framebuffer backend (clear, rects, present) |
| `compat/pcps4_gnm*.[ch]` | GNM submission boundary and optional OpenGNM renderer |
| `compat/pcps4_fault.[ch]` | Fatal-signal handler: terminates the app cleanly instead of continuing on corrupted state |
| `compat/pcps4_status.[ch]` | On-console status screen and log file |

## What the translator handles

Headers: `windows.h`, `synchapi.h`, `winsock2.h`, `ws2tcpip.h` →
`pcps4_compat.h`; `d3d11.h`, `dxgi.h`, `d3dcommon.h` → `pcps4_d3d11.h`.

APIs: `Sleep`, `GetTickCount[64]`, `GetCurrentProcessId`,
`GetCurrentThreadId`, `InterlockedIncrement/Decrement`, `CreateFileA`,
`ReadFile`, `WriteFile`, `CloseHandle`, `CreateThread`, `CreateMutexA`,
`WaitForSingleObject`, `ReleaseMutex`, `VirtualAlloc`, `VirtualFree`,
`WSAStartup`, `WSACleanup`, `WSAGetLastError`, `closesocket`, plus MSVC string
helpers (`_stricmp` etc.), common Win32 types and calling-convention
annotations. XInput calls `XInputGetState`, `XInputSetState`,
`XInputGetCapabilities`, and `XInputEnable` are translated to the
PS4 controller bridge.

The scanner masks comments and string literals before matching, so text inside
them is never rewritten.

### Reported as unsupported (strict mode stops the build)

DirectX 12, D3DCompile/D3DReflect, DXGI factory/adapter APIs, advanced Winsock
(`WSARecv`, `WSASend`, …), `LoadLibrary`/`GetProcAddress`, the registry, extra
Win32 file APIs (`GetFileSize`, `FindFirstFile`, …), events/semaphores, wide
(`…W`) file/mutex APIs, and `D3D11CreateDevice`. A blocker means that subsystem
needs a real port rather than an unsafe rewrite.

## Package contents

The `.pkg` contains, matching the OpenOrbis samples:

    eboot.bin
    sce_sys/param.sfo
    sce_sys/icon0.png
    sce_sys/about/right.sprx
    sce_module/libc.prx
    sce_module/libSceFios2.prx

`sce_module/libc.prx` is required: without it the app cannot start and the PS4
reports it as corrupted.

## Build

    export OO_PS4_TOOLCHAIN=/path/to/OpenOrbis-PS4-Toolchain
    make test        # translator tests (Python)
    make test-host   # PE loader + DXBC reader tests (host C, -Werror)
    make             # build the .pkg (same as: make pkg)

Output: `build/package/IV0000-PCPS00001_00-PCPS4TRANSLATE01.pkg`.

GitHub Actions runs both test suites and uploads the package as the
`PcPs4-PKG` artifact on every push.

## Interactive D3D11/XInput demo

The default build now launches an ordinary PC-style compatibility demo after
the existing startup tests:

- D3D11 device/swap-chain creation;
- input layout + dynamic vertex buffer;
- `IASetPrimitiveTopology` + `Draw`;
- software triangle rasterization into the CPU back buffer;
- XInput left-stick/D-pad movement, Options to exit, and A-button rumble;
- `Present` scales the 640x360 back buffer to the 1920x1080 VideoOut target.

Set `PCPS4_RUN_GAME=0` to return to the old status-screen-only behaviour.

## Open work

- **Graphics:** the CPU backend currently covers a deliberately small D3D11
  subset: POSITION/COLOR float vertex layouts, triangle-list `Draw`, and
  triangle-list `DrawIndexed`. The optional OpenGNM renderer
  (`PCPS4_ENABLE_OPENGNM=1`) remains the route for real PS4 GPU execution.
- **Shaders:** DXBC is read and validated, and the OpenGNM path can consume a
  pre-converted `.sb` shader cache. Automatic DXBC -> PS4 shader compilation
  still needs the external compiler toolchain integrated into packaging.
- **Win32 runtime coverage:** real games import far more APIs than are
  implemented; the loader's unresolved-import count lists exactly what a given
  binary still needs. Imports by ordinal are not resolved yet.

OpenOrbis: https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain
