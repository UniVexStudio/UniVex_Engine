// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// AnimationTree's Inspector blocks: the parameter table and the graph itself. The graph is shown
// as the tree it is - Output at the top, each node's inputs indented under it - with nodes nothing
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

#include "uve/component/animation_tree_component_uve.h"

namespace UVE::Editor {
namespace {

using Kind = Scene::AnimationGraphNodeKindUVE;
using Scene::AnimationGraphNodeUVE;
using Scene::AnimationParameterTypeUVE;
using Scene::AnimationParameterUVE;
using Scene::AnimationTransitionUVE;

constexpr std::array<Kind, 7> kAddableKindsUVE{Kind::Clip,     Kind::Blend2,    Kind::BlendSpace1D, Kind::Additive,
                                               Kind::OneShot, Kind::TimeScale, Kind::StateMachine};

[[nodiscard]] const char* KindLabelUVE(const Kind kind) noexcept {
    switch (kind) {
        case Kind::Output: return "Output";
        case Kind::Clip: return "Clip";
        case Kind::Blend2: return "Blend";
        case Kind::BlendSpace1D: return "Blend Space";
        case Kind::Additive: return "Additive";
        case Kind::OneShot: return "One Shot";
        case Kind::TimeScale: return "Time Scale";
        case Kind::StateMachine: return "State Machine";
    }
    return "?";
}

[[nodiscard]] const char* KindHelpUVE(const Kind kind) noexcept {
    switch (kind) {
        case Kind::Output: return "What the target shows.";
        case Kind::Clip: return "Plays a clip.";
        case Kind::Blend2: return "Mixes A and B by a weight: 0 is A, 1 is B.";
        case Kind::BlendSpace1D: return "Places its inputs along a line and mixes the two either side of a value - walk, jog, run by speed.";
        case Kind::Additive: return "Lays the Layer's motion on top of the Base, scaled by a weight.";
        case Kind::OneShot: return "Plays Shot once over Base when a trigger fires, fading in and out.";
        case Kind::TimeScale: return "Runs its input faster or slower.";
        case Kind::StateMachine: return "Its inputs are states. Transitions move between them and crossfade.";
    }
    return "";
}

/// What each input slot of a kind means, for its label.
[[nodiscard]] std::string SlotLabelUVE(const Kind kind, const std::size_t slot) {
    switch (kind) {
        case Kind::Blend2: return slot == 0U ? "A" : "B";
        case Kind::Additive: return slot == 0U ? "Base" : "Layer";
        case Kind::OneShot: return slot == 0U ? "Base" : "Shot";
        case Kind::StateMachine: return "State " + std::to_string(slot + 1U);
        case Kind::BlendSpace1D: return "Point " + std::to_string(slot + 1U);
        default: return "Input";
    }
}

[[nodiscard]] std::string NodeLabelUVE(const AnimationGraphNodeUVE& node) {
    const std::string name = node.name.empty() ? std::string{KindLabelUVE(node.kind)} : node.name;
    return name + "  #" + std::to_string(node.id);
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

/// A combo over the parameters of `wanted` types, plus "(value)" meaning none. True when changed.
[[nodiscard]] bool PickParameterUVE(const char* const id, const std::vector<AnimationParameterUVE>& parameters,
                                    std::initializer_list<AnimationParameterTypeUVE> wanted, const char* const noneLabel,
                                    std::string& inOutName) {
    const bool known = std::any_of(parameters.begin(), parameters.end(),
                                   [&inOutName](const AnimationParameterUVE& p) { return p.name == inOutName; });
    const std::string preview = inOutName.empty() ? std::string{noneLabel}
                                : known           ? inOutName
                                                  : inOutName + " (missing)";
    bool changed = false;
    if (ImGui::BeginCombo(id, preview.c_str())) {
        if (ImGui::Selectable(noneLabel, inOutName.empty()) && !inOutName.empty()) {
            inOutName.clear();
            changed = true;
        }
        for (const AnimationParameterUVE& parameter : parameters) {
            if (std::find(wanted.begin(), wanted.end(), parameter.type) == wanted.end()) {
                continue;
            }
            if (ImGui::Selectable(parameter.name.c_str(), parameter.name == inOutName) && parameter.name != inOutName) {
                inOutName = parameter.name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
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
                // Nodes and transitions reading the old name follow it, as a second step.
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
        ImGui::TextDisabled("No parameters. Add one for a node or transition to read.");
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
    std::vector<AnimationGraphNodeUVE> nodes = tree.nodes;
    bool changed = false;
    bool continuous = false;

    // A parameter renamed in the table above: the nodes and transitions that read it follow.
    if (m_pendingAnimationParameterRename.has_value()) {
        const auto [from, to] = *m_pendingAnimationParameterRename;
        m_pendingAnimationParameterRename.reset();
        for (AnimationGraphNodeUVE& node : nodes) {
            if (node.parameter == from) {
                node.parameter = to;
                changed = true;
            }
            for (AnimationTransitionUVE& transition : node.transitions) {
                if (transition.parameter == from) {
                    transition.parameter = to;
                    changed = true;
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
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        indexById.emplace(nodes[index].id, index);
        for (const std::uint32_t input : nodes[index].inputs) {
            used.insert(input);
        }
    }

    // Tree order from the Output, then the loose nodes, each with its depth for the indent.
    std::vector<std::pair<std::size_t, int>> order;
    std::unordered_set<std::size_t> listed;
    const std::function<void(std::size_t, int)> visit = [&](const std::size_t index, const int depth) {
        if (!listed.insert(index).second) {
            return;
        }
        order.emplace_back(index, depth);
        for (const std::uint32_t input : nodes[index].inputs) {
            const auto child = indexById.find(input);
            if (child != indexById.end()) {
                visit(child->second, depth + 1);
            }
        }
    };
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        if (nodes[index].kind == Kind::Output) {
            visit(index, 0);
        }
    }
    const std::size_t connectedCount = order.size();
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        if (!listed.contains(index) && !used.contains(nodes[index].id)) {
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
        AnimationGraphNodeUVE& node = nodes[index];
        ImGui::PushID(static_cast<int>(node.id));
        ImGui::Indent(static_cast<float>(depth) * 12.0F + 0.001F);
        const bool open = ImGui::TreeNodeEx("##node", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding,
                                            "%s  %s", KindLabelUVE(node.kind),
                                            node.name.empty() || node.name == KindLabelUVE(node.kind)
                                                ? ("#" + std::to_string(node.id)).c_str()
                                                : node.name.c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", KindHelpUVE(node.kind));
        }
        if (node.kind != Kind::Output && ImGui::BeginPopupContextItem("##node-menu")) {
            if (ImGui::MenuItem("Delete")) {
                removeId = node.id;
            }
            ImGui::EndPopup();
        }
        if (open) {
            if (ImGui::BeginTable("##fields", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
                ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthStretch, 0.38F);
                ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 0.62F);
                RowUVE("Name");
                std::string renamed;
                if (EditTextUVE("##name", node.name, renamed)) {
                    node.name = renamed;
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
                    if (PickParameterUVE("##parameter", tree.parameters, types, none, node.parameter)) {
                        changed = true;
                    }
                };

                switch (node.kind) {
                    case Kind::Clip: {
                        RowUVE("Clip");
                        if (const std::optional<Asset::AssetGuidUVE> picked =
                                DrawAssetPickerUVE("##clip", node.clip, ".uveanim")) {
                            node.clip = *picked;
                            changed = true;
                        }
                        RowUVE("Loop");
                        if (ImGui::Checkbox("##loop", &node.loop)) {
                            changed = true;
                        }
                        drag("Speed", node.speed, 0.01F, -100.0F, 100.0F);
                        break;
                    }
                    case Kind::Blend2:
                    case Kind::Additive:
                        parameterRow("Weight From", {AnimationParameterTypeUVE::Float}, "(fixed value)");
                        if (node.parameter.empty()) {
                            drag("Weight", node.value, 0.01F, 0.0F, 1.0F);
                        }
                        break;
                    case Kind::BlendSpace1D:
                        parameterRow("Position From", {AnimationParameterTypeUVE::Float}, "(fixed value)");
                        if (node.parameter.empty()) {
                            drag("Position", node.value, 0.01F, -1000.0F, 1000.0F);
                        }
                        break;
                    case Kind::OneShot:
                        parameterRow("Fire On", {AnimationParameterTypeUVE::Trigger, AnimationParameterTypeUVE::Bool},
                                     "(never)");
                        drag("Fade", node.fadeSeconds, 0.01F, 0.0F, 10.0F);
                        break;
                    case Kind::TimeScale:
                        parameterRow("Rate From", {AnimationParameterTypeUVE::Float}, "(fixed rate)");
                        if (node.parameter.empty()) {
                            drag("Rate", node.speed, 0.01F, -100.0F, 100.0F);
                        }
                        break;
                    case Kind::StateMachine: {
                        RowUVE("Entry State");
                        int entryState = static_cast<int>(node.entryState);
                        std::string entryPreview = SlotLabelUVE(node.kind, node.entryState);
                        if (ImGui::BeginCombo("##entry", entryPreview.c_str())) {
                            for (std::size_t slot = 0U; slot < node.inputs.size(); ++slot) {
                                if (ImGui::Selectable(SlotLabelUVE(node.kind, slot).c_str(),
                                                      static_cast<int>(slot) == entryState)) {
                                    node.entryState = static_cast<std::uint32_t>(slot);
                                    changed = true;
                                }
                            }
                            ImGui::EndCombo();
                        }
                        break;
                    }
                    case Kind::Output:
                        break;
                }

                // Inputs: each slot picks a node that nothing else uses yet.
                for (std::size_t slot = 0U; slot < node.inputs.size(); ++slot) {
                    ImGui::PushID(static_cast<int>(slot) + 1000);
                    std::string slotLabel = SlotLabelUVE(node.kind, slot);
                    if (node.kind == Kind::StateMachine) {
                        const auto child = indexById.find(node.inputs[slot]);
                        if (child != indexById.end() && !nodes[child->second].name.empty()) {
                            slotLabel += " (" + nodes[child->second].name + ")";
                        }
                    }
                    RowUVE(slotLabel.c_str());
                    const std::uint32_t current = node.inputs[slot];
                    const auto currentIt = indexById.find(current);
                    const std::string preview =
                        currentIt == indexById.end() ? std::string{"(empty)"} : NodeLabelUVE(nodes[currentIt->second]);
                    const bool removable = node.kind == Kind::BlendSpace1D || node.kind == Kind::StateMachine;
                    if (removable) {
                        ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
                    }
                    if (ImGui::BeginCombo("##input", preview.c_str())) {
                        if (ImGui::Selectable("(empty)", current == 0U) && current != 0U) {
                            node.inputs[slot] = 0U;
                            changed = true;
                        }
                        for (const AnimationGraphNodeUVE& candidate : nodes) {
                            if (candidate.id == node.id || candidate.kind == Kind::Output ||
                                (used.contains(candidate.id) && candidate.id != current)) {
                                continue;
                            }
                            if (ImGui::Selectable(NodeLabelUVE(candidate).c_str(), candidate.id == current) &&
                                candidate.id != current) {
                                node.inputs[slot] = candidate.id;
                                changed = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (removable) {
                        ImGui::SameLine();
                        if (ImGui::Button("x", ImVec2(ImGui::GetFrameHeight(), 0.0F)) && node.inputs.size() > 1U) {
                            node.inputs.erase(node.inputs.begin() + static_cast<std::ptrdiff_t>(slot));
                            if (node.kind == Kind::BlendSpace1D && slot < node.points.size()) {
                                node.points.erase(node.points.begin() + static_cast<std::ptrdiff_t>(slot));
                            }
                            if (node.kind == Kind::StateMachine) {
                                // Transitions touching the removed state go; later states shift down.
                                const auto state = static_cast<std::uint32_t>(slot);
                                std::erase_if(node.transitions, [state](const AnimationTransitionUVE& transition) {
                                    return transition.fromState == state || transition.toState == state;
                                });
                                for (AnimationTransitionUVE& transition : node.transitions) {
                                    if (transition.fromState != Scene::kAnyAnimationStateUVE && transition.fromState > state) {
                                        --transition.fromState;
                                    }
                                    if (transition.toState > state) {
                                        --transition.toState;
                                    }
                                }
                                node.entryState = std::min(node.entryState, static_cast<std::uint32_t>(node.inputs.size() - 1U));
                            }
                            changed = true;
                            ImGui::PopID();
                            break;
                        }
                    }
                    if (node.kind == Kind::BlendSpace1D && slot < node.points.size()) {
                        const std::string pointLabel = "  at";
                        drag(pointLabel.c_str(), node.points[slot], 0.01F, -1000.0F, 1000.0F);
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (node.kind == Kind::BlendSpace1D || node.kind == Kind::StateMachine) {
                if (ImGui::SmallButton(node.kind == Kind::StateMachine ? "+ State" : "+ Point") &&
                    node.inputs.size() < Scene::kMaximumAnimationNodeInputsUVE) {
                    node.inputs.push_back(0U);
                    if (node.kind == Kind::BlendSpace1D) {
                        node.points.push_back(node.points.empty() ? 0.0F : node.points.back() + 1.0F);
                    }
                    changed = true;
                }
            }

            if (node.kind == Kind::StateMachine) {
                ImGui::Spacing();
                ImGui::TextDisabled("Transitions");
                std::optional<std::size_t> removeTransition;
                const auto stateLabel = [&node](const std::uint32_t state) {
                    return state == Scene::kAnyAnimationStateUVE ? std::string{"Any"}
                                                                 : SlotLabelUVE(Kind::StateMachine, state);
                };
                for (std::size_t t = 0U; t < node.transitions.size(); ++t) {
                    AnimationTransitionUVE& transition = node.transitions[t];
                    ImGui::PushID(static_cast<int>(t) + 5000);
                    const float width = ImGui::GetContentRegionAvail().x;
                    ImGui::SetNextItemWidth(width * 0.30F);
                    if (ImGui::BeginCombo("##from", stateLabel(transition.fromState).c_str())) {
                        if (ImGui::Selectable("Any", transition.fromState == Scene::kAnyAnimationStateUVE)) {
                            transition.fromState = Scene::kAnyAnimationStateUVE;
                            changed = true;
                        }
                        for (std::uint32_t state = 0U; state < node.inputs.size(); ++state) {
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
                    ImGui::SetNextItemWidth(width * 0.30F);
                    if (ImGui::BeginCombo("##to", stateLabel(transition.toState).c_str())) {
                        for (std::uint32_t state = 0U; state < node.inputs.size(); ++state) {
                            if (ImGui::Selectable(stateLabel(state).c_str(), transition.toState == state)) {
                                transition.toState = state;
                                changed = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("x")) {
                        removeTransition = t;
                    }
                    ImGui::Indent(12.0F);
                    ImGui::SetNextItemWidth(width * 0.5F);
                    int condition = static_cast<int>(transition.condition);
                    if (ImGui::Combo("##when", &condition,
                                     "Always\0At End\0Parameter >\0Parameter <\0Parameter Is True\0Parameter Is False\0Triggered\0")) {
                        transition.condition = static_cast<Scene::AnimationConditionUVE>(condition);
                        changed = true;
                    }
                    using Condition = Scene::AnimationConditionUVE;
                    if (transition.condition != Condition::Always && transition.condition != Condition::AtEnd) {
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        const bool wantsTrigger = transition.condition == Condition::Triggered;
                        if (PickParameterUVE("##param", tree.parameters,
                                             wantsTrigger ? std::initializer_list<AnimationParameterTypeUVE>{AnimationParameterTypeUVE::Trigger}
                                                          : std::initializer_list<AnimationParameterTypeUVE>{
                                                                AnimationParameterTypeUVE::Float,
                                                                AnimationParameterTypeUVE::Bool},
                                             "(pick)", transition.parameter)) {
                            changed = true;
                        }
                    }
                    if (transition.condition == Condition::ParameterGreater ||
                        transition.condition == Condition::ParameterLess) {
                        ImGui::SetNextItemWidth(width * 0.5F);
                        if (ImGui::DragFloat("threshold", &transition.threshold, 0.01F)) {
                            continuous = true;
                        }
                        if (ImGui::IsItemDeactivated()) {
                            static_cast<void>(CommitComponentPropertyPreviewForUVE(entry, property));
                        }
                    }
                    ImGui::SetNextItemWidth(width * 0.5F);
                    if (ImGui::DragFloat("fade (s)", &transition.fadeSeconds, 0.01F, 0.0F, 10.0F, "%.2f")) {
                        continuous = true;
                    }
                    if (ImGui::IsItemDeactivated()) {
                        static_cast<void>(CommitComponentPropertyPreviewForUVE(entry, property));
                    }
                    ImGui::Unindent(12.0F);
                    ImGui::PopID();
                }
                if (removeTransition.has_value()) {
                    node.transitions.erase(node.transitions.begin() + static_cast<std::ptrdiff_t>(*removeTransition));
                    changed = true;
                }
                if (ImGui::SmallButton("+ Transition") && !node.inputs.empty() &&
                    node.transitions.size() < Scene::kMaximumAnimationTransitionsUVE) {
                    AnimationTransitionUVE transition;
                    transition.fromState = 0U;
                    transition.toState = node.inputs.size() > 1U ? 1U : 0U;
                    node.transitions.push_back(transition);
                    changed = true;
                }
            }
            ImGui::TreePop();
        }
        ImGui::Unindent(static_cast<float>(depth) * 12.0F + 0.001F);
        ImGui::PopID();
    }

    if (removeId.has_value()) {
        std::erase_if(nodes, [&removeId](const AnimationGraphNodeUVE& node) { return node.id == *removeId; });
        for (AnimationGraphNodeUVE& node : nodes) {
            std::replace(node.inputs.begin(), node.inputs.end(), *removeId, 0U);
        }
        changed = true;
    }

    ImGui::Spacing();
    if (ImGui::Button("+ Node")) {
        ImGui::OpenPopup("##add-node");
    }
    if (ImGui::BeginPopup("##add-node")) {
        for (const Kind kind : kAddableKindsUVE) {
            if (ImGui::MenuItem(KindLabelUVE(kind)) && nodes.size() < Scene::kMaximumAnimationGraphNodesUVE) {
                AnimationGraphNodeUVE added;
                added.id = Scene::NextAnimationGraphNodeIdUVE(nodes);
                added.kind = kind;
                added.name = KindLabelUVE(kind);
                switch (kind) {
                    case Kind::Blend2:
                    case Kind::Additive:
                    case Kind::OneShot:
                        added.inputs = {0U, 0U};
                        break;
                    case Kind::TimeScale:
                    case Kind::StateMachine:
                        added.inputs = {0U};
                        break;
                    case Kind::BlendSpace1D:
                        added.inputs = {0U, 0U};
                        added.points = {0.0F, 1.0F};
                        added.value = 0.0F;
                        break;
                    default:
                        break;
                }
                nodes.push_back(std::move(added));
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
        static_cast<void>(PreviewSelectedComponentPropertyUVE(entry, property, &nodes));
    } else if (changed) {
        static_cast<void>(SetSelectedComponentPropertyUVE(entry, property, &nodes));
    }
}

} // namespace UVE::Editor
