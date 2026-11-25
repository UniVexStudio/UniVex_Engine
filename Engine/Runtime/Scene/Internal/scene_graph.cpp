// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "scene_graph.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "engine_assert.h"
#include "quaternion.h"
#include "trs.h"
#include "vector3.h"
#include "platform.h"
#include "hierarchy_component.h"
#include "world_transform_component.h"
#include "entity_enabled.h"

namespace UVE::Scene {

namespace {

/// True iff `potentialAncestor` appears in `entity`'s parent chain, or the chain is malformed.
/// The visited set prevents a pre-existing malformed cycle from making this public mutation path
/// loop forever in a release build.
bool IsAncestorOrMalformed(IEntityManager& entityManager, Entity potentialAncestor,
                              Entity entity) {
    std::unordered_set<Entity> visited;
    Entity current = entity;
    while (current != kInvalidEntity) {
        if (!entityManager.IsAlive(current) ||
            !entityManager.HasComponent<HierarchyComponent>(current) ||
            !visited.insert(current).second) {
            return true;
        }
        if (current == potentialAncestor) {
            return true;
        }
        current = entityManager.GetComponent<HierarchyComponent>(current).parent;
    }
    return false;
}

} // namespace

bool ISceneGraph::ResetInterpolationSubtree(IEntityManager& entityManager, const Entity root) {
    if (!entityManager.IsAlive(root)) return false;
    ChildrenIndex children;
    children.Rebuild(entityManager);
    std::vector<Entity> pending{root};
    std::unordered_set<Entity> visited;
    std::vector<Entity> reset;
    while (!pending.empty()) {
        const auto entity = pending.back();
        pending.pop_back();
        if (!visited.insert(entity).second || !entityManager.IsAlive(entity)) continue;
        if (entityManager.HasComponent<PhysicsInterpolationComponent>(entity)) reset.push_back(entity);
        for (const auto child : children.GetChildren(entity)) pending.push_back(child);
    }
    // Complete all allocations before invalidating any history.
    for (const auto entity : reset) {
        entityManager.GetComponent<PhysicsInterpolationComponent>(entity).hasPreviousPose = false;
    }
    return true;
}

bool ISceneGraph::TryTeleportLocalTransform(IEntityManager& entityManager, const Entity entity,
                                                  const TransformComponent& transform) {
    if (!entityManager.IsAlive(entity) || !entityManager.HasComponent<TransformComponent>(entity) ||
        !entityManager.HasComponent<WorldTransformComponent>(entity) ||
        !entityManager.HasComponent<HierarchyComponent>(entity) ||
        !IsTransformComponentValid(transform)) return false;
    // Build/reset the subtree before publishing the authored change, so allocation failure in
    // the temporary index cannot leave a changed transform with stale smoothing history.
    if (!ResetInterpolationSubtree(entityManager, entity)) return false;
    SetLocalTransform(entityManager, entity, transform);
    return true;
}

void SceneGraph::AttachTransform(IEntityManager& entityManager, Entity entity,
                                        const TransformComponent& localTransform) {
    if (!entityManager.IsAlive(entity) || entityManager.HasComponent<TransformComponent>(entity) ||
        entityManager.HasComponent<WorldTransformComponent>(entity) ||
        entityManager.HasComponent<HierarchyComponent>(entity) ||
        !IsTransformComponentValid(localTransform)) {
        UVE_ASSERT(entityManager.IsAlive(entity));
        UVE_ASSERT(!entityManager.HasComponent<TransformComponent>(entity));
        UVE_ASSERT(IsTransformComponentValid(localTransform));
        return;
    }

    entityManager.AddComponent<TransformComponent>(entity, localTransform);
    entityManager.AddComponent<WorldTransformComponent>(entity);
    entityManager.AddComponent<HierarchyComponent>(entity);
}

void SceneGraph::SetLocalTransform(IEntityManager& entityManager, Entity entity,
                                         const TransformComponent& localTransform) {
    if (!entityManager.IsAlive(entity) || !entityManager.HasComponent<TransformComponent>(entity) ||
        !entityManager.HasComponent<WorldTransformComponent>(entity) ||
        !IsTransformComponentValid(localTransform)) {
        UVE_ASSERT(entityManager.HasComponent<TransformComponent>(entity));
        UVE_ASSERT(IsTransformComponentValid(localTransform));
        return;
    }

    entityManager.GetComponent<TransformComponent>(entity) = localTransform;
    entityManager.GetComponent<WorldTransformComponent>(entity).dirty = true;
}

void SceneGraph::SetParent(IEntityManager& entityManager, Entity child, Entity newParent) {
    const bool invalidChild = !entityManager.IsAlive(child) ||
                              !entityManager.HasComponent<HierarchyComponent>(child);
    const bool invalidParent =
        newParent != kInvalidEntity &&
        (!entityManager.IsAlive(newParent) ||
         !entityManager.HasComponent<HierarchyComponent>(newParent));
    const bool createsCycle = !invalidChild && !invalidParent && newParent != kInvalidEntity &&
                              IsAncestorOrMalformed(entityManager, child, newParent);
    if (invalidChild || invalidParent || createsCycle) {
        UVE_ASSERT(!invalidChild);
        UVE_ASSERT(!invalidParent);
        UVE_ASSERT(!createsCycle);
        return;
    }

    HierarchyComponent& hierarchy = entityManager.GetComponent<HierarchyComponent>(child);
    if (hierarchy.parent != newParent) {
        static_cast<void>(ResetInterpolationSubtree(entityManager, child));
        static_cast<void>(AssignParentAndAdvanceSiblingOrder(hierarchy, newParent));
    }
    // A pure Object (no transform of its own) moves in the hierarchy only; nothing composes from it.
    if (entityManager.HasComponent<WorldTransformComponent>(child)) {
        entityManager.GetComponent<WorldTransformComponent>(child).dirty = true;
    }
}

void SceneGraph::ResolveVisibilityParents(IEntityManager& entityManager) {
    // Walk each redirect chain to its end, then apply every `visible` switch along the way.
    //
    // Chains are followed rather than resolved in one hop because a visibility parent may itself
    // redirect. Depth is bounded by the number of entities carrying the component, so the guard
    // below is what makes a cycle terminate rather than a promise that one cannot exist.
    // Copied out before the walk: the lambda must not reach back into the object, and the bound is
    // fixed for this pass anyway.
    const std::size_t chainLimit = m_visibilityRedirectCount + 1U;
    entityManager.ForEach<VisibilityComponent>(
        [&entityManager, chainLimit](const Entity, VisibilityComponent& visibility) {
            if (visibility.visibilityParent == kInvalidEntity) {
                return; // Transform-parent inheritance; already resolved by the main sweep.
            }

            // Start from the entity's own switch and its transform-inherited state. The redirect
            // REPLACES the inherited half, not the entity's own choice: an object hidden in its
            // own right stays hidden no matter what it points at.
            bool resolved = visibility.visible;

            Entity current = visibility.visibilityParent;
            std::size_t guard = 0U;
            const std::size_t limit = chainLimit;
            while (current != kInvalidEntity && guard <= limit) {
                ++guard;
                if (!entityManager.IsAlive(current)) {
                    // A dangling target - the object was deleted, or the reference came from a file
                    // that no longer matches the scene. Treated as "no redirect" rather than as
                    // hidden: losing a reference should not make geometry silently disappear.
                    return;
                }
                if (!entityManager.HasComponent<VisibilityComponent>(current)) {
                    // A target with no component is visible, and has nothing further to redirect
                    // to, so the chain ends here with the state gathered so far.
                    break;
                }
                const VisibilityComponent& target =
                    entityManager.GetComponent<VisibilityComponent>(current);
                if (!target.visible) {
                    resolved = false;
                }
                if (target.visibilityParent == kInvalidEntity) {
                    // The end of the chain: fold in the target's own inherited state, which the
                    // main sweep already computed from ITS transform parent.
                    if (!target.visibleInHierarchy) {
                        resolved = false;
                    }
                    break;
                }
                current = target.visibilityParent;
            }

            if (guard > limit) {
                // A cycle. Falling back to the transform-parent answer keeps the entity visible
                // and predictable instead of picking an arbitrary winner or looping - the same
                // reasoning as the dangling case: a bad reference must not hide geometry.
                return;
            }
            visibility.visibleInHierarchy = resolved;
        });
}

bool SceneGraph::ResolveInterpolationMode(const PendingEntity& item, const bool parentInterpolated) noexcept {
    if (item.interpolation == nullptr) {
        return parentInterpolated;
    }
    bool resolved = parentInterpolated;
    if (item.interpolation->mode == PoseSmoothing::Blended) {
        resolved = true;
    } else if (item.interpolation->mode == PoseSmoothing::Exact) {
        resolved = false;
    }
    item.interpolation->interpolatedInHierarchy = resolved;
    return resolved;
}

bool SceneGraph::ResolveInterpolation(IEntityManager& entityManager, const PendingEntity& item,
                                            const bool parentInterpolated, const WorldTransformComponent& world,
                                            const bool poseChanged, const bool poseValid) {
    // No component means the entity is drawn at its simulated pose, and means the parent's answer
    // passes straight through - an intermediate object that never opted in must not break a rig's
    // inheritance chain, the same rule visibility follows.
    if (item.interpolation == nullptr) {
        return parentInterpolated;
    }

    PhysicsInterpolationComponent& interpolation = *item.interpolation;
    const bool wasBlended = interpolation.interpolatedInHierarchy;
    const bool resolved = ResolveInterpolationMode(item, parentInterpolated);
    if (!IsPhysicsInterpolationComponentValid(interpolation)) {
        interpolation.hasPreviousPose = false;
        interpolation.interpolatedInHierarchy = false;
        return false;
    }
    // DisabledComponent is deliberately a per-entity simulation tag, not visibility or
    // inherited disabling. Keep that existing policy; only discard this entity's smoothing.
    if (!IsEntityEnabled(entityManager, item.entity)) {
        interpolation.hasPreviousPose = false;
        return resolved;
    }
    if (resolved && !wasBlended) interpolation.hasPreviousPose = false;
    if (!poseValid) return resolved;

    const bool differentPose = world.worldPosition != interpolation.currentPosition ||
                               world.worldRotation != interpolation.currentRotation ||
                               world.worldScale != interpolation.currentScale;
    // Recompute is not necessarily motion (e.g. an idempotent setter). Do not collapse a useful
    // previous/current pair on a stationary pass. Explicit reset seeds even when nothing moved.
    if ((poseChanged && differentPose) || !interpolation.hasPreviousPose) {
        if (interpolation.hasPreviousPose) {
            interpolation.previousPosition = interpolation.currentPosition;
            interpolation.previousRotation = interpolation.currentRotation;
            interpolation.previousScale = interpolation.currentScale;
        } else {
            // First pose: seed previous to the same value so the object's first drawn frame
            // blends from where it actually is, rather than flinging in from a default-constructed
            // origin. hasPreviousPose stays meaningful - the blend still reports not-ready until a
            // second, genuinely different pose has arrived.
            interpolation.previousPosition = world.worldPosition;
            interpolation.previousRotation = world.worldRotation;
            interpolation.previousScale = world.worldScale;
        }
        interpolation.currentPosition = world.worldPosition;
        interpolation.currentRotation = world.worldRotation;
        interpolation.currentScale = world.worldScale;
        interpolation.hasPreviousPose = true;
    }
    return resolved;
}

std::optional<ResolvedObjectModes> SceneGraph::TryGetResolvedObjectModes(
    const Entity entity) const {
    const auto iterator = m_passStateScratch.find(entity);
    if (iterator == m_passStateScratch.end()) {
        return std::nullopt;
    }
    // Every stored state went through ResolveInheritedModes, which never leaves Inherit behind
    // (Inherit at the top of a hierarchy resolves to the default), so these are final answers.
    return ResolvedObjectModes{iterator->second.processModeInHierarchy,
                                iterator->second.threadGroupModeInHierarchy,
                                iterator->second.autoTranslateModeInHierarchy};
}

void SceneGraph::ResolveInheritedModes(const PendingEntity& item,
                                             const WorldTransformPassState& parentState,
                                             WorldTransformPassState& outState) noexcept {
    // Each of the three has the same shape: no component means the parent's answer passes straight
    // through, so an intermediate object that never opted in does not break a subtree's chain.
    outState.processModeInHierarchy =
        item.process == nullptr
            ? ResolveTickMode(TickMode::Inherit, parentState.processModeInHierarchy)
            : ResolveTickMode(item.process->mode, parentState.processModeInHierarchy);
    if (item.process != nullptr) {
        item.process->resolvedModeInHierarchy = outState.processModeInHierarchy;
    }

    outState.threadGroupModeInHierarchy =
        ResolveHierarchyThreadGroupMode(item.threadGroup, parentState.threadGroupModeInHierarchy);

    outState.autoTranslateModeInHierarchy =
        ResolveHierarchyLocalizeMode(item.autoTranslate, parentState.autoTranslateModeInHierarchy);
}

bool SceneGraph::ResolveVisibility(const PendingEntity& item, const bool parentVisible) noexcept {
    // No component means visible, and means the parent's state passes straight through. An entity
    // without the component is not a break in the chain - hiding a parent must still hide a
    // grandchild whose intermediate object never opted into having a visibility flag.
    return ResolveInheritedVisibility(item.visibility, parentVisible);
}

void SceneGraph::Update(IEntityManager& entityManager) {
    // Rewritten for cost, not behaviour. The previous shape was measured at 1353us per frame on a
    // 5000-entity scene in which NOTHING was dirty - a completely static scene paying more than
    // the whole render extraction path. Two causes, both removed here and neither changing what
    // this function computes:
    //
    //   1. `pending.erase(pending.begin() + index)` shifted every following element on each
    //      processed entity, making a flat scene O(n^2). Scaling 1000 -> 5000 entities cost 7.5x,
    //      not 5x, which is that quadratic showing up in measurement.
    //   2. Every visit re-resolved the entity's components through GetComponent hash lookups,
    //      including for entities that were clean and would not be recomputed.
    //
    // The sweep semantics are deliberately IDENTICAL: root-first level order, a parent's
    // recomputation forcing every child to recompute, an invalid parent invalidating its subtree,
    // and a leftover remainder still meaning a cycle. Only the bookkeeping changed.
    m_pendingScratch.clear();
    // Recounted every update: entities and their redirects change between frames.
    m_visibilityRedirectCount = 0U;
    entityManager.ForEachSceneGraph<HierarchyComponent, TransformComponent, WorldTransformComponent>(
        GetCacheWriteAccess(), [this, &entityManager](Entity entity, HierarchyComponent& hierarchy, TransformComponent& local,
                               WorldTransformComponent& world) {
            // The component pointers are captured during the walk that already found them. The ECS
            // guarantees they stay valid for the rest of this function because nothing here
            // creates, destroys, or re-archetypes an entity - it only writes to existing
            // components, which never moves a row.
            // Resolved here rather than in the sweep: the sweep may revisit an entity across
            // several passes while waiting for its parent, and asking the ECS each time whether an
            // optional component exists would pay that lookup repeatedly for nothing.
            VisibilityComponent* const visibility =
                entityManager.HasComponent<VisibilityComponent>(entity)
                    ? &entityManager.GetComponent<VisibilityComponent>(entity)
                    : nullptr;
            if (visibility != nullptr && HasVisibilityRedirect(*visibility)) {
                ++m_visibilityRedirectCount;
            }
            PhysicsInterpolationComponent* const interpolation =
                entityManager.HasComponent<PhysicsInterpolationComponent>(entity)
                    ? &entityManager.GetComponent<PhysicsInterpolationComponent>(entity)
                    : nullptr;
            ProcessComponent* const process =
                entityManager.HasComponent<ProcessComponent>(entity)
                    ? &entityManager.GetComponent<ProcessComponent>(entity)
                    : nullptr;
            ThreadGroupComponent* const threadGroup =
                entityManager.HasComponent<ThreadGroupComponent>(entity)
                    ? &entityManager.GetComponent<ThreadGroupComponent>(entity)
                    : nullptr;
            AutoTranslateComponent* const autoTranslate =
                entityManager.HasComponent<AutoTranslateComponent>(entity)
                    ? &entityManager.GetComponent<AutoTranslateComponent>(entity)
                    : nullptr;
            m_pendingScratch.push_back(PendingEntity{entity, hierarchy.parent, &local, &world, visibility,
                                                        interpolation, process, threadGroup, autoTranslate});
        });

    // Pure Objects: in the hierarchy, with no transform of their own - the scene root is the first.
    // They take part in the sweep because their children wait on them and because the inherited
    // modes (Process, Thread Group, Auto Translate, and visibility passing through) must flow down
    // through them; leaving them out would strand every child waiting on a parent answer that never
    // arrives. They carry no transform, so nothing below composes from them: see `spatial`.
    entityManager.ForEach<HierarchyComponent>(
        [this, &entityManager](Entity entity, HierarchyComponent& hierarchy) {
            if (entityManager.HasComponent<WorldTransformComponent>(entity) &&
                entityManager.HasComponent<TransformComponent>(entity)) {
                return; // A spatial object, already gathered above.
            }
            const auto optional = [&entityManager, entity]<typename ComponentT>() -> ComponentT* {
                return entityManager.HasComponent<ComponentT>(entity) ? &entityManager.GetComponent<ComponentT>(entity)
                                                                         : nullptr;
            };
            VisibilityComponent* const visibility = optional.template operator()<VisibilityComponent>();
            if (visibility != nullptr && HasVisibilityRedirect(*visibility)) {
                ++m_visibilityRedirectCount;
            }
            m_pendingScratch.push_back(PendingEntity{
                entity, hierarchy.parent, nullptr, nullptr, visibility,
                optional.template operator()<PhysicsInterpolationComponent>(),
                optional.template operator()<ProcessComponent>(),
                optional.template operator()<ThreadGroupComponent>(),
                optional.template operator()<AutoTranslateComponent>()});
        });

    // Level-order sweep, root-first: repeatedly process any pending entity whose parent has
    // already been processed this pass (or is a root), tracking valid/invalid derived state and
    // whether each valid processed entity's world transform was actually recomputed (as opposed
    // to merely visited) - a processed parent's recomputation unconditionally forces every child
    // to recompute too, even if the child's own dirty flag is false. No persistent tree structure
    // is needed: HierarchyComponent::parent is already the full source of truth, and
    // SetParent() already prevents cycles.
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
            const PendingEntity& item = m_pendingScratch[index];
            const bool hasParent = (item.parent != kInvalidEntity);
            const auto parentIt = hasParent ? m_passStateScratch.find(item.parent) : m_passStateScratch.end();
            // A top-level child still waits for its parent, even though it will ignore the
            // parent's transform: its visibility is inherited, and inheriting from a parent that
            // has not been processed yet would read a stale answer.
            const bool parentReady = !hasParent || parentIt != m_passStateScratch.end();

            if (!parentReady) {
                m_pendingScratch[writeIndex] = item;
                ++writeIndex;
                continue;
            }

            if (item.world == nullptr) {
                // A pure Object. Visibility and the modes pass through it exactly as they would
                // through an object without those components; its interpolation answer is resolved
                // without a pose, because it has none to record.
                const WorldTransformPassState parentState =
                    hasParent ? parentIt->second : WorldTransformPassState{};
                WorldTransformPassState state{.valid = true,
                                                 .recomputed = false,
                                                 .interpolatedInHierarchy =
                                                     ResolveInterpolationMode(item, parentState.interpolatedInHierarchy),
                                                 .visibleInHierarchy =
                                                     ResolveVisibility(item, parentState.visibleInHierarchy),
                                                 .spatial = false};
                ResolveInheritedModes(item, parentState, state);
                m_passStateScratch.emplace(item.entity, state);
                madeProgress = true;
                continue;
            }

            // Top-level entities compose as if they had no parent, and so does a child of a pure
            // Object: there is no parent transform to compose from. Distinct from hasParent because
            // both still ARE children - visibility and the modes still inherit, and the pass state
            // is still keyed off the real parent - so only the transform chain is cut.
            const bool composesFromParent = hasParent && !item.local->topLevel && parentIt->second.spatial;

            WorldTransformComponent& world = *item.world;
            // `composesFromParent`, not `hasParent`: an entity that does not read its parent's
            // world transform cannot be invalidated by it. Propagating the failure anyway would
            // invent a dependency the entity deliberately does not have.
            if (composesFromParent && !parentIt->second.valid) {
                world.dirty = true;
                // Visibility is still resolved on this arm. An invalid world transform is a
                // separate failure from being hidden, and skipping the inheritance here would let
                // a non-finite transform anywhere in a subtree quietly un-hide everything beneath
                // a hidden ancestor.
                const bool inherited = ResolveVisibility(item, parentIt->second.visibleInHierarchy);
                // No pose recorded on this arm: the world transform is not valid, so storing it
                // would hand the renderer a NaN to blend towards and drag the object off over the
                // following frames. Keeping the last good pose freezes it, which is recoverable.
                const bool interpolated = ResolveInterpolation(entityManager, item, parentIt->second.interpolatedInHierarchy,
                                                                  world, /*poseChanged=*/false, /*poseValid=*/false);
                // Designated initializers, deliberately: this aggregate has four same-typed bools
                // and grew one in the middle, which silently rewired every positional call site
                // here. Naming the fields makes the next addition a compile error instead.
                WorldTransformPassState state{.valid = false,
                                                 .recomputed = false,
                                                 .interpolatedInHierarchy = interpolated,
                                                 .visibleInHierarchy = inherited};
                // Resolved on this arm too, for the same reason visibility is: whether an entity
                // runs, which thread it runs on and whether its text is translated are all
                // independent of whether its world transform came out finite.
                ResolveInheritedModes(item, parentIt->second, state);
                m_passStateScratch.emplace(item.entity, state);
                madeProgress = true;
                continue;
            }

            // Only a parent this entity actually composes from can force it to recompute.
            const bool parentWasRecomputed = composesFromParent && parentIt->second.recomputed;
            const bool shouldRecompute = world.dirty || parentWasRecomputed;
            bool publishedValid = IsWorldTransformComponentValid(world);
            if (shouldRecompute) {
                const TransformComponent& local = *item.local;
                WorldTransformComponent candidate =
                    !composesFromParent
                        ? MakeWorldTransformFromLocal(local, world.dirty)
                        : ComposeWorldTransform(
                              entityManager.GetComponent<WorldTransformComponent>(item.parent),
                              local, world.dirty);
                publishedValid = IsWorldTransformComponentValid(candidate);
                if (publishedValid) {
                    candidate.dirty = false;
                    world = candidate;
                } else {
                    world.dirty = true;
                }
            }

            // Visibility uses `hasParent`, not `composesFromParent`: top level cuts the transform
            // chain only. Hiding a parent must still hide a top-level child, or the flag would
            // quietly become "detach from everything" and there would be no way to get the
            // organisational half without the transform half.
            const bool parentVisible = !hasParent || parentIt->second.visibleInHierarchy;
            const bool inherited = ResolveVisibility(item, parentVisible);
            // Interpolation follows the transform chain, not the hierarchy: a top-level entity
            // simulates independently of its parent, so smoothing it against the parent's setting
            // would describe motion it does not have.
            const bool parentInterpolated = !composesFromParent || parentIt->second.interpolatedInHierarchy;
            const bool interpolated =
                ResolveInterpolation(entityManager, item, parentInterpolated, world,
                                        /*poseChanged=*/shouldRecompute && publishedValid, publishedValid);
            WorldTransformPassState state{.valid = publishedValid,
                                             .recomputed = shouldRecompute && publishedValid,
                                             .interpolatedInHierarchy = interpolated,
                                             .visibleInHierarchy = inherited};
            // These three follow the hierarchy, not the transform chain: a top-level entity is
            // still paused with its parent, still constrained to its parent's thread, and still
            // translated with the menu it belongs to. Only the transform chain is cut, which is
            // exactly what TransformComponent::topLevel documents.
            ResolveInheritedModes(item, hasParent ? parentIt->second : WorldTransformPassState{}, state);
            m_passStateScratch.emplace(item.entity, state);
            madeProgress = true;
        }

