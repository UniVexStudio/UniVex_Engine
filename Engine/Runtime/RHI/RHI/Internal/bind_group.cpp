// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "bind_group.h"
#include <algorithm>
#include <type_traits>
#include "logging_macros.h"
#include "i_command_buffer.h"

namespace UVE::Render {
namespace {
BindGroupSlot Slot(const ResourceBinding &binding) noexcept {
    return {static_cast<ResourceBindingKind>(binding.index()),
            std::visit([](const auto &value) { return value.slot; }, binding)};
}
bool HasHandle(const ResourceBinding &binding) noexcept {
    return std::visit(
        [](const auto &value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, BindTextureCommand>)
                return value.texture != kInvalidTextureHandle;
            else if constexpr (std::is_same_v<T, BindSamplerCommand>)
                return value.sampler != kInvalidSamplerHandle;
            else
                return value.buffer != kInvalidBufferHandle;
        },
        binding);
}
} // namespace
BindGroupValidation ValidateBindGroup(const BindGroupDesc &group) noexcept {
    if (group.bindings.size() > kMaximumBindGroupBindings ||
        group.layout.size() > kMaximumBindGroupBindings) {
        return BindGroupValidation::TooManyBindings;
    }
    for (std::size_t i = 0U; i < group.bindings.size(); ++i) {
        const auto &binding = group.bindings[i];
        if (binding.valueless_by_exception())
            return BindGroupValidation::InvalidLayout;
        if (!HasHandle(binding))
            return BindGroupValidation::InvalidHandle;
        for (std::size_t j = 0U; j < i; ++j) {
            if (Slot(binding) == Slot(group.bindings[j]))
                return BindGroupValidation::DuplicateSlot;
        }
    }
    if (!group.layout.empty()) {
        for (std::size_t i = 0U; i < group.layout.size(); ++i) {
            const auto &slot = group.layout[i];
            if (static_cast<std::uint8_t>(slot.kind) >
                static_cast<std::uint8_t>(ResourceBindingKind::Sampler)) {
                return BindGroupValidation::InvalidLayout;
            }
            for (std::size_t j = 0U; j < i; ++j) {
                if (slot == group.layout[j])
                    return BindGroupValidation::InvalidLayout;
            }
        }
        if (group.layout.size() != group.bindings.size())
            return BindGroupValidation::LayoutMismatch;
        for (const auto &binding : group.bindings) {
            if (std::find(group.layout.begin(), group.layout.end(), Slot(binding)) == group.layout.end()) {
                return BindGroupValidation::LayoutMismatch;
            }
        }
    }
    return BindGroupValidation::Valid;
}
void BindGroup(ICommandBuffer &commands, const BindGroupDesc &group) {
    const auto validation = ValidateBindGroup(group);
    if (validation != BindGroupValidation::Valid) {
        UVE_ERROR("BindGroup: rejected descriptor ({})", static_cast<unsigned>(validation));
        return;
    }
    for (const auto &binding : group.bindings) {
        std::visit(
            [&commands](const auto &value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, BindUniformBufferCommand>)
                    commands.BindUniformBuffer(value.buffer, value.slot);
                else if constexpr (std::is_same_v<T, BindStorageBufferCommand>)
                    commands.BindStorageBuffer(value.buffer, value.slot);
                else if constexpr (std::is_same_v<T, BindTextureCommand>)
                    commands.BindTexture(value.texture, value.slot);
                else
                    commands.BindSampler(value.sampler, value.slot);
            },
            binding);
    }
}
} // namespace UVE::Render
