// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "bind_group.h"
#include <array>
#include <vector>
#include <gtest/gtest.h>
#include "null_render_device.h"

namespace UVE::Render::Tests {
namespace {
TEST(BindGroupTest, DistinctBindingSpacesMayShareSlotsAndKeepIssueOrder) {
    NullRenderDevice device;
    const auto uniform = device.CreateBuffer({64U, BufferUsage::Uniform});
    const auto storage = device.CreateBuffer({64U, BufferUsage::Storage});
    const auto texture = device.CreateTexture({1U, 1U, TextureFormat::RGBA8Unorm});
    const auto sampler = device.CreateSampler({});
    const std::array<ResourceBinding, 4> bindings{
        BindUniformBufferCommand{uniform, 0U}, BindStorageBufferCommand{storage, 0U},
        BindTextureCommand{texture, 0U}, BindSamplerCommand{sampler, 0U}};
    const std::array<BindGroupSlot, 4> layout{{{ResourceBindingKind::Sampler, 0U},
                                                  {ResourceBindingKind::Texture, 0U},
                                                  {ResourceBindingKind::StorageBuffer, 0U},
                                                  {ResourceBindingKind::UniformBuffer, 0U}}};
    ASSERT_EQ(ValidateBindGroup({bindings, layout}), BindGroupValidation::Valid);
    auto commands = device.CreateCommandBuffer();
    commands->BeginRenderPass({});
    BindGroup(*commands, {bindings, layout});
    commands->EndRenderPass();
    device.Submit(std::move(commands));
    const auto &recorded = device.GetLastSubmittedCommands();
    ASSERT_EQ(recorded.size(), 6U);
    EXPECT_EQ(std::get<BindUniformBufferCommand>(recorded[1]).buffer, uniform);
    EXPECT_EQ(std::get<BindStorageBufferCommand>(recorded[2]).buffer, storage);
    EXPECT_EQ(std::get<BindTextureCommand>(recorded[3]).texture, texture);
    EXPECT_EQ(std::get<BindSamplerCommand>(recorded[4]).sampler, sampler);
}
TEST(BindGroupTest, MalformedPacketsIssueNoPartialBindings) {
    NullRenderDevice device;
    const auto texture = device.CreateTexture({1U, 1U, TextureFormat::RGBA8Unorm});
    std::array<ResourceBinding, 2> bindings{BindTextureCommand{texture, 0U},
                                               BindSamplerCommand{{}, 0U}};
    EXPECT_EQ(ValidateBindGroup({bindings, {}}), BindGroupValidation::InvalidHandle);
    auto commands = device.CreateCommandBuffer();
    commands->BeginRenderPass({});
    BindGroup(*commands, {bindings, {}});
    commands->EndRenderPass();
    device.Submit(std::move(commands));
    EXPECT_EQ(device.GetLastSubmittedCommands().size(), 2U);
    bindings[1] = BindTextureCommand{texture, 0U};
    EXPECT_EQ(ValidateBindGroup({bindings, {}}), BindGroupValidation::DuplicateSlot);
}
TEST(BindGroupTest, ExplicitLayoutRejectsMissingExtraWrongKindAndUnknownKinds) {
    const std::array<ResourceBinding, 1> bindings{BindTextureCommand{TextureHandle{1U}, 2U}};
    std::array<BindGroupSlot, 1> layout{{{ResourceBindingKind::Texture, 1U}}};
    EXPECT_EQ(ValidateBindGroup({bindings, layout}), BindGroupValidation::LayoutMismatch);
    layout[0] = {ResourceBindingKind::UniformBuffer, 2U};
    EXPECT_EQ(ValidateBindGroup({bindings, layout}), BindGroupValidation::LayoutMismatch);
    layout[0] = {static_cast<ResourceBindingKind>(255), 2U};
    EXPECT_EQ(ValidateBindGroup({bindings, layout}), BindGroupValidation::InvalidLayout);
    layout[0] = {ResourceBindingKind::Texture, 2U};
    EXPECT_EQ(ValidateBindGroup({{}, layout}), BindGroupValidation::LayoutMismatch);
    const std::array<BindGroupSlot, 2> duplicate{layout[0], layout[0]};
    EXPECT_EQ(ValidateBindGroup({bindings, duplicate}), BindGroupValidation::InvalidLayout);
}
TEST(BindGroupTest, RetainedCommandsDoNotBorrowCallerBindingStorage) {
    NullRenderDevice device;
    const auto first = device.CreateTexture({1U, 1U, TextureFormat::RGBA8Unorm});
    const auto second = device.CreateTexture({1U, 1U, TextureFormat::RGBA8Unorm});
    auto commands = device.CreateCommandBuffer();
    commands->BeginRenderPass({});
    {
        std::array<ResourceBinding, 1> bindings{BindTextureCommand{first, 3U}};
        BindGroup(*commands, {bindings, {}});
        bindings[0] = BindTextureCommand{second, 4U};
    }
    commands->EndRenderPass();
    device.Submit(std::move(commands));
    const auto &bind = std::get<BindTextureCommand>(device.GetLastSubmittedCommands()[1]);
    EXPECT_EQ(bind.texture, first);
    EXPECT_EQ(bind.slot, 3U);
}
TEST(BindGroupTest, EmptyPacketsAndBoundedCapacityAreExplicit) {
    EXPECT_EQ(ValidateBindGroup({}), BindGroupValidation::Valid);
    std::vector<ResourceBinding> entries;
    for (std::uint32_t slot = 0; slot <= kMaximumBindGroupBindings; ++slot) {
        entries.emplace_back(BindTextureCommand{TextureHandle{1U}, slot});
    }
    EXPECT_EQ(ValidateBindGroup({entries, {}}), BindGroupValidation::TooManyBindings);
    entries.pop_back();
    EXPECT_EQ(ValidateBindGroup({entries, {}}), BindGroupValidation::Valid);
}
} // namespace
} // namespace UVE::Render::Tests
