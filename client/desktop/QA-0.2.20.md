# flim Rooms 0.2.20

40 desktop reliability fixes and refinements beyond 0.2.19. Changes remain local.
The native FL launcher/engine, exact build profiles and Cloudflare/R2 deployment are unchanged.

## What actually changed

1. **Stop rereading unchanged sample snapshots.** Files compares file identity, size and write
   metadata before opening the body again. A regression case makes eight additional unchanged
   polls with zero additional file-body opens. Replaced files invalidate this cache, including
   same-size replacements that preserve their previous modification time.
2. **Handle short disk reads.** Reading a file is no longer assumed to return its entire content
   in one call. The reader assembles bounded chunks until complete, so an ordinary short read
   cannot turn a valid sample snapshot into an apparent failure.
3. **Check the file that was actually opened.** File identity is verified again after opening.
   If the native writer replaced the snapshot between directory inspection and open, the reader
   retains the previous confirmed activity and checks the replacement on the next poll.
4. **Reject changes during a read.** Size, identity and write metadata are checked after reading,
   against both the open handle and current path. Incomplete, growing or replaced data cannot
   become a new confirmed snapshot. This applies to saved settings reads too.
5. **Reject damaged sample text.** Status-file UTF-8 decoding is strict. Invalid bytes no longer
   silently become replacement characters in a sample name; confirmed files remain visible.
6. **Make overlapping file setup deterministic.** A generation number identifies the latest
   setup even when the same room object is reused. A slower earlier completion cannot replace
   the newer file-status directory.
7. **Ignore obsolete failed reads.** A read that fails after another room/setup takes ownership
   returns an empty detached result, rather than incorrectly marking the current room unavailable.
   It cannot overwrite the newer room's cache or pending read.
8. **Retain confirmed files separately for each FL process.** Previously, error recovery could
   restore only rows left in the combined 96-row list. Now, previously hidden rows can return if
   another process disappears while their process's newest snapshot is unreadable.
9. **Make bounded process selection stable and honest.** Status files are selected in numeric
   process-ID order instead of arbitrary directory order. More than 16 process files produces a
   partial/unavailable status instead of silently claiming complete information. This does not
   impose a room-duration or file-delivery limit.
10. **Validate sample-status process IDs.** Filenames and snapshot IDs must fit the Windows
    unsigned 32-bit PID range. Impossible process identities cannot enter displayed file activity.
11. **Decode relay text strictly.** Invalid UTF-8 in a successful room response is rejected
    before it becomes room credentials, names or status. The app does not silently repair it.
12. **Require an object from the relay.** A successful response containing null, an array, a
    string, a number or a boolean now produces a useful invalid-response error rather than
    leaking into later code and causing an unrelated failure.
13. **Identify empty relay responses.** A body stream that ends without any bytes now reports
    that the relay returned an empty response. It no longer appears as a generic JSON failure.
14. **Preserve response-validation errors during cleanup.** Failed stream cancellation or
    reader-lock release cannot replace the useful oversized/invalid-response explanation with
    an internal cleanup error. Response size bounds remain enabled.
15. **Explain malformed room data correctly.** Missing, malformed or noncanonical room numbers
    returned by the relay are described as incomplete relay information. The app no longer
    tells the user to fix their typed number when the bad number came from the server.
16. **Require a usable host reservation expiry.** Creating a room now requires a valid expiry
    as well as its credentials and path. Malformed host responses cannot disable the desktop's
    reservation-expiry protection by silently turning the expiry into null.
17. **Reject contradictory participant status.** A waiting reservation cannot contain connected
    people, and a room cannot advertise two leaders. Invalid replies preserve the last confirmed
    list instead of changing the app into an inconsistent room state. A transient empty active
    list remains readable; launching a member still requires exactly one active leader.
18. **Display peer names safely and consistently.** Control and directional-formatting characters
    are removed, accents are normalized, and truncation avoids cutting a valid character in half.
    A name made empty by cleaning gets the explicit Participant fallback.
