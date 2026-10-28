# Desktop verification

Current desktop refinements, clipboard/status races, installation rescan and interface recovery
are in [QA-0.2.13.md](QA-0.2.13.md), including the numbered 40-change list. The native FL payload
is unchanged from 0.2.12. No new FL or hardware MIDI session is claimed.

Earlier compact UI, status-only native toolbar, retained-preset provenance fix, and package
checks are in [QA-0.2.12.md](QA-0.2.12.md). The existing user FL24 process was left open; this
revision has no fresh native collaboration run.

Current automatic recovery, any-host fallback, six native version pairs, public-relay CRDT and
package checks are in [QA-0.2.11.md](QA-0.2.11.md).
Earlier plugin-isolation and local VST-path checks are in [QA-0.2.10.md](QA-0.2.10.md).
The 0.2.9 startup fix and earlier live connection evidence are in [QA-0.2.9.md](QA-0.2.9.md).
The unchanged interface's design gate and earlier local checks are in [QA-0.2.8.md](QA-0.2.8.md).
The dated sections below retain earlier evidence and do not certify the new native payload.

Validated on Windows. Current local checks use the supplied Framer component/runtime and rebuilt native client. Earlier live checks used the deployed Cloudflare relay and are recorded separately below. UI fixtures are used only for reproducible renderer states; they are not bundled into the app.

## Version 0.2.7: three release choices and member preflight (2026-10-27)

The dropdown presents one major-release name for each installed FL family, keeps exact path/build
details in a disclosure and preserves an explicitly selected copy. Member startup checks the
authenticated current host before opening FL, using numeric full-build comparisons. The native
handshake remains authoritative if the host changes after the check.

- **PASS: 63 desktop tests.** Includes all nine installed-build host/member pairings, numeric
  patch comparisons, retry after rejection, fresh status despite a cached people list, invalid
  and ambiguous host data, offline/closed rooms and stable selection of duplicate/custom paths.
- **PASS: 54 local relay tests with typecheck/build.** All nine FL24/25/26 pairs are exercised.
  Six allowed pairs forward note/clip/metadata/parameter markers in both directions with correct
  sender builds and increasing sequences. Three older-member/newer-host pairs are refused
  before receiving project data. These use synthetic protocol messages, not FL note objects.
- **PASS: 35 UI checks and production build.** Actual Electron discovers all three installed
  builds and shows only three major-release labels. Details work by keyboard and retain visible
  focus; unsupported explicit folder selections stay selected. Long paths reflow at
  320/375/810/1200px. The real desktop at 200% zoom keeps details and submit reachable.
- **PASS: installed fingerprints.** Signed engines 24.2.2.4597, 25.2.5.5319 and 26.1.7.5654
  match the existing profiles. FL24 stays experimental; no mapping was added or promoted.
- **PASS: packaged and portable 0.2.7 startup.** The complete executable self-extracts and
  discovers the three installed builds. FL24 is selectable with its experimental notice and
  exact path in details. Native hashes match the previous payload, no private/test files enter
  the package, all icon sizes and actual window icons match, and local diagnostics report 0.2.7
  with no FL launches. The app allocated no rooms or R2 transfers during these checks.
- **PASS: alternate-folder package and portable regression.** Both real apps retain an explicit
  saved selection of `D:\Codex-flim-compat\24.2.2.4597\app` alongside installed FL24. The test
  initially called the name-required preferences API with an empty fixture name; correcting
  that test input made the regression pass without a product change.

No native FL project or MIDI runtime test was run in this revision. The previous isolated
launch was rejected by automatic approval review with only `blocked by policy`; it was not
retried. Older failed native tests remain unresolved. These UI/protocol results do not establish
universal FL24–26 compatibility. No GitHub write or Cloudflare deployment occurred.

### 0.2.7 UI delivery gate: PASS

This gate covers the interface. It does not certify native FL collaboration. Current generated
screenshots/results are in `test/evidence`; the durable hashes and scope are recorded in
[desktop-0.2.7-2026-10-27.json](../dev/compatibility/desktop-0.2.7-2026-10-27.json).

