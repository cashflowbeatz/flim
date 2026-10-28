# The engine's per-FL-build table (k_flBuilds in engine/flim_engine.c) for the dev tools, plus the
# few addresses only the tools use. build(pid) = the row for an FL started by restart_test.ps1, from
# the engine log ("FL build 25.x.y.z: using its address table").
import os, re

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.join(HERE, '..', 'engine', 'flim_engine.c')
LOG = os.path.join(os.environ.get('TEMP', HERE), 'flim_engine.log')

# not used by the engine (mapped with sigscan.py like the engine's own)
TOOLS = {
    '24.2.2.4597': dict(FORMADDMARK=0xced220, PL_MARKERS=0xd10),
    '25.2.3.5171': dict(FORMADDMARK=0xd4c810,      # editor form: add marker (form, pos, name, action, num, den)
                        PL_MARKERS=0xd50),         # playlist form: marker list being shown
    '25.1.6.4997': dict(FORMADDMARK=0xde9050, PL_MARKERS=0xd48),
    '25.2.5.5319': dict(FORMADDMARK=0xd523c0, PL_MARKERS=0xd5c),
    # Static candidates for the compile-gated experimental engine; not a support declaration.
    '26.1.6.5639': dict(FORMADDMARK=0xe545b0, PL_MARKERS=0xd84),
    '26.1.7.5654': dict(FORMADDMARK=0xe543b0, PL_MARKERS=0xd84),
}


def table():
    """{build: {NAME: value}} from k_flBuilds"""
    src = open(ENGINE, encoding='utf-8').read()
    body = src[src.index('static const FlBuild k_flBuilds[] = {'):]
    body = body[:body.index('\n};')]
    out = {}
    for row in re.findall(r'\{\s*//[^\n]*\n(.*?)\n    \},', body, re.S):
        build = re.search(r'\.build = L"([^"]+)"', row).group(1)
        vals = {k: int(v, 0) for k, v in re.findall(r'\.([A-Za-z_0-9]+)\s*=\s*(0x[0-9a-fA-F]+|\d+)', row)}
        vals.update(TOOLS.get(build, {}))
        out[build] = vals
    return out


def build_of(pid, log=None):
    if log is None:
        directory = os.environ.get('FLIM_LOG_DIR')
        log = os.path.join(directory, 'native-%d.log' % pid) if directory else LOG
    t = open(log, encoding='utf-8', errors='replace').read()
    m = re.findall(r'\[%d\] FL build ([0-9.]+): using' % pid, t)
    if not m: raise LookupError('no "FL build" line for pid %d in %s' % (pid, log))
    return m[-1]


class Build(dict):
    """attribute access: V.REC, V.CH_GRID ..."""
    __getattr__ = dict.__getitem__


def build(pid):
    b = build_of(pid)
    return Build(table()[b], build=b)


def local_mix_id(p, canonical):
    """Translate only the mixer ID range; mute pseudo IDs and REC_NONE are unchanged."""
    if build(p.pid).build == '24.2.2.4597' and 0x70000000 <= canonical < 0x90000000:
        return canonical - 0x50000000
    return canonical


def clip_stride(p):
    return 0x3c if build(p.pid).build == '24.2.2.4597' else 0x50


def playlist_refresh(p):
    """FL24 inlines this form operation; its profile PLREFRESH takes a grid."""
    v = build(p.pid)
    form = p.q(p.g(v.PLFORM))
    if not form:
        raise ValueError('Playlist form is not ready')
    if v.build != '24.2.2.4597':
        return p.call(v.PLREFRESH, form)
    piano = p.q(form + v.PR_GRID)
    if piano:
        p.call(v.PLREFRESH, piano)
        p.call(0x118a8c0)
    playlist = p.q(form + v.PL_GRID)
    if playlist:
        p.call(v.PLREFRESH, playlist)
