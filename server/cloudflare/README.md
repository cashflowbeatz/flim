# Cloudflare relay for flim Rooms

Workers handle requests, a SQLite Durable Object orders edits/manages rooms and locks, and a
private R2 bucket holds samples and large payloads. Participants do not run server software.
Node.js is only needed for development/deployment.

Rooms 0.2.25 adds authenticated WebRTC signaling at `/peer/<room>`. Direct sample bytes go
between the participants; Cloudflare still orders edits and R2 retains the initial sample
backup and fallback download. No TURN subscription is configured. [Direct file architecture,
limits and tests](../../docs/PEER-FILES.md).

Current clients negotiate [note CRDTs](NOTES-CRDT.md) and portable sync. Any supported FL24/25/26
build may host; older receivers use notes/basic controls instead of loading newer opaque data.
Exact per-build checks remain. [Compatibility](../../BUILD-COMPATIBILITY.md).

The desktop app creates eight-digit rooms without an account. Up to four rooms can run at once,
with eight people in each. The original private room `01` still works and uses one of those four slots.
There is no 15-minute cutoff, idle-session cutoff or daily room quota. The host must stay connected: leaving ends the room. A dropped connection
requires reconnecting and resyncing from the host. The relay orders edits and manages locks;
current clients merge note CRDTs locally. This does not add offline collaboration or project
version conversion.

## Recovery fixes

The current local source restores missing reservation/cleanup alarms when the Durable Object starts,
and schedules another cleanup attempt after an R2 deletion fails. A failed deletion retains its file
reservation until deletion succeeds. If a download finds its R2 object missing, it removes the stale
tracking row so a subsequent missing-file lookup can request a new upload. Monthly transfer attempts
remain counted and the shared storage cutoff is unchanged. The current 76-test emulator suite
also covers scoped pending-room cancellation, authentication and its race with host admission.

These changes are included in the October 27 deployment. Live R2 checks verify private access,
checksum rejection, transfer and attempt accounting. Emulator tests exercise exhausting the
limits without consuming production allowances.

## Desktop room API

All four endpoints accept `POST` with `Content-Type: application/json`. Bodies are limited to
1 KiB, credentials stay out of URLs, and responses are private and not cached. The app makes these
requests from its desktop process; cross-origin browser access is not enabled.

| Endpoint | Body | Successful response |
|---|---|---|
| `/rooms` | `{}` | `201`: `room`, `password`, `hostToken`, `expiresAt`, `websocket`, `maxParticipants` |
| `/rooms/join` | `{ "room": "12345678" }` | `200`: `room`, `password`, `websocket`, `maxParticipants` |
| `/rooms/status` | `{ "room": "12345678", "password": "issued password" }` | `200`: `room`, `state`, `peers`, `maxParticipants`; waiting rooms also include `expiresAt` |
| `/rooms/cancel` | `{ "room": "12345678", "hostToken": "issued host token" }` | `200`: `{ "released": true }`, only for the matching pending reservation |

Cancellation uses the same serialization queue as the
host handshake, so it cannot end an active room or cancel a different/reused reservation.
Unavailable/active reservations and wrong tokens return `404 room_unavailable`. Cancellation
releases the pending slot. Desktop return waits at most three seconds and then
explains automatic expiry if the deployed relay lacks this route or is unavailable.

`websocket` is `/room/<eight-digit code>`. A host sends the existing transport-2 `host` handshake
with `owner: hostToken` and the issued password. A member sends `join` with `session: room` and the
issued password. The welcome message echoes the code. Root `/blob/<hash>` and `/blobs/missing`
requests still use the per-session `x-flim-key`; this key resolves only that room's files.

A room ticket expires after ten minutes if the host has not connected. Tickets are stored as hashes,
are valid only for the issued room and password, and are consumed on host admission. The desktop
keeps credentials in its main process; friends only need the eight-digit code. Anyone who knows a
running room's code can join, so share codes privately. No hosting key is included in the application.
The `/usage` endpoint and legacy `01` hosting still require the owner's separate private hosting key.

