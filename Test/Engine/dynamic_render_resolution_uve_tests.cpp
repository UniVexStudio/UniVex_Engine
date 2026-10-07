// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cmath>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "uve/core/dynamic_render_resolution_uve.h"

namespace UVE::Core {
namespace {

TEST(DynamicRenderResolutionControllerUVETest, ConfiguresAtMaximumAndIgnoresInvalidSamples) {
    DynamicRenderResolutionControllerUVE controller;
    ASSERT_TRUE(controller.ConfigureUVE({0.5, 0.9, 16.6667}));
    EXPECT_DOUBLE_EQ(controller.GetCurrentScaleUVE(), 0.9);
    EXPECT_FALSE(controller.ObserveFrameTimeUVE(0.0));
    EXPECT_FALSE(controller.ObserveFrameTimeUVE(-0.016));
    EXPECT_FALSE(controller.ObserveFrameTimeUVE(std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(controller.ObserveFrameTimeUVE(std::numeric_limits<double>::quiet_NaN()));
    EXPECT_DOUBLE_EQ(controller.GetCurrentScaleUVE(), 0.9);
    EXPECT_DOUBLE_EQ(controller.GetSmoothedFrameTimeMillisecondsUVE(), 0.0);
}

TEST(DynamicRenderResolutionControllerUVETest, RejectsInvalidConfigurationWithoutChangingActiveState) {
    DynamicRenderResolutionControllerUVE controller;
    ASSERT_TRUE(controller.ConfigureUVE({0.5, 1.0, 16.6667}));
    EXPECT_FALSE(controller.ConfigureUVE({0.8, 0.7, 16.6667}));
    EXPECT_FALSE(controller.ConfigureUVE({0.5, 1.01, 16.6667}));
    EXPECT_FALSE(controller.ConfigureUVE({0.5, 1.0, 0.0}));
    EXPECT_DOUBLE_EQ(controller.GetCurrentScaleUVE(), 1.0);
}

TEST(DynamicRenderResolutionControllerUVETest, SustainedSlowFramesDownscaleAndRespectMinimum) {
    DynamicRenderResolutionControllerUVE controller;
    ASSERT_TRUE(controller.ConfigureUVE({0.9, 1.0, 16.6667}));

    bool changed = false;
    for (std::uint32_t frame = 0U; frame < 32U; ++frame) {
        changed = controller.ObserveFrameTimeUVE(0.040) || changed;
    }

    EXPECT_TRUE(changed);
    EXPECT_NEAR(controller.GetCurrentScaleUVE(), 0.9, 1e-12);
}

TEST(DynamicRenderResolutionControllerUVETest, SustainedFastFramesRestoreQualityGradually) {
    DynamicRenderResolutionControllerUVE controller;
    ASSERT_TRUE(controller.ConfigureUVE({0.5, 0.7, 16.6667}));
    for (std::uint32_t frame = 0U; frame < 8U; ++frame) {
        static_cast<void>(controller.ObserveFrameTimeUVE(0.030));
    }
    EXPECT_NEAR(controller.GetCurrentScaleUVE(), 0.65, 1e-12);

    for (std::uint32_t frame = 0U; frame < 100U; ++frame) {
        static_cast<void>(controller.ObserveFrameTimeUVE(0.005));
    }
    EXPECT_DOUBLE_EQ(controller.GetCurrentScaleUVE(), 0.7);
}

TEST(DynamicRenderResolutionControllerUVETest, HysteresisKeepsScaleStableNearTarget) {
    DynamicRenderResolutionControllerUVE controller;
    ASSERT_TRUE(controller.ConfigureUVE({0.5, 1.0, 16.6667}));

    for (std::uint32_t frame = 0U; frame < 120U; ++frame) {
        static_cast<void>(controller.ObserveFrameTimeUVE(0.0168));
    }
    EXPECT_DOUBLE_EQ(controller.GetCurrentScaleUVE(), 1.0);
}

} // namespace
} // namespace UVE::Core
