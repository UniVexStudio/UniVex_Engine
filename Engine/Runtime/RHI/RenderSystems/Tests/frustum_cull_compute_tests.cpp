// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "frustum_cull_compute.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
#include "null_render_device.h"

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/gl.h>

#include "gl_render_device.h"
#include "window_manager.h"

namespace UVE::Render::Tests {
namespace {

/// A camera looking down -Z from the origin, the same shape Renderer3D builds: 60 degree
/// vertical field of view, 16:9, near 0.1, far 100.
[[nodiscard]] Math::Frustum MakeTestFrustum() {
    const Math::Matrix4x4 projection =
        Math::Matrix4x4::Perspective(60.0F * 3.14159265F / 180.0F, 16.0F / 9.0F, 0.1F, 100.0F);
    // An unrotated camera at the origin already looks down -Z in this matrix convention, which is
    // what the box spread below is built around.
    const Math::Matrix4x4 view = Math::Matrix4x4::ViewFromPositionAndRotation(
        Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Quaternion{});
    return Math::Frustum::FromViewProjection(projection * view);
}

/// A spread of boxes that straddles the frustum AT EVERY LENGTH, not just in aggregate.
///
/// The first version of this fixture swept steadily from outside to inside, which looked fine at
/// 1000 boxes (711 visible, 289 culled) but was entirely culled for the first 23 - so the small-N
/// tests below would have passed against a kernel that unconditionally answered "culled". A
/// probe against the real Math::Frustum found that; the pattern is now periodic instead, with
/// every third box pushed far outside a side plane and the side alternating, so even three boxes
/// contain both answers. Positions use offsets with no exact binary representation, since round
/// numbers would agree even through a contracted multiply-add.
[[nodiscard]] std::vector<Math::Aabb> MakeBoxSpread(const std::size_t count) {
    std::vector<Math::Aabb> boxes;
    boxes.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const float step = static_cast<float>(index);
        const float halfSize = 0.25F + std::fmod(step * 0.031F, 0.5F);
        // Depth wanders the length of the frustum without ever leaving the 0.1 .. 100 range, so
        // the near and far planes are not what decides these boxes - the side planes are.
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

/// The CPU authority: the real Math::Frustum::Intersects, not a reimplementation of it here.
[[nodiscard]] std::vector<bool> CullOnCpu(const std::vector<Math::Aabb>& boxes,
                                             const Math::Frustum& frustum) {
    std::vector<bool> visible;
    visible.reserve(boxes.size());
    for (const Math::Aabb& box : boxes) {
        visible.push_back(frustum.Intersects(box));
    }
    return visible;
}

/// Guards the guard: a comparison against a set that is entirely visible - or entirely culled -
/// would pass against a kernel that returns a constant. Every GL test below asserts this on its
/// own input before trusting the agreement it just checked.
void ExpectBothAnswersPresent(const std::vector<bool>& visibility) {
    const std::size_t visibleCount =
        static_cast<std::size_t>(std::count(visibility.begin(), visibility.end(), true));
    EXPECT_GT(visibleCount, 0U) << "the fixture must produce visible boxes";
    EXPECT_LT(visibleCount, visibility.size()) << "the fixture must produce culled boxes";
}

/// Reports the first disagreement in full rather than just failing the vector comparison - a bare
/// "vectors differ" on a thousand boxes is not something anyone can debug.
void ExpectVisibilityIdentical(const std::vector<bool>& actual, const std::vector<bool>& expected,
                                  const std::vector<Math::Aabb>& boxes) {
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        EXPECT_EQ(actual[index], expected[index])
            << "box " << index << " " << Math::ToString(boxes[index])
            << ": the GPU said " << (actual[index] ? "visible" : "culled") << " and the CPU said "
            << (expected[index] ? "visible" : "culled");
    }
}

// ---------------------------------------------------------------------------
// Backend-independent contract, on the Null device. Null records dispatches
// without executing them, so these never assert visibility VALUES - only the
// contract around them.
// ---------------------------------------------------------------------------

class FrustumCullComputeTest : public ::testing::Test {
protected:
    NullRenderDevice device;
    ComputeSystem computeSystem{device};
    FrustumCullCompute cull{device, computeSystem};
};

TEST_F(FrustumCullComputeTest, Initialize_BuildsTheKernelAndIsIdempotent) {
    EXPECT_FALSE(cull.IsReady());
    ASSERT_TRUE(cull.Initialize());
    EXPECT_TRUE(cull.IsReady());

    EXPECT_TRUE(cull.Initialize());
    EXPECT_EQ(computeSystem.GetDiagnostics().programsCreated, 1U);
}

TEST_F(FrustumCullComputeTest, Cull_BeforeInitialize_RefusesAndLeavesTheOutputAlone) {
    const std::vector<Math::Aabb> boxes = MakeBoxSpread(4U);
    std::vector<bool> visible{true, true};

    EXPECT_FALSE(cull.Cull(boxes, MakeTestFrustum(), visible));

    EXPECT_EQ(visible, (std::vector<bool>{true, true})) << "a refused cull must not touch the output";
    EXPECT_EQ(cull.GetDiagnostics().cullsRejected, 1U);
}

TEST_F(FrustumCullComputeTest, Cull_EmptyInput_IsASuccessfulNoOp) {
    ASSERT_TRUE(cull.Initialize());
    std::vector<bool> visible{true, false, true};

    EXPECT_TRUE(cull.Cull({}, MakeTestFrustum(), visible));

    EXPECT_TRUE(visible.empty());
    EXPECT_EQ(cull.GetDiagnostics().cullsSkipped, 1U);
    EXPECT_EQ(cull.GetDiagnostics().boxesTested, 0U);
}

TEST_F(FrustumCullComputeTest, Cull_RefusesNonFiniteInputRatherThanAnsweringDifferently) {
    ASSERT_TRUE(cull.Initialize());
    std::vector<bool> visible;

    Math::Frustum brokenFrustum = MakeTestFrustum();
    brokenFrustum.planes[2].distance = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(cull.Cull(MakeBoxSpread(4U), brokenFrustum, visible));

    std::vector<Math::Aabb> brokenBoxes = MakeBoxSpread(4U);
    brokenBoxes[1].max.y = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(cull.Cull(brokenBoxes, MakeTestFrustum(), visible));

    EXPECT_EQ(cull.GetDiagnostics().cullsRejected, 2U);
}

TEST_F(FrustumCullComputeTest, Cull_OnNullBackend_QueuesRealWorkAndReadsBack) {
    // Null executes nothing, but it is a faithful bookkeeper: the dispatch must be recorded, all
    // three storage buffers bound, and the readback succeed. That is the plumbing, not the maths.
    ASSERT_TRUE(cull.Initialize());
    std::vector<bool> visible;

    EXPECT_TRUE(cull.Cull(MakeBoxSpread(200U), MakeTestFrustum(), visible));

    EXPECT_EQ(visible.size(), 200U);
    EXPECT_EQ(computeSystem.GetDiagnostics().dispatchesRecorded, 1U);
    EXPECT_EQ(computeSystem.GetQueuedDispatchCount(), 0U) << "the queue must not be left dirty";
    EXPECT_EQ(cull.GetDiagnostics().boxesTested, 200U);
    EXPECT_EQ(cull.GetDiagnostics().readbackFailures, 0U);
    EXPECT_EQ(cull.GetDiagnostics().uploadFailures, 0U);
}

TEST_F(FrustumCullComputeTest, Cull_ReusesItsBuffersUntilItMustGrow) {
    ASSERT_TRUE(cull.Initialize());
    const Math::Frustum frustum = MakeTestFrustum();
    std::vector<bool> visible;

    EXPECT_TRUE(cull.Cull(MakeBoxSpread(16U), frustum, visible));
    EXPECT_EQ(cull.GetDiagnostics().bufferReallocations, 1U);

    EXPECT_TRUE(cull.Cull(MakeBoxSpread(16U), frustum, visible));
    EXPECT_TRUE(cull.Cull(MakeBoxSpread(4U), frustum, visible));
    EXPECT_EQ(cull.GetDiagnostics().bufferReallocations, 1U);

    EXPECT_TRUE(cull.Cull(MakeBoxSpread(512U), frustum, visible));
    EXPECT_EQ(cull.GetDiagnostics().bufferReallocations, 2U);
}

// ---------------------------------------------------------------------------
// The real proof, on a real GL context: the GPU's visibility must equal the CPU
// test's for every box. Skips cleanly without a display, like every GL test.
// ---------------------------------------------------------------------------

[[nodiscard]] Window::WindowDesc MakeCullTestWindowDesc() {
    Window::WindowDesc desc;
    desc.title = "uve_frustum_cull_compute_uve_tests";
    desc.width = 64;
    desc.height = 64;
    // This sandbox's Mesa/llvmpipe GLX stack caps at 4.5 Core; compute needs only 4.3.
    desc.glVersionMajor = 4;
    desc.glVersionMinor = 5;
    return desc;
}

class FrustumCullComputeGlTest : public ::testing::Test {
protected:
    void SetUp() override {
        windowManager =
            std::make_unique<Window::WindowManager>(eventSystem, MakeCullTestWindowDesc());
        if (!windowManager->IsValid()) {
            GTEST_SKIP() << "No display available for GlRenderDevice - skipping (run under "
                            "xvfb-run to exercise this test)";
        }
        renderDevice = std::make_unique<GlRenderDevice>(*windowManager);
        if (!renderDevice->IsUsable()) {
            GTEST_SKIP() << "GlRenderDevice came up unusable on this display";
        }
        computeSystem = std::make_unique<ComputeSystem>(*renderDevice);
        cull = std::make_unique<FrustumCullCompute>(*renderDevice, *computeSystem);

        std::string infoLog;
        if (!cull->Initialize(&infoLog)) {
            GTEST_SKIP() << "context lacks compute shaders (GL 4.3+): " << infoLog;
        }
    }

