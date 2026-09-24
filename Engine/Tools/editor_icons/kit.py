"""Drawing kit for the UniVex editor icon set.

Every icon is a 64x64 SVG drawn in one shared style: solid 3D objects lit from the upper left,
three-tone faces with soft gradients, a thin dark silhouette line so the shape reads on dark and
light panels alike, and a soft contact shadow. Each icon file is standalone (its own <defs>).
"""

import colorsys
import math

COS30 = math.cos(math.radians(30))
SIN30 = 0.5
OUTLINE = "#0b0f15"


# ---- colour ---------------------------------------------------------------------------------

def _rgb(hexcolor):
    h = hexcolor.lstrip("#")
    return tuple(int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4))


def _hex(rgb):
    return "#" + "".join(f"{max(0, min(255, round(c * 255))):02x}" for c in rgb)


def shade(hexcolor, lightness=0.0, saturation=0.0):
    """Moves a colour's HLS lightness by `lightness` (-1..1) and saturation by `saturation`."""
    r, g, b = _rgb(hexcolor)
    h, l, s = colorsys.rgb_to_hls(r, g, b)
    l = max(0.0, min(1.0, l + lightness))
    s = max(0.0, min(1.0, s + saturation))
    return _hex(colorsys.hls_to_rgb(h, l, s))


def tones(base):
    """(top, left, right, edge) tones of a base colour for a solid lit from the upper left."""
    return shade(base, 0.16, 0.02), base, shade(base, -0.17, -0.02), shade(base, 0.32)


# ---- document -------------------------------------------------------------------------------

class Icon:
    """Collects defs and body elements for one icon, with ids unique inside the file."""

    def __init__(self, name):
        self.name = name
        self.defs = []
        self.body = []
        self._next = 0

    def uid(self, stem):
        self._next += 1
        return f"{stem}{self._next}"

    def add(self, element):
        self.body.append(element)

    def linear(self, stops, x1=0, y1=0, x2=0, y2=1, units="objectBoundingBox"):
        gid = self.uid("l")
        stop_xml = "".join(
            f'<stop offset="{o}" stop-color="{c}"' + (f' stop-opacity="{a}"' if a != 1 else "") + "/>"
            for o, c, a in _stops(stops))
        self.defs.append(f'<linearGradient id="{gid}" x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" '
                         f'gradientUnits="{units}">{stop_xml}</linearGradient>')
        return f"url(#{gid})"

    def radial(self, stops, cx=0.5, cy=0.5, r=0.5, fx=None, fy=None, units="objectBoundingBox"):
        gid = self.uid("r")
        stop_xml = "".join(
            f'<stop offset="{o}" stop-color="{c}"' + (f' stop-opacity="{a}"' if a != 1 else "") + "/>"
            for o, c, a in _stops(stops))
        focus = f' fx="{fx}" fy="{fy}"' if fx is not None else ""
        self.defs.append(f'<radialGradient id="{gid}" cx="{cx}" cy="{cy}" r="{r}"{focus} '
                         f'gradientUnits="{units}">{stop_xml}</radialGradient>')
        return f"url(#{gid})"

    def svg(self):
        return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" width="64" height="64">'
                f'<title>{self.name}</title><defs>{"".join(self.defs)}</defs>{"".join(self.body)}</svg>')


def _stops(stops):
    for stop in stops:
        if len(stop) == 2:
            yield stop[0], stop[1], 1
        else:
            yield stop


def pts(points):
    return " ".join(f"{x:.2f},{y:.2f}" for x, y in points)


# ---- projection -----------------------------------------------------------------------------

def iso(cx, cy, x, y, z, s=1.0):
    """Isometric projection of (x, y, z), y up, around the 2D anchor (cx, cy)."""
    return cx + (x - z) * COS30 * s, cy + (x + z) * SIN30 * s - y * s


# ---- primitives -----------------------------------------------------------------------------

