// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "vfx_sprite_renderer.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "built_in_shaders.h"
#include "shader_program_desc.h"

namespace UVE::Render {

namespace {

constexpr std::uint32_t kVfxAlbedoTextureSlotUVE = 0U;
constexpr std::uint32_t kVfxSceneDepthTextureSlotUVE = 1U;
constexpr std::uint32_t kVfxInstanceStorageSlotUVE = 0U;

[[nodiscard]] Math::Vector3UVE SafeNormalizeOrUVE(const Math::Vector3UVE& v,
                                                  const Math::Vector3UVE& fallback) noexcept {
    if (!Math::IsFiniteUVE(v) || Math::LengthSquaredUVE(v) < 1.0e-8F) {
        return fallback;
    }
    return Math::NormalizeUVE(v);
}

/// Maps an IEEE-754 float to a monotonic unsigned 32-bit key suitable for descending (largest-first)
/// radix sorting: flips all bits for negative floats and flips the sign bit for non-negative floats,
/// then bitwise-inverts the ascending key so larger floats sort before smaller floats.
[[nodiscard]] constexpr std::uint32_t FloatToDescendingRadixKeyUVE(const float value) noexcept {
    const float safeVal = std::isfinite(value) ? value : 0.0F;
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(safeVal);
    const std::uint32_t mask = (bits & 0x80000000U) != 0U ? 0xFFFFFFFFU : 0x80000000U;
    const std::uint32_t ascendingKey = bits ^ mask;
    return ~ascendingKey;
}

[[nodiscard]] std::vector<VertexAttributeUVE> VfxFallbackVertexLayoutUVE() {
    return {
        VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3,
                           offsetof(VfxSpriteFallbackVertexUVE, position)},
        VertexAttributeUVE{"TEXCOORD", VertexAttributeFormatUVE::Float2,
                           offsetof(VfxSpriteFallbackVertexUVE, uv)},
        VertexAttributeUVE{"COLOR", VertexAttributeFormatUVE::Float4,
                           offsetof(VfxSpriteFallbackVertexUVE, red)},
        VertexAttributeUVE{"TEXCOORD1", VertexAttributeFormatUVE::Float2,
                           offsetof(VfxSpriteFallbackVertexUVE, frame)},
    };
}

[[nodiscard]] std::shared_ptr<Shader::ShaderProgramUVE> CreateVfxProgramVariantUVE(
    Shader::IShaderManagerUVE& shaderManager,
    const PipelineBlendModeUVE blendMode,
    const bool depthTest,
    const bool depthWrite,
    const bool translucentDefines,
    const bool vertexFallback,
    const std::string_view debugName) {
    Shader::ShaderProgramStagesDescUVE desc;
    desc.vertexSource.stage = ShaderStageUVE::Vertex;
    desc.vertexSource.virtualFilePath = std::string(Shader::BuiltIn::kVfxSpriteVirtualPath);
    desc.vertexSource.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kVfxSpriteSource);
    desc.vertexSource.extraDefines.push_back({"VERTEX_SHADER", "1"});
    if (vertexFallback) {
        desc.vertexSource.extraDefines.push_back({"UVE_VFX_VERTEX_FALLBACK", "1"});
    }
    desc.vertexSource.debugNameUVE = std::string(debugName) + " VS";

    desc.fragmentSource.stage = ShaderStageUVE::Fragment;
    desc.fragmentSource.virtualFilePath = std::string(Shader::BuiltIn::kVfxSpriteVirtualPath);
    desc.fragmentSource.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kVfxSpriteSource);
    desc.fragmentSource.extraDefines.push_back({"FRAGMENT_SHADER", "1"});
    if (translucentDefines) {
        desc.fragmentSource.extraDefines.push_back({"UVE_VFX_TRANSLUCENT", "1"});
    }
    desc.fragmentSource.debugNameUVE = std::string(debugName) + " FS";

    if (vertexFallback) {
        desc.vertexLayout = VfxFallbackVertexLayoutUVE();
        desc.vertexStride = static_cast<std::uint32_t>(sizeof(VfxSpriteFallbackVertexUVE));
    }
    desc.blendMode = blendMode;
    desc.cullMode = CullModeUVE::None;
    desc.depthTestEnabled = depthTest;
    desc.depthWriteEnabled = depthWrite;
    desc.depthCompare = DepthCompareUVE::LessOrEqual;
    desc.debugNameUVE = std::string(debugName);
    return shaderManager.CreateProgramFromStagesUVE(desc);
}

} // namespace

