# flim Rooms 0.2.21

40 implemented improvements beyond 0.2.20. Changes, executable and reports remain local.
This pass changes the desktop and native sample engine. Reviewed FL profiles and the relay
deployment are unchanged. The folder setting controls received shared samples on each computer.

## What actually changed

1. **Choose the received-files folder.** Files now has a native Windows folder picker before
   joining a room. Its choice reaches the real sample engine rather than only changing a label.
2. **Remember the destination.** The choice is stored in this computer's preferences and restored
   when the app opens again. Each participant can use a different local destination.
3. **Restore the default without moving files.** Use default removes the custom setting and
   returns to Documents/flim/samples. Selecting or resetting a folder leaves existing audio intact.
4. **Make cancel harmless.** Canceling the native picker preserves both the selected path and the
   saved preferences. The UI does not falsely announce a new saved choice.
5. **Roll back failed saves.** If replacing the preferences file fails, the previous destination
   remains selected and on disk. The UI explains that the new choice was not saved.
6. **Check actual writes.** A unique test file is opened, written and flushed before accepting a
   folder. A folder merely existing no longer counts as evidence that files can be saved there.
7. **Clean up only the owned probe.** The test handle is closed and its file removed after write
   failures too. Failed exclusive creation cannot cause an existing file to be deleted.
8. **Resolve folder junctions first.** Validation and persistence use the actual resolved
   destination. A short link cannot conceal a target path that exceeds the engine's path budget.
9. **Reject unusable folder syntax.** Relative paths, device paths, control characters, alternate
   data-stream syntax and invalid Windows characters are rejected before becoming destinations.
10. **Reserve filename space.** Selected root paths are bounded to 180 UTF-16 characters. The
    cache builder separately keeps full filenames inside classic Windows/FL path limits.
11. **Recover invalid saved destinations explicitly.** A damaged saved path blocks starting FL
    until a folder/default is chosen. Use default remains available even without a valid custom
    path. Other settings saves cannot erase the bad choice and silently redirect downloads.
12. **Protect destinations when preferences are unreadable.** Corrupt, oversized or inaccessible
    settings also require choosing the received-files location again. Discovery remains available;
    the app does not assume that a lost custom-folder setting meant the default folder.
13. **Do not fall back from a missing custom drive.** A vanished or inaccessible chosen folder
    produces a useful error. The app does not scatter new files into a second folder automatically.
14. **Honor redirected Documents.** Both desktop defaults and native fallback use the Windows
    Documents location, replacing the engine's hardcoded USERPROFILE/Documents assumption.
15. **Open the folder actually in use.** Open samples folder now follows the selected or prepared
    session destination. Previously it always opened a hardcoded Documents folder.
16. **Share concurrent open-folder requests.** Main coalesces repeated requests while Windows is
    opening the folder, including requests from a remounted sidebar. Errors release the flight.
17. **Guard folder operations in main.** Duplicate picker calls and competing room actions cannot
    overlap a choice. Changes are rejected while a room exists, even if invoked outside the UI.
18. **Bind storage to each launched FL process.** Only the validated folder enters FLIM_SAMPLE_DIR.
    Inherited overrides are excluded. Running FL processes retain their original folder when the
    desktop returns to start and prepares a different session; the private path is not in generic
    room state or room credentials.
19. **Ignore late sidebar folder replies.** Hiding/remounting Files cannot let an obsolete folder
    completion restore UI state, show an old error or clear a newer component's pending state.
20. **Allow read-only folder opening.** Opening an existing destination checks that it is a folder
    without requiring another write probe. Inspecting saved files does not demand new write access.
21. **Separate local path failure from malformed sample metadata.** An unusable local root no
    longer enters the invalid-remote-metadata disconnect path. The edit stays pending with its stale
    sample mapping cleared, while the room stays connected.
