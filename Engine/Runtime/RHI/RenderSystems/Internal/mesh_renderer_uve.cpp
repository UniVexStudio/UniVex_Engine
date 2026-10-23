

#include "uve/render_systems/mesh_renderer_uve.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "uve/asset/asset_guid_uve.h"
#include "uve/asset/material_asset_uve.h"
#include "uve/logging/assert_uve.h"
#include "uve/math/aabb_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/render_systems/mesh_render_eligibility_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/physics_interpolation_component_uve.h"
#include "uve/component/render_instance_component_uve.h"
#include "uve/component/surface_instance_component_uve.h"
#include "uve/utilities/hash_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"
#include "uve/objects/3d/lod_group_3d_uve.h"
#include "uve/objects/3d/mesh_instance_3d_uve.h"
#include "uve/objects/3d/occluder_3d_uve.h"
#include "uve/objects/3d/visibility_region_3d_uve.h"
#include "uve/objects/3d/world_partition_3d_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/component/world_transform_component_uve.h"

namespace UVE::Render {

namespace {

constexpr std::size_t kNearPlaneIndexUVE = 4U;

/// Resolves `guid` once per walk, returning the shared entry on every subsequent call for the same
/// GUID. Separate from the lambda so the mesh and material paths cannot drift into resolving or
/// classifying their handles differently from one another.
template <typename T>
[[nodiscard]] const ResolvedAssetUVE<T>& ResolveOnceUVE(
    std::unordered_map<Asset::AssetGuidUVE, ResolvedAssetUVE<T>>& resolved, Asset::AssetGuidUVE guid,
    Asset::IAssetManagerUVE& assetManager, Asset::IAssetDatabaseUVE& assetDatabase) {
    const auto existing = resolved.find(guid);
    if (existing != resolved.end()) {
        return existing->second;
    }

    ResolvedAssetUVE<T> entry{assetManager.template LoadUVE<T>(guid, assetDatabase), nullptr, false, false};
    // Queried in the same order and with the same meaning as the per-entity code this replaces:
    // failure first, then pending as "not failed and not yet ready", then the pointer.
    entry.failed = entry.handle.HasFailedUVE();
    entry.pending = !entry.failed && !entry.handle.IsReadyUVE();
    if (!entry.failed && !entry.pending) {
        entry.value = entry.handle.TryGetUVE();
    }
    return resolved.emplace(guid, std::move(entry)).first->second;
}

/// A mesh+material pairing, as raw GUID values. AssetGuidUVE has equality and a std::hash
/// specialization but no ordering, so this is hashed rather than compared - and the raw values are
/// used directly rather than adding an operator< to a type in another module purely for a local
/// lookup table.
struct AssetPairKeyUVE final {
    std::uint64_t meshGuidValue = 0U;
    std::uint64_t materialGuidValue = 0U;

