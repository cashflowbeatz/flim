# FL 25/26 builds and mixed sessions

This fork can load additional FL 25/26 compatibility profiles without recompiling flim. It also
negotiates sessions between different supported builds. **This does not establish support for
every FL 25/26 build.** Each engine still needs a reviewed profile, and mixed sessions have the
restrictions below.

## What a compatibility profile does

flim calls private functions inside FL and reads FL's internal objects. An FL update can move
those functions or change the objects, even in a small patch release. The wrong location can
crash FL or change the wrong data.

A profile lists all 62 addresses, 21 layout fields and the message-box calling mode for one
engine. It also contains the engine's SHA-256 fingerprint. The launcher and engine both check
the profile, fingerprint and basic address ranges before enabling flim. A complete, reviewed
profile can support an additional build without changing flim's executable or DLL.

The fingerprint establishes which engine file the profile describes. It does not prove that
the listed locations are correct. Profiles must come from a trusted maintainer who reviewed
the functions, object layouts, calling conventions and shared data formats, then tested them
in disposable FL instances. Do not turn a signature-scanner result into a runnable profile
without that work.

The two current engine-bound profiles are **25.2.5.5319** and **26.1.7.5654**. The existing built-in
tables for 25.1.6.4997, 25.2.3.5171 and experimental 26.1.6.5639 remain available. Historical
validation and current test results are recorded in [the compatibility directory](client/dev/compatibility/README.md).
An unknown build without a verified profile stays disabled; flim never borrows the nearest build's addresses.

## Installing the client

Build the Windows x64 client with `FLIM_EXPERIMENTAL_FL26=1` to include FL 25 and experimental
FL 26 support. `client/build.bat` writes the result to `client/out-experimental` and copies the
profiles beside it. Keep these together:

```text
flim.exe
flim_engine.dll
profiles/
    25.2.5.5319.profile
    26.1.7.5654.profile
```

Install later reviewed profiles in that same `profiles` folder, named after their exact build.
A present but invalid profile stops launch instead of silently falling back to a built-in table.
Installed FL files are not patched. The normal build remains FL 25 only.

## Collaborating across builds

Update the relay and everyone's flim client. **The person with the oldest FL build must host.**
For example, FL 25.2.5.5319 can host a participant using FL 26.1.7.5654. A participant with an
older build than the host is refused before receiving the project. Using matching builds
continues to support the existing feature set.

The client announces its FL build and shared data-format revision. The server checks these
before admission and includes the sender's admitted build on relayed edits. These versions
are reported by the clients; this is compatibility negotiation, not remote verification of
someone else's installed software. Old clients can still form separate legacy sessions on
the updated relay. New clients require the updated relay.

FL's own project writer creates the host snapshot, including when the host opens an older
project. The receiver checks the complete project stream and its version against the
negotiated host before loading it. The original version stamp is preserved. flim does not
translate a newer project into an older format.

## Mixed-session restrictions

Supported parameter, note, playlist, naming, color and pattern-operation messages can travel
both ways between builds sharing the reviewed wire format. Matching plugins and compatible
plugin versions are still required on each computer.

Channel creation, plugin replacement and whole plugin presets carry FL-specific saved data.
Only people using the host's exact FL build can send those states in a mixed session. Project replacement remains host-only. People
on newer builds should ask the oldest-build host to make those changes. If a newer member
makes an edit requiring an incompatible saved state, flim stops that member's sharing and
explains why; their local edit remains in their FL project. The server also rejects the edit
before forwarding it. Ordinary knob changes do not automatically mean a preset replacement,
but large plugin changes may be detected as a whole-state update.

Different builds therefore do not have identical editing privileges. This restriction avoids
the observed crash path where an older FL loader receives newer project or preset data.
Unknown operation formats are refused in a mixed session.

Pending inbox, outbox, held-project and main-thread tasks carry a session generation. Work
that outlives a disconnect or reconnect is discarded before sending or applying it. Downloads
already underway may finish into the content cache; that does not authorize their old operation
to enter a new session.

## Adding another build

Developers can use [the profile tool](client/dev/profiles.py) to create a review draft:

```powershell
python client/dev/profiles.py draft "C:\path\FLEngine_x64.dll" --output review.json
```

A known build starts with its existing numeric field set. An unknown build starts empty.
Map and review the new engine using the existing signature tools, focused REA/Ghidra analysis
and runtime layout checks. Fill every field and review item, bind the review to the field
fingerprint, and record successful runtime evidence. Export only after that review:

```powershell
python client/dev/profiles.py export "C:\path\FLEngine_x64.dll" --review reviewed.json --output 25.x.y.z.profile
```

The exporter refuses drafts, incomplete reviews, changed field fingerprints, engine mismatches,
unknown fields and invalid ranges. Updating the SHA-256 alone does not port a profile. A build
with a changed calling convention or shared record format may need client code changes as well.

Keep original projects and test with copies. Current checks cover the installed builds and
selected bundled plugins, not every third-party plugin, every historical build or future update.


## What passed on the installed builds

The final experimental client and updated relay passed 34 feature checks in each of three pairs:
FL 25.2.5.5319 hosting FL 26.1.7.5654, two FL 25 instances, and two FL 26 instances. Both native
refusal checks passed: incompatible newer-member presets cannot enter the older host, and an
older member cannot receive a newer host's project. Each runtime loaded its engine-bound profile.

The [runtime record](client/dev/compatibility/mixed-profile-runtime.json) preserves source and
binary fingerprints, log hashes, exact test scope and failed earlier attempts. Testing covers
these two installed builds and selected bundled plugins. It does not establish every historical
or future build, every plugin, or the normal UI path for opening older projects end-to-end.
