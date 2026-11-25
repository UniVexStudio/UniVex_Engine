// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include <cstddef>

#include "entity.h"
#include "univex/gizmo/gizmo_geometry.h"

namespace UVE::Scene {
class IEntityManager;
}

namespace UVE::App::Viewports {

struct ViewportObjectGizmoBuildResult final {
    univex::gizmo::GizmoMesh mesh;
    /// Index in `mesh.lines` where depth-occluded collider wireframes begin.
    std::size_t colliderLinesStart = 0U;
};

/// Stateless spatial gizmo mesh builders for interactive 3D authoring viewports
/// (`LevelViewport` and `EntityViewport`).
class ViewportObjectGizmos final {
public:
    ViewportObjectGizmos() = delete;

    [[nodiscard]] static univex::gizmo::GizmoMesh BuildCameraFrustumMesh(Scene::IEntityManager& entityManager,
                                                                            Scene::Entity hideEntity,
                                                                            float aspectRatio);
    [[nodiscard]] static univex::gizmo::GizmoMesh BuildLightDirectionMesh(Scene::IEntityManager& entityManager);
    [[nodiscard]] static univex::gizmo::GizmoMesh BuildFogVolumeMesh(Scene::IEntityManager& entityManager);
    [[nodiscard]] static univex::gizmo::GizmoMesh BuildReflectionProbeMesh(Scene::IEntityManager& entityManager);
    [[nodiscard]] static univex::gizmo::GizmoMesh BuildWorldPartitionMesh(Scene::IEntityManager& entityManager);
    [[nodiscard]] static univex::gizmo::GizmoMesh BuildVisibilityRegionMesh(Scene::IEntityManager& entityManager);
    [[nodiscard]] static univex::gizmo::GizmoMesh BuildOccluderMesh(Scene::IEntityManager& entityManager);
    [[nodiscard]] static univex::gizmo::GizmoMesh BuildColliderMesh(Scene::IEntityManager& entityManager);
    [[nodiscard]] static univex::gizmo::GizmoMesh BuildSpatialObjectGizmosMesh(
        Scene::IEntityManager& entityManager,
        bool includeSpotCones);

    /// Builds the combined spatial gizmo overlay for one authoring frame. When
    /// `hostDrawsPrimaryGlyphs` is true, camera frustums, directional lights, and spot light cones
    /// are omitted because the PRISM shell draws its own glyphs for them.
    [[nodiscard]] static ViewportObjectGizmoBuildResult BuildAllObjectGizmos(
        Scene::IEntityManager& entityManager,
        Scene::Entity hideCameraEntity,
        float aspectRatio,
        bool hostDrawsPrimaryGlyphs);
};

} // namespace UVE::App::Viewports