    [[nodiscard]] bool operator==(const AssetPairKeyUVE& other) const noexcept {
        return meshGuidValue == other.meshGuidValue && materialGuidValue == other.materialGuidValue;
    }
};

struct AssetPairKeyHashUVE final {
    [[nodiscard]] std::size_t operator()(const AssetPairKeyUVE& key) const noexcept {
        const std::size_t meshHash = std::hash<std::uint64_t>{}(key.meshGuidValue);
        const std::size_t materialHash = std::hash<std::uint64_t>{}(key.materialGuidValue);
        // The two GUIDs are independent, so a plain XOR would collide for any pairing and its
        // reverse — hence the shared combiner. NOTE: the old hand-rolled mix used the 64-bit
        // golden constant where the shared formula uses boost's 32-bit one, so values changed
        // with the 1.7 migration. The pairing table is rebuilt per visibility set and never
        // persisted, so nothing depends on the old values.
        std::size_t seed = meshHash;
        Utilities::HashCombineUVE(seed, materialHash);
        return seed;
    }
};

/// Returns the index of the mesh+material pairing in `assetPairs`, appending it - and taking the
/// frame's one reference to each asset - the first time that pairing is seen.
///
/// Keyed on both GUIDs, not just the mesh: two entities can share a mesh while using different
/// materials, and collapsing those would hand one of them the other's material.
/// Replaces a placement's world matrix and bounds with the pose blended between the entity's last
/// two simulated steps. Returns false - leaving the placement untouched - whenever the simulated
/// pose is the right thing to draw.
///
/// Every rejection path returns the unblended placement rather than a partial result, so a caller
/// that ignores the return value still draws something correct. For a purely visual feature that
/// is the only safe direction to fail in.
[[nodiscard]] bool ApplyInterpolatedPoseUVE(Scene::IEntityManagerUVE& entityManager, const Scene::EntityUVE entity,
                                            const float alpha, const Asset::MeshAssetUVE& mesh,
                                            MeshRenderPlacementUVE& placement) {
    if (!placement.IsPlacedUVE()) {
        // A placement that failed carries no usable matrix to blend, and blending would overwrite
        // the reason it failed with a plausible-looking one.
        return false;
    }
    if (!entityManager.HasComponentUVE<Scene::PhysicsInterpolationComponentUVE>(entity)) {
        return false;
    }
    const Scene::PhysicsInterpolationComponentUVE& interpolation =
        entityManager.GetComponentUVE<Scene::PhysicsInterpolationComponentUVE>(entity);

    Math::Vector3UVE position{};
    Math::QuaternionUVE rotation{};
    Math::Vector3UVE scale{};
    if (!Scene::TryGetInterpolatedPoseUVE(interpolation, alpha, position, rotation, scale)) {
        return false;
    }

    // Recomposed rather than lerped as a matrix: interpolating matrix elements directly would
    // shear an object whose rotation changed, because the rows stop being orthonormal partway
    // through. The pose is blended as position/rotation/scale and the matrix rebuilt from it.
    const Math::Matrix4x4UVE worldMatrix = Math::Matrix4x4UVE::ComposeTrsUVE(position, rotation, scale);
    const Math::AabbUVE worldBounds = mesh.localBounds.TransformUVE(worldMatrix);
    const auto isFiniteVector = [](const Math::Vector3UVE& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    if (!isFiniteVector(worldBounds.min) || !isFiniteVector(worldBounds.max)) {
        // A degenerate blend must not publish a non-finite bound: the cull would then reject or
        // accept it unpredictably, and the object would flicker rather than simply not smooth.
        return false;
    }
    placement.worldMatrix = worldMatrix;
    placement.worldBounds = worldBounds;
    return true;
}

[[nodiscard]] std::size_t ResolveAssetPairIndexUVE(
    std::unordered_map<AssetPairKeyUVE, std::size_t, AssetPairKeyHashUVE>& slots,
    std::vector<MeshVisibilityAssetPairUVE>& assetPairs, Asset::AssetGuidUVE meshGuid,
    Asset::AssetGuidUVE materialGuid, const Asset::AssetHandleUVE<Asset::MeshAssetUVE>& meshHandle,
    const Asset::AssetHandleUVE<Asset::MaterialAssetUVE>& materialHandle) {
    const AssetPairKeyUVE key{meshGuid.value, materialGuid.value};
    const auto existing = slots.find(key);
    if (existing != slots.end()) {
        return existing->second;
    }
    const std::size_t index = assetPairs.size();
    // The only reference-count increments in the walk. Copies rather than moves: the resolution is
    // shared with every other entity using this GUID and must stay intact for them.
    assetPairs.push_back(MeshVisibilityAssetPairUVE{meshHandle, materialHandle});
    slots.emplace(key, index);
    return index;
}

/// Resolves a SurfaceInstance3D material override path to a loadable material GUID.
/// Built once per walk, only if a surface actually names an override: a scene with none pays
/// nothing. Empty path is "no override", not a failed lookup.
[[nodiscard]] bool TryResolveMaterialOverridePathUVE(
    const std::string& materialOverridePath, Asset::IAssetDatabaseUVE& assetDatabase,
    std::unordered_map<std::string, Asset::AssetGuidUVE>& pathToGuidIndex, bool& indexBuilt,
    Asset::AssetGuidUVE& outGuid) noexcept {
    if (materialOverridePath.empty()) {
        return false;
    }
    if (!indexBuilt) {
        pathToGuidIndex.clear();
        for (const Asset::AssetRecordUVE& record : assetDatabase.GetRegisteredAssetsUVE()) {
            pathToGuidIndex.emplace(record.path.lexically_normal().generic_string(), record.guid);
        }
        indexBuilt = true;
    }
    const std::string wanted = std::filesystem::path(materialOverridePath).lexically_normal().generic_string();
    const auto found = pathToGuidIndex.find(wanted);
    if (found == pathToGuidIndex.end()) {
        return false;
    }
    outGuid = found->second;
    return true;
}

[[nodiscard]] Math::Vector3UVE TranslationFromWorldMatrixUVE(const Math::Matrix4x4UVE& matrix) noexcept {
    return Math::Vector3UVE{matrix.m[0][3], matrix.m[1][3], matrix.m[2][3]};
}

[[nodiscard]] Math::Vector3UVE ScaleFromWorldMatrixUVE(const Math::Matrix4x4UVE& matrix) noexcept {
    const Math::Vector3UVE axisX{matrix.m[0][0], matrix.m[1][0], matrix.m[2][0]};
    const Math::Vector3UVE axisY{matrix.m[0][1], matrix.m[1][1], matrix.m[2][1]};
    const Math::Vector3UVE axisZ{matrix.m[0][2], matrix.m[1][2], matrix.m[2][2]};
    return Math::Vector3UVE{Math::LengthUVE(axisX), Math::LengthUVE(axisY), Math::LengthUVE(axisZ)};
}

/// Rewrites `placement` so local +Z faces the camera. Disabled, a degenerate look, or a
/// non-finite rewrite leaves it unchanged.
void ApplyMaterialBillboardToPlacementUVE(const Asset::MaterialBillboardModeUVE mode,
                                          const Math::Vector3UVE& cameraWorldPosition,
                                          const Math::AabbUVE& localBounds,
                                          MeshRenderPlacementUVE& placement) noexcept {
    if (mode == Asset::MaterialBillboardModeUVE::Disabled || !placement.IsPlacedUVE()) {
        return;
    }
    const Math::Vector3UVE position = TranslationFromWorldMatrixUVE(placement.worldMatrix);
    const Math::Vector3UVE scale = ScaleFromWorldMatrixUVE(placement.worldMatrix);
    if (!Math::IsFiniteUVE(position) || !Math::IsFiniteUVE(scale) || scale.x <= 0.0F || scale.y <= 0.0F ||
        scale.z <= 0.0F) {
        return;
    }
    Math::QuaternionUVE rotation{};
    if (!Asset::TryMakeMaterialBillboardRotationUVE(mode, position, cameraWorldPosition, rotation)) {
        return;
    }
    const Math::Matrix4x4UVE worldMatrix = Math::Matrix4x4UVE::ComposeTrsUVE(position, rotation, scale);
    const Math::AabbUVE worldBounds = localBounds.TransformUVE(worldMatrix);
    if (!Math::IsFiniteUVE(worldBounds.min) || !Math::IsFiniteUVE(worldBounds.max) ||
        worldBounds.min.x > worldBounds.max.x || worldBounds.min.y > worldBounds.max.y ||
        worldBounds.min.z > worldBounds.max.z) {
        return;
    }
    placement.worldMatrix = worldMatrix;
    placement.worldBounds = worldBounds;
}

} // namespace


RenderQueueUVE MeshRendererUVE::ExtractRenderQueueUVE(Scene::IEntityManagerUVE& entityManager,
                                                        Asset::IAssetManagerUVE& assetManager,
                                                        Asset::IAssetDatabaseUVE& assetDatabase,
                                                        const Math::FrustumUVE& cullFrustum) const {
    RenderQueueUVE queue;
    ExtractRenderQueueIntoUVE(entityManager, assetManager, assetDatabase, cullFrustum, queue);
    return queue;
}

void MeshRendererUVE::SetActiveCullFrustumUVE(const Math::FrustumUVE& frustum) const noexcept {
    m_activeCullFrustum = frustum;
}

void MeshRendererUVE::SetActiveCullFrustumsUVE(std::vector<Math::FrustumUVE> frustums) const noexcept {
    m_activeCullFrustums = std::move(frustums);
}

void MeshRendererUVE::SetActiveSceneBoundingRegionUVE(const Math::AabbUVE& region) const noexcept {
    m_activeBoundingRegion = region;
}

void MeshRendererUVE::ClearActiveCullFrustumUVE() const noexcept {
    m_activeCullFrustum.reset();
    m_activeCullFrustums.clear();
}

void MeshRendererUVE::ClearActiveSceneBoundingRegionUVE() const noexcept {
    m_activeBoundingRegion.reset();
}

bool MeshRendererUVE::HasActiveCullFrustumUVE() const noexcept {
    return m_activeCullFrustum.has_value() || !m_activeCullFrustums.empty();
}

const Math::FrustumUVE& MeshRendererUVE::GetActiveCullFrustumUVE() const noexcept {
    static const Math::FrustumUVE kDefaultFrustum{};
    if (m_activeCullFrustum.has_value()) {
        return *m_activeCullFrustum;
    }
    if (!m_activeCullFrustums.empty()) {
        return m_activeCullFrustums.front();
    }
    return kDefaultFrustum;
}

bool MeshRendererUVE::HasActiveSceneBoundingRegionUVE() const noexcept {
    return m_activeBoundingRegion.has_value();
}

const Math::AabbUVE& MeshRendererUVE::GetActiveSceneBoundingRegionUVE() const noexcept {
    static const Math::AabbUVE kDefaultAabb{};
    return m_activeBoundingRegion.has_value() ? *m_activeBoundingRegion : kDefaultAabb;
}

void MeshRendererUVE::UpdateEntitySpatialIndexUVE(const Scene::EntityUVE entity,
                                                  const Math::AabbUVE& worldBounds) const {
    m_spatialBvh.InsertOrUpdateUVE(entity, worldBounds);
}

void MeshRendererUVE::UpdateEntitySpatialIndexUVE(Scene::IEntityManagerUVE& entityManager,
                                                  Asset::IAssetManagerUVE& assetManager,
                                                  Asset::IAssetDatabaseUVE& assetDatabase,
                                                  const Scene::EntityUVE entity) const {
    if (!entityManager.IsAliveUVE(entity) ||
        !entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity) ||
        !entityManager.HasComponentUVE<Scene::MeshComponentUVE>(entity)) {
        m_spatialBvh.RemoveUVE(entity);
        ForgetUnindexableUVE(entity);
        return;
    }

