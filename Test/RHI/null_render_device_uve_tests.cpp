// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/rhi_null/null_render_device_uve.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <span>
#include <type_traits>
#include <thread>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "uve/logging/log_sink_uve.h"
#include "uve/logging/logger_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/platform/platform_uve.h"

namespace UVE::Render::Tests {
namespace {

TEST(NullRenderDeviceUVETest, CreateBufferUVE_UnknownUsage_ReturnsInvalidBeforeAllocation) {
    NullRenderDeviceUVE device;
    const BufferHandleUVE invalid =
        device.CreateBufferUVE(BufferDescUVE{16U, static_cast<BufferUsageUVE>(0xFFU)});

    EXPECT_EQ(invalid, kInvalidBufferHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);

    const BufferHandleUVE valid = device.CreateBufferUVE(BufferDescUVE{16U, BufferUsageUVE::Vertex});
    EXPECT_EQ(valid.value, 1U);
}

TEST(NullRenderDeviceUVETest, CreateBufferUVE_ZeroSize_ReturnsInvalidBeforeAllocation) {
    NullRenderDeviceUVE device;

    const BufferHandleUVE invalid = device.CreateBufferUVE(BufferDescUVE{0U, BufferUsageUVE::Vertex});

    EXPECT_EQ(invalid, kInvalidBufferHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);
}

TEST(NullRenderDeviceUVETest, CreateBufferUVE_OversizedInitialData_ReturnsInvalidBeforeAllocation) {
    NullRenderDeviceUVE device;
    const std::array<std::byte, 17> initialData{};
    const BufferHandleUVE invalid =
        device.CreateBufferUVE(BufferDescUVE{16U, BufferUsageUVE::Vertex}, initialData);

    EXPECT_EQ(invalid, kInvalidBufferHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);

    const BufferHandleUVE valid = device.CreateBufferUVE(BufferDescUVE{16U, BufferUsageUVE::Vertex});
    EXPECT_EQ(valid.value, 1U);
}

TEST(NullRenderDeviceUVETest, CreateBufferUVE_ReturnsUniqueHandles) {
    NullRenderDeviceUVE device;
    const BufferHandleUVE first = device.CreateBufferUVE(BufferDescUVE{16, BufferUsageUVE::Vertex});
    const BufferHandleUVE second = device.CreateBufferUVE(BufferDescUVE{16, BufferUsageUVE::Vertex});

    EXPECT_NE(first, kInvalidBufferHandleUVE);
    EXPECT_NE(second, kInvalidBufferHandleUVE);
    EXPECT_NE(first, second);
}

TEST(NullRenderDeviceUVETest, DestroyBufferUVE_UnknownHandle_LogsErrorSafely) {
    Debug::LoggerUVE logger;
    logger.Init(Debug::LogLevelUVE::Trace);
    auto memorySink = std::make_unique<Debug::MemorySinkUVE>();
    Debug::MemorySinkUVE* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    NullRenderDeviceUVE device;
    device.DestroyBufferUVE(BufferHandleUVE{999});

    const std::vector<Debug::LogMessageUVE> messages = memorySinkPtr->GetMessagesUVE();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessageUVE& message) {
            return message.level == Debug::LogLevelUVE::Error;
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
}

TEST(NullRenderDeviceUVETest, ReadbackBufferUVE_ReturnsWhatCreationAndUpdatesWrote) {
    // CS3: the null backend models host-visible buffer CONTENTS, not just descriptors - a
    // readback that could only ever return zeros would make headless assertions a lie.
    NullRenderDeviceUVE device;
    const std::array<std::byte, 4> initial{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    const BufferHandleUVE buffer =
        device.CreateBufferUVE(BufferDescUVE{8U, BufferUsageUVE::Storage}, initial);
    ASSERT_NE(buffer, kInvalidBufferHandleUVE);

    std::array<std::byte, 8> readback{};
    ASSERT_TRUE(device.ReadbackBufferUVE(buffer, readback, 0));
    EXPECT_EQ(readback[0], std::byte{1});
    EXPECT_EQ(readback[3], std::byte{4});
    // Bytes past the initial upload must read as the zero fill a fresh buffer has.
    EXPECT_EQ(readback[4], std::byte{0});
    EXPECT_EQ(readback[7], std::byte{0});

    const std::array<std::byte, 2> update{std::byte{9}, std::byte{9}};
    ASSERT_TRUE(device.UpdateBufferUVE(buffer, update, 6U));
    ASSERT_TRUE(device.ReadbackBufferUVE(buffer, readback, 0));
    EXPECT_EQ(readback[0], std::byte{1}); // untouched prefix survives an offset write
    EXPECT_EQ(readback[6], std::byte{9});
    EXPECT_EQ(readback[7], std::byte{9});

    // A partial read at an offset sees exactly that window.
    std::array<std::byte, 2> window{};
    ASSERT_TRUE(device.ReadbackBufferUVE(buffer, window, 2U));
    EXPECT_EQ(window[0], std::byte{3});
    EXPECT_EQ(window[1], std::byte{4});

    // An empty read is a successful no-op.
    EXPECT_TRUE(device.ReadbackBufferUVE(buffer, std::span<std::byte>{}, 0));

    device.DestroyBufferUVE(buffer);
}

TEST(NullRenderDeviceUVETest, ReadbackBufferUVE_UnknownHandleOrOutOfRange_ReturnsFalse) {
    NullRenderDeviceUVE device;
    std::array<std::byte, 4> readback{};
    EXPECT_FALSE(device.ReadbackBufferUVE(BufferHandleUVE{999}, readback, 0));

    const BufferHandleUVE buffer = device.CreateBufferUVE(BufferDescUVE{4U, BufferUsageUVE::Storage});
    ASSERT_NE(buffer, kInvalidBufferHandleUVE);
    std::array<std::byte, 8> tooLarge{};
    EXPECT_FALSE(device.ReadbackBufferUVE(buffer, tooLarge, 0));
    EXPECT_FALSE(device.ReadbackBufferUVE(buffer, readback, 2U));   // runs off the end
    EXPECT_FALSE(device.ReadbackBufferUVE(buffer, readback,
                                           std::numeric_limits<std::uint64_t>::max())); // overflowed offset
    device.DestroyBufferUVE(buffer);
}

TEST(NullRenderDeviceUVETest, UpdateBufferUVE_UnknownHandle_ReturnsFalseAndLogsError) {
    Debug::LoggerUVE logger;
    logger.Init(Debug::LogLevelUVE::Trace);
    auto memorySink = std::make_unique<Debug::MemorySinkUVE>();
    Debug::MemorySinkUVE* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    NullRenderDeviceUVE device;
    const std::array<std::byte, 4> data{};
    EXPECT_FALSE(device.UpdateBufferUVE(BufferHandleUVE{999}, data, 0));

    const std::vector<Debug::LogMessageUVE> messages = memorySinkPtr->GetMessagesUVE();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessageUVE& message) {
            return message.level == Debug::LogLevelUVE::Error;
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
}

TEST(NullRenderDeviceUVETest, UpdateBufferUVE_WriteWithinSize_ReturnsTrue) {
    NullRenderDeviceUVE device;
    const BufferHandleUVE buffer = device.CreateBufferUVE(BufferDescUVE{16, BufferUsageUVE::Uniform});
    const std::array<std::byte, 4> data{};

    EXPECT_TRUE(device.UpdateBufferUVE(buffer, data, 0));
    EXPECT_TRUE(device.UpdateBufferUVE(buffer, data, 12));
}

TEST(NullRenderDeviceUVETest, UpdateBufferUVE_OverflowedOffset_ReturnsFalse) {
    NullRenderDeviceUVE device;
    const BufferHandleUVE buffer = device.CreateBufferUVE(BufferDescUVE{16, BufferUsageUVE::Uniform});
    const std::array<std::byte, 4> data{};

    EXPECT_FALSE(device.UpdateBufferUVE(buffer, data, std::numeric_limits<std::uint64_t>::max()));
}

TEST(NullRenderDeviceUVETest, UpdateBufferUVE_WriteExceedingSize_ReturnsFalseAndLogsError) {
    Debug::LoggerUVE logger;
    logger.Init(Debug::LogLevelUVE::Trace);
    auto memorySink = std::make_unique<Debug::MemorySinkUVE>();
    Debug::MemorySinkUVE* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    NullRenderDeviceUVE device;
    const BufferHandleUVE buffer = device.CreateBufferUVE(BufferDescUVE{4, BufferUsageUVE::Uniform});
    const std::array<std::byte, 8> data{};
    EXPECT_FALSE(device.UpdateBufferUVE(buffer, data, 0));

    const std::vector<Debug::LogMessageUVE> messages = memorySinkPtr->GetMessagesUVE();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessageUVE& message) {
            return message.level == Debug::LogLevelUVE::Error;
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
}

TEST(NullRenderDeviceUVETest, CreateTextureUVE_ReturnsUniqueHandles) {
    NullRenderDeviceUVE device;
    const TextureHandleUVE first = device.CreateTextureUVE(TextureDescUVE{64, 64, TextureFormatUVE::RGBA8Unorm});
    const TextureHandleUVE second = device.CreateTextureUVE(TextureDescUVE{64, 64, TextureFormatUVE::RGBA8Unorm});

    EXPECT_NE(first, kInvalidTextureHandleUVE);
    EXPECT_NE(second, kInvalidTextureHandleUVE);
    EXPECT_NE(first, second);
}

TEST(NullRenderDeviceUVETest, CreateTextureUVE_InvalidDescriptorOrUpload_ReturnsInvalidBeforeAllocation) {
    NullRenderDeviceUVE device;
    EXPECT_EQ(TextureDescUVE{}.colorSpace, TextureColorSpaceUVE::Linear);
    const std::array<std::byte, 3> incompleteData{};
    const std::array<std::byte, 4> validData{};

    EXPECT_EQ(device.CreateTextureUVE(TextureDescUVE{0U, 1U, TextureFormatUVE::RGBA8Unorm, 1U}),
              kInvalidTextureHandleUVE);
    EXPECT_EQ(device.CreateTextureUVE(TextureDescUVE{1U, 1U, TextureFormatUVE::RGBA8Unorm, 1U}, incompleteData),
              kInvalidTextureHandleUVE);
    const TextureHandleUVE valid =
        device.CreateTextureUVE(TextureDescUVE{1U, 1U, TextureFormatUVE::RGBA8Unorm, 1U}, validData);
    EXPECT_EQ(valid.value, 1U);
    EXPECT_NE(valid, kInvalidTextureHandleUVE);

    const TextureHandleUVE srgb = device.CreateTextureUVE(
        TextureDescUVE{1U, 1U, TextureFormatUVE::RGBA8Unorm, 1U, TextureColorSpaceUVE::Srgb}, validData);
    EXPECT_NE(srgb, kInvalidTextureHandleUVE);
    EXPECT_EQ(device.CreateTextureUVE(
                  TextureDescUVE{1U, 1U, TextureFormatUVE::RGBA16Float, 1U, TextureColorSpaceUVE::Srgb}),
              kInvalidTextureHandleUVE);
    EXPECT_EQ(device.CreateTextureUVE(
                  TextureDescUVE{1U, 1U, TextureFormatUVE::RGBA8Unorm, 1U,
                                 static_cast<TextureColorSpaceUVE>(99U)}),
              kInvalidTextureHandleUVE);

    device.DestroyTextureUVE(srgb);
}

TEST(NullRenderDeviceUVETest, CreateTextureUVE_ValidatesAndRetainsCompleteMipChain) {
    NullRenderDeviceUVE device;
    const TextureDescUVE desc{2U, 2U, TextureFormatUVE::RGBA8Unorm, 2U};
    std::array<std::byte, 20U> pixels{}; // 2x2 level 0 (16 bytes) + 1x1 level 1 (4 bytes)
    std::uint64_t expectedBytes = 0U;
    ASSERT_TRUE(CalculateTextureUploadByteCountUVE(desc, expectedBytes));
    EXPECT_EQ(expectedBytes, pixels.size());
    EXPECT_EQ(MaximumTextureMipLevelCountUVE(desc.width, desc.height), 2U);
    EXPECT_EQ(GetTextureMipExtentUVE(desc.width, desc.height, 1U).width, 1U);
    EXPECT_EQ(GetTextureMipExtentUVE(desc.width, desc.height, 1U).height, 1U);
    EXPECT_TRUE(ValidateTextureUploadUVE(desc, pixels));
    EXPECT_FALSE(ValidateTextureUploadUVE(desc, std::span<const std::byte>(pixels.data(), pixels.size() - 1U)));
    EXPECT_FALSE(ValidateTextureUploadUVE(desc, {})) << "declared mip chains must provide every level";

    TextureDescUVE tooManyLevels = desc;
    tooManyLevels.mipLevels = 3U;
    EXPECT_FALSE(CalculateTextureUploadByteCountUVE(tooManyLevels, expectedBytes));
    EXPECT_FALSE(ValidateTextureUploadUVE(tooManyLevels, pixels));
    EXPECT_FALSE(ValidateTextureUploadUVE(
        TextureDescUVE{2U, 2U, TextureFormatUVE::Depth32Float, 2U}, pixels));

    const TextureHandleUVE texture = device.CreateTextureUVE(desc, pixels);
    ASSERT_NE(texture, kInvalidTextureHandleUVE);
    const std::vector<TextureDescUVE> liveDescs = device.GetLiveTextureDescsUVE();
    ASSERT_EQ(liveDescs.size(), 1U);
    EXPECT_EQ(liveDescs.front().mipLevels, 2U);
    device.DestroyTextureUVE(texture);
}

TEST(NullRenderDeviceUVETest, CompressedTextureUploadsUseBlockAwareMipSizesWithoutAdvertisingSupport) {
    NullRenderDeviceUVE device;
    const TextureDescUVE bc1Desc{7U, 5U, TextureFormatUVE::BC1RGB, 2U, TextureColorSpaceUVE::Srgb};
    std::uint64_t expectedBytes = 0U;
    ASSERT_TRUE(CalculateTextureUploadByteCountUVE(bc1Desc, expectedBytes));
    EXPECT_EQ(expectedBytes, 40U); // 4 base blocks + 1 3x2 mip block, 8 bytes each
    EXPECT_FALSE(ValidateTextureUploadUVE(bc1Desc, {})); // compressed textures are not render targets
    EXPECT_FALSE(device.SupportsTextureFormatUVE(TextureFormatUVE::BC1RGB, TextureColorSpaceUVE::Srgb));

    std::array<std::byte, 40U> bc1Blocks{};
    EXPECT_FALSE(ValidateTextureUploadUVE(
        bc1Desc, std::span<const std::byte>(bc1Blocks.data(), bc1Blocks.size() - 1U)));
    EXPECT_TRUE(ValidateTextureUploadUVE(bc1Desc, bc1Blocks));
    const TextureHandleUVE texture = device.CreateTextureUVE(bc1Desc, bc1Blocks);
    ASSERT_NE(texture, kInvalidTextureHandleUVE);
    device.DestroyTextureUVE(texture);

    const TextureDescUVE etc2AlphaDesc{7U, 5U, TextureFormatUVE::ETC2RGBA8, 2U};
    ASSERT_TRUE(CalculateTextureUploadByteCountUVE(etc2AlphaDesc, expectedBytes));
    EXPECT_EQ(expectedBytes, 80U); // 4 base blocks + 1 mip block, 16 bytes each
    EXPECT_FALSE(device.SupportsTextureFormatUVE(TextureFormatUVE::ETC2RGBA8));
    EXPECT_TRUE(device.SupportsTextureFormatUVE(TextureFormatUVE::RGBA8Unorm,
                                                TextureColorSpaceUVE::Srgb));
}

TEST(NullRenderDeviceUVETest, CreateShaderUVE_UnknownStage_ReturnsInvalidBeforeAllocation) {
    NullRenderDeviceUVE device;
    const ShaderHandleUVE invalid =
        device.CreateShaderUVE(ShaderDescUVE{static_cast<ShaderStageUVE>(0xFFU), "vs"});

    EXPECT_EQ(invalid, kInvalidShaderHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);

    const ShaderHandleUVE valid = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, "vs"});
    EXPECT_EQ(valid.value, 1U);
}

TEST(NullRenderDeviceUVETest, CreateShaderUVE_ReturnsUniqueHandles) {
    NullRenderDeviceUVE device;
    const ShaderHandleUVE first = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, "vs"});
    const ShaderHandleUVE second = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Fragment, "fs"});

    EXPECT_NE(first, kInvalidShaderHandleUVE);
    EXPECT_NE(second, kInvalidShaderHandleUVE);
    EXPECT_NE(first, second);
}

