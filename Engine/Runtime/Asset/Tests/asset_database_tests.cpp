// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "asset_database.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include "log_sink.h"
#include "logger.h"

namespace UVE::Asset::Tests {
namespace {

void WriteFixtureFile(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream file(path);
    ASSERT_TRUE(file.is_open());
    file << contents;
}

class CurrentWorkingDirectoryGuard final {
public:
    CurrentWorkingDirectoryGuard() : m_originalPath(std::filesystem::current_path()) {}

    ~CurrentWorkingDirectoryGuard() {
        std::error_code errorCode;
        std::filesystem::current_path(m_originalPath, errorCode);
    }

    CurrentWorkingDirectoryGuard(const CurrentWorkingDirectoryGuard&) = delete;
    CurrentWorkingDirectoryGuard& operator=(const CurrentWorkingDirectoryGuard&) = delete;

private:
    std::filesystem::path m_originalPath;
};

TEST(AssetDatabaseTest, GetRegisteredAssets_EmptyDatabase_ReturnsEmptySnapshot) {
    AssetDatabase database;
    EXPECT_TRUE(database.GetRegisteredAssets().empty());
}

TEST(AssetDatabaseTest, GetRegisteredAssets_SortsLexicallyThenByGuid) {
    const std::filesystem::path fixturePath = "uve_asset_database_tests_snapshot_sort.uvassetdb";
    WriteFixtureFile(
        fixturePath,
        R"({
            "0000000000000020": "assets/same.uvprefab",
            "0000000000000010": "assets/same.uvprefab",
            "0000000000000030": "assets/zebra.uvprefab",
            "0000000000000040": "assets/apple.uvprefab"
        })");

    AssetDatabase database;
    ASSERT_TRUE(database.Load(fixturePath));
    const std::vector<AssetRecord> records = database.GetRegisteredAssets();

    ASSERT_EQ(records.size(), 4U);
    EXPECT_EQ(records[0].path, std::filesystem::path("assets/apple.uvprefab"));
    EXPECT_EQ(records[0].guid, AssetGuid{0x40U});
    EXPECT_EQ(records[1].path, std::filesystem::path("assets/same.uvprefab"));
    EXPECT_EQ(records[1].guid, AssetGuid{0x10U});
    EXPECT_EQ(records[2].path, std::filesystem::path("assets/same.uvprefab"));
    EXPECT_EQ(records[2].guid, AssetGuid{0x20U});
    EXPECT_EQ(records[3].path, std::filesystem::path("assets/zebra.uvprefab"));
    EXPECT_EQ(records[3].guid, AssetGuid{0x30U});

    std::filesystem::remove(fixturePath);
}

TEST(AssetDatabaseTest, GetRegisteredAssets_ReturnsSnapshotIsolatedFromLaterRegistration) {
    AssetDatabase database;
    const AssetGuid initialGuid = database.Register("assets/initial.uvprefab");
    const std::vector<AssetRecord> snapshot = database.GetRegisteredAssets();

    static_cast<void>(database.Register("assets/later.uvprefab"));

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot.front().guid, initialGuid);
    EXPECT_EQ(snapshot.front().path, std::filesystem::path("assets/initial.uvprefab"));
    EXPECT_EQ(database.GetRegisteredAssets().size(), 2U);
}

TEST(AssetDatabaseTest, Register_SamePathTwice_ReturnsSameGuid) {
    AssetDatabase database;
    const AssetGuid first = database.Register("meshes/cube.uvmodel");
    const AssetGuid second = database.Register("meshes/cube.uvmodel");
    EXPECT_EQ(first, second);
}

TEST(AssetDatabaseTest, Relocate_KeepsTheGuidAtTheNewPath) {
    AssetDatabase db;
    const AssetGuid run = db.Register("anims/Run.uvanim");
    const AssetGuid walk = db.Register("anims/Walk.uvanim");
    ASSERT_TRUE(db.Relocate(run, "anims/Sprint.uvanim"));
    EXPECT_EQ(db.Resolve(run), std::filesystem::path("anims/Sprint.uvanim"));
    EXPECT_EQ(db.Register("anims/Sprint.uvanim"), run) << "the new path is the same asset";
    EXPECT_NE(db.Register("anims/Run.uvanim"), run) << "the old path is free again";
    EXPECT_FALSE(db.Relocate(walk, "anims/Sprint.uvanim")) << "taken by another asset";
    EXPECT_FALSE(db.Relocate(AssetGuid{12345U}, "anims/X.uvanim")) << "unknown guid";
}