def shadow(icon, cx, cy, rx, ry, strength=0.42):
    fill = icon.radial([(0, "#000", strength), (0.65, "#000", strength * 0.45), (1, "#000", 0)])
    icon.add(f'<ellipse cx="{cx}" cy="{cy}" rx="{rx}" ry="{ry}" fill="{fill}"/>')


def box(icon, cx, cy, w, h, d, base, glass=False, edge=True, outline=True):
    """Isometric box standing on (cx, cy): width along x, height up, depth along z."""
    top_c, left_c, right_c, edge_c = tones(base)
    a, b = w / 2, d / 2
    P = lambda x, y, z: iso(cx, cy, x, y, z)
    top = [P(-a, h, -b), P(a, h, -b), P(a, h, b), P(-a, h, b)]
    left = [P(-a, 0, b), P(a, 0, b), P(a, h, b), P(-a, h, b)]
    right = [P(a, 0, b), P(a, 0, -b), P(a, h, -b), P(a, h, b)]
    alpha = 0.55 if glass else 1
    icon.add(f'<polygon points="{pts(left)}" fill="{icon.linear([(0, shade(left_c, 0.06), alpha), (1, shade(left_c, -0.06), alpha)], 0, 0, 1, 1)}"/>')
    icon.add(f'<polygon points="{pts(right)}" fill="{icon.linear([(0, right_c, alpha), (1, shade(right_c, -0.08), alpha)], 0, 0, 1, 1)}"/>')
    icon.add(f'<polygon points="{pts(top)}" fill="{icon.linear([(0, shade(top_c, 0.08), alpha), (1, top_c, alpha)], 0, 0, 1, 1)}"/>')
    if edge:
        icon.add(f'<polyline points="{pts([P(-a, h, b), P(a, h, b), P(a, h, -b)])}" fill="none" '
                 f'stroke="{edge_c}" stroke-opacity="0.9" stroke-width="1.1" stroke-linejoin="round"/>')
        icon.add(f'<line x1="{P(a, h, b)[0]:.2f}" y1="{P(a, h, b)[1]:.2f}" x2="{P(a, 0, b)[0]:.2f}" '
                 f'y2="{P(a, 0, b)[1]:.2f}" stroke="{edge_c}" stroke-opacity="0.45" stroke-width="1"/>')
    if outline:
        hull = [P(-a, 0, b), P(a, 0, b), P(a, 0, -b), P(a, h, -b), P(-a, h, -b), P(-a, h, b)]
        icon.add(f'<polygon points="{pts(hull)}" fill="none" stroke="{OUTLINE}" stroke-opacity="0.7" '
                 f'stroke-width="1.4" stroke-linejoin="round"/>')
    return top, left, right


def wire_box(icon, cx, cy, w, h, d, color, dash="3 2.2", width=1.7, back=True):
    """An isometric box drawn as edges only: the collision-shape look."""
    a, b = w / 2, d / 2
    P = lambda x, y, z: iso(cx, cy, x, y, z)
    front = [
        (P(-a, 0, b), P(a, 0, b)), (P(a, 0, b), P(a, 0, -b)), (P(-a, h, b), P(a, h, b)),
        (P(a, h, b), P(a, h, -b)), (P(-a, h, -b), P(a, h, -b)), (P(-a, h, -b), P(-a, h, b)),
        (P(-a, 0, b), P(-a, h, b)), (P(a, 0, b), P(a, h, b)), (P(a, 0, -b), P(a, h, -b)),
    ]
    hidden = [(P(-a, 0, -b), P(a, 0, -b)), (P(-a, 0, -b), P(-a, 0, b)), (P(-a, 0, -b), P(-a, h, -b))]
    if back:
        for (x1, y1), (x2, y2) in hidden:
            icon.add(f'<line x1="{x1:.2f}" y1="{y1:.2f}" x2="{x2:.2f}" y2="{y2:.2f}" stroke="{color}" '
                     f'stroke-opacity="0.45" stroke-width="{width * 0.8}" stroke-dasharray="{dash}" '
                     f'stroke-linecap="round"/>')
    for (x1, y1), (x2, y2) in front:
        icon.add(f'<line x1="{x1:.2f}" y1="{y1:.2f}" x2="{x2:.2f}" y2="{y2:.2f}" stroke="{OUTLINE}" '
                 f'stroke-opacity="0.55" stroke-width="{width + 1.4}" stroke-linecap="round"/>')
    for (x1, y1), (x2, y2) in front:
        icon.add(f'<line x1="{x1:.2f}" y1="{y1:.2f}" x2="{x2:.2f}" y2="{y2:.2f}" stroke="{color}" '
                 f'stroke-width="{width}" stroke-linecap="round"/>')


