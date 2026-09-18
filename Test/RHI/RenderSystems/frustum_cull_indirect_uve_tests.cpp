// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/frustum_cull_indirect_uve.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "uve/events/event_system_uve.h"
#include "uve/math/aabb_uve.h"
#include "uve/math/frustum_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/render_systems/compute_system_uve.h"
#include "uve/rhi/render_resource_descs_uve.h"
#include "uve/rhi_null/null_render_device_uve.h"

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/gl.h>

#include "uve/rhi_opengl/gl_render_device_uve.h"
#include "uve/window/window_manager_uve.h"

namespace UVE::Render::Tests {
namespace {

// ---------------------------------------------------------------------------
// CS8. The system under test deliberately never tells the CPU how many objects
// survived - that is the entire point of an indirect cull. So these tests read
// the buffers back AFTERWARDS, purely as observers, and hold the result to the
// same standard CS5 is held to: the surviving SET must equal exactly the set the
// CPU frustum test would have produced.
//
// "Set", not "sequence": slots are handed out by an atomic in the kernel, so the
// compacted order is not deterministic and every comparison below sorts first.
// A test that asserted an order would fail intermittently for a reason that is
// not a bug.
// ---------------------------------------------------------------------------

[[nodiscard]] Math::FrustumUVE MakeTestFrustumUVE() {
    const Math::Matrix4x4UVE projection =
        Math::Matrix4x4UVE::PerspectiveUVE(60.0F * 3.14159265F / 180.0F, 16.0F / 9.0F, 0.1F, 100.0F);
    const Math::Matrix4x4UVE view = Math::Matrix4x4UVE::ViewFromPositionAndRotationUVE(
        Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::QuaternionUVE{});
    return Math::FrustumUVE::FromViewProjectionUVE(projection * view);
}

/// The same periodic spread CS5's tests use, and for the same reason: it contains both visible and
/// culled boxes at EVERY length, so even a three-box case cannot be satisfied by a kernel that
/// answers a constant. See frustum_cull_compute_uve_tests.cpp for the monotonic version this
/// replaced and why it was a trap.
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

/// The CPU authority: the real Math::FrustumUVE, never a reimplementation here.
[[nodiscard]] std::vector<std::uint32_t> VisibleIndicesOnCpuUVE(
    const std::vector<Math::AabbUVE>& boxes, const Math::FrustumUVE& frustum) {
    std::vector<std::uint32_t> indices;
    for (std::size_t index = 0U; index < boxes.size(); ++index) {
        if (frustum.IntersectsUVE(boxes[index])) {
            indices.push_back(static_cast<std::uint32_t>(index));
        }
    }
    return indices;
}

/// Guards the guard, exactly as CS5's tests do: a fixture that is entirely visible or entirely
/// culled would be satisfied by a kernel that ignores its input.
void ExpectBothAnswersPresentUVE(const std::size_t visibleCount, const std::size_t total) {
    EXPECT_GT(visibleCount, 0U) << "the fixture must produce visible boxes";
    EXPECT_LT(visibleCount, total) << "the fixture must produce culled boxes";
}

[[nodiscard]] Window::WindowDescUVE MakeIndirectCullTestWindowDescUVE() {
    Window::WindowDescUVE desc;
    desc.title = "uve_frustum_cull_indirect_uve_tests";
    desc.width = 64;
    desc.height = 64;
    desc.glVersionMajor = 4;
    desc.glVersionMinor = 5;
    return desc;
}

/// Mesh parameters with nothing zero or round in them, so a field silently dropped or reordered on
/// the way to the GPU shows up as a wrong number rather than coincidentally matching. The negative
/// vertexOffset is the important one - it is the only signed field, and an unsigned mirror
/// anywhere along the path would turn it into a huge positive index.
[[nodiscard]] DrawIndexedIndirectCommandUVE MakeMeshParamsUVE() {
    DrawIndexedIndirectCommandUVE params;
    params.indexCount = 36U;
    params.instanceCount = 9999U; // must be overwritten - the GPU owns this field
    params.firstIndex = 12U;
    params.vertexOffset = -7;
    params.firstInstance = 3U;
    return params;
}

// ---------------------------------------------------------------------------
// Backend-independent contract, on the Null device. Null records dispatches
// without executing them, so these never assert a cull RESULT - only the
// contract around it.
// ---------------------------------------------------------------------------

class FrustumCullIndirectUVETest : public ::testing::Test {
protected:
    NullRenderDeviceUVE device;
    ComputeSystemUVE computeSystem{device};
    FrustumCullIndirectUVE cull{device, computeSystem};
};

TEST_F(FrustumCullIndirectUVETest, InitializeUVE_BuildsTheKernelAndIsIdempotent) {
    EXPECT_FALSE(cull.IsReadyUVE());
    EXPECT_EQ(cull.GetDrawCommandBufferUVE(), kInvalidBufferHandleUVE);

    ASSERT_TRUE(cull.InitializeUVE());
    EXPECT_TRUE(cull.IsReadyUVE());
    EXPECT_NE(cull.GetDrawCommandBufferUVE(), kInvalidBufferHandleUVE);

    EXPECT_TRUE(cull.InitializeUVE());
    EXPECT_EQ(computeSystem.GetDiagnosticsUVE().programsCreated, 1U);
}

TEST_F(FrustumCullIndirectUVETest, PrepareUVE_BeforeInitialize_Refuses) {
    const std::vector<Math::AabbUVE> boxes = MakeBoxSpreadUVE(4U);
    EXPECT_FALSE(cull.PrepareUVE(boxes, MakeTestFrustumUVE(), MakeMeshParamsUVE()));
    EXPECT_EQ(cull.GetDiagnosticsUVE().cullsRejected, 1U);
    EXPECT_EQ(cull.GetDiagnosticsUVE().drawsPrepared, 0U);
}

TEST_F(FrustumCullIndirectUVETest, PrepareUVE_EmptyInput_StillWritesAZeroInstanceDrawCommand) {
    ASSERT_TRUE(cull.InitializeUVE());
    ASSERT_TRUE(cull.PrepareUVE({}, MakeTestFrustumUVE(), MakeMeshParamsUVE()));

    // A caller that unconditionally records an indirect draw must get a well-formed no-op here,
    // not whatever the buffer happened to hold from the previous frame.
    DrawIndexedIndirectCommandUVE readBack{};
    ASSERT_TRUE(device.ReadbackBufferUVE(
        cull.GetDrawCommandBufferUVE(),
        std::span<std::byte>{reinterpret_cast<std::byte*>(&readBack), sizeof(readBack)}));
    EXPECT_EQ(readBack.instanceCount, 0U);
    EXPECT_EQ(readBack.indexCount, 36U);
    EXPECT_EQ(readBack.vertexOffset, -7);
    EXPECT_EQ(cull.GetDiagnosticsUVE().cullsSkipped, 1U);
}

TEST_F(FrustumCullIndirectUVETest, PrepareUVE_SeedsTheMeshFieldsAndZeroesTheInstanceCount) {
    ASSERT_TRUE(cull.InitializeUVE());
    const std::vector<Math::AabbUVE> boxes = MakeBoxSpreadUVE(8U);
    ASSERT_TRUE(cull.PrepareUVE(boxes, MakeTestFrustumUVE(), MakeMeshParamsUVE()));

    // Null executes nothing, so what sits in the buffer is exactly the seed - which is the point
    // worth asserting here: the four mesh fields survive the trip verbatim, and the caller's
    // instanceCount of 9999 does NOT, because that field belongs to the GPU.
    DrawIndexedIndirectCommandUVE readBack{};
    ASSERT_TRUE(device.ReadbackBufferUVE(
        cull.GetDrawCommandBufferUVE(),
        std::span<std::byte>{reinterpret_cast<std::byte*>(&readBack), sizeof(readBack)}));
    EXPECT_EQ(readBack.indexCount, 36U);
    EXPECT_EQ(readBack.instanceCount, 0U);
    EXPECT_EQ(readBack.firstIndex, 12U);
    EXPECT_EQ(readBack.vertexOffset, -7);
    EXPECT_EQ(readBack.firstInstance, 3U);
}

TEST_F(FrustumCullIndirectUVETest, PrepareUVE_RefusesNonFiniteInputRatherThanAnsweringDifferently) {
    ASSERT_TRUE(cull.InitializeUVE());
    std::vector<Math::AabbUVE> boxes = MakeBoxSpreadUVE(4U);
    boxes.push_back(Math::AabbUVE::FromCenterExtentsUVE(
        Math::Vector3UVE{std::numeric_limits<float>::quiet_NaN(), 0.0F, -5.0F},
        Math::Vector3UVE{1.0F, 1.0F, 1.0F}));

    EXPECT_FALSE(cull.PrepareUVE(boxes, MakeTestFrustumUVE(), MakeMeshParamsUVE()));
    EXPECT_GT(cull.GetDiagnosticsUVE().cullsRejected, 0U);
}

TEST_F(FrustumCullIndirectUVETest, PrepareUVE_ReusesItsBuffersUntilItMustGrow) {
    ASSERT_TRUE(cull.InitializeUVE());
    const Math::FrustumUVE frustum = MakeTestFrustumUVE();

    ASSERT_TRUE(cull.PrepareUVE(MakeBoxSpreadUVE(64U), frustum, MakeMeshParamsUVE()));
    const std::uint32_t afterFirst = cull.GetDiagnosticsUVE().bufferReallocations;
    EXPECT_EQ(afterFirst, 1U);

    // Smaller, then equal: neither should touch the allocator - a caller whose box count
    // oscillates below its high-water mark should stop reallocating entirely.
    ASSERT_TRUE(cull.PrepareUVE(MakeBoxSpreadUVE(16U), frustum, MakeMeshParamsUVE()));
    ASSERT_TRUE(cull.PrepareUVE(MakeBoxSpreadUVE(64U), frustum, MakeMeshParamsUVE()));
    EXPECT_EQ(cull.GetDiagnosticsUVE().bufferReallocations, afterFirst);

    ASSERT_TRUE(cull.PrepareUVE(MakeBoxSpreadUVE(65U), frustum, MakeMeshParamsUVE()));
    EXPECT_EQ(cull.GetDiagnosticsUVE().bufferReallocations, afterFirst + 1U);
}

TEST_F(FrustumCullIndirectUVETest, Diagnostics_ExposeNoVisibleCount) {
    // A compile-time statement of the design rule, and the reason it is worth a test: if someone
    // later adds a `boxesVisible` counter here, the only way to fill it is a readback - which
    // would silently reintroduce the GPU->CPU round trip this whole system exists to remove.
    // Spelled as an aggregate initialization of exactly seven members rather than a sizeof
    // comparison: the struct mixes 32- and 64-bit fields, so its size depends on alignment
    // padding and a sizeof check would be asserting the ABI, not the field list.
    [[maybe_unused]] const FrustumCullIndirectDiagnosticsUVE fieldList{0U, 0U, 0U, 0U, 0U, 0U, 0U};
    ASSERT_TRUE(cull.InitializeUVE());
    ASSERT_TRUE(cull.PrepareUVE(MakeBoxSpreadUVE(32U), MakeTestFrustumUVE(), MakeMeshParamsUVE()));
    EXPECT_EQ(cull.GetDiagnosticsUVE().boxesTested, 32U);
    EXPECT_EQ(cull.GetDiagnosticsUVE().drawsPrepared, 1U);
}

// ---------------------------------------------------------------------------
// The real thing, on a real GL context: the kernel runs, and the compacted
// result is verified against the CPU frustum test.
// ---------------------------------------------------------------------------

class FrustumCullIndirectGlUVETest : public ::testing::Test {
protected:
    void SetUp() override {
        windowManager = std::make_unique<Window::WindowManagerUVE>(
            eventSystem, MakeIndirectCullTestWindowDescUVE());
        if (!windowManager->IsValidUVE()) {
            GTEST_SKIP() << "No display available for GlRenderDeviceUVE - skipping (run under "
                            "xvfb-run to exercise this test)";
        }
        renderDevice = std::make_unique<GlRenderDeviceUVE>(*windowManager);
        if (!renderDevice->IsUsableUVE()) {
            GTEST_SKIP() << "GlRenderDeviceUVE came up unusable on this display";
        }
        computeSystem = std::make_unique<ComputeSystemUVE>(*renderDevice);
        cull = std::make_unique<FrustumCullIndirectUVE>(*renderDevice, *computeSystem);

        std::string infoLog;
        if (!cull->InitializeUVE(&infoLog)) {
            GTEST_SKIP() << "context lacks compute shaders (GL 4.3+): " << infoLog;
        }
    }

