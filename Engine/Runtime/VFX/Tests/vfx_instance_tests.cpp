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

[[nodiscard]] ParticleFloatInputUVE ConstFloat(const float value) {
    ParticleFloatInputUVE input;
    input.kind = ParticleValueKindUVE::Constant;
    input.constant = value;
    return input;
}

[[nodiscard]] ParticleSystemAssetUVE MakeTestSystemUVE(const float duration = 2.0F, const bool looping = true) {
    ParticleSystemAssetUVE sys;
    sys.duration = duration;
    sys.looping = looping;
    ParticleEmitterAssetUVE em;
    em.name = "Main";
    em.maxParticles = 5000U;
    em.render.push_back(MakeDefaultParticleModuleUVE(100U, ParticleModuleTypeUVE::SpriteRenderer));
    sys.emitters.push_back(std::move(em));
    return sys;
}

TEST(VfxInstanceUVETest, SpawnRateCarriesFractions) {
    auto sys = MakeTestSystemUVE(2.0F, true);
    auto rate = MakeDefaultParticleModuleUVE(1U, ParticleModuleTypeUVE::SpawnRate);
    rate.floats["Rate"] = ConstFloat(100.5F);
    sys.emitters[0].emitterUpdate.push_back(std::move(rate));

    auto life = MakeDefaultParticleModuleUVE(2U, ParticleModuleTypeUVE::InitialLifetime);
    life.floats["Lifetime"] = ConstFloat(20.0F);
    sys.emitters[0].particleSpawn.push_back(std::move(life));

    VfxInstanceUVE instance;
    ASSERT_TRUE(instance.InitializeUVE(sys));

    const VfxOwnerUVE owner{};
    for (int i = 0; i < 600; ++i) {
        instance.TickUVE(1.0F / 60.0F, owner);
    }

    const std::size_t live = instance.GetLiveCountUVE();
    EXPECT_GE(live, 1004U);
    EXPECT_LE(live, 1006U);
}

TEST(VfxInstanceUVETest, BurstFiresOncePerCycle) {
    auto sys = MakeTestSystemUVE(2.0F, true);
    auto burst = MakeDefaultParticleModuleUVE(1U, ParticleModuleTypeUVE::SpawnBurst);
    burst.floats["Time"] = ConstFloat(0.2F);
    burst.floats["Interval"] = ConstFloat(0.4F);
    burst.ints["Count"] = 10;
    burst.ints["Cycles"] = 3; // 0.2, 0.6, 1.0 within each 2s loop
    sys.emitters[0].emitterUpdate.push_back(std::move(burst));

    auto life = MakeDefaultParticleModuleUVE(2U, ParticleModuleTypeUVE::InitialLifetime);
    life.floats["Lifetime"] = ConstFloat(10.0F);
    sys.emitters[0].particleSpawn.push_back(std::move(life));

    VfxInstanceUVE instance;
    ASSERT_TRUE(instance.InitializeUVE(sys));

    const VfxOwnerUVE owner{};
    for (int i = 0; i < 60; ++i) {
        instance.TickUVE(1.0F / 60.0F, owner);
    }
    // 1.0s elapsed: bursts at 0.2 and 0.6 fired; 1.0 boundary fires on crossing [1.0, ...) or on 1.0
    EXPECT_GE(instance.GetLiveCountUVE(), 20U);
    EXPECT_LE(instance.GetLiveCountUVE(), 30U);

    for (int i = 0; i < 60; ++i) {
        instance.TickUVE(1.0F / 60.0F, owner);
    }
    // At t = 2.0s (full loop 0 complete): exactly 3 bursts * 10 = 30 particles!
    EXPECT_EQ(instance.GetLiveCountUVE(), 30U);

    // Run Loop 1: another 30 bursts -> 60 total.
    for (int i = 0; i < 120; ++i) {
        instance.TickUVE(1.0F / 60.0F, owner);
    }
    EXPECT_EQ(instance.GetLiveCountUVE(), 60U);
}

