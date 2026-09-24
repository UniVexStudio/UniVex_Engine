// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_color_field_uve.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

#include <imgui.h>

namespace UVE::Editor {

namespace {

constexpr float kTwoPiUVE = 6.28318530718F;
constexpr const char* kPopupIdUVE = "##color-picker";
// Drag payload for a saved colour, so only the shelf's own swatches can be dropped in the bin.
constexpr const char* kSavedColorPayloadUVE = "UVE_SAVED_COLOR";

// One picker can be open at a time - an ImGui popup closes when anything outside it is clicked -
// so one session describes it. It is keyed by the popup's ID, which is unique per field.
struct ColorFieldSessionUVE final {
    ImGuiID popupId = 0;
    bool open = false;
    bool hasAlpha = false;
    bool discDragging = false;
    EditorColorUVE original{};
    EditorColorUVE working{};
    // Kept alongside the RGB value rather than derived from it each frame: a grey has no hue, so
    // deriving it would snap the disc marker to red whenever saturation or value reached zero.
    EditorHsvUVE hsv{};
    std::array<char, 16> hex{};
};

ColorFieldSessionUVE g_colorFieldSessionUVE;

[[nodiscard]] ImVec4 ToImVec4UVE(const EditorColorUVE& color, const bool hasAlpha) {
    return ImVec4{color.r, color.g, color.b, hasAlpha ? color.a : 1.0F};
}

[[nodiscard]] ImU32 ToImU32UVE(const EditorColorUVE& color) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4{color.r, color.g, color.b, 1.0F});
}

// Black or white, whichever reads on top of the colour. A transparent colour is judged as drawn
// over the mid-grey checkerboard behind it.
[[nodiscard]] ImU32 ContrastingTextColorUVE(const EditorColorUVE& color, const bool hasAlpha) {
    const float alpha = hasAlpha ? std::clamp(color.a, 0.0F, 1.0F) : 1.0F;
    const float luminance = (0.2126F * color.r) + (0.7152F * color.g) + (0.0722F * color.b);
    const float shown = (luminance * alpha) + (0.5F * (1.0F - alpha));
    return shown > 0.55F ? IM_COL32(18, 20, 24, 255) : IM_COL32(240, 242, 246, 255);
}

void CopyHexUVE(std::array<char, 16>& buffer, const EditorColorUVE& color, const bool hasAlpha) {
    const std::string hex = FormatColorHexUVE(color, hasAlpha);
    buffer.fill('\0');
    std::memcpy(buffer.data(), hex.data(), std::min(hex.size(), buffer.size() - 1U));
}

// The number of hex digits an author typed, ignoring spaces and a leading '#': an 8-digit entry
// states alpha, anything shorter leaves the current alpha alone.
[[nodiscard]] std::size_t TypedHexDigitsUVE(const std::string_view text) {
    std::size_t digits = 0U;
    for (const char c : text) {
        if (c != ' ' && c != '#') {
            ++digits;
        }
    }
    return digits;
}

// Sets the working colour from RGB, carrying hue and saturation over where RGB has none.
void SetWorkingRgbUVE(ColorFieldSessionUVE& session, EditorColorUVE color) {
    if (!session.hasAlpha) {
        color.a = 1.0F;
    }
    session.working = color;
    session.hsv = RgbToHsvUVE(color, session.hsv);
}

void SetWorkingHsvUVE(ColorFieldSessionUVE& session, const EditorHsvUVE& hsv) {
    session.hsv = hsv;
    session.working = HsvToRgbUVE(hsv, session.working.a);
}

// ---- drawing helpers ------------------------------------------------------------------------

