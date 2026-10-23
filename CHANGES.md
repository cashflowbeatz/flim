# What changed in this fork

These changes add experimental support for specific FL Studio 26 builds, keep supported FL Studio 25 builds available, and fix problems with shared projects, edit ownership and sample transfers.

In this document, **client** means the flim program running with your FL Studio. **Server** means the program that passes edits and sample files between you and your friends. An **edit lock** is a temporary reservation that stops two people from changing the same control at the same time. A **cache** is temporary file storage that can be cleared to make room.

## 1. FL Studio 26 support, with FL Studio 25 support retained

FL Studio updates can move the internal functions and data that flim uses. The old address list cannot safely be reused just because the program still looks similar.

The fork now uses a separate verified address list for each supported build. The launcher and the program loaded inside FL agree on which builds are supported. When a selected build is unsupported, the launcher offers to open ordinary FL without flim instead of guessing how to modify it.

| FL Studio build | What was checked |
|---|---|
| **26.1.7.5654** | The currently installed FL 26 build. Full 31-check live feature runs passed before the latest server-only transfer changes; fresh sample-transfer checks also passed with the latest server. |
| **25.2.5.5319** | The currently installed FL 25 build. The same client passed the corresponding full feature runs and fresh sample-transfer checks. |
| 26.1.6.5639 | Its support table is retained. Earlier live results exist, but that installation was replaced by the update and was not rerun with the newest client. |
| 25.2.3.5171 and 25.1.6.4997 | Their support tables are retained and passed simulated hook-selection checks. These older builds were not rerun live after this update. |

The experimental Windows client supports the builds listed above. The normal build contains the three FL 25 tables. Building with `FLIM_EXPERIMENTAL_FL26=1` includes FL 26; see [client build instructions](client/README.md).

**Everyone in one session must use the same exact FL build.** For example, two people using 26.1.7.5654 can test together. One person using FL 26 and another using FL 25 cannot share that session. Backward support means you can choose a supported FL 25 installation; it does not make FL 26 project files compatible with FL 25. Future FL updates still need their own checks.

## 2. Project files and presets are read more reliably

Some saved FL files use a longer form of an event identifier. The old reader misunderstood it, which could hide sample paths from flim's transfer system.

Both project readers now recognize that form, check that the file has enough data before reading it, and preserve event data they do not otherwise interpret. A corpus check successfully read **8,738 installed presets and projects**, including 128 extended events.

Incoming project data is also checked for a valid, consistent FL version before flim rewrites sample paths, writes a local project copy or asks FL to load it. Missing, malformed or conflicting version information is refused. This reduces the chance of loading a project that the receiving FL build cannot safely interpret.

## 3. Received plugin presets stop fighting with local monitoring

While FL loads a received plugin preset, it can briefly report default parameter values. Previously, flim could mistake those temporary values for your edits and send them back to your friend. That could make a newly loaded preset revert or change unexpectedly.

The client now tracks which loading operation an observation belongs to. Observations and queued changes from an older loading operation are discarded. This keeps temporary intermediate values from overwriting the received preset.

This was tested with controlled loading cases and live effect loading. It is not a promise that every third-party plugin has been tested.

## 4. Downloaded samples can be shared again after saving

FL sometimes saves sample locations using the `%ILSharedData%` shortcut instead of a full Windows path. Previously, flim could lose track of a downloaded file when that shortcut appeared in a later save.

The client now resolves the shortcut using FL's configured shared-data folder. A sample that was downloaded, saved and referenced through that shortcut can remain available for the normal transfer process.

Samples stored inside compressed or encrypted plugin data, including some Kontakt content, remain a limitation.

## 5. Switching projects clears the previous project's edit locks

Imagine someone reserves a control in Project A, then the host opens Project B. The new project may reuse the same internal control number. Previously, the old reservation could block an edit in Project B until it timed out. One older project-switching path cleared the server's reservations without telling the clients.

