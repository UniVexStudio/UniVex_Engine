// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "asset_importer.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "asset_database.h"
#include "shader_asset.h"
#include "log_sink.h"
#include "logger.h"

namespace UVE::Asset::Tests {
namespace {

void WriteFixtureFile(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream file(path, std::ios::binary);
    ASSERT_TRUE(file.is_open());
    file << contents;
}

[[nodiscard]] std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

class AssetImporterTest : public ::testing::Test {
protected:
    AssetImporter importer;
    AssetDatabase assetDatabase;
};

class RejectingAssetDatabase final : public IAssetDatabase {
public:
    bool Load(const std::filesystem::path&) override { return false; }
    bool Save() override { return false; }
    bool Save(const std::filesystem::path&) override { return false; }
    [[nodiscard]] AssetGuid Register(const std::filesystem::path&) override {
        ++registerCount;
        return kInvalidAssetGuid;
    }
    [[nodiscard]] std::filesystem::path Resolve(AssetGuid) const override { return {}; }
    [[nodiscard]] bool HasGuid(AssetGuid) const override { return false; }
    [[nodiscard]] std::vector<AssetRecord> GetRegisteredAssets() const override { return {}; }

    int registerCount = 0;
};

TEST_F(AssetImporterTest, ClassifySource_ReportsAuthorityAndRawParserBoundary) {
    struct ClassificationCase {
        std::string_view path;
        AssetImportSourceKind kind;
        std::string_view normalizedExtension;
        bool importerRegistered;
        bool requiresFormatSpecificParser;
        std::string_view diagnostic;
    };

    constexpr std::array<ClassificationCase, 15> kCases = {{
        {"Readme.TXT", AssetImportSourceKind::PlainText, "txt", true, false,
         "built-in text parser is registered"},
        {"Character.UVMODEL", AssetImportSourceKind::MeshEnvelope, "uvmodel", true, false,
         "built-in generic copy importer is registered"},
        {"Walk.UVANIM", AssetImportSourceKind::AnimationEnvelope, "uvanim", true, false,
         "built-in generic copy importer is registered"},
        {"Character.FBX", AssetImportSourceKind::RawModel, "fbx", true, true,
         "format-specific parser is registered"},
        {"Character.DAE", AssetImportSourceKind::RawModel, "dae", false, true,
         "format-specific parser is not registered"},
        {"Character.OBJ", AssetImportSourceKind::RawModel, "obj", true, true,
         "format-specific parser is registered"},
        {"Albedo.PNG", AssetImportSourceKind::RawTexture, "png", true, true,
         "format-specific parser is registered"},
        {"Albedo.BMP", AssetImportSourceKind::RawTexture, "bmp", true, true,
         "format-specific parser is registered"},
        {"Albedo.TGA", AssetImportSourceKind::RawTexture, "tga", true, true,
         "format-specific parser is registered"},
        {"Surface.MTL", AssetImportSourceKind::RawMaterial, "mtl", true, true,
         "format-specific parser is registered"},
        {"Main.VERT", AssetImportSourceKind::RawShader, "vert", true, true,
         "format-specific parser is registered"},
        {"Main.FRAG", AssetImportSourceKind::RawShader, "frag", true, true,
         "format-specific parser is registered"},
        {"Compute.COMP", AssetImportSourceKind::RawShader, "comp", true, true,
         "format-specific parser is registered"},
        {"Music.OGG", AssetImportSourceKind::RawAudio, "ogg", false, true,
         "format-specific parser is not registered"},
        {"Notes.UNKNOWN", AssetImportSourceKind::Unknown, "unknown", false, false,
         "unsupported source extension"},
    }};

    for (const ClassificationCase& expected : kCases) {
        const AssetImportSourceClassification actual = importer.ClassifySource(expected.path);
        EXPECT_EQ(actual.kind, expected.kind) << expected.path;
        EXPECT_EQ(actual.normalizedExtension, expected.normalizedExtension) << expected.path;
        EXPECT_EQ(actual.importerRegistered, expected.importerRegistered) << expected.path;
        EXPECT_EQ(actual.requiresFormatSpecificParser, expected.requiresFormatSpecificParser) << expected.path;
        EXPECT_EQ(actual.diagnostic, expected.diagnostic) << expected.path;
    }

    importer.RegisterImporter("custom", [](const std::filesystem::path&, const std::filesystem::path&,
                                               const AssetImportSettings&) { return true; });
    const AssetImportSourceClassification custom = importer.ClassifySource("asset.custom");
    EXPECT_EQ(custom.kind, AssetImportSourceKind::Unknown);
    EXPECT_TRUE(custom.importerRegistered);
    EXPECT_FALSE(custom.requiresFormatSpecificParser);
    EXPECT_EQ(custom.diagnostic, "custom importer is registered without built-in classification");
}

TEST_F(AssetImporterTest, RegisterDatabaseAwareImporter_ReceivesTheDatabaseAndSharesTheKeySpace) {
    // Foundation WP7: a database-aware importer gets the same database the destination is
    // registered in afterwards, and the two registration kinds replace each other per extension.
    const std::filesystem::path source = "uve_asset_importer_tests_aware.custom";
    const std::filesystem::path destination = "uve_asset_importer_tests_aware.out";
    std::filesystem::remove(source);
    std::filesystem::remove(destination);
    WriteFixtureFile(source, "payload");

    IAssetDatabase* seenDatabase = nullptr;
    importer.RegisterDatabaseAwareImporter(
        "custom", [&seenDatabase](const std::filesystem::path& sourcePath, const std::filesystem::path& destinationPath,
                                  IAssetDatabase& db, const AssetImportSettings&) {
            seenDatabase = &db;
            std::filesystem::copy_file(sourcePath, destinationPath, std::filesystem::copy_options::overwrite_existing);
            return true;
        });
    EXPECT_TRUE(importer.ClassifySource(source).importerRegistered);
    const AssetGuid guid = importer.Import(source, destination, assetDatabase);
    ASSERT_NE(guid, kInvalidAssetGuid);
    EXPECT_EQ(seenDatabase, &assetDatabase);
    EXPECT_EQ(assetDatabase.Resolve(guid), destination);

    // A plain registration for the same extension replaces the aware one.
    bool plainCalled = false;
    importer.RegisterImporter("custom", [&plainCalled](const std::filesystem::path&, const std::filesystem::path&,
                                                          const AssetImportSettings&) {
        plainCalled = true;
        return true;
    });
    seenDatabase = nullptr;
    ASSERT_NE(importer.Import(source, destination, assetDatabase), kInvalidAssetGuid);
    EXPECT_TRUE(plainCalled);
    EXPECT_EQ(seenDatabase, nullptr);

    // Invalid aware registrations are refused without disturbing the existing importer.
    importer.RegisterDatabaseAwareImporter("custom", nullptr);
    importer.RegisterDatabaseAwareImporter("", [](const std::filesystem::path&, const std::filesystem::path&,
                                                     IAssetDatabase&, const AssetImportSettings&) { return true; });
    plainCalled = false;
    ASSERT_NE(importer.Import(source, destination, assetDatabase), kInvalidAssetGuid);
    EXPECT_TRUE(plainCalled);

    std::filesystem::remove(source);
    std::filesystem::remove(destination);
}

TEST_F(AssetImporterTest, Import_RegistrationFailureReturnsInvalidAfterSuccessfulConversion) {
    const std::filesystem::path sourcePath = "uve_asset_importer_registration_failure_source.custom";
    const std::filesystem::path destinationPath = "uve_asset_importer_registration_failure_dest.custom";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    WriteFixtureFile(sourcePath, "converted before registration failure");

    importer.RegisterImporter("custom", [](const std::filesystem::path& source,
                                                const std::filesystem::path& destination,
                                                const AssetImportSettings&) {
        std::ifstream input(source, std::ios::binary);
        std::ofstream output(destination, std::ios::binary | std::ios::trunc);
        output << input.rdbuf();
        return input.good() && output.good();
    });
    RejectingAssetDatabase rejectingDatabase;

    const AssetGuid guid = importer.Import(sourcePath, destinationPath, rejectingDatabase);

    EXPECT_EQ(guid, kInvalidAssetGuid);
    EXPECT_EQ(rejectingDatabase.registerCount, 1);
    EXPECT_TRUE(std::filesystem::exists(destinationPath));
    EXPECT_EQ(ReadFile(destinationPath), "converted before registration failure");

    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}

TEST_F(AssetImporterTest, Import_GenericImporter_CopiesFileAndRegistersGuid) {
    const std::filesystem::path sourcePath = "uve_asset_importer_tests_source.txt";
    const std::filesystem::path destinationPath = "uve_asset_importer_tests_dest.txt";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    WriteFixtureFile(sourcePath, "hello importer");

    const AssetGuid guid = importer.Import(sourcePath, destinationPath, assetDatabase);
    ASSERT_NE(guid, kInvalidAssetGuid);
    EXPECT_TRUE(std::filesystem::exists(destinationPath));
    EXPECT_EQ(ReadFile(destinationPath), "hello importer");
    EXPECT_EQ(assetDatabase.Resolve(guid), destinationPath);

    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}

TEST_F(AssetImporterTest, Import_GenericImporter_CreatesMissingDestinationDirectories) {
    const std::filesystem::path sourcePath = "uve_asset_importer_nested_source.uvmodel";
    const std::filesystem::path destinationDirectory = "uve_asset_importer_nested_destination";
    const std::filesystem::path destinationPath = destinationDirectory / "nested" / "asset.uvmodel";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove_all(destinationDirectory);
    WriteFixtureFile(sourcePath, "typed UVE envelope import");

    const AssetGuid guid = importer.Import(sourcePath, destinationPath, assetDatabase);

    ASSERT_NE(guid, kInvalidAssetGuid);
    EXPECT_EQ(ReadFile(destinationPath), "typed UVE envelope import");
    EXPECT_EQ(assetDatabase.Resolve(guid), destinationPath);

    std::filesystem::remove(sourcePath);
    std::filesystem::remove_all(destinationDirectory);
}

TEST_F(AssetImporterTest, TextImport_PreservesTextVerbatim) {
    const std::filesystem::path sourcePath = "uve_text_parser_preserve_source.txt";
    const std::filesystem::path destinationPath = "uve_text_parser_preserve_destination.txt";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    const std::string source = "first\r\nsecond\rthird\nfourth";
    WriteFixtureFile(sourcePath, source);

    const AssetGuid guid = importer.Import(sourcePath, destinationPath, assetDatabase);

    ASSERT_NE(guid, kInvalidAssetGuid);
    EXPECT_EQ(ReadFile(destinationPath), source);
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}

TEST_F(AssetImporterTest, TextImport_RejectsNulByte) {
    const std::filesystem::path sourcePath = "uve_text_parser_nul_source.txt";
    const std::filesystem::path destinationPath = "uve_text_parser_nul_destination.txt";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    WriteFixtureFile(sourcePath, std::string{"prefix\0suffix", 13U});

    const AssetGuid guid = importer.Import(sourcePath, destinationPath, assetDatabase);

    EXPECT_EQ(guid, kInvalidAssetGuid);
    EXPECT_FALSE(std::filesystem::exists(destinationPath));
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}

TEST_F(AssetImporterTest, TextImport_NormalizesLineEndingsToLF) {
    const std::filesystem::path sourcePath = "uve_text_parser_lf_source.txt";
    const std::filesystem::path destinationPath = "uve_text_parser_lf_destination.txt";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    WriteFixtureFile(sourcePath, "first\r\nsecond\rthird\nfourth");
    TextImportSettings settings;
    settings.lineEnding = TextImportLineEnding::LineFeed;

    const AssetGuid guid = importer.Import(sourcePath, destinationPath, assetDatabase, settings);

    ASSERT_NE(guid, kInvalidAssetGuid);
    EXPECT_EQ(ReadFile(destinationPath), "first\nsecond\nthird\nfourth");
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}

TEST_F(AssetImporterTest, TextImport_NormalizesLineEndingsToCRLF) {
    const std::filesystem::path sourcePath = "uve_text_parser_crlf_source.txt";
    const std::filesystem::path destinationPath = "uve_text_parser_crlf_destination.txt";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    WriteFixtureFile(sourcePath, "first\nsecond\rthird\r\nfourth");
    TextImportSettings settings;
    settings.lineEnding = TextImportLineEnding::CarriageReturnLineFeed;

    const AssetGuid guid = importer.Import(sourcePath, destinationPath, assetDatabase, settings);

    ASSERT_NE(guid, kInvalidAssetGuid);
    EXPECT_EQ(ReadFile(destinationPath), "first\r\nsecond\r\nthird\r\nfourth");
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}

TEST_F(AssetImporterTest, TextImport_EnsuresTrailingNewline) {
    const std::filesystem::path sourcePath = "uve_text_parser_trailing_source.txt";
    const std::filesystem::path destinationPath = "uve_text_parser_trailing_destination.txt";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    WriteFixtureFile(sourcePath, "text without final newline");
    TextImportSettings settings;
    settings.lineEnding = TextImportLineEnding::LineFeed;
    settings.ensureTrailingLineEnding = true;

    const AssetGuid guid = importer.Import(sourcePath, destinationPath, assetDatabase, settings);

    ASSERT_NE(guid, kInvalidAssetGuid);
    EXPECT_EQ(ReadFile(destinationPath), "text without final newline\n");
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}

TEST_F(AssetImporterTest, TextImport_RejectsOverCapSourceBeforePublish) {
    const std::filesystem::path sourcePath = "uve_text_parser_oversized_source.txt";
    const std::filesystem::path destinationPath = "uve_text_parser_oversized_destination.txt";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    const std::string oversized(kMaximumTextImportBytes + 1U, 'T');
    WriteFixtureFile(sourcePath, oversized);

    EXPECT_EQ(importer.Import(sourcePath, destinationPath, assetDatabase), kInvalidAssetGuid);
    EXPECT_FALSE(std::filesystem::exists(destinationPath));

    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}

TEST(AssetImportSettingsTest, TextImportSettings_CacheVersionIsStable) {
    TextImportSettings preserve;
    TextImportSettings same = preserve;
    TextImportSettings normalized = preserve;
    normalized.lineEnding = TextImportLineEnding::LineFeed;
    TextImportSettings trailing = normalized;
    trailing.ensureTrailingLineEnding = true;

    EXPECT_EQ(preserve.GetCacheVersion(), "text-v1;line-ending=preserve;trailing-newline=false");
    EXPECT_EQ(preserve.GetCacheVersion(), same.GetCacheVersion());
    EXPECT_NE(preserve.GetCacheVersion(), normalized.GetCacheVersion());
    EXPECT_NE(normalized.GetCacheVersion(), trailing.GetCacheVersion());
}

TEST_F(AssetImporterTest, Import_TypedUVEEnvelopeExtensions_CopyAndRegisterGuid) {
    constexpr std::array<std::string_view, 4> kTypedEnvelopeExtensions = {
        ".uvmodel", ".uvtex", ".uvshader", ".uvmat"};

    for (const std::string_view extension : kTypedEnvelopeExtensions) {
        const std::filesystem::path sourcePath =
            std::string("uve_asset_importer_typed_source") + std::string(extension);
        const std::filesystem::path destinationPath =
            std::string("uve_asset_importer_typed_dest") + std::string(extension);
        std::filesystem::remove(sourcePath);
        std::filesystem::remove(destinationPath);
        WriteFixtureFile(sourcePath, "typed UVE envelope import");

        const AssetGuid guid = importer.Import(sourcePath, destinationPath, assetDatabase);
        ASSERT_NE(guid, kInvalidAssetGuid) << extension;
        EXPECT_TRUE(std::filesystem::exists(destinationPath));
        EXPECT_EQ(ReadFile(destinationPath), "typed UVE envelope import");
        EXPECT_EQ(assetDatabase.Resolve(guid), destinationPath);

        std::filesystem::remove(sourcePath);
        std::filesystem::remove(destinationPath);
    }
}

TEST_F(AssetImporterTest, Import_RawShaderSource_InfersStageAndPublishesTypedEnvelope) {
    struct ShaderCase {
        std::string_view extension;
        ShaderStageKind stage;
    };
    constexpr std::array<ShaderCase, 3> kCases = {{
        {".vert", ShaderStageKind::Vertex},
        {".frag", ShaderStageKind::Fragment},
        {".comp", ShaderStageKind::Compute},
    }};

    for (const ShaderCase& expected : kCases) {
        const std::filesystem::path sourcePath =
            std::string("uve_asset_importer_raw_shader_source") + std::string(expected.extension);
        const std::filesystem::path destinationPath =
            std::string("uve_asset_importer_raw_shader_destination") + std::string(expected.extension) + ".uvshader";
        std::filesystem::remove(sourcePath);
        std::filesystem::remove(destinationPath);
        WriteFixtureFile(sourcePath, "#version 450\nvoid main() {}\n");

        const AssetGuid guid = importer.Import(sourcePath, destinationPath, assetDatabase);

        ASSERT_NE(guid, kInvalidAssetGuid) << expected.extension;
        ShaderAsset shader;
        ASSERT_TRUE(LoadShaderAsset(destinationPath, shader)) << expected.extension;
        EXPECT_EQ(shader.stage, expected.stage);
        EXPECT_EQ(shader.sourceCode, "#version 450\nvoid main() {}\n");
        EXPECT_EQ(shader.entryPointName, "main");

        std::filesystem::remove(sourcePath);
        std::filesystem::remove(destinationPath);
    }
}

TEST_F(AssetImporterTest, Import_RawShaderSource_RejectsEmptySourceAndPreservesDestination) {
    const std::filesystem::path sourcePath = "uve_asset_importer_raw_shader_empty.vert";
    const std::filesystem::path destinationPath = "uve_asset_importer_raw_shader_empty.uvshader";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    WriteFixtureFile(destinationPath, "prior typed shader");
    WriteFixtureFile(sourcePath, "");

    EXPECT_EQ(importer.Import(sourcePath, destinationPath, assetDatabase), kInvalidAssetGuid);
    EXPECT_EQ(ReadFile(destinationPath), "prior typed shader");

    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}

TEST_F(AssetImporterTest, Import_UnregisteredExtension_ReturnsInvalidAndLogsError) {
    const std::filesystem::path sourcePath = "uve_asset_importer_tests_source.unknownext";
    std::filesystem::remove(sourcePath);
    WriteFixtureFile(sourcePath, "data");

    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    const AssetGuid guid = importer.Import(sourcePath, "uve_asset_importer_tests_dest.unknownext", assetDatabase);
    EXPECT_EQ(guid, kInvalidAssetGuid);

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Error &&
                   message.message.contains("no importer registered");
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
    std::filesystem::remove(sourcePath);
}

TEST_F(AssetImporterTest, RegisterImporter_InvalidRegistrationPreservesExistingImporter) {
    const std::filesystem::path sourcePath = "uve_asset_importer_tests_invalid_registration.custom";
    const std::filesystem::path destinationPath = "uve_asset_importer_tests_invalid_registration_dest.custom";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    WriteFixtureFile(sourcePath, "source");

    bool validImporterRan = false;
    const auto validImporter = [&validImporterRan](const std::filesystem::path&,
                                                     const std::filesystem::path& destination,
                                                     const AssetImportSettings&) {
        validImporterRan = true;
        std::ofstream file(destination, std::ios::binary);
        file << "valid importer";
        return file.good();
    };
    importer.RegisterImporter("custom", validImporter);
    importer.RegisterImporter("", {});
    importer.RegisterImporter("custom", {});

    const AssetImportSourceClassification noExtension = importer.ClassifySource("asset");
    EXPECT_FALSE(noExtension.importerRegistered);
    const AssetGuid guid = importer.Import(sourcePath, destinationPath, assetDatabase);
    ASSERT_NE(guid, kInvalidAssetGuid);
    EXPECT_TRUE(validImporterRan);
    EXPECT_EQ(ReadFile(destinationPath), "valid importer");

    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}

TEST_F(AssetImporterTest, RegisterImporter_CustomExtension_IsPickedOverGeneric) {
    const std::filesystem::path sourcePath = "uve_asset_importer_tests_source.custom";
    const std::filesystem::path destinationPath = "uve_asset_importer_tests_dest.custom";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    WriteFixtureFile(sourcePath, "ignored by custom importer");

    bool customImporterRan = false;
    importer.RegisterImporter("custom", [&customImporterRan](const std::filesystem::path&,
                                                                  const std::filesystem::path& destination,
                                                                  const AssetImportSettings&) {
        customImporterRan = true;
        std::ofstream file(destination, std::ios::binary);
        file << "written by custom importer";
        return file.good();
    });

    const AssetGuid guid = importer.Import(sourcePath, destinationPath, assetDatabase);
    ASSERT_NE(guid, kInvalidAssetGuid);
    EXPECT_TRUE(customImporterRan);
    EXPECT_EQ(ReadFile(destinationPath), "written by custom importer");

    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}

TEST_F(AssetImporterTest, Import_ImporterExceptionReturnsInvalidWithoutDatabaseRegistration) {
    const std::filesystem::path sourcePath = "uve_asset_importer_tests_source.throwing";
    const std::filesystem::path destinationPath = "uve_asset_importer_tests_dest.throwing";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    WriteFixtureFile(sourcePath, "source");

    class ThrowingAssetDatabase final : public IAssetDatabase {
    public:
        bool Load(const std::filesystem::path&) override { return false; }
        bool Save() override { return false; }
        bool Save(const std::filesystem::path&) override { return false; }
        [[nodiscard]] AssetGuid Register(const std::filesystem::path&) override {
            registered = true;
            return AssetGuid{777U};
        }
        [[nodiscard]] std::filesystem::path Resolve(AssetGuid) const override { return {}; }
        [[nodiscard]] bool HasGuid(AssetGuid) const override { return false; }
        [[nodiscard]] std::vector<AssetRecord> GetRegisteredAssets() const override { return {}; }
        bool registered = false;
    } database;

    importer.RegisterImporter("throwing", [](const std::filesystem::path&, const std::filesystem::path&,
                                                  const AssetImportSettings&) -> bool {
        throw std::runtime_error("injected importer exception");
    });
    EXPECT_EQ(importer.Import(sourcePath, destinationPath, database), kInvalidAssetGuid);
    EXPECT_FALSE(database.registered);
    EXPECT_FALSE(std::filesystem::exists(destinationPath));

    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}

TEST_F(AssetImporterTest, Import_DatabaseRegistrationExceptionReturnsInvalidWithoutEscaping) {
    const std::filesystem::path sourcePath = "uve_asset_importer_tests_source.database_throwing";
    const std::filesystem::path destinationPath = "uve_asset_importer_tests_dest.database_throwing";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    WriteFixtureFile(sourcePath, "source");

    class ThrowingRegistrationDatabase final : public IAssetDatabase {
    public:
        bool Load(const std::filesystem::path&) override { return false; }
        bool Save() override { return false; }
        bool Save(const std::filesystem::path&) override { return false; }
        [[nodiscard]] AssetGuid Register(const std::filesystem::path&) override {
            throw std::runtime_error("injected registration exception");
        }
        [[nodiscard]] std::filesystem::path Resolve(AssetGuid) const override { return {}; }
        [[nodiscard]] bool HasGuid(AssetGuid) const override { return false; }
        [[nodiscard]] std::vector<AssetRecord> GetRegisteredAssets() const override { return {}; }
    } database;

    importer.RegisterImporter("database_throwing", [](const std::filesystem::path&,
                                                           const std::filesystem::path& destination,
                                                           const AssetImportSettings&) -> bool {
        std::ofstream file(destination, std::ios::binary | std::ios::trunc);
        file << "converted before registration failure";
        return file.good();
    });

    AssetGuid guid = kInvalidAssetGuid;
    EXPECT_NO_THROW(guid = importer.Import(sourcePath, destinationPath, database));
    EXPECT_EQ(guid, kInvalidAssetGuid);
    EXPECT_TRUE(std::filesystem::exists(destinationPath));

    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}

TEST_F(AssetImporterTest, Import_ImporterReportsFailure_ReturnsInvalidAndLogsError) {
    const std::filesystem::path sourcePath = "uve_asset_importer_tests_source.failing";
    std::filesystem::remove(sourcePath);
    WriteFixtureFile(sourcePath, "data");

    importer.RegisterImporter("failing", [](const std::filesystem::path&, const std::filesystem::path&,
                                                 const AssetImportSettings&) { return false; });

    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    const AssetGuid guid =
        importer.Import(sourcePath, "uve_asset_importer_tests_dest.failing", assetDatabase);
    EXPECT_EQ(guid, kInvalidAssetGuid);

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Error &&
                   message.message.contains("import failed");
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
    std::filesystem::remove(sourcePath);
}

} // namespace
} // namespace UVE::Asset::Tests