// The hue/saturation disc at full value: hue runs round the rim, saturation from grey at the
// centre to full at the rim. Built from coloured triangles so the colour varies smoothly across
// it rather than in visible bands.
void DrawHueSaturationDiscUVE(ImDrawList& drawList, const ImVec2 center, const float radius) {
    constexpr int kSegments = 96;
    constexpr int kRings = 12;
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    const auto colorAt = [](const float hue, const float saturation) {
        return ToImU32UVE(HsvToRgbUVE(EditorHsvUVE{hue, saturation, 1.0F}, 1.0F));
    };
    const auto pointAt = [center, radius](const float angle, const float fraction) {
        return ImVec2{center.x + (std::cos(angle) * radius * fraction), center.y + (std::sin(angle) * radius * fraction)};
    };
    drawList.PrimReserve(kSegments * kRings * 6, kSegments * kRings * 6);
    for (int ring = 0; ring < kRings; ++ring) {
        const float inner = static_cast<float>(ring) / static_cast<float>(kRings);
        const float outer = static_cast<float>(ring + 1) / static_cast<float>(kRings);
        for (int segment = 0; segment < kSegments; ++segment) {
            const float hue0 = static_cast<float>(segment) / static_cast<float>(kSegments);
            const float hue1 = static_cast<float>(segment + 1) / static_cast<float>(kSegments);
            const float angle0 = hue0 * kTwoPiUVE;
            const float angle1 = hue1 * kTwoPiUVE;
            const ImVec2 a = pointAt(angle0, inner);
            const ImVec2 b = pointAt(angle1, inner);
            const ImVec2 c = pointAt(angle1, outer);
            const ImVec2 d = pointAt(angle0, outer);
            const ImU32 colorA = colorAt(hue0, inner);
            const ImU32 colorB = colorAt(hue1, inner);
            const ImU32 colorC = colorAt(hue1, outer);
            const ImU32 colorD = colorAt(hue0, outer);
            drawList.PrimVtx(a, uv, colorA);
            drawList.PrimVtx(b, uv, colorB);
            drawList.PrimVtx(c, uv, colorC);
            drawList.PrimVtx(a, uv, colorA);
            drawList.PrimVtx(c, uv, colorC);
            drawList.PrimVtx(d, uv, colorD);
        }
    }
    // A soft rim, so the disc's edge is smooth rather than the stepped outline of its triangles.
    drawList.AddCircle(center, radius, ImGui::GetColorU32(ImGuiCol_Border), kSegments, 1.5F);
}

void DrawMarkerUVE(ImDrawList& drawList, const ImVec2 position, const EditorColorUVE& fill) {
    drawList.AddCircleFilled(position, 5.5F, ToImU32UVE(fill), 16);
    drawList.AddCircle(position, 6.0F, IM_COL32(0, 0, 0, 200), 16, 2.5F);
    drawList.AddCircle(position, 5.0F, IM_COL32(255, 255, 255, 255), 16, 1.5F);
}

// A vertical bar whose top is 1 and bottom 0, painted from `top` to `bottom`, with arrows at the
// current value. Returns true while the author drags it; `value` is updated.
[[nodiscard]] bool DrawVerticalBarUVE(const char* id, const ImVec2 size, const ImU32 top, const ImU32 bottom,
                                      float& value, const char* tooltip) {
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max{min.x + size.x, min.y + size.y};
    static_cast<void>(ImGui::InvisibleButton(id, size));
    bool changed = false;
    if (ImGui::IsItemActive()) {
        const float next = std::clamp(1.0F - ((ImGui::GetIO().MousePos.y - min.y) / size.y), 0.0F, 1.0F);
        changed = next != value;
        value = next;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && !ImGui::IsItemActive()) {
        ImGui::SetTooltip("%s", tooltip);
    }
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    drawList.AddRectFilledMultiColor(min, max, top, top, bottom, bottom);
    drawList.AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border));
    const float y = min.y + ((1.0F - std::clamp(value, 0.0F, 1.0F)) * size.y);
    constexpr float kArrow = 5.0F;
    const ImU32 arrow = ImGui::GetColorU32(ImGuiCol_Text);
    drawList.AddTriangleFilled(ImVec2{min.x - 1.0F, y - kArrow}, ImVec2{min.x - 1.0F, y + kArrow},
                               ImVec2{min.x + kArrow, y}, arrow);
    drawList.AddTriangleFilled(ImVec2{max.x + 1.0F, y - kArrow}, ImVec2{max.x + 1.0F, y + kArrow},
                               ImVec2{max.x - kArrow, y}, arrow);
    return changed;
}

