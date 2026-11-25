// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "morph_target_component.h"

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Scene::Tests {
namespace {

[[nodiscard]] Asset::MeshAsset MakeFaceMesh() {
    Asset::MeshAsset mesh;
    mesh.vertices = {
        Asset::MeshVertex{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F, 0.0F},
        Asset::MeshVertex{Math::Vector3{1.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 1.0F, 0.0F},
        Asset::MeshVertex{Math::Vector3{0.0F, 1.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F, 1.0F},
    };
    mesh.indices = {0U, 1U, 2U};
    mesh.localBounds = Math::Aabb{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 0.0F}};

    Asset::MeshMorphTarget jaw;
    jaw.name = "jawOpen";
    jaw.deltas.push_back(Asset::MeshMorphTargetDelta{
        2U, Math::Vector3{0.0F, -0.5F, 0.0F}, Math::Vector3{}, Math::Vector3{}});

    Asset::MeshMorphTarget smile;
    smile.name = "mouthSmile";
    smile.deltas.push_back(Asset::MeshMorphTargetDelta{
        0U, Math::Vector3{-0.2F, 0.0F, 0.0F}, Math::Vector3{}, Math::Vector3{}});

    mesh.morphTargets = {std::move(jaw), std::move(smile)};
    return mesh;
}

TEST(MorphTargetComponentTest, DefaultComponentIsValidAndNoOp) {
    const MorphTargetComponent component{};
    EXPECT_TRUE(component.enabled);
    EXPECT_TRUE(component.weights.empty());
    EXPECT_TRUE(IsMorphTargetComponentValid(component));

    const Asset::MeshAsset mesh = MakeFaceMesh();
    std::vector<Asset::MeshVertex> morphed;
    ASSERT_TRUE(EvaluateMorphTargets(component, mesh, mesh.vertices, morphed));
    ASSERT_EQ(morphed.size(), mesh.vertices.size());
    // No entries means no contribution, so every vertex is exactly where it was.
    for (std::size_t index = 0U; index < morphed.size(); ++index) {
        EXPECT_EQ(morphed[index].position, mesh.vertices[index].position);
    }
}

TEST(MorphTargetComponentTest, ValidatorRejectsEmptyDuplicateAndNonFiniteNames) {
    MorphTargetComponent emptyName;
    emptyName.weights.push_back(MorphTargetWeight{"", 0.5F});
    EXPECT_FALSE(IsMorphTargetComponentValid(emptyName));

    MorphTargetComponent duplicate;
    duplicate.weights.push_back(MorphTargetWeight{"jawOpen", 0.5F});
    duplicate.weights.push_back(MorphTargetWeight{"jawOpen", 0.25F});
    EXPECT_FALSE(IsMorphTargetComponentValid(duplicate));

    MorphTargetComponent nonFinite;
    nonFinite.weights.push_back(
        MorphTargetWeight{"jawOpen", std::numeric_limits<float>::quiet_NaN()});
    EXPECT_FALSE(IsMorphTargetComponentValid(nonFinite));

    MorphTargetComponent tooMany;
    tooMany.weights.resize(kMaximumMorphTargetWeights + 1U);
    for (std::size_t index = 0U; index < tooMany.weights.size(); ++index) {
        tooMany.weights[index].target = "shape" + std::to_string(index);
    }
    EXPECT_FALSE(IsMorphTargetComponentValid(tooMany));
}

TEST(MorphTargetComponentTest, ResolverMapsNamesToIndicesInAnyOrder) {
    const Asset::MeshAsset mesh = MakeFaceMesh();
    MorphTargetComponent component;
    // "mouthSmile" is target 1 and "jawOpen" is target 0: the resolver must honour the names, not
    // the order the rows were authored in.
    component.weights.push_back(MorphTargetWeight{"mouthSmile", 0.75F});
    component.weights.push_back(MorphTargetWeight{"jawOpen", 0.25F});

    std::vector<float> weights;
    ASSERT_TRUE(TryResolveMorphTargetWeights(component, mesh.morphTargets, {}, weights));
    ASSERT_EQ(weights.size(), 2U);
    EXPECT_FLOAT_EQ(weights[0], 0.25F);
    EXPECT_FLOAT_EQ(weights[1], 0.75F);
}

TEST(MorphTargetComponentTest, ResolverFailsHardOnANameTheMeshDoesNotHave) {
    const Asset::MeshAsset mesh = MakeFaceMesh();
    MorphTargetComponent component;
    component.weights.push_back(MorphTargetWeight{"jawopne", 0.5F}); // transposed

    std::vector<float> untouched(1U, 42.0F);
    EXPECT_FALSE(TryResolveMorphTargetWeights(component, mesh.morphTargets, {}, untouched));
    ASSERT_EQ(untouched.size(), 1U);
    EXPECT_FLOAT_EQ(untouched[0], 42.0F);
}

TEST(MorphTargetComponentTest, ResolverStartsFromMeshDefaultsAndOverridesOnlyNamedShapes) {
    Asset::MeshAsset mesh = MakeFaceMesh();
    mesh.defaultMorphWeights = {0.1F, 0.9F};
    MorphTargetComponent component;
    component.weights.push_back(MorphTargetWeight{"jawOpen", 0.6F});

    std::vector<float> weights;
    ASSERT_TRUE(TryResolveMorphTargetWeights(component, mesh.morphTargets, mesh.defaultMorphWeights, weights));
    ASSERT_EQ(weights.size(), 2U);
    EXPECT_FLOAT_EQ(weights[0], 0.6F); // overridden by the component
    EXPECT_FLOAT_EQ(weights[1], 0.9F); // untouched, so the mesh's own default stands
}

TEST(MorphTargetComponentTest, EvaluateAppliesWeightsToTheBindPose) {
    const Asset::MeshAsset mesh = MakeFaceMesh();
    MorphTargetComponent component;
    component.weights.push_back(MorphTargetWeight{"jawOpen", 1.0F});

    std::vector<Asset::MeshVertex> morphed;
    ASSERT_TRUE(EvaluateMorphTargets(component, mesh, mesh.vertices, morphed));
    EXPECT_EQ(morphed[2].position, (Math::Vector3{0.0F, 0.5F, 0.0F}));
    EXPECT_EQ(morphed[0].position, mesh.vertices[0].position);
}

TEST(MorphTargetComponentTest, DisabledComponentFallsBackToMeshDefaultsAndIgnoresStaleNames) {
    Asset::MeshAsset mesh = MakeFaceMesh();
    mesh.defaultMorphWeights = {0.5F, 0.0F};
    MorphTargetComponent component;
    component.enabled = false;
    // A stale name is deliberately present: a paused face must not fail against a mesh it merely
    // happens to be out of date with.
    component.weights.push_back(MorphTargetWeight{"jawOpen", 1.0F});
    component.weights.push_back(MorphTargetWeight{"removedShape", 1.0F});

    std::vector<Asset::MeshVertex> morphed;
    ASSERT_TRUE(EvaluateMorphTargets(component, mesh, mesh.vertices, morphed));
    // The mesh's default 0.5 - not the component's 1.0.
    EXPECT_EQ(morphed[2].position, (Math::Vector3{0.0F, 0.75F, 0.0F}));
}

TEST(MorphTargetComponentTest, EvaluateOnAMeshWithoutShapesIsAPassThrough) {
    Asset::MeshAsset mesh = MakeFaceMesh();
    mesh.morphTargets.clear();
    mesh.defaultMorphWeights.clear();
    MorphTargetComponent component;
    component.weights.push_back(MorphTargetWeight{"jawOpen", 1.0F});

    std::vector<Asset::MeshVertex> morphed;
    // The component names a shape this mesh does not have, which is a real error - the mesh is not
    // silently morphed, and the caller hears about it.
    EXPECT_FALSE(EvaluateMorphTargets(component, mesh, mesh.vertices, morphed));

    MorphTargetComponent emptyComponent;
    ASSERT_TRUE(EvaluateMorphTargets(emptyComponent, mesh, mesh.vertices, morphed));
    EXPECT_EQ(morphed.size(), mesh.vertices.size());
}

} // namespace
} // namespace UVE::Scene::Tests
