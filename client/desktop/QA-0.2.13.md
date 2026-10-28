# flim Rooms 0.2.13

This update contains **40 fixes and refinements** to the desktop interface and its local bridge.
They include UI improvements and error handling, not 40 independently discovered crashes.
Native FL files, compatibility profiles and the production Cloudflare relay are unchanged from
0.2.12. No GitHub write or relay deployment is part of this update.

Use the complete [portable executable](release/flim-rooms-0.2.13-win64.exe). Close the previous
Rooms app first because only one Rooms instance runs per user-data profile. Use the new app for
the next session; existing FL projects were not replaced or interrupted.

## The 40 changes

1. **Visible installation loading.** Setup now says when it is checking FL Studio and disables
   incomplete controls. A slow lookup no longer looks like an empty, ready-to-use form.
2. **Retry failed setup.** A setup-read failure has its own explanation and Retry setup button.
   It no longer incorrectly implies that the native launcher is missing; local logs remain reachable.
3. **Catch unusable names early.** Blank, whitespace-only and punctuation-only names are rejected
   beside the input before a room is allocated or FL is opened.
4. **Validate all eight room digits.** Incomplete room numbers receive an inline error before
   contacting the relay. Spaces and hyphens remain accepted, with mid-number caret editing preserved.
5. **Connect errors to their fields.** Invalid inputs expose their error through accessible
   descriptions and move focus to the field needing correction. Operation errors receive focus
   after controls are re-enabled.
6. **Show input limits.** The name displays its 24-character limit and room numbers show how many
   of eight digits have been entered, reducing trial and error.
7. **Clear obsolete errors during correction.** Changing a name, number, FL selection or room
   action clears the relevant old failure instead of leaving an outdated message underneath it.
8. **Paste room numbers directly.** Paste room number reads the clipboard only when pressed,
   validates the content in the bridge, formats the result, and returns focus to the number field.
   Invalid clipboard text is neither displayed nor logged.
9. **Protect newer typing from late paste.** If clipboard work finishes after typing or switching
   modes, its old result is ignored. A paste failure also preserves the number already entered.
10. **Browse from the selected FL folder.** Choose folder opens at the currently selected copy
    and uses a clear Use this folder button. Canceling retains the selection.
11. **Rescan after installing or updating FL.** Check again discovers installations and rechecks
    their current files without restarting Rooms. It does not enable unverified builds.
12. **Remove unreadable installation entries.** A rescan replaces its installation list atomically
    and drops copies it cannot inspect. If the selected copy disappears, a notice explains the
    fallback selection. Explicitly selected unsupported copies remain visible with their reason.
13. **Preserve input during scans.** A typed name and selected folder survive a rescan; pending
    scans disable duplicate actions. Inspection still runs with a maximum of three files at once.
14. **Keep copying independent of networking.** Copy invitation no longer uses the global room
    action lock or invalidates a pending people refresh. Navigation stays available during copy.
15. **Scope copy feedback to its room visit.** A delayed clipboard response cannot report success
    after leaving and rejoining, even when the new room has the same number. Refreshing people no
    longer clears a valid copy message.
16. **Improve copy success and failure feedback.** Success has a screen-reader announcement. A
    clipboard error leaves room controls usable and explains that the room number can be selected
    and copied manually; selecting it selects the entire number.
17. **React to offline status immediately.** A connection change updates the notice and disables
    new network actions without waiting for the next poll. Confirmed people remain visible,
    local invitation copy remains available, and reconnecting resumes status checks.
18. **Retry failed active-room status sooner.** After an error, the UI schedules an attempt after
    five seconds instead of waiting the ordinary thirty-second active interval. The bridge's
    automatic five/ten-second lookup minimums still limit actual requests.
19. **Make manual refresh useful.** Refresh people and Retry status can bypass the automatic
    status cache after one second. They share an existing lookup and cannot create overlapping
    request bursts.
