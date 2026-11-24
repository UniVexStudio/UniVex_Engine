// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Retarget window: pick a character (or a folder / multi-selection of animations), see how
// its bones answer the humanoid (green: found and fine, yellow: found but look, red: broken, grey:
// missing, conforming adds it), then Generate. Generate conforms the character and the animations
// in place, keeping a backup for Undo.

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "editor.h"
#include "engine_services.h"
#include "fbx_mesh_converter.h"
#include "i_asset_database.h"
#include "i_asset_manager.h"
#include "i_entity_manager.h"
#include "i_project_file_index.h"
#include "mesh_asset.h"
#include "model_animations.h"
#include "retarget_brain.h"
#include "retarget_conform.h"
#include "retarget_files.h"
#include "retarget_humanoid.h"
#include "skeleton_3d.h"

namespace UVE::Editor {
namespace {

constexpr std::size_t kAsyncPlanThresholdUVE = 8U;
constexpr double kFolderImportFrameBudgetSecondsUVE = 0.012;

[[nodiscard]] std::string LowerUVE(std::string text) {
    std::ranges::transform(text, text.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

[[nodiscard]] std::string NormalizedAnimationKeyUVE(const std::filesystem::path& path) {
    std::filesystem::path model;
    std::string take;
    if (Asset::SplitEmbeddedClipPathUVE(path, model, take)) {
        return model.lexically_normal().generic_string() + "#" + take;
    }
    return path.lexically_normal().generic_string();
}

[[nodiscard]] bool IsRelativePathUnderFolderUVE(const std::filesystem::path& relativeEntry,
                                                const std::filesystem::path& relativeFolder) {
    if (relativeFolder.empty()) {
        return true;
    }
    const std::string entryStr = relativeEntry.generic_string();
    const std::string folderStr = relativeFolder.generic_string();
    return entryStr == folderStr ||
           (entryStr.size() > folderStr.size() && entryStr.compare(0U, folderStr.size(), folderStr) == 0 &&
            entryStr[folderStr.size()] == '/');
}

void CancelPendingPlanJobUVE(RetargetWindowStateUVE& window) {
    if (window.planCancel) {
        window.planCancel->store(true, std::memory_order_relaxed);
    }
    if (window.planJob.valid()) {
        window.planJob.wait();
        static_cast<void>(window.planJob.get());
    }
    window.planCancel = std::make_shared<std::atomic<bool>>(false);
}

} // namespace

void EditorUVE::OpenRetargetWindowUVE(std::vector<std::filesystem::path> animations, std::filesystem::path target) {
    if (m_retargetWindow.has_value()) {
        CloseRetargetWindowUVE();
    }
    std::deque<std::filesystem::path> pendingImports;
    const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    std::unordered_set<std::string> seen;
    seen.reserve(animations.size() + 16U);
    for (const std::filesystem::path& path : animations) {
        seen.insert(NormalizedAnimationKeyUVE(path));
    }
    const auto addAnim = [&](const std::filesystem::path& path) {
        if (!path.empty() && seen.insert(NormalizedAnimationKeyUVE(path)).second) {
            animations.push_back(path);
        }
    };

    if (!target.empty() && IsModelSourcePathUVE(target)) {
        const std::filesystem::path absSource = project.contentRoot / target;
        const EditorModelSourceInfoUVE* const info = FindModelSourceInfoUVE(target);
        if (animations.empty()) {
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
                    addAnim(candidate);
                }
            }
        }
        // Ensure embedded takes (or animation-only clips) of the target source are imported before planning.
        const bool isAnimOnly = info != nullptr && info->animationOnly;
        if (isAnimOnly) {
            if (animations.empty() && info->animationCount > 0U) {
                pendingImports.push_back(absSource);
            }
            target.clear();
        } else if (info != nullptr && info->animationCount > 0U && !IsRetargetedModelCurrentUVE(target)) {
            const std::filesystem::path importedModel = GetImportedModelPathUVE(target);
            std::error_code existsErr;
            if (std::filesystem::is_regular_file(importedModel, existsErr) &&
                Asset::ListModelAnimationsUVE(importedModel).empty()) {
                pendingImports.push_back(absSource);
            }
        }
    }

