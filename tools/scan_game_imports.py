#!/usr/bin/env python3
"""
Report which Windows imports a game (and its DLLs) still lack in the PcPs4
runtime, per DLL, without a console in the loop.

usage: scan_game_imports.py <file.exe|file.dll> [...more files]   (run from the repo root)

It mirrors the runtime's resolution rules: functions registered under
KERNEL32 also resolve under USER32/GDI32/ADVAPI32; C-runtime names resolve
under msvcrt/ucrtbase; DXVK's d3d9/d3d10/d3d11/dxgi and the game's own DLLs are
loaded as real DLLs (so their own imports are scanned when passed in). Needs
`pefile`.
"""
import collections, glob, os, re, sys
import pefile

def body(src, name):
    m = re.search(r'\b' + re.escape(name) + r'\s*\([^)]*\)\s*\{', src)
    if not m:
        return ""
    i, depth = m.end(), 1
    while depth and i < len(src):
        depth += (src[i] == '{') - (src[i] == '}')
        i += 1
    return src[m.end():i]

def names(text):
    return set(re.findall(r'(?:[A-Z]REG|KREG|UREG)\(\s*"([^"]+)"', text))

win = open("compat/pcps4_winapi.c", errors="ignore").read()
K32, UCRT = names(body(win, "register_kernel32")), names(body(win, "register_ucrt"))
mine = open("compat/pcps4_dxvk_support.c", errors="ignore").read()
parts = mine.split("static const char *const crt[]")
K32 |= set(re.findall(r'R\(d, "([^"]+)"', parts[0]))
UCRT |= set(re.findall(r'R\(d, "([^"]+)"', parts[1])) if len(parts) > 1 else set()
direct = collections.defaultdict(set)
for dll, nm in re.findall(r'R\("([^"]+)",\s*"([^"]+)"', mine):
    direct[dll.lower()].add(nm)

REAL_DLLS = {"dxgi.dll", "d3d11.dll", "d3d10core.dll", "d3d10.dll", "d3d10_1.dll", "d3d9.dll", "d3d8.dll"}

def resolved(dll, name):
    d = dll.lower()
    if d in ("kernel32.dll", "kernelbase.dll"):
        return name in K32
    if d in ("msvcrt.dll", "ucrtbase.dll", "vcruntime140.dll") or d.startswith("api-ms-win-crt"):
        return name in UCRT
    if d in ("user32.dll", "gdi32.dll", "advapi32.dll"):
        return name in direct[d] or name in K32
    if d == "vulkan-1.dll":
        return name == "vkGetInstanceProcAddr"
    if d in REAL_DLLS:
        return None
    return name in direct[d]

missing = collections.defaultdict(lambda: collections.defaultdict(set))
unknown = collections.defaultdict(set)
for path in sys.argv[1:]:
    pe = pefile.PE(path)
    if not hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
        continue
    for d in pe.DIRECTORY_ENTRY_IMPORT:
        dll = d.dll.decode()
        for imp in d.imports:
            if not imp.name:
                unknown[dll].add("ordinal #%s" % imp.ordinal)
                continue
            r = resolved(dll, imp.name.decode())
            if r is False:
                missing[dll.lower()][os.path.basename(path)].add(imp.name.decode())

total = sum(len(v) for per in missing.values() for v in per.values())
print(f"{len(sys.argv) - 1} file(s) scanned; {total} import(s) not provided by the runtime")
for dll in sorted(missing):
    allnames = sorted(set().union(*missing[dll].values()))
    print(f"\n[{dll}] {len(allnames)} missing: " + " ".join(allnames))
for dll, v in unknown.items():
    print(f"\n[{dll}] imported by ordinal (cannot check): {sorted(v)[:8]}")
sys.exit(1 if total else 0)
