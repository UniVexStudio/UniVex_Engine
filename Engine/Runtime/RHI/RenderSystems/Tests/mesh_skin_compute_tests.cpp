// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "mesh_skin_compute.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mesh_asset.h"
#include "mesh_skinning.h"
#include "event_system.h"
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

// ---------------------------------------------------------------------------
// CS10. The GPU twin of Asset::TrySkinMesh, held to the same standard as
// every other compute workload here: EXACTLY the CPU's result, bit for bit,
// no tolerance.
//
// That standard is only reachable because CS9's CPU path accumulates in float.
// Math::TransformPoint accumulates in double, and GLSL has no portable
// float64 - a measurement during this slice put the resulting disagreement at
// ~1 ULP on roughly one vertex in six. A tolerance would have hidden that, and
// would equally have hidden a genuinely wrong weight, which is the whole reason
// it was not the answer.
// ---------------------------------------------------------------------------

/// Bit-exact comparison. Deliberately not EXPECT_FLOAT_EQ, which tolerates 4 ULP - the claim under
/// test is exact agreement, and a 4 ULP allowance is precisely the size of the discrepancy this
/// design went out of its way to eliminate.
void ExpectBitIdentical(const float actual, const float expected, const char* const what,
                           const std::size_t index) {
    std::uint32_t actualBits = 0U;
    std::uint32_t expectedBits = 0U;
    std::memcpy(&actualBits, &actual, sizeof(actualBits));
    std::memcpy(&expectedBits, &expected, sizeof(expectedBits));
    EXPECT_EQ(actualBits, expectedBits)
        << "vertex " << index << " " << what << ": GPU " << actual << " vs CPU " << expected;
}

void ExpectVerticesBitIdentical(const std::vector<Asset::MeshVertex>& actual,
                                   const std::vector<Asset::MeshVertex>& expected) {
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        ExpectBitIdentical(actual[index].position.x, expected[index].position.x, "position.x", index);
        ExpectBitIdentical(actual[index].position.y, expected[index].position.y, "position.y", index);
        ExpectBitIdentical(actual[index].position.z, expected[index].position.z, "position.z", index);
        ExpectBitIdentical(actual[index].normal.x, expected[index].normal.x, "normal.x", index);
        ExpectBitIdentical(actual[index].normal.y, expected[index].normal.y, "normal.y", index);
        ExpectBitIdentical(actual[index].normal.z, expected[index].normal.z, "normal.z", index);
        ExpectBitIdentical(actual[index].tangent.x, expected[index].tangent.x, "tangent.x", index);
        ExpectBitIdentical(actual[index].tangent.y, expected[index].tangent.y, "tangent.y", index);
        ExpectBitIdentical(actual[index].tangent.z, expected[index].tangent.z, "tangent.z", index);
        ExpectBitIdentical(actual[index].tangentHandedness, expected[index].tangentHandedness,
                              "handedness", index);
        // UVs never reach the GPU at all - they are copied across from the source, and this is
        // what catches that copy going wrong.
        EXPECT_EQ(actual[index].u, expected[index].u) << "vertex " << index << " u";
        EXPECT_EQ(actual[index].v, expected[index].v) << "vertex " << index << " v";
    }
}

/// A chain of joints along +X, each parented to the last. Long enough that a pose composes through
/// several levels rather than one.
[[nodiscard]] std::vector<Asset::MeshJoint> MakeJointChain(const std::size_t jointCount) {
    std::vector<Asset::MeshJoint> joints(jointCount);
    for (std::size_t index = 0U; index < jointCount; ++index) {
        joints[index].parentIndex = (index == 0U) ? Asset::kInvalidJointParent
                                                  : static_cast<std::uint32_t>(index - 1U);
        // Bind transform of joint i is a translation of +i on X, so its inverse is -i.
        joints[index].inverseBindMatrix = Math::Matrix4x4::ComposeTrs(
            Math::Vector3{-static_cast<float>(index), 0.0F, 0.0F}, Math::Quaternion{},
            Math::Vector3{1.0F, 1.0F, 1.0F});
    }
    return joints;
}

