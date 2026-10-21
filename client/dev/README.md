# client/dev: reverse-engineering and test tools

Not part of the flim build (`build.bat` ignores this folder) and never shipped. These are what the
engine's FL addresses were found and checked with (25.2.3.5171 by hand), and how other FL builds
are mapped and tested.

Python 3 with `python -m pip install --user capstone pefile numpy`.

| File | What |
|---|---|
| `flre.py` | static analysis of `FLEngine_x64.dll`: disassembly (`dis`, `near`), resolving the protector's call stubs (`resolve`), callers of a function (`callers`, `show_callers`), rip-relative refs to a global (`xrefs`, `show_xrefs`), Delphi published method names -> addresses (`meth('EEPatInsMenuClick')`, `find_name`), strings (`strs`). Run as `python flre.py "dis(0x11180c0)"`. |
| `probe/probe.c` | **dev-only** DLL injected into a running FL. Answers commands on `\\.\pipe\flimprobe_<pid>`: read/write memory, read a Delphi string, make a Delphi string, call a function on FL's main thread. Any local program can use that pipe while FL runs, so only inject it into an FL you're testing with. Build: `probe\build.bat`. |
| `fl.py` | client for the probe (`inject(pid)`, `Probe(pid)`: `r`, `q`, `d`, `g` (deref a global VA), `call(va, ...)`, `mkstr`, `ustr`), plus window helpers that never move the real mouse or keyboard (`windows`, `close_dialogs`, `confirm`, `poke`) and screenshots (`shot_fl`). |
| `mk.py` | helpers on top of `fl.py` for the two-FL test session (`A`, `B` = leader/member pids from the engine log): markers, time signature, channels, patterns, pattern ops, pattern markers (`pmarks`, `pr_add_marker`), opening FL windows without input (`show(p, 3)` = piano roll). |
| `flp.py`, `tlv.py`, `paths.py` | parse FLP / .fst events, Image-Line's tag/length/value plugin data, and list sample paths inside plugin states. |
| `survey.py` | which factory presets store sample paths in their plugin data, and in what layout. |
| `sigscan.py` | maps every address in the engine's table from one FL build to another, statically: function signatures (call targets and RIP-relative operands wildcarded; level 2 also offsets), call sites, function order between matched neighbours (Delphi keeps a unit's functions in order), globals by matching the code that references them (majority vote), plus a similarity score per function. `--offsets` pairs ~25000 functions by offset-blind signatures and reports what each of flim's struct offsets / strides became. Calls landing in VMProtect's call stubs are ignored. |
| `flbuild.py` | the engine's `k_flBuilds` table for the tools (plus the few addresses only the tools use); `build(pid)` = the row of an FL started by `restart_test.ps1`. |
| `feature_test.py` | two-FL feature test, any supported build: changes things in A through FL's own functions and waits for B to match. Steps: `params mixer channels patterns song patmarks notedel layout fx reorder clips sample` (knobs, mixer, channel add / rename / delete / reorder, pattern names + notes + insert / delete incl. the message box hook, project + pattern time signatures, markers, playlist tracks, note delete, editor layout offsets, a mixer effect's state, playlist clips, sample transfer). `python feature_test.py [step ...]`; `sample` needs `FLIM_FT_FST=<channel .fst saved by that FL build, using a sample of yours>` and a B that has to download it (`restart_test.ps1 -NoLocalB`, empty `Documents\flim\samples`). Known on both builds: a mixer effect's saved state differs in one flag byte (A loaded it through the preset loader). |
| `harness/t_samples.c` | offline test of the plugin-data sample path code (includes the engine source). `harness\build.bat`, then `t_samples.exe` -> `OK`. |

## Adding an FL build
1. `py -3.11 sigscan.py <reference FLEngine_x64.dll> <new FLEngine_x64.dll> ..\engine\flim_engine.c --json out.json`.
   Every address should come out `confirmed` / `unique` (functions: similarity near 1.00; data: one
   vote winner). Look at the rest by hand (the 25.1 message box had a different shape: no wrapper).
2. `py -3.11 sigscan.py --offsets <ref> <new> ..\engine\flim_engine.c`: offsets marked `MOVED?` with a
   clear majority moved; check small / ambiguous ones inside the mapped functions themselves. Record
   sizes show up as changed multipliers (pattern record 0xC0 -> 0xA4 in 25.1).
3. Add a row to `k_flBuilds` (engine) and the build to `FLIM_FL_BUILDS` (launcher), rebuild.
4. `restart_test.ps1 -Minimized -FlDir "<that FL's folder>"`, then `python feature_test.py`: all PASS,
   and the same run on the reference build for comparison. `-FlDirB` starts B in another FL (mixed
   builds: the member must leave with "Everyone in a session needs the same FL Studio build").
- Don't load projects from a newer FL with the low-level load (TFLProject vmt +0x38) in an older FL:
  FL's "created using a more recent version" box (25.1: not the hooked message box) then crashes it
  after "Yes" (double free, FLEngine+0xfaaf), with or without flim. FL's own File > Open survives.

## Typical session
1. Start the server (`cd server && npm start`), then `client\restart_test.ps1 -Minimized` (add
   `-NoLocalB` to make B download samples even though both FLs are on one PC).
2. `python -c "import fl, mk; fl.inject(mk.A); fl.inject(mk.B)"`
3. Change something in A through FL's own function (e.g. `mk.patop(a, 0xf8, 2)` = Insert pattern) and
   check B's state and `%TEMP%\flim_engine.log`.

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
