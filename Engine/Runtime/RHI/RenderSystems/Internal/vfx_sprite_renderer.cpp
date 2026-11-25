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

constexpr std::uint32_t kVfxAlbedoTextureSlot = 0U;
constexpr std::uint32_t kVfxSceneDepthTextureSlot = 1U;
constexpr std::uint32_t kVfxInstanceStorageSlot = 0U;

[[nodiscard]] Math::Vector3 SafeNormalizeOr(const Math::Vector3& v,
                                                  const Math::Vector3& fallback) noexcept {
    if (!Math::IsFinite(v) || Math::LengthSquared(v) < 1.0e-8F) {
        return fallback;
    }
    return Math::Normalize(v);
}

/// Maps an IEEE-754 float to a monotonic unsigned 32-bit key suitable for descending (largest-first)
/// radix sorting: flips all bits for negative floats and flips the sign bit for non-negative floats,
/// then bitwise-inverts the ascending key so larger floats sort before smaller floats.
[[nodiscard]] constexpr std::uint32_t FloatToDescendingRadixKey(const float value) noexcept {
    const float safeVal = std::isfinite(value) ? value : 0.0F;
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(safeVal);
    const std::uint32_t mask = (bits & 0x80000000U) != 0U ? 0xFFFFFFFFU : 0x80000000U;
    const std::uint32_t ascendingKey = bits ^ mask;
    return ~ascendingKey;
}

[[nodiscard]] std::vector<VertexAttribute> VfxFallbackVertexLayout() {
    return {
        VertexAttribute{"POSITION", VertexAttributeFormat::Float3,
                           offsetof(VfxSpriteFallbackVertex, position)},
        VertexAttribute{"TEXCOORD", VertexAttributeFormat::Float2,
                           offsetof(VfxSpriteFallbackVertex, uv)},
        VertexAttribute{"COLOR", VertexAttributeFormat::Float4,
                           offsetof(VfxSpriteFallbackVertex, red)},
        VertexAttribute{"TEXCOORD1", VertexAttributeFormat::Float2,
                           offsetof(VfxSpriteFallbackVertex, frame)},
    };
}

[[nodiscard]] std::shared_ptr<Shader::ShaderProgram> CreateVfxProgramVariant(
    Shader::IShaderManager& shaderManager,
    const PipelineBlendMode blendMode,
    const bool depthTest,
    const bool depthWrite,
    const bool translucentDefines,
    const bool vertexFallback,
    const std::string_view debugName) {
    Shader::ShaderProgramStagesDesc desc;
    desc.vertexSource.stage = ShaderStage::Vertex;
    desc.vertexSource.virtualFilePath = std::string(Shader::BuiltIn::kVfxSpriteVirtualPath);
    desc.vertexSource.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kVfxSpriteSource);
    desc.vertexSource.extraDefines.push_back({"VERTEX_SHADER", "1"});
    if (vertexFallback) {
        desc.vertexSource.extraDefines.push_back({"UVE_VFX_VERTEX_FALLBACK", "1"});
    }
    desc.vertexSource.debugName = std::string(debugName) + " VS";

    desc.fragmentSource.stage = ShaderStage::Fragment;
    desc.fragmentSource.virtualFilePath = std::string(Shader::BuiltIn::kVfxSpriteVirtualPath);
    desc.fragmentSource.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kVfxSpriteSource);
    desc.fragmentSource.extraDefines.push_back({"FRAGMENT_SHADER", "1"});
    if (translucentDefines) {
        desc.fragmentSource.extraDefines.push_back({"UVE_VFX_TRANSLUCENT", "1"});
    }
    desc.fragmentSource.debugName = std::string(debugName) + " FS";

    if (vertexFallback) {
        desc.vertexLayout = VfxFallbackVertexLayout();
        desc.vertexStride = static_cast<std::uint32_t>(sizeof(VfxSpriteFallbackVertex));
    }
    desc.blendMode = blendMode;
    desc.cullMode = CullMode::None;
    desc.depthTestEnabled = depthTest;
    desc.depthWriteEnabled = depthWrite;
    desc.depthCompare = DepthCompare::LessOrEqual;
    desc.debugName = std::string(debugName);
    return shaderManager.CreateProgramFromStages(desc);
}

} // namespace

