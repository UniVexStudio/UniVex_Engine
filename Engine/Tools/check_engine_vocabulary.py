#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""Enforces the engine-vocabulary boundary: names this engine has retired stay retired.

WHY THIS EXISTS. UniVex's architecture is its own - archetype/chunk ECS with a stateless
scene-graph facade (see FOUNDATION.md, "Entity/Scene"), a data-driven kind registry, and its own
scripting language. What it kept borrowing from other engines was only the VOCABULARY sitting on
top of that architecture. Each rename pass removes some of those names; without a check, the next
feature re-introduces one six months later and nobody notices, because a borrowed name compiles
exactly as well as an original one.

This is a ratchet, not a snapshot. The rule is: when you rename something, add the old name to
RETIRED below in the same commit. The list only ever grows, and CI refuses to let a retired name
come back.

TWO tiers, deliberately:

  * RETIRED - hard failure. Names this engine USED to have and has already removed. Any
    occurrence outside ALLOWED_PREFIXES is a regression. Zero false positives by construction,
    because these are our own dead names, not a guess about another engine's vocabulary.

  * FOREIGN_CLASS_NAMES - reported, never fails. Class names belonging to other engines that
    still appear in the tree, either as open findings (see GODOT_STYLE_AUDIT.md) or inside the
    ~35 comparative design comments that name another engine precisely in order to explain how
    UniVex differs. Failing on those would force deleting information, so they are surfaced as a
    count with a file list instead.

Exits non-zero and prints every violation. Run from anywhere:
    python3 Engine/Tools/check_engine_vocabulary.py [repo_root]
