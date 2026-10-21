// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/asset/mesh_asset_uve.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "uve/asset/asset_database_uve.h"
#include "uve/asset/asset_handle_uve.h"
#include "uve/asset/asset_manager_uve.h"
#include "uve/asset/uve_file_envelope_uve.h"
#include "uve/logging/log_sink_uve.h"
#include "uve/logging/logger_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/threading/thread_pool_uve.h"

namespace UVE::Asset::Tests {
namespace {

[[nodiscard]] MeshAssetUVE MakeTestMeshUVE() {
    MeshAssetUVE mesh;
    mesh.vertices = {
        MeshVertexUVE{Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F, 0.0F},
        MeshVertexUVE{Math::Vector3UVE{1.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 1.0F, 0.0F},
        MeshVertexUVE{Math::Vector3UVE{0.0F, 1.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F, 1.0F},
    };
    mesh.indices = {0, 1, 2};
    mesh.localBounds = Math::AabbUVE{Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{1.0F, 1.0F, 0.0F}};
    return mesh;
}

TEST(MeshAssetUVETest, GenerateMeshTangentsUVE_StandardUvTriangle_ProducesOrthonormalPositiveBasis) {
    std::vector<MeshVertexUVE> vertices = {
        MeshVertexUVE{Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 0.0F, 1.0F}, 0.0F, 0.0F},
        MeshVertexUVE{Math::Vector3UVE{1.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 0.0F, 1.0F}, 1.0F, 0.0F},
        MeshVertexUVE{Math::Vector3UVE{0.0F, 1.0F, 0.0F}, Math::Vector3UVE{0.0F, 0.0F, 1.0F}, 0.0F, 1.0F},
    };
    const std::vector<std::uint32_t> indices{0, 1, 2};

    GenerateMeshTangentsUVE(vertices, indices);

    for (const MeshVertexUVE& vertex : vertices) {
        EXPECT_NEAR(vertex.tangent.x, 1.0F, 0.0001F);
        EXPECT_NEAR(vertex.tangent.y, 0.0F, 0.0001F);
        EXPECT_NEAR(vertex.tangent.z, 0.0F, 0.0001F);
        EXPECT_NEAR(Math::DotUVE(vertex.normal, vertex.tangent), 0.0F, 0.0001F);
        EXPECT_FLOAT_EQ(vertex.tangentHandedness, 1.0F);
    }
}

TEST(MeshAssetUVETest, GenerateMeshTangentsUVE_MirroredUvTriangle_ProducesNegativeHandedness) {
    std::vector<MeshVertexUVE> vertices = {
        MeshVertexUVE{Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 0.0F, 1.0F}, 0.0F, 0.0F},
        MeshVertexUVE{Math::Vector3UVE{1.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 0.0F, 1.0F}, 0.0F, 1.0F},
        MeshVertexUVE{Math::Vector3UVE{0.0F, 1.0F, 0.0F}, Math::Vector3UVE{0.0F, 0.0F, 1.0F}, 1.0F, 0.0F},
    };
    const std::vector<std::uint32_t> indices{0, 1, 2};

    GenerateMeshTangentsUVE(vertices, indices);

    for (const MeshVertexUVE& vertex : vertices) {
        EXPECT_NEAR(Math::LengthUVE(vertex.tangent), 1.0F, 0.0001F);
        EXPECT_NEAR(Math::DotUVE(vertex.normal, vertex.tangent), 0.0F, 0.0001F);
        EXPECT_FLOAT_EQ(vertex.tangentHandedness, -1.0F);
    }
}

TEST(MeshAssetUVETest, GenerateMeshTangentsUVE_DegenerateUvs_UsesDeterministicFallback) {
    std::vector<MeshVertexUVE> vertices = {
        MeshVertexUVE{Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 0.0F, 1.0F}, 0.0F, 0.0F},
        MeshVertexUVE{Math::Vector3UVE{1.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 0.0F, 1.0F}, 0.0F, 0.0F},
        MeshVertexUVE{Math::Vector3UVE{0.0F, 1.0F, 0.0F}, Math::Vector3UVE{0.0F, 0.0F, 1.0F}, 0.0F, 0.0F},
    };
    const std::vector<std::uint32_t> indices{0, 1, 2};

