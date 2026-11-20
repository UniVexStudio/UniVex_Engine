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
using VfxSpriteQuadCornersUVE = std::array<Math::Vector3UVE, 4U>;

/// Flipbook UV sample pair for `FrameBlend == 0` (`uv0` only, `blendFactor == 0`) or
/// `FrameBlend == 1` (`mix(sample(uv0), sample(uv1), blendFactor)`).
struct VfxFlipbookUvSampleUVE final {
    Math::Vector2UVE uv0{};
    Math::Vector2UVE uv1{};
    std::int32_t frame0 = 0;
    std::int32_t frame1 = 0;
    float blendFactor = 0.0F;
};

/// Material and texture bindings resolved by `Renderer3DUVE` for a single sprite batch.
struct VfxResolvedBatchAppearanceUVE final {
    TextureHandleUVE albedoTexture = kInvalidTextureHandleUVE;
    Math::Vector4UVE materialTint{1.0F, 1.0F, 1.0F, 1.0F};
    Math::Vector3UVE materialEmissive{0.0F, 0.0F, 0.0F};
    bool ready = true;
};

/// Camera/frame parameters passed to `VfxSpriteRendererUVE` when recording batches.
struct VfxSpriteFrameContextUVE final {
    Math::Matrix4x4UVE viewProjection = Math::Matrix4x4UVE::IdentityUVE();
    Math::Matrix4x4UVE inverseProjection = Math::Matrix4x4UVE::IdentityUVE();
    Math::Vector3UVE cameraPosition{};
    Math::Vector3UVE cameraRight{1.0F, 0.0F, 0.0F};
    Math::Vector3UVE cameraUp{0.0F, 1.0F, 0.0F};
    Math::Vector3UVE cameraForward{0.0F, 0.0F, -1.0F};
    TextureHandleUVE sceneDepthTexture = kInvalidTextureHandleUVE;
    std::uint32_t viewportWidth = 1U;
    std::uint32_t viewportHeight = 1U;
    float depthNdcScale = 2.0F;
    float depthNdcBias = -1.0F;
};

/// Computes the 4 world-space quad corners for one sprite instance across all 4 facing modes:
/// `0 = CameraPlane`, `1 = Spherical`, `2 = Velocity` (stretched by `|velocity| * stretchScale`),
/// `3 = FixedAxis` (`facingAxis`), with `rotationRadians` applied in the quad plane.
[[nodiscard]] VfxSpriteQuadCornersUVE ExpandVfxSpriteCornersUVE(
    const Math::Vector3UVE& center,
    const Math::Vector2UVE& size,
    float rotationRadians,
    const Math::Vector3UVE& velocity,
    std::int32_t facingMode,
    float stretchScale,
    const Math::Vector3UVE& facingAxis,
    const Math::Vector3UVE& cameraPosition,
    const Math::Vector3UVE& cameraRight,
    const Math::Vector3UVE& cameraUp,
    const Math::Vector3UVE& cameraForward) noexcept;

/// Sorts `instances` and `depths` in-place back-to-front (descending depth: largest depth first)
/// using a 4-pass 8-bit radix sort over IEEE-754 float keys (handling negative and positive depths).
void RadixSortVfxSpritesUVE(std::vector<VfxSpriteGpuInstanceUVE>& instances,
                            std::vector<float>& depths);

/// Evaluates the soft-particle fade factor `clamp((sceneLinearDepth - particleLinearDepth) / softDistance, 0, 1)`.
/// Returns `1.0F` when `softDistance <= 0.0F`.
[[nodiscard]] float ComputeVfxSoftParticleFadeUVE(float sceneLinearDepth,
                                                  float particleLinearDepth,
                                                  float softDistance) noexcept;

/// Evaluates the near-camera fade factor `clamp((distanceToCamera - nearDist) / (farDist - nearDist), 0, 1)`.
/// Returns `1.0F` when `farDist <= nearDist` or `farDist <= 0.0F`.
[[nodiscard]] float ComputeVfxCameraFadeUVE(float distanceToCamera,
                                            float nearDist,
                                            float farDist) noexcept;

/// Evaluates flipbook UV coordinates for `cornerUv` in `[0,1]^2` at continuous `frame` in a
/// `rows x cols` sheet (`rows >= 1`, `cols >= 1`), with optional sub-frame blending (`frameBlend != 0`).
[[nodiscard]] VfxFlipbookUvSampleUVE ComputeVfxFlipbookUvsUVE(const Math::Vector2UVE& cornerUv,
                                                              float frame,
                                                              std::int32_t rows,
                                                              std::int32_t cols,
                                                              bool frameBlend) noexcept;

/// Enforces `maxInstances` across `drawList.batches`, truncating excess instances and returning the
/// number of instances removed by this cap (also added to `drawList.stats.instancesTruncated`).
std::size_t ApplyVfxSpriteBudgetUVE(VfxDrawListUVE& drawList,
                                    std::size_t maxInstances = kMaximumVfxSpriteInstancesPerFrameUVE) noexcept;

