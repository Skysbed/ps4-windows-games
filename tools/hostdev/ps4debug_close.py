#!/usr/bin/env python3
"""Graceful close of the running PCPS00006 via ps4debug (port 744) — no TV.

Closes the app through LncService while it is still alive, the same path the
shell uses when you back out of a game. This reclaims the big-app's reserved
direct/flexible memory, so the NEXT launch succeeds.

Do NOT shut the app down with a debugger hard-kill (DebuggingContext.kill_process):
that bypasses LncService teardown and leaks the app's memory, which wedges the
next launch with 0x8002000c (ENOMEM). Always close with this.

appId comes from /tmp/pcps4_appid (written by launch_pcps4.py) unless passed.
Usage: close_pcps4.py [appId_hex] [ip]
"""
import asyncio, os, sys, ps4debug
from ps4debug.core.models import CallRegisters

def ai(r): return int.from_bytes(r, 'little') if isinstance(r, (bytes, bytearray)) else r

if len(sys.argv) > 1:
    APPID = int(sys.argv[1], 0)
else:
    APPID = int(open("/tmp/pcps4_appid").read().strip(), 0)
IP = sys.argv[2] if len(sys.argv) > 2 else "PCPS4_CONSOLE_IP"

async def main():
    pd = ps4debug.PS4Debug(IP)
    procs = await pd.get_processes()
    pid = next(getattr(p, 'pid') for p in procs if getattr(p, 'name', '') == "SceShellUI")
    maps = await pd.get_process_maps(pid)
    base = min(getattr(m, 'start') for m in maps
               if "libSceSystemService.sprx" in str(getattr(m, 'name', '')))
    # sceLncUtilKillApp(appId) = offset 0x58d0; sceSystemServiceKillApp = 0x4690
    for name, off in (("sceLncUtilKillApp", 0x58d0), ("sceSystemServiceKillApp", 0x4690)):
        r = ai(await pd.call(pid, base + off, CallRegisters(rdi=APPID))) & 0xffffffff
        print(f"{name}(0x{APPID:x}) -> 0x{r:08x}")
        if (r & 0x80000000) == 0:
            return 0
    return 1

sys.exit(asyncio.run(main()))
