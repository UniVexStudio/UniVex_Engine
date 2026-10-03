// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// AnimationGraph's Inspector blocks: the parameter table and the graph itself. The graph is shown
// as the tree it is - Output at the top, each object's inputs indented under it - with objects nothing
// uses yet listed after, so a graph can be built piece by piece and wired up as it grows.

#include "uve/editor/editor_uve.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <imgui.h>

#include "editor_animation_graph_widgets_uve.h"

#include "uve/component/animation_tree_component_uve.h"
#include "uve/editor/animation_graph_editing_uve.h"

namespace UVE::Editor {
namespace {

using Kind = Scene::AnimationGraphObjectKindUVE;
using Scene::AnimationGraphObjectUVE;
using Scene::AnimationParameterTypeUVE;
using Scene::AnimationParameterUVE;
using Scene::AnimationTransitionUVE;

constexpr std::array<Kind, 11> kAddableKindsUVE{Kind::Clip,         Kind::Blend2,       Kind::BlendSpace1D, Kind::BlendSpace2D,
                                                Kind::Select,       Kind::Additive,     Kind::LayeredBlend, Kind::OneShot,
                                                Kind::TimeScale,    Kind::TimeSeek,     Kind::StateMachine};

[[nodiscard]] const char* KindLabelUVE(const Kind kind) noexcept { return AnimationGraphKindLabelUVE(kind); }
[[nodiscard]] const char* KindHelpUVE(const Kind kind) noexcept { return AnimationGraphKindHelpUVE(kind); }
[[nodiscard]] std::string SlotLabelUVE(const Kind kind, const std::size_t slot) {
    return AnimationGraphSlotLabelUVE(kind, slot);
}

[[nodiscard]] std::string ObjectLabelUVE(const AnimationGraphObjectUVE& object) {
    const std::string name = object.name.empty() ? std::string{KindLabelUVE(object.kind)} : object.name;
    return name + "  #" + std::to_string(object.id);
}

/// A text field committed when editing ends, so a rename is one undo step, not one per letter.
[[nodiscard]] bool EditTextUVE(const char* const id, const std::string& value, std::string& outValue) {
    std::array<char, 129> buffer{};
    std::strncpy(buffer.data(), value.c_str(), buffer.size() - 1U);
    ImGui::InputText(id, buffer.data(), buffer.size());
    if (ImGui::IsItemDeactivatedAfterEdit() && value != buffer.data()) {
        outValue = buffer.data();
        return true;
    }
    return false;
}


/// A labelled row: the label in the left column, the widget filling the right.
void RowUVE(const char* const label) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

} // namespace

void EditorUVE::DrawAnimationParametersPropertyUVE(const Core::TypeMetadataEntryUVE& entry,
                                                   const Core::TypeMetadataPropertyUVE& property,
                                                   const void* const instance) {
    const auto& tree = *static_cast<const Scene::AnimationTreeComponentUVE*>(instance);
    const bool writable = IsAuthoringCommandAllowedUVE();
    ImGui::BeginDisabled(!writable);
    std::vector<AnimationParameterUVE> parameters = tree.parameters;
    std::optional<std::size_t> removeIndex;
    bool changed = false;
    bool continuous = false;
    if (!parameters.empty() &&
        ImGui::BeginTable("##parameters", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthStretch, 0.40F);
        ImGui::TableSetupColumn("##type", ImGuiTableColumnFlags_WidthStretch, 0.26F);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 0.30F);
        ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        for (std::size_t index = 0U; index < parameters.size(); ++index) {
            AnimationParameterUVE& parameter = parameters[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::SetNextItemWidth(-FLT_MIN);
            std::string renamed;
            if (EditTextUVE("##name", parameter.name, renamed) && !renamed.empty()) {
                // Objects and transitions reading the old name follow it, as a second step.
                RenameAnimationParameterReferencesUVE(parameter.name, renamed);
                parameter.name = renamed;
                changed = true;
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            int type = static_cast<int>(parameter.type);
            if (ImGui::Combo("##type", &type, "Float\0Bool\0Trigger\0")) {
                parameter.type = static_cast<AnimationParameterTypeUVE>(type);
                parameter.value = 0.0F;
                changed = true;
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (parameter.type == AnimationParameterTypeUVE::Float) {
                if (ImGui::DragFloat("##value", &parameter.value, 0.01F)) {
                    continuous = true;
                }
                if (ImGui::IsItemDeactivated()) {
                    static_cast<void>(CommitComponentPropertyPreviewForUVE(entry, property));
                }
            } else if (parameter.type == AnimationParameterTypeUVE::Bool) {
                bool on = parameter.value >= 0.5F;
                if (ImGui::Checkbox("##value", &on)) {
                    parameter.value = on ? 1.0F : 0.0F;
                    changed = true;
                }
            } else if (ImGui::Button(parameter.value >= 0.5F ? "Armed" : "Fire", ImVec2(-FLT_MIN, 0.0F))) {
                parameter.value = 1.0F;
                changed = true;
            }
            ImGui::TableSetColumnIndex(3);
            if (ImGui::Button("x", ImVec2(ImGui::GetFrameHeight(), 0.0F))) {
                removeIndex = index;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Remove %s", parameter.name.c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    } else if (parameters.empty()) {
        ImGui::TextDisabled("No parameters. Add one for an object or transition to read.");
    }
    if (removeIndex.has_value()) {
        parameters.erase(parameters.begin() + static_cast<std::ptrdiff_t>(*removeIndex));
        changed = true;
    }
    if (ImGui::Button("+ Parameter")) {
        std::string name = "param";
        for (int suffix = 1; std::any_of(parameters.begin(), parameters.end(),
                                         [&name](const AnimationParameterUVE& p) { return p.name == name; });
             ++suffix) {
            name = "param" + std::to_string(suffix);
        }
        parameters.push_back(AnimationParameterUVE{name, AnimationParameterTypeUVE::Float, 0.0F});
        changed = true;
    }
    if (continuous) {
        static_cast<void>(PreviewSelectedComponentPropertyUVE(entry, property, &parameters));
    } else if (changed) {
        static_cast<void>(SetSelectedComponentPropertyUVE(entry, property, &parameters));
    }
    ImGui::EndDisabled();
}

void EditorUVE::RenameAnimationParameterReferencesUVE(const std::string& from, const std::string& to) {
    m_pendingAnimationParameterRename = std::make_pair(from, to);
}

void EditorUVE::DrawAnimationGraphPropertyUVE(const Core::TypeMetadataEntryUVE& entry,
                                              const Core::TypeMetadataPropertyUVE& property,
                                              const void* const instance) {
    const auto& tree = *static_cast<const Scene::AnimationTreeComponentUVE*>(instance);
    const bool writable = IsAuthoringCommandAllowedUVE();
    std::vector<AnimationGraphObjectUVE> objects = tree.objects;
    bool changed = false;
    bool continuous = false;

    // A parameter renamed in the table above: the objects and transitions that read it follow.
    if (m_pendingAnimationParameterRename.has_value()) {
        const auto [from, to] = *m_pendingAnimationParameterRename;
        m_pendingAnimationParameterRename.reset();
        for (AnimationGraphObjectUVE& object : objects) {
            for (std::string* const reads : {&object.parameter, &object.parameterY}) {
                if (*reads == from) {
                    *reads = to;
                    changed = true;
                }
            }
            for (AnimationTransitionUVE& transition : object.transitions) {
                for (Scene::AnimationTransitionConditionUVE& test : transition.conditions) {
                    if (test.parameter == from) {
                        test.parameter = to;
                        changed = true;
                    }
                }
            }
        }
    }

    const std::string problem = Scene::DescribeAnimationGraphProblemUVE(tree);
    if (!problem.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95F, 0.55F, 0.45F, 1.0F));
        ImGui::TextWrapped("%s", problem.c_str());
        ImGui::PopStyleColor();
    }

    std::unordered_map<std::uint32_t, std::size_t> indexById;
    std::unordered_set<std::uint32_t> used;
    for (std::size_t index = 0U; index < objects.size(); ++index) {
        indexById.emplace(objects[index].id, index);
        for (const std::uint32_t input : objects[index].inputs) {
            used.insert(input);
        }
    }

    // Tree order from the Output, then the loose objects, each with its depth for the indent.
    std::vector<std::pair<std::size_t, int>> order;
    std::unordered_set<std::size_t> listed;
    const std::function<void(std::size_t, int)> visit = [&](const std::size_t index, const int depth) {
        if (!listed.insert(index).second) {
            return;
        }
        order.emplace_back(index, depth);
        for (const std::uint32_t input : objects[index].inputs) {
            const auto child = indexById.find(input);
            if (child != indexById.end()) {
                visit(child->second, depth + 1);
            }
        }
    };
    for (std::size_t index = 0U; index < objects.size(); ++index) {
        if (objects[index].kind == Kind::Output) {
            visit(index, 0);
        }
    }
    const std::size_t connectedCount = order.size();
    for (std::size_t index = 0U; index < objects.size(); ++index) {
        if (!listed.contains(index) && !used.contains(objects[index].id)) {
            visit(index, 0);
        }
    }

    std::optional<std::uint32_t> removeId;
    ImGui::BeginDisabled(!writable);
    for (std::size_t row = 0U; row < order.size(); ++row) {
        if (row == connectedCount) {
            ImGui::Spacing();
            ImGui::TextDisabled("Not connected");
        }
        const auto [index, depth] = order[row];
        AnimationGraphObjectUVE& object = objects[index];
        ImGui::PushID(static_cast<int>(object.id));
        ImGui::Indent(static_cast<float>(depth) * 12.0F + 0.001F);
        const bool open = ImGui::TreeNodeEx("##object", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding,
                                            "%s  %s", KindLabelUVE(object.kind),
                                            object.name.empty() || object.name == KindLabelUVE(object.kind)
                                                ? ("#" + std::to_string(object.id)).c_str()
                                                : object.name.c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", KindHelpUVE(object.kind));
        }
        if (object.kind != Kind::Output && ImGui::BeginPopupContextItem("##object-menu")) {
            if (ImGui::MenuItem("Delete")) {
                removeId = object.id;
            }
            ImGui::EndPopup();
        }
        if (open) {
            if (ImGui::BeginTable("##fields", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
                ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthStretch, 0.38F);
                ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 0.62F);
                RowUVE("Name");
                std::string renamed;
                if (EditTextUVE("##name", object.name, renamed)) {
                    object.name = renamed;
                    changed = true;
                }

                const auto drag = [&](const char* const label, float& value, const float speed, const float minimum,
                                      const float maximum) {
                    RowUVE(label);
                    const std::string id = std::string{"##"} + label;
                    if (ImGui::DragFloat(id.c_str(), &value, speed, minimum, maximum, "%.3f")) {
                        continuous = true;
                    }
                    if (ImGui::IsItemDeactivated()) {
                        static_cast<void>(CommitComponentPropertyPreviewForUVE(entry, property));
                    }
                };
                const auto parameterRow = [&](const char* const label,
                                              std::initializer_list<AnimationParameterTypeUVE> types,
                                              const char* const none) {
                    RowUVE(label);
                    if (PickParameterUVE("##parameter", tree.parameters, types, none, object.parameter)) {
                        changed = true;
                    }
                };

                const auto syncRow = [&]() {
                    RowUVE("Sync");
                    if (ImGui::Checkbox("##sync", &object.sync)) {
                        changed = true;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Keep the inputs in step: the heaviest leads, the others play at its phase.");
                    }
                };
                const auto blendModeRows = [&]() {
                    RowUVE("Blend");
                    static constexpr std::array<const char*, 3> kModes{"Blend", "Nearest", "Nearest, in step"};
                    const auto mode = std::min(static_cast<std::size_t>(object.blendMode), kModes.size() - 1U);
                    if (ImGui::BeginCombo("##blend-mode", kModes[mode])) {
                        for (std::size_t option = 0U; option < kModes.size(); ++option) {
                            if (ImGui::Selectable(kModes[option], option == mode)) {
                                object.blendMode = static_cast<Scene::AnimationBlendModeUVE>(option);
                                changed = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    drag("Smoothing", object.smoothingSeconds, 0.005F, 0.0F, 5.0F);
                    if (object.blendMode != Scene::AnimationBlendModeUVE::Blend) {
                        drag("Switch", object.fadeSeconds, 0.005F, 0.0F, 5.0F);
                    }
                };

                switch (object.kind) {
                    case Kind::Clip: {
                        RowUVE("Clip");
                        if (const std::optional<Asset::AssetGuidUVE> picked =
                                DrawAssetPickerUVE("##clip", object.clip, ".uvanim")) {
                            object.clip = *picked;
                            changed = true;
                        }
                        RowUVE("Loop");
                        if (ImGui::Checkbox("##loop", &object.loop)) {
                            changed = true;
                        }
                        drag("Speed", object.speed, 0.01F, -100.0F, 100.0F);
                        break;
                    }
                    case Kind::Blend2:
                    case Kind::Additive:
                        parameterRow("Weight From", {AnimationParameterTypeUVE::Float}, "(fixed value)");
                        if (object.parameter.empty()) {
                            drag("Weight", object.value, 0.01F, 0.0F, 1.0F);
                        }
                        syncRow();
                        break;
                    case Kind::BlendSpace1D:
                        parameterRow("Position From", {AnimationParameterTypeUVE::Float}, "(fixed value)");
                        if (object.parameter.empty()) {
                            drag("Position", object.value, 0.01F, -1000.0F, 1000.0F);
                        }
                        blendModeRows();
                        syncRow();
                        break;
                    case Kind::OneShot:
                        parameterRow("Fire On", {AnimationParameterTypeUVE::Trigger, AnimationParameterTypeUVE::Bool},
                                     "(never)");
                        drag("Fade", object.fadeSeconds, 0.01F, 0.0F, 10.0F);
                        break;
                    case Kind::TimeScale:
                        parameterRow("Rate From", {AnimationParameterTypeUVE::Float}, "(fixed rate)");
                        if (object.parameter.empty()) {
                            drag("Rate", object.speed, 0.01F, -100.0F, 100.0F);
                        }
                        break;
                    case Kind::StateMachine: {
                        RowUVE("Entry State");
                        int entryState = static_cast<int>(object.entryState);
                        std::string entryPreview = SlotLabelUVE(object.kind, object.entryState);
                        if (ImGui::BeginCombo("##entry", entryPreview.c_str())) {
                            for (std::size_t slot = 0U; slot < object.inputs.size(); ++slot) {
                                if (ImGui::Selectable(SlotLabelUVE(object.kind, slot).c_str(),
                                                      static_cast<int>(slot) == entryState)) {
                                    object.entryState = static_cast<std::uint32_t>(slot);
                                    changed = true;
                                }
                            }
                            ImGui::EndCombo();
                        }
                        break;
                    }
                    case Kind::BlendSpace2D: {
                        parameterRow("X From", {AnimationParameterTypeUVE::Float}, "(fixed value)");
                        if (object.parameter.empty()) {
                            drag("X", object.value, 0.01F, -1000.0F, 1000.0F);
                        }
                        RowUVE("Y From");
                        if (PickParameterUVE("##parameter-y", tree.parameters, {AnimationParameterTypeUVE::Float},
                                             "(fixed value)", object.parameterY)) {
                            changed = true;
                        }
                        if (object.parameterY.empty()) {
                            drag("Y", object.valueY, 0.01F, -1000.0F, 1000.0F);
                        }
                        blendModeRows();
                        syncRow();
                        break;
                    }
                    case Kind::Select:
                        parameterRow("Pick From", {AnimationParameterTypeUVE::Float, AnimationParameterTypeUVE::Bool},
                                     "(fixed index)");
                        if (object.parameter.empty()) {
                            drag("Index", object.value, 0.05F, 0.0F, static_cast<float>(object.inputs.size() - 1U));
                        }
                        drag("Fade", object.fadeSeconds, 0.01F, 0.0F, 10.0F);
                        RowUVE("Restart");
                        if (ImGui::Checkbox("##restart", &object.restart)) {
                            changed = true;
                        }
                        break;
                    case Kind::LayeredBlend: {
                        parameterRow("Weight From", {AnimationParameterTypeUVE::Float}, "(fixed value)");
                        if (object.parameter.empty()) {
                            drag("Weight", object.value, 0.01F, 0.0F, 1.0F);
                        }
                        RowUVE("Bones");
                        ImGui::TextDisabled("%s", object.bones.empty() ? "whole body" : "branches below:");
                        std::optional<std::size_t> removeBone;
                        for (std::size_t bone = 0U; bone < object.bones.size(); ++bone) {
                            ImGui::PushID(static_cast<int>(bone) + 7000);
                            RowUVE("");
                            ImGui::TextUnformatted(object.bones[bone].c_str());
                            ImGui::SameLine();
                            if (ImGui::SmallButton("x")) {
                                removeBone = bone;
                            }
                            ImGui::PopID();
                        }
                        if (removeBone.has_value()) {
                            object.bones.erase(object.bones.begin() + static_cast<std::ptrdiff_t>(*removeBone));
                            changed = true;
                        }
                        RowUVE("Add Bone");
                        std::string added;
                        if (EditTextUVE("##add-bone", std::string{}, added) && !added.empty() &&
                            std::ranges::find(object.bones, added) == object.bones.end() &&
                            object.bones.size() < Scene::kMaximumAnimationLayerBonesUVE) {
                            object.bones.push_back(added);
                            changed = true;
                        }
                        break;
                    }
                    case Kind::TimeSeek:
                        parameterRow("Seek On", {AnimationParameterTypeUVE::Trigger, AnimationParameterTypeUVE::Bool},
                                     "(never)");
                        drag("Seek To (s)", object.value, 0.01F, 0.0F, 3600.0F);
                        break;
                    case Kind::Output:
                        break;
                }

                // Inputs: each slot picks a object that nothing else uses yet.
                for (std::size_t slot = 0U; slot < object.inputs.size(); ++slot) {
                    ImGui::PushID(static_cast<int>(slot) + 1000);
                    std::string slotLabel = SlotLabelUVE(object.kind, slot);
                    if (object.kind == Kind::StateMachine) {
                        const auto child = indexById.find(object.inputs[slot]);
                        if (child != indexById.end() && !objects[child->second].name.empty()) {
                            slotLabel += " (" + objects[child->second].name + ")";
                        }
                    }
                    RowUVE(slotLabel.c_str());
                    const std::uint32_t current = object.inputs[slot];
                    const auto currentIt = indexById.find(current);
                    const std::string preview =
                        currentIt == indexById.end() ? std::string{"(empty)"} : ObjectLabelUVE(objects[currentIt->second]);
                    const bool removable = object.kind == Kind::Select || object.kind == Kind::StateMachine;
                    if (removable) {
                        ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
                    }
                    if (ImGui::BeginCombo("##input", preview.c_str())) {
                        if (ImGui::Selectable("(empty)", current == 0U) && current != 0U) {
                            object.inputs[slot] = 0U;
                            changed = true;
                        }
                        for (const AnimationGraphObjectUVE& candidate : objects) {
                            if (candidate.id == object.id || candidate.kind == Kind::Output ||
                                (used.contains(candidate.id) && candidate.id != current)) {
                                continue;
                            }
                            if (ImGui::Selectable(ObjectLabelUVE(candidate).c_str(), candidate.id == current) &&
                                candidate.id != current) {
                                object.inputs[slot] = candidate.id;
                                changed = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (removable) {
                        ImGui::SameLine();
                        if (ImGui::Button("x", ImVec2(ImGui::GetFrameHeight(), 0.0F)) && object.inputs.size() > 1U) {
                            std::vector<AnimationGraphObjectUVE> shrunk{object};
                            static_cast<void>(RemoveAnimationGraphInputSlotUVE(shrunk, object.id, slot));
                            object = std::move(shrunk.front());
                            changed = true;
                            ImGui::PopID();
                            break;
                        }
                    }
                    ImGui::PopID();
                }
                // A blend space's own animations, one row each: where, what, how fast.
                if (object.kind == Kind::BlendSpace1D || object.kind == Kind::BlendSpace2D) {
                    std::optional<std::size_t> removePoint;
                    for (std::size_t slot = 0U; slot < object.blendPoints.size(); ++slot) {
                        Scene::AnimationBlendPointUVE& point = object.blendPoints[slot];
                        ImGui::PushID(static_cast<int>(slot) + 3000);
                        const std::string pointLabel = "Point " + std::to_string(slot + 1U);
                        RowUVE(pointLabel.c_str());
                        ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
                        if (const std::optional<Asset::AssetGuidUVE> picked = DrawAssetPickerUVE("##point-clip", point.clip, ".uvanim")) {
                            point.clip = *picked;
                            changed = true;
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("x", ImVec2(ImGui::GetFrameHeight(), 0.0F))) {
                            removePoint = slot;
                        }
                        RowUVE("  at");
                        if (object.kind == Kind::BlendSpace2D) {
                            float xy[2] = {point.position.x, point.position.y};
                            if (ImGui::DragFloat2("##at", xy, 0.01F)) {
                                point.position = Math::Vector2UVE{xy[0], xy[1]};
                                continuous = true;
                            }
                        } else if (ImGui::DragFloat("##at", &point.position.x, 0.01F)) {
                            continuous = true;
                        }
                        if (ImGui::IsItemDeactivated()) {
                            static_cast<void>(CommitComponentPropertyPreviewForUVE(entry, property));
                        }
                        RowUVE("  speed / loop");
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() -
                                                ImGui::GetStyle().ItemSpacing.x);
                        if (ImGui::DragFloat("##speed", &point.speed, 0.01F, -100.0F, 100.0F, "x%.2f")) {
                            continuous = true;
                        }
                        if (ImGui::IsItemDeactivated()) {
                            static_cast<void>(CommitComponentPropertyPreviewForUVE(entry, property));
                        }
                        ImGui::SameLine();
                        if (ImGui::Checkbox("##loop", &point.loop)) {
                            changed = true;
                        }
                        ImGui::PopID();
                    }
                    if (removePoint.has_value()) {
                        object.blendPoints.erase(object.blendPoints.begin() + static_cast<std::ptrdiff_t>(*removePoint));
                        changed = true;
                    }
                }
                ImGui::EndTable();
            }
            if (object.kind == Kind::BlendSpace1D || object.kind == Kind::BlendSpace2D) {
                if (ImGui::SmallButton("+ Point")) {
                    // Past the furthest point along X; the animation is picked on its row.
                    float furthest = -1.0F;
                    for (const Scene::AnimationBlendPointUVE& point : object.blendPoints) {
                        furthest = std::max(furthest, point.position.x);
                    }
                    std::vector<AnimationGraphObjectUVE> grown{object};
                    if (AddBlendSpacePointUVE(grown, object.id, Math::Vector2UVE{furthest + 1.0F, 0.0F}, Asset::AssetGuidUVE{})) {
                        object = std::move(grown.front());
                        changed = true;
                    }
                }
            }
            if (object.kind == Kind::Select || object.kind == Kind::StateMachine) {
                const char* const label = object.kind == Kind::StateMachine ? "+ State" : "+ Option";
                if (ImGui::SmallButton(label)) {
                    std::vector<AnimationGraphObjectUVE> grown{object};
                    if (AddAnimationGraphInputSlotUVE(grown, object.id)) {
                        object = std::move(grown.front());
                        changed = true;
                    }
                }
            }

            if (object.kind == Kind::StateMachine) {
                ImGui::Spacing();
                ImGui::TextDisabled("Transitions");
                std::optional<std::size_t> removeTransition;
                std::optional<std::pair<std::size_t, std::size_t>> moveTransition;
                const auto stateLabel = [&object](const std::uint32_t state) {
                    return state == Scene::kAnyAnimationStateUVE ? std::string{"Any"}
                                                                 : SlotLabelUVE(Kind::StateMachine, state);
                };
                for (std::size_t t = 0U; t < object.transitions.size(); ++t) {
                    AnimationTransitionUVE& transition = object.transitions[t];
                    ImGui::PushID(static_cast<int>(t) + 5000);
                    const float width = ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() * 3.0F;
                    ImGui::SetNextItemWidth(width * 0.38F);
                    if (ImGui::BeginCombo("##from", stateLabel(transition.fromState).c_str())) {
                        if (ImGui::Selectable("Any", transition.fromState == Scene::kAnyAnimationStateUVE)) {
                            transition.fromState = Scene::kAnyAnimationStateUVE;
                            changed = true;
                        }
                        for (std::uint32_t state = 0U; state < object.inputs.size(); ++state) {
                            if (ImGui::Selectable(stateLabel(state).c_str(), transition.fromState == state)) {
                                transition.fromState = state;
                                changed = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SameLine();
                    ImGui::TextUnformatted("->");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(width * 0.38F);
                    if (ImGui::BeginCombo("##to", stateLabel(transition.toState).c_str())) {
                        for (std::uint32_t state = 0U; state < object.inputs.size(); ++state) {
                            if (ImGui::Selectable(stateLabel(state).c_str(), transition.toState == state)) {
                                transition.toState = state;
                                changed = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SameLine();
                    ImGui::BeginDisabled(t == 0U);
                    if (ImGui::ArrowButton("##earlier", ImGuiDir_Up)) {
                        moveTransition = std::pair<std::size_t, std::size_t>{t, t - 1U};
                    }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    ImGui::BeginDisabled(t + 1U >= object.transitions.size());
                    if (ImGui::ArrowButton("##later", ImGuiDir_Down)) {
                        moveTransition = std::pair<std::size_t, std::size_t>{t, t + 1U};
                    }
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        ImGui::SetTooltip("Transitions are tried top to bottom: the first ready one is taken.");
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("x")) {
                        removeTransition = t;
                    }
                    ImGui::Indent(12.0F);
                    const TransitionEditUVE edited = DrawAnimationTransitionUVE(transition, tree.parameters);
                    changed = changed || edited.changed;
                    continuous = continuous || edited.dragging;
                    if (edited.released) {
                        static_cast<void>(CommitComponentPropertyPreviewForUVE(entry, property));
                    }
                    ImGui::Unindent(12.0F);
                    ImGui::Separator();
                    ImGui::PopID();
                }
                if (moveTransition.has_value()) {
                    std::vector<AnimationGraphObjectUVE> reordered{object};
                    if (MoveAnimationTransitionUVE(reordered, object.id, moveTransition->first, moveTransition->second)) {
                        object = std::move(reordered.front());
                        changed = true;
                    }
                }
                if (removeTransition.has_value()) {
                    object.transitions.erase(object.transitions.begin() + static_cast<std::ptrdiff_t>(*removeTransition));
                    changed = true;
                }
                if (ImGui::SmallButton("+ Transition") && !object.inputs.empty() &&
                    object.transitions.size() < Scene::kMaximumAnimationTransitionsUVE) {
                    AnimationTransitionUVE transition;
                    transition.fromState = 0U;
                    transition.toState = object.inputs.size() > 1U ? 1U : 0U;
                    object.transitions.push_back(transition);
                    changed = true;
                }
            }
            ImGui::TreePop();
        }
        ImGui::Unindent(static_cast<float>(depth) * 12.0F + 0.001F);
        ImGui::PopID();
    }

    if (removeId.has_value()) {
        std::erase_if(objects, [&removeId](const AnimationGraphObjectUVE& object) { return object.id == *removeId; });
        for (AnimationGraphObjectUVE& object : objects) {
            std::replace(object.inputs.begin(), object.inputs.end(), *removeId, 0U);
        }
        changed = true;
    }

    ImGui::Spacing();
    if (ImGui::Button("+ Object")) {
        ImGui::OpenPopup("##add-object");
    }
    if (ImGui::BeginPopup("##add-object")) {
        for (const Kind kind : kAddableKindsUVE) {
            if (ImGui::MenuItem(KindLabelUVE(kind)) &&
                AddAnimationGraphObjectUVE(objects, kind, Math::Vector2UVE{}) != 0U) {
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", KindHelpUVE(kind));
            }
        }
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();

    if (continuous) {
        static_cast<void>(PreviewSelectedComponentPropertyUVE(entry, property, &objects));
    } else if (changed) {
        static_cast<void>(SetSelectedComponentPropertyUVE(entry, property, &objects));
    }
}

} // namespace UVE::Editor
