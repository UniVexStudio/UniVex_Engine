// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/prism_imgui_viewport_bridge_uve.h"
#include <imgui.h>

namespace UVE::Editor {

PrismImGuiViewportBridgeUVE::PrismImGuiViewportBridgeUVE() = default;

void PrismImGuiViewportBridgeUVE::SetRootWidget(std::shared_ptr<UI::Prism::PrismWidgetUVE> root) {
    m_rootWidget = std::move(root);
    m_eventRouter.SetRoot(m_rootWidget);
}

void PrismImGuiViewportBridgeUVE::RenderImGuiHost() {
    if (!m_rootWidget) return;

    const ImVec2 screenPos = ImGui::GetCursorScreenPos();
    const ImVec2 availSize = ImGui::GetContentRegionAvail();
    if (availSize.x <= 0.0F || availSize.y <= 0.0F) return;

    const Math::RectUVE hostRect{
        {screenPos.x, screenPos.y},
        {availSize.x, availSize.y}
    };

    // 1. Dual-Pass Deterministic Layout
    m_rootWidget->MeasureUVE(hostRect.size);
    m_rootWidget->ArrangeUVE(hostRect);

    // 2. Event Dispatch from ImGui IO
    ImGuiIO& io = ImGui::GetIO();
    const Math::Vector2UVE cursorPos{io.MousePos.x, io.MousePos.y};

    const bool isHovered = Math::ContainsUVE(hostRect, cursorPos);
    if (isHovered || io.MouseDown[0] || io.MouseDown[1]) {
        // Pointer Move
        m_eventRouter.DispatchPointerMove(UI::Prism::PrismPointerEventUVE{
            cursorPos,
            Math::Vector2UVE{io.MouseDelta.x, io.MouseDelta.y}
        });

        // Pointer Down / Up (Left Click)
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_eventRouter.DispatchPointerDown(UI::Prism::PrismPointerEventUVE{
                cursorPos, {}, Input::MouseButtonUVE::Left
            });
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_eventRouter.DispatchPointerUp(UI::Prism::PrismPointerEventUVE{
                cursorPos, {}, Input::MouseButtonUVE::Left
            });
        }

        // Scroll Wheel
        if (io.MouseWheel != 0.0F || io.MouseWheelH != 0.0F) {
            m_eventRouter.DispatchScroll(UI::Prism::PrismScrollEventUVE{
                cursorPos,
                Math::Vector2UVE{io.MouseWheelH, io.MouseWheel}
            });
        }

        // Character Input
        for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
            const char32_t c = io.InputQueueCharacters[i];
            m_eventRouter.DispatchCharInput(c);
        }

        // Key Down / Navigation
        if (ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            m_eventRouter.DispatchKeyDown(UI::Prism::PrismKeyEventUVE{Input::KeyCodeUVE::Enter});
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
            m_eventRouter.DispatchKeyDown(UI::Prism::PrismKeyEventUVE{Input::KeyCodeUVE::Backspace});
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_eventRouter.DispatchKeyDown(UI::Prism::PrismKeyEventUVE{Input::KeyCodeUVE::Escape});
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            m_eventRouter.DispatchKeyDown(UI::Prism::PrismKeyEventUVE{Input::KeyCodeUVE::Up});
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            m_eventRouter.DispatchKeyDown(UI::Prism::PrismKeyEventUVE{Input::KeyCodeUVE::Down});
        }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            m_eventRouter.DispatchKeyDown(UI::Prism::PrismKeyEventUVE{Input::KeyCodeUVE::Left});
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            m_eventRouter.DispatchKeyDown(UI::Prism::PrismKeyEventUVE{Input::KeyCodeUVE::Right});
        }
    }

    // 3. Paint into PrismDrawList
    m_drawList.Clear();
    m_rootWidget->PaintUVE(m_drawList);

    // 4. Translate PrismDrawList commands into ImDrawList
    ImDrawList* const imDraw = ImGui::GetWindowDrawList();
    const auto& vertices = m_drawList.GetVertices();
    const auto& indices = m_drawList.GetIndices();
    const auto& commands = m_drawList.GetCommands();

    for (const auto& cmd : commands) {
        if (cmd.indexCount == 0) continue;

        // Push Scissor Rect if bounded
        const bool hasClip = (cmd.clipRect.size.x > 0 && cmd.clipRect.size.y > 0);
        if (hasClip) {
            const ImVec2 clipMin{
                static_cast<float>(cmd.clipRect.position.x),
                static_cast<float>(cmd.clipRect.position.y)
            };
            const ImVec2 clipMax{
                static_cast<float>(cmd.clipRect.position.x + cmd.clipRect.size.x),
                static_cast<float>(cmd.clipRect.position.y + cmd.clipRect.size.y)
            };
            imDraw->PushClipRect(clipMin, clipMax, true);
        }

        // Submit triangle list
        for (std::size_t i = 0; i < cmd.indexCount; i += 3) {
            const std::uint32_t idx0 = indices[cmd.indexOffset + i];
            const std::uint32_t idx1 = indices[cmd.indexOffset + i + 1];
            const std::uint32_t idx2 = indices[cmd.indexOffset + i + 2];

            const auto& v0 = vertices[idx0];
            const auto& v1 = vertices[idx1];
            const auto& v2 = vertices[idx2];

            const ImVec2 p0{v0.position.x, v0.position.y};
            const ImVec2 p1{v1.position.x, v1.position.y};
            const ImVec2 p2{v2.position.x, v2.position.y};

            const ImU32 col0 = IM_COL32(
                static_cast<int>(v0.color.r * 255.0F),
                static_cast<int>(v0.color.g * 255.0F),
                static_cast<int>(v0.color.b * 255.0F),
                static_cast<int>(v0.color.a * 255.0F)
            );

            imDraw->AddTriangleFilled(p0, p1, p2, col0);
        }

        if (hasClip) {
            imDraw->PopClipRect();
        }
    }

    // Advance ImGui layout by reserving space
    ImGui::Dummy(availSize);
}

} // namespace UVE::Editor
