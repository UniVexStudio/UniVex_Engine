// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "preview_lighting.h"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include "quaternion.h"
#include "vector3.h"

namespace UVE::Scene::Tests {
namespace {

constexpr float kTolerance = 1.0e-4F;

[[nodiscard]] PreviewLightingSettings DefaultSettings() {
    return PreviewLightingSettings{};
}

void ExpectVectorNear(const Math::Vector3& actual, const Math::Vector3& expected) {
    EXPECT_NEAR(actual.x, expected.x, kTolerance);
    EXPECT_NEAR(actual.y, expected.y, kTolerance);
    EXPECT_NEAR(actual.z, expected.z, kTolerance);
}

// ---------------------------------------------------------------------------------------------
// Godot's rule: the preview exists only for what the scene is missing.
// ---------------------------------------------------------------------------------------------

TEST(PreviewLightingTest, EmptySceneGetsBothPreviews) {
    const PreviewLightingFrame frame = ResolvePreviewLighting(false, false, DefaultSettings());
    EXPECT_TRUE(frame.sunActive);
    EXPECT_TRUE(frame.environmentActive);
    EXPECT_GT(frame.sunEnergy, 0.0F);
    EXPECT_TRUE(frame.environment.hasEnvironment);
}

TEST(PreviewLightingTest, AuthoredDirectionalLightSuppressesOnlyThePreviewSun) {
    const PreviewLightingFrame frame = ResolvePreviewLighting(true, false, DefaultSettings());
    EXPECT_FALSE(frame.sunActive);
    EXPECT_FLOAT_EQ(frame.sunEnergy, 0.0F);
    EXPECT_TRUE(frame.environmentActive);
}

TEST(PreviewLightingTest, AuthoredEnvironmentSuppressesOnlyThePreviewEnvironment) {
    const PreviewLightingFrame frame = ResolvePreviewLighting(false, true, DefaultSettings());
    EXPECT_FALSE(frame.sunActive);
    EXPECT_FLOAT_EQ(frame.sunEnergy, 0.0F);
    EXPECT_FALSE(frame.environmentActive);
    EXPECT_FALSE(frame.environment.hasEnvironment);
}

TEST(PreviewLightingTest, AuthoredLightingSuppressesEverything) {
    const PreviewLightingFrame frame = ResolvePreviewLighting(true, true, DefaultSettings());
    EXPECT_FALSE(frame.sunActive);
    EXPECT_FALSE(frame.environmentActive);
}

TEST(PreviewLightingTest, TogglesTurnEachPreviewOff) {
    PreviewLightingSettings settings = DefaultSettings();
    settings.sunEnabled = false;
    settings.environmentEnabled = false;
    const PreviewLightingFrame frame = ResolvePreviewLighting(false, false, settings);
    EXPECT_FALSE(frame.sunActive);
    EXPECT_FALSE(frame.environmentActive);
}

TEST(PreviewLightingTest, ZeroSunEnergyLeavesTheSceneUnlitRatherThanInventingAnotherLight) {
    PreviewLightingSettings settings = DefaultSettings();
    settings.sunEnergy = 0.0F;
    const PreviewLightingFrame frame = ResolvePreviewLighting(false, false, settings);
    EXPECT_FALSE(frame.sunActive);
    EXPECT_TRUE(frame.environmentActive);
}

// ---------------------------------------------------------------------------------------------
// Solar geometry
// ---------------------------------------------------------------------------------------------

TEST(PreviewLightingTest, DirectionFollowsTheSolarConvention) {
    // Azimuth 0 is due +Z, azimuth 90 is +X, altitude 90 is straight up.
    ExpectVectorNear(GetPreviewSunDirection(0.0F, 0.0F), Math::Vector3{0.0F, 0.0F, 1.0F});
    ExpectVectorNear(GetPreviewSunDirection(90.0F, 0.0F), Math::Vector3{1.0F, 0.0F, 0.0F});
    ExpectVectorNear(GetPreviewSunDirection(0.0F, 90.0F), Math::Vector3{0.0F, 1.0F, 0.0F});
    ExpectVectorNear(GetPreviewSunDirection(0.0F, -90.0F), Math::Vector3{0.0F, -1.0F, 0.0F});
}

TEST(PreviewLightingTest, AzimuthWrapsInsteadOfClamping) {
    const Math::Vector3 wrapped = GetPreviewSunDirection(370.0F, 0.0F);
    const Math::Vector3 expected = GetPreviewSunDirection(10.0F, 0.0F);
    ExpectVectorNear(wrapped, expected);
    ExpectVectorNear(GetPreviewSunDirection(-10.0F, 0.0F), GetPreviewSunDirection(350.0F, 0.0F));
}

TEST(PreviewLightingTest, RotationPointsLocalPositiveZAtTheSun) {
    // A light shines along its local -Z, so +Z must land on the direction towards the sun.
    for (const float azimuth : {-170.0F, -45.0F, 0.0F, 37.0F, 120.0F}) {
        for (const float altitude : {-60.0F, -5.0F, 15.0F, 75.0F}) {
            const std::optional<Math::Quaternion> rotation =
                TryMakePreviewSunRotation(azimuth, altitude);
            ASSERT_TRUE(rotation.has_value());
            const Math::Vector3 aimed = Math::RotateVector(*rotation, Math::Vector3{0.0F, 0.0F, 1.0F});
            ExpectVectorNear(aimed, GetPreviewSunDirection(azimuth, altitude));
        }
    }
}

TEST(PreviewLightingTest, PolarSunStillProducesAUsableRotation) {
    // Straight overhead and straight below are where the world-up look-at degenerates; the preview
    // must still light the scene rather than refusing at the exact angle a user is most likely to
    // drag the sun to.
    for (const float altitude : {90.0F, -90.0F}) {
        const std::optional<Math::Quaternion> rotation = TryMakePreviewSunRotation(0.0F, altitude);
        ASSERT_TRUE(rotation.has_value());
        const Math::Vector3 aimed = Math::RotateVector(*rotation, Math::Vector3{0.0F, 0.0F, 1.0F});
        ExpectVectorNear(aimed, GetPreviewSunDirection(0.0F, altitude));
    }
}

// ---------------------------------------------------------------------------------------------
// Sanitisation: a hand-edited or corrupt settings file must never reach the render loop.
// ---------------------------------------------------------------------------------------------

TEST(PreviewLightingTest, NonFiniteSettingsFallBackToDefaults) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    PreviewLightingSettings settings = DefaultSettings();
    settings.sunAzimuthDegrees = nan;
    settings.sunAltitudeDegrees = inf;
    settings.sunEnergy = nan;
    settings.sunColor = Math::Vector3{nan, inf, -1.0F};
    settings.ambientEnergy = nan;

