set -uo pipefail
export PATH="$HOME/.local/bin:/usr/bin:/bin"; export OO_PS4_TOOLCHAIN=~/openorbis-toolchain
TC=~/openorbis-toolchain; COMPAT=~/src-ps4/orbis-compat; G=~/pcps4-work/build-obj/gfx; LIBDIR=~/pcps4-work/wine-solibs
CRTLIB=$COMPAT/build/crt/crtlib.o; cf=$TC/bin/linux/create-fself
SRC=$(cd "$(dirname "$0")" && pwd)
cd $G
clang --target=x86_64-pc-freebsd12-elf -isysroot $TC -isystem $COMPAT/include -isystem $TC/include -D__PROSPERO__ -D__ORBIS__ -D_BSD_SOURCE=1 -fPIC -m64 -O2 -g -fno-stack-protector -fno-builtin -msse2 -c "$SRC/ps4_fastmem.c" -o $G/ps4_fastmem.o || exit 1
RADV=~/pcps4-work/mesa-ps4-ref/build-orbis/src/amd/vulkan/libvulkan_radeon.a; ZLIB=~/pcps4-work/mesa-ps4-ref/build-orbis/subprojects/zlib-1.3.1/libz.a
clang --target=x86_64-pc-freebsd12-elf -isysroot $TC -shared -Wl,-Bsymbolic -fuse-ld=lld -nostartfiles -nodefaultlibs \
  -Wl,--script=$TC/link_ps4mod.x $CRTLIB -Wl,-soname,libvulkan.prx -Wl,--wrap=regexec \
  -Wl,--wrap=sceGnmSubmitDone -Wl,--wrap=sceGnmSubmitCommandBuffers -o $LIBDIR/libvulkan.so \
  $G/ps4_fastmem.o $G/libvulkan_shim.o $G/libvulkan_desc_loc.o $G/ps4_tls_block_shim.o $G/ps4_submit_keepalive.o \
  -Wl,--whole-archive $RADV $COMPAT/build/liborbis-compat.a -Wl,--no-whole-archive $ZLIB \
  -L$TC/lib -Wl,--start-group -lSceGnmDriver -lSceVideoOut -lkernel -lc -lc++ -lc++abi -lunwind -Wl,--end-group -Wl,--warn-unresolved-symbols 2>$G/libvk.link.err || { tail -5 $G/libvk.link.err; exit 1; }
rm -f libvulkan.prx; $cf -in=$LIBDIR/libvulkan.so -out=libvulkan.oelf --lib=libvulkan.prx --paid 0x3800000000000011 >libvk.cf 2>&1; [ -f libvulkan.prx ] || { tail -3 libvk.cf; exit 1; }
python3 /tmp/patch_tls_dyn.py libvulkan.prx >/dev/null
python3 $G/verify_imports.py libvulkan.prx $LIBDIR/libvulkan.so libSceGnmDriver=$TC/lib/libSceGnmDriver.so libSceVideoOut=$TC/lib/libSceVideoOut.so libkernel=$TC/lib/libkernel.so libc=$TC/lib/libc.so 2>&1 | tail -1
echo "PRXDESC1=$(grep -c PRXDESC1 libvulkan.prx) $(python3 "$SRC/segalign.py" libvulkan.prx 2>&1 | tail -1) keepalive=$(strings libvulkan.prx | grep -c 'submit keepalive running') steal=$(strings libvulkan.prx | grep -c 'taking it from the older')"
python3 -I -c "
import ftplib
f=ftplib.FTP(); import os; f.connect(os.environ['PS4_HOST'],2121,timeout=120); f.login()
p='/data/pcps4/sce_module/libvulkan.prx'
f.delete(p); f.storbinary('STOR '+p, open('libvulkan.prx','rb')); print('uploaded'); f.quit()"
