# flim 0.2.23: FL editing and compatibility sweep

Local build, 2026-10-28. These are **39 additional fixes/refinements after 0.2.22**.
The complete portable executable includes the rebuilt native engine. No GitHub commit/push
or Cloudflare deployment was made. Existing projects and running FL processes were not operated.

## What changed and why it matters

1. **Keep the correct FL version on a waiting plugin update.** Adjacent queued preset loads
   can replace one another. That replacement now copies `presetFl` as well as the file and load
   mode. A retained FL26 preset cannot inherit the preceding FL24 preset's compatibility label.
2. **Dispatch the actual shared edit.** Incoming message types and operation kinds now come
   from direct JSON fields, rather than substring matches. Legal whitespace no longer makes
   an edit disappear, and an unrelated nested `kind` cannot turn a channel edit into a project load.
3. **Hold spaced messages during project loading.** The loading backlog recognizes a real
   `t: op` field regardless of formatting. Such edits wait for the project instead of bypassing
   the existing project-load barrier.
4. **Prepare sample-dependent edits with spaced file lists.** Inbox routing and the download
   worker recognize the direct `samples` member structurally. They also recognize formatted
   stored-edit envelopes. A nested unrelated samples field does not schedule a transfer.
5. **Take your room role from the correct field.** Welcome handling reads the direct role and
   requires exactly leader/member. A peer's role or a prefix such as leader-extra cannot assign
   the local FL host authority. Missing roles pause sharing instead of guessing.
6. **Validate individual control values before routing.** `param.set` now reads unsigned
   32-bit IDs/values from its own operation. Strings, fractions, negative numbers, overflow and
   nested substitute fields cannot wrap into a different control or value.
7. **Validate complete control snapshots before applying a prefix.** Every `state.full`
   entry must contain exactly two valid numeric fields. A bad final entry queues none of the
   preceding controls. Valid spacing, field order and the full unsigned value range are supported.
8. **Reject duplicate control IDs in a snapshot.** A sorted preflight finds duplicate IDs
   before any routing. Conflicting entries cannot produce a partially applied or order-dependent
   initial state.
9. **Reserve queue capacity for the control snapshot.** A complete snapshot must fit before
   any of its controls are routed. Insufficient capacity pauses sharing with an explanation;
   it does not leave a prefix waiting to apply. The existing bounded queue is retained.
10. **Collapse repeated waiting knob updates.** Consecutive remote values for the same control
    keep the newest value in one queue entry. The regression sends 10,000 updates and retains
    one entry with the final value. This is a queue measurement, not an end-to-end latency claim. Coalescing also
    requires matching operation modes and, for local reversions, the same plugin generation;
    remote values cannot inherit an obsolete local-revert guard.
11. **Give FL's interface time between control batches.** Applying queued controls now yields
    after 64 writes or after a 4ms elapsed check, and requests another pass. This prevents the
    entire control backlog being processed in one uninterrupted hook call. One native write can
    itself take longer than 4ms; this is a cooperative budget, not a hard deadline.
12. **Defer optional plugin recovery when the queue is full.** A queued automatic plugin scan
    is retryable background work. It can now wait for a later scan instead of stopping sharing
    just because the main edit queue was full. Received mutations retain their overflow protection.
13. **Report exhausted waiting-plugin control storage.** Existing waiting IDs still update
    in place at the 8,192-entry cap. A new ID beyond the cap now explicitly pauses sharing instead
    of being silently lost. Local FL remains open.
14. **Parse parameter lock IDs completely.** Parameter lock keys must contain a canonical,
    bounded unsigned number. Partial strings, leading-zero aliases, overflow and nested keys
    cannot lock the wrong control. Legal JSON spacing works.
15. **Require exact plugin/pattern/playlist lock keys.** Plugin track/channel/slot numbers and
    pattern/playlist indices are parsed as complete bounded fields. Inputs such as `plug:ch:1junk`
    and `pat:1more` cannot alias a real edit unit through sscanf/atoi prefix parsing.
16. **Validate lock ownership and grant values.** Grant/release handling reads direct fields,
    accepts actual booleans, and checks owner identity syntax. Spaced valid grants/releases work;
    nested or malformed ownership data does not change the local lock record.