20. **Invalidate stale status before newer actions.** Starting a newer bridge action aborts and
    invalidates an older status lookup. A late room-unavailable reply cannot mark a successfully
    launched current room closed.
21. **Show when people were actually confirmed.** Freshness uses the last successful response,
    not an attempted or cached lookup. Errors and offline mode explicitly identify the list as
    the last confirmed people. This is room status, not a MIDI/project-sync certification.
22. **Keep collapsed room status useful.** The artwork caption shows room state and people count.
    The Windows title reflects room/offline status, making the app easier to identify with Alt+Tab.
23. **Add a direct Menu shortcut.** Menu opens room setup or returns to the current room, dismisses
    itself and focuses the appropriate content without leaving the session.
24. **Expose the app version in Menu.** The visible Rooms version makes it easier to tell whether
    both computers are testing the same build without locating the executable again.
25. **Focus a reopened room correctly.** Reopening a collapsed room focuses its heading. Ordinary
    people updates do not repeatedly move keyboard focus away from the current control.
26. **Keep full rooms compact.** The participant list has a 216px scroll area, a Scroll for everyone
    hint and a keyboard-focusable region for longer lists. Counts have descriptive accessible labels.
27. **Stabilize participant order.** The host remains first and member names use a stable natural
    sort, so Friend 2 precedes Friend 10 and relay ordering changes do not shuffle the UI.
28. **Handle international display names more carefully.** Names use bidirectional isolation, and
    initials use a whole grapheme, preserving a letter and its accent rather than splitting it.
    Long names wrap while their initial and role remain aligned.
29. **Simplify participant version labels.** Rows say FL Studio 24, 25 or 26. Exact builds remain
    available through accessible labels and native tooltips for troubleshooting.
30. **Explain mixed-version limitations where they matter.** Rooms with different FL builds show
    a short notes/basic-controls explanation and identify potentially unavailable sounds and
    playlist features. There is no new approval dialog.
31. **Remove obsolete warnings after a room ends.** Closed rooms hide the old uncertain-launch
    warning and offer no launch retry for a dead room. Returning to start remains available.
32. **Contain Menu in short windows.** The tools panel now scrolls within the available height,
    so a long diagnostic error cannot push its actions outside a small window.
33. **Make helper text easier to read.** Footnotes, status metadata and participant FL/role labels
    now use 12px text instead of several 10px treatments, retaining verified text contrast.
34. **Use compact spacing deliberately.** Field gaps and section margins are tighter, and the name
    hint remains available to screen readers without adding another visible row. This offsets the
    larger text while retaining 44px targets and the component-fitting native window.
35. **Guarantee artwork text contrast.** Labels and room captions have solid charcoal backing.
    Their readability no longer depends on whichever image pixels happen to lie behind the text.
36. **Clarify selection and focus.** The selected Create/Join action has a visible control boundary.
    The hero button has a light/dark focus treatment that remains visible over the artwork.
37. **Support Windows high contrast.** Forced-colors styling preserves button/input boundaries,
    selected action, disabled text and focus instead of depending on subtle background shades.
38. **Improve narrow and touch layouts.** Layouts reflow down to 280px without horizontal overflow;
    room numbers scale, small field headings can wrap, and controls have visible pressed feedback.
39. **Explain unusually long operations.** After eight seconds in a pending step, an inline notice
    explains that flim is still waiting. Duplicate room/scan actions stay disabled; there is no
    fabricated percentage or unsafe automatic relaunch.
40. **Recover from an interface rendering error.** A focused recovery screen records the error,
    offers local logs and reloads the renderer. It preserves the main-process room and does not
    close or restart FL Studio. The recovery screen also fits its own content.

## Verification

Final artifact and source hashes are in
[the verification record](../dev/compatibility/desktop-0.2.13-2026-10-27.json).