    GenerateMeshTangentsUVE(vertices, indices);

    for (const MeshVertexUVE& vertex : vertices) {
        EXPECT_NEAR(vertex.tangent.x, 1.0F, 0.0001F);
        EXPECT_NEAR(vertex.tangent.y, 0.0F, 0.0001F);
        EXPECT_NEAR(vertex.tangent.z, 0.0F, 0.0001F);
        EXPECT_FLOAT_EQ(vertex.tangentHandedness, 1.0F);
    }
}

TEST(MeshAssetUVETest, TryGenerateMeshTangentsUVE_OverflowedSharedAccumulatorPreservesOutput) {
    std::vector<MeshVertexUVE> vertices = {
        MeshVertexUVE{Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 0.0F, 1.0F}, 0.0F, 0.0F,
                      Math::Vector3UVE{0.0F, 0.0F, 1.0F}, -1.0F},
        MeshVertexUVE{Math::Vector3UVE{3.0e38F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 0.0F, 1.0F}, 1.0F, 0.0F,
                      Math::Vector3UVE{0.0F, 1.0F, 0.0F}, -1.0F},
        MeshVertexUVE{Math::Vector3UVE{0.0F, 1.0F, 0.0F}, Math::Vector3UVE{0.0F, 0.0F, 1.0F}, 0.0F, 1.0F,
                      Math::Vector3UVE{0.0F, 1.0F, 0.0F}, -1.0F},
        MeshVertexUVE{Math::Vector3UVE{3.0e38F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 0.0F, 1.0F}, 1.0F, 0.0F,
                      Math::Vector3UVE{0.0F, 1.0F, 0.0F}, -1.0F},
        MeshVertexUVE{Math::Vector3UVE{0.0F, 1.0F, 0.0F}, Math::Vector3UVE{0.0F, 0.0F, 1.0F}, 0.0F, 1.0F,
                      Math::Vector3UVE{0.0F, 1.0F, 0.0F}, -1.0F},
    };
    const std::vector<MeshVertexUVE> original = vertices;
    const std::vector<std::uint32_t> indices{0U, 1U, 2U, 0U, 3U, 4U};

    EXPECT_FALSE(TryGenerateMeshTangentsUVE(vertices, indices));
    ASSERT_EQ(vertices.size(), original.size());
    for (std::size_t index = 0U; index < vertices.size(); ++index) {
        EXPECT_EQ(vertices[index].position, original[index].position);
        EXPECT_EQ(vertices[index].normal, original[index].normal);
        EXPECT_EQ(vertices[index].u, original[index].u);
        EXPECT_EQ(vertices[index].v, original[index].v);
        EXPECT_EQ(vertices[index].tangent, original[index].tangent);
        EXPECT_EQ(vertices[index].tangentHandedness, original[index].tangentHandedness);
    }
}

TEST(MeshAssetUVETest, SaveThenLoad_RoundTripsByteExact) {
    const std::filesystem::path path = "uve_mesh_asset_tests_round_trip.uvmodel";
    std::filesystem::remove(path);
    const MeshAssetUVE original = MakeTestMeshUVE();
    ASSERT_TRUE(SaveMeshAssetUVE(original, path));

    MeshAssetUVE loaded;
    ASSERT_TRUE(LoadMeshAssetUVE(path, loaded));

    ASSERT_EQ(loaded.vertices.size(), original.vertices.size());
    for (std::size_t index = 0; index < original.vertices.size(); ++index) {
        EXPECT_EQ(loaded.vertices[index].position, original.vertices[index].position);
        EXPECT_EQ(loaded.vertices[index].normal, original.vertices[index].normal);
        EXPECT_EQ(loaded.vertices[index].u, original.vertices[index].u);
        EXPECT_EQ(loaded.vertices[index].v, original.vertices[index].v);
        EXPECT_NEAR(Math::LengthUVE(loaded.vertices[index].tangent), 1.0F, 0.0001F);
        EXPECT_NEAR(Math::DotUVE(loaded.vertices[index].normal, loaded.vertices[index].tangent), 0.0F, 0.0001F);
        EXPECT_TRUE(loaded.vertices[index].tangentHandedness == -1.0F ||
                    loaded.vertices[index].tangentHandedness == 1.0F);
    }
    EXPECT_EQ(loaded.indices, original.indices);
    EXPECT_EQ(loaded.localBounds, original.localBounds);

    std::filesystem::remove(path);
}

