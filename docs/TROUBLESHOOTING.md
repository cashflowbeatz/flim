# Troubleshooting Rooms

Use the same complete current app on both computers. Save projects before testing.
[Compatibility](../BUILD-COMPATIBILITY.md) explains exact builds and fallback features.

| What you see | What to check |
| --- | --- |
| “Install the Cloudflare-enabled flim client” | An old relay can reject the protocol/build with this wording. Use the current complete app and updated relay. Friends do not install Cloudflare. |
| No FL installation, or unsupported build | **Choose folder** containing `FL64.exe`, expand **Installation details**, and **Check again** after updates. Unknown patches need reviewed mappings. |
| FL opens but the room will not connect | Check room number, current app, host connection and relay/network availability. Read FL status and logs. A people list does not prove project loading finished. |
| “Daily room limit” | Current source removes daily creation quotas. An older deployed relay may still enforce them. Platform/burst/storage limits are separate. |
| Room expired/unavailable | Ask for a fresh number. Unstarted reservations expire after ten minutes; active sessions do not have that timer. Closing the host ends the room. |
| Older FL joins with unavailable sounds | Notes/basic-controls fallback avoids loading newer unsupported content. Use sounds/features both installations support for equivalent playback. |
| A present VST will not open | Confirm matching scanned format/identity. Another folder/version label may work; older vendors may reject newer state. Ambiguous matches stay unavailable. |
| Waiting for folder access | Restore the drive, free disk space or release the file lock. The edit remains pending for retry. Folder changes apply before a new room/process. |
| Waiting to retry automatically | Check network/storage/allowance status. Retries back off; **Refresh files** only reads status. No new sample edit is required. |
| File status stale/unavailable | Controls could not verify status and retain confirmed rows. Retry or reopen the complete app. This alone does not prove native transfers stopped. |
| Keyboard sounds locally but no notes appear remotely | Record into the piano roll. Unrecorded audition/live MIDI and audio are not streamed. Check the current engine and a supported channel. |
| Sharing pauses after a native error | Save and inspect logs. Invalid edits, essential queue overflow and native faults pause sharing. FL stays open; do not keep forcing the failing mutation. |
| UI reloads after a crash | Main-process room state can survive renderer reload. Repeated failures reach a recovery screen. This does not restart FL or prove native session recovery. |

## Report a reproducible problem

Select **Open test logs**. Include the app version, exact FL builds on both sides, host direction,
whether fallback was active, and steps/expected/actual behavior. For plugin problems include its
format/vendor version, without proprietary presets or licenses.

Logs stay local until shared. Review/redact personal paths, project/sample names, invitations
and credentials before posting. Never attach FL binaries, Image-Line licenses or Cloudflare keys.

[Bug report](https://github.com/cashflowbeatz/flim/issues/new?template=bug_report.yml)
· [Test coverage](TESTING.md)
