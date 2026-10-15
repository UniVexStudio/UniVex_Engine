// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include "uve/rhi/recorded_command_uve.h"

namespace UVE::Render {
class ICommandBufferUVE;
/// Reuse the RHI's existing resource commands rather than inventing another handle domain.
using ResourceBindingUVE = std::variant<BindUniformBufferCommandUVE, BindStorageBufferCommandUVE,
                                        BindTextureCommandUVE, BindSamplerCommandUVE>;
enum class ResourceBindingKindUVE : std::uint8_t { UniformBuffer = 0, StorageBuffer, Texture, Sampler };
struct BindGroupSlotUVE final {
    ResourceBindingKindUVE kind = ResourceBindingKindUVE::UniformBuffer;
    std::uint32_t slot = 0U;
    [[nodiscard]] bool operator==(const BindGroupSlotUVE &) const noexcept = default;
};
struct BindGroupDescUVE final {
    std::span<const ResourceBindingUVE> bindings;
    /// Optional exact expected layout. With no layout supplied, the packet defines its own.
    /// Each (kind,slot) occurs once. Texture and sampler may share a slot; different buffer
    /// binding spaces may also share an index, matching the existing backend contract.
    std::span<const BindGroupSlotUVE> layout;
};
enum class BindGroupValidationUVE : std::uint8_t {
    Valid = 0,
    TooManyBindings,
    InvalidHandle,
    DuplicateSlot,
    InvalidLayout,
    LayoutMismatch
};
inline constexpr std::size_t kMaximumBindGroupBindingsUVE = 64U;
[[nodiscard]] BindGroupValidationUVE ValidateBindGroupUVE(const BindGroupDescUVE &group) noexcept;

/// A validated, backend-independent binding packet, not a new GPU resource allocator or a claim
/// of arbitrary native descriptor-set numbering. Validation is failure-atomic: malformed packets
/// log an error and issue NO bind calls. Accepted packets forward the existing bindings in input
/// order; GL applies them, Vulkan retains/replays them, and Null records them. Unlisted slots are
/// unchanged. Spans are borrowed only for this call; retained commands copy the handles.
///
/// The same live-resource/device ownership, graphics/compute pass and hardware-slot-limit rules
/// as the underlying ICommandBufferUVE methods still apply and are validated by those backends.
/// No success/GPU-completion result is fabricated: use ValidateBindGroupUVE for descriptor checks,
/// and the existing backend diagnostics for resource/state errors. GPU resources must outlive use.
void BindGroupUVE(ICommandBufferUVE &commands, const BindGroupDescUVE &group);
} // namespace UVE::Render
