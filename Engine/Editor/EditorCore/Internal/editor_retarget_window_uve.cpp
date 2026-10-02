// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Retarget window: pick a character, see how its bones answer the humanoid (green: found and
// fine, yellow: found but look, red: broken, grey: missing, conforming adds it), then Generate.
// Generate conforms the character and the animations in place, keeping a backup for Undo.

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <imgui.h>

#include "uve/asset/i_asset_database_uve.h"
#include "uve/asset/i_asset_manager_uve.h"
#include "uve/asset/i_project_file_index_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/core/engine_services_uve.h"
#include "uve/editor/editor_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"
#include "uve/retarget/retarget_files_uve.h"
#include "uve/retarget/retarget_humanoid_uve.h"

namespace UVE::Editor {
namespace {

// The joint colours come from GetRetargetStatusColourUVE, so the dots here and the bones in the viewport agree.
[[nodiscard]] ImVec4 ColourOfUVE(const Retarget::JointStatusUVE status) {
    const std::array<float, 3> colour = GetRetargetStatusColourUVE(status);
    return ImVec4{colour[0], colour[1], colour[2], 1.0F};
}

const ImVec4 kGoodUVE = ColourOfUVE(Retarget::JointStatusUVE::Good);
const ImVec4 kBrokenUVE = ColourOfUVE(Retarget::JointStatusUVE::Broken);
const ImVec4 kMissingUVE = ColourOfUVE(Retarget::JointStatusUVE::Missing);
constexpr ImVec4 kAccentUVE{0.357F, 0.478F, 0.600F, 1.0F};

void DotUVE(const ImVec4& colour, const float diameter = 9.0F) {
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float lineHeight = ImGui::GetTextLineHeight();
    drawList.AddCircleFilled(ImVec2{at.x + diameter * 0.5F, at.y + lineHeight * 0.5F}, diameter * 0.5F,
                             ImGui::GetColorU32(colour), 12);
    ImGui::Dummy(ImVec2{diameter + 4.0F, lineHeight});
    ImGui::SameLine(0.0F, 2.0F);
}

/// The humanoid's children lists, from its parents: built once.
[[nodiscard]] const std::vector<std::vector<std::int32_t>>& HumanoidChildrenUVE() {
    static const std::vector<std::vector<std::int32_t>> children = [] {
        const Retarget::HumanoidReferenceUVE& reference = Retarget::GetHumanoidReferenceUVE();
        std::vector<std::vector<std::int32_t>> result(reference.skeleton.bones.size());
        for (std::size_t bone = 0U; bone < reference.skeleton.bones.size(); ++bone) {
            if (const std::int32_t parent = reference.skeleton.bones[bone].parent; parent >= 0) {
                result[static_cast<std::size_t>(parent)].push_back(static_cast<std::int32_t>(bone));
            }
        }
        return result;
    }();
    return children;
}

[[nodiscard]] std::string LowerUVE(std::string text) {
    std::ranges::transform(text, text.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// Does this row, or anything under it, pass the filter? Cached per frame by the caller's rows.
[[nodiscard]] bool RowShownUVE(const RetargetPlanUVE& plan, const std::int32_t bone, const std::string& filter,
                               const bool problemsOnly) {
    const RetargetJointRowUVE& row = plan.joints[static_cast<std::size_t>(bone)];
    const Retarget::HumanoidReferenceUVE& reference = Retarget::GetHumanoidReferenceUVE();
    const bool problem = row.status == Retarget::JointStatusUVE::Warning || row.status == Retarget::JointStatusUVE::Broken;
    const bool matchesText =
        filter.empty() || LowerUVE(reference.skeleton.bones[static_cast<std::size_t>(bone)].name).find(filter) != std::string::npos ||
        LowerUVE(row.characterName).find(filter) != std::string::npos;
    if (matchesText && (!problemsOnly || problem)) {
        return true;
    }
    for (const std::int32_t child : HumanoidChildrenUVE()[static_cast<std::size_t>(bone)]) {
        if (RowShownUVE(plan, child, filter, problemsOnly)) {
            return true;
        }
    }
    return false;
}

void DrawBoneUVE(const RetargetPlanUVE& plan, const std::int32_t bone, const std::string& filter, const bool problemsOnly) {
    if (!RowShownUVE(plan, bone, filter, problemsOnly)) {
        return;
    }
    const Retarget::HumanoidReferenceUVE& reference = Retarget::GetHumanoidReferenceUVE();
    const auto index = static_cast<std::size_t>(bone);
    const RetargetJointRowUVE& row = plan.joints[index];
    const std::vector<std::int32_t>& children = HumanoidChildrenUVE()[index];
    const Retarget::HumanoidBoneKindUVE kind = reference.info[index].kind;
    const bool opensByDefault = kind == Retarget::HumanoidBoneKindUVE::Root || kind == Retarget::HumanoidBoneKindUVE::Body;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
    if (children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    // A filter or the problems switch opens everything that leads to a match.
    if (opensByDefault || !filter.empty() || problemsOnly) {
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    }
    ImGui::PushID(bone);
    const bool open = ImGui::TreeObjectEx("##bone", flags);
    ImGui::SameLine(0.0F, 4.0F);
    DotUVE(ColourOfUVE(row.status));
    ImGui::TextUnformatted(reference.skeleton.bones[index].name.c_str());
    if (!row.characterName.empty() && row.characterName != reference.skeleton.bones[index].name) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", row.characterName.c_str());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s%s%s", GetRetargetStatusLabelUVE(row.status), row.reason.empty() ? "" : ": ",
                          row.status == Retarget::JointStatusUVE::Missing ? "not in this character; Generate adds it"
                                                                          : row.reason.c_str());
    }
    if (open && !children.empty()) {
        for (const std::int32_t child : children) {
            DrawBoneUVE(plan, child, filter, problemsOnly);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

[[nodiscard]] const char* AnimationStateLabelUVE(const Retarget::RetargetAnimationStateUVE state) {
    switch (state) {
        case Retarget::RetargetAnimationStateUVE::Ready: return "will be conformed";
        case Retarget::RetargetAnimationStateUVE::AlreadyConformed: return "already conformed";
        case Retarget::RetargetAnimationStateUVE::NoSkeleton: return "no skeleton";
        case Retarget::RetargetAnimationStateUVE::NoBones: return "moves no bones";
        case Retarget::RetargetAnimationStateUVE::Unreadable: return "cannot read";
    }
    return "";
}

} // namespace

void EditorUVE::OpenRetargetWindowUVE(std::vector<std::filesystem::path> animations, std::filesystem::path target) {
    RetargetWindowStateUVE window;
    window.animations = std::move(animations);
    window.target = std::move(target);
    m_retargetWindow = std::move(window);
    // The window's own world; without it (playing, an entity open) the window still works, minus the picture.
    static_cast<void>(BeginRetargetPreviewUVE());
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
            static_cast<void>(RefreshSkeletonsForRetargetedModelUVE(model));
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
    return GetImportedModelPathUVE(target);
}

std::size_t EditorUVE::RefreshSkeletonsForRetargetedModelUVE(const std::filesystem::path& modelFile) {
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
    const std::size_t skeletons = RefreshSkeletonsForRetargetedModelUVE(window.jobModel);
    window.changedModels.push_back(window.jobModel);
    ClearMeshThumbnailCacheUVE();
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

void EditorUVE::DrawRetargetWindowUVE() {
    RetargetWindowStateUVE& window = *m_retargetWindow;
    // A finished job hands its result back here, on the editor's thread.
    if (window.job.valid() && window.job.wait_for(std::chrono::seconds{0}) == std::future_status::ready) {
        FinishRetargetJobUVE(window, window.job.get());
    }
    const bool running = window.job.valid();

    const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
    // Its own OS window, like the Entity Editor: never merged into the main one, never docked.
    ImGuiWindowClass windowClass;
    windowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
    ImGui::SetNextWindowClass(&windowClass);
    ImGui::SetNextWindowPos(ImVec2{mainViewport->GetCenter().x, mainViewport->GetCenter().y}, ImGuiCond_FirstUseEver,
                            ImVec2{0.5F, 0.5F});
    ImGui::SetNextWindowSize(ImVec2{1320.0F, 760.0F}, ImGuiCond_FirstUseEver);
    bool open = true;
    ImGui::Begin("Retarget###retarget-window", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking);

    // ---- The character ---------------------------------------------------------------------------
    const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    std::vector<std::filesystem::path> candidates;
    for (const auto& [path, info] : m_modelSources) {
        if (info.rigged) {
            candidates.emplace_back(path);
        }
    }
    for (const Asset::ProjectFileEntryUVE& entry : project.entries) {
        if (entry.kind != Asset::ProjectFileEntryKindUVE::Directory && LowerUVE(entry.relativePath.extension().string()) == ".uvmodel") {
            candidates.push_back(entry.relativePath);
        }
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Character");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(320.0F);
    ImGui::BeginDisabled(running);
    const std::string preview = window.target.empty() ? std::string{"Choose the character..."} : window.target.generic_string();
    if (ImGui::BeginCombo("##retarget-target", preview.c_str())) {
        if (candidates.empty()) {
            ImGui::TextDisabled("No rigged model in the project.");
        }
        for (const std::filesystem::path& candidate : candidates) {
            const bool selected = candidate == window.target;
            if (ImGui::Selectable(candidate.generic_string().c_str(), selected) && !selected) {
                window.target = candidate;
                window.planStale = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("The character the animations are for. Its bones, its rest pose and its skin are conformed to the UniVex humanoid.");
    }
    if (window.planStale) {
        const std::filesystem::path modelFile = ResolveRetargetModelFileUVE(window.target);
        window.plan = modelFile.empty() ? RetargetPlanUVE{} : PlanRetargetUVE(modelFile, window.animations);
        if (modelFile.empty()) {
            window.plan.animations.clear();
            for (const std::filesystem::path& file : window.animations) {
                const Retarget::RetargetAnimationCheckUVE check = Retarget::CheckAnimationForRetargetUVE(file);
                window.plan.animations.push_back(RetargetAnimationRowUVE{file, check.state, check.note});
            }
        }
        window.planStale = false;
        // The picture follows the plan: a new character, or files that just changed.
        RebuildRetargetPreviewUVE(window.plan, modelFile);
    }
    const RetargetPlanUVE& plan = window.plan;

    // The four colours, counted: the summary of how the character stands.
    if (plan.modelReadable) {
        ImGui::SameLine(0.0F, 24.0F);
        const std::array<std::pair<Retarget::JointStatusUVE, const char*>, 4> legend{{{Retarget::JointStatusUVE::Good, "good"},
                                                                                       {Retarget::JointStatusUVE::Warning, "check"},
                                                                                       {Retarget::JointStatusUVE::Broken, "broken"},
                                                                                       {Retarget::JointStatusUVE::Missing, "missing"}}};
        for (const auto& [status, label] : legend) {
            DotUVE(ColourOfUVE(status));
            ImGui::Text("%zu %s", plan.CountUVE(status), label);
            ImGui::SameLine(0.0F, 14.0F);
        }
        ImGui::NewLine();
    } else {
        ImGui::SameLine(0.0F, 24.0F);
        ImGui::TextColored(window.target.empty() ? kMissingUVE : kBrokenUVE, "%s",
                           window.target.empty() ? "Pick a character to see how its bones compare." : plan.modelError.c_str());
    }
    ImGui::Separator();

    // ---- Left: the animations. Middle: the viewport. Right: the humanoid's bones ---------------------
    const float footer = 74.0F;
    const float bodyHeight = std::max(120.0F, ImGui::GetContentRegionAvail().y - footer);
    const float leftWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.18F, 200.0F, 280.0F);
    const float rightWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.30F, 300.0F, 420.0F);
    ImGui::BeginChild("##retarget-left", ImVec2{leftWidth, bodyHeight}, true);
    ImGui::TextDisabled("Animations (%zu)", plan.animations.size());
    for (const RetargetAnimationRowUVE& row : plan.animations) {
        const bool ready = row.state == Retarget::RetargetAnimationStateUVE::Ready;
        DotUVE(ready ? kGoodUVE : kMissingUVE);
        ImGui::TextUnformatted(row.file.filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", AnimationStateLabelUVE(row.state));
        if (!ready && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Left alone: %s", row.note.c_str());
        }
    }
    if (!plan.keptBones.empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Character bones the humanoid has no place for (%zu)", plan.keptBones.size());
        ImGui::TextDisabled("They are kept as they are.");
        for (const std::string& name : plan.keptBones) {
            DotUVE(kMissingUVE);
            ImGui::TextUnformatted(name.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 0.0F});
    ImGui::BeginChild("##retarget-view", ImVec2{ImGui::GetContentRegionAvail().x - rightWidth - ImGui::GetStyle().ItemSpacing.x, bodyHeight}, true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    DrawRetargetPreviewUVE();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::SameLine();
    ImGui::BeginChild("##retarget-right", ImVec2{0.0F, bodyHeight}, true);
    ImGui::SetNextItemWidth(220.0F);
    char filterText[96];
    std::snprintf(filterText, sizeof(filterText), "%s", window.boneFilter.c_str());
    if (ImGui::InputTextWithHint("##retarget-filter", "Filter bones", filterText, sizeof(filterText))) {
        window.boneFilter = filterText;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Problems only", &window.problemsOnly);
    ImGui::Separator();
    if (plan.modelReadable) {
        ImGui::BeginChild("##retarget-tree", ImVec2{0.0F, 0.0F}, false);
        DrawBoneUVE(plan, 0, LowerUVE(window.boneFilter), window.problemsOnly);
        ImGui::EndChild();
    } else {
        ImGui::TextDisabled("The UniVex humanoid's bones appear here, coloured by how the character answers them.");
    }
    ImGui::EndChild();

    // ---- Footer: Generate, Undo, progress, and what happened ----------------------------------------
    ImGui::BeginDisabled(!plan.CanGenerateUVE() || running || !IsAuthoringCommandAllowedUVE());
    ImGui::PushStyleColor(ImGuiCol_Button, kAccentUVE);
    if (ImGui::Button("Generate Retarget", ImVec2{170.0F, 30.0F})) {
        Retarget::RetargetFilesRequestUVE request;
        request.model = plan.model;
        request.animations = window.animations;
        request.backupRoot = project.contentRoot.parent_path() / ".retarget-backup";
        window.jobModel = plan.model;
        window.status.clear();
        window.progress->done = 0U;
        window.progress->total = 1U + request.animations.size();
        window.job = std::async(std::launch::async, [request, progress = window.progress]() {
            return Retarget::RetargetFilesUVE(request, Retarget::GetHumanoidReferenceUVE(),
                                              [&progress](const std::size_t done, const std::size_t total, const std::string& what) {
                                                  progress->done = done;
                                                  progress->total = total;
                                                  const std::scoped_lock lock(progress->mutex);
                                                  progress->what = what;
                                              });
        });
    }
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("Conform the character and the animations to the UniVex humanoid, in place.\nThe originals are saved first; Undo puts them back.");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(window.lastBackup.empty() || running);
    if (ImGui::Button("Undo Last", ImVec2{100.0F, 30.0F})) {
        std::string error;
        if (Retarget::RestoreRetargetBackupUVE(window.lastBackup, &error)) {
            Asset::IAssetDatabaseUVE& database = m_services->GetAssetDatabaseUVE();
            const std::filesystem::path modelFile = ResolveRetargetModelFileUVE(window.target);
            for (const std::filesystem::path& file : [&] {
                     std::vector<std::filesystem::path> files{modelFile};
                     files.insert(files.end(), window.animations.begin(), window.animations.end());
                     return files;
                 }()) {
                if (const Asset::AssetGuidUVE guid = database.RegisterUVE(file); guid != Asset::kInvalidAssetGuidUVE) {
                    m_services->GetAssetManagerUVE().ReloadUVE(guid, database);
                }
            }
            const std::size_t skeletons = RefreshSkeletonsForRetargetedModelUVE(modelFile);
            window.changedModels.push_back(modelFile);
            ClearMeshThumbnailCacheUVE();
            window.status = "Put the originals back (" + std::to_string(skeletons) + " skeleton" + (skeletons == 1U ? "" : "s") + " updated).";
            window.statusIsError = false;
            window.lastBackup.clear();
            window.planStale = true;
        } else {
            window.status = error;
            window.statusIsError = true;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0.0F, 18.0F);
    if (running) {
        const std::size_t done = window.progress->done;
        const std::size_t total = std::max<std::size_t>(1U, window.progress->total);
        std::string what;
        {
            const std::scoped_lock lock(window.progress->mutex);
            what = window.progress->what;
        }
        ImGui::ProgressBar(static_cast<float>(done) / static_cast<float>(total), ImVec2{240.0F, 18.0F}, what.c_str());
    } else if (!window.status.empty()) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(window.statusIsError ? kBrokenUVE : kGoodUVE, "%s", window.status.c_str());
    } else if (plan.modelReadable) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%zu of %zu animations will be conformed; %zu bones will be added.", plan.ReadyAnimationsUVE(),
                            plan.animations.size(), plan.CountUVE(Retarget::JointStatusUVE::Missing));
    }
    ImGui::TextDisabled("Files change in place. Originals are saved in .retarget-backup beside the content folder.");
    ImGui::End();

    if (!open) {
        CloseRetargetWindowUVE();
    }
}

} // namespace UVE::Editor
