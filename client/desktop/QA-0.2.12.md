# Compact room component and status-only FL toolbar, 0.2.12

The complete portable app is [flim-rooms-0.2.12-win64.exe](release/flim-rooms-0.2.12-win64.exe).
SHA-256: `3e4bf8c20ecd18b4dd1c9bd90a6ca2efc3bd5cb711d798465eff32a5bf019252`.
The prior 0.2.11 portable executable is retained. Nothing was committed or pushed to GitHub,
and this update does not redeploy the relay.

## Changes

- FL's flim toolbar has no Host, Connect or Disconnect buttons. It shows connection state,
  people/focus information and unavailable-sound hints. Mouse clicks cannot open a connection
  or invitation dialog or disconnect the room. Create/join actions live in the desktop app.
- The toolbar gives status the freed space, drops version text when narrow, and hides an
  unusably small bar instead of covering FL controls. Widening the row restores it. Native
  window text follows the connection status, making it readable without inspecting painted text.
- An identical retained preset from a newer authenticated FL source still updates its origin
  and unavailable mode. Previously, duplicate suppression could preserve an earlier ordinary
  rejection and allow automatic retry to send that chunk to an older loader. The regression
  verifies no native load or sharing fault and no automatic retry of those newer bytes.
- The white desktop window follows a 456 CSS-pixel component with 8px outer padding. The gray
  canvas, separate outer caption and bottom diagnostics area are removed. Opening setup hides
  the artwork to make room for controls. Back is above the form; diagnostics are under Menu.
- Escape dismisses the tools menu and restores its trigger focus. Clicking/tabbing outside
  dismisses it without changing the room. Collapsing setup restores Get started focus after
  the button is visible. No new asset, account flow or confirmation dialog is introduced.

## Verification

| Check | Result |
|---|---|
| Rebuilt native regression executables | 29 pass |
| Native WinHTTP loopback transfer checks | 4 pass |
| Desktop bridge, diagnostics, installation picker and geometry tests | 76 pass |
| Renderer and actual Electron UI checks | 39 pass, zero renderer errors |
| TypeScript/Vite production build and portable packaging | Pass |
| Unpacked app startup, native payload hashes, icons and local diagnostics | Pass |
| Portable executable extraction/startup and clean initial room state | Pass |
| Native window growth, collapse, work-area bounds and 200% zoom | Pass |

The native toolbar harness uses owned hidden Windows fixtures. It exercises clicks across all
four connection states without dialogs, native status text, narrow-row bounds, hiding and
restoration. The plugin harness exercises the actual retained-state/load/retry functions with
mock FL objects. These are regression tests, not another live FL collaboration session.

Desktop preflight tests cover all nine ordered pairings of installed 24.2.2.4597, 25.2.5.5319 and
26.1.7.5654. Actual packaged discovery still finds all three. Exact native profiles remain required;
no unknown build was enabled. UI checks include create/join, copy, refresh, return, failed launch,
closed/waiting rooms, invalid installations, status recovery, logs failure, keyboard focus,
reduced motion, 320/375/810/1200px and short-window states. Fixture people/rooms stay outside ASAR.

Measured native bounds at 100% zoom were **472 × 380** closed and **472 × 676** expanded, including
the Windows frame/title bar. The content width is 456px. At 200%, work-area limits retain vertical
scrolling and reachable controls. Screenshots of closed/expanded native windows, joining, room
state and network error were inspected.

The first generic harness invocation mistakenly ran the HTTP transfer executable without its
required fixture URL and failed its argument assertion. It was rerun through its loopback runner,
where all four checks passed. The corrected native sweep excludes that URL-dependent executable
and passes all 29 standalone harnesses. No product failure was hidden by that runner correction.

## Runtime scope

An existing user FL24 process (PID [local PID omitted]) remained open and untouched. No FL process was started,
closed or injected into during this update. The six native cross-version pairs and two public
relay runs from [0.2.11](QA-0.2.11.md) remain separate evidence for their earlier native payload.
This revision has no new native project-sharing, hardware MIDI or audio-streaming result.

