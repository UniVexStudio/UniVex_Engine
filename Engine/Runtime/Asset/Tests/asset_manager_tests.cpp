// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "asset_manager.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "asset_database.h"
#include "asset_handle.h"
#include "asset_load_completed_event.h"
#include "blob_asset.h"
#include "uve_file_envelope.h"
#include "log_sink.h"
#include "logger.h"
#include "event_system.h"
#include "platform.h"
#include "thread_pool.h"

namespace UVE::Asset::Tests {
namespace {

[[nodiscard]] bool LoadBlob(const std::filesystem::path& path, BlobAsset& outValue) {
    auto result = ReadUveFile(path);
    if (!result.has_value()) {
        return false;
    }
    outValue = std::move(result->second);
    return true;
}

/// Blocks (via bounded busy-poll, never a fixed sleep) until `handle` reaches a terminal state
/// (Loaded or Failed), or `maxIterations` is exceeded. Returns false on timeout.
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

/// A handle reaching a terminal state only guarantees AssetManager's record was updated - the
/// worker thread's subsequent UVE_ERROR()/QueueEvent() calls in the same function are not
/// synchronized with that state flip, so log assertions need their own bounded retry rather than
/// checking the sink exactly once.
[[nodiscard]] bool WaitForLogMessage(Debug::MemorySink& sink, Debug::LogLevel level,
                                         std::string_view substring, int maxIterations = 200000) {
    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        const std::vector<Debug::LogMessage> messages = sink.GetMessages();
        const bool found =
            std::any_of(messages.begin(), messages.end(), [level, substring](const Debug::LogMessage& message) {
                return message.level == level && message.message.contains(substring);
            });
        if (found) {
            return true;
        }
        std::this_thread::yield();
    }
    return false;
}

class ThrowingResolveAssetDatabase final : public IAssetDatabase {
public:
    bool Load(const std::filesystem::path&) override { return false; }
    bool Save() override { return false; }
    bool Save(const std::filesystem::path&) override { return false; }
    [[nodiscard]] AssetGuid Register(const std::filesystem::path&) override {
        return kInvalidAssetGuid;
    }
    [[nodiscard]] std::filesystem::path Resolve(AssetGuid) const override {
        throw std::runtime_error("injected asset database resolver exception");
    }
    [[nodiscard]] bool HasGuid(AssetGuid) const override { return false; }
    [[nodiscard]] std::vector<AssetRecord> GetRegisteredAssets() const override { return {}; }
};

class AssetManagerTest : public ::testing::Test {
protected:
    struct UnregisteredAsset final {};

    Threading::ThreadPool threadPool{2};
    Events::EventSystem eventSystem;
    AssetDatabase assetDatabase;
    AssetManager assetManager{threadPool, eventSystem};

