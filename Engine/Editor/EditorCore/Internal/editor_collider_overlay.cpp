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

#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

#include "editor.h"

#include "hierarchy_component.h"
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

[[nodiscard]] bool IsAncestorOrSame(const Scene::IEntityManager& entities, Scene::Entity candidate,
                                    const Scene::Entity ancestor) noexcept {
    if (ancestor == Scene::kInvalidEntity) {
        return false;
    }
    for (std::uint32_t hops = 0U; hops < 16U && candidate != Scene::kInvalidEntity && entities.IsAlive(candidate);
         ++hops) {
        if (candidate == ancestor) {
            return true;
        }
        if (!entities.HasComponent<Scene::HierarchyComponent>(candidate)) {
            break;
        }
        const Scene::Entity parent = entities.GetComponent<Scene::HierarchyComponent>(candidate).parent;
        if (parent == candidate) {
            break;
        }
        candidate = parent;
    }
    return false;
}

} // namespace

void Editor::BuildColliderOverlay(std::vector<ViewportColliderLine>& outLines) const {
    outLines.clear();
    Scene::IEntityManager& entities = m_services->GetEntityManager();
    Scene::ISceneGraph& sceneGraph = m_services->GetSceneGraph();
    const Scene::Entity selected = GetSelectedEntity();
    const bool documentSimulating =
        m_entityDocumentActive ? (GetEntitySimulationState() == EntitySimulationState::Running)
                               : (m_playModeState == EditorPlayModeState::Playing);

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
    }

    // Collect every Ragdoll3D that should be visualized this frame:
    // - Selected Ragdoll3D (capsules + active-bone gold highlight + orange joint cones, always X-ray)
    // - Ragdoll3D whose linked Skeleton3D or owning ancestor is selected (capsules, always X-ray)
    // - Ragdoll3D running live Simulate Preview (capsules, always X-ray)
    // - Any enabled Ragdoll3D when global "Show Collision" is active
    std::vector<Scene::Entity> visibleRagdolls;
    entities.ForEach<Scene::Ragdoll3DComponent>(
        [&](const Scene::Entity entity, const Scene::Ragdoll3DComponent& ragdoll) {
            if (!ragdoll.enabled && entity != selected) {
                return;
            }
            const bool directSelected = (entity == selected);
            const bool relatedSelected =
                selected != Scene::kInvalidEntity &&
                (ragdoll.skeleton == selected || IsAncestorOrSame(entities, entity, selected));
            if (directSelected || relatedSelected || ragdoll.editorPreviewSimulating ||
                m_viewportOverlayState.collisionShapesVisible) {
                visibleRagdolls.push_back(entity);
            }
        });

    bool graphUpdatedForRagdoll = false;
    std::vector<Physics::ColliderDebugLine>& highlight = HighlightScratch();
    for (const Scene::Entity ragdollEntity : visibleRagdolls) {
        if (!entities.IsAlive(ragdollEntity) || !entities.HasComponent<Scene::Ragdoll3DComponent>(ragdollEntity)) {
            continue;
        }
        Scene::Ragdoll3DComponent& ragdoll = entities.GetComponent<Scene::Ragdoll3DComponent>(ragdollEntity);
        const bool directSelected = (ragdollEntity == selected);

        if (!ragdoll.initialized && ragdoll.skeleton != Scene::kInvalidEntity && entities.IsAlive(ragdoll.skeleton) &&
            entities.HasComponent<Scene::Skeleton3DComponent>(ragdoll.skeleton)) {
            if (!graphUpdatedForRagdoll) {
                sceneGraph.Update(entities);
                graphUpdatedForRagdoll = true;
            }
            if (ragdoll.bones.empty()) {
                const auto& skel = entities.GetComponent<Scene::Skeleton3DComponent>(ragdoll.skeleton);
                ragdoll.bones = Scene::BuildHumanoidRagdollBones(skel.bones);
                if (ragdoll.bones.size() < 2U && !skel.bones.empty()) {
                    ragdoll.bones = Scene::BuildGenericRagdollBones(skel.bones);
                }
            }
            static_cast<void>(Scene::InitializeRagdoll3D(entities, ragdollEntity));
        }

        if (!ragdoll.initialized || ragdoll.solver.GetBodyCount() == 0U) {
            continue;
        }

        // Keep Edit-mode capsules aligned with the character's current transform & pose.
        if (!ragdoll.editorPreviewSimulating && !documentSimulating) {
            if (!graphUpdatedForRagdoll) {
                sceneGraph.Update(entities);
                graphUpdatedForRagdoll = true;
            }
            static_cast<void>(Scene::SyncRagdollBodiesToSkeletonPose(entities, ragdollEntity));
        }

        const std::size_t bodyCount = ragdoll.solver.GetBodyCount();
        Physics::ColliderDebugStyle capsuleStyle;
        capsuleStyle.circleSegments = directSelected ? 16U : 12U;

        constexpr std::array<float, 3> kCapsuleColor{0.26F, 0.96F, 0.72F};
        constexpr std::array<float, 3> kKinematicColor{0.48F, 0.70F, 0.96F};
        constexpr std::array<float, 3> kActiveBoneColor{1.00F, 0.84F, 0.20F};
        constexpr std::array<float, 3> kJointConeColor{1.00F, 0.56F, 0.18F};

        for (std::size_t i = 0U; i < bodyCount; ++i) {
            const Physics::RagdollBody& body = ragdoll.solver.GetBody(i);
            bool isActiveBone = false;
            bool simulateBody = true;
            if (i < ragdoll.bindings.size()) {
                const std::size_t setupIdx = ragdoll.bindings[i].setupIndex;
                if (setupIdx < ragdoll.bones.size()) {
                    const Scene::RagdollBoneSetup& setup = ragdoll.bones[setupIdx];
                    simulateBody = setup.simulate;
                    if (directSelected && !m_selectedRagdollBoneName.empty() &&
                        setup.boneName == m_selectedRagdollBoneName) {
                        isActiveBone = true;
                    }
                }
            }
            Scene::ColliderComponent capsule;
            capsule.shapeType = Scene::ColliderShapeType::Capsule;
            capsule.radius = body.radius;
            capsule.height = 2.0F * (std::max(0.0F, body.halfHeight) + body.radius);
            const Physics::ColliderDebugBuildResult built = Physics::BuildColliderDebugLines(
                capsule, body.position, body.rotation,
                std::span<Physics::ColliderDebugLine>{highlight.data(), highlight.size()}, capsuleStyle);
            const std::array<float, 3> colour =
                isActiveBone ? kActiveBoneColor : (simulateBody ? kCapsuleColor : kKinematicColor);
            const bool thick = directSelected || isActiveBone;
            for (std::size_t l = 0U; l < built.lineCount; ++l) {
                const Physics::ColliderDebugLine& line = highlight[l];
                outLines.push_back(ViewportColliderLine{{line.from.x, line.from.y, line.from.z},
                                                           {line.to.x, line.to.y, line.to.z},
                                                           thick, /*xray=*/true, /*hasColour=*/true, colour});
            }
        }

        if (directSelected) {
            Physics::RagdollDebugDrawOptions jointOptions;
            jointOptions.style = capsuleStyle;
            jointOptions.drawBodies = false;
            jointOptions.drawJointCones = true;
            const Physics::ColliderDebugBuildResult jointsBuilt = Physics::BuildRagdollDebugLines(
                ragdoll.solver, std::span<Physics::ColliderDebugLine>{highlight.data(), highlight.size()},
                jointOptions);
            for (std::size_t l = 0U; l < jointsBuilt.lineCount; ++l) {
                const Physics::ColliderDebugLine& line = highlight[l];
                outLines.push_back(ViewportColliderLine{{line.from.x, line.from.y, line.from.z},
                                                           {line.to.x, line.to.y, line.to.z},
                                                           false, /*xray=*/true, /*hasColour=*/true, kJointConeColor});
            }
        }
    }

    if (selected == Scene::kInvalidEntity || !entities.IsAlive(selected)) {
        return;
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
