"""Generate flim.exe's icon: an original fruit of my own design (a round berry
with a leaf + highlight). Not FL's logo. Outputs flim.ico with several sizes."""
from PIL import Image, ImageDraw
import os, math

def draw(sz):
    S = sz * 4  # supersample
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    cx, cy, r = S * 0.52, S * 0.58, S * 0.33
    # body: a purple/blue berry (original palette, distinct from FL's)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(124, 92, 246, 255))
    # subtle lower shading
    d.ellipse([cx - r * 0.9, cy - r * 0.5, cx + r * 0.95, cy + r * 1.05],
              fill=(92, 64, 210, 120))
    # glossy highlight
    hr = r * 0.42
    d.ellipse([cx - r * 0.55, cy - r * 0.62, cx - r * 0.55 + hr, cy - r * 0.62 + hr],
              fill=(255, 255, 255, 150))
    # stem
    d.line([cx + r * 0.1, cy - r * 0.95, cx + r * 0.35, cy - r * 1.35],
           fill=(90, 60, 40, 255), width=max(2, S // 40))
    # leaf
    lx, ly = cx + r * 0.35, cy - r * 1.3
    leaf = [(lx, ly), (lx + r * 0.75, ly - r * 0.25), (lx + r * 0.95, ly + r * 0.1),
            (lx + r * 0.4, ly + r * 0.22)]
    d.polygon(leaf, fill=(72, 199, 142, 255))
    return img.resize((sz, sz), Image.LANCZOS)

out = os.path.join(os.path.dirname(__file__), "launcher", "flim.ico")
os.makedirs(os.path.dirname(out), exist_ok=True)
sizes = [16, 24, 32, 48, 64, 128, 256]
imgs = [draw(s) for s in sizes]
imgs[0].save(out, format="ICO", sizes=[(s, s) for s in sizes], append_images=imgs[1:])
print("wrote", out, "sizes", sizes)
