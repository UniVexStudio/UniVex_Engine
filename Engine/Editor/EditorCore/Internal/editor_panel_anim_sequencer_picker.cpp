// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Inspector's AnimationSequencer picker for an AnimationGraph: a slot showing which sequencer's
// clips the graph offers, and the small list of every AnimationSequencer to pick one from.

#include "editor.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <imgui.h>

#include "animation_graph_component.h"
#include "animation_sequencer_component.h"
#include "name_component.h"

namespace UVE::Editor {
namespace {

[[nodiscard]] std::string LowerUVE(std::string text) {
    std::ranges::transform(text, text.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

} // namespace

void EditorUVE::DrawAnimationSequencerPickerSlotUVE(const char* const id, const Scene::EntityUVE treeEntity,
                                                   const Scene::EntityUVE currentSequencer,
                                                   const std::function<void(Scene::EntityUVE)>& onPicked) {
    (void)treeEntity;
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const bool isBound = currentSequencer != Scene::kInvalidEntityUVE && entityManager.IsAliveUVE(currentSequencer) &&
                         entityManager.HasComponentUVE<Scene::AnimationSequencerComponentUVE>(currentSequencer);
    std::string displayName = "Empty";
    if (isBound) {
        if (entityManager.HasComponentUVE<Scene::NameComponentUVE>(currentSequencer)) {
            displayName = entityManager.GetComponentUVE<Scene::NameComponentUVE>(currentSequencer).name;
        } else {
            displayName = "Sequencer #" + std::to_string(currentSequencer.index);
        }
    }

    ImGui::PushID(id);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Sequencer:");
    ImGui::SameLine(0.0F, 6.0F);

    const std::string label = displayName + "  v";
    if (!isBound) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55F, 0.55F, 0.58F, 1.0F));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35F, 0.85F, 1.0F, 1.0F));
    }

    const float btnWidth = std::clamp(ImGui::CalcTextSize(label.c_str()).x + 20.0F, 90.0F, 220.0F);
    if (ImGui::Button(label.c_str(), ImVec2{btnWidth, 0.0F})) {
        m_animGraph.openSequencerPickerModal = true;
    }
    ImGui::PopStyleColor();

    if (ImGui::IsItemHovered()) {
        if (isBound) {
            const std::vector<Asset::AssetGuidUVE> clips = GetSequencerClipsUVE(currentSequencer);
            ImGui::SetTooltip("Bound Animation Sequencer: %s\n%zu animation clips accessible.\nClick to change sequencer from Outliner.",
                              displayName.c_str(), clips.size());
        } else {
            ImGui::SetTooltip("No Animation Sequencer assigned.\nClick to pick an Animation Sequencer from the Outliner to access its clips.");
        }
    }

    if (isBound) {
        ImGui::SameLine(0.0F, 4.0F);
        if (ImGui::SmallButton("X##clear-seq")) {
            if (onPicked) {
                onPicked(Scene::kInvalidEntityUVE);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Clear / Unbind Animation Sequencer");
        }
    }

    ImGui::PopID();
}