- R-02 PASS: added product copy contains no em dash; text states actual build/path behavior.
- R-03 PASS: details screenshots at 320/375/810/1200px and automated scroll-width checks show no horizontal overflow.
- R-17 PASS: no statistics added; exact versions come from inspected native installations.
- R-18 PASS: no testimonials or invented people added to production; participants in UI tests are fixtures.
- R-23 PASS: no visual assets or navigation structure created; existing user-requested artwork retained.
- R-24 PASS: no navigation links added; the details disclosure opens actual installation information.
- R-25 PASS: summary text/white is 9.00:1, details text/white 5.74:1, focus/white 6.84:1; unchanged input boundary is 3.48:1.
- R-26 PASS: 35 click-through checks exercise controls; disclosure opens/closes by keyboard and folder selection changes the actual selected ID.
- R-27 PASS: loading, empty, unsupported, network/error, pending/active/ended, failed-launch and compatibility-rejection states are exercised.
- R-28 PASS: no FAQ exists or was added.
- R-32 PASS: summary has the shared 3px focus outline; keyboard Enter/Space, Escape and focus-return tests pass.
- R-33 PASS: changes are written directly in React/CSS/bridge source; no runtime source/CSS patch script added.
- R-34 PASS: existing light interface retained; no theme toggle or unsupported second theme added.
- R-35 PASS: production build and 35 UI checks pass, including real Electron discovery and 200% zoom.
- R-36 PASS: notices retain experimental FL24 and oldest-host limits; no native success or speed claim added.
- R-37 PASS: existing DESIGN.md direction applied before edits; ENERGY 1 / RHYTHM 1 / MOTION 2 retained.
- R-38 PASS: production details use native inspection data; synthetic room data stays in test files.
- R-01 PASS: no gradients or glows added.
- R-04 PASS: no icons added; native disclosure marker communicates its open/closed state.
- R-06 PASS: existing General Sans follows the supplied Framer component; no display type treatment added.
- R-07 PASS: no background patterns added.
- R-08 PASS: no arrows added; existing launch arrows denote opening FL as documented in DESIGN.md.
- R-09 PASS: no badges added; compatibility notices use plain text.
- R-10 PASS: no glass effects added.
- R-12 PASS: existing single-card elevation retained; no new component shadows.
- R-13 PASS: no glow effects added.
- R-14 PASS: no feature-card grid exists or was added.
- R-19 PASS: existing expansion explains menu state; reduced-motion check passes; details uses native disclosure behavior.
- R-22 PASS: existing connected-computers/keyboard artwork remains directly relevant to music collaboration.
- Liveliness/dials PASS: ENERGY 1 / RHYTHM 1 / MOTION 2 is explicit in DESIGN.md and unchanged.
- Liveliness/consistency PASS: no new animated, decorative or competing content introduced.
- Liveliness/focal point PASS: inspected screenshots retain one expanding room card.
- Liveliness/whitespace PASS: details remains in form flow; screenshots show clear field separation.
- Liveliness/accent PASS: existing coral artwork and focus treatment retained.
- Liveliness/identity PASS: linked displays/keyboard motif and General Sans remain consistent.
- Liveliness/design read PASS: existing card direction and dials declared before edits.
- C-1 PASS: disclosure separates exact technical information from simple release selection for a stated product need.
- C-2 PASS: all visible controls have exercised handlers; no placeholder actions added.
- C-3 PASS: added details contains only exact build, folder and folder-choice guidance.
- C-4 PASS: narrow/zoom, keyboard, reduced motion and all form/room error states pass.
- C-5 PASS: installation facts are inspected; protocol tests are explicitly distinguished from native runtime proof.
- R-05 PASS: one form follows the room workflow; no templated marketing sections added.
- R-11 PASS: existing control/card radius hierarchy retained; disclosure is a text control.
- R-15 PASS: user explicitly requested Get started; other controls name their actual actions.
- R-16 PASS: no marketing buzzwords added; compatibility copy states concrete restrictions.
- R-20 PASS: inspected screenshots retain music-equipment artwork and room-specific controls.
- R-21 PASS: existing user-directed light style retained; no forced dark interface.
- R-29 PASS: existing neutral palette and coral accent retained.
- R-30 PASS: supplied Framer card retained; no unrelated product styling copied.
- R-31 PASS: DESIGN.md records the purpose of grouping, disclosure, folder identity, focus and long-path wrapping.

## Installed FL24 check: 2026-10-27

The installed **FL Studio 24.2.2.4597** engine has a valid Image-Line signature and matches
the mapped SHA-256. The existing 0.2.6 packaged and portable apps discover it, label it as an
experimental test and allow selecting it. The packaged client and executable hashes are unchanged.

