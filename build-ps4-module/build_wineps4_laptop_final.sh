#!/bin/bash
set -e

TC=${HOME}/openorbis-toolchain
COMPAT=${HOME}/src-ps4/orbis-compat

BUILD_DIR=${HOME}/pcps4-work/build-obj/wineps4
OUTPUT_DIR=${HOME}/pcps4-work/pkg-build/sce_module

mkdir -p $BUILD_DIR $OUTPUT_DIR

CC="clang --target=x86_64-pc-freebsd12-elf"

CFLAGS="-isysroot $TC"
CFLAGS+=" -isystem $COMPAT/include"
CFLAGS+=" -isystem ${HOME}/pcps4-work/wine-ps4-shims"
CFLAGS+=" -isystem $TC/include"
CFLAGS+=" -D__PROSPERO__ -D__ORBIS__ -D_BSD_SOURCE=1"
CFLAGS+=" -isystem ${HOME}/pcps4-work/.deps/wine/source-clean/include"
CFLAGS+=" -isystem ${HOME}/pcps4-work/.deps/wine/build-host-tools/include"
CFLAGS+=" -isystem ${HOME}/pcps4-work/.deps/wine/source-clean/dlls/mmdevapi"
CFLAGS+=" -isystem ${HOME}/pcps4-work/.deps/wine/source-clean/dlls/wineoss.drv"
CFLAGS+=" -fPIC -funwind-tables -m64 -g -O2 -fno-stack-protector"
CFLAGS+=" -DWINEPS4_MODULE_NAME=\"wineps4\""

LDFLAGS="-nostartfiles -nodefaultlibs -fuse-ld=lld"
LDFLAGS+=" -Wl,--script=$TC/link.x"
LDFLAGS+=" -Wl,--eh-frame-hdr"
LDFLAGS+=" $COMPAT/build/crt/crtlib.o"
LDFLAGS+=" -L$TC/lib -L$COMPAT/build"
LDFLAGS+=" -lorbis-compat -lc -lkernel -lc++ -lc++abi -lunwind"
LDFLAGS+=" --warn-unresolved-symbols"

LINK_SCRIPT=${HOME}/pcps4-work/build-ps4-module/link_ps4mod.x

echo "Compiling wineps4/unix.c..."
$CC $CFLAGS -c -o $BUILD_DIR/wineps4_unix.o ${HOME}/pcps4-work/wineps4/unix.c

echo "Compiling pw_wine_audio_ps4.c..."
$CC $CFLAGS -c -o $BUILD_DIR/pw_wine_audio_ps4.o ${HOME}/pcps4-work/build-ps4-module/pw_wine_audio_ps4.c

echo "Compiling pw_audio_ps4.c..."
$CC $CFLAGS -c -o $BUILD_DIR/pw_audio_ps4.o ${HOME}/pcps4-work/build-ps4-module/pw_audio_ps4.c

echo "Linking wineps4.so..."
$CC $CFLAGS -shared -o $BUILD_DIR/wineps4.so \
  $BUILD_DIR/wineps4_unix.o \
  $BUILD_DIR/pw_wine_audio_ps4.o \
  $BUILD_DIR/pw_audio_ps4.o \
  $LDFLAGS

echo "Converting to PRX..."
$TC/bin/linux/create-fself -in=$BUILD_DIR/wineps4.so \
  --lib=$BUILD_DIR/wineps4.prx \
  --script=$LINK_SCRIPT

echo "Stripping TLS..."
python3 ${HOME}/pcps4-work/build-ps4-module/patch_tls_dyn.py $BUILD_DIR/wineps4.prx

echo "Generating descriptor..."
python3 ${HOME}/pcps4-work/build-ps4-module/gen_prx_descriptor.py \
  --name wineps4 \
  --exports __wine_unix_call_funcs,__wine_unix_call_wow64_funcs,pw_wine_audio_output,pw_wine_audio_available,pw_wine_audio_cleanup \
  --output $BUILD_DIR/wineps4_desc.c

echo "Compiling descriptor..."
$CC $CFLAGS -c -o $BUILD_DIR/wineps4_desc_loc.o $BUILD_DIR/wineps4_desc.c

echo "Linking with descriptor..."
$CC $CFLAGS -shared -o $BUILD_DIR/wineps4_linked.so \
  $BUILD_DIR/wineps4_unix.o \
  $BUILD_DIR/pw_wine_audio_ps4.o \
  $BUILD_DIR/pw_audio_ps4.o \
  $BUILD_DIR/wineps4_desc_loc.o \
  $LDFLAGS

echo "Creating final PRX..."
$TC/bin/linux/create-fself -in=$BUILD_DIR/wineps4_linked.so \
  --lib=$OUTPUT_DIR/wineps4.prx \
  --script=$LINK_SCRIPT

echo "Stripping TLS again..."
python3 ${HOME}/pcps4-work/build-ps4-module/patch_tls_dyn.py $OUTPUT_DIR/wineps4.prx

echo "Build complete: $OUTPUT_DIR/wineps4.prx"
