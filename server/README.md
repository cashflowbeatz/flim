# flim-server

WebSocket relay for [flim](../README.md). It doesn't understand FL Studio. It:
- puts every edit (`op`) in one order and relays it to the other people in the session,
- gives out per-unit **locks** (a knob, a pattern, the playlist) so two people can't edit the
  same thing at once; locks expire after 30 s if a client stops releasing them,
- runs **one session** (`01`), with a password set by its host. With `FLIM_PASSWORD` set, only a host
  that sends it (as `owner`; Ctrl+click Host in flim) may open the session,
- tracks who's in the session (the host is the leader; if the leader leaves, the session ends),
- stores **sample files** per session so each one is uploaded once and the others fetch only what
  they're missing (`src/blobs.ts`, plain HTTP on the same port).

The supplied deployment is **Fly.io**, not a Cloudflare Worker. You can run this Node.js server
behind Cloudflare Tunnel; see [Cloudflare and private testing](DEPLOYMENT.md). Changing server
hosting does not add support for a new FL Studio build: that lives in the client's verified hooks.

**Public mode** (`FLIM_PUBLIC=1`, the public test server flim.fly.dev): up to 8 sessions (`01`-`08`,
first free one for each new host) plus the owner's `00` (only for a host that sends `FLIM_OWNER_KEY`,
or `FLIM_PASSWORD`). Sessions are fully separate: ops, locks, projects and sample files never cross.
Sessions `01`-`08` end after 15 minutes (`FLIM_SESSION_MS`; `welcome` carries `ends_in` ms, the kick is
`time_up` with `cooldown` ms); every IP that was in one then can't host or join another one for 5
minutes (`FLIM_COOLDOWN_MS`; refused with `cooldown` + `retry_in`); sessions without any message for
5 minutes are closed (`FLIM_IDLE_MS`). Slot `00` has no time limit and no cooldown.

Limits: 8 people per session, 12 WebSockets per IP (`FLIM_MAX_CONNS_PER_IP`), 8 wrong passwords /
owner keys per IP per 10 minutes, 400 messages/s per connection (bursts up to 4000).
Slow peers are disconnected if queued outbound data would exceed 128 MiB. Broadcast messages are
serialized once for all peers. Lock keys are limited to 256 characters and a session to 4096 locks.
Lock expiry is checked when editing, acquiring a lock, and joining, as well as by the periodic
sweep. Only accepted edits renew active locks; cursor traffic and rejected operations do not.

Sessions are kept in memory only. When the machine stops, they are gone; the next host starts a new
one from their project.

## Sample store
Content-addressed by SHA-256, one folder per session (deleted when the session ends). Auth: header
`x-flim-key` = the session's blob key from `welcome`; it only reaches that session's files.
| Request | Does |
|---|---|
| `POST /blobs/missing` | body: one hash per line → the ones not stored, one per line |
| `HEAD /blob/<sha256>` | 200 + `content-length`, or 404 |
| `GET /blob/<sha256>` | the file |
| `PUT /blob/<sha256>` | store it (streamed to disk; rejected unless the content hashes to `<sha256>`) |

| Variable | Default |
|---|---|
| `FLIM_BLOB_DIR` | `<system temp>/flim-blobs`; generated session folders live inside `sessions-v1/` |
| `FLIM_BLOB_SESSION_MAX` | 2500 MB per session; its least recently used files are deleted above it |
| `FLIM_BLOB_STORE_MAX` | 5 GB for all sessions together; uploads are refused (507) above it |
| `FLIM_BLOB_FILE_MAX` | 2000 MB per file |

An upload must fit both the file and session limit. In-progress uploads reserve their declared
length against the store limit, so concurrent uploads cannot bypass it. Blob responses use
`Cache-Control: private, no-store`. A completed upload is protected from its own eviction pass;
later uploads may still evict it. Downloads open the file before sending HTTP 200, so a file
removed before open returns 404 and an expired session during lookup returns 410. An opened
download can finish even if its cache path is removed. See [transfer validation](TRANSFER-AUDIT.txt).
Keep one process per store directory; startup removes only
generated session folders inside `sessions-v1/`, preserving the configured parent. Caches made
by older versions directly under `FLIM_BLOB_DIR` are unused and may be removed manually while stopped.
Numeric limits must be decimal integers: positive byte/connection limits, nonnegative time limits,
and a port from 0 to 65535 (0 asks the OS for an available port). Invalid settings stop startup before
the cache is touched. A zero public time limit disables that timer; self-hosted timers stay disabled.

