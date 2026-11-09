#include "editor_theme.h"

#include <imgui.h>
#include "editor_settings.h"

#include <algorithm>
#include <cmath>

namespace UVE::Editor {

void ApplyEditorVisualThemeUVE(const EditorExtendedPreferencesUVE* const preferences) noexcept {
    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }
    ImGuiStyle& style = ImGui::GetStyle();

    const float frameRounding = preferences != nullptr
                                    ? static_cast<float>(std::clamp(preferences->cornerRadiusPx, 0.0, 16.0))
                                    : 2.5F;
    const std::int64_t density = preferences != nullptr ? preferences->spacingDensity : 1;

    // Spacing density: 0=Compact, 1=Comfortable
    if (density == 0) {
        style.WindowPadding = ImVec2{4.0F, 3.0F};
        style.FramePadding = ImVec2{4.0F, 2.0F};
        style.ItemSpacing = ImVec2{4.0F, 2.0F};
        style.ItemInnerSpacing = ImVec2{3.0F, 2.0F};
    } else {
        style.WindowPadding = ImVec2{6.0F, 5.0F};
        style.FramePadding = ImVec2{6.0F, 3.5F};
        style.ItemSpacing = ImVec2{6.0F, 3.5F};
        style.ItemInnerSpacing = ImVec2{4.0F, 3.0F};
    }
    style.ScrollbarSize = 10.0F;
    // Unreal Engine 5 design: dock panels and windows form a flat seamless grid (0px rounding),
    // while interactive controls (inputs, buttons) use a subtle micro-chamfer.
    style.WindowRounding = 0.0F;
    style.ChildRounding = 0.0F;
    style.FrameRounding = frameRounding;
    style.PopupRounding = 3.0F;
    style.ScrollbarRounding = 2.0F;
    style.GrabRounding = 2.0F;
    style.TabRounding = 2.0F;
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
        style.WindowRounding = 0.0F;
        style.ChildRounding = 0.0F;
        style.FrameRounding = frameRounding;
        style.PopupRounding = 3.0F;
        style.ScrollbarRounding = 2.0F;
        style.GrabRounding = 2.0F;
        style.TabRounding = 2.0F;
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
    const float baseR = preferences != nullptr ? preferences->themeBaseColor.r : 0.12F;
    const float baseG = preferences != nullptr ? preferences->themeBaseColor.g : 0.13F;
    const float baseB = preferences != nullptr ? preferences->themeBaseColor.b : 0.15F;

    // Accent reference tint (Unreal Electric Blue #0e70e3)
    const float accR = preferences != nullptr ? preferences->themeAccentColor.r : 0.055F;
    const float accG = preferences != nullptr ? preferences->themeAccentColor.g : 0.440F;
    const float accB = preferences != nullptr ? preferences->themeAccentColor.b : 0.890F;

    // Helpers to blend neutral charcoal with base/accent color
    const auto makeBase = [baseR, baseG, baseB, contrast](const float neutral, const float baseWeight) noexcept {
        const float val = ((neutral * (1.0F - baseWeight)) + (baseR * baseWeight)) * contrast;
        return std::clamp(val, 0.0F, 1.0F);
    };

    colors[ImGuiCol_Text] = ImVec4{0.91F, 0.92F, 0.94F, 1.0F};
    colors[ImGuiCol_TextDisabled] = ImVec4{0.50F, 0.53F, 0.58F, 1.0F};
    colors[ImGuiCol_WindowBg] = ImVec4{makeBase(0.048F, 0.12F), makeBase(0.052F, 0.12F), makeBase(0.058F, 0.12F), 1.0F};
    colors[ImGuiCol_ChildBg] = ImVec4{makeBase(0.082F, 0.15F), makeBase(0.088F, 0.15F), makeBase(0.098F, 0.15F), 1.0F};
    colors[ImGuiCol_PopupBg] = ImVec4{makeBase(0.098F, 0.15F), makeBase(0.106F, 0.15F), makeBase(0.118F, 0.15F), 1.0F};
    colors[ImGuiCol_MenuBarBg] = ImVec4{makeBase(0.075F, 0.12F), makeBase(0.080F, 0.12F), makeBase(0.088F, 0.12F), 1.0F};
    colors[ImGuiCol_TitleBg] = ImVec4{makeBase(0.065F, 0.12F), makeBase(0.070F, 0.12F), makeBase(0.078F, 0.12F), 1.0F};
    colors[ImGuiCol_TitleBgActive] = ImVec4{makeBase(0.100F, 0.18F), makeBase(0.108F, 0.18F), makeBase(0.120F, 0.18F), 1.0F};
    colors[ImGuiCol_Border] = ImVec4{0.145F * contrast, 0.155F * contrast, 0.170F * contrast, 1.0F};
    colors[ImGuiCol_BorderShadow] = ImVec4{0.0F, 0.0F, 0.0F, 0.40F};

