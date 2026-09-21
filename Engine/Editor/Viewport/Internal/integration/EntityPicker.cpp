#include "integration/EntityPicker.h"

#include <cmath>
#include <limits>

#include "integration/MathConversions.h"

#include "univex/camera/OrbitCamera.h"
#include "univex/math/Mat4.h"
#include "univex/math/Vec.h"

#include "uve/asset/mesh_asset_uve.h"
#include "uve/component/editor_internal_entity_component_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/math/aabb_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/render_systems/primitive_geometry_uve.h"

namespace univex::integration {

namespace {

using UVE::Math::AabbUVE;
using UVE::Math::Matrix4x4UVE;
using UVE::Math::RayUVE;
using UVE::Math::Vector3UVE;
using UVE::Scene::EntityUVE;

/// Möller-Trumbore. Returns the distance along `ray` to the front- or back-facing triangle, or a
/// negative value for a miss. Double-sided on purpose: an editor click must select a plane
/// primitive from underneath as readily as from above, and a "you clicked the back of it" miss
/// would read as the click being ignored.
[[nodiscard]] float IntersectTriangleUVE(const RayUVE& ray, const Vector3UVE& a, const Vector3UVE& b,
                                         const Vector3UVE& c) noexcept {
    constexpr float kParallelEpsilon = 1e-8F;

    const Vector3UVE edge1 = b - a;
    const Vector3UVE edge2 = c - a;
    const Vector3UVE pvec = UVE::Math::CrossUVE(ray.direction, edge2);
    const float determinant = UVE::Math::DotUVE(edge1, pvec);
    if (std::fabs(determinant) < kParallelEpsilon) {
        return -1.0F; // ray lies in the triangle's plane
    }

    const float inverseDeterminant = 1.0F / determinant;
    const Vector3UVE tvec = ray.origin - a;
    const float u = UVE::Math::DotUVE(tvec, pvec) * inverseDeterminant;
    if (u < 0.0F || u > 1.0F) {
        return -1.0F;
    }

    const Vector3UVE qvec = UVE::Math::CrossUVE(tvec, edge1);
    const float v = UVE::Math::DotUVE(ray.direction, qvec) * inverseDeterminant;
    if (v < 0.0F || u + v > 1.0F) {
        return -1.0F;
    }

    return UVE::Math::DotUVE(edge2, qvec) * inverseDeterminant;
}

/// Applies only the linear (rotation/scale) part of an affine matrix, which is what a direction
/// needs - a direction has no position to translate. UVE::Math exposes TransformPointUVE but no
/// direction form, and subtracting the transformed origin is exactly the linear part rather than
/// an approximation of it.
[[nodiscard]] Vector3UVE TransformDirectionUVE(const Matrix4x4UVE& matrix,
                                               const Vector3UVE& direction) noexcept {
    const Vector3UVE transformedOrigin = UVE::Math::TransformPointUVE(matrix, Vector3UVE{});
    return UVE::Math::TransformPointUVE(matrix, direction) - transformedOrigin;
}

/// The world matrix for a clean world transform, composed the same way Renderer3DUVE composes it
/// for the very same entity - so what is pickable is placed exactly where it is drawn.
[[nodiscard]] bool TryComputeWorldMatrixUVE(const UVE::Scene::WorldTransformComponentUVE& worldTransform,
                                            Matrix4x4UVE& outWorldMatrix) noexcept {
    if (!UVE::Math::IsFiniteUVE(worldTransform.worldPosition) ||
        !UVE::Math::IsFiniteUVE(worldTransform.worldScale) ||
        !UVE::Math::IsFiniteUVE(worldTransform.worldRotation)) {
        return false;
    }
    UVE::Math::QuaternionUVE normalizedRotation;
    if (!UVE::Math::TryNormalizeUVE(worldTransform.worldRotation, normalizedRotation)) {
        return false;
    }
    outWorldMatrix = Matrix4x4UVE::ComposeTrsUVE(worldTransform.worldPosition, normalizedRotation,
                                                 worldTransform.worldScale);
    return true;
}

/// Considers one candidate hit, keeping whichever is nearest along the ray.
void ConsiderUVE(EntityPickResultUVE& best, const EntityUVE entity, const float distance) noexcept {
    if (!std::isfinite(distance) || distance < 0.0F) {
        return;
    }
    if (!best.hit || distance < best.distance) {
        best = EntityPickResultUVE{entity, distance, true};
    }
}

/// Exact ray/geometry test for one primitive. `worldRay` is transformed into the primitive's own
/// local space rather than transforming its triangles into world space: the local geometry is an
/// immutable shared cache, so moving the ray touches four vectors instead of copying a mesh, and
/// non-uniform scale is handled by the transform itself rather than by rescaling normals.
[[nodiscard]] float IntersectPrimitiveUVE(const RayUVE& worldRay, const Matrix4x4UVE& worldMatrix,
                                          const UVE::Scene::PrimitiveMeshKindUVE kind) {
    Matrix4x4UVE inverseWorldMatrix{};
    if (!UVE::Math::TryInverseUVE(worldMatrix, inverseWorldMatrix)) {
        return -1.0F; // a degenerate (zero-scaled) object has no volume to click
    }

    const Vector3UVE localOrigin = UVE::Math::TransformPointUVE(inverseWorldMatrix, worldRay.origin);
    // Direction, not point: deliberately NOT renormalised. Keeping the local direction as the
    // exact image of the world direction means the parameter t the triangle test returns is
    // already a world-space distance, with no per-axis scale factor to undo afterwards.
    const Vector3UVE localDirection = TransformDirectionUVE(inverseWorldMatrix, worldRay.direction);

    const RayUVE localRay{localOrigin, localDirection};
    const UVE::Render::PrimitiveGeometryUVE& geometry = UVE::Render::GetPrimitiveGeometryUVE(kind);
    if (geometry.indices.size() < 3U) {
        return -1.0F;
    }

    float nearest = -1.0F;
    for (std::size_t index = 0U; index + 2U < geometry.indices.size(); index += 3U) {
        const Vector3UVE& a = geometry.vertices[geometry.indices[index]].position;
        const Vector3UVE& b = geometry.vertices[geometry.indices[index + 1U]].position;
        const Vector3UVE& c = geometry.vertices[geometry.indices[index + 2U]].position;
        const float distance = IntersectTriangleUVE(localRay, a, b, c);
        if (distance >= 0.0F && (nearest < 0.0F || distance < nearest)) {
            nearest = distance;
        }
    }
    return nearest;
}

} // namespace

UVE::Math::RayUVE BuildCursorRayUVE(const univex::camera::OrbitCamera& camera,
                                    const int viewportWidth, const int viewportHeight,
                                    const float pixelX, const float pixelY) {
    if (viewportWidth <= 0 || viewportHeight <= 0) {
        return RayUVE{};
    }

    const float aspect = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
    const univex::math::Mat4 inverseViewProjection = camera.InverseViewProjection(aspect);

    // Pixel -> NDC. The Y flip is the whole of the difference between the host's top-left-origin
    // cursor and GL's bottom-left-origin clip space.
    const float ndcX = (pixelX / static_cast<float>(viewportWidth)) * 2.0F - 1.0F;
    const float ndcY = 1.0F - (pixelY / static_cast<float>(viewportHeight)) * 2.0F;

    const auto unproject = [&](const float ndcZ) {
        const univex::math::Vec4 clip =
            inverseViewProjection.Transform(univex::math::Vec4{ndcX, ndcY, ndcZ, 1.0F});
        return univex::math::PerspectiveDivide(clip);
    };

    const univex::math::Vec3 nearPoint = unproject(-1.0F);
    const univex::math::Vec3 farPoint = unproject(1.0F);
    const univex::math::Vec3 direction = univex::math::Normalize(farPoint - nearPoint);

    return RayUVE{ToUveVector3UVE(nearPoint), ToUveVector3UVE(direction)};
}

bool ProjectWorldPointToPixelUVE(const univex::camera::OrbitCamera& camera,
                                 const int viewportWidth, const int viewportHeight,
                                 const UVE::Math::Vector3UVE& worldPoint,
                                 float& outPixelX, float& outPixelY) {
    if (viewportWidth <= 0 || viewportHeight <= 0) {
        return false;
    }

    const float aspect = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
    const univex::math::Mat4 viewProjection = camera.ViewProjection(aspect);

    const univex::math::Vec3 point = FromUveVector3UVE(worldPoint);
    const univex::math::Vec4 clip =
        viewProjection.Transform(univex::math::Vec4{point.x, point.y, point.z, 1.0F});

    // A point behind the camera (or exactly on its plane) has clip.w <= 0; PerspectiveDivide would
    // silently fold that into {0, 0, 0} instead of signaling the degenerate case, so it's checked
    // here first rather than trusted to the shared helper.
    constexpr float kMinimumClipWUVE = 1e-6F;
    if (clip.w <= kMinimumClipWUVE) {
        return false;
    }

    const univex::math::Vec3 ndc = univex::math::PerspectiveDivide(clip);

    // NDC -> pixel, the exact inverse of BuildCursorRayUVE's pixel -> NDC mapping above.
    outPixelX = (ndc.x + 1.0F) * 0.5F * static_cast<float>(viewportWidth);
    outPixelY = (1.0F - ndc.y) * 0.5F * static_cast<float>(viewportHeight);
    return true;
}

EntityPickResultUVE PickEntityAlongRayUVE(UVE::Scene::IEntityManagerUVE& entityManager,
                                          const UVE::Math::RayUVE& worldRay,
                                          const float maxDistance) {
    EntityPickResultUVE best;
    if (!UVE::Math::IsFiniteUVE(worldRay.origin) || !UVE::Math::IsFiniteUVE(worldRay.direction) ||
        !std::isfinite(maxDistance) || maxDistance <= 0.0F) {
        return best;
    }

    entityManager.ForEachUVE<UVE::Scene::WorldTransformComponentUVE, UVE::Scene::PrimitiveMeshComponentUVE>(
        [&](const EntityUVE entity, const UVE::Scene::WorldTransformComponentUVE& worldTransform,
            const UVE::Scene::PrimitiveMeshComponentUVE& primitive) {
            if (worldTransform.dirty || !UVE::Scene::IsPrimitiveMeshComponentValidUVE(primitive)) {
                return;
            }
            if (entityManager.HasComponentUVE<UVE::Scene::EditorInternalEntityComponentUVE>(entity)) {
                return; // the viewport's own hidden camera proxy and headlight are not selectable
            }

            Matrix4x4UVE worldMatrix{};
            if (!TryComputeWorldMatrixUVE(worldTransform, worldMatrix)) {
                return;
            }

            // Broad phase first: the world bounds reject the great majority of a scene for the
            // cost of six comparisons, so the triangle loop only ever runs for what is plausibly
            // under the cursor.
            const AabbUVE worldBounds =
                UVE::Render::GetPrimitiveGeometryUVE(primitive.kind).localBounds.TransformUVE(worldMatrix);
            if (!UVE::Math::IntersectRayUVE(worldRay, worldBounds, maxDistance).has_value()) {
                return;
            }

            const float distance = IntersectPrimitiveUVE(worldRay, worldMatrix, primitive.kind);
            if (distance >= 0.0F && distance <= maxDistance) {
                ConsiderUVE(best, entity, distance);
            }
        });

    return best;
}

EntityPickResultUVE PickEntityAtPixelUVE(UVE::Scene::IEntityManagerUVE& entityManager,
                                         const univex::camera::OrbitCamera& camera,
                                         const int viewportWidth, const int viewportHeight,
                                         const float pixelX, const float pixelY) {
    const RayUVE ray = BuildCursorRayUVE(camera, viewportWidth, viewportHeight, pixelX, pixelY);
    return PickEntityAlongRayUVE(entityManager, ray, camera.FarPlane());
}

} // namespace univex::integration
