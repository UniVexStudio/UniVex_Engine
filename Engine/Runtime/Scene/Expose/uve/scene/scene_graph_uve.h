// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <unordered_map>
#include <vector>

#include "uve/component/transform_component_uve.h"
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

private:
    /// Whether an entity's derived world transform came out valid this pass, and whether it was
    /// actually RECOMPUTED as opposed to merely visited. Children need the second flag: a parent
    /// that recomputed forces its children to recompute regardless of their own dirty flags.
    struct WorldTransformPassStateUVE final {
        bool valid = false;
        bool recomputed = false;
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
    };

    /// Per-update scratch, cleared on entry to UpdateUVE(). Retained between calls purely to keep
    /// their capacity; see the class doc comment.
    std::vector<PendingEntityUVE> m_pendingScratch;
    std::unordered_map<EntityUVE, WorldTransformPassStateUVE> m_passStateScratch;
};

} // namespace UVE::Scene