VfxSpriteQuadCorners ExpandVfxSpriteCorners(
    const Math::Vector3& center,
    const Math::Vector2& size,
    const float rotationRadians,
    const Math::Vector3& velocity,
    const std::int32_t facingMode,
    const float stretchScale,
    const Math::Vector3& facingAxis,
    const Math::Vector3& cameraPosition,
    const Math::Vector3& cameraRight,
    const Math::Vector3& cameraUp,
    const Math::Vector3& cameraForward) noexcept {
    const Math::Vector3 safeRight = SafeNormalizeOr(cameraRight, Math::Vector3{1.0F, 0.0F, 0.0F});
    const Math::Vector3 safeUp = SafeNormalizeOr(cameraUp, Math::Vector3{0.0F, 1.0F, 0.0F});
    const Math::Vector3 safeForward = SafeNormalizeOr(cameraForward, Math::Vector3{0.0F, 0.0F, -1.0F});

    Math::Vector3 basisRight = safeRight;
    Math::Vector3 basisUp = safeUp;
    Math::Vector2 effectiveSize{
        std::isfinite(size.x) ? std::max(0.0F, size.x) : 0.0F,
        std::isfinite(size.y) ? std::max(0.0F, size.y) : 0.0F,
    };

    if (facingMode == 1) {
        const Math::Vector3 toCamera = cameraPosition - center;
        const Math::Vector3 forwardDir = SafeNormalizeOr(toCamera, -safeForward);
        const Math::Vector3 refUp =
            std::fabs(forwardDir.y) < 0.99F ? Math::Vector3{0.0F, 1.0F, 0.0F}
                                            : Math::Vector3{1.0F, 0.0F, 0.0F};
        basisRight = SafeNormalizeOr(Math::Cross(refUp, forwardDir), safeRight);
        basisUp = SafeNormalizeOr(Math::Cross(forwardDir, basisRight), safeUp);
    } else if (facingMode == 2) {
        const float speed = Math::IsFinite(velocity) ? Math::Length(velocity) : 0.0F;
        const Math::Vector3 velDir = speed > 1.0e-5F ? (velocity * (1.0F / speed)) : safeUp;
        const Math::Vector3 viewDir = SafeNormalizeOr(cameraPosition - center, -safeForward);
        const Math::Vector3 side = Math::Cross(viewDir, velDir);
        basisRight = SafeNormalizeOr(side, safeRight);
        basisUp = velDir;
        const float safeStretch = std::isfinite(stretchScale) ? std::max(0.0F, stretchScale) : 0.0F;
        effectiveSize.y += speed * safeStretch;
    } else if (facingMode == 3) {
        const Math::Vector3 normalAxis = SafeNormalizeOr(facingAxis, Math::Vector3{0.0F, 1.0F, 0.0F});
        const Math::Vector3 refUp =
            std::fabs(normalAxis.y) < 0.99F ? Math::Vector3{0.0F, 1.0F, 0.0F}
                                            : Math::Vector3{0.0F, 0.0F, 1.0F};
        basisRight = SafeNormalizeOr(Math::Cross(refUp, normalAxis), Math::Vector3{1.0F, 0.0F, 0.0F});
        basisUp = SafeNormalizeOr(Math::Cross(normalAxis, basisRight), Math::Vector3{0.0F, 0.0F, 1.0F});
    }

    const float angle = std::isfinite(rotationRadians) ? rotationRadians : 0.0F;
    const float cosA = std::cos(angle);
    const float sinA = std::sin(angle);

    constexpr std::array<Math::Vector2, 4U> kUnitCorners{{
        {-0.5F, -0.5F},
        { 0.5F, -0.5F},
        { 0.5F,  0.5F},
        {-0.5F,  0.5F},
    }};

    VfxSpriteQuadCorners out{};
    for (std::size_t i = 0U; i < 4U; ++i) {
        const float lx = kUnitCorners[i].x * effectiveSize.x;
        const float ly = kUnitCorners[i].y * effectiveSize.y;
        const float rx = lx * cosA - ly * sinA;
        const float ry = lx * sinA + ly * cosA;
        out[i] = center + basisRight * rx + basisUp * ry;
    }
    return out;
}

