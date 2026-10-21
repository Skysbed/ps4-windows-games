#!/usr/bin/env bash
# Build the "DXVK First Light" package: PcPs4 + DXVK's real d3d11/dxgi DLLs +
# a tiny Windows test program, running on the PS4 GPU through vulkan-ps4.
#
# Prereqs (see project notes): OpenOrbis toolchain extracted ($OO_PS4_TOOLCHAIN),
# a directory ($VK_LIBS) with libvulkan_ps4.a (hardware-patched, see
# gpulab/patches), libpsbc.orbis.a and libopengnm.a, Vulkan-Headers, mingw-w64
# (to build the test program) and a DXVK 1.10.3 release unpacked in $DXVK_DIR.
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
: "${OO_PS4_TOOLCHAIN:?}"; : "${VK_LIBS:?}"; : "${DXVK_DIR:?dxvk-1.10.3 directory}"
VKHDR="${VKHDR:-/tmp/ogs/Vulkan-Headers}"
OUT="${OUT:-$HERE/build/dxvk}"
GAME="$OUT/game"
mkdir -p "$GAME"

# The bundled test program + the DXVK DLLs it needs are overridable so we can
# swap in different demos (D3D11 clear, D3D9 triangle, ...) without editing this.
GAME_SRC="${GAME_SRC:-$HERE/tests/win_dxvk_clear.c}"
GAME_LIBS="${GAME_LIBS:--ld3d11 -luser32 -lkernel32 -ldxguid}"
GAME_DLLS="${GAME_DLLS:-d3d11.dll dxgi.dll}"
x86_64-w64-mingw32-gcc -std=c11 -O2 -ffreestanding -fno-stack-protector \
  -fno-asynchronous-unwind-tables -nostdlib -Wl,--entry=WinMainCRTStartup \
  -Wl,--subsystem,windows -Wl,--section-alignment,0x4000 -Wl,--file-alignment,0x200 \
  -o "$GAME/game.exe" "$GAME_SRC" $GAME_LIBS
for _dll in $GAME_DLLS; do cp "$DXVK_DIR/x64/$_dll" "$GAME/"; done

case "$(uname -s)" in
  Darwin) T="$OO_PS4_TOOLCHAIN/bin/macos"; FSELF="$T/create-fself-macos" ;;
  *)      T="$OO_PS4_TOOLCHAIN/bin/linux"; FSELF="$T/create-fself" ;;
esac
export DOTNET_BUNDLE_EXTRACT_BASE_DIR="${DOTNET_BUNDLE_EXTRACT_BASE_DIR:-/tmp/dotnet_extract}"
export DOTNET_EnableWriteXorExecute=0
cd "$HERE"
make -B pkg INTDIR="$OUT" PACKAGE_ROOT="$OUT/package" PCPS4_DXVK=1 PCPS4_BUNDLED_GAME=1 \
  PCPS4_GAME_DIR="$GAME" PCPS4_VK_LIBS="$VK_LIBS" PCPS4_VK_HEADERS="$VKHDR" \
  TITLE="PcPs4 DXVK First Light" TITLE_ID=PCPS00006 CONTENT_ID=IV0000-PCPS00006_00-DXVKFIRSTLIGHT01 \
  CC="clang -Wno-incompatible-pointer-types" CXX="clang++ -Wno-incompatible-pointer-types" \
  CREATE_FSELF="$FSELF" CREATE_GP4="$T/create-gp4" PKGTOOL="$T/PkgTool.Core" -j8
ls -la "$OUT"/package/*.pkg
