// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "resource_dependency_graph.h"

#include <gtest/gtest.h>

namespace UVE::Asset {
namespace {

ResourceDependencyHandle Handle(const std::uint64_t guid, const std::uint64_t generation = 1U) {
    return ResourceDependencyHandle{AssetGuid{guid}, generation};
}

} // namespace

TEST(ResourceDependencyGraphTest, RegisterAndSetDependencies_ProducesSortedCopiedSnapshot) {
    ResourceDependencyGraph graph;
    const ResourceDependencyHandle a = Handle(30U);
    const ResourceDependencyHandle b = Handle(10U);
    const ResourceDependencyHandle c = Handle(20U);
    ASSERT_TRUE(graph.RegisterResource(a).IsApplied());
    ASSERT_TRUE(graph.RegisterResource(b).IsApplied());
    ASSERT_TRUE(graph.RegisterResource(c).IsApplied());
    ASSERT_TRUE(graph.SetDependencies(a, {c, b}).IsApplied());

    const ResourceDependencySnapshot snapshot = graph.GetSnapshot();
    ASSERT_EQ(snapshot.entries.size(), 3U);
    EXPECT_EQ(snapshot.entries[0].handle.guid.value, 10U);
    EXPECT_EQ(snapshot.entries[1].handle.guid.value, 20U);
    EXPECT_EQ(snapshot.entries[2].handle.guid.value, 30U);
    ASSERT_EQ(snapshot.entries[2].dependencies.size(), 2U);
    EXPECT_EQ(snapshot.entries[2].dependencies[0].guid.value, 20U);
    EXPECT_EQ(snapshot.entries[2].dependencies[1].guid.value, 10U);
    EXPECT_EQ(snapshot.graphGeneration, 4U);
}

TEST(ResourceDependencyGraphTest, SetDependencies_RejectsStaleUnknownDuplicateAndCycleReferences) {
    ResourceDependencyGraph graph;
    const ResourceDependencyHandle a = Handle(1U, 4U);
    const ResourceDependencyHandle b = Handle(2U, 1U);
    const ResourceDependencyHandle c = Handle(3U, 1U);
    ASSERT_TRUE(graph.RegisterResource(a).IsApplied());
    ASSERT_TRUE(graph.RegisterResource(b).IsApplied());
    ASSERT_TRUE(graph.RegisterResource(c).IsApplied());
    ASSERT_TRUE(graph.SetDependencies(a, {b}).IsApplied());
    ASSERT_TRUE(graph.SetDependencies(b, {c}).IsApplied());

    EXPECT_EQ(graph.SetDependencies(ResourceDependencyHandle{a.guid, 3U}, {b}).code,
              ResourceDependencyCode::StaleGeneration);
    EXPECT_EQ(graph.SetDependencies(a, {Handle(99U)}).code,
              ResourceDependencyCode::UnknownDependency);
    EXPECT_EQ(graph.SetDependencies(a, {b, b}).code,
              ResourceDependencyCode::DuplicateResource);
    EXPECT_EQ(graph.SetDependencies(c, {a}).code,
              ResourceDependencyCode::CycleDetected);
}

TEST(ResourceDependencyGraphTest, RemoveResource_BlocksDependentsAndAcceptsExactGenerationAfterDetach) {
    ResourceDependencyGraph graph;
    const ResourceDependencyHandle parent = Handle(100U, 2U);
    const ResourceDependencyHandle child = Handle(200U, 1U);
    ASSERT_TRUE(graph.RegisterResource(parent).IsApplied());
    ASSERT_TRUE(graph.RegisterResource(child).IsApplied());
    ASSERT_TRUE(graph.SetDependencies(child, {parent}).IsApplied());

    EXPECT_EQ(graph.RemoveResource(parent).code, ResourceDependencyCode::HasDependents);
    EXPECT_EQ(graph.RemoveResource(ResourceDependencyHandle{parent.guid, 1U}).code,
              ResourceDependencyCode::StaleGeneration);
    ASSERT_TRUE(graph.SetDependencies(child, {}).IsApplied());
    EXPECT_EQ(graph.RemoveResource(parent).code, ResourceDependencyCode::Removed);
    EXPECT_FALSE(graph.HasResource(parent));
}

TEST(ResourceDependencyGraphTest, GetDependentClosure_ReturnsDeterministicTransitivePlan) {
    ResourceDependencyGraph graph;
    const ResourceDependencyHandle root = Handle(10U);
    const ResourceDependencyHandle directA = Handle(20U);
    const ResourceDependencyHandle directB = Handle(30U);
    const ResourceDependencyHandle transitiveA = Handle(40U);
    const ResourceDependencyHandle transitiveB = Handle(50U);
    for (const ResourceDependencyHandle handle : {transitiveB, directB, root, transitiveA, directA}) {
        ASSERT_TRUE(graph.RegisterResource(handle).IsApplied());
    }
    ASSERT_TRUE(graph.SetDependencies(directA, {root}).IsApplied());
    ASSERT_TRUE(graph.SetDependencies(directB, {root}).IsApplied());
    ASSERT_TRUE(graph.SetDependencies(transitiveA, {directA}).IsApplied());
    ASSERT_TRUE(graph.SetDependencies(transitiveB, {directB}).IsApplied());

    const ResourceDependencyInvalidationPlan plan = graph.GetDependentClosure(root);

    ASSERT_TRUE(plan.IsReady());
    EXPECT_EQ(plan.code, ResourceDependencyCode::DependentClosureReady);
    EXPECT_EQ(plan.graphGeneration, 9U);
    EXPECT_EQ(plan.root, root);
    EXPECT_FALSE(plan.dependentsTruncated);
    ASSERT_EQ(plan.dependents.size(), 4U);
    EXPECT_EQ(plan.dependents[0], directA);
    EXPECT_EQ(plan.dependents[1], directB);
    EXPECT_EQ(plan.dependents[2], transitiveA);
    EXPECT_EQ(plan.dependents[3], transitiveB);
}

TEST(ResourceDependencyGraphTest, GetDependentClosure_BoundsResultAndClassifiesRoot) {
    ResourceDependencyGraph graph;
    const ResourceDependencyHandle root = Handle(1U, 3U);
    const ResourceDependencyHandle dependentA = Handle(2U);
    const ResourceDependencyHandle dependentB = Handle(3U);
    ASSERT_TRUE(graph.RegisterResource(root).IsApplied());
    ASSERT_TRUE(graph.RegisterResource(dependentA).IsApplied());
    ASSERT_TRUE(graph.RegisterResource(dependentB).IsApplied());
    ASSERT_TRUE(graph.SetDependencies(dependentA, {root}).IsApplied());
    ASSERT_TRUE(graph.SetDependencies(dependentB, {root}).IsApplied());

    const ResourceDependencyInvalidationPlan bounded = graph.GetDependentClosure(root, 1U);
    EXPECT_TRUE(bounded.IsReady());
    EXPECT_TRUE(bounded.dependentsTruncated);
    ASSERT_EQ(bounded.dependents.size(), 1U);
    EXPECT_EQ(bounded.dependents.front(), dependentA);
    EXPECT_EQ(graph.GetDependentClosure(ResourceDependencyHandle{root.guid, 2U}).code,
              ResourceDependencyCode::StaleGeneration);
    EXPECT_EQ(graph.GetDependentClosure(Handle(99U)).code,
              ResourceDependencyCode::UnknownDependency);
    EXPECT_EQ(graph.GetDependentClosure(ResourceDependencyHandle{}).code,
              ResourceDependencyCode::InvalidHandle);
}

} // namespace UVE::Asset
