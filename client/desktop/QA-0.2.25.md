# flim 0.2.25: direct sample transfer verification

Validated 2026-10-28 after commit 8098548 (0.2.24). This release adds direct sample downloads
with Cloudflare coordination and R2 fallback. [21 changes explained](../../docs/releases/0.2.25.md).
No FL installation, open FL process, project or licensing state was operated in this round.

## Recorded checks

| Check | Result | Scope |
| --- | --- | --- |
| Desktop backend | 193 passed | Existing 180 plus broker authentication, source/path validation, parallel admission, cancellation, shutdown, generations, corrupt streams and optional-service launch handling. The main-process fixture checks hide/reopen/last-PID exit. |
| Actual Electron WebRTC | Passed locally and through deployed Cloudflare | Two isolated launch sessions, one 2,097,171-byte file, three direct downloads including two concurrent; changed source, lost peer, stale generation, origin/token denial and forced fallback. R2 download delta is one per run. |
| Native harnesses | 32 executables passed | Includes nine synthetic FL24/25/26 host/member combinations and 0.2.24 snapshot/content regressions. No new FL profile. |
| Native WinHTTP loopback | 11 named checks passed | Native registration/offering, verified direct hash/length, corrupt/missing fallback, direct bytes during an R2 pause, progress and legacy welcome. |
| Native file-sync loopback | Passed | Real inbound path rewriting, verified samples, quota/retry behavior, retained edits and untouched source audio. |
| Cloudflare emulator | 76 passed | Existing 73 plus file admission/isolation, bounds, unchanged edit order/people, revocation and companion replacement/departure. |
| Original Node relay | Passed | Retained relay behavior unchanged. |
| Room/UI and Files/UI | 127 + 68 passed | Fixture-backed controls, short/narrow layouts, dropdown/sidebar navigation, keyboard/focus, states and reduced motion. |
| Real Electron | Passed | Folder dialog/persistence, fitted window/mode switching, sidebar/200% zoom and bounded controls recovery. |
| Production package | Passed | Worker/preload/main and WebSocket runtime bytes match the bundle. Native hashes match. Reviewed FL discovery, icons, startup and portable extraction pass without starting FL. |

The real WebRTC fixture found and verified the streaming-listener cleanup correction. Broker
tests found and verified the async admission correction. The parent test driver uses the
emulator's matching Undici implementation to avoid a Node 25 global-dispatcher incompatibility.
Electron's broker/native HTTP paths are separate from that test-driver adjustment.

Cloudflare deployment: `419ae2cb-bfe5-4279-b50f-1b0e798a4304`. R2 configuration remains
536,870,912 bytes, 20,000 monthly upload attempts and 100,000 monthly download attempts.
No daily room limit or TURN service was added. New socket attachment fields are optional;
existing native participants retain their room path, and a new welcome negotiates direct files.

[Machine-readable report](../dev/compatibility/desktop-0.2.25-2026-10-28.json).
Raw logs, screenshots and temporary capabilities remain local and excluded from Git.

## Compatibility and remaining coverage

Reviewed FL builds remain **24.2.2.4597, 25.2.5.5319 and 26.1.7.5654**, with exact fingerprints.
FL24 is supported. An older receiver's notes/basic-controls workspace does not convert a newer
full project or make unsupported sounds available. Missing/rejected plugins retain their
existing isolation and preserved-state behavior. No proprietary FL files are distributed.

This pass does not establish two physical computers, a physical keyboard, remote NAT traversal,
long internet soak/load, every VST state version or arbitrary FL patches. The earlier intermittent
FL24 recovery-save stall remains unresolved. Actual native faults can pause sharing while
leaving FL open. Direct encryption does not make the complete session end-to-end encrypted.

## Design read and delivery gate

The existing equipment-card direction is retained: ENERGY 1 / RHYTHM 1 / MOTION 2. There are
no renderer, CSS, assets, palette, typography or layout changes. Current screenshots and
195 renderer checks verify the retained UI. The gate below covers that UI; functional changes
and remaining live coverage are described separately above.