19. **Log replacements at the same headcount.** A different participant or FL version now causes
    a room-status event even if the number of people stays the same. Merely reordering the same
    participants does not spam the log. Names and participant IDs stay out of the recorded fields.
20. **Throttle room checks from completion.** Waiting/active/manual intervals now start when the
    previous request finishes, including failed requests. A slow request can no longer consume
    its own delay and allow another request immediately afterward. Concurrent checks still share
    one request, and backwards clock changes remain recoverable.
21. **Show member preflight results immediately.** The authenticated status fetched before a
    member launch updates the people list, room state and freshness timestamp. The app no longer
    discards that confirmed information and makes an immediate redundant lookup for the UI.
22. **Distinguish intentional cancellation in logs.** Leaving or starting another action can
    deliberately cancel a people lookup. Diagnostics now records request_canceled instead of
    misclassifying that normal behavior as a network failure.
23. **Give relay timeouts their own explanation.** An exhausted request deadline reports that
    the relay took too long and records the timeout code. It is distinguishable from a connection
    failure, and it does not cause an extra status retry after the deadline.
24. **Validate installation versions as actual Windows version numbers.** Native inspection uses
    the same canonical four-part numeric parser as compatibility preflight. Leading-zero,
    truncated or overflowing components cannot enter the installation picker as verified data.
25. **Handle malformed launch confirmations consistently.** Null, arrays, primitive values and
    invalid/out-of-range PIDs produce the typed native_uncertain result. The desktop preserves
    its duplicate-launch protection and provides a useful explanation instead of a TypeError.
26. **Recheck expiry immediately before spawning FL.** The app checks the reservation again
    after installation verification and file-status setup. A reservation that expired during
    those steps is marked closed without starting an unnecessary FL process.
27. **Remember the primary custom FL folder.** A valid saved primary path is also added to the
    remembered installation set. If its drive is temporarily unavailable, choosing another FL
    no longer forgets that custom path; a later rescan can rediscover it.
28. **Apply one rule to remembered paths.** Primary and additional folders now share absolute-path,
    length and control-character validation. Case variants are deduplicated, and the remembered
    list remains bounded instead of retaining multiple spellings of the same Windows folder.
29. **Bound settings reads before allocation.** Preferences are inspected before opening and read
    through the bounded, stable UTF-8 reader. An oversized file is rejected without opening its
    body, rather than reading an arbitrary file completely and only then checking its size.
30. **Make failed settings reads visible.** Corrupt, oversized and inaccessible settings show an
    existing in-app notice and enter local diagnostics. A missing first-run file remains normal.
    Discovery and the current session remain usable; a successful save clears the notice.
31. **Explain an invalid saved name without losing usable folders.** Saved-name validation has
    its own notice instead of a silent catch. It asks for a replacement while restoring usable
    installation choices from the same preferences file.
32. **Make setup retry work and share overlapping attempts.** Failed initialization no longer
    leaves every later request waiting on the same rejected promise. The next request retries;
    concurrent callers share that attempt, and successful initialization stays cached.
33. **Catch initialization failures inside the IPC error path.** Setup failures are logged and
    translated to the app's normal readable error. They cannot bypass error handling and expose
    raw internal setup errors to the interface.
34. **Open local logs even during setup failure.** Open test logs and Open samples folder no longer
    depend on successful installation discovery. Troubleshooting remains available when the
    problem is precisely that setup cannot complete.
35. **Keep recovery alive when window fitting fails.** The render-error screen catches a throwing
    fitWindow bridge, so a resizing failure cannot also destroy the reload/log controls.
36. **Prevent duplicate recovery log actions.** A synchronous in-flight guard covers repeated
    clicks before React updates the button's disabled state. Async completion also checks that
    the error boundary is still mounted before changing its state.
