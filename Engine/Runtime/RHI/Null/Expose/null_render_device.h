// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "i_render_device.h"
#include "recorded_command.h"

namespace UVE::Render {

/// NullRenderDevice is the only IRenderDevice backend this sandbox can build and test: it
/// performs zero real GPU work — there is no display server, GPU device object, or graphics SDK
/// available here (confirmed and documented in docs/CODING_STANDARDS.md) — and instead validates
/// and bookkeeps every call, handing out a NullCommandBuffer "spy" (engine/render/src/,
/// module-private) that records the exact sequence of RHI calls a real backend would have
/// received. Existing solely so the rest of the rendering pipeline (CameraSystem,
/// MeshRenderer, Renderer3D — later increments) can be built and unit-tested against a real
/// IRenderDevice& today; a genuine Vulkan/Metal/D3D12 backend is future work once this
/// environment has the SDK headers, GPU, and windowing it currently lacks.
/// Thread-safety: not thread-safe. Every method is intended to be called only from the main
/// engine/render thread, matching RenderSystem's own single-threaded frame contract.
class NullRenderDevice final : public IRenderDevice {
public:
    NullRenderDevice();
    ~NullRenderDevice() override;
    [[nodiscard]] RenderDeviceCapabilities GetCapabilities() const noexcept override;
    [[nodiscard]] RenderOperationResult GetTextureDesc(TextureHandle,
                                                             TextureDesc &) const override;
    [[nodiscard]] RenderOperationResult GetBufferDesc(BufferHandle,
                                                            BufferDesc &) const override;
    [[nodiscard]] RenderOperationResult CopyBuffer(const BufferCopyDesc &) override;
    [[nodiscard]] RenderOperationResult BlitTexture(const TextureBlitDesc &) override;
    [[nodiscard]] RenderOperationResult GenerateMipmaps(TextureHandle) override;
    [[nodiscard]] RenderOperationResult InsertFence(FenceHandle &) override;
    [[nodiscard]] RenderOperationResult WaitFence(FenceHandle, std::uint64_t) override;
    [[nodiscard]] RenderOperationResult DestroyFence(FenceHandle) override;
    [[nodiscard]] RenderOperationResult WaitIdle() override;
    /// Timeline semaphores are the one part of the Null model that is genuinely concurrent: a
    /// wait blocks, so the thread that signals is by definition a different one. The semaphore
    /// table therefore carries its own mutex and condition variable instead of relying on the
    /// device's single-threaded contract.
    [[nodiscard]] RenderOperationResult CreateSemaphore(const SemaphoreDesc &,
                                                              SemaphoreHandle &) override;
    [[nodiscard]] RenderOperationResult GetSemaphoreCounter(SemaphoreHandle,
                                                                  std::uint64_t &) const override;
    [[nodiscard]] RenderOperationResult SignalSemaphore(SemaphoreHandle, std::uint64_t) override;
    [[nodiscard]] RenderOperationResult WaitSemaphore(SemaphoreHandle, std::uint64_t,
                                                            std::uint64_t) override;
    [[nodiscard]] RenderOperationResult DestroySemaphore(SemaphoreHandle) override;
    [[nodiscard]] RenderOperationResult CreateQuery(RenderQueryKind, QueryHandle &) override;
    [[nodiscard]] RenderOperationResult BeginQuery(QueryHandle) override;
    [[nodiscard]] RenderOperationResult EndQuery(QueryHandle) override;
    [[nodiscard]] RenderOperationResult WriteTimestamp(QueryHandle) override;
    [[nodiscard]] RenderOperationResult GetQueryResult(QueryHandle, RenderQueryResult &,
                                                             bool = false) override;
    [[nodiscard]] RenderOperationResult DestroyQuery(QueryHandle) override;
    /// Bounded diagnostic history of the last 256 device operations, distinct from submitted
    /// command lists. Model queries deliberately expose no fabricated GPU result.
    [[nodiscard]] const std::vector<RecordedDeviceOperation> &
    GetRecordedDeviceOperations() const noexcept;
    [[nodiscard]] std::uint64_t GetDroppedDeviceOperationCount() const noexcept;

    NullRenderDevice(const NullRenderDevice&) = delete;
    NullRenderDevice& operator=(const NullRenderDevice&) = delete;

