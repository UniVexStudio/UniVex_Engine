// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#include "i_command_buffer.h"
#include "i_render_device.h"
#include "i_shader_manager.h"
#include "matrix4x4.h"
#include "shader_program.h"
#include "vector2.h"
#include "vector3.h"
#include "vector4.h"
#include "vfx_draw_list.h"

namespace UVE::Render {

/// Four world-space quad corners in counter-clockwise order:
/// [0] bottom-left (-0.5,-0.5), [1] bottom-right (+0.5,-0.5),
/// [2] top-right (+0.5,+0.5), [3] top-left (-0.5,+0.5).
using VfxSpriteQuadCorners = std::array<Math::Vector3, 4U>;

/// Flipbook UV sample pair for `FrameBlend == 0` (`uv0` only, `blendFactor == 0`) or
/// `FrameBlend == 1` (`mix(sample(uv0), sample(uv1), blendFactor)`).
struct VfxFlipbookUvSample final {
    Math::Vector2 uv0{};
    Math::Vector2 uv1{};
    std::int32_t frame0 = 0;
    std::int32_t frame1 = 0;
    float blendFactor = 0.0F;
};

/// Material and texture bindings resolved by `Renderer3D` for a single sprite batch.
struct VfxResolvedBatchAppearance final {
    TextureHandle albedoTexture = kInvalidTextureHandle;
    Math::Vector4 materialTint{1.0F, 1.0F, 1.0F, 1.0F};
    Math::Vector3 materialEmissive{0.0F, 0.0F, 0.0F};
    bool ready = true;
};

/// Camera/frame parameters passed to `VfxSpriteRenderer` when recording batches.
struct VfxSpriteFrameContext final {
    Math::Matrix4x4 viewProjection = Math::Matrix4x4::Identity();
    Math::Matrix4x4 inverseProjection = Math::Matrix4x4::Identity();
    Math::Vector3 cameraPosition{};
    Math::Vector3 cameraRight{1.0F, 0.0F, 0.0F};
    Math::Vector3 cameraUp{0.0F, 1.0F, 0.0F};
    Math::Vector3 cameraForward{0.0F, 0.0F, -1.0F};
    TextureHandle sceneDepthTexture = kInvalidTextureHandle;
    std::uint32_t viewportWidth = 1U;
    std::uint32_t viewportHeight = 1U;
    float depthNdcScale = 2.0F;
    float depthNdcBias = -1.0F;
};

/// Computes the 4 world-space quad corners for one sprite instance across all 4 facing modes:
/// `0 = CameraPlane`, `1 = Spherical`, `2 = Velocity` (stretched by `|velocity| * stretchScale`),
/// `3 = FixedAxis` (`facingAxis`), with `rotationRadians` applied in the quad plane.
[[nodiscard]] VfxSpriteQuadCorners ExpandVfxSpriteCorners(
    const Math::Vector3& center,
    const Math::Vector2& size,
    float rotationRadians,
    const Math::Vector3& velocity,
    std::int32_t facingMode,
    float stretchScale,
    const Math::Vector3& facingAxis,
    const Math::Vector3& cameraPosition,
    const Math::Vector3& cameraRight,
    const Math::Vector3& cameraUp,
    const Math::Vector3& cameraForward) noexcept;

/// Sorts `instances` and `depths` in-place back-to-front (descending depth: largest depth first)
/// using a 4-pass 8-bit radix sort over IEEE-754 float keys (handling negative and positive depths).
void RadixSortVfxSprites(std::vector<VfxSpriteGpuInstance>& instances,
                            std::vector<float>& depths);

/// Evaluates the soft-particle fade factor `clamp((sceneLinearDepth - particleLinearDepth) / softDistance, 0, 1)`.
/// Returns `1.0F` when `softDistance <= 0.0F`.
[[nodiscard]] float ComputeVfxSoftParticleFade(float sceneLinearDepth,
                                                  float particleLinearDepth,
                                                  float softDistance) noexcept;

/// Evaluates the near-camera fade factor `clamp((distanceToCamera - nearDist) / (farDist - nearDist), 0, 1)`.
/// Returns `1.0F` when `farDist <= nearDist` or `farDist <= 0.0F`.
[[nodiscard]] float ComputeVfxCameraFade(float distanceToCamera,
                                            float nearDist,
                                            float farDist) noexcept;

/// Evaluates flipbook UV coordinates for `cornerUv` in `[0,1]^2` at continuous `frame` in a
/// `rows x cols` sheet (`rows >= 1`, `cols >= 1`), with optional sub-frame blending (`frameBlend != 0`).
[[nodiscard]] VfxFlipbookUvSample ComputeVfxFlipbookUvs(const Math::Vector2& cornerUv,
                                                              float frame,
                                                              std::int32_t rows,
                                                              std::int32_t cols,
                                                              bool frameBlend) noexcept;

/// Enforces `maxInstances` across `drawList.batches`, truncating excess instances and returning the
/// number of instances removed by this cap (also added to `drawList.stats.instancesTruncated`).
std::size_t ApplyVfxSpriteBudget(VfxDrawList& drawList,
                                    std::size_t maxInstances = kMaximumVfxSpriteInstancesPerFrame) noexcept;

/// Manages built-in `vfx_sprite.glsl` pipeline variants, dynamic SSBO/vertex buffers, back-to-front
/// sorting for translucent alpha batches, and draw-call recording for `Renderer3D`.
class VfxSpriteRenderer final {
public:
    using AppearanceResolver =
        std::function<VfxResolvedBatchAppearance(std::string_view texturePath, std::string_view materialPath)>;

