// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/rhi_null/null_render_device_uve.h"

#include <algorithm>
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
};

NullRenderDeviceUVE::NullRenderDeviceUVE() : m_impl(std::make_unique<ImplUVE>()) {}

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

void NullRenderDeviceUVE::DestroyTextureUVE(TextureHandleUVE texture) {
    if (m_impl->textures.erase(texture.value) == 0) {
        UVE_ERROR("NullRenderDeviceUVE: DestroyTextureUVE called with an unknown or already-destroyed handle ({})",
                   texture.value);
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
    if (!m_impl->shaders.contains(desc.vertexShader.value) || !m_impl->shaders.contains(desc.fragmentShader.value)) {
        UVE_ERROR("NullRenderDeviceUVE: CreatePipelineUVE referenced an unknown vertex or fragment shader handle");
        return kInvalidPipelineHandleUVE;
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
    return m_impl->buffers.size() + m_impl->textures.size() + m_impl->shaders.size() +
           m_impl->pipelines.size() + m_impl->computePipelines.size();
}

std::uint64_t NullRenderDeviceUVE::GetTextureCreateAttemptCountUVE() const noexcept {
    return m_impl->textureCreateAttemptCount;
}

std::uint64_t NullRenderDeviceUVE::GetPresentCallCountUVE() const noexcept {
    return m_impl->presentCallCount;
}

} // namespace UVE::Render
