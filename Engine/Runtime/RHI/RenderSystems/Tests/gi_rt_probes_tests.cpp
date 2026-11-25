// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "all_objects_3d.h"
#include "entity_manager.h"
#include "event_system.h"
#include "gi_reflection_probes.h"
#include "memory_manager.h"
#include "primitive_mesh_component.h"
#include "ray_traced_effects.h"
#include "world_transform_component.h"

namespace UVE::Render {
namespace {

TEST(GiReflectionProbesTest, UniformCubemapProjectsToConstantShIrradiance) {
    constexpr std::uint32_t kSize = 8U;
    std::vector<std::uint8_t> whiteFace(kSize * kSize * 4U, 255U);
    const std::array<std::span<const std::uint8_t>, 6U> faces{
        whiteFace, whiteFace, whiteFace, whiteFace, whiteFace, whiteFace};

    const SphericalHarmonicsL2 sh = ProjectCubemapRgba8ToShL2(faces, kSize);
    EXPECT_FALSE(sh.IsZero());
    const Math::Vector3 upIrr = EvaluateShL2Irradiance(sh, Math::Vector3{0.0F, 1.0F, 0.0F});
    const Math::Vector3 sideIrr = EvaluateShL2Irradiance(sh, Math::Vector3{1.0F, 0.0F, 0.0F});
    EXPECT_NEAR(upIrr.x, 1.0F, 0.05F);
    EXPECT_NEAR(upIrr.y, 1.0F, 0.05F);
    EXPECT_NEAR(sideIrr.z, 1.0F, 0.05F);
}

TEST(GiReflectionProbesTest, BakedProbeAssetRoundTripsOnDisk) {
    BakedReflectionProbeAsset original{};
    original.faceSize = 4U;
    original.diffuseSh.coefficients[0] = Math::Vector3{0.4F, 0.5F, 0.6F};
    for (std::size_t f = 0U; f < 6U; ++f) {
        original.faceRgba8[f].assign(4U * 4U * 4U, static_cast<std::uint8_t>(20U + f * 30U));
    }

    const std::filesystem::path tmpPath =
        std::filesystem::temp_directory_path() / "uve_test_probe_roundtrip.uvprobe";
    ASSERT_TRUE(SaveReflectionProbeAsset(tmpPath, original));

    BakedReflectionProbeAsset loaded{};
    ASSERT_TRUE(LoadReflectionProbeAsset(tmpPath, loaded));
    std::filesystem::remove(tmpPath);

    EXPECT_EQ(loaded.faceSize, 4U);
    EXPECT_NEAR(loaded.diffuseSh.coefficients[0].x, 0.4F, 1.0e-5F);
    EXPECT_NEAR(loaded.diffuseSh.coefficients[0].z, 0.6F, 1.0e-5F);
    EXPECT_EQ(loaded.faceRgba8[3][0], original.faceRgba8[3][0]);
}

TEST(GiReflectionProbesTest, HybridLightmapBakerGeneratesAtlasAndUpdatesProbes) {
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager em{memoryManager.GetDefaultAllocator(), eventSystem};
    const Scene::Entity giEntity = em.CreateEntity();
    Scene::WorldTransformComponent giWt{};
    giWt.dirty = false;
    em.AddComponent<Scene::WorldTransformComponent>(giEntity, giWt);
    Scene::LightmapGI3DComponent giComp{};
    giComp.halfExtents = Math::Vector3{8.0F, 4.0F, 8.0F};
    em.AddComponent<Scene::LightmapGI3DComponent>(giEntity, giComp);

    const Scene::Entity probeEntity = em.CreateEntity();
    Scene::WorldTransformComponent probeWt{};
    probeWt.worldPosition = Math::Vector3{0.0F, 1.0F, 0.0F};
    probeWt.dirty = false;
    em.AddComponent<Scene::WorldTransformComponent>(probeEntity, probeWt);
    Scene::LightmapProbe3DComponent probeComp{};
    probeComp.sampledIrradiance = Math::Color{0.0F, 0.0F, 0.0F};
    em.AddComponent<Scene::LightmapProbe3DComponent>(probeEntity, probeComp);

    LightData sun{};
    sun.type = Scene::LightType::Directional;
    sun.direction = Math::Vector3{0.0F, -1.0F, 0.0F};
    sun.color = Math::Color{1.0F, 0.95F, 0.9F};
    sun.intensity = 3.0F;
    const std::vector<LightData> lights{sun};

    LightmapBakeSettings settings{};
    settings.atlasWidth = 16U;
    settings.atlasHeight = 16U;
    const LightmapBakeResult result = BakeSceneLightmap(em, lights, settings);

    EXPECT_EQ(result.width, 16U);
    EXPECT_EQ(result.height, 16U);
    EXPECT_EQ(result.bakedTexels, 256U);
    EXPECT_EQ(result.updatedSceneProbes, 1U);
    EXPECT_TRUE(result.probeGrid.IsValid());
    const auto& updatedProbe = em.GetComponent<Scene::LightmapProbe3DComponent>(probeEntity);
    EXPECT_GT(updatedProbe.sampledIrradiance.r, 0.1F);
}

TEST(RayTracedEffectsTest, SlabTracerDetectsShadowOcclusionAndBounceHit) {
    RtSceneBoxNode blocker{};
    blocker.minAndMetallic = {-1.0F, 2.0F, -1.0F, 0.0F};
    blocker.maxAndRoughness = {1.0F, 3.0F, 1.0F, 0.5F};
    blocker.albedo = {0.8F, 0.2F, 0.1F, 1.0F};
    const std::vector<RtSceneBoxNode> boxes{blocker};

    const RtSurfaceResult occluded = EvaluateRtShadowAndBounce(
        boxes, Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F},
        Math::Vector3{0.0F, 1.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F},
        Math::Vector3{2.0F, 2.0F, 2.0F}, Math::Vector3{0.2F, 0.3F, 0.5F});
    EXPECT_FLOAT_EQ(occluded.sunVisibility, 0.0F);

    const RtSurfaceResult unoccluded = EvaluateRtShadowAndBounce(
        boxes, Math::Vector3{5.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F},
        Math::Vector3{0.0F, 1.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F},
        Math::Vector3{2.0F, 2.0F, 2.0F}, Math::Vector3{0.2F, 0.3F, 0.5F});
    EXPECT_FLOAT_EQ(unoccluded.sunVisibility, 1.0F);
    EXPECT_NEAR(unoccluded.reflectionRadiance.z, 0.5F, 1.0e-5F);
}

} // namespace
} // namespace UVE::Render
