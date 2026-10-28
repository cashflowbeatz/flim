# Your first flim room

[Download the Windows app](https://github.com/cashflowbeatz/flim/releases/tag/rooms-v0.2.25)
and give your friend the same **flim-rooms-0.2.25-win64.exe**. It is a portable app with the
native launcher, engine and profiles included. There is no separate client or Cloudflare installation.

You each need Windows 10/11 x64, internet access and your own supported FL installation.
Reviewed builds: **24.2.2.4597**, **25.2.5.5319**, **26.1.7.5654**.
[Check compatibility](../BUILD-COMPATIBILITY.md) when using different versions.

## Choose where received samples go

This is optional. The default is Windows **Documents\flim\samples**, including redirected
Documents folders. Open **Files →**, then **Save received files to → Choose folder**. Select a
writable folder with a reasonably short path. Rooms remembers it on this computer. **Use default**
restores the default; **Open samples folder** opens the actual destination.

Choose before entering a room. It applies to the next FL process launched by Rooms; active rooms
lock it. Existing files stay put. An unavailable custom drive waits for access instead of silently
redirecting downloads.

## Create a room

1. Press **Get started** and enter your name. No signup is required.
2. Choose **FL Studio 24**, **25** or **26**. **Installation details** shows the exact build and
   folder; **Choose folder** selects a copy on another drive.
3. Select **Create a room** and open FL through the app. This starts a separate session window.
4. Use **Copy room number** and send the eight digits privately to your friend.

## Join your friend

Choose **Join a room**, enter your name, select FL and paste the number. Start FL through Rooms.
Wait for project synchronization to finish in FL before editing. Appearing in the people list
confirms admission, not completion of project loading.

Any supported version may host. Older members joining newer hosts share notes/basic controls;
unsupported sounds/features remain unavailable. flim must successfully preserve the local project
before preparing an older fallback workspace.

## Try a small collaboration check

Use a disposable project and a sound both computers have. Add a few notes, move one and delete
another. Have the member do the same. Record a short MIDI phrase into the piano roll and compare
the resulting notes. Each computer plays its own sound; flim does not stream audio.

Add one small sample through a supported shared channel/playlist operation. Open **Files →** on
the receiving computer to check progress and its local file. Comparing notes/files and playback
tests more than simply seeing names in a room.

Files includes search, **All / Pending / Attention / Ready**, **Both / To me / From me**, and
**Name / Recent** order. Search changes only the view. Recoverable transfers retry automatically;
**Refresh files** refreshes status rather than retrying the transfer itself.

## Finish or update

Closing Rooms or returning to start leaves FL open. Closing the host's FL ends the room.
After updating, save and close session FL windows when convenient, open the new complete app on
every computer and create a fresh room. Already running FL keeps its loaded engine. Distribute
the whole portable app, not a loose `flim.exe` or old client ZIP.

[Troubleshooting](TROUBLESHOOTING.md) · [Features](FEATURES.md) · [README](../README.md)