/// The matching bind pose: each joint sits one unit further along X than its parent.
[[nodiscard]] std::vector<Math::Matrix4x4> MakeBindPose(const std::size_t jointCount) {
    std::vector<Math::Matrix4x4> pose;
    pose.reserve(jointCount);
    pose.push_back(Math::Matrix4x4::Identity());
    for (std::size_t index = 1U; index < jointCount; ++index) {
        pose.push_back(Math::Matrix4x4::ComposeTrs(Math::Vector3{1.0F, 0.0F, 0.0F},
                                                         Math::Quaternion{},
                                                         Math::Vector3{1.0F, 1.0F, 1.0F}));
    }
    return pose;
}

/// An animated pose: every joint rotated and translated by amounts with no exact binary
/// representation. Round numbers would agree even through a contracted multiply-add and prove
/// nothing about the `precise` qualifiers.
[[nodiscard]] std::vector<Math::Matrix4x4> MakeAwkwardPose(const std::size_t jointCount) {
    std::vector<Math::Matrix4x4> pose;
    pose.reserve(jointCount);
    for (std::size_t index = 0U; index < jointCount; ++index) {
        const float step = static_cast<float>(index);
        Math::Quaternion rotation;
        const bool madeRotation = Math::TryMakeAxisAngle(
            Math::Vector3{0.37F, 0.81F - step * 0.03F, -0.44F + step * 0.017F},
            0.613F + step * 0.229F, rotation);
        EXPECT_TRUE(madeRotation);
        pose.push_back(Math::Matrix4x4::ComposeTrs(
            Math::Vector3{1.0F + std::fmod(step * 0.317F, 0.9F),
                             std::fmod(step * 0.211F, 1.3F) - 0.65F,
                             std::fmod(step * 0.173F, 1.1F) - 0.55F},
            rotation, Math::Vector3{1.0F, 1.0F, 1.0F}));
    }
    return pose;
}

