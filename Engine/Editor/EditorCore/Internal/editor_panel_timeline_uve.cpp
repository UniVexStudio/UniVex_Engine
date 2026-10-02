// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Entity Editor's Timeline tab: the clip of the entity's AnimationSequencer as a dope sheet - one
// row per bone with its keys - under a ruler with a playhead, and a transport that previews the
// clip on the entity's skeleton while the tab is open. The preview writes the skeleton's runtime
// pose only, so it never dirties the entity or reaches its file.

#include "uve/editor/editor_uve.h"
#include "uve/editor/animation_clip_editing_uve.h"
#include "editor_chrome_layout_uve.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <limits>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/component/animation_mixer_component_uve.h"
#include "uve/component/animation_player_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/nodes/3d/animation_sequencer_uve.h"
#include "uve/nodes/3d/skeleton_3d_uve.h"

namespace UVE::Editor {
namespace {

constexpr float kTrackListWidthUVE = 220.0F;
constexpr std::size_t kEventNameBytesUVE = 128U;
constexpr float kRulerHeightUVE = 24.0F;
constexpr float kRowHeightUVE = 20.0F;

constexpr ImU32 kRulerBackgroundUVE = IM_COL32(24, 27, 33, 255);
constexpr ImU32 kRowEvenUVE = IM_COL32(30, 33, 40, 255);
constexpr ImU32 kRowOddUVE = IM_COL32(34, 38, 46, 255);
constexpr ImU32 kRowSelectedUVE = IM_COL32(44, 62, 92, 255);
constexpr ImU32 kTickMajorUVE = IM_COL32(120, 128, 140, 255);
constexpr ImU32 kTickMinorUVE = IM_COL32(70, 76, 86, 255);
constexpr ImU32 kGridLineUVE = IM_COL32(255, 255, 255, 14);
// One colour per channel, used for the keys, their held spans and the legend.
constexpr ImU32 kChannelColourUVE[3] = {IM_COL32(236, 146, 72, 255),  // Position: orange
                                        IM_COL32(96, 200, 132, 255),  // Rotation: green
                                        IM_COL32(150, 128, 236, 255)}; // Scale: violet
constexpr const char* kChannelNameUVE[3] = {"Position", "Rotation", "Scale"};
constexpr ImU32 kEventUVE = IM_COL32(112, 196, 255, 255);
constexpr ImU32 kPlayheadUVE = IM_COL32(236, 86, 86, 255);
constexpr ImU32 kOutOfRangeUVE = IM_COL32(0, 0, 0, 70);

/// Frames per second the clip was baked at, from its densest track; 30 when it cannot tell.
[[nodiscard]] double EstimateFrameRateUVE(const Asset::AnimationClipAssetUVE& clip) {
    std::size_t most = clip.samples.size();
    for (const Asset::AnimationAssetBoneTrackUVE& track : clip.bones) {
        most = std::max(most, track.samples.size());
    }
    if (most < 2U || clip.durationSeconds <= 0.0) {
        return 30.0;
    }
    // A baked clip has a key every frame; a hand-made one with a few keys was still made at 30.
    const double rate = static_cast<double>(most - 1U) / clip.durationSeconds;
    return rate < 10.0 ? 30.0 : std::clamp(std::round(rate), 1.0, 240.0);
}

/// A tick spacing, in seconds, that keeps labels at least `minimumPixels` apart.
[[nodiscard]] double PickTickStepUVE(const double pixelsPerSecond, const double frameRate, const float minimumPixels) {
    const double frame = 1.0 / frameRate;
    for (const double step : {frame, frame * 2.0, frame * 5.0, frame * 10.0, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0}) {
        if (step * pixelsPerSecond >= minimumPixels) {
            return step;
        }
    }
    return 120.0;
}

/// True when `channel` (0 position, 1 rotation, 2 scale) differs between two samples: a key worth
/// showing. A baked clip stores every channel every frame; only the ones that move are keys.
[[nodiscard]] bool ChannelChangesUVE(const Asset::AnimationAssetPoseUVE& before, const Asset::AnimationAssetPoseUVE& after,
                                     const int channel) {
    constexpr float kEpsilonUVE = 1.0e-5F;
    const auto differs = [](const float a, const float b) { return std::abs(a - b) > kEpsilonUVE; };
    switch (channel) {
        case 0:
            return differs(before.position.x, after.position.x) || differs(before.position.y, after.position.y) ||
                   differs(before.position.z, after.position.z);
        case 1:
            return differs(before.rotation.x, after.rotation.x) || differs(before.rotation.y, after.rotation.y) ||
                   differs(before.rotation.z, after.rotation.z) || differs(before.rotation.w, after.rotation.w);
        default:
            return differs(before.scale.x, after.scale.x) || differs(before.scale.y, after.scale.y) ||
                   differs(before.scale.z, after.scale.z);
    }
}

/// True when sample `i` is a key worth showing on a row: the channel (or, for -1, any channel)
/// moves into or out of it.
[[nodiscard]] bool ShowsKeyUVE(const std::vector<Asset::AnimationAssetSampleUVE>& samples, const std::size_t i,
                               const int channel) {
    // The ends of a track are always keys, so a track that holds still still shows where it starts
    // and stops.
    if (samples.size() == 1U || i == 0U || i + 1U == samples.size()) {
        return true;
    }
    for (int c = channel < 0 ? 0 : channel; c <= (channel < 0 ? 2 : channel); ++c) {
        if ((i > 0U && ChannelChangesUVE(samples[i - 1U].pose, samples[i].pose, c)) ||
            (i + 1U < samples.size() && ChannelChangesUVE(samples[i].pose, samples[i + 1U].pose, c))) {
            return true;
        }
    }
    return false;
}

} // namespace

void EditorUVE::StopAnimationTimelinePreviewUVE() {
    m_timeline.playing = false;
    m_timelineOwnsKeys = false;
    if (m_timeline.previewSkeleton == Scene::kInvalidEntityUVE) {
        return;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (entityManager.IsAliveUVE(m_timeline.previewSkeleton) &&
        entityManager.HasComponentUVE<Scene::Skeleton3DNodeComponentUVE>(m_timeline.previewSkeleton)) {
        entityManager.GetComponentUVE<Scene::Skeleton3DNodeComponentUVE>(m_timeline.previewSkeleton).pose.clear();
    }
    m_timeline.previewSkeleton = Scene::kInvalidEntityUVE;
}

void EditorUVE::DrawAnimationTimelineUVE() {
    // Last frame's replaced clip is no longer read by anything.
    m_timeline.retired.reset();
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = m_services->GetSceneGraphUVE();
    const auto nameOf = [&entityManager](const Scene::EntityUVE entity) {
        return entityManager.HasComponentUVE<Scene::NameComponentUVE>(entity)
                   ? entityManager.GetComponentUVE<Scene::NameComponentUVE>(entity).name
                   : std::string{"AnimationSequencer"};
    };

    // ---- Which player: the selected one, else the one shown last, else the entity's first -------
    std::vector<Scene::EntityUVE> players;
    for (const Scene::EntityUVE node : CollectEntityEditorNodesUVE()) {
        if (entityManager.HasComponentUVE<Scene::AnimationPlayerComponentUVE>(node)) {
            players.push_back(node);
        }
    }
    if (players.empty()) {
        StopAnimationTimelinePreviewUVE();
        return;
    }
    if (std::find(players.begin(), players.end(), m_selectedEntity) != players.end()) {
        m_timeline.player = m_selectedEntity;
    } else if (std::find(players.begin(), players.end(), m_timeline.player) == players.end()) {
        // The first one with a clip: an entity's own empty player should not hide a working one.
        m_timeline.player = players.front();
        for (const Scene::EntityUVE candidate : players) {
            if (entityManager.GetComponentUVE<Scene::AnimationPlayerComponentUVE>(candidate).clip !=
                Asset::AssetGuidUVE{}) {
                m_timeline.player = candidate;
                break;
            }
        }
    }
    const Scene::EntityUVE playerEntity = m_timeline.player;
    const Scene::AnimationPlayerComponentUVE& player =
        entityManager.GetComponentUVE<Scene::AnimationPlayerComponentUVE>(playerEntity);
    const Scene::AnimationMixerComponentUVE mixer =
        entityManager.HasComponentUVE<Scene::AnimationMixerComponentUVE>(playerEntity)
            ? entityManager.GetComponentUVE<Scene::AnimationMixerComponentUVE>(playerEntity)
            : Scene::AnimationMixerComponentUVE{};

    // ---- The clip, reloaded when the player's clip changes ----------------------------------------
    if (player.clip != m_timeline.clipGuid) {
        // Switching away keeps the work: edits not yet saved are written to their clip first.
        if (m_timeline.dirty && m_timeline.clip != nullptr) {
            const std::filesystem::path previous = m_services->GetAssetDatabaseUVE().ResolveUVE(m_timeline.clipGuid);
            if (!previous.empty() && Asset::SaveAnimationClipAssetUVE(*m_timeline.clip, previous)) {
                m_timeline.status = "Saved your edits to " + previous.filename().string();
            }
        }
        StopAnimationTimelinePreviewUVE();
        m_timeline.clipGuid = player.clip;
        m_timeline.clip.reset();
        m_timeline.selectedKeys.clear();
        m_timeline.selectedEvent = -1;
        m_timeline.undo.clear();
        m_timeline.redo.clear();
        m_timeline.dirty = false;
        m_timeline.draggingKeys = false;
        m_timeline.boxSelecting = false;
        m_timeline.loadError.clear();
        m_timeline.timeSeconds = 0.0;
        m_timeline.scrollSeconds = 0.0;
        if ((player.clip != Asset::AssetGuidUVE{})) {
            auto loaded = std::make_shared<Asset::AnimationClipAssetUVE>();
            const std::filesystem::path path = m_services->GetAssetDatabaseUVE().ResolveUVE(player.clip);
            if (Asset::LoadAnimationClipAssetUVE(path, *loaded)) {
                m_timeline.clip = std::move(loaded);
            } else {
                m_timeline.loadError = "Could not read " + path.filename().string() + ".";
            }
        }
    }

    // ---- The skeleton it previews on: the mixer's target, else the player's parent, searched down -
    Scene::EntityUVE skeletonEntity = Scene::kInvalidEntityUVE;
    {
        Scene::EntityUVE root = mixer.target;
        if (root == Scene::kInvalidEntityUVE || !entityManager.IsAliveUVE(root)) {
            root = entityManager.HasComponentUVE<Scene::HierarchyComponentUVE>(playerEntity)
                       ? entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(playerEntity).parent
                       : Scene::kInvalidEntityUVE;
        }
        std::vector<Scene::EntityUVE> queue;
        if (root != Scene::kInvalidEntityUVE && entityManager.IsAliveUVE(root)) {
            queue.push_back(root);
        }
        for (std::size_t next = 0U; next < queue.size() && next < 4096U; ++next) {
            if (entityManager.HasComponentUVE<Scene::Skeleton3DNodeComponentUVE>(queue[next])) {
                skeletonEntity = queue[next];
                break;
            }
            const std::vector<Scene::EntityUVE> children = sceneGraph.GetChildrenUVE(entityManager, queue[next]);
            queue.insert(queue.end(), children.begin(), children.end());
        }
    }

    const Asset::AnimationClipAssetUVE* const clip = m_timeline.clip.get();
    const double duration = clip != nullptr ? std::max(clip->durationSeconds, 1.0e-3) : 1.0;
    const double frameRate = clip != nullptr ? EstimateFrameRateUVE(*clip) : 30.0;
    const double frame = 1.0 / frameRate;
    const auto frameOf = [frameRate](const double seconds) {
        return static_cast<long>(std::lround(seconds * frameRate));
    };

    // ---- Editing ----------------------------------------------------------------------------------
    // Every change to the clip goes through `edit`: one undo step, and the preview shows it at once.
    const auto edit = [this](const auto& change) {
        auto next = std::make_shared<Asset::AnimationClipAssetUVE>(*m_timeline.clip);
        change(*next);
        m_timeline.undo.push_back(m_timeline.clip);
        if (m_timeline.undo.size() > 64U) {
            m_timeline.undo.erase(m_timeline.undo.begin());
        }
        m_timeline.redo.clear();
        m_timeline.clip = std::move(next);
        m_timeline.dirty = true;
    };
    const auto undoEdit = [this]() {
        if (!m_timeline.undo.empty()) {
            m_timeline.redo.push_back(m_timeline.clip);
            m_timeline.clip = m_timeline.undo.back();
            m_timeline.undo.pop_back();
            m_timeline.selectedKeys.clear();
            m_timeline.selectedEvent = -1;
            m_timeline.dirty = true;
        }
    };
    const auto redoEdit = [this]() {
        if (!m_timeline.redo.empty()) {
            m_timeline.undo.push_back(m_timeline.clip);
            m_timeline.clip = m_timeline.redo.back();
            m_timeline.redo.pop_back();
            m_timeline.selectedKeys.clear();
            m_timeline.selectedEvent = -1;
            m_timeline.dirty = true;
        }
    };
    // K: a key on the selected bone at the playhead, holding the motion it already has there.
    const auto insertKey = [this, &edit, frameRate]() {
        if (m_timeline.clip == nullptr) {
            return;
        }
        // The selected bone's track, else the clip's own node track.
        std::optional<std::string> chosen;
        if (FindClipTrackSamplesUVE(*m_timeline.clip, m_selectedSkeletonBone) != nullptr) {
            chosen = m_selectedSkeletonBone;
        } else if (!m_timeline.clip->samples.empty()) {
            chosen = std::string{};
        }
        if (!chosen.has_value()) {
            m_timeline.status = "Select a bone's row first, then press K.";
            return;
        }
        const std::string track = *chosen;
        Asset::AnimationClipAssetUVE probe = *m_timeline.clip;
        if (!InsertClipKeyUVE(probe, track, m_timeline.timeSeconds, frameRate)) {
            m_timeline.status = "There is already a key there.";
            return;
        }
        edit([&](Asset::AnimationClipAssetUVE& next) {
            static_cast<void>(InsertClipKeyUVE(next, track, m_timeline.timeSeconds, frameRate));
        });
        m_timeline.selectedKeys = {ClipKeyUVE{track, std::round(m_timeline.timeSeconds * frameRate) / frameRate}};
        m_timeline.status = "Key added to " + (track.empty() ? std::string{"Transform"} : track);
    };
    const auto saveClip = [this]() {
        if (m_timeline.clip == nullptr || !m_timeline.dirty) {
            return;
        }
        const std::filesystem::path path = m_services->GetAssetDatabaseUVE().ResolveUVE(m_timeline.clipGuid);
        if (Asset::SaveAnimationClipAssetUVE(*m_timeline.clip, path)) {
            m_timeline.dirty = false;
            m_timeline.status = "Saved " + path.filename().string();
        } else {
            m_timeline.status = "Could not save " + path.filename().string();
        }
    };

    // ---- Which player, when the entity has several ---------------------------------------------------
    if (players.size() > 1U) {
        ImGui::SetNextItemWidth(160.0F);
        if (ImGui::BeginCombo("##tl-player", nameOf(playerEntity).c_str())) {
            for (const Scene::EntityUVE candidate : players) {
                ImGui::PushID(static_cast<int>(candidate.index));
                if (ImGui::Selectable(nameOf(candidate).c_str(), candidate == playerEntity)) {
                    SelectEntityUVE(candidate);
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("The AnimationSequencer this Timeline shows. Selecting a player in the tree picks it too.");
        }
        ImGui::SameLine();
    }

    // ---- Which animation: the player's list, and the ways to grow it --------------------------------
    DrawAnimationPickerUVE(playerEntity, skeletonEntity);
    ImGui::SameLine(0.0F, 12.0F);

    // ---- Transport ---------------------------------------------------------------------------------
    const bool canPlay = clip != nullptr;
    ImGui::BeginDisabled(!canPlay);
    if (ImGui::SmallButton("|<##tl-start")) {
        m_timeline.timeSeconds = 0.0;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("First frame (Home)");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("<##tl-prev")) {
        m_timeline.playing = false;
        m_timeline.timeSeconds = std::max(0.0, (static_cast<double>(frameOf(m_timeline.timeSeconds)) - 1.0) * frame);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Previous frame (Left)");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(m_timeline.playing ? "Pause##tl-play" : "Play##tl-play")) {
        m_timeline.playing = !m_timeline.playing;
        if (m_timeline.playing && !m_timeline.loop && m_timeline.timeSeconds >= duration) {
            m_timeline.timeSeconds = 0.0;
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Preview the clip on this entity (Space)");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Stop##tl-stop")) {
        m_timeline.playing = false;
        m_timeline.timeSeconds = 0.0;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(">##tl-next")) {
        m_timeline.playing = false;
        m_timeline.timeSeconds = std::min(duration, (static_cast<double>(frameOf(m_timeline.timeSeconds)) + 1.0) * frame);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Next frame (Right)");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(">|##tl-end")) {
        m_timeline.timeSeconds = duration;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Last frame (End)");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop##tl-loop", &m_timeline.loop);
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    // The playhead's frame and time; everything else about the clip lives in the bottom bar.
    char clock[64];
    std::snprintf(clock, sizeof(clock), "%ld / %ld", frameOf(m_timeline.timeSeconds), frameOf(duration));
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("0000 / 0000").x + ImGui::GetStyle().FramePadding.x * 2.0F);
    ImGui::BeginDisabled();
    ImGui::InputText("##tl-frame", clock, sizeof(clock), ImGuiInputTextFlags_ReadOnly);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%.2f s", m_timeline.timeSeconds);
    ImGui::SameLine(0.0F, 16.0F);
    ImGui::BeginDisabled(clip == nullptr);
    if (ImGui::SmallButton("Key##tl-key")) {
        insertKey();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Add a key to the selected bone at the playhead (K)");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_timeline.dirty);
    if (ImGui::SmallButton(m_timeline.dirty ? "Save Clip*##tl-save" : "Save Clip##tl-save")) {
        saveClip();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Write the edited clip to its .uvanim (Ctrl+S)");
    }
    ImGui::EndDisabled();

    // An animation is called by its file's name, everywhere.
    const std::string clipLabel = clip != nullptr ? m_services->GetAssetDatabaseUVE().ResolveUVE(m_timeline.clipGuid).stem().string()
                                  : (player.clip != Asset::AssetGuidUVE{}) ? std::string{"(unreadable clip)"}
                                                                           : std::string{"(no clip)"};
    if (clip == nullptr) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", !m_timeline.loadError.empty()
                                      ? m_timeline.loadError.c_str()
                                      : "No animation yet: open the animation menu for New Animation or Add from Project, or drag a .uvanim here.");
        StopAnimationTimelinePreviewUVE();
        return;
    }

    // ---- Advance the preview -----------------------------------------------------------------------
    if (m_timeline.playing) {
        const double speed = std::isfinite(player.speed) && player.speed != 0.0F ? player.speed : 1.0;
        m_timeline.timeSeconds += static_cast<double>(ImGui::GetIO().DeltaTime) * speed * mixer.speedScale;
        if (m_timeline.loop) {
            m_timeline.timeSeconds = std::fmod(m_timeline.timeSeconds, duration);
            if (m_timeline.timeSeconds < 0.0) {
                m_timeline.timeSeconds += duration;
            }
        } else if (m_timeline.timeSeconds >= duration || m_timeline.timeSeconds <= 0.0) {
            m_timeline.timeSeconds = std::clamp(m_timeline.timeSeconds, 0.0, duration);
            m_timeline.playing = false;
        }
    }

    // ---- Rows: events first, then each bone with a track, in the skeleton's order -------------------
    // A track row shows all three channels as stacked lanes; opened, it is followed by one row per
    // channel.
    struct RowUVE {
        std::string label;
        int depth = 0;
        const std::vector<Asset::AnimationAssetSampleUVE>* samples = nullptr;
        bool events = false;
        /// -1 for the track row itself, else the channel (0 position, 1 rotation, 2 scale).
        int channel = -1;
        std::string track;
    };
    std::vector<RowUVE> rows;
    const auto isExpanded = [this](const std::string& track) {
        return std::find(m_timeline.expandedTracks.begin(), m_timeline.expandedTracks.end(), track) !=
               m_timeline.expandedTracks.end();
    };
    const auto pushTrack = [&rows, &isExpanded](const std::string& label, const int depth,
                                                const std::vector<Asset::AnimationAssetSampleUVE>* samples) {
        rows.push_back(RowUVE{label, depth, samples, false, -1, label});
        if (isExpanded(label)) {
            for (int channel = 0; channel < 3; ++channel) {
                rows.push_back(RowUVE{kChannelNameUVE[channel], depth + 1, samples, false, channel, label});
            }
        }
    };
    // Always there, so the first event can be added: double-click the row.
    rows.push_back(RowUVE{"Events", 0, nullptr, true, -1, {}});
    if (!clip->samples.empty()) {
        pushTrack("Transform", 0, &clip->samples);
    }
    std::unordered_map<std::string, const Asset::AnimationAssetBoneTrackUVE*> tracks;
    for (const Asset::AnimationAssetBoneTrackUVE& track : clip->bones) {
        tracks.emplace(track.bone, &track);
    }
    const auto matches = [this](const std::string& label) {
        if (m_timeline.filter.empty()) {
            return true;
        }
        const auto lower = [](std::string text) {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        };
        return lower(label).find(lower(m_timeline.filter)) != std::string::npos;
    };
    const Scene::Skeleton3DNodeComponentUVE* const skeleton =
        skeletonEntity != Scene::kInvalidEntityUVE
            ? &entityManager.GetComponentUVE<Scene::Skeleton3DNodeComponentUVE>(skeletonEntity)
            : nullptr;
    if (skeleton != nullptr) {
        std::vector<int> depth(skeleton->bones.size(), 0);
        for (std::size_t index = 0U; index < skeleton->bones.size(); ++index) {
            const Scene::SkeletonBoneUVE& bone = skeleton->bones[index];
            if (bone.parentIndex >= 0 && static_cast<std::size_t>(bone.parentIndex) < index) {
                depth[index] = depth[static_cast<std::size_t>(bone.parentIndex)] + 1;
            }
            const auto found = tracks.find(bone.name);
            if (found != tracks.end() && matches(bone.name)) {
                pushTrack(bone.name, depth[index], &found->second->samples);
            }
        }
    } else {
        for (const Asset::AnimationAssetBoneTrackUVE& track : clip->bones) {
            if (matches(track.bone)) {
                pushTrack(track.bone, 0, &track.samples);
            }
        }
    }

    // ---- Preview on the skeleton ------------------------------------------------------------------
    if (skeletonEntity != m_timeline.previewSkeleton) {
        StopAnimationTimelinePreviewUVE();
    }
    if (clip->IsSkeletalUVE() && skeletonEntity != Scene::kInvalidEntityUVE) {
        auto& posed = entityManager.GetComponentUVE<Scene::Skeleton3DNodeComponentUVE>(skeletonEntity);
        if (Scene::PoseSkeletonAtTimeUVE(*clip, m_timeline.timeSeconds, posed, mixer)) {
            m_timeline.previewSkeleton = skeletonEntity;
        }
    }

    // ---- Track area --------------------------------------------------------------------------------
    const float bottomBarHeight = ImGui::GetFrameHeight() + 6.0F;
    ImGui::BeginChild("##tl-body", ImVec2{0.0F, -bottomBarHeight}, false, ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 area = ImGui::GetContentRegionAvail();
    const float keysLeft = origin.x + kTrackListWidthUVE;
    const float keysWidth = std::max(40.0F, area.x - kTrackListWidthUVE - 8.0F);
    const double fitScale = static_cast<double>(keysWidth) / duration;
    const double scale = m_timeline.pixelsPerSecond > 0.0F ? static_cast<double>(m_timeline.pixelsPerSecond) : fitScale;
    const double visibleSeconds = static_cast<double>(keysWidth) / scale;
    m_timeline.scrollSeconds =
        m_timeline.pixelsPerSecond > 0.0F ? std::clamp(m_timeline.scrollSeconds, 0.0, std::max(0.0, duration - visibleSeconds))
                                          : 0.0;
    const auto toX = [&](const double seconds) {
        return keysLeft + static_cast<float>((seconds - m_timeline.scrollSeconds) * scale);
    };
    const auto toSeconds = [&](const float x) {
        return m_timeline.scrollSeconds + static_cast<double>(x - keysLeft) / scale;
    };

    // Ruler.
    draw->AddRectFilled(origin, ImVec2{origin.x + area.x, origin.y + kRulerHeightUVE}, kRulerBackgroundUVE);
    const double tick = PickTickStepUVE(scale, frameRate, 56.0F);
    const double minorTick = std::max(frame, tick / 5.0);
    const float rowsBottom = origin.y + area.y;
    for (double t = std::floor(m_timeline.scrollSeconds / minorTick) * minorTick;
         t <= m_timeline.scrollSeconds + visibleSeconds + minorTick; t += minorTick) {
        const float x = toX(t);
        if (x < keysLeft - 0.5F || x > keysLeft + keysWidth + 0.5F || t > duration + 1e-9) {
            continue;
        }
        const bool major = std::abs(std::remainder(t, tick)) < minorTick * 0.25;
        draw->AddLine(ImVec2{x, origin.y + (major ? 10.0F : 17.0F)}, ImVec2{x, origin.y + kRulerHeightUVE},
                      major ? kTickMajorUVE : kTickMinorUVE);
        if (major) {
            char label[32];
            if (tick < 1.0) {
                std::snprintf(label, sizeof(label), "%ld", frameOf(t));
            } else {
                std::snprintf(label, sizeof(label), "%.0fs", t);
            }
            draw->AddText(ImVec2{x + 3.0F, origin.y + 1.0F}, kTickMajorUVE, label);
            draw->AddLine(ImVec2{x, origin.y + kRulerHeightUVE}, ImVec2{x, rowsBottom}, kGridLineUVE);
        }
    }
    // The track list's own header: its filter.
    {
        char filter[128];
        std::snprintf(filter, sizeof(filter), "%s", m_timeline.filter.c_str());
        ImGui::SetCursorScreenPos(ImVec2{origin.x + 4.0F, origin.y + 1.0F});
        ImGui::SetNextItemWidth(kTrackListWidthUVE - 12.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{6.0F, 2.0F});
        if (ImGui::InputTextWithHint("##tl-filter", skeleton != nullptr ? "Filter bones" : "Filter tracks", filter,
                                     sizeof(filter))) {
            m_timeline.filter = filter;
        }
        ImGui::PopStyleVar();
    }

    // Scrubbing: drag on the ruler or click in the key area moves the playhead.
    ImGui::SetCursorScreenPos(ImVec2{keysLeft, origin.y});
    ImGui::InvisibleButton("##tl-ruler", ImVec2{keysWidth, kRulerHeightUVE});
    if (ImGui::IsItemActive()) {
        m_timeline.playing = false;
        const double snapped = std::round(toSeconds(ImGui::GetIO().MousePos.x) * frameRate) / frameRate;
        m_timeline.timeSeconds = std::clamp(snapped, 0.0, duration);
    }

    // Key selection and dragging. A drag shows the keys where they would land, snapped to frames.
    const double dragDelta =
        m_timeline.draggingKeys
            ? std::round(static_cast<double>(ImGui::GetIO().MousePos.x - m_timeline.dragFromX) / scale * frameRate) /
                  frameRate
            : 0.0;
    const auto isSelected = [this](const std::string& track, const double seconds) {
        return std::any_of(m_timeline.selectedKeys.begin(), m_timeline.selectedKeys.end(),
                           [&](const ClipKeyUVE& key) {
                               return key.track == track &&
                                      std::abs(key.timeSeconds - seconds) < kClipKeyTimeToleranceUVE;
                           });
    };
    int clickedRow = -1;

    // Rows, clipped to what is on screen.
    const float rowsTop = origin.y + kRulerHeightUVE;
    ImGui::SetCursorScreenPos(ImVec2{origin.x, rowsTop});
    ImGui::BeginChild("##tl-rows", ImVec2{0.0F, 0.0F}, false);
    ImDrawList* const rowDraw = ImGui::GetWindowDrawList();
    const ImVec2 rowsOrigin = ImGui::GetCursorScreenPos();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()), kRowHeightUVE);
    while (clipper.Step()) {
        for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
            const RowUVE& row = rows[static_cast<std::size_t>(index)];
            const float y = rowsOrigin.y + static_cast<float>(index) * kRowHeightUVE;
            // The selected bone's row, or its channel row when a channel was picked.
            const bool selected = !row.events && row.track == m_selectedSkeletonBone &&
                                  row.channel == m_timeline.curveChannel;
            const ImU32 background = selected ? kRowSelectedUVE : (index % 2 == 0 ? kRowEvenUVE : kRowOddUVE);
            rowDraw->AddRectFilled(ImVec2{origin.x, y}, ImVec2{origin.x + area.x, y + kRowHeightUVE}, background);
            ImGui::SetCursorScreenPos(ImVec2{origin.x, y});
            ImGui::PushID(index);
            const float indent = 8.0F + static_cast<float>(std::min(row.depth, 24)) * 10.0F;
            if (ImGui::InvisibleButton("##row", ImVec2{kTrackListWidthUVE, kRowHeightUVE}) && !row.events) {
                // The arrow opens the track into its channels; the name selects the bone.
                const bool onArrow = row.channel < 0 && ImGui::GetIO().MousePos.x < origin.x + indent + 12.0F;
                if (onArrow) {
                    const auto open = std::find(m_timeline.expandedTracks.begin(), m_timeline.expandedTracks.end(),
                                                row.track);
                    if (open != m_timeline.expandedTracks.end()) {
                        m_timeline.expandedTracks.erase(open);
                    } else {
                        m_timeline.expandedTracks.push_back(row.track);
                    }
                } else {
                    m_selectedSkeletonBone = row.track;
                    m_timeline.curveChannel = row.channel;
                }
            }
            ImGui::PopID();
            rowDraw->PushClipRect(ImVec2{origin.x, y}, ImVec2{keysLeft - 6.0F, y + kRowHeightUVE}, true);
            if (row.events) {
                rowDraw->AddText(ImVec2{origin.x + indent, y + 3.0F}, kEventUVE, row.label.c_str());
            } else if (row.channel < 0) {
                const bool open = isExpanded(row.track);
                const float ax = origin.x + indent + 3.0F;
                const float ay = y + kRowHeightUVE * 0.5F;
                const ImU32 arrow = ImGui::GetColorU32(ImGuiCol_TextDisabled);
                if (open) {
                    rowDraw->AddTriangleFilled(ImVec2{ax - 3.5F, ay - 2.0F}, ImVec2{ax + 3.5F, ay - 2.0F},
                                               ImVec2{ax, ay + 2.5F}, arrow);
                } else {
                    rowDraw->AddTriangleFilled(ImVec2{ax - 2.0F, ay - 3.5F}, ImVec2{ax - 2.0F, ay + 3.5F},
                                               ImVec2{ax + 2.5F, ay}, arrow);
                }
                rowDraw->AddText(ImVec2{origin.x + indent + 12.0F, y + 3.0F}, ImGui::GetColorU32(ImGuiCol_Text),
                                 row.label.c_str());
            } else {
                const float sx = origin.x + indent + 12.0F;
                rowDraw->AddRectFilled(ImVec2{sx, y + 6.0F}, ImVec2{sx + 8.0F, y + 14.0F},
                                       kChannelColourUVE[row.channel], 2.0F);
                rowDraw->AddText(ImVec2{sx + 13.0F, y + 3.0F}, ImGui::GetColorU32(ImGuiCol_TextDisabled),
                                 row.label.c_str());
            }
            rowDraw->PopClipRect();

            if (!m_timeline.curves) {
                rowDraw->PushClipRect(ImVec2{keysLeft, y}, ImVec2{keysLeft + keysWidth, y + kRowHeightUVE}, true);
                const float mid = y + kRowHeightUVE * 0.5F;
                if (row.events) {
                    // Each event: a marker and its name; the selected one outlined, shown where a
                    // drag would put it.
                    for (std::size_t e = 0U; e < clip->events.size(); ++e) {
                        const bool chosen = static_cast<int>(e) == m_timeline.selectedEvent;
                        double at = clip->events[e].timeSeconds;
                        if (chosen && m_timeline.draggingEvent) {
                            at = std::clamp(std::round((at + static_cast<double>(ImGui::GetIO().MousePos.x -
                                                                                  m_timeline.eventDragFromX) /
                                                                  scale) *
                                                       frameRate) /
                                                frameRate,
                                            0.0, duration);
                        }
                        const float x = toX(at);
                        rowDraw->AddTriangleFilled(ImVec2{x, mid - 6.0F}, ImVec2{x + 5.0F, mid + 5.0F},
                                                   ImVec2{x - 5.0F, mid + 5.0F}, kEventUVE);
                        if (chosen) {
                            rowDraw->AddTriangle(ImVec2{x, mid - 8.0F}, ImVec2{x + 7.0F, mid + 6.5F},
                                                 ImVec2{x - 7.0F, mid + 6.5F}, IM_COL32_WHITE, 1.5F);
                        }
                        rowDraw->AddText(ImVec2{x + 7.0F, y + 3.0F}, chosen ? IM_COL32_WHITE : kEventUVE,
                                         clip->events[e].eventId.c_str());
                    }
                    if (clip->events.empty()) {
                        rowDraw->AddText(ImVec2{keysLeft + 6.0F, y + 3.0F}, ImGui::GetColorU32(ImGuiCol_TextDisabled),
                                         "Double-click to add an event");
                    }
                } else if (row.samples != nullptr && !row.samples->empty()) {
                    const auto& samples = *row.samples;
                    // A track row stacks its three channels in thin lanes; a channel row gives one the
                    // full height. Keys are the frames where the channel moves: a baked track has one
                    // every frame, and when they would touch they merge into a held span.
                    const double spacing = samples.size() > 1U
                                               ? (samples.back().timeSeconds - samples.front().timeSeconds) /
                                                     static_cast<double>(samples.size() - 1U)
                                               : duration;
                    // Stacked lanes are thin: keys there read as bars until they are well apart.
                    const bool dense = spacing * scale < (row.channel < 0 ? 18.0 : 7.0);
                    const int first = row.channel < 0 ? 0 : row.channel;
                    const int last = row.channel < 0 ? 2 : row.channel;
                    for (int channel = first; channel <= last; ++channel) {
                        const float laneHeight = row.channel < 0 ? (kRowHeightUVE - 4.0F) / 3.0F : kRowHeightUVE - 6.0F;
                        const float laneTop = row.channel < 0 ? y + 2.0F + laneHeight * static_cast<float>(channel)
                                                              : y + 3.0F;
                        const float laneMid = laneTop + laneHeight * 0.5F;
                        const float half = std::max(2.0F, laneHeight * 0.5F - (row.channel < 0 ? 0.5F : 1.5F));
                        const ImU32 colour = kChannelColourUVE[channel];
                        const ImU32 span = (colour & 0x00FFFFFFU) | (static_cast<ImU32>(row.channel < 0 ? 150 : 110) << 24);
                        const auto moves = [&](const std::size_t i) { return ShowsKeyUVE(samples, i, channel); };
                        if (dense) {
                            std::size_t i = 0U;
                            while (i < samples.size()) {
                                if (!moves(i)) {
                                    ++i;
                                    continue;
                                }
                                std::size_t j = i;
                                while (j + 1U < samples.size() && moves(j + 1U)) {
                                    ++j;
                                }
                                rowDraw->AddRectFilled(ImVec2{toX(samples[i].timeSeconds), laneMid - half * 0.6F},
                                                       ImVec2{std::max(toX(samples[j].timeSeconds), toX(samples[i].timeSeconds) + 2.0F),
                                                              laneMid + half * 0.6F},
                                                       span, 1.5F);
                                i = j + 1U;
                            }
                        } else {
                            for (std::size_t i = 0U; i < samples.size(); ++i) {
                                const float x = toX(samples[i].timeSeconds);
                                if (x < keysLeft - 6.0F || x > keysLeft + keysWidth + 6.0F || !moves(i)) {
                                    continue;
                                }
                                rowDraw->AddQuadFilled(ImVec2{x, laneMid - half}, ImVec2{x + half, laneMid},
                                                       ImVec2{x, laneMid + half}, ImVec2{x - half, laneMid}, colour);
                            }
                        }
                    }
                }
                // Selected keys: a white outline, drawn where a drag would put them.
                if (!row.events && row.samples != nullptr && !m_timeline.selectedKeys.empty()) {
                    const auto& samples = *row.samples;
                    for (std::size_t i = 0U; i < samples.size(); ++i) {
                        if (!isSelected(row.track, samples[i].timeSeconds) || !ShowsKeyUVE(samples, i, row.channel)) {
                            continue;
                        }
                        const double shown = std::clamp(samples[i].timeSeconds + dragDelta, 0.0, duration);
                        const float x = toX(shown);
                        const float half = row.channel < 0 ? 6.0F : 6.5F;
                        rowDraw->AddQuad(ImVec2{x, mid - half}, ImVec2{x + half, mid}, ImVec2{x, mid + half},
                                         ImVec2{x - half, mid}, IM_COL32_WHITE, 1.5F);
                    }
                }
                rowDraw->PopClipRect();
                // The key area takes the clicks: select, drag and box-select.
                ImGui::SetCursorScreenPos(ImVec2{keysLeft, y});
                ImGui::PushID(index);
                ImGui::InvisibleButton("##keys", ImVec2{keysWidth, kRowHeightUVE});
                if (row.events) {
                    // Events: click selects and starts a drag; double-click names one, or adds one
                    // on empty space.
                    const float mouseX = ImGui::GetIO().MousePos.x;
                    int hit = -1;
                    float best = 8.0F;
                    for (std::size_t e = 0U; e < clip->events.size(); ++e) {
                        const float distance = std::abs(toX(clip->events[e].timeSeconds) - mouseX);
                        if (distance < best) {
                            best = distance;
                            hit = static_cast<int>(e);
                        }
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        m_timeline.draggingEvent = false;
                        if (hit < 0) {
                            std::size_t added = 0U;
                            edit([&](Asset::AnimationClipAssetUVE& next) {
                                added = AddClipEventUVE(next, toSeconds(mouseX), frameRate);
                            });
                            hit = static_cast<int>(added);
                        }
                        m_timeline.selectedEvent = hit;
                        m_timeline.renameEventRequested = true;
                    } else if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                        m_timeline.selectedEvent = hit;
                        m_timeline.selectedKeys.clear();
                        if (hit >= 0) {
                            m_timeline.draggingEvent = true;
                            m_timeline.eventDragFromX = mouseX;
                        }
                    }
                } else if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                    clickedRow = index;
                    m_timeline.selectedEvent = -1;
                }
                ImGui::PopID();
            }
        }
    }
    clipper.End();
    ImGui::Dummy(ImVec2{1.0F, 0.0F});

    // ---- Curves view: the selected bone's channels as value-over-time lines -----------------------
    if (m_timeline.curves) {
        const ImVec2 windowPos = ImGui::GetWindowPos();
        const ImVec2 windowSize = ImGui::GetWindowSize();
        const float graphTop = windowPos.y + 2.0F;
        const float graphBottom = windowPos.y + windowSize.y - 2.0F;
        const float graphRight = keysLeft + keysWidth;
        rowDraw->PushClipRect(ImVec2{keysLeft, windowPos.y}, ImVec2{graphRight + 8.0F, windowPos.y + windowSize.y},
                              true);
        rowDraw->AddRectFilled(ImVec2{keysLeft, graphTop}, ImVec2{graphRight, graphBottom}, IM_COL32(22, 25, 31, 255));
        std::string track = m_selectedSkeletonBone;
        const std::vector<Asset::AnimationAssetSampleUVE>* samples = FindClipTrackSamplesUVE(*clip, track);
        if (samples == nullptr && !clip->samples.empty()) {
            track.clear();
            samples = &clip->samples;
        }
        if (samples == nullptr || samples->empty()) {
            const char* hint = "Pick a bone on the left to see its curves.";
            const ImVec2 size = ImGui::CalcTextSize(hint);
            rowDraw->AddText(ImVec2{keysLeft + (keysWidth - size.x) * 0.5F, (graphTop + graphBottom - size.y) * 0.5F},
                             ImGui::GetColorU32(ImGuiCol_TextDisabled), hint);
        } else {
            // Which channels: the one picked on the left, else every one that moves.
            std::vector<int> channels;
            for (int channel = 0; channel < 3; ++channel) {
                bool moves = false;
                for (std::size_t i = 1U; i < samples->size() && !moves; ++i) {
                    moves = ChannelChangesUVE((*samples)[i - 1U].pose, (*samples)[i].pose, channel);
                }
                if (m_timeline.curveChannel == channel || (m_timeline.curveChannel < 0 && moves)) {
                    channels.push_back(channel);
                }
            }
            if (channels.empty()) {
                channels.push_back(0);
            }
            // Each line's values; rotation angles are unwrapped so a turn past 180 degrees stays one line.
            struct LineUVE {
                int channel = 0;
                int axis = 0;
                std::vector<float> values;
                std::vector<float> unwrap;
            };
            std::vector<LineUVE> lines;
            // Each channel has its own range; with more than one channel shown they are drawn
            // normalized, each filling the height, since metres and degrees share no scale.
            float low[3] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                            std::numeric_limits<float>::max()};
            float high[3] = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                             std::numeric_limits<float>::lowest()};
            for (const int channel : channels) {
                for (int axis = 0; axis < 3; ++axis) {
                    LineUVE line{channel, axis, {}, {}};
                    float offset = 0.0F;
                    for (std::size_t i = 0U; i < samples->size(); ++i) {
                        const float raw = GetClipPoseComponentUVE((*samples)[i].pose, channel, axis);
                        if (channel == 1 && i > 0U) {
                            while (raw + offset - line.values.back() > 180.0F) {
                                offset -= 360.0F;
                            }
                            while (raw + offset - line.values.back() < -180.0F) {
                                offset += 360.0F;
                            }
                        }
                        line.values.push_back(raw + offset);
                        line.unwrap.push_back(offset);
                        low[channel] = std::min(low[channel], raw + offset);
                        high[channel] = std::max(high[channel], raw + offset);
                    }
                    lines.push_back(std::move(line));
                }
            }
            for (const int channel : channels) {
                if (high[channel] - low[channel] < 1.0e-3F) {
                    low[channel] -= 1.0F;
                    high[channel] += 1.0F;
                }
                const float pad = (high[channel] - low[channel]) * 0.08F;
                low[channel] -= pad;
                high[channel] += pad;
            }
            if (m_timeline.curveDragging) {
                low[m_timeline.curveComponent / 3] = m_timeline.curveRangeMin;
                high[m_timeline.curveComponent / 3] = m_timeline.curveRangeMax;
            }
            const bool normalized = channels.size() > 1U;
            const float plotTop = graphTop + 22.0F;
            const float plotBottom = graphBottom - 8.0F;
            const auto toY = [&](const float value, const int channel) {
                return plotBottom - (value - low[channel]) / (high[channel] - low[channel]) * (plotBottom - plotTop);
            };
            const auto fromY = [&](const float y, const int channel) {
                return low[channel] + (plotBottom - y) / (plotBottom - plotTop) * (high[channel] - low[channel]);
            };
            // Value grid with labels, for a single channel; normalized lines get plain guides.
            if (normalized) {
                for (int line = 0; line <= 4; ++line) {
                    const float y = plotTop + (plotBottom - plotTop) * static_cast<float>(line) / 4.0F;
                    rowDraw->AddLine(ImVec2{keysLeft, y}, ImVec2{graphRight, y}, kGridLineUVE);
                }
            } else {
                const int only = channels.front();
                const float rawStep = (high[only] - low[only]) / 4.0F;
                const float magnitude = std::pow(10.0F, std::floor(std::log10(std::max(rawStep, 1.0e-6F))));
                float step = magnitude;
                for (const float factor : {1.0F, 2.0F, 5.0F, 10.0F}) {
                    if (magnitude * factor >= rawStep) {
                        step = magnitude * factor;
                        break;
                    }
                }
                for (float value = std::ceil(low[only] / step) * step; value <= high[only]; value += step) {
                    const float y = toY(value, only);
                    rowDraw->AddLine(ImVec2{keysLeft, y}, ImVec2{graphRight, y},
                                     std::abs(value) < step * 0.01F ? IM_COL32(255, 255, 255, 40) : kGridLineUVE);
                    char label[32];
                    std::snprintf(label, sizeof(label), step >= 1.0F ? "%.0f" : (step >= 0.1F ? "%.1f" : "%.2f"),
                                  static_cast<double>(value));
                    rowDraw->AddText(ImVec2{keysLeft + 4.0F, y - ImGui::GetTextLineHeight() - 1.0F}, kTickMinorUVE,
                                     label);
                }
            }
            // The lines: X light, Y the channel's colour, Z dark.
            const auto shade = [](const ImU32 colour, const int axis) {
                const float factor = axis == 0 ? 1.35F : (axis == 1 ? 1.0F : 0.7F);
                const auto part = [&](const int shift) {
                    const float value = static_cast<float>((colour >> shift) & 0xFFU) * factor;
                    return static_cast<ImU32>(std::clamp(value, 0.0F, 255.0F)) << shift;
                };
                return part(IM_COL32_R_SHIFT) | part(IM_COL32_G_SHIFT) | part(IM_COL32_B_SHIFT) | (0xFFU << IM_COL32_A_SHIFT);
            };
            const bool showKeys = samples->size() < 2U ||
                                  ((*samples)[1].timeSeconds - (*samples)[0].timeSeconds) * scale >= 6.0;
            std::vector<ImVec2> points;
            for (const LineUVE& line : lines) {
                const ImU32 colour = shade(kChannelColourUVE[line.channel], line.axis);
                points.clear();
                for (std::size_t i = 0U; i < samples->size(); ++i) {
                    points.emplace_back(toX((*samples)[i].timeSeconds), toY(line.values[i], line.channel));
                }
                rowDraw->AddPolyline(points.data(), static_cast<int>(points.size()), colour, ImDrawFlags_None, 1.5F);
                if (showKeys) {
                    for (const ImVec2& point : points) {
                        rowDraw->AddCircleFilled(point, 2.5F, colour);
                    }
                }
            }
            // Legend: which channel, which axis.
            float legendX = keysLeft + 8.0F;
            for (const int channel : channels) {
                rowDraw->AddText(ImVec2{legendX, graphTop + 3.0F}, kChannelColourUVE[channel], kChannelNameUVE[channel]);
                legendX += ImGui::CalcTextSize(kChannelNameUVE[channel]).x + 6.0F;
                for (int axis = 0; axis < 3; ++axis) {
                    const char* name = axis == 0 ? "X" : (axis == 1 ? "Y" : "Z");
                    rowDraw->AddText(ImVec2{legendX, graphTop + 3.0F}, shade(kChannelColourUVE[channel], axis), name);
                    legendX += ImGui::CalcTextSize(name).x + 4.0F;
                }
                legendX += 14.0F;
            }
            const std::string caption = (track.empty() ? std::string{"Transform"} : track) +
                                        (normalized ? "  (each channel scaled to fit; open a channel row for values)" : "");
            rowDraw->AddText(ImVec2{legendX, graphTop + 3.0F}, ImGui::GetColorU32(ImGuiCol_TextDisabled),
                             caption.c_str());

            // Drag a point up or down to change that number of that key.
            const ImVec2 mousePos = ImGui::GetIO().MousePos;
            ImGui::SetCursorScreenPos(ImVec2{keysLeft, plotTop});
            ImGui::InvisibleButton("##curves", ImVec2{keysWidth, std::max(1.0F, plotBottom - plotTop)});
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                float best = 8.0F;
                for (const LineUVE& line : lines) {
                    for (std::size_t i = 0U; i < samples->size(); ++i) {
                        const float dx = toX((*samples)[i].timeSeconds) - mousePos.x;
                        const float dy = toY(line.values[i], line.channel) - mousePos.y;
                        const float distance = std::sqrt(dx * dx + dy * dy);
                        if (distance < best) {
                            best = distance;
                            m_timeline.curveDragging = true;
                            m_timeline.curveComponent = line.channel * 3 + line.axis;
                            m_timeline.curveKeySeconds = (*samples)[i].timeSeconds;
                            m_timeline.curveUnwrap = line.unwrap[i];
                        }
                    }
                }
                if (m_timeline.curveDragging) {
                    m_timeline.curveRangeMin = low[m_timeline.curveComponent / 3];
                    m_timeline.curveRangeMax = high[m_timeline.curveComponent / 3];
                    m_timeline.playing = false;
                    m_timeline.timeSeconds = m_timeline.curveKeySeconds;
                }
            }
            if (m_timeline.curveDragging) {
                const int channel = m_timeline.curveComponent / 3;
                const int axis = m_timeline.curveComponent % 3;
                const float value = fromY(std::clamp(mousePos.y, plotTop, plotBottom), channel);
                const ImVec2 at{toX(m_timeline.curveKeySeconds), toY(value, channel)};
                rowDraw->AddCircle(at, 5.0F, IM_COL32_WHITE, 0, 1.5F);
                char label[48];
                std::snprintf(label, sizeof(label), "%s %s  %.3f", kChannelNameUVE[channel],
                              axis == 0 ? "X" : (axis == 1 ? "Y" : "Z"), static_cast<double>(value));
                rowDraw->AddText(ImVec2{at.x + 8.0F, at.y - 8.0F}, IM_COL32_WHITE, label);
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    m_timeline.curveDragging = false;
                    const double keySeconds = m_timeline.curveKeySeconds;
                    const float stored = value - m_timeline.curveUnwrap;
                    edit([&](Asset::AnimationClipAssetUVE& next) {
                        static_cast<void>(SetClipKeyComponentUVE(next, track, keySeconds, channel, axis, stored));
                    });
                }
            }
        }
        rowDraw->PopClipRect();
    }
    // Clicks in the key area: on a key, select it (Shift adds, Ctrl toggles) and start dragging the
    // selection; on empty space, start a selection box.
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool shift = ImGui::GetIO().KeyShift;
    const bool ctrl = ImGui::GetIO().KeyCtrl;
    if (clickedRow >= 0) {
        const RowUVE& row = rows[static_cast<std::size_t>(clickedRow)];
        std::optional<double> hit;
        float best = 7.0F;
        if (row.samples != nullptr) {
            for (std::size_t i = 0U; i < row.samples->size(); ++i) {
                const float distance = std::abs(toX((*row.samples)[i].timeSeconds) - mouse.x);
                if (distance < best && ShowsKeyUVE(*row.samples, i, row.channel)) {
                    best = distance;
                    hit = (*row.samples)[i].timeSeconds;
                }
            }
        }
        if (hit.has_value()) {
            const ClipKeyUVE key{row.track, *hit};
            const bool already = isSelected(key.track, key.timeSeconds);
            if (ctrl && already) {
                std::erase(m_timeline.selectedKeys, key);
            } else {
                if (!shift && !ctrl && !already) {
                    m_timeline.selectedKeys.clear();
                }
                if (!already) {
                    m_timeline.selectedKeys.push_back(key);
                }
                m_timeline.draggingKeys = true;
                m_timeline.dragFromX = mouse.x;
            }
        } else {
            if (!shift && !ctrl) {
                m_timeline.selectedKeys.clear();
            }
            m_timeline.boxSelecting = true;
            m_timeline.boxFromX = mouse.x;
            m_timeline.boxFromY = mouse.y;
        }
    }
    if (m_timeline.draggingKeys && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_timeline.draggingKeys = false;
        if (std::abs(dragDelta) > kClipKeyTimeToleranceUVE) {
            std::vector<ClipKeyUVE> landed;
            edit([&](Asset::AnimationClipAssetUVE& next) {
                landed = MoveClipKeysUVE(next, m_timeline.selectedKeys, dragDelta, frameRate);
            });
            m_timeline.selectedKeys = std::move(landed);
        }
    }
    if (m_timeline.draggingEvent && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_timeline.draggingEvent = false;
        const double delta = std::round(static_cast<double>(mouse.x - m_timeline.eventDragFromX) / scale * frameRate) /
                             frameRate;
        const int chosen = m_timeline.selectedEvent;
        if (chosen >= 0 && static_cast<std::size_t>(chosen) < clip->events.size() &&
            std::abs(delta) > kClipKeyTimeToleranceUVE) {
            const double to = clip->events[static_cast<std::size_t>(chosen)].timeSeconds + delta;
            std::optional<std::size_t> landed;
            edit([&](Asset::AnimationClipAssetUVE& next) {
                landed = MoveClipEventUVE(next, static_cast<std::size_t>(chosen), to, frameRate);
            });
            m_timeline.selectedEvent = landed.has_value() ? static_cast<int>(*landed) : -1;
        }
    }
    // Naming an event: the field opens on the event, Enter keeps the name, Escape leaves it.
    if (m_timeline.renameEventRequested && m_timeline.selectedEvent >= 0 &&
        static_cast<std::size_t>(m_timeline.selectedEvent) < m_timeline.clip->events.size()) {
        m_timeline.renameEventRequested = false;
        m_timeline.eventName = m_timeline.clip->events[static_cast<std::size_t>(m_timeline.selectedEvent)].eventId;
        ImGui::OpenPopup("##tl-event-name");
    }
    if (ImGui::BeginPopup("##tl-event-name")) {
        ImGui::TextDisabled("Event name");
        char buffer[kEventNameBytesUVE + 1];
        std::snprintf(buffer, sizeof(buffer), "%s", m_timeline.eventName.c_str());
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(220.0F);
        const bool entered = ImGui::InputText("##tl-event-name-field", buffer, sizeof(buffer),
                                              ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        m_timeline.eventName = buffer;
        ImGui::TextDisabled("Scripts get it as: on animation_event(name)");
        if (entered) {
            const int chosen = m_timeline.selectedEvent;
            const std::string name = m_timeline.eventName;
            if (chosen >= 0 && !name.empty()) {
                edit([&](Asset::AnimationClipAssetUVE& next) {
                    static_cast<void>(RenameClipEventUVE(next, static_cast<std::size_t>(chosen), name));
                });
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (m_timeline.boxSelecting) {
        const float left = std::min(m_timeline.boxFromX, mouse.x);
        const float right = std::max(m_timeline.boxFromX, mouse.x);
        const float top = std::min(m_timeline.boxFromY, mouse.y);
        const float bottom = std::max(m_timeline.boxFromY, mouse.y);
        rowDraw->AddRectFilled(ImVec2{left, top}, ImVec2{right, bottom}, IM_COL32(120, 170, 255, 40));
        rowDraw->AddRect(ImVec2{left, top}, ImVec2{right, bottom}, IM_COL32(120, 170, 255, 200));
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            m_timeline.boxSelecting = false;
            for (std::size_t index = 0U; index < rows.size(); ++index) {
                const RowUVE& row = rows[index];
                const float rowTop = rowsOrigin.y + static_cast<float>(index) * kRowHeightUVE;
                if (row.events || row.samples == nullptr || rowTop + kRowHeightUVE < top || rowTop > bottom) {
                    continue;
                }
                for (std::size_t i = 0U; i < row.samples->size(); ++i) {
                    const double t = (*row.samples)[i].timeSeconds;
                    const float x = toX(t);
                    if (x >= left && x <= right && ShowsKeyUVE(*row.samples, i, row.channel) &&
                        !isSelected(row.track, t)) {
                        m_timeline.selectedKeys.push_back(ClipKeyUVE{row.track, t});
                    }
                }
            }
        }
    }
    ImGui::EndChild();

    const float wheel = ImGui::GetIO().MouseWheel;
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && wheel != 0.0F) {
        if (ImGui::GetIO().KeyCtrl) {
            const double anchor = toSeconds(mouse.x);
            const double next = std::clamp(scale * (wheel > 0.0F ? 1.25 : 0.8), fitScale, 4000.0);
            m_timeline.pixelsPerSecond = next <= fitScale * 1.001 ? 0.0F : static_cast<float>(next);
            m_timeline.scrollSeconds = anchor - static_cast<double>(mouse.x - keysLeft) / next;
        } else if (ImGui::GetIO().KeyShift) {
            m_timeline.scrollSeconds -= static_cast<double>(wheel) * visibleSeconds * 0.1;
        }
    }

    // Playhead over ruler and rows, with the frame it is on.
    const float playheadX = toX(m_timeline.timeSeconds);
    if (playheadX >= keysLeft - 1.0F && playheadX <= keysLeft + keysWidth + 1.0F) {
        ImDrawList* const top = ImGui::GetWindowDrawList();
        top->PushClipRect(ImVec2{keysLeft - 8.0F, origin.y}, ImVec2{keysLeft + keysWidth + 8.0F, rowsBottom}, false);
        top->AddLine(ImVec2{playheadX, origin.y}, ImVec2{playheadX, rowsBottom}, kPlayheadUVE, 1.5F);
        char label[16];
        std::snprintf(label, sizeof(label), "%ld", frameOf(m_timeline.timeSeconds));
        const ImVec2 size = ImGui::CalcTextSize(label);
        top->AddRectFilled(ImVec2{playheadX - size.x * 0.5F - 4.0F, origin.y + 1.0F},
                           ImVec2{playheadX + size.x * 0.5F + 4.0F, origin.y + size.y + 3.0F}, kPlayheadUVE, 3.0F);
        top->AddText(ImVec2{playheadX - size.x * 0.5F, origin.y + 2.0F}, IM_COL32_WHITE, label);
        top->PopClipRect();
    }
    // Past the clip's end, when zoomed out beyond it.
    const float endX = toX(duration);
    if (endX < keysLeft + keysWidth) {
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2{endX, origin.y}, ImVec2{keysLeft + keysWidth, rowsBottom},
                                                  kOutOfRangeUVE);
    }

    // Keys while the Timeline has focus.
    // The keys belong to the Timeline while its tracks have focus (a click on a key or a row).
    m_timelineOwnsKeys = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::GetIO().WantTextInput;
    if (m_timelineOwnsKeys) {
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
            m_timeline.playing = !m_timeline.playing;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            m_timeline.playing = false;
            m_timeline.timeSeconds = std::max(0.0, (static_cast<double>(frameOf(m_timeline.timeSeconds)) - 1.0) * frame);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            m_timeline.playing = false;
            m_timeline.timeSeconds = std::min(duration, (static_cast<double>(frameOf(m_timeline.timeSeconds)) + 1.0) * frame);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
            m_timeline.timeSeconds = 0.0;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_End, false)) {
            m_timeline.timeSeconds = duration;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            m_timeline.selectedKeys.clear();
        }
        if (m_timeline.selectedEvent >= 0 &&
            (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))) {
            const int chosen = m_timeline.selectedEvent;
            edit([&](Asset::AnimationClipAssetUVE& next) {
                static_cast<void>(RemoveClipEventUVE(next, static_cast<std::size_t>(chosen)));
            });
            m_timeline.selectedEvent = -1;
            m_timeline.status = "Deleted an event";
        } else if (!m_timeline.selectedKeys.empty() &&
            (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))) {
            std::size_t removed = 0U;
            edit([&](Asset::AnimationClipAssetUVE& next) { removed = DeleteClipKeysUVE(next, m_timeline.selectedKeys); });
            m_timeline.status = "Deleted " + std::to_string(removed) + " key" + (removed == 1U ? "" : "s") +
                                (removed < m_timeline.selectedKeys.size() ? " (a track keeps its last key)" : "");
            m_timeline.selectedKeys.clear();
        }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && !m_timeline.selectedKeys.empty()) {
            m_timeline.clipboard = CopyClipKeysUVE(*m_timeline.clip, m_timeline.selectedKeys);
            m_timeline.status = "Copied " + std::to_string(m_timeline.clipboard.entries.size()) + " keys";
        }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && !m_timeline.clipboard.IsEmptyUVE()) {
            std::vector<ClipKeyUVE> pasted;
            edit([&](Asset::AnimationClipAssetUVE& next) {
                pasted = PasteClipKeysUVE(next, m_timeline.clipboard, m_timeline.timeSeconds, frameRate);
            });
            m_timeline.selectedKeys = std::move(pasted);
        }
        if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_K, false)) {
            insertKey();
        }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            if (shift) {
                redoEdit();
            } else {
                undoEdit();
            }
        }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
            redoEdit();
        }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            saveClip();
        }
    }
    ImGui::EndChild();

    // ---- Bottom bar: the view, what the colours mean, the clip, and the zoom ------------------------
    ImGui::Separator();
    {
        const auto viewButton = [this](const char* label, const bool curves) {
            const bool active = m_timeline.curves == curves;
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            }
            if (ImGui::Button(label)) {
                m_timeline.curves = curves;
                m_timeline.curveDragging = false;
            }
            if (active) {
                ImGui::PopStyleColor();
            }
        };
        viewButton("Dope Sheet##tl-view", false);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Keys per bone and channel");
        }
        ImGui::SameLine(0.0F, 2.0F);
        viewButton("Curves##tl-view", true);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("The selected bone's values over time; drag a point to change it");
        }
        ImGui::SameLine(0.0F, 18.0F);
    }
    for (int channel = 0; channel < 3; ++channel) {
        if (channel > 0) {
            ImGui::SameLine(0.0F, 12.0F);
        }
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float size = ImGui::GetTextLineHeight() * 0.6F;
        const float top = at.y + (ImGui::GetFrameHeight() - size) * 0.5F;
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2{at.x, top}, ImVec2{at.x + size, top + size},
                                                  kChannelColourUVE[channel], 2.0F);
        ImGui::Dummy(ImVec2{size + 4.0F, ImGui::GetFrameHeight()});
        ImGui::SameLine(0.0F, 0.0F);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", kChannelNameUVE[channel]);
    }
    ImGui::SameLine(0.0F, 24.0F);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", clipLabel.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%.2f s  |  %ld frames  |  %.0f fps  |  %zu tracks", clip->durationSeconds, frameOf(duration),
                        frameRate, clip->bones.size());
    if (!m_timeline.selectedKeys.empty() || !m_timeline.status.empty()) {
        ImGui::SameLine(0.0F, 16.0F);
        if (m_timeline.status.empty()) {
            ImGui::TextDisabled("%zu selected", m_timeline.selectedKeys.size());
        } else {
            ImGui::TextDisabled("%s", m_timeline.status.c_str());
        }
    }
    const float zoomWidth = 140.0F;
    const float fitWidth = ImGui::CalcTextSize("Fit").x + ImGui::GetStyle().FramePadding.x * 2.0F;
    const float rightStart = ImGui::GetWindowContentRegionMax().x - zoomWidth - fitWidth - ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine(std::max(rightStart, ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + 16.0F));
    if (ImGui::Button("Fit##tl-fit")) {
        m_timeline.pixelsPerSecond = 0.0F;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Show the whole clip");
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(zoomWidth);
    float zoom = m_timeline.pixelsPerSecond > 0.0F ? m_timeline.pixelsPerSecond : static_cast<float>(fitScale);
    if (ImGui::SliderFloat("##tl-zoom", &zoom, static_cast<float>(fitScale), 4000.0F,
                           m_timeline.pixelsPerSecond <= 0.0F ? "Zoom: fit" : "Zoom: %.0f px/s",
                           ImGuiSliderFlags_Logarithmic)) {
        m_timeline.pixelsPerSecond = zoom <= static_cast<float>(fitScale) * 1.001F ? 0.0F : zoom;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Zoom. Ctrl + wheel over the tracks zooms around the mouse; Shift + wheel pans.");
    }
    AcceptTimelineClipDropUVE(playerEntity);
}

void EditorUVE::AcceptTimelineClipDropUVE(const Scene::EntityUVE player) {
    // A .uvanim dropped anywhere on the Timeline joins the player's list and starts playing.
    const ImGuiWindow* const window = ImGui::GetCurrentWindow();
    if (window == nullptr || !ImGui::BeginDragDropTargetCustom(window->Rect(), window->ID)) {
        return;
    }
    if (const ImGuiPayload* const payload = ImGui::AcceptDragDropPayload(kContentItemPayloadUVE);
        payload != nullptr && payload->DataSize > 1) {
        const std::filesystem::path relative{
            std::string(static_cast<const char*>(payload->Data), static_cast<std::size_t>(payload->DataSize - 1))};
        if (relative.extension() == ".uvanim") {
            const std::filesystem::path root = m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot;
            static_cast<void>(AddClipToAnimationPlayerUVE(player, root / relative));
        } else {
            m_timeline.status = "Only .uvanim clips can be dropped on the Timeline";
        }
    }
    ImGui::EndDragDropTarget();
}

} // namespace UVE::Editor