    Events::EventSystem eventSystem;
    std::unique_ptr<Window::WindowManager> windowManager;
    std::unique_ptr<GlRenderDevice> renderDevice;
    std::unique_ptr<ComputeSystem> computeSystem;
    std::unique_ptr<FrustumCullCompute> cull;
};

TEST_F(FrustumCullComputeGlTest, Cull_AgreesWithTheCpuFrustumTestOnEveryBox) {
    // 1000 boxes is not a multiple of the 64-wide workgroup, so the kernel's tail guard is
    // exercised against buffers sized exactly to the box count.
    const std::vector<Math::Aabb> boxes = MakeBoxSpread(1000U);
    const Math::Frustum frustum = MakeTestFrustum();

    std::vector<bool> onGpu;
    ASSERT_TRUE(cull->Cull(boxes, frustum, onGpu));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    const std::vector<bool> onCpu = CullOnCpu(boxes, frustum);
    ASSERT_NO_FATAL_FAILURE(ExpectBothAnswersPresent(onCpu));

    ExpectVisibilityIdentical(onGpu, onCpu, boxes);

    const std::size_t visibleCount =
        static_cast<std::size_t>(std::count(onCpu.begin(), onCpu.end(), true));
    EXPECT_EQ(cull->GetDiagnostics().boxesVisible, visibleCount);
    EXPECT_EQ(cull->GetDiagnostics().boxesCulled, onCpu.size() - visibleCount);
}

TEST_F(FrustumCullComputeGlTest, Cull_AgreesOnBoxesSittingExactlyOnThePlanes) {
    // The cases that matter most: a box whose surface touches a plane exactly is INSIDE by the
    // CPU's strict `distance + radius < 0` rejection, and one hair further out is not. If the GPU
    // computed that sum with a fused multiply-add it would land on a different float here - and
    // nowhere else would the difference be visible. Each box is placed by pushing its centre to
    // exactly `radius` behind a plane, then nudged one ULP either way.
    std::vector<Math::Aabb> boxes;
    const Math::Frustum frustum = MakeTestFrustum();
    const Math::Vector3 extents{0.5F, 0.5F, 0.5F};

    for (const Math::Plane& plane : frustum.planes) {
        const float radius = extents.x * std::abs(plane.normal.x) + extents.y * std::abs(plane.normal.y) +
                             extents.z * std::abs(plane.normal.z);
        // A point on the plane, then moved back along the normal by exactly the box's radius: the
        // box is then touching the plane from outside - the exact boundary.
        const Math::Vector3 onPlane{plane.normal.x * -plane.distance, plane.normal.y * -plane.distance,
                                       plane.normal.z * -plane.distance};
        const Math::Vector3 center{onPlane.x - plane.normal.x * radius,
                                      onPlane.y - plane.normal.y * radius,
                                      onPlane.z - plane.normal.z * radius};
        boxes.push_back(Math::Aabb::FromCenterExtents(center, extents));
        for (const float nudge : {-2.0F, -1.0F, 1.0F, 2.0F}) {
            const Math::Vector3 nudged{std::nextafter(center.x, center.x + nudge),
                                          std::nextafter(center.y, center.y + nudge),
                                          std::nextafter(center.z, center.z + nudge)};
            boxes.push_back(Math::Aabb::FromCenterExtents(nudged, extents));
        }
    }

    std::vector<bool> onGpu;
    ASSERT_TRUE(cull->Cull(boxes, frustum, onGpu));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    const std::vector<bool> onCpu = CullOnCpu(boxes, frustum);
    // The nudges must actually land on both sides of the boundary - if every boundary box came
    // back with the same answer, this test would not be probing a boundary at all.
    ASSERT_NO_FATAL_FAILURE(ExpectBothAnswersPresent(onCpu));
    ExpectVisibilityIdentical(onGpu, onCpu, boxes);
}

TEST_F(FrustumCullComputeGlTest, Cull_PartialWorkgroupAndRepeatedCalls_StayInAgreement) {
    // Three boxes: every invocation but three is a tail invocation. Then a second, larger call
    // through the same buffers, to prove the grow path does not leave stale visibility behind -
    // a reused buffer that kept an old entry would still "agree" on any box it happened to match.
    const Math::Frustum frustum = MakeTestFrustum();

    const std::vector<Math::Aabb> few = MakeBoxSpread(3U);
    const std::vector<bool> fewOnCpu = CullOnCpu(few, frustum);
    ASSERT_NO_FATAL_FAILURE(ExpectBothAnswersPresent(fewOnCpu))
        << "even three boxes must contain both answers, or this test proves nothing";

    std::vector<bool> onGpu;
    ASSERT_TRUE(cull->Cull(few, frustum, onGpu));
    ExpectVisibilityIdentical(onGpu, fewOnCpu, few);

    const std::vector<Math::Aabb> many = MakeBoxSpread(300U);
    const std::vector<bool> manyOnCpu = CullOnCpu(many, frustum);
    ASSERT_NO_FATAL_FAILURE(ExpectBothAnswersPresent(manyOnCpu));
    ASSERT_TRUE(cull->Cull(many, frustum, onGpu));
    ExpectVisibilityIdentical(onGpu, manyOnCpu, many);

    // And back down again, reusing the grown buffers.
    ASSERT_TRUE(cull->Cull(few, frustum, onGpu));
    ExpectVisibilityIdentical(onGpu, fewOnCpu, few);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

} // namespace
} // namespace UVE::Render::Tests
