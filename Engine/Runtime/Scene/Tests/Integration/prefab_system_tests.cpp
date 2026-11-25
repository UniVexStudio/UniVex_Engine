// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "prefab_system.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "asset_database.h"
#include "log_sink.h"
#include "logger.h"
#include "event_system.h"
#include "memory_manager.h"
#include "hierarchy_component.h"
#include "mesh_component.h"
#include "prefab_instance_component.h"
#include "rigid_3d_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "scene_graph.h"

namespace UVE::Scene::Tests {
namespace {

class FakePrefabOverrideTarget final : public IPrefabOverrideTarget {
public:
    std::map<std::string, std::string> values;
    std::string failingReadPath;
    std::string failingWritePath;
    std::string throwingReadPath;
    std::string throwingWritePath;
    std::string throwingWriteValue;
    std::size_t readCount = 0U;
    std::size_t writeCount = 0U;

    [[nodiscard]] bool ReadProperty(const std::string_view propertyPath,
                                       std::string& serializedValue) const override {
        ++const_cast<FakePrefabOverrideTarget*>(this)->readCount;
        if (!throwingReadPath.empty() && propertyPath == throwingReadPath) {
            throw std::runtime_error("injected prefab read exception");
        }
        if (!failingReadPath.empty() && propertyPath == failingReadPath) {
            return false;
        }
        const auto iterator = values.find(std::string{propertyPath});
        if (iterator == values.end()) {
            return false;
        }
        serializedValue = iterator->second;
        return true;
    }

    [[nodiscard]] bool WriteProperty(const std::string_view propertyPath,
                                        const std::string_view serializedValue) override {
        ++writeCount;
        if (!throwingWritePath.empty() && propertyPath == throwingWritePath &&
            (throwingWriteValue.empty() || serializedValue == throwingWriteValue)) {
            throw std::runtime_error("injected prefab write exception");
        }
        if (!failingWritePath.empty() && propertyPath == failingWritePath) {
            return false;
        }
        const auto iterator = values.find(std::string{propertyPath});
        if (iterator == values.end()) {
            return false;
        }
        iterator->second = serializedValue;
        return true;
    }
};

class RejectingPrefabAssetDatabase final : public Asset::IAssetDatabase {
public:
    bool Load(const std::filesystem::path&) override { return false; }
    bool Save() override {
        ++saveCallCount;
        return false;
    }
    bool Save(const std::filesystem::path&) override { return false; }
    [[nodiscard]] Asset::AssetGuid Register(const std::filesystem::path&) override {
        return Asset::kInvalidAssetGuid;
    }
    [[nodiscard]] std::filesystem::path Resolve(Asset::AssetGuid) const override { return {}; }
    [[nodiscard]] bool HasGuid(Asset::AssetGuid) const override { return false; }
    [[nodiscard]] std::vector<Asset::AssetRecord> GetRegisteredAssets() const override { return {}; }

    std::size_t saveCallCount = 0U;
};

class ThrowingPrefabAssetDatabase final : public Asset::IAssetDatabase {
public:
    bool Load(const std::filesystem::path&) override { return false; }
    bool Save() override { return true; }
    bool Save(const std::filesystem::path&) override { return true; }
    [[nodiscard]] Asset::AssetGuid Register(const std::filesystem::path&) override {
        if (throwOnRegister) {
            if (throwUnknown) {
                throw 7;
            }
            throw std::runtime_error("injected prefab registration failure");
        }
        return Asset::AssetGuid{99U};
    }
    [[nodiscard]] std::filesystem::path Resolve(Asset::AssetGuid) const override {
        if (throwOnResolve) {
            if (throwUnknown) {
                throw 8;
            }
            throw std::runtime_error("injected prefab resolution failure");
        }
        return {};
    }
    [[nodiscard]] bool HasGuid(Asset::AssetGuid) const override { return false; }
    [[nodiscard]] std::vector<Asset::AssetRecord> GetRegisteredAssets() const override { return {}; }

