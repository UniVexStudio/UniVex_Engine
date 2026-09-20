// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/mesh_skin_compute_uve.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "uve/asset/mesh_asset_uve.h"
#include "uve/asset/mesh_skinning_uve.h"
#include "uve/events/event_system_uve.h"
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

// ---------------------------------------------------------------------------
// CS10. The GPU twin of Asset::TrySkinMeshUVE, held to the same standard as
// every other compute workload here: EXACTLY the CPU's result, bit for bit,
// no tolerance.
//
// That standard is only reachable because CS9's CPU path accumulates in float.
// Math::TransformPointUVE accumulates in double, and GLSL has no portable
// float64 - a measurement during this slice put the resulting disagreement at
// ~1 ULP on roughly one vertex in six. A tolerance would have hidden that, and
// would equally have hidden a genuinely wrong weight, which is the whole reason
// it was not the answer.
// ---------------------------------------------------------------------------

/// Bit-exact comparison. Deliberately not EXPECT_FLOAT_EQ, which tolerates 4 ULP - the claim under
/// test is exact agreement, and a 4 ULP allowance is precisely the size of the discrepancy this
/// design went out of its way to eliminate.
void ExpectBitIdenticalUVE(const float actual, const float expected, const char* const what,
                           const std::size_t index) {
    std::uint32_t actualBits = 0U;
    std::uint32_t expectedBits = 0U;
    std::memcpy(&actualBits, &actual, sizeof(actualBits));
    std::memcpy(&expectedBits, &expected, sizeof(expectedBits));
    EXPECT_EQ(actualBits, expectedBits)
        << "vertex " << index << " " << what << ": GPU " << actual << " vs CPU " << expected;
}

void ExpectVerticesBitIdenticalUVE(const std::vector<Asset::MeshVertexUVE>& actual,
                                   const std::vector<Asset::MeshVertexUVE>& expected) {
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        ExpectBitIdenticalUVE(actual[index].position.x, expected[index].position.x, "position.x", index);
        ExpectBitIdenticalUVE(actual[index].position.y, expected[index].position.y, "position.y", index);
        ExpectBitIdenticalUVE(actual[index].position.z, expected[index].position.z, "position.z", index);
        ExpectBitIdenticalUVE(actual[index].normal.x, expected[index].normal.x, "normal.x", index);
        ExpectBitIdenticalUVE(actual[index].normal.y, expected[index].normal.y, "normal.y", index);
        ExpectBitIdenticalUVE(actual[index].normal.z, expected[index].normal.z, "normal.z", index);
        ExpectBitIdenticalUVE(actual[index].tangent.x, expected[index].tangent.x, "tangent.x", index);
        ExpectBitIdenticalUVE(actual[index].tangent.y, expected[index].tangent.y, "tangent.y", index);
        ExpectBitIdenticalUVE(actual[index].tangent.z, expected[index].tangent.z, "tangent.z", index);
        ExpectBitIdenticalUVE(actual[index].tangentHandedness, expected[index].tangentHandedness,
                              "handedness", index);
        // UVs never reach the GPU at all - they are copied across from the source, and this is
        // what catches that copy going wrong.
        EXPECT_EQ(actual[index].u, expected[index].u) << "vertex " << index << " u";
        EXPECT_EQ(actual[index].v, expected[index].v) << "vertex " << index << " v";
    }
}

/// A chain of joints along +X, each parented to the last. Long enough that a pose composes through
/// several levels rather than one.
[[nodiscard]] std::vector<Asset::MeshJointUVE> MakeJointChainUVE(const std::size_t jointCount) {
    std::vector<Asset::MeshJointUVE> joints(jointCount);
    for (std::size_t index = 0U; index < jointCount; ++index) {
        joints[index].parentIndex = (index == 0U) ? Asset::kInvalidJointParentUVE
                                                  : static_cast<std::uint32_t>(index - 1U);
        // Bind transform of joint i is a translation of +i on X, so its inverse is -i.
        joints[index].inverseBindMatrix = Math::Matrix4x4UVE::ComposeTrsUVE(
            Math::Vector3UVE{-static_cast<float>(index), 0.0F, 0.0F}, Math::QuaternionUVE{},
            Math::Vector3UVE{1.0F, 1.0F, 1.0F});
    }
    return joints;
}

