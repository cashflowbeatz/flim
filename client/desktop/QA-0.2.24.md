# flim 0.2.24: notes, playlist and shared-file reliability

Local build, 2026-10-28. This round adds **34 fixes and refinements after 0.2.23**.
The complete portable app includes the rebuilt native engine. No commit, GitHub push or
Cloudflare deployment was made. Existing FL Studio processes and projects were not operated.

## What changed and why it matters

1. **Share changes involving more patterns.** The note scanner used to discard a batch when
   eight or more patterns changed, assuming it was a project load. It now sends those edits
   through the existing project/session guards. A regression changes twelve patterns and checks
   that all twelve are scheduled, rather than silently becoming the local baseline.
2. **Ignore harmless native note reordering.** The change fingerprint now treats a pattern
   as a collection of notes rather than an ordered memory array. FL rearranging identical
   records does not become another musical edit. Duplicate-note counts still affect the
   fingerprint, and local selection remains excluded.
3. **Read note fingerprints under FL's note lock.** Hashing now takes the same native lock
   used when copying and applying notes. The scanner no longer intentionally reads a note
   array while FL is modifying that protected array.
4. **Protect playlist fingerprints with the native lock too.** Playlist hashing holds the
   existing note/clip lock while copying native records. Conversion and comparison therefore
   use one protected view of the list instead of racing an array replacement.
5. **Verify note contents after applying them.** A matching note count was insufficient:
   an add could leave the wrong pitch or another field while keeping the count correct. The
   receiver now compares the resulting note collection, including duplicate counts. A failed
   comparison pauses sharing instead of confirming a false sync.
6. **Queue complete note snapshots together.** All patterns and the final pattern-removal
   task must fit before any task is inserted. Late allocation failures and a nearly full queue
   leave no prefix of the new snapshot waiting to apply. This is atomic queue preparation,
   not a rollback transaction around FL's later native calls.
7. **Decode received notes once.** Preparation retains the validated decoded payload instead
   of repeatedly decoding base64 and CRDT records during validation and queue insertion. The
   queue owns the prepared bytes, with cleanup on rejection.
8. **Check the whole CRDT history budget up front.** A full note snapshot containing more
   than 262,144 history entries is rejected before any pattern is queued. Individually legal
   patterns cannot hide an oversized combined history until partway through applying it.
9. **Skip unchanged CRDT note application.** When a merged note history produces the notes
   already present in FL, flim updates its tracking state without another native note apply
   or editor refresh. The merged history is retained, including deletion history.
10. **Match playlist selections without repeated full scans.** Sorted clip records replace
    the previous selected-clip-by-incoming-clip search. Duplicate clips match once, with local
    selected copies preferred. The regression covers 6,000 selected clips; this demonstrates
    the matching behavior, not a measured live-session speedup.
11. **Keep identical playlist clips in place.** A received list with the same clip contents
    skips deletion, rebuilding and playlist refresh. Existing native clip IDs and local
    selections remain in place even when the received record order differs.
12. **Verify playlist contents across native layouts.** After an actual replacement, flim
    converts the native result back to its shared format and compares the contents, ignoring
    local IDs and selection. Tests cover FL24's 60-byte records and FL25/26's 80-byte records,
    including a wrong-content result with the expected count.
13. **Report playlist application failures.** Missing arrangements, insufficient allocation
    and unsuccessful native application now pause sharing explicitly. They no longer silently
    discard an incoming playlist edit and allow the peers to continue with different lists.
    FL Studio and the project remain open.
14. **Reserve control-queue space for work that needs it.** Full control snapshots count
    meaningful routed changes rather than every received value. Unchanged controls and mixer
    tracks unavailable in FL24 can be skipped even when the edit queue is full. Actual changes
    still require capacity before routing the snapshot.
15. **Handle long preset temporary paths safely.** Temporary preset filenames use bounded
    formatting with an explicit failure path. An excessively long TEMP directory reports the
    problem and pauses sharing instead of invoking the C runtime's invalid-parameter behavior.
16. **Respect Windows checksum failures.** Streaming hash updates and finalization now return
    and propagate errors. A failed cryptographic provider cannot leave an uninitialized digest
    accepted as a content identity. Tests inject both update and finalization failures.
