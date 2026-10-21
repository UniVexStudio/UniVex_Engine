// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/morph_target_component_uve.h"

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Scene::Tests {
namespace {

[[nodiscard]] Asset::MeshAssetUVE MakeFaceMeshUVE() {
    Asset::MeshAssetUVE mesh;
    mesh.vertices = {
        Asset::MeshVertexUVE{Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F, 0.0F},
        Asset::MeshVertexUVE{Math::Vector3UVE{1.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 1.0F, 0.0F},
        Asset::MeshVertexUVE{Math::Vector3UVE{0.0F, 1.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F, 1.0F},
    };
    mesh.indices = {0U, 1U, 2U};
    mesh.localBounds = Math::AabbUVE{Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{1.0F, 1.0F, 0.0F}};

    Asset::MeshMorphTargetUVE jaw;
    jaw.name = "jawOpen";
    jaw.deltas.push_back(Asset::MeshMorphTargetDeltaUVE{
        2U, Math::Vector3UVE{0.0F, -0.5F, 0.0F}, Math::Vector3UVE{}, Math::Vector3UVE{}});

    Asset::MeshMorphTargetUVE smile;
    smile.name = "mouthSmile";
    smile.deltas.push_back(Asset::MeshMorphTargetDeltaUVE{
        0U, Math::Vector3UVE{-0.2F, 0.0F, 0.0F}, Math::Vector3UVE{}, Math::Vector3UVE{}});

    mesh.morphTargets = {std::move(jaw), std::move(smile)};
    return mesh;
}

TEST(MorphTargetComponentUVETest, DefaultComponentIsValidAndNoOp) {
    const MorphTargetComponentUVE component{};
    EXPECT_TRUE(component.enabled);
    EXPECT_TRUE(component.weights.empty());
    EXPECT_TRUE(IsMorphTargetComponentValidUVE(component));

    const Asset::MeshAssetUVE mesh = MakeFaceMeshUVE();
    std::vector<Asset::MeshVertexUVE> morphed;
    ASSERT_TRUE(EvaluateMorphTargetsUVE(component, mesh, mesh.vertices, morphed));
    ASSERT_EQ(morphed.size(), mesh.vertices.size());
    // No entries means no contribution, so every vertex is exactly where it was.
    for (std::size_t index = 0U; index < morphed.size(); ++index) {
        EXPECT_EQ(morphed[index].position, mesh.vertices[index].position);
    }
}

TEST(MorphTargetComponentUVETest, ValidatorRejectsEmptyDuplicateAndNonFiniteNames) {
    MorphTargetComponentUVE emptyName;
    emptyName.weights.push_back(MorphTargetWeightUVE{"", 0.5F});
    EXPECT_FALSE(IsMorphTargetComponentValidUVE(emptyName));

    MorphTargetComponentUVE duplicate;
    duplicate.weights.push_back(MorphTargetWeightUVE{"jawOpen", 0.5F});
    duplicate.weights.push_back(MorphTargetWeightUVE{"jawOpen", 0.25F});
    EXPECT_FALSE(IsMorphTargetComponentValidUVE(duplicate));

    MorphTargetComponentUVE nonFinite;
    nonFinite.weights.push_back(
        MorphTargetWeightUVE{"jawOpen", std::numeric_limits<float>::quiet_NaN()});
    EXPECT_FALSE(IsMorphTargetComponentValidUVE(nonFinite));

    MorphTargetComponentUVE tooMany;
    tooMany.weights.resize(kMaximumMorphTargetWeightsUVE + 1U);
    for (std::size_t index = 0U; index < tooMany.weights.size(); ++index) {
        tooMany.weights[index].target = "shape" + std::to_string(index);
    }
    EXPECT_FALSE(IsMorphTargetComponentValidUVE(tooMany));
}

TEST(MorphTargetComponentUVETest, ResolverMapsNamesToIndicesInAnyOrder) {
    const Asset::MeshAssetUVE mesh = MakeFaceMeshUVE();
    MorphTargetComponentUVE component;
    // "mouthSmile" is target 1 and "jawOpen" is target 0: the resolver must honour the names, not
    // the order the rows were authored in.
    component.weights.push_back(MorphTargetWeightUVE{"mouthSmile", 0.75F});
    component.weights.push_back(MorphTargetWeightUVE{"jawOpen", 0.25F});

    std::vector<float> weights;
    ASSERT_TRUE(TryResolveMorphTargetWeightsUVE(component, mesh.morphTargets, {}, weights));
    ASSERT_EQ(weights.size(), 2U);
    EXPECT_FLOAT_EQ(weights[0], 0.25F);
    EXPECT_FLOAT_EQ(weights[1], 0.75F);
}

TEST(MorphTargetComponentUVETest, ResolverFailsHardOnANameTheMeshDoesNotHave) {
    const Asset::MeshAssetUVE mesh = MakeFaceMeshUVE();
    MorphTargetComponentUVE component;
    component.weights.push_back(MorphTargetWeightUVE{"jawopne", 0.5F}); // transposed

    std::vector<float> untouched(1U, 42.0F);
    EXPECT_FALSE(TryResolveMorphTargetWeightsUVE(component, mesh.morphTargets, {}, untouched));
    ASSERT_EQ(untouched.size(), 1U);
    EXPECT_FLOAT_EQ(untouched[0], 42.0F);
}

TEST(MorphTargetComponentUVETest, ResolverStartsFromMeshDefaultsAndOverridesOnlyNamedShapes) {
    Asset::MeshAssetUVE mesh = MakeFaceMeshUVE();
    mesh.defaultMorphWeights = {0.1F, 0.9F};
    MorphTargetComponentUVE component;
    component.weights.push_back(MorphTargetWeightUVE{"jawOpen", 0.6F});

    std::vector<float> weights;
    ASSERT_TRUE(TryResolveMorphTargetWeightsUVE(component, mesh.morphTargets, mesh.defaultMorphWeights, weights));
    ASSERT_EQ(weights.size(), 2U);
    EXPECT_FLOAT_EQ(weights[0], 0.6F); // overridden by the component
    EXPECT_FLOAT_EQ(weights[1], 0.9F); // untouched, so the mesh's own default stands
}

TEST(MorphTargetComponentUVETest, EvaluateAppliesWeightsToTheBindPose) {
    const Asset::MeshAssetUVE mesh = MakeFaceMeshUVE();
    MorphTargetComponentUVE component;
    component.weights.push_back(MorphTargetWeightUVE{"jawOpen", 1.0F});

    std::vector<Asset::MeshVertexUVE> morphed;
    ASSERT_TRUE(EvaluateMorphTargetsUVE(component, mesh, mesh.vertices, morphed));
    EXPECT_EQ(morphed[2].position, (Math::Vector3UVE{0.0F, 0.5F, 0.0F}));
    EXPECT_EQ(morphed[0].position, mesh.vertices[0].position);
}

TEST(MorphTargetComponentUVETest, DisabledComponentFallsBackToMeshDefaultsAndIgnoresStaleNames) {
    Asset::MeshAssetUVE mesh = MakeFaceMeshUVE();
    mesh.defaultMorphWeights = {0.5F, 0.0F};
    MorphTargetComponentUVE component;
    component.enabled = false;
    // A stale name is deliberately present: a paused face must not fail against a mesh it merely
    // happens to be out of date with.
    component.weights.push_back(MorphTargetWeightUVE{"jawOpen", 1.0F});
    component.weights.push_back(MorphTargetWeightUVE{"removedShape", 1.0F});

    std::vector<Asset::MeshVertexUVE> morphed;
    ASSERT_TRUE(EvaluateMorphTargetsUVE(component, mesh, mesh.vertices, morphed));
    // The mesh's default 0.5 - not the component's 1.0.
    EXPECT_EQ(morphed[2].position, (Math::Vector3UVE{0.0F, 0.75F, 0.0F}));
}

TEST(MorphTargetComponentUVETest, EvaluateOnAMeshWithoutShapesIsAPassThrough) {
    Asset::MeshAssetUVE mesh = MakeFaceMeshUVE();
    mesh.morphTargets.clear();
    mesh.defaultMorphWeights.clear();
    MorphTargetComponentUVE component;
    component.weights.push_back(MorphTargetWeightUVE{"jawOpen", 1.0F});

    std::vector<Asset::MeshVertexUVE> morphed;
    // The component names a shape this mesh does not have, which is a real error - the mesh is not
    // silently morphed, and the caller hears about it.
    EXPECT_FALSE(EvaluateMorphTargetsUVE(component, mesh, mesh.vertices, morphed));

    MorphTargetComponentUVE emptyComponent;
    ASSERT_TRUE(EvaluateMorphTargetsUVE(emptyComponent, mesh, mesh.vertices, morphed));
    EXPECT_EQ(morphed.size(), mesh.vertices.size());
}

} // namespace
} // namespace UVE::Scene::Tests
