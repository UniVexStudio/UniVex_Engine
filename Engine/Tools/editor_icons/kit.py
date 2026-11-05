"""Drawing kit for the UniVex Editor Monochrome Studio Icon Set.

Every icon is a 64x64 SVG drawn in one shared bespoke style:
- 100% True Neutral Gray & White Soft Matte Studio Shading (zero colorful or blue-tinted hex codes).
- Unified, clean 3D/2D sculpted silhouettes with soft top-left studio lighting.
- Available in two formats:
  1. Standalone Outliner / Inspector glyph on transparent background.
  2. Studio Card ("Box na Parihaba") with dark neutral studio frame, soft floor shadow, and optional 3D/2D/UI badge.
"""

import colorsys
import math

COS30 = math.cos(math.radians(30))
SIN30 = 0.5
OUTLINE = "#18181b"

# Bespoke UniVex True Neutral Gray & White Studio Palette (R ~= G ~= B, Zero Color Tint)
WHITE = "#ffffff"
BRIGHT = "#f4f4f5"
LIGHT = "#e4e4e7"
SILVER = "#d4d4d8"
MID = "#a1a1aa"
SLATE = "#71717a"
DARK = "#52525b"
DEEP = "#3f3f46"
CUTOUT = "#27272a"


def _rgb(hexcolor):
    h = hexcolor.lstrip("#")
    if len(h) == 3:
        h = "".join(c * 2 for c in h)
    return tuple(int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4))


def _hex(rgb):
    return "#" + "".join(f"{max(0, min(255, round(c * 255))):02x}" for c in rgb)


def to_monochrome(hexcolor):
    """Forces any colour into pure neutral studio gray-white (saturation <= 0.03)."""
    r, g, b = _rgb(hexcolor)
    _, l, _ = colorsys.rgb_to_hls(r, g, b)
    return _hex(colorsys.hls_to_rgb(240.0 / 360.0, l, 0.02))


def shade(hexcolor, lightness=0.0, saturation=0.0):
    """Moves a monochrome colour's lightness by `lightness` (-1..1) while keeping it strictly neutral gray."""
    r, g, b = _rgb(to_monochrome(hexcolor))
    h, l, _ = colorsys.rgb_to_hls(r, g, b)
    l = max(0.06, min(0.99, l + lightness))
    return _hex(colorsys.hls_to_rgb(h, l, 0.02))


def tones(base=SILVER):
    """(top, left, right, edge) monochrome tones for a solid lit from the upper left."""
    b = to_monochrome(base)
    return shade(b, 0.16), b, shade(b, -0.18), BRIGHT


COMMON_STUDIO_DEFS = (
    '<linearGradient id="uv_bright" x1="0.15" y1="0" x2="0.85" y2="1">'
    '<stop offset="0%" stop-color="#FFFFFF"/><stop offset="55%" stop-color="#E4E4E7"/>'
    '<stop offset="100%" stop-color="#D4D4D8"/></linearGradient>'
    '<linearGradient id="uv_mid" x1="0" y1="0" x2="0" y2="1">'
    '<stop offset="0%" stop-color="#E4E4E7"/><stop offset="50%" stop-color="#D4D4D8"/>'
    '<stop offset="100%" stop-color="#A1A1AA"/></linearGradient>'
    '<linearGradient id="uv_slate" x1="0" y1="0" x2="0" y2="1">'
    '<stop offset="0%" stop-color="#D4D4D8"/><stop offset="100%" stop-color="#71717A"/></linearGradient>'
    '<linearGradient id="uv_dark" x1="0" y1="0" x2="0" y2="1">'
    '<stop offset="0%" stop-color="#71717A"/><stop offset="100%" stop-color="#3F3F46"/></linearGradient>'
    '<radialGradient id="uv_head_lit" cx="35%" cy="25%" r="72%">'
    '<stop offset="0%" stop-color="#FFFFFF"/><stop offset="55%" stop-color="#E4E4E7"/>'
    '<stop offset="100%" stop-color="#A1A1AA"/></radialGradient>'
    '<radialGradient id="uv_sphere" cx="35%" cy="30%" r="68%">'
    '<stop offset="0%" stop-color="#FFFFFF"/><stop offset="48%" stop-color="#D4D4D8"/>'
    '<stop offset="100%" stop-color="#71717A"/></radialGradient>'
    '<linearGradient id="card_bg" x1="0" y1="0" x2="0" y2="1">'
    '<stop offset="0%" stop-color="#27272A"/><stop offset="65%" stop-color="#1C1C1F"/>'
    '<stop offset="100%" stop-color="#141417"/></linearGradient>'
    '<radialGradient id="card_floor" cx="50%" cy="78%" r="42%">'
    '<stop offset="0%" stop-color="#000000" stop-opacity="0.55"/>'
    '<stop offset="100%" stop-color="#000000" stop-opacity="0"/></radialGradient>'
    '<filter id="soft_drop" x="-12%" y="-12%" width="124%" height="124%">'
    '<feDropShadow dx="0" dy="1.3" stdDeviation="1.0" flood-color="#000000" flood-opacity="0.40"/></filter>'
)