VfxSpriteQuadCornersUVE ExpandVfxSpriteCornersUVE(
    const Math::Vector3UVE& center,
    const Math::Vector2UVE& size,
    const float rotationRadians,
    const Math::Vector3UVE& velocity,
    const std::int32_t facingMode,
    const float stretchScale,
    const Math::Vector3UVE& facingAxis,
    const Math::Vector3UVE& cameraPosition,
    const Math::Vector3UVE& cameraRight,
    const Math::Vector3UVE& cameraUp,
    const Math::Vector3UVE& cameraForward) noexcept {
    const Math::Vector3UVE safeRight = SafeNormalizeOrUVE(cameraRight, Math::Vector3UVE{1.0F, 0.0F, 0.0F});
    const Math::Vector3UVE safeUp = SafeNormalizeOrUVE(cameraUp, Math::Vector3UVE{0.0F, 1.0F, 0.0F});
    const Math::Vector3UVE safeForward = SafeNormalizeOrUVE(cameraForward, Math::Vector3UVE{0.0F, 0.0F, -1.0F});

    Math::Vector3UVE basisRight = safeRight;
    Math::Vector3UVE basisUp = safeUp;
    Math::Vector2UVE effectiveSize{
        std::isfinite(size.x) ? std::max(0.0F, size.x) : 0.0F,
        std::isfinite(size.y) ? std::max(0.0F, size.y) : 0.0F,
    };

    if (facingMode == 1) {
        const Math::Vector3UVE toCamera = cameraPosition - center;
        const Math::Vector3UVE forwardDir = SafeNormalizeOrUVE(toCamera, -safeForward);
        const Math::Vector3UVE refUp =
            std::fabs(forwardDir.y) < 0.99F ? Math::Vector3UVE{0.0F, 1.0F, 0.0F}
                                            : Math::Vector3UVE{1.0F, 0.0F, 0.0F};
        basisRight = SafeNormalizeOrUVE(Math::CrossUVE(refUp, forwardDir), safeRight);
        basisUp = SafeNormalizeOrUVE(Math::CrossUVE(forwardDir, basisRight), safeUp);
    } else if (facingMode == 2) {
        const float speed = Math::IsFiniteUVE(velocity) ? Math::LengthUVE(velocity) : 0.0F;
        const Math::Vector3UVE velDir = speed > 1.0e-5F ? (velocity * (1.0F / speed)) : safeUp;
        const Math::Vector3UVE viewDir = SafeNormalizeOrUVE(cameraPosition - center, -safeForward);
        const Math::Vector3UVE side = Math::CrossUVE(viewDir, velDir);
        basisRight = SafeNormalizeOrUVE(side, safeRight);
        basisUp = velDir;
        const float safeStretch = std::isfinite(stretchScale) ? std::max(0.0F, stretchScale) : 0.0F;
        effectiveSize.y += speed * safeStretch;
    } else if (facingMode == 3) {
        const Math::Vector3UVE normalAxis = SafeNormalizeOrUVE(facingAxis, Math::Vector3UVE{0.0F, 1.0F, 0.0F});
        const Math::Vector3UVE refUp =
            std::fabs(normalAxis.y) < 0.99F ? Math::Vector3UVE{0.0F, 1.0F, 0.0F}
                                            : Math::Vector3UVE{0.0F, 0.0F, 1.0F};
        basisRight = SafeNormalizeOrUVE(Math::CrossUVE(refUp, normalAxis), Math::Vector3UVE{1.0F, 0.0F, 0.0F});
        basisUp = SafeNormalizeOrUVE(Math::CrossUVE(normalAxis, basisRight), Math::Vector3UVE{0.0F, 0.0F, 1.0F});
    }

    const float angle = std::isfinite(rotationRadians) ? rotationRadians : 0.0F;
    const float cosA = std::cos(angle);
    const float sinA = std::sin(angle);

    constexpr std::array<Math::Vector2UVE, 4U> kUnitCorners{{
        {-0.5F, -0.5F},
        { 0.5F, -0.5F},
        { 0.5F,  0.5F},
        {-0.5F,  0.5F},
    }};

    VfxSpriteQuadCornersUVE out{};
    for (std::size_t i = 0U; i < 4U; ++i) {
        const float lx = kUnitCorners[i].x * effectiveSize.x;
        const float ly = kUnitCorners[i].y * effectiveSize.y;
        const float rx = lx * cosA - ly * sinA;
        const float ry = lx * sinA + ly * cosA;
        out[i] = center + basisRight * rx + basisUp * ry;
    }
    return out;
}

