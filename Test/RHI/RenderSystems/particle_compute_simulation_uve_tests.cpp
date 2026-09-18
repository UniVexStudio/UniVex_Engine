// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/particle_compute_simulation_uve.h"

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

#include "uve/component/particle_emitter_component_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/render_systems/compute_system_uve.h"
#include "uve/rhi_null/null_render_device_uve.h"
#include "uve/scene/particle_runtime_uve.h"

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/gl.h>

#include "uve/rhi_opengl/gl_render_device_uve.h"
#include "uve/window/window_manager_uve.h"

namespace UVE::Render::Tests {
namespace {

constexpr Scene::EntityUVE kEmitterEntityUVE{1U};

/// Emits `count` particles with values that are deliberately awkward: irrational-ish decimals that
/// have no exact binary representation, mixed signs, and lifetimes spread across the cull boundary.
/// Nice round numbers would make a bit-for-bit comparison pass even if one side had quietly
/// contracted a multiply-add; these do not.
[[nodiscard]] std::vector<Scene::ParticleStateUVE> MakeAwkwardParticlesUVE(const std::size_t count) {
    std::vector<Scene::ParticleStateUVE> particles;
    particles.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const float offset = static_cast<float>(index);
        particles.push_back(Scene::ParticleStateUVE{
            Math::Vector3UVE{0.1F + offset * 0.3F, -2.7F + offset * 0.17F, 13.37F - offset * 0.9F},
            Math::Vector3UVE{1.1F - offset * 0.23F, 0.77F + offset * 0.31F, -4.2F + offset * 0.13F},
            // Every eleventh particle expires within one 1/60 s step, so culling and compaction
            // are exercised rather than merely declared.
            (index % 11U == 0U) ? 0.004F : 0.5F + offset * 0.01F,
            static_cast<std::uint64_t>(index) + 1U});
    }
    return particles;
}

/// The CPU authority, run through the real Scene::ParticleRuntimeUVE rather than a reimplementation
/// of it in the test - a hand-written "expected" integrator would only prove the test agrees with
/// itself. The runtime is loaded with exactly `particles` by emitting them one at a time, stepped,
/// and its resulting particle array returned.
[[nodiscard]] std::vector<Scene::ParticleStateUVE> SimulateOnCpuUVE(
    const std::vector<Scene::ParticleStateUVE>& particles, const float deltaSeconds,
    const Math::Vector3UVE& acceleration) {
    Scene::ParticleRuntimeUVE runtime;
    Scene::ParticleEmitterComponentUVE emitter;
    emitter.maxParticles = static_cast<std::uint32_t>(particles.size());
    EXPECT_TRUE(runtime.AttachUVE(kEmitterEntityUVE, emitter));

    for (const Scene::ParticleStateUVE& particle : particles) {
        Scene::ParticleEmissionUVE emission;
        emission.count = 1U;
        emission.position = particle.position;
        emission.velocity = particle.velocity;
        emission.lifetimeSeconds = particle.remainingLifetimeSeconds;
        EXPECT_TRUE(runtime.EmitDetailedUVE(kEmitterEntityUVE, emission).IsAcceptedUVE());
    }

    EXPECT_TRUE(runtime.SimulateDetailedUVE(deltaSeconds, acceleration).IsAcceptedUVE());

    const std::optional<Scene::ParticleStateSnapshotUVE> snapshot =
        runtime.GetParticleSnapshotUVE(kEmitterEntityUVE);
    EXPECT_TRUE(snapshot.has_value());
    return snapshot.has_value() ? snapshot->particles : std::vector<Scene::ParticleStateUVE>{};
}

/// Compares two particle arrays EXACTLY - float equality, not a tolerance. That is the whole point
/// of CS4: "close enough" would hide a fused multiply-add or a reordered expression in the kernel,
/// and an engine cannot hand a simulation between CPU and GPU frames if the two disagree at all.
void ExpectParticlesIdenticalUVE(const std::vector<Scene::ParticleStateUVE>& actual,
                                 const std::vector<Scene::ParticleStateUVE>& expected) {
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

class ParticleComputeSimulationUVETest : public ::testing::Test {
protected:
    NullRenderDeviceUVE device;
    ComputeSystemUVE computeSystem{device};
    ParticleComputeSimulationUVE simulation{device, computeSystem};
};

TEST_F(ParticleComputeSimulationUVETest, InitializeUVE_BuildsTheKernelAndIsIdempotent) {
    EXPECT_FALSE(simulation.IsReadyUVE());
    ASSERT_TRUE(simulation.InitializeUVE());
    EXPECT_TRUE(simulation.IsReadyUVE());

    // A second call must not build a second program - the system owns exactly one kernel.
    EXPECT_TRUE(simulation.InitializeUVE());
    EXPECT_EQ(computeSystem.GetDiagnosticsUVE().programsCreated, 1U);
}

TEST_F(ParticleComputeSimulationUVETest, SimulateUVE_BeforeInitialize_RefusesAndChangesNothing) {
    std::vector<Scene::ParticleStateUVE> particles = MakeAwkwardParticlesUVE(4U);
    const std::vector<Scene::ParticleStateUVE> original = particles;

    EXPECT_FALSE(simulation.SimulateUVE(particles, 1.0F / 60.0F, Math::Vector3UVE{0.0F, -9.81F, 0.0F}));

    EXPECT_EQ(particles, original);
    EXPECT_EQ(simulation.GetDiagnosticsUVE().simulationsRejected, 1U);
}

TEST_F(ParticleComputeSimulationUVETest, SimulateUVE_RejectsExactlyTheInputsTheCpuRuntimeRejects) {
    ASSERT_TRUE(simulation.InitializeUVE());
    std::vector<Scene::ParticleStateUVE> particles = MakeAwkwardParticlesUVE(4U);
    const std::vector<Scene::ParticleStateUVE> original = particles;
    const Math::Vector3UVE gravity{0.0F, -9.81F, 0.0F};

    EXPECT_FALSE(simulation.SimulateUVE(particles, -0.001F, gravity));
    EXPECT_FALSE(simulation.SimulateUVE(
        particles, Scene::ParticleRuntimeUVE::kMaximumSimulationDeltaSecondsUVE + 0.001F, gravity));
    EXPECT_FALSE(simulation.SimulateUVE(particles, std::numeric_limits<float>::quiet_NaN(), gravity));
    EXPECT_FALSE(simulation.SimulateUVE(particles, std::numeric_limits<float>::infinity(), gravity));
    EXPECT_FALSE(simulation.SimulateUVE(
        particles, 1.0F / 60.0F,
        Math::Vector3UVE{0.0F, std::numeric_limits<float>::infinity(), 0.0F}));

    // The boundary itself is legal on both sides - an off-by-one here would silently diverge from
    // the CPU runtime's accepted range.
    Scene::ParticleRuntimeUVE runtime;
    EXPECT_TRUE(
        runtime.SimulateDetailedUVE(Scene::ParticleRuntimeUVE::kMaximumSimulationDeltaSecondsUVE, gravity)
            .IsAcceptedUVE());

    EXPECT_EQ(particles, original);
    EXPECT_EQ(simulation.GetDiagnosticsUVE().simulationsRejected, 5U);
    EXPECT_EQ(simulation.GetDiagnosticsUVE().simulationsRequested, 5U);
}

TEST_F(ParticleComputeSimulationUVETest, SimulateUVE_EmptyArrayOrZeroDelta_IsASuccessfulNoOp) {
    ASSERT_TRUE(simulation.InitializeUVE());
    const Math::Vector3UVE gravity{0.0F, -9.81F, 0.0F};

    std::vector<Scene::ParticleStateUVE> empty;
    EXPECT_TRUE(simulation.SimulateUVE(empty, 1.0F / 60.0F, gravity));
    EXPECT_TRUE(empty.empty());

    std::vector<Scene::ParticleStateUVE> particles = MakeAwkwardParticlesUVE(3U);
    const std::vector<Scene::ParticleStateUVE> original = particles;
    EXPECT_TRUE(simulation.SimulateUVE(particles, 0.0F, gravity));
    EXPECT_EQ(particles, original);

    EXPECT_EQ(simulation.GetDiagnosticsUVE().simulationsSkipped, 2U);
    EXPECT_EQ(simulation.GetDiagnosticsUVE().instancesSimulated, 0U);
}

TEST_F(ParticleComputeSimulationUVETest, SimulateUVE_OnNullBackend_QueuesRealWorkAndReadsBack) {
    // Null executes nothing, but it is a faithful bookkeeper: the dispatch must be recorded and
    // the readback must succeed, which is what proves the plumbing rather than the arithmetic.
    ASSERT_TRUE(simulation.InitializeUVE());
    std::vector<Scene::ParticleStateUVE> particles = MakeAwkwardParticlesUVE(100U);

    EXPECT_TRUE(simulation.SimulateUVE(particles, 1.0F / 60.0F, Math::Vector3UVE{0.0F, -9.81F, 0.0F}));

    EXPECT_EQ(computeSystem.GetDiagnosticsUVE().dispatchesRecorded, 1U);
    EXPECT_EQ(computeSystem.GetQueuedDispatchCountUVE(), 0U) << "the queue must not be left dirty";
    EXPECT_EQ(simulation.GetDiagnosticsUVE().instancesSimulated, 1U);
    EXPECT_EQ(simulation.GetDiagnosticsUVE().particlesSimulated, 100U);
    EXPECT_EQ(simulation.GetDiagnosticsUVE().readbackFailures, 0U);
    EXPECT_EQ(simulation.GetDiagnosticsUVE().uploadFailures, 0U);
}

TEST_F(ParticleComputeSimulationUVETest, SimulateUVE_ReusesItsBufferAcrossFramesUntilItMustGrow) {
    ASSERT_TRUE(simulation.InitializeUVE());
    const Math::Vector3UVE gravity{0.0F, -9.81F, 0.0F};
    std::vector<Scene::ParticleStateUVE> small = MakeAwkwardParticlesUVE(8U);
    std::vector<Scene::ParticleStateUVE> large = MakeAwkwardParticlesUVE(256U);

    EXPECT_TRUE(simulation.SimulateUVE(small, 1.0F / 60.0F, gravity));
    EXPECT_EQ(simulation.GetDiagnosticsUVE().bufferReallocations, 1U);

    // Same size again, and then smaller: neither may allocate, because the buffer only grows.
    EXPECT_TRUE(simulation.SimulateUVE(small, 1.0F / 60.0F, gravity));
    std::vector<Scene::ParticleStateUVE> smaller = MakeAwkwardParticlesUVE(2U);
    EXPECT_TRUE(simulation.SimulateUVE(smaller, 1.0F / 60.0F, gravity));
    EXPECT_EQ(simulation.GetDiagnosticsUVE().bufferReallocations, 1U);

    EXPECT_TRUE(simulation.SimulateUVE(large, 1.0F / 60.0F, gravity));
    EXPECT_EQ(simulation.GetDiagnosticsUVE().bufferReallocations, 2U);
}

// ---------------------------------------------------------------------------
// The real proof, on a real GPU/llvmpipe context: the GPU result must equal the
// CPU runtime's, bit for bit. Skips cleanly without a display like every GL test.
// ---------------------------------------------------------------------------

[[nodiscard]] Window::WindowDescUVE MakeParticleComputeWindowDescUVE() {
    Window::WindowDescUVE desc;
    desc.title = "uve_particle_compute_simulation_uve_tests";
    desc.width = 64;
    desc.height = 64;
    // This sandbox's Mesa/llvmpipe GLX stack caps at 4.5 Core; compute needs only 4.3.
    desc.glVersionMajor = 4;
    desc.glVersionMinor = 5;
    return desc;
}

class ParticleComputeSimulationGlUVETest : public ::testing::Test {
protected:
    void SetUp() override {
        windowManager =
            std::make_unique<Window::WindowManagerUVE>(eventSystem, MakeParticleComputeWindowDescUVE());
        if (!windowManager->IsValidUVE()) {
            GTEST_SKIP() << "No display available for GlRenderDeviceUVE - skipping (run under "
                            "xvfb-run to exercise this test)";
        }
        renderDevice = std::make_unique<GlRenderDeviceUVE>(*windowManager);
        if (!renderDevice->IsUsableUVE()) {
            GTEST_SKIP() << "GlRenderDeviceUVE came up unusable on this display";
        }
        computeSystem = std::make_unique<ComputeSystemUVE>(*renderDevice);
        simulation = std::make_unique<ParticleComputeSimulationUVE>(*renderDevice, *computeSystem);

        std::string infoLog;
        if (!simulation->InitializeUVE(&infoLog)) {
            GTEST_SKIP() << "context lacks compute shaders (GL 4.3+): " << infoLog;
        }
    }

    Events::EventSystemUVE eventSystem;
    std::unique_ptr<Window::WindowManagerUVE> windowManager;
    std::unique_ptr<GlRenderDeviceUVE> renderDevice;
    std::unique_ptr<ComputeSystemUVE> computeSystem;
    std::unique_ptr<ParticleComputeSimulationUVE> simulation;
};

TEST_F(ParticleComputeSimulationGlUVETest, SimulateUVE_MatchesTheCpuRuntimeBitForBit) {
    // 1000 particles is past several workgroups and not a multiple of 64, so the kernel's tail
    // guard is exercised: without it the last group would write past the live range.
    const std::vector<Scene::ParticleStateUVE> initial = MakeAwkwardParticlesUVE(1000U);
    const float deltaSeconds = 1.0F / 60.0F;
    const Math::Vector3UVE acceleration{0.37F, -9.81F, 1.13F};

    std::vector<Scene::ParticleStateUVE> onGpu = initial;
    ASSERT_TRUE(simulation->SimulateUVE(onGpu, deltaSeconds, acceleration));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    const std::vector<Scene::ParticleStateUVE> onCpu =
        SimulateOnCpuUVE(initial, deltaSeconds, acceleration);

    // Sanity: the step must have actually moved things and culled the short-lived particles, or
    // the comparison below would be comparing two copies of the input.
    ASSERT_LT(onCpu.size(), initial.size()) << "the fixture must produce particles that expire";
    ASSERT_FALSE(onCpu.empty());
    EXPECT_NE(onCpu.front().position, initial.front().position);

    ExpectParticlesIdenticalUVE(onGpu, onCpu);
}

TEST_F(ParticleComputeSimulationGlUVETest, SimulateUVE_StaysIdenticalToTheCpuAcrossManySteps) {
    // One step agreeing could be luck in the last bit; drift is what actually breaks a simulation
    // that alternates between CPU and GPU frames. Sixty steps compound any disagreement.
    std::vector<Scene::ParticleStateUVE> onGpu = MakeAwkwardParticlesUVE(257U);
    std::vector<Scene::ParticleStateUVE> onCpu = onGpu;
    const float deltaSeconds = 1.0F / 60.0F;
    const Math::Vector3UVE acceleration{0.0F, -9.81F, 0.0F};

    for (int step = 0; step < 60; ++step) {
        ASSERT_TRUE(simulation->SimulateUVE(onGpu, deltaSeconds, acceleration)) << "step " << step;
        if (onCpu.empty()) {
            break;
        }
        onCpu = SimulateOnCpuUVE(onCpu, deltaSeconds, acceleration);
        ASSERT_NO_FATAL_FAILURE(ExpectParticlesIdenticalUVE(onGpu, onCpu)) << "step " << step;
    }

    EXPECT_EQ(simulation->GetDiagnosticsUVE().readbackFailures, 0U);
}

TEST_F(ParticleComputeSimulationGlUVETest, SimulateUVE_SinglePartialWorkgroup_TouchesNothingBeyondTheCount) {
    // Fewer particles than one workgroup: every invocation but three is a tail invocation. The
    // buffer is sized to the count, so a missing guard would be a real out-of-range write.
    const std::vector<Scene::ParticleStateUVE> initial = MakeAwkwardParticlesUVE(3U);
    const float deltaSeconds = 1.0F / 120.0F;
    const Math::Vector3UVE acceleration{-1.5F, 2.25F, 0.5F};

    std::vector<Scene::ParticleStateUVE> onGpu = initial;
    ASSERT_TRUE(simulation->SimulateUVE(onGpu, deltaSeconds, acceleration));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    ExpectParticlesIdenticalUVE(onGpu, SimulateOnCpuUVE(initial, deltaSeconds, acceleration));
}

} // namespace
} // namespace UVE::Render::Tests