    bool throwOnRegister = false;
    bool throwOnResolve = false;
    bool throwUnknown = false;
};

class PrefabSystemTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    SceneGraph sceneGraph;
    Asset::AssetDatabase assetDatabase;
    PrefabSystem prefabSystem;
};

TEST(PrefabInstanceComponent, ApplyAndRevertPrefabOverrides_RestoresExplicitBaseline) {
    PrefabInstanceComponent instance{
        Asset::AssetGuid{7U},
        {{"Transform.position", "[1,2,3]"}, {"Transform.rotation", "[0,0,0,1]"}}};
    const std::vector<PrefabPropertyOverride> baseline{
        {"Transform.position", "[0,0,0]"}, {"Transform.rotation", "[0,0,0,1]"}};
    FakePrefabOverrideTarget target;
    target.values = {{"Transform.position", "[0,0,0]"}, {"Transform.rotation", "[0,0,0,1]"}};

    const PrefabOverrideOperationResult applied = ApplyPrefabOverrides(instance, target);
    ASSERT_TRUE(applied.IsApplied());
    EXPECT_EQ(applied.affectedCount, 2U);
    EXPECT_EQ(target.values.at("Transform.position"), "[1,2,3]");
    EXPECT_EQ(instance.overrides.size(), 2U);

    const PrefabOverrideOperationResult reverted = RevertPrefabOverrides(instance, baseline, target);
    ASSERT_TRUE(reverted.IsApplied());
    EXPECT_EQ(reverted.affectedCount, 2U);
    EXPECT_EQ(target.values.at("Transform.position"), "[0,0,0]");
    EXPECT_EQ(target.values.at("Transform.rotation"), "[0,0,0,1]");
    EXPECT_TRUE(instance.overrides.empty());
}

TEST(PrefabInstanceComponent, CommitPrefabOverridesToSource_BakesAndClearsAfterCleanBaseline) {
    PrefabInstanceComponent instance{Asset::AssetGuid{13U}, {{"A.value", "new-a"}}};
    const std::vector<PrefabPropertyOverride> baseline{{"A.value", "old-a"}};
    FakePrefabOverrideTarget source;
    source.values = {{"A.value", "old-a"}};

    const PrefabOverrideOperationResult result =
        CommitPrefabOverridesToSource(instance, baseline, source);

    ASSERT_TRUE(result.IsApplied());
    EXPECT_EQ(result.affectedCount, 1U);
    EXPECT_EQ(source.values.at("A.value"), "new-a");
    EXPECT_TRUE(instance.overrides.empty());
}

TEST(PrefabInstanceComponent, CommitPrefabOverridesToSource_PublishesCommittedRevisionAfterSuccess) {
    PrefabInstanceComponent instance{Asset::AssetGuid{16U}, { {"A.value", "new-a"} }, 4U, 4U};
    const std::vector<PrefabPropertyOverride> baseline{{"A.value", "old-a"}};
    FakePrefabOverrideTarget source;
    source.values = {{"A.value", "old-a"}};

    const PrefabOverrideOperationResult result =
        CommitPrefabOverridesToSource(instance, baseline, source, 5U);

    ASSERT_TRUE(result.IsApplied());
    EXPECT_EQ(instance.sourceRevision, 5U);
    EXPECT_EQ(instance.instanceRevision, 5U);
    EXPECT_TRUE(instance.overrides.empty());
}

TEST(PrefabInstanceComponent, CommitPrefabOverridesToSource_RejectsZeroCommittedRevisionAtomically) {
    PrefabInstanceComponent instance{Asset::AssetGuid{17U}, {{"A.value", "new-a"}}, 4U, 4U};
    const std::vector<PrefabPropertyOverride> baseline{{"A.value", "old-a"}};
    FakePrefabOverrideTarget source;
    source.values = {{"A.value", "old-a"}};

    const PrefabOverrideOperationResult result =
        CommitPrefabOverridesToSource(instance, baseline, source, 0U);

    EXPECT_EQ(result.code, PrefabOverrideOperationCode::InvalidInstance);
    EXPECT_EQ(instance.sourceRevision, 4U);
    EXPECT_EQ(instance.instanceRevision, 4U);
    ASSERT_EQ(instance.overrides.size(), 1U);
    EXPECT_EQ(source.writeCount, 0U);
}

TEST(PrefabInstanceComponent, CommitPrefabOverridesToSource_RejectsRevisionRegressionAtomically) {
    PrefabInstanceComponent instance{Asset::AssetGuid{18U}, {{"A.value", "new-a"}}, 4U, 4U};
    const std::vector<PrefabPropertyOverride> baseline{{"A.value", "old-a"}};
    FakePrefabOverrideTarget source;
    source.values = {{"A.value", "old-a"}};

    const PrefabOverrideOperationResult result =
        CommitPrefabOverridesToSource(instance, baseline, source, 3U);

    EXPECT_EQ(result.code, PrefabOverrideOperationCode::InvalidInstance);
    EXPECT_EQ(instance.sourceRevision, 4U);
    EXPECT_EQ(instance.instanceRevision, 4U);
    ASSERT_EQ(instance.overrides.size(), 1U);
    EXPECT_EQ(source.writeCount, 0U);
    EXPECT_EQ(source.values.at("A.value"), "old-a");
}

TEST(PrefabInstanceComponent, CommitPrefabOverridesToSource_RejectsStaleBaselineWithoutWrite) {
    PrefabInstanceComponent instance{Asset::AssetGuid{14U}, {{"A.value", "new-a"}}};
    const std::vector<PrefabPropertyOverride> baseline{{"A.value", "old-a"}};
    FakePrefabOverrideTarget source;
    source.values = {{"A.value", "external-a"}};

    const PrefabOverrideOperationResult result =
        CommitPrefabOverridesToSource(instance, baseline, source);

    EXPECT_EQ(result.code, PrefabOverrideOperationCode::ConflictDetected);
    EXPECT_EQ(source.writeCount, 0U);
    EXPECT_EQ(source.values.at("A.value"), "external-a");
    ASSERT_EQ(instance.overrides.size(), 1U);
}

TEST(PrefabInstanceComponent, CommitPrefabOverridesToSource_PreservesInstanceOnWriteFailure) {
    PrefabInstanceComponent instance{Asset::AssetGuid{15U}, {{"A.value", "new-a"}}};
    const std::vector<PrefabPropertyOverride> baseline{{"A.value", "old-a"}};
    FakePrefabOverrideTarget source;
    source.values = {{"A.value", "old-a"}};
    source.failingWritePath = "A.value";

    const PrefabOverrideOperationResult result =
        CommitPrefabOverridesToSource(instance, baseline, source);

    EXPECT_EQ(result.code, PrefabOverrideOperationCode::WriteFailed);
    EXPECT_EQ(source.values.at("A.value"), "old-a");
    ASSERT_EQ(instance.overrides.size(), 1U);
    EXPECT_EQ(instance.overrides.front().serializedValue, "new-a");
}

TEST(PrefabInstanceComponent, ApplyPrefabOverrides_RollsBackEarlierWritesOnFailure) {
    const PrefabInstanceComponent instance{
        Asset::AssetGuid{8U},
        {{"A.value", "new-a"}, {"B.value", "new-b"}}};
    FakePrefabOverrideTarget target;
    target.values = {{"A.value", "old-a"}, {"B.value", "old-b"}};
    target.failingWritePath = "B.value";

    const PrefabOverrideOperationResult result = ApplyPrefabOverrides(instance, target);
    EXPECT_EQ(result.code, PrefabOverrideOperationCode::WriteFailed);
    EXPECT_EQ(target.values.at("A.value"), "old-a");
    EXPECT_EQ(target.values.at("B.value"), "old-b");
    EXPECT_EQ(instance.overrides.size(), 2U);
}

TEST(PrefabInstanceComponent, ApplyPrefabOverrides_StagesAllReadsBeforeWriting) {
    const PrefabInstanceComponent instance{
        Asset::AssetGuid{18U},
        {{"A.value", "new-a"}, {"B.value", "new-b"}}};
    FakePrefabOverrideTarget target;
    target.values = {{"A.value", "old-a"}, {"B.value", "old-b"}};
    target.failingReadPath = "B.value";

    const PrefabOverrideOperationResult result = ApplyPrefabOverrides(instance, target);

    EXPECT_EQ(result.code, PrefabOverrideOperationCode::ReadFailed);
    EXPECT_EQ(result.affectedCount, 1U);
    EXPECT_EQ(target.readCount, 2U);
    EXPECT_EQ(target.writeCount, 0U);
    EXPECT_EQ(target.values.at("A.value"), "old-a");
    EXPECT_EQ(target.values.at("B.value"), "old-b");
}

TEST(PrefabInstanceComponent, ApplyPrefabOverrides_ConvertsReadExceptionToFailureWithoutWriting) {
    const PrefabInstanceComponent instance{
        Asset::AssetGuid{19U},
        {{"A.value", "new-a"}, {"B.value", "new-b"}}};
    FakePrefabOverrideTarget target;
    target.values = {{"A.value", "old-a"}, {"B.value", "old-b"}};
    target.throwingReadPath = "B.value";

    const PrefabOverrideOperationResult result = ApplyPrefabOverrides(instance, target);

    EXPECT_EQ(result.code, PrefabOverrideOperationCode::ReadFailed);
    EXPECT_EQ(target.writeCount, 0U);
    EXPECT_EQ(target.values.at("A.value"), "old-a");
    EXPECT_EQ(target.values.at("B.value"), "old-b");
}

TEST(PrefabInstanceComponent, ApplyPrefabOverrides_RollsBackAfterWriteException) {
    const PrefabInstanceComponent instance{
        Asset::AssetGuid{20U},
        {{"A.value", "new-a"}, {"B.value", "new-b"}}};
    FakePrefabOverrideTarget target;
    target.values = {{"A.value", "old-a"}, {"B.value", "old-b"}};
    target.throwingWritePath = "B.value";
    target.throwingWriteValue = "new-b";

    const PrefabOverrideOperationResult result = ApplyPrefabOverrides(instance, target);

    EXPECT_EQ(result.code, PrefabOverrideOperationCode::WriteFailed);
    EXPECT_EQ(target.values.at("A.value"), "old-a");
    EXPECT_EQ(target.values.at("B.value"), "old-b");
}

TEST(PrefabInstanceComponent, DetectPrefabOverrideConflicts_ConvertsReadExceptionToReadFailed) {
    const PrefabInstanceComponent instance{Asset::AssetGuid{21U}, {{"A.value", "new-a"}}};
    const std::vector<PrefabPropertyOverride> baseline{{"A.value", "old-a"}};
    FakePrefabOverrideTarget target;
    target.values = {{"A.value", "old-a"}};
    target.throwingReadPath = "A.value";

    const PrefabOverrideConflictReport report =
        DetectPrefabOverrideConflicts(instance, baseline, target);

    EXPECT_EQ(report.code, PrefabOverrideOperationCode::ReadFailed);
    EXPECT_EQ(report.inspectedCount, 0U);
    EXPECT_TRUE(report.conflicts.empty());
}

TEST(PrefabInstanceComponent, DetectPrefabOverrideConflicts_CleanBaselineIsReadOnly) {
    const PrefabInstanceComponent instance{
        Asset::AssetGuid{10U}, {{"A.value", "new-a"}, {"B.value", "new-b"}}};
    const std::vector<PrefabPropertyOverride> baseline{{"A.value", "old-a"}, {"B.value", "old-b"}};
    FakePrefabOverrideTarget target;
    target.values = {{"A.value", "old-a"}, {"B.value", "old-b"}};

    const PrefabOverrideConflictReport report = DetectPrefabOverrideConflicts(instance, baseline, target);
    EXPECT_TRUE(report.IsConflictFree());
    EXPECT_EQ(report.inspectedCount, 2U);
    EXPECT_TRUE(report.conflicts.empty());
    EXPECT_FALSE(report.truncated);
    EXPECT_EQ(target.writeCount, 0U);
}

TEST(PrefabInstanceComponent, DetectPrefabOverrideConflicts_ReportsExternalMutationWithoutWriting) {
    const PrefabInstanceComponent instance{Asset::AssetGuid{11U}, {{"A.value", "new-a"}}};
    const std::vector<PrefabPropertyOverride> baseline{{"A.value", "old-a"}};
    FakePrefabOverrideTarget target;
    target.values = {{"A.value", "external-a"}};

    const PrefabOverrideConflictReport report = DetectPrefabOverrideConflicts(instance, baseline, target);
    ASSERT_EQ(report.code, PrefabOverrideOperationCode::ConflictDetected);
    ASSERT_EQ(report.conflicts.size(), 1U);
    EXPECT_EQ(report.conflicts.front().propertyPath, "A.value");
    EXPECT_EQ(report.conflicts.front().expectedSerializedValue, "old-a");
    EXPECT_EQ(report.conflicts.front().actualSerializedValue, "external-a");
    EXPECT_EQ(target.writeCount, 0U);
    EXPECT_EQ(target.values.at("A.value"), "external-a");
}

TEST(PrefabInstanceComponent, DetectPrefabOverrideConflicts_HonorsConflictHardCap) {
    const PrefabInstanceComponent instance{
        Asset::AssetGuid{12U}, {{"A.value", "new-a"}, {"B.value", "new-b"}}};
    const std::vector<PrefabPropertyOverride> baseline{{"A.value", "old-a"}, {"B.value", "old-b"}};
    FakePrefabOverrideTarget target;
    target.values = {{"A.value", "external-a"}, {"B.value", "external-b"}};

    const PrefabOverrideConflictReport report = DetectPrefabOverrideConflicts(instance, baseline, target, 1U);
    EXPECT_EQ(report.code, PrefabOverrideOperationCode::ConflictDetected);
    EXPECT_EQ(report.inspectedCount, 2U);
    EXPECT_EQ(report.conflicts.size(), 1U);
    EXPECT_TRUE(report.truncated);
    EXPECT_EQ(target.writeCount, 0U);
}

TEST(PrefabInstanceComponent, DetectPrefabOverrideConflicts_ClampsCallerLimitToSharedHardCap) {
    std::vector<PrefabPropertyOverride> overrides;
    std::vector<PrefabPropertyOverride> baseline;
    FakePrefabOverrideTarget target;
    overrides.reserve(kMaximumPrefabConflicts + 1U);
    baseline.reserve(kMaximumPrefabConflicts + 1U);
    for (std::size_t index = 0U; index <= kMaximumPrefabConflicts; ++index) {
        const std::string propertyPath = "A." + std::to_string(1000U + index);
        overrides.push_back({propertyPath, "new"});
        baseline.push_back({propertyPath, "old"});
        target.values.emplace(propertyPath, "external");
    }

    const PrefabInstanceComponent instance{Asset::AssetGuid{13U}, overrides};
    const PrefabOverrideConflictReport report = DetectPrefabOverrideConflicts(
        instance, baseline, target, kMaximumPrefabConflicts + 100U);
    EXPECT_EQ(report.code, PrefabOverrideOperationCode::ConflictDetected);
    EXPECT_EQ(report.inspectedCount, kMaximumPrefabConflicts + 1U);
    EXPECT_EQ(report.conflicts.size(), kMaximumPrefabConflicts);
    EXPECT_TRUE(report.truncated);
    EXPECT_EQ(target.writeCount, 0U);
}

TEST(PrefabInstanceComponent, MergePrefabOverrides_CombinesNonConflictingLocalAndRemoteChanges) {
    const PrefabInstanceComponent instance{
        Asset::AssetGuid{14U}, {{"A.value", "base-a"}, {"B.value", "local-b"}}};
    const std::vector<PrefabPropertyOverride> baseline{{"A.value", "base-a"}, {"B.value", "base-b"}};
    const std::vector<PrefabPropertyOverride> remote{{"A.value", "remote-a"}, {"B.value", "base-b"}};
    std::vector<PrefabPropertyOverride> merged;

    const PrefabOverrideOperationResult result =
        MergePrefabOverrides(instance, baseline, remote, merged);

    EXPECT_EQ(result.code, PrefabOverrideOperationCode::Applied);
    EXPECT_EQ(result.affectedCount, 2U);
    ASSERT_EQ(merged.size(), 2U);
    EXPECT_EQ(merged[0].propertyPath, "A.value");
    EXPECT_EQ(merged[0].serializedValue, "remote-a");
    EXPECT_EQ(merged[1].propertyPath, "B.value");
    EXPECT_EQ(merged[1].serializedValue, "local-b");
}

TEST(PrefabInstanceComponent, MergePrefabOverrides_RejectsConflictsWithoutPartialOutput) {
    const PrefabInstanceComponent instance{
        Asset::AssetGuid{15U}, {{"A.value", "local-a"}}};
    const std::vector<PrefabPropertyOverride> baseline{{"A.value", "base-a"}};
    const std::vector<PrefabPropertyOverride> remote{{"A.value", "remote-a"}};
    std::vector<PrefabPropertyOverride> merged{{"sentinel", "unchanged"}};

    const PrefabOverrideOperationResult result =
        MergePrefabOverrides(instance, baseline, remote, merged);

    EXPECT_EQ(result.code, PrefabOverrideOperationCode::ConflictDetected);
    EXPECT_EQ(result.affectedCount, 1U);
    ASSERT_EQ(merged.size(), 1U);
    EXPECT_EQ(merged.front().propertyPath, "sentinel");
}

TEST(PrefabInstanceComponent, RevertPrefabOverrides_RejectsInvalidBaselineWithoutMutation) {
    PrefabInstanceComponent instance{Asset::AssetGuid{9U}, {{"A.value", "new-a"}}};
    FakePrefabOverrideTarget target;
    target.values = {{"A.value", "current-a"}};
    const std::vector<PrefabPropertyOverride> invalidBaseline{{"B.value", "one"}, {"A.value", "two"}};

    const PrefabOverrideOperationResult result = RevertPrefabOverrides(instance, invalidBaseline, target);
    EXPECT_EQ(result.code, PrefabOverrideOperationCode::InvalidBaseline);
    EXPECT_EQ(target.values.at("A.value"), "current-a");
    ASSERT_EQ(instance.overrides.size(), 1U);
    EXPECT_EQ(instance.overrides.front().serializedValue, "new-a");
}

TEST(PrefabInstanceComponent, IsPrefabInstanceComponentValid_RequiresSourceGuidAndSortedOverrides) {
    EXPECT_FALSE(IsPrefabInstanceComponentValid(
        PrefabInstanceComponent{Asset::kInvalidAssetGuid, {}}));
    EXPECT_TRUE(IsPrefabInstanceComponentValid(PrefabInstanceComponent{Asset::AssetGuid{7U}, {}}));
    EXPECT_TRUE(IsPrefabInstanceComponentValid(PrefabInstanceComponent{
        Asset::AssetGuid{7U}, {{"Transform.position", "[1,2,3]"}, {"Transform.rotation", "[0,0,0,1]"}}}));
    EXPECT_FALSE(IsPrefabInstanceComponentValid(PrefabInstanceComponent{
        Asset::AssetGuid{7U}, {{"Transform.rotation", "[0,0,0,1]"}, {"Transform.position", "[1,2,3]"}}}));
    EXPECT_FALSE(IsPrefabInstanceComponentValid(
        PrefabInstanceComponent{Asset::AssetGuid{7U}, {{"Transform.position", ""}}}));
    EXPECT_FALSE(IsPrefabInstanceComponentValid(PrefabInstanceComponent{
        Asset::AssetGuid{7U}, std::vector<PrefabPropertyOverride>(kMaximumPrefabOverrides + 1U)}));
}

TEST_F(PrefabSystemTest, SaveThenInstantiate_ProducesEntityWithSameComponentValues) {
    const Entity source = entityManager.CreateEntity();
    entityManager.AddComponent<MeshComponent>(source, MeshComponent{Asset::AssetGuid{11}, Asset::AssetGuid{12}});

    const std::filesystem::path prefabPath = "uve_prefab_tests_oak.uvprefab";
    std::filesystem::remove(prefabPath);
    const Asset::AssetGuid guid = prefabSystem.SavePrefab(entityManager, assetDatabase, source, prefabPath);
    ASSERT_NE(guid, Asset::kInvalidAssetGuid);

    const Entity instance =
        prefabSystem.Instantiate(entityManager, sceneGraph, assetDatabase, guid, kInvalidEntity);
    ASSERT_NE(instance, kInvalidEntity);
    ASSERT_NE(instance, source);

    EXPECT_EQ(entityManager.GetComponent<MeshComponent>(instance).meshGuid, Asset::AssetGuid{11});
    ASSERT_TRUE(entityManager.HasComponent<PrefabInstanceComponent>(instance));
    EXPECT_EQ(entityManager.GetComponent<PrefabInstanceComponent>(instance).sourcePrefabGuid, guid);
    const std::optional<std::uint64_t> expectedRevision = ComputePrefabSourceRevision(prefabPath);
    ASSERT_TRUE(expectedRevision.has_value());
    EXPECT_EQ(entityManager.GetComponent<PrefabInstanceComponent>(instance).sourceRevision, *expectedRevision);
    EXPECT_EQ(entityManager.GetComponent<PrefabInstanceComponent>(instance).instanceRevision, *expectedRevision);

    std::filesystem::remove(prefabPath);
}

TEST_F(PrefabSystemTest, InstantiateWithRevision_StampsRevisionAndRejectsZero) {
    const Entity source = entityManager.CreateEntity();
    entityManager.AddComponent<MeshComponent>(source, MeshComponent{Asset::AssetGuid{13}, Asset::AssetGuid{14}});
    const std::filesystem::path prefabPath = "uve_prefab_tests_revision.uvprefab";
    std::filesystem::remove(prefabPath);
    const Asset::AssetGuid guid = prefabSystem.SavePrefab(entityManager, assetDatabase, source, prefabPath);
    ASSERT_NE(guid, Asset::kInvalidAssetGuid);

    const Entity instance = prefabSystem.InstantiateWithRevision(
        entityManager, sceneGraph, assetDatabase, guid, kInvalidEntity, 42U);
    ASSERT_NE(instance, kInvalidEntity);
    const PrefabInstanceComponent& component =
        entityManager.GetComponent<PrefabInstanceComponent>(instance);
    EXPECT_EQ(component.sourceRevision, 42U);
    EXPECT_EQ(component.instanceRevision, 42U);
    const std::size_t entityCountAfterValid = entityManager.GetEntityCount();

    EXPECT_EQ(prefabSystem.InstantiateWithRevision(entityManager, sceneGraph, assetDatabase, guid,
                                                       kInvalidEntity, 0U),
              kInvalidEntity);
    EXPECT_EQ(entityManager.GetEntityCount(), entityCountAfterValid);
    std::filesystem::remove(prefabPath);
}

TEST_F(PrefabSystemTest, InstantiateTwice_ProducesIndependentEntities) {
    const Entity source = entityManager.CreateEntity();
    entityManager.AddComponent<Rigid3DComponent>(source, Rigid3DComponent{1.0F, false});

    const std::filesystem::path prefabPath = "uve_prefab_tests_independent.uvprefab";
    std::filesystem::remove(prefabPath);
    const Asset::AssetGuid guid = prefabSystem.SavePrefab(entityManager, assetDatabase, source, prefabPath);

    const Entity instanceA =
        prefabSystem.Instantiate(entityManager, sceneGraph, assetDatabase, guid, kInvalidEntity);
    const Entity instanceB =
        prefabSystem.Instantiate(entityManager, sceneGraph, assetDatabase, guid, kInvalidEntity);
    ASSERT_NE(instanceA, instanceB);

    entityManager.GetComponent<Rigid3DComponent>(instanceA).mass = 99.0F;
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Rigid3DComponent>(instanceB).mass, 1.0F);