void RadixSortVfxSpritesUVE(std::vector<VfxSpriteGpuInstanceUVE>& instances,
                            std::vector<float>& depths) {
    const std::size_t count = std::min(instances.size(), depths.size());
    if (count <= 1U) {
        return;
    }

    static thread_local std::vector<std::uint32_t> indices;
    static thread_local std::vector<std::uint32_t> keys;
    static thread_local std::vector<std::uint32_t> scratchIndices;
    static thread_local std::vector<std::uint32_t> scratchKeys;
    static thread_local std::vector<VfxSpriteGpuInstanceUVE> sortedInstances;
    static thread_local std::vector<float> sortedDepths;

    indices.resize(count);
    keys.resize(count);
    for (std::size_t i = 0U; i < count; ++i) {
        indices[i] = static_cast<std::uint32_t>(i);
        keys[i] = FloatToDescendingRadixKeyUVE(depths[i]);
    }

    scratchIndices.resize(count);
    scratchKeys.resize(count);

    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
        std::array<std::size_t, 256U> histogram{};
        for (std::size_t i = 0U; i < count; ++i) {
            const std::uint32_t bucket = (keys[i] >> shift) & 0xFFU;
            ++histogram[bucket];
        }

        std::size_t prefix = 0U;
        for (std::size_t bucket = 0U; bucket < 256U; ++bucket) {
            const std::size_t bucketCount = histogram[bucket];
            histogram[bucket] = prefix;
            prefix += bucketCount;
        }

        for (std::size_t i = 0U; i < count; ++i) {
            const std::uint32_t bucket = (keys[i] >> shift) & 0xFFU;
            const std::size_t dst = histogram[bucket]++;
            scratchKeys[dst] = keys[i];
            scratchIndices[dst] = indices[i];
        }

        keys.swap(scratchKeys);
        indices.swap(scratchIndices);
    }

    sortedInstances.resize(count);
    sortedDepths.resize(count);
    for (std::size_t i = 0U; i < count; ++i) {
        const std::size_t src = indices[i];
        sortedInstances[i] = instances[src];
        sortedDepths[i] = depths[src];
    }
    instances.swap(sortedInstances);
    depths.swap(sortedDepths);
}

float ComputeVfxSoftParticleFadeUVE(const float sceneLinearDepth,
                                    const float particleLinearDepth,
                                    const float softDistance) noexcept {
    if (!std::isfinite(softDistance) || softDistance <= 0.0F) {
        return 1.0F;
    }
    if (!std::isfinite(sceneLinearDepth) || !std::isfinite(particleLinearDepth)) {
        return 1.0F;
    }
    return std::clamp((sceneLinearDepth - particleLinearDepth) / std::max(softDistance, 1.0e-5F), 0.0F, 1.0F);
}

float ComputeVfxCameraFadeUVE(const float distanceToCamera,
                              const float nearDist,
                              const float farDist) noexcept {
    if (!std::isfinite(nearDist) || !std::isfinite(farDist) || farDist <= nearDist || farDist <= 0.0F) {
        return 1.0F;
    }
    if (!std::isfinite(distanceToCamera)) {
        return 1.0F;
    }
    return std::clamp((distanceToCamera - nearDist) / std::max(farDist - nearDist, 1.0e-5F), 0.0F, 1.0F);
}

