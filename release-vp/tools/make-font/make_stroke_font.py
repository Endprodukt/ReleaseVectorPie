"""make_stroke_font.py â€” turn a thin (monoline) TrueType font into a stroke
font for the vector monitor: every glyph is rendered, thinned to its centre
line, traced into polylines and simplified. Writes a C include for
src/font_xspace.inc (release.h's shape format per glyph).

    python make_stroke_font.py <font.ttf> <out.inc>

Used for the original game's font, Aunchanted Xspace by Graham Meade
(freeware), taken from the Android APK's Unity data (see extract_fonts.py).
Needs numpy and Pillow.
"""
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

SIZE = 360          # render size in pixels (the font's em)
PAD = 40
EPSILON = 3.0       # Douglas-Peucker tolerance, pixels (of a 216-pixel capital)
FIRST, LAST = 32, 126


def thin(img):
    """Zhang-Suen thinning of a 0/1 image."""
    img = img.astype(np.uint8)
    changed = True
    while changed:
        changed = False
        for step in (0, 1):
            p = np.pad(img, 1)
            p2, p3, p4 = p[:-2, 1:-1], p[:-2, 2:], p[1:-1, 2:]
            p5, p6, p7 = p[2:, 2:], p[2:, 1:-1], p[2:, :-2]
            p8, p9 = p[1:-1, :-2], p[:-2, :-2]
            ring = [p2, p3, p4, p5, p6, p7, p8, p9, p2]
            b = p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9
            a = sum(((ring[i] == 0) & (ring[i + 1] == 1)).astype(np.uint8) for i in range(8))
            if step == 0:
                c1, c2 = p2 * p4 * p6, p4 * p6 * p8
            else:
                c1, c2 = p2 * p4 * p8, p2 * p6 * p8
            m = (img == 1) & (b >= 2) & (b <= 6) & (a == 1) & (c1 == 0) & (c2 == 0)
            if m.any():
                img[m] = 0
                changed = True
    return img


def neighbours(pts, p):
    """8-neighbours, but a diagonal only where no 4-neighbour already joins them."""
    y, x = p
    out = []
    for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
        if (y + dy, x + dx) in pts:
            out.append((y + dy, x + dx))
    for dy, dx in ((-1, -1), (-1, 1), (1, -1), (1, 1)):
        q = (y + dy, x + dx)
        if q in pts and (y + dy, x) not in pts and (y, x + dx) not in pts:
            out.append(q)
    return out


def trace(skel):
    pts = set(zip(*np.nonzero(skel)))
    nb = {p: neighbours(pts, p) for p in pts}
    nodes = {p for p in pts if len(nb[p]) != 2}
    used = set()
    paths = []

    def walk(start, nxt):
        path = [start, nxt]
        used.add(frozenset((start, nxt)))
        prev, cur = start, nxt
        while cur not in nodes:
            step = [q for q in nb[cur] if q != prev and frozenset((cur, q)) not in used]
            if not step:
                break
            prev, cur = cur, step[0]
            used.add(frozenset((prev, cur)))
            path.append(cur)
            if cur == start:
                break
        return path

    for n in nodes:
        for q in nb[n]:
            if frozenset((n, q)) not in used:
                paths.append(walk(n, q))
    for p in pts:                       # loops with no node (an O)
        for q in nb[p]:
            if frozenset((p, q)) not in used:
                nodes.add(p)
                paths.append(walk(p, q))
    return paths, nb


def prune(paths, nb, stroke):
    """Drop short spurs (an end that goes nowhere, shorter than the stroke)."""
    keep = []
    for path in paths:
        a, b = path[0], path[-1]
        spur = (len(nb[a]) == 1) != (len(nb[b]) == 1)
        if spur and len(path) < stroke * 1.2:
            continue
        if len(path) < 2:
            continue
        keep.append(path)
    return keep


