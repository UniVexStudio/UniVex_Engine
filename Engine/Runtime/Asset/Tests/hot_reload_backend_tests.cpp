// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "hot_reload.h"
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
#include "Support/test_scratch.h"
#include "asset_database.h"
#include "asset_handle.h"
#include "asset_manager.h"
#include "asset_reloaded_event.h"
#include "blob_asset.h"
#include "uve_file_envelope.h"
#include "event_system.h"
#include "job_counter.h"
#include "thread_pool.h"
#include "scope_guard.h"
namespace {
using namespace UVE;
using namespace UVE::Asset;
bool ReadBlob(const std::filesystem::path &path, BlobAsset &value) {
    auto result = ReadUveFile(path);
    if (!result)
        return false;
    value = std::move(result->second);
    return true;
}
bool WriteBlob(const std::filesystem::path &path, std::byte value) {
    return WriteUveFile(path, AssetKind::Blob, std::vector<std::byte>{value});
}
class HotReloadBackendTest : public ::testing::Test {
  protected:
    Threading::ThreadPool pool{1U};
    Events::EventSystem events;
    AssetDatabase database;
    std::unique_ptr<HotReload> reload;
    std::unique_ptr<AssetManager> manager;
    std::vector<AssetHandle<BlobAsset>> handles;
    std::vector<AssetGuid> reloaded;
    std::filesystem::path root;
    void SetUp() override {
        root = UVE::Tests::MakeTestCaseDirectory();
        events.Subscribe<AssetReloadedEvent>(
            [this](const auto &event) { reloaded.push_back(event.guid); });
    }
    void Configure(HotReloadPreference preference) {
        reload = std::make_unique<HotReload>(events, 1.0, preference);
        manager = std::make_unique<AssetManager>(pool, events, reload.get());
        manager->RegisterLoader<BlobAsset>(ReadBlob);
    }
    bool WaitReady(const AssetHandle<BlobAsset> &handle) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        while (!handle.IsReady() && !handle.HasFailed() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        if (!handle.IsReady())
            return false;
        // Ready precedes Track in the worker. Wait for the completion callback too.
        Threading::JobCounter barrier;
        pool.Submit([] {}, barrier);
        barrier.Wait();
        return true;
    }
    std::optional<AssetGuid> Load(const std::filesystem::path &path) {
        if (!WriteBlob(path, std::byte{1}))
            return std::nullopt;
        const auto guid = database.Register(path);
        handles.push_back(manager->Load<BlobAsset>(guid, database));
        if (!WaitReady(handles.back()))
            return std::nullopt;
        return guid;
    }
    void Poll(double delta = 1.0) {
        reload->Poll(*manager, database, delta);
        events.DispatchQueued();
    }
    void Bump(const std::filesystem::path &path) {
        std::filesystem::last_write_time(path,
                                         std::filesystem::last_write_time(path) + std::chrono::hours{1});
    }
};
TEST_F(HotReloadBackendTest, ForcedPollingDebouncesAndPublishesRealReloadedData) {
    Configure(HotReloadPreference::Polling);
    EXPECT_EQ(reload->GetBackend(), HotReloadBackend::Polling);
    const auto path = root / "asset.uvblob";
    const auto guid = Load(path);
    ASSERT_TRUE(guid);
    Poll();
    EXPECT_TRUE(reloaded.empty());
    ASSERT_TRUE(WriteBlob(path, std::byte{2}));
    Bump(path);
    Poll(0.25);
    ASSERT_TRUE(WriteBlob(path, std::byte{3}));
    Bump(path);
    Poll(0.25);
    EXPECT_TRUE(reloaded.empty());
    Poll(0.5);
    EXPECT_EQ(reloaded, (std::vector<AssetGuid>{*guid}));
    ASSERT_TRUE(WaitReady(handles.front()));
    EXPECT_EQ(handles.front().TryGet()->front(), std::byte{3});
    Poll();
    EXPECT_EQ(reloaded.size(), 1U);
}
TEST_F(HotReloadBackendTest, DeleteThenRecreateWithIdenticalMetadataReloadsWithoutDestroyingLastGoodData) {
    Configure(HotReloadPreference::Polling);
    const auto path = root / "asset.uvblob";
    const auto guid = Load(path);
    ASSERT_TRUE(guid);
    Poll();
    const auto stamp = std::filesystem::last_write_time(path);
    ASSERT_TRUE(std::filesystem::remove(path));
    Poll();
    EXPECT_TRUE(reloaded.empty());
    EXPECT_EQ(handles.front().TryGet()->front(), std::byte{1});
    ASSERT_TRUE(WriteBlob(path, std::byte{2}));
    std::filesystem::last_write_time(path, stamp);
    Poll();
    EXPECT_EQ(reloaded, (std::vector<AssetGuid>{*guid}));
    ASSERT_TRUE(WaitReady(handles.front()));
    EXPECT_EQ(handles.front().TryGet()->front(), std::byte{2});
}
TEST_F(HotReloadBackendTest, RepeatedTrackDoesNotEraseObservedChanges) {
    Configure(HotReloadPreference::Polling);
    const auto path = root / "asset.uvblob";
    const auto guid = Load(path);
    ASSERT_TRUE(guid);
    Poll();
    ASSERT_TRUE(WriteBlob(path, std::byte{2}));
    Bump(path);
    reload->Track(*guid);
    Poll();
    EXPECT_EQ(reloaded, (std::vector<AssetGuid>{*guid}));
}
TEST_F(HotReloadBackendTest, DatabaseRelocationPreservesGuidAndNoticesRename) {
    Configure(HotReloadPreference::Automatic);
    const auto path = root / "before.uvblob", after = root / "after.uvblob";
    const auto guid = Load(path);
    ASSERT_TRUE(guid);
    Poll();
    std::filesystem::rename(path, after);
    ASSERT_TRUE(database.Relocate(*guid, after));
    Poll();
    EXPECT_EQ(reloaded, (std::vector<AssetGuid>{*guid}));
    EXPECT_EQ(database.Resolve(*guid), after);
}
TEST_F(HotReloadBackendTest, RequestsAreSortedAndUntrackedFilesDoNotTriggerFurtherReloads) {
    Configure(HotReloadPreference::Automatic);
    const auto aPath = root / "a.uvblob", bPath = root / "b.uvblob";
    const auto a = Load(aPath), b = Load(bPath);
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    Poll();
    ASSERT_TRUE(WriteBlob(bPath, std::byte{2}));
    Bump(bPath);
    ASSERT_TRUE(WriteBlob(aPath, std::byte{2}));
    Bump(aPath);
    Poll();
    std::vector<AssetGuid> expected{*a, *b};
    std::sort(expected.begin(), expected.end(),
              [](auto left, auto right) { return left.value < right.value; });
    EXPECT_EQ(reloaded, expected);
    for (const auto &handle : handles)
        ASSERT_TRUE(WaitReady(handle));
    reload->Untrack(*a);
    ASSERT_TRUE(WriteBlob(aPath, std::byte{3}));
    Bump(aPath);
    Poll();
    EXPECT_EQ(reloaded, expected);
}
TEST_F(HotReloadBackendTest, NativeSameSizeSameTimestampWritesAreDetectedAndCoalesced) {
    Configure(HotReloadPreference::Automatic);
    if (reload->GetBackend() != HotReloadBackend::Inotify)
        GTEST_SKIP() << "Native inotify unavailable; polling is covered separately";
    const auto path = root / "asset.uvblob";
    const auto guid = Load(path);
    ASSERT_TRUE(guid);
    Poll();
    const auto stamp = std::filesystem::last_write_time(path);
    ASSERT_TRUE(WriteBlob(path, std::byte{2}));
    std::filesystem::last_write_time(path, stamp);
    Poll(0.25);
    ASSERT_TRUE(WriteBlob(path, std::byte{3}));
    std::filesystem::last_write_time(path, stamp);
    Poll(0.25);
    EXPECT_TRUE(reloaded.empty());
    Poll(0.5);
    EXPECT_EQ(reloaded, (std::vector<AssetGuid>{*guid}));
    ASSERT_TRUE(WaitReady(handles.front()));
    EXPECT_EQ(handles.front().TryGet()->front(), std::byte{3});
    Poll();
    EXPECT_EQ(reloaded.size(), 1U);
}
TEST_F(HotReloadBackendTest, RealNativeQueueOverflowRescansLostSameMetadataWrites) {
    Configure(HotReloadPreference::Automatic);
    if (reload->GetBackend() != HotReloadBackend::Inotify)
        GTEST_SKIP() << "Native inotify unavailable";
    std::ifstream limitFile("/proc/sys/fs/inotify/max_queued_events");
    std::size_t limit = 0U;
    if (!(limitFile >> limit) || limit == 0U || limit > 65536U)
        GTEST_SKIP() << "Kernel queue outside bounded fixture range 1..65536";
    const auto path = root / "asset.uvblob";
    const auto guid = Load(path);
    ASSERT_TRUE(guid);
    Poll();
    const auto stamp = std::filesystem::last_write_time(path);
    for (std::size_t i = 0; i <= limit; ++i) {
        std::ofstream file(root / ("overflow-" + std::to_string(i)));
        ASSERT_TRUE(file.good());
    }
    // Distinct names overflow the real queue; the tracked write happens AFTER it is full.
    ASSERT_TRUE(WriteBlob(path, std::byte{2}));
    std::filesystem::last_write_time(path, stamp);
    for (int i = 0; i < 16 && reload->GetNativeOverflowCount() == 0U; ++i)
        Poll();
    EXPECT_GT(reload->GetNativeOverflowCount(), 0U);
    EXPECT_NE(std::find(reloaded.begin(), reloaded.end(), *guid), reloaded.end());
    ASSERT_TRUE(WaitReady(handles.front()));
    EXPECT_EQ(handles.front().TryGet()->front(), std::byte{2});
}
TEST_F(HotReloadBackendTest, ChangeDuringAnInFlightLoadRemainsPending) {
    Configure(HotReloadPreference::Polling);
    const auto path = root / "asset.uvblob";
    const auto guid = Load(path);
    ASSERT_TRUE(guid);
    Poll();
    std::promise<void> entered, release;
    auto entry = entered.get_future(), gate = release.get_future();
    std::atomic<int> calls{0};
    bool released = false;
    Utilities::ScopeGuard cleanup{[&] {
        if (!released)
            release.set_value();
        handles.clear();
        manager.reset();
    }};
    manager->RegisterLoader<BlobAsset>([&](const auto &file, auto &value) {
        const bool read = ReadBlob(file, value);
        if (calls.fetch_add(1) == 0) {
            entered.set_value();
            gate.wait();
        }
        return read;
    });
    const auto stamp = std::filesystem::last_write_time(path);
    ASSERT_TRUE(WriteBlob(path, std::byte{2}));
    std::filesystem::last_write_time(path, stamp + std::chrono::hours{1});
    Poll();
    ASSERT_EQ(entry.wait_for(std::chrono::seconds{2}), std::future_status::ready);
    ASSERT_TRUE(WriteBlob(path, std::byte{3}));
    std::filesystem::last_write_time(path, stamp + std::chrono::hours{2});
    Poll();
    EXPECT_EQ(reloaded, (std::vector<AssetGuid>{*guid}));
    release.set_value();
    released = true;
    ASSERT_TRUE(WaitReady(handles.front()));
    EXPECT_EQ(handles.front().TryGet()->front(), std::byte{2});
    Poll();
    ASSERT_TRUE(WaitReady(handles.front()));
    EXPECT_EQ(handles.front().TryGet()->front(), std::byte{3});
    EXPECT_EQ(reloaded, (std::vector<AssetGuid>{*guid, *guid}));
}
TEST_F(HotReloadBackendTest, EpochTimestampIsNotAnUninitializedSentinel) {
    Configure(HotReloadPreference::Polling);
    const auto path = root / "asset.uvblob";
    const auto guid = Load(path);
    ASSERT_TRUE(guid);
    const auto epoch = std::filesystem::file_time_type{};
    std::filesystem::last_write_time(path, epoch);
    Poll();
    std::filesystem::last_write_time(path, epoch + std::chrono::hours{1});
    Poll();
    EXPECT_EQ(reloaded, (std::vector<AssetGuid>{*guid}));
}
TEST_F(HotReloadBackendTest, NativeDirectoryReplacementRearmsAndRescans) {
    Configure(HotReloadPreference::Automatic);
    if (reload->GetBackend() != HotReloadBackend::Inotify)
        GTEST_SKIP() << "Native inotify unavailable";
    const auto directory = root / "watched";
    ASSERT_TRUE(std::filesystem::create_directory(directory));
    const auto path = directory / "asset.uvblob";
    const auto guid = Load(path);
    ASSERT_TRUE(guid);
    Poll();
    const auto stamp = std::filesystem::last_write_time(path);
    std::filesystem::rename(directory, root / "moved");
    ASSERT_TRUE(std::filesystem::create_directory(directory));
    ASSERT_TRUE(WriteBlob(path, std::byte{2}));
    std::filesystem::last_write_time(path, stamp);
    Poll();
    EXPECT_EQ(reloaded, (std::vector<AssetGuid>{*guid}));
    ASSERT_TRUE(WaitReady(handles.front()));
    EXPECT_EQ(handles.front().TryGet()->front(), std::byte{2});
}
TEST_F(HotReloadBackendTest, NativeSymlinkTargetDirectoryIsAlsoWatched) {
    Configure(HotReloadPreference::Automatic);
    if (reload->GetBackend() != HotReloadBackend::Inotify)
        GTEST_SKIP() << "Native inotify unavailable";
    ASSERT_TRUE(std::filesystem::create_directory(root / "target"));
    const auto target = root / "target/data.uvblob", alias = root / "alias.uvblob";
    ASSERT_TRUE(WriteBlob(target, std::byte{1}));
    std::error_code error;
    std::filesystem::create_symlink(target, alias, error);
    if (error)
        GTEST_SKIP() << "Filesystem cannot create symlinks: " << error.message();
    const auto guid = Load(alias);
    ASSERT_TRUE(guid);
    Poll();
    const auto stamp = std::filesystem::last_write_time(target);
    ASSERT_TRUE(WriteBlob(target, std::byte{2}));
    std::filesystem::last_write_time(target, stamp);
    Poll();
    EXPECT_EQ(reloaded, (std::vector<AssetGuid>{*guid}));
    ASSERT_TRUE(WaitReady(handles.front()));
    EXPECT_EQ(handles.front().TryGet()->front(), std::byte{2});
}
TEST(HotReloadShutdownTest, UnknownPreferenceIsRejected) {
    Events::EventSystem events;
    EXPECT_THROW((HotReload{events, 1.0, static_cast<HotReloadPreference>(255)}),
                 std::invalid_argument);
}
TEST(HotReloadShutdownTest, RepeatedConstructionDoesNotLeakNativeDescriptors) {
    Events::EventSystem events;
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
        HotReload reload{events};
    }
    EXPECT_EQ(descriptors(), before);
#else
    HotReload reload{events};
    EXPECT_EQ(reload.GetBackend(), HotReloadBackend::Polling);
#endif
}
} // namespace
