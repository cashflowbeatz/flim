# FL 24–26 compatibility validation

## Desktop 0.2.25 and 0.2.24: 2026-10-28

[0.2.25 manifest](desktop-0.2.25-2026-10-28.json) records the direct-file implementation,
complete executable/native hashes, 193 desktop tests, 32 native harnesses, 195 UI checks,
76 local Worker tests and real Electron WebRTC bytes through local and deployed signaling.
The direct-byte tests use two launch sessions on one computer. Remote NAT, physical FL/MIDI,
new FL patches and the earlier FL24 recovery-save stall are not resolved by those fixtures.
[0.2.25 QA](../../desktop/QA-0.2.25.md).

[0.2.24 manifest](desktop-0.2.24-2026-10-28.json) and [34-fix report](../../desktop/QA-0.2.24.md)
retain the preceding local validation and its original publication status. Both builds are
published together in this follow-up. Reviewed FL profiles remain unchanged.

## Desktop 0.2.23: 2026-10-28

[The manifest](desktop-0.2.23-2026-10-28.json) identifies the rebuilt complete portable app and
native engine. [39 new fixes and QA](../../desktop/QA-0.2.23.md) concentrate on FL editing,
mixed-build routing, CRDT allocation, plugin provenance and validation before native mutation.
31 native harnesses pass, including nine synthetic host/member combinations. Desktop/worker
suites, real Electron checks, sample/transfer loopbacks and portable/package startup pass.
The same three reviewed builds remain; no new profile, live physical MIDI/two-computer test,
GitHub commit/push or Cloudflare deployment occurred.

## Desktop 0.2.22: 2026-10-28

[The manifest](desktop-0.2.22-2026-10-28.json) records the complete local executable, rebuilt
native status writer and [35-item QA report](../../desktop/QA-0.2.22.md). Automated coverage
includes 177 backend tests, 127 room/UI checks, 68 Files/UI checks, 30 native harnesses,
loopback sample transfers and actual Electron folder/zoom/recovery tests. Reviewed build
profiles remain 24.2.2.4597, 25.2.5.5319 and 26.1.7.5654. Nine mocked mixed-build preflights
test desktop decisions; they do not establish a fresh two-computer or physical MIDI result.
No new universal-patch claim, commit, push or deployment.

## Desktop 0.2.21: 2026-10-28

[The manifest](desktop-0.2.21-2026-10-28.json) binds the complete local executable, rebuilt native
sample engine and [40-item QA report](../../desktop/QA-0.2.21.md). It records 161 backend tests,
124 room/UI checks, 55 Files/UI checks, 29 native harnesses, real Electron folder/zoom/recovery
tests, loopback transfer failures and unpacked/portable startup. The received-files folder is
selected locally before a room and stays bound to each launched FL process. Missing/locked/full
storage retains pending edits without disconnecting the room. Reviewed build profiles remain
24.2.2.4597, 25.2.5.5319 and 26.1.7.5654. Nine mocked mixed-build preflights are not a new live
MIDI/two-computer result, and every FL patch is not claimed. No commit, push or deployment.

## Desktop 0.2.15: 2026-10-27

The tested FL24.2.2.4597 engine is supported without experimental classification or opt-in.
Exact fingerprint and project-preservation checks remain. [The QA record](../../desktop/QA-0.2.15.md)
also covers the supplied card/dropdown styling and replacement of dark field outlines with blue
focus accents. Earlier dated sections below preserve the status of their own releases.
[desktop-0.2.15-2026-10-27.json](desktop-0.2.15-2026-10-27.json) records the app/native hashes,
100 desktop tests, 94 room UI checks, 11 Files UI checks, 29 native harnesses, eight WinHTTP cases
and both package startup checks.

## Desktop 0.2.14: 2026-10-27

[desktop-0.2.14-2026-10-27.json](desktop-0.2.14-2026-10-27.json) binds the new portable app,
rebuilt native payload and test evidence. [QA](../../desktop/QA-0.2.14.md) lists 37 new refinements.
It separates 91 desktop tests, 81 UI checks, real renderer recovery, 29 native harnesses and
eight loopback transfer cases from prior live FL runs. Profiles and the production relay are
unchanged; no FL process was launched or stopped and nothing was sent to GitHub.

