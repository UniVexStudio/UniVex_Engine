#include "uve/editor/editor_theme_uve.h"

#include <imgui.h>

namespace UVE::Editor {

void ApplyEditorVisualThemeUVE() noexcept {
    ImGuiStyle& style = ImGui::GetStyle();
    // Tightened interior padding and row spacing so panel content sits closer to the panel edges
    // and list/tree/inspector rows are denser - the earlier looser values wasted vertical space
    // against the reference's tight rows.
    style.WindowPadding = ImVec2{6.0F, 4.0F};
    style.FramePadding = ImVec2{5.0F, 3.0F};
    style.ItemSpacing = ImVec2{5.0F, 2.0F};
    style.ItemInnerSpacing = ImVec2{4.0F, 3.0F};
    // Square corners throughout - the earlier "glass" look (8px/6px/4px rounding) read as soft
    // and inconsistent against real reference editors; every panel, popup, and control now shares
    // one flat, sharp-cornered language instead.
    style.WindowRounding = 0.0F;
    style.ChildRounding = 0.0F;
    style.FrameRounding = 0.0F;
    style.PopupRounding = 0.0F;
    style.ScrollbarRounding = 0.0F;
    style.GrabRounding = 0.0F;
    style.TabRounding = 0.0F;
    style.WindowBorderSize = 1.0F;
    style.ChildBorderSize = 1.0F;
    style.FrameBorderSize = 1.0F;
    style.PopupBorderSize = 1.0F;
    style.TabBorderSize = 1.0F;
    style.GrabMinSize = 11.0F;
    style.DisabledAlpha = 0.62F;

    ImVec4* const colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4{0.91F, 0.92F, 0.94F, 1.0F};
    colors[ImGuiCol_TextDisabled] = ImVec4{0.56F, 0.59F, 0.64F, 1.0F};
    // rgb retargeted to Cowork's mockup --app-bg #0a0c0f / --panel-bg rgba(22,25,30,.94) - the
    // exact hex values delivered for this editor's own real theme, ported verbatim.
    colors[ImGuiCol_WindowBg] = ImVec4{0.039F, 0.047F, 0.059F, 0.90F};
    colors[ImGuiCol_ChildBg] = ImVec4{0.086F, 0.098F, 0.118F, 0.94F};
    colors[ImGuiCol_PopupBg] = ImVec4{0.106F, 0.125F, 0.149F, 0.94F};
    colors[ImGuiCol_MenuBarBg] = ImVec4{0.106F, 0.125F, 0.149F, 0.94F};
    colors[ImGuiCol_TitleBg] = ImVec4{0.067F, 0.078F, 0.094F, 0.94F};
    colors[ImGuiCol_TitleBgActive] = ImVec4{0.137F, 0.165F, 0.196F, 0.96F};
    // Retargeted to Cowork's mockup --border-soft #2b3037 - the common seam color used for most
    // panel/frame edges in the mockup (the brighter --border #454e5c is reserved there only for
    // dropdown/popup outlines, a single-color distinction ImGui's one Border slot can't express
    // without extra per-widget PushStyleColor calls - accepted as an honest simplification).
    colors[ImGuiCol_Border] = ImVec4{0.169F, 0.188F, 0.216F, 1.0F};
    colors[ImGuiCol_BorderShadow] = ImVec4{0.0F, 0.0F, 0.0F, 0.55F};
    colors[ImGuiCol_FrameBg] = ImVec4{0.106F, 0.125F, 0.149F, 0.92F};
    colors[ImGuiCol_FrameBgHovered] = ImVec4{0.165F, 0.196F, 0.231F, 0.96F};
    colors[ImGuiCol_FrameBgActive] = ImVec4{0.247F, 0.310F, 0.373F, 1.0F};
    colors[ImGuiCol_Header] = ImVec4{0.165F, 0.196F, 0.231F, 0.92F};
    colors[ImGuiCol_HeaderHovered] = ImVec4{0.247F, 0.310F, 0.373F, 0.96F};
    colors[ImGuiCol_HeaderActive] = ImVec4{0.384F, 0.494F, 0.596F, 1.0F};
    colors[ImGuiCol_Button] = ImVec4{0.122F, 0.145F, 0.173F, 0.92F};
    colors[ImGuiCol_ButtonHovered] = ImVec4{0.200F, 0.239F, 0.286F, 0.96F};
    // Accent-driven active states retargeted to Cowork's mockup --accent #5b7a99 / --accent-bright
    // #8fb4d8 exact hex.
    colors[ImGuiCol_ButtonActive] = ImVec4{0.357F, 0.478F, 0.600F, 1.0F};
    colors[ImGuiCol_CheckMark] = ImVec4{0.561F, 0.706F, 0.847F, 1.0F};
    colors[ImGuiCol_SliderGrab] = ImVec4{0.43F, 0.52F, 0.62F, 1.0F};
    colors[ImGuiCol_SliderGrabActive] = ImVec4{0.561F, 0.706F, 0.847F, 1.0F};
    colors[ImGuiCol_Separator] = ImVec4{0.188F, 0.216F, 0.255F, 0.76F};
    colors[ImGuiCol_SeparatorHovered] = ImVec4{0.48F, 0.55F, 0.63F, 0.88F};
    colors[ImGuiCol_SeparatorActive] = ImVec4{0.64F, 0.72F, 0.82F, 1.0F};
    colors[ImGuiCol_Tab] = ImVec4{0.105F, 0.11F, 0.12F, 0.90F};
    colors[ImGuiCol_TabHovered] = ImVec4{0.25F, 0.28F, 0.32F, 0.96F};
    colors[ImGuiCol_TabActive] = ImVec4{0.205F, 0.23F, 0.26F, 0.96F};
    colors[ImGuiCol_TabUnfocused] = ImVec4{0.075F, 0.078F, 0.085F, 0.88F};
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4{0.13F, 0.14F, 0.155F, 0.92F};
    colors[ImGuiCol_ResizeGrip] = ImVec4{0.30F, 0.36F, 0.43F, 0.55F};
    colors[ImGuiCol_ResizeGripHovered] = ImVec4{0.50F, 0.60F, 0.71F, 0.80F};
    colors[ImGuiCol_ResizeGripActive] = ImVec4{0.67F, 0.77F, 0.88F, 0.98F};
    colors[ImGuiCol_ScrollbarBg] = ImVec4{0.035F, 0.037F, 0.042F, 0.88F};
    colors[ImGuiCol_ScrollbarGrab] = ImVec4{0.22F, 0.24F, 0.27F, 1.0F};
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4{0.36F, 0.40F, 0.45F, 1.0F};
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4{0.50F, 0.57F, 0.65F, 1.0F};
    colors[ImGuiCol_TextSelectedBg] = ImVec4{0.247F, 0.337F, 0.420F, 0.72F};
    colors[ImGuiCol_NavHighlight] = ImVec4{0.561F, 0.686F, 0.796F, 0.92F};
}

} // namespace UVE::Editor
