// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "hot_reload.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <limits>
#include <latch>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "asset_database.h"
#include "asset_handle.h"
#include "asset_manager.h"
#include "asset_reloaded_event.h"
#include "blob_asset.h"
#include "uve_file_envelope.h"
#include "log_sink.h"
#include "logger.h"
#include "event_system.h"
#include "thread_pool.h"

namespace UVE::Asset::Tests {
namespace {

[[nodiscard]] std::vector<std::byte> MakePayload(std::string_view text) {
    const auto* const bytes = reinterpret_cast<const std::byte*>(text.data());
    return std::vector<std::byte>(bytes, bytes + text.size());
}

[[nodiscard]] bool LoadBlob(const std::filesystem::path& path, BlobAsset& outValue) {
    auto result = ReadUveFile(path);
    if (!result.has_value()) {
        return false;
    }
    outValue = std::move(result->second);
    return true;
}

template <typename T>
[[nodiscard]] bool WaitForTerminalState(const AssetHandle<T>& handle, int maxIterations = 200000) {
    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        if (handle.IsReady() || handle.HasFailed()) {
            return true;
        }
        std::this_thread::yield();
    }
    return false;
}

class HotReloadTest : public ::testing::Test {
protected:
    Threading::ThreadPool threadPool{2};
    Events::EventSystem eventSystem;
    AssetDatabase assetDatabase;
    HotReload hotReload{eventSystem, 0.05};
    AssetManager assetManager{threadPool, eventSystem, &hotReload};

    void SetUp() override { assetManager.RegisterLoader<BlobAsset>(LoadBlob); }
};

TEST_F(HotReloadTest, ReadyIsPublishedOnlyAfterTrackingCompletes) {
    struct GatedTracking final : IHotReload {
        HotReload& delegate;
        std::latch entered{1};
        std::latch release{1};
        std::atomic<bool> tracked{false};
        explicit GatedTracking(HotReload& value) : delegate(value) {}
        void Track(AssetGuid guid) override {
            entered.count_down();
            release.wait();
            delegate.Track(guid);
            tracked.store(true);
        }
        void Untrack(AssetGuid guid) override { delegate.Untrack(guid); }
        void Poll(IAssetManager& manager, IAssetDatabase& db, double delta) override {
            delegate.Poll(manager,db,delta);
        }
    } tracking(hotReload);
    AssetManager manager(threadPool,eventSystem,&tracking);
    manager.RegisterLoader<BlobAsset>([](const auto&,auto& data) { data.resize(8); return true; });
    auto handle=manager.Load<BlobAsset>(assetDatabase.Register("tracking-publication.uvblob"),assetDatabase);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!tracking.entered.try_wait() && std::chrono::steady_clock::now()<deadline) std::this_thread::yield();
    const bool entered=tracking.entered.try_wait();
    EXPECT_TRUE(entered);
    if(entered) { EXPECT_FALSE(handle.IsReady()); }
    // Always release before any assertion can return; the manager must join this load job.
    tracking.release.count_down();
    ASSERT_TRUE(WaitForTerminalState(handle));
    EXPECT_TRUE(handle.IsReady());
    EXPECT_TRUE(tracking.tracked.load());
}

TEST_F(HotReloadTest, Poll_DetectsOnDiskChange_ReloadsAssetAndPublishesEvent) {
    const std::filesystem::path path = "uve_hot_reload_tests_change.uvblob";
    std::filesystem::remove(path);
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Blob, MakePayload("version1")));
    const AssetGuid guid = assetDatabase.Register(path);

    const AssetHandle<BlobAsset> handle = assetManager.Load<BlobAsset>(guid, assetDatabase);
    ASSERT_TRUE(WaitForTerminalState(handle));
    ASSERT_TRUE(handle.IsReady());

    // First poll (well over the 0.05s interval) merely establishes the tracked baseline mtime -
    // it must not itself be treated as a change.
    hotReload.Poll(assetManager, assetDatabase, 10.0);

    ASSERT_TRUE(WriteUveFile(path, AssetKind::Blob, MakePayload("version2")));
    const auto farFuture = std::filesystem::file_time_type::clock::now() + std::chrono::hours(1);
    std::filesystem::last_write_time(path, farFuture);

    std::atomic<bool> reloadEventReceived{false};
    eventSystem.Subscribe<AssetReloadedEvent>([&reloadEventReceived, guid](const AssetReloadedEvent& event) {
        if (event.guid == guid) {
            reloadEventReceived = true;
        }
    });

    hotReload.Poll(assetManager, assetDatabase, 10.0);
    const auto reloadDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    bool contentReloaded = false;
    while (std::chrono::steady_clock::now() < reloadDeadline &&
           (!reloadEventReceived.load() || !contentReloaded)) {
        eventSystem.DispatchQueued();
        if (handle.IsReady()) {
            const BlobAsset* const blob = handle.TryGet();
            if (blob != nullptr) {
                const std::string content(reinterpret_cast<const char*>(blob->data()), blob->size());
                contentReloaded = content == "version2";
            }
        }
        std::this_thread::yield();
    }
    EXPECT_TRUE(reloadEventReceived.load());
    EXPECT_TRUE(contentReloaded);

    std::filesystem::remove(path);
}

