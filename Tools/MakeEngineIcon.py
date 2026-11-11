"""Renders the Stardust engine icon (Engine/Resources/EngineIcon.svg) to the files the engine uses:
engineIcon.png (256 px, editor menu bar, Hub and window icon) and Engine/Resources/EngineIcon.ico
(16-256 px, embedded in the editor and the default player). Needs Pillow.

The shapes are drawn here directly from the SVG's coordinates (rounded tile, isometric cube, star),
supersampled 8x for smooth edges. Keep both in sync when the icon changes.
"""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
SS = 8  # supersampling factor


def cubic(p0, p1, p2, p3, steps=24):
    out = []
    for i in range(1, steps + 1):
        t = i / steps
        u = 1 - t
        out.append((u**3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t**3 * p3[0],
                    u**3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t**3 * p3[1]))
    return out


def star():
    # M65 32 C66.33 38 70 41.67 76 43 C70 44.33 66.33 48 65 54 C63.67 48 60 44.33 54 43 C60 41.67 63.67 38 65 32 Z
    pts = [(65, 32)]
    pts += cubic((65, 32), (66.3333, 38), (70, 41.6667), (76, 43))
    pts += cubic((76, 43), (70, 44.3333), (66.3333, 48), (65, 54))
    pts += cubic((65, 54), (63.6667, 48), (60, 44.3333), (54, 43))
    pts += cubic((54, 43), (60, 41.6667), (63.6667, 38), (65, 32))
    return pts


def render(size):
    big = size * SS
    k = big / 130.0
    image = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    d = ImageDraw.Draw(image)
    s = lambda pts: [(x * k, y * k) for x, y in pts]
    d.rounded_rectangle([0, 0, big - 1, big - 1], radius=30 * k, fill="#141A26")
    d.polygon(s([(65, 22), (102, 43), (65, 64), (28, 43)]), fill="#5EA6F5")
    d.polygon(s([(28, 43), (65, 64), (65, 108), (28, 87)]), fill="#2F71C8")
    d.polygon(s([(65, 64), (102, 43), (102, 87), (65, 108)]), fill="#1D4A8F")
    d.polygon(s(star()), fill="#FFFFFF")
    return image.resize((size, size), Image.LANCZOS)


if __name__ == "__main__":
    render(256).save(ROOT / "engineIcon.png")
    sizes = [16, 24, 32, 48, 64, 128, 256]
    frames = [render(n) for n in sizes]
    frames[-1].save(ROOT / "Engine" / "Resources" / "EngineIcon.ico", format="ICO", sizes=[(n, n) for n in sizes], append_images=frames[:-1])
    print("Wrote engineIcon.png and Engine/Resources/EngineIcon.ico")
