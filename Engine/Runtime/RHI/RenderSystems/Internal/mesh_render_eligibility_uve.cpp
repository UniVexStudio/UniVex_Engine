// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/mesh_render_eligibility_uve.h"

#include <cmath>

#include "uve/asset/asset_guid_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Render {
namespace {

constexpr std::size_t kNearPlaneIndexUVE = 4U;

[[nodiscard]] bool IsOrderedFiniteAabbUVE(const Math::AabbUVE& bounds) noexcept {
    return Math::IsFiniteUVE(bounds.min) && Math::IsFiniteUVE(bounds.max) && bounds.min.x <= bounds.max.x &&
           bounds.min.y <= bounds.max.y && bounds.min.z <= bounds.max.z;
}

} // namespace

bool EvaluateMeshRenderPlacementUVE(const Scene::MeshComponentUVE& meshComponent,
                                    const Scene::WorldTransformComponentUVE& worldTransform,
                                    const Asset::MeshAssetUVE& mesh,
                                    MeshRenderPlacementUVE& outPlacement) noexcept {
    MeshRenderPlacementUVE candidate;
    if (!Scene::IsMeshComponentValidUVE(meshComponent) ||
        meshComponent.meshGuid == Asset::kInvalidAssetGuidUVE ||
        meshComponent.materialGuid == Asset::kInvalidAssetGuidUVE) {
        candidate.reason = MeshRenderEligibilityReasonUVE::InvalidAssetReferences;
        outPlacement = candidate;
        return false;
    }
    if (!Math::IsFiniteUVE(worldTransform.worldPosition) || !Math::IsFiniteUVE(worldTransform.worldScale) ||
        !Math::IsFiniteUVE(worldTransform.worldRotation)) {
        candidate.reason = MeshRenderEligibilityReasonUVE::InvalidWorldTransform;
        outPlacement = candidate;
        return false;
    }
    Math::QuaternionUVE normalizedRotation;
    if (!Math::TryNormalizeUVE(worldTransform.worldRotation, normalizedRotation)) {
        candidate.reason = MeshRenderEligibilityReasonUVE::InvalidWorldTransform;
        outPlacement = candidate;
        return false;
    }
    if (!IsOrderedFiniteAabbUVE(mesh.localBounds)) {
        candidate.reason = MeshRenderEligibilityReasonUVE::InvalidLocalBounds;
        outPlacement = candidate;
        return false;
    }

    candidate.worldMatrix = Math::Matrix4x4UVE::ComposeTrsUVE(worldTransform.worldPosition, normalizedRotation,
                                                               worldTransform.worldScale);
    candidate.worldBounds = mesh.localBounds.TransformUVE(candidate.worldMatrix);
    if (!IsOrderedFiniteAabbUVE(candidate.worldBounds)) {
        candidate.reason = MeshRenderEligibilityReasonUVE::InvalidWorldTransform;
        outPlacement = candidate;
        return false;
    }

    candidate.reason = MeshRenderEligibilityReasonUVE::Eligible;
    outPlacement = candidate;
    return true;
}

bool TestMeshRenderVisibilityUVE(const MeshRenderPlacementUVE& placement,
                                 const Math::FrustumUVE& cullFrustum,
                                 MeshRenderEligibilityUVE& outEligibility) noexcept {
    MeshRenderEligibilityUVE candidate;

    // A placement that failed carries no usable matrix or bounds, so its own failure reason is
    // propagated rather than overwritten with a frustum verdict - "outside the frustum" would be a
    // lie about a mesh whose transform was never valid to begin with.
    if (!placement.IsPlacedUVE()) {
        candidate.reason = placement.reason;
        outEligibility = candidate;
        return false;
    }

    candidate.worldMatrix = placement.worldMatrix;
    candidate.worldBounds = placement.worldBounds;

    if (!cullFrustum.IntersectsUVE(candidate.worldBounds)) {
        candidate.reason = MeshRenderEligibilityReasonUVE::OutsideFrustum;
        outEligibility = candidate;
        return false;
    }
    candidate.sortDepth = cullFrustum.planes[kNearPlaneIndexUVE].GetSignedDistanceUVE(
        candidate.worldBounds.GetCenterUVE());
    if (!std::isfinite(candidate.sortDepth)) {
        candidate.reason = MeshRenderEligibilityReasonUVE::InvalidWorldTransform;
        outEligibility = candidate;
        return false;
    }
    candidate.reason = MeshRenderEligibilityReasonUVE::Eligible;
    outEligibility = candidate;
    return true;
}

bool EvaluateMeshRenderEligibilityUVE(const Scene::MeshComponentUVE& meshComponent,
                                      const Scene::WorldTransformComponentUVE& worldTransform,
                                      const Asset::MeshAssetUVE& mesh,
                                      const Math::FrustumUVE& cullFrustum,
                                      MeshRenderEligibilityUVE& outEligibility) noexcept {
    // Composed, never duplicated - see the header. The behaviour of this function is unchanged
    // from before the split, which the existing eligibility tests continue to assert.
    MeshRenderPlacementUVE placement;
    static_cast<void>(EvaluateMeshRenderPlacementUVE(meshComponent, worldTransform, mesh, placement));
    return TestMeshRenderVisibilityUVE(placement, cullFrustum, outEligibility);
}

} // namespace UVE::Render