    colors[ImGuiCol_FrameBg] = ImVec4{makeBase(0.040F, 0.10F), makeBase(0.043F, 0.10F), makeBase(0.048F, 0.10F), 1.0F};
    colors[ImGuiCol_FrameBgHovered] = ImVec4{makeBase(0.085F, 0.15F), makeBase(0.092F, 0.15F), makeBase(0.105F, 0.15F), 1.0F};
    colors[ImGuiCol_FrameBgActive] = ImVec4{makeBase(0.115F, 0.20F), makeBase(0.125F, 0.20F), makeBase(0.140F, 0.20F), 1.0F};
    colors[ImGuiCol_Header] = ImVec4{makeBase(0.105F, 0.15F), makeBase(0.115F, 0.15F), makeBase(0.130F, 0.15F), 1.0F};
    colors[ImGuiCol_HeaderHovered] = ImVec4{makeBase(0.155F, 0.20F), makeBase(0.170F, 0.20F), makeBase(0.195F, 0.20F), 1.0F};
    colors[ImGuiCol_HeaderActive] = ImVec4{makeBase(0.190F, 0.25F), makeBase(0.210F, 0.25F), makeBase(0.240F, 0.25F), 1.0F};
    colors[ImGuiCol_Button] = ImVec4{makeBase(0.115F, 0.15F), makeBase(0.125F, 0.15F), makeBase(0.138F, 0.15F), 1.0F};
    colors[ImGuiCol_ButtonHovered] = ImVec4{makeBase(0.165F, 0.20F), makeBase(0.180F, 0.20F), makeBase(0.205F, 0.20F), 1.0F};
    colors[ImGuiCol_ButtonActive] = ImVec4{accR * 0.70F, accG * 0.70F, accB * 0.70F, 1.0F};
    colors[ImGuiCol_CheckMark] = ImVec4{accR, accG, accB, 1.0F};
    colors[ImGuiCol_SliderGrab] = ImVec4{0.310F, 0.365F, 0.420F, 1.0F};
    colors[ImGuiCol_SliderGrabActive] = ImVec4{accR, accG, accB, 1.0F};
    colors[ImGuiCol_Separator] = ImVec4{0.145F, 0.155F, 0.170F, 1.0F};
    colors[ImGuiCol_SeparatorHovered] = ImVec4{0.280F, 0.320F, 0.380F, 1.0F};
    colors[ImGuiCol_SeparatorActive] = ImVec4{accR, accG, accB, 1.0F};
    colors[ImGuiCol_Tab] = ImVec4{makeBase(0.075F, 0.12F), makeBase(0.080F, 0.12F), makeBase(0.090F, 0.12F), 1.0F};
    colors[ImGuiCol_TabHovered] = ImVec4{makeBase(0.150F, 0.20F), makeBase(0.165F, 0.20F), makeBase(0.185F, 0.20F), 1.0F};
    colors[ImGuiCol_TabSelected] = ImVec4{makeBase(0.115F, 0.20F), makeBase(0.125F, 0.20F), makeBase(0.140F, 0.20F), 1.0F};
    colors[ImGuiCol_TabDimmed] = ImVec4{makeBase(0.060F, 0.10F), makeBase(0.065F, 0.10F), makeBase(0.072F, 0.10F), 1.0F};
    colors[ImGuiCol_TabDimmedSelected] = ImVec4{makeBase(0.090F, 0.15F), makeBase(0.098F, 0.15F), makeBase(0.110F, 0.15F), 1.0F};
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
