

#include "mesh_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "asset_guid.h"
#include "material_asset.h"
#include "engine_assert.h"
#include "aabb.h"
#include "quaternion.h"
#include "vector3.h"
#include "mesh_render_eligibility.h"
#include "mesh_component.h"
#include "physics_interpolation_component.h"
#include "render_instance_component.h"
#include "surface_instance_component.h"
#include "hash.h"
#include "abstract_objects_3d.h"
#include "lod_group_3d.h"
#include "mesh_instance_3d.h"
#include "occluder_3d.h"
#include "visibility_region_3d.h"
#include "world_partition_3d.h"
#include "visibility_component.h"
#include "world_transform_component.h"

namespace UVE::Render {

namespace {

constexpr std::size_t kNearPlaneIndex = 4U;

/// Resolves `guid` once per walk, returning the shared entry on every subsequent call for the same
/// GUID. Separate from the lambda so the mesh and material paths cannot drift into resolving or
/// classifying their handles differently from one another.
template <typename T>
[[nodiscard]] const ResolvedAsset<T>& ResolveOnce(
    std::unordered_map<Asset::AssetGuid, ResolvedAsset<T>>& resolved, Asset::AssetGuid guid,
    Asset::IAssetManager& assetManager, Asset::IAssetDatabase& assetDatabase) {
    const auto existing = resolved.find(guid);
    if (existing != resolved.end()) {
        return existing->second;
    }

    ResolvedAsset<T> entry{assetManager.template Load<T>(guid, assetDatabase), nullptr, false, false};
    // Queried in the same order and with the same meaning as the per-entity code this replaces:
    // failure first, then pending as "not failed and not yet ready", then the pointer.
    entry.failed = entry.handle.HasFailed();
    entry.pending = !entry.failed && !entry.handle.IsReady();
    if (!entry.failed && !entry.pending) {
        entry.value = entry.handle.TryGet();
    }
    return resolved.emplace(guid, std::move(entry)).first->second;
}

/// A mesh+material pairing, as raw GUID values. AssetGuid has equality and a std::hash
/// specialization but no ordering, so this is hashed rather than compared - and the raw values are
/// used directly rather than adding an operator< to a type in another module purely for a local
/// lookup table.
struct AssetPairKey final {
    std::uint64_t meshGuidValue = 0U;
    std::uint64_t materialGuidValue = 0U;

