// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "matrix4x4.h"
#include "vector2.h"
#include "vector3.h"
#include "vector4.h"
#include "i_command_buffer.h"
#include "recorded_command.h"

namespace UVE::Render {

/// NullCommandBuffer (engine/render, module-private — never named outside engine/render/src/)
/// is the "spy" retained command buffer NullRenderDevice hands out: every call is appended, in
/// order, to an internal std::vector<RecordedCommand> instead of touching any GPU (there is
/// none in this sandbox). NullRenderDevice::Submit() reads the finished list via
/// GetRecordedCommands() before the buffer is destroyed, so
/// NullRenderDevice::GetLastSubmittedCommands() can hand it to tests, letting them assert
/// exactly what a real backend would have received.
/// Thread-safety: not thread-safe, matching ICommandBuffer's documented single-recorder
/// contract.
class NullCommandBuffer final : public ICommandBuffer {
public:
    /// Resolves a query handle to its kind, or nullopt for an unknown/destroyed handle. The
    /// owning device supplies it so recorded query brackets validate against the real query
    /// table; a buffer constructed without one (tests) only rejects the invalid handle.
    using QueryKindLookup = std::function<std::optional<RenderQueryKind>(QueryHandle)>;

    NullCommandBuffer() = default;
    explicit NullCommandBuffer(QueryKindLookup queryKindLookup)
        : m_queryKindLookup(std::move(queryKindLookup)) {}

  [[nodiscard]] RenderOperationResult BeginQuery(QueryHandle query) override;
  [[nodiscard]] RenderOperationResult EndQuery(QueryHandle query) override;
  [[nodiscard]] RenderOperationResult WriteTimestamp(QueryHandle query) override;
  [[nodiscard]] RenderFeatureSupport GetMemoryBarrierSupport() const noexcept override {
      return RenderFeatureSupport::Model;
  }
  [[nodiscard]] RenderOperationResult MemoryBarrier(MemoryBarrierMask mask) override;
  void BeginRenderPass(const RenderPassDesc &renderPassDesc) override;
  void EndRenderPass() override;
  void BindPipeline(PipelineHandle pipeline) override;
  void BindVertexBuffer(BufferHandle buffer, std::uint32_t slot) override;
  void BindIndexBuffer(BufferHandle buffer) override;
  void BindTexture(TextureHandle texture, std::uint32_t slot) override;
  void BindSampler(SamplerHandle sampler, std::uint32_t slot) override;
  void BindUniformBuffer(BufferHandle buffer, std::uint32_t slot) override;
  void BindStorageBuffer(BufferHandle buffer, std::uint32_t slot) override;
  void SetUniformFloat(std::string_view name, float value) override;
  void SetUniformInt(std::string_view name, std::int32_t value) override;
  void SetUniformBool(std::string_view name, bool value) override;
  void SetUniformVector2(std::string_view name, const Math::Vector2 &value) override;
  void SetUniformVector3(std::string_view name, const Math::Vector3 &value) override;
  void SetUniformVector4(std::string_view name, const Math::Vector4 &value) override;
  void SetUniformMatrix4x4(std::string_view name, const Math::Matrix4x4 &value) override;
  void DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount,
                      std::uint32_t firstIndex) override;
  void DrawIndexedIndirect(BufferHandle buffer, std::uint64_t offsetBytes) override;
  void Draw(std::uint32_t vertexCount, std::uint32_t instanceCount) override;
  void Dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ) override;

  /// Every call recorded so far, in issue order.
  [[nodiscard]] const std::vector<RecordedCommand> &GetRecordedCommands() const noexcept;

private:
    [[nodiscard]] std::optional<RenderQueryKind> LookupQueryKind(QueryHandle query) const;

    bool m_insideRenderPass = false;
    std::vector<RecordedCommand> m_commands;
    QueryKindLookup m_queryKindLookup;
    // Recorded occlusion bracket state: the open query (0 = none) and the pass epoch it was
    // opened in. Every pass marker bumps the epoch, so an End in a different epoch is the
    // "bracket spans pass markers" misuse Vulkan forbids.
    std::uint32_t m_activeQuery = 0U;
    std::uint64_t m_activeQueryPassEpoch = 0U;
    std::uint64_t m_passEpoch = 0U;
};

} // namespace UVE::Render
