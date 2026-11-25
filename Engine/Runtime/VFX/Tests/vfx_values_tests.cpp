// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "vfx_values.h"

#include <gtest/gtest.h>

namespace UVE::VFX::Tests {
namespace {

TEST(VfxValuesTest, CurveTableMatchesExactSampling) {
    Asset::ParticleCurve smoothCurve;
    smoothCurve.smooth = true;
    smoothCurve.keys = {
        {0.0F, 0.2F, 0.0F, 1.5F},
        {0.4F, 1.0F, 0.5F, -0.5F},
        {1.0F, 0.1F, -1.0F, 0.0F},
    };
    const VfxCurveTable bakedSmooth = BakeVfxCurveTable(smoothCurve, 2.5F);
    for (int i = 0; i <= 1000; ++i) {
        const float t = static_cast<float>(i) / 1000.0F;
        const float exact = EvaluateVfxCurveExact(smoothCurve, t) * 2.5F;
        const float sampled = bakedSmooth.Sample(t);
        EXPECT_NEAR(sampled, exact, 1e-3F) << "t=" << t;
    }

    Asset::ParticleCurve linearCurve;
    linearCurve.smooth = false;
    linearCurve.keys = {
        {0.1F, 0.0F, 0.0F, 0.0F},
        {0.5F, 1.0F, 0.0F, 0.0F},
        {0.9F, 0.5F, 0.0F, 0.0F},
    };
    const VfxCurveTable bakedLinear = BakeVfxCurveTable(linearCurve, 1.0F);
    EXPECT_FLOAT_EQ(EvaluateVfxCurveExact(linearCurve, 0.0F), 0.0F);
    EXPECT_FLOAT_EQ(EvaluateVfxCurveExact(linearCurve, 1.0F), 0.5F);
    for (int i = 0; i <= 1000; ++i) {
        const float t = static_cast<float>(i) / 1000.0F;
        const float exact = EvaluateVfxCurveExact(linearCurve, t);
        const float sampled = bakedLinear.Sample(t);
        EXPECT_NEAR(sampled, exact, 5e-3F) << "linear t=" << t;
    }
}

TEST(VfxValuesTest, GradientInterpolatesInLinearSpace) {
    Asset::ParticleGradient gradient;
    gradient.stops = {
        {0.0F, {1.0F, 0.0F, 0.0F}, 0.2F},
        {1.0F, {0.0F, 0.5F, 1.0F}, 0.8F},
    };

    const VfxLinearColorAlpha midExact = EvaluateVfxGradientExact(gradient, 0.5F);
    EXPECT_NEAR(midExact.color.r, 0.5F, 1e-6F);
    EXPECT_NEAR(midExact.color.g, 0.25F, 1e-6F);
    EXPECT_NEAR(midExact.color.b, 0.5F, 1e-6F);
    EXPECT_NEAR(midExact.alpha, 0.5F, 1e-6F);

    const VfxGradientTable table = BakeVfxGradientTable(gradient);
    for (int i = 0; i <= 255; ++i) {
        const float t = static_cast<float>(i) / 255.0F;
        const VfxLinearColorAlpha exact = EvaluateVfxGradientExact(gradient, t);
        const VfxLinearColorAlpha sampled = table.Sample(t);
        EXPECT_NEAR(sampled.color.r, exact.color.r, 1e-3F);
        EXPECT_NEAR(sampled.color.g, exact.color.g, 1e-3F);
        EXPECT_NEAR(sampled.color.b, exact.color.b, 1e-3F);
        EXPECT_NEAR(sampled.alpha, exact.alpha, 1e-3F);
    }
}

} // namespace
} // namespace UVE::VFX::Tests