// A labelled slider with a thin strip under it showing the colours this channel runs through,
// from `stops[0]` at the left to the last stop at the right.
template <std::size_t N>
[[nodiscard]] bool DrawChannelSliderUVE(const char* label, float& value, const float maximum, const char* format,
                                        const float width, const std::array<ImU32, N>& stops) {
    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    const float labelWidth = ImGui::GetFontSize() * 1.1F;
    ImGui::SameLine(0.0F, 0.0F);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, labelWidth - ImGui::CalcTextSize(label).x));
    ImGui::SetNextItemWidth(width - labelWidth);
    const bool changed =
        ImGui::SliderFloat("##slider", &value, 0.0F, maximum, format, ImGuiSliderFlags_AlwaysClamp);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    const float stripTop = max.y + 1.0F;
    const float stripBottom = max.y + 4.0F;
    const float span = (max.x - min.x) / static_cast<float>(N - 1U);
    for (std::size_t index = 0; index + 1U < N; ++index) {
        const float left = min.x + (span * static_cast<float>(index));
        drawList.AddRectFilledMultiColor(ImVec2{left, stripTop}, ImVec2{left + span, stripBottom}, stops[index],
                                         stops[index + 1U], stops[index + 1U], stops[index]);
    }
    ImGui::Dummy(ImVec2{0.0F, 3.0F});
    ImGui::PopID();
    return changed;
}

// A small bin, drawn rather than taken from a font so it needs no icon glyph.
void DrawBinGlyphUVE(ImDrawList& drawList, const ImVec2 center, const float size, const ImU32 color) {
    const float half = size * 0.5F;
    drawList.AddLine(ImVec2{center.x - half, center.y - half + 2.0F}, ImVec2{center.x + half, center.y - half + 2.0F},
                     color, 1.5F);
    drawList.AddLine(ImVec2{center.x - 2.0F, center.y - half}, ImVec2{center.x + 2.0F, center.y - half}, color, 1.5F);
    drawList.AddRect(ImVec2{center.x - half + 2.0F, center.y - half + 4.0F}, ImVec2{center.x + half - 2.0F, center.y + half},
                     color, 1.5F, 0, 1.5F);
    drawList.AddLine(ImVec2{center.x, center.y - half + 6.0F}, ImVec2{center.x, center.y + half - 2.0F}, color, 1.0F);
}

// ---- the picker -----------------------------------------------------------------------------

struct PickerBodyResultUVE final {
    bool edited = false;
    bool commit = false;
    bool cancel = false;
};

