// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "integration/EditorMeshLayer.h"

#include "integration/MathConversions.h"
#include "univex/camera/orbit_camera.h"

#include "color.h"
#include "quaternion.h"
#include "scalar.h"
#include "vector3.h"
#include "gl_render_device.h"
#include "i_renderer_3d.h"
#include "render_resource_descs.h"
#include "camera_component.h"
#include "editor_internal_entity_component.h"
#include "light_component.h"
#include "light_emitter_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "directional_light_3d.h"
#include "i_scene_graph.h"

namespace univex::integration {

// univex::math <-> UVE::Math conversions come from integration/MathConversions.h - the single,
// documented boundary between this module's own OpenGL-facing math kit and the engine-wide
// UVE::Math library (see that header for what may and may not cross the wall).

EditorMeshLayerUVE::EditorMeshLayerUVE(UVE::Core::EngineServicesUVE& services) : services_(services) {
    cameraEntity_ = CreateCameraProxyEntityUVE();
    headlightEntity_ = CreateHeadlightEntityUVE();
}

UVE::Scene::EntityUVE EditorMeshLayerUVE::CreateCameraProxyEntityUVE() {
    UVE::Scene::IEntityManagerUVE& entityManager = services_.GetEntityManagerUVE();
    const UVE::Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    // Identity placeholder - SyncCameraFromOrbitUVE() overwrites this every RenderUVE() call
    // before it is ever read by the renderer. AttachTransformUVE() also wires up this entity's
    // WorldTransformComponentUVE/HierarchyComponentUVE, which SetLocalTransformUVE() needs later.
    services_.GetSceneGraphUVE().AttachTransformUVE(entityManager, entity, UVE::Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<UVE::Scene::CameraComponentUVE>(entity);
    // Marks this as internal tooling infrastructure, not real document content - see the component's
    // own header comment for why (EditorUVE::GetDocumentRootsUVE() excludes it, so Play-mode's
    // snapshot capture/restore never touches it, and it never shows up in the Scene Hierarchy).
    entityManager.AddComponentUVE<UVE::Scene::EditorInternalEntityComponentUVE>(entity);
    return entity;
}

UVE::Scene::EntityUVE EditorMeshLayerUVE::CreateHeadlightEntityUVE() {
    UVE::Scene::IEntityManagerUVE& entityManager = services_.GetEntityManagerUVE();
    const UVE::Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    services_.GetSceneGraphUVE().AttachTransformUVE(entityManager, entity, UVE::Scene::TransformComponentUVE{});
    // Starts dark: SyncHeadlightUVE() decides every frame whether the scene needs it.
    UVE::Scene::LightComponentUVE light{};
    light.intensity = 0.0F;
    entityManager.AddComponentUVE<UVE::Scene::LightComponentUVE>(entity, light);
    UVE::Scene::LightEmitterComponentUVE emitter{};
    emitter.shadowEnabled = false;
    entityManager.AddComponentUVE<UVE::Scene::LightEmitterComponentUVE>(entity, emitter);
    entityManager.AddComponentUVE<UVE::Scene::EditorInternalEntityComponentUVE>(entity);
    return entity;
}

// A scene with no Light3D of its own would render every lit surface at the world's ambient term
// alone - near-black with the default 0.05 ambient - which would make authoring a first object
// look broken rather than unlit. So the viewport supplies one directional light, and only while
// the scene has none: the moment an author adds a real light, theirs is the only one that counts.
//
// It follows the camera rather than the world, so whatever is being looked at is lit, and it is
// deliberately offset up and to the right of the view direction: a light exactly along the view
// axis lights every visible face equally and flattens the shape it is meant to reveal.
void EditorMeshLayerUVE::SetPreviewLightingUVE(const UVE::Scene::PreviewLightingFrameUVE& frame) {
    previewLighting_ = frame;
    previewLightingSet_ = true;
}

bool EditorMeshLayerUVE::SyncHeadlightUVE(const univex::camera::OrbitCamera& camera) {
    UVE::Scene::IEntityManagerUVE& entityManager = services_.GetEntityManagerUVE();
    if (!entityManager.IsAliveUVE(headlightEntity_)) {
        headlightEntity_ = CreateHeadlightEntityUVE();
    }

    // "The scene has a light" is the renderer's answer, not the document's: a light it drops for
    // any reason must not take the panel's only light down with it.
    const bool sceneHasItsOwnLight = HasContributingSceneLightUVE(entityManager);

    UVE::Scene::LightComponentUVE& headlight =
        entityManager.GetComponentUVE<UVE::Scene::LightComponentUVE>(headlightEntity_);
    headlight.type = UVE::Scene::LightTypeUVE::Directional;
    if (!entityManager.HasComponentUVE<UVE::Scene::LightEmitterComponentUVE>(headlightEntity_)) {
        entityManager.AddComponentUVE<UVE::Scene::LightEmitterComponentUVE>(headlightEntity_);
    }
    UVE::Scene::LightEmitterComponentUVE& headlightEmitter =
        entityManager.GetComponentUVE<UVE::Scene::LightEmitterComponentUVE>(headlightEntity_);

    // The editor's preview sun (Godot's "Preview Sun"), when the host supplies one. Authored
    // lighting still wins here even though the resolver already accounted for it: this layer runs
    // after the frame's settings were resolved, so a light added in between must not be overridden.
    if (previewLightingSet_) {
        const bool sceneHasDirectionalSun = HasAuthoredDirectionalLightNodeUVE(entityManager);
        const bool previewSun =
            previewLighting_.sunActive && !sceneHasDirectionalSun && previewLighting_.sunEnergy > 0.0F;
        headlight.intensity = previewSun ? previewLighting_.sunEnergy : 0.0F;
        headlightEmitter.shadowEnabled = previewSun;
        if (!previewSun) {
            return false;
        }
        // Display-authored colour, converted on the way into linear storage - the same rule the
        // legacy headlight below follows.
        headlight.color = UVE::Math::ColorFromDisplayUVE(previewLighting_.sunColor);
        UVE::Scene::TransformComponentUVE previewTransform;
        previewTransform.localPosition = ToUveVector3UVE(camera.Eye());
        previewTransform.localRotation = previewLighting_.sunRotation;
        services_.GetSceneGraphUVE().SetLocalTransformUVE(entityManager, headlightEntity_, previewTransform);
        return true;
    }

    headlightEmitter.shadowEnabled = false;
    // Warm-white display tint, converted: the headlight is authored, not measured, so what the
    // author picked is a display shade and linear storage must hold its conversion.
    headlight.color = UVE::Math::ColorFromDisplayUVE(UVE::Math::Vector3UVE{1.0F, 0.98F, 0.94F});
    headlight.intensity = sceneHasItsOwnLight ? 0.0F : 1.25F;
    if (sceneHasItsOwnLight) {
        return false; // no point orienting a light that contributes nothing
    }

    const univex::math::Vec3 backward =
        univex::math::Normalize(camera.Eye() - camera.Target());
    const univex::math::Vec3 cameraUp = camera.Up();
    const univex::math::Vec3 right = univex::math::Normalize(univex::math::Cross(cameraUp, backward));
    const univex::math::Vec3 offsetBackward =
        univex::math::Normalize(backward + right * 0.40F + cameraUp * 0.55F);

    UVE::Math::QuaternionUVE rotation{};
    if (!UVE::Math::TryMakeLookAtUVE(ToUveVector3UVE(offsetBackward), ToUveVector3UVE(cameraUp),
                                     rotation)) {
        rotation = UVE::Math::QuaternionUVE{};
    }
    UVE::Scene::TransformComponentUVE localTransform;
    localTransform.localPosition = ToUveVector3UVE(camera.Eye());
    localTransform.localRotation = rotation;
    services_.GetSceneGraphUVE().SetLocalTransformUVE(entityManager, headlightEntity_, localTransform);
    return true;
}

EditorMeshLayerUVE::~EditorMeshLayerUVE() {
    DestroyTargetsUVE();
    UVE::Scene::IEntityManagerUVE& entityManager = services_.GetEntityManagerUVE();
    if (cameraEntity_ != UVE::Scene::kInvalidEntityUVE) {
        entityManager.DestroyEntityUVE(cameraEntity_);
    }
    if (headlightEntity_ != UVE::Scene::kInvalidEntityUVE) {
        entityManager.DestroyEntityUVE(headlightEntity_);
    }
}

void EditorMeshLayerUVE::SyncCameraFromOrbitUVE(const univex::camera::OrbitCamera& camera,
                                                const float aspectRatio) {
    static_cast<void>(aspectRatio); // CameraSystemUVE derives aspect from the render target itself.
    UVE::Scene::IEntityManagerUVE& entityManager = services_.GetEntityManagerUVE();
    // Defensive self-heal: cameraEntity_ is a member cached across frames, so if something ever
    // destroys it out from under this layer (the EditorInternalEntityComponentUVE tag above is the
    // real fix for the one known way that happened - see that component's own doc comment - but a
    // cached handle should never be trusted blindly), recreate it instead of crashing.
    if (!entityManager.IsAliveUVE(cameraEntity_)) {
        cameraEntity_ = CreateCameraProxyEntityUVE();
    }

    UVE::Scene::CameraComponentUVE& cameraComponent =
        entityManager.GetComponentUVE<UVE::Scene::CameraComponentUVE>(cameraEntity_);
    const bool ortho = camera.IsOrthographic();
    const float fovDegrees = UVE::Math::RadToDegUVE(camera.Settings().fovYRadians);
    cameraComponent.projection = ortho ? UVE::Scene::CameraProjectionModeUVE::Orthographic
                                       : UVE::Scene::CameraProjectionModeUVE::Perspective;
    cameraComponent.fieldOfViewDegrees = fovDegrees;
    cameraComponent.orthographicSize = camera.OrthographicHalfHeight();
    cameraComponent.current = false;

    // CameraSystemUVE treats local -Z as forward (see camera_system_uve.cpp), so the rotation
    // must point local +Z along the eye-to-target "backward" vector for local -Z to land on the
    // real look direction - TryMakeLookAtUVE's own doc comment states the +Z convention directly.
    univex::math::Vec3 eye = camera.Eye();
    const univex::math::Vec3 target = camera.Target();
    const univex::math::Vec3 backward{eye.x - target.x, eye.y - target.y, eye.z - target.z};
    if (ortho) {
        // CameraComponentUVE requires nearPlane > 0, whereas zooming in orthographic mode moves
        // OrbitCamera::Eye() close to Target(). Backing the proxy camera up along the view axis
        // keeps foreground geometry in front of nearPlane without pushing uViewPosition beyond
        // directional shadow or light fade range.
        const float orthoPullback =
            std::max(camera.Distance() * 2.0F, camera.OrthographicHalfHeight() * 4.0F);
        const univex::math::Vec3 backDir = univex::math::Normalize(backward);
        eye = eye + backDir * orthoPullback;
        cameraComponent.nearPlane = camera.NearPlane();
        cameraComponent.farPlane = camera.FarPlane() + orthoPullback;
    } else {
        cameraComponent.nearPlane = camera.NearPlane();
        cameraComponent.farPlane = camera.FarPlane();
    }
    const univex::math::Vec3 cameraUp = camera.Up();
    UVE::Math::QuaternionUVE rotation{};
    if (!UVE::Math::TryMakeLookAtUVE(ToUveVector3UVE(backward), ToUveVector3UVE(cameraUp), rotation)) {
        rotation = UVE::Math::QuaternionUVE{};
    }

    UVE::Scene::TransformComponentUVE localTransform;
    localTransform.localPosition = ToUveVector3UVE(eye);
    localTransform.localRotation = rotation;
    services_.GetSceneGraphUVE().SetLocalTransformUVE(entityManager, cameraEntity_, localTransform);
    services_.GetSceneGraphUVE().UpdateUVE(entityManager);
}

bool EditorMeshLayerUVE::EnsureTargetsUVE(const std::uint32_t width, const std::uint32_t height) {
    if (width == 0U || height == 0U) {
        return false;
    }
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
                                                       const std::uint32_t width, const std::uint32_t height,
                                                       const std::optional<UVE::Scene::EntityUVE> gameCameraOverride) {
    if (width == 0U || height == 0U || !EnsureTargetsUVE(width, height)) {
        return {};
    }

    UVE::Scene::IEntityManagerUVE& entityManager = services_.GetEntityManagerUVE();
    const bool overrideUsable =
        gameCameraOverride.has_value() && *gameCameraOverride != UVE::Scene::kInvalidEntityUVE &&
        entityManager.HasComponentUVE<UVE::Scene::WorldTransformComponentUVE>(*gameCameraOverride) &&
        entityManager.HasComponentUVE<UVE::Scene::CameraComponentUVE>(*gameCameraOverride);
    const UVE::Scene::EntityUVE renderCameraEntity = overrideUsable ? *gameCameraOverride : cameraEntity_;
    // Ordered before the camera sync so that sync's own SceneGraph update also propagates the
    // headlight's transform. The game-camera path skips that sync, so it updates explicitly -
    // otherwise the light would keep a stale world transform for as long as the override lasts.
    const bool headlightMoved = SyncHeadlightUVE(camera);
    if (!overrideUsable) {
        const float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
        SyncCameraFromOrbitUVE(camera, aspectRatio);
    } else if (headlightMoved) {
        services_.GetSceneGraphUVE().UpdateUVE(entityManager);
    }

    UVE::Render::IRenderer3DUVE& renderer = services_.GetRenderer3DUVE();
    const auto resetHeadlight = [&]() {
        if (entityManager.IsAliveUVE(headlightEntity_) &&
            entityManager.HasComponentUVE<UVE::Scene::LightComponentUVE>(headlightEntity_)) {
            entityManager.GetComponentUVE<UVE::Scene::LightComponentUVE>(headlightEntity_).intensity = 0.0F;
        }
        if (entityManager.IsAliveUVE(headlightEntity_) &&
            entityManager.HasComponentUVE<UVE::Scene::LightEmitterComponentUVE>(headlightEntity_)) {
            entityManager.GetComponentUVE<UVE::Scene::LightEmitterComponentUVE>(headlightEntity_).shadowEnabled = false;
        }
    };
    // Keeps this call's own output correctly sized regardless of EngineCoreUVE's own, unrelated
    // per-frame resize of this same shared renderer to the real window's size (its own
    // presentation-surface path, never shown by this panel) - both calls are cheap no-ops when
    // the requested size already matches, and this one always runs immediately before the render
    // below, so this layer's own result is always correct even if the two occasionally race.
    if (!renderer.ResizeTargetsUVE(width, height)) {
        resetHeadlight();
        return {};
    }
    renderer.RenderFrameToTargetUVE(entityManager, renderCameraEntity, colorTarget_, depthTarget_, width, height);
    resetHeadlight();

    auto* const glRenderDevice = dynamic_cast<UVE::Render::GlRenderDeviceUVE*>(&services_.GetRenderDeviceUVE());
    if (glRenderDevice == nullptr) {
        return {};
    }
    return EditorMeshLayerResultUVE{glRenderDevice->GetNativeTextureIdUVE(colorTarget_),
                                    glRenderDevice->GetNativeTextureIdUVE(depthTarget_)};
}

} // namespace univex::integration
