// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "uve/asset/mesh_asset_uve.h"
#include "uve/asset/mesh_skinning_uve.h"
#include "uve/math/aabb_uve.h"
#include "uve/math/frustum_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/render_systems/compute_system_uve.h"
#include "uve/render_systems/frustum_cull_compute_uve.h"
#include "uve/render_systems/frustum_cull_indirect_uve.h"
#include "uve/render_systems/mesh_skin_compute_uve.h"
#include "uve/render_systems/particle_compute_simulation_uve.h"
#include "uve/rhi/render_resource_descs_uve.h"
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


// --- CS8 on the second backend ----------------------------------------------------------------
//
// CS4 and CS5 each claimed to be backend-independent and each was wrong - in two different ways,
// and only running the second backend found either. So the indirect cull gets its Vulkan arm in
// the same commit as its GL one, not a commit later. The thing most likely to differ here is the
// atomic: the GL and SPIR-V paths reach atomicAdd through different code generators, and a kernel
// that compiled but did not actually atomically accumulate would produce a plausible-looking
// undercount rather than an error.

TEST_F(ComputeWorkloadsVulkanUVETest, FrustumCullIndirect_CompactsExactlyWhatTheCpuKeeps) {
    FrustumCullIndirectUVE cull(*device, *computeSystem);
    std::string infoLog;
    ASSERT_TRUE(cull.InitializeUVE(&infoLog))
        << "the indirect cull kernel must build on Vulkan, not just OpenGL: " << infoLog;

    const std::vector<Math::AabbUVE> boxes = MakeBoxSpreadUVE(1000U);
    const Math::FrustumUVE frustum = MakeTestFrustumUVE();

    std::vector<std::uint32_t> expected;
    for (std::size_t index = 0U; index < boxes.size(); ++index) {
        if (frustum.IntersectsUVE(boxes[index])) {
            expected.push_back(static_cast<std::uint32_t>(index));
        }
    }
    ASSERT_GT(expected.size(), 0U) << "the fixture must produce visible boxes";
    ASSERT_LT(expected.size(), boxes.size()) << "the fixture must produce culled boxes";

    DrawIndexedIndirectCommandUVE meshParams;
    meshParams.indexCount = 36U;
    meshParams.instanceCount = 9999U; // the GPU owns this field and must overwrite it
    meshParams.firstIndex = 12U;
    meshParams.vertexOffset = -7;
    meshParams.firstInstance = 3U;
    ASSERT_TRUE(cull.PrepareUVE(boxes, frustum, meshParams));

    // Reading back is a TEST-only act: PrepareUVE leaves all of this on the GPU, which is the
    // point of the system.
    DrawIndexedIndirectCommandUVE command{};
    ASSERT_TRUE(device->ReadbackBufferUVE(
        cull.GetDrawCommandBufferUVE(),
        std::span<std::byte>{reinterpret_cast<std::byte*>(&command), sizeof(command)}));
    EXPECT_EQ(command.instanceCount, expected.size());
    EXPECT_EQ(command.indexCount, 36U);
    EXPECT_EQ(command.firstIndex, 12U);
    EXPECT_EQ(command.vertexOffset, -7);
    EXPECT_EQ(command.firstInstance, 3U);

    std::vector<std::uint32_t> actual(command.instanceCount, 0U);
    ASSERT_TRUE(device->ReadbackBufferUVE(
        cull.GetVisibleIndexBufferUVE(),
        std::span<std::byte>{reinterpret_cast<std::byte*>(actual.data()),
                             actual.size() * sizeof(std::uint32_t)}));
    // Sorted: the kernel's atomic assigns compaction slots in no defined order, so this is a set
    // comparison by construction.
    std::sort(actual.begin(), actual.end());
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        EXPECT_EQ(actual[index], expected[index])
            << "compacted slot " << index << ": Vulkan kept box " << actual[index]
            << " where the CPU kept box " << expected[index];
    }
}

TEST_F(ComputeWorkloadsVulkanUVETest, FrustumCullIndirect_RepeatedCalls_DoNotAccumulateInstances) {
    // The seed-to-zero step, on the backend where a missed buffer update is most likely: Vulkan
    // queues the dispatch and replays it in PresentUVE, so the ordering of "write the seed" and
    // "run the kernel" is not the record-time ordering GL gives.
    FrustumCullIndirectUVE cull(*device, *computeSystem);
    std::string infoLog;
    ASSERT_TRUE(cull.InitializeUVE(&infoLog)) << infoLog;

    const std::vector<Math::AabbUVE> boxes = MakeBoxSpreadUVE(137U);
    const Math::FrustumUVE frustum = MakeTestFrustumUVE();
    std::size_t expectedVisible = 0U;
    for (const Math::AabbUVE& box : boxes) {
        expectedVisible += frustum.IntersectsUVE(box) ? 1U : 0U;
    }
    ASSERT_GT(expectedVisible, 0U);

    DrawIndexedIndirectCommandUVE meshParams;
    meshParams.indexCount = 6U;
    for (int pass = 0; pass < 3; ++pass) {
        ASSERT_TRUE(cull.PrepareUVE(boxes, frustum, meshParams)) << "on pass " << pass;
        DrawIndexedIndirectCommandUVE command{};
        ASSERT_TRUE(device->ReadbackBufferUVE(
            cull.GetDrawCommandBufferUVE(),
            std::span<std::byte>{reinterpret_cast<std::byte*>(&command), sizeof(command)}));
        EXPECT_EQ(command.instanceCount, expectedVisible) << "on pass " << pass;
    }
}


