// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "asset_import_queue.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "asset_content_fingerprint.h"
#include "asset_database.h"
#include "asset_importer.h"
#include "data_table_asset.h"
#include "data_table_importer.h"
#include "derived_artifact_cache.h"

namespace UVE::Asset::Tests {
namespace {

void WriteFixtureFile(const std::filesystem::path& path, const std::string_view contents) {
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream file(path, std::ios::binary);
    ASSERT_TRUE(file.is_open());
    file << contents;
    ASSERT_TRUE(file.good());
}

[[nodiscard]] AssetImportRequest MakeRequest(const std::filesystem::path& source,
                                                    const std::filesystem::path& destination) {
    AssetImportRequest request;
    request.sourcePath = source;
    request.destinationPath = destination;
    request.settings = std::make_shared<AssetImportSettings>();
    return request;
}

class ThrowingAssetImporter final : public IAssetImporter {
public:
    void RegisterImporter(
        std::string,
        std::function<bool(const std::filesystem::path&, const std::filesystem::path&,
                           const AssetImportSettings&)>) override {}

    [[nodiscard]] AssetGuid Import(const std::filesystem::path&, const std::filesystem::path&,
                                         IAssetDatabase&, const AssetImportSettings&) override {
        throw std::runtime_error("importer boundary failure");
    }
};

class ThrowingDerivedArtifactCache final : public IDerivedArtifactCache {
public:
    explicit ThrowingDerivedArtifactCache(const bool throwOnLoad, const bool throwOnStore)
        : m_throwOnLoad(throwOnLoad), m_throwOnStore(throwOnStore) {}

    [[nodiscard]] std::optional<DerivedArtifactCacheRecord> LoadImportRecord(
        const std::filesystem::path&) const override {
        if (m_throwOnLoad) {
            throw std::runtime_error("cache load boundary failure");
        }
        return std::nullopt;
    }

    [[nodiscard]] bool StoreImportRecord(const std::filesystem::path&,
                                            const DerivedArtifactCacheRecord&) override {
        if (m_throwOnStore) {
            throw std::runtime_error("cache store boundary failure");
        }
        return false;
    }

    [[nodiscard]] std::size_t MarkStaleForSource(const std::filesystem::path&) override { return 0U; }

    [[nodiscard]] std::filesystem::path GetCacheRoot() const override { return "throwing-cache"; }

private:
    bool m_throwOnLoad;
    bool m_throwOnStore;
};

class AssetImportQueueTest : public ::testing::Test {
protected:
    const std::filesystem::path root = "uve_asset_import_queue_tests";
    const std::filesystem::path contentRoot = root / "content";
    const std::filesystem::path cacheRoot = root / "DerivedData" / "Import";
    AssetDatabase assetDatabase;
    AssetImporter importer;
    DerivedArtifactCache cache{cacheRoot};
    AssetImportQueue queue{importer, assetDatabase, cache};

