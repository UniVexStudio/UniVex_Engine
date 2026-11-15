// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "matrix4x4.h"
#include "particle_system_asset.h"
#include "vfx_templates.h"
#include "vfx_world.h"

namespace UVE::VFX::Tests {
namespace {

using Asset::ParticleModuleTypeUVE;
using Asset::ParticleSystemAssetUVE;
using Asset::ParticleUserParameterTypeUVE;
using Asset::ParticleUserParameterUVE;

class ScopedTempDirUVE final {
public:
    explicit ScopedTempDirUVE(const std::string& suffix) {
        m_path = std::filesystem::temp_directory_path() / ("uve_vfx_world_test_" + suffix);
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }

    ~ScopedTempDirUVE() {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }

    [[nodiscard]] const std::filesystem::path& PathUVE() const noexcept { return m_path; }

private:
    std::filesystem::path m_path;
};

[[nodiscard]] ParticleSystemAssetUVE BuildConfigurableRateSystemUVE(const float rateValue,
                                                                   const bool looping = true,
                                                                   const float cullDistance = 0.0F) {
    ParticleSystemAssetUVE sys = CreateParticleTemplateUVE(ParticleTemplateKindUVE::Empty);
    sys.looping = looping;
    sys.duration = 0.5F;
    sys.emitters[0].maxParticles = 5000U;
    sys.emitters[0].emitterUpdate[0].floats["Rate"].constant = rateValue;
    for (Asset::ParticleScalabilityUVE& row : sys.scalability) {
        row.spawnScale = 1.0F;
        row.maxParticlesCap = 0U;
        row.cullDistance = cullDistance;
    }
    sys.scalability[0].spawnScale = 0.25F;
    sys.scalability[0].maxParticlesCap = 120U;
    sys.parameters.push_back(ParticleUserParameterUVE{
        "Tint", ParticleUserParameterTypeUVE::Color, {1.0F, 0.5F, 0.25F}, 1.0F});
    return sys;
}

[[nodiscard]] VfxFrameViewUVE BuildLookForwardViewUVE(const Math::Vector3UVE& camPos) noexcept {
    VfxFrameViewUVE view;
    view.hasCamera = true;
    view.cameraPosition = camPos;
    const Math::Matrix4x4UVE viewMat =
        Math::Matrix4x4UVE::ViewFromPositionAndRotationUVE(camPos, Math::QuaternionUVE{});
    const Math::Matrix4x4UVE projMat =
        Math::Matrix4x4UVE::PerspectiveUVE(1.04719755F, 16.0F / 9.0F, 0.1F, 100.0F);
    view.frustum = Math::FrustumUVE::FromViewProjectionUVE(projMat * viewMat);
    return view;
}

TEST(VfxWorldUVETest, HandleGenerationPreventsStaleAliasAfterKillAndSlotReuse) {
    ScopedTempDirUVE tempDir("handles");
    const std::filesystem::path assetPath = tempDir.PathUVE() / "VFX" / "test_loop.uvfx";
    std::filesystem::create_directories(assetPath.parent_path());
    ASSERT_TRUE(Asset::SaveParticleSystemAssetUVE(BuildConfigurableRateSystemUVE(20.0F), assetPath));

    VfxWorldUVE world;
    world.SetProjectRootUVE(tempDir.PathUVE());

    const VfxHandleUVE first = world.SpawnUVE("VFX/test_loop.uvfx");
    ASSERT_TRUE(first.IsValidUVE());
    EXPECT_TRUE(world.IsAliveUVE(first));
    EXPECT_TRUE(world.KillUVE(first));
    EXPECT_FALSE(world.IsAliveUVE(first));
    EXPECT_FALSE(world.StopUVE(first));

    const VfxHandleUVE second = world.SpawnUVE("VFX/test_loop.uvfx");
    ASSERT_TRUE(second.IsValidUVE());
    EXPECT_EQ(second.index, first.index);
    EXPECT_NE(second.generation, first.generation);
    EXPECT_FALSE(world.IsAliveUVE(first));
    EXPECT_TRUE(world.IsAliveUVE(second));
    EXPECT_EQ(VfxHandleUVE::FromPackedInt64UVE(second.ToPackedInt64UVE()), second);
}

TEST(VfxWorldUVETest, StopAllowsExistingParticlesToExpireThenRetiresSlot) {
    VfxWorldUVE world;
    ParticleSystemAssetUVE asset = BuildConfigurableRateSystemUVE(50.0F);
    const VfxHandleUVE handle = world.SpawnFromAssetUVE("stop_test", asset);
    ASSERT_TRUE(handle.IsValidUVE());

    for (int i = 0; i < 10; ++i) {
        world.TickUVE(0.05F);
    }
    const VfxInstanceUVE* inst = world.TryGetInstanceUVE(handle);
    ASSERT_NE(inst, nullptr);
    EXPECT_GT(inst->GetLiveCountUVE(), 0U);

    EXPECT_TRUE(world.StopUVE(handle));
    for (int i = 0; i < 30; ++i) {
        world.TickUVE(0.05F);
    }
    EXPECT_FALSE(world.IsAliveUVE(handle));
    EXPECT_EQ(world.GetStatsUVE().activeInstanceCount, 0U);
}

TEST(VfxWorldUVETest, GlobalBudgetProportionallyScalesSpawningWhenOverLimit) {
    VfxWorldUVE world;
    world.SetGlobalParticleBudgetUVE(100U);

    const ParticleSystemAssetUVE asset = BuildConfigurableRateSystemUVE(400.0F);
    const VfxHandleUVE handle = world.SpawnFromAssetUVE("budget_test", asset);
    ASSERT_TRUE(handle.IsValidUVE());

    // Accumulate 200 live particles (2x budget).
    for (int i = 0; i < 10; ++i) {
        world.TickUVE(0.05F);
    }
    ASSERT_GE(world.GetStatsUVE().totalLiveParticles, 100U);
    world.TickUVE(1.0F / 60.0F);
    EXPECT_LT(world.GetStatsUVE().lastBudgetScale, 1.0F);
    EXPECT_GT(world.GetStatsUVE().lastBudgetScale, 0.0F);
}

TEST(VfxWorldUVETest, QualityLevelAndDistanceLodCullFarSpawns) {
    VfxWorldUVE world;
    world.SetQualityLevelUVE(0U); // Low -> cap 120, scale 0.25

    const ParticleSystemAssetUVE farCulled = BuildConfigurableRateSystemUVE(200.0F, true, 10.0F);
    VfxOwnerUVE owner;
    owner.position = Math::Vector3UVE{0.0F, 0.0F, -25.0F}; // 25m away > cullDistance 10m
    const VfxHandleUVE farHandle = world.SpawnFromAssetUVE("far_cull", farCulled, owner);
    ASSERT_TRUE(farHandle.IsValidUVE());

    const VfxFrameViewUVE view = BuildLookForwardViewUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    for (int i = 0; i < 10; ++i) {
        world.TickUVE(0.05F, view);
    }
    const VfxInstanceUVE* inst = world.TryGetInstanceUVE(farHandle);
    ASSERT_NE(inst, nullptr);
    EXPECT_EQ(inst->GetLiveCountUVE(), 0U);
    EXPECT_GT(world.GetStatsUVE().distanceCulledSpawns, 0U);
}

TEST(VfxWorldUVETest, OutOfFrustumInstanceUpdatesAtQuarterRateAndPreservesDt) {
    VfxWorldUVE world;
    const ParticleSystemAssetUVE sys = BuildConfigurableRateSystemUVE(100.0F);

    VfxOwnerUVE inViewOwner;
    inViewOwner.position = Math::Vector3UVE{0.0F, 0.0F, -5.0F}; // in front of camera (-Z)
    VfxOwnerUVE behindOwner;
    behindOwner.position = Math::Vector3UVE{0.0F, 0.0F, 25.0F}; // behind camera (+Z), outside frustum

    const VfxHandleUVE inHandle = world.SpawnFromAssetUVE("quarter_test", sys, inViewOwner, 1ULL, 77U);
    const VfxHandleUVE outHandle = world.SpawnFromAssetUVE("quarter_test", sys, behindOwner, 2ULL, 77U);

    const VfxFrameViewUVE view = BuildLookForwardViewUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F});

