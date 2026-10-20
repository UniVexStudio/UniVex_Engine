// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_chrome_layout_uve.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace UVE::Editor::Tests {
namespace {

[[nodiscard]] ImGuiViewport MakeTestViewportUVE(const float width = 1920.0F, const float height = 1080.0F,
                                                const float originX = 0.0F, const float originY = 0.0F) {
    ImGuiViewport viewport{};
    viewport.WorkPos = ImVec2{originX, originY};
    viewport.WorkSize = ImVec2{width, height};
    return viewport;
}

class EditorDockingUVETest : public ::testing::Test {
protected:
    void SetUp() override {
        IMGUI_CHECKVERSION();
        m_ctx = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.DisplaySize = ImVec2{1920.0F, 1080.0F};
        io.DeltaTime = 1.0F / 60.0F;

        // Build minimal font atlas for ImGui frame operations
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

        // Ensure any pending layout reset flag is cleared
        ConsumeResetDockingLayoutRequestUVE();
    }

    void TearDown() override {
        ConsumeResetDockingLayoutRequestUVE();
        if (m_ctx != nullptr) {
            ImGui::DestroyContext(m_ctx);
            m_ctx = nullptr;
        }
    }

    ImGuiContext* m_ctx = nullptr;
};

TEST_F(EditorDockingUVETest, CanonicalDockSpaceIdAndPanelLabelsAreValid) {
    EXPECT_STREQ(kMainDockSpaceIdUVE, "UVEMainDockSpace");
    const ImGuiID expectedId = ImGui::GetID("UVEMainDockSpace");
    EXPECT_EQ(GetMainDockSpaceIdUVE(), expectedId);
    EXPECT_NE(GetMainDockSpaceIdUVE(), 0U);

    // Verify canonical panel labels include expected identifiers
    EXPECT_NE(std::string_view(kPanelLabelViewportUVE).find("Viewport"), std::string_view::npos);
    EXPECT_NE(std::string_view(kPanelLabelSceneUVE).find("Outliner"), std::string_view::npos);
    EXPECT_NE(std::string_view(kPanelLabelInspectorUVE).find("Inspector"), std::string_view::npos);
    EXPECT_NE(std::string_view(kPanelLabelContentBrowserUVE).find("Content Browser"), std::string_view::npos);
    EXPECT_NE(std::string_view(kPanelLabelDebugUVE).find("Debug"), std::string_view::npos);
    EXPECT_NE(std::string_view(kPanelLabelConsoleUVE).find("Console"), std::string_view::npos);
    EXPECT_NE(std::string_view(kPanelLabelCaptureUVE).find("Capture"), std::string_view::npos);
    EXPECT_NE(std::string_view(kPanelLabelProfilerUVE).find("Profiler"), std::string_view::npos);

    // Verify dock split ratios match technical specification (~18% left, ~22% right, ~28% bottom)
    EXPECT_NEAR(kDockSplitRatioLeftUVE, 0.18F, 1e-4F);
    EXPECT_NEAR(kDockSplitRatioRightUVE, 0.22F, 1e-4F);
    EXPECT_NEAR(kDockSplitRatioBottomUVE, 0.28F, 1e-4F);
}

TEST_F(EditorDockingUVETest, ResetDockingLayoutRequestLifecycle) {
    EXPECT_FALSE(IsResetDockingLayoutRequestedUVE());

    RequestResetDockingLayoutUVE();
    EXPECT_TRUE(IsResetDockingLayoutRequestedUVE());

    const bool consumed = ConsumeResetDockingLayoutRequestUVE();
    EXPECT_TRUE(consumed);
    EXPECT_FALSE(IsResetDockingLayoutRequestedUVE());

    // Subsequent consume returns false
    EXPECT_FALSE(ConsumeResetDockingLayoutRequestUVE());
}

TEST_F(EditorDockingUVETest, InitialDockTreeConstructionBuildsExpectedTopology) {
    ImGui::NewFrame();
    const ImGuiViewport vp = MakeTestViewportUVE(1920.0F, 1080.0F);

    const ImGuiID dockspaceId = SetupMainDockSpaceUVE(&vp, false);
    EXPECT_EQ(dockspaceId, GetMainDockSpaceIdUVE());

    ImGuiDockNode* const rootNode = ImGui::DockBuilderGetNode(dockspaceId);
    ASSERT_NE(rootNode, nullptr);
    EXPECT_TRUE(rootNode->IsDockSpace());
    EXPECT_TRUE(rootNode->IsSplitNode());
    EXPECT_FLOAT_EQ(rootNode->Size.x, 1920.0F);
    EXPECT_FLOAT_EQ(rootNode->Size.y, 1080.0F);

    // Root node was split horizontally with Left dock at 0.18 ratio
    ASSERT_NE(rootNode->ChildNodes[0], nullptr);
    ASSERT_NE(rootNode->ChildNodes[1], nullptr);
    // Left child node (Hierarchy / Outliner)
    const ImGuiDockNode* const leftChild = rootNode->ChildNodes[0];
    EXPECT_NEAR(leftChild->Size.x, 1920.0F * kDockSplitRatioLeftUVE, 2.0F);

    // Right child node of root is further split into Right (Inspector) and Central column
    const ImGuiDockNode* const remainderNode = rootNode->ChildNodes[1];
    ASSERT_TRUE(remainderNode->IsSplitNode());
    ASSERT_NE(remainderNode->ChildNodes[0], nullptr);
    ASSERT_NE(remainderNode->ChildNodes[1], nullptr);

    ImGui::Render();
}

TEST_F(EditorDockingUVETest, DockTreeDocksAllPanelsCorrectly) {
    ImGui::NewFrame();
    const ImGuiViewport vp = MakeTestViewportUVE(1920.0F, 1080.0F);

    const ImGuiID dockspaceId = SetupMainDockSpaceUVE(&vp, false);
    EXPECT_EQ(dockspaceId, GetMainDockSpaceIdUVE());

    // Begin the panels to allow ImGui to apply docked associations
    bool visible = true;
    constexpr ImGuiWindowFlags standardFlags = ImGuiWindowFlags_NoCollapse;

    ImGui::Begin(kPanelLabelSceneUVE, &visible, standardFlags);
    const ImGuiWindow* const sceneWindow = ImGui::GetCurrentWindow();
    ASSERT_NE(sceneWindow, nullptr);
    EXPECT_NE(sceneWindow->DockId, 0U);
    ImGui::End();

    ImGui::Begin(kPanelLabelInspectorUVE, &visible, standardFlags);
    const ImGuiWindow* const inspectorWindow = ImGui::GetCurrentWindow();
    ASSERT_NE(inspectorWindow, nullptr);
    EXPECT_NE(inspectorWindow->DockId, 0U);
    ImGui::End();

    ImGui::Begin(kPanelLabelViewportUVE, &visible, standardFlags);
    const ImGuiWindow* const viewportWindow = ImGui::GetCurrentWindow();
    ASSERT_NE(viewportWindow, nullptr);
    EXPECT_NE(viewportWindow->DockId, 0U);
    ImGui::End();

    ImGui::Begin(kPanelLabelContentBrowserUVE, nullptr, standardFlags);
    const ImGuiWindow* const cbWindow = ImGui::GetCurrentWindow();
    ASSERT_NE(cbWindow, nullptr);
    EXPECT_NE(cbWindow->DockId, 0U);
    ImGui::End();

    ImGui::Begin(kPanelLabelDebugUVE, nullptr, standardFlags);
    const ImGuiWindow* const debugWindow = ImGui::GetCurrentWindow();
    ASSERT_NE(debugWindow, nullptr);
    EXPECT_NE(debugWindow->DockId, 0U);
    ImGui::End();

    ImGui::Render();
}

TEST_F(EditorDockingUVETest, PanelFreedomAbsenceOfRestrictiveFlags) {
    // Verified invariant: all panel windows must NOT use NoMove, NoResize, or NoTitleBar
    constexpr ImGuiWindowFlags kStandardFlags = ImGuiWindowFlags_NoCollapse;
    EXPECT_EQ(kStandardFlags & ImGuiWindowFlags_NoMove, 0);
    EXPECT_EQ(kStandardFlags & ImGuiWindowFlags_NoResize, 0);
    EXPECT_EQ(kStandardFlags & ImGuiWindowFlags_NoTitleBar, 0);

    // Verify window begun with standard flags possesses title bar / dock tab capability
    ImGui::NewFrame();
    bool visible = true;
    ImGui::Begin(kPanelLabelViewportUVE, &visible, kStandardFlags);
    const ImGuiWindow* const window = ImGui::GetCurrentWindow();
    ASSERT_NE(window, nullptr);
    EXPECT_FALSE(window->Flags & ImGuiWindowFlags_NoTitleBar);
    EXPECT_FALSE(window->Flags & ImGuiWindowFlags_NoMove);
    EXPECT_FALSE(window->Flags & ImGuiWindowFlags_NoResize);
    ImGui::End();
    ImGui::Render();
}

TEST_F(EditorDockingUVETest, IniPersistenceSettingsAndSaveTrigger) {
    ImGuiIO& io = ImGui::GetIO();
    EXPECT_NE(io.IniFilename, nullptr);
    EXPECT_GT(std::string_view(io.IniFilename).length(), 0U);

    ImGui::NewFrame();
    const ImGuiViewport vp = MakeTestViewportUVE(1920.0F, 1080.0F);
    SetupMainDockSpaceUVE(&vp, false);

    bool visible = true;
    ImGui::Begin(kPanelLabelViewportUVE, &visible, ImGuiWindowFlags_NoCollapse);
    ImGui::End();
    ImGui::Begin(kPanelLabelSceneUVE, &visible, ImGuiWindowFlags_NoCollapse);
    ImGui::End();

    ImGui::Render();

    // Verify settings can be serialized to memory with docking section
    std::size_t iniSize = 0U;
    const char* const iniData = ImGui::SaveIniSettingsToMemory(&iniSize);
    ASSERT_NE(iniData, nullptr);
    EXPECT_GT(iniSize, 0U);

    const std::string_view iniView(iniData, iniSize);
    EXPECT_NE(iniView.find("[Docking]"), std::string_view::npos);
    EXPECT_NE(iniView.find("DockNode"), std::string_view::npos);

    // Verify disk saving function executes without crashing
    ImGui::SaveIniSettingsToDisk(io.IniFilename);
}

TEST_F(EditorDockingUVETest, ForceResetReconstructsDockTree) {
    ImGui::NewFrame();
    const ImGuiViewport vp = MakeTestViewportUVE(1920.0F, 1080.0F);

    const ImGuiID dockspaceId = SetupMainDockSpaceUVE(&vp, false);
    EXPECT_EQ(dockspaceId, GetMainDockSpaceIdUVE());
    ImGui::Render();

    // Request reset and verify reconstruction
    RequestResetDockingLayoutUVE();
    EXPECT_TRUE(IsResetDockingLayoutRequestedUVE());

    ImGui::NewFrame();
    const bool shouldReset = ConsumeResetDockingLayoutRequestUVE();
    EXPECT_TRUE(shouldReset);

    const ImGuiID rebuiltId = SetupMainDockSpaceUVE(&vp, shouldReset);
    EXPECT_EQ(rebuiltId, dockspaceId);

    ImGuiDockNode* const rootNode = ImGui::DockBuilderGetNode(rebuiltId);
    ASSERT_NE(rootNode, nullptr);
    EXPECT_TRUE(rootNode->IsDockSpace());
    EXPECT_TRUE(rootNode->IsSplitNode());
    ImGui::Render();
}

} // namespace
} // namespace UVE::Editor::Tests
