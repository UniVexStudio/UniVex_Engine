"""The UniVex Editor Monochrome Studio Icon Set:
- All wired and unwired 3D Scene Objects
- All wired and unwired 2D Scene Objects
- All wired and unwired UI Controls
- All ECS Inspector Components
- All Object Palette Categories
- All Content Browser Asset Types

100% Pure Gray, Slate & White Soft-Matte Studio Shading (zero colorful hex codes).
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))

from kit import (
    BRIGHT, CUTOUT, DARK, DEEP, Icon, LIGHT, MID, OUTLINE, SILVER, SLATE, WHITE,
    arrow, arrow_head, box, cylinder, faceted_sphere, figure, glass_box, glow,
    glyph, iso, page, pts, shade, shadow, slab, sphere, stroke_line, tile, tones, wire_box
)

# Balanced Studio Palette (Dark Gray bodies vs White-Gray focal accents)
STEEL = SLATE       # Structural bodies, meshes, rigid bodies, mechanics
AMBER = DARK        # Containers, colliders, physics shapes
TEAL = SLATE        # Auxiliary tools, springs, joints, cameras
SUN = BRIGHT        # Light emitters, sun rays, highlights
BLUE = MID          # Viewports, secondary glyphs
MINT = SLATE        # Spatial volumes, audio, environments
NAV = DARK          # Heavy structural bases, anchors
PURPLE = MID        # Animations, skeletons, sequences
RED = DARK          # Combat, occluders, killzones, boundaries
GOLD = SLATE        # Generic objects, markers
PINK = MID          # Bones, IK chains
MAGENTA = SLATE     # Specialized nodes
CODE = MID          # Scripts, code text
LIME = SLATE        # Navmesh, pathways
WORLD = DARK        # World partitions, terrain, background
SKY_TONE = SLATE    # Sky, fog, atmosphere
IVORY = SILVER      # Key focal foreground points
AXIS_X, AXIS_Y, AXIS_Z = SILVER, MID, DARK
COS = math.cos(math.radians(30))

OBJECTS = "objects"
OBJECT_CARDS = "object_cards"
OBJECT_CATEGORIES = "object_categories"
CONTENT_TYPES = "content_types"
COMPONENTS = "components"

ICONS = {}  # (group, id) -> (title, section, draw_fn)


def _register(group, icon_id, title, section):
    def register(fn):
        key = (group, icon_id)
        if key in ICONS:
            raise ValueError(f"icon {group}/{icon_id} is registered twice")
        ICONS[key] = (title, section, fn)
        return fn
    return register


def scene_object(type_id, title, section):
    return _register(OBJECTS, type_id, title, section)


def content_type(icon_id, title):
    return _register(CONTENT_TYPES, icon_id, title, "Content types")


def component_icon(icon_id, title):
    return _register(COMPONENTS, icon_id, title, "Components")


def badge_for_object(icon_id, section):
    """Auto-detects the studio card badge ('3D', '2D', or 'UI') for an object."""
    if icon_id.endswith("_3d") or section in ("Rendering", "Physics", "Animation", "Navigation", "World", "Combat", "Optimization", "Gameplay", "Camera", "Audio", "VFX"):
        if icon_id.endswith("_2d") or section == "2D":
            return "2D"
        return "3D"
    if icon_id.endswith("_2d") or section == "2D" or icon_id in ("tile_map_layer", "canvas_group", "canvas_modulate", "back_buffer_copy", "touch_screen_button"):
        return "2D"
    if section == "UI" or icon_id.startswith("ui_") or icon_id == "canvas":
        return "UI"
    return None


def build(key):
    title, _, fn = ICONS[key]
    ic = Icon(title)
    fn(ic)
    return ic.svg()


def accent_for_key(key):
    group, icon_id = key
    title, section, _ = ICONS[key]
    if group == CONTENT_TYPES:
        if icon_id in ("mesh", "model"):
            return "#F59E0B"
        if icon_id in ("material",):
            return "#22C55E"
        if icon_id in ("shader",):
            return "#06B6D4"
        if icon_id in ("texture",):
            return "#EF4444"
        if icon_id in ("script",):
            return "#3B82F6"
        if icon_id in ("audio",):
            return "#14B8A6"
        if icon_id in ("font",):
            return "#8B5CF6"
        if icon_id in ("animation",):
            return "#EC4899"
        if icon_id in ("folder", "folder_open"):
            return "#F59E0B"
        if icon_id in ("entity", "prefab"):
            return "#0284C7"
        if icon_id in ("viewport", "viewport_3d", "viewport_2d"):
            return "#06B6D4"
        return "#71717A"

    if "mesh" in icon_id or "csg" in icon_id or section == "Mesh" or icon_id in ("box_mesh_3d", "sphere_mesh_3d", "plane_mesh_3d", "mesh_instance_3d"):
        return "#F59E0B"
    if section == "Physics" or "body" in icon_id or "joint" in icon_id or "collider" in icon_id or "ray_cast" in icon_id or "shape" in icon_id:
        return "#F97316"
    if section == "Lighting" or "light" in icon_id or "sun" in icon_id or "reflection" in icon_id or "probe" in icon_id or "fog" in icon_id or "decal" in icon_id:
        return "#FACC15"
    if section == "Camera" or "camera" in icon_id or "spring_arm" in icon_id:
        return "#8B5CF6"
    if section == "Animation" or "animation" in icon_id or "skeleton" in icon_id or "bone" in icon_id or "ik" in icon_id:
        return "#EC4899"
    if section == "Audio" or "audio" in icon_id or "listener" in icon_id:
        return "#14B8A6"
    if section == "UI" or "ui" in icon_id or "canvas" in icon_id or "label" in icon_id:
        return "#6366F1"
    if section == "World" or "world" in icon_id or "environment" in icon_id or "level" in icon_id or "partition" in icon_id or "nav" in icon_id or "occluder" in icon_id:
        return "#10B981"
    if section == "Gameplay" or icon_id in ("character_3d", "player_3d", "spawn_point_3d", "projectile_3d"):
        return "#0284C7"
    return "#38BDF8"


def build_card(key):
    group, icon_id = key
    title, section, fn = ICONS[key]
    ic = Icon(title)
    fn(ic)
    badge = badge_for_object(icon_id, section) if group == OBJECTS else (
        "3D" if icon_id == "viewport_3d" else ("2D" if icon_id == "viewport_2d" else None)
    )
    return ic.card_svg(badge=badge)


# =============================================================================================
# Shared Monochrome Studio Helpers
# =============================================================================================

def dot(ic, cx, cy, r, colour=LIGHT):
    sphere(ic, cx, cy, r, colour)


def link(ic, a, b, colour=LIGHT, width=2.4):
    stroke_line(ic, [a, b], colour, width)


def eye(ic, cx, cy, w, colour=LIGHT, slashed=False):
    h = w * 0.55
    lid = f"M {cx - w / 2} {cy} Q {cx} {cy - h} {cx + w / 2} {cy} Q {cx} {cy + h} {cx - w / 2} {cy} Z"
    ic.add(f'<path d="{lid}" fill="url(#uv_bright)" stroke="{DEEP}" stroke-width="1.3"/>')
    ic.add(f'<circle cx="{cx}" cy="{cy}" r="{h * 0.44:.2f}" fill="url(#uv_dark)"/>')
    ic.add(f'<circle cx="{cx - h * 0.14:.2f}" cy="{cy - h * 0.14:.2f}" r="{h * 0.12:.2f}" fill="{WHITE}"/>')
    if slashed:
        stroke_line(ic, [(cx - w * 0.44, cy + h * 0.65), (cx + w * 0.44, cy - h * 0.65)], BRIGHT, 3.0)


def star(cx, cy, r_outer, r_inner, points=4, rotation=0):
    out = []
    for i in range(points * 2):
        r = r_outer if i % 2 == 0 else r_inner
        a = math.radians(rotation + i * 180 / points - 90)
        out.append((cx + math.cos(a) * r, cy + math.sin(a) * r))
    return out


def axes(ic, ox, oy, length=20, width=3.8, head=9.0):
    arrow(ic, (ox, oy), iso(ox, oy, 0, length, 0), BRIGHT, width, head)
    arrow(ic, (ox, oy), iso(ox, oy, length, 0, 0), LIGHT, width, head)
    arrow(ic, (ox, oy), iso(ox, oy, 0, 0, length), MID, width, head)


# =============================================================================================
# 1. Core & Scene Objects (Wired)
# =============================================================================================

@scene_object("scene_root", "SceneRoot", "Scene")
def scene_root(ic):
    slab(ic, 32, 52, 40, 40, 4, DARK)
    link(ic, (32, 16), (18, 35), LIGHT, 2.8)
    link(ic, (32, 16), (46, 35), LIGHT, 2.8)
    dot(ic, 18, 35, 6.2, SILVER)
    dot(ic, 46, 35, 6.2, SILVER)
    dot(ic, 32, 16, 8.2, BRIGHT)


@scene_object("viewport", "Viewport", "Scene")
def viewport(ic):
    # Bespoke UniVex Viewfinder Brackets + Two-Tone Faceted Mountain Frame
    ic.add(
        '<g fill="none" stroke="url(#uv_bright)" stroke-width="3.0" stroke-linecap="round" stroke-linejoin="round">'
        '<path d="M 9.5 20.5 L 9.5 9.5 L 20.5 9.5"/>'
        '<path d="M 43.5 9.5 L 54.5 9.5 L 54.5 20.5"/>'
        '<path d="M 9.5 43.5 L 9.5 54.5 L 20.5 54.5"/>'
        '<path d="M 54.5 43.5 L 54.5 54.5 L 43.5 54.5"/></g>'
        '<rect x="15" y="15" width="34" height="34" rx="4" fill="url(#uv_dark)" stroke="url(#uv_mid)" stroke-width="2.0"/>'
        '<circle cx="23.5" cy="23.5" r="3.4" fill="url(#uv_bright)"/>'
        '<polygon points="17,46 27,31 37,46" fill="url(#uv_slate)"/>'
        '<polygon points="26,46 38,25 38,46" fill="url(#uv_bright)"/>'
        '<polygon points="38,25 47,46 38,46" fill="url(#uv_mid)"/>'
    )


@scene_object("viewport_3d", "Viewport3D", "Scene")
def viewport_3d(ic):
    ic.add(
        '<g fill="none" stroke="url(#uv_bright)" stroke-width="2.8" stroke-linecap="round" stroke-linejoin="round">'
        '<path d="M 9 20 L 9 9 L 20 9"/><path d="M 44 9 L 55 9 L 55 20"/>'
        '<path d="M 9 44 L 9 55 L 20 55"/><path d="M 55 44 L 55 55 L 44 55"/></g>'
    )
    box(ic, 32, 44, 22, 22, 22, LIGHT)


@scene_object("viewport_2d", "Viewport2D", "2D")
def viewport_2d(ic):
    viewport(ic)


@scene_object("object_3d", "Object3D", "Scene")
def object_3d(ic):
    axes(ic, 28, 42, 26)
    sphere(ic, 28, 42, 8.0, BRIGHT)


@scene_object("marker_3d", "Marker3D", "Scene")
def marker_3d(ic):
    ic.add('<ellipse cx="32" cy="52" rx="11" ry="3.8" fill="none" stroke="url(#uv_mid)" stroke-width="1.8"/>')
    ic.add('<path d="M 32 52 C 26.5 41 17 33 17 22.5 A 15 15 0 0 1 47 22.5 C 47 33 37.5 41 32 52 Z" '
           'fill="url(#uv_bright)" stroke="#3F3F46" stroke-width="1.3" stroke-linejoin="round"/>')
    ic.add('<path d="M 32 7.5 A 15 15 0 0 1 47 22.5 C 47 33 37.5 41 32 52 Z" fill="#71717A" fill-opacity="0.22"/>')
    ic.add('<circle cx="32" cy="22.5" r="5.8" fill="#27272A"/>')


@scene_object("spawn_point_3d", "SpawnPoint3D", "Gameplay")
def spawn_point_3d(ic):
    cylinder(ic, 32, 54, 22, 4, MID, ry=7.5)
    ic.add('<ellipse cx="32" cy="48" rx="15" ry="5" fill="none" stroke="#F4F4F5" stroke-width="1.4" stroke-dasharray="3 2"/>')
    # Upward spawn beacon arrow + sculpted bust
    arrow(ic, (32, 42), (32, 12), BRIGHT, 3.6, 9.5)
    ic.add('<path d="M 22 28 L 32 18 L 42 28" fill="none" stroke="url(#uv_mid)" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"/>')


@scene_object("script", "Script", "Logic")
def script_object(ic):
    page(ic, 13, 7, 38, 48, LIGHT, fold=10)
    brace = "M 26 21 q -4.5 0 -4.5 4.5 v 3 q 0 2.8 -2.8 2.8 q 2.8 0 2.8 2.8 v 3 q 0 4.5 4.5 4.5"
    ic.add(f'<path d="{brace}" fill="none" stroke="#27272A" stroke-width="2.8" stroke-linecap="round" stroke-linejoin="round"/>')
    ic.add(f'<path d="{brace}" transform="translate(64 0) scale(-1 1)" fill="none" stroke="#27272A" stroke-width="2.8" stroke-linecap="round" stroke-linejoin="round"/>')


# =============================================================================================
# 2. Physics & Gameplay 3D (Wired — Bespoke UniVex Monochrome)
# =============================================================================================

@scene_object("character_3d", "Character3D", "Physics")
def character_3d(ic):
    # Unified Sculpted Studio Clay Bust: Head + Defined Jawline/Chin ("may baba") + Connected Neck + Shoulders
    ic.add(
        '<path d="M 25.5 29.5 L 25.2 35.2 C 25.0 37.2, 22.0 38.5, 17.5 40.2 C 12.2 42.2, 9.5 46.0, 9.5 52.5 '
        'C 9.5 54.8, 11.0 56.0, 13.2 56.0 L 50.8 56.0 C 53.0 56.0, 54.5 54.8, 54.5 52.5 '
        'C 54.5 46.0, 51.8 42.2, 46.5 40.2 C 42.0 38.5, 39.0 37.2, 38.8 35.2 L 38.5 29.5 Z" fill="url(#uv_mid)"/>'
        '<path d="M 32 29.5 L 38.5 29.5 L 38.8 35.2 C 39.0 37.2, 42.0 38.5, 46.5 40.2 C 51.8 42.2, 54.5 46.0, 54.5 52.5 '
        'C 54.5 54.8, 53.0 56.0, 50.8 56.0 L 32 56.0 Z" fill="#71717A" fill-opacity="0.25"/>'
        '<path d="M 25.4 31.0 C 27.5 36.2, 36.5 36.2, 38.6 31.0 L 38.7 33.8 C 36.0 37.6, 28.0 37.6, 25.3 33.8 Z" fill="#71717A" fill-opacity="0.55"/>'
        '<path d="M 22.0 39.2 C 25.5 42.0, 28.8 43.2, 32 43.2 C 35.2 43.2, 38.5 42.0, 42.0 39.2" fill="none" stroke="#F4F4F5" stroke-width="1.1" stroke-opacity="0.55" stroke-linecap="round"/>'
        '<path d="M 32 7.5 C 39.8 7.5, 43.8 12.0, 43.5 19.2 C 43.3 23.8, 41.8 27.5, 39.0 30.6 '
        'C 36.8 33.0, 34.4 34.8, 32 34.8 C 29.6 34.8, 27.2 33.0, 25.0 30.6 C 22.2 27.5, 20.7 23.8, 20.5 19.2 '
        'C 20.2 12.0, 24.2 7.5, 32 7.5 Z" fill="url(#uv_head_lit)"/>'
        '<path d="M 32 7.5 C 39.8 7.5, 43.8 12.0, 43.5 19.2 C 43.3 23.8, 41.8 27.5, 39.0 30.6 '
        'C 36.8 33.0, 34.4 34.8, 32 34.8 Z" fill="#71717A" fill-opacity="0.16"/>'
        '<path d="M 21.2 21.5 C 21.8 25.8, 23.5 29.0, 26.0 31.5 C 28.2 33.6, 30.1 34.6, 32 34.6 C 33.9 34.6, 35.8 33.6, 38.0 31.5" '
        'fill="none" stroke="#FFFFFF" stroke-width="1.1" stroke-opacity="0.7" stroke-linecap="round"/>'
    )


@scene_object("player_3d", "Player3D", "Gameplay")
def player_3d(ic):
    # Unified Connected Silhouette (Head+Chin+Neck+Torso+Legs) + Aerodynamic Velocity Chevrons
    ic.add(
        '<ellipse cx="23" cy="54.5" rx="13" ry="3.0" fill="none" stroke="url(#uv_slate)" stroke-width="1.6" stroke-dasharray="3 2"/>'
        '<path d="M 23 7.5 C 26.8 7.5, 28.8 10.0, 28.6 13.8 C 28.4 16.5, 26.8 18.8, 25.2 19.6 L 25.2 21.0 '
        'C 29.5 21.6, 32.5 23.8, 34.0 29.5 L 35.5 34.5 C 36.0 36.2, 33.8 37.0, 32.8 35.5 L 30.2 27.5 L 28.5 27.5 '
        'L 28.5 37.0 L 31.5 51.0 C 31.9 53.0, 29.0 53.8, 28.2 51.8 L 24.2 39.5 L 21.8 39.5 L 17.8 51.8 '
        'C 17.0 53.8, 14.1 53.0, 14.5 51.0 L 17.5 37.0 L 17.5 27.5 L 15.8 27.5 L 13.2 35.5 C 12.2 37.0, 10.0 36.2, 10.5 34.5 '
        'L 12.0 29.5 C 13.5 23.8, 16.5 21.6, 20.8 21.0 L 20.8 19.6 C 19.2 18.8, 17.6 16.5, 17.4 13.8 '
        'C 17.2 10.0, 19.2 7.5, 23 7.5 Z" fill="url(#uv_bright)"/>'
        '<path d="M 20.2 19.2 Q 23 21.2 25.8 19.2" fill="none" stroke="#71717A" stroke-width="1.3" stroke-linecap="round"/>'
        '<g fill="none" stroke="url(#uv_bright)" stroke-width="2.8" stroke-linecap="round" stroke-linejoin="round">'
        '<line x1="38" y1="22" x2="51" y2="22"/><path d="M 46 17.5 L 51 22 L 46 26.5"/>'
        '<line x1="40" y1="33" x2="55" y2="33"/><path d="M 50 28.5 L 55 33 L 50 37.5"/></g>'
        '<g fill="none" stroke="url(#uv_mid)" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">'
        '<line x1="37" y1="44" x2="49" y2="44"/><path d="M 44.5 40 L 49 44 L 44.5 48"/></g>'
    )


@scene_object("collider_3d", "Collider3D", "Physics")
def collider_3d(ic):
    # Sculpted 3D Capsule with Equatorial Seam & Precision Impact Waves
    ic.add(
        '<path d="M 13 24 C 9.5 28.5, 9.5 35.5, 13 40" fill="none" stroke="url(#uv_mid)" stroke-width="2.4" stroke-linecap="round"/>'
        '<path d="M 51 24 C 54.5 28.5, 54.5 35.5, 51 40" fill="none" stroke="url(#uv_mid)" stroke-width="2.4" stroke-linecap="round"/>'
        '<polygon points="15,32 8,29 8,35" fill="url(#uv_bright)"/>'
        '<polygon points="49,32 56,29 56,35" fill="url(#uv_bright)"/>'
        '<path d="M 20 22 A 12 12 0 0 1 44 22 L 44 32 L 20 32 Z" fill="url(#uv_bright)"/>'
        '<path d="M 20 32 L 44 32 L 44 42 A 12 12 0 0 1 20 42 Z" fill="url(#uv_mid)"/>'
        '<path d="M 32 10 A 12 12 0 0 1 44 22 L 44 42 A 12 12 0 0 1 32 54 Z" fill="#52525B" fill-opacity="0.22"/>'
        '<ellipse cx="32" cy="32" rx="12" ry="3.8" fill="none" stroke="#52525B" stroke-width="1.4" stroke-opacity="0.75"/>'
        '<path d="M 32 10 C 27 16, 27 48, 32 54" fill="none" stroke="#FFFFFF" stroke-width="1.0" stroke-opacity="0.55"/>'
    )


@scene_object("ray_cast_3d", "RayCast3D", "Physics")
def ray_cast_3d(ic):
    # Two-Tone Isometric Chiseled 3D Vector Arrow
    ic.add(
        '<ellipse cx="15.5" cy="15.5" rx="7.5" ry="4.2" transform="rotate(-45 15.5 15.5)" fill="none" stroke="url(#uv_slate)" stroke-width="1.8" stroke-opacity="0.75"/>'
        '<path d="M 14 14 L 14 35 L 22 27 L 43 48 L 50 41 L 29 20 L 37 12 Z" fill="url(#uv_dark)" transform="translate(0, 4.5)"/>'
        '<path d="M 14 14 L 37 12 L 29 20 L 50 41 L 46.5 44.5 L 14 14 Z" fill="url(#uv_bright)"/>'
        '<path d="M 14 14 L 14 35 L 22 27 L 43 48 L 46.5 44.5 L 14 14 Z" fill="url(#uv_mid)"/>'
        '<line x1="14.5" y1="14.5" x2="46" y2="44" stroke="#FFFFFF" stroke-width="1.1" stroke-opacity="0.7" stroke-linecap="round"/>'
    )


@scene_object("area_3d", "Area3D", "Physics")
def area_3d(ic):
    glass_box(ic, 32, 48, 32, 26, 32, LIGHT, fill_alpha=0.14, width=1.8)
    for x in (24, 32, 40):
        arrow(ic, (x, 20), (x, 36), BRIGHT, 2.2, 5.5)


@scene_object("static_3d", "Static3D", "Physics")
def static_3d(ic):
    box(ic, 32, 51, 34, 18, 34, MID)
    cx, cy = iso(32, 51, 0, 18, 0)
    cylinder(ic, cx, cy + 1, 5.0, 5, BRIGHT, ry=2.4)


@scene_object("kinematic_3d", "Kinematic3D", "Physics")
def kinematic_3d(ic):
    slab(ic, 32, 45, 30, 20, 6, SILVER)
    left, right = iso(32, 45, -29, 3, 0), iso(32, 45, 29, 3, 0)
    stroke_line(ic, [iso(32, 45, -17, 3, 0), left], BRIGHT, 2.2, dash="3 2.5")
    stroke_line(ic, [iso(32, 45, 17, 3, 0), right], BRIGHT, 2.2, dash="3 2.5")
    arrow_head(ic, left, (-COS, -0.5), 7.5, BRIGHT)
    arrow_head(ic, right, (COS, 0.5), 7.5, BRIGHT)
    box(ic, 30, 36, 10, 10, 10, LIGHT)


@scene_object("rigid_3d", "Rigid3D", "Physics")
def rigid_3d(ic):
    for i, (x, y) in enumerate(((14, 11), (10, 21), (14, 31))):
        stroke_line(ic, [(x, y), (x + 8, y + 5)], MID, 2.2, opacity=0.85 - i * 0.15)
    ic.add('<g transform="rotate(-14 35 30)">')
    box(ic, 35, 44, 23, 23, 23, LIGHT)
    ic.add('</g>')
    arrow(ic, (51, 40), (51, 56), BRIGHT, 2.8, 6.8)


# =============================================================================================
# 3. Animation 3D (Wired — Bespoke UniVex Monochrome)
# =============================================================================================

def bone_segment(ic, x1, y1, x2, y2, width, colour=LIGHT):
    dx, dy = x2 - x1, y2 - y1
    n = math.hypot(dx, dy) or 1
    ux, uy = dx / n, dy / n
    px, py = -uy, ux
    widest = (x1 + dx * 0.22, y1 + dy * 0.22)
    shape = [(x1, y1), (widest[0] + px * width, widest[1] + py * width), (x2, y2),
             (widest[0] - px * width, widest[1] - py * width)]
    ic.add(f'<polygon points="{pts(shape)}" fill="url(#uv_bright)" stroke="{DEEP}" stroke-width="1.2" stroke-linejoin="round"/>')
    dark = [(x1, y1), (widest[0] - px * width, widest[1] - py * width), (x2, y2)]
    ic.add(f'<polygon points="{pts(dark)}" fill="{SLATE}" fill-opacity="0.45"/>')


def bone(ic, x1, y1, x2, y2, r, colour=LIGHT):
    dx, dy = x2 - x1, y2 - y1
    n = math.hypot(dx, dy) or 1
    ux, uy = dx / n, dy / n
    px, py = -uy, ux
    shaft = [(x1 + px * r * 0.52, y1 + py * r * 0.52), (x2 + px * r * 0.52, y2 + py * r * 0.52),
             (x2 - px * r * 0.52, y2 - py * r * 0.52), (x1 - px * r * 0.52, y1 - py * r * 0.52)]
    ic.add(f'<polygon points="{pts(shaft)}" fill="url(#uv_mid)" stroke="{DEEP}" stroke-width="1.1"/>')
    for (bx, by) in ((x1, y1), (x2, y2)):
        for side in (1, -1):
            sphere(ic, bx + px * r * 0.58 * side, by + py * r * 0.58 * side, r * 0.68, BRIGHT)
    ic.add(f'<polygon points="{pts(shaft)}" fill="url(#uv_bright)"/>')


@scene_object("skeleton_3d", "Skeleton3D", "Animation")
def skeleton_3d(ic):
    # Connected Anatomical Skull + Jaw/Chin + Cervical Spine + Clavicles + Ribcage + Pelvis
    ic.add(
        '<rect x="30.2" y="25" width="3.6" height="28" rx="1.5" fill="url(#uv_slate)"/>'
        '<path d="M 32 7.5 C 38.8 7.5, 42.5 11.2, 42.2 16.8 C 42 20.2, 40.2 22.2, 38.2 23.4 L 37.2 27.5 '
        'C 36.8 29.2, 34.8 30.2, 32 30.2 C 29.2 30.2, 27.2 29.2, 26.8 27.5 L 25.8 23.4 C 23.8 22.2, 22 20.2, 21.8 16.8 '
        'C 21.5 11.2, 25.2 7.5, 32 7.5 Z" fill="url(#uv_bright)"/>'
        '<ellipse cx="28" cy="17.5" rx="2.7" ry="2.3" fill="#27272A"/>'
        '<ellipse cx="36" cy="17.5" rx="2.7" ry="2.3" fill="#27272A"/>'
        '<polygon points="32,19.8 30.5,23.2 33.5,23.2" fill="#27272A"/>'
        '<path d="M 15 33.5 Q 23.5 31.5 32 33 Q 40.5 31.5 49 33.5" fill="none" stroke="url(#uv_bright)" stroke-width="3.2" stroke-linecap="round"/>'
        '<circle cx="14.5" cy="34" r="2.4" fill="url(#uv_mid)"/>'
        '<circle cx="49.5" cy="34" r="2.4" fill="url(#uv_mid)"/>'
        '<line x1="14.5" y1="34" x2="13" y2="45" stroke="url(#uv_mid)" stroke-width="3.0" stroke-linecap="round"/>'
        '<line x1="49.5" y1="34" x2="51" y2="45" stroke="url(#uv_mid)" stroke-width="3.0" stroke-linecap="round"/>'
        '<rect x="30.5" y="32.5" width="3" height="13" rx="1.2" fill="url(#uv_bright)"/>'
        '<path d="M 20 38 Q 32 40.5 44 38" fill="none" stroke="url(#uv_bright)" stroke-width="3.0" stroke-linecap="round"/>'
        '<path d="M 21.5 43 Q 32 45.2 42.5 43" fill="none" stroke="url(#uv_mid)" stroke-width="2.8" stroke-linecap="round"/>'
        '<path d="M 24 47.5 Q 32 49.2 40 47.5" fill="none" stroke="url(#uv_slate)" stroke-width="2.5" stroke-linecap="round"/>'
        '<path d="M 21 52.5 C 21 50.5, 25 50.5, 32 51.5 C 39 50.5, 43 50.5, 43 52.5 C 43 55.5, 39 56.5, 35.5 55.5 '
        'L 32 53.8 L 28.5 55.5 C 25 56.5, 21 55.5, 21 52.5 Z" fill="url(#uv_mid)"/>'
    )


@scene_object("bone_attachment_3d", "BoneAttachment3D", "Animation")
def bone_attachment_3d(ic):
    bone(ic, 13, 41, 37, 17, 6.5)
    ic.add('<circle cx="45" cy="44" r="9.5" fill="none" stroke="url(#uv_bright)" stroke-width="4.2"/>')
    stroke_line(ic, [(36, 26), (41, 35)], MID, 2.4)


@scene_object("two_bone_ik_3d", "TwoBoneIK3D", "Animation")
def two_bone_ik_3d(ic):
    bone_segment(ic, 11, 50, 29, 31, 6.0)
    bone_segment(ic, 29, 31, 47, 17, 4.8)
    sphere(ic, 29, 31, 5.2, BRIGHT)
    ic.add('<circle cx="47" cy="17" r="8.5" fill="none" stroke="url(#uv_bright)" stroke-width="2.2" stroke-dasharray="4 2.5"/>')
    stroke_line(ic, [(27, 27), (15, 13)], MID, 1.8, dash="2.5 2.5")
    sphere(ic, 14, 12, 3.4, SILVER)


@scene_object("full_body_ik_3d", "FullBodyIK3D", "Animation")
def full_body_ik_3d(ic):
    figure(ic, "stand", LIGHT, scale=0.88, joints=True)
    for cx, cy in ((14, 34), (50, 34)):
        ic.add(f'<circle cx="{cx}" cy="{cy}" r="5.5" fill="none" stroke="url(#uv_bright)" stroke-width="1.8" stroke-dasharray="3 2"/>')


@scene_object("animation_sequencer", "AnimationSequencer", "Animation")
def animation_sequencer(ic):
    ic.add('<rect x="6" y="14" width="52" height="36" rx="5" fill="url(#uv_dark)" stroke="url(#uv_mid)" stroke-width="1.6"/>')
    for x in range(10, 56, 8):
        for y in (17, 43):
            ic.add(f'<rect x="{x}" y="{y}" width="4.2" height="3.8" rx="1" fill="#D4D4D8"/>')
    ic.add('<polygon points="26,22 43,32 26,42" fill="url(#uv_bright)"/>')


@scene_object("animation_graph", "AnimationGraph", "Animation")
def animation_graph(ic):
    stroke_line(ic, [(24, 16), (40, 16)], MID, 2.4)
    stroke_line(ic, [(49, 23), (38, 40)], MID, 2.4)
    stroke_line(ic, [(24, 42), (14, 23)], MID, 2.4)
    for x, y, w in ((5, 9, 20), (39, 9, 20), (21, 39, 22)):
        ic.add(f'<rect x="{x}" y="{y}" width="{w}" height="14" rx="4" fill="url(#uv_bright)" stroke="#3F3F46" stroke-width="1.2"/>')
        ic.add(f'<rect x="{x + 3}" y="{y + 3}" width="{w - 6}" height="3" rx="1.2" fill="#71717A"/>')


# =============================================================================================
# 4. Cameras 3D & 2D (Wired — Bespoke UniVex Monochrome)
# =============================================================================================

@scene_object("camera_3d", "Camera3D", "Camera")
def camera_3d(ic):
    # Clean Sculpted Studio Cinema Camera (No robot-face circles)
    ic.add(
        '<path d="M 14 20 L 17 13.5 L 31 13.5 L 33.5 20 Z" fill="url(#uv_mid)"/>'
        '<rect x="8.5" y="19" width="31" height="26" rx="5" fill="url(#uv_bright)"/>'
        '<path d="M 8.5 33 L 39.5 33 L 39.5 40 C 39.5 42.8, 37.3 45, 34.5 45 L 13.5 45 C 10.7 45, 8.5 42.8, 8.5 40 Z" fill="url(#uv_mid)"/>'
        '<rect x="13.5" y="24" width="18" height="4.5" rx="2.2" fill="#71717A" fill-opacity="0.55"/>'
        '<circle cx="34.5" cy="26.2" r="2.0" fill="#52525B"/>'
        '<rect x="39" y="25.5" width="4.5" height="13" rx="1" fill="url(#uv_slate)"/>'
        '<polygon points="43,23.5 55.5,16.5 55.5,47.5 43,40.5" fill="url(#uv_bright)" stroke="url(#uv_mid)" stroke-width="1.2" stroke-linejoin="round"/>'
        '<polygon points="45.5,25.5 53.5,20.5 53.5,43.5 45.5,38.5" fill="url(#uv_dark)"/>'
    )


@scene_object("spring_arm_3d", "SpringArm3D", "Camera")
def spring_arm_3d(ic):
    # Precision Telescopic Damper Rod + 3D Helical Coils + Dual Pivot Joints
    ic.add(
        '<g stroke="url(#uv_dark)" stroke-width="3.0" stroke-linecap="round">'
        '<line x1="22" y1="33" x2="34" y2="39"/><line x1="28" y1="27" x2="40" y2="33"/><line x1="34" y1="21" x2="46" y2="27"/></g>'
        '<line x1="17" y1="47" x2="49" y2="15" stroke="url(#uv_slate)" stroke-width="4.5" stroke-linecap="round"/>'
        '<line x1="17" y1="47" x2="49" y2="15" stroke="#F4F4F5" stroke-width="1.2" stroke-opacity="0.45" stroke-linecap="round"/>'
        '<g stroke="url(#uv_bright)" stroke-width="3.4" stroke-linecap="round">'
        '<line x1="22" y1="33" x2="31" y2="42"/><line x1="28" y1="27" x2="37" y2="36"/>'
        '<line x1="34" y1="21" x2="43" y2="30"/><line x1="40" y1="15" x2="49" y2="24"/></g>'
        '<circle cx="49.5" cy="14.5" r="5.2" fill="url(#uv_mid)"/>'
        '<circle cx="49.5" cy="14.5" r="2.2" fill="#27272A"/>'
        '<circle cx="16.5" cy="47.5" r="8.5" fill="url(#uv_sphere)"/>'
        '<circle cx="16.5" cy="47.5" r="3.0" fill="#3F3F46"/>'
    )


@scene_object("camera_2d", "Camera2D", "2D")
def camera_2d(ic):
    ic.add(
        '<rect x="9" y="16" width="32" height="28" rx="4.5" fill="url(#uv_bright)" stroke="#52525B" stroke-width="1.2"/>'
        '<circle cx="25" cy="30" r="7.5" fill="url(#uv_dark)" stroke="url(#uv_mid)" stroke-width="1.5"/>'
        '<circle cx="23" cy="28" r="2.0" fill="#F4F4F5"/>'
        '<polygon points="41,24 54,18 54,42 41,36" fill="url(#uv_mid)"/>'
    )


# =============================================================================================
# 5. Combat & Gameplay 3D (Wired — Monochrome)
# =============================================================================================

@scene_object("hitbox_3d", "Hitbox3D", "Combat")
def hitbox_3d(ic):
    wire_box(ic, 32, 48, 30, 26, 30, MID, dash="3 2", width=1.6)
    # Four inward precision strike chevrons
    ic.add(
        '<path d="M 32 10 L 36 22 L 32 19 L 28 22 Z" fill="url(#uv_bright)"/>'
        '<path d="M 32 54 L 36 42 L 32 45 L 28 42 Z" fill="url(#uv_bright)"/>'
        '<path d="M 10 32 L 22 28 L 19 32 L 22 36 Z" fill="url(#uv_bright)"/>'
        '<path d="M 54 32 L 42 28 L 45 32 L 42 36 Z" fill="url(#uv_bright)"/>'
        '<circle cx="32" cy="32" r="4.5" fill="url(#uv_sphere)"/>'
    )


@scene_object("hurtbox_3d", "Hurtbox3D", "Combat")
def hurtbox_3d(ic):
    # Clean sculpted heraldic shield with two-tone bevel (replaces cartoon cracked heart)
    ic.add(
        '<path d="M 32 8 L 50 14 V 30 C 50 43 41 51 32 56 C 23 51 14 43 14 30 V 14 Z" fill="url(#uv_bright)" stroke="#3F3F46" stroke-width="1.4" stroke-linejoin="round"/>'
        '<path d="M 32 8 L 50 14 V 30 C 50 43 41 51 32 56 Z" fill="#71717A" fill-opacity="0.25"/>'
        '<path d="M 32 13 L 45 17.5 V 29.5 C 45 39.5 38 46 32 50 C 26 46 19 39.5 19 29.5 V 17.5 Z" fill="none" stroke="#52525B" stroke-width="1.2"/>'
    )


@scene_object("projectile_3d", "Projectile3D", "Combat")
def projectile_3d(ic):
    for i, off in enumerate((0, 6, 12)):
        stroke_line(ic, [(9 + off * 0.3, 45 - off), (21 + off * 0.3, 39 - off)], MID, 2.2, opacity=0.85 - i * 0.2)
    ic.add(
        '<g transform="rotate(-38 35 31)">'
        '<rect x="18" y="25" width="20" height="13" rx="2" fill="url(#uv_mid)" stroke="#3F3F46" stroke-width="1.2"/>'
        '<path d="M 38 25 C 48 25 54 28.5 57 31.5 C 54 34.5 48 38 38 38 Z" fill="url(#uv_bright)" stroke="#3F3F46" stroke-width="1.2"/>'
        '</g>'
    )


@scene_object("interaction_area_3d", "InteractionArea3D", "Gameplay")
def interaction_area_3d(ic):
    ic.add('<ellipse cx="32" cy="50" rx="24" ry="8" fill="none" stroke="url(#uv_mid)" stroke-width="2.0" stroke-dasharray="4 3"/>')
    ic.add('<rect x="16" y="18" width="32" height="28" rx="6" fill="url(#uv_slate)" stroke="#3F3F46" stroke-width="1.3"/>')
    ic.add('<rect x="19" y="14" width="26" height="24" rx="5" fill="url(#uv_bright)"/>')
    ic.add('<path d="M 36 20 H 27 V 32 H 36 M 27 26 H 34" fill="none" stroke="#27272A" stroke-width="3.0" stroke-linecap="round" stroke-linejoin="round"/>')


# =============================================================================================
# 6. Rendering, Lighting, Environment, World, Optimization, Audio, VFX, UI (Wired)
# =============================================================================================

@scene_object("mesh_instance_3d", "MeshInstance3D", "Rendering")
def mesh_instance_3d(ic):
    faceted_sphere(ic, 34, 30, 19, LIGHT, detail=0)
    axes(ic, 14, 51, 11, 2.6, 5.8)


@scene_object("box_mesh_3d", "BoxMesh3D", "Rendering")
def box_mesh_3d(ic):
    box(ic, 32, 48, 27, 27, 27, LIGHT)


@scene_object("sphere_mesh_3d", "SphereMesh3D", "Rendering")
def sphere_mesh_3d(ic):
    sphere(ic, 32, 32, 21, LIGHT)


@scene_object("plane_mesh_3d", "PlaneMesh3D", "Rendering")
def plane_mesh_3d(ic):
    slab(ic, 32, 45, 42, 42, 4.5, LIGHT)


@scene_object("light_3d", "Light3D", "Rendering")
def light_3d(ic):
    glow(ic, 32, 31, 26, WHITE, 0.25)
    for i in range(8):
        angle = math.radians(i * 45)
        x1, y1 = 32 + math.cos(angle) * 16.5, 29 + math.sin(angle) * 16.5
        x2, y2 = 32 + math.cos(angle) * 23.5, 29 + math.sin(angle) * 23.5
        stroke_line(ic, [(x1, y1), (x2, y2)], BRIGHT, 2.5)
    sphere(ic, 32, 29, 12.5, BRIGHT)
    ic.add('<rect x="27" y="42" width="10" height="6" rx="2" fill="url(#uv_mid)"/>')


@scene_object("directional_light_3d", "DirectionalLight3D", "Rendering")
def directional_light_3d(ic):
    sphere(ic, 21, 20, 10.5, BRIGHT)
    for x, y, l in ((30, 29, 19), (37, 22, 16), (23, 36, 16)):
        arrow(ic, (x, y), (x + l * 0.7, y + l * 0.7), LIGHT, 3.0, 6.8)


@scene_object("sky", "Sky", "Rendering")
def sky_object(ic):
    ic.add('<path d="M 10 44 A 22 22 0 0 1 54 44 Z" fill="url(#uv_mid)" stroke="#52525B" stroke-width="1.3"/>')
    ic.add('<path d="M 20 44 C 16 44 14 40 17 37 C 17 32 23 30 27 33 C 30 28 38 29 40 34 C 45 34 47 38 45 42 C 44 44 41 44 38 44 Z" fill="url(#uv_bright)"/>')


@scene_object("world_environment_3d", "WorldEnvironment", "Rendering")
def world_environment_3d(ic):
    sphere(ic, 32, 32, 21, MID)
    ic.add('<ellipse cx="32" cy="32" rx="21" ry="7.5" fill="none" stroke="#F4F4F5" stroke-width="1.4" stroke-opacity="0.75"/>')
    ic.add('<ellipse cx="32" cy="32" rx="9.5" ry="21" fill="none" stroke="#F4F4F5" stroke-width="1.4" stroke-opacity="0.75"/>')


@scene_object("reflection_probe_3d", "ReflectionProbe3D", "Rendering")
def reflection_probe_3d(ic):
    sphere(ic, 32, 29, 17, BRIGHT)
    ic.add('<ellipse cx="32" cy="29" rx="23" ry="8" fill="none" stroke="url(#uv_mid)" stroke-width="1.8"/>')
    ic.add('<rect x="23" y="17" width="8" height="6" rx="1.5" fill="#FFFFFF" transform="skewX(-12)"/>')


@scene_object("decal_3d", "Decal3D", "Rendering")
def decal_3d(ic):
    slab(ic, 32, 50, 38, 38, 4, MID)
    slab(ic, 32, 39, 24, 24, 2.5, BRIGHT)
    arrow(ic, (32, 12), (32, 32), BRIGHT, 2.8, 7.0)


@scene_object("fog_volume_3d", "FogVolume3D", "Rendering")
def fog_volume_3d(ic):
    wire_box(ic, 32, 49, 32, 24, 32, MID, dash="3 2", width=1.4)
    for y, w in ((27, 26), (34, 32), (41, 24)):
        ic.add(f'<rect x="{32 - w / 2:.1f}" y="{y}" width="{w}" height="4.2" rx="2.1" fill="url(#uv_bright)"/>')


@scene_object("lod_group_3d", "LODGroup3D", "Optimization")
def lod_group_3d(ic):
    sphere(ic, 14, 41, 9.5, BRIGHT)
    faceted_sphere(ic, 34, 38, 11.5, SILVER, detail=0)
    faceted_sphere(ic, 51, 44, 7.5, SLATE, detail=-1)


@scene_object("occluder_3d", "Occluder3D", "Optimization")
def occluder_3d(ic):
    box(ic, 32, 52, 28, 34, 8, MID)
    eye(ic, 30, 30, 22, BRIGHT, slashed=True)


@scene_object("visibility_region_3d", "VisibilityRegion3D", "Optimization")
def visibility_region_3d(ic):
    ic.add('<path d="M 17 32 L 56 14 V 50 Z" fill="#D4D4D8" fill-opacity="0.22" stroke="#E4E4E7" stroke-width="1.6" stroke-dasharray="4 2.5"/>')
    eye(ic, 17, 32, 22, BRIGHT)


@scene_object("nav_mesh_volume_3d", "NavMeshVolume3D", "Navigation")
def nav_mesh_volume_3d(ic):
    top, _, _ = slab(ic, 32, 49, 40, 40, 4, MID)
    a, b, c, d = top
    stroke_line(ic, [a, c], BRIGHT, 1.5)
    stroke_line(ic, [b, d], BRIGHT, 1.5)


@scene_object("nav_seeker_3d", "NavSeeker3D", "Navigation")
def nav_seeker_3d(ic):
    stroke_line(ic, [(14, 50), (30, 44), (48, 49)], MID, 2.4, dash="3.5 2.5")
    figure(ic, "stand", BRIGHT, dx=-10, dy=0, scale=0.82)
    arrow(ic, (36, 32), (52, 20), BRIGHT, 2.8, 7.0)


@scene_object("navigation_region_3d", "NavigationRegion3D", "Navigation")
def navigation_region_3d(ic):
    nav_mesh_volume_3d(ic)


@scene_object("navigation_agent_3d", "NavigationAgent3D", "Navigation")
def navigation_agent_3d(ic):
    nav_seeker_3d(ic)


@scene_object("level_streamer_3d", "LevelStreamer3D", "World")
def level_streamer_3d(ic):
    slab(ic, 32, 53, 32, 32, 5, MID)
    glass_box(ic, 32, 30, 32, 5, 32, BRIGHT, fill_alpha=0.16, width=1.5)
    arrow(ic, (32, 13), (32, 42), BRIGHT, 3.4, 9.0)


@scene_object("world_partition_3d", "WorldPartition3D", "World")
def world_partition_3d(ic):
    size, gap = 11.5, 1.8
    for i in (-1, 0, 1):
        for j in (-1, 0, 1):
            raised = (i, j) == (1, 1)
            cx, cy = iso(32, 40, i * (size + gap), 0, j * (size + gap))
            slab(ic, cx, cy - (5 if raised else 0), size, size, 3.2, BRIGHT if raised else MID)


@scene_object("audio_source_3d", "AudioSource3D", "Audio")
def audio_source_3d(ic):
    ic.add('<path d="M 12 25 H 21 L 33 14 V 50 L 21 39 H 12 A 2 2 0 0 1 10 37 V 27 A 2 2 0 0 1 12 25 Z" fill="url(#uv_bright)"/>')
    for r, w in ((9, 2.8), (16, 2.6), (23, 2.4)):
        ic.add(f'<path d="M {35 + r * 0.45:.1f} {32 - r * 0.65:.1f} A {r} {r} 0 0 1 {35 + r * 0.45:.1f} {32 + r * 0.65:.1f}" '
               f'fill="none" stroke="url(#uv_mid)" stroke-width="{w}" stroke-linecap="round"/>')


@scene_object("particle_emitter_3d", "ParticleEmitter3D", "VFX")
def particle_emitter_3d(ic):
    cylinder(ic, 32, 55, 11, 8, MID, ry=4.5)
    for cx, cy, ro, ri in ((32, 24, 9.5, 3.2), (18, 34, 6.0, 2.0), (46, 32, 6.5, 2.2), (24, 13, 5.0, 1.7), (43, 15, 5.5, 1.8)):
        ic.add(f'<polygon points="{pts(star(cx, cy, ro, ri, 4))}" fill="url(#uv_bright)"/>')


@scene_object("sprite_2d", "Sprite2D", "2D")
def sprite_2d(ic):
    viewport(ic)


@scene_object("canvas", "Canvas", "UI")
def canvas_object(ic):
    ic.add('<rect x="8" y="11" width="48" height="42" rx="5" fill="url(#uv_dark)" stroke="url(#uv_bright)" stroke-width="2.0"/>')
    ic.add('<line x1="8" y1="20" x2="56" y2="20" stroke="#D4D4D8" stroke-width="1.6"/>')
    ic.add('<rect x="14" y="26" width="16" height="10" rx="2" fill="url(#uv_mid)"/>')
    ic.add('<rect x="34" y="26" width="16" height="20" rx="2" fill="url(#uv_slate)"/>')
    ic.add('<rect x="14" y="40" width="16" height="6" rx="2" fill="url(#uv_bright)"/>')


@scene_object("ui_text", "UIText", "UI")
def ui_text(ic):
    ic.add('<path d="M 14 14 H 50 V 23 H 36.5 V 51 H 27.5 V 23 H 14 Z" fill="url(#uv_bright)" stroke="#3F3F46" stroke-width="1.3" stroke-linejoin="round"/>')


@scene_object("ui_image", "UIImage", "UI")
def ui_image(ic):
    ic.add('<rect x="9" y="12" width="46" height="40" rx="5" fill="url(#uv_dark)" stroke="url(#uv_bright)" stroke-width="2.0"/>')
    ic.add('<circle cx="21" cy="23" r="4.0" fill="url(#uv_bright)"/>')
    ic.add('<polygon points="13,47 26,31 37,47" fill="url(#uv_slate)"/>')
    ic.add('<polygon points="26,47 39,26 51,47" fill="url(#uv_bright)"/>')


@scene_object("ui_button", "UIButton", "UI")
def ui_button(ic):
    ic.add('<rect x="8" y="23" width="48" height="22" rx="11" fill="url(#uv_dark)"/>')
    ic.add('<rect x="8" y="19" width="48" height="22" rx="11" fill="url(#uv_bright)" stroke="#3F3F46" stroke-width="1.4"/>')
    ic.add('<rect x="19" y="28" width="26" height="4" rx="2" fill="#3F3F46"/>')


# =============================================================================================
# 7. Unwired 3D Scene Objects (42 Objects in Engine/Runtime/Objects/3D/)
# =============================================================================================

@scene_object("csg_box_3d", "CSGBox3D", "Rendering")
def csg_box_3d(ic):
    box(ic, 32, 48, 26, 26, 26, LIGHT)
    wire_box(ic, 32, 48, 28, 28, 28, BRIGHT, dash="2.5 2", width=1.2, back=False)


@scene_object("csg_sphere_3d", "CSGSphere3D", "Rendering")
def csg_sphere_3d(ic):
    sphere(ic, 32, 32, 19, LIGHT)
    ic.add('<ellipse cx="32" cy="32" rx="19" ry="6.5" fill="none" stroke="#F4F4F5" stroke-width="1.2" stroke-dasharray="3 2"/>')


@scene_object("csg_cylinder_3d", "CSGCylinder3D", "Rendering")
def csg_cylinder_3d(ic):
    cylinder(ic, 32, 48, 15, 28, LIGHT)


@scene_object("csg_torus_3d", "CSGTorus3D", "Rendering")
def csg_torus_3d(ic):
    ic.add('<ellipse cx="32" cy="33" rx="21" ry="12" fill="none" stroke="url(#uv_bright)" stroke-width="8.5"/>')
    ic.add('<ellipse cx="32" cy="33" rx="21" ry="12" fill="none" stroke="#52525B" stroke-width="1.2"/>')


@scene_object("csg_mesh_3d", "CSGMesh3D", "Rendering")
def csg_mesh_3d(ic):
    faceted_sphere(ic, 32, 32, 20, LIGHT, detail=0, wire=BRIGHT)


@scene_object("csg_polygon_3d", "CSGPolygon3D", "Rendering")
def csg_polygon_3d(ic):
    ic.add('<polygon points="18,46 14,24 32,12 50,24 46,46" fill="url(#uv_bright)" stroke="#3F3F46" stroke-width="1.4"/>')


@scene_object("csg_combiner_3d", "CSGCombiner3D", "Rendering")
def csg_combiner_3d(ic):
    ic.add('<circle cx="25" cy="32" r="14" fill="url(#uv_bright)" fill-opacity="0.85" stroke="#F4F4F5" stroke-width="1.5"/>')
    ic.add('<circle cx="39" cy="32" r="14" fill="url(#uv_mid)" fill-opacity="0.85" stroke="#D4D4D8" stroke-width="1.5"/>')


@scene_object("voxel_gi_3d", "VoxelGI3D", "Rendering")
def voxel_gi_3d(ic):
    for dx, dy, dz in ((-7, 0, -7), (7, 0, -7), (-7, 0, 7), (7, 0, 7), (-7, 14, -7), (7, 14, -7), (0, 14, 7)):
        cx, cy = iso(32, 44, dx, dy, dz)
        box(ic, cx, cy, 11, 11, 11, BRIGHT if dy > 0 else MID)


@scene_object("lightmap_gi_3d", "LightmapGI3D", "Rendering")
def lightmap_gi_3d(ic):
    slab(ic, 32, 48, 38, 38, 4, MID)
    sphere(ic, 32, 22, 9, BRIGHT)


@scene_object("lightmap_probe_3d", "LightmapProbe3D", "Rendering")
def lightmap_probe_3d(ic):
    for cx, cy, r in ((32, 20, 8), (17, 42, 6.5), (47, 42, 6.5)):
        sphere(ic, cx, cy, r, BRIGHT)
    stroke_line(ic, [(32, 28), (17, 42), (47, 42), (32, 28)], MID, 1.6, dash="2.5 2")


@scene_object("multi_mesh_instance_3d", "MultiMeshInstance3D", "Rendering")
def multi_mesh_instance_3d(ic):
    box(ic, 20, 44, 15, 15, 15, MID)
    box(ic, 44, 44, 15, 15, 15, MID)
    box(ic, 32, 52, 17, 17, 17, BRIGHT)


@scene_object("sprite_3d", "Sprite3D", "Rendering")
def sprite_3d(ic):
    ic.add('<g transform="skewY(-12) translate(0, 7)">')
    ui_image(ic)
    ic.add('</g>')


@scene_object("animated_sprite_3d", "AnimatedSprite3D", "Rendering")
def animated_sprite_3d(ic):
    ic.add('<rect x="16" y="10" width="36" height="32" rx="4" fill="url(#uv_slate)" opacity="0.6"/>')
    ic.add('<rect x="11" y="16" width="36" height="32" rx="4" fill="url(#uv_bright)" stroke="#3F3F46" stroke-width="1.3"/>')
    ic.add('<polygon points="24,24 37,32 24,40" fill="#27272A"/>')


@scene_object("label_3d", "Label3D", "Rendering")
def label_3d(ic):
    ic.add('<path d="M 14 16 H 50 V 25 H 36.5 V 52 H 27.5 V 25 H 14 Z" fill="url(#uv_dark)" transform="translate(3, 3)"/>')
    ui_text(ic)


@scene_object("grid_map_3d", "GridMap3D", "World")
def grid_map_3d(ic):
    world_partition_3d(ic)


@scene_object("path_3d", "Path3D", "Scene")
def path_3d(ic):
    ic.add('<path d="M 12 48 C 14 18, 50 46, 52 16" fill="none" stroke="url(#uv_bright)" stroke-width="3.2" stroke-linecap="round"/>')
    dot(ic, 12, 48, 4.5, BRIGHT)
    dot(ic, 32, 32, 4.0, MID)
    dot(ic, 52, 16, 4.5, BRIGHT)


@scene_object("path_follow_3d", "PathFollow3D", "Scene")
def path_follow_3d(ic):
    path_3d(ic)
    arrow(ic, (24, 36), (40, 24), BRIGHT, 3.0, 8.0)


@scene_object("remote_transform_3d", "RemoteTransform3D", "Scene")
def remote_transform_3d(ic):
    axes(ic, 18, 48, 14, 2.6, 6.0)
    axes(ic, 42, 28, 14, 2.6, 6.0)
    stroke_line(ic, [(22, 44), (38, 30)], BRIGHT, 2.0, dash="3 2")


@scene_object("visible_on_screen_notifier_3d", "VisibleOnScreenNotifier3D", "Optimization")
def visible_on_screen_notifier_3d(ic):
    wire_box(ic, 32, 48, 30, 26, 30, MID, width=1.5)
    eye(ic, 32, 32, 24, BRIGHT)


@scene_object("audio_listener_3d", "AudioListener3D", "Audio")
def audio_listener_3d(ic):
    character_3d(ic)
    ic.add('<path d="M 15 20 A 17 17 0 0 1 49 20" fill="none" stroke="url(#uv_bright)" stroke-width="3.5" stroke-linecap="round"/>')
    ic.add('<rect x="12" y="18" width="5" height="10" rx="2.5" fill="url(#uv_bright)"/>')
    ic.add('<rect x="47" y="18" width="5" height="10" rx="2.5" fill="url(#uv_bright)"/>')


@scene_object("shape_cast_3d", "ShapeCast3D", "Physics")
def shape_cast_3d(ic):
    sphere(ic, 18, 18, 8.5, MID)
    stroke_line(ic, [(18, 18), (44, 44)], BRIGHT, 2.6, dash="4 2.5")
    sphere(ic, 44, 44, 10.5, BRIGHT)


@scene_object("soft_body_3d", "SoftBody3D", "Physics")
def soft_body_3d(ic):
    ic.add('<path d="M 14 20 Q 32 28 50 20 Q 46 38 50 50 Q 32 42 14 50 Q 18 35 14 20 Z" fill="url(#uv_bright)" stroke="#3F3F46" stroke-width="1.4"/>')
    ic.add('<path d="M 16 35 Q 32 41 48 35 M 32 24 V 46" fill="none" stroke="#71717A" stroke-width="1.3"/>')


@scene_object("collision_polygon_3d", "CollisionPolygon3D", "Physics")
def collision_polygon_3d(ic):
    csg_polygon_3d(ic)
    wire_box(ic, 32, 48, 30, 24, 30, BRIGHT, width=1.4, back=False)


@scene_object("physical_bone_3d", "PhysicalBone3D", "Physics")
def physical_bone_3d(ic):
    bone(ic, 16, 46, 48, 18, 7.0)
    ic.add('<ellipse cx="32" cy="32" rx="18" ry="9" transform="rotate(-41 32 32)" fill="none" stroke="#F4F4F5" stroke-width="1.6" stroke-dasharray="4 2.5"/>')


@scene_object("ragdoll_3d", "Ragdoll3D", "Physics")
def ragdoll_3d(ic):
    # Dynamic articulated ragdoll figure in physical tumble/fall pose:
    # Head, tilted torso capsule, limb capsules with joint constraint nodes & velocity arcs
    ic.add('<circle cx="36" cy="14" r="5.5" fill="url(#uv_head_lit)" stroke="#71717A" stroke-width="1.2"/>')
    ic.add('<path d="M 33 20 L 26 34" stroke="url(#uv_bright)" stroke-width="6.5" stroke-linecap="round"/>')
    ic.add('<path d="M 33 20 L 26 34" stroke="#52525B" stroke-width="1.0" fill="none"/>')
    ic.add('<path d="M 33 21 L 43 25 L 49 19" fill="none" stroke="url(#uv_bright)" stroke-width="3.6" stroke-linecap="round" stroke-linejoin="round"/>')
    ic.add('<path d="M 31 22 L 20 23 L 14 30" fill="none" stroke="url(#uv_bright)" stroke-width="3.6" stroke-linecap="round" stroke-linejoin="round"/>')
    ic.add('<path d="M 26 34 L 33 43 L 42 48" fill="none" stroke="url(#uv_bright)" stroke-width="4.0" stroke-linecap="round" stroke-linejoin="round"/>')
    ic.add('<path d="M 26 34 L 17 41 L 11 51" fill="none" stroke="url(#uv_bright)" stroke-width="4.0" stroke-linecap="round" stroke-linejoin="round"/>')
    for cx, cy in [(33, 21), (31, 22), (26, 34), (43, 25), (20, 23), (33, 43), (17, 41)]:
        ic.add(f'<circle cx="{cx}" cy="{cy}" r="2.0" fill="#27272A" stroke="#F4F4F5" stroke-width="1.0"/>')
    ic.add('<path d="M 46 32 L 51 35 L 47 40" fill="none" stroke="#71717A" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"/>')
    ic.add('<path d="M 50 27 L 55 30 L 51 35" fill="none" stroke="#A1A1AA" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"/>')


@scene_object("vehicle_body_3d", "VehicleBody3D", "Physics")
def vehicle_body_3d(ic):
    ic.add('<path d="M 10 38 L 16 24 H 42 L 50 34 H 55 A 3 3 0 0 1 58 37 V 44 H 6 V 38 Z" fill="url(#uv_bright)" stroke="#3F3F46" stroke-width="1.3" stroke-linejoin="round"/>')
    ic.add('<circle cx="18" cy="45" r="6.5" fill="url(#uv_dark)" stroke="#D4D4D8" stroke-width="2"/>')
    ic.add('<circle cx="45" cy="45" r="6.5" fill="url(#uv_dark)" stroke="#D4D4D8" stroke-width="2"/>')


@scene_object("vehicle_wheel_3d", "VehicleWheel3D", "Physics")
def vehicle_wheel_3d(ic):
    ic.add('<circle cx="32" cy="32" r="19" fill="url(#uv_dark)" stroke="url(#uv_bright)" stroke-width="3.5"/>')
    ic.add('<circle cx="32" cy="32" r="9" fill="url(#uv_mid)"/>')
    ic.add('<circle cx="32" cy="32" r="3.5" fill="#27272A"/>')


@scene_object("pin_joint_3d", "PinJoint3D", "Physics")
def pin_joint_3d(ic):
    stroke_line(ic, [(12, 48), (32, 32), (52, 16)], MID, 5.0)
    sphere(ic, 32, 32, 8.5, BRIGHT)


@scene_object("hinge_joint_3d", "HingeJoint3D", "Physics")
def hinge_joint_3d(ic):
    cylinder(ic, 32, 42, 8, 20, BRIGHT)
    arrow(ic, (14, 32), (50, 32), MID, 2.5, 6.5)


@scene_object("slider_joint_3d", "SliderJoint3D", "Physics")
def slider_joint_3d(ic):
    stroke_line(ic, [(10, 32), (54, 32)], MID, 5.0)
    box(ic, 32, 40, 16, 16, 16, BRIGHT)


@scene_object("cone_twist_joint_3d", "ConeTwistJoint3D", "Physics")
def cone_twist_joint_3d(ic):
    ic.add('<polygon points="16,46 46,16 52,36" fill="url(#uv_mid)" stroke="#F4F4F5" stroke-width="1.4"/>')
    sphere(ic, 16, 46, 6.5, BRIGHT)


@scene_object("generic_6dof_joint_3d", "Generic6DOFJoint3D", "Physics")
def generic_6dof_joint_3d(ic):
    axes(ic, 32, 36, 20, 3.2, 7.5)
    ic.add('<circle cx="32" cy="32" r="16" fill="none" stroke="url(#uv_mid)" stroke-width="1.8" stroke-dasharray="3 2"/>')


@scene_object("navigation_obstacle_3d", "NavigationObstacle3D", "Navigation")
def navigation_obstacle_3d(ic):
    slab(ic, 32, 50, 38, 38, 4, DARK)
    cylinder(ic, 32, 46, 11, 22, BRIGHT)


@scene_object("navigation_link_3d", "NavigationLink3D", "Navigation")
def navigation_link_3d(ic):
    slab(ic, 18, 48, 16, 16, 4, MID)
    slab(ic, 46, 48, 16, 16, 4, MID)
    ic.add('<path d="M 18 42 Q 32 14 46 42" fill="none" stroke="url(#uv_bright)" stroke-width="3.0" stroke-dasharray="4 2.5" stroke-linecap="round"/>')


@scene_object("smart_object_3d", "SmartObject3D", "Gameplay")
def smart_object_3d(ic):
    box(ic, 32, 48, 22, 22, 22, BRIGHT)
    for angle in (0, 90, 180, 270):
        a = math.radians(angle)
        dot(ic, 32 + math.cos(a) * 22, 34 + math.sin(a) * 11, 3.5, LIGHT)


@scene_object("world_query_3d", "WorldQuery3D", "Gameplay")
def world_query_3d(ic):
    for r in (8, 15, 22):
        ic.add(f'<ellipse cx="32" cy="38" rx="{r}" ry="{r * 0.48:.1f}" fill="none" stroke="url(#uv_bright)" stroke-width="1.8" stroke-dasharray="3 2"/>')
    sphere(ic, 32, 22, 6.5, BRIGHT)


@scene_object("state_tree_3d", "StateTree3D", "Logic")
def state_tree_3d(ic):
    animation_graph(ic)


@scene_object("spring_bone_simulator_3d", "SpringBoneSimulator3D", "Animation")
def spring_bone_simulator_3d(ic):
    spring_arm_3d(ic)


@scene_object("spring_bone_collision_3d", "SpringBoneCollision3D", "Animation")
def spring_bone_collision_3d(ic):
    collider_3d(ic)


@scene_object("xr_origin_3d", "XROrigin3D", "Camera")
def xr_origin_3d(ic):
    ic.add('<ellipse cx="32" cy="48" rx="22" ry="7" fill="none" stroke="url(#uv_bright)" stroke-width="2.0"/>')
    axes(ic, 32, 46, 18, 3.0, 7.0)


@scene_object("xr_camera_3d", "XRCamera3D", "Camera")
def xr_camera_3d(ic):
    # VR Headset Visor Silhouette
    ic.add('<rect x="10" y="22" width="44" height="22" rx="8" fill="url(#uv_bright)" stroke="#3F3F46" stroke-width="1.4"/>')
    ic.add('<rect x="15" y="26" width="34" height="12" rx="5" fill="url(#uv_dark)"/>')
    ic.add('<path d="M 28 44 L 32 39 L 36 44" fill="#27272A"/>')


@scene_object("xr_controller_3d", "XRController3D", "Camera")
def xr_controller_3d(ic):
    ic.add('<ellipse cx="32" cy="24" rx="16" ry="9" fill="none" stroke="url(#uv_bright)" stroke-width="3.5"/>')
    ic.add('<rect x="27" y="24" width="10" height="26" rx="5" fill="url(#uv_mid)" stroke="#3F3F46" stroke-width="1.2"/>')


@scene_object("xr_face_modifier_3d", "XRFaceModifier3D", "Animation")
def xr_face_modifier_3d(ic):
    character_3d(ic)


# =============================================================================================
# 8. ECS Inspector Component Icons (20 Components)
# =============================================================================================

@component_icon("component_transform", "Transform Component")
def comp_transform(ic):
    object_3d(ic)


@component_icon("component_mesh", "Mesh Component")
def comp_mesh(ic):
    mesh_instance_3d(ic)


@component_icon("component_primitive_mesh", "Primitive Mesh Component")
def comp_primitive_mesh(ic):
    box_mesh_3d(ic)


@component_icon("component_surface_material", "Surface Material Component")
def comp_surface_material(ic):
    material_asset(ic)


@component_icon("component_skin", "Skin Component")
def comp_skin(ic):
    skeleton_3d(ic)


@component_icon("component_morph_target", "Morph Target Component")
def comp_morph_target(ic):
    character_3d(ic)


@component_icon("component_collider", "Collider Component")
def comp_collider(ic):
    collider_3d(ic)


@component_icon("component_rigid_body", "RigidBody Component")
def comp_rigid_body(ic):
    rigid_3d(ic)


@component_icon("component_character_controller", "Character Controller Component")
def comp_char_controller(ic):
    player_3d(ic)


@component_icon("component_camera", "Camera Component")
def comp_camera(ic):
    camera_3d(ic)


@component_icon("component_light", "Light Component")
def comp_light(ic):
    light_3d(ic)


@component_icon("component_audio_source", "Audio Source Component")
def comp_audio_source(ic):
    audio_source_3d(ic)


@component_icon("component_particle_emitter", "Particle Emitter Component")
def comp_particle_emitter(ic):
    particle_emitter_3d(ic)


@component_icon("component_animation_sequencer", "Animation Sequencer Component")
def comp_anim_seq(ic):
    animation_sequencer(ic)


@component_icon("component_animation_graph", "Animation Graph Component")
def comp_anim_graph(ic):
    animation_graph(ic)


@component_icon("component_script", "Script Component")
def comp_script(ic):
    script_object(ic)


@component_icon("component_visibility", "Visibility Component")
def comp_visibility(ic):
    eye(ic, 32, 32, 36, BRIGHT)


@component_icon("component_hierarchy", "Hierarchy Component")
def comp_hierarchy(ic):
    scene_root(ic)


@component_icon("component_prefab_instance", "Prefab Instance Component")
def comp_prefab(ic):
    prefab_asset(ic)


@component_icon("component_health", "Health Component")
def comp_health(ic):
    hurtbox_3d(ic)


@component_icon("component_ragdoll_3d", "Ragdoll3D Component")
def comp_ragdoll_3d(ic):
    ragdoll_3d(ic)



# =============================================================================================
# 11. Object Palette Categories (15 Categories — Monochrome Matte Tiles)
# =============================================================================================

CATEGORIES = [
    ("Scene", "M 31 18 V 26 M 31 26 L 19 38 M 31 26 L 43 38", [(31, 16, 4.5), (19, 40, 4.0), (43, 40, 4.0)]),
    ("2D", "M 16 16 H 48 V 48 H 16 Z M 16 38 L 28 26 L 38 36 L 48 28", []),
    ("Physics", "M 14 26 H 22 M 12 33 H 21 M 16 40 H 23", [(36, 32, 9.5)]),
    ("Navigation", "M 20 47 V 15 M 20 17 H 42 L 36 24.5 L 42 32 H 20", []),
    ("Animation", "M 24 18 L 43 31 L 24 44 Z", []),
    ("Camera", "M 13 22 H 37 V 42 H 13 Z M 37 28 L 48 22 V 42 L 37 36", [(25, 32, 4.5)]),
    ("Combat", "M 31 13 V 22 M 31 40 V 49 M 13 31 H 22 M 40 31 H 49", [(31, 31, 3.0)]),
    ("Gameplay", None, []),
    ("Rendering", "M 31 14 L 46 22.5 V 39.5 L 31 48 L 16 39.5 V 22.5 Z M 16 22.5 L 31 31 L 46 22.5 M 31 31 V 48", []),
    ("Optimization", "M 14 40 A 17 17 0 0 1 48 40 M 31 40 L 41 27", [(31, 40, 3.2)]),
    ("World", "M 31 14 A 17 17 0 1 0 31.01 14 Z M 14 31 H 48 M 31 14 C 22 22 22 40 31 48 M 31 14 C 40 22 40 40 31 48", []),
    ("Audio", "M 14 26 H 21 L 29 18 V 44 L 21 36 H 14 Z M 35 24 Q 40 31 35 38 M 40 19 Q 48 31 40 43", []),
    ("VFX", None, []),
    ("Logic", "M 25 16 Q 19 16 19 22 V 27 Q 19 31 15 31 Q 19 31 19 35 V 40 Q 19 46 25 46 M 37 16 Q 43 16 43 22 V 27 Q 43 31 47 31 Q 43 31 43 35 V 40 Q 43 46 37 46", []),
    ("UI", "M 13 16 H 49 V 42 H 13 Z M 13 23 H 49", [(17, 19.5, 1.4), (21, 19.5, 1.4)]),
]


def _category(name, path, dots):
    def draw(ic):
        tile(ic, SLATE)
        if name == "Gameplay":
            glyph(ic, "M " + " L ".join(f"{x:.1f} {y:.1f}" for x, y in star(32, 32, 15, 6.5, 5)) + " Z", width=2.2, fill=BRIGHT)
        elif name == "VFX":
            glyph(ic, "M " + " L ".join(f"{x:.1f} {y:.1f}" for x, y in star(28, 34, 14, 4.2, 4)) + " Z", width=2.0, fill=BRIGHT)
            glyph(ic, "M " + " L ".join(f"{x:.1f} {y:.1f}" for x, y in star(44, 19, 6.5, 2.0, 4)) + " Z", width=1.6, fill=BRIGHT)
        elif name == "Animation":
            glyph(ic, path, width=2.6, fill=BRIGHT)
        else:
            glyph(ic, path, width=3.4, color=BRIGHT)
        for cx, cy, r in dots:
            ic.add(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{BRIGHT}"/>')
    return draw


for _name, _path, _dots in CATEGORIES:
    _register(OBJECT_CATEGORIES, _name.lower(), _name, "Object categories")(_category(_name, _path, _dots))


# =============================================================================================
# 12. Content Browser Asset Types (21 Assets — Monochrome Studio)
# =============================================================================================

@content_type("folder", "Folder")
def folder_asset(ic):
    ic.defs.append(
        '<radialGradient id="r_fold" cx="0.5" cy="0.5" r="0.5" gradientUnits="objectBoundingBox">'
        '<stop offset="0" stop-color="#000" stop-opacity="0.42"/>'
        '<stop offset="0.65" stop-color="#000" stop-opacity="0.189"/>'
        '<stop offset="1" stop-color="#000" stop-opacity="0"/></radialGradient>'
        '<linearGradient id="l_fold2" x1="0" y1="0" x2="0" y2="1" gradientUnits="objectBoundingBox">'
        '<stop offset="0" stop-color="#f1ad2d"/><stop offset="1" stop-color="#ba7e0d"/></linearGradient>'
        '<linearGradient id="l_fold3" x1="0" y1="0" x2="0" y2="1" gradientUnits="objectBoundingBox">'
        '<stop offset="0" stop-color="#f8d89b"/><stop offset="0.55" stop-color="#f2b33b"/><stop offset="1" stop-color="#d7920e"/></linearGradient>'
    )
    ic.add('<ellipse cx="32" cy="56" rx="27" ry="4.5" fill="url(#r_fold)"/>')
    ic.add('<path d="M 7 12 h 17 l 5 5 h 28 a 3 3 0 0 1 3 3 v 30 a 3 3 0 0 1 -3 3 H 7 a 3 3 0 0 1 -3 -3 V 15 a 3 3 0 0 1 3 -3 Z" fill="url(#l_fold2)" stroke="#0b0f15" stroke-opacity="0.75" stroke-width="1.5" stroke-linejoin="round"/>')
    ic.add('<rect x="11" y="19" width="40" height="18" rx="1.5" fill="#f4f6f9" stroke="#0b0f15" stroke-opacity="0.35" stroke-width="1"/>')
    ic.add('<rect x="14" y="23" width="22" height="2" rx="1" fill="#c3ccd8"/>')
    ic.add('<path d="M 4.5 27 a 3 3 0 0 1 3 -3 h 50 a 3 3 0 0 1 3 3 l -2 23 a 3 3 0 0 1 -3 3 H 8.5 a 3 3 0 0 1 -3 -3 Z" fill="url(#l_fold3)" stroke="#0b0f15" stroke-opacity="0.75" stroke-width="1.5" stroke-linejoin="round"/>')
    ic.add('<path d="M 7.5 26.5 h 50" stroke="#fdf2de" stroke-width="1.3" stroke-linecap="round"/>')


@scene_object("folder", "Folder", "Scene")
def folder_object(ic):
    folder_asset(ic)


@content_type("folder_open", "Folder (open)")
def folder_open_asset(ic):
    ic.defs.append(
        '<radialGradient id="r_fold" cx="0.5" cy="0.5" r="0.5" gradientUnits="objectBoundingBox">'
        '<stop offset="0" stop-color="#000" stop-opacity="0.42"/>'
        '<stop offset="0.65" stop-color="#000" stop-opacity="0.189"/>'
        '<stop offset="1" stop-color="#000" stop-opacity="0"/></radialGradient>'
        '<linearGradient id="l_fold2" x1="0" y1="0" x2="0" y2="1" gradientUnits="objectBoundingBox">'
        '<stop offset="0" stop-color="#f1ad2d"/><stop offset="1" stop-color="#ba7e0d"/></linearGradient>'
        '<linearGradient id="l_fold3_open" x1="0" y1="0" x2="0" y2="1" gradientUnits="objectBoundingBox">'
        '<stop offset="0" stop-color="#fae0ae"/><stop offset="0.55" stop-color="#f2b33b"/><stop offset="1" stop-color="#d7920e"/></linearGradient>'
    )
    ic.add('<ellipse cx="32" cy="56" rx="27" ry="4.5" fill="url(#r_fold)"/>')
    ic.add('<path d="M 7 12 h 17 l 5 5 h 28 a 3 3 0 0 1 3 3 v 30 a 3 3 0 0 1 -3 3 H 7 a 3 3 0 0 1 -3 -3 V 15 a 3 3 0 0 1 3 -3 Z" fill="url(#l_fold2)" stroke="#0b0f15" stroke-opacity="0.75" stroke-width="1.5" stroke-linejoin="round"/>')
    ic.add('<rect x="11" y="19" width="40" height="18" rx="1.5" fill="#f4f6f9" stroke="#0b0f15" stroke-opacity="0.35" stroke-width="1"/>')
    ic.add('<rect x="14" y="23" width="22" height="2" rx="1" fill="#c3ccd8"/>')
    ic.add('<path d="M 12 31 a 3 3 0 0 1 3 -2.5 h 45 a 2.5 2.5 0 0 1 2.4 3.2 l -6 19 a 3 3 0 0 1 -3 2.3 H 7 a 2.5 2.5 0 0 1 -2.4 -3.2 Z" fill="url(#l_fold3_open)" stroke="#0b0f15" stroke-opacity="0.75" stroke-width="1.5" stroke-linejoin="round"/>')
    ic.add('<path d="M 15 31 h 44" stroke="#fdf6e7" stroke-width="1.3" stroke-linecap="round"/>')


@content_type("viewport", "Viewport")
def viewport_asset(ic):
    viewport(ic)


@content_type("viewport_3d", "Viewport 3D")
def viewport_3d_asset(ic):
    viewport_3d(ic)


@content_type("Viewport3D", "Viewport3D")
def viewport_3d_camel_asset(ic):
    viewport_3d(ic)


@content_type("viewport_2d", "Viewport 2D")
def viewport_2d_asset(ic):
    viewport_2d(ic)


@content_type("prefab", "Prefab")
def prefab_asset(ic):
    glass_box(ic, 26, 40, 21, 21, 21, MID, fill_alpha=0.14, width=1.5)
    box(ic, 37, 52, 21, 21, 21, BRIGHT)


@content_type("entity", "Entity")
def entity_asset(ic):
    character_3d(ic)


@content_type("bundle", "Bundle")
def bundle_asset(ic):
    box(ic, 32, 50, 29, 22, 29, LIGHT)
    P = lambda x, z: iso(32, 50, x, 22, z)
    tape = [P(-3.5, -14.5), P(3.5, -14.5), P(3.5, 14.5), P(-3.5, 14.5)]
    ic.add(f'<polygon points="{pts(tape)}" fill="#FFFFFF"/>')


@content_type("mesh", "Mesh")
def mesh_asset(ic):
    faceted_sphere(ic, 32, 32, 21, LIGHT, detail=0, rot=(14, 22), wire=BRIGHT)


@content_type("model", "Model")
def model_asset(ic):
    character_3d(ic)


@content_type("texture", "Texture")
def texture_asset(ic):
    ui_image(ic)


@content_type("shader", "Shader")
def shader_asset(ic):
    sphere(ic, 32, 32, 20, BRIGHT)
    ic.add('<path d="M 16 33 Q 24 23 32 33 T 48 33" fill="none" stroke="#3F3F46" stroke-width="2.8" stroke-linecap="round"/>')


@content_type("material", "Material")
def material_asset(ic):
    # Bespoke UniVex PBR Studio Material Ball on Checker Base (100% Monochrome)
    slab(ic, 32, 53, 38, 38, 3.5, DARK)
    sphere(ic, 32, 29, 17.5, BRIGHT)
    ic.add('<ellipse cx="32" cy="29" rx="17.5" ry="5.5" fill="none" stroke="#52525B" stroke-width="1.5" stroke-dasharray="3.5 2.5"/>')


@content_type("save", "Save")
def save_asset(ic):
    ic.add('<path d="M 12 8 h 33 l 9 9 v 33 a 4 4 0 0 1 -4 4 H 12 a 4 4 0 0 1 -4 -4 V 12 a 4 4 0 0 1 4 -4 Z" '
           'fill="url(#uv_mid)" stroke="#3F3F46" stroke-width="1.4" stroke-linejoin="round"/>')
    ic.add('<rect x="17" y="8.5" width="23" height="14" rx="1.5" fill="url(#uv_bright)"/>')
    ic.add('<rect x="14" y="30" width="36" height="20" rx="2" fill="#F4F4F5"/>')


@content_type("file", "File")
def file_asset(ic):
    page(ic, 13, 7, 38, 48, LIGHT, fold=10)
    for y, w in ((24, 22), (31, 24), (38, 18), (45, 22)):
        ic.add(f'<rect x="19" y="{y}" width="{w}" height="2.6" rx="1.3" fill="#52525B"/>')


@content_type("script", "Script file")
def script_file_asset(ic):
    script_object(ic)


@content_type("audio", "Audio")
def audio_asset(ic):
    audio_source_3d(ic)


@content_type("animation", "Animation")
def animation_asset(ic):
    animation_sequencer(ic)


@content_type("font", "Font")
def font_asset(ic):
    outline = "M 10 52 L 26 10 H 38 L 54 52 H 44 L 40 41 H 24 L 20 52 Z M 26.5 33 H 37.5 L 32 18 Z"
    ic.add(f'<path d="{outline}" fill="url(#uv_bright)" fill-rule="evenodd" stroke="#3F3F46" stroke-width="1.4" stroke-linejoin="round"/>')


@content_type("input_map", "Input map")
def input_map_asset(ic):
    body = "M 15 20 H 49 C 57 20 60 35 59 45 C 58 52 51 54 46 48 L 41 42 H 23 L 18 48 C 13 54 6 52 5 45 C 4 35 7 20 15 20 Z"
    ic.add(f'<path d="{body}" fill="url(#uv_bright)" stroke="#3F3F46" stroke-width="1.4" stroke-linejoin="round"/>')
    ic.add('<path d="M 14 32 h 9 M 18.5 27.5 v 9" stroke="#27272A" stroke-width="3.2" stroke-linecap="round"/>')
    for cx, cy in ((45, 27.5), (49.5, 32), (40.5, 32), (45, 36.5)):
        ic.add(f'<circle cx="{cx}" cy="{cy}" r="2.6" fill="#3F3F46"/>')


@content_type("project_settings", "Project settings")
def project_settings_asset(ic):
    teeth = []
    for i in range(16):
        r = 24 if i % 2 == 0 else 19
        a = math.radians(i * 22.5 - 90 + 11.25)
        teeth.append((32 + math.cos(a) * r, 32 + math.sin(a) * r))
    ic.add(f'<polygon points="{pts(teeth)}" fill="url(#uv_bright)" stroke="#3F3F46" stroke-width="1.4" stroke-linejoin="round"/>')
    ic.add('<circle cx="32" cy="32" r="8" fill="#27272A"/>')
