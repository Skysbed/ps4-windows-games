# Writes a Java @argument file that starts Minecraft offline (singleplayer)
# from the files mc_fetch.py downloaded, with Windows paths for the PS4.
import json, os, sys, uuid
SRC, ROOT, JAVA_HEAP, NAME = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]   # ROOT: Windows path on the console
v = json.load(open(os.path.join(SRC, "version.json")))
cp = [l.strip() for l in open(os.path.join(SRC, "classpath.txt")) if l.strip()]
win = lambda rel: ROOT + "\\" + rel.replace("/", "\\")
subst = {
    "${auth_player_name}": NAME, "${version_name}": v["id"], "${game_directory}": win("game"),
    "${assets_root}": win("assets"), "${assets_index_name}": v["assetIndex"]["id"],
    "${auth_uuid}": str(uuid.uuid3(uuid.NAMESPACE_DNS, "OfflinePlayer:" + NAME)).replace("-", ""),
    "${auth_access_token}": "0", "${clientid}": "0", "${auth_xuid}": "0", "${user_type}": "legacy",
    "${version_type}": v.get("type", "release"), "${natives_directory}": win("natives"),
    "${launcher_name}": "pcps4", "${launcher_version}": "1", "${classpath}": ";".join(win(p) for p in cp),
    "${classpath_separator}": ";", "${library_directory}": win("libraries"),
}
def expand(items):
    out = []
    for a in items:
        if isinstance(a, dict):
            if any(r.get("features") or (r.get("os") and r["os"].get("name") != "windows") for r in a.get("rules", [])): continue
            vals = a["value"] if isinstance(a["value"], list) else [a["value"]]
        else: vals = [a]
        for s in vals:
            for k, val in subst.items(): s = s.replace(k, val)
            out.append(s)
    return out
jvm = ["-Xmx" + JAVA_HEAP, "-Xms512M", "-Dorg.lwjgl.system.SharedLibraryExtractPath=" + win("natives"),
       "-Djava.io.tmpdir=" + win("tmp")] + expand(v["arguments"]["jvm"])
game = expand(v["arguments"]["game"]) + ["--width", "1920", "--height", "1080", "--fullscreen"]
q = lambda s: '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'
with open(os.path.join(SRC, "launch.args"), "w") as f:
    for a in jvm + [v["mainClass"]] + game: f.write(q(a) + "\n")
print("main", v["mainClass"], "jvm args", len(jvm), "game args", len(game), "classpath", len(cp))