The first package check failed because its old extracted FL24 fixture and the new installation
both appeared in discovery. The app correctly listed both; the test incorrectly required exactly
one experimental installation. Package/portable checks now default to installed FL24, accept
additional installations, and check the selected exact path. Both the installed-copy checks and
the alternate-copy package regression pass.

- **PASS:** 72 Python tests, 48 desktop tests, 45 local relay tests with typecheck/build, and 11
  existing native offline harnesses covering compatibility, profiles, clips, notes and load failures.
- **PASS:** actual packaged and portable startup, FL24 selection/notice, native payload hashes,
  diagnostics and nine live non-admitting relay probes. No live room or R2 transfer was created.
- **BLOCKED:** automatic approval review rejected the isolated FL runtime launch with only
  `blocked by policy`. No test FL process was launched. Native project opening, edit sharing and
  MIDI remain unverified; the historical failed runs are not replaced by these offline results.

The pre-existing FL24 process was preserved. The temporary local relay was stopped. No GitHub
write, Cloudflare deployment or distributable rebuild occurred. See
[the installed-build evidence](../dev/compatibility/fl24-installed-2026-10-27.json).

## Relay connection fix: 2026-10-25

The complete 0.2.6 package already contains the launcher, native engine and reviewed profiles.
Its SHA-256 remains `657c19417b17365132dec1c78bdf98937061797b9bb4109ef1f59f10358ac728`.
The live relay was rejecting FL24 with a misleading installation message. Worker
`c5647634-648d-450d-ad79-a61c60b8336b` now accepts exact FL24.2.2.4597 and explains the
different compatibility failures. No desktop rebuild was needed.

- **PASS: 45 local relay tests**, including FL24 admission and differentiated rejection messages.
- **PASS: exact deployment artifact.** Isolated Miniflare checks passed before deployment;
  the downloaded live script matches the tested SHA-256. Existing namespace, bucket, hosting-key
  binding and storage/monthly cutoffs were preserved.
- **PASS: 9 live non-admitting checks and 24 temporary-room checks.** Synthetic FL24, FL25 and
  FL26 peers joined one owned room, exchanged inline note markers in all directions and observed
  increasing sequence numbers. Cleanup made that room unavailable. No FL process was launched
  and no R2 file was uploaded or downloaded.
- **Copy checks PASS:** the rejection reasons identify the failing connection requirement,
  explicitly say Cloudflare needs no separate installation and fit the native alert length.
  The setup notes identify the complete executable and preserve the experimental FL24 limits.

These are relay and packaging checks, not native project or MIDI validation. FL24 project sharing
remains unverified, and unknown patch builds remain refused. Broader pending relay fixes remain
undeployed. No GitHub write occurred. See [the evidence record](../dev/compatibility/relay-fl24-hotfix-2026-10-25.json).

## Version 0.2.6: FL24 test selection and local diagnostics

This revision enables an explicitly selected test of exact FL **24.2.2.4597** and adds local
diagnostics for the user's next live run. It does not establish working FL24 project sharing
or support for untested FL patches. No FL instance was launched by these automated checks.
The earlier failed live baseline remains unresolved; no physical MIDI test was performed.

- **PASS: 48 desktop tests.** Experimental selections are rechecked and receive the explicit
  native opt-in; custom installation folders survive version switches. Logger tests cover
  bounded queues, file rotation, credential redaction, failed writes, retention, active native
  processes and exact native hashes. Trusted IPC opens only the owned log folder. Its flush
  wait is bounded at two seconds, including a regression for a stalled disk operation.
- **PASS: 32 UI checks.** The FL24 experimental notice and selection, local log action,
  log-folder failure, and unavailable diagnostics are exercised alongside all existing room
  form/status states. Keyboard/focus, reduced motion and layout checks pass with zero console
  errors. Renderer room/launch scenarios use fixtures; real Electron discovery is read-only.
- **PASS: 24 native programs, four WinHTTP checks and 72 Python tests.** Fresh native logger
  checks cover concurrent writes, rotation, Unicode paths, redaction and destination recovery.
  Normal and experimental product builds pass; the experimental build is packaged. See the
  [native verification record](../dev/compatibility/diagnostics-026-native-20261025-050228.json).
