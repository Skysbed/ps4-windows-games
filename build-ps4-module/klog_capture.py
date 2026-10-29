import os, socket, sys, time
out = sys.argv[1]; dur = int(sys.argv[2])
ip = os.environ.get("PCPS4_CONSOLE_IP")
if not ip:
    sys.exit("set PCPS4_CONSOLE_IP to your PS4's LAN IP (klog listens on port 3232, GoldHEN klog plugin)")
s = socket.socket(); s.settimeout(dur); s.connect((ip, 3232))
end = time.time() + dur
with open(out, "wb") as f:
    try:
        while time.time() < end:
            s.settimeout(max(0.5, end - time.time()))
            d = s.recv(8192)
            if not d: break
            f.write(d); f.flush()
    except Exception: pass
