// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "project_compatibility.h"

#include <array>
#include <cstddef>
#include <optional>

namespace UVE::Pack {
namespace {

inline constexpr std::size_t kVersionComponentCountUVE = 4U;

using VersionComponentsUVE = std::array<std::uint32_t, kVersionComponentCountUVE>;

[[nodiscard]] Core::VersionUVE ToEngineVersionUVE(const Platform::EditorProjectVersionUVE& version) noexcept {
    return {version.major, version.minor, version.patch, version.build};
}

[[nodiscard]] VersionComponentsUVE ComponentsUVE(const Core::VersionUVE& version) noexcept {
    return {version.major, version.minor, version.patch, version.build};
}

/// The most significant component at which the two versions differ, or none when they are equal.
[[nodiscard]] std::optional<std::size_t> FirstDifferenceUVE(const VersionComponentsUVE& lhs,
                                                            const VersionComponentsUVE& rhs) noexcept {
    for (std::size_t index = 0U; index < kVersionComponentCountUVE; ++index) {
        if (lhs[index] != rhs[index]) {
            return index;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool IsAtOrAboveUVE(const std::size_t component, const VersionComponentUVE threshold) noexcept {
    return component <= static_cast<std::size_t>(threshold);
}

} // namespace

ProjectCompatibilityUVE EvaluateProjectCompatibilityUVE(const Platform::EditorProjectVersionUVE& projectVersion,
                                                        const Core::VersionUVE& engineVersion,
                                                        const ProjectCompatibilityPolicyUVE& policy) {
    const Core::VersionUVE project = ToEngineVersionUVE(projectVersion);
    const VersionComponentsUVE projectComponents = ComponentsUVE(project);
    const VersionComponentsUVE engineComponents = ComponentsUVE(engineVersion);
    const std::string projectText = project.ToStringUVE();
    const std::string engineText = engineVersion.ToStringUVE();

    const std::optional<std::size_t> difference = FirstDifferenceUVE(projectComponents, engineComponents);
    if (!difference.has_value()) {
        return {ProjectCompatibilityVerdictUVE::Same, "The project was last saved with engine " + engineText + "."};
    }

    if (projectComponents[*difference] > engineComponents[*difference]) {
        if (IsAtOrAboveUVE(*difference, policy.newerRejectedFrom)) {
            return {ProjectCompatibilityVerdictUVE::NewerIncompatible,
                    "The project was saved with engine " + projectText + ", which engine " + engineText +
                        " cannot open. Use engine " + projectText + " or newer."};
        }
        return {ProjectCompatibilityVerdictUVE::NewerCompatible,
                "The project was saved with the newer engine " + projectText + "; engine " + engineText +
                    " opens it but may not support everything it uses."};
    }

    if (IsAtOrAboveUVE(*difference, policy.olderUpgradePromptFrom)) {
        return {ProjectCompatibilityVerdictUVE::OlderNeedsUpgrade,
                "The project was saved with engine " + projectText + ". Upgrading it to engine " + engineText +
                    " means older engines may no longer open it."};
    }
    return {ProjectCompatibilityVerdictUVE::OlderUpgradesSilently,
            "The project was saved with engine " + projectText + " and is upgraded to engine " + engineText + "."};
}

bool ShouldRecordEngineVersionUVE(const ProjectCompatibilityVerdictUVE verdict, const bool upgradeAccepted) noexcept {
    switch (verdict) {
    case ProjectCompatibilityVerdictUVE::Same:
    case ProjectCompatibilityVerdictUVE::OlderUpgradesSilently:
        return true;
    case ProjectCompatibilityVerdictUVE::OlderNeedsUpgrade:
        return upgradeAccepted;
    case ProjectCompatibilityVerdictUVE::NewerCompatible:
    case ProjectCompatibilityVerdictUVE::NewerIncompatible:
        return false;
    }
    return false;
}

std::string FormatProjectVersionUVE(const Platform::EditorProjectVersionUVE& version) {
    return ToEngineVersionUVE(version).ToStringUVE();
}

} // namespace UVE::Pack
