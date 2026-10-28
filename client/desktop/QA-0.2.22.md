# flim Rooms 0.2.22

35 additional changes beyond 0.2.21. Everything remains local. The received-files folder
picker from 0.2.21 is retained; this list describes this round's changes only.

## What actually changed

1. **Reserve settings scratch files safely.** Saving no longer overwrites the predictable
   preferences.json.tmp path. Each save exclusively creates a unique owned file. A foreign
   scratch file is left intact, and a failed reservation cannot delete it.
2. **Flush settings before replacing the saved copy.** A complete write is flushed and closed
   before replacement. If flushing fails, the previous preferences remain intact and the owned
   temporary file is removed. This improves save durability without a crash-proof guarantee.
3. **Recover from brief Windows file locks.** Settings replacement retries EBUSY/EACCES/EPERM
   twice with short pauses. A transient scanner/sharing lock can clear; permanent failures
   remain bounded, preserve the saved copy and report failure.
4. **Make storage logs useful for diagnosis.** Settings-save and received-folder failures now
   include the operation and OS error code, such as ENOSPC. Private paths and raw filesystem
   error messages are not added to these records.
5. **Keep decomposed accents in participant names.** Names normalize to NFC before validation
   and sanitizing. A pasted decomposed Jose + accent becomes José in saved settings and the FL
   launch environment instead of silently losing the accent. Existing native name limits remain.
6. **Explain clipboard-copy failure.** A failed copy now produces an intentional bridge error
   explaining that the room number can be selected and copied manually. It no longer falls
   through main's generic unexpected-step error.
7. **Do not copy expired reservations.** Copy room number checks reservation expiry before
   touching the clipboard. An expired unused invitation cannot be presented as a usable invite.
8. **Retire expired unused rooms in the controls.** Refresh marks an unlaunched host reservation
   closed when its deadline has passed, without an unnecessary relay call. Launched or uncertain
   sessions are excluded from this local expiry decision.
9. **Back off repeated room-status failures.** Automatic checks wait 5, 10, 20, 40 and at most
   60 seconds after consecutive failures instead of repeatedly hitting an unavailable relay.
   Success resets the cadence; a new room clears old backoff. Manual retries retain their
   one-second burst guard, and backwards clock changes cannot freeze the failure cooldown.
10. **Honor relay Retry-After.** Valid seconds and HTTP dates on 429/503 responses pause both
    automatic and forced checks. A 503 with a requested pause skips the immediate service retry.
    Delays are bounded to five minutes, and malformed headers cannot create an unlimited wait.
11. **Recover setup after a controls reload during work.** If main still owns an operation,
    reloaded controls poll its state until it finishes, even when no room exists yet. Previously
    a busy folder/discovery action could leave the no-room form disabled indefinitely. Recovery
    restores main's final selection and does not start another FL instance.
12. **Refresh people when returning to the app.** Window focus checks current room status as
    visibility/online resumption already did. Returning from FL can update the list promptly;
    existing request coalescing and backend rate/backoff guards still apply.
13. **Find files from the room controls.** Ctrl+F or Meta+F opens the Files sidebar, focuses
    Find a file and selects its current query. The shortcut works before the sidebar has been
    mounted, and requires a room with file-status support.
14. **Leave modified find shortcuts alone.** Shift/Alt-modified keys, IME composition and
    already-handled events do not trigger Files search. The old sidebar handler intercepted
    Ctrl+Shift+F too, taking focus from the user's current control.
15. **Separate incoming and outgoing activity.** Both / To me / From me combines with filename
    search and status filters. Counts match the selected direction. Direction survives hiding
    Files for the current room; Show all files clears direction, query and status together.
16. **Recover a status-file replacement immediately.** If a native status file changes during
    inspection/read, the reader makes one fresh, fully verified attempt. Frequent atomic native
    publications need not cause a stale-list pause. A continuously changing file gets only two
    attempts and keeps the previous confirmed data.
17. **Reject contradictory transfer direction.** A downloading record cannot claim to be an
    upload, or an uploading record a download. Corrupt status data is rejected and confirmed
    activity is retained, rather than displaying the wrong incoming/outgoing activity.
