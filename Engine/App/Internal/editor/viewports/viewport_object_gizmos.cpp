// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "viewport_object_gizmos.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "audio_listener_3d.h"
#include "camera_3d.h"
#include "camera_component.h"
#include "camera_system.h"
#include "collider_component.h"
#include "decal_3d.h"
#include "directional_light_3d.h"
#include "editor_internal_entity_component.h"
#include "fog_volume_3d.h"
#include "gi_probes_3d.h"
#include "i_entity_manager.h"
#include "integration/math_conversions.h"
#include "light_component.h"
#include "marker_3d.h"
#include "nav_mesh_volume_3d.h"
#include "occluder_3d.h"
#include "path_3d.h"
#include "quaternion.h"
#include "ray_cast_3d.h"
#include "reflection_probe_3d.h"
#include "shape_cast_3d.h"
#include "spawn_point_3d.h"
#include "spring_arm_3d.h"
#include "vector3.h"
#include "visibility_component.h"
#include "visibility_region_3d.h"
#include "visible_on_screen_notifier_3d.h"
#include "world_partition_3d.h"
#include "world_transform_component.h"

namespace UVE::App::Viewports {
namespace {

void AppendMeshUVE(univex::gizmo::GizmoMesh& target, const univex::gizmo::GizmoMesh& source) {
    target.lines.insert(target.lines.end(), source.lines.begin(), source.lines.end());
    target.triangles.insert(target.triangles.end(), source.triangles.begin(), source.triangles.end());
}

void AppendLightDirectionGizmoUVE(univex::gizmo::GizmoMesh& mesh, const Scene::LightDirectionGizmoUVE& gizmo) {
    using univex::math::Cross;
    using univex::math::Dot;
    using univex::math::Normalize;
    using univex::math::Vec3;
    const Vec3 origin = univex::integration::FromUveVector3UVE(gizmo.origin);
    const Vec3 direction = Normalize(univex::integration::FromUveVector3UVE(gizmo.direction));
    if (Dot(direction, direction) < 1.0e-8F) {
        return;
    }
    const Vec3 color = univex::integration::FromUveVector3UVE(gizmo.color);
    constexpr float kLength = 2.4F;
    constexpr float kHead = 0.45F;
    constexpr float kRay = 0.9F;
    constexpr float kOffset = 0.18F;
    const Vec3 tip = origin + direction * kLength;
    const Vec3 neck = origin + direction * (kLength - kHead);
    const auto addLine = [&](const Vec3& a, const Vec3& b, const float width) {
        mesh.lines.push_back(univex::gizmo::GizmoLine{a, b, color, width});
    };
    addLine(origin, tip, 2.4F);
    const Vec3 upHint = std::fabs(direction.y) < 0.92F ? Vec3{0.0F, 1.0F, 0.0F} : Vec3{1.0F, 0.0F, 0.0F};
    const Vec3 right = Normalize(Cross(direction, upHint));
    const Vec3 up = Normalize(Cross(right, direction));
    addLine(tip, neck + right * (kHead * 0.45F), 2.0F);
    addLine(tip, neck - right * (kHead * 0.45F), 2.0F);
    addLine(tip, neck + up * (kHead * 0.45F), 2.0F);
    addLine(tip, neck - up * (kHead * 0.45F), 2.0F);
    const Vec3 offsets[4] = {right * kOffset, right * -kOffset, up * kOffset, up * -kOffset};
    for (const Vec3& offset : offsets) {
        const Vec3 start = origin + direction * 0.35F + offset;
        addLine(start, start + direction * kRay, 1.4F);
    }
}

[[nodiscard]] univex::math::Vec3 FogVolumeGizmoPointUVE(const Scene::FogVolume3DGizmoUVE& gizmo,
                                                        const float x,
                                                        const float y,
                                                        const float z) {
    const univex::math::Vec3 origin = univex::integration::FromUveVector3UVE(gizmo.origin);
    const univex::math::Vec3 axisX = univex::integration::FromUveVector3UVE(gizmo.axisX);
    const univex::math::Vec3 axisY = univex::integration::FromUveVector3UVE(gizmo.axisY);
    const univex::math::Vec3 axisZ = univex::integration::FromUveVector3UVE(gizmo.axisZ);
    return origin + axisX * x + axisY * y + axisZ * z;
}

void AppendFogVolumeRingUVE(univex::gizmo::GizmoMesh& mesh,
                            const Scene::FogVolume3DGizmoUVE& gizmo,
                            const int axis,
                            const float along,
                            const float radiusA,
                            const float radiusB,
                            const univex::math::Vec3& color) {
    constexpr int kSegments = 16;
    constexpr float kTwoPi = 6.28318530718F;
    univex::math::Vec3 previous{};
    for (int i = 0; i <= kSegments; ++i) {
        const float angle = kTwoPi * static_cast<float>(i) / static_cast<float>(kSegments);
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        univex::math::Vec3 point;
        if (axis == 0) {
            point = FogVolumeGizmoPointUVE(gizmo, along, c * radiusA, s * radiusB);
        } else if (axis == 1) {
            point = FogVolumeGizmoPointUVE(gizmo, c * radiusA, along, s * radiusB);
        } else {
            point = FogVolumeGizmoPointUVE(gizmo, c * radiusA, s * radiusB, along);
        }
        if (i > 0) {
            mesh.lines.push_back(univex::gizmo::GizmoLine{previous, point, color, 1.5F});
        }
        previous = point;
    }
}

void AppendFogVolumeGizmoUVE(univex::gizmo::GizmoMesh& mesh, const Scene::FogVolume3DGizmoUVE& gizmo) {
    using univex::math::Vec3;
    const Vec3 color = univex::integration::FromUveVector3UVE(gizmo.color);
    const float hx = gizmo.halfExtents.x;
    const float hy = gizmo.halfExtents.y;
    const float hz = gizmo.halfExtents.z;
    const auto addLine = [&](const float ax, const float ay, const float az, const float bx, const float by,
                             const float bz) {
        mesh.lines.push_back(univex::gizmo::GizmoLine{FogVolumeGizmoPointUVE(gizmo, ax, ay, az),
                                                      FogVolumeGizmoPointUVE(gizmo, bx, by, bz), color, 1.6F});
    };
    if (gizmo.shape == Scene::FogVolumeShapeUVE::Ellipsoid) {
        AppendFogVolumeRingUVE(mesh, gizmo, 0, 0.0F, hy, hz, color);
        AppendFogVolumeRingUVE(mesh, gizmo, 1, 0.0F, hx, hz, color);
        AppendFogVolumeRingUVE(mesh, gizmo, 2, 0.0F, hx, hy, color);
        return;
    }
    if (gizmo.shape == Scene::FogVolumeShapeUVE::Cylinder) {
        AppendFogVolumeRingUVE(mesh, gizmo, 1, hy, hx, hz, color);
        AppendFogVolumeRingUVE(mesh, gizmo, 1, -hy, hx, hz, color);
        addLine(hx, -hy, 0.0F, hx, hy, 0.0F);
        addLine(-hx, -hy, 0.0F, -hx, hy, 0.0F);
        addLine(0.0F, -hy, hz, 0.0F, hy, hz);
        addLine(0.0F, -hy, -hz, 0.0F, hy, -hz);
        return;
    }
    if (gizmo.shape == Scene::FogVolumeShapeUVE::Cone) {
        AppendFogVolumeRingUVE(mesh, gizmo, 1, -hy, hx, hz, color);
        addLine(hx, -hy, 0.0F, 0.0F, hy, 0.0F);
        addLine(-hx, -hy, 0.0F, 0.0F, hy, 0.0F);
        addLine(0.0F, -hy, hz, 0.0F, hy, 0.0F);
        addLine(0.0F, -hy, -hz, 0.0F, hy, 0.0F);
        return;
    }
    addLine(-hx, -hy, -hz, hx, -hy, -hz);
    addLine(hx, -hy, -hz, hx, -hy, hz);
    addLine(hx, -hy, hz, -hx, -hy, hz);
    addLine(-hx, -hy, hz, -hx, -hy, -hz);
    addLine(-hx, hy, -hz, hx, hy, -hz);
    addLine(hx, hy, -hz, hx, hy, hz);
    addLine(hx, hy, hz, -hx, hy, hz);
    addLine(-hx, hy, hz, -hx, hy, -hz);
    addLine(-hx, -hy, -hz, -hx, hy, -hz);
    addLine(hx, -hy, -hz, hx, hy, -hz);
    addLine(hx, -hy, hz, hx, hy, hz);
    addLine(-hx, -hy, hz, -hx, hy, hz);
}

[[nodiscard]] univex::math::Vec3 ReflectionProbeGizmoPointUVE(const Scene::ReflectionProbe3DGizmoUVE& gizmo,
                                                              const float x,
                                                              const float y,
                                                              const float z) {
    const univex::math::Vec3 origin = univex::integration::FromUveVector3UVE(gizmo.origin);
    const univex::math::Vec3 axisX = univex::integration::FromUveVector3UVE(gizmo.axisX);
    const univex::math::Vec3 axisY = univex::integration::FromUveVector3UVE(gizmo.axisY);
    const univex::math::Vec3 axisZ = univex::integration::FromUveVector3UVE(gizmo.axisZ);
    return origin + axisX * x + axisY * y + axisZ * z;
}

void AppendReflectionProbeGizmoUVE(univex::gizmo::GizmoMesh& mesh, const Scene::ReflectionProbe3DGizmoUVE& gizmo) {
    using univex::math::Vec3;
    const Vec3 color = univex::integration::FromUveVector3UVE(gizmo.color);
    const float hx = gizmo.halfExtents.x;
    const float hy = gizmo.halfExtents.y;
    const float hz = gizmo.halfExtents.z;
    const auto addLine = [&](const float ax, const float ay, const float az, const float bx, const float by,
                             const float bz, const float width) {
        mesh.lines.push_back(univex::gizmo::GizmoLine{ReflectionProbeGizmoPointUVE(gizmo, ax, ay, az),
                                                      ReflectionProbeGizmoPointUVE(gizmo, bx, by, bz), color, width});
    };
    addLine(-hx, -hy, -hz, hx, -hy, -hz, 1.6F);
    addLine(hx, -hy, -hz, hx, -hy, hz, 1.6F);
    addLine(hx, -hy, hz, -hx, -hy, hz, 1.6F);
    addLine(-hx, -hy, hz, -hx, -hy, -hz, 1.6F);
    addLine(-hx, hy, -hz, hx, hy, -hz, 1.6F);
    addLine(hx, hy, -hz, hx, hy, hz, 1.6F);
    addLine(hx, hy, hz, -hx, hy, hz, 1.6F);
    addLine(-hx, hy, hz, -hx, hy, -hz, 1.6F);
    addLine(-hx, -hy, -hz, -hx, hy, -hz, 1.6F);
    addLine(hx, -hy, -hz, hx, hy, -hz, 1.6F);
    addLine(hx, -hy, hz, hx, hy, hz, 1.6F);
    addLine(-hx, -hy, hz, -hx, hy, hz, 1.6F);
    addLine(-hx, 0.0F, 0.0F, hx, 0.0F, 0.0F, 1.2F);
    addLine(0.0F, -hy, 0.0F, 0.0F, hy, 0.0F, 1.2F);
    addLine(0.0F, 0.0F, -hz, 0.0F, 0.0F, hz, 1.2F);
}

void AppendWorldPartitionGizmoUVE(univex::gizmo::GizmoMesh& mesh, const Scene::WorldPartition3DGizmoUVE& gizmo) {
    using univex::math::Vec3;
    const Vec3 origin = univex::integration::FromUveVector3UVE(gizmo.origin);
    const Vec3 size = univex::integration::FromUveVector3UVE(gizmo.size);
    const Vec3 color = univex::integration::FromUveVector3UVE(gizmo.color);
    const auto add = [&](const Vec3& a, const Vec3& b, const float width) {
        mesh.lines.push_back(univex::gizmo::GizmoLine{a, b, color, width});
    };
    const Vec3 x{size.x, 0.0F, 0.0F};
    const Vec3 y{0.0F, size.y, 0.0F};
    const Vec3 z{0.0F, 0.0F, size.z};
    add(origin, origin + x, 1.6F);
    add(origin + x, origin + x + z, 1.6F);
    add(origin + x + z, origin + z, 1.6F);
    add(origin + z, origin, 1.6F);
    add(origin + y, origin + y + x, 1.6F);
    add(origin + y + x, origin + y + x + z, 1.6F);
    add(origin + y + x + z, origin + y + z, 1.6F);
    add(origin + y + z, origin + y, 1.6F);
    add(origin, origin + y, 1.6F);
    add(origin + x, origin + y + x, 1.6F);
    add(origin + x + z, origin + y + x + z, 1.6F);
    add(origin + z, origin + y + z, 1.6F);

    constexpr std::uint32_t kMaximumInteriorDivisionsUVE = 32U;
    const std::uint32_t countX = gizmo.cellCounts[0U];
    const std::uint32_t countY = gizmo.cellCounts[1U];
    const std::uint32_t countZ = gizmo.cellCounts[2U];
    if (countX > 1U && countX <= kMaximumInteriorDivisionsUVE && size.x > 0.0F) {
        for (std::uint32_t i = 1U; i < countX; ++i) {
            const float t = size.x * (static_cast<float>(i) / static_cast<float>(countX));
            add(origin + Vec3{t, 0.0F, 0.0F}, origin + Vec3{t, 0.0F, size.z}, 1.0F);
            add(origin + Vec3{t, size.y, 0.0F}, origin + Vec3{t, size.y, size.z}, 1.0F);
        }
    }
    if (countZ > 1U && countZ <= kMaximumInteriorDivisionsUVE && size.z > 0.0F) {
        for (std::uint32_t i = 1U; i < countZ; ++i) {
            const float t = size.z * (static_cast<float>(i) / static_cast<float>(countZ));
            add(origin + Vec3{0.0F, 0.0F, t}, origin + Vec3{size.x, 0.0F, t}, 1.0F);
            add(origin + Vec3{0.0F, size.y, t}, origin + Vec3{size.x, size.y, t}, 1.0F);
        }
    }
    if (countY > 1U && countY <= kMaximumInteriorDivisionsUVE && size.y > 0.0F) {
        for (std::uint32_t i = 1U; i < countY; ++i) {
            const float t = size.y * (static_cast<float>(i) / static_cast<float>(countY));
            add(origin + Vec3{0.0F, t, 0.0F}, origin + Vec3{size.x, t, 0.0F}, 1.0F);
            add(origin + Vec3{size.x, t, 0.0F}, origin + Vec3{size.x, t, size.z}, 1.0F);
            add(origin + Vec3{size.x, t, size.z}, origin + Vec3{0.0F, t, size.z}, 1.0F);
            add(origin + Vec3{0.0F, t, size.z}, origin + Vec3{0.0F, t, 0.0F}, 1.0F);
        }
    }
}

void AppendAxisAlignedBoxGizmoUVE(univex::gizmo::GizmoMesh& mesh,
                                  const Math::Vector3UVE& originUve,
                                  const Math::Vector3UVE& halfExtentsUve,
                                  const Math::Vector3UVE& colorUve) {
    using univex::math::Vec3;
    const Vec3 origin = univex::integration::FromUveVector3UVE(originUve);
    const Vec3 color = univex::integration::FromUveVector3UVE(colorUve);
    const float hx = halfExtentsUve.x;
    const float hy = halfExtentsUve.y;
    const float hz = halfExtentsUve.z;
    const auto add = [&](const float ax, const float ay, const float az, const float bx, const float by,
                         const float bz) {
        mesh.lines.push_back(univex::gizmo::GizmoLine{origin + Vec3{ax, ay, az}, origin + Vec3{bx, by, bz},
                                                      color, 1.6F});
    };
    add(-hx, -hy, -hz, hx, -hy, -hz);
    add(hx, -hy, -hz, hx, -hy, hz);
    add(hx, -hy, hz, -hx, -hy, hz);
    add(-hx, -hy, hz, -hx, -hy, -hz);
    add(-hx, hy, -hz, hx, hy, -hz);
    add(hx, hy, -hz, hx, hy, hz);
    add(hx, hy, hz, -hx, hy, hz);
    add(-hx, hy, hz, -hx, hy, -hz);
    add(-hx, -hy, -hz, -hx, hy, -hz);
    add(hx, -hy, -hz, hx, hy, -hz);
    add(hx, -hy, hz, hx, hy, hz);
    add(-hx, -hy, hz, -hx, hy, hz);
}

class ObjectGizmoBuilderUVE final {
public:
    using Vec3 = univex::math::Vec3;

