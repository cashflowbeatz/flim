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


def mixrec(p, t):
    v = mk.V(p)
    return p.g(v.MIXARR) + t * v.MIX_STRIDE
def arrangement(p):
    v = mk.V(p)
    return p.q(p.g(v.ARRS) + 8 * p.d(p.a(v.ARRCUR)))


def t_params(a, b, v):
    REC = v.REC
    vb = mk.V(b)
    a.call(REC, 0, 6400, 1 | 16)                                   # channel 1 volume
    check('channel volume (REC)', lambda: b.call(vb.REC, 0, 0, 2), 6400)
    mid = 0x70000000 + (1 << 22) + 0x1fc0                          # mixer insert 1 volume
    a.call(REC, mid, 7000, 1 | 16)
    check('mixer volume (REC)', lambda: b.call(vb.REC, mid, 0, 2), 7000)


def t_mixer(a, b, v):
    a.call(v.MUTE, 2, 0)
    check('mixer mute', lambda: b.r(mixrec(b, 2) + 0x18, 1)[0], 0)
    a.call(v.MIXNAME, 3, a.mkstr('Drums'), 1)
    check('mixer track name', lambda: b.ustr(b.q(mixrec(b, 3) + 0x0c)), 'Drums')
    a.call(v.MIXCOLOR, mixrec(a, 4), 0x3366ff)
    check('mixer track color', lambda: b.d(mixrec(b, 4) + 4) & 0xffffff, 0x3366ff)
    mix = a.q(a.g(v.MIXOBJ))
    a.call(v.SETROUTE, mix, 5, 6, 1, 1); a.call(v.ROUTEUPD, mix)
    check('mixer routing', lambda: b.r(mixrec(b, 5) + mk.V(b).MIX_ROUTE + 6 * 8 + 4, 1)[0], 1)


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
    t, _ = bg_call(a, 'call %x %d 2' % (a.q(a.q(a.q(a.q(a.g(v.FLPROJ)) + 0x10)) + v.PATOP_INSERT_SLOT), a.q(a.q(a.g(v.FLPROJ)) + 0x10)))
    close_dialogs_until(t, [A])
    check('pattern op: insert', lambda: mk.pats(b, 4), [(1, 'Verse', 1), (2, '', 0), (3, 'Chorus', 1), (4, 'Bridge', 1)], 15)
    # Delete pattern 2 (A: FL asks "are you sure", confirmed here; B: message box answered by flim's hook)
    arr = a.alloc(8); a.w(arr, struct.pack('<i', 2))
    proj = a.q(a.q(a.g(v.FLPROJ)) + 0x10)
    t, _ = bg_call(a, 'call %x %d %d 0' % (a.q(a.q(proj) + v.PATOP_DELETE_SLOT), proj, arr))
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


FL_DIRS = {'25.2.3.5171': r'C:\Program Files (x86)\Image-Line\FL Studio 2025',
           '25.1.6.4997': r'C:\Program Files (x86)\Image-Line\FL4997',
           '25.2.5.5319': r'C:\Program Files\Image-Line\FL Studio 2025',
           '26.1.6.5639': r'C:\Program Files\Image-Line\FL Studio 2026',
           '26.1.7.5654': r'C:\Program Files\Image-Line\FL Studio 2026'}
MASK_FX = 48


def plugin_state(p, v, obj, tag):
    """the slot's plugin state as FL saves it (a preset file), or None"""
    import os, tempfile
    path = os.path.join(tempfile.gettempdir(), 'flim_ft_%s_%d.fst' % (tag, p.pid))
    if os.path.exists(path): os.remove(path)
    r = p.call(v.SAVEPRESET, p.mkstr(path), MASK_FX, obj, 1) & 0xffffffff
    if r != 0 or not os.path.exists(path): return None
    return open(path, 'rb').read()


