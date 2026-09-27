// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/script_component_uve.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace UVE::Scene {

[[nodiscard]] bool IsScriptAssetPathValidUVE(const std::string_view path) noexcept {
    if (path.empty() || path.size() > kMaximumScriptAssetPathBytesUVE ||
        path.contains('\0') || path.contains('\\') ||
        path.contains(':') || path.front() == '/') {
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
    if (!IsScriptAssetPathValidUVE(component.scriptAssetPath) ||
        component.exportValues.size() > kMaximumScriptExportValuesUVE) {
        return false;
    }
    for (const auto& [name, value] : component.exportValues) {
        if (name.empty() || name.size() > kMaximumScriptExportNameBytesUVE || name.contains('\0') ||
            value.size() > kMaximumScriptExportValueBytesUVE) {
            return false;
        }
    }
    return true;
}

} // namespace UVE::Scene