void EditorUVE::DrawAnimationSequencerPickerModalUVE(const Scene::EntityUVE currentSequencer,
                                                    const std::function<void(Scene::EntityUVE)>& onPicked) {
    if (m_animGraph.openSequencerPickerModal) {
        ImGui::OpenPopup("Select Animation Sequencer##seq-picker-modal");
        m_animGraph.openSequencerPickerModal = false;
        m_animGraph.sequencerSearch.clear();
    }

    ImGui::SetNextWindowSize(ImVec2{460.0F, 340.0F}, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Select Animation Sequencer##seq-picker-modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();

        ImGui::TextUnformatted("Animation Sequencers in Outliner:");
        ImGui::TextDisabled("Select an Animation Sequencer so this Animation Graph can access its animation clips.");
        ImGui::Separator();

        // Search bar
        std::array<char, 64> searchBuffer{};
        std::strncpy(searchBuffer.data(), m_animGraph.sequencerSearch.c_str(), searchBuffer.size() - 1U);
        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::InputTextWithHint("##seq-search-input", "Search sequencer entity name...", searchBuffer.data(), searchBuffer.size())) {
            m_animGraph.sequencerSearch = searchBuffer.data();
        }
        const std::string needle = LowerUVE(m_animGraph.sequencerSearch);

        // Collect outliner entities with AnimationSequencerComponentUVE
        struct OutlinerSequencerEntryUVE {
            Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
            std::string name;
            std::size_t clipCount = 0;
            std::string activeClipName;
        };
        std::vector<OutlinerSequencerEntryUVE> entries;

        entityManager.ForEachUVE<Scene::AnimationSequencerComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::AnimationSequencerComponentUVE& comp) {
                if (!entityManager.IsAliveUVE(entity)) {
                    return;
                }
                OutlinerSequencerEntryUVE entry;
                entry.entity = entity;
                entry.name = entityManager.HasComponentUVE<Scene::NameComponentUVE>(entity)
                                 ? entityManager.GetComponentUVE<Scene::NameComponentUVE>(entity).name
                                 : ("Entity #" + std::to_string(entity.index));
                if (entry.name.empty()) {
                    entry.name = "Entity #" + std::to_string(entity.index);
                }
                const std::vector<Asset::AssetGuidUVE> clips = GetSequencerClipsUVE(entity);
                entry.clipCount = clips.size();
                if (comp.clip != Asset::AssetGuidUVE{}) {
                    const std::filesystem::path p = m_services->GetAssetDatabaseUVE().ResolveUVE(comp.clip);
                    entry.activeClipName = p.empty() ? "(clip)" : p.stem().string();
                }
                entries.push_back(std::move(entry));
            });

        // Filter
        if (!needle.empty()) {
            std::erase_if(entries, [&needle](const OutlinerSequencerEntryUVE& e) {
                return LowerUVE(e.name).find(needle) == std::string::npos;
            });
        }

        // List
        ImGui::BeginChild("##seq-picker-scroll", ImVec2{440.0F, 180.0F}, true);

        // Option 1: Empty / Unbind
        const bool isEmptySelected = (currentSequencer == Scene::kInvalidEntityUVE);
        if (ImGui::Selectable("  [ Empty ] - (None / Unbind)", isEmptySelected)) {
            if (onPicked) {
                onPicked(Scene::kInvalidEntityUVE);
            }
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Disconnect animation sequencer; graph operates without a linked sequencer.");
        }
        ImGui::Separator();

        if (entries.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("No AnimationSequencer found in the Scene Outliner.");
            ImGui::TextDisabled("Add an AnimationSequencerComponent to your character entity.");
        } else {
            for (const auto& item : entries) {
                const bool isCurrent = (item.entity == currentSequencer);
                std::string itemText = item.name + " (" + std::to_string(item.clipCount) +
                                       (item.clipCount == 1 ? " clip" : " clips") + ")";
                if (isCurrent) {
                    itemText += "  [Active]";
                }
                if (!item.activeClipName.empty()) {
                    itemText += "  [" + item.activeClipName + "]";
                }

                if (ImGui::Selectable(itemText.c_str(), isCurrent)) {
                    if (onPicked) {
                        onPicked(item.entity);
                    }
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered()) {
                    const std::vector<Asset::AssetGuidUVE> clips = GetSequencerClipsUVE(item.entity);
                    std::string tip = "Entity ID: " + std::to_string(item.entity.index) + "\nClips in library:";
                    for (const auto& c : clips) {
                        const std::filesystem::path cp = m_services->GetAssetDatabaseUVE().ResolveUVE(c);
                        tip += "\n  - " + (cp.empty() ? "(clip)" : cp.stem().string());
                    }
                    ImGui::SetTooltip("%s", tip.c_str());
                }
            }
        }

        ImGui::EndChild();

        ImGui::Separator();
        if (ImGui::Button("Cancel", ImVec2{100.0F, 0.0F}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

} // namespace UVE::Editor
