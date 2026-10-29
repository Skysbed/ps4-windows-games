import struct,sys
def info(path):
    f=open(path,"rb").read()
    d={}
    d["self_magic"]=f[:4].hex()
    d["key_type"]=struct.unpack_from("<H",f,0x08)[0]
    d["self_attr"]=f[0x07]
    d["self_mode"]=f[0x05]
    num_seg,=struct.unpack_from("<H",f,0x18); d["num_seg"]=num_seg
    elf=f.find(b"\x7fELF"); d["elf_off"]=hex(elf)
    d["e_type"]=hex(struct.unpack_from("<H",f,elf+0x10)[0])
    d["e_machine"]=hex(struct.unpack_from("<H",f,elf+0x12)[0])
    e_phoff,=struct.unpack_from("<Q",f,elf+0x20); e_phnum,=struct.unpack_from("<H",f,elf+0x38); e_phent,=struct.unpack_from("<H",f,elf+0x36)
    d["phnum"]=e_phnum
    pts=[]
    ph=elf+e_phoff
    for i in range(e_phnum):
        pt,=struct.unpack_from("<I",f,ph+i*e_phent)
        pts.append(hex(pt))
    d["ph_types"]=pts
    # SCE extended / PAID: after the ELF phdrs there's an SCE special segment with program authority id.
    # Search for the 8-byte PAID pattern 0x3800000000000011 if present
    paid=f.find(struct.pack("<Q",0x3800000000000011))
    d["paid_0x38..11_at"]=hex(paid) if paid>=0 else "not found"
    # module/proc param: find .data.sce_module_param marker? check PT_SCE_MODULE_PARAM(0x61000002) or PROCPARAM(0x61000001)
    d["has_MODULE_PARAM(0x61000002)"]="0x61000002" in pts
    d["has_PROC_PARAM(0x61000001)"]="0x61000001" in pts
    return d
for p in sys.argv[1:]:
    print("###",p.split("/")[-1])
    for k,v in info(p).items(): print("   %-28s %s"%(k,v))