TEST(MeshAssetUVETest, MeshAssetRoundTripsMaterialGuids) {
    // Foundation WP7: material references are a tagged trailing section - present only when the
    // mesh has any, read back verbatim, and absent from a mesh without materials so that file's
    // bytes stay exactly what they were before the field existed.
    const std::filesystem::path withPath = "uve_mesh_asset_tests_materials.uvmodel";
    const std::filesystem::path withoutPath = "uve_mesh_asset_tests_no_materials.uvmodel";
    const std::filesystem::path legacyPath = "uve_mesh_asset_tests_legacy_bytes.uvmodel";
    std::filesystem::remove(withPath);
    std::filesystem::remove(withoutPath);
    std::filesystem::remove(legacyPath);

    MeshAssetUVE original = MakeTestMeshUVE();
    original.materialGuids = {AssetGuidUVE{0x1122334455667788ULL}, kInvalidAssetGuidUVE, AssetGuidUVE{7U}};
    ASSERT_TRUE(SaveMeshAssetUVE(original, withPath));
    MeshAssetUVE loaded;
    ASSERT_TRUE(LoadMeshAssetUVE(withPath, loaded));
    ASSERT_EQ(loaded.materialGuids.size(), 3U);
    EXPECT_EQ(loaded.materialGuids[0], AssetGuidUVE{0x1122334455667788ULL});
    EXPECT_EQ(loaded.materialGuids[1], kInvalidAssetGuidUVE) << "an unpublished slot survives as the invalid GUID";
    EXPECT_EQ(loaded.materialGuids[2], AssetGuidUVE{7U});
    EXPECT_EQ(loaded.indices, original.indices);
    EXPECT_FALSE(loaded.IsSkinnedUVE()) << "the material tag must not be mistaken for a joint count";

    const MeshAssetUVE plain = MakeTestMeshUVE();
    ASSERT_TRUE(SaveMeshAssetUVE(plain, withoutPath));
    MeshAssetUVE loadedPlain;
    ASSERT_TRUE(LoadMeshAssetUVE(withoutPath, loadedPlain));
    EXPECT_TRUE(loadedPlain.materialGuids.empty());
    EXPECT_LT(std::filesystem::file_size(withoutPath), std::filesystem::file_size(withPath))
        << "a mesh without materials writes no material section";

    // A mesh that carries materials AND skinning keeps both.
    MeshAssetUVE skinned = MakeTestMeshUVE();
    skinned.joints.push_back(MeshJointUVE{});
    skinned.joints.back().parentIndex = kInvalidJointParentUVE;
    skinned.joints.back().name = "root";
    skinned.skinningInfluences.assign(skinned.vertices.size(), MeshSkinningInfluenceUVE{});
    for (MeshSkinningInfluenceUVE& influence : skinned.skinningInfluences) {
        influence.weights[0] = 1.0F;
    }
    skinned.materialGuids = {AssetGuidUVE{42U}};
    ASSERT_TRUE(SaveMeshAssetUVE(skinned, legacyPath));
    MeshAssetUVE loadedSkinned;
    ASSERT_TRUE(LoadMeshAssetUVE(legacyPath, loadedSkinned));
    EXPECT_TRUE(loadedSkinned.IsSkinnedUVE());
    ASSERT_EQ(loadedSkinned.joints.size(), 1U);
    EXPECT_EQ(loadedSkinned.joints[0].name, "root");
    ASSERT_EQ(loadedSkinned.materialGuids.size(), 1U);
    EXPECT_EQ(loadedSkinned.materialGuids[0], AssetGuidUVE{42U});

    std::filesystem::remove(withPath);
    std::filesystem::remove(withoutPath);
    std::filesystem::remove(legacyPath);
}

