# flim

**Real-time multiplayer for FL Studio, with an experimental FL 26 port.** Two or more people each run their own FL Studio, with their
own windows, zoom and selection, while the song itself (knobs, plugins, channels, notes, playlist, mixer,
samples) is kept in sync over the internet. It works a bit like [Drawpile](https://drawpile.net/), but for FL.

> 🚧 **Still missing features.** A few things don't sync yet, such as samples inside Kontakt and
> other people's selection. See [What's synced](#whats-synced) for the full list.

> ⚠️ **Unofficial and experimental.** flim is not made by, affiliated with or endorsed by Image-Line.
> It works by running extra code inside FL Studio's process, and it only supports **specific FL builds**
> (see [Requirements](#requirements)). It can crash FL, so save often. flim never overwrites your
> original `.flp` files (see [Sessions](#sessions)).

This fork adds an opt-in port for **FL Studio 26.1.7.5654 and 26.1.6.5639 on Windows x64**. Build it with
`FLIM_EXPERIMENTAL_FL26=1`; the resulting launcher and engine are in `client/out-experimental`.
The same experimental client also supports FL **25.2.5.5319, 25.2.3.5171 and 25.1.6.4997**;
the default build includes those three FL 25 tables. Reviewed external profiles can add more FL 25/26
builds without recompiling the client. Unknown builds still need their own mapping and tests.

**Different supported builds can share a session when the oldest build hosts.** Update everyone's
client and relay. Newer members can send supported notes, parameter and naming edits; only people
on the host's exact build can send whole presets or create channels. Project replacement remains host-only. A newer
member making an incompatible edit stops sharing and keeps the local edit. flim does not convert
newer projects or presets for an older FL loader.
See [build support and mixed sessions](BUILD-COMPATIBILITY.md) for setup and restrictions, and
[compatibility validation](client/dev/compatibility/README.md) for test evidence.
The release links and public-test instructions below describe the upstream FL 25 release; they do
not publish the new FL 26 client automatically.

**Read [what changed in this fork](CHANGES.md)** for a plain-language explanation of each fix,
examples of the problems it addresses, the exact test results and what you need to update.

The supplied server runs on **Fly.io**. For your own server behind Cloudflare Tunnel, see
[private testing and Cloudflare](server/DEPLOYMENT.md).
The [upstream source audit](server/AUDIT.txt) documents fixes for lock expiry, host-only project
replacement, ambiguous protocol fields and invalid server settings. The [continued audit](server/CONTINUED-AUDIT.txt)
also covers releasing old project locks when switching projects and avoiding duplicate sample metadata checks.
The [sample transfer audit](server/TRANSFER-AUDIT.txt) covers download races and protecting a new upload
from its own cache eviction. Run this fork's revised server to receive these fixes; the upstream
public server is not updated by publishing this fork.

---

## ! PUBLIC TEST !

**Try flim with a friend right now, no server needed.** There is a public test server at
**`flim.fly.dev`** with 8 session slots.

1. You both need **FL Studio 2025, build 25.2.3.5171** (Help > About) on Windows 10/11. Different
   build? See [Other FL Studio builds](#other-fl-studio-builds).
2. Download `flim-client-win64.zip` from the [latest release](../../releases/latest), unzip it, and run
   **`flim.exe`**. The first time, it asks for your FL Studio folder and a name.
3. **One of you hosts:** in FL's top toolbar (next to the CPU / memory meters) click **Host**. The
   server is already filled in (`flim.fly.dev`) and so is a random session password (change it if you
   like). Click **Host**. flim shows your **session link** (e.g. `https://flim.fly.dev/03`) and the
   **password**, with a **Copy** button. Send both to your friend.
4. **The other joins:** click **Connect**, paste the link, enter the password, click **Connect**. Your
   friend's FL now loads the host's project, and from then on you're making the same song.

Rules of the public test:
- **Sessions last 15 minutes**, so nobody hogs a slot. The time left is shown at the right end of
  flim's toolbar. When it runs out, everyone is disconnected, and **you keep all progress in your
  FL**: save it as usual.
- After a session's time runs out, everyone who was in it waits a **5-minute cooldown** (shown as
  `COOLDOWN: mm:ss`) before hosting or joining again.
- A session where nobody does anything for 5 minutes is closed.
- If all 8 slots are taken, Host tells you so. Try again a bit later.
- Up to 8 people per session. If the host leaves, the session ends.

Want no public-test time limit? [Run your own server](#server); transfer and connection limits
still apply.

---

## How it works

```
 you                                  server                         friend
┌────────────────────────┐        ┌──────────────┐        ┌────────────────────────┐
│ flim.exe starts FL and │   ws   │ flim-server  │   ws   │ flim.exe starts FL and │
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
  It adds **Connect** and **Host** buttons to FL's top toolbar.
- **`flim-server`** is a small WebSocket relay. It puts edits in one order for everyone and gives out locks,
  so two people can't edit the same pattern or knob at the same moment. One person (or a server
  somewhere) runs it, and everyone connects to it.

### What's synced
| Synced | Not yet |
|---|---|
| Channel volume / pan / on-off / mixer routing | Samples inside plugins that store them compressed or encrypted (Kontakt, ...) |
| Every plugin parameter (generators and effects) | Showing other people's selection inside an editor |
| Full plugin state (presets, wavetables, ...) | |
| Mixer volume / pan / stereo separation / mute | |
| Mixer track names, colors and routing (sends on/off and send levels) | |
| Adding, deleting and reordering channels; channel names and colors | |
| Adding, replacing and removing mixer effects | |
| Notes in every pattern; pattern names, colors and length (so new patterns too) | |
| Inserting, cloning, deleting and splitting patterns (later patterns move along) | |
| Playlist clips; playlist track names and colors | |
| Playlist markers (including time signature markers); the project's time signature | |
| Time signatures of single patterns (pattern menu) and piano roll markers | |
| Automation clips | |
| Sample files used by channels, audio clips and plugins like FPC (see [Samples](#samples)) | |
| Opening or loading a project (the leader's project is sent to everyone) | |
| Which window everyone is working in, and where their mouse is in the piano roll and playlist (see [Who's where](#whos-where)) | |

**Locks:** while you tweak a mixer knob, change a plugin, edit a pattern's notes or edit the playlist,
that part is yours. If someone else touches it at the same time, their change is undone and FL's hint bar
shows *"Pattern 3 is being edited by Alex"* or *"Channel 'Serum 2' is being edited by Alex"*. A plugin is
locked as a whole: any knob or preset change on it takes the lock. Locks are released shortly after you
stop: about 0.6 s for a channel or mixer knob, 2 s for a plugin, a pattern or the playlist.
Names, colors, routing, markers and the time signature have no locks; if two people change the same
one at the same moment, the leader's change wins.

### Who's where
The status next to the Connect button shows the window each other person last clicked or typed in,
e.g. `Connected to flim.example.com · leader · 2 people · Alex: Piano roll - Kick · Pattern 3`.
Hover over it to see everyone in FL's hint bar.

When someone's mouse is over the piano roll or the playlist, you see a small arrow with their name at
the same spot in yours (the same note and bar, whatever your zoom or scroll), as long as you are looking
at the same pattern or arrangement. The arrows can't be clicked and never get in your way.

---

## Requirements

- **Client:** Windows 10 or 11 (64-bit). The default build supports FL **25.2.5.5319**, **25.2.3.5171** and
  **25.1.6.4997**; the experimental build additionally supports **26.1.7.5654** and **26.1.6.5639** (Help → About).
  Update everyone's client and relay, and let the oldest supported build host. Mixed sessions restrict whole presets and channel creation; see [the policy](BUILD-COMPATIBILITY.md).
  Have a different build? See [Other FL Studio builds](#other-fl-studio-builds).
- **Server:** a supported Node.js LTS release (Node.js 24 is the container default) on Windows,
  Linux or macOS, reachable by the participants.

---

## Quick start

### Client
1. Download `flim-client-win64.zip` from [Releases](../../releases) and unzip it anywhere.
   Keep `flim.exe` and `flim_engine.dll` in the same folder.
2. Double-click **`flim.exe`**. The first time, it asks for:
   - your **FL Studio 2025 folder** (the one that contains `FL64.exe`, usually
     `C:\Program Files (x86)\Image-Line\FL Studio 2025`). It is checked right away.
   - a **name** that the others will see (letters, digits, spaces, `- _ .`, up to 24 characters).
3. FL Studio opens. In the top toolbar, to the right of the meters:

   `[ Connect ] [ Host ]  ● Not connected`

4. **To start a session**, click **Host**. Enter the server (e.g. `flim.fly.dev`) and a session
   password (a random one is filled in; change it if you like), click **Host**. flim shows your
   **session link** (e.g. `https://flim.fly.dev/03`) and the password, with a **Copy** button: send
   both to the people you want to play with. Click the status text later to see them again.
   If every session on the server is taken, you're told so; try again later.
5. **To join**, click **Connect**, paste the session link and enter the password.
   The status turns green: `Connected to flim.fly.dev/03 · member · 2 people`.
6. Make music together. Click **Disconnect** to leave.

### Server
Only one person needs to run this.
```bash
git clone https://github.com/fckwader/flim.git
cd flim/server
npm install
npm run build
npm start
```
The server listens on port **8080**. Whoever hosts enters its address in the Host dialog:
- **Same network:** `ws://<your-LAN-IP>:8080`, e.g. `ws://192.168.1.20:8080`.
- **Over the internet:** forward TCP port 8080 on your router to this PC and use
  `ws://<your-public-IP>:8080`. Or put the server behind a reverse proxy with HTTPS and use `wss://`
  (see [Server details](#server-details)).
- **Just testing on one PC:** `localhost:8080`.

---

## Using flim

### Sessions
- A server you run yourself holds **one session** (`01`). The public test server `flim.fly.dev`
  runs up to **8** at once (`01` to `08`). Every session has its own password, and sessions are fully
  separate: nothing (edits, locks, projects, sample files) crosses from one to another.
- Whoever **hosts** a session is its **leader**.
- Anyone who joins later is a **member** and gets the leader's current project. It is saved as a copy, e.g.
  `Documents\flim\MySong_flim_1012_2130.flp`, and opened automatically.
- When the leader opens another project, it is sent to everyone. Your original `.flp` files are never
  saved over: flim always works on `*_flim_MMDD_hhmm.flp` copies.
- If the leader leaves, the session ends. The members keep their copy of the project.
- **On `flim.fly.dev` only** (testing, so nobody hogs a slot): sessions last at most **15 minutes**;
  the time left is shown at the right end of flim's toolbar. When it runs out, everyone is
  disconnected and keeps their FL project exactly as it is. Everyone who was in it then has a
  **5-minute cooldown** (shown as `COOLDOWN: mm:ss`) before hosting or joining another session. A
  session where nobody does anything for **5 minutes** is closed, so its slot is free again.

### Samples
The samples your channels, audio clips and sample-based plugins use are sent along automatically,
both when the leader's project is sent and when someone loads a new sample during the session.
- Each file is uploaded to the server **once per session**; the others download only the files they don't already
  have, into `Documents\flim\samples\`, and their copy of the project points at those files.
- While that happens, the toolbar status shows *"Uploading samples: 12.3 / 40.0 MB"* (or *Downloading*).
  Your other edits wait until the upload is done, so they arrive in the right order.
- **If more than 50 MB would have to be uploaded at once, flim asks first** and shows the exact amount,
  e.g. *"Sending them uploads 60.6 MB (63,504,132 bytes). Send them now?"*. **No** leaves those samples
  out (they will be missing for the others) and flim won't ask about them again until FL restarts.
- Samples in FL's factory content are never sent: every FL installation has them.
- Samples inside plugin states are found too: in FPC (and other newer Image-Line plugins that store
  them the same way), and as plain file paths in any other plugin's state. For the latter, the
  receiving copy points at a link to the downloaded file under `Documents\flim\samples\`, named to
  keep the plugin's data valid. Plugins that compress or encrypt their state (Kontakt, ...) aren't covered.

### Server addresses
| You type | Means |
|---|---|
| `localhost:8080` | a server on your own PC (plain `ws://`) |
| `ws://192.168.1.20:8080` | a server elsewhere on your LAN |
| `ws://203.0.113.7:8080` | a server over the internet, unencrypted |
| `wss://flim.example.com` | a server behind HTTPS (encrypted, port 443) |
| `flim.example.com` | same thing: a bare host name means `wss://`, port 443 |

### Changing your settings
Run `flim.exe --setup` to change your FL folder or name. You can also change your name in the
Connect and Host dialogs.

### Where flim keeps things
| What | Where |
|---|---|
| Settings (FL folder, name, last session link and host server, password if "Remember" is ticked) | `%APPDATA%\flim\flim.ini`. Passwords are encrypted for your Windows user (DPAPI). |
| Log (attach it to bug reports) | `%TEMP%\flim_engine.log` |
| Project copies received from the leader | `Documents\flim\` |
| Samples received from others (safe to delete when no session is running) | `Documents\flim\samples\` |

---

## Server details

See [`server/README.md`](server/README.md) for the protocol and options.

- **Session:** your server holds one session (`01`), with no time limit. Whoever hosts picks its
  password; joining needs the session link and that password.
- **Server password:** set `FLIM_PASSWORD` and only someone who knows it can host (Ctrl+click
  **Host** to enter it). Without it, anyone who knows the address can host. Wrong passwords are
  rejected after a second; 8 wrong ones from one IP in 10 minutes block that IP for the rest of the
  10 minutes.
- **Limits:** 8 people per session, 12 connections per IP, and a cap on messages per second per
  connection.
- **Public mode** (`FLIM_PUBLIC=1`, what `flim.fly.dev` runs): 8 sessions plus the owner's `00`
  (`FLIM_OWNER_KEY`), 15-minute sessions with a 5-minute cooldown and an idle timeout. See
  [`server/README.md`](server/README.md).
- **Port:** 8080 by default; set `PORT` to change it.
- **Encryption:** the server speaks plain `ws://`. For `wss://`, put it behind any reverse proxy that
  handles HTTPS and WebSockets (Caddy, nginx, a cloud load balancer, ...) and forward to port 8080.
  The proxy must pass plain HTTP requests too (sample files use `/blob/...` on the same port) and allow
  large request bodies.
- **Sample store:** sample files are kept on the server's disk under their SHA-256, in a folder per
  session under `FLIM_BLOB_DIR` (default: the system temp folder), deleted when the session ends.
  `FLIM_BLOB_SESSION_MAX` caps one session (default 2500 MB; its least recently used files go first),
  `FLIM_BLOB_STORE_MAX` all of them together (default 5 GB; uploads are refused above it) and
  `FLIM_BLOB_FILE_MAX` a single file (default 2000 MB).
- **Docker:** a `Dockerfile` is included:
  ```bash
  docker build -t flim-server server
  docker run -p 8080:8080 -e FLIM_PASSWORD=pick-a-password flim-server
  ```
- **Health check:** `GET /health` answers `flim-server ok`.
- Sessions live in memory only. Restarting the server ends all sessions; run a single instance.

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
These environment variables are read by the engine and skip the Connect / Host dialogs:

| Variable | Effect |
|---|---|
| `FLIM_AUTOCONNECT=<session link>` | join at startup (with `FLIM_PASSWORD=<session password>`) |
| `FLIM_HOST=1` | `FLIM_AUTOCONNECT` is a server address to host on instead (`FLIM_OWNER_KEY=<key>`: the server's password, or the owner key = slot `00` on a public server) |
| `FLIM_NAME=<name>` | override the name from `flim.ini` (e.g. to run two FLs on one PC) |
| `FLIM_SAMPLE_NOLOCAL=1` | never use a local copy of a received sample, always download it (to test sample transfer on one PC) |

`client\restart_test.ps1 -Minimized -FlDir "<FL folder>" [-Experimental] [-Url localhost:8080] [-OwnerKey <server password>] [-Slot 01] [-Password test]`
closes only test processes whose recorded identities still match, rebuilds, and starts two instances:
"A" hosts, "B" joins session `-Slot`.
Handy for testing sync on one machine with a local server (`npm start` in `server/`).

---

## Repository layout
```
client/                 the FL side (C, Win32)
  launcher/flim.c       flim.exe: setup window, starts FL, loads the engine into it
  engine/flim_engine.c  the engine: edit detection, applying edits, locks, pattern ops, sample transfer,
                        networking, toolbar UI (status + who is where)
  common/               settings (flim.ini) and resource IDs shared by both
  build.bat             build script (MSVC)
  restart_test.ps1      dev loop: rebuild and start two connected FLs
  dev/                  reverse-engineering and test tools (not built or shipped)
server/                 flim-server (Node.js + TypeScript, WebSocket + HTTP)
  src/                  server.ts (connections, slots, limits), session.ts (ordering, locks), protocol.ts,
                        blobs.ts (sample file store)
  Dockerfile            container image
```

## Troubleshooting
- **"This FL Studio is version X. flim supports these builds: ..."**: your FL build isn't supported yet.
  See [Other FL Studio builds](#other-fl-studio-builds).
- **"Can't connect: server not found"** or **"nothing answers at that address"**: check the address, that
  the server is running, and (over the internet) that the port is forwarded.
- **"Can't connect: secure connection failed"**: you typed a bare host name or `wss://`, but the server
  speaks plain WebSocket. Use `ws://host:8080`.
- **"Can't connect: not a flim server (HTTP …)"**: something answered at that address, but it isn't
  flim-server.
- **"Wrong session password"** / **"Session not found"**: ask the host for the link and password again;
  the session may have ended (host left, or 5 minutes without activity).
- **"All sessions are in use"**: the server's session(s) are taken. Try again later.
- **"Server password needed"**: the server has `FLIM_PASSWORD` set. Ctrl+click **Host** and enter it.
- **Build compatibility refusal**: update everyone's client and the relay, and let the oldest
  supported FL build host. Newer members cannot send whole presets or project replacements.
  See [build support and mixed sessions](BUILD-COMPATIBILITY.md).
- **Something didn't sync or FL crashed**: please open an issue and attach `%TEMP%\flim_engine.log`.

---

## Other FL Studio builds

flim calls FL Studio's internal functions, and where those sit inside FL changes with every FL
update. So flim keeps a table for each FL build it knows. Check yours under **Help → About**.

| Build | Status |
|---|---|
| **26.1.7.5654** | Experimental; current runtime validation is linked below. |
| **26.1.6.5639** | Experimental table retained; 31 runtime feature checks passed before the installed FL update. |
| **25.2.5.5319** | Current runtime validation uses the same experimental client as FL 26. |
| **25.2.3.5171** | Original reference table retained; synthetic hook checks pass. |
| **25.1.6.4997** | Original upstream table retained; synthetic hook checks pass. |

The installed FL 25.2.5.5319 host and FL 26.1.7.5654 member passed **34/34 mixed-session checks**,
including supported edits back to the host. Same-build checks and exact binary identities are in
[the validation record](client/dev/compatibility/mixed-profile-runtime.json).

**Your build isn't listed?** `flim.exe` tells you when it starts. If you start FL anyway, flim stays
off and FL runs as usual: nothing breaks, it just doesn't sync. What you can do:
1. **Ask for it:** [open an issue](../../issues) with your exact build number (Help → About). Adding
   a build means reviewing its addresses, layouts and behavior; signature matches alone are insufficient.
2. **Add a reviewed profile:** see [the profile workflow](BUILD-COMPATIBILITY.md#adding-another-build).
   Profiles bind a complete mapping to the exact engine hash; changing only the hash is unsafe.
3. **Use 25.2.3.5171** if you have it installed.

---

*FL Studio is a trademark of Image-Line. This project contains no FL Studio code or files. It only talks
to an FL Studio installation that you already have.*
