#!/usr/bin/env python3
"""Hands-free launcher for PCPS00006 via ps4debug (persistent port 744).
Calls sceSystemServiceLaunchApp in SceShellUI with a (zeroed) param struct —
the combo that works. No controller/TV needed. Usage: launch_pcps4.py [ip] [titleid]"""
import asyncio, sys, ps4debug
from ps4debug.core.models import CallRegisters
IP = sys.argv[1] if len(sys.argv) > 1 else "PCPS4_CONSOLE_IP"
TITLE = (sys.argv[2] if len(sys.argv) > 2 else "PCPS00006").encode() + b"\x00"
async def main():
    pd = ps4debug.PS4Debug(IP)
    procs = await pd.get_processes()
    pid = next(getattr(p,'pid') for p in procs if getattr(p,'name','')=="SceShellUI")
    maps = await pd.get_process_maps(pid)
    base = min(getattr(m,'start') for m in maps if "libSceSystemService.sprx" in str(getattr(m,'name','')))
    func = base + 0x4600  # sceSystemServiceLaunchApp export offset (NID l4FB3wNa-Ac)
    mem = await pd.allocate_memory(pid, 256)
    await pd.write_memory(pid, mem, TITLE)
    await pd.write_memory(pid, mem+24, (0).to_bytes(24,'little'))  # zeroed param struct (required)
    rax = await pd.call(pid, func, CallRegisters(rdi=mem, rsi=0, rdx=mem+24))
    v = int.from_bytes(rax,'little') if isinstance(rax,(bytes,bytearray)) else rax
    ok = (v & 0x80000000) == 0
    print(f"launch {'OK appId=0x%x' % v if ok else 'FAILED err=0x%08x' % (v & 0xffffffff)}")
    return 0 if ok else 1
sys.exit(asyncio.run(main()))
