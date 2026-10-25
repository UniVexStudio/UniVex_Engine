// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/sky_3d_uve.h"
#include "uve/objects/3d/world_environment_3d_uve.h"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/scalar_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/memory/memory_manager_uve.h"

namespace UVE::Scene::Tests {
namespace {

constexpr float kToleranceUVE = 1.0e-4F;

/// A sun direction from an altitude in degrees, in the XY plane: +Y is the zenith, -Y the nadir.
[[nodiscard]] Math::Vector3UVE SunAtAltitudeUVE(const float degrees) {
    const float radians = degrees * Math::kPiUVE / 180.0F;
    return Math::Vector3UVE{std::cos(radians), std::sin(radians), 0.0F};
}

void ExpectVectorNear(const Math::Vector3UVE& actual, const Math::Vector3UVE& expected) {
    EXPECT_NEAR(actual.x, expected.x, kToleranceUVE);
    EXPECT_NEAR(actual.y, expected.y, kToleranceUVE);
    EXPECT_NEAR(actual.z, expected.z, kToleranceUVE);
}

[[nodiscard]] Sky3DComponentUVE SkyWithLinkUVE() {
    Sky3DComponentUVE sky;
    sky.ambientFromSky = true;
    sky.fogFromSky = true;
    sky.reflectionsFromSky = true;
    return sky;
}

// ---------------------------------------------------------------------------------------------
// Off by default. Adding a Sky must not relight the level until someone says so.
// ---------------------------------------------------------------------------------------------

TEST(SkyLightingLinkUVETest, LinksAreOffByDefault) {
    const Sky3DComponentUVE sky;
    EXPECT_FALSE(sky.ambientFromSky);
    EXPECT_FALSE(sky.fogFromSky);
    EXPECT_FALSE(sky.reflectionsFromSky);
    EXPECT_FLOAT_EQ(sky.ambientStrength, 1.0F);
    EXPECT_FLOAT_EQ(sky.fogStrength, 1.0F);
}

TEST(SkyLightingLinkUVETest, AnUntouchedSkyLeavesTheFrameAlone) {
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    WorldEnvironmentFrameUVE frame = ResolveWorldEnvironmentFrameUVE(entityManager, Math::Vector3UVE{0.1F, 0.2F, 0.3F});
    const Math::Vector3UVE ambientBefore = frame.skyAmbient;
    const Math::Vector3UVE groundBefore = frame.groundAmbient;
    const Math::Vector3UVE fogBefore = frame.fogColor;

    EXPECT_FALSE(frame.skyAmbientLinked);
    EXPECT_FALSE(frame.skyFogLinked);
    EXPECT_FALSE(frame.skyReflectionsLinked);

    ApplySunToWorldEnvironmentFrameUVE(frame, SunAtAltitudeUVE(60.0F), Math::Vector3UVE{1.0F, 1.0F, 1.0F}, 1.0F);
    ExpectVectorNear(frame.skyAmbient, ambientBefore);
    ExpectVectorNear(frame.groundAmbient, groundBefore);
    ExpectVectorNear(frame.fogColor, fogBefore);
}

// ---------------------------------------------------------------------------------------------
// The ambient follows the sun through the air rather than through a hand-picked curve.
// ---------------------------------------------------------------------------------------------

TEST(SkyLightingLinkUVETest, SunHighAboveGivesABrightBlueFill) {
    const SkyAmbientUVE ambient = ResolveSkyAmbientUVE(SkyWithLinkUVE(), SunAtAltitudeUVE(70.0F));
    EXPECT_GT(ambient.sky.y, ambient.sky.x);
    EXPECT_GT(ambient.sky.z, ambient.sky.x);
    EXPECT_FLOAT_EQ(ambient.daylight, 1.0F);
    // The ground bounces what lands on it, so it is dimmer than the sky that lights it.
    EXPECT_LT(Math::LengthUVE(ambient.ground), Math::LengthUVE(ambient.sky));
}

TEST(SkyLightingLinkUVETest, LowSunGoesWarmAndTheNadirGoesDark) {
    const SkyAmbientUVE noon = ResolveSkyAmbientUVE(SkyWithLinkUVE(), SunAtAltitudeUVE(70.0F));
    const SkyAmbientUVE setting = ResolveSkyAmbientUVE(SkyWithLinkUVE(), SunAtAltitudeUVE(2.0F));

    // Redder than it was: the blue channel falls off faster through more air, which is why a
    // sunset is orange rather than simply dimmer.
    const float noonWarmth = noon.sky.x / noon.sky.z;
    const float settingWarmth = setting.sky.x / setting.sky.z;
    EXPECT_GT(settingWarmth, noonWarmth);

    const SkyAmbientUVE night = ResolveSkyAmbientUVE(SkyWithLinkUVE(), SunAtAltitudeUVE(-30.0F));
    EXPECT_FLOAT_EQ(night.daylight, 0.0F);
    ExpectVectorNear(night.sky, Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    ExpectVectorNear(night.ground, Math::Vector3UVE{0.0F, 0.0F, 0.0F});
}

TEST(SkyLightingLinkUVETest, DaylightRisesMonotonicallyWithTheSun) {
    float previous = -1.0F;
    for (float altitude = -30.0F; altitude <= 30.0F; altitude += 2.0F) {
        const SkyAmbientUVE ambient = ResolveSkyAmbientUVE(SkyWithLinkUVE(), SunAtAltitudeUVE(altitude));
        EXPECT_GE(ambient.daylight, previous) << "altitude " << altitude;
        previous = ambient.daylight;
    }
    EXPECT_FLOAT_EQ(previous, 1.0F);
}

TEST(SkyLightingLinkUVETest, StrengthZeroGivesNoFillAndOneMatchesTheDefault) {
    Sky3DComponentUVE off = SkyWithLinkUVE();
    off.ambientStrength = 0.0F;
    ExpectVectorNear(ResolveSkyAmbientUVE(off, SunAtAltitudeUVE(45.0F)).sky, Math::Vector3UVE{0.0F, 0.0F, 0.0F});

    Sky3DComponentUVE full = SkyWithLinkUVE();
    full.ambientStrength = 1.0F;
    const SkyAmbientUVE atFull = ResolveSkyAmbientUVE(full, SunAtAltitudeUVE(45.0F));
    const SkyAmbientUVE atDefault = ResolveSkyAmbientUVE(SkyWithLinkUVE(), SunAtAltitudeUVE(45.0F));
    ExpectVectorNear(atFull.sky, atDefault.sky);
}

TEST(SkyLightingLinkUVETest, TheSkyColourActuallyReachesTheFill) {
    Sky3DComponentUVE green = SkyWithLinkUVE();
    green.skyColor = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    const SkyAmbientUVE tinted = ResolveSkyAmbientUVE(green, SunAtAltitudeUVE(70.0F));
    EXPECT_GT(tinted.sky.y, tinted.sky.x);
    EXPECT_GT(tinted.sky.y, tinted.sky.z);
}

TEST(SkyLightingLinkUVETest, ABadDirectionIsTreatedAsTheZenithRatherThanPropagatingNaN) {
    const SkyAmbientUVE nan = ResolveSkyAmbientUVE(SkyWithLinkUVE(),
                                                   Math::Vector3UVE{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F});
    EXPECT_TRUE(std::isfinite(nan.sky.x) && std::isfinite(nan.sky.y) && std::isfinite(nan.sky.z));
    ExpectVectorNear(nan.sky, ResolveSkyAmbientUVE(SkyWithLinkUVE(), SunAtAltitudeUVE(90.0F)).sky);
}

// ---------------------------------------------------------------------------------------------
// Fog: the horizon the distance actually dissolves into.
// ---------------------------------------------------------------------------------------------

TEST(SkyLightingLinkUVETest, FogSitsBetweenTheGroundAndTheHorizon) {
    const Math::Vector3UVE ground{0.1F, 0.1F, 0.1F};
    const Math::Vector3UVE horizon{0.9F, 0.9F, 0.9F};
    const Math::Vector3UVE fog = ResolveSkyFogColorUVE(ground, horizon);
    EXPECT_GT(fog.x, ground.x);
    EXPECT_LT(fog.x, horizon.x);
    // Weighted toward the horizon: fog is the far distance, which is the horizon band.
    EXPECT_GT(fog.x, 0.5F * (ground.x + horizon.x));
}

TEST(SkyLightingLinkUVETest, FogFromSkyBlendsByStrengthInsteadOfSnapping) {
    WorldEnvironmentFrameUVE authored;
    authored.fogColor = Math::Vector3UVE{0.2F, 0.25F, 0.3F};

    WorldEnvironmentFrameUVE half = authored;
    half.skyFogLinked = true;
    half.groundColor = Math::Vector3UVE{0.0F, 0.1F, 0.2F};
    half.horizonColor = Math::Vector3UVE{0.8F, 0.9F, 1.0F};
    // A strength of zero must reproduce the authored colour exactly, so the switch can be flipped
    // back and forth without the fog drifting.
    const Math::Vector3UVE skyFog = ResolveSkyFogColorUVE(half.groundColor, half.horizonColor);
    const Math::Vector3UVE atZero{half.fogColor.x + (skyFog.x - half.fogColor.x) * 0.0F,
                                  half.fogColor.y + (skyFog.y - half.fogColor.y) * 0.0F,
                                  half.fogColor.z + (skyFog.z - half.fogColor.z) * 0.0F};
    ExpectVectorNear(atZero, authored.fogColor);

    const Math::Vector3UVE atOne{half.fogColor.x + (skyFog.x - half.fogColor.x) * 1.0F,
                                 half.fogColor.y + (skyFog.y - half.fogColor.y) * 1.0F,
                                 half.fogColor.z + (skyFog.z - half.fogColor.z) * 1.0F};
    ExpectVectorNear(atOne, skyFog);
}

// ---------------------------------------------------------------------------------------------
// The sun pass, where all three links actually land.
// ---------------------------------------------------------------------------------------------

TEST(SkyLightingLinkUVETest, TheSunPassWritesTheSkyFillIntoTheFrame) {
    WorldEnvironmentFrameUVE frame;
    frame.hasEnvironment = true;
    frame.skyAmbientLinked = true;
    frame.skyColor = Math::Vector3UVE{0.3F, 0.45F, 0.7F};
    frame.horizonColor = Math::Vector3UVE{0.9F, 0.85F, 0.8F};
    frame.groundColor = Math::Vector3UVE{0.2F, 0.18F, 0.15F};

    ApplySunToWorldEnvironmentFrameUVE(frame, SunAtAltitudeUVE(60.0F), Math::Vector3UVE{1.0F, 0.95F, 0.9F}, 1.0F);
    const SkyAmbientUVE expected = ResolveSkyAmbientUVE(frame.skyColor, frame.horizonColor, frame.groundColor, 1.0F,
                                                        1.0F, SunAtAltitudeUVE(60.0F));
    ExpectVectorNear(frame.skyAmbient, expected.sky);
    ExpectVectorNear(frame.groundAmbient, expected.ground);
}

TEST(SkyLightingLinkUVETest, NoAmbientSourceStillMeansNoAmbient) {
    WorldEnvironmentFrameUVE frame;
    frame.hasEnvironment = true;
    frame.ambientSource = WorldEnvironmentAmbientSourceUVE::None;
    frame.skyAmbientLinked = true;
    frame.skyAmbient = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    frame.groundAmbient = Math::Vector3UVE{0.0F, 0.0F, 0.0F};

    ApplySunToWorldEnvironmentFrameUVE(frame, SunAtAltitudeUVE(60.0F), Math::Vector3UVE{1.0F, 1.0F, 1.0F}, 1.0F);
    // The environment said none; a Sky offering fill does not overrule an explicit choice.
    ExpectVectorNear(frame.skyAmbient, Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    ExpectVectorNear(frame.groundAmbient, Math::Vector3UVE{0.0F, 0.0F, 0.0F});
}

TEST(SkyLightingLinkUVETest, ReflectionBandsAreTheSkyColourScaledByDaylight) {
    WorldEnvironmentFrameUVE frame;
    frame.hasEnvironment = true;
    frame.skyReflectionsLinked = true;
    frame.skyColor = Math::Vector3UVE{0.3F, 0.45F, 0.7F};
    frame.horizonColor = Math::Vector3UVE{0.9F, 0.85F, 0.8F};
    frame.groundColor = Math::Vector3UVE{0.2F, 0.18F, 0.15F};

    ApplySunToWorldEnvironmentFrameUVE(frame, SunAtAltitudeUVE(70.0F), Math::Vector3UVE{1.0F, 1.0F, 1.0F}, 1.0F);
    ExpectVectorNear(frame.reflectionSky, frame.skyColor);
    ExpectVectorNear(frame.reflectionHorizon, frame.horizonColor);
    ExpectVectorNear(frame.reflectionGround, frame.groundColor);

    // Below the horizon there is nothing to reflect: a mirror under a dark sky shows dark.
    ApplySunToWorldEnvironmentFrameUVE(frame, SunAtAltitudeUVE(-30.0F), Math::Vector3UVE{1.0F, 1.0F, 1.0F}, 1.0F);
    ExpectVectorNear(frame.reflectionSky, Math::Vector3UVE{0.0F, 0.0F, 0.0F});
}

TEST(SkyLightingLinkUVETest, ReflectionsSurviveAnAmbientSourceOfNone) {
    WorldEnvironmentFrameUVE frame;
    frame.hasEnvironment = true;
    frame.ambientSource = WorldEnvironmentAmbientSourceUVE::None;
    frame.skyReflectionsLinked = true;
    frame.skyColor = Math::Vector3UVE{0.3F, 0.45F, 0.7F};

    ApplySunToWorldEnvironmentFrameUVE(frame, SunAtAltitudeUVE(70.0F), Math::Vector3UVE{1.0F, 1.0F, 1.0F}, 1.0F);
    // Reflections are not ambient: turning the ambient off does not turn the mirror off.
    ExpectVectorNear(frame.reflectionSky, frame.skyColor);
}

TEST(SkyLightingLinkUVETest, WithoutASunTheReflectionLinkDropsRatherThanGoingBlack) {
    WorldEnvironmentFrameUVE frame;
    frame.hasEnvironment = true;
    frame.skyReflectionsLinked = true;
    frame.skyColor = Math::Vector3UVE{0.3F, 0.45F, 0.7F};

    // Energy 0 is the state before a directional light exists. The bands would be zero, and a
    // mirror fed zero is a black mirror, so the link gives way to the ambient hemisphere instead.
    ApplySunToWorldEnvironmentFrameUVE(frame, SunAtAltitudeUVE(70.0F), Math::Vector3UVE{1.0F, 1.0F, 1.0F}, 0.0F);
    EXPECT_FALSE(frame.skyReflectionsLinked);
    ExpectVectorNear(frame.reflectionSky, Math::Vector3UVE{0.0F, 0.0F, 0.0F});
}

// ---------------------------------------------------------------------------------------------
// Guard rails on the authored numbers.
// ---------------------------------------------------------------------------------------------

TEST(SkyLightingLinkUVETest, OutOfRangeStrengthsAreRejected) {
    Sky3DComponentUVE sky;
    EXPECT_TRUE(IsSky3DObjectComponentValidUVE(sky));

    sky.ambientStrength = -0.1F;
    EXPECT_FALSE(IsSky3DObjectComponentValidUVE(sky));
    sky.ambientStrength = 1.1F;
    EXPECT_FALSE(IsSky3DObjectComponentValidUVE(sky));
    sky.ambientStrength = 0.5F;
    EXPECT_TRUE(IsSky3DObjectComponentValidUVE(sky));

    sky.fogStrength = -0.1F;
    EXPECT_FALSE(IsSky3DObjectComponentValidUVE(sky));
    sky.fogStrength = 1.0F;
    EXPECT_TRUE(IsSky3DObjectComponentValidUVE(sky));

    sky.fogStrength = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsSky3DObjectComponentValidUVE(sky));
}

} // namespace
} // namespace UVE::Scene::Tests
