// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <uve/core/render_backend_selection_uve.h>

namespace UVE::Core::Tests {
namespace {

TEST(RenderBackendSelectionUVETest, AutoAndOpenGlPreferencesKeepOpenGlAsTheDefault) {
    constexpr RenderBackendAttemptOrderUVE autoOrder =
        ResolveRenderBackendAttemptOrderUVE(RenderBackendPreferenceUVE::AutoUVE);
    constexpr RenderBackendAttemptOrderUVE openGlOrder =
        ResolveRenderBackendAttemptOrderUVE(RenderBackendPreferenceUVE::OpenGLUVE);
    constexpr RenderBackendAttemptOrderUVE expected{{RenderBackendKindUVE::OpenGL,
                                                      RenderBackendKindUVE::Null,
                                                      RenderBackendKindUVE::Vulkan},
                                                     2U};
    EXPECT_EQ(autoOrder, expected);
    EXPECT_EQ(openGlOrder, expected);
}

TEST(RenderBackendSelectionUVETest, VulkanFallsBackThroughOpenGlBeforeNull) {
    constexpr RenderBackendAttemptOrderUVE order =
        ResolveRenderBackendAttemptOrderUVE(RenderBackendPreferenceUVE::VulkanUVE);
    EXPECT_EQ(order.backends[0], RenderBackendKindUVE::Vulkan);
    EXPECT_EQ(order.backends[1], RenderBackendKindUVE::OpenGL);
    EXPECT_EQ(order.backends[2], RenderBackendKindUVE::Null);
    EXPECT_EQ(order.count, 3U);
}

TEST(RenderBackendSelectionUVETest, NullPreferenceSkipsRealBackends) {
    constexpr RenderBackendAttemptOrderUVE order =
        ResolveRenderBackendAttemptOrderUVE(RenderBackendPreferenceUVE::NullUVE);
    EXPECT_EQ(order.backends[0], RenderBackendKindUVE::Null);
    EXPECT_EQ(order.count, 1U);
    EXPECT_TRUE(order.Contains(RenderBackendKindUVE::Null));
    EXPECT_FALSE(order.Contains(RenderBackendKindUVE::OpenGL));
    EXPECT_FALSE(order.Contains(RenderBackendKindUVE::Vulkan));
}

TEST(RenderBackendSelectionUVETest, InvalidPreferenceUsesSafeOpenGlFallbackOrder) {
    constexpr RenderBackendAttemptOrderUVE order = ResolveRenderBackendAttemptOrderUVE(
        static_cast<RenderBackendPreferenceUVE>(255U));
    EXPECT_EQ(order.backends[0], RenderBackendKindUVE::OpenGL);
    EXPECT_EQ(order.backends[1], RenderBackendKindUVE::Null);
    EXPECT_EQ(order.count, 2U);
}

} // namespace
} // namespace UVE::Core::Tests