def shared_effect_state(data, plugin_payload_only=False):
    """Keep sound and wrapper state; editor visibility belongs to each participant."""
    from flp import events
    if not data: raise ValueError('FL did not save the effect preset')
    out = []
    for event, value in events(data):
        if plugin_payload_only and event not in (201, 212, 213):
            continue
        if event == 212:
            if len(value) < 18: raise ValueError('Truncated effect wrapper')
            value = bytearray(value)
            value[16] &= ~1  # WrapperEvent flags bit 0: editor visible (PyFLP plugin.py).
            value = bytes(value)
        out.append((event, value))
    if not any(e == 212 for e, _ in out) or not any(e == 213 for e, _ in out):
        raise ValueError('Effect wrapper/plugin data missing')
    if plugin_payload_only and not any(e == 201 for e, _ in out):
        raise ValueError('Effect plugin identity missing')
    return out


def t_fx(a, b, v):
    """a mixer effect: replacing the plugin in a slot sends its whole state (plug.state)"""
    vb = mk.V(b)
    slot = lambda p, vv: p.q(mixrec(p, 1) + vv.MIX_SLOTS_OFF)     # insert 1, slot 1
    preset = FL_DIRS[v.build] + r'\Data\Patches\Plugin presets\Effects\Fruity Delay 3\Chorus.fst'
    a.call(v.LOADPRESET, a.mkstr(preset), MASK_FX, slot(a, v), 0)
    check('effect added in A', lambda: a.call(v.HASPLUGIN, slot(a, v)) & 0xff, 1, 3)
    check('effect plugin in B', lambda: b.call(vb.HASPLUGIN, slot(b, vb)) & 0xff, 1, 15)
    sa = plugin_state(a, v, slot(a, v), 'a')
    mixed = v.build != vb.build
    check('effect identity/wrapper/sound' if mixed else 'effect state (local visibility excluded)',
          lambda: shared_effect_state(plugin_state(b, vb, slot(b, vb), 'b'), mixed),
          shared_effect_state(sa, mixed), 10)
    a.call(v.LOADPRESET, a.mkstr(FL_DIRS[v.build] + r'\Data\System\(delete) effect.fst'), MASK_FX, slot(a, v), 0)
    check('effect removed in B', lambda: b.call(vb.HASPLUGIN, slot(b, vb)) & 0xff, 0, 15)


def t_reorder(a, b, v):
    """move a channel up the way FL's Alt+Up does (selection + ReorderChannels mode 4)"""
    for _ in range(max(0, 3 - len(mk.chans(a)))):
        a.call(v.ADDCHAN, 0xFFFFFFFFFFFFFFFF, 0, 1)
    items = a.q(a.q(a.g(v.CHANLIST)) + 8)
    for i in range(3):
        ch = a.q(items + 8 * i)
        a.cmd('call %x %d %d' % (a.q(a.q(ch) + 0x70), ch, a.mkstr('flim reorder %d' % i)))
    before = [n for _, n in mk.chans(a)]
    if not check('reorder fixture replicated', lambda: [n for _, n in mk.chans(b)], before, 15): return
    items = a.q(a.q(a.g(v.CHANLIST)) + 8)
    objs = [a.q(items + 8 * i) for i in range(len(before))]
    sel = [a.r(o + v.CH_SELECTED, 1) for o in objs]
    for i, o in enumerate(objs): a.w(o + v.CH_SELECTED, bytes([1 if i == 2 else 0]))
    a.call(v.MOVECHAN, a.q(a.g(v.MOVEOBJ)), 4, a.d(a.g(v.MOVEARG)), 1)
    for o, s in zip(objs, sel): a.w(o + v.CH_SELECTED, s)
    want = before[:1] + [before[2], before[1]] + before[3:]
    check('channel reorder in A', lambda: [n for _, n in mk.chans(a)], want, 3)
    check('channel reorder in B', lambda: [n for _, n in mk.chans(b)], want, 15)


