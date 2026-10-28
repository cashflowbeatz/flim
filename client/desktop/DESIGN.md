# flim Rooms: current design direction

The user's supplied dropdown and profile-card references add softly rounded surfaces and padded
choices. No black input outlines, Menu, red input boxes, redundant lines or circular ornament.
Keep the supplied equipment artwork, logo, white component and General Sans. Anonymous
numbered rooms and the requested Get started label remain. No account or signup flow.

Reading this as: compact FL collaboration controls for musicians, in the supplied equipment-card
language, ENERGY 1 / RHYTHM 1 / MOTION 2.

0.2.21 adds received-file location controls inside Files, available before room setup. A plain
path, native Choose folder action, optional Use default action and Open samples folder avoid
another menu or modal inside the renderer. Paths wrap at folder separators and remain selectable,
with a left-to-right isolated path inside the existing typography. One explanation identifies
next-session behavior and the active-room lock. Pending, saved, invalid and retry states use
text, the existing notice surface and the established blue keyboard focus. No new palette,
artwork or motion. ENERGY 1 / RHYTHM 1 / MOTION 2 are retained. Native local-storage failures
use the existing waiting group, with a specific explanation instead of a false corruption label.

0.2.20 keeps the same visual direction. This pass improves the reliability beneath the current
controls rather than adding another panel. Settings read failures use the existing notice area.
Recovery retains its reload/log actions when window fitting is unavailable, guards duplicate
log clicks, and refits after content wraps or changes size. No new palette, artwork or motion.

0.2.19 keeps that direction. The Files view remembers the current room's query, filter, ordering,
scroll and confirmed snapshot; this avoids rebuilding a musician's place after checking a room.
Cached activity is labelled until confirmed again. Inset Clear preserves field width. Filter/group
counts and an existing check communicate available results and selection without color alone.
Name order stays the default; optional Recent order serves finding a newly touched sample.
One explanation per waiting/allowance group reduces repeated prose. Scroll hints/tab stops follow
measured overflow. The shared logs/caption row follows both columns when Files is open, preserving
narrow-view diagnostics and matching keyboard/visual order. Common room status prevents the badge,
artwork caption and window title disagreeing during offline, stale and closed states. Existing
check and arrow shapes carry state/navigation only. No new artwork, menu or decorative outline.

- The component is the application surface. A 16px corner and near-white fill group the room
  controls as one card, with no outer border or shadow. The equipment artwork keeps that corner.
- Room actions use a compact two-position switch; the white selected tab separates from its pale track.
  Tab semantics, one keyboard stop, arrows, Home and End make the switch predictable.
- Use softly rounded pale blue-gray fields with no dark perimeter. Keyboard focus uses a blue
  bottom edge and a tinted fill; invalid fields pair text with a dashed blue edge, never a red box.
- The FL picker adapts the supplied dropdown's padded rows, rounded sheet and small shadow.
  Its list expands in document flow so the native window grows instead of clipping the choices.
  One check identifies the selected FL family. Arrow keys preview, Enter/Space select, and Escape
  closes just the list. Tab and outside clicks dismiss without changing the chosen installation.
- Remove circular initials and participant rules; names, FL family and role carry the information.
Keep bounded scrolling and natural sorting so the room remains compact and stable.
- Copy room number is a text action with the existing copy symbol. Its wording describes the
  digits actually copied. No decorative action borders or inset outlines on button presses.
- Place Open test logs in the existing caption row, avoiding another footer row or a menu.
  Keep the app version visible beside the wordmark. Explain unavailable logs beside the component.
- Keep 44px action targets and structural whitespace. Bound the room canvas to 400 CSS pixels; native
  bounds follow content, monitor work area and zoom. Small viewports retain vertical scrolling.
- The blue focus accent follows the supplied dropdown reference's active indicator. Charcoal text on white carries
  hierarchy. General Sans preserves the original identity; the large room number aids dictation.
- Artwork text uses a solid charcoal backing; Get started uses an inset blue keyboard indicator.
  Existing artwork remains the identity motif. No new visuals or fabricated claims are introduced.
- Short expansion and hover changes explain interaction; reduced motion disables transitions.
  No perpetual movement. Keyboard navigation follows the visual order.
- Collapsing setup cancels pending paste ownership. Mode autofocus must not steal a user's newer
  cursor position. Restored controls open their existing room and respect work still held by main.
- Recoverable status/sample lookups retry within fixed limits. Quota, memory, invalid edit data
  and project-preservation protections remain. A failed auxiliary sample lookup retains the edit
  and original paths; an absent remote sample is not claimed to be available.
- A renderer crash can reload controls without discarding main-process state or stopping FL.
  Repeated failures reach a bounded recovery screen. All recovery attempts enter local diagnostics.

The FL toolbar stays status-only. No Host/Connect/Disconnect actions are reintroduced.

The user's latest TapIn reference supplies a tightly fitted widget shape, compact header and
integrated tools. The explicit correction requires a lateral Files sidebar, with room controls
remaining beside it. Use a 400px room surface and 680px total width with Files open. The native
titlebar controls occupy the app header, removing the separate Windows title strip; keep a
draggable header and native minimize/close behavior. Preserve the music equipment artwork and
General Sans instead of copying the reference's account, voice or chat features.

