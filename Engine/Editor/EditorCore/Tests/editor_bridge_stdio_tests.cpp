// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <array>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"
#include <nlohmann/json.hpp>

#include "engine_core.h"
#include "editor_bridge_stdio.h"
#include "mesh_component.h"

namespace UVE::Editor::Tests {
namespace {

using Json = nlohmann::json;

[[nodiscard]] Core::EngineConfig MakeBridgeStdioTestConfig() {
    Core::EngineConfig config{};
    config.headless = true;
    config.logFilePath = "uve_editor_bridge_stdio_tests.log";
    config.settingsFilePath = "uve_editor_bridge_stdio_tests_settings.json";
    config.assetDatabaseFilePath = "uve_editor_bridge_stdio_tests_assets.json";
    config.saveDirectoryPath = "uve_editor_bridge_stdio_tests_saves";
    config.shaderCachePath = "uve_editor_bridge_stdio_tests_shader_cache";
    config.shaderSourceRealDirectory = ::UVE::Tests::RepositoryRoot() / "Engine/Runtime/RHI/Shader/built_in";
    config.shaderSourceMountPrefix = "shaders";
    return config;
}

void AppendFrame(std::ostream& output, const Json& message) {
    const std::string body = message.dump();
    const std::uint32_t length = static_cast<std::uint32_t>(body.size());
    const std::array<char, 4U> header{
        static_cast<char>((length >> 24U) & 0xFFU), static_cast<char>((length >> 16U) & 0xFFU),
        static_cast<char>((length >> 8U) & 0xFFU), static_cast<char>(length & 0xFFU)};
    output.write(header.data(), static_cast<std::streamsize>(header.size()));
    output.write(body.data(), static_cast<std::streamsize>(body.size()));
}

[[nodiscard]] std::vector<Json> ReadFrames(std::istream& input) {
    std::vector<Json> frames;
    while (true) {
        std::array<char, 4U> header{};
        input.read(header.data(), static_cast<std::streamsize>(header.size()));
        if (input.gcount() == 0 && input.eof()) {
            return frames;
        }
        EXPECT_EQ(input.gcount(), static_cast<std::streamsize>(header.size()));
        const std::uint32_t length =
            (static_cast<std::uint32_t>(static_cast<unsigned char>(header[0])) << 24U) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(header[1])) << 16U) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(header[2])) << 8U) |
            static_cast<std::uint32_t>(static_cast<unsigned char>(header[3]));
        std::string body(length, '\0');
        input.read(body.data(), static_cast<std::streamsize>(body.size()));
        EXPECT_EQ(input.gcount(), static_cast<std::streamsize>(body.size()));
        frames.push_back(Json::parse(body));
    }
}

