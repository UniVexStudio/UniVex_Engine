// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include "bindless_texture_table.h"
#include "null_render_device.h"
#include "shader_cross_compiler.h"

namespace UVE::Render {
namespace {

TEST(BindlessTextureTableTest, RegistersRecyclesAndAutoUnregistersOnDestroy) {
    EXPECT_EQ(RenderDeviceCapabilities{}.bindlessTextures, RenderFeatureSupport::Unsupported);

    NullRenderDevice device;
    EXPECT_TRUE(device.SupportsBindlessTextures());
    EXPECT_EQ(device.GetCapabilities().bindlessTextures, RenderFeatureSupport::Model);
    EXPECT_EQ(device.GetCapabilities().maxBindlessTextures, kDefaultMaxBindlessTextures);

    TextureDesc desc{};
    desc.width = 4U;
    desc.height = 4U;
    desc.format = TextureFormat::RGBA8Unorm;
    const TextureHandle texA = device.CreateTexture(desc);
    const TextureHandle texB = device.CreateTexture(desc);
    ASSERT_NE(texA, kInvalidTextureHandle);
    ASSERT_NE(texB, kInvalidTextureHandle);

    const std::uint32_t slotA = device.RegisterBindlessTexture(texA);
    const std::uint32_t slotA2 = device.RegisterBindlessTexture(texA);
    const std::uint32_t slotB = device.RegisterBindlessTexture(texB);
    EXPECT_EQ(slotA, slotA2);
    EXPECT_NE(slotA, kInvalidBindlessTextureIndex);
    EXPECT_NE(slotB, kInvalidBindlessTextureIndex);
    EXPECT_NE(slotA, slotB);

    // Destroying texA releases its slot so a new texture recycles it.
    device.DestroyTexture(texA);
    EXPECT_EQ(device.RegisterBindlessTexture(texA), kInvalidBindlessTextureIndex);

    const TextureHandle texC = device.CreateTexture(desc);
    const std::uint32_t slotC = device.RegisterBindlessTexture(texC);
    EXPECT_EQ(slotC, slotA);

    device.DestroyTexture(texB);
    device.DestroyTexture(texC);
}

TEST(ShaderCrossCompilerTest, TranspilesSingleSourceForAllDialects) {
    const std::string_view src =
        "#version 330 core\n"
        "#ifdef VERTEX_SHADER\nvoid main() {}\n#endif\n"
        "#ifdef FRAGMENT_SHADER\nvoid main() {}\n#endif\n";

    const auto vk = Shader::TranspileShaderSourceForDialect(
        src, ShaderStage::Fragment, Shader::ShaderTargetDialect::VulkanGlsl450);
    ASSERT_TRUE(vk.ok);
    EXPECT_NE(vk.source.find("#version 450"), std::string::npos);
    EXPECT_NE(vk.source.find("#define UVE_VULKAN 1"), std::string::npos);
    EXPECT_NE(vk.source.find("#define FRAGMENT_SHADER 1"), std::string::npos);
    EXPECT_EQ(vk.source.find("#version 330"), std::string::npos);

    const auto es = Shader::TranspileShaderSourceForDialect(
        src, ShaderStage::Vertex, Shader::ShaderTargetDialect::OpenGLES300);
    ASSERT_TRUE(es.ok);
    EXPECT_NE(es.source.find("#version 300 es"), std::string::npos);
    EXPECT_NE(es.source.find("precision highp float;"), std::string::npos);
    EXPECT_NE(es.source.find("#define VERTEX_SHADER 1"), std::string::npos);
}

} // namespace
} // namespace UVE::Render
