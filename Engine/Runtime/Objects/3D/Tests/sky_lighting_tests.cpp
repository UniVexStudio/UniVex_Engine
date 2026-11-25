// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "sky_3d.h"
#include "world_environment_3d.h"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include "entity_manager.h"
#include "event_system.h"
#include "scalar.h"
#include "vector3.h"
#include "memory_manager.h"

namespace UVE::Scene::Tests {
namespace {

constexpr float kTolerance = 1.0e-4F;

/// A sun direction from an altitude in degrees, in the XY plane: +Y is the zenith, -Y the nadir.
[[nodiscard]] Math::Vector3 SunAtAltitude(const float degrees) {
    const float radians = degrees * Math::kPi / 180.0F;
    return Math::Vector3{std::cos(radians), std::sin(radians), 0.0F};
}

void ExpectVectorNear(const Math::Vector3& actual, const Math::Vector3& expected) {
    EXPECT_NEAR(actual.x, expected.x, kTolerance);
    EXPECT_NEAR(actual.y, expected.y, kTolerance);
    EXPECT_NEAR(actual.z, expected.z, kTolerance);
}

[[nodiscard]] Sky3DComponent SkyWithLink() {
    Sky3DComponent sky;
    sky.ambientFromSky = true;
    sky.fogFromSky = true;
    sky.reflectionsFromSky = true;
    return sky;
}

// ---------------------------------------------------------------------------------------------
// Off by default. Adding a Sky must not relight the level until someone says so.
// ---------------------------------------------------------------------------------------------

TEST(SkyLightingLinkTest, LinksAreOffByDefault) {
    const Sky3DComponent sky;
    EXPECT_FALSE(sky.ambientFromSky);
    EXPECT_FALSE(sky.fogFromSky);
    EXPECT_FALSE(sky.reflectionsFromSky);
    EXPECT_FLOAT_EQ(sky.ambientStrength, 1.0F);
    EXPECT_FLOAT_EQ(sky.fogStrength, 1.0F);
}

TEST(SkyLightingLinkTest, AnUntouchedSkyLeavesTheFrameAlone) {
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    WorldEnvironmentFrame frame = ResolveWorldEnvironmentFrame(entityManager, Math::Vector3{0.1F, 0.2F, 0.3F});
    const Math::Vector3 ambientBefore = frame.skyAmbient;
    const Math::Vector3 groundBefore = frame.groundAmbient;
    const Math::Vector3 fogBefore = frame.fogColor;

    EXPECT_FALSE(frame.skyAmbientLinked);
    EXPECT_FALSE(frame.skyFogLinked);
    EXPECT_FALSE(frame.skyReflectionsLinked);

    ApplySunToWorldEnvironmentFrame(frame, SunAtAltitude(60.0F), Math::Vector3{1.0F, 1.0F, 1.0F}, 1.0F);
    ExpectVectorNear(frame.skyAmbient, ambientBefore);
    ExpectVectorNear(frame.groundAmbient, groundBefore);
    ExpectVectorNear(frame.fogColor, fogBefore);
}

// ---------------------------------------------------------------------------------------------
// The ambient follows the sun through the air rather than through a hand-picked curve.
// ---------------------------------------------------------------------------------------------

TEST(SkyLightingLinkTest, SunHighAboveGivesABrightBlueFill) {
    const SkyAmbient ambient = ResolveSkyAmbient(SkyWithLink(), SunAtAltitude(70.0F));
    EXPECT_GT(ambient.sky.y, ambient.sky.x);
    EXPECT_GT(ambient.sky.z, ambient.sky.x);
    EXPECT_FLOAT_EQ(ambient.daylight, 1.0F);
    // The ground bounces what lands on it, so it is dimmer than the sky that lights it.
    EXPECT_LT(Math::Length(ambient.ground), Math::Length(ambient.sky));
}

TEST(SkyLightingLinkTest, LowSunGoesWarmAndTheNadirGoesDark) {
    const SkyAmbient noon = ResolveSkyAmbient(SkyWithLink(), SunAtAltitude(70.0F));
    const SkyAmbient setting = ResolveSkyAmbient(SkyWithLink(), SunAtAltitude(2.0F));

    // Redder than it was: the blue channel falls off faster through more air, which is why a
    // sunset is orange rather than simply dimmer.
    const float noonWarmth = noon.sky.x / noon.sky.z;
    const float settingWarmth = setting.sky.x / setting.sky.z;
    EXPECT_GT(settingWarmth, noonWarmth);

    const SkyAmbient night = ResolveSkyAmbient(SkyWithLink(), SunAtAltitude(-30.0F));
    EXPECT_FLOAT_EQ(night.daylight, 0.0F);
    ExpectVectorNear(night.sky, Math::Vector3{0.0F, 0.0F, 0.0F});
    ExpectVectorNear(night.ground, Math::Vector3{0.0F, 0.0F, 0.0F});
}

TEST(SkyLightingLinkTest, DaylightRisesMonotonicallyWithTheSun) {
    float previous = -1.0F;
    for (float altitude = -30.0F; altitude <= 30.0F; altitude += 2.0F) {
        const SkyAmbient ambient = ResolveSkyAmbient(SkyWithLink(), SunAtAltitude(altitude));
        EXPECT_GE(ambient.daylight, previous) << "altitude " << altitude;
        previous = ambient.daylight;
    }
    EXPECT_FLOAT_EQ(previous, 1.0F);
}

TEST(SkyLightingLinkTest, StrengthZeroGivesNoFillAndOneMatchesTheDefault) {
    Sky3DComponent off = SkyWithLink();
    off.ambientStrength = 0.0F;
    ExpectVectorNear(ResolveSkyAmbient(off, SunAtAltitude(45.0F)).sky, Math::Vector3{0.0F, 0.0F, 0.0F});

    Sky3DComponent full = SkyWithLink();
    full.ambientStrength = 1.0F;
    const SkyAmbient atFull = ResolveSkyAmbient(full, SunAtAltitude(45.0F));
    const SkyAmbient atDefault = ResolveSkyAmbient(SkyWithLink(), SunAtAltitude(45.0F));
    ExpectVectorNear(atFull.sky, atDefault.sky);
}

TEST(SkyLightingLinkTest, TheSkyColourActuallyReachesTheFill) {
    Sky3DComponent green = SkyWithLink();
    green.skyColor = Math::Vector3{0.0F, 1.0F, 0.0F};
    const SkyAmbient tinted = ResolveSkyAmbient(green, SunAtAltitude(70.0F));
    EXPECT_GT(tinted.sky.y, tinted.sky.x);
    EXPECT_GT(tinted.sky.y, tinted.sky.z);
}

TEST(SkyLightingLinkTest, ABadDirectionIsTreatedAsTheZenithRatherThanPropagatingNaN) {
    const SkyAmbient nan = ResolveSkyAmbient(SkyWithLink(),
                                                   Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F});
    EXPECT_TRUE(std::isfinite(nan.sky.x) && std::isfinite(nan.sky.y) && std::isfinite(nan.sky.z));
    ExpectVectorNear(nan.sky, ResolveSkyAmbient(SkyWithLink(), SunAtAltitude(90.0F)).sky);
}

// ---------------------------------------------------------------------------------------------
// Fog: the horizon the distance actually dissolves into.
// ---------------------------------------------------------------------------------------------

TEST(SkyLightingLinkTest, FogSitsBetweenTheGroundAndTheHorizon) {
    const Math::Vector3 ground{0.1F, 0.1F, 0.1F};
    const Math::Vector3 horizon{0.9F, 0.9F, 0.9F};
    const Math::Vector3 fog = ResolveSkyFogColor(ground, horizon);
    EXPECT_GT(fog.x, ground.x);
    EXPECT_LT(fog.x, horizon.x);
    // Weighted toward the horizon: fog is the far distance, which is the horizon band.
    EXPECT_GT(fog.x, 0.5F * (ground.x + horizon.x));
}

TEST(SkyLightingLinkTest, FogFromSkyBlendsByStrengthInsteadOfSnapping) {
    WorldEnvironmentFrame authored;
    authored.fogColor = Math::Vector3{0.2F, 0.25F, 0.3F};

    WorldEnvironmentFrame half = authored;
    half.skyFogLinked = true;
    half.groundColor = Math::Vector3{0.0F, 0.1F, 0.2F};
    half.horizonColor = Math::Vector3{0.8F, 0.9F, 1.0F};
    // A strength of zero must reproduce the authored colour exactly, so the switch can be flipped
    // back and forth without the fog drifting.
    const Math::Vector3 skyFog = ResolveSkyFogColor(half.groundColor, half.horizonColor);
    const Math::Vector3 atZero{half.fogColor.x + (skyFog.x - half.fogColor.x) * 0.0F,
                                  half.fogColor.y + (skyFog.y - half.fogColor.y) * 0.0F,
                                  half.fogColor.z + (skyFog.z - half.fogColor.z) * 0.0F};
    ExpectVectorNear(atZero, authored.fogColor);

    const Math::Vector3 atOne{half.fogColor.x + (skyFog.x - half.fogColor.x) * 1.0F,
                                 half.fogColor.y + (skyFog.y - half.fogColor.y) * 1.0F,
                                 half.fogColor.z + (skyFog.z - half.fogColor.z) * 1.0F};
    ExpectVectorNear(atOne, skyFog);
}

// ---------------------------------------------------------------------------------------------
// The sun pass, where all three links actually land.
// ---------------------------------------------------------------------------------------------

TEST(SkyLightingLinkTest, TheSunPassWritesTheSkyFillIntoTheFrame) {
    WorldEnvironmentFrame frame;
    frame.hasEnvironment = true;
    frame.skyAmbientLinked = true;
    frame.skyColor = Math::Vector3{0.3F, 0.45F, 0.7F};
    frame.horizonColor = Math::Vector3{0.9F, 0.85F, 0.8F};
    frame.groundColor = Math::Vector3{0.2F, 0.18F, 0.15F};

    ApplySunToWorldEnvironmentFrame(frame, SunAtAltitude(60.0F), Math::Vector3{1.0F, 0.95F, 0.9F}, 1.0F);
    const SkyAmbient expected = ResolveSkyAmbient(frame.skyColor, frame.horizonColor, frame.groundColor, 1.0F,
                                                        1.0F, SunAtAltitude(60.0F));
    ExpectVectorNear(frame.skyAmbient, expected.sky);
    ExpectVectorNear(frame.groundAmbient, expected.ground);
}

TEST(SkyLightingLinkTest, NoAmbientSourceStillMeansNoAmbient) {
    WorldEnvironmentFrame frame;
    frame.hasEnvironment = true;
    frame.ambientSource = WorldEnvironmentAmbientSource::None;
    frame.skyAmbientLinked = true;
    frame.skyAmbient = Math::Vector3{0.0F, 0.0F, 0.0F};
    frame.groundAmbient = Math::Vector3{0.0F, 0.0F, 0.0F};

    ApplySunToWorldEnvironmentFrame(frame, SunAtAltitude(60.0F), Math::Vector3{1.0F, 1.0F, 1.0F}, 1.0F);
    // The environment said none; a Sky offering fill does not overrule an explicit choice.
    ExpectVectorNear(frame.skyAmbient, Math::Vector3{0.0F, 0.0F, 0.0F});
    ExpectVectorNear(frame.groundAmbient, Math::Vector3{0.0F, 0.0F, 0.0F});
}

TEST(SkyLightingLinkTest, ReflectionBandsAreTheSkyColourScaledByDaylight) {
    WorldEnvironmentFrame frame;
    frame.hasEnvironment = true;
    frame.skyReflectionsLinked = true;
    frame.skyColor = Math::Vector3{0.3F, 0.45F, 0.7F};
    frame.horizonColor = Math::Vector3{0.9F, 0.85F, 0.8F};
    frame.groundColor = Math::Vector3{0.2F, 0.18F, 0.15F};

    ApplySunToWorldEnvironmentFrame(frame, SunAtAltitude(70.0F), Math::Vector3{1.0F, 1.0F, 1.0F}, 1.0F);
    ExpectVectorNear(frame.reflectionSky, frame.skyColor);
    ExpectVectorNear(frame.reflectionHorizon, frame.horizonColor);
    ExpectVectorNear(frame.reflectionGround, frame.groundColor);

    // Below the horizon there is nothing to reflect: a mirror under a dark sky shows dark.
    ApplySunToWorldEnvironmentFrame(frame, SunAtAltitude(-30.0F), Math::Vector3{1.0F, 1.0F, 1.0F}, 1.0F);
    ExpectVectorNear(frame.reflectionSky, Math::Vector3{0.0F, 0.0F, 0.0F});
}

TEST(SkyLightingLinkTest, ReflectionsSurviveAnAmbientSourceOfNone) {
    WorldEnvironmentFrame frame;
    frame.hasEnvironment = true;
    frame.ambientSource = WorldEnvironmentAmbientSource::None;
    frame.skyReflectionsLinked = true;
    frame.skyColor = Math::Vector3{0.3F, 0.45F, 0.7F};

    ApplySunToWorldEnvironmentFrame(frame, SunAtAltitude(70.0F), Math::Vector3{1.0F, 1.0F, 1.0F}, 1.0F);
    // Reflections are not ambient: turning the ambient off does not turn the mirror off.
    ExpectVectorNear(frame.reflectionSky, frame.skyColor);
}

TEST(SkyLightingLinkTest, WithoutASunTheReflectionLinkDropsRatherThanGoingBlack) {
    WorldEnvironmentFrame frame;
    frame.hasEnvironment = true;
    frame.skyReflectionsLinked = true;
    frame.skyColor = Math::Vector3{0.3F, 0.45F, 0.7F};

    // Energy 0 is the state before a directional light exists. The bands would be zero, and a
    // mirror fed zero is a black mirror, so the link gives way to the ambient hemisphere instead.
    ApplySunToWorldEnvironmentFrame(frame, SunAtAltitude(70.0F), Math::Vector3{1.0F, 1.0F, 1.0F}, 0.0F);
    EXPECT_FALSE(frame.skyReflectionsLinked);
    ExpectVectorNear(frame.reflectionSky, Math::Vector3{0.0F, 0.0F, 0.0F});
}

// ---------------------------------------------------------------------------------------------
// Output and behaviour. The whole-sky controls, which is why this file - nominally about the
// lighting link - is also where the Sky component's own contract is pinned.
// ---------------------------------------------------------------------------------------------

TEST(SkyOutputTest, LuminanceAndAnimationAreNeutralByDefault) {
    const Sky3DComponent sky;
    EXPECT_FLOAT_EQ(sky.luminance, 1.0F);
    EXPECT_TRUE(sky.animate);
}

TEST(SkyOutputTest, TheFrameCarriesTheSkyOutputToTheRenderer) {
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    const Scene::Entity skyEntity = entityManager.CreateEntity();
    Sky3DComponent authored;
    authored.luminance = 2.0F;
    authored.animate = false;
    entityManager.AddComponent<Sky3DComponent>(skyEntity, authored);

    const WorldEnvironmentFrame frame = ResolveWorldEnvironmentFrame(entityManager, Math::Vector3{});
    EXPECT_FLOAT_EQ(frame.skyLuminance, 2.0F);
    EXPECT_FALSE(frame.skyAnimated);

    // No sky at all is the neutral frame: the renderer must not quietly fade a scene that has none.
    Memory::MemoryManager emptyMemory;
    Events::EventSystem emptyEvents;
    EntityManager emptyManager{emptyMemory.GetDefaultAllocator(), emptyEvents};
    const WorldEnvironmentFrame empty = ResolveWorldEnvironmentFrame(emptyManager, Math::Vector3{});
    EXPECT_FLOAT_EQ(empty.skyLuminance, 1.0F);
    EXPECT_TRUE(empty.skyAnimated);
}

TEST(SkyOutputTest, ZeroLuminanceIsBlackAndTheCeilingIsFinite) {
    // Zero is a legal answer - a sky that contributes nothing - and it must survive validation so
    // an author can fade a sky out instead of deleting it.
    Sky3DComponent sky;
    sky.luminance = 0.0F;
    EXPECT_TRUE(IsSky3DObjectComponentValid(sky));
    sky.luminance = 8.0F;
    EXPECT_TRUE(IsSky3DObjectComponentValid(sky));

    sky.luminance = -0.01F;
    EXPECT_FALSE(IsSky3DObjectComponentValid(sky));
    sky.luminance = 8.01F;
    EXPECT_FALSE(IsSky3DObjectComponentValid(sky));
    sky.luminance = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsSky3DObjectComponentValid(sky));
}

// ---------------------------------------------------------------------------------------------
// Guard rails on the authored numbers.
// ---------------------------------------------------------------------------------------------

TEST(SkyLightingLinkTest, OutOfRangeStrengthsAreRejected) {
    Sky3DComponent sky;
    EXPECT_TRUE(IsSky3DObjectComponentValid(sky));

    sky.ambientStrength = -0.1F;
    EXPECT_FALSE(IsSky3DObjectComponentValid(sky));
    sky.ambientStrength = 1.1F;
    EXPECT_FALSE(IsSky3DObjectComponentValid(sky));
    sky.ambientStrength = 0.5F;
    EXPECT_TRUE(IsSky3DObjectComponentValid(sky));

    sky.fogStrength = -0.1F;
    EXPECT_FALSE(IsSky3DObjectComponentValid(sky));
    sky.fogStrength = 1.0F;
    EXPECT_TRUE(IsSky3DObjectComponentValid(sky));

    sky.fogStrength = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsSky3DObjectComponentValid(sky));
}

} // namespace
} // namespace UVE::Scene::Tests
