// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "audio_spatialization.h"

#include <cmath>

#include <gtest/gtest.h>

namespace UVE::Audio::Tests {
namespace {

constexpr float kRootTwo = 1.41421356F;

TEST(AudioPanLawTest, CentreIsExactlyUnityAndTheLawIsConstantPower) {
    const AudioPanGains centre = ComputeAudioPanGains(0.0F);
    // Centred content must mix exactly as it did before panning existed.
    EXPECT_FLOAT_EQ(centre.left, 1.0F);
    EXPECT_FLOAT_EQ(centre.right, 1.0F);

    const AudioPanGains hardLeft = ComputeAudioPanGains(-1.0F);
    EXPECT_NEAR(hardLeft.left, kRootTwo, 1.0e-6F);
    EXPECT_NEAR(hardLeft.right, 0.0F, 1.0e-6F);

    const AudioPanGains hardRight = ComputeAudioPanGains(1.0F);
    EXPECT_NEAR(hardRight.left, 0.0F, 1.0e-6F);
    EXPECT_NEAR(hardRight.right, kRootTwo, 1.0e-6F);

    // Equal power: L^2 + R^2 is constant across the whole pan range.
    for (int step = -10; step <= 10; ++step) {
        const float pan = static_cast<float>(step) / 10.0F;
        const AudioPanGains gains = ComputeAudioPanGains(pan);
        EXPECT_NEAR(gains.left * gains.left + gains.right * gains.right, 2.0F, 1.0e-5F) << "pan " << pan;
        EXPECT_GE(gains.left, 0.0F);
        EXPECT_GE(gains.right, 0.0F);
    }
}

TEST(AudioPanLawTest, PanIsClampedAndMonotonicFromLeftToRight) {
    const AudioPanGains beyondLeft = ComputeAudioPanGains(-12.0F);
    const AudioPanGains hardLeft = ComputeAudioPanGains(-1.0F);
    EXPECT_FLOAT_EQ(beyondLeft.left, hardLeft.left);
    EXPECT_FLOAT_EQ(beyondLeft.right, hardLeft.right);

    float previous = -1.0F;
    for (int step = -10; step <= 10; ++step) {
        const float pan = ComputeAudioPan(Math::Vector3{static_cast<float>(step), 0.0F, 0.0F},
                                              Math::Vector3{1.0F, 0.0F, 0.0F});
        EXPECT_GE(pan, previous);
        previous = pan;
    }
}

TEST(AudioPanLawTest, PanIsTheProjectionOntoTheListenerRightAxis) {
    const Math::Vector3 right{1.0F, 0.0F, 0.0F};
    EXPECT_NEAR(ComputeAudioPan(Math::Vector3{5.0F, 0.0F, 0.0F}, right), 1.0F, 1.0e-6F);
    EXPECT_NEAR(ComputeAudioPan(Math::Vector3{-5.0F, 0.0F, 0.0F}, right), -1.0F, 1.0e-6F);
    // Straight ahead or straight behind carries no left/right information in a stereo pan.
    EXPECT_NEAR(ComputeAudioPan(Math::Vector3{0.0F, 0.0F, -5.0F}, right), 0.0F, 1.0e-6F);
    EXPECT_NEAR(ComputeAudioPan(Math::Vector3{0.0F, 0.0F, 5.0F}, right), 0.0F, 1.0e-6F);
    // A source sitting on the listener is centred rather than dividing by zero.
    EXPECT_FLOAT_EQ(ComputeAudioPan(Math::Vector3{}, right), 0.0F);
}

TEST(AudioPanLawTest, ListenerRightComesFromTheOrientationPair) {
    const Math::Vector3 right = ComputeAudioListenerRight(Math::Vector3{0.0F, 0.0F, -1.0F},
                                                                 Math::Vector3{0.0F, 1.0F, 0.0F});
    EXPECT_NEAR(right.x, 1.0F, 1.0e-6F);
    EXPECT_NEAR(right.y, 0.0F, 1.0e-6F);
    EXPECT_NEAR(right.z, 0.0F, 1.0e-6F);

    // Facing +X with +Y up puts the listener's right hand on +Z, and the vector stays a unit vector
    // whatever the orientation is.
    const Math::Vector3 turned = ComputeAudioListenerRight(Math::Vector3{1.0F, 0.0F, 0.0F},
                                                                  Math::Vector3{0.0F, 1.0F, 0.0F});
    EXPECT_NEAR(Math::Length(turned), 1.0F, 1.0e-6F);
    EXPECT_NEAR(turned.z, 1.0F, 1.0e-6F);

    // Degenerate pair (parallel vectors) falls back to +X instead of producing a NaN.
    const Math::Vector3 fallback = ComputeAudioListenerRight(Math::Vector3{0.0F, 0.0F, -1.0F},
                                                                    Math::Vector3{0.0F, 0.0F, -1.0F});
    EXPECT_FLOAT_EQ(fallback.x, 1.0F);
    EXPECT_FLOAT_EQ(fallback.y, 0.0F);
    EXPECT_FLOAT_EQ(fallback.z, 0.0F);
}

TEST(AudioConeTest, GainIsFullInsideFallsAcrossTheEdgeAndSettlesOnTheOuterGain) {
    EXPECT_FLOAT_EQ(ComputeAudioConeAttenuation(0.0F, 60.0F, 120.0F, 0.2F), 1.0F);
    EXPECT_FLOAT_EQ(ComputeAudioConeAttenuation(60.0F, 60.0F, 120.0F, 0.2F), 1.0F);
    EXPECT_NEAR(ComputeAudioConeAttenuation(90.0F, 60.0F, 120.0F, 0.2F), 0.6F, 1.0e-5F);
    EXPECT_NEAR(ComputeAudioConeAttenuation(120.0F, 60.0F, 120.0F, 0.2F), 0.2F, 1.0e-5F);
    EXPECT_FLOAT_EQ(ComputeAudioConeAttenuation(180.0F, 60.0F, 120.0F, 0.2F), 0.2F);

    // The defaults are omnidirectional and must never attenuate anything.
    EXPECT_FLOAT_EQ(ComputeAudioConeAttenuation(180.0F, 360.0F, 360.0F, 1.0F), 1.0F);
    // A degenerate cone (outer == inner) is a hard edge, not a division by zero; the edge itself
    // counts as inside.
    EXPECT_FLOAT_EQ(ComputeAudioConeAttenuation(59.0F, 60.0F, 60.0F, 0.3F), 1.0F);
    EXPECT_FLOAT_EQ(ComputeAudioConeAttenuation(60.0F, 60.0F, 60.0F, 0.3F), 1.0F);
    EXPECT_FLOAT_EQ(ComputeAudioConeAttenuation(61.0F, 60.0F, 60.0F, 0.3F), 0.3F);
    // A corrupt angle fails closed to silence rather than letting the source through at full volume.
    EXPECT_FLOAT_EQ(ComputeAudioConeAttenuation(std::nanf(""), 60.0F, 120.0F, 0.2F), 0.0F);
}

TEST(AudioConeTest, AngleIsMeasuredFromTheEmitterAxisToTheListener) {
    const Math::Vector3 axis{0.0F, 0.0F, -1.0F};
    EXPECT_NEAR(ComputeAudioConeAngleDegrees(axis, Math::Vector3{0.0F, 0.0F, -5.0F}), 0.0F, 1.0e-3F);
    EXPECT_NEAR(ComputeAudioConeAngleDegrees(axis, Math::Vector3{0.0F, 0.0F, 5.0F}), 180.0F, 1.0e-3F);
    EXPECT_NEAR(ComputeAudioConeAngleDegrees(axis, Math::Vector3{5.0F, 0.0F, 0.0F}), 90.0F, 1.0e-3F);
    // A zero-length axis or offset reports 0 degrees, which is full volume - an unset cone must
    // never be the reason a source goes silent.
    EXPECT_FLOAT_EQ(ComputeAudioConeAngleDegrees(Math::Vector3{}, Math::Vector3{0.0F, 0.0F, 5.0F}), 0.0F);
    EXPECT_FLOAT_EQ(ComputeAudioConeAngleDegrees(axis, Math::Vector3{}), 0.0F);
}

TEST(AudioDopplerTest, StaticSourceAndListenerAreExactlyUnity) {
    const Math::Vector3 direction{1.0F, 0.0F, 0.0F};
    EXPECT_FLOAT_EQ(ComputeAudioDopplerPitch(Math::Vector3{}, Math::Vector3{}, direction, 1.0F), 1.0F);
    // Doppler factor 0 disables the effect outright, even with violent motion.
    EXPECT_FLOAT_EQ(ComputeAudioDopplerPitch(Math::Vector3{50.0F, 0.0F, 0.0F},
                                                 Math::Vector3{-50.0F, 0.0F, 0.0F}, direction, 0.0F),
                    1.0F);
    // Nothing can be inferred from a source sitting exactly on the listener.
    EXPECT_FLOAT_EQ(ComputeAudioDopplerPitch(Math::Vector3{10.0F, 0.0F, 0.0F},
                                                 Math::Vector3{-10.0F, 0.0F, 0.0F}, Math::Vector3{}, 1.0F),
                    1.0F);
}

TEST(AudioDopplerTest, ApproachingRaisesPitchAndRecedingLowersIt) {
    const Math::Vector3 direction{1.0F, 0.0F, 0.0F}; // listener -> source
    // Source closing on the listener at 20 m/s: 343 / (343 - 20).
    const float approach = ComputeAudioDopplerPitch(Math::Vector3{}, Math::Vector3{-20.0F, 0.0F, 0.0F},
                                                        direction, 1.0F);
    EXPECT_NEAR(approach, 343.0F / 323.0F, 1.0e-4F);
    EXPECT_GT(approach, 1.0F);

    const float recede = ComputeAudioDopplerPitch(Math::Vector3{}, Math::Vector3{20.0F, 0.0F, 0.0F},
                                                      direction, 1.0F);
    EXPECT_NEAR(recede, 343.0F / 363.0F, 1.0e-4F);
    EXPECT_LT(recede, 1.0F);

    // A listener running toward the source raises the pitch just as the source moving toward the
    // listener does - the model is relative, not source-only.
    const float listenerApproach = ComputeAudioDopplerPitch(Math::Vector3{20.0F, 0.0F, 0.0F},
                                                                Math::Vector3{}, direction, 1.0F);
    EXPECT_NEAR(listenerApproach, 363.0F / 343.0F, 1.0e-4F);
}

TEST(AudioDopplerTest, ExtraterrestrialSpeedsStayInsideThePitchClamp) {
    const Math::Vector3 direction{1.0F, 0.0F, 0.0F};
    const float absurd = ComputeAudioDopplerPitch(Math::Vector3{}, Math::Vector3{-5000.0F, 0.0F, 0.0F},
                                                      direction, 1.0F);
    EXPECT_FLOAT_EQ(absurd, kMaximumAudioDopplerPitch);
    const float fleeing = ComputeAudioDopplerPitch(Math::Vector3{}, Math::Vector3{5000.0F, 0.0F, 0.0F},
                                                       direction, 1.0F);
    EXPECT_FLOAT_EQ(fleeing, kMinimumAudioDopplerPitch);
    // Non-finite input is refused rather than poisoning the device-facing pitch.
    EXPECT_FLOAT_EQ(ComputeAudioDopplerPitch(Math::Vector3{std::nanf(""), 0.0F, 0.0F},
                                                 Math::Vector3{}, direction, 1.0F),
                    1.0F);
}

} // namespace
} // namespace UVE::Audio::Tests