17. **Keep source bytes stable while hashing.** File hashing opens with read sharing only,
    so a concurrent writer or delete handle cannot be accepted during the read. An actively
    written file fails the hash attempt rather than being published under a mixed-content hash.
18. **Cancel obsolete hashing between read chunks.** A changed room/session generation
    stops a long file hash. Work from an old session cannot continue hashing the full file
    just because it had already started. A blocked individual Windows read is not interrupted.
19. **Check cached paths outside the shared cache lock.** Cache lookup copies a candidate's
    metadata under the lock, then probes the filesystem after releasing it. A slow or
    disconnected drive no longer holds that shared lock for the filesystem check.
20. **Match Windows source paths without case sensitivity.** Collected paths and source-to-
    local mappings now treat drive/folder letter casing consistently. The same Windows path
    written with different capitalization does not create duplicate mappings or miss a rewrite.
21. **Recognize forward-slash sample paths in plugin data.** Embedded absolute paths such
    as C:/Audio/kick.wav are collected alongside backslash paths. Plugins using that spelling
    can participate in the existing sample-copy and path-rewrite flow.
22. **Reject conflicting identities for one source path.** One received path cannot name
    two different hashes or byte counts in the same edit. Preflight detects the conflict
    before local probing, downloading or changing existing mappings.
23. **Reject contradictory sizes for the same content.** Different source paths may refer
    to the same hash, but they must agree on its byte count. A contradictory list no longer
    gets as far as downloading one reference and treating another as a separate identity.
24. **Validate decoded source paths before filesystem work.** Received path metadata must
    contain valid UTF-16 and an allowed absolute or FL token path. Device namespaces, alternate
    data streams, control characters and invalid Windows path characters are rejected. FL's
    token-plus-filename and token-plus-separator forms remain accepted.
25. **Require canonical base64 for source paths.** The parser validates padding and unused
    bits as well as characters and decoded length. Malformed alternate encodings cannot reach
    path mapping or filesystem work.
26. **Avoid probing a sender's network share automatically.** Received UNC source paths go
    through content transfer rather than causing this computer to open another person's UNC
    location. A network folder explicitly chosen as this receiver's cache remains available.
27. **Count repeated downloadable content once.** Several source references to one missing
    hash add its byte size to the transfer total once. The displayed amount no longer counts
    the same audio multiple times when one cached copy will serve those references.
28. **Reject Windows device names in the destination picker.** Folder validation catches
    reserved components such as NUL, CON, COM1 and LPT1, including the Windows superscript-digit
    variants. It does so before attempting the disk probe.
29. **Reject malformed Unicode destinations.** A folder path with an unpaired surrogate is
    rejected before disk access. Valid supplementary Unicode characters remain accepted;
    international folder names are not reduced to ASCII.
30. **Preserve the first folder-probe error.** If writing or syncing the test file fails,
    later close/unlink failures no longer replace that original error. Cleanup still attempts
    both operations, so the app can explain the actual storage problem.
31. **Give useful destination error messages.** Full disks/quotas, missing or disconnected
    drives, and denied/read-only storage receive different recovery guidance. Error text does
    not echo private filesystem paths. Both folder selection and room-launch preflight use it.
32. **Separate simultaneous transfer progress.** Upload and download workers now keep their
    byte counters, totals, direction and update timing in thread-local storage. One worker
    cannot overwrite another worker's progress fields. A concurrent-worker regression checks
    this separation; the toolbar still displays one current status at a time.
33. **Reset progress before a quick network retry.** A retried request restores its starting
    batch offset, excluding bytes from the failed attempt. A partial ten-byte read followed by
    a successful 22-byte retry adds 22 bytes to the preceding total rather than 32.
34. **Bound downloads by the advertised sample size.** An oversized body stops before the
    excess chunk is written, and a short body fails verification. Wrong-sized content uses
    the pending verification retry path rather than the fast network retry. The edit releases
    only after valid bytes and a matching content hash arrive; the room stays connected.

## Verification

- 32 deterministic native harness executables pass, including the new t_sweep24 coverage and
  all nine synthetic host/member combinations across the reviewed FL24/25/26 families.
- 180 desktop backend tests, 127 room/UI checks and 68 Files/UI checks pass. Both UI result
  files report no browser errors. These include existing states and features as regressions,
  not 195 newly added features.
