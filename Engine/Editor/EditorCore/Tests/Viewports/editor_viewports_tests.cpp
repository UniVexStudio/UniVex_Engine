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

[[nodiscard]] Core::EngineConfig MakeViewportTestConfig() {
    Core::EngineConfig config{};
    config.headless = true;
    config.logFilePath = "uve_editor_viewports_tests.log";
    config.settingsFilePath = "uve_editor_viewports_tests_settings.json";
    config.projectSettingsFilePath = "uve_editor_viewports_tests_project_settings.json";
    config.inputMapFilePath = "uve_editor_viewports_tests_input_map.json";
    config.assetDatabaseFilePath = "uve_editor_viewports_tests_assets.json";
    config.saveDirectoryPath = "uve_editor_viewports_tests_saves";
    config.shaderCachePath = "uve_editor_viewports_tests_shader_cache";
    config.shaderSourceRealDirectory = ::UVE::Tests::RepositoryRoot() / "Engine/Runtime/RHI/Shader/built_in";
    config.shaderSourceMountPrefix = "shaders";
    return config;
}

TEST(EditorViewportsTest, DefaultEditorStartsWithAllSevenViewportSlotsUnbound) {
    Core::EngineCore engine(MakeViewportTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_viewports_default.uvscene");
        editor.Init();

        const Editor::EditorViewports& viewports = editor.GetEditorViewports();
        EXPECT_FALSE(static_cast<bool>(viewports.level));
        EXPECT_FALSE(static_cast<bool>(viewports.cameraPreview));
        EXPECT_FALSE(static_cast<bool>(viewports.entity));
        EXPECT_FALSE(static_cast<bool>(viewports.widgetPreview));
        EXPECT_FALSE(static_cast<bool>(viewports.retarget));
        EXPECT_FALSE(static_cast<bool>(viewports.montage));
        EXPECT_FALSE(static_cast<bool>(viewports.vfx));

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorViewportsTest, IndividualViewportSettersIsolateEachWindowSlot) {
    Core::EngineCore engine(MakeViewportTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_viewports_isolated.uvscene");
        editor.Init();

        std::array<int, 7> callCounts{};
        const auto makeCallback = [&callCounts](const std::size_t slotIndex, const std::uint64_t textureId) {
            return [&callCounts, slotIndex, textureId](const Math::Vector2& availableSize,
                                                       Math::Vector2& outUsedSize,
                                                       const Editor::ViewportOverlayState& /*overlayState*/) {
                ++callCounts[slotIndex];
                outUsedSize = availableSize;
                return textureId;
            };
        };

        editor.SetLevelViewportRenderer(makeCallback(0U, 101U));
        EXPECT_TRUE(static_cast<bool>(editor.GetEditorViewports().level));
        EXPECT_FALSE(static_cast<bool>(editor.GetEditorViewports().cameraPreview));
        EXPECT_FALSE(static_cast<bool>(editor.GetEditorViewports().entity));
        EXPECT_FALSE(static_cast<bool>(editor.GetEditorViewports().widgetPreview));
        EXPECT_FALSE(static_cast<bool>(editor.GetEditorViewports().retarget));
        EXPECT_FALSE(static_cast<bool>(editor.GetEditorViewports().montage));
        EXPECT_FALSE(static_cast<bool>(editor.GetEditorViewports().vfx));

        editor.SetCameraPreviewViewportRenderer(makeCallback(1U, 102U));
        editor.SetEntityViewportRenderer(makeCallback(2U, 103U));
        editor.SetWidgetPreviewViewportRenderer(makeCallback(3U, 104U));
        editor.SetRetargetViewportRenderer(makeCallback(4U, 105U));
        editor.SetMontageViewportRenderer(makeCallback(5U, 106U));
        editor.SetVfxViewportRenderer(makeCallback(6U, 107U));

        const Editor::EditorViewports& viewports = editor.GetEditorViewports();
        const Editor::ViewportOverlayState overlay{};
        Math::Vector2 used{0.0F, 0.0F};

        EXPECT_EQ(viewports.level(Math::Vector2{1280.0F, 720.0F}, used, overlay), 101U);
        EXPECT_FLOAT_EQ(used.x, 1280.0F);
        EXPECT_FLOAT_EQ(used.y, 720.0F);

        EXPECT_EQ(viewports.cameraPreview(Math::Vector2{320.0F, 180.0F}, used, overlay), 102U);
        EXPECT_FLOAT_EQ(used.x, 320.0F);
        EXPECT_FLOAT_EQ(used.y, 180.0F);

        EXPECT_EQ(viewports.entity(Math::Vector2{800.0F, 600.0F}, used, overlay), 103U);
        EXPECT_EQ(viewports.widgetPreview(Math::Vector2{400.0F, 400.0F}, used, overlay), 104U);
        EXPECT_EQ(viewports.retarget(Math::Vector2{640.0F, 480.0F}, used, overlay), 105U);
        EXPECT_EQ(viewports.montage(Math::Vector2{720.0F, 540.0F}, used, overlay), 106U);
        EXPECT_EQ(viewports.vfx(Math::Vector2{512.0F, 512.0F}, used, overlay), 107U);

        for (const int count : callCounts) {
            EXPECT_EQ(count, 1);
        }

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorViewportsTest, BulkSetAndClearManageAllSevenViewportCallbacksCleanly) {
    Core::EngineCore engine(MakeViewportTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_viewports_bulk.uvscene");
        editor.Init();

        Editor::EditorViewports batch{};
        const auto makeStatic = [](const std::uint64_t textureId) {
            return [textureId](const Math::Vector2& availableSize, Math::Vector2& outUsedSize,
                               const Editor::ViewportOverlayState& /*overlayState*/) {
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
        editor.SetEditorViewports(std::move(batch));

        const Editor::ViewportOverlayState overlay{};
        Math::Vector2 used{0.0F, 0.0F};
        EXPECT_EQ(editor.GetEditorViewports().level(Math::Vector2{640.0F, 360.0F}, used, overlay), 201U);
        EXPECT_EQ(editor.GetEditorViewports().cameraPreview(Math::Vector2{200.0F, 120.0F}, used, overlay), 202U);
        EXPECT_EQ(editor.GetEditorViewports().entity(Math::Vector2{640.0F, 360.0F}, used, overlay), 203U);
        EXPECT_EQ(editor.GetEditorViewports().widgetPreview(Math::Vector2{256.0F, 256.0F}, used, overlay), 204U);
        EXPECT_EQ(editor.GetEditorViewports().retarget(Math::Vector2{500.0F, 500.0F}, used, overlay), 205U);
        EXPECT_EQ(editor.GetEditorViewports().montage(Math::Vector2{500.0F, 500.0F}, used, overlay), 206U);
        EXPECT_EQ(editor.GetEditorViewports().vfx(Math::Vector2{500.0F, 500.0F}, used, overlay), 207U);

        editor.ClearEditorViewports();
        EXPECT_FALSE(static_cast<bool>(editor.GetEditorViewports().level));
        EXPECT_FALSE(static_cast<bool>(editor.GetEditorViewports().cameraPreview));
        EXPECT_FALSE(static_cast<bool>(editor.GetEditorViewports().entity));
        EXPECT_FALSE(static_cast<bool>(editor.GetEditorViewports().widgetPreview));
        EXPECT_FALSE(static_cast<bool>(editor.GetEditorViewports().retarget));
        EXPECT_FALSE(static_cast<bool>(editor.GetEditorViewports().montage));
        EXPECT_FALSE(static_cast<bool>(editor.GetEditorViewports().vfx));

        const EditorViewportInput input{};
        Editor::WidgetPreviewRequest widgetRequest{};
        widgetRequest.assetPath = "assets/NonExistent.uventity";
        EXPECT_EQ(editor.RenderWidgetPreview(widgetRequest, Math::Vector2{256.0F, 256.0F}, used), 0U);
        EXPECT_EQ(editor.RenderMontagePreview(Math::Vector2{256.0F, 256.0F}, used, input), 0U);
        EXPECT_EQ(editor.RenderVfxPreview(Math::Vector2{256.0F, 256.0F}, used, input), 0U);

        editor.Shutdown();
    }

    engine.Shutdown();
}

} // namespace
} // namespace UVE::Editor::Tests