- **PASS: packaged and portable startup.** Both detect two reviewed installations and the
  exact experimental FL24 candidate using isolated test preferences. Real local diagnostic
  files contain version 0.2.6, installation classifications and matching native hashes.
  Native payloads, local assets, Windows icons, renderer isolation and clean room state pass.
  The user profile and already-running FL process were preserved.

The deliverable is `release/flim-rooms-0.2.6-win64.exe`, SHA-256
`657c19417b17365132dec1c78bdf98937061797b9bb4109ef1f59f10358ac728`.
The [source/package evidence record](../dev/compatibility/diagnostics-0.2.6-2026-10-25.json)
binds these checks to the current files.

No GitHub writes or Cloudflare deployment occurred. Relay code was unchanged in this revision;
earlier relay results remain historical evidence, not a newly repeated deployed test.

### 0.2.6 design delivery gate: PASS

- **Hard gate PASS:** the 32-check UI run exercises the new action and its error/unavailable
  states, and the existing interactive inventory. No visual assets, navigation, invented
  participants or unsupported compatibility claims were added. The user-requested Get started
  label remains. Current screenshots show no horizontal clipping.
- **Accessibility PASS:** the log action uses the existing 44px text-button target and visible
  keyboard focus. Checked text contrast: log link on canvas **7.75:1**, muted text on canvas
  **4.94:1**, experimental help on white **5.74:1**, error text **7.38:1**. Keyboard/Escape/focus
  and reduced-motion checks pass; 320x450 controls remain scrollable.
- **Purpose PASS:** `DESIGN.md` records why compatibility advice belongs beside the selector
  and the optional local log action sits below the room card. No new decorative effects.
- **Liveliness PASS:** ENERGY 1 / RHYTHM 1 / MOTION 2 remains consistent with the supplied
  expanding card, General Sans, equipment motif, coral accent and structural whitespace.
- **Craftsmanship PASS:** inspected `fl24-experimental.png`, `create-375.png` and
  `short-window.png`; notices wrap, controls align, and the log action remains reachable.
  Layout tests also cover 810/1200px. These are interface checks, not live FL feature claims.

## Version 0.2.5: 2026-10-25 second sweep

This checkpoint covers local regression tests and packaging. No FL project was opened by this
sweep, no physical MIDI device was tested, and no Cloudflare deployment or GitHub write occurred.
The earlier failed live baseline remains unresolved. These results do not certify every FL feature
or every 24/25/26 patch. See [the exact evidence](../dev/compatibility/sweep-2026-10-25-pass2.json).

- **PASS: 23 native programs.** The complete build-script set excluding the separately driven
  transfer harness passed. This includes the earlier build/profile/FL24 adapter checks plus
  complete incoming-state validation, edit ordering, preserved failed-plugin baselines and
  staged whole-plugin snapshots. `t_files` exercises actual Windows files with short-write,
  disk, flush, rename, copy, lock, allocation and read failures. Existing files and backups remain
  intact. `t_samples` fails each of five nested-rewrite allocations in turn, preserves trailers,
  and proves an equal-size sample collision stops before applying the file. Disconnect cleanup
  and stale-worker rejection have regressions. Normal and experimental product builds pass
  without compiler warnings.
- **PASS: 4 real WinHTTP transfer checks.** Bounded 1,025-reference lookup, partial writes,
  streamed content/checksum and stale-session write cancellation.
- **PASS: 72 Python tests.** Compatibility profiles and static tooling.
- **PASS: 43 Cloudflare checks.** Includes request timeouts, interrupted readers, per-peer/shared
  backlog limits and recovery, closed-sender cleanup and fair abandoned-file removal. Earlier
  R2 storage/monthly limits, reservations, restarts and room isolation remain covered. The
  Worker compiles and type checks. This is local Miniflare/R2 emulation, not a deployed run.
- **PASS: 56 legacy relay tests plus 58 session checks.** Retained relay behavior continues to pass.
- **PASS: 36 desktop bridge tests.** Settings persistence failures, cancellation, room-code
  normalization, clock rollback and oversized-response cancellation are included.
- **PASS: 28 UI checks.** Create/join/retry/return, keyboard/focus, polling recovery and production
  CSP pass. Actual installed FL25/26 discovery is read-only. A narrow-window geometry assertion
  initially measured 43.9999847px during an animation; the test now waits for the real animation
  to finish before retaining the strict 44px requirement. Eight diagnostic runs confirmed the
  cause, then the full suite passed. Product styles were unchanged.