## Desktop 0.2.13: 2026-10-27

[desktop-0.2.13-2026-10-27.json](desktop-0.2.13-2026-10-27.json) binds the desktop UI, bridge,
test evidence and portable executable. [The detailed QA report](../../desktop/QA-0.2.13.md)
lists 40 fixes and refinements. Native payload and profiles match 0.2.12; no FL project was
opened or stopped, no public room was created, and no GitHub write or relay deployment occurred.

## Desktop 0.2.12: 2026-10-27

[desktop-0.2.12-2026-10-27.json](desktop-0.2.12-2026-10-27.json) records the status-only native
toolbar, duplicate-preset provenance regression, compact component UI and rebuilt portable app.
FL mappings and the relay are unchanged. No live FL processes were launched or stopped; the
existing user FL24 process remained open. [QA](../../desktop/QA-0.2.12.md) separates these checks
from the earlier six native version pairs.

## Desktop 0.2.11 and portable sync: 2026-10-27

[desktop-0.2.11-2026-10-27.json](desktop-0.2.11-2026-10-27.json) records exact artifacts, six
native cross-version pairs, public relay/CRDT tests and deployment identity. Any supported
build may host when portable sync is negotiated; older members share notes/basic controls
without loading newer project/preset bytes. FL24 remains experimental and unknown builds still
need reviewed mappings. [Current QA](../../desktop/QA-0.2.11.md) records failures and limits.
Earlier sections below describe their own versions.

## Desktop 0.2.8, compact window and note CRDT: 2026-10-27

[desktop-0.2.8-2026-10-27.json](desktop-0.2.8-2026-10-27.json) records the local CRDT, native
synthetic regressions, compact-window UI checks and package identities. The Worker is undeployed;
production rooms keep locked snapshots. No new live FL session or physical MIDI result exists.
The older-host policy and exact supported-build checks remain. See
[the current design/test report](../../desktop/QA-0.2.8.md) for scope and limitations.

## Desktop 0.2.7 and all-pairs checks: 2026-10-27

[desktop-0.2.7-2026-10-27.json](desktop-0.2.7-2026-10-27.json) records the simpler major-release
picker, exact folder preservation and pre-launch member compatibility check. The desktop and
local relay tests cover all nine combinations of installed FL24/25/26 builds. Allowed pairings
exchange supported message markers both ways; older members are refused from newer hosts.
These tests do not validate FL project parsing or MIDI. The installed engine fingerprints
remain unchanged, FL24 remains experimental, and no native test launch bypassed the previous
automatic approval rejection. No GitHub write or Cloudflare deployment is included.

## Installed FL24 recheck: 2026-10-27

[fl24-installed-2026-10-27.json](fl24-installed-2026-10-27.json) verifies the signed installed
24.2.2.4597 engine identity, actual 0.2.6 package/portable discovery and selection, and local
regressions. It records the package-test fix for multiple FL24 installations. Automatic approval
review blocked the isolated native launch, so no new project, edit or MIDI runtime result exists.
FL24 remains experimental. No profile was promoted, package rebuilt, relay deployed or GitHub
change published. The existing user FL process was preserved.

## Deployed relay hotfix: 2026-10-25

[relay-fl24-hotfix-2026-10-25.json](relay-fl24-hotfix-2026-10-25.json) records the narrow Cloudflare
deployment **`c5647634-648d-450d-ad79-a61c60b8336b`**. It adds relay admission for the exact
**24.2.2.4597** compatibility declaration and separates transport/compatibility rejection messages.
It does not deploy the broader local relay fixes, rebuild the desktop app, add a native build
mapping, or publish anything to GitHub.

Production validation passed **9/9 non-admitting probes** and **24/24 synthetic checks** in one
disposable room with an FL24-declared host and FL25/26-declared members. Checks covered admission,
note-message marker forwarding in both directions, sequence ordering, status and verified cleanup.
There were **zero FL process launches and zero R2 uploads/downloads**. Synthetic messages establish
relay behavior only; they do not exercise FL note handling, project loading or recorded MIDI.
The failed native runs below remain unresolved, and native FL24 sharing remains unverified.

## Current local sweep: 2026-10-25