class Icon:
    """Collects defs and body elements for one 64x64 monochrome icon."""

    def __init__(self, name):
        self.name = name
        self.defs = [COMMON_STUDIO_DEFS]
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
            f'<stop offset="{o}" stop-color="{to_monochrome(c)}"' + (f' stop-opacity="{a}"' if a != 1 else "") + "/>"
            for o, c, a in _stops(stops))
        self.defs.append(f'<linearGradient id="{gid}" x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" '
                         f'gradientUnits="{units}">{stop_xml}</linearGradient>')
        return f"url(#{gid})"

    def radial(self, stops, cx=0.5, cy=0.5, r=0.5, fx=None, fy=None, units="objectBoundingBox"):
        gid = self.uid("r")
        stop_xml = "".join(
            f'<stop offset="{o}" stop-color="{to_monochrome(c)}"' + (f' stop-opacity="{a}"' if a != 1 else "") + "/>"
            for o, c, a in _stops(stops))
        focus = f' fx="{fx}" fy="{fy}"' if fx is not None else ""
        self.defs.append(f'<radialGradient id="{gid}" cx="{cx}" cy="{cy}" r="{r}"{focus} '
                         f'gradientUnits="{units}">{stop_xml}</radialGradient>')
        return f"url(#{gid})"

    def svg(self):
        return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" width="64" height="64">'
                f'<title>{self.name}</title><defs>{"".join(self.defs)}</defs>'
                f'<g filter="url(#soft_drop)">{"".join(self.body)}</g></svg>')

    def card_svg(self, badge=None):
        badge_xml = ""
        if badge:
            badge_xml = (
                f'<rect x="38" y="9.5" width="15" height="9.5" rx="2.2" fill="#141417" fill-opacity="0.86" '
                f'stroke="#52525B" stroke-width="0.7"/>'
                f'<text x="45.5" y="16.6" font-family="Segoe UI, Inter, Arial, sans-serif" font-size="6.8" '
                f'font-weight="800" fill="#D4D4D8" text-anchor="middle" letter-spacing="0.3">{badge}</text>'
            )
        return (
            '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" width="64" height="64">'
            f'<title>{self.name}</title><defs>{"".join(self.defs)}</defs>'
            '<rect x="5" y="6.5" width="54" height="51" rx="6" fill="url(#card_bg)" stroke="#3F3F46" stroke-width="1.2"/>'
            '<rect x="6.2" y="7.7" width="51.6" height="48.6" rx="5" fill="none" stroke="#FFFFFF" stroke-opacity="0.05" stroke-width="0.8"/>'
            '<ellipse cx="32" cy="49" rx="18" ry="4.5" fill="url(#card_floor)"/>'
            f'<g transform="translate(32, 33) scale(0.68) translate(-32, -32)" filter="url(#soft_drop)">{"".join(self.body)}</g>'
            f'{badge_xml}</svg>'
        )


