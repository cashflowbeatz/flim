# flim Rooms 0.2.14

This update makes **37 new fixes and refinements**. These are specific interface and reliability
changes, not a claim that 37 crashes were found. The earlier 0.2.13 list is separate history.
No GitHub write or Cloudflare deployment is included. Exact compatibility profiles are unchanged.

Use the complete [portable app](release/flim-rooms-0.2.14-win64.exe) for the next session. Everyone
should open the same complete app. Existing FL windows were not replaced or interrupted.

## What actually changed

1. **Removed Menu.** Open test logs is now a direct action in the existing caption row. There is
   no dropdown or extra footer row to navigate before reaching diagnostics.
2. **Made the app version persistent.** The release number appears beside the wordmark, making
   it possible to compare copies with a friend without opening another control.
3. **Removed the outer card shell.** The large rounded wrapper and its shadow are gone. The
   equipment artwork keeps a modest corner; the expanded form sits directly on the white surface.
4. **Simplified input shapes.** Name, room number and FL selection use a pale neutral surface
   with one bottom edge, replacing nested rounded boxes while keeping editable areas identifiable.
5. **Removed red input outlines.** Keyboard focus is charcoal and compact. Invalid fields retain
   their written error and a dashed neutral edge instead of a red border or inset red shadow.
6. **Removed outline-like press effects.** Buttons no longer draw another inset box when pressed.
   A slight brightness change supplies press feedback without adding lines around the control.
7. **Cleaned participant rows.** Circular initial avatars and divider rules are removed. Real
   names, FL families and roles remain, with the existing stable order and bounded scrolling.
8. **Simplified and clarified copying.** The copy action has no surrounding box and now says
   Copy room number, accurately describing the eight digits copied rather than implying a full invitation.
9. **Flattened Create/Join switching.** Two text choices replace the gray tray and nested outlined
   buttons. An underline and stronger text identify the selected action.
10. **Corrected switch semantics.** Create/Join are now labelled tabs with one tab stop and a
    connected panel. Arrows, Home and End operate the choices; Tab moves into the form.
11. **Kept repeated selections harmless.** Clicking the already selected room action no longer
    clears validation or schedules another focus jump. A user can inspect an error without losing it.
12. **Fixed a typing/focus race.** Delayed mode autofocus checks that the user has not moved focus
    or edited the number. It cannot steal the cursor just as input begins.
13. **Reopened Join at the useful field.** If the name is already valid, reopening collapsed Join
    focuses the room number instead of sending the user back through the name field.
14. **Canceled stale paste ownership on collapse.** A pending clipboard result cannot overwrite
    the draft after Back/Escape closes the controls, even if they are reopened immediately.
15. **Reset setup between room visits.** Returning to start selects Create and clears the old room
    number and field errors, while retaining the name and selected FL installation.
16. **Accepted international number entry.** Full-width, Arabic-Indic and Eastern Arabic-Indic
    digits normalize to the same room number in both the interface and native-launch bridge.
17. **Accepted copied separator variants.** Nonbreaking/thin whitespace and common Unicode dashes
    normalize consistently. A room number copied from another app no longer fails solely on typography.
18. **Handled ordinary paste as a room number.** Ctrl+V pastes a complete valid number into the
    field rather than accidentally appending it to an existing eight-digit draft.
19. **Preserved the draft on invalid paste.** Unrelated text or an incomplete clipboard value leaves
    the current number intact and shows a field-specific explanation. No room request is sent.
20. **Formatted after editing.** A complete number becomes two groups of four on blur. Formatting
    is deferred while typing so deleting or inserting in the middle retains the caret position.
21. **Repeated copy feedback correctly.** Starting another copy clears the prior success state,
    allowing the completion to be announced again rather than leaving stale confirmation visible.
22. **Removed dead actions after room closure.** Copy and Refresh are omitted for ended rooms.
    Return to start remains available, without presenting buttons that cannot do anything useful.
23. **Prevented duplicate log requests.** A synchronous in-flight guard blocks rapid repeated
    activations before React updates the button, avoiding duplicate folder-open requests.
24. **Restored the current room after interface reload.** Startup opens the room already held by
    the main process instead of showing the unrelated Get started screen after control recovery.
25. **Respected restored work in progress.** Controls honor the bridge's busy state after reload,
    preventing another launch or return action while the original main-process operation finishes.
