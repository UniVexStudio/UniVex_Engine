// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/rhi_null/null_render_device_uve.h"
#include <memory>
#include <thread>
#include <vector>
#include <gtest/gtest.h>
namespace UVE::Render::Tests {
TEST(RenderResourceLifetimeUVETest, SubmissionThreadOnlyQueuesRetirement) {
    NullRenderDeviceUVE device;
    const auto texture = device.CreateTextureUVE({1U, 1U, TextureFormatUVE::RGBA8Unorm});
    std::vector<TextureHandleUVE> owned{texture};
    auto lease = device.AdoptTransientTexturesUVE(std::move(owned));
    ASSERT_NE(lease, nullptr);
    EXPECT_TRUE(owned.empty());
    auto commands = device.CreateCommandBufferUVE();
    commands->RetainResourcesUVE(std::move(lease));
    std::thread worker(
        [&device, commands = std::move(commands)]() mutable { device.SubmitUVE(std::move(commands)); });
    worker.join();
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 1U);
    EXPECT_EQ(device.CollectRetiredTexturesUVE(), 1U);
    EXPECT_EQ(device.CollectRetiredTexturesUVE(), 0U);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);
}
TEST(RenderResourceLifetimeUVETest, SharedLeaseWaitsForEveryRecording) {
    NullRenderDeviceUVE device;
    const auto texture = device.CreateTextureUVE({1U, 1U, TextureFormatUVE::RGBA8Unorm});
    auto lease = device.AdoptTransientTexturesUVE(std::vector<TextureHandleUVE>{texture});
    auto a = device.CreateCommandBufferUVE(), b = device.CreateCommandBufferUVE();
    a->RetainResourcesUVE(lease);
    b->RetainResourcesUVE(std::move(lease));
    a.reset();
    EXPECT_EQ(device.CollectRetiredTexturesUVE(), 0U);
    b.reset();
    EXPECT_EQ(device.CollectRetiredTexturesUVE(), 1U);
}
TEST(RenderResourceLifetimeUVETest, InvalidDuplicateAndDeadHandlesDoNotTransferOwnership) {
    NullRenderDeviceUVE device;
    const auto texture = device.CreateTextureUVE({1U, 1U, TextureFormatUVE::RGBA8Unorm});
    std::vector<TextureHandleUVE> duplicate{texture, texture};
    EXPECT_EQ(device.AdoptTransientTexturesUVE(std::move(duplicate)), nullptr);
    EXPECT_EQ(duplicate.size(), 2U);
    device.DestroyTextureUVE(texture);
    std::vector<TextureHandleUVE> stale{texture};
    EXPECT_EQ(device.AdoptTransientTexturesUVE(std::move(stale)), nullptr);
    EXPECT_EQ(stale.size(), 1U);
    std::vector<TextureHandleUVE> invalid{{}};
    EXPECT_EQ(device.AdoptTransientTexturesUVE(std::move(invalid)), nullptr);
    EXPECT_EQ(invalid.size(), 1U);
}
TEST(RenderResourceLifetimeUVETest, RecordingMayBeDiscardedAfterTheDeviceIsGone) {
    auto device = std::make_unique<NullRenderDeviceUVE>();
    const auto texture = device->CreateTextureUVE({1U, 1U, TextureFormatUVE::RGBA8Unorm});
    auto commands = device->CreateCommandBufferUVE();
    auto lease = device->AdoptTransientTexturesUVE(std::vector<TextureHandleUVE>{texture});
    std::weak_ptr<void> observer = lease;
    commands->RetainResourcesUVE(std::move(lease));
    device.reset();
    EXPECT_FALSE(observer.expired());
    commands.reset(); // weak retirement state: no call through a destroyed device
    EXPECT_TRUE(observer.expired());
}
} // namespace UVE::Render::Tests
