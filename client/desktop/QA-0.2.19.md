# flim Rooms 0.2.19

37 desktop UI improvements beyond 0.2.18. Changes remain local. The FL launcher, engine,
reviewed build profiles, relay and R2 behavior are unchanged.

## What actually changed

1. **Keep Files search and filters.** Hiding and reopening Files restores the query and chosen
   status filter for the current room, rather than making you configure the view again.
2. **Keep your place.** Files restores its list scroll position when reopened. This position
   belongs to the current room and lasts while the controls are running.
3. **Reopen with confirmed activity visible.** Files retains its last snapshot in memory,
   avoiding a blank list while a fresh local status read completes. Closing still stops UI polling.
4. **Identify cached activity honestly.** A confirming message distinguishes that saved list
   from a completed new status read. Failed reads retain the existing unavailable/stale notice.
5. **Separate room views.** Leaving or changing rooms clears the saved query, filter, ordering,
   position and file snapshot. A new session cannot inherit another room's display state.
6. **Give search the full field width.** Clear now sits inside the field's reserved end area,
   instead of using a separate 44px grid column and gap. Text and the action cannot overlap.
7. **Show counts beside every filter.** All, Pending, Attention and Ready show how many files
   match the current search, so you can see which filter will have results before opening it.
8. **Describe counts accessibly.** Filter labels remain short; their accessible descriptions
   supply matching counts and distinguish initial checking from confirmed counts.
9. **Make filter selection independent of color.** A check marks the selected filter.
   Its space remains reserved on the other filters so switching does not shift their labels.
10. **Choose and remember file ordering.** The Name/Recent action switches between natural
    filename order and newest activity within each group. The current room remembers the choice.
11. **Resolve activity ties consistently.** Equal timestamps fall back to natural filename
    order and transfer identity. Equal-time records do not randomly trade positions.
12. **Start changed results at the top.** Changing the query, filter or order resets only the
    file list's scroll. Newly matching first rows are no longer hidden above the previous position.
13. **Use scrolling only when needed.** The file list gets a keyboard stop and Scroll for more
    files hint only when its measured content overflows. Short results avoid an extra tab stop.
14. **Count each visible group.** Transferring, Waiting, Needs attention and Ready headings
    show their own result counts, making long filtered lists easier to scan. Single-file results
    now say file instead of files.
15. **Explain waiting once.** Queued files share one waiting explanation per group,
    instead of repeating the same paragraph under every sample. Each file keeps its status.
16. **Explain the allowance once.** Blocked files share one allowance/retry explanation per
    attention group. Verification, missing-source and other file-specific help stays by its file.
17. **Replace a search more quickly.** Ctrl+F/Meta+F inside Files selects the current query,
    so typing starts a new search rather than appending to the old text.
18. **Protect filter keyboard input.** Composing text, modified shortcuts and handled events
    cannot accidentally change filters. Normal arrows, Home and End retain their behavior.
19. **Reuse search results for counts.** Filenames are matched once per query/data change;
    filter counts reuse those groups instead of normalizing every filename for every filter.
    This reduces repeated work; no end-to-end performance benchmark is claimed.
20. **Correct the Files arrow.** It points right to reveal the right-hand sidebar and left
    to return. Closing through that toggle also restores useful focus.
21. **Stop showing a green open badge during unavailable status.** Offline and failed status
    reads produce Offline or Status unavailable, with the non-active indicator tone.
22. **Label collapsed people counts honestly.** The compact artwork card uses the same
    connection state and marks retained participant counts as last confirmed during an outage.
23. **Keep title and closure status consistent.** The window title uses that shared status
    calculation. A confirmed closed room stays Room closed even if the computer goes offline.
24. **Copy invitations during other room work.** Copy room number remains available while an
    unrelated FL launch operation is pending. The local clipboard operation still prevents duplicates.
25. **Make copy feedback clear.** Success replaces the copy glyph with a check. Pending copy
    and log-opening actions expose their busy state to assistive technology.
26. **Explain disabled setup actions.** The primary action's linked help states whether setup,
    connection, launcher files or a supported installation is needed. It avoids duplicating the
    exact compatibility error already shown by the picker.
