#!/usr/bin/env python3
"""Auto-launch a small PCPS4 test tile and stream its result file.

usage: run_ps4_test.py <tile> [seconds] [test folder]

The test writes /data/pcps4/games/<test folder>/result.txt and ends it with a line starting DONE.
The launcher reads its tile list only when it starts, so a new test can run through a tile it already
knows: pass that tile, and the new test's folder as the third argument.
If a game is still running it is closed first. A stalled test gets a freeze report and is closed.
"""
import os, ftplib, io, sys, time

HOST, PORT = os.environ['PS4_HOST'], 2121
tile = sys.argv[1]
limit = int(sys.argv[2]) if len(sys.argv) > 2 else 120
RESULT = f'/data/pcps4/games/{sys.argv[3] if len(sys.argv) > 3 else tile}/result.txt'


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
    text = (get(f, '/data/pcps4/launcher.log') or '').strip().splitlines()
    return bool(text) and 'launcher exit' not in text[-1] and any('pad open' in l for l in text)


def wait_launcher(seconds):
    end = time.time() + seconds
    while time.time() < end:
        try:
            f = ftp(); ok = at_launcher(f); f.quit()
            if ok: return True
        except ftplib.all_errors + (OSError,): pass
        time.sleep(1)
    return False


def close_game():
    f = ftp(); put(f, '/data/pcps4_stop', b'1\n'); f.quit()
    if wait_launcher(45): return True
    # A wedged game never reads these; left behind they would hit the next launch.
    f = ftp()
    for path in ('/data/pcps4_stop', '/data/pcps4_hangdump'):
        try: f.delete(path)
        except ftplib.all_errors: pass
    f.quit()
    return False


f = ftp()
ready = at_launcher(f)
f.quit()
if not ready:
    print('a game is running: closing it', flush=True)
    if not close_game(): sys.exit('launcher did not come back')
    time.sleep(2)

f = ftp()
try: f.delete(RESULT)
except ftplib.all_errors: pass
put(f, '/data/pcps4/auto_launch.txt', (tile + '\n').encode())
f.quit()
print(f'launched {tile}', flush=True)

t0 = time.time(); shown = 0; last_change = time.time(); finished = False
while time.time() - t0 < limit:
    time.sleep(1)
    try:
        f = ftp(); text = get(f, RESULT)
        if text is None and 'auto id not found' in (get(f, '/data/pcps4/launcher.log') or '')[-200:]:
            f.delete('/data/pcps4/auto_launch.txt'); f.quit()
            sys.exit(f'the launcher does not know tile "{tile}" (it reads tiles at startup); request removed')
        f.quit()
    except ftplib.all_errors + (OSError,):
        continue
    if text is None:
        if time.time() - t0 > 45: print('no result file after 45 s'); break
        continue
    lines = text.splitlines()
    if len(lines) > shown:
        for line in lines[shown:]: print(f'{int(time.time() - t0):3d}s {line}', flush=True)
        shown = len(lines); last_change = time.time()
    if any(l.startswith('DONE') for l in lines): finished = True; break
    if time.time() - last_change > 60: print('stalled for 60 s'); break

if not finished:
    f = ftp(); put(f, '/data/pcps4_hangdump', b'1\n'); f.quit()
    time.sleep(4)
    print('freeze report requested: /data/pcps4/hang.log', flush=True)

if not wait_launcher(8):
    print('closing the test', flush=True)
    print('launcher back' if close_game() else 'launcher did NOT come back', flush=True)
else:
    print('launcher back', flush=True)
