// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "transform_component.h"
#include "physics_interpolation_component.h"
#include "auto_translate_component.h"
#include "object_metadata_component.h"
#include "process_component.h"
#include "thread_group_component.h"
#include "visibility_component.h"
#include "world_transform_component.h"
#include "children_index.h"
#include "i_scene_graph.h"

namespace UVE::Scene {

/// SceneGraph is the concrete, engine-standard implementation of ISceneGraph. Holds no
/// persistent SCENE STATE of its own — every method takes the IEntityManager it operates on
/// explicitly, so it is trivially constructible/testable and has no ordering dependency on any
/// specific manager instance's construction.
///
/// It does own two per-update SCRATCH buffers. They are not state: Update() clears both on
/// entry, so two calls with the same scene produce the same result and a fresh instance behaves
/// identically to a reused one. They exist only so a per-frame update does not reallocate
/// scene-sized containers every frame — Update() runs once per frame over every transform
/// entity, which made it the single most expensive thing a static scene did.
/// Thread-safety: not thread-safe — every method must be called only from the main/scene
/// thread, matching IEntityManager's own contract. The scratch buffers make that contract
/// load-bearing rather than merely documented, which is why it is restated here.
class SceneGraph final : public ISceneGraph {
public:
    SceneGraph() = default;

    void AttachTransform(IEntityManager& entityManager, Entity entity,
                             const TransformComponent& localTransform) override;
    void SetLocalTransform(IEntityManager& entityManager, Entity entity,
                               const TransformComponent& localTransform) override;
    void SetParent(IEntityManager& entityManager, Entity child, Entity newParent) override;
    void Update(IEntityManager& entityManager) override;
    [[nodiscard]] std::vector<Entity> GetChildren(IEntityManager& entityManager,
                                                         Entity parent) override;
    [[nodiscard]] std::optional<std::size_t> GetSiblingIndex(IEntityManager& entityManager,
                                                                 Entity entity) override;
    [[nodiscard]] bool SetSiblingIndex(IEntityManager& entityManager, Entity entity,
                                          std::size_t index) override;
    /// Rebuilds the graph's children index from the manager's current hierarchy links. Nothing
    /// calls this implicitly — not even Update — because the rebuild is an O(n) pass and the
    /// index exists to make that cost explicit and payable once. Call it when the hierarchy may
    /// have changed since the last rebuild, then serve every child-list query from
    /// GetIndexedChildren until it changes again.
    void RebuildChildrenIndex(IEntityManager& entityManager);

    /// The children of `parent` from the graph's index, in the same sibling order
    /// GetChildren returns. Answers from the last RebuildChildrenIndex — an empty span
    /// means "no children recorded", which for a never-rebuilt index means "never rebuilt".
    /// Prefer GetChildren when the hierarchy may have changed since the last rebuild; prefer
    /// this when it has not and many parents' children are needed.
    [[nodiscard]] std::span<const Entity> GetIndexedChildren(Entity parent) const noexcept;
    /// Answered from the pass state Update() leaves behind, which is retained until the next
    /// update clears it - so this costs one hash lookup and adds no storage or pass of its own.
    [[nodiscard]] std::optional<ResolvedObjectModes> TryGetResolvedObjectModes(
        Entity entity) const override;

private:
    /// Whether an entity's derived world transform came out valid this pass, and whether it was
    /// actually RECOMPUTED as opposed to merely visited. Children need the second flag: a parent
    /// that recomputed forces its children to recompute regardless of their own dirty flags.
    struct WorldTransformPassState final {
        bool valid = false;
        bool recomputed = false;
        /// This entity's resolved interpolation setting, for its children to inherit. Carried for
        /// the same reason the visibility answer is: a child needs the ANSWER, and an entity with
        /// no component still has one.
        bool interpolatedInHierarchy = true;

        /// This entity's resolved visibility, for its children to inherit. Carried in the pass
        /// state rather than read back off the component because the child needs the ANSWER, and
        /// an entity with no VisibilityComponent still has one - it is visible, and its
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
        /// False for a pure Object, which is in the hierarchy but has no transform. A child never
        /// composes its world transform from a non-spatial parent; it starts a new transform chain
        /// there, the way a Object3D under a plain Object does.
        bool spatial = true;

        TickMode processModeInHierarchy = TickMode::Inherit;
        ThreadGroupMode threadGroupModeInHierarchy = ThreadGroupMode::Inherit;
        LocalizeMode autoTranslateModeInHierarchy = LocalizeMode::Inherit;
    };

