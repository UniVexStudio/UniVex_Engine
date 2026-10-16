// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/name_component_uve.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace UVE::Scene {

[[nodiscard]] bool IsEntityNameStringValidUVE(const std::string_view name) noexcept {
    return name.size() <= kMaximumEntityNameBytesUVE &&
           name.find('\0') == std::string_view::npos;
}

[[nodiscard]] bool IsNameComponentValidUVE(const NameComponentUVE& component) noexcept {
    return IsEntityNameStringValidUVE(component.name);
}

[[nodiscard]] bool HasNonEmptyEntityNameUVE(const NameComponentUVE* const component) noexcept {
    return component != nullptr && !component->name.empty() &&
           IsEntityNameStringValidUVE(component->name);
}

[[nodiscard]] std::string SanitizeEntityNameStringUVE(const std::string_view rawName,
                                                      const std::string_view fallbackName) {
    std::string cleaned;
    cleaned.reserve(std::min(rawName.size(), kMaximumEntityNameBytesUVE));
    for (const char ch : rawName) {
        if (ch == '\0') {
            continue;
        }
        cleaned.push_back(ch);
        if (cleaned.size() >= kMaximumEntityNameBytesUVE) {
            break;
        }
    }
    if (!cleaned.empty() || fallbackName.empty()) {
        return cleaned;
    }
    return SanitizeEntityNameStringUVE(fallbackName, {});
}

[[nodiscard]] NameComponentUVE SanitizeNameComponentUVE(const NameComponentUVE& component,
                                                        const std::string_view fallbackName) {
    return NameComponentUVE{
        .name = SanitizeEntityNameStringUVE(component.name, fallbackName),
    };
}

[[nodiscard]] std::string_view ResolveEntityDisplayNameOrFallbackUVE(
    const NameComponentUVE* const component, const std::string_view fallbackName) noexcept {
    if (HasNonEmptyEntityNameUVE(component)) {
        return component->name;
    }
    return fallbackName;
}

} // namespace UVE::Scene
