# Automatic recovery and portable sync, 0.2.11

The updated app automatically isolates missing/rejected sounds, retries matching scanned
installations when their evidence changes, and keeps routine hosting, transfer and recovery
notices passive. Unchanged rejected states are not repeatedly loaded. Native faults still stop
sharing. Stable VST identities can resolve a different scanned path, but the plugin must accept
the received state; this does not convert arbitrary vendor versions or VST formats.

Any supported FL build can host when the room negotiates portable sync. An older member uses
blank note channels and shared basic controls. Newer project/preset data stays outside its
native loader. Unsupported sounds and playlist features remain unavailable. A recovery project
is saved before resetting the older workspace, and subsequent session saves use a separate
path. Recovery failure stops before clearing channels. Exact engine profiles remain required.

## Automated and package checks

- PASS: 28 rebuilt native harnesses. Covers capability immutability, targeted bootstrap order,
  safe newer-preset retention and provenance, unavailable plugin controls, installation-change
  recovery, unchanged-state suppression, preservation failure, FL's minimum-one-channel rule,
  pre-reset-equal controls, empty note scopes, queue generations and file-recipient validation.
- PASS: 76 desktop bridge/diagnostic/window-fit tests, including all nine host/member preflight
  pairings and legacy refusal when portable sync is absent.
- PASS: 71 Worker tests for capability/admission policy, all nine version pairings, targeted
  messages, restart persistence, room isolation, cancellation, R2 accounting/limits and cleanup.
- PASS: 72 Python tool tests and four HTTP transfer checks.
- PASS: 36 UI checks, native compact-window/200% zoom checks, TypeScript/Vite build and packaging.
- PASS: unpacked and portable executable checks. Native bytes match the tested payload, installed
  FL24/25/26 are discovered, no keys/test fixtures/logs enter ASAR, diagnostics report 0.2.11 and
  matching hashes, production CSP and fonts load, and taskbar/window/executable icons match.

## Real installed FL sessions

Normal installed profiles were launched into owned scratch sessions. No license/registry copying,
isolated-runtime helper or physical MIDI input was used. Each local pair below passed six initial
state checks, eight ongoing note checks and nine control checks, then retained two active peers.

| Host | Member | Initial notes/controls | Ongoing notes both ways | Controls both ways |
|---|---|---|---|---|
| 24.2.2.4597 | 25.2.5.5319 | 6/6 | 8/8 | 9/9 |
| 24.2.2.4597 | 26.1.7.5654 | 6/6 | 8/8 | 9/9 |
| 25.2.5.5319 | 24.2.2.4597 | 6/6 | 8/8 | 9/9 |
| 25.2.5.5319 | 26.1.7.5654 | 6/6 | 8/8 | 9/9 |
| 26.1.7.5654 | 24.2.2.4597 | 6/6 | 8/8 | 9/9 |
| 26.1.7.5654 | 25.2.5.5319 | 6/6 | 8/8 | 9/9 |

Initial state includes a pre-existing note and pattern name, channel volume and mixer name,
color and routing. Ongoing tests add 30 notes over 2.4 seconds and require remote arrival before
insertion finishes, then compare complete notes, a move and a deletion in both directions.
Later controls include channel/mixer volume, mute, name/color/routing and member-to-host edits.
Observed first arrivals in the final local runs ranged from 0.280 to 1.007 seconds; these are
individual local measurements, not an internet latency promise.

Public-relay runs also passed with FL26 hosting FL24 and FL25 hosting FL26. Each passed the same
23 checks plus two simultaneous-insertion checks: 12 distinct notes inserted by each participant
into the same pattern survived on both sides. Logs confirm CRDT-1 negotiation. These tests use
FL's native note functions, not a physical MIDI keyboard. They do not establish live audition or
audio streaming. FL24 remains experimental.

## Deployment and observed failures

Worker `ca9554a7-76d6-40b1-9f48-995f6f0e2618` is deployed at
`https://flim-relay.cashflowbeatz.workers.dev`, with 100% traffic verified by deployment readback.
The original private R2 bucket, Durable Object namespace, migration tag, hosting secret binding
and limits remain: 512 MiB storage, 20,000 upload attempts and 100,000 download attempts monthly.
Fourteen public service checks passed, including private R2 transfer, checksum rejection,
attempt accounting, authentication and revocation on host departure. Limit exhaustion is tested
in the emulator, without intentionally exhausting production allowances. Legacy rooms retain
their negotiated modes. GitHub was neither committed nor pushed.

The local tests found and fixed the last-channel replacement assumption, a blocking hosting
dialog race and a delayed first note caused by processing hundreds of empty pattern baselines.
Test-only PID/log lookup and fixture placement errors were corrected before the final runs.
An early package inspection ran before packaging finished; final bundle/portable checks passed.

