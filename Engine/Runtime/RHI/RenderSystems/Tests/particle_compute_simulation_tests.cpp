// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "particle_compute_simulation.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "particle_emitter_component.h"
#include "event_system.h"
#include "vector3.h"
#include "compute_system.h"
#include "null_render_device.h"
#include "particle_runtime.h"

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/gl.h>

#include "gl_render_device.h"
#include "window_manager.h"

namespace UVE::Render::Tests {
namespace {

constexpr Scene::Entity kEmitterEntity{1U};

/// Emits `count` particles with values that are deliberately awkward: irrational-ish decimals that
/// have no exact binary representation, mixed signs, and lifetimes spread across the cull boundary.
/// Nice round numbers would make a bit-for-bit comparison pass even if one side had quietly
/// contracted a multiply-add; these do not.
[[nodiscard]] std::vector<Scene::ParticleState> MakeAwkwardParticles(const std::size_t count) {
    std::vector<Scene::ParticleState> particles;
    particles.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const float offset = static_cast<float>(index);
        particles.push_back(Scene::ParticleState{
            Math::Vector3{0.1F + offset * 0.3F, -2.7F + offset * 0.17F, 13.37F - offset * 0.9F},
            Math::Vector3{1.1F - offset * 0.23F, 0.77F + offset * 0.31F, -4.2F + offset * 0.13F},
            // Every eleventh particle expires within one 1/60 s step, so culling and compaction
            // are exercised rather than merely declared.
            (index % 11U == 0U) ? 0.004F : 0.5F + offset * 0.01F,
            static_cast<std::uint64_t>(index) + 1U});
    }
    return particles;
}

/// The CPU authority, held as an object rather than a function, and backed by the real
/// Scene::ParticleRuntime rather than a reimplementation of it in the test - a hand-written
/// "expected" integrator would only prove the test agrees with itself.
///
/// It is stateful ON PURPOSE. The runtime assigns each emitted particle a sequence number, so
/// re-emitting the survivors before every step would renumber them while the GPU side carries its
/// originals forward; the two would then disagree on identity from the first cull onwards - a
/// difference in the test harness, not in the engine. One runtime, loaded once, stepped
/// repeatedly, is what actually mirrors how the GPU path is driven.
class CpuAuthority final {
public:
    explicit CpuAuthority(const std::vector<Scene::ParticleState>& particles) {
        Scene::ParticleEmitterComponent emitter;
        emitter.maxParticles = static_cast<std::uint32_t>(particles.size());
        EXPECT_TRUE(m_runtime.Attach(kEmitterEntity, emitter));

        for (const Scene::ParticleState& particle : particles) {
            Scene::ParticleEmission emission;
            emission.count = 1U;
            emission.position = particle.position;
            emission.velocity = particle.velocity;
            emission.lifetimeSeconds = particle.remainingLifetimeSeconds;
            EXPECT_TRUE(m_runtime.EmitDetailed(kEmitterEntity, emission).IsAccepted());
        }
        // MakeAwkwardParticles() numbers its particles from one in emission order, exactly as
        // the runtime does, so the loaded state matches the array the GPU path is handed.
        EXPECT_EQ(GetParticles(), particles);
    }

    void Step(const float deltaSeconds, const Math::Vector3& acceleration) {
        EXPECT_TRUE(m_runtime.SimulateDetailed(deltaSeconds, acceleration).IsAccepted());
    }

    [[nodiscard]] std::vector<Scene::ParticleState> GetParticles() const {
        const std::optional<Scene::ParticleStateSnapshot> snapshot =
            m_runtime.GetParticleSnapshot(kEmitterEntity);
        EXPECT_TRUE(snapshot.has_value());
        return snapshot.has_value() ? snapshot->particles : std::vector<Scene::ParticleState>{};
    }

private:
    Scene::ParticleRuntime m_runtime;
};

/// Compares two particle arrays EXACTLY - float equality, not a tolerance. That is the whole point
/// of CS4: "close enough" would hide a fused multiply-add or a reordered expression in the kernel,
/// and an engine cannot hand a simulation between CPU and GPU frames if the two disagree at all.
void ExpectParticlesIdentical(const std::vector<Scene::ParticleState>& actual,
                                 const std::vector<Scene::ParticleState>& expected) {
    ASSERT_EQ(actual.size(), expected.size()) << "culling and compaction must agree with the CPU";
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        EXPECT_EQ(actual[index], expected[index])
            << "particle " << index << " (sequence " << expected[index].sequence
            << ") differs between the GPU and CPU simulations";
    }
}

