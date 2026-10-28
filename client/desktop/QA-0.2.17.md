# flim Rooms 0.2.17

Local update for the missing sidebar trigger and jumpy room menu. No GitHub push or Cloudflare
deployment occurred. Native launcher/engine, supported fingerprints and relay behavior are unchanged.

## What changed

1. **Files is visible on the first screen.** A small labelled arrow sits at the upper right
   edge of the card, so file sharing can be found before creating or joining a room.
2. **The arrow remains available throughout setup and room use.** It no longer depends on both
   joining a room and expanding its controls. Collapsing the room does not close Files.
3. **Files remains a real sidebar.** The normal 400px app expands to 680px with Files beside
   the card. At narrow widths or high zoom, Hide files restores the room in the available space.
4. **Before joining, Files explains the next step.** Open room setup goes directly to the form.
   There are no fake transfers, zero-count success claims, or reads from a previous room.
5. **The navigation has one clear row.** Back is on the left and the Files arrow is on the right.
   The duplicate Room/Files tab row and repeated room-navigation label were removed.
6. **The arrow communicates its state.** It changes direction, exposes expanded/collapsed state
   to assistive technology, and has a Show/Hide files sidebar tooltip and a 44px target.
7. **Create/Join keeps the form still.** A shared grid slot holds either the room explanation or
   number entry, preserving the common fields, submit position, overall height and scroll position.
8. **Hidden number controls cannot receive focus or submit.** They are disabled in Create mode
   and excluded from the accessibility tree. The draft number remains available when returning to Join.
9. **The first typed digit is retained after choosing Join.** Focus now moves after the React
   update commits, before the next paint, rather than waiting for another animation frame.
10. **Focus does not scroll the room form.** Switching actions, opening setup and restoring the
    entry control use preventScroll. Keyboard arrows/Home/End retain tab focus as before.
11. **Closing Files restores focus immediately.** Escape and Hide files return to the visible
    edge arrow after the layout commits. Open room setup restores its useful input in the same way.
12. **Opening Files no longer briefly collapses the window.** The width changes first, retaining
    the previous height until the new responsive layout can be measured.
13. **The initial header stays put as setup grows.** The app starts 24px below the monitor work
    area's upper edge. Setup and the installation picker grow downward. Screen containment still
    takes priority if the user moves the window near a monitor edge.
14. **Files keeps keyboard focus while room discovery completes.** Loading an existing room or
    finishing setup cannot move focus behind the sidebar through ordinary room autofocus.
15. **Returning to setup closes the old room's panel.** Late file reads are ignored, and the next
    opening without a room shows the initial explanation instead of old transfer data.
16. **The complete executable contains these changes.** The unpacked app and self-extracting
    portable were both started and their new pre-room Files arrow exercised.

## Verification

- Production TypeScript/Vite build passed. Desktop tests: **101 passed**.
- Room UI: **97 checks passed**. Files UI: **15 checks passed**. No reported browser errors.
- New switching regressions compare exact field/form/submit rectangles and scroll position
  at 280px, 400px and 810px, repeatedly preserving the typed name and room number.
- Real Electron setup/picker tests passed at normal and 200% zoom. Initial content was
  **400x354**; setup **400x776**; picker **400x930**. All three kept **y=24**.
- Ten Create/Join switches kept the native window's exact size and position. Immediate
  keyboard typing was checked, alongside existing paste/caret/ownership regressions.
- A real Electron room/file fixture measured **400x577** closed and **680x584** with Files.
  Every intermediate native resize retained at least the original height, avoiding the collapse.
  Placement, usable room controls, closing width/focus and **200% zoom** passed.
- Renderer crash recovery passed: two automatic reloads, bounded manual recovery, retained
  bridge state, and no FL launch. Tested in an isolated profile.
- Packaged source and native payload match the local files. Both packaged startup paths
  exercise the pre-room arrow and 400/680px widths. Native hashes, installed FL24/25/26 discovery,
  diagnostics, CSP, lack of renderer Node access, logo and embedded icons passed.
- New arrow text contrast: **7.13:1** idle and **6.75:1** expanded. Muted card text: **5.50:1**.
  Existing text pairs retain their previously checked colors. Forced colors and reduced motion passed.
- User FL24 PID **[local PID omitted]**, started **08:20:55 local time**, remained running with its original start time.

Evidence: `test/evidence/0.2.17`, `test/latest-*-0217.log`, plus packaged/portable result JSON
copied into the versioned evidence directory. Room/file screenshots use named layout fixtures.

## Build and scope

`release/flim-rooms-0.2.17-win64.exe`, **103,547,286 bytes**.

SHA-256: `797b78da2e30f61ce5beefd902333d724a2a9b9663c29e1d71c2446ed26dca27`

Launcher: `4157b725f0b51a9d5b506177ca1708016fc27c3bc53937c73ab0913d22ba85e2`

Engine: `94de8224c448bf3e3dddf4a897d6a1ffd5a652aa90b8f3e7fa3e016d4c67209a`

