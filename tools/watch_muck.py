"""Start before launching Muck. Report only player and hang logs changed since arming."""
import argparse
import ftplib
import io
import time

PLAYER = "/data/pcps4/games/muck/player.log"
HANG = "/data/pcps4/hang.log"


def stamp(ftp, path):
    try:
        return ftp.sendcmd("MDTM " + path), ftp.size(path)
    except ftplib.error_perm as exc:
        if str(exc).startswith("550"):
            return None
        raise


def watch(host, port):
    seen = {}
    launched = False
    pending_dump = False
    while True:
        try:
            with ftplib.FTP() as ftp:
                ftp.connect(host, port, timeout=8)
                ftp.login()
                ftp.voidcmd("TYPE I")
                now = {path: stamp(ftp, path) for path in (PLAYER, HANG)}
                if not seen:
                    print("Armed. Waiting for a new Muck run.", flush=True)
                else:
                    if now[HANG] and now[HANG] != seen[HANG]:
                        pending_dump = True
                    if now[PLAYER] and now[PLAYER] != seen[PLAYER]:
                        log = io.BytesIO()
                        ftp.retrbinary("RETR " + PLAYER, log.write)
                        lines = log.getvalue().decode(errors="replace").splitlines()
                        launched = True
                        print(f"{len(lines)} lines | {lines[-1][:160] if lines else '(empty)'}", flush=True)
                        if any("Completed reload" in line or "UnloadTime:" in line for line in lines):
                            print("Unity passed assembly reload.", flush=True)
                            return
                    if launched and pending_dump:
                        print("Hang report changed. Inspect the stacks before diagnosing a freeze.", flush=True)
                        pending_dump = False
                seen = now
        except (OSError, EOFError, ftplib.error_temp) as exc:
            print(f"FTP interrupted: {exc}", flush=True)
        time.sleep(3)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("host")
    p.add_argument("--port", type=int, default=2121)
    args = p.parse_args()
    watch(args.host, args.port)
