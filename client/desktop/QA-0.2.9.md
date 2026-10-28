# Room startup fix, 0.2.9

The user's four 0.2.8 launch logs stopped at a null main-thread hook on the early `TApplication`
window. An instrumented normal FL26 launch reproduced `get=126 call=126` before any connection
attempt. This check used the installed FL profile, without copying registry/license state or
running the previously rejected isolation helper.

The fix passes NULL as the module for callbacks already loaded within this process, waits for
`TFruityLoopsMainForm`, validates its process/thread and requires both message hooks. Partial
installation is cleaned up, and missing-window/hook failures identify the startup stage in logs.

Live normal-profile host checks on the unchanged deployed Cloudflare relay:

| Installed engine | Hooks ready | Welcome accepted | Own host in room list | Startup to confirmation |
| --- | --- | --- | --- | --- |
| 24.2.2.4597 | yes | yes | yes | 8,579 ms |
| 25.2.5.5319 | yes | yes | yes | 9,683 ms |
| 26.1.7.5654 | yes | yes | yes | 6,381 ms |

FL26 member admission also passed with the native DLL taken from the final 0.2.9 package:
hooks ready, native welcome and its own member entry confirmed in 8,966 ms. The host was a
protocol fixture that deliberately sent no project, so this does not test project loading.

These are measured single-run startup checks, not latency guarantees. Each test closed only its
own FL process normally and left the room. No project or preset was sent. The new opt-in
`test/native-room-startup.cjs` repeats this path through the real desktop bridge and live relay;
its member option uses a protocol fixture host without sending a project snapshot.

Regression checks: 26 native harness executables passed, including the new main-form discovery,
same-process module argument, ownership recheck, both partial failures and bounded window wait.
The four HTTP transfer checks and all 67 desktop tests passed. No renderer, preload or desktop
bridge behavior changed; the existing visual direction and design gate are retained.

Final package and portable executable checks passed: native hashes match the tested payload,
ASAR source matches the build, installed FL24/25/26 are discovered, icons and local diagnostics
are correct, and startup reports 0.2.9 without errors. No test FL or desktop process remained.
Exact binary/source hashes and run summaries are in
[the compatibility record](../dev/compatibility/desktop-0.2.9-2026-10-27.json).

The source Worker remains undeployed. The live relay negotiated locked note snapshots, as
expected. These checks establish room connectivity for the exact installed engines, not full
project synchronization, recorded/unrecorded MIDI, every feature or every FL24/25/26 patch build.
The earlier live feature limitations remain in force. GitHub was not changed.