    std::filesystem::remove(prefabPath);
}

TEST_F(PrefabSystemTest, Instantiate_InvalidParentFailsBeforeEntityMutation) {
    const Entity source = entityManager.CreateEntity();
    entityManager.AddComponent<MeshComponent>(source, MeshComponent{Asset::AssetGuid{21}, Asset::AssetGuid{22}});
    sceneGraph.AttachTransform(entityManager, source, TransformComponent{});

    const std::filesystem::path prefabPath = "uve_prefab_tests_invalid_parent.uvprefab";
    std::filesystem::remove(prefabPath);
    const Asset::AssetGuid guid = prefabSystem.SavePrefab(entityManager, assetDatabase, source, prefabPath);
    const std::size_t entityCountBefore = entityManager.GetEntityCount();
    const Entity invalidParent{0xFFFFFFFFU, 1U};

    const Entity instance = prefabSystem.Instantiate(entityManager, sceneGraph, assetDatabase, guid, invalidParent);

    EXPECT_EQ(instance, kInvalidEntity);
    EXPECT_EQ(entityManager.GetEntityCount(), entityCountBefore);
    std::filesystem::remove(prefabPath);
}

TEST_F(PrefabSystemTest, Instantiate_WithParent_ReparentsNewRoot) {
    const Entity parent = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, parent, TransformComponent{});

    const Entity source = entityManager.CreateEntity();
    entityManager.AddComponent<MeshComponent>(source, MeshComponent{Asset::AssetGuid{21}, Asset::AssetGuid{22}});
    sceneGraph.AttachTransform(entityManager, source, TransformComponent{});

    const std::filesystem::path prefabPath = "uve_prefab_tests_reparent.uvprefab";
    std::filesystem::remove(prefabPath);
    const Asset::AssetGuid guid = prefabSystem.SavePrefab(entityManager, assetDatabase, source, prefabPath);

    const Entity instance = prefabSystem.Instantiate(entityManager, sceneGraph, assetDatabase, guid, parent);
    ASSERT_NE(instance, kInvalidEntity);
    EXPECT_EQ(entityManager.GetComponent<HierarchyComponent>(instance).parent, parent);

    std::filesystem::remove(prefabPath);
}

