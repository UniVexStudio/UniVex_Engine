// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "world_environment_3d.h"

#include <limits>

#include <gtest/gtest.h>

#include "visibility_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"

namespace UVE::Scene::Tests {
namespace {

class WorldEnvironmentTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
};

TEST_F(WorldEnvironmentTest, MissingEnvironmentKeepsTheFallbackAmbient) {
    const Math::Vector3 fallback{0.1F, 0.2F, 0.3F};
    const WorldEnvironmentFrame frame = ResolveWorldEnvironmentFrame(entityManager, fallback);
    EXPECT_FALSE(frame.hasEnvironment);
    EXPECT_EQ(frame.ambientColor, fallback);
    EXPECT_EQ(frame.skyAmbient, fallback);
    EXPECT_EQ(frame.groundAmbient, fallback);
    EXPECT_FLOAT_EQ(frame.exposure, 1.0F);
    EXPECT_FALSE(frame.fogEnabled);
    EXPECT_FALSE(frame.postProcessingEnabled);
}

TEST_F(WorldEnvironmentTest, AmbientSourceSelectsNoneFlatSkyAndEnvironmentMapLighting) {
    const Entity environmentEntity = entityManager.CreateEntity();
    WorldEnvironment3DComponent authored{};
    authored.ambientColor = Math::Vector3{0.2F, 0.3F, 0.4F};
    authored.ambientEnergy = 2.0F;
    authored.skyColor = Math::Vector3{0.5F, 0.6F, 0.7F};
    authored.groundColor = Math::Vector3{0.1F, 0.2F, 0.3F};
    authored.skyAssetPath = "environment/day.hdr";
    entityManager.AddComponent<WorldEnvironment3DComponent>(environmentEntity, authored);
    WorldEnvironment3DComponent& environment =
        entityManager.GetComponent<WorldEnvironment3DComponent>(environmentEntity);

    environment.ambientSource = WorldEnvironmentAmbientSource::None;
    WorldEnvironmentFrame frame = ResolveWorldEnvironmentFrame(entityManager, Math::Vector3{});
    EXPECT_EQ(frame.ambientSource, WorldEnvironmentAmbientSource::None);
    EXPECT_EQ(frame.ambientColor, Math::Vector3{});
    EXPECT_EQ(frame.skyAmbient, Math::Vector3{});
    EXPECT_EQ(frame.groundAmbient, Math::Vector3{});

    environment.ambientSource = WorldEnvironmentAmbientSource::FlatColor;
    frame = ResolveWorldEnvironmentFrame(entityManager, Math::Vector3{});
    EXPECT_EQ(frame.ambientSource, WorldEnvironmentAmbientSource::FlatColor);
    EXPECT_EQ(frame.ambientColor, (Math::Vector3{0.4F, 0.6F, 0.8F}));
    EXPECT_EQ(frame.skyAmbient, Math::Vector3{});
    EXPECT_EQ(frame.groundAmbient, Math::Vector3{});

    environment.ambientSource = WorldEnvironmentAmbientSource::Sky;
    frame = ResolveWorldEnvironmentFrame(entityManager, Math::Vector3{});
    EXPECT_EQ(frame.ambientSource, WorldEnvironmentAmbientSource::Sky);
    EXPECT_EQ(frame.ambientColor, (Math::Vector3{0.4F, 0.6F, 0.8F}));
    EXPECT_NEAR(frame.skyAmbient.x, 0.2F, 1.0e-5F);
    EXPECT_NEAR(frame.skyAmbient.y, 0.36F, 1.0e-5F);
    EXPECT_NEAR(frame.skyAmbient.z, 0.56F, 1.0e-5F);
    EXPECT_NEAR(frame.groundAmbient.x, 0.04F, 1.0e-5F);
    EXPECT_NEAR(frame.groundAmbient.y, 0.12F, 1.0e-5F);
    EXPECT_NEAR(frame.groundAmbient.z, 0.24F, 1.0e-5F);

    environment.ambientSource = WorldEnvironmentAmbientSource::EnvironmentMap;
    frame = ResolveWorldEnvironmentFrame(entityManager, Math::Vector3{});
    EXPECT_EQ(frame.ambientSource, WorldEnvironmentAmbientSource::EnvironmentMap);
    EXPECT_EQ(frame.skyAssetPath, authored.skyAssetPath);
    EXPECT_NEAR(frame.skyAmbient.x, 0.2F, 1.0e-5F); // Procedural fallback while the map is unavailable.
    EXPECT_NEAR(frame.skyAmbient.y, 0.36F, 1.0e-5F);
    EXPECT_NEAR(frame.skyAmbient.z, 0.56F, 1.0e-5F);
}

// A default environment does not change the picture. Adding one used to switch the effects stack
// on, so the act of adding an environment looked like a colour change; post-processing now starts
// off, and an author turns on what they want. The sky and its ambient do resolve, because those are
// the reason to add an environment in the first place.
TEST_F(WorldEnvironmentTest, DefaultEnvironmentLightsTheSceneWithoutSwitchingTheEffectsOn) {
    EXPECT_TRUE(IsWorldEnvironment3DObjectComponentValid(WorldEnvironment3DComponent{}));
    const Entity environment = entityManager.CreateEntity();
    entityManager.AddComponent<WorldEnvironment3DComponent>(environment, WorldEnvironment3DComponent{});

    const WorldEnvironmentFrame frame =
        ResolveWorldEnvironmentFrame(entityManager, Math::Vector3{0.05F, 0.05F, 0.05F});
    EXPECT_TRUE(frame.hasEnvironment);
    EXPECT_FALSE(frame.postProcessingEnabled);
    EXPECT_TRUE(frame.bloomEnabled);
    EXPECT_FLOAT_EQ(frame.bloomSoftKnee, 0.0F);
    EXPECT_EQ(frame.bloomMipCount, 1U);
    EXPECT_FLOAT_EQ(frame.vignetteIntensity, 0.0F);
    EXPECT_FLOAT_EQ(frame.vignetteRadius, 0.65F);
    EXPECT_FLOAT_EQ(frame.chromaticAberrationIntensity, 0.0F);
    EXPECT_FLOAT_EQ(frame.filmGrainIntensity, 0.0F);
    EXPECT_FLOAT_EQ(frame.lensDistortionIntensity, 0.0F);
    EXPECT_FALSE(frame.depthOfFieldEnabled);
    EXPECT_EQ(frame.depthOfFieldFocusMode, WorldEnvironmentDepthOfFieldFocusMode::Manual);
    EXPECT_EQ(frame.depthOfFieldBokehShape, WorldEnvironmentDepthOfFieldBokehShape::Circular);
    EXPECT_FLOAT_EQ(frame.depthOfFieldFocusDistance, 10.0F);
    EXPECT_FLOAT_EQ(frame.depthOfFieldAperture, 0.5F);
    EXPECT_EQ(frame.depthOfFieldQuality, 1U);
    EXPECT_FALSE(frame.motionBlurEnabled);
    EXPECT_FLOAT_EQ(frame.motionBlurStrength, 0.5F);
    EXPECT_EQ(frame.motionBlurSampleCount, 8U);
    EXPECT_TRUE(frame.ssaoEnabled);
    EXPECT_EQ(frame.ambientSource, WorldEnvironmentAmbientSource::Sky);
    EXPECT_EQ(frame.skyColor, (Math::Vector3{0.31F, 0.54F, 0.91F}));
    EXPECT_EQ(frame.groundColor, (Math::Vector3{0.24F, 0.21F, 0.18F}));
    EXPECT_EQ(frame.fogMode, WorldEnvironmentFogMode::Height);
    EXPECT_EQ(frame.backgroundColor, frame.horizonColor);
    EXPECT_GT(frame.skyAmbient.z, frame.groundAmbient.z);
    EXPECT_GT(frame.groundAmbient.x, frame.skyAmbient.x * 0.5F);
}

TEST_F(WorldEnvironmentTest, FirstValidEnvironmentDrivesSkyGroundFogBloomAndGrade) {
    const Entity environment = entityManager.CreateEntity();
    WorldEnvironment3DComponent authored{};
    authored.ambientColor = Math::Vector3{0.5F, 0.5F, 0.5F};
    authored.ambientEnergy = 2.0F;
    authored.skyColor = Math::Vector3{0.2F, 0.4F, 1.0F};
    authored.horizonColor = Math::Vector3{0.8F, 0.9F, 1.0F};
    authored.groundColor = Math::Vector3{0.4F, 0.2F, 0.1F};
    authored.fogColor = Math::Vector3{0.8F, 0.7F, 0.6F};
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
    authored.colorFilter = Math::Vector3{1.0F, 0.95F, 0.9F};
    authored.vignetteIntensity = 0.75F;
    authored.vignetteRadius = 0.55F;
    authored.chromaticAberrationIntensity = 0.4F;
    authored.filmGrainIntensity = 0.35F;
    authored.lensDistortionIntensity = 0.5F;
    authored.depthOfFieldEnabled = true;
    authored.depthOfFieldFocusMode = WorldEnvironmentDepthOfFieldFocusMode::ScreenCenter;
    authored.depthOfFieldBokehShape = WorldEnvironmentDepthOfFieldBokehShape::Hexagonal;
    authored.depthOfFieldFocusDistance = 24.0F;
    authored.depthOfFieldAperture = 0.8F;
    authored.depthOfFieldQuality = 2U;
    authored.motionBlurEnabled = true;
    authored.motionBlurStrength = 0.75F;
    authored.motionBlurSampleCount = 12U;
    entityManager.AddComponent<WorldEnvironment3DComponent>(environment, authored);

    const WorldEnvironmentFrame frame =
        ResolveWorldEnvironmentFrame(entityManager, Math::Vector3{0.1F, 0.1F, 0.1F});
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
    EXPECT_EQ(frame.depthOfFieldFocusMode, WorldEnvironmentDepthOfFieldFocusMode::ScreenCenter);
    EXPECT_EQ(frame.depthOfFieldBokehShape, WorldEnvironmentDepthOfFieldBokehShape::Hexagonal);
    EXPECT_FLOAT_EQ(frame.depthOfFieldFocusDistance, 24.0F);
    EXPECT_FLOAT_EQ(frame.depthOfFieldAperture, 0.8F);
    EXPECT_EQ(frame.depthOfFieldQuality, 2U);
    EXPECT_TRUE(frame.motionBlurEnabled);
    EXPECT_FLOAT_EQ(frame.motionBlurStrength, 0.75F);
    EXPECT_EQ(frame.motionBlurSampleCount, 12U);
    EXPECT_EQ(frame.backgroundColor, authored.horizonColor);
    EXPECT_TRUE(frame.skyAssetPath.empty());
}

TEST_F(WorldEnvironmentTest, FogModeAndLinearRangeCopyOntoTheFrame) {
    const Entity environment = entityManager.CreateEntity();
    WorldEnvironment3DComponent authored{};
    authored.fogEnabled = true;
    authored.fogMode = WorldEnvironmentFogMode::Linear;
    authored.fogStart = 12.5F;
    authored.fogEnd = 240.0F;
    entityManager.AddComponent<WorldEnvironment3DComponent>(environment, authored);

    const WorldEnvironmentFrame frame =
        ResolveWorldEnvironmentFrame(entityManager, Math::Vector3{0.1F, 0.1F, 0.1F});
    ASSERT_TRUE(frame.hasEnvironment);
    EXPECT_TRUE(frame.fogEnabled);
    EXPECT_EQ(frame.fogMode, WorldEnvironmentFogMode::Linear);
    EXPECT_FLOAT_EQ(frame.fogStart, 12.5F);
    EXPECT_FLOAT_EQ(frame.fogEnd, 240.0F);
}

TEST_F(WorldEnvironmentTest, SkyAssetPathCopiesOntoTheFrame) {
    const Entity environment = entityManager.CreateEntity();
    WorldEnvironment3DComponent authored{};
    authored.skyAssetPath = "environment/day.hdr";
    entityManager.AddComponent<WorldEnvironment3DComponent>(environment, authored);

    const WorldEnvironmentFrame frame =
        ResolveWorldEnvironmentFrame(entityManager, Math::Vector3{0.1F, 0.1F, 0.1F});
    EXPECT_TRUE(frame.hasEnvironment);
    EXPECT_EQ(frame.skyAssetPath, authored.skyAssetPath);
}

TEST_F(WorldEnvironmentTest, InvalidEnvironmentIsSkipped) {
    const Entity bad = entityManager.CreateEntity();
    WorldEnvironment3DComponent invalid{};
    invalid.exposure = 0.0F;
    entityManager.AddComponent<WorldEnvironment3DComponent>(bad, invalid);

    const Entity good = entityManager.CreateEntity();
    WorldEnvironment3DComponent authored{};
    authored.ambientColor = Math::Vector3{0.2F, 0.3F, 0.4F};
    authored.fogEnabled = false;
    entityManager.AddComponent<WorldEnvironment3DComponent>(good, authored);

    const WorldEnvironmentFrame frame =
        ResolveWorldEnvironmentFrame(entityManager, Math::Vector3{});
    EXPECT_TRUE(frame.hasEnvironment);
    EXPECT_EQ(frame.ambientColor, authored.ambientColor);
    EXPECT_EQ(frame.backgroundColor, authored.horizonColor);
    EXPECT_FALSE(frame.fogEnabled);
}

TEST_F(WorldEnvironmentTest, HiddenEnvironmentIsSkipped) {
    const Entity hiddenEnv = entityManager.CreateEntity();
    entityManager.AddComponent<WorldEnvironment3DComponent>(
        hiddenEnv, WorldEnvironment3DComponent{});
    VisibilityComponent hidden{};
    hidden.visible = false;
    hidden.visibleInHierarchy = false;
    entityManager.AddComponent<VisibilityComponent>(hiddenEnv, hidden);

    const WorldEnvironmentFrame frame =
        ResolveWorldEnvironmentFrame(entityManager, Math::Vector3{0.1F, 0.2F, 0.3F});
    EXPECT_FALSE(frame.hasEnvironment);
}

TEST(WorldEnvironmentValidationTest, RejectsBrokenSkyBloomAndGrade) {
    WorldEnvironment3DComponent environment{};
    EXPECT_TRUE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment.ambientSource = static_cast<WorldEnvironmentAmbientSource>(255U);
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.skyCurve = 0.0F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.bloomIntensity = -0.1F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.bloomSoftKnee = -0.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.bloomSoftKnee = 1.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.bloomSoftKnee = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.bloomMipCount = 0U;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.bloomMipCount = kMaximumWorldEnvironmentBloomMipCount + 1U;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.ssaoRadius = 0.0F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.contrast = -0.1F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.vignetteIntensity = -0.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.vignetteIntensity = 1.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.vignetteIntensity = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.vignetteRadius = 1.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.vignetteRadius = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.chromaticAberrationIntensity = -0.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.chromaticAberrationIntensity = 1.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.chromaticAberrationIntensity = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.filmGrainIntensity = -0.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.filmGrainIntensity = 1.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.filmGrainIntensity = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.lensDistortionIntensity = -0.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.lensDistortionIntensity = 1.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.lensDistortionIntensity = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.depthOfFieldFocusMode = static_cast<WorldEnvironmentDepthOfFieldFocusMode>(255U);
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.depthOfFieldBokehShape = static_cast<WorldEnvironmentDepthOfFieldBokehShape>(255U);
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.depthOfFieldFocusDistance = 0.09F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.depthOfFieldFocusDistance = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.depthOfFieldAperture = -0.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.depthOfFieldAperture = 1.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.depthOfFieldQuality = 3U;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.motionBlurStrength = 1.01F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.motionBlurStrength = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.motionBlurSampleCount = 6U;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.skyColor.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.fogHeightFalloff = 0.0F;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.fogMode = static_cast<WorldEnvironmentFogMode>(255U);
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.fogStart = environment.fogEnd;
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
    environment = {};
    environment.fogEnd = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsWorldEnvironment3DObjectComponentValid(environment));
}

