// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "particle_runtime.h"

#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include "thread_pool.h"

namespace UVE::Scene::Tests {

TEST(ParticleRuntimeTest, Attach_RejectsInvalidDuplicateAndPreservesBudgetAtomicity) {
    ParticleRuntime runtime;
    EXPECT_EQ(runtime.AttachDetailed(kInvalidEntity, ParticleEmitterComponent{10U}).code,
              ParticleRuntimeCode::InvalidEntity);
    EXPECT_EQ(runtime.AttachDetailed({1U, 1U}, ParticleEmitterComponent{0U}).code,
              ParticleRuntimeCode::InvalidComponent);

    ASSERT_TRUE(runtime.Attach({1U, 1U}, ParticleEmitterComponent{100U}));
    EXPECT_EQ(runtime.AttachDetailed({1U, 1U}, ParticleEmitterComponent{100U}).code,
              ParticleRuntimeCode::DuplicateInstance);
    EXPECT_EQ(runtime.GetInstanceCount(), 1U);
    EXPECT_EQ(runtime.GetTotalBudget(), 100U);
}

TEST(ParticleRuntimeTest, Attach_EnforcesAggregateBudgetAndReleasesItOnDetach) {
    ParticleRuntime runtime;
    const ParticleEmitterComponent maximum{kMaximumParticleEmitterParticles};
    ASSERT_TRUE(runtime.Attach({2U, 1U}, maximum));
    ASSERT_TRUE(runtime.Attach({3U, 1U}, maximum));
    ASSERT_TRUE(runtime.Attach({4U, 1U}, maximum));
    ASSERT_TRUE(runtime.Attach({5U, 1U}, maximum));
    EXPECT_EQ(runtime.GetTotalBudget(), ParticleRuntime::kMaximumTotalParticleBudget);
    EXPECT_EQ(runtime.AttachDetailed({6U, 1U}, ParticleEmitterComponent{1U}).code,
              ParticleRuntimeCode::CapacityExceeded);
    EXPECT_EQ(runtime.GetInstanceCount(), 4U);

    ASSERT_TRUE(runtime.Detach({3U, 1U}));
    EXPECT_EQ(runtime.GetTotalBudget(), 3'000'000U);
    ASSERT_TRUE(runtime.Attach({6U, 1U}, ParticleEmitterComponent{1'000'000U}));
    EXPECT_EQ(runtime.GetTotalBudget(), ParticleRuntime::kMaximumTotalParticleBudget);
}

TEST(ParticleRuntimeTest, State_EnforcesLiveCountAndEnabledLifecycle) {
    ParticleRuntime runtime;
    const Entity entity{7U, 1U};
    ASSERT_TRUE(runtime.Attach(entity, ParticleEmitterComponent{128U}));

    EXPECT_EQ(runtime.SetLiveParticleCountDetailed(entity, 129U).code,
              ParticleRuntimeCode::LiveParticleCountExceeded);
    EXPECT_EQ(runtime.SetLiveParticleCountDetailed(entity, 64U).code,
              ParticleRuntimeCode::Applied);
    EXPECT_EQ(runtime.SetLiveParticleCountDetailed(entity, 64U).code,
              ParticleRuntimeCode::Unchanged);
    EXPECT_EQ(runtime.SetEnabledDetailed(entity, false).code, ParticleRuntimeCode::Applied);
    EXPECT_EQ(runtime.SetEnabledDetailed(entity, false).code, ParticleRuntimeCode::Unchanged);

    const ParticleRuntimeSnapshot snapshot = runtime.GetSnapshot();
    ASSERT_EQ(snapshot.instances.size(), 1U);
    EXPECT_EQ(snapshot.instances.front().entity, entity);
    EXPECT_EQ(snapshot.instances.front().maxParticles, 128U);
    EXPECT_EQ(snapshot.instances.front().liveParticles, 64U);
    EXPECT_FALSE(snapshot.instances.front().enabled);
}

TEST(ParticleRuntimeTest, GetSnapshot_IsDeterministicByGenerationalEntityOrder) {
    ParticleRuntime runtime;
    ASSERT_TRUE(runtime.Attach({11U, 2U}, ParticleEmitterComponent{20U}));
    ASSERT_TRUE(runtime.Attach({10U, 3U}, ParticleEmitterComponent{30U}));
    ASSERT_TRUE(runtime.Attach({10U, 1U}, ParticleEmitterComponent{40U}));

    const ParticleRuntimeSnapshot snapshot = runtime.GetSnapshot();
    ASSERT_EQ(snapshot.instances.size(), 3U);
    EXPECT_EQ(snapshot.instances[0].entity, (Entity{10U, 1U}));
    EXPECT_EQ(snapshot.instances[1].entity, (Entity{10U, 3U}));
    EXPECT_EQ(snapshot.instances[2].entity, (Entity{11U, 2U}));
    EXPECT_EQ(snapshot.totalBudget, 90U);
}

TEST(ParticleRuntimeTest, Emit_AppendsStableParticleStateWithinBudget) {
    ParticleRuntime runtime;
    const Entity entity{13U, 1U};
    ASSERT_TRUE(runtime.Attach(entity, ParticleEmitterComponent{2U}));
    const ParticleEmission emission{2U, Math::Vector3{1.0F, 2.0F, 3.0F},
                                       Math::Vector3{4.0F, 5.0F, 6.0F}, 5.0F};

    ASSERT_TRUE(runtime.EmitDetailed(entity, emission).IsAccepted());
    const std::optional<ParticleStateSnapshot> snapshot = runtime.GetParticleSnapshot(entity);
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->particles.size(), 2U);
    EXPECT_EQ(snapshot->particles[0].sequence, 1U);
    EXPECT_EQ(snapshot->particles[1].sequence, 2U);
    EXPECT_EQ(snapshot->particles[0].position, emission.position);
    EXPECT_EQ(snapshot->particles[0].velocity, emission.velocity);
    EXPECT_EQ(snapshot->particles[0].remainingLifetimeSeconds, 5.0F);
    EXPECT_EQ(runtime.GetSnapshot().instances.front().liveParticles, 2U);

