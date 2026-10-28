# Automatic sample sharing

## Current app: 0.2.23

Use the complete [flim-rooms-0.2.23-win64.exe](https://github.com/cashflowbeatz/flim/releases/download/rooms-v0.2.23/flim-rooms-0.2.23-win64.exe) on both
computers. The Files sidebar is integrated into the regular app; a separate preview is not needed.
Version 0.2.23 also recognizes file lists and stored-edit messages regardless of legal JSON
spacing, preserving the download-before-apply path. It retains the file-location and transfer
recovery features described below. [FL editing fixes and QA](QA-0.2.23.md).

Create/join a room, open FL Studio through flim, then add an audio sample through a supported
shared channel or playlist operation. Use the **Files** arrow at the upper right of the card;
it is available on the first screen, during setup, and inside a room. Before a room exists,
the sidebar explains how to begin and does not read an old room's transfers. Inside a room,
Files opens beside the room controls and shows real progress.
Hide files closes the sidebar and restores focus. Escape clears a search first, then closes
Files on the next press. Ctrl+F from room controls opens Files and selects **Find a file**.
**Both / To me / From me** limits the list to incoming or outgoing files. The choice survives
hiding the sidebar; **Show all files** clears direction, search and status filters together.
No Syncthing or separate service is needed.

Activity remains bounded to 96 displayed entries, with unfinished entries ahead of ready ones.
A notice explains limited lists and retains the last confirmed coverage through read failures.
This display limit does not cancel or cap actual sample transfers. Native per-process activity
also evicts completed entries before waiting work. Local diagnostics record status availability
transitions without recording filenames or paths. See [0.2.22 fixes and evidence](QA-0.2.22.md).

Before joining, **Choose folder** under **Save received files to** sets the destination for this
computer. This uses the Windows folder picker and remembers the validated choice across starts.
**Use default** returns to Windows Documents/flim/samples, respecting redirected Documents.
**Open samples folder** opens that actual destination. Existing samples are never moved or
deleted by changing the choice. The next FL process receives the selected folder; an active
room locks changes, and older FL processes retain their own folder even after returning to start.
The canonical folder path must be 180 characters or fewer, with space left for cache names.
A write-and-flush probe checks access before saving. An unavailable custom drive or damaged
saved location requires repairing or selecting the location; it does not scatter downloads into
an automatic fallback. This is a local received-files setting, not an upload-source or R2 setting.

Search matches filenames regardless of case or accents. **All**, **Pending**, **Attention** and
**Ready** filter the displayed files without changing delivery. Arrow keys, Home and End move
between filters. **Clear** removes the query; **Show all files** clears both search and filter
when nothing matches. Visible and total file counts distinguish filtering from missing files.
Each filter's count follows the current query, and a check identifies selection. Name/Recent
switches natural filename versus newest-activity ordering within each group. Hiding Files
preserves the current room's search, filter, order and list position. Reopening shows confirmed
cached activity while checking it again; leaving the room clears this view. Open test logs stays
accessible below Files even at high zoom or narrow widths.

Active transfers and waiting files have separate counts. Direction describes whether a file is
coming to this computer or leaving it, and progress shows actual completed/total bytes. Unknown
sizes are explicit. Filenames stay in natural order within each state group. The room number
above the list identifies the session being shown.

**Refresh files** checks local status; it does not retry a transfer or bypass an allowance.
It shares one read with automatic checks. Healthy open views check each second; read failures
back off to 2, 4, 8 and then 15 seconds. Hidden views pause and resume on return. Closed rooms
retain their last confirmed activity and stop automatic checks; manual refresh remains available.
A failed status read cannot be mistaken for a confirmed empty list. If only one FL process's
status file is unreadable, its confirmed rows remain while other processes can still update.

Unchanged process snapshots reuse their parsed data after metadata checks, avoiding repeated
file-body opens. Changed files are read in bounded chunks and checked for replacement or writes
during the read; unverified data does not replace confirmed progress. Each process retains its
own confirmed files even when the combined 96-row view previously hid them. Up to 16 process
files are read in numeric PID order; excess or invalid process records show partial/unavailable
status rather than a false complete result. This bounds the status viewer, not room creation,
file delivery or the length of a collaboration session.

Temporary lookup, upload, download and verification failures now keep the original edit pending
and retry automatically without requiring another edit. Delay grows from one second to a maximum
of 30 seconds. A sample allowance response respects the existing one-minute pause before checking
again. Failed requests stop the current file batch so another name for the same unavailable sample
does not immediately repeat the failed download. Completed files are reused by content hash.

The ordered native workers release an edit once its recoverable sample dependencies verify.
Later edits in that direction wait behind it, preserving channel/playlist ordering. FL Studio's
audio engine and local editing stay on their existing threads. Closing the sidebar or desktop app
does not stop these workers while FL Studio is running. Changing sessions cancels old pending work;
the current room's ordinary state synchronization determines recovery, rather than replaying an
old channel snapshot into a different project. Local destination, disk-write, flush and locked
replacement failures show **Waiting for folder access** and retain the edit for automatic retry.
Restore drive access/free space or release the file lock; retries do not require a new edit.
Existing source-file, size, memory and essential serialized-edit protections remain.

Downloads must match size and SHA-256 before their temporary files are installed in
the selected received-files folder (Windows Documents/flim/samples by default). Downloads flush
to disk before installation, use unique owned temporary files, and keep cache filenames within
FL's classic Windows path budget. Long names preserve a short extension; reserved Windows
names and trailing dots/spaces are normalized. The inbound preset parser substitutes the local path
without changing its channel target. Nothing copies whole sample packs or installs VSTs. Files
embedded in encrypted/compressed plugin formats remain outside the existing path extractor.
Older FL portable fallback still leaves unsupported sounds and playlist features unavailable.

[0.2.16 QA](QA-0.2.16.md) separates local native/HTTP tests from real cross-computer FL evidence.
[0.2.17 QA](QA-0.2.17.md) records sidebar discoverability, focus and window geometry checks.
[0.2.18 QA](QA-0.2.18.md) records search, filters, status recovery, input and layout refinements.
[0.2.19 QA](QA-0.2.19.md) records saved views, counts, ordering, keyboard and diagnostics refinements.
[0.2.20 QA](QA-0.2.20.md) records caching, verified status reads, setup/launch recovery and diagnostics fixes.
[0.2.21 QA](QA-0.2.21.md) records configurable destinations, local storage recovery and filename/UI fixes.
No fresh two-computer drag/drop or physical MIDI session was run for this update.

## 0.2.14 preview notes (historical)

This preview adds a collapsible Files drawer to Rooms and connects it to the native sample
transfer workers. It uses the existing Cloudflare relay and R2 file store. No SyncThing,
Node.js installation, account signup or shared-folder configuration is required on participants' PCs.
There are no server changes or deployment in this feature.

## Try it

Use [flim-rooms-0.2.14-files-preview-win64.exe](https://github.com/cashflowbeatz/flim/releases/tag/rooms-v0.2.23)
on both computers. This portable preview has a separate application identity and preferences
from the regular release. Its files are built separately from the existing release artifacts.

1. Create or join a room and open a supported FL installation through the preview.
2. Add an audio sample to a shared channel or supported playlist edit.
3. Open **Files** in Rooms to see checking, upload/download progress, ready files and failures.
4. Use **Open samples folder** to inspect received files. **Hide files** or Escape closes the drawer.

The native workers transfer samples whether Files is open or closed. The drawer only reads
local progress. Closing the drawer does not cancel an upload or download.

## What changed

- Sample references already captured by flim's channel/project/playlist paths now publish
  real per-file status. The transfer mechanism is reused rather than introducing a second
  folder synchronization service.
- Missing content uploads automatically to the relay's file store. The receiver downloads it
  into `%USERPROFILE%\Documents\flim\samples` and rewrites the shared file reference to that
  local copy. Only files referenced by supported shared edits are considered.
- Identical content under two source names uploads once. The receiver can reuse one verified
  cached file for both references within the same operation.
- Temporary HTTP/network failures retry once. Retries reset the displayed progress and show
  what is being retried. Retries are bounded to avoid an endless transfer loop.
- A download must match both its expected byte size and SHA-256 fingerprint before it becomes
  a usable cached sample. Partial or corrupt content is discarded.
- Download temporary filenames include the FL process ID, preventing two FL processes from
  writing the same temporary file.
- A failed replacement clears its previous path mapping, preventing the previous sound from
  being substituted as if the replacement succeeded.
- Ordinary sample HTTP 429/507 responses pause further sample requests for one minute while
  preserving the room connection. Existing stricter protections for an essential serialized
  shared edit remain in place.
- A source file's size and modification time are checked again after hashing so a file that
  changes during that read is not placed in the hash cache as a completed read.
- The optional status writer uses atomic snapshots, at most 96 recent entries per FL process,
  and throttled progress updates. A status-write failure cannot prevent sample delivery.
- Desktop reads only snapshots in the current native launch's owned directory, bounds input
  sizes and record counts, validates states, strips bidi controls, and returns only known
  fields. Full original paths are excluded from the drawer.
- Files polls once a second only while mounted and visible. Failed reads retain the last
  confirmed list; later successful reads recover automatically. Stale responses cannot bring
  a closed drawer back.
- The drawer sits beside the room at wider sizes and stacks below it on narrow displays.
  Long names wrap, long lists scroll, keyboard focus is restored on close, and window bounds
  respect monitor space and native zoom.

## Verification and limits

The native transfer loopback test passed with **one upload, seven download attempts and one
quota response**. It covers identical content under different names, retry, cache reuse,
corrupt content, wrong-size metadata, stale mapping removal, quota isolation, untouched source
audio and parsing real native status snapshots. It does not run FL Studio.

The existing native transfer runner also passed its eight checks: 1,025 references in bounded
batches, partial HTTP writes, streamed checksums, lookup retry, failed-batch rollback,
unrequested hashes, nonfatal lookup failure and stale-session cancellation. Relevant sample,
file safety, queue, inbound validation and plugin regression harnesses passed. The production
engine DLL and launcher compiled with MSVC /O2 /W3 without warnings.

**99 desktop unit/integration tests passed**, including current-launch ownership, unknown-field
filtering, inherited-environment isolation, status-setup failure, trusted IPC and folder action.
**11 browser checks passed**, covering every drawer control, status recovery, stale responses,
empty/loading/error states, focus, forced colors, reduced motion and widths 280/375/756/1200.
The packaged Electron smoke test passed four checks, including real preload/file IPC, native
payload hashes and 100%/200% zoom sizing. Its displayed room is a test fixture; it creates no
real room and launches no FL process.

Evidence generated locally:

- `test/evidence/file-sync/native.json`
- `test/evidence/file-sync/ui.json`
- `test/evidence/file-sync/package.json`
- `test/evidence/file-sync/desktop-files.png`
- `test/evidence/file-sync/packaged-files.png`
- `test/evidence/file-sync/packaged-files-zoom2.png`

Actual drag/drop from a real FL browser on two computers has **not** been verified in this
change. FL24/25/26 still require the existing exact supported profiles. Older participants
using the existing portable notes/basic-controls fallback retain its unavailable sounds and
playlist limitations. This feature does not remove those limitations or transfer VST binaries.

Sample-dependent edits retain the existing ordered worker queues and wait for their file
transfer attempts. There is no separate persistent retry scheduler: after the immediate retry
is exhausted, another shared edit referencing that sample can try again. A quota pause is
checked again by a later sample request after a minute. Existing file-size and relay/R2 limits
remain in force. The existing Framer bundle still produces Vite's large-chunk advisory; no
startup performance improvement is claimed.

## Reproduce locally

From `client/desktop`:

```powershell
npm test
npm run test:files:ui
npm run package:files
npm run test:files:package
```

From the repository root:

```powershell
cmd /c client\dev\harness\build-file-sync.bat
node client/dev/file_sync_test.cjs
```

The native test runners use loopback endpoints and temporary fixtures. Do not invoke
`t_transfer.exe` or `t_file_sync.exe` directly; they require the endpoint supplied by their
Node runners. No GitHub push or Cloudflare deployment was performed.

## Antislop delivery gate: PASS

The gate applies to the new Files drawer and its integration. Existing artwork, logo,
wordmark, font and room controls remain the source of visual direction. Screenshots were
opened and inspected in addition to programmatic layout checks.

- R-02 PASS: new drawer copy contains no em dash.
- R-03 PASS: browser assertions found no horizontal overflow at 280, 375, 756 or 1200 CSS pixels.
- R-17 PASS: file counts, sizes and progress are derived from native status; screenshot fixtures are identified as tests.
- R-18 PASS: no testimonials or fictional customer identities were added.
- R-23 PASS: the requested sidebar is the only added navigation; no new visual assets were created.
- R-24 PASS: Files, Hide files and Open samples folder have implemented destinations/actions.
- R-25 PASS: computed contrast is 5.36:1 for muted text, 7.29:1 for notice text and 14.14:1 for primary text; focus/progress charcoal is 9.22:1 against the drawer.
- R-26 PASS: UI tests click all drawer controls; main-process tests verify the trusted folder action.
- R-27 PASS: browser checks exercise loading, empty, unavailable, ready, transfer and failure states; failure copy gives a next action.
- R-28 PASS: no FAQ was added.
- R-32 PASS: focus, Tab, Escape and focus restoration were exercised; all drawer controls meet the existing 44px target height.
- R-33 PASS: feature behavior and styling are written directly in the source files.
- R-34 PASS: light styling and forced-colors mode were tested; no theme switch was introduced.
- R-35 PASS: production build, eleven browser checks and packaged 100%/200% zoom checks passed; native capture verified the zoomed surface.
- R-36 PASS: there are no unmeasured speed or security claims; live FL limits are stated above.
- R-37 PASS: DESIGN.md records the existing equipment identity and ENERGY 1 / RHYTHM 1 / MOTION 2 direction.
- R-38 PASS: the product shows actual status; test fixtures remain in test code.
- R-01 PASS: drawer uses a flat neutral surface to separate room controls from file status; no gradient or glow was introduced.
- R-04 PASS: file actions use plain labels; no generic icon set was added.
- R-06 PASS: existing General Sans preserves the app identity; tabular numerals help compare byte counts.
- R-07 PASS: no decorative background pattern was added.
- R-08 PASS: file actions have no decorative arrows.
- R-09 PASS: no promotional badge or capsule was added.
- R-10 PASS: no glass effect was introduced.
- R-12 PASS: no drawer shadow was introduced.
- R-13 PASS: no glow or decorative status light was introduced.
- R-14 PASS: file rows are sized by their actual status/help content; no feature-card template was added.
- R-19 PASS: file status has no perpetual animation; reduced-motion browser runs passed.
- R-22 PASS: the existing linked-computers/keyboard artwork remains; no generic illustration was added.
- Liveliness PASS: dials and Design Read are recorded; the large room number remains the focal point, spacing separates actions, and the equipment/wordmark supplies the existing accent and motif.
- C-1 PASS: DESIGN.md explains the drawer surface, grouping, sizing and plain actions.
- C-2 PASS: all new interactive controls were exercised.
- C-3 PASS: each drawer group corresponds to actual transfer work or a required recovery action.
- C-4 PASS: narrow layouts, long filenames, empty/error states, keyboard, forced colors and 200% native zoom were checked.
- C-5 PASS: product facts come from native status; runtime limitations are explicit.
- R-05 PASS: layout follows the room/file workflow; no generic dashboard sections were added.
- R-11 PASS: drawer has no outer circle, capsule or rounded-card frame.
- R-15 PASS: new actions say Files, Hide files and Open samples folder; the user-requested existing Get started label is unchanged.
- R-16 PASS: new drawer copy contains no marketing claims or buzzwords.
- R-20 PASS: existing music equipment artwork and room-first composition remain the identity.
- R-21 PASS: the existing white surface remains; no dark-mode default was imposed.
- R-29 PASS: the new surface uses the documented neutral palette and existing accent.
- R-30 PASS: no unrelated product layout was introduced.
- R-31 PASS: major layout/type/color decisions have one-line purposes in DESIGN.md.

UI and human skill supplements PASS: actual states have text labels, verified text contrast,
visible keyboard focus, real controls, bounded scrolling and zoom reflow. No decorative
metrics, avatars, images, extra menu or routine transfer confirmation was added.
