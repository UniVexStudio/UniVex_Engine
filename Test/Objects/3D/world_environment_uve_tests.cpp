// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/world_environment_3d_uve.h"

#include <limits>

#include <gtest/gtest.h>

#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"

namespace UVE::Scene::Tests {
namespace {

class WorldEnvironmentUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
};

TEST_F(WorldEnvironmentUVETest, MissingEnvironmentKeepsTheFallbackAmbient) {
    const Math::Vector3UVE fallback{0.1F, 0.2F, 0.3F};
    const WorldEnvironmentFrameUVE frame = ResolveWorldEnvironmentFrameUVE(entityManager, fallback);
    EXPECT_FALSE(frame.hasEnvironment);
    EXPECT_EQ(frame.ambientColor, fallback);
    EXPECT_EQ(frame.skyAmbient, fallback);
    EXPECT_EQ(frame.groundAmbient, fallback);
    EXPECT_FLOAT_EQ(frame.exposure, 1.0F);
    EXPECT_FALSE(frame.fogEnabled);
    EXPECT_FALSE(frame.postProcessingEnabled);
}

TEST_F(WorldEnvironmentUVETest, AmbientSourceSelectsNoneFlatSkyAndEnvironmentMapLighting) {
    const EntityUVE environmentEntity = entityManager.CreateEntityUVE();
    WorldEnvironment3DComponentUVE authored{};
    authored.ambientColor = Math::Vector3UVE{0.2F, 0.3F, 0.4F};
    authored.ambientEnergy = 2.0F;
    authored.skyColor = Math::Vector3UVE{0.5F, 0.6F, 0.7F};
    authored.groundColor = Math::Vector3UVE{0.1F, 0.2F, 0.3F};
    authored.skyAssetPath = "environment/day.hdr";
    entityManager.AddComponentUVE<WorldEnvironment3DComponentUVE>(environmentEntity, authored);
    WorldEnvironment3DComponentUVE& environment =
        entityManager.GetComponentUVE<WorldEnvironment3DComponentUVE>(environmentEntity);

    environment.ambientSource = WorldEnvironmentAmbientSourceUVE::None;
    WorldEnvironmentFrameUVE frame = ResolveWorldEnvironmentFrameUVE(entityManager, Math::Vector3UVE{});
    EXPECT_EQ(frame.ambientSource, WorldEnvironmentAmbientSourceUVE::None);
    EXPECT_EQ(frame.ambientColor, Math::Vector3UVE{});
    EXPECT_EQ(frame.skyAmbient, Math::Vector3UVE{});
    EXPECT_EQ(frame.groundAmbient, Math::Vector3UVE{});

    environment.ambientSource = WorldEnvironmentAmbientSourceUVE::FlatColor;
    frame = ResolveWorldEnvironmentFrameUVE(entityManager, Math::Vector3UVE{});
    EXPECT_EQ(frame.ambientSource, WorldEnvironmentAmbientSourceUVE::FlatColor);
    EXPECT_EQ(frame.ambientColor, (Math::Vector3UVE{0.4F, 0.6F, 0.8F}));
    EXPECT_EQ(frame.skyAmbient, Math::Vector3UVE{});
    EXPECT_EQ(frame.groundAmbient, Math::Vector3UVE{});

    environment.ambientSource = WorldEnvironmentAmbientSourceUVE::Sky;
    frame = ResolveWorldEnvironmentFrameUVE(entityManager, Math::Vector3UVE{});
    EXPECT_EQ(frame.ambientSource, WorldEnvironmentAmbientSourceUVE::Sky);
    EXPECT_EQ(frame.ambientColor, (Math::Vector3UVE{0.4F, 0.6F, 0.8F}));
    EXPECT_NEAR(frame.skyAmbient.x, 0.2F, 1.0e-5F);
    EXPECT_NEAR(frame.skyAmbient.y, 0.36F, 1.0e-5F);
    EXPECT_NEAR(frame.skyAmbient.z, 0.56F, 1.0e-5F);
    EXPECT_NEAR(frame.groundAmbient.x, 0.04F, 1.0e-5F);
    EXPECT_NEAR(frame.groundAmbient.y, 0.12F, 1.0e-5F);
    EXPECT_NEAR(frame.groundAmbient.z, 0.24F, 1.0e-5F);

    environment.ambientSource = WorldEnvironmentAmbientSourceUVE::EnvironmentMap;
    frame = ResolveWorldEnvironmentFrameUVE(entityManager, Math::Vector3UVE{});
    EXPECT_EQ(frame.ambientSource, WorldEnvironmentAmbientSourceUVE::EnvironmentMap);
    EXPECT_EQ(frame.skyAssetPath, authored.skyAssetPath);
    EXPECT_NEAR(frame.skyAmbient.x, 0.2F, 1.0e-5F); // Procedural fallback while the map is unavailable.
    EXPECT_NEAR(frame.skyAmbient.y, 0.36F, 1.0e-5F);
    EXPECT_NEAR(frame.skyAmbient.z, 0.56F, 1.0e-5F);
}

TEST_F(WorldEnvironmentUVETest, DefaultEnvironmentEnablesBloomSsaoAndARealSky) {
    EXPECT_TRUE(IsWorldEnvironment3DObjectComponentValidUVE(WorldEnvironment3DComponentUVE{}));
    const EntityUVE environment = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<WorldEnvironment3DComponentUVE>(environment, WorldEnvironment3DComponentUVE{});

    const WorldEnvironmentFrameUVE frame =
        ResolveWorldEnvironmentFrameUVE(entityManager, Math::Vector3UVE{0.05F, 0.05F, 0.05F});
    EXPECT_TRUE(frame.hasEnvironment);
    EXPECT_TRUE(frame.postProcessingEnabled);
    EXPECT_TRUE(frame.bloomEnabled);
    EXPECT_FLOAT_EQ(frame.bloomSoftKnee, 0.0F);
    EXPECT_EQ(frame.bloomMipCount, 1U);
    EXPECT_FLOAT_EQ(frame.vignetteIntensity, 0.0F);
    EXPECT_FLOAT_EQ(frame.vignetteRadius, 0.65F);
    EXPECT_FLOAT_EQ(frame.chromaticAberrationIntensity, 0.0F);
    EXPECT_FLOAT_EQ(frame.filmGrainIntensity, 0.0F);
    EXPECT_FLOAT_EQ(frame.lensDistortionIntensity, 0.0F);
    EXPECT_FALSE(frame.depthOfFieldEnabled);
    EXPECT_EQ(frame.depthOfFieldFocusMode, WorldEnvironmentDepthOfFieldFocusModeUVE::Manual);
    EXPECT_EQ(frame.depthOfFieldBokehShape, WorldEnvironmentDepthOfFieldBokehShapeUVE::Circular);
    EXPECT_FLOAT_EQ(frame.depthOfFieldFocusDistance, 10.0F);
    EXPECT_FLOAT_EQ(frame.depthOfFieldAperture, 0.5F);
    EXPECT_EQ(frame.depthOfFieldQuality, 1U);
    EXPECT_FALSE(frame.motionBlurEnabled);
    EXPECT_FLOAT_EQ(frame.motionBlurStrength, 0.5F);
    EXPECT_EQ(frame.motionBlurSampleCount, 8U);
    EXPECT_TRUE(frame.ssaoEnabled);
    EXPECT_EQ(frame.ambientSource, WorldEnvironmentAmbientSourceUVE::Sky);
    EXPECT_EQ(frame.skyColor, (Math::Vector3UVE{0.31F, 0.54F, 0.91F}));
    EXPECT_EQ(frame.groundColor, (Math::Vector3UVE{0.24F, 0.21F, 0.18F}));
    EXPECT_EQ(frame.fogMode, WorldEnvironmentFogModeUVE::Height);
    EXPECT_EQ(frame.backgroundColor, frame.horizonColor);
    EXPECT_GT(frame.skyAmbient.z, frame.groundAmbient.z);
    EXPECT_GT(frame.groundAmbient.x, frame.skyAmbient.x * 0.5F);
}

TEST_F(WorldEnvironmentUVETest, FirstValidEnvironmentDrivesSkyGroundFogBloomAndGrade) {
    const EntityUVE environment = entityManager.CreateEntityUVE();
    WorldEnvironment3DComponentUVE authored{};
    authored.ambientColor = Math::Vector3UVE{0.5F, 0.5F, 0.5F};
    authored.ambientEnergy = 2.0F;
    authored.skyColor = Math::Vector3UVE{0.2F, 0.4F, 1.0F};
    authored.horizonColor = Math::Vector3UVE{0.8F, 0.9F, 1.0F};
    authored.groundColor = Math::Vector3UVE{0.4F, 0.2F, 0.1F};
    authored.fogColor = Math::Vector3UVE{0.8F, 0.7F, 0.6F};
    authored.fogDensity = 0.04F;
    authored.fogEnabled = true;
    authored.fogSkyAffect = 0.2F;
    authored.exposure = 1.5F;
    authored.postProcessingEnabled = true;
    authored.bloomIntensity = 0.8F;
    authored.bloomThreshold = 1.2F;
    authored.bloomSoftKnee = 0.6F;
    authored.bloomMipCount = 4U;
    authored.ssaoIntensity = 1.4F;
    authored.ssaoRadius = 0.7F;
    authored.brightness = 0.05F;
    authored.contrast = 1.1F;
    authored.saturation = 0.9F;
    authored.colorFilter = Math::Vector3UVE{1.0F, 0.95F, 0.9F};
    authored.vignetteIntensity = 0.75F;
    authored.vignetteRadius = 0.55F;
    authored.chromaticAberrationIntensity = 0.4F;
    authored.filmGrainIntensity = 0.35F;
    authored.lensDistortionIntensity = 0.5F;
    authored.depthOfFieldEnabled = true;
    authored.depthOfFieldFocusMode = WorldEnvironmentDepthOfFieldFocusModeUVE::ScreenCenter;
    authored.depthOfFieldBokehShape = WorldEnvironmentDepthOfFieldBokehShapeUVE::Hexagonal;
    authored.depthOfFieldFocusDistance = 24.0F;
    authored.depthOfFieldAperture = 0.8F;
    authored.depthOfFieldQuality = 2U;
    authored.motionBlurEnabled = true;
    authored.motionBlurStrength = 0.75F;
    authored.motionBlurSampleCount = 12U;
    entityManager.AddComponentUVE<WorldEnvironment3DComponentUVE>(environment, authored);

    const WorldEnvironmentFrameUVE frame =
        ResolveWorldEnvironmentFrameUVE(entityManager, Math::Vector3UVE{0.1F, 0.1F, 0.1F});
    EXPECT_TRUE(frame.hasEnvironment);
    EXPECT_NEAR(frame.ambientColor.x, 1.0F, 1.0e-5F);
    EXPECT_NEAR(frame.skyAmbient.z, 1.0F, 1.0e-5F);
    EXPECT_NEAR(frame.groundAmbient.x, 0.4F, 1.0e-5F);
    EXPECT_EQ(frame.skyColor, authored.skyColor);
    EXPECT_EQ(frame.horizonColor, authored.horizonColor);
    EXPECT_EQ(frame.groundColor, authored.groundColor);
    EXPECT_EQ(frame.fogColor, authored.fogColor);
    EXPECT_FLOAT_EQ(frame.fogDensity, 0.04F);
    EXPECT_FLOAT_EQ(frame.fogSkyAffect, 0.2F);
    EXPECT_TRUE(frame.fogEnabled);
    EXPECT_FLOAT_EQ(frame.exposure, 1.5F);
    EXPECT_TRUE(frame.postProcessingEnabled);
    EXPECT_FLOAT_EQ(frame.bloomIntensity, 0.8F);
    EXPECT_FLOAT_EQ(frame.bloomSoftKnee, 0.6F);
    EXPECT_EQ(frame.bloomMipCount, 4U);
    EXPECT_FLOAT_EQ(frame.ssaoRadius, 0.7F);
    EXPECT_FLOAT_EQ(frame.contrast, 1.1F);
    EXPECT_EQ(frame.colorFilter, authored.colorFilter);
    EXPECT_FLOAT_EQ(frame.vignetteIntensity, 0.75F);
    EXPECT_FLOAT_EQ(frame.vignetteRadius, 0.55F);
    EXPECT_FLOAT_EQ(frame.chromaticAberrationIntensity, 0.4F);
    EXPECT_FLOAT_EQ(frame.filmGrainIntensity, 0.35F);
    EXPECT_FLOAT_EQ(frame.lensDistortionIntensity, 0.5F);
    EXPECT_TRUE(frame.depthOfFieldEnabled);
    EXPECT_EQ(frame.depthOfFieldFocusMode, WorldEnvironmentDepthOfFieldFocusModeUVE::ScreenCenter);
    EXPECT_EQ(frame.depthOfFieldBokehShape, WorldEnvironmentDepthOfFieldBokehShapeUVE::Hexagonal);
    EXPECT_FLOAT_EQ(frame.depthOfFieldFocusDistance, 24.0F);
    EXPECT_FLOAT_EQ(frame.depthOfFieldAperture, 0.8F);
    EXPECT_EQ(frame.depthOfFieldQuality, 2U);
    EXPECT_TRUE(frame.motionBlurEnabled);
    EXPECT_FLOAT_EQ(frame.motionBlurStrength, 0.75F);
    EXPECT_EQ(frame.motionBlurSampleCount, 12U);
    EXPECT_EQ(frame.backgroundColor, authored.horizonColor);
    EXPECT_TRUE(frame.skyAssetPath.empty());
}

TEST_F(WorldEnvironmentUVETest, FogModeAndLinearRangeCopyOntoTheFrame) {
    const EntityUVE environment = entityManager.CreateEntityUVE();
    WorldEnvironment3DComponentUVE authored{};
    authored.fogEnabled = true;
    authored.fogMode = WorldEnvironmentFogModeUVE::Linear;
    authored.fogStart = 12.5F;
    authored.fogEnd = 240.0F;
    entityManager.AddComponentUVE<WorldEnvironment3DComponentUVE>(environment, authored);

    const WorldEnvironmentFrameUVE frame =
        ResolveWorldEnvironmentFrameUVE(entityManager, Math::Vector3UVE{0.1F, 0.1F, 0.1F});
    ASSERT_TRUE(frame.hasEnvironment);
    EXPECT_TRUE(frame.fogEnabled);
    EXPECT_EQ(frame.fogMode, WorldEnvironmentFogModeUVE::Linear);
    EXPECT_FLOAT_EQ(frame.fogStart, 12.5F);
    EXPECT_FLOAT_EQ(frame.fogEnd, 240.0F);
}

TEST_F(WorldEnvironmentUVETest, SkyAssetPathCopiesOntoTheFrame) {
    const EntityUVE environment = entityManager.CreateEntityUVE();
    WorldEnvironment3DComponentUVE authored{};
    authored.skyAssetPath = "environment/day.hdr";
    entityManager.AddComponentUVE<WorldEnvironment3DComponentUVE>(environment, authored);

    const WorldEnvironmentFrameUVE frame =
        ResolveWorldEnvironmentFrameUVE(entityManager, Math::Vector3UVE{0.1F, 0.1F, 0.1F});
    EXPECT_TRUE(frame.hasEnvironment);
    EXPECT_EQ(frame.skyAssetPath, authored.skyAssetPath);
}

TEST_F(WorldEnvironmentUVETest, InvalidEnvironmentIsSkipped) {
    const EntityUVE bad = entityManager.CreateEntityUVE();
    WorldEnvironment3DComponentUVE invalid{};
    invalid.exposure = 0.0F;
    entityManager.AddComponentUVE<WorldEnvironment3DComponentUVE>(bad, invalid);

    const EntityUVE good = entityManager.CreateEntityUVE();
    WorldEnvironment3DComponentUVE authored{};
    authored.ambientColor = Math::Vector3UVE{0.2F, 0.3F, 0.4F};
    authored.fogEnabled = false;
    entityManager.AddComponentUVE<WorldEnvironment3DComponentUVE>(good, authored);

    const WorldEnvironmentFrameUVE frame =
        ResolveWorldEnvironmentFrameUVE(entityManager, Math::Vector3UVE{});
    EXPECT_TRUE(frame.hasEnvironment);
    EXPECT_EQ(frame.ambientColor, authored.ambientColor);
    EXPECT_EQ(frame.backgroundColor, authored.horizonColor);
    EXPECT_FALSE(frame.fogEnabled);
}

TEST(WorldEnvironmentValidationUVETest, RejectsBrokenSkyBloomAndGrade) {
    WorldEnvironment3DComponentUVE environment{};
    EXPECT_TRUE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment.ambientSource = static_cast<WorldEnvironmentAmbientSourceUVE>(255U);
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.skyCurve = 0.0F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.bloomIntensity = -0.1F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.bloomSoftKnee = -0.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.bloomSoftKnee = 1.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.bloomSoftKnee = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.bloomMipCount = 0U;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.bloomMipCount = kMaximumWorldEnvironmentBloomMipCountUVE + 1U;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.ssaoRadius = 0.0F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.contrast = -0.1F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.vignetteIntensity = -0.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.vignetteIntensity = 1.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.vignetteIntensity = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.vignetteRadius = 1.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.vignetteRadius = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.chromaticAberrationIntensity = -0.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.chromaticAberrationIntensity = 1.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.chromaticAberrationIntensity = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.filmGrainIntensity = -0.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.filmGrainIntensity = 1.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.filmGrainIntensity = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.lensDistortionIntensity = -0.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.lensDistortionIntensity = 1.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.lensDistortionIntensity = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.depthOfFieldFocusMode = static_cast<WorldEnvironmentDepthOfFieldFocusModeUVE>(255U);
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.depthOfFieldBokehShape = static_cast<WorldEnvironmentDepthOfFieldBokehShapeUVE>(255U);
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.depthOfFieldFocusDistance = 0.09F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.depthOfFieldFocusDistance = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.depthOfFieldAperture = -0.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.depthOfFieldAperture = 1.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.depthOfFieldQuality = 3U;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.motionBlurStrength = 1.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.motionBlurStrength = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.motionBlurSampleCount = 6U;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.skyColor.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.fogHeightFalloff = 0.0F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.fogMode = static_cast<WorldEnvironmentFogModeUVE>(255U);
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.fogStart = environment.fogEnd;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
    environment = {};
    environment.fogEnd = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValidUVE(environment));
}

