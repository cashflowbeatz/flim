# Plugin recovery and local VST resolution, 0.2.10

The native client isolates a missing scanned external plugin or an ordinary rejected preset
instead of closing the room. It retains the latest received bytes, blocks the unavailable
plugin's parameter writes and placeholder snapshots, and leaves channel/mixer basics and other
supported edits enabled. Rejected identical states do not trigger repeated loads or save sweeps.
Malformed FL streams and understood-but-invalid wrapper metadata are isolated before loading.
Native access violations, structural divergence and incomplete disk/memory transfers still stop
sharing safely. Continuing after those failures would risk the project.

VST2 unique IDs and VST3 class IDs resolve an unambiguous scanned local installation independently
of folder or product-version labels. Only path tag 55 and its container lengths change in the
loading copy. The class ID, wrapper flags and opaque sound bytes remain unchanged. Name-only,
different-ID and cross-format substitutions are not used. Multiple distinct binaries sharing an
ID require an exact scanned path match; unknown catalogs/ID-only scan records defer to FL.
The plugin itself must accept the received state. Two actual vendor releases were not compared.

Unavailable channel state and pending controls follow reorder/deletion. Received controls are
coalesced without calling the missing plugin and replay after recovery. A host includes retained
presets after its project snapshot and then sends current known controls so a late joiner does
not receive the host's placeholder values. The native harness checks this outbox ordering and
the exact retained bytes. There is no new server protocol or deployment.

Newer members restore an agreed preset when their preset edit cannot be represented by the
oldest host. A defensive send gate withholds that preset without closing the room. Channel
creation/project replacement still obey existing structural and oldest-build safeguards.

## Checks

- PASS: 27 rebuilt native harness executables, including real load/queue calls against mock
  objects for rejected-state isolation, stale queues, parameter buffering/replay, recovery,
  duplicate suppression, native faults, effect clearing and channel reorder/deletion.
- PASS: bounded FLP/TLV parsing and path replacement, byte-identical opaque state, different IDs,
  VST2/VST3 separation, duplicate paths, ambiguous binaries, unknown catalogs, absent path tags,
  malformed 64-bit lengths, size limits and the Retry/Cancel/keyboard/context-menu handlers.
- PASS: 67 desktop tests and 36 UI checks. Includes the compact window, installed FL24/25/26
  choices, error/retry states, keyboard/focus, reduced motion, 375/810/1200px and 200% zoom.
- PASS: four HTTP transfer checks for bounded metadata batches, complete partial writes,
  download checksums and cancellation of stale transfers.
- PASS: native build, desktop build and portable packaging. Final package/portable checks verify
  the native payload hashes, real installed-build discovery, diagnostics, ASAR, local assets,
  production CSP, icons and initial room state. Exact artifacts are recorded in the JSON below.

## Live service checks

Normal installed profiles connected to the existing Cloudflare relay, using protocol fixture
hosts which deliberately sent no project. Each missing-plugin test sent a random VST3 identity
to mixer 1/slot 10, checked preflight rejection, repeated the identical preset, sent a blocked
plugin parameter, successfully applied a mixer pan value, verified active room membership and
cleared the unavailable empty slot. No native load was attempted for that missing VST.

| Actual member | Fixture host declaration | Missing isolation | Other control applied | Room active |
| --- | --- | --- | --- | --- |
| 24.2.2.4597 | 24.2.2.4597 | pass | pass | pass |
| 25.2.5.5319 | 24.2.2.4597 | pass | pass | pass |
| 26.1.7.5654 | 26.1.7.5654 | pass | pass | pass |
| 26.1.7.5654 | 24.2.2.4597 | pass | pass | pass |

FL26 also loaded the real installed Valhalla Supermassive VST3 from scanned metadata after
its incoming binary path was replaced by a nonexistent path from another computer. The native
log records stable-ID local resolution, load return 0 and verified effect clearing return 0.
The repeat test used the packaged native payload and completed after selecting **No** on the
owned untitled fixture's save prompt. The first attempt exposed a test-cleanup hang when the
prompt remained open; the fixture now releases its room/socket even if normal FL closing fails.
No project was saved or process force-terminated. The prior rejected isolation helper was not used.

The final malformed-preset preflight refinement was rebuilt and rechecked with the native suite
and a final live FL26 missing-plugin run. Earlier live checks remain scoped to the paths they
exercised; the exact payload hash/run association is in
[the compatibility record](../dev/compatibility/desktop-0.2.10-2026-10-27.json).

