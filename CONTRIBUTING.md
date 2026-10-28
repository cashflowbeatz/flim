# Contributing

Start with the [architecture/review map](docs/ARCHITECTURE.md), [build instructions](docs/BUILDING.md)
and [test guide](docs/TESTING.md). Credit the original [fckwader/flim](https://github.com/fckwader/flim)
when describing or redistributing work based on it. No new project license is assigned here;
third-party dependencies retain their own terms.

For a bug, include the flim version, exact FL build on each side, host direction, steps and
expected/actual behavior. Explain whether it occurred with a missing plugin, fallback workspace
or pending sample. Redact public diagnostics. Do not attach FL binaries, licenses, private
presets, personal projects, room credentials or Cloudflare keys.

Keep changes focused and describe their practical effect. Add a meaningful regression for
native/protocol/state bugs. List checks actually run and distinguish synthetic/loopback tests
from live FL results. Do not call a patch universally compatible because a version label matches.

New FL profiles require the exact engine identity, reviewed address/layout evidence and runtime
checks. Follow [Adding an FL build](client/dev/README.md#adding-an-fl-build); never disable
fingerprint/size/queue/project-preservation guards to make admission succeed.

Builds and local tests do not deploy. Cloudflare deployment, public releases and changes to
live rooms must be deliberate operator actions. Test with owned scratch sessions and avoid
touching someone else's running FL project.
