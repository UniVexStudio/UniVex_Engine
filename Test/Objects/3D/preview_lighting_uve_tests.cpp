// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/preview_lighting_uve.h"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene::Tests {
namespace {

constexpr float kToleranceUVE = 1.0e-4F;

[[nodiscard]] PreviewLightingSettingsUVE DefaultSettingsUVE() {
    return PreviewLightingSettingsUVE{};
}

void ExpectVectorNear(const Math::Vector3UVE& actual, const Math::Vector3UVE& expected) {
    EXPECT_NEAR(actual.x, expected.x, kToleranceUVE);
    EXPECT_NEAR(actual.y, expected.y, kToleranceUVE);
    EXPECT_NEAR(actual.z, expected.z, kToleranceUVE);
}

// ---------------------------------------------------------------------------------------------
// Godot's rule: the preview exists only for what the scene is missing.
// ---------------------------------------------------------------------------------------------

TEST(PreviewLightingUVETest, EmptySceneGetsBothPreviews) {
    const PreviewLightingFrameUVE frame = ResolvePreviewLightingUVE(false, false, DefaultSettingsUVE());
    EXPECT_TRUE(frame.sunActive);
    EXPECT_TRUE(frame.environmentActive);
    EXPECT_GT(frame.sunEnergy, 0.0F);
    EXPECT_TRUE(frame.environment.hasEnvironment);
}

TEST(PreviewLightingUVETest, AuthoredDirectionalLightSuppressesOnlyThePreviewSun) {
    const PreviewLightingFrameUVE frame = ResolvePreviewLightingUVE(true, false, DefaultSettingsUVE());
    EXPECT_FALSE(frame.sunActive);
    EXPECT_FLOAT_EQ(frame.sunEnergy, 0.0F);
    EXPECT_TRUE(frame.environmentActive);
}

TEST(PreviewLightingUVETest, AuthoredEnvironmentSuppressesOnlyThePreviewEnvironment) {
    const PreviewLightingFrameUVE frame = ResolvePreviewLightingUVE(false, true, DefaultSettingsUVE());
    EXPECT_TRUE(frame.sunActive);
    EXPECT_FALSE(frame.environmentActive);
    EXPECT_FALSE(frame.environment.hasEnvironment);
}

TEST(PreviewLightingUVETest, AuthoredLightingSuppressesEverything) {
    const PreviewLightingFrameUVE frame = ResolvePreviewLightingUVE(true, true, DefaultSettingsUVE());
    EXPECT_FALSE(frame.sunActive);
    EXPECT_FALSE(frame.environmentActive);
}

TEST(PreviewLightingUVETest, TogglesTurnEachPreviewOff) {
    PreviewLightingSettingsUVE settings = DefaultSettingsUVE();
    settings.sunEnabled = false;
    settings.environmentEnabled = false;
    const PreviewLightingFrameUVE frame = ResolvePreviewLightingUVE(false, false, settings);
    EXPECT_FALSE(frame.sunActive);
    EXPECT_FALSE(frame.environmentActive);
}

TEST(PreviewLightingUVETest, ZeroSunEnergyLeavesTheSceneUnlitRatherThanInventingAnotherLight) {
    PreviewLightingSettingsUVE settings = DefaultSettingsUVE();
    settings.sunEnergy = 0.0F;
    const PreviewLightingFrameUVE frame = ResolvePreviewLightingUVE(false, false, settings);
    EXPECT_FALSE(frame.sunActive);
    EXPECT_TRUE(frame.environmentActive);
}

// ---------------------------------------------------------------------------------------------
// Solar geometry
// ---------------------------------------------------------------------------------------------

TEST(PreviewLightingUVETest, DirectionFollowsTheSolarConvention) {
    // Azimuth 0 is due +Z, azimuth 90 is +X, altitude 90 is straight up.
    ExpectVectorNear(GetPreviewSunDirectionUVE(0.0F, 0.0F), Math::Vector3UVE{0.0F, 0.0F, 1.0F});
    ExpectVectorNear(GetPreviewSunDirectionUVE(90.0F, 0.0F), Math::Vector3UVE{1.0F, 0.0F, 0.0F});
    ExpectVectorNear(GetPreviewSunDirectionUVE(0.0F, 90.0F), Math::Vector3UVE{0.0F, 1.0F, 0.0F});
    ExpectVectorNear(GetPreviewSunDirectionUVE(0.0F, -90.0F), Math::Vector3UVE{0.0F, -1.0F, 0.0F});
}

TEST(PreviewLightingUVETest, AzimuthWrapsInsteadOfClamping) {
    const Math::Vector3UVE wrapped = GetPreviewSunDirectionUVE(370.0F, 0.0F);
    const Math::Vector3UVE expected = GetPreviewSunDirectionUVE(10.0F, 0.0F);
    ExpectVectorNear(wrapped, expected);
    ExpectVectorNear(GetPreviewSunDirectionUVE(-10.0F, 0.0F), GetPreviewSunDirectionUVE(350.0F, 0.0F));
}

TEST(PreviewLightingUVETest, RotationPointsLocalPositiveZAtTheSun) {
    // A light shines along its local -Z, so +Z must land on the direction towards the sun.
    for (const float azimuth : {-170.0F, -45.0F, 0.0F, 37.0F, 120.0F}) {
        for (const float altitude : {-60.0F, -5.0F, 15.0F, 75.0F}) {
            const std::optional<Math::QuaternionUVE> rotation =
                TryMakePreviewSunRotationUVE(azimuth, altitude);
            ASSERT_TRUE(rotation.has_value());
            const Math::Vector3UVE aimed = Math::RotateVectorUVE(*rotation, Math::Vector3UVE{0.0F, 0.0F, 1.0F});
            ExpectVectorNear(aimed, GetPreviewSunDirectionUVE(azimuth, altitude));
        }
    }
}

TEST(PreviewLightingUVETest, PolarSunStillProducesAUsableRotation) {
    // Straight overhead and straight below are where the world-up look-at degenerates; the preview
    // must still light the scene rather than refusing at the exact angle a user is most likely to
    // drag the sun to.
    for (const float altitude : {90.0F, -90.0F}) {
        const std::optional<Math::QuaternionUVE> rotation = TryMakePreviewSunRotationUVE(0.0F, altitude);
        ASSERT_TRUE(rotation.has_value());
        const Math::Vector3UVE aimed = Math::RotateVectorUVE(*rotation, Math::Vector3UVE{0.0F, 0.0F, 1.0F});
        ExpectVectorNear(aimed, GetPreviewSunDirectionUVE(0.0F, altitude));
    }
}

// ---------------------------------------------------------------------------------------------
// Sanitisation: a hand-edited or corrupt settings file must never reach the render loop.
// ---------------------------------------------------------------------------------------------

TEST(PreviewLightingUVETest, NonFiniteSettingsFallBackToDefaults) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    PreviewLightingSettingsUVE settings = DefaultSettingsUVE();
    settings.sunAzimuthDegrees = nan;
    settings.sunAltitudeDegrees = inf;
    settings.sunEnergy = nan;
    settings.sunColor = Math::Vector3UVE{nan, inf, -1.0F};
    settings.ambientEnergy = nan;