TEST(NullRenderDeviceUVETest, CreatePipelineUVE_WithLiveShaders_Succeeds) {
    NullRenderDeviceUVE device;
    const ShaderHandleUVE vertexShader = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, "vs"});
    const ShaderHandleUVE fragmentShader = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Fragment, "fs"});

    PipelineDescUVE desc;
    desc.vertexShader = vertexShader;
    desc.fragmentShader = fragmentShader;

    EXPECT_NE(device.CreatePipelineUVE(desc), kInvalidPipelineHandleUVE);
}

TEST(NullRenderDeviceUVETest, CreatePipelineUVE_UnknownShaderHandle_ReturnsInvalidAndLogsError) {
    Debug::LoggerUVE logger;
    logger.Init(Debug::LogLevelUVE::Trace);
    auto memorySink = std::make_unique<Debug::MemorySinkUVE>();
    Debug::MemorySinkUVE* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    NullRenderDeviceUVE device;
    PipelineDescUVE desc;
    desc.vertexShader = ShaderHandleUVE{999};
    desc.fragmentShader = ShaderHandleUVE{998};
    EXPECT_EQ(device.CreatePipelineUVE(desc), kInvalidPipelineHandleUVE);

    const std::vector<Debug::LogMessageUVE> messages = memorySinkPtr->GetMessagesUVE();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessageUVE& message) {
            return message.level == Debug::LogLevelUVE::Error;
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
}

TEST(NullRenderDeviceUVETest, GetLiveResourceCountUVE_TracksCreateAndDestroy) {
    NullRenderDeviceUVE device;
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);

    const BufferHandleUVE buffer = device.CreateBufferUVE(BufferDescUVE{16, BufferUsageUVE::Vertex});
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 1U);

    const TextureHandleUVE texture = device.CreateTextureUVE(TextureDescUVE{4, 4, TextureFormatUVE::RGBA8Unorm});
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 2U);

    device.DestroyBufferUVE(buffer);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 1U);

    device.DestroyTextureUVE(texture);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);
}

TEST(NullRenderDeviceUVETest, GetBackendNameUVE_ReturnsNull) {
    NullRenderDeviceUVE device;
    EXPECT_EQ(device.GetBackendNameUVE(), "Null");
}

TEST(NullRenderDeviceUVETest, CommandBufferRecordingThenSubmit_ProducesExpectedCommandSequenceInOrder) {
    NullRenderDeviceUVE device;
    const ShaderHandleUVE vertexShader = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, "vs"});
    const ShaderHandleUVE fragmentShader = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Fragment, "fs"});
    PipelineDescUVE pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    const PipelineHandleUVE pipeline = device.CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);

    const BufferHandleUVE vertexBuffer = device.CreateBufferUVE(BufferDescUVE{64, BufferUsageUVE::Vertex});
    const BufferHandleUVE indexBuffer = device.CreateBufferUVE(BufferDescUVE{12, BufferUsageUVE::Index});
    const TextureHandleUVE colorTarget = device.CreateTextureUVE(TextureDescUVE{64, 64, TextureFormatUVE::RGBA8Unorm});

    std::unique_ptr<ICommandBufferUVE> commandBuffer = device.CreateCommandBufferUVE();
    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = colorTarget;
    commandBuffer->BeginRenderPassUVE(passDesc);
    commandBuffer->BindPipelineUVE(pipeline);
    commandBuffer->BindVertexBufferUVE(vertexBuffer, 0);
    commandBuffer->BindIndexBufferUVE(indexBuffer);
    commandBuffer->DrawIndexedUVE(3, 1);
    commandBuffer->EndRenderPassUVE();

    device.SubmitUVE(std::move(commandBuffer));

    const std::vector<RecordedCommandUVE>& recorded = device.GetLastSubmittedCommandsUVE();
    ASSERT_EQ(recorded.size(), 6U);
    EXPECT_TRUE(std::holds_alternative<BeginRenderPassCommandUVE>(recorded[0]));
    EXPECT_TRUE(std::holds_alternative<BindPipelineCommandUVE>(recorded[1]));
    EXPECT_TRUE(std::holds_alternative<BindVertexBufferCommandUVE>(recorded[2]));
    EXPECT_TRUE(std::holds_alternative<BindIndexBufferCommandUVE>(recorded[3]));
    EXPECT_TRUE(std::holds_alternative<DrawIndexedCommandUVE>(recorded[4]));
    EXPECT_TRUE(std::holds_alternative<EndRenderPassCommandUVE>(recorded[5]));

    EXPECT_EQ(std::get<BeginRenderPassCommandUVE>(recorded[0]).desc.colorAttachment, colorTarget);
    EXPECT_EQ(std::get<BindPipelineCommandUVE>(recorded[1]).pipeline, pipeline);
    EXPECT_EQ(std::get<BindVertexBufferCommandUVE>(recorded[2]).buffer, vertexBuffer);
    EXPECT_EQ(std::get<BindIndexBufferCommandUVE>(recorded[3]).buffer, indexBuffer);
    EXPECT_EQ(std::get<DrawIndexedCommandUVE>(recorded[4]).indexCount, 3U);
    EXPECT_EQ(std::get<DrawIndexedCommandUVE>(recorded[4]).instanceCount, 1U);
}

// --- CS7: indirect indexed draw -------------------------------------------------------------