27. **Name restored pending work.** Setup says Finishing the current step when the main process
    is already busy, rather than leaving a disabled button labelled as though it could start a room.
28. **Defend indirect form submission.** Enter/programmatic submission checks launcher readiness
    and the selected installation's support, in addition to the existing button-disabled state.
29. **Protect room-tab keyboard input.** Create/Join ignores composition, modified shortcuts
    and already-handled events. Ordinary navigation remains one predictable tab stop.
30. **Close installation details first.** Escape closes an expanded Installation details
    disclosure and focuses its summary. It no longer collapses the entire setup at that point.
31. **Support familiar Windows picker shortcuts.** Alt+Down opens the FL selector, Alt+Up
    closes it, and F4 toggles it. These commands do not commit a different installation.
    Alt+F4 remains available for normal window closing.
32. **Cycle version prefixes.** Repeating a prefix such as 2 advances among matching FL
    families. Enter/Space still commits the preview; Escape leaves the original choice intact.
33. **Expose exact build information without clutter.** Visible picker labels stay FL Studio
    24/25/26. Accessible descriptions and option tooltips provide exact builds and support reasons.
34. **Keep logs accessible with Files open.** The existing caption/logs row moves beneath the
    combined view. Open test logs remains reachable when the narrow Files view hides the room card.
35. **Keep unavailable-log guidance visible.** Disk-space/folder-access guidance remains visible
    in narrow Files mode rather than being hidden along with the room's footnotes.
36. **Align error reading order with placement.** Global errors follow Files and its footer in
    both the document and visual order. The existing dismiss action restores useful focus.
37. **Make the caption describe the current task.** An open room asks you to keep FL Studio
    running; a closed room points back to start. Setup keeps its short no-account explanation.

## Verification

- `npm test`: 108 passed, no failures.
- `npm run build`: TypeScript and production build passed.
- `node test/ui.cjs`: 121 checks, no recorded browser errors.
- `FLIM_FILES_TEST_DIST=dist node test/file-sidebar-ui.cjs`: 39 checks, no recorded browser errors.
- `node test/window.cjs`: actual Electron sizing, ten mode switches, immediate typing,
  picker expansion/collapse and 200% zoom passed.
- `node test/sidebar-window.cjs`: actual lateral placement, no transient height collapse,
  focus/width restoration and 200% zoom passed with isolated room/file fixtures.
- `node test/recovery.cjs`: two isolated Electron crashes, bounded recovery and preserved bridge
  state passed. No user FL process was closed.
- `node test/package.cjs`: packaged startup, byte-identical source payload, native hashes,
  profile discovery, local font/CSP, icons, initial room state and Files geometry passed.
- `node test/portable.cjs`: actual self-extraction/startup, supported installed FL24/25/26
  discovery and matching local diagnostics/hash metadata passed, using process-scoped D: temp.

Screenshots and machine-readable results are in `test/evidence/0.2.19`. Added checks cover saved
view restoration during delayed reads, search-sensitive counts, equal-time ordering, composition,
modified keys, shared status, disabled-submit guards, copy while busy and narrow diagnostics.
The existing failed-read/backoff checks also cover the overflow hook after its redundant state
updates were removed. Final screenshots were inspected at 280/400/680px and in actual Electron.

## Build and startup

The complete local portable executable is `release/flim-rooms-0.2.19-win64.exe`,
**103,551,106 bytes**. Unpacked app: **384,890,238 bytes**.

SHA-256: `4915ce7cfb7246598aa50da4d138eb750456167c0d151ed6036b93778156d0b5`

Launcher: `4157b725f0b51a9d5b506177ca1708016fc27c3bc53937c73ab0913d22ba85e2`

Engine: `94de8224c448bf3e3dddf4a897d6a1ffd5a652aa90b8f3e7fa3e016d4c67209a`

Package fingerprints and startup results are recorded in
`../dev/compatibility/desktop-0.2.19-2026-10-28.json`.
Packaging/extraction use process-scoped TEMP/TMP on D: to preserve space on C:.
Windows system settings are not changed.

