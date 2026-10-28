# flim Rooms 0.2.18

Desktop UI, input and local file-status refinements. All work is local. Native synchronization,
MIDI, FL fingerprints, Cloudflare relay and R2 limits are unchanged.

## What actually changed

1. **Find a file by name.** Files now has a search field, so a room with many samples does not
   require scrolling through every transfer. Searching only filters the local display.
2. **Search tolerates naming differences.** Case, accents and compatibility-width characters
   are normalized for matching. Searching `kick` can find `Kíck` without renaming the sample.
3. **Clear search without moving the field.** A dedicated Clear action restores the complete
   query result and focuses search. Its space remains reserved while empty, preventing width jumps.
4. **Choose which transfer states to see.** All, Pending, Attention and Ready narrow the list
   to the files relevant to the current task, without pausing or restarting delivery.
5. **Operate filters from the keyboard.** Arrows, Home and End select filters. One tab stop,
   pressed state and focus feedback keep the four controls predictable.
6. **Recover from a search with no results.** A specific no-match message and Show all files
   reset both query and filter. An unmatched search is no longer confused with no shared files.
7. **See visible and total counts.** Showing X of Y files explains how much of the confirmed
   list the active search/filter is showing.
8. **Separate waiting work from active transfers.** Waiting has its own count and group.
   A queued retry is no longer described as an actively transferring file.
9. **Know transfer direction.** Each file says From this computer or To this computer,
   explaining which side is sending or receiving it.
10. **Read actual byte progress.** Active uploads/downloads show completed bytes, total bytes
    and percentage together, rather than only the total file size.
11. **Hear useful progress through assistive technology.** Progress controls expose the same
    byte description as accessible value text, alongside the filename and transfer action.
12. **Distinguish unknown from empty.** A checking/waiting/missing/blocked file with no known
    size says Size not known yet; an actual empty-file failure can still report 0 B.
13. **Format large sizes sensibly.** Size formatting supports GB and TB and rejects invalid
    values instead of displaying negative or nonnumeric byte amounts.
14. **Identify the room in Files.** The current eight-digit room number appears above the
    file list; a closed session is labelled Closed.
15. **Keep filenames in natural order.** Within each state group, numbered names sort as
    kick 2 before kick 10. Progress timestamps no longer continuously rearrange the list.
16. **Refresh status deliberately.** Refresh files has visible pending/disabled feedback.
    It rereads local status, rather than falsely implying a transfer retry or allowance override.
17. **Coalesce refresh work.** Manual and automatic checks use the same in-flight read.
    Clicking during a pending automatic read waits for that read instead of starting a duplicate.
18. **Keep automatic checks visually quiet.** Background reads do not switch the Refresh
    label back and forth each second. Explicit manual refresh still shows Checking files.
19. **Reduce repeated failing reads.** Local status failures wait 2, 4, 8 and then up to 15
    seconds between attempts. A healthy read restores the normal one-second interval.
20. **Pause hidden views.** Hiding the document cancels its pending status timer. Returning
    to it triggers a fresh read; closing Files still stops its status reader completely.
21. **Respect both unavailable indicators.** Either an unavailable response or a stale flag
    produces the recovery notice. Available=true can no longer conceal stale data.
22. **Explain an unconfirmed first read.** If status has never loaded, Files says no confirmed
    list is available. It does not present a successful empty-room instruction.
23. **Explain missing bridge support.** Older/incomplete controls without the file-status API
    ask the user to reopen the complete current app, rather than silently showing nothing.
24. **Stop checking closed rooms automatically.** Closed rooms explain that only their last
    file activity is shown. Manual refresh remains possible.
25. **Preserve activity as a room closes.** The room-status transition no longer resets its
    confirmed files or current search. A failed final read therefore cannot erase the list.
26. **Retain files through empty stale responses.** Both unavailable and stale empty snapshots
    keep the last confirmed entries. A healthy confirmed empty snapshot can still clear them.
27. **Reuse unchanged transfer data.** Equal file records and snapshots retain their existing
    object references; memoized grouping/filtering avoids rebuilding unchanged list data.
28. **Isolate a damaged process snapshot.** Status files are read independently. One broken
    FL process file preserves its own confirmed rows while healthy processes continue updating.
29. **Reject duplicate transfer identities.** A malformed snapshot cannot supply duplicate
    row keys, avoiding ambiguous file rows and React identity collisions.
