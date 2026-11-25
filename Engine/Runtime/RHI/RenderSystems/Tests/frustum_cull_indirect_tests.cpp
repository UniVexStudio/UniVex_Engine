// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "frustum_cull_indirect.h"

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

#include "event_system.h"
#include "aabb.h"
#include "frustum.h"
#include "matrix4x4.h"
#include "quaternion.h"
#include "vector3.h"
#include "compute_system.h"
#include "render_resource_descs.h"
#include "null_render_device.h"

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/gl.h>

#include "gl_render_device.h"
#include "window_manager.h"

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

[[nodiscard]] Math::Frustum MakeTestFrustum() {
    const Math::Matrix4x4 projection =
        Math::Matrix4x4::Perspective(60.0F * 3.14159265F / 180.0F, 16.0F / 9.0F, 0.1F, 100.0F);
    const Math::Matrix4x4 view = Math::Matrix4x4::ViewFromPositionAndRotation(
        Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Quaternion{});
    return Math::Frustum::FromViewProjection(projection * view);
}

/// The same periodic spread CS5's tests use, and for the same reason: it contains both visible and
/// culled boxes at EVERY length, so even a three-box case cannot be satisfied by a kernel that
/// answers a constant. See frustum_cull_compute_uve_tests.cpp for the monotonic version this
/// replaced and why it was a trap.
[[nodiscard]] std::vector<Math::Aabb> MakeBoxSpread(const std::size_t count) {
    std::vector<Math::Aabb> boxes;
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
        boxes.push_back(Math::Aabb::FromCenterExtents(
            Math::Vector3{lateral, vertical, depth},
            Math::Vector3{halfSize, halfSize, halfSize}));
    }
    return boxes;
}

/// The CPU authority: the real Math::Frustum, never a reimplementation here.
[[nodiscard]] std::vector<std::uint32_t> VisibleIndicesOnCpu(
    const std::vector<Math::Aabb>& boxes, const Math::Frustum& frustum) {
    std::vector<std::uint32_t> indices;
    for (std::size_t index = 0U; index < boxes.size(); ++index) {
        if (frustum.Intersects(boxes[index])) {
            indices.push_back(static_cast<std::uint32_t>(index));
        }
    }
    return indices;
}

/// Guards the guard, exactly as CS5's tests do: a fixture that is entirely visible or entirely
/// culled would be satisfied by a kernel that ignores its input.
void ExpectBothAnswersPresent(const std::size_t visibleCount, const std::size_t total) {
    EXPECT_GT(visibleCount, 0U) << "the fixture must produce visible boxes";
    EXPECT_LT(visibleCount, total) << "the fixture must produce culled boxes";
}

