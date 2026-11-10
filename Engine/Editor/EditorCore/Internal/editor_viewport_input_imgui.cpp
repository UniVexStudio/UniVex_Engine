// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_viewport_input_imgui.h"

#include <imgui.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace UVE::Editor {
namespace {

using ButtonUVE = EditorViewportInputUVE::ButtonUVE;
using KeyUVE = EditorViewportInputUVE::KeyUVE;

constexpr std::array<std::pair<KeyUVE, ImGuiKey>, EditorViewportInputUVE::kKeyCountUVE> kKeyMapUVE{{
    {KeyUVE::Num0, ImGuiKey_0}, {KeyUVE::Num1, ImGuiKey_1}, {KeyUVE::Num2, ImGuiKey_2},
    {KeyUVE::Num3, ImGuiKey_3}, {KeyUVE::Num4, ImGuiKey_4}, {KeyUVE::Num5, ImGuiKey_5},
    {KeyUVE::Num6, ImGuiKey_6}, {KeyUVE::Num7, ImGuiKey_7}, {KeyUVE::Num8, ImGuiKey_8},
    {KeyUVE::Num9, ImGuiKey_9},
    {KeyUVE::Keypad1, ImGuiKey_Keypad1}, {KeyUVE::Keypad3, ImGuiKey_Keypad3},
    {KeyUVE::Keypad5, ImGuiKey_Keypad5}, {KeyUVE::Keypad7, ImGuiKey_Keypad7},
    {KeyUVE::A, ImGuiKey_A}, {KeyUVE::C, ImGuiKey_C}, {KeyUVE::D, ImGuiKey_D},
    {KeyUVE::F, ImGuiKey_F}, {KeyUVE::S, ImGuiKey_S}, {KeyUVE::W, ImGuiKey_W},
    {KeyUVE::Space, ImGuiKey_Space}, {KeyUVE::Home, ImGuiKey_Home}, {KeyUVE::Escape, ImGuiKey_Escape},
    {KeyUVE::LeftShift, ImGuiKey_LeftShift}, {KeyUVE::LeftCtrl, ImGuiKey_LeftCtrl},
}};

constexpr std::array<ImGuiMouseButton, EditorViewportInputUVE::kButtonCountUVE> kButtonMapUVE{
    ImGuiMouseButton_Left, ImGuiMouseButton_Right, ImGuiMouseButton_Middle};

} // namespace

EditorViewportInputUVE CaptureImGuiViewportInputUVE() {
    const ImGuiIO& io = ImGui::GetIO();
    EditorViewportInputUVE input{};
    input.hovered = ImGui::IsWindowHovered();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    input.imageOrigin = Math::Vector2UVE{origin.x, origin.y};
    input.mousePos = Math::Vector2UVE{io.MousePos.x, io.MousePos.y};
    input.mouseDelta = Math::Vector2UVE{io.MouseDelta.x, io.MouseDelta.y};
    input.wheel = io.MouseWheel;
    input.deltaTime = io.DeltaTime;
    input.ctrl = io.KeyCtrl;
    input.alt = io.KeyAlt;
    input.shift = io.KeyShift;
    input.wantTextInput = io.WantTextInput;
    for (std::size_t i = 0; i < kButtonMapUVE.size(); ++i) {
        const ImGuiMouseButton button = kButtonMapUVE[i];
        input.down[i] = ImGui::IsMouseDown(button);
        input.clicked[i] = ImGui::IsMouseClicked(button);
        input.released[i] = ImGui::IsMouseReleased(button);
        input.pressPos[i] = Math::Vector2UVE{io.MouseClickedPos[button].x, io.MouseClickedPos[button].y};
        input.dragDistance[i] = std::sqrt(io.MouseDragMaxDistanceSqr[button]);
    }
    for (const auto& [key, imguiKey] : kKeyMapUVE) {
        const auto index = static_cast<std::size_t>(key);
        input.keysDown.set(index, ImGui::IsKeyDown(imguiKey));
        input.keysPressed.set(index, ImGui::IsKeyPressed(imguiKey, false));
    }
    return input;
}

void ApplyImGuiViewportCursorUVE(const EditorViewportInputUVE& input) {
    if (!input.cursorRequested) {
        return;
    }
    using CursorUVE = EditorViewportInputUVE::CursorUVE;
    ImGuiMouseCursor cursor = ImGuiMouseCursor_Arrow;
    switch (input.requestedCursor) {
        case CursorUVE::Arrow: cursor = ImGuiMouseCursor_Arrow; break;
        case CursorUVE::Hand: cursor = ImGuiMouseCursor_Hand; break;
        case CursorUVE::ResizeAll: cursor = ImGuiMouseCursor_ResizeAll; break;
        case CursorUVE::ResizeNS: cursor = ImGuiMouseCursor_ResizeNS; break;
        case CursorUVE::ResizeEW: cursor = ImGuiMouseCursor_ResizeEW; break;
        case CursorUVE::ResizeNWSE: cursor = ImGuiMouseCursor_ResizeNWSE; break;
        case CursorUVE::ResizeNESW: cursor = ImGuiMouseCursor_ResizeNESW; break;
    }
    ImGui::SetMouseCursor(cursor);
}

} // namespace UVE::Editor