    /// One entity awaiting processing, with its components resolved once by the initial walk
    /// instead of re-looked-up on every sweep. The pointers are valid for the duration of a single
    /// Update() only: nothing in that function creates, destroys, or re-archetypes an entity,
    /// which are the only operations that could move a component row.
    struct PendingEntity final {
        Entity entity;
        Entity parent;
        TransformComponent* local = nullptr;
        WorldTransformComponent* world = nullptr;
        /// Null when the entity has no VisibilityComponent, which is the common case and means
        /// "visible". Resolved during the gather walk so the sweep never has to ask the ECS
        /// whether a component exists - the sweep runs once per entity per pass and that lookup
        /// would be pure overhead for the many entities that will never carry one.
        VisibilityComponent* visibility = nullptr;

        /// Null when the entity has no PhysicsInterpolationComponent, which is the common case
        /// and means "not interpolated". Resolved during the gather walk for the same reason the
        /// visibility pointer is: the sweep can revisit an entity across several passes while it
        /// waits for its parent, and an ECS existence check per pass would be paid for nothing.
        PhysicsInterpolationComponent* interpolation = nullptr;

        /// Null when the entity carries no such component, which is the common case and means
        /// "inherit". Resolved during the gather walk for the same reason the two pointers above
        /// are: an existence check per sweep pass would be paid by every entity that will never
        /// carry one.
        ProcessComponent* process = nullptr;
        ThreadGroupComponent* threadGroup = nullptr;
        AutoTranslateComponent* autoTranslate = nullptr;
    };

    /// Per-update scratch, cleared on entry to Update(). Retained between calls purely to keep
    /// their capacity; see the class doc comment.
    /// Resolves one entity's inherited visibility and publishes it to its component.
    ///
    /// Returns the resolved value even when the entity has no VisibilityComponent - in that
    /// case the entity is visible, and its children inherit the parent's state unchanged rather
    /// than being treated as a break in the chain. That is what makes the component optional
    /// without putting a hole in the middle of a hierarchy.
    [[nodiscard]] static bool ResolveVisibility(const PendingEntity& item, bool parentVisible) noexcept;

    /// Resolves one entity's inherited interpolation setting, records this step's world pose into
    /// the component, and publishes the resolved value.
    ///
    /// Recording happens here rather than in the physics system because this is the one place that
    /// already knows a world transform has just been finalised - the physics system writes LOCAL
    /// transforms and would have to wait for this sweep anyway.
    /// The Inherit/On/Off rule alone, published to the component when there is one. Shared by the
    /// spatial path (which also records poses) and the pure-Object path (which has no pose).
    static bool ResolveInterpolationMode(const PendingEntity& item, bool parentInterpolated) noexcept;

    static bool ResolveInterpolation(IEntityManager& entityManager, const PendingEntity& item,
                                        bool parentInterpolated, const WorldTransformComponent& world,
                                        bool poseChanged, bool poseValid);

    /// Resolves the three inheriting modes for one entity and publishes each to its component.
    ///
    /// Grouped into one function rather than three because they share a shape exactly - an
    /// optional component, Inherit passing the parent's answer through, the resolved answer
    /// written to a derived field nothing else may write - and because the sweep already has the
    /// parent's state in hand at each of its two exit arms. Three separate calls at both arms
    /// would be six places to forget one.
    static void ResolveInheritedModes(const PendingEntity& item,
                                         const WorldTransformPassState& parentState,
                                         WorldTransformPassState& outState) noexcept;

    /// Second visibility pass, for entities that inherit from a VisibilityComponent's
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
    void ResolveVisibilityParents(IEntityManager& entityManager);

    /// How many entities used a visibility redirect during the last gather walk.
    ///
    /// Counted there because that walk already touches every entity's components, so the count is
    /// free - and it is what lets the second pass be skipped entirely by the overwhelming majority
    /// of scenes, which use no redirects at all. It also bounds the chain-following guard: no
    /// legitimate chain can be longer than the number of entities that have a redirect.
    std::size_t m_visibilityRedirectCount = 0U;

    std::vector<PendingEntity> m_pendingScratch;
    std::unordered_map<Entity, WorldTransformPassState> m_passStateScratch;

    /// Frame-held children lookup for bulk queries; rebuilt only by RebuildChildrenIndex.
    ChildrenIndex m_childrenIndex;
};

} // namespace UVE::Scene