VfxFlipbookUvSampleUVE ComputeVfxFlipbookUvsUVE(const Math::Vector2UVE& cornerUv,
                                                const float frame,
                                                const std::int32_t rows,
                                                const std::int32_t cols,
                                                const bool frameBlend) noexcept {
    const std::int32_t safeRows = std::max(rows, 1);
    const std::int32_t safeCols = std::max(cols, 1);
    const std::int32_t totalFrames = safeRows * safeCols;
    const float safeFrame = std::isfinite(frame)
        ? std::clamp(frame, 0.0F, static_cast<float>( std::max(totalFrames - 1, 0)))
        : 0.0F;
    const std::int32_t frame0 =
        std::clamp(static_cast<std::int32_t>(std::floor(safeFrame)), 0, std::max(totalFrames - 1, 0));
    const std::int32_t frame1 = frameBlend ? std::min(frame0 + 1, std::max(totalFrames - 1, 0)) : frame0;
    const float blendFactor = (frameBlend && totalFrames > 1) ? (safeFrame - std::floor(safeFrame)) : 0.0F;

    const float uClamped = std::isfinite(cornerUv.x) ? std::clamp(cornerUv.x, 0.0F, 1.0F) : 0.0F;
    const float vClamped = std::isfinite(cornerUv.y) ? std::clamp(cornerUv.y, 0.0F, 1.0F) : 0.0F;
    const float invCols = 1.0F / static_cast<float>(safeCols);
    const float invRows = 1.0F / static_cast<float>(safeRows);

    const auto cellUv = [&](const std::int32_t f) noexcept -> Math::Vector2UVE {
        const std::int32_t col = f % safeCols;
        const std::int32_t row = f / safeCols;
        return Math::Vector2UVE{
            (static_cast<float>(col) + uClamped) * invCols,
            (static_cast<float>(row) + vClamped) * invRows,
        };
    };

    VfxFlipbookUvSampleUVE out{};
    out.uv0 = cellUv(frame0);
    out.uv1 = cellUv(frame1);
    out.frame0 = frame0;
    out.frame1 = frame1;
    out.blendFactor = std::clamp(blendFactor, 0.0F, 1.0F);
    return out;
}

std::size_t ApplyVfxSpriteBudgetUVE(VfxDrawListUVE& drawList, const std::size_t maxInstances) noexcept {
    std::size_t keptTotal = 0U;
    std::size_t newlyTruncated = 0U;
    std::size_t opaqueCount = 0U;
    std::size_t translucentCount = 0U;

    for (auto it = drawList.batches.begin(); it != drawList.batches.end();) {
        const std::size_t batchSize = it->instances.size();
        const std::size_t remaining = keptTotal < maxInstances ? (maxInstances - keptTotal) : 0U;
        if (remaining == 0U) {
            newlyTruncated += batchSize;
            it = drawList.batches.erase(it);
            continue;
        }
        if (batchSize > remaining) {
            newlyTruncated += (batchSize - remaining);
            it->instances.resize(remaining);
            if (it->sortDepths.size() > remaining) {
                it->sortDepths.resize(remaining);
            }
        }
        keptTotal += it->instances.size();
        if (it->blend == 2) {
            ++opaqueCount;
        } else {
            ++translucentCount;
        }
        ++it;
    }

    drawList.stats.batchesExtracted = drawList.batches.size();
    drawList.stats.opaqueBatchesExtracted = opaqueCount;
    drawList.stats.translucentBatchesExtracted = translucentCount;
    drawList.stats.instancesExtracted = keptTotal;
    drawList.stats.instancesTruncated += newlyTruncated;
    return newlyTruncated;
}

void VfxSpriteRendererUVE::InitializeUVE(IRenderDeviceUVE& renderDevice,
                                         Shader::IShaderManagerUVE& shaderManager) {
    m_opaqueSsboProgram = CreateVfxProgramVariantUVE(
        shaderManager, PipelineBlendModeUVE::Opaque, true, true, false, false, "VfxSpriteOpaqueSSBO");
    m_alphaSsboProgram = CreateVfxProgramVariantUVE(
        shaderManager, PipelineBlendModeUVE::PremultipliedAlpha, false, false, true, false, "VfxSpriteAlphaSSBO");
    m_additiveSsboProgram = CreateVfxProgramVariantUVE(
        shaderManager, PipelineBlendModeUVE::Additive, false, false, true, false, "VfxSpriteAdditiveSSBO");

    m_opaqueFallbackProgram = CreateVfxProgramVariantUVE(
        shaderManager, PipelineBlendModeUVE::Opaque, true, true, false, true, "VfxSpriteOpaqueFallback");
    m_alphaFallbackProgram = CreateVfxProgramVariantUVE(
        shaderManager, PipelineBlendModeUVE::PremultipliedAlpha, false, false, true, true, "VfxSpriteAlphaFallback");
    m_additiveFallbackProgram = CreateVfxProgramVariantUVE(
        shaderManager, PipelineBlendModeUVE::Additive, false, false, true, true, "VfxSpriteAdditiveFallback");

    if (m_dummyVertexBuffer == kInvalidBufferHandleUVE) {
        const std::array<float, 6U> dummyZeros{};
        m_dummyVertexBuffer = renderDevice.CreateBufferUVE(
            BufferDescUVE{sizeof(dummyZeros), BufferUsageUVE::Vertex},
            std::as_bytes(std::span<const float>(dummyZeros)));
    }
}