[sweep-2026-10-25-pass2.json](sweep-2026-10-25-pass2.json) records the separate 0.2.5
file-safety and recovery update. Its evidence covers local native, relay, desktop and package
checks. No new live FL run or physical MIDI test was made, and no new build profile was added.

[sweep-2026-10-25.json](sweep-2026-10-25.json) records the 0.2.4 source/binary/package identities,
local regression results and the unsuccessful fresh FL25/26 attempt (**0/6** basic checks).
License/update prompts and member trial mode were observed; the normal initial sync did not
complete. The run stopped before broader features or recording were retested. Later native
fixes were built and tested in harnesses but have no fresh successful real-FL session result.
Older passing records below remain tied to their original binary hashes.

Desktop version **0.2.6** offers exact **24.2.2.4597** as an explicitly selected **experimental test**.
Choosing it passes `--allow-experimental`; the native launcher still requires the candidate's
exact engine fingerprint. The app prefers a non-experimental installation when no choice is
saved. This replaces the 0.2.4 picker restriction, not the failed native test results. Unknown
patch builds remain refused. See [the feature matrix](../../desktop/QA.md) and
[current compatibility scope](../../../BUILD-COMPATIBILITY.md).

## FL 24.2.2.4597 candidate

The exact signed FL 24 engine has a [static mapping](24.2.2.4597-static.json) and native format
adapters under `FLIM_EXPERIMENTAL_FL26`. [The investigation](24.2.2.4597-investigation.json)
identifies the official archive and engine fingerprint. [The runtime record](24.2.2.4597-runtime.json)
keeps successful startup/room admission separate from failed project loading and edit checks.
The isolated application reported trial mode; no license restriction was bypassed. An unlocked
runtime is required to continue project tests. No reviewed profile is exported, mixed FL 24/25/26
sessions remain unverified, and unknown adjacent FL 24 builds stay refused.

## Earlier cutoff and recorded-note validation

[reliability-runtime.json](reliability-runtime.json) identifies the earlier client binaries and
deployed Cloudflare Worker. Twenty local relay tests, fourteen deployed-service checks, sixteen
native harness executables and sixty Python tests passed. Recording-style native FL tests passed
8/8 for a local FL25 host/FL26 member, 14/14 for two FL26 instances (including member channel
creation/deletion/reordering), and 8/8 through the deployed relay with FL25/FL26.

Notes arrived while insertion continued in both directions; final contents, native moves and
deletions matched. These probes insert persisted notes through FL functions. They do not inject
physical MIDI input or verify unrecorded audition. The record preserves the earlier fixture
failure and its correction. It also binds source, package and redacted log hashes. The broader
feature results below are prior validation and are not substitutes for this latest record.

One experimental Windows x64 client supports all five exact builds below. Build both launcher
and engine with `FLIM_EXPERIMENTAL_FL26=1` using `client/build.bat`; output goes to
`client/out-experimental`. The default build includes the three FL 25 tables in `client/out`.

| FL build | Validation in this fork |
|---|---|
| **26.1.7.5654** | Historical 31/31 runtime checks; see current results below |
| **25.2.5.5319** | Historical 31/31 runtime checks; see current results below |
| 26.1.6.5639 | Table retained; earlier client passed 31/31 checks and 12/12 repeated effect checks before FL was updated |
| 25.2.3.5171 | Original reference table retained; synthetic hook checks passed |
| 25.1.6.4997 | Original upstream table retained; synthetic hook checks passed |

Reviewed external profiles can extend FL 25/26 support without rebuilding. The client and relay
negotiate a shared format before admission. The oldest build must host; project and preset states
can only originate from people on that exact host build. Supported logical edits travel both ways.
Unknown builds remain disabled until reviewed and tested. See [the policy](../../../BUILD-COMPATIBILITY.md).

## Earlier profile and mixed-session validation

The installed FL 25.2.5.5319 host and FL 26.1.7.5654 member passed **34/34 feature checks** with the
new profiles and negotiation, including supported parameter, name and note edits back to the host.
[The new runtime record](mixed-profile-runtime.json) binds current binaries, profiles, server source
and logs. Same-build and refusal checks are listed there as separate results.

The mixed effect check compares plugin identity (201), wrapper state (212, local visibility excluded)
and plugin payload (213). Other FL writer events differ across versions and are not claimed equal.
Same-build checks still compare all events except local visibility.

