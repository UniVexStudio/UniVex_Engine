// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/scene/scene_graph_uve.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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
                              !entityManager.HasComponentUVE<HierarchyComponentUVE>(child);
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

    HierarchyComponentUVE& hierarchy = entityManager.GetComponentUVE<HierarchyComponentUVE>(child);
    if (hierarchy.parent != newParent) {
        hierarchy.parent = newParent;
        hierarchy.siblingOrder = NextSiblingOrderUVE(); // after the siblings already there
    }
    // A pure Node (no transform of its own) moves in the hierarchy only; nothing composes from it.
    if (entityManager.HasComponentUVE<WorldTransformComponentUVE>(child)) {
        entityManager.GetComponentUVE<WorldTransformComponentUVE>(child).dirty = true;
    }
}

void SceneGraphUVE::ResolveVisibilityParentsUVE(IEntityManagerUVE& entityManager) {
    // Walk each redirect chain to its end, then apply every `visible` switch along the way.
    //
    // Chains are followed rather than resolved in one hop because a visibility parent may itself
    // redirect. Depth is bounded by the number of entities carrying the component, so the guard
    // below is what makes a cycle terminate rather than a promise that one cannot exist.
    // Copied out before the walk: the lambda must not reach back into the object, and the bound is
    // fixed for this pass anyway.
    const std::size_t chainLimit = m_visibilityRedirectCount + 1U;
    entityManager.ForEachUVE<VisibilityComponentUVE>(
        [&entityManager, chainLimit](const EntityUVE, VisibilityComponentUVE& visibility) {
            if (visibility.visibilityParent == kInvalidEntityUVE) {
                return; // Transform-parent inheritance; already resolved by the main sweep.
            }

            // Start from the entity's own switch and its transform-inherited state. The redirect
            // REPLACES the inherited half, not the entity's own choice: an object hidden in its
            // own right stays hidden no matter what it points at.
            bool resolved = visibility.visible;

            EntityUVE current = visibility.visibilityParent;
            std::size_t guard = 0U;
            const std::size_t limit = chainLimit;
            while (current != kInvalidEntityUVE && guard <= limit) {
                ++guard;
                if (!entityManager.IsAliveUVE(current)) {
                    // A dangling target - the node was deleted, or the reference came from a file
                    // that no longer matches the scene. Treated as "no redirect" rather than as
                    // hidden: losing a reference should not make geometry silently disappear.
                    return;
                }
                if (!entityManager.HasComponentUVE<VisibilityComponentUVE>(current)) {
                    // A target with no component is visible, and has nothing further to redirect
                    // to, so the chain ends here with the state gathered so far.
                    break;
                }
                const VisibilityComponentUVE& target =
                    entityManager.GetComponentUVE<VisibilityComponentUVE>(current);
                if (!target.visible) {
                    resolved = false;
                }
                if (target.visibilityParent == kInvalidEntityUVE) {
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

bool SceneGraphUVE::ResolveInterpolationModeUVE(const PendingEntityUVE& item, const bool parentInterpolated) noexcept {
    if (item.interpolation == nullptr) {
        return parentInterpolated;
    }
    bool resolved = parentInterpolated;
    if (item.interpolation->mode == PhysicsInterpolationModeUVE::On) {
        resolved = true;
    } else if (item.interpolation->mode == PhysicsInterpolationModeUVE::Off) {
        resolved = false;
    }
    item.interpolation->interpolatedInHierarchy = resolved;
    return resolved;
}

bool SceneGraphUVE::ResolveInterpolationUVE(const PendingEntityUVE& item, const bool parentInterpolated,
                                            const WorldTransformComponentUVE& world,
                                            const bool poseChanged) noexcept {
    // No component means the entity is drawn at its simulated pose, and means the parent's answer
    // passes straight through - an intermediate node that never opted in must not break a rig's
    // inheritance chain, the same rule visibility follows.
    if (item.interpolation == nullptr) {
        return parentInterpolated;
    }

    PhysicsInterpolationComponentUVE& interpolation = *item.interpolation;
    const bool resolved = ResolveInterpolationModeUVE(item, parentInterpolated);

    // Poses are recorded only when the transform actually changed. Recording on every sweep would
    // collapse previous onto current for a stationary object, and the first frame after it began
    // moving would then have nothing to blend from - a visible hitch at exactly the moment motion
    // starts, which is the worst place for one.
    if (poseChanged) {
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

std::optional<ResolvedNodeModesUVE> SceneGraphUVE::TryGetResolvedNodeModesUVE(
    const EntityUVE entity) const {
    const auto iterator = m_passStateScratch.find(entity);
    if (iterator == m_passStateScratch.end()) {
        return std::nullopt;
    }
    // Every stored state went through ResolveInheritedModesUVE, which never leaves Inherit behind
    // (Inherit at the top of a hierarchy resolves to the default), so these are final answers.
    return ResolvedNodeModesUVE{iterator->second.processModeInHierarchy,
                                iterator->second.threadGroupModeInHierarchy,
                                iterator->second.autoTranslateModeInHierarchy};
}

void SceneGraphUVE::ResolveInheritedModesUVE(const PendingEntityUVE& item,
                                             const WorldTransformPassStateUVE& parentState,
                                             WorldTransformPassStateUVE& outState) noexcept {
    // Each of the three has the same shape: no component means the parent's answer passes straight
    // through, so an intermediate node that never opted in does not break a subtree's chain.
    outState.processModeInHierarchy =
        item.process == nullptr
            ? ResolveProcessModeUVE(ProcessModeUVE::Inherit, parentState.processModeInHierarchy)
            : ResolveProcessModeUVE(item.process->mode, parentState.processModeInHierarchy);
    if (item.process != nullptr) {
        item.process->resolvedModeInHierarchy = outState.processModeInHierarchy;
    }

    outState.threadGroupModeInHierarchy =
        item.threadGroup == nullptr
            ? ResolveThreadGroupModeUVE(ThreadGroupModeUVE::Inherit, parentState.threadGroupModeInHierarchy)
            : ResolveThreadGroupModeUVE(item.threadGroup->mode, parentState.threadGroupModeInHierarchy);
    if (item.threadGroup != nullptr) {
        item.threadGroup->resolvedModeInHierarchy = outState.threadGroupModeInHierarchy;
    }

    outState.autoTranslateModeInHierarchy =
        item.autoTranslate == nullptr
            ? ResolveAutoTranslateModeUVE(AutoTranslateModeUVE::Inherit,
                                          parentState.autoTranslateModeInHierarchy)
            : ResolveAutoTranslateModeUVE(item.autoTranslate->mode,
                                          parentState.autoTranslateModeInHierarchy);
    if (item.autoTranslate != nullptr) {
        item.autoTranslate->resolvedModeInHierarchy = outState.autoTranslateModeInHierarchy;
    }
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
    // Recounted every update: entities and their redirects change between frames.
    m_visibilityRedirectCount = 0U;
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
            if (visibility != nullptr && visibility->visibilityParent != kInvalidEntityUVE) {
                ++m_visibilityRedirectCount;
            }
            PhysicsInterpolationComponentUVE* const interpolation =
                entityManager.HasComponentUVE<PhysicsInterpolationComponentUVE>(entity)
                    ? &entityManager.GetComponentUVE<PhysicsInterpolationComponentUVE>(entity)
                    : nullptr;
            ProcessComponentUVE* const process =
                entityManager.HasComponentUVE<ProcessComponentUVE>(entity)
                    ? &entityManager.GetComponentUVE<ProcessComponentUVE>(entity)
                    : nullptr;
            ThreadGroupComponentUVE* const threadGroup =
                entityManager.HasComponentUVE<ThreadGroupComponentUVE>(entity)
                    ? &entityManager.GetComponentUVE<ThreadGroupComponentUVE>(entity)
                    : nullptr;
            AutoTranslateComponentUVE* const autoTranslate =
                entityManager.HasComponentUVE<AutoTranslateComponentUVE>(entity)
                    ? &entityManager.GetComponentUVE<AutoTranslateComponentUVE>(entity)
                    : nullptr;
            m_pendingScratch.push_back(PendingEntityUVE{entity, hierarchy.parent, &local, &world, visibility,
                                                        interpolation, process, threadGroup, autoTranslate});
        });

    // Pure Nodes: in the hierarchy, with no transform of their own - the scene root is the first.
    // They take part in the sweep because their children wait on them and because the inherited
    // modes (Process, Thread Group, Auto Translate, and visibility passing through) must flow down
    // through them; leaving them out would strand every child waiting on a parent answer that never
    // arrives. They carry no transform, so nothing below composes from them: see `spatial`.
    entityManager.ForEachUVE<HierarchyComponentUVE>(
        [this, &entityManager](EntityUVE entity, HierarchyComponentUVE& hierarchy) {
            if (entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity) &&
                entityManager.HasComponentUVE<TransformComponentUVE>(entity)) {
                return; // A spatial node, already gathered above.
            }
            const auto optional = [&entityManager, entity]<typename ComponentT>() -> ComponentT* {
                return entityManager.HasComponentUVE<ComponentT>(entity) ? &entityManager.GetComponentUVE<ComponentT>(entity)
                                                                         : nullptr;
            };
            VisibilityComponentUVE* const visibility = optional.template operator()<VisibilityComponentUVE>();
            if (visibility != nullptr && visibility->visibilityParent != kInvalidEntityUVE) {
                ++m_visibilityRedirectCount;
            }
            m_pendingScratch.push_back(PendingEntityUVE{
                entity, hierarchy.parent, nullptr, nullptr, visibility,
                optional.template operator()<PhysicsInterpolationComponentUVE>(),
                optional.template operator()<ProcessComponentUVE>(),
                optional.template operator()<ThreadGroupComponentUVE>(),
                optional.template operator()<AutoTranslateComponentUVE>()});
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
            const bool hasParent = (item.parent != kInvalidEntityUVE);
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
                // A pure Node. Visibility and the modes pass through it exactly as they would
                // through a node without those components; its interpolation answer is resolved
                // without a pose, because it has none to record.
                const WorldTransformPassStateUVE parentState =
                    hasParent ? parentIt->second : WorldTransformPassStateUVE{};
                WorldTransformPassStateUVE state{.valid = true,
                                                 .recomputed = false,
                                                 .interpolatedInHierarchy =
                                                     ResolveInterpolationModeUVE(item, parentState.interpolatedInHierarchy),
                                                 .visibleInHierarchy =
                                                     ResolveVisibilityUVE(item, parentState.visibleInHierarchy),
                                                 .spatial = false};
                ResolveInheritedModesUVE(item, parentState, state);
                m_passStateScratch.emplace(item.entity, state);
                madeProgress = true;
                continue;
            }

            // Top-level entities compose as if they had no parent, and so does a child of a pure
            // Node: there is no parent transform to compose from. Distinct from hasParent because
            // both still ARE children - visibility and the modes still inherit, and the pass state
            // is still keyed off the real parent - so only the transform chain is cut.
            const bool composesFromParent = hasParent && !item.local->topLevel && parentIt->second.spatial;

            WorldTransformComponentUVE& world = *item.world;
            // `composesFromParent`, not `hasParent`: an entity that does not read its parent's
            // world transform cannot be invalidated by it. Propagating the failure anyway would
            // invent a dependency the entity deliberately does not have.
            if (composesFromParent && !parentIt->second.valid) {
                world.dirty = true;
                // Visibility is still resolved on this arm. An invalid world transform is a
                // separate failure from being hidden, and skipping the inheritance here would let
                // a non-finite transform anywhere in a subtree quietly un-hide everything beneath
                // a hidden ancestor.
                const bool inherited = ResolveVisibilityUVE(item, parentIt->second.visibleInHierarchy);
                // No pose recorded on this arm: the world transform is not valid, so storing it
                // would hand the renderer a NaN to blend towards and drag the object off over the
                // following frames. Keeping the last good pose freezes it, which is recoverable.
                const bool interpolated = ResolveInterpolationUVE(item, parentIt->second.interpolatedInHierarchy,
                                                                  world, /*poseChanged=*/false);
                // Designated initializers, deliberately: this aggregate has four same-typed bools
                // and grew one in the middle, which silently rewired every positional call site
                // here. Naming the fields makes the next addition a compile error instead.
                WorldTransformPassStateUVE state{.valid = false,
                                                 .recomputed = false,
                                                 .interpolatedInHierarchy = interpolated,
                                                 .visibleInHierarchy = inherited};
                // Resolved on this arm too, for the same reason visibility is: whether an entity
                // runs, which thread it runs on and whether its text is translated are all
                // independent of whether its world transform came out finite.
                ResolveInheritedModesUVE(item, parentIt->second, state);
                m_passStateScratch.emplace(item.entity, state);
                madeProgress = true;
                continue;
            }

            // Only a parent this entity actually composes from can force it to recompute.
            const bool parentWasRecomputed = composesFromParent && parentIt->second.recomputed;
            const bool shouldRecompute = world.dirty || parentWasRecomputed;
            bool publishedValid = IsFiniteWorldTransformUVE(world);
            if (shouldRecompute) {
                const TransformComponentUVE& local = *item.local;
                WorldTransformComponentUVE candidate = world;
                if (!composesFromParent) {
                    // Local values ARE world values - the same arithmetic a real root gets.
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

            // Visibility uses `hasParent`, not `composesFromParent`: top level cuts the transform
            // chain only. Hiding a parent must still hide a top-level child, or the flag would
            // quietly become "detach from everything" and there would be no way to get the
            // organisational half without the transform half.
            const bool parentVisible = !hasParent || parentIt->second.visibleInHierarchy;
            const bool inherited = ResolveVisibilityUVE(item, parentVisible);
            // Interpolation follows the transform chain, not the hierarchy: a top-level entity
            // simulates independently of its parent, so smoothing it against the parent's setting
            // would describe motion it does not have.
            const bool parentInterpolated = !composesFromParent || parentIt->second.interpolatedInHierarchy;
            const bool interpolated =
                ResolveInterpolationUVE(item, parentInterpolated, world,
                                        /*poseChanged=*/shouldRecompute && publishedValid);
            WorldTransformPassStateUVE state{.valid = publishedValid,
                                             .recomputed = shouldRecompute && publishedValid,
                                             .interpolatedInHierarchy = interpolated,
                                             .visibleInHierarchy = inherited};
            // These three follow the hierarchy, not the transform chain: a top-level entity is
            // still paused with its parent, still constrained to its parent's thread, and still
            // translated with the menu it belongs to. Only the transform chain is cut, which is
            // exactly what TransformComponentUVE::topLevel documents.
            ResolveInheritedModesUVE(item, hasParent ? parentIt->second : WorldTransformPassStateUVE{}, state);
            m_passStateScratch.emplace(item.entity, state);
            madeProgress = true;
        }

        m_pendingScratch.resize(writeIndex);
    }

    // Redirects last: they need every transform-inherited answer already computed, including on
    // entities the redirect points at from an unrelated branch.
    if (m_visibilityRedirectCount > 0U) {
        ResolveVisibilityParentsUVE(entityManager);
    }

    // A non-empty remainder here means a cycle slipped past SetParentUVE()'s guard - a genuine
    // engine bug, not user error, worth catching in debug builds.
    UVE_ASSERT(m_pendingScratch.empty());
}

std::vector<EntityUVE> SceneGraphUVE::GetChildrenUVE(IEntityManagerUVE& entityManager, EntityUVE parent) {
    // Sorted, because the ECS visits entities in storage order, which moves whenever a component
    // is added or removed: without it, giving a node a script could reorder its siblings.
    std::vector<std::pair<std::int64_t, EntityUVE>> ordered;
    entityManager.ForEachUVE<HierarchyComponentUVE>(
        [&ordered, parent](EntityUVE entity, HierarchyComponentUVE& hierarchy) {
            if (hierarchy.parent == parent) {
                ordered.emplace_back(hierarchy.siblingOrder, entity);
            }
        });
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) {
            return a.first < b.first;
        }
        return a.second.index != b.second.index ? a.second.index < b.second.index
                                                : a.second.generation < b.second.generation;
    });
    std::vector<EntityUVE> children;
    children.reserve(ordered.size());
    for (const auto& [order, entity] : ordered) {
        children.push_back(entity);
    }
    return children;
}

std::optional<std::size_t> SceneGraphUVE::GetSiblingIndexUVE(IEntityManagerUVE& entityManager, EntityUVE entity) {
    if (!entityManager.IsAliveUVE(entity) || !entityManager.HasComponentUVE<HierarchyComponentUVE>(entity)) {
        return std::nullopt;
    }
    const std::vector<EntityUVE> siblings =
        GetChildrenUVE(entityManager, entityManager.GetComponentUVE<HierarchyComponentUVE>(entity).parent);
    const auto found = std::find(siblings.begin(), siblings.end(), entity);
    return found == siblings.end() ? std::nullopt
                                   : std::optional<std::size_t>{static_cast<std::size_t>(found - siblings.begin())};
}

bool SceneGraphUVE::SetSiblingIndexUVE(IEntityManagerUVE& entityManager, EntityUVE entity, std::size_t index) {
    if (!entityManager.IsAliveUVE(entity) || !entityManager.HasComponentUVE<HierarchyComponentUVE>(entity)) {
        return false;
    }
    std::vector<EntityUVE> siblings =
        GetChildrenUVE(entityManager, entityManager.GetComponentUVE<HierarchyComponentUVE>(entity).parent);
    std::erase(siblings, entity);
    siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(std::min(index, siblings.size())), entity);
    // Fresh orders for the whole run keep them strictly increasing in the new sequence.
    for (const EntityUVE sibling : siblings) {
        entityManager.GetComponentUVE<HierarchyComponentUVE>(sibling).siblingOrder = NextSiblingOrderUVE();
    }
    return true;
}

} // namespace UVE::Scene