    const ParticleRuntimeSnapshot beforeRejectedEmission = runtime.GetSnapshot();
    EXPECT_EQ(runtime.EmitDetailed(entity, emission).code, ParticleRuntimeCode::LiveParticleCountExceeded);
    EXPECT_EQ(runtime.GetSnapshot(), beforeRejectedEmission);
}

TEST(ParticleRuntimeTest, Emit_RejectsDisabledAndInvalidInputWithoutMutation) {
    ParticleRuntime runtime;
    const Entity entity{14U, 1U};
    ASSERT_TRUE(runtime.Attach(entity, ParticleEmitterComponent{8U}));
    ASSERT_EQ(runtime.SetEnabledDetailed(entity, false).code, ParticleRuntimeCode::Applied);
    EXPECT_EQ(runtime.EmitDetailed(entity, ParticleEmission{1U, {}, {}, 1.0F}).code,
              ParticleRuntimeCode::DisabledInstance);
    ASSERT_EQ(runtime.SetEnabledDetailed(entity, true).code, ParticleRuntimeCode::Applied);

    const ParticleRuntimeSnapshot beforeInvalid = runtime.GetSnapshot();
    ParticleEmission invalid{1U, {}, {}, 1.0F};
    invalid.velocity.x = std::numeric_limits<float>::infinity();
    EXPECT_EQ(runtime.EmitDetailed(entity, invalid).code, ParticleRuntimeCode::InvalidSimulationInput);
    EXPECT_EQ(runtime.GetSnapshot(), beforeInvalid);
}

