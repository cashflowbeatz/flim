# client/dev: reverse-engineering and test tools

Not part of the flim build (`build.bat` ignores this folder) and never shipped. These are what the
engine's FL addresses were found and checked with (25.2.3.5171 by hand), and how other FL builds
are mapped and tested.

Python 3 with `python -m pip install --user capstone pefile numpy`.

| File | What |
|---|---|
| `flre.py` | static analysis of `FLEngine_x64.dll`: disassembly (`dis`, `near`), resolving the protector's call stubs (`resolve`), callers of a function (`callers`, `show_callers`), rip-relative refs to a global (`xrefs`, `show_xrefs`), Delphi published method names -> addresses (`meth('EEPatInsMenuClick')`, `find_name`), strings (`strs`). Select a DLL with `--dll`, `FLIM_FL_DLL`, or `flre.load_target(path)`; see below. |
| `probe/probe.c` | **dev-only** DLL injected into a running FL. Answers commands on `\\.\pipe\flimprobe_<pid>`: read/write memory, read a Delphi string, make a Delphi string, call a function on FL's main thread. Any local program can use that pipe while FL runs, so only inject it into an FL you're testing with. Build: `probe\build.bat`. |
| `fl.py` | client for the probe (`inject(pid)`, `Probe(pid)`: `r`, `q`, `d`, `g` (deref a global VA), `call(va, ...)`, `mkstr`, `ustr`), plus window helpers that never move the real mouse or keyboard (`windows`, `close_dialogs`, `confirm`, `poke`) and screenshots (`shot_fl`). |
| `mk.py` | helpers on top of `fl.py` for the two-FL test session (`A`, `B` = leader/member pids from the engine log): markers, time signature, channels, patterns, pattern ops, pattern markers (`pmarks`, `pr_add_marker`), opening FL windows without input (`show(p, 3)` = piano roll). |
| `flp.py`, `tlv.py`, `paths.py` | parse FLP / .fst events, Image-Line's tag/length/value plugin data, and list sample paths inside plugin states. |
| `survey.py` | which factory presets store sample paths in their plugin data, and in what layout. |
| `sigscan.py` | reads address and layout fields from the actual `k_flBuilds` row selected by the reference DLL's fixed PE file version. Finds static candidates using function signatures, call sites, function order and global references. `--offsets` compares aligned operand values for per-build fields and fixed layout constants. Saves both DLL identities and unresolved fields in JSON; incomplete results exit nonzero. |
| `test_static_tools.py` | offline tests of table parsing, exact version selection, evidence, failure handling, small operand drift and target cache isolation. No FL installation or injection required. |
| `inspect_layout.py` | bounded, read-only observations of candidate layout fields in an explicitly selected running FL 26 process. Checks installation path, pinned DLL hash and mapped PE headers; emits numeric structure observations without reading names. Requires address and offset evidence. |
| `test_inspect_layout.py` | synthetic-memory tests of read bounds, target identity, unavailable editors, and the correct main-form pointer chain. |
| `test_process_ownership.ps1`, `test_restart_ownership.ps1` | process-identity helpers for the restart script, and fixture/mock tests that never launch or stop real FL processes. |
| `flbuild.py` | the engine's `k_flBuilds` table for the tools (plus the few addresses only the tools use); `build(pid)` = the row of an FL started by `restart_test.ps1`. |
| `feature_test.py` | Two-FL feature test using explicit test PIDs: `params mixer channels patterns song patmarks notedel layout fx reorder clips sample`. Reordering creates distinct test channels. Effect comparison excludes only the documented local editor visibility bit. The sample step creates a small synthetic WAV from the installed Sampler template and verifies downloaded content by SHA-256; use `-NoLocalB` so B must download it. `FLIM_FT_FST` optionally selects your own single-sample channel preset. Failed checks produce a nonzero exit code. |
| `harness/t_samples.c` | offline test of the plugin-data sample path code (includes the engine source). `harness\build.bat`, then `t_samples.exe` -> `OK`. |

## Adding an FL build
1. Use the exact reference DLL named by an existing `k_flBuilds` row. A different patch build is
   not interchangeable: `25.2.5.5319` cannot be used with the `25.2.3.5171` table. Run the commands
   below to collect static address candidates and layout evidence.
   Every required address should come out `confirmed` / `unique` (functions: similarity near 1.00;
   data: one vote winner). Check the rest by hand (the 25.1 message box had a different shape: no
   wrapper). A zero `MSGBOX_CORE` is only allowed for the reference's wrapper-free message-box mode.
2. Inspect `--offsets` output, including all per-build fields and fixed layout constants. A `MOVED?`
   flag is a lead: the same numeric offset can refer to different objects, so a majority is not proof
   of a field's new layout. Verify each required field in the mapped functions themselves. VMT slots
   such as `(0x38 / 8)` are reported as byte displacements (`0x38`), matching the instruction encoding.
3. Add a row to `k_flBuilds` (engine) and the build to `FLIM_FL_BUILDS` (launcher), rebuild.
4. `restart_test.ps1 -Minimized -FlDir "<that FL's folder>"`, then `python feature_test.py`: all PASS,
   and the same run on the reference build for comparison. `-FlDirB` starts B in another FL (mixed
   builds: the member must leave with "Everyone in a session needs the same FL Studio build").
- Don't load projects from a newer FL with the low-level load (TFLProject vmt +0x38) in an older FL:
  FL's "created using a more recent version" box (25.1: not the hooked message box) then crashes it
  after "Yes" (double free, FLEngine+0xfaaf), with or without flim. FL's own File > Open survives.

From the repository root, these PowerShell commands preserve the original positional source-file
argument; omitting it also works because the default resolves relative to `sigscan.py`:

