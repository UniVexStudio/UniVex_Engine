// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "audio_attenuation_model.h"

#include <gtest/gtest.h>

namespace UVE::Audio::Tests {
namespace {

TEST(AudioAttenuationModelTest, DistanceAtOrBelowMinDistance_ReturnsFullGain_BothModels) {
    EXPECT_FLOAT_EQ(ComputeDistanceAttenuation(1.0F, 1.0F, 10.0F, AudioAttenuationModel::Linear), 1.0F);
    EXPECT_FLOAT_EQ(ComputeDistanceAttenuation(0.5F, 1.0F, 10.0F, AudioAttenuationModel::Linear), 1.0F);
    EXPECT_FLOAT_EQ(ComputeDistanceAttenuation(1.0F, 1.0F, 10.0F, AudioAttenuationModel::InverseSquare), 1.0F);
    EXPECT_FLOAT_EQ(ComputeDistanceAttenuation(0.5F, 1.0F, 10.0F, AudioAttenuationModel::InverseSquare), 1.0F);
}

TEST(AudioAttenuationModelTest, DistanceAtOrBeyondMaxDistance_ReturnsZeroGain_BothModels) {
    EXPECT_FLOAT_EQ(ComputeDistanceAttenuation(10.0F, 1.0F, 10.0F, AudioAttenuationModel::Linear), 0.0F);
    EXPECT_FLOAT_EQ(ComputeDistanceAttenuation(20.0F, 1.0F, 10.0F, AudioAttenuationModel::Linear), 0.0F);
    EXPECT_FLOAT_EQ(ComputeDistanceAttenuation(10.0F, 1.0F, 10.0F, AudioAttenuationModel::InverseSquare), 0.0F);
    EXPECT_FLOAT_EQ(ComputeDistanceAttenuation(20.0F, 1.0F, 10.0F, AudioAttenuationModel::InverseSquare), 0.0F);
}

TEST(AudioAttenuationModelTest, Linear_AtMidpoint_ReturnsHalfGain) {
    // minDistance=1, maxDistance=9 -> midpoint distance=5.
    EXPECT_FLOAT_EQ(ComputeDistanceAttenuation(5.0F, 1.0F, 9.0F, AudioAttenuationModel::Linear), 0.5F);
}

TEST(AudioAttenuationModelTest, InverseSquare_AtTwiceMinDistance_ReturnsQuarterGain) {
    // (minDistance / distance)^2 = (1/2)^2 = 0.25.
    EXPECT_FLOAT_EQ(ComputeDistanceAttenuation(2.0F, 1.0F, 100.0F, AudioAttenuationModel::InverseSquare), 0.25F);
}

TEST(AudioAttenuationModelTest, Linear_StrictlyDecreasesWithDistance) {
    const float near = ComputeDistanceAttenuation(2.0F, 1.0F, 10.0F, AudioAttenuationModel::Linear);
    const float mid = ComputeDistanceAttenuation(5.0F, 1.0F, 10.0F, AudioAttenuationModel::Linear);
    const float far = ComputeDistanceAttenuation(8.0F, 1.0F, 10.0F, AudioAttenuationModel::Linear);
    EXPECT_GT(near, mid);
    EXPECT_GT(mid, far);
}

TEST(AudioAttenuationModelTest, InverseSquare_StrictlyDecreasesWithDistance) {
    const float near = ComputeDistanceAttenuation(2.0F, 1.0F, 100.0F, AudioAttenuationModel::InverseSquare);
    const float mid = ComputeDistanceAttenuation(10.0F, 1.0F, 100.0F, AudioAttenuationModel::InverseSquare);
    const float far = ComputeDistanceAttenuation(50.0F, 1.0F, 100.0F, AudioAttenuationModel::InverseSquare);
    EXPECT_GT(near, mid);
    EXPECT_GT(mid, far);
}

} // namespace
} // namespace UVE::Audio::Tests
