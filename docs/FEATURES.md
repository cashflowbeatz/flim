# What Rooms adds to flim

The original flim engine shares edits between separate FL processes. Rooms puts setup,
invitations, installation selection and file activity into a compact Windows app.

## A small app for the session

- **Anonymous rooms:** create/join with a name and eight digits, with up to eight participants
  subject to relay capacity. No email or signup.
- **One executable:** the portable app includes its native launcher, engine, profiles and taskbar
  icon. Friends do not install another client.
- **Simple FL selection:** one entry for FL24, FL25 and FL26, exact details, custom folders and
  rescan after updates. Unsupported copies explain the problem.
- **A fitted window:** a 400px room surface expands to 680px with Files beside it. At high zoom,
  Files uses the available surface. Native minimize/close controls remain.
- **Predictable navigation:** Back collapses setup; a visible Files arrow opens the sidebar.
  Keyboard switching, paste, input composition, reduced motion and forced colors are tested.
- **Truthful status:** waiting/open/ended/offline and stale people lists are distinct. People
  appearing does not claim project synchronization or sound playback succeeded.

## Shared notes and supported edits

**Recorded MIDI and piano-roll editing.** Hosts and members add, remove, move and record notes.
Note identities and retained deletions let CRDT deltas merge without resurrecting deleted notes
after delayed delivery. Duplicate notes at the same position remain distinct notes. Pending local
recording survives incoming remote deltas. CRDT scope is notes, not the whole DAW, files or audio.
Unrecorded keyboard audition and live audio are not broadcast.

**Controls and structure.** Supported parameters, channels, patterns, names, colors, time
signatures, mixer state/routing and compatible playlist data use ordering and temporary locks.
Locks release when owners leave or projects change. Queues are bounded and inputs validate
before native setters; repeated waiting values coalesce and apply in short batches.

**FL24/25/26 together.** Reviewed builds can join in any host order. Older receivers share
notes/basic controls when they cannot read newer projects. Unsupported content stays unavailable;
this does not convert newer `.flp` files. [Compatibility](../BUILD-COMPATIBILITY.md).

**Missing/different plugins.** Missing or rejected sounds retain remote state while supported
editing continues. Stable VST identities can resolve a scanned local path in a loading copy.
Vendor labels alone do not determine identity; the plugin must accept the state. Conflicting or
zero IDs, ambiguous installs and cross-format substitutions are not guessed. Automatic retry
waits for changed scan evidence.

## Referenced samples arrive in the background

A supported channel/playlist edit with an extractable sample path checks which content is
missing, uploads/downloads it through the relay, verifies size/SHA-256 and substitutes the local
path. Ready content is reused by hash. Unrelated folders and whole sample packs are not mirrored.

Choose the **received-files folder** before a room. Folder access/write/flush/locked-target
failures retain the waiting edit for retry. Network failures back off. Pending work belongs to
its session, preventing old completions from overwriting a newer project.

Files shows direction, byte progress, status groups, search and recent ordering. It preserves
confirmed status through incomplete reads. Hidden panels stop polling; native delivery continues
while FL runs. Bounded activity lists limit display, not delivery. Encrypted/compressed plugin
formats remain outside the sample extractor. [Transfer details](../client/desktop/FILES-SHARING.md).

## Cloudflare hosting and recovery

Workers handle requests, a SQLite Durable Object orders edits/manages rooms, and private R2
stores samples and large payloads. There is no application 15-minute cutoff or daily room quota.
The current policy permits four simultaneous rooms, eight people each, including the legacy slot.
The host must stay connected.

A persistent global budget guards R2 storage and monthly transfer attempts. Defaults are
**512 MiB**, **20,000 upload attempts** and **100,000 download attempts**. Burst/memory guards
remain. These safeguards do not cap account-wide Cloudflare billing. Other workloads/platform
charges still matter. TLS protects transit; the relay can read session data.
[Operator setup](../server/cloudflare/README.md).

Settings tolerate brief Windows locks and preserve the previous choice on failed saves. Slow
requests are bounded, duplicate clicks share work, stale replies cannot replace newer sessions,
and status retries back off. Renderer recovery restores controls with a limit against crash loops.

**Open test logs** opens local rotating diagnostics; nothing uploads automatically. Native logs
may include names/paths, so review before sharing. Serious native faults stop sharing without
intentionally closing FL or claiming failed edits were synced.

[39 latest fixes](../client/desktop/QA-0.2.23.md) · [History](../CHANGES.md) · [Testing](TESTING.md)
