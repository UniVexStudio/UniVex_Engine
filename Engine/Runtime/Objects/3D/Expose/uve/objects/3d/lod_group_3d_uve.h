// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "uve/asset/asset_guid_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

inline constexpr std::size_t kMaximumLodLevelsUVE = 8U;

/// The largest hysteresis band an author can ask for, as a fraction of a threshold. At 0.5 a
/// level's entry point is half again its exit point, which is already a very wide band; anything
/// more would start to overlap the neighbouring level's band.
inline constexpr float kMaximumLodHysteresisUVE = 0.5F;

/// Distance-based detail selection for one entity.
///
/// WHAT IT DOES. The renderer resolves `currentLevel` from the camera distance every frame, draws
/// that level's mesh, and skips the entity entirely once it is past the last threshold. The level
/// is resolved with a hysteresis band so an object drifting across a threshold does not swap meshes
/// on alternate frames - the band is what makes the switch a decision instead of a coin flip.
///
/// WHICH MESH. `lodMeshGuids[level]` names the mesh to draw at that level; an empty slot falls back
/// to the entity's own MeshComponentUVE mesh, which is why an author who assigns only levels 1..N
/// still gets the full-detail mesh at level 0. The renderer reads the resolved GUID through
/// ResolveLodGroup3DMeshGuidUVE(): one call, so the "which mesh" rule has one home and cannot drift
/// between the extraction walk and anything else that draws.
///
/// The resolved fields are derived, not authored: writing `currentLevel`, `culledByDistance` or the
/// mesh choice by hand has no effect, because the next extraction overwrites them.
struct LodGroup3DComponentUVE final {
    /// Distance at which each level takes over, nearest first. An entity beyond
    /// `distanceThresholds[levelCount - 1]` is not drawn at all.
    ///
    /// The array is fixed-size rather than a vector so the component stays trivially copyable and
    /// cache-friendly - it is read once per entity per frame in the extraction walk, which is the
    /// hottest loop in the renderer.
    std::array<float, kMaximumLodLevelsUVE> distanceThresholds{10.0F, 25.0F, 60.0F, 120.0F, 240.0F, 480.0F, 960.0F, 1920.0F};

    /// The mesh drawn at each level, index for index with `distanceThresholds`. An invalid GUID
    /// means "draw the entity's MeshComponentUVE mesh at this level", so a group only has to name
    /// the levels it actually replaces detail on.
    std::array<Asset::AssetGuidUVE, kMaximumLodLevelsUVE> lodMeshGuids{};

    /// How many entries of `distanceThresholds` and `lodMeshGuids` are in use. Levels beyond this
    /// are ignored, so an author can shorten a chain without having to rewrite the distances.
    std::uint8_t levelCount = 4U;

    /// Relative width of the band around every threshold, as a fraction of that threshold. A level
    /// is entered at `threshold * (1 + hysteresis)` and left at `threshold * (1 - hysteresis)`;
    /// between the two the previous level is kept, which is what stops a mesh swap from chattering
    /// while an object hovers on a boundary. Zero is the stateless rule - the level a distance
    /// maps to, with no memory - and is the default, so an author opts in per object.
    float hysteresis = 0.0F;

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
///
/// The component's own `currentLevel`/`culledByDistance` are the previous answer and the only state
/// the rule has: hysteresis needs to know where the object is coming from, and reading it from the
/// value being resolved keeps the call site a single statement. A `hysteresis` of 0 is the
/// stateless rule an object resolved by before the field existed - the level a distance maps to,
/// with no memory - so opting in is the only way to get a band, and nothing changes for anyone who
/// does not.
void ResolveLodGroup3DLevelUVE(LodGroup3DComponentUVE& value, float distanceToCamera) noexcept;

/// The mesh to draw for the level `value` currently resolves to. `baseMeshGuid` is the entity's
/// MeshComponentUVE mesh, used for every level the group does not override - including every level
/// when it overrides none - so the fallback is one rule rather than a special first level.
[[nodiscard]] Asset::AssetGuidUVE ResolveLodGroup3DMeshGuidUVE(
    const LodGroup3DComponentUVE& value, const Asset::AssetGuidUVE& baseMeshGuid) noexcept;

[[nodiscard]] bool IsLodGroup3DObjectComponentValidUVE(const LodGroup3DComponentUVE& value) noexcept;

struct LodGroup3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "LODGroup3D";
    LodGroup3DComponentUVE lod{};
};

void ApplyLodGroup3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                        const LodGroup3DObjectDefinitionUVE& value);

} // namespace UVE::Scene
