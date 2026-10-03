// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_animation_graph_widgets_uve.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstddef>
#include <optional>

#include <imgui.h>

namespace UVE::Editor {
namespace {

using Condition = Scene::AnimationConditionUVE;
using ParameterType = Scene::AnimationParameterTypeUVE;

constexpr float kLabelWidthUVE = 84.0F;

void LabelUVE(const char* const label) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(kLabelWidthUVE);
}

/// The tests offered, in the order a person reaches for them. Always is left out: no condition is
/// the same thing.
struct ConditionChoiceUVE final {
    Condition condition;
    const char* label;
    const char* help;
};
constexpr std::array<ConditionChoiceUVE, 6> kConditionChoicesUVE{{
    {Condition::ParameterTrue, "is true", "A Bool (or a Float of at least 0.5) is on."},
    {Condition::ParameterFalse, "is false", "A Bool (or a Float under 0.5) is off."},
    {Condition::ParameterGreater, "is above", "A Float is above the value."},
    {Condition::ParameterLess, "is below", "A Float is below the value."},
    {Condition::Triggered, "fires", "A Trigger was set. It is used up when this transition is taken."},
    {Condition::AtEnd, "state finished", "The state's animation reached its end (for a loop, each time round)."},
}};

[[nodiscard]] const char* ConditionLabelUVE(const Condition condition) {
    const auto found = std::ranges::find(kConditionChoicesUVE, condition, &ConditionChoiceUVE::condition);
    return found != kConditionChoicesUVE.end() ? found->label : "always";
}

} // namespace

