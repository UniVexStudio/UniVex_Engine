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

constexpr std::size_t kAsyncPlanThreshold = 8U;
constexpr double kFolderImportFrameBudgetSeconds = 0.012;

[[nodiscard]] std::string Lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

[[nodiscard]] std::string NormalizedAnimationKey(const std::filesystem::path& path) {
    std::filesystem::path model;
    std::string take;
    if (Asset::SplitEmbeddedClipPath(path, model, take)) {
        return model.lexically_normal().generic_string() + "#" + take;
    }
    return path.lexically_normal().generic_string();
}

[[nodiscard]] bool IsRelativePathUnderFolder(const std::filesystem::path& relativeEntry,
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

void CancelPendingPlanJob(RetargetWindowState& window) {
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

void Editor::OpenRetargetWindow(std::vector<std::filesystem::path> animations, std::filesystem::path target) {
    if (m_retargetWindow.has_value()) {
        CloseRetargetWindow();
    }
    std::deque<std::filesystem::path> pendingImports;
    const Asset::ProjectFileSnapshot project = m_services->GetProjectFileIndex().GetSnapshot();
    std::unordered_set<std::string> seen;
    seen.reserve(animations.size() + 16U);
    for (const std::filesystem::path& path : animations) {
        seen.insert(NormalizedAnimationKey(path));
    }
    const auto addAnim = [&](const std::filesystem::path& path) {
        if (!path.empty() && seen.insert(NormalizedAnimationKey(path)).second) {
            animations.push_back(path);
        }
    };

    if (!target.empty() && IsModelSourcePath(target)) {
        const std::filesystem::path absSource = project.contentRoot / target;
        const EditorModelSourceInfo* const info = FindModelSourceInfo(target);
        if (animations.empty()) {
            const std::filesystem::path parentDir = absSource.parent_path();
            const std::string stem = absSource.stem().string();
            std::error_code dirError;
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(parentDir, dirError)) {
                if (!entry.is_regular_file()) {
                    continue;
                }
                const std::filesystem::path candidate = entry.path();
                if (Lower(candidate.extension().string()) != ".uvanim") {
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
        } else if (info != nullptr && info->animationCount > 0U && !IsRetargetedModelCurrent(target)) {
            const std::filesystem::path importedModel = GetImportedModelPath(target);
            std::error_code existsErr;
            if (std::filesystem::is_regular_file(importedModel, existsErr) &&
                Asset::ListModelAnimations(importedModel).empty()) {
                pendingImports.push_back(absSource);
            }
        }
    }

    RetargetWindowState window;
    window.autoSelectDefaultCandidate = animations.empty() && target.empty() && pendingImports.empty();
    window.animations = std::move(animations);
    window.target = std::move(target);
    window.pendingImportTotal = pendingImports.size();
    window.pendingFolderImports = std::move(pendingImports);
    m_retargetWindow = std::move(window);
    // The window's own world (the level is not touched), and the Rig Brain trained meanwhile.
    static_cast<void>(BeginRetargetPreview());
    Retarget::WarmDefaultRigBrain();
}

void Editor::OpenRetargetWindowForPaths(const std::vector<std::filesystem::path>& relativePaths) {
    if (m_retargetWindow.has_value()) {
        CloseRetargetWindow();
    }
    const Asset::ProjectFileSnapshot project = m_services->GetProjectFileIndex().GetSnapshot();

    // Index existing .uvanim files by directory once (O(N)) so checking whether an animation-only
    // FBX already has extracted .uvanim clips never rescans the filesystem per file.
    std::unordered_map<std::string, std::vector<std::filesystem::path>> dirUvanims;
    std::unordered_set<std::string> directoryEntries;
    for (const Asset::ProjectFileEntry& entry : project.entries) {
        if (entry.kind == Asset::ProjectFileEntryKind::Directory) {
            directoryEntries.insert(entry.relativePath.generic_string());
        } else if (Lower(entry.relativePath.extension().string()) == ".uvanim") {
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
        if (!path.empty() && seenAnimations.insert(NormalizedAnimationKey(path)).second) {
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
        const std::string ext = Lower(relPath.extension().string());
        const std::filesystem::path absPath = project.contentRoot / relPath;
        if (ext == ".uvanim") {
            addAnimation(absPath);
            return;
        }
        if (ext == ".uvmodel") {
            addCharacter(relPath);
            for (const Asset::ModelAnimationEntry& take : Asset::ListModelAnimations(absPath)) {
                addAnimation(Asset::MakeEmbeddedClipPath(absPath, take.name));
            }
            return;
        }
        if (!IsModelSourcePath(relPath)) {
            return;
        }
        const EditorModelSourceInfo* const info = FindModelSourceInfo(relPath);
        const bool riggedMesh = info != nullptr && info->rigged && !info->animationOnly;
        if (riggedMesh) {
            addCharacter(relPath);
            const std::filesystem::path importedModel = GetImportedModelPath(relPath);
            std::error_code ec;
            if (std::filesystem::is_regular_file(importedModel, ec)) {
                const std::vector<Asset::ModelAnimationEntry> takes = Asset::ListModelAnimations(importedModel);
                for (const Asset::ModelAnimationEntry& take : takes) {
                    addAnimation(Asset::MakeEmbeddedClipPath(importedModel, take.name));
                }
                if (takes.empty() && info->animationCount > 0U && !IsRetargetedModelCurrent(relPath)) {
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
            for (const Asset::ProjectFileEntry& entry : project.entries) {
                if (entry.kind == Asset::ProjectFileEntryKind::Directory) {
                    continue;
                }
                if (IsRelativePathUnderFolder(entry.relativePath, cleanRel)) {
                    processFile(entry.relativePath);
                }
            }
        } else {
            processFile(cleanRel);
        }
    }

    std::ranges::sort(animations);
    RetargetWindowState window;
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
    static_cast<void>(BeginRetargetPreview());
    Retarget::WarmDefaultRigBrain();
}

void Editor::OpenRetargetWindowForFolder(const std::filesystem::path& relativeFolder) {
    OpenRetargetWindowForPaths({relativeFolder});
}

void Editor::CloseRetargetWindow() {
    if (m_retargetWindow.has_value()) {
        CancelPendingPlanJob(*m_retargetWindow);
        if (m_retargetWindow->job.valid()) {
            m_retargetWindow->job.wait(); // never leave files half written
            static_cast<void>(m_retargetWindow->job.get());
        }
    }
    if (!EndRetargetPreview()) {
        return; // the scene did not come back: the window stays, so nothing is lost
    }
    if (m_retargetWindow.has_value()) {
        // The scene was put aside while files changed: bring its skeletons up to date.
        for (const std::filesystem::path& model : m_retargetWindow->changedModels) {
            if (!model.empty()) {
                static_cast<void>(RefreshSkeletonsForRetargetedModel(model));
            }
        }
    }
    m_retargetWindow.reset();
}

std::filesystem::path Editor::ResolveRetargetModelFile(const std::filesystem::path& target) const {
    if (target.empty()) {
        return {};
    }
    const Asset::ProjectFileSnapshot project = m_services->GetProjectFileIndex().GetSnapshot();
    if (Lower(target.extension().string()) == ".uvmodel") {
        return target.is_absolute() ? target.lexically_normal() : (project.contentRoot / target).lexically_normal();
    }
    if (const EditorModelSourceInfo* const info = FindModelSourceInfo(target);
        info != nullptr && info->animationOnly) {
        return {};
    }
    return GetImportedModelPath(target);
}

std::size_t Editor::RefreshSkeletonsForRetargetedModel(const std::filesystem::path& modelFile) {
    if (modelFile.empty()) {
        return 0U;
    }
    const Asset::ProjectFileSnapshot project = m_services->GetProjectFileIndex().GetSnapshot();
    const std::filesystem::path wanted = modelFile.lexically_normal();
    std::error_code canonErr;
    const std::filesystem::path wantedCanon = std::filesystem::weakly_canonical(wanted, canonErr);
    const std::string wantedKey = canonErr ? wanted.generic_string() : wantedCanon.generic_string();

    const auto matchesWanted = [&](const std::filesystem::path& source) -> bool {
        if (source.empty()) {
            return false;
        }
        std::filesystem::path resolved;
        if (IsModelSourcePath(source)) {
            resolved = GetImportedModelPath(source);
        } else if (Lower(source.extension().string()) == ".uvmodel") {
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

    const std::optional<Asset::GltfSkeleton> freshBones =
        ReadSkeletonForModelFile(wanted, Scene::kMaximumSkeletonBones);
    if (!freshBones.has_value()) {
        return 0U;
    }

    const auto refreshCurrentDocument = [&]() -> std::size_t {
        std::size_t changed = 0U;
        Scene::IEntityManager& entityManager = m_services->GetEntityManager();
        entityManager.ForEach<Scene::Skeleton3DComponent>(
            [&](const Scene::Entity, Scene::Skeleton3DComponent& skeleton) {
                const std::filesystem::path source{skeleton.skeletonAssetPath};
                if (!matchesWanted(source)) {
                    return;
                }
                skeleton.bones.clear();
                skeleton.bones.reserve(freshBones->joints.size());
                for (const Asset::GltfJoint& joint : freshBones->joints) {
                    skeleton.bones.push_back(
                        Scene::SkeletonBone{joint.name, joint.parentIndex, joint.translation, joint.rotation, joint.scale});
                }
                skeleton.pose.clear();
                ++changed;
            });
        if (changed > 0U) {
            // Re-sync SkinComponent bindings on any meshes in this document bound to the updated rig.
            entityManager.ForEach<Scene::SkinComponent>(
                [&](const Scene::Entity entity, Scene::SkinComponent& skin) {
                    static_cast<void>(AutoPopulateSkinFromModel(entity, skin));
                });
            m_sceneDirty = true;
        }
        return changed;
    };

    std::size_t totalChanged = refreshCurrentDocument();
    if (m_entityEditSession.has_value() && !m_entityDocumentActive) {
        const EntityDocumentScope scope(*this);
        totalChanged += refreshCurrentDocument();
    }
    return totalChanged;
}

void Editor::ReloadRetargetedFiles(const std::vector<std::filesystem::path>& files) {
    // A file can be registered under more than one spelling (relative to the working folder, or
    // absolute), and each spelling is its own GUID with its own loaded copy: a character placed
    // before Generate plays the clip it loaded then. Every loaded GUID whose path is in `files`
    // reloads — checking load state BEFORE canonicalizing avoids thousands of stat() syscalls when
    // retargeting a 1,000-animation folder.
    Asset::IAssetDatabase& database = m_services->GetAssetDatabase();
    Asset::IAssetManager& assets = m_services->GetAssetManager();
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
        if (Asset::SplitEmbeddedClipPath(path, model, take)) {
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
        const std::string ext = Lower(file.extension().string());
        if (ext == ".uvmodel" || ext == ".uvskel") {
            static_cast<void>(database.Register(file));
        }
    }
    if (wanted.empty()) {
        return;
    }
    for (const Asset::AssetRecord& record : database.GetRegisteredAssets()) {
        if (record.guid == Asset::kInvalidAssetGuid ||
            assets.GetLoadState(record.guid) == Asset::AssetLoadState::NotLoaded) {
            continue;
        }
        if (wanted.contains(identity(record.path))) {
            assets.Reload(record.guid, database);
        }
    }
}

void Editor::FinishRetargetJob(RetargetWindowState& window, Retarget::RetargetFilesResult result) {
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
    for (const Retarget::RetargetFileReport& file : result.files) {
        if (file.changed) {
            changedFiles.push_back(file.file);
            ++changed;
        }
    }
    ReloadRetargetedFiles(changedFiles);
    const std::size_t skeletons =
        window.jobConformedModel && !window.jobModel.empty() ? RefreshSkeletonsForRetargetedModel(window.jobModel) : 0U;
    if (window.jobConformedModel && !window.jobModel.empty()) {
        window.changedModels.push_back(window.jobModel);
        MarkRetargetedModel(window.target, true);
    }
    ClearMeshThumbnailCache();
    // The preview's world drew the files as they were (its own renderer caches them): a new one.
    static_cast<void>(EndRetargetPreview());
    static_cast<void>(BeginRetargetPreview());
    window.lastBackup = result.backupDir;
    char text[192];
    std::snprintf(text, sizeof(text), "Conformed %zu of %zu files, %zu skeleton%s in the scene updated.", changed, result.files.size(),
                  skeletons, skeletons == 1U ? "" : "s");
    window.status = text;
    window.planStale = true;
}

void Editor::TickRetargetWindow() {
    if (!m_retargetWindow.has_value()) {
        return;
    }
    RetargetWindowState& window = *m_retargetWindow;
    // A finished Generate job hands its result back here, on the editor's thread.
    if (window.job.valid() && window.job.wait_for(std::chrono::seconds{0}) == std::future_status::ready) {
        FinishRetargetJob(window, window.job.get());
    }
    // A finished background plan scan publishes the new plan and refreshes the preview.
    if (window.planJob.valid() && window.planJob.wait_for(std::chrono::seconds{0}) == std::future_status::ready) {
        RetargetPlan nextPlan = window.planJob.get();
        if (!window.planCancel || !window.planCancel->load(std::memory_order_relaxed)) {
            nextPlan.alreadyRetargeted = !window.target.empty() && IsRetargetedModelCurrent(window.target);
            window.plan = std::move(nextPlan);
            ++window.planSerial;
            RebuildRetargetPreview(window.plan, window.planModelFile);
        }
    }
    // Drain un-imported folder animation sources incrementally within a per-frame budget so
    // right-clicking a 1,000-FBX folder never stalls the UI thread.
    if (!window.pendingFolderImports.empty() && !window.job.valid()) {
        const auto began = std::chrono::steady_clock::now();
        std::unordered_set<std::string> seen;
        seen.reserve(window.animations.size() + 16U);
        for (const std::filesystem::path& existing : window.animations) {
            seen.insert(NormalizedAnimationKey(existing));
        }
        bool addedAny = false;
        while (!window.pendingFolderImports.empty()) {
            const std::filesystem::path absSource = std::move(window.pendingFolderImports.front());
            window.pendingFolderImports.pop_front();
            const std::vector<std::filesystem::path> imported = ImportModelAnimations(absSource);
            for (const std::filesystem::path& clipPath : imported) {
                if (seen.insert(NormalizedAnimationKey(clipPath)).second) {
                    window.animations.push_back(clipPath);
                    addedAny = true;
                }
            }
            ++window.pendingImportDone;
            if (m_prismUiRequested &&
                std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count() >=
                    kFolderImportFrameBudgetSeconds) {
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
    const std::vector<std::filesystem::path> candidates = RetargetCandidates();
    if (window.autoSelectDefaultCandidate && window.target.empty() && !candidates.empty() && window.animations.empty()) {
        window.target = candidates.front();
        window.planStale = true;
    }
    // The Rig Brain finished training: plan again with it.
    if (!window.plan.usedBrain && window.plan.modelReadable && Retarget::IsDefaultRigBrainReady() &&
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
        const std::filesystem::path modelFile = ResolveRetargetModelFile(window.target);
        window.planStale = false;
        window.planModelFile = modelFile;
        if (!m_prismUiRequested || window.animations.size() <= kAsyncPlanThreshold) {
            window.plan = PlanRetarget(modelFile, window.animations);
            window.plan.alreadyRetargeted = !window.target.empty() && IsRetargetedModelCurrent(window.target);
            ++window.planSerial;
            // The picture follows the plan: a new character, or files that just changed.
            RebuildRetargetPreview(window.plan, modelFile);
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
                    return PlanRetarget(
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

std::vector<std::filesystem::path> Editor::RetargetCandidates() const {
    std::vector<std::filesystem::path> candidates;
    for (const auto& [path, info] : m_modelSources) {
        if (info.rigged) {
            candidates.emplace_back(path);
        }
    }
    const Asset::ProjectFileSnapshot project = m_services->GetProjectFileIndex().GetSnapshot();
    for (const Asset::ProjectFileEntry& entry : project.entries) {
        if (entry.kind != Asset::ProjectFileEntryKind::Directory && Lower(entry.relativePath.extension().string()) == ".uvmodel") {
            candidates.push_back(entry.relativePath);
        }
    }
    std::ranges::sort(candidates);
    return candidates;
}

bool Editor::StartRetarget() {
    if (!m_retargetWindow.has_value()) {
        return false;
    }
    RetargetWindowState& window = *m_retargetWindow;
    const RetargetPlan& plan = window.plan;
    if (window.job.valid() || window.planJob.valid() || !window.pendingFolderImports.empty() ||
        !plan.CanGenerate() || !IsAuthoringCommandAllowed()) {
        return false;
    }
    const Asset::ProjectFileSnapshot project = m_services->GetProjectFileIndex().GetSnapshot();
    Retarget::RetargetFilesRequest request;
    // A character done already is left alone: only the animations (including embedded takes!) are conformed.
    request.model = plan.CharacterDone() ? std::filesystem::path{} : plan.model;
    std::unordered_set<std::string> seen;
    seen.reserve(plan.animations.size() + window.animations.size() + 8U);
    const auto pushAnim = [&](const std::filesystem::path& path) {
        if (!path.empty() && seen.insert(NormalizedAnimationKey(path)).second) {
            request.animations.push_back(path);
        }
    };
    // Plan animations already include both explicit window.animations and all embedded takes of plan.model.
    for (const RetargetAnimationRow& row : plan.animations) {
        pushAnim(row.file);
    }
    for (const std::filesystem::path& path : window.animations) {
        pushAnim(path);
    }
    if (!plan.model.empty()) {
        for (const Asset::ModelAnimationEntry& take : Asset::ListModelAnimations(plan.model)) {
            pushAnim(Asset::MakeEmbeddedClipPath(plan.model, take.name));
        }
    }
    request.backupRoot = project.contentRoot.parent_path() / ".retarget-backup";
    window.jobModel = plan.model;
    window.jobConformedModel = !request.model.empty();
    window.status.clear();
    window.progress->done = 0U;
    window.progress->total = (request.model.empty() ? 0U : 1U) + request.animations.size();
    window.job = std::async(std::launch::async, [request, progress = window.progress]() {
        return Retarget::RetargetFiles(request, Retarget::GetHumanoidReference(),
                                          [&progress](const std::size_t done, const std::size_t total, const std::string& what) {
                                              progress->done = done;
                                              progress->total = total;
                                              const std::scoped_lock lock(progress->mutex);
                                              progress->what = what;
                                          });
    });
    return true;
}

bool Editor::UndoLastRetarget() {
    if (!m_retargetWindow.has_value()) {
        return false;
    }
    RetargetWindowState& window = *m_retargetWindow;
    if (window.lastBackup.empty() || window.job.valid() || window.planJob.valid()) {
        return false;
    }
    std::string error;
    if (!Retarget::RestoreRetargetBackup(window.lastBackup, &error)) {
        window.status = error;
        window.statusIsError = true;
        return false;
    }
    const std::filesystem::path modelFile = ResolveRetargetModelFile(window.target);
    std::vector<std::filesystem::path> files;
    if (!modelFile.empty()) {
        // The character and everything published beside it (parts, skeleton, embedded takes) came back.
        files.push_back(modelFile);
        std::error_code dirErr;
        const std::string prefix = modelFile.stem().string() + ".";
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(modelFile.parent_path(), dirErr)) {
            const std::string name = entry.path().filename().string();
            const std::string extension = Lower(entry.path().extension().string());
            if (entry.path() != modelFile && name.starts_with(prefix) && (extension == ".uvmodel" || extension == ".uvskel")) {
                files.push_back(entry.path());
            }
        }
        for (const Asset::ModelAnimationEntry& take : Asset::ListModelAnimations(modelFile)) {
            files.push_back(Asset::MakeEmbeddedClipPath(modelFile, take.name));
        }
    }
    files.insert(files.end(), window.animations.begin(), window.animations.end());
    for (const RetargetAnimationRow& row : window.plan.animations) {
        files.push_back(row.file);
    }
    ReloadRetargetedFiles(files);
    const std::size_t skeletons =
        window.jobConformedModel && !modelFile.empty() ? RefreshSkeletonsForRetargetedModel(modelFile) : 0U;
    if (window.jobConformedModel && !window.target.empty()) {
        MarkRetargetedModel(window.target, false);
    }
    ClearMeshThumbnailCache();
    static_cast<void>(EndRetargetPreview());
    static_cast<void>(BeginRetargetPreview());
    window.status = "Put the originals back (" + std::to_string(skeletons) + " skeleton" + (skeletons == 1U ? "" : "s") + " updated).";
    window.statusIsError = false;
    window.lastBackup.clear();
    window.planStale = true;
    return true;
}

} // namespace UVE::Editor
