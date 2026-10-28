# Note protocol 1

Deployed October 27, 2026 with Worker `ca9554a7-76d6-40b1-9f48-995f6f0e2618`. Updated clients
negotiate this mode; legacy rooms retain locked snapshots. Exact native and live verification
is in [QA-0.2.11](../../client/desktop/QA-0.2.11.md).

The native host/join hello offers `notes_crdt: 1` alongside compatibility and file transport.
A room created with that offer persists `notesCrdt: true`. Its welcome and presence responses
include `notes_crdt: 1`; members must offer it before admission. An absent acknowledgment selects
the legacy format. A mode change during an admitted session is an error.

Only `notes.set` and `notes.full` change format. Each pattern item has `pat`, `crdt: 1` and base64
`data`. CRDT deltas omit the whole-pattern lock key. The relay verifies the mode marker for inline
edits. Stored edits retain operation/build policy and sequence metadata; the native receiver
validates the decoded body and mode. The relay forwards changes; it does not merge FL state.

The binary header is 32 bytes: `FLIMNC01` (8 bytes), scope (16 bytes), little-endian entry count
(4 bytes) and zero reserved bytes (4 bytes). Entries are 48 bytes: immutable ID (16), canonical
FL note record (24), dead flag (1), and zero reserved bytes (7). Entries are sorted by nonzero
unique ID. The note's local selection bit is cleared. A full snapshot includes every pattern's
scope, even an empty one. The native full-state parser validates all item encodings before queuing.

Merge joins identities and ORs deletion flags. Reordering/repeating a delta cannot resurrect an
observed deletion. Reusing an identity with different note content fails atomically. Duplicate
musical notes retain distinct identities. A move or velocity change is remove plus fresh add;
concurrent moves can leave both fresh versions. Pending FL edits are captured before incoming
changes are merged. Applying the materialized result retains identical FL records in place,
preferring selected duplicates, and mutates only unmatched records.

Initial scopes derive from a SHA-256 hash of canonical sorted notes. Baseline identities use the
scope prefix and index; new additions use BCrypt random 128-bit IDs. Structural refreshes keep
existing state when its materialized notes are unchanged. Changed baselines chain the prior scope
with the new canonical digest, avoiding a retired scope when visible notes return to an old value.
Only an authoritative host full snapshot may establish a different scope at a receiver. A delta
from another scope stops sharing and requires resync.

Within a baseline, tombstones are retained. A pattern is limited to 16,384 history entries; the
client aggregate is limited to 262,144. Reaching a bound, failed allocations or random-ID
generation, invalid records, or incompatible baselines stops sharing with a diagnostic. A fresh
room resets the history. There is no offline queue or automatic tombstone compaction.

Plugin state, project replacement and structural operations keep their existing compatibility
and host policies. This protocol does not provide CRDT semantics for the whole project, unrecorded
MIDI audition, audio streaming or universally supported FL builds. Installed FL24/25/26 pair
tests cover the note/control paths, with simultaneous insertion verified over the public relay.
Those tests insert notes through FL's own functions; physical keyboard input is not exercised.

Portable sync uses host-only, recipient-targeted `portable.start`, `notes.full` and `state.full`
baselines. A full note baseline includes empty pattern scopes but avoids native array creation
when both sides are empty. File-backed references retain the intended recipient through R2
upload/download; the receiver validates the reference against the decoded operation. Joining
one participant does not reset existing peers. Room capabilities persist across Worker restarts
and cannot change within an admitted session.
