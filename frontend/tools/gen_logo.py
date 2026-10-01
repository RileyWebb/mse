"""Renders the MSE logo into every file that carries it.

    python frontend/tools/gen_logo.py

Writes, relative to the repository root:

    data/logo/mse.svg            the full drawing, for 48px and up
    data/logo/mse-small.svg      the simplified drawing, for 32px and down
    data/logo/mse-<N>.png        16..256, the window icon at runtime
    packaging/windows/mse.ico    the executable's icon and the installer's

The outputs are checked in, so this only needs running when the logo changes.
No dependencies beyond the standard library: the shapes are a handful of
rounded rectangles, and rasterising them here is simpler than asking every
contributor to install a vector renderer.

Every file comes from the shape lists below, SVG and PNG alike, so they cannot
disagree with each other. The same geometry is also drawn live by
frontend/src/frontend_logo.c, which has to be kept in step by hand.
"""

import os
import struct
import zlib

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

BODY = (0x4C, 0xC9, 0xF0)    # the Abyss theme's cyan
LABEL = (0x10, 0x10, 0x18)   # the label window, solid so it reads on any desktop
CORAL = (0xF2, 0x54, 0x5B)
AMBER = (0xF5, 0x9E, 0x0B)
GREEN = (0x4A, 0xDE, 0x80)

# Sizes up to this use the simplified drawing. Below 48px the full one's
# connector teeth and three stripes are narrower than a pixel and merge into a
# smear; the small one is laid out on the 16px grid so every edge lands on one.
SMALL_MAX = 32


# --- shapes ------------------------------------------------------------------
#
# Two kinds, both in the drawing's 64-unit square:
#   ("rect", colour, x0, y0, x1, y1, tl, tr, br, bl)   a radius per corner
#   ("body", colour, x0, y0, x1, y1, tl, cut_x)        the cartridge outline:
#       top-left rounded, top-right cut at 45 degrees from (cut_x, y0)
#
# Gaps -- the groove, the spaces between the connector's prongs -- are gaps in
# the shapes, never shapes painted in a background colour, so the logo sits
# correctly on whatever it is placed over.


def rect(colour, x0, y0, x1, y1, tl=0.0, tr=0.0, br=0.0, bl=0.0):
    return ("rect", colour, x0, y0, x1, y1, tl, tr, br, bl)


def rounded(colour, x0, y0, x1, y1, r):
    return rect(colour, x0, y0, x1, y1, r, r, r, r)


def body(colour, x0, y0, x1, y1, tl, cut_x):
    return ("body", colour, x0, y0, x1, y1, tl, cut_x)


def full_geometry():
    """The drawing for 48px and up, back to front."""
    shapes = [
        body(BODY, 8, 5, 56, 43, 4, 47),
        rect(BODY, 8, 45, 56, 49),
        rect(BODY, 14, 48, 50, 52),
        rect(BODY, 14, 52, 18.5, 59, bl=3),
    ]
    for x in (21.5, 27.5, 33.5, 39.5):
        shapes.append(rect(BODY, x, 52, x + 3, 59))
    shapes += [
        rect(BODY, 45.5, 52, 50, 59, br=3),
        rounded(LABEL, 14, 12, 50, 38, 3),
        rounded(CORAL, 19, 18, 45, 21.5, 1.75),
        rounded(AMBER, 19, 24, 45, 27.5, 1.75),
        rounded(GREEN, 19, 30, 35, 33.5, 1.75),
    ]
    return shapes


def small_geometry():
    """The simplified drawing: every edge on a multiple of 4 units, which is
    one pixel at 16px. No groove, three wide prongs, two stripes."""
    return [
        body(BODY, 8, 4, 56, 48, 4, 48),
        rect(BODY, 16, 48, 48, 52),
        rect(BODY, 16, 52, 24, 60, bl=2),
        rect(BODY, 28, 52, 36, 60),
        rect(BODY, 40, 52, 48, 60, br=2),
        rounded(LABEL, 16, 12, 48, 32, 2),
        rect(CORAL, 20, 16, 44, 20),
        rect(AMBER, 20, 24, 36, 28),
    ]


# --- as point tests, for the rasteriser ----------------------------------------


def _in_rect(x, y, x0, y0, x1, y1, tl, tr, br, bl):
    if x < x0 or x >= x1 or y < y0 or y >= y1:
        return False
    for r, cx, cy, left, top in (
        (tl, x0 + tl, y0 + tl, True, True),
        (tr, x1 - tr, y0 + tr, False, True),
        (br, x1 - br, y1 - br, False, False),
        (bl, x0 + bl, y1 - bl, True, False),
    ):
        if r <= 0:
            continue
        if (x < cx if left else x > cx) and (y < cy if top else y > cy):
            return (x - cx) ** 2 + (y - cy) ** 2 <= r * r
    return True


