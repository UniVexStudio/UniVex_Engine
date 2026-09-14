// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "integration/EditorMeshLayer.h"

#include "univex/camera/OrbitCamera.h"

#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/render/gl_render_device_uve.h"
#include "uve/render/i_renderer_3d_uve.h"
#include "uve/render/render_resource_descs_uve.h"
#include "uve/scene/components/camera_component_uve.h"
#include "uve/scene/components/transform_component_uve.h"
#include "uve/scene/i_entity_manager_uve.h"
#include "uve/scene/i_scene_graph_uve.h"

namespace univex::integration {

namespace {

// univex::math::Vec3 -> UVE::Math::Vector3UVE: two structurally-identical {x,y,z} float structs
// from unrelated math libraries (this module stays engine-agnostic - see this file's own header
// comment) - a plain field-by-field copy, not a real conversion.
[[nodiscard]] UVE::Math::Vector3UVE ToUveVectorUVE(const univex::math::Vec3& value) noexcept {
    return UVE::Math::Vector3UVE{value.x, value.y, value.z};
}

} // namespace

EditorMeshLayerUVE::EditorMeshLayerUVE(UVE::Core::EngineServicesUVE& services) : services_(services) {
    UVE::Scene::IEntityManagerUVE& entityManager = services_.GetEntityManagerUVE();
    cameraEntity_ = entityManager.CreateEntityUVE();
    // Identity placeholder - SyncCameraFromOrbitUVE() overwrites this every RenderUVE() call
    // before it is ever read by the renderer. AttachTransformUVE() also wires up this entity's
    // WorldTransformComponentUVE/HierarchyComponentUVE, which SetLocalTransformUVE() needs later.
    services_.GetSceneGraphUVE().AttachTransformUVE(entityManager, cameraEntity_, UVE::Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<UVE::Scene::CameraComponentUVE>(cameraEntity_);
}

EditorMeshLayerUVE::~EditorMeshLayerUVE() {
    DestroyTargetsUVE();
    if (cameraEntity_ != UVE::Scene::kInvalidEntityUVE) {
        services_.GetEntityManagerUVE().DestroyEntityUVE(cameraEntity_);
    }
}

void EditorMeshLayerUVE::SyncCameraFromOrbitUVE(const univex::camera::OrbitCamera& camera,
                                                const float aspectRatio) {
    static_cast<void>(aspectRatio); // CameraSystemUVE derives aspect from the render target itself.
    UVE::Scene::IEntityManagerUVE& entityManager = services_.GetEntityManagerUVE();

    UVE::Scene::CameraComponentUVE& cameraComponent =
        entityManager.GetComponentUVE<UVE::Scene::CameraComponentUVE>(cameraEntity_);
    const float fovDegrees = camera.Settings().fovYRadians * (180.0F / 3.14159265358979323846F);
    cameraComponent.fieldOfViewDegrees = fovDegrees;
    cameraComponent.nearPlane = camera.NearPlane();
    cameraComponent.farPlane = camera.FarPlane();

    // CameraSystemUVE treats local -Z as forward (see camera_system_uve.cpp), so the rotation
    // must point local +Z along the eye-to-target "backward" vector for local -Z to land on the
    // real look direction - TryMakeLookAtUVE's own doc comment states the +Z convention directly.
    const univex::math::Vec3 eye = camera.Eye();
    const univex::math::Vec3 target = camera.Target();
    const univex::math::Vec3 backward{eye.x - target.x, eye.y - target.y, eye.z - target.z};
    UVE::Math::QuaternionUVE rotation{};
    if (!UVE::Math::TryMakeLookAtUVE(ToUveVectorUVE(backward), UVE::Math::Vector3UVE{0.0F, 1.0F, 0.0F}, rotation)) {
        rotation = UVE::Math::QuaternionUVE{};
    }

    UVE::Scene::TransformComponentUVE localTransform;
    localTransform.localPosition = ToUveVectorUVE(eye);
    localTransform.localRotation = rotation;
    services_.GetSceneGraphUVE().SetLocalTransformUVE(entityManager, cameraEntity_, localTransform);
    services_.GetSceneGraphUVE().UpdateUVE(entityManager);
}

bool EditorMeshLayerUVE::EnsureTargetsUVE(const std::uint32_t width, const std::uint32_t height) {
    if (width == targetWidth_ && height == targetHeight_ && colorTarget_ != UVE::Render::kInvalidTextureHandleUVE) {
        return true;
    }
    DestroyTargetsUVE();

    UVE::Render::IRenderDeviceUVE& renderDevice = services_.GetRenderDeviceUVE();
    const UVE::Render::TextureHandleUVE newColorTarget = renderDevice.CreateTextureUVE(
        UVE::Render::TextureDescUVE{width, height, UVE::Render::TextureFormatUVE::RGBA8Unorm, 1});
    const UVE::Render::TextureHandleUVE newDepthTarget = renderDevice.CreateTextureUVE(
        UVE::Render::TextureDescUVE{width, height, UVE::Render::TextureFormatUVE::Depth32Float, 1});
    if (newColorTarget == UVE::Render::kInvalidTextureHandleUVE ||
        newDepthTarget == UVE::Render::kInvalidTextureHandleUVE) {
        if (newColorTarget != UVE::Render::kInvalidTextureHandleUVE) {
            renderDevice.DestroyTextureUVE(newColorTarget);
        }
        if (newDepthTarget != UVE::Render::kInvalidTextureHandleUVE) {
            renderDevice.DestroyTextureUVE(newDepthTarget);
        }
        return false;
    }
    colorTarget_ = newColorTarget;
    depthTarget_ = newDepthTarget;
    targetWidth_ = width;
    targetHeight_ = height;
    return true;
}

void EditorMeshLayerUVE::DestroyTargetsUVE() {
    UVE::Render::IRenderDeviceUVE& renderDevice = services_.GetRenderDeviceUVE();
    if (colorTarget_ != UVE::Render::kInvalidTextureHandleUVE) {
        renderDevice.DestroyTextureUVE(colorTarget_);
        colorTarget_ = UVE::Render::kInvalidTextureHandleUVE;
    }
    if (depthTarget_ != UVE::Render::kInvalidTextureHandleUVE) {
        renderDevice.DestroyTextureUVE(depthTarget_);
        depthTarget_ = UVE::Render::kInvalidTextureHandleUVE;
    }
    targetWidth_ = 0U;
    targetHeight_ = 0U;
}

EditorMeshLayerResultUVE EditorMeshLayerUVE::RenderUVE(const univex::camera::OrbitCamera& camera,
                                                       const std::uint32_t width, const std::uint32_t height) {
    if (width == 0U || height == 0U || !EnsureTargetsUVE(width, height)) {
        return {};
    }

    const float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    SyncCameraFromOrbitUVE(camera, aspectRatio);

    UVE::Render::IRenderer3DUVE& renderer = services_.GetRenderer3DUVE();
    // Keeps this call's own output correctly sized regardless of EngineCoreUVE's own, unrelated
    // per-frame resize of this same shared renderer to the real window's size (its own
    // presentation-surface path, never shown by this panel) - both calls are cheap no-ops when
    // the requested size already matches, and this one always runs immediately before the render
    // below, so this layer's own result is always correct even if the two occasionally race.
    if (!renderer.ResizeTargetsUVE(width, height)) {
        return {};
    }
    renderer.RenderFrameToTargetUVE(services_.GetEntityManagerUVE(), cameraEntity_, colorTarget_, depthTarget_);

    auto* const glRenderDevice = dynamic_cast<UVE::Render::GlRenderDeviceUVE*>(&services_.GetRenderDeviceUVE());
    if (glRenderDevice == nullptr) {
        return {};
    }
    return EditorMeshLayerResultUVE{glRenderDevice->GetNativeTextureIdUVE(colorTarget_),
                                    glRenderDevice->GetNativeTextureIdUVE(depthTarget_)};
}

} // namespace univex::integration