    const auto& worldTransform = entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
    const auto& meshComponent = entityManager.GetComponentUVE<Scene::MeshComponentUVE>(entity);
    if (!Scene::IsMeshComponentValidUVE(meshComponent)) {
        MarkUnindexableUVE(entity);
        return;
    }

    Math::AabbUVE worldBounds{};
    bool boundsDetermined = false;
    if (meshComponent.meshGuid != Asset::kInvalidAssetGuidUVE) {
        Asset::AssetHandleUVE<Asset::MeshAssetUVE> meshHandle =
            assetManager.LoadUVE<Asset::MeshAssetUVE>(meshComponent.meshGuid, assetDatabase);
        if (meshHandle.IsReadyUVE()) {
            if (const Asset::MeshAssetUVE* const mesh = meshHandle.TryGetUVE()) {
                MeshRenderPlacementUVE placement;
                if (EvaluateMeshRenderPlacementUVE(meshComponent, worldTransform, *mesh, placement)) {
                    worldBounds = placement.worldBounds;
                    boundsDetermined = true;
                }
            }
        }
    }

    if (!boundsDetermined) {
        const Math::Vector3UVE halfExtents{0.5F, 0.5F, 0.5F};
        worldBounds = Math::AabbUVE::FromCenterExtentsUVE(worldTransform.worldPosition, halfExtents);
    }