def contains(shape, x, y):
    kind = shape[0]
    if kind == "rect":
        return _in_rect(x, y, *shape[2:])
    _, _, x0, y0, x1, y1, tl, cut_x = shape
    return _in_rect(x, y, x0, y0, x1, y1, tl, 0, 0, 0) and not (x - cut_x > y - y0)


# --- as SVG --------------------------------------------------------------------


def _n(v):
    """Numbers as short as they will go: 12 rather than 12.0."""
    return ("%g" % v)


def path_d(shape):
    kind = shape[0]
    if kind == "body":
        _, _, x0, y0, x1, y1, tl, cut_x = shape
        return "M{x0} {ya}A{r} {r} 0 0 1 {xa} {y0}H{cx}L{x1} {yc}V{y1}H{x0}Z".format(
            x0=_n(x0), ya=_n(y0 + tl), r=_n(tl), xa=_n(x0 + tl), y0=_n(y0), cx=_n(cut_x),
            x1=_n(x1), yc=_n(y0 + (x1 - cut_x)), y1=_n(y1))

    _, _, x0, y0, x1, y1, tl, tr, br, bl = shape

    def arc(r, x, y):
        return "A{0} {0} 0 0 1 {1} {2}".format(_n(r), _n(x), _n(y)) if r > 0 else ""

    return ("M{} {}H{}{}V{}{}H{}{}V{}{}Z".format(
        _n(x0 + tl), _n(y0),
        _n(x1 - tr), arc(tr, x1, y0 + tr),
        _n(y1 - br), arc(br, x1 - br, y1),
        _n(x0 + bl), arc(bl, x0, y1 - bl),
        _n(y0 + tl), arc(tl, x0 + tl, y0)))


def svg(shapes, size):
    def hexc(c):
        return "#%02X%02X%02X" % c

    parts = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" width="{0}" height="{0}">'
             '<title>MSE</title>'.format(size)]
    # Consecutive shapes of one colour share a path: fewer elements, and no
    # hairline seams where two anti-aliased edges meet.
    run_colour, run = None, []
    for shape in shapes + [None]:
        colour = shape[1] if shape is not None else None
        if colour != run_colour and run:
            parts.append('<path fill="%s" d="%s"/>' % (hexc(run_colour), "".join(run)))
            run = []
        if shape is not None:
            run_colour = colour
            run.append(path_d(shape))
    parts.append("</svg>\n")
    return "".join(parts)


# --- rasterising ---------------------------------------------------------------


def render(size, samples=4):
    """RGBA bytes, supersampled. Colour is the average of the covered samples;
    alpha is how many of them were covered."""
    shapes = small_geometry() if size <= SMALL_MAX else full_geometry()
    scale = 64.0 / size
    step = scale / samples
    offsets = [(i + 0.5) * step for i in range(samples)]
    total = samples * samples

    out = bytearray()
    for py in range(size):
        for px in range(size):
            r = g = b = hits = 0
            for oy in offsets:
                y = py * scale + oy
                for ox in offsets:
                    x = px * scale + ox
                    colour = None
                    for shape in shapes:
                        if contains(shape, x, y):
                            colour = shape[1]
                    if colour is not None:
                        r += colour[0]
                        g += colour[1]
                        b += colour[2]
                        hits += 1
            if hits:
                out += bytes((r // hits, g // hits, b // hits, (255 * hits) // total))
            else:
                out += b"\0\0\0\0"
    return bytes(out)


def png(size, rgba):
    def chunk(kind, data):
        body_ = kind + data
        return struct.pack(">I", len(data)) + body_ + struct.pack(">I", zlib.crc32(body_) & 0xFFFFFFFF)

    stride = size * 4
    raw = b"".join(b"\0" + rgba[y * stride:(y + 1) * stride] for y in range(size))
    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def ico(images):
    """PNG-compressed entries, which every Windows since Vista reads at any size."""
    out = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for size, data in images:
        dim = 0 if size >= 256 else size
        out += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    return out + b"".join(data for _, data in images)


def main():
    logo_dir = os.path.join(ROOT, "data", "logo")
    os.makedirs(logo_dir, exist_ok=True)

    for name, shapes, size in (("mse.svg", full_geometry(), 256),
                               ("mse-small.svg", small_geometry(), 32)):
        with open(os.path.join(logo_dir, name), "w", newline="\n") as f:
            f.write(svg(shapes, size))
        print("%-14s %6d bytes" % (name, os.path.getsize(os.path.join(logo_dir, name))))

    icons = []
    for size in (16, 24, 32, 48, 64, 128, 256):
        data = png(size, render(size))
        with open(os.path.join(logo_dir, "mse-%d.png" % size), "wb") as f:
            f.write(data)
        icons.append((size, data))
        print("%-14s %6d bytes" % ("mse-%d.png" % size, len(data)))

    ico_path = os.path.join(ROOT, "packaging", "windows", "mse.ico")
    with open(ico_path, "wb") as f:
        f.write(ico(icons))
    print("%-14s %6d bytes" % ("mse.ico", os.path.getsize(ico_path)))


if __name__ == "__main__":
    main()