18. **Give invisible-only sample names a useful label.** Status display names trim surrounding
    whitespace and remove invisible directional/zero-width formatting characters. An empty
    result displays Sample file instead of a blank row. Ordinary Unicode names remain visible.
19. **Keep unfinished activity in the desktop's bounded list.** Aggregating multiple FL process
    snapshots now puts unfinished records ahead of ready files before applying the 96-row cap.
    A newer block of completed samples cannot crowd out an older waiting transfer.
20. **Disclose limited activity coverage.** Snapshot metadata now includes the known total and
    whether rows/processes were capped. Files explains that the displayed list is limited and
    that sharing continues for entries outside it. This is a display bound, not a transfer quota.
21. **Keep coverage warnings through an outage.** When an empty failed read retains confirmed
    rows, it also retains their known total and partial-list flag. Reconciliation updates a
    coverage-only change even when every row is unchanged, preventing a stale or missing warning.
22. **Log file-status health transitions.** Local diagnostics record available/unavailable once
    per transition. Repeated failed polls do not flood the log; recovery is recorded. Events
    contain neither sample names, source paths nor file content.
23. **Publish ready files with complete progress.** Native ff_begin now initializes ready
    records at their full byte count. A cached ready file no longer briefly publishes zero
    transferred bytes before ff_finish corrects it.
24. **Preserve pending work in native activity retention.** At 96 native records, eviction
    chooses ready entries before older waiting/active entries. When every retained entry is
    unfinished, a new ready record is omitted instead of evicting pending activity. This changes
    status retention, not the native transfer queue or actual file delivery.
25. **Handle empty native basenames.** A path ending at a separator now emits Sample file.
    Previously conversion of an empty basename succeeded with an empty string, causing the
    desktop to reject the whole process's status snapshot.
26. **Clean failed native status publication.** If the final MoveFileEx replacement fails, its
    temporary .part file is removed. The last confirmed status file remains intact instead of
    leaving an incomplete scratch publication behind.
27. **Coalesce Open test logs in main.** Simultaneous requests, including requests across
    renderer remounts, share one flush/open operation. Success and failure both release the
    shared operation, so later attempts work normally.
28. **Show existing controls on a second launch.** The single-instance handler now explicitly
    shows the existing window, in addition to restoring a minimized window and focusing it.
    Launching flim again can reveal hidden controls instead of focusing an invisible window.
29. **Show the actual pending folder action.** Choose folder displays Choosing folder, reset
    displays Restoring default, and opening displays Opening samples. The pending step is also
    announced while the path remains visible, rather than merely disabling static labels.
30. **Restore focus after folder reset.** When Use default disappears after success, keyboard
    focus moves to Choose folder if the user has not moved elsewhere. Existing action controls
    regain focus after completion; unrelated user focus is not stolen.
31. **Disable unsupported folder actions individually.** A partial/older bridge exposing folder
    reads/opening but not choosing/resetting no longer offers buttons that call missing methods.
    Supported opening remains usable, with guards in both rendering and the action handler.
32. **Recheck folder controls when setup unlocks.** Folder state is fetched again when the
    parent lock changes. A canChange=false result read during main's busy operation cannot
    leave Choose folder disabled after setup becomes available.
33. **Unify mouse and keyboard FL-picker previews.** Moving the mouse over an option updates
    its active descendant. Arrow navigation and Enter continue from that preview, preventing
    the keyboard from selecting a different option than the one highlighted by the mouse.
34. **Recover a closed room's never-confirmed file list.** An empty failed read no longer
    counts as a confirmed snapshot. Reopening Files after the room closes can retrieve the
    first confirmed list once, while already confirmed closed-room activity remains unpolled.
35. **Identify the end of the file list.** Scrolling to the bottom changes the hint to End of
    file list. The hint belongs to the current result set, so a changed search/list does not
    reuse an old bottom claim. Scroll events update it without a synchronous layout-state loop.

## Validation