Both project-switching paths now notify clients that the old locks have been released before sending the replacement project. The host is notified too. The new project can immediately reuse those control numbers. Normal full-state updates preserve existing locks because they do not represent a different project.

Only an accepted project replacement from the host clears these locks. A member's rejected replacement request cannot clear another person's reservations.

## 6. Expired edit locks stop blocking people

A lock is supposed to expire after 30 seconds without a valid renewal. Previously, it could keep blocking edits until a later cleanup timer ran. Some traffic could revive an already expired lock, rejected edits could keep other locks alive, and new participants could receive stale reservations.

The server now checks expiry when a lock is used or sent to a new participant. Accepted edits renew active locks; cursor movements and rejected edits do not. Expired entries are also removed before applying the limit on how many active locks a session may hold.

The practical effect is fewer situations where a control stays marked as occupied after its reservation should have ended.

## 7. Only the host can replace the shared project or complete state

The clients treated certain whole-project and whole-state messages as host instructions, but the old server could forward those messages from any member. That could leave the host and members looking at different results.

The server now rejects these replacement messages from members. Members can still make ordinary supported edits and request the host's current state.

The server also rejects ambiguous control fields hidden inside another message. This matters because the existing client recognizes some instructions by searching the incoming text. The server checks prevent a nested or disguised field from being interpreted as a different instruction. Replacing the client's text-search parser with a complete parser remains separate work.

## 8. Sample transfers handle disappearing files and session expiry

Several failures happened between checking a file and actually using it:

| Before | Now | Why it matters |
|---|---|---|
| A new upload could be deleted by the cache cleanup that its own completion triggered. | That cleanup pass protects the file that just finished uploading. | A successful upload is not immediately undone by its own cleanup. Later uploads can still evict it. |
| A download could announce success before opening its file. If cleanup removed the file in that gap, the connection reset. | The server opens the file and checks its metadata before announcing success. A file already gone gets a complete "not found" response. | Clients receive a clear result instead of a broken transfer. |
| A download could lose its path after the file was opened. | The transfer reads through the already-open file handle. | Removing the cache path does not interrupt those opened bytes. |
| A session ending while a download's file lookup was pending could still receive the file. | The server rechecks the session before sending success headers or sample bytes. | Work waiting on disk does not continue as though the session were still active. Transfers already streaming are not revoked by this change. |
| A sample-list request could finish after its session ended. | Session validity is checked again after its request body arrives. | The server refuses the expired request before checking the old storage. |

Disconnected requests and failed file checks close their opened handles. Downloads and file-size requests now perform their initial file lookup asynchronously, allowing the server to handle other work while that lookup is pending.

## 9. Sample storage limits and cleanup are more dependable

Uploads are streamed to disk and checked against the promised content hash. A hash is a fingerprint of the file's bytes; this check detects an upload whose contents do not match the requested file.

An upload must fit both the individual-file limit and its session's cache limit. In-progress uploads reserve their declared size against the total storage budget, so several simultaneous uploads cannot each claim the same free space. Failed or disconnected uploads clean up their temporary files and release their reservations.

A regression also covers two people uploading the same sample: a late disconnect after a file is committed must not delete a valid copy used by the other upload.

Cleanup preserves the configured parent folder and unrelated files. Sample responses tell intermediaries not to cache this private session data. Keep one server process per cache directory, as described in the [server instructions](server/README.md).

The cache remains temporary storage. Keep your own project and sample originals.

## 10. Less repeated work, with clearer performance limits

- **Repeated sample references:** if a project lists the same sample fingerprint several times, the server checks that file once and lists a missing file once.
- **Plugin monitoring:** the client counts activity in one pass instead of repeatedly recounting changes for each plugin. This reduces work in that specific monitoring loop.
- **Sending to several friends:** the server prepares an outgoing message once and reuses it for the recipients, instead of recreating the same text for each person.
- **Slow connections:** a peer's queued outgoing data is limited, so a connection that stops reading cannot grow its server queue indefinitely.
- **Download lookup:** initial file opening and metadata checks no longer use a synchronous path-stat call on the server's main event loop.