/// The matching bind pose: each joint sits one unit further along X than its parent.
[[nodiscard]] std::vector<Math::Matrix4x4UVE> MakeBindPoseUVE(const std::size_t jointCount) {
    std::vector<Math::Matrix4x4UVE> pose;
    pose.reserve(jointCount);
    pose.push_back(Math::Matrix4x4UVE::IdentityUVE());
    for (std::size_t index = 1U; index < jointCount; ++index) {
        pose.push_back(Math::Matrix4x4UVE::ComposeTrsUVE(Math::Vector3UVE{1.0F, 0.0F, 0.0F},
                                                         Math::QuaternionUVE{},
                                                         Math::Vector3UVE{1.0F, 1.0F, 1.0F}));
    }
    return pose;
}

/// An animated pose: every joint rotated and translated by amounts with no exact binary
/// representation. Round numbers would agree even through a contracted multiply-add and prove
/// nothing about the `precise` qualifiers.
[[nodiscard]] std::vector<Math::Matrix4x4UVE> MakeAwkwardPoseUVE(const std::size_t jointCount) {
    std::vector<Math::Matrix4x4UVE> pose;
    pose.reserve(jointCount);
    for (std::size_t index = 0U; index < jointCount; ++index) {
        const float step = static_cast<float>(index);
        Math::QuaternionUVE rotation;
        const bool madeRotation = Math::TryMakeAxisAngleUVE(
            Math::Vector3UVE{0.37F, 0.81F - step * 0.03F, -0.44F + step * 0.017F},
            0.613F + step * 0.229F, rotation);
        EXPECT_TRUE(madeRotation);
        pose.push_back(Math::Matrix4x4UVE::ComposeTrsUVE(
            Math::Vector3UVE{1.0F + std::fmod(step * 0.317F, 0.9F),
                             std::fmod(step * 0.211F, 1.3F) - 0.65F,
                             std::fmod(step * 0.173F, 1.1F) - 0.55F},
            rotation, Math::Vector3UVE{1.0F, 1.0F, 1.0F}));
    }
    return pose;
}

/// A skinned mesh whose vertices spread across the joint chain, with weights that are genuinely
/// split rather than 1.0/0.0 everywhere - the blend is the part most likely to diverge.
[[nodiscard]] Asset::MeshAssetUVE MakeSkinnedMeshUVE(const std::size_t vertexCount,
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
        // Unnormalized on purpose: the kernel must not assume unit normals, and an unnormalized
        // input makes a stray normalize() in either path visible immediately.
        vertex.normal = Math::Vector3UVE{0.577F + std::fmod(step * 0.031F, 0.4F), -0.577F,
                                         0.577F - std::fmod(step * 0.019F, 0.3F)};
        vertex.tangent = Math::Vector3UVE{-0.707F, std::fmod(step * 0.023F, 0.5F), 0.707F};
        vertex.tangentHandedness = (index % 2U == 0U) ? 1.0F : -1.0F;
        vertex.u = std::fmod(step * 0.113F, 1.0F);
        vertex.v = std::fmod(step * 0.271F, 1.0F);
        mesh.vertices.push_back(vertex);

        Asset::MeshSkinningInfluenceUVE influence;
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
    mesh.joints = MakeJointChainUVE(jointCount);
    return mesh;
}