TEST(EditorBridgeStdioTest, Serve_HandshakesAndRoutesExistingBridgeDispatch) {
    Core::EngineCore engine(MakeBridgeStdioTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_bridge_stdio_roundtrip.uvscene");
        editor.Init();
        const Scene::Entity meshEntity = editor.CreateDocumentEntity(EditorEntityKind::Cube);
        ASSERT_NE(meshEntity, Scene::kInvalidEntity);
        engine.GetServices().GetEntityManager().AddComponent<Scene::MeshComponent>(
            meshEntity, Scene::MeshComponent{Asset::AssetGuid{0x3333U}, Asset::AssetGuid{0x4444U}});
        EditorBridge bridge(editor);
        Asset::DataTable previewTable("weapons");
        ASSERT_TRUE(previewTable.DefineColumn("damage", Asset::DataTableColumnType::Integer));
        ASSERT_TRUE(previewTable.AddRow("pistol", {std::int64_t{25}}));
        bridge.SetDataTablePreviewSnapshot(previewTable.GetSnapshot());
        EditorBridgeStdioServer server(bridge);
        std::stringstream input;
        std::stringstream output;
        std::stringstream diagnostics;

        AppendFrame(input, Json{{"jsonrpc", "2.0"},
                                      {"id", 1U},
                                      {"method", "bridge.hello"},
                                      {"params", {{"protocolVersion", kEditorBridgeProtocolVersion}}}});
        AppendFrame(input, Json{{"jsonrpc", "2.0"},
                                      {"id", 2U},
                                      {"method", "bridge.dispatch"},
                                      {"params", {{"protocolVersion", kEditorBridgeProtocolVersion},
                                                  {"requestId", 42U},
                                                  {"expectedRevision", 1U},
                                                  {"kind", "createDocumentEntity"},
                                                  {"entityKind", "cube"}}}});
        AppendFrame(input, Json{{"jsonrpc", "2.0"},
                                      {"id", 3U},
                                      {"method", "bridge.dispatch"},
                                      {"params", {{"protocolVersion", kEditorBridgeProtocolVersion},
                                                  {"requestId", 46U},
                                                  {"expectedRevision", 999U},
                                                  {"kind", "queueAssetTableImport"},
                                                  {"contentEntryPath", "notes.txt"},
                                                  {"contentImportDestinationPath", "notes_imported.txt"}}}});

        EXPECT_EQ(server.Serve(input, output, diagnostics), 0);
        EXPECT_TRUE(diagnostics.str().empty());
        const std::vector<Json> frames = ReadFrames(output);
        ASSERT_EQ(frames.size(), 3U);
        EXPECT_TRUE(frames[0U].at("result").at("compatible").get<bool>());
        EXPECT_EQ(frames[0U].at("result").at("protocolVersion").get<std::uint32_t>(),
                  kEditorBridgeProtocolVersion);
        const Json& handshakeSnapshot = frames[0U].at("result").at("snapshot");
        EXPECT_TRUE(handshakeSnapshot.at("selectedEntitiesTruncated").is_boolean());
        EXPECT_TRUE(handshakeSnapshot.at("hierarchy").at("entries").is_array());
        EXPECT_TRUE(handshakeSnapshot.at("inspector").at("eligibleDrawerIds").is_array());
        ASSERT_TRUE(handshakeSnapshot.at("inspector").at("attachedComponentIds").is_array());
        // A PlaneMesh3D is a SurfaceInstance3D: its sections run from the object's own (its collision
        // drawn inside it) through its bases to the common Object section.
        EXPECT_EQ(handshakeSnapshot.at("inspector").at("attachedComponentIds"),
                  (Json{"mesh", "surface-instance", "render-instance", "visibility", "process",
                           "physics-interpolation", "auto-translate", "editor-description", "script",
                           "object-metadata"}));
        ASSERT_TRUE(handshakeSnapshot.at("inspector").at("assetBinding").is_object());
        EXPECT_EQ(handshakeSnapshot.at("inspector").at("assetBinding").at("meshGuid").get<std::uint64_t>(), 0x3333U);
        EXPECT_EQ(handshakeSnapshot.at("inspector").at("assetBinding").at("materialGuid").get<std::uint64_t>(), 0x4444U);
        EXPECT_TRUE(handshakeSnapshot.at("assetTable").at("breadcrumbs").is_array());
        ASSERT_TRUE(handshakeSnapshot.at("assetTable").at("importAction").is_object());
        EXPECT_FALSE(handshakeSnapshot.at("assetTable").at("importAction").at("hasSelection").get<bool>());
        EXPECT_FALSE(handshakeSnapshot.at("assetTable").at("importAction").at("canImport").get<bool>());
        EXPECT_FALSE(handshakeSnapshot.at("assetTable").at("importAction").at("canReimport").get<bool>());
        EXPECT_EQ(handshakeSnapshot.at("assetTable").at("importAction").at("diagnostic").get<std::string>(),
                  "no Asset Table entry is selected");
        ASSERT_TRUE(handshakeSnapshot.at("viewportSurface").is_object());
        EXPECT_EQ(handshakeSnapshot.at("viewportSurface").at("state").get<std::uint8_t>(), 0U);
        EXPECT_TRUE(handshakeSnapshot.at("viewportSurface").at("nativeRendererOwnsSurface").get<bool>());
        EXPECT_FALSE(handshakeSnapshot.at("viewportSurface").at("managedAttachAllowed").get<bool>());
        EXPECT_TRUE(handshakeSnapshot.at("viewportSurface").at("reason").is_string());
        ASSERT_TRUE(handshakeSnapshot.at("dataTableCatalog").is_object());
        EXPECT_TRUE(handshakeSnapshot.at("dataTableCatalog").at("generation").is_number_unsigned());
        EXPECT_TRUE(handshakeSnapshot.at("dataTableCatalog").at("entriesTruncated").is_boolean());
        EXPECT_TRUE(handshakeSnapshot.at("dataTableCatalog").at("entries").is_array());
        ASSERT_TRUE(handshakeSnapshot.at("dataTablePreview").is_object());
        EXPECT_TRUE(handshakeSnapshot.at("dataTablePreview").at("available").get<bool>());
        ASSERT_EQ(handshakeSnapshot.at("dataTablePreview").at("columns").size(), 1U);
        EXPECT_EQ(handshakeSnapshot.at("dataTablePreview").at("columns").front().at("name").get<std::string>(), "damage");
        ASSERT_EQ(handshakeSnapshot.at("dataTablePreview").at("rows").size(), 1U);
        EXPECT_EQ(handshakeSnapshot.at("dataTablePreview").at("rows").front().at("values").front().get<std::string>(), "25");
        EXPECT_TRUE(frames[1U].at("result").at("applied").get<bool>());
        EXPECT_EQ(frames[1U].at("result").at("code").get<std::string>(), "bridge.command.applied");
        EXPECT_TRUE(frames[1U].at("result").at("createdEntity").is_object());
        EXPECT_FALSE(frames[2U].at("result").at("applied").get<bool>());
        EXPECT_EQ(frames[2U].at("result").at("code").get<std::string>(),
                  "bridge.snapshot.stale");
        EXPECT_TRUE(frames[2U].at("result").at("contentImportJobId").is_null());
        EXPECT_TRUE(editor.IsSceneDirty());

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorBridgeStdioTest, Serve_RoutesRegistryBackedDataTablePreviewSelection) {
    Core::EngineCore engine(MakeBridgeStdioTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_bridge_stdio_data_table_selection.uvscene");
        editor.Init();
        Asset::DataTableRegistry registry;
        Asset::DataTable table("weapons");
        ASSERT_TRUE(table.DefineColumn("damage", Asset::DataTableColumnType::Integer));
        ASSERT_TRUE(table.AddRow("pistol", {std::int64_t{25}}));
        ASSERT_TRUE(registry.Register(std::move(table)));
        EditorBridge bridge(editor, &registry);
        EditorBridgeStdioServer server(bridge);
        std::stringstream input;
        std::stringstream output;
        std::stringstream diagnostics;

        AppendFrame(input, Json{{"jsonrpc", "2.0"},
                                      {"id", 1U},
                                      {"method", "bridge.hello"},
                                      {"params", {{"protocolVersion", kEditorBridgeProtocolVersion}}}});
        AppendFrame(input, Json{{"jsonrpc", "2.0"},
                                      {"id", 2U},
                                      {"method", "bridge.dispatch"},
                                      {"params", {{"protocolVersion", kEditorBridgeProtocolVersion},
                                                  {"requestId", 7U},
                                                  {"expectedRevision", 1U},
                                                  {"kind", "selectDataTablePreview"},
                                                  {"dataTableName", "weapons"}}}});

        EXPECT_EQ(server.Serve(input, output, diagnostics), 0);
        EXPECT_TRUE(diagnostics.str().empty());
        const std::vector<Json> frames = ReadFrames(output);
        ASSERT_EQ(frames.size(), 2U);
        const Json& response = frames[1U].at("result");
        EXPECT_TRUE(response.at("applied").get<bool>());
        EXPECT_EQ(response.at("code").get<std::string>(), "bridge.command.applied");
        const Json& preview = response.at("snapshot").at("dataTablePreview");
        EXPECT_TRUE(preview.at("available").get<bool>());
        EXPECT_EQ(preview.at("name").get<std::string>(), "weapons");
        EXPECT_EQ(preview.at("rows").front().at("identifier").get<std::string>(), "pistol");
        EXPECT_EQ(preview.at("rows").front().at("values").front().get<std::string>(), "25");

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorBridgeStdioTest, Serve_ReportsIncompatibleHelloWithoutMutatingEditorState) {
    Core::EngineCore engine(MakeBridgeStdioTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_bridge_stdio_incompatible.uvscene");
        editor.Init();
        EditorBridge bridge(editor);
        EditorBridgeStdioServer server(bridge);
        std::stringstream input;
        std::stringstream output;
        std::stringstream diagnostics;
        AppendFrame(input, Json{{"jsonrpc", "2.0"},
                                      {"id", 4U},
                                      {"method", "bridge.hello"},
                                      {"params", {{"protocolVersion", 99U}}}});

        EXPECT_EQ(server.Serve(input, output, diagnostics), 0);
        EXPECT_TRUE(diagnostics.str().empty());
        const std::vector<Json> frames = ReadFrames(output);
        ASSERT_EQ(frames.size(), 1U);
        EXPECT_FALSE(frames.front().at("result").at("compatible").get<bool>());
        EXPECT_EQ(frames.front().at("result").at("code").get<std::string>(), "bridge.protocol.unsupported");
        // The document was not mutated: the only root is the ever-present scene root.
        ASSERT_EQ(editor.GetDocumentRoots().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRoots()[0U], editor.GetDocumentSceneRoot());
        EXPECT_FALSE(editor.IsSceneDirty());

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorBridgeStdioTest, Serve_RejectsMalformedJsonWithoutDispatchingEditorState) {
    Core::EngineCore engine(MakeBridgeStdioTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_bridge_stdio_invalid_json.uvscene");
        editor.Init();
        EditorBridge bridge(editor);
        EditorBridgeStdioServer server(bridge);
        std::stringstream input;
        std::stringstream output;
        std::stringstream diagnostics;
        const std::string malformedJson{"{"};
        const std::uint32_t length = static_cast<std::uint32_t>(malformedJson.size());
        const std::array<char, 4U> header{
            static_cast<char>((length >> 24U) & 0xFFU), static_cast<char>((length >> 16U) & 0xFFU),
            static_cast<char>((length >> 8U) & 0xFFU), static_cast<char>(length & 0xFFU)};
        input.write(header.data(), static_cast<std::streamsize>(header.size()));
        input.write(malformedJson.data(), static_cast<std::streamsize>(malformedJson.size()));

        EXPECT_EQ(server.Serve(input, output, diagnostics), 0);
        EXPECT_EQ(diagnostics.str(), "bridge.transport.json.invalid\n");
        const std::vector<Json> frames = ReadFrames(output);
        ASSERT_EQ(frames.size(), 1U);
        EXPECT_EQ(frames.front().at("error").at("data").at("code").get<std::string>(),
                  "bridge.transport.json.invalid");
        // The document was not mutated: the only root is the ever-present scene root.
        ASSERT_EQ(editor.GetDocumentRoots().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRoots()[0U], editor.GetDocumentSceneRoot());
        EXPECT_FALSE(editor.IsSceneDirty());

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorBridgeStdioTest, Serve_RejectsZeroLengthFrameBeforeDispatchingEditorState) {
    Core::EngineCore engine(MakeBridgeStdioTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_bridge_stdio_invalid_frame.uvscene");
        editor.Init();
        EditorBridge bridge(editor);
        EditorBridgeStdioServer server(bridge);
        std::stringstream input;
        std::stringstream output;
        std::stringstream diagnostics;
        const std::array<char, 4U> zeroLengthHeader{};
        input.write(zeroLengthHeader.data(), static_cast<std::streamsize>(zeroLengthHeader.size()));

        EXPECT_EQ(server.Serve(input, output, diagnostics), 2);
        EXPECT_EQ(diagnostics.str(), "bridge.transport.frame.zero_length\n");
        const std::vector<Json> frames = ReadFrames(output);
        ASSERT_EQ(frames.size(), 1U);
        EXPECT_EQ(frames.front().at("error").at("data").at("code").get<std::string>(),
                  "bridge.transport.frame.zero_length");
        // The document was not mutated: the only root is the ever-present scene root.
        ASSERT_EQ(editor.GetDocumentRoots().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRoots()[0U], editor.GetDocumentSceneRoot());
        EXPECT_FALSE(editor.IsSceneDirty());

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorBridgeStdioTest, Serve_ClassifiesTruncatedAndOversizedFramesBeforeDispatchingEditorState) {
    Core::EngineCore engine(MakeBridgeStdioTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_bridge_stdio_frame_classification.uvscene");
        editor.Init();
        EditorBridge bridge(editor);

        const auto verifyMalformedFrame = [&](const std::string_view bytes, const std::string_view expectedCode) {
            EditorBridgeStdioServer server(bridge);
            std::stringstream input;
            std::stringstream output;
            std::stringstream diagnostics;
            input.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));

            EXPECT_EQ(server.Serve(input, output, diagnostics), 2);
            EXPECT_EQ(diagnostics.str(), std::string(expectedCode) + "\n");
            const std::vector<Json> frames = ReadFrames(output);
            ASSERT_EQ(frames.size(), 1U);
            EXPECT_EQ(frames.front().at("error").at("data").at("code").get<std::string>(), expectedCode);
            // The document was not mutated: the only root is the ever-present scene root.
        ASSERT_EQ(editor.GetDocumentRoots().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRoots()[0U], editor.GetDocumentSceneRoot());
            EXPECT_FALSE(editor.IsSceneDirty());
        };

        const std::string truncatedHeader{"\x00\x01", 2U};
        verifyMalformedFrame(truncatedHeader, "bridge.transport.frame.truncated_header");

        std::string truncatedBody;
        const std::uint32_t declaredLength = 2U;
        const std::array<char, 4U> truncatedBodyHeader{
            static_cast<char>((declaredLength >> 24U) & 0xFFU), static_cast<char>((declaredLength >> 16U) & 0xFFU),
            static_cast<char>((declaredLength >> 8U) & 0xFFU), static_cast<char>(declaredLength & 0xFFU)};
        truncatedBody.append(truncatedBodyHeader.data(), truncatedBodyHeader.size());
        truncatedBody.push_back('{');
        verifyMalformedFrame(truncatedBody, "bridge.transport.frame.truncated_body");

        std::string oversizedFrame;
        const std::uint32_t oversizedLength = EditorBridgeStdioServer::kMaximumFrameBytes + 1U;
        const std::array<char, 4U> oversizedHeader{
            static_cast<char>((oversizedLength >> 24U) & 0xFFU), static_cast<char>((oversizedLength >> 16U) & 0xFFU),
            static_cast<char>((oversizedLength >> 8U) & 0xFFU), static_cast<char>(oversizedLength & 0xFFU)};
        oversizedFrame.append(oversizedHeader.data(), oversizedHeader.size());
        verifyMalformedFrame(oversizedFrame, "bridge.transport.frame.oversized");

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorBridgeStdioTest, Serve_SerializesPrefabRevisionSnapshot) {
    const std::filesystem::path prefabPath = "uve_editor_bridge_stdio_prefab.uvprefab";
    std::filesystem::remove(prefabPath);
    Core::EngineCore engine(MakeBridgeStdioTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_bridge_stdio_prefab.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        const Scene::Entity source = editor.CreateDocumentEntity(EditorEntityKind::Empty);
        ASSERT_NE(source, Scene::kInvalidEntity);
        const Asset::AssetGuid guid = services.GetPrefabSystem().SavePrefab(
            entityManager, services.GetAssetDatabase(), source, prefabPath);
        ASSERT_NE(guid, Asset::kInvalidAssetGuid);
        const Scene::Entity instance = services.GetPrefabSystem().Instantiate(
            entityManager, services.GetSceneGraph(), services.GetAssetDatabase(), guid,
            Scene::kInvalidEntity);
        ASSERT_NE(instance, Scene::kInvalidEntity);
        editor.SelectEntity(instance);

        EditorBridge bridge(editor);
        EditorBridgeStdioServer server(bridge);
        std::stringstream input;
        std::stringstream output;
        std::stringstream diagnostics;
        AppendFrame(input, Json{{"jsonrpc", "2.0"},
                                      {"id", 1U},
                                      {"method", "bridge.hello"},
                                      {"params", {{"protocolVersion", kEditorBridgeProtocolVersion}}}});
        ASSERT_EQ(server.Serve(input, output, diagnostics), 0);
        ASSERT_TRUE(diagnostics.str().empty());
        const std::vector<Json> frames = ReadFrames(output);
        ASSERT_EQ(frames.size(), 1U);
        const Json& prefab = frames.front().at("result").at("snapshot").at("inspector").at("prefab");
        ASSERT_TRUE(prefab.is_object());
        EXPECT_EQ(prefab.at("sourcePrefabGuid").get<std::uint64_t>(), guid.value);
        EXPECT_EQ(prefab.at("status").get<std::uint8_t>(), 1U);
        EXPECT_EQ(prefab.at("overrideCount").get<std::size_t>(), 0U);
        EXPECT_FALSE(prefab.at("canRefresh").get<bool>());

        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove(prefabPath);
}

} // namespace
} // namespace UVE::Editor::Tests