void VfxSpriteRendererUVE::DestroyUVE(IRenderDeviceUVE& renderDevice) noexcept {
    if (m_instanceStorageBuffer != kInvalidBufferHandleUVE) {
        renderDevice.DestroyBufferUVE(m_instanceStorageBuffer);
        m_instanceStorageBuffer = kInvalidBufferHandleUVE;
    }
    m_instanceStorageCapacity = 0U;

    if (m_dummyVertexBuffer != kInvalidBufferHandleUVE) {
        renderDevice.DestroyBufferUVE(m_dummyVertexBuffer);
        m_dummyVertexBuffer = kInvalidBufferHandleUVE;
    }

    if (m_fallbackVertexBuffer != kInvalidBufferHandleUVE) {
        renderDevice.DestroyBufferUVE(m_fallbackVertexBuffer);
        m_fallbackVertexBuffer = kInvalidBufferHandleUVE;
    }
    m_fallbackVertexCapacity = 0U;

    m_opaqueSsboProgram.reset();
    m_alphaSsboProgram.reset();
    m_additiveSsboProgram.reset();
    m_opaqueFallbackProgram.reset();
    m_alphaFallbackProgram.reset();
    m_additiveFallbackProgram.reset();

    m_preparedBatches.clear();
    m_opaqueRanges.clear();
    m_translucentRanges.clear();
    m_stagingInstances.clear();
    m_stagingFallbackVertices.clear();
    m_ssboUploadReady = false;
    m_fallbackUploadReady = false;
}

bool VfxSpriteRendererUVE::AreOpaqueProgramsReadyUVE() const noexcept {
    return (m_opaqueSsboProgram != nullptr && m_opaqueSsboProgram->IsValidUVE()) ||
           (m_opaqueFallbackProgram != nullptr && m_opaqueFallbackProgram->IsValidUVE());
}

bool VfxSpriteRendererUVE::AreTranslucentProgramsReadyUVE() const noexcept {
    const bool ssboReady = m_alphaSsboProgram != nullptr && m_alphaSsboProgram->IsValidUVE() &&
                           m_additiveSsboProgram != nullptr && m_additiveSsboProgram->IsValidUVE();
    const bool fallbackReady = m_alphaFallbackProgram != nullptr && m_alphaFallbackProgram->IsValidUVE() &&
                               m_additiveFallbackProgram != nullptr && m_additiveFallbackProgram->IsValidUVE();
    return ssboReady || fallbackReady;
}

bool VfxSpriteRendererUVE::EnsureStorageBufferCapacityUVE(IRenderDeviceUVE& renderDevice,
                                                          const std::size_t instanceCount) {
    if (instanceCount == 0U) {
        return true;
    }
    if (m_instanceStorageBuffer != kInvalidBufferHandleUVE && m_instanceStorageCapacity >= instanceCount) {
        return true;
    }
    std::size_t newCapacity = std::max<std::size_t>(256U, m_instanceStorageCapacity);
    while (newCapacity < instanceCount) {
        newCapacity = std::min(newCapacity * 2U, kMaximumVfxSpriteInstancesPerFrameUVE);
        if (newCapacity < instanceCount) {
            newCapacity = instanceCount;
            break;
        }
    }
    if (m_instanceStorageBuffer != kInvalidBufferHandleUVE) {
        renderDevice.DestroyBufferUVE(m_instanceStorageBuffer);
        m_instanceStorageBuffer = kInvalidBufferHandleUVE;
        m_instanceStorageCapacity = 0U;
    }
    m_instanceStorageBuffer = renderDevice.CreateBufferUVE(
        BufferDescUVE{newCapacity * sizeof(VfxSpriteGpuInstanceUVE), BufferUsageUVE::Storage});
    if (m_instanceStorageBuffer == kInvalidBufferHandleUVE) {
        return false;
    }
    m_instanceStorageCapacity = newCapacity;
    return true;
}

