// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/scene/scene_graph_uve.h"

#include <cmath>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "uve/logging/assert_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/platform/platform_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/world_transform_component_uve.h"

namespace UVE::Scene {

namespace {

/// True iff `potentialAncestor` appears in `entity`'s parent chain, or the chain is malformed.
/// The visited set prevents a pre-existing malformed cycle from making this public mutation path
/// loop forever in a release build.
bool IsAncestorOrMalformedUVE(IEntityManagerUVE& entityManager, EntityUVE potentialAncestor,
                              EntityUVE entity) {
    std::unordered_set<EntityUVE> visited;
    EntityUVE current = entity;
    while (current != kInvalidEntityUVE) {
        if (!entityManager.IsAliveUVE(current) ||
            !entityManager.HasComponentUVE<HierarchyComponentUVE>(current) ||
            !visited.insert(current).second) {
            return true;
        }
        if (current == potentialAncestor) {
            return true;
        }
        current = entityManager.GetComponentUVE<HierarchyComponentUVE>(current).parent;
    }
    return false;
}

[[nodiscard]] bool IsFiniteWorldTransformUVE(const WorldTransformComponentUVE& transform) noexcept {
    return std::isfinite(transform.worldPosition.x) && std::isfinite(transform.worldPosition.y) &&
           std::isfinite(transform.worldPosition.z) && Math::IsFiniteUVE(transform.worldRotation) &&
           std::isfinite(transform.worldScale.x) && std::isfinite(transform.worldScale.y) &&
           std::isfinite(transform.worldScale.z);
}

} // namespace

void SceneGraphUVE::AttachTransformUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                        const TransformComponentUVE& localTransform) {
    if (!entityManager.IsAliveUVE(entity) || entityManager.HasComponentUVE<TransformComponentUVE>(entity) ||
        entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity) ||
        entityManager.HasComponentUVE<HierarchyComponentUVE>(entity) ||
        !IsTransformComponentValidUVE(localTransform)) {
        UVE_ASSERT(entityManager.IsAliveUVE(entity));
        UVE_ASSERT(!entityManager.HasComponentUVE<TransformComponentUVE>(entity));
        UVE_ASSERT(IsTransformComponentValidUVE(localTransform));
        return;
    }

    entityManager.AddComponentUVE<TransformComponentUVE>(entity, localTransform);
    entityManager.AddComponentUVE<WorldTransformComponentUVE>(entity);
    entityManager.AddComponentUVE<HierarchyComponentUVE>(entity);
}

void SceneGraphUVE::SetLocalTransformUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                         const TransformComponentUVE& localTransform) {
    if (!entityManager.IsAliveUVE(entity) || !entityManager.HasComponentUVE<TransformComponentUVE>(entity) ||
        !entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity) ||
        !IsTransformComponentValidUVE(localTransform)) {
        UVE_ASSERT(entityManager.HasComponentUVE<TransformComponentUVE>(entity));
        UVE_ASSERT(IsTransformComponentValidUVE(localTransform));
        return;
    }

    entityManager.GetComponentUVE<TransformComponentUVE>(entity) = localTransform;
    entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity).dirty = true;
}

void SceneGraphUVE::SetParentUVE(IEntityManagerUVE& entityManager, EntityUVE child, EntityUVE newParent) {
    const bool invalidChild = !entityManager.IsAliveUVE(child) ||
                              !entityManager.HasComponentUVE<HierarchyComponentUVE>(child) ||
                              !entityManager.HasComponentUVE<WorldTransformComponentUVE>(child);
    const bool invalidParent =
        newParent != kInvalidEntityUVE &&
        (!entityManager.IsAliveUVE(newParent) ||
         !entityManager.HasComponentUVE<HierarchyComponentUVE>(newParent));
    const bool createsCycle = !invalidChild && !invalidParent && newParent != kInvalidEntityUVE &&
                              IsAncestorOrMalformedUVE(entityManager, child, newParent);
    if (invalidChild || invalidParent || createsCycle) {
        UVE_ASSERT(!invalidChild);
        UVE_ASSERT(!invalidParent);
        UVE_ASSERT(!createsCycle);
        return;
    }

    entityManager.GetComponentUVE<HierarchyComponentUVE>(child).parent = newParent;
    entityManager.GetComponentUVE<WorldTransformComponentUVE>(child).dirty = true;
}