- Production TypeScript/Vite build passes.
- 177 backend tests pass, including 16 new regressions beyond 0.2.21. Coverage includes
  real reserved settings files, write/flush/replacement failures, bounded Windows-lock retries,
  foreign scratch preservation, Retry-After/backoff/reset/clock handling, expired invitations,
  replacement-during-read recovery, continuous mutation bounds, capped-list priority and health logs.
- 127 room/UI checks pass, including restored busy/no-room setup, mouse-to-keyboard picker
  navigation and focus resumption. Nine FL24/25/26 mixed-build preflight combinations use
  simulated native/relay replies; they are desktop decision checks, not live collaboration proof.
- 68 Files/UI checks pass, including room-wide find, modified shortcuts, direction/filter/search
  combinations, remembered direction, partial-list warnings, pending folder feedback, focus after
  reset, busy unlock, missing-method guards, closed-room first confirmation and end-of-list hints.
- 30 native harness programs pass. The added file_status harness directly verifies initial ready
  progress, empty basenames, pending retention and failed replacement cleanup. Existing note/CRDT,
  plugin, compatibility, startup, project-protection, queues and inbound validation harnesses pass.
- Native sample loopback passes: one upload, 13 downloads across storage/network/verification
  failure scenarios, real inbound preset rewriting, verified cache reuse and session cancellation.
  Source audio remains untouched. The broader bounded transfer/streaming loopback suite passes.
- Real Electron folder IPC, persisted choice, redirected Documents, concurrent folder/log opening,
  showing hidden controls on second launch, retained form text and 200% zoom pass. Actual window
  geometry/sidebar fitting and bounded renderer-crash recovery pass in isolated test profiles.
- Unpacked and portable executable startup pass. Packaged modules match the source, including
  atomic-settings and single-flight; native payload hashes match the rebuilt binaries. Icons,
  local diagnostics, production isolation, installed FL24/25/26 discovery and empty initial
  room state are verified. Test extraction uses process-local TEMP/TMP on D:.
- Screenshots of the new direction/coverage controls, list end and actual 200% folder UI were
  inspected. Keyboard, forced colors and narrow/intermediate/wide layouts are covered by the
  suites. Existing reduced-motion behavior remains. No new assets or animation were introduced.
- Contrast checks pass for existing/new text, notice and focus pairs: 5.07:1 through 13.37:1;
  focus is 5.36:1 against the sidebar and 6.07:1 against white. Actions retain 44px targets.

Evidence: test/evidence/0.2.22 and test/latest-*-0222.log. Build identity and packaged-startup
results are recorded in client/dev/compatibility/desktop-0.2.22-2026-10-28.json.

Portable executable: 103,554,566 bytes. Unpacked application: 384,917,397 bytes.
SHA-256: `ea35c4a59e6538a1fcd39121f4672447cfc3a0a7cb6183e53d0fd6977af5962d`.
Native engine SHA-256: `599daa6ea314c13565d09c943a3cf2cee9defdbf3dee167150332558ea327a2b`.
The existing user FL24 process, PID [local PID omitted], retained its original start time of
2026-10-27T08:20:55.927559-04:00 at the end of validation.

## Scope and limits

Use the complete release/flim-rooms-0.2.22-win64.exe on both computers to get the updated engine.
The same reviewed FL builds remain 24.2.2.4597, 25.2.5.5319 and 26.1.7.5654; no new FL profile
was added. This pass does not establish compatibility with every historical/future patch.
Older/newer fallback behavior and unavailable plugin/features retain their existing limits.

There was no fresh two-computer, physical MIDI keyboard, VST-version or live drag/drop session.
Automated, synthetic and loopback checks are distinguished above from actual FL collaboration.
The user's existing FL process and project were not closed or changed. No GitHub commit/push,
Cloudflare deployment or account/billing setting change occurred.

## antislop delivery gate: PASS

ENERGY 1 / RHYTHM 1 / MOTION 2 retained from DESIGN.md. No new artwork, fonts, palette, menus,
signups, decorative lines/rings, field outlines or animation are added.

