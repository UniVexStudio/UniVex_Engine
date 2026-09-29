// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/retarget/retarget_files_uve.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <optional>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/retarget/retarget_conform_uve.h"

namespace UVE::Retarget {
namespace {

namespace fs = std::filesystem;

constexpr const char* kManifestNameUVE = "retarget-backup.json";

/// One file, read and conformed in memory; nothing on disk has changed yet.
struct PlannedFileUVE final {
    fs::path file;
    std::optional<Asset::MeshAssetUVE> mesh;
    std::optional<Asset::AnimationClipAssetUVE> clip;
    std::string note;
};

[[nodiscard]] std::string TimestampUVE() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm parts{};
#if defined(_WIN32)
    gmtime_s(&parts, &now);
#else
    gmtime_r(&now, &parts);
#endif
    char text[32];
    std::strftime(text, sizeof(text), "%Y%m%d-%H%M%S", &parts);
    return text;
}

[[nodiscard]] RetargetSkeletonUVE RigFromClipUVE(const Asset::AnimationClipAssetUVE& clip) {
    RetargetSkeletonUVE rig;
    for (const Asset::AnimationAssetRestBoneUVE& rest : clip.rest) {
        RetargetBoneUVE bone;
        bone.name = rest.bone;
        bone.parent = rest.parent;
        bone.position = rest.position;
        bone.rotation = rest.rotation;
        bone.scale = rest.scale.x; // rigs scale uniformly
        rig.bones.push_back(std::move(bone));
    }
    return rig;
}

[[nodiscard]] std::vector<Asset::AnimationAssetRestBoneUVE> RestFromSkeletonUVE(const RetargetSkeletonUVE& skeleton) {
    std::vector<Asset::AnimationAssetRestBoneUVE> rest;
    for (const RetargetBoneUVE& bone : skeleton.bones) {
        rest.push_back(Asset::AnimationAssetRestBoneUVE{bone.name, bone.parent, bone.position, bone.rotation,
                                                        Math::Vector3UVE{1.0F, 1.0F, 1.0F}});
    }
    return rest;
}

[[nodiscard]] fs::path TemporaryPathUVE(const fs::path& file) {
    return fs::path{file.string() + ".retarget-tmp"};
}

[[nodiscard]] bool MoveIntoPlaceUVE(const fs::path& temporary, const fs::path& file) {
    std::error_code error;
    fs::rename(temporary, file, error);
    if (error) {
        fs::remove(temporary, error);
        return false;
    }
    return true;
}

[[nodiscard]] bool WritePlannedFileUVE(const PlannedFileUVE& planned) {
    const fs::path temporary = TemporaryPathUVE(planned.file);
    const bool written = planned.mesh.has_value() ? Asset::SaveMeshAssetUVE(*planned.mesh, temporary)
                                                  : Asset::SaveAnimationClipAssetUVE(*planned.clip, temporary);
    if (!written) {
        std::error_code error;
        fs::remove(temporary, error);
        return false;
    }
    return MoveIntoPlaceUVE(temporary, planned.file);
}

/// Copies the files into `dir` (numbered, so two files of one name never collide) and writes the
/// manifest that maps each back to where it came from.
[[nodiscard]] bool WriteBackupUVE(const fs::path& dir, const std::vector<fs::path>& files, std::string& error) {
    std::error_code code;
    fs::create_directories(dir, code);
    if (code) {
        error = "cannot create the backup folder " + dir.string() + ": " + code.message();
        return false;
    }
    nlohmann::json manifest{{"version", 1}, {"files", nlohmann::json::array()}};
    for (std::size_t index = 0U; index < files.size(); ++index) {
        const std::string name = std::to_string(index) + "_" + files[index].filename().string();
        fs::copy_file(files[index], dir / name, fs::copy_options::overwrite_existing, code);
        if (code) {
            error = "cannot back up " + files[index].string() + ": " + code.message();
            return false;
        }
        manifest["files"].push_back({{"backup", name}, {"original", files[index].generic_string()}});
    }
    std::ofstream out(dir / kManifestNameUVE, std::ios::binary | std::ios::trunc);
    out << manifest.dump(1, '\t') << '\n';
    if (!out) {
        error = "cannot write the backup manifest in " + dir.string();
        return false;
    }
    return true;
}

} // namespace