void RadixSortVfxSprites(std::vector<VfxSpriteGpuInstance>& instances,
                            std::vector<float>& depths) {
    const std::size_t count = std::min(instances.size(), depths.size());
    if (count <= 1U) {
        return;
    }

    static thread_local std::vector<std::uint32_t> indices;
    static thread_local std::vector<std::uint32_t> keys;
    static thread_local std::vector<std::uint32_t> scratchIndices;
    static thread_local std::vector<std::uint32_t> scratchKeys;
    static thread_local std::vector<VfxSpriteGpuInstance> sortedInstances;
    static thread_local std::vector<float> sortedDepths;

    indices.resize(count);
    keys.resize(count);
    for (std::size_t i = 0U; i < count; ++i) {
        indices[i] = static_cast<std::uint32_t>(i);
        keys[i] = FloatToDescendingRadixKey(depths[i]);
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

float ComputeVfxSoftParticleFade(const float sceneLinearDepth,
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

float ComputeVfxCameraFade(const float distanceToCamera,
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

VfxFlipbookUvSample ComputeVfxFlipbookUvs(const Math::Vector2& cornerUv,
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

    const auto cellUv = [&](const std::int32_t f) noexcept -> Math::Vector2 {
        const std::int32_t col = f % safeCols;
        const std::int32_t row = f / safeCols;
        return Math::Vector2{
            (static_cast<float>(col) + uClamped) * invCols,
            (static_cast<float>(row) + vClamped) * invRows,
        };
    };

    VfxFlipbookUvSample out{};
    out.uv0 = cellUv(frame0);
    out.uv1 = cellUv(frame1);
    out.frame0 = frame0;
    out.frame1 = frame1;
    out.blendFactor = std::clamp(blendFactor, 0.0F, 1.0F);
    return out;
}

std::size_t ApplyVfxSpriteBudget(VfxDrawList& drawList, const std::size_t maxInstances) noexcept {
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

void VfxSpriteRenderer::Initialize(IRenderDevice& renderDevice,
                                         Shader::IShaderManager& shaderManager) {
    if (renderDevice.GetCapabilities().storageBuffers != RenderFeatureSupport::Unsupported) {
        m_opaqueSsboProgram = CreateVfxProgramVariant(
            shaderManager, PipelineBlendMode::Opaque, true, true, false, false, "VfxSpriteOpaqueSSBO");
        m_alphaSsboProgram = CreateVfxProgramVariant(
            shaderManager, PipelineBlendMode::PremultipliedAlpha, false, false, true, false, "VfxSpriteAlphaSSBO");
        m_additiveSsboProgram = CreateVfxProgramVariant(
            shaderManager, PipelineBlendMode::Additive, false, false, true, false, "VfxSpriteAdditiveSSBO");
    }

    m_opaqueFallbackProgram = CreateVfxProgramVariant(
        shaderManager, PipelineBlendMode::Opaque, true, true, false, true, "VfxSpriteOpaqueFallback");
    m_alphaFallbackProgram = CreateVfxProgramVariant(
        shaderManager, PipelineBlendMode::PremultipliedAlpha, false, false, true, true, "VfxSpriteAlphaFallback");
    m_additiveFallbackProgram = CreateVfxProgramVariant(
        shaderManager, PipelineBlendMode::Additive, false, false, true, true, "VfxSpriteAdditiveFallback");

    if (m_dummyVertexBuffer == kInvalidBufferHandle) {
        const std::array<float, 6U> dummyZeros{};
        m_dummyVertexBuffer = renderDevice.CreateBuffer(
            BufferDesc{sizeof(dummyZeros), BufferUsage::Vertex},
            std::as_bytes(std::span<const float>(dummyZeros)));
    }
}

void VfxSpriteRenderer::Destroy(IRenderDevice& renderDevice) noexcept {
    if (m_instanceStorageBuffer != kInvalidBufferHandle) {
        renderDevice.DestroyBuffer(m_instanceStorageBuffer);
        m_instanceStorageBuffer = kInvalidBufferHandle;
    }
    m_instanceStorageCapacity = 0U;

    if (m_dummyVertexBuffer != kInvalidBufferHandle) {
        renderDevice.DestroyBuffer(m_dummyVertexBuffer);
        m_dummyVertexBuffer = kInvalidBufferHandle;
    }

    if (m_fallbackVertexBuffer != kInvalidBufferHandle) {
        renderDevice.DestroyBuffer(m_fallbackVertexBuffer);
        m_fallbackVertexBuffer = kInvalidBufferHandle;
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

bool VfxSpriteRenderer::AreOpaqueProgramsReady() const noexcept {
    return (m_opaqueSsboProgram != nullptr && m_opaqueSsboProgram->IsValid()) ||
           (m_opaqueFallbackProgram != nullptr && m_opaqueFallbackProgram->IsValid());
}

bool VfxSpriteRenderer::AreTranslucentProgramsReady() const noexcept {
    const bool ssboReady = m_alphaSsboProgram != nullptr && m_alphaSsboProgram->IsValid() &&
                           m_additiveSsboProgram != nullptr && m_additiveSsboProgram->IsValid();
    const bool fallbackReady = m_alphaFallbackProgram != nullptr && m_alphaFallbackProgram->IsValid() &&
                               m_additiveFallbackProgram != nullptr && m_additiveFallbackProgram->IsValid();
    return ssboReady || fallbackReady;
}

bool VfxSpriteRenderer::EnsureStorageBufferCapacity(IRenderDevice& renderDevice,
                                                          const std::size_t instanceCount) {
    if (instanceCount == 0U) {
        return true;
    }
    if (m_instanceStorageBuffer != kInvalidBufferHandle && m_instanceStorageCapacity >= instanceCount) {
        return true;
    }
    std::size_t newCapacity = std::max<std::size_t>(256U, m_instanceStorageCapacity);
    while (newCapacity < instanceCount) {
        newCapacity = std::min(newCapacity * 2U, kMaximumVfxSpriteInstancesPerFrame);
        if (newCapacity < instanceCount) {
            newCapacity = instanceCount;
            break;
        }
    }
    if (m_instanceStorageBuffer != kInvalidBufferHandle) {
        renderDevice.DestroyBuffer(m_instanceStorageBuffer);
        m_instanceStorageBuffer = kInvalidBufferHandle;
        m_instanceStorageCapacity = 0U;
    }
    m_instanceStorageBuffer = renderDevice.CreateBuffer(
        BufferDesc{newCapacity * sizeof(VfxSpriteGpuInstance), BufferUsage::Storage});
    if (m_instanceStorageBuffer == kInvalidBufferHandle) {
        return false;
    }
    m_instanceStorageCapacity = newCapacity;
    return true;
}

bool VfxSpriteRenderer::EnsureFallbackBufferCapacity(IRenderDevice& renderDevice,
                                                           const std::size_t vertexCount) {
    if (vertexCount == 0U) {
        return true;
    }
    if (m_fallbackVertexBuffer != kInvalidBufferHandle && m_fallbackVertexCapacity >= vertexCount) {
        return true;
    }
    std::size_t newCapacity = std::max<std::size_t>(1536U, m_fallbackVertexCapacity);
    while (newCapacity < vertexCount) {
        newCapacity *= 2U;
    }
    if (m_fallbackVertexBuffer != kInvalidBufferHandle) {
        renderDevice.DestroyBuffer(m_fallbackVertexBuffer);
        m_fallbackVertexBuffer = kInvalidBufferHandle;
        m_fallbackVertexCapacity = 0U;
    }
    m_fallbackVertexBuffer = renderDevice.CreateBuffer(
        BufferDesc{newCapacity * sizeof(VfxSpriteFallbackVertex), BufferUsage::Vertex});
    if (m_fallbackVertexBuffer == kInvalidBufferHandle) {
        return false;
    }
    m_fallbackVertexCapacity = newCapacity;
    return true;
}

void VfxSpriteRenderer::PrepareFrameBatches(IRenderDevice& renderDevice,
                                                  const VfxDrawList& drawList,
                                                  const VfxSpriteFrameContext& frameContext) {
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
    for (VfxSpriteBatch& batch : m_preparedBatches) {
        if (batch.instances.empty()) {
            continue;
        }
        if (batch.blend == 1 && batch.sortDepths.size() == batch.instances.size()) {
            RadixSortVfxSprites(batch.instances, batch.sortDepths);
        }
        totalInstances += batch.instances.size();
    }

    if (totalInstances == 0U) {
        return;
    }

    m_stagingInstances.reserve(totalInstances);
    for (std::size_t batchIdx = 0U; batchIdx < m_preparedBatches.size(); ++batchIdx) {
        const VfxSpriteBatch& batch = m_preparedBatches[batchIdx];
        if (batch.instances.empty()) {
            continue;
        }

        PreparedBatchRange range{};
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

    if (EnsureStorageBufferCapacity(renderDevice, m_stagingInstances.size())) {
        const std::span<const VfxSpriteGpuInstance> instSpan(m_stagingInstances);
        m_ssboUploadReady = renderDevice.UpdateBuffer(m_instanceStorageBuffer, std::as_bytes(instSpan));
    }

    // If SSBO upload is unavailable, expand vertices on CPU for the fallback attribute pipeline.
    if (!m_ssboUploadReady) {
        m_stagingFallbackVertices.reserve(m_stagingInstances.size() * kVfxSpriteVerticesPerQuad);
        constexpr std::array<Math::Vector2, 4U> kCornerUvs{{
            {0.0F, 1.0F},
            {1.0F, 1.0F},
            {1.0F, 0.0F},
            {0.0F, 0.0F},
        }};
        constexpr std::array<std::size_t, 6U> kTriOrder{{0U, 1U, 2U, 0U, 2U, 3U}};

        auto populateRanges = [&](std::vector<PreparedBatchRange>& ranges) {
            for (PreparedBatchRange& range : ranges) {
                const VfxSpriteBatch& batch = m_preparedBatches[range.batchIndex];
                range.firstFallbackVertex = static_cast<std::uint32_t>(m_stagingFallbackVertices.size());
                for (const VfxSpriteGpuInstance& inst : batch.instances) {
                    const VfxSpriteQuadCorners corners = ExpandVfxSpriteCorners(
                        Math::Vector3{inst.posX, inst.posY, inst.posZ},
                        Math::Vector2{inst.sizeX, inst.sizeY},
                        inst.rotationRad,
                        Math::Vector3{inst.velX, inst.velY, inst.velZ},
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
                    UnpackVfxRgba8Unorm(std::bit_cast<std::uint32_t>(inst.packedColorBits),
                                           inst.colorHdrScale, r, g, b, a);
                    for (const std::size_t cornerIdx : kTriOrder) {
                        VfxSpriteFallbackVertex vtx{};
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

        if (EnsureFallbackBufferCapacity(renderDevice, m_stagingFallbackVertices.size())) {
            const std::span<const VfxSpriteFallbackVertex> vtxSpan(m_stagingFallbackVertices);
            m_fallbackUploadReady = renderDevice.UpdateBuffer(m_fallbackVertexBuffer, std::as_bytes(vtxSpan));
        }
    }
}

std::size_t VfxSpriteRenderer::RecordOpaqueBatches(ICommandBuffer& commandBuffer,
                                                         const VfxSpriteFrameContext& frameContext,
                                                         const AppearanceResolver& resolver,
                                                         std::size_t* const outInstancesDrawn) {
    return RecordBatchList(
        m_opaqueRanges, false, commandBuffer, frameContext, resolver, outInstancesDrawn);
}

std::size_t VfxSpriteRenderer::RecordTranslucentBatches(ICommandBuffer& commandBuffer,
                                                              const VfxSpriteFrameContext& frameContext,
                                                              const AppearanceResolver& resolver,
                                                              std::size_t* const outInstancesDrawn) {
    return RecordBatchList(
        m_translucentRanges, true, commandBuffer, frameContext, resolver, outInstancesDrawn);
}

std::size_t VfxSpriteRenderer::RecordBatchList(const std::vector<PreparedBatchRange>& ranges,
                                                     const bool translucentPass,
                                                     ICommandBuffer& commandBuffer,
                                                     const VfxSpriteFrameContext& frameContext,
                                                     const AppearanceResolver& resolver,
                                                     std::size_t* const outInstancesDrawn) {
    if (ranges.empty()) {
        return 0U;
    }

    std::size_t drawCalls = 0U;
    std::size_t instancesDrawn = 0U;
    const Math::Vector2 invViewport{
        1.0F / static_cast<float>(std::max(frameContext.viewportWidth, 1U)),
        1.0F / static_cast<float>(std::max(frameContext.viewportHeight, 1U)),
    };

    for (const PreparedBatchRange& range : ranges) {
        if (range.instanceCount == 0U) {
            continue;
        }
        const VfxSpriteBatch& batch = m_preparedBatches[range.batchIndex];
        const VfxResolvedBatchAppearance appearance =
            resolver ? resolver(batch.texturePath, batch.materialPath) : VfxResolvedBatchAppearance{};
        if (!appearance.ready || appearance.albedoTexture == kInvalidTextureHandle) {
            continue;
        }

        Shader::ShaderProgram* program = nullptr;
        bool useSsboPath = false;

        if (m_ssboUploadReady && m_instanceStorageBuffer != kInvalidBufferHandle) {
            Shader::ShaderProgram* const candidate =
                batch.blend == 2 ? m_opaqueSsboProgram.get()
                : batch.blend == 0 ? m_additiveSsboProgram.get()
                                   : m_alphaSsboProgram.get();
            if (candidate != nullptr && candidate->IsValid()) {
                program = candidate;
                useSsboPath = true;
            }
        }
        if (program == nullptr && m_fallbackUploadReady && m_fallbackVertexBuffer != kInvalidBufferHandle) {
            Shader::ShaderProgram* const candidate =
                batch.blend == 2 ? m_opaqueFallbackProgram.get()
                : batch.blend == 0 ? m_additiveFallbackProgram.get()
                                   : m_alphaFallbackProgram.get();
            if (candidate != nullptr && candidate->IsValid()) {
                program = candidate;
                useSsboPath = false;
            }
        }
        if (program == nullptr) {
            continue;
        }

        program->SetMatrix4x4("uViewProjection", frameContext.viewProjection);
        program->SetVector3("uCameraPosition", frameContext.cameraPosition);
        program->SetInt("uAlbedoTexture", static_cast<std::int32_t>(kVfxAlbedoTextureSlot));
        program->SetVector4("uMaterialTint", appearance.materialTint);
        program->SetVector3("uMaterialEmissive", appearance.materialEmissive);
        program->SetInt("uFlipbookRows", batch.rows);
        program->SetInt("uFlipbookCols", batch.cols);
        program->SetInt("uFrameBlend", batch.frameBlend);
        program->SetInt("uBlendMode", batch.blend);
        program->SetInt("uFacingMode", batch.facing);
        program->SetFloat("uAlphaCutoff", batch.alphaCutoff);
        program->SetFloat("uCameraFadeNear", batch.cameraFadeNear);
        program->SetFloat("uCameraFadeFar", batch.cameraFadeFar);
        program->SetFloat("uEmissive", batch.emissive);

        if (useSsboPath) {
            program->SetVector3("uCameraRight", frameContext.cameraRight);
            program->SetVector3("uCameraUp", frameContext.cameraUp);
            program->SetVector3("uCameraForward", frameContext.cameraForward);
            program->SetVector3("uFacingAxis", batch.axis);
            program->SetInt("uInstanceBaseIndex", static_cast<std::int32_t>(range.firstInstance));
            program->SetFloat("uStretchScale", batch.stretchScale);
        }

        if (translucentPass) {
            program->SetInt("uSceneDepthTexture", static_cast<std::int32_t>(kVfxSceneDepthTextureSlot));
            program->SetMatrix4x4("uInverseProjection", frameContext.inverseProjection);
            program->SetVector2("uInvViewportSize", invViewport);
            program->SetFloat("uSoftDistance", batch.softDistance);
            program->SetFloat("uDepthNdcScale", frameContext.depthNdcScale);
            program->SetFloat("uDepthNdcBias", frameContext.depthNdcBias);
        }

        program->ApplyTo(commandBuffer);
        commandBuffer.BindTexture(appearance.albedoTexture, kVfxAlbedoTextureSlot);
        if (translucentPass && frameContext.sceneDepthTexture != kInvalidTextureHandle) {
            commandBuffer.BindTexture(frameContext.sceneDepthTexture, kVfxSceneDepthTextureSlot);
        }

        if (useSsboPath) {
            if (m_dummyVertexBuffer != kInvalidBufferHandle) {
                commandBuffer.BindVertexBuffer(m_dummyVertexBuffer);
            }
            commandBuffer.BindStorageBuffer(m_instanceStorageBuffer, kVfxInstanceStorageSlot);
            commandBuffer.Draw(
                static_cast<std::uint32_t>(kVfxSpriteVerticesPerQuad), range.instanceCount);
        } else {
            commandBuffer.BindVertexBuffer(m_fallbackVertexBuffer);
            commandBuffer.Draw(range.fallbackVertexCount, 1U);
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
