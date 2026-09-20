// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/frustum_cull_compute_uve.h"

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

#include "uve/events/event_system_uve.h"
#include "uve/math/aabb_uve.h"
#include "uve/math/frustum_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/render_systems/compute_system_uve.h"
#include "uve/rhi_null/null_render_device_uve.h"

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/gl.h>

#include "uve/rhi_opengl/gl_render_device_uve.h"
#include "uve/window/window_manager_uve.h"

namespace UVE::Render::Tests {
namespace {

/// A camera looking down -Z from the origin, the same shape Renderer3DUVE builds: 60 degree
/// vertical field of view, 16:9, near 0.1, far 100.
[[nodiscard]] Math::FrustumUVE MakeTestFrustumUVE() {
    const Math::Matrix4x4UVE projection =
        Math::Matrix4x4UVE::PerspectiveUVE(60.0F * 3.14159265F / 180.0F, 16.0F / 9.0F, 0.1F, 100.0F);
    // An unrotated camera at the origin already looks down -Z in this matrix convention, which is
    // what the box spread below is built around.
    const Math::Matrix4x4UVE view = Math::Matrix4x4UVE::ViewFromPositionAndRotationUVE(
        Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::QuaternionUVE{});
    return Math::FrustumUVE::FromViewProjectionUVE(projection * view);
}

/// A spread of boxes that straddles the frustum AT EVERY LENGTH, not just in aggregate.
///
/// The first version of this fixture swept steadily from outside to inside, which looked fine at
/// 1000 boxes (711 visible, 289 culled) but was entirely culled for the first 23 - so the small-N
/// tests below would have passed against a kernel that unconditionally answered "culled". A
/// probe against the real Math::FrustumUVE found that; the pattern is now periodic instead, with
/// every third box pushed far outside a side plane and the side alternating, so even three boxes
/// contain both answers. Positions use offsets with no exact binary representation, since round
/// numbers would agree even through a contracted multiply-add.
[[nodiscard]] std::vector<Math::AabbUVE> MakeBoxSpreadUVE(const std::size_t count) {
    std::vector<Math::AabbUVE> boxes;
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
        boxes.push_back(Math::AabbUVE::FromCenterExtentsUVE(
            Math::Vector3UVE{lateral, vertical, depth},
            Math::Vector3UVE{halfSize, halfSize, halfSize}));
    }
    return boxes;
}

/// The CPU authority: the real Math::FrustumUVE::IntersectsUVE, not a reimplementation of it here.
[[nodiscard]] std::vector<bool> CullOnCpuUVE(const std::vector<Math::AabbUVE>& boxes,
                                             const Math::FrustumUVE& frustum) {
    std::vector<bool> visible;
    visible.reserve(boxes.size());
    for (const Math::AabbUVE& box : boxes) {
        visible.push_back(frustum.IntersectsUVE(box));
    }
    return visible;
}

/// Guards the guard: a comparison against a set that is entirely visible - or entirely culled -
/// would pass against a kernel that returns a constant. Every GL test below asserts this on its
/// own input before trusting the agreement it just checked.
void ExpectBothAnswersPresentUVE(const std::vector<bool>& visibility) {
    const std::size_t visibleCount =
        static_cast<std::size_t>(std::count(visibility.begin(), visibility.end(), true));
    EXPECT_GT(visibleCount, 0U) << "the fixture must produce visible boxes";
    EXPECT_LT(visibleCount, visibility.size()) << "the fixture must produce culled boxes";
}

/// Reports the first disagreement in full rather than just failing the vector comparison - a bare
/// "vectors differ" on a thousand boxes is not something anyone can debug.
void ExpectVisibilityIdenticalUVE(const std::vector<bool>& actual, const std::vector<bool>& expected,
                                  const std::vector<Math::AabbUVE>& boxes) {
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        EXPECT_EQ(actual[index], expected[index])
            << "box " << index << " " << Math::ToStringUVE(boxes[index])
            << ": the GPU said " << (actual[index] ? "visible" : "culled") << " and the CPU said "
            << (expected[index] ? "visible" : "culled");
    }
}

// ---------------------------------------------------------------------------
// Backend-independent contract, on the Null device. Null records dispatches
// without executing them, so these never assert visibility VALUES - only the
// contract around them.
// ---------------------------------------------------------------------------