30. **Reject invalid snapshot times.** Negative outer timestamps are refused alongside the
    existing per-file checks; the last confirmed status remains available.
31. **Remove disguised filename direction controls.** Additional invisible bidi characters
    are stripped from displayed sample names. The original file itself is not renamed.
32. **Keep folder errors with their room.** A delayed samples-folder error is only displayed
    if it still belongs to the current room, preventing a late failure appearing in another session.
33. **Use search shortcuts predictably.** Ctrl+F/Meta+F inside Files focuses its search.
    Escape first clears the query, then closes Files on a subsequent press.
34. **Respect newer focus after a delayed paste.** A clipboard response is ignored if the
    user has moved to another field/control. It cannot overwrite the draft or pull focus back.
35. **Respect text composition.** Enter during IME composition cannot submit the room;
    Escape cannot collapse setup, and picker keys cannot select an installation prematurely.
36. **Stop the picker moving the document.** Keyboard previews adjust only its option
    list's scroll position. Selection restores trigger focus without scrolling the app.
37. **Keep picker pointer targets still.** Its entrance now fades without vertical travel,
    reducing movement beneath the pointer while selecting an FL family.
38. **Measure real participant overflow.** Scroll for everyone and the scroll-region tab
    stop appear only when names actually exceed the available height, rather than by headcount.
39. **Give narrow participant names room.** Below 340px, the people list uses one column
    instead of squeezing two names and their FL details side by side.
40. **Announce restored work as busy.** The room controls include main-process busy state in
    aria-busy, matching the already-disabled actions when the renderer reconnects to pending work.
41. **Keep general errors visible with Files open.** Action/diagnostic errors sit outside
    the hidden narrow room card and span the wide layout instead of overlapping one column.
42. **Dismiss errors and return to useful focus.** Dismiss message clears the general error
    and returns focus to Files, the room heading, setup name or Get started as appropriate.
43. **Reserve submit-button height.** A 56px minimum reduces height shifts as the normal
    compact-width action changes to a longer busy label.

## Verification

- Production TypeScript/Vite build passed, 383 modules. Final assets:
  `index-mgQ28rMs.css` and `index-BhIURrNK.js`.
- **107 desktop unit tests passed.** New regressions include malformed/duplicate status,
  independent process recovery, search normalization, byte/GB/TB labels and snapshot reuse.
  A preexisting mock was updated to supply the primary-display API used by actual startup.
- **106 room UI checks passed**, including delayed paste focus, input composition, picker
  document-scroll stability, measured participant overflow, narrow error visibility and busy ARIA.
- **29 Files UI checks passed**, including all four filters, search/clear/no-match reset,
  keyboard shortcuts, accessible progress, manual/automatic read coalescing, quiet automatic
  refresh, 2/4/8/15-second backoff, hidden views, missing API, stale responses, room closure and
  forced colors. Browser suites reported no renderer errors.
- Browser layouts cover 280-1200px. Screenshots were inspected for wide Files, 280px Files,
  320px participants, narrow/wide error messages, forced colors and native zoom.
- Real Electron window tests passed: 400x354 initial, 400x777 setup, 400x931 open picker;
  all retained y=24. Ten Create/Join switches kept exact native bounds and immediate typing.
- Real Electron Files tests passed: lateral alignment, no transient collapse, focus/width
  restoration and 200% zoom. Renderer recovery passed with two automatic restarts followed
  by bounded manual recovery, retaining bridge state in an isolated test profile.
- Contrast checker results: muted sidebar text **5.07:1**, filter selection **8.44:1**,
  sidebar action text **7.94:1**, search placeholder **5.74:1**, error text **7.38:1**,
  blue focus against sidebar **5.36:1**. Other retained pairs have prior-release evidence.

Logs: `test/latest-*-0218.log`. Versioned screenshots/results:
`test/evidence/0.2.18`. Browser room/file data is supplied by explicit test fixtures.
The unpacked application and complete portable executable both passed startup checks.
Packaged main/preload/bridge/status-reader files and built JS/CSS match the local sources.
Native hashes, all nine embedded icon sizes, running window icons, installed supported
FL24/25/26 discovery, production CSP and diagnostics metadata passed. Neither test allocated
a room or launched FL. An initial icon-check process hit a buffer allocation error; the clean
rerun passed without changing application source.