/// Manages built-in `vfx_sprite.glsl` pipeline variants, dynamic SSBO/vertex buffers, back-to-front
/// sorting for translucent alpha batches, and draw-call recording for `Renderer3DUVE`.
class VfxSpriteRendererUVE final {
public:
    using AppearanceResolverUVE =
        std::function<VfxResolvedBatchAppearanceUVE(std::string_view texturePath, std::string_view materialPath)>;

    VfxSpriteRendererUVE() = default;
    ~VfxSpriteRendererUVE() = default;

    VfxSpriteRendererUVE(const VfxSpriteRendererUVE&) = delete;
    VfxSpriteRendererUVE& operator=(const VfxSpriteRendererUVE&) = delete;

    void InitializeUVE(IRenderDeviceUVE& renderDevice, Shader::IShaderManagerUVE& shaderManager);
    void DestroyUVE(IRenderDeviceUVE& renderDevice) noexcept;

    [[nodiscard]] bool AreOpaqueProgramsReadyUVE() const noexcept;
    [[nodiscard]] bool AreTranslucentProgramsReadyUVE() const noexcept;
    [[nodiscard]] bool HasOpaqueBatchesUVE() const noexcept { return !m_opaqueRanges.empty(); }
    [[nodiscard]] bool HasTranslucentBatchesUVE() const noexcept { return !m_translucentRanges.empty(); }

    /// Sorts translucent `Blend == 1` batches back-to-front and uploads all visible batches for the frame.
    void PrepareFrameBatchesUVE(IRenderDeviceUVE& renderDevice,
                                const VfxDrawListUVE& drawList,
                                const VfxSpriteFrameContextUVE& frameContext);

    /// Records opaque cutout (`Blend == 2`) sprite batches inside the `MainColor` pass.
    /// Returns the number of draw calls issued.
    [[nodiscard]] std::size_t RecordOpaqueBatchesUVE(ICommandBufferUVE& commandBuffer,
                                                     const VfxSpriteFrameContextUVE& frameContext,
                                                     const AppearanceResolverUVE& resolver,
                                                     std::size_t* outInstancesDrawn = nullptr);

    /// Records translucent (`Blend == 0` Additive and `Blend == 1` AlphaBlend) sprite batches inside
    /// the dedicated `VfxParticles` pass. Returns the number of draw calls issued.
    [[nodiscard]] std::size_t RecordTranslucentBatchesUVE(ICommandBufferUVE& commandBuffer,
                                                          const VfxSpriteFrameContextUVE& frameContext,
                                                          const AppearanceResolverUVE& resolver,
                                                          std::size_t* outInstancesDrawn = nullptr);

private:
    struct PreparedBatchRangeUVE final {
        std::size_t batchIndex = 0U;
        std::uint32_t firstInstance = 0U;
        std::uint32_t instanceCount = 0U;
        std::uint32_t firstFallbackVertex = 0U;
        std::uint32_t fallbackVertexCount = 0U;
    };

    [[nodiscard]] bool EnsureStorageBufferCapacityUVE(IRenderDeviceUVE& renderDevice, std::size_t instanceCount);
    [[nodiscard]] bool EnsureFallbackBufferCapacityUVE(IRenderDeviceUVE& renderDevice, std::size_t vertexCount);

    [[nodiscard]] std::size_t RecordBatchListUVE(const std::vector<PreparedBatchRangeUVE>& ranges,
                                                 bool translucentPass,
                                                 ICommandBufferUVE& commandBuffer,
                                                 const VfxSpriteFrameContextUVE& frameContext,
                                                 const AppearanceResolverUVE& resolver,
                                                 std::size_t* outInstancesDrawn);

    std::shared_ptr<Shader::ShaderProgramUVE> m_opaqueSsboProgram;
    std::shared_ptr<Shader::ShaderProgramUVE> m_alphaSsboProgram;
    std::shared_ptr<Shader::ShaderProgramUVE> m_additiveSsboProgram;

    std::shared_ptr<Shader::ShaderProgramUVE> m_opaqueFallbackProgram;
    std::shared_ptr<Shader::ShaderProgramUVE> m_alphaFallbackProgram;
    std::shared_ptr<Shader::ShaderProgramUVE> m_additiveFallbackProgram;

    BufferHandleUVE m_instanceStorageBuffer = kInvalidBufferHandleUVE;
    std::size_t m_instanceStorageCapacity = 0U;
    BufferHandleUVE m_dummyVertexBuffer = kInvalidBufferHandleUVE;

    BufferHandleUVE m_fallbackVertexBuffer = kInvalidBufferHandleUVE;
    std::size_t m_fallbackVertexCapacity = 0U;

    std::vector<VfxSpriteBatchUVE> m_preparedBatches;
    std::vector<PreparedBatchRangeUVE> m_opaqueRanges;
    std::vector<PreparedBatchRangeUVE> m_translucentRanges;
    std::vector<VfxSpriteGpuInstanceUVE> m_stagingInstances;
    std::vector<VfxSpriteFallbackVertexUVE> m_stagingFallbackVertices;
    bool m_ssboUploadReady = false;
    bool m_fallbackUploadReady = false;
};

} // namespace UVE::Render