The same final binaries also passed **34/34 checks on each same-build pair**, FL 25 and FL 26.
The native refusal checks confirmed the newer-member preset restriction and older-member admission
refusal before project disclosure. Offline validation passed 57 Python tests, 56 Node tests,
58 session checks, 14 native harnesses and 20 process-ownership checks. All matching owned runtime
processes and the loopback relay were stopped; PID reuse was checked before lifecycle actions.

An optional older-project direct-loader helper bypassed FL's normal open notification and did not
establish end-to-end opening behavior. Its failed attempts are kept separate in the runtime record.
The current snapshot-writer source path must not be read as a completed normal UI opening test.


## Prior mapping and same-build evidence (2026-10-22)

Both installed executables and engines passed Authenticode validation. Exact launcher identities,
client hashes and runtime checks are in [the historical runtime record](25-26-runtime.json).

| Engine | SHA-256 |
|---|---|
| 26.1.7.5654 | `6d9c5ab09c1d5594012ac2c31cf9ed08e73f42164ed0964cccae10a95d9b8e02` |
| 25.2.5.5319 | `1b7e2381f2853ee590bff74429fb8aa5540d33a5b33c0c5e41ec01c03fe0c7ac` |

[FL 25.2.5 static evidence](25.2.5.5319-static.json) records all 62 addresses and reviewed calling
conventions. Four fields moved from the reference: busy pointer `3344 -> 334c`, playlist patterns
`d00 -> d0c`, selected marker `d58 -> d64` and playlist grid `cf8 -> d04`. All seven project virtual
method slots remain unchanged. Marker and editor fields were checked in their actual handlers.

[FL 26.1.7 static evidence](26.1.7.5654-static.json) records all 62 addresses, 21 layout fields,
virtual methods and focused ABI reviews. A misleading signature selected `0xf51fd0` for `SETPROJ`;
callers, arguments and string assignments establish `0x11d9380`. The new build retains the
previously verified FL 26 layouts described below. Aggregate operand counts alone were not
treated as proof of a field's object identity.

REA with Ghidra 12.1.4 independently analysed the updated engine's parameter dispatcher at
`0x1046a70`, evidence `ev_59ac9ad838ddf20bb307631f624fdab73a4a6a5ea17a3ee8fd5277a56fee2ba7`.
Its decompiler expressions corroborate mixer stride `0x1478`, effect offset `0x134c` and routing
offset `0x394`. The body contains 25,243 bytes, 799 basic blocks and 102 callees. Value-flow output
omitted 215 known edges and indirect calls remain partly unresolved. See [the Ghidra record](26.1.7.5654-ghidra.json)
for exact target identity, analysis profile and coverage limits. Raw pseudocode stays local.

At the historical checkpoint, two disposable same-build pairs used one experimental client and a loopback-only relay. Both
project joins returned `1` from FL's loader and settled before feature checks. All four test
processes and the relay were stopped using their recorded PID, start time and executable path.
Each build passed **31/31** checks covering the feature categories described below. Sample tests
used fresh 8,864-byte synthetic WAVs, forced B to download and verified downloaded content by SHA-256.

The updated project gate passed **120 normal and 184 experimental checks**, including all build
pairs, truncated inputs, invalid versions and conflicting duplicate versions. Mocked launcher
tests passed 11 normal and 13 experimental scenarios; synthetic VMT tests covered all five rows.
All 48 Python tests, buffer/FLP/plugin-race/sample/launcher harnesses and 231 activity reference
comparisons passed. Normal and experimental client builds succeeded.

## Earlier FL 26 investigation

The following analysis and runtime results refer to **26.1.6.5639** and earlier client hashes,
before the installed FL update. They are retained as historical evidence and are not attributed
to the updated engine hash. See [the earlier runtime record](26.1.6.5639-runtime.json).

### Earlier target and reference

| Image | Version | SHA-256 |
|---|---|---|
| Target `FLEngine_x64.dll` | 26.1.6.5639 | `bc3637c3e41694c820865e757d88a42bdc15b6db4011582a2ef8f4d1b05a2d95` |
| Target `FL64.exe` | 26.1.6.5639 | `b8006e980c58b3d3a7899ab9edffa8f7701c5eed9fc0dade1899bf639d80866c` |
| Reference `FLEngine_x64.dll` | 25.2.3.5171 | `54eff612a64c9eb047ee63f44204cc42678affdc270c64733bf997ecc0dccd35` |

