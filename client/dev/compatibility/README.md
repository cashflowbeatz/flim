# FL 26 port: validation record

The opt-in table targets **FL Studio 26.1.6.5639, Windows x64**. It is an experimental port,
not a promise of compatibility with other FL 26 builds. Build both launcher and engine with
`FLIM_EXPERIMENTAL_FL26=1` using `client/build.bat`.

## Target and reference

| Image | Version | SHA-256 |
|---|---|---|
| Target `FLEngine_x64.dll` | 26.1.6.5639 | `bc3637c3e41694c820865e757d88a42bdc15b6db4011582a2ef8f4d1b05a2d95` |
| Target `FL64.exe` | 26.1.6.5639 | `b8006e980c58b3d3a7899ab9edffa8f7701c5eed9fc0dade1899bf639d80866c` |
| Reference `FLEngine_x64.dll` | 25.2.3.5171 | `54eff612a64c9eb047ee63f44204cc42678affdc270c64733bf997ecc0dccd35` |

The reference was extracted without running the signed Image-Line installer from
`https://install.image-line.com/flstudio/flstudio_win64_25.2.3.5171.exe`.
Both engines and the target launcher passed Authenticode validation. Neither the installer,
proprietary binaries, nor decompiled source belong in this repository or client distribution.

## What changed between builds

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

## Runtime checks

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
- Cross-version FL project compatibility is not established; use the same exact FL build.
- Docker image building and a deployed Cloudflare Tunnel were not tested here.
- Existing FL 25 tables passed synthetic hook tests; installed FL 25.2.5.5319 is not supported
  merely because it shares the FL 25 name.

Use disposable project copies for friend testing. The code does not patch installed FL files.
