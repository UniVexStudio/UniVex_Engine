// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <optional>
#include <unordered_map>
#include <vector>

#include "uve/component/transform_component_uve.h"
#include "uve/component/physics_interpolation_component_uve.h"
#include "uve/component/auto_translate_component_uve.h"
#include "uve/component/node_metadata_component_uve.h"
#include "uve/component/process_component_uve.h"
#include "uve/component/thread_group_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/scene/i_scene_graph_uve.h"

namespace UVE::Scene {

/// SceneGraphUVE is the concrete, engine-standard implementation of ISceneGraphUVE. Holds no
/// persistent SCENE STATE of its own — every method takes the IEntityManagerUVE it operates on
/// explicitly, so it is trivially constructible/testable and has no ordering dependency on any
/// specific manager instance's construction.
///
/// It does own two per-update SCRATCH buffers. They are not state: UpdateUVE() clears both on
/// entry, so two calls with the same scene produce the same result and a fresh instance behaves
/// identically to a reused one. They exist only so a per-frame update does not reallocate
/// scene-sized containers every frame — UpdateUVE() runs once per frame over every transform
/// entity, which made it the single most expensive thing a static scene did.
/// Thread-safety: not thread-safe — every method must be called only from the main/scene
/// thread, matching IEntityManagerUVE's own contract. The scratch buffers make that contract
/// load-bearing rather than merely documented, which is why it is restated here.
class SceneGraphUVE final : public ISceneGraphUVE {
public:
    SceneGraphUVE() = default;

    void AttachTransformUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                             const TransformComponentUVE& localTransform) override;
    void SetLocalTransformUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                               const TransformComponentUVE& localTransform) override;
    void SetParentUVE(IEntityManagerUVE& entityManager, EntityUVE child, EntityUVE newParent) override;
    void UpdateUVE(IEntityManagerUVE& entityManager) override;
    [[nodiscard]] std::vector<EntityUVE> GetChildrenUVE(IEntityManagerUVE& entityManager,
                                                         EntityUVE parent) override;
    /// Answered from the pass state UpdateUVE() leaves behind, which is retained until the next
    /// update clears it - so this costs one hash lookup and adds no storage or pass of its own.
    [[nodiscard]] std::optional<ResolvedNodeModesUVE> TryGetResolvedNodeModesUVE(
        EntityUVE entity) const override;

