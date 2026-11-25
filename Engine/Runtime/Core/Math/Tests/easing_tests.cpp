// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <array>

#include <gtest/gtest.h>

#include "easing.h"

namespace UVE::Math::Tests {
namespace {

constexpr std::array<EaseKind, 31U> kAllKinds{
    EaseKind::Linear, EaseKind::SineIn, EaseKind::SineOut, EaseKind::SineInOut,
    EaseKind::QuadIn, EaseKind::QuadOut, EaseKind::QuadInOut, EaseKind::CubicIn,
    EaseKind::CubicOut, EaseKind::CubicInOut, EaseKind::QuartIn, EaseKind::QuartOut,
    EaseKind::QuartInOut, EaseKind::QuintIn, EaseKind::QuintOut, EaseKind::QuintInOut,
    EaseKind::ExpoIn, EaseKind::ExpoOut, EaseKind::ExpoInOut, EaseKind::CircIn,
    EaseKind::CircOut, EaseKind::CircInOut, EaseKind::BackIn, EaseKind::BackOut,
    EaseKind::BackInOut, EaseKind::ElasticIn, EaseKind::ElasticOut,
    EaseKind::ElasticInOut, EaseKind::BounceIn, EaseKind::BounceOut,
    EaseKind::BounceInOut,
};

TEST(EasingTest, EveryCurve_StartsAtZeroAndEndsAtOne) {
    for (const EaseKind kind : kAllKinds) {
        EXPECT_NEAR(Ease(kind, 0.0F), 0.0F, 1e-5F) << static_cast<int>(kind);
        EXPECT_NEAR(Ease(kind, 1.0F), 1.0F, 1e-5F) << static_cast<int>(kind);
    }
}

TEST(EasingTest, PolynomialCurves_HitExactMidpoints) {
    EXPECT_FLOAT_EQ(Ease(EaseKind::QuadIn, 0.5F), 0.25F);
    EXPECT_FLOAT_EQ(Ease(EaseKind::QuadInOut, 0.5F), 0.5F);
    EXPECT_FLOAT_EQ(Ease(EaseKind::CubicIn, 0.5F), 0.125F);
    EXPECT_FLOAT_EQ(Ease(EaseKind::Linear, 0.37F), 0.37F);
}

TEST(EasingTest, NonOvershootingCurves_AreMonotone) {
    // Back, Elastic and Bounce leave [0, 1] by design; everything else must never go back.
    for (const EaseKind kind : kAllKinds) {
        switch (kind) {
            case EaseKind::BackIn:
            case EaseKind::BackOut:
            case EaseKind::BackInOut:
            case EaseKind::ElasticIn:
            case EaseKind::ElasticOut:
            case EaseKind::ElasticInOut:
            case EaseKind::BounceIn:
            case EaseKind::BounceOut:
            case EaseKind::BounceInOut:
                continue;
            default:
                break;
        }
        float previous = -1.0F;
        for (int step = 0; step <= 100; ++step) {
            const float value = Ease(kind, static_cast<float>(step) / 100.0F);
            EXPECT_GE(value, previous - 1e-6F) << static_cast<int>(kind) << " at " << step;
            EXPECT_GE(value, -1e-6F);
            EXPECT_LE(value, 1.0F + 1e-6F);
            previous = value;
        }
    }
}

TEST(EasingTest, OvershootCurves_LeaveTheUnitRange) {
    // BackOut swings past 1 mid-flight; that is the documented behaviour, not a bug.
    bool overshot = false;
    for (int step = 0; step <= 100; ++step) {
        overshot = overshot || (Ease(EaseKind::BackOut, static_cast<float>(step) / 100.0F) > 1.0F);
    }
    EXPECT_TRUE(overshot);
}

} // namespace
} // namespace UVE::Math::Tests
