// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "null_render_device.h"
#include <memory>
#include <thread>
#include <vector>
#include <gtest/gtest.h>
namespace UVE::Render::Tests {
TEST(RenderResourceLifetimeTest, SubmissionThreadOnlyQueuesRetirement) {
    NullRenderDevice device;
    const auto texture = device.CreateTexture({1U, 1U, TextureFormat::RGBA8Unorm});
    std::vector<TextureHandle> owned{texture};
    auto lease = device.AdoptTransientTextures(std::move(owned));
    ASSERT_NE(lease, nullptr);
    EXPECT_TRUE(owned.empty());
    auto commands = device.CreateCommandBuffer();
    commands->RetainResources(std::move(lease));
    std::thread worker(
        [&device, commands = std::move(commands)]() mutable { device.Submit(std::move(commands)); });
    worker.join();
    EXPECT_EQ(device.GetLiveResourceCount(), 1U);
    EXPECT_EQ(device.CollectRetiredTextures(), 1U);
    EXPECT_EQ(device.CollectRetiredTextures(), 0U);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);
}
TEST(RenderResourceLifetimeTest, SharedLeaseWaitsForEveryRecording) {
    NullRenderDevice device;
    const auto texture = device.CreateTexture({1U, 1U, TextureFormat::RGBA8Unorm});
    auto lease = device.AdoptTransientTextures(std::vector<TextureHandle>{texture});
    auto a = device.CreateCommandBuffer(), b = device.CreateCommandBuffer();
    a->RetainResources(lease);
    b->RetainResources(std::move(lease));
    a.reset();
    EXPECT_EQ(device.CollectRetiredTextures(), 0U);
    b.reset();
    EXPECT_EQ(device.CollectRetiredTextures(), 1U);
}
TEST(RenderResourceLifetimeTest, InvalidDuplicateAndDeadHandlesDoNotTransferOwnership) {
    NullRenderDevice device;
    const auto texture = device.CreateTexture({1U, 1U, TextureFormat::RGBA8Unorm});
    std::vector<TextureHandle> duplicate{texture, texture};
    EXPECT_EQ(device.AdoptTransientTextures(std::move(duplicate)), nullptr);
    EXPECT_EQ(duplicate.size(), 2U);
    device.DestroyTexture(texture);
    std::vector<TextureHandle> stale{texture};
    EXPECT_EQ(device.AdoptTransientTextures(std::move(stale)), nullptr);
    EXPECT_EQ(stale.size(), 1U);
    std::vector<TextureHandle> invalid{{}};
    EXPECT_EQ(device.AdoptTransientTextures(std::move(invalid)), nullptr);
    EXPECT_EQ(invalid.size(), 1U);
}
TEST(RenderResourceLifetimeTest, RecordingMayBeDiscardedAfterTheDeviceIsGone) {
    auto device = std::make_unique<NullRenderDevice>();
    const auto texture = device->CreateTexture({1U, 1U, TextureFormat::RGBA8Unorm});
    auto commands = device->CreateCommandBuffer();
    auto lease = device->AdoptTransientTextures(std::vector<TextureHandle>{texture});
    std::weak_ptr<void> observer = lease;
    commands->RetainResources(std::move(lease));
    device.reset();
    EXPECT_FALSE(observer.expired());
    commands.reset(); // weak retirement state: no call through a destroyed device
    EXPECT_TRUE(observer.expired());
}
} // namespace UVE::Render::Tests
