#include "uve/editor/editor_theme_uve.h"

#include <imgui.h>

namespace UVE::Editor {

void ApplyEditorVisualThemeUVE() noexcept {
    ImGuiStyle& style = ImGui::GetStyle();
    // Compact professional-editor spacing: dense rows, clear hierarchy, and no soft "glass" look.
    style.WindowPadding = ImVec2{6.0F, 4.0F};
    style.FramePadding = ImVec2{5.0F, 2.0F};
    style.ItemSpacing = ImVec2{5.0F, 2.0F};
    style.ItemInnerSpacing = ImVec2{4.0F, 3.0F};
    style.ScrollbarSize = 9.0F;
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
    // Layered charcoal palette: deepest surfaces are reserved for numeric fields, consoles, and
    // empty work areas; rows and panels step upward so hierarchy remains readable without blue tint.
    colors[ImGuiCol_Text] = ImVec4{0.89F, 0.90F, 0.92F, 1.0F};              // #E3E5EA
    colors[ImGuiCol_TextDisabled] = ImVec4{0.48F, 0.51F, 0.55F, 1.0F};      // #7A828C
    colors[ImGuiCol_WindowBg] = ImVec4{0.043F, 0.047F, 0.051F, 0.98F};     // #0B0C0D
    colors[ImGuiCol_ChildBg] = ImVec4{0.106F, 0.118F, 0.129F, 1.0F};       // #1B1E21
    colors[ImGuiCol_PopupBg] = ImVec4{0.125F, 0.137F, 0.149F, 1.0F};       // #202327
    colors[ImGuiCol_MenuBarBg] = ImVec4{0.106F, 0.118F, 0.129F, 1.0F};     // #1B1E21
    colors[ImGuiCol_TitleBg] = ImVec4{0.075F, 0.082F, 0.090F, 1.0F};       // #131517
    colors[ImGuiCol_TitleBgActive] = ImVec4{0.137F, 0.153F, 0.169F, 1.0F}; // #23272B
    colors[ImGuiCol_Border] = ImVec4{0.220F, 0.239F, 0.259F, 1.0F};       // #383D42
    colors[ImGuiCol_BorderShadow] = ImVec4{0.0F, 0.0F, 0.0F, 0.65F};
    // Recessed controls: numeric fields and collapsible Inspector rows are the deepest layer.
    colors[ImGuiCol_FrameBg] = ImVec4{0.055F, 0.063F, 0.071F, 1.0F};       // #0E1012
    colors[ImGuiCol_FrameBgHovered] = ImVec4{0.102F, 0.114F, 0.125F, 1.0F}; // #1A1D20
    colors[ImGuiCol_FrameBgActive] = ImVec4{0.145F, 0.165F, 0.184F, 1.0F};  // #252A2F
    colors[ImGuiCol_Header] = ImVec4{0.055F, 0.063F, 0.071F, 1.0F};        // #0E1012
    colors[ImGuiCol_HeaderHovered] = ImVec4{0.106F, 0.118F, 0.129F, 1.0F}; // #1B1E21
    colors[ImGuiCol_HeaderActive] = ImVec4{0.161F, 0.188F, 0.216F, 1.0F};  // #293036
    colors[ImGuiCol_Button] = ImVec4{0.145F, 0.165F, 0.184F, 1.0F};        // #252A2F
    colors[ImGuiCol_ButtonHovered] = ImVec4{0.208F, 0.239F, 0.271F, 1.0F}; // #353D45
    colors[ImGuiCol_ButtonActive] = ImVec4{0.286F, 0.376F, 0.471F, 1.0F};  // #496078
    colors[ImGuiCol_CheckMark] = ImVec4{0.392F, 0.620F, 0.816F, 1.0F};     // #649ED0
    colors[ImGuiCol_SliderGrab] = ImVec4{0.310F, 0.365F, 0.420F, 1.0F};    // #4F5D6B
    colors[ImGuiCol_SliderGrabActive] = ImVec4{0.392F, 0.620F, 0.816F, 1.0F};
    colors[ImGuiCol_Separator] = ImVec4{0.196F, 0.216F, 0.235F, 1.0F};    // #32373C
    colors[ImGuiCol_SeparatorHovered] = ImVec4{0.329F, 0.380F, 0.431F, 1.0F};
    colors[ImGuiCol_SeparatorActive] = ImVec4{0.392F, 0.478F, 0.565F, 1.0F};
    colors[ImGuiCol_Tab] = ImVec4{0.102F, 0.114F, 0.125F, 1.0F};           // #1A1D20
    colors[ImGuiCol_TabHovered] = ImVec4{0.204F, 0.231F, 0.259F, 1.0F};    // #343B42
    colors[ImGuiCol_TabActive] = ImVec4{0.161F, 0.188F, 0.216F, 1.0F};     // #293036
    colors[ImGuiCol_TabUnfocused] = ImVec4{0.067F, 0.071F, 0.078F, 1.0F};  // #111214
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4{0.122F, 0.137F, 0.153F, 1.0F};
    colors[ImGuiCol_ResizeGrip] = ImVec4{0.267F, 0.302F, 0.337F, 0.55F};
    colors[ImGuiCol_ResizeGripHovered] = ImVec4{0.416F, 0.478F, 0.541F, 0.80F};
    colors[ImGuiCol_ResizeGripActive] = ImVec4{0.580F, 0.686F, 0.792F, 0.98F};
    colors[ImGuiCol_ScrollbarBg] = ImVec4{0.035F, 0.039F, 0.043F, 1.0F};  // #090A0B
    colors[ImGuiCol_ScrollbarGrab] = ImVec4{0.180F, 0.196F, 0.216F, 1.0F};
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4{0.302F, 0.333F, 0.365F, 1.0F};
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4{0.424F, 0.471F, 0.518F, 1.0F};
    colors[ImGuiCol_TextSelectedBg] = ImVec4{0.239F, 0.361F, 0.478F, 0.72F};
    colors[ImGuiCol_NavHighlight] = ImVec4{0.392F, 0.620F, 0.816F, 0.92F};
}

} // namespace UVE::Editor
