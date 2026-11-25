// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "device_operations.h"
#include "render_surface.h"
#include "null_render_device.h"
#include <array>
#include <limits>
#include <memory>
#include <utility>
#include <variant>
#include <vector>
#include <gtest/gtest.h>
namespace UVE::Render::Tests {
namespace {
using Result = RenderOperationResult;
TEST(DeviceOperationsTest, NullCapabilitiesNeverClaimNativeTimingOrSurface) {
    NullRenderDevice device;
    const auto caps = device.GetCapabilities();
    EXPECT_EQ(caps.fences, RenderFeatureSupport::Model);
    EXPECT_EQ(caps.memoryBarriers, RenderFeatureSupport::Model);
    EXPECT_EQ(caps.bufferCopy, RenderFeatureSupport::Model);
    EXPECT_EQ(caps.textureBlit, RenderFeatureSupport::Model);
    EXPECT_EQ(caps.timestampQueries, RenderFeatureSupport::Model);
    EXPECT_EQ(caps.surface, RenderFeatureSupport::Unsupported);
    RenderSurface surface(device);
    RenderSurfaceInfo sentinel{42, 17, true, true, 9};
    EXPECT_EQ(surface.Describe(sentinel), Result::Unsupported);
    EXPECT_EQ(sentinel.width, 42U);
    EXPECT_EQ(surface.Present(), Result::Unsupported);
    EXPECT_EQ(device.GetPresentCallCount(), 0U);
}
TEST(DeviceOperationsTest, CopyChangesRealModelBytesAndRecordsDescriptor) {
    NullRenderDevice device;
    const std::array<std::byte, 8> bytes{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    const auto source = device.CreateBuffer({8, BufferUsage::Storage}, bytes);
    const auto target = device.CreateBuffer({8, BufferUsage::Storage});
    ASSERT_EQ(device.CopyBuffer({source, target, 1, 3, 3}), Result::Recorded);
    std::array<std::byte, 8> output{};
    ASSERT_TRUE(device.ReadbackBuffer(target, output));
    EXPECT_EQ(output[2], std::byte{0});
    EXPECT_EQ(output[3], std::byte{2});
    EXPECT_EQ(output[4], std::byte{3});
    EXPECT_EQ(output[5], std::byte{4});
    ASSERT_EQ(device.GetRecordedDeviceOperations().size(), 1U);
    const auto &copy = std::get<BufferCopyDesc>(device.GetRecordedDeviceOperations().front());
    EXPECT_EQ(copy.sourceOffset, 1U);
    EXPECT_EQ(copy.destinationOffset, 3U);
    EXPECT_EQ(copy.sizeBytes, 3U);
}
TEST(DeviceOperationsTest, InvalidCopyRangesAndOverlapAreFailureAtomic) {
    NullRenderDevice device;
    const std::array<std::byte, 8> bytes{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    const auto buffer = device.CreateBuffer({8, BufferUsage::Storage}, bytes);
    EXPECT_EQ(device.CopyBuffer({{}, buffer, 0, 0, 1}), Result::InvalidHandle);
    EXPECT_EQ(device.CopyBuffer({buffer, buffer, 0, 0, 0}), Result::InvalidArgument);
    EXPECT_EQ(device.CopyBuffer({buffer, buffer, 0, 1, 4}), Result::InvalidArgument);
    EXPECT_EQ(device.CopyBuffer({buffer, buffer, UINT64_MAX, 0, 1}), Result::InvalidArgument);
    EXPECT_EQ(device.CopyBuffer({buffer, buffer, 0, 7, 2}), Result::InvalidArgument);
    std::array<std::byte, 8> result{};
    ASSERT_TRUE(device.ReadbackBuffer(buffer, result));
    EXPECT_EQ(result, bytes);
    EXPECT_TRUE(device.GetRecordedDeviceOperations().empty());
    EXPECT_EQ(device.CopyBuffer({buffer, buffer, 0, 4, 4}), Result::Recorded);
    ASSERT_TRUE(device.ReadbackBuffer(buffer, result));
    EXPECT_EQ(result[7], std::byte{4});
}
TEST(DeviceOperationsTest, BlitValidatesMipLayerFormatAndRegionsBeforeRecording) {
    NullRenderDevice device;
    const auto source = device.CreateTexture({4, 4, TextureFormat::RGBA8Unorm});
    const auto target = device.CreateTexture({2, 2, TextureFormat::RGBA8Unorm});
    TextureBlitDesc blit{source, target, {0, 0, 0, 0, 4, 4}, {0, 0, 0, 0, 2, 2}, BlitFilter::Linear};
    ASSERT_EQ(device.BlitTexture(blit), Result::Recorded);
    const auto count = device.GetRecordedDeviceOperations().size();
    blit.sourceRegion.x = UINT32_MAX;
    EXPECT_EQ(device.BlitTexture(blit), Result::InvalidArgument);
    blit.sourceRegion.x = 0;
    blit.sourceRegion.layer = 1;
    EXPECT_EQ(device.BlitTexture(blit), Result::InvalidArgument);
    blit.sourceRegion.layer = 0;
    blit.sourceRegion.mip = 1;
    EXPECT_EQ(device.BlitTexture(blit), Result::InvalidArgument);
    blit.sourceRegion.mip = 0;
    blit.filter = static_cast<BlitFilter>(255);
    EXPECT_EQ(device.BlitTexture(blit), Result::InvalidArgument);
    EXPECT_EQ(device.GetRecordedDeviceOperations().size(), count);
    device.DestroyTexture(source);
    EXPECT_EQ(device.BlitTexture(blit), Result::InvalidHandle);
}
TEST(DeviceOperationsTest, BlitAndMipsAcceptEveryUncompressedColorFormatAndNothingElse) {
    // Format slice: the shared validators gate on "uncompressed colour", not on a two-format list,
    // so one- and two-channel and 32-bit float textures blit and build mip chains while depth
    // and block-compressed textures still cannot. Formats must still match between the two ends.
    NullRenderDevice device;
    for (const TextureFormat format : {TextureFormat::R8Unorm, TextureFormat::RG8Unorm,
                                          TextureFormat::R16Float, TextureFormat::RG16Float,
                                          TextureFormat::R32Float, TextureFormat::RG32Float,
                                          TextureFormat::RGBA32Float}) {
        // Mip chains must be uploaded in full (the existing contract), so size the data from
        // the shared block table: (16 + 4 + 1) texels of the format's byte size.
        const std::vector<std::byte> chain(static_cast<std::size_t>(21U * GetTextureFormatBlockInfo(format).bytes));
        const auto source = device.CreateTexture({4, 4, format, 3}, chain);
        const auto target = device.CreateTexture({2, 2, format});
        ASSERT_NE(source, kInvalidTextureHandle);
        ASSERT_NE(target, kInvalidTextureHandle);
        const TextureBlitDesc blit{source, target, {0, 0, 0, 0, 4, 4}, {0, 0, 0, 0, 2, 2}, BlitFilter::Linear};
        EXPECT_EQ(device.BlitTexture(blit), Result::Recorded) << static_cast<int>(format);
        EXPECT_EQ(device.GenerateMipmaps(source), Result::Recorded) << static_cast<int>(format);
        EXPECT_EQ(device.GenerateMipmaps(target), Result::InvalidArgument) << "single level";
        device.DestroyTexture(source);
        device.DestroyTexture(target);
    }
    const auto r8 = device.CreateTexture({2, 2, TextureFormat::R8Unorm});
    const auto rg8 = device.CreateTexture({2, 2, TextureFormat::RG8Unorm});
    EXPECT_EQ(device.BlitTexture({r8, rg8, {0, 0, 0, 0, 2, 2}, {0, 0, 0, 0, 2, 2}, BlitFilter::Point}),
              Result::InvalidArgument)
        << "format conversion stays rejected";
    const auto depth = device.CreateTexture({2, 2, TextureFormat::Depth32Float});
    ASSERT_NE(depth, kInvalidTextureHandle);
    EXPECT_EQ(device.GenerateMipmaps(depth), Result::InvalidArgument) << "depth never builds mips";
    EXPECT_EQ(device.BlitTexture({depth, depth, {0, 0, 0, 0, 2, 2}, {0, 0, 0, 0, 2, 2}, BlitFilter::Point}),
              Result::InvalidArgument);
}

TEST(DeviceOperationsTest, MipsRequireAllocatedColorLevelsAndDescriptorReadPreservesFailureOutput) {
    NullRenderDevice device;
    const std::vector<std::byte> pixels(84U);
    const auto texture = device.CreateTexture({4, 4, TextureFormat::RGBA8Unorm, 3}, pixels);
    ASSERT_NE(texture, kInvalidTextureHandle);
    EXPECT_EQ(device.GenerateMipmaps(texture), Result::Recorded);
    const auto single = device.CreateTexture({4, 4, TextureFormat::RGBA8Unorm});
    EXPECT_EQ(device.GenerateMipmaps(single), Result::InvalidArgument);
    EXPECT_EQ(device.GenerateMipmaps({}), Result::InvalidHandle);
    TextureDesc output{99, 99, TextureFormat::RGBA8Unorm};
    EXPECT_EQ(device.GetTextureDesc({}, output), Result::InvalidHandle);
    EXPECT_EQ(output.width, 99U);
    EXPECT_EQ(device.GetTextureDesc(texture, output), Result::Success);
    EXPECT_EQ(output.mipLevels, 3U);
}
TEST(DeviceOperationsTest, MemoryBarriersRecordOnlyValidOutsidePassDependencies) {
    NullRenderDevice device;
    auto commands = device.CreateCommandBuffer();
    EXPECT_EQ(commands->MemoryBarrier(static_cast<MemoryBarrierMask>(0)), Result::InvalidArgument);
    EXPECT_EQ(commands->MemoryBarrier(static_cast<MemoryBarrierMask>(64)), Result::InvalidArgument);
    commands->BeginRenderPass({});
    EXPECT_EQ(commands->MemoryBarrier(MemoryBarrierMask::All), Result::InvalidState);
    commands->EndRenderPass();
    EXPECT_EQ(commands->MemoryBarrier(MemoryBarrierMask::ShaderStorage |
                                         MemoryBarrierMask::IndirectCommands),
              Result::Recorded);
    device.Submit(std::move(commands));
    const auto &recorded = device.GetLastSubmittedCommands();
    ASSERT_EQ(recorded.size(), 3U);
    EXPECT_EQ(std::get<MemoryBarrierCommand>(recorded.back()).mask,
              MemoryBarrierMask::ShaderStorage | MemoryBarrierMask::IndirectCommands);
}
TEST(DeviceOperationsTest, FenceLifecycleIsExplicitlyAModelAndRejectsDestroyedHandles) {
    NullRenderDevice device;
    FenceHandle fence;
    ASSERT_EQ(device.InsertFence(fence), Result::Recorded);
    EXPECT_EQ(device.GetLiveResourceCount(), 1U);
    EXPECT_EQ(device.WaitFence(fence, 0), Result::Recorded);
    EXPECT_EQ(device.DestroyFence(fence), Result::Recorded);
    EXPECT_EQ(device.WaitFence(fence, UINT64_MAX), Result::InvalidHandle);
    EXPECT_EQ(device.DestroyFence(fence), Result::InvalidHandle);
    FenceHandle next;
    ASSERT_EQ(device.InsertFence(next), Result::Recorded);
    EXPECT_NE(next, fence);
    EXPECT_EQ(device.WaitIdle(), Result::Recorded);
}
TEST(DeviceOperationsTest, QueryStateMachineRejectsWrongKindsAndNestedQueries) {
    NullRenderDevice device;
    QueryHandle query, another, timestamp;
    ASSERT_EQ(device.CreateQuery(RenderQueryKind::AnySamplesPassed, query), Result::Recorded);
    ASSERT_EQ(device.CreateQuery(RenderQueryKind::AnySamplesPassed, another), Result::Recorded);
    ASSERT_EQ(device.CreateQuery(RenderQueryKind::Timestamp, timestamp), Result::Recorded);
    auto unchanged = query;
    EXPECT_EQ(device.CreateQuery(static_cast<RenderQueryKind>(255), unchanged),
              Result::InvalidArgument);
    EXPECT_EQ(unchanged, query);
    EXPECT_EQ(device.EndQuery(query), Result::InvalidState);
    EXPECT_EQ(device.BeginQuery(timestamp), Result::InvalidArgument);
    EXPECT_EQ(device.WriteTimestamp(query), Result::InvalidArgument);
    ASSERT_EQ(device.BeginQuery(query), Result::Recorded);
    EXPECT_EQ(device.BeginQuery(another), Result::InvalidState);
    EXPECT_EQ(device.EndQuery(another), Result::InvalidState);
    EXPECT_EQ(device.DestroyQuery(query), Result::InvalidState);
    ASSERT_EQ(device.EndQuery(query), Result::Recorded);
    EXPECT_EQ(device.DestroyQuery(query), Result::Recorded);
    EXPECT_EQ(device.BeginQuery(query), Result::InvalidHandle);
}
TEST(DeviceOperationsTest, NullNeverInventsGpuVisibilityOrNanoseconds) {
    NullRenderDevice device;
    QueryHandle handle;
    ASSERT_EQ(device.CreateQuery(RenderQueryKind::Timestamp, handle), Result::Recorded);
    RenderQueryResult output{RenderQueryKind::AnySamplesPassed, 42};
    EXPECT_EQ(device.GetQueryResult(handle, output), Result::NotReady);
    ASSERT_EQ(device.WriteTimestamp(handle), Result::Recorded);
    EXPECT_EQ(device.GetQueryResult(handle, output, true), Result::Unsupported);
    EXPECT_EQ(output.kind, RenderQueryKind::AnySamplesPassed);
    EXPECT_EQ(output.value, 42U);
    EXPECT_EQ(device.DestroyQuery(handle), Result::Recorded);
    EXPECT_EQ(device.GetQueryResult(handle, output), Result::InvalidHandle);
}
TEST(DeviceOperationsTest, DeviceDiagnosticHistoryIsBoundedAndReportsEviction) {
    NullRenderDevice device;
    for (int i = 0; i < 300; ++i) {
        FenceHandle handle;
        ASSERT_EQ(device.InsertFence(handle), Result::Recorded);
        ASSERT_EQ(device.DestroyFence(handle), Result::Recorded);
    }
    EXPECT_EQ(device.GetRecordedDeviceOperations().size(), 256U);
    EXPECT_EQ(device.GetDroppedDeviceOperationCount(), 44U);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);
}

// ---- Foundation WP3: recorded query brackets (ICommandBuffer) ------------------------
TEST(DeviceOperationsTest, NullCommandBufferRecordsQueryBracketsInOrder) {
    NullRenderDevice device;
    QueryHandle occlusion, timestamp;
    ASSERT_EQ(device.CreateQuery(RenderQueryKind::AnySamplesPassed, occlusion), Result::Recorded);
    ASSERT_EQ(device.CreateQuery(RenderQueryKind::Timestamp, timestamp), Result::Recorded);
    std::unique_ptr<ICommandBuffer> commands = device.CreateCommandBuffer();
    ASSERT_NE(commands, nullptr);
    EXPECT_EQ(commands->WriteTimestamp(timestamp), Result::Recorded);
    commands->BeginRenderPass(RenderPassDesc{});
    EXPECT_EQ(commands->BeginQuery(occlusion), Result::Recorded);
    commands->Draw(3U, 1U);
    EXPECT_EQ(commands->EndQuery(occlusion), Result::Recorded);
    commands->EndRenderPass();
    EXPECT_EQ(commands->WriteTimestamp(timestamp), Result::Recorded);
    device.Submit(std::move(commands));
    const std::vector<RecordedCommand>& recorded = device.GetLastSubmittedCommands();
    ASSERT_EQ(recorded.size(), 7U);
    EXPECT_TRUE(std::holds_alternative<WriteTimestampCommand>(recorded[0]));
    EXPECT_TRUE(std::holds_alternative<BeginRenderPassCommand>(recorded[1]));
    ASSERT_TRUE(std::holds_alternative<BeginQueryCommand>(recorded[2]));
    EXPECT_EQ(std::get<BeginQueryCommand>(recorded[2]).query, occlusion);
    EXPECT_TRUE(std::holds_alternative<DrawCommand>(recorded[3]));
    ASSERT_TRUE(std::holds_alternative<EndQueryCommand>(recorded[4]));
    EXPECT_EQ(std::get<EndQueryCommand>(recorded[4]).query, occlusion);
    EXPECT_TRUE(std::holds_alternative<EndRenderPassCommand>(recorded[5]));
    ASSERT_TRUE(std::holds_alternative<WriteTimestampCommand>(recorded[6]));
    EXPECT_EQ(std::get<WriteTimestampCommand>(recorded[6]).query, timestamp);
}

TEST(DeviceOperationsTest, NullCommandBufferRejectsBadQueryBrackets) {
    NullRenderDevice device;
    QueryHandle occlusion, second, timestamp;
    ASSERT_EQ(device.CreateQuery(RenderQueryKind::AnySamplesPassed, occlusion), Result::Recorded);
    ASSERT_EQ(device.CreateQuery(RenderQueryKind::AnySamplesPassed, second), Result::Recorded);
    ASSERT_EQ(device.CreateQuery(RenderQueryKind::Timestamp, timestamp), Result::Recorded);
    std::unique_ptr<ICommandBuffer> commands = device.CreateCommandBuffer();
    ASSERT_NE(commands, nullptr);
    // Handle and kind validation against the device's query table.
    EXPECT_EQ(commands->BeginQuery(kInvalidQueryHandle), Result::InvalidHandle);
    EXPECT_EQ(commands->BeginQuery(QueryHandle{999999U}), Result::InvalidHandle);
    EXPECT_EQ(commands->BeginQuery(timestamp), Result::InvalidArgument);
    EXPECT_EQ(commands->WriteTimestamp(occlusion), Result::InvalidArgument);
    EXPECT_EQ(commands->WriteTimestamp(QueryHandle{999999U}), Result::InvalidHandle);
    // End without begin, nesting, and ending the wrong query.
    EXPECT_EQ(commands->EndQuery(occlusion), Result::InvalidState);
    commands->BeginRenderPass(RenderPassDesc{});
    EXPECT_EQ(commands->BeginQuery(occlusion), Result::Recorded);
    EXPECT_EQ(commands->BeginQuery(second), Result::InvalidState);
    EXPECT_EQ(commands->EndQuery(second), Result::InvalidState);
    EXPECT_EQ(commands->EndQuery(occlusion), Result::Recorded);
    // A bracket may not span the pass boundary: the pass end closes it, the late end fails.
    EXPECT_EQ(commands->BeginQuery(second), Result::Recorded);
    commands->EndRenderPass();
    EXPECT_EQ(commands->EndQuery(second), Result::InvalidState);
    device.Submit(std::move(commands));
    const std::vector<RecordedCommand>& recorded = device.GetLastSubmittedCommands();
    // begin-pass, begin(occlusion), end(occlusion), begin(second), auto end(second), end-pass
    ASSERT_EQ(recorded.size(), 6U);
    ASSERT_TRUE(std::holds_alternative<EndQueryCommand>(recorded[4]));
    EXPECT_EQ(std::get<EndQueryCommand>(recorded[4]).query, second);
    EXPECT_TRUE(std::holds_alternative<EndRenderPassCommand>(recorded[5]));
}

TEST(DeviceOperationsTest, NullRecordedQueriesNeverInventResults) {
    NullRenderDevice device;
    QueryHandle occlusion, timestamp;
    ASSERT_EQ(device.CreateQuery(RenderQueryKind::AnySamplesPassed, occlusion), Result::Recorded);
    ASSERT_EQ(device.CreateQuery(RenderQueryKind::Timestamp, timestamp), Result::Recorded);
    std::unique_ptr<ICommandBuffer> commands = device.CreateCommandBuffer();
    ASSERT_NE(commands, nullptr);
    commands->BeginRenderPass(RenderPassDesc{});
    ASSERT_EQ(commands->BeginQuery(occlusion), Result::Recorded);
    commands->Draw(3U, 1U);
    ASSERT_EQ(commands->EndQuery(occlusion), Result::Recorded);
    commands->EndRenderPass();
    ASSERT_EQ(commands->WriteTimestamp(timestamp), Result::Recorded);
    device.Submit(std::move(commands));
    device.Present();
    RenderQueryResult untouched{};
    untouched.value = 4321U;
    EXPECT_EQ(device.GetQueryResult(occlusion, untouched, false), Result::NotReady);
    EXPECT_EQ(device.GetQueryResult(occlusion, untouched, true), Result::NotReady);
    EXPECT_EQ(device.GetQueryResult(timestamp, untouched, false), Result::NotReady);
    EXPECT_EQ(untouched.value, 4321U) << "a recorded bracket never produces a visibility or timing number";
    EXPECT_EQ(device.DestroyQuery(occlusion), Result::Recorded);
    EXPECT_EQ(device.DestroyQuery(timestamp), Result::Recorded);
}
} // namespace
} // namespace UVE::Render::Tests
