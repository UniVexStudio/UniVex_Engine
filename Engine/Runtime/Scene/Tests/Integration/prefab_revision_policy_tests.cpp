// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prefab_revision_policy.h"
#include "prefab_instance_component.h"
#include <gtest/gtest.h>
namespace UVE::Scene::Tests {
namespace {
TEST(PrefabRevisionPolicyTest, EqualNonzeroRevisionsAreCurrent) {
    EXPECT_EQ(EvaluatePrefabRevision(7U, 7U), PrefabRevisionStatus::Current);
}
TEST(PrefabRevisionPolicyTest, MismatchedNonzeroRevisionsAreStale) {
    EXPECT_EQ(EvaluatePrefabRevision(8U, 7U), PrefabRevisionStatus::Stale);
}
TEST(PrefabRevisionPolicyTest, ZeroRevisionIsInvalid) {
    EXPECT_EQ(EvaluatePrefabRevision(0U, 7U), PrefabRevisionStatus::Invalid);
    EXPECT_EQ(EvaluatePrefabRevision(7U, 0U), PrefabRevisionStatus::Invalid);
}
TEST(PrefabRevisionPolicyTest, RefreshPrefabInstanceRevision_UpdatesBothRevisionsWithoutOverrides) {
    PrefabInstanceComponent instance{Asset::AssetGuid{7U}, {}, 3U, 3U};

    EXPECT_TRUE(RefreshPrefabInstanceRevision(instance, 9U));
    EXPECT_EQ(instance.sourceRevision, 9U);
    EXPECT_EQ(instance.instanceRevision, 9U);
}
TEST(PrefabRevisionPolicyTest, RefreshPrefabInstanceRevision_RejectsLocalOverridesAtomically) {
    PrefabInstanceComponent instance{Asset::AssetGuid{8U}, {{"Transform.position", "[1,2,3]"}}, 3U, 3U};

    EXPECT_FALSE(RefreshPrefabInstanceRevision(instance, 9U));
    EXPECT_EQ(instance.sourceRevision, 3U);
    EXPECT_EQ(instance.instanceRevision, 3U);
    ASSERT_EQ(instance.overrides.size(), 1U);
    EXPECT_EQ(instance.overrides.front().serializedValue, "[1,2,3]");
}
TEST(PrefabRevisionPolicyTest, RefreshPrefabInstanceRevision_RejectsRevisionRegressionAtomically) {
    PrefabInstanceComponent instance{Asset::AssetGuid{9U}, {}, 7U, 7U};

    EXPECT_FALSE(RefreshPrefabInstanceRevision(instance, 6U));
    EXPECT_EQ(instance.sourceRevision, 7U);
    EXPECT_EQ(instance.instanceRevision, 7U);
}
TEST(PrefabRevisionPolicyTest, EvaluatePrefabRevisionRefreshDecision_DistinguishesNoOpRefreshAndMerge) {
    EXPECT_EQ(EvaluatePrefabRevisionRefreshDecision(7U, 7U, false),
              PrefabRevisionRefreshDecision::NoOp);
    EXPECT_EQ(EvaluatePrefabRevisionRefreshDecision(9U, 7U, false),
              PrefabRevisionRefreshDecision::Refresh);
    EXPECT_EQ(EvaluatePrefabRevisionRefreshDecision(9U, 7U, true),
              PrefabRevisionRefreshDecision::MergeRequired);
}
TEST(PrefabRevisionPolicyTest, EvaluatePrefabRevisionRefreshDecision_RejectsInvalidRevisionOrder) {
    EXPECT_EQ(EvaluatePrefabRevisionRefreshDecision(0U, 7U, false),
              PrefabRevisionRefreshDecision::Invalid);
    EXPECT_EQ(EvaluatePrefabRevisionRefreshDecision(6U, 7U, false),
              PrefabRevisionRefreshDecision::Invalid);
}
} // namespace
} // namespace UVE::Scene::Tests
