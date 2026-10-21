# flim client

`flim.exe` (launcher) + `flim_engine.dll` (engine loaded into FL Studio). See the
[main README](../README.md) for what it does and how to use it.

## Build
Requires Visual Studio 2022 Build Tools with the C++ workload.
```bat
build.bat
```
Output: `out\flim.exe`, `out\flim_engine.dll`. Keep the two files in the same folder.
If the Build Tools are installed somewhere else, change `VCVARS` at the top of `build.bat`.

## Files
| File | What |
|---|---|
| `launcher/flim.c`, `flim.rc`, `flim.ico` | the launcher: first-run setup window (FL folder + name, validated), starts FL suspended, loads the engine, resumes |
| `engine/flim_engine.c`, `flim_engine.rc` | the engine: polls FL's state, sends and applies edits on FL's main thread, locks, sample transfer (WinHTTP, SHA-256 via BCrypt), WinHTTP WebSocket client, Connect / Host buttons and dialogs |
| `common/flim_config.h` | `%APPDATA%\flim\flim.ini` read/write, name sanitizing, DPAPI-encrypted password |
| `common/flim_res.h` | dialog resource IDs |
| `restart_test.ps1` | dev loop: close FL, rebuild, start leader "A" and member "B" connected to a server (`-Minimized`, `-NoLocalB`) |
| `gen_icon.py` | regenerates `launcher/flim.ico` (needs Pillow) |
| `dev/` | reverse-engineering and test tools (not built or shipped), see [dev/README.md](dev/README.md) |

## FL build dependency
The engine calls FL's internal functions and reads its data at addresses and object offsets that move
between FL builds. They live in one table, `k_flBuilds` at the top of `engine/flim_engine.c`, one row
per supported build (25.2.3.5171, the reference found by hand, and 25.1.6.4997); the engine picks the
row matching the loaded `FLEngine_x64.dll` and stays off without one. The launcher's list is
`FLIM_FL_BUILDS` in `common/flim_config.h`. Adding a build: see "Adding an FL build" in
[dev/README.md](dev/README.md).