17. **Match metadata entity types exactly.** `pt-more` no longer matches the pattern tag `pt`.
    Renames/colors/details reach only the explicitly named supported entity type.
18. **Bound metadata targets before touching FL.** Metadata indices come from direct, checked
    integer fields. An oversized value cannot wrap into an existing pattern/channel/track index.
19. **Validate complete shared names.** Names require canonical base64, even UTF16 byte length,
    a maximum of 4,096 UTF16 code units, no embedded NUL and complete surrogate pairs. A malformed
    name blocks the whole metadata update before a valid length/color in that update is applied.
    Intentional empty names remain supported.
20. **Validate shared colors as unsigned values.** Negative, textual or overflowing colors
    are rejected before setters run. The full unsigned 32-bit field remains available.
21. **Validate pattern lengths as signed integers.** Pattern lengths preserve the wire's signed
    32-bit representation, including sentinels, while rejecting strings, fractions and overflow.
    The parser no longer accepts a numeric prefix and silently discards the rest.
22. **Preflight time signatures.** Both fields must be complete numeric values within the
    existing FL setter's 1–16 range. Missing/invalid fields cannot be marked as successfully synced.
23. **Do not confirm metadata after a native exception.** A fault in an FL metadata setter now
    pauses sharing and leaves the last confirmed hash unchanged. The old code logged the fault
    and then advanced the baseline as if the update had succeeded. A fault-injection test covers it.
24. **Handle metadata-send allocation failure.** Metadata JSON construction checks its first
    allocation before formatting. A low-memory failure now produces a sharing error instead of
    attempting to write through a null pointer.
25. **Detect silent note insertion refusal.** After native note edits, the resulting note count
    must match the requested count before the agreed copy/hash is advanced. An add routine that
    silently refuses a note no longer looks like a successful sync. This count check does not
    prove arbitrary same-count content corruption is impossible.
26. **Detect silent playlist insertion refusal.** The native playlist apply path also checks the
    resulting count before committing the agreed list. A failed native add pauses sharing instead
    of confirming a shorter playlist. Existing FL24 60-byte / FL25+ 80-byte conversion remains.
27. **Preserve a CRDT scope when reseeding from itself.** The scope is copied before clearing
    the previous state. Passing that state's own scope no longer zeroes the new baseline identity.
28. **Merge repeated CRDT deliveries without allocation.** A validated delta that changes no
    IDs or deletion flags now returns without allocating/replacing the note history. Duplicate
    delivery continues to work even when a new allocation would fail.
29. **Allocate CRDT joins for the actual union.** Merge preflight counts unique identities and
    rejects immutable-note conflicts before allocating. Joining overlapping histories allocates
    their union, rather than space for both entire inputs. The history cap and tombstones remain.
30. **Avoid cloning unchanged CRDT histories.** Capture compares the sorted live-note multisets
    first. If no edit exists, it frees those comparison arrays without cloning/sorting the history
    or allocating an outgoing patch. Existing duplicate-note semantics remain intact.
31. **Size CRDT capture work to actual activity.** Comparison storage uses live entries rather
    than all tombstones; replacement history and patch arrays are sized from the computed additions
    and edits. Deletion-heavy sessions avoid reserving a worst-case patch for every old identity.
32. **Make empty CRDT captures allocation-free.** An empty piano roll whose history contains
    only retained deletions can complete a no-op capture without allocations. Tombstones are
    preserved, so delayed deltas cannot resurrect those notes.
33. **Clear failed CRDT wire outputs.** Packing failures return a zero byte count. Unpacking
    failures clear the count, entry pointer and scope; they expose a record only after complete
    validation. A caller cannot reuse stale decoded metadata from a previous success.
34. **Read escaped international editor labels.** Focus labels decode JSON escapes, valid UTF8
    and Unicode surrogate pairs, including quoted plugin names and musical emoji. Invalid/truncated
    sequences are ignored; control characters become spaces. Protocol identifiers remain strict.
35. **Validate remote cursor locations.** Cursor messages require exact editor labels, complete
    integer context/coordinates and bounded nonnegative coordinates. Bad messages preserve the last
    valid location; an explicit empty label still hides the cursor. No notes are changed by cursors.
36. **Read relay errors structurally.** Error-code matching no longer depends on compact JSON,
    and escaped user-facing reasons decode correctly. The generic compatibility guidance no longer
    tells portable mixed-version users that the oldest FL must host.