Join returns `409 room_not_ready` while the host is opening FL, `404 room_unavailable` for an ended
or unknown code, and `409 room_full` at eight participants. Create returns `409 relay_full` when all
four slots are reserved/running. There is no daily room creation limit, including legacy `01` hosting.
Status requires the issued password and exposes only that room's participants. The desktop app polls
waiting rooms every five seconds and active rooms every thirty seconds, pauses while hidden/offline,
and stops on an ended room. Requests also remain subject to the request protections below.

Creation requests use a short burst limit: ten attempts per IP and sixty globally per UTC minute,
then become available again in the next minute. Existing daily creation counters do not block
rooms after an update. Daily API request budgets still bound code guessing: join allows one
hundred per IP and two thousand globally,
authenticated status requests allow ten thousand per IP and fifty thousand globally, and cancellation
allows one hundred per IP and five hundred globally. Exceeding
one returns `429 room_rate_limit`. WebSocket admission also counts all attempts, including unknown
room codes: two hundred per IP and four thousand globally per UTC day. Reaching that allowance
returns the existing `rate_limit` WebSocket error. Existing admitted sessions continue to work.
The code is an invitation, not a durable account or recovery identity.

All rooms remain in the existing `private-room-01` Durable Object. File byte reservations, file
counts, daily transfers and monthly R2 usage are global: creating another room cannot multiply or
reset the cutoff. Ending a room revokes its files and aborts only its own uploads. Other rooms keep
running. The old stored private room migrates without changing its file-generation or accounting.

## What you need to update

Every participant can use the complete `flim-rooms-0.2.6-win64.exe`; its native client already
includes the Cloudflare transport. No separate Cloudflare installation is needed. An older native
client is rejected before receiving the room's project or file key. Updated clients also work with the
existing Node relay: the file-envelope transport turns on only when the server negotiates it.

FL build restrictions still apply. Use the oldest supported build as host. The installed builds
used in local testing are FL 25.2.5.5319 and FL 26.1.7.5654. FL 26 remains experimental and requires
`FLIM_EXPERIMENTAL_FL26=1` when building the client. Unknown builds require reviewed profiles and
runtime tests. See [build compatibility](../../BUILD-COMPATIBILITY.md).

## Limits for small private tests

| Limit | Value |
|---|---:|
| Concurrent rooms / people per room | 4 / 8 |
| Inline message | 128 KiB |
| Serialized edit stored as a file | 64 MiB |
| Individual sample file | 90,000,000 bytes |
| Aggregate file ledger, including uploads and cleanup backlog | 512 MiB / 2,000 files |
| New uploads / downloads per UTC day | 1,000 / 5,000 |
| R2 upload attempts / download attempts per UTC month | 20,000 / 100,000 |
| New rooms per UTC day | No application limit |
| Creation request attempts per UTC minute, per IP / globally | 10 / 60 |
| Unstarted room reservation | 10 minutes |
| Missing-file query | 512 hashes |
| Edit lock expiry | 30 seconds |

Locks are also bounded by the Durable Object attachment size. The relay refuses additional locks
before reaching Cloudflare's attachment limit. Storage does not evict files from a running room;
when full, new uploads fail explicitly. Session file access is revoked when the host leaves, and
alarms delete abandoned files in batches. R2 is temporary session storage, not a project backup.
An upload completing after its session ends is rejected and removed.
Active uploads have a ten-minute transfer deadline and are cancelled when the host leaves.
Cleanup retains their storage reservations until the write has stopped and deletion succeeds.

### Automatic R2 cutoff

The relay refuses an upload before it would exceed 512 MiB or 2,000 files. It also refuses new R2
uploads after 20,000 attempts, and new R2 downloads after 100,000 attempts in a UTC calendar month.
Failed attempts count, so checksum failures and retries cannot bypass the budget. Concurrent
requests reserve their allowance before accessing R2. Counters are stored in SQLite and survive
leaving a room, reconnecting, hibernation and ordinary Worker deployments. Monthly operation
allowances restart at 00:00 UTC on the first day of the next month; storage space becomes available
as ended-session files are removed. There is no automatic paid upgrade or quota override.