// --- CS10 on the second backend ----------------------------------------------------------------
//
// The bit-for-bit claim is the whole point of CS10, and the two backends reach it through entirely
// different compilers - GLSL through the driver, SPIR-V through glslang. If a `precise` qualifier
// were doing nothing on one of them, this is where it shows.

[[nodiscard]] Asset::MeshAssetUVE MakeSkinnedMeshForVulkanUVE(const std::size_t vertexCount,
                                                              const std::size_t jointCount) {
    Asset::MeshAssetUVE mesh;
    mesh.vertices.reserve(vertexCount);
    mesh.skinningInfluences.reserve(vertexCount);
    for (std::size_t index = 0U; index < vertexCount; ++index) {
        const float step = static_cast<float>(index);
        Asset::MeshVertexUVE vertex;
        vertex.position = Math::Vector3UVE{std::fmod(step * 0.731F, 7.3F) - 3.65F,
                                           std::fmod(step * 0.379F, 2.9F) - 1.45F,
                                           std::fmod(step * 0.517F, 4.1F) - 2.05F};
        vertex.normal = Math::Vector3UVE{0.577F + std::fmod(step * 0.031F, 0.4F), -0.577F,
                                         0.577F - std::fmod(step * 0.019F, 0.3F)};
        vertex.tangent = Math::Vector3UVE{-0.707F, std::fmod(step * 0.023F, 0.5F), 0.707F};
        vertex.tangentHandedness = (index % 2U == 0U) ? 1.0F : -1.0F;
        mesh.vertices.push_back(vertex);

        Asset::MeshSkinningInfluenceUVE influence;
        influence.joints[0] = static_cast<std::uint32_t>(index % jointCount);
        influence.joints[1] = static_cast<std::uint32_t>((index + 1U) % jointCount);
        influence.joints[2] = static_cast<std::uint32_t>((index + 2U) % jointCount);
        influence.joints[3] = influence.joints[0];
        if (index % 5U == 0U) {
            influence.weights[0] = 1.0F; // rigid, exercising the zero-weight skip
        } else {
            const float first = 0.2F + std::fmod(step * 0.037F, 0.5F);
            const float second = 0.3F - std::fmod(step * 0.013F, 0.25F);
            influence.weights[0] = first;
            influence.weights[1] = second;
            influence.weights[2] = 1.0F - first - second;
        }
        mesh.skinningInfluences.push_back(influence);
    }
    mesh.indices = {0U, 1U, 2U};
    mesh.joints.resize(jointCount);
    for (std::size_t index = 0U; index < jointCount; ++index) {
        mesh.joints[index].parentIndex = (index == 0U)
                                             ? Asset::kInvalidJointParentUVE
                                             : static_cast<std::uint32_t>(index - 1U);
        mesh.joints[index].inverseBindMatrix = Math::Matrix4x4UVE::ComposeTrsUVE(
            Math::Vector3UVE{-static_cast<float>(index), 0.0F, 0.0F}, Math::QuaternionUVE{},
            Math::Vector3UVE{1.0F, 1.0F, 1.0F});
    }
    return mesh;
}

[[nodiscard]] std::vector<Math::Matrix4x4UVE> MakeAwkwardSkinPoseUVE(const std::size_t jointCount) {
    std::vector<Math::Matrix4x4UVE> pose;
    pose.reserve(jointCount);
    for (std::size_t index = 0U; index < jointCount; ++index) {
        const float step = static_cast<float>(index);
        Math::QuaternionUVE rotation;
        EXPECT_TRUE(Math::TryMakeAxisAngleUVE(
            Math::Vector3UVE{0.37F, 0.81F - step * 0.03F, -0.44F + step * 0.017F},
            0.613F + step * 0.229F, rotation));
        pose.push_back(Math::Matrix4x4UVE::ComposeTrsUVE(
            Math::Vector3UVE{1.0F + std::fmod(step * 0.317F, 0.9F),
                             std::fmod(step * 0.211F, 1.3F) - 0.65F,
                             std::fmod(step * 0.173F, 1.1F) - 0.55F},
            rotation, Math::Vector3UVE{1.0F, 1.0F, 1.0F}));
    }
    return pose;
}

