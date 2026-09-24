"""The UniVex editor icon set: scene nodes, node-palette categories and content-browser assets."""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))

from kit import (Icon, OUTLINE, arrow, arrow_head, box, cylinder, faceted_sphere, figure, glass_box, glow,
                 glyph, iso, page, pts, shade, shadow, slab, sphere, stroke_line, tile, tones, wire_box)

# One hue per family, so a row's colour already says what kind of node it is.
STEEL = "#7f93ad"      # scene structure
AMBER = "#f0962e"      # geometry
TEAL = "#24b3ad"       # cameras
SUN = "#ffc83a"        # lights
BLUE = "#3f86f0"       # physics bodies
MINT = "#3fd47e"       # collision shapes
NAV = "#5cc84a"        # navigation
PURPLE = "#a46cf0"     # animation
RED = "#e8484f"        # combat
GOLD = "#f2b33b"       # gameplay
PINK = "#ec6aae"       # audio
MAGENTA = "#f45fd0"    # effects
CODE = "#5b8cff"       # logic
LIME = "#9bd548"       # ui
WORLD = "#2fbd9b"      # world
SLATE = "#58a4c8"      # optimisation
SKY = "#4aa8f0"        # environment
IVORY = "#eadcc0"      # bones
AXIS_X, AXIS_Y, AXIS_Z = "#f0514d", "#72d94c", "#4d8ef0"
COS = math.cos(math.radians(30))

# The three icon groups, each one directory under Engine/Editor/EditorCore/assets/icons/. An icon's
# id is its file stem and the name the editor looks it up by: a node's registry typeId, a palette
# category in lower case, a content browser type label in lower case.
NODES, NODE_CATEGORIES, CONTENT_TYPES = "nodes", "node_categories", "content_types"

ICONS = {}   # (group, id) -> (title, section, draw function), in registration order


def _register(group, icon_id, title, section):
    def register(fn):
        key = (group, icon_id)
        if key in ICONS:
            raise ValueError(f"icon {group}/{icon_id} is registered twice")
        ICONS[key] = (title, section, fn)
        return fn
    return register


def scene_node(type_id, title, section):
    return _register(NODES, type_id, title, section)


def content_type(icon_id, title):
    return _register(CONTENT_TYPES, icon_id, title, "Content types")


def build(key):
    title, _, fn = ICONS[key]
    ic = Icon(title)
    fn(ic)
    return ic.svg()


def node(ic, cx, cy, r, colour):
    sphere(ic, cx, cy, r, colour)


def link(ic, a, b, colour="#dfe6ef", width=2.6):
    stroke_line(ic, [a, b], colour, width)


def eye(ic, cx, cy, w, colour, slashed=False):
    """An eye: lids as a filled almond, an iris, a highlight."""
    h = w * 0.55
    lid = f"M {cx - w / 2} {cy} Q {cx} {cy - h} {cx + w / 2} {cy} Q {cx} {cy + h} {cx - w / 2} {cy} Z"
    white = ic.linear([(0, "#ffffff"), (1, "#cfd8e3")], 0, 0, 0, 1)
    ic.add(f'<path d="{lid}" fill="{white}" stroke="{OUTLINE}" stroke-opacity="0.75" stroke-width="1.5"/>')
    iris = ic.radial([(0, shade(colour, 0.25)), (0.7, colour), (1, shade(colour, -0.25))], cx=0.4, cy=0.35)
    ic.add(f'<circle cx="{cx}" cy="{cy}" r="{h * 0.42:.2f}" fill="{iris}"/>')
    ic.add(f'<circle cx="{cx}" cy="{cy}" r="{h * 0.18:.2f}" fill="#0b0f15"/>')
    ic.add(f'<circle cx="{cx - h * 0.14:.2f}" cy="{cy - h * 0.14:.2f}" r="{h * 0.08:.2f}" fill="#fff"/>')
    if slashed:
        stroke_line(ic, [(cx - w * 0.42, cy + h * 0.62), (cx + w * 0.42, cy - h * 0.62)], "#ff5d5d", 3.2)


def star(cx, cy, r_outer, r_inner, points=4, rotation=0):
    out = []
    for i in range(points * 2):
        r = r_outer if i % 2 == 0 else r_inner
        a = math.radians(rotation + i * 180 / points - 90)
        out.append((cx + math.cos(a) * r, cy + math.sin(a) * r))
    return out


# =============================================================================================
# Scene
# =============================================================================================

def axes(ic, ox, oy, length=20, width=4.4, head=10.5):
    arrow(ic, (ox, oy), iso(ox, oy, 0, 0, length), AXIS_Z, width, head)
    arrow(ic, (ox, oy), iso(ox, oy, length, 0, 0), AXIS_X, width, head)
    arrow(ic, (ox, oy), iso(ox, oy, 0, length, 0), AXIS_Y, width, head)


@scene_node("scene_root", "SceneRoot", "Scene")
def scene_root(ic):
    shadow(ic, 32, 56, 26, 6)
    slab(ic, 32, 50, 40, 40, 5, shade(STEEL, -0.12))
    link(ic, (32, 17), (19, 34))
    link(ic, (32, 17), (45, 34))
    node(ic, 19, 34, 6, shade(STEEL, 0.05))
    node(ic, 45, 34, 6, shade(STEEL, 0.05))
    node(ic, 32, 16, 8, "#9fb4cf")


@scene_node("node_3d", "Node3D", "Scene")
def node_3d(ic):
    shadow(ic, 30, 54, 22, 6)
    axes(ic, 29, 41, 27)
    sphere(ic, 29, 41, 8.5, STEEL)


@scene_node("marker_3d", "Marker3D", "Scene")
def marker_3d(ic):
    shadow(ic, 32, 54, 14, 4.5, 0.5)
    pin = "#ef5b3d"
    ic.add(f'<ellipse cx="32" cy="53" rx="9" ry="3.2" fill="none" stroke="{pin}" stroke-opacity="0.8" stroke-width="1.6"/>')
    fill = ic.radial([(0, shade(pin, 0.3)), (0.55, pin), (1, shade(pin, -0.25))], cx=0.36, cy=0.3, r=0.75)
    ic.add(f'<path d="M 32 53 C 27 42 17 34 17 23 A 15 15 0 0 1 47 23 C 47 34 37 42 32 53 Z" fill="{fill}" '
           f'stroke="{OUTLINE}" stroke-opacity="0.75" stroke-width="1.5" stroke-linejoin="round"/>')
    sphere(ic, 32, 23, 6.2, "#f4f6f9", gloss=0.7)
    spec = ic.radial([(0, "#fff", 0.8), (1, "#fff", 0)])
    ic.add(f'<ellipse cx="24.5" cy="15.5" rx="4.5" ry="3" fill="{spec}" transform="rotate(-35 24.5 15.5)"/>')


@scene_node("spawn_point_3d", "SpawnPoint3D", "Gameplay")
def spawn_point_3d(ic):
    shadow(ic, 32, 58, 25, 4.5)
    cylinder(ic, 32, 56, 24, 4, shade(GOLD, -0.12), ry=8.5)
    for ry, alpha in ((8.5, 0.9), (6, 0.6)):
        ic.add(f'<ellipse cx="32" cy="{52 - (8.5 - ry) * 2.2:.1f}" rx="{ry * 2.4:.1f}" ry="{ry:.1f}" fill="none" '
               f'stroke="#fff1c4" stroke-opacity="{alpha}" stroke-width="1.6"/>')
    figure(ic, "stand", GOLD, dy=-3, scale=0.86)


@scene_node("script", "Script", "Logic")
def script_node(ic):
    shadow(ic, 33, 57, 21, 4.5)
    page(ic, 12, 6, 38, 48, CODE, fold=11)
    brace = "M 26 19 q -5 0 -5 5 v 3 q 0 3 -3 3 q 3 0 3 3 v 3 q 0 5 5 5"
    ic.add(f'<path d="{brace}" fill="none" stroke="#ffffff" stroke-width="3.2" stroke-linecap="round" '
           f'stroke-linejoin="round"/>')
    ic.add(f'<path d="{brace}" transform="translate(64 0) scale(-1 1) translate(1 0)" fill="none" stroke="#ffffff" '
           f'stroke-width="3.2" stroke-linecap="round" stroke-linejoin="round"/>')


# =============================================================================================
# Physics
# =============================================================================================

@scene_node("area_3d", "Area3D", "Physics")
def area_3d(ic):
    zone = "#4ec3f2"
    shadow(ic, 32, 56, 24, 5, 0.25)
    body = ic.linear([(0, zone, 0.14), (0.5, zone, 0.3), (1, zone, 0.12)], 0, 0, 1, 0)
    ic.add(f'<path d="M 9 14 V 50 A 23 8 0 0 0 55 50 V 14 Z" fill="{body}"/>')
    ic.add(f'<ellipse cx="32" cy="14" rx="23" ry="8" fill="{zone}" fill-opacity="0.22"/>')
    for d in ("M 9 50 A 23 8 0 0 0 55 50", "M 9 14 A 23 8 0 0 0 55 14", "M 9 14 V 50", "M 55 14 V 50"):
        ic.add(f'<path d="{d}" fill="none" stroke="{OUTLINE}" stroke-opacity="0.5" stroke-width="3.6" stroke-linecap="round"/>')
        ic.add(f'<path d="{d}" fill="none" stroke="{shade(zone, 0.15)}" stroke-width="2" stroke-linecap="round"/>')
    for d in ("M 9 50 A 23 8 0 0 1 55 50", "M 9 14 A 23 8 0 0 1 55 14"):
        ic.add(f'<path d="{d}" fill="none" stroke="{shade(zone, 0.15)}" stroke-opacity="0.6" stroke-width="1.6" '
               f'stroke-dasharray="3 2.5"/>')
    for x in (24, 32, 40):
        arrow(ic, (x, 22), (x, 38), "#e6f8ff", 2.2, 6)