bool VfxSpriteRendererUVE::EnsureFallbackBufferCapacityUVE(IRenderDeviceUVE& renderDevice,
                                                           const std::size_t vertexCount) {
    if (vertexCount == 0U) {
        return true;
    }
    if (m_fallbackVertexBuffer != kInvalidBufferHandleUVE && m_fallbackVertexCapacity >= vertexCount) {
        return true;
    }
    std::size_t newCapacity = std::max<std::size_t>(1536U, m_fallbackVertexCapacity);
    while (newCapacity < vertexCount) {
        newCapacity *= 2U;
    }
    if (m_fallbackVertexBuffer != kInvalidBufferHandleUVE) {
        renderDevice.DestroyBufferUVE(m_fallbackVertexBuffer);
        m_fallbackVertexBuffer = kInvalidBufferHandleUVE;
        m_fallbackVertexCapacity = 0U;
    }
    m_fallbackVertexBuffer = renderDevice.CreateBufferUVE(
        BufferDescUVE{newCapacity * sizeof(VfxSpriteFallbackVertexUVE), BufferUsageUVE::Vertex});
    if (m_fallbackVertexBuffer == kInvalidBufferHandleUVE) {
        return false;
    }
    m_fallbackVertexCapacity = newCapacity;
    return true;
}

void VfxSpriteRendererUVE::PrepareFrameBatchesUVE(IRenderDeviceUVE& renderDevice,
                                                  const VfxDrawListUVE& drawList,
                                                  const VfxSpriteFrameContextUVE& frameContext) {
    m_preparedBatches.clear();
    m_opaqueRanges.clear();
    m_translucentRanges.clear();
    m_stagingInstances.clear();
    m_stagingFallbackVertices.clear();
    m_ssboUploadReady = false;
    m_fallbackUploadReady = false;

    if (drawList.batches.empty()) {
        return;
    }

    m_preparedBatches = drawList.batches;
    std::size_t totalInstances = 0U;
    for (VfxSpriteBatchUVE& batch : m_preparedBatches) {
        if (batch.instances.empty()) {
            continue;
        }
        if (batch.blend == 1 && batch.sortDepths.size() == batch.instances.size()) {
            RadixSortVfxSpritesUVE(batch.instances, batch.sortDepths);
        }
        totalInstances += batch.instances.size();
    }

    if (totalInstances == 0U) {
        return;
    }

    m_stagingInstances.reserve(totalInstances);
    for (std::size_t batchIdx = 0U; batchIdx < m_preparedBatches.size(); ++batchIdx) {
        const VfxSpriteBatchUVE& batch = m_preparedBatches[batchIdx];
        if (batch.instances.empty()) {
            continue;
        }

        PreparedBatchRangeUVE range{};
        range.batchIndex = batchIdx;
        range.firstInstance = static_cast<std::uint32_t>(m_stagingInstances.size());
        range.instanceCount = static_cast<std::uint32_t>(batch.instances.size());
        m_stagingInstances.insert(m_stagingInstances.end(), batch.instances.begin(), batch.instances.end());

        if (batch.blend == 2) {
            m_opaqueRanges.push_back(range);
        } else {
            m_translucentRanges.push_back(range);
        }
    }

    if (EnsureStorageBufferCapacityUVE(renderDevice, m_stagingInstances.size())) {
        const std::span<const VfxSpriteGpuInstanceUVE> instSpan(m_stagingInstances);
        m_ssboUploadReady = renderDevice.UpdateBufferUVE(m_instanceStorageBuffer, std::as_bytes(instSpan));
    }

    // If SSBO upload is unavailable, expand vertices on CPU for the fallback attribute pipeline.
    if (!m_ssboUploadReady) {
        m_stagingFallbackVertices.reserve(m_stagingInstances.size() * kVfxSpriteVerticesPerQuadUVE);
        constexpr std::array<Math::Vector2UVE, 4U> kCornerUvs{{
            {0.0F, 1.0F},
            {1.0F, 1.0F},
            {1.0F, 0.0F},
            {0.0F, 0.0F},
        }};
        constexpr std::array<std::size_t, 6U> kTriOrder{{0U, 1U, 2U, 0U, 2U, 3U}};

        auto populateRanges = [&](std::vector<PreparedBatchRangeUVE>& ranges) {
            for (PreparedBatchRangeUVE& range : ranges) {
                const VfxSpriteBatchUVE& batch = m_preparedBatches[range.batchIndex];
                range.firstFallbackVertex = static_cast<std::uint32_t>(m_stagingFallbackVertices.size());
                for (const VfxSpriteGpuInstanceUVE& inst : batch.instances) {
                    const VfxSpriteQuadCornersUVE corners = ExpandVfxSpriteCornersUVE(
                        Math::Vector3UVE{inst.posX, inst.posY, inst.posZ},
                        Math::Vector2UVE{inst.sizeX, inst.sizeY},
                        inst.rotationRad,
                        Math::Vector3UVE{inst.velX, inst.velY, inst.velZ},
                        batch.facing,
                        batch.stretchScale,
                        batch.axis,
                        frameContext.cameraPosition,
                        frameContext.cameraRight,
                        frameContext.cameraUp,
                        frameContext.cameraForward);

                    float r = 1.0F;
                    float g = 1.0F;
                    float b = 1.0F;
                    float a = 1.0F;
                    UnpackVfxRgba8UnormUVE(std::bit_cast<std::uint32_t>(inst.packedColorBits),
                                           inst.colorHdrScale, r, g, b, a);
                    for (const std::size_t cornerIdx : kTriOrder) {
                        VfxSpriteFallbackVertexUVE vtx{};
                        vtx.position = corners[cornerIdx];
                        vtx.uv = kCornerUvs[cornerIdx];
                        vtx.red = r;
                        vtx.green = g;
                        vtx.blue = b;
                        vtx.alpha = a;
                        vtx.frame = inst.frame;
                        m_stagingFallbackVertices.push_back(vtx);
                    }
                }
                range.fallbackVertexCount =
                    static_cast<std::uint32_t>(m_stagingFallbackVertices.size()) - range.firstFallbackVertex;
            }
        };
        populateRanges(m_opaqueRanges);
        populateRanges(m_translucentRanges);

        if (EnsureFallbackBufferCapacityUVE(renderDevice, m_stagingFallbackVertices.size())) {
            const std::span<const VfxSpriteFallbackVertexUVE> vtxSpan(m_stagingFallbackVertices);
            m_fallbackUploadReady = renderDevice.UpdateBufferUVE(m_fallbackVertexBuffer, std::as_bytes(vtxSpan));
        }
    }
}

