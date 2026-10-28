# GitHub presentation checks

Publication review, 2026-10-28. antislop was applied during editing under the owner's existing
session choice. This is a repository/documentation update; the app UI and packaged executable
are unchanged. Existing design direction is the compact equipment card, ENERGY 1 / RHYTHM 1 /
MOTION 2. Documentation uses static GitHub layout and its native accessible controls.

The GitHub Markdown API rendered the README. A local preview of that rendered HTML was checked
at 1280px and 375px; all four images loaded, each had alt text, no page overflow or browser errors
occurred, and the download link accepted keyboard focus. Both screenshots were inspected.
The preview approximates GitHub's surrounding CSS; GitHub controls its actual light/dark themes.

## Delivery gate

- R-02 PASS: new README/guides contain no em-dash emphasis.
- R-03 PASS: 1280px/375px preview checks report no page overflow; images scale and tables scroll.
- R-17 PASS: test counts are tied to versioned QA and rerun logs, not promotional statistics.
- R-18 PASS: there are no testimonials or invented customers.
- R-23 PASS: existing requested logo/art and actual app screenshots are reused; fixture details are labelled.
- R-24 PASS: all local Markdown/image destinations exist; release links target the publication tag.
- R-25 PASS: preview text/white contrast is 15.80:1; link/white is 5.19:1. App screenshot palette is unchanged.
- R-26 PASS: links have actual destinations; screenshot controls are illustrations, not fake interactive widgets.
- R-27 PASS: troubleshooting covers missing installs, waiting/stale files, offline/expired rooms and native failures.
- R-28 PASS: help addresses specific room, MIDI, plugin and storage behavior.
- R-32 PASS: preview download link takes keyboard focus; native GitHub links retain keyboard behavior.
- R-33 PASS: presentation is stored as Markdown/HTML in source; no runtime source-patching mechanism was added.
- R-34 PASS: no custom theme toggle; native GitHub theme behavior is retained.
- R-35 PASS: desktop build, 177 backend tests, 127 room/UI checks and 68 Files checks reran successfully.
- R-36 PASS: exact builds, older fallback, unavailable content and absent fresh live tests are explicit.
- R-37 PASS: existing app design direction is retained, with static documentation appropriate to GitHub.
- R-38 PASS: screenshot transfer/person details are explicitly identified as demonstration fixtures.
- R-01 PASS: no new gradient or glow; existing artwork identifies the music collaboration workflow.
- R-04 PASS: the existing computers/keyboard logo depicts the product; no decorative icon set was added.
- R-06 PASS: native GitHub typography serves reading; General Sans remains an app asset obtained separately.
- R-07 PASS: no decorative background grid.
- R-08 PASS: Files arrows quote the actual sidebar control; no repeated ornamental action arrows.
- R-09 PASS: no promotional capsules, uptime or security badges.
- R-10 PASS: no glass surfaces.
- R-12 PASS: no added shadows or floating feature cards.
- R-13 PASS: no added glow.
- R-14 PASS: features use a behavior table and prose rather than repeated marketing cards.
- R-19 PASS: documentation is static; existing app motion/reduced-motion behavior is unchanged.
- R-22 PASS: retained equipment illustration is the owner's requested identity motif.
- L-1 PASS: app dials remain 1/1/2; GitHub documentation introduces no animation.
- L-2 PASS: calm hierarchy, native headings and functional navigation match that direction.
- L-3 PASS: compatibility and the download action are the first focal points.
- L-4 PASS: sections separate setup, features, evidence and operator details.
- L-5 PASS: existing coral artwork supplies identity; native blue links identify navigation.
- L-6 PASS: the computers/keyboard motif appears in the logo and real app views.
- L-7 PASS: the existing design read informed the presentation before editing.
- C-1 PASS: screenshots show the actual setup/sidebar; the matrix explains concrete compatibility choices.
- C-2 PASS: download/setup/help/build/review destinations have implemented content.
- C-3 PASS: each guide serves a participant, operator or contributor task.
- C-4 PASS: responsive preview and current app keyboard/zoom/error checks passed.
- C-5 PASS: automation, earlier live observations and remaining coverage are separated.
- R-05 PASS: composition follows compatibility, setup, capabilities and evidence without template filler.
- R-11 PASS: no custom card/button radius system was added to GitHub.
- R-15 PASS: download/navigation labels name their actions; Get started quotes the owner's app label.
- R-16 PASS: new copy describes behavior instead of promising universal/seamless/bug-free operation.
- R-20 PASS: FL build matrix, room numbers, recorded notes and sample paths make the presentation specific.
- R-21 PASS: no forced dark/light documentation theme.
- R-29 PASS: native GitHub palette and existing artwork are retained.
- R-30 PASS: no unrelated product template was copied.
- R-31 PASS: visuals identify the real app, tables explain compatibility, and guides isolate practical tasks.

## Source and artifact checks

The publication scan found no credential patterns, personal account paths, oversized source
files, invalid JSON or broken local document links. Private auth state, native/desktop build
outputs, raw evidence/logs, FL projects/presets and standalone font files are excluded from Git.
The portable app is a release asset, not a Git blob. Its SHA-256 is recorded in
[0.2.23.sha256](releases/0.2.23.sha256). Hosted CI status is separate from local test results.
