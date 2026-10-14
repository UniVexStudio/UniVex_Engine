// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "uve/rhi/i_render_device_uve.h"
#include "uve/rhi/recorded_command_uve.h"

namespace UVE::Render {

/// NullRenderDeviceUVE is the only IRenderDeviceUVE backend this sandbox can build and test: it
/// performs zero real GPU work — there is no display server, GPU device object, or graphics SDK
/// available here (confirmed and documented in docs/CODING_STANDARDS.md) — and instead validates
/// and bookkeeps every call, handing out a NullCommandBufferUVE "spy" (engine/render/src/,
/// module-private) that records the exact sequence of RHI calls a real backend would have
/// received. Existing solely so the rest of the rendering pipeline (CameraSystemUVE,
/// MeshRendererUVE, Renderer3DUVE — later increments) can be built and unit-tested against a real
/// IRenderDeviceUVE& today; a genuine Vulkan/Metal/D3D12 backend is future work once this
/// environment has the SDK headers, GPU, and windowing it currently lacks.
/// Thread-safety: not thread-safe. Every method is intended to be called only from the main
/// engine/render thread, matching RenderSystemUVE's own single-threaded frame contract.
class NullRenderDeviceUVE final : public IRenderDeviceUVE {
public:
    NullRenderDeviceUVE();
    ~NullRenderDeviceUVE() override;
    [[nodiscard]] RenderDeviceCapabilitiesUVE GetCapabilitiesUVE() const noexcept override;
    [[nodiscard]] RenderOperationResultUVE GetTextureDescUVE(TextureHandleUVE,
                                                             TextureDescUVE &) const override;
    [[nodiscard]] RenderOperationResultUVE GetBufferDescUVE(BufferHandleUVE,
                                                            BufferDescUVE &) const override;
    [[nodiscard]] RenderOperationResultUVE CopyBufferUVE(const BufferCopyDescUVE &) override;
    [[nodiscard]] RenderOperationResultUVE BlitTextureUVE(const TextureBlitDescUVE &) override;
    [[nodiscard]] RenderOperationResultUVE GenerateMipmapsUVE(TextureHandleUVE) override;
    [[nodiscard]] RenderOperationResultUVE InsertFenceUVE(FenceHandleUVE &) override;
    [[nodiscard]] RenderOperationResultUVE WaitFenceUVE(FenceHandleUVE, std::uint64_t) override;
    [[nodiscard]] RenderOperationResultUVE DestroyFenceUVE(FenceHandleUVE) override;
    [[nodiscard]] RenderOperationResultUVE WaitIdleUVE() override;
    /// Timeline semaphores are the one part of the Null model that is genuinely concurrent: a
    /// wait blocks, so the thread that signals is by definition a different one. The semaphore
    /// table therefore carries its own mutex and condition variable instead of relying on the
    /// device's single-threaded contract.
    [[nodiscard]] RenderOperationResultUVE CreateSemaphoreUVE(const SemaphoreDescUVE &,
                                                              SemaphoreHandleUVE &) override;
    [[nodiscard]] RenderOperationResultUVE GetSemaphoreCounterUVE(SemaphoreHandleUVE,
                                                                  std::uint64_t &) const override;
    [[nodiscard]] RenderOperationResultUVE SignalSemaphoreUVE(SemaphoreHandleUVE, std::uint64_t) override;
    [[nodiscard]] RenderOperationResultUVE WaitSemaphoreUVE(SemaphoreHandleUVE, std::uint64_t,
                                                            std::uint64_t) override;
    [[nodiscard]] RenderOperationResultUVE DestroySemaphoreUVE(SemaphoreHandleUVE) override;
    [[nodiscard]] RenderOperationResultUVE CreateQueryUVE(RenderQueryKindUVE, QueryHandleUVE &) override;
    [[nodiscard]] RenderOperationResultUVE BeginQueryUVE(QueryHandleUVE) override;
    [[nodiscard]] RenderOperationResultUVE EndQueryUVE(QueryHandleUVE) override;
    [[nodiscard]] RenderOperationResultUVE WriteTimestampUVE(QueryHandleUVE) override;
    [[nodiscard]] RenderOperationResultUVE GetQueryResultUVE(QueryHandleUVE, RenderQueryResultUVE &,
                                                             bool = false) override;
    [[nodiscard]] RenderOperationResultUVE DestroyQueryUVE(QueryHandleUVE) override;
    /// Bounded diagnostic history of the last 256 device operations, distinct from submitted
    /// command lists. Model queries deliberately expose no fabricated GPU result.
    [[nodiscard]] const std::vector<RecordedDeviceOperationUVE> &
    GetRecordedDeviceOperationsUVE() const noexcept;
    [[nodiscard]] std::uint64_t GetDroppedDeviceOperationCountUVE() const noexcept;

    NullRenderDeviceUVE(const NullRenderDeviceUVE&) = delete;
    NullRenderDeviceUVE& operator=(const NullRenderDeviceUVE&) = delete;