The local portable package is `release/flim-rooms-0.2.5-win64.exe`. Package checks verify the
native payload hashes, bundled profiles, local assets/font, renderer isolation/CSP, clean room
state, Windows icons and actual portable startup. They allocate no real room and launch no FL.
The package is experimental; its native fixes have no fresh successful real-FL collaboration result.

### 0.2.5 design delivery gate: PASS

- **Hard gate PASS:** current 28-check UI run has zero page/console errors; forms, folder cancel,
  retry, copy, refresh and return are exercised. Error/loading/empty/closed states remain clear.
  No fabricated participants, metrics, testimonials, signup or navigation were added to production.
- **Accessibility PASS:** keyboard/Escape/focus-return and reduced-motion checks pass. Strict
  44px target checks and scrolling pass at 320x450. The new storage notice uses existing
  `#5c4b20` on `#f2eee2` contrast and fits the inspected 375px view. Existing palette/contrast
  checks remain applicable because styles are unchanged.
- **Purpose and direction PASS:** `DESIGN.md` retains the supplied expanding card, General Sans,
  user-requested Get started label and equipment logo. ENERGY 1 / RHYTHM 1 / MOTION 2. The new
  notice explains a real recoverable failure; disabling closed-room refresh removes a dead action.
- **Liveliness PASS:** one music-equipment card remains the focal point, with a single primary
  action, structural whitespace and the existing coral accent. Expansion explains the menu state.
- **Craftsmanship PASS:** current create/notice/short-window views were inspected and retain
  alignment and readable wrapping. Desktop layouts at 375/810/1200 pass. The changes preserve
  the simple room flow; technical diagnostics stay outside the normal user interface.

## Version 0.2.4: 2026-10-25 sweep

**Local regression and package checks pass. Fresh live collaboration did not pass.** The
disposable FL25.2.5.5319 host / FL26.1.7.5654 member run passed **0/6** basic parameter/mixer
checks. License/update prompts appeared and the member reported trial mode. Initial sync did
not complete normally; these observations do not prove the sole cause of every failed check.
The owned FL processes and loopback relay were stopped using their recorded identities.
The final binaries include subsequent native fixes and have not had a successful fresh real-FL run.
No physical MIDI test or further complete live feature suite followed the failed baseline.

Passed checks:

- **21 native test programs:** exact build/profile gates, mocked launcher/injection failure paths,
  mixed-build policy, FL24 clip/mixer adapters, sample paths, plugin apply generations, metadata,
  continuous-note scheduling, room links, project-load refusal/faults and queue/cache bounds.
  New fault injection checks cover allocation failure, invalid full-note snapshots, complete
  channel permutations, atomic sample-path replacement, optional hash-cache eviction and failed
  plugin-file writes. Both normal and experimental product builds compile without compiler warnings.
- **4 native HTTP checks:** 1,025 sample references in bounded batches, complete partial writes,
  streamed checksum verification and cancellation of writes after a session generation changes.
- **72 Python tests:** profile review/export, static-tool validation and compatibility tooling.
- **35 Cloudflare emulator tests:** storage/counter recovery and cutoff behavior, room isolation,
  scoped admission and pending-reservation cancellation/authentication/handshake races. Local only.
- **56 legacy relay tests and 58 session checks:** existing protocol, lifecycle and compatibility behavior.
- **32 desktop bridge tests:** private credentials, launch/error states, cancellation/fallback,
  status lifetime and installation discovery with at most three simultaneous checks.
- **26 UI checks:** renderer controls and failure states, keyboard/focus, room-code caret/paste,
  stale copy/status requests, hidden/offline recovery, small windows, native installation discovery
  and production CSP. No renderer errors. Room/participant UI scenarios use explicit test fixtures.
- **Packaged and portable app:** final 0.2.4 opens, self-extracts, loads local fonts/artwork,
  discovers the two installed reviewed builds, and starts without a room. Native payload hashes
  match the final rebuilt files; private files, test launchers and fixtures are absent. All nine
  executable icon frames and actual running-window small/large icons match the logo.
  These startup checks allocate no room and launch no FL instance. Packaging remains unsigned.

### Feature coverage

