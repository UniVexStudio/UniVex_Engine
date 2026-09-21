// univex/integration/EntityPicker.h (private to this module - not under Expose/)
// -----------------------------------------------------------------------
// What the cursor is pointing at in the viewport.
//
// Until now nothing in the editor could select by clicking: selection arrived only from the
// hierarchy panel, so the 3D view was something to look at rather than something to work in.
// This is the missing query. It lives in the engine bridge because it is the one target in this
// module allowed to see UVE::* types - it has to read the live ECS - and beside EditorMeshLayer
// for the same reason.
//
// Deliberately NOT built on EditorUVE::TryGetEntityBoundsUVE, which requires a
// ColliderComponentUVE and therefore cannot see a mesh that was never given physics. Almost
// everything an author places in an empty scene falls into exactly that gap.
//
// Two phases, the usual way round. The broad phase reuses Math::IntersectRayUVE - the same
// slab-method ray/AABB the physics raycast and shape-cast systems call - against each candidate's
// world bounds. The narrow phase then intersects the primitive's real indexed triangles, so
// clicking the empty corner beside a sphere does not select the sphere. Nearest hit wins.
// -----------------------------------------------------------------------
#pragma once

#include "uve/component/entity_uve.h"
#include "uve/math/ray_uve.h"
#include "uve/entity/i_entity_manager_uve.h"

namespace univex::camera {
class OrbitCamera;
}

namespace univex::integration {

struct EntityPickResultUVE final {
    UVE::Scene::EntityUVE entity = UVE::Scene::kInvalidEntityUVE;
    /// World-space distance along the ray to the hit. Only meaningful when `hit` is true.
    float distance = 0.0F;
    bool hit = false;
};

/// The world-space ray through a viewport pixel, with the pixel given in the same top-left-origin
/// coordinates the panel receives from the host (x right, y down).
///
/// Built by unprojecting the near and far plane through OrbitCamera::InverseViewProjection, which
/// is the identical reconstruction infinite_grid.vert performs per pixel - reusing that one
/// formulation rather than deriving a second one that could drift from it.
[[nodiscard]] UVE::Math::RayUVE BuildCursorRayUVE(const univex::camera::OrbitCamera& camera,
                                                  int viewportWidth, int viewportHeight,
                                                  float pixelX, float pixelY);

/// The inverse of BuildCursorRayUVE: projects a world-space point through
/// camera.ViewProjection(aspect) to a pixel in that same top-left-origin space (x right, y down).
/// Returns false - and leaves outPixelX/outPixelY untouched - when the point is behind the camera
/// or the projection is otherwise degenerate (w too close to zero), rather than silently returning
/// a misleading {0, 0}. Used to anchor viewport-space UI (e.g. an entity context toolbar) to a
/// world-space position.
[[nodiscard]] bool ProjectWorldPointToPixelUVE(const univex::camera::OrbitCamera& camera,
                                               int viewportWidth, int viewportHeight,
                                               const UVE::Math::Vector3UVE& worldPoint,
                                               float& outPixelX, float& outPixelY);

/// The nearest entity `worldRay` hits, or a result with `hit == false`.
///
/// Candidates are entities carrying a clean (non-dirty) WorldTransformComponentUVE and a
/// PrimitiveMeshComponentUVE, tested exactly against their triangles. Editor-internal entities are
/// skipped: the viewport's own hidden camera proxy and headlight are not things an author can
/// select.
///
/// Asset-backed MeshComponentUVE entities are deliberately NOT candidates yet. Resolving one needs
/// the asset manager, and with no mesh asset pipeline in the engine a MeshInstance3D currently
/// renders nothing at all - so the branch could never fire, and a picker that selects invisible
/// things would be worse than one that admits the gap. It is a candidate source to add the day
/// those meshes render.
[[nodiscard]] EntityPickResultUVE PickEntityAlongRayUVE(UVE::Scene::IEntityManagerUVE& entityManager,
                                                        const UVE::Math::RayUVE& worldRay,
                                                        float maxDistance);

/// BuildCursorRayUVE followed by PickEntityAlongRayUVE - what the panel actually calls.
[[nodiscard]] EntityPickResultUVE PickEntityAtPixelUVE(UVE::Scene::IEntityManagerUVE& entityManager,
                                                       const univex::camera::OrbitCamera& camera,
                                                       int viewportWidth, int viewportHeight,
                                                       float pixelX, float pixelY);

} // namespace univex::integration