An always-visible Files label and directional arrow sit at the card's upper right edge, before
room setup as well as inside a room. They toggle the sidebar without changing the room or
requiring the room controls to be expanded. One Back action sits at the other end of that row;
remove the second Room/Files tab row. Pre-room Files explains how samples arrive and links
directly to setup, without reading transfers from a previous room. A short
horizontal reveal explains its placement; reduced motion removes movement. Below 600 CSS pixels,
the sidebar occupies the available app surface and Hide files restores the room, so zoom and
small monitors do not squeeze two unusable columns or turn Files into a dropdown.

The pale blue-gray sidebar separates transfer status without dark outlines or decorative rings.
Group actual transfer states, show byte progress and explain failures in text. Bound the list
height and wrap filenames. Hide files and Escape restore focus to Files. Open samples folder
has visible pending and error states. Show no fabricated activity or full source paths. Poll local
snapshots only while Files is visible; failed reads retain the last confirmed list.

Create and Join share a measured grid slot for the room explanation/number field, so switching
keeps the common fields and submit button in place. Inactive number controls are hidden from
assistive technology and disabled. Autofocus runs immediately after the mode commits, before
the next paint, and cannot steal a newer field selection. It does not scroll the window.
When Files opens, resize the native width before measuring the new responsive height. The app
starts 24px below the work area's upper edge so setup and the picker can grow downward without
moving the header. Monitor bounds still take priority if a user moves the window near an edge.

Native transfer workers retain sample-dependent edits through recoverable network failures and
retry with a growing delay capped at 30 seconds, respecting the one-minute allowance pause.
The original ordered edit is released once, after files verify; never replay a completed channel
snapshot later because that could overwrite newer edits. Session-generation changes cancel
old pending work. Corrupt downloads remain pending and retry; unverifiable bytes never become
usable files. Existing terminal local-file errors and size/memory protections remain explicit.
These workers continue when Files or the desktop app closes while FL Studio remains running.

The app component uses React and Framer Motion directly. Its entrance changes opacity without
moving form controls during focus or pointer interaction. Participant names use two compact
columns to keep the room fitted to content; there are no fabricated invite slots or avatars.

Exact native profiles remain required; the version picker must not imply every FL patch is verified.
The tested FL24.2.2.4597 installation is supported, without an experimental label or opt-in.
Its exact engine fingerprint check and project-preservation protections remain enabled.

QA: build and click through closed, create, join, waiting, active, ended, offline, setup failure,
invalid input, missing native files, unsupported build, unavailable logs and failed clipboard.
Verify keyboard switching, immediate typing, ordinary and deferred paste, room restoration,
busy restoration, narrow layouts, forced colors, reduced motion and 200% native zoom.
Test real renderer crashes in an isolated profile; never crash or replace the user's FL session.
Verify native sample lookup failures on loopback and native harnesses separately from UI fixtures.

The 0.2.18 refinement adds filename search and four plain status filters inside Files. Search
helps locate an actual sample, while All/Pending/Attention/Ready answer which files need work.
Keep their 44px targets and two-column arrangement in the narrow sidebar. The white selected
surface, blue selected label and pressed state distinguish selection without dark outlines.
Reserve the Clear action's space so typing does not resize the input. Search and filters only
change the visible list; they never start a transfer or contact a separate service.

Separate waiting work from active transfer counts. Show actual received/sent bytes, direction,
unknown sizes and current room number. Natural filename ordering makes the same file easy to
find between updates. Preserve confirmed rows through partial read failures and explain the
unavailable status. The manual Refresh action reports its own pending state; automatic reads
keep its label still. Slower status retries reduce disk reads during errors, and hidden or
closed views stop automatic status checks. Native sample delivery keeps its existing behavior.

Picker entrance now changes opacity only to keep its pointer targets still. Keyboard previews
scroll the option list alone, without moving the whole app. Input composition must not submit,
collapse setup or select an FL installation. Delayed paste belongs to the current input focus.
Measure participant overflow instead of guessing from headcount, with one participant column
below 340px. A global actionable error stays visible with Files open, including the narrow view,
and dismissing it restores useful focus. A 56px submit minimum prevents busy labels changing
the button's height at the normal compact width. These choices retain the existing dials.

The 0.2.22 direction controls answer whether a file is coming to this computer or leaving it.
Both / To me / From me use the existing selected white surface, blue text and pressed semantics,
with three equal 44px targets. This is an additional view of real transfers, never a transfer
start button. Direction persists with the room's other Files view settings. A limited-list
notice explains coverage instead of implying all files are visible; it uses the existing
warning surface, without an icon or decorative border. The end-of-list hint responds to actual
scroll position and the current result set. It does not measure-and-update React during layout.

Ctrl+F from room controls opens the lateral Files panel and focuses search. Modified shortcuts
retain their existing meaning. Folder progress names the actual pending operation; resetting a
folder restores focus when its reset button disappears. Keyboard and mouse previews share the
same FL-picker active option. No assets, typography, palette, dials or animation are added.