void DrawSavedShelfUVE(ColorFieldSessionUVE& session, ColorPickerPreferencesUVE& preferences, const float width,
                       PickerBodyResultUVE& result) {
    const bool hasAlpha = session.hasAlpha;
    const float height = ImGui::GetFrameHeight() + 4.0F;
    const float buttonWidth = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float shelfWidth = width - ((buttonWidth + spacing) * 2.0F);
    const ImVec2 shelfMin = ImGui::GetCursorScreenPos();
    const ImVec2 shelfMax{shelfMin.x + shelfWidth, shelfMin.y + height};
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    // Filled and outlined, so it reads as a place to drop things even while it is empty.
    drawList.AddRectFilled(shelfMin, shelfMax, ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding);
    drawList.AddRect(shelfMin, shelfMax,
                     ImGui::GetColorU32(ImGui::GetDragDropPayload() != nullptr ? ImGuiCol_DragDropTarget : ImGuiCol_Border),
                     ImGui::GetStyle().FrameRounding);

    // The swatches, laid along the shelf; whatever does not fit is simply not shown.
    const float swatch = height - 6.0F;
    int removeIndex = -1;
    ImGui::SetCursorScreenPos(ImVec2{shelfMin.x + 3.0F, shelfMin.y + 3.0F});
    ImGui::BeginGroup();
    std::size_t shown = 0U;
    for (std::size_t index = 0; index < preferences.saved.size(); ++index) {
        if (((swatch + 3.0F) * static_cast<float>(shown + 1U)) > shelfWidth - 3.0F) {
            break;
        }
        if (shown > 0U) {
            ImGui::SameLine(0.0F, 3.0F);
        }
        ++shown;
        const EditorColorUVE saved = preferences.saved[index];
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::ColorButton("##saved", ToImVec4UVE(saved, hasAlpha),
                               ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop |
                                   (hasAlpha ? ImGuiColorEditFlags_AlphaPreviewHalf : ImGuiColorEditFlags_NoAlpha),
                               ImVec2{swatch, swatch})) {
            SetWorkingRgbUVE(session, saved);
            result.edited = true;
        }
        if (ImGui::BeginDragDropSource()) {
            const int payloadIndex = static_cast<int>(index);
            ImGui::SetDragDropPayload(kSavedColorPayloadUVE, &payloadIndex, sizeof(payloadIndex));
            ImGui::ColorButton("##dragged", ToImVec4UVE(saved, hasAlpha), ImGuiColorEditFlags_NoTooltip,
                               ImVec2{swatch, swatch});
            ImGui::SameLine();
            ImGui::TextUnformatted("Drop on the bin to remove");
            ImGui::EndDragDropSource();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImGui::SetTooltip("%s - click to use, right-click to remove", FormatColorHexUVE(saved, hasAlpha).c_str());
        }
        if (ImGui::BeginPopupContextItem("##saved-menu")) {
            if (ImGui::MenuItem("Remove")) {
                removeIndex = static_cast<int>(index);
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndGroup();
    if (preferences.saved.empty()) {
        const char* hint = "Drop colours here to save them";
        const ImVec2 hintSize = ImGui::CalcTextSize(hint);
        drawList.AddText(ImVec2{shelfMin.x + ((shelfWidth - hintSize.x) * 0.5F), shelfMin.y + ((height - hintSize.y) * 0.5F)},
                         ImGui::GetColorU32(ImGuiCol_TextDisabled), hint);
    }

    // The whole shelf takes a dropped colour - from the swatches below, the recents, or any other
    // colour field in the editor.
    ImGui::SetCursorScreenPos(shelfMin);
    ImGui::Dummy(ImVec2{shelfWidth, height});
    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IMGUI_PAYLOAD_TYPE_COLOR_4F);
        if (payload == nullptr) {
            payload = ImGui::AcceptDragDropPayload(IMGUI_PAYLOAD_TYPE_COLOR_3F);
        }
        if (payload != nullptr) {
            std::array<float, 4> dropped{0.0F, 0.0F, 0.0F, 1.0F};
            std::memcpy(dropped.data(), payload->Data,
                        std::min(static_cast<std::size_t>(payload->DataSize), sizeof(dropped)));
            static_cast<void>(AddSavedColorUVE(preferences.saved,
                                               EditorColorUVE{dropped[0], dropped[1], dropped[2], dropped[3]}));
        }
        ImGui::EndDragDropTarget();
    }

    // Save the current colour, and the bin.
    ImGui::SameLine(0.0F, spacing);
    ImGui::BeginDisabled(preferences.saved.size() >= kMaxSavedColorsUVE);
    if (ImGui::Button("+", ImVec2{buttonWidth, height})) {
        static_cast<void>(AddSavedColorUVE(preferences.saved, session.working));
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(preferences.saved.size() >= kMaxSavedColorsUVE ? "The saved colours are full"
                                                                         : "Save the current colour");
    }
    ImGui::SameLine(0.0F, spacing);
    const ImVec2 binMin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##bin", ImVec2{buttonWidth, height});
    const bool binHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSavedColorPayloadUVE)) {
            removeIndex = *static_cast<const int*>(payload->Data);
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("Drag a saved colour here to remove it");
    }
    DrawBinGlyphUVE(drawList, ImVec2{binMin.x + (buttonWidth * 0.5F), binMin.y + (height * 0.5F)},
                    ImGui::GetFontSize() * 0.8F,
                    ImGui::GetColorU32(binHovered ? ImGuiCol_Text : ImGuiCol_TextDisabled));

    if (removeIndex >= 0 && static_cast<std::size_t>(removeIndex) < preferences.saved.size()) {
        preferences.saved.erase(preferences.saved.begin() + removeIndex);
    }
}