def clip_list(p):
    ca = p.q(arrangement(p) + 0x14)
    n, data = p.d(ca + 0x14), p.q(ca + 8)
    return [p.r(data + i * 0x50, 0x50) for i in range(n)]


def t_clips(a, b, v):
    """a pattern clip added to the playlist (clips.set)"""
    rec = bytearray(0x50)
    # FLP playlist item: position, pattern base 0x5000, item = base + pattern, length, track (499 - index),
    # group, -, flags, -, start / end offset (-1 = whole pattern)
    struct.pack_into('<IHHIHHHHIff', rec, 0, 192, 0x5000, 0x5000 + 1, 384, 499, 0, 0, 0x40, 0, -1.0, -1.0)
    buf = a.alloc(0x50); a.w(buf, bytes(rec))
    ca = a.q(arrangement(a) + 0x14)
    a.cmd('call %x %d %d' % (a.q(a.q(ca) + 8), ca, buf))
    a.call(v.PLREFRESH, a.q(a.g(v.PLFORM)))
    check('clip added in A', lambda: len(clip_list(a)), 1, 3)
    check('clip in B (position, pattern, length, track)', lambda: [c[:14].hex() for c in clip_list(b)], [c[:14].hex() for c in clip_list(a)], 15)


def make_sample_fixture(v):
    """Use FL's installed sampler template and a fresh, tiny generated WAV."""
    import os, random, tempfile, uuid, wave
    from flp import events, data_bounds
    template = FL_DIRS[v.build] + r'\Data\Patches\Channel presets\Sampler\Default.fst'
    data = open(template, 'rb').read()
    version = next(value.rstrip(b'\0').decode('ascii') for event, value in events(data) if event == 199)
    stem = os.path.join(tempfile.gettempdir(), 'flim_sample_test_' + uuid.uuid4().hex)
    wav, fst = stem + '.wav', stem + '.fst'
    rng = random.Random(stem)
    with wave.open(wav, 'wb') as out:
        out.setnchannels(1); out.setsampwidth(2); out.setframerate(44100)
        out.writeframes(b''.join(struct.pack('<h', rng.randrange(-400, 401)) for _ in range(4410)))
    path = (wav + '\0').encode('ascii' if int(version.split('.')[0]) < 11 else 'utf-16-le')
    length, n = bytearray(), len(path)
    while n >= 128: length.append((n & 127) | 128); n >>= 7
    length.append(n)
    start, end = data_bounds(data)
    body = data[start:end] + bytes([196]) + bytes(length) + path
    with open(fst, 'wb') as out: out.write(data[:start - 4] + struct.pack('<I', len(body)) + body + data[end:])
    return fst