def sphere(icon, cx, cy, r, base, gloss=1.0, outline=True):
    top_c, _, dark_c, _ = tones(base)
    fill = icon.radial([(0, shade(base, 0.34)), (0.45, top_c), (0.8, base), (1, shade(dark_c, -0.08))],
                       cx=0.38, cy=0.32, r=0.72, fx=0.34, fy=0.28)
    icon.add(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{fill}"/>')
    rim = icon.linear([(0, "#fff", 0), (1, shade(base, 0.25), 0.55)], 0, 0, 1, 1)
    icon.add(f'<path d="M {cx - r * 0.15:.2f} {cy + r * 0.98:.2f} A {r} {r} 0 0 0 {cx + r * 0.98:.2f} '
             f'{cy + r * 0.15:.2f}" fill="none" stroke="{rim}" stroke-width="{max(1.0, r * 0.1):.2f}"/>')
    spec = icon.radial([(0, "#fff", 0.95 * gloss), (1, "#fff", 0)])
    icon.add(f'<ellipse cx="{cx - r * 0.34:.2f}" cy="{cy - r * 0.4:.2f}" rx="{r * 0.36:.2f}" '
             f'ry="{r * 0.24:.2f}" fill="{spec}" transform="rotate(-30 {cx - r * 0.34:.2f} {cy - r * 0.4:.2f})"/>')
    if outline:
        icon.add(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="none" stroke="{OUTLINE}" stroke-opacity="0.7" '
                 f'stroke-width="1.4"/>')


def cylinder(icon, cx, cy, rx, h, base, ry=None, outline=True, glass=False):
    """Upright cylinder whose base ellipse is centred on (cx, cy)."""
    ry = rx * 0.5 if ry is None else ry
    top_c, _, dark_c, edge_c = tones(base)
    alpha = 0.6 if glass else 1
    body = icon.linear([(0, shade(base, -0.05), alpha), (0.28, shade(base, 0.12), alpha), (0.6, base, alpha),
                        (1, shade(dark_c, -0.06), alpha)], 0, 0, 1, 0)
    icon.add(f'<path d="M {cx - rx} {cy - h} L {cx - rx} {cy} A {rx} {ry} 0 0 0 {cx + rx} {cy} '
             f'L {cx + rx} {cy - h} Z" fill="{body}"/>')
    top = icon.linear([(0, shade(top_c, 0.08), alpha), (1, top_c, alpha)], 0, 0, 1, 1)
    icon.add(f'<ellipse cx="{cx}" cy="{cy - h}" rx="{rx}" ry="{ry}" fill="{top}"/>')
    icon.add(f'<path d="M {cx - rx} {cy - h} A {rx} {ry} 0 0 0 {cx + rx} {cy - h}" fill="none" '
             f'stroke="{edge_c}" stroke-opacity="0.8" stroke-width="1"/>')
    if outline:
        icon.add(f'<path d="M {cx - rx} {cy - h} A {rx} {ry} 0 0 1 {cx + rx} {cy - h} L {cx + rx} {cy} '
                 f'A {rx} {ry} 0 0 1 {cx - rx} {cy} Z" fill="none" stroke="{OUTLINE}" stroke-opacity="0.7" '
                 f'stroke-width="1.4" stroke-linejoin="round"/>')


def slab(icon, cx, cy, w, d, t, base, outline=True):
    """A thin isometric plate: a ground, a pad, a tile."""
    return box(icon, cx, cy, w, t, d, base, outline=outline)


def stroke_line(icon, points, color, width=2.4, outline=True, cap="round", opacity=1.0, dash=None):
    """A line with a dark outline under it so it reads on any background."""
    d = "M " + " L ".join(f"{x:.2f} {y:.2f}" for x, y in points)
    dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
    if outline:
        icon.add(f'<path d="{d}" fill="none" stroke="{OUTLINE}" stroke-opacity="0.6" stroke-width="{width + 1.8}" '
                 f'stroke-linecap="{cap}" stroke-linejoin="round"{dash_attr}/>')
    icon.add(f'<path d="{d}" fill="none" stroke="{color}" stroke-opacity="{opacity}" stroke-width="{width}" '
             f'stroke-linecap="{cap}" stroke-linejoin="round"{dash_attr}/>')


def arrow_head(icon, tip, direction, size, color):
    dx, dy = direction
    n = math.hypot(dx, dy) or 1
    dx, dy = dx / n, dy / n
    px, py = -dy, dx
    base = (tip[0] - dx * size, tip[1] - dy * size)
    tri = [tip, (base[0] + px * size * 0.55, base[1] + py * size * 0.55),
           (base[0] - px * size * 0.55, base[1] - py * size * 0.55)]
    fill = icon.linear([(0, shade(color, 0.2)), (1, shade(color, -0.12))], 0, 0, 1, 1)
    icon.add(f'<polygon points="{pts(tri)}" fill="{fill}" stroke="{OUTLINE}" stroke-opacity="0.65" '
             f'stroke-width="1.2" stroke-linejoin="round"/>')


def arrow(icon, start, end, color, width=2.6, head=6.5):
    dx, dy = end[0] - start[0], end[1] - start[1]
    n = math.hypot(dx, dy) or 1
    shaft_end = (end[0] - dx / n * head * 0.8, end[1] - dy / n * head * 0.8)
    stroke_line(icon, [start, shaft_end], color, width)
    arrow_head(icon, end, (dx, dy), head, color)


def glow(icon, cx, cy, r, color, strength=0.55):
    fill = icon.radial([(0, color, strength), (0.5, color, strength * 0.35), (1, color, 0)])
    icon.add(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{fill}"/>')


def page(icon, x, y, w, h, base, fold=8):
    """A document sheet with thickness and a folded corner."""
    top_c, _, dark_c, edge_c = tones(base)
    depth = 2.2
    icon.add(f'<path d="M {x + depth} {y + depth} h {w - fold} l {fold} {fold} v {h - fold} h {-w} Z" '
             f'fill="{shade(dark_c, -0.1)}"/>')
    body = icon.linear([(0, shade(top_c, 0.1)), (1, base)], 0, 0, 1, 1)
    icon.add(f'<path d="M {x} {y} h {w - fold} l {fold} {fold} v {h - fold} h {-w} Z" fill="{body}" '
             f'stroke="{OUTLINE}" stroke-opacity="0.7" stroke-width="1.4" stroke-linejoin="round"/>')
    icon.add(f'<path d="M {x + w - fold} {y} v {fold} h {fold}" fill="{shade(top_c, 0.18)}" '
             f'stroke="{OUTLINE}" stroke-opacity="0.6" stroke-width="1.2" stroke-linejoin="round"/>')
    icon.add(f'<path d="M {x + 1.2} {y + h - 1.2} V {y + 1.2} H {x + w - fold - 0.5}" fill="none" '
             f'stroke="{edge_c}" stroke-opacity="0.8" stroke-width="1"/>')


def glass_box(icon, cx, cy, w, h, d, color, fill_alpha=0.22, width=1.9):
    """A see-through volume: tinted faces with bright edges (areas, triggers, regions)."""
    a, b = w / 2, d / 2
    P = lambda x, y, z: iso(cx, cy, x, y, z)
    faces = [
        [P(-a, 0, -b), P(a, 0, -b), P(a, 0, b), P(-a, 0, b)],
        [P(-a, 0, b), P(a, 0, b), P(a, h, b), P(-a, h, b)],
        [P(a, 0, b), P(a, 0, -b), P(a, h, -b), P(a, h, b)],
        [P(-a, h, -b), P(a, h, -b), P(a, h, b), P(-a, h, b)],
    ]
    tints = [shade(color, -0.2), color, shade(color, -0.12), shade(color, 0.14)]
    for face, tint in zip(faces, tints):
        icon.add(f'<polygon points="{pts(face)}" fill="{tint}" fill-opacity="{fill_alpha}"/>')
    wire_box(icon, cx, cy, w, h, d, shade(color, 0.12), width=width)


def faceted_sphere(icon, cx, cy, r, base, detail=1, rot=(18, 28), edges=True, outline=True, wire=None):
    """A low-poly sphere shaded per facet: an icosahedron, subdivided `detail` times (0-2), or an
    octahedron when detail is -1."""
    verts, faces = _polyhedron(detail)
    ax, ay = (math.radians(a) for a in rot)
    def rotate(v):
        x, y, z = v
        y, z = y * math.cos(ax) - z * math.sin(ax), y * math.sin(ax) + z * math.cos(ax)
        x, z = x * math.cos(ay) + z * math.sin(ay), -x * math.sin(ay) + z * math.cos(ay)
        return x, y, z
    verts = [rotate(v) for v in verts]
    light = _norm((-0.55, 0.72, 0.55))
    lo, hi = shade(base, -0.2), shade(base, 0.3)
    drawn = []
    for f in faces:
        a, b, c = (verts[i] for i in f)
        n = _norm(_cross(_sub(b, a), _sub(c, a)))
        if n[2] < 0:
            n = tuple(-k for k in n)
            a, c = c, a
        centre_z = (a[2] + b[2] + c[2]) / 3
        facing = _dot(_norm(_add3(a, b, c)), (0, 0, 1))
        if facing <= 0:
            continue
        t = max(0.0, _dot(n, light)) ** 0.85
        drawn.append((centre_z, [a, b, c], _mix(lo, hi, t)))
    drawn.sort(key=lambda d: d[0])
    for _, tri, colour in drawn:
        points = [(cx + v[0] * r, cy - v[1] * r) for v in tri]
        if wire:
            stroke = f' stroke="{wire}" stroke-opacity="0.75" stroke-width="1.1" stroke-linejoin="round"'
        elif edges:
            stroke = f' stroke="{shade(colour, -0.12)}" stroke-width="0.6" stroke-linejoin="round"'
        else:
            stroke = f' stroke="{colour}" stroke-width="0.5" stroke-linejoin="round"'
        icon.add(f'<polygon points="{pts(points)}" fill="{colour}"{stroke}/>')
    if outline:
        icon.add(f'<circle cx="{cx}" cy="{cy}" r="{r * 0.995:.2f}" fill="none" stroke="{OUTLINE}" '
                 f'stroke-opacity="0.0"/>')


def _polyhedron(detail):
    if detail < 0:
        verts = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]
        faces = [(0, 2, 4), (0, 4, 3), (0, 3, 5), (0, 5, 2), (1, 4, 2), (1, 3, 4), (1, 5, 3), (1, 2, 5)]
        return verts, faces
    p = (1 + 5 ** 0.5) / 2
    raw = [(-1, p, 0), (1, p, 0), (-1, -p, 0), (1, -p, 0), (0, -1, p), (0, 1, p), (0, -1, -p), (0, 1, -p),
           (p, 0, -1), (p, 0, 1), (-p, 0, -1), (-p, 0, 1)]
    verts = [_norm(v) for v in raw]
    faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2),
             (10, 7, 6), (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5),
             (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    for _ in range(detail):
        cache = {}
        def mid(i, j):
            key = (min(i, j), max(i, j))
            if key not in cache:
                verts.append(_norm(tuple((verts[i][k] + verts[j][k]) / 2 for k in range(3))))
                cache[key] = len(verts) - 1
            return cache[key]
        new = []
        for a, b, c in faces:
            ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
            new += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        faces = new
    return verts, faces


def _sub(a, b):
    return tuple(a[i] - b[i] for i in range(3))


def _add3(a, b, c):
    return tuple(a[i] + b[i] + c[i] for i in range(3))


def _dot(a, b):
    return sum(a[i] * b[i] for i in range(3))


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _norm(v):
    n = math.sqrt(_dot(v, v)) or 1
    return tuple(k / n for k in v)


def _mix(c1, c2, t):
    a, b = _rgb(c1), _rgb(c2)
    return _hex(tuple(a[i] + (b[i] - a[i]) * t for i in range(3)))


def tile(icon, base):
    """The rounded, bevelled square behind a palette-category glyph."""
    top_c, _, dark_c, edge_c = tones(base)
    icon.add(f'<rect x="6" y="8" width="54" height="54" rx="12" fill="{shade(dark_c, -0.12)}" fill-opacity="0.9"/>')
    fill = icon.linear([(0, shade(top_c, 0.1)), (0.5, base), (1, shade(dark_c, -0.04))], 0, 0, 0.35, 1)
    icon.add(f'<rect x="4" y="4" width="54" height="54" rx="12" fill="{fill}" stroke="{OUTLINE}" '
             f'stroke-opacity="0.65" stroke-width="1.4"/>')
    gloss = icon.linear([(0, "#fff", 0.42), (1, "#fff", 0)], 0, 0, 0, 1)
    icon.add(f'<path d="M 9 8.5 h 44 a 5 5 0 0 1 5 5 v 12 C 44 20 20 20 4.5 26 v -12.5 a 5 5 0 0 1 4.5 -5 Z" '
             f'fill="{gloss}"/>')


def glyph(icon, d, width=4.2, color="#ffffff", fill="none"):
    """A white glyph on a palette tile, with a soft dark under-stroke for depth."""
    icon.add(f'<path d="{d}" transform="translate(1 1.4)" fill="{"none" if fill == "none" else OUTLINE}" '
             f'fill-opacity="0.35" stroke="{OUTLINE}" stroke-opacity="0.35" stroke-width="{width}" '
             f'stroke-linecap="round" stroke-linejoin="round"/>')
    icon.add(f'<path d="{d}" fill="{fill}" stroke="{color}" stroke-width="{width}" stroke-linecap="round" '
             f'stroke-linejoin="round"/>')


# ---- people ---------------------------------------------------------------------------------

# Joint positions for a figure facing right, on the 64 px canvas. Each limb is a chain of points;
# "back" limbs are drawn first and darker, so the body reads in depth.
POSES = {
    "run": {
        "head": (37.5, 10.5), "neck": (35, 18.5), "hip": (29.5, 34.5),
        "back_arm": [(32.5, 21.5), (23, 26.5), (18.5, 34)],
        "back_leg": [(28.5, 35.5), (21.5, 45.5), (11.5, 46.5)],
        "front_leg": [(30, 35.5), (41, 41), (38.5, 52.5)],
        "front_arm": [(35, 21.5), (44, 28), (49.5, 21.5)],
        "feet": [((11.5, 46.5), (9.5, 42.5)), ((38.5, 52.5), (44.5, 53.5))],
    },
    "walk": {
        "head": (32.5, 9.5), "neck": (32.5, 17.5), "hip": (31.5, 34),
        "back_arm": [(30.5, 20.5), (27.5, 28.5), (25.5, 35)],
        "back_leg": [(30.5, 35), (27.5, 45.5), (22.5, 55)],
        "front_leg": [(32.5, 35), (36.5, 45.5), (39.5, 55)],
        "front_arm": [(34.5, 20.5), (38.5, 28.5), (40.5, 35)],
        "feet": [((22.5, 55), (26.5, 56.5)), ((39.5, 55), (44, 56))],
    },
    "stand": {
        "head": (32, 9.5), "neck": (32, 17.5), "hip": (32, 34.5),
        "back_arm": [(27.5, 20.5), (24.5, 28.5), (23.5, 36)],
        "back_leg": [(29.8, 35.5), (29, 45.5), (28.8, 55.5)],
        "front_leg": [(34.2, 35.5), (35, 45.5), (35.2, 55.5)],
        "front_arm": [(36.5, 20.5), (39.5, 28.5), (40.5, 36)],
        "feet": [((28.8, 55.5), (26, 56.5)), ((35.2, 55.5), (38, 56.5))],
    },
}


def _limb(icon, chain, width, colour, highlight=True):
    d = "M " + " L ".join(f"{x:.2f} {y:.2f}" for x, y in chain)
    icon.add(f'<path d="{d}" fill="none" stroke="{OUTLINE}" stroke-opacity="0.78" stroke-width="{width + 2.8:.2f}" '
             f'stroke-linecap="round" stroke-linejoin="round"/>')
    icon.add(f'<path d="{d}" fill="none" stroke="{colour}" stroke-width="{width:.2f}" stroke-linecap="round" '
             f'stroke-linejoin="round"/>')
    if highlight:
        hi = "M " + " L ".join(f"{x - width * 0.18:.2f} {y - width * 0.2:.2f}" for x, y in chain)
        icon.add(f'<path d="{hi}" fill="none" stroke="{shade(colour, 0.3)}" stroke-opacity="0.75" '
                 f'stroke-width="{width * 0.3:.2f}" stroke-linecap="round" stroke-linejoin="round"/>')


def figure(icon, pose, base, dx=0.0, dy=0.0, scale=1.0, joints=None):
    """A person built like a mannequin: rounded limbs, a tapered torso, a lit head. `joints`, when
    given, marks the skeleton's joints in that colour."""
    p = POSES[pose]
    T = lambda pt: (dx + 32 + (pt[0] - 32) * scale, dy + 32 + (pt[1] - 32) * scale)
    chain = lambda key: [T(pt) for pt in p[key]]
    far, near = shade(base, -0.14), shade(base, 0.04)
    limb_w, leg_w = 6.4 * scale, 7.6 * scale
    _limb(icon, chain("back_arm"), limb_w, far, highlight=False)
    _limb(icon, chain("back_leg"), leg_w, far, highlight=False)
    for (heel, toe), which in zip(p["feet"], ("back", "front")):
        if which == "back":
            _limb(icon, [T(heel), T(toe)], 4.6 * scale, far, highlight=False)
    neck, hip = T(p["neck"]), T(p["hip"])
    _limb(icon, [neck, hip], 13.5 * scale, base)
    _limb(icon, chain("front_leg"), leg_w, near)
    heel, toe = p["feet"][1]
    _limb(icon, [T(heel), T(toe)], 4.6 * scale, near, highlight=False)
    _limb(icon, chain("front_arm"), limb_w, near)
    hx, hy = T(p["head"])
    sphere(icon, hx, hy, 7.2 * scale, shade(base, 0.06))
    if joints:
        for key in ("front_arm", "front_leg"):
            for x, y in chain(key)[:2]:
                icon.add(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{1.9 * scale:.2f}" fill="{joints}" '
                         f'stroke="#ffffff" stroke-width="0.8"/>')
        for x, y in (neck, hip):
            icon.add(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{2.1 * scale:.2f}" fill="{joints}" '
                     f'stroke="#ffffff" stroke-width="0.8"/>')
