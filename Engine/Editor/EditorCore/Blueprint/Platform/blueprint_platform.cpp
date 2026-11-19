// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_platform.h"

#include <algorithm>
#include <vector>

namespace UVE::Blueprint::Platform {

BlueprintPlatformKindUVE GetCurrentBlueprintPlatformKindUVE() noexcept {
#if defined(__ANDROID__)
    return BlueprintPlatformKindUVE::Android;
#elif defined(_WIN32)
    return BlueprintPlatformKindUVE::Windows;
#elif defined(__linux__)
    return BlueprintPlatformKindUVE::Linux;
#else
    return BlueprintPlatformKindUVE::Unknown;
#endif
}

std::string_view BlueprintPlatformNameUVE(const BlueprintPlatformKindUVE kind) noexcept {
    switch (kind) {
        case BlueprintPlatformKindUVE::Linux: return "Linux";
        case BlueprintPlatformKindUVE::Windows: return "Windows";
        case BlueprintPlatformKindUVE::Android: return "Android";
        case BlueprintPlatformKindUVE::Unknown: return "Unknown";
    }
    return "Unknown";
}

BlueprintPlatformCapabilitiesUVE GetBlueprintPlatformCapabilitiesForUVE(const BlueprintPlatformKindUVE kind) noexcept {
    switch (kind) {
        case BlueprintPlatformKindUVE::Linux:
            return {kind, "Linux", ".so", '/', true, true, true, true, 512U, 2'000'000U};
        case BlueprintPlatformKindUVE::Windows:
            return {kind, "Windows", ".dll", '\\', true, true, true, true, 512U, 2'000'000U};
        case BlueprintPlatformKindUVE::Android:
            // Android runtime executes cooked Blueprint scripts without desktop editor hot-reload or dynamic plugins.
            return {kind, "Android", ".so", '/', true, false, false, false, 256U, 500'000U};
        case BlueprintPlatformKindUVE::Unknown:
        default:
            return {BlueprintPlatformKindUVE::Unknown, "Unknown", "", '/', false, false, true, false, 256U, 1'000'000U};
    }
}

BlueprintPlatformCapabilitiesUVE GetCurrentBlueprintPlatformCapabilitiesUVE() noexcept {
    return GetBlueprintPlatformCapabilitiesForUVE(GetCurrentBlueprintPlatformKindUVE());
}

std::optional<std::string> NormalizeBlueprintAssetPathUVE(const std::string_view rawPath) {
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

std::filesystem::path GetBlueprintCacheDirectoryUVE(const std::filesystem::path& projectRoot,
                                                    const BlueprintPlatformKindUVE platform) {
    std::string platDir = "common";
    switch (platform) {
        case BlueprintPlatformKindUVE::Linux: platDir = "linux"; break;
        case BlueprintPlatformKindUVE::Windows: platDir = "windows"; break;
        case BlueprintPlatformKindUVE::Android: platDir = "android"; break;
        case BlueprintPlatformKindUVE::Unknown: platDir = "common"; break;
    }
    return projectRoot / ".uvcache" / "blueprint" / platDir;
}

} // namespace UVE::Blueprint::Platform
