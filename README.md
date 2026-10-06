# PcPs4

PcPs4 is a source-level translation layer for moving PC-oriented C/C++ projects toward PS4 homebrew builds.

It takes source written against a supported subset of Windows/MSVC APIs, rewrites that source into PcPs4 compatibility calls, and then sends the translated sources through the OpenOrbis PS4 Toolchain.

## Pipeline

    PC / Windows-style C or C++
                |
                v
       pcps4_translate.py
                |
                v
       PcPs4 compatibility layer
                |
                v
       OpenOrbis Clang / Clang++
                |
                v
           ld.lld + ELF
                |
                v
             create-fself
                |
                v
             eboot.bin
                |
                v
             create-gp4
                |
                v
           PkgTool.Core
                |
                v
              .pkg

The default `make` target builds the .pkg. The package is generated from eboot.bin and sce_sys/param.sfo using OpenOrbis's GP4/package tooling.

This is source translation, not binary conversion. An arbitrary Windows .exe is not magically portable to PS4 because its executable format, imports, graphics APIs, filesystem APIs, ABI and runtime may all depend on Windows.

## What translates now

Headers:
- windows.h -> pcps4_compat.h
- synchapi.h -> pcps4_compat.h

Timing/process:
- Sleep(ms) -> pcps4_sleep_ms(ms)
- GetTickCount64() -> pcps4_monotonic_ms()
- GetTickCount() -> 32-bit cast of pcps4_monotonic_ms()
- GetCurrentProcessId() -> pcps4_get_current_process_id()

Atomics:
- InterlockedIncrement/Decrement -> compiler atomic wrappers

Common Win32/MSVC types:
- DWORD, WORD, BYTE
- UINT, UINT64, ULONG
- LONG, LONGLONG, ULONGLONG
- HRESULT, BOOL, INT
- CHAR, WCHAR
- LPCSTR, LPSTR, LPVOID, PVOID, SIZE_T
- __int64

MSVC annotations:
- WINAPI, __stdcall, __cdecl, __fastcall
- supported __declspec(dllimport/dllexport/nothrow)

String/memory compatibility:
- _stricmp/_strnicmp/stricmp/strnicmp -> strcasecmp/strncasecmp
- ZeroMemory/RtlZeroMemory -> memset
- CopyMemory -> memcpy
- MoveMemory -> memmove

The scanner masks comments and string/character literals before matching, so text such as "Sleep(GetTickCount64())" is not rewritten.

## Blockers

Strict translation reports unsupported Windows subsystems before the compiler runs:

- DirectX headers/APIs
- Winsock
- Win32 GUI/message APIs
- Windows DLL loading
- Windows registry
- Win32 file APIs
- Win32 synchronization APIs

That is intentional. A blocker means the project needs a subsystem-level port rather than an unsafe regex rewrite.

## Multi-file projects

Put PC source in pc_source/:

    pc_source/main.c
    pc_source/runtime.c
    pc_source/game.cpp

The Makefile discovers C and C++ files automatically, translates each one into build/generated/, and links all generated object files.

Translate without linking:

    make translate

Run the translator directly:

    python3 tools/pcps4_translate.py pc_source/main.c \
      --out build/generated/main.c \
      --report build/translation.json \
      --strict

## Shader pipeline

For D3D Shader Model 5.x bytecode, use:

    python3 tools/pcps4_shader_pipeline.py \
      --kind dxbc --stage vertex \
      --input game.vert.dxbc \
      --output build/game.vert.sb \
      --dxbc-compiler /path/to/dxbc_compiler \
      --psbc /path/to/opengnm-psbc

The pipeline is intentionally explicit: `dxbc-spirv` handles the D3D shader front-end, then `opengnm-psbc` targets the PS4 base/GFX7 profile. Neither step pretends that a DXBC blob can be executed directly by the PS4 GPU.

## Build

Install OpenOrbis and set its root:

    export OO_PS4_TOOLCHAIN=/path/to/OpenOrbis-PS4-Toolchain

Then:

    make test
    make translate
    make
    # same as: make pkg

Outputs:

    build/generated/*.c
    build/generated/*.cpp
    build/translation.json
    build/PcPs4.elf
    build/PcPs4.oelf
    eboot.bin
    build/package/IV0000-PCPS00001_00-PCPS4TRANSLATE01.pkg

GitHub Actions uploads the .pkg as the `PcPs4-PKG` artifact on a successful build.

The OpenOrbis integration follows the upstream sample build model: PS4-compatible x86-64 compilation, the OpenOrbis linker script/libraries, and create-fself for eboot generation.

## Next translation work

The architecture is ready for deeper adapters. The next substantial layer is filesystem/path handling, threading/synchronization, socket translation, and eventually a graphics abstraction for PC projects that currently talk directly to DirectX.

OpenOrbis: https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain


## CI

The normal build workflow intentionally uses the stable VideoOut fallback so optional GPU dependencies do not make ordinary commits unbuildable. The separate graphics workflow exercises the real OpenGNM renderer and shader compiler.
