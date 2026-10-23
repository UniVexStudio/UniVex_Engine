// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "uve/math/matrix4x4_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/math/vector4_uve.h"
#include "uve/rhi/i_command_buffer_uve.h"
#include "uve/rhi/recorded_command_uve.h"

namespace UVE::Render {

/// NullCommandBufferUVE (engine/render, module-private — never named outside engine/render/src/)
/// is the "spy" retained command buffer NullRenderDeviceUVE hands out: every call is appended, in
/// order, to an internal std::vector<RecordedCommandUVE> instead of touching any GPU (there is
/// none in this sandbox). NullRenderDeviceUVE::SubmitUVE() reads the finished list via
/// GetRecordedCommandsUVE() before the buffer is destroyed, so
/// NullRenderDeviceUVE::GetLastSubmittedCommandsUVE() can hand it to tests, letting them assert
/// exactly what a real backend would have received.
/// Thread-safety: not thread-safe, matching ICommandBufferUVE's documented single-recorder
/// contract.
class NullCommandBufferUVE final : public ICommandBufferUVE {
public:
    /// Resolves a query handle to its kind, or nullopt for an unknown/destroyed handle. The
    /// owning device supplies it so recorded query brackets validate against the real query
    /// table; a buffer constructed without one (tests) only rejects the invalid handle.
    using QueryKindLookupUVE = std::function<std::optional<RenderQueryKindUVE>(QueryHandleUVE)>;

    NullCommandBufferUVE() = default;
    explicit NullCommandBufferUVE(QueryKindLookupUVE queryKindLookup)
        : m_queryKindLookup(std::move(queryKindLookup)) {}

  [[nodiscard]] RenderOperationResultUVE BeginQueryUVE(QueryHandleUVE query) override;
  [[nodiscard]] RenderOperationResultUVE EndQueryUVE(QueryHandleUVE query) override;
  [[nodiscard]] RenderOperationResultUVE WriteTimestampUVE(QueryHandleUVE query) override;
  [[nodiscard]] RenderFeatureSupportUVE GetMemoryBarrierSupportUVE() const noexcept override {
      return RenderFeatureSupportUVE::Model;
  }
  [[nodiscard]] RenderOperationResultUVE MemoryBarrierUVE(MemoryBarrierMaskUVE mask) override;
  void BeginRenderPassUVE(const RenderPassDescUVE &renderPassDesc) override;
  void EndRenderPassUVE() override;
  void BindPipelineUVE(PipelineHandleUVE pipeline) override;
  void BindVertexBufferUVE(BufferHandleUVE buffer, std::uint32_t slot) override;
  void BindIndexBufferUVE(BufferHandleUVE buffer) override;
  void BindTextureUVE(TextureHandleUVE texture, std::uint32_t slot) override;
  void BindSamplerUVE(SamplerHandleUVE sampler, std::uint32_t slot) override;
  void BindUniformBufferUVE(BufferHandleUVE buffer, std::uint32_t slot) override;
  void BindStorageBufferUVE(BufferHandleUVE buffer, std::uint32_t slot) override;
  void SetUniformFloatUVE(std::string_view name, float value) override;
  void SetUniformIntUVE(std::string_view name, std::int32_t value) override;
  void SetUniformBoolUVE(std::string_view name, bool value) override;
  void SetUniformVector2UVE(std::string_view name, const Math::Vector2UVE &value) override;
  void SetUniformVector3UVE(std::string_view name, const Math::Vector3UVE &value) override;
  void SetUniformVector4UVE(std::string_view name, const Math::Vector4UVE &value) override;
  void SetUniformMatrix4x4UVE(std::string_view name, const Math::Matrix4x4UVE &value) override;
  void DrawIndexedUVE(std::uint32_t indexCount, std::uint32_t instanceCount,
                      std::uint32_t firstIndex) override;
  void DrawIndexedIndirectUVE(BufferHandleUVE buffer, std::uint64_t offsetBytes) override;
  void DrawUVE(std::uint32_t vertexCount, std::uint32_t instanceCount) override;
  void DispatchUVE(std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ) override;

  /// Every call recorded so far, in issue order.
  [[nodiscard]] const std::vector<RecordedCommandUVE> &GetRecordedCommandsUVE() const noexcept;

private:
    [[nodiscard]] std::optional<RenderQueryKindUVE> LookupQueryKindUVE(QueryHandleUVE query) const;

    bool m_insideRenderPass = false;
    std::vector<RecordedCommandUVE> m_commands;
    QueryKindLookupUVE m_queryKindLookup;
    // Recorded occlusion bracket state: the open query (0 = none) and the pass epoch it was
    // opened in. Every pass marker bumps the epoch, so an End in a different epoch is the
    // "bracket spans pass markers" misuse Vulkan forbids.
    std::uint32_t m_activeQuery = 0U;
    std::uint64_t m_activeQueryPassEpoch = 0U;
    std::uint64_t m_passEpoch = 0U;
};

} // namespace UVE::Render