@scene_node("ray_cast_3d", "RayCast3D", "Physics")
def ray_cast_3d(ic):
    shadow(ic, 44, 56, 16, 4.5)
    slab(ic, 44, 52, 22, 22, 3, shade(STEEL, -0.1))
    for rx, ry, a in ((8, 4, 0.9), (4.5, 2.2, 1)):
        ic.add(f'<ellipse cx="45" cy="45" rx="{rx}" ry="{ry}" fill="none" stroke="#ff7b5a" stroke-opacity="{a}" stroke-width="1.8"/>')
    stroke_line(ic, [(16, 16), (42, 42)], "#ffd0c0", 2.4, dash="4 2.5")
    arrow_head(ic, (45, 45), (1, 1), 8, "#ff6a4d")
    sphere(ic, 15, 15, 7.5, BLUE)


@scene_node("static_body_3d", "StaticBody3D", "Physics")
def static_body_3d(ic):
    shadow(ic, 32, 57, 27, 6)
    box(ic, 32, 51, 34, 18, 34, "#6a86ad")
    # a bolt on the top face says "fixed in place"
    cx, cy = iso(32, 51, 0, 18, 0)
    cylinder(ic, cx, cy + 1, 4.2, 5, "#c7d2df", ry=2.2)


@scene_node("animatable_body_3d", "AnimatableBody3D", "Physics")
def animatable_body_3d(ic):
    shadow(ic, 32, 55, 22, 4.5)
    slab(ic, 32, 45, 30, 20, 6, BLUE)
    left, right = iso(32, 45, -30, 3, 0), iso(32, 45, 30, 3, 0)
    stroke_line(ic, [iso(32, 45, -17, 3, 0), left], "#e3ecff", 2.2, dash="3 2.5")
    stroke_line(ic, [iso(32, 45, 17, 3, 0), right], "#e3ecff", 2.2, dash="3 2.5")
    arrow_head(ic, left, (-COS, -0.5), 8, PURPLE)
    arrow_head(ic, right, (COS, 0.5), 8, PURPLE)
    # what it carries
    shadow(ic, 30, 37, 6, 2, 0.4)
    box(ic, 30, 36, 9, 9, 9, shade(STEEL, 0.05))


@scene_node("character_body_3d", "CharacterBody3D", "Physics")
def character_body_3d(ic):
    shadow(ic, 31, 57, 20, 4.5)
    # the body's collision capsule, drawn around the runner
    ic.add(f'<rect x="14" y="3" width="36" height="55" rx="18" fill="{BLUE}" fill-opacity="0.1" stroke="{MINT}" '
           f'stroke-width="1.8" stroke-dasharray="4 3"/>')
    figure(ic, "run", BLUE)


@scene_node("collider_3d", "Collider3D", "Physics")
def collider_3d(ic):
    shadow(ic, 32, 56, 22, 5, 0.3)
    faceted_sphere(ic, 32, 38, 15, "#8f98a4", detail=0, rot=(32, 12))
    wire_box(ic, 32, 51, 27, 28, 27, MINT, dash="3 2.2", width=2.2)


@scene_node("rigid_body_3d", "RigidBody3D", "Physics")
def rigid_body_3d(ic):
    wood = "#bf8a52"
    shadow(ic, 34, 58, 17, 4)
    for i, (x, y) in enumerate(((13, 10), (8, 20), (12, 30))):
        stroke_line(ic, [(x, y), (x + 9, y + 6)], "#d8e6ff", 2.2, opacity=0.9 - i * 0.15)
    ic.add('<g transform="rotate(-16 36 30)">')
    top, left, right = box(ic, 36, 44, 24, 24, 24, wood)
    for face in (left, right):
        a, b, c, d = face
        for t in (0.34, 0.67):
            p = (a[0] + (d[0] - a[0]) * t, a[1] + (d[1] - a[1]) * t)
            q = (b[0] + (c[0] - b[0]) * t, b[1] + (c[1] - b[1]) * t)
            ic.add(f'<line x1="{p[0]:.2f}" y1="{p[1]:.2f}" x2="{q[0]:.2f}" y2="{q[1]:.2f}" stroke="{shade(wood, -0.25)}" '
                   f'stroke-width="1.1"/>')
    ic.add('</g>')
    arrow(ic, (51, 44), (51, 60), "#ffd24a", 3, 7)


# =============================================================================================
# Navigation
# =============================================================================================

@scene_node("navigation_region_3d", "NavigationRegion3D", "Navigation")
def navigation_region_3d(ic):
    shadow(ic, 32, 55, 27, 5.5)
    top, _, _ = slab(ic, 32, 50, 42, 42, 4, NAV)
    a, b, c, d = top
    mid = lambda p, q, t: (p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t)
    lines = [(a, c), (mid(a, b, 0.5), mid(d, c, 0.5)), (mid(a, d, 0.5), mid(b, c, 0.5)),
             (mid(a, b, 0.5), mid(a, d, 0.5)), (mid(b, c, 0.5), mid(d, c, 0.5))]
    for p, q in lines:
        ic.add(f'<line x1="{p[0]:.2f}" y1="{p[1]:.2f}" x2="{q[0]:.2f}" y2="{q[1]:.2f}" stroke="#eaffdf" '
               f'stroke-opacity="0.85" stroke-width="1.4"/>')
    for p in (a, b, c, d, mid(a, c, 0.5)):
        ic.add(f'<circle cx="{p[0]:.2f}" cy="{p[1]:.2f}" r="1.9" fill="#ffffff"/>')


@scene_node("navigation_agent_3d", "NavigationAgent3D", "Navigation")
def navigation_agent_3d(ic):
    shadow(ic, 22, 58, 12, 3.5)
    stroke_line(ic, [(29, 57), (41, 53), (50, 56)], "#d4f7c4", 2.6, dash="3.5 3")
    # destination flag
    stroke_line(ic, [(53, 57), (53, 34)], "#e8edf2", 2.4)
    flag = ic.linear([(0, shade(NAV, 0.2)), (1, shade(NAV, -0.15))], 0, 0, 1, 1)
    ic.add(f'<path d="M 53 34 L 63 38.5 L 53 43 Z" fill="{flag}" stroke="{OUTLINE}" stroke-opacity="0.7" '
           f'stroke-width="1.2" stroke-linejoin="round"/>')
    figure(ic, "walk", NAV, dx=-10, dy=2, scale=0.9)


# =============================================================================================
# Animation
# =============================================================================================

def bone_segment(ic, x1, y1, x2, y2, width, colour=IVORY):
    """A rig bone: a tapered, lit diamond from the parent joint to the child."""
    dx, dy = x2 - x1, y2 - y1
    n = math.hypot(dx, dy)
    ux, uy = dx / n, dy / n
    px, py = -uy, ux
    widest = (x1 + dx * 0.22, y1 + dy * 0.22)
    shape = [(x1, y1), (widest[0] + px * width, widest[1] + py * width), (x2, y2),
             (widest[0] - px * width, widest[1] - py * width)]
    lit = ic.linear([(0, shade(colour, 0.1)), (1, shade(colour, -0.08))], 0, 0, 1, 1)
    ic.add(f'<polygon points="{pts(shape)}" fill="{lit}" stroke="{OUTLINE}" stroke-opacity="0.75" stroke-width="1.3" '
           f'stroke-linejoin="round"/>')
    dark = [(x1, y1), (widest[0] - px * width, widest[1] - py * width), (x2, y2)]
    ic.add(f'<polygon points="{pts(dark)}" fill="{shade(colour, -0.22)}" fill-opacity="0.8"/>')


def bone(ic, x1, y1, x2, y2, r, colour=IVORY):
    dx, dy = x2 - x1, y2 - y1
    n = math.hypot(dx, dy)
    ux, uy = dx / n, dy / n
    px, py = -uy, ux
    shaft = [(x1 + px * r * 0.55, y1 + py * r * 0.55), (x2 + px * r * 0.55, y2 + py * r * 0.55),
             (x2 - px * r * 0.55, y2 - py * r * 0.55), (x1 - px * r * 0.55, y1 - py * r * 0.55)]
    fill = ic.linear([(0, shade(colour, 0.08)), (0.5, colour), (1, shade(colour, -0.2))], 0, 0, 1, 1)
    ic.add(f'<polygon points="{pts(shaft)}" fill="{fill}" stroke="{OUTLINE}" stroke-opacity="0.7" stroke-width="1.3"/>')
    for (bx, by) in ((x1, y1), (x2, y2)):
        for side in (1, -1):
            sphere(ic, bx + px * r * 0.6 * side, by + py * r * 0.6 * side, r * 0.72, colour, gloss=0.5)
    ic.add(f'<polygon points="{pts(shaft)}" fill="{fill}"/>')