TEST(ParticleRuntimeTest, Simulate_UsesDeterministicSemiImplicitIntegrationAndLifetimeCulling) {
    ParticleRuntime runtime;
    const Entity entity{15U, 1U};
    ASSERT_TRUE(runtime.Attach(entity, ParticleEmitterComponent{4U}));
    ASSERT_TRUE(runtime.EmitDetailed(
                       entity, ParticleEmission{1U, {}, Math::Vector3{1.0F, 0.0F, 0.0F}, 1.0F})
                    .IsAccepted());

    ASSERT_TRUE(runtime.SimulateDetailed(0.25F, Math::Vector3{0.0F, -2.0F, 0.0F}).IsAccepted());
    auto midpoint = runtime.GetParticleSnapshot(entity);
    ASSERT_TRUE(midpoint.has_value());
    ASSERT_EQ(midpoint->particles.size(), 1U);
    EXPECT_EQ(midpoint->particles.front().position, (Math::Vector3{0.25F, -0.125F, 0.0F}));
    EXPECT_EQ(midpoint->particles.front().velocity, (Math::Vector3{1.0F, -0.5F, 0.0F}));
    EXPECT_EQ(midpoint->particles.front().remainingLifetimeSeconds, 0.75F);

    ASSERT_TRUE(runtime.SimulateDetailed(0.25F, Math::Vector3{0.0F, -2.0F, 0.0F}).IsAccepted());
    auto secondStep = runtime.GetParticleSnapshot(entity);
    ASSERT_TRUE(secondStep.has_value());
    ASSERT_EQ(secondStep->particles.size(), 1U);
    EXPECT_EQ(secondStep->particles.front().position, (Math::Vector3{0.5F, -0.375F, 0.0F}));
    EXPECT_EQ(secondStep->particles.front().velocity, (Math::Vector3{1.0F, -1.0F, 0.0F}));
    EXPECT_EQ(secondStep->particles.front().remainingLifetimeSeconds, 0.5F);

    ASSERT_TRUE(runtime.SimulateDetailed(0.25F, Math::Vector3{0.0F, -2.0F, 0.0F}).IsAccepted());
    ASSERT_TRUE(runtime.SimulateDetailed(0.25F, Math::Vector3{0.0F, -2.0F, 0.0F}).IsAccepted());
    EXPECT_TRUE(runtime.GetParticleSnapshot(entity)->particles.empty());
    EXPECT_EQ(runtime.GetSnapshot().instances.front().liveParticles, 0U);
}

// Thread Group moves worker-eligible emitters onto a thread pool. These tests hold the parallel
// path to the serial one: the same particles, bit for bit, and the same all-or-nothing failure.

/// Builds a runtime with `emitterCount` emitters, each seeded with distinct particles so a mix-up
/// between emitters would show, and every other emitter marked worker-eligible.
[[nodiscard]] std::vector<Entity> SeedParallelRuntime(ParticleRuntime& runtime, const std::uint32_t emitterCount,
                                                            const bool markEligible) {
    std::vector<Entity> entities;
    for (std::uint32_t index = 0; index < emitterCount; ++index) {
        const Entity entity{100U + index, 1U};
        EXPECT_TRUE(runtime.Attach(entity, ParticleEmitterComponent{64U}));
        const float seed = static_cast<float>(index) * 0.37F;
        EXPECT_TRUE(runtime.EmitDetailed(entity, ParticleEmission{
                                                        48U,
                                                        Math::Vector3{seed, seed * 2.0F, -seed},
                                                        Math::Vector3{1.5F + seed, 0.25F, -0.75F * seed},
                                                        1.0F + seed})
                        .IsAccepted());
        if (markEligible && index % 2U == 0U) {
            EXPECT_TRUE(runtime.SetWorkerEligibleDetailed(entity, true).IsAccepted());
        }
        entities.push_back(entity);
    }
    return entities;
}