- R-02 PASS: added product text contains no em dashes; pending/recovery labels name real actions.
- R-03 PASS: Files suites cover 280/375/400/599/680/756/1200 widths with no horizontal overflow.
- R-17 PASS: file totals, cap flags and direction counts come from snapshots; no promotional stats.
- R-18 PASS: no testimonials or fabricated participants are added to the app.
- R-23 PASS: no new assets; direction controls fit the existing requested Files navigation.
- R-24 PASS: all added actions/shortcuts/filter/reset/retry paths have real handlers and tests.
- R-25 PASS: contrast.txt records text/background and focus pairs; minimum text ratio is 5.07:1.
- R-26 PASS: browser, native harness and real Electron functionality checks were executed.
- R-27 PASS: empty, pending, unavailable, partial, missing-method and recovery states are tested.
- R-28 PASS: no FAQ, marketing or filler sections are added to the product.
- R-32 PASS: keyboard search, picker continuity, reset focus and forced-colors checks pass.
- R-33 PASS: ordinary source modules/CSS implement the UI; no runtime source replacement.
- R-34 PASS: existing light and forced-colors states retained; no unsupported theme control.
- R-35 PASS: production build, regression suites, real windows and renderer recovery executed.
- R-36 PASS: no zero-bug, latency benchmark or universal-FL assertion; test boundaries are stated.
- R-37 PASS: DESIGN.md records the equipment-card direction and reasons for the new controls.
- R-38 PASS: runtime status data is real; simulated rooms/files are confined to fixtures.
- R-01 PASS: no new gradients; existing artwork treatment retained.
- R-04 PASS: no new icons/icon library; controls use text that describes their filter function.
- R-06 PASS: established General Sans remains readable and tied to the equipment-card identity.
- R-07 PASS: no decorative backgrounds, grids or dots.
- R-08 PASS: existing Files arrow continues to identify lateral navigation; no added arrows.
- R-09 PASS: no promotional pills or badges.
- R-10 PASS: no glass surfaces.
- R-12 PASS: no added shadows; existing picker elevation retained.
- R-13 PASS: no glow.
- R-14 PASS: no generic feature-card grid inside the application.
- R-19 PASS: no new motion; existing brief reveal and reduced-motion rules retained.
- R-22 PASS: existing computer/keyboard art retains its collaboration purpose.
- Liveliness/dials PASS: explicit ENERGY 1 / RHYTHM 1 / MOTION 2 remain consistent.
- Liveliness/focal point PASS: room creation/joining remains primary; Files is a task-specific side panel.
- Liveliness/whitespace PASS: direction controls sit beside list filters without a new nested card.
- Liveliness/accent PASS: existing blue selected labels/focus remain the interaction accent.
- Liveliness/identity PASS: computers/keyboard artwork, logo and type remain specific to flim.
- Liveliness/Design Read PASS: existing direction read before edits and extended with their reasons.
- C-1 PASS: direction selection, coverage warning and focus feedback each serve a documented task.
- C-2 PASS: pending/error/success/disabled action states are functional and regression-tested.
- C-3 PASS: all new product controls operate actual sample activity or received-file storage.
- C-4 PASS: failures, delayed completion, controls reload, narrow widths, keyboard and zoom tested.
- C-5 PASS: evidence and unperformed live FL tests are distinguished explicitly.
- R-05 PASS: compact room/sidebar composition retained; no website template.
- R-11 PASS: existing component radius hierarchy retained; no pill redesign.
- R-15 PASS: Get started remains the user's chosen label; new labels describe actual tasks.
- R-16 PASS: concrete product text without marketing/speed claims.
- R-20 PASS: music-equipment identity retained.
- R-21 PASS: established light desktop theme retained.
- R-29 PASS: existing neutrals and blue accent retained.
- R-30 PASS: existing app refined without cloning a different product's shell.
- R-31 PASS: design reasons for direction, warning, focus and keyboard changes are documented.

Human/layout supplements PASS: measured text/focus contrast, non-color pressed/status semantics,
44px actions, keyboard operation, intermediate reflow and 200% native zoom are verified. This is
a Windows desktop application; mobile virtual-keyboard behavior is outside the shipped surface.
