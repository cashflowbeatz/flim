# flim Rooms for Windows

The complete portable app bundles its native launcher, engine and reviewed profiles. Participants
need only the executable and their own supported FL Studio installation; there is no second client,
Cloudflare installation, account signup, Node.js or VPN requirement.

**FL Studio 24, 25 and 26 can share a room with any supported build hosting.** Reviewed installs:
24.2.2.4597, 25.2.5.5319, 26.1.7.5654. Older members joining newer hosts use notes/basic controls;
unsupported content remains unavailable. Exact build/fingerprint checks remain.

## Get started

[Download Rooms 0.2.23](https://github.com/cashflowbeatz/flim/releases/tag/rooms-v0.2.23)
and open **flim-rooms-0.2.23-win64.exe** on each computer. Press **Get started**, enter a name,
select FL and create/join using an eight-digit number. Wait for native project sync before editing.

The Files arrow opens a lateral sidebar. Before a room, **Save received files to → Choose folder**
selects this computer's destination. The setting applies to the next launched FL process and does
not move existing files. Transfers continue while FL runs, even with the sidebar hidden.

- [First-room guide](../../docs/QUICKSTART.md)
- [Features and limits](../../docs/FEATURES.md)
- [Compatibility and host/member table](../../BUILD-COMPATIBILITY.md)
- [Sample transfer details](FILES-SHARING.md)
- [Build/package instructions](../../docs/BUILDING.md)
- [Testing and evidence](../../docs/TESTING.md)
- [Electron bridge contract](electron/README.md)
- [All 39 latest fixes](QA-0.2.23.md)

## Sharing the app and connection errors

Give friends the whole portable executable, not an extracted `flim.exe` or old native ZIP. An
“Install the Cloudflare-enabled flim client” message may mean an old relay rejected the protocol
or build; it does not require installing Cloudflare on a friend's computer. Update the app and
relay together. [Connection troubleshooting](../../docs/TROUBLESHOOTING.md).

The default relay belongs to this fork. Its capacity/platform/R2 limits apply. An already running
FL retains its loaded engine after replacing the portable executable; save and restart session
windows when convenient, then create a fresh room with the same current app on every computer.

## Test logs

**Open test logs** opens local rotating desktop/native diagnostics. They are never uploaded
automatically. Review native logs before sharing: they can include paths and project/sample names.
Redact invitation numbers, credentials and personal details. [Bug reporting](../../CONTRIBUTING.md).

Versioned QA files preserve the status of their original test run. Raw local evidence and private
process observations are excluded from Git. Automated fixtures do not establish every FL patch,
live physical MIDI session, plugin version or internet performance.