26. **Refreshed restored rooms promptly.** Recovered controls request status immediately instead of
    waiting for the next 30-second interval. The existing bridge cache still deduplicates requests.
27. **Retried temporary status failures safely.** A read-only status request retries one network
    reset or HTTP 502/503/504 within its original timeout. Leaving cancels the retry; room creation,
    quota failures and missing-room responses are not retried automatically.
28. **Explained non-JSON service failures.** An HTML error page from an unavailable relay now maps
    to the relevant service error instead of the misleading invalid-JSON message. Successful
    responses still require valid, bounded JSON and room data.
29. **Recovered a crashed control renderer automatically.** The desktop reloads the interface
    without replacing its main-process bridge or issuing leave/launch commands to FL Studio.
30. **Bounded crash recovery.** Two automatic retries are allowed per minute. Repeated failures
    reach a small recovery screen with a working reload action, avoiding an endless reload loop.
31. **Kept load errors from quitting the app.** Main-frame load failures enter recovery rather
    than falling into the startup quit path. Aborted navigation and subframe failures are ignored.
32. **Recorded recovery evidence.** Local diagnostics now record read-only request retries and
    automatic/manual renderer recovery decisions, using the existing bounded and redacted logger.
33. **Retried temporary sample lookups.** The native worker retries one transient transport or
    502/503/504 failure while checking which files are missing. Other errors and safety blocks
    are not turned into retry loops.
34. **Stopped auxiliary sample lookups from ending sharing.** If lookup still fails without a
    safety fault, the edit continues without remote sample references and keeps its original paths.
    The failure is logged; the remote sound can remain unavailable. Quota and memory guards remain.
35. **Bounded metadata and DNS waits.** Sample metadata requests use 10-second send/read timeouts
    instead of the 120-second file-transfer setting. DNS resolution has a 15-second limit rather
    than an unlimited wait. Actual file transfers retain their longer transfer timeout.
36. **Rejected unrelated sample hashes.** A lookup response may request only hashes in its current
    batch. Unexpected values no longer silently pass through the response parser.
37. **Cleared partial lookup decisions.** If any lookup batch fails, every partial upload flag is
    reset. An earlier successful batch cannot leave half-valid decisions behind after a later failure.

## Verification and evidence

- 91 desktop tests: bridge, permission boundaries, preferences, diagnostics, build combinations,
  read-only retries/cancellation, input normalization, recovery budget and window geometry.
- 81 browser/Electron UI checks: all controls and principal states, 280-1200px layouts,
  keyboard use, ordinary/deferred paste, mode focus race, restored rooms, forced colors,
  reduced motion, installed FL discovery/rescan and actual 200% zoom. No unexpected UI errors.
- Real Electron recovery: deliberately crashed an isolated test renderer three times. The first
  two reloaded; the third reached recovery; its button restored controls. Main PID and bridge
  state remained the same. Recovery-only IPC rejected the normal renderer, and logs recorded it.
- All 29 native harnesses pass. This includes existing CRDT convergence/reordering/duplicate
  tests, notes, portable workspace, plugin loading/provenance, sample rewriting, project-load
  preservation, bounded queues, launcher, toolbar and profile checks.
- Eight real WinHTTP loopback cases pass: bounded sample batches, partial writes, streamed
  checksum, temporary lookup retry, partial-decision reset, unrequested hash rejection,
  nonfatal lookup fallback and stale-session download cancellation.
- Production TypeScript/Vite build and experimental native build pass. Native window geometry
  is 472x395 closed, 472x666 setup, capped to 928x1008 at 200% zoom on this monitor. No claim of
  measured startup speed or reduced live MIDI latency is made.
- Packaged and self-extracting portable startup pass. The tested package contains the rebuilt
  native files, exact profiles and recovery assets, and has no test fixtures, preferences or keys.
  All nine executable icon sizes and the running Windows icons match the existing logo.

Portable size: 103,617,231 bytes. SHA256:
`4413437fbb88392df7c081cdcc55bc7c34696eb59ea751dc8b33097d99b988c5`.

Screenshots and UI/recovery results: [0.2.14 evidence](test/evidence/0.2.14).
Command logs use the latest-*-0214.log names under test. Exact package hashes and packaging
verification are recorded in [the compatibility record](../dev/compatibility/desktop-0.2.14-2026-10-27.json).

## Design delivery gate