    VfxSpriteRenderer() = default;
    ~VfxSpriteRenderer() = default;

    VfxSpriteRenderer(const VfxSpriteRenderer&) = delete;
    VfxSpriteRenderer& operator=(const VfxSpriteRenderer&) = delete;

    void Initialize(IRenderDevice& renderDevice, Shader::IShaderManager& shaderManager);
    void Destroy(IRenderDevice& renderDevice) noexcept;

    [[nodiscard]] bool AreOpaqueProgramsReady() const noexcept;
    [[nodiscard]] bool AreTranslucentProgramsReady() const noexcept;
    [[nodiscard]] bool HasOpaqueBatches() const noexcept { return !m_opaqueRanges.empty(); }
    [[nodiscard]] bool HasTranslucentBatches() const noexcept { return !m_translucentRanges.empty(); }

    /// Sorts translucent `Blend == 1` batches back-to-front and uploads all visible batches for the frame.
    void PrepareFrameBatches(IRenderDevice& renderDevice,
                                const VfxDrawList& drawList,
                                const VfxSpriteFrameContext& frameContext);

    /// Records opaque cutout (`Blend == 2`) sprite batches inside the `MainColor` pass.
    /// Returns the number of draw calls issued.
    [[nodiscard]] std::size_t RecordOpaqueBatches(ICommandBuffer& commandBuffer,
                                                     const VfxSpriteFrameContext& frameContext,
                                                     const AppearanceResolver& resolver,
                                                     std::size_t* outInstancesDrawn = nullptr);

    /// Records translucent (`Blend == 0` Additive and `Blend == 1` AlphaBlend) sprite batches inside
    /// the dedicated `VfxParticles` pass. Returns the number of draw calls issued.
    [[nodiscard]] std::size_t RecordTranslucentBatches(ICommandBuffer& commandBuffer,
                                                          const VfxSpriteFrameContext& frameContext,
                                                          const AppearanceResolver& resolver,
                                                          std::size_t* outInstancesDrawn = nullptr);

private:
    struct PreparedBatchRange final {
        std::size_t batchIndex = 0U;
        std::uint32_t firstInstance = 0U;
        std::uint32_t instanceCount = 0U;
        std::uint32_t firstFallbackVertex = 0U;
        std::uint32_t fallbackVertexCount = 0U;
    };

    [[nodiscard]] bool EnsureStorageBufferCapacity(IRenderDevice& renderDevice, std::size_t instanceCount);
    [[nodiscard]] bool EnsureFallbackBufferCapacity(IRenderDevice& renderDevice, std::size_t vertexCount);

    [[nodiscard]] std::size_t RecordBatchList(const std::vector<PreparedBatchRange>& ranges,
                                                 bool translucentPass,
                                                 ICommandBuffer& commandBuffer,
                                                 const VfxSpriteFrameContext& frameContext,
                                                 const AppearanceResolver& resolver,
                                                 std::size_t* outInstancesDrawn);

    std::shared_ptr<Shader::ShaderProgram> m_opaqueSsboProgram;
    std::shared_ptr<Shader::ShaderProgram> m_alphaSsboProgram;
    std::shared_ptr<Shader::ShaderProgram> m_additiveSsboProgram;

    std::shared_ptr<Shader::ShaderProgram> m_opaqueFallbackProgram;
    std::shared_ptr<Shader::ShaderProgram> m_alphaFallbackProgram;
    std::shared_ptr<Shader::ShaderProgram> m_additiveFallbackProgram;

    BufferHandle m_instanceStorageBuffer = kInvalidBufferHandle;
    std::size_t m_instanceStorageCapacity = 0U;
    BufferHandle m_dummyVertexBuffer = kInvalidBufferHandle;

    BufferHandle m_fallbackVertexBuffer = kInvalidBufferHandle;
    std::size_t m_fallbackVertexCapacity = 0U;

    std::vector<VfxSpriteBatch> m_preparedBatches;
    std::vector<PreparedBatchRange> m_opaqueRanges;
    std::vector<PreparedBatchRange> m_translucentRanges;
    std::vector<VfxSpriteGpuInstance> m_stagingInstances;
    std::vector<VfxSpriteFallbackVertex> m_stagingFallbackVertices;
    bool m_ssboUploadReady = false;
    bool m_fallbackUploadReady = false;
};

} // namespace UVE::Render
