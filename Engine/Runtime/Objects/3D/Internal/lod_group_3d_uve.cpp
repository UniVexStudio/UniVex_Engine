// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/lod_group_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"

namespace UVE::Scene {

namespace {

/// The hysteresis to actually resolve with. A non-finite or negative value is a configuration
/// mistake: it resolves as no band at all, which is the behaviour that was there before the field,
/// rather than an arbitrary band nobody asked for.
[[nodiscard]] float EffectiveHysteresisUVE(const float declared) noexcept {
    if (!std::isfinite(declared) || declared <= 0.0F) {
        return 0.0F;
    }
    return std::min(declared, kMaximumLodHysteresisUVE);
}

} // namespace

bool IsLodGroup3DObjectComponentValidUVE(const LodGroup3DComponentUVE& value) noexcept {
    if (value.levelCount == 0U || value.levelCount > kMaximumLodLevelsUVE || value.currentLevel >= value.levelCount) {
        return false;
    }
    if (!std::isfinite(value.hysteresis) || value.hysteresis < 0.0F || value.hysteresis > kMaximumLodHysteresisUVE) {
        return false;
    }
    for (std::size_t index = 0U; index < value.levelCount; ++index) {
        if (!std::isfinite(value.distanceThresholds[index]) || value.distanceThresholds[index] < 0.0F ||
            (index > 0U && value.distanceThresholds[index] <= value.distanceThresholds[index - 1U])) {
            return false;
        }
    }
    return true;
}

void ApplyLodGroup3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                        const LodGroup3DObjectDefinitionUVE& value) {
    ApplyObject3DRecipeUVE(entityManager, entity, LodGroup3DObjectDefinitionUVE::defaultName);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<LodGroup3DComponentUVE>(entity)) {
        entityManager.AddComponentUVE<LodGroup3DComponentUVE>(entity, value.lod);
    }
}

void ResolveLodGroup3DLevelUVE(LodGroup3DComponentUVE& value, const float distanceToCamera) noexcept {
    // Every degenerate case resolves to "draw at full detail". A configuration mistake should be
    // visible so it gets fixed, not silently hide geometry and look like a missing asset.
    if (!value.enabled || value.levelCount == 0U || value.levelCount > kMaximumLodLevelsUVE ||
        value.currentLevel >= value.levelCount || !std::isfinite(distanceToCamera)) {
        value.currentLevel = 0U;
        value.culledByDistance = false;
        return;
    }

    const float hysteresis = EffectiveHysteresisUVE(value.hysteresis);

    // No band: the level a distance maps to, full stop. This is the stateless scan the renderer
    // always used, kept as its own path rather than folded into the walk below, so an object that
    // never opts into hysteresis resolves exactly as it always did - including on an exact
    // threshold, where a walk can only ever say "keep what you had".
    if (hysteresis == 0.0F) {
        // Thresholds are ascending - the validator enforces it - so the first one the distance fits
        // under is the level. A linear scan over at most eight entries beats anything cleverer: it
        // is branch-predictable and the array is a single cache line.
        for (std::uint8_t level = 0U; level < value.levelCount; ++level) {
            if (distanceToCamera <= value.distanceThresholds[level]) {
                value.currentLevel = level;
                value.culledByDistance = false;
                return;
            }
        }

        // Past the end of the chain. currentLevel stays at the last real level rather than running
        // one past it, so a consumer that indexes by level cannot walk off the end just because an
        // object moved too far away.
        value.currentLevel = static_cast<std::uint8_t>(value.levelCount - 1U);
        value.culledByDistance = true;
        return;
    }

    const std::uint8_t lastLevel = static_cast<std::uint8_t>(value.levelCount - 1U);
    const float lastThreshold = value.distanceThresholds[lastLevel];

    // The previous answer is this value's own currentLevel/culledByDistance - the only state the
    // rule carries, which is what lets hysteresis be decided here instead of by every caller.
    std::uint8_t level = value.currentLevel;

    // Coming back from a cull has its own band, and it is the one place the walk below cannot be
    // trusted on its own: while culled, currentLevel was left at the last level, so a distance that
    // has not clearly come back would look like "still on the last level" and the walk would leave
    // it culled anyway. Asking the question first keeps a culled object culled until it is inside
    // the band, and then hands the level choice to the walk - so an object that comes back to a
    // nearer threshold un-culls and switches level on the same frame.
    if (value.culledByDistance && distanceToCamera > lastThreshold * (1.0F - hysteresis)) {
        value.culledByDistance = true;
        return;
    }

    // Away from the camera: a level is entered past its threshold by the whole band. Walking one
    // step at a time rather than jumping to the threshold the distance lands in is what makes the
    // band apply per crossing - a distance that jumps several levels still satisfies every crossed
    // step's test.
    while (level < lastLevel && distanceToCamera > value.distanceThresholds[level] * (1.0F + hysteresis)) {
        ++level;
    }

    // Towards the camera: a level is left only once the distance is under its threshold by the
    // whole band. The two walks cannot fight - one moves away, the other back, and the band between
    // them is the region where neither fires, which is the entire point of the band.
    while (level > 0U && distanceToCamera < value.distanceThresholds[level - 1U] * (1.0F - hysteresis)) {
        --level;
    }

    value.currentLevel = level;

    // Culled only from the end of the chain, and only past the far side of the last threshold's
    // band. A level that stepped back down is not culled by definition.
    value.culledByDistance = level == lastLevel && distanceToCamera > lastThreshold * (1.0F + hysteresis);
}

Asset::AssetGuidUVE ResolveLodGroup3DMeshGuidUVE(const LodGroup3DComponentUVE& value,
                                                 const Asset::AssetGuidUVE& baseMeshGuid) noexcept {
    // A level outside the chain cannot be read from, and a group with no levels has nothing to say:
    // both fall back to the entity's own mesh rather than to a stale or out-of-range slot.
    if (value.levelCount == 0U || value.levelCount > kMaximumLodLevelsUVE || value.currentLevel >= value.levelCount) {
        return baseMeshGuid;
    }
    const Asset::AssetGuidUVE& levelMesh = value.lodMeshGuids[value.currentLevel];
    return levelMesh == Asset::kInvalidAssetGuidUVE ? baseMeshGuid : levelMesh;
}

} // namespace UVE::Scene
