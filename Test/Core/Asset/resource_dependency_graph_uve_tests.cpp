// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/resource_dependency_graph_uve.h"

#include <gtest/gtest.h>

namespace UVE::Asset {
namespace {

ResourceDependencyHandleUVE HandleUVE(const std::uint64_t guid, const std::uint64_t generation = 1U) {
    return ResourceDependencyHandleUVE{AssetGuidUVE{guid}, generation};
}

} // namespace

TEST(ResourceDependencyGraphUVETest, RegisterAndSetDependenciesUVE_ProducesSortedCopiedSnapshot) {
    ResourceDependencyGraphUVE graph;
    const ResourceDependencyHandleUVE a = HandleUVE(30U);
    const ResourceDependencyHandleUVE b = HandleUVE(10U);
    const ResourceDependencyHandleUVE c = HandleUVE(20U);
    ASSERT_TRUE(graph.RegisterResourceUVE(a).IsAppliedUVE());
    ASSERT_TRUE(graph.RegisterResourceUVE(b).IsAppliedUVE());
    ASSERT_TRUE(graph.RegisterResourceUVE(c).IsAppliedUVE());
    ASSERT_TRUE(graph.SetDependenciesUVE(a, {c, b}).IsAppliedUVE());

    const ResourceDependencySnapshotUVE snapshot = graph.GetSnapshotUVE();
    ASSERT_EQ(snapshot.entries.size(), 3U);
    EXPECT_EQ(snapshot.entries[0].handle.guid.value, 10U);
    EXPECT_EQ(snapshot.entries[1].handle.guid.value, 20U);
    EXPECT_EQ(snapshot.entries[2].handle.guid.value, 30U);
    ASSERT_EQ(snapshot.entries[2].dependencies.size(), 2U);
    EXPECT_EQ(snapshot.entries[2].dependencies[0].guid.value, 20U);
    EXPECT_EQ(snapshot.entries[2].dependencies[1].guid.value, 10U);
    EXPECT_EQ(snapshot.graphGeneration, 4U);
}

TEST(ResourceDependencyGraphUVETest, SetDependenciesUVE_RejectsStaleUnknownDuplicateAndCycleReferences) {
    ResourceDependencyGraphUVE graph;
    const ResourceDependencyHandleUVE a = HandleUVE(1U, 4U);
    const ResourceDependencyHandleUVE b = HandleUVE(2U, 1U);
    const ResourceDependencyHandleUVE c = HandleUVE(3U, 1U);
    ASSERT_TRUE(graph.RegisterResourceUVE(a).IsAppliedUVE());
    ASSERT_TRUE(graph.RegisterResourceUVE(b).IsAppliedUVE());
    ASSERT_TRUE(graph.RegisterResourceUVE(c).IsAppliedUVE());
    ASSERT_TRUE(graph.SetDependenciesUVE(a, {b}).IsAppliedUVE());
    ASSERT_TRUE(graph.SetDependenciesUVE(b, {c}).IsAppliedUVE());

    EXPECT_EQ(graph.SetDependenciesUVE(ResourceDependencyHandleUVE{a.guid, 3U}, {b}).code,
              ResourceDependencyCodeUVE::StaleGeneration);
    EXPECT_EQ(graph.SetDependenciesUVE(a, {HandleUVE(99U)}).code,
              ResourceDependencyCodeUVE::UnknownDependency);
    EXPECT_EQ(graph.SetDependenciesUVE(a, {b, b}).code,
              ResourceDependencyCodeUVE::DuplicateResource);
    EXPECT_EQ(graph.SetDependenciesUVE(c, {a}).code,
              ResourceDependencyCodeUVE::CycleDetected);
}

TEST(ResourceDependencyGraphUVETest, RemoveResourceUVE_BlocksDependentsAndAcceptsExactGenerationAfterDetach) {
    ResourceDependencyGraphUVE graph;
    const ResourceDependencyHandleUVE parent = HandleUVE(100U, 2U);
    const ResourceDependencyHandleUVE child = HandleUVE(200U, 1U);
    ASSERT_TRUE(graph.RegisterResourceUVE(parent).IsAppliedUVE());
    ASSERT_TRUE(graph.RegisterResourceUVE(child).IsAppliedUVE());
    ASSERT_TRUE(graph.SetDependenciesUVE(child, {parent}).IsAppliedUVE());

    EXPECT_EQ(graph.RemoveResourceUVE(parent).code, ResourceDependencyCodeUVE::HasDependents);
    EXPECT_EQ(graph.RemoveResourceUVE(ResourceDependencyHandleUVE{parent.guid, 1U}).code,
              ResourceDependencyCodeUVE::StaleGeneration);
    ASSERT_TRUE(graph.SetDependenciesUVE(child, {}).IsAppliedUVE());
    EXPECT_EQ(graph.RemoveResourceUVE(parent).code, ResourceDependencyCodeUVE::Removed);
    EXPECT_FALSE(graph.HasResourceUVE(parent));
}

TEST(ResourceDependencyGraphUVETest, GetDependentClosureUVE_ReturnsDeterministicTransitivePlan) {
    ResourceDependencyGraphUVE graph;
    const ResourceDependencyHandleUVE root = HandleUVE(10U);
    const ResourceDependencyHandleUVE directA = HandleUVE(20U);
    const ResourceDependencyHandleUVE directB = HandleUVE(30U);
    const ResourceDependencyHandleUVE transitiveA = HandleUVE(40U);
    const ResourceDependencyHandleUVE transitiveB = HandleUVE(50U);
    for (const ResourceDependencyHandleUVE handle : {transitiveB, directB, root, transitiveA, directA}) {
        ASSERT_TRUE(graph.RegisterResourceUVE(handle).IsAppliedUVE());
    }
    ASSERT_TRUE(graph.SetDependenciesUVE(directA, {root}).IsAppliedUVE());
    ASSERT_TRUE(graph.SetDependenciesUVE(directB, {root}).IsAppliedUVE());
    ASSERT_TRUE(graph.SetDependenciesUVE(transitiveA, {directA}).IsAppliedUVE());
    ASSERT_TRUE(graph.SetDependenciesUVE(transitiveB, {directB}).IsAppliedUVE());

    const ResourceDependencyInvalidationPlanUVE plan = graph.GetDependentClosureUVE(root);

    ASSERT_TRUE(plan.IsReadyUVE());
    EXPECT_EQ(plan.code, ResourceDependencyCodeUVE::DependentClosureReady);
    EXPECT_EQ(plan.graphGeneration, 9U);
    EXPECT_EQ(plan.root, root);
    EXPECT_FALSE(plan.dependentsTruncated);
    ASSERT_EQ(plan.dependents.size(), 4U);
    EXPECT_EQ(plan.dependents[0], directA);
    EXPECT_EQ(plan.dependents[1], directB);
    EXPECT_EQ(plan.dependents[2], transitiveA);
    EXPECT_EQ(plan.dependents[3], transitiveB);
}

TEST(ResourceDependencyGraphUVETest, GetDependentClosureUVE_BoundsResultAndClassifiesRoot) {
    ResourceDependencyGraphUVE graph;
    const ResourceDependencyHandleUVE root = HandleUVE(1U, 3U);
    const ResourceDependencyHandleUVE dependentA = HandleUVE(2U);
    const ResourceDependencyHandleUVE dependentB = HandleUVE(3U);
    ASSERT_TRUE(graph.RegisterResourceUVE(root).IsAppliedUVE());
    ASSERT_TRUE(graph.RegisterResourceUVE(dependentA).IsAppliedUVE());
    ASSERT_TRUE(graph.RegisterResourceUVE(dependentB).IsAppliedUVE());
    ASSERT_TRUE(graph.SetDependenciesUVE(dependentA, {root}).IsAppliedUVE());
    ASSERT_TRUE(graph.SetDependenciesUVE(dependentB, {root}).IsAppliedUVE());

    const ResourceDependencyInvalidationPlanUVE bounded = graph.GetDependentClosureUVE(root, 1U);
    EXPECT_TRUE(bounded.IsReadyUVE());
    EXPECT_TRUE(bounded.dependentsTruncated);
    ASSERT_EQ(bounded.dependents.size(), 1U);
    EXPECT_EQ(bounded.dependents.front(), dependentA);
    EXPECT_EQ(graph.GetDependentClosureUVE(ResourceDependencyHandleUVE{root.guid, 2U}).code,
              ResourceDependencyCodeUVE::StaleGeneration);
    EXPECT_EQ(graph.GetDependentClosureUVE(HandleUVE(99U)).code,
              ResourceDependencyCodeUVE::UnknownDependency);
    EXPECT_EQ(graph.GetDependentClosureUVE(ResourceDependencyHandleUVE{}).code,
              ResourceDependencyCodeUVE::InvalidHandle);
}

} // namespace UVE::Asset
