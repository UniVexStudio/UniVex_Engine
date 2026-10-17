// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/editor_description_component_uve.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace UVE::Scene {

bool IsEditorDescriptionTextValidUVE(const std::string_view text) noexcept {
    // Embedded nulls are rejected for the same reason NameComponentUVE rejects them: the string
    // crosses into JSON and into C-string UI calls, both of which would silently truncate at the
    // null and leave the author's text quietly cut in half.
    return text.size() <= kMaximumEditorDescriptionBytesUVE &&
           text.find('\0') == std::string_view::npos;
}

bool IsEditorDescriptionComponentValidUVE(
    const EditorDescriptionComponentUVE& component) noexcept {
    return IsEditorDescriptionTextValidUVE(component.description);
}

bool HasNonEmptyEditorDescriptionUVE(
    const EditorDescriptionComponentUVE& component) noexcept {
    for (const char ch : component.description) {
        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r' && ch != '\0') {
            return true;
        }
    }
    return false;
}

std::size_t CountEditorDescriptionLinesUVE(
    const EditorDescriptionComponentUVE& component) noexcept {
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

std::string SanitizeEditorDescriptionTextUVE(const std::string_view text) {
    std::string cleaned;
    cleaned.reserve(std::min(text.size(), kMaximumEditorDescriptionBytesUVE));
    for (const char ch : text) {
        if (ch == '\0') {
            continue;
        }
        cleaned.push_back(ch);
        if (cleaned.size() >= kMaximumEditorDescriptionBytesUVE) {
            break;
        }
    }
    return cleaned;
}

EditorDescriptionComponentUVE SanitizeEditorDescriptionComponentUVE(
    const EditorDescriptionComponentUVE& component) {
    return EditorDescriptionComponentUVE{
        .description = SanitizeEditorDescriptionTextUVE(component.description),
    };
}

std::string BuildEditorDescriptionPreviewUVE(const EditorDescriptionComponentUVE& component,
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