// ---------------------------------------------------------------------------
// Backend-independent contract, on the Null device: input validation, atomicity
// and diagnostics. Null records dispatches without executing them, so these
// tests deliberately never assert integrated VALUES - only the contract.
// ---------------------------------------------------------------------------

class ParticleComputeSimulationTest : public ::testing::Test {
protected:
    NullRenderDevice device;
    ComputeSystem computeSystem{device};
    ParticleComputeSimulation simulation{device, computeSystem};
};

TEST_F(ParticleComputeSimulationTest, Initialize_BuildsTheKernelAndIsIdempotent) {
    EXPECT_FALSE(simulation.IsReady());
    ASSERT_TRUE(simulation.Initialize());
    EXPECT_TRUE(simulation.IsReady());

    // A second call must not build a second program - the system owns exactly one kernel.
    EXPECT_TRUE(simulation.Initialize());
    EXPECT_EQ(computeSystem.GetDiagnostics().programsCreated, 1U);
}

TEST_F(ParticleComputeSimulationTest, Simulate_BeforeInitialize_RefusesAndChangesNothing) {
    std::vector<Scene::ParticleState> particles = MakeAwkwardParticles(4U);
    const std::vector<Scene::ParticleState> original = particles;

    EXPECT_FALSE(simulation.Simulate(particles, 1.0F / 60.0F, Math::Vector3{0.0F, -9.81F, 0.0F}));

    EXPECT_EQ(particles, original);
    EXPECT_EQ(simulation.GetDiagnostics().simulationsRejected, 1U);
}

TEST_F(ParticleComputeSimulationTest, Simulate_RejectsExactlyTheInputsTheCpuRuntimeRejects) {
    ASSERT_TRUE(simulation.Initialize());
    std::vector<Scene::ParticleState> particles = MakeAwkwardParticles(4U);
    const std::vector<Scene::ParticleState> original = particles;
    const Math::Vector3 gravity{0.0F, -9.81F, 0.0F};

    EXPECT_FALSE(simulation.Simulate(particles, -0.001F, gravity));
    EXPECT_FALSE(simulation.Simulate(
        particles, Scene::ParticleRuntime::kMaximumSimulationDeltaSeconds + 0.001F, gravity));
    EXPECT_FALSE(simulation.Simulate(particles, std::numeric_limits<float>::quiet_NaN(), gravity));
    EXPECT_FALSE(simulation.Simulate(particles, std::numeric_limits<float>::infinity(), gravity));
    EXPECT_FALSE(simulation.Simulate(
        particles, 1.0F / 60.0F,
        Math::Vector3{0.0F, std::numeric_limits<float>::infinity(), 0.0F}));

    // The boundary itself is legal on both sides - an off-by-one here would silently diverge from
    // the CPU runtime's accepted range.
    Scene::ParticleRuntime runtime;
    EXPECT_TRUE(
        runtime.SimulateDetailed(Scene::ParticleRuntime::kMaximumSimulationDeltaSeconds, gravity)
            .IsAccepted());

    EXPECT_EQ(particles, original);
    EXPECT_EQ(simulation.GetDiagnostics().simulationsRejected, 5U);
    EXPECT_EQ(simulation.GetDiagnostics().simulationsRequested, 5U);
}

TEST_F(ParticleComputeSimulationTest, Simulate_EmptyArrayOrZeroDelta_IsASuccessfulNoOp) {
    ASSERT_TRUE(simulation.Initialize());
    const Math::Vector3 gravity{0.0F, -9.81F, 0.0F};

    std::vector<Scene::ParticleState> empty;
    EXPECT_TRUE(simulation.Simulate(empty, 1.0F / 60.0F, gravity));
    EXPECT_TRUE(empty.empty());

    std::vector<Scene::ParticleState> particles = MakeAwkwardParticles(3U);
    const std::vector<Scene::ParticleState> original = particles;
    EXPECT_TRUE(simulation.Simulate(particles, 0.0F, gravity));
    EXPECT_EQ(particles, original);

    EXPECT_EQ(simulation.GetDiagnostics().simulationsSkipped, 2U);
    EXPECT_EQ(simulation.GetDiagnostics().instancesSimulated, 0U);
}

