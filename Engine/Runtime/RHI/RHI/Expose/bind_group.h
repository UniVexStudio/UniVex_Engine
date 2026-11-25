// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include "recorded_command.h"

namespace UVE::Render {
class ICommandBuffer;
/// Reuse the RHI's existing resource commands rather than inventing another handle domain.
using ResourceBinding = std::variant<BindUniformBufferCommand, BindStorageBufferCommand,
                                        BindTextureCommand, BindSamplerCommand>;
enum class ResourceBindingKind : std::uint8_t { UniformBuffer = 0, StorageBuffer, Texture, Sampler };
struct BindGroupSlot final {
    ResourceBindingKind kind = ResourceBindingKind::UniformBuffer;
    std::uint32_t slot = 0U;
    [[nodiscard]] bool operator==(const BindGroupSlot &) const noexcept = default;
};
struct BindGroupDesc final {
    std::span<const ResourceBinding> bindings;
    /// Optional exact expected layout. With no layout supplied, the packet defines its own.
    /// Each (kind,slot) occurs once. Texture and sampler may share a slot; different buffer
    /// binding spaces may also share an index, matching the existing backend contract.
    std::span<const BindGroupSlot> layout;
};
enum class BindGroupValidation : std::uint8_t {
    Valid = 0,
    TooManyBindings,
    InvalidHandle,
    DuplicateSlot,
    InvalidLayout,
    LayoutMismatch
};
inline constexpr std::size_t kMaximumBindGroupBindings = 64U;
[[nodiscard]] BindGroupValidation ValidateBindGroup(const BindGroupDesc &group) noexcept;

/// A validated, backend-independent binding packet, not a new GPU resource allocator or a claim
/// of arbitrary native descriptor-set numbering. Validation is failure-atomic: malformed packets
/// log an error and issue NO bind calls. Accepted packets forward the existing bindings in input
/// order; GL applies them, Vulkan retains/replays them, and Null records them. Unlisted slots are
/// unchanged. Spans are borrowed only for this call; retained commands copy the handles.
///
/// The same live-resource/device ownership, graphics/compute pass and hardware-slot-limit rules
/// as the underlying ICommandBuffer methods still apply and are validated by those backends.
/// No success/GPU-completion result is fabricated: use ValidateBindGroup for descriptor checks,
/// and the existing backend diagnostics for resource/state errors. GPU resources must outlive use.
void BindGroup(ICommandBuffer &commands, const BindGroupDesc &group);
} // namespace UVE::Render
