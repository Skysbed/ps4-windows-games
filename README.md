# PcPs4

PcPs4 is a Linux-first source compatibility/build layer for compiling portable C/C++ programs into PS4 homebrew executables with the OpenOrbis PS4 Toolchain.

## Build pipeline

PC C/C++ source -> PcPs4 compatibility layer -> OpenOrbis Clang/LLD -> ELF -> create-fself -> eboot.bin

This is a source-level compatibility layer. It does not convert arbitrary Windows .exe files.

## Requirements

- Linux
- make
- clang + lld
- OpenOrbis PS4 Toolchain
- `OO_PS4_TOOLCHAIN` set to the OpenOrbis toolchain root

OpenOrbis: https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain

## Build

```bash
export OO_PS4_TOOLCHAIN=/path/to/OpenOrbis-PS4-Toolchain
make
```

Outputs:

```
eboot.bin
build/PcPs4.elf
build/PcPs4.oelf
```

Diagnostics:

```bash
make info
```

Clean:

```bash
make clean
```

The Makefile follows the structure used by OpenOrbis' Linux samples: Clang targets `x86_64-pc-freebsd12-elf`, the OpenOrbis linker script is used for linking, and `create-fself` produces the PS4 eboot.

## Layout

- `src/main.c` - sample app
- `compat/pcps4_compat.h` - portability helpers
- `Makefile` - OpenOrbis build
- `tools/pcps4_build.py` - diagnostics/build wrapper

## Roadmap

1. Expand Windows/POSIX API translation.
2. Add filesystem, timing, threading and socket abstractions.
3. Add a source scanner/rewriter for larger projects.
4. Add automatic OpenOrbis project generation.
5. Add GP4/package generation once application metadata/assets are supplied.

The project is intended for development and homebrew source projects where the source can legally be built for the target.
