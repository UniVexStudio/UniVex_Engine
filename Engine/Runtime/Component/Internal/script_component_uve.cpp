// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/script_component_uve.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace UVE::Scene {

[[nodiscard]] bool IsScriptAssetPathValidUVE(const std::string_view path) noexcept {
    if (path.empty() || path.size() > kMaximumScriptAssetPathBytesUVE ||
        path.find('\0') != std::string_view::npos || path.find('\\') != std::string_view::npos ||
        path.find(':') != std::string_view::npos || path.front() == '/') {
        return path.empty();
    }

    std::size_t segmentStart = 0U;
    for (std::size_t index = 0U; index <= path.size(); ++index) {
        if (index != path.size() && path[index] != '/') {
            continue;
        }
        const std::string_view segment = path.substr(segmentStart, index - segmentStart);
        if (segment.empty() || segment == "." || segment == "..") {
            return false;
        }
        segmentStart = index + 1U;
    }
    return true;
}

[[nodiscard]] bool IsScriptComponentValidUVE(const ScriptComponentUVE& component) noexcept {
    return IsScriptAssetPathValidUVE(component.scriptAssetPath);
}

} // namespace UVE::Scene
