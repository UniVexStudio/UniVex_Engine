// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Retarget window: pick a character, see how its bones answer the humanoid (green: found and
// fine, yellow: found but look, red: broken, grey: missing, conforming adds it), then Generate.
// Generate conforms the character and the animations in place, keeping a backup for Undo.

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>


#include "fbx_mesh_converter.h"
#include "i_asset_database.h"
#include "i_asset_manager.h"
#include "i_project_file_index.h"
#include "mesh_asset.h"
#include "engine_services.h"
#include "editor.h"
#include "i_entity_manager.h"
#include "skeleton_3d.h"
#include "retarget_brain.h"
#include "retarget_files.h"
#include "retarget_humanoid.h"

namespace UVE::Editor {
namespace {

[[nodiscard]] std::string LowerUVE(std::string text) {
    std::ranges::transform(text, text.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

} // namespace

void EditorUVE::OpenRetargetWindowUVE(std::vector<std::filesystem::path> animations, std::filesystem::path target) {
    if (animations.empty() && !target.empty()) {
        const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
        if (IsModelSourcePathUVE(target)) {
            const std::filesystem::path absSource = project.contentRoot / target;
            const std::filesystem::path parentDir = absSource.parent_path();
            const std::string stem = absSource.stem().string();
            std::error_code dirError;
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(parentDir, dirError)) {
                if (!entry.is_regular_file()) {
                    continue;
                }
                const std::filesystem::path candidate = entry.path();
                if (LowerUVE(candidate.extension().string()) != ".uvanim") {
                    continue;
                }
                const std::string candidateStem = candidate.stem().string();
                if (candidateStem == stem || candidateStem.rfind(stem + "_", 0U) == 0U) {
                    if (std::ranges::find(animations, candidate) == animations.end()) {
                        animations.push_back(candidate);
                    }
                }
            }
            if (const EditorModelSourceInfoUVE* const info = FindModelSourceInfoUVE(target);
                info != nullptr && info->animationOnly) {
                target.clear();
            }
        }
    }
    RetargetWindowStateUVE window;
    window.animations = std::move(animations);
    window.target = std::move(target);
    m_retargetWindow = std::move(window);
    // The window's own world (the level is not touched), and the Rig Brain trained meanwhile.
    static_cast<void>(BeginRetargetPreviewUVE());
    Retarget::WarmDefaultRigBrainUVE();
}

void EditorUVE::CloseRetargetWindowUVE() {
    if (m_retargetWindow.has_value() && m_retargetWindow->job.valid()) {
        m_retargetWindow->job.wait(); // never leave files half written
        static_cast<void>(m_retargetWindow->job.get());
    }
    if (!EndRetargetPreviewUVE()) {
        return; // the scene did not come back: the window stays, so nothing is lost
    }
    if (m_retargetWindow.has_value()) {
        // The scene was put aside while files changed: bring its skeletons up to date.
        for (const std::filesystem::path& model : m_retargetWindow->changedModels) {
            if (!model.empty()) {
                static_cast<void>(RefreshSkeletonsForRetargetedModelUVE(model));
            }
        }
    }
    m_retargetWindow.reset();
}

std::filesystem::path EditorUVE::ResolveRetargetModelFileUVE(const std::filesystem::path& target) const {
    if (target.empty()) {
        return {};
    }
    const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    if (LowerUVE(target.extension().string()) == ".uvmodel") {
        return project.contentRoot / target;
    }
    if (const EditorModelSourceInfoUVE* const info = FindModelSourceInfoUVE(target);
        info != nullptr && info->animationOnly) {
        return {};
    }
    return GetImportedModelPathUVE(target);
}

std::size_t EditorUVE::RefreshSkeletonsForRetargetedModelUVE(const std::filesystem::path& modelFile) {
    if (modelFile.empty()) {
        return 0U;
    }
    std::size_t changed = 0U;
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const std::filesystem::path wanted = modelFile.lexically_normal();
    entityManager.ForEachUVE<Scene::Skeleton3DComponentUVE>(
        [&](const Scene::EntityUVE, Scene::Skeleton3DComponentUVE& skeleton) {
            const std::filesystem::path source{skeleton.skeletonAssetPath};
            if (source.empty() || !IsModelSourcePathUVE(source) || GetImportedModelPathUVE(source) != wanted) {
                return;
            }
            const std::optional<Asset::GltfSkeletonUVE> bones =
                ReadSkeletonForSourceUVE(source, Scene::kMaximumSkeletonBonesUVE);
            if (!bones.has_value()) {
                return;
            }
            skeleton.bones.clear();
            for (const Asset::GltfJointUVE& joint : bones->joints) {
                skeleton.bones.push_back(
                    Scene::SkeletonBoneUVE{joint.name, joint.parentIndex, joint.translation, joint.rotation, joint.scale});
            }
            skeleton.pose.clear();
            ++changed;
        });
    if (changed > 0U) {
        m_sceneDirty = true;
    }
    return changed;
}

void EditorUVE::FinishRetargetJobUVE(RetargetWindowStateUVE& window, Retarget::RetargetFilesResultUVE result) {
    window.statusIsError = !result.ok;
    if (!result.ok) {
        window.status = result.error;
        window.planStale = true;
        return;
    }
    // The renderer and the animation players may hold the old files.
    Asset::IAssetDatabaseUVE& database = m_services->GetAssetDatabaseUVE();
    for (const Retarget::RetargetFileReportUVE& file : result.files) {
        if (!file.changed) {
            continue;
        }
        const Asset::AssetGuidUVE guid = database.RegisterUVE(file.file);
        if (guid != Asset::kInvalidAssetGuidUVE) {
            m_services->GetAssetManagerUVE().ReloadUVE(guid, database);
        }
    }
    const std::size_t skeletons =
        window.jobModel.empty() ? 0U : RefreshSkeletonsForRetargetedModelUVE(window.jobModel);
    if (!window.jobModel.empty()) {
        window.changedModels.push_back(window.jobModel);
    }
    ClearMeshThumbnailCacheUVE();
    if (!window.jobModel.empty()) {
        MarkRetargetedModelUVE(window.target, true);
    }
    window.lastBackup = result.backupDir;
    std::size_t changed = 0U;
    for (const Retarget::RetargetFileReportUVE& file : result.files) {
        changed += file.changed ? 1U : 0U;
    }
    char text[192];
    std::snprintf(text, sizeof(text), "Conformed %zu of %zu files, %zu skeleton%s in the scene updated.", changed, result.files.size(),
                  skeletons, skeletons == 1U ? "" : "s");
    window.status = text;
    window.planStale = true;
}

void EditorUVE::TickRetargetWindowUVE() {
    if (!m_retargetWindow.has_value()) {
        return;
    }
    RetargetWindowStateUVE& window = *m_retargetWindow;
    // A finished job hands its result back here, on the editor's thread.
    if (window.job.valid() && window.job.wait_for(std::chrono::seconds{0}) == std::future_status::ready) {
        FinishRetargetJobUVE(window, window.job.get());
    }
    const std::vector<std::filesystem::path> candidates = RetargetCandidatesUVE();
    if (window.target.empty() && !candidates.empty() && window.animations.empty()) {
        window.target = candidates.front();
        window.planStale = true;
    }
    // The Rig Brain finished training: plan again with it.
    if (!window.plan.usedBrain && window.plan.modelReadable && Retarget::IsDefaultRigBrainReadyUVE()) {
        window.planStale = true;
    }
    if (window.planStale && !window.job.valid()) {
        const std::filesystem::path modelFile = ResolveRetargetModelFileUVE(window.target);
        window.plan = PlanRetargetUVE(modelFile, window.animations);
        window.planStale = false;
        ++window.planSerial;
        // The picture follows the plan: a new character, or files that just changed.
        RebuildRetargetPreviewUVE(window.plan, modelFile);
    }
}

std::vector<std::filesystem::path> EditorUVE::RetargetCandidatesUVE() const {
    std::vector<std::filesystem::path> candidates;
    for (const auto& [path, info] : m_modelSources) {
        if (info.rigged) {
            candidates.emplace_back(path);
        }
    }
    const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    for (const Asset::ProjectFileEntryUVE& entry : project.entries) {
        if (entry.kind != Asset::ProjectFileEntryKindUVE::Directory && LowerUVE(entry.relativePath.extension().string()) == ".uvmodel") {
            candidates.push_back(entry.relativePath);
        }
    }
    std::ranges::sort(candidates);
    return candidates;
}

bool EditorUVE::StartRetargetUVE() {
    if (!m_retargetWindow.has_value()) {
        return false;
    }
    RetargetWindowStateUVE& window = *m_retargetWindow;
    const RetargetPlanUVE& plan = window.plan;
    if (window.job.valid() || !plan.CanGenerateUVE() || !IsAuthoringCommandAllowedUVE()) {
        return false;
    }
    const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    Retarget::RetargetFilesRequestUVE request;
    request.model = plan.model;
    request.animations = window.animations;
    request.backupRoot = project.contentRoot.parent_path() / ".retarget-backup";
    window.jobModel = plan.model;
    window.status.clear();
    window.progress->done = 0U;
    window.progress->total = (request.model.empty() ? 0U : 1U) + request.animations.size();
    window.job = std::async(std::launch::async, [request, progress = window.progress]() {
        return Retarget::RetargetFilesUVE(request, Retarget::GetHumanoidReferenceUVE(),
                                          [&progress](const std::size_t done, const std::size_t total, const std::string& what) {
                                              progress->done = done;
                                              progress->total = total;
                                              const std::scoped_lock lock(progress->mutex);
                                              progress->what = what;
                                          });
    });
    return true;
}

bool EditorUVE::UndoLastRetargetUVE() {
    if (!m_retargetWindow.has_value()) {
        return false;
    }
    RetargetWindowStateUVE& window = *m_retargetWindow;
    if (window.lastBackup.empty() || window.job.valid()) {
        return false;
    }
    std::string error;
    if (!Retarget::RestoreRetargetBackupUVE(window.lastBackup, &error)) {
        window.status = error;
        window.statusIsError = true;
        return false;
    }
    Asset::IAssetDatabaseUVE& database = m_services->GetAssetDatabaseUVE();
    const std::filesystem::path modelFile = ResolveRetargetModelFileUVE(window.target);
    std::vector<std::filesystem::path> files;
    if (!modelFile.empty()) {
        files.push_back(modelFile);
    }
    files.insert(files.end(), window.animations.begin(), window.animations.end());
    for (const std::filesystem::path& file : files) {
        if (const Asset::AssetGuidUVE guid = database.RegisterUVE(file); guid != Asset::kInvalidAssetGuidUVE) {
            m_services->GetAssetManagerUVE().ReloadUVE(guid, database);
        }
    }
    const std::size_t skeletons = modelFile.empty() ? 0U : RefreshSkeletonsForRetargetedModelUVE(modelFile);
    MarkRetargetedModelUVE(window.target, false);
    ClearMeshThumbnailCacheUVE();
    window.status = "Put the originals back (" + std::to_string(skeletons) + " skeleton" + (skeletons == 1U ? "" : "s") + " updated).";
    window.statusIsError = false;
    window.lastBackup.clear();
    window.planStale = true;
    return true;
}

} // namespace UVE::Editor