bool PickParameterUVE(const char* const id, const std::vector<Scene::AnimationParameterUVE>& parameters,
                      const std::initializer_list<Scene::AnimationParameterTypeUVE> wanted, const char* const noneLabel,
                      std::string& inOutName) {
    const bool known = std::ranges::any_of(parameters, [&inOutName](const Scene::AnimationParameterUVE& p) {
        return p.name == inOutName;
    });
    const std::string preview = inOutName.empty() ? std::string{noneLabel}
                                : known           ? inOutName
                                                  : inOutName + " (missing)";
    bool changed = false;
    if (ImGui::BeginCombo(id, preview.c_str())) {
        if (ImGui::Selectable(noneLabel, inOutName.empty()) && !inOutName.empty()) {
            inOutName.clear();
            changed = true;
        }
        for (const Scene::AnimationParameterUVE& parameter : parameters) {
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

TransitionEditUVE DrawAnimationTransitionUVE(Scene::AnimationGraphTransitionUVE& transition,
                                             const std::vector<Scene::AnimationParameterUVE>& parameters) {
    TransitionEditUVE edit;
    const auto track = [&edit]() {
        if (ImGui::IsItemActive()) {
            edit.dragging = true;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            edit.released = true;
        }
    };
    const float remove = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;

    // ---- When: every condition must hold (each on its own full-width row) ------------------------
    std::optional<std::size_t> removeCondition;
    LabelUVE("When");
    ImGui::TextDisabled(transition.conditions.empty() ? "always, as soon as it may"
                        : transition.conditions.size() == 1U ? "this holds:"
                                                             : "all of these hold:");
    for (std::size_t index = 0U; index < transition.conditions.size(); ++index) {
        Scene::AnimationTransitionConditionUVE& test = transition.conditions[index];
        ImGui::PushID(static_cast<int>(index));
        const float row = ImGui::GetContentRegionAvail().x - remove - spacing;
        const bool needsParameter = test.condition != Condition::AtEnd && test.condition != Condition::Always;
        const bool needsValue = test.condition == Condition::ParameterGreater || test.condition == Condition::ParameterLess;
        if (needsParameter) {
            ImGui::SetNextItemWidth(needsValue ? row * 0.42F : row * 0.55F);
            const std::initializer_list<ParameterType> types =
                test.condition == Condition::Triggered ? std::initializer_list<ParameterType>{ParameterType::Trigger}
                : needsValue                           ? std::initializer_list<ParameterType>{ParameterType::Float}
                                                       : std::initializer_list<ParameterType>{ParameterType::Bool, ParameterType::Float};
            if (PickParameterUVE("##parameter", parameters, types, "(pick)", test.parameter)) {
                edit.changed = true;
            }
            ImGui::SameLine();
        }
        ImGui::SetNextItemWidth(needsParameter ? (needsValue ? row * 0.33F - spacing : row * 0.45F - spacing) : row);
        if (ImGui::BeginCombo("##test", ConditionLabelUVE(test.condition))) {
            for (const ConditionChoiceUVE& choice : kConditionChoicesUVE) {
                if (ImGui::Selectable(choice.label, choice.condition == test.condition) && choice.condition != test.condition) {
                    test.condition = choice.condition;
                    edit.changed = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", choice.help);
                }
            }
            ImGui::EndCombo();
        }
        if (needsValue) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(row * 0.25F - spacing);
            ImGui::DragFloat("##value", &test.threshold, 0.01F, 0.0F, 0.0F, "%.2f");
            track();
        }
        ImGui::SameLine();
        if (ImGui::Button("x", ImVec2{remove, 0.0F})) {
            removeCondition = index;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Remove this condition");
        }
        ImGui::PopID();
    }
    if (removeCondition.has_value()) {
        transition.conditions.erase(transition.conditions.begin() + static_cast<std::ptrdiff_t>(*removeCondition));
        edit.changed = true;
    }
    if (transition.conditions.size() < Scene::kMaximumAnimationTransitionConditionsUVE && ImGui::SmallButton("+ Condition")) {
        transition.conditions.push_back(Scene::AnimationTransitionConditionUVE{});
        edit.changed = true;
    }
    ImGui::Spacing();

    // ---- How far the state plays first ------------------------------------------------------------
    LabelUVE("Leave after");
    bool waits = transition.exitPhase >= 0.0F;
    if (ImGui::Checkbox("##waits", &waits)) {
        transition.exitPhase = waits ? 0.75F : -1.0F;
        edit.changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Hold the transition until the state has played this far through its cycle,\n"
                          "so a step or a swing finishes before the next state takes over.");
    }
    if (waits) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        float percent = transition.exitPhase * 100.0F;
        ImGui::SliderFloat("##exit", &percent, 0.0F, 100.0F, "%.0f%% of the cycle");
        transition.exitPhase = std::clamp(percent / 100.0F, 0.0F, 1.0F);
        track();
    } else {
        ImGui::SameLine();
        ImGui::TextDisabled("any time");
    }

    // ---- Where the next state starts --------------------------------------------------------------
    static constexpr std::array<const char*, 3> kStarts{"from its start", "in step", "where it was left"};
    static constexpr std::array<const char*, 3> kStartHelp{
        "The state entered plays from its beginning.",
        "The state entered starts at the phase the state left had reached: a walk into a run keeps its feet.",
        "The state entered carries on from where it stopped last time."};
    LabelUVE("Next starts");
    ImGui::SetNextItemWidth(-FLT_MIN);
    const auto start = std::min(static_cast<std::size_t>(transition.start), kStarts.size() - 1U);
    if (ImGui::BeginCombo("##start", kStarts[start])) {
        for (std::size_t option = 0U; option < kStarts.size(); ++option) {
            if (ImGui::Selectable(kStarts[option], option == start) && option != start) {
                transition.start = static_cast<Scene::AnimationTransitionStartUVE>(option);
                edit.changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", kStartHelp[option]);
            }
        }
        ImGui::EndCombo();
    }

    // ---- The hand-over ----------------------------------------------------------------------------
    static constexpr std::array<const char*, 4> kCurves{"linear", "ease in", "ease out", "ease in-out"};
    LabelUVE("Fade");
    const float fadeRow = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(fadeRow * 0.45F);
    ImGui::DragFloat("##fade", &transition.fadeSeconds, 0.005F, 0.0F, 10.0F, transition.fadeSeconds > 0.0F ? "%.2f s" : "cut");
    transition.fadeSeconds = std::clamp(transition.fadeSeconds, 0.0F, 10.0F);
    track();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    const auto curve = std::min(static_cast<std::size_t>(transition.curve), kCurves.size() - 1U);
    ImGui::BeginDisabled(transition.fadeSeconds <= 0.0F);
    if (ImGui::BeginCombo("##curve", kCurves[curve])) {
        for (std::size_t option = 0U; option < kCurves.size(); ++option) {
            if (ImGui::Selectable(kCurves[option], option == curve) && option != curve) {
                transition.curve = static_cast<Scene::AnimationTransitionCurveUVE>(option);
                edit.changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("The crossfade's shape. An inertialized driver blends out the difference instead.");
    }

    LabelUVE("Options");
    if (ImGui::Checkbox("Interruptible", &transition.interruptible)) {
        edit.changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Another transition may take over before this fade ends.");
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("On", &transition.enabled)) {
        edit.changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Off keeps the transition but never takes it: handy while trying things out.");
    }
    return edit;
}

} // namespace UVE::Editor
