// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Entity Editor's Timeline tab: the clip of the entity's AnimationPlayer as a dope sheet - one
// row per bone with its keys - under a ruler with a playhead, and a transport that previews the
// clip on the entity's skeleton while the tab is open. The preview writes the skeleton's runtime
// pose only, so it never dirties the entity or reaches its file.

#include "uve/editor/editor_uve.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/component/animation_mixer_component_uve.h"
#include "uve/component/animation_player_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/nodes/3d/animation_player_uve.h"
#include "uve/nodes/3d/skeleton_3d_uve.h"

namespace UVE::Editor {
namespace {

constexpr float kTrackListWidthUVE = 220.0F;
constexpr float kRulerHeightUVE = 24.0F;
constexpr float kRowHeightUVE = 20.0F;

constexpr ImU32 kRulerBackgroundUVE = IM_COL32(24, 27, 33, 255);
constexpr ImU32 kRowEvenUVE = IM_COL32(30, 33, 40, 255);
constexpr ImU32 kRowOddUVE = IM_COL32(34, 38, 46, 255);
constexpr ImU32 kRowSelectedUVE = IM_COL32(44, 62, 92, 255);
constexpr ImU32 kTickMajorUVE = IM_COL32(120, 128, 140, 255);
constexpr ImU32 kTickMinorUVE = IM_COL32(70, 76, 86, 255);
constexpr ImU32 kGridLineUVE = IM_COL32(255, 255, 255, 14);
constexpr ImU32 kKeyUVE = IM_COL32(222, 190, 92, 255);
constexpr ImU32 kKeyBandUVE = IM_COL32(222, 190, 92, 90);
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
    return std::clamp(std::round(static_cast<double>(most - 1U) / clip.durationSeconds), 1.0, 240.0);
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

} // namespace

void EditorUVE::StopAnimationTimelinePreviewUVE() {
    m_timeline.playing = false;
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
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = m_services->GetSceneGraphUVE();
    const auto nameOf = [&entityManager](const Scene::EntityUVE entity) {
        return entityManager.HasComponentUVE<Scene::NameComponentUVE>(entity)
                   ? entityManager.GetComponentUVE<Scene::NameComponentUVE>(entity).name
                   : std::string{"AnimationPlayer"};
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
        StopAnimationTimelinePreviewUVE();
        m_timeline.clipGuid = player.clip;
        m_timeline.clip.reset();
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
            ImGui::SetTooltip("The AnimationPlayer this Timeline shows. Selecting a player in the tree picks it too.");
        }
        ImGui::SameLine();
    }

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
    char clock[96];
    std::snprintf(clock, sizeof(clock), "%.2f / %.2f s   frame %ld / %ld   %.0f fps", m_timeline.timeSeconds,
                  clip != nullptr ? clip->durationSeconds : 0.0, frameOf(m_timeline.timeSeconds), frameOf(duration),
                  frameRate);
    ImGui::TextUnformatted(clock);