These checks do not establish every FL patch, initial project load with every missing plugin,
all third-party preset-version conversions, every shared-edit feature, hardware MIDI or audio
streaming. FL24 remains experimental. The oldest exact build must still host mixed rooms.
The local Worker/CRDT changes remain undeployed. GitHub was not changed. The working 0.2.9
portable app and its native payload were preserved.

## Design gate

Scope: the existing desktop module and the new native unavailable-plugin status/dialog. The
established ENERGY 1 / RHYTHM 1 / MOTION 2 direction and prior desktop visual gate are retained;
no desktop layout, illustration, palette or animation was changed. Relevant new behavior is
covered by native dialog/queue tests, while the full desktop click-through was repeated.

- R-02 PASS: new status/dialog copy uses ordinary punctuation; no em dashes added.
- R-03 PASS: 375/810/1200px, short-window scrolling and zoom pass in the 36 UI checks; native status uses ellipsis.
- R-17 PASS: no usage, customer or performance claims added to the UI.
- R-18 PASS: no testimonials or fictional people added.
- R-23 PASS: existing user-requested logo/artwork retained; no new visual assets or navigation.
- R-24 PASS: no links added; existing room actions pass the click-through.
- R-25 PASS: native status retains #d4dade on the fallback #4b555c, measured at 5.41:1; native dialogs use system controls.
- R-26 PASS: status opens the list; Retry queues retained presets, Cancel queues nothing; duplicate Retry requests coalesce.
- R-27 PASS: unavailable count, retained-state explanation, install/scan instructions, recovery and log evidence are implemented/tested.
- R-28 PASS: no FAQ added.
- R-32 PASS: native tab-stop/focus and Enter/Space/context-menu handlers exist; handler tests and desktop keyboard/Escape checks pass.
- R-33 PASS: behavior is in native source; test fixtures remain excluded from the app.
- R-34 PASS: no theme control introduced or changed; existing native text treatment retained.
- R-35 PASS: native/desktop builds, 36 UI checks, dialog actions, live native paths and packaged startup checked.
- R-36 PASS: tests and unverified vendor/FL compatibility limits are explicitly separated above.
- R-37 PASS: existing written direction retained; native status/dialog purpose recorded in DESIGN.md.
- R-38 PASS: no fabricated product content added.
- R-01 PASS: no gradients or new glow treatments added.
- R-04 PASS: no icon system added; native system information dialog identifies an actual unavailable state.
- R-06 PASS: existing native Segoe UI/system dialog typography retained for toolbar integration.
- R-07 PASS: no decorative grid or background added.
- R-08 PASS: no decorative arrows added.
- R-09 PASS: a real unavailable count replaces decorative badges; none added.
- R-10 PASS: no glass surfaces added.
- R-12 PASS: no shadows added.
- R-13 PASS: no glow added.
- R-14 PASS: no feature cards added.
- R-19 PASS: no new motion; desktop reduced-motion checks pass.
- R-22 PASS: requested connected-computers/keyboard artwork retained.
- Liveliness PASS: declared 1/1/2 dials, one equipment-card focal point, structural spacing, coral accent and equipment motif retained in UI captures.
- C-1 PASS: the unavailable count/list/retry addresses a real failure; DESIGN.md records its purpose.
- C-2 PASS: native Retry/Cancel actions and all desktop actions have tested behavior.
- C-3 PASS: no additional desktop page, signup flow or template section added.
- C-4 PASS: desktop viewport, zoom, loading/error, keyboard and recovery checks pass; native status text is clipped with ellipsis.
- C-5 PASS: no fabricated customer or compatibility claims.
- R-05 PASS: the user's single-module composition is retained.
- R-11 PASS: no radius system changes.
- R-15 PASS: the user's explicit Get started label is retained; new action is named Retry.
- R-16 PASS: no marketing language added to interface copy.
- R-20 PASS: the user's equipment motif and numbered-room flow remain unchanged.
- R-21 PASS: approved desktop light canvas and FL toolbar integration retained.
- R-29 PASS: no palette additions.
- R-30 PASS: existing supplied Framer component retained; no new visual template introduced.
- R-31 PASS: new native status/dialog decisions have a written functional reason in DESIGN.md.