def _stops(stops):
    for stop in stops:
        if len(stop) == 2:
            yield stop[0], stop[1], 1
        else:
            yield stop


def pts(points):
    return " ".join(f"{x:.2f},{y:.2f}" for x, y in points)


def iso(cx, cy, x, y, z, s=1.0):
    """Isometric projection of (x, y, z), y up, around the 2D anchor (cx, cy)."""
    return cx + (x - z) * COS30 * s, cy + (x + z) * SIN30 * s - y * s


def shadow(icon, cx, cy, rx, ry, strength=0.35):
    fill = icon.radial([(0, "#000000", strength), (0.65, "#000000", strength * 0.4), (1, "#000000", 0)])
    icon.add(f'<ellipse cx="{cx}" cy="{cy}" rx="{rx}" ry="{ry}" fill="{fill}"/>')


def box(icon, cx, cy, w, h, d, base=SILVER, glass=False, edge=True, outline=True):
    """Isometric 3-tone matte studio box standing on (cx, cy)."""
    top_c, left_c, right_c, edge_c = tones(base)
    a, b = w / 2, d / 2
    P = lambda x, y, z: iso(cx, cy, x, y, z)
    top = [P(-a, h, -b), P(a, h, -b), P(a, h, b), P(-a, h, b)]
    left = [P(-a, 0, b), P(a, 0, b), P(a, h, b), P(-a, h, b)]
    right = [P(a, 0, b), P(a, 0, -b), P(a, h, -b), P(a, h, b)]
    alpha = 0.52 if glass else 1.0
    icon.add(f'<polygon points="{pts(left)}" fill="{icon.linear([(0, shade(left_c, 0.05), alpha), (1, shade(left_c, -0.05), alpha)], 0, 0, 1, 1)}"/>')
    icon.add(f'<polygon points="{pts(right)}" fill="{icon.linear([(0, right_c, alpha), (1, shade(right_c, -0.08), alpha)], 0, 0, 1, 1)}"/>')
    icon.add(f'<polygon points="{pts(top)}" fill="{icon.linear([(0, BRIGHT, alpha), (1, top_c, alpha)], 0, 0, 1, 1)}"/>')
    if edge:
        icon.add(f'<polyline points="{pts([P(-a, h, b), P(a, h, b), P(a, h, -b)])}" fill="none" '
                 f'stroke="{edge_c}" stroke-opacity="0.85" stroke-width="1.1" stroke-linejoin="round"/>')
        icon.add(f'<line x1="{P(a, h, b)[0]:.2f}" y1="{P(a, h, b)[1]:.2f}" x2="{P(a, 0, b)[0]:.2f}" '
                 f'y2="{P(a, 0, b)[1]:.2f}" stroke="{edge_c}" stroke-opacity="0.45" stroke-width="1"/>')
    if outline:
        hull = [P(-a, 0, b), P(a, 0, b), P(a, 0, -b), P(a, h, -b), P(-a, h, -b), P(-a, h, b)]
        icon.add(f'<polygon points="{pts(hull)}" fill="none" stroke="{DEEP}" stroke-opacity="0.65" '
                 f'stroke-width="1.2" stroke-linejoin="round"/>')
    return top, left, right


def wire_box(icon, cx, cy, w, h, d, color=LIGHT, dash="3 2.2", width=1.8, back=True):
    """Monochrome wireframe isometric box."""
    c = to_monochrome(color)
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
            icon.add(f'<line x1="{x1:.2f}" y1="{y1:.2f}" x2="{x2:.2f}" y2="{y2:.2f}" stroke="{c}" '
                     f'stroke-opacity="0.45" stroke-width="{width * 0.8}" stroke-dasharray="{dash}" stroke-linecap="round"/>')
    for (x1, y1), (x2, y2) in front:
        icon.add(f'<line x1="{x1:.2f}" y1="{y1:.2f}" x2="{x2:.2f}" y2="{y2:.2f}" stroke="{c}" '
                 f'stroke-width="{width}" stroke-linecap="round"/>')