        m_pendingScratch.resize(writeIndex);
    }

    // Redirects last: they need every transform-inherited answer already computed, including on
    // entities the redirect points at from an unrelated branch.
    if (m_visibilityRedirectCount > 0U) {
        ResolveVisibilityParents(entityManager);
    }

    // A non-empty remainder here means a cycle slipped past SetParent()'s guard - a genuine
    // engine bug, not user error, worth catching in debug builds.
    UVE_ASSERT(m_pendingScratch.empty());
}

void SceneGraph::RebuildChildrenIndex(IEntityManager& entityManager) {
    m_childrenIndex.Rebuild(entityManager);
}

std::span<const Entity> SceneGraph::GetIndexedChildren(const Entity parent) const noexcept {
    return m_childrenIndex.GetChildren(parent);
}

std::vector<Entity> SceneGraph::GetChildren(IEntityManager& entityManager, Entity parent) {
    // Sorted, because the ECS visits entities in storage order, which moves whenever a component
    // is added or removed: without it, giving an object a script could reorder its siblings.
    std::vector<std::pair<std::int64_t, Entity>> ordered;
    entityManager.ForEach<HierarchyComponent>(
        [&ordered, parent](Entity entity, HierarchyComponent& hierarchy) {
            if (hierarchy.parent == parent) {
                ordered.emplace_back(hierarchy.siblingOrder, entity);
            }
        });
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
        return CompareSiblingHierarchyOrder(a.first, a.second, b.first, b.second);
    });
    std::vector<Entity> children;
    children.reserve(ordered.size());
    for (const auto& [order, entity] : ordered) {
        children.push_back(entity);
    }
    return children;
}