    // Right side: which player and clip, zoom and the track filter.
    const std::string clipLabel = (players.size() > 1U ? std::string{} : nameOf(playerEntity) + "  >  ") +
                                  (clip != nullptr ? clip->clipId
                                                   : ((player.clip != Asset::AssetGuidUVE{}) ? std::string{"(unreadable clip)"}
                                                                               : std::string{"(no clip)"}));
    const float filterWidth = 150.0F;
    const float zoomWidth = 110.0F;
    const float rightWidth = ImGui::CalcTextSize(clipLabel.c_str()).x + filterWidth + zoomWidth + 40.0F;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 12.0F, ImGui::GetWindowContentRegionMax().x - rightWidth));
    ImGui::TextUnformatted(clipLabel.c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(zoomWidth);
    float zoom = m_timeline.pixelsPerSecond;
    ImGui::SliderFloat("##tl-zoom", &zoom, 0.0F, 2000.0F, zoom <= 0.0F ? "Fit" : "%.0f px/s",
                       ImGuiSliderFlags_Logarithmic);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Zoom. Far left fits the whole clip; Ctrl + wheel over the tracks zooms too.");
    }
    m_timeline.pixelsPerSecond = zoom < 10.0F ? 0.0F : zoom;
    ImGui::SameLine();
    char filter[128];
    std::snprintf(filter, sizeof(filter), "%s", m_timeline.filter.c_str());
    ImGui::SetNextItemWidth(filterWidth);
    if (ImGui::InputTextWithHint("##tl-filter", "Filter bones", filter, sizeof(filter))) {
        m_timeline.filter = filter;
    }

    if (clip == nullptr) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", !m_timeline.loadError.empty()
                                      ? m_timeline.loadError.c_str()
                                      : "This AnimationPlayer has no clip. Set its Clip in the Inspector.");
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
    struct RowUVE {
        std::string label;
        int depth = 0;
        const std::vector<Asset::AnimationAssetSampleUVE>* samples = nullptr;
        bool events = false;
    };
    std::vector<RowUVE> rows;
    if (!clip->events.empty()) {
        rows.push_back(RowUVE{"Events", 0, nullptr, true});
    }
    if (!clip->samples.empty()) {
        rows.push_back(RowUVE{"Transform", 0, &clip->samples, false});
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
                rows.push_back(RowUVE{bone.name, depth[index], &found->second->samples, false});
            }
        }
    } else {
        for (const Asset::AnimationAssetBoneTrackUVE& track : clip->bones) {
            if (matches(track.bone)) {
                rows.push_back(RowUVE{track.bone, 0, &track.samples, false});
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
    ImGui::BeginChild("##tl-body", ImVec2{0.0F, 0.0F}, false, ImGuiWindowFlags_NoScrollWithMouse);
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
    draw->AddText(ImVec2{origin.x + 8.0F, origin.y + 4.0F}, kTickMajorUVE,
                  skeleton != nullptr ? "Bones" : "Tracks");

    // Scrubbing: drag on the ruler or click in the key area moves the playhead.
    ImGui::SetCursorScreenPos(ImVec2{keysLeft, origin.y});
    ImGui::InvisibleButton("##tl-ruler", ImVec2{keysWidth, kRulerHeightUVE});
    if (ImGui::IsItemActive()) {
        m_timeline.playing = false;
        const double snapped = std::round(toSeconds(ImGui::GetIO().MousePos.x) * frameRate) / frameRate;
        m_timeline.timeSeconds = std::clamp(snapped, 0.0, duration);
    }

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
            const bool selected = !row.events && row.label == m_selectedSkeletonBone;
            const ImU32 background = selected ? kRowSelectedUVE : (index % 2 == 0 ? kRowEvenUVE : kRowOddUVE);
            rowDraw->AddRectFilled(ImVec2{origin.x, y}, ImVec2{origin.x + area.x, y + kRowHeightUVE}, background);
            ImGui::SetCursorScreenPos(ImVec2{origin.x, y});
            ImGui::PushID(index);
            if (ImGui::InvisibleButton("##row", ImVec2{kTrackListWidthUVE, kRowHeightUVE}) && !row.events) {
                m_selectedSkeletonBone = row.label;
            }
            ImGui::PopID();
            const float indent = 8.0F + static_cast<float>(std::min(row.depth, 24)) * 10.0F;
            rowDraw->PushClipRect(ImVec2{origin.x, y}, ImVec2{keysLeft - 6.0F, y + kRowHeightUVE}, true);
            rowDraw->AddText(ImVec2{origin.x + indent, y + 3.0F},
                             row.events ? kEventUVE : ImGui::GetColorU32(ImGuiCol_Text), row.label.c_str());
            rowDraw->PopClipRect();

            rowDraw->PushClipRect(ImVec2{keysLeft, y}, ImVec2{keysLeft + keysWidth, y + kRowHeightUVE}, true);
            const float mid = y + kRowHeightUVE * 0.5F;
            if (row.events) {
                for (const Asset::AnimationAssetEventUVE& event : clip->events) {
                    const float x = toX(event.timeSeconds);
                    rowDraw->AddTriangleFilled(ImVec2{x, mid - 6.0F}, ImVec2{x + 5.0F, mid + 5.0F},
                                               ImVec2{x - 5.0F, mid + 5.0F}, kEventUVE);
                }
            } else if (row.samples != nullptr && !row.samples->empty()) {
                const auto& samples = *row.samples;
                // A baked track has a key every frame: when keys would touch, draw the span as a band
                // with keys at its ends, like a dope sheet's held range.
                const double spacing = samples.size() > 1U
                                           ? (samples.back().timeSeconds - samples.front().timeSeconds) /
                                                 static_cast<double>(samples.size() - 1U)
                                           : duration;
                if (spacing * scale < 7.0 && samples.size() > 1U) {
                    rowDraw->AddRectFilled(ImVec2{toX(samples.front().timeSeconds), mid - 3.0F},
                                           ImVec2{toX(samples.back().timeSeconds), mid + 3.0F}, kKeyBandUVE, 2.0F);
                    for (const double t : {samples.front().timeSeconds, samples.back().timeSeconds}) {
                        const float x = toX(t);
                        rowDraw->AddQuadFilled(ImVec2{x, mid - 5.0F}, ImVec2{x + 5.0F, mid}, ImVec2{x, mid + 5.0F},
                                               ImVec2{x - 5.0F, mid}, kKeyUVE);
                    }
                } else {
                    for (const Asset::AnimationAssetSampleUVE& sample : samples) {
                        const float x = toX(sample.timeSeconds);
                        if (x < keysLeft - 6.0F || x > keysLeft + keysWidth + 6.0F) {
                            continue;
                        }
                        rowDraw->AddQuadFilled(ImVec2{x, mid - 5.0F}, ImVec2{x + 5.0F, mid}, ImVec2{x, mid + 5.0F},
                                               ImVec2{x - 5.0F, mid}, kKeyUVE);
                    }
                }
            }
            rowDraw->PopClipRect();
        }
    }
    clipper.End();
    ImGui::Dummy(ImVec2{1.0F, 0.0F});
    // Clicking in the key area scrubs too; Ctrl + wheel zooms around the mouse, Shift + wheel pans.
    const bool hovered = ImGui::IsWindowHovered();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (hovered && mouse.x >= keysLeft && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsAnyItemActive()) {
        m_timeline.playing = false;
        m_timeline.timeSeconds = std::clamp(std::round(toSeconds(mouse.x) * frameRate) / frameRate, 0.0, duration);
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
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
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
    }
    ImGui::EndChild();
}

} // namespace UVE::Editor