TEST(NullRenderDeviceUVETest, DrawIndexedIndirectCommandUVE_MatchesTheGpuParameterLayout) {
    // Both Vulkan's VkDrawIndexedIndirectCommand and GL's DrawElementsIndirectCommand are five
    // 32-bit words in this order. A compute shader writes them with an std430 uvec-ish layout, so
    // the CPU-side mirror has to agree field for field - a silent reorder here would produce
    // draws with nonsense counts that nothing on the CPU ever observes.
    static_assert(sizeof(DrawIndexedIndirectCommandUVE) == 20U);
    EXPECT_EQ(offsetof(DrawIndexedIndirectCommandUVE, indexCount), 0U);
    EXPECT_EQ(offsetof(DrawIndexedIndirectCommandUVE, instanceCount), 4U);
    EXPECT_EQ(offsetof(DrawIndexedIndirectCommandUVE, firstIndex), 8U);
    EXPECT_EQ(offsetof(DrawIndexedIndirectCommandUVE, vertexOffset), 12U);
    EXPECT_EQ(offsetof(DrawIndexedIndirectCommandUVE, firstInstance), 16U);
    // vertexOffset is the one signed field in both APIs; an unsigned mirror would turn a small
    // negative rebase into a ~4-billion vertex index.
    static_assert(std::is_signed_v<decltype(DrawIndexedIndirectCommandUVE::vertexOffset)>);
}

TEST(NullRenderDeviceUVETest, IndirectStorageUsage_IsValidAndStorageBindable) {
    EXPECT_TRUE(IsBufferUsageValidUVE(BufferUsageUVE::IndirectStorage));
    // The whole point of the usage: a compute shader must be able to bind it as an SSBO and
    // write the draw parameters, otherwise indirect draw buys nothing over DrawIndexedUVE.
    EXPECT_TRUE(IsStorageBindableUsageUVE(BufferUsageUVE::IndirectStorage));
    EXPECT_TRUE(IsStorageBindableUsageUVE(BufferUsageUVE::Storage));
    EXPECT_FALSE(IsStorageBindableUsageUVE(BufferUsageUVE::Vertex));
    EXPECT_FALSE(IsStorageBindableUsageUVE(BufferUsageUVE::Index));
    EXPECT_FALSE(IsStorageBindableUsageUVE(BufferUsageUVE::Uniform));
}

TEST(NullRenderDeviceUVETest, CreateBufferUVE_IndirectStorageUsage_Allocates) {
    NullRenderDeviceUVE device;
    const BufferHandleUVE indirect = device.CreateBufferUVE(
        BufferDescUVE{sizeof(DrawIndexedIndirectCommandUVE), BufferUsageUVE::IndirectStorage});
    EXPECT_NE(indirect, kInvalidBufferHandleUVE);
}

TEST(NullRenderDeviceUVETest, DrawIndexedIndirectUVE_InsideAPass_RecordsBufferAndOffset) {
    NullRenderDeviceUVE device;
    const TextureHandleUVE colorTarget =
        device.CreateTextureUVE(TextureDescUVE{16, 16, TextureFormatUVE::RGBA8Unorm});
    const BufferHandleUVE indirect = device.CreateBufferUVE(
        BufferDescUVE{2U * sizeof(DrawIndexedIndirectCommandUVE), BufferUsageUVE::IndirectStorage});

    std::unique_ptr<ICommandBufferUVE> commandBuffer = device.CreateCommandBufferUVE();
    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = colorTarget;
    commandBuffer->BeginRenderPassUVE(passDesc);
    // A non-zero offset is the interesting case: it is how a single buffer holds a batch of
    // draws, and it is the value most likely to be dropped on the way through the recorder.
    commandBuffer->DrawIndexedIndirectUVE(indirect, sizeof(DrawIndexedIndirectCommandUVE));
    commandBuffer->EndRenderPassUVE();
    device.SubmitUVE(std::move(commandBuffer));

    const std::vector<RecordedCommandUVE>& recorded = device.GetLastSubmittedCommandsUVE();
    ASSERT_EQ(recorded.size(), 3U);
    ASSERT_TRUE(std::holds_alternative<DrawIndexedIndirectCommandRecordUVE>(recorded[1]));
    const auto& record = std::get<DrawIndexedIndirectCommandRecordUVE>(recorded[1]);
    EXPECT_EQ(record.buffer, indirect);
    EXPECT_EQ(record.offsetBytes, sizeof(DrawIndexedIndirectCommandUVE));
}

TEST(NullRenderDeviceUVETest, GetLastSubmittedCommandsUVE_BeforeAnySubmit_IsEmpty) {
    NullRenderDeviceUVE device;
    EXPECT_TRUE(device.GetLastSubmittedCommandsUVE().empty());
}

TEST(NullRenderDeviceUVETest, CreateShaderUVE_OutInfoLogParameter_IsAcceptedAndUnused) {
    NullRenderDeviceUVE device;
    std::string infoLog = "unset";
    const ShaderHandleUVE handle = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, "vs"}, &infoLog);
    EXPECT_NE(handle, kInvalidShaderHandleUVE);
}

TEST(NullRenderDeviceUVETest, CreatePipelineUVE_OutInfoLogParameter_IsAcceptedAndUnused) {
    NullRenderDeviceUVE device;
    const ShaderHandleUVE vertexShader = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, "vs"});
    const ShaderHandleUVE fragmentShader = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Fragment, "fs"});
    PipelineDescUVE desc;
    desc.vertexShader = vertexShader;
    desc.fragmentShader = fragmentShader;

    std::string infoLog = "unset";
    EXPECT_NE(device.CreatePipelineUVE(desc, &infoLog), kInvalidPipelineHandleUVE);
}

TEST(NullRenderDeviceUVETest, CreatePipelineUVE_UnknownVertexFormat_ReturnsInvalidBeforePublication) {
    NullRenderDeviceUVE device;
    const ShaderHandleUVE vertexShader = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, "vs"});
    const ShaderHandleUVE fragmentShader = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Fragment, "fs"});
    PipelineDescUVE invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.vertexLayout = {VertexAttributeUVE{"POSITION", static_cast<VertexAttributeFormatUVE>(0xFFU), 0U}};

    EXPECT_EQ(device.CreatePipelineUVE(invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 2U);

    PipelineDescUVE validDesc = invalidDesc;
    validDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U}};
    const PipelineHandleUVE validPipeline = device.CreatePipelineUVE(validDesc);
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceUVETest, CreatePipelineFromBinaryUVE_UnknownVertexFormat_ReturnsInvalidBeforePublication) {
    NullRenderDeviceUVE device;
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDescUVE invalidDesc;
    invalidDesc.vertexLayout = {VertexAttributeUVE{"POSITION", static_cast<VertexAttributeFormatUVE>(0xFFU), 0U}};

    EXPECT_EQ(device.CreatePipelineFromBinaryUVE(binary, 0U, invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);

    const PipelineHandleUVE validPipeline = device.CreatePipelineFromBinaryUVE(binary, 0U, PipelineBinaryDescUVE{});
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceUVETest, CreatePipelineUVE_UnknownBlendMode_ReturnsInvalidBeforePublication) {
    NullRenderDeviceUVE device;
    const ShaderHandleUVE vertexShader = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, "vs"});
    const ShaderHandleUVE fragmentShader = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Fragment, "fs"});
    PipelineDescUVE invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.blendMode = static_cast<PipelineBlendModeUVE>(0xFFU);

    EXPECT_EQ(device.CreatePipelineUVE(invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 2U);

    invalidDesc.blendMode = PipelineBlendModeUVE::Opaque;
    const PipelineHandleUVE validPipeline = device.CreatePipelineUVE(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceUVETest, CreatePipelineUVE_LinesTopology_IsAccepted) {
    NullRenderDeviceUVE device;
    const ShaderHandleUVE vertexShader = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, "vs"});
    const ShaderHandleUVE fragmentShader = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Fragment, "fs"});
    PipelineDescUVE desc;
    desc.vertexShader = vertexShader;
    desc.fragmentShader = fragmentShader;
    desc.topology = PrimitiveTopologyUVE::Lines;

    const PipelineHandleUVE pipeline = device.CreatePipelineUVE(desc);
    EXPECT_NE(pipeline, kInvalidPipelineHandleUVE);

    EXPECT_TRUE(IsPrimitiveTopologyValidUVE(PrimitiveTopologyUVE::Triangles));
    EXPECT_TRUE(IsPrimitiveTopologyValidUVE(PrimitiveTopologyUVE::Lines));
    EXPECT_FALSE(IsPrimitiveTopologyValidUVE(static_cast<PrimitiveTopologyUVE>(0xFFU)));

    PipelineDescUVE invalidDesc = desc;
    invalidDesc.topology = static_cast<PrimitiveTopologyUVE>(0xFFU);
    EXPECT_EQ(device.CreatePipelineUVE(invalidDesc), kInvalidPipelineHandleUVE);
}

