// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/rhi_null/null_render_device_uve.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <iterator>
#include <mutex>
#include <unordered_map>
#include <utility>

#include "null_command_buffer_uve.h"
#include "uve/logging/assert_uve.h"
#include "uve/logging/logging_macros_uve.h"

namespace UVE::Render {

struct NullRenderDeviceUVE::ImplUVE {
    // CS3: the Null backend now keeps each buffer's BYTES, not just its descriptor. Without
    // them ReadbackBufferUVE could only ever hand back zeros, which would make a headless
    // "what did the write leave behind" assertion a lie; with them the null device models the
    // one thing a memory-less backend can honestly model - host-visible buffer contents.
    struct BufferRecordUVE {
        BufferDescUVE desc;
        std::vector<std::byte> bytes;
    };
    std::unordered_map<std::uint32_t, BufferRecordUVE> buffers;
    std::uint32_t nextBufferHandle = 1;
    std::unordered_map<std::uint32_t, TextureDescUVE> textures;
    std::uint32_t nextTextureHandle = 1;
    std::unordered_map<std::uint32_t, SamplerDescUVE> samplers;
    std::uint32_t nextSamplerHandle = 1;
    std::uint64_t textureCreateAttemptCount = 0;
    std::unordered_map<std::uint32_t, ShaderDescUVE> shaders;
    std::uint32_t nextShaderHandle = 1;
    std::unordered_map<std::uint32_t, PipelineDescUVE> pipelines;
    std::unordered_map<std::uint32_t, ComputePipelineDescUVE> computePipelines; // M5a, same handle domain
    std::uint32_t nextPipelineHandle = 1;
    // M4: SubmitUVE may be called from any thread (the same contract the Vulkan backend
    // honors for its submission FIFO) — the spy write happens under this lock. Readers of
    // GetLastSubmittedCommandsUVE() get a const reference, so they must be externally
    // quiesced (the test-spy pattern: submit, join all threads, then inspect).
    std::mutex submissionMutex;
    std::vector<RecordedCommandUVE> lastSubmittedCommands;
    std::uint64_t presentCallCount = 0;
    std::unordered_map<std::uint32_t, bool> fences;
    std::uint32_t nextFence = 1U;
    struct QueryRecordUVE {
        RenderQueryKindUVE kind;
        bool issued = false;
    };
    // Timeline semaphores: real counters, real blocking waits. `condition` is notified on every
    // signal and on destruction so no waiter can outlive the device.
    struct SemaphoreRecordUVE {
        std::uint64_t value = 0U;
    };
    mutable std::mutex semaphoreMutex;
    std::condition_variable semaphoreCondition;
    std::unordered_map<std::uint32_t, SemaphoreRecordUVE> semaphores;
    std::uint32_t nextSemaphore = 1U;
    std::unordered_map<std::uint32_t, QueryRecordUVE> queries;
    std::uint32_t nextQuery = 1U, activeOcclusion = 0U;
    std::vector<RecordedDeviceOperationUVE> operations;
    std::uint64_t droppedOperations = 0U;
    void RecordUVE(RecordedDeviceOperationUVE operation) {
        if (operations.size() == 256U) {
            operations.erase(operations.begin());
            if (droppedOperations != UINT64_MAX)
                ++droppedOperations;
        }
        operations.push_back(std::move(operation));
    }
};

NullRenderDeviceUVE::NullRenderDeviceUVE() : m_impl(std::make_unique<ImplUVE>()) {
    m_impl->operations.reserve(256U);
}

NullRenderDeviceUVE::~NullRenderDeviceUVE() = default;

BufferHandleUVE NullRenderDeviceUVE::CreateBufferUVE(const BufferDescUVE& desc,
                                                       std::span<const std::byte> initialData) {
    if (!ValidateBufferUploadUVE(desc, initialData)) {
        UVE_ERROR("NullRenderDeviceUVE: CreateBufferUVE initial data exceeds buffer size");
        return kInvalidBufferHandleUVE;
    }
    if (!IsBufferUsageValidUVE(desc.usage)) {
        UVE_ERROR("NullRenderDeviceUVE: CreateBufferUVE received an unknown buffer usage");
        return kInvalidBufferHandleUVE;
    }
    const std::uint32_t handleValue = m_impl->nextBufferHandle++;
    // Zero-filled to the declared size, then the optional initial upload on top: the same
    // observable state a real backend leaves behind for a freshly created buffer.
    ImplUVE::BufferRecordUVE record{desc, std::vector<std::byte>(desc.sizeBytes, std::byte{0})};
    if (!initialData.empty()) {
        std::copy(initialData.begin(), initialData.end(), record.bytes.begin());
    }
    m_impl->buffers.emplace(handleValue, std::move(record));
    return BufferHandleUVE{handleValue};
}

void NullRenderDeviceUVE::DestroyBufferUVE(BufferHandleUVE buffer) {
    if (m_impl->buffers.erase(buffer.value) == 0) {
        UVE_ERROR("NullRenderDeviceUVE: DestroyBufferUVE called with an unknown or already-destroyed handle ({})",
                   buffer.value);
    }
}

bool NullRenderDeviceUVE::UpdateBufferUVE(BufferHandleUVE buffer, std::span<const std::byte> data,
                                           std::uint64_t offsetBytes) {
    const auto iterator = m_impl->buffers.find(buffer.value);
    if (iterator == m_impl->buffers.end()) {
        UVE_ERROR("NullRenderDeviceUVE: UpdateBufferUVE called with an unknown handle ({})", buffer.value);
        return false;
    }
    if (!ValidateBufferUpdateUVE(iterator->second.desc.sizeBytes, data.size(), offsetBytes)) {
        UVE_ERROR("NullRenderDeviceUVE: UpdateBufferUVE write of {} bytes at offset {} exceeds buffer size {}",
                   data.size(), offsetBytes, iterator->second.desc.sizeBytes);
        return false;
    }
    std::copy(data.begin(), data.end(),
              iterator->second.bytes.begin() + static_cast<std::ptrdiff_t>(offsetBytes));
    return true;
}

bool NullRenderDeviceUVE::ReadbackBufferUVE(BufferHandleUVE buffer, std::span<std::byte> outData,
                                             std::uint64_t offsetBytes) {
    const auto iterator = m_impl->buffers.find(buffer.value);
    if (iterator == m_impl->buffers.end()) {
        UVE_ERROR("NullRenderDeviceUVE: ReadbackBufferUVE called with an unknown handle ({})", buffer.value);
        return false;
    }
    // Same range rule as the write direction - a read that runs off the end is the same
    // authoring bug as a write that does.
    if (!ValidateBufferUpdateUVE(iterator->second.desc.sizeBytes, outData.size(), offsetBytes)) {
        UVE_ERROR("NullRenderDeviceUVE: ReadbackBufferUVE read of {} bytes at offset {} exceeds buffer size {}",
                   outData.size(), offsetBytes, iterator->second.desc.sizeBytes);
        return false;
    }
    if (outData.empty()) {
        return true;
    }
    // No device, no synchronization needed: the null backend's "GPU memory" is this vector.
    const auto begin = iterator->second.bytes.begin() + static_cast<std::ptrdiff_t>(offsetBytes);
    std::copy(begin, begin + static_cast<std::ptrdiff_t>(outData.size()), outData.begin());
    return true;
}

TextureHandleUVE NullRenderDeviceUVE::CreateTextureUVE(const TextureDescUVE& desc,
                                                         std::span<const std::byte> initialData) {
    ++m_impl->textureCreateAttemptCount;
    if (!ValidateTextureUploadUVE(desc, initialData)) {
        UVE_ERROR("NullRenderDeviceUVE: CreateTextureUVE received an invalid descriptor or initial upload");
        return kInvalidTextureHandleUVE;
    }
    static_cast<void>(initialData); // NullRenderDeviceUVE performs no real upload, bookkeeping only.
    const std::uint32_t handleValue = m_impl->nextTextureHandle++;
    m_impl->textures.emplace(handleValue, desc);
    return TextureHandleUVE{handleValue};
}

bool NullRenderDeviceUVE::SupportsTextureFormatUVE(const TextureFormatUVE format,
                                                        const TextureColorSpaceUVE colorSpace) const noexcept {
    const bool formatKnown = GetTextureFormatBlockInfoUVE(format).bytes != 0U;
    switch (colorSpace) {
        case TextureColorSpaceUVE::Linear:
            return formatKnown && !IsTextureFormatCompressedUVE(format);
        case TextureColorSpaceUVE::Srgb:
            return formatKnown && IsTextureFormatSrgbCapableUVE(format) &&
                   !IsTextureFormatCompressedUVE(format);
    }
    return false;
}

void NullRenderDeviceUVE::DestroyTextureUVE(TextureHandleUVE texture) {
    if (m_impl->textures.erase(texture.value) == 0) {
        UVE_ERROR("NullRenderDeviceUVE: DestroyTextureUVE called with an unknown or already-destroyed handle ({})",
                   texture.value);
    }
}

SamplerHandleUVE NullRenderDeviceUVE::CreateSamplerUVE(const SamplerDescUVE& desc) {
    if (!IsSamplerDescValidUVE(desc)) {
        UVE_ERROR("NullRenderDeviceUVE: CreateSamplerUVE received an invalid sampler descriptor");
        return kInvalidSamplerHandleUVE;
    }
    const std::uint32_t handleValue = m_impl->nextSamplerHandle++;
    m_impl->samplers.emplace(handleValue, desc);
    return SamplerHandleUVE{handleValue};
}

void NullRenderDeviceUVE::DestroySamplerUVE(SamplerHandleUVE sampler) {
    if (m_impl->samplers.erase(sampler.value) == 0) {
        UVE_ERROR("NullRenderDeviceUVE: DestroySamplerUVE called with an unknown or already-destroyed handle ({})",
                   sampler.value);
    }
}

ShaderHandleUVE NullRenderDeviceUVE::CreateShaderUVE(const ShaderDescUVE& desc, std::string* outInfoLog) {
    static_cast<void>(outInfoLog); // NullRenderDeviceUVE never compiles anything real - nothing to log.
    if (!IsShaderStageValidUVE(desc.stage)) {
        UVE_ERROR("NullRenderDeviceUVE: CreateShaderUVE received an unknown shader stage");
        return kInvalidShaderHandleUVE;
    }
    const std::uint32_t handleValue = m_impl->nextShaderHandle++;
    m_impl->shaders.emplace(handleValue, desc);
    return ShaderHandleUVE{handleValue};
}

void NullRenderDeviceUVE::DestroyShaderUVE(ShaderHandleUVE shader) {
    if (m_impl->shaders.erase(shader.value) == 0) {
        UVE_ERROR("NullRenderDeviceUVE: DestroyShaderUVE called with an unknown or already-destroyed handle ({})",
                   shader.value);
    }
}

PipelineHandleUVE NullRenderDeviceUVE::CreatePipelineUVE(const PipelineDescUVE& desc, std::string* outInfoLog) {
    static_cast<void>(outInfoLog); // NullRenderDeviceUVE never links anything real - nothing to log.
    if (!IsVertexLayoutValidUVE(desc.vertexLayout)) {
        UVE_ERROR("NullRenderDeviceUVE: CreatePipelineUVE received an unknown vertex attribute format");
        return kInvalidPipelineHandleUVE;
    }
    if (!IsPipelineBlendModeValidUVE(desc.blendMode)) {
        UVE_ERROR("NullRenderDeviceUVE: CreatePipelineUVE received an unknown blend mode");
        return kInvalidPipelineHandleUVE;
    }
    if (!IsPrimitiveTopologyValidUVE(desc.topology)) {
        UVE_ERROR("NullRenderDeviceUVE: CreatePipelineUVE received an unknown primitive topology");
        return kInvalidPipelineHandleUVE;
    }
    if (!IsCullModeValidUVE(desc.cullMode)) {
        UVE_ERROR("NullRenderDeviceUVE: CreatePipelineUVE received an unknown cull mode");
        return kInvalidPipelineHandleUVE;
    }
    if (!IsFrontFaceValidUVE(desc.frontFace)) {
        UVE_ERROR("NullRenderDeviceUVE: CreatePipelineUVE received an unknown front-face winding");
        return kInvalidPipelineHandleUVE;
    }
    if (!IsFillModeValidUVE(desc.fillMode)) {
        UVE_ERROR("NullRenderDeviceUVE: CreatePipelineUVE received an unknown fill mode");
        return kInvalidPipelineHandleUVE;
    }
    if (!IsDepthCompareValidUVE(desc.depthCompare)) {
        UVE_ERROR("NullRenderDeviceUVE: CreatePipelineUVE received an unknown depth comparison");
        return kInvalidPipelineHandleUVE;
    }
    if (!IsStencilStateValidUVE(desc.stencil)) {
        UVE_ERROR("NullRenderDeviceUVE: CreatePipelineUVE received an unknown stencil state");
        return kInvalidPipelineHandleUVE;
    }
    if (!m_impl->shaders.contains(desc.vertexShader.value) || !m_impl->shaders.contains(desc.fragmentShader.value)) {
        UVE_ERROR("NullRenderDeviceUVE: CreatePipelineUVE referenced an unknown vertex or fragment shader handle");
        return kInvalidPipelineHandleUVE;
    }
    const auto rejectStage = [outInfoLog](const char* message) {
        UVE_ERROR("NullRenderDeviceUVE: CreatePipelineUVE {}", message);
        if (outInfoLog != nullptr) *outInfoLog = message;
        return kInvalidPipelineHandleUVE;
    };
    if (m_impl->shaders.at(desc.vertexShader.value).stage != ShaderStageUVE::Vertex ||
        m_impl->shaders.at(desc.fragmentShader.value).stage != ShaderStageUVE::Fragment) {
        return rejectStage("vertex/fragment shader handles are bound to the wrong stages");
    }
    if (desc.geometryShader != kInvalidShaderHandleUVE) {
        const auto geometry = m_impl->shaders.find(desc.geometryShader.value);
        if (geometry == m_impl->shaders.end()) {
            return rejectStage("geometry shader handle does not reference a live shader");
        }
        if (geometry->second.stage != ShaderStageUVE::Geometry) {
            return rejectStage("geometry shader slot requires a Geometry-stage shader");
        }
    }
    const std::uint32_t handleValue = m_impl->nextPipelineHandle++;
    m_impl->pipelines.emplace(handleValue, desc);
    return PipelineHandleUVE{handleValue};
}

PipelineHandleUVE NullRenderDeviceUVE::CreateComputePipelineUVE(const ComputePipelineDescUVE& desc,
                                                                 std::string* outInfoLog) {
    static_cast<void>(outInfoLog); // NullRenderDeviceUVE never links anything real - nothing to log.
    const auto shader = m_impl->shaders.find(desc.computeShader.value);
    if (shader == m_impl->shaders.end()) {
        UVE_ERROR("NullRenderDeviceUVE: CreateComputePipelineUVE referenced an unknown shader handle");
        return kInvalidPipelineHandleUVE;
    }
    if (shader->second.stage != ShaderStageUVE::Compute) {
        UVE_ERROR("NullRenderDeviceUVE: CreateComputePipelineUVE requires a Compute-stage shader");
        return kInvalidPipelineHandleUVE;
    }
    const std::uint32_t handleValue = m_impl->nextPipelineHandle++;
    m_impl->computePipelines.emplace(handleValue, desc);
    return PipelineHandleUVE{handleValue};
}

void NullRenderDeviceUVE::DestroyPipelineUVE(PipelineHandleUVE pipeline) {
    if (m_impl->pipelines.erase(pipeline.value) == 0 &&
        m_impl->computePipelines.erase(pipeline.value) == 0) {
        UVE_ERROR("NullRenderDeviceUVE: DestroyPipelineUVE called with an unknown or already-destroyed handle ({})",
                   pipeline.value);
    }
}

std::vector<UniformReflectionUVE> NullRenderDeviceUVE::GetPipelineUniformsUVE(PipelineHandleUVE pipeline) const {
    static_cast<void>(pipeline); // NullRenderDeviceUVE never really links anything, so nothing to reflect.
    return {};
}

bool NullRenderDeviceUVE::GetPipelineBinaryUVE(PipelineHandleUVE pipeline, std::vector<std::byte>& outBinary,
                                                std::uint32_t& outFormat) const {
    static_cast<void>(pipeline);
    static_cast<void>(outBinary);
    static_cast<void>(outFormat);
    return false; // NullRenderDeviceUVE never compiles anything real, so it has no binary to give.
}

PipelineHandleUVE NullRenderDeviceUVE::CreatePipelineFromBinaryUVE(std::span<const std::byte> binary,
                                                                    std::uint32_t format,
                                                                    const PipelineBinaryDescUVE& desc) {
    if (!IsVertexLayoutValidUVE(desc.vertexLayout)) {
        UVE_ERROR("NullRenderDeviceUVE: CreatePipelineFromBinaryUVE received an unknown vertex attribute format");
        return kInvalidPipelineHandleUVE;
    }
    if (!IsPipelineBlendModeValidUVE(desc.blendMode)) {
        UVE_ERROR("NullRenderDeviceUVE: CreatePipelineFromBinaryUVE received an unknown blend mode");
        return kInvalidPipelineHandleUVE;
    }
    if (!IsPrimitiveTopologyValidUVE(desc.topology)) {
        UVE_ERROR("NullRenderDeviceUVE: CreatePipelineFromBinaryUVE received an unknown primitive topology");
        return kInvalidPipelineHandleUVE;
    }
    if (!IsCullModeValidUVE(desc.cullMode)) {
        UVE_ERROR("NullRenderDeviceUVE: CreatePipelineFromBinaryUVE received an unknown cull mode");
        return kInvalidPipelineHandleUVE;
    }
    if (!IsFrontFaceValidUVE(desc.frontFace)) {
        UVE_ERROR("NullRenderDeviceUVE: CreatePipelineFromBinaryUVE received an unknown front-face winding");
        return kInvalidPipelineHandleUVE;
    }
    if (!IsFillModeValidUVE(desc.fillMode)) {
        UVE_ERROR("NullRenderDeviceUVE: CreatePipelineFromBinaryUVE received an unknown fill mode");
        return kInvalidPipelineHandleUVE;
    }
    if (!IsDepthCompareValidUVE(desc.depthCompare)) {
        UVE_ERROR("NullRenderDeviceUVE: CreatePipelineFromBinaryUVE received an unknown depth comparison");
        return kInvalidPipelineHandleUVE;
    }
    if (!IsStencilStateValidUVE(desc.stencil)) {
        UVE_ERROR("NullRenderDeviceUVE: CreatePipelineFromBinaryUVE received an unknown stencil state");
        return kInvalidPipelineHandleUVE;
    }
    static_cast<void>(binary); // NullRenderDeviceUVE never inspects binary contents.
    static_cast<void>(format);
    PipelineDescUVE bookkeepingDesc;
    bookkeepingDesc.vertexShader = kInvalidShaderHandleUVE; // no shader was ever compiled for this pipeline
    bookkeepingDesc.fragmentShader = kInvalidShaderHandleUVE;
    bookkeepingDesc.vertexLayout = desc.vertexLayout;
    bookkeepingDesc.topology = desc.topology;
    bookkeepingDesc.depthTestEnabled = desc.depthTestEnabled;
    bookkeepingDesc.depthWriteEnabled = desc.depthWriteEnabled;
    bookkeepingDesc.vertexStride = desc.vertexStride;
    bookkeepingDesc.blendMode = desc.blendMode;
    bookkeepingDesc.cullMode = desc.cullMode;
    bookkeepingDesc.frontFace = desc.frontFace;
    bookkeepingDesc.fillMode = desc.fillMode;
    bookkeepingDesc.depthBiasEnabled = desc.depthBiasEnabled;
    bookkeepingDesc.depthBiasConstantFactor = desc.depthBiasConstantFactor;
    bookkeepingDesc.depthBiasSlopeFactor = desc.depthBiasSlopeFactor;
    bookkeepingDesc.depthCompare = desc.depthCompare;
    bookkeepingDesc.stencil = desc.stencil;

    const std::uint32_t handleValue = m_impl->nextPipelineHandle++;
    m_impl->pipelines.emplace(handleValue, bookkeepingDesc);
    return PipelineHandleUVE{handleValue};
}

std::unique_ptr<ICommandBufferUVE> NullRenderDeviceUVE::CreateCommandBufferUVE() {
    return std::make_unique<NullCommandBufferUVE>();
}

void NullRenderDeviceUVE::SubmitUVE(std::unique_ptr<ICommandBufferUVE> commandBuffer) {
    UVE_ASSERT(commandBuffer != nullptr);
    auto* const nullCommandBuffer = dynamic_cast<NullCommandBufferUVE*>(commandBuffer.get());
    UVE_ASSERT(nullCommandBuffer != nullptr); // only this device's own CreateCommandBufferUVE() ever produces one
    const std::lock_guard<std::mutex> submissionLock(m_impl->submissionMutex);
    m_impl->lastSubmittedCommands = nullCommandBuffer->GetRecordedCommandsUVE();
}

void NullRenderDeviceUVE::PresentUVE() {
    CollectRetiredTexturesUVE();
    ++m_impl->presentCallCount;
}

bool NullRenderDeviceUVE::IsUsableUVE() const noexcept {
    return true;
}

std::string_view NullRenderDeviceUVE::GetBackendNameUVE() const noexcept {
    return "Null";
}

const std::vector<RecordedCommandUVE>& NullRenderDeviceUVE::GetLastSubmittedCommandsUVE() const noexcept {
    return m_impl->lastSubmittedCommands;
}

std::size_t NullRenderDeviceUVE::GetLiveResourceCountUVE() const noexcept {
    return m_impl->buffers.size() + m_impl->textures.size() + m_impl->samplers.size() +
           m_impl->shaders.size() + m_impl->pipelines.size() + m_impl->computePipelines.size() +
           m_impl->fences.size() + m_impl->queries.size();
}

std::vector<TextureDescUVE> NullRenderDeviceUVE::GetLiveTextureDescsUVE() const {
    std::vector<TextureDescUVE> descriptors;
    descriptors.reserve(m_impl->textures.size());
    for (const auto& [handle, desc] : m_impl->textures) {
        static_cast<void>(handle);
        descriptors.push_back(desc);
    }
    return descriptors;
}

std::vector<SamplerDescUVE> NullRenderDeviceUVE::GetLiveSamplerDescsUVE() const {
    std::vector<SamplerDescUVE> descriptors;
    descriptors.reserve(m_impl->samplers.size());
    for (const auto& [handle, desc] : m_impl->samplers) {
        static_cast<void>(handle);
        descriptors.push_back(desc);
    }
    return descriptors;
}

std::uint64_t NullRenderDeviceUVE::GetTextureCreateAttemptCountUVE() const noexcept {
    return m_impl->textureCreateAttemptCount;
}

std::uint64_t NullRenderDeviceUVE::GetPresentCallCountUVE() const noexcept {
    return m_impl->presentCallCount;
}

} // namespace UVE::Render

