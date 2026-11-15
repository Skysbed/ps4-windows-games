set -uo pipefail
export PATH="$HOME/.local/bin:/usr/bin:/bin"; export OO_PS4_TOOLCHAIN=~/openorbis-toolchain
TC=~/openorbis-toolchain; COMPAT=~/src-ps4/orbis-compat
B=~/pcps4-work/.deps/wine/build-ps4; WS=~/pcps4-work/build-obj/ws; G=~/pcps4-work/build-obj/gfx
LIBDIR=~/pcps4-work/wine-solibs; cf=$TC/bin/linux/create-fself; CRTLIB=$COMPAT/build/crt/crtlib.o
LDM="-nostartfiles -nodefaultlibs -fuse-ld=lld -Wl,--script=$TC/link_ps4mod.x $CRTLIB -L$TC/lib -L$COMPAT/build -Wl,--start-group -lorbis-compat -lc -lkernel -lc++ -lc++abi -lunwind -Wl,--end-group -Wl,--warn-unresolved-symbols"
DLW="-Wl,--wrap=dlopen -Wl,--wrap=dlsym -Wl,--wrap=dlclose $G/ps4_dl_forward.o"
cd "$B"; rm -f dlls/win32u/ps5drv.o dlls/win32u/win32u.so
make -j$(nproc) dlls/win32u/win32u.so LDFLAGS="$WS/ps4_tls_shim.o $G/win32u_desc_loc.o $DLW $LDM" >$G/win32u.make.log 2>&1 || { echo WIN32U FAIL; grep -E "error" $G/win32u.make.log|head -8; exit 1; }
cp dlls/win32u/win32u.so $LIBDIR/
nm -D $LIBDIR/win32u.so | grep -qE ' U (dlopen|dlsym|dlclose)$' && { echo "AMBIGUOUS DL IMPORTS"; exit 1; }
SW=$G/stubs_win32u; rm -rf $SW; mkdir -p $SW; cp $LIBDIR/ntdll.so $SW/
rm -f $G/win32u.prx
$cf -in="$LIBDIR/win32u.so" -out=$G/win32u.oelf --lib=$G/win32u.prx --library-path="$SW" --paid 0x3800000000000011 >$G/win32u.cf 2>&1
[ -f $G/win32u.prx ] || { echo "FSELF FAIL"; tail -3 $G/win32u.cf; exit 1; }
python3 /tmp/patch_tls_dyn.py $G/win32u.prx >/dev/null
python3 $G/verify_imports.py $G/win32u.prx $LIBDIR/win32u.so ntdll=$LIBDIR/ntdll.so libkernel=$TC/lib/libkernel.so 2>&1 | tail -1
grep -q PRXDESC1 $G/win32u.prx || exit 1
echo "win32u pad hooks=$(strings $G/win32u.prx | grep -c pcps4_pad_start)"
python3 -I -c "
import ftplib
f=ftplib.FTP(); import os; f.connect(os.environ['PS4_HOST'],2121,timeout=120); f.login()
p='/data/pcps4/sce_module/win32u.prx'
try: f.delete(p)
except Exception: pass
f.storbinary('STOR '+p, open('$G/win32u.prx','rb'), blocksize=1<<20); print('uploaded win32u'); f.quit()"