TEST(NullRenderDeviceUVETest, CreatePipelineFromBinaryUVE_UnknownBlendMode_ReturnsInvalidBeforePublication) {
    NullRenderDeviceUVE device;
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDescUVE invalidDesc;
    invalidDesc.blendMode = static_cast<PipelineBlendModeUVE>(0xFFU);

    EXPECT_EQ(device.CreatePipelineFromBinaryUVE(binary, 0U, invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);

    invalidDesc.blendMode = PipelineBlendModeUVE::Opaque;
    const PipelineHandleUVE validPipeline = device.CreatePipelineFromBinaryUVE(binary, 0U, invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceUVETest, CreatePipelineUVE_UnknownTopology_ReturnsInvalidBeforePublication) {
    NullRenderDeviceUVE device;
    const ShaderHandleUVE vertexShader = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, "vs"});
    const ShaderHandleUVE fragmentShader = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Fragment, "fs"});
    PipelineDescUVE invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.topology = static_cast<PrimitiveTopologyUVE>(0xFFU);

    EXPECT_EQ(device.CreatePipelineUVE(invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 2U);

    invalidDesc.topology = PrimitiveTopologyUVE::Triangles;
    const PipelineHandleUVE validPipeline = device.CreatePipelineUVE(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceUVETest, CreatePipelineFromBinaryUVE_UnknownTopology_ReturnsInvalidBeforePublication) {
    NullRenderDeviceUVE device;
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDescUVE invalidDesc;
    invalidDesc.topology = static_cast<PrimitiveTopologyUVE>(0xFFU);

    EXPECT_EQ(device.CreatePipelineFromBinaryUVE(binary, 0U, invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);

    invalidDesc.topology = PrimitiveTopologyUVE::Triangles;
    const PipelineHandleUVE validPipeline = device.CreatePipelineFromBinaryUVE(binary, 0U, invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceUVETest, GetPipelineUniformsUVE_AnyHandle_ReturnsEmpty) {
    NullRenderDeviceUVE device;
    EXPECT_TRUE(device.GetPipelineUniformsUVE(PipelineHandleUVE{1}).empty());
}

TEST(NullRenderDeviceUVETest, GetPipelineBinaryUVE_AnyHandle_ReturnsFalse) {
    NullRenderDeviceUVE device;
    std::vector<std::byte> outBinary;
    std::uint32_t outFormat = 0;
    EXPECT_FALSE(device.GetPipelineBinaryUVE(PipelineHandleUVE{1}, outBinary, outFormat));
    EXPECT_TRUE(outBinary.empty());
}

TEST(NullRenderDeviceUVETest, CreatePipelineFromBinaryUVE_AlwaysSucceeds) {
    NullRenderDeviceUVE device;
    const std::array<std::byte, 4> binary{};
    const PipelineHandleUVE pipeline = device.CreatePipelineFromBinaryUVE(binary, 0, PipelineBinaryDescUVE{});
    EXPECT_NE(pipeline, kInvalidPipelineHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 1U);

    device.DestroyPipelineUVE(pipeline);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);
}

TEST(NullRenderDeviceUVETest, CommandBuffer_SetUniformCalls_AreRecordedInOrderWithValues) {
    NullRenderDeviceUVE device;
    std::unique_ptr<ICommandBufferUVE> commandBuffer = device.CreateCommandBufferUVE();
    commandBuffer->BeginRenderPassUVE(RenderPassDescUVE{});
    commandBuffer->SetUniformFloatUVE("uFloat", 1.5F);
    commandBuffer->SetUniformIntUVE("uInt", 7);
    commandBuffer->SetUniformBoolUVE("uBool", true);
    commandBuffer->SetUniformVector3UVE("uVec3", Math::Vector3UVE{1.0F, 2.0F, 3.0F});
    commandBuffer->SetUniformMatrix4x4UVE("uMat4", Math::Matrix4x4UVE::IdentityUVE());
    commandBuffer->EndRenderPassUVE();
    device.SubmitUVE(std::move(commandBuffer));

    const std::vector<RecordedCommandUVE>& recorded = device.GetLastSubmittedCommandsUVE();
    ASSERT_EQ(recorded.size(), 7U);
    ASSERT_TRUE(std::holds_alternative<BeginRenderPassCommandUVE>(recorded[0]));
    ASSERT_TRUE(std::holds_alternative<SetUniformFloatCommandUVE>(recorded[1]));
    EXPECT_EQ(std::get<SetUniformFloatCommandUVE>(recorded[1]).name, "uFloat");
    EXPECT_FLOAT_EQ(std::get<SetUniformFloatCommandUVE>(recorded[1]).value, 1.5F);

    ASSERT_TRUE(std::holds_alternative<SetUniformIntCommandUVE>(recorded[2]));
    EXPECT_EQ(std::get<SetUniformIntCommandUVE>(recorded[2]).value, 7);

    ASSERT_TRUE(std::holds_alternative<SetUniformBoolCommandUVE>(recorded[3]));
    EXPECT_TRUE(std::get<SetUniformBoolCommandUVE>(recorded[3]).value);

    ASSERT_TRUE(std::holds_alternative<SetUniformVector3CommandUVE>(recorded[4]));
    EXPECT_EQ(std::get<SetUniformVector3CommandUVE>(recorded[4]).value.y, 2.0F);

    ASSERT_TRUE(std::holds_alternative<SetUniformMatrix4x4CommandUVE>(recorded[5]));
    ASSERT_TRUE(std::holds_alternative<EndRenderPassCommandUVE>(recorded[6]));
}


TEST(NullRenderDeviceUVETest, CommandBuffer_ParallelRecordAndSubmitIsSafeAndRetainsWholeLists) {
    // M4: Null honors the same threading contract as the Vulkan backend — N threads may each
    // create, record, and submit their OWN command buffers concurrently (SubmitUVE stores the
    // spy under a mutex). The spy keeps the LAST-submitted list — which thread that is, is
    // nondeterministic — so every thread records the same STRUCTURE with thread-tagged values:
    // whichever list lands last, it must be a complete, untorn Begin + 5 uniform sets + End
    // whose values all carry one thread's tag.
    NullRenderDeviceUVE device;
    std::array<bool, 4> submitted{};
    std::vector<std::thread> threads;
    threads.reserve(4U);
    for (std::int32_t worker = 0; worker < 4; ++worker) {
        threads.emplace_back([&device, &submitted, worker]() {
            std::unique_ptr<ICommandBufferUVE> commandBuffer = device.CreateCommandBufferUVE();
            if (commandBuffer == nullptr) {
                return;
            }
            commandBuffer->BeginRenderPassUVE(RenderPassDescUVE{});
            for (std::int32_t uniformIndex = 0; uniformIndex < 5; ++uniformIndex) {
                commandBuffer->SetUniformIntUVE("uInt", worker * 100 + uniformIndex);
            }
            commandBuffer->EndRenderPassUVE();
            device.SubmitUVE(std::move(commandBuffer));
            submitted[static_cast<std::size_t>(worker)] = true;
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    for (std::size_t worker = 0; worker < 4U; ++worker) {
        ASSERT_TRUE(submitted[worker]) << "worker " << worker << " failed to record+submit";
    }

    // All threads joined — the spy read is quiesced (the documented reader contract).
    const std::vector<RecordedCommandUVE>& recorded = device.GetLastSubmittedCommandsUVE();
    ASSERT_EQ(recorded.size(), 7U);
    EXPECT_TRUE(std::holds_alternative<BeginRenderPassCommandUVE>(recorded[0]));
    EXPECT_TRUE(std::holds_alternative<EndRenderPassCommandUVE>(recorded[6]));
    std::int32_t workerTag = -1;
    for (std::size_t index = 1; index <= 5; ++index) {
        ASSERT_TRUE(std::holds_alternative<SetUniformIntCommandUVE>(recorded[index]));
        const std::int32_t value = std::get<SetUniformIntCommandUVE>(recorded[index]).value;
        ASSERT_GE(value, 0);
        ASSERT_LT(value, 400);
        const std::int32_t tag = value / 100;
        if (workerTag < 0) {
            workerTag = tag;
        }
        EXPECT_EQ(tag, workerTag) << "the retained list must be ONE thread's whole recording, "
                                     "never an interleaved tear";
        EXPECT_EQ(value % 100, static_cast<std::int32_t>(index) - 1);
    }
}

TEST(NullRenderDeviceUVETest, CommandBuffer_BindStorageBufferUVE_IsRecordedWithBufferAndSlot) {
    // M2f: the Null spy must retain storage-buffer binds like every other bind family, so
    // RenderSystems-level tests can assert the recorded buffer/slot without a real GPU.
    NullRenderDeviceUVE device;
    std::unique_ptr<ICommandBufferUVE> commandBuffer = device.CreateCommandBufferUVE();
    commandBuffer->BeginRenderPassUVE(RenderPassDescUVE{});
    commandBuffer->BindStorageBufferUVE(BufferHandleUVE{7U}, 2U);
    commandBuffer->EndRenderPassUVE();
    device.SubmitUVE(std::move(commandBuffer));

    const std::vector<RecordedCommandUVE>& recorded = device.GetLastSubmittedCommandsUVE();
    ASSERT_EQ(recorded.size(), 3U);
    ASSERT_TRUE(std::holds_alternative<BindStorageBufferCommandUVE>(recorded[1]));
    EXPECT_EQ(std::get<BindStorageBufferCommandUVE>(recorded[1]).buffer, BufferHandleUVE{7U});
    EXPECT_EQ(std::get<BindStorageBufferCommandUVE>(recorded[1]).slot, 2U);
}

TEST(NullCommandBufferUVETest, BeginRenderPassUVE_UnknownLoadOp_DoesNotRecordOrEnterPass) {
    NullRenderDeviceUVE device;
    std::unique_ptr<ICommandBufferUVE> invalidCommandBuffer = device.CreateCommandBufferUVE();
    RenderPassDescUVE invalidDesc;
    invalidDesc.colorLoadOp = static_cast<LoadOpUVE>(0xFFU);
    invalidCommandBuffer->BeginRenderPassUVE(invalidDesc);
    device.SubmitUVE(std::move(invalidCommandBuffer));
    EXPECT_TRUE(device.GetLastSubmittedCommandsUVE().empty());

    std::unique_ptr<ICommandBufferUVE> validCommandBuffer = device.CreateCommandBufferUVE();
    validCommandBuffer->BeginRenderPassUVE(RenderPassDescUVE{});
    validCommandBuffer->EndRenderPassUVE();
    device.SubmitUVE(std::move(validCommandBuffer));
    EXPECT_EQ(device.GetLastSubmittedCommandsUVE().size(), 2U);
}

TEST(NullRenderDeviceUVETest, CreateComputePipelineUVE_BookkeepsAndValidatesStage) {
    // M5a: compute pipelines live in the SAME handle domain as graphics pipelines (one
    // DestroyPipelineUVE erases from either map) and count as live resources; the shader
    // handle must be live AND Compute-stage.
    NullRenderDeviceUVE device;
    const ShaderHandleUVE computeShader = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Compute, "cs"});
    ASSERT_NE(computeShader, kInvalidShaderHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 1U);

    ComputePipelineDescUVE pipelineDesc;
    pipelineDesc.computeShader = computeShader;
    const PipelineHandleUVE pipeline = device.CreateComputePipelineUVE(pipelineDesc);
    EXPECT_NE(pipeline, kInvalidPipelineHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 2U);

    device.DestroyPipelineUVE(pipeline);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 1U);

    ComputePipelineDescUVE unknownShaderDesc;
    unknownShaderDesc.computeShader = ShaderHandleUVE{999999U};
    EXPECT_EQ(device.CreateComputePipelineUVE(unknownShaderDesc), kInvalidPipelineHandleUVE);

    const ShaderHandleUVE vertexShader = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, "vs"});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ComputePipelineDescUVE wrongStageDesc;
    wrongStageDesc.computeShader = vertexShader;
    EXPECT_EQ(device.CreateComputePipelineUVE(wrongStageDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 2U); // both shaders live, no pipeline recorded

    device.DestroyShaderUVE(computeShader);
    device.DestroyShaderUVE(vertexShader);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);
}

