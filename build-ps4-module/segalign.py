import struct,sys
def show(path):
    f=open(path,"rb").read(); elf=f.find(b"\x7fELF")
    e_phoff,=struct.unpack_from("<Q",f,elf+0x20); e_phnum,=struct.unpack_from("<H",f,elf+0x38); e_phent,=struct.unpack_from("<H",f,elf+0x36)
    ph=elf+e_phoff; print("###",path.split("/")[-1])
    names={1:"LOAD",2:"DYNAMIC",3:"INTERP",6:"PHDR",7:"TLS",0:"NULL",0x6474e550:"GNU_EH",0x61000000:"SCE_DLD",0x61000001:"SCE_PROC",0x61000002:"SCE_MODP",0x61000010:"SCE_RELRO"}
    bad=0
    for i in range(e_phnum):
        pt,fl=struct.unpack_from("<II",f,ph+i*e_phent)
        p_off,p_va,p_pa,p_fsz,p_msz,p_al=struct.unpack_from("<QQQQQQ",f,ph+i*e_phent+8)
        if pt in (1,0x61000010):  # LOAD + SCE_RELRO are the ones rtld maps/checks
            ok = (p_va%0x4000==0) and (p_off%0x4000==0)
            if not ok: bad+=1
            print("  seg#%d %-8s off=%#010x va=%#010x filesz=%#x  ABS16K-ok=%s%s"%(i,names.get(pt,hex(pt)),p_off,p_va,p_fsz,ok,"" if ok else "  <-- MISALIGNED"))
    print("  => %s"%("ALL ALIGNED" if bad==0 else "%d misaligned"%bad))
for p in sys.argv[1:]: show(p)
