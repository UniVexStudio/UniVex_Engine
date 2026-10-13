// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "uve/asset/asset_guid_uve.h"
#include "uve/asset/i_asset_database_uve.h"
#include "uve/asset/i_asset_manager_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/math/vector4_uve.h"
#include "uve/render_systems/i_camera_2d_system_uve.h"
#include "uve/render_systems/i_render_system_uve.h"
#include "uve/render_systems/i_renderer_2d_uve.h"
#include "uve/rhi/i_render_device_uve.h"
#include "uve/rhi/render_resource_descs_uve.h"
#include "uve/rhi_shader/i_shader_manager_uve.h"
#include "uve/rhi_shader/shader_program_uve.h"

namespace UVE::Render {

/// One sprite vertex: a world-space position, the texture sample point, and the packed
/// modulate color + alpha the shader multiplies the texel by.
struct Sprite2DVertexUVE final {
    Math::Vector3UVE position{};
    Math::Vector2UVE texCoord{};
    Math::Vector4UVE color{1.0F, 1.0F, 1.0F, 1.0F};
};

/// One extracted sprite: the entity it came from, its painter depth (world z — higher draws
/// later, toward the viewer), the texture its quad samples, and the six counter-clockwise
/// triangle vertices (TL,BR,TR + TL,BL,BR) the draw uploads.
struct Sprite2DDrawUVE final {
    Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
    float sortDepth = 0.0F;
    TextureHandleUVE texture = kInvalidTextureHandleUVE;
    std::array<Sprite2DVertexUVE, 6U> vertices{};
};

/// Batch bound: 1024 sprites per frame. Sprites past the cap are dropped and counted rather
/// than growing the frame without bound — a sprite flood must not move the allocator.
inline constexpr std::size_t kMaximumSpritesPerFrame2DUVE = 1024U;

/// Texture slot the sprite program's uTexture sampler reads (the program's only sampler, so
/// slot 0 — the same first-sampler convention the 3D albedo slot follows).
inline constexpr std::uint32_t kSprite2DTextureSlotUVE = 0U;

/// One uploaded sprite texture: the GPU handle plus the pixel size the renderer composed
/// against (upload once, reuse every frame it is referenced).
struct SpriteTextureCacheEntryUVE final {
    TextureHandleUVE texture = kInvalidTextureHandleUVE;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
};

/// Size in pixels of an untextured, regionless sprite's flat quad: a zero GUID with no region
/// authored has no natural size, so it draws the definition's own 64x64-equivalent default
/// rather than nothing.
inline constexpr float kUntexturedSprite2DSizeUVE = 64.0F;

/// Renderer2DUVE is the concrete, engine-standard implementation of IRenderer2DUVE.
/// It owns GPU-shaped resources (the sprite program, one 6-vertex upload buffer, the 1x1
/// fallback-white texture, and a per-texture-GUID cache of uploaded sprite textures), so —
/// unlike the stateless Camera2DSystemUVE — it is a real service object: non-copyable, with
/// every reference outliving it. Textures upload once and are never invalidated in v1;
/// hot-reload invalidation arrives with the asset-watch increment.
class Renderer2DUVE final : public IRenderer2DUVE {
public:
    Renderer2DUVE(IRenderDeviceUVE& renderDevice, IRenderSystemUVE& renderSystem,
                  ICamera2DSystemUVE& cameraSystem, Shader::IShaderManagerUVE& shaderManager,
                  Asset::IAssetManagerUVE& assetManager, Asset::IAssetDatabaseUVE& assetDatabase);
    ~Renderer2DUVE() override;

    Renderer2DUVE(const Renderer2DUVE&) = delete;
    Renderer2DUVE& operator=(const Renderer2DUVE&) = delete;

    void RenderFrameToTargetUVE(Scene::IEntityManagerUVE& entityManager, Scene::EntityUVE cameraEntity,
                                TextureHandleUVE colorTarget, TextureHandleUVE depthTarget,
                                std::uint32_t width, std::uint32_t height) override;
    void SetSceneClearColorUVE(const std::array<float, 4U>& color) noexcept override;
    void SetCullingSettingsUVE(const Renderer2DCullingSettingsUVE& settings) noexcept override;

    /// The last frame's extracted sprite draws in submission order (painter-sorted, culled).
    [[nodiscard]] std::span<const Sprite2DDrawUVE> SpritesUVE() const noexcept;

    [[nodiscard]] Renderer2DFrameDiagnosticsUVE GetLastFrameDiagnosticsUVE() const noexcept override;

private:
    IRenderDeviceUVE& m_renderDevice;
    IRenderSystemUVE& m_renderSystem;
    ICamera2DSystemUVE& m_cameraSystem;
    Shader::IShaderManagerUVE& m_shaderManager;
    Asset::IAssetManagerUVE& m_assetManager;
    Asset::IAssetDatabaseUVE& m_assetDatabase;

    std::shared_ptr<Shader::ShaderProgramUVE> m_program;
    BufferHandleUVE m_vertexBuffer = kInvalidBufferHandleUVE;
    TextureHandleUVE m_fallbackWhiteTexture = kInvalidTextureHandleUVE;
    std::unordered_map<Asset::AssetGuidUVE, SpriteTextureCacheEntryUVE> m_textureCache;

    std::vector<Sprite2DDrawUVE> m_sprites;
    std::array<float, 4U> m_clearColor{0.0F, 0.0F, 0.0F, 1.0F};
    Renderer2DCullingSettingsUVE m_culling{};
    Renderer2DFrameDiagnosticsUVE m_diagnostics{};
};

} // namespace UVE::Render
