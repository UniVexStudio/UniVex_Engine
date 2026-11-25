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

using Asset::ParticleModuleType;
using Asset::ParticleSystemAsset;
using Asset::ParticleUserParameterType;
using Asset::ParticleUserParameter;

class ScopedTempDir final {
public:
    explicit ScopedTempDir(const std::string& suffix) {
        m_path = std::filesystem::temp_directory_path() / ("uve_vfx_world_test_" + suffix);
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }

    ~ScopedTempDir() {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }

    [[nodiscard]] const std::filesystem::path& Path() const noexcept { return m_path; }

private:
    std::filesystem::path m_path;
};

[[nodiscard]] ParticleSystemAsset BuildConfigurableRateSystem(const float rateValue,
                                                                   const bool looping = true,
                                                                   const float cullDistance = 0.0F) {
    ParticleSystemAsset sys = CreateParticleTemplate(ParticleTemplateKind::Empty);
    sys.looping = looping;
    sys.duration = 0.5F;
    sys.emitters[0].maxParticles = 5000U;
    sys.emitters[0].emitterUpdate[0].floats["Rate"].constant = rateValue;
    for (Asset::ParticleScalability& row : sys.scalability) {
        row.spawnScale = 1.0F;
        row.maxParticlesCap = 0U;
        row.cullDistance = cullDistance;
    }
    sys.scalability[0].spawnScale = 0.25F;
    sys.scalability[0].maxParticlesCap = 120U;
    sys.parameters.push_back(ParticleUserParameter{
        "Tint", ParticleUserParameterType::Color, {1.0F, 0.5F, 0.25F}, 1.0F});
    return sys;
}

[[nodiscard]] VfxFrameView BuildLookForwardView(const Math::Vector3& camPos) noexcept {
    VfxFrameView view;
    view.hasCamera = true;
    view.cameraPosition = camPos;
    const Math::Matrix4x4 viewMat =
        Math::Matrix4x4::ViewFromPositionAndRotation(camPos, Math::Quaternion{});
    const Math::Matrix4x4 projMat =
        Math::Matrix4x4::Perspective(1.04719755F, 16.0F / 9.0F, 0.1F, 100.0F);
    view.frustum = Math::Frustum::FromViewProjection(projMat * viewMat);
    return view;
}

TEST(VfxWorldTest, HandleGenerationPreventsStaleAliasAfterKillAndSlotReuse) {
    ScopedTempDir tempDir("handles");
    const std::filesystem::path assetPath = tempDir.Path() / "VFX" / "test_loop.uvfx";
    std::filesystem::create_directories(assetPath.parent_path());
    ASSERT_TRUE(Asset::SaveParticleSystemAsset(BuildConfigurableRateSystem(20.0F), assetPath));

    VfxWorld world;
    world.SetProjectRoot(tempDir.Path());

    const VfxHandle first = world.Spawn("VFX/test_loop.uvfx");
    ASSERT_TRUE(first.IsValid());
    EXPECT_TRUE(world.IsAlive(first));
    EXPECT_TRUE(world.Kill(first));
    EXPECT_FALSE(world.IsAlive(first));
    EXPECT_FALSE(world.Stop(first));

    const VfxHandle second = world.Spawn("VFX/test_loop.uvfx");
    ASSERT_TRUE(second.IsValid());
    EXPECT_EQ(second.index, first.index);
    EXPECT_NE(second.generation, first.generation);
    EXPECT_FALSE(world.IsAlive(first));
    EXPECT_TRUE(world.IsAlive(second));
    EXPECT_EQ(VfxHandle::FromPackedInt64(second.ToPackedInt64()), second);
}

TEST(VfxWorldTest, StopAllowsExistingParticlesToExpireThenRetiresSlot) {
    VfxWorld world;
    ParticleSystemAsset asset = BuildConfigurableRateSystem(50.0F);
    const VfxHandle handle = world.SpawnFromAsset("stop_test", asset);
    ASSERT_TRUE(handle.IsValid());

    for (int i = 0; i < 10; ++i) {
        world.Tick(0.05F);
    }
    const VfxInstance* inst = world.TryGetInstance(handle);
    ASSERT_NE(inst, nullptr);
    EXPECT_GT(inst->GetLiveCount(), 0U);

    EXPECT_TRUE(world.Stop(handle));
    for (int i = 0; i < 30; ++i) {
        world.Tick(0.05F);
    }
    EXPECT_FALSE(world.IsAlive(handle));
    EXPECT_EQ(world.GetStats().activeInstanceCount, 0U);
}

TEST(VfxWorldTest, GlobalBudgetProportionallyScalesSpawningWhenOverLimit) {
    VfxWorld world;
    world.SetGlobalParticleBudget(100U);

    const ParticleSystemAsset asset = BuildConfigurableRateSystem(400.0F);
    const VfxHandle handle = world.SpawnFromAsset("budget_test", asset);
    ASSERT_TRUE(handle.IsValid());

    // Accumulate 200 live particles (2x budget).
    for (int i = 0; i < 10; ++i) {
        world.Tick(0.05F);
    }
    ASSERT_GE(world.GetStats().totalLiveParticles, 100U);
    world.Tick(1.0F / 60.0F);
    EXPECT_LT(world.GetStats().lastBudgetScale, 1.0F);
    EXPECT_GT(world.GetStats().lastBudgetScale, 0.0F);
}