class FrustumCullComputeUVETest : public ::testing::Test {
protected:
    NullRenderDeviceUVE device;
    ComputeSystemUVE computeSystem{device};
    FrustumCullComputeUVE cull{device, computeSystem};
};

TEST_F(FrustumCullComputeUVETest, InitializeUVE_BuildsTheKernelAndIsIdempotent) {
    EXPECT_FALSE(cull.IsReadyUVE());
    ASSERT_TRUE(cull.InitializeUVE());
    EXPECT_TRUE(cull.IsReadyUVE());

    EXPECT_TRUE(cull.InitializeUVE());
    EXPECT_EQ(computeSystem.GetDiagnosticsUVE().programsCreated, 1U);
}

TEST_F(FrustumCullComputeUVETest, CullUVE_BeforeInitialize_RefusesAndLeavesTheOutputAlone) {
    const std::vector<Math::AabbUVE> boxes = MakeBoxSpreadUVE(4U);
    std::vector<bool> visible{true, true};

    EXPECT_FALSE(cull.CullUVE(boxes, MakeTestFrustumUVE(), visible));

    EXPECT_EQ(visible, (std::vector<bool>{true, true})) << "a refused cull must not touch the output";
    EXPECT_EQ(cull.GetDiagnosticsUVE().cullsRejected, 1U);
}

TEST_F(FrustumCullComputeUVETest, CullUVE_EmptyInput_IsASuccessfulNoOp) {
    ASSERT_TRUE(cull.InitializeUVE());
    std::vector<bool> visible{true, false, true};

    EXPECT_TRUE(cull.CullUVE({}, MakeTestFrustumUVE(), visible));

    EXPECT_TRUE(visible.empty());
    EXPECT_EQ(cull.GetDiagnosticsUVE().cullsSkipped, 1U);
    EXPECT_EQ(cull.GetDiagnosticsUVE().boxesTested, 0U);
}

TEST_F(FrustumCullComputeUVETest, CullUVE_RefusesNonFiniteInputRatherThanAnsweringDifferently) {
    ASSERT_TRUE(cull.InitializeUVE());
    std::vector<bool> visible;

    Math::FrustumUVE brokenFrustum = MakeTestFrustumUVE();
    brokenFrustum.planes[2].distance = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(cull.CullUVE(MakeBoxSpreadUVE(4U), brokenFrustum, visible));

    std::vector<Math::AabbUVE> brokenBoxes = MakeBoxSpreadUVE(4U);
    brokenBoxes[1].max.y = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(cull.CullUVE(brokenBoxes, MakeTestFrustumUVE(), visible));

    EXPECT_EQ(cull.GetDiagnosticsUVE().cullsRejected, 2U);
}

TEST_F(FrustumCullComputeUVETest, CullUVE_OnNullBackend_QueuesRealWorkAndReadsBack) {
    // Null executes nothing, but it is a faithful bookkeeper: the dispatch must be recorded, all
    // three storage buffers bound, and the readback succeed. That is the plumbing, not the maths.
    ASSERT_TRUE(cull.InitializeUVE());
    std::vector<bool> visible;

    EXPECT_TRUE(cull.CullUVE(MakeBoxSpreadUVE(200U), MakeTestFrustumUVE(), visible));

    EXPECT_EQ(visible.size(), 200U);
    EXPECT_EQ(computeSystem.GetDiagnosticsUVE().dispatchesRecorded, 1U);
    EXPECT_EQ(computeSystem.GetQueuedDispatchCountUVE(), 0U) << "the queue must not be left dirty";
    EXPECT_EQ(cull.GetDiagnosticsUVE().boxesTested, 200U);
    EXPECT_EQ(cull.GetDiagnosticsUVE().readbackFailures, 0U);
    EXPECT_EQ(cull.GetDiagnosticsUVE().uploadFailures, 0U);
}

