// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "project_change_watcher.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "asset_content_fingerprint.h"
#include "asset_database.h"
#include "derived_artifact_cache.h"
#include "i_derived_artifact_cache.h"
#include "project_change_watcher.h"

namespace UVE::Asset::Tests {
namespace {

void WriteFixtureFile(const std::filesystem::path& path, const std::string_view contents) {
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(file.is_open());
    file << contents;
    ASSERT_TRUE(file.good());
}

[[nodiscard]] const ProjectFileChange* FindChange(const ProjectChangeSnapshot& snapshot,
                                                         const std::string_view relativePath,
                                                         const ProjectFileChangeKind kind) {
    const auto found = std::find_if(snapshot.changes.begin(), snapshot.changes.end(),
                                    [relativePath, kind](const ProjectFileChange& change) {
                                        return change.relativePath.generic_string() == relativePath &&
                                               change.kind == kind;
                                    });
    return found == snapshot.changes.end() ? nullptr : &*found;
}

class RecordingDerivedArtifactCache final : public IDerivedArtifactCache {
public:
    [[nodiscard]] std::optional<DerivedArtifactCacheRecord>
    LoadImportRecord(const std::filesystem::path&) const override {
        return std::nullopt;
    }

    [[nodiscard]] bool StoreImportRecord(const std::filesystem::path&,
                                             const DerivedArtifactCacheRecord&) override {
        return false;
    }

    [[nodiscard]] std::size_t MarkStaleForSource(const std::filesystem::path& sourcePath) override {
        markedSources.push_back(sourcePath.lexically_normal());
        return staleRecordsPerChange;
    }

    [[nodiscard]] std::filesystem::path GetCacheRoot() const override { return {}; }

    std::size_t staleRecordsPerChange = 0U;
    std::vector<std::filesystem::path> markedSources;
};

class ProjectChangeWatcherTest : public ::testing::Test {
protected:
    const std::filesystem::path root = "uve_project_change_watcher_tests";
    AssetDatabase assetDatabase;
    RecordingDerivedArtifactCache derivedArtifactCache;