22. **Check directory-creation failures before downloading.** A blocked destination/subfolder
    retains the edit and waits. It does not spend a download request before noticing that no file
    can be written, or release the edit with an unavailable sample.
23. **Identify disk-write failures correctly.** Failed/short writes are local storage failures,
    rather than generic network errors. The incomplete download is discarded and the ordered edit
    remains pending for automatic retry; an immediate second HTTP attempt is avoided.
24. **Identify locked replacements correctly.** A valid download that cannot replace a locked
    cache file reports folder access, not corrupt audio. Existing bytes remain intact, stale path
    mappings are cleared, and the edit waits until a replacement succeeds.
25. **Flush before installing a download.** A successful HTTP response and matching hash are not
    enough if the disk flush fails. Flush errors retain the edit; successful replacement requests
    write-through behavior before the new path is published.
26. **Use unique owned download files.** GetTempFileName reserves a separate temporary file for
    the transfer instead of reusing a predictable filename plus PID. Failed downloads/replacements
    clean up their own temporary file. Existing samples are not used as download scratch space.
27. **Shorten long sample names within FL's path budget.** Long basenames are shortened while
    preserving a short audio extension. The previous code could build paths longer than the APIs
    or FL could open, despite fitting its larger in-memory buffer.
28. **Normalize reserved Windows basenames.** Names such as CON.wav and COM1.wav receive a safe
    leading underscore for local caching, preventing special-device interpretation or failed saves.
29. **Remove unusable trailing dots and spaces.** Cached filenames lose Windows-normalized
    suffixes; names made empty use sample.wav. The sender's displayed name remains unchanged.
30. **Keep shortened extensions valid.** Preserved extensions are sanitized and trimmed too,
    so shortening cannot reintroduce an invalid character or trailing space from the original name.
31. **Avoid splitting Unicode characters when shortening.** Filename truncation backs off from a
    UTF-16 high surrogate instead of leaving half of a character immediately before the extension.
32. **Explain storage waits in Files.** Waiting for folder access has its own status and guidance
    about drive access, free space and locks. It does not claim that a file is ready or corrupt.
33. **Stop an extra automatic read when a room closes.** Files retains the current confirmed
    snapshot without starting a fresh read as the ended-state effect mounts. An initially closed
    room with no snapshot can still read its last activity once; manual refresh remains available.
34. **Group transfers in one pass.** After ordering, one traversal assigns rows to transferring,
    waiting, attention and ready groups, replacing four complete filtering passes.
35. **Reuse search normalization between keystrokes.** Normalized filename keys are computed
    when the file list changes, rather than for every query. An empty query reuses the full groups.
    Accent-insensitive matching, natural ordering and displayed original names remain.
36. **Bound malformed progress values.** Nonfinite byte counts cannot produce NaN/Infinity
    progress values. Legitimate progress still clamps to zero through 100 percent.
37. **Keep controls usable if window fitting throws.** Main app fitting failures are caught and
    logged once per active fitting effect. Search, Files and folder actions remain usable instead
    of throwing an uncaught timer exception. Existing resize/monitor bounds remain.
38. **Log actual successful downloads.** Sample summaries count successful network downloads
    instead of treating duplicate reused paths or unattempted entries as completed downloads.
    Pending work is identified in the summary instead of being described as permanently missing.
39. **Wrap paths at folder separators.** The destination remains selectable and isolated in
    left-to-right order, with natural break opportunities after separators and a fallback for long
    segments. Long paths do not push controls outside narrow or zoomed views.
40. **Complete folder loading/error/retry states.** Files shows pending folder checks/actions,
    disables conflicting controls, reports actual persistence errors, offers Retry folder check,
    and explains why active rooms lock the choice. There is no added signup, menu or dropdown.

## Verification

- Production TypeScript/Vite build passes.
- 161 backend tests pass, including 21 additions beyond 0.2.20. Storage cases cover real writes,
  cleanup, junctions, canonical limits, cancellation, persistence rollback, invalid/corrupt settings,
  missing drives, launch environment, active-room locking and concurrent picker requests.
