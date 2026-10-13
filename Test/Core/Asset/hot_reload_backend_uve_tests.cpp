// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/asset/hot_reload_uve.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <gtest/gtest.h>
#include "Support/test_scratch_uve.h"
#include "uve/asset/asset_database_uve.h"
#include "uve/asset/asset_handle_uve.h"
#include "uve/asset/asset_manager_uve.h"
#include "uve/asset/asset_reloaded_event_uve.h"
#include "uve/asset/blob_asset_uve.h"
#include "uve/asset/uve_file_envelope_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/threading/job_counter_uve.h"
#include "uve/threading/thread_pool_uve.h"
#include "uve/utilities/scope_guard_uve.h"
namespace {
using namespace UVE;
using namespace UVE::Asset;
bool ReadBlobUVE(const std::filesystem::path &path, BlobAssetUVE &value) {
    auto result = ReadUveFileUVE(path);
    if (!result)
        return false;
    value = std::move(result->second);
    return true;
}
bool WriteBlobUVE(const std::filesystem::path &path, std::byte value) {
    return WriteUveFileUVE(path, AssetKindUVE::Blob, std::vector<std::byte>{value});
}
class HotReloadBackendUVETest : public ::testing::Test {
  protected:
    Threading::ThreadPoolUVE pool{1U};
    Events::EventSystemUVE events;
    AssetDatabaseUVE database;
    std::unique_ptr<HotReloadUVE> reload;
    std::unique_ptr<AssetManagerUVE> manager;
    std::vector<AssetHandleUVE<BlobAssetUVE>> handles;
    std::vector<AssetGuidUVE> reloaded;
    std::filesystem::path root;
    void SetUp() override {
        root = UVE::Tests::MakeTestCaseDirectoryUVE();
        events.Subscribe<AssetReloadedEventUVE>(
            [this](const auto &event) { reloaded.push_back(event.guid); });
    }
    void ConfigureUVE(HotReloadPreferenceUVE preference) {
        reload = std::make_unique<HotReloadUVE>(events, 1.0, preference);
        manager = std::make_unique<AssetManagerUVE>(pool, events, reload.get());
        manager->RegisterLoaderUVE<BlobAssetUVE>(ReadBlobUVE);
    }
    bool WaitReadyUVE(const AssetHandleUVE<BlobAssetUVE> &handle) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        while (!handle.IsReadyUVE() && !handle.HasFailedUVE() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        if (!handle.IsReadyUVE())
            return false;
        // Ready precedes Track in the worker. Wait for the completion callback too.
        Threading::JobCounterUVE barrier;
        pool.SubmitUVE([] {}, barrier);
        barrier.WaitUVE();
        return true;
    }
    std::optional<AssetGuidUVE> LoadUVE(const std::filesystem::path &path) {
        if (!WriteBlobUVE(path, std::byte{1}))
            return std::nullopt;
        const auto guid = database.RegisterUVE(path);
        handles.push_back(manager->LoadUVE<BlobAssetUVE>(guid, database));
        if (!WaitReadyUVE(handles.back()))
            return std::nullopt;
        return guid;
    }
    void PollUVE(double delta = 1.0) {
        reload->PollUVE(*manager, database, delta);
        events.DispatchQueuedUVE();
    }
    void BumpUVE(const std::filesystem::path &path) {
        std::filesystem::last_write_time(path,
                                         std::filesystem::last_write_time(path) + std::chrono::hours{1});
    }
};
TEST_F(HotReloadBackendUVETest, ForcedPollingDebouncesAndPublishesRealReloadedData) {
    ConfigureUVE(HotReloadPreferenceUVE::Polling);
    EXPECT_EQ(reload->GetBackendUVE(), HotReloadBackendUVE::Polling);
    const auto path = root / "asset.uvblob";
    const auto guid = LoadUVE(path);
    ASSERT_TRUE(guid);
    PollUVE();
    EXPECT_TRUE(reloaded.empty());
    ASSERT_TRUE(WriteBlobUVE(path, std::byte{2}));
    BumpUVE(path);
    PollUVE(0.25);
    ASSERT_TRUE(WriteBlobUVE(path, std::byte{3}));
    BumpUVE(path);
    PollUVE(0.25);
    EXPECT_TRUE(reloaded.empty());
    PollUVE(0.5);
    EXPECT_EQ(reloaded, (std::vector<AssetGuidUVE>{*guid}));
    ASSERT_TRUE(WaitReadyUVE(handles.front()));
    EXPECT_EQ(handles.front().TryGetUVE()->front(), std::byte{3});
    PollUVE();
    EXPECT_EQ(reloaded.size(), 1U);
}
TEST_F(HotReloadBackendUVETest, DeleteThenRecreateWithIdenticalMetadataReloadsWithoutDestroyingLastGoodData) {
    ConfigureUVE(HotReloadPreferenceUVE::Polling);
    const auto path = root / "asset.uvblob";
    const auto guid = LoadUVE(path);
    ASSERT_TRUE(guid);
    PollUVE();
    const auto stamp = std::filesystem::last_write_time(path);
    ASSERT_TRUE(std::filesystem::remove(path));
    PollUVE();
    EXPECT_TRUE(reloaded.empty());
    EXPECT_EQ(handles.front().TryGetUVE()->front(), std::byte{1});
    ASSERT_TRUE(WriteBlobUVE(path, std::byte{2}));
    std::filesystem::last_write_time(path, stamp);
    PollUVE();
    EXPECT_EQ(reloaded, (std::vector<AssetGuidUVE>{*guid}));
    ASSERT_TRUE(WaitReadyUVE(handles.front()));
    EXPECT_EQ(handles.front().TryGetUVE()->front(), std::byte{2});
}
TEST_F(HotReloadBackendUVETest, RepeatedTrackDoesNotEraseObservedChanges) {
    ConfigureUVE(HotReloadPreferenceUVE::Polling);
    const auto path = root / "asset.uvblob";
    const auto guid = LoadUVE(path);
    ASSERT_TRUE(guid);
    PollUVE();
    ASSERT_TRUE(WriteBlobUVE(path, std::byte{2}));
    BumpUVE(path);
    reload->TrackUVE(*guid);
    PollUVE();
    EXPECT_EQ(reloaded, (std::vector<AssetGuidUVE>{*guid}));
}
TEST_F(HotReloadBackendUVETest, DatabaseRelocationPreservesGuidAndNoticesRename) {
    ConfigureUVE(HotReloadPreferenceUVE::Automatic);
    const auto path = root / "before.uvblob", after = root / "after.uvblob";
    const auto guid = LoadUVE(path);
    ASSERT_TRUE(guid);
    PollUVE();
    std::filesystem::rename(path, after);
    ASSERT_TRUE(database.RelocateUVE(*guid, after));
    PollUVE();
    EXPECT_EQ(reloaded, (std::vector<AssetGuidUVE>{*guid}));
    EXPECT_EQ(database.ResolveUVE(*guid), after);
}
TEST_F(HotReloadBackendUVETest, RequestsAreSortedAndUntrackedFilesDoNotTriggerFurtherReloads) {
    ConfigureUVE(HotReloadPreferenceUVE::Automatic);
    const auto aPath = root / "a.uvblob", bPath = root / "b.uvblob";
    const auto a = LoadUVE(aPath), b = LoadUVE(bPath);
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    PollUVE();
    ASSERT_TRUE(WriteBlobUVE(bPath, std::byte{2}));
    BumpUVE(bPath);
    ASSERT_TRUE(WriteBlobUVE(aPath, std::byte{2}));
    BumpUVE(aPath);
    PollUVE();
    std::vector<AssetGuidUVE> expected{*a, *b};
    std::sort(expected.begin(), expected.end(),
              [](auto left, auto right) { return left.value < right.value; });
    EXPECT_EQ(reloaded, expected);
    for (const auto &handle : handles)
        ASSERT_TRUE(WaitReadyUVE(handle));
    reload->UntrackUVE(*a);
    ASSERT_TRUE(WriteBlobUVE(aPath, std::byte{3}));
    BumpUVE(aPath);
    PollUVE();
    EXPECT_EQ(reloaded, expected);
}
TEST_F(HotReloadBackendUVETest, NativeSameSizeSameTimestampWritesAreDetectedAndCoalesced) {
    ConfigureUVE(HotReloadPreferenceUVE::Automatic);
    if (reload->GetBackendUVE() != HotReloadBackendUVE::Inotify)
        GTEST_SKIP() << "Native inotify unavailable; polling is covered separately";
    const auto path = root / "asset.uvblob";
    const auto guid = LoadUVE(path);
    ASSERT_TRUE(guid);
    PollUVE();
    const auto stamp = std::filesystem::last_write_time(path);
    ASSERT_TRUE(WriteBlobUVE(path, std::byte{2}));
    std::filesystem::last_write_time(path, stamp);
    PollUVE(0.25);
    ASSERT_TRUE(WriteBlobUVE(path, std::byte{3}));
    std::filesystem::last_write_time(path, stamp);
    PollUVE(0.25);
    EXPECT_TRUE(reloaded.empty());
    PollUVE(0.5);
    EXPECT_EQ(reloaded, (std::vector<AssetGuidUVE>{*guid}));
    ASSERT_TRUE(WaitReadyUVE(handles.front()));
    EXPECT_EQ(handles.front().TryGetUVE()->front(), std::byte{3});
    PollUVE();
    EXPECT_EQ(reloaded.size(), 1U);
}
TEST_F(HotReloadBackendUVETest, RealNativeQueueOverflowRescansLostSameMetadataWrites) {
    ConfigureUVE(HotReloadPreferenceUVE::Automatic);
    if (reload->GetBackendUVE() != HotReloadBackendUVE::Inotify)
        GTEST_SKIP() << "Native inotify unavailable";
    std::ifstream limitFile("/proc/sys/fs/inotify/max_queued_events");
    std::size_t limit = 0U;
    if (!(limitFile >> limit) || limit == 0U || limit > 65536U)
        GTEST_SKIP() << "Kernel queue outside bounded fixture range 1..65536";
    const auto path = root / "asset.uvblob";
    const auto guid = LoadUVE(path);
    ASSERT_TRUE(guid);
    PollUVE();
    const auto stamp = std::filesystem::last_write_time(path);
    for (std::size_t i = 0; i <= limit; ++i) {
        std::ofstream file(root / ("overflow-" + std::to_string(i)));
        ASSERT_TRUE(file.good());
    }
    // Distinct names overflow the real queue; the tracked write happens AFTER it is full.
    ASSERT_TRUE(WriteBlobUVE(path, std::byte{2}));
    std::filesystem::last_write_time(path, stamp);
    for (int i = 0; i < 16 && reload->GetNativeOverflowCountUVE() == 0U; ++i)
        PollUVE();
    EXPECT_GT(reload->GetNativeOverflowCountUVE(), 0U);
    EXPECT_NE(std::find(reloaded.begin(), reloaded.end(), *guid), reloaded.end());
    ASSERT_TRUE(WaitReadyUVE(handles.front()));
    EXPECT_EQ(handles.front().TryGetUVE()->front(), std::byte{2});
}
TEST_F(HotReloadBackendUVETest, ChangeDuringAnInFlightLoadRemainsPending) {
    ConfigureUVE(HotReloadPreferenceUVE::Polling);
    const auto path = root / "asset.uvblob";
    const auto guid = LoadUVE(path);
    ASSERT_TRUE(guid);
    PollUVE();
    std::promise<void> entered, release;
    auto entry = entered.get_future(), gate = release.get_future();
    std::atomic<int> calls{0};
    bool released = false;
    Utilities::ScopeGuardUVE cleanup{[&] {
        if (!released)
            release.set_value();
        handles.clear();
        manager.reset();
    }};
    manager->RegisterLoaderUVE<BlobAssetUVE>([&](const auto &file, auto &value) {
        const bool read = ReadBlobUVE(file, value);
        if (calls.fetch_add(1) == 0) {
            entered.set_value();
            gate.wait();
        }
        return read;
    });
    const auto stamp = std::filesystem::last_write_time(path);
    ASSERT_TRUE(WriteBlobUVE(path, std::byte{2}));
    std::filesystem::last_write_time(path, stamp + std::chrono::hours{1});
    PollUVE();
    ASSERT_EQ(entry.wait_for(std::chrono::seconds{2}), std::future_status::ready);
    ASSERT_TRUE(WriteBlobUVE(path, std::byte{3}));
    std::filesystem::last_write_time(path, stamp + std::chrono::hours{2});
    PollUVE();
    EXPECT_EQ(reloaded, (std::vector<AssetGuidUVE>{*guid}));
    release.set_value();
    released = true;
    ASSERT_TRUE(WaitReadyUVE(handles.front()));
    EXPECT_EQ(handles.front().TryGetUVE()->front(), std::byte{2});
    PollUVE();
    ASSERT_TRUE(WaitReadyUVE(handles.front()));
    EXPECT_EQ(handles.front().TryGetUVE()->front(), std::byte{3});
    EXPECT_EQ(reloaded, (std::vector<AssetGuidUVE>{*guid, *guid}));
}
TEST_F(HotReloadBackendUVETest, EpochTimestampIsNotAnUninitializedSentinel) {
    ConfigureUVE(HotReloadPreferenceUVE::Polling);
    const auto path = root / "asset.uvblob";
    const auto guid = LoadUVE(path);
    ASSERT_TRUE(guid);
    const auto epoch = std::filesystem::file_time_type{};
    std::filesystem::last_write_time(path, epoch);
    PollUVE();
    std::filesystem::last_write_time(path, epoch + std::chrono::hours{1});
    PollUVE();
    EXPECT_EQ(reloaded, (std::vector<AssetGuidUVE>{*guid}));
}
TEST_F(HotReloadBackendUVETest, NativeDirectoryReplacementRearmsAndRescans) {
    ConfigureUVE(HotReloadPreferenceUVE::Automatic);
    if (reload->GetBackendUVE() != HotReloadBackendUVE::Inotify)
        GTEST_SKIP() << "Native inotify unavailable";
    const auto directory = root / "watched";
    ASSERT_TRUE(std::filesystem::create_directory(directory));
    const auto path = directory / "asset.uvblob";
    const auto guid = LoadUVE(path);
    ASSERT_TRUE(guid);
    PollUVE();
    const auto stamp = std::filesystem::last_write_time(path);
    std::filesystem::rename(directory, root / "moved");
    ASSERT_TRUE(std::filesystem::create_directory(directory));
    ASSERT_TRUE(WriteBlobUVE(path, std::byte{2}));
    std::filesystem::last_write_time(path, stamp);
    PollUVE();
    EXPECT_EQ(reloaded, (std::vector<AssetGuidUVE>{*guid}));
    ASSERT_TRUE(WaitReadyUVE(handles.front()));
    EXPECT_EQ(handles.front().TryGetUVE()->front(), std::byte{2});
}
TEST_F(HotReloadBackendUVETest, NativeSymlinkTargetDirectoryIsAlsoWatched) {
    ConfigureUVE(HotReloadPreferenceUVE::Automatic);
    if (reload->GetBackendUVE() != HotReloadBackendUVE::Inotify)
        GTEST_SKIP() << "Native inotify unavailable";
    ASSERT_TRUE(std::filesystem::create_directory(root / "target"));
    const auto target = root / "target/data.uvblob", alias = root / "alias.uvblob";
    ASSERT_TRUE(WriteBlobUVE(target, std::byte{1}));
    std::error_code error;
    std::filesystem::create_symlink(target, alias, error);
    if (error)
        GTEST_SKIP() << "Filesystem cannot create symlinks: " << error.message();
    const auto guid = LoadUVE(alias);
    ASSERT_TRUE(guid);
    PollUVE();
    const auto stamp = std::filesystem::last_write_time(target);
    ASSERT_TRUE(WriteBlobUVE(target, std::byte{2}));
    std::filesystem::last_write_time(target, stamp);
    PollUVE();
    EXPECT_EQ(reloaded, (std::vector<AssetGuidUVE>{*guid}));
    ASSERT_TRUE(WaitReadyUVE(handles.front()));
    EXPECT_EQ(handles.front().TryGetUVE()->front(), std::byte{2});
}
TEST(HotReloadShutdownUVETest, UnknownPreferenceIsRejected) {
    Events::EventSystemUVE events;
    EXPECT_THROW((HotReloadUVE{events, 1.0, static_cast<HotReloadPreferenceUVE>(255)}),
                 std::invalid_argument);
}
TEST(HotReloadShutdownUVETest, RepeatedConstructionDoesNotLeakNativeDescriptors) {
    Events::EventSystemUVE events;
#if defined(__linux__)
    const auto descriptors = [] {
        std::size_t count = 0U;
        for (const auto &entry : std::filesystem::directory_iterator("/proc/self/fd")) {
            static_cast<void>(entry);
            ++count;
        }
        return count;
    };
    const auto before = descriptors();
    for (int i = 0; i < 64; ++i) {
        HotReloadUVE reload{events};
    }
    EXPECT_EQ(descriptors(), before);
#else
    HotReloadUVE reload{events};
    EXPECT_EQ(reload.GetBackendUVE(), HotReloadBackendUVE::Polling);
#endif
}
} // namespace
