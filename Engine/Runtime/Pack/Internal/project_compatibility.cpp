// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "project_compatibility.h"

#include <array>
#include <cstddef>
#include <optional>

namespace UVE::Pack {
namespace {

inline constexpr std::size_t kVersionComponentCount = 4U;

using VersionComponents = std::array<std::uint32_t, kVersionComponentCount>;

[[nodiscard]] Core::Version ToEngineVersion(const Platform::EditorProjectVersion& version) noexcept {
    return {version.major, version.minor, version.patch, version.build};
}

[[nodiscard]] VersionComponents Components(const Core::Version& version) noexcept {
    return {version.major, version.minor, version.patch, version.build};
}

/// The most significant component at which the two versions differ, or none when they are equal.
[[nodiscard]] std::optional<std::size_t> FirstDifference(const VersionComponents& lhs,
                                                            const VersionComponents& rhs) noexcept {
    for (std::size_t index = 0U; index < kVersionComponentCount; ++index) {
        if (lhs[index] != rhs[index]) {
            return index;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool IsAtOrAbove(const std::size_t component, const VersionComponent threshold) noexcept {
    return component <= static_cast<std::size_t>(threshold);
}

} // namespace

ProjectCompatibility EvaluateProjectCompatibility(const Platform::EditorProjectVersion& projectVersion,
                                                        const Core::Version& engineVersion,
                                                        const ProjectCompatibilityPolicy& policy) {
    const Core::Version project = ToEngineVersion(projectVersion);
    const VersionComponents projectComponents = Components(project);
    const VersionComponents engineComponents = Components(engineVersion);
    const std::string projectText = project.ToString();
    const std::string engineText = engineVersion.ToString();

    const std::optional<std::size_t> difference = FirstDifference(projectComponents, engineComponents);
    if (!difference.has_value()) {
        return {ProjectCompatibilityVerdict::Same, "The project was last saved with engine " + engineText + "."};
    }

    if (projectComponents[*difference] > engineComponents[*difference]) {
        if (IsAtOrAbove(*difference, policy.newerRejectedFrom)) {
            return {ProjectCompatibilityVerdict::NewerIncompatible,
                    "The project was saved with engine " + projectText + ", which engine " + engineText +
                        " cannot open. Use engine " + projectText + " or newer."};
        }
        return {ProjectCompatibilityVerdict::NewerCompatible,
                "The project was saved with the newer engine " + projectText + "; engine " + engineText +
                    " opens it but may not support everything it uses."};
    }

    if (IsAtOrAbove(*difference, policy.olderUpgradePromptFrom)) {
        return {ProjectCompatibilityVerdict::OlderNeedsUpgrade,
                "The project was saved with engine " + projectText + ". Upgrading it to engine " + engineText +
                    " means older engines may no longer open it."};
    }
    return {ProjectCompatibilityVerdict::OlderUpgradesSilently,
            "The project was saved with engine " + projectText + " and is upgraded to engine " + engineText + "."};
}

bool ShouldRecordEngineVersion(const ProjectCompatibilityVerdict verdict, const bool upgradeAccepted) noexcept {
    switch (verdict) {
    case ProjectCompatibilityVerdict::Same:
    case ProjectCompatibilityVerdict::OlderUpgradesSilently:
        return true;
    case ProjectCompatibilityVerdict::OlderNeedsUpgrade:
        return upgradeAccepted;
    case ProjectCompatibilityVerdict::NewerCompatible:
    case ProjectCompatibilityVerdict::NewerIncompatible:
        return false;
    }
    return false;
}

std::string FormatProjectVersion(const Platform::EditorProjectVersion& version) {
    return ToEngineVersion(version).ToString();
}

} // namespace UVE::Pack