def simplify(pts, eps):
    if len(pts) < 3:
        return pts
    a, b = np.array(pts[0], float), np.array(pts[-1], float)
    d = b - a
    n = np.hypot(*d)
    best, idx = -1.0, 0
    for i in range(1, len(pts) - 1):
        p = np.array(pts[i], float)
        dist = abs(d[0] * (a[1] - p[1]) - d[1] * (a[0] - p[0])) / n if n else np.hypot(*(p - a))
        if dist > best:
            best, idx = dist, i
    if best <= eps:
        return [pts[0], pts[-1]]
    return simplify(pts[:idx + 1], eps)[:-1] + simplify(pts[idx:], eps)


def join(polys):
    """Chain polylines that meet end to end, so the beam draws them in one go."""
    polys = [list(p) for p in polys]
    merged = True
    while merged:
        merged = False
        for i in range(len(polys)):
            for j in range(len(polys)):
                if i == j or not polys[i] or not polys[j]:
                    continue
                a, b = polys[i], polys[j]
                if a[-1] == b[0]:
                    a.extend(b[1:]); polys[j] = []; merged = True
                elif a[-1] == b[-1]:
                    a.extend(reversed(b[:-1])); polys[j] = []; merged = True
        polys = [p for p in polys if p]
    return polys


def glyph(font, ch, baseline, stroke):
    w = int(font.getlength(ch)) + 2 * PAD
    h = SIZE + 2 * PAD
    img = Image.new("L", (max(w, 8), h), 0)
    ImageDraw.Draw(img).text((PAD, baseline), ch, font=font, fill=255, anchor="ls")
    ink = (np.array(img) > 110).astype(np.uint8)
    if not ink.any():
        return []
    paths, nb = trace(thin(ink))
    paths = prune(paths, nb, stroke)
    polys = []
    for path in paths:
        s = simplify(path, EPSILON)
        polys.append([(x - PAD, baseline - y) for y, x in s])   # x right, y up from the baseline
    return join(polys)


def main():
    src, out = sys.argv[1], sys.argv[2]
    font = ImageFont.truetype(src, SIZE)
    baseline = PAD + int(SIZE * 0.85)
    left, top, right, bottom = font.getbbox("H", anchor="ls")
    cap = -top
    stroke = 8
    # the stroke width: the H's left stem, measured on its middle row
    img = Image.new("L", (SIZE * 2, SIZE + 2 * PAD), 0)
    ImageDraw.Draw(img).text((PAD, baseline), "H", font=font, fill=255, anchor="ls")
    row = (np.array(img)[baseline - cap // 2] > 110).astype(int)
    xs = np.nonzero(row)[0]
    if len(xs):
        run = 1
        while run < len(xs) and xs[run] == xs[run - 1] + 1:
            run += 1
        stroke = run

    data, table = [], []
    for code in range(FIRST, LAST + 1):
        ch = chr(code)
        offset = len(data)
        for poly in glyph(font, ch, baseline, stroke):
            data.append(len(poly))
            for x, y in poly:
                data += [int(x), int(y)]
        data.append(0)
        table.append((int(round(font.getlength(ch))), offset))

    with open(out, "w") as f:
        f.write("/* font_xspace.inc â€” Aunchanted Xspace (Graham Meade, freeware), the\n"
                " * original game's font, as strokes: made by tools/make-font/make_stroke_font.py.\n"
                " * Each glyph: polylines (count, then x,y pairs; y up from the baseline), 0 ends. */\n")
        f.write(f"#define XS_CAP    {cap}\n#define XS_STROKE {stroke}\n#define XS_FIRST  {FIRST}\n#define XS_LAST   {LAST}\n\n")
        f.write("static const short xs_data[] = {\n")
        for i in range(0, len(data), 16):
            f.write("    " + ", ".join(str(v) for v in data[i:i + 16]) + ",\n")
        f.write("};\n\nstatic const struct { short advance; int offset; } xs_glyph[] = {\n")
        for code, (adv, off) in zip(range(FIRST, LAST + 1), table):
            label = chr(code) if chr(code) not in "\\'" else "\\" + chr(code)
            f.write(f"    {{ {adv:3d}, {off:5d} }},   /* '{label}' */\n")
        f.write("};\n")
    print(f"cap {cap}, stroke {stroke}, {len(data)} shorts")


if __name__ == "__main__":
    main()