```powershell
$referenceDll = 'C:\path\to\25.2.3.5171\FLEngine_x64.dll'
$targetDll = 'C:\Program Files\Image-Line\FL Studio 2026\FLEngine_x64.dll'
python client/dev/sigscan.py $referenceDll $targetDll client/engine/flim_engine.c --reference-version 25.2.3.5171 --json address-evidence.json
python client/dev/sigscan.py --offsets $referenceDll $targetDll --json offset-evidence.json
python -m unittest discover -s client/dev -p 'test_*.py' -v
```

`--reference-version` is an optional assertion; it never overrides the DLL's version. The JSON is
an evidence object with `reference`, `target`, `source`, `selected_build`, `summary`, and `results`
fields, replacing the old bare result list. Each DLL identity includes its path, fixed file version,
SHA-256, size, and PE image base. Address values retain the engine's normalized `IMG_BASE` convention.
Exit codes: **0** means every scanned item has an unambiguous static candidate, **1** means evidence
is incomplete or ambiguous, and **2** means invalid input or an output error. Failed version checks
also produce an error report when `--json` is provided. Even exit 0 leaves `runtime_validated: false`;
it does not authorize enabling an untested build.
Rows gated by `FLIM_EXPERIMENTAL_FL26` are excluded from reference selection, so candidate mappings
cannot silently serve as the baseline for another port. Other unrecognized table preprocessor
directives are rejected rather than interpreted.

To inspect a selected DLL without reading another installed version:

```powershell
python client/dev/flre.py --dll $targetDll --info
python client/dev/flre.py --dll $referenceDll "dis(0xf4e910, n=12)"
$env:FLIM_FL_DLL = $referenceDll
python client/dev/flre.py "print(find_name('EEPatInsMenuClick'))"
```

When importing the helpers, call `flre.load_target(path)` before inspection or set `FLIM_FL_DLL`.
Changing the target resets every in-memory index. Method names are no longer loaded from a shared
temporary pickle, which could carry addresses from another DLL version. `--info` prints the exact
identity; code commands print it to stderr before running.

For a running FL 26.1.6.5639 instance, set `$flPid` to the specific process ID you intend to observe:

```powershell
python client/dev/inspect_layout.py --pid $flPid --map address-evidence.json --offsets offset-evidence.json --json live-layout.json
```

This diagnostic is pinned to the analyzed FL26 DLL hash and candidate offsets. It opens the process
with query and memory-read rights, locates the DLL through Toolhelp, and compares the mapped PE header
with the hashed disk image. It reads at most 1 MiB and samples bounded numeric fields: counts, flags,
pointer shapes, routing levels, pattern length, time signature, editor geometry, and candidate HWNDs.
It does not read project, sample, or channel names. No code is injected, called, or written into FL;
the process remains running. Exit **0** means all available observations were plausible, **1** means
a check failed, and **2** means identity/input/read setup failed. Closed editors are reported as
unavailable. All reports remain `candidate_only: true`; plausible idle values and null pointers do
not establish compatibility. HWND values are read from memory without OS window validation.

## Typical session
1. Start the server (`cd server && npm start`), then `client\restart_test.ps1 -Minimized` (add
   `-NoLocalB` to make B download samples even though both FLs are on one PC).
2. `python -c "import fl, mk; fl.inject(mk.A); fl.inject(mk.B)"`
3. Change something in A through FL's own function (e.g. `mk.patop(a, 0xf8, 2)` = Insert pattern) and
   check B's state and `%TEMP%\flim_engine.log`.

`restart_test.ps1` requires exactly one of `-Visible` or `-Minimized`. Build and launcher helper
windows remain hidden; `-Minimized` minimizes only the verified child FL window after it appears.
The script records each launcher and its FL child in `client/out/restart-test-processes.json`,
including executable path and precise creation time. Subsequent runs stop only matching identities
from that manifest. Missing entries, reused PIDs, and different executable paths do not authorize a
stop. It never stops FL by process name, and it preserves the shared engine log. Filter retained logs
by the new process IDs printed after startup.

Child ownership requires the launcher's parent PID, a creation time within the startup interval,
and the expected `FL64.exe` path. Failure to establish a unique child stops the script and preserves
recorded identities for later cleanup. `-FlDir` selects the installation; otherwise the script reads
`fl_dir` from `flim.ini`. `-FlDirB` overrides B's installation. The default build uses `client/out`;
`-Experimental` sets `FLIM_EXPERIMENTAL_FL26=1` for build and launch and uses `client/out-experimental`.
Both modes share the ownership manifest. Temporary environment overrides are restored when the
script returns, including on failure.

Run ownership tests without launching or stopping FL:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File client/dev/test_restart_ownership.ps1
```

Notes on FL internals that took a while to find:
- Calls in `FLEngine_x64.dll` go through stubs (`E9 rel32` + junk byte) outside `.text`; `resolve()`
  follows them. The real function table is the PE exception directory, not the `.pdata` section.
- Menu handlers: the binary's DFM text maps a caption to its `OnClick` name, and the Delphi method
  table maps the name to the address (`meth()`).
- FL's message box is its own form (`TMsgForm`, `0xa885e0`); dialogs inside a call block the probe's
  main-thread call until they are closed (`fl.confirm` / `fl.close_dialogs`).
- Editor grids are painted by the form, not child windows: piano roll grid = `TEventPanel_PR` at form+0xc40,
  playlist = `TEventPanel_AU` at form+0xcf8; bounds at +0x90, ticks/pixel = form+0xaec (double), scroll
  form+0xafc / grid+0x3fc. The engine's "where everyone is pointing" section has the full mapping.
  `FLIM_CURSOR_NOCOVER=1` shows other people's arrows even when FL is behind other windows (tests).
- Screenshots: capture FL's window with `PrintWindow`, never the screen (other apps may be on top).
