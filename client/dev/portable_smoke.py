"""Opt-in: two owned, normal FL profiles against the local Cloudflare test relay."""
import argparse
import ctypes
import ctypes.wintypes as W
import json
import os
from pathlib import Path
import subprocess
import struct
import sys
import tempfile
import time
import urllib.request
from urllib.parse import urlsplit

import fl
import flbuild

parser = argparse.ArgumentParser()
parser.add_argument('--host', required=True, choices=('24', '25', '26'))
parser.add_argument('--member', required=True, choices=('24', '25', '26'))
parser.add_argument('--relay', default='http://127.0.0.1:49938')
args = parser.parse_args()
origin = urlsplit(args.relay)
if (origin.scheme != 'https' and not (origin.scheme == 'http' and origin.hostname in ('127.0.0.1', 'localhost'))) or origin.username or origin.password or origin.query or origin.fragment or origin.path not in ('', '/'):
    raise SystemExit('Use a relay HTTPS origin or loopback HTTP origin.')
relay = args.relay.rstrip('/')
if fl.pids():
    raise SystemExit('Close other FL Studio processes before this opt-in test.')
root = Path(__file__).resolve().parents[1]
evidence = root / 'desktop/test/.tmp'
evidence.mkdir(parents=True, exist_ok=True)
run = Path(tempfile.mkdtemp(prefix=f'portable-{args.host}-{args.member}-', dir=evidence))
os.environ['FLIM_LOG_DIR'] = str(run)
owned = []


def request(route, body):
    req = urllib.request.Request(relay + route, json.dumps(body).encode(),
                                 {'Content-Type': 'application/json', 'User-Agent': 'flim-compatibility-test/0.2.11'})
    with urllib.request.urlopen(req, timeout=10) as response:
        return json.load(response)


def launch(major, role, ticket):
    env = dict(os.environ)
    for key in list(env):
        if key.startswith('FLIM_'):
            del env[key]
    websocket = ('wss://' if origin.scheme == 'https' else 'ws://') + origin.netloc + ticket['websocket']
    env.update(FLIM_AUTOCONNECT=websocket, FLIM_PASSWORD=ticket['password'],
               FLIM_HOST='1' if role == 'host' else '0', FLIM_NAME=f'Owned test FL{major}', FLIM_LOG_DIR=str(run))
    if role == 'host':
        env['FLIM_OWNER_KEY'] = ticket['hostToken']
    install = Path(os.environ['ProgramFiles']) / f'Image-Line/FL Studio 20{major}'
    command = [str(root / 'out-experimental/flim.exe'), '--desktop', '--fl-dir', str(install)]
    if major == '24':
        command.append('--allow-experimental')
    result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=45, creationflags=subprocess.CREATE_NO_WINDOW)
    result.check_returncode()
    launched = json.loads(result.stdout)
    assert launched['launched'] and isinstance(launched['pid'], int)
    pid = launched['pid']
    owned.append(pid)
    return pid


def wait_log(pid, marker, timeout=75):
    started = time.monotonic()
    while time.monotonic() - started < timeout:
        file = run / f'native-{pid}.log'
        text = file.read_text(encoding='utf-8', errors='replace') if file.exists() else ''
        if 'SHARING stopped' in text or 'NET relay error' in text:
            raise RuntimeError('Native failure; inspect owned evidence ' + str(run))
        if marker in text:
            return text
        time.sleep(.25)
    raise TimeoutError(f'{pid}: {marker}; evidence {run}')


def close_owned(pid):
    for _, hwnd, cls, _ in fl.windows((pid,)):
        if cls == 'TFruityLoopsMainForm':
            fl.u32.PostMessageW(hwnd, 0x10, 0, 0)
    until = time.monotonic() + 20
    while pid in fl.pids() and time.monotonic() < until:
        for _, hwnd, cls, title in fl.windows((pid,)):
            if cls != 'TMsgForm' or title != 'Confirm':
                continue
            buttons = [h for h, name in fl.children(hwnd) if name == 'TQuickFocusBtn']
            if len(buttons) != 3:
                continue
            def left(h):
                rect = W.RECT()
                fl.u32.GetWindowRect(h, ctypes.byref(rect))
                return rect.left
            # This owned scratch run has the known Yes/No/Cancel save prompt.
            fl.post_click(sorted(buttons, key=left)[1])
        time.sleep(.25)
    if pid in fl.pids():
        raise RuntimeError(f'Owned FL {pid} needs closing; it was not force-terminated.')


