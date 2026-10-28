# Local verification for 0.2.8

The source, native payload and Windows app were built locally. The Worker was not deployed and
GitHub was not changed. This report does not establish live FL24/25/26 project or hardware MIDI
compatibility. The previous isolated FL launch was rejected by automatic approval review with
only “blocked by policy”; it was not retried. Existing live results belong to their earlier payloads.

Checks: 67 desktop tests; 60 local Worker tests including typecheck/build; 36 browser/Electron UI
checks; 25 native synthetic harness programs; the local HTTP transfer fixture; and a dedicated
Electron geometry check. Package checks verify source/payload equality, installed FL discovery,
icons, diagnostics and startup. The portable test opens the distributed executable. They do not
allocate a real room or launch FL. Exact hashes and final package results are in
[the compatibility record](../dev/compatibility/desktop-0.2.8-2026-10-27.json).

Native regressions include pending local recording during a remote merge, duplicate delivery,
new full-snapshot scope, wrong delta scope, history budget exhaustion, preserving tombstones
through an unchanged structural refresh, avoiding retired scopes, and empty-pattern scope
delivery to joiners. A 6,000-note update performs one addition and zero deletions for one new
note. The pure CRDT harness covers 200 rounds of three replicas, duplicate notes, concurrent
moves/deletes, reordered changes, malformed wire data, allocation/RNG failures and history caps.

Recorded notes are sent after FL exposes them in its note array. The batching interval is not an
end-to-end latency guarantee. Unrecorded audition and audio streaming were not implemented. CRDT
activation requires the updated relay; the deployed relay selects locked snapshots. The oldest
build must still host a mixed-version room. FL24 remains an exact-fingerprint experimental option.

## Design gate

Design read: preserve the requested single Framer card and equipment artwork; fit the native
window around it. ENERGY 1 / RHYTHM 1 / MOTION 2. The resize debounce follows card expansion and
keeps overflow scrollable. Default inspected bounds were 536 × 616 pixels including the native
frame; opening the menu grew to 536 × 962 on this monitor. Work-area and zoom values vary by device.
Screenshots were inspected at default zoom and 200%; browser screenshots cover 320/375/810/1200
pixel widths. Generated evidence is in `test/evidence/`, excluded from the application.

R-02 PASS: no new interface prose or em-dash styling; existing interface wording retained.
R-03 PASS: width/overflow assertions pass at narrow widths and in the zoomed native window.
R-17 PASS: no marketing statistics; dimensions and test counts come from executed checks.
R-18 PASS: no testimonials; participant fixtures remain in tests.
R-23 PASS: no assets or navigation added; existing user-approved computer/keyboard logo retained.
R-24 PASS: no new navigation links; existing controls are actions or real disclosures.
R-25 PASS: colors unchanged; prior checked muted/placeholder/error/primary ratios remain 4.94/4.58/7.38/15.14:1.
R-26 PASS: UI click-through covers expand/collapse, room modes, picker, inputs, submit, retry, copy, refresh, logs and return.
R-27 PASS: loading, no installation, unsupported build, errors, waiting, active and ended states pass.
R-28 PASS: no FAQ or filler content.
R-32 PASS: Escape/focus-return, visible input focus, keyboard disclosure and native scrolling checks pass.
R-33 PASS: window UI edits were made in App.tsx, main/preload and the geometry helper source.
R-34 PASS: no theme toggle; existing approved light card and dark equipment artwork retained.
R-35 PASS: renderer build and click-through, native window and packaged/portable startup checks executed.
R-36 PASS: no zero-bug, universal-build, live-MIDI or production-CRDT claim; limits stated above.
R-37 PASS: existing DESIGN.md direction retained with explicit 1/1/2 dials and window-fit rationale.
R-38 PASS: no invented production content; test fixtures stay outside the packaged application.

R-01 PASS: existing dark image overlay supports white equipment-caption text; no new glow/gradient.
R-04 PASS: existing chevrons indicate disclosure; approved computer/keyboard mark identifies the app.
R-06 PASS: General Sans and hierarchy retained; DESIGN.md records the supplied component direction.
R-07 PASS: no decorative grid or blueprint background added.
R-08 PASS: arrows remain limited to existing primary actions/disclosures; none added.
R-09 PASS: no decorative status badge added.
R-10 PASS: no glass surfaces added.
R-12 PASS: existing single card shadow distinguishes the card from its canvas.
R-13 PASS: no glow system.
R-14 PASS: one room module, no repeated feature-card template.
R-19 PASS: existing short expansion and reduced-motion overrides retained; native resizing is debounced.
R-22 PASS: existing equipment artwork depicts the requested shared MIDI workflow.

Liveliness dials PASS: 1/1/2 retained in DESIGN.md; no additional animation or decorative sections.
Focal point PASS: the equipment card remains the single primary module, as visually inspected.
Whitespace PASS: canvas follows the module rather than filling a large fixed-height window.
Accent PASS: existing coral cables and logo dot retain the single identity accent.
Motif PASS: linked computers/keyboard remains consistent in the module, header and executable icon.
Design read PASS: established direction retained before edits; geometry purpose recorded in DESIGN.md.

C-1 PASS: native width, frame and height follow the card; monitor cap and zoom address concrete clipping cases.
C-2 PASS: all affected controls remain functional in UI checks.
C-3 PASS: no template sections or account flow added.
C-4 PASS: narrow/short windows, zoom, keyboard, loading and error states pass.
C-5 PASS: no fabricated claims; local/synthetic and native end-to-end limits distinguished.
R-05 PASS: single requested module, no template landing-page structure added.
R-11 PASS: existing differentiated radii retained for card, inputs and buttons.
R-15 PASS: Get started is the user's explicit requested label; room actions identify their actual operation.
R-16 PASS: no marketing buzzwords added to the interface.
R-20 PASS: requested equipment motif and room-number flow retained, as visually inspected.
R-21 PASS: approved light canvas and image direction retained; no forced theme change.
R-29 PASS: existing warm gray, charcoal and coral palette unchanged.
R-30 PASS: user-supplied Framer component retained; no copied product visual system introduced.
R-31 PASS: window geometry and retained visual choices have written reasons in DESIGN.md.
