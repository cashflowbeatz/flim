# flim client

`flim.exe` (launcher) + `flim_engine.dll` (engine loaded into FL Studio). See the
[main README](../README.md) for what it does and how to use it.

## Build
Requires Visual Studio 2022 or newer with the C++ workload (Build Tools or Community).
```bat
build.bat
```
Output: `out\flim.exe`, `out\flim_engine.dll`. Keep the two files in the same folder.

For the experimental **FL 26.1.6.5639** port, use an ordinary Command Prompt:

```bat
set FLIM_EXPERIMENTAL_FL26=1
build.bat
```

Output: `out-experimental\flim.exe` and `out-experimental\flim_engine.dll`.
Both participants should use the same FL build. See [validation and limits](dev/compatibility/README.md).
Setting the environment variable only at runtime does not enable FL 26 in a normal compiled client.
The build finds the latest installed x64 C++ toolchain using `vswhere`, or uses an existing x64
developer prompt. To select a specific install, set `VCVARS` to its `vcvars64.bat` path.

The setup dialog discovers FL installs under both Program Files locations, preferring a supported
build. It also recognizes newer installs such as FL Studio 2026, but that does not enable flim
support: an exact, verified address-table entry is required. When an unsupported build is selected,
the launcher offers to start ordinary FL without loading the flim engine.

Offline checks: run `dev\harness\build.bat`, then `t_ws_buffer.exe`, `t_launcher.exe`,
`t_injection.exe`, `t_patops.exe`, `t_flp.exe`, `t_samples.exe` and `t_plugin_apply.exe` from that folder.
These check bounded WebSocket buffer growth,
read-only install discovery, launcher failure handling with mocked process APIs, and VMT hook selection
in synthetic memory without starting FL. The existing
`t_samples.exe` checks sample-path rewriting and creates small test aliases. `t_flp.exe` validates
legacy and escaped event parsing; `t_plugin_apply.exe` checks remote-load isolation and stale
parameter reversion handling using mocked FL functions.

## Files
| File | What |
|---|---|
| `launcher/flim.c`, `flim.rc`, `flim.ico` | the launcher: first-run setup window (FL folder + name, validated), starts FL suspended, loads the engine, resumes |
| `engine/flim_engine.c`, `flim_engine.rc` | the engine: polls FL's state, sends and applies edits on FL's main thread, locks, sample transfer (WinHTTP, SHA-256 via BCrypt), WinHTTP WebSocket client, Connect / Host buttons and dialogs |
| `common/flim_config.h` | `%APPDATA%\flim\flim.ini` read/write, name sanitizing, DPAPI-encrypted password |
| `common/flim_res.h` | dialog resource IDs |
| `restart_test.ps1` | dev loop: close only previously recorded test-process identities, rebuild, and start leader "A" and member "B". Requires `-Visible` or `-Minimized`; `-Experimental` uses `out-experimental`. Other FL instances and the shared log are preserved. |
| `gen_icon.py` | regenerates `launcher/flim.ico` (needs Pillow) |
| `dev/` | reverse-engineering and test tools (not built or shipped), see [dev/README.md](dev/README.md) |

## FL build dependency
The engine calls FL's internal functions and reads its data at addresses and object offsets that move
between FL builds. They live in one table, `k_flBuilds` at the top of `engine/flim_engine.c`, one row
per supported build (25.2.3.5171, the reference found by hand, and 25.1.6.4997); the engine picks the
row matching the loaded `FLEngine_x64.dll` and stays off without one. The launcher's list is
`FLIM_FL_BUILDS` in `common/flim_config.h`. Adding a build: see "Adding an FL build" in
[dev/README.md](dev/README.md).
