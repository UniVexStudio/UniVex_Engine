// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "vfx_instance.h"
#include "vfx_templates.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::VFX::Tests {
namespace {

using namespace Asset;

[[nodiscard]] ParticleFloatInput ConstFloat(const float value) {
    ParticleFloatInput input;
    input.kind = ParticleValueKind::Constant;
    input.constant = value;
    return input;
}

[[nodiscard]] ParticleSystemAsset MakeTestSystem(const float duration = 2.0F, const bool looping = true) {
    ParticleSystemAsset sys;
    sys.duration = duration;
    sys.looping = looping;
    ParticleEmitterAsset em;
    em.name = "Main";
    em.maxParticles = 5000U;
    em.render.push_back(MakeDefaultParticleModule(100U, ParticleModuleType::SpriteRenderer));
    sys.emitters.push_back(std::move(em));
    return sys;
}

TEST(VfxInstanceTest, SpawnRateCarriesFractions) {
    auto sys = MakeTestSystem(2.0F, true);
    auto rate = MakeDefaultParticleModule(1U, ParticleModuleType::SpawnRate);
    rate.floats["Rate"] = ConstFloat(100.5F);
    sys.emitters[0].emitterUpdate.push_back(std::move(rate));

    auto life = MakeDefaultParticleModule(2U, ParticleModuleType::InitialLifetime);
    life.floats["Lifetime"] = ConstFloat(20.0F);
    sys.emitters[0].particleSpawn.push_back(std::move(life));

    VfxInstance instance;
    ASSERT_TRUE(instance.Initialize(sys));

    const VfxOwner owner{};
    for (int i = 0; i < 600; ++i) {
        instance.Tick(1.0F / 60.0F, owner);
    }

    const std::size_t live = instance.GetLiveCount();
    EXPECT_GE(live, 1004U);
    EXPECT_LE(live, 1006U);
}

TEST(VfxInstanceTest, BurstFiresOncePerCycle) {
    auto sys = MakeTestSystem(2.0F, true);
    auto burst = MakeDefaultParticleModule(1U, ParticleModuleType::SpawnBurst);
    burst.floats["Time"] = ConstFloat(0.2F);
    burst.floats["Interval"] = ConstFloat(0.4F);
    burst.ints["Count"] = 10;
    burst.ints["Cycles"] = 3; // 0.2, 0.6, 1.0 within each 2s loop
    sys.emitters[0].emitterUpdate.push_back(std::move(burst));

    auto life = MakeDefaultParticleModule(2U, ParticleModuleType::InitialLifetime);
    life.floats["Lifetime"] = ConstFloat(10.0F);
    sys.emitters[0].particleSpawn.push_back(std::move(life));

    VfxInstance instance;
    ASSERT_TRUE(instance.Initialize(sys));

    const VfxOwner owner{};
    for (int i = 0; i < 60; ++i) {
        instance.Tick(1.0F / 60.0F, owner);
    }
    // 1.0s elapsed: bursts at 0.2 and 0.6 fired; 1.0 boundary fires on crossing [1.0, ...) or on 1.0
    EXPECT_GE(instance.GetLiveCount(), 20U);
    EXPECT_LE(instance.GetLiveCount(), 30U);

    for (int i = 0; i < 60; ++i) {
        instance.Tick(1.0F / 60.0F, owner);
    }
    // At t = 2.0s (full loop 0 complete): exactly 3 bursts * 10 = 30 particles!
    EXPECT_EQ(instance.GetLiveCount(), 30U);

    // Run Loop 1: another 30 bursts -> 60 total.
    for (int i = 0; i < 120; ++i) {
        instance.Tick(1.0F / 60.0F, owner);
    }
    EXPECT_EQ(instance.GetLiveCount(), 60U);
}

TEST(VfxInstanceTest, PerDistanceSpawnsAlongThePath) {
    auto sys = MakeTestSystem(2.0F, true);
    auto perDist = MakeDefaultParticleModule(1U, ParticleModuleType::SpawnPerDistance);
    perDist.floats["PerUnit"] = ConstFloat(2.0F);
    sys.emitters[0].emitterUpdate.push_back(std::move(perDist));
    sys.emitters[0].particleSpawn.push_back(MakeDefaultParticleModule(2U, ParticleModuleType::ShapePoint));
    auto life = MakeDefaultParticleModule(3U, ParticleModuleType::InitialLifetime);
    life.floats["Lifetime"] = ConstFloat(5.0F);
    sys.emitters[0].particleSpawn.push_back(std::move(life));

    VfxInstance instance;
    ASSERT_TRUE(instance.Initialize(sys));

    VfxOwner owner;
    owner.position = {10.0F, 0.0F, 0.0F};
    instance.Tick(1.0F / 60.0F, owner);

    const auto& state = instance.GetEmitterState(0U);
    ASSERT_EQ(state.liveCount, 20U);
    float minX = state.posX[0];
    float maxX = state.posX[0];
    for (std::size_t i = 0U; i < state.liveCount; ++i) {
        EXPECT_NEAR(state.posY[i], 0.0F, 1e-5F);
        EXPECT_NEAR(state.posZ[i], 0.0F, 1e-5F);
        EXPECT_GE(state.posX[i], 0.0F);
        EXPECT_LE(state.posX[i], 10.0F);
        minX = std::min(minX, state.posX[i]);
        maxX = std::max(maxX, state.posX[i]);
    }
    EXPECT_LT(minX, 1.0F);
    EXPECT_GT(maxX, 9.0F);
}

TEST(VfxInstanceTest, MaxParticlesIsNeverExceeded) {
    auto sys = MakeTestSystem(2.0F, true);
    sys.emitters[0].maxParticles = 45U;
    auto rate = MakeDefaultParticleModule(1U, ParticleModuleType::SpawnRate);
    rate.floats["Rate"] = ConstFloat(5000.0F);
    sys.emitters[0].emitterUpdate.push_back(std::move(rate));
    auto burst = MakeDefaultParticleModule(2U, ParticleModuleType::SpawnBurst);
    burst.ints["Count"] = 200;
    sys.emitters[0].emitterUpdate.push_back(std::move(burst));

    VfxInstance instance;
    ASSERT_TRUE(instance.Initialize(sys));
    const VfxOwner owner{};
    for (int i = 0; i < 60; ++i) {
        instance.Tick(0.05F, owner);
        EXPECT_LE(instance.GetLiveCount(), 45U);
    }
}

TEST(VfxInstanceTest, SameSeedSameState) {
    const ParticleSystemAsset sparks = CreateParticleTemplate(ParticleTemplateKind::Sparks);
    VfxInstance a;
    VfxInstance b;
    ASSERT_TRUE(a.Initialize(sparks, VfxOwner{}, 777U));
    ASSERT_TRUE(b.Initialize(sparks, VfxOwner{}, 777U));

    VfxOwner owner;
    for (int step = 0; step < 1000; ++step) {
        const float t = static_cast<float>(step) * 0.01F;
        owner.position = {std::sin(t), 0.0F, std::cos(t)};
        a.Tick(0.01F, owner);
        b.Tick(0.01F, owner);
    }

    const auto& sa = a.GetEmitterState(0U);
    const auto& sb = b.GetEmitterState(0U);
    ASSERT_GT(sa.liveCount, 0U);
    EXPECT_EQ(sa.liveCount, sb.liveCount);
    EXPECT_EQ(sa.id, sb.id);
    EXPECT_EQ(sa.seed, sb.seed);
    EXPECT_EQ(sa.posX, sb.posX);
    EXPECT_EQ(sa.posY, sb.posY);
    EXPECT_EQ(sa.posZ, sb.posZ);
    EXPECT_EQ(sa.velX, sb.velX);
    EXPECT_EQ(sa.velY, sb.velY);
    EXPECT_EQ(sa.velZ, sb.velZ);
    EXPECT_EQ(sa.age, sb.age);
    EXPECT_EQ(sa.lifetime, sb.lifetime);
}

TEST(VfxInstanceTest, DifferentSeedDifferentState) {
    const ParticleSystemAsset sparks = CreateParticleTemplate(ParticleTemplateKind::Sparks);
    VfxInstance a;
    VfxInstance b;
    ASSERT_TRUE(a.Initialize(sparks, VfxOwner{}, 101U));
    ASSERT_TRUE(b.Initialize(sparks, VfxOwner{}, 202U));

    const VfxOwner owner{};
    for (int step = 0; step < 20; ++step) {
        a.Tick(1.0F / 60.0F, owner);
        b.Tick(1.0F / 60.0F, owner);
    }

    const auto& sa = a.GetEmitterState(0U);
    const auto& sb = b.GetEmitterState(0U);
    ASSERT_GT(sa.liveCount, 0U);
    EXPECT_NE(sa.posX, sb.posX);
    EXPECT_NE(sa.seed, sb.seed);
}

TEST(VfxInstanceTest, KillByAgeRemovesExactlyTheExpired) {
    auto sys = MakeTestSystem(4.0F, false);
    auto burst = MakeDefaultParticleModule(1U, ParticleModuleType::SpawnBurst);
    burst.floats["Time"] = ConstFloat(0.0F);
    burst.ints["Count"] = 100;
    sys.emitters[0].emitterUpdate.push_back(std::move(burst));

    auto life = MakeDefaultParticleModule(2U, ParticleModuleType::InitialLifetime);
    life.floats["Lifetime"].kind = ParticleValueKind::RandomRange;
    life.floats["Lifetime"].min = 0.2F;
    life.floats["Lifetime"].max = 1.2F;
    sys.emitters[0].particleSpawn.push_back(std::move(life));

    VfxInstance instance;
    ASSERT_TRUE(instance.Initialize(sys, VfxOwner{}, 55U));
    const VfxOwner owner{};

    // First frame spawns all 100 particles (lifetimes in [0.2, 1.2), age = 0.025).
    instance.Tick(0.025F, owner);
    const auto& initial = instance.GetEmitterState(0U);
    ASSERT_EQ(initial.liveCount, 100U);

    // Count how many have lifetime > 0.6s (strictly surviving at t = 0.6s).
    std::size_t expectedSurvivors = 0U;
    for (const float lt : initial.lifetime) {
        if (0.6F < lt) {
            ++expectedSurvivors;
        }
    }
    ASSERT_GT(expectedSurvivors, 0U);
    ASSERT_LT(expectedSurvivors, 100U);

    // Advance remainder up to 0.6s total.
    for (int step = 0; step < 23; ++step) {
        instance.Tick(0.025F, owner);
    }

    const auto& after = instance.GetEmitterState(0U);
    EXPECT_EQ(after.liveCount, expectedSurvivors);
    for (std::size_t i = 0U; i < after.liveCount; ++i) {
        EXPECT_LT(after.age[i], after.lifetime[i]);
    }
}

TEST(VfxInstanceTest, WorldVsLocalSpace) {
    ParticleSystemAsset sys;
    sys.duration = 2.0F;

    auto buildEmitter = [](const std::string& name, const ParticleSimSpace space) {
        ParticleEmitterAsset em;
        em.name = name;
        em.space = space;
        em.maxParticles = 100U;
        auto burst = MakeDefaultParticleModule(1U, ParticleModuleType::SpawnBurst);
        burst.ints["Count"] = 1;
        em.emitterUpdate.push_back(std::move(burst));
        em.particleSpawn.push_back(MakeDefaultParticleModule(2U, ParticleModuleType::ShapePoint));
        auto life = MakeDefaultParticleModule(3U, ParticleModuleType::InitialLifetime);
        life.floats["Lifetime"] = ConstFloat(5.0F);
        em.particleSpawn.push_back(std::move(life));
        em.render.push_back(MakeDefaultParticleModule(4U, ParticleModuleType::SpriteRenderer));
        return em;
    };
    sys.emitters.push_back(buildEmitter("WorldEm", ParticleSimSpace::World));
    sys.emitters.push_back(buildEmitter("LocalEm", ParticleSimSpace::Local));

    VfxInstance instance;
    VfxOwner owner;
    owner.position = {5.0F, 2.0F, 0.0F};
    ASSERT_TRUE(instance.Initialize(sys, owner));
    instance.Tick(1.0F / 60.0F, owner);

    owner.position = {25.0F, 2.0F, 0.0F};
    instance.Tick(1.0F / 60.0F, owner);

    const auto& worldState = instance.GetEmitterState(0U);
    const auto& localState = instance.GetEmitterState(1U);
    ASSERT_EQ(worldState.liveCount, 1U);
    ASSERT_EQ(localState.liveCount, 1U);
    EXPECT_NEAR(worldState.posX[0], 5.0F, 1e-4F);
    EXPECT_NEAR(worldState.posY[0], 2.0F, 1e-4F);
    EXPECT_NEAR(localState.posX[0], 0.0F, 1e-4F);
    EXPECT_NEAR(localState.posY[0], 0.0F, 1e-4F);
}

TEST(VfxInstanceTest, NonLoopingCompletes) {
    auto sys = MakeTestSystem(0.5F, false);
    auto rate = MakeDefaultParticleModule(1U, ParticleModuleType::SpawnRate);
    rate.floats["Rate"] = ConstFloat(20.0F);
    sys.emitters[0].emitterUpdate.push_back(std::move(rate));
    auto life = MakeDefaultParticleModule(2U, ParticleModuleType::InitialLifetime);
    life.floats["Lifetime"] = ConstFloat(0.25F);
    sys.emitters[0].particleSpawn.push_back(std::move(life));

    VfxInstance instance;
    ASSERT_TRUE(instance.Initialize(sys));
    const VfxOwner owner{};

    EXPECT_TRUE(instance.IsAlive());
    for (int i = 0; i < 10; ++i) {
        instance.Tick(0.025F, owner); // 0.25s
    }
    EXPECT_TRUE(instance.IsAlive());
    EXPECT_GT(instance.GetLiveCount(), 0U);

    // Run past duration + lifetime.
    for (int i = 0; i < 30; ++i) {
        instance.Tick(0.025F, owner);
    }
    EXPECT_EQ(instance.GetLiveCount(), 0U);
    EXPECT_FALSE(instance.IsAlive());
}

TEST(VfxInstanceTest, WarmUpAdvancesBeforeTheFirstFrame) {
    auto sys = CreateParticleTemplate(ParticleTemplateKind::Fountain);
    sys.warmUpSeconds = 1.0F;

    VfxInstance instance;
    ASSERT_TRUE(instance.Initialize(sys));
    EXPECT_GT(instance.GetLiveCount(), 50U);
    EXPECT_TRUE(instance.IsAlive());
}

TEST(VfxInstanceTest, EachTemplateCompilesAndRunsTenSeconds) {
    const VfxOwner owner{};
    for (const ParticleTemplateInfo& info : GetParticleTemplates()) {
        const ParticleSystemAsset asset = CreateParticleTemplate(info.kind);
        EXPECT_TRUE(IsParticleSystemAssetValid(asset)) << info.name;

        VfxInstance instance;
        std::vector<ParticleCompileMessage> messages;
        ASSERT_TRUE(instance.Initialize(asset, owner, 1U, VfxCompileContext{}, &messages)) << info.name;
        EXPECT_TRUE(messages.empty()) << info.name;

        if (info.kind == ParticleTemplateKind::Fountain) {
            std::cout << "[V2a Fountain 10s live count log]\n";
        }
        for (int second = 1; second <= 10; ++second) {
            for (int frame = 0; frame < 60; ++frame) {
                instance.Tick(1.0F / 60.0F, owner);
            }
            EXPECT_GT(instance.GetLiveCount(), 0U) << info.name << " at s=" << second;
            if (info.kind == ParticleTemplateKind::Fountain) {
                std::cout << "  t=" << second << "s live=" << instance.GetLiveCount() << "\n";
            }
        }
        EXPECT_TRUE(instance.IsAlive()) << info.name;
    }
}

} // namespace
} // namespace UVE::VFX::Tests
