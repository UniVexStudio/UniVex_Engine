// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "prefab_system.h"

#include <exception>
#include <optional>
#include <vector>

#include "asset_content_fingerprint.h"

#include "logging_macros.h"
#include "hierarchy_component.h"
#include "prefab_instance_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "prefab_revision_policy.h"

namespace UVE::Scene {
namespace {

void DestroySubtree(IEntityManager& entityManager, ISceneGraph& sceneGraph, const Entity root) {
    if (!entityManager.IsAlive(root)) {
        return;
    }
    const std::vector<Entity> children = sceneGraph.GetChildren(entityManager, root);
    for (const Entity child : children) {
        DestroySubtree(entityManager, sceneGraph, child);
    }
    entityManager.DestroyEntity(root);
}

} // namespace

std::optional<std::uint64_t> ComputePrefabSourceRevision(const std::filesystem::path& path) {
    const std::optional<Asset::AssetContentFingerprint> fingerprint =
        Asset::ComputeAssetContentFingerprint(path);
    if (!fingerprint.has_value() || fingerprint->byteCount == 0U) {
        return std::nullopt;
    }
    std::uint64_t revision = fingerprint->hash;
    if (revision == 0U) {
        revision = fingerprint->byteCount;
    }
    return revision == 0U ? std::optional<std::uint64_t>{1U} : std::optional<std::uint64_t>{revision};
}

Asset::AssetGuid PrefabSystem::SavePrefab(IEntityManager& entityManager,
                                                    Asset::IAssetDatabase& assetDatabase,
                                                    Entity rootEntity, const std::filesystem::path& path) {
    // A `.uventity` is the same envelope; the extension only tells the editor it is an entity asset.
    // A `.uvwbp` (Widget Blueprint) is one too, its graph kept beside the widgets in the payload.
    if (path.empty() ||
        (path.extension() != ".uvprefab" && path.extension() != ".uventity" && path.extension() != ".uvwbp")) {
        UVE_ERROR("PrefabSystem: prefab path must use the .uvprefab, .uventity or .uvwbp extension");
        return Asset::kInvalidAssetGuid;
    }
    const bool saved =
        m_sceneSerializer.Save(entityManager, {rootEntity}, path, SceneAssetType::Prefab);
    if (!saved) {
        UVE_ERROR("PrefabSystem: failed to save prefab to \"{}\"", path.string());
        return Asset::kInvalidAssetGuid;
    }

    Asset::AssetGuid guid = Asset::kInvalidAssetGuid;
    try {
        guid = assetDatabase.Register(path);
        if (guid == Asset::kInvalidAssetGuid) {
            UVE_ERROR("PrefabSystem: asset database rejected prefab registration for \"{}\"", path.string());
            return Asset::kInvalidAssetGuid;
        }
        static_cast<void>(assetDatabase.Save());
    } catch (const std::exception& exception) {
        UVE_ERROR("PrefabSystem: asset database threw while registering prefab \"{}\": {}", path.string(),
                  exception.what());
        return Asset::kInvalidAssetGuid;
    } catch (...) {
        UVE_ERROR("PrefabSystem: asset database threw an unknown exception while registering prefab \"{}\"",
                  path.string());
        return Asset::kInvalidAssetGuid;
    }
    return guid;
}

Entity PrefabSystem::Instantiate(IEntityManager& entityManager, ISceneGraph& sceneGraph,
                                           Asset::IAssetDatabase& assetDatabase,
                                           Asset::AssetGuid prefabGuid, Entity parent) {
    std::filesystem::path path;
    try {
        path = assetDatabase.Resolve(prefabGuid);
    } catch (const std::exception& exception) {
        UVE_ERROR("PrefabSystem: asset database threw while resolving prefab GUID {}: {}", prefabGuid.value,
                  exception.what());
        return kInvalidEntity;
    } catch (...) {
        UVE_ERROR("PrefabSystem: asset database threw an unknown exception while resolving prefab GUID {}",
                  prefabGuid.value);
        return kInvalidEntity;
    }
    if (path.empty()) {
        UVE_ERROR("PrefabSystem: unknown prefab GUID {}", prefabGuid.value);
        return kInvalidEntity;
    }
    const std::optional<std::uint64_t> sourceRevision = ComputePrefabSourceRevision(path);
    if (!sourceRevision.has_value()) {
        UVE_ERROR("PrefabSystem: could not compute source revision for prefab GUID {}", prefabGuid.value);
        return kInvalidEntity;
    }
    return InstantiateWithRevision(entityManager, sceneGraph, assetDatabase, prefabGuid, parent, *sourceRevision);
}

Entity PrefabSystem::InstantiateWithRevision(
    IEntityManager& entityManager, ISceneGraph& sceneGraph, Asset::IAssetDatabase& assetDatabase,
    Asset::AssetGuid prefabGuid, Entity parent, const std::uint64_t sourceRevision) {
    if (sourceRevision == 0U) {
        UVE_ERROR("PrefabSystem: source revision must be nonzero for prefab GUID {}", prefabGuid.value);
        return kInvalidEntity;
    }
    if (parent != kInvalidEntity && !entityManager.IsAlive(parent)) {
        UVE_ERROR("PrefabSystem: cannot instantiate prefab GUID {} under an invalid parent ({}, {})",
                  prefabGuid.value, parent.index, parent.generation);
        return kInvalidEntity;
    }
    std::filesystem::path path;
    try {
        path = assetDatabase.Resolve(prefabGuid);
    } catch (const std::exception& exception) {
        UVE_ERROR("PrefabSystem: asset database threw while resolving prefab GUID {}: {}", prefabGuid.value,
                  exception.what());
        return kInvalidEntity;
    } catch (...) {
        UVE_ERROR("PrefabSystem: asset database threw an unknown exception while resolving prefab GUID {}",
                  prefabGuid.value);
        return kInvalidEntity;
    }
    if (path.empty()) {
        UVE_ERROR("PrefabSystem: unknown prefab GUID {}", prefabGuid.value);
        return kInvalidEntity;
    }

    const std::vector<Entity> roots = m_sceneSerializer.Load(entityManager, path);
    if (roots.empty()) {
        UVE_ERROR("PrefabSystem: failed to instantiate prefab \"{}\" (GUID {})", path.string(),
                  prefabGuid.value);
        return kInvalidEntity;
    }

    // A prefab file always has exactly one root (SavePrefab only ever serializes one entity).
    const Entity root = roots.front();

    // The loaded root may already carry a PrefabInstanceComponent (e.g. it was itself an
    // instance of a different prefab when this one was saved) — overwrite it so the tag always
    // names the prefab this entity was *directly* instantiated from.
    if (entityManager.HasComponent<PrefabInstanceComponent>(root)) {
        entityManager.RemoveComponent<PrefabInstanceComponent>(root);
    }
    entityManager.AddComponent<PrefabInstanceComponent>(
        root, PrefabInstanceComponent{prefabGuid, {}, sourceRevision, sourceRevision});

    if (parent != kInvalidEntity) {
        const bool hasTransform = entityManager.HasComponent<TransformComponent>(root);
        const bool hasWorldTransform = entityManager.HasComponent<WorldTransformComponent>(root);
        const bool hasHierarchy = entityManager.HasComponent<HierarchyComponent>(root);
        if (!hasTransform && !hasWorldTransform && !hasHierarchy) {
            // A parented prefab must participate in the scene graph even when its authored root
            // had no transform components; preserve transform-less roots only for unparented use.
            sceneGraph.AttachTransform(entityManager, root, TransformComponent{});
        }
        sceneGraph.SetParent(entityManager, root, parent);
    }

    return root;
}

PrefabRefreshResult PrefabSystem::RefreshInstance(
    IEntityManager& entityManager, ISceneGraph& sceneGraph,
    Asset::IAssetDatabase& assetDatabase, const Entity instanceRoot, const bool forceRefresh) {
    if (!entityManager.IsAlive(instanceRoot) ||
        !entityManager.HasComponent<PrefabInstanceComponent>(instanceRoot)) {
        return {PrefabRefreshCode::InvalidInstance, instanceRoot, 0U,
                "Prefab refresh requires a live entity with a prefab instance component."};
    }

    const PrefabInstanceComponent instance =
        entityManager.GetComponent<PrefabInstanceComponent>(instanceRoot);
    std::filesystem::path path;
    try {
        path = assetDatabase.Resolve(instance.sourcePrefabGuid);
    } catch (const std::exception&) {
        return {PrefabRefreshCode::SourceUnavailable, instanceRoot, 0U,
                "Prefab refresh could not resolve the source asset."};
    } catch (...) {
        return {PrefabRefreshCode::SourceUnavailable, instanceRoot, 0U,
                "Prefab refresh could not resolve the source asset."};
    }
    const std::optional<std::uint64_t> observedRevision = ComputePrefabSourceRevision(path);
    if (path.empty() || !observedRevision.has_value()) {
        return {PrefabRefreshCode::SourceUnavailable, instanceRoot, 0U,
                "Prefab refresh could not resolve or fingerprint the source asset."};
    }

    const PrefabRevisionRefreshDecision decision =
        *observedRevision == instance.instanceRevision
            ? PrefabRevisionRefreshDecision::NoOp
            : (instance.overrides.empty() ? PrefabRevisionRefreshDecision::Refresh
                                           : PrefabRevisionRefreshDecision::MergeRequired);
    if (decision == PrefabRevisionRefreshDecision::NoOp) {
        return {PrefabRefreshCode::NoOp, instanceRoot, *observedRevision,
                "Prefab instance already matches its source revision."};
    }
    if (decision == PrefabRevisionRefreshDecision::MergeRequired && !forceRefresh) {
        return {PrefabRefreshCode::MergeRequired, instanceRoot, *observedRevision,
                "Prefab source changed while local overrides are present; merge is required."};
    }
    if (decision != PrefabRevisionRefreshDecision::Refresh &&
        !(forceRefresh && decision == PrefabRevisionRefreshDecision::MergeRequired)) {
        return {PrefabRefreshCode::SourceRevisionInvalid, instanceRoot, *observedRevision,
                "Prefab source revision cannot safely refresh this instance."};
    }

    Entity parent = kInvalidEntity;
    if (entityManager.HasComponent<HierarchyComponent>(instanceRoot)) {
        parent = entityManager.GetComponent<HierarchyComponent>(instanceRoot).parent;
    }
    const Entity replacement = InstantiateWithRevision(
        entityManager, sceneGraph, assetDatabase, instance.sourcePrefabGuid, parent, *observedRevision);
    if (replacement == kInvalidEntity) {
        return {PrefabRefreshCode::SourceUnavailable, instanceRoot, *observedRevision,
                "Prefab refresh could not load a replacement source subtree."};
    }

    DestroySubtree(entityManager, sceneGraph, instanceRoot);
    return {PrefabRefreshCode::Refreshed, replacement, *observedRevision,
            "Prefab instance refreshed from its current source revision."};
}

} // namespace UVE::Scene
