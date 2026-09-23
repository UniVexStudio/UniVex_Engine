// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/particle_runtime_uve.h"

#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include "uve/threading/thread_pool_uve.h"

namespace UVE::Scene::Tests {

TEST(ParticleRuntimeUVETest, AttachUVE_RejectsInvalidDuplicateAndPreservesBudgetAtomicity) {
    ParticleRuntimeUVE runtime;
    EXPECT_EQ(runtime.AttachDetailedUVE(kInvalidEntityUVE, ParticleEmitterComponentUVE{10U}).code,
              ParticleRuntimeCodeUVE::InvalidEntity);
    EXPECT_EQ(runtime.AttachDetailedUVE({1U, 1U}, ParticleEmitterComponentUVE{0U}).code,
              ParticleRuntimeCodeUVE::InvalidComponent);

    ASSERT_TRUE(runtime.AttachUVE({1U, 1U}, ParticleEmitterComponentUVE{100U}));
    EXPECT_EQ(runtime.AttachDetailedUVE({1U, 1U}, ParticleEmitterComponentUVE{100U}).code,
              ParticleRuntimeCodeUVE::DuplicateInstance);
    EXPECT_EQ(runtime.GetInstanceCountUVE(), 1U);
    EXPECT_EQ(runtime.GetTotalBudgetUVE(), 100U);
}

TEST(ParticleRuntimeUVETest, AttachUVE_EnforcesAggregateBudgetAndReleasesItOnDetach) {
    ParticleRuntimeUVE runtime;
    const ParticleEmitterComponentUVE maximum{kMaximumParticleEmitterParticlesUVE};
    ASSERT_TRUE(runtime.AttachUVE({2U, 1U}, maximum));
    ASSERT_TRUE(runtime.AttachUVE({3U, 1U}, maximum));
    ASSERT_TRUE(runtime.AttachUVE({4U, 1U}, maximum));
    ASSERT_TRUE(runtime.AttachUVE({5U, 1U}, maximum));
    EXPECT_EQ(runtime.GetTotalBudgetUVE(), ParticleRuntimeUVE::kMaximumTotalParticleBudgetUVE);
    EXPECT_EQ(runtime.AttachDetailedUVE({6U, 1U}, ParticleEmitterComponentUVE{1U}).code,
              ParticleRuntimeCodeUVE::CapacityExceeded);
    EXPECT_EQ(runtime.GetInstanceCountUVE(), 4U);

    ASSERT_TRUE(runtime.DetachUVE({3U, 1U}));
    EXPECT_EQ(runtime.GetTotalBudgetUVE(), 3'000'000U);
    ASSERT_TRUE(runtime.AttachUVE({6U, 1U}, ParticleEmitterComponentUVE{1'000'000U}));
    EXPECT_EQ(runtime.GetTotalBudgetUVE(), ParticleRuntimeUVE::kMaximumTotalParticleBudgetUVE);
}

TEST(ParticleRuntimeUVETest, StateUVE_EnforcesLiveCountAndEnabledLifecycle) {
    ParticleRuntimeUVE runtime;
    const EntityUVE entity{7U, 1U};
    ASSERT_TRUE(runtime.AttachUVE(entity, ParticleEmitterComponentUVE{128U}));

    EXPECT_EQ(runtime.SetLiveParticleCountDetailedUVE(entity, 129U).code,
              ParticleRuntimeCodeUVE::LiveParticleCountExceeded);
    EXPECT_EQ(runtime.SetLiveParticleCountDetailedUVE(entity, 64U).code,
              ParticleRuntimeCodeUVE::Applied);
    EXPECT_EQ(runtime.SetLiveParticleCountDetailedUVE(entity, 64U).code,
              ParticleRuntimeCodeUVE::Unchanged);
    EXPECT_EQ(runtime.SetEnabledDetailedUVE(entity, false).code, ParticleRuntimeCodeUVE::Applied);
    EXPECT_EQ(runtime.SetEnabledDetailedUVE(entity, false).code, ParticleRuntimeCodeUVE::Unchanged);

    const ParticleRuntimeSnapshotUVE snapshot = runtime.GetSnapshotUVE();
    ASSERT_EQ(snapshot.instances.size(), 1U);
    EXPECT_EQ(snapshot.instances.front().entity, entity);
    EXPECT_EQ(snapshot.instances.front().maxParticles, 128U);
    EXPECT_EQ(snapshot.instances.front().liveParticles, 64U);
    EXPECT_FALSE(snapshot.instances.front().enabled);
}

TEST(ParticleRuntimeUVETest, GetSnapshotUVE_IsDeterministicByGenerationalEntityOrder) {
    ParticleRuntimeUVE runtime;
    ASSERT_TRUE(runtime.AttachUVE({11U, 2U}, ParticleEmitterComponentUVE{20U}));
    ASSERT_TRUE(runtime.AttachUVE({10U, 3U}, ParticleEmitterComponentUVE{30U}));
    ASSERT_TRUE(runtime.AttachUVE({10U, 1U}, ParticleEmitterComponentUVE{40U}));

    const ParticleRuntimeSnapshotUVE snapshot = runtime.GetSnapshotUVE();
    ASSERT_EQ(snapshot.instances.size(), 3U);
    EXPECT_EQ(snapshot.instances[0].entity, (EntityUVE{10U, 1U}));
    EXPECT_EQ(snapshot.instances[1].entity, (EntityUVE{10U, 3U}));
    EXPECT_EQ(snapshot.instances[2].entity, (EntityUVE{11U, 2U}));
    EXPECT_EQ(snapshot.totalBudget, 90U);
}

TEST(ParticleRuntimeUVETest, EmitUVE_AppendsStableParticleStateWithinBudget) {
    ParticleRuntimeUVE runtime;
    const EntityUVE entity{13U, 1U};
    ASSERT_TRUE(runtime.AttachUVE(entity, ParticleEmitterComponentUVE{2U}));
    const ParticleEmissionUVE emission{2U, Math::Vector3UVE{1.0F, 2.0F, 3.0F},
                                       Math::Vector3UVE{4.0F, 5.0F, 6.0F}, 5.0F};

    ASSERT_TRUE(runtime.EmitDetailedUVE(entity, emission).IsAcceptedUVE());
    const std::optional<ParticleStateSnapshotUVE> snapshot = runtime.GetParticleSnapshotUVE(entity);
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->particles.size(), 2U);
    EXPECT_EQ(snapshot->particles[0].sequence, 1U);
    EXPECT_EQ(snapshot->particles[1].sequence, 2U);
    EXPECT_EQ(snapshot->particles[0].position, emission.position);
    EXPECT_EQ(snapshot->particles[0].velocity, emission.velocity);
    EXPECT_EQ(snapshot->particles[0].remainingLifetimeSeconds, 5.0F);
    EXPECT_EQ(runtime.GetSnapshotUVE().instances.front().liveParticles, 2U);

