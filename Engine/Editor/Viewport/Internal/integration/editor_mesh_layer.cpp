// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "integration/editor_mesh_layer.h"

#include "integration/math_conversions.h"
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

EditorMeshLayer::EditorMeshLayer(UVE::Core::EngineServices& services) : services_(services) {
    cameraEntity_ = CreateCameraProxyEntity();
    headlightEntity_ = CreateHeadlightEntity();
}

UVE::Scene::Entity EditorMeshLayer::CreateCameraProxyEntity() {
    UVE::Scene::IEntityManager& entityManager = services_.GetEntityManager();
    const UVE::Scene::Entity entity = entityManager.CreateEntity();
    // Identity placeholder - SyncCameraFromOrbit() overwrites this every Render() call
    // before it is ever read by the renderer. AttachTransform() also wires up this entity's
    // WorldTransformComponent/HierarchyComponent, which SetLocalTransform() needs later.
    services_.GetSceneGraph().AttachTransform(entityManager, entity, UVE::Scene::TransformComponent{});
    entityManager.AddComponent<UVE::Scene::CameraComponent>(entity);
    // Marks this as internal tooling infrastructure, not real document content - see the component's
    // own header comment for why (Editor::GetDocumentRoots() excludes it, so Play-mode's
    // snapshot capture/restore never touches it, and it never shows up in the Scene Hierarchy).
    entityManager.AddComponent<UVE::Scene::EditorInternalEntityComponent>(entity);
    return entity;
}

