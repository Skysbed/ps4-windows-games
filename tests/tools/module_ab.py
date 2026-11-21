#!/usr/bin/env python3
"""Fair A/B of two builds of a PS4 module, alternating runs of drawbench and d3dbench.

usage: module_ab.py <module.prx> <backup suffix> [rounds]
The new build must be installed as <module.prx>, the old one as <module.prx>.<suffix>.
If any run fails, the OLD build is left installed and the new one is kept as <module.prx>.ab-new.
"""
import os, ftplib, io, re, statistics, subprocess, sys

RUNNER = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'run_ps4_test.py')
DIR = '/data/pcps4/sce_module/'
MOD = sys.argv[1]
OLD = MOD + '.' + sys.argv[2]
TMP = MOD + '.ab-new'
ROUNDS = int(sys.argv[3]) if len(sys.argv) > 3 else 5
TESTS = [('drawbench', 'DONE ok pass=8 fail=0', 2), ('d3dbench', 'DONE ok pass=8 fail=0', 3)]


def ftp():
    f = ftplib.FTP(); f.connect(os.environ['PS4_HOST'], 2121, timeout=20); f.login(); return f


def names(f):
    return [x.rsplit('/', 1)[-1] for x in f.nlst(DIR)]


def busy():
    f = ftp(); b = io.BytesIO(); f.retrbinary('RETR /data/pcps4/launcher.log', b.write); f.quit()
    t = b.getvalue().decode(errors='replace').strip().splitlines()
    return not t or 'launcher exit' in t[-1]


def use_old():
    f = ftp(); f.rename(DIR + MOD, DIR + TMP); f.rename(DIR + OLD, DIR + MOD); f.quit()


def use_new():
    f = ftp(); f.rename(DIR + MOD, DIR + OLD); f.rename(DIR + TMP, DIR + MOD); f.quit()


f = ftp(); n = names(f); f.quit()
if OLD not in n or MOD not in n or TMP in n: sys.exit(f'unexpected module state: {OLD in n} {MOD in n} {TMP in n}')

data = {}
order = {t[0]: [] for t in TESTS}
on_old = False
failed = None
try:
    for r in range(ROUNDS):
        for which in ('old', 'new') if r % 2 == 0 else ('new', 'old'):
            if (which == 'old') != on_old:
                use_old() if which == 'old' else use_new()
                on_old = which == 'old'
            for test, good, col in TESTS:
                if busy(): failed = 'PS4 became busy'; break
                out = subprocess.run([sys.executable, '-u', RUNNER, test, '150'], capture_output=True, text=True).stdout
                if good not in out or 'launcher back' not in out:
                    failed = f'{test} on the {which} build did not finish cleanly:\n' + out[-500:]; break
                for line in out.splitlines():
                    m = re.search(r'INFO (.+?)\s+(\d+)\s+(\d+)(?:\s+[\d.]+)?$', line)
                    if m:
                        s = m.group(1).strip()
                        if s not in order[test]: order[test].append(s)
                        data.setdefault((test, s, which), []).append(int(m.group(col)))
            if failed: break
        if failed: break
finally:
    if failed:
        if not on_old: use_old()
        print('FAILED:', failed); print('old build left installed; new build kept as', TMP)
    elif on_old:
        use_new()

for test, _, _ in TESTS:
    print(f'--- {test} (ns per draw, median)')
    print(f'{"scenario":28s}{"old":>8s}{"new":>8s}{"change":>9s}')
    for s in order[test]:
        a, b = data.get((test, s, 'old')), data.get((test, s, 'new'))
        if a and b:
            x, y = statistics.median(a), statistics.median(b)
            print(f'{s:28s}{x:8.0f}{y:8.0f}{(y - x) / x * 100:+8.1f}%')
sys.exit(1 if failed else 0)
