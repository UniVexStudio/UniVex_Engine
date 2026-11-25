// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "mesh_render_eligibility.h"

#include <cmath>

#include "asset_guid.h"
#include "mesh_component.h"
#include "quaternion.h"
#include "vector3.h"

namespace UVE::Render {
namespace {

constexpr std::size_t kNearPlaneIndex = 4U;

[[nodiscard]] bool IsOrderedFiniteAabb(const Math::Aabb& bounds) noexcept {
    return Math::IsFinite(bounds.min) && Math::IsFinite(bounds.max) && bounds.min.x <= bounds.max.x &&
           bounds.min.y <= bounds.max.y && bounds.min.z <= bounds.max.z;
}

} // namespace

bool EvaluateMeshRenderPlacement(const Scene::MeshComponent& meshComponent,
                                    const Scene::WorldTransformComponent& worldTransform,
                                    const Asset::MeshAsset& mesh,
                                    MeshRenderPlacement& outPlacement) noexcept {
    MeshRenderPlacement candidate;
    if (!Scene::IsMeshComponentValid(meshComponent) ||
        meshComponent.meshGuid == Asset::kInvalidAssetGuid ||
        meshComponent.materialGuid == Asset::kInvalidAssetGuid) {
        candidate.reason = MeshRenderEligibilityReason::InvalidAssetReferences;
        outPlacement = candidate;
        return false;
    }
    if (!Math::IsFinite(worldTransform.worldPosition) || !Math::IsFinite(worldTransform.worldScale) ||
        !Math::IsFinite(worldTransform.worldRotation)) {
        candidate.reason = MeshRenderEligibilityReason::InvalidWorldTransform;
        outPlacement = candidate;
        return false;
    }
    Math::Quaternion normalizedRotation;
    if (!Math::TryNormalize(worldTransform.worldRotation, normalizedRotation)) {
        candidate.reason = MeshRenderEligibilityReason::InvalidWorldTransform;
        outPlacement = candidate;
        return false;
    }
    if (!IsOrderedFiniteAabb(mesh.localBounds)) {
        candidate.reason = MeshRenderEligibilityReason::InvalidLocalBounds;
        outPlacement = candidate;
        return false;
    }

    candidate.worldMatrix = Math::Matrix4x4::ComposeTrs(worldTransform.worldPosition, normalizedRotation,
                                                               worldTransform.worldScale);
    candidate.worldBounds = mesh.localBounds.Transform(candidate.worldMatrix);
    if (!IsOrderedFiniteAabb(candidate.worldBounds)) {
        candidate.reason = MeshRenderEligibilityReason::InvalidWorldTransform;
        outPlacement = candidate;
        return false;
    }

    candidate.reason = MeshRenderEligibilityReason::Eligible;
    outPlacement = candidate;
    return true;
}

bool TestMeshRenderVisibility(const MeshRenderPlacement& placement,
                                 const Math::Frustum& cullFrustum,
                                 MeshRenderEligibility& outEligibility,
                                 const bool frustumTestsDisabled) noexcept {
    MeshRenderEligibility candidate;

    // A placement that failed carries no usable matrix or bounds, so its own failure reason is
    // propagated rather than overwritten with a frustum verdict - "outside the frustum" would be a
    // lie about a mesh whose transform was never valid to begin with.
    if (!placement.IsPlaced()) {
        candidate.reason = placement.reason;
        outEligibility = candidate;
        return false;
    }

    candidate.worldMatrix = placement.worldMatrix;
    candidate.worldBounds = placement.worldBounds;

    // Skipped, not short-circuited away: the freeze still needs the sort depth below, which is
    // measured against this frustum's near plane whether or not the plane test ran.
    if (!frustumTestsDisabled && !cullFrustum.Intersects(candidate.worldBounds)) {
        candidate.reason = MeshRenderEligibilityReason::OutsideFrustum;
        outEligibility = candidate;
        return false;
    }
    candidate.sortDepth = std::fabs(cullFrustum.planes[kNearPlaneIndex].GetSignedDistance(
        candidate.worldBounds.GetCenter()));
    if (!std::isfinite(candidate.sortDepth)) {
        candidate.reason = MeshRenderEligibilityReason::InvalidWorldTransform;
        outEligibility = candidate;
        return false;
    }
    candidate.reason = MeshRenderEligibilityReason::Eligible;
    outEligibility = candidate;
    return true;
}

bool EvaluateMeshRenderEligibility(const Scene::MeshComponent& meshComponent,
                                      const Scene::WorldTransformComponent& worldTransform,
                                      const Asset::MeshAsset& mesh,
                                      const Math::Frustum& cullFrustum,
                                      MeshRenderEligibility& outEligibility) noexcept {
    // Composed, never duplicated - see the header. The behaviour of this function is unchanged
    // from before the split, which the existing eligibility tests continue to assert.
    MeshRenderPlacement placement;
    static_cast<void>(EvaluateMeshRenderPlacement(meshComponent, worldTransform, mesh, placement));
    return TestMeshRenderVisibility(placement, cullFrustum, outEligibility);
}

} // namespace UVE::Render
