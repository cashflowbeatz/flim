# client/dev: reverse-engineering and test tools

Not part of the flim build (`build.bat` ignores this folder) and never shipped. These are what the
engine's `*_VA` addresses were found and checked with, for FL Studio 25.2.3.5171.

Python 3 with `python -m pip install --user capstone pefile numpy`.

| File | What |
|---|---|
| `flre.py` | static analysis of `FLEngine_x64.dll`: disassembly (`dis`, `near`), resolving the protector's call stubs (`resolve`), callers of a function (`callers`, `show_callers`), rip-relative refs to a global (`xrefs`, `show_xrefs`), Delphi published method names -> addresses (`meth('EEPatInsMenuClick')`, `find_name`), strings (`strs`). Run as `python flre.py "dis(0x11180c0)"`. |
| `probe/probe.c` | **dev-only** DLL injected into a running FL. Answers commands on `\\.\pipe\flimprobe_<pid>`: read/write memory, read a Delphi string, make a Delphi string, call a function on FL's main thread. Any local program can use that pipe while FL runs, so only inject it into an FL you're testing with. Build: `probe\build.bat`. |
| `fl.py` | client for the probe (`inject(pid)`, `Probe(pid)`: `r`, `q`, `d`, `g` (deref a global VA), `call(va, ...)`, `mkstr`, `ustr`), plus window helpers that never move the real mouse or keyboard (`windows`, `close_dialogs`, `confirm`, `poke`) and screenshots (`shot_fl`). |
| `mk.py` | helpers on top of `fl.py` for the two-FL test session (`A`, `B` = leader/member pids from the engine log): markers, time signature, channels, patterns, pattern ops. |
| `flp.py`, `tlv.py`, `paths.py` | parse FLP / .fst events, Image-Line's tag/length/value plugin data, and list sample paths inside plugin states. |
| `survey.py` | which factory presets store sample paths in their plugin data, and in what layout. |
| `harness/t_samples.c` | offline test of the plugin-data sample path code (includes the engine source). `harness\build.bat`, then `t_samples.exe` -> `OK`. |

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