    explicit ObjectGizmoBuilderUVE(univex::gizmo::GizmoMesh& mesh) : m_mesh(mesh) {}

    void Line(const Vec3& a, const Vec3& b, const Vec3& color, const float width = 1.4F) {
        m_mesh.lines.push_back(univex::gizmo::GizmoLine{a, b, color, width});
    }

    [[nodiscard]] static Vec3 Place(const Scene::WorldTransformComponentUVE& world, const Vec3& local) {
        const Math::Vector3UVE scaled{local.x * world.worldScale.x, local.y * world.worldScale.y,
                                      local.z * world.worldScale.z};
        const Math::Vector3UVE rotated = Math::RotateVectorUVE(world.worldRotation, scaled);
        return Vec3{world.worldPosition.x + rotated.x, world.worldPosition.y + rotated.y,
                    world.worldPosition.z + rotated.z};
    }

    [[nodiscard]] static Vec3 Turn(const Scene::WorldTransformComponentUVE& world, const Vec3& local) {
        const Math::Vector3UVE rotated =
            Math::RotateVectorUVE(world.worldRotation, Math::Vector3UVE{local.x, local.y, local.z});
        return Vec3{rotated.x, rotated.y, rotated.z};
    }

    void Box(const Scene::WorldTransformComponentUVE& world,
             const Vec3& centre,
             const Vec3& half,
             const Vec3& color,
             const float width = 1.4F) {
        const auto corner = [&](const float sx, const float sy, const float sz) {
            return Place(world, Vec3{centre.x + sx * half.x, centre.y + sy * half.y, centre.z + sz * half.z});
        };
        const float s[2] = {-1.0F, 1.0F};
        for (const float y : s) {
            for (const float z : s) {
                Line(corner(-1.0F, y, z), corner(1.0F, y, z), color, width);
            }
        }
        for (const float x : s) {
            for (const float z : s) {
                Line(corner(x, -1.0F, z), corner(x, 1.0F, z), color, width);
            }
        }
        for (const float x : s) {
            for (const float y : s) {
                Line(corner(x, y, -1.0F), corner(x, y, 1.0F), color, width);
            }
        }
    }