    const ParticleRuntimeSnapshotUVE beforeRejectedEmission = runtime.GetSnapshotUVE();
    EXPECT_EQ(runtime.EmitDetailedUVE(entity, emission).code, ParticleRuntimeCodeUVE::LiveParticleCountExceeded);
    EXPECT_EQ(runtime.GetSnapshotUVE(), beforeRejectedEmission);
}

TEST(ParticleRuntimeUVETest, EmitUVE_RejectsDisabledAndInvalidInputWithoutMutation) {
    ParticleRuntimeUVE runtime;
    const EntityUVE entity{14U, 1U};
    ASSERT_TRUE(runtime.AttachUVE(entity, ParticleEmitterComponentUVE{8U}));
    ASSERT_EQ(runtime.SetEnabledDetailedUVE(entity, false).code, ParticleRuntimeCodeUVE::Applied);
    EXPECT_EQ(runtime.EmitDetailedUVE(entity, ParticleEmissionUVE{1U, {}, {}, 1.0F}).code,
              ParticleRuntimeCodeUVE::DisabledInstance);
    ASSERT_EQ(runtime.SetEnabledDetailedUVE(entity, true).code, ParticleRuntimeCodeUVE::Applied);

    const ParticleRuntimeSnapshotUVE beforeInvalid = runtime.GetSnapshotUVE();
    ParticleEmissionUVE invalid{1U, {}, {}, 1.0F};
    invalid.velocity.x = std::numeric_limits<float>::infinity();
    EXPECT_EQ(runtime.EmitDetailedUVE(entity, invalid).code, ParticleRuntimeCodeUVE::InvalidSimulationInput);
    EXPECT_EQ(runtime.GetSnapshotUVE(), beforeInvalid);
}