private:
    /// Whether an entity's derived world transform came out valid this pass, and whether it was
    /// actually RECOMPUTED as opposed to merely visited. Children need the second flag: a parent
    /// that recomputed forces its children to recompute regardless of their own dirty flags.
    struct WorldTransformPassStateUVE final {
        bool valid = false;
        bool recomputed = false;
        /// This entity's resolved interpolation setting, for its children to inherit. Carried for
        /// the same reason the visibility answer is: a child needs the ANSWER, and an entity with
        /// no component still has one.
        bool interpolatedInHierarchy = true;

        /// This entity's resolved visibility, for its children to inherit. Carried in the pass
        /// state rather than read back off the component because the child needs the ANSWER, and
        /// an entity with no VisibilityComponentUVE still has one - it is visible, and its
        /// children must inherit that rather than finding no component and guessing.
        bool visibleInHierarchy = true;

        /// The three modes that inherit the same way visibility does, carried for the same reason:
        /// an entity with no component still has an answer, and its children inherit that answer
        /// rather than finding no component and guessing.
        /// Defaulted to Inherit, not to each mode's hierarchy default: a default-constructed pass
        /// state is what a ROOT is resolved against, and a root has no ancestor constraining it.
        /// Seeding the defaults here instead would make every root look like a child of something
        /// that had already chosen - which a thread group treats as a constraint, so a root could
        /// then never opt into a worker at all.
        ProcessModeUVE processModeInHierarchy = ProcessModeUVE::Inherit;
        ThreadGroupModeUVE threadGroupModeInHierarchy = ThreadGroupModeUVE::Inherit;
        AutoTranslateModeUVE autoTranslateModeInHierarchy = AutoTranslateModeUVE::Inherit;
    };

    /// One entity awaiting processing, with its components resolved once by the initial walk
    /// instead of re-looked-up on every sweep. The pointers are valid for the duration of a single
    /// UpdateUVE() only: nothing in that function creates, destroys, or re-archetypes an entity,
    /// which are the only operations that could move a component row.
    struct PendingEntityUVE final {
        EntityUVE entity;
        EntityUVE parent;
        TransformComponentUVE* local = nullptr;
        WorldTransformComponentUVE* world = nullptr;
        /// Null when the entity has no VisibilityComponentUVE, which is the common case and means
        /// "visible". Resolved during the gather walk so the sweep never has to ask the ECS
        /// whether a component exists - the sweep runs once per entity per pass and that lookup
        /// would be pure overhead for the many entities that will never carry one.
        VisibilityComponentUVE* visibility = nullptr;

        /// Null when the entity has no PhysicsInterpolationComponentUVE, which is the common case
        /// and means "not interpolated". Resolved during the gather walk for the same reason the
        /// visibility pointer is: the sweep can revisit an entity across several passes while it
        /// waits for its parent, and an ECS existence check per pass would be paid for nothing.
        PhysicsInterpolationComponentUVE* interpolation = nullptr;

        /// Null when the entity carries no such component, which is the common case and means
        /// "inherit". Resolved during the gather walk for the same reason the two pointers above
        /// are: an existence check per sweep pass would be paid by every entity that will never
        /// carry one.
        ProcessComponentUVE* process = nullptr;
        ThreadGroupComponentUVE* threadGroup = nullptr;
        AutoTranslateComponentUVE* autoTranslate = nullptr;
    };

    /// Per-update scratch, cleared on entry to UpdateUVE(). Retained between calls purely to keep
    /// their capacity; see the class doc comment.
    /// Resolves one entity's inherited visibility and publishes it to its component.
    ///
    /// Returns the resolved value even when the entity has no VisibilityComponentUVE - in that
    /// case the entity is visible, and its children inherit the parent's state unchanged rather
    /// than being treated as a break in the chain. That is what makes the component optional
    /// without putting a hole in the middle of a hierarchy.
    [[nodiscard]] static bool ResolveVisibilityUVE(const PendingEntityUVE& item, bool parentVisible) noexcept;

    /// Resolves one entity's inherited interpolation setting, records this step's world pose into
    /// the component, and publishes the resolved value.
    ///
    /// Recording happens here rather than in the physics system because this is the one place that
    /// already knows a world transform has just been finalised - the physics system writes LOCAL
    /// transforms and would have to wait for this sweep anyway.
    static bool ResolveInterpolationUVE(const PendingEntityUVE& item, bool parentInterpolated,
                                        const WorldTransformComponentUVE& world, bool poseChanged) noexcept;

    /// Resolves the three inheriting modes for one entity and publishes each to its component.
    ///
    /// Grouped into one function rather than three because they share a shape exactly - an
    /// optional component, Inherit passing the parent's answer through, the resolved answer
    /// written to a derived field nothing else may write - and because the sweep already has the
    /// parent's state in hand at each of its two exit arms. Three separate calls at both arms
    /// would be six places to forget one.
    static void ResolveInheritedModesUVE(const PendingEntityUVE& item,
                                         const WorldTransformPassStateUVE& parentState,
                                         WorldTransformPassStateUVE& outState) noexcept;

    /// Second visibility pass, for entities that inherit from a VisibilityComponentUVE's
    /// `visibilityParent` instead of from their transform parent.
    ///
    /// Separate from the transform sweep because it cannot share its ordering. That sweep is
    /// root-first over the transform hierarchy, which guarantees a transform parent is resolved
    /// before its children - but a visibility parent is an arbitrary entity that may sit anywhere,
    /// including in a branch processed much later. Resolving it inline would read a stale answer
    /// roughly half the time, depending on nothing more meaningful than entity creation order.
    ///
    /// Runs only when at least one entity actually uses the redirect, so the common scene pays a
    /// single bool check for a feature it does not use.
    void ResolveVisibilityParentsUVE(IEntityManagerUVE& entityManager);

    /// How many entities used a visibility redirect during the last gather walk.
    ///
    /// Counted there because that walk already touches every entity's components, so the count is
    /// free - and it is what lets the second pass be skipped entirely by the overwhelming majority
    /// of scenes, which use no redirects at all. It also bounds the chain-following guard: no
    /// legitimate chain can be longer than the number of entities that have a redirect.
    std::size_t m_visibilityRedirectCount = 0U;

    std::vector<PendingEntityUVE> m_pendingScratch;
    std::unordered_map<EntityUVE, WorldTransformPassStateUVE> m_passStateScratch;
};

} // namespace UVE::Scene