| Feature | Current local checks | Fresh real-FL coverage in this sweep |
|---|---|---|
| Get started, create/join form, validation, picker | UI fixtures and bridge tests pass; real installed discovery passes | No new successful create/join-to-sync result |
| Copy, people refresh, return, retry/error states | UI/bridge checks pass, including request/timer races | Participant state is not a live session retest |
| Unused host reservation release | Authenticated emulator and bridge fallback tests pass | New endpoint remains undeployed |
| Room isolation, admission, locks and R2 cutoff | Emulator/legacy relay checks pass | No deployed relay retest in this revision |
| Project loading and resync | Synthetic loader success/refusal/fault and stale-work checks pass | Initial sync did not complete normally |
| Channel/mixer controls, names, colors, routing | Native protocol/metadata/gate checks pass | All six attempted baseline checks failed |
| Recorded piano-roll notes, moves and deletion | Native scheduling, payload, selection and backlog checks pass | Not retested after failed baseline; older runtime results are separate |
| Live unrecorded MIDI keys/audio | Not implemented as live audition/audio streaming | Physical MIDI input not tested |
| Channels, plugin state and channel reordering | Synthetic structure/hooks/order and plugin apply/failure checks pass | No fresh complete FL test; no broad third-party plugin matrix |
| Playlist clips, markers, pattern operations, time signatures | Native format/VMT/metadata checks pass | Not retested after failed baseline |
| Samples, large lists, interruption, checksum | Real loopback WinHTTP and native path tests pass | No final-binary real-FL sample retest |
| Peer focus and mouse arrows | Existing implementation retained | Interactive rendering/conflict combinations remain unverified |
| FL24 and unknown 24/25/26 patches | Exact-build refusal and candidate adapters pass synthetic checks | FL24 project/edit checks failed previously; universal support unavailable |

The new [machine-readable sweep record](../dev/compatibility/sweep-2026-10-25.json) binds source,
native, package and evidence hashes. The [compatibility policy](../../BUILD-COMPATIBILITY.md)
lists exact builds and mixed-session restrictions. Historical successes below are tied to earlier
files and do not certify this package. Internet latency, audio rendering and long-session memory/CPU
behavior have not been benchmarked; bounded queues/cache are verified limits, not speed guarantees.
No GitHub writes or Cloudflare deployment occurred in this sweep.

### 0.2.4 design delivery gate — PASS

- **Direction, purpose and identity:** the supplied expanding card and equipment logo remain the
  focal point. `DESIGN.md` records the purpose of control sizing, contrast, feedback and layout.
  ENERGY 1 / RHYTHM 1 / MOTION 2. No assets, signup, unused navigation or decorative metrics added.
  The user explicitly requested **Get started**, overriding the generic-CTA preference.
- **Content and functional completeness:** every visible control has an exercised handler.
  Loading/empty/error/waiting/active/ended and uncertain-launch behavior is represented in tests;
  participant fixtures are outside production. Copy/status feedback belongs to the current room.
  Copy accurately distinguishes a launched FL window from a connected session.
- **Contrast and keyboard:** input border `#8b8880` on `#fdfdfb` is **3.48:1**; placeholder
  `#737068` on the same field is **4.86:1**; muted text `#686660` on white is **5.74:1**.
  Existing primary/error/focus ratios remain applicable. Focus indicators remain visible,
  including on the room heading. Keyboard/Escape/focus-return and reduced-motion checks pass.
- **Reflow and targets:** inspected current create/active-room screenshots at 375/810/1200px;
  controls remain aligned and text wraps without horizontal clipping. Targets are at least 44px.
  Actual Electron at 200% zoom has reachable controls with vertical scrolling. Visible-window
  capture `sweep-native-zoom-capture.png` confirms this; the cropped CDP full-page zoom capture
  is excluded as evidence. This is desktop/window QA, not physical mobile-keyboard testing.
- **Runtime and restraint:** no console errors, asset fetch failures, placeholder people or
  misleading success states in the final UI checks. One column and one primary action serve the
  room workflow; animation explains expansion and respects reduced motion. The pass applies to
  the interface and does not claim all live FL features passed.

## Version 0.2.3: reliability review

