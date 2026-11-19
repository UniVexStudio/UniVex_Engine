// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <utility>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"
#include "editor.h"
#include "engine_core.h"
#include "vector2.h"

namespace UVE::Editor::Tests {
namespace {

[[nodiscard]] Core::EngineConfigUVE MakeViewportTestConfigUVE() {
    Core::EngineConfigUVE config{};
    config.headlessUVE = true;
    config.logFilePath = "uve_editor_viewports_tests.log";
    config.settingsFilePath = "uve_editor_viewports_tests_settings.json";
    config.projectSettingsFilePath = "uve_editor_viewports_tests_project_settings.json";
    config.inputMapFilePath = "uve_editor_viewports_tests_input_map.json";
    config.assetDatabaseFilePath = "uve_editor_viewports_tests_assets.json";
    config.saveDirectoryPath = "uve_editor_viewports_tests_saves";
    config.shaderCachePath = "uve_editor_viewports_tests_shader_cache";
    config.shaderSourceRealDirectoryUVE = ::UVE::Tests::RepositoryRootUVE() / "Engine/Runtime/RHI/Shader/built_in";
    config.shaderSourceMountPrefixUVE = "shaders";
    return config;
}

TEST(EditorViewportsUVETest, DefaultEditorStartsWithAllSevenViewportSlotsUnbound) {
    Core::EngineCoreUVE engine(MakeViewportTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_viewports_default.uvscene");
        editor.InitUVE();

        const EditorUVE::EditorViewportsUVE& viewports = editor.GetEditorViewportsUVE();
        EXPECT_FALSE(static_cast<bool>(viewports.level));
        EXPECT_FALSE(static_cast<bool>(viewports.cameraPreview));
        EXPECT_FALSE(static_cast<bool>(viewports.entity));
        EXPECT_FALSE(static_cast<bool>(viewports.widgetPreview));
        EXPECT_FALSE(static_cast<bool>(viewports.retarget));
        EXPECT_FALSE(static_cast<bool>(viewports.montage));
        EXPECT_FALSE(static_cast<bool>(viewports.vfx));

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorViewportsUVETest, IndividualViewportSettersIsolateEachWindowSlot) {
    Core::EngineCoreUVE engine(MakeViewportTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_viewports_isolated.uvscene");
        editor.InitUVE();

        std::array<int, 7> callCounts{};
        const auto makeCallback = [&callCounts](const std::size_t slotIndex, const std::uint64_t textureId) {
            return [&callCounts, slotIndex, textureId](const Math::Vector2UVE& availableSize,
                                                       Math::Vector2UVE& outUsedSize,
                                                       const EditorUVE::ViewportOverlayStateUVE& /*overlayState*/) {
                ++callCounts[slotIndex];
                outUsedSize = availableSize;
                return textureId;
            };
        };

        editor.SetLevelViewportRendererUVE(makeCallback(0U, 101U));
        EXPECT_TRUE(static_cast<bool>(editor.GetEditorViewportsUVE().level));
        EXPECT_FALSE(static_cast<bool>(editor.GetEditorViewportsUVE().cameraPreview));
        EXPECT_FALSE(static_cast<bool>(editor.GetEditorViewportsUVE().entity));
        EXPECT_FALSE(static_cast<bool>(editor.GetEditorViewportsUVE().widgetPreview));
        EXPECT_FALSE(static_cast<bool>(editor.GetEditorViewportsUVE().retarget));
        EXPECT_FALSE(static_cast<bool>(editor.GetEditorViewportsUVE().montage));
        EXPECT_FALSE(static_cast<bool>(editor.GetEditorViewportsUVE().vfx));

        editor.SetCameraPreviewViewportRendererUVE(makeCallback(1U, 102U));
        editor.SetEntityViewportRendererUVE(makeCallback(2U, 103U));
        editor.SetWidgetPreviewViewportRendererUVE(makeCallback(3U, 104U));
        editor.SetRetargetViewportRendererUVE(makeCallback(4U, 105U));
        editor.SetMontageViewportRendererUVE(makeCallback(5U, 106U));
        editor.SetVfxViewportRendererUVE(makeCallback(6U, 107U));

        const EditorUVE::EditorViewportsUVE& viewports = editor.GetEditorViewportsUVE();
        const EditorUVE::ViewportOverlayStateUVE overlay{};
        Math::Vector2UVE used{0.0F, 0.0F};

        EXPECT_EQ(viewports.level(Math::Vector2UVE{1280.0F, 720.0F}, used, overlay), 101U);
        EXPECT_FLOAT_EQ(used.x, 1280.0F);
        EXPECT_FLOAT_EQ(used.y, 720.0F);

        EXPECT_EQ(viewports.cameraPreview(Math::Vector2UVE{320.0F, 180.0F}, used, overlay), 102U);
        EXPECT_FLOAT_EQ(used.x, 320.0F);
        EXPECT_FLOAT_EQ(used.y, 180.0F);

        EXPECT_EQ(viewports.entity(Math::Vector2UVE{800.0F, 600.0F}, used, overlay), 103U);
        EXPECT_EQ(viewports.widgetPreview(Math::Vector2UVE{400.0F, 400.0F}, used, overlay), 104U);
        EXPECT_EQ(viewports.retarget(Math::Vector2UVE{640.0F, 480.0F}, used, overlay), 105U);
        EXPECT_EQ(viewports.montage(Math::Vector2UVE{720.0F, 540.0F}, used, overlay), 106U);
        EXPECT_EQ(viewports.vfx(Math::Vector2UVE{512.0F, 512.0F}, used, overlay), 107U);

        for (const int count : callCounts) {
            EXPECT_EQ(count, 1);
        }

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorViewportsUVETest, BulkSetAndClearManageAllSevenViewportCallbacksCleanly) {
    Core::EngineCoreUVE engine(MakeViewportTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_viewports_bulk.uvscene");
        editor.InitUVE();

        EditorUVE::EditorViewportsUVE batch{};
        const auto makeStatic = [](const std::uint64_t textureId) {
            return [textureId](const Math::Vector2UVE& availableSize, Math::Vector2UVE& outUsedSize,
                               const EditorUVE::ViewportOverlayStateUVE& /*overlayState*/) {
                outUsedSize = availableSize;
                return textureId;
            };
        };
        batch.level = makeStatic(201U);
        batch.cameraPreview = makeStatic(202U);
        batch.entity = makeStatic(203U);
        batch.widgetPreview = makeStatic(204U);
        batch.retarget = makeStatic(205U);
        batch.montage = makeStatic(206U);
        batch.vfx = makeStatic(207U);
        editor.SetEditorViewportsUVE(std::move(batch));

        const EditorUVE::ViewportOverlayStateUVE overlay{};
        Math::Vector2UVE used{0.0F, 0.0F};
        EXPECT_EQ(editor.GetEditorViewportsUVE().level(Math::Vector2UVE{640.0F, 360.0F}, used, overlay), 201U);
        EXPECT_EQ(editor.GetEditorViewportsUVE().cameraPreview(Math::Vector2UVE{200.0F, 120.0F}, used, overlay), 202U);
        EXPECT_EQ(editor.GetEditorViewportsUVE().entity(Math::Vector2UVE{640.0F, 360.0F}, used, overlay), 203U);
        EXPECT_EQ(editor.GetEditorViewportsUVE().widgetPreview(Math::Vector2UVE{256.0F, 256.0F}, used, overlay), 204U);
        EXPECT_EQ(editor.GetEditorViewportsUVE().retarget(Math::Vector2UVE{500.0F, 500.0F}, used, overlay), 205U);
        EXPECT_EQ(editor.GetEditorViewportsUVE().montage(Math::Vector2UVE{500.0F, 500.0F}, used, overlay), 206U);
        EXPECT_EQ(editor.GetEditorViewportsUVE().vfx(Math::Vector2UVE{500.0F, 500.0F}, used, overlay), 207U);

        editor.ClearEditorViewportsUVE();
        EXPECT_FALSE(static_cast<bool>(editor.GetEditorViewportsUVE().level));
        EXPECT_FALSE(static_cast<bool>(editor.GetEditorViewportsUVE().cameraPreview));
        EXPECT_FALSE(static_cast<bool>(editor.GetEditorViewportsUVE().entity));
        EXPECT_FALSE(static_cast<bool>(editor.GetEditorViewportsUVE().widgetPreview));
        EXPECT_FALSE(static_cast<bool>(editor.GetEditorViewportsUVE().retarget));
        EXPECT_FALSE(static_cast<bool>(editor.GetEditorViewportsUVE().montage));
        EXPECT_FALSE(static_cast<bool>(editor.GetEditorViewportsUVE().vfx));

        const EditorViewportInputUVE input{};
        EditorUVE::WidgetPreviewRequestUVE widgetRequest{};
        widgetRequest.assetPath = "assets/NonExistent.uventity";
        EXPECT_EQ(editor.RenderWidgetPreviewUVE(widgetRequest, Math::Vector2UVE{256.0F, 256.0F}, used), 0U);
        EXPECT_EQ(editor.RenderMontagePreviewUVE(Math::Vector2UVE{256.0F, 256.0F}, used, input), 0U);
        EXPECT_EQ(editor.RenderVfxPreviewUVE(Math::Vector2UVE{256.0F, 256.0F}, used, input), 0U);

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

} // namespace
} // namespace UVE::Editor::Tests