TEST(WorldEnvironmentSunTest, NoonKeepsAmbientAndNightDarkens) {
    WorldEnvironmentFrame noon{};
    noon.hasEnvironment = true;
    noon.skyAmbient = Math::Vector3{0.2F, 0.3F, 0.5F};
    noon.groundAmbient = Math::Vector3{0.1F, 0.08F, 0.06F};
    WorldEnvironmentFrame night = noon;
    ApplySunToWorldEnvironmentFrame(noon, Math::Vector3{0.0F, 1.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F},
                                       1.0F);
    ApplySunToWorldEnvironmentFrame(night, Math::Vector3{0.0F, -1.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F},
                                       1.0F);
    EXPECT_NEAR(noon.skyAmbient.z, 0.5F, 0.02F);
    EXPECT_LT(night.skyAmbient.z, noon.skyAmbient.z * 0.2F);
}

TEST(SkyEquirectTest, AxisDirectionsMapToStableUv) {
    Math::Vector2 uv{};
    ASSERT_TRUE(TryMakeSkyEquirectUv(Math::Vector3{1.0F, 0.0F, 0.0F}, uv));
    EXPECT_NEAR(uv.x, 0.5F, 1.0e-5F);
    EXPECT_NEAR(uv.y, 0.5F, 1.0e-5F);
    ASSERT_TRUE(TryMakeSkyEquirectUv(Math::Vector3{0.0F, 1.0F, 0.0F}, uv));
    EXPECT_NEAR(uv.x, 0.5F, 1.0e-5F);
    EXPECT_NEAR(uv.y, 1.0F, 1.0e-5F);
    ASSERT_TRUE(TryMakeSkyEquirectUv(Math::Vector3{0.0F, -1.0F, 0.0F}, uv));
    EXPECT_NEAR(uv.x, 0.5F, 1.0e-5F);
    EXPECT_NEAR(uv.y, 0.0F, 1.0e-5F);
    ASSERT_TRUE(TryMakeSkyEquirectUv(Math::Vector3{0.0F, 0.0F, 1.0F}, uv));
    EXPECT_NEAR(uv.x, 0.75F, 1.0e-5F);
    EXPECT_NEAR(uv.y, 0.5F, 1.0e-5F);
}

