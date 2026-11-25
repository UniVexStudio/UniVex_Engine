// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "content_scale_policy.h"

#include <limits>
#include <optional>

#include <gtest/gtest.h>

namespace UVE::Window::Tests {
namespace {

TEST(ContentScalePolicyTest, PositiveOverrideResolvesUniformlyAndZeroMeansAutomatic) {
    const std::optional<float> overrideScale = ResolveContentScaleOverride(1.75);
    ASSERT_TRUE(overrideScale.has_value());
    EXPECT_FLOAT_EQ(*overrideScale, 1.75F);
    const std::optional<float> maximumOverride =
        ResolveContentScaleOverride(Platform::kMaximumContentScaleOverride);
    ASSERT_TRUE(maximumOverride.has_value());
    EXPECT_FLOAT_EQ(*maximumOverride, 4.0F);
    EXPECT_FALSE(ResolveContentScaleOverride(0.0).has_value());
}

TEST(ContentScalePolicyTest, InvalidOverridesAreIgnored) {
    EXPECT_FALSE(ResolveContentScaleOverride(-0.1).has_value());
    EXPECT_FALSE(ResolveContentScaleOverride(Platform::kMaximumContentScaleOverride + 0.01).has_value());
    EXPECT_FALSE(ResolveContentScaleOverride(std::numeric_limits<double>::quiet_NaN()).has_value());
    EXPECT_FALSE(ResolveContentScaleOverride(std::numeric_limits<double>::infinity()).has_value());
    EXPECT_FALSE(ResolveContentScaleOverride(std::numeric_limits<double>::denorm_min()).has_value());
}

TEST(ContentScalePolicyTest, ValidNativeScalePreservesIndependentAxes) {
    EXPECT_EQ(SanitizeNativeContentScale(1.25F, 2.0F), (ContentScale{1.25F, 2.0F}));
}

TEST(ContentScalePolicyTest, InvalidNativeAxisFallsBackToOneOnBothAxes) {
    EXPECT_EQ(SanitizeNativeContentScale(0.0F, 1.5F), ContentScale{});
    EXPECT_EQ(SanitizeNativeContentScale(1.5F, -1.0F), ContentScale{});
    EXPECT_EQ(SanitizeNativeContentScale(std::numeric_limits<float>::quiet_NaN(), 1.5F),
              ContentScale{});
    EXPECT_EQ(SanitizeNativeContentScale(1.5F, std::numeric_limits<float>::infinity()),
              ContentScale{});
}

} // namespace
} // namespace UVE::Window::Tests