[[nodiscard]] Window::WindowDescUVE MakeSkinTestWindowDescUVE() {
    Window::WindowDescUVE desc;
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

class MeshSkinComputeUVETest : public ::testing::Test {
protected:
    NullRenderDeviceUVE device;
    ComputeSystemUVE computeSystem{device};
    MeshSkinComputeUVE skin{device, computeSystem};
};

TEST_F(MeshSkinComputeUVETest, InitializeUVE_BuildsTheKernelAndIsIdempotent) {
    EXPECT_FALSE(skin.IsReadyUVE());
    ASSERT_TRUE(skin.InitializeUVE());
    EXPECT_TRUE(skin.IsReadyUVE());
    EXPECT_TRUE(skin.InitializeUVE());
    EXPECT_EQ(computeSystem.GetDiagnosticsUVE().programsCreated, 1U);
}

TEST_F(MeshSkinComputeUVETest, SkinUVE_BeforeInitialize_RefusesAndLeavesTheOutputAlone) {
    const Asset::MeshAssetUVE mesh = MakeSkinnedMeshUVE(8U, 3U);
    std::vector<Math::Matrix4x4UVE> matrices;
    ASSERT_TRUE(Asset::TryResolvePoseUVE(mesh.joints, MakeBindPoseUVE(3U), matrices));

    std::vector<Asset::MeshVertexUVE> out(2U);
    EXPECT_FALSE(skin.SkinUVE(mesh, matrices, out));
    EXPECT_EQ(out.size(), 2U);
    EXPECT_EQ(skin.GetDiagnosticsUVE().skinsRejected, 1U);
}

TEST_F(MeshSkinComputeUVETest, SkinUVE_MirrorsTheCpuPathsRefusalSet) {
    ASSERT_TRUE(skin.InitializeUVE());
    std::vector<Asset::MeshVertexUVE> out;

    // A caller must be able to switch between the CPU and GPU paths without changing its error
    // handling, so each refusal here is one the CPU path also makes.
    Asset::MeshAssetUVE staticMesh;
    staticMesh.vertices.resize(3U);
    EXPECT_FALSE(skin.SkinUVE(staticMesh, std::vector<Math::Matrix4x4UVE>{}, out));

    const Asset::MeshAssetUVE mesh = MakeSkinnedMeshUVE(8U, 3U);
    std::vector<Math::Matrix4x4UVE> matrices;
    ASSERT_TRUE(Asset::TryResolvePoseUVE(mesh.joints, MakeBindPoseUVE(3U), matrices));

    const std::vector<Math::Matrix4x4UVE> tooFew{matrices.front()};
    EXPECT_FALSE(skin.SkinUVE(mesh, tooFew, out));

    Asset::MeshAssetUVE badWeights = mesh;
    badWeights.skinningInfluences[1].weights[0] = 0.25F;
    badWeights.skinningInfluences[1].weights[1] = 0.25F;
    badWeights.skinningInfluences[1].weights[2] = 0.25F;
    EXPECT_FALSE(skin.SkinUVE(badWeights, matrices, out));

    Asset::MeshAssetUVE nonFinite = mesh;
    nonFinite.vertices[2].position.y = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(skin.SkinUVE(nonFinite, matrices, out));

    EXPECT_TRUE(out.empty());
}

TEST_F(MeshSkinComputeUVETest, SkinUVE_ReusesItsBuffersUntilItMustGrow) {
    ASSERT_TRUE(skin.InitializeUVE());
    const std::vector<Math::Matrix4x4UVE> bindPose = MakeBindPoseUVE(4U);
    std::vector<Asset::MeshVertexUVE> out;

    const Asset::MeshAssetUVE large = MakeSkinnedMeshUVE(64U, 4U);
    std::vector<Math::Matrix4x4UVE> matrices;
    ASSERT_TRUE(Asset::TryResolvePoseUVE(large.joints, bindPose, matrices));
    ASSERT_TRUE(skin.SkinUVE(large, matrices, out));
    const std::uint32_t afterFirst = skin.GetDiagnosticsUVE().bufferReallocations;
    EXPECT_EQ(afterFirst, 1U);

    // Fewer vertices, same skeleton: nothing should be reallocated.
    const Asset::MeshAssetUVE small = MakeSkinnedMeshUVE(16U, 4U);
    ASSERT_TRUE(skin.SkinUVE(small, matrices, out));
    EXPECT_EQ(skin.GetDiagnosticsUVE().bufferReallocations, afterFirst);

    const Asset::MeshAssetUVE bigger = MakeSkinnedMeshUVE(65U, 4U);
    ASSERT_TRUE(skin.SkinUVE(bigger, matrices, out));
    EXPECT_EQ(skin.GetDiagnosticsUVE().bufferReallocations, afterFirst + 1U);
}

// ---------------------------------------------------------------------------
// The real thing, on a real GL context.
// ---------------------------------------------------------------------------

class MeshSkinComputeGlUVETest : public ::testing::Test {
protected:
    void SetUp() override {
        windowManager =
            std::make_unique<Window::WindowManagerUVE>(eventSystem, MakeSkinTestWindowDescUVE());
        if (!windowManager->IsValidUVE()) {
            GTEST_SKIP() << "No display available for GlRenderDeviceUVE - skipping (run under "
                            "xvfb-run to exercise this test)";
        }
        renderDevice = std::make_unique<GlRenderDeviceUVE>(*windowManager);
        if (!renderDevice->IsUsableUVE()) {
            GTEST_SKIP() << "GlRenderDeviceUVE came up unusable on this display";
        }
        computeSystem = std::make_unique<ComputeSystemUVE>(*renderDevice);
        skin = std::make_unique<MeshSkinComputeUVE>(*renderDevice, *computeSystem);

        std::string infoLog;
        if (!skin->InitializeUVE(&infoLog)) {
            GTEST_SKIP() << "context lacks compute shaders (GL 4.3+): " << infoLog;
        }
    }

    Events::EventSystemUVE eventSystem;
    std::unique_ptr<Window::WindowManagerUVE> windowManager;
    std::unique_ptr<GlRenderDeviceUVE> renderDevice;
    std::unique_ptr<ComputeSystemUVE> computeSystem;
    std::unique_ptr<MeshSkinComputeUVE> skin;
};

TEST_F(MeshSkinComputeGlUVETest, SkinUVE_MatchesTheCpuBitForBitOnAnAnimatedPose) {
    // 1000 vertices is not a multiple of the 64-wide workgroup, so the kernel's tail guard runs.
    const Asset::MeshAssetUVE mesh = MakeSkinnedMeshUVE(1000U, 6U);
    std::vector<Math::Matrix4x4UVE> matrices;
    ASSERT_TRUE(Asset::TryResolvePoseUVE(mesh.joints, MakeAwkwardPoseUVE(6U), matrices));

    std::vector<Asset::MeshVertexUVE> onCpu;
    ASSERT_TRUE(Asset::TrySkinMeshUVE(mesh, matrices, onCpu));
    std::vector<Asset::MeshVertexUVE> onGpu;
    ASSERT_TRUE(skin->SkinUVE(mesh, matrices, onGpu));

    ExpectVerticesBitIdenticalUVE(onGpu, onCpu);
}

TEST_F(MeshSkinComputeGlUVETest, SkinUVE_BindPose_ReturnsTheSourceVerticesUnchanged) {
    // The definitive property of an inverse bind matrix, now on the GPU: posing a skeleton in its
    // own bind pose must leave every vertex exactly where it already was. Exact, not near - any
    // drift here means the matrix upload or its transpose is wrong.
    const Asset::MeshAssetUVE mesh = MakeSkinnedMeshUVE(200U, 5U);
    std::vector<Math::Matrix4x4UVE> matrices;
    ASSERT_TRUE(Asset::TryResolvePoseUVE(mesh.joints, MakeBindPoseUVE(5U), matrices));

    std::vector<Asset::MeshVertexUVE> onGpu;
    ASSERT_TRUE(skin->SkinUVE(mesh, matrices, onGpu));
    ExpectVerticesBitIdenticalUVE(onGpu, mesh.vertices);
}

TEST_F(MeshSkinComputeGlUVETest, SkinUVE_TransposeIsCorrect_ATranslatedJointMovesVerticesNotShearsThem) {
    // A transposed matrix upload is the failure this kernel is most exposed to, and it is quiet:
    // with a symmetric pose it produces plausible-looking output. So this uses a pose that is
    // deliberately NOT symmetric - a pure translation, whose transpose would put the translation
    // into the matrix's bottom row where nothing reads it, leaving vertices unmoved.
    Asset::MeshAssetUVE mesh = MakeSkinnedMeshUVE(64U, 1U);
    for (Asset::MeshSkinningInfluenceUVE& influence : mesh.skinningInfluences) {
        influence.joints[0] = 0U;
        influence.weights[0] = 1.0F;
        influence.weights[1] = 0.0F;
        influence.weights[2] = 0.0F;
        influence.weights[3] = 0.0F;
    }
    const std::vector<Math::Matrix4x4UVE> pose{Math::Matrix4x4UVE::ComposeTrsUVE(
        Math::Vector3UVE{3.5F, -1.25F, 7.75F}, Math::QuaternionUVE{},
        Math::Vector3UVE{1.0F, 1.0F, 1.0F})};
    std::vector<Math::Matrix4x4UVE> matrices;
    ASSERT_TRUE(Asset::TryResolvePoseUVE(mesh.joints, pose, matrices));

    std::vector<Asset::MeshVertexUVE> onGpu;
    ASSERT_TRUE(skin->SkinUVE(mesh, matrices, onGpu));
    ASSERT_EQ(onGpu.size(), mesh.vertices.size());
    for (std::size_t index = 0U; index < onGpu.size(); ++index) {
        ExpectBitIdenticalUVE(onGpu[index].position.x, mesh.vertices[index].position.x + 3.5F,
                              "translated position.x", index);
        ExpectBitIdenticalUVE(onGpu[index].position.y, mesh.vertices[index].position.y - 1.25F,
                              "translated position.y", index);
        ExpectBitIdenticalUVE(onGpu[index].position.z, mesh.vertices[index].position.z + 7.75F,
                              "translated position.z", index);
        // Normals are directions: a pure translation must leave them completely alone.
        ExpectBitIdenticalUVE(onGpu[index].normal.x, mesh.vertices[index].normal.x, "normal.x", index);
        ExpectBitIdenticalUVE(onGpu[index].normal.y, mesh.vertices[index].normal.y, "normal.y", index);
        ExpectBitIdenticalUVE(onGpu[index].normal.z, mesh.vertices[index].normal.z, "normal.z", index);
    }
}

TEST_F(MeshSkinComputeGlUVETest, SkinUVE_AgreesAcrossPartialWorkgroupsAndRepeatedCalls) {
    const std::vector<Math::Matrix4x4UVE> pose = MakeAwkwardPoseUVE(4U);
    for (const std::size_t count : {std::size_t{1U}, std::size_t{63U}, std::size_t{64U},
                                    std::size_t{65U}, std::size_t{129U}}) {
        const Asset::MeshAssetUVE mesh = MakeSkinnedMeshUVE(count, 4U);
        std::vector<Math::Matrix4x4UVE> matrices;
        ASSERT_TRUE(Asset::TryResolvePoseUVE(mesh.joints, pose, matrices));

        std::vector<Asset::MeshVertexUVE> onCpu;
        ASSERT_TRUE(Asset::TrySkinMeshUVE(mesh, matrices, onCpu));
        // Twice, to catch a buffer left dirty between calls - the output buffer is reused and is
        // only fully overwritten if the dispatch really covers every vertex.
        for (int pass = 0; pass < 2; ++pass) {
            std::vector<Asset::MeshVertexUVE> onGpu;
            ASSERT_TRUE(skin->SkinUVE(mesh, matrices, onGpu)) << "count " << count << " pass " << pass;
            ExpectVerticesBitIdenticalUVE(onGpu, onCpu);
        }
    }
}

} // namespace
} // namespace UVE::Render::Tests