std::size_t VfxSpriteRendererUVE::RecordOpaqueBatchesUVE(ICommandBufferUVE& commandBuffer,
                                                         const VfxSpriteFrameContextUVE& frameContext,
                                                         const AppearanceResolverUVE& resolver,
                                                         std::size_t* const outInstancesDrawn) {
    return RecordBatchListUVE(
        m_opaqueRanges, false, commandBuffer, frameContext, resolver, outInstancesDrawn);
}

std::size_t VfxSpriteRendererUVE::RecordTranslucentBatchesUVE(ICommandBufferUVE& commandBuffer,
                                                              const VfxSpriteFrameContextUVE& frameContext,
                                                              const AppearanceResolverUVE& resolver,
                                                              std::size_t* const outInstancesDrawn) {
    return RecordBatchListUVE(
        m_translucentRanges, true, commandBuffer, frameContext, resolver, outInstancesDrawn);
}

std::size_t VfxSpriteRendererUVE::RecordBatchListUVE(const std::vector<PreparedBatchRangeUVE>& ranges,
                                                     const bool translucentPass,
                                                     ICommandBufferUVE& commandBuffer,
                                                     const VfxSpriteFrameContextUVE& frameContext,
                                                     const AppearanceResolverUVE& resolver,
                                                     std::size_t* const outInstancesDrawn) {
    if (ranges.empty()) {
        return 0U;
    }

    std::size_t drawCalls = 0U;
    std::size_t instancesDrawn = 0U;
    const Math::Vector2UVE invViewport{
        1.0F / static_cast<float>(std::max(frameContext.viewportWidth, 1U)),
        1.0F / static_cast<float>(std::max(frameContext.viewportHeight, 1U)),
    };

    for (const PreparedBatchRangeUVE& range : ranges) {
        if (range.instanceCount == 0U) {
            continue;
        }
        const VfxSpriteBatchUVE& batch = m_preparedBatches[range.batchIndex];
        const VfxResolvedBatchAppearanceUVE appearance =
            resolver ? resolver(batch.texturePath, batch.materialPath) : VfxResolvedBatchAppearanceUVE{};
        if (!appearance.ready || appearance.albedoTexture == kInvalidTextureHandleUVE) {
            continue;
        }

        Shader::ShaderProgramUVE* program = nullptr;
        bool useSsboPath = false;

        if (m_ssboUploadReady && m_instanceStorageBuffer != kInvalidBufferHandleUVE) {
            Shader::ShaderProgramUVE* const candidate =
                batch.blend == 2 ? m_opaqueSsboProgram.get()
                : batch.blend == 0 ? m_additiveSsboProgram.get()
                                   : m_alphaSsboProgram.get();
            if (candidate != nullptr && candidate->IsValidUVE()) {
                program = candidate;
                useSsboPath = true;
            }
        }
        if (program == nullptr && m_fallbackUploadReady && m_fallbackVertexBuffer != kInvalidBufferHandleUVE) {
            Shader::ShaderProgramUVE* const candidate =
                batch.blend == 2 ? m_opaqueFallbackProgram.get()
                : batch.blend == 0 ? m_additiveFallbackProgram.get()
                                   : m_alphaFallbackProgram.get();
            if (candidate != nullptr && candidate->IsValidUVE()) {
                program = candidate;
                useSsboPath = false;
            }
        }
        if (program == nullptr) {
            continue;
        }

        program->SetMatrix4x4UVE("uViewProjection", frameContext.viewProjection);
        program->SetVector3UVE("uCameraPosition", frameContext.cameraPosition);
        program->SetIntUVE("uAlbedoTexture", static_cast<std::int32_t>(kVfxAlbedoTextureSlotUVE));
        program->SetVector4UVE("uMaterialTint", appearance.materialTint);
        program->SetVector3UVE("uMaterialEmissive", appearance.materialEmissive);
        program->SetIntUVE("uFlipbookRows", batch.rows);
        program->SetIntUVE("uFlipbookCols", batch.cols);
        program->SetIntUVE("uFrameBlend", batch.frameBlend);
        program->SetIntUVE("uBlendMode", batch.blend);
        program->SetFloatUVE("uAlphaCutoff", batch.alphaCutoff);
        program->SetFloatUVE("uCameraFadeNear", batch.cameraFadeNear);
        program->SetFloatUVE("uCameraFadeFar", batch.cameraFadeFar);
        program->SetFloatUVE("uEmissive", batch.emissive);

        if (useSsboPath) {
            program->SetVector3UVE("uCameraRight", frameContext.cameraRight);
            program->SetVector3UVE("uCameraUp", frameContext.cameraUp);
            program->SetVector3UVE("uCameraForward", frameContext.cameraForward);
            program->SetVector3UVE("uFacingAxis", batch.axis);
            program->SetIntUVE("uFacingMode", batch.facing);
            program->SetIntUVE("uInstanceBaseIndex", static_cast<std::int32_t>(range.firstInstance));
            program->SetFloatUVE("uStretchScale", batch.stretchScale);
        }

        if (translucentPass) {
            program->SetIntUVE("uSceneDepthTexture", static_cast<std::int32_t>(kVfxSceneDepthTextureSlotUVE));
            program->SetMatrix4x4UVE("uInverseProjection", frameContext.inverseProjection);
            program->SetVector2UVE("uInvViewportSize", invViewport);
            program->SetFloatUVE("uSoftDistance", batch.softDistance);
        }

        program->ApplyToUVE(commandBuffer);
        commandBuffer.BindTextureUVE(appearance.albedoTexture, kVfxAlbedoTextureSlotUVE);
        if (translucentPass && frameContext.sceneDepthTexture != kInvalidTextureHandleUVE) {
            commandBuffer.BindTextureUVE(frameContext.sceneDepthTexture, kVfxSceneDepthTextureSlotUVE);
        }

        if (useSsboPath) {
            if (m_dummyVertexBuffer != kInvalidBufferHandleUVE) {
                commandBuffer.BindVertexBufferUVE(m_dummyVertexBuffer);
            }
            commandBuffer.BindStorageBufferUVE(m_instanceStorageBuffer, kVfxInstanceStorageSlotUVE);
            commandBuffer.DrawUVE(
                static_cast<std::uint32_t>(kVfxSpriteVerticesPerQuadUVE), range.instanceCount);
        } else {
            commandBuffer.BindVertexBufferUVE(m_fallbackVertexBuffer);
            commandBuffer.DrawUVE(range.fallbackVertexCount, 1U);
        }

        ++drawCalls;
        instancesDrawn += range.instanceCount;
    }

    if (outInstancesDrawn != nullptr) {
        *outInstancesDrawn += instancesDrawn;
    }
    return drawCalls;
}

} // namespace UVE::Render