TEST_F(FrustumCullComputeUVETest, CullUVE_ReusesItsBuffersUntilItMustGrow) {
    ASSERT_TRUE(cull.InitializeUVE());
    const Math::FrustumUVE frustum = MakeTestFrustumUVE();
    std::vector<bool> visible;

    EXPECT_TRUE(cull.CullUVE(MakeBoxSpreadUVE(16U), frustum, visible));
    EXPECT_EQ(cull.GetDiagnosticsUVE().bufferReallocations, 1U);

    EXPECT_TRUE(cull.CullUVE(MakeBoxSpreadUVE(16U), frustum, visible));
    EXPECT_TRUE(cull.CullUVE(MakeBoxSpreadUVE(4U), frustum, visible));
    EXPECT_EQ(cull.GetDiagnosticsUVE().bufferReallocations, 1U);

    EXPECT_TRUE(cull.CullUVE(MakeBoxSpreadUVE(512U), frustum, visible));
    EXPECT_EQ(cull.GetDiagnosticsUVE().bufferReallocations, 2U);
}

// ---------------------------------------------------------------------------
// The real proof, on a real GL context: the GPU's visibility must equal the CPU
// test's for every box. Skips cleanly without a display, like every GL test.
// ---------------------------------------------------------------------------

[[nodiscard]] Window::WindowDescUVE MakeCullTestWindowDescUVE() {
    Window::WindowDescUVE desc;
    desc.title = "uve_frustum_cull_compute_uve_tests";
    desc.width = 64;
    desc.height = 64;
    // This sandbox's Mesa/llvmpipe GLX stack caps at 4.5 Core; compute needs only 4.3.
    desc.glVersionMajor = 4;
    desc.glVersionMinor = 5;
    return desc;
}

class FrustumCullComputeGlUVETest : public ::testing::Test {
protected:
    void SetUp() override {
        windowManager =
            std::make_unique<Window::WindowManagerUVE>(eventSystem, MakeCullTestWindowDescUVE());
        if (!windowManager->IsValidUVE()) {
            GTEST_SKIP() << "No display available for GlRenderDeviceUVE - skipping (run under "
                            "xvfb-run to exercise this test)";
        }
        renderDevice = std::make_unique<GlRenderDeviceUVE>(*windowManager);
        if (!renderDevice->IsUsableUVE()) {
            GTEST_SKIP() << "GlRenderDeviceUVE came up unusable on this display";
        }
        computeSystem = std::make_unique<ComputeSystemUVE>(*renderDevice);
        cull = std::make_unique<FrustumCullComputeUVE>(*renderDevice, *computeSystem);

        std::string infoLog;
        if (!cull->InitializeUVE(&infoLog)) {
            GTEST_SKIP() << "context lacks compute shaders (GL 4.3+): " << infoLog;
        }
    }

    Events::EventSystemUVE eventSystem;
    std::unique_ptr<Window::WindowManagerUVE> windowManager;
    std::unique_ptr<GlRenderDeviceUVE> renderDevice;
    std::unique_ptr<ComputeSystemUVE> computeSystem;
    std::unique_ptr<FrustumCullComputeUVE> cull;
};

TEST_F(FrustumCullComputeGlUVETest, CullUVE_AgreesWithTheCpuFrustumTestOnEveryBox) {
    // 1000 boxes is not a multiple of the 64-wide workgroup, so the kernel's tail guard is
    // exercised against buffers sized exactly to the box count.
    const std::vector<Math::AabbUVE> boxes = MakeBoxSpreadUVE(1000U);
    const Math::FrustumUVE frustum = MakeTestFrustumUVE();

    std::vector<bool> onGpu;
    ASSERT_TRUE(cull->CullUVE(boxes, frustum, onGpu));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    const std::vector<bool> onCpu = CullOnCpuUVE(boxes, frustum);
    ASSERT_NO_FATAL_FAILURE(ExpectBothAnswersPresentUVE(onCpu));

    ExpectVisibilityIdenticalUVE(onGpu, onCpu, boxes);

    const std::size_t visibleCount =
        static_cast<std::size_t>(std::count(onCpu.begin(), onCpu.end(), true));
    EXPECT_EQ(cull->GetDiagnosticsUVE().boxesVisible, visibleCount);
    EXPECT_EQ(cull->GetDiagnosticsUVE().boxesCulled, onCpu.size() - visibleCount);
}

