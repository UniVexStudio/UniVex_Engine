// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "mesh_skinning.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

#include "mesh_asset.h"
#include "uve_file_envelope.h"
#include "matrix4x4.h"
#include "quaternion.h"
#include "vector3.h"

namespace UVE::Asset::Tests {
namespace {

constexpr float kTolerance = 1.0e-5F;

void ExpectVectorNear(const Math::Vector3& actual, const Math::Vector3& expected,
                         const char* const what) {
    EXPECT_NEAR(actual.x, expected.x, kTolerance) << what << ".x";
    EXPECT_NEAR(actual.y, expected.y, kTolerance) << what << ".y";
    EXPECT_NEAR(actual.z, expected.z, kTolerance) << what << ".z";
}

/// A two-joint chain along +X: root at the origin, child at (1,0,0). The bind pose is the identity
/// chain, so each inverse bind matrix is just the inverse of that joint's model-space bind
/// transform - computed here by construction rather than by inverting, so the fixture itself never
/// depends on the inverse being right.
[[nodiscard]] std::vector<MeshJoint> MakeTwoJointChain() {
    std::vector<MeshJoint> joints(2U);

    joints[0].parentIndex = kInvalidJointParent;
    joints[0].inverseBindMatrix = Math::Matrix4x4::Identity();

    // Child's bind transform is a translation of +1 on X, so its inverse is -1 on X.
    joints[1].parentIndex = 0U;
    joints[1].inverseBindMatrix = Math::Matrix4x4::ComposeTrs(
        Math::Vector3{-1.0F, 0.0F, 0.0F}, Math::Quaternion{}, Math::Vector3{1.0F, 1.0F, 1.0F});
    return joints;
}

/// The bind pose of the chain above: root identity, child translated +1 on X.
[[nodiscard]] std::vector<Math::Matrix4x4> MakeBindPose() {
    return {Math::Matrix4x4::Identity(),
            Math::Matrix4x4::ComposeTrs(Math::Vector3{1.0F, 0.0F, 0.0F},
                                              Math::Quaternion{},
                                              Math::Vector3{1.0F, 1.0F, 1.0F})};
}

[[nodiscard]] MeshSkinningInfluence MakeInfluence(const std::uint32_t jointA, const float weightA,
                                                        const std::uint32_t jointB,
                                                        const float weightB) {
    MeshSkinningInfluence influence;
    influence.joints[0] = jointA;
    influence.weights[0] = weightA;
    influence.joints[1] = jointB;
    influence.weights[1] = weightB;
    return influence;
}

/// Three vertices: one fully on the root, one fully on the child, one split evenly - the three
/// cases whose behaviour differs, rather than three arbitrary vertices.
[[nodiscard]] MeshAsset MakeSkinnedMesh() {
    MeshAsset mesh;
    mesh.vertices = {
        MeshVertex{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F, 0.0F},
        MeshVertex{Math::Vector3{2.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 1.0F, 0.0F},
        MeshVertex{Math::Vector3{1.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 0.5F, 1.0F},
    };
    mesh.indices = {0U, 1U, 2U};
    mesh.localBounds =
        Math::Aabb{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{2.0F, 0.0F, 0.0F}};
    mesh.joints = MakeTwoJointChain();
    mesh.skinningInfluences = {
        MakeInfluence(0U, 1.0F, 0U, 0.0F),
        MakeInfluence(1U, 1.0F, 0U, 0.0F),
        MakeInfluence(0U, 0.5F, 1U, 0.5F),
    };
    return mesh;
}

// --- the data model --------------------------------------------------------------------------

TEST(MeshSkinningTest, StaticMesh_HasNoSkinningDataAndIsStillValid) {
    // Most meshes are static, and a validator that rejected them would make every caller
    // special-case the common path.
    MeshAsset mesh;
    mesh.vertices.resize(3U);
    mesh.indices = {0U, 1U, 2U};
    EXPECT_FALSE(mesh.IsSkinned());
    EXPECT_TRUE(IsMeshSkinningDataValid(mesh));
}

TEST(MeshSkinningTest, HalfPresentSkinningData_IsRejected) {
    // Joints without influences, or influences without joints, is not a state the renderer has any
    // answer for - better to refuse it at the door than to discover it mid-frame.
    MeshAsset withJointsOnly = MakeSkinnedMesh();
    withJointsOnly.skinningInfluences.clear();
    EXPECT_FALSE(IsMeshSkinningDataValid(withJointsOnly));

    MeshAsset withInfluencesOnly = MakeSkinnedMesh();
    withInfluencesOnly.joints.clear();
    EXPECT_FALSE(IsMeshSkinningDataValid(withInfluencesOnly));
}

TEST(MeshSkinningTest, PartiallySkinnedMesh_IsRejected) {
    MeshAsset mesh = MakeSkinnedMesh();
    mesh.skinningInfluences.pop_back();
    EXPECT_FALSE(IsMeshSkinningDataValid(mesh));
}

TEST(MeshSkinningTest, OutOfRangeJointIndex_IsRejectedEvenAtZeroWeight) {
    // The zero weight would cancel the contribution arithmetically, but a GPU path indexing a
    // joint array with this value reads out of bounds regardless of what it then multiplies by.
    MeshAsset mesh = MakeSkinnedMesh();
    mesh.skinningInfluences[0].joints[3] = 99U;
    mesh.skinningInfluences[0].weights[3] = 0.0F;
    EXPECT_FALSE(IsMeshSkinningDataValid(mesh));
}

TEST(MeshSkinningTest, WeightsThatDoNotSumToOne_AreRejectedRatherThanRenormalized) {
    // Deliberately not fixed up here: quietly renormalizing would hide an asset defect from the
    // importer that could correct it at the source.
    MeshAsset mesh = MakeSkinnedMesh();
    mesh.skinningInfluences[2].weights[0] = 0.5F;
    mesh.skinningInfluences[2].weights[1] = 0.2F;
    EXPECT_FALSE(IsSkinningInfluenceNormalized(mesh.skinningInfluences[2]));
    EXPECT_FALSE(IsMeshSkinningDataValid(mesh));
}

TEST(MeshSkinningTest, NonFiniteOrNegativeWeights_AreRejected) {
    MeshAsset nan = MakeSkinnedMesh();
    nan.skinningInfluences[0].weights[0] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsMeshSkinningDataValid(nan));

    // A negative weight can still sum to one across the set, so the sum check alone would let it
    // through - it needs its own rejection.
    MeshAsset negative = MakeSkinnedMesh();
    negative.skinningInfluences[2].weights[0] = -0.5F;
    negative.skinningInfluences[2].weights[1] = 1.5F;
    EXPECT_TRUE(IsSkinningInfluenceNormalized(negative.skinningInfluences[2]));
    EXPECT_FALSE(IsMeshSkinningDataValid(negative));
}

TEST(MeshSkinningTest, SkeletonOrdering_RequiresParentsBeforeChildren) {
    std::vector<MeshJoint> ordered = MakeTwoJointChain();
    EXPECT_TRUE(IsSkeletonTopologicallyOrdered(ordered));

    // A forward reference: joint 0 parented to joint 1.
    std::vector<MeshJoint> forwardReference = ordered;
    forwardReference[0].parentIndex = 1U;
    EXPECT_FALSE(IsSkeletonTopologicallyOrdered(forwardReference));

    // Self-parenting is caught by the same `parent >= index` comparison - a one-joint cycle.
    std::vector<MeshJoint> selfParented = ordered;
    selfParented[1].parentIndex = 1U;
    EXPECT_FALSE(IsSkeletonTopologicallyOrdered(selfParented));
}

// --- pose resolution -------------------------------------------------------------------------

TEST(MeshSkinningTest, TryResolvePose_BindPose_ProducesIdentitySkinningMatrices) {
    // The definitive property of an inverse bind matrix: posing a skeleton in its own bind pose
    // must skin every vertex to exactly where it already is. If this fails, nothing downstream can
    // be trusted.
    const std::vector<MeshJoint> joints = MakeTwoJointChain();
    const std::vector<Math::Matrix4x4> bindPose = MakeBindPose();

    std::vector<Math::Matrix4x4> skinning;
    ASSERT_TRUE(TryResolvePose(joints, bindPose, skinning));
    ASSERT_EQ(skinning.size(), 2U);

    const Math::Matrix4x4 identity = Math::Matrix4x4::Identity();
    for (std::size_t jointIndex = 0U; jointIndex < skinning.size(); ++jointIndex) {
        for (std::size_t row = 0U; row < 4U; ++row) {
            for (std::size_t column = 0U; column < 4U; ++column) {
                EXPECT_NEAR(skinning[jointIndex].m[row][column], identity.m[row][column],
                            kTolerance)
                    << "joint " << jointIndex << " element [" << row << "][" << column << "]";
            }
        }
    }
}

TEST(MeshSkinningTest, TryResolvePose_ChildInheritsItsParentTransform) {
    // The reason a pose is resolved at all rather than used directly: a child's local transform is
    // relative to its parent, so moving the root must move the child too.
    const std::vector<MeshJoint> joints = MakeTwoJointChain();
    std::vector<Math::Matrix4x4> pose = MakeBindPose();
    pose[0] = Math::Matrix4x4::ComposeTrs(Math::Vector3{0.0F, 3.0F, 0.0F},
                                                Math::Quaternion{},
                                                Math::Vector3{1.0F, 1.0F, 1.0F});

    std::vector<Math::Matrix4x4> skinning;
    ASSERT_TRUE(TryResolvePose(joints, pose, skinning));

    MeshAsset mesh = MakeSkinnedMesh();
    std::vector<MeshVertex> skinned;
    ASSERT_TRUE(TrySkinMesh(mesh, skinning, skinned));

    // Every vertex rises by 3 - including the one bound entirely to the child, which received the
    // motion only through its parent.
    ExpectVectorNear(skinned[0].position, Math::Vector3{0.0F, 3.0F, 0.0F}, "root-bound");
    ExpectVectorNear(skinned[1].position, Math::Vector3{2.0F, 3.0F, 0.0F}, "child-bound");
    ExpectVectorNear(skinned[2].position, Math::Vector3{1.0F, 3.0F, 0.0F}, "blended");
}

TEST(MeshSkinningTest, TryResolvePose_RejectsMismatchedPoseSizeAndUnorderedSkeleton) {
    const std::vector<MeshJoint> joints = MakeTwoJointChain();
    std::vector<Math::Matrix4x4> skinning{Math::Matrix4x4::Identity()};

    const std::vector<Math::Matrix4x4> shortPose{Math::Matrix4x4::Identity()};
    EXPECT_FALSE(TryResolvePose(joints, shortPose, skinning));
    // The output must be left alone on failure, not half-filled.
    EXPECT_EQ(skinning.size(), 1U);

    std::vector<MeshJoint> unordered = joints;
    unordered[0].parentIndex = 1U;
    EXPECT_FALSE(TryResolvePose(unordered, MakeBindPose(), skinning));
    EXPECT_EQ(skinning.size(), 1U);
}

// --- skinning --------------------------------------------------------------------------------

TEST(MeshSkinningTest, TrySkinMesh_BindPose_IsTheIdentityOnEveryVertex) {
    const MeshAsset mesh = MakeSkinnedMesh();
    std::vector<Math::Matrix4x4> skinning;
    ASSERT_TRUE(TryResolvePose(mesh.joints, MakeBindPose(), skinning));

    std::vector<MeshVertex> skinned;
    ASSERT_TRUE(TrySkinMesh(mesh, skinning, skinned));
    ASSERT_EQ(skinned.size(), mesh.vertices.size());
    for (std::size_t index = 0U; index < skinned.size(); ++index) {
        ExpectVectorNear(skinned[index].position, mesh.vertices[index].position, "position");
        ExpectVectorNear(skinned[index].normal, mesh.vertices[index].normal, "normal");
    }
}

TEST(MeshSkinningTest, TrySkinMesh_BlendsTwoJointsByWeight) {
    // The one case linear blend skinning exists for. The child is translated +2 on X while the
    // root stays put; the half-and-half vertex must land halfway between the two answers, which is
    // what "linear blend" means and what a GPU kernel will have to reproduce exactly.
    MeshAsset mesh = MakeSkinnedMesh();
    std::vector<Math::Matrix4x4> pose = MakeBindPose();
    pose[1] = Math::Matrix4x4::ComposeTrs(Math::Vector3{3.0F, 0.0F, 0.0F},
                                                Math::Quaternion{},
                                                Math::Vector3{1.0F, 1.0F, 1.0F});

    std::vector<Math::Matrix4x4> skinning;
    ASSERT_TRUE(TryResolvePose(mesh.joints, pose, skinning));
    std::vector<MeshVertex> skinned;
    ASSERT_TRUE(TrySkinMesh(mesh, skinning, skinned));

    // Root-bound vertex: untouched.
    ExpectVectorNear(skinned[0].position, Math::Vector3{0.0F, 0.0F, 0.0F}, "root-bound");
    // Child-bound vertex at x=2: the child moved from x=1 to x=3, so +2.
    ExpectVectorNear(skinned[1].position, Math::Vector3{4.0F, 0.0F, 0.0F}, "child-bound");
    // Half-and-half vertex at x=1: half of nothing and half of +2 is +1.
    ExpectVectorNear(skinned[2].position, Math::Vector3{2.0F, 0.0F, 0.0F}, "blended");
}

TEST(MeshSkinningTest, TrySkinMesh_RotatesNormalsWithoutTranslatingThem) {
    // The distinction that a point/direction mix-up destroys: this joint both rotates and
    // translates, and a normal run through the POINT transform would come out displaced by the
    // translation - roughly pointing at the joint rather than away from the surface. The position
    // assertion below pins the translation, the normal assertion pins its absence.
    MeshAsset mesh = MakeSkinnedMesh();
    std::vector<Math::Matrix4x4> pose = MakeBindPose();
    // A quarter turn about +Z takes +Y to -X, plus a large translation to make a leak obvious.
    Math::Quaternion quarterTurn;
    ASSERT_TRUE(Math::TryMakeAxisAngle(Math::Vector3{0.0F, 0.0F, 1.0F}, 1.5707963F,
                                          quarterTurn));
    pose[0] = Math::Matrix4x4::ComposeTrs(Math::Vector3{10.0F, 0.0F, 0.0F}, quarterTurn,
                                                Math::Vector3{1.0F, 1.0F, 1.0F});

    std::vector<Math::Matrix4x4> skinning;
    ASSERT_TRUE(TryResolvePose(mesh.joints, pose, skinning));
    std::vector<MeshVertex> skinned;
    ASSERT_TRUE(TrySkinMesh(mesh, skinning, skinned));

    // Vertex 0 sits at the origin and is bound entirely to the root, so it lands on the
    // translation itself.
    ExpectVectorNear(skinned[0].position, Math::Vector3{10.0F, 0.0F, 0.0F}, "position");
    // Its normal was +Y and must be exactly -X: rotated, not translated. Had the translation
    // leaked in, this would read about (9, 0, 0).
    ExpectVectorNear(skinned[0].normal, Math::Vector3{-1.0F, 0.0F, 0.0F}, "normal");
}

TEST(MeshSkinningTest, TrySkinMesh_RefusesStaticMeshesAndMismatchedMatrixCounts) {
    MeshAsset staticMesh;
    staticMesh.vertices.resize(3U);
    staticMesh.indices = {0U, 1U, 2U};
    std::vector<MeshVertex> out;
    EXPECT_FALSE(TrySkinMesh(staticMesh, std::vector<Math::Matrix4x4>{}, out));

    const MeshAsset mesh = MakeSkinnedMesh();
    const std::vector<Math::Matrix4x4> tooFew{Math::Matrix4x4::Identity()};
    EXPECT_FALSE(TrySkinMesh(mesh, tooFew, out));
    EXPECT_TRUE(out.empty());
}

// --- serialization ---------------------------------------------------------------------------

TEST(MeshSkinningTest, SaveAndLoad_RoundTripSkinningData) {
    const MeshAsset original = MakeSkinnedMesh();
    const std::filesystem::path path =
        ::UVE::Tests::ScratchRoot() / "uve_skinned_roundtrip.uvmodel";
    ASSERT_TRUE(SaveMeshAsset(original, path));

    MeshAsset loaded;
    ASSERT_TRUE(LoadMeshAsset(path, loaded));
    ASSERT_TRUE(loaded.IsSkinned());
    ASSERT_EQ(loaded.joints.size(), original.joints.size());
    ASSERT_EQ(loaded.skinningInfluences.size(), original.skinningInfluences.size());

    for (std::size_t jointIndex = 0U; jointIndex < original.joints.size(); ++jointIndex) {
        EXPECT_EQ(loaded.joints[jointIndex].parentIndex, original.joints[jointIndex].parentIndex);
        for (std::size_t row = 0U; row < 4U; ++row) {
            for (std::size_t column = 0U; column < 4U; ++column) {
                // Exact equality, not near: these are the same floats written and read back, and a
                // bit-level difference here would silently desynchronize CPU and GPU skinning.
                EXPECT_EQ(loaded.joints[jointIndex].inverseBindMatrix.m[row][column],
                          original.joints[jointIndex].inverseBindMatrix.m[row][column]);
            }
        }
    }
    for (std::size_t index = 0U; index < original.skinningInfluences.size(); ++index) {
        for (std::size_t slot = 0U; slot < kMaxJointInfluences; ++slot) {
            EXPECT_EQ(loaded.skinningInfluences[index].joints[slot],
                      original.skinningInfluences[index].joints[slot]);
            EXPECT_EQ(loaded.skinningInfluences[index].weights[slot],
                      original.skinningInfluences[index].weights[slot]);
        }
    }
    std::filesystem::remove(path);
}

TEST(MeshSkinningTest, StaticMeshBytes_AreUnchangedByTheSkinningSection) {
    // The compatibility guarantee, asserted rather than assumed: the skinning section is optional
    // and trails the payload, so a static mesh must serialize to exactly the bytes this engine
    // produced before skinning existed. The envelope's version field is global across all asset
    // kinds, so bumping it was never an option - every scene and texture would have been
    // invalidated to describe a mesh feature.
    MeshAsset staticMesh;
    staticMesh.vertices = {
        MeshVertex{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F, 0.0F},
        MeshVertex{Math::Vector3{1.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 1.0F, 0.0F},
        MeshVertex{Math::Vector3{0.0F, 1.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F, 1.0F},
    };
    staticMesh.indices = {0U, 1U, 2U};
    staticMesh.localBounds =
        Math::Aabb{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 0.0F}};

    // 3 vertices * 8 floats, + a vertex count, + an index count and 3 indices, + 6 bounds floats.
    const std::size_t expectedPayloadBytes = sizeof(std::uint32_t) + 3U * 8U * sizeof(float) +
                                             sizeof(std::uint32_t) + 3U * sizeof(std::uint32_t) +
                                             6U * sizeof(float);

    const std::filesystem::path path =
        ::UVE::Tests::ScratchRoot() / "uve_static_compat.uvmodel";
    ASSERT_TRUE(SaveMeshAsset(staticMesh, path));
    const std::uintmax_t fileBytes = std::filesystem::file_size(path);

    // The header size is internal to the envelope, so it is measured rather than restated here:
    // an empty-payload envelope IS the header. Restating the layout would make this test fail for
    // an unrelated envelope change instead of for the thing it is guarding.
    const std::size_t headerBytes = EncodeUveFileEnvelope(AssetKind::Mesh, {}).size();
    EXPECT_EQ(fileBytes, headerBytes + expectedPayloadBytes)
        << "a static mesh grew on disk - the skinning section must be written only when skinned";

    MeshAsset loaded;
    ASSERT_TRUE(LoadMeshAsset(path, loaded));
    EXPECT_FALSE(loaded.IsSkinned());
    EXPECT_EQ(loaded.vertices.size(), 3U);
    std::filesystem::remove(path);
}

TEST(MeshSkinningTest, Load_RejectsStructurallyInvalidSkinningDataWithoutTouchingTheOutput) {
    // Save bypasses validation by design (it serializes what it is given), so a hand-built bad
    // file is reachable - and the loader is where it must be caught.
    MeshAsset bad = MakeSkinnedMesh();
    bad.skinningInfluences[0].joints[0] = 7U; // no such joint

    const std::filesystem::path path =
        ::UVE::Tests::ScratchRoot() / "uve_bad_skinning.uvmodel";
    ASSERT_TRUE(SaveMeshAsset(bad, path));

    MeshAsset loaded;
    loaded.vertices.resize(1U);
    EXPECT_FALSE(LoadMeshAsset(path, loaded));
    // Untouched on rejection, matching the loader's existing failure-atomic contract.
    EXPECT_EQ(loaded.vertices.size(), 1U);
    std::filesystem::remove(path);
}

} // namespace
} // namespace UVE::Asset::Tests