def t_sample(a, b, v, fst=None):
    """A loads a channel preset that uses a sample B doesn't have: B downloads it (plug.state + samples)"""
    import os, re, tempfile, hashlib
    supplied = fst or os.environ.get('FLIM_FT_FST')
    fst = supplied or make_sample_fixture(v)
    log = os.path.join(os.environ['TEMP'], 'flim_engine.log')
    mark = os.path.getsize(log)
    def tail():
        with open(log, encoding='utf-8', errors='replace') as f: f.seek(mark); return f.read()
    # a new channel with the preset in it: chan.add carries the channel's whole state (a preset over an
    # existing channel only counts with real user input, or when it brings a new plugin)
    lst = a.q(a.g(v.CHANLIST)); last = a.d(lst + 0x10)
    a.call(v.ADDCHAN, 0xFFFFFFFFFFFFFFFF, 0, 1)
    ch = a.q(a.q(lst + 8) + 8 * last)
    a.call(v.LOADPRESET, a.mkstr(fst), 32, ch, 0)
    pat = r'\[%d\] SAMPLES op lists (\d+) samples: (\d+) downloaded, (\d+) missing' % B
    check('sample downloaded by B', lambda: [m for m in re.findall(pat, tail()) if m[1] != '0'][-1:],
          [('1', '1', '0')], 30)
    vb = mk.V(b)
    chb = b.q(b.q(b.q(b.g(vb.CHANLIST)) + 8) + 8 * last)
    path = os.path.join(tempfile.gettempdir(), 'flim_ft_ch_%d.fst' % b.pid)
    def b_uses_download():
        if os.path.exists(path): os.remove(path)
        b.call(vb.SAVEPRESET, b.mkstr(path), 32, chb, 1)
        if not os.path.exists(path): return False
        data = open(path, 'rb').read()
        if supplied:
            return any(p.encode('utf-16-le') in data for p in
                       (r'Documents\flim\samples', r'%ILSharedData%flim\samples'))
        from flp import events
        import winreg
        source = os.path.splitext(fst)[0] + '.wav'
        expected = hashlib.sha256(open(source, 'rb').read()).digest()
        cache = os.path.join(os.environ['USERPROFILE'], 'Documents', 'flim', 'samples')
        for event, value in events(data):
            if event != 196: continue
            saved = value.decode('utf-16-le').rstrip('\0')
            if saved.lower().startswith('%ilshareddata%'):
                try:
                    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\Image-Line\Shared\Paths') as key:
                        shared = winreg.QueryValueEx(key, 'Shared data')[0]
                except OSError: shared = os.path.join(os.environ['USERPROFILE'], 'Documents', 'Image-Line')
                saved = os.path.join(shared, saved[len('%ILSharedData%'):].lstrip('\\/'))
            if os.path.commonpath([os.path.abspath(saved), cache]).lower() != cache.lower(): continue
            if os.path.isfile(saved) and hashlib.sha256(open(saved, 'rb').read()).digest() == expected:
                return True
        return False
    check("B's channel uses the downloaded file", b_uses_download, True, 15)
    a.call(v.DELCHAN, last, 0, 3)


def t_returnpath(a, b, v):
    """Member-to-host edits must use each process's own build-specific function addresses."""
    vb = mk.V(b)
    b.call(vb.REC, 0, 6100, 1 | 16)
    check('member channel volume reaches host', lambda: a.call(v.REC, 0, 0, 2), 6100)
    b.call(vb.PATNAME, 4, b.mkstr('Member pattern'))
    check('member pattern name reaches host', lambda: mk.pats(a, 4)[3][1], 'Member pattern')
    arr = b.call(vb.NOTEARR, 4, 1)
    rec = b.alloc(24)
    b.w(rec, struct.pack('<IHHIHHBBBBBBBB', 0, 0x4000, 0, 192, 67, 0, 120, 0, 64, 0, 64, 100, 128, 128))
    b.cmd('call %x %d %d' % (b.q(b.q(arr) + 8), arr, rec))
    check('member note reaches host', lambda: mk.pats(a, 4)[3][2], 1)


STEPS = dict(params=t_params, mixer=t_mixer, channels=t_channels, patterns=t_patterns, song=t_song, patmarks=t_patmarks,
             notedel=t_notedel, layout=t_layout, fx=t_fx, reorder=t_reorder, clips=t_clips, sample=t_sample, returnpath=t_returnpath)

if __name__ == '__main__':
    for pid in (A, B): fl.inject(pid)
    for pid, h, c, t in fl.windows([A, B]):          # flim's own boxes ("you're hosting", notes)
        if c == '#32770': fl.u32.PostMessageW(h, 0x111, 1, 0)
    a, b = fl.Probe(A), fl.Probe(B)
    v = mk.V(a)
    print('FL build', v.build, '| A', A, 'B', B, flush=True)
    for name in (sys.argv[1:] or list(STEPS)):
        try: STEPS[name](a, b, v)
        except Exception as e:
            results.append((name, False)); print('FAIL %-34s exception: %s' % (name, e), flush=True)
    n = sum(ok for _, ok in results)
    print('\n%d / %d passed' % (n, len(results)))
    raise SystemExit(0 if n == len(results) else 1)