    void SetUp() override { assetManager.RegisterLoader<BlobAsset>(LoadBlob); }
};

struct ShutdownTrackedAsset {
    std::atomic<int>* destroyed = nullptr;
    std::optional<AssetHandle<ShutdownTrackedAsset>> dependency;
    ~ShutdownTrackedAsset() { if (destroyed != nullptr) destroyed->fetch_add(1); }
};

TEST_F(AssetManagerTest, ShutdownDestroysCachedPayloadsAndSafelyReleasesDependentHandles) {
    std::atomic<int> destroyed{0};
    {
        AssetManager manager(threadPool, eventSystem);
        const auto child = assetDatabase.Register("shutdown-child.test");
        const auto parent = assetDatabase.Register("shutdown-parent.test");
        manager.RegisterLoader<ShutdownTrackedAsset>([&](const std::filesystem::path& path, auto& value) {
            value.destroyed = &destroyed;
            if (path.filename() == "shutdown-parent.test") value.dependency.emplace(manager.Load<ShutdownTrackedAsset>(child, assetDatabase));
            return true;
        });
        auto handle = manager.Load<ShutdownTrackedAsset>(parent, assetDatabase);
        ASSERT_TRUE(WaitForTerminalState(handle));
        ASSERT_TRUE(handle.IsReady());
        ASSERT_TRUE(handle.TryGet()->dependency.has_value());
        ASSERT_TRUE(WaitForTerminalState(*handle.TryGet()->dependency));
        EXPECT_EQ(destroyed.load(), 0);
        // The handle dies before the manager; no explicit garbage collection is performed.
    }
    EXPECT_EQ(destroyed.load(), 2);
}

TEST_F(AssetManagerTest, ThrowingLoaderDestroysItsPartiallyInitializedPayload) {
    std::atomic<int> destroyed{0};
    assetManager.RegisterLoader<ShutdownTrackedAsset>([&](const std::filesystem::path&, auto& value) -> bool {
        value.destroyed = &destroyed;
        throw std::runtime_error("injected loader failure");
    });
    const auto guid = assetDatabase.Register("throwing-payload.test");
    auto handle = assetManager.Load<ShutdownTrackedAsset>(guid, assetDatabase);
    ASSERT_TRUE(WaitForTerminalState(handle));
    EXPECT_TRUE(handle.HasFailed());
    EXPECT_EQ(destroyed.load(), 1);
}

#if UVE_DEBUG
TEST_F(AssetManagerTest, AddRef_MissingRecord_Asserts) {
    EXPECT_DEATH({ assetManager.AddRef(AssetGuid{0xBAD001U}); }, "");
}

TEST_F(AssetManagerTest, Release_MissingRecord_Asserts) {
    EXPECT_DEATH({ assetManager.Release(AssetGuid{0xBAD002U}); }, "");
}
#else
TEST_F(AssetManagerTest, MissingRecordRefcountCallsFailClosed) {
    EXPECT_NO_FATAL_FAILURE(assetManager.AddRef(AssetGuid{0xBAD001U}));
    EXPECT_NO_FATAL_FAILURE(assetManager.Release(AssetGuid{0xBAD002U}));
}
#endif

TEST_F(AssetManagerTest, Load_ResolverExceptionBecomesFailedHandleAndCompletionEvent) {
    ThrowingResolveAssetDatabase throwingDatabase;
    int completedCount = 0;
    bool completedSuccessfully = true;
    eventSystem.Subscribe<AssetLoadCompletedEvent>([&](const AssetLoadCompletedEvent& event) {
        ++completedCount;
        completedSuccessfully = event.success;
    });

    std::optional<AssetHandle<BlobAsset>> handle;
    EXPECT_NO_THROW(handle.emplace(assetManager.Load<BlobAsset>(AssetGuid{9002U}, throwingDatabase)));
    ASSERT_TRUE(handle.has_value());
    ASSERT_TRUE(WaitForTerminalState(*handle));
    EXPECT_FALSE(handle->IsReady());
    EXPECT_TRUE(handle->HasFailed());
    EXPECT_EQ(handle->GetFailureReason(),
              "asset database resolver threw: injected asset database resolver exception");
    eventSystem.DispatchQueued();
    EXPECT_EQ(completedCount, 1);
    EXPECT_FALSE(completedSuccessfully);
}

TEST_F(AssetManagerTest, Reload_ResolverExceptionPreservesLoadedValueAndReportsFailure) {
    const std::filesystem::path path = "uve_asset_manager_tests_resolver_exception.uvblob";
    std::filesystem::remove(path);
    const std::string text = "resolver exception keeps this value";
    const auto* const textBytes = reinterpret_cast<const std::byte*>(text.data());
    ASSERT_TRUE(WriteUveFile(
        path, AssetKind::Blob, std::vector<std::byte>(textBytes, textBytes + text.size())));

    const AssetGuid guid = assetDatabase.Register(path);
    const AssetHandle<BlobAsset> handle = assetManager.Load<BlobAsset>(guid, assetDatabase);
    ASSERT_TRUE(WaitForTerminalState(handle));
    ASSERT_TRUE(handle.IsReady());
    ThrowingResolveAssetDatabase throwingDatabase;

    assetManager.Reload(guid, throwingDatabase);

    ASSERT_TRUE(handle.IsReady());
    ASSERT_NE(handle.TryGet(), nullptr);
    const BlobAsset* const blob = handle.TryGet();
    ASSERT_NE(blob, nullptr);
    const std::string roundTripped(reinterpret_cast<const char*>(blob->data()), blob->size());
    EXPECT_EQ(roundTripped, text);
    EXPECT_EQ(handle.GetFailureReason(),
              "asset database resolver threw: injected asset database resolver exception");

    std::filesystem::remove(path);
}

TEST_F(AssetManagerTest, Load_MissingLoaderReturnsFailedHandleWithoutWorkerSubmission) {
    const AssetHandle<UnregisteredAsset> handle =
        assetManager.Load<UnregisteredAsset>(AssetGuid{9001U}, assetDatabase);

    EXPECT_TRUE(WaitForTerminalState(handle));
    EXPECT_FALSE(handle.IsReady());
    EXPECT_TRUE(handle.HasFailed());
    EXPECT_EQ(handle.GetFailureReason(), "no loader registered for asset type");
    EXPECT_EQ(assetManager.GetLoadedAssetCount(), 0U);
}

TEST_F(AssetManagerTest, Load_RealFile_BecomesReadyWithMatchingBytes) {
    const std::filesystem::path path = "uve_asset_manager_tests_basic.uvblob";
    std::filesystem::remove(path);
    const std::string text = "hello asset manager";
    const auto* const textBytes = reinterpret_cast<const std::byte*>(text.data());
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Blob, std::vector<std::byte>(textBytes, textBytes + text.size())));

    const AssetGuid guid = assetDatabase.Register(path);
    const AssetHandle<BlobAsset> handle = assetManager.Load<BlobAsset>(guid, assetDatabase);

    ASSERT_TRUE(WaitForTerminalState(handle));
    ASSERT_TRUE(handle.IsReady());
    const BlobAsset* const blob = handle.TryGet();
    ASSERT_NE(blob, nullptr);
    const std::string roundTripped(reinterpret_cast<const char*>(blob->data()), blob->size());
    EXPECT_EQ(roundTripped, text);

    std::filesystem::remove(path);
}

