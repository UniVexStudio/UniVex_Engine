// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "name_component.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace UVE::Scene {

[[nodiscard]] bool IsEntityNameStringValid(const std::string_view name) noexcept {
    return name.size() <= kMaximumEntityNameBytes &&
           name.find('\0') == std::string_view::npos;
}

[[nodiscard]] bool IsNameComponentValid(const NameComponent& component) noexcept {
    return IsEntityNameStringValid(component.name);
}

[[nodiscard]] bool HasNonEmptyEntityName(const NameComponent* const component) noexcept {
    return component != nullptr && !component->name.empty() &&
           IsEntityNameStringValid(component->name);
}

[[nodiscard]] std::string SanitizeEntityNameString(const std::string_view rawName,
                                                      const std::string_view fallbackName) {
    std::string cleaned;
    cleaned.reserve(std::min(rawName.size(), kMaximumEntityNameBytes));
    for (const char ch : rawName) {
        if (ch == '\0') {
            continue;
        }
        cleaned.push_back(ch);
        if (cleaned.size() >= kMaximumEntityNameBytes) {
            break;
        }
    }
    if (!cleaned.empty() || fallbackName.empty()) {
        return cleaned;
    }
    return SanitizeEntityNameString(fallbackName, {});
}

[[nodiscard]] NameComponent SanitizeNameComponent(const NameComponent& component,
                                                        const std::string_view fallbackName) {
    return NameComponent{
        .name = SanitizeEntityNameString(component.name, fallbackName),
    };
}

[[nodiscard]] std::string_view ResolveEntityDisplayNameOrFallback(
    const NameComponent* const component, const std::string_view fallbackName) noexcept {
    if (HasNonEmptyEntityName(component)) {
        return component->name;
    }
    return fallbackName;
}

} // namespace UVE::Scene