TEST(NullRenderDeviceUVETest, DispatchUVE_RecordsOutsidePassWithGroupCounts) {
    // M5a recording contract: the compute flow (bind compute pipeline, bind its SSBO,
    // dispatch) is recorded entirely OUTSIDE render-pass markers, in call order, with the
    // exact group counts.
    NullRenderDeviceUVE device;
    const ShaderHandleUVE computeShader = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Compute, "cs"});
    ComputePipelineDescUVE pipelineDesc;
    pipelineDesc.computeShader = computeShader;
    const PipelineHandleUVE computePipeline = device.CreateComputePipelineUVE(pipelineDesc);
    ASSERT_NE(computePipeline, kInvalidPipelineHandleUVE);
    const BufferHandleUVE storageBuffer = device.CreateBufferUVE(BufferDescUVE{64, BufferUsageUVE::Storage});
    ASSERT_NE(storageBuffer, kInvalidBufferHandleUVE);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = device.CreateCommandBufferUVE();
    commandBuffer->BindPipelineUVE(computePipeline);
    commandBuffer->BindStorageBufferUVE(storageBuffer, 0U);
    commandBuffer->DispatchUVE(4U, 2U, 3U);
    device.SubmitUVE(std::move(commandBuffer));

    const std::vector<RecordedCommandUVE>& recorded = device.GetLastSubmittedCommandsUVE();
    ASSERT_EQ(recorded.size(), 3U);
    EXPECT_TRUE(std::holds_alternative<BindPipelineCommandUVE>(recorded[0]));
    EXPECT_TRUE(std::holds_alternative<BindStorageBufferCommandUVE>(recorded[1]));
    EXPECT_TRUE(std::holds_alternative<DispatchCommandUVE>(recorded[2]));
    EXPECT_EQ(std::get<BindPipelineCommandUVE>(recorded[0]).pipeline, computePipeline);
    const DispatchCommandUVE& dispatch = std::get<DispatchCommandUVE>(recorded[2]);
    EXPECT_EQ(dispatch.groupCountX, 4U);
    EXPECT_EQ(dispatch.groupCountY, 2U);
    EXPECT_EQ(dispatch.groupCountZ, 3U);

    device.DestroyBufferUVE(storageBuffer);
    device.DestroyPipelineUVE(computePipeline);
    device.DestroyShaderUVE(computeShader);
}

TEST(NullRenderDeviceUVETest, DispatchUVE_RecordsOutsidePassWithStorageTextureAndGroupCounts) {
    // M5b: BindTextureUVE feeds STORAGE_IMAGE descriptors outside pass markers as well.
    NullRenderDeviceUVE device;
    const ShaderHandleUVE computeShader =
        device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Compute, "compute"});
    ASSERT_NE(computeShader, kInvalidShaderHandleUVE);
    ComputePipelineDescUVE pipelineDesc{};
    pipelineDesc.computeShader = computeShader;
    const PipelineHandleUVE computePipeline = device.CreateComputePipelineUVE(pipelineDesc);
    ASSERT_NE(computePipeline, kInvalidPipelineHandleUVE);
    const TextureHandleUVE storageTexture =
        device.CreateTextureUVE(TextureDescUVE{4U, 4U, TextureFormatUVE::RGBA8Unorm, 1U});
    ASSERT_NE(storageTexture, kInvalidTextureHandleUVE);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = device.CreateCommandBufferUVE();
    commandBuffer->BindPipelineUVE(computePipeline);
    commandBuffer->BindTextureUVE(storageTexture, 0U);
    commandBuffer->DispatchUVE(2U, 2U, 1U);
    device.SubmitUVE(std::move(commandBuffer));

    const std::vector<RecordedCommandUVE>& recorded = device.GetLastSubmittedCommandsUVE();
    ASSERT_EQ(recorded.size(), 3U);
    EXPECT_TRUE(std::holds_alternative<BindPipelineCommandUVE>(recorded[0]));
    EXPECT_TRUE(std::holds_alternative<BindTextureCommandUVE>(recorded[1]));
    EXPECT_TRUE(std::holds_alternative<DispatchCommandUVE>(recorded[2]));
    EXPECT_EQ(std::get<BindPipelineCommandUVE>(recorded[0]).pipeline, computePipeline);
    EXPECT_EQ(std::get<BindTextureCommandUVE>(recorded[1]).texture, storageTexture);
    const DispatchCommandUVE& dispatch = std::get<DispatchCommandUVE>(recorded[2]);
    EXPECT_EQ(dispatch.groupCountX, 2U);
    EXPECT_EQ(dispatch.groupCountY, 2U);
    EXPECT_EQ(dispatch.groupCountZ, 1U);

    device.DestroyTextureUVE(storageTexture);
    device.DestroyPipelineUVE(computePipeline);
    device.DestroyShaderUVE(computeShader);
}