37. **Reject unusable zero VST identities.** A wrapper with an all-zero VST2/VST3 ID is not used
    for automatic local matching. Its unknown identity cannot accidentally select another plugin
    with the same unusable ID. Existing unavailable-plugin handling retains the shared state.
38. **Reject conflicting wrapper names.** Duplicate name tags make a wrapper ambiguous and
    ineligible for automatic adaptation. The parser no longer silently takes the last name while
    matching that preset's identity and path.
39. **Recognize equivalent local VST paths.** Scanned paths are compared after Windows path
    normalization, slash normalization and case-insensitive comparison. Two scan records for the
    same logical path no longer appear to be conflicting installations. Stable VST IDs are still
    required, genuinely different binaries remain ambiguous, and the sound chunk stays untouched.


## Executed checks

- **31 native harness executables passed.** Added `t_edit_compat`; extended CRDT, notes,
  playlist, plugin, metadata and queue regressions. The native build log has no warnings/errors.
- **Nine synthetic host/member combinations** cover reviewed FL24/25/26 versions in each
  direction. They exercise negotiated portable mode plus formatted notes, controls and structural
  edit dispatch. These are native-code fixtures, not nine live FL sessions.
- Existing native MIDI-recording merge tests still pass: unsent local recorded notes survive a
  simultaneous remote delta; duplicate delivery, duplicate notes, concurrent moves/deletions,
  history limits, and 200 three-replica CRDT rounds converge.
- **177 desktop backend tests**, **127 room/UI checks**, **68 Files/UI checks** passed.
- Real Electron folder-dialog IPC/persistence/default location, 200% zoom, window/sidebar
  sizing and bounded renderer-crash recovery passed. Existing UI design is retained.
- Native sample loopback passed with **1 stored upload and 13 downloads** across failure/retry
  cases. It tests actual inbound preset rewriting, cancellation and untouched source audio.
  The broader transfer/streaming loopback passed too.
- **73 local Cloudflare worker tests passed**, including current mixed-version and storage
  handling. These tests run locally; they do not deploy or change account/billing settings.
- Portable/unpacked packaging checks passed: bundled native bytes equal the tested binaries;
  packaged app sources equal this checkout; icons and local diagnostics work; reviewed installed
  builds are discovered; no room is allocated and no FL is launched by package-startup checks.

Evidence: `test/evidence/0.2.23`, `test/latest-*-0223.log`,
`test/latest-native-results-0223.json`, and
`../dev/compatibility/desktop-0.2.23-2026-10-28.json`. The manifest records package sizes/hashes
and the unchanged running-user-FL observation.

## Compatibility boundaries

Reviewed installations remain **24.2.2.4597**, **25.2.5.5319**, **26.1.7.5654**. FL24 is supported,
not experimental. No new FL profile, binary mapping or fingerprint exception was added. These
fixes improve the shared protocol and editing paths; they do not establish every FL patch.

Validation retains the existing skip for mixer tracks unavailable in FL24. Older FL members
may share notes/basic controls with a newer host while unsupported sounds,
playlist fields and other features remain unavailable. Stable VST identity does not guarantee
that a vendor's older plugin can load a newer version's opaque sound state. Ambiguous installs
are not guessed. Invalid edits/native failures pause **sharing**; they do not close FL Studio.

The full-control snapshot still must fit the bounded edit queue. This pass does not remove
existing note-history, memory, transfer or storage guards. No new physical MIDI keyboard,
two-computer, live VST-version or drag/drop session was performed. Use the complete 0.2.23
portable executable on both computers for a fresh collaboration test; an already running FL
keeps the engine it loaded at startup.

## Design check

antislop remains active during development under the user's session choice. ENERGY 1 /
RHYTHM 1 / MOTION 2 and the established compact equipment card are retained. No renderer/CSS,
palette, artwork, fonts, animation, menus, field outlines or sidebar geometry changed in this
round. Existing UI suites revalidated keyboard, errors, narrow layouts, forced colors and zoom;
the actual sidebar screenshot was inspected. The previous measured contrast palette is unchanged.
No new visual asset or decorative treatment was introduced. Functional changes report real
native states; simulated participants and samples remain confined to tests.