The first portable attempts could not fully extract because C: had about 75 MiB free. The
partially extracted Electron executable was only 39,343,672 bytes rather than 249,866,752.
The archive integrity test passed. Repeating startup with TEMP/TMP pointed to a fresh D: folder
for that one test process passed in about eight seconds. Global Windows temp settings and the
user's existing Rooms/FL windows were not changed. Attempted failed-extraction cleanup was
blocked by automatic policy review and was not performed. This is a local disk-space limit,
not a passed C:-extraction result; leave at least 600 MB available for temporary extraction.

The user FL24 process, PID **[local PID omitted]**, retained its original start time
**2026-10-27 08:20:55.9275592 -04:00** through verification.

## Build

`release/flim-rooms-0.2.18-win64.exe`, **103,548,596 bytes**. Unpacked app: **384,884,699 bytes**.

SHA-256: `bade73564bb5c5faa9b4b44ab05eea0c4a0e38fd820faf8043b3ef494a8f2873`

Launcher: `4157b725f0b51a9d5b506177ca1708016fc27c3bc53937c73ab0913d22ba85e2`

Engine: `94de8224c448bf3e3dddf4a897d6a1ffd5a652aa90b8f3e7fa3e016d4c67209a`

Native payload is unchanged from 0.2.16/0.2.17. A machine-readable record is saved at
`../dev/compatibility/desktop-0.2.18-2026-10-27.json`.

## Limits

This update does not change or freshly test native FL synchronization, physical MIDI or a
two-computer session. Installed discovery remains covered for FL24 24.2.2.4597, FL25
25.2.5.5319 and FL26 26.1.7.5654. Short family labels do not certify every patch; exact
fingerprints remain required. Older-version fallback still shares notes/basic controls while
unsupported sounds and playlist features remain unavailable. UI room/file fixtures do not
prove live network throughput. No GitHub push or Cloudflare deployment occurred.

## Antislop delivery gate