## Limits

This is a desktop UI update. It does not newly verify physical MIDI, two-computer sample delivery,
mixed-version project synchronization, every FL patch or network latency. Discovery checks cover
the installed FL24 24.2.2.4597, FL25 25.2.5.5319 and FL26 26.1.7.5654. Exact native fingerprints
remain required. Supported FL24 is not experimental. Older-version fallback and existing
unsupported-feature limits remain. No GitHub push or Cloudflare deployment occurred.

## Antislop delivery gate

Applied during development using the user's saved session choice. Direction: equipment card,
General Sans, numbered invitations, compact lateral Files, no dark/red input outlines, Menu,
decorative rings or new artwork. ENERGY 1 / RHYTHM 1 / MOTION 2 retained.

Hard gate:

- R-02 PASS: source scan found no em dashes in changed user-facing TSX/utilities.
- R-03 PASS: 280-1200px browser coverage and native 200% zoom passed without horizontal leakage.
- R-17/R-18/R-38 PASS: counts come from actual snapshots; no fake people, claims or activity added.
- R-23 PASS: user-authorized navigation refinements reuse the logo, artwork, arrows and check.
- R-24/R-26 PASS: named tests exercise every changed control and existing room/file actions.
- R-25 PASS: contrast checker measured blue/white 8.44:1, muted/sidebar 5.07:1,
  text/sidebar 7.94:1, focus/sidebar 5.36:1 and muted/white 5.74:1. No new palette introduced.
- R-27 PASS: initial, checking, cached, empty, no-match, failed, stale and closed states exercised.
- R-28 PASS: no FAQ or filler disclosure added.
- R-32 PASS: Tab/arrows/Enter/Escape, IME guards, picker shortcuts and focus restoration passed.
- R-33 PASS: features are ordinary source files; no runtime source/CSS rewriting introduced.
- R-34 PASS: existing light theme and forced-colors behavior retained and exercised.
- R-35 PASS: build, interactive tests and actual Electron checks recorded; package startup
  evidence is separately recorded in the compatibility manifest.
- R-36 PASS: no zero-bug, universal-patch or unmeasured throughput/latency claims.
- R-37 PASS: design direction and unchanged dials recorded in DESIGN.md.

Purpose gate:

- R-01/R-07/R-10/R-12/R-13 PASS: no new gradient, pattern, glass, floating sidebar or glow.
  Retained artwork scrim aids caption contrast; picker elevation separates its option sheet.
- R-04/R-08/R-09 PASS: arrows describe actual placement, checks show selection/copy success,
  and the room indicator follows confirmed status. No promotional badges or unrelated icons.
- R-06/R-14/R-22 PASS: retained General Sans, equipment imagery and prominent room digits
  serve readable invitations and FL collaboration; no generic feature-card grid was added.
- R-19 PASS: no new entrance motion; existing in-place fades/lateral reveal and reduced-motion
  handling remain. Changes do not introduce bouncing or moving button targets.

Liveliness and craftsmanship:

- Dials, focal point, whitespace, accent, identity and Design Read PASS: existing direction is
  preserved; the active room number or file search leads each state. Counts support file navigation.
- C-1/C-2/C-3 PASS: each change has a stated purpose above and an implemented action/state;
  no signup, chat, voice or filler widget was added to meet the requested number.
- C-4/C-5 PASS: interruptions, delayed responses, composition, stale data, narrow views,
  forced colors and native zoom covered. Fixture results are distinguished from live FL evidence.
- R-05/R-11/R-15/R-16/R-20/R-21/R-29/R-30/R-31 PASS: compact task-driven composition,
  varied bounded corners, user-requested Get started label, plain copy, equipment identity,
  established light palette and documented reasons remain; no generic product template introduced.

UI/human/layout supplements PASS: contrast measured, non-color selection, visible blue keyboard
focus, 44px targets, useful loading/empty/error states, keyboard operation and no horizontal overflow
verified. This Windows desktop runtime does not support a mobile virtual keyboard.