TEST(AssetDatabaseTest, RelocateMovedContent_RepointsEntriesToTheCurrentContentFolder) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "uve_asset_db_relocate_test";
    std::filesystem::remove_all(root);
    const std::filesystem::path content = root / "NewHome" / "Content";
    std::filesystem::create_directories(content / "Props");
    { std::ofstream(content / "Props" / "crate.uventity") << "x"; }
    { std::ofstream(content / "Props" / "taken.uventity") << "x"; }

    AssetDatabase database;
    // An absolute path that does not exist, as the project's old home would not any more.
    const std::filesystem::path oldContent = root / "OldHome" / "MyGame" / "Content";
    // Registered while the project lived somewhere else: gone from there, present under today's Content.
    const AssetGuid moved = database.Register(oldContent / "Props" / "crate.uventity");
    // A relative entry belongs to its own package root, never to the process directory.
    const AssetGuid relative = database.Register("Content/Props/crate.uventity");
    // The file at its current place already belongs to another GUID: this one is left alone.
    const AssetGuid owner = database.Register(content / "Props" / "taken.uventity");
    const AssetGuid duplicate = database.Register(oldContent / "Props" / "taken.uventity");

    EXPECT_EQ(database.RelocateMovedContent(content), 1U);
    EXPECT_EQ(database.Resolve(moved), (content / "Props" / "crate.uventity").lexically_normal());
    EXPECT_EQ(database.Resolve(relative), std::filesystem::path{"Content/Props/crate.uventity"});
    EXPECT_EQ(database.Resolve(owner), (content / "Props" / "taken.uventity").lexically_normal());
    EXPECT_EQ(database.Resolve(duplicate), (oldContent / "Props" / "taken.uventity").lexically_normal());
    EXPECT_EQ(database.Register(content / "Props" / "crate.uventity"), moved)
        << "the new path now maps back to the same GUID";
    EXPECT_EQ(database.RelocateMovedContent(content), 0U) << "nothing left to move";
    std::filesystem::remove_all(root);
}

TEST(AssetDatabaseTest, Register_DifferentPaths_ReturnDifferentGuids) {
    AssetDatabase database;
    const AssetGuid first = database.Register("meshes/cube.uvmodel");
    const AssetGuid second = database.Register("meshes/sphere.uvmodel");
    EXPECT_NE(first, second);
}

TEST(AssetDatabaseTest, Register_EquivalentLexicalPaths_ReturnSameGuidAndNormalizedStoredPath) {
    AssetDatabase database;
    const AssetGuid first = database.Register("meshes/generated/../cube.uvmodel");
    const AssetGuid second = database.Register("meshes/cube.uvmodel");

    EXPECT_EQ(first, second);
    EXPECT_EQ(database.Resolve(first), std::filesystem::path("meshes/cube.uvmodel"));
    const std::vector<AssetRecord> records = database.GetRegisteredAssets();
    ASSERT_EQ(records.size(), 1U);
    EXPECT_EQ(records.front().path, std::filesystem::path("meshes/cube.uvmodel"));
}

TEST(AssetDatabaseTest, Register_StableRelativePath_IsIndependentOfCurrentWorkingDirectory) {
    std::error_code errorCode;
    const std::filesystem::path fixtureRoot =
        std::filesystem::absolute("uve_asset_database_tests_working_directory", errorCode);
    ASSERT_FALSE(errorCode);
    const std::filesystem::path firstWorkingDirectory = fixtureRoot / "first";
    const std::filesystem::path secondWorkingDirectory = fixtureRoot / "second";
    std::filesystem::remove_all(fixtureRoot);
    std::filesystem::create_directories(firstWorkingDirectory);
    std::filesystem::create_directories(secondWorkingDirectory);

    {
        CurrentWorkingDirectoryGuard workingDirectoryGuard;
        errorCode.clear();
        AssetDatabase database;
        std::filesystem::current_path(firstWorkingDirectory, errorCode);
        ASSERT_FALSE(errorCode);
        const AssetGuid firstGuid = database.Register("assets/tree.uvprefab");

        std::filesystem::current_path(secondWorkingDirectory, errorCode);
        ASSERT_FALSE(errorCode);
        const AssetGuid secondGuid = database.Register("assets/tree.uvprefab");

        EXPECT_EQ(firstGuid, secondGuid);
        EXPECT_EQ(database.Resolve(firstGuid), std::filesystem::path("assets/tree.uvprefab"));
    }
    std::filesystem::remove_all(fixtureRoot);
}

TEST(AssetDatabaseTest, Resolve_RoundTripsRegisteredPath) {
    AssetDatabase database;
    const AssetGuid guid = database.Register("prefabs/tree.uvprefab");
    EXPECT_EQ(database.Resolve(guid), std::filesystem::path("prefabs/tree.uvprefab"));
}