UVE::Scene::Entity EditorMeshLayer::CreateHeadlightEntity() {
    UVE::Scene::IEntityManager& entityManager = services_.GetEntityManager();
    const UVE::Scene::Entity entity = entityManager.CreateEntity();
    services_.GetSceneGraph().AttachTransform(entityManager, entity, UVE::Scene::TransformComponent{});
    // Starts dark: SyncHeadlight() decides every frame whether the scene needs it.
    UVE::Scene::LightComponent light{};
    light.intensity = 0.0F;
    entityManager.AddComponent<UVE::Scene::LightComponent>(entity, light);
    UVE::Scene::LightEmitterComponent emitter{};
    emitter.shadowEnabled = false;
    entityManager.AddComponent<UVE::Scene::LightEmitterComponent>(entity, emitter);
    entityManager.AddComponent<UVE::Scene::EditorInternalEntityComponent>(entity);
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
void EditorMeshLayer::SetPreviewLighting(const UVE::Scene::PreviewLightingFrame& frame) {
    previewLighting_ = frame;
    previewLightingSet_ = true;
}

bool EditorMeshLayer::SyncHeadlight(const univex::camera::OrbitCamera& camera) {
    UVE::Scene::IEntityManager& entityManager = services_.GetEntityManager();
    if (!entityManager.IsAlive(headlightEntity_)) {
        headlightEntity_ = CreateHeadlightEntity();
    }

    // "The scene has a light" is the renderer's answer, not the document's: a light it drops for
    // any reason must not take the panel's only light down with it.
    const bool sceneHasItsOwnLight = HasContributingSceneLight(entityManager);

    UVE::Scene::LightComponent& headlight =
        entityManager.GetComponent<UVE::Scene::LightComponent>(headlightEntity_);
    headlight.type = UVE::Scene::LightType::Directional;
    if (!entityManager.HasComponent<UVE::Scene::LightEmitterComponent>(headlightEntity_)) {
        entityManager.AddComponent<UVE::Scene::LightEmitterComponent>(headlightEntity_);
    }
    UVE::Scene::LightEmitterComponent& headlightEmitter =
        entityManager.GetComponent<UVE::Scene::LightEmitterComponent>(headlightEntity_);

    // The editor's preview sun (Godot's "Preview Sun"), when the host supplies one. Authored
    // lighting still wins here even though the resolver already accounted for it: this layer runs
    // after the frame's settings were resolved, so a light added in between must not be overridden.
    if (previewLightingSet_) {
        const bool sceneHasDirectionalSun = HasAuthoredDirectionalLightNode(entityManager);
        const bool previewSun =
            previewLighting_.sunActive && !sceneHasDirectionalSun && previewLighting_.sunEnergy > 0.0F;
        headlight.intensity = previewSun ? previewLighting_.sunEnergy : 0.0F;
        headlightEmitter.shadowEnabled = previewSun;
        if (!previewSun) {
            return false;
        }
        // Display-authored colour, converted on the way into linear storage - the same rule the
        // legacy headlight below follows.
        headlight.color = UVE::Math::ColorFromDisplay(previewLighting_.sunColor);
        UVE::Scene::TransformComponent previewTransform;
        previewTransform.localPosition = ToUveVector3(camera.Eye());
        previewTransform.localRotation = previewLighting_.sunRotation;
        services_.GetSceneGraph().SetLocalTransform(entityManager, headlightEntity_, previewTransform);
        return true;
    }

    headlightEmitter.shadowEnabled = false;
    // Warm-white display tint, converted: the headlight is authored, not measured, so what the
    // author picked is a display shade and linear storage must hold its conversion.
    headlight.color = UVE::Math::ColorFromDisplay(UVE::Math::Vector3{1.0F, 0.98F, 0.94F});
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

    UVE::Math::Quaternion rotation{};
    if (!UVE::Math::TryMakeLookAt(ToUveVector3(offsetBackward), ToUveVector3(cameraUp),
                                     rotation)) {
        rotation = UVE::Math::Quaternion{};
    }
    UVE::Scene::TransformComponent localTransform;
    localTransform.localPosition = ToUveVector3(camera.Eye());
    localTransform.localRotation = rotation;
    services_.GetSceneGraph().SetLocalTransform(entityManager, headlightEntity_, localTransform);
    return true;
}

EditorMeshLayer::~EditorMeshLayer() {
    DestroyTargets();
    UVE::Scene::IEntityManager& entityManager = services_.GetEntityManager();
    if (cameraEntity_ != UVE::Scene::kInvalidEntity) {
        entityManager.DestroyEntity(cameraEntity_);
    }
    if (headlightEntity_ != UVE::Scene::kInvalidEntity) {
        entityManager.DestroyEntity(headlightEntity_);
    }
}

void EditorMeshLayer::SyncCameraFromOrbit(const univex::camera::OrbitCamera& camera,
                                                const float aspectRatio) {
    static_cast<void>(aspectRatio); // CameraSystem derives aspect from the render target itself.
    UVE::Scene::IEntityManager& entityManager = services_.GetEntityManager();
    // Defensive self-heal: cameraEntity_ is a member cached across frames, so if something ever
    // destroys it out from under this layer (the EditorInternalEntityComponent tag above is the
    // real fix for the one known way that happened - see that component's own doc comment - but a
    // cached handle should never be trusted blindly), recreate it instead of crashing.
    if (!entityManager.IsAlive(cameraEntity_)) {
        cameraEntity_ = CreateCameraProxyEntity();
    }

    UVE::Scene::CameraComponent& cameraComponent =
        entityManager.GetComponent<UVE::Scene::CameraComponent>(cameraEntity_);
    const bool ortho = camera.IsOrthographic();
    const float fovDegrees = UVE::Math::RadToDeg(camera.Settings().fovYRadians);
    cameraComponent.projection = ortho ? UVE::Scene::CameraProjectionMode::Orthographic
                                       : UVE::Scene::CameraProjectionMode::Perspective;
    cameraComponent.fieldOfViewDegrees = fovDegrees;
    cameraComponent.orthographicSize = camera.OrthographicHalfHeight();
    cameraComponent.current = false;

    // CameraSystem treats local -Z as forward (see camera_system_uve.cpp), so the rotation
    // must point local +Z along the eye-to-target "backward" vector for local -Z to land on the
    // real look direction - TryMakeLookAt's own doc comment states the +Z convention directly.
    univex::math::Vec3 eye = camera.Eye();
    const univex::math::Vec3 target = camera.Target();
    const univex::math::Vec3 backward{eye.x - target.x, eye.y - target.y, eye.z - target.z};
    if (ortho) {
        // CameraComponent requires nearPlane > 0, whereas zooming in orthographic mode moves
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
    UVE::Math::Quaternion rotation{};
    if (!UVE::Math::TryMakeLookAt(ToUveVector3(backward), ToUveVector3(cameraUp), rotation)) {
        rotation = UVE::Math::Quaternion{};
    }

    UVE::Scene::TransformComponent localTransform;
    localTransform.localPosition = ToUveVector3(eye);
    localTransform.localRotation = rotation;
    services_.GetSceneGraph().SetLocalTransform(entityManager, cameraEntity_, localTransform);
    services_.GetSceneGraph().Update(entityManager);
}

bool EditorMeshLayer::EnsureTargets(const std::uint32_t width, const std::uint32_t height) {
    if (width == 0U || height == 0U) {
        return false;
    }
    if (width == targetWidth_ && height == targetHeight_ && colorTarget_ != UVE::Render::kInvalidTextureHandle) {
        return true;
    }
    DestroyTargets();

    UVE::Render::IRenderDevice& renderDevice = services_.GetRenderDevice();
    const UVE::Render::TextureHandle newColorTarget = renderDevice.CreateTexture(
        UVE::Render::TextureDesc{width, height, UVE::Render::TextureFormat::RGBA8Unorm, 1});
    const UVE::Render::TextureHandle newDepthTarget = renderDevice.CreateTexture(
        UVE::Render::TextureDesc{width, height, UVE::Render::TextureFormat::Depth32Float, 1});
    if (newColorTarget == UVE::Render::kInvalidTextureHandle ||
        newDepthTarget == UVE::Render::kInvalidTextureHandle) {
        if (newColorTarget != UVE::Render::kInvalidTextureHandle) {
            renderDevice.DestroyTexture(newColorTarget);
        }
        if (newDepthTarget != UVE::Render::kInvalidTextureHandle) {
            renderDevice.DestroyTexture(newDepthTarget);
        }
        return false;
    }
    colorTarget_ = newColorTarget;
    depthTarget_ = newDepthTarget;
    targetWidth_ = width;
    targetHeight_ = height;
    return true;
}

void EditorMeshLayer::DestroyTargets() {
    UVE::Render::IRenderDevice& renderDevice = services_.GetRenderDevice();
    if (colorTarget_ != UVE::Render::kInvalidTextureHandle) {
        renderDevice.DestroyTexture(colorTarget_);
        colorTarget_ = UVE::Render::kInvalidTextureHandle;
    }
    if (depthTarget_ != UVE::Render::kInvalidTextureHandle) {
        renderDevice.DestroyTexture(depthTarget_);
        depthTarget_ = UVE::Render::kInvalidTextureHandle;
    }
    targetWidth_ = 0U;
    targetHeight_ = 0U;
}

EditorMeshLayerResult EditorMeshLayer::Render(const univex::camera::OrbitCamera& camera,
                                                       const std::uint32_t width, const std::uint32_t height,
                                                       const std::optional<UVE::Scene::Entity> gameCameraOverride) {
    if (width == 0U || height == 0U || !EnsureTargets(width, height)) {
        return {};
    }

    UVE::Scene::IEntityManager& entityManager = services_.GetEntityManager();
    const bool overrideUsable =
        gameCameraOverride.has_value() && *gameCameraOverride != UVE::Scene::kInvalidEntity &&
        entityManager.HasComponent<UVE::Scene::WorldTransformComponent>(*gameCameraOverride) &&
        entityManager.HasComponent<UVE::Scene::CameraComponent>(*gameCameraOverride);
    const UVE::Scene::Entity renderCameraEntity = overrideUsable ? *gameCameraOverride : cameraEntity_;
    // Ordered before the camera sync so that sync's own SceneGraph update also propagates the
    // headlight's transform. The game-camera path skips that sync, so it updates explicitly -
    // otherwise the light would keep a stale world transform for as long as the override lasts.
    const bool headlightMoved = SyncHeadlight(camera);
    if (!overrideUsable) {
        const float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
        SyncCameraFromOrbit(camera, aspectRatio);
    } else if (headlightMoved) {
        services_.GetSceneGraph().Update(entityManager);
    }

    UVE::Render::IRenderer3D& renderer = services_.GetRenderer3D();
    const auto resetHeadlight = [&]() {
        if (entityManager.IsAlive(headlightEntity_) &&
            entityManager.HasComponent<UVE::Scene::LightComponent>(headlightEntity_)) {
            entityManager.GetComponent<UVE::Scene::LightComponent>(headlightEntity_).intensity = 0.0F;
        }
        if (entityManager.IsAlive(headlightEntity_) &&
            entityManager.HasComponent<UVE::Scene::LightEmitterComponent>(headlightEntity_)) {
            entityManager.GetComponent<UVE::Scene::LightEmitterComponent>(headlightEntity_).shadowEnabled = false;
        }
    };
    // Keeps this call's own output correctly sized regardless of EngineCore's own, unrelated
    // per-frame resize of this same shared renderer to the real window's size (its own
    // presentation-surface path, never shown by this panel) - both calls are cheap no-ops when
    // the requested size already matches, and this one always runs immediately before the render
    // below, so this layer's own result is always correct even if the two occasionally race.
    if (!renderer.ResizeTargets(width, height)) {
        resetHeadlight();
        return {};
    }
    renderer.RenderFrameToTarget(entityManager, renderCameraEntity, colorTarget_, depthTarget_, width, height);
    resetHeadlight();

    auto* const glRenderDevice = dynamic_cast<UVE::Render::GlRenderDevice*>(&services_.GetRenderDevice());
    if (glRenderDevice == nullptr) {
        return {};
    }
    return EditorMeshLayerResult{glRenderDevice->GetNativeTextureId(colorTarget_),
                                    glRenderDevice->GetNativeTextureId(depthTarget_)};
}

} // namespace univex::integration
