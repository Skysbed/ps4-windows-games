#!/usr/bin/env python3
"""Stream the PS4 klog/TTY (port 3232) to a file, UNFILTERED, until timeout.
Run this BEFORE launching a build that may GPU-hang, so the full kernel GPU
exception dump is captured (the ring buffer rotates fast and isn't persisted,
so a live capture through the hang is the only way to keep it).
Usage: klog_capture.py [out.txt] [seconds]"""
import socket, sys, time
OUT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/klog_live.txt"
SECS = int(sys.argv[2]) if len(sys.argv) > 2 else 180
def main():
    s = socket.create_connection(("PCPS4_CONSOLE_IP", 3232), timeout=8)
    s.settimeout(5)
    end = time.time() + SECS
    n = 0
    with open(OUT, "wb") as f:
        print(f"capturing klog -> {OUT} for {SECS}s (launch the build now)...", flush=True)
        while time.time() < end:
            try:
                c = s.recv(8192)
                if not c:
                    break
                f.write(c); f.flush(); n += len(c)
            except socket.timeout:
                continue
            except KeyboardInterrupt:
                break
    s.close()
    print(f"captured {n} bytes to {OUT}")
main()