TEST(ParticleRuntimeUVETest, SimulateUVE_UsesDeterministicSemiImplicitIntegrationAndLifetimeCulling) {
    ParticleRuntimeUVE runtime;
    const EntityUVE entity{15U, 1U};
    ASSERT_TRUE(runtime.AttachUVE(entity, ParticleEmitterComponentUVE{4U}));
    ASSERT_TRUE(runtime.EmitDetailedUVE(
                       entity, ParticleEmissionUVE{1U, {}, Math::Vector3UVE{1.0F, 0.0F, 0.0F}, 1.0F})
                    .IsAcceptedUVE());

    ASSERT_TRUE(runtime.SimulateDetailedUVE(0.25F, Math::Vector3UVE{0.0F, -2.0F, 0.0F}).IsAcceptedUVE());
    auto midpoint = runtime.GetParticleSnapshotUVE(entity);
    ASSERT_TRUE(midpoint.has_value());
    ASSERT_EQ(midpoint->particles.size(), 1U);
    EXPECT_EQ(midpoint->particles.front().position, (Math::Vector3UVE{0.25F, -0.125F, 0.0F}));
    EXPECT_EQ(midpoint->particles.front().velocity, (Math::Vector3UVE{1.0F, -0.5F, 0.0F}));
    EXPECT_EQ(midpoint->particles.front().remainingLifetimeSeconds, 0.75F);

    ASSERT_TRUE(runtime.SimulateDetailedUVE(0.25F, Math::Vector3UVE{0.0F, -2.0F, 0.0F}).IsAcceptedUVE());
    auto secondStep = runtime.GetParticleSnapshotUVE(entity);
    ASSERT_TRUE(secondStep.has_value());
    ASSERT_EQ(secondStep->particles.size(), 1U);
    EXPECT_EQ(secondStep->particles.front().position, (Math::Vector3UVE{0.5F, -0.375F, 0.0F}));
    EXPECT_EQ(secondStep->particles.front().velocity, (Math::Vector3UVE{1.0F, -1.0F, 0.0F}));
    EXPECT_EQ(secondStep->particles.front().remainingLifetimeSeconds, 0.5F);

    ASSERT_TRUE(runtime.SimulateDetailedUVE(0.25F, Math::Vector3UVE{0.0F, -2.0F, 0.0F}).IsAcceptedUVE());
    ASSERT_TRUE(runtime.SimulateDetailedUVE(0.25F, Math::Vector3UVE{0.0F, -2.0F, 0.0F}).IsAcceptedUVE());
    EXPECT_TRUE(runtime.GetParticleSnapshotUVE(entity)->particles.empty());
    EXPECT_EQ(runtime.GetSnapshotUVE().instances.front().liveParticles, 0U);
}

// Thread Group moves worker-eligible emitters onto a thread pool. These tests hold the parallel
// path to the serial one: the same particles, bit for bit, and the same all-or-nothing failure.

/// Builds a runtime with `emitterCount` emitters, each seeded with distinct particles so a mix-up
/// between emitters would show, and every other emitter marked worker-eligible.
[[nodiscard]] std::vector<EntityUVE> SeedParallelRuntimeUVE(ParticleRuntimeUVE& runtime, const std::uint32_t emitterCount,
                                                            const bool markEligible) {
    std::vector<EntityUVE> entities;
    for (std::uint32_t index = 0; index < emitterCount; ++index) {
        const EntityUVE entity{100U + index, 1U};
        EXPECT_TRUE(runtime.AttachUVE(entity, ParticleEmitterComponentUVE{64U}));
        const float seed = static_cast<float>(index) * 0.37F;
        EXPECT_TRUE(runtime.EmitDetailedUVE(entity, ParticleEmissionUVE{
                                                        48U,
                                                        Math::Vector3UVE{seed, seed * 2.0F, -seed},
                                                        Math::Vector3UVE{1.5F + seed, 0.25F, -0.75F * seed},
                                                        1.0F + seed})
                        .IsAcceptedUVE());
        if (markEligible && index % 2U == 0U) {
            EXPECT_TRUE(runtime.SetWorkerEligibleDetailedUVE(entity, true).IsAcceptedUVE());
        }
        entities.push_back(entity);
    }
    return entities;
}