TEST(NullRenderDeviceUVETest, CreatePipelineUVE_UnknownCullMode_ReturnsInvalidBeforePublication) {
    NullRenderDeviceUVE device;
    const ShaderHandleUVE vertexShader = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, "vs"});
    const ShaderHandleUVE fragmentShader = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Fragment, "fs"});
    PipelineDescUVE invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.cullMode = static_cast<CullModeUVE>(0xFFU);

    EXPECT_EQ(device.CreatePipelineUVE(invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 2U);

    invalidDesc.cullMode = CullModeUVE::Back;
    const PipelineHandleUVE validPipeline = device.CreatePipelineUVE(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceUVETest, CreatePipelineFromBinaryUVE_UnknownCullMode_ReturnsInvalidBeforePublication) {
    NullRenderDeviceUVE device;
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDescUVE invalidDesc;
    invalidDesc.cullMode = static_cast<CullModeUVE>(0xFFU);

    EXPECT_EQ(device.CreatePipelineFromBinaryUVE(binary, 0U, invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);

    invalidDesc.cullMode = CullModeUVE::Back;
    const PipelineHandleUVE validPipeline = device.CreatePipelineFromBinaryUVE(binary, 0U, invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceUVETest, CreatePipelineUVE_UnknownFrontFace_ReturnsInvalidBeforePublication) {
    NullRenderDeviceUVE device;
    const ShaderHandleUVE vertexShader = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, "vs"});
    const ShaderHandleUVE fragmentShader = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Fragment, "fs"});
    PipelineDescUVE invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.frontFace = static_cast<FrontFaceUVE>(0xFFU);

    EXPECT_EQ(device.CreatePipelineUVE(invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 2U);

    invalidDesc.frontFace = FrontFaceUVE::Clockwise;
    const PipelineHandleUVE validPipeline = device.CreatePipelineUVE(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceUVETest, CreatePipelineFromBinaryUVE_UnknownFrontFace_ReturnsInvalidBeforePublication) {
    NullRenderDeviceUVE device;
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDescUVE invalidDesc;
    invalidDesc.frontFace = static_cast<FrontFaceUVE>(0xFFU);

    EXPECT_EQ(device.CreatePipelineFromBinaryUVE(binary, 0U, invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);

    invalidDesc.frontFace = FrontFaceUVE::Clockwise;
    const PipelineHandleUVE validPipeline = device.CreatePipelineFromBinaryUVE(binary, 0U, invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceUVETest, CreatePipelineUVE_UnknownFillMode_ReturnsInvalidBeforePublication) {
    NullRenderDeviceUVE device;
    const ShaderHandleUVE vertexShader = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, "vs"});
    const ShaderHandleUVE fragmentShader = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Fragment, "fs"});
    PipelineDescUVE invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.fillMode = static_cast<FillModeUVE>(0xFFU);

    EXPECT_EQ(device.CreatePipelineUVE(invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 2U);

    invalidDesc.fillMode = FillModeUVE::Wireframe;
    const PipelineHandleUVE validPipeline = device.CreatePipelineUVE(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceUVETest, CreatePipelineFromBinaryUVE_UnknownFillMode_ReturnsInvalidBeforePublication) {
    NullRenderDeviceUVE device;
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDescUVE invalidDesc;
    invalidDesc.fillMode = static_cast<FillModeUVE>(0xFFU);

    EXPECT_EQ(device.CreatePipelineFromBinaryUVE(binary, 0U, invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);

    invalidDesc.fillMode = FillModeUVE::Wireframe;
    const PipelineHandleUVE validPipeline = device.CreatePipelineFromBinaryUVE(binary, 0U, invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceUVETest, CreatePipelineUVE_UnknownDepthCompare_ReturnsInvalidBeforePublication) {
    NullRenderDeviceUVE device;
    const ShaderHandleUVE vertexShader = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, "vs"});
    const ShaderHandleUVE fragmentShader = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Fragment, "fs"});
    PipelineDescUVE invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.depthCompare = static_cast<DepthCompareUVE>(0xFFU);

    EXPECT_EQ(device.CreatePipelineUVE(invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 2U);

    invalidDesc.depthCompare = DepthCompareUVE::LessOrEqual;
    const PipelineHandleUVE validPipeline = device.CreatePipelineUVE(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceUVETest, CreatePipelineFromBinaryUVE_UnknownDepthCompare_ReturnsInvalidBeforePublication) {
    NullRenderDeviceUVE device;
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDescUVE invalidDesc;
    invalidDesc.depthCompare = static_cast<DepthCompareUVE>(0xFFU);

    EXPECT_EQ(device.CreatePipelineFromBinaryUVE(binary, 0U, invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);

    invalidDesc.depthCompare = DepthCompareUVE::LessOrEqual;
    const PipelineHandleUVE validPipeline = device.CreatePipelineFromBinaryUVE(binary, 0U, invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceUVETest, PipelineDescs_DefaultRasterizerState_MatchesPreTier21Behavior) {
    const PipelineDescUVE desc;
    EXPECT_EQ(desc.cullMode, CullModeUVE::None);
    EXPECT_EQ(desc.frontFace, FrontFaceUVE::CounterClockwise);
    EXPECT_EQ(desc.fillMode, FillModeUVE::Fill);
    EXPECT_FALSE(desc.depthBiasEnabled);
    EXPECT_FLOAT_EQ(desc.depthBiasConstantFactor, 0.0F);
    EXPECT_FLOAT_EQ(desc.depthBiasSlopeFactor, 0.0F);
    EXPECT_EQ(desc.depthCompare, DepthCompareUVE::Less);

    const PipelineBinaryDescUVE binaryDesc;
    EXPECT_EQ(binaryDesc.cullMode, CullModeUVE::None);
    EXPECT_EQ(binaryDesc.frontFace, FrontFaceUVE::CounterClockwise);
    EXPECT_EQ(binaryDesc.fillMode, FillModeUVE::Fill);
    EXPECT_FALSE(binaryDesc.depthBiasEnabled);
    EXPECT_FLOAT_EQ(binaryDesc.depthBiasConstantFactor, 0.0F);
    EXPECT_FLOAT_EQ(binaryDesc.depthBiasSlopeFactor, 0.0F);
    EXPECT_EQ(binaryDesc.depthCompare, DepthCompareUVE::Less);
}

TEST(NullRenderDeviceUVETest, SamplerDescUVE_Defaults_MatchPreTier22Behavior) {
    const SamplerDescUVE desc;
    EXPECT_EQ(desc.magFilter, SamplerFilterUVE::Linear);
    EXPECT_EQ(desc.minFilter, SamplerFilterUVE::Linear);
    EXPECT_EQ(desc.mipMode, SamplerMipModeUVE::Linear);
    EXPECT_EQ(desc.wrapU, SamplerWrapUVE::ClampToEdge);
    EXPECT_EQ(desc.wrapV, SamplerWrapUVE::ClampToEdge);
    EXPECT_EQ(desc.wrapW, SamplerWrapUVE::ClampToEdge);
    EXPECT_FLOAT_EQ(desc.maxAnisotropy, 1.0F);
    EXPECT_TRUE(IsSamplerDescValidUVE(desc));
}

TEST(NullRenderDeviceUVETest, CreateSamplerUVE_InvalidMagFilter_ReturnsInvalid) {
    NullRenderDeviceUVE device;
    SamplerDescUVE desc;
    desc.magFilter = static_cast<SamplerFilterUVE>(0xFFU);
    EXPECT_EQ(device.CreateSamplerUVE(desc), kInvalidSamplerHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);
}

TEST(NullRenderDeviceUVETest, CreateSamplerUVE_InvalidMinFilter_ReturnsInvalid) {
    NullRenderDeviceUVE device;
    SamplerDescUVE desc;
    desc.minFilter = static_cast<SamplerFilterUVE>(0xFFU);
    EXPECT_EQ(device.CreateSamplerUVE(desc), kInvalidSamplerHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);
}

TEST(NullRenderDeviceUVETest, CreateSamplerUVE_InvalidMipMode_ReturnsInvalid) {
    NullRenderDeviceUVE device;
    SamplerDescUVE desc;
    desc.mipMode = static_cast<SamplerMipModeUVE>(0xFFU);
    EXPECT_EQ(device.CreateSamplerUVE(desc), kInvalidSamplerHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);
}

TEST(NullRenderDeviceUVETest, CreateSamplerUVE_InvalidWrap_ReturnsInvalid) {
    NullRenderDeviceUVE device;
    SamplerDescUVE desc;
    desc.wrapV = static_cast<SamplerWrapUVE>(0xFFU);
    EXPECT_EQ(device.CreateSamplerUVE(desc), kInvalidSamplerHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);
}

TEST(NullRenderDeviceUVETest, CreateSamplerUVE_BadAnisotropy_ReturnsInvalid) {
    NullRenderDeviceUVE device;
    SamplerDescUVE desc;
    desc.maxAnisotropy = 0.5F;
    EXPECT_EQ(device.CreateSamplerUVE(desc), kInvalidSamplerHandleUVE);
    desc.maxAnisotropy = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(device.CreateSamplerUVE(desc), kInvalidSamplerHandleUVE);
    desc.maxAnisotropy = std::numeric_limits<float>::infinity();
    EXPECT_EQ(device.CreateSamplerUVE(desc), kInvalidSamplerHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);

    // Null has no device limit — any finite >= 1.0 is accepted (real backends clamp).
    desc.maxAnisotropy = 1.0e30F;
    EXPECT_NE(device.CreateSamplerUVE(desc), kInvalidSamplerHandleUVE);
}

TEST(NullRenderDeviceUVETest, CreateSamplerUVE_NonDefaults_RoundTripThroughLiveDescs) {
    NullRenderDeviceUVE device;
    SamplerDescUVE desc;
    desc.magFilter = SamplerFilterUVE::Point;
    desc.minFilter = SamplerFilterUVE::Point;
    desc.mipMode = SamplerMipModeUVE::None;
    desc.wrapU = SamplerWrapUVE::Repeat;
    desc.wrapV = SamplerWrapUVE::MirroredRepeat;
    desc.maxAnisotropy = 4.0F;
    const SamplerHandleUVE sampler = device.CreateSamplerUVE(desc);
    ASSERT_NE(sampler, kInvalidSamplerHandleUVE);

    const std::vector<SamplerDescUVE> liveDescs = device.GetLiveSamplerDescsUVE();
    ASSERT_EQ(liveDescs.size(), 1U);
    EXPECT_EQ(liveDescs[0].magFilter, SamplerFilterUVE::Point);
    EXPECT_EQ(liveDescs[0].minFilter, SamplerFilterUVE::Point);
    EXPECT_EQ(liveDescs[0].mipMode, SamplerMipModeUVE::None);
    EXPECT_EQ(liveDescs[0].wrapU, SamplerWrapUVE::Repeat);
    EXPECT_EQ(liveDescs[0].wrapV, SamplerWrapUVE::MirroredRepeat);
    EXPECT_FLOAT_EQ(liveDescs[0].maxAnisotropy, 4.0F);
}

TEST(NullRenderDeviceUVETest, BindSamplerUVE_RecordedWithSlotOnSubmit) {
    NullRenderDeviceUVE device;
    const SamplerHandleUVE sampler = device.CreateSamplerUVE(SamplerDescUVE{});
    ASSERT_NE(sampler, kInvalidSamplerHandleUVE);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = device.CreateCommandBufferUVE();
    commandBuffer->BeginRenderPassUVE(RenderPassDescUVE{});
    commandBuffer->BindSamplerUVE(sampler, 3U);
    commandBuffer->EndRenderPassUVE();
    device.SubmitUVE(std::move(commandBuffer));

    const std::vector<RecordedCommandUVE>& recorded = device.GetLastSubmittedCommandsUVE();
    ASSERT_EQ(recorded.size(), 3U);
    const auto* bind = std::get_if<BindSamplerCommandUVE>(&recorded[1]);
    ASSERT_NE(bind, nullptr);
    EXPECT_EQ(bind->sampler, sampler);
    EXPECT_EQ(bind->slot, 3U);
}

TEST(NullRenderDeviceUVETest, CreateSamplerUVE_CountsTowardLiveResourcesUntilDestroyed) {
    NullRenderDeviceUVE device;
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);
    const SamplerHandleUVE sampler = device.CreateSamplerUVE(SamplerDescUVE{});
    ASSERT_NE(sampler, kInvalidSamplerHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 1U);
    EXPECT_TRUE(device.GetLiveSamplerDescsUVE().empty() == false);
    device.DestroySamplerUVE(sampler);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);
    EXPECT_TRUE(device.GetLiveSamplerDescsUVE().empty());
}

TEST(NullRenderDeviceUVETest, DestroySamplerUVE_UnknownHandle_IsSafeNoOp) {
    NullRenderDeviceUVE device;
    const SamplerHandleUVE sampler = device.CreateSamplerUVE(SamplerDescUVE{});
    ASSERT_NE(sampler, kInvalidSamplerHandleUVE);
    device.DestroySamplerUVE(sampler);
    device.DestroySamplerUVE(sampler); // double-destroy: logged, no crash
    device.DestroySamplerUVE(kInvalidSamplerHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);
}


// Tier 2.3: array layers + cubemaps. The Null backend shares the RHI validators with every
// backend, so these pin the dimensional contract (defaults, byte math, upload sizes, handle
// retention, pass-layer recording) without needing a GPU.
TEST(NullRenderDeviceUVETest, TextureTypeUVE_DefaultsTo2DWithOneLayer) {
    const TextureDescUVE desc;
    EXPECT_EQ(desc.type, TextureTypeUVE::Texture2D);
    EXPECT_EQ(desc.arrayLayers, 1U);
    EXPECT_TRUE(IsTextureTypeValidUVE(TextureTypeUVE::Texture2D));
    EXPECT_TRUE(IsTextureTypeValidUVE(TextureTypeUVE::Texture2DArray));
    EXPECT_TRUE(IsTextureTypeValidUVE(TextureTypeUVE::Cubemap));
    EXPECT_FALSE(IsTextureTypeValidUVE(static_cast<TextureTypeUVE>(99U)));
}

TEST(NullRenderDeviceUVETest, CalculateTextureUploadByteCountUVE_MultipliesByLayerCount) {
    // 2x2 RGBA8, two levels: 16 + 4 = 20 bytes per layer; x4 layers = 80.
    const TextureDescUVE arrayDesc{2U, 2U, TextureFormatUVE::RGBA8Unorm, 2U,
                                   TextureColorSpaceUVE::Linear, TextureTypeUVE::Texture2DArray, 4U};
    std::uint64_t expectedBytes = 0U;
    ASSERT_TRUE(CalculateTextureUploadByteCountUVE(arrayDesc, expectedBytes));
    EXPECT_EQ(expectedBytes, 80U);

    // 4x4 RGBA8 cube, one level: 64 bytes per face; x6 faces = 384.
    const TextureDescUVE cubeDesc{4U, 4U, TextureFormatUVE::RGBA8Unorm, 1U,
                                  TextureColorSpaceUVE::Linear, TextureTypeUVE::Cubemap, 6U};
    ASSERT_TRUE(CalculateTextureUploadByteCountUVE(cubeDesc, expectedBytes));
    EXPECT_EQ(expectedBytes, 384U);
}

TEST(NullRenderDeviceUVETest, CalculateTextureUploadByteCountUVE_RejectsBadDimensionality) {
    std::uint64_t expectedBytes = 0U;
    // 2D textures carry exactly one layer.
    EXPECT_FALSE(CalculateTextureUploadByteCountUVE(
        TextureDescUVE{4U, 4U, TextureFormatUVE::RGBA8Unorm, 1U, TextureColorSpaceUVE::Linear,
                       TextureTypeUVE::Texture2D, 2U},
        expectedBytes));
    EXPECT_FALSE(CalculateTextureUploadByteCountUVE(
        TextureDescUVE{4U, 4U, TextureFormatUVE::RGBA8Unorm, 1U, TextureColorSpaceUVE::Linear,
                       TextureTypeUVE::Texture2D, 0U},
        expectedBytes));
    // Cubes are square with exactly six layers.
    EXPECT_FALSE(CalculateTextureUploadByteCountUVE(
        TextureDescUVE{4U, 8U, TextureFormatUVE::RGBA8Unorm, 1U, TextureColorSpaceUVE::Linear,
                       TextureTypeUVE::Cubemap, 6U},
        expectedBytes));
    EXPECT_FALSE(CalculateTextureUploadByteCountUVE(
        TextureDescUVE{4U, 4U, TextureFormatUVE::RGBA8Unorm, 1U, TextureColorSpaceUVE::Linear,
                       TextureTypeUVE::Cubemap, 5U},
        expectedBytes));
    // Arrays need at least one layer.
    EXPECT_FALSE(CalculateTextureUploadByteCountUVE(
        TextureDescUVE{4U, 4U, TextureFormatUVE::RGBA8Unorm, 1U, TextureColorSpaceUVE::Linear,
                       TextureTypeUVE::Texture2DArray, 0U},
        expectedBytes));
    // Unknown dimensionality never computes a size.
    EXPECT_FALSE(CalculateTextureUploadByteCountUVE(
        TextureDescUVE{4U, 4U, TextureFormatUVE::RGBA8Unorm, 1U, TextureColorSpaceUVE::Linear,
                       static_cast<TextureTypeUVE>(99U), 1U},
        expectedBytes));
}

TEST(NullRenderDeviceUVETest, ValidateTextureUploadUVE_ArrayRequiresEveryLayer) {
    const TextureDescUVE arrayDesc{4U, 4U, TextureFormatUVE::RGBA8Unorm, 1U,
                                   TextureColorSpaceUVE::Linear, TextureTypeUVE::Texture2DArray, 3U};
    std::array<std::byte, 192U> allLayers{}; // 64 bytes x 3 layers, level-major
    EXPECT_TRUE(ValidateTextureUploadUVE(arrayDesc, allLayers));
    EXPECT_FALSE(ValidateTextureUploadUVE(
        arrayDesc, std::span<const std::byte>(allLayers.data(), 64U))) << "one layer is short";
    EXPECT_FALSE(ValidateTextureUploadUVE(
        arrayDesc, std::span<const std::byte>(allLayers.data(), 128U))) << "two layers are short";
    // Render-target arrays (single-level, uncompressed) still allow a data-free creation.
    EXPECT_TRUE(ValidateTextureUploadUVE(arrayDesc, {}));

    const TextureDescUVE cubeDesc{4U, 4U, TextureFormatUVE::RGBA8Unorm, 1U,
                                  TextureColorSpaceUVE::Linear, TextureTypeUVE::Cubemap, 6U};
    std::array<std::byte, 384U> allFaces{};
    EXPECT_TRUE(ValidateTextureUploadUVE(cubeDesc, allFaces));
    EXPECT_FALSE(ValidateTextureUploadUVE(
        cubeDesc, std::span<const std::byte>(allFaces.data(), 320U))) << "five faces are short";
    EXPECT_TRUE(ValidateTextureUploadUVE(cubeDesc, {}));
}

TEST(NullRenderDeviceUVETest, CreateTextureUVE_ArrayAndCube_RoundTripThroughLiveDescs) {
    NullRenderDeviceUVE device;
    const TextureDescUVE arrayDesc{4U, 4U, TextureFormatUVE::RGBA8Unorm, 1U,
                                   TextureColorSpaceUVE::Linear, TextureTypeUVE::Texture2DArray, 3U};
    const TextureHandleUVE array = device.CreateTextureUVE(arrayDesc);
    ASSERT_NE(array, kInvalidTextureHandleUVE);
    const TextureDescUVE cubeDesc{4U, 4U, TextureFormatUVE::RGBA8Unorm, 1U,
                                  TextureColorSpaceUVE::Linear, TextureTypeUVE::Cubemap, 6U};
    const TextureHandleUVE cube = device.CreateTextureUVE(cubeDesc);
    ASSERT_NE(cube, kInvalidTextureHandleUVE);

    const std::vector<TextureDescUVE> liveDescs = device.GetLiveTextureDescsUVE();
    ASSERT_EQ(liveDescs.size(), 2U);
    // Live-desc order follows the device's unordered handle map, so match by type.
    const TextureDescUVE* arrayLive = nullptr;
    const TextureDescUVE* cubeLive = nullptr;
    for (const TextureDescUVE& live : liveDescs) {
        if (live.type == TextureTypeUVE::Texture2DArray) {
            arrayLive = &live;
        } else if (live.type == TextureTypeUVE::Cubemap) {
            cubeLive = &live;
        }
    }
    ASSERT_NE(arrayLive, nullptr);
    ASSERT_NE(cubeLive, nullptr);
    EXPECT_EQ(arrayLive->arrayLayers, 3U);
    EXPECT_EQ(cubeLive->arrayLayers, 6U);

    device.DestroyTextureUVE(array);
    device.DestroyTextureUVE(cube);
    EXPECT_TRUE(device.GetLiveTextureDescsUVE().empty());
}

TEST(NullRenderDeviceUVETest, CreateTextureUVE_RejectsBadDimensionality) {
    NullRenderDeviceUVE device;
    EXPECT_EQ(device.CreateTextureUVE(TextureDescUVE{4U, 4U, TextureFormatUVE::RGBA8Unorm, 1U,
                                                     TextureColorSpaceUVE::Linear,
                                                     TextureTypeUVE::Texture2D, 2U}),
              kInvalidTextureHandleUVE);
    EXPECT_EQ(device.CreateTextureUVE(TextureDescUVE{4U, 4U, TextureFormatUVE::RGBA8Unorm, 1U,
                                                     TextureColorSpaceUVE::Linear,
                                                     TextureTypeUVE::Cubemap, 5U}),
              kInvalidTextureHandleUVE);
    EXPECT_EQ(device.CreateTextureUVE(TextureDescUVE{4U, 8U, TextureFormatUVE::RGBA8Unorm, 1U,
                                                     TextureColorSpaceUVE::Linear,
                                                     TextureTypeUVE::Cubemap, 6U}),
              kInvalidTextureHandleUVE);
    EXPECT_TRUE(device.GetLiveTextureDescsUVE().empty());
}

TEST(NullRenderDeviceUVETest, RenderPassDescUVE_LayersDefaultToZero) {
    const RenderPassDescUVE desc;
    EXPECT_EQ(desc.colorLayer, 0U);
    EXPECT_EQ(desc.depthLayer, 0U);
}

TEST(NullRenderDeviceUVETest, BeginRenderPassUVE_RecordsSelectedLayers) {
    NullRenderDeviceUVE device;
    const TextureHandleUVE color = device.CreateTextureUVE(
        TextureDescUVE{4U, 4U, TextureFormatUVE::RGBA8Unorm, 1U, TextureColorSpaceUVE::Linear,
                       TextureTypeUVE::Texture2DArray, 4U});
    ASSERT_NE(color, kInvalidTextureHandleUVE);
    const TextureHandleUVE depth = device.CreateTextureUVE(
        TextureDescUVE{4U, 4U, TextureFormatUVE::Depth32Float, 1U});
    ASSERT_NE(depth, kInvalidTextureHandleUVE);

    auto commandBuffer = device.CreateCommandBufferUVE();
    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = color;
    passDesc.depthAttachment = depth;
    passDesc.colorLayer = 2U;
    commandBuffer->BeginRenderPassUVE(passDesc);
    commandBuffer->EndRenderPassUVE();
    device.SubmitUVE(std::move(commandBuffer));

    const std::vector<RecordedCommandUVE>& recorded = device.GetLastSubmittedCommandsUVE();
    ASSERT_EQ(recorded.size(), 2U);
    ASSERT_TRUE(std::holds_alternative<BeginRenderPassCommandUVE>(recorded[0]));
    EXPECT_EQ(std::get<BeginRenderPassCommandUVE>(recorded[0]).desc.colorLayer, 2U);
    EXPECT_EQ(std::get<BeginRenderPassCommandUVE>(recorded[0]).desc.depthLayer, 0U);

    device.DestroyTextureUVE(color);
    device.DestroyTextureUVE(depth);
}

#if UVE_DEBUG
TEST(NullRenderDeviceUVEDeathTest, CommandBuffer_DispatchInsideRenderPass_Asserts) {
    NullRenderDeviceUVE device;
    std::unique_ptr<ICommandBufferUVE> commandBuffer = device.CreateCommandBufferUVE();
    commandBuffer->BeginRenderPassUVE(RenderPassDescUVE{});
    EXPECT_DEATH({ commandBuffer->DispatchUVE(1U, 1U, 1U); }, "");
}

TEST(NullRenderDeviceUVEDeathTest, CommandBuffer_NestedBeginRenderPass_Asserts) {
    NullRenderDeviceUVE device;
    std::unique_ptr<ICommandBufferUVE> commandBuffer = device.CreateCommandBufferUVE();
    commandBuffer->BeginRenderPassUVE(RenderPassDescUVE{});
    EXPECT_DEATH({ commandBuffer->BeginRenderPassUVE(RenderPassDescUVE{}); }, "");
}

TEST(NullRenderDeviceUVEDeathTest, CommandBuffer_DrawOutsideRenderPass_Asserts) {
    NullRenderDeviceUVE device;
    std::unique_ptr<ICommandBufferUVE> commandBuffer = device.CreateCommandBufferUVE();
    EXPECT_DEATH({ commandBuffer->DrawUVE(3); }, "");
}

TEST(NullRenderDeviceUVEDeathTest, CommandBuffer_DrawIndexedIndirectOutsideRenderPass_Asserts) {
    // Same inside-a-pass invariant as DrawUVE above, and asserted the same way: the null backend
    // treats a draw outside a pass as an authoring bug, not a recoverable condition, so in debug
    // builds it traps before the error-log arm is ever reached.
    NullRenderDeviceUVE device;
    const BufferHandleUVE indirect = device.CreateBufferUVE(
        BufferDescUVE{sizeof(DrawIndexedIndirectCommandUVE), BufferUsageUVE::IndirectStorage});
    std::unique_ptr<ICommandBufferUVE> commandBuffer = device.CreateCommandBufferUVE();
    EXPECT_DEATH({ commandBuffer->DrawIndexedIndirectUVE(indirect, 0U); }, "");
}

TEST(NullRenderDeviceUVEDeathTest, CommandBuffer_EndRenderPassWithoutBegin_Asserts) {
    NullRenderDeviceUVE device;
    std::unique_ptr<ICommandBufferUVE> commandBuffer = device.CreateCommandBufferUVE();
    EXPECT_DEATH({ commandBuffer->EndRenderPassUVE(); }, "");
}
#endif

#if !UVE_DEBUG
TEST(NullCommandBufferUVERuntimeTest, CommandBufferLifecycleMisuseIsSafeNoOpInRelease) {
    NullRenderDeviceUVE device;
    std::unique_ptr<ICommandBufferUVE> commandBuffer = device.CreateCommandBufferUVE();

    commandBuffer->BeginRenderPassUVE(RenderPassDescUVE{});
    commandBuffer->BeginRenderPassUVE(RenderPassDescUVE{});
    commandBuffer->EndRenderPassUVE();
    commandBuffer->EndRenderPassUVE();
    commandBuffer->BindPipelineUVE(PipelineHandleUVE{1U});
    commandBuffer->BindVertexBufferUVE(BufferHandleUVE{1U}, 0U);
    commandBuffer->BindIndexBufferUVE(BufferHandleUVE{1U});
    commandBuffer->BindTextureUVE(TextureHandleUVE{1U}, 0U);
    commandBuffer->BindUniformBufferUVE(BufferHandleUVE{1U}, 0U);
    commandBuffer->BindStorageBufferUVE(BufferHandleUVE{1U}, 0U);
    commandBuffer->SetUniformFloatUVE("uFloat", 1.0F);
    commandBuffer->SetUniformIntUVE("uInt", 1);
    commandBuffer->SetUniformBoolUVE("uBool", true);
    commandBuffer->SetUniformVector3UVE("uVector", Math::Vector3UVE{});
    commandBuffer->SetUniformMatrix4x4UVE("uMatrix", Math::Matrix4x4UVE{});
    commandBuffer->DrawIndexedUVE(3U);
    commandBuffer->DrawUVE(3U);
    commandBuffer->DispatchUVE(1U, 1U, 1U);

    // M5b contract update: pipeline binds, texture binds (storage images), storage-buffer
    // binds, SetUniform* and dispatches are LEGAL outside pass markers (the compute flow lives
    // there), so those calls record for real. Every remaining misuse above (nested begin,
    // double end, outside-pass vertex/index/uniform-buffer binds, outside-pass draws) is still
    // a release-safe no-op that must not add a command a later retained submission could execute.
    device.SubmitUVE(std::move(commandBuffer));
    const std::vector<RecordedCommandUVE>& recorded = device.GetLastSubmittedCommandsUVE();
    ASSERT_EQ(recorded.size(), 11U);
    EXPECT_TRUE(std::holds_alternative<BeginRenderPassCommandUVE>(recorded[0]));
    EXPECT_TRUE(std::holds_alternative<EndRenderPassCommandUVE>(recorded[1]));
    EXPECT_TRUE(std::holds_alternative<BindPipelineCommandUVE>(recorded[2]));
    EXPECT_TRUE(std::holds_alternative<BindTextureCommandUVE>(recorded[3]));
    EXPECT_TRUE(std::holds_alternative<BindStorageBufferCommandUVE>(recorded[4]));
    EXPECT_TRUE(std::holds_alternative<SetUniformFloatCommandUVE>(recorded[5]));
    EXPECT_TRUE(std::holds_alternative<SetUniformIntCommandUVE>(recorded[6]));
    EXPECT_TRUE(std::holds_alternative<SetUniformBoolCommandUVE>(recorded[7]));
    EXPECT_TRUE(std::holds_alternative<SetUniformVector3CommandUVE>(recorded[8]));
    EXPECT_TRUE(std::holds_alternative<SetUniformMatrix4x4CommandUVE>(recorded[9]));
    EXPECT_TRUE(std::holds_alternative<DispatchCommandUVE>(recorded[10]));
}
#endif


TEST(NullCommandBufferUVETest, BeginRenderPassUVE_UnknownStoreOp_DoesNotRecordOrEnterPass) {
    // Tier 2.5 mirror of UnknownLoadOp: a garbage store enum is a malformed pass on every
    // backend, so Null refuses to record or enter it (both slots covered, then a valid pass
    // proves the device is unaffected).
    NullRenderDeviceUVE device;
    std::unique_ptr<ICommandBufferUVE> badColorStore = device.CreateCommandBufferUVE();
    RenderPassDescUVE badColorDesc;
    badColorDesc.colorStoreOp = static_cast<StoreOpUVE>(0xFFU);
    badColorStore->BeginRenderPassUVE(badColorDesc);
    device.SubmitUVE(std::move(badColorStore));
    EXPECT_TRUE(device.GetLastSubmittedCommandsUVE().empty());

    std::unique_ptr<ICommandBufferUVE> badDepthStore = device.CreateCommandBufferUVE();
    RenderPassDescUVE badDepthDesc;
    badDepthDesc.depthStoreOp = static_cast<StoreOpUVE>(0xFFU);
    badDepthStore->BeginRenderPassUVE(badDepthDesc);
    device.SubmitUVE(std::move(badDepthStore));
    EXPECT_TRUE(device.GetLastSubmittedCommandsUVE().empty());

    std::unique_ptr<ICommandBufferUVE> validCommandBuffer = device.CreateCommandBufferUVE();
    validCommandBuffer->BeginRenderPassUVE(RenderPassDescUVE{});
    validCommandBuffer->EndRenderPassUVE();
    device.SubmitUVE(std::move(validCommandBuffer));
    EXPECT_EQ(device.GetLastSubmittedCommandsUVE().size(), 2U);
}

TEST(NullCommandBufferUVETest, BeginRenderPassUVE_ExtrasRoundTrip_RecordsSlotsVerbatim) {
    // Tier 2.4/2.5: a two-extra prefix plus non-default store ops records opaquely and reads
    // back verbatim. The defaults pinned first are the zero-behavior-change contract (the
    // renderer's 73 pass sites never set these fields).
    const RenderPassDescUVE defaults;
    EXPECT_EQ(defaults.colorStoreOp, StoreOpUVE::Store);
    EXPECT_EQ(defaults.depthStoreOp, StoreOpUVE::Store);
    for (const ColorAttachmentUVE& slot : defaults.extraColorAttachments) {
        EXPECT_EQ(slot.target, kInvalidTextureHandleUVE);
    }

    NullRenderDeviceUVE device;
    const TextureHandleUVE color = device.CreateTextureUVE(TextureDescUVE{4U, 4U});
    ASSERT_NE(color, kInvalidTextureHandleUVE);
    const TextureHandleUVE depth =
        device.CreateTextureUVE(TextureDescUVE{4U, 4U, TextureFormatUVE::Depth32Float, 1U});
    ASSERT_NE(depth, kInvalidTextureHandleUVE);
    const TextureHandleUVE extra0 = device.CreateTextureUVE(TextureDescUVE{4U, 4U});
    ASSERT_NE(extra0, kInvalidTextureHandleUVE);
    const TextureHandleUVE extra1 = device.CreateTextureUVE(TextureDescUVE{4U, 4U});
    ASSERT_NE(extra1, kInvalidTextureHandleUVE);

    auto commandBuffer = device.CreateCommandBufferUVE();
    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = color;
    passDesc.depthAttachment = depth;
    passDesc.colorStoreOp = StoreOpUVE::Store;
    passDesc.depthStoreOp = StoreOpUVE::DontCare;
    passDesc.extraColorAttachments[0].target = extra0;
    passDesc.extraColorAttachments[0].layer = 1U;
    passDesc.extraColorAttachments[0].loadOp = LoadOpUVE::Load;
    passDesc.extraColorAttachments[0].storeOp = StoreOpUVE::DontCare;
    passDesc.extraColorAttachments[0].clearColor = {0.25F, 0.5F, 0.75F, 1.0F};
    passDesc.extraColorAttachments[1].target = extra1;
    passDesc.extraColorAttachments[1].layer = 0U;
    passDesc.extraColorAttachments[1].loadOp = LoadOpUVE::Clear;
    passDesc.extraColorAttachments[1].storeOp = StoreOpUVE::Store;
    passDesc.extraColorAttachments[1].clearColor = {1.0F, 0.0F, 0.0F, 1.0F};
    commandBuffer->BeginRenderPassUVE(passDesc);
    commandBuffer->EndRenderPassUVE();
    device.SubmitUVE(std::move(commandBuffer));

    const std::vector<RecordedCommandUVE>& recorded = device.GetLastSubmittedCommandsUVE();
    ASSERT_EQ(recorded.size(), 2U);
    ASSERT_TRUE(std::holds_alternative<BeginRenderPassCommandUVE>(recorded[0]));
    const RenderPassDescUVE& roundTripped = std::get<BeginRenderPassCommandUVE>(recorded[0]).desc;
    EXPECT_EQ(roundTripped.colorAttachment, color);
    EXPECT_EQ(roundTripped.depthAttachment, depth);
    EXPECT_EQ(roundTripped.colorStoreOp, StoreOpUVE::Store);
    EXPECT_EQ(roundTripped.depthStoreOp, StoreOpUVE::DontCare);
    EXPECT_EQ(roundTripped.extraColorAttachments[0].target, extra0);
    EXPECT_EQ(roundTripped.extraColorAttachments[0].layer, 1U);
    EXPECT_EQ(roundTripped.extraColorAttachments[0].loadOp, LoadOpUVE::Load);
    EXPECT_EQ(roundTripped.extraColorAttachments[0].storeOp, StoreOpUVE::DontCare);
    EXPECT_FLOAT_EQ(roundTripped.extraColorAttachments[0].clearColor[0], 0.25F);
    EXPECT_FLOAT_EQ(roundTripped.extraColorAttachments[0].clearColor[1], 0.5F);
    EXPECT_FLOAT_EQ(roundTripped.extraColorAttachments[0].clearColor[2], 0.75F);
    EXPECT_FLOAT_EQ(roundTripped.extraColorAttachments[0].clearColor[3], 1.0F);
    EXPECT_EQ(roundTripped.extraColorAttachments[1].target, extra1);
    EXPECT_EQ(roundTripped.extraColorAttachments[1].layer, 0U);
    EXPECT_EQ(roundTripped.extraColorAttachments[1].loadOp, LoadOpUVE::Clear);
    EXPECT_EQ(roundTripped.extraColorAttachments[1].storeOp, StoreOpUVE::Store);
    EXPECT_EQ(roundTripped.extraColorAttachments[2].target, kInvalidTextureHandleUVE);

    device.DestroyTextureUVE(color);
    device.DestroyTextureUVE(depth);
    device.DestroyTextureUVE(extra0);
    device.DestroyTextureUVE(extra1);
}

TEST(NullCommandBufferUVETest, BeginRenderPassUVE_GappyExtras_DoesNotRecordOrEnterPass) {
    // Tier 2.4: location 2 set while 1 is empty is malformed on every backend — Null refuses
    // to record or enter it, then proves the device is unaffected with a valid prefix.
    NullRenderDeviceUVE device;
    const TextureHandleUVE extra = device.CreateTextureUVE(TextureDescUVE{4U, 4U});
    ASSERT_NE(extra, kInvalidTextureHandleUVE);

    std::unique_ptr<ICommandBufferUVE> gappyCommandBuffer = device.CreateCommandBufferUVE();
    RenderPassDescUVE gappyDesc;
    gappyDesc.extraColorAttachments[0].target = extra;
    gappyDesc.extraColorAttachments[2].target = extra;
    gappyCommandBuffer->BeginRenderPassUVE(gappyDesc);
    device.SubmitUVE(std::move(gappyCommandBuffer));
    EXPECT_TRUE(device.GetLastSubmittedCommandsUVE().empty());

    std::unique_ptr<ICommandBufferUVE> validCommandBuffer = device.CreateCommandBufferUVE();
    RenderPassDescUVE validDesc;
    validDesc.extraColorAttachments[0].target = extra;
    validCommandBuffer->BeginRenderPassUVE(validDesc);
    validCommandBuffer->EndRenderPassUVE();
    device.SubmitUVE(std::move(validCommandBuffer));
    EXPECT_EQ(device.GetLastSubmittedCommandsUVE().size(), 2U);

    device.DestroyTextureUVE(extra);
}

TEST(NullCommandBufferUVETest, BeginRenderPassUVE_UnknownExtraStoreOp_DoesNotRecordOrEnterPass) {
    // Tier 2.4/2.5: per-slot ops validate like the location-0 ops — a garbage extra store enum
    // refuses to record or enter.
    NullRenderDeviceUVE device;
    const TextureHandleUVE extra = device.CreateTextureUVE(TextureDescUVE{4U, 4U});
    ASSERT_NE(extra, kInvalidTextureHandleUVE);

    std::unique_ptr<ICommandBufferUVE> invalidCommandBuffer = device.CreateCommandBufferUVE();
    RenderPassDescUVE invalidDesc;
    invalidDesc.extraColorAttachments[0].target = extra;
    invalidDesc.extraColorAttachments[0].storeOp = static_cast<StoreOpUVE>(0xFFU);
    invalidCommandBuffer->BeginRenderPassUVE(invalidDesc);
    device.SubmitUVE(std::move(invalidCommandBuffer));
    EXPECT_TRUE(device.GetLastSubmittedCommandsUVE().empty());

    std::unique_ptr<ICommandBufferUVE> validCommandBuffer = device.CreateCommandBufferUVE();
    validCommandBuffer->BeginRenderPassUVE(RenderPassDescUVE{});
    validCommandBuffer->EndRenderPassUVE();
    device.SubmitUVE(std::move(validCommandBuffer));
    EXPECT_EQ(device.GetLastSubmittedCommandsUVE().size(), 2U);

    device.DestroyTextureUVE(extra);
}

} // namespace
} // namespace UVE::Render::Tests
