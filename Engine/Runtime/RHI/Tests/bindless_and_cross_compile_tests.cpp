// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include "bindless_texture_table.h"
#include "null_render_device.h"
#include "shader_cross_compiler.h"

namespace UVE::Render {
namespace {

TEST(BindlessTextureTableUVETest, RegistersRecyclesAndAutoUnregistersOnDestroy) {
    EXPECT_EQ(RenderDeviceCapabilitiesUVE{}.bindlessTextures, RenderFeatureSupportUVE::Unsupported);

    NullRenderDeviceUVE device;
    EXPECT_TRUE(device.SupportsBindlessTexturesUVE());
    EXPECT_EQ(device.GetCapabilitiesUVE().bindlessTextures, RenderFeatureSupportUVE::Model);
    EXPECT_EQ(device.GetCapabilitiesUVE().maxBindlessTextures, kDefaultMaxBindlessTexturesUVE);

    TextureDescUVE desc{};
    desc.width = 4U;
    desc.height = 4U;
    desc.format = TextureFormatUVE::RGBA8Unorm;
    const TextureHandleUVE texA = device.CreateTextureUVE(desc);
    const TextureHandleUVE texB = device.CreateTextureUVE(desc);
    ASSERT_TRUE(texA.IsValidUVE());
    ASSERT_TRUE(texB.IsValidUVE());

    const std::uint32_t slotA = device.RegisterBindlessTextureUVE(texA);
    const std::uint32_t slotA2 = device.RegisterBindlessTextureUVE(texA);
    const std::uint32_t slotB = device.RegisterBindlessTextureUVE(texB);
    EXPECT_EQ(slotA, slotA2);
    EXPECT_NE(slotA, kInvalidBindlessTextureIndexUVE);
    EXPECT_NE(slotB, kInvalidBindlessTextureIndexUVE);
    EXPECT_NE(slotA, slotB);

    // Destroying texA releases its slot so a new texture recycles it.
    device.DestroyTextureUVE(texA);
    EXPECT_EQ(device.RegisterBindlessTextureUVE(texA), kInvalidBindlessTextureIndexUVE);

    const TextureHandleUVE texC = device.CreateTextureUVE(desc);
    const std::uint32_t slotC = device.RegisterBindlessTextureUVE(texC);
    EXPECT_EQ(slotC, slotA);

    device.DestroyTextureUVE(texB);
    device.DestroyTextureUVE(texC);
}

TEST(ShaderCrossCompilerUVETest, TranspilesSingleSourceForAllDialects) {
    const std::string_view src =
        "#version 330 core\n"
        "#ifdef VERTEX_SHADER\nvoid main() {}\n#endif\n"
        "#ifdef FRAGMENT_SHADER\nvoid main() {}\n#endif\n";

    const auto vk = Shader::TranspileShaderSourceForDialectUVE(
        src, ShaderStageUVE::Fragment, Shader::ShaderTargetDialectUVE::VulkanGlsl450);
    ASSERT_TRUE(vk.ok);
    EXPECT_NE(vk.source.find("#version 450"), std::string::npos);
    EXPECT_NE(vk.source.find("#define UVE_VULKAN 1"), std::string::npos);
    EXPECT_NE(vk.source.find("#define FRAGMENT_SHADER 1"), std::string::npos);
    EXPECT_EQ(vk.source.find("#version 330"), std::string::npos);

    const auto es = Shader::TranspileShaderSourceForDialectUVE(
        src, ShaderStageUVE::Vertex, Shader::ShaderTargetDialectUVE::OpenGLES300);
    ASSERT_TRUE(es.ok);
    EXPECT_NE(es.source.find("#version 300 es"), std::string::npos);
    EXPECT_NE(es.source.find("precision highp float;"), std::string::npos);
    EXPECT_NE(es.source.find("#define VERTEX_SHADER 1"), std::string::npos);
}

} // namespace
} // namespace UVE::Render