An upload cutoff still permits downloads of existing files; a download cutoff stops R2 reads.
Small edits already travelling over WebSocket do not use R2. When an edit needs a blocked file
transfer, the updated client stops sharing and keeps local edits, so it cannot quietly diverge.
Save your project locally, then reconnect and resync when the applicable allowance is available.
Metadata checks, duplicate-upload checks and cleanup remain available at the cutoff. R2 deletion
is free, and cleanup must remain able to reclaim space.

The three `R2_*_LIMIT*` settings in `wrangler.jsonc` can lower the defaults, including setting a
resource to `0` to disable it. Invalid settings or settings above these ceilings disable the
affected resource instead of removing the guard. Raising a ceiling requires a deliberate code
change. Keeping the same Durable Object namespace and `private-room-01` identity preserves
accounting; deleting its storage or changing those identities starts new accounting.

To see current usage, run from `server/cloudflare` in PowerShell:

```powershell
$env:FLIM_RELAY = 'https://your-worker.workers.dev'
$env:FLIM_HOST_KEY_FILE = 'C:\private\host-key.txt'
npm run usage
```

The command reads the hosting key from your private file and requests `/usage`; it never prints
the key. The endpoint requires the `x-flim-owner` header and returns only counters and limits.
Do not put the key in a URL, Git, a public file or the client package.

Large edits are uploaded once, then announced with a small hash reference. Receivers verify the
file's length, SHA-256, operation type and lock key before applying it. Sender/build/sequence
metadata comes from the relay. A failed edit transfer stops native sharing and retains local edits;
reconnect and resync before continuing. Existing sample-file warnings still apply separately.

## Free-tier behavior

Workers and SQLite Durable Objects are available on the Workers Free plan. Hibernation lets the
room sleep between activity while retaining its connected sockets. There is no application timer,
but free daily quotas, deployment changes and network failures can interrupt a session.

R2 requires activating its subscription even when starting within the free allowance. The R2 page
currently includes 10 GB-month storage, 1 million Class A operations and 10 million Class B
operations per month. The persistent cutoffs above leave substantial headroom for this small relay.
They count this Worker's R2 attempts from installation of the guard and bound files in its ledger;
they are not Cloudflare billing readings or an account-wide spending cap. Previous usage, direct
bucket access, other buckets, Workers or later changes can affect usage. Keep this private bucket
dedicated to the relay. Do not upgrade the Workers plan merely to run this backend.