PickerBodyResultUVE DrawPickerBodyUVE(const char* title, ColorFieldSessionUVE& session,
                                      ColorPickerPreferencesUVE& preferences) {
    PickerBodyResultUVE result;
    const bool hasAlpha = session.hasAlpha;
    // A text field was being typed in last frame: Enter and Escape belong to it, not the picker.
    const bool typing = ImGui::GetIO().WantTextInput;
    const float font = ImGui::GetFontSize();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float discSize = font * 12.0F;
    const float barWidth = font * 1.1F;
    const float barGap = font * 0.9F;
    const float sideWidth = font * 6.5F;
    const float width = discSize + ((barGap + barWidth) * 2.0F) + barGap + sideWidth;

    ImGui::TextUnformatted(title);
    ImGui::Spacing();
    DrawSavedShelfUVE(session, preferences, width, result);
    ImGui::Spacing();

    // The disc.
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    const ImVec2 discMin = ImGui::GetCursorScreenPos();
    const float radius = discSize * 0.5F;
    const ImVec2 center{discMin.x + radius, discMin.y + radius};
    ImGui::InvisibleButton("##disc", ImVec2{discSize, discSize});
    if (ImGui::IsItemActivated()) {
        // Only a press on the disc itself starts a drag, not one in the square's corners.
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        session.discDragging = std::hypot(mouse.x - center.x, mouse.y - center.y) <= radius + 2.0F;
    }
    if (ImGui::IsItemActive() && session.discDragging) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const float dx = mouse.x - center.x;
        const float dy = mouse.y - center.y;
        float hue = std::atan2(dy, dx) / kTwoPiUVE;
        if (hue < 0.0F) {
            hue += 1.0F;
        }
        EditorHsvUVE next = session.hsv;
        next.h = hue;
        next.s = std::clamp(std::hypot(dx, dy) / radius, 0.0F, 1.0F);
        SetWorkingHsvUVE(session, next);
        result.edited = true;
    }
    if (ImGui::IsItemDeactivated()) {
        session.discDragging = false;
    }
    DrawHueSaturationDiscUVE(drawList, center, radius);
    const float markerAngle = session.hsv.h * kTwoPiUVE;
    DrawMarkerUVE(drawList,
                  ImVec2{center.x + (std::cos(markerAngle) * session.hsv.s * radius),
                         center.y + (std::sin(markerAngle) * session.hsv.s * radius)},
                  HsvToRgbUVE(EditorHsvUVE{session.hsv.h, session.hsv.s, 1.0F}, 1.0F));

    // Saturation and value bars, each painted with the colours it leads to.
    ImGui::SameLine(0.0F, barGap);
    float saturation = session.hsv.s;
    if (DrawVerticalBarUVE("##saturation", ImVec2{barWidth, discSize},
                           ToImU32UVE(HsvToRgbUVE(EditorHsvUVE{session.hsv.h, 1.0F, session.hsv.v}, 1.0F)),
                           ToImU32UVE(HsvToRgbUVE(EditorHsvUVE{session.hsv.h, 0.0F, session.hsv.v}, 1.0F)),
                           saturation, "Saturation")) {
        SetWorkingHsvUVE(session, EditorHsvUVE{session.hsv.h, saturation, session.hsv.v});
        result.edited = true;
    }
    ImGui::SameLine(0.0F, barGap);
    float value = session.hsv.v;
    if (DrawVerticalBarUVE("##value", ImVec2{barWidth, discSize},
                           ToImU32UVE(HsvToRgbUVE(EditorHsvUVE{session.hsv.h, session.hsv.s, 1.0F}, 1.0F)),
                           IM_COL32(0, 0, 0, 255), value, "Value (brightness)")) {
        SetWorkingHsvUVE(session, EditorHsvUVE{session.hsv.h, session.hsv.s, value});
        result.edited = true;
    }

    // Old above new; the old one is a button that goes back to it. Recents under them.
    ImGui::SameLine(0.0F, barGap);
    ImGui::BeginGroup();
    const ImGuiColorEditFlags swatchFlags =
        ImGuiColorEditFlags_NoTooltip | (hasAlpha ? ImGuiColorEditFlags_AlphaPreviewHalf : ImGuiColorEditFlags_NoAlpha);
    const ImVec2 compareSize{sideWidth, font * 2.2F};
    ImGui::TextDisabled("Old");
    if (ImGui::ColorButton("##original", ToImVec4UVE(session.original, hasAlpha), swatchFlags, compareSize)) {
        SetWorkingRgbUVE(session, session.original);
        result.edited = true;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImGui::SetTooltip("%s - click to go back to it", FormatColorHexUVE(session.original, hasAlpha).c_str());
    }
    static_cast<void>(ImGui::ColorButton("##current", ToImVec4UVE(session.working, hasAlpha), swatchFlags, compareSize));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImGui::SetTooltip("%s - drag it onto the shelf to save it", FormatColorHexUVE(session.working, hasAlpha).c_str());
    }
    ImGui::TextDisabled("New");
    if (!preferences.recents.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Recent");
        const float recent = (sideWidth - (4.0F * 3.0F)) / 5.0F;
        for (std::size_t index = 0; index < preferences.recents.size(); ++index) {
            if (index % 5U != 0U) {
                ImGui::SameLine(0.0F, 3.0F);
            }
            const EditorColorUVE color = preferences.recents[index];
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::ColorButton("##recent", ToImVec4UVE(color, hasAlpha), swatchFlags, ImVec2{recent, recent})) {
                SetWorkingRgbUVE(session, color);
                result.edited = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImGui::SetTooltip("%s", FormatColorHexUVE(color, hasAlpha).c_str());
            }
            ImGui::PopID();
        }
    }
    ImGui::EndGroup();

    // Advanced: every channel as a number, and hex.
    ImGui::Spacing();
    ImGui::SetNextItemOpen(preferences.advancedOpen, ImGuiCond_Always);
    preferences.advancedOpen = ImGui::TreeNodeEx("Advanced", ImGuiTreeNodeFlags_NoTreePushOnOpen);
    if (preferences.advancedOpen) {
        const float column = (width - (spacing * 2.0F)) * 0.5F;
        const EditorColorUVE c = session.working;
        const EditorHsvUVE hsv = session.hsv;
        const auto rgb = [](const float r, const float g, const float b) {
            return ToImU32UVE(EditorColorUVE{r, g, b, 1.0F});
        };

        ImGui::BeginGroup();
        EditorColorUVE edited = c;
        bool rgbChanged = false;
        rgbChanged |= DrawChannelSliderUVE("R", edited.r, 1.0F, "%.3f", column,
                                           std::array<ImU32, 2>{rgb(0.0F, c.g, c.b), rgb(1.0F, c.g, c.b)});
        rgbChanged |= DrawChannelSliderUVE("G", edited.g, 1.0F, "%.3f", column,
                                           std::array<ImU32, 2>{rgb(c.r, 0.0F, c.b), rgb(c.r, 1.0F, c.b)});
        rgbChanged |= DrawChannelSliderUVE("B", edited.b, 1.0F, "%.3f", column,
                                           std::array<ImU32, 2>{rgb(c.r, c.g, 0.0F), rgb(c.r, c.g, 1.0F)});
        if (hasAlpha) {
            rgbChanged |= DrawChannelSliderUVE("A", edited.a, 1.0F, "%.3f", column,
                                               std::array<ImU32, 2>{IM_COL32(128, 128, 128, 255), rgb(c.r, c.g, c.b)});
        }
        ImGui::EndGroup();
        if (rgbChanged) {
            SetWorkingRgbUVE(session, edited);
            result.edited = true;
        }

        ImGui::SameLine(0.0F, spacing * 2.0F);
        ImGui::BeginGroup();
        EditorHsvUVE editedHsv = hsv;
        float degrees = hsv.h * 360.0F;
        bool hsvChanged = false;
        std::array<ImU32, 7> rainbow{};
        for (std::size_t index = 0; index < rainbow.size(); ++index) {
            rainbow[index] = ToImU32UVE(HsvToRgbUVE(EditorHsvUVE{static_cast<float>(index) / 6.0F, 1.0F, 1.0F}, 1.0F));
        }
        if (DrawChannelSliderUVE("H", degrees, 360.0F, "%.1f", column, rainbow)) {
            editedHsv.h = std::clamp(degrees / 360.0F, 0.0F, 1.0F);
            hsvChanged = true;
        }
        hsvChanged |= DrawChannelSliderUVE(
            "S", editedHsv.s, 1.0F, "%.3f", column,
            std::array<ImU32, 2>{ToImU32UVE(HsvToRgbUVE(EditorHsvUVE{hsv.h, 0.0F, hsv.v}, 1.0F)),
                                 ToImU32UVE(HsvToRgbUVE(EditorHsvUVE{hsv.h, 1.0F, hsv.v}, 1.0F))});
        hsvChanged |= DrawChannelSliderUVE(
            "V", editedHsv.v, 1.0F, "%.3f", column,
            std::array<ImU32, 2>{IM_COL32(0, 0, 0, 255),
                                 ToImU32UVE(HsvToRgbUVE(EditorHsvUVE{hsv.h, hsv.s, 1.0F}, 1.0F))});
        if (hsvChanged) {
            SetWorkingHsvUVE(session, editedHsv);
            result.edited = true;
        }

        // Hex, applied when the field is left or Enter is pressed. While it is not being typed in
        // it follows the colour, so dragging the disc keeps it current.
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Hex");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(column - (ImGui::CalcTextSize("Hex").x + spacing));
        ImGui::InputText("##hex", session.hex.data(), session.hex.size(),
                         ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_AutoSelectAll);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            if (const std::optional<EditorColorUVE> parsed = ParseColorHexUVE(session.hex.data())) {
                EditorColorUVE next = *parsed;
                if (hasAlpha && TypedHexDigitsUVE(session.hex.data()) != 8U) {
                    next.a = session.working.a;
                }
                SetWorkingRgbUVE(session, next);
                result.edited = true;
            }
        }
        if (!ImGui::IsItemActive()) {
            CopyHexUVE(session.hex, session.working, hasAlpha);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip(hasAlpha ? "#RGB, #RRGGBB or #RRGGBBAA" : "#RGB or #RRGGBB");
        }
        ImGui::EndGroup();
    }

    // OK and Cancel, bottom right.
    ImGui::Spacing();
    ImGui::Separator();
    const float buttonWidth = font * 5.0F;
    ImGui::SetCursorPosX(ImGui::GetCursorStartPos().x + width - ((buttonWidth * 2.0F) + spacing));
    if (ImGui::Button("OK", ImVec2{buttonWidth, 0.0F})) {
        result.commit = true;
    }
    ImGui::SameLine(0.0F, spacing);
    if (ImGui::Button("Cancel", ImVec2{buttonWidth, 0.0F})) {
        result.cancel = true;
    }

    if (!typing && !ImGui::IsAnyItemActive()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
            result.commit = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            result.cancel = true;
        }
    }
    return result;
}