TEST_F(ParticleComputeSimulationTest, Simulate_OnNullBackend_QueuesRealWorkAndReadsBack) {
    // Null executes nothing, but it is a faithful bookkeeper: the dispatch must be recorded and
    // the readback must succeed, which is what proves the plumbing rather than the arithmetic.
    ASSERT_TRUE(simulation.Initialize());
    std::vector<Scene::ParticleState> particles = MakeAwkwardParticles(100U);

    EXPECT_TRUE(simulation.Simulate(particles, 1.0F / 60.0F, Math::Vector3{0.0F, -9.81F, 0.0F}));

    EXPECT_EQ(computeSystem.GetDiagnostics().dispatchesRecorded, 1U);
    EXPECT_EQ(computeSystem.GetQueuedDispatchCount(), 0U) << "the queue must not be left dirty";
    EXPECT_EQ(simulation.GetDiagnostics().instancesSimulated, 1U);
    EXPECT_EQ(simulation.GetDiagnostics().particlesSimulated, 100U);
    EXPECT_EQ(simulation.GetDiagnostics().readbackFailures, 0U);
    EXPECT_EQ(simulation.GetDiagnostics().uploadFailures, 0U);
}

TEST_F(ParticleComputeSimulationTest, Simulate_ReusesItsBufferAcrossFramesUntilItMustGrow) {
    ASSERT_TRUE(simulation.Initialize());
    const Math::Vector3 gravity{0.0F, -9.81F, 0.0F};
    std::vector<Scene::ParticleState> small = MakeAwkwardParticles(8U);
    std::vector<Scene::ParticleState> large = MakeAwkwardParticles(256U);

    EXPECT_TRUE(simulation.Simulate(small, 1.0F / 60.0F, gravity));
    EXPECT_EQ(simulation.GetDiagnostics().bufferReallocations, 1U);

    // Same size again, and then smaller: neither may allocate, because the buffer only grows.
    EXPECT_TRUE(simulation.Simulate(small, 1.0F / 60.0F, gravity));
    std::vector<Scene::ParticleState> smaller = MakeAwkwardParticles(2U);
    EXPECT_TRUE(simulation.Simulate(smaller, 1.0F / 60.0F, gravity));
    EXPECT_EQ(simulation.GetDiagnostics().bufferReallocations, 1U);

    EXPECT_TRUE(simulation.Simulate(large, 1.0F / 60.0F, gravity));
    EXPECT_EQ(simulation.GetDiagnostics().bufferReallocations, 2U);
}

// ---------------------------------------------------------------------------
// The real proof, on a real GPU/llvmpipe context: the GPU result must equal the
// CPU runtime's, bit for bit. Skips cleanly without a display like every GL test.
// ---------------------------------------------------------------------------

[[nodiscard]] Window::WindowDesc MakeParticleComputeWindowDesc() {
    Window::WindowDesc desc;
    desc.title = "uve_particle_compute_simulation_uve_tests";
    desc.width = 64;
    desc.height = 64;
    // This sandbox's Mesa/llvmpipe GLX stack caps at 4.5 Core; compute needs only 4.3.
    desc.glVersionMajor = 4;
    desc.glVersionMinor = 5;
    return desc;
}

class ParticleComputeSimulationGlTest : public ::testing::Test {
protected:
    void SetUp() override {
        windowManager =
            std::make_unique<Window::WindowManager>(eventSystem, MakeParticleComputeWindowDesc());
        if (!windowManager->IsValid()) {
            GTEST_SKIP() << "No display available for GlRenderDevice - skipping (run under "
                            "xvfb-run to exercise this test)";
        }
        renderDevice = std::make_unique<GlRenderDevice>(*windowManager);
        if (!renderDevice->IsUsable()) {
            GTEST_SKIP() << "GlRenderDevice came up unusable on this display";
        }
        computeSystem = std::make_unique<ComputeSystem>(*renderDevice);
        simulation = std::make_unique<ParticleComputeSimulation>(*renderDevice, *computeSystem);

        std::string infoLog;
        if (!simulation->Initialize(&infoLog)) {
            GTEST_SKIP() << "context lacks compute shaders (GL 4.3+): " << infoLog;
        }
    }

