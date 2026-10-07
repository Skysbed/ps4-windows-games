#!/usr/bin/env bash
# Build the PcPs4 GPU Lab package on macOS (or Linux).
#
# Prereqs (see project notes): OpenOrbis toolchain extracted, the OpenGNM stack
# checked out (vendor examples, opengnm, opengnm-psbc, Vulkan-Headers), a built
# host `opengnm-psbc`, and the PS4-target libraries libopengnm.a,
# libpsbc.orbis.a, libvulkan_ps4.a in one directory.
#
# Override any of these through the environment.
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"

: "${OO_PS4_TOOLCHAIN:?set OO_PS4_TOOLCHAIN}"
STACK="${STACK:-/tmp/ogs}"
export VENDOR="${VENDOR:-$STACK/freegnm-examples}"
export OGNM="${OGNM:-$STACK/opengnm}"
export PSBC_INC="${PSBC_INC:-$STACK/opengnm-psbc/libpsbc}"
export VKHDR="${VKHDR:-$STACK/Vulkan-Headers}"
export LIBS="${LIBS:?set LIBS to the dir with libopengnm.a libpsbc.orbis.a libvulkan_ps4.a}"
PSBC="${PSBC:-$STACK/opengnm-psbc/opengnm-psbc}"

case "$(uname -s)" in
  Darwin) TOOLS="$OO_PS4_TOOLCHAIN/bin/macos"; FSELF="$TOOLS/create-fself-macos" ;;
  *)      TOOLS="$OO_PS4_TOOLCHAIN/bin/linux"; FSELF="$TOOLS/create-fself" ;;
esac
export DOTNET_BUNDLE_EXTRACT_BASE_DIR="${DOTNET_BUNDLE_EXTRACT_BASE_DIR:-/tmp/dotnet_extract}"
mkdir -p "$DOTNET_BUNDLE_EXTRACT_BASE_DIR"
export DOTNET_EnableWriteXorExecute=0

cd "$HERE/gpulab"
make "$@" \
  CC="clang -Wno-incompatible-pointer-types" LD=ld.lld GLSLC=glslc PSBC="$PSBC" \
  MKFSELF="$FSELF" MKGP4="$TOOLS/create-gp4" PKGTOOL="$TOOLS/PkgTool.Core" \
  RUNTIME_MODULE_DIR="$OO_PS4_TOOLCHAIN/src/modules" all
ls -la ./*.pkg
