#!/usr/bin/env bash
# Build libvulkan_ps4 with the PcPs4 hardware patch applied.
#
# The vendor driver (PS4-OpenGNM/vulkan-ps4) was only ever tested on a host
# with an ordinary heap. On a real PS4 it needs GPU-visible memory for command
# buffers and shader code, relocated shader addresses, valid submit arguments
# and a completion wait. See gpulab/patches/vulkan-ps4-hardware.patch.
#
# usage: build_vulkan_ps4_hw.sh <vulkan-ps4 checkout> <prebuilt libvulkan_ps4.a> <out.a>
#
# Only the four patched files are recompiled with the local compiler and swapped
# into a prebuilt vendor archive (vk_ps4_dispatch.c does not compile with newer
# Clang, and does not need to change).
set -euo pipefail
SRC="${1:?vulkan-ps4 checkout}"; PREBUILT="${2:?prebuilt libvulkan_ps4.a}"; OUT="${3:?output .a}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
: "${OO_PS4_TOOLCHAIN:?set OO_PS4_TOOLCHAIN}"
AR="${AR:-llvm-ar}"

cd "$SRC"
git apply --check "$HERE/gpulab/patches/vulkan-ps4-hardware.patch" 2>/dev/null \
  && git apply "$HERE/gpulab/patches/vulkan-ps4-hardware.patch" || echo "(patch already applied)"
rm -f src/*.o
for f in entry pipeline queue swapchain; do
  make -f Makefile.orbis "src/vk_ps4_$f.o" CC="clang -Wno-incompatible-pointer-types"
done
TMP="$(mktemp -d)"
for f in entry pipeline queue swapchain; do cp "src/vk_ps4_$f.o" "$TMP/vk_ps4_$f.orbis.o"; done
cp "$PREBUILT" "$OUT"
( cd "$TMP" && "$AR" r "$OLDPWD/$OUT" vk_ps4_entry.orbis.o vk_ps4_pipeline.orbis.o vk_ps4_queue.orbis.o vk_ps4_swapchain.orbis.o ) 2>/dev/null \
  || ( cd "$TMP" && "$AR" r "$OUT" vk_ps4_entry.orbis.o vk_ps4_pipeline.orbis.o vk_ps4_queue.orbis.o vk_ps4_swapchain.orbis.o )
echo "built $OUT"