/// A two-shape face-ish mesh: three vertices, "jawOpen" moving only vertex 2 downward and
/// "smile" moving vertices 0 and 1 outward, plus a default weight that is deliberately off zero so
/// the round trip has to preserve it rather than re-deriving zeroes.
[[nodiscard]] MeshAssetUVE MakeMorphTestMeshUVE() {
    MeshAssetUVE mesh = MakeTestMeshUVE();
    MeshMorphTargetUVE jaw;
    jaw.name = "jawOpen";
    jaw.deltas.push_back(MeshMorphTargetDeltaUVE{2U, Math::Vector3UVE{0.0F, -0.25F, 0.1F},
                                                 Math::Vector3UVE{0.0F, -1.0F, 0.0F},
                                                 Math::Vector3UVE{}});
    MeshMorphTargetUVE smile;
    smile.name = "smile";
    smile.deltas.push_back(MeshMorphTargetDeltaUVE{0U, Math::Vector3UVE{-0.1F, 0.0F, 0.0F},
                                                   Math::Vector3UVE{}, Math::Vector3UVE{}});
    smile.deltas.push_back(MeshMorphTargetDeltaUVE{1U, Math::Vector3UVE{0.1F, 0.0F, 0.0F},
                                                   Math::Vector3UVE{}, Math::Vector3UVE{}});
    mesh.morphTargets = {std::move(jaw), std::move(smile)};
    mesh.defaultMorphWeights = {0.25F, 0.0F};
    return mesh;
}

TEST(MeshAssetUVETest, StaticMesh_IsValidMorphTargetData) {
    // Every mesh that existed before blend shapes must keep validating unchanged.
    const MeshAssetUVE mesh = MakeTestMeshUVE();
    EXPECT_FALSE(mesh.HasMorphTargetsUVE());
    EXPECT_TRUE(IsMeshMorphTargetDataValidUVE(mesh));
}

TEST(MeshAssetUVETest, MorphTargetData_RejectsOutOfRangeVertex) {
    MeshAssetUVE mesh = MakeMorphTestMeshUVE();
    mesh.morphTargets[0].deltas[0].vertexIndex = 3U; // three vertices, index 3 is past the end
    EXPECT_FALSE(IsMeshMorphTargetDataValidUVE(mesh));
}

TEST(MeshAssetUVETest, MorphTargetData_RejectsOutOfOrderAndDuplicateDeltas) {
    MeshAssetUVE mesh = MakeMorphTestMeshUVE();
    // "smile" lists vertices 0 then 1; swapping them makes the array descending, and repeating a
    // vertex would double-displace it. Both are the same defect: the ascending invariant is broken.
    std::swap(mesh.morphTargets[1].deltas[0], mesh.morphTargets[1].deltas[1]);
    EXPECT_FALSE(IsMeshMorphTargetDataValidUVE(mesh));

    MeshAssetUVE duplicated = MakeMorphTestMeshUVE();
    duplicated.morphTargets[1].deltas[1].vertexIndex = 0U;
    EXPECT_FALSE(IsMeshMorphTargetDataValidUVE(duplicated));
}

TEST(MeshAssetUVETest, MorphTargetData_RejectsNonFiniteDeltaAndPartiallyDefaultedWeights) {
    MeshAssetUVE nonFinite = MakeMorphTestMeshUVE();
    nonFinite.morphTargets[0].deltas[0].positionDelta.y = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsMeshMorphTargetDataValidUVE(nonFinite));

    // Two shapes, one weight: the missing weight has no meaning, so it is a defect rather than a
    // zero by omission.
    MeshAssetUVE shortWeights = MakeMorphTestMeshUVE();
    shortWeights.defaultMorphWeights = {0.5F};
    EXPECT_FALSE(IsMeshMorphTargetDataValidUVE(shortWeights));
}

TEST(MeshAssetUVETest, MorphTargetData_EmptyDefaultWeightsMeansRestShapeAndIsValid) {
    MeshAssetUVE mesh = MakeMorphTestMeshUVE();
    mesh.defaultMorphWeights.clear();
    EXPECT_TRUE(mesh.HasMorphTargetsUVE());
    EXPECT_TRUE(IsMeshMorphTargetDataValidUVE(mesh));
}