| Check | Result |
|---|---|
| Desktop bridge, diagnostics, installation picker and geometry tests | 85 pass |
| Renderer and actual Electron UI checks | 70 pass |
| TypeScript/Vite production compilation and portable packaging | Pass |
| Unpacked app, packaged source/native hashes, all nine icon sizes and local diagnostics | Pass |
| Portable extraction/startup, FL24/25/26 discovery and clean initial room state | Pass |
| Actual native-window growth/collapse, work-area limits and 200% zoom | Pass |
| Native launcher/engine and profiles against 0.2.12 | Unchanged |
| Git whitespace check | Pass |

The default window measures **472 × 381** closed and **472 × 662** with setup open at 100%,
including the Windows frame. The expanded 0.2.12 measurement was 472 × 676. The closed view
grew one pixel as helper text became easier to read; setup is 14px shorter in the measured
default state. Extra notices/details grow the window as needed and stay scrollable at work-area
limits. At 200% the checked work area capped bounds at 928 × 1008 without horizontal overflow.

The portable executable is **103,615,933 bytes**, SHA256
`3457f914dcf1f3400be9009e7f69978772ab26fd136ca3916f081f57472a7f00`.

Normal flows have no unexpected renderer
errors. The deliberately malformed-state recovery test separately verifies that an error is
recorded and can be recovered; that expected fault is not counted as a clean renderer page.

The UI inventory covers Get started, Back/Escape, Create/Join and their keyboard controls, name,
FL selector, Choose folder/cancel, Check again, installation details, room number, clipboard
paste/error/delay, create/join submission, launch retry, invitation copy/error/delay, participant
scrolling, refresh/retry, return to start, Menu, existing-screen shortcuts, log opening/failure,
and interface reload. Creation, clipboard and error scenarios use test-only fixtures. The actual
Electron app also discovers and rescans installed FL24/25/26 through the real bridge without
opening FL or allocating a room. Tests cover 280/320/375/600/810/1200px, short windows, forced
colors, reduced motion, keyboard focus and real native-window fitting at 200% zoom.

During test development, a repeat-initialize regression changed the selected copy; preserving
the previous path corrected it. The preload whitelist test was extended for the two new narrow
APIs. UI runner corrections waited for animation-frame focus/default scrolling, selected an
unsupported copy through its folder route rather than a hidden dropdown option, scoped the
footnote selector, and used real Tab navigation for focus-visible. These were corrected before
the successful full sweep; they are not unresolved test failures.

Screenshots and the click-through inventory are in `test/evidence/0.2.13/`. The retained Framer
runtime still produces Vite's existing large-chunk advisory; no measured startup-speed claim is
made. Native FL payloads match 0.2.12 during package verification. The existing user FL24 process,
PID [local PID omitted] with start time 2026-10-27 08:20:55 -04:00, remained open and untouched.

## Runtime limits

This update starts, stops and injects into **no FL Studio process**. It has no new live native
project-sharing, hardware MIDI or audio-streaming test. Existing six-direction FL collaboration
and public CRDT results belong to 0.2.11 and remain separate evidence. No public room is created
by these UI/package checks.

Installed discovery and desktop preflight checks cover 24.2.2.4597, 25.2.5.5319 and 26.1.7.5654.
FL24 remains experimental, including its previously observed intermittent recovery-save stall.
Unknown patches still require mappings and runtime evidence. Older members' unavailable sounds
and playlist features remain unavailable. Real native faults and failed recovery can still stop
sharing to protect the local project. These UI changes do not establish compatibility with every
FL release or promise uninterrupted sessions.

## Design delivery gate

Mode: during, session override. Direction: the existing user-supplied equipment card and compact
room controls. ENERGY 1 / RHYTHM 1 / MOTION 2 remains unchanged. No visual assets were generated.

