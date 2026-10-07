// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/vulkan/vulkan_composite_alpha_policy_uve.h"

#include <gtest/gtest.h>

namespace UVE::Render::Vulkan::Tests {
namespace {

TEST(VulkanCompositeAlphaPolicyUVETest, OpaqueIsPreferredWhenTransparencyIsNotRequested) {
    EXPECT_EQ(ResolveVulkanCompositeAlphaUVE(false, CompositeAlphaAvailabilityUVE{true, true, true, true}),
              (CompositeAlphaResolutionUVE{CompositeAlphaModeUVE::Opaque, CompositeAlphaFallbackUVE::None}));
}

TEST(VulkanCompositeAlphaPolicyUVETest, TransparencyPrefersPremultipliedThenPostmultipliedThenInherit) {
    const CompositeAlphaAvailabilityUVE allModes{true, true, true, true};
    EXPECT_EQ(ResolveVulkanCompositeAlphaUVE(true, allModes),
              (CompositeAlphaResolutionUVE{CompositeAlphaModeUVE::PreMultiplied,
                                           CompositeAlphaFallbackUVE::None}));
    EXPECT_EQ(ResolveVulkanCompositeAlphaUVE(true, CompositeAlphaAvailabilityUVE{true, false, true, true}),
              (CompositeAlphaResolutionUVE{CompositeAlphaModeUVE::PostMultiplied,
                                           CompositeAlphaFallbackUVE::None}));
    EXPECT_EQ(ResolveVulkanCompositeAlphaUVE(true, CompositeAlphaAvailabilityUVE{true, false, false, true}),
              (CompositeAlphaResolutionUVE{CompositeAlphaModeUVE::Inherit, CompositeAlphaFallbackUVE::None}));
}

TEST(VulkanCompositeAlphaPolicyUVETest, MissingTransparentModeFallsBackToOpaqueAndReportsIt) {
    EXPECT_EQ(ResolveVulkanCompositeAlphaUVE(true, CompositeAlphaAvailabilityUVE{true, false, false, false}),
              (CompositeAlphaResolutionUVE{CompositeAlphaModeUVE::Opaque,
                                           CompositeAlphaFallbackUVE::TransparencyUnavailable}));
    EXPECT_EQ(ResolveVulkanCompositeAlphaUVE(true, CompositeAlphaAvailabilityUVE{}),
              (CompositeAlphaResolutionUVE{std::nullopt,
                                           CompositeAlphaFallbackUVE::TransparencyUnavailable}));
}

TEST(VulkanCompositeAlphaPolicyUVETest, MissingOpaqueModeUsesTheFirstSupportedFallback) {
    EXPECT_EQ(ResolveVulkanCompositeAlphaUVE(false, CompositeAlphaAvailabilityUVE{false, true, true, true}),
              (CompositeAlphaResolutionUVE{CompositeAlphaModeUVE::PreMultiplied,
                                           CompositeAlphaFallbackUVE::OpaqueUnavailable}));
    EXPECT_EQ(ResolveVulkanCompositeAlphaUVE(false, CompositeAlphaAvailabilityUVE{false, false, true, true}),
              (CompositeAlphaResolutionUVE{CompositeAlphaModeUVE::PostMultiplied,
                                           CompositeAlphaFallbackUVE::OpaqueUnavailable}));
    EXPECT_EQ(ResolveVulkanCompositeAlphaUVE(false, CompositeAlphaAvailabilityUVE{false, false, false, true}),
              (CompositeAlphaResolutionUVE{CompositeAlphaModeUVE::Inherit,
                                           CompositeAlphaFallbackUVE::OpaqueUnavailable}));
}

TEST(VulkanCompositeAlphaPolicyUVETest, FailsWhenNoSurfaceAlphaModeIsSupported) {
    EXPECT_EQ(ResolveVulkanCompositeAlphaUVE(false, CompositeAlphaAvailabilityUVE{}),
              (CompositeAlphaResolutionUVE{std::nullopt, CompositeAlphaFallbackUVE::NoSupportedMode}));
}

} // namespace
} // namespace UVE::Render::Vulkan::Tests
