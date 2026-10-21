# Feature test for a two-FL session (restart_test.ps1): each step changes something in the leader A
# through FL's own functions (probe), then waits for the member B to show the same state.
# Works with every FL build in the engine's table (flbuild.py). No mouse / keyboard input is used.
#   python feature_test.py            (after restart_test.ps1; injects the probe into A and B)
#   python feature_test.py params mixer   (only those steps)
import fl, mk, flbuild, struct, sys, time, threading

A, B = mk.A, mk.B
results = []


def wait_for(fn, want, timeout=8.0):
    t0, got = time.time(), None
    while time.time() - t0 < timeout:
        try: got = fn()
        except Exception as e: got = 'ERR %s' % e
        if got == want: return True, got
        time.sleep(0.25)
    return False, got


def check(name, fn, want, timeout=8.0):
    ok, got = wait_for(fn, want, timeout)
    results.append((name, ok))
    print('%-4s %-34s %s' % ('PASS' if ok else 'FAIL', name, '' if ok else 'want %r, got %r' % (want, got)), flush=True)
    return ok


def bg_call(p, cmd):
    """a main-thread call that may open an FL dialog: run it in a thread so the dialog can be closed"""
    out = {}
    t = threading.Thread(target=lambda: out.setdefault('r', p.cmd(cmd)), daemon=True)
    t.start()
    return t, out


def confirm_yes(pids):
    """FL's own message box: click its leftmost button (Yes / OK)"""
    import ctypes, ctypes.wintypes as W
    for pid, h, c, t in fl.windows(pids):
        if c != 'TMsgForm': continue
        btns = [k for k, cc in fl.children(h) if cc == 'TQuickFocusBtn']
        def left(k):
            r = W.RECT(); fl.u32.GetWindowRect(k, ctypes.byref(r)); return r.left
        if btns: fl.post_click(min(btns, key=left))


def close_dialogs_until(t, pids, timeout=8.0):
    t0 = time.time()
    while t.is_alive() and time.time() - t0 < timeout:
        fl.close_dialogs(pids, cls=('TNameEditForm',))
        confirm_yes(pids)
        time.sleep(0.3)
    t.join(1)


def mixrec(p, t): return p.g(mk.V(p).MIXARR) + t * 0x1474
def arrangement(p):
    v = mk.V(p)
    return p.q(p.g(v.ARRS) + 8 * p.d(p.a(v.ARRCUR)))


def t_params(a, b, v):
    REC = v.REC
    a.call(REC, 0, 6400, 1 | 16)                                   # channel 1 volume
    check('channel volume (REC)', lambda: b.call(REC, 0, 0, 2), 6400)
    mid = 0x70000000 + (1 << 22) + 0x1fc0                          # mixer insert 1 volume
    a.call(REC, mid, 7000, 1 | 16)
    check('mixer volume (REC)', lambda: b.call(REC, mid, 0, 2), 7000)


def t_mixer(a, b, v):
    a.call(v.MUTE, 2, 0)
    check('mixer mute', lambda: b.r(mixrec(b, 2) + 0x18, 1)[0], 0)
    a.call(v.MIXNAME, 3, a.mkstr('Drums'), 1)
    check('mixer track name', lambda: b.ustr(b.q(mixrec(b, 3) + 0x0c)), 'Drums')
    a.call(v.MIXCOLOR, mixrec(a, 4), 0x3366ff)
    check('mixer track color', lambda: b.d(mixrec(b, 4) + 4) & 0xffffff, 0x3366ff)
    mix = a.q(a.g(v.MIXOBJ))
    a.call(v.SETROUTE, mix, 5, 6, 1, 1); a.call(v.ROUTEUPD, mix)
    check('mixer routing', lambda: b.r(mixrec(b, 5) + 0x2e4 + 6 * 8 + 4, 1)[0], 1)


def t_channels(a, b, v):
    n0 = len(mk.chans(b))
    a.call(v.ADDCHAN, 0xFFFFFFFFFFFFFFFF, 0, 1)
    check('channel add', lambda: len(mk.chans(b)), n0 + 1, 15)
    items = a.q(a.q(a.g(v.CHANLIST)) + 8)
    ch = a.q(items + 8 * n0)
    a.cmd('call %x %d %d' % (a.q(a.q(ch) + 0x70), ch, a.mkstr('Lead')))
    check('channel name', lambda: mk.chans(b)[n0][1] if len(mk.chans(b)) > n0 else None, 'Lead')
    a.call(v.DELCHAN, n0, 0, 3)
    check('channel delete', lambda: len(mk.chans(b)), n0, 15)


