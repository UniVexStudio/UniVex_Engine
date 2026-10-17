#include "uve/editor/editor_theme_uve.h"

#include <imgui.h>
#include "uve/editor/editor_settings_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Editor {

void ApplyEditorVisualThemeUVE(const EditorExtendedPreferencesUVE* const preferences) noexcept {
    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }
    ImGuiStyle& style = ImGui::GetStyle();

    const float rounding = preferences != nullptr
                               ? static_cast<float>(std::clamp(preferences->cornerRadiusPx, 0.0, 32.0))
                               : 5.0F;
    const std::int64_t density = preferences != nullptr ? preferences->spacingDensity : 1;

    // Spacing density: 0=Compact, 1=Comfortable
    if (density == 0) {
        style.WindowPadding = ImVec2{4.0F, 3.0F};
        style.FramePadding = ImVec2{4.0F, 1.5F};
        style.ItemSpacing = ImVec2{4.0F, 1.5F};
        style.ItemInnerSpacing = ImVec2{3.0F, 2.0F};
    } else {
        style.WindowPadding = ImVec2{6.0F, 4.0F};
        style.FramePadding = ImVec2{5.0F, 2.0F};
        style.ItemSpacing = ImVec2{5.0F, 2.0F};
        style.ItemInnerSpacing = ImVec2{4.0F, 3.0F};
    }
    style.ScrollbarSize = 9.0F;
    style.WindowRounding = rounding;
    style.ChildRounding = rounding;
    style.FrameRounding = rounding;
    style.PopupRounding = rounding;
    style.ScrollbarRounding = rounding;
    style.GrabRounding = rounding;
    style.TabRounding = rounding;
    style.WindowBorderSize = 1.0F;
    style.ChildBorderSize = 1.0F;
    style.FrameBorderSize = 1.0F;
    style.PopupBorderSize = 1.0F;
    style.TabBorderSize = 1.0F;
    style.GrabMinSize = 11.0F;
    style.DisabledAlpha = 0.62F;

    const std::int64_t preset = preferences != nullptr ? preferences->themePreset : 0;
    if (preset == 1) {
        // Preset 1: Light Theme
        ImGui::StyleColorsLight();
        style.WindowRounding = rounding;
        style.ChildRounding = rounding;
        style.FrameRounding = rounding;
        style.PopupRounding = rounding;
        style.ScrollbarRounding = rounding;
        style.GrabRounding = rounding;
        style.TabRounding = rounding;
        return;
    }

    ImVec4* const colors = style.Colors;
    if (preset == 2) {
        // Preset 2: High Contrast Theme
        colors[ImGuiCol_Text] = ImVec4{1.0F, 1.0F, 1.0F, 1.0F};
        colors[ImGuiCol_TextDisabled] = ImVec4{0.70F, 0.70F, 0.70F, 1.0F};
        colors[ImGuiCol_WindowBg] = ImVec4{0.0F, 0.0F, 0.0F, 1.0F};
        colors[ImGuiCol_ChildBg] = ImVec4{0.05F, 0.05F, 0.05F, 1.0F};
        colors[ImGuiCol_PopupBg] = ImVec4{0.05F, 0.05F, 0.05F, 1.0F};
        colors[ImGuiCol_MenuBarBg] = ImVec4{0.05F, 0.05F, 0.05F, 1.0F};
        colors[ImGuiCol_TitleBg] = ImVec4{0.10F, 0.10F, 0.10F, 1.0F};
        colors[ImGuiCol_TitleBgActive] = ImVec4{0.20F, 0.20F, 0.20F, 1.0F};
        colors[ImGuiCol_Border] = ImVec4{0.80F, 0.80F, 0.80F, 1.0F};
        colors[ImGuiCol_BorderShadow] = ImVec4{0.0F, 0.0F, 0.0F, 1.0F};
        colors[ImGuiCol_FrameBg] = ImVec4{0.10F, 0.10F, 0.10F, 1.0F};
        colors[ImGuiCol_FrameBgHovered] = ImVec4{0.25F, 0.25F, 0.25F, 1.0F};
        colors[ImGuiCol_FrameBgActive] = ImVec4{0.35F, 0.35F, 0.35F, 1.0F};
        colors[ImGuiCol_Header] = ImVec4{0.15F, 0.15F, 0.15F, 1.0F};
        colors[ImGuiCol_HeaderHovered] = ImVec4{0.30F, 0.30F, 0.30F, 1.0F};
        colors[ImGuiCol_HeaderActive] = ImVec4{0.40F, 0.40F, 0.40F, 1.0F};
        colors[ImGuiCol_Button] = ImVec4{0.15F, 0.15F, 0.15F, 1.0F};
        colors[ImGuiCol_ButtonHovered] = ImVec4{0.30F, 0.30F, 0.30F, 1.0F};
        colors[ImGuiCol_ButtonActive] = ImVec4{0.50F, 0.50F, 0.50F, 1.0F};
        colors[ImGuiCol_CheckMark] = ImVec4{1.0F, 1.0F, 0.0F, 1.0F};
        colors[ImGuiCol_SliderGrab] = ImVec4{0.60F, 0.60F, 0.60F, 1.0F};
        colors[ImGuiCol_SliderGrabActive] = ImVec4{1.0F, 1.0F, 0.0F, 1.0F};
        colors[ImGuiCol_Separator] = ImVec4{0.70F, 0.70F, 0.70F, 1.0F};
        colors[ImGuiCol_SeparatorHovered] = ImVec4{0.90F, 0.90F, 0.90F, 1.0F};
        colors[ImGuiCol_SeparatorActive] = ImVec4{1.0F, 1.0F, 0.0F, 1.0F};
        colors[ImGuiCol_Tab] = ImVec4{0.15F, 0.15F, 0.15F, 1.0F};
        colors[ImGuiCol_TabHovered] = ImVec4{0.35F, 0.35F, 0.35F, 1.0F};
        colors[ImGuiCol_TabSelected] = ImVec4{0.30F, 0.30F, 0.30F, 1.0F};
        colors[ImGuiCol_TabDimmed] = ImVec4{0.08F, 0.08F, 0.08F, 1.0F};
        colors[ImGuiCol_TabDimmedSelected] = ImVec4{0.20F, 0.20F, 0.20F, 1.0F};
        colors[ImGuiCol_ResizeGrip] = ImVec4{0.60F, 0.60F, 0.60F, 0.8F};
        colors[ImGuiCol_ResizeGripHovered] = ImVec4{0.80F, 0.80F, 0.80F, 1.0F};
        colors[ImGuiCol_ResizeGripActive] = ImVec4{1.0F, 1.0F, 0.0F, 1.0F};
        colors[ImGuiCol_ScrollbarBg] = ImVec4{0.02F, 0.02F, 0.02F, 1.0F};
        colors[ImGuiCol_ScrollbarGrab] = ImVec4{0.40F, 0.40F, 0.40F, 1.0F};
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4{0.60F, 0.60F, 0.60F, 1.0F};
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4{0.80F, 0.80F, 0.80F, 1.0F};
        colors[ImGuiCol_TextSelectedBg] = ImVec4{0.30F, 0.30F, 0.80F, 0.8F};
        colors[ImGuiCol_NavCursor] = ImVec4{1.0F, 1.0F, 0.0F, 1.0F};
        return;
    }

    // Preset 0 (Dark) and Preset 3 (Custom): Dynamic layered charcoal palette with customizable accent/base
    const float contrast = preferences != nullptr
                               ? static_cast<float>(std::clamp(preferences->themeContrast, 0.5, 2.0))
                               : 1.0F;

    // Base surface reference tint
    const float baseR = preferences != nullptr ? preferences->themeBaseColor.r : 0.14F;
    const float baseG = preferences != nullptr ? preferences->themeBaseColor.g : 0.15F;
    const float baseB = preferences != nullptr ? preferences->themeBaseColor.b : 0.18F;

    // Accent reference tint
    const float accR = preferences != nullptr ? preferences->themeAccentColor.r : 0.22F;
    const float accG = preferences != nullptr ? preferences->themeAccentColor.g : 0.56F;
    const float accB = preferences != nullptr ? preferences->themeAccentColor.b : 0.96F;

    // Helpers to blend neutral charcoal with base/accent color
    const auto makeBase = [baseR, baseG, baseB, contrast](const float neutral, const float baseWeight) noexcept {
        const float val = ((neutral * (1.0F - baseWeight)) + (baseR * baseWeight)) * contrast;
        return std::clamp(val, 0.0F, 1.0F);
    };

    colors[ImGuiCol_Text] = ImVec4{0.89F, 0.90F, 0.92F, 1.0F};
    colors[ImGuiCol_TextDisabled] = ImVec4{0.48F, 0.51F, 0.55F, 1.0F};
    colors[ImGuiCol_WindowBg] = ImVec4{makeBase(0.043F, 0.15F), makeBase(0.047F, 0.15F), makeBase(0.051F, 0.15F), 0.98F};
    colors[ImGuiCol_ChildBg] = ImVec4{makeBase(0.106F, 0.20F), makeBase(0.118F, 0.20F), makeBase(0.129F, 0.20F), 1.0F};
    colors[ImGuiCol_PopupBg] = ImVec4{makeBase(0.125F, 0.20F), makeBase(0.137F, 0.20F), makeBase(0.149F, 0.20F), 1.0F};
    colors[ImGuiCol_MenuBarBg] = ImVec4{makeBase(0.106F, 0.20F), makeBase(0.118F, 0.20F), makeBase(0.129F, 0.20F), 1.0F};
    colors[ImGuiCol_TitleBg] = ImVec4{makeBase(0.075F, 0.15F), makeBase(0.082F, 0.15F), makeBase(0.090F, 0.15F), 1.0F};
    colors[ImGuiCol_TitleBgActive] = ImVec4{makeBase(0.137F, 0.25F), makeBase(0.153F, 0.25F), makeBase(0.169F, 0.25F), 1.0F};
    colors[ImGuiCol_Border] = ImVec4{0.220F * contrast, 0.239F * contrast, 0.259F * contrast, 1.0F};
    colors[ImGuiCol_BorderShadow] = ImVec4{0.0F, 0.0F, 0.0F, 0.65F};

    colors[ImGuiCol_FrameBg] = ImVec4{makeBase(0.055F, 0.15F), makeBase(0.063F, 0.15F), makeBase(0.071F, 0.15F), 1.0F};
    colors[ImGuiCol_FrameBgHovered] = ImVec4{makeBase(0.102F, 0.20F), makeBase(0.114F, 0.20F), makeBase(0.125F, 0.20F), 1.0F};
    colors[ImGuiCol_FrameBgActive] = ImVec4{makeBase(0.145F, 0.25F), makeBase(0.165F, 0.25F), makeBase(0.184F, 0.25F), 1.0F};
    colors[ImGuiCol_Header] = ImVec4{makeBase(0.055F, 0.15F), makeBase(0.063F, 0.15F), makeBase(0.071F, 0.15F), 1.0F};
    colors[ImGuiCol_HeaderHovered] = ImVec4{makeBase(0.106F, 0.20F), makeBase(0.118F, 0.20F), makeBase(0.129F, 0.20F), 1.0F};
    colors[ImGuiCol_HeaderActive] = ImVec4{makeBase(0.161F, 0.25F), makeBase(0.188F, 0.25F), makeBase(0.216F, 0.25F), 1.0F};
    colors[ImGuiCol_Button] = ImVec4{makeBase(0.145F, 0.20F), makeBase(0.165F, 0.20F), makeBase(0.184F, 0.20F), 1.0F};
    colors[ImGuiCol_ButtonHovered] = ImVec4{makeBase(0.208F, 0.25F), makeBase(0.239F, 0.25F), makeBase(0.271F, 0.25F), 1.0F};
    colors[ImGuiCol_ButtonActive] = ImVec4{accR * 0.70F, accG * 0.70F, accB * 0.70F, 1.0F};
    colors[ImGuiCol_CheckMark] = ImVec4{accR, accG, accB, 1.0F};
    colors[ImGuiCol_SliderGrab] = ImVec4{0.310F, 0.365F, 0.420F, 1.0F};
    colors[ImGuiCol_SliderGrabActive] = ImVec4{accR, accG, accB, 1.0F};
    colors[ImGuiCol_Separator] = ImVec4{0.196F, 0.216F, 0.235F, 1.0F};
    colors[ImGuiCol_SeparatorHovered] = ImVec4{0.329F, 0.380F, 0.431F, 1.0F};
    colors[ImGuiCol_SeparatorActive] = ImVec4{accR, accG, accB, 1.0F};
    colors[ImGuiCol_Tab] = ImVec4{makeBase(0.102F, 0.15F), makeBase(0.114F, 0.15F), makeBase(0.125F, 0.15F), 1.0F};
    colors[ImGuiCol_TabHovered] = ImVec4{makeBase(0.204F, 0.25F), makeBase(0.231F, 0.25F), makeBase(0.259F, 0.25F), 1.0F};
    colors[ImGuiCol_TabSelected] = ImVec4{makeBase(0.161F, 0.30F), makeBase(0.188F, 0.30F), makeBase(0.216F, 0.30F), 1.0F};
    colors[ImGuiCol_TabDimmed] = ImVec4{makeBase(0.067F, 0.10F), makeBase(0.071F, 0.10F), makeBase(0.078F, 0.10F), 1.0F};
    colors[ImGuiCol_TabDimmedSelected] = ImVec4{makeBase(0.122F, 0.20F), makeBase(0.137F, 0.20F), makeBase(0.153F, 0.20F), 1.0F};
    colors[ImGuiCol_ResizeGrip] = ImVec4{0.267F, 0.302F, 0.337F, 0.55F};
    colors[ImGuiCol_ResizeGripHovered] = ImVec4{0.416F, 0.478F, 0.541F, 0.80F};
    colors[ImGuiCol_ResizeGripActive] = ImVec4{accR, accG, accB, 0.98F};
    colors[ImGuiCol_ScrollbarBg] = ImVec4{makeBase(0.035F, 0.10F), makeBase(0.039F, 0.10F), makeBase(0.043F, 0.10F), 1.0F};
    colors[ImGuiCol_ScrollbarGrab] = ImVec4{0.180F, 0.196F, 0.216F, 1.0F};
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4{0.302F, 0.333F, 0.365F, 1.0F};
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4{accR * 0.8F, accG * 0.8F, accB * 0.8F, 1.0F};
    colors[ImGuiCol_TextSelectedBg] = ImVec4{accR * 0.6F, accG * 0.6F, accB * 0.6F, 0.72F};
    colors[ImGuiCol_NavCursor] = ImVec4{accR, accG, accB, 0.92F};
}

} // namespace UVE::Editor
