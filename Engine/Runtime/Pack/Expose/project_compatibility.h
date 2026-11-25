// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>

#include "editor_project_package.h"
#include "engine_version.h"

namespace UVE::Pack {

/// The parts of a version, most significant first.
enum class VersionComponent : std::uint8_t {
    Major = 0,
    Minor,
    Patch,
    Build,
};

/// How a project's recorded engine version relates to the running engine.
enum class ProjectCompatibilityVerdict : std::uint8_t {
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
struct ProjectCompatibilityPolicy final {
    VersionComponent newerRejectedFrom = VersionComponent::Major;
    VersionComponent olderUpgradePromptFrom = VersionComponent::Minor;
};

inline constexpr ProjectCompatibilityPolicy kDefaultProjectCompatibilityPolicy{};

struct ProjectCompatibility final {
    ProjectCompatibilityVerdict verdict = ProjectCompatibilityVerdict::Same;
    /// A sentence naming both versions, suitable for a log line or a dialog.
    std::string message;

    /// False only for a project this engine must refuse to open.
    [[nodiscard]] bool CanOpen() const noexcept {
        return verdict != ProjectCompatibilityVerdict::NewerIncompatible;
    }
    /// Whether the user must confirm before the project is upgraded.
    [[nodiscard]] bool NeedsUpgradeConfirmation() const noexcept {
        return verdict == ProjectCompatibilityVerdict::OlderNeedsUpgrade;
    }
};

/// Compares the engine version a project last recorded with the running engine's. Pure and
/// thread-safe; shared by the editor's open path and any tool that lists projects.
[[nodiscard]] ProjectCompatibility EvaluateProjectCompatibility(
    const Platform::EditorProjectVersion& projectVersion, const Core::Version& engineVersion,
    const ProjectCompatibilityPolicy& policy = kDefaultProjectCompatibilityPolicy);

/// Whether closing an editor session may record the running engine's version in the project.
/// A newer project never records an older engine's version, and an upgrade that needed
/// confirmation records it only once the user accepted.
[[nodiscard]] bool ShouldRecordEngineVersion(ProjectCompatibilityVerdict verdict,
                                                bool upgradeAccepted) noexcept;

/// "MAJOR.MINOR.PATCH+build.BUILD", the same form Core::Version::ToString() produces.
[[nodiscard]] std::string FormatProjectVersion(const Platform::EditorProjectVersion& version);

} // namespace UVE::Pack