    [[nodiscard]] BufferHandle CreateBuffer(const BufferDesc& desc,
                                                   std::span<const std::byte> initialData = {}) override;
    void DestroyBuffer(BufferHandle buffer) override;
    [[nodiscard]] bool UpdateBuffer(BufferHandle buffer, std::span<const std::byte> data,
                                        std::uint64_t offsetBytes = 0) override;
    [[nodiscard]] bool ReadbackBuffer(BufferHandle buffer, std::span<std::byte> outData,
                                          std::uint64_t offsetBytes = 0) override;

    [[nodiscard]] TextureHandle CreateTexture(const TextureDesc& desc,
                                                     std::span<const std::byte> initialData = {}) override;
    [[nodiscard]] bool SupportsTextureFormat(
        TextureFormat format, TextureColorSpace colorSpace = TextureColorSpace::Linear) const noexcept override;
    void DestroyTexture(TextureHandle texture) override;
    [[nodiscard]] std::uint32_t RegisterBindlessTexture(TextureHandle texture) override;
    void UnregisterBindlessTexture(std::uint32_t bindlessIndex) override;
    [[nodiscard]] SamplerHandle CreateSampler(const SamplerDesc& desc) override;
    void DestroySampler(SamplerHandle sampler) override;

    [[nodiscard]] ShaderHandle CreateShader(const ShaderDesc& desc, std::string* outInfoLog = nullptr) override;
    void DestroyShader(ShaderHandle shader) override;

    [[nodiscard]] PipelineHandle CreatePipeline(const PipelineDesc& desc,
                                                       std::string* outInfoLog = nullptr) override;
    [[nodiscard]] PipelineHandle CreateComputePipeline(const ComputePipelineDesc& desc,
                                                              std::string* outInfoLog = nullptr) override;
    void DestroyPipeline(PipelineHandle pipeline) override;

    [[nodiscard]] std::vector<UniformReflection> GetPipelineUniforms(PipelineHandle pipeline) const override;
    [[nodiscard]] bool GetPipelineBinary(PipelineHandle pipeline, std::vector<std::byte>& outBinary,
                                             std::uint32_t& outFormat) const override;
    [[nodiscard]] PipelineHandle CreatePipelineFromBinary(std::span<const std::byte> binary,
                                                                 std::uint32_t format,
                                                                 const PipelineBinaryDesc& desc) override;

    [[nodiscard]] std::unique_ptr<ICommandBuffer> CreateCommandBuffer() override;
    void Submit(std::unique_ptr<ICommandBuffer> commandBuffer) override;
    /// Foundation WP4: validates the desc against this device's semaphore table, records a
    /// SubmitOperation with the exact waits/signals, then submits like Submit. No counter
    /// moves: a recorded signal is evidence of a request, never of GPU progress.
    [[nodiscard]] RenderOperationResult SubmitWithSync(std::unique_ptr<ICommandBuffer> commandBuffer,
                                                            const SubmitDesc &desc) override;
    void Present() override;

    [[nodiscard]] bool IsUsable() const noexcept override;
    [[nodiscard]] std::string_view GetBackendName() const noexcept override;

    /// Test-only hook (not part of IRenderDevice): the command list from the most recently
    /// submitted command buffer, in recorded order. Empty until the first Submit() call.
    [[nodiscard]] const std::vector<RecordedCommand>& GetLastSubmittedCommands() const noexcept;

    /// Test-only hook: how many buffer/texture/shader/pipeline resources are currently alive
    /// (created but not yet destroyed) — lets tests confirm cleanup without a GPU to inspect.
    [[nodiscard]] std::size_t GetLiveResourceCount() const noexcept;

    /// Test-only hook: snapshots descriptors for live textures so renderer integration tests can
    /// verify asset metadata reached the RHI without exposing backend storage through the RHI API.
    [[nodiscard]] std::vector<TextureDesc> GetLiveTextureDescs() const;

    /// Test-only hook: snapshots descriptors for live samplers (the Tier 2.2 analogue of
    /// GetLiveTextureDescs above).
    [[nodiscard]] std::vector<SamplerDesc> GetLiveSamplerDescs() const;

    /// Test-only hook: how many CreateTexture() attempts have reached this device, including
    /// attempts rejected by descriptor validation. It exposes retry behavior without adding a
    /// production cache or changing the IRenderDevice contract.
    [[nodiscard]] std::uint64_t GetTextureCreateAttemptCount() const noexcept;

    /// Test-only hook: how many times Present() has been called since construction.
    [[nodiscard]] std::uint64_t GetPresentCallCount() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace UVE::Render