- R-02 PASS: the changed user-facing TSX/utilities contain no em dash; source scan returned no matches.
- R-03 PASS: room/Files suites cover 280-1200px without horizontal overflow; native 200% zoom passed.
- R-17 PASS: counters derive from actual snapshot items; test names and activity stay inside fixtures.
- R-18 PASS: no profile photos, endorsements, fictitious invite slots or production fake activity were added.
- R-23 PASS: the user authorized UI refinements; existing equipment artwork and linked-computer logo were reused.
- R-24 PASS: Files, Back, Hide files, Open room setup and Show all files reach their implemented states.
- R-25 PASS: the checker measured 5.07:1/8.44:1/7.94:1/5.74:1/7.38:1 for changed/retained text on the new controls and 5.36:1 for focus; forced colors passed.
- R-26 PASS: named UI checks exercise room creation/join, discovery, picker, paste/copy, logs, Files, filters, refresh, folder, dismiss and recovery actions.
- R-27 PASS: initial/loading/empty/no-match/unavailable/stale/ended states have distinct messages; fixtures exercised them.
- R-28 PASS: no FAQ or accordion filler was added.
- R-32 PASS: keyboard filter navigation, search shortcuts, Escape, IME guards and focus restoration passed.
- R-33 PASS: shipped features are ordinary TSX/CSS/CJS source; no runtime source-rewriting script is included.
- R-34 PASS: the established light theme is retained; forced-colors selection, keyboard focus and disabled states passed.
- R-35 PASS: production build, browser interaction, native sizing/zoom and isolated crash recovery passed; executable verification is recorded with the build fingerprint.
- R-36 PASS: no universal-patch, zero-bug, live throughput or unmeasured latency claims are made.
- R-37 PASS: DESIGN.md records the supplied direction and purposes, ENERGY 1 / RHYTHM 1 / MOTION 2.
- R-38 PASS: status failures and missing APIs are explicit; a failed first read cannot masquerade as confirmed empty data.
- R-01 PASS: retained artwork contrast scrim serves its caption; no new decorative gradient was added.
- R-04 PASS: existing chevrons indicate sidebar/picker direction; no unrelated feature icons were added.
- R-06 PASS: General Sans and prominent room digits preserve the equipment-card identity and readable invitations.
- R-07 PASS: no patterned, blueprint, dotted or grid background was added.
- R-08 PASS: existing Back/sidebar/FL-opening arrows retain actual navigation meanings; new actions use text.
- R-09 PASS: no promotional badge was added; room status labels reflect the actual session state.
- R-10 PASS: the app/sidebar use opaque surfaces without new glass layers.
- R-12 PASS: the sidebar has no floating shadow; the retained picker elevation separates its option sheet.
- R-13 PASS: no glow or perpetual status animation was added.
- R-14 PASS: the view contains real room/file data rather than repeated marketing feature cards.
- R-19 PASS: picker entry fades in place; sidebar entry shows lateral placement; reduced motion disables transitions.
- R-22 PASS: approved computers/keyboard artwork remains connected to the actual collaboration task.
- Dials PASS: compact neutral controls, stable target positions and brief transitions match ENERGY 1 / RHYTHM 1 / MOTION 2.
- Focal point PASS: Get started, the current submit action, room number and file search each lead their own state.
- Whitespace PASS: native 400/680px sizing follows content; search/filter controls serve navigation rather than filling empty space.
- Accent PASS: blue continues to identify interaction and focus, rather than covering the interface.
- Identity PASS: the equipment artwork/logo, General Sans and numbered invitation remain specific to flim.
- Design Read PASS: DESIGN.md connects compact lateral Files, padded picker choices and outline-free fields to the user's references.
- C-1 PASS: DESIGN.md records the reasons for search, filtering, grouping, spacing, type, motion and recovery messages.
- C-2 PASS: signup, chat, voice and reference-only controls were not introduced.
- C-3 PASS: every new control serves finding files, interpreting their state, refreshing status or recovering from an error.
- C-4 PASS: browser fixtures cover interruptions, delayed responses, composition, hidden/closed views and narrow/zoomed layouts.
- C-5 PASS: unit, UI, native-window and package evidence are identified separately from live FL/MIDI/network evidence.
- R-05 PASS: layout follows actual room and file tasks, with no generic dashboard template or statistic tiles.
- R-11 PASS: the existing bounded card/field/button corners remain, without new decorative rings or universal pills.
- R-15 PASS: Get started retains the user's requested label; new controls name the operation they perform.
- R-16 PASS: state messages describe the next useful action without promotional claims.
- R-20 PASS: Files is built around finding an actual sample and understanding its status.
- R-21 PASS: refinements retain the user's light equipment-card direction without imposing a different theme.
- R-29 PASS: neutral surfaces with the established blue focus/selection accent keep the palette bounded.
- R-30 PASS: supplied references inform spacing and component treatment; account/chat UI was not cloned.
- R-31 PASS: major visual choices have purposes recorded in DESIGN.md.

UI/human/layout supplements PASS: measured text contrast, visible keyboard focus and selection,
44px targets, narrow participant reflow, large-name wrapping, no horizontal leakage, meaningful
data states and 200% native zoom. This is a Windows desktop app; a mobile virtual keyboard is
not a supported runtime. Reduced-motion and forced-colors settings were exercised.

## Interaction evidence

- Get started/Back: open/collapse setup with focus restoration; Create/Join: stable field geometry and immediate input.
- FL picker/details: open, preview/select by keyboard and pointer, Escape/Tab/outside dismiss, disclose build/folder.
- Check again/Choose folder: discovery success/failure, explicit folder selection and canceled selection preserve user input.
- Name/room fields: required validation, eight-digit parsing, paste success/failure/late response and IME composition.
- Create/Join/Open FL: fixture success and failed launch remain actionable; restored pending work disables duplicate actions.
- Copy/Refresh people/Retry status/Return to start: clipboard and status success/failure, fresh/ended state and setup restoration.
- Files arrow/Hide files/Open room setup: pre-room and in-room reveal, lateral alignment, Escape and focus restoration.
- Search/Clear/All/Pending/Attention/Ready/Show all: normalized matches, four state filters, no-match reset and keyboard selection.
- Refresh files: successful/failed/manual/automatic/in-flight/hidden/ended status paths; no extra transfer command is sent.
- Open samples folder: fixture success and visible failure; Open test logs: visible pending/error; Dismiss: clears and restores useful focus.
- Renderer recovery: two actual isolated Electron crashes reload controls; repeated failure reaches manual recovery and preserves bridge state.

The corresponding check names and empty error arrays are saved in `ui-results.json` and
`files/ui.json`; these fixture actions do not open the user's FL project or allocate test rooms.