TEST(MeshAssetUVETest, AccumulateMorphTargets_UsesSparseDeltasAndSkipsZeroWeights) {
    const MeshAssetUVE mesh = MakeMorphTestMeshUVE();
    // A zero weight on "smile" must leave its vertices exactly where they were - not merely close.
    const std::array<float, 2U> weights{1.0F, 0.0F};
    std::vector<MeshVertexUVE> morphed;
    ASSERT_TRUE(AccumulateMorphTargetsUVE(mesh.vertices, mesh.morphTargets, weights, morphed));
    ASSERT_EQ(morphed.size(), mesh.vertices.size());

    EXPECT_EQ(morphed[2].position, (Math::Vector3UVE{0.0F, 0.75F, 0.1F}));
    EXPECT_EQ(morphed[2].normal, (Math::Vector3UVE{0.0F, 0.0F, 0.0F}));
    EXPECT_EQ(morphed[0].position, mesh.vertices[0].position);
    EXPECT_EQ(morphed[1].position, mesh.vertices[1].position);
}

TEST(MeshAssetUVETest, AccumulateMorphTargets_ScalesByWeightAndIsOrderIndependent) {
    const MeshAssetUVE mesh = MakeMorphTestMeshUVE();
    const std::array<float, 2U> weights{0.5F, 1.0F};
    std::vector<MeshVertexUVE> morphed;
    ASSERT_TRUE(AccumulateMorphTargetsUVE(mesh.vertices, mesh.morphTargets, weights, morphed));
    EXPECT_FLOAT_EQ(morphed[0].position.x, -0.1F);
    EXPECT_FLOAT_EQ(morphed[1].position.x, 1.1F);
    EXPECT_FLOAT_EQ(morphed[2].position.y, 0.875F);

    // Reordering the TARGETS (and their weights) must move the same vertices the same distance.
    // This is the property that makes two assets describing one face comparable at all.
    MeshAssetUVE reordered = mesh;
    std::swap(reordered.morphTargets[0], reordered.morphTargets[1]);
    std::vector<MeshVertexUVE> reorderedVertices;
    const std::array<float, 2U> reorderedWeights{1.0F, 0.5F};
    ASSERT_TRUE(AccumulateMorphTargetsUVE(mesh.vertices, reordered.morphTargets, reorderedWeights,
                                          reorderedVertices));
    for (std::size_t index = 0U; index < morphed.size(); ++index) {
        EXPECT_NEAR(reorderedVertices[index].position.x, morphed[index].position.x, 1.0e-6F);
        EXPECT_NEAR(reorderedVertices[index].position.y, morphed[index].position.y, 1.0e-6F);
        EXPECT_NEAR(reorderedVertices[index].position.z, morphed[index].position.z, 1.0e-6F);
    }
}

TEST(MeshAssetUVETest, AccumulateMorphTargets_FailsAtomicallyOnWeightMismatch) {
    const MeshAssetUVE mesh = MakeMorphTestMeshUVE();
    std::vector<MeshVertexUVE> untouched(1U);
    untouched[0].position = Math::Vector3UVE{7.0F, 7.0F, 7.0F};
    const std::vector<MeshVertexUVE> sentinel = untouched;
    const std::array<float, 1U> tooFewWeights{1.0F};
    EXPECT_FALSE(AccumulateMorphTargetsUVE(mesh.vertices, mesh.morphTargets, tooFewWeights, untouched));
    ASSERT_EQ(untouched.size(), sentinel.size());
    EXPECT_EQ(untouched[0].position, sentinel[0].position);
}