- R-02 PASS: new release copy and this report use plain punctuation without em dashes.
- R-03 PASS: current UI suites cover 280/320/375/400/600/680/810/1200px layouts and short windows.
- R-17 PASS: test counts come from saved output; no customer or speed statistics were added.
- R-18 PASS: no testimonials or profile images were added; participants in screenshots are fixtures.
- R-23 PASS: no new visual asset or navigation structure was created.
- R-24 PASS: retained controls use app handlers; this round introduces no navigation link.
- R-25 PASS: contrast.json calculates the retained body, muted, focus, action, error, notice
  and caption pairs at 5.07:1 or above; the retained error/notice surfaces are unchanged.
- R-26 PASS: room and Files suites exercise action handlers, errors, retries and unavailable states.
- R-27 PASS: UI evidence covers empty, busy, ready, failed, disconnected and waiting states.
- R-28 PASS: the app has no FAQ or added generic content section.
- R-32 PASS: current tests cover Tab, arrows, Home/End, Enter/Space, Escape and restored focus.
- R-33 PASS: functional changes are saved in source; no runtime source/CSS patch was introduced.
- R-34 PASS: the single supplied light design is retained; forced-colors checks pass.
- R-35 PASS: production frontend built; 195 UI checks and actual Electron window checks pass.
- R-36 PASS: no zero-bug, universal-version or latency guarantee is made.
- R-37 PASS: DESIGN.md supplies the retained equipment-card direction and explicit dials.
- R-38 PASS: simulated room/sample data remains in test fixtures, not shipped activity.
- R-01 PASS: no glow/gradient added; retained artwork overlay supports readable controls.
- R-04 PASS: no new icons; copy, equipment and Files arrows retain their documented roles.
- R-06 PASS: General Sans and tabular room digits retain the recorded typography purpose.
- R-07 PASS: no background grid or pattern was added.
- R-08 PASS: Files/Back arrows indicate navigation and are not added to every action.
- R-09 PASS: no decorative capability badge was added.
- R-10 PASS: no glass surfaces were added.
- R-12 PASS: no new shadow; the retained picker shadow separates its expanded choices.
- R-13 PASS: no card/button/background glow was introduced.
- R-14 PASS: this remains a room component with a lateral file tool, not a feature-card grid.
- R-19 PASS: retained reveal explains sidebar placement; reduced-motion checks pass.
- R-22 PASS: retained equipment artwork has a direct computer/keyboard collaboration purpose.
- Liveliness PASS: dials are explicit and unchanged; the room/action is the focal point,
  whitespace separates room and file work, blue marks interaction, and equipment is the motif.
- C-1 PASS: compact surfaces, typography, focus and sidebar placement have reasons in DESIGN.md.
- C-2 PASS: current click-throughs exercise the room, installation, file, folder and log actions.
- C-3 PASS: no template section or extra panel was added.
- C-4 PASS: narrow/short layouts, keyboard use, error states, forced colors and zoom pass.
- C-5 PASS: claims above cite tests and include live-session limits.
- R-05 PASS: no landing-page template was introduced; the app retains its task-based composition.
- R-11 PASS: existing card/field/action radii remain varied; no ornamental circles were added.
- R-15 PASS: existing folder messages give specific recovery actions; the requested Get started label stays.
- R-16 PASS: release copy describes transfers and limits without marketing claims.
- R-20 PASS: equipment artwork, numbered rooms and FL-family selection retain the product identity.
- R-21 PASS: the user-directed light surface remains; no forced dark mode was introduced.
- R-29 PASS: charcoal, pale surfaces and the existing blue focus accent remain unchanged.
- R-30 PASS: no external product layout was copied in this round.
- R-31 PASS: the native sidebar screenshot was inspected; each retained visual decision is in DESIGN.md.
- Human checks PASS: measured text/focus pairs, visible focus and text state labels are retained;
  real Electron 200% zoom and keyboard checks pass. Unoutlined fields follow the user's explicit
  direction and retain visible labels plus blue keyboard focus. This is a Windows desktop app;
  no mobile virtual-keyboard claim is made.
