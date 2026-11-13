#!/usr/bin/env python3
"""Check a converted PS4 module: every import must be tagged with a library
that actually DEFINES that symbol.  create-fself can tag an import with a
library that merely mentions the name (as one of its own imports), which the
console only reports at call time as PRX_NOT_RESOLVED_FUNCTION.

usage: verify_imports.py MODULE.prx MODULE.so  NAME=LIB.so [NAME=LIB.so ...]
"""
import sys, re, hashlib, struct, base64, subprocess
def nid(name):
    h = hashlib.sha1(name.encode() + bytes.fromhex("518D64A635DED8C1E6B039B1C3E55230")).digest()[:8]
    return base64.b64encode(struct.pack(">Q", struct.unpack("<Q", h)[0]), altchars=b"+-").decode()[:11]
def nm(path, want):
    out = subprocess.run(["nm", "-D", path], capture_output=True, text=True).stdout
    r = set()
    for l in out.splitlines():
        p = l.split()
        if want == "U" and len(p) == 2 and p[0] == "U": r.add(p[1])
        if want == "D" and len(p) == 3 and p[1] in "TDBWVRi": r.add(p[2])
    return r
prx, so = sys.argv[1], sys.argv[2]
libs = dict(a.split("=", 1) for a in sys.argv[3:])
data = open(prx, "rb").read()
tag = {m.group(1).decode(): m.group(2).decode() for m in re.finditer(rb"([A-Za-z0-9+\-]{11})#([A-Za-z0-9+\-]{1,2})#[A-Za-z0-9+\-]{1,2}\x00", data)}
defs = {name: nm(path, "D") for name, path in libs.items()}
und = nm(so, "U")
# learn which tag id belongs to which library from symbols only ONE library defines
ident = {}
for s in und:
    owners = [n for n, d in defs.items() if s in d]
    t = tag.get(nid(s))
    if len(owners) == 1 and t: ident.setdefault(t, {}).setdefault(owners[0], 0); ident[t][owners[0]] += 1
idmap = {t: max(v, key=v.get) for t, v in ident.items()}
bad = []
for s in sorted(und):
    t = tag.get(nid(s)); owners = [n for n, d in defs.items() if s in d]
    if not t or not owners: continue
    lib = idmap.get(t)
    if lib and lib not in owners: bad.append((s, lib, owners))
print(f"{prx.split('/')[-1]}: {len(und)} imports; tag ids -> {idmap}")
for s, lib, owners in bad: print(f"  MIS-TAGGED {s}: tagged as {lib}, but defined by {owners}")
print("  imports OK" if not bad else f"  {len(bad)} MIS-TAGGED IMPORT(S)")
sys.exit(1 if bad else 0)
