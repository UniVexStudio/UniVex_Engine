// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>

#include "uve/asset/mesh_asset_uve.h"
#include "uve/math/aabb_uve.h"
#include "uve/math/frustum_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/world_transform_component_uve.h"

namespace UVE::Render {

enum class MeshRenderEligibilityReasonUVE : std::uint8_t {
    Eligible = 0,
    InvalidAssetReferences,
    InvalidWorldTransform,
    InvalidLocalBounds,
    OutsideFrustum,
};

/// The frustum-INDEPENDENT half of eligibility: where a mesh is in the world and how big it is.
///
/// This is split out because it is the expensive half and the reusable half at once. Computing it
/// costs a quaternion normalization, a full TRS compose and an AABB transform, and NONE of that
/// depends on which frustum is asking - so a frame that culls against four frusta (three shadow
/// cascades plus the main view) can compute it once and test it four times instead of redoing
/// identical arithmetic per frustum.
///
/// It is also the shape every future culling stage needs. Occlusion culling and LOD selection both
/// want world bounds detached from any particular frustum; while bounds were computed inside the
/// frustum test there was nowhere for either to hook in.
///
/// ON OCCLUSION CULLING, since this is where someone will come looking for it.
/// Occluder3DNodeComponentUVE exists and is wired through the editor, the node registry and the
/// serializer, but nothing consumes it. I costed a conservative screen-space box rejection here
/// before writing one, against the cull as it now stands - clustered, roughly 1100 candidates
/// surviving cluster rejection per view:
///
///    1 occluder     4.0 us/view   removes   9/1100  ( 1%)
///    4 occluders    6.2 us/view   removes 310/1100  (28%)
///   16 occluders   16.2 us/view   removes 625/1100  (57%)
///   64 occluders   54.1 us/view   removes 655/1100  (60%)
///
/// The entire frustum-test budget it would be saving from is about 17 us/view. At 16 occluders
/// the test costs as much as the work it skips; at 64 it costs three times the whole cull. Adding
/// it to save CPU culling time would be a straight loss.
///
/// That is NOT an argument that occlusion culling is worthless - its real payoff is the draw calls
/// and GPU work never submitted, which is a different budget entirely and usually the larger one.
/// It is an argument that the payoff cannot be demonstrated from here: this engine's tests and CI
/// run on the null RHI, where a skipped draw costs nothing, so a measurement taken in this repo
/// can only ever show the cost and never the benefit. Anyone picking this up should land a real
/// GPU timing path first, then decide - and should size the occluder count deliberately, because
/// the table above flattens hard after about 16.
///
/// The hook point is this struct: worldBounds is already frustum-independent and already computed
/// once per frame, which is exactly what a rejection stage needs.
struct MeshRenderPlacementUVE final {
    MeshRenderEligibilityReasonUVE reason = MeshRenderEligibilityReasonUVE::InvalidAssetReferences;
    Math::Matrix4x4UVE worldMatrix{};
    Math::AabbUVE worldBounds{};

    /// True when the placement is usable - it says nothing about visibility, which needs a frustum.
    [[nodiscard]] bool IsPlacedUVE() const noexcept {
        return reason == MeshRenderEligibilityReasonUVE::Eligible;
    }
};

struct MeshRenderEligibilityUVE final {
    MeshRenderEligibilityReasonUVE reason = MeshRenderEligibilityReasonUVE::InvalidAssetReferences;
    Math::Matrix4x4UVE worldMatrix{};
    Math::AabbUVE worldBounds{};
    float sortDepth = 0.0F;

    [[nodiscard]] bool IsEligibleUVE() const noexcept {
        return reason == MeshRenderEligibilityReasonUVE::Eligible;
    }
};

/// The two steps above, composed, for callers that cull against exactly one frustum and have no
/// reason to keep the placement. Defined IN TERMS of them rather than duplicating their logic, so
/// the one-frustum and many-frustum paths can never drift apart.
///
/// Evaluates one loaded scene MeshComponentUVE for render-queue handoff. The contract validates
/// asset GUID coherence, finite world transform/quaternion normalization, finite ordered mesh bounds,
/// transformed bounds, frustum visibility, and finite sort depth, then publishes copied matrix/bounds
/// facts. It performs no asset loading, GPU allocation, command submission, or ownership transfer.
/// Computes the frustum-independent placement: asset coherence, finite transform, normalized
/// rotation, finite ordered local bounds, the composed world matrix and the transformed world
/// bounds. Every validity rule that does not mention a frustum lives here, so the two entry points
/// cannot disagree about which meshes are well-formed.
[[nodiscard]] bool EvaluateMeshRenderPlacementUVE(
    const Scene::MeshComponentUVE& meshComponent,
    const Scene::WorldTransformComponentUVE& worldTransform,
    const Asset::MeshAssetUVE& mesh,
    MeshRenderPlacementUVE& outPlacement) noexcept;

/// Tests an already-computed placement against one frustum and derives its sort depth.
///
/// Cheap by construction - a plane test and a dot product - which is the entire point of the
/// split: this is the part that must run per frustum, so it is the part kept small.
///
/// An unplaced input is rejected rather than trusted. A caller that skipped the placement step, or
/// whose placement failed, must not silently get a queue entry built on a default matrix.
[[nodiscard]] bool TestMeshRenderVisibilityUVE(
    const MeshRenderPlacementUVE& placement,
    const Math::FrustumUVE& cullFrustum,
    MeshRenderEligibilityUVE& outEligibility) noexcept;

[[nodiscard]] bool EvaluateMeshRenderEligibilityUVE(
    const Scene::MeshComponentUVE& meshComponent,
    const Scene::WorldTransformComponentUVE& worldTransform,
    const Asset::MeshAssetUVE& mesh,
    const Math::FrustumUVE& cullFrustum,
    MeshRenderEligibilityUVE& outEligibility) noexcept;

} // namespace UVE::Render
