// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "retarget_files.h"
#include "model_animations.h"

#include "skeleton_asset.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "animation_clip_asset.h"
#include "mesh_asset.h"
#include "retarget_conform.h"

namespace UVE::Retarget {
namespace {

namespace fs = std::filesystem;

constexpr const char* kManifestName = "retarget-backup.json";

[[nodiscard]] std::string Timestamp() {
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

[[nodiscard]] RetargetSkeleton RigFromClip(const Asset::AnimationClipAsset& clip) {
    RetargetSkeleton rig;
    rig.bones.reserve(clip.rest.size());
    for (const Asset::AnimationAssetRestBone& rest : clip.rest) {
        RetargetBone bone;
        bone.name = rest.bone;
        bone.parent = rest.parent;
        bone.position = rest.position;
        bone.rotation = rest.rotation;
        bone.scale = rest.scale.x; // rigs scale uniformly
        rig.bones.push_back(std::move(bone));
    }
    return rig;
}

[[nodiscard]] std::vector<Asset::AnimationAssetRestBone> RestFromSkeleton(const RetargetSkeleton& skeleton) {
    std::vector<Asset::AnimationAssetRestBone> rest;
    rest.reserve(skeleton.bones.size());
    for (const RetargetBone& bone : skeleton.bones) {
        rest.push_back(Asset::AnimationAssetRestBone{bone.name, bone.parent, bone.position, bone.rotation,
                                                        Math::Vector3{1.0F, 1.0F, 1.0F}});
    }
    return rest;
}

[[nodiscard]] std::string RestRigCacheKey(const std::vector<Asset::AnimationAssetRestBone>& rest) {
    // Bit-exact key over every rest bone so identical rigs (50 embedded takes or 1,000 Mixamo clips)
    // reuse one Rig Brain match + conformed skeleton without any numerical drift.
    std::string key;
    key.reserve(rest.size() * 64U);
    const auto appendBytes = [&key](const void* data, const std::size_t size) {
        key.append(static_cast<const char*>(data), size);
    };
    const auto appendFloat = [&appendBytes](const float value) {
        const float normalized = value == 0.0F ? 0.0F : value;
        std::uint32_t bits = 0U;
        std::memcpy(&bits, &normalized, sizeof(bits));
        appendBytes(&bits, sizeof(bits));
    };
    const std::uint32_t count = static_cast<std::uint32_t>(rest.size());
    appendBytes(&count, sizeof(count));
    for (const Asset::AnimationAssetRestBone& bone : rest) {
        const std::uint32_t nameSize = static_cast<std::uint32_t>(bone.bone.size());
        appendBytes(&nameSize, sizeof(nameSize));
        key.append(bone.bone);
        appendBytes(&bone.parent, sizeof(bone.parent));
        appendFloat(bone.position.x);
        appendFloat(bone.position.y);
        appendFloat(bone.position.z);
        appendFloat(bone.rotation.x);
        appendFloat(bone.rotation.y);
        appendFloat(bone.rotation.z);
        appendFloat(bone.rotation.w);
        appendFloat(bone.scale.x);
    }
    return key;
}

[[nodiscard]] fs::path TemporaryPath(const fs::path& file) {
    return fs::path{file.string() + ".retarget-tmp"};
}

[[nodiscard]] bool MoveIntoPlace(const fs::path& temporary, const fs::path& file) {
    std::error_code error;
    fs::rename(temporary, file, error);
    if (error) {
        error.clear();
        fs::copy_file(temporary, file, fs::copy_options::overwrite_existing, error);
        std::error_code removeError;
        fs::remove(temporary, removeError);
        return !error;
    }
    return true;
}

[[nodiscard]] RetargetAnimationCheck CheckSummary(const Asset::AnimationClipSummary& summary) {
    if (!summary.valid) {
        return {RetargetAnimationState::Unreadable, "cannot read it"};
    }
    if (summary.conformed) {
        return {RetargetAnimationState::AlreadyConformed, "already conformed"};
    }
    if (!summary.hasRest) {
        return {RetargetAnimationState::NoSkeleton, "it has no skeleton, import the FBX again"};
    }
    if (!summary.hasBones) {
        return {RetargetAnimationState::NoBones, "it moves no bones"};
    }
    return {RetargetAnimationState::Ready, {}};
}

[[nodiscard]] RetargetAnimationCheck CheckClip(const Asset::AnimationClipAsset& clip) {
    if (clip.conformed) {
        return {RetargetAnimationState::AlreadyConformed, "already conformed"};
    }
    if (clip.rest.empty()) {
        return {RetargetAnimationState::NoSkeleton, "it has no skeleton, import the FBX again"};
    }
    if (!clip.IsSkeletal()) {
        return {RetargetAnimationState::NoBones, "it moves no bones"};
    }
    return {RetargetAnimationState::Ready, {}};
}

/// Lazily creates the run's backup directory and backs up each unique real on-disk file once before
/// it is modified. Writing the manifest before RestoreRetargetBackup guarantees atomic rollback
/// while streaming 1,000+ loose clips in O(1) memory.
class IncrementalBackup final {
public:
    explicit IncrementalBackup(const fs::path& backupRoot) : m_backupRoot(backupRoot) {}

    [[nodiscard]] bool BackupOnce(const fs::path& realFile, std::string& error) {
        const fs::path normalized = realFile.lexically_normal();
        const std::string key = normalized.generic_string();
        if (!m_seen.insert(key).second) {
            return true;
        }
        if (!EnsureDir(error)) {
            return false;
        }
        std::error_code existsError;
        if (!fs::exists(normalized, existsError)) {
            m_entries.push_back(Entry{true, {}, normalized});
            return true;
        }
        const std::string name = std::to_string(m_entries.size()) + "_" + normalized.filename().string();
        std::error_code copyError;
        fs::copy_file(normalized, m_dir / name, fs::copy_options::overwrite_existing, copyError);
        if (copyError) {
            error = "cannot back up " + normalized.string() + ": " + copyError.message();
            return false;
        }
        m_entries.push_back(Entry{false, name, normalized});
        return true;
    }

    [[nodiscard]] bool WriteManifest(std::string& error) {
        if (m_entries.empty()) {
            return true;
        }
        if (!EnsureDir(error)) {
            return false;
        }
        nlohmann::json manifest{{"version", 1}, {"files", nlohmann::json::array()}};
        for (const Entry& entry : m_entries) {
            if (entry.created) {
                manifest["files"].push_back({{"created", true}, {"original", entry.original.generic_string()}});
            } else {
                manifest["files"].push_back({{"backup", entry.backupName}, {"original", entry.original.generic_string()}});
            }
        }
        std::ofstream out(m_dir / kManifestName, std::ios::binary | std::ios::trunc);
        out << manifest.dump(1, '\t') << '\n';
        if (!out) {
            error = "cannot write the backup manifest in " + m_dir.string();
            return false;
        }
        return true;
    }

    [[nodiscard]] bool Rollback(std::string& restoreError) {
        if (m_entries.empty() || m_dir.empty()) {
            return true;
        }
        std::string manifestError;
        if (!WriteManifest(manifestError)) {
            restoreError = manifestError;
            return false;
        }
        return RestoreRetargetBackup(m_dir, &restoreError);
    }

    [[nodiscard]] bool HasBackup() const noexcept { return !m_entries.empty() && !m_dir.empty(); }
    [[nodiscard]] const fs::path& Dir() const noexcept { return m_dir; }

private:
    struct Entry final {
        bool created = false;
        std::string backupName;
        fs::path original;
    };

    [[nodiscard]] bool EnsureDir(std::string& error) {
        if (!m_dir.empty()) {
            return true;
        }
        const std::string stamp = Timestamp();
        fs::path candidate = m_backupRoot / stamp;
        for (int suffix = 1; fs::exists(candidate); ++suffix) {
            candidate = m_backupRoot / (stamp + "-" + std::to_string(suffix));
        }
        std::error_code code;
        fs::create_directories(candidate, code);
        if (code) {
            error = "cannot create the backup folder " + candidate.string() + ": " + code.message();
            return false;
        }
        m_dir = std::move(candidate);
        return true;
    }

    fs::path m_backupRoot;
    fs::path m_dir;
    std::unordered_set<std::string> m_seen;
    std::vector<Entry> m_entries;
};

} // namespace

RetargetAnimationCheck CheckAnimationForRetarget(const fs::path& file) {
    return CheckSummary(Asset::InspectAnimationClipFileSummary(file));
}

RetargetFilesResult RetargetFiles(const RetargetFilesRequest& request, const HumanoidReference& reference,
                                        const RetargetProgress& progress) {
    RetargetFilesResult result;
    const bool hasModel = !request.model.empty();
    // The mesh nodes of the character published beside it (`<model>.part-<node>.uvmodel`): each is a mesh of
    // its own that binds to the same skeleton, so it is conformed with the model or the scene would draw
    // old meshes on the conformed rig.
    std::vector<fs::path> partFiles;
    if (hasModel) {
        const std::string partPrefix = request.model.stem().string() + ".part-";
        std::error_code listError;
        for (const fs::directory_entry& entry : fs::directory_iterator(request.model.parent_path(), listError)) {
            const std::string name = entry.path().filename().string();
            if (entry.path().extension() == ".uvmodel" && name.rfind(partPrefix, 0U) == 0U &&
                entry.is_regular_file(listError)) {
                partFiles.push_back(entry.path());
            }
        }
        std::sort(partFiles.begin(), partFiles.end());
    }
    const std::size_t total = (hasModel ? 1U : 0U) + partFiles.size() + request.animations.size();
    std::size_t progressDone = 0U;
    const auto report = [&](const std::size_t done, const std::string& what) {
        if (progress) {
            progress(done, total, what);
        }
    };

    if (!hasModel && request.animations.empty()) {
        result.error = "nothing to retarget: no character or animations were given";
        return result;
    }

    // Names first; the Rig Brain for the bones they cannot place.
    const RigBrain* const brain = !request.useBrain ? nullptr : (request.brain != nullptr ? request.brain : &GetDefaultRigBrain());
    const auto matchOf = [&](const RetargetSkeleton& rig) {
        return brain != nullptr ? MatchHumanoidWithBrain(rig, reference, *brain) : MatchHumanoid(rig, reference);
    };

    // ---- 1. Validate & conform the character + parts in memory first (fail fast, zero disk changes) ---
    Asset::MeshAsset conformedModelMesh;
    std::string modelNote;
    fs::path skeletonFile;
    Asset::SkeletonAsset conformedSkeletonAsset;
    std::vector<Asset::MeshAsset> conformedPartMeshes;
    if (hasModel) {
        report(0U, request.model.filename().string());
        std::error_code exists;
        if (!fs::is_regular_file(request.model, exists) || !Asset::LoadMeshAsset(request.model, conformedModelMesh)) {
            result.error = "cannot read the character " + request.model.string();
            return result;
        }
        std::string error;
        const std::optional<RetargetSkeleton> rig = RigFromMesh(conformedModelMesh, &error);
        if (!rig.has_value()) {
            result.error = request.model.filename().string() + ": " + error;
            return result;
        }
        const HumanoidMatch match = matchOf(*rig);
        // An all-but-complete character is renamed, not rebuilt: body bones are added only when it
        // lacks many of them.
        ConformOptions options;
        options.addBodyBonesOnlyWhenLacking = true;
        const std::optional<ConformedRig> conformed = ConformSkeleton(*rig, match, reference, &error, options);
        const Asset::MeshAsset before = conformedModelMesh;
        // The character keeps its own rest pose: no vertex moves (the A-pose is where clips take it).
        if (!conformed.has_value() ||
            !ConformMesh(conformedModelMesh, *conformed, &error, SkinBlend::DualQuaternion, ConformMeshMode::KeepRestPose)) {
            result.error = request.model.filename().string() + ": " + error;
            return result;
        }
        result.heightScale = conformed->heightScale;
        result.meshDistortion = MeasureMeshDistortion(before, conformedModelMesh);
        char distortion[64];
        std::snprintf(distortion, sizeof(distortion), "; %.1f%% of edges changed by more than 10%%",
                      static_cast<double>(result.meshDistortion.fractionOverTenPercent) * 100.0);
        const std::size_t added =
            conformed->skeleton.bones.size() > rig->bones.size() ? conformed->skeleton.bones.size() - rig->bones.size() : 0U;
        modelNote = (added == 0U ? std::string{"renamed only, no bone added"} : std::to_string(added) + " bones added") + distortion;

        // The character's skeleton beside the model (what the editor builds its Skeleton3D from):
        // the engine's bones resting in the A-pose. The mesh stays bound where it was made, so the
        // character stands in the A-pose without one vertex of its file moving, and clips conformed
        // to the humanoid play on it exactly.
        skeletonFile = request.model;
        skeletonFile.replace_extension(".uvskel");
        conformedSkeletonAsset.skeletonId = request.model.stem().string();
        conformedSkeletonAsset.joints.reserve(conformed->skeleton.bones.size());
        for (const RetargetBone& bone : conformed->skeleton.bones) {
            conformedSkeletonAsset.joints.push_back(Asset::SkeletonJoint{
                bone.name, bone.parent, bone.position, bone.rotation, Math::Vector3{bone.scale, bone.scale, bone.scale}});
        }

        conformedPartMeshes.reserve(partFiles.size());
        for (const fs::path& partFile : partFiles) {
            Asset::MeshAsset partMesh;
            std::string partError;
            if (!Asset::LoadMeshAsset(partFile, partMesh) ||
                !ConformMesh(partMesh, *conformed, &partError, SkinBlend::DualQuaternion, ConformMeshMode::KeepRestPose)) {
                result.error = partFile.filename().string() + ": " + (partError.empty() ? "cannot read it" : partError);
                return result;
            }
            conformedPartMeshes.push_back(std::move(partMesh));
        }
    }

    // Reserve report slots in exact canonical order: model, .uvskel, parts, then animations in request order.
    const std::size_t animReportBase = hasModel ? (2U + partFiles.size()) : 0U;
    result.files.resize(animReportBase + request.animations.size());
    for (std::size_t i = 0U; i < request.animations.size(); ++i) {
        result.files[animReportBase + i].file = request.animations[i];
    }

    // Partition animations into embedded .uvmodel#take groups (1 read + 1 write per model) and loose .uvanim files.
    struct EmbeddedReq final {
        std::size_t requestIndex = 0U;
        std::string take;
    };
    std::map<fs::path, std::vector<EmbeddedReq>> embeddedGroups;
    std::vector<std::size_t> looseIndices;
    looseIndices.reserve(request.animations.size());
    for (std::size_t i = 0U; i < request.animations.size(); ++i) {
        fs::path backingModel;
        std::string take;
        if (Asset::SplitEmbeddedClipPath(request.animations[i], backingModel, take)) {
            embeddedGroups[backingModel.lexically_normal()].push_back(EmbeddedReq{i, std::move(take)});
        } else {
            looseIndices.push_back(i);
        }
    }

    // Cache Rig Brain match + conformed rig by exact rest-bone layout: 1x solve across 1,000 clips of one rig.
    struct CachedClipRig final {
        RetargetSkeleton rig;
        std::optional<ConformedRig> conformed;
        std::string error;
    };
    std::unordered_map<std::string, CachedClipRig> clipRigCache;
    const auto conformClip = [&](const Asset::AnimationClipAsset& clip, std::string& outError)
        -> std::optional<Asset::AnimationClipAsset> {
        const std::string rigKey = RestRigCacheKey(clip.rest);
        auto it = clipRigCache.find(rigKey);
        if (it == clipRigCache.end()) {
            CachedClipRig cached;
            cached.rig = RigFromClip(clip);
            const HumanoidMatch match = matchOf(cached.rig);
            cached.conformed = ConformSkeleton(cached.rig, match, reference, &cached.error);
            it = clipRigCache.emplace(rigKey, std::move(cached)).first;
        }
        const CachedClipRig& cached = it->second;
        if (!cached.conformed.has_value()) {
            outError = cached.error;
            return std::nullopt;
        }
        if (!hasModel && result.heightScale == 1.0F) {
            result.heightScale = cached.conformed->heightScale;
        }
        std::optional<Asset::AnimationClipAsset> converted =
            ConformClip(clip, cached.rig, *cached.conformed, reference, &outError);
        if (converted.has_value()) {
            converted->rest = RestFromSkeleton(cached.conformed->skeleton);
            converted->conformed = true;
        }
        return converted;
    };

    // ---- 2. Stream backups & atomic writes (all-or-nothing rollback on any error) ---------------------
    IncrementalBackup backup(request.backupRoot);
    const auto failWithRollback = [&](const std::string& message) -> RetargetFilesResult {
        std::string restoreError;
        const bool restored = backup.Rollback(restoreError);
        result.error = message + (restored ? "; every file was put back" : "; putting the files back failed: " + restoreError);
        result.files.clear();
        result.backupDir.clear();
        return result;
    };

    // Write the character model (merging any embedded takes belonging to it in ONE atomic write).
    if (hasModel) {
        const fs::path normalizedModel = request.model.lexically_normal();
        std::vector<Asset::AnimationClipAsset> modelTakes;
        std::uint64_t modelTakeStamp = 0U;
        const bool hasExistingTakes = Asset::LoadAllModelAnimations(request.model, modelTakes, &modelTakeStamp);

        // If any requested embedded takes live inside request.model, conform them in memory right now.
        if (const auto groupIt = embeddedGroups.find(normalizedModel); groupIt != embeddedGroups.end()) {
            std::unordered_map<std::string, std::size_t> takeIndex;
            takeIndex.reserve(modelTakes.size());
            for (std::size_t ti = 0U; ti < modelTakes.size(); ++ti) {
                takeIndex.emplace(modelTakes[ti].clipId, ti);
            }
            for (const EmbeddedReq& req : groupIt->second) {
                report(++progressDone, Asset::AssetDisplayName(request.animations[req.requestIndex]));
                RetargetFileReport& slot = result.files[animReportBase + req.requestIndex];
                const auto found = hasExistingTakes ? takeIndex.find(req.take) : takeIndex.end();
                if (found == takeIndex.end()) {
                    slot.note = "left alone: cannot read it";
                    continue;
                }
                Asset::AnimationClipAsset& existing = modelTakes[found->second];
                const RetargetAnimationCheck check = CheckClip(existing);
                if (check.state != RetargetAnimationState::Ready) {
                    slot.note = "left alone: " + check.note;
                    continue;
                }
                std::string clipError;
                std::optional<Asset::AnimationClipAsset> converted = conformClip(existing, clipError);
                if (!converted.has_value()) {
                    slot.note = "left alone: " + clipError;
                    continue;
                }
                converted->clipId = req.take;
                existing = std::move(*converted);
                slot.changed = true;
                slot.note = "conformed";
            }
            embeddedGroups.erase(groupIt);
        }

        std::string backupError;
        if (!backup.BackupOnce(request.model, backupError)) {
            return failWithRollback(backupError);
        }
        const fs::path temporary = TemporaryPath(request.model);
        bool written = Asset::SaveMeshAsset(conformedModelMesh, temporary);
        if (written && !modelTakes.empty()) {
            written = Asset::SaveModelAnimations(temporary, modelTakes, modelTakeStamp);
        }
        if (!written || !MoveIntoPlace(temporary, request.model)) {
            std::error_code rmErr;
            fs::remove(temporary, rmErr);
            return failWithRollback("cannot write " + request.model.string());
        }
        result.files[0] = RetargetFileReport{request.model, true, modelNote};
        ++progressDone;

        // Write sibling .uvskel (A-pose rest skeleton).
        if (!backup.BackupOnce(skeletonFile, backupError)) {
            return failWithRollback(backupError);
        }
        const fs::path skelTemp = TemporaryPath(skeletonFile);
        if (!Asset::SaveSkeletonAsset(conformedSkeletonAsset, skelTemp) || !MoveIntoPlace(skelTemp, skeletonFile)) {
            std::error_code rmErr;
            fs::remove(skelTemp, rmErr);
            return failWithRollback("cannot write " + skeletonFile.string());
        }
        result.files[1] = RetargetFileReport{skeletonFile, true, "the character's skeleton: the engine's bones, resting in the A-pose"};

        // Write mesh parts published beside the character.
        for (std::size_t p = 0U; p < partFiles.size(); ++p) {
            report(++progressDone, partFiles[p].filename().string());
            if (!backup.BackupOnce(partFiles[p], backupError)) {
                return failWithRollback(backupError);
            }
            const fs::path partTemp = TemporaryPath(partFiles[p]);
            if (!Asset::SaveMeshAsset(conformedPartMeshes[p], partTemp) || !MoveIntoPlace(partTemp, partFiles[p])) {
                std::error_code rmErr;
                fs::remove(partTemp, rmErr);
                return failWithRollback("cannot write " + partFiles[p].string());
            }
            result.files[2U + p] = RetargetFileReport{partFiles[p], true, "conformed with the character"};
        }
    }

    // Batch-conform embedded takes inside any other .uvmodel (e.g. already-conformed model or folder of .uvmodels).
    for (const auto& [backingModel, reqs] : embeddedGroups) {
        std::vector<Asset::AnimationClipAsset> takes;
        std::uint64_t stamp = 0U;
        const bool loaded = Asset::LoadAllModelAnimations(backingModel, takes, &stamp);
        std::unordered_map<std::string, std::size_t> takeIndex;
        takeIndex.reserve(takes.size());
        for (std::size_t ti = 0U; ti < takes.size(); ++ti) {
            takeIndex.emplace(takes[ti].clipId, ti);
        }
        bool anyTakeChanged = false;
        for (const EmbeddedReq& req : reqs) {
            report(++progressDone, Asset::AssetDisplayName(request.animations[req.requestIndex]));
            RetargetFileReport& slot = result.files[animReportBase + req.requestIndex];
            const auto found = loaded ? takeIndex.find(req.take) : takeIndex.end();
            if (found == takeIndex.end()) {
                slot.note = "left alone: cannot read it";
                continue;
            }
            Asset::AnimationClipAsset& existing = takes[found->second];
            const RetargetAnimationCheck check = CheckClip(existing);
            if (check.state != RetargetAnimationState::Ready) {
                slot.note = "left alone: " + check.note;
                continue;
            }
            std::string clipError;
            std::optional<Asset::AnimationClipAsset> converted = conformClip(existing, clipError);
            if (!converted.has_value()) {
                slot.note = "left alone: " + clipError;
                continue;
            }
            converted->clipId = req.take;
            existing = std::move(*converted);
            anyTakeChanged = true;
            slot.changed = true;
            slot.note = "conformed";
        }
        if (anyTakeChanged) {
            std::string backupError;
            if (!backup.BackupOnce(backingModel, backupError)) {
                return failWithRollback(backupError);
            }
            // Copy to a temporary file, update its takes section once, then move atomically into place.
            const fs::path temporary = TemporaryPath(backingModel);
            std::error_code copyErr;
            fs::copy_file(backingModel, temporary, fs::copy_options::overwrite_existing, copyErr);
            if (copyErr || !Asset::SaveModelAnimations(temporary, takes, stamp) || !MoveIntoPlace(temporary, backingModel)) {
                std::error_code rmErr;
                fs::remove(temporary, rmErr);
                return failWithRollback("cannot write " + backingModel.string());
            }
        }
    }

    // Stream loose .uvanim files one clip at a time (O(1) peak memory even for 1,000+ animations).
    for (const std::size_t reqIndex : looseIndices) {
        const fs::path& path = request.animations[reqIndex];
        report(++progressDone, path.filename().string());
        RetargetFileReport& slot = result.files[animReportBase + reqIndex];
        // Fast zero-DOM skip for clips that are already conformed, missing a rest skeleton, or non-skeletal.
        const RetargetAnimationCheck quickCheck = CheckAnimationForRetarget(path);
        if (quickCheck.state != RetargetAnimationState::Ready) {
            slot.note = "left alone: " + quickCheck.note;
            continue;
        }
        Asset::AnimationClipAsset clip;
        if (!Asset::LoadAnimationClipAsset(path, clip)) {
            slot.note = "left alone: cannot read it";
            continue;
        }
        const RetargetAnimationCheck check = CheckClip(clip);
        if (check.state != RetargetAnimationState::Ready) {
            slot.note = "left alone: " + check.note;
            continue;
        }
        std::string clipError;
        std::optional<Asset::AnimationClipAsset> converted = conformClip(clip, clipError);
        if (!converted.has_value()) {
            slot.note = "left alone: " + clipError;
            continue;
        }
        std::string backupError;
        if (!backup.BackupOnce(path, backupError)) {
            return failWithRollback(backupError);
        }
        const fs::path temporary = TemporaryPath(path);
        if (!Asset::SaveAnimationClipAsset(*converted, temporary) || !MoveIntoPlace(temporary, path)) {
            std::error_code rmErr;
            fs::remove(temporary, rmErr);
            return failWithRollback("cannot write " + path.string());
        }
        slot.changed = true;
        slot.note = "conformed";
    }

    std::string manifestError;
    if (!backup.WriteManifest(manifestError)) {
        return failWithRollback(manifestError);
    }
    if (backup.HasBackup()) {
        result.backupDir = backup.Dir();
    }
    report(total, {});
    result.ok = true;
    return result;
}

bool RestoreRetargetBackup(const fs::path& backupDir, std::string* error) {
    const auto fail = [error](std::string text) {
        if (error != nullptr) {
            *error = std::move(text);
        }
        return false;
    };
    std::ifstream in(backupDir / kManifestName, std::ios::binary);
    if (!in) {
        return fail("no backup manifest in " + backupDir.string());
    }
    std::vector<std::pair<fs::path, fs::path>> pairs;
    std::vector<fs::path> created;
    try {
        const nlohmann::json manifest = nlohmann::json::parse(in);
        for (const nlohmann::json& file : manifest.at("files")) {
            if (file.value("created", false)) {
                created.emplace_back(file.at("original").get<std::string>());
                continue;
            }
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
    for (const fs::path& file : created) {
        std::error_code code;
        fs::remove(file, code);
    }
    return allRestored || fail(failures);
}

} // namespace UVE::Retarget