namespace UVE::Render {
RenderDeviceCapabilitiesUVE NullRenderDeviceUVE::GetCapabilitiesUVE() const noexcept {
    const auto model = RenderFeatureSupportUVE::Model;
    RenderDeviceCapabilitiesUVE caps{model, model, model, model, model, model, model,
                                     RenderFeatureSupportUVE::Unsupported, model};
    caps.geometryShaders = model;
    caps.timelineSemaphores = model;
    return caps;
}
RenderOperationResultUVE NullRenderDeviceUVE::GetTextureDescUVE(TextureHandleUVE handle,
                                                                TextureDescUVE &output) const {
    const auto found = m_impl->textures.find(handle.value);
    if (found == m_impl->textures.end())
        return RenderOperationResultUVE::InvalidHandle;
    output = found->second;
    return RenderOperationResultUVE::Success;
}
RenderOperationResultUVE NullRenderDeviceUVE::CopyBufferUVE(const BufferCopyDescUVE &copy) {
    const auto source = m_impl->buffers.find(copy.source.value);
    const auto target = m_impl->buffers.find(copy.destination.value);
    if (source == m_impl->buffers.end() || target == m_impl->buffers.end())
        return RenderOperationResultUVE::InvalidHandle;
    if (!IsBufferCopyRangeValidUVE(copy, source->second.bytes.size(), target->second.bytes.size()))
        return RenderOperationResultUVE::InvalidArgument;
    std::copy_n(source->second.bytes.data() + static_cast<std::size_t>(copy.sourceOffset),
                static_cast<std::size_t>(copy.sizeBytes),
                target->second.bytes.data() + static_cast<std::size_t>(copy.destinationOffset));
    m_impl->RecordUVE(copy);
    return RenderOperationResultUVE::Recorded;
}
RenderOperationResultUVE NullRenderDeviceUVE::BlitTextureUVE(const TextureBlitDescUVE &blit) {
    const auto source = m_impl->textures.find(blit.source.value);
    const auto target = m_impl->textures.find(blit.destination.value);
    if (source == m_impl->textures.end() || target == m_impl->textures.end())
        return RenderOperationResultUVE::InvalidHandle;
    if (!IsTextureBlitValidUVE(blit, source->second, target->second))
        return RenderOperationResultUVE::InvalidArgument;
    m_impl->RecordUVE(blit);
    return RenderOperationResultUVE::Recorded;
}
RenderOperationResultUVE NullRenderDeviceUVE::GenerateMipmapsUVE(TextureHandleUVE texture) {
    const auto found = m_impl->textures.find(texture.value);
    if (found == m_impl->textures.end())
        return RenderOperationResultUVE::InvalidHandle;
    const auto &desc = found->second;
    if (desc.mipLevels < 2U ||
        !IsUncompressedColorFormatUVE(desc.format))
        return RenderOperationResultUVE::InvalidArgument;
    m_impl->RecordUVE(GenerateMipsOperationUVE{texture});
    return RenderOperationResultUVE::Recorded;
}
RenderOperationResultUVE NullRenderDeviceUVE::InsertFenceUVE(FenceHandleUVE &output) {
    if (m_impl->nextFence == 0U)
        return RenderOperationResultUVE::SystemError;
    const FenceHandleUVE handle{m_impl->nextFence++};
    m_impl->fences.emplace(handle.value, true); // host model only; no claim about a GPU
    m_impl->RecordUVE(FenceOperationUVE{handle});
    output = handle;
    return RenderOperationResultUVE::Recorded;
}
RenderOperationResultUVE NullRenderDeviceUVE::WaitFenceUVE(FenceHandleUVE handle, std::uint64_t) {
    return m_impl->fences.contains(handle.value) ? RenderOperationResultUVE::Recorded
                                                 : RenderOperationResultUVE::InvalidHandle;
}
RenderOperationResultUVE NullRenderDeviceUVE::DestroyFenceUVE(FenceHandleUVE handle) {
    return m_impl->fences.erase(handle.value) != 0U ? RenderOperationResultUVE::Recorded
                                                    : RenderOperationResultUVE::InvalidHandle;
}
RenderOperationResultUVE NullRenderDeviceUVE::WaitIdleUVE() {
    CollectRetiredTexturesUVE();
    return RenderOperationResultUVE::Recorded;
}
RenderOperationResultUVE NullRenderDeviceUVE::CreateSemaphoreUVE(const SemaphoreDescUVE &desc,
                                                                    SemaphoreHandleUVE &output) {
    const std::uint32_t handleValue = m_impl->nextSemaphore++;
    {
        const std::lock_guard lock(m_impl->semaphoreMutex);
        m_impl->semaphores.emplace(handleValue, ImplUVE::SemaphoreRecordUVE{desc.initialValue});
    }
    output = SemaphoreHandleUVE{handleValue};
    return RenderOperationResultUVE::Success;
}

RenderOperationResultUVE NullRenderDeviceUVE::GetSemaphoreCounterUVE(const SemaphoreHandleUVE semaphore,
                                                                     std::uint64_t &output) const {
    const std::lock_guard lock(m_impl->semaphoreMutex);
    const auto found = m_impl->semaphores.find(semaphore.value);
    if (found == m_impl->semaphores.end()) {
        return RenderOperationResultUVE::InvalidHandle;
    }
    output = found->second.value;
    return RenderOperationResultUVE::Success;
}

RenderOperationResultUVE NullRenderDeviceUVE::SignalSemaphoreUVE(const SemaphoreHandleUVE semaphore,
                                                                 const std::uint64_t value) {
    {
        const std::lock_guard lock(m_impl->semaphoreMutex);
        const auto found = m_impl->semaphores.find(semaphore.value);
        if (found == m_impl->semaphores.end()) {
            return RenderOperationResultUVE::InvalidHandle;
        }
        if (!IsSemaphoreSignalValueValidUVE(found->second.value, value)) {
            UVE_ERROR("NullRenderDeviceUVE: SignalSemaphoreUVE ignored a non-increasing value");
            return RenderOperationResultUVE::InvalidArgument;
        }
        found->second.value = value;
    }
    m_impl->RecordUVE(SignalSemaphoreOperationUVE{semaphore, value});
    m_impl->semaphoreCondition.notify_all();
    return RenderOperationResultUVE::Success;
}

RenderOperationResultUVE NullRenderDeviceUVE::WaitSemaphoreUVE(const SemaphoreHandleUVE semaphore,
                                                               const std::uint64_t value,
                                                               const std::uint64_t timeoutNs) {
    const auto reached = [this, semaphore, value] {
        const auto found = m_impl->semaphores.find(semaphore.value);
        return found != m_impl->semaphores.end() && found->second.value >= value;
    };
    bool satisfied = false;
    {
        std::unique_lock lock(m_impl->semaphoreMutex);
        if (m_impl->semaphores.find(semaphore.value) == m_impl->semaphores.end()) {
            return RenderOperationResultUVE::InvalidHandle;
        }
        satisfied = m_impl->semaphoreCondition.wait_for(lock, std::chrono::nanoseconds(timeoutNs),
                                                        reached);
        if (!satisfied && m_impl->semaphores.find(semaphore.value) == m_impl->semaphores.end()) {
            return RenderOperationResultUVE::InvalidHandle; // destroyed while waiting
        }
    }
    m_impl->RecordUVE(WaitSemaphoreOperationUVE{semaphore, value, timeoutNs, satisfied});
    return satisfied ? RenderOperationResultUVE::Success : RenderOperationResultUVE::TimedOut;
}

RenderOperationResultUVE NullRenderDeviceUVE::DestroySemaphoreUVE(const SemaphoreHandleUVE semaphore) {
    {
        const std::lock_guard lock(m_impl->semaphoreMutex);
        if (m_impl->semaphores.erase(semaphore.value) == 0) {
            UVE_ERROR("NullRenderDeviceUVE: DestroySemaphoreUVE called with an unknown or "
                      "already-destroyed handle ({})",
                      semaphore.value);
            return RenderOperationResultUVE::InvalidHandle;
        }
    }
    m_impl->semaphoreCondition.notify_all();
    return RenderOperationResultUVE::Success;
}

RenderOperationResultUVE NullRenderDeviceUVE::CreateQueryUVE(RenderQueryKindUVE kind,
                                                             QueryHandleUVE &output) {
    if (kind != RenderQueryKindUVE::Timestamp && kind != RenderQueryKindUVE::AnySamplesPassed)
        return RenderOperationResultUVE::InvalidArgument;
    if (m_impl->nextQuery == 0U)
        return RenderOperationResultUVE::SystemError;
    const QueryHandleUVE handle{m_impl->nextQuery++};
    m_impl->queries.emplace(handle.value, ImplUVE::QueryRecordUVE{kind});
    output = handle;
    return RenderOperationResultUVE::Recorded;
}
RenderOperationResultUVE NullRenderDeviceUVE::BeginQueryUVE(QueryHandleUVE handle) {
    const auto found = m_impl->queries.find(handle.value);
    if (found == m_impl->queries.end())
        return RenderOperationResultUVE::InvalidHandle;
    if (found->second.kind != RenderQueryKindUVE::AnySamplesPassed)
        return RenderOperationResultUVE::InvalidArgument;
    if (m_impl->activeOcclusion != 0U)
        return RenderOperationResultUVE::InvalidState;
    m_impl->activeOcclusion = handle.value;
    found->second.issued = false;
    m_impl->RecordUVE(QueryOperationUVE{handle, found->second.kind, true});
    return RenderOperationResultUVE::Recorded;
}
RenderOperationResultUVE NullRenderDeviceUVE::EndQueryUVE(QueryHandleUVE handle) {
    const auto found = m_impl->queries.find(handle.value);
    if (found == m_impl->queries.end())
        return RenderOperationResultUVE::InvalidHandle;
    if (m_impl->activeOcclusion != handle.value)
        return RenderOperationResultUVE::InvalidState;
    m_impl->activeOcclusion = 0U;
    found->second.issued = true;
    m_impl->RecordUVE(QueryOperationUVE{handle, found->second.kind, false});
    return RenderOperationResultUVE::Recorded;
}
RenderOperationResultUVE NullRenderDeviceUVE::WriteTimestampUVE(QueryHandleUVE handle) {
    const auto found = m_impl->queries.find(handle.value);
    if (found == m_impl->queries.end())
        return RenderOperationResultUVE::InvalidHandle;
    if (found->second.kind != RenderQueryKindUVE::Timestamp)
        return RenderOperationResultUVE::InvalidArgument;
    found->second.issued = true;
    m_impl->RecordUVE(QueryOperationUVE{handle, found->second.kind, false});
    return RenderOperationResultUVE::Recorded;
}
RenderOperationResultUVE NullRenderDeviceUVE::GetQueryResultUVE(QueryHandleUVE handle, RenderQueryResultUVE &,
                                                                bool) {
    const auto found = m_impl->queries.find(handle.value);
    if (found == m_impl->queries.end())
        return RenderOperationResultUVE::InvalidHandle;
    if (!found->second.issued || m_impl->activeOcclusion == handle.value)
        return RenderOperationResultUVE::NotReady;
    return RenderOperationResultUVE::Unsupported; // no fabricated timing or visibility
}
RenderOperationResultUVE NullRenderDeviceUVE::DestroyQueryUVE(QueryHandleUVE handle) {
    if (handle.value != 0U && m_impl->activeOcclusion == handle.value)
        return RenderOperationResultUVE::InvalidState;
    return m_impl->queries.erase(handle.value) != 0U ? RenderOperationResultUVE::Recorded
                                                     : RenderOperationResultUVE::InvalidHandle;
}
const std::vector<RecordedDeviceOperationUVE> &
NullRenderDeviceUVE::GetRecordedDeviceOperationsUVE() const noexcept {
    return m_impl->operations;
}
std::uint64_t NullRenderDeviceUVE::GetDroppedDeviceOperationCountUVE() const noexcept {
    return m_impl->droppedOperations;
}
} // namespace UVE::Render