The reference was extracted without running the signed Image-Line installer from
`https://install.image-line.com/flstudio/flstudio_win64_25.2.3.5171.exe`.
Both engines and the target launcher passed Authenticode validation. Neither the installer,
proprietary binaries, nor decompiled source belong in this repository or client distribution.

### What changed between the original builds

The [static layout record](26.1.6.5639-layout.json) records each observation and its limits.
Automatic signatures alone were insufficient: a short prologue falsely selected `0xf52060`
for `SETPROJ`; caller and argument analysis established `0x11d8e40` instead.

| Field | FL 25 reference | FL 26 |
|---|---|---|
| Mixer record stride | `0x1474` | `0x1478` |
| Effect slots | `0x1324` | `0x134c` |
| Mixer routing | `0x2e4` | `0x394` |
| Project LoadFile VMT byte slot | `0x38` | `0x40` |
| Pattern operation slots | `d8/e0/e8/f0/f8/100` | `e8/f8/108/110/118/120` |

These are now per-build values. Pattern operation IDs on the network remain unchanged.
Editor and channel offsets also moved. The original FL 25 tables retain their prior values.

REA with Ghidra 12.1.4 analysed the target engine's parameter dispatcher at `0x1046540`.
Evidence `ev_c974d8e4815259f16e4e6272ce29d738e4a84f410f721f95d897e38e6d1102d7`
records a 25,243-byte function body, 799 basic blocks and 102 callees. Its pseudocode independently
contains the new mixer stride, slots and routing offsets. Value-flow output was truncated
(215 known omitted edges); indirect calls remain partly unresolved. This is focused function
analysis, not a reconstruction of the whole application.

The bootstrap's engine-name initialization was also analysed with Ghidra, evidence
`ev_a0f99257f525c7415754b1c54ee12a0b6b99f39561eb2a2cebd5cc86b84eed5b`.
Managed companion inspection was partial because the provider reached its representation budget.

### Earlier runtime checks

Tests used two separately launched disposable FL 26 instances, an experimental client build,
and a loopback-only relay. The user's pre-existing FL process was not injected or modified.
An earlier read-only layout probe inspected bounded counts and geometry in that process.

The first run passed 25/27 checks. One failure was a test fixture with fewer than three channels;
the other was a local editor visibility bit in the effect wrapper. Reordering passed after
creating distinct channels. The effect test compares all serialized state except the documented
local editor visibility bit; it still rejects missing or malformed plugin state.

Project loading on join, channel/mixer parameters, names, colors, routing, channel add/delete,
pattern notes and insert/delete, time signatures, markers, editor geometry, effect add/remove,
channel reordering and playlist clips were exercised. The final rebuilt client passed **31/31**
feature checks, followed by **12/12** checks in three additional effect-load cycles.
The sample check verifies downloaded bytes by SHA-256 after resolving FL's saved path.
See [the runtime record](26.1.6.5639-runtime.json) for binary identities and exact coverage.

This testing also exposed a receiver race: polling during a remote preset load queued default
parameter values that later overwrote the preset. The [apply-generation fix](plugin-apply-race.json)
guards the load and rejects stale observations/reversions. The [event-format evidence](flp-extended-events.json)
explains escaped IDs and the 8,738-file preset corpus check. `%ILSharedData%` paths are resolved
through FL's configured shared-data root so downloaded samples can be shared again after resaving.

## Remaining coverage

- No broad third-party plugin matrix, long-session soak, audio render comparison or internet
  latency/load benchmark was performed.
- Mouse-arrow rendering, every pattern operation, automation editing and all lock-conflict
  combinations still need interactive testing.
- Kontakt/compressed or encrypted embedded samples remain unsupported upstream behavior.
- Current mixed-build coverage and directional preset/project restrictions are recorded above. Arbitrary cross-version format conversion is not implemented.
- Docker image building and a deployed Cloudflare Tunnel were not tested here.
- The original FL 25.2.3.5171 and 25.1.6.4997 tables were retained and passed synthetic hook
  checks. They were not rerun live after this update. Other patch builds need their own tables.

Use disposable project copies for friend testing. The code does not patch installed FL files.
