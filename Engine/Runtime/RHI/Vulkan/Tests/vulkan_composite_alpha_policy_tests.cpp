// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "vulkan_composite_alpha_policy.h"

#include <gtest/gtest.h>

namespace UVE::Render::Vulkan::Tests {
namespace {

TEST(VulkanCompositeAlphaPolicyTest, OpaqueIsPreferredWhenTransparencyIsNotRequested) {
    EXPECT_EQ(ResolveVulkanCompositeAlpha(false, CompositeAlphaAvailability{true, true, true, true}),
              (CompositeAlphaResolution{CompositeAlphaMode::Opaque, CompositeAlphaFallback::None}));
}

TEST(VulkanCompositeAlphaPolicyTest, TransparencyPrefersPremultipliedThenPostmultipliedThenInherit) {
    const CompositeAlphaAvailability allModes{true, true, true, true};
    EXPECT_EQ(ResolveVulkanCompositeAlpha(true, allModes),
              (CompositeAlphaResolution{CompositeAlphaMode::PreMultiplied,
                                           CompositeAlphaFallback::None}));
    EXPECT_EQ(ResolveVulkanCompositeAlpha(true, CompositeAlphaAvailability{true, false, true, true}),
              (CompositeAlphaResolution{CompositeAlphaMode::PostMultiplied,
                                           CompositeAlphaFallback::None}));
    EXPECT_EQ(ResolveVulkanCompositeAlpha(true, CompositeAlphaAvailability{true, false, false, true}),
              (CompositeAlphaResolution{CompositeAlphaMode::Inherit, CompositeAlphaFallback::None}));
}

TEST(VulkanCompositeAlphaPolicyTest, MissingTransparentModeFallsBackToOpaqueAndReportsIt) {
    EXPECT_EQ(ResolveVulkanCompositeAlpha(true, CompositeAlphaAvailability{true, false, false, false}),
              (CompositeAlphaResolution{CompositeAlphaMode::Opaque,
                                           CompositeAlphaFallback::TransparencyUnavailable}));
    EXPECT_EQ(ResolveVulkanCompositeAlpha(true, CompositeAlphaAvailability{}),
              (CompositeAlphaResolution{std::nullopt,
                                           CompositeAlphaFallback::TransparencyUnavailable}));
}

TEST(VulkanCompositeAlphaPolicyTest, MissingOpaqueModeUsesTheFirstSupportedFallback) {
    EXPECT_EQ(ResolveVulkanCompositeAlpha(false, CompositeAlphaAvailability{false, true, true, true}),
              (CompositeAlphaResolution{CompositeAlphaMode::PreMultiplied,
                                           CompositeAlphaFallback::OpaqueUnavailable}));
    EXPECT_EQ(ResolveVulkanCompositeAlpha(false, CompositeAlphaAvailability{false, false, true, true}),
              (CompositeAlphaResolution{CompositeAlphaMode::PostMultiplied,
                                           CompositeAlphaFallback::OpaqueUnavailable}));
    EXPECT_EQ(ResolveVulkanCompositeAlpha(false, CompositeAlphaAvailability{false, false, false, true}),
              (CompositeAlphaResolution{CompositeAlphaMode::Inherit,
                                           CompositeAlphaFallback::OpaqueUnavailable}));
}

TEST(VulkanCompositeAlphaPolicyTest, FailsWhenNoSurfaceAlphaModeIsSupported) {
    EXPECT_EQ(ResolveVulkanCompositeAlpha(false, CompositeAlphaAvailability{}),
              (CompositeAlphaResolution{std::nullopt, CompositeAlphaFallback::NoSupportedMode}));
}

} // namespace
} // namespace UVE::Render::Vulkan::Tests
