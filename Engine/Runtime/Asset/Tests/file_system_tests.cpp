// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "file_system.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "asset_bundle.h"
#include "log_sink.h"
#include "logger.h"

namespace UVE::Asset::Tests {
namespace {

void WriteFixtureFile(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream file(path, std::ios::binary);
    ASSERT_TRUE(file.is_open());
    file << contents;
}

[[nodiscard]] std::string ReadRealFileAsString(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

[[nodiscard]] std::string ToString(const std::vector<std::byte>& bytes) {
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

[[nodiscard]] std::vector<std::byte> ToBytes(std::string_view text) {
    const auto* const bytes = reinterpret_cast<const std::byte*>(text.data());
    return std::vector<std::byte>(bytes, bytes + text.size());
}

class FileSystemTest : public ::testing::Test {
protected:
    AssetBundle assetBundle;
    FileSystem fileSystem{assetBundle};
};

TEST_F(FileSystemTest, ReadFile_DirectoryMount_ReturnsRealFileBytes) {
    const std::filesystem::path dir = "uve_file_system_tests_basic";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    WriteFixtureFile(dir / "hello.txt", "hello vfs");

    fileSystem.MountDirectory("", dir, 0);

    const std::optional<std::vector<std::byte>> data = fileSystem.ReadFile("hello.txt");
    ASSERT_TRUE(data.has_value());
    EXPECT_EQ(ToString(*data), "hello vfs");

    std::filesystem::remove_all(dir);
}

TEST_F(FileSystemTest, ReadFile_TwoDirectoryMountsSamePrefix_HigherPriorityWins) {
    const std::filesystem::path lowDir = "uve_file_system_tests_low";
    const std::filesystem::path highDir = "uve_file_system_tests_high";
    std::filesystem::remove_all(lowDir);
    std::filesystem::remove_all(highDir);
    std::filesystem::create_directories(lowDir);
    std::filesystem::create_directories(highDir);
    WriteFixtureFile(lowDir / "a.txt", "low priority content");
    WriteFixtureFile(highDir / "a.txt", "high priority content");

    fileSystem.MountDirectory("", lowDir, 0);
    fileSystem.MountDirectory("", highDir, 10);

    const std::optional<std::vector<std::byte>> data = fileSystem.ReadFile("a.txt");
    ASSERT_TRUE(data.has_value());
    EXPECT_EQ(ToString(*data), "high priority content");

    std::filesystem::remove_all(lowDir);
    std::filesystem::remove_all(highDir);
}

TEST_F(FileSystemTest, HasFile_PrefixMatchesOnSegmentBoundaryOnly) {
    const std::filesystem::path dir = "uve_file_system_tests_tex";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    WriteFixtureFile(dir / "rock.uvtex", "data");

    fileSystem.MountDirectory("tex", dir, 0);

    EXPECT_FALSE(fileSystem.HasFile("textures/rock.uvtex")); // "tex" must not match "textures/..."
    EXPECT_TRUE(fileSystem.HasFile("tex/rock.uvtex"));

    std::filesystem::remove_all(dir);
}

TEST_F(FileSystemTest, ReadFile_BundleMount_ReturnsBundledBytes) {
    const std::filesystem::path source = "uve_file_system_tests_bundle_source.txt";
    WriteFixtureFile(source, "bundled content");
    const std::filesystem::path bundlePath = "uve_file_system_tests_basic.uvbundle";
    std::filesystem::remove(bundlePath);

    AssetBundleEntry entry;
    entry.guid = AssetGuid{4001};
    entry.sourcePath = source;
    entry.virtualName = "meshes/cube.uvmodel";
    ASSERT_TRUE(assetBundle.Pack({entry}, bundlePath));

    fileSystem.MountBundle("", bundlePath, 0);

    const std::optional<std::vector<std::byte>> data = fileSystem.ReadFile("meshes/cube.uvmodel");
    ASSERT_TRUE(data.has_value());
    EXPECT_EQ(ToString(*data), "bundled content");

    std::filesystem::remove(source);
    std::filesystem::remove(bundlePath);
}

TEST_F(FileSystemTest, ReadFile_DirectoryMountOverridesBundleMount_WhenHigherPriority) {
    const std::filesystem::path looseDir = "uve_file_system_tests_override_loose";
    std::filesystem::remove_all(looseDir);
    std::filesystem::create_directories(looseDir);
    WriteFixtureFile(looseDir / "shared.txt", "loose override");

    const std::filesystem::path bundleSource = "uve_file_system_tests_override_bundle_source.txt";
    WriteFixtureFile(bundleSource, "bundle content");
    const std::filesystem::path bundlePath = "uve_file_system_tests_override.uvbundle";
    std::filesystem::remove(bundlePath);
    AssetBundleEntry entry;
    entry.guid = AssetGuid{5001};
    entry.sourcePath = bundleSource;
    entry.virtualName = "shared.txt";
    ASSERT_TRUE(assetBundle.Pack({entry}, bundlePath));

    fileSystem.MountBundle("", bundlePath, 0);
    fileSystem.MountDirectory("", looseDir, 10);

    const std::optional<std::vector<std::byte>> data = fileSystem.ReadFile("shared.txt");
    ASSERT_TRUE(data.has_value());
    EXPECT_EQ(ToString(*data), "loose override");

    std::filesystem::remove_all(looseDir);
    std::filesystem::remove(bundleSource);
    std::filesystem::remove(bundlePath);
}

TEST_F(FileSystemTest, ReadFile_BundleMountOverridesDirectoryMount_WhenHigherPriority) {
    const std::filesystem::path looseDir = "uve_file_system_tests_underride_loose";
    std::filesystem::remove_all(looseDir);
    std::filesystem::create_directories(looseDir);
    WriteFixtureFile(looseDir / "shared.txt", "loose content");

    const std::filesystem::path bundleSource = "uve_file_system_tests_underride_bundle_source.txt";
    WriteFixtureFile(bundleSource, "bundle wins content");
    const std::filesystem::path bundlePath = "uve_file_system_tests_underride.uvbundle";
    std::filesystem::remove(bundlePath);
    AssetBundleEntry entry;
    entry.guid = AssetGuid{5002};
    entry.sourcePath = bundleSource;
    entry.virtualName = "shared.txt";
    ASSERT_TRUE(assetBundle.Pack({entry}, bundlePath));

    fileSystem.MountDirectory("", looseDir, 0);
    fileSystem.MountBundle("", bundlePath, 10);

    const std::optional<std::vector<std::byte>> data = fileSystem.ReadFile("shared.txt");
    ASSERT_TRUE(data.has_value());
    EXPECT_EQ(ToString(*data), "bundle wins content");

    std::filesystem::remove_all(looseDir);
    std::filesystem::remove(bundleSource);
    std::filesystem::remove(bundlePath);
}

TEST_F(FileSystemTest, WriteFile_WritesIntoHighestPriorityDirectoryMount) {
    const std::filesystem::path lowDir = "uve_file_system_tests_write_low";
    const std::filesystem::path highDir = "uve_file_system_tests_write_high";
    std::filesystem::remove_all(lowDir);
    std::filesystem::remove_all(highDir);
    std::filesystem::create_directories(lowDir);
    std::filesystem::create_directories(highDir);

    fileSystem.MountDirectory("", lowDir, 0);
    fileSystem.MountDirectory("", highDir, 10);

    ASSERT_TRUE(fileSystem.WriteFile("out/data.txt", ToBytes("written content")));

    EXPECT_TRUE(std::filesystem::exists(highDir / "out" / "data.txt"));
    EXPECT_FALSE(std::filesystem::exists(lowDir / "out" / "data.txt"));
    EXPECT_EQ(ReadRealFileAsString(highDir / "out" / "data.txt"), "written content");

    std::filesystem::remove_all(lowDir);
    std::filesystem::remove_all(highDir);
}

TEST_F(FileSystemTest, WriteFile_TraversalPath_ReturnsFalseWithoutEscapingMount) {
    const std::filesystem::path directory = "uve_file_system_tests_traversal";
    const std::filesystem::path escapedPath = "uve_file_system_tests_escaped.txt";
    std::filesystem::remove_all(directory);
    std::filesystem::remove(escapedPath);
    std::filesystem::create_directories(directory);
    fileSystem.MountDirectory("", directory, 0);

    EXPECT_FALSE(fileSystem.WriteFile("../uve_file_system_tests_escaped.txt", ToBytes("blocked")));
    EXPECT_FALSE(fileSystem.WriteFile("..\\\\uve_file_system_tests_escaped.txt", ToBytes("blocked")));
    EXPECT_FALSE(std::filesystem::exists(escapedPath));

    std::filesystem::remove_all(directory);
}

TEST_F(FileSystemTest, WriteFile_OnlyBundleMountMatches_ReturnsFalseAndLogsError) {
    const std::filesystem::path source = "uve_file_system_tests_write_bundle_source.txt";
    WriteFixtureFile(source, "data");
    const std::filesystem::path bundlePath = "uve_file_system_tests_write.uvbundle";
    std::filesystem::remove(bundlePath);
    AssetBundleEntry entry;
    entry.guid = AssetGuid{6001};
    entry.sourcePath = source;
    entry.virtualName = "shared.txt";
    ASSERT_TRUE(assetBundle.Pack({entry}, bundlePath));
    fileSystem.MountBundle("", bundlePath, 0);

    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    EXPECT_FALSE(fileSystem.WriteFile("shared.txt", ToBytes("attempted write")));

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Error &&
                   message.message.contains("no writable");
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
    std::filesystem::remove(source);
    std::filesystem::remove(bundlePath);
}

TEST_F(FileSystemTest, Unmount_RemovesMount_SubsequentReadFails) {
    const std::filesystem::path dir = "uve_file_system_tests_unmount";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    WriteFixtureFile(dir / "f.txt", "data");

    const MountHandle handle = fileSystem.MountDirectory("", dir, 0);
    ASSERT_TRUE(fileSystem.HasFile("f.txt"));

    fileSystem.Unmount(handle);
    EXPECT_FALSE(fileSystem.HasFile("f.txt"));

    std::filesystem::remove_all(dir);
}

TEST_F(FileSystemTest, ResolveRealPath_DirectoryMount_ReturnsRealPathAndEmptyWhenUnresolved) {
    const std::filesystem::path dir = "uve_file_system_tests_resolve";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    WriteFixtureFile(dir / "f.txt", "data");
    fileSystem.MountDirectory("", dir, 0);

    EXPECT_EQ(fileSystem.ResolveRealPath("f.txt"), dir / "f.txt");
    EXPECT_TRUE(fileSystem.ResolveRealPath("missing.txt").empty());

    std::filesystem::remove_all(dir);
}

TEST_F(FileSystemTest, ResolveRealPath_BundleBackedVirtualPath_ReturnsEmptyPath) {
    const std::filesystem::path source = "uve_file_system_tests_resolve_bundle_source.txt";
    WriteFixtureFile(source, "data");
    const std::filesystem::path bundlePath = "uve_file_system_tests_resolve.uvbundle";
    std::filesystem::remove(bundlePath);
    AssetBundleEntry entry;
    entry.guid = AssetGuid{7001};
    entry.sourcePath = source;
    entry.virtualName = "shared.txt";
    ASSERT_TRUE(assetBundle.Pack({entry}, bundlePath));
    fileSystem.MountBundle("", bundlePath, 0);

    EXPECT_TRUE(fileSystem.ResolveRealPath("shared.txt").empty());

    std::filesystem::remove(source);
    std::filesystem::remove(bundlePath);
}

TEST_F(FileSystemTest, ReadFile_NoMountResolves_ReturnsNulloptAndLogsError) {
    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    EXPECT_FALSE(fileSystem.ReadFile("nonexistent.txt").has_value());

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Error &&
                   message.message.contains("no mount resolves");
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
}

TEST_F(FileSystemTest, AssetBundleUnpack_CreatesNestedParentDirectories) {
    const std::filesystem::path source = "uve_file_system_tests_unpack_nested_src.txt";
    const std::filesystem::path bundlePath = "uve_file_system_tests_unpack_nested.uvbundle";
    const std::filesystem::path outDir = "uve_file_system_tests_unpack_nested_out";
    std::filesystem::remove_all(outDir);
    std::filesystem::remove(bundlePath);
    WriteFixtureFile(source, "nested bundle bytes");

    AssetBundleEntry entry;
    entry.guid = AssetGuid{8001};
    entry.sourcePath = source;
    entry.virtualName = "sub/dir/file.txt";
    ASSERT_TRUE(assetBundle.Pack({entry}, bundlePath));
    ASSERT_TRUE(assetBundle.Unpack(bundlePath, outDir));

    const std::filesystem::path unpacked = outDir / "sub" / "dir" / "file.txt";
    ASSERT_TRUE(std::filesystem::exists(unpacked));
    EXPECT_EQ(ReadRealFileAsString(unpacked), "nested bundle bytes");

    std::filesystem::remove_all(outDir);
    std::filesystem::remove(source);
    std::filesystem::remove(bundlePath);
}

} // namespace
} // namespace UVE::Asset::Tests
