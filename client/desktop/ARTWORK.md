# Session artwork

The user requested removing the portrait behind Get started and replacing it with computers linked through a keyboard. The replacement is bundled at `public/framer-assets/session-link.png` and is used in both card variants. The portrait is removed from the app's public assets.

Generated with the built-in image-generation tool on 2026-10-23. It is a decorative illustration of music collaboration, not an assertion about supported hardware connections.

## Generation prompt

```text
Use case: stylized-concept.
Asset type: background illustration for flim, a compact desktop app for collaborating in FL Studio.
Primary request: replace a portrait with a clear, logo-like illustration of two computers connected through a MIDI piano keyboard, expressing musicians making one project together.
Scene: two small desktop computer displays at left and right, showing simple abstract piano-roll note blocks with no text, linked by visible cables to one compact MIDI piano keyboard in the foreground. No people.
Style: polished minimal 3D editorial illustration, rounded but precise equipment, matte charcoal and warm off-white materials, restrained coral-red accents that fit the existing flim app. Clean and recognizable at small size.
Composition: landscape image, approximately 3:2. Main equipment forms one balanced horizontal arrangement around the middle: computers on left/right, keyboard just below center, curved connecting cables. Keep the exact center as calm empty negative space for the real app's Get started button; do not draw a button. Put the entire important arrangement in the central 55 percent of image height so a wide shallow crop still makes sense. Dark warm charcoal background, subtle studio light, strong readable silhouettes, generous outer margin.
Text: none.
Avoid: people, portraits, faces, hands, letters, words, logos, brands, watermarks, glowing neon, extra instruments, photographic room clutter, fake app controls.
```

## App logo and Windows icons (0.2.2)

The user requested turning the connected computers/keyboard artwork into the app logo and Windows taskbar icon. The built-in image-generation tool produced a separate simplified mark on 2026-10-23, using the existing illustration as a subject/palette reference. It shows two screens linked to a single keyboard, with off-white shapes and coral cables on a charcoal tile. Transparency outside the tile is preserved.

- Generated master: `artwork/flim-logo-master.png`.
- In-app mark: `public/brand/flim-logo.png` (256px).
- Window, executable and favicon: `public/brand/flim.ico`, with 16, 20, 24, 32, 40, 48, 64, 128 and 256px frames.
- Recreate the size/format derivatives with `npm run icons`. Electron's native image encoder resizes the master; the script writes a standard ICO directory around the PNG frames. No image API is needed to regenerate these derivatives.
- Original generated file: `exec-118cc06e-c806-435c-84e4-2f484e970414.png` in the conversation's generated-images directory. The project owns a copy of the master and does not depend on that external path.

### Logo generation prompt

```text
Use case: logo-brand.
Asset type: finished square Windows desktop application logo for flim, used as a taskbar icon and beside the app name.
Input image 1 is a reference for subject and palette only: the two connected computers and MIDI keyboard illustration. Create a NEW simplified logo rather than shrinking the scene.
Primary request: one bold compact emblem of two computer screens connected to one small piano keyboard, expressing two musicians sharing one FL Studio session.
Style: precise flat vector-like graphic, crisp solid shapes, balanced professional software icon. No photographic or 3D detail.
Composition: square canvas. A warm charcoal rounded-square tile fills about 90% of the canvas with transparent outside corners. Within it, two simple off-white outlined monitor shapes sit side by side in the upper half; each screen contains just one thick coral horizontal piano-roll note. Their bases connect through a simple thick coral U-shaped cable to a single compact off-white piano keyboard in the lower half, with only a few charcoal black keys. Make the whole linked shape unified, symmetrical, large and bold, with very thick strokes and generous spacing so it remains recognizable at 16 to 32 pixels.
Palette: charcoal #272629, warm off-white #F0EDE6, coral #ED7965. High contrast between shapes and tile.
Text: none. No letters, wordmark, gradients, shadows, glow, thin details, borders outside the tile, watermarks, people, hands or extra objects. Actual transparency outside the rounded-square tile. Output only one centered finished logo, not a presentation sheet or mockup.
```