- **PASS: 26 bridge tests.** Includes failed/throttled retries, response-stream failure, duplicate installation roots, strict participant validation, and aborting an old room's request while immediately checking a new room.
- **PASS: 23 UI checks.** Includes automatic status recovery and hidden/offline pause/resume. The existing create/join, all error states, keyboard/focus, 375/810/1200px layouts, local assets and actual Electron discovery checks also pass. The refreshed error-state screenshot and packaged closed/expanded views were inspected.
- **PASS: 31 local relay tests.** Includes restored startup alarms, another scheduled attempt after cleanup failure, and reupload after an R2 object goes missing. Cleanup failures retain reservations and transfer counters stay persistent. No deployment or live relay test was performed for this revision.
- **PASS: 17 existing native harnesses.** The exact build gates, mixed-build negotiation, recorded notes, sample paths, profile parsing/loading, launcher failures, stale work and protocol checks still pass. Both default/experimental harness variants compile; the experimental native app was rebuilt successfully.
- **PASS: 4 native network checks.** Real WinHTTP against an ephemeral loopback fixture handles 1,025 references as 512/512/1, sends a complete 65,569-byte upload despite forced partial writes, verifies streamed download content/checksum, and stops writing after a session generation changes during a blocking read. The same harness asserts cloud/legacy file-size boundaries. It launches no FL instance and writes nothing to Cloudflare.
- **PASS: packaged payload and portable startup.** The final 0.2.3 portable executable self-extracts, loads local renderer/font/logo, and discovers FL 25.2.5.5319 and 26.1.7.5654. Renderer isolation, production CSP, private-file exclusions, all nine executable icon frames and actual Windows small/large icons pass. Bundled native hashes match the newly rebuilt files. These checks allocate no room and launch no FL instance. Test profiles are isolated from the user's open app.
- **PASS: UI delivery gate.** Existing direction and dials are retained; no assets or navigation were added. The new refresh state is connected to the pending request, repeat clicks are disabled, errors explain retry, and fixture-only participants remain outside production. Existing contrast, keyboard/focus, reduced-motion and layout checks remain applicable. Current screenshots show no overlap or clipping.

This revision changes native sample transport, so earlier live FL collaboration results are a **baseline**, not a live retest of these binaries. No physical MIDI hardware, unrecorded live audition, remote latency or unknown FL build was validated. The relay fixes are local and undeployed; the deployed version is still `23d1001c-7c2a-4bc1-bfc1-7ae9dbb8b4cc`. No GitHub writes were made. Packaging is unsigned and publishing is disabled.

## Earlier visual revisions

Version **0.2.2** adds the requested connected-computers/keyboard logo to the header, favicon, executable and window. All 21 UI checks passed again at 375/810/1200px, including image loading, room controls, focus and actual Electron discovery. Closed and expanded screenshots were inspected. The logo is decorative beside the text wordmark and adds no keyboard stop or repeated accessible name.

The packaged app and portable launcher each contain all nine expected ICO frames (16/20/24/32/40/48/64/128/256px), verified byte for byte against the source icon using their Windows executable resources. The running window's actual 16px and 32px icons were read with Windows WM_GETICON and matched the corresponding logo frames exactly, including transparency. The stable AppUserModelID is `com.cashflowbeatz.flim`. Both the packaged app and final portable executable opened successfully, loaded the local logo and found the two supported FL installations without opening a room or launching FL. Tests now use separate local profiles so an already-open user app does not take over their single-instance launch.

Native payload hashes match the earlier tested binaries. The existing relay/native/bridge results below are retained from that prior verification; this logo revision does not redeploy the relay or repeat collaboration tests. Packaging remains unsigned and publishing is disabled. The source, exact built-in image-generation prompt and reproducible icon conversion are recorded in `ARTWORK.md`.

Version 0.2.1 replaces the portrait with generated computer/MIDI-keyboard artwork. The UI suite passed all 21 checks after replacement, including 375/810/1200-pixel layouts and real Electron startup. Closed and expanded screenshots were inspected; the image loads locally, and the original portrait asset is absent from the new payload. This visual update does not change native collaboration or the deployed relay.

## Earlier live and integration results (before 0.2.3)