bool SceneGraphUVE::ResolveVisibilityUVE(const PendingEntityUVE& item, const bool parentVisible) noexcept {
    // No component means visible, and means the parent's state passes straight through. An entity
    // without the component is not a break in the chain - hiding a parent must still hide a
    // grandchild whose intermediate node never opted into having a visibility flag.
    if (item.visibility == nullptr) {
        return parentVisible;
    }

    // Two fields, one derived: `visible` is the author's switch and is never written here, while
    // `visibleInHierarchy` is the answer. Writing only the derived field is what lets a child stay
    // hidden after its parent is shown again - the child's own choice was never overwritten.
    const bool resolved = parentVisible && item.visibility->visible;
    item.visibility->visibleInHierarchy = resolved;
    return resolved;
}

void SceneGraphUVE::UpdateUVE(IEntityManagerUVE& entityManager) {
    // Rewritten for cost, not behaviour. The previous shape was measured at 1353us per frame on a
    // 5000-entity scene in which NOTHING was dirty - a completely static scene paying more than
    // the whole render extraction path. Two causes, both removed here and neither changing what
    // this function computes:
    //
    //   1. `pending.erase(pending.begin() + index)` shifted every following element on each
    //      processed entity, making a flat scene O(n^2). Scaling 1000 -> 5000 entities cost 7.5x,
    //      not 5x, which is that quadratic showing up in measurement.
    //   2. Every visit re-resolved the entity's components through GetComponentUVE hash lookups,
    //      including for entities that were clean and would not be recomputed.
    //
    // The sweep semantics are deliberately IDENTICAL: root-first level order, a parent's
    // recomputation forcing every child to recompute, an invalid parent invalidating its subtree,
    // and a leftover remainder still meaning a cycle. Only the bookkeeping changed.
    m_pendingScratch.clear();
    entityManager.ForEachUVE<HierarchyComponentUVE, TransformComponentUVE, WorldTransformComponentUVE>(
        [this, &entityManager](EntityUVE entity, HierarchyComponentUVE& hierarchy, TransformComponentUVE& local,
                               WorldTransformComponentUVE& world) {
            // The component pointers are captured during the walk that already found them. The ECS
            // guarantees they stay valid for the rest of this function because nothing here
            // creates, destroys, or re-archetypes an entity - it only writes to existing
            // components, which never moves a row.
            // Resolved here rather than in the sweep: the sweep may revisit an entity across
            // several passes while waiting for its parent, and asking the ECS each time whether an
            // optional component exists would pay that lookup repeatedly for nothing.
            VisibilityComponentUVE* const visibility =
                entityManager.HasComponentUVE<VisibilityComponentUVE>(entity)
                    ? &entityManager.GetComponentUVE<VisibilityComponentUVE>(entity)
                    : nullptr;
            m_pendingScratch.push_back(
                PendingEntityUVE{entity, hierarchy.parent, &local, &world, visibility});
        });

    // Level-order sweep, root-first: repeatedly process any pending entity whose parent has
    // already been processed this pass (or is a root), tracking valid/invalid derived state and
    // whether each valid processed entity's world transform was actually recomputed (as opposed
    // to merely visited) - a processed parent's recomputation unconditionally forces every child
    // to recompute too, even if the child's own dirty flag is false. No persistent tree structure
    // is needed: HierarchyComponentUVE::parent is already the full source of truth, and
    // SetParentUVE() already prevents cycles.
    m_passStateScratch.clear();
    m_passStateScratch.reserve(m_pendingScratch.size());

    // Compaction replaces erase-from-the-middle: each sweep writes the entities it could not yet
    // process back to the front of the same buffer, so a pass costs O(remaining) rather than
    // O(remaining^2). A flat scene of roots now completes in exactly one sweep with no shifting at
    // all, which is the overwhelmingly common case.
    bool madeProgress = true;
    while (madeProgress && !m_pendingScratch.empty()) {
        madeProgress = false;
        std::size_t writeIndex = 0U;

        for (std::size_t index = 0; index < m_pendingScratch.size(); ++index) {
            const PendingEntityUVE& item = m_pendingScratch[index];
            const bool parentIsRoot = (item.parent == kInvalidEntityUVE);
            const auto parentIt = parentIsRoot ? m_passStateScratch.end() : m_passStateScratch.find(item.parent);
            const bool parentReady = parentIsRoot || parentIt != m_passStateScratch.end();

            if (!parentReady) {
                m_pendingScratch[writeIndex] = item;
                ++writeIndex;
                continue;
            }

            WorldTransformComponentUVE& world = *item.world;
            if (!parentIsRoot && !parentIt->second.valid) {
                world.dirty = true;
                // Visibility is still resolved on this arm. An invalid world transform is a
                // separate failure from being hidden, and skipping the inheritance here would let
                // a non-finite transform anywhere in a subtree quietly un-hide everything beneath
                // a hidden ancestor.
                const bool inherited = ResolveVisibilityUVE(item, parentIt->second.visibleInHierarchy);
                m_passStateScratch.emplace(item.entity, WorldTransformPassStateUVE{false, false, inherited});
                madeProgress = true;
                continue;
            }

            const bool parentWasRecomputed = !parentIsRoot && parentIt->second.recomputed;
            const bool shouldRecompute = world.dirty || parentWasRecomputed;
            bool publishedValid = IsFiniteWorldTransformUVE(world);
            if (shouldRecompute) {
                const TransformComponentUVE& local = *item.local;
                WorldTransformComponentUVE candidate = world;
                if (parentIsRoot) {
                    candidate.worldPosition = local.localPosition;
                    candidate.worldRotation = local.localRotation;
                    candidate.worldScale = local.localScale;
                } else {
                    const WorldTransformComponentUVE& parentWorld =
                        entityManager.GetComponentUVE<WorldTransformComponentUVE>(item.parent);
                    candidate.worldScale = parentWorld.worldScale * local.localScale;
                    candidate.worldRotation = Math::MultiplyUVE(parentWorld.worldRotation, local.localRotation);
                    candidate.worldPosition =
                        parentWorld.worldPosition + Math::RotateVectorUVE(parentWorld.worldRotation,
                                                                            parentWorld.worldScale * local.localPosition);
                }
                publishedValid = IsFiniteWorldTransformUVE(candidate);
                if (publishedValid) {
                    candidate.dirty = false;
                    world = candidate;
                } else {
                    world.dirty = true;
                }
            }

            const bool parentVisible = parentIsRoot || parentIt->second.visibleInHierarchy;
            const bool inherited = ResolveVisibilityUVE(item, parentVisible);
            m_passStateScratch.emplace(
                item.entity,
                WorldTransformPassStateUVE{publishedValid, shouldRecompute && publishedValid, inherited});
            madeProgress = true;
        }

        m_pendingScratch.resize(writeIndex);
    }

    // A non-empty remainder here means a cycle slipped past SetParentUVE()'s guard - a genuine
    // engine bug, not user error, worth catching in debug builds.
    UVE_ASSERT(m_pendingScratch.empty());
}

std::vector<EntityUVE> SceneGraphUVE::GetChildrenUVE(IEntityManagerUVE& entityManager, EntityUVE parent) {
    std::vector<EntityUVE> children;
    entityManager.ForEachUVE<HierarchyComponentUVE>(
        [&children, parent](EntityUVE entity, HierarchyComponentUVE& hierarchy) {
            if (hierarchy.parent == parent) {
                children.push_back(entity);
            }
        });
    return children;
}

} // namespace UVE::Scene