TEST(ParticleRuntimeParallelTest, SimulateDetailed_WithWorkersIsBitIdenticalToSerial) {
    ParticleRuntime serial;
    ParticleRuntime parallel;
    const std::vector<Entity> entities = SeedParallelRuntime(serial, 24U, /*markEligible=*/false);
    static_cast<void>(SeedParallelRuntime(parallel, 24U, /*markEligible=*/true));
    Threading::ThreadPool pool{4U};

    // Many steps, so an order- or race-dependent difference has room to compound and show. Enough
    // steps also carry particles past their lifetime, exercising the compaction path on workers.
    const Math::Vector3 gravity{0.0F, -9.81F, 0.0F};
    for (int step = 0; step < 90; ++step) {
        const ParticleRuntimeResult serialResult = serial.SimulateDetailed(1.0F / 60.0F, gravity);
        const ParticleRuntimeResult parallelResult = parallel.SimulateDetailed(1.0F / 60.0F, gravity, &pool);
        ASSERT_EQ(serialResult.code, parallelResult.code) << "step " << step;
    }

    // operator== on ParticleState compares floats exactly: the parallel path runs the same
    // per-emitter functions in the same order, so any difference at all would be a bug.
    for (const Entity entity : entities) {
        const std::optional<ParticleStateSnapshot> expected = serial.GetParticleSnapshot(entity);
        const std::optional<ParticleStateSnapshot> actual = parallel.GetParticleSnapshot(entity);
        ASSERT_TRUE(expected.has_value());
        ASSERT_TRUE(actual.has_value());
        EXPECT_EQ(expected->particles, actual->particles) << "emitter " << entity.index;
    }
    // Live counts and generations agree too. Compared field by field rather than as whole instance
    // snapshots, because the snapshots differ - correctly - in exactly one field: workerEligible,
    // which this test set on the parallel runtime only.
    const ParticleRuntimeSnapshot serialSnapshot = serial.GetSnapshot();
    const ParticleRuntimeSnapshot parallelSnapshot = parallel.GetSnapshot();
    ASSERT_EQ(serialSnapshot.instances.size(), parallelSnapshot.instances.size());
    for (std::size_t index = 0; index < serialSnapshot.instances.size(); ++index) {
        EXPECT_EQ(serialSnapshot.instances[index].liveParticles, parallelSnapshot.instances[index].liveParticles);
        EXPECT_EQ(serialSnapshot.instances[index].generation, parallelSnapshot.instances[index].generation);
        EXPECT_EQ(parallelSnapshot.instances[index].workerEligible, index % 2U == 0U);
    }
}

TEST(ParticleRuntimeParallelTest, SimulateDetailed_ANonFiniteWorkerEmitterLeavesEveryEmitterUntouched) {
    // Atomicity across the barrier. The failing emitter is simulated on a worker; the healthy ones
    // are on both the worker and the calling thread. None of them may be written.
    ParticleRuntime runtime;
    const std::vector<Entity> healthy = SeedParallelRuntime(runtime, 8U, /*markEligible=*/true);
    const Entity poisoned{900U, 1U};
    ASSERT_TRUE(runtime.Attach(poisoned, ParticleEmitterComponent{4U}));
    ASSERT_TRUE(runtime.EmitDetailed(poisoned, ParticleEmission{
                                                      1U,
                                                      {},
                                                      Math::Vector3{std::numeric_limits<float>::max(), 0.0F, 0.0F},
                                                      2.0F})
                    .IsAccepted());
    ASSERT_TRUE(runtime.SetWorkerEligibleDetailed(poisoned, true).IsAccepted());

    std::vector<std::optional<ParticleStateSnapshot>> before;
    for (const Entity entity : healthy) {
        before.push_back(runtime.GetParticleSnapshot(entity));
    }
    const std::optional<ParticleStateSnapshot> poisonedBefore = runtime.GetParticleSnapshot(poisoned);

    Threading::ThreadPool pool{4U};
    const ParticleRuntimeResult result = runtime.SimulateDetailed(
        0.1F, Math::Vector3{std::numeric_limits<float>::max(), 0.0F, 0.0F}, &pool);

    EXPECT_EQ(result.code, ParticleRuntimeCode::NonFiniteSimulation);
    for (std::size_t index = 0; index < healthy.size(); ++index) {
        EXPECT_EQ(runtime.GetParticleSnapshot(healthy[index]), before[index]) << "emitter " << index;
    }
    EXPECT_EQ(runtime.GetParticleSnapshot(poisoned), poisonedBefore);
}

