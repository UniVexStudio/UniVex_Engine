// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cmath>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "dynamic_render_resolution.h"

namespace UVE::Core {
namespace {

TEST(DynamicRenderResolutionControllerTest, ConfiguresAtMaximumAndIgnoresInvalidSamples) {
    DynamicRenderResolutionController controller;
    ASSERT_TRUE(controller.Configure({0.5, 0.9, 16.6667}));
    EXPECT_DOUBLE_EQ(controller.GetCurrentScale(), 0.9);
    EXPECT_FALSE(controller.ObserveFrameTime(0.0));
    EXPECT_FALSE(controller.ObserveFrameTime(-0.016));
    EXPECT_FALSE(controller.ObserveFrameTime(std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(controller.ObserveFrameTime(std::numeric_limits<double>::quiet_NaN()));
    EXPECT_DOUBLE_EQ(controller.GetCurrentScale(), 0.9);
    EXPECT_DOUBLE_EQ(controller.GetSmoothedFrameTimeMilliseconds(), 0.0);
}

TEST(DynamicRenderResolutionControllerTest, RejectsInvalidConfigurationWithoutChangingActiveState) {
    DynamicRenderResolutionController controller;
    ASSERT_TRUE(controller.Configure({0.5, 1.0, 16.6667}));
    EXPECT_FALSE(controller.Configure({0.8, 0.7, 16.6667}));
    EXPECT_FALSE(controller.Configure({0.5, 1.01, 16.6667}));
    EXPECT_FALSE(controller.Configure({0.5, 1.0, 0.0}));
    EXPECT_DOUBLE_EQ(controller.GetCurrentScale(), 1.0);
}

TEST(DynamicRenderResolutionControllerTest, SustainedSlowFramesDownscaleAndRespectMinimum) {
    DynamicRenderResolutionController controller;
    ASSERT_TRUE(controller.Configure({0.9, 1.0, 16.6667}));

    bool changed = false;
    for (std::uint32_t frame = 0U; frame < 32U; ++frame) {
        changed = controller.ObserveFrameTime(0.040) || changed;
    }

    EXPECT_TRUE(changed);
    EXPECT_NEAR(controller.GetCurrentScale(), 0.9, 1e-12);
}

TEST(DynamicRenderResolutionControllerTest, SustainedFastFramesRestoreQualityGradually) {
    DynamicRenderResolutionController controller;
    ASSERT_TRUE(controller.Configure({0.5, 0.7, 16.6667}));
    for (std::uint32_t frame = 0U; frame < 8U; ++frame) {
        static_cast<void>(controller.ObserveFrameTime(0.030));
    }
    EXPECT_NEAR(controller.GetCurrentScale(), 0.65, 1e-12);

    for (std::uint32_t frame = 0U; frame < 100U; ++frame) {
        static_cast<void>(controller.ObserveFrameTime(0.005));
    }
    EXPECT_DOUBLE_EQ(controller.GetCurrentScale(), 0.7);
}

TEST(DynamicRenderResolutionControllerTest, HysteresisKeepsScaleStableNearTarget) {
    DynamicRenderResolutionController controller;
    ASSERT_TRUE(controller.Configure({0.5, 1.0, 16.6667}));

    for (std::uint32_t frame = 0U; frame < 120U; ++frame) {
        static_cast<void>(controller.ObserveFrameTime(0.0168));
    }
    EXPECT_DOUBLE_EQ(controller.GetCurrentScale(), 1.0);
}

} // namespace
} // namespace UVE::Core