- **PASS: 21 bridge tests.** Narrow IPC, private credentials, native validation, bounded responses, duplicate launch protection, preferences, expiry and status races.
- **PASS: 21 UI checks.** Closed/open variants, create/join, name validation, installation selection, unsupported build, folder-picker invocation, copy, return, refresh, delayed status replies, startup/network/status errors, pending/ended rooms, no-install state and browser preview. The final two checks inspect real Electron discovery and production CSP.
- **PASS: 29 relay tests.** Room isolation, scoped authentication, admission throttles, legacy routes, file cleanup, and the existing persistent global R2 limits.
- **PASS: 11 live relay checks.** Two independent scoped rooms, cross-room isolation, join/status authorization and host departure. No R2 payload writes in this smoke test. See [live relay record](../../server/cloudflare/test/evidence/rooms-live.json).
- **PASS: 17 native harness executables and both native builds.** See [native room record](../dev/compatibility/desktop-rooms-runtime.json).
- **PASS: 17 local real-FL collaboration checks.** FL25 host / FL26 member, member notes, channels, samples, 6,000-note transfer, plus project resync.
- **PASS: live desktop path.** A real click on Create room opened FL25; a real FL26 member joined the deployed room. Both appeared in the UI. Member volume, pattern name and piano-roll note reached the host. No mock app, launcher or relay responses were used. Owned test processes were closed and previous preferences restored.
- **PASS: packaged payload checks.** Native files match the tested hashes. The production app loads local assets and both installations, ignores the development URL flag, has no renderer Node access, and contains no private key, test fixture, project, preference or log payload.
- **PASS: portable executable.** The final `.exe` self-extracts and opens the actual app, finds both supported FL installations, and starts with no room selected. This check creates no real room and starts no FL process.

The deployed Worker version is `23d1001c-7c2a-4bc1-bfc1-7ae9dbb8b4cc`. The Durable Object identity and existing budget tables were retained. These tests cover installed builds **25.2.5.5319** and **26.1.7.5654**, not every FL25/26 release. Physical MIDI hardware, unrecorded audition, and cross-country latency are outside this app verification.

## Design delivery gate

- **PASS, direction and purpose:** the supplied rounded card and General Sans guide the app. The user-requested computer and MIDI keyboard illustration replaces the portrait. `DESIGN.md` records the simple-room brief and the reason for color, typography, spacing, imagery and a single card. ENERGY 1 / RHYTHM 1 / MOTION 2.
- **PASS, content:** no signup, invented participants, testimonials, statistics or unused navigation in production. Test participants are explicitly confined to the test runner. Every production room row comes from authenticated status.
- **PASS, layout:** screenshots inspected at 375, 810 and 1200 pixels and in the real desktop window; no horizontal overflow. The form remains in normal document flow with vertical scrolling available on shorter windows.
- **PASS, type and contrast:** General Sans loads and starts the computed family stack. Checked ratios: muted text on canvas **4.94:1**, placeholders **4.58:1**, errors **7.38:1**, primary button text **15.14:1**. The equipment illustration has a dark backdrop and text overlay.
- **PASS, controls:** Get started and Close toggle the supplied variants. Create/join, picker, version, submit, retry, copy, refresh and return have working handlers. UI fixtures cover their error paths; real UI/native integration covers actual creation and launch.
- **PASS, keyboard and motion:** name receives focus after discovery; Escape closes the menu and returns focus. Native inputs/select/buttons have visible focus states. The room status is announced politely. Reduced-motion disables decorative transitions. Labels stay mounted through hover.
- **PASS, runtime:** no StrictMode or SSR; vendor files excluded from Fast Refresh. No `__withFX` warning, JavaScript error, remote asset failure or invalid room-input regex in the final UI run. Content slots replace ineffective portfolio FX nodes with app-owned behavior. Explicit class overrides prevent Framer's layout transforms from stretching the form during expansion.
- **PASS, state and safety of actions:** loading, missing installation, unsupported build, network failure, failed launch, empty/pending room, active room, ended room and retry states are covered. A delayed status response cannot restore a room after returning to start. The UI accurately says returning leaves FL open.
- **PASS, visual consistency:** one type family, one column, consistent control sizing and alignment, and one primary action per form. The supplied artwork provides the color accent. Screenshots verify both open and closed states rather than only the initial view.

Local screenshots and machine-readable results are under `test/evidence/` (ignored as generated test artifacts). The durable verification record is [desktop-app-runtime.json](../dev/compatibility/desktop-app-runtime.json). Re-run `npm test`, `npm run test:ui`, `node test/package.cjs` and `node test/portable.cjs` for bridge, UI and packaged-app checks. The packaged test requires a completed Windows build and these installed FL versions.
