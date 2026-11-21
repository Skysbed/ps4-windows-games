#!/usr/bin/env python3
"""Alternate DXVK settings on d3dbench. Stops at the first run that does not finish cleanly and always removes its config."""
import os, ftplib, io, re, statistics, subprocess, sys

RUNNER = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools', 'run_ps4_test.py')
DIR = '/data/pcps4/games/d3dbench'
ROUNDS = 3
CONFIGS = [
    ('default', ''),
    ('descriptor sets', 'dxvk.enableDescriptorHeap = False\ndxvk.enableDescriptorBuffer = False\n'),
    ('no pipeline libs', 'dxvk.enableGraphicsPipelineLibrary = False\n'),
]


def ftp():
    f = ftplib.FTP(); f.connect(os.environ['PS4_HOST'], 2121, timeout=15); f.login(); return f


def get(f, p):
    b = io.BytesIO()
    try: f.retrbinary('RETR ' + p, b.write)
    except ftplib.all_errors: return ''
    return b.getvalue().decode(errors='replace')


def set_conf(text):
    f = ftp()
    try: f.delete(DIR + '/dxvk.conf')
    except ftplib.all_errors: pass
    if text: f.storbinary('STOR ' + DIR + '/dxvk.conf', io.BytesIO(text.encode()))
    f.quit()


def busy():
    f = ftp(); t = get(f, '/data/pcps4/launcher.log').strip().splitlines(); f.quit()
    return not t or 'launcher exit' in t[-1]


data = {name: {} for name, _ in CONFIGS}
order = []
model = {}
ok = True
try:
    for round_no in range(ROUNDS):
        for name, conf in CONFIGS:
            if busy(): print('PS4 became busy: stopping'); ok = False; break
            set_conf(conf)
            out = subprocess.run([sys.executable, '-u', RUNNER, 'd3dbench', '120'], capture_output=True, text=True).stdout
            if 'DONE ok pass=8 fail=0' not in out or 'launcher back' not in out:
                print(f'STOPPED: {name} round {round_no + 1} did not finish cleanly'); print(out[-700:]); ok = False; break
            f = ftp(); log = get(f, DIR + '/d3dbench_d3d11.log'); f.quit()
            m = re.search(r'Binding model: (.+)', log)
            model[name] = (m.group(1).strip() if m else 'unknown') + (', GPL' if 'Graphics pipeline libraries supported' in log else ', no GPL')
            for line in out.splitlines():
                m = re.search(r'INFO (.+?)\s+(\d+)\s+(\d+)$', line)
                if m:
                    scenario = m.group(1).strip()
                    if scenario not in order: order.append(scenario)
                    data[name].setdefault(scenario, []).append(int(m.group(3)))
        if not ok: break
finally:
    set_conf('')

for name, _ in CONFIGS: print(f'{name}: {model.get(name, "not run")}')
print(f'{"total ns per draw (median)":28s}' + ''.join(f'{n:>18s}' for n, _ in CONFIGS))
for s in order:
    row = f'{s:28s}'
    for name, _ in CONFIGS:
        v = data[name].get(s)
        row += f'{int(statistics.median(v)) if v else 0:18d}'
    print(row)
print('config removed')
