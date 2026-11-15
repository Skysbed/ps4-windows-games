# Fetches the latest Minecraft Java release for Windows x64 from Mojang's
# official servers: client jar, libraries (with Windows x64 natives) and assets.
import json, os, sys, hashlib, urllib.request, concurrent.futures as cf
OUT = sys.argv[1]
UA = {"User-Agent": "pcps4-fetch"}
def get(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=120) as r: return r.read()
def fetch(url, path, sha1=None):
    if os.path.exists(path) and (not sha1 or hashlib.sha1(open(path, "rb").read()).hexdigest() == sha1): return 0
    os.makedirs(os.path.dirname(path), exist_ok=True)
    for attempt in range(6):
        try:
            data = get(url)
            if sha1 and hashlib.sha1(data).hexdigest() != sha1: raise ValueError("sha1 mismatch " + url)
            break
        except Exception:
            if attempt == 5: raise
            import time; time.sleep(2 * (attempt + 1))
    open(path, "wb").write(data); return len(data)
def allowed(lib):
    ok = True
    for r in lib.get("rules", []):
        os_ = r.get("os", {}); match = (not os_) or (os_.get("name") == "windows" and os_.get("arch") in (None, "x86_64", "x64"))
        if r["action"] == "allow": ok = match if os_ else True
        elif match: ok = False
    return ok
manifest = json.loads(get("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"))
ver = manifest["latest"]["release"]
vinfo = next(v for v in manifest["versions"] if v["id"] == ver)
vjson = json.loads(get(vinfo["url"]))
os.makedirs(OUT, exist_ok=True); json.dump(vjson, open(os.path.join(OUT, "version.json"), "w"))
print("version", ver, "java", vjson.get("javaVersion", {}).get("majorVersion"), flush=True)
jobs = [(vjson["downloads"]["client"]["url"], os.path.join(OUT, "client.jar"), vjson["downloads"]["client"]["sha1"])]
cp = []
for lib in vjson["libraries"]:
    if not allowed(lib): continue
    art = lib.get("downloads", {}).get("artifact")
    if not art: continue
    name = lib["name"]
    if ":natives-windows" in name and not name.endswith("natives-windows"): continue   # skip -arm64 / -x86 natives
    p = os.path.join(OUT, "libraries", art["path"]); jobs.append((art["url"], p, art["sha1"])); cp.append(os.path.relpath(p, OUT))
ai = vjson["assetIndex"]; idx_path = os.path.join(OUT, "assets", "indexes", ai["id"] + ".json")
fetch(ai["url"], idx_path, ai["sha1"])
for obj in json.load(open(idx_path))["objects"].values():
    h = obj["hash"]; jobs.append(("https://resources.download.minecraft.net/%s/%s" % (h[:2], h), os.path.join(OUT, "assets", "objects", h[:2], h), h))
open(os.path.join(OUT, "classpath.txt"), "w").write("\n".join(cp + ["client.jar"]) + "\n")
print("files", len(jobs), flush=True)
total = 0
with cf.ThreadPoolExecutor(32) as ex:
    for i, n in enumerate(ex.map(lambda j: fetch(*j), jobs)):
        total += n
        if i % 1000 == 0: print(i, "files", total >> 20, "MB", flush=True)
print("DONE", ver, total >> 20, "MB new", flush=True)