std::optional<std::size_t> SceneGraph::GetSiblingIndex(IEntityManager& entityManager, Entity entity) {
    if (!entityManager.IsAlive(entity) || !entityManager.HasComponent<HierarchyComponent>(entity)) {
        return std::nullopt;
    }
    const std::vector<Entity> siblings =
        GetChildren(entityManager, entityManager.GetComponent<HierarchyComponent>(entity).parent);
    const auto found = std::find(siblings.begin(), siblings.end(), entity);
    return found == siblings.end() ? std::nullopt
                                   : std::optional<std::size_t>{static_cast<std::size_t>(found - siblings.begin())};
}

bool SceneGraph::SetSiblingIndex(IEntityManager& entityManager, Entity entity, std::size_t index) {
    if (!entityManager.IsAlive(entity) || !entityManager.HasComponent<HierarchyComponent>(entity)) {
        return false;
    }
    std::vector<Entity> siblings =
        GetChildren(entityManager, entityManager.GetComponent<HierarchyComponent>(entity).parent);
    std::erase(siblings, entity);
    siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(std::min(index, siblings.size())), entity);
    // Fresh orders for the whole run keep them strictly increasing in the new sequence.
    for (const Entity sibling : siblings) {
        entityManager.GetComponent<HierarchyComponent>(sibling).siblingOrder = NextSiblingOrder();
    }
    return true;
}

} // namespace UVE::Scene
