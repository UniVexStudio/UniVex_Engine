// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <optional>
#include <vector>

#include "uve/component/auto_translate_component_uve.h"
#include "uve/component/process_component_uve.h"
#include "uve/component/thread_group_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

/// The three inherited node modes as resolved for one entity by the most recent UpdateUVE().
/// Always fully resolved - never Inherit - so a consumer uses the value as-is.
struct ResolvedNodeModesUVE final {
    ProcessModeUVE process = ProcessModeUVE::Pausable;
    ThreadGroupModeUVE threadGroup = ThreadGroupModeUVE::MainThread;
    AutoTranslateModeUVE autoTranslate = AutoTranslateModeUVE::Always;

    [[nodiscard]] bool operator==(const ResolvedNodeModesUVE&) const = default;
};

/// ISceneGraphUVE is the hierarchical-transform-tree interface: parent/child relationships and
/// dirty-flag-propagated world-space transforms, built on top of IEntityManagerUVE's
/// TransformComponentUVE/HierarchyComponentUVE/WorldTransformComponentUVE rather than a
/// separate tree structure (see docs — "Node3D is a thin handle over ECS data"). Stateless:
/// every method takes the IEntityManagerUVE it operates on explicitly, so a conforming
/// implementation has no ordering dependency on any specific manager instance's construction.
/// Thread-safety: not thread-safe — every method must be called only from the main/scene
/// thread, matching IEntityManagerUVE's own contract.
class ISceneGraphUVE {
public:
    virtual ~ISceneGraphUVE() = default;

    /// Makes `entity` a scene-graph node: adds TransformComponentUVE (set to `localTransform`),
    /// WorldTransformComponentUVE (dirty), and HierarchyComponentUVE (parent = invalid) in one
    /// call, so callers never have to remember all three individually. Debug builds assert that
    /// `entity` is alive, has no scene-graph components, and receives a valid transform; release
    /// builds return without mutation when any precondition is invalid.
    virtual void AttachTransformUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                     const TransformComponentUVE& localTransform) = 0;

    /// Overwrites `entity`'s authored local transform and marks its WorldTransformComponentUVE
    /// dirty. Debug builds assert that the entity and transform are valid; release builds return
    /// without mutation when the preconditions fail.
    virtual void SetLocalTransformUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                       const TransformComponentUVE& localTransform) = 0;

    /// Reparents `child` under `newParent` (or makes it a root if `newParent ==
    /// kInvalidEntityUVE`), marking `child` dirty. Debug builds assert valid entities/components and
    /// a cycle-free parent chain; release builds return without mutation for invalid input rather
    /// than silently corrupting the hierarchy.
    virtual void SetParentUVE(IEntityManagerUVE& entityManager, EntityUVE child, EntityUVE newParent) = 0;

    /// The dirty-flag-propagation pass: recomputes every scene-graph entity's
    /// WorldTransformComponentUVE from its parent chain, root-first. A processed parent's
    /// recomputation unconditionally forces every direct child to recompute too, even if the
    /// child's own dirty flag was false — the correct semantics for propagation, not just
    /// per-entity dirty tracking. Called once per frame by EngineCoreUVE::Update().
    virtual void UpdateUVE(IEntityManagerUVE& entityManager) = 0;

    /// Returns every entity whose HierarchyComponentUVE::parent equals `parent`. O(n) over
    /// every scene-graph entity — acceptable per the coding standards' "profile before
    /// optimizing" rule; a cached parent→children index is a documented future optimization.
    [[nodiscard]] virtual std::vector<EntityUVE> GetChildrenUVE(IEntityManagerUVE& entityManager,
                                                                 EntityUVE parent) = 0;

    /// The inherited node modes UpdateUVE() resolved for `entity`, or nothing when the entity was
    /// not part of the last update (not a scene-graph node, or created since).
    ///
    /// WHY CONSUMERS ASK HERE RATHER THAN READING THE COMPONENT. The resolved answer is written
    /// onto a ProcessComponentUVE / AutoTranslateComponentUVE only when the entity carries one.
    /// An entity without the component still HAS an answer - inherited from its ancestors - and a
    /// consumer that read only the component would ignore it: a label with no Auto Translate
    /// component under a Disabled menu would still be translated, which is exactly the
    /// inheritance the mode promises. This returns the answer for every node, carrier or not.
    [[nodiscard]] virtual std::optional<ResolvedNodeModesUVE> TryGetResolvedNodeModesUVE(
        EntityUVE entity) const = 0;
};

} // namespace UVE::Scene
