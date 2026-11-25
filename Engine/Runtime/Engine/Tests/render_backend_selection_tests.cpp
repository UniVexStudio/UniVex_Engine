// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <render_backend_selection.h>

namespace UVE::Core::Tests {
namespace {

TEST(RenderBackendSelectionTest, AutoAndOpenGlPreferencesKeepOpenGlAsTheDefault) {
    constexpr RenderBackendAttemptOrder autoOrder =
        ResolveRenderBackendAttemptOrder(RenderBackendPreference::Auto);
    constexpr RenderBackendAttemptOrder openGlOrder =
        ResolveRenderBackendAttemptOrder(RenderBackendPreference::OpenGL);
    constexpr RenderBackendAttemptOrder expected{{RenderBackendKind::OpenGL,
                                                      RenderBackendKind::Null,
                                                      RenderBackendKind::Vulkan},
                                                     2U};
    EXPECT_EQ(autoOrder, expected);
    EXPECT_EQ(openGlOrder, expected);
}

TEST(RenderBackendSelectionTest, VulkanFallsBackThroughOpenGlBeforeNull) {
    constexpr RenderBackendAttemptOrder order =
        ResolveRenderBackendAttemptOrder(RenderBackendPreference::Vulkan);
    EXPECT_EQ(order.backends[0], RenderBackendKind::Vulkan);
    EXPECT_EQ(order.backends[1], RenderBackendKind::OpenGL);
    EXPECT_EQ(order.backends[2], RenderBackendKind::Null);
    EXPECT_EQ(order.count, 3U);
}

TEST(RenderBackendSelectionTest, NullPreferenceSkipsRealBackends) {
    constexpr RenderBackendAttemptOrder order =
        ResolveRenderBackendAttemptOrder(RenderBackendPreference::Null);
    EXPECT_EQ(order.backends[0], RenderBackendKind::Null);
    EXPECT_EQ(order.count, 1U);
    EXPECT_TRUE(order.Contains(RenderBackendKind::Null));
    EXPECT_FALSE(order.Contains(RenderBackendKind::OpenGL));
    EXPECT_FALSE(order.Contains(RenderBackendKind::Vulkan));
}

TEST(RenderBackendSelectionTest, InvalidPreferenceUsesSafeOpenGlFallbackOrder) {
    constexpr RenderBackendAttemptOrder order = ResolveRenderBackendAttemptOrder(
        static_cast<RenderBackendPreference>(255U));
    EXPECT_EQ(order.backends[0], RenderBackendKind::OpenGL);
    EXPECT_EQ(order.backends[1], RenderBackendKind::Null);
    EXPECT_EQ(order.count, 2U);
}

} // namespace
} // namespace UVE::Core::Tests