Any supported build can still host with portable sync. Older members receive notes/basic
controls while unsupported sounds and playlist features remain unavailable. Initial projects
with arbitrary missing plugins, unknown FL patches and all vendor preset combinations are not
certified. Real native faults or failed recovery still stop sharing to preserve the project.
The previously observed intermittent FL24 recovery-save stall remains unresolved.

Close the older Rooms desktop app before opening the new portable app, since the app permits
one instance per user-data profile. Existing FL windows keep their loaded DLL; the toolbar update
appears on a later FL launch through 0.2.12. Running projects were not replaced in place.

Artifact, source/native hashes and test-log hashes are in
[the verification record](../dev/compatibility/desktop-0.2.12-2026-10-27.json).

## Design gate

Direction: the supplied Framer equipment component and the user's request for less surrounding
canvas and simpler navigation. ENERGY 1 / RHYTHM 1 / MOTION 2. Existing artwork, app icons and
General Sans are retained; no new asset was needed. The component becomes the native window's
main surface, and opening setup makes the form the focal point.

| Gate | Result and evidence |
|---|---|
| R-02, R-16 | PASS: new product labels use plain action words and no em dash or promotional claims. |
| R-03, R-35 | PASS: viewport, native-window, short-window, keyboard, zoom and package checks pass. Fresh live FL runtime checks are explicitly absent. |
| R-17, R-18, R-36, R-38, C-5 | PASS: no fabricated product data or compatibility guarantees. Fixture peers stay outside production. |
| R-23 | PASS: retain the user's existing equipment artwork/logo; no asset creation. |
| R-24, R-26, R-33, C-2 | PASS: room actions, tools menu, log action and native status paths are implemented and exercised. No ghost navigation. |
| R-25 | PASS: normal text #272622/white 15.14:1, muted #686660/white 5.74:1, menu #4b4943/#f0efeb 7.82:1, primary white/#272622 15.14:1; field boundary 3.48:1 and focus 6.84:1. Native default badge #d4dade/#4b555c is 5.41:1. Existing unchanged state colors retain prior verified pairings. |
| R-27 | PASS: loading, empty, unavailable diagnostics, unsupported installation, launch/network error and ended-room states stay actionable. |
| R-28 | PASS: no FAQ or filler section. |
| R-32, C-4 | PASS: Tab/Enter/Space/Escape, focus return, menu dismissal, no horizontal overflow and reduced-motion checks pass. |
| R-34, R-21 | PASS: retain the supplied light component and FL's own toolbar background; no broken or decorative theme toggle. |
| R-37, R-31, C-1 | PASS: user-directed compact component, recorded dials, navigation and native-space decisions are written in DESIGN.md. |
| R-01, R-07, R-10, R-13 | PASS: no new glow, grid or glass. Existing image overlay serves equipment text legibility. |
| R-04, R-08, R-09 | PASS: equipment icon identifies the app; Back/launch arrows name actual navigation; native status reflects real connection state. |
| R-06 | PASS: retain established typography and native system font. |
| R-12 | PASS: remove the old outer card shadow. The tools popup shadow distinguishes a temporary overlay. |
| R-14, R-05, R-30, C-3 | PASS: retain a single supplied component, with no dashboard/template sections or feature-card grid. |
| R-19 | PASS: short menu state motion remains; reduced-motion checks disable it. |
| R-22, R-20 | PASS: computers/keyboard motif and numbered-room flow remain specific to the requested app. |
| L-1 through L-7 | PASS: explicit 1/1/2 dials, one focal point per state, structural spacing, coral accent and existing equipment identity. |
| R-11 | PASS: artwork/card, field and control radii retain distinct roles. |
| R-15 | PASS: user-requested Get started is retained; new Menu/Back labels name their actions. |
| R-29 | PASS: neutral surfaces plus the existing coral accent; no palette expansion. |

Delivery gate: PASS within the tested scope and runtime limits above.