TEST(AssetDatabaseTest, Resolve_UnknownGuid_ReturnsEmptyPath) {
    AssetDatabase database;
    EXPECT_TRUE(database.Resolve(AssetGuid{12345}).empty());
}

TEST(AssetDatabaseTest, HasGuid_TrueForRegisteredFalseOtherwise) {
    AssetDatabase database;
    const AssetGuid guid = database.Register("prefabs/rock.uvprefab");
    EXPECT_TRUE(database.HasGuid(guid));
    EXPECT_FALSE(database.HasGuid(AssetGuid{999}));
    EXPECT_FALSE(database.HasGuid(kInvalidAssetGuid));
}

TEST(AssetDatabaseTest, Load_LegacyEquivalentPathAliases_UsesSmallestGuidForFutureRegistration) {
    const std::filesystem::path fixturePath = "uve_asset_database_tests_legacy_aliases.uvassetdb";
    WriteFixtureFile(
        fixturePath,
        R"({
            "0000000000000020": "assets/generated/../tree.uvprefab",
            "0000000000000010": "assets/tree.uvprefab"
        })");

    AssetDatabase database;
    ASSERT_TRUE(database.Load(fixturePath));
    EXPECT_EQ(database.Register("assets/tree.uvprefab"), AssetGuid{0x10U});
    EXPECT_EQ(database.Register("assets/./tree.uvprefab"), AssetGuid{0x10U});

    const std::vector<AssetRecord> records = database.GetRegisteredAssets();
    ASSERT_EQ(records.size(), 2U);
    EXPECT_EQ(records[0].path, std::filesystem::path("assets/tree.uvprefab"));
    EXPECT_EQ(records[1].path, std::filesystem::path("assets/tree.uvprefab"));
    std::filesystem::remove(fixturePath);
}

TEST(AssetDatabaseTest, SaveThenLoad_RoundTripsThroughDisk) {
    const std::filesystem::path savePath = "uve_asset_database_tests_roundtrip.uvassetdb";
    std::filesystem::remove(savePath);

    AssetGuid guid{};
    {
        AssetDatabase writer;
        guid = writer.Register("prefabs/tree.uvprefab");
        ASSERT_TRUE(writer.Save(savePath));
    }

    AssetDatabase reader;
    ASSERT_TRUE(reader.Load(savePath));
    EXPECT_EQ(reader.Resolve(guid), std::filesystem::path("prefabs/tree.uvprefab"));

    std::filesystem::remove(savePath);
}

TEST(AssetDatabaseTest, Load_MissingFile_LogsWarning) {
    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    AssetDatabase database;
    EXPECT_FALSE(database.Load("uve_asset_database_tests_nonexistent.uvassetdb"));

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundWarning =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Warning &&
                   message.message.contains("not found");
        });
    EXPECT_TRUE(foundWarning);

    logger.Shutdown();
}

TEST(AssetDatabaseTest, Load_MalformedJson_PreservesPreviousRegistryAndSaveTarget) {
    const std::filesystem::path previousPath = "uve_asset_database_tests_previous_target.uvassetdb";
    const std::filesystem::path malformedPath = "uve_asset_database_tests_malformed_target.uvassetdb";
    std::filesystem::remove(previousPath);
    std::filesystem::remove(malformedPath);

    AssetDatabase database;
    const AssetGuid stableGuid = database.Register("assets/stable.uvasset");
    ASSERT_TRUE(database.Save(previousPath));
    WriteFixtureFile(malformedPath, "{ not valid json");

    EXPECT_FALSE(database.Load(malformedPath));
    EXPECT_TRUE(database.HasGuid(stableGuid));
    const AssetGuid laterGuid = database.Register("assets/later.uvasset");
    ASSERT_TRUE(database.Save());

    AssetDatabase reader;
    ASSERT_TRUE(reader.Load(previousPath));
    EXPECT_TRUE(reader.HasGuid(stableGuid));
    EXPECT_TRUE(reader.HasGuid(laterGuid));
    EXPECT_EQ(std::ifstream(malformedPath).peek(), std::char_traits<char>::to_int_type('{'));

    std::filesystem::remove(previousPath);
    std::filesystem::remove(malformedPath);
}

TEST(AssetDatabaseTest, Load_MalformedJson_ReturnsFalseAndLogsError) {
    const std::filesystem::path fixturePath = "uve_asset_database_tests_malformed.uvassetdb";
    WriteFixtureFile(fixturePath, "{ not valid json");

    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    AssetDatabase database;
    EXPECT_FALSE(database.Load(fixturePath));

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundParseError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Error &&
                   message.message.contains("failed to parse");
        });
    EXPECT_TRUE(foundParseError);

    logger.Shutdown();
    std::filesystem::remove(fixturePath);
}

} // namespace
} // namespace UVE::Asset::Tests