    const PreviewLightingFrame frame = ResolvePreviewLighting(false, false, settings);
    EXPECT_TRUE(frame.sunActive);
    EXPECT_TRUE(std::isfinite(frame.sunEnergy));
    EXPECT_GE(frame.sunColor.x, 0.0F);
    EXPECT_LE(frame.sunColor.x, 1.0F);
    EXPECT_GE(frame.sunColor.z, 0.0F);
    EXPECT_TRUE(std::isfinite(frame.environment.skyAmbient.x));
    EXPECT_TRUE(std::isfinite(frame.environment.groundAmbient.y));
}

TEST(PreviewLightingTest, EnergyIsClampedRatherThanTrusted) {
    PreviewLightingSettings settings = DefaultSettings();
    settings.sunEnergy = 1.0e6F;
    settings.ambientEnergy = -5.0F;
    const PreviewLightingFrame frame = ResolvePreviewLighting(false, false, settings);
    EXPECT_LE(frame.sunEnergy, 16.0F);
    EXPECT_GE(frame.environment.skyAmbient.x, 0.0F);
}

// ---------------------------------------------------------------------------------------------
// The environment frame is what "Add Environment to Scene" copies, so its bands matter.
// ---------------------------------------------------------------------------------------------

TEST(PreviewLightingTest, EnvironmentCarriesTheAuthoredBandsAndAmbient) {
    PreviewLightingSettings settings = DefaultSettings();
    settings.skyColor = Math::Vector3{0.2F, 0.4F, 0.8F};
    settings.horizonColor = Math::Vector3{0.9F, 0.8F, 0.7F};
    settings.groundColor = Math::Vector3{0.1F, 0.1F, 0.1F};
    settings.ambientEnergy = 2.0F;

    const PreviewLightingFrame frame = ResolvePreviewLighting(false, false, settings);
    ASSERT_TRUE(frame.environmentActive);
    ExpectVectorNear(frame.environment.skyColor, settings.skyColor);
    ExpectVectorNear(frame.environment.horizonColor, settings.horizonColor);
    ExpectVectorNear(frame.environment.groundColor, settings.groundColor);
    ExpectVectorNear(frame.environment.skyAmbient,
                     Math::Vector3{0.4F, 0.8F, 1.6F});
    EXPECT_EQ(frame.environment.ambientSource, WorldEnvironmentAmbientSource::Sky);
}

TEST(PreviewLightingTest, PreviewCarriesCleanPostProcessingDefaultsWithoutFog) {
    // The preview environment matches the runtime AAA post-processing baseline (soft-knee bloom
    // and ambient-only SSAO active, fog opt-in) so the editor viewport matches the game output.
    const PreviewLightingFrame frame = ResolvePreviewLighting(false, false, DefaultSettings());
    ASSERT_TRUE(frame.environmentActive);
    EXPECT_TRUE(frame.environment.hasPostProcessingOverride);
    EXPECT_TRUE(frame.environment.postProcessingEnabled);
    EXPECT_TRUE(frame.environment.bloomEnabled);
    EXPECT_TRUE(frame.environment.ssaoEnabled);
    EXPECT_FALSE(frame.environment.fogEnabled);
}

TEST(PreviewLightingTest, SunsetWarmsTheSkyAmbientAndNightDarkensIt) {
    PreviewLightingSettings settings = DefaultSettings();
    settings.sunAltitudeDegrees = 70.0F;
    const Math::Vector3 noon = ResolvePreviewLighting(false, false, settings).environment.skyAmbient;

    settings.sunAltitudeDegrees = -40.0F;
    const Math::Vector3 night = ResolvePreviewLighting(false, false, settings).environment.skyAmbient;

    EXPECT_LT(night.y, noon.y);
    EXPECT_GE(night.y, 0.0F);
}

} // namespace
} // namespace UVE::Scene::Tests