    RetargetWindowStateUVE window;
    window.autoSelectDefaultCandidate = animations.empty() && target.empty() && pendingImports.empty();
    window.animations = std::move(animations);
    window.target = std::move(target);
    window.pendingImportTotal = pendingImports.size();
    window.pendingFolderImports = std::move(pendingImports);
    m_retargetWindow = std::move(window);
    // The window's own world (the level is not touched), and the Rig Brain trained meanwhile.
    static_cast<void>(BeginRetargetPreviewUVE());
    Retarget::WarmDefaultRigBrainUVE();
}

void EditorUVE::OpenRetargetWindowForPathsUVE(const std::vector<std::filesystem::path>& relativePaths) {
    if (m_retargetWindow.has_value()) {
        CloseRetargetWindowUVE();
    }
    const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();

    // Index existing .uvanim files by directory once (O(N)) so checking whether an animation-only
    // FBX already has extracted .uvanim clips never rescans the filesystem per file.
    std::unordered_map<std::string, std::vector<std::filesystem::path>> dirUvanims;
    std::unordered_set<std::string> directoryEntries;
    for (const Asset::ProjectFileEntryUVE& entry : project.entries) {
        if (entry.kind == Asset::ProjectFileEntryKindUVE::Directory) {
            directoryEntries.insert(entry.relativePath.generic_string());
        } else if (LowerUVE(entry.relativePath.extension().string()) == ".uvanim") {
            dirUvanims[entry.relativePath.parent_path().generic_string()].push_back(project.contentRoot / entry.relativePath);
        }
    }

    std::vector<std::filesystem::path> animations;
    std::unordered_set<std::string> seenAnimations;
    std::deque<std::filesystem::path> pendingImports;
    std::unordered_set<std::string> seenImports;
    std::vector<std::filesystem::path> characterCandidates;
    std::unordered_set<std::string> seenCharacters;

    const auto addAnimation = [&](const std::filesystem::path& path) {
        if (!path.empty() && seenAnimations.insert(NormalizedAnimationKeyUVE(path)).second) {
            animations.push_back(path);
        }
    };
    const auto queueImport = [&](const std::filesystem::path& absSource) {
        if (!absSource.empty() && seenImports.insert(absSource.lexically_normal().generic_string()).second) {
            pendingImports.push_back(absSource);
        }
    };
    const auto addCharacter = [&](const std::filesystem::path& relPath) {
        if (!relPath.empty() && seenCharacters.insert(relPath.generic_string()).second) {
            characterCandidates.push_back(relPath);
        }
    };
    const auto collectLooseForSource = [&](const std::filesystem::path& relSource) -> std::size_t {
        std::size_t found = 0U;
        const std::string dirKey = relSource.parent_path().generic_string();
        const auto it = dirUvanims.find(dirKey);
        if (it == dirUvanims.end()) {
            return 0U;
        }
        const std::string stem = relSource.stem().string();
        for (const std::filesystem::path& candidate : it->second) {
            const std::string candidateStem = candidate.stem().string();
            if (candidateStem == stem || candidateStem.rfind(stem + "_", 0U) == 0U) {
                addAnimation(candidate);
                ++found;
            }
        }
        return found;
    };

    const auto processFile = [&](const std::filesystem::path& relPath) {
        const std::string ext = LowerUVE(relPath.extension().string());
        const std::filesystem::path absPath = project.contentRoot / relPath;
        if (ext == ".uvanim") {
            addAnimation(absPath);
            return;
        }
        if (ext == ".uvmodel") {
            addCharacter(relPath);
            for (const Asset::ModelAnimationEntryUVE& take : Asset::ListModelAnimationsUVE(absPath)) {
                addAnimation(Asset::MakeEmbeddedClipPathUVE(absPath, take.name));
            }
            return;
        }
        if (!IsModelSourcePathUVE(relPath)) {
            return;
        }
        const EditorModelSourceInfoUVE* const info = FindModelSourceInfoUVE(relPath);
        const bool riggedMesh = info != nullptr && info->rigged && !info->animationOnly;
        if (riggedMesh) {
            addCharacter(relPath);
            const std::filesystem::path importedModel = GetImportedModelPathUVE(relPath);
            std::error_code ec;
            if (std::filesystem::is_regular_file(importedModel, ec)) {
                const std::vector<Asset::ModelAnimationEntryUVE> takes = Asset::ListModelAnimationsUVE(importedModel);
                for (const Asset::ModelAnimationEntryUVE& take : takes) {
                    addAnimation(Asset::MakeEmbeddedClipPathUVE(importedModel, take.name));
                }
                if (takes.empty() && info->animationCount > 0U && !IsRetargetedModelCurrentUVE(relPath)) {
                    queueImport(absPath);
                }
            }
            static_cast<void>(collectLooseForSource(relPath));
            return;
        }
        // Animation-only model source (or unknown/not-yet-indexed .fbx/.glb): collect its .uvanim clips,
        // or queue it for frame-budgeted extraction if none exist on disk yet.
        const std::size_t existingLoose = collectLooseForSource(relPath);
        if (existingLoose == 0U && (info == nullptr || info->animationCount > 0U || info->animationOnly || info->hasSkeleton)) {
            queueImport(absPath);
        }
    };

    for (const std::filesystem::path& rawRel : relativePaths) {
        const std::filesystem::path rel = rawRel.lexically_normal();
        const std::filesystem::path cleanRel = (rel == ".") ? std::filesystem::path{} : rel;
        std::error_code ec;
        const bool isDir = cleanRel.empty() || directoryEntries.contains(cleanRel.generic_string()) ||
                           std::filesystem::is_directory(project.contentRoot / cleanRel, ec);
        if (isDir) {
            for (const Asset::ProjectFileEntryUVE& entry : project.entries) {
                if (entry.kind == Asset::ProjectFileEntryKindUVE::Directory) {
                    continue;
                }
                if (IsRelativePathUnderFolderUVE(entry.relativePath, cleanRel)) {
                    processFile(entry.relativePath);
                }
            }
        } else {
            processFile(cleanRel);
        }
    }

    std::ranges::sort(animations);
    RetargetWindowStateUVE window;
    // Never auto-pick an unrelated character when opening an explicit selection or animation folder.
    window.autoSelectDefaultCandidate = false;
    window.animations = std::move(animations);
    // Auto-select the character only when the user's selection / folder uniquely names one character.
    if (characterCandidates.size() == 1U) {
        window.target = characterCandidates.front();
    }
    window.pendingImportTotal = pendingImports.size();
    window.pendingFolderImports = std::move(pendingImports);
    m_retargetWindow = std::move(window);
    static_cast<void>(BeginRetargetPreviewUVE());
    Retarget::WarmDefaultRigBrainUVE();
}

void EditorUVE::OpenRetargetWindowForFolderUVE(const std::filesystem::path& relativeFolder) {
    OpenRetargetWindowForPathsUVE({relativeFolder});
}

void EditorUVE::CloseRetargetWindowUVE() {
    if (m_retargetWindow.has_value()) {
        CancelPendingPlanJobUVE(*m_retargetWindow);
        if (m_retargetWindow->job.valid()) {
            m_retargetWindow->job.wait(); // never leave files half written
            static_cast<void>(m_retargetWindow->job.get());
        }
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
        return target.is_absolute() ? target.lexically_normal() : (project.contentRoot / target).lexically_normal();
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
    const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    const std::filesystem::path wanted = modelFile.lexically_normal();
    std::error_code canonErr;
    const std::filesystem::path wantedCanon = std::filesystem::weakly_canonical(wanted, canonErr);
    const std::string wantedKey = canonErr ? wanted.generic_string() : wantedCanon.generic_string();

    const auto matchesWanted = [&](const std::filesystem::path& source) -> bool {
        if (source.empty()) {
            return false;
        }
        std::filesystem::path resolved;
        if (IsModelSourcePathUVE(source)) {
            resolved = GetImportedModelPathUVE(source);
        } else if (LowerUVE(source.extension().string()) == ".uvmodel") {
            resolved = source.is_absolute() ? source.lexically_normal() : (project.contentRoot / source).lexically_normal();
        } else {
            return false;
        }
        if (resolved == wanted) {
            return true;
        }
        std::error_code err;
        const std::filesystem::path rc = std::filesystem::weakly_canonical(resolved, err);
        return !err && rc.generic_string() == wantedKey;
    };

    const std::optional<Asset::GltfSkeletonUVE> freshBones =
        ReadSkeletonForModelFileUVE(wanted, Scene::kMaximumSkeletonBonesUVE);
    if (!freshBones.has_value()) {
        return 0U;
    }

    const auto refreshCurrentDocument = [&]() -> std::size_t {
        std::size_t changed = 0U;
        Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
        entityManager.ForEachUVE<Scene::Skeleton3DComponentUVE>(
            [&](const Scene::EntityUVE, Scene::Skeleton3DComponentUVE& skeleton) {
                const std::filesystem::path source{skeleton.skeletonAssetPath};
                if (!matchesWanted(source)) {
                    return;
                }
                skeleton.bones.clear();
                skeleton.bones.reserve(freshBones->joints.size());
                for (const Asset::GltfJointUVE& joint : freshBones->joints) {
                    skeleton.bones.push_back(
                        Scene::SkeletonBoneUVE{joint.name, joint.parentIndex, joint.translation, joint.rotation, joint.scale});
                }
                skeleton.pose.clear();
                ++changed;
            });
        if (changed > 0U) {
            // Re-sync SkinComponentUVE bindings on any meshes in this document bound to the updated rig.
            entityManager.ForEachUVE<Scene::SkinComponentUVE>(
                [&](const Scene::EntityUVE entity, Scene::SkinComponentUVE& skin) {
                    static_cast<void>(AutoPopulateSkinFromModelUVE(entity, skin));
                });
            m_sceneDirty = true;
        }
        return changed;
    };

    std::size_t totalChanged = refreshCurrentDocument();
    if (m_entityEditSession.has_value() && !m_entityDocumentActive) {
        const EntityDocumentScopeUVE scope(*this);
        totalChanged += refreshCurrentDocument();
    }
    return totalChanged;
}

void EditorUVE::ReloadRetargetedFilesUVE(const std::vector<std::filesystem::path>& files) {
    // A file can be registered under more than one spelling (relative to the working folder, or
    // absolute), and each spelling is its own GUID with its own loaded copy: a character placed
    // before Generate plays the clip it loaded then. Every loaded GUID whose path is in `files`
    // reloads — checking load state BEFORE canonicalizing avoids thousands of stat() syscalls when
    // retargeting a 1,000-animation folder.
    Asset::IAssetDatabaseUVE& database = m_services->GetAssetDatabaseUVE();
    Asset::IAssetManagerUVE& assets = m_services->GetAssetManagerUVE();
    std::unordered_map<std::string, std::string> canonicalCache;
    const auto canonicalModel = [&canonicalCache](const std::filesystem::path& path) -> std::string {
        const std::string raw = path.lexically_normal().generic_string();
        if (const auto it = canonicalCache.find(raw); it != canonicalCache.end()) {
            return it->second;
        }
        std::error_code error;
        const std::filesystem::path absolute = std::filesystem::weakly_canonical(std::filesystem::absolute(path, error), error);
        std::string resolved = error ? raw : absolute.generic_string();
        canonicalCache.emplace(raw, resolved);
        return resolved;
    };
    const auto identity = [&canonicalModel](const std::filesystem::path& path) -> std::string {
        std::filesystem::path model;
        std::string take;
        if (Asset::SplitEmbeddedClipPathUVE(path, model, take)) {
            return canonicalModel(model) + "#" + take;
        }
        return canonicalModel(path);
    };
    std::unordered_set<std::string> wanted;
    wanted.reserve(files.size() + 8U);
    for (const std::filesystem::path& file : files) {
        if (file.empty()) {
            continue;
        }
        wanted.insert(identity(file));
        const std::string ext = LowerUVE(file.extension().string());
        if (ext == ".uvmodel" || ext == ".uvskel") {
            static_cast<void>(database.RegisterUVE(file));
        }
    }
    if (wanted.empty()) {
        return;
    }
    for (const Asset::AssetRecordUVE& record : database.GetRegisteredAssetsUVE()) {
        if (record.guid == Asset::kInvalidAssetGuidUVE ||
            assets.GetLoadStateUVE(record.guid) == Asset::AssetLoadStateUVE::NotLoaded) {
            continue;
        }
        if (wanted.contains(identity(record.path))) {
            assets.ReloadUVE(record.guid, database);
        }
    }
}

void EditorUVE::FinishRetargetJobUVE(RetargetWindowStateUVE& window, Retarget::RetargetFilesResultUVE result) {
    window.statusIsError = !result.ok;
    if (!result.ok) {
        window.status = result.error;
        window.planStale = true;
        return;
    }
    // The renderer and the animation players may hold the old files.
    std::vector<std::filesystem::path> changedFiles;
    changedFiles.reserve(result.files.size());
    std::size_t changed = 0U;
    for (const Retarget::RetargetFileReportUVE& file : result.files) {
        if (file.changed) {
            changedFiles.push_back(file.file);
            ++changed;
        }
    }
    ReloadRetargetedFilesUVE(changedFiles);
    const std::size_t skeletons =
        window.jobConformedModel && !window.jobModel.empty() ? RefreshSkeletonsForRetargetedModelUVE(window.jobModel) : 0U;
    if (window.jobConformedModel && !window.jobModel.empty()) {
        window.changedModels.push_back(window.jobModel);
        MarkRetargetedModelUVE(window.target, true);
    }
    ClearMeshThumbnailCacheUVE();
    // The preview's world drew the files as they were (its own renderer caches them): a new one.
    static_cast<void>(EndRetargetPreviewUVE());
    static_cast<void>(BeginRetargetPreviewUVE());
    window.lastBackup = result.backupDir;
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
    // A finished Generate job hands its result back here, on the editor's thread.
    if (window.job.valid() && window.job.wait_for(std::chrono::seconds{0}) == std::future_status::ready) {
        FinishRetargetJobUVE(window, window.job.get());
    }
    // A finished background plan scan publishes the new plan and refreshes the preview.
    if (window.planJob.valid() && window.planJob.wait_for(std::chrono::seconds{0}) == std::future_status::ready) {
        RetargetPlanUVE nextPlan = window.planJob.get();
        if (!window.planCancel || !window.planCancel->load(std::memory_order_relaxed)) {
            nextPlan.alreadyRetargeted = !window.target.empty() && IsRetargetedModelCurrentUVE(window.target);
            window.plan = std::move(nextPlan);
            ++window.planSerial;
            RebuildRetargetPreviewUVE(window.plan, window.planModelFile);
        }
    }
    // Drain un-imported folder animation sources incrementally within a per-frame budget so
    // right-clicking a 1,000-FBX folder never stalls the UI thread.
    if (!window.pendingFolderImports.empty() && !window.job.valid()) {
        const auto began = std::chrono::steady_clock::now();
        std::unordered_set<std::string> seen;
        seen.reserve(window.animations.size() + 16U);
        for (const std::filesystem::path& existing : window.animations) {
            seen.insert(NormalizedAnimationKeyUVE(existing));
        }
        bool addedAny = false;
        while (!window.pendingFolderImports.empty()) {
            const std::filesystem::path absSource = std::move(window.pendingFolderImports.front());
            window.pendingFolderImports.pop_front();
            const std::vector<std::filesystem::path> imported = ImportModelAnimationsUVE(absSource);
            for (const std::filesystem::path& clipPath : imported) {
                if (seen.insert(NormalizedAnimationKeyUVE(clipPath)).second) {
                    window.animations.push_back(clipPath);
                    addedAny = true;
                }
            }
            ++window.pendingImportDone;
            if (m_prismUiRequested &&
                std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count() >=
                    kFolderImportFrameBudgetSecondsUVE) {
                break;
            }
        }
        if (window.pendingFolderImports.empty()) {
            if (addedAny) {
                std::ranges::sort(window.animations);
            }
            window.pendingImportTotal = 0U;
            window.pendingImportDone = 0U;
            window.planStale = true;
        }
        return;
    }
    const std::vector<std::filesystem::path> candidates = RetargetCandidatesUVE();
    if (window.autoSelectDefaultCandidate && window.target.empty() && !candidates.empty() && window.animations.empty()) {
        window.target = candidates.front();
        window.planStale = true;
    }
    // The Rig Brain finished training: plan again with it.
    if (!window.plan.usedBrain && window.plan.modelReadable && Retarget::IsDefaultRigBrainReadyUVE() &&
        !window.planJob.valid()) {
        window.planStale = true;
    }
    if (window.planStale && !window.job.valid()) {
        if (window.planJob.valid()) {
            // Ask the old scan to stop; start the new scan on the next tick once it finishes.
            if (window.planCancel) {
                window.planCancel->store(true, std::memory_order_relaxed);
            }
            return;
        }
        const std::filesystem::path modelFile = ResolveRetargetModelFileUVE(window.target);
        window.planStale = false;
        window.planModelFile = modelFile;
        if (!m_prismUiRequested || window.animations.size() <= kAsyncPlanThresholdUVE) {
            window.plan = PlanRetargetUVE(modelFile, window.animations);
            window.plan.alreadyRetargeted = !window.target.empty() && IsRetargetedModelCurrentUVE(window.target);
            ++window.planSerial;
            // The picture follows the plan: a new character, or files that just changed.
            RebuildRetargetPreviewUVE(window.plan, modelFile);
        } else {
            window.planCancel = std::make_shared<std::atomic<bool>>(false);
            window.planProgress->done = 0U;
            window.planProgress->total = std::max<std::size_t>(1U, window.animations.size());
            {
                const std::scoped_lock lock(window.planProgress->mutex);
                window.planProgress->what.clear();
            }
            std::vector<std::filesystem::path> animCopy = window.animations;
            window.planJob = std::async(
                std::launch::async,
                [modelFile, animCopy = std::move(animCopy), progress = window.planProgress, cancel = window.planCancel]() {
                    return PlanRetargetUVE(
                        modelFile, animCopy,
                        [&progress](const std::size_t done, const std::size_t total, const std::string& what) {
                            progress->done = done;
                            progress->total = total;
                            const std::scoped_lock lock(progress->mutex);
                            progress->what = what;
                        },
                        cancel.get());
                });
        }
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
    if (window.job.valid() || window.planJob.valid() || !window.pendingFolderImports.empty() ||
        !plan.CanGenerateUVE() || !IsAuthoringCommandAllowedUVE()) {
        return false;
    }
    const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    Retarget::RetargetFilesRequestUVE request;
    // A character done already is left alone: only the animations (including embedded takes!) are conformed.
    request.model = plan.CharacterDoneUVE() ? std::filesystem::path{} : plan.model;
    std::unordered_set<std::string> seen;
    seen.reserve(plan.animations.size() + window.animations.size() + 8U);
    const auto pushAnim = [&](const std::filesystem::path& path) {
        if (!path.empty() && seen.insert(NormalizedAnimationKeyUVE(path)).second) {
            request.animations.push_back(path);
        }
    };
    // Plan animations already include both explicit window.animations and all embedded takes of plan.model.
    for (const RetargetAnimationRowUVE& row : plan.animations) {
        pushAnim(row.file);
    }
    for (const std::filesystem::path& path : window.animations) {
        pushAnim(path);
    }
    if (!plan.model.empty()) {
        for (const Asset::ModelAnimationEntryUVE& take : Asset::ListModelAnimationsUVE(plan.model)) {
            pushAnim(Asset::MakeEmbeddedClipPathUVE(plan.model, take.name));
        }
    }
    request.backupRoot = project.contentRoot.parent_path() / ".retarget-backup";
    window.jobModel = plan.model;
    window.jobConformedModel = !request.model.empty();
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
    if (window.lastBackup.empty() || window.job.valid() || window.planJob.valid()) {
        return false;
    }
    std::string error;
    if (!Retarget::RestoreRetargetBackupUVE(window.lastBackup, &error)) {
        window.status = error;
        window.statusIsError = true;
        return false;
    }
    const std::filesystem::path modelFile = ResolveRetargetModelFileUVE(window.target);
    std::vector<std::filesystem::path> files;
    if (!modelFile.empty()) {
        // The character and everything published beside it (parts, skeleton, embedded takes) came back.
        files.push_back(modelFile);
        std::error_code dirErr;
        const std::string prefix = modelFile.stem().string() + ".";
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(modelFile.parent_path(), dirErr)) {
            const std::string name = entry.path().filename().string();
            const std::string extension = LowerUVE(entry.path().extension().string());
            if (entry.path() != modelFile && name.starts_with(prefix) && (extension == ".uvmodel" || extension == ".uvskel")) {
                files.push_back(entry.path());
            }
        }
        for (const Asset::ModelAnimationEntryUVE& take : Asset::ListModelAnimationsUVE(modelFile)) {
            files.push_back(Asset::MakeEmbeddedClipPathUVE(modelFile, take.name));
        }
    }
    files.insert(files.end(), window.animations.begin(), window.animations.end());
    for (const RetargetAnimationRowUVE& row : window.plan.animations) {
        files.push_back(row.file);
    }
    ReloadRetargetedFilesUVE(files);
    const std::size_t skeletons =
        window.jobConformedModel && !modelFile.empty() ? RefreshSkeletonsForRetargetedModelUVE(modelFile) : 0U;
    if (window.jobConformedModel && !window.target.empty()) {
        MarkRetargetedModelUVE(window.target, false);
    }
    ClearMeshThumbnailCacheUVE();
    static_cast<void>(EndRetargetPreviewUVE());
    static_cast<void>(BeginRetargetPreviewUVE());
    window.status = "Put the originals back (" + std::to_string(skeletons) + " skeleton" + (skeletons == 1U ? "" : "s") + " updated).";
    window.statusIsError = false;
    window.lastBackup.clear();
    window.planStale = true;
    return true;
}

} // namespace UVE::Editor