TEST_F(PrefabSystemTest, Instantiate_WithParent_AttachesTransformToTransformlessRoot) {
    const Entity parent = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, parent, TransformComponent{});

    const Entity source = entityManager.CreateEntity();
    entityManager.AddComponent<MeshComponent>(source, MeshComponent{Asset::AssetGuid{23}, Asset::AssetGuid{24}});

    const std::filesystem::path prefabPath = "uve_prefab_tests_transformless_parent.uvprefab";
    std::filesystem::remove(prefabPath);
    const Asset::AssetGuid guid = prefabSystem.SavePrefab(entityManager, assetDatabase, source, prefabPath);
    ASSERT_NE(guid, Asset::kInvalidAssetGuid);

    const Entity instance = prefabSystem.Instantiate(entityManager, sceneGraph, assetDatabase, guid, parent);
    ASSERT_NE(instance, kInvalidEntity);
    ASSERT_TRUE(entityManager.HasComponent<TransformComponent>(instance));
    ASSERT_TRUE(entityManager.HasComponent<WorldTransformComponent>(instance));
    ASSERT_TRUE(entityManager.HasComponent<HierarchyComponent>(instance));
    EXPECT_EQ(entityManager.GetComponent<HierarchyComponent>(instance).parent, parent);

    std::filesystem::remove(prefabPath);
}