/// A skinned mesh whose vertices spread across the joint chain, with weights that are genuinely
/// split rather than 1.0/0.0 everywhere - the blend is the part most likely to diverge.
[[nodiscard]] Asset::MeshAsset MakeSkinnedMesh(const std::size_t vertexCount,
                                                     const std::size_t jointCount) {
    Asset::MeshAsset mesh;
    mesh.vertices.reserve(vertexCount);
    mesh.skinningInfluences.reserve(vertexCount);
    for (std::size_t index = 0U; index < vertexCount; ++index) {
        const float step = static_cast<float>(index);
        Asset::MeshVertex vertex;
        vertex.position = Math::Vector3{std::fmod(step * 0.731F, 7.3F) - 3.65F,
                                           std::fmod(step * 0.379F, 2.9F) - 1.45F,
                                           std::fmod(step * 0.517F, 4.1F) - 2.05F};
        // Unnormalized on purpose: the kernel must not assume unit normals, and an unnormalized
        // input makes a stray normalize() in either path visible immediately.
        vertex.normal = Math::Vector3{0.577F + std::fmod(step * 0.031F, 0.4F), -0.577F,
                                         0.577F - std::fmod(step * 0.019F, 0.3F)};
        vertex.tangent = Math::Vector3{-0.707F, std::fmod(step * 0.023F, 0.5F), 0.707F};
        vertex.tangentHandedness = (index % 2U == 0U) ? 1.0F : -1.0F;
        vertex.u = std::fmod(step * 0.113F, 1.0F);
        vertex.v = std::fmod(step * 0.271F, 1.0F);
        mesh.vertices.push_back(vertex);

        Asset::MeshSkinningInfluence influence;
        const auto firstJoint = static_cast<std::uint32_t>(index % jointCount);
        const auto secondJoint = static_cast<std::uint32_t>((index + 1U) % jointCount);
        const auto thirdJoint = static_cast<std::uint32_t>((index + 2U) % jointCount);
        influence.joints[0] = firstJoint;
        influence.joints[1] = secondJoint;
        influence.joints[2] = thirdJoint;
        influence.joints[3] = firstJoint;
        if (index % 5U == 0U) {
            // Every fifth vertex is rigidly bound - the zero-weight slots are what exercise the
            // kernel's skip-rather-than-multiply rule.
            influence.weights[0] = 1.0F;
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
    mesh.joints = MakeJointChain(jointCount);
    return mesh;
}

[[nodiscard]] Window::WindowDesc MakeSkinTestWindowDesc() {
    Window::WindowDesc desc;
    desc.title = "uve_mesh_skin_compute_uve_tests";
    desc.width = 64;
    desc.height = 64;
    desc.glVersionMajor = 4;
    desc.glVersionMinor = 5;
    return desc;
}

// ---------------------------------------------------------------------------
// Backend-independent contract, on the Null device. Null records dispatches
// without executing them, so these never assert skinned VALUES.
// ---------------------------------------------------------------------------

class MeshSkinComputeTest : public ::testing::Test {
protected:
    NullRenderDevice device;
    ComputeSystem computeSystem{device};
    MeshSkinCompute skin{device, computeSystem};
};

TEST_F(MeshSkinComputeTest, Initialize_BuildsTheKernelAndIsIdempotent) {
    EXPECT_FALSE(skin.IsReady());
    ASSERT_TRUE(skin.Initialize());
    EXPECT_TRUE(skin.IsReady());
    EXPECT_TRUE(skin.Initialize());
    EXPECT_EQ(computeSystem.GetDiagnostics().programsCreated, 1U);
}

TEST_F(MeshSkinComputeTest, Skin_BeforeInitialize_RefusesAndLeavesTheOutputAlone) {
    const Asset::MeshAsset mesh = MakeSkinnedMesh(8U, 3U);
    std::vector<Math::Matrix4x4> matrices;
    ASSERT_TRUE(Asset::TryResolvePose(mesh.joints, MakeBindPose(3U), matrices));

    std::vector<Asset::MeshVertex> out(2U);
    EXPECT_FALSE(skin.Skin(mesh, matrices, out));
    EXPECT_EQ(out.size(), 2U);
    EXPECT_EQ(skin.GetDiagnostics().skinsRejected, 1U);
}

TEST_F(MeshSkinComputeTest, Skin_MirrorsTheCpuPathsRefusalSet) {
    ASSERT_TRUE(skin.Initialize());
    std::vector<Asset::MeshVertex> out;

    // A caller must be able to switch between the CPU and GPU paths without changing its error
    // handling, so each refusal here is one the CPU path also makes.
    Asset::MeshAsset staticMesh;
    staticMesh.vertices.resize(3U);
    EXPECT_FALSE(skin.Skin(staticMesh, std::vector<Math::Matrix4x4>{}, out));

    const Asset::MeshAsset mesh = MakeSkinnedMesh(8U, 3U);
    std::vector<Math::Matrix4x4> matrices;
    ASSERT_TRUE(Asset::TryResolvePose(mesh.joints, MakeBindPose(3U), matrices));

    const std::vector<Math::Matrix4x4> tooFew{matrices.front()};
    EXPECT_FALSE(skin.Skin(mesh, tooFew, out));

    Asset::MeshAsset badWeights = mesh;
    badWeights.skinningInfluences[1].weights[0] = 0.25F;
    badWeights.skinningInfluences[1].weights[1] = 0.25F;
    badWeights.skinningInfluences[1].weights[2] = 0.25F;
    EXPECT_FALSE(skin.Skin(badWeights, matrices, out));

    Asset::MeshAsset nonFinite = mesh;
    nonFinite.vertices[2].position.y = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(skin.Skin(nonFinite, matrices, out));

    EXPECT_TRUE(out.empty());
}

TEST_F(MeshSkinComputeTest, Skin_ReusesItsBuffersUntilItMustGrow) {
    ASSERT_TRUE(skin.Initialize());
    const std::vector<Math::Matrix4x4> bindPose = MakeBindPose(4U);
    std::vector<Asset::MeshVertex> out;

    const Asset::MeshAsset large = MakeSkinnedMesh(64U, 4U);
    std::vector<Math::Matrix4x4> matrices;
    ASSERT_TRUE(Asset::TryResolvePose(large.joints, bindPose, matrices));
    ASSERT_TRUE(skin.Skin(large, matrices, out));
    const std::uint32_t afterFirst = skin.GetDiagnostics().bufferReallocations;
    EXPECT_EQ(afterFirst, 1U);

    // Fewer vertices, same skeleton: nothing should be reallocated.
    const Asset::MeshAsset small = MakeSkinnedMesh(16U, 4U);
    ASSERT_TRUE(skin.Skin(small, matrices, out));
    EXPECT_EQ(skin.GetDiagnostics().bufferReallocations, afterFirst);

    const Asset::MeshAsset bigger = MakeSkinnedMesh(65U, 4U);
    ASSERT_TRUE(skin.Skin(bigger, matrices, out));
    EXPECT_EQ(skin.GetDiagnostics().bufferReallocations, afterFirst + 1U);
}

// ---------------------------------------------------------------------------
// The real thing, on a real GL context.
// ---------------------------------------------------------------------------

class MeshSkinComputeGlTest : public ::testing::Test {
protected:
    void SetUp() override {
        windowManager =
            std::make_unique<Window::WindowManager>(eventSystem, MakeSkinTestWindowDesc());
        if (!windowManager->IsValid()) {
            GTEST_SKIP() << "No display available for GlRenderDevice - skipping (run under "
                            "xvfb-run to exercise this test)";
        }
        renderDevice = std::make_unique<GlRenderDevice>(*windowManager);
        if (!renderDevice->IsUsable()) {
            GTEST_SKIP() << "GlRenderDevice came up unusable on this display";
        }
        computeSystem = std::make_unique<ComputeSystem>(*renderDevice);
        skin = std::make_unique<MeshSkinCompute>(*renderDevice, *computeSystem);

        std::string infoLog;
        if (!skin->Initialize(&infoLog)) {
            GTEST_SKIP() << "context lacks compute shaders (GL 4.3+): " << infoLog;
        }
    }

    Events::EventSystem eventSystem;
    std::unique_ptr<Window::WindowManager> windowManager;
    std::unique_ptr<GlRenderDevice> renderDevice;
    std::unique_ptr<ComputeSystem> computeSystem;
    std::unique_ptr<MeshSkinCompute> skin;
};

TEST_F(MeshSkinComputeGlTest, Skin_MatchesTheCpuBitForBitOnAnAnimatedPose) {
    // 1000 vertices is not a multiple of the 64-wide workgroup, so the kernel's tail guard runs.
    const Asset::MeshAsset mesh = MakeSkinnedMesh(1000U, 6U);
    std::vector<Math::Matrix4x4> matrices;
    ASSERT_TRUE(Asset::TryResolvePose(mesh.joints, MakeAwkwardPose(6U), matrices));

    std::vector<Asset::MeshVertex> onCpu;
    ASSERT_TRUE(Asset::TrySkinMesh(mesh, matrices, onCpu));
    std::vector<Asset::MeshVertex> onGpu;
    ASSERT_TRUE(skin->Skin(mesh, matrices, onGpu));

    ExpectVerticesBitIdentical(onGpu, onCpu);
}

TEST_F(MeshSkinComputeGlTest, Skin_BindPose_ReturnsTheSourceVerticesUnchanged) {
    // The definitive property of an inverse bind matrix, now on the GPU: posing a skeleton in its
    // own bind pose must leave every vertex exactly where it already was. Exact, not near - any
    // drift here means the matrix upload or its transpose is wrong.
    const Asset::MeshAsset mesh = MakeSkinnedMesh(200U, 5U);
    std::vector<Math::Matrix4x4> matrices;
    ASSERT_TRUE(Asset::TryResolvePose(mesh.joints, MakeBindPose(5U), matrices));

    std::vector<Asset::MeshVertex> onGpu;
    ASSERT_TRUE(skin->Skin(mesh, matrices, onGpu));
    ExpectVerticesBitIdentical(onGpu, mesh.vertices);
}

TEST_F(MeshSkinComputeGlTest, Skin_TransposeIsCorrect_ATranslatedJointMovesVerticesNotShearsThem) {
    // A transposed matrix upload is the failure this kernel is most exposed to, and it is quiet:
    // with a symmetric pose it produces plausible-looking output. So this uses a pose that is
    // deliberately NOT symmetric - a pure translation, whose transpose would put the translation
    // into the matrix's bottom row where nothing reads it, leaving vertices unmoved.
    Asset::MeshAsset mesh = MakeSkinnedMesh(64U, 1U);
    for (Asset::MeshSkinningInfluence& influence : mesh.skinningInfluences) {
        influence.joints[0] = 0U;
        influence.weights[0] = 1.0F;
        influence.weights[1] = 0.0F;
        influence.weights[2] = 0.0F;
        influence.weights[3] = 0.0F;
    }
    const std::vector<Math::Matrix4x4> pose{Math::Matrix4x4::ComposeTrs(
        Math::Vector3{3.5F, -1.25F, 7.75F}, Math::Quaternion{},
        Math::Vector3{1.0F, 1.0F, 1.0F})};
    std::vector<Math::Matrix4x4> matrices;
    ASSERT_TRUE(Asset::TryResolvePose(mesh.joints, pose, matrices));

    std::vector<Asset::MeshVertex> onGpu;
    ASSERT_TRUE(skin->Skin(mesh, matrices, onGpu));
    ASSERT_EQ(onGpu.size(), mesh.vertices.size());
    for (std::size_t index = 0U; index < onGpu.size(); ++index) {
        ExpectBitIdentical(onGpu[index].position.x, mesh.vertices[index].position.x + 3.5F,
                              "translated position.x", index);
        ExpectBitIdentical(onGpu[index].position.y, mesh.vertices[index].position.y - 1.25F,
                              "translated position.y", index);
        ExpectBitIdentical(onGpu[index].position.z, mesh.vertices[index].position.z + 7.75F,
                              "translated position.z", index);
        // Normals are directions: a pure translation must leave them completely alone.
        ExpectBitIdentical(onGpu[index].normal.x, mesh.vertices[index].normal.x, "normal.x", index);
        ExpectBitIdentical(onGpu[index].normal.y, mesh.vertices[index].normal.y, "normal.y", index);
        ExpectBitIdentical(onGpu[index].normal.z, mesh.vertices[index].normal.z, "normal.z", index);
    }
}

TEST_F(MeshSkinComputeGlTest, Skin_AgreesAcrossPartialWorkgroupsAndRepeatedCalls) {
    const std::vector<Math::Matrix4x4> pose = MakeAwkwardPose(4U);
    for (const std::size_t count : {std::size_t{1U}, std::size_t{63U}, std::size_t{64U},
                                    std::size_t{65U}, std::size_t{129U}}) {
        const Asset::MeshAsset mesh = MakeSkinnedMesh(count, 4U);
        std::vector<Math::Matrix4x4> matrices;
        ASSERT_TRUE(Asset::TryResolvePose(mesh.joints, pose, matrices));

        std::vector<Asset::MeshVertex> onCpu;
        ASSERT_TRUE(Asset::TrySkinMesh(mesh, matrices, onCpu));
        // Twice, to catch a buffer left dirty between calls - the output buffer is reused and is
        // only fully overwritten if the dispatch really covers every vertex.
        for (int pass = 0; pass < 2; ++pass) {
            std::vector<Asset::MeshVertex> onGpu;
            ASSERT_TRUE(skin->Skin(mesh, matrices, onGpu)) << "count " << count << " pass " << pass;
            ExpectVerticesBitIdentical(onGpu, onCpu);
        }
    }
}

} // namespace
} // namespace UVE::Render::Tests