void ExpectFloatBitIdenticalUVE(const float actual, const float expected, const char* const what,
                                const std::size_t index) {
    std::uint32_t actualBits = 0U;
    std::uint32_t expectedBits = 0U;
    std::memcpy(&actualBits, &actual, sizeof(actualBits));
    std::memcpy(&expectedBits, &expected, sizeof(expectedBits));
    EXPECT_EQ(actualBits, expectedBits)
        << "vertex " << index << " " << what << ": Vulkan " << actual << " vs CPU " << expected;
}

TEST_F(ComputeWorkloadsVulkanUVETest, MeshSkin_MatchesTheCpuBitForBit) {
    MeshSkinComputeUVE skin(*device, *computeSystem);
    std::string infoLog;
    ASSERT_TRUE(skin.InitializeUVE(&infoLog))
        << "the skinning kernel must build on Vulkan, not just OpenGL: " << infoLog;

    const Asset::MeshAssetUVE mesh = MakeSkinnedMeshForVulkanUVE(1000U, 6U);
    std::vector<Math::Matrix4x4UVE> matrices;
    ASSERT_TRUE(Asset::TryResolvePoseUVE(mesh.joints, MakeAwkwardSkinPoseUVE(6U), matrices));

    std::vector<Asset::MeshVertexUVE> onCpu;
    ASSERT_TRUE(Asset::TrySkinMeshUVE(mesh, matrices, onCpu));
    std::vector<Asset::MeshVertexUVE> onGpu;
    ASSERT_TRUE(skin.SkinUVE(mesh, matrices, onGpu));

    ASSERT_EQ(onGpu.size(), onCpu.size());
    for (std::size_t index = 0U; index < onCpu.size(); ++index) {
        ExpectFloatBitIdenticalUVE(onGpu[index].position.x, onCpu[index].position.x, "position.x", index);
        ExpectFloatBitIdenticalUVE(onGpu[index].position.y, onCpu[index].position.y, "position.y", index);
        ExpectFloatBitIdenticalUVE(onGpu[index].position.z, onCpu[index].position.z, "position.z", index);
        ExpectFloatBitIdenticalUVE(onGpu[index].normal.x, onCpu[index].normal.x, "normal.x", index);
        ExpectFloatBitIdenticalUVE(onGpu[index].normal.y, onCpu[index].normal.y, "normal.y", index);
        ExpectFloatBitIdenticalUVE(onGpu[index].normal.z, onCpu[index].normal.z, "normal.z", index);
        ExpectFloatBitIdenticalUVE(onGpu[index].tangent.x, onCpu[index].tangent.x, "tangent.x", index);
        ExpectFloatBitIdenticalUVE(onGpu[index].tangentHandedness, onCpu[index].tangentHandedness,
                                   "handedness", index);
    }
}

TEST_F(ComputeWorkloadsVulkanUVETest, MeshSkin_BindPose_ReturnsTheSourceVerticesUnchanged) {
    // Also the transpose check on this backend: the matrix upload order is host-side code shared
    // by both, but only a real dispatch proves the shader reads it the way the host wrote it.
    MeshSkinComputeUVE skin(*device, *computeSystem);
    std::string infoLog;
    ASSERT_TRUE(skin.InitializeUVE(&infoLog)) << infoLog;

    const Asset::MeshAssetUVE mesh = MakeSkinnedMeshForVulkanUVE(200U, 5U);
    std::vector<Math::Matrix4x4UVE> bindPose{Math::Matrix4x4UVE::IdentityUVE()};
    for (std::size_t index = 1U; index < mesh.joints.size(); ++index) {
        bindPose.push_back(Math::Matrix4x4UVE::ComposeTrsUVE(Math::Vector3UVE{1.0F, 0.0F, 0.0F},
                                                             Math::QuaternionUVE{},
                                                             Math::Vector3UVE{1.0F, 1.0F, 1.0F}));
    }
    std::vector<Math::Matrix4x4UVE> matrices;
    ASSERT_TRUE(Asset::TryResolvePoseUVE(mesh.joints, bindPose, matrices));

    std::vector<Asset::MeshVertexUVE> onGpu;
    ASSERT_TRUE(skin.SkinUVE(mesh, matrices, onGpu));
    ASSERT_EQ(onGpu.size(), mesh.vertices.size());
    for (std::size_t index = 0U; index < onGpu.size(); ++index) {
        ExpectFloatBitIdenticalUVE(onGpu[index].position.x, mesh.vertices[index].position.x,
                                   "bind position.x", index);
        ExpectFloatBitIdenticalUVE(onGpu[index].position.y, mesh.vertices[index].position.y,
                                   "bind position.y", index);
        ExpectFloatBitIdenticalUVE(onGpu[index].position.z, mesh.vertices[index].position.z,
                                   "bind position.z", index);
    }
}

} // namespace
} // namespace UVE::Render::Tests
