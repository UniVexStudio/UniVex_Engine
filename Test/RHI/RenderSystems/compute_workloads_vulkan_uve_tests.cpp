// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "uve/math/aabb_uve.h"
#include "uve/math/frustum_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/render_systems/compute_system_uve.h"
#include "uve/render_systems/frustum_cull_compute_uve.h"
#include "uve/render_systems/particle_compute_simulation_uve.h"
#include "uve/rhi_vulkan/vulkan_render_device_uve.h"
#include "uve/component/particle_emitter_component_uve.h"
#include "uve/scene/particle_runtime_uve.h"

namespace UVE::Render::Tests {
namespace {

// ---------------------------------------------------------------------------
// CS4's particle simulation and CS5's culling were both proven on a real GL
// context, and both claimed to be backend-independent engine systems. They were
// not: their kernels existed only as GLSL, so on Vulkan - which takes SPIR-V -
// they compiled nothing and refused to initialize. CS6 closed that gap by baking
// SPIR-V for both kernels and selecting the right payload per backend. These
// tests are what stop the gap reopening: the SAME workloads, the SAME bit-for-bit
// standard, on the Vulkan device.
//
// The fixture mirrors VulkanRenderDeviceUVETest's two real-device paths - windowed
// where a display exists, headless over VK_EXT_headless_surface otherwise - and
// skips only when neither is available.
// ---------------------------------------------------------------------------

constexpr Scene::EntityUVE kEmitterEntityUVE{1U};

class ComputeWorkloadsVulkanUVETest : public ::testing::Test {
protected:
    void SetUp() override {
        device = VulkanRenderDeviceUVE::CreateHeadlessUVE();
        if (device == nullptr) {
            GTEST_SKIP() << "No headless Vulkan device path on this host - skipping (needs a "
                            "loader/ICD with VK_EXT_headless_surface, like SwiftShader or lavapipe)";
        }
        computeSystem = std::make_unique<ComputeSystemUVE>(*device);
    }

    std::unique_ptr<VulkanRenderDeviceUVE> device;
    std::unique_ptr<ComputeSystemUVE> computeSystem;
};

/// Same awkward values CS4's GL test uses: decimals with no exact binary representation, mixed
/// signs, and lifetimes straddling the cull boundary. Round numbers would agree even through a
/// contracted multiply-add and prove nothing.
[[nodiscard]] std::vector<Scene::ParticleStateUVE> MakeAwkwardParticlesUVE(const std::size_t count) {
    std::vector<Scene::ParticleStateUVE> particles;
    particles.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const float offset = static_cast<float>(index);
        particles.push_back(Scene::ParticleStateUVE{
            Math::Vector3UVE{0.1F + offset * 0.3F, -2.7F + offset * 0.17F, 13.37F - offset * 0.9F},
            Math::Vector3UVE{1.1F - offset * 0.23F, 0.77F + offset * 0.31F, -4.2F + offset * 0.13F},
            (index % 11U == 0U) ? 0.004F : 0.5F + offset * 0.01F,
            static_cast<std::uint64_t>(index) + 1U});
    }
    return particles;
}

/// The CPU authority for particles: the real Scene::ParticleRuntimeUVE, loaded once and stepped -
/// never a reimplementation of the integrator in the test, which would only prove the test agrees
/// with itself.
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

[[nodiscard]] Math::FrustumUVE MakeTestFrustumUVE() {
    const Math::Matrix4x4UVE projection =
        Math::Matrix4x4UVE::PerspectiveUVE(60.0F * 3.14159265F / 180.0F, 16.0F / 9.0F, 0.1F, 100.0F);
    const Math::Matrix4x4UVE view = Math::Matrix4x4UVE::ViewFromPositionAndRotationUVE(
        Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::QuaternionUVE{});
    return Math::FrustumUVE::FromViewProjectionUVE(projection * view);
}

/// The same periodic spread CS5's GL test uses, which is verified to contain both visible and
/// culled boxes at every length - the earlier monotonic sweep was entirely culled for small counts
/// and would have passed against a kernel that answered a constant.
[[nodiscard]] std::vector<Math::AabbUVE> MakeBoxSpreadUVE(const std::size_t count) {
    std::vector<Math::AabbUVE> boxes;
    boxes.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const float step = static_cast<float>(index);
        const float halfSize = 0.25F + std::fmod(step * 0.031F, 0.5F);
        const float depth = -1.7F - std::fmod(step * 3.301F, 90.0F);
        const bool pushedOutside = (index % 3U) == 0U;
        const float lateral = pushedOutside ? (index % 2U == 0U ? -140.0F : 140.0F)
                                            : (-1.3F + std::fmod(step * 0.211F, 2.6F));
        const float vertical = pushedOutside ? (index % 2U == 0U ? 90.0F : -90.0F)
                                             : (0.9F - std::fmod(step * 0.173F, 1.8F));
        boxes.push_back(Math::AabbUVE::FromCenterExtentsUVE(
            Math::Vector3UVE{lateral, vertical, depth},
            Math::Vector3UVE{halfSize, halfSize, halfSize}));
    }
    return boxes;
}

