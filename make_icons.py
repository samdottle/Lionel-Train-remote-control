"""
Generates icon-192.png and icon-512.png for the O27 Controller PWA.

v2: a single bold speed-dial gauge, no separate "device body" rectangle.
The first version paired a rounded rectangle with a circle (echoing the
Digital Level 1 RV icon's layout), but the tick marks and needle needed to
read it as a *dial* rather than just "a square and a circle" disappear at
icon size — so the two bold overlapping outlines were all that actually
showed up. This version drops the rectangle and thickens everything else
so the gauge reads clearly even small.
"""
import math
from PIL import Image, ImageDraw

BG = (10, 15, 30, 255)      # #0a0f1e
FG = (251, 191, 36, 255)    # #fbbf24


def draw_icon(size):
    img = Image.new("RGBA", (size, size), BG)
    d = ImageDraw.Draw(img)

    cx, cy = size * 0.5, size * 0.5
    r = size * 0.34
    ring_stroke = max(3, round(size * 0.05))
    needle_stroke = max(4, round(size * 0.065))

    start_deg, end_deg = 125, 415  # ~290deg sweep, OFF -> FULL SPEED

    def pt(radius, deg):
        a = math.radians(deg)
        return (cx + radius * math.cos(a), cy + radius * math.sin(a))

    # Dial arc (open at the bottom, like the real OFF..FULL SPEED sweep).
    bbox = [cx - r, cy - r, cx + r, cy + r]
    d.arc(bbox, start_deg, end_deg, fill=FG, width=ring_stroke)

    # A few bold tick marks — fewer and thicker than v1 so they survive
    # downscaling instead of turning into visual noise.
    for i in range(5):
        deg = start_deg + (end_deg - start_deg) * i / 4
        x1, y1 = pt(r * 1.14, deg)
        x2, y2 = pt(r * 1.32, deg)
        d.line([x1, y1, x2, y2], fill=FG, width=max(2, round(size * 0.022)))

    # Needle, parked partway around the sweep.
    needle_deg = 250
    nx, ny = pt(r * 0.8, needle_deg)
    d.line([cx, cy, nx, ny], fill=FG, width=needle_stroke, joint="curve")
    # Round the needle's outer tip to match the line caps PIL doesn't add.
    tip_r = needle_stroke / 2
    d.ellipse([nx - tip_r, ny - tip_r, nx + tip_r, ny + tip_r], fill=FG)

    # Center hub, drawn last so it sits cleanly over the needle base.
    hub = size * 0.075
    d.ellipse([cx - hub, cy - hub, cx + hub, cy + hub], fill=FG)

    return img


for size, name in [(192, "icon-192.png"), (512, "icon-512.png")]:
    draw_icon(size).save(name)
    print(f"wrote {name}")
