"""Mirror a single run's Mesa log over FTP. Start with a unique remote log path."""
import argparse
import ftplib
from pathlib import Path
import time


def mirror(host, port, remote, output):
    offset = 0
    with output.open("xb", buffering=0) as dest:
        while True:
            try:
                with ftplib.FTP() as ftp:
                    ftp.connect(host, port, timeout=5)
                    ftp.login()
                    ftp.voidcmd("TYPE I")
                    while True:
                        try:
                            size = ftp.size(remote)
                        except ftplib.error_perm as exc:
                            if not str(exc).startswith("550"):
                                raise
                            time.sleep(1)
                            continue
                        if size < offset:
                            raise RuntimeError("Remote log was truncated. Start a new capture.")
                        if size > offset:
                            def save(block):
                                nonlocal offset
                                dest.write(block)
                                offset += len(block)
                            ftp.retrbinary("RETR " + remote, save, rest=offset or None)
                        time.sleep(1)
            except (OSError, EOFError, ftplib.error_temp) as exc:
                print(f"Connection interrupted at byte {offset}: {exc}", flush=True)
                time.sleep(2)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host")
    parser.add_argument("remote")
    parser.add_argument("output", type=Path)
    parser.add_argument("--port", type=int, default=2121)
    args = parser.parse_args()
    mirror(args.host, args.port, args.remote, args.output)