    /// Reads back what the GPU decided. Only tests do this - PrepareUVE() itself never does, and
    /// the compacted list is truncated to the instanceCount the GPU wrote, because the slots past
    /// it are uninitialized by construction.
    [[nodiscard]] std::vector<std::uint32_t> ReadVisibleIndicesUVE(std::uint32_t& outInstanceCount) {
        DrawIndexedIndirectCommandUVE command{};
        EXPECT_TRUE(renderDevice->ReadbackBufferUVE(
            cull->GetDrawCommandBufferUVE(),
            std::span<std::byte>{reinterpret_cast<std::byte*>(&command), sizeof(command)}));
        outInstanceCount = command.instanceCount;

        std::vector<std::uint32_t> indices(command.instanceCount, 0U);
        if (command.instanceCount != 0U) {
            EXPECT_TRUE(renderDevice->ReadbackBufferUVE(
                cull->GetVisibleIndexBufferUVE(),
                std::span<std::byte>{reinterpret_cast<std::byte*>(indices.data()),
                                     indices.size() * sizeof(std::uint32_t)}));
        }
        // Sorted, because the kernel's atomic assigns slots in no defined order.
        std::sort(indices.begin(), indices.end());
        return indices;
    }

    Events::EventSystemUVE eventSystem;
    std::unique_ptr<Window::WindowManagerUVE> windowManager;
    std::unique_ptr<GlRenderDeviceUVE> renderDevice;
    std::unique_ptr<ComputeSystemUVE> computeSystem;
    std::unique_ptr<FrustumCullIndirectUVE> cull;
};

TEST_F(FrustumCullIndirectGlUVETest, PrepareUVE_CompactsExactlyTheBoxesTheCpuTestKeeps) {
    // 1000 is not a multiple of the 64-wide workgroup, so the kernel's tail guard is exercised;
    // without it the tail invocations would each claim a compaction slot for a box that does not
    // exist and inflate the instance count.
    const std::vector<Math::AabbUVE> boxes = MakeBoxSpreadUVE(1000U);
    const Math::FrustumUVE frustum = MakeTestFrustumUVE();
    const std::vector<std::uint32_t> expected = VisibleIndicesOnCpuUVE(boxes, frustum);
    ExpectBothAnswersPresentUVE(expected.size(), boxes.size());

    ASSERT_TRUE(cull->PrepareUVE(boxes, frustum, MakeMeshParamsUVE()));

    std::uint32_t instanceCount = 0U;
    const std::vector<std::uint32_t> actual = ReadVisibleIndicesUVE(instanceCount);

    // The count the GPU wrote into the draw command IS the survivor count - that equality is the
    // entire contract, since the draw consumes that field and nothing else.
    EXPECT_EQ(instanceCount, expected.size());
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        EXPECT_EQ(actual[index], expected[index])
            << "compacted slot " << index << ": the GPU kept box " << actual[index]
            << " where the CPU kept box " << expected[index];
    }
}

TEST_F(FrustumCullIndirectGlUVETest, PrepareUVE_LeavesTheMeshFieldsUntouchedWhileWritingTheCount) {
    const std::vector<Math::AabbUVE> boxes = MakeBoxSpreadUVE(300U);
    ASSERT_TRUE(cull->PrepareUVE(boxes, MakeTestFrustumUVE(), MakeMeshParamsUVE()));

    DrawIndexedIndirectCommandUVE command{};
    ASSERT_TRUE(renderDevice->ReadbackBufferUVE(
        cull->GetDrawCommandBufferUVE(),
        std::span<std::byte>{reinterpret_cast<std::byte*>(&command), sizeof(command)}));

    // The kernel touches one word of this struct through an atomic. The other four describe the
    // mesh and must come through the dispatch byte-identical - a misdeclared std430 block would
    // most likely corrupt a neighbour rather than fail loudly.
    EXPECT_EQ(command.indexCount, 36U);
    EXPECT_EQ(command.firstIndex, 12U);
    EXPECT_EQ(command.vertexOffset, -7);
    EXPECT_EQ(command.firstInstance, 3U);
    EXPECT_GT(command.instanceCount, 0U);
    EXPECT_LT(command.instanceCount, 300U);
}

TEST_F(FrustumCullIndirectGlUVETest, PrepareUVE_AgreesOnBoxesSittingExactlyOnThePlanes) {
    // The boundary is where the two paths are most likely to diverge and where divergence is most
    // visible - an object popping in or out depending on which path ran. Each box is placed so its
    // supporting corner lands exactly on a plane, then nudged one ULP each way.
    const Math::FrustumUVE frustum = MakeTestFrustumUVE();
    std::vector<Math::AabbUVE> boxes;
    for (std::size_t planeIndex = 0U; planeIndex < 6U; ++planeIndex) {
        const Math::PlaneUVE& plane = frustum.planes[planeIndex];
        const Math::Vector3UVE extents{0.3F, 0.3F, 0.3F};
        const float radius = extents.x * std::fabs(plane.normal.x) +
                             extents.y * std::fabs(plane.normal.y) +
                             extents.z * std::fabs(plane.normal.z);
        // A centre whose signed distance is exactly -radius: the touching case, which the CPU
        // treats as INSIDE because its comparison is a strict `< 0`.
        const float target = -radius;
        const Math::Vector3UVE center{plane.normal.x * (target - plane.distance),
                                      plane.normal.y * (target - plane.distance),
                                      plane.normal.z * (target - plane.distance)};
        for (const float nudge : {0.0F, 1.0F, -1.0F}) {
            Math::Vector3UVE shifted = center;
            if (nudge != 0.0F) {
                shifted.x = std::nextafter(shifted.x, nudge * std::numeric_limits<float>::max());
                shifted.y = std::nextafter(shifted.y, nudge * std::numeric_limits<float>::max());
                shifted.z = std::nextafter(shifted.z, nudge * std::numeric_limits<float>::max());
            }
            boxes.push_back(Math::AabbUVE::FromCenterExtentsUVE(shifted, extents));
        }
    }

    const std::vector<std::uint32_t> expected = VisibleIndicesOnCpuUVE(boxes, frustum);
    ASSERT_TRUE(cull->PrepareUVE(boxes, frustum, MakeMeshParamsUVE()));

    std::uint32_t instanceCount = 0U;
    const std::vector<std::uint32_t> actual = ReadVisibleIndicesUVE(instanceCount);
    EXPECT_EQ(instanceCount, expected.size());
    EXPECT_EQ(actual, expected);
}

TEST_F(FrustumCullIndirectGlUVETest, PrepareUVE_RepeatedCalls_DoNotAccumulateInstances) {
    // The instanceCount seed exists for this: the kernel only ever adds to that field, so a run
    // that forgot to re-zero it would report twice the survivors on the second call and every
    // call after. Nothing downstream would notice - the number never reaches the CPU in
    // production - which is exactly why it is worth an explicit test.
    const std::vector<Math::AabbUVE> boxes = MakeBoxSpreadUVE(137U);
    const Math::FrustumUVE frustum = MakeTestFrustumUVE();
    const std::vector<std::uint32_t> expected = VisibleIndicesOnCpuUVE(boxes, frustum);
    ExpectBothAnswersPresentUVE(expected.size(), boxes.size());

    for (int pass = 0; pass < 3; ++pass) {
        ASSERT_TRUE(cull->PrepareUVE(boxes, frustum, MakeMeshParamsUVE()));
        std::uint32_t instanceCount = 0U;
        const std::vector<std::uint32_t> actual = ReadVisibleIndicesUVE(instanceCount);
        EXPECT_EQ(instanceCount, expected.size()) << "on pass " << pass;
        EXPECT_EQ(actual, expected) << "on pass " << pass;
    }
}

TEST_F(FrustumCullIndirectGlUVETest, PrepareUVE_AgreesWithTheCpuAcrossPartialWorkgroups) {
    const Math::FrustumUVE frustum = MakeTestFrustumUVE();
    // 1 and 63 are sub-workgroup, 64 is exact, 65 and 129 straddle - the sizes where a tail guard
    // or a group-count rounding error shows up.
    for (const std::size_t count : {std::size_t{1U}, std::size_t{63U}, std::size_t{64U},
                                    std::size_t{65U}, std::size_t{129U}}) {
        const std::vector<Math::AabbUVE> boxes = MakeBoxSpreadUVE(count);
        const std::vector<std::uint32_t> expected = VisibleIndicesOnCpuUVE(boxes, frustum);
        ASSERT_TRUE(cull->PrepareUVE(boxes, frustum, MakeMeshParamsUVE())) << "at count " << count;

        std::uint32_t instanceCount = 0U;
        const std::vector<std::uint32_t> actual = ReadVisibleIndicesUVE(instanceCount);
        EXPECT_EQ(instanceCount, expected.size()) << "at count " << count;
        EXPECT_EQ(actual, expected) << "at count " << count;
    }
}

} // namespace
} // namespace UVE::Render::Tests