TEST_F(AssetManagerTest, Reload_WhileLoading_RunsAgainAndEndsWithTheNewFile) {
    // A tool rewrote the file while it was being loaded: the reload asked for meanwhile is not
    // dropped, so the asset ends up holding the new file, not the one the running load read.
    const std::filesystem::path path = "uve_asset_manager_tests_queued_reload.uvblob";
    std::filesystem::remove(path);
    const auto write = [&path](const std::string& text) {
        const auto* const bytes = reinterpret_cast<const std::byte*>(text.data());
        return WriteUveFile(path, AssetKind::Blob, std::vector<std::byte>(bytes, bytes + text.size()));
    };
    ASSERT_TRUE(write("before"));
    std::atomic<bool> release{false};
    std::atomic<int> loads{0};
    assetManager.RegisterLoader<BlobAsset>([&release, &loads](const std::filesystem::path& file, BlobAsset& out) {
        if (loads.fetch_add(1) == 0) {
            for (int spin = 0; spin < 2000000 && !release.load(); ++spin) {
                std::this_thread::yield();
            }
        }
        return LoadBlob(file, out);
    });
    const AssetGuid guid = assetDatabase.Register(path);
    const AssetHandle<BlobAsset> handle = assetManager.Load<BlobAsset>(guid, assetDatabase);
    for (int spin = 0; spin < 2000000 && loads.load() == 0; ++spin) {
        std::this_thread::yield();
    }
    ASSERT_EQ(loads.load(), 1);
    ASSERT_FALSE(handle.IsReady());
    assetManager.Reload(guid, assetDatabase);
    ASSERT_TRUE(write("after"));
    release.store(true);
    for (int spin = 0; spin < 4000000 && loads.load() < 2; ++spin) {
        std::this_thread::yield();
    }
    EXPECT_EQ(loads.load(), 2);
    bool sawAfter = false;
    for (int spin = 0; spin < 4000000 && !sawAfter; ++spin) {
        if (const BlobAsset* const blob = handle.TryGet(); blob != nullptr) {
            sawAfter = std::string(reinterpret_cast<const char*>(blob->data()), blob->size()) == "after";
        }
        std::this_thread::yield();
    }
    EXPECT_TRUE(sawAfter);
    std::filesystem::remove(path);
}

TEST_F(AssetManagerTest, Load_CalledTwiceForSameGuid_LoadsOnlyOnce) {
    const std::filesystem::path path = "uve_asset_manager_tests_shared.uvblob";
    std::filesystem::remove(path);
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Blob, {}));
    const AssetGuid guid = assetDatabase.Register(path);

    static std::atomic<int> loadCount{0};
    loadCount = 0;
    assetManager.RegisterLoader<BlobAsset>([](const std::filesystem::path& loaderPath, BlobAsset& out) {
        ++loadCount;
        return LoadBlob(loaderPath, out);
    });

    const AssetHandle<BlobAsset> first = assetManager.Load<BlobAsset>(guid, assetDatabase);
    const AssetHandle<BlobAsset> second = assetManager.Load<BlobAsset>(guid, assetDatabase);
    ASSERT_TRUE(WaitForTerminalState(first));
    ASSERT_TRUE(WaitForTerminalState(second));

    EXPECT_EQ(loadCount.load(), 1);

    std::filesystem::remove(path);
}

