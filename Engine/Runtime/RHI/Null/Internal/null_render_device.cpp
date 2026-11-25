// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "null_render_device.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <iterator>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bindless_texture_table.h"
#include "null_command_buffer.h"
#include "engine_assert.h"
#include "logging_macros.h"

namespace UVE::Render {

struct NullRenderDevice::Impl {
    // CS3: the Null backend now keeps each buffer's BYTES, not just its descriptor. Without
    // them ReadbackBuffer could only ever hand back zeros, which would make a headless
    // "what did the write leave behind" assertion a lie; with them the null device models the
    // one thing a memory-less backend can honestly model - host-visible buffer contents.
    struct BufferRecord {
        BufferDesc desc;
        std::vector<std::byte> bytes;
    };
    std::unordered_map<std::uint32_t, BufferRecord> buffers;
    std::uint32_t nextBufferHandle = 1;
    std::unordered_map<std::uint32_t, TextureDesc> textures;
    std::uint32_t nextTextureHandle = 1;
    std::unordered_map<std::uint32_t, SamplerDesc> samplers;
    std::uint32_t nextSamplerHandle = 1;
    std::uint64_t textureCreateAttemptCount = 0;
    std::unordered_map<std::uint32_t, ShaderDesc> shaders;
    std::uint32_t nextShaderHandle = 1;
    std::unordered_map<std::uint32_t, PipelineDesc> pipelines;
    std::unordered_map<std::uint32_t, ComputePipelineDesc> computePipelines; // M5a, same handle domain
    std::uint32_t nextPipelineHandle = 1;
    // M4: Submit may be called from any thread (the same contract the Vulkan backend
    // honors for its submission FIFO) — the spy write happens under this lock. Readers of
    // GetLastSubmittedCommands() get a const reference, so they must be externally
    // quiesced (the test-spy pattern: submit, join all threads, then inspect).
    std::mutex submissionMutex;
    std::vector<RecordedCommand> lastSubmittedCommands;
    std::uint64_t presentCallCount = 0;
    std::unordered_map<std::uint32_t, bool> fences;
    std::uint32_t nextFence = 1U;
    struct QueryRecord {
        RenderQueryKind kind;
        bool issued = false;
    };
    // Timeline semaphores: real counters, real blocking waits. `condition` is notified on every
    // signal and on destruction so no waiter can outlive the device.
    struct SemaphoreRecord {
        std::uint64_t value = 0U;
    };
    mutable std::mutex semaphoreMutex;
    std::condition_variable semaphoreCondition;
    std::unordered_map<std::uint32_t, SemaphoreRecord> semaphores;
    std::uint32_t nextSemaphore = 1U;
    std::unordered_map<std::uint32_t, QueryRecord> queries;
    std::uint32_t nextQuery = 1U, activeOcclusion = 0U;
    std::vector<RecordedDeviceOperation> operations;
    std::uint64_t droppedOperations = 0U;
    BindlessTextureTable bindlessTable{};
    void Record(RecordedDeviceOperation operation) {
        if (operations.size() == 256U) {
            operations.erase(operations.begin());
            if (droppedOperations != UINT64_MAX)
                ++droppedOperations;
        }
        operations.push_back(std::move(operation));
    }
};

NullRenderDevice::NullRenderDevice() : m_impl(std::make_unique<Impl>()) {
    m_impl->operations.reserve(256U);
}

NullRenderDevice::~NullRenderDevice() = default;

BufferHandle NullRenderDevice::CreateBuffer(const BufferDesc& desc,
                                                       std::span<const std::byte> initialData) {
    if (!ValidateBufferUpload(desc, initialData)) {
        UVE_ERROR("NullRenderDevice: CreateBuffer initial data exceeds buffer size");
        return kInvalidBufferHandle;
    }
    if (!IsBufferDescValid(desc)) {
        UVE_ERROR("NullRenderDevice: CreateBuffer received an unknown buffer usage, memory or "
                  "update-frequency value");
        return kInvalidBufferHandle;
    }
    const std::uint32_t handleValue = m_impl->nextBufferHandle++;
    // Zero-filled to the declared size, then the optional initial upload on top: the same
    // observable state a real backend leaves behind for a freshly created buffer.
    Impl::BufferRecord record{desc, std::vector<std::byte>(desc.sizeBytes, std::byte{0})};
    if (!initialData.empty()) {
        std::copy(initialData.begin(), initialData.end(), record.bytes.begin());
    }
    m_impl->buffers.emplace(handleValue, std::move(record));
    return BufferHandle{handleValue};
}

void NullRenderDevice::DestroyBuffer(BufferHandle buffer) {
    if (m_impl->buffers.erase(buffer.value) == 0) {
        UVE_ERROR("NullRenderDevice: DestroyBuffer called with an unknown or already-destroyed handle ({})",
                   buffer.value);
    }
}

bool NullRenderDevice::UpdateBuffer(BufferHandle buffer, std::span<const std::byte> data,
                                           std::uint64_t offsetBytes) {
    const auto iterator = m_impl->buffers.find(buffer.value);
    if (iterator == m_impl->buffers.end()) {
        UVE_ERROR("NullRenderDevice: UpdateBuffer called with an unknown handle ({})", buffer.value);
        return false;
    }
    if (!ValidateBufferUpdate(iterator->second.desc.sizeBytes, data.size(), offsetBytes)) {
        UVE_ERROR("NullRenderDevice: UpdateBuffer write of {} bytes at offset {} exceeds buffer size {}",
                   data.size(), offsetBytes, iterator->second.desc.sizeBytes);
        return false;
    }
    std::copy(data.begin(), data.end(),
              iterator->second.bytes.begin() + static_cast<std::ptrdiff_t>(offsetBytes));
    return true;
}

bool NullRenderDevice::ReadbackBuffer(BufferHandle buffer, std::span<std::byte> outData,
                                             std::uint64_t offsetBytes) {
    const auto iterator = m_impl->buffers.find(buffer.value);
    if (iterator == m_impl->buffers.end()) {
        UVE_ERROR("NullRenderDevice: ReadbackBuffer called with an unknown handle ({})", buffer.value);
        return false;
    }
    // Same range rule as the write direction - a read that runs off the end is the same
    // authoring bug as a write that does.
    if (!ValidateBufferUpdate(iterator->second.desc.sizeBytes, outData.size(), offsetBytes)) {
        UVE_ERROR("NullRenderDevice: ReadbackBuffer read of {} bytes at offset {} exceeds buffer size {}",
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

TextureHandle NullRenderDevice::CreateTexture(const TextureDesc& desc,
                                                         std::span<const std::byte> initialData) {
    ++m_impl->textureCreateAttemptCount;
    if (!ValidateTextureUpload(desc, initialData)) {
        UVE_ERROR("NullRenderDevice: CreateTexture received an invalid descriptor or initial upload");
        return kInvalidTextureHandle;
    }
    static_cast<void>(initialData); // NullRenderDevice performs no real upload, bookkeeping only.
    const std::uint32_t handleValue = m_impl->nextTextureHandle++;
    m_impl->textures.emplace(handleValue, desc);
    return TextureHandle{handleValue};
}

bool NullRenderDevice::SupportsTextureFormat(const TextureFormat format,
                                                        const TextureColorSpace colorSpace) const noexcept {
    const bool formatKnown = GetTextureFormatBlockInfo(format).bytes != 0U;
    switch (colorSpace) {
        case TextureColorSpace::Linear:
            return formatKnown && !IsTextureFormatCompressed(format);
        case TextureColorSpace::Srgb:
            return formatKnown && IsTextureFormatSrgbCapable(format) &&
                   !IsTextureFormatCompressed(format);
    }
    return false;
}

void NullRenderDevice::DestroyTexture(TextureHandle texture) {
    m_impl->bindlessTable.UnregisterTexture(texture);
    if (m_impl->textures.erase(texture.value) == 0) {
        UVE_ERROR("NullRenderDevice: DestroyTexture called with an unknown or already-destroyed handle ({})",
                   texture.value);
    }
}

std::uint32_t NullRenderDevice::RegisterBindlessTexture(TextureHandle texture) {
    if (m_impl->textures.find(texture.value) == m_impl->textures.end()) {
        return kInvalidBindlessTextureIndex;
    }
    return m_impl->bindlessTable.Register(texture);
}

void NullRenderDevice::UnregisterBindlessTexture(std::uint32_t bindlessIndex) {
    m_impl->bindlessTable.UnregisterSlot(bindlessIndex);
}

SamplerHandle NullRenderDevice::CreateSampler(const SamplerDesc& desc) {
    if (!IsSamplerDescValid(desc)) {
        UVE_ERROR("NullRenderDevice: CreateSampler received an invalid sampler descriptor");
        return kInvalidSamplerHandle;
    }
    const std::uint32_t handleValue = m_impl->nextSamplerHandle++;
    m_impl->samplers.emplace(handleValue, desc);
    return SamplerHandle{handleValue};
}

void NullRenderDevice::DestroySampler(SamplerHandle sampler) {
    if (m_impl->samplers.erase(sampler.value) == 0) {
        UVE_ERROR("NullRenderDevice: DestroySampler called with an unknown or already-destroyed handle ({})",
                   sampler.value);
    }
}

ShaderHandle NullRenderDevice::CreateShader(const ShaderDesc& desc, std::string* outInfoLog) {
    static_cast<void>(outInfoLog); // NullRenderDevice never compiles anything real - nothing to log.
    if (!IsShaderStageValid(desc.stage)) {
        UVE_ERROR("NullRenderDevice: CreateShader received an unknown shader stage");
        return kInvalidShaderHandle;
    }
    const std::uint32_t handleValue = m_impl->nextShaderHandle++;
    m_impl->shaders.emplace(handleValue, desc);
    return ShaderHandle{handleValue};
}

void NullRenderDevice::DestroyShader(ShaderHandle shader) {
    if (m_impl->shaders.erase(shader.value) == 0) {
        UVE_ERROR("NullRenderDevice: DestroyShader called with an unknown or already-destroyed handle ({})",
                   shader.value);
    }
}

PipelineHandle NullRenderDevice::CreatePipeline(const PipelineDesc& desc, std::string* outInfoLog) {
    static_cast<void>(outInfoLog); // NullRenderDevice never links anything real - nothing to log.
    if (!IsVertexLayoutValid(desc.vertexLayout)) {
        UVE_ERROR("NullRenderDevice: CreatePipeline received an unknown vertex attribute format");
        return kInvalidPipelineHandle;
    }
    if (!IsPipelineBlendModeValid(desc.blendMode)) {
        UVE_ERROR("NullRenderDevice: CreatePipeline received an unknown blend mode");
        return kInvalidPipelineHandle;
    }
    if (!IsPrimitiveTopologyValid(desc.topology)) {
        UVE_ERROR("NullRenderDevice: CreatePipeline received an unknown primitive topology");
        return kInvalidPipelineHandle;
    }
    if (!IsCullModeValid(desc.cullMode)) {
        UVE_ERROR("NullRenderDevice: CreatePipeline received an unknown cull mode");
        return kInvalidPipelineHandle;
    }
    if (!IsFrontFaceValid(desc.frontFace)) {
        UVE_ERROR("NullRenderDevice: CreatePipeline received an unknown front-face winding");
        return kInvalidPipelineHandle;
    }
    if (!IsFillModeValid(desc.fillMode)) {
        UVE_ERROR("NullRenderDevice: CreatePipeline received an unknown fill mode");
        return kInvalidPipelineHandle;
    }
    if (!IsDepthCompareValid(desc.depthCompare)) {
        UVE_ERROR("NullRenderDevice: CreatePipeline received an unknown depth comparison");
        return kInvalidPipelineHandle;
    }
    if (!IsStencilStateValid(desc.stencil)) {
        UVE_ERROR("NullRenderDevice: CreatePipeline received an unknown stencil state");
        return kInvalidPipelineHandle;
    }
    if (!m_impl->shaders.contains(desc.vertexShader.value) || !m_impl->shaders.contains(desc.fragmentShader.value)) {
        UVE_ERROR("NullRenderDevice: CreatePipeline referenced an unknown vertex or fragment shader handle");
        return kInvalidPipelineHandle;
    }
    const auto rejectStage = [outInfoLog](const char* message) {
        UVE_ERROR("NullRenderDevice: CreatePipeline {}", message);
        if (outInfoLog != nullptr) *outInfoLog = message;
        return kInvalidPipelineHandle;
    };
    if (m_impl->shaders.at(desc.vertexShader.value).stage != ShaderStage::Vertex ||
        m_impl->shaders.at(desc.fragmentShader.value).stage != ShaderStage::Fragment) {
        return rejectStage("vertex/fragment shader handles are bound to the wrong stages");
    }
    if (desc.geometryShader != kInvalidShaderHandle) {
        const auto geometry = m_impl->shaders.find(desc.geometryShader.value);
        if (geometry == m_impl->shaders.end()) {
            return rejectStage("geometry shader handle does not reference a live shader");
        }
        if (geometry->second.stage != ShaderStage::Geometry) {
            return rejectStage("geometry shader slot requires a Geometry-stage shader");
        }
    }
    const std::uint32_t handleValue = m_impl->nextPipelineHandle++;
    m_impl->pipelines.emplace(handleValue, desc);
    return PipelineHandle{handleValue};
}

PipelineHandle NullRenderDevice::CreateComputePipeline(const ComputePipelineDesc& desc,
                                                                 std::string* outInfoLog) {
    static_cast<void>(outInfoLog); // NullRenderDevice never links anything real - nothing to log.
    const auto shader = m_impl->shaders.find(desc.computeShader.value);
    if (shader == m_impl->shaders.end()) {
        UVE_ERROR("NullRenderDevice: CreateComputePipeline referenced an unknown shader handle");
        return kInvalidPipelineHandle;
    }
    if (shader->second.stage != ShaderStage::Compute) {
        UVE_ERROR("NullRenderDevice: CreateComputePipeline requires a Compute-stage shader");
        return kInvalidPipelineHandle;
    }
    const std::uint32_t handleValue = m_impl->nextPipelineHandle++;
    m_impl->computePipelines.emplace(handleValue, desc);
    return PipelineHandle{handleValue};
}

void NullRenderDevice::DestroyPipeline(PipelineHandle pipeline) {
    if (m_impl->pipelines.erase(pipeline.value) == 0 &&
        m_impl->computePipelines.erase(pipeline.value) == 0) {
        UVE_ERROR("NullRenderDevice: DestroyPipeline called with an unknown or already-destroyed handle ({})",
                   pipeline.value);
    }
}

std::vector<UniformReflection> NullRenderDevice::GetPipelineUniforms(PipelineHandle pipeline) const {
    static_cast<void>(pipeline); // NullRenderDevice never really links anything, so nothing to reflect.
    return {};
}

bool NullRenderDevice::GetPipelineBinary(PipelineHandle pipeline, std::vector<std::byte>& outBinary,
                                                std::uint32_t& outFormat) const {
    static_cast<void>(pipeline);
    static_cast<void>(outBinary);
    static_cast<void>(outFormat);
    return false; // NullRenderDevice never compiles anything real, so it has no binary to give.
}

PipelineHandle NullRenderDevice::CreatePipelineFromBinary(std::span<const std::byte> binary,
                                                                    std::uint32_t format,
                                                                    const PipelineBinaryDesc& desc) {
    if (!IsVertexLayoutValid(desc.vertexLayout)) {
        UVE_ERROR("NullRenderDevice: CreatePipelineFromBinary received an unknown vertex attribute format");
        return kInvalidPipelineHandle;
    }
    if (!IsPipelineBlendModeValid(desc.blendMode)) {
        UVE_ERROR("NullRenderDevice: CreatePipelineFromBinary received an unknown blend mode");
        return kInvalidPipelineHandle;
    }
    if (!IsPrimitiveTopologyValid(desc.topology)) {
        UVE_ERROR("NullRenderDevice: CreatePipelineFromBinary received an unknown primitive topology");
        return kInvalidPipelineHandle;
    }
    if (!IsCullModeValid(desc.cullMode)) {
        UVE_ERROR("NullRenderDevice: CreatePipelineFromBinary received an unknown cull mode");
        return kInvalidPipelineHandle;
    }
    if (!IsFrontFaceValid(desc.frontFace)) {
        UVE_ERROR("NullRenderDevice: CreatePipelineFromBinary received an unknown front-face winding");
        return kInvalidPipelineHandle;
    }
    if (!IsFillModeValid(desc.fillMode)) {
        UVE_ERROR("NullRenderDevice: CreatePipelineFromBinary received an unknown fill mode");
        return kInvalidPipelineHandle;
    }
    if (!IsDepthCompareValid(desc.depthCompare)) {
        UVE_ERROR("NullRenderDevice: CreatePipelineFromBinary received an unknown depth comparison");
        return kInvalidPipelineHandle;
    }
    if (!IsStencilStateValid(desc.stencil)) {
        UVE_ERROR("NullRenderDevice: CreatePipelineFromBinary received an unknown stencil state");
        return kInvalidPipelineHandle;
    }
    static_cast<void>(binary); // NullRenderDevice never inspects binary contents.
    static_cast<void>(format);
    PipelineDesc bookkeepingDesc;
    bookkeepingDesc.vertexShader = kInvalidShaderHandle; // no shader was ever compiled for this pipeline
    bookkeepingDesc.fragmentShader = kInvalidShaderHandle;
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
    return PipelineHandle{handleValue};
}

std::unique_ptr<ICommandBuffer> NullRenderDevice::CreateCommandBuffer() {
    // Recorded query brackets validate against this device's query table (owner-thread
    // recording, like GL); the lookup reads the table without mutating it.
    Impl* const impl = m_impl.get();
    return std::make_unique<NullCommandBuffer>(
        [impl](const QueryHandle query) -> std::optional<RenderQueryKind> {
            const auto found = impl->queries.find(query.value);
            if (found == impl->queries.end())
                return std::nullopt;
            return found->second.kind;
        });
}

void NullRenderDevice::Submit(std::unique_ptr<ICommandBuffer> commandBuffer) {
    UVE_ASSERT(commandBuffer != nullptr);
    auto* const nullCommandBuffer = dynamic_cast<NullCommandBuffer*>(commandBuffer.get());
    UVE_ASSERT(nullCommandBuffer != nullptr); // only this device's own CreateCommandBuffer() ever produces one
    const std::lock_guard<std::mutex> submissionLock(m_impl->submissionMutex);
    m_impl->lastSubmittedCommands = nullCommandBuffer->GetRecordedCommands();
}

RenderOperationResult NullRenderDevice::SubmitWithSync(std::unique_ptr<ICommandBuffer> commandBuffer,
                                                                const SubmitDesc &desc) {
    if (desc.waits.empty() && desc.signals.empty()) {
        Submit(std::move(commandBuffer));
        return RenderOperationResult::Recorded;
    }
    SubmitOperation record;
    {
        const std::lock_guard lock(m_impl->semaphoreMutex);
        std::vector<std::uint32_t> seen;
        const auto unique = [&seen](const std::uint32_t handleValue) {
            if (std::find(seen.begin(), seen.end(), handleValue) != seen.end())
                return false;
            seen.push_back(handleValue);
            return true;
        };
        for (const SemaphoreWait &wait : desc.waits) {
            if (m_impl->semaphores.find(wait.semaphore.value) == m_impl->semaphores.end())
                return RenderOperationResult::InvalidHandle;
            if (!unique(wait.semaphore.value))
                return RenderOperationResult::InvalidArgument;
        }
        for (const SemaphoreSignal &signal : desc.signals) {
            const auto found = m_impl->semaphores.find(signal.semaphore.value);
            if (found == m_impl->semaphores.end())
                return RenderOperationResult::InvalidHandle;
            if (!unique(signal.semaphore.value) || !IsSemaphoreSignalValueValid(found->second.value, signal.value))
                return RenderOperationResult::InvalidArgument;
        }
        record.waits.assign(desc.waits.begin(), desc.waits.end());
        record.signals.assign(desc.signals.begin(), desc.signals.end());
    }
    m_impl->Record(std::move(record));
    Submit(std::move(commandBuffer));
    return RenderOperationResult::Recorded;
}

void NullRenderDevice::Present() {
    CollectRetiredTextures();
    ++m_impl->presentCallCount;
}

bool NullRenderDevice::IsUsable() const noexcept {
    return true;
}

std::string_view NullRenderDevice::GetBackendName() const noexcept {
    return "Null";
}

const std::vector<RecordedCommand>& NullRenderDevice::GetLastSubmittedCommands() const noexcept {
    return m_impl->lastSubmittedCommands;
}

std::size_t NullRenderDevice::GetLiveResourceCount() const noexcept {
    return m_impl->buffers.size() + m_impl->textures.size() + m_impl->samplers.size() +
           m_impl->shaders.size() + m_impl->pipelines.size() + m_impl->computePipelines.size() +
           m_impl->fences.size() + m_impl->queries.size();
}

std::vector<TextureDesc> NullRenderDevice::GetLiveTextureDescs() const {
    std::vector<TextureDesc> descriptors;
    descriptors.reserve(m_impl->textures.size());
    for (const auto& [handle, desc] : m_impl->textures) {
        static_cast<void>(handle);
        descriptors.push_back(desc);
    }
    return descriptors;
}

std::vector<SamplerDesc> NullRenderDevice::GetLiveSamplerDescs() const {
    std::vector<SamplerDesc> descriptors;
    descriptors.reserve(m_impl->samplers.size());
    for (const auto& [handle, desc] : m_impl->samplers) {
        static_cast<void>(handle);
        descriptors.push_back(desc);
    }
    return descriptors;
}

std::uint64_t NullRenderDevice::GetTextureCreateAttemptCount() const noexcept {
    return m_impl->textureCreateAttemptCount;
}

std::uint64_t NullRenderDevice::GetPresentCallCount() const noexcept {
    return m_impl->presentCallCount;
}

} // namespace UVE::Render

namespace UVE::Render {
RenderDeviceCapabilities NullRenderDevice::GetCapabilities() const noexcept {
    const auto model = RenderFeatureSupport::Model;
    RenderDeviceCapabilities caps{model, model, model, model, model, model, model,
                                     RenderFeatureSupport::Unsupported, model};
    caps.geometryShaders = model;
    caps.timelineSemaphores = model;
    caps.bindlessTextures = model;
    caps.maxBindlessTextures = kDefaultMaxBindlessTextures;
    caps.storageBuffers = model;
    caps.halfFloatColorTargets = model;
    return caps;
}
RenderOperationResult NullRenderDevice::GetTextureDesc(TextureHandle handle,
                                                                TextureDesc &output) const {
    const auto found = m_impl->textures.find(handle.value);
    if (found == m_impl->textures.end())
        return RenderOperationResult::InvalidHandle;
    output = found->second;
    return RenderOperationResult::Success;
}
RenderOperationResult NullRenderDevice::GetBufferDesc(BufferHandle handle,
                                                               BufferDesc &output) const {
    const auto found = m_impl->buffers.find(handle.value);
    if (found == m_impl->buffers.end())
        return RenderOperationResult::InvalidHandle;
    output = found->second.desc;
    return RenderOperationResult::Success;
}
RenderOperationResult NullRenderDevice::CopyBuffer(const BufferCopyDesc &copy) {
    const auto source = m_impl->buffers.find(copy.source.value);
    const auto target = m_impl->buffers.find(copy.destination.value);
    if (source == m_impl->buffers.end() || target == m_impl->buffers.end())
        return RenderOperationResult::InvalidHandle;
    if (!IsBufferCopyRangeValid(copy, source->second.bytes.size(), target->second.bytes.size()))
        return RenderOperationResult::InvalidArgument;
    std::copy_n(source->second.bytes.data() + static_cast<std::size_t>(copy.sourceOffset),
                static_cast<std::size_t>(copy.sizeBytes),
                target->second.bytes.data() + static_cast<std::size_t>(copy.destinationOffset));
    m_impl->Record(copy);
    return RenderOperationResult::Recorded;
}
RenderOperationResult NullRenderDevice::BlitTexture(const TextureBlitDesc &blit) {
    const auto source = m_impl->textures.find(blit.source.value);
    const auto target = m_impl->textures.find(blit.destination.value);
    if (source == m_impl->textures.end() || target == m_impl->textures.end())
        return RenderOperationResult::InvalidHandle;
    if (!IsTextureBlitValid(blit, source->second, target->second))
        return RenderOperationResult::InvalidArgument;
    m_impl->Record(blit);
    return RenderOperationResult::Recorded;
}
RenderOperationResult NullRenderDevice::GenerateMipmaps(TextureHandle texture) {
    const auto found = m_impl->textures.find(texture.value);
    if (found == m_impl->textures.end())
        return RenderOperationResult::InvalidHandle;
    const auto &desc = found->second;
    if (!CanGenerateMipmaps(desc))
        return RenderOperationResult::InvalidArgument;
    m_impl->Record(GenerateMipsOperation{texture});
    return RenderOperationResult::Recorded;
}
RenderOperationResult NullRenderDevice::InsertFence(FenceHandle &output) {
    if (m_impl->nextFence == 0U)
        return RenderOperationResult::SystemError;
    const FenceHandle handle{m_impl->nextFence++};
    m_impl->fences.emplace(handle.value, true); // host model only; no claim about a GPU
    m_impl->Record(FenceOperation{handle});
    output = handle;
    return RenderOperationResult::Recorded;
}
RenderOperationResult NullRenderDevice::WaitFence(FenceHandle handle, std::uint64_t) {
    return m_impl->fences.contains(handle.value) ? RenderOperationResult::Recorded
                                                 : RenderOperationResult::InvalidHandle;
}
RenderOperationResult NullRenderDevice::DestroyFence(FenceHandle handle) {
    return m_impl->fences.erase(handle.value) != 0U ? RenderOperationResult::Recorded
                                                    : RenderOperationResult::InvalidHandle;
}
RenderOperationResult NullRenderDevice::WaitIdle() {
    CollectRetiredTextures();
    return RenderOperationResult::Recorded;
}
RenderOperationResult NullRenderDevice::CreateSemaphore(const SemaphoreDesc &desc,
                                                                    SemaphoreHandle &output) {
    const std::uint32_t handleValue = m_impl->nextSemaphore++;
    {
        const std::lock_guard lock(m_impl->semaphoreMutex);
        m_impl->semaphores.emplace(handleValue, Impl::SemaphoreRecord{desc.initialValue});
    }
    output = SemaphoreHandle{handleValue};
    return RenderOperationResult::Success;
}

RenderOperationResult NullRenderDevice::GetSemaphoreCounter(const SemaphoreHandle semaphore,
                                                                     std::uint64_t &output) const {
    const std::lock_guard lock(m_impl->semaphoreMutex);
    const auto found = m_impl->semaphores.find(semaphore.value);
    if (found == m_impl->semaphores.end()) {
        return RenderOperationResult::InvalidHandle;
    }
    output = found->second.value;
    return RenderOperationResult::Success;
}

RenderOperationResult NullRenderDevice::SignalSemaphore(const SemaphoreHandle semaphore,
                                                                 const std::uint64_t value) {
    {
        const std::lock_guard lock(m_impl->semaphoreMutex);
        const auto found = m_impl->semaphores.find(semaphore.value);
        if (found == m_impl->semaphores.end()) {
            return RenderOperationResult::InvalidHandle;
        }
        if (!IsSemaphoreSignalValueValid(found->second.value, value)) {
            UVE_ERROR("NullRenderDevice: SignalSemaphore ignored a non-increasing value");
            return RenderOperationResult::InvalidArgument;
        }
        found->second.value = value;
    }
    m_impl->Record(SignalSemaphoreOperation{semaphore, value});
    m_impl->semaphoreCondition.notify_all();
    return RenderOperationResult::Success;
}

RenderOperationResult NullRenderDevice::WaitSemaphore(const SemaphoreHandle semaphore,
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
            return RenderOperationResult::InvalidHandle;
        }
        satisfied = m_impl->semaphoreCondition.wait_for(lock, std::chrono::nanoseconds(timeoutNs),
                                                        reached);
        if (!satisfied && m_impl->semaphores.find(semaphore.value) == m_impl->semaphores.end()) {
            return RenderOperationResult::InvalidHandle; // destroyed while waiting
        }
    }
    m_impl->Record(WaitSemaphoreOperation{semaphore, value, timeoutNs, satisfied});
    return satisfied ? RenderOperationResult::Success : RenderOperationResult::TimedOut;
}

RenderOperationResult NullRenderDevice::DestroySemaphore(const SemaphoreHandle semaphore) {
    {
        const std::lock_guard lock(m_impl->semaphoreMutex);
        if (m_impl->semaphores.erase(semaphore.value) == 0) {
            UVE_ERROR("NullRenderDevice: DestroySemaphore called with an unknown or "
                      "already-destroyed handle ({})",
                      semaphore.value);
            return RenderOperationResult::InvalidHandle;
        }
    }
    m_impl->semaphoreCondition.notify_all();
    return RenderOperationResult::Success;
}

RenderOperationResult NullRenderDevice::CreateQuery(RenderQueryKind kind,
                                                             QueryHandle &output) {
    if (kind != RenderQueryKind::Timestamp && kind != RenderQueryKind::AnySamplesPassed)
        return RenderOperationResult::InvalidArgument;
    if (m_impl->nextQuery == 0U)
        return RenderOperationResult::SystemError;
    const QueryHandle handle{m_impl->nextQuery++};
    m_impl->queries.emplace(handle.value, Impl::QueryRecord{kind});
    output = handle;
    return RenderOperationResult::Recorded;
}
RenderOperationResult NullRenderDevice::BeginQuery(QueryHandle handle) {
    const auto found = m_impl->queries.find(handle.value);
    if (found == m_impl->queries.end())
        return RenderOperationResult::InvalidHandle;
    if (found->second.kind != RenderQueryKind::AnySamplesPassed)
        return RenderOperationResult::InvalidArgument;
    if (m_impl->activeOcclusion != 0U)
        return RenderOperationResult::InvalidState;
    m_impl->activeOcclusion = handle.value;
    found->second.issued = false;
    m_impl->Record(QueryOperation{handle, found->second.kind, true});
    return RenderOperationResult::Recorded;
}
RenderOperationResult NullRenderDevice::EndQuery(QueryHandle handle) {
    const auto found = m_impl->queries.find(handle.value);
    if (found == m_impl->queries.end())
        return RenderOperationResult::InvalidHandle;
    if (m_impl->activeOcclusion != handle.value)
        return RenderOperationResult::InvalidState;
    m_impl->activeOcclusion = 0U;
    found->second.issued = true;
    m_impl->Record(QueryOperation{handle, found->second.kind, false});
    return RenderOperationResult::Recorded;
}
RenderOperationResult NullRenderDevice::WriteTimestamp(QueryHandle handle) {
    const auto found = m_impl->queries.find(handle.value);
    if (found == m_impl->queries.end())
        return RenderOperationResult::InvalidHandle;
    if (found->second.kind != RenderQueryKind::Timestamp)
        return RenderOperationResult::InvalidArgument;
    found->second.issued = true;
    m_impl->Record(QueryOperation{handle, found->second.kind, false});
    return RenderOperationResult::Recorded;
}
RenderOperationResult NullRenderDevice::GetQueryResult(QueryHandle handle, RenderQueryResult &,
                                                                bool) {
    const auto found = m_impl->queries.find(handle.value);
    if (found == m_impl->queries.end())
        return RenderOperationResult::InvalidHandle;
    if (!found->second.issued || m_impl->activeOcclusion == handle.value)
        return RenderOperationResult::NotReady;
    return RenderOperationResult::Unsupported; // no fabricated timing or visibility
}
RenderOperationResult NullRenderDevice::DestroyQuery(QueryHandle handle) {
    if (handle.value != 0U && m_impl->activeOcclusion == handle.value)
        return RenderOperationResult::InvalidState;
    return m_impl->queries.erase(handle.value) != 0U ? RenderOperationResult::Recorded
                                                     : RenderOperationResult::InvalidHandle;
}
const std::vector<RecordedDeviceOperation> &
NullRenderDevice::GetRecordedDeviceOperations() const noexcept {
    return m_impl->operations;
}
std::uint64_t NullRenderDevice::GetDroppedDeviceOperationCount() const noexcept {
    return m_impl->droppedOperations;
}
} // namespace UVE::Render