def t_patterns(a, b, v):
    mk.setup(a)
    want = [(1, 'Verse', 1), (2, 'Chorus', 1), (3, 'Bridge', 1)]
    check('pattern names + notes', lambda: mk.pats(b, 3), want)
    # Insert one before pattern 2 (A: FL opens a rename box, closed here; B: answered by flim)
    t, _ = bg_call(a, 'call %x %d 2' % (a.q(a.q(a.q(a.q(a.g(v.FLPROJ)) + 0x10)) + 0xf8), a.q(a.q(a.g(v.FLPROJ)) + 0x10)))
    close_dialogs_until(t, [A])
    check('pattern op: insert', lambda: mk.pats(b, 4), [(1, 'Verse', 1), (2, '', 0), (3, 'Chorus', 1), (4, 'Bridge', 1)], 15)
    # Delete pattern 2 (A: FL asks "are you sure", confirmed here; B: message box answered by flim's hook)
    arr = a.alloc(8); a.w(arr, struct.pack('<i', 2))
    proj = a.q(a.q(a.g(v.FLPROJ)) + 0x10)
    t, _ = bg_call(a, 'call %x %d %d 0' % (a.q(a.q(proj) + 0xe0), proj, arr))
    close_dialogs_until(t, [A])
    check('pattern op: delete (msgbox hook)', lambda: mk.pats(b, 3), want, 15)


def t_song(a, b, v):
    # what the toolbar's time settings do (engine: tsig_apply): controls from the globals, then the setter
    pn, pd = a.g(v.TSNUM), a.g(v.TSDEN)
    on, od = a.d(pn), a.d(pd)
    form = a.q(a.g(v.MAINFORM))
    a.w(pn, struct.pack('<i', 3)); a.call(v.TSCTLS, form)
    a.w(pn, struct.pack('<i', on)); a.w(pd, struct.pack('<i', od)); a.call(v.TSSET, form)
    check('time signature set in A', lambda: mk.ts(a)[0], 3, 3)
    check('project time signature', lambda: mk.ts(b)[0], 3, 12)
    pl = a.q(a.g(v.PLFORM))
    a.cmd('call %x %d 384 0 0 4 4' % (a.a(v.FORMADDMARK), pl))
    check('playlist marker', lambda: [m[0] for m in mk.marks(b)][-1:] if mk.marks(b) else [], [384], 12)
    arrA = arrangement(a)
    a.call(v.PLTRACK, arrA, 3, a.mkstr('Bass'), 0x00ff00)
    check('playlist track name', lambda: b.ustr(b.q(arrangement(b) + 3 * 0x114 + 0x24)), 'Bass', 12)


def t_patmarks(a, b, v):
    mk.show(a, 3)
    time.sleep(1)
    cur = a.d(a.g(v.CURPAT))
    mk.pr_add_marker(a, 0, 8, 7, 8)
    check('pattern time signature (pattern %d)' % cur, lambda: [(m[1], m[3][:4]) for m in mk.pmarks(b, cur)][:1], [(8, '0708')], 12)


def t_notedel(a, b, v):
    arr = a.call(v.NOTEARR, 3, 1)                                  # pattern 3 has one note (patterns step)
    a.call(v.NOTEDEL, arr, 0, 0)
    a.call(v.GRIDREFRESH, a.q(a.q(a.q(a.g(v.CHANLIST)) + 8)) + v.CH_GRID)
    check('note delete', lambda: mk.pats(b, 3)[2][2], 0, 12)


def t_layout(a, b, v):
    """the offsets behind other people's mouse arrows read sensibly (arrows need a real mouse)"""
    import ctypes, struct as st
    mk.show(a, 3); mk.show(a, 2); time.sleep(1.5)
    for nm, gv, grid in (('piano roll', v.PRFORM, v.PR_GRID), ('playlist', v.PLFORM, v.PL_GRID)):
        f = a.q(a.g(gv))
        hwnd = a.q(f + 0x45c) & 0xffffffff
        cls = ctypes.create_unicode_buffer(64); fl.u32.GetClassNameW(hwnd, cls, 64)
        tpp = st.unpack('<d', a.r(f + v.ED_TICKSPERPX, 8))[0]
        g = a.q(f + grid)
        L, T, W_, H = st.unpack('<iiii', a.r(g + 0x90, 16))
        vs = a.d(g + v.GRID_VSCROLL)
        ok = cls.value == 'TEventEditForm' and 0.001 < tpp < 1000 and 50 < W_ < 10000 and 20 < H < 10000 and 0 <= vs < 1000000
        results.append((nm + ' layout', ok))
        print('%-4s %-34s hwnd %s %s, ticks/px %.3f, grid %dx%d at %d,%d, vscroll %d' %
              ('PASS' if ok else 'FAIL', nm + ' layout offsets', hex(hwnd), cls.value, tpp, W_, H, L, T, vs), flush=True)


STEPS = dict(params=t_params, mixer=t_mixer, channels=t_channels, patterns=t_patterns, song=t_song, patmarks=t_patmarks,
             notedel=t_notedel, layout=t_layout)

if __name__ == '__main__':
    for pid in (A, B): fl.inject(pid)
    a, b = fl.Probe(A), fl.Probe(B)
    v = mk.V(a)
    print('FL build', v.build, '| A', A, 'B', B, flush=True)
    for name in (sys.argv[1:] or list(STEPS)):
        try: STEPS[name](a, b, v)
        except Exception as e:
            results.append((name, False)); print('FAIL %-34s exception: %s' % (name, e), flush=True)
    n = sum(ok for _, ok in results)
    print('\n%d / %d passed' % (n, len(results)))
