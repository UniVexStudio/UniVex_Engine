// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/render_systems/renderer_2d_uve.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

#include "uve/asset/asset_handle_uve.h"
#include "uve/asset/texture_asset_uve.h"
#include "uve/component/camera_2d_component_uve.h"
#include "uve/component/sprite_2d_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/logging/logging_macros_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/2d/sprite_2d_uve.h"
#include "uve/objects/2d/tile_parallax_sprite_2d_uve.h"
#include "uve/rhi/i_command_buffer_uve.h"
#include "uve/rhi_shader/built_in_shaders_uve.h"
#include "uve/rhi_shader/shader_program_desc_uve.h"

namespace UVE::Render {
namespace {

/// 1x1 opaque-white pixel data for the fallback texture created once per Renderer2DUVE
/// instance (see the constructor): sampling it always yields {1,1,1,1}, so
/// `texture(uTexture, uv) * vColor == vColor` for untextured sprites, with no shader-side
/// branching needed — the same fallback-texture rule Renderer3DUVE's albedo path follows.
constexpr std::array<std::uint8_t, 4U> kWhitePixelUVE{0xFF, 0xFF, 0xFF, 0xFF};

/// Translates a loaded TextureAssetUVE's format into the RHI's own TextureFormatUVE.
/// Mirrors Renderer3DUVE's own mapper (which also stays file-local): asset formats never use
/// Depth32Float, so this is exhaustive over TextureAssetFormatUVE's two enumerators.
[[nodiscard]] TextureFormatUVE ToRenderTextureFormatUVE(Asset::TextureAssetFormatUVE format) noexcept {
    switch (format) {
        case Asset::TextureAssetFormatUVE::RGBA8Unorm:
            return TextureFormatUVE::RGBA8Unorm;
        case Asset::TextureAssetFormatUVE::RGBA16Float:
            return TextureFormatUVE::RGBA16Float;
    }
    return TextureFormatUVE::RGBA8Unorm;
}

/// True when the quad's NDC bounding box overlaps the [-1, 1] view box on both axes: a
/// conservative visibility test (an overlapping box always draws, so a visible sprite is never
/// culled; a thin diagonal quad whose box overlaps but whose pixels miss still draws — the
/// exact edge-intersection upgrade arrives with the run-batcher).
[[nodiscard]] bool QuadOverlapsViewUVE(const Math::Matrix4x4UVE& viewProjection,
                                       const std::array<Math::Vector3UVE, 4U>& worldCorners) noexcept {
    float minX = std::numeric_limits<float>::infinity();
    float maxX = -std::numeric_limits<float>::infinity();
    float minY = std::numeric_limits<float>::infinity();
    float maxY = -std::numeric_limits<float>::infinity();
    for (const Math::Vector3UVE& corner : worldCorners) {
        const Math::Vector3UVE ndc = Math::TransformPointUVE(viewProjection, corner);
        minX = std::min(minX, ndc.x);
        maxX = std::max(maxX, ndc.x);
        minY = std::min(minY, ndc.y);
        maxY = std::max(maxY, ndc.y);
    }
    return minX <= 1.0F && maxX >= -1.0F && minY <= 1.0F && maxY >= -1.0F;
}

} // namespace

Renderer2DUVE::Renderer2DUVE(IRenderDeviceUVE& renderDevice, IRenderSystemUVE& renderSystem,
                             ICamera2DSystemUVE& cameraSystem, Shader::IShaderManagerUVE& shaderManager,
                             Asset::IAssetManagerUVE& assetManager, Asset::IAssetDatabaseUVE& assetDatabase)
    : m_renderDevice(renderDevice),
      m_renderSystem(renderSystem),
      m_cameraSystem(cameraSystem),
      m_shaderManager(shaderManager),
      m_assetManager(assetManager),
      m_assetDatabase(assetDatabase) {
    m_fallbackWhiteTexture = m_renderDevice.CreateTextureUVE(
        TextureDescUVE{1, 1, TextureFormatUVE::RGBA8Unorm, 1}, std::as_bytes(std::span(kWhitePixelUVE)));
    if (m_fallbackWhiteTexture == kInvalidTextureHandleUVE) {
        UVE_ERROR("Renderer2DUVE: fallback-white texture creation failed; untextured sprites will not draw");
    }
}

Renderer2DUVE::~Renderer2DUVE() = default;

void Renderer2DUVE::RenderFrameToTargetUVE(Scene::IEntityManagerUVE& entityManager,
                                           Scene::EntityUVE cameraEntity, TextureHandleUVE colorTarget,
                                           TextureHandleUVE depthTarget, std::uint32_t width,
                                           std::uint32_t height) {
    m_diagnostics = Renderer2DFrameDiagnosticsUVE{};
    m_diagnostics.renderTargetWidth = width;
    m_diagnostics.renderTargetHeight = height;
    m_sprites.clear();
    if (width == 0U || height == 0U) {
        return; // No target area, no view: fail closed without recording anything.
    }

    const Scene::Camera2DComponentUVE& camera =
        entityManager.GetComponentUVE<Scene::Camera2DComponentUVE>(cameraEntity);
    bool recordSprites = Scene::IsCamera2DComponentValidUVE(camera);
    m_diagnostics.cameraValid = recordSprites;

    const Math::Vector2UVE viewportSize{static_cast<float>(width), static_cast<float>(height)};
    const Math::Matrix4x4UVE viewProjection =
        recordSprites ? m_cameraSystem.ComputeViewProjectionUVE(entityManager, cameraEntity, viewportSize)
                      : Math::Matrix4x4UVE::IdentityUVE();

    if (recordSprites) {
        entityManager.ForEachUVE<Scene::Sprite2DComponentUVE>(
            [this, &entityManager, &viewProjection](Scene::EntityUVE entity, Scene::Sprite2DComponentUVE& sprite) {
                ++m_diagnostics.spritesConsidered;
                if (!entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity)) {
                    ++m_diagnostics.spritesWithoutTransforms;
                    return;
                }
                if (entityManager.HasComponentUVE<Scene::VisibilityComponentUVE>(entity) &&
                    !entityManager.GetComponentUVE<Scene::VisibilityComponentUVE>(entity).visibleInHierarchy) {
                    ++m_diagnostics.hiddenSprites;
                    return;
                }
                if (!Scene::IsSprite2DComponentValidUVE(sprite)) {
                    ++m_diagnostics.invalidSprites;
                    return;
                }
                if (m_sprites.size() >= kMaximumSpritesPerFrame2DUVE) {
                    ++m_diagnostics.spritesDroppedPastBatchCap;
                    return;
                }

                TextureHandleUVE texture = m_fallbackWhiteTexture;
                Math::Vector2UVE textureSize{kUntexturedSprite2DSizeUVE, kUntexturedSprite2DSizeUVE};
                if (sprite.textureAssetGuid != Asset::kInvalidAssetGuidUVE) {
                    const auto found = m_textureCache.find(sprite.textureAssetGuid);
                    if (found != m_textureCache.end()) {
                        texture = found->second.texture;
                        textureSize = Math::Vector2UVE{static_cast<float>(found->second.width),
                                                       static_cast<float>(found->second.height)};
                    } else {
                        Asset::AssetHandleUVE<Asset::TextureAssetUVE> handle =
                            m_assetManager.LoadUVE<Asset::TextureAssetUVE>(sprite.textureAssetGuid,
                                                                          m_assetDatabase);
                        const Asset::TextureAssetUVE* loaded =
                            handle.IsReadyUVE() ? handle.TryGetUVE() : nullptr;
                        if (loaded == nullptr) {
                            ++m_diagnostics.unresolvedTextures;
                            return;
                        }
                        const std::uint32_t bytesPerPixel = Asset::BytesPerPixelUVE(loaded->format);
                        if (loaded->payloadEncoding != Asset::TexturePayloadEncodingUVE::RawPixels ||
                            loaded->width == 0U || loaded->height == 0U || bytesPerPixel == 0U ||
                            loaded->pixels.size() !=
                                static_cast<std::size_t>(loaded->width) * loaded->height * bytesPerPixel) {
                            ++m_diagnostics.invalidSprites;
                            return;
                        }
                        texture = m_renderDevice.CreateTextureUVE(
                            TextureDescUVE{loaded->width, loaded->height,
                                           ToRenderTextureFormatUVE(loaded->format), 1},
                            std::as_bytes(std::span(loaded->pixels)));
                        if (texture == kInvalidTextureHandleUVE) {
                            ++m_diagnostics.invalidSprites;
                            return;
                        }
                        m_textureCache.emplace(sprite.textureAssetGuid,
                                               SpriteTextureCacheEntryUVE{texture, loaded->width, loaded->height});
                        textureSize = Math::Vector2UVE{static_cast<float>(loaded->width),
                                                       static_cast<float>(loaded->height)};
                    }
                } else if (sprite.sourceRect.size.x > 0.0F && sprite.sourceRect.size.y > 0.0F) {
                    // No texture to measure against: size the quad from its own region (the UVs
                    // sample white, so their fractions are immaterial).
                    textureSize = Math::Vector2UVE{sprite.sourceRect.position.x + sprite.sourceRect.size.x,
                                                   sprite.sourceRect.position.y + sprite.sourceRect.size.y};
                }

                const std::optional<Scene::Sprite2DQuadUVE> quad =
                    Scene::ComposeSprite2DQuadUVE(sprite, textureSize);
                if (!quad.has_value()) {
                    ++m_diagnostics.invalidSprites;
                    return;
                }

                // Pixel space is y-down, the world is y-up (one world convention across 3D and
                // 2D): the corner's y negates on the way in, and the UVs travel with their
                // corners untouched. The full world rotation applies, so a tilted sprite
                // projects instead of breaking.
                const Scene::WorldTransformComponentUVE& world =
                    entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
                std::array<Math::Vector3UVE, 4U> worldCorners{};
                for (std::size_t corner = 0U; corner < 4U; ++corner) {
                    const Math::Vector3UVE local{quad->corners[corner].x * world.worldScale.x,
                                                 -quad->corners[corner].y * world.worldScale.y, 0.0F};
                    worldCorners[corner] = world.worldPosition + Math::RotateVectorUVE(world.worldRotation, local);
                }
                if (m_culling.viewCullingEnabledUVE && !QuadOverlapsViewUVE(viewProjection, worldCorners)) {
                    ++m_diagnostics.spritesCulled;
                    return;
                }

                Sprite2DDrawUVE draw{};
                draw.entity = entity;
                draw.sortDepth = world.worldPosition.z;
                draw.texture = texture;
                const Math::Vector4UVE color{sprite.modulate.x, sprite.modulate.y, sprite.modulate.z,
                                             sprite.alpha};
                // Two counter-clockwise triangles: (TL,BR,TR) + (TL,BL,BR).
                const std::size_t order[6] = {0U, 2U, 1U, 0U, 3U, 2U};
                for (std::size_t vertex = 0U; vertex < 6U; ++vertex) {
                    draw.vertices[vertex].position = worldCorners[order[vertex]];
                    draw.vertices[vertex].texCoord = quad->uvs[order[vertex]];
                    draw.vertices[vertex].color = color;
                }
                m_sprites.push_back(draw);
            });

        entityManager.ForEachUVE<Scene::TileMapLayerComponentUVE>(
            [this, &entityManager, &viewProjection](Scene::EntityUVE entity, Scene::TileMapLayerComponentUVE& tileMap) {
                if (!tileMap.enabled || tileMap.cells.empty()) {
                    return;
                }
                if (!entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity)) {
                    return;
                }
                if (entityManager.HasComponentUVE<Scene::VisibilityComponentUVE>(entity) &&
                    !entityManager.GetComponentUVE<Scene::VisibilityComponentUVE>(entity).visibleInHierarchy) {
                    return;
                }
                const Scene::WorldTransformComponentUVE& world =
                    entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);

                const Math::Vector2UVE tileSize =
                    (Math::IsFiniteUVE(tileMap.tileSizePixels) && tileMap.tileSizePixels.x > 0.0F &&
                     tileMap.tileSizePixels.y > 0.0F)
                        ? tileMap.tileSizePixels
                        : Math::Vector2UVE{16.0F, 16.0F};

                TextureHandleUVE texture = m_fallbackWhiteTexture;
                if (tileMap.tileSetAssetGuid != Asset::kInvalidAssetGuidUVE) {
                    const auto found = m_textureCache.find(tileMap.tileSetAssetGuid);
                    if (found != m_textureCache.end()) {
                        texture = found->second.texture;
                    }
                }

                for (const auto& cell : tileMap.cells) {
                    if (m_sprites.size() >= kMaximumSpritesPerFrame2DUVE) {
                        ++m_diagnostics.spritesDroppedPastBatchCap;
                        break;
                    }
                    if (cell.tileId < 0) {
                        continue;
                    }

                    const Math::Vector2UVE cellCenter =
                        Scene::TileMapLayerUVE::MapToLocalPositionUVE(tileMap, cell.coord);
                    const float halfW = tileSize.x * 0.5F;
                    const float halfH = tileSize.y * 0.5F;

                    const std::array<Math::Vector2UVE, 4U> localCorners{
                        Math::Vector2UVE{cellCenter.x - halfW, cellCenter.y - halfH},
                        Math::Vector2UVE{cellCenter.x + halfW, cellCenter.y - halfH},
                        Math::Vector2UVE{cellCenter.x + halfW, cellCenter.y + halfH},
                        Math::Vector2UVE{cellCenter.x - halfW, cellCenter.y + halfH},
                    };

                    std::array<Math::Vector3UVE, 4U> worldCorners{};
                    for (std::size_t c = 0U; c < 4U; ++c) {
                        const Math::Vector3UVE local{localCorners[c].x * world.worldScale.x,
                                                     -localCorners[c].y * world.worldScale.y, 0.0F};
                        worldCorners[c] = world.worldPosition + Math::RotateVectorUVE(world.worldRotation, local);
                    }

                    if (m_culling.viewCullingEnabledUVE && !QuadOverlapsViewUVE(viewProjection, worldCorners)) {
                        ++m_diagnostics.spritesCulled;
                        continue;
                    }

                    Sprite2DDrawUVE draw{};
                    draw.entity = entity;
                    draw.sortDepth = world.worldPosition.z;
                    draw.texture = texture;
                    const Math::Vector4UVE color{1.0F, 1.0F, 1.0F, 1.0F};

                    const std::size_t order[6] = {0U, 2U, 1U, 0U, 3U, 2U};
                    const std::array<Math::Vector2UVE, 4U> uvs{
                        Math::Vector2UVE{0.0F, 0.0F},
                        Math::Vector2UVE{1.0F, 0.0F},
                        Math::Vector2UVE{1.0F, 1.0F},
                        Math::Vector2UVE{0.0F, 1.0F},
                    };
                    for (std::size_t vertex = 0U; vertex < 6U; ++vertex) {
                        draw.vertices[vertex].position = worldCorners[order[vertex]];
                        draw.vertices[vertex].texCoord = uvs[order[vertex]];
                        draw.vertices[vertex].color = color;
                    }
                    m_sprites.push_back(draw);
                }
            });

        std::stable_sort(m_sprites.begin(), m_sprites.end(), [](const Sprite2DDrawUVE& left,
                                                                 const Sprite2DDrawUVE& right) {
            return left.sortDepth < right.sortDepth;
        });
    }
    m_diagnostics.viewCullingFrozen = recordSprites && !m_culling.viewCullingEnabledUVE;

    if (m_program == nullptr) {
        Shader::ShaderProgramDescUVE desc;
        desc.virtualFilePath = std::string(Shader::BuiltIn::kSprite2DVirtualPath);
        desc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kSprite2DSource);
        desc.vertexLayout = {
            VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3,
                               static_cast<std::uint32_t>(offsetof(Sprite2DVertexUVE, position))},
            VertexAttributeUVE{"TEXCOORD", VertexAttributeFormatUVE::Float2,
                               static_cast<std::uint32_t>(offsetof(Sprite2DVertexUVE, texCoord))},
            VertexAttributeUVE{"COLOR", VertexAttributeFormatUVE::Float4,
                               static_cast<std::uint32_t>(offsetof(Sprite2DVertexUVE, color))},
        };
        desc.vertexStride = static_cast<std::uint32_t>(sizeof(Sprite2DVertexUVE));
        desc.topology = PrimitiveTopologyUVE::Triangles;
        desc.depthTestEnabled = false;
        desc.depthWriteEnabled = false;
        desc.blendMode = PipelineBlendModeUVE::SourceAlphaOver;
        desc.debugNameUVE = "Sprite2D";
        m_program = m_shaderManager.CreateProgramUVE(desc);
        if (m_program == nullptr) {
            UVE_ERROR("Renderer2DUVE: sprite program creation failed; frame recording will be skipped");
            return;
        }
    }
    if (m_vertexBuffer == kInvalidBufferHandleUVE) {
        // One quad rewritten per sprite draw: host-visible, streamed placement.
        m_vertexBuffer = m_renderDevice.CreateBufferUVE(
            BufferDescUVE{sizeof(Sprite2DVertexUVE) * 6U, BufferUsageUVE::Vertex,
                          BufferMemoryUVE::Upload, BufferUpdateFrequencyUVE::Stream});
        if (m_vertexBuffer == kInvalidBufferHandleUVE) {
            UVE_ERROR("Renderer2DUVE: sprite vertex buffer creation failed; frame recording will be skipped");
            return;
        }
    }

    m_renderSystem.BeginFrameUVE();
    ICommandBufferUVE& commands = m_renderSystem.GetFrameCommandBufferUVE();
    RenderPassDescUVE pass{};
    pass.colorAttachment = colorTarget;
    pass.depthAttachment = depthTarget;
    pass.colorLoadOp = LoadOpUVE::Clear;
    pass.clearColor = m_clearColor;
    pass.depthLoadOp = LoadOpUVE::Clear;
    pass.clearDepth = 1.0F;
    commands.BeginRenderPassUVE(pass);
    if (recordSprites && !m_sprites.empty() && m_program->IsValidUVE()) {
        m_program->SetMatrix4x4UVE("uViewProjection", viewProjection);
        m_program->ApplyToUVE(commands);
        commands.BindVertexBufferUVE(m_vertexBuffer);
        for (const Sprite2DDrawUVE& draw : m_sprites) {
            if (!m_renderDevice.UpdateBufferUVE(m_vertexBuffer, std::as_bytes(std::span(draw.vertices)), 0U)) {
                UVE_ERROR("Renderer2DUVE: sprite vertex upload failed; stopping this frame's draws");
                break;
            }
            commands.BindTextureUVE(draw.texture, kSprite2DTextureSlotUVE);
            commands.DrawUVE(6U);
            ++m_diagnostics.spriteDrawsRecorded;
        }
    }
    commands.EndRenderPassUVE();
    m_renderSystem.EndFrameUVE();
    m_diagnostics.mainPassRecorded = true;
}

void Renderer2DUVE::SetSceneClearColorUVE(const std::array<float, 4U>& color) noexcept {
    m_clearColor = color;
}

void Renderer2DUVE::SetCullingSettingsUVE(const Renderer2DCullingSettingsUVE& settings) noexcept {
    m_culling = settings;
}

std::span<const Sprite2DDrawUVE> Renderer2DUVE::SpritesUVE() const noexcept {
    return m_sprites;
}

Renderer2DFrameDiagnosticsUVE Renderer2DUVE::GetLastFrameDiagnosticsUVE() const noexcept {
    return m_diagnostics;
}

} // namespace UVE::Render
