#include "integration/entity_picker.h"

#include <cmath>
#include <limits>

#include "integration/math_conversions.h"

#include "univex/camera/orbit_camera.h"
#include "univex/math/mat4.h"
#include "univex/math/vec.h"

#include "mesh_asset.h"
#include "editor_internal_entity_component.h"
#include "mesh_component.h"
#include "primitive_mesh_component.h"
#include "world_transform_component.h"
#include "aabb.h"
#include "matrix4x4.h"
#include "primitive_geometry.h"

namespace univex::integration {

namespace {

using UVE::Math::Aabb;
using UVE::Math::Matrix4x4;
using UVE::Math::Ray;
using UVE::Math::Vector3;
using UVE::Scene::Entity;

/// Möller-Trumbore. Returns the distance along `ray` to the front- or back-facing triangle, or a
/// negative value for a miss. Double-sided on purpose: an editor click must select a plane
/// primitive from underneath as readily as from above, and a "you clicked the back of it" miss
/// would read as the click being ignored.
[[nodiscard]] float IntersectTriangle(const Ray& ray, const Vector3& a, const Vector3& b,
                                         const Vector3& c) noexcept {
    constexpr float kParallelEpsilon = 1e-8F;

    const Vector3 edge1 = b - a;
    const Vector3 edge2 = c - a;
    const Vector3 pvec = UVE::Math::Cross(ray.direction, edge2);
    const float determinant = UVE::Math::Dot(edge1, pvec);
    if (std::fabs(determinant) < kParallelEpsilon) {
        return -1.0F; // ray lies in the triangle's plane
    }

    const float inverseDeterminant = 1.0F / determinant;
    const Vector3 tvec = ray.origin - a;
    const float u = UVE::Math::Dot(tvec, pvec) * inverseDeterminant;
    if (u < 0.0F || u > 1.0F) {
        return -1.0F;
    }

    const Vector3 qvec = UVE::Math::Cross(tvec, edge1);
    const float v = UVE::Math::Dot(ray.direction, qvec) * inverseDeterminant;
    if (v < 0.0F || u + v > 1.0F) {
        return -1.0F;
    }

    return UVE::Math::Dot(edge2, qvec) * inverseDeterminant;
}

/// Applies only the linear (rotation/scale) part of an affine matrix, which is what a direction
/// needs - a direction has no position to translate. UVE::Math exposes TransformPoint but no
/// direction form, and subtracting the transformed origin is exactly the linear part rather than
/// an approximation of it.
[[nodiscard]] Vector3 TransformDirection(const Matrix4x4& matrix,
                                               const Vector3& direction) noexcept {
    const Vector3 transformedOrigin = UVE::Math::TransformPoint(matrix, Vector3{});
    return UVE::Math::TransformPoint(matrix, direction) - transformedOrigin;
}

/// Considers one candidate hit, keeping whichever is nearest along the ray.
void Consider(EntityPickResult& best, const Entity entity, const float distance) noexcept {
    if (!std::isfinite(distance) || distance < 0.0F) {
        return;
    }
    if (!best.hit || distance < best.distance) {
        best = EntityPickResult{entity, distance, true};
    }
}

/// Exact ray/geometry test for one primitive. `worldRay` is transformed into the primitive's own
/// local space rather than transforming its triangles into world space: the local geometry is an
/// immutable shared cache, so moving the ray touches four vectors instead of copying a mesh, and
/// non-uniform scale is handled by the transform itself rather than by rescaling normals.
[[nodiscard]] float IntersectPrimitive(const Ray& worldRay, const Matrix4x4& worldMatrix,
                                          const UVE::Scene::PrimitiveMeshKind kind) {
    Matrix4x4 inverseWorldMatrix{};
    if (!UVE::Math::TryInverse(worldMatrix, inverseWorldMatrix)) {
        return -1.0F; // a degenerate (zero-scaled) object has no volume to click
    }

    const Vector3 localOrigin = UVE::Math::TransformPoint(inverseWorldMatrix, worldRay.origin);
    // Direction, not point: deliberately NOT renormalised. Keeping the local direction as the
    // exact image of the world direction means the parameter t the triangle test returns is
    // already a world-space distance, with no per-axis scale factor to undo afterwards.
    const Vector3 localDirection = TransformDirection(inverseWorldMatrix, worldRay.direction);

    const Ray localRay{localOrigin, localDirection};
    const UVE::Render::PrimitiveGeometry& geometry = UVE::Render::GetPrimitiveGeometry(kind);
    if (geometry.indices.size() < 3U) {
        return -1.0F;
    }

    float nearest = -1.0F;
    for (std::size_t index = 0U; index + 2U < geometry.indices.size(); index += 3U) {
        const Vector3& a = geometry.vertices[geometry.indices[index]].position;
        const Vector3& b = geometry.vertices[geometry.indices[index + 1U]].position;
        const Vector3& c = geometry.vertices[geometry.indices[index + 2U]].position;
        const float distance = IntersectTriangle(localRay, a, b, c);
        if (distance >= 0.0F && (nearest < 0.0F || distance < nearest)) {
            nearest = distance;
        }
    }
    return nearest;
}

} // namespace

bool TryComputeWorldMatrix(const UVE::Scene::WorldTransformComponent& worldTransform,
                              Matrix4x4& outWorldMatrix) noexcept {
    if (!UVE::Math::IsFinite(worldTransform.worldPosition) ||
        !UVE::Math::IsFinite(worldTransform.worldScale) ||
        !UVE::Math::IsFinite(worldTransform.worldRotation)) {
        return false;
    }
    UVE::Math::Quaternion normalizedRotation;
    if (!UVE::Math::TryNormalize(worldTransform.worldRotation, normalizedRotation)) {
        return false;
    }
    outWorldMatrix = Matrix4x4::ComposeTrs(worldTransform.worldPosition, normalizedRotation,
                                                 worldTransform.worldScale);
    return true;
}

UVE::Math::Ray BuildCursorRay(const univex::camera::OrbitCamera& camera,
                                    const int viewportWidth, const int viewportHeight,
                                    const float pixelX, const float pixelY) {
    if (viewportWidth <= 0 || viewportHeight <= 0) {
        return Ray{};
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

    return Ray{ToUveVector3(nearPoint), ToUveVector3(direction)};
}

bool ProjectWorldPointToPixel(const univex::camera::OrbitCamera& camera,
                                 const int viewportWidth, const int viewportHeight,
                                 const UVE::Math::Vector3& worldPoint,
                                 float& outPixelX, float& outPixelY) {
    if (viewportWidth <= 0 || viewportHeight <= 0) {
        return false;
    }

    const float aspect = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
    const univex::math::Mat4 viewProjection = camera.ViewProjection(aspect);

    const univex::math::Vec3 point = FromUveVector3(worldPoint);
    const univex::math::Vec4 clip =
        viewProjection.Transform(univex::math::Vec4{point.x, point.y, point.z, 1.0F});

    // A point behind the camera (or exactly on its plane) has clip.w <= 0; PerspectiveDivide would
    // silently fold that into {0, 0, 0} instead of signaling the degenerate case, so it's checked
    // here first rather than trusted to the shared helper.
    constexpr float kMinimumClipW = 1e-6F;
    if (clip.w <= kMinimumClipW) {
        return false;
    }

    const univex::math::Vec3 ndc = univex::math::PerspectiveDivide(clip);

    // NDC -> pixel, the exact inverse of BuildCursorRay's pixel -> NDC mapping above.
    outPixelX = (ndc.x + 1.0F) * 0.5F * static_cast<float>(viewportWidth);
    outPixelY = (1.0F - ndc.y) * 0.5F * static_cast<float>(viewportHeight);
    return true;
}

EntityPickResult PickEntityAlongRay(UVE::Scene::IEntityManager& entityManager,
                                          const UVE::Math::Ray& worldRay,
                                          const float maxDistance) {
    EntityPickResult best;
    if (!UVE::Math::IsFinite(worldRay.origin) || !UVE::Math::IsFinite(worldRay.direction) ||
        !std::isfinite(maxDistance) || maxDistance <= 0.0F) {
        return best;
    }

    entityManager.ForEach<UVE::Scene::WorldTransformComponent, UVE::Scene::PrimitiveMeshComponent>(
        [&](const Entity entity, const UVE::Scene::WorldTransformComponent& worldTransform,
            const UVE::Scene::PrimitiveMeshComponent& primitive) {
            if (worldTransform.dirty || !UVE::Scene::IsPrimitiveMeshComponentValid(primitive)) {
                return;
            }
            if (entityManager.HasComponent<UVE::Scene::EditorInternalEntityComponent>(entity)) {
                return; // the viewport's own hidden camera proxy and headlight are not selectable
            }

            Matrix4x4 worldMatrix{};
            if (!TryComputeWorldMatrix(worldTransform, worldMatrix)) {
                return;
            }

            // Broad phase first: the world bounds reject the great majority of a scene for the
            // cost of six comparisons, so the triangle loop only ever runs for what is plausibly
            // under the cursor.
            const Aabb worldBounds =
                UVE::Render::GetPrimitiveGeometry(primitive.kind).localBounds.Transform(worldMatrix);
            if (!UVE::Math::IntersectRay(worldRay, worldBounds, maxDistance).has_value()) {
                return;
            }

            const float distance = IntersectPrimitive(worldRay, worldMatrix, primitive.kind);
            if (distance >= 0.0F && distance <= maxDistance) {
                Consider(best, entity, distance);
            }
        });

    return best;
}

EntityPickResult PickEntityAtPixel(UVE::Scene::IEntityManager& entityManager,
                                         const univex::camera::OrbitCamera& camera,
                                         const int viewportWidth, const int viewportHeight,
                                         const float pixelX, const float pixelY) {
    const Ray ray = BuildCursorRay(camera, viewportWidth, viewportHeight, pixelX, pixelY);
    const float maxDistance =
        camera.IsOrthographic() ? 2.0F * camera.OrthographicHalfDepth() : camera.FarPlane();
    return PickEntityAlongRay(entityManager, ray, maxDistance);
}

} // namespace univex::integration
