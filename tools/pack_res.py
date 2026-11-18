# pack_res.py <root dir> <out pack> - build a PCPKPK01 pack of every file under root (paths relative, '/'-separated)
import os, struct, sys
root, out = sys.argv[1], sys.argv[2]
files=[]
for d, ds, fs in os.walk(root):
    ds.sort()
    for f in sorted(fs):
        p=os.path.join(d,f); files.append((os.path.relpath(p,root).replace(os.sep,'/'), os.path.getsize(p), p))
hdr=b"PCPKPK01"+struct.pack("<I",len(files))
idx_len=sum(2+len(r.encode())+16 for r,_,_ in files)
off=len(hdr)+idx_len; idx=b""
for r,s,_ in files:
    rb=r.encode(); idx+=struct.pack("<H",len(rb))+rb+struct.pack("<QQ",off,s); off+=s
with open(out,"wb") as o:
    o.write(hdr); o.write(idx)
    for _,_,p in files:
        with open(p,"rb") as fh: o.write(fh.read())
print(len(files),"files,",off>>20,"MB ->",out)
