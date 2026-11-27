// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <memory>

#include "i_render_device.h"
#include "i_window_manager.h"

namespace UVE::Render {

/// GlRenderDevice is the real, OpenGL 4.6 Core Profile-targeting implementation of
/// IRenderDevice. Constructed strictly *after* an IWindowManager& that has already created
/// its window and made its GL context current — GlRenderDevice never creates, destroys, or
/// activates a GL context itself (see docs/CODING_STANDARDS.md, "WindowManager owns the GL
/// context lifecycle"); it only loads GL function pointers (via the engine's shared proc-table
/// loader, uve/rhi_opengl/gl_functions_uve.h — see that header for why it is public) and issues
/// GL calls against the context that already exists. Every GL header beyond that one contract
/// header (`<GL/gl.h>`, `<GL/glext.h>`) and every GLuint/GLenum type is otherwise confined
/// to engine/render/src/ — this header, like every other public IRenderDevice consumer's, only
/// ever sees the same backend-agnostic RHI types NullRenderDevice does.
/// Thread-safety: not thread-safe, matching IRenderDevice's own documented contract; every
/// method is intended to be called only from the main engine/render thread.
class GlRenderDevice final : public IRenderDevice {
public:
    /// `windowManager` must outlive this GlRenderDevice, must already have a current GL
    /// context (IsValid() == true), and its window must never be destroyed before this
    /// GlRenderDevice is — every GL object this device owns must be destroyed while the
    /// context windowManager owns is still valid.
    explicit GlRenderDevice(Window::IWindowManager& windowManager);
    ~GlRenderDevice() override;
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
    [[nodiscard]] RenderOperationResult CreateQuery(RenderQueryKind, QueryHandle &) override;
    [[nodiscard]] RenderOperationResult BeginQuery(QueryHandle) override;
    [[nodiscard]] RenderOperationResult EndQuery(QueryHandle) override;
    [[nodiscard]] RenderOperationResult WriteTimestamp(QueryHandle) override;
    [[nodiscard]] RenderOperationResult GetQueryResult(QueryHandle, RenderQueryResult &,
                                                             bool = false) override;
    [[nodiscard]] RenderOperationResult DestroyQuery(QueryHandle) override;
    [[nodiscard]] RenderOperationResult GetSurfaceInfo(RenderSurfaceInfo &) const override;

    GlRenderDevice(const GlRenderDevice&) = delete;
    GlRenderDevice& operator=(const GlRenderDevice&) = delete;

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
    void Present() override;
    [[nodiscard]] std::uint64_t GetPresentEpoch() const noexcept override { return m_presentEpoch; }

    [[nodiscard]] std::string_view GetBackendName() const noexcept override;

    /// Returns true only when construction found a valid current context and loaded every GL
    /// entry point required by this device. A false result is a recoverable backend-selection
    /// outcome; callers must not use the device as an active GL backend in that state.
    [[nodiscard]] bool IsUsable() const noexcept override;

    /// Test-only hook (not part of IRenderDevice): how many buffer/texture/shader/pipeline
    /// resources are currently alive — mirrors NullRenderDevice::GetLiveResourceCount()'s
    /// role so tests can confirm cleanup the same way regardless of backend.
    [[nodiscard]] std::size_t GetLiveResourceCount() const noexcept;

    /// Editor-integration-only hook (not part of IRenderDevice, and not exposed on any other
    /// backend): returns the raw GL texture object name behind `texture`, or 0 if the handle is
    /// unknown. Every other IRenderDevice consumer only ever sees the backend-agnostic
    /// TextureHandle (per this class's own doc comment above) - this is a deliberate, narrowly
    /// scoped exception for the one caller that must hand a real GLuint to the editor's view to
    /// display a Renderer3D::RenderFrameToTarget() result inside an editor panel; it must
    /// never be used to justify leaking native GL types anywhere else.
    [[nodiscard]] std::uint32_t GetNativeTextureId(TextureHandle texture) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::uint64_t m_presentEpoch = 0U;
};

} // namespace UVE::Render