Official references: [Workers pricing](https://developers.cloudflare.com/workers/platform/pricing/),
[Durable Objects pricing](https://developers.cloudflare.com/durable-objects/platform/pricing/),
[R2 pricing](https://developers.cloudflare.com/r2/pricing/).

## Deploy

Run commands from `server/cloudflare`. The Wrangler config publishes only `src/worker.ts` and its
imports; test RPC helpers and the development hosting key are not deployment entry points.

1. Activate R2 in the intended Cloudflare account and review its billing terms.
2. Run `npm ci`, then `npm test`.
3. Run `npx wrangler login --scopes account:read user:read workers:write workers_scripts:write`
   and authorize the intended account. These scopes cover this deployment; the warning about
   other missing scopes does not require granting access to unrelated Cloudflare products.
   If this computer already has another Cloudflare login, use a separate Wrangler auth profile
   and config directory so the existing login is preserved. Verify `npx wrangler whoami` before
   creating resources, and set `CLOUDFLARE_ACCOUNT_ID` to the intended account ID.
4. Run `npx wrangler r2 bucket create flim-private-files`. Keep the bucket private; do not enable
   an `r2.dev` public URL or add a public domain to it.
5. Run `npx wrangler secret put HOST_KEY`. At its prompt, enter a new random hosting key of at
   least 24 characters. Do not put it in Git, a deployment command line or the public README.
6. Run `npx wrangler deploy`. Wrangler installs the `v1` SQLite Durable Object migration and
   binds the private bucket. If the account has more than one project/account, explicitly select
   the intended account before deploying.
7. Open `https://<worker-hostname>/health`; a configured relay returns `flim-server ok`.

To review the deployment without publishing, run `npx wrangler deploy --dry-run`.

For the current Rooms app, create/join in the desktop UI. Its native FL toolbar is status-only;
it has no Host/Connect buttons. Review the app relay constant before distributing your own build
([building guide](../../docs/BUILDING.md)). Keep the account-wide hosting key private; the app
uses scoped room tickets. TLS protects transit, not end-to-end encryption.

## Local validation

`npm test` runs the real Worker in Miniflare with SQLite and R2, testing admission, mixed-build
policy, file hashes, quotas, locks, session cleanup, close handshakes and upload/session-end races.
Cutoff regressions cover failed attempts, concurrent reservations, reset periods, Worker restarts,
metadata checks, malformed lengths and cleanup racing a live upload. Room regressions cover scoped
tickets, password binding, expiry, restart persistence, peer/edit/lock/file isolation, authenticated
status, rate limits and the single global R2 budget across concurrent rooms.
`node test/rooms-smoke.mjs` checks production health and room API availability without writing.
Set `FLIM_TEST_RELAY` to another HTTPS relay or loopback HTTP address. Opt in to the room lifecycle
smoke with `FLIM_ROOM_SMOKE_CREATE=yes`: it consumes exactly two room reservations, starts and
closes its own sockets, verifies isolated edits and participant lists, and performs no R2 writes.
It never prints invitation codes, passwords or hosting tickets. Use this opt-in only when room
creation on that relay is authorized; unsuccessful reservations expire after ten minutes.

`node test/local-server.mjs` starts an isolated loopback runtime on port 49938 with a development-only
key. Type `stop` in its terminal to dispose it. Nothing is uploaded to Cloudflare by these commands.

Build the native harness with `client/dev/harness/build.bat` and run `t_cloud.exe` for malformed
file references, altered operation/lock metadata, duplicate/escaped/nested control fields and
snapshot delivery. `client/dev/cloudflare_smoke.py` exercises two explicitly owned FL processes
through the native probe without mouse or keyboard automation; `--large` adds a 6,000-note fixture.
Use `--transport-only` to check parameters, mixer edits and member-to-host edits without the
channel creation/deletion and sample fixtures. Wait for the joining FL's `PROJECT settled` log
before starting either mode; a welcome message alone does not mean the project has loaded.
`test/live-project.mjs` joins a running test room, requests a project and verifies a stored response.
It accepts `FLIM_TEST_RELAY`, `FLIM_TEST_PASSWORD`, `FLIM_TEST_ROOM` (defaults to `01`) and
`FLIM_TEST_FL` environment variables. App room sockets use `/room/<code>`; file requests remain at
the relay root.

`node test/compatibility-smoke.mjs` checks health and FL24/25/26 compatibility using an invalid
user, stopping before admission and creating no room. Set `FLIM_TEST_RELAY` to select another
relay. Set `FLIM_COMPAT_SMOKE_CREATE=yes` only when allocating one temporary room is intended;
that mode tests synthetic mixed-build membership, inline marker forwarding, sequence ordering
and cleanup. It does not open FL or transfer R2 files. Alternatively, set
`FLIM_COMPAT_SMOKE_ARTIFACT` to an absolute built Worker path for an isolated local Miniflare run;
do not combine that setting with `FLIM_TEST_RELAY`.

`node test/remote-smoke.mjs` checks a deployed relay's health, hosting key, room password,
mixed-build policy, private file access, checksum validation, large-edit transfer and session-end
access revocation. Set `FLIM_TEST_RELAY` to its HTTPS URL and `FLIM_TEST_HOST_KEY_FILE` to a local
file containing the private hosting key. Run this only when the private room is unused; it creates
and closes its own test room. It prints check results without exposing the hosting or file keys.

The latest cutoff and continuing-note validation is
[reliability-runtime.json](../../client/dev/compatibility/reliability-runtime.json): 20 local relay
tests, 14 deployed relay checks, and 30 native FL recording/edit checks passed. The original
Cloudflare transport record is [cloudflare-runtime.json](../../client/dev/compatibility/cloudflare-runtime.json).
It distinguishes local simulation, native FL checks and deployed-service checks. Passing these
checks does not establish a long-session soak, internet latency benchmark or compatibility with
every FL build and plugin.