    [[nodiscard]] BufferHandleUVE CreateBufferUVE(const BufferDescUVE& desc,
                                                   std::span<const std::byte> initialData = {}) override;
    void DestroyBufferUVE(BufferHandleUVE buffer) override;
    [[nodiscard]] bool UpdateBufferUVE(BufferHandleUVE buffer, std::span<const std::byte> data,
                                        std::uint64_t offsetBytes = 0) override;
    [[nodiscard]] bool ReadbackBufferUVE(BufferHandleUVE buffer, std::span<std::byte> outData,
                                          std::uint64_t offsetBytes = 0) override;

    [[nodiscard]] TextureHandleUVE CreateTextureUVE(const TextureDescUVE& desc,
                                                     std::span<const std::byte> initialData = {}) override;
    [[nodiscard]] bool SupportsTextureFormatUVE(
        TextureFormatUVE format, TextureColorSpaceUVE colorSpace = TextureColorSpaceUVE::Linear) const noexcept override;
    void DestroyTextureUVE(TextureHandleUVE texture) override;
    [[nodiscard]] SamplerHandleUVE CreateSamplerUVE(const SamplerDescUVE& desc) override;
    void DestroySamplerUVE(SamplerHandleUVE sampler) override;

    [[nodiscard]] ShaderHandleUVE CreateShaderUVE(const ShaderDescUVE& desc, std::string* outInfoLog = nullptr) override;
    void DestroyShaderUVE(ShaderHandleUVE shader) override;

    [[nodiscard]] PipelineHandleUVE CreatePipelineUVE(const PipelineDescUVE& desc,
                                                       std::string* outInfoLog = nullptr) override;
    [[nodiscard]] PipelineHandleUVE CreateComputePipelineUVE(const ComputePipelineDescUVE& desc,
                                                              std::string* outInfoLog = nullptr) override;
    void DestroyPipelineUVE(PipelineHandleUVE pipeline) override;

    [[nodiscard]] std::vector<UniformReflectionUVE> GetPipelineUniformsUVE(PipelineHandleUVE pipeline) const override;
    [[nodiscard]] bool GetPipelineBinaryUVE(PipelineHandleUVE pipeline, std::vector<std::byte>& outBinary,
                                             std::uint32_t& outFormat) const override;
    [[nodiscard]] PipelineHandleUVE CreatePipelineFromBinaryUVE(std::span<const std::byte> binary,
                                                                 std::uint32_t format,
                                                                 const PipelineBinaryDescUVE& desc) override;

    [[nodiscard]] std::unique_ptr<ICommandBufferUVE> CreateCommandBufferUVE() override;
    void SubmitUVE(std::unique_ptr<ICommandBufferUVE> commandBuffer) override;
    /// Foundation WP4: validates the desc against this device's semaphore table, records a
    /// SubmitOperationUVE with the exact waits/signals, then submits like SubmitUVE. No counter
    /// moves: a recorded signal is evidence of a request, never of GPU progress.
    [[nodiscard]] RenderOperationResultUVE SubmitWithSyncUVE(std::unique_ptr<ICommandBufferUVE> commandBuffer,
                                                            const SubmitDescUVE &desc) override;
    void PresentUVE() override;

    [[nodiscard]] bool IsUsableUVE() const noexcept override;
    [[nodiscard]] std::string_view GetBackendNameUVE() const noexcept override;

    /// Test-only hook (not part of IRenderDeviceUVE): the command list from the most recently
    /// submitted command buffer, in recorded order. Empty until the first SubmitUVE() call.
    [[nodiscard]] const std::vector<RecordedCommandUVE>& GetLastSubmittedCommandsUVE() const noexcept;

    /// Test-only hook: how many buffer/texture/shader/pipeline resources are currently alive
    /// (created but not yet destroyed) — lets tests confirm cleanup without a GPU to inspect.
    [[nodiscard]] std::size_t GetLiveResourceCountUVE() const noexcept;

    /// Test-only hook: snapshots descriptors for live textures so renderer integration tests can
    /// verify asset metadata reached the RHI without exposing backend storage through the RHI API.
    [[nodiscard]] std::vector<TextureDescUVE> GetLiveTextureDescsUVE() const;

    /// Test-only hook: snapshots descriptors for live samplers (the Tier 2.2 analogue of
    /// GetLiveTextureDescsUVE above).
    [[nodiscard]] std::vector<SamplerDescUVE> GetLiveSamplerDescsUVE() const;

    /// Test-only hook: how many CreateTextureUVE() attempts have reached this device, including
    /// attempts rejected by descriptor validation. It exposes retry behavior without adding a
    /// production cache or changing the IRenderDeviceUVE contract.
    [[nodiscard]] std::uint64_t GetTextureCreateAttemptCountUVE() const noexcept;

    /// Test-only hook: how many times PresentUVE() has been called since construction.
    [[nodiscard]] std::uint64_t GetPresentCallCountUVE() const noexcept;

private:
    struct ImplUVE;
    std::unique_ptr<ImplUVE> m_impl;
};

} // namespace UVE::Render