    if (std::isfinite(worldBounds.min.x) && std::isfinite(worldBounds.max.x) &&
        worldBounds.min.x <= worldBounds.max.x) {
        m_spatialBvh.InsertOrUpdateUVE(entity, worldBounds);
        ForgetUnindexableUVE(entity);
    } else {
        MarkUnindexableUVE(entity);
    }
}

void MeshRendererUVE::RemoveEntitySpatialIndexUVE(const Scene::EntityUVE entity) const {
    m_spatialBvh.RemoveUVE(entity);
    ForgetUnindexableUVE(entity);
}

void MeshRendererUVE::MarkUnindexableUVE(const Scene::EntityUVE entity) const {
    m_spatialBvh.RemoveUVE(entity);
    const auto existing = std::find(m_unindexableEntities.begin(), m_unindexableEntities.end(), entity);
    if (existing == m_unindexableEntities.end()) {
        m_unindexableEntities.push_back(entity);
    }
}

void MeshRendererUVE::ForgetUnindexableUVE(const Scene::EntityUVE entity) const {
    const auto existing = std::find(m_unindexableEntities.begin(), m_unindexableEntities.end(), entity);
    if (existing != m_unindexableEntities.end()) {
        *existing = m_unindexableEntities.back();
        m_unindexableEntities.pop_back();
    }
}

void MeshRendererUVE::AppendUnindexableEntitiesUVE(std::vector<Scene::EntityUVE>& survivingEntities) const {
    survivingEntities.insert(survivingEntities.end(), m_unindexableEntities.begin(),
                             m_unindexableEntities.end());
}

void MeshRendererUVE::SynchronizeSpatialAccelerationUVE(Scene::IEntityManagerUVE& entityManager,
                                                        Asset::IAssetManagerUVE& assetManager,
                                                        Asset::IAssetDatabaseUVE& assetDatabase) const {
    std::vector<DynamicMeshBvhUVE::EntityEntry> entries;
    m_unindexableEntities.clear();
    entityManager.ForEachUVE<Scene::WorldTransformComponentUVE, Scene::MeshComponentUVE>(
        [&](const Scene::EntityUVE entity, const Scene::WorldTransformComponentUVE& worldTransform,
            const Scene::MeshComponentUVE& meshComponent) {
            if (!Scene::IsMeshComponentValidUVE(meshComponent)) {
                m_unindexableEntities.push_back(entity);
                return;
            }

            Math::AabbUVE worldBounds{};
            bool boundsDetermined = false;
            if (meshComponent.meshGuid != Asset::kInvalidAssetGuidUVE) {
                Asset::AssetHandleUVE<Asset::MeshAssetUVE> meshHandle =
                    assetManager.LoadUVE<Asset::MeshAssetUVE>(meshComponent.meshGuid, assetDatabase);
                if (meshHandle.IsReadyUVE()) {
                    if (const Asset::MeshAssetUVE* const mesh = meshHandle.TryGetUVE()) {
                        MeshRenderPlacementUVE placement;
                        if (EvaluateMeshRenderPlacementUVE(meshComponent, worldTransform, *mesh, placement)) {
                            worldBounds = placement.worldBounds;
                            boundsDetermined = true;
                        }
                    }
                }
            }

            if (!boundsDetermined) {
                const Math::Vector3UVE halfExtents{0.5F, 0.5F, 0.5F};
                worldBounds = Math::AabbUVE::FromCenterExtentsUVE(worldTransform.worldPosition, halfExtents);
            }

            if (std::isfinite(worldBounds.min.x) && std::isfinite(worldBounds.max.x) &&
                worldBounds.min.x <= worldBounds.max.x) {
                entries.push_back(DynamicMeshBvhUVE::EntityEntry{entity, worldBounds});
            } else {
                m_unindexableEntities.push_back(entity);
            }
        });

    m_spatialBvh.BuildUVE(std::move(entries));
}

void MeshRendererUVE::RebuildSpatialAccelerationUVE(Scene::IEntityManagerUVE& entityManager,
                                                    Asset::IAssetManagerUVE& assetManager,
                                                    Asset::IAssetDatabaseUVE& assetDatabase) const {
    m_spatialBvh.ClearUVE();
    SynchronizeSpatialAccelerationUVE(entityManager, assetManager, assetDatabase);
}

void MeshRendererUVE::ClearSpatialAccelerationUVE() const noexcept {
    m_spatialBvh.ClearUVE();
    m_unindexableEntities.clear();
    m_activeCullFrustum.reset();
    m_activeCullFrustums.clear();
    m_activeBoundingRegion.reset();
}

void MeshRendererUVE::GetAllIndexedEntitiesUVE(std::vector<Scene::EntityUVE>& outEntities) const {
    outEntities.clear();
    if (m_spatialBvh.IsEmptyUVE()) {
        return;
    }
    const std::int32_t root = m_spatialBvh.GetRootIndexUVE();
    if (root == DynamicMeshBvhUVE::kInvalidNodeIndex) {
        return;
    }
    std::vector<std::int32_t> stack;
    stack.reserve(64U);
    stack.push_back(root);
    const auto& nodes = m_spatialBvh.GetNodesUVE();
    while (!stack.empty()) {
        const std::int32_t current = stack.back();
        stack.pop_back();
        if (current < 0 || static_cast<std::size_t>(current) >= nodes.size()) {
            continue;
        }
        const DynamicMeshBvhUVE::Node& node = nodes[static_cast<std::size_t>(current)];
        if (node.IsLeaf()) {
            for (std::uint32_t i = 0U; i < node.entryCount; ++i) {
                outEntities.push_back(node.entries[i].entity);
            }
        } else {
            if (node.rightChild != DynamicMeshBvhUVE::kInvalidNodeIndex) {
                stack.push_back(node.rightChild);
            }
            if (node.leftChild != DynamicMeshBvhUVE::kInvalidNodeIndex) {
                stack.push_back(node.leftChild);
            }
        }
    }
}

void MeshRendererUVE::QueryFrustumsUVE(const std::vector<Math::FrustumUVE>& frustums,
                                       std::vector<Scene::EntityUVE>& outEntities) const {
    outEntities.clear();
    if (frustums.empty() || m_spatialBvh.IsEmptyUVE()) {
        return;
    }
    if (frustums.size() == 1U) {
        m_spatialBvh.QueryFrustumUVE(frustums[0], outEntities);
        return;
    }

    std::unordered_set<Scene::EntityUVE> seen;
    std::vector<Scene::EntityUVE> perFrustum;
    for (const auto& frustum : frustums) {
        perFrustum.clear();
        m_spatialBvh.QueryFrustumUVE(frustum, perFrustum);
        for (const Scene::EntityUVE entity : perFrustum) {
            if (seen.insert(entity).second) {
                outEntities.push_back(entity);
            }
        }
    }
}

void MeshRendererUVE::BuildCandidatesFromSurvivingEntitiesUVE(
    const std::vector<Scene::EntityUVE>& survivingEntities,
    Scene::IEntityManagerUVE& entityManager,
    Asset::IAssetManagerUVE& assetManager,
    Asset::IAssetDatabaseUVE& assetDatabase,
    MeshVisibilitySetUVE& outVisibilitySet) const {
    outVisibilitySet.ClearUVE();
    // Pre-increment, so no live entry can carry the new stamp before the walk assigns it. Starting
    // at 1 also lets 0 mean "never populated", which is how a default-constructed cache entry -
    // the one operator[] just inserted - is told apart from a genuine hit.
    ++outVisibilitySet.frameIndex;

    // Resolved once per distinct GUID for the duration of this walk, then discarded. Many
    // entities share one mesh and one material - that is the premise the instanced path is built
    // on - and resolving a handle costs about fourteen mutex-guarded lookups, so doing it per
    // entity meant asking the same question about the same GUID over and over.
    //
    // Deliberately local, not a member: asset state is asynchronous, and a load that completes, a
    // hot reload that replaces a pointer, or a load that fails must be visible on the very next
    // frame. These die with the walk.
    std::unordered_map<Asset::AssetGuidUVE, ResolvedAssetUVE<Asset::MeshAssetUVE>> resolvedMeshes;
    std::unordered_map<Asset::AssetGuidUVE, ResolvedAssetUVE<Asset::MaterialAssetUVE>> resolvedMaterials;

    // Maps a mesh+material pairing to its slot in the set's asset table, so the reference pair is
    // created once however many entities share it. Local for the same reason the resolutions are:
    // it describes this walk only.
    std::unordered_map<AssetPairKeyUVE, std::size_t, AssetPairKeyHashUVE> assetPairSlots;

    std::vector<Scene::Occluder3DSnapshotUVE> occluders;
    Scene::CollectOccluder3DSnapshotsUVE(entityManager, occluders);

    std::unordered_map<std::string, Asset::AssetGuidUVE> materialOverridePathIndex;
    bool materialOverridePathIndexBuilt = false;

    for (const Scene::EntityUVE entity : survivingEntities) {
        if (!entityManager.IsAliveUVE(entity) ||
            !entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity) ||
            !entityManager.HasComponentUVE<Scene::MeshComponentUVE>(entity)) {
            m_spatialBvh.RemoveUVE(entity);
            continue;
        }

        const auto& worldTransform = entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
        const auto& meshComponent = entityManager.GetComponentUVE<Scene::MeshComponentUVE>(entity);
        UVE_ASSERT(Scene::IsMeshComponentValidUVE(meshComponent));

        // Hidden first, before anything is resolved or counted. A hidden mesh must cost as
        // close to nothing as the walk allows, so this sits ahead of the asset resolution and
        // the placement cache rather than filtering at cull time - culling a candidate that
        // was never going to be drawn still pays to have built it.
        //
        // Also deliberately ahead of the diagnostics: a hidden object is not a scene problem,
        // and counting its unresolved assets as invalid references would make the stats panel
        // report faults for objects the author has simply switched off.
        if (entityManager.HasComponentUVE<Scene::VisibilityComponentUVE>(entity) &&
            !entityManager.GetComponentUVE<Scene::VisibilityComponentUVE>(entity).visibleInHierarchy) {
            ++outVisibilitySet.hiddenEntities;
            continue;
        }

        const Scene::SurfaceInstanceComponentUVE* surface = nullptr;
        if (entityManager.HasComponentUVE<Scene::SurfaceInstanceComponentUVE>(entity)) {
            const Scene::SurfaceInstanceComponentUVE& surfaceComponent =
                entityManager.GetComponentUVE<Scene::SurfaceInstanceComponentUVE>(entity);
            if (!Scene::IsSurfaceInstanceComponentValidUVE(surfaceComponent)) {
                ++outVisibilitySet.invalidRenderEligibility;
                continue;
            }
            surface = &surfaceComponent;
        }

        std::uint32_t renderLayers = 1U;
        float sortingOffset = 0.0F;
        bool sortingUseAabbCenter = true;
        if (entityManager.HasComponentUVE<Scene::RenderInstanceComponentUVE>(entity)) {
            const Scene::RenderInstanceComponentUVE& instance =
                entityManager.GetComponentUVE<Scene::RenderInstanceComponentUVE>(entity);
            if (!Scene::IsRenderInstanceComponentValidUVE(instance)) {
                ++outVisibilitySet.invalidRenderEligibility;
                continue;
            }
            renderLayers = instance.renderLayers;
            sortingOffset = instance.sortingOffset;
            sortingUseAabbCenter = instance.sortingUseAabbCenter;
        }
        if (!Scene::IsRenderInstance3DOnViewLayersUVE(renderLayers, outVisibilitySet.viewLayerMask)) {
            ++outVisibilitySet.layerCulledEntities;
            continue;
        }

        // Distance detail, resolved before anything is loaded or computed. A LodGroup3D past
        // the end of its chain costs a subtraction and a length here, and nothing else - no
        // asset resolution, no placement, no plane test, no draw call. Measured on a
        // 2000-object 200 m scene: 74% culled at a 50 m draw distance, 50% at 100 m.
        //
        // The level is resolved even for entities that are NOT culled, because currentLevel is
        // what a future mesh swap indexes by and the Inspector shows. Resolving it only on the
        // cull path would make the field correct exactly when nobody can see the object.
        // The mesh this entity draws with is the level the group resolved to, not necessarily
        // the MeshComponentUVE mesh: a level the group overrides names its own asset, and every
        // other level falls back to the component's own mesh. Resolved once here so the
        // diagnostics, the asset resolution, the placement cache key and the asset-pair bucket
        // below all speak about the mesh that was actually chosen - a shared resolution would
        // otherwise be reused under the identity of the wrong asset.
        Asset::AssetGuidUVE effectiveMeshGuid = meshComponent.meshGuid;
        if (entityManager.HasComponentUVE<Scene::LodGroup3DComponentUVE>(entity)) {
            Scene::LodGroup3DComponentUVE& lodGroup =
                entityManager.GetComponentUVE<Scene::LodGroup3DComponentUVE>(entity);
            const Math::Vector3UVE toCamera =
                worldTransform.worldPosition - outVisibilitySet.cameraWorldPosition;
            float lodDistance = Math::LengthUVE(toCamera);
            if (surface != nullptr) {
                lodDistance = Scene::SurfaceInstance3DLodDistanceUVE(*surface, lodDistance);
            }
            Scene::ResolveLodGroup3DLevelUVE(lodGroup, lodDistance);
            if (lodGroup.culledByDistance) {
                ++outVisibilitySet.distanceCulledEntities;
                continue;
            }
            effectiveMeshGuid = Scene::ResolveLodGroup3DMeshGuidUVE(lodGroup, meshComponent.meshGuid);
        }

        // World partition vis-budget: a faded cell skips this draw. A membership whose owner
        // was destroyed fails open so a residual opinion cannot hide content forever.
        if (Scene::IsWorldPartition3DDrawHiddenUVE(entityManager, entity)) {
            ++outVisibilitySet.partitionCulledEntities;
            continue;
        }

        // Visibility region room box: an inactive room skips this draw. A dead region fails
        // open so a residual opinion cannot hide content forever.
        if (Scene::IsVisibilityRegion3DDrawHiddenUVE(entityManager, entity)) {
            ++outVisibilitySet.regionCulledEntities;
            continue;
        }

        if (surface != nullptr) {
            const Math::Vector3UVE toCamera =
                worldTransform.worldPosition - outVisibilitySet.cameraWorldPosition;
            if (Scene::IsSurfaceInstance3DOutsideVisibilityRangeUVE(*surface, Math::LengthUVE(toCamera))) {
                ++outVisibilitySet.rangeCulledEntities;
                continue;
            }
        }

        // The material the OBJECT names - the component's field or a SurfaceInstance override
        // path. When it is set the whole mesh draws with it, exactly as it always has. When it is
        // not, the mesh's own surface materials shade it, which is what an imported multi-material
        // model arrives with.
        Asset::AssetGuidUVE objectMaterialGuid = meshComponent.materialGuid;
        if (Scene::MeshInstance3DUVE::HasSurfaceMaterialOverrideUVE(surface)) {
            Asset::AssetGuidUVE overrideGuid = Asset::kInvalidAssetGuidUVE;
            if (!TryResolveMaterialOverridePathUVE(surface->materialOverridePath, assetDatabase,
                                                   materialOverridePathIndex, materialOverridePathIndexBuilt,
                                                   overrideGuid)) {
                ++outVisibilitySet.invalidAssetReferences;
                continue;
            }
            objectMaterialGuid =
                Scene::MeshInstance3DUVE::ResolveEffectiveMaterialGuidUVE(meshComponent, overrideGuid);
        }

        if (effectiveMeshGuid == Asset::kInvalidAssetGuidUVE) {
            ++outVisibilitySet.invalidAssetReferences;
        }
        // A mesh with no material of any kind is not a broken reference: the renderer draws it with
        // the built-in lit shader (Renderer3DUVE's unmaterialed path), so only an unassigned pair
        // counts its material as missing. An override path that resolved is a material.
        if (objectMaterialGuid == Asset::kInvalidAssetGuidUVE &&
            effectiveMeshGuid == Asset::kInvalidAssetGuidUVE) {
            ++outVisibilitySet.invalidAssetReferences;
        }
        if (effectiveMeshGuid == Asset::kInvalidAssetGuidUVE) {
            continue;
        }

        // Resolved up-front because whether the mesh is split into surfaces decides HOW its
        // material is chosen, and because the surface table that answers it lives on the mesh.
        const ResolvedAssetUVE<Asset::MeshAssetUVE>& resolvedMesh =
            ResolveOnceUVE(resolvedMeshes, effectiveMeshGuid, assetManager, assetDatabase);
        // Counted per ENTITY, not per resolution. Sharing the resolution is an implementation
        // detail of how the answer was obtained; the diagnostic answers "how many entities could
        // not be drawn this frame". Mesh failures are counted here, material failures where the
        // material is resolved below, so an entity that never gets that far cannot inflate them.
        if (!resolvedMesh.IsUsableUVE()) {
            outVisibilitySet.failedAssetLoads += static_cast<std::size_t>(resolvedMesh.failed);
            outVisibilitySet.pendingAssetLoads += static_cast<std::size_t>(resolvedMesh.pending);
            continue;
        }
        const Asset::MeshAssetUVE* const mesh = resolvedMesh.value;

        // The mesh decides the material when the object names none. A material belongs to the mesh
        // it was authored with - an imported model carries its own look - so an object's material
        // field is an override of that, not the only way for one to exist. Without this, a model
        // with materials arrives grey and stays grey until somebody assigns one by hand.
        const Asset::AssetGuidUVE effectiveMaterialGuid =
            objectMaterialGuid != Asset::kInvalidAssetGuidUVE ? objectMaterialGuid
                                                              : mesh->FirstSurfaceMaterialGuidUVE();

        // A mesh with no material of any kind is not drawn here: Renderer3DUVE's unmaterialed path
        // draws it with the built-in lit shader instead.
        if (!Scene::MeshInstance3DUVE::HasRenderableAssetPairUVE(effectiveMeshGuid, effectiveMaterialGuid)) {
            continue;
        }

        // Surface by surface only when the mesh HAS a split and the object does not override it
        // with one material. A single-material mesh keeps the one-draw path, and an object-level
        // override keeps it too: that is the whole meaning of an override.
        const bool drawPerSurface =
            mesh->HasExplicitSurfacesUVE() && objectMaterialGuid == Asset::kInvalidAssetGuidUVE;

        // The cache lookup. Placement is the frame's dominant cost - measured at roughly 29x
        // the price of comparing this key and reusing the answer - and most objects in most
        // scenes do not move, so most of that cost is recomputing last frame's answer.
        const MeshPlacementKeyUVE key{worldTransform.worldPosition, worldTransform.worldRotation,
                                      worldTransform.worldScale, effectiveMeshGuid, mesh->localBounds};

        MeshPlacementCacheEntryUVE& cacheEntry = outVisibilitySet.placementCache[entity];
        const bool reusable = cacheEntry.lastSeenFrame != 0U && cacheEntry.key.MatchesUVE(key);
        if (reusable) {
            ++outVisibilitySet.placementCacheHits;
        } else {
            ++outVisibilitySet.placementCacheMisses;
            cacheEntry.key = key;
            Scene::MeshComponentUVE placementMesh = meshComponent;
            placementMesh.meshGuid = effectiveMeshGuid;
            placementMesh.materialGuid = effectiveMaterialGuid;
            static_cast<void>(
                EvaluateMeshRenderPlacementUVE(placementMesh, worldTransform, *mesh, cacheEntry.placement));
        }
        // Stamped on hit as well as miss: the stamp records "seen this frame", which is what
        // the prune reads. Only stamping misses would evict every stationary object.
        cacheEntry.lastSeenFrame = outVisibilitySet.frameIndex;

        const MeshRenderPlacementUVE& placement = cacheEntry.placement;
        if (!placement.IsPlacedUVE()) {
            if (placement.reason == MeshRenderEligibilityReasonUVE::InvalidWorldTransform ||
                placement.reason == MeshRenderEligibilityReasonUVE::InvalidLocalBounds) {
                ++outVisibilitySet.invalidRenderEligibility;
            }
            continue;
        }

        // Keep spatial BVH updated if bounds drifted
        const auto bvhBoundsOpt = m_spatialBvh.GetEntityBoundsUVE(entity);
        if (bvhBoundsOpt.has_value() && placement.worldBounds != *bvhBoundsOpt) {
            m_spatialBvh.InsertOrUpdateUVE(entity, placement.worldBounds);
        }

        Math::AabbUVE occlusionBounds = placement.worldBounds;
        if (surface != nullptr) {
            Scene::ExpandSurfaceInstance3DCullBoundsUVE(*surface, occlusionBounds);
        }
        if (!(surface != nullptr && surface->ignoreOcclusionCulling) &&
            Scene::IsOccluder3DAabbDrawHiddenUVE(occluders, outVisibilitySet.cameraWorldPosition,
                                                 occlusionBounds)) {
            ++outVisibilitySet.occlusionCulledEntities;
            continue;
        }

        // Physics interpolation, applied to the CANDIDATE rather than to the cached placement.
        //
        // The placement cache is keyed on the simulated world transform, which changes once
        // per fixed step. Keying it on the interpolated pose instead would miss on every frame
        // for every moving object - the cache is worth about 29x, and an interpolated scene
        // would give all of that back. So the expensive part (matrix compose, bounds
        // transform) stays cached against the simulated pose, and only the cheap part - a
        // position lerp and a rotation slerp - is redone per frame. Measured at 15.6x cheaper
        // than recomputing the placement.
        MeshRenderPlacementUVE posedPlacement = placement;
        if (ApplyInterpolatedPoseUVE(entityManager, entity, outVisibilitySet.physicsInterpolationAlpha,
                                     *mesh, posedPlacement)) {
            ++outVisibilitySet.interpolatedCandidates;
        }

        // SurfaceInstance3D's own verdicts, shared by every candidate this entity emits: they
        // describe the OBJECT, not the material its surfaces happen to name.
        bool castsShadow = true;
        bool drawsInView = true;
        float opacity = 1.0F;
        if (surface != nullptr) {
            castsShadow = Scene::SurfaceInstance3DCastsShadowUVE(*surface);
            drawsInView = Scene::SurfaceInstance3DDrawsInViewUVE(*surface);
            opacity = Scene::SurfaceInstance3DOpacityUVE(*surface);
            const Math::Vector3UVE toCamera =
                worldTransform.worldPosition - outVisibilitySet.cameraWorldPosition;
            opacity *= Scene::SurfaceInstance3DVisibilityFadeWeightUVE(*surface, Math::LengthUVE(toCamera));
            if (opacity <= 0.0F) {
                drawsInView = false;
            }
        }

        // One candidate per material the object draws with: exactly one for a whole-mesh object,
        // one per surface for a mesh split into surfaces. Everything that can be decided before
        // the material is - placement, interpolation, cull bounds - is done once above; only the
        // billboard pose and the transparent bucket depend on which material is being emitted.
        //
        // Bucketing is decided here, not per frustum: transparency is a property of the material
        // and of SurfaceInstance3D, and no frustum can change it.
        //
        // The candidate stores an INDEX, not a pair of handles. The set holds one reference per
        // distinct mesh+material pair and that is what keeps the assets alive across the frame; a
        // reference per entity would be the same two records counted thousands of times, at a
        // mutex and a hash each way.
        const auto emitCandidateUVE = [&](const Asset::MaterialAssetUVE& material,
                                          const Asset::AssetGuidUVE materialGuid,
                                          const Asset::AssetHandleUVE<Asset::MaterialAssetUVE>& materialHandle,
                                          const Asset::MeshSurfaceUVE surfaceRange) {
            const std::size_t pairIndex = ResolveAssetPairIndexUVE(
                assetPairSlots, outVisibilitySet.assetPairs, effectiveMeshGuid, materialGuid,
                resolvedMesh.handle, materialHandle);
            MeshRenderPlacementUVE emittedPlacement = posedPlacement;
            ApplyMaterialBillboardToPlacementUVE(material.billboardMode,
                                                 outVisibilitySet.cameraWorldPosition, mesh->localBounds,
                                                 emittedPlacement);
            if (surface != nullptr) {
                Scene::ExpandSurfaceInstance3DCullBoundsUVE(*surface, emittedPlacement.worldBounds);
            }
            const bool isTransparent = material.isTransparent || opacity < 1.0F;
            outVisibilitySet.candidates.push_back(MeshVisibilityCandidateUVE{
                pairIndex, emittedPlacement, isTransparent, entity, renderLayers, sortingOffset,
                sortingUseAabbCenter, castsShadow, drawsInView, opacity, false, surfaceRange});
        };

        if (drawPerSurface) {
            // Each surface is drawn with the material its own slot names. A slot the importer could
            // not publish falls back to the mesh's first material rather than dropping the range: a
            // piece of a model shaded by the wrong material is a defect the author can see, an
            // invisible one is a defect they have to guess at.
            for (std::size_t surfaceIndex = 0U; surfaceIndex < mesh->SurfaceCountUVE(); ++surfaceIndex) {
                Asset::AssetGuidUVE surfaceMaterialGuid = mesh->SurfaceMaterialGuidUVE(surfaceIndex);
                if (surfaceMaterialGuid == Asset::kInvalidAssetGuidUVE) {
                    surfaceMaterialGuid = mesh->FirstSurfaceMaterialGuidUVE();
                }
                if (surfaceMaterialGuid == Asset::kInvalidAssetGuidUVE) {
                    continue;
                }
                const ResolvedAssetUVE<Asset::MaterialAssetUVE>& resolvedSurfaceMaterial =
                    ResolveOnceUVE(resolvedMaterials, surfaceMaterialGuid, assetManager, assetDatabase);
                outVisibilitySet.failedAssetLoads +=
                    static_cast<std::size_t>(resolvedSurfaceMaterial.failed);
                outVisibilitySet.pendingAssetLoads +=
                    static_cast<std::size_t>(resolvedSurfaceMaterial.pending);
                if (!resolvedSurfaceMaterial.IsUsableUVE()) {
                    continue;
                }
                emitCandidateUVE(*resolvedSurfaceMaterial.value, surfaceMaterialGuid,
                                 resolvedSurfaceMaterial.handle, mesh->GetSurfaceUVE(surfaceIndex));
            }
        } else {
            const ResolvedAssetUVE<Asset::MaterialAssetUVE>& resolvedMaterial =
                ResolveOnceUVE(resolvedMaterials, effectiveMaterialGuid, assetManager, assetDatabase);
            outVisibilitySet.failedAssetLoads += static_cast<std::size_t>(resolvedMaterial.failed);
            outVisibilitySet.pendingAssetLoads += static_cast<std::size_t>(resolvedMaterial.pending);
            if (!resolvedMaterial.IsUsableUVE()) {
                continue;
            }
            emitCandidateUVE(*resolvedMaterial.value, effectiveMaterialGuid, resolvedMaterial.handle,
                             Asset::MeshSurfaceUVE{0U, static_cast<std::uint32_t>(mesh->indices.size()),
                                                   Asset::kInvalidMeshSurfaceMaterialSlotUVE});
        }

        if (surface == nullptr || !drawsInView || !Scene::SurfaceInstance3DHasOverlayUVE(*surface)) {
            continue;
        }
        Asset::AssetGuidUVE overlayGuid = Asset::kInvalidAssetGuidUVE;
        if (!TryResolveMaterialOverridePathUVE(surface->materialOverlayPath, assetDatabase,
                                               materialOverridePathIndex, materialOverridePathIndexBuilt,
                                               overlayGuid)) {
            ++outVisibilitySet.invalidAssetReferences;
            continue;
        }
        const ResolvedAssetUVE<Asset::MaterialAssetUVE>& resolvedOverlay =
            ResolveOnceUVE(resolvedMaterials, overlayGuid, assetManager, assetDatabase);
        outVisibilitySet.failedAssetLoads += static_cast<std::size_t>(resolvedOverlay.failed);
        outVisibilitySet.pendingAssetLoads += static_cast<std::size_t>(resolvedOverlay.pending);
        if (!resolvedOverlay.IsUsableUVE()) {
            continue;
        }
        // The overlay is ONE second pass over the whole mesh, not one per surface: it draws every
        // triangle again with one material, which is exactly what a whole-buffer range means.
        MeshRenderPlacementUVE overlayPlacement = posedPlacement;
        ApplyMaterialBillboardToPlacementUVE(resolvedOverlay.value->billboardMode,
                                             outVisibilitySet.cameraWorldPosition, mesh->localBounds,
                                             overlayPlacement);
        if (surface != nullptr) {
            Scene::ExpandSurfaceInstance3DCullBoundsUVE(*surface, overlayPlacement.worldBounds);
        }
        const std::size_t overlayPairIndex = ResolveAssetPairIndexUVE(
            assetPairSlots, outVisibilitySet.assetPairs, effectiveMeshGuid, overlayGuid, resolvedMesh.handle,
            resolvedOverlay.handle);
        outVisibilitySet.candidates.push_back(MeshVisibilityCandidateUVE{
            overlayPairIndex, overlayPlacement, true, entity, renderLayers, sortingOffset,
            sortingUseAabbCenter, false, true, opacity, true,
            Asset::MeshSurfaceUVE{0U, static_cast<std::uint32_t>(mesh->indices.size()),
                                  Asset::kInvalidMeshSurfaceMaterialSlotUVE}});
    }

