"""Draws the OscreenSaver icon: the letters OTS in a dash-dot frame with a mouse cursor, no background.

Run:  python3 res/make_icon.py   (needs Pillow and the DejaVu Sans Bold font)
Writes res/app.ico with sizes 16..256 and res/icon_preview.png.
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

BLUE = (0, 120, 212, 255)
BLACK = (24, 24, 24, 255)
WHITE = (255, 255, 255, 255)
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
HERE = Path(__file__).resolve().parent


def dash_dot_line(d, a, b, width, dash, dot, gap):
    """Dash-dot pattern along a straight horizontal or vertical line."""
    (x0, y0), (x1, y1) = a, b
    horizontal = y0 == y1
    length = abs(x1 - x0) if horizontal else abs(y1 - y0)
    pos = 0.0
    pattern = [(dash, True), (gap, False), (dot, True), (gap, False)]
    i = 0
    while pos < length:
        seg, draw = pattern[i % 4]
        end = min(pos + seg, length)
        if draw:
            if horizontal:
                d.line([(x0 + pos, y0), (x0 + end, y0)], fill=BLUE, width=width)
            else:
                d.line([(x0, y0 + pos), (x0, y0 + end)], fill=BLUE, width=width)
        pos = end
        i += 1


def cursor(d, x, y, s):
    """Classic arrow pointer, black with a white outline, tip at (x, y), height about 18*s."""
    pts = [(0, 0), (0, 16), (4.2, 12.3), (7, 18.2), (9.6, 17.1), (6.9, 11.3), (12, 11.3)]
    poly = [(x + px * s, y + py * s) for px, py in pts]
    # White edge: a thick line along the outline, then the black arrow on top of it.
    edge = max(2, round(2.2 * s))
    d.polygon(poly, fill=WHITE)
    d.line(poly + [poly[0]], fill=WHITE, width=edge, joint="curve")
    d.polygon(poly, fill=BLACK)


def draw(size):
    k = 8  # draw large, then shrink: smooth edges at every size
    n = size * k
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    small = size <= 24

    inset = n * 0.06
    l, t, r, b = inset, inset + n * 0.08, n - inset, n - inset - n * 0.08
    w = max(k, round(n * (0.055 if small else 0.04)))
    if small:
        # Very small icons: a solid frame reads better than a dashed one.
        d.rectangle([l, t, r, b], outline=BLUE, width=w)
    else:
        dash, dot, gap = n * 0.11, n * 0.025, n * 0.04
        dash_dot_line(d, (l, t), (r, t), w, dash, dot, gap)
        dash_dot_line(d, (r, t), (r, b), w, dash, dot, gap)
        dash_dot_line(d, (l, b), (r, b), w, dash, dot, gap)
        dash_dot_line(d, (l, t), (l, b), w, dash, dot, gap)

    text = "OTS"
    font_size = n * (0.36 if small else 0.33)
    font = ImageFont.truetype(FONT, int(font_size))
    box = d.textbbox((0, 0), text, font=font)
    tw, th = box[2] - box[0], box[3] - box[1]
    cx, cy = (l + r) / 2 - n * 0.03, (t + b) / 2 - n * 0.02
    d.text((cx - tw / 2 - box[0], cy - th / 2 - box[1]), text, font=font, fill=BLUE)

    cursor(d, n * 0.64, n * 0.55, n / 46)
    return img.resize((size, size), Image.LANCZOS)


def main():
    sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
    images = [draw(s) for s in sizes]
    images[-1].save(HERE / "app.ico", sizes=[(s, s) for s in sizes], append_images=images[:-1])
    preview = Image.new("RGBA", (sum(sizes) + 10 * len(sizes), 256 * 2 + 10), (0, 0, 0, 0))
    for row, bg in enumerate([(243, 243, 243, 255), (32, 32, 32, 255)]):
        x = 0
        for s, im in zip(sizes, images):
            tile = Image.new("RGBA", (s, s), bg)
            tile.alpha_composite(im)
            preview.paste(tile, (x, row * 266))
            x += s + 10
    preview.save(HERE / "icon_preview.png")


if __name__ == "__main__":
    main()