- 124 room/UI checks pass. Existing nine FL24/25/26 mixed-build preflight combinations use mocked
  relay/native replies. They test desktop decisions, not actual live cross-version synchronization.
- 55 Files/UI checks pass, including 16 additions. Narrow widths 280/400/599, lateral layouts
  680/1200, forced colors, keyboard control, delayed replies and status/folder recovery are covered.
- 29 native harness executables pass, covering sample rewriting, file failures, compatibility,
  launcher/injection guards, notes/CRDT, plugins, portable state, queues, project-load protection,
  inbound ordering and logging. They do not operate a user's FL process.
- The native sample/loopback test passes: one upload, 13 GETs including injected failure cases,
  verified cache reuse, invalid/missing destination, locked replacement, disk-write/flush failures,
  verification recovery, ordered real preset rewriting, allowance pause and session cancellation.
  Incomplete temporary files are removed and the source audio remains untouched.
- The broader loopback transfer suite passes, including bounded streaming, malformed manifests,
  failed lookups and stale-session cancellation.
- Real Electron tests pass for native folder-dialog IPC (dialog result supplied by the fixture),
  persisted choice/default, redirected Documents, duplicate opening, retained form text and 200%
  zoom. Window/sidebar geometry and bounded renderer-crash recovery also pass.
- Unpacked and self-extracting portable app startup pass. Packaged source modules/native hashes,
  icons, local diagnostics, supported installed FL24/25/26 discovery and clean initial room state
  are verified. Test extraction uses process-local TEMP/TMP on D:.
- Contrast checks for sidebar text/actions/notices pass: 5.07:1 through 13.37:1. New controls use
  existing 44px actions, blue keyboard focus and plain-text status. No new palette or motion.

Evidence: test/evidence/0.2.21, test/latest-*-0221.log, and the compatibility manifest at
client/dev/compatibility/desktop-0.2.21-2026-10-28.json.

Local portable executable: release/flim-rooms-0.2.21-win64.exe, 103,554,801 bytes.
Unpacked size: 384,908,588 bytes. SHA-256:
`a6ed1f252aa28e05a5a56bf05114e7c00fe8d1ac1e3335682a26948377e22594`.
The rebuilt native engine SHA-256 is
`7fa698b0818902ec6eab6a56e6056774879e69b949902188732370d28b89e171`.

## Scope and practical limits

Use the complete flim-rooms-0.2.21-win64.exe on both computers to receive the rebuilt engine.
Choose a destination before joining. The choice applies to FL processes subsequently launched
through flim, not to a running process's environment. Existing audio is neither migrated nor
deleted. Matching local audio may still be reused from an existing path instead of downloaded.

The installed reviewed builds remain 24.2.2.4597, 25.2.5.5319 and 26.1.7.5654. No new FL build
profile was added, and this pass does not establish support for every past/future patch. Existing
portable fallback, unavailable sounds/features and fixed-length plugin-path constraints remain.
No fresh two-computer physical-MIDI, VST-version or live drag/drop session was run. Loopback tests
prove the transfer/storage code paths; they do not replace that real collaboration test. No user
project or FL process was launched, closed or changed for this pass. No GitHub commit/push or
Cloudflare/R2 configuration/deployment occurred.

## antislop delivery gate: PASS