TEST(SkyEquirectTest, ZeroAndNonFiniteDirectionsFailClosed) {
    Math::Vector2 uv{1.0F, 1.0F};
    EXPECT_FALSE(TryMakeSkyEquirectUv(Math::Vector3{}, uv));
    EXPECT_EQ(uv, (Math::Vector2{}));
    EXPECT_FALSE(TryMakeSkyEquirectUv(Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}, uv));
}

TEST(WorldEnvironmentSunTest, HorizonSunWarmsTheGround) {
    WorldEnvironmentFrame frame{};
    frame.hasEnvironment = true;
    frame.skyAmbient = Math::Vector3{0.2F, 0.2F, 0.4F};
    frame.groundAmbient = Math::Vector3{0.2F, 0.2F, 0.2F};
    ApplySunToWorldEnvironmentFrame(frame, Math::Vector3{0.0F, 0.0F, 1.0F}, Math::Vector3{1.0F, 0.8F, 0.4F},
                                       1.0F);
    EXPECT_GT(frame.groundAmbient.x, frame.groundAmbient.z);
}

TEST(WorldEnvironmentSunTest, VolumeBlendingAndCameraDefaultPreviewPrecedenceWorkDeterministically) {
    WorldEnvironment3DComponent envA{};
    envA.exposure = 1.0F;
    envA.bloomIntensity = 0.2F;
    WorldEnvironment3DComponent envB{};
    envB.exposure = 2.0F;
    envB.bloomIntensity = 0.8F;

    const WorldEnvironment3DComponent blended = BlendWorldEnvironment3DComponents(envA, envB, 0.25F);
    EXPECT_NEAR(blended.exposure, 1.25F, 1.0e-5F);
    EXPECT_NEAR(blended.bloomIntensity, 0.35F, 1.0e-5F);

    WorldEnvironment3DComponent preview{};
    preview.exposure = 0.9F;
    WorldEnvironment3DComponent projectDefault{};
    projectDefault.exposure = 1.1F;
    WorldEnvironment3DComponent cameraOverride{};
    cameraOverride.exposure = 1.75F;

    EXPECT_FLOAT_EQ(ResolveEffectiveWorldEnvironment3D(&cameraOverride, &envB, &projectDefault, &preview).exposure,
                    1.75F);
    EXPECT_FLOAT_EQ(ResolveEffectiveWorldEnvironment3D(nullptr, &envB, &projectDefault, &preview).exposure, 2.0F);
    EXPECT_FLOAT_EQ(ResolveEffectiveWorldEnvironment3D(nullptr, nullptr, &projectDefault, &preview).exposure, 1.1F);
    EXPECT_FLOAT_EQ(ResolveEffectiveWorldEnvironment3D(nullptr, nullptr, nullptr, &preview).exposure, 0.9F);
}

} // namespace
} // namespace UVE::Scene::Tests