@scene_node("skeleton_3d", "Skeleton3D", "Animation")
def skeleton_3d(ic):
    shadow(ic, 32, 58, 22, 4)
    joints = [(10, 50), (27, 33), (47, 30), (56, 14)]
    for (x1, y1), (x2, y2) in zip(joints, joints[1:]):
        bone_segment(ic, x1, y1, x2, y2, 7 if x1 < 40 else 5.5)
    for i, (x, y) in enumerate(joints):
        sphere(ic, x, y, 5.2 if i < 3 else 4.2, PURPLE)


@scene_node("bone_attachment_3d", "BoneAttachment3D", "Animation")
def bone_attachment_3d(ic):
    shadow(ic, 30, 57, 22, 4.5)
    bone(ic, 12, 40, 36, 17, 7)
    ring = ic.linear([(0, shade(PURPLE, 0.25)), (1, shade(PURPLE, -0.2))], 0, 0, 1, 1)
    ic.add(f'<circle cx="45" cy="44" r="10" fill="none" stroke="{OUTLINE}" stroke-opacity="0.7" stroke-width="7.4"/>')
    ic.add(f'<circle cx="45" cy="44" r="10" fill="none" stroke="{ring}" stroke-width="5"/>')
    ic.add(f'<path d="M 38 38 A 10 10 0 0 1 49 34.8" fill="none" stroke="#fff" stroke-opacity="0.6" stroke-width="1.6" stroke-linecap="round"/>')
    stroke_line(ic, [(36, 26), (40, 36)], "#d9c9ff", 2.4)


@scene_node("animation_player", "AnimationPlayer", "Animation")
def animation_player(ic):
    shadow(ic, 32, 57, 26, 4.5)
    strip = "#3a3446"
    fill = ic.linear([(0, shade(strip, 0.1)), (1, shade(strip, -0.08))], 0, 0, 0, 1)
    ic.add(f'<rect x="5" y="14" width="54" height="38" rx="5" fill="{fill}" stroke="{OUTLINE}" '
           f'stroke-opacity="0.8" stroke-width="1.5"/>')
    for x in range(9, 58, 8):
        for y in (17, 45):
            ic.add(f'<rect x="{x}" y="{y}" width="4.4" height="4" rx="1" fill="#cfc6de" fill-opacity="0.9"/>')
    tri = "M 25 21.5 L 44 33 L 25 44.5 Z"
    play = ic.linear([(0, shade(PURPLE, 0.28)), (0.55, PURPLE), (1, shade(PURPLE, -0.2))], 0, 0, 1, 1)
    ic.add(f'<path d="{tri}" fill="{play}" stroke="{OUTLINE}" stroke-opacity="0.8" stroke-width="1.6" '
           f'stroke-linejoin="round"/>')
    ic.add('<path d="M 27 24.5 L 38.5 31.4" stroke="#fff" stroke-opacity="0.7" stroke-width="1.5" stroke-linecap="round"/>')


@scene_node("animation_tree", "AnimationTree", "Animation")
def animation_tree(ic):
    shadow(ic, 32, 58, 26, 3.5)
    def state(x, y, w, h, colour):
        ic.add(f'<rect x="{x + 1.5}" y="{y + 2.5}" width="{w}" height="{h}" rx="5" fill="{shade(colour, -0.35)}"/>')
        fill = ic.linear([(0, shade(colour, 0.22)), (1, shade(colour, -0.08))], 0, 0, 0, 1)
        ic.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="5" fill="{fill}" stroke="{OUTLINE}" '
               f'stroke-opacity="0.75" stroke-width="1.4"/>')
        ic.add(f'<rect x="{x + 3}" y="{y + 2}" width="{w - 6}" height="3" rx="1.5" fill="#fff" fill-opacity="0.45"/>')
    stroke_line(ic, [(24, 16), (38, 16)], "#e6dcff", 2.4)
    arrow_head(ic, (41, 16), (1, 0), 7, "#e6dcff")
    stroke_line(ic, [(50, 23), (40, 38)], "#e6dcff", 2.4)
    arrow_head(ic, (38, 41), (-0.6, 1), 7, "#e6dcff")
    stroke_line(ic, [(24, 42), (14, 26)], "#e6dcff", 2.4)
    arrow_head(ic, (12.5, 23), (-0.5, -1), 7, "#e6dcff")
    state(3, 8, 22, 15, PURPLE)
    state(40, 8, 21, 15, shade(PURPLE, 0.08))
    state(20, 40, 24, 15, shade(PURPLE, -0.06))


# =============================================================================================
# Camera
# =============================================================================================

def movie_camera(ic, ox, oy, s, lens_colour=TEAL):
    """A movie camera whose body's top-left corner is at (ox, oy), scaled by s."""
    body = "#465262"
    top_c, _, dark_c, edge_c = tones(body)
    X = lambda x: ox + x * s
    Y = lambda y: oy + y * s
    for cx, r in ((12, 8.5), (28, 7.5)):
        sphere(ic, X(cx), Y(-7), r * s, "#566476", gloss=0.6)
        ic.add(f'<circle cx="{X(cx):.2f}" cy="{Y(-7):.2f}" r="{r * s * 0.32:.2f}" fill="{shade(body, -0.2)}"/>')
    fill = ic.linear([(0, shade(top_c, 0.12)), (0.5, body), (1, shade(dark_c, -0.06))], 0, 0, 0, 1)
    ic.add(f'<rect x="{X(0):.2f}" y="{Y(0):.2f}" width="{34 * s:.2f}" height="{26 * s:.2f}" rx="{5 * s:.2f}" '
           f'fill="{fill}" stroke="{OUTLINE}" stroke-opacity="0.75" stroke-width="1.5"/>')
    ic.add(f'<path d="M {X(2):.2f} {Y(2.5):.2f} h {29 * s:.2f}" stroke="{edge_c}" stroke-opacity="0.7" '
           f'stroke-width="1.2" stroke-linecap="round"/>')
    hood = ic.linear([(0, shade(body, 0.1)), (1, shade(body, -0.12))], 0, 0, 0, 1)
    ic.add(f'<polygon points="{pts([(X(34), Y(7)), (X(46), Y(1)), (X(46), Y(25)), (X(34), Y(19))])}" '
           f'fill="{hood}" stroke="{OUTLINE}" stroke-opacity="0.75" stroke-width="1.5" stroke-linejoin="round"/>')
    glass = ic.radial([(0, "#e8fffd"), (0.35, shade(lens_colour, 0.18)), (0.8, lens_colour),
                       (1, shade(lens_colour, -0.25))], cx=0.4, cy=0.35, r=0.7, fx=0.35, fy=0.3)
    ic.add(f'<circle cx="{X(17):.2f}" cy="{Y(13):.2f}" r="{8.5 * s:.2f}" fill="{glass}" stroke="{OUTLINE}" '
           f'stroke-opacity="0.75" stroke-width="1.4"/>')
    ic.add(f'<circle cx="{X(17):.2f}" cy="{Y(13):.2f}" r="{4 * s:.2f}" fill="{shade(lens_colour, -0.3)}" fill-opacity="0.75"/>')
    ic.add(f'<circle cx="{X(14.4):.2f}" cy="{Y(10.2):.2f}" r="{1.8 * s:.2f}" fill="#fff" fill-opacity="0.9"/>')


@scene_node("camera_3d", "Camera3D", "Camera")
def camera_3d(ic):
    shadow(ic, 32, 56, 24, 5.5)
    movie_camera(ic, 8, 24, 1.0)


@scene_node("spring_arm_3d", "SpringArm3D", "Camera")
def spring_arm_3d(ic):
    shadow(ic, 30, 58, 24, 4)
    sphere(ic, 10, 52, 6, STEEL)
    stroke_line(ic, [(10, 52), (19, 45)], "#c9d3df", 3.4)
    coil = []
    for i in range(9):
        t = i / 8
        x, y = 19 + t * 14, 45 - t * 11
        off = 4 if i % 2 else -4
        coil.append((x + off * 0.62, y + off * 0.78))
    stroke_line(ic, [(19, 45)] + coil + [(33, 34)], "#ffd35a", 2.2)
    stroke_line(ic, [(33, 34), (37, 31)], "#c9d3df", 3.4)
    movie_camera(ic, 34, 16, 0.58)


# =============================================================================================
# Combat
# =============================================================================================

