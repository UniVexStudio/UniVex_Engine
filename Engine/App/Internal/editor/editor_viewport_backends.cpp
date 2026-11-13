// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// GL/glew.h (pulled in via GlApi.h) must be included before anything that might transitively pull
// <GLFW/glfw3.h>, or GLFW's own bundled GL header conflicts with GLEW's - see GlApi.h's own
// comment. Nothing else in this file currently drags in GLFW, but this ordering is cheap
// insurance and matches every other translation unit in Engine/Editor/Viewport that mixes the two.
#include "univex/render/gl_api.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "viewport_render_pass.h"
#include "integration/editor_mesh_layer.h"
#include "integration/selection_outline_geometry.h"
#include "integration/entity_picker.h"
#include "univex/camera/viewport_metrics.h"
#include "univex/gizmo/gizmo_drag.h"
#include "univex/gizmo/gizmo_picking.h"
#include "integration/math_conversions.h"
#include "univex/camera/orbit_camera.h"
#include "univex/render/shader_program.h"
#include "univex/render/studio_backdrop_renderer.h"

#include "engine_core.h"
#include "gl_render_device.h"
#include "logging_macros.h"
#include "editor_bridge_stdio.h"
#include "ui_draw_batch.h"
#include "ui_font_atlas.h"
#include "editor.h"
#include "vector2.h"
#include "camera_component.h"
#include "collider_component.h"
#include "editor_internal_entity_component.h"
#include "light_emitter_component.h"
#include "vector3.h"
#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "quaternion.h"
#include "camera_3d.h"
#include "directional_light_3d.h"
#include "preview_lighting.h"
#include "player_3d.h"
#include "fog_volume_3d.h"
#include "occluder_3d.h"
#include "reflection_probe_3d.h"
#include "sky_3d.h"
#include "visibility_region_3d.h"
#include "world_environment_3d.h"
#include "world_partition_3d.h"
#include "audio_listener_3d.h"
#include "decal_3d.h"
#include "gi_probes_3d.h"
#include "light_component.h"
#include "marker_3d.h"
#include "nav_mesh_volume_3d.h"
#include "path_3d.h"
#include "ray_cast_3d.h"
#include "shape_cast_3d.h"
#include "spawn_point_3d.h"
#include "spring_arm_3d.h"
#include "visibility_component.h"
#include "visible_on_screen_notifier_3d.h"
#include "camera_system.h"
#include "i_scene_graph.h"
#include "univex/gizmo/gizmo_geometry.h"

#include "editor_viewport_backends.h"
#include "editor_viewport_2d_backend.h"

