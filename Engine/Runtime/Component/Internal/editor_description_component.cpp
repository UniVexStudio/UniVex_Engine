// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_description_component.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace UVE::Scene {

bool IsEditorDescriptionTextValid(const std::string_view text) noexcept {
    // Embedded nulls are rejected for the same reason NameComponent rejects them: the string
    // crosses into JSON and into C-string UI calls, both of which would silently truncate at the
    // null and leave the author's text quietly cut in half.
    return text.size() <= kMaximumEditorDescriptionBytes &&
           text.find('\0') == std::string_view::npos;
}

bool IsEditorDescriptionComponentValid(
    const EditorDescriptionComponent& component) noexcept {
    return IsEditorDescriptionTextValid(component.description);
}

bool HasNonEmptyEditorDescription(
    const EditorDescriptionComponent& component) noexcept {
    for (const char ch : component.description) {
        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r' && ch != '\0') {
            return true;
        }
    }
    return false;
}

std::size_t CountEditorDescriptionLines(
    const EditorDescriptionComponent& component) noexcept {
    if (component.description.empty()) {
        return 0U;
    }
    std::size_t lines = 1U;
    for (const char ch : component.description) {
        if (ch == '\n') {
            ++lines;
        }
    }
    return lines;
}

std::string SanitizeEditorDescriptionText(const std::string_view text) {
    std::string cleaned;
    cleaned.reserve(std::min(text.size(), kMaximumEditorDescriptionBytes));
    for (const char ch : text) {
        if (ch == '\0') {
            continue;
        }
        cleaned.push_back(ch);
        if (cleaned.size() >= kMaximumEditorDescriptionBytes) {
            break;
        }
    }
    return cleaned;
}

EditorDescriptionComponent SanitizeEditorDescriptionComponent(
    const EditorDescriptionComponent& component) {
    return EditorDescriptionComponent{
        .description = SanitizeEditorDescriptionText(component.description),
    };
}

std::string BuildEditorDescriptionPreview(const EditorDescriptionComponent& component,
                                             const std::size_t maxPreviewBytes) {
    if (maxPreviewBytes == 0U || component.description.empty()) {
        return {};
    }
    std::string preview;
    preview.reserve(std::min(component.description.size(), maxPreviewBytes));
    bool started = false;
    for (const char ch : component.description) {
        if (ch == '\0' || ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            if (started) {
                break;
            }
            continue;
        }
        if (!started && (ch == ' ' || ch == '\t')) {
            continue;
        }
        started = true;
        preview.push_back(ch);
        if (preview.size() >= maxPreviewBytes) {
            break;
        }
    }
    return preview;
}

} // namespace UVE::Scene