- R-02 PASS: no em dashes in added product text; error/help text reviewed in the tested UI.
- R-03 PASS: Files tests cover 280/400/599/680/1200 widths with zero horizontal overflow.
- R-17 PASS: runtime progress/counts come from actual snapshots; no promotional statistics added.
- R-18 PASS: no testimonials or invented people added to the application.
- R-23 PASS: no new artwork/assets/navigation structure; controls live inside the requested Files sidebar.
- R-24 PASS: native folder, default, retry and open actions have tested destinations/handlers.
- R-25 PASS: contrast.txt records the checked text/background pairs, minimum 5.07:1.
- R-26 PASS: new controls are exercised through browser and real Electron IPC tests.
- R-27 PASS: folder loading, empty/pre-room, invalid settings, canceled picker and failure/retry states tested.
- R-28 PASS: no FAQ or filler sections introduced.
- R-32 PASS: keyboard folder controls, Escape/sidebar focus, selection and forced-colors checks pass.
- R-33 PASS: implementation is in ordinary source modules/CSS; no script patches source at runtime.
- R-34 PASS: established light/forced-colors behavior retained; no unsupported theme toggle added.
- R-35 PASS: production build, UI suites, real windows, renderer recovery and both packaged startups run.
- R-36 PASS: no latency, zero-bug or universal-FL claim; measured work reduction and scope stated above.
- R-37 PASS: DESIGN.md records the supplied equipment-card direction and unchanged dials before delivery.
- R-38 PASS: runtime folder data is real; simulated transfers/dialog results are confined to tests.
- R-01 PASS: no new gradients; existing artwork treatment retained.
- R-04 PASS: no new decorative icons or icon-library choice.
- R-06 PASS: General Sans retains the established readable equipment-card identity.
- R-07 PASS: no decorative grids, dots or blueprint background added.
- R-08 PASS: existing Files arrow retains its sidebar navigation purpose; no new decorative arrows.
- R-09 PASS: no promotional pills/badges added.
- R-10 PASS: no glass surfaces added.
- R-12 PASS: no new shadows; existing picker elevation retained.
- R-13 PASS: no glow added.
- R-14 PASS: no generic feature-card grid added to the application.
- R-19 PASS: folder updates use normal state changes; existing short reveal/reduced-motion rules remain.
- R-22 PASS: existing computer/keyboard artwork retains its collaboration meaning.
- Liveliness/dials PASS: ENERGY 1 / RHYTHM 1 / MOTION 2 remain explicit and consistent.
- Liveliness/focal point PASS: setup/room action remains primary; the path heading identifies the Files task.
- Liveliness/whitespace PASS: compact location grouping separates storage from transfer activity without a new panel.
- Liveliness/accent PASS: established blue focus/selection remains the single interaction accent.
- Liveliness/identity PASS: linked equipment artwork, wordmark and General Sans remain the motif.
- Liveliness/Design Read PASS: DESIGN.md was read before editing and extended with this pass's reasons.
- C-1 PASS: each visual addition has a task purpose documented in DESIGN.md and items 39-40.
- C-2 PASS: choose/default/open/retry are real, tested behaviors with disabled/error states.
- C-3 PASS: storage controls serve the explicit user request; no template widget added for the count.
- C-4 PASS: delayed replies, write failures, corrupt settings, narrow widths, keyboard and zoom tested.
- C-5 PASS: automated/loopback evidence is distinguished from unperformed live FL collaboration.
- R-05 PASS: existing compact room/sidebar layout retained without a website template.
- R-11 PASS: established varied component corners retained; no pill redesign.
- R-15 PASS: Get started is the user's explicitly chosen label; new actions describe folder tasks.
- R-16 PASS: added product copy uses concrete actions and recovery guidance without marketing claims.
- R-20 PASS: established music-equipment identity retained.
- R-21 PASS: light desktop theme retained; no forced dark mode.
- R-29 PASS: existing neutral palette and blue interaction accent retained.
- R-30 PASS: existing application refined without cloning another product's shell.
- R-31 PASS: reasons for path wrapping, local placement, typography and states are written in DESIGN.md.

Human/layout supplements PASS: measured contrast, 44px actions, non-color status, keyboard focus,
reflow at intermediate widths and 200% Electron zoom are verified. This Windows desktop app does
not use a mobile virtual keyboard. The native file dialog is provided by Windows.