    [[nodiscard]] bool operator==(const AssetPairKey& other) const noexcept {
        return meshGuidValue == other.meshGuidValue && materialGuidValue == other.materialGuidValue;
    }
};

struct AssetPairKeyHash final {
    [[nodiscard]] std::size_t operator()(const AssetPairKey& key) const noexcept {
        const std::size_t meshHash = std::hash<std::uint64_t>{}(key.meshGuidValue);
        const std::size_t materialHash = std::hash<std::uint64_t>{}(key.materialGuidValue);
        // The two GUIDs are independent, so a plain XOR would collide for any pairing and its
        // reverse — hence the shared combiner. NOTE: the old hand-rolled mix used the 64-bit
        // golden constant where the shared formula uses boost's 32-bit one, so values changed
        // with the 1.7 migration. The pairing table is rebuilt per visibility set and never
        // persisted, so nothing depends on the old values.
        std::size_t seed = meshHash;
        Utilities::HashCombine(seed, materialHash);
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
[[nodiscard]] bool ApplyInterpolatedPose(Scene::IEntityManager& entityManager, const Scene::Entity entity,
                                            const float alpha, const Asset::MeshAsset& mesh,
                                            MeshRenderPlacement& placement) {
    if (!placement.IsPlaced()) {
        // A placement that failed carries no usable matrix to blend, and blending would overwrite
        // the reason it failed with a plausible-looking one.
        return false;
    }
    if (!entityManager.HasComponent<Scene::PhysicsInterpolationComponent>(entity)) {
        return false;
    }
    const Scene::PhysicsInterpolationComponent& interpolation =
        entityManager.GetComponent<Scene::PhysicsInterpolationComponent>(entity);

    Math::Vector3 position{};
    Math::Quaternion rotation{};
    Math::Vector3 scale{};
    if (!Scene::TryGetInterpolatedPose(interpolation, alpha, position, rotation, scale)) {
        return false;
    }

    // Recomposed rather than lerped as a matrix: interpolating matrix elements directly would
    // shear an object whose rotation changed, because the rows stop being orthonormal partway
    // through. The pose is blended as position/rotation/scale and the matrix rebuilt from it.
    const Math::Matrix4x4 worldMatrix = Math::Matrix4x4::ComposeTrs(position, rotation, scale);
    const Math::Aabb worldBounds = mesh.localBounds.Transform(worldMatrix);
    const auto isFiniteVector = [](const Math::Vector3& value) {
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

[[nodiscard]] std::size_t ResolveAssetPairIndex(
    std::unordered_map<AssetPairKey, std::size_t, AssetPairKeyHash>& slots,
    std::vector<MeshVisibilityAssetPair>& assetPairs, Asset::AssetGuid meshGuid,
    Asset::AssetGuid materialGuid, const Asset::AssetHandle<Asset::MeshAsset>& meshHandle,
    const Asset::AssetHandle<Asset::MaterialAsset>& materialHandle) {
    const AssetPairKey key{meshGuid.value, materialGuid.value};
    const auto existing = slots.find(key);
    if (existing != slots.end()) {
        return existing->second;
    }
    const std::size_t index = assetPairs.size();
    // The only reference-count increments in the walk. Copies rather than moves: the resolution is
    // shared with every other entity using this GUID and must stay intact for them.
    assetPairs.push_back(MeshVisibilityAssetPair{meshHandle, materialHandle});
    slots.emplace(key, index);
    return index;
}

/// Lookup table for `materialOverridePath` / `materialOverlayPath` strings, built once per walk.
///
/// An override is authored relative to the project's content root ("Materials/M_Steel.uvmaterial",
/// what the Inspector writes), while the asset database stores whatever path it was registered
/// with - commonly the same file with the content root already prefixed ("assets/Materials/...").
/// Comparing those two strings directly never matches, so the index also holds every suffix that
/// begins on a path separator. A suffix that two different GUIDs share is AMBIGUOUS and resolves
/// nothing: drawing the wrong material is worse than drawing none, and the caller counts it as an
/// invalid reference so the problem is visible rather than silent.
struct MaterialOverridePathIndex final {
    std::unordered_map<std::string, Asset::AssetGuid> exact;
    std::unordered_map<std::string, Asset::AssetGuid> suffixes;
    std::unordered_set<std::string> ambiguousSuffixes;
};

void BuildMaterialOverridePathIndex(Asset::IAssetDatabase& assetDatabase,
                                       MaterialOverridePathIndex& index) {
    index.exact.clear();
    index.suffixes.clear();
    index.ambiguousSuffixes.clear();
    for (const Asset::AssetRecord& record : assetDatabase.GetRegisteredAssets()) {
        const std::string path = record.path.lexically_normal().generic_string();
        index.exact.emplace(path, record.guid);
        for (std::size_t separator = path.find('/'); separator != std::string::npos;
             separator = path.find('/', separator + 1U)) {
            if (separator + 1U >= path.size()) {
                continue;
            }
            const std::string suffix = path.substr(separator + 1U);
            const auto [found, inserted] = index.suffixes.try_emplace(suffix, record.guid);
            if (!inserted && found->second != record.guid) {
                index.ambiguousSuffixes.insert(suffix);
            }
        }
    }
}

/// Resolves a SurfaceInstance3D material override path to a loadable material GUID.
/// Built once per walk, only if a surface actually names an override: a scene with none pays
/// nothing. Empty path is "no override", not a failed lookup.
[[nodiscard]] bool TryResolveMaterialOverridePath(
    const std::string_view materialOverridePath, Asset::IAssetDatabase& assetDatabase,
    MaterialOverridePathIndex& index, bool& indexBuilt, Asset::AssetGuid& outGuid) {
    if (materialOverridePath.empty()) {
        return false;
    }
    if (!indexBuilt) {
        BuildMaterialOverridePathIndex(assetDatabase, index);
        indexBuilt = true;
    }
    const std::string wanted =
        std::filesystem::path(materialOverridePath).lexically_normal().generic_string();
    // The registered form wins outright: a project that registered both spellings has said which
    // one it means, and the suffix below is only a convenience for the spelling it did not record.
    if (const auto exact = index.exact.find(wanted); exact != index.exact.end()) {
        outGuid = exact->second;
        return true;
    }
    if (index.ambiguousSuffixes.find(wanted) != index.ambiguousSuffixes.end()) {
        return false;
    }
    if (const auto suffix = index.suffixes.find(wanted); suffix != index.suffixes.end()) {
        outGuid = suffix->second;
        return true;
    }
    return false;
}

[[nodiscard]] Math::Vector3 TranslationFromWorldMatrix(const Math::Matrix4x4& matrix) noexcept {
    return Math::Vector3{matrix.m[0][3], matrix.m[1][3], matrix.m[2][3]};
}

[[nodiscard]] Math::Vector3 ScaleFromWorldMatrix(const Math::Matrix4x4& matrix) noexcept {
    const Math::Vector3 axisX{matrix.m[0][0], matrix.m[1][0], matrix.m[2][0]};
    const Math::Vector3 axisY{matrix.m[0][1], matrix.m[1][1], matrix.m[2][1]};
    const Math::Vector3 axisZ{matrix.m[0][2], matrix.m[1][2], matrix.m[2][2]};
    return Math::Vector3{Math::Length(axisX), Math::Length(axisY), Math::Length(axisZ)};
}

/// Rewrites `placement` so local +Z faces the camera. Disabled, a degenerate look, or a
/// non-finite rewrite leaves it unchanged.
void ApplyMaterialBillboardToPlacement(const Asset::MaterialBillboardMode mode,
                                          const Math::Vector3& cameraWorldPosition,
                                          const Math::Aabb& localBounds,
                                          MeshRenderPlacement& placement) noexcept {
    if (mode == Asset::MaterialBillboardMode::Disabled || !placement.IsPlaced()) {
        return;
    }
    const Math::Vector3 position = TranslationFromWorldMatrix(placement.worldMatrix);
    const Math::Vector3 scale = ScaleFromWorldMatrix(placement.worldMatrix);
    if (!Math::IsFinite(position) || !Math::IsFinite(scale) || scale.x <= 0.0F || scale.y <= 0.0F ||
        scale.z <= 0.0F) {
        return;
    }
    Math::Quaternion rotation{};
    if (!Asset::TryMakeMaterialBillboardRotation(mode, position, cameraWorldPosition, rotation)) {
        return;
    }
    const Math::Matrix4x4 worldMatrix = Math::Matrix4x4::ComposeTrs(position, rotation, scale);
    const Math::Aabb worldBounds = localBounds.Transform(worldMatrix);
    if (!Math::IsFinite(worldBounds.min) || !Math::IsFinite(worldBounds.max) ||
        worldBounds.min.x > worldBounds.max.x || worldBounds.min.y > worldBounds.max.y ||
        worldBounds.min.z > worldBounds.max.z) {
        return;
    }
    placement.worldMatrix = worldMatrix;
    placement.worldBounds = worldBounds;
}

} // namespace


RenderQueue MeshRenderer::ExtractRenderQueue(Scene::IEntityManager& entityManager,
                                                        Asset::IAssetManager& assetManager,
                                                        Asset::IAssetDatabase& assetDatabase,
                                                        const Math::Frustum& cullFrustum) const {
    RenderQueue queue;
    ExtractRenderQueueInto(entityManager, assetManager, assetDatabase, cullFrustum, queue);
    return queue;
}

void MeshRenderer::SetActiveCullFrustum(const Math::Frustum& frustum) const noexcept {
    m_activeCullFrustum = frustum;
}

void MeshRenderer::SetActiveCullFrustums(std::vector<Math::Frustum> frustums) const noexcept {
    m_activeCullFrustums = std::move(frustums);
}

void MeshRenderer::SetActiveSceneBoundingRegion(const Math::Aabb& region) const noexcept {
    m_activeBoundingRegion = region;
}

void MeshRenderer::ClearActiveCullFrustum() const noexcept {
    m_activeCullFrustum.reset();
    m_activeCullFrustums.clear();
}

void MeshRenderer::ClearActiveSceneBoundingRegion() const noexcept {
    m_activeBoundingRegion.reset();
}

bool MeshRenderer::HasActiveCullFrustum() const noexcept {
    return m_activeCullFrustum.has_value() || !m_activeCullFrustums.empty();
}

const Math::Frustum& MeshRenderer::GetActiveCullFrustum() const noexcept {
    static const Math::Frustum kDefaultFrustum{};
    if (m_activeCullFrustum.has_value()) {
        return *m_activeCullFrustum;
    }
    if (!m_activeCullFrustums.empty()) {
        return m_activeCullFrustums.front();
    }
    return kDefaultFrustum;
}

bool MeshRenderer::HasActiveSceneBoundingRegion() const noexcept {
    return m_activeBoundingRegion.has_value();
}

const Math::Aabb& MeshRenderer::GetActiveSceneBoundingRegion() const noexcept {
    static const Math::Aabb kDefaultAabb{};
    return m_activeBoundingRegion.has_value() ? *m_activeBoundingRegion : kDefaultAabb;
}

void MeshRenderer::UpdateEntitySpatialIndex(const Scene::Entity entity,
                                                  const Math::Aabb& worldBounds) const {
    m_spatialBvh.InsertOrUpdate(entity, worldBounds);
}

void MeshRenderer::UpdateEntitySpatialIndex(Scene::IEntityManager& entityManager,
                                                  Asset::IAssetManager& assetManager,
                                                  Asset::IAssetDatabase& assetDatabase,
                                                  const Scene::Entity entity) const {
    if (!entityManager.IsAlive(entity) ||
        !entityManager.HasComponent<Scene::WorldTransformComponent>(entity) ||
        !entityManager.HasComponent<Scene::MeshComponent>(entity)) {
        m_spatialBvh.Remove(entity);
        ForgetUnindexable(entity);
        return;
    }

    const auto& worldTransform = entityManager.GetComponent<Scene::WorldTransformComponent>(entity);
    const auto& meshComponent = entityManager.GetComponent<Scene::MeshComponent>(entity);
    if (!Scene::IsMeshComponentValid(meshComponent)) {
        MarkUnindexable(entity);
        return;
    }

    Math::Aabb worldBounds{};
    bool boundsDetermined = false;
    if (meshComponent.meshGuid != Asset::kInvalidAssetGuid) {
        Asset::AssetHandle<Asset::MeshAsset> meshHandle =
            assetManager.Load<Asset::MeshAsset>(meshComponent.meshGuid, assetDatabase);
        if (meshHandle.IsReady()) {
            if (const Asset::MeshAsset* const mesh = meshHandle.TryGet()) {
                MeshRenderPlacement placement;
                if (EvaluateMeshRenderPlacement(meshComponent, worldTransform, *mesh, placement)) {
                    worldBounds = placement.worldBounds;
                    boundsDetermined = true;
                }
            }
        }
    }

    if (!boundsDetermined) {
        const Math::Vector3 halfExtents{0.5F, 0.5F, 0.5F};
        worldBounds = Math::Aabb::FromCenterExtents(worldTransform.worldPosition, halfExtents);
    }

    if (std::isfinite(worldBounds.min.x) && std::isfinite(worldBounds.max.x) &&
        worldBounds.min.x <= worldBounds.max.x) {
        m_spatialBvh.InsertOrUpdate(entity, worldBounds);
        ForgetUnindexable(entity);
    } else {
        MarkUnindexable(entity);
    }
}

void MeshRenderer::RemoveEntitySpatialIndex(const Scene::Entity entity) const {
    m_spatialBvh.Remove(entity);
    ForgetUnindexable(entity);
}

void MeshRenderer::MarkUnindexable(const Scene::Entity entity) const {
    m_spatialBvh.Remove(entity);
    const auto existing = std::find(m_unindexableEntities.begin(), m_unindexableEntities.end(), entity);
    if (existing == m_unindexableEntities.end()) {
        m_unindexableEntities.push_back(entity);
    }
}

void MeshRenderer::ForgetUnindexable(const Scene::Entity entity) const {
    const auto existing = std::find(m_unindexableEntities.begin(), m_unindexableEntities.end(), entity);
    if (existing != m_unindexableEntities.end()) {
        *existing = m_unindexableEntities.back();
        m_unindexableEntities.pop_back();
    }
}

void MeshRenderer::AppendUnindexableEntities(std::vector<Scene::Entity>& survivingEntities) const {
    survivingEntities.insert(survivingEntities.end(), m_unindexableEntities.begin(),
                             m_unindexableEntities.end());
}

void MeshRenderer::SynchronizeSpatialAcceleration(Scene::IEntityManager& entityManager,
                                                        Asset::IAssetManager& assetManager,
                                                        Asset::IAssetDatabase& assetDatabase) const {
    std::vector<DynamicMeshBvh::EntityEntry> entries;
    m_unindexableEntities.clear();
    entityManager.ForEach<Scene::WorldTransformComponent, Scene::MeshComponent>(
        [&](const Scene::Entity entity, const Scene::WorldTransformComponent& worldTransform,
            const Scene::MeshComponent& meshComponent) {
            if (!Scene::IsMeshComponentValid(meshComponent)) {
                m_unindexableEntities.push_back(entity);
                return;
            }

            Math::Aabb worldBounds{};
            bool boundsDetermined = false;
            if (meshComponent.meshGuid != Asset::kInvalidAssetGuid) {
                Asset::AssetHandle<Asset::MeshAsset> meshHandle =
                    assetManager.Load<Asset::MeshAsset>(meshComponent.meshGuid, assetDatabase);
                if (meshHandle.IsReady()) {
                    if (const Asset::MeshAsset* const mesh = meshHandle.TryGet()) {
                        MeshRenderPlacement placement;
                        if (EvaluateMeshRenderPlacement(meshComponent, worldTransform, *mesh, placement)) {
                            worldBounds = placement.worldBounds;
                            boundsDetermined = true;
                        }
                    }
                }
            }

            if (!boundsDetermined) {
                const Math::Vector3 halfExtents{0.5F, 0.5F, 0.5F};
                worldBounds = Math::Aabb::FromCenterExtents(worldTransform.worldPosition, halfExtents);
            }

            if (std::isfinite(worldBounds.min.x) && std::isfinite(worldBounds.max.x) &&
                worldBounds.min.x <= worldBounds.max.x) {
                entries.push_back(DynamicMeshBvh::EntityEntry{entity, worldBounds});
            } else {
                m_unindexableEntities.push_back(entity);
            }
        });

    m_spatialBvh.Build(std::move(entries));
}

void MeshRenderer::RebuildSpatialAcceleration(Scene::IEntityManager& entityManager,
                                                    Asset::IAssetManager& assetManager,
                                                    Asset::IAssetDatabase& assetDatabase) const {
    m_spatialBvh.Clear();
    SynchronizeSpatialAcceleration(entityManager, assetManager, assetDatabase);
}

void MeshRenderer::ClearSpatialAcceleration() const noexcept {
    m_spatialBvh.Clear();
    m_unindexableEntities.clear();
    m_activeCullFrustum.reset();
    m_activeCullFrustums.clear();
    m_activeBoundingRegion.reset();
}

void MeshRenderer::GetAllIndexedEntities(std::vector<Scene::Entity>& outEntities) const {
    outEntities.clear();
    if (m_spatialBvh.IsEmpty()) {
        return;
    }
    const std::int32_t root = m_spatialBvh.GetRootIndex();
    if (root == DynamicMeshBvh::kInvalidNodeIndex) {
        return;
    }
    std::vector<std::int32_t> stack;
    stack.reserve(64U);
    stack.push_back(root);
    const auto& nodes = m_spatialBvh.GetNodes();
    while (!stack.empty()) {
        const std::int32_t current = stack.back();
        stack.pop_back();
        if (current < 0 || static_cast<std::size_t>(current) >= nodes.size()) {
            continue;
        }
        const DynamicMeshBvh::Node& node = nodes[static_cast<std::size_t>(current)];
        if (node.IsLeaf()) {
            for (std::uint32_t i = 0U; i < node.entryCount; ++i) {
                outEntities.push_back(node.entries[i].entity);
            }
        } else {
            if (node.rightChild != DynamicMeshBvh::kInvalidNodeIndex) {
                stack.push_back(node.rightChild);
            }
            if (node.leftChild != DynamicMeshBvh::kInvalidNodeIndex) {
                stack.push_back(node.leftChild);
            }
        }
    }
}

void MeshRenderer::QueryFrustums(const std::vector<Math::Frustum>& frustums,
                                       std::vector<Scene::Entity>& outEntities) const {
    outEntities.clear();
    if (frustums.empty() || m_spatialBvh.IsEmpty()) {
        return;
    }
    if (frustums.size() == 1U) {
        m_spatialBvh.QueryFrustum(frustums[0], outEntities);
        return;
    }

    std::unordered_set<Scene::Entity> seen;
    std::vector<Scene::Entity> perFrustum;
    for (const auto& frustum : frustums) {
        perFrustum.clear();
        m_spatialBvh.QueryFrustum(frustum, perFrustum);
        for (const Scene::Entity entity : perFrustum) {
            if (seen.insert(entity).second) {
                outEntities.push_back(entity);
            }
        }
    }
}

void MeshRenderer::BuildCandidatesFromSurvivingEntities(
    const std::vector<Scene::Entity>& survivingEntities,
    Scene::IEntityManager& entityManager,
    Asset::IAssetManager& assetManager,
    Asset::IAssetDatabase& assetDatabase,
    MeshVisibilitySet& outVisibilitySet) const {
    outVisibilitySet.Clear();
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
    std::unordered_map<Asset::AssetGuid, ResolvedAsset<Asset::MeshAsset>> resolvedMeshes;
    std::unordered_map<Asset::AssetGuid, ResolvedAsset<Asset::MaterialAsset>> resolvedMaterials;

    // Maps a mesh+material pairing to its slot in the set's asset table, so the reference pair is
    // created once however many entities share it. Local for the same reason the resolutions are:
    // it describes this walk only.
    std::unordered_map<AssetPairKey, std::size_t, AssetPairKeyHash> assetPairSlots;

    std::vector<Scene::Occluder3DSnapshot> occluders;
    Scene::CollectOccluder3DSnapshots(entityManager, occluders);

    MaterialOverridePathIndex materialOverridePathIndex;
    bool materialOverridePathIndexBuilt = false;

    for (const Scene::Entity entity : survivingEntities) {
        if (!entityManager.IsAlive(entity) ||
            !entityManager.HasComponent<Scene::WorldTransformComponent>(entity) ||
            !entityManager.HasComponent<Scene::MeshComponent>(entity)) {
            m_spatialBvh.Remove(entity);
            continue;
        }

        const auto& worldTransform = entityManager.GetComponent<Scene::WorldTransformComponent>(entity);
        const auto& meshComponent = entityManager.GetComponent<Scene::MeshComponent>(entity);
        UVE_ASSERT(Scene::IsMeshComponentValid(meshComponent));

        // Hidden first, before anything is resolved or counted. A hidden mesh must cost as
        // close to nothing as the walk allows, so this sits ahead of the asset resolution and
        // the placement cache rather than filtering at cull time - culling a candidate that
        // was never going to be drawn still pays to have built it.
        //
        // Also deliberately ahead of the diagnostics: a hidden object is not a scene problem,
        // and counting its unresolved assets as invalid references would make the stats panel
        // report faults for objects the author has simply switched off.
        if (entityManager.HasComponent<Scene::VisibilityComponent>(entity) &&
            !entityManager.GetComponent<Scene::VisibilityComponent>(entity).visibleInHierarchy) {
            ++outVisibilitySet.hiddenEntities;
            continue;
        }

        const Scene::SurfaceInstanceComponent* surface = nullptr;
        if (entityManager.HasComponent<Scene::SurfaceInstanceComponent>(entity)) {
            const Scene::SurfaceInstanceComponent& surfaceComponent =
                entityManager.GetComponent<Scene::SurfaceInstanceComponent>(entity);
            if (!Scene::IsSurfaceInstanceComponentValid(surfaceComponent)) {
                ++outVisibilitySet.invalidRenderEligibility;
                continue;
            }
            surface = &surfaceComponent;
        }

        std::uint32_t renderLayers = 1U;
        float sortingOffset = 0.0F;
        bool sortingUseAabbCenter = true;
        if (entityManager.HasComponent<Scene::RenderInstanceComponent>(entity)) {
            const Scene::RenderInstanceComponent& instance =
                entityManager.GetComponent<Scene::RenderInstanceComponent>(entity);
            if (!Scene::IsRenderInstanceComponentValid(instance)) {
                ++outVisibilitySet.invalidRenderEligibility;
                continue;
            }
            renderLayers = instance.renderLayers;
            sortingOffset = instance.sortingOffset;
            sortingUseAabbCenter = instance.sortingUseAabbCenter;
        }
        if (!Scene::IsRenderInstance3DOnViewLayers(renderLayers, outVisibilitySet.viewLayerMask)) {
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
        // the MeshComponent mesh: a level the group overrides names its own asset, and every
        // other level falls back to the component's own mesh. Resolved once here so the
        // diagnostics, the asset resolution, the placement cache key and the asset-pair bucket
        // below all speak about the mesh that was actually chosen - a shared resolution would
        // otherwise be reused under the identity of the wrong asset.
        Asset::AssetGuid effectiveMeshGuid = meshComponent.meshGuid;
        if (entityManager.HasComponent<Scene::LodGroup3DComponent>(entity)) {
            Scene::LodGroup3DComponent& lodGroup =
                entityManager.GetComponent<Scene::LodGroup3DComponent>(entity);
            const Math::Vector3 toCamera =
                worldTransform.worldPosition - outVisibilitySet.cameraWorldPosition;
            float lodDistance = Math::Length(toCamera);
            if (surface != nullptr) {
                lodDistance = Scene::SurfaceInstance3DLodDistance(*surface, lodDistance);
            }
            Scene::ResolveLodGroup3DLevel(lodGroup, lodDistance);
            if (lodGroup.culledByDistance) {
                ++outVisibilitySet.distanceCulledEntities;
                continue;
            }
            effectiveMeshGuid = Scene::ResolveLodGroup3DMeshGuid(lodGroup, meshComponent.meshGuid);
        }

        // World partition vis-budget: a faded cell skips this draw. A membership whose owner
        // was destroyed fails open so a residual opinion cannot hide content forever.
        if (Scene::IsWorldPartition3DDrawHidden(entityManager, entity)) {
            ++outVisibilitySet.partitionCulledEntities;
            continue;
        }

        // Visibility region room box: an inactive room skips this draw. A dead region fails
        // open so a residual opinion cannot hide content forever.
        if (Scene::IsVisibilityRegion3DDrawHidden(entityManager, entity)) {
            ++outVisibilitySet.regionCulledEntities;
            continue;
        }

        if (surface != nullptr) {
            const Math::Vector3 toCamera =
                worldTransform.worldPosition - outVisibilitySet.cameraWorldPosition;
            if (Scene::IsSurfaceInstance3DOutsideVisibilityRange(*surface, Math::Length(toCamera))) {
                ++outVisibilitySet.rangeCulledEntities;
                continue;
            }
        }

        // The material the OBJECT names - the component's field or a SurfaceInstance override
        // path. When it is set the whole mesh draws with it, exactly as it always has. When it is
        // not, the mesh's own surface materials shade it, which is what an imported multi-material
        // model arrives with.
        //
        // The three levels, most specific first, are the priority `get_active_material()` states:
        // the whole-object override, then a surface's own override, then the material the mesh was
        // authored with. A whole-object override outranks the per-surface list entirely - it is
        // one material for every surface, so asking surface by surface would contradict it.
        Asset::AssetGuid objectMaterialGuid = meshComponent.materialGuid;
        if (Scene::MeshInstance3D::HasSurfaceMaterialOverride(surface)) {
            Asset::AssetGuid overrideGuid = Asset::kInvalidAssetGuid;
            if (!TryResolveMaterialOverridePath(surface->materialOverridePath, assetDatabase,
                                                   materialOverridePathIndex, materialOverridePathIndexBuilt,
                                                   overrideGuid)) {
                ++outVisibilitySet.invalidAssetReferences;
                continue;
            }
            objectMaterialGuid =
                Scene::MeshInstance3D::ResolveEffectiveMaterialGuid(meshComponent, overrideGuid);
        }
        const bool perSurfaceOverrides =
            Scene::MeshInstance3D::HasPerSurfaceMaterialOverride(surface);

        if (effectiveMeshGuid == Asset::kInvalidAssetGuid) {
            ++outVisibilitySet.invalidAssetReferences;
        }
        // A mesh with no material of any kind is not a broken reference: the renderer draws it with
        // the built-in lit shader (Renderer3D's unmaterialed path), so only an unassigned pair
        // counts its material as missing. An override path that resolved is a material.
        if (objectMaterialGuid == Asset::kInvalidAssetGuid &&
            effectiveMeshGuid == Asset::kInvalidAssetGuid) {
            ++outVisibilitySet.invalidAssetReferences;
        }
        if (effectiveMeshGuid == Asset::kInvalidAssetGuid) {
            continue;
        }

        // Resolved up-front because whether the mesh is split into surfaces decides HOW its
        // material is chosen, and because the surface table that answers it lives on the mesh.
        const ResolvedAsset<Asset::MeshAsset>& resolvedMesh =
            ResolveOnce(resolvedMeshes, effectiveMeshGuid, assetManager, assetDatabase);
        // Counted per ENTITY, not per resolution. Sharing the resolution is an implementation
        // detail of how the answer was obtained; the diagnostic answers "how many entities could
        // not be drawn this frame". Mesh failures are counted here, material failures where the
        // material is resolved below, so an entity that never gets that far cannot inflate them.
        if (!resolvedMesh.IsUsable()) {
            outVisibilitySet.failedAssetLoads += static_cast<std::size_t>(resolvedMesh.failed);
            outVisibilitySet.pendingAssetLoads += static_cast<std::size_t>(resolvedMesh.pending);
            continue;
        }
        const Asset::MeshAsset* const mesh = resolvedMesh.value;

        // Skinned meshes require per-entity dynamic buffers and skeleton pose evaluation;
        // they are drawn through the dedicated skinned path in Renderer3D.
        if (mesh->IsSkinned()) {
            continue;
        }

        // The mesh decides the material when the object names none. A material belongs to the mesh
        // it was authored with - an imported model carries its own look - so an object's material
        // field is an override of that, not the only way for one to exist. Without this, a model
        // with materials arrives grey and stays grey until somebody assigns one by hand.
        const Asset::AssetGuid effectiveMaterialGuid =
            objectMaterialGuid != Asset::kInvalidAssetGuid ? objectMaterialGuid
                                                              : mesh->FirstSurfaceMaterialGuid();

        // A mesh with no material of any kind is not drawn here: Renderer3D's unmaterialed path
        // draws it with the built-in lit shader instead. A per-surface override is itself a way to
        // have a material, so an instance that supplies one draws even when the mesh published
        // none and the object names none - the surfaces it does not override are then skipped
        // individually below, rather than the whole object disappearing.
        if (!Scene::MeshInstance3D::HasRenderableAssetPair(effectiveMeshGuid, effectiveMaterialGuid) &&
            !perSurfaceOverrides) {
            continue;
        }

        // Surface by surface when the mesh HAS a split, or when the instance overrides a surface of
        // its own, and the object does not override the whole mesh with one material. A
        // single-material mesh keeps the one-draw path, and an object-level override keeps it
        // too: that is the whole meaning of an override. A mesh with one implicit surface still
        // has surface 0 for an instance to override, which is why the list alone is enough.
        const bool drawPerSurface =
            (mesh->HasExplicitSurfaces() || perSurfaceOverrides) &&
            objectMaterialGuid == Asset::kInvalidAssetGuid;

        // The cache lookup. Placement is the frame's dominant cost - measured at roughly 29x
        // the price of comparing this key and reusing the answer - and most objects in most
        // scenes do not move, so most of that cost is recomputing last frame's answer.
        const MeshPlacementKey key{worldTransform.worldPosition, worldTransform.worldRotation,
                                      worldTransform.worldScale, effectiveMeshGuid, mesh->localBounds};

        MeshPlacementCacheEntry& cacheEntry = outVisibilitySet.placementCache[entity];
        const bool reusable = cacheEntry.lastSeenFrame != 0U && cacheEntry.key.Matches(key);
        if (reusable) {
            ++outVisibilitySet.placementCacheHits;
        } else {
            ++outVisibilitySet.placementCacheMisses;
            cacheEntry.key = key;
            Scene::MeshComponent placementMesh = meshComponent;
            placementMesh.meshGuid = effectiveMeshGuid;
            placementMesh.materialGuid = effectiveMaterialGuid;
            static_cast<void>(
                EvaluateMeshRenderPlacement(placementMesh, worldTransform, *mesh, cacheEntry.placement));
        }
        // Stamped on hit as well as miss: the stamp records "seen this frame", which is what
        // the prune reads. Only stamping misses would evict every stationary object.
        cacheEntry.lastSeenFrame = outVisibilitySet.frameIndex;

        const MeshRenderPlacement& placement = cacheEntry.placement;
        if (!placement.IsPlaced()) {
            if (placement.reason == MeshRenderEligibilityReason::InvalidWorldTransform ||
                placement.reason == MeshRenderEligibilityReason::InvalidLocalBounds) {
                ++outVisibilitySet.invalidRenderEligibility;
            }
            continue;
        }

        // Keep spatial BVH updated if bounds drifted
        const auto bvhBoundsOpt = m_spatialBvh.GetEntityBounds(entity);
        if (bvhBoundsOpt.has_value() && placement.worldBounds != *bvhBoundsOpt) {
            m_spatialBvh.InsertOrUpdate(entity, placement.worldBounds);
        }

        Math::Aabb occlusionBounds = placement.worldBounds;
        if (surface != nullptr) {
            Scene::ExpandSurfaceInstance3DCullBounds(*surface, occlusionBounds);
        }
        if (!(surface != nullptr && surface->ignoreOcclusionCulling) &&
            Scene::IsOccluder3DAabbDrawHidden(occluders, outVisibilitySet.cameraWorldPosition,
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
        MeshRenderPlacement posedPlacement = placement;
        if (ApplyInterpolatedPose(entityManager, entity, outVisibilitySet.physicsInterpolationAlpha,
                                     *mesh, posedPlacement)) {
            ++outVisibilitySet.interpolatedCandidates;
        }

        // SurfaceInstance3D's own verdicts, shared by every candidate this entity emits: they
        // describe the OBJECT, not the material its surfaces happen to name.
        bool castsShadow = true;
        bool drawsInView = true;
        float opacity = 1.0F;
        if (surface != nullptr) {
            castsShadow = Scene::SurfaceInstance3DCastsShadow(*surface);
            drawsInView = Scene::SurfaceInstance3DDrawsInView(*surface);
            opacity = Scene::SurfaceInstance3DOpacity(*surface);
            const Math::Vector3 toCamera =
                worldTransform.worldPosition - outVisibilitySet.cameraWorldPosition;
            opacity *= Scene::SurfaceInstance3DVisibilityFadeWeight(*surface, Math::Length(toCamera));
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
        const auto emitCandidate = [&](const Asset::MaterialAsset& material,
                                          const Asset::AssetGuid materialGuid,
                                          const Asset::AssetHandle<Asset::MaterialAsset>& materialHandle,
                                          const Asset::MeshSurface surfaceRange) {
            const std::size_t pairIndex = ResolveAssetPairIndex(
                assetPairSlots, outVisibilitySet.assetPairs, effectiveMeshGuid, materialGuid,
                resolvedMesh.handle, materialHandle);
            MeshRenderPlacement emittedPlacement = posedPlacement;
            ApplyMaterialBillboardToPlacement(material.billboardMode,
                                                 outVisibilitySet.cameraWorldPosition, mesh->localBounds,
                                                 emittedPlacement);
            if (surface != nullptr) {
                Scene::ExpandSurfaceInstance3DCullBounds(*surface, emittedPlacement.worldBounds);
            }
            // The bucket is decided by ShouldBucketMaterialTransparent, the same predicate the
            // material thumbnail renderer already consulted: a material authored as additive or
            // alpha-blended composites against what is already there, so it has to be drawn after
            // the opaque pass and back to front. Deciding it from isTransparent alone put an
            // additive material in the opaque pass, in whatever order the scene happened to have,
            // and left its blend mode deciding nothing at all.
            const bool isTransparent = Asset::ShouldBucketMaterialTransparent(material) || opacity < 1.0F;
            outVisibilitySet.candidates.push_back(MeshVisibilityCandidate{
                pairIndex, emittedPlacement, isTransparent, entity, renderLayers, sortingOffset,
                sortingUseAabbCenter, castsShadow, drawsInView, opacity, false, surfaceRange});
        };

        if (drawPerSurface) {
            // Each surface is drawn with the most specific material that names it: the instance's
            // override for that surface, then the material the mesh's own slot names, then - for a
            // slot the importer could not publish - the mesh's first material rather than dropping
            // the range. A piece of a model shaded by the wrong material is a defect the author can
            // see, an invisible one is a defect they have to guess at.
            for (std::size_t surfaceIndex = 0U; surfaceIndex < mesh->SurfaceCount(); ++surfaceIndex) {
                // The instance's word about THIS surface comes first: that is the whole point of
                // overriding one. An override that no longer resolves drops the surface instead of
                // falling back - the author asked for a specific material, and quietly drawing the
                // authored one makes a moved or deleted .uvmaterial look like it still applies.
                Asset::AssetGuid surfaceMaterialGuid = Asset::kInvalidAssetGuid;
                if (perSurfaceOverrides && surface != nullptr) {
                    const std::string_view overridePath =
                        Scene::MeshInstance3D::SurfaceMaterialOverridePath(*surface, surfaceIndex);
                    if (!overridePath.empty()) {
                        if (!TryResolveMaterialOverridePath(overridePath, assetDatabase,
                                                               materialOverridePathIndex,
                                                               materialOverridePathIndexBuilt,
                                                               surfaceMaterialGuid)) {
                            ++outVisibilitySet.invalidAssetReferences;
                            continue;
                        }
                    }
                }
                if (surfaceMaterialGuid == Asset::kInvalidAssetGuid) {
                    surfaceMaterialGuid = mesh->SurfaceMaterialGuid(surfaceIndex);
                }
                if (surfaceMaterialGuid == Asset::kInvalidAssetGuid) {
                    surfaceMaterialGuid = mesh->FirstSurfaceMaterialGuid();
                }
                if (surfaceMaterialGuid == Asset::kInvalidAssetGuid) {
                    continue;
                }
                const ResolvedAsset<Asset::MaterialAsset>& resolvedSurfaceMaterial =
                    ResolveOnce(resolvedMaterials, surfaceMaterialGuid, assetManager, assetDatabase);
                outVisibilitySet.failedAssetLoads +=
                    static_cast<std::size_t>(resolvedSurfaceMaterial.failed);
                outVisibilitySet.pendingAssetLoads +=
                    static_cast<std::size_t>(resolvedSurfaceMaterial.pending);
                if (!resolvedSurfaceMaterial.IsUsable()) {
                    continue;
                }
                emitCandidate(*resolvedSurfaceMaterial.value, surfaceMaterialGuid,
                                 resolvedSurfaceMaterial.handle, mesh->GetSurface(surfaceIndex));
            }
        } else {
            const ResolvedAsset<Asset::MaterialAsset>& resolvedMaterial =
                ResolveOnce(resolvedMaterials, effectiveMaterialGuid, assetManager, assetDatabase);
            outVisibilitySet.failedAssetLoads += static_cast<std::size_t>(resolvedMaterial.failed);
            outVisibilitySet.pendingAssetLoads += static_cast<std::size_t>(resolvedMaterial.pending);
            if (!resolvedMaterial.IsUsable()) {
                continue;
            }
            emitCandidate(*resolvedMaterial.value, effectiveMaterialGuid, resolvedMaterial.handle,
                             Asset::MeshSurface{0U, static_cast<std::uint32_t>(mesh->indices.size()),
                                                   Asset::kInvalidMeshSurfaceMaterialSlot});
        }

        if (surface == nullptr || !drawsInView || !Scene::SurfaceInstance3DHasOverlay(*surface)) {
            continue;
        }
        Asset::AssetGuid overlayGuid = Asset::kInvalidAssetGuid;
        if (!TryResolveMaterialOverridePath(surface->materialOverlayPath, assetDatabase,
                                               materialOverridePathIndex, materialOverridePathIndexBuilt,
                                               overlayGuid)) {
            ++outVisibilitySet.invalidAssetReferences;
            continue;
        }
        const ResolvedAsset<Asset::MaterialAsset>& resolvedOverlay =
            ResolveOnce(resolvedMaterials, overlayGuid, assetManager, assetDatabase);
        outVisibilitySet.failedAssetLoads += static_cast<std::size_t>(resolvedOverlay.failed);
        outVisibilitySet.pendingAssetLoads += static_cast<std::size_t>(resolvedOverlay.pending);
        if (!resolvedOverlay.IsUsable()) {
            continue;
        }
        // The overlay is ONE second pass over the whole mesh, not one per surface: it draws every
        // triangle again with one material, which is exactly what a whole-buffer range means.
        MeshRenderPlacement overlayPlacement = posedPlacement;
        ApplyMaterialBillboardToPlacement(resolvedOverlay.value->billboardMode,
                                             outVisibilitySet.cameraWorldPosition, mesh->localBounds,
                                             overlayPlacement);
        if (surface != nullptr) {
            Scene::ExpandSurfaceInstance3DCullBounds(*surface, overlayPlacement.worldBounds);
        }
        const std::size_t overlayPairIndex = ResolveAssetPairIndex(
            assetPairSlots, outVisibilitySet.assetPairs, effectiveMeshGuid, overlayGuid, resolvedMesh.handle,
            resolvedOverlay.handle);
        outVisibilitySet.candidates.push_back(MeshVisibilityCandidate{
            overlayPairIndex, overlayPlacement, true, entity, renderLayers, sortingOffset,
            sortingUseAabbCenter, false, true, opacity, true,
            Asset::MeshSurface{0U, static_cast<std::uint32_t>(mesh->indices.size()),
                                  Asset::kInvalidMeshSurfaceMaterialSlot}});
    }

    // Bound the cache. Without this it retains an entry for every entity the scene has ever had,
    // which for a streaming world is a slow leak rather than a cache.
    outVisibilitySet.PruneUnseenPlacements();

    // Built once here, consumed by every cull this set feeds. This reorders candidates, which is
    // safe precisely because nothing downstream may depend on their order - each cull sorts its
    // own queue by depth.
    outVisibilitySet.BuildSpatialClusters();
}

void MeshRenderer::BuildVisibilitySet(Scene::IEntityManager& entityManager,
                                            Asset::IAssetManager& assetManager,
                                            Asset::IAssetDatabase& assetDatabase,
                                            const Math::Frustum& cullFrustum,
                                            MeshVisibilitySet& outVisibilitySet) const {
    if (m_spatialBvh.IsEmpty()) {
        SynchronizeSpatialAcceleration(entityManager, assetManager, assetDatabase);
    }

    std::vector<Scene::Entity> survivingEntities;
    if (outVisibilitySet.frustumTestsDisabled) {
        GetAllIndexedEntities(survivingEntities);
    } else {
        m_spatialBvh.QueryFrustum(cullFrustum, survivingEntities);
    }

    AppendUnindexableEntities(survivingEntities);
    BuildCandidatesFromSurvivingEntities(survivingEntities, entityManager, assetManager, assetDatabase,
                                           outVisibilitySet);
}

void MeshRenderer::BuildVisibilitySet(Scene::IEntityManager& entityManager,
                                            Asset::IAssetManager& assetManager,
                                            Asset::IAssetDatabase& assetDatabase,
                                            const std::vector<Math::Frustum>& cullFrustums,
                                            MeshVisibilitySet& outVisibilitySet) const {
    if (m_spatialBvh.IsEmpty()) {
        SynchronizeSpatialAcceleration(entityManager, assetManager, assetDatabase);
    }

    std::vector<Scene::Entity> survivingEntities;
    if (outVisibilitySet.frustumTestsDisabled || cullFrustums.empty()) {
        GetAllIndexedEntities(survivingEntities);
    } else {
        QueryFrustums(cullFrustums, survivingEntities);
    }

    AppendUnindexableEntities(survivingEntities);
    BuildCandidatesFromSurvivingEntities(survivingEntities, entityManager, assetManager, assetDatabase,
                                           outVisibilitySet);
}

void MeshRenderer::BuildVisibilitySet(Scene::IEntityManager& entityManager,
                                            Asset::IAssetManager& assetManager,
                                            Asset::IAssetDatabase& assetDatabase,
                                            const Math::Aabb& boundingRegion,
                                            MeshVisibilitySet& outVisibilitySet) const {
    if (m_spatialBvh.IsEmpty()) {
        SynchronizeSpatialAcceleration(entityManager, assetManager, assetDatabase);
    }

    std::vector<Scene::Entity> survivingEntities;
    if (outVisibilitySet.frustumTestsDisabled) {
        GetAllIndexedEntities(survivingEntities);
    } else {
        m_spatialBvh.QueryAabb(boundingRegion, survivingEntities);
    }

    AppendUnindexableEntities(survivingEntities);
    BuildCandidatesFromSurvivingEntities(survivingEntities, entityManager, assetManager, assetDatabase,
                                           outVisibilitySet);
}

void MeshRenderer::BuildVisibilitySet(Scene::IEntityManager& entityManager,
                                            Asset::IAssetManager& assetManager,
                                            Asset::IAssetDatabase& assetDatabase,
                                            MeshVisibilitySet& outVisibilitySet) const {
    if (m_spatialBvh.IsEmpty()) {
        SynchronizeSpatialAcceleration(entityManager, assetManager, assetDatabase);
    }

    std::vector<Scene::Entity> survivingEntities;
    if (!outVisibilitySet.frustumTestsDisabled && m_activeCullFrustum.has_value()) {
        m_spatialBvh.QueryFrustum(*m_activeCullFrustum, survivingEntities);
    } else if (!outVisibilitySet.frustumTestsDisabled && !m_activeCullFrustums.empty()) {
        QueryFrustums(m_activeCullFrustums, survivingEntities);
    } else if (!outVisibilitySet.frustumTestsDisabled && m_activeBoundingRegion.has_value()) {
        m_spatialBvh.QueryAabb(*m_activeBoundingRegion, survivingEntities);
    } else {
        GetAllIndexedEntities(survivingEntities);
    }

    AppendUnindexableEntities(survivingEntities);
    BuildCandidatesFromSurvivingEntities(survivingEntities, entityManager, assetManager, assetDatabase,
                                           outVisibilitySet);
}

void MeshRenderer::CullVisibilitySetInto(const MeshVisibilitySet& visibilitySet,
                                               const Math::Frustum& cullFrustum,
                                               RenderQueue& outQueue) const {
    outQueue.Clear();

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
    // candidates without calling BuildSpatialClusters still gets correct output rather than an
    // empty frame. Correctness must not depend on the optimization having run.
    const auto cullRange = [&](const std::size_t first, const std::size_t count) {
        for (std::size_t index = first; index < first + count; ++index) {
            const MeshVisibilityCandidate& candidate = visibilitySet.candidates[index];
            MeshRenderEligibility eligibility;
            if (visibilitySet.shadowPass) {
                if (!candidate.castsShadow) {
                    continue;
                }
            } else if (!candidate.drawsInView) {
                continue;
            }
            if (!TestMeshRenderVisibility(candidate.placement, cullFrustum, eligibility,
                                             visibilitySet.frustumTestsDisabled)) {
                continue;
            }

            // Copies, not moves: one candidate set feeds four queues in a frame, so a move here
            // would empty the handles the remaining cascades still need. AssetHandle copies are
            // refcount bumps, which is exactly what this wants - the handle outlives all four
            // queues anyway.
            // The queue item still owns its own handles: RenderQueue is returned by value from
            // the public ExtractRenderQueue, so it can outlive the visibility set that produced
            // it and cannot borrow that set's references. This is the one place the per-item cost
            // is genuinely load-bearing, and it is paid only for candidates that survived culling
            // rather than for every candidate in the scene.
            const MeshVisibilityAssetPair& assetPair = visibilitySet.assetPairs[candidate.assetPairIndex];
            float sortDepth = eligibility.sortDepth;
            if (!candidate.sortingUseAabbCenter) {
                const Math::Vector3 origin{eligibility.worldMatrix.m[0][3], eligibility.worldMatrix.m[1][3],
                                              eligibility.worldMatrix.m[2][3]};
                sortDepth = cullFrustum.planes[kNearPlaneIndex].GetSignedDistance(origin);
            }
            sortDepth = Scene::ApplyRenderInstance3DSortingOffset(sortDepth, candidate.sortingOffset);
            if (candidate.overlay) {
                sortDepth = Scene::ApplySurfaceInstance3DOverlaySortBias(sortDepth);
            }
            if (!std::isfinite(sortDepth)) {
                continue;
            }
            RenderItem item{eligibility.worldMatrix, assetPair.meshHandle, assetPair.materialHandle, sortDepth,
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
        cullRange(0U, visibilitySet.candidates.size());
        return;
    }
    for (const MeshVisibilitySet::CandidateCluster& cluster : visibilitySet.clusters) {
        // A frozen cull takes every cluster: rejecting one here would drop candidates the disabled
        // frustum test was asked to keep, and the whole point of the freeze is that nothing is
        // dropped for being off-screen.
        if (!visibilitySet.frustumTestsDisabled && !cullFrustum.Intersects(cluster.bounds)) {
            continue;
        }
        cullRange(cluster.first, cluster.count);
    }
}

void MeshRenderer::ExtractRenderQueueInto(Scene::IEntityManager& entityManager,
                                            Asset::IAssetManager& assetManager,
                                            Asset::IAssetDatabase& assetDatabase,
                                            const Math::Frustum& cullFrustum, RenderQueue& outQueue) const {
    // Kept as the single-frustum entry point, now expressed as build-then-cull rather than a
    // second copy of the walk. A caller that culls once pays nothing for the split; a caller that
    // culls four times calls the two halves directly and pays the walk once.
    MeshVisibilitySet visibilitySet;
    BuildVisibilitySet(entityManager, assetManager, assetDatabase, cullFrustum, visibilitySet);
    CullVisibilitySetInto(visibilitySet, cullFrustum, outQueue);
}

} // namespace UVE::Render
