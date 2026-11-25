// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_platform.h"

#include <algorithm>
#include <vector>

namespace UVE::Blueprint::Platform {

BlueprintPlatformKind GetCurrentBlueprintPlatformKind() noexcept {
#if defined(__ANDROID__)
    return BlueprintPlatformKind::Android;
#elif defined(_WIN32)
    return BlueprintPlatformKind::Windows;
#elif defined(__linux__)
    return BlueprintPlatformKind::Linux;
#else
    return BlueprintPlatformKind::Unknown;
#endif
}

std::string_view BlueprintPlatformName(const BlueprintPlatformKind kind) noexcept {
    switch (kind) {
        case BlueprintPlatformKind::Linux: return "Linux";
        case BlueprintPlatformKind::Windows: return "Windows";
        case BlueprintPlatformKind::Android: return "Android";
        case BlueprintPlatformKind::Unknown: return "Unknown";
    }
    return "Unknown";
}

BlueprintPlatformCapabilities GetBlueprintPlatformCapabilitiesFor(const BlueprintPlatformKind kind) noexcept {
    switch (kind) {
        case BlueprintPlatformKind::Linux:
            return {kind, "Linux", ".so", '/', true, true, true, true, 512U, 2'000'000U};
        case BlueprintPlatformKind::Windows:
            return {kind, "Windows", ".dll", '\\', true, true, true, true, 512U, 2'000'000U};
        case BlueprintPlatformKind::Android:
            // Android runtime executes cooked Blueprint scripts without desktop editor hot-reload or dynamic plugins.
            return {kind, "Android", ".so", '/', true, false, false, false, 256U, 500'000U};
        case BlueprintPlatformKind::Unknown:
        default:
            return {BlueprintPlatformKind::Unknown, "Unknown", "", '/', false, false, true, false, 256U, 1'000'000U};
    }
}

BlueprintPlatformCapabilities GetCurrentBlueprintPlatformCapabilities() noexcept {
    return GetBlueprintPlatformCapabilitiesFor(GetCurrentBlueprintPlatformKind());
}

std::optional<std::string> NormalizeBlueprintAssetPath(const std::string_view rawPath) {
    if (rawPath.empty()) {
        return std::nullopt;
    }
    std::string normalized(rawPath);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');

    // Reject Windows drive roots ("C:/...") and POSIX absolute roots ("/...").
    if (normalized.front() == '/' || (normalized.size() >= 2U && normalized[1] == ':')) {
        return std::nullopt;
    }

    std::vector<std::string> segments;
    std::size_t start = 0U;
    while (start <= normalized.size()) {
        const std::size_t slash = normalized.find('/', start);
        const std::string_view seg =
            slash == std::string_view::npos ? std::string_view(normalized).substr(start)
                                            : std::string_view(normalized).substr(start, slash - start);
        if (!seg.empty() && seg != ".") {
            if (seg == "..") {
                return std::nullopt;
            }
            segments.emplace_back(seg);
        }
        if (slash == std::string_view::npos) {
            break;
        }
        start = slash + 1U;
    }

    if (segments.empty()) {
        return std::nullopt;
    }

    std::string out;
    for (std::size_t i = 0U; i < segments.size(); ++i) {
        if (i > 0U) {
            out.push_back('/');
        }
        out += segments[i];
    }
    return out;
}

std::filesystem::path GetBlueprintCacheDirectory(const std::filesystem::path& projectRoot,
                                                    const BlueprintPlatformKind platform) {
    std::string platDir = "common";
    switch (platform) {
        case BlueprintPlatformKind::Linux: platDir = "linux"; break;
        case BlueprintPlatformKind::Windows: platDir = "windows"; break;
        case BlueprintPlatformKind::Android: platDir = "android"; break;
        case BlueprintPlatformKind::Unknown: platDir = "common"; break;
    }
    return projectRoot / ".uvcache" / "blueprint" / platDir;
}

} // namespace UVE::Blueprint::Platform
