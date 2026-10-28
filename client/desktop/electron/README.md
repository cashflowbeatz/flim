# Desktop bridge

The renderer calls `window.flimDesktop`. Each method returns a promise. Failures reject with an `Error` whose `message` is safe to display; preload removes Electron's remote-method prefix. After a failed action, read `getState()` again so the UI sees an expired room, an updated compatibility result, or an uncertain launch.

| Method | Input | Result |
| --- | --- | --- |
| `getState()` | None | Current public state, after initial discovery finishes |
| `savePreferences(input)` | `{name, installId}` | Public state |
| `chooseInstall()` | None; opens a native folder picker | Public state; cancellation keeps the current selection |
| `createRoom(input)` | `{name, installId}` | Public state with a reserved host room |
| `joinRoom(input)` | `{name, installId, code}` | Public state with member credentials held privately |
| `launchFL()` | None | Public state after the native launcher finishes |
| `refreshRoom()` | None | Public state from an authenticated status lookup |
| `leaveRoom()` | None | Public state after forgetting the room; attempts to release an unstarted host reservation |
| `copyInvite()` | None | `{copied: true}`; copies only the eight-digit room number |

Public state:

```ts
{
  platform: string;
  nativeReady: boolean;
  name: string;
  selectedInstallId: string;
  installs: Array<{
    id: string;
    path: string;
    version: string;
    supported: boolean;
    reason: string;
  }>;
  busy: boolean;
  notice?: string; // expiry fallback after an unused reservation could not be released
  room: null | {
    code: string;
    role: 'host' | 'member';
    maxParticipants: 8;
    expiresAt: string | null; // ISO timestamp for a pending reservation
    state: 'ready' | 'launched' | 'uncertain';
    status: 'waiting' | 'active' | 'ended';
    peers: Array<{id: string; name: string; role: 'host' | 'member'; fl: string}>;
  };
}
```

`installs[].id` is an opaque token valid for this app process. The renderer cannot choose a command, executable, installation path, relay URL, or argument list. The native picker is the only way to add an installation. Discovery and each launch use the bundled native launcher's exact version/profile/hash checks.

`room.state` describes this app's launcher, while `room.status` describes the server room. A launch can finish before FL connects to the relay, so `launched` does not mean synchronization is ready. A reserved host room normally reports `waiting` with an empty peer list until the host connects. Reservations expire after ten minutes; active rooms do not use that reservation timer. A status response of `room_unavailable` changes the status to `ended`; rate limits or connection failures reject without falsely ending a live room.

Poll status every five seconds while `waiting` and every thirty seconds while `active`. Pause while hidden or offline, refresh when visible/online again, retry temporary errors, and stop on `ended`. The bridge combines overlapping requests for the same room and limits network lookups to once every five seconds while waiting or ten seconds while active. A throttled retry retains the last error until a real successful lookup; it cannot falsely clear a failed request. Leaving or changing rooms aborts the old lookup, and its eventual reply cannot change the new room or clear its pending request. Each request has a twelve-second timeout, including reading the bounded response body. Malformed or duplicate participant rows reject without replacing the last verified list. Other actions are serialized; disable controls while an action is running. Pending joins return a clear message asking the member to wait for the host to open FL Studio.

`launchFL()` waits for `flim.exe` to validate the selected install, create FL, load the engine, and resume it. The native launcher owns its injection timeout and cleanup. The desktop host does not kill it on an external timer, which could strand a suspended FL process. A confirmed native failure can be retried. Missing or malformed success output sets `state: 'uncertain'` and blocks another launch, so an ambiguous result cannot silently open duplicate FL instances. Ask the user to check FL before leaving and retrying.

Leaving a room or closing this app does not close FL Studio or disconnect an already launched client. The actual session ends when its host leaves in FL or closes the hosted FL instance. The UI must say this near its leave action. Reopening the desktop app requires a new create/join action; room credentials are intentionally not persisted.

For a host room with `state: 'ready'` and `status: 'waiting'`, `leaveRoom()` posts the private
host ticket to `/rooms/cancel` with a three-second timeout. Members and launched/uncertain
rooms never use cancellation. Failure returns to the form with `notice` explaining expiry;
an already unavailable reservation needs no warning. The endpoint is currently local and
undeployed, so the notice path remains expected against an older relay. Discovery deduplicates
directory aliases and runs at most three inspections together, preserving installation order
and any valid saved selection.

Only `{name, flPath}` is stored in `preferences.json` under Electron's user-data directory. Room passwords and scoped hosting tickets stay in main-process memory and are passed only to the launched native child environment. The renderer never receives them, and the app never includes the account-wide hosting key.

Production expects `electron/main.cjs`, `electron/preload.cjs`, `electron/bridge-core.cjs`, and `dist/index.html` inside the packaged app. The launcher, engine DLL, and reviewed `profiles/*.profile` belong in `resources/native`, outside the ASAR archive. Include only those native artifacts, not test launchers or `.obj`/`.res` files. Production always loads the packaged renderer even if `FLIM_DESKTOP_DEV=1` is inherited. Development with that flag uses only `http://127.0.0.1:5173/`.

The renderer has context isolation, sandboxing, no Node integration, no external navigation, no popups or webviews, and no browser permissions. Vendor component scripts, images, and fonts locally. Put the production content-security policy in the HTML as well as the main-process response handler; file protocol responses must not be the only place policy is specified. Production renderer networking is disabled; all relay access goes through the fixed main-process endpoint.

Run `node --test test/bridge.test.cjs` from `client/desktop` for the bridge regression suite. These tests stub process creation and never launch FL Studio.