    world.TickUVE(0.05F, view);
    const VfxInstanceUVE* inInst = world.TryGetInstanceUVE(inHandle);
    const VfxInstanceUVE* outInst = world.TryGetInstanceUVE(outHandle);
    ASSERT_NE(inInst, nullptr);
    ASSERT_NE(outInst, nullptr);
    EXPECT_GT(inInst->GetLiveCountUVE(), 0U);
    EXPECT_EQ(outInst->GetLiveCountUVE(), 0U); // skipped frame 1/3

    world.TickUVE(0.05F, view);
    EXPECT_EQ(outInst->GetLiveCountUVE(), 0U); // skipped frame 2/3

    world.TickUVE(0.05F, view);
    EXPECT_EQ(outInst->GetLiveCountUVE(), 0U); // skipped frame 3/3

    world.TickUVE(0.05F, view); // frame 4 catches up full accumulated 0.20s!
    EXPECT_EQ(outInst->GetLiveCountUVE(), inInst->GetLiveCountUVE());
    EXPECT_GE(world.GetStatsUVE().quarterRateSkippedTicks, 3U);
}

TEST(VfxWorldUVETest, HotReloadRecompilesAndPreservesOverridesSafely) {
    ScopedTempDirUVE tempDir("hotreload");
    const std::filesystem::path assetPath = tempDir.PathUVE() / "VFX" / "hot.uvfx";
    std::filesystem::create_directories(assetPath.parent_path());
    ASSERT_TRUE(Asset::SaveParticleSystemAssetUVE(BuildConfigurableRateSystemUVE(10.0F), assetPath));

    VfxWorldUVE world;
    world.SetProjectRootUVE(tempDir.PathUVE());
    const VfxHandleUVE handle = world.SpawnUVE("VFX/hot.uvfx", {}, 42ULL, 1234U);
    ASSERT_TRUE(handle.IsValidUVE());
    EXPECT_TRUE(world.SetParameterColorUVE(handle, "Tint", Math::ColorUVE{0.1F, 0.9F, 0.2F}, 0.75F));

    world.TickUVE(0.05F);
    EXPECT_EQ(world.GetStatsUVE().hotReloadCount, 0U);

    // Ensure distinct filesystem modification time.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ASSERT_TRUE(Asset::SaveParticleSystemAssetUVE(BuildConfigurableRateSystemUVE(120.0F), assetPath));
    // Bump last_write_time explicitly in case coarse filesystem clocks coincide.
    std::error_code ec;
    const auto oldTime = std::filesystem::last_write_time(assetPath, ec);
    if (!ec) {
        std::filesystem::last_write_time(assetPath, oldTime + std::chrono::seconds(2), ec);
    }

    EXPECT_EQ(world.PollHotReloadUVE(), 1U);
    EXPECT_EQ(world.GetStatsUVE().hotReloadCount, 1U);
    ASSERT_TRUE(world.IsAliveUVE(handle));

    const VfxInstanceUVE* reloaded = world.TryGetInstanceUVE(handle);
    ASSERT_NE(reloaded, nullptr);
    EXPECT_EQ(reloaded->GetInstanceSeedUVE(), 1234U);
    ASSERT_EQ(reloaded->GetParametersUVE().size(), 1U);
    EXPECT_NEAR(reloaded->GetParametersUVE()[0].vector.y, 0.9F, 1e-5F);
    EXPECT_NEAR(reloaded->GetParametersUVE()[0].alpha, 0.75F, 1e-5F);

    // Corrupt reload leaves the running instance intact.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    {
        std::ofstream bad(assetPath, std::ios::binary | std::ios::trunc);
        bad << "not valid uvfx";
    }
    EXPECT_EQ(world.PollHotReloadUVE(), 0U);
    EXPECT_TRUE(world.IsAliveUVE(handle));
}

} // namespace
} // namespace UVE::VFX::Tests
