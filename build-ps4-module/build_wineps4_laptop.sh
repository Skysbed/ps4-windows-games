#!/bin/bash
# Build script for wineps4 audio driver on laptop

set -e

# Configuration for laptop
TC=${HOME}/openorbis-toolchain
COMPAT=${HOME}/src-ps4/orbis-compat
WINEPS4_SRC=${HOME}/pcps4-work/wineps4/unix.c
WINEPS4_EXTRA_SRCS="${HOME}/pcps4-work/build-ps4-module/pw_wine_audio_ps4.c ${HOME}/pcps4-work/build-ps4-module/pw_audio_ps4.c"
WINEPS4_MODULE_NAME=wineps4

# Directories
BUILD_DIR=${HOME}/pcps4-work/build-obj/wineps4
OUTPUT_DIR=${HOME}/pcps4-work/pkg-build/sce_module

# Create directories
mkdir -p $BUILD_DIR $OUTPUT_DIR

# Compiler flags
CC="clang --target=x86_64-pc-freebsd12-elf"
CFLAGS="-isysroot $TC -isystem $COMPAT/include -isystem ${HOME}/pcps4-work/wine-ps4-shims \
        -isystem $TC/include -D__PROSPERO__ -D__ORBIS__ -D_BSD_SOURCE=1 \
        -isystem ${HOME}/pcps4-work/.deps/wine/source-clean/include \
        -fPIC -funwind-tables -m64 -g -O2 -fno-stack-protector \
        -DWINEPS4_MODULE_NAME=\"$WINEPS4_MODULE_NAME\""

# Linker flags
LDFLAGS="-nostartfiles -nodefaultlibs -fuse-ld=lld \
        -Wl,--script=$TC/link.x \
        -Wl,--eh-frame-hdr \
        $COMPAT/build/crt/crtlib.o \
        -L$TC/lib -L$COMPAT/build \
        -lorbis-compat -lc -lkernel -lc++ -lc++abi -lunwind \
        --warn-unresolved-symbols"

# Linker script for modules (16KiB alignment)
LINK_SCRIPT="${HOME}/pcps4-work/build-ps4-module/link_ps4mod.x"

# Compile wineps4/unix.c
echo "Compiling wineps4/unix.c..."
$CC $CFLAGS -c -o $BUILD_DIR/wineps4_unix.o $WINEPS4_SRC

# Compile pw_wine_audio_ps4.c
echo "Compiling pw_wine_audio_ps4.c..."
$CC $CFLAGS -c -o $BUILD_DIR/pw_wine_audio_ps4.o ${HOME}/pcps4-work/build-ps4-module/pw_wine_audio_ps4.c

# Compile pw_audio_ps4.c
echo "Compiling pw_audio_ps4.c..."
$CC $CFLAGS -c -o $BUILD_DIR/pw_audio_ps4.o ${HOME}/pcps4-work/build-ps4-module/pw_audio_ps4.c

# Build the Unix library (wineps4.so) - link all object files together
echo "Linking wineps4.so..."
$CC $CFLAGS -shared -o $BUILD_DIR/$WINEPS4_MODULE_NAME.so \
    $BUILD_DIR/wineps4_unix.o \
    $BUILD_DIR/pw_wine_audio_ps4.o \
    $BUILD_DIR/pw_audio_ps4.o \
    $LDFLAGS

# Convert to PRX module
echo "Converting to PRX module..."
$TC/bin/linux/create-fself -in=$BUILD_DIR/$WINEPS4_MODULE_NAME.so \
    --lib=$BUILD_DIR/$WINEPS4_MODULE_NAME.prx \
    --script=$LINK_SCRIPT

# Strip TLS (PS4 modules can't have TLS)
echo "Stripping TLS..."
python3 ${HOME}/pcps4-work/build-ps4-module/patch_tls_dyn.py $BUILD_DIR/$WINEPS4_MODULE_NAME.prx

# Generate PRX descriptor
echo "Generating PRX descriptor..."
python3 ${HOME}/pcps4-work/build-ps4-module/gen_prx_descriptor.py \
    --name $WINEPS4_MODULE_NAME \
    --exports __wine_unix_call_funcs,__wine_unix_call_wow64_funcs,pw_wine_audio_output,pw_wine_audio_available,pw_wine_audio_cleanup \
    --output $BUILD_DIR/${WINEPS4_MODULE_NAME}_desc.c

# Compile descriptor
echo "Compiling descriptor..."
$CC $CFLAGS -c -o $BUILD_DIR/${WINEPS4_MODULE_NAME}_desc_loc.o $BUILD_DIR/${WINEPS4_MODULE_NAME}_desc.c

# Re-link with descriptor
echo "Linking with descriptor..."
$CC $CFLAGS -shared -o $BUILD_DIR/${WINEPS4_MODULE_NAME}_linked.so \
    $BUILD_DIR/wineps4_unix.o \
    $BUILD_DIR/pw_wine_audio_ps4.o \
    $BUILD_DIR/pw_audio_ps4.o \
    $BUILD_DIR/${WINEPS4_MODULE_NAME}_desc_loc.o \
    $LDFLAGS

# Convert final PRX
echo "Creating final PRX..."
$TC/bin/linux/create-fself -in=$BUILD_DIR/${WINEPS4_MODULE_NAME}_linked.so \
    --lib=$OUTPUT_DIR/$WINEPS4_MODULE_NAME.prx \
    --script=$LINK_SCRIPT

# Strip TLS again
python3 ${HOME}/pcps4-work/build-ps4-module/patch_tls_dyn.py $OUTPUT_DIR/$WINEPS4_MODULE_NAME.prx

echo "Build complete: $OUTPUT_DIR/$WINEPS4_MODULE_NAME.prx"
