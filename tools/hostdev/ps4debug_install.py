#!/usr/bin/env python3
"""Hands-free PKG install via ps4debug (persistent port 744) — no controller/TV.

Drives libSceAppInstUtil (hosted in SceShellUI) to install a .pkg that is
already on the console's filesystem. This is the self-service path for testing
new builds: FTP-push pcps4_vNN_dxvk.pkg to /data, then install it here, then
launch_pcps4.py. Reinstalling also re-registers the title fresh, which clears a
launch-state that a prior hard-kill left wedged.

Usage: install_pkg.py [pkg_path_on_ps4] [ip]
  default pkg_path = /data/pcps4_v67_dxvk.pkg
"""
import asyncio, sys, ps4debug
from ps4debug.core.models import CallRegisters

PKG = (sys.argv[1] if len(sys.argv) > 1 else "/data/pcps4_v67_dxvk.pkg")
IP = sys.argv[2] if len(sys.argv) > 2 else "PCPS4_CONSOLE_IP"

# Export offsets in libSceAppInstUtil.sprx (resolved from the pulled sprx).
OFF_INIT = 0x2cd0   # sceAppInstUtilInitialize()
OFF_TID = 0x3870    # sceAppInstUtilGetTitleIdFromPkg(path, char* out)
OFF_INSTALL = 0x3110  # sceAppInstUtilAppInstallPkg(path, void* info|NULL)


def as_int(rax):
    return int.from_bytes(rax, 'little') if isinstance(rax, (bytes, bytearray)) else rax


async def main():
    pd = ps4debug.PS4Debug(IP)
    procs = await pd.get_processes()
    pid = next(getattr(p, 'pid') for p in procs if getattr(p, 'name', '') == "SceShellUI")
    maps = await pd.get_process_maps(pid)
    base = min(getattr(m, 'start') for m in maps
               if "libSceAppInstUtil.sprx" in str(getattr(m, 'name', '')))
    print(f"SceShellUI pid={pid} libSceAppInstUtil base=0x{base:x}")

    # Scratch memory: path string + a titleid-out buffer.
    mem = await pd.allocate_memory(pid, 1024)
    path = PKG.encode() + b"\x00"
    await pd.write_memory(pid, mem, path)
    tid_out = mem + 512
    await pd.write_memory(pid, tid_out, (0).to_bytes(64, 'little'))

    ret = as_int(await pd.call(pid, base + OFF_INIT, CallRegisters()))
    print(f"sceAppInstUtilInitialize -> 0x{ret & 0xffffffff:08x}")

    ret = as_int(await pd.call(pid, base + OFF_TID, CallRegisters(rdi=mem, rsi=tid_out)))
    tid = (await pd.read_memory(pid, tid_out, 16)).split(b"\x00")[0].decode("latin1", "replace")
    print(f"sceAppInstUtilGetTitleIdFromPkg -> 0x{ret & 0xffffffff:08x} titleid='{tid}'")

    ret = as_int(await pd.call(pid, base + OFF_INSTALL, CallRegisters(rdi=mem, rsi=0)))
    err = ret & 0xffffffff
    ok = (err & 0x80000000) == 0
    print(f"sceAppInstUtilAppInstallPkg -> 0x{err:08x} {'OK' if ok else 'FAILED'}")
    return 0 if ok else 1

sys.exit(asyncio.run(main()))