    void SetUp() override { std::filesystem::remove_all(root); }
    void TearDown() override { std::filesystem::remove_all(root); }
};

TEST_F(ProjectChangeWatcherTest, PollNow_InitialBaselineEmitsNoChanges) {
    WriteFixtureFile(root / "existing.txt", "existing");
    ProjectChangeWatcher watcher(root, 1.0, 16U);

    ASSERT_TRUE(watcher.PollNow(assetDatabase, derivedArtifactCache));
    const ProjectChangeSnapshot snapshot = watcher.GetSnapshot();
    EXPECT_EQ(snapshot.successfulScanGeneration, 1U);
    EXPECT_TRUE(snapshot.changes.empty());
    EXPECT_FALSE(snapshot.rescanRequired);
    EXPECT_TRUE(derivedArtifactCache.markedSources.empty());
}

TEST_F(ProjectChangeWatcherTest, PollNow_NormalizesRelativeTrailingSeparatorBeforeBaselineScan) {
    WriteFixtureFile(root / "existing.txt", "existing");
    const std::filesystem::path configuredRoot = root.generic_string() + "/";
    ProjectChangeWatcher watcher(configuredRoot, 1.0, 16U);

    ASSERT_TRUE(watcher.PollNow(assetDatabase, derivedArtifactCache));
    const ProjectChangeSnapshot snapshot = watcher.GetSnapshot();
    EXPECT_EQ(snapshot.contentRoot, root);
    EXPECT_EQ(snapshot.successfulScanGeneration, 1U);
    EXPECT_TRUE(snapshot.changes.empty());
    EXPECT_FALSE(snapshot.lastScanDiagnostic.has_value());
}

TEST_F(ProjectChangeWatcherTest, Poll_NonFiniteDeltaDoesNotMutateBaselineOrJournal) {
    const std::filesystem::path source = root / "tracked.txt";
    WriteFixtureFile(source, "before");
    ProjectChangeWatcher watcher(root, 1.0, 16U);
    ASSERT_TRUE(watcher.PollNow(assetDatabase, derivedArtifactCache));
    const ProjectChangeSnapshot baselineSnapshot = watcher.GetSnapshot();

    WriteFixtureFile(source, "after");
    EXPECT_FALSE(watcher.Poll(std::numeric_limits<double>::infinity(), assetDatabase, derivedArtifactCache));

    const ProjectChangeSnapshot retainedSnapshot = watcher.GetSnapshot();
    EXPECT_EQ(retainedSnapshot.successfulScanGeneration, baselineSnapshot.successfulScanGeneration);
    EXPECT_EQ(retainedSnapshot.latestSequence, baselineSnapshot.latestSequence);
    EXPECT_TRUE(retainedSnapshot.changes.empty());
    EXPECT_TRUE(derivedArtifactCache.markedSources.empty());

    EXPECT_FALSE(watcher.Poll(0.5, assetDatabase, derivedArtifactCache));
    EXPECT_TRUE(watcher.Poll(0.5, assetDatabase, derivedArtifactCache));
    const ProjectChangeSnapshot changedSnapshot = watcher.GetSnapshot();
    ASSERT_NE(FindChange(changedSnapshot, "tracked.txt", ProjectFileChangeKind::Modified), nullptr);
}

TEST_F(ProjectChangeWatcherTest, Poll_DefersScanUntilConfiguredInterval) {
    const std::filesystem::path source = root / "tracked.txt";
    WriteFixtureFile(source, "before");
    ProjectChangeWatcher watcher(root, 1.0, 16U);
    ASSERT_TRUE(watcher.PollNow(assetDatabase, derivedArtifactCache));

    WriteFixtureFile(source, "after");
    EXPECT_FALSE(watcher.Poll(0.4, assetDatabase, derivedArtifactCache));
    EXPECT_TRUE(watcher.GetSnapshot().changes.empty());
    EXPECT_TRUE(watcher.Poll(0.6, assetDatabase, derivedArtifactCache));

    const ProjectChangeSnapshot snapshot = watcher.GetSnapshot();
    ASSERT_NE(FindChange(snapshot, "tracked.txt", ProjectFileChangeKind::Modified), nullptr);
}

TEST_F(ProjectChangeWatcherTest, Poll_InitialPollHonorsConfiguredInterval) {
    WriteFixtureFile(root / "tracked.txt", "before");
    ProjectChangeWatcher watcher(root, 10.0, 16U);

    // Frame-1 delta well below pollIntervalSeconds must not trigger a synchronous initial scan.
    EXPECT_FALSE(watcher.Poll(0.016, assetDatabase, derivedArtifactCache));
    EXPECT_EQ(watcher.GetSnapshot().successfulScanGeneration, 0U);

    // Once the configured interval elapses, the initial baseline scan executes.
    EXPECT_TRUE(watcher.Poll(10.0, assetDatabase, derivedArtifactCache));
    EXPECT_EQ(watcher.GetSnapshot().successfulScanGeneration, 1U);
}

TEST_F(ProjectChangeWatcherTest, PollNow_JournalsCreateModifyAndRemoveEvents) {
    const std::filesystem::path source = root / "tracked.txt";
    ProjectChangeWatcher watcher(root, 60.0, 16U);
    ASSERT_TRUE(watcher.PollNow(assetDatabase, derivedArtifactCache));

    WriteFixtureFile(source, "created");
    ASSERT_TRUE(watcher.PollNow(assetDatabase, derivedArtifactCache));
    ProjectChangeSnapshot snapshot = watcher.GetSnapshot();
    ASSERT_NE(FindChange(snapshot, "tracked.txt", ProjectFileChangeKind::Created), nullptr);
    watcher.AcknowledgeThrough(snapshot.latestSequence);

    WriteFixtureFile(source, "modified");
    ASSERT_TRUE(watcher.PollNow(assetDatabase, derivedArtifactCache));
    snapshot = watcher.GetSnapshot();
    ASSERT_NE(FindChange(snapshot, "tracked.txt", ProjectFileChangeKind::Modified), nullptr);
    watcher.AcknowledgeThrough(snapshot.latestSequence);

    ASSERT_TRUE(std::filesystem::remove(source));
    ASSERT_TRUE(watcher.PollNow(assetDatabase, derivedArtifactCache));
    snapshot = watcher.GetSnapshot();
    ASSERT_NE(FindChange(snapshot, "tracked.txt", ProjectFileChangeKind::Removed), nullptr);
}

TEST_F(ProjectChangeWatcherTest, RemovedEvent_RetainsGuidCapturedByLastSuccessfulBaseline) {
    const std::filesystem::path source = root / "registered.txt";
    WriteFixtureFile(source, "registered");
    const AssetGuid guid = assetDatabase.Register(source);
    ASSERT_NE(guid, kInvalidAssetGuid);

    ProjectChangeWatcher watcher(root, 0.0, 16U);
    ASSERT_TRUE(watcher.PollNow(assetDatabase, derivedArtifactCache));
    ASSERT_TRUE(std::filesystem::remove(source));
    ASSERT_TRUE(watcher.PollNow(assetDatabase, derivedArtifactCache));

    const ProjectChangeSnapshot snapshot = watcher.GetSnapshot();
    const ProjectFileChange* const removed =
        FindChange(snapshot, "registered.txt", ProjectFileChangeKind::Removed);
    ASSERT_NE(removed, nullptr);
    EXPECT_EQ(removed->registeredAssetGuid, guid);
}

TEST_F(ProjectChangeWatcherTest, PollNow_UsesDeterministicLexicalChangeOrdering) {
    ProjectChangeWatcher watcher(root, 0.0, 16U);
    ASSERT_TRUE(watcher.PollNow(assetDatabase, derivedArtifactCache));

    WriteFixtureFile(root / "zeta.txt", "zeta");
    WriteFixtureFile(root / "alpha.txt", "alpha");
    ASSERT_TRUE(watcher.PollNow(assetDatabase, derivedArtifactCache));

    const ProjectChangeSnapshot snapshot = watcher.GetSnapshot();
    ASSERT_EQ(snapshot.changes.size(), 2U);
    EXPECT_EQ(snapshot.changes[0].relativePath.generic_string(), "alpha.txt");
    EXPECT_EQ(snapshot.changes[1].relativePath.generic_string(), "zeta.txt");
    EXPECT_LT(snapshot.changes[0].sequence, snapshot.changes[1].sequence);
}

TEST_F(ProjectChangeWatcherTest, PollNow_DoesNotFollowOrJournalSymlinkEntries) {
    const std::filesystem::path outside = root.parent_path() / "uve_project_change_watcher_tests_outside";
    std::filesystem::remove_all(outside);
    WriteFixtureFile(root / "safe.txt", "safe");
    WriteFixtureFile(outside / "outside.txt", "outside");

    ProjectChangeWatcher watcher(root, 0.0, 16U);
    ASSERT_TRUE(watcher.PollNow(assetDatabase, derivedArtifactCache));

    std::error_code errorCode;
    std::filesystem::create_directory_symlink(outside, root / "outside-link", errorCode);
    if (errorCode) {
        std::filesystem::remove_all(outside);
        GTEST_SKIP() << "Host does not permit test symlink creation: " << errorCode.message();
    }

    ASSERT_TRUE(watcher.PollNow(assetDatabase, derivedArtifactCache));
    const ProjectChangeSnapshot snapshot = watcher.GetSnapshot();
    EXPECT_TRUE(snapshot.changes.empty());
    EXPECT_TRUE(derivedArtifactCache.markedSources.empty());
    std::filesystem::remove_all(outside);
}

TEST_F(ProjectChangeWatcherTest, MissingRoot_BaselinesAsEmptyAndLaterCreationIsObserved) {
    ProjectChangeWatcher watcher(root, 0.0, 16U);
    ASSERT_TRUE(watcher.PollNow(assetDatabase, derivedArtifactCache));
    ProjectChangeSnapshot snapshot = watcher.GetSnapshot();
    EXPECT_TRUE(snapshot.changes.empty());
    EXPECT_EQ(snapshot.successfulScanGeneration, 1U);

    WriteFixtureFile(root / "later.txt", "later");
    ASSERT_TRUE(watcher.PollNow(assetDatabase, derivedArtifactCache));
    snapshot = watcher.GetSnapshot();
    ASSERT_NE(FindChange(snapshot, "later.txt", ProjectFileChangeKind::Created), nullptr);
}

TEST_F(ProjectChangeWatcherTest, FailedScan_RetainsLastSuccessfulBaselineAndReportsDiagnostic) {
    const std::filesystem::path outside = root.parent_path() / "uve_project_change_watcher_tests_failure_outside";
    std::filesystem::remove_all(outside);
    WriteFixtureFile(root / "tracked.txt", "tracked");
    WriteFixtureFile(outside / "outside.txt", "outside");

    ProjectChangeWatcher watcher(root, 0.0, 16U);
    ASSERT_TRUE(watcher.PollNow(assetDatabase, derivedArtifactCache));
    const ProjectChangeSnapshot goodSnapshot = watcher.GetSnapshot();

    std::filesystem::remove_all(root);
    std::error_code errorCode;
    std::filesystem::create_directory_symlink(outside, root, errorCode);
    if (errorCode) {
        std::filesystem::remove_all(outside);
        GTEST_SKIP() << "Host does not permit root symlink creation: " << errorCode.message();
    }

    EXPECT_FALSE(watcher.PollNow(assetDatabase, derivedArtifactCache));
    const ProjectChangeSnapshot failedSnapshot = watcher.GetSnapshot();
    EXPECT_EQ(failedSnapshot.successfulScanGeneration, goodSnapshot.successfulScanGeneration);
    EXPECT_TRUE(failedSnapshot.changes.empty());
    ASSERT_TRUE(failedSnapshot.lastScanDiagnostic.has_value());
    EXPECT_FALSE(failedSnapshot.lastScanDiagnostic->empty());

    std::filesystem::remove(root, errorCode);
    std::filesystem::remove_all(outside);
}

TEST_F(ProjectChangeWatcherTest, RingBufferOverflow_PreservesNewestEntriesAndContinuesTargetedStaleMarking) {
    ProjectChangeWatcher watcher(root, 0.0, 1U);
    derivedArtifactCache.staleRecordsPerChange = 3U;
    ASSERT_TRUE(watcher.PollNow(assetDatabase, derivedArtifactCache));

    WriteFixtureFile(root / "first.txt", "first");
    ASSERT_TRUE(watcher.PollNow(assetDatabase, derivedArtifactCache));
    WriteFixtureFile(root / "second.txt", "second");
    ASSERT_TRUE(watcher.PollNow(assetDatabase, derivedArtifactCache));
    WriteFixtureFile(root / "first.txt", "first modified");
    ASSERT_TRUE(watcher.PollNow(assetDatabase, derivedArtifactCache));

    const ProjectChangeSnapshot snapshot = watcher.GetSnapshot();
    ASSERT_EQ(snapshot.changes.size(), 1U);
    EXPECT_EQ(snapshot.changes.front().relativePath.generic_string(), "first.txt");
    EXPECT_EQ(snapshot.changes.front().kind, ProjectFileChangeKind::Modified);
    EXPECT_EQ(snapshot.changes.front().staleArtifactCount, 3U);
    EXPECT_TRUE(snapshot.rescanRequired);
    ASSERT_EQ(derivedArtifactCache.markedSources.size(), 3U);
    EXPECT_EQ(derivedArtifactCache.markedSources.back(), (root / "first.txt").lexically_normal());
}

TEST_F(ProjectChangeWatcherTest, ModifiedSource_MarksMatchingDerivedRecordStaleAndReportsCount) {
    const std::filesystem::path contentRoot = root / "content";
    const std::filesystem::path cacheRoot = root / "DerivedData" / "Import";
    const std::filesystem::path source = contentRoot / "source.custom";
    const std::filesystem::path destination = contentRoot / "output.custom";
    WriteFixtureFile(source, "before");
    WriteFixtureFile(destination, "output");

    const std::optional<AssetContentFingerprint> sourceFingerprint = ComputeAssetContentFingerprint(source);
    const std::optional<AssetContentFingerprint> destinationFingerprint =
        ComputeAssetContentFingerprint(destination);
    ASSERT_TRUE(sourceFingerprint.has_value());
    ASSERT_TRUE(destinationFingerprint.has_value());

    DerivedArtifactCache cache(cacheRoot);
    const DerivedArtifactCacheRecord record{kDerivedArtifactCacheSchemaVersion,
                                                source,
                                                destination,
                                                *sourceFingerprint,
                                                *destinationFingerprint,
                                                "generic-v1",
                                                AssetGuid{77U}};
    ASSERT_TRUE(cache.StoreImportRecord(destination, record));

    ProjectChangeWatcher watcher(contentRoot, 0.0, 16U);
    ASSERT_TRUE(watcher.PollNow(assetDatabase, cache));
    WriteFixtureFile(source, "after");
    ASSERT_TRUE(watcher.PollNow(assetDatabase, cache));

    const ProjectChangeSnapshot snapshot = watcher.GetSnapshot();
    const ProjectFileChange* const change =
        FindChange(snapshot, "source.custom", ProjectFileChangeKind::Modified);
    ASSERT_NE(change, nullptr);
    EXPECT_EQ(change->staleArtifactCount, 1U);
    const std::optional<DerivedArtifactCacheRecord> staleRecord = cache.LoadImportRecord(destination);
    ASSERT_TRUE(staleRecord.has_value());
    EXPECT_TRUE(staleRecord->stale);
}

TEST_F(ProjectChangeWatcherTest, GetSnapshot_ReturnsCopyIsolationAndPollNowBypassesInterval) {
    ProjectChangeWatcher watcher(root, 3600.0, 16U);
    ASSERT_TRUE(watcher.PollNow(assetDatabase, derivedArtifactCache));
    WriteFixtureFile(root / "explicit.txt", "explicit");

    ASSERT_TRUE(watcher.PollNow(assetDatabase, derivedArtifactCache));
    ProjectChangeSnapshot callerSnapshot = watcher.GetSnapshot();
    ASSERT_EQ(callerSnapshot.changes.size(), 1U);
    callerSnapshot.changes.front().relativePath = "caller-mutated.txt";
    callerSnapshot.changes.clear();

    const ProjectChangeSnapshot retainedSnapshot = watcher.GetSnapshot();
    ASSERT_EQ(retainedSnapshot.changes.size(), 1U);
    EXPECT_EQ(retainedSnapshot.changes.front().relativePath.generic_string(), "explicit.txt");
}

TEST_F(ProjectChangeWatcherTest, Acknowledgements_PreserveOverflowBoundaryUntilExplicitRescanAcknowledgement) {
    ProjectChangeWatcher watcher(root, 0.0, 1U);
    ASSERT_TRUE(watcher.PollNow(assetDatabase, derivedArtifactCache));
    WriteFixtureFile(root / "first.txt", "first");
    ASSERT_TRUE(watcher.PollNow(assetDatabase, derivedArtifactCache));
    WriteFixtureFile(root / "second.txt", "second");
    ASSERT_TRUE(watcher.PollNow(assetDatabase, derivedArtifactCache));

    const ProjectChangeSnapshot overflowSnapshot = watcher.GetSnapshot();
    ASSERT_TRUE(overflowSnapshot.rescanRequired);
    watcher.AcknowledgeThrough(overflowSnapshot.latestSequence);
    const ProjectChangeSnapshot acknowledgedSnapshot = watcher.GetSnapshot();
    EXPECT_TRUE(acknowledgedSnapshot.changes.empty());
    EXPECT_TRUE(acknowledgedSnapshot.rescanRequired);

    watcher.AcknowledgeRescan();
    const ProjectChangeSnapshot rescanAcknowledgedSnapshot = watcher.GetSnapshot();
    EXPECT_TRUE(rescanAcknowledgedSnapshot.changes.empty());
    EXPECT_FALSE(rescanAcknowledgedSnapshot.rescanRequired);
}

} // namespace
} // namespace UVE::Asset::Tests
