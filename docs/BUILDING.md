# Build Rooms from source

Participants use the [portable release](https://github.com/cashflowbeatz/flim/releases/tag/rooms-v0.2.23).
These instructions are for contributors. Building does not deploy a server or launch FL.

## Prerequisites

Windows x64, Git, Node.js **22.12+** (or a compatible newer LTS), npm, and Visual Studio C++
Build Tools with the Windows SDK. `client/msvc_env.bat` finds the x64 toolchain using `vswhere`,
or accepts `VCVARS` pointing to `vcvars64.bat`.

The app uses General Sans from Fontshare. Its standalone font files are intentionally excluded
from Git. Download [General Sans from Fontshare](https://www.fontshare.com/fonts/general-sans)
under its license, and copy the unmodified web fonts into:

```text
client/desktop/public/framer-assets/general-sans-regular.woff2
client/desktop/public/framer-assets/general-sans-medium.woff2
```

Use the archive's `Fonts/WEB/fonts/GeneralSans-Regular.woff2` and `GeneralSans-Medium.woff2`.
Without them the app falls back to its system font; visual/package font checks expect them.
[Notices](../THIRD-PARTY-NOTICES.md) explain the source and embedding terms.

## Build the complete native payload

From the repository root in PowerShell:

```powershell
$env:FLIM_EXPERIMENTAL_FL26 = '1'
cmd /c client\build.bat
if ($LASTEXITCODE) { throw 'Native build failed' }
Remove-Item Env:FLIM_EXPERIMENTAL_FL26
```

The historical flag name enables the combined FL24/25/26 payload at `client/out-experimental`.
It does not mark reviewed FL24 as experimental. Exact build/profile/fingerprint checks remain.
The default native build without that flag targets the original FL25 workflow.

## Build and package the desktop app

```powershell
Set-Location client/desktop
npm ci
npm test
npm run build
npm run package
```

Output: `client/desktop/release/flim-rooms-0.2.23-win64.exe`. The packaging configuration includes
only the renderer, Electron bridge/recovery code and the native launcher/engine/profiles. It
does not include tests, preferences, local credentials or diagnostics. Icons can be regenerated
from the included master with `npm run icons`.

For local renderer development run `npm run dev`, then in another terminal set
`FLIM_DESKTOP_DEV=1` and run `npm start`. Production ignores that flag and loads packaged content.
The renderer uses a restricted preload bridge and has no Node.js access.

## Relay selection and deployment

The current app's default relay is the fork's community endpoint in
[`electron/bridge-core.cjs`](../client/desktop/electron/bridge-core.cjs). To distribute your own
build against a different relay, review/change that constant before building and rerun tests.
The renderer cannot choose arbitrary relay URLs. The account-wide hosting key is never bundled.

[Cloudflare setup](../server/cloudflare/README.md) is a separate operator task. `npm test` in
that directory uses local Miniflare; `npm run deploy` publishes. Neither building Rooms nor
publishing a GitHub release changes a Cloudflare account.

[Tests](TESTING.md) · [Architecture](ARCHITECTURE.md) · [Native development tools](../client/dev/README.md)
