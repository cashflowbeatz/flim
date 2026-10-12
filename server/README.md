# flim-server

WebSocket relay for [flim](../README.md). It doesn't understand FL Studio. It:
- puts every edit (`op`) in one order and relays it to the other people in the session,
- gives out per-unit **locks** (a knob, a pattern, the playlist) so two people can't edit the
  same thing at once; locks expire after 30 s if a client stops releasing them,
- tracks who's in the session (the first to join is the leader; if the leader leaves, the session ends),
- checks the shared **password** (`FLIM_PASSWORD`).

Everything is kept in memory only. When the machine stops, the session is gone; the next leader
starts a new one from their project.

## Run locally
Requires Node.js 20+.
```bash
npm install
npm run build
npm start                 # :8080, or set PORT
npm run dev               # auto-restarting dev mode (tsx watch)
curl http://localhost:8080/health    # -> flim-server ok
```
Set `FLIM_PASSWORD` to require a password. If it's not set, the server lets in anyone who
knows its address (a warning is logged).

## Deploy to fly.io
```bash
fly auth login
fly launch --no-deploy --copy-config --name <your-unique-app-name>
fly secrets set FLIM_PASSWORD=<something-long>
fly deploy
```
Clients connect to `wss://<your-unique-app-name>.fly.dev`.

- `fly.toml` scales to zero (`auto_stop_machines = "stop"`, `min_machines_running = 0`): no cost
  while idle, and it wakes in a second or two when someone connects. Set `min_machines_running = 1`
  for an always-warm instance.
- Change `primary_region` to the region closest to you and your friends (`fly platform regions`).
- `MAX_PAYLOAD` in `src/server.ts` (64 MB) limits the size of a project sent as one message.

## Protocol
JSON over WebSocket; see `src/protocol.ts`. A client sends
`{"t":"join_room","room":"FLIM","password":"…","user":{"id":"…","name":"…"}}` and gets `welcome`
(its role plus the peer list), then exchanges `op`, `lock_acquire` / `lock_release` (→ `lock`,
`lock_released`), and receives `presence` updates. A wrong password gets
`{"t":"error","code":"bad_password"}` and the connection is closed with code 4001.