RetargetFilesResultUVE RetargetFilesUVE(const RetargetFilesRequestUVE& request, const HumanoidReferenceUVE& reference,
                                        const RetargetProgressUVE& progress) {
    RetargetFilesResultUVE result;
    const std::size_t total = 1U + request.animations.size();
    const auto report = [&](const std::size_t done, const std::string& what) {
        if (progress) {
            progress(done, total, what);
        }
    };

    // ---- 1. Read and conform everything in memory ----------------------------------------------
    std::vector<PlannedFileUVE> planned;
    {
        report(0U, request.model.filename().string());
        PlannedFileUVE model;
        model.file = request.model;
        Asset::MeshAssetUVE mesh;
        std::error_code exists;
        if (!fs::is_regular_file(request.model, exists) || !Asset::LoadMeshAssetUVE(request.model, mesh)) {
            result.error = "cannot read the character " + request.model.string();
            return result;
        }
        std::string error;
        const std::optional<RetargetSkeletonUVE> rig = RigFromMeshUVE(mesh, &error);
        if (!rig.has_value()) {
            result.error = request.model.filename().string() + ": " + error;
            return result;
        }
        const HumanoidMatchUVE match = MatchHumanoidUVE(*rig, reference);
        const std::optional<ConformedRigUVE> conformed = ConformSkeletonUVE(*rig, match, reference, &error);
        if (!conformed.has_value() || !ConformMeshUVE(mesh, *conformed, &error)) {
            result.error = request.model.filename().string() + ": " + error;
            return result;
        }
        result.heightScale = conformed->heightScale;
        model.note = std::to_string(conformed->skeleton.bones.size() - rig->bones.size()) + " bones added";
        model.mesh = std::move(mesh);
        planned.push_back(std::move(model));
    }
    for (std::size_t index = 0U; index < request.animations.size(); ++index) {
        const fs::path& path = request.animations[index];
        report(index + 1U, path.filename().string());
        PlannedFileUVE animation;
        animation.file = path;
        Asset::AnimationClipAssetUVE clip;
        if (!Asset::LoadAnimationClipAssetUVE(path, clip)) {
            animation.note = "left alone: cannot read it";
        } else if (clip.conformed) {
            animation.note = "left alone: already conformed";
        } else if (clip.rest.empty()) {
            animation.note = "left alone: it has no skeleton, import the FBX again";
        } else if (!clip.IsSkeletalUVE()) {
            animation.note = "left alone: it moves no bones";
        } else {
            std::string error;
            const RetargetSkeletonUVE rig = RigFromClipUVE(clip);
            const HumanoidMatchUVE match = MatchHumanoidUVE(rig, reference);
            const std::optional<ConformedRigUVE> conformed = ConformSkeletonUVE(rig, match, reference, &error);
            std::optional<Asset::AnimationClipAssetUVE> converted;
            if (conformed.has_value()) {
                converted = ConformClipUVE(clip, rig, *conformed, reference, &error);
            }
            if (!converted.has_value()) {
                animation.note = "left alone: " + error;
            } else {
                converted->rest = RestFromSkeletonUVE(conformed->skeleton);
                converted->conformed = true;
                animation.clip = std::move(converted);
            }
        }
        planned.push_back(std::move(animation));
    }

    // ---- 2. Back up what will change ---------------------------------------------------------------
    std::vector<fs::path> changing;
    for (const PlannedFileUVE& file : planned) {
        if (file.mesh.has_value() || file.clip.has_value()) {
            changing.push_back(file.file);
        }
    }
    result.backupDir = request.backupRoot / TimestampUVE();
    for (int suffix = 1; fs::exists(result.backupDir); ++suffix) {
        result.backupDir = request.backupRoot / (TimestampUVE() + "-" + std::to_string(suffix));
    }
    if (!WriteBackupUVE(result.backupDir, changing, result.error)) {
        std::error_code ignored;
        fs::remove_all(result.backupDir, ignored);
        result.backupDir.clear();
        return result;
    }

    // ---- 3. Write, all or nothing -----------------------------------------------------------------
    for (std::size_t index = 0U; index < planned.size(); ++index) {
        const PlannedFileUVE& file = planned[index];
        RetargetFileReportUVE entry;
        entry.file = file.file;
        entry.note = file.note;
        if (file.mesh.has_value() || file.clip.has_value()) {
            report(index, file.file.filename().string());
            if (!WritePlannedFileUVE(file)) {
                std::string restoreError;
                const bool restored = RestoreRetargetBackupUVE(result.backupDir, &restoreError);
                result.error = "cannot write " + file.file.string() +
                               (restored ? "; every file was put back" : "; putting the files back failed: " + restoreError);
                result.files.clear();
                return result;
            }
            entry.changed = true;
            if (entry.note.empty()) {
                entry.note = "conformed";
            }
        }
        result.files.push_back(std::move(entry));
    }
    report(total, {});
    result.ok = true;
    return result;
}

bool RestoreRetargetBackupUVE(const fs::path& backupDir, std::string* error) {
    const auto fail = [error](std::string text) {
        if (error != nullptr) {
            *error = std::move(text);
        }
        return false;
    };
    std::ifstream in(backupDir / kManifestNameUVE, std::ios::binary);
    if (!in) {
        return fail("no backup manifest in " + backupDir.string());
    }
    std::vector<std::pair<fs::path, fs::path>> pairs;
    try {
        const nlohmann::json manifest = nlohmann::json::parse(in);
        for (const nlohmann::json& file : manifest.at("files")) {
            pairs.emplace_back(backupDir / file.at("backup").get<std::string>(),
                               fs::path{file.at("original").get<std::string>()});
        }
    } catch (const std::exception& exception) {
        return fail(std::string{"the backup manifest is damaged: "} + exception.what());
    }
    bool allRestored = true;
    std::string failures;
    for (const auto& [backup, original] : pairs) {
        std::error_code code;
        fs::copy_file(backup, original, fs::copy_options::overwrite_existing, code);
        if (code) {
            allRestored = false;
            failures += (failures.empty() ? "" : "; ") + original.string() + ": " + code.message();
        }
    }
    return allRestored || fail(failures);
}

} // namespace UVE::Retarget
