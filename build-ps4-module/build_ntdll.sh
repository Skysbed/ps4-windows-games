set -uo pipefail
export PATH="$HOME/.local/bin:/usr/bin:/bin"; export OO_PS4_TOOLCHAIN=~/openorbis-toolchain
TC=~/openorbis-toolchain; COMPAT=~/src-ps4/orbis-compat; P=~/pcps4-work/prospero-win
B=~/pcps4-work/.deps/wine/build-ps4; W=~/pcps4-work/wine-ps5-support; WS=~/pcps4-work/build-obj/ws; ND=~/pcps4-work/build-obj/ntdll
L=~/pcps4-work/ps4-local-src; LIBDIR=~/pcps4-work/wine-solibs
CRTLIB=$COMPAT/build/crt/crtlib.o; cf=$TC/bin/linux/create-fself
CCW="clang --target=x86_64-pc-freebsd12-elf -isysroot $TC -isystem $COMPAT/include -isystem $HOME/pcps4-work/wine-ps4-shims -isystem $TC/include -D__PROSPERO__ -D__ORBIS__ -D_BSD_SOURCE=1 -fPIC -funwind-tables -m64 -g -O2 -fno-stack-protector"
$CCW '-DRTLD_SELF=((void*)-3)' -I$P/wine/ps5 -I$P/src -c $L/pw_wine_dl.c -o $ND/pw_wine_dl.o 2>/tmp/dl.err || { head -8 /tmp/dl.err; exit 1; }
$CCW -c $L/ps4_pipe_fallback.c -o $ND/ps4_pipe_fallback.o 2>/tmp/pf.err || { head -8 /tmp/pf.err; exit 1; }
$CCW -c $L/ps4_pad_input.c -o $ND/ps4_pad_input.o 2>/tmp/pi.err || { head -8 /tmp/pi.err; exit 1; }
$CCW -c "$L/ps4_sigmask.c" -o "$ND/ps4_sigmask.o" || exit 1
$CCW -c "$L/ps4_entropy.c" -o "$ND/ps4_entropy.o" || exit 1
$CCW -c $L/ps4_preload.c -o $ND/ps4_preload.o 2>/tmp/pp.err || { head -8 /tmp/pp.err; exit 1; }
$CCW -I$P/wine/ps5 -c $ND/ntdll_desc.c -o $ND/ntdll_desc_loc.o 2>/tmp/nd.err || { head -8 /tmp/nd.err; exit 1; }
llvm-objcopy --localize-symbol=module_start --localize-symbol=module_stop $ND/ntdll_desc_loc.o
WRAPS=$(grep -oE "__wrap_[a-z_]+" "$P/wine/ps5/pw_wine_cwd_libc.c" | sed "s/__wrap_//" | sort -u); WF="-Wl,--wrap=pthread_sigmask -Wl,--wrap=pw_compat_pipe -Wl,--wrap=read -Wl,--wrap=pread"; for w in $WRAPS; do WF="$WF -Wl,--wrap=$w"; done
LD="-nostartfiles -nodefaultlibs -fuse-ld=lld -Wl,--script=$TC/link_ps4mod.x $CRTLIB -L$TC/lib -L$COMPAT/build -Wl,--start-group -lorbis-compat -lc -lkernel -lc++ -lc++abi -lunwind -Wl,--end-group -Wl,--warn-unresolved-symbols"
SRC=$(cd "$(dirname "$0")" && pwd)
$CCW -I$P/wine/ps5 -I$P/src '-DPW_CLOSE_TAG="cli"' -c "$SRC/pw_wine_cwd_libc_ps4.c" -o "$WS/pw_wine_cwd_libc_cli.o" || exit 1
NSUP="$WS/ps4_tls_shim.o $ND/pw_wine_prx.o $ND/pw_wine_dl.o $ND/pw_wine_dl_libc.o $WS/pw_wine_compat.o $WS/pw_wine_compat_libc.o $W/pw_wine_cwd.o $WS/pw_wine_cwd_libc_cli.o $W/pw_wine_sink.o $W/pw_wine_heap.o $W/pw_wine_heap_libc.o $ND/pw_wine_dmem_core.o $ND/pw_wine_dmem_ps4.o $ND/ps4_pipe_fallback.o $ND/ps4_pad_input.o $ND/ps4_preload.o $ND/ps4_sigmask.o $ND/ps4_entropy.o $ND/stubs_w.o $ND/ntdll_desc_loc.o"
for o in $NSUP; do [ -f $o ] || { echo "missing $o"; exit 1; }; done
cd $B; rm -f dlls/ntdll/ntdll.so
make -j$(nproc) dlls/ntdll/ntdll.so LDFLAGS="$NSUP $WF $LD" >/tmp/ntdll.make.log 2>&1 || { echo NTDLL FAIL; grep error /tmp/ntdll.make.log|head; exit 1; }
rm -f $ND/ntdll.prx
$cf -in=dlls/ntdll/ntdll.so -out=/tmp/n.oelf --lib=$ND/ntdll.prx --paid 0x3800000000000011 >/dev/null 2>&1; python3 /tmp/patch_tls_dyn.py $ND/ntdll.prx >/dev/null
cp dlls/ntdll/ntdll.so $LIBDIR/ntdll.so
echo "ntdll size=$(stat -c%s $ND/ntdll.prx) preload=$(strings $ND/ntdll.prx | grep -c 'sce_module/libvulkan.prx') desc=$(grep -c PRXDESC1 $ND/ntdll.prx) dmemps4=$(strings $ND/ntdll.prx | grep -c 'dmem protect REFUSED') pipefb=$(strings $ND/ntdll.prx | grep -c 'socket budget spent')"
cd $ND && python3 -I -c "
import ftplib
f=ftplib.FTP(); import os; f.connect(os.environ['PS4_HOST'],2121,timeout=120); f.login()
f.storbinary('STOR /data/pcps4/sce_module/ntdll.prx', open('ntdll.prx','rb')); print('uploaded ntdll'); f.quit()"