TEST(VfxWorldTest, QualityLevelAndDistanceLodCullFarSpawns) {
    VfxWorld world;
    world.SetQualityLevel(0U); // Low -> cap 120, scale 0.25

    const ParticleSystemAsset farCulled = BuildConfigurableRateSystem(200.0F, true, 10.0F);
    VfxOwner owner;
    owner.position = Math::Vector3{0.0F, 0.0F, -25.0F}; // 25m away > cullDistance 10m
    const VfxHandle farHandle = world.SpawnFromAsset("far_cull", farCulled, owner);
    ASSERT_TRUE(farHandle.IsValid());

    const VfxFrameView view = BuildLookForwardView(Math::Vector3{0.0F, 0.0F, 0.0F});
    for (int i = 0; i < 10; ++i) {
        world.Tick(0.05F, view);
    }
    const VfxInstance* inst = world.TryGetInstance(farHandle);
    ASSERT_NE(inst, nullptr);
    EXPECT_EQ(inst->GetLiveCount(), 0U);
    EXPECT_GT(world.GetStats().distanceCulledSpawns, 0U);
}

TEST(VfxWorldTest, OutOfFrustumInstanceUpdatesAtQuarterRateAndPreservesDt) {
    VfxWorld world;
    const ParticleSystemAsset sys = BuildConfigurableRateSystem(100.0F);

    VfxOwner inViewOwner;
    inViewOwner.position = Math::Vector3{0.0F, 0.0F, -5.0F}; // in front of camera (-Z)
    VfxOwner behindOwner;
    behindOwner.position = Math::Vector3{0.0F, 0.0F, 25.0F}; // behind camera (+Z), outside frustum

    const VfxHandle inHandle = world.SpawnFromAsset("quarter_test", sys, inViewOwner, 1ULL, 77U);
    const VfxHandle outHandle = world.SpawnFromAsset("quarter_test", sys, behindOwner, 2ULL, 77U);

    const VfxFrameView view = BuildLookForwardView(Math::Vector3{0.0F, 0.0F, 0.0F});

    world.Tick(0.05F, view);
    const VfxInstance* inInst = world.TryGetInstance(inHandle);
    const VfxInstance* outInst = world.TryGetInstance(outHandle);
    ASSERT_NE(inInst, nullptr);
    ASSERT_NE(outInst, nullptr);
    EXPECT_GT(inInst->GetLiveCount(), 0U);
    EXPECT_EQ(outInst->GetLiveCount(), 0U); // skipped frame 1/3

    world.Tick(0.05F, view);
    EXPECT_EQ(outInst->GetLiveCount(), 0U); // skipped frame 2/3

    world.Tick(0.05F, view);
    EXPECT_EQ(outInst->GetLiveCount(), 0U); // skipped frame 3/3

    world.Tick(0.05F, view); // frame 4 catches up full accumulated 0.20s!
    EXPECT_EQ(outInst->GetLiveCount(), inInst->GetLiveCount());
    EXPECT_GE(world.GetStats().quarterRateSkippedTicks, 3U);
}

TEST(VfxWorldTest, HotReloadRecompilesAndPreservesOverridesSafely) {
    ScopedTempDir tempDir("hotreload");
    const std::filesystem::path assetPath = tempDir.Path() / "VFX" / "hot.uvfx";
    std::filesystem::create_directories(assetPath.parent_path());
    ASSERT_TRUE(Asset::SaveParticleSystemAsset(BuildConfigurableRateSystem(10.0F), assetPath));

    VfxWorld world;
    world.SetProjectRoot(tempDir.Path());
    const VfxHandle handle = world.Spawn("VFX/hot.uvfx", {}, 42ULL, 1234U);
    ASSERT_TRUE(handle.IsValid());
    EXPECT_TRUE(world.SetParameterColor(handle, "Tint", Math::Color{0.1F, 0.9F, 0.2F}, 0.75F));

    world.Tick(0.05F);
    EXPECT_EQ(world.GetStats().hotReloadCount, 0U);

    // Ensure distinct filesystem modification time.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ASSERT_TRUE(Asset::SaveParticleSystemAsset(BuildConfigurableRateSystem(120.0F), assetPath));
    // Bump last_write_time explicitly in case coarse filesystem clocks coincide.
    std::error_code ec;
    const auto oldTime = std::filesystem::last_write_time(assetPath, ec);
    if (!ec) {
        std::filesystem::last_write_time(assetPath, oldTime + std::chrono::seconds(2), ec);
    }

    EXPECT_EQ(world.PollHotReload(), 1U);
    EXPECT_EQ(world.GetStats().hotReloadCount, 1U);
    ASSERT_TRUE(world.IsAlive(handle));

    const VfxInstance* reloaded = world.TryGetInstance(handle);
    ASSERT_NE(reloaded, nullptr);
    EXPECT_EQ(reloaded->GetInstanceSeed(), 1234U);
    ASSERT_EQ(reloaded->GetParameters().size(), 1U);
    EXPECT_NEAR(reloaded->GetParameters()[0].vector.y, 0.9F, 1e-5F);
    EXPECT_NEAR(reloaded->GetParameters()[0].alpha, 0.75F, 1e-5F);

    // Corrupt reload leaves the running instance intact.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    {
        std::ofstream bad(assetPath, std::ios::binary | std::ios::trunc);
        bad << "not valid uvfx";
    }
    EXPECT_EQ(world.PollHotReload(), 0U);
    EXPECT_TRUE(world.IsAlive(handle));
}

} // namespace
} // namespace UVE::VFX::Tests