37. **Refit recovery when content wraps or changes size.** A ResizeObserver watches the recovery
    surface, including narrow layouts and pending/error messages. It disconnects on unmount.
    The error screen no longer relies only on React updates to keep native sizing current.
38. **Keep diagnostics working with invalid clocks.** Finite but unusable dates, negative times
    and years outside the supported session-name format fall back to the system timestamp.
    Invalid test/device clock values cannot disable log creation or date serialization.
39. **Write native-PID metadata atomically.** A complete session.pending file is renamed over
    session.json, so an interrupted replacement leaves the previous JSON valid. A leftover
    pending marker makes retention skip that session. A metadata disk failure still marks logging
    unavailable; it does not claim that subsequent events were saved.
40. **Honor diagnostic byte limits without damaged characters.** Truncation reserves space for
    its ellipsis and stops on a UTF-8 boundary. Long multibyte messages, reasons and stacks stay
    inside their actual limits without broken replacement characters.

## Verification

- `npm test`: 140 tests passed, including 32 new regression cases.
- `npm run build`: TypeScript and production build passed.
- `node test/ui.cjs`: 124 room/UI checks, no recorded unexpected browser errors.
- `FLIM_FILES_TEST_DIST=dist node test/file-sidebar-ui.cjs`: 39 Files checks passed.
- `node test/window.cjs`: actual Electron resizing, ten mode switches, immediate typing,
  picker expansion/collapse and 200% zoom passed.
- `node test/sidebar-window.cjs`: actual lateral placement, stable expansion, restored focus/width
  and 200% zoom passed using isolated room/sample fixtures.
- `node test/recovery.cjs`: two real isolated renderer crashes recover automatically, a third
  reaches bounded manual recovery, and the main-process state survives without starting FL.

Package/portable checks and fingerprints are recorded in
`../dev/compatibility/desktop-0.2.20-2026-10-28.json`.

- `node test/package.cjs`: packaged startup, source/payload equality, native hashes, supported
  installed builds, local CSP/font/icons, Files geometry and clean diagnostics passed.
- `node test/portable.cjs`: actual executable extraction/startup and installed FL24/25/26
  discovery passed, using process-only D: TEMP/TMP. System settings were not changed.

Local executable: `release/flim-rooms-0.2.20-win64.exe` (103,551,573 bytes).

SHA-256: `36ede094f5a4cae8e28c254ad799e84920aaedaa6f3b1cf0c3f403d43439af91`

Screenshots and structured results are in `test/evidence/0.2.20`; command logs are
`test/latest-*-0220.log`. Inspected screenshots include recovery at 280px, actual lateral Files,
and the supported FL24 selection. No palette, input-outline or layout redesign was introduced.

## Scope and limits

The nine FL24/25/26 member-launch combinations pass mocked relay/native preflight regression
tests. Installed-build discovery covers FL24 24.2.2.4597, FL25 25.2.5.5319 and FL26 26.1.7.5654.
These are different forms of evidence: this pass does not newly prove two-computer MIDI,
project/plugin/sample synchronization, every FL patch, or end-to-end latency. Existing exact
engine fingerprint checks, unsupported-feature fallback and project-preservation protections
remain. Supported FL24 is not experimental. The user's existing FL process is left running.
No GitHub push, commit or Cloudflare deployment was performed.

## Antislop delivery gate

During-development mode follows the user's saved session choice. Design Read: compact equipment
card for numbered music rooms, existing General Sans, useful Files sidebar, unchanged light
palette, no dark/red input perimeter or decorative rings. ENERGY 1 / RHYTHM 1 / MOTION 2.

- R-02 PASS: no em dashes added in changed user-facing source or new report.
- R-03 PASS: room/Files browser tests cover 280-1200px; native 200% zoom remains usable.
- R-17 PASS: no invented counts, usage metrics or benchmark claims; file-open count is a test result.
- R-18 PASS: no invented testimonials or people added to the application.
- R-23 PASS: established user-authorized logo/artwork retained, no new asset or navigation invented.
- R-24 PASS: existing navigation retained and exercised by the room and Files suites.
- R-25 PASS: contrast checker measured text/white 15.14:1, muted/white 5.74:1, blue/white 8.44:1,
  focus/white 6.07:1 and muted/sidebar 5.07:1. No new palette.