"""

import re
import sys
from collections import Counter
from pathlib import Path

SOURCE_EXTENSIONS = {".h", ".hpp", ".cpp", ".c", ".cc", ".cxx", ".inl"}
BUILD_EXTENSIONS = {".txt", ".cmake"}
SKIP_DIRS = {".git", "build", "node_modules", ".vs", "out", "_deps"}

# Names this engine has retired. Each entry is (old_name, what replaced it). Add to this list in
# the same commit that performs a rename - that is the whole mechanism.
RETIRED = {
    # The scene-object pass: the word "Node" left the engine's own types, files and labels.
    "SceneNodeKindUVE": "SceneObjectKindUVE",
    "NodeMetadataComponentUVE": "ObjectMetadataComponentUVE",
    "SceneNodeTypeComponentUVE": "SceneObjectTypeComponentUVE",
    # The Godot-derived enum names (GODOT_STYLE_AUDIT.md Finding A). Persisted by number, so the
    # rename is source-only - which is exactly why nothing on disk stops a reintroduction.
    "ProcessModeUVE": "TickModeUVE",
    "AutoTranslateModeUVE": "LocalizeModeUVE",
    "PhysicsInterpolationModeUVE": "PoseSmoothingUVE",
    "IsProcessingUVE": "IsTickingUVE",
    "ResolveProcessModeUVE": "ResolveTickModeUVE",
    "ResolveAutoTranslateModeUVE": "ResolveLocalizeModeUVE",
    # The variant pass (GODOT_STYLE_AUDIT.md Finding C). "Packed*Array" is another engine's family
    # verbatim - all nine members, same order, minus one - and "StringName" is its name for an
    # interned string. Variant types are persisted BY NAME, so each of these is a load-time alias
    # in TryParseVariantTypeNameUVE, never a silent break.
    "PackedByteArray": "ByteArray",
    "PackedInt32Array": "Int32Array",
    "PackedInt64Array": "Int64Array",
    "PackedFloat32Array": "Float32Array",
    "PackedFloat64Array": "Float64Array",
    "PackedStringArray": "StringArray",
    "PackedVector2Array": "Vector2Array",
    "PackedVector3Array": "Vector3Array",
    "PackedColorArray": "ColorArray",
    "StringName": "InternedString",
    # The Godot class name used as a folder and library name (Finding F).
    "CanvasLayer": "Canvas (folder Engine/Runtime/Objects/UI/Expose/uve/objects/canvas)",
    "all_objects_canvas_layer_uve.h": "all_objects_canvas_uve.h",
    "uve_nodes_canvas_layer": "uve_objects_canvas",
    "uve_nodes_3d": "uve_objects_3d",
    # Godot's event vocabulary in the editor chrome (Finding G).
    "EntityEditorTabUVE::Signals": "EntityEditorTabUVE::Events",
    "EditorRightPanelTabUVE::Signals": "EditorRightPanelTabUVE::Events",
}

# Whole families retired at once (GODOT_STYLE_AUDIT.md Finding B). Listing every derived symbol -
# AnimationTreeComponentUVE, AnimationTreeObjectUVE, IsAnimationTreeComponentValidUVE,
# SetAnimationTreeParameterUVE, StepSkeletalAnimationTreeUVE, EvaluateAnimationTreeUVE and thirty
# more - would bury the intent, so these match as an identifier PREFIX instead of a whole word.
# Each stem was checked against the tree before being retired: nothing legitimate begins with it.
RETIRED_STEMS = {
    "AnimationTree": "AnimationGraph",
    "AnimationPlayer": "AnimationSequencer",
    "RigidBody": "Rigid3D",
    "AnimatableBody": "Kinematic",
}

# The three legacy-alias tables MUST keep the old strings forever, because a saved document written
# before a rename still has to load. These are the only places a retired name may legitimately
# appear; every one of them is a string literal feeding a load-time alias, never a live type.
ALLOWED_PREFIXES = (
    "Engine/Runtime/Scene/Internal/scene_serializer_uve.cpp",
    "Engine/Runtime/Objects/Core/Internal/variant_uve.cpp",
    "Engine/Runtime/Scene/Internal/objects/scene_object_registry_uve.cpp",
    # The test that proves those aliases work has to name what it is proving.
    "Test/Integration/Object/variant_uve_tests.cpp",
    # This tool's own denylist obviously names what it forbids.
    "Engine/Tools/check_engine_vocabulary.py",
)

# Other engines' class names that still occur in the tree. Reported, not enforced - see the module
# docstring for why. Kept here so the count is honest rather than invented at review time.
FOREIGN_CLASS_NAMES = (
    "AnimationMixerComponentUVE",
    "MeshInstance3D",
    "SpringArm3D",
    "BoneAttachment3D",
    "Marker3D",
    "NavigationRegion3D",
    "NavigationAgent3D",
    "RayCast3D",
    "Occluder3D",
    "Area3D",
    "Skeleton3D",
    "Camera3D",
    "DirectionalLight3D",
    "Viewport",
    "is_on_floor",
)


def iter_files(repo_root: Path, extensions):
    for path in repo_root.rglob("*"):
        if not path.is_file() or path.suffix not in extensions:
            continue
        if SKIP_DIRS & set(path.parts):
            continue
        yield path


def main() -> int:
    repo_root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]

    retired_patterns = {
        name: re.compile(r"(?<![A-Za-z0-9_])" + re.escape(name) + r"(?![A-Za-z0-9_])")
        for name in RETIRED
    }
    # Prefix match on purpose: the stem retires a whole derived family, not one identifier.
    retired_stem_patterns = {
        stem: re.compile(r"(?<![A-Za-z0-9_])" + re.escape(stem)) for stem in RETIRED_STEMS
    }
    foreign_patterns = {
        name: re.compile(r"(?<![A-Za-z0-9_])" + re.escape(name) + r"(?![A-Za-z0-9_])")
        for name in FOREIGN_CLASS_NAMES
    }

    violations = []
    foreign_hits = Counter()
    foreign_files = Counter()

    for path in iter_files(repo_root, SOURCE_EXTENSIONS | BUILD_EXTENSIONS):
        rel = path.relative_to(repo_root).as_posix()
        try:
            lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
        except OSError:
            continue

        allowed = rel.startswith(ALLOWED_PREFIXES)
        for number, line in enumerate(lines, start=1):
            for name, pattern in retired_patterns.items():
                if pattern.search(line) and not allowed:
                    violations.append(
                        f"{rel}:{number}: retired name '{name}' (now {RETIRED[name]}).\n"
                        f"    {line.strip()}\n"
                        f"    Rename it, or - if this is a load-time alias for a saved file - move it\n"
                        f"    into one of the three legacy-alias tables and add that file to\n"
                        f"    ALLOWED_PREFIXES in Engine/Tools/check_engine_vocabulary.py."
                    )
            for stem, pattern in retired_stem_patterns.items():
                if pattern.search(line) and not allowed:
                    violations.append(
                        f"{rel}:{number}: retired name stem '{stem}*' (now {RETIRED_STEMS[stem]}*).\n"
                        f"    {line.strip()}\n"
                        f"    This whole family was renamed; rename this identifier too, or move it\n"
                        f"    into one of the three legacy-alias tables and add that file to\n"
                        f"    ALLOWED_PREFIXES in Engine/Tools/check_engine_vocabulary.py."
                    )
            if rel.startswith("Engine/") or rel.startswith("Test/"):
                for name, pattern in foreign_patterns.items():
                    if pattern.search(line):
                        foreign_hits[name] += 1
                        foreign_files[rel] += 1

    if violations:
        print("engine vocabulary check FAILED:")
        for violation in violations:
            print(f"\n  {violation}")
        return 1

    print("engine vocabulary check passed")
    print(
        f"  {len(RETIRED)} retired names and {len(RETIRED_STEMS)} retired name stems enforced, "
        "0 reintroductions."
    )
    if foreign_hits:
        total = sum(foreign_hits.values())
        print(
            f"  {total} occurrences of {len(foreign_hits)} foreign class names remain "
            f"(reported, not enforced - see GODOT_STYLE_AUDIT.md):"
        )
        for name, count in foreign_hits.most_common(8):
            print(f"    {name:<32} {count}")
        print(f"    ...across {len(foreign_files)} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
