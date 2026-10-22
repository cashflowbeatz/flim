flim file companion (opt-in)
===========================

Syncthing can move saved projects and sample libraries in the background while
flim handles live editing. This companion publishes immutable snapshots, waits
for complete arriving files, verifies them, and prepares a separate local FLP.
It does not install/configure Syncthing, start FL Studio, or change flim's relay.
Python 3.10 or newer is required; only the Python standard library is used.
Keep client/files/companion.py and client/dev/flp.py in the supplied layout.

Choose a dedicated Syncthing folder on each computer, for example D:\FlimShare.
Share it only with collaborators you trust. The companion reads no Syncthing
API keys and needs no access to its web UI. Configure the share in Syncthing
yourself. Sender-only and receiver-only folders suit one-host handoff; send and
receive permits each collaborator to publish their own digest-named snapshots.
Do not place your active project, autosaves, local prepared projects or flim's
sample cache inside the shared folder. Do not sync a live FLP in place.

In Syncthing's ignore patterns for this dedicated folder, add:

    /.flim-staging

That prevents temporary publisher copies from being transferred. The companion
does not write .stignore or change an existing daemon's configuration. Objects
and manifests remain usable if delivery order differs; preparation verifies all
objects rather than trusting the presence of a manifest or Syncthing's status.

Run these commands from the repository root. Replace paths and exact builds
with your own. The examples use the currently tested FL 26 build.

1. Save a project outside the share, then publish a snapshot:

    python client/files/companion.py publish --share "D:\FlimShare" --project "D:\Music\Song.flp" --build 26.1.7.5654

   The JSON result includes a 64-character snapshot ID. The project build is
   also read from the FLP; --build is an optional additional check at publish.
   The source FLP and samples are copied, never modified or hard-linked. Save
   and close the project before publishing for a consistent point-in-time copy.
   Detected file changes during copying abort publication. Files already
   published remain immutable; a new saved version gets a different snapshot.

2. On the receiving computer, list manifests that have arrived:

    python client/files/companion.py status --share "D:\FlimShare"

   Status is a local inventory; it does not query the daemon or verify objects.

3. Prepare a specific snapshot (use the ID returned by publish/status):

    python client/files/companion.py prepare --share "D:\FlimShare" --snapshot <ID> --local-root "D:\FlimPrepared" --build 26.1.7.5654

   Every project and asset is checked for full SHA256 and size, then copied.
   Standard sample paths in the prepared FLP are rewritten to verified local
   cache paths. The returned project path is safe to open manually in the
   selected matching FL build. The original FLP, unrelated events, escaped
   32-bit event IDs and trailing bytes are preserved.

   On Windows the default sample cache matches the existing native engine:

       %USERPROFILE%\Documents\flim\samples\<first16SHA256>\<sample basename>

   --cache-root can select a separate cache for isolated tests. Using a custom
   cache still produces a locally rewritten FLP, but only the default engine
   directory prewarms flim's normal native sample cache. Both cache and local
   project directories must be separate from the shared folder.

4. Or leave a background watcher running:

    python client/files/companion.py watch --share "D:\FlimShare" --local-root "D:\FlimPrepared" --build 26.1.7.5654 --interval 3

   Watch polls local files, retries missing/incomplete arriving objects and
   prints a project path once it is ready. Each successful snapshot is handled
   once per watcher run. A restarted watcher verifies/reuses existing copies.
   It never opens FL or processes the local output as a new shared snapshot.
   Ctrl+C stops it. --once makes a single scan, useful for a script/task.
   A normal continuous watcher intentionally waits across failed transfers.

Keep sessions on one exact FL build. For FL 25 use, for example,
--build 25.2.5.5319 on both publish and prepare/watch. A build mismatch aborts
preparation; this tool does not convert a FL 26 project into FL 25 format.
New same-major FL builds can be staged only when the saved project's exact
build matches --build; native flim support is still a separate build-table gate.

Sample coverage and limits
--------------------------

Automatic discovery covers nonempty standard FLP sample-path events (ID 196),
including absolute paths, paths relative to the source project, and the macros
%ILSharedData% and %FLStudioUserData%. On Windows the Image-Line shared data
root is read from HKCU\Software\Image-Line\Shared\Paths, value "Shared data",
falling back to %USERPROFILE%\Documents\Image-Line. No other registry values
or personal settings are read. --shared-data can supply that root explicitly;
it must be the Image-Line root containing the "FL Studio" directory.

Unknown FL macros, missing standard references and inaccessible samples block
publication. --skip-missing explicitly publishes an INCOMPLETE snapshot;
prepare/watch additionally require --allow-incomplete to use it. Skipped paths
remain unchanged and can be missing when opened. This is never a guarantee
that all project assets are available.

Third-party plugins may hide sample references inside compressed/proprietary
state. This companion does not discover or rewrite those plugin formats.
You can repeat --extra-sample "D:\Library\Instrument.wav" to transfer known
assets and preseed the cache, but a plugin can still require manually relinking
that asset. Plugin binaries, licenses and factory libraries are not replicated.
The coverage field explicitly describes standard-event196-and-explicit-extras;
even a complete standard reference set does not certify arbitrary plugin state.

Supported basenames use the engine's invalid-character-to-underscore mapping.
Windows reserved DOS names, trailing dots/spaces, and names over 120 UTF-16
units are rejected; rename those assets before publishing. This avoids quietly
prewarming a different native cache path. Prepared native cache paths must fit
the engine's 259-character limit. Hash prefix collisions or conflicting cache
files cause an error rather than replacement.

Bounds: project 64 MiB, sample 1 GiB, total snapshot 8 GiB, manifest 2 MiB,
4096 sample references/assets, 1000000 parsed FLP events, and 10000 snapshot
manifests per share. Source/project/object paths must use regular local files;
symlinks and Windows reparse points are rejected in path components. Immutable
installation requires a filesystem supporting hard links (NTFS works); the
links are made from private temporary COPIES, never from a user's source file.

Local prepared projects live at local-root\snapshots\<ID>\project.flp. Save As
to a separate working project before editing. Re-preparation verifies an
existing prepared copy and refuses to overwrite local edits or cache conflicts.
Move/rename an edited prepared directory if you want a fresh preparation.
After moving a corrupt cache file aside, prepare can restore it from the share.
The tool performs no automatic deletion/garbage collection of shared objects.
Digest checks detect corruption; they are not signatures authenticating a
publisher. Shared project files can contain private paths and project content.

Output and checks
-----------------

Commands print JSON (watch uses JSON lines). Put --text before the subcommand
for readable key/value output. Success exits 0; errors exit 1; a single prepare
waiting for an object exits 2. watch --once exits 1 if any snapshot waits or is
rejected. Successful incomplete publication/preparation requires explicit
flags and remains labeled incomplete in output. No command launches FL Studio.

Run regression checks from the repository root:

    python -m unittest discover -s client/files -p test_companion.py -v
    python -m unittest discover -s client/dev -p test_flp.py -v

The live-operation relay and its exact-build guard remain in use. This is a
file preparation companion, not an alternate live synchronization protocol.