TEST(VfxInstanceUVETest, PerDistanceSpawnsAlongThePath) {
    auto sys = MakeTestSystemUVE(2.0F, true);
    auto perDist = MakeDefaultParticleModuleUVE(1U, ParticleModuleTypeUVE::SpawnPerDistance);
    perDist.floats["PerUnit"] = ConstFloat(2.0F);
    sys.emitters[0].emitterUpdate.push_back(std::move(perDist));
    sys.emitters[0].particleSpawn.push_back(MakeDefaultParticleModuleUVE(2U, ParticleModuleTypeUVE::ShapePoint));
    auto life = MakeDefaultParticleModuleUVE(3U, ParticleModuleTypeUVE::InitialLifetime);
    life.floats["Lifetime"] = ConstFloat(5.0F);
    sys.emitters[0].particleSpawn.push_back(std::move(life));

    VfxInstanceUVE instance;
    ASSERT_TRUE(instance.InitializeUVE(sys));

    VfxOwnerUVE owner;
    owner.position = {10.0F, 0.0F, 0.0F};
    instance.TickUVE(1.0F / 60.0F, owner);

    const auto& state = instance.GetEmitterStateUVE(0U);
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

TEST(VfxInstanceUVETest, MaxParticlesIsNeverExceeded) {
    auto sys = MakeTestSystemUVE(2.0F, true);
    sys.emitters[0].maxParticles = 45U;
    auto rate = MakeDefaultParticleModuleUVE(1U, ParticleModuleTypeUVE::SpawnRate);
    rate.floats["Rate"] = ConstFloat(5000.0F);
    sys.emitters[0].emitterUpdate.push_back(std::move(rate));
    auto burst = MakeDefaultParticleModuleUVE(2U, ParticleModuleTypeUVE::SpawnBurst);
    burst.ints["Count"] = 200;
    sys.emitters[0].emitterUpdate.push_back(std::move(burst));

    VfxInstanceUVE instance;
    ASSERT_TRUE(instance.InitializeUVE(sys));
    const VfxOwnerUVE owner{};
    for (int i = 0; i < 60; ++i) {
        instance.TickUVE(0.05F, owner);
        EXPECT_LE(instance.GetLiveCountUVE(), 45U);
    }
}

TEST(VfxInstanceUVETest, SameSeedSameState) {
    const ParticleSystemAssetUVE sparks = CreateParticleTemplateUVE(ParticleTemplateKindUVE::Sparks);
    VfxInstanceUVE a;
    VfxInstanceUVE b;
    ASSERT_TRUE(a.InitializeUVE(sparks, VfxOwnerUVE{}, 777U));
    ASSERT_TRUE(b.InitializeUVE(sparks, VfxOwnerUVE{}, 777U));

    VfxOwnerUVE owner;
    for (int step = 0; step < 1000; ++step) {
        const float t = static_cast<float>(step) * 0.01F;
        owner.position = {std::sin(t), 0.0F, std::cos(t)};
        a.TickUVE(0.01F, owner);
        b.TickUVE(0.01F, owner);
    }

    const auto& sa = a.GetEmitterStateUVE(0U);
    const auto& sb = b.GetEmitterStateUVE(0U);
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

TEST(VfxInstanceUVETest, DifferentSeedDifferentState) {
    const ParticleSystemAssetUVE sparks = CreateParticleTemplateUVE(ParticleTemplateKindUVE::Sparks);
    VfxInstanceUVE a;
    VfxInstanceUVE b;
    ASSERT_TRUE(a.InitializeUVE(sparks, VfxOwnerUVE{}, 101U));
    ASSERT_TRUE(b.InitializeUVE(sparks, VfxOwnerUVE{}, 202U));

    const VfxOwnerUVE owner{};
    for (int step = 0; step < 20; ++step) {
        a.TickUVE(1.0F / 60.0F, owner);
        b.TickUVE(1.0F / 60.0F, owner);
    }

    const auto& sa = a.GetEmitterStateUVE(0U);
    const auto& sb = b.GetEmitterStateUVE(0U);
    ASSERT_GT(sa.liveCount, 0U);
    EXPECT_NE(sa.posX, sb.posX);
    EXPECT_NE(sa.seed, sb.seed);
}

TEST(VfxInstanceUVETest, KillByAgeRemovesExactlyTheExpired) {
    auto sys = MakeTestSystemUVE(4.0F, false);
    auto burst = MakeDefaultParticleModuleUVE(1U, ParticleModuleTypeUVE::SpawnBurst);
    burst.floats["Time"] = ConstFloat(0.0F);
    burst.ints["Count"] = 100;
    sys.emitters[0].emitterUpdate.push_back(std::move(burst));

    auto life = MakeDefaultParticleModuleUVE(2U, ParticleModuleTypeUVE::InitialLifetime);
    life.floats["Lifetime"].kind = ParticleValueKindUVE::RandomRange;
    life.floats["Lifetime"].min = 0.2F;
    life.floats["Lifetime"].max = 1.2F;
    sys.emitters[0].particleSpawn.push_back(std::move(life));

    VfxInstanceUVE instance;
    ASSERT_TRUE(instance.InitializeUVE(sys, VfxOwnerUVE{}, 55U));
    const VfxOwnerUVE owner{};

    // First frame spawns all 100 particles (lifetimes in [0.2, 1.2), age = 0.025).
    instance.TickUVE(0.025F, owner);
    const auto& initial = instance.GetEmitterStateUVE(0U);
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
        instance.TickUVE(0.025F, owner);
    }

    const auto& after = instance.GetEmitterStateUVE(0U);
    EXPECT_EQ(after.liveCount, expectedSurvivors);
    for (std::size_t i = 0U; i < after.liveCount; ++i) {
        EXPECT_LT(after.age[i], after.lifetime[i]);
    }
}

TEST(VfxInstanceUVETest, WorldVsLocalSpace) {
    ParticleSystemAssetUVE sys;
    sys.duration = 2.0F;

    auto buildEmitter = [](const std::string& name, const ParticleSimSpaceUVE space) {
        ParticleEmitterAssetUVE em;
        em.name = name;
        em.space = space;
        em.maxParticles = 100U;
        auto burst = MakeDefaultParticleModuleUVE(1U, ParticleModuleTypeUVE::SpawnBurst);
        burst.ints["Count"] = 1;
        em.emitterUpdate.push_back(std::move(burst));
        em.particleSpawn.push_back(MakeDefaultParticleModuleUVE(2U, ParticleModuleTypeUVE::ShapePoint));
        auto life = MakeDefaultParticleModuleUVE(3U, ParticleModuleTypeUVE::InitialLifetime);
        life.floats["Lifetime"] = ConstFloat(5.0F);
        em.particleSpawn.push_back(std::move(life));
        em.render.push_back(MakeDefaultParticleModuleUVE(4U, ParticleModuleTypeUVE::SpriteRenderer));
        return em;
    };
    sys.emitters.push_back(buildEmitter("WorldEm", ParticleSimSpaceUVE::World));
    sys.emitters.push_back(buildEmitter("LocalEm", ParticleSimSpaceUVE::Local));

    VfxInstanceUVE instance;
    VfxOwnerUVE owner;
    owner.position = {5.0F, 2.0F, 0.0F};
    ASSERT_TRUE(instance.InitializeUVE(sys, owner));
    instance.TickUVE(1.0F / 60.0F, owner);

    owner.position = {25.0F, 2.0F, 0.0F};
    instance.TickUVE(1.0F / 60.0F, owner);

    const auto& worldState = instance.GetEmitterStateUVE(0U);
    const auto& localState = instance.GetEmitterStateUVE(1U);
    ASSERT_EQ(worldState.liveCount, 1U);
    ASSERT_EQ(localState.liveCount, 1U);
    EXPECT_NEAR(worldState.posX[0], 5.0F, 1e-4F);
    EXPECT_NEAR(worldState.posY[0], 2.0F, 1e-4F);
    EXPECT_NEAR(localState.posX[0], 0.0F, 1e-4F);
    EXPECT_NEAR(localState.posY[0], 0.0F, 1e-4F);
}

TEST(VfxInstanceUVETest, NonLoopingCompletes) {
    auto sys = MakeTestSystemUVE(0.5F, false);
    auto rate = MakeDefaultParticleModuleUVE(1U, ParticleModuleTypeUVE::SpawnRate);
    rate.floats["Rate"] = ConstFloat(20.0F);
    sys.emitters[0].emitterUpdate.push_back(std::move(rate));
    auto life = MakeDefaultParticleModuleUVE(2U, ParticleModuleTypeUVE::InitialLifetime);
    life.floats["Lifetime"] = ConstFloat(0.25F);
    sys.emitters[0].particleSpawn.push_back(std::move(life));

    VfxInstanceUVE instance;
    ASSERT_TRUE(instance.InitializeUVE(sys));
    const VfxOwnerUVE owner{};

    EXPECT_TRUE(instance.IsAliveUVE());
    for (int i = 0; i < 10; ++i) {
        instance.TickUVE(0.025F, owner); // 0.25s
    }
    EXPECT_TRUE(instance.IsAliveUVE());
    EXPECT_GT(instance.GetLiveCountUVE(), 0U);

    // Run past duration + lifetime.
    for (int i = 0; i < 30; ++i) {
        instance.TickUVE(0.025F, owner);
    }
    EXPECT_EQ(instance.GetLiveCountUVE(), 0U);
    EXPECT_FALSE(instance.IsAliveUVE());
}

TEST(VfxInstanceUVETest, WarmUpAdvancesBeforeTheFirstFrame) {
    auto sys = CreateParticleTemplateUVE(ParticleTemplateKindUVE::Fountain);
    sys.warmUpSeconds = 1.0F;

    VfxInstanceUVE instance;
    ASSERT_TRUE(instance.InitializeUVE(sys));
    EXPECT_GT(instance.GetLiveCountUVE(), 50U);
    EXPECT_TRUE(instance.IsAliveUVE());
}

TEST(VfxInstanceUVETest, EachTemplateCompilesAndRunsTenSeconds) {
    const VfxOwnerUVE owner{};
    for (const ParticleTemplateInfoUVE& info : GetParticleTemplatesUVE()) {
        const ParticleSystemAssetUVE asset = CreateParticleTemplateUVE(info.kind);
        EXPECT_TRUE(IsParticleSystemAssetValidUVE(asset)) << info.name;

        VfxInstanceUVE instance;
        std::vector<ParticleCompileMessageUVE> messages;
        ASSERT_TRUE(instance.InitializeUVE(asset, owner, 1U, VfxCompileContextUVE{}, &messages)) << info.name;
        EXPECT_TRUE(messages.empty()) << info.name;

        if (info.kind == ParticleTemplateKindUVE::Fountain) {
            std::cout << "[V2a Fountain 10s live count log]\n";
        }
        for (int second = 1; second <= 10; ++second) {
            for (int frame = 0; frame < 60; ++frame) {
                instance.TickUVE(1.0F / 60.0F, owner);
            }
            EXPECT_GT(instance.GetLiveCountUVE(), 0U) << info.name << " at s=" << second;
            if (info.kind == ParticleTemplateKindUVE::Fountain) {
                std::cout << "  t=" << second << "s live=" << instance.GetLiveCountUVE() << "\n";
            }
        }
        EXPECT_TRUE(instance.IsAliveUVE()) << info.name;
    }
}

} // namespace
} // namespace UVE::VFX::Tests