- R-26 PASS: reload, logs, room controls, file controls and installation actions have real handlers.
- R-27 PASS: settings failure, setup retry, malformed replies, stale reads and recovery tested.
- R-28 PASS: no FAQ or filler content introduced.
- R-32 PASS: existing keyboard/focus checks pass; recovery heading retains focus after a fit failure.
- R-33 PASS: changes live in normal source modules; no runtime source or CSS rewriting.
- R-34 PASS: light/forced-colors checks pass; no theme toggle or alternative theme introduced.
- R-35 PASS: production build, interactive suites, native windows and real renderer recovery run.
- R-36 PASS: performance statement concerns file opens only; no universal or zero-bug claims.
- R-37 PASS: DESIGN.md records direction and unchanged dials.
- R-38 PASS: fixture data remains confined to tests; runtime status comes from actual sources.
- R-01 PASS: no new gradients; existing artwork scrim has the documented readability purpose.
- R-04 PASS: no new icons or icon-library choice.
- R-06 PASS: General Sans retained for the existing equipment-card identity and readable controls.
- R-07 PASS: no decorative grid, blueprint or background pattern.
- R-08 PASS: existing arrows retain their actual navigation/launch purpose; no new arrows.
- R-09 PASS: no promotional badges; room indicators keep their real status meaning.
- R-10 PASS: no glass surfaces added.
- R-12 PASS: no new shadows; existing picker elevation distinguishes its option sheet.
- R-13 PASS: no glows added.
- R-14 PASS: no generic feature-card grid added to the application.
- R-19 PASS: no new motion; existing short transitions and reduced-motion behavior retained.
- R-22 PASS: existing linked-computer/music equipment imagery retained for FL collaboration.
- Liveliness/dials PASS: ENERGY 1 / RHYTHM 1 / MOTION 2 remain explicit and consistent.
- Liveliness/focus PASS: room number, setup action or recovery reload remains the focal point.
- Liveliness/whitespace PASS: existing structural gaps preserved; recovery still fits its content.
- Liveliness/accent PASS: existing blue focus/selection accent retained, without extra decoration.
- Liveliness/identity PASS: linked computers, equipment imagery and General Sans remain the motif.
- Liveliness/Design Read PASS: existing direction read and retained before source edits.
- C-1 PASS: each new behavior has its concrete purpose listed above; no new visual defaults.
- C-2 PASS: recovery failure does not remove working reload/log controls; actions tested.
- C-3 PASS: no new signup, dashboard, menu or filler widget added to meet the requested count.
- C-4 PASS: delayed/partial data, failed setup, narrow recovery, keyboard and zoom paths exercised.
- C-5 PASS: live discovery, fixture tests and unverified live-MIDI scope are distinguished.
- R-05 PASS: compact existing task layout retained, without a generic site template.
- R-11 PASS: existing varied component corners retained; no pill-shaped redesign.
- R-15 PASS: Get started remains the user's explicit label; other actions name their task.
- R-16 PASS: added error messages use plain language without marketing claims.
- R-20 PASS: existing music-equipment identity preserved.
- R-21 PASS: established light desktop theme retained; no forced dark mode.
- R-29 PASS: established neutral palette and blue interaction accent retained.
- R-30 PASS: no cloning of another product's shell or interaction model.
- R-31 PASS: reasons for the recovery behaviors are written in DESIGN.md and items 35-37.

UI/human supplements PASS: no new colors, decorative motion or controls; established keyboard,
non-color status, contrast, 44px action targets, readable errors and reduced-motion checks retained.
This Windows desktop target does not provide a mobile virtual keyboard.