TEST(ParticleRuntimeParallelUVETest, SimulateDetailedUVE_WithWorkersIsBitIdenticalToSerial) {
    ParticleRuntimeUVE serial;
    ParticleRuntimeUVE parallel;
    const std::vector<EntityUVE> entities = SeedParallelRuntimeUVE(serial, 24U, /*markEligible=*/false);
    static_cast<void>(SeedParallelRuntimeUVE(parallel, 24U, /*markEligible=*/true));
    Threading::ThreadPoolUVE pool{4U};

    // Many steps, so an order- or race-dependent difference has room to compound and show. Enough
    // steps also carry particles past their lifetime, exercising the compaction path on workers.
    const Math::Vector3UVE gravity{0.0F, -9.81F, 0.0F};
    for (int step = 0; step < 90; ++step) {
        const ParticleRuntimeResultUVE serialResult = serial.SimulateDetailedUVE(1.0F / 60.0F, gravity);
        const ParticleRuntimeResultUVE parallelResult = parallel.SimulateDetailedUVE(1.0F / 60.0F, gravity, &pool);
        ASSERT_EQ(serialResult.code, parallelResult.code) << "step " << step;
    }

    // operator== on ParticleStateUVE compares floats exactly: the parallel path runs the same
    // per-emitter functions in the same order, so any difference at all would be a bug.
    for (const EntityUVE entity : entities) {
        const std::optional<ParticleStateSnapshotUVE> expected = serial.GetParticleSnapshotUVE(entity);
        const std::optional<ParticleStateSnapshotUVE> actual = parallel.GetParticleSnapshotUVE(entity);
        ASSERT_TRUE(expected.has_value());
        ASSERT_TRUE(actual.has_value());
        EXPECT_EQ(expected->particles, actual->particles) << "emitter " << entity.index;
    }
    // Live counts and generations agree too. Compared field by field rather than as whole instance
    // snapshots, because the snapshots differ - correctly - in exactly one field: workerEligible,
    // which this test set on the parallel runtime only.
    const ParticleRuntimeSnapshotUVE serialSnapshot = serial.GetSnapshotUVE();
    const ParticleRuntimeSnapshotUVE parallelSnapshot = parallel.GetSnapshotUVE();
    ASSERT_EQ(serialSnapshot.instances.size(), parallelSnapshot.instances.size());
    for (std::size_t index = 0; index < serialSnapshot.instances.size(); ++index) {
        EXPECT_EQ(serialSnapshot.instances[index].liveParticles, parallelSnapshot.instances[index].liveParticles);
        EXPECT_EQ(serialSnapshot.instances[index].generation, parallelSnapshot.instances[index].generation);
        EXPECT_EQ(parallelSnapshot.instances[index].workerEligible, index % 2U == 0U);
    }
}

TEST(ParticleRuntimeParallelUVETest, SimulateDetailedUVE_ANonFiniteWorkerEmitterLeavesEveryEmitterUntouched) {
    // Atomicity across the barrier. The failing emitter is simulated on a worker; the healthy ones
    // are on both the worker and the calling thread. None of them may be written.
    ParticleRuntimeUVE runtime;
    const std::vector<EntityUVE> healthy = SeedParallelRuntimeUVE(runtime, 8U, /*markEligible=*/true);
    const EntityUVE poisoned{900U, 1U};
    ASSERT_TRUE(runtime.AttachUVE(poisoned, ParticleEmitterComponentUVE{4U}));
    ASSERT_TRUE(runtime.EmitDetailedUVE(poisoned, ParticleEmissionUVE{
                                                      1U,
                                                      {},
                                                      Math::Vector3UVE{std::numeric_limits<float>::max(), 0.0F, 0.0F},
                                                      2.0F})
                    .IsAcceptedUVE());
    ASSERT_TRUE(runtime.SetWorkerEligibleDetailedUVE(poisoned, true).IsAcceptedUVE());

    std::vector<std::optional<ParticleStateSnapshotUVE>> before;
    for (const EntityUVE entity : healthy) {
        before.push_back(runtime.GetParticleSnapshotUVE(entity));
    }
    const std::optional<ParticleStateSnapshotUVE> poisonedBefore = runtime.GetParticleSnapshotUVE(poisoned);

    Threading::ThreadPoolUVE pool{4U};
    const ParticleRuntimeResultUVE result = runtime.SimulateDetailedUVE(
        0.1F, Math::Vector3UVE{std::numeric_limits<float>::max(), 0.0F, 0.0F}, &pool);

    EXPECT_EQ(result.code, ParticleRuntimeCodeUVE::NonFiniteSimulation);
    for (std::size_t index = 0; index < healthy.size(); ++index) {
        EXPECT_EQ(runtime.GetParticleSnapshotUVE(healthy[index]), before[index]) << "emitter " << index;
    }
    EXPECT_EQ(runtime.GetParticleSnapshotUVE(poisoned), poisonedBefore);
}