TEST_F(HotReloadTest, Poll_InvalidDeltaTimes_DoNotMutateAccumulatorOrTriggerReload) {
    const std::filesystem::path path = "uve_hot_reload_tests_invalid_delta.uvblob";
    std::filesystem::remove(path);
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Blob, MakePayload("version1")));
    const AssetGuid guid = assetDatabase.Register(path);

    const AssetHandle<BlobAsset> handle = assetManager.Load<BlobAsset>(guid, assetDatabase);
    ASSERT_TRUE(WaitForTerminalState(handle));
    ASSERT_TRUE(handle.IsReady());
    hotReload.Poll(assetManager, assetDatabase, 10.0); // establish the baseline mtime

    ASSERT_TRUE(WriteUveFile(path, AssetKind::Blob, MakePayload("version2")));
    const auto farFuture = std::filesystem::file_time_type::clock::now() + std::chrono::hours(1);
    std::filesystem::last_write_time(path, farFuture);

    std::atomic<bool> reloadEventReceived{false};
    eventSystem.Subscribe<AssetReloadedEvent>([&reloadEventReceived, guid](const AssetReloadedEvent& event) {
        if (event.guid == guid) {
            reloadEventReceived = true;
        }
    });

    hotReload.Poll(assetManager, assetDatabase, -1.0);
    hotReload.Poll(assetManager, assetDatabase, std::numeric_limits<double>::quiet_NaN());
    hotReload.Poll(assetManager, assetDatabase, 0.04); // below the 0.05s interval if invalid deltas were ignored
    eventSystem.DispatchQueued();

    EXPECT_FALSE(reloadEventReceived.load());
    EXPECT_TRUE(handle.IsReady());
    const BlobAsset* const blob = handle.TryGet();
    if (blob != nullptr) {
        const std::string content(reinterpret_cast<const char*>(blob->data()), blob->size());
        EXPECT_EQ(content, "version1");
    }

    std::filesystem::remove(path);
}

TEST_F(HotReloadTest, Poll_BelowConfiguredInterval_NeverReloads) {
    HotReload localHotReload(eventSystem, 1000.0);
    AssetManager localAssetManager(threadPool, eventSystem, &localHotReload);
    localAssetManager.RegisterLoader<BlobAsset>(LoadBlob);

    const std::filesystem::path path = "uve_hot_reload_tests_below_interval.uvblob";
    std::filesystem::remove(path);
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Blob, MakePayload("version1")));
    const AssetGuid guid = assetDatabase.Register(path);

    const AssetHandle<BlobAsset> handle = localAssetManager.Load<BlobAsset>(guid, assetDatabase);
    ASSERT_TRUE(WaitForTerminalState(handle));

    ASSERT_TRUE(WriteUveFile(path, AssetKind::Blob, MakePayload("version2")));
    const auto farFuture = std::filesystem::file_time_type::clock::now() + std::chrono::hours(1);
    std::filesystem::last_write_time(path, farFuture);

    localHotReload.Poll(localAssetManager, assetDatabase, 1.0); // far below the 1000s interval

    const BlobAsset* const blob = handle.TryGet();
    ASSERT_NE(blob, nullptr);
    const std::string content(reinterpret_cast<const char*>(blob->data()), blob->size());
    EXPECT_EQ(content, "version1");

    std::filesystem::remove(path);
}

TEST_F(HotReloadTest, Poll_TrackedFileDeleted_LogsWarningAndDoesNotCrash) {
    const std::filesystem::path path = "uve_hot_reload_tests_deleted.uvblob";
    std::filesystem::remove(path);
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Blob, MakePayload("version1")));
    const AssetGuid guid = assetDatabase.Register(path);

    const AssetHandle<BlobAsset> handle = assetManager.Load<BlobAsset>(guid, assetDatabase);
    ASSERT_TRUE(WaitForTerminalState(handle));
    std::filesystem::remove(path);

    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    hotReload.Poll(assetManager, assetDatabase, 10.0);

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundWarning =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Warning &&
                   message.message.contains("missing");
        });
    EXPECT_TRUE(foundWarning);

    logger.Shutdown();
}

} // namespace
} // namespace UVE::Asset::Tests
