// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/rhi/bind_group_uve.h"
#include <algorithm>
#include <type_traits>
#include "uve/logging/logging_macros_uve.h"
#include "uve/rhi/i_command_buffer_uve.h"

namespace UVE::Render {
namespace {
BindGroupSlotUVE SlotUVE(const ResourceBindingUVE &binding) noexcept {
    return {static_cast<ResourceBindingKindUVE>(binding.index()),
            std::visit([](const auto &value) { return value.slot; }, binding)};
}
bool HasHandleUVE(const ResourceBindingUVE &binding) noexcept {
    return std::visit(
        [](const auto &value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, BindTextureCommandUVE>)
                return value.texture != kInvalidTextureHandleUVE;
            else if constexpr (std::is_same_v<T, BindSamplerCommandUVE>)
                return value.sampler != kInvalidSamplerHandleUVE;
            else
                return value.buffer != kInvalidBufferHandleUVE;
        },
        binding);
}
} // namespace
BindGroupValidationUVE ValidateBindGroupUVE(const BindGroupDescUVE &group) noexcept {
    if (group.bindings.size() > kMaximumBindGroupBindingsUVE ||
        group.layout.size() > kMaximumBindGroupBindingsUVE) {
        return BindGroupValidationUVE::TooManyBindings;
    }
    for (std::size_t i = 0U; i < group.bindings.size(); ++i) {
        const auto &binding = group.bindings[i];
        if (binding.valueless_by_exception())
            return BindGroupValidationUVE::InvalidLayout;
        if (!HasHandleUVE(binding))
            return BindGroupValidationUVE::InvalidHandle;
        for (std::size_t j = 0U; j < i; ++j) {
            if (SlotUVE(binding) == SlotUVE(group.bindings[j]))
                return BindGroupValidationUVE::DuplicateSlot;
        }
    }
    if (!group.layout.empty()) {
        for (std::size_t i = 0U; i < group.layout.size(); ++i) {
            const auto &slot = group.layout[i];
            if (static_cast<std::uint8_t>(slot.kind) >
                static_cast<std::uint8_t>(ResourceBindingKindUVE::Sampler)) {
                return BindGroupValidationUVE::InvalidLayout;
            }
            for (std::size_t j = 0U; j < i; ++j) {
                if (slot == group.layout[j])
                    return BindGroupValidationUVE::InvalidLayout;
            }
        }
        if (group.layout.size() != group.bindings.size())
            return BindGroupValidationUVE::LayoutMismatch;
        for (const auto &binding : group.bindings) {
            if (std::find(group.layout.begin(), group.layout.end(), SlotUVE(binding)) == group.layout.end()) {
                return BindGroupValidationUVE::LayoutMismatch;
            }
        }
    }
    return BindGroupValidationUVE::Valid;
}
void BindGroupUVE(ICommandBufferUVE &commands, const BindGroupDescUVE &group) {
    const auto validation = ValidateBindGroupUVE(group);
    if (validation != BindGroupValidationUVE::Valid) {
        UVE_ERROR("BindGroupUVE: rejected descriptor ({})", static_cast<unsigned>(validation));
        return;
    }
    for (const auto &binding : group.bindings) {
        std::visit(
            [&commands](const auto &value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, BindUniformBufferCommandUVE>)
                    commands.BindUniformBufferUVE(value.buffer, value.slot);
                else if constexpr (std::is_same_v<T, BindStorageBufferCommandUVE>)
                    commands.BindStorageBufferUVE(value.buffer, value.slot);
                else if constexpr (std::is_same_v<T, BindTextureCommandUVE>)
                    commands.BindTextureUVE(value.texture, value.slot);
                else
                    commands.BindSamplerUVE(value.sampler, value.slot);
            },
            binding);
    }
}
} // namespace UVE::Render