TEST_F(PrefabSystemTest, Instantiate_UnknownGuid_ReturnsInvalidAndLogsError) {
    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    const Entity instance = prefabSystem.Instantiate(entityManager, sceneGraph, assetDatabase,
                                                            Asset::AssetGuid{424242}, kInvalidEntity);
    EXPECT_EQ(instance, kInvalidEntity);

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Error &&
                   message.message.contains("unknown prefab GUID");
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
}

TEST_F(PrefabSystemTest, NestedPrefab_PreservesSourceGuidWithoutRecursiveReinstantiation) {
    const Entity innerSource = entityManager.CreateEntity();
    entityManager.AddComponent<MeshComponent>(innerSource, MeshComponent{Asset::AssetGuid{31}, Asset::AssetGuid{32}});
    sceneGraph.AttachTransform(entityManager, innerSource, TransformComponent{});
    const std::filesystem::path innerPath = "uve_prefab_tests_inner.uvprefab";
    std::filesystem::remove(innerPath);
    const Asset::AssetGuid innerGuid =
        prefabSystem.SavePrefab(entityManager, assetDatabase, innerSource, innerPath);

    const Entity outerRoot = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, outerRoot, TransformComponent{});
    const Entity innerInstance =
        prefabSystem.Instantiate(entityManager, sceneGraph, assetDatabase, innerGuid, outerRoot);
    ASSERT_NE(innerInstance, kInvalidEntity);

    const std::filesystem::path outerPath = "uve_prefab_tests_outer.uvprefab";
    std::filesystem::remove(outerPath);
    const Asset::AssetGuid outerGuid =
        prefabSystem.SavePrefab(entityManager, assetDatabase, outerRoot, outerPath);

    EntityManager freshManager(memoryManager.GetDefaultAllocator(), eventSystem);
    SceneGraph freshSceneGraph;
    const Entity outerInstance =
        prefabSystem.Instantiate(freshManager, freshSceneGraph, assetDatabase, outerGuid, kInvalidEntity);
    ASSERT_NE(outerInstance, kInvalidEntity);

    Entity nestedChild = kInvalidEntity;
    freshManager.ForEach<HierarchyComponent>(
        [&nestedChild, outerInstance](Entity entity, HierarchyComponent& hierarchy) {
            if (hierarchy.parent == outerInstance) {
                nestedChild = entity;
            }
        });
    ASSERT_NE(nestedChild, kInvalidEntity);
    ASSERT_TRUE(freshManager.HasComponent<MeshComponent>(nestedChild));
    EXPECT_EQ(freshManager.GetComponent<MeshComponent>(nestedChild).meshGuid, Asset::AssetGuid{31});
    ASSERT_TRUE(freshManager.HasComponent<PrefabInstanceComponent>(nestedChild));
    EXPECT_EQ(freshManager.GetComponent<PrefabInstanceComponent>(nestedChild).sourcePrefabGuid, innerGuid);

    std::filesystem::remove(innerPath);
    std::filesystem::remove(outerPath);
}

