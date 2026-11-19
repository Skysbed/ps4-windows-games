#!/bin/bash
# Build script for PS4 audio sink (pw_wine_audio_ps4)

set -e

# Configuration
TC=${HOME}/openorbis-toolchain
COMPAT=${HOME}/src-ps4/orbis-compat
AUDIO_SINK_SRC=build-ps4-module/pw_wine_audio_ps4.c
AUDIO_SINK_MODULE_NAME=pw_wine_audio_ps4

# Directories
BUILD_DIR=build-obj/audio_sink
OUTPUT_DIR=pkg-build/sce_module

# Create directories
mkdir -p $BUILD_DIR $OUTPUT_DIR

# Compiler flags
CC="clang --target=x86_64-pc-freebsd12-elf"
CFLAGS="-isysroot $TC -isystem $COMPAT/include -isystem $HOME/pcps4-work/wine-ps4-shims \
        -isystem $TC/include -D__PROSPERO__ -D__ORBIS__ -D_BSD_SOURCE=1 \
        -fPIC -funwind-tables -m64 -g -O2 -fno-stack-protector"

# Linker flags
LDFLAGS="-nostartfiles -nodefaultlibs -fuse-ld=lld \
        -Wl,--script=$TC/link.x \
        -Wl,--eh-frame-hdr \
        $COMPAT/build/crt/crtlib.o \
        -L$TC/lib -L$COMPAT/build \
        -lorbis-compat -lc -lkernel -lc++ -lc++abi -lunwind \
        -lSceAudioOut \
        --warn-unresolved-symbols"

# Linker script for modules
LINK_SCRIPT="$HOME/pcps4-work/build-ps4-module/link_ps4mod.x"

# Build the object file
echo "Building $AUDIO_SINK_MODULE_NAME.o..."
$CC $CFLAGS -c -o $BUILD_DIR/$AUDIO_SINK_MODULE_NAME.o $AUDIO_SINK_SRC

# Link as shared library
echo "Linking $AUDIO_SINK_MODULE_NAME.so..."
$CC $CFLAGS -shared -o $BUILD_DIR/$AUDIO_SINK_MODULE_NAME.so \
    $BUILD_DIR/$AUDIO_SINK_MODULE_NAME.o \
    $LDFLAGS

# Convert to PRX module
echo "Converting to PRX module..."
$TC/bin/linux/create-fself -in=$BUILD_DIR/$AUDIO_SINK_MODULE_NAME.so \
    --lib=$BUILD_DIR/$AUDIO_SINK_MODULE_NAME.prx \
    --script=$LINK_SCRIPT

# Strip TLS
echo "Stripping TLS..."
python3 $HOME/pcps4-work/build-ps4-module/patch_tls_dyn.py $BUILD_DIR/$AUDIO_SINK_MODULE_NAME.prx

# The audio sink functions will be linked into ntdll.prx, not as a separate module
# For now, just compile and link as a standalone module for testing

# Copy to output
cp $BUILD_DIR/$AUDIO_SINK_MODULE_NAME.prx $OUTPUT_DIR/

echo "Build complete: $OUTPUT_DIR/$AUDIO_SINK_MODULE_NAME.prx"