    // Bound the cache. Without this it retains an entry for every entity the scene has ever had,
    // which for a streaming world is a slow leak rather than a cache.
    outVisibilitySet.PruneUnseenPlacementsUVE();

    // Built once here, consumed by every cull this set feeds. This reorders candidates, which is
    // safe precisely because nothing downstream may depend on their order - each cull sorts its
    // own queue by depth.
    outVisibilitySet.BuildSpatialClustersUVE();
}

void MeshRendererUVE::BuildVisibilitySetUVE(Scene::IEntityManagerUVE& entityManager,
                                            Asset::IAssetManagerUVE& assetManager,
                                            Asset::IAssetDatabaseUVE& assetDatabase,
                                            const Math::FrustumUVE& cullFrustum,
                                            MeshVisibilitySetUVE& outVisibilitySet) const {
    if (m_spatialBvh.IsEmptyUVE()) {
        SynchronizeSpatialAccelerationUVE(entityManager, assetManager, assetDatabase);
    }

    std::vector<Scene::EntityUVE> survivingEntities;
    if (outVisibilitySet.frustumTestsDisabled) {
        GetAllIndexedEntitiesUVE(survivingEntities);
    } else {
        m_spatialBvh.QueryFrustumUVE(cullFrustum, survivingEntities);
    }

    AppendUnindexableEntitiesUVE(survivingEntities);
    BuildCandidatesFromSurvivingEntitiesUVE(survivingEntities, entityManager, assetManager, assetDatabase,
                                           outVisibilitySet);
}