TEST_F(PrefabSystemTest, SavePrefab_RegistrationExceptionFailsClosed) {
    const Entity source = entityManager.CreateEntity();
    const std::filesystem::path path = "uve_prefab_tests_registration_exception.uvprefab";
    std::filesystem::remove(path);
    ThrowingPrefabAssetDatabase throwingDatabase;
    throwingDatabase.throwOnRegister = true;

    const Asset::AssetGuid guid = prefabSystem.SavePrefab(entityManager, throwingDatabase, source, path);

    EXPECT_EQ(guid, Asset::kInvalidAssetGuid);
    EXPECT_TRUE(std::filesystem::exists(path));
    std::filesystem::remove(path);
}

TEST_F(PrefabSystemTest, Instantiate_ResolutionUnknownExceptionFailsBeforeEntityPublication) {
    ThrowingPrefabAssetDatabase throwingDatabase;
    throwingDatabase.throwOnResolve = true;
    throwingDatabase.throwUnknown = true;

    const Entity instance = prefabSystem.Instantiate(entityManager, sceneGraph, throwingDatabase,
                                                            Asset::AssetGuid{424242}, kInvalidEntity);

    EXPECT_EQ(instance, kInvalidEntity);
}

TEST_F(PrefabSystemTest, SavePrefab_InvalidRegistrationFailsBeforeRegistrySave) {
    const Entity source = entityManager.CreateEntity();
    const std::filesystem::path path = "uve_prefab_tests_invalid_registration.uvprefab";
    std::filesystem::remove(path);
    RejectingPrefabAssetDatabase rejectingDatabase;

    const Asset::AssetGuid guid = prefabSystem.SavePrefab(entityManager, rejectingDatabase, source, path);

    EXPECT_EQ(guid, Asset::kInvalidAssetGuid);
    EXPECT_EQ(rejectingDatabase.saveCallCount, 0U);
    std::filesystem::remove(path);
}

TEST_F(PrefabSystemTest, SavePrefab_SamePathTwice_KeepsGuidStable) {
    const Entity source = entityManager.CreateEntity();
    entityManager.AddComponent<MeshComponent>(source, MeshComponent{Asset::AssetGuid{41}, Asset::AssetGuid{42}});

    const std::filesystem::path path = "uve_prefab_tests_stable_guid.uvprefab";
    std::filesystem::remove(path);

    const Asset::AssetGuid firstGuid = prefabSystem.SavePrefab(entityManager, assetDatabase, source, path);
    const Asset::AssetGuid secondGuid = prefabSystem.SavePrefab(entityManager, assetDatabase, source, path);
    EXPECT_EQ(firstGuid, secondGuid);

    std::filesystem::remove(path);
}

} // namespace
} // namespace UVE::Scene::Tests