The first public FL26-to-FL24 run timed out after 75 seconds inside the recovery snapshot call.
Closing that owned test caused the save to fail; the older workspace was not cleared. A clean
repeat passed all 25 checks. The cause of that one snapshot stall is not established. It remains
an intermittent startup/snapshot limitation in the retained logs, not a proven fixed defect.
Other transient tooling failures were a default Wrangler profile pointing at another account,
credential storage disabled for the dedicated profile, one low-disk npm failure during packaging
cleanup, and Python's default user-agent being rejected by the public endpoint. Deployment used
the existing approved profile with process-local keyring access; the test now supplies its own
user-agent. The unrelated Cloudflare login was preserved. No new subscription was activated.

Results do not establish every FL patch, every project/playlist operation, initial project loads
containing arbitrary missing plugins, all third-party preset conversions, physical MIDI input,
offline editing or uninterrupted sessions. Earlier VST resolution/isolation runs are retained
separately in QA-0.2.10; their broader vendor-compatibility limits still apply. Exact artifacts,
source fingerprints, per-run log hashes and failed attempts are recorded in
[the compatibility record](../dev/compatibility/desktop-0.2.11-2026-10-27.json).

## Design gate

Scope: unchanged card layout and artwork, revised version guidance and passive native notices.
Direction remains in DESIGN.md: ENERGY 1 / RHYTHM 1 / MOTION 2. Current screenshots, including
the FL24 fallback notice, were inspected. The gate applies to the tested interface; the runtime
limits above remain explicit.

- R-02 PASS: new interface copy contains no em dash.
- R-03 PASS: 375/810/1200px and short-window/zoom checks pass without horizontal overflow.
- R-17 PASS: no unsupported usage or performance statistics were added to the UI.
- R-18 PASS: no testimonials or fabricated people were added.
- R-23 PASS: user-requested connected-computers/keyboard assets are retained.
- R-24 PASS: existing room actions and disclosures pass the click-through; no ghost links added.
- R-25 PASS: unchanged help text #686660 on white is 5.74:1; native status retains its prior verified palette.
- R-26 PASS: desktop actions pass; native status shows a hint and automatic recovery queues real work.
- R-27 PASS: loading, unavailable, invalid build, error, retry and room-ended states pass.
- R-28 PASS: no FAQ added.
- R-32 PASS: Tab, Enter, Space, Escape, visible focus and focus return pass current UI checks.
- R-33 PASS: functionality remains in repository source; test fixtures are excluded from packages.
- R-34 PASS: no theme control added; approved existing colors and system controls retained.
- R-35 PASS: native/desktop builds, full click-through, real FL sessions and packaged startup ran.
- R-36 PASS: tested builds and runtime limitations are stated beside the results.
- R-37 PASS: written direction and existing 1/1/2 dials retained.
- R-38 PASS: no fabricated product content added.
- R-01 PASS: retained image overlay keeps equipment-card text legible; no new glow/gradient added.
- R-04 PASS: retained equipment icons have the user-requested music-sharing purpose.
- R-06 PASS: General Sans and native system typography retain the established module/toolbar roles.
- R-07 PASS: no decorative grid added.
- R-08 PASS: existing launch arrow indicates opening FL; no decorative arrows added to other actions.
- R-09 PASS: unavailable count communicates actual state; no promotional badges added.
- R-10 PASS: no glass surfaces added.
- R-12 PASS: retained single-card shadow separates it from the canvas; no new shadows added.
- R-13 PASS: no new glow added.
- R-14 PASS: no feature-card grid added.
- R-19 PASS: retained expansion motion explains state; reduced-motion UI check passes.
- R-22 PASS: retained illustration depicts the user's requested connected music workflow.
- L-1 PASS: ENERGY 1 / RHYTHM 1 / MOTION 2 is explicit in DESIGN.md.
- L-2 PASS: current screenshots retain one calm module and its short state transition.
- L-3 PASS: the equipment card remains the focal point.
- L-4 PASS: existing spacing groups identity, form, room state and logs.
- L-5 PASS: coral remains the single illustration/interaction accent.
- L-6 PASS: connected displays, cable and keyboard motif is retained throughout the app/icons.
- L-7 PASS: existing design read is preserved in DESIGN.md; only factual guidance changed.
- C-1 PASS: passive hints and fallback guidance address concrete sync failures; purpose is recorded.
- C-2 PASS: native status and all desktop actions have implemented/tested behavior.
- C-3 PASS: no extra screen, account flow or template section added.
- C-4 PASS: viewport, zoom, keyboard, network failure and recovery-state checks pass.
- C-5 PASS: no fabricated customer or compatibility claims added.
- R-05 PASS: the user's single-module composition is retained.
- R-11 PASS: the existing radius hierarchy remains unchanged.
- R-15 PASS: the user's explicit Get started label is retained; no generic new action added.
- R-16 PASS: new guidance describes actual behavior without marketing claims.
- R-20 PASS: the equipment motif and numbered-room flow remain specific to flim.
- R-21 PASS: approved light canvas and FL toolbar integration retained.
- R-29 PASS: no palette additions.
- R-30 PASS: supplied Framer component retained; no replacement template introduced.
- R-31 PASS: purposes of native notices and fallback guidance are written in DESIGN.md.