- R-02 PASS: authored UI copy has no em dash; room separators are input data, not prose.
- R-03 PASS: UI sweep checks 280, 320, 375, 600, 810 and 1200px; native 200% zoom has no horizontal overflow.
- R-17 PASS: displayed room/people data comes from state; test people remain only in fixtures.
- R-18 PASS: no testimonials or invented production users are present.
- R-23 PASS: existing artwork/logo were explicitly requested; no new visual assets were invented.
- R-24 PASS: Menu navigation is removed; Back, room entry and room return have real destinations.
- R-25 PASS: measured focus on input 9.22:1, field boundary 3.30:1 (non-text), muted text on white
  5.74:1 and placeholder on input 4.62:1. Existing solid artwork backing is retained.
- R-26 PASS: mode choices, form, installation actions, paste, copy, refresh, logs and recovery reload
  have working routes exercised by the UI, bridge and recovery checks.
- R-27 PASS: loading, empty, invalid, offline, missing/unsupported installation and failure states are exercised.
- R-28 PASS: no FAQ is present.
- R-32 PASS: tabs/panel/one stop, arrows/Home/End, Tab, Escape, neutral focus and forced colors are checked.
- R-33 PASS: UI features are written in repository source; no runtime CSS/source patch script is shipped.
- R-34 PASS: no theme toggle is present; forced-colors mode is tested.
- R-35 PASS: production build, 81 click-through/state checks, real Electron sizing and crash recovery run.
- R-36 PASS: no fabricated performance, security or universal-compatibility claims are added.
- R-37 PASS: DESIGN.md records the latest user direction and the retained requested Get started wording.
- R-38 PASS: no fictional activity feed, metrics, production people or feature claims are introduced.
- R-01 PASS: the retained artwork scrim supplies contrast; no new gradient/glow decoration.
- R-04 PASS: the existing copy symbol identifies copying; no generic decorative icon set is added.
- R-06 PASS: General Sans retains the supplied component identity; no oversized code/monospace treatment.
- R-07 PASS: no grid/dot background.
- R-08 PASS: external-launch arrow is retained only on the FL-opening action; Back has its directional symbol.
- R-09 PASS: no promotional capsule badges.
- R-10 PASS: no glass layers.
- R-12 PASS: outer shell shadow removed; the artwork button's small shadow separates it from the image.
- R-13 PASS: no decorative glows or pulsing state lights.
- R-14 PASS: a single functional component; no repeated feature-card grid.
- R-19 PASS: short entry/hover feedback follows MOTION 2; reduced motion disables it.
- R-22 PASS: equipment artwork depicts the user's requested connected music setup.
- Liveliness PASS: ENERGY 1 / RHYTHM 1 / MOTION 2, one main action per state, structural whitespace,
  retained cable/wordmark accent and equipment motif; direction recorded before edits.
- C-1/C-2/C-3 PASS: removal and placement decisions are explained in DESIGN.md; every retained element
  serves room setup, status or diagnostics and has functioning behavior.
- C-4/C-5 PASS: errors, narrow screens, keyboard, zoom and renderer recovery tested; evidence limits stated.
- R-05/R-11/R-15/R-16 PASS: no dashboard template, capsule controls or buzzword copy; requested Get started
  remains and other actions name their operation.
- R-20/R-21/R-29/R-30/R-31 PASS: retained product artwork/logo, user-requested white surface and bounded
  neutral palette; each layout/color/type/spacing choice has a written purpose in DESIGN.md.

## Limits that still matter

Installed builds are FL24 24.2.2.4597 (experimental), FL25 25.2.5.5319 and FL26 26.1.7.5654.
Unknown patches still need reviewed native profiles. No profile was generalized by bypassing checks.
Supported older members retain shared notes/basic controls when a newer host's full project cannot load.
Unsupported sounds and playlist features remain unavailable in that fallback.

FL24's intermittent recovery-save stall remains unresolved. This update did not run a new live
cross-machine FL session or physical MIDI-keyboard test. Earlier six-direction/native/relay
evidence belongs to 0.2.11. Harness convergence is not a new live MIDI recording test.

The native engine was rebuilt, while compatibility profiles and the production relay were unchanged.
R2 and corruption/memory/project-preservation safety blocks remain. Recovery cannot guarantee that
FL or a third-party plugin will never crash. A skipped sample may still need to be made available
to the other person. The existing user FL24 process remained open throughout the work.