    void SetUp() override { std::filesystem::remove_all(root); }
    void TearDown() override { std::filesystem::remove_all(root); }
};

TEST_F(AssetImportQueueTest, Tick_TranslatesThrowingImporterIntoRetryableFailure) {
    const std::filesystem::path source = root / "source.throwing";
    const std::filesystem::path destination = contentRoot / "output.throwing";
    WriteFixtureFile(source, "source bytes");

    ThrowingAssetImporter throwingImporter;
    AssetImportQueue throwingQueue{throwingImporter, assetDatabase, cache};
    const std::optional<AssetImportJobId> id =
        throwingQueue.Enqueue(MakeRequest(source, destination));
    ASSERT_TRUE(id.has_value());

    EXPECT_TRUE(throwingQueue.Tick());
    const std::vector<AssetImportJob> jobs = throwingQueue.GetJobs();
    ASSERT_EQ(jobs.size(), 1U);
    EXPECT_EQ(jobs.front().state, AssetImportJobState::Failed);
    EXPECT_EQ(jobs.front().attemptCount, 1U);
    ASSERT_EQ(jobs.front().diagnostics.size(), 1U);
    EXPECT_EQ(jobs.front().diagnostics.front().code, AssetImportDiagnosticCode::ImporterFailed);
    EXPECT_NE(jobs.front().diagnostics.front().message.find("importer boundary failure"), std::string::npos);
    EXPECT_TRUE(throwingQueue.Retry(*id));
}

TEST_F(AssetImportQueueTest, Tick_TranslatesThrowingCacheLoadIntoRetryableFailure) {
    const std::filesystem::path source = root / "source.txt";
    const std::filesystem::path destination = contentRoot / "output.txt";
    WriteFixtureFile(source, "source bytes");

    ThrowingDerivedArtifactCache throwingCache(true, false);
    AssetImportQueue throwingQueue{importer, assetDatabase, throwingCache};
    const std::optional<AssetImportJobId> id =
        throwingQueue.Enqueue(MakeRequest(source, destination));
    ASSERT_TRUE(id.has_value());

    EXPECT_TRUE(throwingQueue.Tick());
    const std::vector<AssetImportJob> jobs = throwingQueue.GetJobs();
    ASSERT_EQ(jobs.size(), 1U);
    EXPECT_EQ(jobs.front().state, AssetImportJobState::Failed);
    ASSERT_EQ(jobs.front().diagnostics.size(), 1U);
    EXPECT_EQ(jobs.front().diagnostics.front().code, AssetImportDiagnosticCode::CacheReadFailed);
    EXPECT_NE(jobs.front().diagnostics.front().message.find("cache load boundary failure"), std::string::npos);
    EXPECT_TRUE(throwingQueue.Retry(*id));
}

TEST_F(AssetImportQueueTest, Tick_TranslatesThrowingCacheStoreIntoExistingWarning) {
    const std::filesystem::path source = root / "source.custom";
    const std::filesystem::path destination = contentRoot / "output.custom";
    WriteFixtureFile(source, "source bytes");
    std::filesystem::create_directories(contentRoot);

    importer.RegisterImporter("custom", [](const std::filesystem::path& sourcePath,
                                               const std::filesystem::path& destinationPath,
                                               const AssetImportSettings&) {
        std::ifstream input(sourcePath, std::ios::binary);
        std::ofstream output(destinationPath, std::ios::binary);
        output << input.rdbuf();
        return input.good() || input.eof();
    });
    ThrowingDerivedArtifactCache throwingCache(false, true);
    AssetImportQueue throwingQueue{importer, assetDatabase, throwingCache};
    ASSERT_TRUE(throwingQueue.Enqueue(MakeRequest(source, destination)).has_value());

    EXPECT_TRUE(throwingQueue.Tick());
    const std::vector<AssetImportJob> jobs = throwingQueue.GetJobs();
    ASSERT_EQ(jobs.size(), 1U);
    EXPECT_EQ(jobs.front().state, AssetImportJobState::Succeeded);
    ASSERT_EQ(jobs.front().diagnostics.size(), 1U);
    EXPECT_EQ(jobs.front().diagnostics.front().severity, AssetImportDiagnosticSeverity::Warning);
    EXPECT_EQ(jobs.front().diagnostics.front().code, AssetImportDiagnosticCode::CacheWriteFailed);
    EXPECT_NE(jobs.front().diagnostics.front().message.find("cache store boundary failure"), std::string::npos);
}

TEST_F(AssetImportQueueTest, EnqueueAndTick_ProcessExactlyOneFifoJobPerTick) {
    const std::filesystem::path firstSource = root / "source_first.txt";
    const std::filesystem::path secondSource = root / "source_second.txt";
    const std::filesystem::path firstDestination = contentRoot / "first.txt";
    const std::filesystem::path secondDestination = contentRoot / "second.txt";
    WriteFixtureFile(firstSource, "first bytes");
    WriteFixtureFile(secondSource, "second bytes");
    std::filesystem::create_directories(contentRoot);

    const std::optional<AssetImportJobId> firstId = queue.Enqueue(MakeRequest(firstSource, firstDestination));
    const std::optional<AssetImportJobId> secondId = queue.Enqueue(MakeRequest(secondSource, secondDestination));
    ASSERT_TRUE(firstId.has_value());
    ASSERT_TRUE(secondId.has_value());
    EXPECT_LT(firstId->value, secondId->value);

    ASSERT_TRUE(queue.Tick());
    std::vector<AssetImportJob> jobs = queue.GetJobs();
    ASSERT_EQ(jobs.size(), 2U);
    EXPECT_EQ(jobs[0].state, AssetImportJobState::Succeeded);
    EXPECT_EQ(jobs[0].attemptCount, 1U);
    EXPECT_EQ(jobs[1].state, AssetImportJobState::Queued);
    EXPECT_FALSE(std::filesystem::exists(secondDestination));

    ASSERT_TRUE(queue.Tick());
    jobs = queue.GetJobs();
    EXPECT_EQ(jobs[1].state, AssetImportJobState::Succeeded);
    EXPECT_EQ(jobs[1].attemptCount, 1U);
    EXPECT_TRUE(std::filesystem::exists(secondDestination));
    EXPECT_FALSE(queue.Tick());
}

TEST_F(AssetImportQueueTest, Retry_ReturnsFailedJobToTailWithoutJumpingQueuedWork) {
    const std::filesystem::path failingSource = root / "failing.unsupported";
    const std::filesystem::path succeedingSource = root / "succeeding.txt";
    const std::filesystem::path failingDestination = contentRoot / "failing.unsupported";
    const std::filesystem::path succeedingDestination = contentRoot / "succeeding.txt";
    WriteFixtureFile(failingSource, "unsupported bytes");
    WriteFixtureFile(succeedingSource, "good bytes");
    std::filesystem::create_directories(contentRoot);

    const std::optional<AssetImportJobId> failingId =
        queue.Enqueue(MakeRequest(failingSource, failingDestination));
    const std::optional<AssetImportJobId> succeedingId =
        queue.Enqueue(MakeRequest(succeedingSource, succeedingDestination));
    ASSERT_TRUE(failingId.has_value());
    ASSERT_TRUE(succeedingId.has_value());

    ASSERT_TRUE(queue.Tick());
    std::vector<AssetImportJob> jobs = queue.GetJobs();
    ASSERT_EQ(jobs[0].state, AssetImportJobState::Failed);
    EXPECT_EQ(jobs[0].attemptCount, 1U);
    ASSERT_TRUE(queue.Retry(*failingId));

    ASSERT_TRUE(queue.Tick());
    jobs = queue.GetJobs();
    EXPECT_EQ(jobs[1].id, *succeedingId);
    EXPECT_EQ(jobs[1].state, AssetImportJobState::Succeeded);
    EXPECT_EQ(jobs[0].state, AssetImportJobState::Queued);
    EXPECT_EQ(jobs[0].attemptCount, 1U);

    ASSERT_TRUE(queue.Tick());
    jobs = queue.GetJobs();
    EXPECT_EQ(jobs[0].id, *failingId);
    EXPECT_EQ(jobs[0].state, AssetImportJobState::Failed);
    EXPECT_EQ(jobs[0].attemptCount, 2U);
}

TEST_F(AssetImportQueueTest, Cache_HitsForMatchingBytesAndInvalidatesOutOfBandDestinationDrift) {
    const std::filesystem::path source = root / "source.custom";
    const std::filesystem::path destination = contentRoot / "output.custom";
    WriteFixtureFile(source, "source bytes");
    std::filesystem::create_directories(contentRoot);

    int importerCallCount = 0;
    importer.RegisterImporter("custom", [&importerCallCount](const std::filesystem::path& sourcePath,
                                                                   const std::filesystem::path& destinationPath,
                                                                   const AssetImportSettings&) {
        ++importerCallCount;
        std::ifstream input(sourcePath, std::ios::binary);
        std::ofstream output(destinationPath, std::ios::binary);
        output << input.rdbuf();
        return input.good() || input.eof();
    });

    const std::optional<AssetImportJobId> firstId = queue.Enqueue(MakeRequest(source, destination));
    ASSERT_TRUE(firstId.has_value());
    ASSERT_TRUE(queue.Tick());
    EXPECT_EQ(importerCallCount, 1);
    std::vector<AssetImportJob> jobs = queue.GetJobs();
    ASSERT_TRUE(jobs[0].resultGuid.has_value());
    EXPECT_FALSE(jobs[0].cacheHit);

    const std::optional<AssetImportJobId> secondId = queue.Enqueue(MakeRequest(source, destination));
    ASSERT_TRUE(secondId.has_value());
    ASSERT_TRUE(queue.Tick());
    EXPECT_EQ(importerCallCount, 1);
    jobs = queue.GetJobs();
    EXPECT_EQ(jobs[1].state, AssetImportJobState::Succeeded);
    EXPECT_TRUE(jobs[1].cacheHit);
    EXPECT_EQ(jobs[1].resultGuid, jobs[0].resultGuid);

    WriteFixtureFile(destination, "out-of-band destination drift");
    const std::optional<AssetImportJobId> thirdId = queue.Enqueue(MakeRequest(source, destination));
    ASSERT_TRUE(thirdId.has_value());
    ASSERT_TRUE(queue.Tick());
    EXPECT_EQ(importerCallCount, 2);
    jobs = queue.GetJobs();
    EXPECT_EQ(jobs[2].state, AssetImportJobState::Succeeded);
    EXPECT_FALSE(jobs[2].cacheHit);
    EXPECT_EQ(*ComputeAssetContentFingerprint(destination), *ComputeAssetContentFingerprint(source));
}

TEST_F(AssetImportQueueTest, DataTableSettingsFingerprintForcesCacheMissWhenSchemaChanges) {
    const std::filesystem::path source = root / "weapons.csv";
    const std::filesystem::path destination = contentRoot / "weapons.uvtable";
    WriteFixtureFile(source, "id,damage\npistol,25\n");
    std::filesystem::create_directories(contentRoot);

    importer.RegisterImporter(".csv", [](const std::filesystem::path& sourcePath,
                                             const std::filesystem::path& destinationPath,
                                             const AssetImportSettings& settings) {
        const auto* const dataTableSettings = dynamic_cast<const DataTableImportSettings*>(&settings);
        if (dataTableSettings == nullptr) {
            return false;
        }
        std::ifstream input(sourcePath, std::ios::binary);
        std::string document((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (!input.good() && !input.eof()) {
            return false;
        }
        DataTable table(dataTableSettings->tableName);
        for (const DataTableColumn& column : dataTableSettings->columns) {
            if (!table.DefineColumn(column.name, column.type)) {
                return false;
            }
        }
        return table.ImportCsv(document) && SaveDataTableAsset(table, destinationPath);
    });

    auto integerSettings = std::make_shared<DataTableImportSettings>();
    integerSettings->tableName = "weapons";
    integerSettings->columns = {DataTableColumn{"damage", DataTableColumnType::Integer}};
    auto numberSettings = std::make_shared<DataTableImportSettings>(*integerSettings);
    numberSettings->columns.front().type = DataTableColumnType::Number;
    EXPECT_NE(integerSettings->GetCacheVersion(), numberSettings->GetCacheVersion());

    AssetImportRequest firstRequest;
    firstRequest.sourcePath = source;
    firstRequest.destinationPath = destination;
    firstRequest.settings = integerSettings;
    ASSERT_TRUE(queue.Enqueue(std::move(firstRequest)).has_value());
    ASSERT_TRUE(queue.Tick());

    AssetImportRequest secondRequest;
    secondRequest.sourcePath = source;
    secondRequest.destinationPath = destination;
    secondRequest.settings = numberSettings;
    ASSERT_TRUE(queue.Enqueue(std::move(secondRequest)).has_value());
    ASSERT_TRUE(queue.Tick());

    const std::vector<AssetImportJob> jobs = queue.GetJobs();
    ASSERT_EQ(jobs.size(), 2U);
    EXPECT_FALSE(jobs[0].cacheHit);
    EXPECT_FALSE(jobs[1].cacheHit);
    ASSERT_TRUE(jobs[0].request.settingsVersion != jobs[1].request.settingsVersion);
    DataTable imported;
    ASSERT_TRUE(LoadDataTableAsset(destination, imported));
    ASSERT_EQ(imported.GetSnapshot().columns.size(), 1U);
    EXPECT_EQ(imported.GetSnapshot().columns.front().type, DataTableColumnType::Number);
}

TEST_F(AssetImportQueueTest, SnapshotAndCacheContracts_RejectInvalidRequestAndReturnCopies) {
    AssetImportRequest invalidRequest;
    EXPECT_FALSE(queue.Enqueue(std::move(invalidRequest)).has_value());

    const std::filesystem::path source = root / "source.txt";
    const std::filesystem::path destination = contentRoot / "output.txt";
    WriteFixtureFile(source, "source bytes");
    std::filesystem::create_directories(contentRoot);
    ASSERT_TRUE(queue.Enqueue(MakeRequest(source, destination)).has_value());

    std::vector<AssetImportJob> firstSnapshot = queue.GetJobs();
    ASSERT_EQ(firstSnapshot.size(), 1U);
    firstSnapshot[0].state = AssetImportJobState::Succeeded;
    firstSnapshot[0].request.sourcePath = "caller-mutated";
    const std::vector<AssetImportJob> secondSnapshot = queue.GetJobs();
    EXPECT_EQ(secondSnapshot[0].state, AssetImportJobState::Queued);
    EXPECT_NE(secondSnapshot[0].request.sourcePath, std::filesystem::path("caller-mutated"));

    const DerivedArtifactCacheRecord invalidRecord{kDerivedArtifactCacheSchemaVersion,
                                                       source,
                                                       root / "different-destination.txt",
                                                       {},
                                                       {},
                                                       "generic-v1",
                                                       AssetGuid{1U}};
    EXPECT_FALSE(cache.StoreImportRecord(destination, invalidRecord));
    EXPECT_FALSE(std::filesystem::exists(cacheRoot));
}

TEST(AssetContentFingerprintTest, Compute_IsTimestampIndependentAndByteSensitive) {
    const std::filesystem::path root = "uve_asset_content_fingerprint_tests";
    const std::filesystem::path first = root / "first.bin";
    const std::filesystem::path second = root / "second.bin";
    std::filesystem::remove_all(root);
    WriteFixtureFile(first, "same bytes");
    WriteFixtureFile(second, "same bytes");

    const std::optional<AssetContentFingerprint> firstFingerprint = ComputeAssetContentFingerprint(first);
    const std::optional<AssetContentFingerprint> secondFingerprint = ComputeAssetContentFingerprint(second);
    ASSERT_TRUE(firstFingerprint.has_value());
    ASSERT_TRUE(secondFingerprint.has_value());
    EXPECT_EQ(*firstFingerprint, *secondFingerprint);

    WriteFixtureFile(second, "different bytes");
    const std::optional<AssetContentFingerprint> changedFingerprint = ComputeAssetContentFingerprint(second);
    ASSERT_TRUE(changedFingerprint.has_value());
    EXPECT_NE(*firstFingerprint, *changedFingerprint);

    std::filesystem::remove_all(root);
}

// Increment 61 stale-metadata regressions remain intentionally beside queue cache-hit coverage:
// the cache owns persistence and the queue owns acceptance of a persisted record.
TEST_F(AssetImportQueueTest, DerivedArtifactCache_MarkStaleForSource_IsTargetedAndIdempotent) {
    const std::filesystem::path firstSource = root / "first-source.custom";
    const std::filesystem::path secondSource = root / "second-source.custom";
    const std::filesystem::path firstDestination = contentRoot / "first-output.custom";
    const std::filesystem::path secondDestination = contentRoot / "second-output.custom";
    WriteFixtureFile(firstSource, "first source");
    WriteFixtureFile(secondSource, "second source");
    WriteFixtureFile(firstDestination, "first destination");
    WriteFixtureFile(secondDestination, "second destination");

    const std::optional<AssetContentFingerprint> firstSourceFingerprint =
        ComputeAssetContentFingerprint(firstSource);
    const std::optional<AssetContentFingerprint> secondSourceFingerprint =
        ComputeAssetContentFingerprint(secondSource);
    const std::optional<AssetContentFingerprint> firstDestinationFingerprint =
        ComputeAssetContentFingerprint(firstDestination);
    const std::optional<AssetContentFingerprint> secondDestinationFingerprint =
        ComputeAssetContentFingerprint(secondDestination);
    ASSERT_TRUE(firstSourceFingerprint.has_value());
    ASSERT_TRUE(secondSourceFingerprint.has_value());
    ASSERT_TRUE(firstDestinationFingerprint.has_value());
    ASSERT_TRUE(secondDestinationFingerprint.has_value());

    const DerivedArtifactCacheRecord firstRecord{kDerivedArtifactCacheSchemaVersion,
                                                     firstSource,
                                                     firstDestination,
                                                     *firstSourceFingerprint,
                                                     *firstDestinationFingerprint,
                                                     "generic-v1",
                                                     AssetGuid{1U}};
    const DerivedArtifactCacheRecord secondRecord{kDerivedArtifactCacheSchemaVersion,
                                                      secondSource,
                                                      secondDestination,
                                                      *secondSourceFingerprint,
                                                      *secondDestinationFingerprint,
                                                      "generic-v1",
                                                      AssetGuid{2U}};
    ASSERT_TRUE(cache.StoreImportRecord(firstDestination, firstRecord));
    ASSERT_TRUE(cache.StoreImportRecord(secondDestination, secondRecord));

    EXPECT_EQ(cache.MarkStaleForSource(firstSource), 1U);
    const std::optional<DerivedArtifactCacheRecord> staleFirst = cache.LoadImportRecord(firstDestination);
    const std::optional<DerivedArtifactCacheRecord> freshSecond = cache.LoadImportRecord(secondDestination);
    ASSERT_TRUE(staleFirst.has_value());
    ASSERT_TRUE(freshSecond.has_value());
    EXPECT_TRUE(staleFirst->stale);
    EXPECT_FALSE(freshSecond->stale);
    EXPECT_EQ(cache.MarkStaleForSource(root / "unrelated-source.custom"), 0U);
    EXPECT_EQ(cache.MarkStaleForSource(firstSource), 0U);
    EXPECT_FALSE(cache.LoadImportRecord(secondDestination)->stale);
}

TEST_F(AssetImportQueueTest, Cache_StoreRejectsCallerStaleRecordAndRetainsFreshArtifact) {
    const std::filesystem::path source = root / "source.custom";
    const std::filesystem::path destination = contentRoot / "output.custom";
    WriteFixtureFile(source, "source bytes");
    WriteFixtureFile(destination, "output bytes");

    const std::optional<AssetContentFingerprint> sourceFingerprint =
        ComputeAssetContentFingerprint(source);
    const std::optional<AssetContentFingerprint> destinationFingerprint =
        ComputeAssetContentFingerprint(destination);
    ASSERT_TRUE(sourceFingerprint.has_value());
    ASSERT_TRUE(destinationFingerprint.has_value());

    const DerivedArtifactCacheRecord freshRecord{kDerivedArtifactCacheSchemaVersion,
                                                     source,
                                                     destination,
                                                     *sourceFingerprint,
                                                     *destinationFingerprint,
                                                     "generic-v1",
                                                     AssetGuid{1U},
                                                     false};
    ASSERT_TRUE(cache.StoreImportRecord(destination, freshRecord));

    DerivedArtifactCacheRecord staleCallerRecord = freshRecord;
    staleCallerRecord.stale = true;
    EXPECT_FALSE(cache.StoreImportRecord(destination, staleCallerRecord));

    const std::optional<DerivedArtifactCacheRecord> retainedRecord = cache.LoadImportRecord(destination);
    ASSERT_TRUE(retainedRecord.has_value());
    EXPECT_FALSE(retainedRecord->stale);
    EXPECT_EQ(retainedRecord->sourceFingerprint, freshRecord.sourceFingerprint);
    EXPECT_EQ(retainedRecord->destinationFingerprint, freshRecord.destinationFingerprint);
}

TEST_F(AssetImportQueueTest, Cache_StaleRecordForcesFreshImportAndSuccessfulWriteClearsStaleState) {
    const std::filesystem::path source = root / "source.custom";
    const std::filesystem::path destination = contentRoot / "output.custom";
    WriteFixtureFile(source, "source bytes");
    std::filesystem::create_directories(contentRoot);

    int importerCallCount = 0;
    importer.RegisterImporter("custom", [&importerCallCount](const std::filesystem::path& sourcePath,
                                                                   const std::filesystem::path& destinationPath,
                                                                   const AssetImportSettings&) {
        ++importerCallCount;
        std::ifstream input(sourcePath, std::ios::binary);
        std::ofstream output(destinationPath, std::ios::binary | std::ios::trunc);
        output << input.rdbuf();
        return input.good() || input.eof();
    });

    ASSERT_TRUE(queue.Enqueue(MakeRequest(source, destination)).has_value());
    ASSERT_TRUE(queue.Tick());
    EXPECT_EQ(importerCallCount, 1);
    ASSERT_EQ(cache.MarkStaleForSource(source), 1U);
    const std::optional<DerivedArtifactCacheRecord> staleRecord = cache.LoadImportRecord(destination);
    ASSERT_TRUE(staleRecord.has_value());
    EXPECT_TRUE(staleRecord->stale);

    ASSERT_TRUE(queue.Enqueue(MakeRequest(source, destination)).has_value());
    ASSERT_TRUE(queue.Tick());
    EXPECT_EQ(importerCallCount, 2);
    const std::vector<AssetImportJob> jobsAfterFreshImport = queue.GetJobs();
    ASSERT_EQ(jobsAfterFreshImport.size(), 2U);
    EXPECT_FALSE(jobsAfterFreshImport[1].cacheHit);
    const std::optional<DerivedArtifactCacheRecord> refreshedRecord = cache.LoadImportRecord(destination);
    ASSERT_TRUE(refreshedRecord.has_value());
    EXPECT_FALSE(refreshedRecord->stale);

    ASSERT_TRUE(queue.Enqueue(MakeRequest(source, destination)).has_value());
    ASSERT_TRUE(queue.Tick());
    EXPECT_EQ(importerCallCount, 2);
    const std::vector<AssetImportJob> finalJobs = queue.GetJobs();
    ASSERT_EQ(finalJobs.size(), 3U);
    EXPECT_TRUE(finalJobs[2].cacheHit);
}

} // namespace
} // namespace UVE::Asset::Tests