TEST(ParticleRuntimeParallelUVETest, SimulateDetailedUVE_WithoutAPoolOrEligibleEmitterTakesTheSerialPath) {
    ParticleRuntimeUVE withNullPool;
    ParticleRuntimeUVE nothingEligible;
    ParticleRuntimeUVE reference;
    const std::vector<EntityUVE> entities = SeedParallelRuntimeUVE(reference, 6U, false);
    static_cast<void>(SeedParallelRuntimeUVE(withNullPool, 6U, true));
    static_cast<void>(SeedParallelRuntimeUVE(nothingEligible, 6U, false));
    Threading::ThreadPoolUVE pool{2U};

    const Math::Vector3UVE gravity{0.0F, -9.81F, 0.0F};
    ASSERT_TRUE(reference.SimulateDetailedUVE(0.05F, gravity).IsAcceptedUVE());
    ASSERT_TRUE(withNullPool.SimulateDetailedUVE(0.05F, gravity, nullptr).IsAcceptedUVE());
    ASSERT_TRUE(nothingEligible.SimulateDetailedUVE(0.05F, gravity, &pool).IsAcceptedUVE());

    for (const EntityUVE entity : entities) {
        EXPECT_EQ(withNullPool.GetParticleSnapshotUVE(entity), reference.GetParticleSnapshotUVE(entity));
        EXPECT_EQ(nothingEligible.GetParticleSnapshotUVE(entity), reference.GetParticleSnapshotUVE(entity));
    }
    // Invalid input answers identically through either overload.
    EXPECT_EQ(withNullPool.SimulateDetailedUVE(-1.0F, gravity, &pool).code,
              ParticleRuntimeCodeUVE::InvalidSimulationInput);
    EXPECT_EQ(withNullPool.SetWorkerEligibleDetailedUVE(EntityUVE{7777U, 1U}, true).code,
              ParticleRuntimeCodeUVE::NoActiveInstance);
}

TEST(ParticleRuntimeUVETest, SimulateUVE_RejectsNonFiniteInputAtomically) {
    ParticleRuntimeUVE runtime;
    const EntityUVE entity{16U, 1U};
    ASSERT_TRUE(runtime.AttachUVE(entity, ParticleEmitterComponentUVE{4U}));
    ASSERT_TRUE(runtime.EmitDetailedUVE(entity, ParticleEmissionUVE{
                                               1U,
                                               {},
                                               Math::Vector3UVE{std::numeric_limits<float>::max(), 0.0F, 0.0F},
                                               2.0F})
                    .IsAcceptedUVE());
    const std::optional<ParticleStateSnapshotUVE> before = runtime.GetParticleSnapshotUVE(entity);
    ASSERT_TRUE(before.has_value());

    const ParticleRuntimeResultUVE result = runtime.SimulateDetailedUVE(
        0.1F, Math::Vector3UVE{std::numeric_limits<float>::max(), 0.0F, 0.0F});
    EXPECT_EQ(result.code, ParticleRuntimeCodeUVE::NonFiniteSimulation);
    EXPECT_EQ(runtime.GetParticleSnapshotUVE(entity), before);
}

TEST(ParticleRuntimeUVETest, DetachUVE_ReportsMissingInstancesAndRemovesOwnership) {
    ParticleRuntimeUVE runtime;
    EXPECT_EQ(runtime.DetachDetailedUVE({12U, 1U}).code, ParticleRuntimeCodeUVE::NoActiveInstance);
    ASSERT_TRUE(runtime.AttachUVE({12U, 1U}, ParticleEmitterComponentUVE{12U}));
    ASSERT_TRUE(runtime.DetachUVE({12U, 1U}));
    EXPECT_FALSE(runtime.HasInstanceUVE({12U, 1U}));
    EXPECT_EQ(runtime.GetInstanceCountUVE(), 0U);
    EXPECT_EQ(runtime.GetTotalBudgetUVE(), 0U);
}

} // namespace UVE::Scene::Tests