TEST_F(AssetManagerTest, CollectGarbage_DoesNotUnloadWhileAnotherHandleIsAlive) {
    const std::filesystem::path path = "uve_asset_manager_tests_refcount.uvblob";
    std::filesystem::remove(path);
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Blob, {}));
    const AssetGuid guid = assetDatabase.Register(path);

    AssetHandle<BlobAsset> keepAlive = assetManager.Load<BlobAsset>(guid, assetDatabase);
    ASSERT_TRUE(WaitForTerminalState(keepAlive));
    {
        const AssetHandle<BlobAsset> temporary = assetManager.Load<BlobAsset>(guid, assetDatabase);
        static_cast<void>(temporary);
    }

    assetManager.CollectGarbage();
    EXPECT_EQ(assetManager.GetLoadedAssetCount(), 1U);
    EXPECT_TRUE(keepAlive.IsReady());

    std::filesystem::remove(path);
}

TEST_F(AssetManagerTest, CollectGarbage_UnloadsOnceLastHandleIsReleased) {
    const std::filesystem::path path = "uve_asset_manager_tests_gc.uvblob";
    std::filesystem::remove(path);
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Blob, {}));
    const AssetGuid guid = assetDatabase.Register(path);

    {
        const AssetHandle<BlobAsset> handle = assetManager.Load<BlobAsset>(guid, assetDatabase);
        ASSERT_TRUE(WaitForTerminalState(handle));
        EXPECT_EQ(assetManager.GetLoadedAssetCount(), 1U);
    }

    assetManager.CollectGarbage();
    EXPECT_EQ(assetManager.GetLoadedAssetCount(), 0U);

    std::filesystem::remove(path);
}

TEST_F(AssetManagerTest, Load_UnknownGuid_BecomesFailedAndLogsError) {
    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    const AssetHandle<BlobAsset> handle =
        assetManager.Load<BlobAsset>(AssetGuid{424242}, assetDatabase);
    ASSERT_TRUE(WaitForTerminalState(handle));
    EXPECT_TRUE(handle.HasFailed());
    EXPECT_EQ(handle.TryGet(), nullptr);
    EXPECT_EQ(handle.GetFailureReason(), "asset database resolved no path");
    EXPECT_EQ(assetManager.GetFailureReason(AssetGuid{424242}), "asset database resolved no path");

    EXPECT_TRUE(WaitForLogMessage(*memorySinkPtr, Debug::LogLevel::Error, "failed to load asset"));

    logger.Shutdown();
}

TEST_F(AssetManagerTest, Load_LoaderRejectsExistingFile_ReportsReasonAndSuccessfulReloadClearsIt) {
    const std::filesystem::path path = "uve_asset_manager_tests_failure_reason.uvblob";
    std::filesystem::remove(path);
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Blob, {}));
    const AssetGuid guid = assetDatabase.Register(path);

    assetManager.RegisterLoader<BlobAsset>([](const std::filesystem::path&, BlobAsset&) { return false; });
    const AssetHandle<BlobAsset> handle = assetManager.Load<BlobAsset>(guid, assetDatabase);
    ASSERT_TRUE(WaitForTerminalState(handle));
    EXPECT_TRUE(handle.HasFailed());
    EXPECT_EQ(handle.GetFailureReason(), "registered asset loader rejected file");

    assetManager.RegisterLoader<BlobAsset>(LoadBlob);
    assetManager.Reload(guid, assetDatabase);
    ASSERT_TRUE(WaitForTerminalState(handle));
    EXPECT_TRUE(handle.IsReady());
    EXPECT_TRUE(handle.GetFailureReason().empty());

    std::filesystem::remove(path);
}