void RememberRecentUVE(ColorPickerPreferencesUVE& preferences, const ColorFieldSessionUVE& session) {
    if (FormatColorHexUVE(session.working, true) != FormatColorHexUVE(session.original, true)) {
        PushRecentColorUVE(preferences.recents, session.working);
    }
}

} // namespace

ColorFieldEventUVE DrawColorFieldUVE(const char* const id, const char* const title, EditorColorUVE& color,
                                     const bool hasAlpha, ColorPickerPreferencesUVE& preferences) {
    ImGui::PushID(id);
    ColorFieldSessionUVE& session = g_colorFieldSessionUVE;
    const ImGuiID popupId = ImGui::GetID(kPopupIdUVE);
    ColorFieldEventUVE event = ColorFieldEventUVE::None;

    // The picker closed since last frame without going through its own OK/Cancel - a click
    // outside it, most often. ImGui also closes a popup on Escape by itself, so Escape is told
    // apart here: it cancels, every other way out keeps the edit.
    if (session.open && session.popupId == popupId && !ImGui::IsPopupOpen(kPopupIdUVE)) {
        session.open = false;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            color = session.original;
            event = ColorFieldEventUVE::Cancelled;
        } else {
            color = session.working;
            RememberRecentUVE(preferences, session);
            event = ColorFieldEventUVE::Committed;
        }
    }

    const bool sessionOpen = session.open && session.popupId == popupId;
    const EditorColorUVE shown = sessionOpen ? session.working : color;

    // The swatch fills the row; its hex code is written on it in whichever of black or white reads.
    const ImVec2 size{std::max(ImGui::CalcItemWidth(), ImGui::GetFrameHeight()), ImGui::GetFrameHeight()};
    const ImGuiColorEditFlags swatchFlags =
        ImGuiColorEditFlags_NoTooltip | (hasAlpha ? ImGuiColorEditFlags_AlphaPreviewHalf : ImGuiColorEditFlags_NoAlpha);
    const bool clicked = ImGui::ColorButton("##swatch", ToImVec4UVE(shown, hasAlpha), swatchFlags, size);
    const std::string hex = FormatColorHexUVE(shown, hasAlpha);
    {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const ImVec2 textSize = ImGui::CalcTextSize(hex.c_str());
        if (textSize.x + 8.0F <= max.x - min.x) {
            ImGui::GetWindowDrawList()->AddText(
                ImVec2{min.x + (((max.x - min.x) - textSize.x) * 0.5F), min.y + (((max.y - min.y) - textSize.y) * 0.5F)},
                ContrastingTextColorUVE(shown, hasAlpha), hex.c_str());
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && !sessionOpen &&
        !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImGui::SetTooltip("%s - click to edit", hex.c_str());
    }
    // A colour dragged here from another swatch is a finished edit of its own.
    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IMGUI_PAYLOAD_TYPE_COLOR_4F);
        if (payload == nullptr) {
            payload = ImGui::AcceptDragDropPayload(IMGUI_PAYLOAD_TYPE_COLOR_3F);
        }
        if (payload != nullptr && event == ColorFieldEventUVE::None && !sessionOpen) {
            std::array<float, 4> dropped{0.0F, 0.0F, 0.0F, 1.0F};
            std::memcpy(dropped.data(), payload->Data,
                        std::min(static_cast<std::size_t>(payload->DataSize), sizeof(dropped)));
            color = EditorColorUVE{dropped[0], dropped[1], dropped[2], hasAlpha ? dropped[3] : 1.0F};
            PushRecentColorUVE(preferences.recents, color);
            event = ColorFieldEventUVE::Committed;
        }
        ImGui::EndDragDropTarget();
    }

    if (clicked && !sessionOpen) {
        session = ColorFieldSessionUVE{};
        session.popupId = popupId;
        session.open = true;
        session.hasAlpha = hasAlpha;
        session.original = color;
        SetWorkingRgbUVE(session, color);
        CopyHexUVE(session.hex, session.working, hasAlpha);
        ImGui::OpenPopup(kPopupIdUVE);
    }

    if (ImGui::BeginPopup(kPopupIdUVE)) {
        if (session.open && session.popupId == popupId) {
            const PickerBodyResultUVE body = DrawPickerBodyUVE(title, session, preferences);
            if (body.cancel) {
                session.open = false;
                color = session.original;
                event = ColorFieldEventUVE::Cancelled;
                ImGui::CloseCurrentPopup();
            } else if (body.commit) {
                session.open = false;
                color = session.working;
                RememberRecentUVE(preferences, session);
                event = ColorFieldEventUVE::Committed;
                ImGui::CloseCurrentPopup();
            } else if (body.edited && event == ColorFieldEventUVE::None) {
                color = session.working;
                event = ColorFieldEventUVE::Edited;
            }
        } else {
            // A picker left open by a session that no longer belongs to this field.
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::PopID();
    return event;
}

} // namespace UVE::Editor
