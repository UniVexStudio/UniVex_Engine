// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/window/window_title_format_uve.h"

#include <cctype>

namespace UVE::Window {
namespace {

void ReplaceAllUVE(std::string& value, const std::string_view token, const std::string_view replacement) {
    std::size_t position = 0U;
    while ((position = value.find(token, position)) != std::string::npos) {
        value.replace(position, token.size(), replacement);
        position += replacement.size();
    }
}

[[nodiscard]] bool IsWhitespaceUVE(const char value) noexcept {
    return std::isspace(static_cast<unsigned char>(value)) != 0;
}

[[nodiscard]] bool IsSceneSeparatorUVE(const char value) noexcept {
    return value == '-' || value == '|' || value == '/' || value == ':' || value == '(' || value == ')' ||
           value == '[' || value == ']';
}

void TrimWindowTitleUVE(std::string& title) {
    std::size_t first = 0U;
    while (first < title.size() && IsWhitespaceUVE(title[first])) {
        ++first;
    }
    std::size_t last = title.size();
    while (last > first && IsWhitespaceUVE(title[last - 1U])) {
        --last;
    }
    title = title.substr(first, last - first);
}

void RemoveSceneNameTokensUVE(std::string& title) {
    constexpr std::string_view kSceneToken = "{sceneName}";
    std::size_t position = 0U;
    while ((position = title.find(kSceneToken, position)) != std::string::npos) {
        const std::size_t tokenEnd = position + kSceneToken.size();
        std::size_t left = position;
        while (left > 0U && IsWhitespaceUVE(title[left - 1U])) {
            --left;
        }
        std::size_t right = tokenEnd;
        while (right < title.size() && IsWhitespaceUVE(title[right])) {
            ++right;
        }

        std::size_t eraseStart = position;
        std::size_t eraseEnd = tokenEnd;
        const bool hasLeftSeparator = left > 0U && IsSceneSeparatorUVE(title[left - 1U]);
        const bool hasRightSeparator = right < title.size() && IsSceneSeparatorUVE(title[right]);
        if (hasLeftSeparator && hasRightSeparator &&
            ((title[left - 1U] == '(' && title[right] == ')') ||
             (title[left - 1U] == '[' && title[right] == ']'))) {
            eraseStart = left - 1U;
            eraseEnd = right + 1U;
        } else if (hasLeftSeparator) {
            eraseStart = left - 1U;
        } else if (hasRightSeparator) {
            eraseEnd = right + 1U;
            while (eraseEnd < title.size() && IsWhitespaceUVE(title[eraseEnd])) {
                ++eraseEnd;
            }
        }

        title.erase(eraseStart, eraseEnd - eraseStart);
        TrimWindowTitleUVE(title);
        position = eraseStart;
    }
}

void TruncateUtf8UVE(std::string& value, const std::size_t maximumBytes) {
    if (value.size() <= maximumBytes) {
        return;
    }
    std::size_t length = maximumBytes;
    while (length > 0U && (static_cast<unsigned char>(value[length]) & 0xC0U) == 0x80U) {
        --length;
    }
    value.resize(length);
}

} // namespace

std::string FormatWindowTitleUVE(
    const std::string_view format, const std::string_view productName, const std::string_view projectName,
    const std::string_view sceneName, const bool editorPlayMode, const bool appendSceneNameInEditorPlayMode) {
    const std::string_view safeProductName = productName.empty() ? std::string_view{"UniVex Engine"} : productName;
    const std::string_view safeProjectName = projectName.empty() ? safeProductName : projectName;
    std::string title{format.empty() ? std::string_view{"{productName}"} : format};
    const bool appendScene = editorPlayMode && appendSceneNameInEditorPlayMode && !sceneName.empty();
    const bool containsSceneToken = title.find("{sceneName}") != std::string::npos;

    ReplaceAllUVE(title, "{productName}", safeProductName);
    ReplaceAllUVE(title, "{projectName}", safeProjectName);
    if (appendScene) {
        ReplaceAllUVE(title, "{sceneName}", sceneName);
    } else {
        RemoveSceneNameTokensUVE(title);
    }

    if (appendScene && !containsSceneToken) {
        title += " - ";
        title += sceneName;
    }
    if (title.empty()) {
        title.assign(safeProductName);
    }
    TruncateUtf8UVE(title, kMaximumWindowTitleBytesUVE);
    return title;
}

} // namespace UVE::Window
