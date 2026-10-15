// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/rhi/bind_group_uve.h"
#include <array>
#include <vector>
#include <gtest/gtest.h>
#include "uve/rhi_null/null_render_device_uve.h"

namespace UVE::Render::Tests {
namespace {
TEST(BindGroupUVETest, DistinctBindingSpacesMayShareSlotsAndKeepIssueOrder) {
    NullRenderDeviceUVE device;
    const auto uniform = device.CreateBufferUVE({64U, BufferUsageUVE::Uniform});
    const auto storage = device.CreateBufferUVE({64U, BufferUsageUVE::Storage});
    const auto texture = device.CreateTextureUVE({1U, 1U, TextureFormatUVE::RGBA8Unorm});
    const auto sampler = device.CreateSamplerUVE({});
    const std::array<ResourceBindingUVE, 4> bindings{
        BindUniformBufferCommandUVE{uniform, 0U}, BindStorageBufferCommandUVE{storage, 0U},
        BindTextureCommandUVE{texture, 0U}, BindSamplerCommandUVE{sampler, 0U}};
    const std::array<BindGroupSlotUVE, 4> layout{{{ResourceBindingKindUVE::Sampler, 0U},
                                                  {ResourceBindingKindUVE::Texture, 0U},
                                                  {ResourceBindingKindUVE::StorageBuffer, 0U},
                                                  {ResourceBindingKindUVE::UniformBuffer, 0U}}};
    ASSERT_EQ(ValidateBindGroupUVE({bindings, layout}), BindGroupValidationUVE::Valid);
    auto commands = device.CreateCommandBufferUVE();
    commands->BeginRenderPassUVE({});
    BindGroupUVE(*commands, {bindings, layout});
    commands->EndRenderPassUVE();
    device.SubmitUVE(std::move(commands));
    const auto &recorded = device.GetLastSubmittedCommandsUVE();
    ASSERT_EQ(recorded.size(), 6U);
    EXPECT_EQ(std::get<BindUniformBufferCommandUVE>(recorded[1]).buffer, uniform);
    EXPECT_EQ(std::get<BindStorageBufferCommandUVE>(recorded[2]).buffer, storage);
    EXPECT_EQ(std::get<BindTextureCommandUVE>(recorded[3]).texture, texture);
    EXPECT_EQ(std::get<BindSamplerCommandUVE>(recorded[4]).sampler, sampler);
}
TEST(BindGroupUVETest, MalformedPacketsIssueNoPartialBindings) {
    NullRenderDeviceUVE device;
    const auto texture = device.CreateTextureUVE({1U, 1U, TextureFormatUVE::RGBA8Unorm});
    std::array<ResourceBindingUVE, 2> bindings{BindTextureCommandUVE{texture, 0U},
                                               BindSamplerCommandUVE{{}, 0U}};
    EXPECT_EQ(ValidateBindGroupUVE({bindings, {}}), BindGroupValidationUVE::InvalidHandle);
    auto commands = device.CreateCommandBufferUVE();
    commands->BeginRenderPassUVE({});
    BindGroupUVE(*commands, {bindings, {}});
    commands->EndRenderPassUVE();
    device.SubmitUVE(std::move(commands));
    EXPECT_EQ(device.GetLastSubmittedCommandsUVE().size(), 2U);
    bindings[1] = BindTextureCommandUVE{texture, 0U};
    EXPECT_EQ(ValidateBindGroupUVE({bindings, {}}), BindGroupValidationUVE::DuplicateSlot);
}
TEST(BindGroupUVETest, ExplicitLayoutRejectsMissingExtraWrongKindAndUnknownKinds) {
    const std::array<ResourceBindingUVE, 1> bindings{BindTextureCommandUVE{TextureHandleUVE{1U}, 2U}};
    std::array<BindGroupSlotUVE, 1> layout{{{ResourceBindingKindUVE::Texture, 1U}}};
    EXPECT_EQ(ValidateBindGroupUVE({bindings, layout}), BindGroupValidationUVE::LayoutMismatch);
    layout[0] = {ResourceBindingKindUVE::UniformBuffer, 2U};
    EXPECT_EQ(ValidateBindGroupUVE({bindings, layout}), BindGroupValidationUVE::LayoutMismatch);
    layout[0] = {static_cast<ResourceBindingKindUVE>(255), 2U};
    EXPECT_EQ(ValidateBindGroupUVE({bindings, layout}), BindGroupValidationUVE::InvalidLayout);
    layout[0] = {ResourceBindingKindUVE::Texture, 2U};
    EXPECT_EQ(ValidateBindGroupUVE({{}, layout}), BindGroupValidationUVE::LayoutMismatch);
    const std::array<BindGroupSlotUVE, 2> duplicate{layout[0], layout[0]};
    EXPECT_EQ(ValidateBindGroupUVE({bindings, duplicate}), BindGroupValidationUVE::InvalidLayout);
}
TEST(BindGroupUVETest, RetainedCommandsDoNotBorrowCallerBindingStorage) {
    NullRenderDeviceUVE device;
    const auto first = device.CreateTextureUVE({1U, 1U, TextureFormatUVE::RGBA8Unorm});
    const auto second = device.CreateTextureUVE({1U, 1U, TextureFormatUVE::RGBA8Unorm});
    auto commands = device.CreateCommandBufferUVE();
    commands->BeginRenderPassUVE({});
    {
        std::array<ResourceBindingUVE, 1> bindings{BindTextureCommandUVE{first, 3U}};
        BindGroupUVE(*commands, {bindings, {}});
        bindings[0] = BindTextureCommandUVE{second, 4U};
    }
    commands->EndRenderPassUVE();
    device.SubmitUVE(std::move(commands));
    const auto &bind = std::get<BindTextureCommandUVE>(device.GetLastSubmittedCommandsUVE()[1]);
    EXPECT_EQ(bind.texture, first);
    EXPECT_EQ(bind.slot, 3U);
}
TEST(BindGroupUVETest, EmptyPacketsAndBoundedCapacityAreExplicit) {
    EXPECT_EQ(ValidateBindGroupUVE({}), BindGroupValidationUVE::Valid);
    std::vector<ResourceBindingUVE> entries;
    for (std::uint32_t slot = 0; slot <= kMaximumBindGroupBindingsUVE; ++slot) {
        entries.emplace_back(BindTextureCommandUVE{TextureHandleUVE{1U}, slot});
    }
    EXPECT_EQ(ValidateBindGroupUVE({entries, {}}), BindGroupValidationUVE::TooManyBindings);
    entries.pop_back();
    EXPECT_EQ(ValidateBindGroupUVE({entries, {}}), BindGroupValidationUVE::Valid);
}
} // namespace
} // namespace UVE::Render::Tests