void MeshRendererUVE::BuildVisibilitySetUVE(Scene::IEntityManagerUVE& entityManager,
                                            Asset::IAssetManagerUVE& assetManager,
                                            Asset::IAssetDatabaseUVE& assetDatabase,
                                            const std::vector<Math::FrustumUVE>& cullFrustums,
                                            MeshVisibilitySetUVE& outVisibilitySet) const {
    if (m_spatialBvh.IsEmptyUVE()) {
        SynchronizeSpatialAccelerationUVE(entityManager, assetManager, assetDatabase);
    }

    std::vector<Scene::EntityUVE> survivingEntities;
    if (outVisibilitySet.frustumTestsDisabled || cullFrustums.empty()) {
        GetAllIndexedEntitiesUVE(survivingEntities);
    } else {
        QueryFrustumsUVE(cullFrustums, survivingEntities);
    }

    AppendUnindexableEntitiesUVE(survivingEntities);
    BuildCandidatesFromSurvivingEntitiesUVE(survivingEntities, entityManager, assetManager, assetDatabase,
                                           outVisibilitySet);
}

void MeshRendererUVE::BuildVisibilitySetUVE(Scene::IEntityManagerUVE& entityManager,
                                            Asset::IAssetManagerUVE& assetManager,
                                            Asset::IAssetDatabaseUVE& assetDatabase,
                                            const Math::AabbUVE& boundingRegion,
                                            MeshVisibilitySetUVE& outVisibilitySet) const {
    if (m_spatialBvh.IsEmptyUVE()) {
        SynchronizeSpatialAccelerationUVE(entityManager, assetManager, assetDatabase);
    }

    std::vector<Scene::EntityUVE> survivingEntities;
    if (outVisibilitySet.frustumTestsDisabled) {
        GetAllIndexedEntitiesUVE(survivingEntities);
    } else {
        m_spatialBvh.QueryAabbUVE(boundingRegion, survivingEntities);
    }

    AppendUnindexableEntitiesUVE(survivingEntities);
    BuildCandidatesFromSurvivingEntitiesUVE(survivingEntities, entityManager, assetManager, assetDatabase,
                                           outVisibilitySet);
}