@scene_node("hitbox_3d", "Hitbox3D", "Combat")
def hitbox_3d(ic):
    shadow(ic, 30, 58, 20, 3.5)
    # the hit volume around the blade's striking end
    ic.add(f'<rect x="32" y="4" width="28" height="26" rx="4" fill="{RED}" fill-opacity="0.14" stroke="{RED}" '
           f'stroke-width="2" stroke-dasharray="4 2.8"/>')
    ic.add('<g transform="rotate(45 32 32)">')
    blade = ic.linear([(0, "#ffffff"), (0.48, "#c9d3de"), (0.52, "#8594a6"), (1, "#5e6b7c")], 0, 0, 1, 0)
    ic.add(f'<path d="M 28.5 38 V 2 L 32 -4 L 35.5 2 V 38 Z" fill="{blade}" stroke="{OUTLINE}" stroke-opacity="0.8" '
           f'stroke-width="1.4" stroke-linejoin="round"/>')
    guard = ic.linear([(0, "#ffe29a"), (1, "#a8741f")], 0, 0, 0, 1)
    ic.add(f'<rect x="21" y="37" width="22" height="5" rx="2.5" fill="{guard}" stroke="{OUTLINE}" stroke-opacity="0.8" stroke-width="1.3"/>')
    grip = ic.linear([(0, "#7a4a2a"), (1, "#3e2413")], 0, 0, 1, 0)
    ic.add(f'<rect x="29.5" y="42" width="5" height="12" rx="1.5" fill="{grip}" stroke="{OUTLINE}" stroke-opacity="0.8" stroke-width="1.2"/>')
    sphere(ic, 32, 57, 3.6, "#e2b54a")
    ic.add('</g>')


def heart_path(cx, cy, s):
    return (f"M {cx} {cy + 9 * s} C {cx - 14 * s} {cy} {cx - 11 * s} {cy - 10 * s} {cx - 5 * s} {cy - 10 * s} "
            f"C {cx - 2 * s} {cy - 10 * s} {cx} {cy - 8 * s} {cx} {cy - 5.5 * s} "
            f"C {cx} {cy - 8 * s} {cx + 2 * s} {cy - 10 * s} {cx + 5 * s} {cy - 10 * s} "
            f"C {cx + 11 * s} {cy - 10 * s} {cx + 14 * s} {cy} {cx} {cy + 9 * s} Z")


@scene_node("hurtbox_3d", "Hurtbox3D", "Combat")
def hurtbox_3d(ic):
    shadow(ic, 32, 58, 17, 4)
    fill = ic.radial([(0, shade(RED, 0.3)), (0.6, RED), (1, shade(RED, -0.25))], cx=0.35, cy=0.3, r=0.75)
    ic.add(f'<path d="{heart_path(32, 32, 1.9)}" fill="{fill}" stroke="{OUTLINE}" stroke-opacity="0.8" '
           f'stroke-width="1.6" stroke-linejoin="round"/>')
    ic.add('<path d="M 34 14 L 28 25 L 36 30 L 29 42" fill="none" stroke="#3a0d10" stroke-width="2.6" '
           'stroke-linejoin="round" stroke-linecap="round"/>')
    ic.add('<ellipse cx="21" cy="20" rx="5" ry="3" fill="#fff" fill-opacity="0.6" transform="rotate(-35 21 20)"/>')
    for a, b in (((6, 8), (11, 13)), ((58, 8), (53, 13)), ((4, 30), (10, 30))):
        stroke_line(ic, [a, b], "#ffd24a", 2.4)


@scene_node("projectile_3d", "Projectile3D", "Combat")
def projectile_3d(ic):
    shadow(ic, 36, 57, 20, 4)
    for i, off in enumerate((0, 6, 12)):
        stroke_line(ic, [(8 + off * 0.3, 44 - off), (20 + off * 0.3, 38 - off)], "#ffb199", 2.2, opacity=0.9 - i * 0.2)
    ic.add('<g transform="rotate(-38 36 32)">')
    casing = ic.linear([(0, "#ffe7a3"), (0.35, "#e7b04a"), (1, "#8f5d17")], 0, 0, 0, 1)
    ic.add(f'<rect x="18" y="25" width="22" height="14" rx="2" fill="{casing}" stroke="{OUTLINE}" '
           f'stroke-opacity="0.75" stroke-width="1.4"/>')
    tip = ic.linear([(0, "#ffd9c9"), (0.35, "#e45b3e"), (1, "#7a1f14")], 0, 0, 0, 1)
    ic.add(f'<path d="M 40 25 C 50 25 55 29 58 32 C 55 35 50 39 40 39 Z" fill="{tip}" stroke="{OUTLINE}" '
           f'stroke-opacity="0.75" stroke-width="1.4" stroke-linejoin="round"/>')
    ic.add('<rect x="20" y="27" width="18" height="2.5" rx="1.2" fill="#fff" fill-opacity="0.55"/>')
    ic.add('</g>')


# =============================================================================================
# Gameplay
# =============================================================================================

@scene_node("interaction_area_3d", "InteractionArea3D", "Gameplay")
def interaction_area_3d(ic):
    ic.add(f'<ellipse cx="32" cy="50" rx="28" ry="11" fill="{GOLD}" fill-opacity="0.16" stroke="{GOLD}" '
           f'stroke-width="2.4" stroke-dasharray="5 3.5"/>')
    shadow(ic, 32, 52, 18, 4.5)
    # a key cap, seen from the front: dark skirt, bright face, the "E" legend upright
    ic.add(f'<rect x="14" y="20" width="36" height="32" rx="8" fill="#8793a3" stroke="{OUTLINE}" '
           f'stroke-opacity="0.8" stroke-width="1.5"/>')
    face = ic.linear([(0, "#ffffff"), (1, "#d3dae3")], 0, 0, 0, 1)
    ic.add(f'<rect x="17" y="16" width="30" height="28" rx="6.5" fill="{face}" stroke="{OUTLINE}" '
           f'stroke-opacity="0.55" stroke-width="1.2"/>')
    ic.add('<path d="M 38 22.5 H 27 V 37.5 H 38 M 27 30 H 35.5" fill="none" stroke="#27303b" stroke-width="3.6" '
           'stroke-linecap="round" stroke-linejoin="round"/>')
    # someone within reach
    figure(ic, "stand", shade(GOLD, -0.05), dx=19, dy=-9, scale=0.5)


# =============================================================================================
# Rendering
# =============================================================================================

@scene_node("mesh_instance_3d", "MeshInstance3D", "Rendering")
def mesh_instance_3d(ic):
    shadow(ic, 32, 57, 20, 5)
    faceted_sphere(ic, 34, 29, 20, AMBER, detail=0)
    # the instance's own transform at its pivot
    axes(ic, 14, 52, 11, 2.8, 6)


@scene_node("box_mesh_3d", "BoxMesh3D", "Rendering")
def box_mesh_3d(ic):
    shadow(ic, 32, 56, 24, 7)
    box(ic, 32, 47, 26, 26, 26, AMBER)


@scene_node("sphere_mesh_3d", "SphereMesh3D", "Rendering")
def sphere_mesh_3d(ic):
    shadow(ic, 32, 56, 19, 6)
    sphere(ic, 32, 31, 21, AMBER)


@scene_node("plane_mesh_3d", "PlaneMesh3D", "Rendering")
def plane_mesh_3d(ic):
    shadow(ic, 32, 52, 29, 7)
    slab(ic, 32, 46, 42, 42, 6, shade(AMBER, -0.06))


@scene_node("light_3d", "Light3D", "Rendering")
def light_3d(ic):
    glow(ic, 32, 31, 30, SUN, 0.5)
    for i in range(8):
        angle = math.radians(i * 45 + 22.5)
        inner, outer, cx, cy = 17.5, 28, 32, 31
        tip = (cx + math.cos(angle) * outer, cy + math.sin(angle) * outer)
        side = math.radians(10)
        a = (cx + math.cos(angle - side) * inner, cy + math.sin(angle - side) * inner)
        b = (cx + math.cos(angle + side) * inner, cy + math.sin(angle + side) * inner)
        fill = ic.linear([(0, shade(SUN, 0.2)), (1, shade(SUN, -0.12))], 0, 0, 1, 1)
        ic.add(f'<polygon points="{pts([a, tip, b])}" fill="{fill}" stroke="{OUTLINE}" stroke-opacity="0.55" '
               f'stroke-width="1.1" stroke-linejoin="round"/>')
    sphere(ic, 32, 31, 14, SUN)


