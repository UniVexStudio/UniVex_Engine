// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <array>

#include <gtest/gtest.h>

#include "uve/math/easing_uve.h"

namespace UVE::Math::Tests {
namespace {

constexpr std::array<EaseKindUVE, 31U> kAllKindsUVE{
    EaseKindUVE::Linear, EaseKindUVE::SineIn, EaseKindUVE::SineOut, EaseKindUVE::SineInOut,
    EaseKindUVE::QuadIn, EaseKindUVE::QuadOut, EaseKindUVE::QuadInOut, EaseKindUVE::CubicIn,
    EaseKindUVE::CubicOut, EaseKindUVE::CubicInOut, EaseKindUVE::QuartIn, EaseKindUVE::QuartOut,
    EaseKindUVE::QuartInOut, EaseKindUVE::QuintIn, EaseKindUVE::QuintOut, EaseKindUVE::QuintInOut,
    EaseKindUVE::ExpoIn, EaseKindUVE::ExpoOut, EaseKindUVE::ExpoInOut, EaseKindUVE::CircIn,
    EaseKindUVE::CircOut, EaseKindUVE::CircInOut, EaseKindUVE::BackIn, EaseKindUVE::BackOut,
    EaseKindUVE::BackInOut, EaseKindUVE::ElasticIn, EaseKindUVE::ElasticOut,
    EaseKindUVE::ElasticInOut, EaseKindUVE::BounceIn, EaseKindUVE::BounceOut,
    EaseKindUVE::BounceInOut,
};

TEST(EasingUVETest, EveryCurve_StartsAtZeroAndEndsAtOne) {
    for (const EaseKindUVE kind : kAllKindsUVE) {
        EXPECT_NEAR(EaseUVE(kind, 0.0F), 0.0F, 1e-5F) << static_cast<int>(kind);
        EXPECT_NEAR(EaseUVE(kind, 1.0F), 1.0F, 1e-5F) << static_cast<int>(kind);
    }
}

TEST(EasingUVETest, PolynomialCurves_HitExactMidpoints) {
    EXPECT_FLOAT_EQ(EaseUVE(EaseKindUVE::QuadIn, 0.5F), 0.25F);
    EXPECT_FLOAT_EQ(EaseUVE(EaseKindUVE::QuadInOut, 0.5F), 0.5F);
    EXPECT_FLOAT_EQ(EaseUVE(EaseKindUVE::CubicIn, 0.5F), 0.125F);
    EXPECT_FLOAT_EQ(EaseUVE(EaseKindUVE::Linear, 0.37F), 0.37F);
}

TEST(EasingUVETest, NonOvershootingCurves_AreMonotone) {
    // Back, Elastic and Bounce leave [0, 1] by design; everything else must never go back.
    for (const EaseKindUVE kind : kAllKindsUVE) {
        switch (kind) {
            case EaseKindUVE::BackIn:
            case EaseKindUVE::BackOut:
            case EaseKindUVE::BackInOut:
            case EaseKindUVE::ElasticIn:
            case EaseKindUVE::ElasticOut:
            case EaseKindUVE::ElasticInOut:
            case EaseKindUVE::BounceIn:
            case EaseKindUVE::BounceOut:
            case EaseKindUVE::BounceInOut:
                continue;
            default:
                break;
        }
        float previous = -1.0F;
        for (int step = 0; step <= 100; ++step) {
            const float value = EaseUVE(kind, static_cast<float>(step) / 100.0F);
            EXPECT_GE(value, previous - 1e-6F) << static_cast<int>(kind) << " at " << step;
            EXPECT_GE(value, -1e-6F);
            EXPECT_LE(value, 1.0F + 1e-6F);
            previous = value;
        }
    }
}

TEST(EasingUVETest, OvershootCurves_LeaveTheUnitRange) {
    // BackOut swings past 1 mid-flight; that is the documented behaviour, not a bug.
    bool overshot = false;
    for (int step = 0; step <= 100; ++step) {
        overshot = overshot || (EaseUVE(EaseKindUVE::BackOut, static_cast<float>(step) / 100.0F) > 1.0F);
    }
    EXPECT_TRUE(overshot);
}

} // namespace
} // namespace UVE::Math::Tests