TEST_F(AssetManagerTest, LoaderExceptionFailsInitialLoadAndPreservesLastKnownGoodDataOnReload) {
    const std::filesystem::path path = "uve_asset_manager_tests_loader_exception.uvblob";
    std::filesystem::remove(path);
    const std::string text = "stable asset";
    const auto* const textBytes = reinterpret_cast<const std::byte*>(text.data());
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Blob,
                                std::vector<std::byte>(textBytes, textBytes + text.size())));
    const AssetGuid guid = assetDatabase.Register(path);

    assetManager.RegisterLoader<BlobAsset>(
        [](const std::filesystem::path&, BlobAsset&) -> bool {
            throw std::runtime_error("injected loader exception");
        });
    const AssetHandle<BlobAsset> failedHandle = assetManager.Load<BlobAsset>(guid, assetDatabase);
    ASSERT_TRUE(WaitForTerminalState(failedHandle));
    EXPECT_TRUE(failedHandle.HasFailed());
    EXPECT_EQ(failedHandle.GetFailureReason(), "asset loader threw: injected loader exception");

    assetManager.RegisterLoader<BlobAsset>(LoadBlob);
    assetManager.Reload(guid, assetDatabase);
    ASSERT_TRUE(WaitForTerminalState(failedHandle));
    ASSERT_TRUE(failedHandle.IsReady());
    const BlobAsset* const loaded = failedHandle.TryGet();
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(loaded->data()), loaded->size()), text);

    assetManager.RegisterLoader<BlobAsset>(
        [](const std::filesystem::path&, BlobAsset&) -> bool {
            throw std::runtime_error("injected reload exception");
        });
    assetManager.Reload(guid, assetDatabase);
    for (int iteration = 0; iteration < 200000; ++iteration) {
        if (failedHandle.IsReady() &&
            failedHandle.GetFailureReason() == "asset loader threw: injected reload exception") {
            break;
        }
        std::this_thread::yield();
    }
    ASSERT_TRUE(failedHandle.IsReady());
    EXPECT_EQ(failedHandle.GetFailureReason(), "asset loader threw: injected reload exception");
    const BlobAsset* const preserved = failedHandle.TryGet();
    ASSERT_NE(preserved, nullptr);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(preserved->data()), preserved->size()), text);

    std::filesystem::remove(path);
}

TEST_F(AssetManagerTest, Reload_FailurePreservesLastKnownGoodDataAndReportsReason) {
    const std::filesystem::path path = "uve_asset_manager_tests_last_known_good.uvblob";
    std::filesystem::remove(path);
    const std::string text = "last known good";
    const auto* const textBytes = reinterpret_cast<const std::byte*>(text.data());
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Blob, std::vector<std::byte>(textBytes, textBytes + text.size())));
    const AssetGuid guid = assetDatabase.Register(path);
    const AssetHandle<BlobAsset> handle = assetManager.Load<BlobAsset>(guid, assetDatabase);
    ASSERT_TRUE(WaitForTerminalState(handle));
    ASSERT_TRUE(handle.IsReady());

    std::filesystem::remove(path);
    assetManager.Reload(guid, assetDatabase);
    for (int iteration = 0; iteration < 200000; ++iteration) {
        if (handle.IsReady() && handle.GetFailureReason() == "asset file does not exist") {
            break;
        }
        std::this_thread::yield();
    }

    ASSERT_TRUE(handle.IsReady());
    EXPECT_FALSE(handle.HasFailed());
    const BlobAsset* const blob = handle.TryGet();
    ASSERT_NE(blob, nullptr);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(blob->data()), blob->size()), text);
    EXPECT_EQ(handle.GetFailureReason(), "asset file does not exist");
    EXPECT_EQ(assetManager.GetLoadedAssetCount(), 1U);
}

TEST_F(AssetManagerTest, LoadCompletion_PublishesAssetLoadCompletedEvent) {
    const std::filesystem::path path = "uve_asset_manager_tests_event.uvblob";
    std::filesystem::remove(path);
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Blob, {}));
    const AssetGuid guid = assetDatabase.Register(path);

    std::atomic<bool> received{false};
    std::atomic<bool> receivedSuccess{false};
    eventSystem.Subscribe<AssetLoadCompletedEvent>([&](const AssetLoadCompletedEvent& event) {
        if (event.guid == guid) {
            received = true;
            receivedSuccess = event.success;
        }
    });

    const AssetHandle<BlobAsset> handle = assetManager.Load<BlobAsset>(guid, assetDatabase);
    ASSERT_TRUE(WaitForTerminalState(handle));

    for (int iteration = 0; iteration < 200000 && !received.load(); ++iteration) {
        eventSystem.DispatchQueued();
        std::this_thread::yield();
    }

    EXPECT_TRUE(received.load());
    EXPECT_TRUE(receivedSuccess.load());

    std::filesystem::remove(path);
}

} // namespace
} // namespace UVE::Asset::Tests