TEST_F(ComputeWorkloadsVulkanUVETest, ParticleSimulation_InitializesAndMatchesTheCpuBitForBit) {
    ParticleComputeSimulationUVE simulation(*device, *computeSystem);

    std::string infoLog;
    // This assertion is the whole point of CS6. Before the SPIR-V bake this returned false on
    // Vulkan - the kernel was GLSL text the backend rightly refuses - so the workload was
    // GL-only while presenting itself as a backend-independent engine system.
    ASSERT_TRUE(simulation.InitializeUVE(&infoLog))
        << "the particle kernel must build on Vulkan, not just OpenGL: " << infoLog;
    EXPECT_TRUE(simulation.IsReadyUVE());

    const std::vector<Scene::ParticleStateUVE> initial = MakeAwkwardParticlesUVE(1000U);
    const float deltaSeconds = 1.0F / 60.0F;
    const Math::Vector3UVE acceleration{0.37F, -9.81F, 1.13F};

    std::vector<Scene::ParticleStateUVE> onGpu = initial;
    ASSERT_TRUE(simulation.SimulateUVE(onGpu, deltaSeconds, acceleration));

    const std::vector<Scene::ParticleStateUVE> onCpu =
        SimulateOnCpuUVE(initial, deltaSeconds, acceleration);
    ASSERT_LT(onCpu.size(), initial.size()) << "the fixture must produce particles that expire";
    ASSERT_FALSE(onCpu.empty());

    ASSERT_EQ(onGpu.size(), onCpu.size()) << "culling and compaction must agree with the CPU";
    for (std::size_t index = 0U; index < onCpu.size(); ++index) {
        EXPECT_EQ(onGpu[index], onCpu[index])
            << "particle " << index << " differs between the Vulkan GPU and CPU simulations";
    }
}

TEST_F(ComputeWorkloadsVulkanUVETest, FrustumCull_InitializesAndAgreesWithTheCpuOnEveryBox) {
    FrustumCullComputeUVE cull(*device, *computeSystem);

    std::string infoLog;
    ASSERT_TRUE(cull.InitializeUVE(&infoLog))
        << "the cull kernel must build on Vulkan, not just OpenGL: " << infoLog;

    // 1000 boxes is not a multiple of the 64-wide workgroup, so the kernel's tail guard runs
    // against buffers sized exactly to the box count.
    const std::vector<Math::AabbUVE> boxes = MakeBoxSpreadUVE(1000U);
    const Math::FrustumUVE frustum = MakeTestFrustumUVE();

    std::vector<bool> onGpu;
    ASSERT_TRUE(cull.CullUVE(boxes, frustum, onGpu));

    std::vector<bool> onCpu;
    onCpu.reserve(boxes.size());
    for (const Math::AabbUVE& box : boxes) {
        onCpu.push_back(frustum.IntersectsUVE(box));
    }

    const std::size_t visibleCount =
        static_cast<std::size_t>(std::count(onCpu.begin(), onCpu.end(), true));
    ASSERT_GT(visibleCount, 0U) << "the fixture must produce visible boxes";
    ASSERT_LT(visibleCount, onCpu.size()) << "the fixture must produce culled boxes";

    ASSERT_EQ(onGpu.size(), onCpu.size());
    for (std::size_t index = 0U; index < onCpu.size(); ++index) {
        EXPECT_EQ(onGpu[index], onCpu[index])
            << "box " << index << " " << Math::ToStringUVE(boxes[index]) << ": Vulkan said "
            << (onGpu[index] ? "visible" : "culled") << " and the CPU said "
            << (onCpu[index] ? "visible" : "culled");
    }
}

TEST_F(ComputeWorkloadsVulkanUVETest, FrustumCull_AgreesOnBoxesSittingExactlyOnThePlanes) {
    // The boundary cases, on the second backend: a box touching a plane exactly is INSIDE by the
    // CPU's strict rejection, and a fused multiply-add in the kernel would land on a different
    // float precisely here and nowhere else.
    FrustumCullComputeUVE cull(*device, *computeSystem);
    std::string infoLog;
    ASSERT_TRUE(cull.InitializeUVE(&infoLog)) << infoLog;

    const Math::FrustumUVE frustum = MakeTestFrustumUVE();
    const Math::Vector3UVE extents{0.5F, 0.5F, 0.5F};
    std::vector<Math::AabbUVE> boxes;
    for (const Math::PlaneUVE& plane : frustum.planes) {
        const float radius = extents.x * std::abs(plane.normal.x) + extents.y * std::abs(plane.normal.y) +
                             extents.z * std::abs(plane.normal.z);
        const Math::Vector3UVE onPlane{plane.normal.x * -plane.distance, plane.normal.y * -plane.distance,
                                       plane.normal.z * -plane.distance};
        const Math::Vector3UVE center{onPlane.x - plane.normal.x * radius,
                                      onPlane.y - plane.normal.y * radius,
                                      onPlane.z - plane.normal.z * radius};
        boxes.push_back(Math::AabbUVE::FromCenterExtentsUVE(center, extents));
        for (const float nudge : {-2.0F, -1.0F, 1.0F, 2.0F}) {
            boxes.push_back(Math::AabbUVE::FromCenterExtentsUVE(
                Math::Vector3UVE{std::nextafter(center.x, center.x + nudge),
                                 std::nextafter(center.y, center.y + nudge),
                                 std::nextafter(center.z, center.z + nudge)},
                extents));
        }
    }

    std::vector<bool> onGpu;
    ASSERT_TRUE(cull.CullUVE(boxes, frustum, onGpu));

    ASSERT_EQ(onGpu.size(), boxes.size());
    std::size_t visibleCount = 0U;
    for (std::size_t index = 0U; index < boxes.size(); ++index) {
        const bool expected = frustum.IntersectsUVE(boxes[index]);
        visibleCount += expected ? 1U : 0U;
        EXPECT_EQ(onGpu[index], expected) << "boundary box " << index << " disagrees on Vulkan";
    }
    EXPECT_GT(visibleCount, 0U) << "the boundary set must contain both answers to prove anything";
    EXPECT_LT(visibleCount, boxes.size());
}

} // namespace
} // namespace UVE::Render::Tests