    Events::EventSystem eventSystem;
    std::unique_ptr<Window::WindowManager> windowManager;
    std::unique_ptr<GlRenderDevice> renderDevice;
    std::unique_ptr<ComputeSystem> computeSystem;
    std::unique_ptr<ParticleComputeSimulation> simulation;
};

TEST_F(ParticleComputeSimulationGlTest, Simulate_MatchesTheCpuRuntimeBitForBit) {
    // 1000 particles is past several workgroups and not a multiple of 64, so the kernel's tail
    // guard is exercised: without it the last group would write past the live range.
    const std::vector<Scene::ParticleState> initial = MakeAwkwardParticles(1000U);
    const float deltaSeconds = 1.0F / 60.0F;
    const Math::Vector3 acceleration{0.37F, -9.81F, 1.13F};

    std::vector<Scene::ParticleState> onGpu = initial;
    ASSERT_TRUE(simulation->Simulate(onGpu, deltaSeconds, acceleration));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    CpuAuthority cpu(initial);
    cpu.Step(deltaSeconds, acceleration);
    const std::vector<Scene::ParticleState> onCpu = cpu.GetParticles();

    // Sanity: the step must have actually moved things and culled the short-lived particles, or
    // the comparison below would be comparing two copies of the input.
    ASSERT_LT(onCpu.size(), initial.size()) << "the fixture must produce particles that expire";
    ASSERT_FALSE(onCpu.empty());
    EXPECT_FALSE(onCpu.front().position == initial.front().position);

    ExpectParticlesIdentical(onGpu, onCpu);
}

TEST_F(ParticleComputeSimulationGlTest, Simulate_StaysIdenticalToTheCpuAcrossManySteps) {
    // One step agreeing could be luck in the last bit; drift is what actually breaks a simulation
    // that alternates between CPU and GPU frames. Sixty steps compound any disagreement.
    std::vector<Scene::ParticleState> onGpu = MakeAwkwardParticles(257U);
    CpuAuthority cpu(onGpu);
    const float deltaSeconds = 1.0F / 60.0F;
    const Math::Vector3 acceleration{0.0F, -9.81F, 0.0F};

    for (int step = 0; step < 60; ++step) {
        ASSERT_TRUE(simulation->Simulate(onGpu, deltaSeconds, acceleration)) << "step " << step;
        cpu.Step(deltaSeconds, acceleration);
        ASSERT_NO_FATAL_FAILURE(ExpectParticlesIdentical(onGpu, cpu.GetParticles()))
            << "step " << step;
        ASSERT_FALSE(onGpu.empty()) << "the fixture must keep particles alive for sixty steps";
    }

    EXPECT_EQ(simulation->GetDiagnostics().readbackFailures, 0U);
}

TEST_F(ParticleComputeSimulationGlTest, Simulate_SinglePartialWorkgroup_TouchesNothingBeyondTheCount) {
    // Fewer particles than one workgroup: every invocation but three is a tail invocation. The
    // buffer is sized to the count, so a missing guard would be a real out-of-range write.
    const std::vector<Scene::ParticleState> initial = MakeAwkwardParticles(3U);
    const float deltaSeconds = 1.0F / 120.0F;
    const Math::Vector3 acceleration{-1.5F, 2.25F, 0.5F};

    std::vector<Scene::ParticleState> onGpu = initial;
    ASSERT_TRUE(simulation->Simulate(onGpu, deltaSeconds, acceleration));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    CpuAuthority cpu(initial);
    cpu.Step(deltaSeconds, acceleration);
    ExpectParticlesIdentical(onGpu, cpu.GetParticles());
}

} // namespace
} // namespace UVE::Render::Tests