    void Circle(const Vec3& centre,
                const Vec3& u,
                const Vec3& v,
                const float radius,
                const Vec3& color,
                const float width = 1.4F) {
        constexpr int kSegments = 40;
        Vec3 previous = centre + u * radius;
        for (int i = 1; i <= kSegments; ++i) {
            const float a = static_cast<float>(i) * (6.2831853F / static_cast<float>(kSegments));
            const Vec3 point = centre + u * (std::cos(a) * radius) + v * (std::sin(a) * radius);
            Line(previous, point, color, width);
            previous = point;
        }
    }

    void Sphere(const Vec3& centre, const float radius, const Vec3& color) {
        Circle(centre, Vec3{1.0F, 0.0F, 0.0F}, Vec3{0.0F, 1.0F, 0.0F}, radius, color, 1.2F);
        Circle(centre, Vec3{1.0F, 0.0F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F}, radius, color, 1.2F);
        Circle(centre, Vec3{0.0F, 1.0F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F}, radius, color, 1.2F);
    }

    void Arrow(const Vec3& from, const Vec3& to, const Vec3& color, const float width = 1.6F) {
        Line(from, to, color, width);
        const Vec3 d = to - from;
        const float length = univex::math::Length(d);
        if (length < 1e-4F) {
            return;
        }
        const Vec3 dir = d * (1.0F / length);
        const Vec3 side = std::fabs(dir.y) < 0.9F
                              ? univex::math::Normalize(univex::math::Cross(dir, Vec3{0.0F, 1.0F, 0.0F}))
                              : univex::math::Normalize(univex::math::Cross(dir, Vec3{1.0F, 0.0F, 0.0F}));
        const Vec3 up = univex::math::Cross(side, dir);
        const float head = std::min(0.25F, length * 0.3F);
        const Vec3 base = to - dir * head;
        Line(to, base + side * (head * 0.45F), color, width);
        Line(to, base - side * (head * 0.45F), color, width);
        Line(to, base + up * (head * 0.45F), color, width);
        Line(to, base - up * (head * 0.45F), color, width);
    }

private:
    univex::gizmo::GizmoMesh& m_mesh;
};

[[nodiscard]] bool ShowsObjectGizmoUVE(Scene::IEntityManagerUVE& entityManager, const Scene::EntityUVE entity) {
    if (entityManager.HasComponentUVE<Scene::EditorInternalEntityComponentUVE>(entity)) {
        return false;
    }
    return !entityManager.HasComponentUVE<Scene::VisibilityComponentUVE>(entity) ||
           entityManager.GetComponentUVE<Scene::VisibilityComponentUVE>(entity).visibleInHierarchy;
}

} // namespace

univex::gizmo::GizmoMesh ViewportObjectGizmosUVE::BuildCameraFrustumMeshUVE(Scene::IEntityManagerUVE& entityManager,
                                                                            const Scene::EntityUVE hideEntity,
                                                                            const float aspectRatio) {
    univex::gizmo::GizmoMesh mesh;
    if (!(std::isfinite(aspectRatio) && aspectRatio > 0.0F)) {
        return mesh;
    }
    Render::CameraSystemUVE cameras;
    entityManager.ForEachUVE<Scene::WorldTransformComponentUVE, Scene::CameraComponentUVE>(
        [&](const Scene::EntityUVE entity, const Scene::WorldTransformComponentUVE& worldTransform,
            const Scene::CameraComponentUVE& camera) {
            if (entity == hideEntity || !Scene::IsDocumentCameraEntityUVE(entityManager, entity) ||
                !Scene::IsCameraComponentValidUVE(camera)) {
                return;
            }
            const Render::CameraFrustumCornersUVE corners =
                cameras.ComputeFrustumCornersUVE(entityManager, entity, aspectRatio);
            constexpr float kEditorFrustumLength = 2.4F;
            const float visualFarScale =
                std::min(1.0F, kEditorFrustumLength / std::max(camera.farPlane, camera.nearPlane));
            const univex::math::Vec3 origin{worldTransform.worldPosition.x, worldTransform.worldPosition.y,
                                            worldTransform.worldPosition.z};
            const univex::math::Vec3 color =
                camera.current ? univex::math::Vec3{0.55F, 0.92F, 1.0F} : univex::math::Vec3{0.35F, 0.72F, 0.95F};
            const float widthPx = camera.current ? 2.0F : 1.5F;
            const auto addLine = [&](const int a, const int b) {
                const auto visualPoint = [&](const int index) {
                    const univex::math::Vec3 point{corners[static_cast<std::size_t>(index)].x,
                                                   corners[static_cast<std::size_t>(index)].y,
                                                   corners[static_cast<std::size_t>(index)].z};
                    return index < 4 ? point : origin + (point - origin) * visualFarScale;
                };
                mesh.lines.push_back(univex::gizmo::GizmoLine{visualPoint(a), visualPoint(b), color, widthPx});
            };
            const auto rotate = [&](const Math::Vector3UVE local) {
                const Math::Vector3UVE world = Math::RotateVectorUVE(worldTransform.worldRotation, local);
                return univex::math::Vec3{world.x, world.y, world.z};
            };
            const univex::math::Vec3 right = rotate({1.0F, 0.0F, 0.0F});
            const univex::math::Vec3 up = rotate({0.0F, 1.0F, 0.0F});
            const univex::math::Vec3 forward = rotate({0.0F, 0.0F, -1.0F});
            const univex::math::Vec3 iconColor{1.0F, 0.72F, 0.22F};
            const float bodyWidth = 0.20F;
            const float bodyHeight = 0.14F;
            const float bodyDepth = 0.10F;
            const univex::math::Vec3 bodyBack = origin - forward * bodyDepth;
            const univex::math::Vec3 bodyFront = origin + forward * bodyDepth;
            const univex::math::Vec3 bodyCorners[8] = {
                bodyBack - right * bodyWidth - up * bodyHeight,
                bodyBack + right * bodyWidth - up * bodyHeight,
                bodyBack - right * bodyWidth + up * bodyHeight,
                bodyBack + right * bodyWidth + up * bodyHeight,
                bodyFront - right * bodyWidth - up * bodyHeight,
                bodyFront + right * bodyWidth - up * bodyHeight,
                bodyFront - right * bodyWidth + up * bodyHeight,
                bodyFront + right * bodyWidth + up * bodyHeight,
            };
            const auto addIconLine = [&](const univex::math::Vec3& a, const univex::math::Vec3& b) {
                mesh.lines.push_back(univex::gizmo::GizmoLine{a, b, iconColor, 2.0F});
            };
            for (const std::array<int, 2> edge : {std::array<int, 2>{0, 1}, std::array<int, 2>{1, 3},
                                                   std::array<int, 2>{3, 2}, std::array<int, 2>{2, 0},
                                                   std::array<int, 2>{4, 5}, std::array<int, 2>{5, 7},
                                                   std::array<int, 2>{7, 6}, std::array<int, 2>{6, 4},
                                                   std::array<int, 2>{0, 4}, std::array<int, 2>{1, 5},
                                                   std::array<int, 2>{2, 6}, std::array<int, 2>{3, 7}}) {
                addIconLine(bodyCorners[edge[0]], bodyCorners[edge[1]]);
            }
            const univex::math::Vec3 lensCenter = bodyFront + forward * 0.045F;
            const univex::math::Vec3 lensCorners[4] = {
                lensCenter - right * 0.08F - up * 0.055F,
                lensCenter + right * 0.08F - up * 0.055F,
                lensCenter + right * 0.08F + up * 0.055F,
                lensCenter - right * 0.08F + up * 0.055F,
            };
            addIconLine(lensCorners[0], lensCorners[1]);
            addIconLine(lensCorners[1], lensCorners[2]);
            addIconLine(lensCorners[2], lensCorners[3]);
            addIconLine(lensCorners[3], lensCorners[0]);
            addLine(0, 1);
            addLine(1, 3);
            addLine(3, 2);
            addLine(2, 0);
            addLine(4, 5);
            addLine(5, 7);
            addLine(7, 6);
            addLine(6, 4);
            addLine(0, 4);
            addLine(1, 5);
            addLine(2, 6);
            addLine(3, 7);
        });
    return mesh;
}

univex::gizmo::GizmoMesh ViewportObjectGizmosUVE::BuildLightDirectionMeshUVE(Scene::IEntityManagerUVE& entityManager) {
    univex::gizmo::GizmoMesh mesh;
    std::vector<Scene::LightDirectionGizmoUVE> gizmos;
    Scene::CollectLightDirectionGizmosUVE(entityManager, gizmos);
    for (const Scene::LightDirectionGizmoUVE& gizmo : gizmos) {
        AppendLightDirectionGizmoUVE(mesh, gizmo);
    }
    return mesh;
}

univex::gizmo::GizmoMesh ViewportObjectGizmosUVE::BuildFogVolumeMeshUVE(Scene::IEntityManagerUVE& entityManager) {
    univex::gizmo::GizmoMesh mesh;
    std::vector<Scene::FogVolume3DGizmoUVE> gizmos;
    Scene::CollectFogVolume3DGizmosUVE(entityManager, gizmos);
    for (const Scene::FogVolume3DGizmoUVE& gizmo : gizmos) {
        AppendFogVolumeGizmoUVE(mesh, gizmo);
    }
    return mesh;
}

univex::gizmo::GizmoMesh ViewportObjectGizmosUVE::BuildReflectionProbeMeshUVE(Scene::IEntityManagerUVE& entityManager) {
    univex::gizmo::GizmoMesh mesh;
    std::vector<Scene::ReflectionProbe3DGizmoUVE> gizmos;
    Scene::CollectReflectionProbe3DGizmosUVE(entityManager, gizmos);
    for (const Scene::ReflectionProbe3DGizmoUVE& gizmo : gizmos) {
        AppendReflectionProbeGizmoUVE(mesh, gizmo);
    }
    return mesh;
}

univex::gizmo::GizmoMesh ViewportObjectGizmosUVE::BuildWorldPartitionMeshUVE(Scene::IEntityManagerUVE& entityManager) {
    univex::gizmo::GizmoMesh mesh;
    std::vector<Scene::WorldPartition3DGizmoUVE> gizmos;
    Scene::CollectWorldPartition3DGizmosUVE(entityManager, gizmos);
    for (const Scene::WorldPartition3DGizmoUVE& gizmo : gizmos) {
        AppendWorldPartitionGizmoUVE(mesh, gizmo);
    }
    return mesh;
}

univex::gizmo::GizmoMesh ViewportObjectGizmosUVE::BuildVisibilityRegionMeshUVE(
    Scene::IEntityManagerUVE& entityManager) {
    univex::gizmo::GizmoMesh mesh;
    std::vector<Scene::VisibilityRegion3DGizmoUVE> gizmos;
    Scene::CollectVisibilityRegion3DGizmosUVE(entityManager, gizmos);
    for (const Scene::VisibilityRegion3DGizmoUVE& gizmo : gizmos) {
        AppendAxisAlignedBoxGizmoUVE(mesh, gizmo.origin, gizmo.halfExtents, gizmo.color);
    }
    return mesh;
}

univex::gizmo::GizmoMesh ViewportObjectGizmosUVE::BuildOccluderMeshUVE(Scene::IEntityManagerUVE& entityManager) {
    univex::gizmo::GizmoMesh mesh;
    std::vector<Scene::Occluder3DGizmoUVE> gizmos;
    Scene::CollectOccluder3DGizmosUVE(entityManager, gizmos);
    for (const Scene::Occluder3DGizmoUVE& gizmo : gizmos) {
        AppendAxisAlignedBoxGizmoUVE(mesh, gizmo.origin, gizmo.halfExtents, gizmo.color);
    }
    return mesh;
}

univex::gizmo::GizmoMesh ViewportObjectGizmosUVE::BuildColliderMeshUVE(Scene::IEntityManagerUVE& entityManager) {
    univex::gizmo::GizmoMesh mesh;
    entityManager.ForEachUVE<Scene::WorldTransformComponentUVE, Scene::ColliderComponentUVE>(
        [&](const Scene::EntityUVE, const Scene::WorldTransformComponentUVE& worldTransform,
            const Scene::ColliderComponentUVE& collider) {
            const univex::math::Vec3 color =
                collider.disabled ? univex::math::Vec3{0.5F, 0.5F, 0.5F} : univex::math::Vec3{0.35F, 0.9F, 0.35F};
            constexpr float kLineWidth = 1.5F;

            const auto toWorld = [&](const float lx, const float ly, const float lz) -> univex::math::Vec3 {
                const Math::Vector3UVE w = Scene::TransformWorldPointUVE(worldTransform, {lx, ly, lz});
                return univex::integration::FromUveVector3UVE(w);
            };
            const auto addEdge = [&](const univex::math::Vec3& p1, const univex::math::Vec3& p2) {
                mesh.lines.push_back(univex::gizmo::GizmoLine{p1, p2, color, kLineWidth});
            };
            constexpr float kPi = 3.14159265358979323846F;
            constexpr int kRingSegments = 24;
            const auto addRing = [&](const float yCenter, const float ringRadius) {
                if (ringRadius <= 1e-4F) {
                    return;
                }
                univex::math::Vec3 prev = toWorld(ringRadius, yCenter, 0.0F);
                for (int i = 1; i <= kRingSegments; ++i) {
                    const float theta = static_cast<float>(i) * (2.0F * kPi / static_cast<float>(kRingSegments));
                    const univex::math::Vec3 curr =
                        toWorld(std::cos(theta) * ringRadius, yCenter, std::sin(theta) * ringRadius);
                    addEdge(prev, curr);
                    prev = curr;
                }
            };

            switch (collider.shapeType) {
                case Scene::ColliderShapeTypeUVE::Box: {
                    const float hx = collider.halfExtents.x;
                    const float hy = collider.halfExtents.y;
                    const float hz = collider.halfExtents.z;
                    const univex::math::Vec3 c[8] = {
                        toWorld(-hx, -hy, -hz), toWorld(hx, -hy, -hz), toWorld(hx, -hy, hz), toWorld(-hx, -hy, hz),
                        toWorld(-hx, hy, -hz),  toWorld(hx, hy, -hz),  toWorld(hx, hy, hz),  toWorld(-hx, hy, hz),
                    };
                    addEdge(c[0], c[1]);
                    addEdge(c[1], c[2]);
                    addEdge(c[2], c[3]);
                    addEdge(c[3], c[0]);
                    addEdge(c[4], c[5]);
                    addEdge(c[5], c[6]);
                    addEdge(c[6], c[7]);
                    addEdge(c[7], c[4]);
                    addEdge(c[0], c[4]);
                    addEdge(c[1], c[5]);
                    addEdge(c[2], c[6]);
                    addEdge(c[3], c[7]);
                    break;
                }
                case Scene::ColliderShapeTypeUVE::Sphere: {
                    const float r = std::max(0.001F, collider.radius);
                    addRing(0.0F, r);
                    const float angles[2] = {kPi / 6.0F, kPi / 3.0F};
                    for (const float a : angles) {
                        const float y = r * std::sin(a);
                        const float subR = r * std::cos(a);
                        addRing(y, subR);
                        addRing(-y, subR);
                    }
                    constexpr int kMeridians = 4;
                    for (int m = 0; m < kMeridians; ++m) {
                        const float phi = static_cast<float>(m) * (kPi / static_cast<float>(kMeridians));
                        const float cosPhi = std::cos(phi);
                        const float sinPhi = std::sin(phi);
                        univex::math::Vec3 prev = toWorld(r * cosPhi, 0.0F, r * sinPhi);
                        for (int i = 1; i <= kRingSegments; ++i) {
                            const float theta =
                                static_cast<float>(i) * (2.0F * kPi / static_cast<float>(kRingSegments));
                            const float x = r * std::cos(theta) * cosPhi;
                            const float y = r * std::sin(theta);
                            const float z = r * std::cos(theta) * sinPhi;
                            const univex::math::Vec3 curr = toWorld(x, y, z);
                            addEdge(prev, curr);
                            prev = curr;
                        }
                    }
                    break;
                }
                case Scene::ColliderShapeTypeUVE::Capsule: {
                    const float r = std::max(0.001F, collider.radius);
                    const float h = std::max(0.0F, collider.height);
                    const float cylH = std::max(0.0F, h - 2.0F * r);
                    const float halfH = cylH * 0.5F;

                    if (halfH > 1e-4F) {
                        addRing(0.0F, r);
                    }
                    addRing(halfH, r);
                    addRing(-halfH, r);
                    const float latAngles[2] = {kPi / 6.0F, kPi / 3.0F};
                    for (const float a : latAngles) {
                        const float dy = r * std::sin(a);
                        const float domeR = r * std::cos(a);
                        addRing(halfH + dy, domeR);
                        addRing(-halfH - dy, domeR);
                    }

                    constexpr int kMeridianCount = 8;
                    constexpr int kDomeSteps = 8;
                    for (int m = 0; m < kMeridianCount; ++m) {
                        const float phi = static_cast<float>(m) * (2.0F * kPi / static_cast<float>(kMeridianCount));
                        const float cosPhi = std::cos(phi);
                        const float sinPhi = std::sin(phi);

                        if (halfH > 1e-4F) {
                            addEdge(toWorld(r * cosPhi, -halfH, r * sinPhi), toWorld(r * cosPhi, halfH, r * sinPhi));
                        }

                        univex::math::Vec3 prevTop = toWorld(r * cosPhi, halfH, r * sinPhi);
                        for (int s = 1; s <= kDomeSteps; ++s) {
                            const float alpha = static_cast<float>(s) * (0.5F * kPi / static_cast<float>(kDomeSteps));
                            const float x = r * std::cos(alpha) * cosPhi;
                            const float y = halfH + r * std::sin(alpha);
                            const float z = r * std::cos(alpha) * sinPhi;
                            const univex::math::Vec3 curr = toWorld(x, y, z);
                            addEdge(prevTop, curr);
                            prevTop = curr;
                        }

                        univex::math::Vec3 prevBottom = toWorld(r * cosPhi, -halfH, r * sinPhi);
                        for (int s = 1; s <= kDomeSteps; ++s) {
                            const float alpha = static_cast<float>(s) * (0.5F * kPi / static_cast<float>(kDomeSteps));
                            const float x = r * std::cos(alpha) * cosPhi;
                            const float y = -halfH - r * std::sin(alpha);
                            const float z = r * std::cos(alpha) * sinPhi;
                            const univex::math::Vec3 curr = toWorld(x, y, z);
                            addEdge(prevBottom, curr);
                            prevBottom = curr;
                        }
                    }
                    break;
                }
                case Scene::ColliderShapeTypeUVE::Cylinder: {
                    const float r = std::max(0.001F, collider.radius);
                    const float halfH = std::max(0.001F, collider.height * 0.5F);
                    addRing(halfH, r);
                    addRing(-halfH, r);
                    addRing(0.0F, r);
                    constexpr int kSideLines = 8;
                    for (int s = 0; s < kSideLines; ++s) {
                        const float phi = static_cast<float>(s) * (2.0F * kPi / static_cast<float>(kSideLines));
                        const float x = r * std::cos(phi);
                        const float z = r * std::sin(phi);
                        addEdge(toWorld(x, -halfH, z), toWorld(x, halfH, z));
                    }
                    break;
                }
            }
        });
    return mesh;
}

univex::gizmo::GizmoMesh ViewportObjectGizmosUVE::BuildSpatialObjectGizmosMeshUVE(
    Scene::IEntityManagerUVE& entityManager,
    const bool includeSpotCones) {
    using univex::math::Vec3;
    using WorldUVE = Scene::WorldTransformComponentUVE;
    univex::gizmo::GizmoMesh mesh;
    ObjectGizmoBuilderUVE g(mesh);
    const Vec3 zero{0.0F, 0.0F, 0.0F};
    const auto origin = [](const WorldUVE& w) { return Vec3{w.worldPosition.x, w.worldPosition.y, w.worldPosition.z}; };

    // Point lights: range spheres; spot lights: cones out to range (when not drawn by the host).
    entityManager.ForEachUVE<WorldUVE, Scene::LightComponentUVE>(
        [&](const Scene::EntityUVE e, const WorldUVE& w, const Scene::LightComponentUVE& light) {
            if (!ShowsObjectGizmoUVE(entityManager, e) || light.type == Scene::LightTypeUVE::Directional ||
                !(light.range > 0.0F) || !std::isfinite(light.range)) {
                return;
            }
            const Vec3 color{0.5F + 0.5F * std::clamp(light.color.r, 0.0F, 1.0F),
                             0.5F + 0.5F * std::clamp(light.color.g, 0.0F, 1.0F),
                             0.5F + 0.5F * std::clamp(light.color.b, 0.0F, 1.0F)};
            if (light.type == Scene::LightTypeUVE::Point) {
                g.Sphere(origin(w), light.range, color);
                return;
            }
            if (!includeSpotCones) {
                return;
            }
            const Vec3 apex = origin(w);
            const Vec3 forward = ObjectGizmoBuilderUVE::Turn(w, Vec3{0.0F, 0.0F, -1.0F});
            const Vec3 right = ObjectGizmoBuilderUVE::Turn(w, Vec3{1.0F, 0.0F, 0.0F});
            const Vec3 up = ObjectGizmoBuilderUVE::Turn(w, Vec3{0.0F, 1.0F, 0.0F});
            const float angle = std::clamp(light.spotAngleDegrees, 0.5F, 89.5F) * 0.0174532925F;
            const float radius = light.range * std::tan(angle);
            const Vec3 end = apex + forward * light.range;
            g.Circle(end, right, up, radius, color);
            g.Line(apex, end + right * radius, color);
            g.Line(apex, end - right * radius, color);
            g.Line(apex, end + up * radius, color);
            g.Line(apex, end - up * radius, color);
        });

    // Decals: projection box and -Y projection arrow.
    entityManager.ForEachUVE<WorldUVE, Scene::Decal3DComponentUVE>(
        [&](const Scene::EntityUVE e, const WorldUVE& w, const Scene::Decal3DComponentUVE& decal) {
            if (!ShowsObjectGizmoUVE(entityManager, e)) {
                return;
            }
            const Vec3 color{0.95F, 0.78F, 0.40F};
            const Vec3 half{decal.size.x * 0.5F, decal.size.y * 0.5F, decal.size.z * 0.5F};
            g.Box(w, zero, half, color);
            g.Arrow(ObjectGizmoBuilderUVE::Place(w, zero), ObjectGizmoBuilderUVE::Place(w, Vec3{0.0F, -half.y, 0.0F}),
                    color);
        });

    // Global illumination volumes, navigation mesh bounds, and on-screen visibility notifiers.
    entityManager.ForEachUVE<WorldUVE, Scene::VoxelGI3DComponentUVE>(
        [&](const Scene::EntityUVE e, const WorldUVE& w, const Scene::VoxelGI3DComponentUVE& gi) {
            if (ShowsObjectGizmoUVE(entityManager, e)) {
                g.Box(w, zero, Vec3{gi.halfExtents.x, gi.halfExtents.y, gi.halfExtents.z}, Vec3{0.55F, 0.85F, 1.0F});
            }
        });
    entityManager.ForEachUVE<WorldUVE, Scene::LightmapGI3DComponentUVE>(
        [&](const Scene::EntityUVE e, const WorldUVE& w, const Scene::LightmapGI3DComponentUVE& gi) {
            if (ShowsObjectGizmoUVE(entityManager, e)) {
                g.Box(w, zero, Vec3{gi.halfExtents.x, gi.halfExtents.y, gi.halfExtents.z}, Vec3{1.0F, 0.85F, 0.45F});
            }
        });
    entityManager.ForEachUVE<WorldUVE, Scene::NavMeshVolume3DComponentUVE>(
        [&](const Scene::EntityUVE e, const WorldUVE& w, const Scene::NavMeshVolume3DComponentUVE& nav) {
            if (ShowsObjectGizmoUVE(entityManager, e)) {
                g.Box(w, zero, Vec3{nav.boundsHalfExtents.x, nav.boundsHalfExtents.y, nav.boundsHalfExtents.z},
                      Vec3{0.35F, 0.90F, 0.50F});
            }
        });
    entityManager.ForEachUVE<WorldUVE, Scene::VisibleOnScreenNotifier3DComponentUVE>(
        [&](const Scene::EntityUVE e, const WorldUVE& w, const Scene::VisibleOnScreenNotifier3DComponentUVE& n) {
            if (ShowsObjectGizmoUVE(entityManager, e)) {
                g.Box(w, Vec3{n.aabbCenterOffset.x, n.aabbCenterOffset.y, n.aabbCenterOffset.z},
                      Vec3{n.aabbHalfExtents.x, n.aabbHalfExtents.y, n.aabbHalfExtents.z}, Vec3{0.95F, 0.60F, 0.95F});
            }
        });

    // Ray and shape casts.
    entityManager.ForEachUVE<WorldUVE, Scene::RayCast3DComponentUVE>(
        [&](const Scene::EntityUVE e, const WorldUVE& w, const Scene::RayCast3DComponentUVE& ray) {
            if (!ShowsObjectGizmoUVE(entityManager, e) || !(ray.length > 0.0F) || !std::isfinite(ray.length)) {
                return;
            }
            const Vec3 dir = univex::math::Normalize(
                ObjectGizmoBuilderUVE::Turn(w, Vec3{ray.direction.x, ray.direction.y, ray.direction.z}));
            g.Arrow(origin(w), origin(w) + dir * ray.length,
                    ray.hit ? Vec3{1.0F, 0.35F, 0.30F} : Vec3{1.0F, 0.62F, 0.22F});
        });
    entityManager.ForEachUVE<WorldUVE, Scene::ShapeCast3DComponentUVE>(
        [&](const Scene::EntityUVE e, const WorldUVE& w, const Scene::ShapeCast3DComponentUVE& cast) {
            if (!ShowsObjectGizmoUVE(entityManager, e) || !(cast.maxDistance > 0.0F) ||
                !std::isfinite(cast.maxDistance)) {
                return;
            }
            const Vec3 color = cast.hit ? Vec3{1.0F, 0.35F, 0.30F} : Vec3{1.0F, 0.62F, 0.22F};
            const Vec3 dir = univex::math::Normalize(
                ObjectGizmoBuilderUVE::Turn(w, Vec3{cast.direction.x, cast.direction.y, cast.direction.z}));
            const float reach = cast.hit ? cast.hitDistance : cast.maxDistance;
            g.Line(origin(w), origin(w) + dir * reach, color);
            g.Sphere(origin(w), std::max(0.01F, cast.radius), color);
            g.Sphere(origin(w) + dir * reach, std::max(0.01F, cast.radius), color);
        });

    // Spring arms extend along local +Z.
    entityManager.ForEachUVE<WorldUVE, Scene::SpringArm3DComponentUVE>(
        [&](const Scene::EntityUVE e, const WorldUVE& w, const Scene::SpringArm3DComponentUVE& arm) {
            if (!ShowsObjectGizmoUVE(entityManager, e) || !(arm.armLength > 0.0F) || !std::isfinite(arm.armLength)) {
                return;
            }
            const Vec3 dir = ObjectGizmoBuilderUVE::Turn(w, Vec3{0.0F, 0.0F, 1.0F});
            const float current = std::clamp(arm.currentLength, 0.0F, arm.armLength);
            g.Line(origin(w), origin(w) + dir * current, Vec3{0.92F, 0.90F, 0.40F}, 2.0F);
            g.Line(origin(w) + dir * current, origin(w) + dir * arm.armLength, Vec3{0.55F, 0.54F, 0.30F}, 1.2F);
            g.Sphere(origin(w) + dir * current, 0.08F, Vec3{0.92F, 0.90F, 0.40F});
        });

    // Markers, spawn points, and audio listeners.
    entityManager.ForEachUVE<WorldUVE, Scene::Marker3DComponentUVE>(
        [&](const Scene::EntityUVE e, const WorldUVE& w, const Scene::Marker3DComponentUVE&) {
            if (!ShowsObjectGizmoUVE(entityManager, e)) {
                return;
            }
            constexpr float kSize = 0.5F;
            g.Line(ObjectGizmoBuilderUVE::Place(w, Vec3{-kSize, 0.0F, 0.0F}),
                   ObjectGizmoBuilderUVE::Place(w, Vec3{kSize, 0.0F, 0.0F}), Vec3{0.90F, 0.32F, 0.32F});
            g.Line(ObjectGizmoBuilderUVE::Place(w, Vec3{0.0F, -kSize, 0.0F}),
                   ObjectGizmoBuilderUVE::Place(w, Vec3{0.0F, kSize, 0.0F}), Vec3{0.45F, 0.80F, 0.30F});
            g.Line(ObjectGizmoBuilderUVE::Place(w, Vec3{0.0F, 0.0F, -kSize}),
                   ObjectGizmoBuilderUVE::Place(w, Vec3{0.0F, 0.0F, kSize}), Vec3{0.30F, 0.55F, 0.92F});
        });
    entityManager.ForEachUVE<WorldUVE, Scene::SpawnPoint3DComponentUVE>(
        [&](const Scene::EntityUVE e, const WorldUVE& w, const Scene::SpawnPoint3DComponentUVE&) {
            if (!ShowsObjectGizmoUVE(entityManager, e)) {
                return;
            }
            const Vec3 color{0.40F, 0.80F, 1.0F};
            g.Circle(origin(w), ObjectGizmoBuilderUVE::Turn(w, Vec3{1.0F, 0.0F, 0.0F}),
                     ObjectGizmoBuilderUVE::Turn(w, Vec3{0.0F, 0.0F, 1.0F}), 0.5F, color);
            g.Arrow(origin(w), origin(w) + ObjectGizmoBuilderUVE::Turn(w, Vec3{0.0F, 0.0F, -1.0F}) * 0.9F, color);
        });
    entityManager.ForEachUVE<WorldUVE, Scene::AudioListener3DComponentUVE>(
        [&](const Scene::EntityUVE e, const WorldUVE& w, const Scene::AudioListener3DComponentUVE&) {
            if (!ShowsObjectGizmoUVE(entityManager, e)) {
                return;
            }
            const Vec3 color{0.82F, 0.82F, 0.86F};
            g.Circle(origin(w), ObjectGizmoBuilderUVE::Turn(w, Vec3{1.0F, 0.0F, 0.0F}),
                     ObjectGizmoBuilderUVE::Turn(w, Vec3{0.0F, 1.0F, 0.0F}), 0.35F, color);
            g.Arrow(origin(w), origin(w) + ObjectGizmoBuilderUVE::Turn(w, Vec3{0.0F, 0.0F, -1.0F}) * 0.7F, color);
        });

    // Path3D curves.
    entityManager.ForEachUVE<WorldUVE, Scene::Path3DComponentUVE>(
        [&](const Scene::EntityUVE e, const WorldUVE& w, const Scene::Path3DComponentUVE& path) {
            if (!ShowsObjectGizmoUVE(entityManager, e) || path.points.size() < 2U) {
                return;
            }
            const Vec3 color{0.42F, 0.72F, 1.0F};
            const std::size_t count = std::min(path.points.size(), Scene::kMaximumPath3DPointsUVE);
            const auto at = [&](const std::size_t i) {
                const Math::Vector3UVE& p = path.points[i];
                return ObjectGizmoBuilderUVE::Place(w, Vec3{p.x, p.y, p.z});
            };
            for (std::size_t i = 1U; i < count; ++i) {
                g.Line(at(i - 1U), at(i), color, 2.0F);
            }
            if (path.closed && count > 2U) {
                g.Line(at(count - 1U), at(0U), color, 2.0F);
            }
            for (std::size_t i = 0U; i < count; ++i) {
                g.Sphere(at(i), 0.06F, color);
            }
        });
    return mesh;
}

ViewportObjectGizmoBuildResultUVE ViewportObjectGizmosUVE::BuildAllObjectGizmosUVE(
    Scene::IEntityManagerUVE& entityManager,
    const Scene::EntityUVE hideCameraEntity,
    const float aspectRatio,
    const bool hostDrawsPrimaryGlyphs) {
    ViewportObjectGizmoBuildResultUVE result{};
    if (!hostDrawsPrimaryGlyphs) {
        result.mesh = BuildCameraFrustumMeshUVE(entityManager, hideCameraEntity, aspectRatio);
        AppendMeshUVE(result.mesh, BuildLightDirectionMeshUVE(entityManager));
    }
    AppendMeshUVE(result.mesh, BuildFogVolumeMeshUVE(entityManager));
    AppendMeshUVE(result.mesh, BuildReflectionProbeMeshUVE(entityManager));
    AppendMeshUVE(result.mesh, BuildWorldPartitionMeshUVE(entityManager));
    AppendMeshUVE(result.mesh, BuildVisibilityRegionMeshUVE(entityManager));
    AppendMeshUVE(result.mesh, BuildOccluderMeshUVE(entityManager));
    AppendMeshUVE(result.mesh, BuildSpatialObjectGizmosMeshUVE(entityManager, !hostDrawsPrimaryGlyphs));
    result.colliderLinesStart = result.mesh.lines.size();
    AppendMeshUVE(result.mesh, BuildColliderMeshUVE(entityManager));
    return result;
}

} // namespace UVE::App::Viewports