def sphere(icon, cx, cy, r, base=SILVER, gloss=0.35, outline=True):
    """Smooth soft-matte monochrome studio sphere (not glossy)."""
    b = to_monochrome(base)
    top_c, _, dark_c, _ = tones(b)
    fill = icon.radial([(0, WHITE), (0.48, top_c), (0.82, b), (1, shade(dark_c, -0.06))],
                       cx=0.36, cy=0.30, r=0.72, fx=0.34, fy=0.28)
    icon.add(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{fill}"/>')
    if outline:
        icon.add(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="none" stroke="{DEEP}" stroke-opacity="0.55" stroke-width="1.1"/>')


def cylinder(icon, cx, cy, rx, h, base=SILVER, ry=None, outline=True, glass=False):
    """Upright soft-matte monochrome cylinder."""
    b = to_monochrome(base)
    ry = rx * 0.46 if ry is None else ry
    top_c, _, dark_c, edge_c = tones(b)
    alpha = 0.58 if glass else 1.0
    body = icon.linear([(0, shade(b, 0.08), alpha), (0.35, top_c, alpha), (0.7, b, alpha), (1, dark_c, alpha)], 0, 0, 1, 0)
    icon.add(f'<path d="M {cx - rx} {cy - h} L {cx - rx} {cy} A {rx} {ry} 0 0 0 {cx + rx} {cy} L {cx + rx} {cy - h} Z" fill="{body}"/>')
    top = icon.linear([(0, BRIGHT, alpha), (1, top_c, alpha)], 0, 0, 1, 1)
    icon.add(f'<ellipse cx="{cx}" cy="{cy - h}" rx="{rx}" ry="{ry}" fill="{top}"/>')
    icon.add(f'<path d="M {cx - rx} {cy - h} A {rx} {ry} 0 0 0 {cx + rx} {cy - h}" fill="none" stroke="{edge_c}" stroke-opacity="0.75" stroke-width="1"/>')
    if outline:
        icon.add(f'<path d="M {cx - rx} {cy - h} A {rx} {ry} 0 0 1 {cx + rx} {cy - h} L {cx + rx} {cy} A {rx} {ry} 0 0 1 {cx - rx} {cy} Z" '
                 f'fill="none" stroke="{DEEP}" stroke-opacity="0.6" stroke-width="1.1" stroke-linejoin="round"/>')


def slab(icon, cx, cy, w, d, t, base=SILVER, outline=True):
    return box(icon, cx, cy, w, t, d, base, outline=outline)


def stroke_line(icon, points, color=LIGHT, width=2.4, outline=False, cap="round", opacity=1.0, dash=None):
    c = to_monochrome(color)
    d = "M " + " L ".join(f"{x:.2f} {y:.2f}" for x, y in points)
    dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
    icon.add(f'<path d="{d}" fill="none" stroke="{c}" stroke-opacity="{opacity}" stroke-width="{width}" '
             f'stroke-linecap="{cap}" stroke-linejoin="round"{dash_attr}/>')


def arrow_head(icon, tip, direction, size, color=BRIGHT):
    c = to_monochrome(color)
    dx, dy = direction
    n = math.hypot(dx, dy) or 1
    dx, dy = dx / n, dy / n
    px, py = -dy, dx
    base_pt = (tip[0] - dx * size, tip[1] - dy * size)
    tri = [tip, (base_pt[0] + px * size * 0.55, base_pt[1] + py * size * 0.55),
           (base_pt[0] - px * size * 0.55, base_pt[1] - py * size * 0.55)]
    fill = icon.linear([(0, shade(c, 0.15)), (1, shade(c, -0.10))], 0, 0, 1, 1)
    icon.add(f'<polygon points="{pts(tri)}" fill="{fill}" stroke="{DEEP}" stroke-opacity="0.5" stroke-width="1.0" stroke-linejoin="round"/>')


def arrow(icon, start, end, color=BRIGHT, width=2.6, head=6.5):
    dx, dy = end[0] - start[0], end[1] - start[1]
    n = math.hypot(dx, dy) or 1
    shaft_end = (end[0] - dx / n * head * 0.75, end[1] - dy / n * head * 0.75)
    stroke_line(icon, [start, shaft_end], color, width)
    arrow_head(icon, end, (dx, dy), head, color)


def glow(icon, cx, cy, r, color=WHITE, strength=0.28):
    c = to_monochrome(color)
    fill = icon.radial([(0, c, strength), (0.5, c, strength * 0.35), (1, c, 0)])
    icon.add(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{fill}"/>')


def page(icon, x, y, w, h, base=LIGHT, fold=9):
    """A clean studio document sheet with folded corner in monochrome gray-white."""
    b = to_monochrome(base)
    top_c, _, dark_c, _ = tones(b)
    icon.add(f'<path d="M {x + 2} {y + 2} h {w - fold} l {fold} {fold} v {h - fold} h {-w} Z" fill="{DARK}" fill-opacity="0.45"/>')
    body = icon.linear([(0, BRIGHT), (1, b)], 0, 0, 1, 1)
    icon.add(f'<path d="M {x} {y} h {w - fold} l {fold} {fold} v {h - fold} h {-w} Z" fill="{body}" '
             f'stroke="{DEEP}" stroke-opacity="0.6" stroke-width="1.2" stroke-linejoin="round"/>')
    icon.add(f'<path d="M {x + w - fold} {y} v {fold} h {fold}" fill="{MID}" '
             f'stroke="{DEEP}" stroke-opacity="0.55" stroke-width="1.1" stroke-linejoin="round"/>')


def glass_box(icon, cx, cy, w, h, d, color=SILVER, fill_alpha=0.18, width=1.7):
    c = to_monochrome(color)
    a, b = w / 2, d / 2
    P = lambda x, y, z: iso(cx, cy, x, y, z)
    faces = [
        [P(-a, 0, b), P(a, 0, b), P(a, h, b), P(-a, h, b)],
        [P(a, 0, b), P(a, 0, -b), P(a, h, -b), P(a, h, b)],
        [P(-a, h, -b), P(a, h, -b), P(a, h, b), P(-a, h, b)],
    ]
    for face in faces:
        icon.add(f'<polygon points="{pts(face)}" fill="{c}" fill-opacity="{fill_alpha}"/>')
    wire_box(icon, cx, cy, w, h, d, shade(c, 0.14), width=width)


def faceted_sphere(icon, cx, cy, r, base=SILVER, detail=1, rot=(18, 28), edges=True, outline=True, wire=None):
    b = to_monochrome(base)
    verts, faces = _polyhedron(detail)
    ax, ay = (math.radians(a) for a in rot)

    def rotate(v):
        x, y, z = v
        y, z = y * math.cos(ax) - z * math.sin(ax), y * math.sin(ax) + z * math.cos(ax)
        x, z = x * math.cos(ay) + z * math.sin(ay), -x * math.sin(ay) + z * math.cos(ay)
        return x, y, z

    verts = [rotate(v) for v in verts]
    light = _norm((-0.55, 0.72, 0.55))
    lo, hi = shade(b, -0.22), WHITE
    drawn = []
    for f in faces:
        a, b_v, c = (verts[i] for i in f)
        n = _norm(_cross(_sub(b_v, a), _sub(c, a)))
        if n[2] < 0:
            n = tuple(-k for k in n)
            a, c = c, a
        centre_z = (a[2] + b_v[2] + c[2]) / 3
        facing = _dot(_norm(_add3(a, b_v, c)), (0, 0, 1))
        if facing <= 0:
            continue
        t = max(0.0, _dot(n, light)) ** 0.85
        drawn.append((centre_z, [a, b_v, c], _mix(lo, hi, t)))
    drawn.sort(key=lambda d: d[0])
    for _, tri, colour in drawn:
        points = [(cx + v[0] * r, cy - v[1] * r) for v in tri]
        if wire:
            stroke = f' stroke="{to_monochrome(wire)}" stroke-opacity="0.7" stroke-width="1.0" stroke-linejoin="round"'
        elif edges:
            stroke = f' stroke="{shade(colour, -0.12)}" stroke-width="0.6" stroke-linejoin="round"'
        else:
            stroke = f' stroke="{colour}" stroke-width="0.5" stroke-linejoin="round"'
        icon.add(f'<polygon points="{pts(points)}" fill="{colour}"{stroke}/>')


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


def tile(icon, base=SLATE):
    """Matte monochrome neutral studio tile for category icons."""
    icon.add('<rect x="6" y="6" width="52" height="52" rx="11" fill="url(#card_bg)" stroke="#52525B" stroke-width="1.4"/>')
    icon.add('<rect x="7.5" y="7.5" width="49" height="49" rx="9.5" fill="none" stroke="#FFFFFF" stroke-opacity="0.06" stroke-width="0.9"/>')


def glyph(icon, d, width=3.8, color="#f4f4f5", fill="none"):
    c = to_monochrome(color)
    f = "none" if fill == "none" else to_monochrome(fill)
    icon.add(f'<path d="{d}" fill="{f}" stroke="{c}" stroke-width="{width}" stroke-linecap="round" stroke-linejoin="round"/>')


def figure(icon, pose="stand", base=LIGHT, dx=0.0, dy=0.0, scale=1.0, joints=None):
    """Unified, connected sculpted mannequin silhouette (head + chin + neck + torso + limbs connected seamlessly)."""
    icon.add(f'<g transform="translate({dx:.2f}, {dy:.2f}) translate(32, 32) scale({scale:.3f}) translate(-32, -32)">')
    icon.add(
        '<path d="M 32 7.5 C 35.8 7.5, 37.8 10.0, 37.6 13.8 C 37.4 16.5, 35.8 18.8, 34.2 19.6 L 34.2 21.0 '
        'C 38.5 21.6, 41.5 23.8, 43.0 29.5 L 44.5 34.5 C 45.0 36.2, 42.8 37.0, 41.8 35.5 L 39.2 27.5 L 37.5 27.5 '
        'L 37.5 37.0 L 40.5 51.0 C 40.9 53.0, 38.0 53.8, 37.2 51.8 L 33.2 39.5 L 30.8 39.5 L 26.8 51.8 '
        'C 26.0 53.8, 23.1 53.0, 23.5 51.0 L 26.5 37.0 L 26.5 27.5 L 24.8 27.5 L 22.2 35.5 C 21.2 37.0, 19.0 36.2, 19.5 34.5 '
        'L 21.0 29.5 C 22.5 23.8, 25.5 21.6, 29.8 21.0 L 29.8 19.6 C 28.2 18.8, 26.6 16.5, 26.4 13.8 '
        'C 26.2 10.0, 28.2 7.5, 32 7.5 Z" fill="url(#uv_bright)"/>'
        '<path d="M 29.2 19.2 Q 32 21.2 34.8 19.2" fill="none" stroke="#71717A" stroke-width="1.3" stroke-linecap="round"/>'
    )
    if joints:
        for jx, jy in ((32, 20), (26.5, 24), (37.5, 24), (32, 37), (26.5, 38), (37.5, 38)):
            icon.add(f'<circle cx="{jx}" cy="{jy}" r="2.1" fill="#52525B" stroke="#F4F4F5" stroke-width="0.9"/>')
    icon.add('</g>')
