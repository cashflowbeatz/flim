# Credits and third-party notices

**Original project:** [fckwader/flim](https://github.com/fckwader/flim). This fork retains its Git
history, native architecture and original documentation in [LEGACY.md](LEGACY.md). Rooms and
the compatibility/reliability additions are maintained in
[cashflowbeatz/flim](https://github.com/cashflowbeatz/flim). This contribution does not assign a
new license to the upstream work. At publication, upstream does not include a project LICENSE file.

**FL Studio / Image-Line:** proprietary software installed separately by each user. No FL binaries,
factory presets, licenses or decompiled FL source are distributed. flim is unofficial and not
affiliated with or endorsed by Image-Line.

**General Sans:** by Indian Type Foundry, obtained through
[Fontshare](https://www.fontshare.com/fonts/general-sans). Its ITF Free Font License permits
application embedding but restricts standalone redistribution. Obtain font files directly from
Fontshare for local builds; the standalone files are excluded from this repository. The font
license does not become a license for flim. See the license supplied with the official download.

**App artwork:** the connected computers/keyboard illustration and simplified icon were generated
with OpenAI's image-generation tool at the fork owner's request. The bundled masters and
derivative/icon build process are documented in [ARTWORK.md](client/desktop/ARTWORK.md).
The app's React `RoomModule` adapts the owner's visual references; unused historical Framer exports
are not included in the published source or production bundle.

**Dependencies:** React/React DOM, Framer Motion, Electron, Vite, TypeScript, electron-builder,
Playwright, Cloudflare Wrangler/Miniflare, esbuild, ws and other lockfile dependencies retain
their own notices/licenses. Package names/versions are pinned in the relevant package lockfiles;
generated packages carry their dependency notices. No dependency license grants rights to FL.
