#!/usr/bin/env python3
"""Controller-free PKG install via the PS4 bgft daemon + an HTTP serve on this Mac.
Uninstalls the existing title, then registers+starts a background download/install
of the pkg from the HTTP URL, and polls to completion. ps4debug port 744."""
import asyncio, struct, sys, ps4debug
from ps4debug.core.models import CallRegisters
IP="PCPS4_CONSOLE_IP"
URL=sys.argv[1] if len(sys.argv)>1 else "http://PCPS4_HOST_IP:8099/pcps4_v73_dxvk_trivid.pkg"
CID=sys.argv[2] if len(sys.argv)>2 else "IV0000-PCPS00006_00-DXVKFIRSTLIGHT01"
TITLE=sys.argv[3] if len(sys.argv)>3 else "PCPS00006"
SIZE=int(sys.argv[4]) if len(sys.argv)>4 else 23134208
def ai(r): return int.from_bytes(r,'little') if isinstance(r,(bytes,bytearray)) else r
async def main():
    pd=ps4debug.PS4Debug(IP); procs=await pd.get_processes()
    pid=next(getattr(p,'pid') for p in procs if getattr(p,'name','')=="SceShellUI")
    maps=await pd.get_process_maps(pid)
    def base(l): return min(getattr(m,'start') for m in maps if l in str(getattr(m,'name','')))
    US=base("libSceUserService.sprx"); AIU=base("libSceAppInstUtil.sprx"); BG=base("libSceBgft.sprx")
    mem=await pd.allocate_memory(pid, 8192); await pd.write_memory(pid, mem, b"\x00"*8192)
    await pd.call(pid, US+0x3070, CallRegisters(rdi=mem+112)); uid=int.from_bytes(await pd.read_memory(pid, mem+112,4),'little')
    await pd.call(pid, AIU+0x2cd0, CallRegisters())
    # CRITICAL: close any running big app FIRST. Uninstalling a RUNNING title
    # wedges the shell's install IPC, which in turn wedges ps4debug's call
    # mechanism (recoverable only by reloading the payload or rebooting). Close
    # it gracefully via LncService before touching the install.
    SS=min(getattr(m,'start') for m in maps if "libSceSystemService.sprx" in str(getattr(m,'name','')))
    appid=ai(await pd.call(pid, SS+0x4910, CallRegisters()))&0xffffffff  # GetAppIdOfBigApp
    if appid and not (appid & 0x80000000):
        await pd.call(pid, SS+0x58d0, CallRegisters(rdi=appid))  # sceLncUtilKillApp
        await pd.call(pid, SS+0x4690, CallRegisters(rdi=appid))  # sceSystemServiceKillApp
        await asyncio.sleep(3)
    # uninstall existing title (AppUnInstall = 0x3160)
    tb=TITLE.encode()+b"\x00"; await pd.write_memory(pid, mem+200, tb)
    ru=ai(await pd.call(pid, AIU+0x3160, CallRegisters(rdi=mem+200)))&0xffffffff
    print(f"AppUnInstall({TITLE}) -> 0x{ru:08x}")
    await asyncio.sleep(2)
    # strings
    so=mem+1024; S={}
    async def put(k,s):
        nonlocal so
        b=s.encode()+b"\x00"; await pd.write_memory(pid, so, b); S[k]=so; so+=len(b)
    await put("id",CID); await put("url",URL); await put("name","PcPs4 DXVK"); await put("empty",""); await put("playgo","0"); await put("pt","PS4GD")
    taskout=mem+104
    st=bytearray(104)
    struct.pack_into("<i",st,0,uid); struct.pack_into("<i",st,4,5)
    struct.pack_into("<Q",st,8,S["id"]); struct.pack_into("<Q",st,16,S["url"])
    struct.pack_into("<Q",st,32,S["name"]); struct.pack_into("<Q",st,40,S["empty"])
    struct.pack_into("<I",st,56,0)  # option = NONE (CDN flag is rejected on this fw)
    struct.pack_into("<Q",st,64,S["playgo"]); struct.pack_into("<Q",st,80,S["pt"])
    struct.pack_into("<Q",st,88,S["empty"]); struct.pack_into("<I",st,96,SIZE)
    await pd.write_memory(pid, mem, bytes(st)); await pd.write_memory(pid, taskout,(0).to_bytes(4,'little'))
    rr=ai(await pd.call(pid, BG+0x220, CallRegisters(rdi=mem, rsi=taskout)))&0xffffffff
    tid=int.from_bytes(await pd.read_memory(pid, taskout,4),'little')
    print(f"RegisterTask -> 0x{rr:08x} taskId={tid}")
    if rr & 0x80000000:
        print("register failed"); return
    rs=ai(await pd.call(pid, BG+0x260, CallRegisters(rdi=tid)))&0xffffffff
    print(f"StartTask({tid}) -> 0x{rs:08x}")
    prog=mem+300
    for i in range(60):
        await asyncio.sleep(2)
        await pd.write_memory(pid, prog, b"\x00"*48)
        rp=ai(await pd.call(pid, BG+0x2a0, CallRegisters(rdi=tid, rsi=prog)))&0xffffffff
        bits,err,length,xfer,lenT,xferT=struct.unpack_from("<IiIIII",await pd.read_memory(pid,prog,48),0)
        pct = (100*xferT//lenT) if lenT else 0
        print(f"  [{i}] ret=0x{rp:08x} bits=0x{bits:x} err=0x{err&0xffffffff:08x} {xferT}/{lenT} ({pct}%)")
        if err & 0x80000000: print("  ERROR"); break
        if lenT>0 and xferT>=lenT: print("  DOWNLOAD+INSTALL COMPLETE"); break
asyncio.run(main())
