# FL Studio 24, 25 and 26 compatibility

**Any supported build can host.** Use the current complete Rooms app on every computer and a
relay that negotiates `portable_sync:1`. The release picker shows the FL family; **Installation
details** shows the exact build and folder. Short labels do not bypass native checks.

## Reviewed installations

| FL family | Reviewed installed build | Current status |
| --- | --- | --- |
| FL Studio 24 | **24.2.2.4597** | Supported, with FL24 format adapters and an exact engine fingerprint check. No experimental opt-in. |
| FL Studio 25 | **25.2.5.5319** | Supported, with the reviewed build profile. |
| FL Studio 26 | **26.1.7.5654** | Supported in the complete Rooms package, with the reviewed build profile. |

These are the reviewed installations for release 0.2.25, not a promise of support for the latest
version Image-Line may release later. Retained mappings for **25.1.6.4997**, **25.2.3.5171** and
**26.1.6.5639** have earlier/static evidence; they were not rerun live in the latest sweep.
Unknown patches need their own mapping, identity review and tests. Use **Check again** after an
FL update; an unsupported result calls for a verified profile, not disabling checks.

## Which version can host which?

| Host ↓ / Member → | FL24.2.2.4597 | FL25.2.5.5319 | FL26.1.7.5654 |
| --- | --- | --- | --- |
| **FL24.2.2.4597** | Project-sharing path | Project-sharing path | Project-sharing path |
| **FL25.2.5.5319** | Notes/basic controls | Project-sharing path | Project-sharing path |
| **FL26.1.7.5654** | Notes/basic controls | Notes/basic controls | Project-sharing path |

**Project-sharing path** means flim can use its normal initial project-loading flow. It does not
guarantee identical sound: the receiving FL and plugins must read the project and have the required
content. **Notes/basic controls** means an older receiver joins without trying to load a newer
host's project or opaque presets. These are protocol/format paths, not nine newly proven live sessions.

An older receiver's blank channels can receive notes, channel names, pattern metadata, supported
controls and mixer names/colors/routing. Supported edits flow both ways. Unsupported sounds,
newer mixer tracks and playlist features stay unavailable. Before preparing this workspace,
flim saves a recovery copy and uses a separate session path. Preservation failure stops the operation.

Only the host replaces the shared project. Older fallback members cannot publish playlist
snapshots. Legacy rooms retain their negotiated policy; update everyone and create a fresh room.

## MIDI, concurrent edits and plugins

Recording a MIDI keyboard into the piano roll creates shared notes for hosts and members. Stable
note identities and retained deletions (a CRDT) merge duplicate deliveries and concurrent edits.
Pending local recording survives an arriving remote delta. This is recorded-note sharing, not
unrecorded live MIDI monitoring or audio streaming. Structure, presets and projects use ordering/locks.

A missing/rejected plugin retains shared state while other supported editing continues. Automatic
retry waits for matching installation evidence to change. Stable VST2 unique IDs and VST3 class IDs
can identify the same plugin at another path or vendor version label. Different IDs, VST2-to-VST3
substitution and name-only guessing are refused. The installed plugin must accept the saved state.
Older FL does not load a retained preset marked as newer than its supported source format.

## Evidence and limits

The latest sweep passed 31 native harnesses, including **nine synthetic host/member pairings**,
CRDT convergence cases, control validation and FL24 adapters. Earlier live initial-sync/editing
observations are in [0.2.11 QA](client/desktop/QA-0.2.11.md), separate from current fixture results.

There was no fresh physical MIDI, two-computer, live VST-version or drag/drop test in 0.2.23.
No arbitrary patch support, project downgrade or guarantee against every crash is claimed.
The recovery-save stall observed during earlier FL24 testing remains a limit to investigate;
synthetic tests do not resolve it. A native fault pauses sharing and leaves FL open.

[Current QA](client/desktop/QA-0.2.23.md) · [Testing](docs/TESTING.md)
· [Historical evidence](client/dev/compatibility/README.md)
· [Adding a reviewed build](client/dev/README.md#adding-an-fl-build)