    const PreviewLightingFrameUVE frame = ResolvePreviewLightingUVE(false, false, settings);
    EXPECT_TRUE(frame.sunActive);
    EXPECT_TRUE(std::isfinite(frame.sunEnergy));
    EXPECT_GE(frame.sunColor.x, 0.0F);
    EXPECT_LE(frame.sunColor.x, 1.0F);
    EXPECT_GE(frame.sunColor.z, 0.0F);
    EXPECT_TRUE(std::isfinite(frame.environment.skyAmbient.x));
    EXPECT_TRUE(std::isfinite(frame.environment.groundAmbient.y));
}

TEST(PreviewLightingUVETest, EnergyIsClampedRatherThanTrusted) {
    PreviewLightingSettingsUVE settings = DefaultSettingsUVE();
    settings.sunEnergy = 1.0e6F;
    settings.ambientEnergy = -5.0F;
    const PreviewLightingFrameUVE frame = ResolvePreviewLightingUVE(false, false, settings);
    EXPECT_LE(frame.sunEnergy, 16.0F);
    EXPECT_GE(frame.environment.skyAmbient.x, 0.0F);
}

// ---------------------------------------------------------------------------------------------
// The environment frame is what "Add Environment to Scene" copies, so its bands matter.
// ---------------------------------------------------------------------------------------------

TEST(PreviewLightingUVETest, EnvironmentCarriesTheAuthoredBandsAndAmbient) {
    PreviewLightingSettingsUVE settings = DefaultSettingsUVE();
    settings.skyColor = Math::Vector3UVE{0.2F, 0.4F, 0.8F};
    settings.horizonColor = Math::Vector3UVE{0.9F, 0.8F, 0.7F};
    settings.groundColor = Math::Vector3UVE{0.1F, 0.1F, 0.1F};
    settings.ambientEnergy = 2.0F;

    const PreviewLightingFrameUVE frame = ResolvePreviewLightingUVE(false, false, settings);
    ASSERT_TRUE(frame.environmentActive);
    ExpectVectorNear(frame.environment.skyColor, settings.skyColor);
    ExpectVectorNear(frame.environment.horizonColor, settings.horizonColor);
    ExpectVectorNear(frame.environment.groundColor, settings.groundColor);
    ExpectVectorNear(frame.environment.skyAmbient,
                     Math::Vector3UVE{0.4F, 0.8F, 1.6F});
    EXPECT_EQ(frame.environment.ambientSource, WorldEnvironmentAmbientSourceUVE::Sky);
}

TEST(PreviewLightingUVETest, ThePreviewNeverAddsPostProcessingOfItsOwn) {
    // A stand-in for a missing environment must not silently bloom, occlude or grade the author's
    // scene: those are decisions that belong to the WorldEnvironment they will add themselves.
    const PreviewLightingFrameUVE frame = ResolvePreviewLightingUVE(false, false, DefaultSettingsUVE());
    ASSERT_TRUE(frame.environmentActive);
    EXPECT_FALSE(frame.environment.postProcessingEnabled);
    EXPECT_FALSE(frame.environment.bloomEnabled);
    EXPECT_FALSE(frame.environment.ssaoEnabled);
    EXPECT_FALSE(frame.environment.fogEnabled);
}

TEST(PreviewLightingUVETest, SunsetWarmsTheSkyAmbientAndNightDarkensIt) {
    PreviewLightingSettingsUVE settings = DefaultSettingsUVE();
    settings.sunAltitudeDegrees = 70.0F;
    const Math::Vector3UVE noon = ResolvePreviewLightingUVE(false, false, settings).environment.skyAmbient;

    settings.sunAltitudeDegrees = -40.0F;
    const Math::Vector3UVE night = ResolvePreviewLightingUVE(false, false, settings).environment.skyAmbient;

    EXPECT_LT(night.y, noon.y);
    EXPECT_GE(night.y, 0.0F);
}

} // namespace
} // namespace UVE::Scene::Tests
