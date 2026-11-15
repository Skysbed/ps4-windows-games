set -uo pipefail
export PATH="$HOME/.local/bin:/usr/bin:/bin"; export OO_PS4_TOOLCHAIN=~/openorbis-toolchain
S=~/pcps4-work/.deps/wine/source/server
TC=~/openorbis-toolchain; COMPAT=~/src-ps4/orbis-compat; P=~/pcps4-work/prospero-win
B=~/pcps4-work/.deps/wine/build-ps4; W=~/pcps4-work/wine-ps5-support; WS=~/pcps4-work/build-obj/ws
CRTLIB=$COMPAT/build/crt/crtlib.o; cf=$TC/bin/linux/create-fself
WRAPS=$(grep -oE "__wrap_[a-z_]+" "$P/wine/ps5/pw_wine_cwd_libc.c" | sed "s/__wrap_//" | sort -u); WF=""; for w in $WRAPS; do WF="$WF -Wl,--wrap=$w"; done
LD="-nostartfiles -nodefaultlibs -fuse-ld=lld -Wl,--script=$TC/link_ps4mod.x $CRTLIB -L$TC/lib -L$COMPAT/build -Wl,--start-group -lorbis-compat -lc -lkernel -lc++ -lc++abi -lunwind -Wl,--end-group -Wl,--warn-unresolved-symbols"
SSUP="$WS/ps4_tls_shim.o $W/pw_wine_heap.o $W/pw_wine_heap_libc.o $WS/pw_wine_compat.o $WS/pw_wine_compat_libc.o $WS/pw_wine_threads.o $WS/pw_wine_threads_libc.o $W/pw_wine_cwd.o $WS/pw_wine_cwd_libc.o $WS/wineserver_desc_loc.o"
cd $B; rm -f server/fd.o server/request.o server/thread.o
make -j$(nproc) server/fd.o server/request.o server/thread.o >/tmp/srv.make.log 2>&1 || { echo SRV FAIL; grep error /tmp/srv.make.log | head; exit 1; }
clang --target=x86_64-pc-freebsd12-elf -fPIC -m64 -shared -Wl,-Bsymbolic -o $WS/wineserver.so server/*.o $SSUP $WF $LD 2>/tmp/srv.link.log || { echo SRVLINK FAIL; tail -5 /tmp/srv.link.log; exit 1; }
rm -f $WS/wineserver.prx
$cf -in=$WS/wineserver.so -out=/tmp/w.oelf --lib=$WS/wineserver.prx --paid 0x3800000000000011 >/dev/null 2>&1; python3 /tmp/patch_tls_dyn.py $WS/wineserver.prx >/dev/null
grep -q PRXDESC1 $WS/wineserver.prx || { echo NODESC; exit 1; }
echo "fileid=$(strings $WS/wineserver.prx | grep -c . ) built"
python3 -I -c "
import ftplib
f=ftplib.FTP(); import os; f.connect(os.environ['PS4_HOST'],2121,timeout=120); f.login()
p='/data/pcps4/sce_module/wineserver.prx'
try: f.delete(p)
except Exception: pass
f.storbinary('STOR '+p, open('$WS/wineserver.prx','rb'), blocksize=1<<20); print('uploaded wineserver'); f.quit()"
