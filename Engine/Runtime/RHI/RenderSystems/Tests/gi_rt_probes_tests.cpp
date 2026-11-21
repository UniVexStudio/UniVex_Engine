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

TEST(GiReflectionProbesUVETest, UniformCubemapProjectsToConstantShIrradiance) {
    constexpr std::uint32_t kSize = 8U;
    std::vector<std::uint8_t> whiteFace(kSize * kSize * 4U, 255U);
    const std::array<std::span<const std::uint8_t>, 6U> faces{
        whiteFace, whiteFace, whiteFace, whiteFace, whiteFace, whiteFace};

    const SphericalHarmonicsL2UVE sh = ProjectCubemapRgba8ToShL2UVE(faces, kSize);
    EXPECT_FALSE(sh.IsZeroUVE());
    const Math::Vector3UVE upIrr = EvaluateShL2IrradianceUVE(sh, Math::Vector3UVE{0.0F, 1.0F, 0.0F});
    const Math::Vector3UVE sideIrr = EvaluateShL2IrradianceUVE(sh, Math::Vector3UVE{1.0F, 0.0F, 0.0F});
    EXPECT_NEAR(upIrr.x, 1.0F, 0.05F);
    EXPECT_NEAR(upIrr.y, 1.0F, 0.05F);
    EXPECT_NEAR(sideIrr.z, 1.0F, 0.05F);
}

TEST(GiReflectionProbesUVETest, BakedProbeAssetRoundTripsOnDisk) {
    BakedReflectionProbeAssetUVE original{};
    original.faceSize = 4U;
    original.diffuseSh.coefficients[0] = Math::Vector3UVE{0.4F, 0.5F, 0.6F};
    for (std::size_t f = 0U; f < 6U; ++f) {
        original.faceRgba8[f].assign(4U * 4U * 4U, static_cast<std::uint8_t>(20U + f * 30U));
    }

    const std::filesystem::path tmpPath =
        std::filesystem::temp_directory_path() / "uve_test_probe_roundtrip.uvprobe";
    ASSERT_TRUE(SaveReflectionProbeAssetUVE(tmpPath, original));

    BakedReflectionProbeAssetUVE loaded{};
    ASSERT_TRUE(LoadReflectionProbeAssetUVE(tmpPath, loaded));
    std::filesystem::remove(tmpPath);

    EXPECT_EQ(loaded.faceSize, 4U);
    EXPECT_NEAR(loaded.diffuseSh.coefficients[0].x, 0.4F, 1.0e-5F);
    EXPECT_NEAR(loaded.diffuseSh.coefficients[0].z, 0.6F, 1.0e-5F);
    EXPECT_EQ(loaded.faceRgba8[3][0], original.faceRgba8[3][0]);
}

TEST(GiReflectionProbesUVETest, HybridLightmapBakerGeneratesAtlasAndUpdatesProbes) {
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE em{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    const Scene::EntityUVE giEntity = em.CreateEntityUVE();
    Scene::WorldTransformComponentUVE giWt{};
    giWt.dirty = false;
    em.AddComponentUVE<Scene::WorldTransformComponentUVE>(giEntity, giWt);
    Scene::LightmapGI3DComponentUVE giComp{};
    giComp.halfExtents = Math::Vector3UVE{8.0F, 4.0F, 8.0F};
    em.AddComponentUVE<Scene::LightmapGI3DComponentUVE>(giEntity, giComp);

    const Scene::EntityUVE probeEntity = em.CreateEntityUVE();
    Scene::WorldTransformComponentUVE probeWt{};
    probeWt.worldPosition = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    probeWt.dirty = false;
    em.AddComponentUVE<Scene::WorldTransformComponentUVE>(probeEntity, probeWt);
    Scene::LightmapProbe3DComponentUVE probeComp{};
    probeComp.sampledIrradiance = Math::ColorUVE{0.0F, 0.0F, 0.0F};
    em.AddComponentUVE<Scene::LightmapProbe3DComponentUVE>(probeEntity, probeComp);

    LightDataUVE sun{};
    sun.type = Scene::LightTypeUVE::Directional;
    sun.direction = Math::Vector3UVE{0.0F, -1.0F, 0.0F};
    sun.color = Math::ColorUVE{1.0F, 0.95F, 0.9F};
    sun.intensity = 3.0F;
    const std::vector<LightDataUVE> lights{sun};

    LightmapBakeSettingsUVE settings{};
    settings.atlasWidth = 16U;
    settings.atlasHeight = 16U;
    const LightmapBakeResultUVE result = BakeSceneLightmapUVE(em, lights, settings);

    EXPECT_EQ(result.width, 16U);
    EXPECT_EQ(result.height, 16U);
    EXPECT_EQ(result.bakedTexels, 256U);
    EXPECT_EQ(result.updatedSceneProbes, 1U);
    EXPECT_TRUE(result.probeGrid.IsValidUVE());
    const auto& updatedProbe = em.GetComponentUVE<Scene::LightmapProbe3DComponentUVE>(probeEntity);
    EXPECT_GT(updatedProbe.sampledIrradiance.r, 0.1F);
}

TEST(RayTracedEffectsUVETest, SlabTracerDetectsShadowOcclusionAndBounceHit) {
    RtSceneBoxNodeUVE blocker{};
    blocker.minAndMetallic = {-1.0F, 2.0F, -1.0F, 0.0F};
    blocker.maxAndRoughness = {1.0F, 3.0F, 1.0F, 0.5F};
    blocker.albedo = {0.8F, 0.2F, 0.1F, 1.0F};
    const std::vector<RtSceneBoxNodeUVE> boxes{blocker};

    const RtSurfaceResultUVE occluded = EvaluateRtShadowAndBounceUVE(
        boxes, Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F},
        Math::Vector3UVE{0.0F, 1.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F},
        Math::Vector3UVE{2.0F, 2.0F, 2.0F}, Math::Vector3UVE{0.2F, 0.3F, 0.5F});
    EXPECT_FLOAT_EQ(occluded.sunVisibility, 0.0F);

    const RtSurfaceResultUVE unoccluded = EvaluateRtShadowAndBounceUVE(
        boxes, Math::Vector3UVE{5.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F},
        Math::Vector3UVE{0.0F, 1.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F},
        Math::Vector3UVE{2.0F, 2.0F, 2.0F}, Math::Vector3UVE{0.2F, 0.3F, 0.5F});
    EXPECT_FLOAT_EQ(unoccluded.sunVisibility, 1.0F);
    EXPECT_NEAR(unoccluded.reflectionRadiance.z, 0.5F, 1.0e-5F);
}

} // namespace
} // namespace UVE::Render
