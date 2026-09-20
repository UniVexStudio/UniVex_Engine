// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

/// A VisibilityRegion3D: an authored axis-aligned box (halfExtents around the node's world
/// position) that owns the interior culling verdict for the meshes inside it - Godot has no
/// built-in equivalent at all (Godot's VisibilityNotifier3D only says whether a box is on
/// screen; Unreal's Precomputed Visibility Volume solves the same problem but needs an offline
/// cook). This one is live and evaluated every tick.
///
/// Semantics: the engine computes `active` each tick - true while at least one viewer (the
/// active camera or any character controller, the same viewer cloud the streamer and the world
/// partition use) stands INSIDE the box, or while there are no viewers at all (fail-open: an
/// empty world shows everything). A mesh whose position lands inside the box AND whose
/// MeshComponentUVE::visibilityLayers mask attracts this region's mask becomes a member: it
/// renders only while the region is `active`. Walking out of the box releases the membership
/// back to live on the same tick (see SyncVisibilityRegion3DNodesUVE), so a stale verdict can
/// never hide content. Interior content with an external camera is skipped with zero render
/// work - the whole point of room-scale culling.
struct VisibilityRegion3DNodeComponentUVE final {
    Math::Vector3UVE halfExtents{10.0F, 10.0F, 10.0F};
    /// Which MeshComponentUVE::visibilityLayers bits this region manages. The intersection is
    /// what lets one room manage its props but not the NPC walking through it, without a
    /// special node for each. A 0 mask manages NOTHING - an authoring choice, not an error.
    std::uint32_t visibilityLayers = 0xFFFFFFFFU;
    bool enabled = true;
    // Runtime-only: whether at least one viewer stood inside this tick. The serializer keeps
    // authored payloads only (ToJson never writes it, FromJson never reads it), so it starts at
    // true on every fresh load and the sync rewrites it the same tick; authoring it would have
    // no lasting effect.
    bool active = true;
};

[[nodiscard]] bool IsVisibilityRegion3DNodeComponentValidUVE(const VisibilityRegion3DNodeComponentUVE& value) noexcept;

// Runtime-only membership, owned by EngineCoreUVE::SyncVisibilityRegion3DNodesUVE() and
// ATTACHED BY THE ENGINE to meshes inside a region that the layer gate lets it manage. Never
// authored (a scene file cannot carry it - the serializer never registered it): `region` is the
// deciding region entity, `live` is this tick's verdict. The MeshRendererUVE candidate walk
// consults this through ResolveVisibilityRegion3DMembershipLiveUVE() below.
struct VisibilityRegion3DMembershipComponentUVE final {
    Scene::EntityUVE region = Scene::kInvalidEntityUVE;
    bool live = true;
};

// The layer gate, pure and measurable: a region manages a mesh only when the masks share at
// least one bit (region on default 0xFFFFFFFF manages everything the OLD defaults asked for; a
// mesh on the new default 0x00000001 matches every region mask whose layer-0 bit is set).
[[nodiscard]] inline constexpr bool ResolveVisibilityRegion3DLayerGateUVE(
    std::uint32_t regionLayers, std::uint32_t meshLayers) noexcept {
    return (regionLayers & meshLayers) != 0U;
}

// Point containment, in world space, axis-aligned (a rotated region node is NOT folded into the
// box - the same world-aligned rule WorldPartition3D documents, so content authored against one
// coarse volume system reads identically against the other). The boundary is INCLUDED (|d| <=
// halfExtent), so a mesh pasted exactly on the wall is managed, not abandoned. An invalid
// config or non-finite pose answers false - an unusable region manages nothing.
[[nodiscard]] bool ResolveVisibilityRegion3DContainsPointUVE(
    const VisibilityRegion3DNodeComponentUVE& config, const Math::Vector3UVE& regionWorldPosition,
    const Math::Vector3UVE& pointWorld) noexcept;

// Whether ANY viewer stands inside, pure. An empty viewer list answers false here; the NO-
// VIEWERS-AT-ALL fail-open ("no eye, nothing to manage emptiness for - show everything") is the
// engine's policy layer on top, so the pure function can stay total and the policy stays in one
// place per tick.
[[nodiscard]] bool ResolveVisibilityRegion3DAnyViewerInsideUVE(
    const VisibilityRegion3DNodeComponentUVE& config, const Math::Vector3UVE& regionWorldPosition,
    const std::vector<Math::Vector3UVE>& viewerPositions) noexcept;

// The renderer-side verdict for one membership entry. Alive owner: trust the live flag. Dead
// or disabled-out owner (the region was destroyed, or its document unloaded): fail OPEN - its
// residual opinion must not hide content forever (the same rule WorldPartition3D's membership
// keeps, so the two engines-measured systems fail in the same direction).
[[nodiscard]] inline constexpr bool ResolveVisibilityRegion3DMembershipLiveUVE(
    bool regionOwnerAlive, bool live) noexcept {
    return live || !regionOwnerAlive;
}

} // namespace UVE::Scene
