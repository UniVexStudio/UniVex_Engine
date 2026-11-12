// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The settings inside each Animation Graph node, Godot style in the blueprint look: what one tunes
// most, right on the node - the clip and its loop and speed, a blend's weight (or the parameter
// that drives it), a one-shot's trigger - so the graph reads and edits without a side trip.

#include "editor_prism_anim_graph_panel.h"

#include "animation_graph_editing.h"
#include "i_asset_database.h"
#include "i_project_file_index.h"

#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Layouts.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace UVE::Editor {
namespace {

using Kind = Scene::AnimationGraphNodeKindUVE;
using ParamType = Scene::AnimationParameterTypeUVE;

constexpr float kRowUVE = 25.0F;

[[nodiscard]] std::size_t BodyRowsUVE(const Kind kind) noexcept {
    switch (kind) {
        case Kind::Output: return 0U;
        case Kind::Slot: return 1U;
        case Kind::StateMachine: return 2U;
        case Kind::BlendSpace1D: return 3U;
        case Kind::Clip:
        case Kind::Blend2:
        case Kind::Additive:
        case Kind::LayeredBlend:
        case Kind::OneShot:
        case Kind::TimeScale:
        case Kind::TimeSeek: return 2U;
        case Kind::Select: return 4U;
        case Kind::BlendSpace2D: return 6U;
    }
    return 0U;
}

/// One labelled row of a node's settings.
[[nodiscard]] prism::Ref<prism::Widget> RowUVE(const std::string& label, const prism::Ref<prism::Widget>& control) {
    auto row = prism::hbox(4.0F);
    auto text = prism::make<prism::Label>(label, prism::FontRole::Small);
    text->setColor(prism::ThemeColor(&prism::Theme::textDim));
    auto textBox = prism::make<prism::SizeBox>();
    textBox->setWidthOverride(52.0F);
    textBox->setContent(text);
    row->add(textBox, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    row->add(control, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    auto sized = prism::make<prism::SizeBox>();
    sized->setHeightOverride(kRowUVE - 3.0F);
    sized->setContent(row);
    return sized;
}

} // namespace

float EditorPrismAnimGraphPanelUVE::BodyHeightUVE(const NodeUVE& node) {
    const std::size_t rows = BodyRowsUVE(node.kind);
    return rows == 0U ? 0.0F : static_cast<float>(rows) * kRowUVE + 8.0F;
}

std::string EditorPrismAnimGraphPanelUVE::SummaryUVE(const NodeUVE& node) {
    char text[96];
    switch (node.kind) {
        case Kind::Clip:
            std::snprintf(text, sizeof(text), "%s  x%.2f", node.loop ? "loop" : "once", static_cast<double>(node.speed));
            return text;
        case Kind::Blend2:
        case Kind::Additive:
        case Kind::LayeredBlend:
        case Kind::BlendSpace1D:
            if (!node.parameter.empty()) {
                return "by " + node.parameter;
            }
            std::snprintf(text, sizeof(text), "at %.2f", static_cast<double>(node.value));
            return text;
        case Kind::BlendSpace2D:
            return "by " + (node.parameter.empty() ? std::string{"x"} : node.parameter) + ", " +
                   (node.parameterY.empty() ? std::string{"y"} : node.parameterY);
        case Kind::OneShot: return node.parameter.empty() ? std::string{"never fires"} : "on " + node.parameter;
        case Kind::TimeScale:
            if (!node.parameter.empty()) {
                return "rate " + node.parameter;
            }
            std::snprintf(text, sizeof(text), "x%.2f", static_cast<double>(node.speed));
            return text;
        case Kind::TimeSeek:
            std::snprintf(text, sizeof(text), "to %.2fs", static_cast<double>(node.value));
            return text + (node.parameter.empty() ? std::string{} : " on " + node.parameter);
        case Kind::Select:
            if (!node.parameter.empty()) {
                return "by " + node.parameter;
            }
            return "option " + std::to_string(std::lround(node.value));
        case Kind::StateMachine: return std::to_string(node.inputs.size()) + " states, " + std::to_string(node.transitions.size()) + " transitions";
        case Kind::Slot: return "slot " + node.parameter;
        case Kind::Output: return {};
    }
    return {};
}

std::vector<std::string> EditorPrismAnimGraphPanelUVE::ParameterNamesUVE(const std::initializer_list<ParamType> types) const {
    std::vector<std::string> names;
    if (const GraphUVE* graph = GraphPtrUVE()) {
        for (const Scene::AnimationParameterUVE& parameter : graph->parameters) {
            if (std::find(types.begin(), types.end(), parameter.type) != types.end()) {
                names.push_back(parameter.name);
            }
        }
    }
    return names;
}

std::vector<Asset::AssetGuidUVE> EditorPrismAnimGraphPanelUVE::ClipChoicesUVE() const {
    // The bound sequencer's animations first, then every other clip in the project.
    std::vector<Asset::AssetGuidUVE> clips = m_editor.GetSequencerClipsUVE(m_editor.ResolveActiveSequencerForGraphUVE(m_view.tree));
    const Asset::ProjectFileSnapshotUVE project = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    for (const Asset::ProjectFileEntryUVE& entry : project.entries) {
        if (entry.kind != Asset::ProjectFileEntryKindUVE::File || entry.relativePath.extension() != ".uvanim") {
            continue;
        }
        const Asset::AssetGuidUVE guid = entry.registeredAssetGuid.has_value()
                                             ? *entry.registeredAssetGuid
                                             : m_editor.m_services->GetAssetDatabaseUVE().RegisterUVE(project.contentRoot / entry.relativePath);
        if (guid != Asset::AssetGuidUVE{} && std::ranges::find(clips, guid) == clips.end()) {
            clips.push_back(guid);
        }
    }
    return clips;
}

prism::Ref<prism::Widget> EditorPrismAnimGraphPanelUVE::BuildBodyUVE(const NodeUVE& node) {
    auto body = prism::vbox(3.0F);
    const std::uint32_t id = node.id;
    const GraphUVE* graph = GraphPtrUVE();

    // "Fixed value" or one of the graph's parameters of these types; picking one drives the setting.
    const auto parameterCombo = [this, id](const std::string& current, const std::initializer_list<ParamType> types, const bool y) {
        std::vector<std::string> names = ParameterNamesUVE(types);
        std::vector<std::string> items{"Fixed value"};
        int selected = 0;
        for (std::size_t i = 0U; i < names.size(); ++i) {
            items.push_back(names[i]);
            if (names[i] == current) {
                selected = static_cast<int>(i) + 1;
            }
        }
        if (!current.empty() && selected == 0) {
            items.push_back(current + " (missing)");
            selected = static_cast<int>(items.size()) - 1;
            names.push_back(current);
        }
        auto combo = prism::make<prism::ComboBox>(items, selected);
        combo->onChange = [this, id, names, y](const int i) {
            const std::string picked = i <= 0 ? std::string{} : names[static_cast<std::size_t>(i - 1)];
            EditNodeUVE(id, [&picked, y](NodeUVE& n) { (y ? n.parameterY : n.parameter) = picked; });
        };
        return combo;
    };
    // A value slider: the node's own value, or - when a parameter drives it - that parameter's.
    const auto valueSlider = [this, id, graph](const float lo, const float hi, const float own, const std::string& parameter, const bool y) {
        std::optional<std::size_t> bound;
        float value = own;
        if (graph != nullptr && !parameter.empty()) {
            for (std::size_t i = 0U; i < graph->parameters.size(); ++i) {
                if (graph->parameters[i].name == parameter) {
                    bound = i;
                    value = graph->parameters[i].value;
                }
            }
        }
        auto slider = prism::make<prism::Slider>(lo, hi, std::clamp(value, lo, hi));
        slider->setDecimals(2);
        if (bound.has_value()) {
            slider->setTooltip("Drives the parameter " + parameter + " (a script sets it in game)");
        }
        slider->onChange = [this, id, bound, y](const float v) {
            if (bound.has_value()) {
                LiveParameterUVE(*bound, v);
            } else {
                LiveNodeUVE(id, [v, y](NodeUVE& n) { (y ? n.valueY : n.value) = v; });
            }
        };
        slider->onCommit = [this](float) { CommitLiveUVE(); };
        return slider;
    };
    const auto number = [this, id](const float value, const float step, const float lo, const float hi, const char* suffix,
                                   const std::function<void(NodeUVE&, float)>& set) {
        auto field = prism::make<prism::NumberField>(value, step, 2);
        field->setRange(lo, hi);
        if (suffix != nullptr) {
            field->setSuffix(suffix);
        }
        field->onChange = [this, id, set](const float v) { LiveNodeUVE(id, [&set, v](NodeUVE& n) { set(n, v); }); };
        field->onCommit = [this](float) { CommitLiveUVE(); };
        return field;
    };
    const auto check = [this, id](const std::string& label, const bool on, const std::function<void(NodeUVE&, bool)>& set) {
        auto box = prism::make<prism::CheckBox>(label, on);
        box->onChange = [this, id, set](const bool v) { EditNodeUVE(id, [&set, v](NodeUVE& n) { set(n, v); }); };
        return box;
    };
    const auto open = [this, id](const std::string& label) {
        auto button = prism::make<prism::Button>(label, prism::Icon::ChevronRight, prism::ButtonStyle::Default);
        button->setTooltip("Open it in its own view (or double-click the node)");
        button->onClick = [this, id] { OpenFocusUVE(id); };
        return button;
    };
    const auto pair = [](const prism::Ref<prism::Widget>& a, const prism::Ref<prism::Widget>& b) {
        auto row = prism::hbox(4.0F);
        row->add(a, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        row->add(b, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        return row;
    };

    switch (node.kind) {
        case Kind::Output: break;
        case Kind::Clip: {
            const std::vector<Asset::AssetGuidUVE> clips = ClipChoicesUVE();
            std::vector<Asset::AssetGuidUVE> choices{Asset::AssetGuidUVE{}};
            std::vector<std::string> names{"None"};
            int selected = 0;
            if (node.clip != Asset::AssetGuidUVE{} && std::ranges::find(clips, node.clip) == clips.end()) {
                choices.push_back(node.clip);
                names.push_back(m_editor.AnimationClipNameUVE(node.clip).empty() ? "(missing clip)" : m_editor.AnimationClipNameUVE(node.clip));
            }
            for (const Asset::AssetGuidUVE guid : clips) {
                choices.push_back(guid);
                names.push_back(m_editor.AnimationClipNameUVE(guid));
            }
            for (std::size_t i = 0U; i < choices.size(); ++i) {
                if (choices[i] == node.clip) {
                    selected = static_cast<int>(i);
                }
            }
            auto combo = prism::make<prism::ComboBox>(names, selected);
            combo->onChange = [this, id, choices](const int i) {
                if (i >= 0 && static_cast<std::size_t>(i) < choices.size()) {
                    const Asset::AssetGuidUVE clip = choices[static_cast<std::size_t>(i)];
                    EditNodeUVE(id, [clip](NodeUVE& n) { n.clip = clip; });
                }
            };
            body->add(RowUVE("Clip", combo));
            body->add(RowUVE("Speed", pair(number(node.speed, 0.01F, -10.0F, 10.0F, "x", [](NodeUVE& n, const float v) { n.speed = v; }),
                                           check("Loop", node.loop, [](NodeUVE& n, const bool v) { n.loop = v; }))));
            break;
        }
        case Kind::Blend2:
        case Kind::Additive:
        case Kind::LayeredBlend: {
            body->add(RowUVE(node.kind == Kind::Additive ? "Amount" : "Weight", valueSlider(0.0F, 1.0F, node.value, node.parameter, false)));
            auto by = parameterCombo(node.parameter, {ParamType::Float}, false);
            if (node.kind == Kind::Blend2) {
                body->add(RowUVE("By", pair(by, check("Sync", node.sync, [](NodeUVE& n, const bool v) { n.sync = v; }))));
            } else {
                body->add(RowUVE("By", by));
            }
            break;
        }
        case Kind::BlendSpace1D:
            body->add(RowUVE("X", valueSlider(node.areaMin.x, node.areaMax.x, node.value, node.parameter, false)));
            body->add(RowUVE("By", parameterCombo(node.parameter, {ParamType::Float}, false)));
            body->add(RowUVE("", open("Open Blend Space")));
            break;
        case Kind::BlendSpace2D: {
            body->add(RowUVE("X", valueSlider(node.areaMin.x, node.areaMax.x, node.value, node.parameter, false)));
            body->add(RowUVE("Y", valueSlider(node.areaMin.y, node.areaMax.y, node.valueY, node.parameterY, true)));
            body->add(RowUVE("X by", parameterCombo(node.parameter, {ParamType::Float}, false)));
            body->add(RowUVE("Y by", parameterCombo(node.parameterY, {ParamType::Float}, true)));
            auto mode = prism::make<prism::ComboBox>(std::vector<std::string>{"Blend", "Nearest", "Nearest, in step"},
                                                     static_cast<int>(node.blendMode));
            mode->onChange = [this, id](const int i) {
                EditNodeUVE(id, [i](NodeUVE& n) { n.blendMode = static_cast<Scene::AnimationBlendModeUVE>(std::clamp(i, 0, 2)); });
            };
            body->add(RowUVE("Mode", mode));
            body->add(RowUVE("", open("Open Blend Space")));
            break;
        }
        case Kind::OneShot:
            body->add(RowUVE("Fire on", parameterCombo(node.parameter, {ParamType::Trigger, ParamType::Bool}, false)));
            body->add(RowUVE("Fade", number(node.fadeSeconds, 0.01F, 0.0F, 5.0F, " s", [](NodeUVE& n, const float v) { n.fadeSeconds = v; })));
            break;
        case Kind::TimeScale:
            body->add(RowUVE("Rate", number(node.speed, 0.01F, -10.0F, 10.0F, "x", [](NodeUVE& n, const float v) { n.speed = v; })));
            body->add(RowUVE("By", parameterCombo(node.parameter, {ParamType::Float}, false)));
            break;
        case Kind::TimeSeek:
            body->add(RowUVE("On", parameterCombo(node.parameter, {ParamType::Trigger, ParamType::Bool}, false)));
            body->add(RowUVE("To", number(node.value, 0.01F, 0.0F, 3600.0F, " s", [](NodeUVE& n, const float v) { n.value = v; })));
            break;
        case Kind::Select: {
            const float last = static_cast<float>(std::max<std::size_t>(1U, node.inputs.size()) - 1U);
            auto option = prism::make<prism::NumberField>(std::round(node.value), 1.0F, 0);
            option->setRange(0.0F, last);
            option->onChange = [this, id](const float v) { LiveNodeUVE(id, [v](NodeUVE& n) { n.value = std::round(v); }); };
            option->onCommit = [this](float) { CommitLiveUVE(); };
            body->add(RowUVE("Option", option));
            body->add(RowUVE("By", parameterCombo(node.parameter, {ParamType::Float}, false)));
            body->add(RowUVE("Fade", pair(number(node.fadeSeconds, 0.01F, 0.0F, 5.0F, " s", [](NodeUVE& n, const float v) { n.fadeSeconds = v; }),
                                          check("Restart", node.restart, [](NodeUVE& n, const bool v) { n.restart = v; }))));
            auto more = prism::make<prism::Button>("Option", prism::Icon::Plus, prism::ButtonStyle::Flat);
            more->setTooltip("Add an input");
            more->onClick = [this, id] { EditUVE([id](GraphUVE& g) { static_cast<void>(AddAnimationGraphInputSlotUVE(g.nodes, id)); }); };
            auto less = prism::make<prism::Button>("Option", prism::Icon::Minus, prism::ButtonStyle::Flat);
            less->setTooltip("Remove the last input");
            const std::size_t slots = node.inputs.size();
            less->setEnabled(slots > 2U);
            less->onClick = [this, id, slots] {
                if (slots > 0U) {
                    EditUVE([id, slots](GraphUVE& g) { static_cast<void>(RemoveAnimationGraphInputSlotUVE(g.nodes, id, slots - 1U)); });
                }
            };
            auto buttons = prism::hbox(4.0F);
            buttons->add(more, prism::Slot{1.0F});
            buttons->add(less, prism::Slot{1.0F});
            body->add(RowUVE("", buttons));
            break;
        }
        case Kind::StateMachine: {
            std::vector<std::string> states;
            for (std::size_t slot = 0U; slot < node.inputs.size(); ++slot) {
                std::string name = AnimationGraphSlotLabelUVE(node.kind, slot);
                if (graph != nullptr && node.inputs[slot] != 0U) {
                    if (const auto child = std::ranges::find(graph->nodes, node.inputs[slot], &NodeUVE::id);
                        child != graph->nodes.end() && !child->name.empty()) {
                        name = child->name;
                    }
                }
                states.push_back(name);
            }
            if (states.empty()) {
                states.push_back("(no states)");
            }
            auto entry = prism::make<prism::ComboBox>(states, static_cast<int>(std::min<std::size_t>(node.entryState, states.size() - 1U)));
            entry->onChange = [this, id](const int i) { EditNodeUVE(id, [i](NodeUVE& n) { n.entryState = static_cast<std::uint32_t>(std::max(0, i)); }); };
            body->add(RowUVE("Entry", entry));
            body->add(RowUVE("", open("Open States")));
            break;
        }
        case Kind::Slot: {
            auto name = prism::make<prism::TextInput>(node.parameter, "DefaultSlot");
            name->setTooltip("The montage slot laid over the input here (a MontagePlayer plays into it)");
            name->onCommit = [this, id](const std::string& to) { EditNodeUVE(id, [&to](NodeUVE& n) { n.parameter = to; }); };
            body->add(RowUVE("Slot", name));
            break;
        }
    }
    return body;
}

} // namespace UVE::Editor