On fly.io the disk is reset when the machine stops, which is fine for a cache. Sample downloads
count towards fly.io's outbound data transfer.

## Run locally
Use a supported Node.js LTS release (24 recommended; the Docker image uses 24).
```bash
npm ci
npm run build
npm start                 # :8080, or set PORT
npm run dev               # auto-restarting dev mode (tsx watch)
curl http://localhost:8080/health    # -> flim-server ok
```
Set `FLIM_PASSWORD` so only you (or whoever you tell) can host. `npm test` builds and runs the
hardening regressions and the session tests on OS-assigned loopback ports.
`npm run benchmark:relay` compares the old per-recipient serialization loop with the current relay
on this machine. It measures CPU work with seven simulated recipients, not internet or FL latency.

| Variable | Default / purpose |
|---|---|
| `PORT` | `8080` |
| `FLIM_BIND` | All interfaces (Node's default); Cloudflare Tunnel mode defaults to `127.0.0.1` and requires loopback |
| `FLIM_PROXY` | `none`; `fly` trusts `Fly-Client-IP`, `cloudflare` trusts `CF-Connecting-IP` from local `cloudflared` |
| `FLIM_LOG_OPS` | Off; `1` logs operation names for diagnosis without logging project/plugin payloads |

Leave proxy mode `none` for a direct server. `fly` is for an origin reachable only through the Fly
proxy; another reverse proxy must not let arbitrary clients supply trusted headers. The included
`fly.toml` sets `FLIM_PROXY=fly`. Cloudflare mode is specifically for a connector on the same host;
it rejects a non-loopback bind instead of exposing a header-trusting origin to the LAN.

## Deploy to fly.io
```bash
fly auth login
fly launch --no-deploy --copy-config --name <your-unique-app-name>
fly secrets set FLIM_PASSWORD=<something-long>     # optional: only who knows it can host
fly deploy
fly scale count 1                                    # sessions live in one process: keep one machine
```
Hosts enter `<your-unique-app-name>.fly.dev`; the session link is
`https://<your-unique-app-name>.fly.dev/01` (opened in a browser, they show a short how-to page).

- `fly.toml` scales to zero (`auto_stop_machines = "stop"`, `min_machines_running = 0`): no cost
  while idle, and it wakes in a second or two when someone connects. Set `min_machines_running = 1`
  for an always-warm instance.
- Change `primary_region` to the region closest to you and your friends (`fly platform regions`).
- `MAX_PAYLOAD` in `src/server.ts` (64 MB) limits the size of a project sent as one message.

## Protocol
JSON over WebSocket; see `src/protocol.ts`. A host sends
`{"t":"host","password":"…","user":{"id":"…","name":"…"}}` (plus `"owner":"…"`: the server password, or
slot `00` in public mode), a member
`{"t":"join","session":"03","password":"…","user":{…}}`. Both get `welcome` (session, role, peer list,
blob key), then exchange `op`, `lock_acquire` / `lock_release` (→ `lock`, `lock_released`), and receive
`presence` updates. Ops that carry a project or a plugin state may list the samples it uses
(`"samples":[{"p":…,"h":<sha256>,"s":<bytes>}]`), fetched from `/blob/<h>`. Refusals are
`{"t":"error","code":…}` (`full`, `no_session`, `bad_password`, `bad_owner`, `owner_required`, `in_use`, `session_full`,
`too_many_attempts`, …) followed by close code 4001; an idle session ends with `idle` and code 4002.

Only the host may send `project.load`, `plug.full`, `notes.full`, or `state.full`; members may request
resynchronization with `state.req` and continue sending ordinary edits. Operation payloads may not
contain `t`, nested `kind`, or property names containing quotes, backslashes, or control characters.
These fields can confuse the native client's substring parser. Rejections return `bad_request` or
`not_leader` without forwarding the operation. See [the upstream audit](AUDIT.txt) for regression evidence.
