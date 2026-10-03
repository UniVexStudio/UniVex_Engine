// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "uve/retarget/retarget_conform_uve.h"
#include "uve/retarget/retarget_humanoid_uve.h"

namespace UVE::Retarget {

/// What to conform: one character and the animations to play on it. Everything is changed where
/// it is (no copies), after the originals are saved under `backupRoot`.
struct RetargetFilesRequestUVE final {
    /// The character, a `.uvmodel` with a skin.
    std::filesystem::path model;
    /// The animations, `.uvanim` clips that carry their rest skeleton (imported from an FBX).
    std::vector<std::filesystem::path> animations;
    /// Where backups go; each run makes a folder of its own inside it (for example
    /// `<content>/.retarget-backup`).
    std::filesystem::path backupRoot;
};

struct RetargetFileReportUVE final {
    std::filesystem::path file;
    /// Written (or, for a skipped animation, deliberately left alone with a note).
    bool changed = false;
    /// What happened, or why the file was left alone.
    std::string note;
};

struct RetargetFilesResultUVE final {
    bool ok = false;
    /// Set when `ok` is false: why nothing changed (or that everything was put back).
    std::string error;
    /// The folder holding the originals; pass it to RestoreRetargetBackupUVE to undo.
    std::filesystem::path backupDir;
    /// The character first, then each animation in request order.
    std::vector<RetargetFileReportUVE> files;
    /// The conformed character's hips height over the humanoid's, when the character was conformed.
    float heightScale = 1.0F;
    /// How much the character's shape changed by going into the A-pose (the mesh is only moved, its
    /// triangles and weights are kept). A large share of stretched edges means the rig's rest pose
    /// was far from the A-pose; check the shoulders.
    MeshDistortionUVE meshDistortion;
};

/// Whether an animation can be conformed, and if not, why not.
enum class RetargetAnimationStateUVE : std::uint8_t {
    Ready = 0,
    /// Already the humanoid's: nothing to do.
    AlreadyConformed,
    /// Saved before clips carried their skeleton: import the FBX again.
    NoSkeleton,
    /// Moves no bones (an object animation).
    NoBones,
    Unreadable,
};

struct RetargetAnimationCheckUVE final {
    RetargetAnimationStateUVE state = RetargetAnimationStateUVE::Unreadable;
    /// The reason in a line, for anything but Ready.
    std::string note;
};

/// Reads `file` and says whether RetargetFilesUVE would conform it.
[[nodiscard]] RetargetAnimationCheckUVE CheckAnimationForRetargetUVE(const std::filesystem::path& file);

/// Called as work proceeds: files done so far, files in all, and what is being worked on.
using RetargetProgressUVE = std::function<void(std::size_t done, std::size_t total, const std::string& what)>;

/// Conforms the character and its animations to `reference` in place:
///  1. everything is read and conformed in memory first; a character that cannot be conformed
///     stops the run before any file is touched, and an animation that cannot be (no skeleton,
///     already conformed) is left alone with a note;
///  2. the originals are copied into a new folder under `backupRoot`, with a manifest;
///  3. each result is written beside its file and moved into place; if any write fails, every
///     file is put back from the backup and the run reports the failure.
[[nodiscard]] RetargetFilesResultUVE RetargetFilesUVE(const RetargetFilesRequestUVE& request,
                                                     const HumanoidReferenceUVE& reference,
                                                     const RetargetProgressUVE& progress = {});

/// Puts back every file a run saved in `backupDir`. False, with the reason in `error`, when the
/// manifest is missing or a file cannot be restored (the files that could be are restored).
[[nodiscard]] bool RestoreRetargetBackupUVE(const std::filesystem::path& backupDir, std::string* error = nullptr);

} // namespace UVE::Retarget
