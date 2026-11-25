// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "null_render_device.h"

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

#include "log_sink.h"
#include "logger.h"
#include "matrix4x4.h"
#include "vector3.h"
#include "platform.h"

namespace UVE::Render::Tests {
namespace {

TEST(NullRenderDeviceTest, CreateBuffer_UnknownUsage_ReturnsInvalidBeforeAllocation) {
    NullRenderDevice device;
    const BufferHandle invalid =
        device.CreateBuffer(BufferDesc{16U, static_cast<BufferUsage>(0xFFU)});

    EXPECT_EQ(invalid, kInvalidBufferHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);

    const BufferHandle valid = device.CreateBuffer(BufferDesc{16U, BufferUsage::Vertex});
    EXPECT_EQ(valid.value, 1U);
}

TEST(NullRenderDeviceTest, BufferMemoryHintsAreValidatedRecordedAndNeverChangeTheContract) {
    // Buffer memory/update hints: unknown values are refused before allocation (like an unknown
    // usage), known ones are recorded verbatim and reported by GetBufferDesc, and the
    // upload/update/readback contract is identical for every placement — the hint steers where
    // a real backend puts the bytes, never what the caller observes.
    NullRenderDevice device;
    EXPECT_EQ(device.CreateBuffer(BufferDesc{16U, BufferUsage::Storage,
                                                   static_cast<BufferMemory>(0xFFU)}),
              kInvalidBufferHandle);
    EXPECT_EQ(device.CreateBuffer(BufferDesc{16U, BufferUsage::Storage, BufferMemory::Auto,
                                                   static_cast<BufferUpdateFrequency>(0xFFU)}),
              kInvalidBufferHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);
    EXPECT_FALSE(IsBufferDescValid(BufferDesc{16U, BufferUsage::Storage,
                                                    static_cast<BufferMemory>(4U)}));
    EXPECT_TRUE(IsBufferDescValid(BufferDesc{16U, BufferUsage::Storage, BufferMemory::Readback,
                                                   BufferUpdateFrequency::Stream}));

    const std::array<std::byte, 8> bytes{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
                                         std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
    for (const BufferMemory memory : {BufferMemory::Auto, BufferMemory::DeviceLocal,
                                         BufferMemory::Upload, BufferMemory::Readback}) {
        for (const BufferUpdateFrequency frequency :
             {BufferUpdateFrequency::Static, BufferUpdateFrequency::Dynamic, BufferUpdateFrequency::Stream}) {
            const BufferDesc desc{8U, BufferUsage::Vertex, memory, frequency};
            const BufferHandle buffer = device.CreateBuffer(desc, bytes);
            ASSERT_NE(buffer, kInvalidBufferHandle);
            BufferDesc reported{};
            ASSERT_EQ(device.GetBufferDesc(buffer, reported), RenderOperationResult::Success);
            EXPECT_EQ(reported.sizeBytes, 8U);
            EXPECT_EQ(reported.usage, BufferUsage::Vertex);
            EXPECT_EQ(reported.memory, memory);
            EXPECT_EQ(reported.updateFrequency, frequency);
            const std::array<std::byte, 2> patch{std::byte{9}, std::byte{10}};
            ASSERT_TRUE(device.UpdateBuffer(buffer, patch, 6U));
            std::array<std::byte, 8> observed{};
            ASSERT_TRUE(device.ReadbackBuffer(buffer, observed));
            EXPECT_EQ(observed[0], std::byte{1});
            EXPECT_EQ(observed[5], std::byte{6});
            EXPECT_EQ(observed[6], std::byte{9});
            EXPECT_EQ(observed[7], std::byte{10});
            device.DestroyBuffer(buffer);
        }
    }
    BufferDesc untouched{99U, BufferUsage::Index};
    EXPECT_EQ(device.GetBufferDesc(BufferHandle{12345U}, untouched), RenderOperationResult::InvalidHandle);
    EXPECT_EQ(untouched.sizeBytes, 99U) << "failure leaves the output untouched";
    EXPECT_EQ(untouched.usage, BufferUsage::Index);
}

TEST(NullRenderDeviceTest, CreateBuffer_ZeroSize_ReturnsInvalidBeforeAllocation) {
    NullRenderDevice device;

    const BufferHandle invalid = device.CreateBuffer(BufferDesc{0U, BufferUsage::Vertex});

    EXPECT_EQ(invalid, kInvalidBufferHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);
}

TEST(NullRenderDeviceTest, CreateBuffer_OversizedInitialData_ReturnsInvalidBeforeAllocation) {
    NullRenderDevice device;
    const std::array<std::byte, 17> initialData{};
    const BufferHandle invalid =
        device.CreateBuffer(BufferDesc{16U, BufferUsage::Vertex}, initialData);

    EXPECT_EQ(invalid, kInvalidBufferHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);

    const BufferHandle valid = device.CreateBuffer(BufferDesc{16U, BufferUsage::Vertex});
    EXPECT_EQ(valid.value, 1U);
}

TEST(NullRenderDeviceTest, CreateBuffer_ReturnsUniqueHandles) {
    NullRenderDevice device;
    const BufferHandle first = device.CreateBuffer(BufferDesc{16, BufferUsage::Vertex});
    const BufferHandle second = device.CreateBuffer(BufferDesc{16, BufferUsage::Vertex});

    EXPECT_NE(first, kInvalidBufferHandle);
    EXPECT_NE(second, kInvalidBufferHandle);
    EXPECT_NE(first, second);
}

TEST(NullRenderDeviceTest, DestroyBuffer_UnknownHandle_LogsErrorSafely) {
    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    NullRenderDevice device;
    device.DestroyBuffer(BufferHandle{999});

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Error;
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
}

TEST(NullRenderDeviceTest, ReadbackBuffer_ReturnsWhatCreationAndUpdatesWrote) {
    // CS3: the null backend models host-visible buffer CONTENTS, not just descriptors - a
    // readback that could only ever return zeros would make headless assertions a lie.
    NullRenderDevice device;
    const std::array<std::byte, 4> initial{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    const BufferHandle buffer =
        device.CreateBuffer(BufferDesc{8U, BufferUsage::Storage}, initial);
    ASSERT_NE(buffer, kInvalidBufferHandle);

    std::array<std::byte, 8> readback{};
    ASSERT_TRUE(device.ReadbackBuffer(buffer, readback, 0));
    EXPECT_EQ(readback[0], std::byte{1});
    EXPECT_EQ(readback[3], std::byte{4});
    // Bytes past the initial upload must read as the zero fill a fresh buffer has.
    EXPECT_EQ(readback[4], std::byte{0});
    EXPECT_EQ(readback[7], std::byte{0});

    const std::array<std::byte, 2> update{std::byte{9}, std::byte{9}};
    ASSERT_TRUE(device.UpdateBuffer(buffer, update, 6U));
    ASSERT_TRUE(device.ReadbackBuffer(buffer, readback, 0));
    EXPECT_EQ(readback[0], std::byte{1}); // untouched prefix survives an offset write
    EXPECT_EQ(readback[6], std::byte{9});
    EXPECT_EQ(readback[7], std::byte{9});

    // A partial read at an offset sees exactly that window.
    std::array<std::byte, 2> window{};
    ASSERT_TRUE(device.ReadbackBuffer(buffer, window, 2U));
    EXPECT_EQ(window[0], std::byte{3});
    EXPECT_EQ(window[1], std::byte{4});

    // An empty read is a successful no-op.
    EXPECT_TRUE(device.ReadbackBuffer(buffer, std::span<std::byte>{}, 0));

    device.DestroyBuffer(buffer);
}

TEST(NullRenderDeviceTest, ReadbackBuffer_UnknownHandleOrOutOfRange_ReturnsFalse) {
    NullRenderDevice device;
    std::array<std::byte, 4> readback{};
    EXPECT_FALSE(device.ReadbackBuffer(BufferHandle{999}, readback, 0));

    const BufferHandle buffer = device.CreateBuffer(BufferDesc{4U, BufferUsage::Storage});
    ASSERT_NE(buffer, kInvalidBufferHandle);
    std::array<std::byte, 8> tooLarge{};
    EXPECT_FALSE(device.ReadbackBuffer(buffer, tooLarge, 0));
    EXPECT_FALSE(device.ReadbackBuffer(buffer, readback, 2U));   // runs off the end
    EXPECT_FALSE(device.ReadbackBuffer(buffer, readback,
                                           std::numeric_limits<std::uint64_t>::max())); // overflowed offset
    device.DestroyBuffer(buffer);
}

TEST(NullRenderDeviceTest, UpdateBuffer_UnknownHandle_ReturnsFalseAndLogsError) {
    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    NullRenderDevice device;
    const std::array<std::byte, 4> data{};
    EXPECT_FALSE(device.UpdateBuffer(BufferHandle{999}, data, 0));

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Error;
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
}

TEST(NullRenderDeviceTest, UpdateBuffer_WriteWithinSize_ReturnsTrue) {
    NullRenderDevice device;
    const BufferHandle buffer = device.CreateBuffer(BufferDesc{16, BufferUsage::Uniform});
    const std::array<std::byte, 4> data{};

    EXPECT_TRUE(device.UpdateBuffer(buffer, data, 0));
    EXPECT_TRUE(device.UpdateBuffer(buffer, data, 12));
}

TEST(NullRenderDeviceTest, UpdateBuffer_OverflowedOffset_ReturnsFalse) {
    NullRenderDevice device;
    const BufferHandle buffer = device.CreateBuffer(BufferDesc{16, BufferUsage::Uniform});
    const std::array<std::byte, 4> data{};

    EXPECT_FALSE(device.UpdateBuffer(buffer, data, std::numeric_limits<std::uint64_t>::max()));
}

TEST(NullRenderDeviceTest, UpdateBuffer_WriteExceedingSize_ReturnsFalseAndLogsError) {
    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    NullRenderDevice device;
    const BufferHandle buffer = device.CreateBuffer(BufferDesc{4, BufferUsage::Uniform});
    const std::array<std::byte, 8> data{};
    EXPECT_FALSE(device.UpdateBuffer(buffer, data, 0));

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Error;
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
}

TEST(NullRenderDeviceTest, CreateTexture_ReturnsUniqueHandles) {
    NullRenderDevice device;
    const TextureHandle first = device.CreateTexture(TextureDesc{64, 64, TextureFormat::RGBA8Unorm});
    const TextureHandle second = device.CreateTexture(TextureDesc{64, 64, TextureFormat::RGBA8Unorm});

    EXPECT_NE(first, kInvalidTextureHandle);
    EXPECT_NE(second, kInvalidTextureHandle);
    EXPECT_NE(first, second);
}

TEST(NullRenderDeviceTest, CreateTexture_InvalidDescriptorOrUpload_ReturnsInvalidBeforeAllocation) {
    NullRenderDevice device;
    EXPECT_EQ(TextureDesc{}.colorSpace, TextureColorSpace::Linear);
    const std::array<std::byte, 3> incompleteData{};
    const std::array<std::byte, 4> validData{};

    EXPECT_EQ(device.CreateTexture(TextureDesc{0U, 1U, TextureFormat::RGBA8Unorm, 1U}),
              kInvalidTextureHandle);
    EXPECT_EQ(device.CreateTexture(TextureDesc{1U, 1U, TextureFormat::RGBA8Unorm, 1U}, incompleteData),
              kInvalidTextureHandle);
    const TextureHandle valid =
        device.CreateTexture(TextureDesc{1U, 1U, TextureFormat::RGBA8Unorm, 1U}, validData);
    EXPECT_EQ(valid.value, 1U);
    EXPECT_NE(valid, kInvalidTextureHandle);

    const TextureHandle srgb = device.CreateTexture(
        TextureDesc{1U, 1U, TextureFormat::RGBA8Unorm, 1U, TextureColorSpace::Srgb}, validData);
    EXPECT_NE(srgb, kInvalidTextureHandle);
    EXPECT_EQ(device.CreateTexture(
                  TextureDesc{1U, 1U, TextureFormat::RGBA16Float, 1U, TextureColorSpace::Srgb}),
              kInvalidTextureHandle);
    EXPECT_EQ(device.CreateTexture(
                  TextureDesc{1U, 1U, TextureFormat::RGBA8Unorm, 1U,
                                 static_cast<TextureColorSpace>(99U)}),
              kInvalidTextureHandle);

    device.DestroyTexture(srgb);
}

TEST(NullRenderDeviceTest, CreateTexture_ValidatesAndRetainsCompleteMipChain) {
    NullRenderDevice device;
    const TextureDesc desc{2U, 2U, TextureFormat::RGBA8Unorm, 2U};
    std::array<std::byte, 20U> pixels{}; // 2x2 level 0 (16 bytes) + 1x1 level 1 (4 bytes)
    std::uint64_t expectedBytes = 0U;
    ASSERT_TRUE(CalculateTextureUploadByteCount(desc, expectedBytes));
    EXPECT_EQ(expectedBytes, pixels.size());
    EXPECT_EQ(MaximumTextureMipLevelCount(desc.width, desc.height), 2U);
    EXPECT_EQ(GetTextureMipExtent(desc.width, desc.height, 1U).width, 1U);
    EXPECT_EQ(GetTextureMipExtent(desc.width, desc.height, 1U).height, 1U);
    EXPECT_TRUE(ValidateTextureUpload(desc, pixels));
    EXPECT_FALSE(ValidateTextureUpload(desc, std::span<const std::byte>(pixels.data(), pixels.size() - 1U)));
    EXPECT_FALSE(ValidateTextureUpload(desc, {})) << "declared mip chains must provide every level";

    TextureDesc tooManyLevels = desc;
    tooManyLevels.mipLevels = 3U;
    EXPECT_FALSE(CalculateTextureUploadByteCount(tooManyLevels, expectedBytes));
    EXPECT_FALSE(ValidateTextureUpload(tooManyLevels, pixels));
    EXPECT_FALSE(ValidateTextureUpload(
        TextureDesc{2U, 2U, TextureFormat::Depth32Float, 2U}, pixels));

    const TextureHandle texture = device.CreateTexture(desc, pixels);
    ASSERT_NE(texture, kInvalidTextureHandle);
    const std::vector<TextureDesc> liveDescs = device.GetLiveTextureDescs();
    ASSERT_EQ(liveDescs.size(), 1U);
    EXPECT_EQ(liveDescs.front().mipLevels, 2U);
    device.DestroyTexture(texture);
}

TEST(NullRenderDeviceTest, SingleTwoChannelAndFloatFormatsUseTightlyPackedTexelSizes) {
    // One-/two-channel and float formats: byte counts come from the shared block table (tightly packed rows, no
    // alignment padding), they are linear-only, and the Null device accepts exactly-sized uploads
    // for every one of them while rejecting an sRGB request. A 3-wide level is the case GL's
    // default unpack alignment would mis-stride, so it is the width used here.
    struct FormatCase final {
        TextureFormat format;
        std::uint32_t bytesPerTexel;
        std::uint32_t channels;
    };
    constexpr std::array<FormatCase, 7U> kCases{{
        {TextureFormat::R8Unorm, 1U, 1U},      {TextureFormat::RG8Unorm, 2U, 2U},
        {TextureFormat::R16Float, 2U, 1U},     {TextureFormat::RG16Float, 4U, 2U},
        {TextureFormat::R32Float, 4U, 1U},     {TextureFormat::RG32Float, 8U, 2U},
        {TextureFormat::RGBA32Float, 16U, 4U},
    }};
    NullRenderDevice device;
    for (const FormatCase &testCase : kCases) {
        const TextureFormatBlockInfo info = GetTextureFormatBlockInfo(testCase.format);
        EXPECT_EQ(info.width, 1U);
        EXPECT_EQ(info.height, 1U);
        EXPECT_EQ(info.bytes, testCase.bytesPerTexel);
        EXPECT_FALSE(info.compressed);
        EXPECT_TRUE(IsUncompressedColorFormat(testCase.format));
        EXPECT_EQ(GetTextureFormatChannelCount(testCase.format), testCase.channels);
        EXPECT_FALSE(IsTextureFormatSrgbCapable(testCase.format));
        EXPECT_FALSE(IsDepthFormat(testCase.format));
        EXPECT_TRUE(device.SupportsTextureFormat(testCase.format));
        EXPECT_FALSE(device.SupportsTextureFormat(testCase.format, TextureColorSpace::Srgb));

        const TextureDesc desc{3U, 2U, testCase.format, 2U};
        std::uint64_t expectedBytes = 0U;
        ASSERT_TRUE(CalculateTextureUploadByteCount(desc, expectedBytes));
        EXPECT_EQ(expectedBytes, static_cast<std::uint64_t>(3U * 2U + 1U * 1U) * testCase.bytesPerTexel)
            << "3x2 base level plus a 1x1 mip, no row padding";
        std::vector<std::byte> texels(static_cast<std::size_t>(expectedBytes));
        EXPECT_FALSE(ValidateTextureUpload(
            desc, std::span<const std::byte>(texels.data(), texels.size() - 1U)));
        EXPECT_TRUE(ValidateTextureUpload(desc, texels));
        const TextureHandle texture = device.CreateTexture(desc, texels);
        ASSERT_NE(texture, kInvalidTextureHandle);
        device.DestroyTexture(texture);

        const TextureDesc srgbDesc{3U, 2U, testCase.format, 1U, TextureColorSpace::Srgb};
        EXPECT_FALSE(ValidateTextureUpload(srgbDesc, {}))
            << "single/two-channel and float formats have no sRGB encoding";
        EXPECT_EQ(device.CreateTexture(srgbDesc, {}), kInvalidTextureHandle);
    }
    // The legacy formats keep their byte sizes: the table change is append-only.
    EXPECT_EQ(GetTextureFormatBlockInfo(TextureFormat::RGBA8Unorm).bytes, 4U);
    EXPECT_EQ(GetTextureFormatBlockInfo(TextureFormat::RGBA16Float).bytes, 8U);
    EXPECT_EQ(GetTextureFormatChannelCount(TextureFormat::RGBA8Unorm), 4U);
    EXPECT_EQ(GetTextureFormatChannelCount(TextureFormat::Depth32Float), 0U);
    EXPECT_EQ(GetTextureFormatChannelCount(TextureFormat::BC7RGBA), 0U);
    EXPECT_FALSE(IsUncompressedColorFormat(TextureFormat::Depth24Stencil8));
    EXPECT_FALSE(IsUncompressedColorFormat(TextureFormat::ETC2RGB8));
    EXPECT_FALSE(IsUncompressedColorFormat(static_cast<TextureFormat>(255U)));
}

TEST(NullRenderDeviceTest, CompressedTextureUploadsUseBlockAwareMipSizesWithoutAdvertisingSupport) {
    NullRenderDevice device;
    const TextureDesc bc1Desc{7U, 5U, TextureFormat::BC1RGB, 2U, TextureColorSpace::Srgb};
    std::uint64_t expectedBytes = 0U;
    ASSERT_TRUE(CalculateTextureUploadByteCount(bc1Desc, expectedBytes));
    EXPECT_EQ(expectedBytes, 40U); // 4 base blocks + 1 3x2 mip block, 8 bytes each
    EXPECT_FALSE(ValidateTextureUpload(bc1Desc, {})); // compressed textures are not render targets
    EXPECT_FALSE(device.SupportsTextureFormat(TextureFormat::BC1RGB, TextureColorSpace::Srgb));

    std::array<std::byte, 40U> bc1Blocks{};
    EXPECT_FALSE(ValidateTextureUpload(
        bc1Desc, std::span<const std::byte>(bc1Blocks.data(), bc1Blocks.size() - 1U)));
    EXPECT_TRUE(ValidateTextureUpload(bc1Desc, bc1Blocks));
    const TextureHandle texture = device.CreateTexture(bc1Desc, bc1Blocks);
    ASSERT_NE(texture, kInvalidTextureHandle);
    device.DestroyTexture(texture);

    const TextureDesc etc2AlphaDesc{7U, 5U, TextureFormat::ETC2RGBA8, 2U};
    ASSERT_TRUE(CalculateTextureUploadByteCount(etc2AlphaDesc, expectedBytes));
    EXPECT_EQ(expectedBytes, 80U); // 4 base blocks + 1 mip block, 16 bytes each
    EXPECT_FALSE(device.SupportsTextureFormat(TextureFormat::ETC2RGBA8));
    EXPECT_TRUE(device.SupportsTextureFormat(TextureFormat::RGBA8Unorm,
                                                TextureColorSpace::Srgb));
}

TEST(NullRenderDeviceTest, CreateShader_UnknownStage_ReturnsInvalidBeforeAllocation) {
    NullRenderDevice device;
    const ShaderHandle invalid =
        device.CreateShader(ShaderDesc{static_cast<ShaderStage>(0xFFU), "vs"});

    EXPECT_EQ(invalid, kInvalidShaderHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);

    const ShaderHandle valid = device.CreateShader(ShaderDesc{ShaderStage::Vertex, "vs"});
    EXPECT_EQ(valid.value, 1U);
}

TEST(NullRenderDeviceTest, CreateShader_ReturnsUniqueHandles) {
    NullRenderDevice device;
    const ShaderHandle first = device.CreateShader(ShaderDesc{ShaderStage::Vertex, "vs"});
    const ShaderHandle second = device.CreateShader(ShaderDesc{ShaderStage::Fragment, "fs"});

    EXPECT_NE(first, kInvalidShaderHandle);
    EXPECT_NE(second, kInvalidShaderHandle);
    EXPECT_NE(first, second);
}

TEST(NullRenderDeviceTest, CreatePipeline_WithLiveShaders_Succeeds) {
    NullRenderDevice device;
    const ShaderHandle vertexShader = device.CreateShader(ShaderDesc{ShaderStage::Vertex, "vs"});
    const ShaderHandle fragmentShader = device.CreateShader(ShaderDesc{ShaderStage::Fragment, "fs"});

    PipelineDesc desc;
    desc.vertexShader = vertexShader;
    desc.fragmentShader = fragmentShader;

    EXPECT_NE(device.CreatePipeline(desc), kInvalidPipelineHandle);
}

TEST(NullRenderDeviceTest, CreatePipeline_UnknownShaderHandle_ReturnsInvalidAndLogsError) {
    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    NullRenderDevice device;
    PipelineDesc desc;
    desc.vertexShader = ShaderHandle{999};
    desc.fragmentShader = ShaderHandle{998};
    EXPECT_EQ(device.CreatePipeline(desc), kInvalidPipelineHandle);

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Error;
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
}

TEST(NullRenderDeviceTest, GetLiveResourceCount_TracksCreateAndDestroy) {
    NullRenderDevice device;
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);

    const BufferHandle buffer = device.CreateBuffer(BufferDesc{16, BufferUsage::Vertex});
    EXPECT_EQ(device.GetLiveResourceCount(), 1U);

    const TextureHandle texture = device.CreateTexture(TextureDesc{4, 4, TextureFormat::RGBA8Unorm});
    EXPECT_EQ(device.GetLiveResourceCount(), 2U);

    device.DestroyBuffer(buffer);
    EXPECT_EQ(device.GetLiveResourceCount(), 1U);

    device.DestroyTexture(texture);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);
}

TEST(NullRenderDeviceTest, GetBackendName_ReturnsNull) {
    NullRenderDevice device;
    EXPECT_EQ(device.GetBackendName(), "Null");
}

TEST(NullRenderDeviceTest, CommandBufferRecordingThenSubmit_ProducesExpectedCommandSequenceInOrder) {
    NullRenderDevice device;
    const ShaderHandle vertexShader = device.CreateShader(ShaderDesc{ShaderStage::Vertex, "vs"});
    const ShaderHandle fragmentShader = device.CreateShader(ShaderDesc{ShaderStage::Fragment, "fs"});
    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    const PipelineHandle pipeline = device.CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    const BufferHandle vertexBuffer = device.CreateBuffer(BufferDesc{64, BufferUsage::Vertex});
    const BufferHandle indexBuffer = device.CreateBuffer(BufferDesc{12, BufferUsage::Index});
    const TextureHandle colorTarget = device.CreateTexture(TextureDesc{64, 64, TextureFormat::RGBA8Unorm});

    std::unique_ptr<ICommandBuffer> commandBuffer = device.CreateCommandBuffer();
    RenderPassDesc passDesc;
    passDesc.colorAttachment = colorTarget;
    commandBuffer->BeginRenderPass(passDesc);
    commandBuffer->BindPipeline(pipeline);
    commandBuffer->BindVertexBuffer(vertexBuffer, 0);
    commandBuffer->BindIndexBuffer(indexBuffer);
    commandBuffer->DrawIndexed(3, 1);
    commandBuffer->EndRenderPass();

    device.Submit(std::move(commandBuffer));

    const std::vector<RecordedCommand>& recorded = device.GetLastSubmittedCommands();
    ASSERT_EQ(recorded.size(), 6U);
    EXPECT_TRUE(std::holds_alternative<BeginRenderPassCommand>(recorded[0]));
    EXPECT_TRUE(std::holds_alternative<BindPipelineCommand>(recorded[1]));
    EXPECT_TRUE(std::holds_alternative<BindVertexBufferCommand>(recorded[2]));
    EXPECT_TRUE(std::holds_alternative<BindIndexBufferCommand>(recorded[3]));
    EXPECT_TRUE(std::holds_alternative<DrawIndexedCommand>(recorded[4]));
    EXPECT_TRUE(std::holds_alternative<EndRenderPassCommand>(recorded[5]));

    EXPECT_EQ(std::get<BeginRenderPassCommand>(recorded[0]).desc.colorAttachment, colorTarget);
    EXPECT_EQ(std::get<BindPipelineCommand>(recorded[1]).pipeline, pipeline);
    EXPECT_EQ(std::get<BindVertexBufferCommand>(recorded[2]).buffer, vertexBuffer);
    EXPECT_EQ(std::get<BindIndexBufferCommand>(recorded[3]).buffer, indexBuffer);
    EXPECT_EQ(std::get<DrawIndexedCommand>(recorded[4]).indexCount, 3U);
    EXPECT_EQ(std::get<DrawIndexedCommand>(recorded[4]).instanceCount, 1U);
    // Whole-buffer draws start at zero, which is what every caller predating surfaces relies on.
    EXPECT_EQ(std::get<DrawIndexedCommand>(recorded[4]).firstIndex, 0U);
}

TEST(NullRenderDeviceTest, DrawIndexed_RecordsTheFirstIndexOfAnIndexRange) {
    // A mesh surface is an index range of a shared buffer, so the offset has to survive recording
    // intact - a silently dropped firstIndex draws the wrong triangles with the right count.
    NullRenderDevice device;
    const ShaderHandle vertexShader = device.CreateShader(ShaderDesc{ShaderStage::Vertex, "vs"});
    const ShaderHandle fragmentShader = device.CreateShader(ShaderDesc{ShaderStage::Fragment, "fs"});
    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    const PipelineHandle pipeline = device.CreatePipeline(pipelineDesc);
    const BufferHandle indexBuffer = device.CreateBuffer(BufferDesc{48, BufferUsage::Index});
    const TextureHandle colorTarget = device.CreateTexture(TextureDesc{16, 16, TextureFormat::RGBA8Unorm});

    std::unique_ptr<ICommandBuffer> commandBuffer = device.CreateCommandBuffer();
    RenderPassDesc passDesc;
    passDesc.colorAttachment = colorTarget;
    commandBuffer->BeginRenderPass(passDesc);
    commandBuffer->BindPipeline(pipeline);
    commandBuffer->BindIndexBuffer(indexBuffer);
    commandBuffer->DrawIndexed(3U, 1U, 9U);
    commandBuffer->EndRenderPass();
    device.Submit(std::move(commandBuffer));

    const std::vector<RecordedCommand>& recorded = device.GetLastSubmittedCommands();
    const auto draw = std::find_if(recorded.cbegin(), recorded.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<DrawIndexedCommand>(command);
    });
    ASSERT_NE(draw, recorded.cend());
    EXPECT_EQ(std::get<DrawIndexedCommand>(*draw).indexCount, 3U);
    EXPECT_EQ(std::get<DrawIndexedCommand>(*draw).firstIndex, 9U);
}

// --- CS7: indirect indexed draw -------------------------------------------------------------

TEST(NullRenderDeviceTest, DrawIndexedIndirectCommand_MatchesTheGpuParameterLayout) {
    // Both Vulkan's VkDrawIndexedIndirectCommand and GL's DrawElementsIndirectCommand are five
    // 32-bit words in this order. A compute shader writes them with an std430 uvec-ish layout, so
    // the CPU-side mirror has to agree field for field - a silent reorder here would produce
    // draws with nonsense counts that nothing on the CPU ever observes.
    static_assert(sizeof(DrawIndexedIndirectCommand) == 20U);
    EXPECT_EQ(offsetof(DrawIndexedIndirectCommand, indexCount), 0U);
    EXPECT_EQ(offsetof(DrawIndexedIndirectCommand, instanceCount), 4U);
    EXPECT_EQ(offsetof(DrawIndexedIndirectCommand, firstIndex), 8U);
    EXPECT_EQ(offsetof(DrawIndexedIndirectCommand, vertexOffset), 12U);
    EXPECT_EQ(offsetof(DrawIndexedIndirectCommand, firstInstance), 16U);
    // vertexOffset is the one signed field in both APIs; an unsigned mirror would turn a small
    // negative rebase into a ~4-billion vertex index.
    static_assert(std::is_signed_v<decltype(DrawIndexedIndirectCommand::vertexOffset)>);
}

TEST(NullRenderDeviceTest, IndirectStorageUsage_IsValidAndStorageBindable) {
    EXPECT_TRUE(IsBufferUsageValid(BufferUsage::IndirectStorage));
    // The whole point of the usage: a compute shader must be able to bind it as an SSBO and
    // write the draw parameters, otherwise indirect draw buys nothing over DrawIndexed.
    EXPECT_TRUE(IsStorageBindableUsage(BufferUsage::IndirectStorage));
    EXPECT_TRUE(IsStorageBindableUsage(BufferUsage::Storage));
    EXPECT_FALSE(IsStorageBindableUsage(BufferUsage::Vertex));
    EXPECT_FALSE(IsStorageBindableUsage(BufferUsage::Index));
    EXPECT_FALSE(IsStorageBindableUsage(BufferUsage::Uniform));
}

TEST(NullRenderDeviceTest, CreateBuffer_IndirectStorageUsage_Allocates) {
    NullRenderDevice device;
    const BufferHandle indirect = device.CreateBuffer(
        BufferDesc{sizeof(DrawIndexedIndirectCommand), BufferUsage::IndirectStorage});
    EXPECT_NE(indirect, kInvalidBufferHandle);
}

TEST(NullRenderDeviceTest, DrawIndexedIndirect_InsideAPass_RecordsBufferAndOffset) {
    NullRenderDevice device;
    const TextureHandle colorTarget =
        device.CreateTexture(TextureDesc{16, 16, TextureFormat::RGBA8Unorm});
    const BufferHandle indirect = device.CreateBuffer(
        BufferDesc{2U * sizeof(DrawIndexedIndirectCommand), BufferUsage::IndirectStorage});

    std::unique_ptr<ICommandBuffer> commandBuffer = device.CreateCommandBuffer();
    RenderPassDesc passDesc;
    passDesc.colorAttachment = colorTarget;
    commandBuffer->BeginRenderPass(passDesc);
    // A non-zero offset is the interesting case: it is how a single buffer holds a batch of
    // draws, and it is the value most likely to be dropped on the way through the recorder.
    commandBuffer->DrawIndexedIndirect(indirect, sizeof(DrawIndexedIndirectCommand));
    commandBuffer->EndRenderPass();
    device.Submit(std::move(commandBuffer));

    const std::vector<RecordedCommand>& recorded = device.GetLastSubmittedCommands();
    ASSERT_EQ(recorded.size(), 3U);
    ASSERT_TRUE(std::holds_alternative<DrawIndexedIndirectCommandRecord>(recorded[1]));
    const auto& record = std::get<DrawIndexedIndirectCommandRecord>(recorded[1]);
    EXPECT_EQ(record.buffer, indirect);
    EXPECT_EQ(record.offsetBytes, sizeof(DrawIndexedIndirectCommand));
}

TEST(NullRenderDeviceTest, GetLastSubmittedCommands_BeforeAnySubmit_IsEmpty) {
    NullRenderDevice device;
    EXPECT_TRUE(device.GetLastSubmittedCommands().empty());
}

TEST(NullRenderDeviceTest, CreateShader_OutInfoLogParameter_IsAcceptedAndUnused) {
    NullRenderDevice device;
    std::string infoLog = "unset";
    const ShaderHandle handle = device.CreateShader(ShaderDesc{ShaderStage::Vertex, "vs"}, &infoLog);
    EXPECT_NE(handle, kInvalidShaderHandle);
}

TEST(NullRenderDeviceTest, CreatePipeline_OutInfoLogParameter_IsAcceptedAndUnused) {
    NullRenderDevice device;
    const ShaderHandle vertexShader = device.CreateShader(ShaderDesc{ShaderStage::Vertex, "vs"});
    const ShaderHandle fragmentShader = device.CreateShader(ShaderDesc{ShaderStage::Fragment, "fs"});
    PipelineDesc desc;
    desc.vertexShader = vertexShader;
    desc.fragmentShader = fragmentShader;

    std::string infoLog = "unset";
    EXPECT_NE(device.CreatePipeline(desc, &infoLog), kInvalidPipelineHandle);
}

TEST(NullRenderDeviceTest, CreatePipeline_UnknownVertexFormat_ReturnsInvalidBeforePublication) {
    NullRenderDevice device;
    const ShaderHandle vertexShader = device.CreateShader(ShaderDesc{ShaderStage::Vertex, "vs"});
    const ShaderHandle fragmentShader = device.CreateShader(ShaderDesc{ShaderStage::Fragment, "fs"});
    PipelineDesc invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.vertexLayout = {VertexAttribute{"POSITION", static_cast<VertexAttributeFormat>(0xFFU), 0U}};

    EXPECT_EQ(device.CreatePipeline(invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 2U);

    PipelineDesc validDesc = invalidDesc;
    validDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U}};
    const PipelineHandle validPipeline = device.CreatePipeline(validDesc);
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceTest, CreatePipelineFromBinary_UnknownVertexFormat_ReturnsInvalidBeforePublication) {
    NullRenderDevice device;
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDesc invalidDesc;
    invalidDesc.vertexLayout = {VertexAttribute{"POSITION", static_cast<VertexAttributeFormat>(0xFFU), 0U}};

    EXPECT_EQ(device.CreatePipelineFromBinary(binary, 0U, invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);

    const PipelineHandle validPipeline = device.CreatePipelineFromBinary(binary, 0U, PipelineBinaryDesc{});
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceTest, CreatePipeline_UnknownBlendMode_ReturnsInvalidBeforePublication) {
    NullRenderDevice device;
    const ShaderHandle vertexShader = device.CreateShader(ShaderDesc{ShaderStage::Vertex, "vs"});
    const ShaderHandle fragmentShader = device.CreateShader(ShaderDesc{ShaderStage::Fragment, "fs"});
    PipelineDesc invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.blendMode = static_cast<PipelineBlendMode>(0xFFU);

    EXPECT_EQ(device.CreatePipeline(invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 2U);

    invalidDesc.blendMode = PipelineBlendMode::Opaque;
    const PipelineHandle validPipeline = device.CreatePipeline(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceTest, CreatePipeline_LinesTopology_IsAccepted) {
    NullRenderDevice device;
    const ShaderHandle vertexShader = device.CreateShader(ShaderDesc{ShaderStage::Vertex, "vs"});
    const ShaderHandle fragmentShader = device.CreateShader(ShaderDesc{ShaderStage::Fragment, "fs"});
    PipelineDesc desc;
    desc.vertexShader = vertexShader;
    desc.fragmentShader = fragmentShader;
    desc.topology = PrimitiveTopology::Lines;

    const PipelineHandle pipeline = device.CreatePipeline(desc);
    EXPECT_NE(pipeline, kInvalidPipelineHandle);

    EXPECT_TRUE(IsPrimitiveTopologyValid(PrimitiveTopology::Triangles));
    EXPECT_TRUE(IsPrimitiveTopologyValid(PrimitiveTopology::Lines));
    EXPECT_FALSE(IsPrimitiveTopologyValid(static_cast<PrimitiveTopology>(0xFFU)));

    PipelineDesc invalidDesc = desc;
    invalidDesc.topology = static_cast<PrimitiveTopology>(0xFFU);
    EXPECT_EQ(device.CreatePipeline(invalidDesc), kInvalidPipelineHandle);
}

TEST(NullRenderDeviceTest, PointAndStripTopologiesAreAcceptedAndTheirPrimitiveArithmeticIsExact) {
    // Points, LineStrip and TriangleStrip join the valid set (pipelines accept them, from
    // source and from binary) and the two count helpers describe exactly how a draw's vertex
    // count will be consumed: lists need multiples, strips need a minimum, points take anything.
    NullRenderDevice device;
    const ShaderHandle vertexShader = device.CreateShader(ShaderDesc{ShaderStage::Vertex, "vs"});
    const ShaderHandle fragmentShader = device.CreateShader(ShaderDesc{ShaderStage::Fragment, "fs"});
    for (const PrimitiveTopology topology : {PrimitiveTopology::Points, PrimitiveTopology::LineStrip,
                                                PrimitiveTopology::TriangleStrip}) {
        EXPECT_TRUE(IsPrimitiveTopologyValid(topology));
        PipelineDesc desc;
        desc.vertexShader = vertexShader;
        desc.fragmentShader = fragmentShader;
        desc.topology = topology;
        const PipelineHandle pipeline = device.CreatePipeline(desc);
        EXPECT_NE(pipeline, kInvalidPipelineHandle);
        const std::array<std::byte, 4> binary{};
        PipelineBinaryDesc binaryDesc;
        binaryDesc.topology = topology;
        EXPECT_NE(device.CreatePipelineFromBinary(binary, 0U, binaryDesc), kInvalidPipelineHandle);
    }
    EXPECT_FALSE(IsPrimitiveTopologyValid(static_cast<PrimitiveTopology>(5U)));

    EXPECT_EQ(GetPrimitiveCount(PrimitiveTopology::Triangles, 7U), 2U);
    EXPECT_EQ(GetPrimitiveCount(PrimitiveTopology::Lines, 7U), 3U);
    EXPECT_EQ(GetPrimitiveCount(PrimitiveTopology::Points, 7U), 7U);
    EXPECT_EQ(GetPrimitiveCount(PrimitiveTopology::LineStrip, 7U), 6U);
    EXPECT_EQ(GetPrimitiveCount(PrimitiveTopology::LineStrip, 1U), 0U);
    EXPECT_EQ(GetPrimitiveCount(PrimitiveTopology::TriangleStrip, 7U), 5U);
    EXPECT_EQ(GetPrimitiveCount(PrimitiveTopology::TriangleStrip, 2U), 0U);
    EXPECT_EQ(GetPrimitiveCount(static_cast<PrimitiveTopology>(0xFFU), 7U), 0U);

    EXPECT_TRUE(IsVertexCountCompleteForTopology(PrimitiveTopology::Triangles, 0U));
    EXPECT_TRUE(IsVertexCountCompleteForTopology(PrimitiveTopology::Triangles, 6U));
    EXPECT_FALSE(IsVertexCountCompleteForTopology(PrimitiveTopology::Triangles, 7U));
    EXPECT_TRUE(IsVertexCountCompleteForTopology(PrimitiveTopology::Lines, 4U));
    EXPECT_FALSE(IsVertexCountCompleteForTopology(PrimitiveTopology::Lines, 5U));
    EXPECT_TRUE(IsVertexCountCompleteForTopology(PrimitiveTopology::Points, 5U));
    EXPECT_TRUE(IsVertexCountCompleteForTopology(PrimitiveTopology::LineStrip, 0U));
    EXPECT_FALSE(IsVertexCountCompleteForTopology(PrimitiveTopology::LineStrip, 1U));
    EXPECT_TRUE(IsVertexCountCompleteForTopology(PrimitiveTopology::LineStrip, 2U));
    EXPECT_FALSE(IsVertexCountCompleteForTopology(PrimitiveTopology::TriangleStrip, 2U));
    EXPECT_TRUE(IsVertexCountCompleteForTopology(PrimitiveTopology::TriangleStrip, 3U));
    EXPECT_FALSE(IsVertexCountCompleteForTopology(static_cast<PrimitiveTopology>(0xFFU), 0U));
}

TEST(NullRenderDeviceTest, PackedVertexAttributeFormatsHaveExactWidthsAndFitStrideChecks) {
    // Byte widths drive the stride validation, so a packed layout that would have been
    // rejected under the old float-only arithmetic is accepted exactly when it fits.
    struct FormatCase final {
        VertexAttributeFormat format;
        std::uint32_t bytes;
        std::uint32_t components;
        bool integer;
    };
    constexpr std::array<FormatCase, 9> kCases{{
        {VertexAttributeFormat::Float2, 8U, 2U, false},
        {VertexAttributeFormat::Float3, 12U, 3U, false},
        {VertexAttributeFormat::Float4, 16U, 4U, false},
        {VertexAttributeFormat::UByte4Norm, 4U, 4U, false},
        {VertexAttributeFormat::UShort4, 8U, 4U, true},
        {VertexAttributeFormat::UInt4, 16U, 4U, true},
        {VertexAttributeFormat::Short2Norm, 4U, 2U, false},
        {VertexAttributeFormat::Half2, 4U, 2U, false},
        {VertexAttributeFormat::Half4, 8U, 4U, false},
    }};
    NullRenderDevice device;
    const ShaderHandle vertexShader = device.CreateShader(ShaderDesc{ShaderStage::Vertex, "vs"});
    const ShaderHandle fragmentShader = device.CreateShader(ShaderDesc{ShaderStage::Fragment, "fs"});
    for (const FormatCase& testCase : kCases) {
        EXPECT_TRUE(IsVertexAttributeFormatValid(testCase.format));
        EXPECT_EQ(GetVertexAttributeFormatByteSize(testCase.format), testCase.bytes);
        EXPECT_EQ(GetVertexAttributeFormatComponentCount(testCase.format), testCase.components);
        EXPECT_EQ(IsVertexAttributeFormatInteger(testCase.format), testCase.integer);
        const std::array<VertexAttribute, 1> layout{VertexAttribute{"ATTR", testCase.format, 4U}};
        EXPECT_TRUE(IsVertexLayoutWithinStride(layout, 4U + testCase.bytes));
        EXPECT_FALSE(IsVertexLayoutWithinStride(layout, 4U + testCase.bytes - 1U));
        PipelineDesc desc;
        desc.vertexShader = vertexShader;
        desc.fragmentShader = fragmentShader;
        desc.vertexLayout = {layout.begin(), layout.end()};
        desc.vertexStride = 4U + testCase.bytes;
        EXPECT_NE(device.CreatePipeline(desc), kInvalidPipelineHandle);
    }
    const auto unknown = static_cast<VertexAttributeFormat>(0xFFU);
    EXPECT_FALSE(IsVertexAttributeFormatValid(unknown));
    EXPECT_EQ(GetVertexAttributeFormatByteSize(unknown), 0U);
    EXPECT_EQ(GetVertexAttributeFormatComponentCount(unknown), 0U);
    EXPECT_FALSE(IsVertexAttributeFormatInteger(unknown));
    // A 16-byte skinned-vertex tail: colour (4) + joints (8) + padding fits where four floats of
    // colour alone (16) at the same offset would not.
    const std::array<VertexAttribute, 2> packedTail{
        VertexAttribute{"COLOR", VertexAttributeFormat::UByte4Norm, 12U},
        VertexAttribute{"JOINTS", VertexAttributeFormat::UShort4, 16U}};
    EXPECT_TRUE(IsVertexLayoutWithinStride(packedTail, 24U));
    const std::array<VertexAttribute, 1> floatColour{VertexAttribute{"COLOR", VertexAttributeFormat::Float4, 12U}};
    EXPECT_FALSE(IsVertexLayoutWithinStride(floatColour, 24U));
}

TEST(NullRenderDeviceTest, CreatePipelineFromBinary_UnknownBlendMode_ReturnsInvalidBeforePublication) {
    NullRenderDevice device;
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDesc invalidDesc;
    invalidDesc.blendMode = static_cast<PipelineBlendMode>(0xFFU);

    EXPECT_EQ(device.CreatePipelineFromBinary(binary, 0U, invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);

    invalidDesc.blendMode = PipelineBlendMode::Opaque;
    const PipelineHandle validPipeline = device.CreatePipelineFromBinary(binary, 0U, invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceTest, CreatePipeline_UnknownTopology_ReturnsInvalidBeforePublication) {
    NullRenderDevice device;
    const ShaderHandle vertexShader = device.CreateShader(ShaderDesc{ShaderStage::Vertex, "vs"});
    const ShaderHandle fragmentShader = device.CreateShader(ShaderDesc{ShaderStage::Fragment, "fs"});
    PipelineDesc invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.topology = static_cast<PrimitiveTopology>(0xFFU);

    EXPECT_EQ(device.CreatePipeline(invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 2U);

    invalidDesc.topology = PrimitiveTopology::Triangles;
    const PipelineHandle validPipeline = device.CreatePipeline(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceTest, CreatePipelineFromBinary_UnknownTopology_ReturnsInvalidBeforePublication) {
    NullRenderDevice device;
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDesc invalidDesc;
    invalidDesc.topology = static_cast<PrimitiveTopology>(0xFFU);

    EXPECT_EQ(device.CreatePipelineFromBinary(binary, 0U, invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);

    invalidDesc.topology = PrimitiveTopology::Triangles;
    const PipelineHandle validPipeline = device.CreatePipelineFromBinary(binary, 0U, invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceTest, GetPipelineUniforms_AnyHandle_ReturnsEmpty) {
    NullRenderDevice device;
    EXPECT_TRUE(device.GetPipelineUniforms(PipelineHandle{1}).empty());
}

TEST(NullRenderDeviceTest, GetPipelineBinary_AnyHandle_ReturnsFalse) {
    NullRenderDevice device;
    std::vector<std::byte> outBinary;
    std::uint32_t outFormat = 0;
    EXPECT_FALSE(device.GetPipelineBinary(PipelineHandle{1}, outBinary, outFormat));
    EXPECT_TRUE(outBinary.empty());
}

TEST(NullRenderDeviceTest, CreatePipelineFromBinary_AlwaysSucceeds) {
    NullRenderDevice device;
    const std::array<std::byte, 4> binary{};
    const PipelineHandle pipeline = device.CreatePipelineFromBinary(binary, 0, PipelineBinaryDesc{});
    EXPECT_NE(pipeline, kInvalidPipelineHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 1U);

    device.DestroyPipeline(pipeline);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);
}

TEST(NullRenderDeviceTest, CommandBuffer_SetUniformCalls_AreRecordedInOrderWithValues) {
    NullRenderDevice device;
    std::unique_ptr<ICommandBuffer> commandBuffer = device.CreateCommandBuffer();
    commandBuffer->BeginRenderPass(RenderPassDesc{});
    commandBuffer->SetUniformFloat("uFloat", 1.5F);
    commandBuffer->SetUniformInt("uInt", 7);
    commandBuffer->SetUniformBool("uBool", true);
    commandBuffer->SetUniformVector3("uVec3", Math::Vector3{1.0F, 2.0F, 3.0F});
    commandBuffer->SetUniformMatrix4x4("uMat4", Math::Matrix4x4::Identity());
    commandBuffer->EndRenderPass();
    device.Submit(std::move(commandBuffer));

    const std::vector<RecordedCommand>& recorded = device.GetLastSubmittedCommands();
    ASSERT_EQ(recorded.size(), 7U);
    ASSERT_TRUE(std::holds_alternative<BeginRenderPassCommand>(recorded[0]));
    ASSERT_TRUE(std::holds_alternative<SetUniformFloatCommand>(recorded[1]));
    EXPECT_EQ(std::get<SetUniformFloatCommand>(recorded[1]).name, "uFloat");
    EXPECT_FLOAT_EQ(std::get<SetUniformFloatCommand>(recorded[1]).value, 1.5F);

    ASSERT_TRUE(std::holds_alternative<SetUniformIntCommand>(recorded[2]));
    EXPECT_EQ(std::get<SetUniformIntCommand>(recorded[2]).value, 7);

    ASSERT_TRUE(std::holds_alternative<SetUniformBoolCommand>(recorded[3]));
    EXPECT_TRUE(std::get<SetUniformBoolCommand>(recorded[3]).value);

    ASSERT_TRUE(std::holds_alternative<SetUniformVector3Command>(recorded[4]));
    EXPECT_EQ(std::get<SetUniformVector3Command>(recorded[4]).value.y, 2.0F);

    ASSERT_TRUE(std::holds_alternative<SetUniformMatrix4x4Command>(recorded[5]));
    ASSERT_TRUE(std::holds_alternative<EndRenderPassCommand>(recorded[6]));
}


TEST(NullRenderDeviceTest, CommandBuffer_ParallelRecordAndSubmitIsSafeAndRetainsWholeLists) {
    // M4: Null honors the same threading contract as the Vulkan backend — N threads may each
    // create, record, and submit their OWN command buffers concurrently (Submit stores the
    // spy under a mutex). The spy keeps the LAST-submitted list — which thread that is, is
    // nondeterministic — so every thread records the same STRUCTURE with thread-tagged values:
    // whichever list lands last, it must be a complete, untorn Begin + 5 uniform sets + End
    // whose values all carry one thread's tag.
    NullRenderDevice device;
    std::array<bool, 4> submitted{};
    std::vector<std::thread> threads;
    threads.reserve(4U);
    for (std::int32_t worker = 0; worker < 4; ++worker) {
        threads.emplace_back([&device, &submitted, worker]() {
            std::unique_ptr<ICommandBuffer> commandBuffer = device.CreateCommandBuffer();
            if (commandBuffer == nullptr) {
                return;
            }
            commandBuffer->BeginRenderPass(RenderPassDesc{});
            for (std::int32_t uniformIndex = 0; uniformIndex < 5; ++uniformIndex) {
                commandBuffer->SetUniformInt("uInt", worker * 100 + uniformIndex);
            }
            commandBuffer->EndRenderPass();
            device.Submit(std::move(commandBuffer));
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
    const std::vector<RecordedCommand>& recorded = device.GetLastSubmittedCommands();
    ASSERT_EQ(recorded.size(), 7U);
    EXPECT_TRUE(std::holds_alternative<BeginRenderPassCommand>(recorded[0]));
    EXPECT_TRUE(std::holds_alternative<EndRenderPassCommand>(recorded[6]));
    std::int32_t workerTag = -1;
    for (std::size_t index = 1; index <= 5; ++index) {
        ASSERT_TRUE(std::holds_alternative<SetUniformIntCommand>(recorded[index]));
        const std::int32_t value = std::get<SetUniformIntCommand>(recorded[index]).value;
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

TEST(NullRenderDeviceTest, CommandBuffer_BindStorageBuffer_IsRecordedWithBufferAndSlot) {
    // M2f: the Null spy must retain storage-buffer binds like every other bind family, so
    // RenderSystems-level tests can assert the recorded buffer/slot without a real GPU.
    NullRenderDevice device;
    std::unique_ptr<ICommandBuffer> commandBuffer = device.CreateCommandBuffer();
    commandBuffer->BeginRenderPass(RenderPassDesc{});
    commandBuffer->BindStorageBuffer(BufferHandle{7U}, 2U);
    commandBuffer->EndRenderPass();
    device.Submit(std::move(commandBuffer));

    const std::vector<RecordedCommand>& recorded = device.GetLastSubmittedCommands();
    ASSERT_EQ(recorded.size(), 3U);
    ASSERT_TRUE(std::holds_alternative<BindStorageBufferCommand>(recorded[1]));
    EXPECT_EQ(std::get<BindStorageBufferCommand>(recorded[1]).buffer, BufferHandle{7U});
    EXPECT_EQ(std::get<BindStorageBufferCommand>(recorded[1]).slot, 2U);
}

TEST(NullCommandBufferTest, BeginRenderPass_UnknownLoadOp_DoesNotRecordOrEnterPass) {
    NullRenderDevice device;
    std::unique_ptr<ICommandBuffer> invalidCommandBuffer = device.CreateCommandBuffer();
    RenderPassDesc invalidDesc;
    invalidDesc.colorLoadOp = static_cast<LoadOp>(0xFFU);
    invalidCommandBuffer->BeginRenderPass(invalidDesc);
    device.Submit(std::move(invalidCommandBuffer));
    EXPECT_TRUE(device.GetLastSubmittedCommands().empty());

    std::unique_ptr<ICommandBuffer> validCommandBuffer = device.CreateCommandBuffer();
    validCommandBuffer->BeginRenderPass(RenderPassDesc{});
    validCommandBuffer->EndRenderPass();
    device.Submit(std::move(validCommandBuffer));
    EXPECT_EQ(device.GetLastSubmittedCommands().size(), 2U);
}

TEST(NullRenderDeviceTest, CreateComputePipeline_BookkeepsAndValidatesStage) {
    // M5a: compute pipelines live in the SAME handle domain as graphics pipelines (one
    // DestroyPipeline erases from either map) and count as live resources; the shader
    // handle must be live AND Compute-stage.
    NullRenderDevice device;
    const ShaderHandle computeShader = device.CreateShader(ShaderDesc{ShaderStage::Compute, "cs"});
    ASSERT_NE(computeShader, kInvalidShaderHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 1U);

    ComputePipelineDesc pipelineDesc;
    pipelineDesc.computeShader = computeShader;
    const PipelineHandle pipeline = device.CreateComputePipeline(pipelineDesc);
    EXPECT_NE(pipeline, kInvalidPipelineHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 2U);

    device.DestroyPipeline(pipeline);
    EXPECT_EQ(device.GetLiveResourceCount(), 1U);

    ComputePipelineDesc unknownShaderDesc;
    unknownShaderDesc.computeShader = ShaderHandle{999999U};
    EXPECT_EQ(device.CreateComputePipeline(unknownShaderDesc), kInvalidPipelineHandle);

    const ShaderHandle vertexShader = device.CreateShader(ShaderDesc{ShaderStage::Vertex, "vs"});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ComputePipelineDesc wrongStageDesc;
    wrongStageDesc.computeShader = vertexShader;
    EXPECT_EQ(device.CreateComputePipeline(wrongStageDesc), kInvalidPipelineHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 2U); // both shaders live, no pipeline recorded

    device.DestroyShader(computeShader);
    device.DestroyShader(vertexShader);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);
}

TEST(NullRenderDeviceTest, Dispatch_RecordsOutsidePassWithGroupCounts) {
    // M5a recording contract: the compute flow (bind compute pipeline, bind its SSBO,
    // dispatch) is recorded entirely OUTSIDE render-pass markers, in call order, with the
    // exact group counts.
    NullRenderDevice device;
    const ShaderHandle computeShader = device.CreateShader(ShaderDesc{ShaderStage::Compute, "cs"});
    ComputePipelineDesc pipelineDesc;
    pipelineDesc.computeShader = computeShader;
    const PipelineHandle computePipeline = device.CreateComputePipeline(pipelineDesc);
    ASSERT_NE(computePipeline, kInvalidPipelineHandle);
    const BufferHandle storageBuffer = device.CreateBuffer(BufferDesc{64, BufferUsage::Storage});
    ASSERT_NE(storageBuffer, kInvalidBufferHandle);

    std::unique_ptr<ICommandBuffer> commandBuffer = device.CreateCommandBuffer();
    commandBuffer->BindPipeline(computePipeline);
    commandBuffer->BindStorageBuffer(storageBuffer, 0U);
    commandBuffer->Dispatch(4U, 2U, 3U);
    device.Submit(std::move(commandBuffer));

    const std::vector<RecordedCommand>& recorded = device.GetLastSubmittedCommands();
    ASSERT_EQ(recorded.size(), 3U);
    EXPECT_TRUE(std::holds_alternative<BindPipelineCommand>(recorded[0]));
    EXPECT_TRUE(std::holds_alternative<BindStorageBufferCommand>(recorded[1]));
    EXPECT_TRUE(std::holds_alternative<DispatchCommand>(recorded[2]));
    EXPECT_EQ(std::get<BindPipelineCommand>(recorded[0]).pipeline, computePipeline);
    const DispatchCommand& dispatch = std::get<DispatchCommand>(recorded[2]);
    EXPECT_EQ(dispatch.groupCountX, 4U);
    EXPECT_EQ(dispatch.groupCountY, 2U);
    EXPECT_EQ(dispatch.groupCountZ, 3U);

    device.DestroyBuffer(storageBuffer);
    device.DestroyPipeline(computePipeline);
    device.DestroyShader(computeShader);
}

TEST(NullRenderDeviceTest, Dispatch_RecordsOutsidePassWithStorageTextureAndGroupCounts) {
    // M5b: BindTexture feeds STORAGE_IMAGE descriptors outside pass markers as well.
    NullRenderDevice device;
    const ShaderHandle computeShader =
        device.CreateShader(ShaderDesc{ShaderStage::Compute, "compute"});
    ASSERT_NE(computeShader, kInvalidShaderHandle);
    ComputePipelineDesc pipelineDesc{};
    pipelineDesc.computeShader = computeShader;
    const PipelineHandle computePipeline = device.CreateComputePipeline(pipelineDesc);
    ASSERT_NE(computePipeline, kInvalidPipelineHandle);
    const TextureHandle storageTexture =
        device.CreateTexture(TextureDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U});
    ASSERT_NE(storageTexture, kInvalidTextureHandle);

    std::unique_ptr<ICommandBuffer> commandBuffer = device.CreateCommandBuffer();
    commandBuffer->BindPipeline(computePipeline);
    commandBuffer->BindTexture(storageTexture, 0U);
    commandBuffer->Dispatch(2U, 2U, 1U);
    device.Submit(std::move(commandBuffer));

    const std::vector<RecordedCommand>& recorded = device.GetLastSubmittedCommands();
    ASSERT_EQ(recorded.size(), 3U);
    EXPECT_TRUE(std::holds_alternative<BindPipelineCommand>(recorded[0]));
    EXPECT_TRUE(std::holds_alternative<BindTextureCommand>(recorded[1]));
    EXPECT_TRUE(std::holds_alternative<DispatchCommand>(recorded[2]));
    EXPECT_EQ(std::get<BindPipelineCommand>(recorded[0]).pipeline, computePipeline);
    EXPECT_EQ(std::get<BindTextureCommand>(recorded[1]).texture, storageTexture);
    const DispatchCommand& dispatch = std::get<DispatchCommand>(recorded[2]);
    EXPECT_EQ(dispatch.groupCountX, 2U);
    EXPECT_EQ(dispatch.groupCountY, 2U);
    EXPECT_EQ(dispatch.groupCountZ, 1U);

    device.DestroyTexture(storageTexture);
    device.DestroyPipeline(computePipeline);
    device.DestroyShader(computeShader);
}

TEST(NullRenderDeviceTest, CreatePipeline_UnknownCullMode_ReturnsInvalidBeforePublication) {
    NullRenderDevice device;
    const ShaderHandle vertexShader = device.CreateShader(ShaderDesc{ShaderStage::Vertex, "vs"});
    const ShaderHandle fragmentShader = device.CreateShader(ShaderDesc{ShaderStage::Fragment, "fs"});
    PipelineDesc invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.cullMode = static_cast<CullMode>(0xFFU);

    EXPECT_EQ(device.CreatePipeline(invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 2U);

    invalidDesc.cullMode = CullMode::Back;
    const PipelineHandle validPipeline = device.CreatePipeline(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceTest, CreatePipelineFromBinary_UnknownCullMode_ReturnsInvalidBeforePublication) {
    NullRenderDevice device;
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDesc invalidDesc;
    invalidDesc.cullMode = static_cast<CullMode>(0xFFU);

    EXPECT_EQ(device.CreatePipelineFromBinary(binary, 0U, invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);

    invalidDesc.cullMode = CullMode::Back;
    const PipelineHandle validPipeline = device.CreatePipelineFromBinary(binary, 0U, invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceTest, CreatePipeline_UnknownFrontFace_ReturnsInvalidBeforePublication) {
    NullRenderDevice device;
    const ShaderHandle vertexShader = device.CreateShader(ShaderDesc{ShaderStage::Vertex, "vs"});
    const ShaderHandle fragmentShader = device.CreateShader(ShaderDesc{ShaderStage::Fragment, "fs"});
    PipelineDesc invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.frontFace = static_cast<FrontFace>(0xFFU);

    EXPECT_EQ(device.CreatePipeline(invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 2U);

    invalidDesc.frontFace = FrontFace::Clockwise;
    const PipelineHandle validPipeline = device.CreatePipeline(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceTest, CreatePipelineFromBinary_UnknownFrontFace_ReturnsInvalidBeforePublication) {
    NullRenderDevice device;
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDesc invalidDesc;
    invalidDesc.frontFace = static_cast<FrontFace>(0xFFU);

    EXPECT_EQ(device.CreatePipelineFromBinary(binary, 0U, invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);

    invalidDesc.frontFace = FrontFace::Clockwise;
    const PipelineHandle validPipeline = device.CreatePipelineFromBinary(binary, 0U, invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceTest, CreatePipeline_UnknownFillMode_ReturnsInvalidBeforePublication) {
    NullRenderDevice device;
    const ShaderHandle vertexShader = device.CreateShader(ShaderDesc{ShaderStage::Vertex, "vs"});
    const ShaderHandle fragmentShader = device.CreateShader(ShaderDesc{ShaderStage::Fragment, "fs"});
    PipelineDesc invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.fillMode = static_cast<FillMode>(0xFFU);

    EXPECT_EQ(device.CreatePipeline(invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 2U);

    invalidDesc.fillMode = FillMode::Wireframe;
    const PipelineHandle validPipeline = device.CreatePipeline(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceTest, CreatePipelineFromBinary_UnknownFillMode_ReturnsInvalidBeforePublication) {
    NullRenderDevice device;
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDesc invalidDesc;
    invalidDesc.fillMode = static_cast<FillMode>(0xFFU);

    EXPECT_EQ(device.CreatePipelineFromBinary(binary, 0U, invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);

    invalidDesc.fillMode = FillMode::Wireframe;
    const PipelineHandle validPipeline = device.CreatePipelineFromBinary(binary, 0U, invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceTest, CreatePipeline_UnknownDepthCompare_ReturnsInvalidBeforePublication) {
    NullRenderDevice device;
    const ShaderHandle vertexShader = device.CreateShader(ShaderDesc{ShaderStage::Vertex, "vs"});
    const ShaderHandle fragmentShader = device.CreateShader(ShaderDesc{ShaderStage::Fragment, "fs"});
    PipelineDesc invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.depthCompare = static_cast<DepthCompare>(0xFFU);

    EXPECT_EQ(device.CreatePipeline(invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 2U);

    invalidDesc.depthCompare = DepthCompare::LessOrEqual;
    const PipelineHandle validPipeline = device.CreatePipeline(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceTest, CreatePipelineFromBinary_UnknownDepthCompare_ReturnsInvalidBeforePublication) {
    NullRenderDevice device;
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDesc invalidDesc;
    invalidDesc.depthCompare = static_cast<DepthCompare>(0xFFU);

    EXPECT_EQ(device.CreatePipelineFromBinary(binary, 0U, invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);

    invalidDesc.depthCompare = DepthCompare::LessOrEqual;
    const PipelineHandle validPipeline = device.CreatePipelineFromBinary(binary, 0U, invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);
}

TEST(NullRenderDeviceTest, PipelineDescs_DefaultRasterizerState_MatchesPreTier21Behavior) {
    const PipelineDesc desc;
    EXPECT_EQ(desc.cullMode, CullMode::None);
    EXPECT_EQ(desc.frontFace, FrontFace::CounterClockwise);
    EXPECT_EQ(desc.fillMode, FillMode::Fill);
    EXPECT_FALSE(desc.depthBiasEnabled);
    EXPECT_FLOAT_EQ(desc.depthBiasConstantFactor, 0.0F);
    EXPECT_FLOAT_EQ(desc.depthBiasSlopeFactor, 0.0F);
    EXPECT_EQ(desc.depthCompare, DepthCompare::Less);

    const PipelineBinaryDesc binaryDesc;
    EXPECT_EQ(binaryDesc.cullMode, CullMode::None);
    EXPECT_EQ(binaryDesc.frontFace, FrontFace::CounterClockwise);
    EXPECT_EQ(binaryDesc.fillMode, FillMode::Fill);
    EXPECT_FALSE(binaryDesc.depthBiasEnabled);
    EXPECT_FLOAT_EQ(binaryDesc.depthBiasConstantFactor, 0.0F);
    EXPECT_FLOAT_EQ(binaryDesc.depthBiasSlopeFactor, 0.0F);
    EXPECT_EQ(binaryDesc.depthCompare, DepthCompare::Less);
}

TEST(NullRenderDeviceTest, SamplerDesc_Defaults_MatchPreTier22Behavior) {
    const SamplerDesc desc;
    EXPECT_EQ(desc.magFilter, SamplerFilter::Linear);
    EXPECT_EQ(desc.minFilter, SamplerFilter::Linear);
    EXPECT_EQ(desc.mipMode, SamplerMipMode::Linear);
    EXPECT_EQ(desc.wrapU, SamplerWrap::ClampToEdge);
    EXPECT_EQ(desc.wrapV, SamplerWrap::ClampToEdge);
    EXPECT_EQ(desc.wrapW, SamplerWrap::ClampToEdge);
    EXPECT_FLOAT_EQ(desc.maxAnisotropy, 1.0F);
    EXPECT_TRUE(IsSamplerDescValid(desc));
}

TEST(NullRenderDeviceTest, CreateSampler_InvalidMagFilter_ReturnsInvalid) {
    NullRenderDevice device;
    SamplerDesc desc;
    desc.magFilter = static_cast<SamplerFilter>(0xFFU);
    EXPECT_EQ(device.CreateSampler(desc), kInvalidSamplerHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);
}

TEST(NullRenderDeviceTest, CreateSampler_InvalidMinFilter_ReturnsInvalid) {
    NullRenderDevice device;
    SamplerDesc desc;
    desc.minFilter = static_cast<SamplerFilter>(0xFFU);
    EXPECT_EQ(device.CreateSampler(desc), kInvalidSamplerHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);
}

TEST(NullRenderDeviceTest, CreateSampler_InvalidMipMode_ReturnsInvalid) {
    NullRenderDevice device;
    SamplerDesc desc;
    desc.mipMode = static_cast<SamplerMipMode>(0xFFU);
    EXPECT_EQ(device.CreateSampler(desc), kInvalidSamplerHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);
}

TEST(NullRenderDeviceTest, CreateSampler_InvalidWrap_ReturnsInvalid) {
    NullRenderDevice device;
    SamplerDesc desc;
    desc.wrapV = static_cast<SamplerWrap>(0xFFU);
    EXPECT_EQ(device.CreateSampler(desc), kInvalidSamplerHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);
}

TEST(NullRenderDeviceTest, CreateSampler_BadAnisotropy_ReturnsInvalid) {
    NullRenderDevice device;
    SamplerDesc desc;
    desc.maxAnisotropy = 0.5F;
    EXPECT_EQ(device.CreateSampler(desc), kInvalidSamplerHandle);
    desc.maxAnisotropy = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(device.CreateSampler(desc), kInvalidSamplerHandle);
    desc.maxAnisotropy = std::numeric_limits<float>::infinity();
    EXPECT_EQ(device.CreateSampler(desc), kInvalidSamplerHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);

    // Null has no device limit — any finite >= 1.0 is accepted (real backends clamp).
    desc.maxAnisotropy = 1.0e30F;
    EXPECT_NE(device.CreateSampler(desc), kInvalidSamplerHandle);
}

TEST(NullRenderDeviceTest, CreateSampler_NonDefaults_RoundTripThroughLiveDescs) {
    NullRenderDevice device;
    SamplerDesc desc;
    desc.magFilter = SamplerFilter::Point;
    desc.minFilter = SamplerFilter::Point;
    desc.mipMode = SamplerMipMode::None;
    desc.wrapU = SamplerWrap::Repeat;
    desc.wrapV = SamplerWrap::MirroredRepeat;
    desc.maxAnisotropy = 4.0F;
    const SamplerHandle sampler = device.CreateSampler(desc);
    ASSERT_NE(sampler, kInvalidSamplerHandle);

    const std::vector<SamplerDesc> liveDescs = device.GetLiveSamplerDescs();
    ASSERT_EQ(liveDescs.size(), 1U);
    EXPECT_EQ(liveDescs[0].magFilter, SamplerFilter::Point);
    EXPECT_EQ(liveDescs[0].minFilter, SamplerFilter::Point);
    EXPECT_EQ(liveDescs[0].mipMode, SamplerMipMode::None);
    EXPECT_EQ(liveDescs[0].wrapU, SamplerWrap::Repeat);
    EXPECT_EQ(liveDescs[0].wrapV, SamplerWrap::MirroredRepeat);
    EXPECT_FLOAT_EQ(liveDescs[0].maxAnisotropy, 4.0F);
}

TEST(NullRenderDeviceTest, BindSampler_RecordedWithSlotOnSubmit) {
    NullRenderDevice device;
    const SamplerHandle sampler = device.CreateSampler(SamplerDesc{});
    ASSERT_NE(sampler, kInvalidSamplerHandle);

    std::unique_ptr<ICommandBuffer> commandBuffer = device.CreateCommandBuffer();
    commandBuffer->BeginRenderPass(RenderPassDesc{});
    commandBuffer->BindSampler(sampler, 3U);
    commandBuffer->EndRenderPass();
    device.Submit(std::move(commandBuffer));

    const std::vector<RecordedCommand>& recorded = device.GetLastSubmittedCommands();
    ASSERT_EQ(recorded.size(), 3U);
    const auto* bind = std::get_if<BindSamplerCommand>(&recorded[1]);
    ASSERT_NE(bind, nullptr);
    EXPECT_EQ(bind->sampler, sampler);
    EXPECT_EQ(bind->slot, 3U);
}

TEST(NullRenderDeviceTest, CreateSampler_CountsTowardLiveResourcesUntilDestroyed) {
    NullRenderDevice device;
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);
    const SamplerHandle sampler = device.CreateSampler(SamplerDesc{});
    ASSERT_NE(sampler, kInvalidSamplerHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 1U);
    EXPECT_TRUE(device.GetLiveSamplerDescs().empty() == false);
    device.DestroySampler(sampler);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);
    EXPECT_TRUE(device.GetLiveSamplerDescs().empty());
}

TEST(NullRenderDeviceTest, DestroySampler_UnknownHandle_IsSafeNoOp) {
    NullRenderDevice device;
    const SamplerHandle sampler = device.CreateSampler(SamplerDesc{});
    ASSERT_NE(sampler, kInvalidSamplerHandle);
    device.DestroySampler(sampler);
    device.DestroySampler(sampler); // double-destroy: logged, no crash
    device.DestroySampler(kInvalidSamplerHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);
}


// Tier 2.3: array layers + cubemaps. The Null backend shares the RHI validators with every
// backend, so these pin the dimensional contract (defaults, byte math, upload sizes, handle
// retention, pass-layer recording) without needing a GPU.
TEST(NullRenderDeviceTest, TextureType_DefaultsTo2DWithOneLayer) {
    const TextureDesc desc;
    EXPECT_EQ(desc.type, TextureType::Texture2D);
    EXPECT_EQ(desc.arrayLayers, 1U);
    EXPECT_TRUE(IsTextureTypeValid(TextureType::Texture2D));
    EXPECT_TRUE(IsTextureTypeValid(TextureType::Texture2DArray));
    EXPECT_TRUE(IsTextureTypeValid(TextureType::Cubemap));
    EXPECT_FALSE(IsTextureTypeValid(static_cast<TextureType>(99U)));
}

TEST(NullRenderDeviceTest, CalculateTextureUploadByteCount_MultipliesByLayerCount) {
    // 2x2 RGBA8, two levels: 16 + 4 = 20 bytes per layer; x4 layers = 80.
    const TextureDesc arrayDesc{2U, 2U, TextureFormat::RGBA8Unorm, 2U,
                                   TextureColorSpace::Linear, TextureType::Texture2DArray, 4U};
    std::uint64_t expectedBytes = 0U;
    ASSERT_TRUE(CalculateTextureUploadByteCount(arrayDesc, expectedBytes));
    EXPECT_EQ(expectedBytes, 80U);

    // 4x4 RGBA8 cube, one level: 64 bytes per face; x6 faces = 384.
    const TextureDesc cubeDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U,
                                  TextureColorSpace::Linear, TextureType::Cubemap, 6U};
    ASSERT_TRUE(CalculateTextureUploadByteCount(cubeDesc, expectedBytes));
    EXPECT_EQ(expectedBytes, 384U);
}

TEST(NullRenderDeviceTest, CalculateTextureUploadByteCount_RejectsBadDimensionality) {
    std::uint64_t expectedBytes = 0U;
    // 2D textures carry exactly one layer.
    EXPECT_FALSE(CalculateTextureUploadByteCount(
        TextureDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U, TextureColorSpace::Linear,
                       TextureType::Texture2D, 2U},
        expectedBytes));
    EXPECT_FALSE(CalculateTextureUploadByteCount(
        TextureDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U, TextureColorSpace::Linear,
                       TextureType::Texture2D, 0U},
        expectedBytes));
    // Cubes are square with exactly six layers.
    EXPECT_FALSE(CalculateTextureUploadByteCount(
        TextureDesc{4U, 8U, TextureFormat::RGBA8Unorm, 1U, TextureColorSpace::Linear,
                       TextureType::Cubemap, 6U},
        expectedBytes));
    EXPECT_FALSE(CalculateTextureUploadByteCount(
        TextureDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U, TextureColorSpace::Linear,
                       TextureType::Cubemap, 5U},
        expectedBytes));
    // Arrays need at least one layer.
    EXPECT_FALSE(CalculateTextureUploadByteCount(
        TextureDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U, TextureColorSpace::Linear,
                       TextureType::Texture2DArray, 0U},
        expectedBytes));
    // Unknown dimensionality never computes a size.
    EXPECT_FALSE(CalculateTextureUploadByteCount(
        TextureDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U, TextureColorSpace::Linear,
                       static_cast<TextureType>(99U), 1U},
        expectedBytes));
}

TEST(NullRenderDeviceTest, ValidateTextureUpload_ArrayRequiresEveryLayer) {
    const TextureDesc arrayDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U,
                                   TextureColorSpace::Linear, TextureType::Texture2DArray, 3U};
    std::array<std::byte, 192U> allLayers{}; // 64 bytes x 3 layers, level-major
    EXPECT_TRUE(ValidateTextureUpload(arrayDesc, allLayers));
    EXPECT_FALSE(ValidateTextureUpload(
        arrayDesc, std::span<const std::byte>(allLayers.data(), 64U))) << "one layer is short";
    EXPECT_FALSE(ValidateTextureUpload(
        arrayDesc, std::span<const std::byte>(allLayers.data(), 128U))) << "two layers are short";
    // Render-target arrays (single-level, uncompressed) still allow a data-free creation.
    EXPECT_TRUE(ValidateTextureUpload(arrayDesc, {}));

    const TextureDesc cubeDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U,
                                  TextureColorSpace::Linear, TextureType::Cubemap, 6U};
    std::array<std::byte, 384U> allFaces{};
    EXPECT_TRUE(ValidateTextureUpload(cubeDesc, allFaces));
    EXPECT_FALSE(ValidateTextureUpload(
        cubeDesc, std::span<const std::byte>(allFaces.data(), 320U))) << "five faces are short";
    EXPECT_TRUE(ValidateTextureUpload(cubeDesc, {}));
}

TEST(NullRenderDeviceTest, CreateTexture_ArrayAndCube_RoundTripThroughLiveDescs) {
    NullRenderDevice device;
    const TextureDesc arrayDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U,
                                   TextureColorSpace::Linear, TextureType::Texture2DArray, 3U};
    const TextureHandle array = device.CreateTexture(arrayDesc);
    ASSERT_NE(array, kInvalidTextureHandle);
    const TextureDesc cubeDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U,
                                  TextureColorSpace::Linear, TextureType::Cubemap, 6U};
    const TextureHandle cube = device.CreateTexture(cubeDesc);
    ASSERT_NE(cube, kInvalidTextureHandle);

    const std::vector<TextureDesc> liveDescs = device.GetLiveTextureDescs();
    ASSERT_EQ(liveDescs.size(), 2U);
    // Live-desc order follows the device's unordered handle map, so match by type.
    const TextureDesc* arrayLive = nullptr;
    const TextureDesc* cubeLive = nullptr;
    for (const TextureDesc& live : liveDescs) {
        if (live.type == TextureType::Texture2DArray) {
            arrayLive = &live;
        } else if (live.type == TextureType::Cubemap) {
            cubeLive = &live;
        }
    }
    ASSERT_NE(arrayLive, nullptr);
    ASSERT_NE(cubeLive, nullptr);
    EXPECT_EQ(arrayLive->arrayLayers, 3U);
    EXPECT_EQ(cubeLive->arrayLayers, 6U);

    device.DestroyTexture(array);
    device.DestroyTexture(cube);
    EXPECT_TRUE(device.GetLiveTextureDescs().empty());
}

TEST(NullRenderDeviceTest, CreateTexture_RejectsBadDimensionality) {
    NullRenderDevice device;
    EXPECT_EQ(device.CreateTexture(TextureDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U,
                                                     TextureColorSpace::Linear,
                                                     TextureType::Texture2D, 2U}),
              kInvalidTextureHandle);
    EXPECT_EQ(device.CreateTexture(TextureDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U,
                                                     TextureColorSpace::Linear,
                                                     TextureType::Cubemap, 5U}),
              kInvalidTextureHandle);
    EXPECT_EQ(device.CreateTexture(TextureDesc{4U, 8U, TextureFormat::RGBA8Unorm, 1U,
                                                     TextureColorSpace::Linear,
                                                     TextureType::Cubemap, 6U}),
              kInvalidTextureHandle);
    EXPECT_TRUE(device.GetLiveTextureDescs().empty());
}

TEST(NullRenderDeviceTest, TextureUsage_DefaultResolvesToWhatTheFormatAndShapeSupport) {
    // Usage flags: an aggregate-initialised descriptor still means "everything the format/shape
    // can honour", so pre-flag call sites keep their behaviour; narrower sets are kept verbatim.
    const TextureDesc legacy{4U, 4U, TextureFormat::RGBA8Unorm};
    EXPECT_EQ(legacy.usage, TextureUsage::Default);
    EXPECT_EQ(legacy.depth, 1U);
    EXPECT_EQ(GetEffectiveTextureUsage(legacy), TextureUsage::Default);

    const TextureDesc bc1{4U, 4U, TextureFormat::BC1RGB};
    EXPECT_EQ(GetEffectiveTextureUsage(bc1),
              TextureUsage::Sampled | TextureUsage::TransferSrc | TextureUsage::TransferDst);
    EXPECT_FALSE(HasTextureUsage(GetEffectiveTextureUsage(bc1), TextureUsage::RenderTarget));
    EXPECT_FALSE(HasTextureUsage(GetEffectiveTextureUsage(bc1), TextureUsage::Storage));

    const TextureDesc depth{4U, 4U, TextureFormat::Depth32Float};
    EXPECT_EQ(GetEffectiveTextureUsage(depth),
              TextureUsage::Sampled | TextureUsage::RenderTarget | TextureUsage::TransferDst);

    TextureDesc volume{4U, 4U, TextureFormat::RGBA8Unorm};
    volume.type = TextureType::Texture3D;
    volume.depth = 4U;
    EXPECT_EQ(GetEffectiveTextureUsage(volume),
              TextureUsage::Sampled | TextureUsage::TransferSrc | TextureUsage::TransferDst);

    TextureDesc sampledOnly{4U, 4U, TextureFormat::RGBA8Unorm};
    sampledOnly.usage = TextureUsage::Sampled;
    EXPECT_EQ(GetEffectiveTextureUsage(sampledOnly), TextureUsage::Sampled);
    EXPECT_TRUE(IsTextureUsageSupported(sampledOnly));

    EXPECT_FALSE(IsTextureUsageValid(TextureUsage::None));
    EXPECT_FALSE(IsTextureUsageValid(static_cast<TextureUsage>(0x80U)));
    EXPECT_TRUE(IsTextureUsageValid(TextureUsage::Sampled | TextureUsage::TransferSrc));
    EXPECT_FALSE(HasTextureUsage(TextureUsage::Default, TextureUsage::None));
}

TEST(NullRenderDeviceTest, CreateTexture_RefusesUsageOutsideTheSupportedSet) {
    // Asking for a use the format/shape can never honour is a caller error caught by the shared
    // validator before any backend allocates, on every backend alike.
    NullRenderDevice device;
    const std::vector<std::byte> bc1Block(8U);
    TextureDesc renderTargetBc1{4U, 4U, TextureFormat::BC1RGB};
    renderTargetBc1.usage = TextureUsage::Sampled | TextureUsage::RenderTarget;
    EXPECT_EQ(device.CreateTexture(renderTargetBc1, bc1Block), kInvalidTextureHandle);

    TextureDesc storageDepth{4U, 4U, TextureFormat::Depth32Float};
    storageDepth.usage = TextureUsage::Storage;
    EXPECT_EQ(device.CreateTexture(storageDepth), kInvalidTextureHandle);

    TextureDesc transferSrcDepth{4U, 4U, TextureFormat::Depth32Float};
    transferSrcDepth.usage = TextureUsage::TransferSrc;
    EXPECT_EQ(device.CreateTexture(transferSrcDepth), kInvalidTextureHandle);

    TextureDesc unknownBit{4U, 4U, TextureFormat::RGBA8Unorm};
    unknownBit.usage = static_cast<TextureUsage>(0x40U);
    EXPECT_EQ(device.CreateTexture(unknownBit), kInvalidTextureHandle);

    TextureDesc emptyUsage{4U, 4U, TextureFormat::RGBA8Unorm};
    emptyUsage.usage = TextureUsage::None;
    EXPECT_EQ(device.CreateTexture(emptyUsage), kInvalidTextureHandle);

    TextureDesc renderTargetVolume{4U, 4U, TextureFormat::RGBA8Unorm};
    renderTargetVolume.type = TextureType::Texture3D;
    renderTargetVolume.depth = 2U;
    renderTargetVolume.usage = TextureUsage::RenderTarget;
    EXPECT_EQ(device.CreateTexture(renderTargetVolume, std::vector<std::byte>(128U)),
              kInvalidTextureHandle);
    EXPECT_TRUE(device.GetLiveTextureDescs().empty());

    // A supported narrow set round-trips verbatim through the live descriptor.
    TextureDesc sampledOnly{4U, 4U, TextureFormat::RGBA8Unorm};
    sampledOnly.usage = TextureUsage::Sampled | TextureUsage::TransferDst;
    const TextureHandle texture = device.CreateTexture(sampledOnly);
    ASSERT_NE(texture, kInvalidTextureHandle);
    TextureDesc reported{};
    ASSERT_EQ(device.GetTextureDesc(texture, reported), RenderOperationResult::Success);
    EXPECT_EQ(reported.usage, TextureUsage::Sampled | TextureUsage::TransferDst);
    EXPECT_EQ(reported.depth, 1U);
    device.DestroyTexture(texture);
}

TEST(NullRenderDeviceTest, CalculateTextureUploadByteCount_VolumeIsSliceMajorWithHalvingDepth) {
    // 4x4x4 RGBA8 with the full chain: 4*4*4*4 (256) + 2*2*2*4 (32) + 1*1*1*4 (4) = 292 bytes.
    TextureDesc volume{4U, 4U, TextureFormat::RGBA8Unorm, 3U};
    volume.type = TextureType::Texture3D;
    volume.depth = 4U;
    std::uint64_t bytes = 0U;
    ASSERT_TRUE(CalculateTextureUploadByteCount(volume, bytes));
    EXPECT_EQ(bytes, 292U);
    EXPECT_EQ(MaximumTextureMipLevelCount(4U, 4U, 4U), 3U);
    EXPECT_EQ(MaximumTextureMipLevelCount(4U, 4U, 16U), 5U); // depth alone extends the chain
    EXPECT_EQ(MaximumTextureMipLevelCount(4U, 4U, 0U), 0U);
    EXPECT_EQ(GetTextureMipDepth(4U, 0U), 4U);
    EXPECT_EQ(GetTextureMipDepth(4U, 1U), 2U);
    EXPECT_EQ(GetTextureMipDepth(4U, 2U), 1U);
    EXPECT_EQ(GetTextureMipDepth(4U, 9U), 1U);
    EXPECT_EQ(GetTextureMipDepth(1U, 0U), 1U);

    // A deep-but-flat volume: 1x1x8, four levels = 8 + 4 + 2 + 1 texels.
    TextureDesc column{1U, 1U, TextureFormat::R8Unorm, 4U};
    column.type = TextureType::Texture3D;
    column.depth = 8U;
    ASSERT_TRUE(CalculateTextureUploadByteCount(column, bytes));
    EXPECT_EQ(bytes, 15U);

    // Shape rules: only a volume carries depth, a volume never carries layers or a depth format,
    // and a zero depth is as invalid as a zero width.
    TextureDesc layeredVolume = volume;
    layeredVolume.arrayLayers = 2U;
    EXPECT_FALSE(CalculateTextureUploadByteCount(layeredVolume, bytes));
    TextureDesc deep2D{4U, 4U, TextureFormat::RGBA8Unorm};
    deep2D.depth = 2U;
    EXPECT_FALSE(CalculateTextureUploadByteCount(deep2D, bytes));
    TextureDesc zeroDepth = volume;
    zeroDepth.depth = 0U;
    EXPECT_FALSE(CalculateTextureUploadByteCount(zeroDepth, bytes));
    TextureDesc depthVolume = volume;
    depthVolume.format = TextureFormat::Depth32Float;
    depthVolume.mipLevels = 1U;
    EXPECT_FALSE(CalculateTextureUploadByteCount(depthVolume, bytes));
    TextureDesc tooManyMips = volume;
    tooManyMips.mipLevels = 4U;
    EXPECT_FALSE(CalculateTextureUploadByteCount(tooManyMips, bytes));
    EXPECT_EQ(bytes, 15U); // failure leaves the output untouched
}

TEST(NullRenderDeviceTest, CreateTexture_VolumeRequiresEverySliceAndRoundTripsDepth) {
    NullRenderDevice device;
    TextureDesc volume{4U, 4U, TextureFormat::RGBA8Unorm, 3U};
    volume.type = TextureType::Texture3D;
    volume.depth = 4U;
    // One slice short of the full chain is refused; the exact 292-byte chain is accepted.
    EXPECT_EQ(device.CreateTexture(volume, std::vector<std::byte>(292U - 64U)), kInvalidTextureHandle);
    EXPECT_EQ(device.CreateTexture(volume, {}), kInvalidTextureHandle); // multi-level needs data
    const TextureHandle texture = device.CreateTexture(volume, std::vector<std::byte>(292U));
    ASSERT_NE(texture, kInvalidTextureHandle);
    TextureDesc reported{};
    ASSERT_EQ(device.GetTextureDesc(texture, reported), RenderOperationResult::Success);
    EXPECT_EQ(reported.type, TextureType::Texture3D);
    EXPECT_EQ(reported.depth, 4U);
    EXPECT_EQ(reported.arrayLayers, 1U);
    EXPECT_EQ(reported.mipLevels, 3U);
    EXPECT_EQ(reported.usage, TextureUsage::Default);
    device.DestroyTexture(texture);
    EXPECT_TRUE(device.GetLiveTextureDescs().empty());
}

TEST(NullRenderDeviceTest, BlitAndMipGenerationRequireTransferUsageAndRefuseVolumes) {
    // Device-level enforcement shared by every backend: a blit needs TransferSrc on its source and
    // TransferDst on its destination, mip generation needs both on the same texture, and volumes
    // never take part (the blit model is 2D). Null sees descriptors here, so it enforces too.
    NullRenderDevice device;
    TextureDesc sampledOnly{4U, 4U, TextureFormat::RGBA8Unorm};
    sampledOnly.usage = TextureUsage::Sampled;
    TextureDesc noTransferDst{4U, 4U, TextureFormat::RGBA8Unorm};
    noTransferDst.usage = TextureUsage::Sampled | TextureUsage::TransferSrc;
    const TextureHandle sealed = device.CreateTexture(sampledOnly);
    const TextureHandle readable = device.CreateTexture(noTransferDst);
    const TextureHandle open = device.CreateTexture(TextureDesc{4U, 4U, TextureFormat::RGBA8Unorm});
    ASSERT_NE(sealed, kInvalidTextureHandle);
    ASSERT_NE(readable, kInvalidTextureHandle);
    ASSERT_NE(open, kInvalidTextureHandle);
    const TextureRegion whole{0U, 0U, 0U, 0U, 4U, 4U};
    EXPECT_EQ(device.BlitTexture({sealed, open, whole, whole, BlitFilter::Point}),
              RenderOperationResult::InvalidArgument); // source lacks TransferSrc
    EXPECT_EQ(device.BlitTexture({open, readable, whole, whole, BlitFilter::Point}),
              RenderOperationResult::InvalidArgument); // destination lacks TransferDst
    EXPECT_EQ(device.BlitTexture({readable, open, whole, whole, BlitFilter::Point}),
              RenderOperationResult::Recorded);
    EXPECT_EQ(device.GetRecordedDeviceOperations().size(), 1U);

    TextureDesc sampledChain{4U, 4U, TextureFormat::RGBA8Unorm, 3U};
    sampledChain.usage = TextureUsage::Sampled | TextureUsage::TransferDst;
    const TextureHandle chain = device.CreateTexture(sampledChain, std::vector<std::byte>(84U));
    ASSERT_NE(chain, kInvalidTextureHandle);
    EXPECT_FALSE(CanGenerateMipmaps(sampledChain));
    EXPECT_EQ(device.GenerateMipmaps(chain), RenderOperationResult::InvalidArgument);
    const TextureHandle fullChain =
        device.CreateTexture(TextureDesc{4U, 4U, TextureFormat::RGBA8Unorm, 3U}, std::vector<std::byte>(84U));
    ASSERT_NE(fullChain, kInvalidTextureHandle);
    EXPECT_EQ(device.GenerateMipmaps(fullChain), RenderOperationResult::Recorded);

    // A volume never blits (the blit model is 2D) but does build its own mip chain: 4x4x2 with
    // two levels = 128 + 16 bytes, depth halving with the other axes.
    TextureDesc volumeDesc{4U, 4U, TextureFormat::RGBA8Unorm, 2U};
    volumeDesc.type = TextureType::Texture3D;
    volumeDesc.depth = 2U;
    const TextureHandle volume = device.CreateTexture(volumeDesc, std::vector<std::byte>(144U));
    ASSERT_NE(volume, kInvalidTextureHandle);
    EXPECT_EQ(device.BlitTexture({volume, open, whole, whole, BlitFilter::Point}),
              RenderOperationResult::InvalidArgument);
    EXPECT_EQ(device.BlitTexture({open, volume, whole, whole, BlitFilter::Point}),
              RenderOperationResult::InvalidArgument);
    EXPECT_TRUE(CanGenerateMipmaps(volumeDesc));
    EXPECT_EQ(device.GenerateMipmaps(volume), RenderOperationResult::Recorded);
    for (const TextureHandle handle : {sealed, readable, open, chain, fullChain, volume}) {
        device.DestroyTexture(handle);
    }
}

TEST(NullRenderDeviceTest, RenderPassDesc_LayersDefaultToZero) {
    const RenderPassDesc desc;
    EXPECT_EQ(desc.colorLayer, 0U);
    EXPECT_EQ(desc.depthLayer, 0U);
}

TEST(NullRenderDeviceTest, BeginRenderPass_RecordsSelectedLayers) {
    NullRenderDevice device;
    const TextureHandle color = device.CreateTexture(
        TextureDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U, TextureColorSpace::Linear,
                       TextureType::Texture2DArray, 4U});
    ASSERT_NE(color, kInvalidTextureHandle);
    const TextureHandle depth = device.CreateTexture(
        TextureDesc{4U, 4U, TextureFormat::Depth32Float, 1U});
    ASSERT_NE(depth, kInvalidTextureHandle);

    auto commandBuffer = device.CreateCommandBuffer();
    RenderPassDesc passDesc;
    passDesc.colorAttachment = color;
    passDesc.depthAttachment = depth;
    passDesc.colorLayer = 2U;
    commandBuffer->BeginRenderPass(passDesc);
    commandBuffer->EndRenderPass();
    device.Submit(std::move(commandBuffer));

    const std::vector<RecordedCommand>& recorded = device.GetLastSubmittedCommands();
    ASSERT_EQ(recorded.size(), 2U);
    ASSERT_TRUE(std::holds_alternative<BeginRenderPassCommand>(recorded[0]));
    EXPECT_EQ(std::get<BeginRenderPassCommand>(recorded[0]).desc.colorLayer, 2U);
    EXPECT_EQ(std::get<BeginRenderPassCommand>(recorded[0]).desc.depthLayer, 0U);

    device.DestroyTexture(color);
    device.DestroyTexture(depth);
}

#if UVE_DEBUG
TEST(NullRenderDeviceDeathTest, CommandBuffer_DispatchInsideRenderPass_Asserts) {
    NullRenderDevice device;
    std::unique_ptr<ICommandBuffer> commandBuffer = device.CreateCommandBuffer();
    commandBuffer->BeginRenderPass(RenderPassDesc{});
    EXPECT_DEATH({ commandBuffer->Dispatch(1U, 1U, 1U); }, "");
}

TEST(NullRenderDeviceDeathTest, CommandBuffer_NestedBeginRenderPass_Asserts) {
    NullRenderDevice device;
    std::unique_ptr<ICommandBuffer> commandBuffer = device.CreateCommandBuffer();
    commandBuffer->BeginRenderPass(RenderPassDesc{});
    EXPECT_DEATH({ commandBuffer->BeginRenderPass(RenderPassDesc{}); }, "");
}

TEST(NullRenderDeviceDeathTest, CommandBuffer_DrawOutsideRenderPass_Asserts) {
    NullRenderDevice device;
    std::unique_ptr<ICommandBuffer> commandBuffer = device.CreateCommandBuffer();
    EXPECT_DEATH({ commandBuffer->Draw(3); }, "");
}

TEST(NullRenderDeviceDeathTest, CommandBuffer_DrawIndexedIndirectOutsideRenderPass_Asserts) {
    // Same inside-a-pass invariant as Draw above, and asserted the same way: the null backend
    // treats a draw outside a pass as an authoring bug, not a recoverable condition, so in debug
    // builds it traps before the error-log arm is ever reached.
    NullRenderDevice device;
    const BufferHandle indirect = device.CreateBuffer(
        BufferDesc{sizeof(DrawIndexedIndirectCommand), BufferUsage::IndirectStorage});
    std::unique_ptr<ICommandBuffer> commandBuffer = device.CreateCommandBuffer();
    EXPECT_DEATH({ commandBuffer->DrawIndexedIndirect(indirect, 0U); }, "");
}

TEST(NullRenderDeviceDeathTest, CommandBuffer_EndRenderPassWithoutBegin_Asserts) {
    NullRenderDevice device;
    std::unique_ptr<ICommandBuffer> commandBuffer = device.CreateCommandBuffer();
    EXPECT_DEATH({ commandBuffer->EndRenderPass(); }, "");
}
#endif

#if !UVE_DEBUG
TEST(NullCommandBufferRuntimeTest, CommandBufferLifecycleMisuseIsSafeNoOpInRelease) {
    NullRenderDevice device;
    std::unique_ptr<ICommandBuffer> commandBuffer = device.CreateCommandBuffer();

    commandBuffer->BeginRenderPass(RenderPassDesc{});
    commandBuffer->BeginRenderPass(RenderPassDesc{});
    commandBuffer->EndRenderPass();
    commandBuffer->EndRenderPass();
    commandBuffer->BindPipeline(PipelineHandle{1U});
    commandBuffer->BindVertexBuffer(BufferHandle{1U}, 0U);
    commandBuffer->BindIndexBuffer(BufferHandle{1U});
    commandBuffer->BindTexture(TextureHandle{1U}, 0U);
    commandBuffer->BindUniformBuffer(BufferHandle{1U}, 0U);
    commandBuffer->BindStorageBuffer(BufferHandle{1U}, 0U);
    commandBuffer->SetUniformFloat("uFloat", 1.0F);
    commandBuffer->SetUniformInt("uInt", 1);
    commandBuffer->SetUniformBool("uBool", true);
    commandBuffer->SetUniformVector3("uVector", Math::Vector3{});
    commandBuffer->SetUniformMatrix4x4("uMatrix", Math::Matrix4x4{});
    commandBuffer->DrawIndexed(3U);
    commandBuffer->Draw(3U);
    commandBuffer->Dispatch(1U, 1U, 1U);

    // M5b contract update: pipeline binds, texture binds (storage images), storage-buffer
    // binds, SetUniform* and dispatches are LEGAL outside pass markers (the compute flow lives
    // there), so those calls record for real. Every remaining misuse above (nested begin,
    // double end, outside-pass vertex/index/uniform-buffer binds, outside-pass draws) is still
    // a release-safe no-op that must not add a command a later retained submission could execute.
    device.Submit(std::move(commandBuffer));
    const std::vector<RecordedCommand>& recorded = device.GetLastSubmittedCommands();
    ASSERT_EQ(recorded.size(), 11U);
    EXPECT_TRUE(std::holds_alternative<BeginRenderPassCommand>(recorded[0]));
    EXPECT_TRUE(std::holds_alternative<EndRenderPassCommand>(recorded[1]));
    EXPECT_TRUE(std::holds_alternative<BindPipelineCommand>(recorded[2]));
    EXPECT_TRUE(std::holds_alternative<BindTextureCommand>(recorded[3]));
    EXPECT_TRUE(std::holds_alternative<BindStorageBufferCommand>(recorded[4]));
    EXPECT_TRUE(std::holds_alternative<SetUniformFloatCommand>(recorded[5]));
    EXPECT_TRUE(std::holds_alternative<SetUniformIntCommand>(recorded[6]));
    EXPECT_TRUE(std::holds_alternative<SetUniformBoolCommand>(recorded[7]));
    EXPECT_TRUE(std::holds_alternative<SetUniformVector3Command>(recorded[8]));
    EXPECT_TRUE(std::holds_alternative<SetUniformMatrix4x4Command>(recorded[9]));
    EXPECT_TRUE(std::holds_alternative<DispatchCommand>(recorded[10]));
}
#endif


TEST(NullCommandBufferTest, BeginRenderPass_UnknownStoreOp_DoesNotRecordOrEnterPass) {
    // Tier 2.5 mirror of UnknownLoadOp: a garbage store enum is a malformed pass on every
    // backend, so Null refuses to record or enter it (both slots covered, then a valid pass
    // proves the device is unaffected).
    NullRenderDevice device;
    std::unique_ptr<ICommandBuffer> badColorStore = device.CreateCommandBuffer();
    RenderPassDesc badColorDesc;
    badColorDesc.colorStoreOp = static_cast<StoreOp>(0xFFU);
    badColorStore->BeginRenderPass(badColorDesc);
    device.Submit(std::move(badColorStore));
    EXPECT_TRUE(device.GetLastSubmittedCommands().empty());

    std::unique_ptr<ICommandBuffer> badDepthStore = device.CreateCommandBuffer();
    RenderPassDesc badDepthDesc;
    badDepthDesc.depthStoreOp = static_cast<StoreOp>(0xFFU);
    badDepthStore->BeginRenderPass(badDepthDesc);
    device.Submit(std::move(badDepthStore));
    EXPECT_TRUE(device.GetLastSubmittedCommands().empty());

    std::unique_ptr<ICommandBuffer> validCommandBuffer = device.CreateCommandBuffer();
    validCommandBuffer->BeginRenderPass(RenderPassDesc{});
    validCommandBuffer->EndRenderPass();
    device.Submit(std::move(validCommandBuffer));
    EXPECT_EQ(device.GetLastSubmittedCommands().size(), 2U);
}

TEST(NullCommandBufferTest, BeginRenderPass_ExtrasRoundTrip_RecordsSlotsVerbatim) {
    // Tier 2.4/2.5: a two-extra prefix plus non-default store ops records opaquely and reads
    // back verbatim. The defaults pinned first are the zero-behavior-change contract (the
    // renderer's 73 pass sites never set these fields).
    const RenderPassDesc defaults;
    EXPECT_EQ(defaults.colorStoreOp, StoreOp::Store);
    EXPECT_EQ(defaults.depthStoreOp, StoreOp::Store);
    for (const ColorAttachment& slot : defaults.extraColorAttachments) {
        EXPECT_EQ(slot.target, kInvalidTextureHandle);
    }

    NullRenderDevice device;
    const TextureHandle color = device.CreateTexture(TextureDesc{4U, 4U});
    ASSERT_NE(color, kInvalidTextureHandle);
    const TextureHandle depth =
        device.CreateTexture(TextureDesc{4U, 4U, TextureFormat::Depth32Float, 1U});
    ASSERT_NE(depth, kInvalidTextureHandle);
    const TextureHandle extra0 = device.CreateTexture(TextureDesc{4U, 4U});
    ASSERT_NE(extra0, kInvalidTextureHandle);
    const TextureHandle extra1 = device.CreateTexture(TextureDesc{4U, 4U});
    ASSERT_NE(extra1, kInvalidTextureHandle);

    auto commandBuffer = device.CreateCommandBuffer();
    RenderPassDesc passDesc;
    passDesc.colorAttachment = color;
    passDesc.depthAttachment = depth;
    passDesc.colorStoreOp = StoreOp::Store;
    passDesc.depthStoreOp = StoreOp::DontCare;
    passDesc.extraColorAttachments[0].target = extra0;
    passDesc.extraColorAttachments[0].layer = 1U;
    passDesc.extraColorAttachments[0].loadOp = LoadOp::Load;
    passDesc.extraColorAttachments[0].storeOp = StoreOp::DontCare;
    passDesc.extraColorAttachments[0].clearColor = {0.25F, 0.5F, 0.75F, 1.0F};
    passDesc.extraColorAttachments[1].target = extra1;
    passDesc.extraColorAttachments[1].layer = 0U;
    passDesc.extraColorAttachments[1].loadOp = LoadOp::Clear;
    passDesc.extraColorAttachments[1].storeOp = StoreOp::Store;
    passDesc.extraColorAttachments[1].clearColor = {1.0F, 0.0F, 0.0F, 1.0F};
    commandBuffer->BeginRenderPass(passDesc);
    commandBuffer->EndRenderPass();
    device.Submit(std::move(commandBuffer));

    const std::vector<RecordedCommand>& recorded = device.GetLastSubmittedCommands();
    ASSERT_EQ(recorded.size(), 2U);
    ASSERT_TRUE(std::holds_alternative<BeginRenderPassCommand>(recorded[0]));
    const RenderPassDesc& roundTripped = std::get<BeginRenderPassCommand>(recorded[0]).desc;
    EXPECT_EQ(roundTripped.colorAttachment, color);
    EXPECT_EQ(roundTripped.depthAttachment, depth);
    EXPECT_EQ(roundTripped.colorStoreOp, StoreOp::Store);
    EXPECT_EQ(roundTripped.depthStoreOp, StoreOp::DontCare);
    EXPECT_EQ(roundTripped.extraColorAttachments[0].target, extra0);
    EXPECT_EQ(roundTripped.extraColorAttachments[0].layer, 1U);
    EXPECT_EQ(roundTripped.extraColorAttachments[0].loadOp, LoadOp::Load);
    EXPECT_EQ(roundTripped.extraColorAttachments[0].storeOp, StoreOp::DontCare);
    EXPECT_FLOAT_EQ(roundTripped.extraColorAttachments[0].clearColor[0], 0.25F);
    EXPECT_FLOAT_EQ(roundTripped.extraColorAttachments[0].clearColor[1], 0.5F);
    EXPECT_FLOAT_EQ(roundTripped.extraColorAttachments[0].clearColor[2], 0.75F);
    EXPECT_FLOAT_EQ(roundTripped.extraColorAttachments[0].clearColor[3], 1.0F);
    EXPECT_EQ(roundTripped.extraColorAttachments[1].target, extra1);
    EXPECT_EQ(roundTripped.extraColorAttachments[1].layer, 0U);
    EXPECT_EQ(roundTripped.extraColorAttachments[1].loadOp, LoadOp::Clear);
    EXPECT_EQ(roundTripped.extraColorAttachments[1].storeOp, StoreOp::Store);
    EXPECT_EQ(roundTripped.extraColorAttachments[2].target, kInvalidTextureHandle);

    device.DestroyTexture(color);
    device.DestroyTexture(depth);
    device.DestroyTexture(extra0);
    device.DestroyTexture(extra1);
}

TEST(NullCommandBufferTest, BeginRenderPass_GappyExtras_DoesNotRecordOrEnterPass) {
    // Tier 2.4: location 2 set while 1 is empty is malformed on every backend — Null refuses
    // to record or enter it, then proves the device is unaffected with a valid prefix.
    NullRenderDevice device;
    const TextureHandle extra = device.CreateTexture(TextureDesc{4U, 4U});
    ASSERT_NE(extra, kInvalidTextureHandle);

    std::unique_ptr<ICommandBuffer> gappyCommandBuffer = device.CreateCommandBuffer();
    RenderPassDesc gappyDesc;
    gappyDesc.extraColorAttachments[0].target = extra;
    gappyDesc.extraColorAttachments[2].target = extra;
    gappyCommandBuffer->BeginRenderPass(gappyDesc);
    device.Submit(std::move(gappyCommandBuffer));
    EXPECT_TRUE(device.GetLastSubmittedCommands().empty());

    std::unique_ptr<ICommandBuffer> validCommandBuffer = device.CreateCommandBuffer();
    RenderPassDesc validDesc;
    validDesc.extraColorAttachments[0].target = extra;
    validCommandBuffer->BeginRenderPass(validDesc);
    validCommandBuffer->EndRenderPass();
    device.Submit(std::move(validCommandBuffer));
    EXPECT_EQ(device.GetLastSubmittedCommands().size(), 2U);

    device.DestroyTexture(extra);
}

TEST(NullCommandBufferTest, BeginRenderPass_UnknownExtraStoreOp_DoesNotRecordOrEnterPass) {
    // Tier 2.4/2.5: per-slot ops validate like the location-0 ops — a garbage extra store enum
    // refuses to record or enter.
    NullRenderDevice device;
    const TextureHandle extra = device.CreateTexture(TextureDesc{4U, 4U});
    ASSERT_NE(extra, kInvalidTextureHandle);

    std::unique_ptr<ICommandBuffer> invalidCommandBuffer = device.CreateCommandBuffer();
    RenderPassDesc invalidDesc;
    invalidDesc.extraColorAttachments[0].target = extra;
    invalidDesc.extraColorAttachments[0].storeOp = static_cast<StoreOp>(0xFFU);
    invalidCommandBuffer->BeginRenderPass(invalidDesc);
    device.Submit(std::move(invalidCommandBuffer));
    EXPECT_TRUE(device.GetLastSubmittedCommands().empty());

    std::unique_ptr<ICommandBuffer> validCommandBuffer = device.CreateCommandBuffer();
    validCommandBuffer->BeginRenderPass(RenderPassDesc{});
    validCommandBuffer->EndRenderPass();
    device.Submit(std::move(validCommandBuffer));
    EXPECT_EQ(device.GetLastSubmittedCommands().size(), 2U);

    device.DestroyTexture(extra);
}

} // namespace
} // namespace UVE::Render::Tests
