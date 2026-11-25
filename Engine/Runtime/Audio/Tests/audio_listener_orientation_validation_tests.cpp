// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "audio_listener_orientation_validation.h"

#include <limits>

#include <gtest/gtest.h>

namespace UVE::Audio::Tests {
namespace {

TEST(AudioListenerOrientationValidationTest, AcceptsFiniteNonZeroBasisVectors) {
    EXPECT_TRUE(IsAudioListenerOrientationValid(Math::Vector3{0.0F, 0.0F, -1.0F},
                                                    Math::Vector3{0.0F, 1.0F, 0.0F}));
}

TEST(AudioListenerOrientationValidationTest, RejectsZeroAndNonFiniteBasisVectors) {
    EXPECT_FALSE(IsAudioListenerOrientationValid(Math::Vector3{},
                                                     Math::Vector3{0.0F, 1.0F, 0.0F}));
    EXPECT_FALSE(IsAudioListenerOrientationValid(
        Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, -1.0F},
        Math::Vector3{0.0F, 1.0F, 0.0F}));
    EXPECT_FALSE(IsAudioListenerOrientationValid(
        Math::Vector3{0.0F, 0.0F, -1.0F},
        Math::Vector3{std::numeric_limits<float>::infinity(), 1.0F, 0.0F}));
}

TEST(AudioListenerOrientationValidationTest, RejectsFiniteVectorsWhoseSquaredLengthOverflows) {
    constexpr float kLargeFiniteComponent = std::numeric_limits<float>::max();
    EXPECT_FALSE(IsAudioListenerOrientationValid(
        Math::Vector3{kLargeFiniteComponent, kLargeFiniteComponent, 0.0F},
        Math::Vector3{0.0F, 1.0F, 0.0F}));
}

} // namespace
} // namespace UVE::Audio::Tests