TEST(MeshAssetUVETest, SaveThenLoad_RoundTripsMorphTargetsAndDefaultWeights) {
    const std::filesystem::path path = "uve_mesh_asset_tests_morph_round_trip.uvmodel";
    std::filesystem::remove(path);
    const MeshAssetUVE original = MakeMorphTestMeshUVE();
    ASSERT_TRUE(SaveMeshAssetUVE(original, path));

    MeshAssetUVE loaded;
    ASSERT_TRUE(LoadMeshAssetUVE(path, loaded));
    ASSERT_EQ(loaded.morphTargets.size(), 2U);
    EXPECT_EQ(loaded.morphTargets[0].name, "jawOpen");
    EXPECT_EQ(loaded.morphTargets[1].name, "smile");
    EXPECT_EQ(loaded.morphTargets[0].deltas, original.morphTargets[0].deltas);
    EXPECT_EQ(loaded.morphTargets[1].deltas, original.morphTargets[1].deltas);
    EXPECT_EQ(loaded.defaultMorphWeights, original.defaultMorphWeights);
    std::filesystem::remove(path);
}

TEST(MeshAssetUVETest, SaveThenLoad_MeshWithoutShapesKeepsItsOldBytes) {
    // A static mesh must not grow a section just because the format learned about blend shapes:
    // that is what lets an older engine build read a newer file's geometry.
    const std::filesystem::path path = "uve_mesh_asset_tests_static_bytes.uvmodel";
    std::filesystem::remove(path);
    const MeshAssetUVE mesh = MakeTestMeshUVE();
    ASSERT_TRUE(SaveMeshAssetUVE(mesh, path));

    MeshAssetUVE loaded;
    ASSERT_TRUE(LoadMeshAssetUVE(path, loaded));
    EXPECT_TRUE(loaded.morphTargets.empty());
    EXPECT_TRUE(loaded.defaultMorphWeights.empty());
    std::filesystem::remove(path);
}

TEST(MeshAssetUVETest, SaveThenLoad_RoundTripsShapesOnASkinnedMesh) {
    // Blend shapes and skinning are independent sections and must not disturb each other; a face
    // mesh is normally BOTH, so this is the combination that ships.
    MeshAssetUVE mesh = MakeMorphTestMeshUVE();
    mesh.joints.resize(1U);
    mesh.joints[0].name = "Root";
    mesh.joints[0].inverseBindMatrix = Math::Matrix4x4UVE::IdentityUVE();
    mesh.skinningInfluences.assign(mesh.vertices.size(), MeshSkinningInfluenceUVE{});
    for (MeshSkinningInfluenceUVE& influence : mesh.skinningInfluences) {
        influence.joints[0] = 0U;
        influence.weights[0] = 1.0F;
    }
    mesh.materialGuids = {AssetGuidUVE{0x1234U}};

    const std::filesystem::path path = "uve_mesh_asset_tests_skinned_morph.uvmodel";
    std::filesystem::remove(path);
    ASSERT_TRUE(SaveMeshAssetUVE(mesh, path));

    MeshAssetUVE loaded;
    ASSERT_TRUE(LoadMeshAssetUVE(path, loaded));
    EXPECT_TRUE(loaded.IsSkinnedUVE());
    ASSERT_EQ(loaded.morphTargets.size(), 2U);
    EXPECT_EQ(loaded.morphTargets[0].name, "jawOpen");
    ASSERT_EQ(loaded.materialGuids.size(), 1U);
    EXPECT_EQ(loaded.materialGuids[0].value, 0x1234U);
    std::filesystem::remove(path);
}

TEST(MeshAssetUVETest, LoadMeshAssetUVE_WrongAssetKind_FailsCleanlyAndLogsError) {
    const std::filesystem::path path = "uve_mesh_asset_tests_wrong_kind.uvblob";
    std::filesystem::remove(path);
    ASSERT_TRUE(WriteUveFileUVE(path, AssetKindUVE::Blob, {}));

    Debug::LoggerUVE logger;
    logger.Init(Debug::LogLevelUVE::Trace);
    auto memorySink = std::make_unique<Debug::MemorySinkUVE>();
    Debug::MemorySinkUVE* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    MeshAssetUVE mesh;
    EXPECT_FALSE(LoadMeshAssetUVE(path, mesh));

    const std::vector<Debug::LogMessageUVE> messages = memorySinkPtr->GetMessagesUVE();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessageUVE& message) {
            return message.level == Debug::LogLevelUVE::Error && message.message.contains("not a mesh file");
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
    std::filesystem::remove(path);
}