namespace UVE::Asset::Tests {
namespace {
TEST_F(AssetManagerTest, FiniteBudgetPinsPayloadAndRetriesAfterRelease) {
    const auto a = assetDatabase.Register("budget-a");
    const auto b = assetDatabase.Register("budget-b");
    std::atomic<int> calls{0};
    assetManager.RegisterLoader<BlobAsset>([&](const auto&, auto& v) {
        ++calls; v.resize(64); return true;
    }, [](const auto& v) { return v.capacity(); });
    assetManager.SetResidentBudget(64);
    EXPECT_EQ(calls.load(), 0);
    std::optional<AssetHandle<BlobAsset>> first(assetManager.Load<BlobAsset>(a,assetDatabase));
    ASSERT_TRUE(WaitForTerminalState(*first)); ASSERT_TRUE(first->IsReady());
    {
        const auto duplicate = assetManager.Load<BlobAsset>(a,assetDatabase);
        EXPECT_EQ(duplicate.TryGet(),first->TryGet()); EXPECT_EQ(calls.load(),1);
    }
    {
        auto second = assetManager.Load<BlobAsset>(b,assetDatabase);
        ASSERT_TRUE(WaitForTerminalState(second)); EXPECT_TRUE(second.HasFailed());
        EXPECT_NE(second.GetFailureReason().find("budget exceeded"),std::string::npos);
        EXPECT_TRUE(first->IsReady()); EXPECT_EQ(assetManager.GetResidentBytes(),64U);
    }
    first.reset(); assetManager.CollectGarbage(); EXPECT_EQ(assetManager.GetResidentBytes(),0U);
    auto retry = assetManager.Load<BlobAsset>(b,assetDatabase);
    ASSERT_TRUE(WaitForTerminalState(retry)); EXPECT_TRUE(retry.IsReady());
    EXPECT_EQ(assetManager.GetPeakResidentBytes(),64U);
}
TEST_F(AssetManagerTest, ConcurrentAdmissionsStayWithinBudgetAndReloadPreservesOldData) {
    assetManager.RegisterLoader<BlobAsset>([](const auto&, auto& v) {v.resize(32); return true;},
        [](const auto& v) {return v.capacity();});
    assetManager.SetResidentBudget(64);
    std::vector<AssetHandle<BlobAsset>> handles;
    for (unsigned i=0;i<12;++i) handles.push_back(assetManager.Load<BlobAsset>(
        assetDatabase.Register("budget-"+std::to_string(i)),assetDatabase));
    unsigned ready=0;
    for (auto& h:handles) {ASSERT_TRUE(WaitForTerminalState(h)); if(h.IsReady()) ++ready;}
    EXPECT_EQ(ready,2U); EXPECT_EQ(assetManager.GetResidentBytes(),64U);
    auto it=std::find_if(handles.begin(),handles.end(),[](const auto& h){return h.IsReady();});
    ASSERT_NE(it,handles.end());
    const auto* old=it->TryGet();
    assetManager.RegisterLoader<BlobAsset>([](const auto&,auto& v){v.resize(96);return true;},
        [](const auto& v){return v.capacity();});
    assetManager.Reload(it->GetGuid(),assetDatabase);
    ASSERT_TRUE(WaitForTerminalState(*it)); EXPECT_TRUE(it->IsReady()); EXPECT_EQ(it->TryGet(),old);
    EXPECT_EQ(assetManager.GetResidentBytes(),64U);
}
TEST_F(AssetManagerTest, FiniteBudgetRefusesUnknownSizerAndThrowingSizer) {
    assetManager.SetResidentBudget(1024);
    auto guid=assetDatabase.Register("budget-unknown");
    assetManager.RegisterLoader<BlobAsset>([](const auto&,auto& v){v.resize(32);return true;});
    auto h=assetManager.Load<BlobAsset>(guid,assetDatabase);
    ASSERT_TRUE(WaitForTerminalState(h)); EXPECT_TRUE(h.HasFailed());
    EXPECT_EQ(assetManager.GetResidentBytes(),0U);
    assetManager.RegisterLoader<BlobAsset>([](const auto&,auto& v){v.resize(32);return true;},
        [](const BlobAsset&)->std::size_t{throw std::runtime_error("sizing failed");});
    assetManager.Reload(guid,assetDatabase);
    ASSERT_TRUE(WaitForTerminalState(h)); EXPECT_TRUE(h.HasFailed());
    EXPECT_NE(h.GetFailureReason().find("callback failed"),std::string::npos);
}
} // namespace UVE::Asset::Tests

} // namespace