TEST(WorldEnvironmentSunUVETest, NoonKeepsAmbientAndNightDarkens) {
    WorldEnvironmentFrameUVE noon{};
    noon.hasEnvironment = true;
    noon.skyAmbient = Math::Vector3UVE{0.2F, 0.3F, 0.5F};
    noon.groundAmbient = Math::Vector3UVE{0.1F, 0.08F, 0.06F};
    WorldEnvironmentFrameUVE night = noon;
    ApplySunToWorldEnvironmentFrameUVE(noon, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, Math::Vector3UVE{1.0F, 1.0F, 1.0F},
                                       1.0F);
    ApplySunToWorldEnvironmentFrameUVE(night, Math::Vector3UVE{0.0F, -1.0F, 0.0F}, Math::Vector3UVE{1.0F, 1.0F, 1.0F},
                                       1.0F);
    EXPECT_NEAR(noon.skyAmbient.z, 0.5F, 0.02F);
    EXPECT_LT(night.skyAmbient.z, noon.skyAmbient.z * 0.2F);
}

TEST(SkyEquirectUVETest, AxisDirectionsMapToStableUv) {
    Math::Vector2UVE uv{};
    ASSERT_TRUE(TryMakeSkyEquirectUvUVE(Math::Vector3UVE{1.0F, 0.0F, 0.0F}, uv));
    EXPECT_NEAR(uv.x, 0.5F, 1.0e-5F);
    EXPECT_NEAR(uv.y, 0.5F, 1.0e-5F);
    ASSERT_TRUE(TryMakeSkyEquirectUvUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F}, uv));
    EXPECT_NEAR(uv.x, 0.5F, 1.0e-5F);
    EXPECT_NEAR(uv.y, 1.0F, 1.0e-5F);
    ASSERT_TRUE(TryMakeSkyEquirectUvUVE(Math::Vector3UVE{0.0F, -1.0F, 0.0F}, uv));
    EXPECT_NEAR(uv.x, 0.5F, 1.0e-5F);
    EXPECT_NEAR(uv.y, 0.0F, 1.0e-5F);
    ASSERT_TRUE(TryMakeSkyEquirectUvUVE(Math::Vector3UVE{0.0F, 0.0F, 1.0F}, uv));
    EXPECT_NEAR(uv.x, 0.75F, 1.0e-5F);
    EXPECT_NEAR(uv.y, 0.5F, 1.0e-5F);
}

TEST(SkyEquirectUVETest, ZeroAndNonFiniteDirectionsFailClosed) {
    Math::Vector2UVE uv{1.0F, 1.0F};
    EXPECT_FALSE(TryMakeSkyEquirectUvUVE(Math::Vector3UVE{}, uv));
    EXPECT_EQ(uv, (Math::Vector2UVE{}));
    EXPECT_FALSE(TryMakeSkyEquirectUvUVE(Math::Vector3UVE{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}, uv));
}

TEST(WorldEnvironmentSunUVETest, HorizonSunWarmsTheGround) {
    WorldEnvironmentFrameUVE frame{};
    frame.hasEnvironment = true;
    frame.skyAmbient = Math::Vector3UVE{0.2F, 0.2F, 0.4F};
    frame.groundAmbient = Math::Vector3UVE{0.2F, 0.2F, 0.2F};
    ApplySunToWorldEnvironmentFrameUVE(frame, Math::Vector3UVE{0.0F, 0.0F, 1.0F}, Math::Vector3UVE{1.0F, 0.8F, 0.4F},
                                       1.0F);
    EXPECT_GT(frame.groundAmbient.x, frame.groundAmbient.z);
}

} // namespace
} // namespace UVE::Scene::Tests
