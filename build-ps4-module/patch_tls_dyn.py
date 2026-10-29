import struct,sys
p=sys.argv[1]; f=bytearray(open(p,"rb").read())
elf=f.find(b"\x7fELF")
e_phoff,=struct.unpack_from("<Q",f,elf+0x20); e_phnum,=struct.unpack_from("<H",f,elf+0x38); e_phent,=struct.unpack_from("<H",f,elf+0x36)
ph=elf+e_phoff
# 1) PT_TLS (7) -> PT_NULL (0)
tls=0
for i in range(e_phnum):
    off=ph+i*e_phent
    pt,=struct.unpack_from("<I",f,off)
    if pt==7:
        struct.pack_into("<I",f,off,0); tls+=1
# 2) null all DTPMOD64(16)/DTPOFF64(17) reloc types across file (Elf64_Rela: r_info low32)
nul=0
for t in (16,17):
    pat=struct.pack("<I",t); i=0
    while True:
        j=f.find(pat,i)
        if j<0: break
        i=j+1
        if j+8>len(f): continue
        r_info=struct.unpack_from("<Q",f,j)[0]
        if (r_info&0xffffffff)!=t: continue
        sym=r_info>>32
        if sym>0x20000 or j<8: continue
        r_off=struct.unpack_from("<Q",f,j-8)[0]
        if 0x1000<=r_off<=0x2000000:
            f[j]=0; nul+=1   # type low byte -> 0 (R_X86_64_NONE)
open(p,"wb").write(f)
print("PT_TLS nulled:",tls,"  DTPMOD/DTPOFF relocs nulled:",nul)