namespace {

// How far the pointer may travel on the nav gizmo (accumulated since the button went down, in
// ImGui points) and still count as a click rather than a drag - mirrors app/main.cpp's own
// kNavClickSlopPixels for the standalone demo.
constexpr float kNavClickSlopPixelsUVE = 4.0F;

// Fullscreen-triangle pass that injects EditorMeshLayerUVE's real mesh/material render into the
// viewport's own framebuffer, colour AND depth, so that the editor's grid and gizmos share one
// depth buffer with the engine's scene geometry instead of being reconciled against it afterwards.
// No vertex buffer needed (same gl_VertexID trick already used by Renderer3DUVE's own internal
// tonemap/fullscreen-quad shader).
constexpr std::string_view kMeshBlitVertexShaderUVE = R"(#version 330 core
void main() {
    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Coverage comes from alpha, not depth: Renderer3DUVE's tone-mapping pass reports which pixels it
// actually drew by writing alpha 1 there and 0 elsewhere (see fullscreen_quad.glsl), because the
// depth attachment a caller hands RenderFrameToTargetUVE is cleared by that pass and never
// written, and the scene's own clear colour is indistinguishable from dark geometry.
//
// Covered pixels are pushed to the near plane rather than carried at their real depth, so scene
// geometry occludes the grid drawn after it. That is correct for opaque geometry above the ground
// plane, which is every case the editor can currently author; geometry below y=0 will hide grid
// lines that should cross in front of it. Fixing that properly needs the renderer to export real
// per-pixel depth, which in turn needs a depth-compare mode the RHI does not expose yet.
constexpr std::string_view kMeshBlitFragmentShaderUVE = R"(#version 330 core
uniform sampler2D uMeshColor;
out vec4 FragColor;
void main() {
    vec4 meshColor = texelFetch(uMeshColor, ivec2(gl_FragCoord.xy), 0);
    if (meshColor.a < 0.25) {
        discard; // the renderer drew nothing here; leave the backdrop and its depth alone
    }
    FragColor = vec4(meshColor.rgb, 1.0);
    gl_FragDepth = 0.0; // every covered pixel hides the grid: an edge or a soft surface too, never the grid over it
}
)";

// Scene depth for the PRISM shell's overlays: each output texel is the nearest surface of a 2x2
// block of the renderer's depth target, as the distance along the editor view's forward axis from
// its eye (world units), or 1e30 where nothing was drawn.
constexpr std::string_view kHostDepthFragmentShaderUVE = R"(#version 330 core
uniform sampler2D uDepth;
uniform mat4 uInvViewProj;
uniform vec3 uEye;
uniform vec3 uForward;
uniform vec2 uOutSize;
out vec4 FragColor;
void main() {
    ivec2 size = textureSize(uDepth, 0);
    vec2 uv = gl_FragCoord.xy / uOutSize;
    ivec2 base = ivec2(uv * vec2(size));
    float d = 1.0;
    float u = 0.0;
    float v = 0.0;
    for (int y = -1; y <= 0; ++y) {
        for (int x = -1; x <= 0; ++x) {
            ivec2 p = clamp(base + ivec2(x, y), ivec2(0), size - 1);
            float s = texelFetch(uDepth, p, 0).r;
            if (s < d) {
                d = s;
                u = (float(p.x) + 0.5) / float(size.x);
                v = (float(p.y) + 0.5) / float(size.y);
            }
        }
    }
    if (d >= 0.999999) {
        FragColor = vec4(1.0e30);
        return;
    }
    vec4 world = uInvViewProj * vec4(u * 2.0 - 1.0, v * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    world /= world.w;
    FragColor = vec4(dot(world.xyz - uEye, uForward));
}
)";

[[nodiscard]] std::optional<UVE::Scene::EntityUVE> FindGameCameraEntityUVE(
    UVE::Scene::IEntityManagerUVE& entityManager) {
    return UVE::Scene::FindCurrentCameraEntityUVE(entityManager);
}

[[nodiscard]] bool HasActiveWorldEnvironmentUVE(UVE::Scene::IEntityManagerUVE& entityManager) {
    bool hasEnvironment = false;
    entityManager.ForEachUVE<UVE::Scene::WorldEnvironment3DComponentUVE>(
        [&hasEnvironment](UVE::Scene::EntityUVE, const UVE::Scene::WorldEnvironment3DComponentUVE& value) {
            hasEnvironment |= UVE::Scene::IsWorldEnvironment3DObjectComponentValidUVE(value);
        });
    entityManager.ForEachUVE<UVE::Scene::Sky3DComponentUVE>(
        [&hasEnvironment](UVE::Scene::EntityUVE, const UVE::Scene::Sky3DComponentUVE& sky) {
            if (!sky.enabled || !UVE::Scene::IsSky3DObjectComponentValidUVE(sky)) {
                return;
            }
            if (sky.source == UVE::Scene::SkySourceUVE::Panorama && sky.panoramaPath.empty()) {
                return;
            }
            hasEnvironment = true;
        });
    return hasEnvironment;
}

/// True when the DOCUMENT carries an authored DirectionalLight3D entity (even if currently hidden
/// or set to 0 energy), so turning an authored DirectionalLight3D off in the editor leaves the
/// scene unlit rather than re-activating the editor's preview sun.
[[nodiscard]] bool HasAuthoredDirectionalLightUVE(UVE::Scene::IEntityManagerUVE& entityManager) {
    return univex::integration::HasAuthoredDirectionalLightNodeUVE(entityManager);
}

[[nodiscard]] univex::gizmo::GizmoMesh BuildCameraFrustumMeshUVE(
    UVE::Scene::IEntityManagerUVE& entityManager, const UVE::Scene::EntityUVE hideEntity, const float aspectRatio) {
    univex::gizmo::GizmoMesh mesh;
    if (!(std::isfinite(aspectRatio) && aspectRatio > 0.0F)) {
        return mesh;
    }
    UVE::Render::CameraSystemUVE cameras;
    entityManager.ForEachUVE<UVE::Scene::WorldTransformComponentUVE, UVE::Scene::CameraComponentUVE>(
        [&](const UVE::Scene::EntityUVE entity, const UVE::Scene::WorldTransformComponentUVE& worldTransform,
            const UVE::Scene::CameraComponentUVE& camera) {
            if (entity == hideEntity || !UVE::Scene::IsDocumentCameraEntityUVE(entityManager, entity) ||
                !UVE::Scene::IsCameraComponentValidUVE(camera)) {
                return;
            }
            const UVE::Render::CameraFrustumCornersUVE corners =
                cameras.ComputeFrustumCornersUVE(entityManager, entity, aspectRatio);
            // The authored far plane can be hundreds or thousands of units away. Keep the editor
            // visualization compact around the camera while leaving the real camera projection,
            // far plane, and runtime rendering completely unchanged.
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
                const univex::math::Vec3 start = visualPoint(a);
                const univex::math::Vec3 end = visualPoint(b);
                mesh.lines.push_back(univex::gizmo::GizmoLine{
                    start, end, color, widthPx});
            };
            const auto rotate = [&](const UVE::Math::Vector3UVE local) {
                const UVE::Math::Vector3UVE world = UVE::Math::RotateVectorUVE(worldTransform.worldRotation, local);
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

void AppendLightDirectionGizmoUVE(univex::gizmo::GizmoMesh& mesh, const UVE::Scene::LightDirectionGizmoUVE& gizmo) {
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

[[nodiscard]] univex::gizmo::GizmoMesh BuildLightDirectionMeshUVE(UVE::Scene::IEntityManagerUVE& entityManager) {
    univex::gizmo::GizmoMesh mesh;
    std::vector<UVE::Scene::LightDirectionGizmoUVE> gizmos;
    UVE::Scene::CollectLightDirectionGizmosUVE(entityManager, gizmos);
    for (const UVE::Scene::LightDirectionGizmoUVE& gizmo : gizmos) {
        AppendLightDirectionGizmoUVE(mesh, gizmo);
    }
    return mesh;
}

[[nodiscard]] univex::math::Vec3 FogVolumeGizmoPointUVE(const UVE::Scene::FogVolume3DGizmoUVE& gizmo, const float x,
                                                        const float y, const float z) {
    const univex::math::Vec3 origin = univex::integration::FromUveVector3UVE(gizmo.origin);
    const univex::math::Vec3 axisX = univex::integration::FromUveVector3UVE(gizmo.axisX);
    const univex::math::Vec3 axisY = univex::integration::FromUveVector3UVE(gizmo.axisY);
    const univex::math::Vec3 axisZ = univex::integration::FromUveVector3UVE(gizmo.axisZ);
    return origin + axisX * x + axisY * y + axisZ * z;
}

void AppendFogVolumeRingUVE(univex::gizmo::GizmoMesh& mesh, const UVE::Scene::FogVolume3DGizmoUVE& gizmo,
                            const int axis, const float along, const float radiusA, const float radiusB,
                            const univex::math::Vec3& color) {
    constexpr int kSegments = 16;
    const float twoPi = 6.28318530718F;
    univex::math::Vec3 previous{};
    for (int i = 0; i <= kSegments; ++i) {
        const float angle = twoPi * static_cast<float>(i) / static_cast<float>(kSegments);
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

void AppendFogVolumeGizmoUVE(univex::gizmo::GizmoMesh& mesh, const UVE::Scene::FogVolume3DGizmoUVE& gizmo) {
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
    if (gizmo.shape == UVE::Scene::FogVolumeShapeUVE::Ellipsoid) {
        AppendFogVolumeRingUVE(mesh, gizmo, 0, 0.0F, hy, hz, color);
        AppendFogVolumeRingUVE(mesh, gizmo, 1, 0.0F, hx, hz, color);
        AppendFogVolumeRingUVE(mesh, gizmo, 2, 0.0F, hx, hy, color);
        return;
    }
    if (gizmo.shape == UVE::Scene::FogVolumeShapeUVE::Cylinder) {
        AppendFogVolumeRingUVE(mesh, gizmo, 1, hy, hx, hz, color);
        AppendFogVolumeRingUVE(mesh, gizmo, 1, -hy, hx, hz, color);
        addLine(hx, -hy, 0.0F, hx, hy, 0.0F);
        addLine(-hx, -hy, 0.0F, -hx, hy, 0.0F);
        addLine(0.0F, -hy, hz, 0.0F, hy, hz);
        addLine(0.0F, -hy, -hz, 0.0F, hy, -hz);
        return;
    }
    if (gizmo.shape == UVE::Scene::FogVolumeShapeUVE::Cone) {
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

[[nodiscard]] univex::gizmo::GizmoMesh BuildFogVolumeMeshUVE(UVE::Scene::IEntityManagerUVE& entityManager) {
    univex::gizmo::GizmoMesh mesh;
    std::vector<UVE::Scene::FogVolume3DGizmoUVE> gizmos;
    UVE::Scene::CollectFogVolume3DGizmosUVE(entityManager, gizmos);
    for (const UVE::Scene::FogVolume3DGizmoUVE& gizmo : gizmos) {
        AppendFogVolumeGizmoUVE(mesh, gizmo);
    }
    return mesh;
}

[[nodiscard]] univex::math::Vec3 ReflectionProbeGizmoPointUVE(const UVE::Scene::ReflectionProbe3DGizmoUVE& gizmo,
                                                              const float x, const float y, const float z) {
    const univex::math::Vec3 origin = univex::integration::FromUveVector3UVE(gizmo.origin);
    const univex::math::Vec3 axisX = univex::integration::FromUveVector3UVE(gizmo.axisX);
    const univex::math::Vec3 axisY = univex::integration::FromUveVector3UVE(gizmo.axisY);
    const univex::math::Vec3 axisZ = univex::integration::FromUveVector3UVE(gizmo.axisZ);
    return origin + axisX * x + axisY * y + axisZ * z;
}

void AppendReflectionProbeGizmoUVE(univex::gizmo::GizmoMesh& mesh, const UVE::Scene::ReflectionProbe3DGizmoUVE& gizmo) {
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

[[nodiscard]] univex::gizmo::GizmoMesh BuildReflectionProbeMeshUVE(UVE::Scene::IEntityManagerUVE& entityManager) {
    univex::gizmo::GizmoMesh mesh;
    std::vector<UVE::Scene::ReflectionProbe3DGizmoUVE> gizmos;
    UVE::Scene::CollectReflectionProbe3DGizmosUVE(entityManager, gizmos);
    for (const UVE::Scene::ReflectionProbe3DGizmoUVE& gizmo : gizmos) {
        AppendReflectionProbeGizmoUVE(mesh, gizmo);
    }
    return mesh;
}

void AppendWorldPartitionGizmoUVE(univex::gizmo::GizmoMesh& mesh, const UVE::Scene::WorldPartition3DGizmoUVE& gizmo) {
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

[[nodiscard]] univex::gizmo::GizmoMesh BuildWorldPartitionMeshUVE(UVE::Scene::IEntityManagerUVE& entityManager) {
    univex::gizmo::GizmoMesh mesh;
    std::vector<UVE::Scene::WorldPartition3DGizmoUVE> gizmos;
    UVE::Scene::CollectWorldPartition3DGizmosUVE(entityManager, gizmos);
    for (const UVE::Scene::WorldPartition3DGizmoUVE& gizmo : gizmos) {
        AppendWorldPartitionGizmoUVE(mesh, gizmo);
    }
    return mesh;
}

void AppendVisibilityRegionGizmoUVE(univex::gizmo::GizmoMesh& mesh,
                                    const UVE::Scene::VisibilityRegion3DGizmoUVE& gizmo) {
    using univex::math::Vec3;
    const Vec3 origin = univex::integration::FromUveVector3UVE(gizmo.origin);
    const Vec3 color = univex::integration::FromUveVector3UVE(gizmo.color);
    const float hx = gizmo.halfExtents.x;
    const float hy = gizmo.halfExtents.y;
    const float hz = gizmo.halfExtents.z;
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

[[nodiscard]] univex::gizmo::GizmoMesh BuildVisibilityRegionMeshUVE(UVE::Scene::IEntityManagerUVE& entityManager) {
    univex::gizmo::GizmoMesh mesh;
    std::vector<UVE::Scene::VisibilityRegion3DGizmoUVE> gizmos;
    UVE::Scene::CollectVisibilityRegion3DGizmosUVE(entityManager, gizmos);
    for (const UVE::Scene::VisibilityRegion3DGizmoUVE& gizmo : gizmos) {
        AppendVisibilityRegionGizmoUVE(mesh, gizmo);
    }
    return mesh;
}

void AppendOccluderGizmoUVE(univex::gizmo::GizmoMesh& mesh, const UVE::Scene::Occluder3DGizmoUVE& gizmo) {
    using univex::math::Vec3;
    const Vec3 origin = univex::integration::FromUveVector3UVE(gizmo.origin);
    const Vec3 color = univex::integration::FromUveVector3UVE(gizmo.color);
    const float hx = gizmo.halfExtents.x;
    const float hy = gizmo.halfExtents.y;
    const float hz = gizmo.halfExtents.z;
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

[[nodiscard]] univex::gizmo::GizmoMesh BuildColliderMeshUVE(UVE::Scene::IEntityManagerUVE& entityManager) {
    univex::gizmo::GizmoMesh mesh;
    entityManager.ForEachUVE<UVE::Scene::WorldTransformComponentUVE, UVE::Scene::ColliderComponentUVE>(
        [&](const UVE::Scene::EntityUVE, const UVE::Scene::WorldTransformComponentUVE& worldTransform,
            const UVE::Scene::ColliderComponentUVE& collider) {
            const univex::math::Vec3 color =
                collider.disabled ? univex::math::Vec3{0.5F, 0.5F, 0.5F} : univex::math::Vec3{0.35F, 0.9F, 0.35F};
            const float lineWidth = 1.5F;

            const auto toWorld = [&](const float lx, const float ly, const float lz) -> univex::math::Vec3 {
                const UVE::Math::Vector3UVE w =
                    UVE::Scene::TransformWorldPointUVE(worldTransform, {lx, ly, lz});
                return univex::integration::FromUveVector3UVE(w);
            };
            const auto addEdge = [&](const univex::math::Vec3& p1, const univex::math::Vec3& p2) {
                mesh.lines.push_back(univex::gizmo::GizmoLine{p1, p2, color, lineWidth});
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
                case UVE::Scene::ColliderShapeTypeUVE::Box: {
                    const float hx = collider.halfExtents.x;
                    const float hy = collider.halfExtents.y;
                    const float hz = collider.halfExtents.z;
                    const univex::math::Vec3 c[8] = {
                        toWorld(-hx, -hy, -hz), toWorld(hx, -hy, -hz),
                        toWorld(hx, -hy, hz),  toWorld(-hx, -hy, hz),
                        toWorld(-hx, hy, -hz),  toWorld(hx, hy, -hz),
                        toWorld(hx, hy, hz),   toWorld(-hx, hy, hz),
                    };
                    addEdge(c[0], c[1]); addEdge(c[1], c[2]); addEdge(c[2], c[3]); addEdge(c[3], c[0]);
                    addEdge(c[4], c[5]); addEdge(c[5], c[6]); addEdge(c[6], c[7]); addEdge(c[7], c[4]);
                    addEdge(c[0], c[4]); addEdge(c[1], c[5]); addEdge(c[2], c[6]); addEdge(c[3], c[7]);
                    break;
                }
                case UVE::Scene::ColliderShapeTypeUVE::Sphere: {
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
                            const float theta = static_cast<float>(i) * (2.0F * kPi / static_cast<float>(kRingSegments));
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
                case UVE::Scene::ColliderShapeTypeUVE::Capsule: {
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
                            addEdge(toWorld(r * cosPhi, -halfH, r * sinPhi),
                                    toWorld(r * cosPhi, halfH, r * sinPhi));
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
                case UVE::Scene::ColliderShapeTypeUVE::Cylinder: {
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

[[nodiscard]] univex::gizmo::GizmoMesh BuildOccluderMeshUVE(UVE::Scene::IEntityManagerUVE& entityManager) {
    univex::gizmo::GizmoMesh mesh;
    std::vector<UVE::Scene::Occluder3DGizmoUVE> gizmos;
    UVE::Scene::CollectOccluder3DGizmosUVE(entityManager, gizmos);
    for (const UVE::Scene::Occluder3DGizmoUVE& gizmo : gizmos) {
        AppendOccluderGizmoUVE(mesh, gizmo);
    }
    return mesh;
}

// Gizmos for the remaining spatial objects, from their own components: point light range spheres
// and spot light cones (unless the host draws spot lights itself), decal and GI volumes, navigation mesh bounds, on-screen notifier boxes,
// ray / shape casts, spring arms, markers, spawn points, audio listeners and paths. Shown only for
// objects visible in the hierarchy, never for the editor's own internal entities.
class ObjectGizmoBuilderUVE final {
public:
    using Vec3 = univex::math::Vec3;

    explicit ObjectGizmoBuilderUVE(univex::gizmo::GizmoMesh& mesh) : mesh_(mesh) {}

    void Line(const Vec3& a, const Vec3& b, const Vec3& color, const float width = 1.4F) {
        mesh_.lines.push_back(univex::gizmo::GizmoLine{a, b, color, width});
    }

    // `local` placed by the object's world transform (scale, then rotation, then position).
    [[nodiscard]] static Vec3 Place(const UVE::Scene::WorldTransformComponentUVE& world, const Vec3& local) {
        const UVE::Math::Vector3UVE scaled{local.x * world.worldScale.x, local.y * world.worldScale.y,
                                           local.z * world.worldScale.z};
        const UVE::Math::Vector3UVE rotated = UVE::Math::RotateVectorUVE(world.worldRotation, scaled);
        return Vec3{world.worldPosition.x + rotated.x, world.worldPosition.y + rotated.y,
                    world.worldPosition.z + rotated.z};
    }
    // A direction rotated (not scaled) into world space.
    [[nodiscard]] static Vec3 Turn(const UVE::Scene::WorldTransformComponentUVE& world, const Vec3& local) {
        const UVE::Math::Vector3UVE rotated =
            UVE::Math::RotateVectorUVE(world.worldRotation, UVE::Math::Vector3UVE{local.x, local.y, local.z});
        return Vec3{rotated.x, rotated.y, rotated.z};
    }

    void Box(const UVE::Scene::WorldTransformComponentUVE& world, const Vec3& centre, const Vec3& half,
             const Vec3& color, const float width = 1.4F) {
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

    // A world-space circle around `centre` in the plane spanned by `u` and `v` (unit vectors).
    void Circle(const Vec3& centre, const Vec3& u, const Vec3& v, const float radius, const Vec3& color,
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
        const Vec3 side = std::fabs(dir.y) < 0.9F ? univex::math::Normalize(univex::math::Cross(dir, Vec3{0.0F, 1.0F, 0.0F}))
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
    univex::gizmo::GizmoMesh& mesh_;
};

[[nodiscard]] bool ShowsObjectGizmoUVE(UVE::Scene::IEntityManagerUVE& entityManager, const UVE::Scene::EntityUVE entity) {
    if (entityManager.HasComponentUVE<UVE::Scene::EditorInternalEntityComponentUVE>(entity)) {
        return false;
    }
    return !entityManager.HasComponentUVE<UVE::Scene::VisibilityComponentUVE>(entity) ||
           entityManager.GetComponentUVE<UVE::Scene::VisibilityComponentUVE>(entity).visibleInHierarchy;
}

[[nodiscard]] univex::gizmo::GizmoMesh BuildMoreObjectGizmosMeshUVE(UVE::Scene::IEntityManagerUVE& entityManager,
                                                                   const bool includeSpotCones) {
    using univex::math::Vec3;
    using WorldUVE = UVE::Scene::WorldTransformComponentUVE;
    univex::gizmo::GizmoMesh mesh;
    ObjectGizmoBuilderUVE g(mesh);
    const Vec3 zero{0.0F, 0.0F, 0.0F};
    const auto origin = [](const WorldUVE& w) { return Vec3{w.worldPosition.x, w.worldPosition.y, w.worldPosition.z}; };

    // Point lights: their range as three rings; spot lights: their cone out to the range.
    entityManager.ForEachUVE<WorldUVE, UVE::Scene::LightComponentUVE>(
        [&](const UVE::Scene::EntityUVE e, const WorldUVE& w, const UVE::Scene::LightComponentUVE& light) {
            if (!ShowsObjectGizmoUVE(entityManager, e) || light.type == UVE::Scene::LightTypeUVE::Directional ||
                !(light.range > 0.0F) || !std::isfinite(light.range)) {
                return;
            }
            const Vec3 color{0.5F + 0.5F * std::clamp(light.color.r, 0.0F, 1.0F),
                             0.5F + 0.5F * std::clamp(light.color.g, 0.0F, 1.0F),
                             0.5F + 0.5F * std::clamp(light.color.b, 0.0F, 1.0F)};
            if (light.type == UVE::Scene::LightTypeUVE::Point) {
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

    // Decals: the projection box and the direction they project along (-Y).
    entityManager.ForEachUVE<WorldUVE, UVE::Scene::Decal3DComponentUVE>(
        [&](const UVE::Scene::EntityUVE e, const WorldUVE& w, const UVE::Scene::Decal3DComponentUVE& decal) {
            if (!ShowsObjectGizmoUVE(entityManager, e)) {
                return;
            }
            const Vec3 color{0.95F, 0.78F, 0.40F};
            const Vec3 half{decal.size.x * 0.5F, decal.size.y * 0.5F, decal.size.z * 0.5F};
            g.Box(w, zero, half, color);
            g.Arrow(ObjectGizmoBuilderUVE::Place(w, zero), ObjectGizmoBuilderUVE::Place(w, Vec3{0.0F, -half.y, 0.0F}),
                    color);
        });

    // Global illumination volumes and navigation mesh bounds.
    entityManager.ForEachUVE<WorldUVE, UVE::Scene::VoxelGI3DComponentUVE>(
        [&](const UVE::Scene::EntityUVE e, const WorldUVE& w, const UVE::Scene::VoxelGI3DComponentUVE& gi) {
            if (ShowsObjectGizmoUVE(entityManager, e)) {
                g.Box(w, zero, Vec3{gi.halfExtents.x, gi.halfExtents.y, gi.halfExtents.z}, Vec3{0.55F, 0.85F, 1.0F});
            }
        });
    entityManager.ForEachUVE<WorldUVE, UVE::Scene::LightmapGI3DComponentUVE>(
        [&](const UVE::Scene::EntityUVE e, const WorldUVE& w, const UVE::Scene::LightmapGI3DComponentUVE& gi) {
            if (ShowsObjectGizmoUVE(entityManager, e)) {
                g.Box(w, zero, Vec3{gi.halfExtents.x, gi.halfExtents.y, gi.halfExtents.z}, Vec3{1.0F, 0.85F, 0.45F});
            }
        });
    entityManager.ForEachUVE<WorldUVE, UVE::Scene::NavMeshVolume3DComponentUVE>(
        [&](const UVE::Scene::EntityUVE e, const WorldUVE& w, const UVE::Scene::NavMeshVolume3DComponentUVE& nav) {
            if (ShowsObjectGizmoUVE(entityManager, e)) {
                g.Box(w, zero, Vec3{nav.boundsHalfExtents.x, nav.boundsHalfExtents.y, nav.boundsHalfExtents.z},
                      Vec3{0.35F, 0.90F, 0.50F});
            }
        });
    entityManager.ForEachUVE<WorldUVE, UVE::Scene::VisibleOnScreenNotifier3DComponentUVE>(
        [&](const UVE::Scene::EntityUVE e, const WorldUVE& w, const UVE::Scene::VisibleOnScreenNotifier3DComponentUVE& n) {
            if (ShowsObjectGizmoUVE(entityManager, e)) {
                g.Box(w, Vec3{n.aabbCenterOffset.x, n.aabbCenterOffset.y, n.aabbCenterOffset.z},
                      Vec3{n.aabbHalfExtents.x, n.aabbHalfExtents.y, n.aabbHalfExtents.z}, Vec3{0.95F, 0.60F, 0.95F});
            }
        });

    // Ray and shape casts: the cast, with the swept shape's end where it stops.
    entityManager.ForEachUVE<WorldUVE, UVE::Scene::RayCast3DComponentUVE>(
        [&](const UVE::Scene::EntityUVE e, const WorldUVE& w, const UVE::Scene::RayCast3DComponentUVE& ray) {
            if (!ShowsObjectGizmoUVE(entityManager, e) || !(ray.length > 0.0F) || !std::isfinite(ray.length)) {
                return;
            }
            const Vec3 dir = univex::math::Normalize(
                ObjectGizmoBuilderUVE::Turn(w, Vec3{ray.direction.x, ray.direction.y, ray.direction.z}));
            g.Arrow(origin(w), origin(w) + dir * ray.length,
                    ray.hit ? Vec3{1.0F, 0.35F, 0.30F} : Vec3{1.0F, 0.62F, 0.22F});
        });
    entityManager.ForEachUVE<WorldUVE, UVE::Scene::ShapeCast3DComponentUVE>(
        [&](const UVE::Scene::EntityUVE e, const WorldUVE& w, const UVE::Scene::ShapeCast3DComponentUVE& cast) {
            if (!ShowsObjectGizmoUVE(entityManager, e) || !(cast.maxDistance > 0.0F) || !std::isfinite(cast.maxDistance)) {
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

    // Spring arms extend along local +Z; the current length is the solid part.
    entityManager.ForEachUVE<WorldUVE, UVE::Scene::SpringArm3DComponentUVE>(
        [&](const UVE::Scene::EntityUVE e, const WorldUVE& w, const UVE::Scene::SpringArm3DComponentUVE& arm) {
            if (!ShowsObjectGizmoUVE(entityManager, e) || !(arm.armLength > 0.0F) || !std::isfinite(arm.armLength)) {
                return;
            }
            const Vec3 dir = ObjectGizmoBuilderUVE::Turn(w, Vec3{0.0F, 0.0F, 1.0F});
            const float current = std::clamp(arm.currentLength, 0.0F, arm.armLength);
            g.Line(origin(w), origin(w) + dir * current, Vec3{0.92F, 0.90F, 0.40F}, 2.0F);
            g.Line(origin(w) + dir * current, origin(w) + dir * arm.armLength, Vec3{0.55F, 0.54F, 0.30F}, 1.2F);
            g.Sphere(origin(w) + dir * current, 0.08F, Vec3{0.92F, 0.90F, 0.40F});
        });

    // Markers: a small axis cross. Spawn points: a ring and the facing arrow. Listeners: a ring.
    entityManager.ForEachUVE<WorldUVE, UVE::Scene::Marker3DComponentUVE>(
        [&](const UVE::Scene::EntityUVE e, const WorldUVE& w, const UVE::Scene::Marker3DComponentUVE&) {
            if (!ShowsObjectGizmoUVE(entityManager, e)) {
                return;
            }
            const float s = 0.5F;
            g.Line(ObjectGizmoBuilderUVE::Place(w, Vec3{-s, 0.0F, 0.0F}), ObjectGizmoBuilderUVE::Place(w, Vec3{s, 0.0F, 0.0F}),
                   Vec3{0.90F, 0.32F, 0.32F});
            g.Line(ObjectGizmoBuilderUVE::Place(w, Vec3{0.0F, -s, 0.0F}), ObjectGizmoBuilderUVE::Place(w, Vec3{0.0F, s, 0.0F}),
                   Vec3{0.45F, 0.80F, 0.30F});
            g.Line(ObjectGizmoBuilderUVE::Place(w, Vec3{0.0F, 0.0F, -s}), ObjectGizmoBuilderUVE::Place(w, Vec3{0.0F, 0.0F, s}),
                   Vec3{0.30F, 0.55F, 0.92F});
        });
    entityManager.ForEachUVE<WorldUVE, UVE::Scene::SpawnPoint3DComponentUVE>(
        [&](const UVE::Scene::EntityUVE e, const WorldUVE& w, const UVE::Scene::SpawnPoint3DComponentUVE&) {
            if (!ShowsObjectGizmoUVE(entityManager, e)) {
                return;
            }
            const Vec3 color{0.40F, 0.80F, 1.0F};
            g.Circle(origin(w), ObjectGizmoBuilderUVE::Turn(w, Vec3{1.0F, 0.0F, 0.0F}),
                     ObjectGizmoBuilderUVE::Turn(w, Vec3{0.0F, 0.0F, 1.0F}), 0.5F, color);
            g.Arrow(origin(w), origin(w) + ObjectGizmoBuilderUVE::Turn(w, Vec3{0.0F, 0.0F, -1.0F}) * 0.9F, color);
        });
    entityManager.ForEachUVE<WorldUVE, UVE::Scene::AudioListener3DComponentUVE>(
        [&](const UVE::Scene::EntityUVE e, const WorldUVE& w, const UVE::Scene::AudioListener3DComponentUVE&) {
            if (!ShowsObjectGizmoUVE(entityManager, e)) {
                return;
            }
            const Vec3 color{0.82F, 0.82F, 0.86F};
            g.Circle(origin(w), ObjectGizmoBuilderUVE::Turn(w, Vec3{1.0F, 0.0F, 0.0F}),
                     ObjectGizmoBuilderUVE::Turn(w, Vec3{0.0F, 1.0F, 0.0F}), 0.35F, color);
            g.Arrow(origin(w), origin(w) + ObjectGizmoBuilderUVE::Turn(w, Vec3{0.0F, 0.0F, -1.0F}) * 0.7F, color);
        });

    // Paths: the curve through their points (local space), closed when they loop.
    entityManager.ForEachUVE<WorldUVE, UVE::Scene::Path3DComponentUVE>(
        [&](const UVE::Scene::EntityUVE e, const WorldUVE& w, const UVE::Scene::Path3DComponentUVE& path) {
            if (!ShowsObjectGizmoUVE(entityManager, e) || path.points.size() < 2U) {
                return;
            }
            const Vec3 color{0.42F, 0.72F, 1.0F};
            const std::size_t count = std::min(path.points.size(), UVE::Scene::kMaximumPath3DPointsUVE);
            const auto at = [&](const std::size_t i) {
                const UVE::Math::Vector3UVE& p = path.points[i];
                return ObjectGizmoBuilderUVE::Place(w, Vec3{p.x, p.y, p.z});
            };
            for (std::size_t i = 1; i < count; ++i) {
                g.Line(at(i - 1), at(i), color, 2.0F);
            }
            if (path.closed && count > 2U) {
                g.Line(at(count - 1), at(0), color, 2.0F);
            }
            for (std::size_t i = 0; i < count; ++i) {
                g.Sphere(at(i), 0.06F, color);
            }
        });
    return mesh;
}

// Bridges Engine/Editor/Viewport's real GL renderer (grid + orbit camera + transform/orientation
// gizmos + one proxy cube per live scene entity) into EditorUVE's generic, viewport-agnostic
// "Viewport" panel hook (EditorUVE::SetViewportPanelRendererUVE) - see that method's own doc
// comment in editor_uve.h for why EditorCore itself stays free of any Viewport-module/GL/ImGui
// types. Renders off-screen into an MSAA framebuffer, resolves into a plain 2D texture
// ImGui::Image() can sample, and recreates both whenever the panel's reported size changes -
// the same multisample-then-resolve shape tools/headless_capture.cpp already uses (that file's
// own comment explains why: the solid gizmo/cube geometry has nothing but MSAA to soften its
// silhouettes) and the same resize-on-demand shape app/main.cpp's interactive demo already uses
// for its own window-sized framebuffer, just driven by the ImGui panel's reported size instead of
// a GLFW framebuffer-resize callback.
using VpInputUVE = UVE::Editor::EditorViewportInputUVE;
using VpBtnUVE = VpInputUVE::ButtonUVE;
using VpKeyUVE = VpInputUVE::KeyUVE;
using VpCursorUVE = VpInputUVE::CursorUVE;

class ViewportPanelBackendUVE final {
public:
    /// Draws the scene `services` hold: the level's, or a document scene's (the Entity Editor's).
    ViewportPanelBackendUVE(UVE::Editor::EditorUVE& editor, UVE::Core::EngineCoreUVE& engine,
                            UVE::Core::EngineServicesUVE& services,
                            const UVE::Editor::EditorUVE::ViewportContextUVE context)
        : editor_(editor), engine_(engine), services_(services), entityManager_(services.GetEntityManagerUVE()),
          meshLayer_(services), context_(context) {
        camera_.Settings().allowFullOrbit = true;
    }

    ~ViewportPanelBackendUVE() {
        DestroyFramebuffersUVE();
        if (meshBlitVao_ != 0U) {
            glDeleteVertexArrays(1, &meshBlitVao_);
        }
        if (uiFontAtlasTexture_ != 0U) {
            glDeleteTextures(1, &uiFontAtlasTexture_);
        }
    }

    ViewportPanelBackendUVE(const ViewportPanelBackendUVE&) = delete;
    ViewportPanelBackendUVE& operator=(const ViewportPanelBackendUVE&) = delete;

    /// This frame's input for the view (see EditorViewportInputUVE); empty outside RenderUVE.
    [[nodiscard]] const VpInputUVE& In() const noexcept {
        static const VpInputUVE kNoInputUVE{};
        return input_ != nullptr ? *input_ : kNoInputUVE;
    }
    const VpInputUVE* input_ = nullptr;

    [[nodiscard]] std::uint64_t RenderUVE(const UVE::Math::Vector2UVE& availableSize,
                                          UVE::Math::Vector2UVE& outUsedSize,
                                          const UVE::Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) {
        input_ = &overlayState.input;
        if (!EnsureGlewInitializedUVE() || !EnsureRenderPassUVE()) {
            return 0U;
        }
        // The game's UI is drawn inside this view, so its buttons are hit where they are drawn:
        // the pointer is measured from the view's corner, not the window's.
        if (context_ == UVE::Editor::EditorUVE::ViewportContextUVE::Main) {
            engine_.SetUIPointerOriginUVE(In().imageOrigin);
            engine_.SetUIViewportSizeUVE(UVE::Math::Vector2UVE{std::floor(availableSize.x), std::floor(availableSize.y)});
            if (!engine_.HasMouseCursorHandlerUVE()) {
                // Set Show Mouse Cursor in Play frees the editor's view pointer for the game's UI.
                engine_.SetMouseCursorHandlerUVE([this](const bool show) { editor_.SetGameMouseCapturedUVE(!show); });
            }
        }

        const int width = std::max(1, static_cast<int>(availableSize.x));
        const int height = std::max(1, static_cast<int>(availableSize.y));
        if (!EnsureFramebuffersUVE(width, height)) {
            return 0U;
        }

        if (overlayState.is2DMode && !overlayState.studioView &&
            context_ != UVE::Editor::EditorUVE::ViewportContextUVE::InspectorCameraPreview) {
            if (!viewport2dHost_.has_value()) {
                viewport2dHost_.emplace(editor_, engine_);
            }
            if (!overlayState.axisColorsValid) {
                SeedEditorAxisColorsFromDefaultsUVE();
            }
            outUsedSize = UVE::Math::Vector2UVE{static_cast<float>(width), static_cast<float>(height)};
            GLint previousFbo = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
            glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
            glEnable(GL_MULTISAMPLE);
            viewport2dHost_->RenderToBoundFramebufferUVE(width, height, overlayState);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFbo_);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo_);
            glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
            if (overlayState.gameWorkspaceActive) {
                DrawUIOverlayUVE();
            }
            return static_cast<std::uint64_t>(resolveColorTexture_);
        }

        ApplyOverlayStateUVE(overlayState);
        const UVE::Scene::EntityUVE previewCamera = editor_.GetPreviewCameraUVE();
        const bool inspectorCameraPreview = context_ == UVE::Editor::EditorUVE::ViewportContextUVE::InspectorCameraPreview;
        const UVE::Scene::EntityUVE selectedEntity = editor_.GetSelectedEntityUVE();
        const bool selectedIsCamera = inspectorCameraPreview &&
                                      UVE::Scene::IsDocumentCameraEntityUVE(entityManager_, selectedEntity);
        const bool wantPreview =
            !gameWorkspaceActive_ && !studioView_ &&
            ((previewCamera != UVE::Scene::kInvalidEntityUVE) || selectedIsCamera);
        // Looking through a Camera3D flies the view into it, and back out to where it was: an
        // animation of the editor camera, not a cut (the small Details preview cuts, it has no
        // view of its own to fly from).
        const bool flies = !inspectorCameraPreview && !selectedIsCamera;
        if (wantPreview && !previewing_) {
            poseBeforePreview_ = CameraPoseUVE{camera_.Target(), camera_.Yaw(), camera_.Pitch(), camera_.Distance(),
                                               camera_.IsOrthographic()};
            fovBeforePreview_ = camera_.Settings().fovYRadians;
            previewing_ = true;
            if (flies) {
                if (const std::optional<CameraFlightPoseUVE> into = CameraEntityPoseUVE(previewCamera)) {
                    camera_.CancelAnimation();
                    flight_ = CameraFlightUVE{CurrentFlightPoseUVE(), *into, 0.0F, true};
                    camera_.SetOrthographic(false);
                }
            }
        } else if (!wantPreview && previewing_) {
            if (poseBeforePreview_.has_value()) {
                camera_.CancelAnimation();
                const CameraFlightPoseUVE back{poseBeforePreview_->target, poseBeforePreview_->yaw,
                                               poseBeforePreview_->pitch, poseBeforePreview_->distance, fovBeforePreview_};
                const std::optional<CameraFlightPoseUVE> from =
                    flight_.has_value() ? std::optional<CameraFlightPoseUVE>{CurrentFlightPoseUVE()}
                                        : CameraEntityPoseUVE(lastPreviewCamera_);
                if (flies && from.has_value()) {
                    ApplyFlightPoseUVE(*from);
                    flight_ = CameraFlightUVE{*from, back, 0.0F, false};
                } else {
                    flight_.reset();
                    ApplyFlightPoseUVE(back);
                }
                camera_.SetOrthographic(poseBeforePreview_->orthographic);
                poseBeforePreview_.reset();
            }
            previewing_ = false;
        }
        if (previewCamera != UVE::Scene::kInvalidEntityUVE) {
            lastPreviewCamera_ = previewCamera;
        }
        if (flight_.has_value()) {
            flight_->t = std::min(1.0F, flight_->t + In().deltaTime / kCameraFlightSecondsUVE);
            const float t = flight_->t;
            const float e = t * t * (3.0F - 2.0F * t);   // ease in and out
            const CameraFlightPoseUVE& a = flight_->from;
            const CameraFlightPoseUVE& b = flight_->to;
            float yawDelta = b.yaw - a.yaw;
            while (yawDelta > 3.14159265F) { yawDelta -= 6.2831853F; }
            while (yawDelta < -3.14159265F) { yawDelta += 6.2831853F; }
            ApplyFlightPoseUVE(CameraFlightPoseUVE{a.target + (b.target - a.target) * e, a.yaw + yawDelta * e,
                                                   a.pitch + (b.pitch - a.pitch) * e,
                                                   a.distance + (b.distance - a.distance) * e,
                                                   a.fovY + (b.fovY - a.fovY) * e});
            if (t >= 1.0F) {
                if (!flight_->entering) {
                    camera_.Settings().fovYRadians = fovBeforePreview_;
                }
                flight_.reset();
            }
        }
        const bool hostGizmos = overlayState.hostDrawsGizmos;
        renderPass_->Settings().viewGizmos = !hostGizmos && !studioView_ && !gameWorkspaceActive_ && !previewing_;
        renderPass_->Settings().viewTransformGizmo = !hostGizmos && !studioView_ && !gameWorkspaceActive_ && !previewing_;
        if (studioView_) {
            // Looked at, not edited: nothing is selected or moved, and the view does not turn.
            renderPass_->Settings().viewTransformGizmo = false;
            renderPass_->SetHighlightedGizmoHandleUVE(univex::gizmo::GizmoHandleUVE::None);
            if (In().hovered && In().wheel != 0.0F) {
                camera_.Dolly(In().wheel);
            }
        } else {
            UpdateSelectionGizmoUVE();
            if (hostGizmos) {
                // The shell draws and drives the gizmos; it hands the navigation gizmo's orbit
                // drags over as pixels, the same units a mouse orbit uses.
                renderPass_->Settings().viewTransformGizmo = false;
                if (overlayState.orbitRequestX != 0.0F || overlayState.orbitRequestY != 0.0F) {
                    camera_.Orbit(overlayState.orbitRequestX, overlayState.orbitRequestY);
                    editor_.NotifyViewportOrbitedUVE();
                }
            }
            const bool navGizmoOwnsGesture =
                (previewing_ || hostGizmos) ? false : UpdateNavGizmoInteractionUVE(width, height);
            // A handle drag outranks both: grabbing an arrow must not also orbit the camera or, on
            // release, register as a click that selects whatever is behind the gizmo.
            const bool gizmoOwnsGesture = !hostGizmos && !navGizmoOwnsGesture && UpdateGizmoDragUVE(width, height);
            if (!hostGizmos) {
                UpdateGizmoHoverHighlightUVE(width, height, navGizmoOwnsGesture);
            }
            const bool pointerTaken = navGizmoOwnsGesture || gizmoOwnsGesture;
            UpdateSelectionFromMouseUVE(width, height, pointerTaken);
            UpdateEntityContextToolbarFromMouseUVE(width, height, pointerTaken);
            if (!previewing_) {
                UpdateCameraFromMouseUVE(height, pointerTaken);
                UpdateViewportViewHotkeysUVE();
                UpdateViewportBookmarkHotkeysUVE();
            }
        }
        // Advances the eased snap-to-axis animation SnapToDirection() starts (a manual orbit/pan
        // cancels it instead - see OrbitCamera.cpp) - without this the camera would flag itself
        // "animating" and then never actually move, since nothing else ticks it forward. Mirrors
        // app/main.cpp's own per-frame state.camera.Update(deltaSeconds) call in the standalone demo.
        camera_.Update(In().deltaTime);
        // Where a new object appears when its placement follows the view.
        const univex::math::Vec3 cameraFocus = camera_.Target();
        editor_.SetViewportCameraFocusUVE(UVE::Math::Vector3UVE{cameraFocus.x, cameraFocus.y, cameraFocus.z});

        // Real scene entities, rendered via the same lit/shaded pipeline EngineCoreUVE itself uses
        // at runtime (Renderer3DUVE::RenderFrameToTargetUVE) - see EditorMeshLayerUVE's own header
        // comment. This runs first because its colour and depth are injected into the viewport's
        // framebuffer below, between the backdrop and the grid, so the grid depth-tests against
        // real geometry. While the Game workspace tab is active, render through the scene's own
        // camera instead of the editor's free-look OrbitCamera - see FindGameCameraEntityUVE's own
        // comment for the "first camera found" convention; EditorMeshLayerUVE itself falls back to
        // the OrbitCamera-synced view if the scene has no usable camera, so a Play session with no
        // authored camera still shows something instead of a blank panel.
        std::optional<UVE::Scene::EntityUVE> lookThroughCamera;
        if (gameWorkspaceActive_) {
            lookThroughCamera = FindGameCameraEntityUVE(entityManager_);
        } else if (selectedIsCamera) {
            lookThroughCamera = selectedEntity;
        } else if (wantPreview && !flight_.has_value()) {
            // Through the camera itself once the view has flown into it.
            lookThroughCamera = previewCamera;
        }
        // ---- Editor preview lighting (Godot's "Preview Sun and Environment") -----------------
        // Resolved every frame from the toolbar's own state and from what the document actually
        // contains, so the preview appears exactly while the scene lacks the real thing and
        // disappears the moment it gains one. The Game tab is a preview of the running project,
        // where an editor convenience must never appear, so it opts out entirely - which is also
        // why a scene with no lights of its own looks dark there, honestly.
        {
            const bool hasAuthoredSun = HasAuthoredDirectionalLightUVE(entityManager_);
            const bool hasAuthoredEnv = HasActiveWorldEnvironmentUVE(entityManager_);
            const bool previewAllowed = !overlayState.gameWorkspaceActive;
            UVE::Scene::PreviewLightingSettingsUVE previewSettings{};
            previewSettings.sunEnabled = overlayState.previewSun && previewAllowed;
            previewSettings.environmentEnabled = overlayState.previewEnvironment && previewAllowed;
            previewSettings.sunAzimuthDegrees = overlayState.previewSunAzimuth;
            previewSettings.sunAltitudeDegrees = overlayState.previewSunAltitude;
            previewSettings.sunEnergy = overlayState.previewSunEnergy;
            previewSettings.sunColor = UVE::Math::Vector3UVE{overlayState.previewSunColor[0],
                                                             overlayState.previewSunColor[1],
                                                             overlayState.previewSunColor[2]};
            previewSettings.skyColor = UVE::Math::Vector3UVE{overlayState.previewSkyColor[0],
                                                             overlayState.previewSkyColor[1],
                                                             overlayState.previewSkyColor[2]};
            previewSettings.horizonColor = UVE::Math::Vector3UVE{overlayState.previewHorizonColor[0],
                                                                 overlayState.previewHorizonColor[1],
                                                                 overlayState.previewHorizonColor[2]};
            previewSettings.groundColor = UVE::Math::Vector3UVE{overlayState.previewGroundColor[0],
                                                                overlayState.previewGroundColor[1],
                                                                overlayState.previewGroundColor[2]};
            previewSettings.ambientEnergy = overlayState.previewAmbientEnergy;

            const UVE::Scene::PreviewLightingFrameUVE previewFrame = UVE::Scene::ResolvePreviewLightingUVE(
                hasAuthoredSun, hasAuthoredEnv, previewSettings);
            meshLayer_.SetPreviewLightingUVE(previewFrame);
            services_.GetRenderer3DUVE().SetPreviewEnvironmentUVE(
                previewFrame.environmentActive
                    ? std::optional<UVE::Scene::WorldEnvironmentFrameUVE>(previewFrame.environment)
                    : std::nullopt);
        }

        const univex::integration::EditorMeshLayerResultUVE meshResult = meshLayer_.RenderUVE(
            camera_, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), lookThroughCamera);
        const bool wantOverlay = !gameWorkspaceActive_ && !studioView_ && !previewing_;
        if (wantOverlay) {
            const float aspect = static_cast<float>(width) / static_cast<float>(height);
            const UVE::Scene::EntityUVE lookThrough = lookThroughCamera.value_or(UVE::Scene::kInvalidEntityUVE);
            const std::size_t entityCount = entityManager_.GetEntityCountUVE();

            overlayRefreshTimerSeconds_ += In().deltaTime;
            const bool timerElapsed = overlayRefreshTimerSeconds_ >= 0.10F;
            const bool stateChanged = !cachedOverlayActive_ ||
                                      std::fabs(aspect - cachedOverlayAspect_) > 0.001F ||
                                      lookThrough != cachedLookThroughCamera_ ||
                                      entityCount != cachedOverlayEntityCount_ ||
                                      dragHandle_ != univex::gizmo::GizmoHandleUVE::None;
            if (stateChanged || timerElapsed) {
                overlayRefreshTimerSeconds_ = 0.0F;
                cachedOverlayActive_ = true;
                cachedOverlayAspect_ = aspect;
                cachedLookThroughCamera_ = lookThrough;
                cachedOverlayEntityCount_ = entityCount;

                // The PRISM shell draws cameras and directional / spot lights with its own glyphs.
                univex::gizmo::GizmoMesh overlay =
                    hostGizmos ? univex::gizmo::GizmoMesh{} : BuildCameraFrustumMeshUVE(entityManager_, lookThrough, aspect);
                univex::gizmo::GizmoMesh lights =
                    hostGizmos ? univex::gizmo::GizmoMesh{} : BuildLightDirectionMeshUVE(entityManager_);
                overlay.lines.insert(overlay.lines.end(), lights.lines.begin(), lights.lines.end());
                overlay.triangles.insert(overlay.triangles.end(), lights.triangles.begin(), lights.triangles.end());
                univex::gizmo::GizmoMesh fog = BuildFogVolumeMeshUVE(entityManager_);
                overlay.lines.insert(overlay.lines.end(), fog.lines.begin(), fog.lines.end());
                overlay.triangles.insert(overlay.triangles.end(), fog.triangles.begin(), fog.triangles.end());
                univex::gizmo::GizmoMesh probes = BuildReflectionProbeMeshUVE(entityManager_);
                overlay.lines.insert(overlay.lines.end(), probes.lines.begin(), probes.lines.end());
                overlay.triangles.insert(overlay.triangles.end(), probes.triangles.begin(), probes.triangles.end());
                univex::gizmo::GizmoMesh partitions = BuildWorldPartitionMeshUVE(entityManager_);
                overlay.lines.insert(overlay.lines.end(), partitions.lines.begin(), partitions.lines.end());
                overlay.triangles.insert(overlay.triangles.end(), partitions.triangles.begin(), partitions.triangles.end());
                univex::gizmo::GizmoMesh regions = BuildVisibilityRegionMeshUVE(entityManager_);
                overlay.lines.insert(overlay.lines.end(), regions.lines.begin(), regions.lines.end());
                overlay.triangles.insert(overlay.triangles.end(), regions.triangles.begin(), regions.triangles.end());
                univex::gizmo::GizmoMesh occluders = BuildOccluderMeshUVE(entityManager_);
                overlay.lines.insert(overlay.lines.end(), occluders.lines.begin(), occluders.lines.end());
                overlay.triangles.insert(overlay.triangles.end(), occluders.triangles.begin(), occluders.triangles.end());
                univex::gizmo::GizmoMesh more = BuildMoreObjectGizmosMeshUVE(entityManager_, !hostGizmos);
                overlay.lines.insert(overlay.lines.end(), more.lines.begin(), more.lines.end());
                univex::gizmo::GizmoMesh colliders = BuildColliderMeshUVE(entityManager_);
                const std::size_t colliderLinesFrom = overlay.lines.size();
                overlay.lines.insert(overlay.lines.end(), colliders.lines.begin(), colliders.lines.end());
                overlay.triangles.insert(overlay.triangles.end(), colliders.triangles.begin(), colliders.triangles.end());
                if (hostGizmos) {
                    hostOverlayMesh_ = std::move(overlay);
                    hostOverlayOccludedFrom_ = colliderLinesFrom;
                    renderPass_->SetCameraFrustumMeshUVE({});
                } else {
                    hostOverlayMesh_.Clear();
                    renderPass_->SetCameraFrustumMeshUVE(std::move(overlay));
                }
            }
        } else {
            hostOverlayMesh_.Clear();
            if (cachedOverlayActive_) {
                renderPass_->SetCameraFrustumMeshUVE({});
                cachedOverlayActive_ = false;
            }
        }
        ExportViewForHostUVE(overlayState, width, height, wantOverlay);
        outUsedSize = UVE::Math::Vector2UVE{static_cast<float>(width), static_cast<float>(height)};

        // One framebuffer, one depth buffer, drawn back to front: backdrop, then the engine's
        // scene geometry, then the grid (which depth-tests against it), then the gizmos last of
        // all so nothing can paint over them.
        GLint previousFbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
        glEnable(GL_MULTISAMPLE);
        renderPass_->ClearUVE(width, height);
        const univex::render::StudioBackdropRenderer* const studio = studioView_ ? StudioUVE() : nullptr;
        if (studio != nullptr) {
            studio->DrawBackdrop(camera_, width, height);
        } else {
            const bool neutralMainFallback = !HasActiveWorldEnvironmentUVE(entityManager_);
            renderPass_->RenderBackgroundUVE(neutralMainFallback);
        }
        if (meshResult.colorTextureId != 0U && EnsureMeshBlitResourcesUVE()) {
            BlitMeshLayerUVE(meshResult);
        }
        if (studio != nullptr) {
            studio->DrawFloor(camera_, width, height);
        }
        // The PRISM shell draws the grid and the selection outline itself (depth-tested against
        // the scene depth exported below), from the settings and the mask this pass hands it.
        overlayState.selectionMask.clear();
        overlayState.selectionMaskWidth = overlayState.selectionMaskHeight = 0;
        ExportGridForHostUVE(overlayState, hostGizmos && !previewing_ && !studioView_);
        if (hostGizmos && !lookThroughCamera.has_value()) {
            ExportSceneDepthForHostUVE(overlayState, width, height);
        } else {
            overlayState.sceneDepth.clear();
            overlayState.sceneDepthWidth = overlayState.sceneDepthHeight = 0;
        }
        if (!previewing_ && !hostGizmos) {
            renderPass_->RenderGridUVE(camera_, width, height);
        }
        // The selection outline sits over the scene and the grid, under the gizmos. The Game tab
        // previews what a player sees, so no editor outline there.
        if (!gameWorkspaceActive_ && !studioView_ && !previewing_) {
            const auto& selected = editor_.GetSelectedEntitiesUVE();
            const auto active = editor_.GetSelectedEntityUVE();
            if (selected.empty()) {
                cachedSelectionTriangles_.clear();
                lastSelectedEntities_.clear();
                lastSelectedEntity_ = UVE::Scene::kInvalidEntityUVE;
                lastSelectedSnapshots_.clear();
            } else {
                bool needsRebuild = false;
                if (selected != lastSelectedEntities_ || active != lastSelectedEntity_) {
                    needsRebuild = true;
                } else if (dragHandle_ != univex::gizmo::GizmoHandleUVE::None) {
                    needsRebuild = true;
                } else if (AnySelectedEntityMoved()) {
                    needsRebuild = true;
                }

                if (needsRebuild) {
                    cachedSelectionTriangles_ = univex::integration::CollectSelectionOutlineTrianglesUVE(
                        entityManager_, selected, active);
                    lastSelectedEntities_ = selected;
                    lastSelectedEntity_ = active;
                    lastSelectedSnapshots_.clear();
                    lastSelectedSnapshots_.reserve(selected.size());
                    for (const auto ent : selected) {
                        if (entityManager_.IsAliveUVE(ent) &&
                            entityManager_.HasComponentUVE<UVE::Scene::WorldTransformComponentUVE>(ent)) {
                            const auto& wt = entityManager_.GetComponentUVE<UVE::Scene::WorldTransformComponentUVE>(ent);
                            lastSelectedSnapshots_.push_back({ent, wt.worldPosition, wt.worldRotation});
                        }
                    }
                }
            }
            if (!cachedSelectionTriangles_.empty()) {
                if (hostGizmos) {
                    const univex::render::SelectionOutlineSettings& outline = renderPass_->SelectionOutlineSettingsUVE();
                    if (renderPass_->ReadSelectionMaskUVE(camera_, width, height, cachedSelectionTriangles_,
                                                          overlayState.selectionMask)) {
                        overlayState.selectionMaskWidth = width;
                        overlayState.selectionMaskHeight = height;
                        overlayState.selectionOutlineRadius =
                            static_cast<int>(std::lround(outline.thicknessPixels));
                        overlayState.selectionOutlineRgb = {outline.r, outline.g, outline.b};
                    }
                    // ReadMask leaves its own framebuffer bound for reading; draw on into ours.
                    glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
                    glViewport(0, 0, width, height);
                } else {
                    renderPass_->RenderSelectionOutlineUVE(camera_, width, height, cachedSelectionTriangles_);
                }
            }
        }
        renderPass_->RenderOverlayUVE(camera_, width, height);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFbo_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo_);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));

        // Player-facing HUD content only shows during the Game workspace tab's "what a player
        // would see" preview (matching the grid/transform-gizmo hiding above) - drawn via ImGui's
        // own foreground overlay rather than baked into meshResult's texture; see this method's own
        // DrawUIOverlayUVE() comment for why.
        if (gameWorkspaceActive_) {
            DrawUIOverlayUVE();
        }

        return static_cast<std::uint64_t>(resolveColorTexture_);
    }

private:
    [[nodiscard]] bool EnsureGlewInitializedUVE() {
        if (glewInitialized_) {
            return true;
        }
        glewExperimental = GL_TRUE;
        if (const GLenum status = glewInit(); status != GLEW_OK) {
            UVE_ERROR("uve_editor_app: glewInit failed for the viewport panel: {}",
                      reinterpret_cast<const char*>(glewGetErrorString(status)));
            return false;
        }
        glGetError(); // discard GLEW's benign core-profile extension-probe error
        glewInitialized_ = true;
        return true;
    }

    [[nodiscard]] bool EnsureRenderPassUVE() {
        if (renderPass_.has_value()) {
            return true;
        }
        std::string error;
        renderPass_ = univex::app::ViewportRenderPass::Create(error);
        if (!renderPass_.has_value()) {
            UVE_ERROR("uve_editor_app: viewport render pass init failed: {}", error);
            return false;
        }
        return true;
    }

    [[nodiscard]] bool EnsureFramebuffersUVE(const int width, const int height) {
        if (width <= 0 || height <= 0) {
            return false;
        }
        if (width == framebufferWidth_ && height == framebufferHeight_ && resolveFbo_ != 0U) {
            return true;
        }
        DestroyFramebuffersUVE();
        framebufferWidth_ = width;
        framebufferHeight_ = height;

        GLint maxSamples = 0;
        glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        const GLsizei samples = std::min(maxSamples, 8);

        glGenFramebuffers(1, &msaaFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
        glGenRenderbuffers(1, &msaaColorRb_);
        glBindRenderbuffer(GL_RENDERBUFFER, msaaColorRb_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaaColorRb_);
        glGenRenderbuffers(1, &msaaDepthRb_);
        glBindRenderbuffer(GL_RENDERBUFFER, msaaDepthRb_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, msaaDepthRb_);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            UVE_ERROR("uve_editor_app: viewport MSAA framebuffer incomplete");
            return false;
        }

        glGenFramebuffers(1, &resolveFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, resolveFbo_);
        glGenTextures(1, &resolveColorTexture_);
        glBindTexture(GL_TEXTURE_2D, resolveColorTexture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveColorTexture_, 0);
        const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (!complete) {
            UVE_ERROR("uve_editor_app: viewport resolve framebuffer incomplete");
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return complete;
    }

    void DestroyFramebuffersUVE() {
        if (resolveColorTexture_ != 0U) {
            glDeleteTextures(1, &resolveColorTexture_);
        }
        if (resolveFbo_ != 0U) {
            glDeleteFramebuffers(1, &resolveFbo_);
        }
        if (msaaColorRb_ != 0U) {
            glDeleteRenderbuffers(1, &msaaColorRb_);
        }
        if (msaaDepthRb_ != 0U) {
            glDeleteRenderbuffers(1, &msaaDepthRb_);
        }
        if (msaaFbo_ != 0U) {
            glDeleteFramebuffers(1, &msaaFbo_);
        }
        resolveColorTexture_ = resolveFbo_ = msaaColorRb_ = msaaDepthRb_ = msaaFbo_ = 0U;
        framebufferWidth_ = framebufferHeight_ = 0;
    }

    // Lazily builds the mesh-blit shader and its empty VAO once. Nothing here is size-dependent:
    // the pass draws straight into the viewport's own MSAA framebuffer, so there is no second
    // colour target to keep in step with the panel's size.
    [[nodiscard]] bool EnsureMeshBlitResourcesUVE() {
        if (meshBlitProgram_.has_value()) {
            return true;
        }
        std::string error;
        meshBlitProgram_ = univex::render::ShaderProgram::Build(kMeshBlitVertexShaderUVE,
                                                                 kMeshBlitFragmentShaderUVE, error);
        if (!meshBlitProgram_.has_value()) {
            UVE_ERROR("uve_editor_app: viewport mesh blit shader build failed: {}", error);
            return false;
        }
        glGenVertexArrays(1, &meshBlitVao_);
        return true;
    }

    // Writes EditorMeshLayerUVE's colour and depth into the currently bound framebuffer, so the
    // grid and gizmos that follow share one depth buffer with the engine's scene geometry. Depth
    // testing stays off: the mesh layer's own depth already resolved visibility between meshes,
    // and at this point in the frame only the backdrop is underneath.
    void BlitMeshLayerUVE(const univex::integration::EditorMeshLayerResultUVE& meshResult) {
        // GL_ALWAYS rather than disabling the test: OpenGL skips depth-buffer writes entirely
        // while GL_DEPTH_TEST is disabled, whatever the write mask says, and the depth this pass
        // writes is the whole reason it exists.
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_ALWAYS);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        meshBlitProgram_->Use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, meshResult.colorTextureId);
        glUniform1i(meshBlitProgram_->UniformLocation("uMeshColor"), 0);
        glBindVertexArray(meshBlitVao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE0);
        // The grid depth-tests against what this pass just wrote, so put the comparison back.
        glDepthFunc(GL_LESS);
    }

    // Uploads UI::UIFontAtlasUVE's baked RGBA8 bitmap once (it never changes after construction),
    // for AddImage()'s glyph quads below.
    void EnsureUIFontAtlasTextureUVE(const UVE::UI::UIFontAtlasUVE& fontAtlas) {
        if (uiFontAtlasTexture_ != 0U || !fontAtlas.IsValidUVE()) {
            return;
        }
        glGenTextures(1, &uiFontAtlasTexture_);
        glBindTexture(GL_TEXTURE_2D, uiFontAtlasTexture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, UVE::UI::UIFontAtlasUVE::kAtlasWidthUVE,
                     UVE::UI::UIFontAtlasUVE::kAtlasHeightUVE, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     fontAtlas.GetBitmapUVE().data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // Draws UIRuntimeUVE's current draw batch directly via ImGui's own foreground overlay draw
    // list, in the same real-window pixel coordinates UIQuadUVE is authored in (matching
    // IInputSystemUVE::GetMousePositionUVE()'s own convention - confirmed directly: hovering the
    // real cursor at a button's authored positionPixels correctly sets isHovered, proving that
    // space really is the whole application window, not this one panel's local render-target
    // space). This is why UI content is NOT baked into meshLayer_'s own offscreen texture (see
    // EditorMeshLayerUVE::RenderUVE()'s own call site comment) - a per-pixel depth-based compositor
    // could never distinguish "UI was drawn here" from "nothing was drawn here" without real
    // OpenGL depth WRITES, which require depth TESTING to also be enabled - exactly what a
    // screen-space overlay must never have (it always draws on top, regardless of the 3D scene).
    // Image quads referencing a real (non-zero) texture asset guid fall back to a flat tint here
    // (this preview path does not resolve/upload arbitrary imported textures) - an honest, stated
    // limitation, not a silent gap.
    void DrawUIOverlayUVE() {
        const UVE::UI::UIDrawBatchUVE& batch = engine_.GetUIRuntimeUVE().GetDrawBatchUVE();
        if (batch.quads.empty() || ImGui::GetCurrentContext() == nullptr) {
            return;
        }
        EnsureUIFontAtlasTextureUVE(engine_.GetUIRuntimeUVE().GetFontAtlasUVE());
        // Window pixels, placed where the window is: with viewports on, ImGui draws in screen
        // coordinates, so the main window's own screen position is added.
        const ImVec2 origin = ImGui::GetMainViewport()->Pos;
        ImDrawList* const drawList = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
        for (const UVE::UI::UIQuadUVE& quad : batch.quads) {
            const ImVec2 pMin{origin.x + quad.rect.position.x, origin.y + quad.rect.position.y};
            const ImVec2 pMax{pMin.x + quad.rect.size.x, pMin.y + quad.rect.size.y};
            const ImU32 tint = ImGui::ColorConvertFloat4ToU32(
                ImVec4{quad.color.x, quad.color.y, quad.color.z, quad.alpha});
            if (quad.kind == UVE::UI::UIDrawItemKindUVE::Glyph && uiFontAtlasTexture_ != 0U) {
                drawList->AddImage(static_cast<ImTextureID>(static_cast<std::uintptr_t>(uiFontAtlasTexture_)), pMin,
                                   pMax, ImVec2{quad.u0, quad.v0}, ImVec2{quad.u1, quad.v1}, tint);
            } else {
                drawList->AddRectFilled(pMin, pMax, tint);
            }
        }
    }

    // Applies EditorUVE's own generic overlay-toolbar state (see ViewportOverlayStateUVE's doc
    // comment on why it's plain enums/bools rather than any Viewport-module type) to the real
    // ViewportRenderPass each frame. Snap is no longer decorative: the bubble now writes through
    // to EditorUVE's real snapping settings, which is what quantises a handle drag (see
    // UpdateGizmoDragUVE).
    using ViewportViewUVE = UVE::Editor::EditorUVE::ViewportViewUVE;

    // Looks at the orbit target along a world axis. The direction is where the eye sits relative
    // to the target, the same convention as the nav gizmo's balls (+Y is Top).
    void ApplyNamedViewUVE(const ViewportViewUVE view) {
        // Top and Bottom are set by yaw and pitch directly: looking down the pole has no yaw of its
        // own, and a named view must always land the same way - X to the right, -Z up the screen
        // for Top - whatever angle the camera came from.
        constexpr float kHalfPi = 1.5707963f;
        const auto& limits = camera_.Settings();
        switch (view) {
            case ViewportViewUVE::Top: camera_.SnapToYawPitch(kHalfPi, limits.pitchMax); return;
            case ViewportViewUVE::Bottom: camera_.SnapToYawPitch(kHalfPi, limits.pitchMin); return;
            case ViewportViewUVE::User: return;
            default: camera_.SnapToDirection(NamedViewDirectionUVE(view)); return;
        }
    }

    // Where the eye sits relative to the target for a named view (the nav gizmo's convention,
    // +Y is Top); zero for a free view.
    [[nodiscard]] static univex::math::Vec3 NamedViewDirectionUVE(const ViewportViewUVE view) {
        switch (view) {
            case ViewportViewUVE::Top: return {0.f, 1.f, 0.f};
            case ViewportViewUVE::Bottom: return {0.f, -1.f, 0.f};
            case ViewportViewUVE::Front: return {0.f, 0.f, 1.f};
            case ViewportViewUVE::Back: return {0.f, 0.f, -1.f};
            case ViewportViewUVE::Right: return {1.f, 0.f, 0.f};
            case ViewportViewUVE::Left: return {-1.f, 0.f, 0.f};
            case ViewportViewUVE::User: break;
        }
        return {0.f, 0.f, 0.f};
    }

    // The nav gizmo's ball directions are exact world axes; anything else is not a named view.
    [[nodiscard]] static ViewportViewUVE NamedViewForDirectionUVE(const univex::math::Vec3& d) {
        if (d.y > 0.5f) return ViewportViewUVE::Top;
        if (d.y < -0.5f) return ViewportViewUVE::Bottom;
        if (d.z > 0.5f) return ViewportViewUVE::Front;
        if (d.z < -0.5f) return ViewportViewUVE::Back;
        if (d.x > 0.5f) return ViewportViewUVE::Right;
        if (d.x < -0.5f) return ViewportViewUVE::Left;
        return ViewportViewUVE::User;
    }

    void ApplyOverlayStateUVE(const UVE::Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) {
        services_.GetRenderer3DUVE().SetShadingModeUVE(overlayState.shadingMode);
        auto& settings = renderPass_->Settings();
        settings.projection = overlayState.orthographic ? univex::viewport::ProjectionMode::Orthographic
                                                        : univex::viewport::ProjectionMode::Perspective;
        // The editor owns projection and named views; the camera follows it. Before this, the
        // toolbar's projection pill changed only this settings field, which nothing in the editor
        // host read - the camera never left perspective.
        camera_.SetOrthographic(overlayState.orthographic);
        if (overlayState.viewRequestSerial != appliedViewRequestSerial_) {
            appliedViewRequestSerial_ = overlayState.viewRequestSerial;
            ApplyNamedViewUVE(overlayState.view);
        }
        if (overlayState.focusRequestSerial != appliedFocusRequestSerial_) {
            appliedFocusRequestSerial_ = overlayState.focusRequestSerial;
            FocusCameraOnEntityUVE(overlayState.focusEntity);
        }
        // The Game workspace tab previews what a player would see - no editor-only grid overlay.
        settings.viewGrid = overlayState.gridVisible && !overlayState.gameWorkspaceActive;
        renderPass_->SetGridOpacityUVE(overlayState.gridOpacity);
        renderPass_->SetGridCellSizeUVE(overlayState.gridCellSize);
        const auto& prefs = editor_.GetExtendedPreferencesUVE();
        if (prefs.gridFadeDistance > 0.0) {
            renderPass_->Grid().Settings().fadeEndDistanceScale =
                static_cast<float>(std::clamp(prefs.gridFadeDistance * 0.25, 10.0, 500.0));
        }
        constexpr float kSelectionOutlineVisualScaleUVE = 0.20F;
        renderPass_->SetSelectionOutlineUVE(univex::render::SelectionOutlineSettings{
            overlayState.selectionOutlineVisible, overlayState.selectionOutlineColor.r,
            overlayState.selectionOutlineColor.g, overlayState.selectionOutlineColor.b,
            overlayState.selectionOutlineThickness * kSelectionOutlineVisualScaleUVE});
        // A named side view looks along the ground, which is only an edge from there; the grid
        // stands up on the plane facing the camera instead, so the view keeps a reference.
        const univex::math::Vec3 viewAxis = NamedViewDirectionUVE(overlayState.view);
        renderPass_->SetGridPlaneUVE(univex::render::GridPlaneFacing(viewAxis.x, viewAxis.y, viewAxis.z));
        gameWorkspaceActive_ = overlayState.gameWorkspaceActive;
        // Play flies the editing camera (mouse look, WASD) when the scene has no camera of its own;
        // Stop puts it back where it was, so the level is seen again as it was left - not from
        // wherever the game's mouse look happened to turn it.
        {
            const bool inPlay = editor_.GetPlayModeStateUVE() != UVE::Editor::EditorPlayModeStateUVE::Edit;
            if (inPlay && !poseBeforePlay_.has_value()) {
                poseBeforePlay_ = CameraPoseUVE{camera_.Target(), camera_.Yaw(), camera_.Pitch(), camera_.Distance(),
                                                camera_.IsOrthographic()};
            } else if (!inPlay && poseBeforePlay_.has_value()) {
                camera_.CancelAnimation();
                camera_.SetTarget(poseBeforePlay_->target);
                camera_.SetYawPitch(poseBeforePlay_->yaw, poseBeforePlay_->pitch);
                camera_.SetDistance(poseBeforePlay_->distance);
                camera_.SetOrthographic(poseBeforePlay_->orthographic);
                poseBeforePlay_.reset();
            }
        }
        pointerOverOverlay_ = overlayState.pointerOverOverlay;
        ApplyStudioViewUVE(overlayState);
        // Axis colours drive the gizmo AND the grid's own axis lines, so they go through
        // SetAxisPaletteUVE rather than being written into either one directly - see that method.
        // Skipped until the host has seeded the real defaults, so an unset state cannot paint
        // every axis black (see ViewportOverlayStateUVE::axisColorsValid).
        if (overlayState.axisColorsValid) {
            static_cast<void>(renderPass_->SetAxisPaletteUVE(univex::viewport::AxisPaletteUVE{
                univex::viewport::AxisRgbUVE{overlayState.axisColorX.r, overlayState.axisColorX.g,
                                             overlayState.axisColorX.b},
                univex::viewport::AxisRgbUVE{overlayState.axisColorY.r, overlayState.axisColorY.g,
                                             overlayState.axisColorY.b},
                univex::viewport::AxisRgbUVE{overlayState.axisColorZ.r, overlayState.axisColorZ.g,
                                             overlayState.axisColorZ.b}}));
        } else {
            SeedEditorAxisColorsFromDefaultsUVE();
        }
        std::vector<univex::gizmo::BoneOverlayUVE> bones;
        bones.reserve(overlayState.bones.size());
        const auto toVec3 = [](const std::array<float, 3>& value) { return univex::math::Vec3{value[0], value[1], value[2]}; };
        for (const auto& bone : overlayState.bones) {
            bones.push_back(univex::gizmo::BoneOverlayUVE{toVec3(bone.head), toVec3(bone.tail), toVec3(bone.side),
                                                          bone.hasLink, toVec3(bone.linkFrom), bone.skeletonSelected,
                                                          bone.boneSelected, bone.hasColour, toVec3(bone.colour),
                                                          bone.jointOnly, bone.helper});
        }
        if (overlayState.hostDrawsGizmos) {
            // The shell draws them: keep the same bone shapes, built for the current view.
            const univex::math::Vec3 view = univex::math::Normalize(camera_.Target() - camera_.Eye());
            hostBonesMesh_ = univex::gizmo::BuildBoneMeshUVE(bones, view);
            renderPass_->SetBonesUVE({});
        } else {
            hostBonesMesh_.Clear();
            renderPass_->SetBonesUVE(std::move(bones));
        }
        // Collision wireframes. One colour for every shape: the point of the overlay is WHERE the
        // collider is, and a per-shape palette would make two boxes on different entities read as
        // two different kinds of thing. The blue-green is deliberately not the selection orange, so
        // a selected object's outline and its collider never look like the same annotation.
        {
            constexpr float kColliderWireRedUVE = 0.20F;
            constexpr float kColliderWireGreenUVE = 0.95F;
            constexpr float kColliderWireBlueUVE = 0.45F;
            constexpr float kColliderWireWidthPixelsUVE = 1.5F;
            // The selected object's own shape, lifted rather than recoloured: same family as every
            // other wireframe, so it still reads as a collider, and still not the selection orange,
            // so a highlight never looks like the object's outline.
            constexpr float kColliderHighlightWidthPixelsUVE = 2.5F;
            const univex::math::Vec3 wireColour{kColliderWireRedUVE, kColliderWireGreenUVE,
                                                kColliderWireBlueUVE};
            const univex::math::Vec3 highlightColour{0.70F, 1.00F, 0.82F};
            std::vector<univex::gizmo::GizmoLine> colliderLines;
            colliderLines.reserve(overlayState.colliderLines.size());
            for (const auto& line : overlayState.colliderLines) {
                colliderLines.push_back(
                    univex::gizmo::GizmoLine{toVec3(line.from), toVec3(line.to),
                                             line.highlighted ? highlightColour : wireColour,
                                             line.highlighted ? kColliderHighlightWidthPixelsUVE
                                                              : kColliderWireWidthPixelsUVE});
            }
            if (overlayState.hostDrawsGizmos) {
                hostColliderLines_ = std::move(colliderLines);
                renderPass_->SetColliderWireframesUVE({});
            } else {
                hostColliderLines_.clear();
                renderPass_->SetColliderWireframesUVE(std::move(colliderLines));
            }
        }
        using UVE::Editor::EditorUVE;
        switch (overlayState.gizmoMode) {
            case EditorUVE::ViewportGizmoModeUVE::Move:
                renderPass_->SetGizmoMode(univex::gizmo::GizmoMode::Move);
                break;
            case EditorUVE::ViewportGizmoModeUVE::Rotate:
                renderPass_->SetGizmoMode(univex::gizmo::GizmoMode::Rotate);
                break;
            case EditorUVE::ViewportGizmoModeUVE::Scale:
                renderPass_->SetGizmoMode(univex::gizmo::GizmoMode::Scale);
                break;
            case EditorUVE::ViewportGizmoModeUVE::Universal:
                renderPass_->SetGizmoMode(univex::gizmo::GizmoMode::Universal);
                break;
        }
    }

    // The studio view (the Retarget window): no grid, corner gizmo or dark backdrop, and a camera
    // placed to face the front. The editing camera's pose is kept and put back afterwards, so
    // the scene comes back seen from where it was left.
    void ApplyStudioViewUVE(const UVE::Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) {
        auto& settings = renderPass_->Settings();
        if (overlayState.studioView && !studioView_) {
            poseBeforeStudio_ = CameraPoseUVE{camera_.Target(), camera_.Yaw(), camera_.Pitch(), camera_.Distance(),
                                              camera_.IsOrthographic()};
        } else if (!overlayState.studioView && studioView_ && poseBeforeStudio_.has_value()) {
            camera_.CancelAnimation();
            camera_.SetTarget(poseBeforeStudio_->target);
            camera_.SetYawPitch(poseBeforeStudio_->yaw, poseBeforeStudio_->pitch);
            camera_.SetDistance(poseBeforeStudio_->distance);
            camera_.SetOrthographic(poseBeforeStudio_->orthographic);
            poseBeforeStudio_.reset();
        }
        studioView_ = overlayState.studioView;
        settings.viewGizmos = !studioView_;
        settings.viewEnvironment = !studioView_;
        if (studioView_) {
            settings.viewGrid = false;
            camera_.SetOrthographic(false);
            if (overlayState.studioFramingSerial != appliedStudioFramingSerial_) {
                appliedStudioFramingSerial_ = overlayState.studioFramingSerial;
                // From the front (+Z, where the humanoid faces), a little above, looking slightly down.
                constexpr float kFrontYaw = 1.5707963f;
                constexpr float kStudioPitch = 0.10f;
                camera_.CancelAnimation();
                camera_.Focus(univex::math::Vec3{overlayState.studioTarget[0], overlayState.studioTarget[1],
                                                 overlayState.studioTarget[2]},
                              overlayState.studioRadius);
                camera_.SetYawPitch(kFrontYaw, kStudioPitch);
            }
        }
    }

    [[nodiscard]] const univex::render::StudioBackdropRenderer* StudioUVE() {
        if (!studioTried_) {
            studioTried_ = true;
            std::string error;
            studio_ = univex::render::StudioBackdropRenderer::Create(error);
            if (!studio_.has_value()) {
                UVE_ERROR("uve_editor_app: studio backdrop unavailable: {}", error);
            }
        }
        return studio_.has_value() ? &*studio_ : nullptr;
    }

    // Hands EditorCore the viewport's own default axis hues. It cannot name them itself -
    // AxisPalette.h belongs to the Viewport module EditorCore deliberately does not depend on -
    // so this side of the boundary supplies them, and the menu bar's picker edits from there.
    //
    // Driven by the `axisColorsValid == false` branch above rather than called once at startup, so
    // one line covers both cases that need it: the first frame, and "Reset to defaults", which
    // clears the flag precisely so these defaults come back from the one place that owns them.
    void SeedEditorAxisColorsFromDefaultsUVE() {
        const univex::viewport::AxisPaletteUVE defaults;
        using EditorAxisColorUVE = UVE::Editor::EditorUVE::ViewportAxisColorUVE;
        const auto toEditor = [](const univex::viewport::AxisRgbUVE& color) {
            return EditorAxisColorUVE{color.r, color.g, color.b};
        };
        static_cast<void>(editor_.SetViewportAxisColorsUVE(
            toEditor(defaults.x), toEditor(defaults.y), toEditor(defaults.z)));
    }

    // Only shows the transform gizmo (and its pivot dot) while a real entity is selected
    // in EditorUVE, like a Object3D-style engine - the reference standalone demo always draws it at
    // the camera's own orbit target since it has no independent "selected object" concept, which
    // read as a stray gizmo floating with nothing selected once wired into a real editor.
    // Repositions the gizmo to the selected entity's actual world transform via
    // SetGizmoPivotOverride() (see that method's own comment on why camera.Target() alone isn't
    // enough - orbiting the camera must not drag a selected object's gizmo along with it).
    void UpdateSelectionGizmoUVE() {
        // The pivot is the centroid of everything selected, not the active entity's own position.
        // With one object the two are identical; with several, using the active entity put the gizmo
        // on whichever object happened to be clicked last, sitting off to one side of the group it
        // claims to represent.
        univex::math::Vec3 centroid{0.0F, 0.0F, 0.0F};
        int contributing = 0;
        for (const UVE::Scene::EntityUVE entity : editor_.GetSelectedEntitiesUVE()) {
            if (entity == UVE::Scene::kInvalidEntityUVE ||
                !entityManager_.HasComponentUVE<UVE::Scene::WorldTransformComponentUVE>(entity)) {
                continue;
            }
            const auto& worldTransform =
                entityManager_.GetComponentUVE<UVE::Scene::WorldTransformComponentUVE>(entity);
            centroid += univex::integration::FromUveVector3UVE(worldTransform.worldPosition);
            ++contributing;
        }

        const bool hasSelection = contributing > 0;
        renderPass_->Settings().viewTransformGizmo = hasSelection && !gameWorkspaceActive_ && !previewing_;
        if (hasSelection) {
            gizmoPivot_ = centroid * (1.0F / static_cast<float>(contributing));
            renderPass_->SetGizmoPivotOverride(gizmoPivot_);
        } else {
            gizmoPivot_.reset();
            renderPass_->SetGizmoPivotOverride(std::nullopt);
        }
    }

    // Drags a transform handle.
    //
    // The gizmo has been drawable but not touchable: geometry was produced and nothing ever asked
    // what the pointer was on. This closes that loop - pick a handle, project the cursor ray onto
    // it each frame, and feed the result through EditorUVE's gesture API so the whole drag is one
    // undo step.
    //
    // Every number here is the one the renderer drew with, taken from the same helpers
    // (ScaleForPixelRadius / WorldPerPixelAtPointUVE at the gizmo's own pivot), so the region that
    // responds is the region that is visible.
    //
    // Returns true while a drag owns the pointer, which suppresses both orbit and click-to-select.
    [[nodiscard]] bool UpdateGizmoDragUVE(const int width, const int height) {
        if (dragHandle_ != univex::gizmo::GizmoHandleUVE::None) {
            return ContinueGizmoDragUVE(width, height);
        }
        return TryBeginGizmoDragUVE(width, height);
    }

    void UpdateGizmoHoverHighlightUVE(const int width, const int height, const bool navGizmoOwnsGesture) {
        if (dragHandle_ != univex::gizmo::GizmoHandleUVE::None) {
            renderPass_->SetHighlightedGizmoHandleUVE(dragHandle_);
            return;
        }
        if (navGizmoOwnsGesture || pointerOverOverlay_ || !In().hovered) {
            renderPass_->SetHighlightedGizmoHandleUVE(univex::gizmo::GizmoHandleUVE::None);
            return;
        }
        const std::optional<GizmoPlacementUVE> placement = CurrentGizmoPlacementUVE(height);
        if (!placement.has_value()) {
            renderPass_->SetHighlightedGizmoHandleUVE(univex::gizmo::GizmoHandleUVE::None);
            return;
        }
        const UVE::Math::Vector2UVE imageOrigin = In().imageOrigin;
        const UVE::Editor::EditorViewportInputUVE& io = In();
        univex::math::Vec3 rayOrigin{};
        univex::math::Vec3 rayDirection{};
        CursorRayUVE(width, height, io.mousePos.x - imageOrigin.x, io.mousePos.y - imageOrigin.y,
                     rayOrigin, rayDirection);
        const univex::gizmo::GizmoPickResultUVE pick = univex::gizmo::PickGizmoHandleUVE(
            renderPass_->Mode(), renderPass_->Style(), rayOrigin, rayDirection, placement->pivot,
            placement->scale, placement->viewDirection, placement->unitsPerPixel);
        renderPass_->SetHighlightedGizmoHandleUVE(pick.handle);
    }

    /// The gizmo's placement this frame: the pivot it is drawn at, the world size of one gizmo
    /// unit, and how many gizmo units a pixel spans. Empty when no gizmo is on screen.
    struct GizmoPlacementUVE final {
        univex::math::Vec3 pivot{};
        float scale = 1.0F;
        float unitsPerPixel = 1.0F;
        univex::math::Vec3 viewDirection{};
    };

    [[nodiscard]] std::optional<GizmoPlacementUVE> CurrentGizmoPlacementUVE(const int height) const {
        if (!gizmoPivot_.has_value() || !renderPass_->Settings().viewTransformGizmo || height <= 0) {
            return std::nullopt;
        }
        GizmoPlacementUVE placement;
        placement.pivot = *gizmoPivot_;
        placement.scale = univex::render::GizmoRenderer::ScaleForPixelRadius(
            camera_, height, renderPass_->Style().gizmoPixelRadius, placement.pivot);
        const float worldPerPixel =
            univex::camera::WorldPerPixelAtPointUVE(camera_, height, placement.pivot);
        placement.unitsPerPixel = (placement.scale > 0.0F) ? worldPerPixel / placement.scale : 1.0F;
        placement.viewDirection = univex::math::Normalize(camera_.Target() - camera_.Eye());
        return placement;
    }

    /// The cursor ray in the viewport's own math kit, for the gizmo code that lives in the
    /// engine-agnostic core.
    void CursorRayUVE(const int width, const int height, const float pixelX, const float pixelY,
                      univex::math::Vec3& outOrigin, univex::math::Vec3& outDirection) const {
        const UVE::Math::RayUVE ray =
            univex::integration::BuildCursorRayUVE(camera_, width, height, pixelX, pixelY);
        outOrigin = univex::integration::FromUveVector3UVE(ray.origin);
        outDirection = univex::integration::FromUveVector3UVE(ray.direction);
    }

    [[nodiscard]] bool TryBeginGizmoDragUVE(const int width, const int height) {
        if (pointerOverOverlay_ || !In().hovered ||
            !In().IsClickedUVE(VpBtnUVE::Left)) {
            return false;
        }
        const std::optional<GizmoPlacementUVE> placement = CurrentGizmoPlacementUVE(height);
        if (!placement.has_value()) {
            return false;
        }
        const univex::gizmo::GizmoMode mode = renderPass_->Mode();
        if (mode != univex::gizmo::GizmoMode::Move && mode != univex::gizmo::GizmoMode::Rotate &&
            mode != univex::gizmo::GizmoMode::Scale && mode != univex::gizmo::GizmoMode::Universal) {
            return false;
        }

        const UVE::Math::Vector2UVE imageOrigin = In().imageOrigin;
        const UVE::Editor::EditorViewportInputUVE& io = In();
        univex::math::Vec3 rayOrigin{};
        univex::math::Vec3 rayDirection{};
        CursorRayUVE(width, height, io.mousePos.x - imageOrigin.x, io.mousePos.y - imageOrigin.y,
                     rayOrigin, rayDirection);

        const univex::gizmo::GizmoPickResultUVE pick = univex::gizmo::PickGizmoHandleUVE(
            mode, renderPass_->Style(), rayOrigin, rayDirection, placement->pivot, placement->scale,
            placement->viewDirection, placement->unitsPerPixel);
        if (pick.handle == univex::gizmo::GizmoHandleUVE::None) {
            return false;
        }

        univex::gizmo::GizmoMode effectiveMode = mode;
        if (mode == univex::gizmo::GizmoMode::Universal) {
            if (pick.handle == univex::gizmo::GizmoHandleUVE::Uniform) {
                effectiveMode = univex::gizmo::GizmoMode::Scale;
            } else {
                // Disambiguate whether the click landed on the rotate ring, scale cube, or move arrow/shaft.
                univex::gizmo::GizmoStyle universalStyle = renderPass_->Style();
                universalStyle.ringRadius = universalStyle.universalRingRadius;
                universalStyle.ringLineWidthPx = universalStyle.universalRingWidthPx;
                const auto rotatePick = univex::gizmo::PickGizmoHandleUVE(
                    univex::gizmo::GizmoMode::Rotate, universalStyle, rayOrigin, rayDirection,
                    placement->pivot, placement->scale, placement->viewDirection, placement->unitsPerPixel);
                if (rotatePick.handle == pick.handle) {
                    effectiveMode = univex::gizmo::GizmoMode::Rotate;
                } else {
                    if (const auto axisDir = univex::gizmo::AxisDirectionForHandleUVE(pick.handle)) {
                        const auto along = univex::gizmo::ProjectRayOntoAxisUVE(
                            rayOrigin, rayDirection, placement->pivot, *axisDir);
                        const float splitOffset =
                            (universalStyle.universalShaftEnd + universalStyle.universalConeLength +
                             universalStyle.universalScaleBoxOffset) *
                            0.5f * placement->scale;
                        effectiveMode = (along.has_value() && *along >= splitOffset)
                                            ? univex::gizmo::GizmoMode::Scale
                                            : univex::gizmo::GizmoMode::Move;
                    } else {
                        effectiveMode = univex::gizmo::GizmoMode::Move;
                    }
                }
            }
        }

        UVE::Editor::EditorToolSessionModeUVE sessionMode{};
        switch (effectiveMode) {
            case univex::gizmo::GizmoMode::Move:
                sessionMode = UVE::Editor::EditorToolSessionModeUVE::Translate;
                break;
            case univex::gizmo::GizmoMode::Rotate:
                sessionMode = UVE::Editor::EditorToolSessionModeUVE::Rotate;
                break;
            default:
                sessionMode = UVE::Editor::EditorToolSessionModeUVE::Scale;
                break;
        }

        // Capture where the drag started, in the handle's own terms. Every later frame reports its
        // value the same way and subtracts this one, so the amount fed to the gesture is always
        // measured from the press rather than accumulated frame to frame.
        if (!CaptureDragReferenceUVE(*placement, effectiveMode, pick.handle, rayOrigin, rayDirection,
                                     dragPressValue_)) {
            return false;
        }
        if (!editor_.BeginTransformGestureUVE(sessionMode)) {
            return false;
        }

        dragHandle_ = pick.handle;
        dragMode_ = effectiveMode;
        dragPivot_ = placement->pivot;
        return true;
    }

    /// One scalar and one point, enough to express every handle's press-time reference: the
    /// distance along an axis, the world point on a plane, or the angle around a ring.
    struct DragReferenceUVE final {
        float scalar = 0.0F;
        univex::math::Vec3 point{};
    };

    [[nodiscard]] bool CaptureDragReferenceUVE(const GizmoPlacementUVE& placement,
                                               const univex::gizmo::GizmoMode mode,
                                               const univex::gizmo::GizmoHandleUVE handle,
                                               const univex::math::Vec3& rayOrigin,
                                               const univex::math::Vec3& rayDirection,
                                               DragReferenceUVE& outReference) const {
        using univex::gizmo::GizmoHandleUVE;

        if (const std::optional<univex::math::Vec3> axis =
                univex::gizmo::AxisDirectionForHandleUVE(handle);
            axis.has_value()) {
            if (mode == univex::gizmo::GizmoMode::Rotate) {
                const std::optional<float> angle = univex::gizmo::ProjectRayOntoRingAngleUVE(
                    rayOrigin, rayDirection, placement.pivot, *axis);
                if (!angle.has_value()) {
                    return false;
                }
                outReference.scalar = *angle;
                return true;
            }
            const std::optional<float> along = univex::gizmo::ProjectRayOntoAxisUVE(
                rayOrigin, rayDirection, placement.pivot, *axis);
            if (!along.has_value()) {
                return false;
            }
            outReference.scalar = *along;
            return true;
        }

        if (const std::optional<univex::math::Vec3> normal = PlaneHandleNormalUVE(handle);
            normal.has_value()) {
            const std::optional<univex::math::Vec3> point = univex::gizmo::ProjectRayOntoPlaneUVE(
                rayOrigin, rayDirection, placement.pivot, *normal);
            if (!point.has_value()) {
                return false;
            }
            outReference.point = *point;
            return true;
        }

        if (handle == GizmoHandleUVE::Uniform && mode == univex::gizmo::GizmoMode::Scale) {
            // Uniform scale has no axis to run along, so it reads the cursor's distance from the
            // pivot in the view plane - drag away from the object to grow it, toward it to shrink.
            const std::optional<univex::math::Vec3> point = univex::gizmo::ProjectRayOntoPlaneUVE(
                rayOrigin, rayDirection, placement.pivot, placement.viewDirection);
            if (!point.has_value()) {
                return false;
            }
            outReference.scalar = univex::math::Length(*point - placement.pivot);
            return true;
        }

        // Anything else (the rotate gizmo's screen-space ring, a plane handle in Scale mode) has
        // no single-axis command to drive yet, so it stays pickable but not draggable rather than
        // being mapped onto an axis it does not mean.
        return false;
    }

    [[nodiscard]] static std::optional<univex::math::Vec3> PlaneHandleNormalUVE(
        const univex::gizmo::GizmoHandleUVE handle) {
        using univex::gizmo::GizmoHandleUVE;
        switch (handle) {
            case GizmoHandleUVE::PlaneXY: return univex::math::Vec3{0.0F, 0.0F, 1.0F};
            case GizmoHandleUVE::PlaneYZ: return univex::math::Vec3{1.0F, 0.0F, 0.0F};
            case GizmoHandleUVE::PlaneZX: return univex::math::Vec3{0.0F, 1.0F, 0.0F};
            default: return std::nullopt;
        }
    }

    [[nodiscard]] static UVE::Editor::EditorTransformAxisUVE EditorAxisForHandleUVE(
        const univex::gizmo::GizmoHandleUVE handle) {
        using univex::gizmo::GizmoHandleUVE;
        switch (handle) {
            case GizmoHandleUVE::AxisX: return UVE::Editor::EditorTransformAxisUVE::X;
            case GizmoHandleUVE::AxisY: return UVE::Editor::EditorTransformAxisUVE::Y;
            case GizmoHandleUVE::AxisZ: return UVE::Editor::EditorTransformAxisUVE::Z;
            default: return UVE::Editor::EditorTransformAxisUVE::None;
        }
    }

    [[nodiscard]] bool ContinueGizmoDragUVE(const int width, const int height) {
        // Esc abandons the drag and puts the object back, the universal convention.
        if (In().IsKeyPressedUVE(VpKeyUVE::Escape)) {
            static_cast<void>(editor_.CancelTransformGestureUVE());
            EndGizmoDragUVE();
            return true;
        }
        if (In().IsReleasedUVE(VpBtnUVE::Left)) {
            static_cast<void>(editor_.CommitTransformGestureUVE());
            EndGizmoDragUVE();
            return true;
        }

        const std::optional<GizmoPlacementUVE> placement = CurrentGizmoPlacementUVE(height);
        if (!placement.has_value()) {
            static_cast<void>(editor_.CancelTransformGestureUVE());
            EndGizmoDragUVE();
            return true;
        }

        const UVE::Math::Vector2UVE imageOrigin = In().imageOrigin;
        const UVE::Editor::EditorViewportInputUVE& io = In();
        univex::math::Vec3 rayOrigin{};
        univex::math::Vec3 rayDirection{};
        CursorRayUVE(width, height, io.mousePos.x - imageOrigin.x, io.mousePos.y - imageOrigin.y,
                     rayOrigin, rayDirection);

        // The pivot is frozen at the press: it moves with the object during a translate, and
        // re-reading it each frame would make the gizmo chase itself.
        ApplyDragPreviewUVE(rayOrigin, rayDirection, placement->viewDirection);
        return true;
    }

    void ApplyDragPreviewUVE(const univex::math::Vec3& rayOrigin,
                             const univex::math::Vec3& rayDirection,
                             const univex::math::Vec3& viewDirection) {
        using univex::gizmo::GizmoHandleUVE;

        if (const std::optional<univex::math::Vec3> normal = PlaneHandleNormalUVE(dragHandle_);
            normal.has_value()) {
            const std::optional<univex::math::Vec3> point =
                univex::gizmo::ProjectRayOntoPlaneUVE(rayOrigin, rayDirection, dragPivot_, *normal);
            if (!point.has_value()) {
                return; // grazing the plane: hold the last good preview rather than jumping
            }
            const univex::math::Vec3 delta = *point - dragPressValue_.point;
            static_cast<void>(editor_.PreviewTranslateGestureUVE(
                univex::integration::ToUveVector3UVE(delta)));
            return;
        }

        if (dragHandle_ == GizmoHandleUVE::Uniform) {
            const std::optional<univex::math::Vec3> point = univex::gizmo::ProjectRayOntoPlaneUVE(
                rayOrigin, rayDirection, dragPivot_, viewDirection);
            if (!point.has_value()) {
                return;
            }
            const float radius = univex::math::Length(*point - dragPivot_);
            static_cast<void>(editor_.PreviewTransformGestureUVE(
                UVE::Editor::EditorTransformAxisUVE::None, radius - dragPressValue_.scalar));
            return;
        }

        const std::optional<univex::math::Vec3> axis =
            univex::gizmo::AxisDirectionForHandleUVE(dragHandle_);
        if (!axis.has_value()) {
            return;
        }
        const UVE::Editor::EditorTransformAxisUVE editorAxis = EditorAxisForHandleUVE(dragHandle_);

        if (dragMode_ == univex::gizmo::GizmoMode::Rotate) {
            const std::optional<float> angle =
                univex::gizmo::ProjectRayOntoRingAngleUVE(rayOrigin, rayDirection, dragPivot_, *axis);
            if (!angle.has_value()) {
                return;
            }
            // The ring's angle wraps at +-pi; a drag reads the short way round, which is the only
            // way a pointer can have travelled between two frames.
            static_cast<void>(editor_.PreviewTransformGestureUVE(
                editorAxis, univex::gizmo::ShortestAngleDeltaUVE(dragPressValue_.scalar, *angle)));
            return;
        }

        const std::optional<float> along =
            univex::gizmo::ProjectRayOntoAxisUVE(rayOrigin, rayDirection, dragPivot_, *axis);
        if (!along.has_value()) {
            return;
        }
        static_cast<void>(
            editor_.PreviewTransformGestureUVE(editorAxis, *along - dragPressValue_.scalar));
    }

    void EndGizmoDragUVE() {
        dragHandle_ = univex::gizmo::GizmoHandleUVE::None;
    }

    // Click-to-select. Until now selection came only from the Hierarchy panel, so the 3D view was
    // something to look at rather than something to work in.
    //
    // The whole difficulty is that the left button already means "orbit". A press is therefore not
    // committed to either meaning until release: travel more than the slop and it was a drag, so
    // the camera keeps it and nothing is selected; release within the slop and it was a click, so
    // it picks. That is the same press-relative accumulation UpdateNavGizmoInteractionUVE uses,
    // and the reason both read GetMouseDragDelta rather than the per-frame delta - a slow drag of
    // many tiny movements never exceeds a per-frame threshold.
    //
    // `suppressed` is true when the nav gizmo already owns this gesture, in which case the click
    // belongs to it and must not also fall through to selection.
    void UpdateSelectionFromMouseUVE(const int width, const int height, const bool suppressed) {
        if (suppressed) {
            selectionPressActive_ = false;
            return;
        }

        const UVE::Editor::EditorViewportInputUVE& io = In();
        if (!pointerOverOverlay_ && In().hovered &&
            In().IsClickedUVE(VpBtnUVE::Left)) {
            const UVE::Math::Vector2UVE imageOrigin = In().imageOrigin;
            selectionPressActive_ = true;
            selectionPressX_ = io.mousePos.x - imageOrigin.x;
            selectionPressY_ = io.mousePos.y - imageOrigin.y;
        }

        if (!selectionPressActive_ || !In().IsReleasedUVE(VpBtnUVE::Left)) {
            return;
        }
        selectionPressActive_ = false;

        const UVE::Math::Vector2UVE dragDelta = In().DragDeltaUVE(VpBtnUVE::Left);
        if ((std::fabs(dragDelta.x) + std::fabs(dragDelta.y)) > kNavClickSlopPixelsUVE) {
            return; // that was an orbit, not a click
        }

        const univex::integration::EntityPickResultUVE pick = univex::integration::PickEntityAtPixelUVE(
            entityManager_, camera_, width, height, selectionPressX_, selectionPressY_);

        // Ctrl extends the selection, matching every other multi-select surface in the editor;
        // a plain click replaces it, and a plain click on nothing clears it. Ctrl-clicking empty
        // space deliberately does nothing rather than clearing, so a mis-aimed extend does not
        // throw away a selection the user spent time building.
        const bool extend = io.ctrl;
        if (!pick.hit) {
            if (!extend) {
                editor_.ClearSelectionUVE();
            }
            return;
        }
        if (extend) {
            editor_.ToggleEntitySelectionUVE(pick.entity);
        } else {
            editor_.SelectEntityUVE(pick.entity);
        }
    }

    // Right-click an entity -> select it and open the floating "Scripting" toolbar anchored at
    // its projected screen position (editor_.DrawEntityContextToolbarUVE draws it; this method
    // only decides whether to arm it). Same click-vs-drag shape as UpdateSelectionFromMouseUVE
    // above, but for the right button, which today only drives camera pan on an actual drag
    // (UpdateCameraFromMouseUVE's IsMouseDragging(Right) check below) - a clean right-click
    // currently falls through that check and does nothing, which is exactly the gap this fills.
    //
    // A right-drag beyond the slop is left alone: UpdateCameraFromMouseUVE already panned the
    // camera live during the drag, and an in-flight pan should not also fight whatever toolbar
    // state already existed before the drag started.
    void UpdateEntityContextToolbarFromMouseUVE(const int width, const int height, const bool suppressed) {
        if (suppressed) {
            contextToolbarPressActive_ = false;
            return;
        }

        const UVE::Editor::EditorViewportInputUVE& io = In();
        if (!pointerOverOverlay_ && In().hovered &&
            In().IsClickedUVE(VpBtnUVE::Right)) {
            const UVE::Math::Vector2UVE imageOrigin = In().imageOrigin;
            contextToolbarPressActive_ = true;
            contextToolbarPressX_ = io.mousePos.x - imageOrigin.x;
            contextToolbarPressY_ = io.mousePos.y - imageOrigin.y;
        }

        if (!contextToolbarPressActive_ || !In().IsReleasedUVE(VpBtnUVE::Right)) {
            return;
        }
        contextToolbarPressActive_ = false;

        const UVE::Math::Vector2UVE dragDelta = In().DragDeltaUVE(VpBtnUVE::Right);
        if ((std::fabs(dragDelta.x) + std::fabs(dragDelta.y)) > kNavClickSlopPixelsUVE) {
            return; // that was a pan, not a click - leave any existing toolbar state alone
        }

        const univex::integration::EntityPickResultUVE pick = univex::integration::PickEntityAtPixelUVE(
            entityManager_, camera_, width, height, contextToolbarPressX_, contextToolbarPressY_);
        if (!pick.hit) {
            editor_.ClearEntityContextToolbarUVE();
            return;
        }

        editor_.SelectEntityUVE(pick.entity);

        if (!entityManager_.HasComponentUVE<UVE::Scene::WorldTransformComponentUVE>(pick.entity)) {
            editor_.ClearEntityContextToolbarUVE();
            return;
        }
        const auto& worldTransform =
            entityManager_.GetComponentUVE<UVE::Scene::WorldTransformComponentUVE>(pick.entity);
        float anchorPixelX = 0.0F;
        float anchorPixelY = 0.0F;
        if (univex::integration::ProjectWorldPointToPixelUVE(camera_, width, height, worldTransform.worldPosition,
                                                              anchorPixelX, anchorPixelY)) {
            editor_.SetEntityContextToolbarAnchorUVE(pick.entity, anchorPixelX, anchorPixelY);
        } else {
            editor_.ClearEntityContextToolbarUVE();
        }
    }

    // Mirrors app/main.cpp's own GLFW mouse-button/scroll wiring (left-drag orbits, middle/right
    // drag pans, wheel dollies) but sourced from ImGui's IO instead of GLFW callbacks, since input
    // flows through ImGui while the viewport is docked. Called from inside EditorUVE's own
    // ImGui::Begin("Viewport")/End() block (via the render callback this class provides to
    // SetViewportPanelRendererUVE), so In().hovered here correctly reports hover over
    // the Viewport panel specifically. Known simplification versus the GLFW demo: a drag that
    // began inside the panel stops orbiting the moment the cursor leaves it, rather than
    // continuing to track a global drag - acceptable for this integration slice.
    //
    // `suppressOrbit` is true while UpdateNavGizmoInteractionUVE() below owns the current left-
    // button gesture (it started on the nav gizmo) - without it, this method's own
    // IsMouseDragging(Left) check would ALSO orbit the camera from the same drag, double-applying
    // the same mouse delta on top of the nav gizmo's own orbit-while-dragging behavior.
    void UpdateCameraFromMouseUVE(const int framebufferHeight, const bool suppressOrbit) {
        const bool isGamePlaying =
            gameWorkspaceActive_ && (editor_.GetPlayModeStateUVE() == UVE::Editor::EditorPlayModeStateUVE::Playing);
        const bool isMouseCaptured = editor_.IsGameMouseCapturedUVE();

        if (isGamePlaying && isMouseCaptured) {
            const UVE::Editor::EditorViewportInputUVE& io = In();
            const auto& prefs = editor_.GetExtendedPreferencesUVE();
            const float orbitSens = static_cast<float>(std::clamp(prefs.orbitSensitivity, 0.05, 10.0));

            // Smooth free-look from mouse delta when captured in game mode
            if (std::abs(io.mouseDelta.x) > 0.0F || std::abs(io.mouseDelta.y) > 0.0F) {
                camera_.Orbit(io.mouseDelta.x * orbitSens, io.mouseDelta.y * orbitSens);
                editor_.NotifyViewportOrbitedUVE();
            }

            // If there is an active possessed player character, smoothly follow it
            const UVE::Scene::EntityUVE player = UVE::Scene::ResolvePossessedPlayerUVE(entityManager_);
            if (player != UVE::Scene::kInvalidEntityUVE &&
                entityManager_.HasComponentUVE<UVE::Scene::WorldTransformComponentUVE>(player)) {
                const auto& wt = entityManager_.GetComponentUVE<UVE::Scene::WorldTransformComponentUVE>(player);
                if (UVE::Math::IsFiniteUVE(wt.worldPosition)) {
                    camera_.SetTarget(univex::math::Vec3{wt.worldPosition.x, wt.worldPosition.y + 1.2F, wt.worldPosition.z});
                }
            } else {
                // Free-fly spectator camera using WASD + Space/Ctrl/Shift
                univex::math::Vec3 forward = camera_.Target() - camera_.Eye();
                forward = univex::math::Normalize(forward);
                if (univex::math::Length(forward) < 0.001F) {
                    forward = univex::math::Vec3{0.0F, 0.0F, -1.0F};
                }

                const univex::math::Vec3 up{0.0F, 1.0F, 0.0F};
                univex::math::Vec3 right = univex::math::Normalize(univex::math::Cross(forward, up));

                univex::math::Vec3 moveDir{0.0F, 0.0F, 0.0F};
                if (In().IsKeyDownUVE(VpKeyUVE::W)) { moveDir = moveDir + forward; }
                if (In().IsKeyDownUVE(VpKeyUVE::S)) { moveDir = moveDir - forward; }
                if (In().IsKeyDownUVE(VpKeyUVE::D)) { moveDir = moveDir + right; }
                if (In().IsKeyDownUVE(VpKeyUVE::A)) { moveDir = moveDir - right; }
                if (In().IsKeyDownUVE(VpKeyUVE::Space)) { moveDir = moveDir + up; }
                if (In().IsKeyDownUVE(VpKeyUVE::LeftCtrl) || In().IsKeyDownUVE(VpKeyUVE::C)) { moveDir = moveDir - up; }

                const float moveLen = univex::math::Length(moveDir);
                if (moveLen > 0.001F) {
                    const float flySpeed =
                        (In().IsKeyDownUVE(VpKeyUVE::LeftShift) ? 22.0F : 8.0F) * std::max(0.001F, io.deltaTime);
                    moveDir = moveDir * (flySpeed / moveLen);
                    camera_.SetTarget(camera_.Target() + moveDir);
                }
            }
            return;
        }

        // Same overlay guard as selection and the handle drag: pressing a toolbar bubble must not
        // also start orbiting the scene behind it.
        if (pointerOverOverlay_ || !In().hovered) {
            return;
        }
        const auto& prefs = editor_.GetExtendedPreferencesUVE();
        const float orbitSens = static_cast<float>(std::clamp(prefs.orbitSensitivity, 0.05, 10.0));
        const float panSens = static_cast<float>(std::clamp(prefs.panSensitivity, 0.05, 10.0));
        const float zoomSens = static_cast<float>(std::clamp(prefs.zoomSensitivity, 0.05, 10.0));

        const UVE::Editor::EditorViewportInputUVE& io = In();
        if (!suppressOrbit && In().IsDraggingUVE(VpBtnUVE::Left, 0.0F)) {
            camera_.Orbit(io.mouseDelta.x * orbitSens, io.mouseDelta.y * orbitSens);
            editor_.NotifyViewportOrbitedUVE();
        } else if (In().IsDraggingUVE(VpBtnUVE::Middle, 0.0F) ||
                   In().IsDraggingUVE(VpBtnUVE::Right, 0.0F)) {
            camera_.Pan(io.mouseDelta.x * panSens, io.mouseDelta.y * panSens, framebufferHeight);
        }
        if (io.wheel != 0.0F) {
            camera_.Dolly(io.wheel * zoomSens);
        }
    }

    // Unreal's Ctrl+digit (store) / digit (restore) viewport bookmarks, plus F-to-focus - the
    // live consumer of Marker3D in the real editor. Same hovered-panel routing
    // UpdateCameraFromMouseUVE() uses, and deliberately inert while a text field owns the
    // keyboard so typing a digit into an input never hijacks the camera. All bookmark state and
    // marker composition live in EditorUVE (tested there); this method only translates between
    // those plain orbit poses and this OrbitCamera - it owns no camera logic of its own.
    // Named views from the keyboard: the keypad layout (7 Top, 1 Front, 3 Right, Ctrl for the
    // opposite side, 5 to switch projection), with Alt+digit as the same thing on keyboards that
    // have no keypad. Alt keeps them clear of the bookmarks, which own plain and Ctrl+digit.
    // Same hovered-panel and text-input routing as the bookmarks.
    void UpdateViewportViewHotkeysUVE() {
        const UVE::Editor::EditorViewportInputUVE& io = In();
        if (!In().hovered || io.wantTextInput) {
            return;
        }
        struct DigitKeysUVE {
            int digit;
            VpKeyUVE keypad;
            VpKeyUVE row;
        };
        static constexpr DigitKeysUVE kKeysUVE[] = {
            {1, VpKeyUVE::Keypad1, VpKeyUVE::Num1},
            {3, VpKeyUVE::Keypad3, VpKeyUVE::Num3},
            {5, VpKeyUVE::Keypad5, VpKeyUVE::Num5},
            {7, VpKeyUVE::Keypad7, VpKeyUVE::Num7},
        };
        for (const DigitKeysUVE& keys : kKeysUVE) {
            const bool pressed = In().IsKeyPressedUVE(keys.keypad) ||
                                 (io.alt && In().IsKeyPressedUVE(keys.row));
            if (!pressed) {
                continue;
            }
            if (keys.digit == 5) {
                editor_.SetViewportOrthographicUVE(!editor_.IsViewportOrthographicUVE());
                continue;
            }
            if (const auto view = UVE::Editor::EditorUVE::GetViewportViewForKeypadDigitUVE(keys.digit, io.ctrl)) {
                editor_.RequestViewportViewUVE(*view);
            }
        }
    }

    void UpdateViewportBookmarkHotkeysUVE() {
        const UVE::Editor::EditorViewportInputUVE& io = In();
        // Alt+digit belongs to the named views (UpdateViewportViewHotkeysUVE).
        if (!In().hovered || io.wantTextInput || io.alt) {
            return;
        }
        static constexpr VpKeyUVE kDigitKeysUVE[] = {
            VpKeyUVE::Num1, VpKeyUVE::Num2, VpKeyUVE::Num3, VpKeyUVE::Num4, VpKeyUVE::Num5,
            VpKeyUVE::Num6, VpKeyUVE::Num7, VpKeyUVE::Num8, VpKeyUVE::Num9, VpKeyUVE::Num0,
        };
        for (std::size_t order = 0U; order < 10U; ++order) {
            const std::size_t slot = (order + 1U) % 10U; // 1..9,0 like a real keyboard row
            if (!In().IsKeyPressedUVE(kDigitKeysUVE[order])) {
                continue;
            }
            if (io.ctrl) {
                const univex::math::Vec3 target = camera_.Target();
                const bool stored = editor_.SetViewportBookmarkUVE(
                    slot, UVE::Editor::EditorViewportBookmarkUVE{
                               UVE::Math::Vector3UVE{target.x, target.y, target.z},
                               camera_.Yaw(), camera_.Pitch(), camera_.Distance()});
                // A live OrbitCamera only ever produces well-formed poses, so failure is
                // impossible here by construction; the bool is consumed, not ignored.
                (void)stored;

                continue;
            }
            const std::optional<UVE::Editor::EditorViewportBookmarkUVE> bookmark =
                editor_.GetViewportBookmarkUVE(slot);
            if (!bookmark.has_value()) {
                continue; // an empty slot restores nothing - Unreal does the same
            }
            editor_.NotifyViewportOrbitedUVE(); // a restored pose is a free view, not a named one
            camera_.CancelAnimation();
            camera_.SetTarget(univex::integration::FromUveVector3UVE(bookmark->target));
            camera_.SetYawPitch(bookmark->yawRadians, bookmark->pitchRadians);
            camera_.SetDistance(bookmark->distance);
        }
        // Goes through the editor like the hierarchy's Focus in Viewport, so both share
        // FocusCameraOnEntityUVE() and the key does nothing for an object that cannot be focused.
        if (In().IsKeyPressedUVE(VpKeyUVE::F) && !io.ctrl) {
            static_cast<void>(editor_.RequestViewportFocusUVE(editor_.GetSelectedEntityUVE()));
        }
    }

    void FocusCameraOnEntityUVE(const UVE::Scene::EntityUVE entity) {
        // A Marker3D flies the camera INTO the marker's named viewpoint (the thing Godot's inert
        // Marker3D cannot do); anything else gets a plain re-target focus.
        if (const std::optional<UVE::Editor::EditorViewportBookmarkUVE> markerView =
                editor_.ComposeMarker3DFocusBookmarkUVE(entity)) {
            editor_.NotifyViewportOrbitedUVE(); // the marker's viewpoint is a free view, not a named one
            camera_.CancelAnimation();
            camera_.SetTarget(univex::integration::FromUveVector3UVE(markerView->target));
            camera_.SetYawPitch(markerView->yawRadians, markerView->pitchRadians);
            camera_.SetDistance(markerView->distance);
        } else if (const std::optional<UVE::Math::Vector3UVE> focusTarget =
                       editor_.ResolveEntityFocusTargetUVE(entity)) {
            camera_.CancelAnimation();
            camera_.SetTarget(univex::integration::FromUveVector3UVE(*focusTarget));
        }
    }

    // Returns true and fills the nav-local position if the cursor is over the orientation gizmo -
    // ports app/main.cpp's own CursorOverNavGizmo() (the standalone GLFW demo, where this already
    // works) into the ImGui-hosted real editor. `fbX`/`fbY` are pixel coordinates relative to the
    // Viewport panel's own rendered image top-left, matching NavViewportRectFor()'s own convention.
    [[nodiscard]] bool CursorOverNavGizmoUVE(int width, int height, float fbX, float fbY,
                                            float& outLocalX, float& outLocalY) const {
        if (!renderPass_.has_value() || !renderPass_->Settings().viewGizmos) {
            return false;
        }
        const univex::app::NavViewportRect rect =
            univex::app::ViewportRenderPass::NavViewportRectFor(renderPass_->Style(), width, height);
        if (rect.size <= 0) {
            return false;
        }
        // rect.y is a GL viewport origin (bottom-left); convert to top-left, matching fbY's own
        // top-left-origin convention (the same one IInputSystemUVE::GetMousePositionUVE() uses).
        const float top = static_cast<float>(height - rect.y - rect.size);
        const float left = static_cast<float>(rect.x);
        if (fbX < left || fbX > left + static_cast<float>(rect.size)) {
            return false;
        }
        if (fbY < top || fbY > top + static_cast<float>(rect.size)) {
            return false;
        }
        outLocalX = fbX - left;
        outLocalY = fbY - top;
        return true;
    }

    // Handles both nav-gizmo gestures - drag-anywhere-on-it orbits exactly like dragging the scene,
    // click-a-ball snaps the camera to look down that axis - mirroring app/main.cpp's own
    // OnMouseButton()/OnCursorPos() handling for the standalone demo, just driven by ImGui's per-
    // frame IO instead of GLFW press/release/move callbacks. Returns true while this gesture owns
    // the left mouse button, so UpdateCameraFromMouseUVE() knows to suppress its own generic orbit.
    [[nodiscard]] bool UpdateNavGizmoInteractionUVE(const int width, const int height) {
        if (!In().hovered && !navDragging_) {
            return false;
        }
        const UVE::Editor::EditorViewportInputUVE& io = In();
        const UVE::Math::Vector2UVE imageOrigin = In().imageOrigin;
        const float fbX = io.mousePos.x - imageOrigin.x;
        const float fbY = io.mousePos.y - imageOrigin.y;

        if (!navDragging_) {
            if (In().IsClickedUVE(VpBtnUVE::Left)) {
                float localX = 0.0F;
                float localY = 0.0F;
                if (CursorOverNavGizmoUVE(width, height, fbX, fbY, localX, localY)) {
                    navDragging_ = true;
                    navDragMoved_ = false;
                    navPressX_ = localX;
                    navPressY_ = localY;
                    camera_.CancelAnimation();
                }
            }
            return false;
        }

        // A gesture that started on the gizmo: still deciding click vs. drag, or already
        // committed to orbiting. GetMouseDragDelta accumulates from the original press position,
        // matching the demo's own "press-relative slop" check exactly (not per-frame delta, which
        // would never exceed the threshold for a series of tiny frame-to-frame movements).
        if (!navDragMoved_) {
            const UVE::Math::Vector2UVE dragDelta = In().DragDeltaUVE(VpBtnUVE::Left);
            if ((std::fabs(dragDelta.x) + std::fabs(dragDelta.y)) > kNavClickSlopPixelsUVE) {
                navDragMoved_ = true;
            }
        }
        if (navDragMoved_) {
            camera_.Orbit(io.mouseDelta.x, io.mouseDelta.y);
            editor_.NotifyViewportOrbitedUVE();
        }

        if (In().IsReleasedUVE(VpBtnUVE::Left)) {
            if (!navDragMoved_) {
                const univex::gizmo::NavPickResult pick = univex::gizmo::PickNavGizmo(
                    renderPass_->Style(), univex::app::ViewportRenderPass::NavViewMatrix(camera_),
                    navPressX_, navPressY_, static_cast<float>(renderPass_->Style().navPixelSize));
                if (pick.hit) {
                    // Through the editor, like the toolbar menu, so the label, the projection and
                    // the camera all agree; the snap itself happens when the request is applied.
                    editor_.RequestViewportViewUVE(NamedViewForDirectionUVE(pick.direction));
                }
            }
            navDragging_ = false;
            navDragMoved_ = false;
        }
        return true;
    }

    UVE::Editor::EditorUVE& editor_;
    // The last view request applied to the camera; see ViewportOverlayStateUVE::viewRequestSerial.
    std::uint32_t appliedViewRequestSerial_ = 0U;
    // The last focus request applied; see ViewportOverlayStateUVE::focusRequestSerial.
    std::uint32_t appliedFocusRequestSerial_ = 0U;
    UVE::Core::EngineCoreUVE& engine_;

public:
    [[nodiscard]] const UVE::Core::EngineServicesUVE* ServicesUVE() const noexcept { return &services_; }

private:
    UVE::Core::EngineServicesUVE& services_;
    UVE::Scene::IEntityManagerUVE& entityManager_;
    univex::integration::EditorMeshLayerUVE meshLayer_;
    UVE::Editor::EditorUVE::ViewportContextUVE context_;
    std::optional<univex::app::ViewportRenderPass> renderPass_;
    // The studio view's sky, ground and faded floor (see ViewportOverlayStateUVE::studioView).
    // Optional: a driver that cannot build its shaders shows the plain backdrop instead.
    std::optional<univex::render::StudioBackdropRenderer> studio_;
    bool studioTried_ = false;
    bool studioView_ = false;
    std::uint32_t appliedStudioFramingSerial_ = 0U;
    // The editing camera's pose from before the studio view, put back when it ends.
    struct CameraPoseUVE final {
        univex::math::Vec3 target{};
        float yaw = 0.0F;
        float pitch = 0.0F;
        float distance = 1.0F;
        bool orthographic = false;
    };
    std::optional<CameraPoseUVE> poseBeforeStudio_;
    /// The editing camera as Play found it; Stop restores it.
    std::optional<CameraPoseUVE> poseBeforePlay_;
    std::optional<CameraPoseUVE> poseBeforePreview_;
    bool previewing_ = false;

    // The fly into a Camera3D (and back out): the editor camera eased between two poses.
    static constexpr float kCameraFlightSecondsUVE = 0.45F;
    struct CameraFlightPoseUVE final {
        univex::math::Vec3 target{};
        float yaw = 0.0F;
        float pitch = 0.0F;
        float distance = 1.0F;
        float fovY = 0.87F;
    };
    struct CameraFlightUVE final {
        CameraFlightPoseUVE from;
        CameraFlightPoseUVE to;
        float t = 0.0F;
        bool entering = true;
    };
    std::optional<CameraFlightUVE> flight_;
    float fovBeforePreview_ = 0.87F;
    UVE::Scene::EntityUVE lastPreviewCamera_ = UVE::Scene::kInvalidEntityUVE;

    [[nodiscard]] CameraFlightPoseUVE CurrentFlightPoseUVE() const {
        return CameraFlightPoseUVE{camera_.Target(), camera_.Yaw(), camera_.Pitch(), camera_.Distance(),
                                   camera_.Settings().fovYRadians};
    }
    void ApplyFlightPoseUVE(const CameraFlightPoseUVE& pose) {
        camera_.SetTarget(pose.target);
        camera_.SetYawPitch(pose.yaw, pose.pitch);
        camera_.SetDistance(pose.distance);
        camera_.Settings().fovYRadians = pose.fovY;
    }
    /// The editor-camera pose that sees what `entity` (a Camera3D) sees: its eye at the camera,
    /// looking down its -Z, with its field of view.
    [[nodiscard]] std::optional<CameraFlightPoseUVE> CameraEntityPoseUVE(const UVE::Scene::EntityUVE entity) const {
        if (entity == UVE::Scene::kInvalidEntityUVE || !entityManager_.IsAliveUVE(entity) ||
            !entityManager_.HasComponentUVE<UVE::Scene::WorldTransformComponentUVE>(entity)) {
            return std::nullopt;
        }
        const auto& world = entityManager_.GetComponentUVE<UVE::Scene::WorldTransformComponentUVE>(entity);
        const UVE::Math::Vector3UVE f =
            UVE::Math::RotateVectorUVE(world.worldRotation, UVE::Math::Vector3UVE{0.0F, 0.0F, -1.0F});
        const float length = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
        if (!(length > 1e-5F)) {
            return std::nullopt;
        }
        const univex::math::Vec3 forward{f.x / length, f.y / length, f.z / length};
        const float distance = std::max(1.0F, camera_.Distance() * 0.25F);
        const univex::math::Vec3 eye{world.worldPosition.x, world.worldPosition.y, world.worldPosition.z};
        CameraFlightPoseUVE pose;
        pose.target = eye + forward * distance;
        pose.distance = distance;
        // Eye = target + offset * distance with offset = -forward (OrbitCamera::Eye()).
        pose.pitch = std::asin(std::clamp(-forward.y, -1.0F, 1.0F));
        pose.yaw = std::atan2(-forward.z, -forward.x);
        pose.fovY = camera_.Settings().fovYRadians;
        if (entityManager_.HasComponentUVE<UVE::Scene::CameraComponentUVE>(entity)) {
            const float degrees = entityManager_.GetComponentUVE<UVE::Scene::CameraComponentUVE>(entity).fieldOfViewDegrees;
            if (degrees > 1.0F && degrees < 179.0F) {
                pose.fovY = degrees * 3.14159265F / 180.0F;
            }
        }
        return pose;
    }
    // Set each frame by ApplyOverlayStateUVE(), read by UpdateSelectionGizmoUVE() so it can force
    // the transform gizmo off while the Game workspace tab is active (see ApplyOverlayStateUVE's
    // own comment - it already forces the grid off directly, but the gizmo's visibility is decided
    // later in the same frame by selection state, so it needs this stored flag instead).
    bool gameWorkspaceActive_ = false;
    // See ViewportOverlayStateUVE::pointerOverOverlay - the toolbar floats inside this same
    // window, so without this a click on one of its buttons also lands in the scene behind it.
    bool pointerOverOverlay_ = false;
    univex::camera::OrbitCamera camera_;
    // Nav-gizmo click-vs-drag state - see UpdateNavGizmoInteractionUVE()'s own comment.
    bool navDragging_ = false;
    bool navDragMoved_ = false;
    float navPressX_ = 0.0F;
    float navPressY_ = 0.0F;
    // Click-vs-drag state for click-to-select - see UpdateSelectionFromMouseUVE().
    bool selectionPressActive_ = false;
    float selectionPressX_ = 0.0F;
    float selectionPressY_ = 0.0F;
    // Click-vs-drag state for the entity context toolbar - see UpdateEntityContextToolbarFromMouseUVE().
    bool contextToolbarPressActive_ = false;
    float contextToolbarPressX_ = 0.0F;
    float contextToolbarPressY_ = 0.0F;
    // Transform-gizmo drag state - see UpdateGizmoDragUVE(). dragPivot_ and dragPressValue_ are
    // frozen at the press: the object moves during the drag, and re-reading them each frame would
    // make the gesture chase its own result.
    std::optional<univex::math::Vec3> gizmoPivot_;
    // Gizmo geometry kept for a shell that draws the gizmos itself (overlayState.hostDrawsGizmos).
    univex::gizmo::GizmoMesh hostOverlayMesh_;
    univex::gizmo::GizmoMesh hostBonesMesh_;
    std::vector<univex::gizmo::GizmoLine> hostColliderLines_;
    std::size_t hostOverlayOccludedFrom_ = static_cast<std::size_t>(-1);

    // Reports the view's camera, the selection gizmo pivot and the object gizmo geometry through the
    // overlay state's output fields, so a shell can draw the gizmos over the image.
    void ExportViewForHostUVE(const UVE::Editor::EditorUVE::ViewportOverlayStateUVE& overlayState, const int width,
                              const int height, const bool wantOverlay) {
        auto& cam = overlayState.viewCamera;
        const univex::math::Vec3 eye = camera_.Eye();
        const univex::math::Vec3 forward = univex::math::Normalize(camera_.Target() - eye);
        univex::math::Vec3 right = univex::math::Normalize(univex::math::Cross(forward, camera_.Up()));
        const univex::math::Vec3 up = univex::math::Normalize(univex::math::Cross(right, forward));
        cam.valid = true;
        cam.eye = {eye.x, eye.y, eye.z};
        cam.forward = {forward.x, forward.y, forward.z};
        cam.right = {right.x, right.y, right.z};
        cam.up = {up.x, up.y, up.z};
        cam.fovYRadians = camera_.Settings().fovYRadians;
        cam.nearPlane = camera_.NearPlane();
        cam.orthographic = camera_.IsOrthographic();
        cam.orthographicHalfHeight = camera_.OrthographicHalfHeight();
        cam.width = static_cast<float>(width);
        cam.height = static_cast<float>(height);
        overlayState.gizmoPivotValid = gizmoPivot_.has_value();
        if (gizmoPivot_.has_value()) {
            overlayState.gizmoPivot = {gizmoPivot_->x, gizmoPivot_->y, gizmoPivot_->z};
        }
        overlayState.gizmoLines.clear();
        overlayState.gizmoTriangles.clear();
        if (!overlayState.hostDrawsGizmos || !wantOverlay) {
            return;
        }
        const auto a3 = [](const univex::math::Vec3& v) { return std::array<float, 3>{v.x, v.y, v.z}; };
        const auto append = [&](const univex::gizmo::GizmoMesh& mesh) {
            for (const univex::gizmo::GizmoLine& line : mesh.lines) {
                overlayState.gizmoLines.push_back({a3(line.a), a3(line.b), a3(line.color), line.widthPx});
            }
            for (const univex::gizmo::GizmoTriangle& tri : mesh.triangles) {
                overlayState.gizmoTriangles.push_back({a3(tri.a), a3(tri.b), a3(tri.c), a3(tri.color), tri.alpha});
            }
        };
        append(hostOverlayMesh_);
        // Collision shapes hide behind the mesh they wrap, as in the GL path; a faint x-ray of every
        // edge made a solid box read as glass.
        for (std::size_t i = hostOverlayOccludedFrom_; i < hostOverlayMesh_.lines.size(); ++i) {
            overlayState.gizmoLines[overlayState.gizmoLines.size() - hostOverlayMesh_.lines.size() + i].occluded = true;
        }
        append(hostBonesMesh_);
        for (const univex::gizmo::GizmoLine& line : hostColliderLines_) {
            overlayState.gizmoLines.push_back({a3(line.a), a3(line.b), a3(line.color), line.widthPx, true});
        }
    }
    void ExportGridForHostUVE(const UVE::Editor::EditorUVE::ViewportOverlayStateUVE& overlayState, const bool want) {
        auto& out = overlayState.hostGrid;
        const univex::render::GridSettings& grid = renderPass_->Grid().Settings();
        out.visible = want && renderPass_->Settings().viewGrid;
        out.plane = static_cast<int>(grid.plane);
        out.baseSpacing = grid.baseSpacing;
        out.targetCellPixels = grid.targetCellPixels;
        out.lineWidth = grid.lineWidthPixels;
        out.axisWidth = grid.axisWidthPixels;
        const auto rgb = [](const univex::render::GridColor& c) { return std::array<float, 3>{c.r, c.g, c.b}; };
        out.thin = rgb(grid.thinColor);
        out.mid = rgb(grid.midColor);
        out.thick = rgb(grid.thickColor);
        out.thinIntensity = grid.thinIntensity;
        out.midIntensity = grid.midIntensity;
        out.thickIntensity = grid.thickIntensity;
        out.axis = {rgb(grid.axisColorX), rgb(grid.axisColorY), rgb(grid.axisColorZ)};
        out.fadeStart = grid.fadeStartDistanceScale;
        out.fadeEnd = grid.fadeEndDistanceScale;
        out.opacity = grid.opacity;
    }

    // The renderer's scene depth turned into what the shell tests against: the distance along the
    // view's forward axis from the editor camera's eye, at half resolution (the nearest of each 2x2
    // block, so thin geometry still hides what is behind it), read back top-down.
    void ExportSceneDepthForHostUVE(const UVE::Editor::EditorUVE::ViewportOverlayStateUVE& overlayState,
                                    const int width, const int height) {
        overlayState.sceneDepth.clear();
        overlayState.sceneDepthWidth = overlayState.sceneDepthHeight = 0;
        const UVE::Render::SceneDepthExportUVE depth =
            services_.GetRenderer3DUVE().GetLastSceneDepthUVE();
        auto* const glDevice = dynamic_cast<UVE::Render::GlRenderDeviceUVE*>(&services_.GetRenderDeviceUVE());
        if (!depth.valid || glDevice == nullptr || depth.width == 0U || depth.height == 0U || !EnsureHostDepthUVE(width, height)) {
            return;
        }
        const GLuint depthTexture = static_cast<GLuint>(glDevice->GetNativeTextureIdUVE(depth.depthTexture));
        if (depthTexture == 0U) {
            return;
        }
        GLint previousDrawFbo = 0;
        GLint previousReadFbo = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFbo);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFbo);
        std::array<GLint, 4> previousViewport{};
        glGetIntegerv(GL_VIEWPORT, previousViewport.data());
        const GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
        const GLboolean blend = glIsEnabled(GL_BLEND);

        glBindFramebuffer(GL_FRAMEBUFFER, hostDepthFbo_);
        glViewport(0, 0, hostDepthWidth_, hostDepthHeight_);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        hostDepthProgram_->Use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, depthTexture);
        glUniform1i(hostDepthProgram_->UniformLocation("uDepth"), 0);
        glUniformMatrix4fv(hostDepthProgram_->UniformLocation("uInvViewProj"), 1, GL_TRUE,
                           &depth.inverseViewProjection.m[0][0]);
        const univex::math::Vec3 eye = camera_.Eye();
        const univex::math::Vec3 forward = univex::math::Normalize(camera_.Target() - eye);
        glUniform3f(hostDepthProgram_->UniformLocation("uEye"), eye.x, eye.y, eye.z);
        glUniform3f(hostDepthProgram_->UniformLocation("uForward"), forward.x, forward.y, forward.z);
        glUniform2f(hostDepthProgram_->UniformLocation("uOutSize"), static_cast<float>(hostDepthWidth_),
                    static_cast<float>(hostDepthHeight_));
        glBindVertexArray(meshBlitVao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);

        std::vector<float> rows(static_cast<std::size_t>(hostDepthWidth_) * static_cast<std::size_t>(hostDepthHeight_));
        GLint packAlignment = 4;
        glGetIntegerv(GL_PACK_ALIGNMENT, &packAlignment);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, hostDepthFbo_);
        glReadPixels(0, 0, hostDepthWidth_, hostDepthHeight_, GL_RED, GL_FLOAT, rows.data());
        glPixelStorei(GL_PACK_ALIGNMENT, packAlignment);
        overlayState.sceneDepth.resize(rows.size());
        const std::size_t stride = static_cast<std::size_t>(hostDepthWidth_);
        for (int y = 0; y < hostDepthHeight_; ++y) {
            std::copy_n(rows.data() + static_cast<std::size_t>(hostDepthHeight_ - 1 - y) * stride, stride,
                        overlayState.sceneDepth.data() + static_cast<std::size_t>(y) * stride);
        }
        overlayState.sceneDepthWidth = hostDepthWidth_;
        overlayState.sceneDepthHeight = hostDepthHeight_;

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDrawFbo));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFbo));
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        if (depthTest == GL_TRUE) glEnable(GL_DEPTH_TEST);
        if (blend == GL_TRUE) glEnable(GL_BLEND);
    }

    [[nodiscard]] bool EnsureHostDepthUVE(const int width, const int height) {
        if (!hostDepthProgram_.has_value()) {
            std::string error;
            hostDepthProgram_ = univex::render::ShaderProgram::Build(kMeshBlitVertexShaderUVE,
                                                                     kHostDepthFragmentShaderUVE, error);
            if (!hostDepthProgram_.has_value()) {
                UVE_ERROR("uve_editor_app: viewport depth export shader build failed: {}", error);
                hostDepthFailed_ = true;
            }
        }
        if (hostDepthFailed_ || !hostDepthProgram_.has_value() || meshBlitVao_ == 0U) {
            return false;
        }
        const int w = std::max(1, (width + 1) / 2);
        const int h = std::max(1, (height + 1) / 2);
        if (hostDepthFbo_ != 0U && w == hostDepthWidth_ && h == hostDepthHeight_) {
            return true;
        }
        if (hostDepthTexture_ == 0U) glGenTextures(1, &hostDepthTexture_);
        if (hostDepthFbo_ == 0U) glGenFramebuffers(1, &hostDepthFbo_);
        glBindTexture(GL_TEXTURE_2D, hostDepthTexture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, w, h, 0, GL_RED, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
        GLint previousFbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, hostDepthFbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, hostDepthTexture_, 0);
        const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
        hostDepthWidth_ = complete ? w : 0;
        hostDepthHeight_ = complete ? h : 0;
        return complete;
    }

    std::optional<univex::render::ShaderProgram> hostDepthProgram_;
    bool hostDepthFailed_ = false;
    GLuint hostDepthFbo_ = 0U;
    GLuint hostDepthTexture_ = 0U;
    int hostDepthWidth_ = 0;
    int hostDepthHeight_ = 0;

    univex::gizmo::GizmoHandleUVE dragHandle_ = univex::gizmo::GizmoHandleUVE::None;
    univex::gizmo::GizmoMode dragMode_ = univex::gizmo::GizmoMode::Move;
    univex::math::Vec3 dragPivot_{};
    DragReferenceUVE dragPressValue_{};
    bool glewInitialized_ = false;
    GLuint msaaFbo_ = 0U;
    GLuint msaaColorRb_ = 0U;
    GLuint msaaDepthRb_ = 0U;
    GLuint resolveFbo_ = 0U;
    GLuint resolveColorTexture_ = 0U;
    int framebufferWidth_ = 0;
    int framebufferHeight_ = 0;
    std::optional<univex::render::ShaderProgram> meshBlitProgram_;
    GLuint meshBlitVao_ = 0U;
    // Uploaded lazily on first use by DrawUIOverlayUVE() - see that method's own comment for why
    // the editor keeps its own copy of this texture rather than reading Renderer3DUVE's internal
    // one (created only inside its "UIOverlay" render-graph pass, which this panel deliberately
    // never runs - see EditorMeshLayerUVE::RenderUVE()'s own call site comment).
    GLuint uiFontAtlasTexture_ = 0U;
    std::optional<UVE::App::Viewport2DIntegration::Viewport2DHostUVE> viewport2dHost_;

    // Gizmo overlay geometry cache (WP-P4: eliminates per-frame heap churn while orbiting/swiping).
    float cachedOverlayAspect_ = 0.0F;
    UVE::Scene::EntityUVE cachedLookThroughCamera_ = UVE::Scene::kInvalidEntityUVE;
    std::size_t cachedOverlayEntityCount_ = 0;
    bool cachedOverlayActive_ = false;
    float overlayRefreshTimerSeconds_ = 0.0F;

    // Selection outline geometry cache (WP-P4: avoids re-scanning scene & transforming vertices on idle/orbit frames).
    std::vector<UVE::Scene::EntityUVE> lastSelectedEntities_;
    UVE::Scene::EntityUVE lastSelectedEntity_ = UVE::Scene::kInvalidEntityUVE;
    std::vector<univex::render::SelectionOutlineVertex> cachedSelectionTriangles_;
    struct SelectedEntitySnapshotUVE {
        UVE::Scene::EntityUVE entity;
        UVE::Math::Vector3UVE position;
        UVE::Math::QuaternionUVE rotation;
    };
    std::vector<SelectedEntitySnapshotUVE> lastSelectedSnapshots_;

    [[nodiscard]] bool AnySelectedEntityMoved() const {
        for (const auto& snap : lastSelectedSnapshots_) {
            if (!entityManager_.IsAliveUVE(snap.entity) ||
                !entityManager_.HasComponentUVE<UVE::Scene::WorldTransformComponentUVE>(snap.entity)) {
                return true;
            }
            const auto& wt = entityManager_.GetComponentUVE<UVE::Scene::WorldTransformComponentUVE>(snap.entity);
            if (wt.dirty) {
                return true;
            }
            if (snap.position != wt.worldPosition || snap.rotation != wt.worldRotation) {
                return true;
            }
        }
        return false;
    }
};

} // namespace

namespace UVE::App {

struct EditorViewportBackendsUVE::ImplUVE final {
    static constexpr std::size_t kContextCountUVE =
        static_cast<std::size_t>(Editor::EditorUVE::ViewportContextUVE::Count);

    ImplUVE(Editor::EditorUVE& editor, Core::EngineCoreUVE& engine) : editor(editor), engine(engine) {}

    Editor::EditorUVE& editor;
    Core::EngineCoreUVE& engine;
    // Built on first use, for the scene of the document active when the view is drawn, and built
    // again when that changes (the Entity Editor's view draws the entity's own scene).
    std::array<std::optional<ViewportPanelBackendUVE>, kContextCountUVE> backends;
};

EditorViewportBackendsUVE::EditorViewportBackendsUVE(Editor::EditorUVE& editor,
                                                     Core::EngineCoreUVE& engine)
    : m_impl(std::make_unique<ImplUVE>(editor, engine)) {}

EditorViewportBackendsUVE::~EditorViewportBackendsUVE() = default;

void EditorViewportBackendsUVE::ForgetServicesUVE(const Core::EngineServicesUVE& services) {
    for (std::optional<ViewportPanelBackendUVE>& backend : m_impl->backends) {
        if (backend.has_value() && backend->ServicesUVE() == &services) {
            backend.reset();
        }
    }
}

std::uint64_t EditorViewportBackendsUVE::RenderUVE(
    const Editor::EditorUVE::ViewportContextUVE context,
    const Math::Vector2UVE& availableSize,
    Math::Vector2UVE& outUsedSize,
    const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) {
    const std::size_t index = static_cast<std::size_t>(context);
    if (index >= m_impl->backends.size()) {
        return 0U;
    }
    std::optional<ViewportPanelBackendUVE>& backend = m_impl->backends[index];
    Core::EngineServicesUVE& services = m_impl->editor.GetActiveServicesUVE();
    if (!backend.has_value() || backend->ServicesUVE() != &services) {
        backend.reset();
        backend.emplace(m_impl->editor, m_impl->engine, services, context);
    }
    return backend->RenderUVE(availableSize, outUsedSize, overlayState);
}

} // namespace UVE::App
