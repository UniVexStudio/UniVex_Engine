// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/audio/audio_spatialization_uve.h"

#include <cmath>

#include <gtest/gtest.h>

namespace UVE::Audio::Tests {
namespace {

constexpr float kRootTwoUVE = 1.41421356F;

TEST(AudioPanLawUVETest, CentreIsExactlyUnityAndTheLawIsConstantPower) {
    const AudioPanGainsUVE centre = ComputeAudioPanGainsUVE(0.0F);
    // Centred content must mix exactly as it did before panning existed.
    EXPECT_FLOAT_EQ(centre.left, 1.0F);
    EXPECT_FLOAT_EQ(centre.right, 1.0F);

    const AudioPanGainsUVE hardLeft = ComputeAudioPanGainsUVE(-1.0F);
    EXPECT_NEAR(hardLeft.left, kRootTwoUVE, 1.0e-6F);
    EXPECT_NEAR(hardLeft.right, 0.0F, 1.0e-6F);

    const AudioPanGainsUVE hardRight = ComputeAudioPanGainsUVE(1.0F);
    EXPECT_NEAR(hardRight.left, 0.0F, 1.0e-6F);
    EXPECT_NEAR(hardRight.right, kRootTwoUVE, 1.0e-6F);

    // Equal power: L^2 + R^2 is constant across the whole pan range.
    for (int step = -10; step <= 10; ++step) {
        const float pan = static_cast<float>(step) / 10.0F;
        const AudioPanGainsUVE gains = ComputeAudioPanGainsUVE(pan);
        EXPECT_NEAR(gains.left * gains.left + gains.right * gains.right, 2.0F, 1.0e-5F) << "pan " << pan;
        EXPECT_GE(gains.left, 0.0F);
        EXPECT_GE(gains.right, 0.0F);
    }
}

TEST(AudioPanLawUVETest, PanIsClampedAndMonotonicFromLeftToRight) {
    const AudioPanGainsUVE beyondLeft = ComputeAudioPanGainsUVE(-12.0F);
    const AudioPanGainsUVE hardLeft = ComputeAudioPanGainsUVE(-1.0F);
    EXPECT_FLOAT_EQ(beyondLeft.left, hardLeft.left);
    EXPECT_FLOAT_EQ(beyondLeft.right, hardLeft.right);

    float previous = -1.0F;
    for (int step = -10; step <= 10; ++step) {
        const float pan = ComputeAudioPanUVE(Math::Vector3UVE{static_cast<float>(step), 0.0F, 0.0F},
                                              Math::Vector3UVE{1.0F, 0.0F, 0.0F});
        EXPECT_GE(pan, previous);
        previous = pan;
    }
}

TEST(AudioPanLawUVETest, PanIsTheProjectionOntoTheListenerRightAxis) {
    const Math::Vector3UVE right{1.0F, 0.0F, 0.0F};
    EXPECT_NEAR(ComputeAudioPanUVE(Math::Vector3UVE{5.0F, 0.0F, 0.0F}, right), 1.0F, 1.0e-6F);
    EXPECT_NEAR(ComputeAudioPanUVE(Math::Vector3UVE{-5.0F, 0.0F, 0.0F}, right), -1.0F, 1.0e-6F);
    // Straight ahead or straight behind carries no left/right information in a stereo pan.
    EXPECT_NEAR(ComputeAudioPanUVE(Math::Vector3UVE{0.0F, 0.0F, -5.0F}, right), 0.0F, 1.0e-6F);
    EXPECT_NEAR(ComputeAudioPanUVE(Math::Vector3UVE{0.0F, 0.0F, 5.0F}, right), 0.0F, 1.0e-6F);
    // A source sitting on the listener is centred rather than dividing by zero.
    EXPECT_FLOAT_EQ(ComputeAudioPanUVE(Math::Vector3UVE{}, right), 0.0F);
}

TEST(AudioPanLawUVETest, ListenerRightComesFromTheOrientationPair) {
    const Math::Vector3UVE right = ComputeAudioListenerRightUVE(Math::Vector3UVE{0.0F, 0.0F, -1.0F},
                                                                 Math::Vector3UVE{0.0F, 1.0F, 0.0F});
    EXPECT_NEAR(right.x, 1.0F, 1.0e-6F);
    EXPECT_NEAR(right.y, 0.0F, 1.0e-6F);
    EXPECT_NEAR(right.z, 0.0F, 1.0e-6F);

    // Facing +X with +Y up puts the listener's right hand on +Z, and the vector stays a unit vector
    // whatever the orientation is.
    const Math::Vector3UVE turned = ComputeAudioListenerRightUVE(Math::Vector3UVE{1.0F, 0.0F, 0.0F},
                                                                  Math::Vector3UVE{0.0F, 1.0F, 0.0F});
    EXPECT_NEAR(Math::LengthUVE(turned), 1.0F, 1.0e-6F);
    EXPECT_NEAR(turned.z, 1.0F, 1.0e-6F);

    // Degenerate pair (parallel vectors) falls back to +X instead of producing a NaN.
    const Math::Vector3UVE fallback = ComputeAudioListenerRightUVE(Math::Vector3UVE{0.0F, 0.0F, -1.0F},
                                                                    Math::Vector3UVE{0.0F, 0.0F, -1.0F});
    EXPECT_FLOAT_EQ(fallback.x, 1.0F);
    EXPECT_FLOAT_EQ(fallback.y, 0.0F);
    EXPECT_FLOAT_EQ(fallback.z, 0.0F);
}

TEST(AudioConeUVETest, GainIsFullInsideFallsAcrossTheEdgeAndSettlesOnTheOuterGain) {
    EXPECT_FLOAT_EQ(ComputeAudioConeAttenuationUVE(0.0F, 60.0F, 120.0F, 0.2F), 1.0F);
    EXPECT_FLOAT_EQ(ComputeAudioConeAttenuationUVE(60.0F, 60.0F, 120.0F, 0.2F), 1.0F);
    EXPECT_NEAR(ComputeAudioConeAttenuationUVE(90.0F, 60.0F, 120.0F, 0.2F), 0.6F, 1.0e-5F);
    EXPECT_NEAR(ComputeAudioConeAttenuationUVE(120.0F, 60.0F, 120.0F, 0.2F), 0.2F, 1.0e-5F);
    EXPECT_FLOAT_EQ(ComputeAudioConeAttenuationUVE(180.0F, 60.0F, 120.0F, 0.2F), 0.2F);

    // The defaults are omnidirectional and must never attenuate anything.
    EXPECT_FLOAT_EQ(ComputeAudioConeAttenuationUVE(180.0F, 360.0F, 360.0F, 1.0F), 1.0F);
    // A degenerate cone (outer == inner) is a hard edge, not a division by zero; the edge itself
    // counts as inside.
    EXPECT_FLOAT_EQ(ComputeAudioConeAttenuationUVE(59.0F, 60.0F, 60.0F, 0.3F), 1.0F);
    EXPECT_FLOAT_EQ(ComputeAudioConeAttenuationUVE(60.0F, 60.0F, 60.0F, 0.3F), 1.0F);
    EXPECT_FLOAT_EQ(ComputeAudioConeAttenuationUVE(61.0F, 60.0F, 60.0F, 0.3F), 0.3F);
    // A corrupt angle fails closed to silence rather than letting the source through at full volume.
    EXPECT_FLOAT_EQ(ComputeAudioConeAttenuationUVE(std::nanf(""), 60.0F, 120.0F, 0.2F), 0.0F);
}

TEST(AudioConeUVETest, AngleIsMeasuredFromTheEmitterAxisToTheListener) {
    const Math::Vector3UVE axis{0.0F, 0.0F, -1.0F};
    EXPECT_NEAR(ComputeAudioConeAngleDegreesUVE(axis, Math::Vector3UVE{0.0F, 0.0F, -5.0F}), 0.0F, 1.0e-3F);
    EXPECT_NEAR(ComputeAudioConeAngleDegreesUVE(axis, Math::Vector3UVE{0.0F, 0.0F, 5.0F}), 180.0F, 1.0e-3F);
    EXPECT_NEAR(ComputeAudioConeAngleDegreesUVE(axis, Math::Vector3UVE{5.0F, 0.0F, 0.0F}), 90.0F, 1.0e-3F);
    // A zero-length axis or offset reports 0 degrees, which is full volume - an unset cone must
    // never be the reason a source goes silent.
    EXPECT_FLOAT_EQ(ComputeAudioConeAngleDegreesUVE(Math::Vector3UVE{}, Math::Vector3UVE{0.0F, 0.0F, 5.0F}), 0.0F);
    EXPECT_FLOAT_EQ(ComputeAudioConeAngleDegreesUVE(axis, Math::Vector3UVE{}), 0.0F);
}

TEST(AudioDopplerUVETest, StaticSourceAndListenerAreExactlyUnity) {
    const Math::Vector3UVE direction{1.0F, 0.0F, 0.0F};
    EXPECT_FLOAT_EQ(ComputeAudioDopplerPitchUVE(Math::Vector3UVE{}, Math::Vector3UVE{}, direction, 1.0F), 1.0F);
    // Doppler factor 0 disables the effect outright, even with violent motion.
    EXPECT_FLOAT_EQ(ComputeAudioDopplerPitchUVE(Math::Vector3UVE{50.0F, 0.0F, 0.0F},
                                                 Math::Vector3UVE{-50.0F, 0.0F, 0.0F}, direction, 0.0F),
                    1.0F);
    // Nothing can be inferred from a source sitting exactly on the listener.
    EXPECT_FLOAT_EQ(ComputeAudioDopplerPitchUVE(Math::Vector3UVE{10.0F, 0.0F, 0.0F},
                                                 Math::Vector3UVE{-10.0F, 0.0F, 0.0F}, Math::Vector3UVE{}, 1.0F),
                    1.0F);
}

TEST(AudioDopplerUVETest, ApproachingRaisesPitchAndRecedingLowersIt) {
    const Math::Vector3UVE direction{1.0F, 0.0F, 0.0F}; // listener -> source
    // Source closing on the listener at 20 m/s: 343 / (343 - 20).
    const float approach = ComputeAudioDopplerPitchUVE(Math::Vector3UVE{}, Math::Vector3UVE{-20.0F, 0.0F, 0.0F},
                                                        direction, 1.0F);
    EXPECT_NEAR(approach, 343.0F / 323.0F, 1.0e-4F);
    EXPECT_GT(approach, 1.0F);

    const float recede = ComputeAudioDopplerPitchUVE(Math::Vector3UVE{}, Math::Vector3UVE{20.0F, 0.0F, 0.0F},
                                                      direction, 1.0F);
    EXPECT_NEAR(recede, 343.0F / 363.0F, 1.0e-4F);
    EXPECT_LT(recede, 1.0F);

    // A listener running toward the source raises the pitch just as the source moving toward the
    // listener does - the model is relative, not source-only.
    const float listenerApproach = ComputeAudioDopplerPitchUVE(Math::Vector3UVE{20.0F, 0.0F, 0.0F},
                                                                Math::Vector3UVE{}, direction, 1.0F);
    EXPECT_NEAR(listenerApproach, 363.0F / 343.0F, 1.0e-4F);
}

TEST(AudioDopplerUVETest, ExtraterrestrialSpeedsStayInsideThePitchClamp) {
    const Math::Vector3UVE direction{1.0F, 0.0F, 0.0F};
    const float absurd = ComputeAudioDopplerPitchUVE(Math::Vector3UVE{}, Math::Vector3UVE{-5000.0F, 0.0F, 0.0F},
                                                      direction, 1.0F);
    EXPECT_FLOAT_EQ(absurd, kMaximumAudioDopplerPitchUVE);
    const float fleeing = ComputeAudioDopplerPitchUVE(Math::Vector3UVE{}, Math::Vector3UVE{5000.0F, 0.0F, 0.0F},
                                                       direction, 1.0F);
    EXPECT_FLOAT_EQ(fleeing, kMinimumAudioDopplerPitchUVE);
    // Non-finite input is refused rather than poisoning the device-facing pitch.
    EXPECT_FLOAT_EQ(ComputeAudioDopplerPitchUVE(Math::Vector3UVE{std::nanf(""), 0.0F, 0.0F},
                                                 Math::Vector3UVE{}, direction, 1.0F),
                    1.0F);
}

} // namespace
} // namespace UVE::Audio::Tests
