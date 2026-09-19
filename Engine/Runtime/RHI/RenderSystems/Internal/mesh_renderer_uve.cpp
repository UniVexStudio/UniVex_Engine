

#include "uve/render_systems/mesh_renderer_uve.h"

#include <cstddef>
#include <utility>

#include "uve/asset/asset_guid_uve.h"
#include "uve/logging/assert_uve.h"
#include "uve/render_systems/mesh_render_eligibility_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/world_transform_component_uve.h"

namespace UVE::Render {

RenderQueueUVE MeshRendererUVE::ExtractRenderQueueUVE(Scene::IEntityManagerUVE& entityManager,
                                                        Asset::IAssetManagerUVE& assetManager,
                                                        Asset::IAssetDatabaseUVE& assetDatabase,
                                                        const Math::FrustumUVE& cullFrustum) const {
    RenderQueueUVE queue;
    ExtractRenderQueueIntoUVE(entityManager, assetManager, assetDatabase, cullFrustum, queue);
    return queue;
}

void MeshRendererUVE::BuildVisibilitySetUVE(Scene::IEntityManagerUVE& entityManager,
                                            Asset::IAssetManagerUVE& assetManager,
                                            Asset::IAssetDatabaseUVE& assetDatabase,
                                            MeshVisibilitySetUVE& outVisibilitySet) const {
    outVisibilitySet.ClearUVE();

    entityManager.ForEachUVE<Scene::WorldTransformComponentUVE, Scene::MeshComponentUVE>(
        [&](Scene::EntityUVE, const Scene::WorldTransformComponentUVE& worldTransform,
            const Scene::MeshComponentUVE& meshComponent) {
            UVE_ASSERT(Scene::IsMeshComponentValidUVE(meshComponent));
            if (meshComponent.meshGuid == Asset::kInvalidAssetGuidUVE) {
                ++outVisibilitySet.invalidAssetReferences;
            }
            if (meshComponent.materialGuid == Asset::kInvalidAssetGuidUVE) {
                ++outVisibilitySet.invalidAssetReferences;
            }
            if (meshComponent.meshGuid == Asset::kInvalidAssetGuidUVE ||
                meshComponent.materialGuid == Asset::kInvalidAssetGuidUVE) {
                return;
            }

            Asset::AssetHandleUVE<Asset::MeshAssetUVE> meshHandle =
                assetManager.LoadUVE<Asset::MeshAssetUVE>(meshComponent.meshGuid, assetDatabase);
            Asset::AssetHandleUVE<Asset::MaterialAssetUVE> materialHandle =
                assetManager.LoadUVE<Asset::MaterialAssetUVE>(meshComponent.materialGuid, assetDatabase);
            const bool meshFailed = meshHandle.HasFailedUVE();
            const bool materialFailed = materialHandle.HasFailedUVE();
            outVisibilitySet.failedAssetLoads +=
                static_cast<std::size_t>(meshFailed) + static_cast<std::size_t>(materialFailed);
            const bool meshPending = !meshFailed && !meshHandle.IsReadyUVE();
            const bool materialPending = !materialFailed && !materialHandle.IsReadyUVE();
            outVisibilitySet.pendingAssetLoads +=
                static_cast<std::size_t>(meshPending) + static_cast<std::size_t>(materialPending);
            if (meshPending || materialPending || meshFailed || materialFailed) {
                return;
            }

            const Asset::MeshAssetUVE* const mesh = meshHandle.TryGetUVE();
            const Asset::MaterialAssetUVE* const material = materialHandle.TryGetUVE();

            // Placement first, then construct. MeshVisibilityCandidateUVE holds AssetHandleUVE
            // members, which have no default constructor - the same reason RenderItemUVE is
            // aggregate-initialized at its push site rather than built up field by field.
            MeshRenderPlacementUVE placement;
            if (!EvaluateMeshRenderPlacementUVE(meshComponent, worldTransform, *mesh, placement)) {
                if (placement.reason == MeshRenderEligibilityReasonUVE::InvalidWorldTransform ||
                    placement.reason == MeshRenderEligibilityReasonUVE::InvalidLocalBounds) {
                    ++outVisibilitySet.invalidRenderEligibility;
                }
                return;
            }

            // Bucketing is decided here, not per frustum: transparency is a property of the
            // material, and no frustum can change it.
            outVisibilitySet.candidates.push_back(MeshVisibilityCandidateUVE{
                std::move(meshHandle), std::move(materialHandle), placement, material->isTransparent});
        });
}

void MeshRendererUVE::CullVisibilitySetIntoUVE(const MeshVisibilitySetUVE& visibilitySet,
                                               const Math::FrustumUVE& cullFrustum,
                                               RenderQueueUVE& outQueue) const {
    outQueue.ClearUVE();

    // The scene-wide counters are copied onto every queue this set produces. They describe the
    // scene rather than this view of it, so they are the same for all four frusta - a broken
    // material is one broken material, not one per cascade.
    outQueue.invalidAssetReferences = visibilitySet.invalidAssetReferences;
    outQueue.pendingAssetLoads = visibilitySet.pendingAssetLoads;
    outQueue.failedAssetLoads = visibilitySet.failedAssetLoads;
    outQueue.invalidRenderEligibility = visibilitySet.invalidRenderEligibility;

    for (const MeshVisibilityCandidateUVE& candidate : visibilitySet.candidates) {
        MeshRenderEligibilityUVE eligibility;
        if (!TestMeshRenderVisibilityUVE(candidate.placement, cullFrustum, eligibility)) {
            continue;
        }

        // Copies, not moves: one candidate set feeds four queues in a frame, so a move here would
        // empty the handles the remaining cascades still need. AssetHandleUVE copies are refcount
        // bumps, which is exactly what this wants - the handle outlives all four queues anyway.
        RenderItemUVE item{eligibility.worldMatrix, candidate.meshHandle, candidate.materialHandle,
                           eligibility.sortDepth};
        if (candidate.isTransparent) {
            outQueue.transparentItems.push_back(std::move(item));
        } else {
            outQueue.opaqueItems.push_back(std::move(item));
        }
    }
}

void MeshRendererUVE::ExtractRenderQueueIntoUVE(Scene::IEntityManagerUVE& entityManager,
                                            Asset::IAssetManagerUVE& assetManager,
                                            Asset::IAssetDatabaseUVE& assetDatabase,
                                            const Math::FrustumUVE& cullFrustum, RenderQueueUVE& outQueue) const {
    // Kept as the single-frustum entry point, now expressed as build-then-cull rather than a
    // second copy of the walk. A caller that culls once pays nothing for the split; a caller that
    // culls four times calls the two halves directly and pays the walk once.
    MeshVisibilitySetUVE visibilitySet;
    BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);
    CullVisibilitySetIntoUVE(visibilitySet, cullFrustum, outQueue);
}

} // namespace UVE::Render