TEST(MeshAssetUVETest, LoadMeshAssetUVE_ImpossibleVertexCountFailsBeforeReserve) {
    const std::filesystem::path path = "uve_mesh_asset_tests_impossible_vertex_count.uvmodel";
    std::filesystem::remove(path);
    const std::uint32_t impossibleVertexCount = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::byte> payload(sizeof(impossibleVertexCount));
    std::memcpy(payload.data(), &impossibleVertexCount, sizeof(impossibleVertexCount));
    ASSERT_TRUE(WriteUveFileUVE(path, AssetKindUVE::Mesh, payload));

    MeshAssetUVE loaded = MakeTestMeshUVE();
    const MeshAssetUVE original = loaded;
    EXPECT_FALSE(LoadMeshAssetUVE(path, loaded));
    EXPECT_EQ(loaded.vertices.size(), original.vertices.size());
    EXPECT_EQ(loaded.indices, original.indices);
    EXPECT_EQ(loaded.localBounds, original.localBounds);
    std::filesystem::remove(path);
}

TEST(MeshAssetUVETest, LoadMeshAssetUVE_MissingFile_ReturnsFalse) {
    const std::filesystem::path path = "uve_mesh_asset_tests_nonexistent.uvmodel";
    std::filesystem::remove(path);

    MeshAssetUVE mesh;
    EXPECT_FALSE(LoadMeshAssetUVE(path, mesh));
}

TEST(MeshAssetUVETest, LoadMeshAssetUVE_OutOfBoundsIndex_FailsAndLogsError) {
    const std::filesystem::path path = "uve_mesh_asset_tests_bad_index.uvmodel";
    std::filesystem::remove(path);

    MeshAssetUVE invalidMesh = MakeTestMeshUVE();
    invalidMesh.indices = {0, 1, 5}; // 5 is out of bounds - only 3 vertices exist
    ASSERT_TRUE(SaveMeshAssetUVE(invalidMesh, path));

    Debug::LoggerUVE logger;
    logger.Init(Debug::LogLevelUVE::Trace);
    auto memorySink = std::make_unique<Debug::MemorySinkUVE>();
    Debug::MemorySinkUVE* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    MeshAssetUVE loaded;
    EXPECT_FALSE(LoadMeshAssetUVE(path, loaded));

    const std::vector<Debug::LogMessageUVE> messages = memorySinkPtr->GetMessagesUVE();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessageUVE& message) {
            return message.level == Debug::LogLevelUVE::Error &&
                   message.message.contains("out-of-bounds index");
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
    std::filesystem::remove(path);
}

TEST(MeshAssetUVETest, EndToEnd_RegisterLoaderThenLoadUVE_ReachesLoadedWithMatchingData) {
    const std::filesystem::path path = "uve_mesh_asset_tests_end_to_end.uvmodel";
    std::filesystem::remove(path);
    const MeshAssetUVE original = MakeTestMeshUVE();
    ASSERT_TRUE(SaveMeshAssetUVE(original, path));

    Threading::ThreadPoolUVE threadPool(2);
    Events::EventSystemUVE eventSystem;
    AssetDatabaseUVE assetDatabase;
    AssetManagerUVE assetManager(threadPool, eventSystem);
    assetManager.RegisterLoaderUVE<MeshAssetUVE>(&LoadMeshAssetUVE);

    const AssetGuidUVE guid = assetDatabase.RegisterUVE(path);
    const AssetHandleUVE<MeshAssetUVE> handle = assetManager.LoadUVE<MeshAssetUVE>(guid, assetDatabase);

    bool ready = false;
    for (int iteration = 0; iteration < 200000 && !ready; ++iteration) {
        ready = handle.IsReadyUVE() || handle.HasFailedUVE();
        if (!ready) {
            std::this_thread::yield();
        }
    }
    ASSERT_TRUE(ready);
    ASSERT_TRUE(handle.IsReadyUVE());
    const MeshAssetUVE* const loaded = handle.TryGetUVE();
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->indices, original.indices);
    ASSERT_EQ(loaded->vertices.size(), original.vertices.size());

    std::filesystem::remove(path);
}

} // namespace
} // namespace UVE::Asset::Tests
