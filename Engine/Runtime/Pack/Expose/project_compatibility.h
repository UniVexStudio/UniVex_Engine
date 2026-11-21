// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>

#include "editor_project_package.h"
#include "engine_version.h"

namespace UVE::Pack {

/// The parts of a version, most significant first.
enum class VersionComponentUVE : std::uint8_t {
    Major = 0,
    Minor,
    Patch,
    Build,
};

/// How a project's recorded engine version relates to the running engine.
enum class ProjectCompatibilityVerdictUVE : std::uint8_t {
    /// Authored with this exact engine version.
    Same = 0,
    /// Authored with an older engine, differing only below the upgrade-prompt threshold; the
    /// project is upgraded without asking.
    OlderUpgradesSilently,
    /// Authored with an older engine, differing at or above the upgrade-prompt threshold; the user
    /// confirms the upgrade before the project records the new version.
    OlderNeedsUpgrade,
    /// Authored with a newer engine that this engine can still open; the user is warned.
    NewerCompatible,
    /// Authored with a newer engine that this engine must not open.
    NewerIncompatible,
};

/// Where the compatibility boundaries sit. A difference at `newerRejectedFrom` or any more
/// significant component makes a newer project incompatible; a difference at
/// `olderUpgradePromptFrom` or any more significant component makes an older project ask before
/// it is upgraded.
struct ProjectCompatibilityPolicyUVE final {
    VersionComponentUVE newerRejectedFrom = VersionComponentUVE::Major;
    VersionComponentUVE olderUpgradePromptFrom = VersionComponentUVE::Minor;
};

inline constexpr ProjectCompatibilityPolicyUVE kDefaultProjectCompatibilityPolicyUVE{};

struct ProjectCompatibilityUVE final {
    ProjectCompatibilityVerdictUVE verdict = ProjectCompatibilityVerdictUVE::Same;
    /// A sentence naming both versions, suitable for a log line or a dialog.
    std::string message;

    /// False only for a project this engine must refuse to open.
    [[nodiscard]] bool CanOpenUVE() const noexcept {
        return verdict != ProjectCompatibilityVerdictUVE::NewerIncompatible;
    }
    /// Whether the user must confirm before the project is upgraded.
    [[nodiscard]] bool NeedsUpgradeConfirmationUVE() const noexcept {
        return verdict == ProjectCompatibilityVerdictUVE::OlderNeedsUpgrade;
    }
};

/// Compares the engine version a project last recorded with the running engine's. Pure and
/// thread-safe; shared by the editor's open path and any tool that lists projects.
[[nodiscard]] ProjectCompatibilityUVE EvaluateProjectCompatibilityUVE(
    const Platform::EditorProjectVersionUVE& projectVersion, const Core::VersionUVE& engineVersion,
    const ProjectCompatibilityPolicyUVE& policy = kDefaultProjectCompatibilityPolicyUVE);

/// Whether closing an editor session may record the running engine's version in the project.
/// A newer project never records an older engine's version, and an upgrade that needed
/// confirmation records it only once the user accepted.
[[nodiscard]] bool ShouldRecordEngineVersionUVE(ProjectCompatibilityVerdictUVE verdict,
                                                bool upgradeAccepted) noexcept;

/// "MAJOR.MINOR.PATCH+build.BUILD", the same form Core::VersionUVE::ToStringUVE() produces.
[[nodiscard]] std::string FormatProjectVersionUVE(const Platform::EditorProjectVersionUVE& version);

} // namespace UVE::Pack