TEST_F(FrustumCullComputeGlUVETest, CullUVE_AgreesOnBoxesSittingExactlyOnThePlanes) {
    // The cases that matter most: a box whose surface touches a plane exactly is INSIDE by the
    // CPU's strict `distance + radius < 0` rejection, and one hair further out is not. If the GPU
    // computed that sum with a fused multiply-add it would land on a different float here - and
    // nowhere else would the difference be visible. Each box is placed by pushing its centre to
    // exactly `radius` behind a plane, then nudged one ULP either way.
    std::vector<Math::AabbUVE> boxes;
    const Math::FrustumUVE frustum = MakeTestFrustumUVE();
    const Math::Vector3UVE extents{0.5F, 0.5F, 0.5F};

    for (const Math::PlaneUVE& plane : frustum.planes) {
        const float radius = extents.x * std::abs(plane.normal.x) + extents.y * std::abs(plane.normal.y) +
                             extents.z * std::abs(plane.normal.z);
        // A point on the plane, then moved back along the normal by exactly the box's radius: the
        // box is then touching the plane from outside - the exact boundary.
        const Math::Vector3UVE onPlane{plane.normal.x * -plane.distance, plane.normal.y * -plane.distance,
                                       plane.normal.z * -plane.distance};
        const Math::Vector3UVE center{onPlane.x - plane.normal.x * radius,
                                      onPlane.y - plane.normal.y * radius,
                                      onPlane.z - plane.normal.z * radius};
        boxes.push_back(Math::AabbUVE::FromCenterExtentsUVE(center, extents));
        for (const float nudge : {-2.0F, -1.0F, 1.0F, 2.0F}) {
            const Math::Vector3UVE nudged{std::nextafter(center.x, center.x + nudge),
                                          std::nextafter(center.y, center.y + nudge),
                                          std::nextafter(center.z, center.z + nudge)};
            boxes.push_back(Math::AabbUVE::FromCenterExtentsUVE(nudged, extents));
        }
    }

    std::vector<bool> onGpu;
    ASSERT_TRUE(cull->CullUVE(boxes, frustum, onGpu));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    const std::vector<bool> onCpu = CullOnCpuUVE(boxes, frustum);
    // The nudges must actually land on both sides of the boundary - if every boundary box came
    // back with the same answer, this test would not be probing a boundary at all.
    ASSERT_NO_FATAL_FAILURE(ExpectBothAnswersPresentUVE(onCpu));
    ExpectVisibilityIdenticalUVE(onGpu, onCpu, boxes);
}

TEST_F(FrustumCullComputeGlUVETest, CullUVE_PartialWorkgroupAndRepeatedCalls_StayInAgreement) {
    // Three boxes: every invocation but three is a tail invocation. Then a second, larger call
    // through the same buffers, to prove the grow path does not leave stale visibility behind -
    // a reused buffer that kept an old entry would still "agree" on any box it happened to match.
    const Math::FrustumUVE frustum = MakeTestFrustumUVE();

    const std::vector<Math::AabbUVE> few = MakeBoxSpreadUVE(3U);
    const std::vector<bool> fewOnCpu = CullOnCpuUVE(few, frustum);
    ASSERT_NO_FATAL_FAILURE(ExpectBothAnswersPresentUVE(fewOnCpu))
        << "even three boxes must contain both answers, or this test proves nothing";

    std::vector<bool> onGpu;
    ASSERT_TRUE(cull->CullUVE(few, frustum, onGpu));
    ExpectVisibilityIdenticalUVE(onGpu, fewOnCpu, few);

    const std::vector<Math::AabbUVE> many = MakeBoxSpreadUVE(300U);
    const std::vector<bool> manyOnCpu = CullOnCpuUVE(many, frustum);
    ASSERT_NO_FATAL_FAILURE(ExpectBothAnswersPresentUVE(manyOnCpu));
    ASSERT_TRUE(cull->CullUVE(many, frustum, onGpu));
    ExpectVisibilityIdenticalUVE(onGpu, manyOnCpu, many);

    // And back down again, reusing the grown buffers.
    ASSERT_TRUE(cull->CullUVE(few, frustum, onGpu));
    ExpectVisibilityIdenticalUVE(onGpu, fewOnCpu, few);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

} // namespace
} // namespace UVE::Render::Tests
