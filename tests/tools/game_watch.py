#!/usr/bin/env python3
"""Auto-launch a real game, stream its log, and report the moment it exits.

usage: game_watch.py <tile> <seconds> <log path> [--keep]
       game_watch.py --stop

--keep leaves the game running after the time is up so its screen can be checked.
"""
import os, ftplib, io, sys, time

HOST, PORT = os.environ['PS4_HOST'], 2121
EVIDENCE = ['/data/pcps4_sigframe.txt', '/data/stkuser/hs_err_java.log']


def ftp():
    f = ftplib.FTP(); f.connect(HOST, PORT, timeout=10); f.login(); return f


def get(f, path):
    b = io.BytesIO()
    try: f.retrbinary('RETR ' + path, b.write)
    except ftplib.all_errors: return None
    return b.getvalue().decode('utf-8', 'replace')


def put(f, path, data):
    try: f.delete(path)
    except ftplib.all_errors: pass
    f.storbinary('STOR ' + path, io.BytesIO(data))


def at_launcher(f):
    lines = (get(f, '/data/pcps4/launcher.log') or '').strip().splitlines()
    return bool(lines) and 'launcher exit' not in lines[-1] and any('pad open' in l for l in lines)


def stop():
    f = ftp(); put(f, '/data/pcps4_stop', b'1\n'); f.quit()
    end = time.time() + 45
    while time.time() < end:
        time.sleep(1)
        try:
            f = ftp(); ok = at_launcher(f); f.quit()
            if ok:
                print('closed cleanly, launcher back', flush=True); return True
        except ftplib.all_errors + (OSError,): pass
    f = ftp()
    for p in ('/data/pcps4_stop', '/data/pcps4_hangdump'):
        try: f.delete(p)
        except ftplib.all_errors: pass
    f.quit()
    print('GAME IS WEDGED: it ignored the stop request', flush=True)
    return False


if sys.argv[1] == '--stop':
    sys.exit(0 if stop() else 1)

tile, limit, log = sys.argv[1], int(sys.argv[2]), sys.argv[3]
keep = '--keep' in sys.argv

f = ftp()
if not at_launcher(f): sys.exit('PS4 is not at the launcher: someone may be playing. Nothing launched.')
before = {p: get(f, p) for p in EVIDENCE}
old_log = get(f, log)
put(f, '/data/pcps4/auto_launch.txt', (tile + '\n').encode())
f.quit()
print(f'launched {tile}', flush=True)

t0 = time.time(); started = False; shown = 0; fresh = False; last_change = t0; exited = False
while time.time() - t0 < limit:
    time.sleep(2)
    try:
        f = ftp(); back = at_launcher(f); text = get(f, log)
        if 'auto id not found' in (get(f, '/data/pcps4/launcher.log') or '')[-200:]:
            f.delete('/data/pcps4/auto_launch.txt'); f.quit(); sys.exit(f'launcher does not know tile {tile}; request removed')
        f.quit()
    except ftplib.all_errors + (OSError,):
        continue
    if not back: started = True
    if text is not None and (fresh or text != old_log):
        if not fresh:
            fresh = True
            shown = 0 if old_log is None or not text.startswith(old_log[:200]) else len(old_log.splitlines())
        lines = text.splitlines()
        if len(lines) > shown:
            for line in lines[shown:]: print(f'{int(time.time() - t0):3d}s {line[:170]}', flush=True)
            shown = len(lines); last_change = time.time()
    if started and back:
        exited = True
        print(f'{int(time.time() - t0)}s GAME EXITED on its own', flush=True)
        break
    if not started and time.time() - t0 > 40:
        print('game never started', flush=True); break

if not exited:
    print(f'still running at {int(time.time() - t0)}s; log quiet for {int(time.time() - last_change)}s', flush=True)

f = ftp()
for p in EVIDENCE:
    now = get(f, p)
    if now is not None and now != before[p]:
        print(f'--- NEW {p}\n{now[:1500]}', flush=True)
f.quit()

if not exited and not keep:
    sys.exit(0 if stop() else 1)
