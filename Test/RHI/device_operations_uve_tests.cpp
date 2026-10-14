// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/rhi/device_operations_uve.h"
#include "uve/rhi/render_surface_uve.h"
#include "uve/rhi_null/null_render_device_uve.h"
#include <array>
#include <limits>
#include <variant>
#include <vector>
#include <gtest/gtest.h>
namespace UVE::Render::Tests {
namespace {
using ResultUVE = RenderOperationResultUVE;
TEST(DeviceOperationsUVETest, NullCapabilitiesNeverClaimNativeTimingOrSurface) {
    NullRenderDeviceUVE device;
    const auto caps = device.GetCapabilitiesUVE();
    EXPECT_EQ(caps.fences, RenderFeatureSupportUVE::Model);
    EXPECT_EQ(caps.memoryBarriers, RenderFeatureSupportUVE::Model);
    EXPECT_EQ(caps.bufferCopy, RenderFeatureSupportUVE::Model);
    EXPECT_EQ(caps.textureBlit, RenderFeatureSupportUVE::Model);
    EXPECT_EQ(caps.timestampQueries, RenderFeatureSupportUVE::Model);
    EXPECT_EQ(caps.surface, RenderFeatureSupportUVE::Unsupported);
    RenderSurfaceUVE surface(device);
    RenderSurfaceInfoUVE sentinel{42, 17, true, true, 9};
    EXPECT_EQ(surface.DescribeUVE(sentinel), ResultUVE::Unsupported);
    EXPECT_EQ(sentinel.width, 42U);
    EXPECT_EQ(surface.PresentUVE(), ResultUVE::Unsupported);
    EXPECT_EQ(device.GetPresentCallCountUVE(), 0U);
}
TEST(DeviceOperationsUVETest, CopyChangesRealModelBytesAndRecordsDescriptor) {
    NullRenderDeviceUVE device;
    const std::array<std::byte, 8> bytes{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    const auto source = device.CreateBufferUVE({8, BufferUsageUVE::Storage}, bytes);
    const auto target = device.CreateBufferUVE({8, BufferUsageUVE::Storage});
    ASSERT_EQ(device.CopyBufferUVE({source, target, 1, 3, 3}), ResultUVE::Recorded);
    std::array<std::byte, 8> output{};
    ASSERT_TRUE(device.ReadbackBufferUVE(target, output));
    EXPECT_EQ(output[2], std::byte{0});
    EXPECT_EQ(output[3], std::byte{2});
    EXPECT_EQ(output[4], std::byte{3});
    EXPECT_EQ(output[5], std::byte{4});
    ASSERT_EQ(device.GetRecordedDeviceOperationsUVE().size(), 1U);
    const auto &copy = std::get<BufferCopyDescUVE>(device.GetRecordedDeviceOperationsUVE().front());
    EXPECT_EQ(copy.sourceOffset, 1U);
    EXPECT_EQ(copy.destinationOffset, 3U);
    EXPECT_EQ(copy.sizeBytes, 3U);
}
TEST(DeviceOperationsUVETest, InvalidCopyRangesAndOverlapAreFailureAtomic) {
    NullRenderDeviceUVE device;
    const std::array<std::byte, 8> bytes{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    const auto buffer = device.CreateBufferUVE({8, BufferUsageUVE::Storage}, bytes);
    EXPECT_EQ(device.CopyBufferUVE({{}, buffer, 0, 0, 1}), ResultUVE::InvalidHandle);
    EXPECT_EQ(device.CopyBufferUVE({buffer, buffer, 0, 0, 0}), ResultUVE::InvalidArgument);
    EXPECT_EQ(device.CopyBufferUVE({buffer, buffer, 0, 1, 4}), ResultUVE::InvalidArgument);
    EXPECT_EQ(device.CopyBufferUVE({buffer, buffer, UINT64_MAX, 0, 1}), ResultUVE::InvalidArgument);
    EXPECT_EQ(device.CopyBufferUVE({buffer, buffer, 0, 7, 2}), ResultUVE::InvalidArgument);
    std::array<std::byte, 8> result{};
    ASSERT_TRUE(device.ReadbackBufferUVE(buffer, result));
    EXPECT_EQ(result, bytes);
    EXPECT_TRUE(device.GetRecordedDeviceOperationsUVE().empty());
    EXPECT_EQ(device.CopyBufferUVE({buffer, buffer, 0, 4, 4}), ResultUVE::Recorded);
    ASSERT_TRUE(device.ReadbackBufferUVE(buffer, result));
    EXPECT_EQ(result[7], std::byte{4});
}
TEST(DeviceOperationsUVETest, BlitValidatesMipLayerFormatAndRegionsBeforeRecording) {
    NullRenderDeviceUVE device;
    const auto source = device.CreateTextureUVE({4, 4, TextureFormatUVE::RGBA8Unorm});
    const auto target = device.CreateTextureUVE({2, 2, TextureFormatUVE::RGBA8Unorm});
    TextureBlitDescUVE blit{source, target, {0, 0, 0, 0, 4, 4}, {0, 0, 0, 0, 2, 2}, BlitFilterUVE::Linear};
    ASSERT_EQ(device.BlitTextureUVE(blit), ResultUVE::Recorded);
    const auto count = device.GetRecordedDeviceOperationsUVE().size();
    blit.sourceRegion.x = UINT32_MAX;
    EXPECT_EQ(device.BlitTextureUVE(blit), ResultUVE::InvalidArgument);
    blit.sourceRegion.x = 0;
    blit.sourceRegion.layer = 1;
    EXPECT_EQ(device.BlitTextureUVE(blit), ResultUVE::InvalidArgument);
    blit.sourceRegion.layer = 0;
    blit.sourceRegion.mip = 1;
    EXPECT_EQ(device.BlitTextureUVE(blit), ResultUVE::InvalidArgument);
    blit.sourceRegion.mip = 0;
    blit.filter = static_cast<BlitFilterUVE>(255);
    EXPECT_EQ(device.BlitTextureUVE(blit), ResultUVE::InvalidArgument);
    EXPECT_EQ(device.GetRecordedDeviceOperationsUVE().size(), count);
    device.DestroyTextureUVE(source);
    EXPECT_EQ(device.BlitTextureUVE(blit), ResultUVE::InvalidHandle);
}
TEST(DeviceOperationsUVETest, MipsRequireAllocatedColorLevelsAndDescriptorReadPreservesFailureOutput) {
    NullRenderDeviceUVE device;
    const std::vector<std::byte> pixels(84U);
    const auto texture = device.CreateTextureUVE({4, 4, TextureFormatUVE::RGBA8Unorm, 3}, pixels);
    ASSERT_NE(texture, kInvalidTextureHandleUVE);
    EXPECT_EQ(device.GenerateMipmapsUVE(texture), ResultUVE::Recorded);
    const auto single = device.CreateTextureUVE({4, 4, TextureFormatUVE::RGBA8Unorm});
    EXPECT_EQ(device.GenerateMipmapsUVE(single), ResultUVE::InvalidArgument);
    EXPECT_EQ(device.GenerateMipmapsUVE({}), ResultUVE::InvalidHandle);
    TextureDescUVE output{99, 99, TextureFormatUVE::RGBA8Unorm};
    EXPECT_EQ(device.GetTextureDescUVE({}, output), ResultUVE::InvalidHandle);
    EXPECT_EQ(output.width, 99U);
    EXPECT_EQ(device.GetTextureDescUVE(texture, output), ResultUVE::Success);
    EXPECT_EQ(output.mipLevels, 3U);
}
TEST(DeviceOperationsUVETest, MemoryBarriersRecordOnlyValidOutsidePassDependencies) {
    NullRenderDeviceUVE device;
    auto commands = device.CreateCommandBufferUVE();
    EXPECT_EQ(commands->MemoryBarrierUVE(static_cast<MemoryBarrierMaskUVE>(0)), ResultUVE::InvalidArgument);
    EXPECT_EQ(commands->MemoryBarrierUVE(static_cast<MemoryBarrierMaskUVE>(64)), ResultUVE::InvalidArgument);
    commands->BeginRenderPassUVE({});
    EXPECT_EQ(commands->MemoryBarrierUVE(MemoryBarrierMaskUVE::All), ResultUVE::InvalidState);
    commands->EndRenderPassUVE();
    EXPECT_EQ(commands->MemoryBarrierUVE(MemoryBarrierMaskUVE::ShaderStorage |
                                         MemoryBarrierMaskUVE::IndirectCommands),
              ResultUVE::Recorded);
    device.SubmitUVE(std::move(commands));
    const auto &recorded = device.GetLastSubmittedCommandsUVE();
    ASSERT_EQ(recorded.size(), 3U);
    EXPECT_EQ(std::get<MemoryBarrierCommandUVE>(recorded.back()).mask,
              MemoryBarrierMaskUVE::ShaderStorage | MemoryBarrierMaskUVE::IndirectCommands);
}
TEST(DeviceOperationsUVETest, FenceLifecycleIsExplicitlyAModelAndRejectsDestroyedHandles) {
    NullRenderDeviceUVE device;
    FenceHandleUVE fence;
    ASSERT_EQ(device.InsertFenceUVE(fence), ResultUVE::Recorded);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 1U);
    EXPECT_EQ(device.WaitFenceUVE(fence, 0), ResultUVE::Recorded);
    EXPECT_EQ(device.DestroyFenceUVE(fence), ResultUVE::Recorded);
    EXPECT_EQ(device.WaitFenceUVE(fence, UINT64_MAX), ResultUVE::InvalidHandle);
    EXPECT_EQ(device.DestroyFenceUVE(fence), ResultUVE::InvalidHandle);
    FenceHandleUVE next;
    ASSERT_EQ(device.InsertFenceUVE(next), ResultUVE::Recorded);
    EXPECT_NE(next, fence);
    EXPECT_EQ(device.WaitIdleUVE(), ResultUVE::Recorded);
}
TEST(DeviceOperationsUVETest, QueryStateMachineRejectsWrongKindsAndNestedQueries) {
    NullRenderDeviceUVE device;
    QueryHandleUVE query, another, timestamp;
    ASSERT_EQ(device.CreateQueryUVE(RenderQueryKindUVE::AnySamplesPassed, query), ResultUVE::Recorded);
    ASSERT_EQ(device.CreateQueryUVE(RenderQueryKindUVE::AnySamplesPassed, another), ResultUVE::Recorded);
    ASSERT_EQ(device.CreateQueryUVE(RenderQueryKindUVE::Timestamp, timestamp), ResultUVE::Recorded);
    auto unchanged = query;
    EXPECT_EQ(device.CreateQueryUVE(static_cast<RenderQueryKindUVE>(255), unchanged),
              ResultUVE::InvalidArgument);
    EXPECT_EQ(unchanged, query);
    EXPECT_EQ(device.EndQueryUVE(query), ResultUVE::InvalidState);
    EXPECT_EQ(device.BeginQueryUVE(timestamp), ResultUVE::InvalidArgument);
    EXPECT_EQ(device.WriteTimestampUVE(query), ResultUVE::InvalidArgument);
    ASSERT_EQ(device.BeginQueryUVE(query), ResultUVE::Recorded);
    EXPECT_EQ(device.BeginQueryUVE(another), ResultUVE::InvalidState);
    EXPECT_EQ(device.EndQueryUVE(another), ResultUVE::InvalidState);
    EXPECT_EQ(device.DestroyQueryUVE(query), ResultUVE::InvalidState);
    ASSERT_EQ(device.EndQueryUVE(query), ResultUVE::Recorded);
    EXPECT_EQ(device.DestroyQueryUVE(query), ResultUVE::Recorded);
    EXPECT_EQ(device.BeginQueryUVE(query), ResultUVE::InvalidHandle);
}
TEST(DeviceOperationsUVETest, NullNeverInventsGpuVisibilityOrNanoseconds) {
    NullRenderDeviceUVE device;
    QueryHandleUVE handle;
    ASSERT_EQ(device.CreateQueryUVE(RenderQueryKindUVE::Timestamp, handle), ResultUVE::Recorded);
    RenderQueryResultUVE output{RenderQueryKindUVE::AnySamplesPassed, 42};
    EXPECT_EQ(device.GetQueryResultUVE(handle, output), ResultUVE::NotReady);
    ASSERT_EQ(device.WriteTimestampUVE(handle), ResultUVE::Recorded);
    EXPECT_EQ(device.GetQueryResultUVE(handle, output, true), ResultUVE::Unsupported);
    EXPECT_EQ(output.kind, RenderQueryKindUVE::AnySamplesPassed);
    EXPECT_EQ(output.value, 42U);
    EXPECT_EQ(device.DestroyQueryUVE(handle), ResultUVE::Recorded);
    EXPECT_EQ(device.GetQueryResultUVE(handle, output), ResultUVE::InvalidHandle);
}
TEST(DeviceOperationsUVETest, DeviceDiagnosticHistoryIsBoundedAndReportsEviction) {
    NullRenderDeviceUVE device;
    for (int i = 0; i < 300; ++i) {
        FenceHandleUVE handle;
        ASSERT_EQ(device.InsertFenceUVE(handle), ResultUVE::Recorded);
        ASSERT_EQ(device.DestroyFenceUVE(handle), ResultUVE::Recorded);
    }
    EXPECT_EQ(device.GetRecordedDeviceOperationsUVE().size(), 256U);
    EXPECT_EQ(device.GetDroppedDeviceOperationCountUVE(), 44U);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);
}
} // namespace
} // namespace UVE::Render::Tests
