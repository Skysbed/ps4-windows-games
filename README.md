# PcPs4

PcPs4 is a source-level translation layer that takes PC-oriented C/C++ source, rewrites a supported subset of Windows/MSVC APIs into portable PcPs4 compatibility calls, and then builds the translated result with the OpenOrbis PS4 Toolchain.

## The pipeline

    PC / Windows-style C/C++
            |
            v
    tools/pcps4_translate.py
            |
            v
    PcPs4 compatibility layer
            |
            v
    OpenOrbis Clang -> ld.lld
            |
            v
    ELF -> create-fself -> eboot.bin

The translator is intentionally source-level. It does not claim to convert an arbitrary Windows .exe. Code that depends on DirectX, WinSock, Win32 GUI, registry, DLL loading, or other unsupported subsystems is reported as a blocker instead of being silently broken.

## Currently translated

- windows.h / synchapi.h -> pcps4_compat.h
- Sleep(ms) -> pcps4_sleep_ms(ms)
- GetTickCount64() -> pcps4_monotonic_ms()
- GetTickCount() -> 32-bit cast of pcps4_monotonic_ms()
- GetCurrentProcessId() -> pcps4_get_current_process_id()
- InterlockedIncrement/Decrement -> compiler atomic wrappers
- common Win32 scalar types such as DWORD, BOOL, UINT, LONG, HRESULT
- MSVC calling conventions/storage annotations such as WINAPI and __stdcall
- _stricmp/_strnicmp/stricmp/strnicmp -> POSIX string comparison
- ZeroMemory/RtlZeroMemory/CopyMemory/MoveMemory -> libc memory operations

Comments and string/character literals are protected from rewriting.

## Unsupported APIs

The translator detects and reports, among others:

- DirectX headers/APIs
- Winsock
- Win32 window/message APIs
- Windows DLL loading
- Windows registry
- Win32 file APIs
- Win32 synchronization APIs

Strict mode makes these blockers a hard build failure.

## Use the translator directly

    python3 tools/pcps4_translate.py pc_source/main.c \
      --out build/generated/main.c \
      --report build/translation.json \
      --strict

## Build the app

Set the OpenOrbis toolchain root:

    export OO_PS4_TOOLCHAIN=/path/to/OpenOrbis-PS4-Toolchain

Then:

    make test
    make translate
    make

The build produces:

- build/generated/main.c
- build/translation.json
- build/PcPs4.elf
- build/PcPs4.oelf
- eboot.bin

The Makefile follows the OpenOrbis sample flow: Clang targets x86_64-pc-freebsd12-elf, the OpenOrbis linker script and libraries are used for the ELF, and create-fself creates eboot.bin.

## Roadmap

1. Multi-file translation.
2. More Win32 filesystem/threading/process APIs.
3. Socket/network translation.
4. DirectX-to-PS4 graphics abstraction.
5. Automatic project discovery and dependency mapping.
6. GP4/package generation.

OpenOrbis: https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain
