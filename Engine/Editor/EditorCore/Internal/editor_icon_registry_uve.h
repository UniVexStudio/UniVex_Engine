// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <string_view>

#include "editor_node_icons_uve.h"

namespace UVE::Editor {

/// Name-keyed lookup for the editor's node icons: IconUVE("camera") instead of
/// HierarchyNodeIconKindUVE::Camera.
///
/// WHY. The icons were reachable only through an enum, so every place that wanted one had to know
/// the enumerator, and every new icon meant touching the enum, the dispatch switch and each
/// caller. A name is the thing an author actually has - a node type, an asset kind, a component -
/// and looking one up is how the rest of this engine already addresses assets.
///
/// WHAT THIS IS NOT. It does not replace the drawing. The glyphs are still the procedural
/// ImDrawList painting in editor_node_icons_uve.h, and not one of them is redrawn here. Swapping
/// those bodies for rasterised artwork later is a change to that file alone - this header keeps
/// working, because a name is exactly the indirection that makes the backend replaceable. Doing
/// the naming first and the artwork second is deliberate: the naming is cheap, reversible and
/// testable, while a rasteriser is a dependency, a cache and a DPI problem.
///
/// Names are lowercase and hyphen-free by convention so that a caller never has to guess at
/// casing. Lookup is a linear scan of a dozen entries, which is not worth a hash: it runs once per
/// drawn row, against a table that fits in a cache line or two.

/// One named icon: the name an author writes, and the glyph it resolves to.
struct EditorIconEntryUVE final {
    std::string_view name;
    HierarchyNodeIconKindUVE kind = HierarchyNodeIconKindUVE::Node3D;
};

/// The registry. Adding an icon means adding one row here and one glyph in
/// editor_node_icons_uve.h - the enum and the dispatch switch are no longer a caller's problem.
///
/// Every HierarchyNodeIconKindUVE has at least one name, which a test enforces: an enumerator
/// with no name would be an icon that exists and cannot be asked for. Several have aliases,
/// because the same glyph legitimately answers to more than one word depending on which panel is
/// asking - "light" and "sun" are the same picture, and forcing one vocabulary on both the
/// outliner and the inspector would just move the guessing somewhere else.
inline constexpr std::array<EditorIconEntryUVE, 18U> kEditorIconRegistryUVE{{
    {"mesh", HierarchyNodeIconKindUVE::Mesh},
    {"model", HierarchyNodeIconKindUVE::Mesh},
    {"camera", HierarchyNodeIconKindUVE::Camera},
    {"light", HierarchyNodeIconKindUVE::Light},
    {"sun", HierarchyNodeIconKindUVE::Light},
    {"environment", HierarchyNodeIconKindUVE::Environment},
    {"world", HierarchyNodeIconKindUVE::Environment},
    {"physics", HierarchyNodeIconKindUVE::Physics},
    {"collider", HierarchyNodeIconKindUVE::Physics},
    {"audio", HierarchyNodeIconKindUVE::Audio},
    {"sound", HierarchyNodeIconKindUVE::Audio},
    {"particle", HierarchyNodeIconKindUVE::Particle},
    {"script", HierarchyNodeIconKindUVE::Script},
    {"animation", HierarchyNodeIconKindUVE::Animation},
    {"node_3d", HierarchyNodeIconKindUVE::Node3D},
    {"node3d", HierarchyNodeIconKindUVE::Node3D},
    {"node", HierarchyNodeIconKindUVE::Node3D},
    // Legacy name from before the kind rename: kept resolving so old layouts and docs that say
    // "empty" still pick the base-node glyph.
    {"empty", HierarchyNodeIconKindUVE::Node3D},
}};

/// Resolves a name to its glyph, falling back to the empty-node glyph for anything unknown.
///
/// A miss draws the neutral placeholder rather than nothing and rather than asserting. An icon is
/// decoration: a typo in a name should leave the row readable with a generic marker, not blank
/// out the outliner or take the editor down. The fallback is also what makes it safe for a panel
/// to ask for an icon it is not certain exists yet.
[[nodiscard]] constexpr HierarchyNodeIconKindUVE ResolveEditorIconUVE(const std::string_view name) noexcept {
    for (const EditorIconEntryUVE& entry : kEditorIconRegistryUVE) {
        if (entry.name == name) {
            return entry.kind;
        }
    }
    return HierarchyNodeIconKindUVE::Node3D;
}

/// True when `name` is registered. Separate from ResolveEditorIconUVE because that one cannot
/// distinguish "unknown name" from "the base icon was asked for by name" - both return Node3D, and
/// a caller validating author-supplied text needs to tell those apart.
[[nodiscard]] constexpr bool IsEditorIconRegisteredUVE(const std::string_view name) noexcept {
    for (const EditorIconEntryUVE& entry : kEditorIconRegistryUVE) {
        if (entry.name == name) {
            return true;
        }
    }
    return false;
}

/// Draws a named icon. The thin wrapper that lets a caller say what it wants a picture OF rather
/// than which enumerator to pass.
inline void DrawNamedIconUVE(ImDrawList& drawList, const ImVec2 center, const float radius,
                             const std::string_view name, const std::uintptr_t sunTextureId = 0U,
                             const std::uintptr_t environmentTextureId = 0U) {
    DrawHierarchyNodeIconUVE(drawList, center, radius, ResolveEditorIconUVE(name), sunTextureId,
                             environmentTextureId);
}

} // namespace UVE::Editor