def seed_host(pid):
    fl.inject(pid)
    p, v = fl.Probe(pid), flbuild.build(pid)
    p.call(v.REC, 0, 5200, 1 | 16)
    p.call(v.PATNAME, 15, p.mkstr('Initial shared notes'))
    arr, rec = p.call(v.NOTEARR, 15, 1), p.alloc(24)
    p.w(rec, struct.pack('<IHHIHHBBBBBBBB', 96, 0x4000, 0, 192, 64, 0, 120, 0, 64, 0, 64, 100, 128, 128))
    p.cmd('call %x %d %d' % (p.q(p.q(arr) + 8), arr, rec))
    p.call(v.MIXNAME, 7, p.mkstr('Initial mix'), 1)
    p.call(v.MIXCOLOR, p.g(v.MIXARR) + 7 * v.MIX_STRIDE, 0x225588)
    mix = p.q(p.g(v.MIXOBJ))
    p.call(v.SETROUTE, mix, 7, 8, 1, 1)
    p.call(v.ROUTEUPD, mix)
    time.sleep(2)


def verify_bootstrap(pid):
    fl.inject(pid)
    p, v = fl.Probe(pid), flbuild.build(pid)
    pat = p.g(v.PATARR) + 15 * v.PAT_STRIDE
    mix = p.g(v.MIXARR) + 7 * v.MIX_STRIDE
    def notes():
        arr = p.q(pat + 0x28)
        return p.d(arr + 0x14) if arr else 0
    checks = [
        ('initial note', notes, 1),
        ('initial pattern name', lambda: p.ustr(p.q(pat)), 'Initial shared notes'),
        ('initial channel volume', lambda: p.call(v.REC, 0, 0, 2), 5200),
        ('initial mixer name', lambda: p.ustr(p.q(mix + 0x0c)), 'Initial mix'),
        ('initial mixer color', lambda: p.d(mix + 4) & 0xffffff, 0x225588),
        ('initial mixer routing', lambda: p.r(mix + v.MIX_ROUTE + 8 * 8 + 4, 1)[0], 1),
    ]
    evidence = []
    for name, read, expected in checks:
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            actual = read()
            if actual == expected:
                evidence.append(name)
                print('PASS', name, flush=True)
                break
            time.sleep(.1)
        else:
            raise AssertionError(f'{name}: expected {expected!r}, received {actual!r}')
    return evidence


try:
    ticket = request('/rooms', {})
    host = launch(args.host, 'host', ticket)
    wait_log(host, 'NET welcome accepted')
    seed_host(host)
    member = launch(args.member, 'member', ticket)
    wait_log(member, 'NET welcome accepted')
    if int(args.member) < int(args.host):
        wait_log(member, 'PORTABLE ready:')
    else:
        wait_log(member, 'PROJECT opened')
    bootstrap = verify_bootstrap(member)
    env = dict(os.environ, FLIM_TEST_A=str(host), FLIM_TEST_B=str(member), FLIM_LOG_DIR=str(run))
    result = subprocess.run([sys.executable, str(root / 'dev/recording_smoke.py')], env=env, capture_output=True, text=True, timeout=150)
    (run / 'notes.txt').write_text(result.stdout + result.stderr, encoding='utf-8')
    print(result.stdout, flush=True)
    result.check_returncode()
    features = subprocess.run([sys.executable, str(root / 'dev/feature_test.py'), 'params', 'mixer', 'returnpath'],
                              env=env, capture_output=True, text=True, timeout=150)
    (run / 'controls.txt').write_text(features.stdout + features.stderr, encoding='utf-8')
    print(features.stdout, flush=True)
    features.check_returncode()
    status = request('/rooms/status', {'room': ticket['room'], 'password': ticket['password']})
    assert status['state'] == 'active' and len(status['peers']) == 2
    (run / 'result.json').write_text(json.dumps({'host': args.host, 'member': args.member, 'pids': owned,
        'relay': relay, 'bootstrap': bootstrap, 'notesPassed': True, 'controlsPassed': True, 'portable': int(args.member) < int(args.host), 'roomStillActive': True}, indent=2), encoding='utf-8')
    print('Evidence:', run, flush=True)
finally:
    failures = []
    for pid in reversed(owned):
        try:
            close_owned(pid)
        except Exception as error:
            failures.append(str(error))
    if failures:
        raise RuntimeError('; '.join(failures))
