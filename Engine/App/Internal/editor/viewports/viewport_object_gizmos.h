// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include <cstddef>

#include "entity.h"
#include "univex/gizmo/gizmo_geometry.h"

namespace UVE::Scene {
class IEntityManagerUVE;
}

namespace UVE::App::Viewports {

struct ViewportObjectGizmoBuildResultUVE final {
    univex::gizmo::GizmoMesh mesh;
    /// Index in `mesh.lines` where depth-occluded collider wireframes begin.
    std::size_t colliderLinesStart = 0U;
};

/// Stateless spatial gizmo mesh builders for interactive 3D authoring viewports
/// (`LevelViewportUVE` and `EntityViewportUVE`).
class ViewportObjectGizmosUVE final {
public:
    ViewportObjectGizmosUVE() = delete;

    [[nodiscard]] static univex::gizmo::GizmoMesh BuildCameraFrustumMeshUVE(Scene::IEntityManagerUVE& entityManager,
                                                                            Scene::EntityUVE hideEntity,
                                                                            float aspectRatio);
    [[nodiscard]] static univex::gizmo::GizmoMesh BuildLightDirectionMeshUVE(Scene::IEntityManagerUVE& entityManager);
    [[nodiscard]] static univex::gizmo::GizmoMesh BuildFogVolumeMeshUVE(Scene::IEntityManagerUVE& entityManager);
    [[nodiscard]] static univex::gizmo::GizmoMesh BuildReflectionProbeMeshUVE(Scene::IEntityManagerUVE& entityManager);
    [[nodiscard]] static univex::gizmo::GizmoMesh BuildWorldPartitionMeshUVE(Scene::IEntityManagerUVE& entityManager);
    [[nodiscard]] static univex::gizmo::GizmoMesh BuildVisibilityRegionMeshUVE(Scene::IEntityManagerUVE& entityManager);
    [[nodiscard]] static univex::gizmo::GizmoMesh BuildOccluderMeshUVE(Scene::IEntityManagerUVE& entityManager);
    [[nodiscard]] static univex::gizmo::GizmoMesh BuildColliderMeshUVE(Scene::IEntityManagerUVE& entityManager);
    [[nodiscard]] static univex::gizmo::GizmoMesh BuildSpatialObjectGizmosMeshUVE(
        Scene::IEntityManagerUVE& entityManager,
        bool includeSpotCones);

    /// Builds the combined spatial gizmo overlay for one authoring frame. When
    /// `hostDrawsPrimaryGlyphs` is true, camera frustums, directional lights, and spot light cones
    /// are omitted because the PRISM shell draws its own glyphs for them.
    [[nodiscard]] static ViewportObjectGizmoBuildResultUVE BuildAllObjectGizmosUVE(
        Scene::IEntityManagerUVE& entityManager,
        Scene::EntityUVE hideCameraEntity,
        float aspectRatio,
        bool hostDrawsPrimaryGlyphs);
};

} // namespace UVE::App::Viewports
