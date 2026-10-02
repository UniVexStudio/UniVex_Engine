// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

inline constexpr std::size_t kMaximumLodLevelsUVE = 8U;

/// Distance-based detail selection for one entity.
///
/// WHAT IT DOES TODAY. The renderer resolves `currentLevel` from the camera distance every frame,
/// and skips the entity entirely once it is past the last threshold. That culling is the part
/// that pays immediately: a culled object costs no placement, no plane test and no draw call.
/// Measured on a 2000-object, 200 m scene - 74% culled at a 50 m draw distance, 50% at 100 m.
///
/// WHAT IT IS READY FOR. `currentLevel` is the index a mesh swap would use. MeshComponentUVE
/// holds a single mesh GUID today, so there is nothing to swap TO - adding LOD meshes is an asset
/// pipeline job (import, generation, storage), not a node's. The level is computed and published
/// now so that work lands as a consumer rather than a redesign, and so the Inspector can show
/// which level an object is on before any of it exists.
struct LodGroup3DNodeComponentUVE final {
    /// Distance at which each level takes over, nearest first. An entity beyond
    /// `distanceThresholds[levelCount - 1]` is not drawn at all.
    ///
    /// The array is fixed-size rather than a vector so the component stays trivially copyable and
    /// cache-friendly - it is read once per entity per frame in the extraction walk, which is the
    /// hottest loop in the renderer.
    std::array<float, kMaximumLodLevelsUVE> distanceThresholds{10.0F, 25.0F, 60.0F, 120.0F, 240.0F, 480.0F, 960.0F, 1920.0F};

    /// How many entries of `distanceThresholds` are in use. Levels beyond this are ignored, so an
    /// author can shorten a chain without having to rewrite the distances.
    std::uint8_t levelCount = 4U;

    /// Resolved each frame from the camera distance. Derived, not authored: writing it by hand has
    /// no effect, because the next extraction overwrites it.
    std::uint8_t currentLevel = 0U;

    /// True when the entity is past the last threshold and should not be drawn. Derived alongside
    /// `currentLevel` for the same reason visibility keeps a separate resolved field - a consumer
    /// needs the ANSWER, not the inputs and the rule.
    bool culledByDistance = false;

    /// Turns the whole group off. A disabled group draws at level 0 and is never distance-culled,
    /// which is what an author wants while placing or debugging an object.
    bool enabled = true;
};

/// Resolves which level `distanceToCamera` falls in, and whether the entity is past the end of
/// the chain entirely.
///
/// Pure and out-of-line so the rule has one home and can be tested without a renderer. Returns
/// level 0 and not-culled for a disabled group, a zero-length chain, or a non-finite distance -
/// every degenerate case draws the object at full detail rather than hiding it, because a
/// configuration mistake should be visible, not invisible.
void ResolveLodGroup3DLevelUVE(LodGroup3DNodeComponentUVE& value, float distanceToCamera) noexcept;

[[nodiscard]] bool IsLodGroup3DNodeComponentValidUVE(const LodGroup3DNodeComponentUVE& value) noexcept;

} // namespace UVE::Scene
