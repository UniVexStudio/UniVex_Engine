// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/asset/mesh_skinning_uve.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch_uve.h"

#include "uve/asset/mesh_asset_uve.h"
#include "uve/asset/uve_file_envelope_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Asset::Tests {
namespace {

constexpr float kToleranceUVE = 1.0e-5F;

void ExpectVectorNearUVE(const Math::Vector3UVE& actual, const Math::Vector3UVE& expected,
                         const char* const what) {
    EXPECT_NEAR(actual.x, expected.x, kToleranceUVE) << what << ".x";
    EXPECT_NEAR(actual.y, expected.y, kToleranceUVE) << what << ".y";
    EXPECT_NEAR(actual.z, expected.z, kToleranceUVE) << what << ".z";
}

/// A two-joint chain along +X: root at the origin, child at (1,0,0). The bind pose is the identity
/// chain, so each inverse bind matrix is just the inverse of that joint's model-space bind
/// transform - computed here by construction rather than by inverting, so the fixture itself never
/// depends on the inverse being right.
[[nodiscard]] std::vector<MeshJointUVE> MakeTwoJointChainUVE() {
    std::vector<MeshJointUVE> joints(2U);

    joints[0].parentIndex = kInvalidJointParentUVE;
    joints[0].inverseBindMatrix = Math::Matrix4x4UVE::IdentityUVE();

    // Child's bind transform is a translation of +1 on X, so its inverse is -1 on X.
    joints[1].parentIndex = 0U;
    joints[1].inverseBindMatrix = Math::Matrix4x4UVE::ComposeTrsUVE(
        Math::Vector3UVE{-1.0F, 0.0F, 0.0F}, Math::QuaternionUVE{}, Math::Vector3UVE{1.0F, 1.0F, 1.0F});
    return joints;
}

/// The bind pose of the chain above: root identity, child translated +1 on X.
[[nodiscard]] std::vector<Math::Matrix4x4UVE> MakeBindPoseUVE() {
    return {Math::Matrix4x4UVE::IdentityUVE(),
            Math::Matrix4x4UVE::ComposeTrsUVE(Math::Vector3UVE{1.0F, 0.0F, 0.0F},
                                              Math::QuaternionUVE{},
                                              Math::Vector3UVE{1.0F, 1.0F, 1.0F})};
}

[[nodiscard]] MeshSkinningInfluenceUVE MakeInfluenceUVE(const std::uint32_t jointA, const float weightA,
                                                        const std::uint32_t jointB,
                                                        const float weightB) {
    MeshSkinningInfluenceUVE influence;
    influence.joints[0] = jointA;
    influence.weights[0] = weightA;
    influence.joints[1] = jointB;
    influence.weights[1] = weightB;
    return influence;
}

/// Three vertices: one fully on the root, one fully on the child, one split evenly - the three
/// cases whose behaviour differs, rather than three arbitrary vertices.
[[nodiscard]] MeshAssetUVE MakeSkinnedMeshUVE() {
    MeshAssetUVE mesh;
    mesh.vertices = {
        MeshVertexUVE{Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F, 0.0F},
        MeshVertexUVE{Math::Vector3UVE{2.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 1.0F, 0.0F},
        MeshVertexUVE{Math::Vector3UVE{1.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.5F, 1.0F},
    };
    mesh.indices = {0U, 1U, 2U};
    mesh.localBounds =
        Math::AabbUVE{Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{2.0F, 0.0F, 0.0F}};
    mesh.joints = MakeTwoJointChainUVE();
    mesh.skinningInfluences = {
        MakeInfluenceUVE(0U, 1.0F, 0U, 0.0F),
        MakeInfluenceUVE(1U, 1.0F, 0U, 0.0F),
        MakeInfluenceUVE(0U, 0.5F, 1U, 0.5F),
    };
    return mesh;
}

// --- the data model --------------------------------------------------------------------------

TEST(MeshSkinningUVETest, StaticMesh_HasNoSkinningDataAndIsStillValid) {
    // Most meshes are static, and a validator that rejected them would make every caller
    // special-case the common path.
    MeshAssetUVE mesh;
    mesh.vertices.resize(3U);
    mesh.indices = {0U, 1U, 2U};
    EXPECT_FALSE(mesh.IsSkinnedUVE());
    EXPECT_TRUE(IsMeshSkinningDataValidUVE(mesh));
}

TEST(MeshSkinningUVETest, HalfPresentSkinningData_IsRejected) {
    // Joints without influences, or influences without joints, is not a state the renderer has any
    // answer for - better to refuse it at the door than to discover it mid-frame.
    MeshAssetUVE withJointsOnly = MakeSkinnedMeshUVE();
    withJointsOnly.skinningInfluences.clear();
    EXPECT_FALSE(IsMeshSkinningDataValidUVE(withJointsOnly));

    MeshAssetUVE withInfluencesOnly = MakeSkinnedMeshUVE();
    withInfluencesOnly.joints.clear();
    EXPECT_FALSE(IsMeshSkinningDataValidUVE(withInfluencesOnly));
}

TEST(MeshSkinningUVETest, PartiallySkinnedMesh_IsRejected) {
    MeshAssetUVE mesh = MakeSkinnedMeshUVE();
    mesh.skinningInfluences.pop_back();
    EXPECT_FALSE(IsMeshSkinningDataValidUVE(mesh));
}

TEST(MeshSkinningUVETest, OutOfRangeJointIndex_IsRejectedEvenAtZeroWeight) {
    // The zero weight would cancel the contribution arithmetically, but a GPU path indexing a
    // joint array with this value reads out of bounds regardless of what it then multiplies by.
    MeshAssetUVE mesh = MakeSkinnedMeshUVE();
    mesh.skinningInfluences[0].joints[3] = 99U;
    mesh.skinningInfluences[0].weights[3] = 0.0F;
    EXPECT_FALSE(IsMeshSkinningDataValidUVE(mesh));
}

TEST(MeshSkinningUVETest, WeightsThatDoNotSumToOne_AreRejectedRatherThanRenormalized) {
    // Deliberately not fixed up here: quietly renormalizing would hide an asset defect from the
    // importer that could correct it at the source.
    MeshAssetUVE mesh = MakeSkinnedMeshUVE();
    mesh.skinningInfluences[2].weights[0] = 0.5F;
    mesh.skinningInfluences[2].weights[1] = 0.2F;
    EXPECT_FALSE(IsSkinningInfluenceNormalizedUVE(mesh.skinningInfluences[2]));
    EXPECT_FALSE(IsMeshSkinningDataValidUVE(mesh));
}

TEST(MeshSkinningUVETest, NonFiniteOrNegativeWeights_AreRejected) {
    MeshAssetUVE nan = MakeSkinnedMeshUVE();
    nan.skinningInfluences[0].weights[0] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsMeshSkinningDataValidUVE(nan));

    // A negative weight can still sum to one across the set, so the sum check alone would let it
    // through - it needs its own rejection.
    MeshAssetUVE negative = MakeSkinnedMeshUVE();
    negative.skinningInfluences[2].weights[0] = -0.5F;
    negative.skinningInfluences[2].weights[1] = 1.5F;
    EXPECT_TRUE(IsSkinningInfluenceNormalizedUVE(negative.skinningInfluences[2]));
    EXPECT_FALSE(IsMeshSkinningDataValidUVE(negative));
}

TEST(MeshSkinningUVETest, SkeletonOrdering_RequiresParentsBeforeChildren) {
    std::vector<MeshJointUVE> ordered = MakeTwoJointChainUVE();
    EXPECT_TRUE(IsSkeletonTopologicallyOrderedUVE(ordered));

    // A forward reference: joint 0 parented to joint 1.
    std::vector<MeshJointUVE> forwardReference = ordered;
    forwardReference[0].parentIndex = 1U;
    EXPECT_FALSE(IsSkeletonTopologicallyOrderedUVE(forwardReference));

    // Self-parenting is caught by the same `parent >= index` comparison - a one-joint cycle.
    std::vector<MeshJointUVE> selfParented = ordered;
    selfParented[1].parentIndex = 1U;
    EXPECT_FALSE(IsSkeletonTopologicallyOrderedUVE(selfParented));
}

// --- pose resolution -------------------------------------------------------------------------

TEST(MeshSkinningUVETest, TryResolvePoseUVE_BindPose_ProducesIdentitySkinningMatrices) {
    // The definitive property of an inverse bind matrix: posing a skeleton in its own bind pose
    // must skin every vertex to exactly where it already is. If this fails, nothing downstream can
    // be trusted.
    const std::vector<MeshJointUVE> joints = MakeTwoJointChainUVE();
    const std::vector<Math::Matrix4x4UVE> bindPose = MakeBindPoseUVE();

    std::vector<Math::Matrix4x4UVE> skinning;
    ASSERT_TRUE(TryResolvePoseUVE(joints, bindPose, skinning));
    ASSERT_EQ(skinning.size(), 2U);

    const Math::Matrix4x4UVE identity = Math::Matrix4x4UVE::IdentityUVE();
    for (std::size_t jointIndex = 0U; jointIndex < skinning.size(); ++jointIndex) {
        for (std::size_t row = 0U; row < 4U; ++row) {
            for (std::size_t column = 0U; column < 4U; ++column) {
                EXPECT_NEAR(skinning[jointIndex].m[row][column], identity.m[row][column],
                            kToleranceUVE)
                    << "joint " << jointIndex << " element [" << row << "][" << column << "]";
            }
        }
    }
}

TEST(MeshSkinningUVETest, TryResolvePoseUVE_ChildInheritsItsParentTransform) {
    // The reason a pose is resolved at all rather than used directly: a child's local transform is
    // relative to its parent, so moving the root must move the child too.
    const std::vector<MeshJointUVE> joints = MakeTwoJointChainUVE();
    std::vector<Math::Matrix4x4UVE> pose = MakeBindPoseUVE();
    pose[0] = Math::Matrix4x4UVE::ComposeTrsUVE(Math::Vector3UVE{0.0F, 3.0F, 0.0F},
                                                Math::QuaternionUVE{},
                                                Math::Vector3UVE{1.0F, 1.0F, 1.0F});

    std::vector<Math::Matrix4x4UVE> skinning;
    ASSERT_TRUE(TryResolvePoseUVE(joints, pose, skinning));

    MeshAssetUVE mesh = MakeSkinnedMeshUVE();
    std::vector<MeshVertexUVE> skinned;
    ASSERT_TRUE(TrySkinMeshUVE(mesh, skinning, skinned));

    // Every vertex rises by 3 - including the one bound entirely to the child, which received the
    // motion only through its parent.
    ExpectVectorNearUVE(skinned[0].position, Math::Vector3UVE{0.0F, 3.0F, 0.0F}, "root-bound");
    ExpectVectorNearUVE(skinned[1].position, Math::Vector3UVE{2.0F, 3.0F, 0.0F}, "child-bound");
    ExpectVectorNearUVE(skinned[2].position, Math::Vector3UVE{1.0F, 3.0F, 0.0F}, "blended");
}

TEST(MeshSkinningUVETest, TryResolvePoseUVE_RejectsMismatchedPoseSizeAndUnorderedSkeleton) {
    const std::vector<MeshJointUVE> joints = MakeTwoJointChainUVE();
    std::vector<Math::Matrix4x4UVE> skinning{Math::Matrix4x4UVE::IdentityUVE()};

    const std::vector<Math::Matrix4x4UVE> shortPose{Math::Matrix4x4UVE::IdentityUVE()};
    EXPECT_FALSE(TryResolvePoseUVE(joints, shortPose, skinning));
    // The output must be left alone on failure, not half-filled.
    EXPECT_EQ(skinning.size(), 1U);

    std::vector<MeshJointUVE> unordered = joints;
    unordered[0].parentIndex = 1U;
    EXPECT_FALSE(TryResolvePoseUVE(unordered, MakeBindPoseUVE(), skinning));
    EXPECT_EQ(skinning.size(), 1U);
}

// --- skinning --------------------------------------------------------------------------------

TEST(MeshSkinningUVETest, TrySkinMeshUVE_BindPose_IsTheIdentityOnEveryVertex) {
    const MeshAssetUVE mesh = MakeSkinnedMeshUVE();
    std::vector<Math::Matrix4x4UVE> skinning;
    ASSERT_TRUE(TryResolvePoseUVE(mesh.joints, MakeBindPoseUVE(), skinning));

    std::vector<MeshVertexUVE> skinned;
    ASSERT_TRUE(TrySkinMeshUVE(mesh, skinning, skinned));
    ASSERT_EQ(skinned.size(), mesh.vertices.size());
    for (std::size_t index = 0U; index < skinned.size(); ++index) {
        ExpectVectorNearUVE(skinned[index].position, mesh.vertices[index].position, "position");
        ExpectVectorNearUVE(skinned[index].normal, mesh.vertices[index].normal, "normal");
    }
}

TEST(MeshSkinningUVETest, TrySkinMeshUVE_BlendsTwoJointsByWeight) {
    // The one case linear blend skinning exists for. The child is translated +2 on X while the
    // root stays put; the half-and-half vertex must land halfway between the two answers, which is
    // what "linear blend" means and what a GPU kernel will have to reproduce exactly.
    MeshAssetUVE mesh = MakeSkinnedMeshUVE();
    std::vector<Math::Matrix4x4UVE> pose = MakeBindPoseUVE();
    pose[1] = Math::Matrix4x4UVE::ComposeTrsUVE(Math::Vector3UVE{3.0F, 0.0F, 0.0F},
                                                Math::QuaternionUVE{},
                                                Math::Vector3UVE{1.0F, 1.0F, 1.0F});

    std::vector<Math::Matrix4x4UVE> skinning;
    ASSERT_TRUE(TryResolvePoseUVE(mesh.joints, pose, skinning));
    std::vector<MeshVertexUVE> skinned;
    ASSERT_TRUE(TrySkinMeshUVE(mesh, skinning, skinned));

    // Root-bound vertex: untouched.
    ExpectVectorNearUVE(skinned[0].position, Math::Vector3UVE{0.0F, 0.0F, 0.0F}, "root-bound");
    // Child-bound vertex at x=2: the child moved from x=1 to x=3, so +2.
    ExpectVectorNearUVE(skinned[1].position, Math::Vector3UVE{4.0F, 0.0F, 0.0F}, "child-bound");
    // Half-and-half vertex at x=1: half of nothing and half of +2 is +1.
    ExpectVectorNearUVE(skinned[2].position, Math::Vector3UVE{2.0F, 0.0F, 0.0F}, "blended");
}

TEST(MeshSkinningUVETest, TrySkinMeshUVE_RotatesNormalsWithoutTranslatingThem) {
    // The distinction that a point/direction mix-up destroys: this joint both rotates and
    // translates, and a normal run through the POINT transform would come out displaced by the
    // translation - roughly pointing at the joint rather than away from the surface. The position
    // assertion below pins the translation, the normal assertion pins its absence.
    MeshAssetUVE mesh = MakeSkinnedMeshUVE();
    std::vector<Math::Matrix4x4UVE> pose = MakeBindPoseUVE();
    // A quarter turn about +Z takes +Y to -X, plus a large translation to make a leak obvious.
    Math::QuaternionUVE quarterTurn;
    ASSERT_TRUE(Math::TryMakeAxisAngleUVE(Math::Vector3UVE{0.0F, 0.0F, 1.0F}, 1.5707963F,
                                          quarterTurn));
    pose[0] = Math::Matrix4x4UVE::ComposeTrsUVE(Math::Vector3UVE{10.0F, 0.0F, 0.0F}, quarterTurn,
                                                Math::Vector3UVE{1.0F, 1.0F, 1.0F});

    std::vector<Math::Matrix4x4UVE> skinning;
    ASSERT_TRUE(TryResolvePoseUVE(mesh.joints, pose, skinning));
    std::vector<MeshVertexUVE> skinned;
    ASSERT_TRUE(TrySkinMeshUVE(mesh, skinning, skinned));

    // Vertex 0 sits at the origin and is bound entirely to the root, so it lands on the
    // translation itself.
    ExpectVectorNearUVE(skinned[0].position, Math::Vector3UVE{10.0F, 0.0F, 0.0F}, "position");
    // Its normal was +Y and must be exactly -X: rotated, not translated. Had the translation
    // leaked in, this would read about (9, 0, 0).
    ExpectVectorNearUVE(skinned[0].normal, Math::Vector3UVE{-1.0F, 0.0F, 0.0F}, "normal");
}

TEST(MeshSkinningUVETest, TrySkinMeshUVE_RefusesStaticMeshesAndMismatchedMatrixCounts) {
    MeshAssetUVE staticMesh;
    staticMesh.vertices.resize(3U);
    staticMesh.indices = {0U, 1U, 2U};
    std::vector<MeshVertexUVE> out;
    EXPECT_FALSE(TrySkinMeshUVE(staticMesh, std::vector<Math::Matrix4x4UVE>{}, out));

    const MeshAssetUVE mesh = MakeSkinnedMeshUVE();
    const std::vector<Math::Matrix4x4UVE> tooFew{Math::Matrix4x4UVE::IdentityUVE()};
    EXPECT_FALSE(TrySkinMeshUVE(mesh, tooFew, out));
    EXPECT_TRUE(out.empty());
}

// --- serialization ---------------------------------------------------------------------------

TEST(MeshSkinningUVETest, SaveAndLoad_RoundTripSkinningData) {
    const MeshAssetUVE original = MakeSkinnedMeshUVE();
    const std::filesystem::path path =
        ::UVE::Tests::ScratchRootUVE() / "uve_skinned_roundtrip.uvemodel";
    ASSERT_TRUE(SaveMeshAssetUVE(original, path));

    MeshAssetUVE loaded;
    ASSERT_TRUE(LoadMeshAssetUVE(path, loaded));
    ASSERT_TRUE(loaded.IsSkinnedUVE());
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
        for (std::size_t slot = 0U; slot < kMaxJointInfluencesUVE; ++slot) {
            EXPECT_EQ(loaded.skinningInfluences[index].joints[slot],
                      original.skinningInfluences[index].joints[slot]);
            EXPECT_EQ(loaded.skinningInfluences[index].weights[slot],
                      original.skinningInfluences[index].weights[slot]);
        }
    }
    std::filesystem::remove(path);
}

TEST(MeshSkinningUVETest, StaticMeshBytes_AreUnchangedByTheSkinningSection) {
    // The compatibility guarantee, asserted rather than assumed: the skinning section is optional
    // and trails the payload, so a static mesh must serialize to exactly the bytes this engine
    // produced before skinning existed. The envelope's version field is global across all asset
    // kinds, so bumping it was never an option - every scene and texture would have been
    // invalidated to describe a mesh feature.
    MeshAssetUVE staticMesh;
    staticMesh.vertices = {
        MeshVertexUVE{Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F, 0.0F},
        MeshVertexUVE{Math::Vector3UVE{1.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 1.0F, 0.0F},
        MeshVertexUVE{Math::Vector3UVE{0.0F, 1.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F, 1.0F},
    };
    staticMesh.indices = {0U, 1U, 2U};
    staticMesh.localBounds =
        Math::AabbUVE{Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{1.0F, 1.0F, 0.0F}};

    // 3 vertices * 8 floats, + a vertex count, + an index count and 3 indices, + 6 bounds floats.
    const std::size_t expectedPayloadBytes = sizeof(std::uint32_t) + 3U * 8U * sizeof(float) +
                                             sizeof(std::uint32_t) + 3U * sizeof(std::uint32_t) +
                                             6U * sizeof(float);

    const std::filesystem::path path =
        ::UVE::Tests::ScratchRootUVE() / "uve_static_compat.uvemodel";
    ASSERT_TRUE(SaveMeshAssetUVE(staticMesh, path));
    const std::uintmax_t fileBytes = std::filesystem::file_size(path);

    // The header size is internal to the envelope, so it is measured rather than restated here:
    // an empty-payload envelope IS the header. Restating the layout would make this test fail for
    // an unrelated envelope change instead of for the thing it is guarding.
    const std::size_t headerBytes = EncodeUveFileEnvelopeUVE(AssetKindUVE::Mesh, {}).size();
    EXPECT_EQ(fileBytes, headerBytes + expectedPayloadBytes)
        << "a static mesh grew on disk - the skinning section must be written only when skinned";

    MeshAssetUVE loaded;
    ASSERT_TRUE(LoadMeshAssetUVE(path, loaded));
    EXPECT_FALSE(loaded.IsSkinnedUVE());
    EXPECT_EQ(loaded.vertices.size(), 3U);
    std::filesystem::remove(path);
}

TEST(MeshSkinningUVETest, Load_RejectsStructurallyInvalidSkinningDataWithoutTouchingTheOutput) {
    // Save bypasses validation by design (it serializes what it is given), so a hand-built bad
    // file is reachable - and the loader is where it must be caught.
    MeshAssetUVE bad = MakeSkinnedMeshUVE();
    bad.skinningInfluences[0].joints[0] = 7U; // no such joint

    const std::filesystem::path path =
        ::UVE::Tests::ScratchRootUVE() / "uve_bad_skinning.uvemodel";
    ASSERT_TRUE(SaveMeshAssetUVE(bad, path));

    MeshAssetUVE loaded;
    loaded.vertices.resize(1U);
    EXPECT_FALSE(LoadMeshAssetUVE(path, loaded));
    // Untouched on rejection, matching the loader's existing failure-atomic contract.
    EXPECT_EQ(loaded.vertices.size(), 1U);
    std::filesystem::remove(path);
}

} // namespace
} // namespace UVE::Asset::Tests
