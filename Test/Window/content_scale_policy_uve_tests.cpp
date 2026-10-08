// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/window/content_scale_policy_uve.h"

#include <limits>
#include <optional>

#include <gtest/gtest.h>

namespace UVE::Window::Tests {
namespace {

TEST(ContentScalePolicyUVETest, PositiveOverrideResolvesUniformlyAndZeroMeansAutomatic) {
    const std::optional<float> overrideScale = ResolveContentScaleOverrideUVE(1.75);
    ASSERT_TRUE(overrideScale.has_value());
    EXPECT_FLOAT_EQ(*overrideScale, 1.75F);
    const std::optional<float> maximumOverride =
        ResolveContentScaleOverrideUVE(Platform::kMaximumContentScaleOverrideUVE);
    ASSERT_TRUE(maximumOverride.has_value());
    EXPECT_FLOAT_EQ(*maximumOverride, 4.0F);
    EXPECT_FALSE(ResolveContentScaleOverrideUVE(0.0).has_value());
}

TEST(ContentScalePolicyUVETest, InvalidOverridesAreIgnored) {
    EXPECT_FALSE(ResolveContentScaleOverrideUVE(-0.1).has_value());
    EXPECT_FALSE(ResolveContentScaleOverrideUVE(Platform::kMaximumContentScaleOverrideUVE + 0.01).has_value());
    EXPECT_FALSE(ResolveContentScaleOverrideUVE(std::numeric_limits<double>::quiet_NaN()).has_value());
    EXPECT_FALSE(ResolveContentScaleOverrideUVE(std::numeric_limits<double>::infinity()).has_value());
    EXPECT_FALSE(ResolveContentScaleOverrideUVE(std::numeric_limits<double>::denorm_min()).has_value());
}

TEST(ContentScalePolicyUVETest, ValidNativeScalePreservesIndependentAxes) {
    EXPECT_EQ(SanitizeNativeContentScaleUVE(1.25F, 2.0F), (ContentScaleUVE{1.25F, 2.0F}));
}

TEST(ContentScalePolicyUVETest, InvalidNativeAxisFallsBackToOneOnBothAxes) {
    EXPECT_EQ(SanitizeNativeContentScaleUVE(0.0F, 1.5F), ContentScaleUVE{});
    EXPECT_EQ(SanitizeNativeContentScaleUVE(1.5F, -1.0F), ContentScaleUVE{});
    EXPECT_EQ(SanitizeNativeContentScaleUVE(std::numeric_limits<float>::quiet_NaN(), 1.5F),
              ContentScaleUVE{});
    EXPECT_EQ(SanitizeNativeContentScaleUVE(1.5F, std::numeric_limits<float>::infinity()),
              ContentScaleUVE{});
}

} // namespace
} // namespace UVE::Window::Tests