void MeshRendererUVE::BuildVisibilitySetUVE(Scene::IEntityManagerUVE& entityManager,
                                            Asset::IAssetManagerUVE& assetManager,
                                            Asset::IAssetDatabaseUVE& assetDatabase,
                                            MeshVisibilitySetUVE& outVisibilitySet) const {
    if (m_spatialBvh.IsEmptyUVE()) {
        SynchronizeSpatialAccelerationUVE(entityManager, assetManager, assetDatabase);
    }

    std::vector<Scene::EntityUVE> survivingEntities;
    if (!outVisibilitySet.frustumTestsDisabled && m_activeCullFrustum.has_value()) {
        m_spatialBvh.QueryFrustumUVE(*m_activeCullFrustum, survivingEntities);
    } else if (!outVisibilitySet.frustumTestsDisabled && !m_activeCullFrustums.empty()) {
        QueryFrustumsUVE(m_activeCullFrustums, survivingEntities);
    } else if (!outVisibilitySet.frustumTestsDisabled && m_activeBoundingRegion.has_value()) {
        m_spatialBvh.QueryAabbUVE(*m_activeBoundingRegion, survivingEntities);
    } else {
        GetAllIndexedEntitiesUVE(survivingEntities);
    }

    AppendUnindexableEntitiesUVE(survivingEntities);
    BuildCandidatesFromSurvivingEntitiesUVE(survivingEntities, entityManager, assetManager, assetDatabase,
                                           outVisibilitySet);
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

    // Cluster-first. A frustum that misses a cluster's enclosing box misses every candidate in it,
    // so the run is rejected with one plane test rather than one per member. On a scene where most
    // objects are off-screen - which is most scenes, and all four of this frame's frusta - that is
    // where the culling time goes.
    //
    // Falls back to testing everything when the cluster list is empty, so a caller that populated
    // candidates without calling BuildSpatialClustersUVE still gets correct output rather than an
    // empty frame. Correctness must not depend on the optimization having run.
    const auto cullRangeUVE = [&](const std::size_t first, const std::size_t count) {
        for (std::size_t index = first; index < first + count; ++index) {
            const MeshVisibilityCandidateUVE& candidate = visibilitySet.candidates[index];
            MeshRenderEligibilityUVE eligibility;
            if (visibilitySet.shadowPass) {
                if (!candidate.castsShadow) {
                    continue;
                }
            } else if (!candidate.drawsInView) {
                continue;
            }
            if (!TestMeshRenderVisibilityUVE(candidate.placement, cullFrustum, eligibility,
                                             visibilitySet.frustumTestsDisabled)) {
                continue;
            }

            // Copies, not moves: one candidate set feeds four queues in a frame, so a move here
            // would empty the handles the remaining cascades still need. AssetHandleUVE copies are
            // refcount bumps, which is exactly what this wants - the handle outlives all four
            // queues anyway.
            // The queue item still owns its own handles: RenderQueueUVE is returned by value from
            // the public ExtractRenderQueueUVE, so it can outlive the visibility set that produced
            // it and cannot borrow that set's references. This is the one place the per-item cost
            // is genuinely load-bearing, and it is paid only for candidates that survived culling
            // rather than for every candidate in the scene.
            const MeshVisibilityAssetPairUVE& assetPair = visibilitySet.assetPairs[candidate.assetPairIndex];
            float sortDepth = eligibility.sortDepth;
            if (!candidate.sortingUseAabbCenter) {
                const Math::Vector3UVE origin{eligibility.worldMatrix.m[0][3], eligibility.worldMatrix.m[1][3],
                                              eligibility.worldMatrix.m[2][3]};
                sortDepth = cullFrustum.planes[kNearPlaneIndexUVE].GetSignedDistanceUVE(origin);
            }
            sortDepth = Scene::ApplyRenderInstance3DSortingOffsetUVE(sortDepth, candidate.sortingOffset);
            if (candidate.overlay) {
                sortDepth = Scene::ApplySurfaceInstance3DOverlaySortBiasUVE(sortDepth);
            }
            if (!std::isfinite(sortDepth)) {
                continue;
            }
            RenderItemUVE item{eligibility.worldMatrix, assetPair.meshHandle, assetPair.materialHandle, sortDepth,
                               candidate.renderLayers, candidate.opacity, candidate.overlay,
                               candidate.surface.firstIndex, candidate.surface.indexCount};
            // Shadow cascades write depth only and consume opaqueItems. A faded or material-
            // transparent caster still belongs there; the colour view is what sorts it back-to-front.
            if (!visibilitySet.shadowPass && candidate.isTransparent) {
                outQueue.transparentItems.push_back(std::move(item));
            } else {
                outQueue.opaqueItems.push_back(std::move(item));
            }
        }
    };

    if (visibilitySet.clusters.empty()) {
        cullRangeUVE(0U, visibilitySet.candidates.size());
        return;
    }
    for (const MeshVisibilitySetUVE::CandidateClusterUVE& cluster : visibilitySet.clusters) {
        // A frozen cull takes every cluster: rejecting one here would drop candidates the disabled
        // frustum test was asked to keep, and the whole point of the freeze is that nothing is
        // dropped for being off-screen.
        if (!visibilitySet.frustumTestsDisabled && !cullFrustum.IntersectsUVE(cluster.bounds)) {
            continue;
        }
        cullRangeUVE(cluster.first, cluster.count);
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
    BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, cullFrustum, visibilitySet);
    CullVisibilitySetIntoUVE(visibilitySet, cullFrustum, outQueue);
}

} // namespace UVE::Render