@scene_node("world_environment_3d", "WorldEnvironment3D", "Rendering")
def world_environment_3d(ic):
    shadow(ic, 32, 57, 27, 4)
    ground = ic.linear([(0, "#7cc45d"), (1, "#356e2a")], 0, 0, 0, 1)
    ic.add(f'<ellipse cx="32" cy="50" rx="28" ry="8" fill="{ground}" stroke="{OUTLINE}" stroke-opacity="0.7" stroke-width="1.4"/>')
    sky = ic.linear([(0, "#d3efff"), (0.6, SKY), (1, "#2d6fbe")], 0, 0, 0, 1)
    ic.add(f'<path d="M 8 50 A 24 42 0 0 1 56 50 A 24 5.5 0 0 1 8 50 Z" fill="{sky}" fill-opacity="0.92" '
           f'stroke="{OUTLINE}" stroke-opacity="0.7" stroke-width="1.4"/>')
    sphere(ic, 41, 22, 5.5, SUN, outline=False)
    for cx, cy, r in ((20, 29, 4.5), (25.5, 27, 5.5), (31, 30, 4)):
        ic.add(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="#ffffff" fill-opacity="0.95"/>')
    spec = ic.linear([(0, "#fff", 0.55), (1, "#fff", 0)], 0, 0, 1, 1)
    ic.add(f'<path d="M 13 42 A 21 37 0 0 1 26 13" fill="none" stroke="{spec}" stroke-width="2.6" stroke-linecap="round"/>')


@scene_node("reflection_probe_3d", "ReflectionProbe3D", "Rendering")
def reflection_probe_3d(ic):
    shadow(ic, 32, 59, 18, 3.5)
    for a, b in (((32, 40), (18, 58)), ((32, 40), (46, 58)), ((32, 40), (32, 60))):
        stroke_line(ic, [a, b], "#9aa6b5", 2.4)
    chrome = ic.linear([(0, "#ffffff"), (0.35, "#b9c7d6"), (0.5, "#44536a"), (0.53, "#8a6a4b"),
                        (0.8, "#d8c3a4"), (1, "#6e5a44")], 0, 0, 0, 1)
    ic.add(f'<circle cx="32" cy="24" r="19" fill="{chrome}"/>')
    edge = ic.radial([(0, "#000", 0), (0.7, "#000", 0.05), (1, "#000", 0.45)], cx=0.4, cy=0.38, r=0.68)
    ic.add(f'<circle cx="32" cy="24" r="19" fill="{edge}"/>')
    ic.add('<rect x="21" y="11" width="8" height="7" rx="1.5" fill="#fff" fill-opacity="0.9" transform="skewX(-12)"/>')
    ic.add(f'<circle cx="32" cy="24" r="19" fill="none" stroke="{OUTLINE}" stroke-opacity="0.7" stroke-width="1.4"/>')


@scene_node("decal_3d", "Decal3D", "Rendering")
def decal_3d(ic):
    shadow(ic, 32, 56, 27, 5)
    top, _, _ = slab(ic, 32, 51, 40, 40, 4, "#8c96a3")
    P = lambda x, z: iso(32, 51, x, 4, z)
    splat = [P(math.cos(math.radians(a)) * r, math.sin(math.radians(a)) * r)
             for a, r in zip(range(0, 360, 30), (13, 7, 12, 6.5, 13.5, 7, 12, 6, 13, 7.5, 11.5, 6.5))]
    fill = ic.linear([(0, shade(MAGENTA, 0.2)), (1, shade(MAGENTA, -0.15))], 0, 0, 1, 1)
    ic.add(f'<polygon points="{pts(splat)}" fill="{fill}" stroke="{OUTLINE}" stroke-opacity="0.5" stroke-width="1"/>')
    for corner in ((-13, -13), (13, -13), (13, 13), (-13, 13)):
        lx, ly = P(*corner)
        ux, uy = iso(32, 51, corner[0] * 0.55, 30, corner[1] * 0.55)
        ic.add(f'<line x1="{ux:.2f}" y1="{uy:.2f}" x2="{lx:.2f}" y2="{ly:.2f}" stroke="#ffd1f2" '
               f'stroke-opacity="0.85" stroke-width="1.3" stroke-dasharray="2.5 2"/>')
    sphere(ic, *iso(32, 51, 0, 30, 0), 4.5, MAGENTA)


@scene_node("fog_volume_3d", "FogVolume3D", "Rendering")
def fog_volume_3d(ic):
    shadow(ic, 32, 55, 26, 4, 0.25)
    for y, w, a in ((49, 44, 0.55), (43, 40, 0.75)):
        ic.add(f'<path d="M {32 - w / 2} {y} q {w / 8} -4 {w / 4} 0 t {w / 4} 0 t {w / 4} 0 t {w / 4} 0" fill="none" '
               f'stroke="#dfe7f1" stroke-opacity="{a}" stroke-width="3.2" stroke-linecap="round"/>')
    puffs = ((20, 31, 9), (31, 25, 12), (43, 30, 9.5), (32, 34, 9))
    for cx, cy, r in puffs:
        ic.add(f'<circle cx="{cx}" cy="{cy + 1.4}" r="{r + 1.4}" fill="{OUTLINE}" fill-opacity="0.55"/>')
    for cx, cy, r in puffs:
        fill = ic.radial([(0, "#ffffff"), (0.65, "#e6edf5"), (1, "#a9b8c9")], cx=0.4, cy=0.3, r=0.75)
        ic.add(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{fill}"/>')


# =============================================================================================
# Optimisation
# =============================================================================================

@scene_node("lod_group_3d", "LODGroup3D", "Optimization")
def lod_group_3d(ic):
    shadow(ic, 32, 54, 28, 4)
    sphere(ic, 13, 42, 9.5, SLATE)
    faceted_sphere(ic, 34, 39, 12, SLATE, detail=0, rot=(20, 25))
    faceted_sphere(ic, 52, 45, 7.5, SLATE, detail=-1, rot=(25, 35))


@scene_node("occluder_3d", "Occluder3D", "Optimization")
def occluder_3d(ic):
    shadow(ic, 32, 57, 22, 4.5)
    box(ic, 32, 53, 30, 36, 8, "#4d5f75")
    eye(ic, 30, 30, 24, SLATE, slashed=True)


@scene_node("visibility_region_3d", "VisibilityRegion3D", "Optimization")
def visibility_region_3d(ic):
    shadow(ic, 34, 57, 24, 4, 0.25)
    cone = ic.linear([(0, SLATE, 0.45), (1, SLATE, 0.08)], 0, 0, 1, 0)
    ic.add(f'<path d="M 17 32 L 60 12 V 52 Z" fill="{cone}"/>')
    for d in ("M 17 32 L 60 12", "M 17 32 L 60 52"):
        stroke_line(ic, [tuple(map(float, d.split()[1:3])), tuple(map(float, d.split()[4:6]))],
                    shade(SLATE, 0.2), 1.9, dash="4 2.5")
    ic.add(f'<ellipse cx="60" cy="32" rx="3.5" ry="20" fill="none" stroke="{shade(SLATE, 0.2)}" stroke-width="1.8"/>')
    eye(ic, 17, 32, 26, SLATE)


# =============================================================================================
# World
# =============================================================================================

@scene_node("level_streamer_3d", "LevelStreamer3D", "World")
def level_streamer_3d(ic):
    shadow(ic, 32, 59, 25, 4)
    slab(ic, 32, 55, 32, 32, 6, WORLD)
    # the level on its way in: a ghost of the tile, above
    glass_box(ic, 32, 30, 32, 6, 32, shade(WORLD, 0.2), fill_alpha=0.18, width=1.5)
    arrow(ic, (32, 14), (32, 44), "#ffffff", 4, 11)


@scene_node("world_partition_3d", "WorldPartition3D", "World")
def world_partition_3d(ic):
    shadow(ic, 32, 58, 28, 5)
    size, gap = 12, 1.8
    cells = [(i, j) for i in (-1, 0, 1) for j in (-1, 0, 1)]
    for i, j in sorted(cells, key=lambda c: c[0] + c[1]):
        raised = (i, j) == (1, 1)
        x, z = i * (size + gap), j * (size + gap)
        cx, cy = iso(32, 40, x, 0, z)
        slab(ic, cx, cy - (6 if raised else 0), size, size, 3.5,
             shade(WORLD, 0.18) if raised else shade(WORLD, -0.1))


# =============================================================================================
# Audio, effects
# =============================================================================================

@scene_node("audio_source_3d", "AudioSource3D", "Audio")
def audio_source_3d(ic):
    shadow(ic, 26, 57, 18, 4.5)
    box(ic, 22, 53, 16, 32, 20, "#3b4454")
    cone = ic.radial([(0, "#ffd4ea"), (0.35, PINK), (1, shade(PINK, -0.3))], cx=0.45, cy=0.4)
    cx, cy = iso(22, 53, 0, 16, 10)
    ic.add(f'<ellipse cx="{cx - 1:.2f}" cy="{cy:.2f}" rx="6.5" ry="8.5" fill="{cone}" stroke="{OUTLINE}" '
           f'stroke-opacity="0.7" stroke-width="1.2" transform="skewY(-14 )"/>')
    for i, r in enumerate((9, 15, 21)):
        ic.add(f'<path d="M {36 + i * 1} {22 - r * 0.2:.1f} a {r} {r} 0 0 1 0 {r * 1.25:.1f}" fill="none" '
               f'stroke="{OUTLINE}" stroke-opacity="0.55" stroke-width="5" stroke-linecap="round" transform="translate({i * 3} {8 - i * 2})"/>')
        ic.add(f'<path d="M {36 + i * 1} {22 - r * 0.2:.1f} a {r} {r} 0 0 1 0 {r * 1.25:.1f}" fill="none" '
               f'stroke="{shade(PINK, 0.12)}" stroke-width="3.2" stroke-linecap="round" transform="translate({i * 3} {8 - i * 2})"/>')


@scene_node("particle_emitter_3d", "ParticleEmitter3D", "VFX")
def particle_emitter_3d(ic):
    shadow(ic, 32, 59, 16, 3.5)
    particles = ((32, 42, 3.4), (29, 33, 3.8), (35, 25, 4.4), (26, 18, 4.2), (40, 14, 3.6), (19, 27, 3.2),
                 (46, 26, 3.4), (14, 12, 2.6), (50, 8, 2.8), (33, 7, 3.2))
    for cx, cy, r in particles:
        sphere(ic, cx, cy, r, MAGENTA, gloss=0.8)
    cylinder(ic, 32, 57, 12, 10, "#4a5566", ry=5)
    ic.add(f'<ellipse cx="32" cy="47" rx="7" ry="2.8" fill="{shade(MAGENTA, 0.25)}" stroke="{OUTLINE}" '
           f'stroke-opacity="0.5" stroke-width="1"/>')


# =============================================================================================
# UI
# =============================================================================================

def panel(ic, x, y, w, h, base, r=6, inner=None):
    top_c, _, dark_c, edge_c = tones(base)
    ic.add(f'<rect x="{x + 2.5}" y="{y + 3}" width="{w}" height="{h}" rx="{r}" fill="{shade(dark_c, -0.12)}"/>')
    fill = ic.linear([(0, shade(top_c, 0.1)), (0.6, base), (1, shade(dark_c, -0.04))], 0, 0, 0.3, 1)
    ic.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{fill}" stroke="{OUTLINE}" '
           f'stroke-opacity="0.72" stroke-width="1.5"/>')
    ic.add(f'<path d="M {x + r} {y + 1.4} h {w - 2 * r}" stroke="{edge_c}" stroke-opacity="0.9" stroke-width="1.3" stroke-linecap="round"/>')
    if inner:
        ix, iy, iw, ih, ic_col = inner
        infill = ic.linear([(0, shade(ic_col, -0.08)), (1, shade(ic_col, 0.06))], 0, 0, 0, 1)
        ic.add(f'<rect x="{ix}" y="{iy}" width="{iw}" height="{ih}" rx="{max(1.5, r - 3)}" fill="{infill}" '
               f'stroke="{OUTLINE}" stroke-opacity="0.45" stroke-width="1"/>')


@scene_node("canvas", "Canvas", "UI")
def canvas_node(ic):
    panel(ic, 6, 9, 50, 44, LIME, 7, (11, 20, 40, 28, "#26331b"))
    for i, (w, c) in enumerate(((22, "#cdf59a"), (30, "#89c152"), (16, "#89c152"))):
        ic.add(f'<rect x="15" y="{25 + i * 7}" width="{w}" height="3.2" rx="1.6" fill="{c}"/>')
    for i in range(3):
        ic.add(f'<circle cx="{14 + i * 5}" cy="15" r="1.6" fill="#26331b" fill-opacity="0.6"/>')


@scene_node("ui_text", "UIText", "UI")
def ui_text(ic):
    shadow(ic, 32, 57, 20, 4.5)
    box(ic, 32, 50, 9, 30, 9, LIME)
    box(ic, 32, 25, 34, 8, 9, shade(LIME, 0.05))


@scene_node("ui_image", "UIImage", "UI")
def ui_image(ic):
    panel(ic, 6, 10, 50, 42, LIME, 5, (11, 15, 40, 32, "#9ed2ff"))
    ic.add('<clipPath id="pic"><rect x="11" y="15" width="40" height="32" rx="2"/></clipPath>')
    hill = ic.linear([(0, "#6fbf4d"), (1, "#2f6a24")], 0, 0, 0, 1)
    far = ic.linear([(0, "#9bb7c9"), (1, "#5d7890")], 0, 0, 0, 1)
    ic.add(f'<g clip-path="url(#pic)"><path d="M 11 40 L 24 27 L 33 36 L 40 30 L 51 40 V 47 H 11 Z" fill="{far}"/>'
           f'<path d="M 11 44 Q 25 34 36 41 T 51 40 V 47 H 11 Z" fill="{hill}"/>'
           f'<circle cx="41" cy="22" r="4" fill="#fff3a6"/></g>')


@scene_node("ui_button", "UIButton", "UI")
def ui_button(ic):
    shadow(ic, 30, 50, 25, 5)
    ic.add(f'<rect x="6" y="24" width="48" height="22" rx="11" fill="{shade(LIME, -0.32)}"/>')
    fill = ic.linear([(0, shade(LIME, 0.22)), (0.55, LIME), (1, shade(LIME, -0.12))], 0, 0, 0, 1)
    ic.add(f'<rect x="6" y="18" width="48" height="22" rx="11" fill="{fill}" stroke="{OUTLINE}" '
           f'stroke-opacity="0.75" stroke-width="1.5"/>')
    ic.add(f'<rect x="10" y="20.5" width="40" height="7" rx="3.5" fill="#ffffff" fill-opacity="0.35"/>')
    ic.add('<rect x="18" y="27" width="20" height="4" rx="2" fill="#2a3a18" fill-opacity="0.75"/>')
    cursor = "M 40 30 L 40 55 L 46 49 L 50 58 L 54 56 L 50 47.5 L 58 47.5 Z"
    ic.add(f'<path d="{cursor}" fill="#ffffff" stroke="{OUTLINE}" stroke-opacity="0.85" stroke-width="1.6" '
           f'stroke-linejoin="round"/>')


# =============================================================================================
# Node palette categories: a bevelled tile in the family colour with a white glyph
# =============================================================================================

CATEGORIES = [
    ("Scene", STEEL, "M 31 18 V 26 M 31 26 L 19 38 M 31 26 L 43 38", [(31, 16, 5), (19, 40, 4.5), (43, 40, 4.5)]),
    ("Physics", BLUE, "M 14 26 H 22 M 12 33 H 21 M 16 40 H 23", [(35, 32, 10)]),
    ("Navigation", NAV, "M 20 47 V 15 M 20 17 H 42 L 36 24.5 L 42 32 H 20", []),
    ("Animation", PURPLE, "M 24 18 L 43 31 L 24 44 Z", []),
    ("Camera", TEAL, "M 13 22 H 37 V 42 H 13 Z M 37 28 L 48 22 V 42 L 37 36", [(25, 32, 5)]),
    ("Combat", RED, "M 31 13 V 22 M 31 40 V 49 M 13 31 H 22 M 40 31 H 49", [(31, 31, 3)]),
    ("Gameplay", GOLD, None, []),
    ("Rendering", AMBER, "M 31 14 L 46 22.5 V 39.5 L 31 48 L 16 39.5 V 22.5 Z M 16 22.5 L 31 31 L 46 22.5 M 31 31 V 48", []),
    ("Optimization", SLATE, "M 14 40 A 17 17 0 0 1 48 40 M 31 40 L 41 27", [(31, 40, 3.2)]),
    ("World", WORLD, "M 31 14 A 17 17 0 1 0 31.01 14 Z M 14 31 H 48 M 31 14 C 22 22 22 40 31 48 M 31 14 C 40 22 40 40 31 48", []),
    ("Audio", PINK, "M 14 26 H 21 L 29 18 V 44 L 21 36 H 14 Z M 35 24 Q 40 31 35 38 M 40 19 Q 48 31 40 43", []),
    ("VFX", MAGENTA, None, []),
    ("Logic", CODE, "M 25 16 Q 19 16 19 22 V 27 Q 19 31 15 31 Q 19 31 19 35 V 40 Q 19 46 25 46 "
                    "M 37 16 Q 43 16 43 22 V 27 Q 43 31 47 31 Q 43 31 43 35 V 40 Q 43 46 37 46", []),
    ("UI", LIME, "M 13 16 H 49 V 42 H 13 Z M 13 23 H 49", [(17, 19.5, 1.2), (21, 19.5, 1.2)]),
]


def _category(name, colour, path, dots):
    def draw(ic):
        tile(ic, colour)
        if name == "Gameplay":
            glyph(ic, "M " + " L ".join(f"{x:.1f} {y:.1f}" for x, y in star(31, 32, 16, 7, 5)) + " Z",
                  width=2.2, fill="#ffffff")
        elif name == "VFX":
            glyph(ic, "M " + " L ".join(f"{x:.1f} {y:.1f}" for x, y in star(28, 34, 15, 4.5, 4)) + " Z",
                  width=2, fill="#ffffff")
            glyph(ic, "M " + " L ".join(f"{x:.1f} {y:.1f}" for x, y in star(44, 19, 7, 2.2, 4)) + " Z",
                  width=1.6, fill="#ffffff")
        elif name == "Animation":
            glyph(ic, path, width=2.6, fill="#ffffff")
        else:
            glyph(ic, path)
        for cx, cy, r in dots:
            ic.add(f'<circle cx="{cx + 1}" cy="{cy + 1.4}" r="{r}" fill="{OUTLINE}" fill-opacity="0.35"/>')
            ic.add(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="#ffffff"/>')
    return draw


for _name, _colour, _path, _dots in CATEGORIES:
    _register(NODE_CATEGORIES, _name.lower(), _name, "Node categories")(_category(_name, _colour, _path, _dots))


# =============================================================================================
# Content browser assets
# =============================================================================================

def folder_back(ic):
    back_c = shade(GOLD, -0.08)
    back = ic.linear([(0, shade(back_c, 0.05)), (1, shade(back_c, -0.12))], 0, 0, 0, 1)
    ic.add(f'<path d="M 7 12 h 17 l 5 5 h 28 a 3 3 0 0 1 3 3 v 30 a 3 3 0 0 1 -3 3 H 7 a 3 3 0 0 1 -3 -3 V 15 '
           f'a 3 3 0 0 1 3 -3 Z" fill="{back}" stroke="{OUTLINE}" stroke-opacity="0.75" stroke-width="1.5" '
           f'stroke-linejoin="round"/>')
    ic.add('<rect x="11" y="19" width="40" height="18" rx="1.5" fill="#f4f6f9" stroke="#0b0f15" '
           'stroke-opacity="0.35" stroke-width="1"/>')
    ic.add('<rect x="14" y="23" width="22" height="2" rx="1" fill="#c3ccd8"/>')


@content_type("folder", "Folder")
def folder_asset(ic):
    shadow(ic, 32, 56, 27, 4.5)
    folder_back(ic)
    front = ic.linear([(0, shade(GOLD, 0.2)), (0.55, GOLD), (1, shade(GOLD, -0.14))], 0, 0, 0, 1)
    ic.add(f'<path d="M 4.5 27 a 3 3 0 0 1 3 -3 h 50 a 3 3 0 0 1 3 3 l -2 23 a 3 3 0 0 1 -3 3 H 8.5 '
           f'a 3 3 0 0 1 -3 -3 Z" fill="{front}" stroke="{OUTLINE}" stroke-opacity="0.75" stroke-width="1.5" '
           f'stroke-linejoin="round"/>')
    ic.add(f'<path d="M 7.5 26.5 h 50" stroke="{shade(GOLD, 0.34)}" stroke-width="1.3" stroke-linecap="round"/>')


@scene_node("folder", "Folder", "Scene")
def folder_node(ic):
    # The Scene panel's Folder looks like the Content Browser's: the same thing, organising.
    folder_asset(ic)


@content_type("folder_open", "Folder (open)")
def folder_open_asset(ic):
    shadow(ic, 32, 56, 27, 4.5)
    folder_back(ic)
    front = ic.linear([(0, shade(GOLD, 0.24)), (0.55, GOLD), (1, shade(GOLD, -0.14))], 0, 0, 0, 1)
    ic.add(f'<path d="M 12 31 a 3 3 0 0 1 3 -2.5 h 45 a 2.5 2.5 0 0 1 2.4 3.2 l -6 19 a 3 3 0 0 1 -3 2.3 H 7 '
           f'a 2.5 2.5 0 0 1 -2.4 -3.2 Z" fill="{front}" stroke="{OUTLINE}" stroke-opacity="0.75" stroke-width="1.5" '
           f'stroke-linejoin="round"/>')
    ic.add(f'<path d="M 15 31 h 44" stroke="{shade(GOLD, 0.36)}" stroke-width="1.3" stroke-linecap="round"/>')


@content_type("scene", "Scene")
def scene_asset(ic):
    shadow(ic, 32, 57, 28, 5)
    slab(ic, 32, 52, 44, 44, 5, shade(STEEL, -0.1))
    shadow(ic, 22, 40, 8, 3, 0.35)
    box(ic, 21, 40, 12, 12, 12, AMBER)
    shadow(ic, 42, 42, 7, 2.5, 0.35)
    sphere(ic, 42, 35, 7.5, BLUE)
    cone = ic.linear([(0, shade(NAV, 0.2)), (0.6, NAV), (1, shade(NAV, -0.2))], 0, 0, 1, 0)
    ic.add(f'<path d="M 32 13 L 40 31 A 8 3.5 0 0 1 24 31 Z" fill="{cone}" stroke="{OUTLINE}" '
           f'stroke-opacity="0.7" stroke-width="1.3" stroke-linejoin="round"/>')


@content_type("prefab", "Prefab")
def prefab_asset(ic):
    prefab = "#4aa3ff"
    shadow(ic, 34, 57, 23, 5)
    glass_box(ic, 26, 40, 22, 22, 22, prefab, fill_alpha=0.12, width=1.6)
    box(ic, 37, 53, 22, 22, 22, prefab)


@content_type("entity", "Entity")
def entity_asset(ic):
    # An entity asset is a ready-made node tree - drawn as the character body it usually holds.
    character_body_3d(ic)


@content_type("bundle", "Bundle")
def bundle_asset(ic):
    card = "#c99a5c"
    shadow(ic, 32, 57, 26, 6)
    top, left, right = box(ic, 32, 51, 30, 22, 30, card)
    P = lambda x, z: iso(32, 51, x, 22, z)
    tape = [P(-3.5, -15), P(3.5, -15), P(3.5, 15), P(-3.5, 15)]
    ic.add(f'<polygon points="{pts(tape)}" fill="#f0dcb4" fill-opacity="0.95"/>')
    side = [iso(32, 51, -3.5, 22, 15), iso(32, 51, 3.5, 22, 15), iso(32, 51, 3.5, 12, 15), iso(32, 51, -3.5, 12, 15)]
    ic.add(f'<polygon points="{pts(side)}" fill="#e2c894"/>')


@content_type("mesh", "Mesh")
def mesh_asset(ic):
    shadow(ic, 32, 57, 20, 5)
    faceted_sphere(ic, 32, 31, 22, AMBER, detail=0, rot=(14, 22), wire="#fff4e0")


@content_type("model", "Model")
def model_asset(ic):
    shadow(ic, 32, 58, 16, 3.5)
    figure(ic, "stand", "#a9b6c6", joints=PURPLE)


@content_type("texture", "Texture")
def texture_asset(ic):
    shadow(ic, 33, 57, 26, 4)
    ic.add(f'<rect x="7.5" y="10.5" width="50" height="44" rx="3" fill="#6a7a8e"/>')
    ic.add(f'<rect x="5" y="8" width="50" height="44" rx="3" fill="#f3f5f8" stroke="{OUTLINE}" '
           f'stroke-opacity="0.75" stroke-width="1.5"/>')
    ic.add('<clipPath id="tex"><rect x="9" y="12" width="42" height="36" rx="1.5"/></clipPath>')
    sky = ic.linear([(0, "#7fc4ff"), (1, "#d7eeff")], 0, 0, 0, 1)
    hill = ic.linear([(0, "#6fbf4d"), (1, "#2f6a24")], 0, 0, 0, 1)
    far = ic.linear([(0, "#9bb7c9"), (1, "#5d7890")], 0, 0, 0, 1)
    checker = "".join(f'<rect x="{9 + i * 6}" y="{12 + j * 6}" width="6" height="6" fill="#c7cfd9"/>'
                      for i in range(7) for j in range(6) if (i + j) % 2 == 0 and i + j < 5)
    ic.add(f'<g clip-path="url(#tex)"><rect x="9" y="12" width="42" height="36" fill="{sky}"/>'
           f'<path d="M 9 40 L 22 26 L 31 35 L 38 29 L 51 40 V 48 H 9 Z" fill="{far}"/>'
           f'<path d="M 9 44 Q 24 34 35 41 T 51 40 V 48 H 9 Z" fill="{hill}"/>'
           f'<circle cx="41" cy="20" r="4.5" fill="#fff3a6"/>{checker}</g>')


@content_type("shader", "Shader")
def shader_asset(ic):
    shadow(ic, 32, 57, 19, 5)
    orb = ic.linear([(0, "#ff7ad9"), (0.35, "#8f6bff"), (0.7, "#35c8ff"), (1, "#3be3a0")], 0, 0, 1, 1)
    ic.add(f'<circle cx="32" cy="31" r="21" fill="{orb}"/>')
    edge = ic.radial([(0, "#fff", 0.35), (0.5, "#fff", 0), (0.8, "#000", 0.08), (1, "#000", 0.45)],
                     cx=0.38, cy=0.32, r=0.72)
    ic.add(f'<circle cx="32" cy="31" r="21" fill="{edge}"/>')
    ic.add('<path d="M 17 33 Q 24.5 24 32 33 T 47 33" fill="none" stroke="#ffffff" stroke-width="3" '
           'stroke-linecap="round" stroke-opacity="0.9"/>')
    spec = ic.radial([(0, "#fff", 0.9), (1, "#fff", 0)])
    ic.add(f'<ellipse cx="24" cy="20" rx="8" ry="5" fill="{spec}" transform="rotate(-30 24 20)"/>')
    ic.add(f'<circle cx="32" cy="31" r="21" fill="none" stroke="{OUTLINE}" stroke-opacity="0.7" stroke-width="1.4"/>')


@content_type("material", "Material")
def material_asset(ic):
    shadow(ic, 32, 58, 27, 4)
    top, _, _ = slab(ic, 32, 54, 40, 40, 4, "#59636f")
    for i in range(4):
        for j in range(4):
            if (i + j) % 2:
                continue
            x0, z0 = -20 + i * 10, -20 + j * 10
            quad = [iso(32, 54, x0, 4, z0), iso(32, 54, x0 + 10, 4, z0), iso(32, 54, x0 + 10, 4, z0 + 10),
                    iso(32, 54, x0, 4, z0 + 10)]
            ic.add(f'<polygon points="{pts(quad)}" fill="#c9d1db" fill-opacity="0.85"/>')
    shadow(ic, 33, 44, 14, 4, 0.5)
    sphere(ic, 32, 28, 17, "#e5433b")


@content_type("save", "Save")
def save_asset(ic):
    disk = "#3c6fd8"
    shadow(ic, 33, 58, 25, 4)
    ic.add(f'<rect x="9.5" y="9.5" width="46" height="46" rx="5" fill="{shade(disk, -0.3)}"/>')
    fill = ic.linear([(0, shade(disk, 0.15)), (1, shade(disk, -0.1))], 0, 0, 0, 1)
    ic.add(f'<path d="M 12 7 h 34 l 9 9 v 34 a 5 5 0 0 1 -5 5 H 12 a 5 5 0 0 1 -5 -5 V 12 a 5 5 0 0 1 5 -5 Z" '
           f'fill="{fill}" stroke="{OUTLINE}" stroke-opacity="0.75" stroke-width="1.5" stroke-linejoin="round"/>')
    metal = ic.linear([(0, "#f1f4f8"), (0.5, "#b8c3d0"), (1, "#8894a3")], 0, 0, 1, 0)
    ic.add(f'<rect x="17" y="7.5" width="24" height="15" rx="1.5" fill="{metal}" stroke="{OUTLINE}" '
           f'stroke-opacity="0.55" stroke-width="1"/>')
    ic.add(f'<rect x="32" y="10" width="5.5" height="10" rx="1" fill="{shade(disk, -0.35)}"/>')
    ic.add(f'<rect x="13" y="30" width="36" height="21" rx="2" fill="#f4f6f9" stroke="{OUTLINE}" '
           f'stroke-opacity="0.45" stroke-width="1"/>')
    for y in (36, 41, 46):
        ic.add(f'<rect x="17" y="{y}" width="28" height="1.8" rx="0.9" fill="#b8c3d0"/>')


@content_type("file", "File")
def file_asset(ic):
    shadow(ic, 33, 58, 21, 4)
    page(ic, 12, 6, 38, 48, "#b9c4d2", fold=11)
    for y, w in ((24, 24), (31, 26), (38, 20), (45, 24)):
        ic.add(f'<rect x="18" y="{y}" width="{w}" height="2.6" rx="1.3" fill="#6f7d8f" fill-opacity="0.8"/>')


@content_type("script", "Script file")
def script_file_asset(ic):
    shadow(ic, 33, 58, 21, 4)
    page(ic, 12, 6, 38, 48, "#dfe6ef", fold=11)
    lines = ((17, 21, 10, "#c678dd"), (29, 21, 12, "#5b8cff"), (21, 28, 16, "#98c379"), (21, 35, 9, "#e5c07b"),
             (32, 35, 10, "#5b8cff"), (21, 42, 18, "#98c379"), (17, 49, 6, "#c678dd"))
    for x, y, w, colour in lines:
        ic.add(f'<rect x="{x}" y="{y}" width="{w}" height="3" rx="1.5" fill="{colour}"/>')


@content_type("audio", "Audio")
def audio_asset(ic):
    shadow(ic, 32, 57, 22, 4)
    beam = ic.linear([(0, shade(PINK, 0.2)), (1, shade(PINK, -0.15))], 0, 0, 1, 1)
    ic.add(f'<path d="M 22 16 L 48 10 V 17 L 22 23 Z" fill="{beam}" stroke="{OUTLINE}" stroke-opacity="0.75" '
           f'stroke-width="1.4" stroke-linejoin="round"/>')
    for x in (22, 48):
        ic.add(f'<rect x="{x - 2}" y="{17 if x == 22 else 11}" width="4" height="{27 if x == 22 else 27}" '
               f'fill="{shade(PINK, -0.1)}" stroke="{OUTLINE}" stroke-opacity="0.7" stroke-width="1.2"/>')
    for cx, cy in ((15.5, 45), (41.5, 39)):
        note = ic.radial([(0, shade(PINK, 0.35)), (0.6, PINK), (1, shade(PINK, -0.28))], cx=0.35, cy=0.3, r=0.75)
        ic.add(f'<ellipse cx="{cx}" cy="{cy}" rx="8" ry="6" fill="{note}" stroke="{OUTLINE}" stroke-opacity="0.75" '
               f'stroke-width="1.4" transform="rotate(-20 {cx} {cy})"/>')
        ic.add(f'<ellipse cx="{cx - 2.5}" cy="{cy - 2}" rx="2.6" ry="1.5" fill="#fff" fill-opacity="0.75" '
               f'transform="rotate(-20 {cx} {cy})"/>')


@content_type("animation", "Animation")
def animation_asset(ic):
    shadow(ic, 32, 57, 26, 4)
    panel_fill = ic.linear([(0, "#3f3a4d"), (1, "#2b2735")], 0, 0, 0, 1)
    ic.add(f'<rect x="5" y="12" width="54" height="40" rx="6" fill="{panel_fill}" stroke="{OUTLINE}" '
           f'stroke-opacity="0.8" stroke-width="1.5"/>')
    ic.add('<path d="M 10 42 C 20 42 22 22 32 22 S 44 38 54 20" fill="none" stroke="#d8c6ff" stroke-width="2.4" '
           'stroke-linecap="round"/>')
    ic.add('<rect x="9" y="47" width="46" height="2" rx="1" fill="#6b6280"/>')
    for x, y in ((10, 42), (32, 22), (54, 20)):
        key = ic.linear([(0, shade(PURPLE, 0.3)), (1, shade(PURPLE, -0.15))], 0, 0, 1, 1)
        ic.add(f'<rect x="{x - 4.5}" y="{y - 4.5}" width="9" height="9" rx="1.5" fill="{key}" stroke="#fff" '
               f'stroke-width="1.2" transform="rotate(45 {x} {y})"/>')


@content_type("font", "Font")
def font_asset(ic):
    shadow(ic, 32, 58, 24, 4)
    glyph_colour = "#e9edf2"
    outline = "M 8 54 L 26 8 H 38 L 56 54 H 45 L 41 43 H 23 L 19 54 Z M 26 34 H 38 L 32 17 Z"
    ic.add(f'<path d="{outline}" transform="translate(3 3)" fill="#6b7788" fill-rule="evenodd"/>')
    fill = ic.linear([(0, "#ffffff"), (0.6, glyph_colour), (1, "#aab5c3")], 0, 0, 0, 1)
    ic.add(f'<path d="{outline}" fill="{fill}" fill-rule="evenodd" stroke="{OUTLINE}" stroke-opacity="0.8" '
           f'stroke-width="1.5" stroke-linejoin="round"/>')


@content_type("input_map", "Input map")
def input_map_asset(ic):
    pad = "#3d4553"
    shadow(ic, 32, 56, 27, 4)
    body = "M 14 20 H 50 C 58 20 61 36 60 46 C 59 53 52 55 47 49 L 42 43 H 22 L 17 49 C 12 55 5 53 4 46 C 3 36 6 20 14 20 Z"
    ic.add(f'<path d="{body}" transform="translate(0 3)" fill="{shade(pad, -0.3)}"/>')
    fill = ic.linear([(0, shade(pad, 0.2)), (0.5, pad), (1, shade(pad, -0.15))], 0, 0, 0, 1)
    ic.add(f'<path d="{body}" fill="{fill}" stroke="{OUTLINE}" stroke-opacity="0.8" stroke-width="1.5" stroke-linejoin="round"/>')
    ic.add('<path d="M 12 32 h 10 M 17 27 v 10" stroke="#c8d0da" stroke-width="3.6" stroke-linecap="round"/>')
    for (cx, cy), colour in (((46, 27), "#f2c14e"), ((51, 32), "#e8484f"), ((41, 32), "#4d8ef0"), ((46, 37), "#5cc84a")):
        sphere(ic, cx, cy, 3.1, colour, outline=False)


@content_type("project_settings", "Project settings")
def project_settings_asset(ic):
    shadow(ic, 32, 58, 22, 4)
    teeth = []
    for i in range(16):
        r = 25 if i % 2 == 0 else 19.5
        a = math.radians(i * 22.5 - 90 + 11.25)
        teeth.append((32 + math.cos(a) * r, 31 + math.sin(a) * r))
    gear = ic.linear([(0, shade(STEEL, 0.28)), (0.55, STEEL), (1, shade(STEEL, -0.25))], 0, 0, 1, 1)
    ic.add(f'<polygon points="{pts([(x + 2, y + 2.5) for x, y in teeth])}" fill="{shade(STEEL, -0.35)}"/>')
    ic.add(f'<polygon points="{pts(teeth)}" fill="{gear}" stroke="{OUTLINE}" stroke-opacity="0.75" stroke-width="1.5" '
           f'stroke-linejoin="round"/>')
    hole = ic.radial([(0, "#1d232b"), (1, "#3b4552")], cx=0.6, cy=0.6)
    ic.add(f'<circle cx="32" cy="31" r="8" fill="{hole}" stroke="{OUTLINE}" stroke-opacity="0.7" stroke-width="1.3"/>')
    ic.add(f'<path d="M 18 22 A 16 16 0 0 1 30 15" fill="none" stroke="#fff" stroke-opacity="0.5" stroke-width="2" '
           f'stroke-linecap="round"/>')
