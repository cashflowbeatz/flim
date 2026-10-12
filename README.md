# flim

**Real-time multiplayer for FL Studio 2025.** Two or more people each run their own FL Studio, with their
own windows, zoom and selection, while the song itself (knobs, plugins, channels, notes, playlist) is kept
in sync over the internet. It works a bit like [Drawpile](https://drawpile.net/), but for FL.

> ⚠️ **Unofficial and experimental.** flim is not made by, affiliated with or endorsed by Image-Line.
> It works by running extra code inside FL Studio's process, and it only supports **one exact FL build**
> (see [Requirements](#requirements)). It can crash FL, so save often. flim never overwrites your
> original `.flp` files (see [Sessions](#sessions)).

---

## How it works

```
 you                                  server                         friend
┌────────────────────────┐        ┌──────────────┐        ┌────────────────────────┐
│ flim.exe starts FL and │  wss   │ flim-server  │  wss   │ flim.exe starts FL and │
│ loads flim_engine.dll  │◄──────►│ orders edits │◄──────►│ loads flim_engine.dll  │
│ into it                │        │ hands out    │        │ into it                │
│  - notices your edits  │        │ edit locks   │        │  - applies your edits  │
│  - applies theirs      │        │ (Node.js)    │        │  - sends theirs        │
└────────────────────────┘        └──────────────┘        └────────────────────────┘
```

- **`flim.exe`** (the launcher) starts your normal FL Studio and loads the flim engine into it. Nothing in
  your FL Studio installation is modified on disk.
- **`flim_engine.dll`** (the engine) runs inside FL. It watches for local edits, sends them to the server,
  and applies everyone else's edits through FL's own internal functions, so FL's UI stays consistent.
  It adds a **Connect** button to FL's top toolbar.
- **`flim-server`** is a small WebSocket relay. It puts edits in one order for everyone and gives out locks,
  so two people can't edit the same pattern or knob at the same moment. It runs on a free or cheap
  [fly.io](https://fly.io) machine, or on any PC with Node.js.

### What's synced
| Synced | Not yet |
|---|---|
| Channel volume / pan / on-off / mixer routing | Mixer track routing, names and colors |
| Every plugin parameter (generators and effects) | Channel / pattern / playlist track names and colors |
| Full plugin state (presets, wavetables, ...) | Creating new patterns, pattern length |
| Mixer volume / pan / stereo separation / mute | Markers, time signature |
| Adding, deleting and reordering channels | Automation clips |
| Adding, replacing and removing mixer effects | Showing other people's selection or cursor |
| Notes in every pattern | |
| Playlist clips | |
| Opening or loading a project (the leader's project is sent to everyone) | |

**Locks:** while you tweak a knob, edit a pattern's notes or edit the playlist, that part is yours. If
someone else touches it at the same time, their change is undone and FL's hint bar shows
*"Pattern 3 is being edited by Alex"*. Locks are released shortly after you stop: about 0.6 s for a knob, 2 s for a pattern or the playlist.

---

## Requirements

- Windows 10 or 11, 64-bit
- **FL Studio 2025, build 25.2.3.5171 exactly** (Help → About). The engine calls into FL at fixed internal
  addresses, and those change with every FL build. With any other build, flim warns you before starting
  and FL will most likely crash. Supporting a new build means finding those addresses again.
- A flim server everyone can reach (see [Hosting a server](#hosting-a-server))

---

## Quick start

1. Download `flim-client-win64.zip` from [Releases](../../releases) and unzip it anywhere.
   Keep `flim.exe` and `flim_engine.dll` in the same folder.
2. Double-click **`flim.exe`**. The first time, it asks for:
   - your **FL Studio 2025 folder** (the one that contains `FL64.exe`, usually
     `C:\Program Files (x86)\Image-Line\FL Studio 2025`). It is checked right away.
   - a **name** that the others will see (letters, digits, spaces, `- _ .`, up to 24 characters).

   Run `flim.exe --setup` to change these later.
3. FL Studio opens. In the top toolbar, to the right of the meters:

   `[ Connect ]  ● Not connected`

4. Click **Connect** and enter the server URL (for example `wss://my-flim.fly.dev`) and the password.
   The status turns green: `Connected to my-flim.fly.dev · leader · 2 people`.
5. Make music together. Click **Disconnect** to leave.

### Sessions
- A server holds **one session**. The **first person to connect is the leader**.
- Anyone who joins later is a **member** and gets the leader's current project. It is saved as a copy, e.g.
  `Documents\flim\MySong_flim_1012_2130.flp`, and opened automatically.
- When the leader opens another project, it is sent to everyone. Your original `.flp` files are never
  saved over: flim always works on `*_flim_MMDD_hhmm.flp` copies.
- If the leader leaves, the session ends. The members keep their copy of the project.

### Server URLs
| You type | Means |
|---|---|
| `wss://my-flim.fly.dev` | encrypted WebSocket on port 443 (use this for fly.io) |
| `my-flim.fly.dev` | same thing: a bare host name means `wss://`, port 443 |
| `localhost:8080` | a server on your own PC (plain `ws://`) |
| `ws://192.168.1.20:8080` | a server elsewhere on your LAN |

### Where flim keeps things
| What | Where |
|---|---|
| Settings (FL folder, name, last URL, password if "Remember" is ticked) | `%APPDATA%\flim\flim.ini`. The password is encrypted for your Windows user (DPAPI). |
| Log (attach it to bug reports) | `%TEMP%\flim_engine.log` |
| Project copies received from the leader | `Documents\flim\` |

---

## Hosting a server

The server is in [`server/`](server). See [`server/README.md`](server/README.md) for details.

### fly.io (recommended)
The machine scales to zero when nobody is connected and wakes in a second or two when someone connects.
```bash
# install flyctl: https://fly.io/docs/flyctl/install/
fly auth login
cd server
fly launch --no-deploy --copy-config --name <your-unique-app-name>
fly secrets set FLIM_PASSWORD=<something-long>
fly deploy
```
Everyone then connects to `wss://<your-unique-app-name>.fly.dev` with that password.
Check that it's up: `https://<your-unique-app-name>.fly.dev/health` should answer `flim-server ok`.

### Your own PC / LAN
Requires Node.js 20 or newer.
```bash
cd server
npm install
npm run build
FLIM_PASSWORD=secret npm start      # PowerShell: $env:FLIM_PASSWORD="secret"; npm start
```
The server listens on port 8080 (set `PORT` to change it). Friends outside your LAN need a port forward,
or use fly.io instead.

**Password:** if `FLIM_PASSWORD` is not set, the server lets in anyone who knows its address. That's fine
for testing on your own PC, not for a public server.

---

## Building the client from source

Requires **Visual Studio 2022 Build Tools** (the "Desktop development with C++" workload).
```bat
cd client
build.bat
```
`build.bat` writes `flim.exe` and `flim_engine.dll` to `client\out\`. If your Build Tools are not in the
default location, change `VCVARS` at the top of `build.bat`. Both files are plain C, linked statically,
and use only DLLs that ship with Windows.

### Developer shortcuts
These environment variables are read by the engine and skip the Connect dialog:

| Variable | Effect |
|---|---|
| `FLIM_AUTOCONNECT=<url>` | connect at startup (with `FLIM_PASSWORD=<pw>`) |
| `FLIM_NAME=<name>` | override the name from `flim.ini` (e.g. to run two FLs on one PC) |
| `FLIM_ROOM=<room>` | use another session name on the same server (default `FLIM`) |

`client\restart_test.ps1 [-Url localhost:8080] [-Password ...]` closes all FL instances, rebuilds, and
starts two of them ("A" as leader, "B" as member) connected to a server, which is handy for testing sync
on one machine.

---

## Repository layout
```
client/                 the FL side (C, Win32)
  launcher/flim.c       flim.exe: setup window, starts FL, loads the engine into it
  engine/flim_engine.c  the engine: edit detection, applying edits, locks, networking, toolbar UI
  common/               settings (flim.ini) and resource IDs shared by both
  build.bat             build script (MSVC)
  restart_test.ps1      dev loop: rebuild and start two connected FLs
server/                 flim-server (Node.js + TypeScript, WebSocket)
  src/                  server.ts (connections, password), session.ts (ordering, locks), protocol.ts
  Dockerfile, fly.toml  fly.io deployment
```

## Troubleshooting
- **"This FL Studio is version X, but flim was built against 25.2.3.5171"**: your FL build is different.
  See [Requirements](#requirements).
- **"Can't connect: server not found"**: check the URL. fly.io apps are `wss://<app>.fly.dev`.
- **"Can't connect: not a flim server (HTTP …)"**: something answered at that address, but it isn't
  flim-server.
- **"Wrong password"**: ask whoever runs the server. The password is the server's `FLIM_PASSWORD`.
- **Something didn't sync or FL crashed**: please open an issue and attach `%TEMP%\flim_engine.log`.

---

*FL Studio is a trademark of Image-Line. This project contains no FL Studio code or files. It only talks
to an FL Studio installation that you already have.*
