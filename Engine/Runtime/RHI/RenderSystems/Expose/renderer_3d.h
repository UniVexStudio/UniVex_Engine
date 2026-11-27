// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "i_asset_database.h"
#include "i_asset_manager.h"
#include "i_event_system.h"
#include "vector3.h"
#include "i_camera_system.h"
#include "i_light_system.h"
#include "i_mesh_renderer.h"
#include "i_render_device.h"
#include "i_render_system.h"
#include "i_renderer_3d.h"
#include "i_shader_manager.h"

namespace UVE::Render {

/// Renderer3D is the concrete, engine-standard implementation of IRenderer3D. Unlike
/// CameraSystem/MeshRenderer, it is deliberately stateful (PIMPL, matching
/// AssetManager's/NullRenderDevice's precedent for services that own real resources) — it
/// owns the offscreen render target and a GPU-resource cache (mesh GUID -> vertex/index buffers,
/// material GUID -> pipeline) that must persist and be invalidated across frames.
class Renderer3D final : public IRenderer3D {
public:
    /// Constructs the offscreen color+depth render target at `targetWidth` x `targetHeight` and
    /// subscribes to Asset::AssetReloadedEvent for GPU-resource-cache
    /// invalidation. `ambientColor` is the flat ambient term added to every item's final color
    /// every frame regardless of whether an active light exists (see EngineConfig::ambientColor,
    /// Increment 23). `shaderManager` compiles the built-in shadow-depth program used by the
    /// directional-light shadow pre-pass (Increment 26); `shadowMapResolution`/
    /// `shadowMapHalfExtent`/`shadowMapNearPlane`/`shadowMapFarPlane` size and bound that
    /// pre-pass's orthographic projection (see EngineConfig's matching fields). Increment 29
    /// derives the projection's actual bounds from the active camera frustum and applies
    /// `shadowFrustumPadding` to protect against edge clipping. `shadowCascadeSplitLambda`
    /// controls the bounded three-cascade practical split distribution. `shadowCascadeBlendRatio`
    /// cross-fades cascade transitions. `shadowPcfKernelRadius` controls the canonical material
    /// shader's bounded PCF filter: zero
    /// retains a hard shadow and one produces a 3x3 kernel. Every reference must outlive this
    /// Renderer3D.
    Renderer3D(IRenderDevice& renderDevice, IRenderSystem& renderSystem, IMeshRenderer& meshRenderer,
                  ICameraSystem& cameraSystem, ILightSystem& lightSystem,
                  Shader::IShaderManager& shaderManager, Asset::IAssetManager& assetManager,
                  Asset::IAssetDatabase& assetDatabase, Events::IEventSystem& eventSystem,
                  std::uint32_t targetWidth, std::uint32_t targetHeight, Math::Vector3 ambientColor,
                  std::uint32_t shadowMapResolution, float shadowMapHalfExtent, float shadowMapNearPlane,
                  float shadowMapFarPlane, float shadowFrustumPadding, float shadowCascadeSplitLambda,
                  float shadowCascadeBlendRatio, std::uint32_t shadowPcfKernelRadius);
    ~Renderer3D() override;

    Renderer3D(const Renderer3D&) = delete;
    Renderer3D& operator=(const Renderer3D&) = delete;

    void RenderFrame(Scene::IEntityManager& entityManager, Scene::Entity cameraEntity) override;
    [[nodiscard]] bool ResizeTargets(std::uint32_t width, std::uint32_t height) override;
    void SetSceneClearColor(const std::array<float, 4U>& color) noexcept override;
    void RenderFrameWithParticleRuntime(Scene::IEntityManager& entityManager, Scene::Entity cameraEntity,
                                           const Scene::ParticleRuntime& particleRuntime) override;
    void RenderFrameToRegion(Scene::IEntityManager& entityManager, Scene::Entity cameraEntity,
                                const ViewportRect& region,
                                const Scene::ParticleRuntime* particleRuntime = nullptr) override;
    void RenderFrameToTarget(Scene::IEntityManager& entityManager, Scene::Entity cameraEntity,
                                TextureHandle colorTarget, TextureHandle depthTarget,
                                std::uint32_t width = 0U, std::uint32_t height = 0U,
                                std::uint32_t colorLayer = 0U, std::uint32_t depthLayer = 0U) override;
    void SetPostProcessSettings(const PostProcessSettings& settings) override;
    [[nodiscard]] PostProcessSettings GetPostProcessSettings() const noexcept override;
    void SetPreviewEnvironment(const std::optional<Scene::WorldEnvironmentFrame>& frame) override;
    void SetShadowBiasDefaults(float depthBias, float normalBias) noexcept override;
    void SetShadowAtlasTileResolution(std::uint32_t resolution) noexcept override;
    void SetCullingSettings(const CullingSettings& settings) noexcept override;
    void SetMaterialSamplerSettings(bool repeatEnabled, float maxAnisotropy) noexcept override;
    void SetMaterialSamplerWrap(SamplerWrap wrap, float maxAnisotropy) noexcept override;
    void SetShadingMode(std::int32_t shadingMode) noexcept override;
    void SetScalabilityMode(std::int32_t scalabilityMode) noexcept override;
    void SetRendererProfile(Platform::RendererProfile profile) noexcept override;
    [[nodiscard]] RendererProfileResolution GetRendererProfileResolution() const noexcept override;
    void SetMeshUploadBudget(std::size_t meshesPerFrame) noexcept override;
    void SetUIRuntime(const UI::UIRuntime* uiRuntime) noexcept override;
    void SetVfxDrawList(const VfxDrawList* vfxDrawList) noexcept override;
    void SetStudioFloor(const std::optional<StudioFloorSettings>& settings) noexcept override;
    /// Renders every 3D View the UI drew last frame (UIView3DComponent) into its own picture,
    /// which the UI pass then draws in its rect. Called at the start of each frame.
    void CaptureUIViews(Scene::IEntityManager& entityManager, Scene::Entity cameraEntity);
    void SetPhysicsInterpolationAlpha(float alpha) noexcept override;

    [[nodiscard]] Renderer3DFrameDiagnostics GetLastFrameDiagnostics() const noexcept override;
    [[nodiscard]] SceneDepthExport GetLastSceneDepth() const noexcept override;

private:
    void CaptureDueReflectionProbes(Scene::IEntityManager& entityManager, Scene::Entity cameraEntity);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace UVE::Render