TEST(ParticleRuntimeParallelTest, SimulateDetailed_WithoutAPoolOrEligibleEmitterTakesTheSerialPath) {
    ParticleRuntime withNullPool;
    ParticleRuntime nothingEligible;
    ParticleRuntime reference;
    const std::vector<Entity> entities = SeedParallelRuntime(reference, 6U, false);
    static_cast<void>(SeedParallelRuntime(withNullPool, 6U, true));
    static_cast<void>(SeedParallelRuntime(nothingEligible, 6U, false));
    Threading::ThreadPool pool{2U};

    const Math::Vector3 gravity{0.0F, -9.81F, 0.0F};
    ASSERT_TRUE(reference.SimulateDetailed(0.05F, gravity).IsAccepted());
    ASSERT_TRUE(withNullPool.SimulateDetailed(0.05F, gravity, nullptr).IsAccepted());
    ASSERT_TRUE(nothingEligible.SimulateDetailed(0.05F, gravity, &pool).IsAccepted());

    for (const Entity entity : entities) {
        EXPECT_EQ(withNullPool.GetParticleSnapshot(entity), reference.GetParticleSnapshot(entity));
        EXPECT_EQ(nothingEligible.GetParticleSnapshot(entity), reference.GetParticleSnapshot(entity));
    }
    // Invalid input answers identically through either overload.
    EXPECT_EQ(withNullPool.SimulateDetailed(-1.0F, gravity, &pool).code,
              ParticleRuntimeCode::InvalidSimulationInput);
    EXPECT_EQ(withNullPool.SetWorkerEligibleDetailed(Entity{7777U, 1U}, true).code,
              ParticleRuntimeCode::NoActiveInstance);
}

TEST(ParticleRuntimeTest, Simulate_RejectsNonFiniteInputAtomically) {
    ParticleRuntime runtime;
    const Entity entity{16U, 1U};
    ASSERT_TRUE(runtime.Attach(entity, ParticleEmitterComponent{4U}));
    ASSERT_TRUE(runtime.EmitDetailed(entity, ParticleEmission{
                                               1U,
                                               {},
                                               Math::Vector3{std::numeric_limits<float>::max(), 0.0F, 0.0F},
                                               2.0F})
                    .IsAccepted());
    const std::optional<ParticleStateSnapshot> before = runtime.GetParticleSnapshot(entity);
    ASSERT_TRUE(before.has_value());

    const ParticleRuntimeResult result = runtime.SimulateDetailed(
        0.1F, Math::Vector3{std::numeric_limits<float>::max(), 0.0F, 0.0F});
    EXPECT_EQ(result.code, ParticleRuntimeCode::NonFiniteSimulation);
    EXPECT_EQ(runtime.GetParticleSnapshot(entity), before);
}

TEST(ParticleRuntimeTest, Detach_ReportsMissingInstancesAndRemovesOwnership) {
    ParticleRuntime runtime;
    EXPECT_EQ(runtime.DetachDetailed({12U, 1U}).code, ParticleRuntimeCode::NoActiveInstance);
    ASSERT_TRUE(runtime.Attach({12U, 1U}, ParticleEmitterComponent{12U}));
    ASSERT_TRUE(runtime.Detach({12U, 1U}));
    EXPECT_FALSE(runtime.HasInstance({12U, 1U}));
    EXPECT_EQ(runtime.GetInstanceCount(), 0U);
    EXPECT_EQ(runtime.GetTotalBudget(), 0U);
}

} // namespace UVE::Scene::Tests

