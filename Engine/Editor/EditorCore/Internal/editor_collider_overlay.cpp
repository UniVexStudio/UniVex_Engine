// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The editor's "Show Collision" overlay: the bridge between the physics layer's wireframe
// geometry and the viewport's line list.
//
// What lives here and what does not. The GEOMETRY is `Physics::BuildColliderDebugLines` - pure
// math over one collider, no ECS, unit-tested without a world. WHICH colliders exist and whether
// they can be hit is `Scene::BuildColliderDebugScene`, which asks the same participation
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
constexpr std::size_t kMaximumOverlayLines = 16384U;

/// Reused across frames so a visible overlay does not allocate every frame. Thread-local because
/// an Editor is main-thread-only, and a static would still be wrong if a second editor instance
/// ever runs in the same process - 384 KB is cheap, aliasing between two editors is not.
[[nodiscard]] std::vector<Physics::ColliderDebugLine>& OverlayScratch() {
    static thread_local std::vector<Physics::ColliderDebugLine> scratch(kMaximumOverlayLines);
    return scratch;
}

/// One collider's worth of lines, reused. Sized for the most tessellated shape at the finest style.
[[nodiscard]] std::vector<Physics::ColliderDebugLine>& HighlightScratch() {
    static thread_local std::vector<Physics::ColliderDebugLine> scratch(4096U);
    return scratch;
}

} // namespace

void Editor::BuildColliderOverlay(std::vector<ViewportColliderLine>& outLines) const {
    outLines.clear();
    Scene::IEntityManager& entities = m_services->GetEntityManager();
    const Scene::Entity selected = GetSelectedEntity();

    if (m_viewportOverlayState.collisionShapesVisible) {
        std::vector<Physics::ColliderDebugLine>& scratch = OverlayScratch();

        Scene::ColliderDebugSceneSettings settings;
        settings.maximumColliders = 1024U;
        settings.excludedEntity = selected;

        const Scene::ColliderDebugSceneStats stats = Scene::BuildColliderDebugScene(
            entities, settings,
            std::span<Physics::ColliderDebugLine>{scratch.data(), kMaximumOverlayLines});

        outLines.reserve(stats.lineCount + 512U);
        for (std::size_t index = 0U; index < stats.lineCount; ++index) {
            const Physics::ColliderDebugLine& line = scratch[index];
            outLines.push_back(ViewportColliderLine{{line.from.x, line.from.y, line.from.z},
                                                       {line.to.x, line.to.y, line.to.z}, false});
        }

        // Draw all non-selected initialized Ragdoll3D bodies when Show Collision is enabled.
        Physics::RagdollDebugDrawOptions bulkRagdollOptions;
        bulkRagdollOptions.style.circleSegments = 12U;
        bulkRagdollOptions.drawBodies = true;
        bulkRagdollOptions.drawJointCones = false;
        entities.ForEach<Scene::Ragdoll3DComponent>(
            [&](const Scene::Entity entity, const Scene::Ragdoll3DComponent& ragdoll) {
                if (entity == selected || !ragdoll.enabled || !ragdoll.initialized) return;
                const Physics::ColliderDebugBuildResult built = Physics::BuildRagdollDebugLines(
                    ragdoll.solver, std::span<Physics::ColliderDebugLine>{scratch.data(), scratch.size()},
                    bulkRagdollOptions);
                for (std::size_t i = 0U; i < built.lineCount; ++i) {
                    const Physics::ColliderDebugLine& line = scratch[i];
                    outLines.push_back(ViewportColliderLine{{line.from.x, line.from.y, line.from.z},
                                                               {line.to.x, line.to.y, line.to.z}, false});
                }
            });
    }

    if (selected == Scene::kInvalidEntity || !entities.IsAlive(selected)) {
        return;
    }

    // Selected Ragdoll3D: always draw highlighted capsules + 3D swing cones + hinge twist arcs
    // (initializing lazily from its linked Skeleton3D if needed so authors see capsules immediately!).
    if (entities.HasComponent<Scene::Ragdoll3DComponent>(selected)) {
        const Scene::Ragdoll3DComponent& ragdoll =
            entities.GetComponent<Scene::Ragdoll3DComponent>(selected);
        if (ragdoll.initialized && ragdoll.solver.GetBodyCount() > 0U) {
            std::vector<Physics::ColliderDebugLine>& highlight = HighlightScratch();
            Physics::RagdollDebugDrawOptions selectedRagdollOptions;
            selectedRagdollOptions.style.circleSegments = 16U;
            selectedRagdollOptions.drawBodies = true;
            selectedRagdollOptions.drawJointCones = true;
            const Physics::ColliderDebugBuildResult built = Physics::BuildRagdollDebugLines(
                ragdoll.solver, std::span<Physics::ColliderDebugLine>{highlight.data(), highlight.size()},
                selectedRagdollOptions);
            for (std::size_t i = 0U; i < built.lineCount; ++i) {
                const Physics::ColliderDebugLine& line = highlight[i];
                outLines.push_back(ViewportColliderLine{{line.from.x, line.from.y, line.from.z},
                                                           {line.to.x, line.to.y, line.to.z}, true});
            }
        }
    }

    if (!m_viewportOverlayState.collisionShapesVisible) {
        return;
    }

    const Scene::Entity highlighted =
        Scene::ResolveCharacterShapeEntity(entities, m_services->GetSceneGraph(), selected);
    if (!entities.HasComponent<Scene::ColliderComponent>(highlighted) ||
        !entities.HasComponent<Scene::WorldTransformComponent>(highlighted)) {
        return;
    }
    const Scene::ColliderComponent& collider =
        entities.GetComponent<Scene::ColliderComponent>(highlighted);
    if (!Scene::Collider3D::IsParticipating(collider)) {
        return;
    }
    const Scene::WorldTransformComponent& worldTransform =
        entities.GetComponent<Scene::WorldTransformComponent>(highlighted);
    std::vector<Physics::ColliderDebugLine>& highlight = HighlightScratch();
    Scene::ColliderDebugSceneSettings settings;
    const Physics::ColliderDebugBuildResult built = Physics::BuildColliderDebugLines(
        collider, worldTransform.worldPosition, worldTransform.worldRotation,
        std::span<Physics::ColliderDebugLine>{highlight.data(), highlight.size()}, settings.style,
        worldTransform.worldScale);
    for (std::size_t index = 0U; index < built.lineCount; ++index) {
        const Physics::ColliderDebugLine& line = highlight[index];
        outLines.push_back(ViewportColliderLine{{line.from.x, line.from.y, line.from.z},
                                                   {line.to.x, line.to.y, line.to.z}, true});
    }
}

bool Editor::SetViewportCollisionShapes(const bool visible) {
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
