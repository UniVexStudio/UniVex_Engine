// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The editor's "Show Collision" overlay: the bridge between the physics layer's wireframe
// geometry and the viewport's line list.
//
// What lives here and what does not. The GEOMETRY is `Physics::BuildColliderDebugLinesUVE` - pure
// math over one collider, no ECS, unit-tested without a world. WHICH colliders exist and whether
// they can be hit is `Scene::BuildColliderDebugSceneUVE`, which asks the same participation
// helpers the collision system asks. This file owns exactly one thing those two cannot:
// translating the result into the viewport's own layout (plain float triples, no engine math
// types), so EditorCore keeps its documented promise of not linking the viewport module.

#include <cstddef>
#include <span>
#include <vector>

#include "editor.h"

#include "world_transform_component.h"
#include "character_3d.h"
#include "collider_3d.h"
#include "collider_debug_scene.h"
#include "ragdoll_3d.h"

namespace UVE::Editor {
namespace {

/// Enough for a level's worth of props at the default collider budget: a few hundred boxes and a
/// dozen capsules fit with room to spare. 16K segments is 384 KB of line data, and a level past it
/// degrades to the first shapes that fit rather than to an unbounded allocation on a debug toggle.
constexpr std::size_t kMaximumOverlayLinesUVE = 16384U;

/// Reused across frames so a visible overlay does not allocate every frame. Thread-local because
/// an EditorUVE is main-thread-only, and a static would still be wrong if a second editor instance
/// ever runs in the same process - 384 KB is cheap, aliasing between two editors is not.
[[nodiscard]] std::vector<Physics::ColliderDebugLineUVE>& OverlayScratchUVE() {
    static thread_local std::vector<Physics::ColliderDebugLineUVE> scratch(kMaximumOverlayLinesUVE);
    return scratch;
}

/// One collider's worth of lines, reused. Sized for the most tessellated shape at the finest style.
[[nodiscard]] std::vector<Physics::ColliderDebugLineUVE>& HighlightScratchUVE() {
    static thread_local std::vector<Physics::ColliderDebugLineUVE> scratch(4096U);
    return scratch;
}

} // namespace

void EditorUVE::BuildColliderOverlayUVE(std::vector<ViewportColliderLineUVE>& outLines) const {
    outLines.clear();
    Scene::IEntityManagerUVE& entities = m_services->GetEntityManagerUVE();
    const Scene::EntityUVE selected = GetSelectedEntityUVE();

    if (m_viewportOverlayState.collisionShapesVisible) {
        std::vector<Physics::ColliderDebugLineUVE>& scratch = OverlayScratchUVE();

        Scene::ColliderDebugSceneSettingsUVE settings;
        settings.maximumColliders = 1024U;
        settings.excludedEntity = selected;

        const Scene::ColliderDebugSceneStatsUVE stats = Scene::BuildColliderDebugSceneUVE(
            entities, settings,
            std::span<Physics::ColliderDebugLineUVE>{scratch.data(), kMaximumOverlayLinesUVE});

        outLines.reserve(stats.lineCount + 512U);
        for (std::size_t index = 0U; index < stats.lineCount; ++index) {
            const Physics::ColliderDebugLineUVE& line = scratch[index];
            outLines.push_back(ViewportColliderLineUVE{{line.from.x, line.from.y, line.from.z},
                                                       {line.to.x, line.to.y, line.to.z}, false});
        }

        // Draw all non-selected initialized Ragdoll3D bodies when Show Collision is enabled.
        Physics::RagdollDebugDrawOptionsUVE bulkRagdollOptions;
        bulkRagdollOptions.style.circleSegments = 12U;
        bulkRagdollOptions.drawBodies = true;
        bulkRagdollOptions.drawJointCones = false;
        entities.ForEachUVE<Scene::Ragdoll3DComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::Ragdoll3DComponentUVE& ragdoll) {
                if (entity == selected || !ragdoll.enabled || !ragdoll.initialized) return;
                const Physics::ColliderDebugBuildResultUVE built = Physics::BuildRagdollDebugLinesUVE(
                    ragdoll.solver, std::span<Physics::ColliderDebugLineUVE>{scratch.data(), scratch.size()},
                    bulkRagdollOptions);
                for (std::size_t i = 0U; i < built.lineCount; ++i) {
                    const Physics::ColliderDebugLineUVE& line = scratch[i];
                    outLines.push_back(ViewportColliderLineUVE{{line.from.x, line.from.y, line.from.z},
                                                               {line.to.x, line.to.y, line.to.z}, false});
                }
            });
    }

    if (selected == Scene::kInvalidEntityUVE || !entities.IsAliveUVE(selected)) {
        return;
    }

    // Selected Ragdoll3D: always draw highlighted capsules + 3D swing cones + hinge twist arcs
    // (initializing lazily from its linked Skeleton3D if needed so authors see capsules immediately!).
    if (entities.HasComponentUVE<Scene::Ragdoll3DComponentUVE>(selected)) {
        const Scene::Ragdoll3DComponentUVE& ragdoll =
            entities.GetComponentUVE<Scene::Ragdoll3DComponentUVE>(selected);
        if (ragdoll.initialized && ragdoll.solver.GetBodyCountUVE() > 0U) {
            std::vector<Physics::ColliderDebugLineUVE>& highlight = HighlightScratchUVE();
            Physics::RagdollDebugDrawOptionsUVE selectedRagdollOptions;
            selectedRagdollOptions.style.circleSegments = 16U;
            selectedRagdollOptions.drawBodies = true;
            selectedRagdollOptions.drawJointCones = true;
            const Physics::ColliderDebugBuildResultUVE built = Physics::BuildRagdollDebugLinesUVE(
                ragdoll.solver, std::span<Physics::ColliderDebugLineUVE>{highlight.data(), highlight.size()},
                selectedRagdollOptions);
            for (std::size_t i = 0U; i < built.lineCount; ++i) {
                const Physics::ColliderDebugLineUVE& line = highlight[i];
                outLines.push_back(ViewportColliderLineUVE{{line.from.x, line.from.y, line.from.z},
                                                           {line.to.x, line.to.y, line.to.z}, true});
            }
        }
    }

    if (!m_viewportOverlayState.collisionShapesVisible) {
        return;
    }

    const Scene::EntityUVE highlighted =
        Scene::ResolveCharacterShapeEntityUVE(entities, m_services->GetSceneGraphUVE(), selected);
    if (!entities.HasComponentUVE<Scene::ColliderComponentUVE>(highlighted) ||
        !entities.HasComponentUVE<Scene::WorldTransformComponentUVE>(highlighted)) {
        return;
    }
    const Scene::ColliderComponentUVE& collider =
        entities.GetComponentUVE<Scene::ColliderComponentUVE>(highlighted);
    if (!Scene::Collider3DUVE::IsParticipatingUVE(collider)) {
        return;
    }
    const Scene::WorldTransformComponentUVE& worldTransform =
        entities.GetComponentUVE<Scene::WorldTransformComponentUVE>(highlighted);
    std::vector<Physics::ColliderDebugLineUVE>& highlight = HighlightScratchUVE();
    Scene::ColliderDebugSceneSettingsUVE settings;
    const Physics::ColliderDebugBuildResultUVE built = Physics::BuildColliderDebugLinesUVE(
        collider, worldTransform.worldPosition, worldTransform.worldRotation,
        std::span<Physics::ColliderDebugLineUVE>{highlight.data(), highlight.size()}, settings.style,
        worldTransform.worldScale);
    for (std::size_t index = 0U; index < built.lineCount; ++index) {
        const Physics::ColliderDebugLineUVE& line = highlight[index];
        outLines.push_back(ViewportColliderLineUVE{{line.from.x, line.from.y, line.from.z},
                                                   {line.to.x, line.to.y, line.to.z}, true});
    }
}

bool EditorUVE::SetViewportCollisionShapesUVE(const bool visible) {
    const bool previous = m_viewportOverlayState.collisionShapesVisible;
    m_viewportOverlayState.collisionShapesVisible = visible;
    if (!visible) {
        // Dropping the geometry with the flag keeps the "no lines while off" invariant true for
        // the host, which reads both fields without re-checking the flag first.
        m_viewportOverlayState.colliderLines.clear();
        m_entityViewportOverlayState.colliderLines.clear();
        m_retargetViewportOverlayState.colliderLines.clear();
    }
    return previous != visible;
}

} // namespace UVE::Editor
