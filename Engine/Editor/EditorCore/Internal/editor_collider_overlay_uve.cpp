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

#include "uve/editor/editor_uve.h"

#include "uve/component/world_transform_component_uve.h"
#include "uve/objects/3d/character_3d_uve.h"
#include "uve/objects/3d/collider_3d_uve.h"
#include "uve/objects/3d/collider_debug_scene_uve.h"

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
    static thread_local std::vector<Physics::ColliderDebugLineUVE> scratch(512U);
    return scratch;
}

} // namespace

void EditorUVE::BuildColliderOverlayUVE(std::vector<ViewportColliderLineUVE>& outLines) const {
    outLines.clear();
    if (!m_viewportOverlayState.collisionShapesVisible) {
        return;
    }

    std::vector<Physics::ColliderDebugLineUVE>& scratch = OverlayScratchUVE();

    Scene::ColliderDebugSceneSettingsUVE settings;
    // The overlay is a view of the whole document, so the layer filter stays open: narrowing it is
    // a per-project need and belongs on the settings struct, not hard-coded here.
    settings.maximumColliders = 1024U;
    // The selected collider is drawn by the second pass below, not by the bulk one. Asking the
    // bulk pass to leave it out keeps a shape from being drawn twice at two widths in the same
    // pixels, which is what a highlight drawn straight over the base colour would do.
    const Scene::EntityUVE selected = GetSelectedEntityUVE();
    settings.excludedEntity = selected;

    const Scene::ColliderDebugSceneStatsUVE stats = Scene::BuildColliderDebugSceneUVE(
        m_services->GetEntityManagerUVE(), settings,
        std::span<Physics::ColliderDebugLineUVE>{scratch.data(), kMaximumOverlayLinesUVE});

    outLines.reserve(stats.lineCount + 256U);
    for (std::size_t index = 0U; index < stats.lineCount; ++index) {
        const Physics::ColliderDebugLineUVE& line = scratch[index];
        outLines.push_back(ViewportColliderLineUVE{{line.from.x, line.from.y, line.from.z},
                                                   {line.to.x, line.to.y, line.to.z}, false});
    }

    // Clicking the mesh marks its shape. The shape is resolved the same way the simulation
    // resolves it, so a character whose body is the Collider3D under it highlights that one and a
    // mesh with no collider highlights nothing at all - which is itself the useful answer.
    if (selected == Scene::kInvalidEntityUVE) {
        return;
    }
    Scene::IEntityManagerUVE& entities = m_services->GetEntityManagerUVE();
    // A character's shape lives on a Collider3D under it, so the highlight has to ask the same
    // question the stepper asks. For everything else this returns the entity itself.
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
    const Physics::ColliderDebugBuildResultUVE built = Physics::BuildColliderDebugLinesUVE(
        collider, worldTransform.worldPosition, worldTransform.worldRotation,
        std::span<Physics::ColliderDebugLineUVE>{highlight.data(), highlight.size()}, settings.style,
        worldTransform.worldScale);
    for (std::size_t index = 0U; index < built.lineCount; ++index) {
        const Physics::ColliderDebugLineUVE& line = highlight[index];
        outLines.push_back(ViewportColliderLineUVE{{line.from.x, line.from.y, line.from.z},
                                                   {line.to.x, line.to.y, line.to.z}, true});
    }

    // ---- Ragdoll3D Bone Colliders & Floating Trajectory Viewport Gizmos ----
    entities.ForEachUVE<Scene::Ragdoll3DComponentUVE>([this, selected, &entities, &outLines](
        const Scene::EntityUVE ragdollEntity, Scene::Ragdoll3DComponentUVE& ragdoll) {
        if (!ragdoll.enabled) return;
        const bool isSelected = (selected == ragdollEntity || selected == ragdoll.skeleton);

        // 1. Bone Colliders Wireframes
        if (ragdoll.showBoneColliders || isSelected) {
            const std::size_t bodyCount = ragdoll.solver.GetBodyCountUVE();
            for (std::size_t i = 0; i < bodyCount; ++i) {
                const Physics::RagdollBodyUVE& body = ragdoll.solver.GetBodyUVE(i);
                Scene::RagdollShapeTypeUVE shapeType = Scene::RagdollShapeTypeUVE::Capsule;
                Math::Vector3UVE boxExtents{body.radius, body.halfHeight + body.radius, body.radius};
                if (i < ragdoll.bones.size()) {
                    shapeType = ragdoll.bones[i].shapeType;
                    boxExtents = ragdoll.bones[i].boxExtents;
                }

                if (shapeType == Scene::RagdollShapeTypeUVE::Box) {
                    std::array<Physics::ColliderDebugLineUVE, 12U> boxLines{};
                    Physics::ColliderDebugBuildResultUVE res{};
                    Physics::BuildOrientedBoxDebugLinesUVE(body.position, boxExtents * 0.5F, body.rotation, boxLines, res);
                    for (std::size_t b = 0; b < res.lineCount; ++b) {
                        outLines.push_back(ViewportColliderLineUVE{
                            {boxLines[b].from.x, boxLines[b].from.y, boxLines[b].from.z},
                            {boxLines[b].to.x, boxLines[b].to.y, boxLines[b].to.z}, isSelected});
                    }
                } else {
                    Scene::ColliderComponentUVE tempCollider;
                    tempCollider.shapeType = (shapeType == Scene::RagdollShapeTypeUVE::Cylinder)
                        ? Scene::ColliderShapeTypeUVE::Cylinder : Scene::ColliderShapeTypeUVE::Capsule;
                    tempCollider.radius = body.radius;
                    tempCollider.height = 2.0F * (body.halfHeight + body.radius);

                    std::array<Physics::ColliderDebugLineUVE, 128U> shapeLines{};
                    const Physics::ColliderDebugBuildResultUVE res = Physics::BuildColliderDebugLinesUVE(
                        tempCollider, body.position, body.rotation, shapeLines);
                    for (std::size_t s = 0; s < res.lineCount; ++s) {
                        outLines.push_back(ViewportColliderLineUVE{
                            {shapeLines[s].from.x, shapeLines[s].from.y, shapeLines[s].from.z},
                            {shapeLines[s].to.x, shapeLines[s].to.y, shapeLines[s].to.z}, isSelected});
                    }
                }
            }
        }

        // 2. Floating Trajectory Arc & Ground Landing Target
        if (ragdoll.showTrajectoryGizmo && ragdoll.enableTrajectory) {
            const auto& samples = ragdoll.trajectory.samples;
            for (std::size_t s = 1; s < samples.size(); ++s) {
                outLines.push_back(ViewportColliderLineUVE{
                    {samples[s - 1].position.x, samples[s - 1].position.y, samples[s - 1].position.z},
                    {samples[s].position.x, samples[s].position.y, samples[s].position.z}, true});
            }
            if (ragdoll.trajectory.hasPredictedLanding) {
                const Math::Vector3UVE lp = ragdoll.trajectory.predictedLandingPosition;
                // Circular ring on ground
                constexpr int kRingSegments = 16;
                constexpr float kRingRadius = 0.35F;
                for (int r = 0; r < kRingSegments; ++r) {
                    const float a1 = static_cast<float>(r) * (6.2831853F / static_cast<float>(kRingSegments));
                    const float a2 = static_cast<float>(r + 1) * (6.2831853F / static_cast<float>(kRingSegments));
                    outLines.push_back(ViewportColliderLineUVE{
                        {lp.x + std::cos(a1) * kRingRadius, lp.y + 0.02F, lp.z + std::sin(a1) * kRingRadius},
                        {lp.x + std::cos(a2) * kRingRadius, lp.y + 0.02F, lp.z + std::sin(a2) * kRingRadius}, true});
                }
                // Crosshair marker
                outLines.push_back(ViewportColliderLineUVE{{lp.x - kRingRadius, lp.y + 0.02F, lp.z}, {lp.x + kRingRadius, lp.y + 0.02F, lp.z}, true});
                outLines.push_back(ViewportColliderLineUVE{{lp.x, lp.y + 0.02F, lp.z - kRingRadius}, {lp.x, lp.y + 0.02F, lp.z + kRingRadius}, true});
            }
        }

        // 3. Center of Mass & Balance Support Polygon
        if (ragdoll.showCenterOfMass && ragdoll.balanceState.balanceState != Physics::BalanceStateUVE::UnrecoverableFall) {
            const Math::Vector3UVE com = ragdoll.balanceState.centerOfMass;
            constexpr float kCross = 0.08F;
            outLines.push_back(ViewportColliderLineUVE{{com.x - kCross, com.y, com.z}, {com.x + kCross, com.y, com.z}, true});
            outLines.push_back(ViewportColliderLineUVE{{com.x, com.y - kCross, com.z}, {com.x, com.y + kCross, com.z}, true});
            outLines.push_back(ViewportColliderLineUVE{{com.x, com.y, com.z - kCross}, {com.x, com.y, com.z + kCross}, true});
            // Plumb line to ground ZMP
            outLines.push_back(ViewportColliderLineUVE{
                {com.x, com.y, com.z},
                {ragdoll.balanceState.extrapolatedCenterOfMass.x, 0.02F, ragdoll.balanceState.extrapolatedCenterOfMass.y}, false});
        }
    });
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