| Rule | Result and evidence |
|---|---|
| R-01 | PASS: retained artwork scrim and sparse accents; solid text backing has a legibility purpose in DESIGN.md. |
| R-02 | PASS: new authored UI/docs use plain copy without em dashes. |
| R-03 | PASS: no horizontal overflow at six widths from 280 to 1200px, short-window actions remain reachable. |
| R-04 | PASS: existing equipment logo and copy/navigation glyphs name actual actions; no decorative icon set added. |
| R-05 | PASS: one component and its room form; no dashboard or filler sections. |
| R-06 | PASS: General Sans is retained from the supplied component; increased metadata sizes serve readability. |
| R-07 | PASS: no decorative grid or pattern. |
| R-08 | PASS: existing Back and open-FL arrows retain their directional meaning; new controls use text. |
| R-09 | PASS: room state and count come from state; no promotional badges. |
| R-10 | PASS: no added glass/blur treatment. |
| R-11 | PASS: input, action, component and temporary menu keep distinct radii. |
| R-12 | PASS: only the existing temporary tools layer and hero action retain purposeful elevation. |
| R-13 | PASS: no decorative glow. |
| R-14 | PASS: no feature-card grid or template cards. |
| R-15 | PASS: user-requested Get started retained; new Paste, Check again and recovery actions describe their behavior. |
| R-16 | PASS: no invented performance/marketing claims in the UI. |
| R-17 | PASS: counts, build labels, limits and freshness derive from actual state; fixtures stay outside production. |
| R-18 | PASS: no testimonials or invented customers. |
| R-19 | PASS: short existing menu-state motion; reduced-motion checks disable it. |
| R-20 | PASS: numbered FL rooms, equipment identity and version context remain specific to this application. |
| R-21 | PASS: retained user-directed light component; no theme toggle introduced. |
| R-22 | PASS: retained linked computers/keyboard art reflects the requested music workflow. |
| R-23 | PASS: no asset creation; navigation refinements follow the user's explicit request. |
| R-24 | PASS: Menu shortcuts open existing setup/room content; logs open the local folder. |
| R-25 | PASS: white/charcoal 15.49:1; muted/white 5.74:1; menu text/hover 7.82:1; error text/background 7.38:1; focus/white 6.84:1. Gray control boundary/white is 3.54:1, satisfying non-text contrast. |
| R-26 | PASS: all controls have the behaviors in the recorded 70-check inventory, including new rescan and paste. |
| R-27 | PASS: loading, empty, validation, setup/clipboard/network/log failure, offline, ended, slow and recovery states exercised. |
| R-28 | PASS: no FAQ or unrelated filler. |
| R-29 | PASS: retained neutral surfaces and coral accent; no new palette. |
| R-30 | PASS: retained supplied component rather than another product's template. |
| R-31 | PASS: typography, text backing, list bounds, spacing, shortcuts and recovery reasons recorded in DESIGN.md. |
| R-32 | PASS: Tab/Shift+Tab, Enter, Space, Escape, arrows/Home/End, focus return and keyboard list scrolling verified. |
| R-33 | PASS: features implemented directly in source with patches, without a source-rewriting helper. |
| R-34 | PASS: retained theme and forced-colors state tested; no broken theme switch. |
| R-35 | PASS: production build, renderer click-through, actual Electron rescan/window checks and recorded screenshots. Package checks are tracked separately. |
| R-36 | PASS: no claim of every FL patch, verified MIDI, uninterrupted sessions or measured speedup. |
| R-37 | PASS: existing DESIGN.md and explicit user direction retained with the 1/1/2 Design Read. |
| R-38 | PASS: no fabricated business data or peers in production; fixture isolation is checked during packaging. |
| Liveliness | PASS: one focal component, structural spacing, sparse coral, retained equipment motif and purposeful menu transition. |
| C-1 | PASS: major visual decisions have written user/product reasons. |
| C-2 | PASS: full interactive inventory has implemented behavior. |
| C-3 | PASS: content is limited to entering, checking, navigating and troubleshooting a room. |
| C-4 | PASS: narrow/short/zoom/keyboard/high-contrast/error states verified. |
| C-5 | PASS: measured test results are separated from absent live FL/MIDI evidence. |

Delivery gate: PASS for the tested desktop interface. See runtime limits above.