This is a desktop/navigation update. No new native binary, relay, R2 or synchronization protocol
change was made. Native sample recovery evidence remains in [0.2.16 QA](QA-0.2.16.md); those native
harnesses were not rerun for this UI update. No fresh two-computer sample drag/drop or physical
MIDI session was performed. Reviewed installed builds remain 24.2.2.4597, 25.2.5.5319 and
26.1.7.5654. Unknown patches still require verified fingerprints. Older versions retain notes
and basic-controls fallback when a newer project cannot be loaded; unsupported sounds and
playlist features remain unavailable.

## Antislop delivery gate

- R-02 PASS: changed user-facing TSX copy contains no em dash.
- R-03 PASS: room/Files suites cover 280-1200px without horizontal overflow; native 200% zoom passed.
- R-17 PASS: only real transfer/participant counts appear; no invented performance or usage statistics.
- R-18 PASS: no testimonials or profile photos were added; fixture names stay in tests.
- R-23 PASS: the user requested the arrow/sidebar and menu changes; approved artwork/logo were reused.
- R-24 PASS: Back, Files, Hide files and Open room setup have real implemented destinations/actions.
- R-25 PASS: contrast checker measured 7.13:1/6.75:1 for the new arrow and 5.50:1 muted card text.
- R-26 PASS: all existing room/picker/clipboard/folder/log actions plus the new entry paths were exercised.
- R-27 PASS: initial, empty, loading, retrying, stale, error, busy, offline and ended states passed fixtures.
- R-28 PASS: no FAQ was added.
- R-32 PASS: keyboard tabs/arrows, Enter, Escape, visible focus and immediate restoration passed.
- R-33 PASS: behavior/styles are repository TSX/CSS/CJS; no runtime source patcher is shipped.
- R-34 PASS: the established light direction remains; forced colors passed without a simulated theme toggle.
- R-35 PASS: build, UI suites, real native sizing/recovery, packaged and portable startup all passed.
- R-36 PASS: no zero-bug, universal-patch or unmeasured latency claim is made.
- R-37 PASS: DESIGN.md records the supplied direction and ENERGY 1 / RHYTHM 1 / MOTION 2.
- R-38 PASS: pre-room Files reports its actual lack of a room; no transfer activity is fabricated.
- R-01 PASS: the retained artwork scrim serves caption contrast; no new gradient/glow was added.
- R-04 PASS: the Files chevron identifies the requested lateral reveal and its state.
- R-06 PASS: retained General Sans and room-number scale preserve the existing music-equipment identity.
- R-07 PASS: no decorative grid, blueprint or dotted background was added.
- R-08 PASS: Files arrows show direction; Back and FL-opening arrows identify their real actions.
- R-09 PASS: no promotional badges were added.
- R-10 PASS: app/sidebar surfaces remain opaque without glass layers.
- R-12 PASS: the sidebar has no floating shadow; picker elevation separates the actual choices.
- R-13 PASS: no glow or perpetual animation was added.
- R-14 PASS: no repeated marketing feature cards were added.
- R-19 PASS: brief sidebar entry conveys lateral placement; reduced motion disables movement.
- R-22 PASS: the approved computers/keyboard artwork is retained.
- Dials PASS: compact neutral controls and brief transitions match ENERGY 1 / RHYTHM 1 / MOTION 2.
- Focal point PASS: Get started, the form action or the room number leads each respective state.
- Whitespace PASS: 400/680px widths fit the component; the shared entry slot prevents tab movement.
- Accent PASS: blue marks interaction/focus on the established neutral surfaces.
- Identity PASS: equipment artwork, linked-computer logo, General Sans and room number remain specific to flim.
- Design Read PASS: DESIGN.md connects the compact widget reference and explicit arrow request to the layout.
- C-1 PASS: DESIGN.md records a purpose for the width, spacing, type, motion and startup placement.
- C-2 PASS: no account/chat/voice/reference-only controls were added.
- C-3 PASS: each section serves entry, room state, real transfers or local diagnostics.
- C-4 PASS: errors, busy restoration, narrow widths, keyboard, zoom, focus and crash recovery passed.
- C-5 PASS: UI/package checks are distinguished from earlier live FL/native evidence.
- R-05 PASS: structure follows the actual create/join and simultaneous room/file tasks.
- R-11 PASS: bounded card/field/button corners remain without circular ornament or universal pill shapes.
- R-15 PASS: user-requested Get started remains; new actions name their operation.
- R-16 PASS: new copy explains actions and state without marketing claims.
- R-20 PASS: the existing equipment identity and numbered rooms remain; the sidebar serves actual sample sharing.
- R-21 PASS: the user's light direction remains consistent without forcing a new theme.
- R-29 PASS: neutral surfaces and a blue interaction accent retain the bounded palette.
- R-30 PASS: the reference informed compact structure; account/chat/voice UI was not cloned.
- R-31 PASS: major visual choices have written purposes in DESIGN.md.

UI/human/layout supplements PASS: real actions and data states, readable contrast, keyboard focus,
44px targets, narrow-screen reflow, forced colors, reduced motion and native 200% zoom were checked.
