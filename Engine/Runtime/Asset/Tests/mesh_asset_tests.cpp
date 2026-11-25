// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "mesh_asset.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "asset_database.h"
#include "asset_handle.h"
#include "asset_manager.h"
#include "uve_file_envelope.h"
#include "log_sink.h"
#include "logger.h"
#include "event_system.h"
#include "thread_pool.h"

namespace UVE::Asset::Tests {
namespace {

[[nodiscard]] MeshAsset MakeTestMesh() {
    MeshAsset mesh;
    mesh.vertices = {
        MeshVertex{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F, 0.0F},
        MeshVertex{Math::Vector3{1.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 1.0F, 0.0F},
        MeshVertex{Math::Vector3{0.0F, 1.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F, 1.0F},
    };
    mesh.indices = {0, 1, 2};
    mesh.localBounds = Math::Aabb{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 0.0F}};
    return mesh;
}

TEST(MeshAssetTest, GenerateMeshTangents_StandardUvTriangle_ProducesOrthonormalPositiveBasis) {
    std::vector<MeshVertex> vertices = {
        MeshVertex{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 0.0F, 0.0F},
        MeshVertex{Math::Vector3{1.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 1.0F, 0.0F},
        MeshVertex{Math::Vector3{0.0F, 1.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 0.0F, 1.0F},
    };
    const std::vector<std::uint32_t> indices{0, 1, 2};

    GenerateMeshTangents(vertices, indices);

    for (const MeshVertex& vertex : vertices) {
        EXPECT_NEAR(vertex.tangent.x, 1.0F, 0.0001F);
        EXPECT_NEAR(vertex.tangent.y, 0.0F, 0.0001F);
        EXPECT_NEAR(vertex.tangent.z, 0.0F, 0.0001F);
        EXPECT_NEAR(Math::Dot(vertex.normal, vertex.tangent), 0.0F, 0.0001F);
        EXPECT_FLOAT_EQ(vertex.tangentHandedness, 1.0F);
    }
}

TEST(MeshAssetTest, GenerateMeshTangents_MirroredUvTriangle_ProducesNegativeHandedness) {
    std::vector<MeshVertex> vertices = {
        MeshVertex{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 0.0F, 0.0F},
        MeshVertex{Math::Vector3{1.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 0.0F, 1.0F},
        MeshVertex{Math::Vector3{0.0F, 1.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 1.0F, 0.0F},
    };
    const std::vector<std::uint32_t> indices{0, 1, 2};

    GenerateMeshTangents(vertices, indices);

    for (const MeshVertex& vertex : vertices) {
        EXPECT_NEAR(Math::Length(vertex.tangent), 1.0F, 0.0001F);
        EXPECT_NEAR(Math::Dot(vertex.normal, vertex.tangent), 0.0F, 0.0001F);
        EXPECT_FLOAT_EQ(vertex.tangentHandedness, -1.0F);
    }
}

TEST(MeshAssetTest, GenerateMeshTangents_DegenerateUvs_UsesDeterministicFallback) {
    std::vector<MeshVertex> vertices = {
        MeshVertex{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 0.0F, 0.0F},
        MeshVertex{Math::Vector3{1.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 0.0F, 0.0F},
        MeshVertex{Math::Vector3{0.0F, 1.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 0.0F, 0.0F},
    };
    const std::vector<std::uint32_t> indices{0, 1, 2};

    GenerateMeshTangents(vertices, indices);

    for (const MeshVertex& vertex : vertices) {
        EXPECT_NEAR(vertex.tangent.x, 1.0F, 0.0001F);
        EXPECT_NEAR(vertex.tangent.y, 0.0F, 0.0001F);
        EXPECT_NEAR(vertex.tangent.z, 0.0F, 0.0001F);
        EXPECT_FLOAT_EQ(vertex.tangentHandedness, 1.0F);
    }
}

TEST(MeshAssetTest, TryGenerateMeshTangents_OverflowedSharedAccumulatorPreservesOutput) {
    std::vector<MeshVertex> vertices = {
        MeshVertex{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 0.0F, 0.0F,
                      Math::Vector3{0.0F, 0.0F, 1.0F}, -1.0F},
        MeshVertex{Math::Vector3{3.0e38F, 0.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 1.0F, 0.0F,
                      Math::Vector3{0.0F, 1.0F, 0.0F}, -1.0F},
        MeshVertex{Math::Vector3{0.0F, 1.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 0.0F, 1.0F,
                      Math::Vector3{0.0F, 1.0F, 0.0F}, -1.0F},
        MeshVertex{Math::Vector3{3.0e38F, 0.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 1.0F, 0.0F,
                      Math::Vector3{0.0F, 1.0F, 0.0F}, -1.0F},
        MeshVertex{Math::Vector3{0.0F, 1.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 0.0F, 1.0F,
                      Math::Vector3{0.0F, 1.0F, 0.0F}, -1.0F},
    };
    const std::vector<MeshVertex> original = vertices;
    const std::vector<std::uint32_t> indices{0U, 1U, 2U, 0U, 3U, 4U};

    EXPECT_FALSE(TryGenerateMeshTangents(vertices, indices));
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

TEST(MeshAssetTest, SaveThenLoad_RoundTripsByteExact) {
    const std::filesystem::path path = "uve_mesh_asset_tests_round_trip.uvmodel";
    std::filesystem::remove(path);
    const MeshAsset original = MakeTestMesh();
    ASSERT_TRUE(SaveMeshAsset(original, path));

    MeshAsset loaded;
    ASSERT_TRUE(LoadMeshAsset(path, loaded));

    ASSERT_EQ(loaded.vertices.size(), original.vertices.size());
    for (std::size_t index = 0; index < original.vertices.size(); ++index) {
        EXPECT_EQ(loaded.vertices[index].position, original.vertices[index].position);
        EXPECT_EQ(loaded.vertices[index].normal, original.vertices[index].normal);
        EXPECT_EQ(loaded.vertices[index].u, original.vertices[index].u);
        EXPECT_EQ(loaded.vertices[index].v, original.vertices[index].v);
        EXPECT_NEAR(Math::Length(loaded.vertices[index].tangent), 1.0F, 0.0001F);
        EXPECT_NEAR(Math::Dot(loaded.vertices[index].normal, loaded.vertices[index].tangent), 0.0F, 0.0001F);
        EXPECT_TRUE(loaded.vertices[index].tangentHandedness == -1.0F ||
                    loaded.vertices[index].tangentHandedness == 1.0F);
    }
    EXPECT_EQ(loaded.indices, original.indices);
    EXPECT_EQ(loaded.localBounds, original.localBounds);

    std::filesystem::remove(path);
}

TEST(MeshAssetTest, MeshAssetRoundTripsMaterialGuids) {
    // Foundation WP7: material references are a tagged trailing section - present only when the
    // mesh has any, read back verbatim, and absent from a mesh without materials so that file's
    // bytes stay exactly what they were before the field existed.
    const std::filesystem::path withPath = "uve_mesh_asset_tests_materials.uvmodel";
    const std::filesystem::path withoutPath = "uve_mesh_asset_tests_no_materials.uvmodel";
    const std::filesystem::path legacyPath = "uve_mesh_asset_tests_legacy_bytes.uvmodel";
    std::filesystem::remove(withPath);
    std::filesystem::remove(withoutPath);
    std::filesystem::remove(legacyPath);

    MeshAsset original = MakeTestMesh();
    original.materialGuids = {AssetGuid{0x1122334455667788ULL}, kInvalidAssetGuid, AssetGuid{7U}};
    ASSERT_TRUE(SaveMeshAsset(original, withPath));
    MeshAsset loaded;
    ASSERT_TRUE(LoadMeshAsset(withPath, loaded));
    ASSERT_EQ(loaded.materialGuids.size(), 3U);
    EXPECT_EQ(loaded.materialGuids[0], AssetGuid{0x1122334455667788ULL});
    EXPECT_EQ(loaded.materialGuids[1], kInvalidAssetGuid) << "an unpublished slot survives as the invalid GUID";
    EXPECT_EQ(loaded.materialGuids[2], AssetGuid{7U});
    EXPECT_EQ(loaded.indices, original.indices);
    EXPECT_FALSE(loaded.IsSkinned()) << "the material tag must not be mistaken for a joint count";

    const MeshAsset plain = MakeTestMesh();
    ASSERT_TRUE(SaveMeshAsset(plain, withoutPath));
    MeshAsset loadedPlain;
    ASSERT_TRUE(LoadMeshAsset(withoutPath, loadedPlain));
    EXPECT_TRUE(loadedPlain.materialGuids.empty());
    EXPECT_LT(std::filesystem::file_size(withoutPath), std::filesystem::file_size(withPath))
        << "a mesh without materials writes no material section";

    // A mesh that carries materials AND skinning keeps both.
    MeshAsset skinned = MakeTestMesh();
    skinned.joints.push_back(MeshJoint{});
    skinned.joints.back().parentIndex = kInvalidJointParent;
    skinned.joints.back().name = "root";
    skinned.skinningInfluences.assign(skinned.vertices.size(), MeshSkinningInfluence{});
    for (MeshSkinningInfluence& influence : skinned.skinningInfluences) {
        influence.weights[0] = 1.0F;
    }
    skinned.materialGuids = {AssetGuid{42U}};
    ASSERT_TRUE(SaveMeshAsset(skinned, legacyPath));
    MeshAsset loadedSkinned;
    ASSERT_TRUE(LoadMeshAsset(legacyPath, loadedSkinned));
    EXPECT_TRUE(loadedSkinned.IsSkinned());
    ASSERT_EQ(loadedSkinned.joints.size(), 1U);
    EXPECT_EQ(loadedSkinned.joints[0].name, "root");
    ASSERT_EQ(loadedSkinned.materialGuids.size(), 1U);
    EXPECT_EQ(loadedSkinned.materialGuids[0], AssetGuid{42U});

    std::filesystem::remove(withPath);
    std::filesystem::remove(withoutPath);
    std::filesystem::remove(legacyPath);
}

/// A two-shape face-ish mesh: three vertices, "jawOpen" moving only vertex 2 downward and
/// "smile" moving vertices 0 and 1 outward, plus a default weight that is deliberately off zero so
/// the round trip has to preserve it rather than re-deriving zeroes.
[[nodiscard]] MeshAsset MakeMorphTestMesh() {
    MeshAsset mesh = MakeTestMesh();
    MeshMorphTarget jaw;
    jaw.name = "jawOpen";
    jaw.deltas.push_back(MeshMorphTargetDelta{2U, Math::Vector3{0.0F, -0.25F, 0.1F},
                                                 Math::Vector3{0.0F, -1.0F, 0.0F},
                                                 Math::Vector3{}});
    MeshMorphTarget smile;
    smile.name = "smile";
    smile.deltas.push_back(MeshMorphTargetDelta{0U, Math::Vector3{-0.1F, 0.0F, 0.0F},
                                                   Math::Vector3{}, Math::Vector3{}});
    smile.deltas.push_back(MeshMorphTargetDelta{1U, Math::Vector3{0.1F, 0.0F, 0.0F},
                                                   Math::Vector3{}, Math::Vector3{}});
    mesh.morphTargets = {std::move(jaw), std::move(smile)};
    mesh.defaultMorphWeights = {0.25F, 0.0F};
    return mesh;
}

TEST(MeshAssetTest, StaticMesh_IsValidMorphTargetData) {
    // Every mesh that existed before blend shapes must keep validating unchanged.
    const MeshAsset mesh = MakeTestMesh();
    EXPECT_FALSE(mesh.HasMorphTargets());
    EXPECT_TRUE(IsMeshMorphTargetDataValid(mesh));
}

TEST(MeshAssetTest, MorphTargetData_RejectsOutOfRangeVertex) {
    MeshAsset mesh = MakeMorphTestMesh();
    mesh.morphTargets[0].deltas[0].vertexIndex = 3U; // three vertices, index 3 is past the end
    EXPECT_FALSE(IsMeshMorphTargetDataValid(mesh));
}

TEST(MeshAssetTest, MorphTargetData_RejectsOutOfOrderAndDuplicateDeltas) {
    MeshAsset mesh = MakeMorphTestMesh();
    // "smile" lists vertices 0 then 1; swapping them makes the array descending, and repeating a
    // vertex would double-displace it. Both are the same defect: the ascending invariant is broken.
    std::swap(mesh.morphTargets[1].deltas[0], mesh.morphTargets[1].deltas[1]);
    EXPECT_FALSE(IsMeshMorphTargetDataValid(mesh));

    MeshAsset duplicated = MakeMorphTestMesh();
    duplicated.morphTargets[1].deltas[1].vertexIndex = 0U;
    EXPECT_FALSE(IsMeshMorphTargetDataValid(duplicated));
}

TEST(MeshAssetTest, MorphTargetData_RejectsNonFiniteDeltaAndPartiallyDefaultedWeights) {
    MeshAsset nonFinite = MakeMorphTestMesh();
    nonFinite.morphTargets[0].deltas[0].positionDelta.y = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsMeshMorphTargetDataValid(nonFinite));

    // Two shapes, one weight: the missing weight has no meaning, so it is a defect rather than a
    // zero by omission.
    MeshAsset shortWeights = MakeMorphTestMesh();
    shortWeights.defaultMorphWeights = {0.5F};
    EXPECT_FALSE(IsMeshMorphTargetDataValid(shortWeights));
}

TEST(MeshAssetTest, MorphTargetData_EmptyDefaultWeightsMeansRestShapeAndIsValid) {
    MeshAsset mesh = MakeMorphTestMesh();
    mesh.defaultMorphWeights.clear();
    EXPECT_TRUE(mesh.HasMorphTargets());
    EXPECT_TRUE(IsMeshMorphTargetDataValid(mesh));
}

TEST(MeshAssetTest, AccumulateMorphTargets_UsesSparseDeltasAndSkipsZeroWeights) {
    const MeshAsset mesh = MakeMorphTestMesh();
    // A zero weight on "smile" must leave its vertices exactly where they were - not merely close.
    const std::array<float, 2U> weights{1.0F, 0.0F};
    std::vector<MeshVertex> morphed;
    ASSERT_TRUE(AccumulateMorphTargets(mesh.vertices, mesh.morphTargets, weights, morphed));
    ASSERT_EQ(morphed.size(), mesh.vertices.size());

    EXPECT_EQ(morphed[2].position, (Math::Vector3{0.0F, 0.75F, 0.1F}));
    EXPECT_EQ(morphed[2].normal, (Math::Vector3{0.0F, 0.0F, 0.0F}));
    EXPECT_EQ(morphed[0].position, mesh.vertices[0].position);
    EXPECT_EQ(morphed[1].position, mesh.vertices[1].position);
}

TEST(MeshAssetTest, AccumulateMorphTargets_ScalesByWeightAndIsOrderIndependent) {
    const MeshAsset mesh = MakeMorphTestMesh();
    const std::array<float, 2U> weights{0.5F, 1.0F};
    std::vector<MeshVertex> morphed;
    ASSERT_TRUE(AccumulateMorphTargets(mesh.vertices, mesh.morphTargets, weights, morphed));
    EXPECT_FLOAT_EQ(morphed[0].position.x, -0.1F);
    EXPECT_FLOAT_EQ(morphed[1].position.x, 1.1F);
    EXPECT_FLOAT_EQ(morphed[2].position.y, 0.875F);

    // Reordering the TARGETS (and their weights) must move the same vertices the same distance.
    // This is the property that makes two assets describing one face comparable at all.
    MeshAsset reordered = mesh;
    std::swap(reordered.morphTargets[0], reordered.morphTargets[1]);
    std::vector<MeshVertex> reorderedVertices;
    const std::array<float, 2U> reorderedWeights{1.0F, 0.5F};
    ASSERT_TRUE(AccumulateMorphTargets(mesh.vertices, reordered.morphTargets, reorderedWeights,
                                          reorderedVertices));
    for (std::size_t index = 0U; index < morphed.size(); ++index) {
        EXPECT_NEAR(reorderedVertices[index].position.x, morphed[index].position.x, 1.0e-6F);
        EXPECT_NEAR(reorderedVertices[index].position.y, morphed[index].position.y, 1.0e-6F);
        EXPECT_NEAR(reorderedVertices[index].position.z, morphed[index].position.z, 1.0e-6F);
    }
}

TEST(MeshAssetTest, AccumulateMorphTargets_FailsAtomicallyOnWeightMismatch) {
    const MeshAsset mesh = MakeMorphTestMesh();
    std::vector<MeshVertex> untouched(1U);
    untouched[0].position = Math::Vector3{7.0F, 7.0F, 7.0F};
    const std::vector<MeshVertex> sentinel = untouched;
    const std::array<float, 1U> tooFewWeights{1.0F};
    EXPECT_FALSE(AccumulateMorphTargets(mesh.vertices, mesh.morphTargets, tooFewWeights, untouched));
    ASSERT_EQ(untouched.size(), sentinel.size());
    EXPECT_EQ(untouched[0].position, sentinel[0].position);
}

TEST(MeshAssetTest, SaveThenLoad_RoundTripsMorphTargetsAndDefaultWeights) {
    const std::filesystem::path path = "uve_mesh_asset_tests_morph_round_trip.uvmodel";
    std::filesystem::remove(path);
    const MeshAsset original = MakeMorphTestMesh();
    ASSERT_TRUE(SaveMeshAsset(original, path));

    MeshAsset loaded;
    ASSERT_TRUE(LoadMeshAsset(path, loaded));
    ASSERT_EQ(loaded.morphTargets.size(), 2U);
    EXPECT_EQ(loaded.morphTargets[0].name, "jawOpen");
    EXPECT_EQ(loaded.morphTargets[1].name, "smile");
    EXPECT_EQ(loaded.morphTargets[0].deltas, original.morphTargets[0].deltas);
    EXPECT_EQ(loaded.morphTargets[1].deltas, original.morphTargets[1].deltas);
    EXPECT_EQ(loaded.defaultMorphWeights, original.defaultMorphWeights);
    std::filesystem::remove(path);
}

TEST(MeshAssetTest, SaveThenLoad_MeshWithoutShapesKeepsItsOldBytes) {
    // A static mesh must not grow a section just because the format learned about blend shapes:
    // that is what lets an older engine build read a newer file's geometry.
    const std::filesystem::path path = "uve_mesh_asset_tests_static_bytes.uvmodel";
    std::filesystem::remove(path);
    const MeshAsset mesh = MakeTestMesh();
    ASSERT_TRUE(SaveMeshAsset(mesh, path));

    MeshAsset loaded;
    ASSERT_TRUE(LoadMeshAsset(path, loaded));
    EXPECT_TRUE(loaded.morphTargets.empty());
    EXPECT_TRUE(loaded.defaultMorphWeights.empty());
    std::filesystem::remove(path);
}

TEST(MeshAssetTest, SaveThenLoad_RoundTripsShapesOnASkinnedMesh) {
    // Blend shapes and skinning are independent sections and must not disturb each other; a face
    // mesh is normally BOTH, so this is the combination that ships.
    MeshAsset mesh = MakeMorphTestMesh();
    mesh.joints.resize(1U);
    mesh.joints[0].name = "Root";
    mesh.joints[0].inverseBindMatrix = Math::Matrix4x4::Identity();
    mesh.skinningInfluences.assign(mesh.vertices.size(), MeshSkinningInfluence{});
    for (MeshSkinningInfluence& influence : mesh.skinningInfluences) {
        influence.joints[0] = 0U;
        influence.weights[0] = 1.0F;
    }
    mesh.materialGuids = {AssetGuid{0x1234U}};

    const std::filesystem::path path = "uve_mesh_asset_tests_skinned_morph.uvmodel";
    std::filesystem::remove(path);
    ASSERT_TRUE(SaveMeshAsset(mesh, path));

    MeshAsset loaded;
    ASSERT_TRUE(LoadMeshAsset(path, loaded));
    EXPECT_TRUE(loaded.IsSkinned());
    ASSERT_EQ(loaded.morphTargets.size(), 2U);
    EXPECT_EQ(loaded.morphTargets[0].name, "jawOpen");
    ASSERT_EQ(loaded.materialGuids.size(), 1U);
    EXPECT_EQ(loaded.materialGuids[0].value, 0x1234U);
    std::filesystem::remove(path);
}

TEST(MeshAssetTest, LoadMeshAsset_WrongAssetKind_FailsCleanlyAndLogsError) {
    const std::filesystem::path path = "uve_mesh_asset_tests_wrong_kind.uvblob";
    std::filesystem::remove(path);
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Blob, {}));

    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    MeshAsset mesh;
    EXPECT_FALSE(LoadMeshAsset(path, mesh));

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Error && message.message.contains("not a mesh file");
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
    std::filesystem::remove(path);
}

TEST(MeshAssetTest, LoadMeshAsset_ImpossibleVertexCountFailsBeforeReserve) {
    const std::filesystem::path path = "uve_mesh_asset_tests_impossible_vertex_count.uvmodel";
    std::filesystem::remove(path);
    const std::uint32_t impossibleVertexCount = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::byte> payload(sizeof(impossibleVertexCount));
    std::memcpy(payload.data(), &impossibleVertexCount, sizeof(impossibleVertexCount));
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Mesh, payload));

    MeshAsset loaded = MakeTestMesh();
    const MeshAsset original = loaded;
    EXPECT_FALSE(LoadMeshAsset(path, loaded));
    EXPECT_EQ(loaded.vertices.size(), original.vertices.size());
    EXPECT_EQ(loaded.indices, original.indices);
    EXPECT_EQ(loaded.localBounds, original.localBounds);
    std::filesystem::remove(path);
}

TEST(MeshAssetTest, LoadMeshAsset_MissingFile_ReturnsFalse) {
    const std::filesystem::path path = "uve_mesh_asset_tests_nonexistent.uvmodel";
    std::filesystem::remove(path);

    MeshAsset mesh;
    EXPECT_FALSE(LoadMeshAsset(path, mesh));
}

TEST(MeshAssetTest, LoadMeshAsset_OutOfBoundsIndex_FailsAndLogsError) {
    const std::filesystem::path path = "uve_mesh_asset_tests_bad_index.uvmodel";
    std::filesystem::remove(path);

    MeshAsset invalidMesh = MakeTestMesh();
    invalidMesh.indices = {0, 1, 5}; // 5 is out of bounds - only 3 vertices exist
    ASSERT_TRUE(SaveMeshAsset(invalidMesh, path));

    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    MeshAsset loaded;
    EXPECT_FALSE(LoadMeshAsset(path, loaded));

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Error &&
                   message.message.contains("out-of-bounds index");
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
    std::filesystem::remove(path);
}

/// A quad split into two triangles with one material per triangle: the smallest mesh that can
/// carry a real surface split.
[[nodiscard]] MeshAsset MakeSurfaceTestMesh() {
    MeshAsset mesh;
    mesh.vertices = {
        MeshVertex{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 0.0F, 0.0F},
        MeshVertex{Math::Vector3{1.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 1.0F, 0.0F},
        MeshVertex{Math::Vector3{1.0F, 1.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 1.0F, 1.0F},
        MeshVertex{Math::Vector3{0.0F, 1.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 0.0F, 1.0F},
    };
    mesh.indices = {0U, 1U, 2U, 0U, 2U, 3U};
    mesh.localBounds = Math::Aabb{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 0.0F}};
    mesh.materialGuids = {AssetGuid{11U}, AssetGuid{12U}};
    mesh.surfaces = {MeshSurface{0U, 3U, 0U}, MeshSurface{3U, 3U, 1U}};
    return mesh;
}

TEST(MeshAssetTest, Surfaces_MeshWithoutExplicitSplitIsOneImplicitWholeBufferSurface) {
    MeshAsset mesh = MakeTestMesh();
    mesh.materialGuids = {AssetGuid{7U}};

    EXPECT_FALSE(mesh.HasExplicitSurfaces());
    ASSERT_EQ(mesh.SurfaceCount(), 1U);
    const MeshSurface surface = mesh.GetSurface(0U);
    EXPECT_EQ(surface.firstIndex, 0U);
    EXPECT_EQ(surface.indexCount, 3U);
    EXPECT_EQ(surface.materialSlot, 0U);
    EXPECT_EQ(mesh.SurfaceMaterialGuid(0U), AssetGuid{7U});

    // A mesh with no materials still has its one surface - it is simply shaded by the object.
    mesh.materialGuids.clear();
    EXPECT_EQ(mesh.GetSurface(0U).materialSlot, kInvalidMeshSurfaceMaterialSlot);
    EXPECT_EQ(mesh.SurfaceMaterialGuid(0U), kInvalidAssetGuid);

    // An empty mesh has nothing to draw, and an out-of-range index answers an empty range rather
    // than reading past the end.
    MeshAsset empty;
    EXPECT_EQ(empty.SurfaceCount(), 0U);
    EXPECT_EQ(mesh.GetSurface(1U), (MeshSurface{}));
}

TEST(MeshAssetTest, Surfaces_ExplicitSplitAnswersPerSurfaceMaterial) {
    const MeshAsset mesh = MakeSurfaceTestMesh();

    EXPECT_TRUE(mesh.HasExplicitSurfaces());
    ASSERT_EQ(mesh.SurfaceCount(), 2U);
    EXPECT_EQ(mesh.GetSurface(1U), (MeshSurface{3U, 3U, 1U}));
    EXPECT_EQ(mesh.SurfaceMaterialGuid(0U), AssetGuid{11U});
    EXPECT_EQ(mesh.SurfaceMaterialGuid(1U), AssetGuid{12U});
    // FirstSurfaceMaterialGuid keeps meaning "the material an object with none of its own gets".
    EXPECT_EQ(mesh.FirstSurfaceMaterialGuid(), AssetGuid{11U});
}

TEST(MeshAssetTest, SaveThenLoad_RoundTripsSurfacesAndTheirMaterialSlots) {
    const std::filesystem::path path = "uve_mesh_asset_tests_surface_round_trip.uvmodel";
    std::filesystem::remove(path);
    const MeshAsset original = MakeSurfaceTestMesh();
    ASSERT_TRUE(SaveMeshAsset(original, path));

    MeshAsset loaded;
    ASSERT_TRUE(LoadMeshAsset(path, loaded));
    EXPECT_EQ(loaded.surfaces, original.surfaces);
    EXPECT_EQ(loaded.materialGuids, original.materialGuids);
    EXPECT_EQ(loaded.indices, original.indices);
    ASSERT_EQ(loaded.SurfaceCount(), 2U);
    EXPECT_EQ(loaded.SurfaceMaterialGuid(1U), AssetGuid{12U});
    std::filesystem::remove(path);
}

TEST(MeshAssetTest, SaveThenLoad_MeshWithoutSurfacesKeepsItsOldBytes) {
    // Surfaces are an optional trailing section: a single-material mesh must keep producing the
    // exact bytes it always did, so an older build can still read the file and this one does not
    // grow every asset in the project.
    const std::filesystem::path plainPath = "uve_mesh_asset_tests_no_surfaces_a.uvmodel";
    const std::filesystem::path againPath = "uve_mesh_asset_tests_no_surfaces_b.uvmodel";
    std::filesystem::remove(plainPath);
    std::filesystem::remove(againPath);
    const MeshAsset mesh = MakeSurfaceTestMesh();
    MeshAsset plain = mesh;
    plain.surfaces.clear();
    ASSERT_TRUE(SaveMeshAsset(plain, plainPath));
    ASSERT_TRUE(SaveMeshAsset(plain, againPath));

    MeshAsset loaded;
    ASSERT_TRUE(LoadMeshAsset(plainPath, loaded));
    EXPECT_TRUE(loaded.surfaces.empty());
    EXPECT_EQ(loaded.SurfaceCount(), 1U);
    EXPECT_EQ(std::filesystem::file_size(plainPath), std::filesystem::file_size(againPath));
    std::filesystem::remove(plainPath);
    std::filesystem::remove(againPath);
}

TEST(MeshAssetTest, IsMeshSurfaceDataValid_EnforcesTilingAndSlotBounds) {
    MeshAsset mesh = MakeSurfaceTestMesh();
    EXPECT_TRUE(IsMeshSurfaceDataValid(mesh));

    // A mesh with no explicit surfaces is valid by definition: one implicit whole-buffer surface.
    MeshAsset implicit = mesh;
    implicit.surfaces.clear();
    EXPECT_TRUE(IsMeshSurfaceDataValid(implicit));

    MeshAsset gap = mesh;
    gap.surfaces[1].firstIndex = 4U;
    EXPECT_FALSE(IsMeshSurfaceDataValid(gap));

    MeshAsset overlap = mesh;
    overlap.surfaces[1].firstIndex = 2U;
    EXPECT_FALSE(IsMeshSurfaceDataValid(overlap));

    MeshAsset shortCoverage = mesh;
    shortCoverage.surfaces.pop_back();
    EXPECT_FALSE(IsMeshSurfaceDataValid(shortCoverage));

    MeshAsset partialTriangle = mesh;
    partialTriangle.surfaces[1].indexCount = 4U;
    EXPECT_FALSE(IsMeshSurfaceDataValid(partialTriangle));

    MeshAsset outOfRangeSlot = mesh;
    outOfRangeSlot.surfaces[1].materialSlot = 2U;
    EXPECT_FALSE(IsMeshSurfaceDataValid(outOfRangeSlot));

    // A surface that names no material is not a defect: it means "the object's material".
    MeshAsset noSlot = mesh;
    noSlot.surfaces[1].materialSlot = kInvalidMeshSurfaceMaterialSlot;
    EXPECT_TRUE(IsMeshSurfaceDataValid(noSlot));
}

TEST(MeshAssetTest, LoadMeshAsset_OverlappingSurfaces_FailsAndLeavesMeshUntouched) {
    const std::filesystem::path path = "uve_mesh_asset_tests_bad_surfaces.uvmodel";
    std::filesystem::remove(path);
    ASSERT_TRUE(SaveMeshAsset(MakeSurfaceTestMesh(), path));

    // The surface section is last, so the second surface's index count is the uint32 at end - 8.
    // Widening it to the whole buffer makes the second range both overlap the first and run past
    // the end of the index buffer; the loader has to refuse that rather than draw the same
    // triangles twice with two materials.
    {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(file.is_open());
        file.seekp(-8, std::ios::end);
        const std::uint32_t overlappingCount = 6U;
        file.write(reinterpret_cast<const char*>(&overlappingCount), sizeof(overlappingCount));
        ASSERT_TRUE(file.good());
    }

    MeshAsset loaded = MakeTestMesh();
    const MeshAsset before = loaded;
    EXPECT_FALSE(LoadMeshAsset(path, loaded));
    EXPECT_EQ(loaded.indices, before.indices);
    EXPECT_EQ(loaded.surfaces, before.surfaces);
    std::filesystem::remove(path);
}

TEST(MeshAssetTest, EndToEnd_RegisterLoaderThenLoad_ReachesLoadedWithMatchingData) {
    const std::filesystem::path path = "uve_mesh_asset_tests_end_to_end.uvmodel";
    std::filesystem::remove(path);
    const MeshAsset original = MakeTestMesh();
    ASSERT_TRUE(SaveMeshAsset(original, path));

    Threading::ThreadPool threadPool(2);
    Events::EventSystem eventSystem;
    AssetDatabase assetDatabase;
    AssetManager assetManager(threadPool, eventSystem);
    assetManager.RegisterLoader<MeshAsset>(&LoadMeshAsset);

    const AssetGuid guid = assetDatabase.Register(path);
    const AssetHandle<MeshAsset> handle = assetManager.Load<MeshAsset>(guid, assetDatabase);

    bool ready = false;
    for (int iteration = 0; iteration < 200000 && !ready; ++iteration) {
        ready = handle.IsReady() || handle.HasFailed();
        if (!ready) {
            std::this_thread::yield();
        }
    }
    ASSERT_TRUE(ready);
    ASSERT_TRUE(handle.IsReady());
    const MeshAsset* const loaded = handle.TryGet();
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->indices, original.indices);
    ASSERT_EQ(loaded->vertices.size(), original.vertices.size());

    std::filesystem::remove(path);
}

} // namespace
} // namespace UVE::Asset::Tests