[[nodiscard]] Window::WindowDesc MakeIndirectCullTestWindowDesc() {
    Window::WindowDesc desc;
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
[[nodiscard]] DrawIndexedIndirectCommand MakeMeshParams() {
    DrawIndexedIndirectCommand params;
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

class FrustumCullIndirectTest : public ::testing::Test {
protected:
    NullRenderDevice device;
    ComputeSystem computeSystem{device};
    FrustumCullIndirect cull{device, computeSystem};
};

TEST_F(FrustumCullIndirectTest, Initialize_BuildsTheKernelAndIsIdempotent) {
    EXPECT_FALSE(cull.IsReady());
    EXPECT_EQ(cull.GetDrawCommandBuffer(), kInvalidBufferHandle);

    ASSERT_TRUE(cull.Initialize());
    EXPECT_TRUE(cull.IsReady());
    EXPECT_NE(cull.GetDrawCommandBuffer(), kInvalidBufferHandle);

    EXPECT_TRUE(cull.Initialize());
    EXPECT_EQ(computeSystem.GetDiagnostics().programsCreated, 1U);
}

TEST_F(FrustumCullIndirectTest, Prepare_BeforeInitialize_Refuses) {
    const std::vector<Math::Aabb> boxes = MakeBoxSpread(4U);
    EXPECT_FALSE(cull.Prepare(boxes, MakeTestFrustum(), MakeMeshParams()));
    EXPECT_EQ(cull.GetDiagnostics().cullsRejected, 1U);
    EXPECT_EQ(cull.GetDiagnostics().drawsPrepared, 0U);
}

TEST_F(FrustumCullIndirectTest, Prepare_EmptyInput_StillWritesAZeroInstanceDrawCommand) {
    ASSERT_TRUE(cull.Initialize());
    ASSERT_TRUE(cull.Prepare({}, MakeTestFrustum(), MakeMeshParams()));

    // A caller that unconditionally records an indirect draw must get a well-formed no-op here,
    // not whatever the buffer happened to hold from the previous frame.
    DrawIndexedIndirectCommand readBack{};
    ASSERT_TRUE(device.ReadbackBuffer(
        cull.GetDrawCommandBuffer(),
        std::span<std::byte>{reinterpret_cast<std::byte*>(&readBack), sizeof(readBack)}));
    EXPECT_EQ(readBack.instanceCount, 0U);
    EXPECT_EQ(readBack.indexCount, 36U);
    EXPECT_EQ(readBack.vertexOffset, -7);
    EXPECT_EQ(cull.GetDiagnostics().cullsSkipped, 1U);
}

TEST_F(FrustumCullIndirectTest, Prepare_SeedsTheMeshFieldsAndZeroesTheInstanceCount) {
    ASSERT_TRUE(cull.Initialize());
    const std::vector<Math::Aabb> boxes = MakeBoxSpread(8U);
    ASSERT_TRUE(cull.Prepare(boxes, MakeTestFrustum(), MakeMeshParams()));

    // Null executes nothing, so what sits in the buffer is exactly the seed - which is the point
    // worth asserting here: the four mesh fields survive the trip verbatim, and the caller's
    // instanceCount of 9999 does NOT, because that field belongs to the GPU.
    DrawIndexedIndirectCommand readBack{};
    ASSERT_TRUE(device.ReadbackBuffer(
        cull.GetDrawCommandBuffer(),
        std::span<std::byte>{reinterpret_cast<std::byte*>(&readBack), sizeof(readBack)}));
    EXPECT_EQ(readBack.indexCount, 36U);
    EXPECT_EQ(readBack.instanceCount, 0U);
    EXPECT_EQ(readBack.firstIndex, 12U);
    EXPECT_EQ(readBack.vertexOffset, -7);
    EXPECT_EQ(readBack.firstInstance, 3U);
}

TEST_F(FrustumCullIndirectTest, Prepare_RefusesNonFiniteInputRatherThanAnsweringDifferently) {
    ASSERT_TRUE(cull.Initialize());
    std::vector<Math::Aabb> boxes = MakeBoxSpread(4U);
    boxes.push_back(Math::Aabb::FromCenterExtents(
        Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, -5.0F},
        Math::Vector3{1.0F, 1.0F, 1.0F}));

    EXPECT_FALSE(cull.Prepare(boxes, MakeTestFrustum(), MakeMeshParams()));
    EXPECT_GT(cull.GetDiagnostics().cullsRejected, 0U);
}

TEST_F(FrustumCullIndirectTest, Prepare_ReusesItsBuffersUntilItMustGrow) {
    ASSERT_TRUE(cull.Initialize());
    const Math::Frustum frustum = MakeTestFrustum();

    ASSERT_TRUE(cull.Prepare(MakeBoxSpread(64U), frustum, MakeMeshParams()));
    const std::uint32_t afterFirst = cull.GetDiagnostics().bufferReallocations;
    EXPECT_EQ(afterFirst, 1U);

    // Smaller, then equal: neither should touch the allocator - a caller whose box count
    // oscillates below its high-water mark should stop reallocating entirely.
    ASSERT_TRUE(cull.Prepare(MakeBoxSpread(16U), frustum, MakeMeshParams()));
    ASSERT_TRUE(cull.Prepare(MakeBoxSpread(64U), frustum, MakeMeshParams()));
    EXPECT_EQ(cull.GetDiagnostics().bufferReallocations, afterFirst);

    ASSERT_TRUE(cull.Prepare(MakeBoxSpread(65U), frustum, MakeMeshParams()));
    EXPECT_EQ(cull.GetDiagnostics().bufferReallocations, afterFirst + 1U);
}

TEST_F(FrustumCullIndirectTest, Diagnostics_ExposeNoVisibleCount) {
    // A compile-time statement of the design rule, and the reason it is worth a test: if someone
    // later adds a `boxesVisible` counter here, the only way to fill it is a readback - which
    // would silently reintroduce the GPU->CPU round trip this whole system exists to remove.
    // Spelled as an aggregate initialization of exactly seven members rather than a sizeof
    // comparison: the struct mixes 32- and 64-bit fields, so its size depends on alignment
    // padding and a sizeof check would be asserting the ABI, not the field list.
    [[maybe_unused]] const FrustumCullIndirectDiagnostics fieldList{0U, 0U, 0U, 0U, 0U, 0U, 0U};
    ASSERT_TRUE(cull.Initialize());
    ASSERT_TRUE(cull.Prepare(MakeBoxSpread(32U), MakeTestFrustum(), MakeMeshParams()));
    EXPECT_EQ(cull.GetDiagnostics().boxesTested, 32U);
    EXPECT_EQ(cull.GetDiagnostics().drawsPrepared, 1U);
}

// ---------------------------------------------------------------------------
// The real thing, on a real GL context: the kernel runs, and the compacted
// result is verified against the CPU frustum test.
// ---------------------------------------------------------------------------

class FrustumCullIndirectGlTest : public ::testing::Test {
protected:
    void SetUp() override {
        windowManager = std::make_unique<Window::WindowManager>(
            eventSystem, MakeIndirectCullTestWindowDesc());
        if (!windowManager->IsValid()) {
            GTEST_SKIP() << "No display available for GlRenderDevice - skipping (run under "
                            "xvfb-run to exercise this test)";
        }
        renderDevice = std::make_unique<GlRenderDevice>(*windowManager);
        if (!renderDevice->IsUsable()) {
            GTEST_SKIP() << "GlRenderDevice came up unusable on this display";
        }
        computeSystem = std::make_unique<ComputeSystem>(*renderDevice);
        cull = std::make_unique<FrustumCullIndirect>(*renderDevice, *computeSystem);

        std::string infoLog;
        if (!cull->Initialize(&infoLog)) {
            GTEST_SKIP() << "context lacks compute shaders (GL 4.3+): " << infoLog;
        }
    }

    /// Reads back what the GPU decided. Only tests do this - Prepare() itself never does, and
    /// the compacted list is truncated to the instanceCount the GPU wrote, because the slots past
    /// it are uninitialized by construction.
    [[nodiscard]] std::vector<std::uint32_t> ReadVisibleIndices(std::uint32_t& outInstanceCount) {
        DrawIndexedIndirectCommand command{};
        EXPECT_TRUE(renderDevice->ReadbackBuffer(
            cull->GetDrawCommandBuffer(),
            std::span<std::byte>{reinterpret_cast<std::byte*>(&command), sizeof(command)}));
        outInstanceCount = command.instanceCount;

        std::vector<std::uint32_t> indices(command.instanceCount, 0U);
        if (command.instanceCount != 0U) {
            EXPECT_TRUE(renderDevice->ReadbackBuffer(
                cull->GetVisibleIndexBuffer(),
                std::span<std::byte>{reinterpret_cast<std::byte*>(indices.data()),
                                     indices.size() * sizeof(std::uint32_t)}));
        }
        // Sorted, because the kernel's atomic assigns slots in no defined order.
        std::sort(indices.begin(), indices.end());
        return indices;
    }

    Events::EventSystem eventSystem;
    std::unique_ptr<Window::WindowManager> windowManager;
    std::unique_ptr<GlRenderDevice> renderDevice;
    std::unique_ptr<ComputeSystem> computeSystem;
    std::unique_ptr<FrustumCullIndirect> cull;
};

TEST_F(FrustumCullIndirectGlTest, Prepare_CompactsExactlyTheBoxesTheCpuTestKeeps) {
    // 1000 is not a multiple of the 64-wide workgroup, so the kernel's tail guard is exercised;
    // without it the tail invocations would each claim a compaction slot for a box that does not
    // exist and inflate the instance count.
    const std::vector<Math::Aabb> boxes = MakeBoxSpread(1000U);
    const Math::Frustum frustum = MakeTestFrustum();
    const std::vector<std::uint32_t> expected = VisibleIndicesOnCpu(boxes, frustum);
    ExpectBothAnswersPresent(expected.size(), boxes.size());

    ASSERT_TRUE(cull->Prepare(boxes, frustum, MakeMeshParams()));

    std::uint32_t instanceCount = 0U;
    const std::vector<std::uint32_t> actual = ReadVisibleIndices(instanceCount);

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

TEST_F(FrustumCullIndirectGlTest, Prepare_LeavesTheMeshFieldsUntouchedWhileWritingTheCount) {
    const std::vector<Math::Aabb> boxes = MakeBoxSpread(300U);
    ASSERT_TRUE(cull->Prepare(boxes, MakeTestFrustum(), MakeMeshParams()));

    DrawIndexedIndirectCommand command{};
    ASSERT_TRUE(renderDevice->ReadbackBuffer(
        cull->GetDrawCommandBuffer(),
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

TEST_F(FrustumCullIndirectGlTest, Prepare_AgreesOnBoxesSittingExactlyOnThePlanes) {
    // The boundary is where the two paths are most likely to diverge and where divergence is most
    // visible - an object popping in or out depending on which path ran. Each box is placed so its
    // supporting corner lands exactly on a plane, then nudged one ULP each way.
    const Math::Frustum frustum = MakeTestFrustum();
    std::vector<Math::Aabb> boxes;
    for (std::size_t planeIndex = 0U; planeIndex < 6U; ++planeIndex) {
        const Math::Plane& plane = frustum.planes[planeIndex];
        const Math::Vector3 extents{0.3F, 0.3F, 0.3F};
        const float radius = extents.x * std::fabs(plane.normal.x) +
                             extents.y * std::fabs(plane.normal.y) +
                             extents.z * std::fabs(plane.normal.z);
        // A centre whose signed distance is exactly -radius: the touching case, which the CPU
        // treats as INSIDE because its comparison is a strict `< 0`.
        const float target = -radius;
        const Math::Vector3 center{plane.normal.x * (target - plane.distance),
                                      plane.normal.y * (target - plane.distance),
                                      plane.normal.z * (target - plane.distance)};
        for (const float nudge : {0.0F, 1.0F, -1.0F}) {
            Math::Vector3 shifted = center;
            if (nudge != 0.0F) {
                shifted.x = std::nextafter(shifted.x, nudge * std::numeric_limits<float>::max());
                shifted.y = std::nextafter(shifted.y, nudge * std::numeric_limits<float>::max());
                shifted.z = std::nextafter(shifted.z, nudge * std::numeric_limits<float>::max());
            }
            boxes.push_back(Math::Aabb::FromCenterExtents(shifted, extents));
        }
    }

    const std::vector<std::uint32_t> expected = VisibleIndicesOnCpu(boxes, frustum);
    ASSERT_TRUE(cull->Prepare(boxes, frustum, MakeMeshParams()));

    std::uint32_t instanceCount = 0U;
    const std::vector<std::uint32_t> actual = ReadVisibleIndices(instanceCount);
    EXPECT_EQ(instanceCount, expected.size());
    EXPECT_EQ(actual, expected);
}

TEST_F(FrustumCullIndirectGlTest, Prepare_RepeatedCalls_DoNotAccumulateInstances) {
    // The instanceCount seed exists for this: the kernel only ever adds to that field, so a run
    // that forgot to re-zero it would report twice the survivors on the second call and every
    // call after. Nothing downstream would notice - the number never reaches the CPU in
    // production - which is exactly why it is worth an explicit test.
    const std::vector<Math::Aabb> boxes = MakeBoxSpread(137U);
    const Math::Frustum frustum = MakeTestFrustum();
    const std::vector<std::uint32_t> expected = VisibleIndicesOnCpu(boxes, frustum);
    ExpectBothAnswersPresent(expected.size(), boxes.size());

    for (int pass = 0; pass < 3; ++pass) {
        ASSERT_TRUE(cull->Prepare(boxes, frustum, MakeMeshParams()));
        std::uint32_t instanceCount = 0U;
        const std::vector<std::uint32_t> actual = ReadVisibleIndices(instanceCount);
        EXPECT_EQ(instanceCount, expected.size()) << "on pass " << pass;
        EXPECT_EQ(actual, expected) << "on pass " << pass;
    }
}

TEST_F(FrustumCullIndirectGlTest, Prepare_AgreesWithTheCpuAcrossPartialWorkgroups) {
    const Math::Frustum frustum = MakeTestFrustum();
    // 1 and 63 are sub-workgroup, 64 is exact, 65 and 129 straddle - the sizes where a tail guard
    // or a group-count rounding error shows up.
    for (const std::size_t count : {std::size_t{1U}, std::size_t{63U}, std::size_t{64U},
                                    std::size_t{65U}, std::size_t{129U}}) {
        const std::vector<Math::Aabb> boxes = MakeBoxSpread(count);
        const std::vector<std::uint32_t> expected = VisibleIndicesOnCpu(boxes, frustum);
        ASSERT_TRUE(cull->Prepare(boxes, frustum, MakeMeshParams())) << "at count " << count;

        std::uint32_t instanceCount = 0U;
        const std::vector<std::uint32_t> actual = ReadVisibleIndices(instanceCount);
        EXPECT_EQ(instanceCount, expected.size()) << "at count " << count;
        EXPECT_EQ(actual, expected) << "at count " << count;
    }
}

} // namespace
} // namespace UVE::Render::Tests