- Actual Electron checks pass for the native folder-dialog bridge, saved destinations, window
  fitting, ten Create/Join switches, lateral Files placement, focus return and 200% zoom.
  Isolated renderer-crash tests recover twice, bound the retry loop and retain main-process
  state without launching an FL process.
- The native file-sync loopback passes: one stored upload, thirteen download requests,
  recovery from lookup/upload/download/verification failures, explicit local disk failures,
  quota pause, canceled old sessions, verified path rewriting and untouched source bytes.
- The native WinHTTP loopback passes its ten named scenarios, including 1,025 references
  split into 512/512/1 batches, partial writes, advertised-size checks, a partial-read retry
  and zero redirect follows. The evidence count is derived from the actual PASS lines.
- 73 local Cloudflare tests and the original Node relay suite pass. No live Cloudflare state
  changed. The new native harness is included in the local CI workflow's execution list;
  that workflow has not been pushed or run on GitHub in this round.
- The complete portable app builds and both unpacked-package and self-extracting startup
  checks pass. Packaged sources/native payloads match this checkout; installed FL24/25/26
  discovery, local diagnostic hashes, production renderer isolation and app/taskbar icons pass.
  Those checks allocate no real room and launch no FL process.

Raw output and current screenshots are in test/evidence/0.2.24. The compatibility manifest
records the final artifact hash, native hashes and packaged-startup results.

## Compatibility and remaining limits

Reviewed builds remain **24.2.2.4597**, **25.2.5.5319** and **26.1.7.5654**. FL24 is supported
without an experimental label. Exact native fingerprints remain required. This round adds no
new build profile or binary mapping and does not prove every historical or future patch works.

An older FL member can join a newer host using shared notes/basic controls while unsupported
sounds, playlist fields and other features remain unavailable. A plugin's stable identity does
not guarantee that its older release can read a newer release's opaque preset. Missing or
incompatible content is not claimed to be converted by these changes.

No fresh physical-keyboard, two-computer, live VST-version or drag/drop session was performed.
Recorded MIDI/note behavior is covered by synthetic native tests. The earlier intermittent FL24
recovery-save stall remains unresolved and was not retested in the user's open project. These
results do not justify promising zero crashes or complete compatibility for every FL feature.

Use the complete 0.2.24 portable app on each test computer when starting the next session.
Already running FL processes retain the engine loaded at their startup. Their projects were
not closed, replaced or injected during this sweep. The existing transfer/history/storage
guards remain; no relay policy was removed or deployed in this round.

## Design delivery gate

The user's during-development antislop choice remains active. The existing direction is
ENERGY 1 / RHYTHM 1 / MOTION 2, as recorded in DESIGN.md. No renderer, CSS, assets, font,
palette, motion or navigation structure changed. This gate covers the retained desktop UI and
the revised folder-error copy, with functional/native evidence distinguished above.

- R-02 PASS: newly authored UI errors and this report use plain punctuation without em dashes.
- R-03 PASS: current UI suites cover 280/320/375/400/600/680/810/1200px layouts and short windows.
- R-17 PASS: test counts come from saved output; no customer or speed statistics were added.
- R-18 PASS: no testimonials or profile images were added; participants in screenshots are fixtures.
- R-23 PASS: no new visual asset or navigation structure was created.
- R-24 PASS: retained controls use app handlers; this round introduces no navigation link.
- R-25 PASS: contrast.json calculates the retained body, muted, focus, action, error, notice
  and caption pairs at 5.07:1 or above; the new errors use the unchanged error/notice surfaces.
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
- R-15 PASS: folder messages give specific recovery actions; the requested Get started label stays.
- R-16 PASS: new copy describes storage failures and actions without marketing claims.
- R-20 PASS: equipment artwork, numbered rooms and FL-family selection retain the product identity.
- R-21 PASS: the user-directed light surface remains; no forced dark mode was introduced.
- R-29 PASS: charcoal, pale surfaces and the existing blue focus accent remain unchanged.
- R-30 PASS: no external product layout was copied in this round.
- R-31 PASS: the native sidebar screenshot was inspected; each retained visual decision is in DESIGN.md.
- Human checks PASS: measured text/focus pairs, visible focus and text state labels are retained;
  real Electron 200% zoom and keyboard checks pass. Unoutlined fields follow the user's explicit
  direction and retain visible labels plus blue keyboard focus. This is a Windows desktop app;
  no mobile virtual-keyboard claim is made.