Local measurements showed improvements in the monitoring loop and message-preparation work. Those are measurements of individual pieces, not evidence of a particular increase in FL's frame rate, lower audio latency, or faster internet transfers. Upload storage accounting and cache eviction still scan files synchronously and remain an area for further work.

## 11. Startup, connection handling and developer tests are safer

The launcher checks whether loading flim into its newly started FL process succeeded. Unsupported builds and failures are handled more deliberately, including long installation paths. Incoming network messages have bounded buffer growth, and connection cleanup was tightened.

Invalid server settings, such as an impossible file-size limit or a nonnumeric timeout, now stop startup before cache cleanup or listening begins. Proxy headers are trusted only in the configured proxy mode; Cloudflare Tunnel mode expects a local origin connection.

Developer tools require explicit reference files or test-process selections, reject ambiguous address matches, and report failed checks with a failing exit status. The restart tooling checks a saved process's number, creation time and executable before stopping it, so an unrelated FL session is preserved.

Server dependencies were updated. The container configuration uses Node.js 24 and a non-root user. The Docker image itself was not built in this testing pass.

## 12. No separate Syncthing integration

The optional Syncthing file companion was removed from the current source and packages. flim's existing project and sample transfer remains the supported path in this fork. No personal Syncthing installation, device pairing, shared folder or configuration was changed.

## What testing actually established

| Checkpoint | Results | What this does and does not mean |
|---|---|---|
| Current server transfer changes, commit `a4ae092` | **44 Node tests and 58 session checks passed.** | Includes local HTTP and WebSocket tests for transfer races, cache limits, lock handling, session separation and existing behavior. |
| Current server with the installed FL builds | **2 focused live checks passed per build**, on 26.1.7.5654 and 25.2.5.5319. | A receiver with local lookup disabled downloaded a fresh synthetic sample, and the saved channel state referenced matching cached bytes. This was not a new full feature run. |
| Previous server revision, commit `a8513d4` | **31 live feature checks passed on each current build; 36 Node tests, 58 session checks and 48 Python tests passed.** | Full feature coverage included project joins, parameters, mixer settings, channels, notes, markers, effects, reordering, clips and samples. |
| Earlier client compatibility work | Both normal and experimental clients built; parser, launcher, buffer, preset-race and supported-build checks passed. | Detailed records identify the binaries and scope of each historical run. |

Three selected transfer scenarios failed before the latest fixes and passed afterward. Tests deliberately control timing and file callbacks to reproduce rare races. This gives repeatable evidence; it does not mean every possible bug is fixed.

Live checks used disposable same-build FL pairs and loopback servers. All owned test processes were stopped by their saved identities. Installed FL files were not patched. Long sessions, broad third-party plugin coverage, remote-network performance, audio-render comparisons and Linux runtime behavior remain untested in the latest pass.

See the [compatibility record](client/dev/compatibility/README.md), [original source audit](server/AUDIT.txt), [project-lock audit](server/CONTINUED-AUDIT.txt) and [sample-transfer audit](server/TRANSFER-AUDIT.txt) for evidence and limitations. The JSON audit files preserve the state at their own checkpoints; their `pushed: false` entries describe when the tests were recorded, before GitHub publication.

## What you need to update

1. **For FL 26 support and client fixes:** build the experimental Windows client and keep `flim.exe` and `flim_engine.dll` together. All participants need the same exact supported FL build and the required plugins.
2. **For the relay and sample-transfer fixes:** run the revised server from this branch. Updating only the client does not update an older server.
3. **For friend testing:** start with project copies and a server you control. [Private testing instructions](server/DEPLOYMENT.md) explain the server and Cloudflare Tunnel setup.

The supplied server targets **Fly.io**. This fork does not contain a Cloudflare Workers backend. Cloudflare Tunnel can provide the public connection to a separately running Node.js server, but account setup and deployment are still separate steps. Publishing this code on GitHub does not deploy it, update the upstream public relay or create a new downloadable release.
