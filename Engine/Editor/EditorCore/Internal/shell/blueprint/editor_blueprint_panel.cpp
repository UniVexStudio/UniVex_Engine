// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_blueprint_panel.h"

#include "character_controller_component.h"

#include "editor_fit_width.h"

#include "blueprint_nodes.h"
#include "widget_blueprint_asset.h"
#include "i_entity_manager.h"
#include "i_file_system.h"
#include "i_project_file_index.h"
#include "name_component.h"
#include "scene_object_registry.h"
#include "scene_object_type.h"
#include "script_component.h"

#include "prism/Application.h"
#include "prism/Containers.h"
#include "prism/Layouts.h"
#include "prism/Controls.h"
#include "prism/Menu.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"
#include "prism/Views.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace UVE::Editor {
namespace {

using T = Asset::BlueprintPinTypeUVE;
using PT = prism::BlueprintPinType;
using Role = Asset::BlueprintNodeRoleUVE;

[[nodiscard]] PT ToPrismUVE(const T type) {
    switch (type) {
        case T::Exec: return PT::Exec;
        case T::Bool: return PT::Bool;
        case T::Int: return PT::Int;
        case T::Float: return PT::Float;
        case T::Vector: return PT::Vector;
        case T::String: return PT::String;
        case T::Object: return PT::Object;
    }
    return PT::Float;
}

[[nodiscard]] T FromPrismUVE(const PT type) {
    switch (type) {
        case PT::Bool: return T::Bool;
        case PT::Int: return T::Int;
        case PT::Vector:
        case PT::Color: return T::Vector;
        case PT::String:
        case PT::Name: return T::String;
        case PT::Object: return T::Object;
        default: return T::Float;
    }
}

[[nodiscard]] prism::BlueprintNodeCategory CategoryOfUVE(const Asset::BlueprintNodeTypeUVE& type) {
    switch (type.role) {
        case Role::Event: return prism::BlueprintNodeCategory::Event;
        case Role::Pure: return prism::BlueprintNodeCategory::PureMath;
        case Role::GetVariable:
        case Role::SetVariable: return prism::BlueprintNodeCategory::Variable;
        case Role::Action: return prism::BlueprintNodeCategory::Function;
        default: return prism::BlueprintNodeCategory::Flow;
    }
}

/// A graph node of `type` (for Get / Set, of `variable`), pins as the engine defines them.
[[nodiscard]] prism::BlueprintNode MakeNodeUVE(const Asset::BlueprintNodeTypeUVE& type, const Asset::BlueprintAssetUVE& blueprint,
                                               const std::string& variable) {
    prism::BlueprintNode node;
    node.category = CategoryOfUVE(type);
    node.tooltip = type.tooltip;
    if (type.role == Role::GetVariable) {
        node.title = variable;
        node.style = prism::BlueprintNodeStyle::Compact;
    } else if (type.role == Role::SetVariable) {
        node.title = "Set " + variable;
    } else {
        node.title = type.title;
        const std::size_t bar = type.category.find('|');
        node.subtitle = bar == std::string::npos ? type.category : type.category.substr(bar + 1U);
    }
    Asset::BlueprintNodeUVE probe;
    probe.type = type.key;
    probe.variable = variable;
    const Asset::BlueprintNodePinsUVE pins = Asset::ResolveBlueprintNodePinsUVE(blueprint, probe);
    const auto pinOf = [](const Asset::BlueprintPinSpecUVE& spec, const prism::BlueprintPinKind kind) {
        prism::BlueprintPin pin;
        pin.name = spec.type == T::Exec && spec.name.empty() ? std::string("Exec") : spec.name;
        pin.hideLabel = spec.type == T::Exec && spec.name.empty();
        pin.type = ToPrismUVE(spec.type);
        // A colour is picked, not typed: the colour picker for a Vector named for a colour.
        if (spec.type == T::Vector && (spec.name == "Color" || spec.name == "Colour" || spec.name == "Tint")) {
            pin.type = PT::Color;
        }
        pin.kind = kind;
        pin.defaultValue = kind == prism::BlueprintPinKind::Input ? spec.defaultText : std::string{};
        // Text and numbers keep their box even when cleared, so they can always be typed into.
        pin.editable = kind == prism::BlueprintPinKind::Input && spec.type != T::Exec && spec.type != T::Object;
        return pin;
    };
    for (const Asset::BlueprintPinSpecUVE& spec : pins.inputs) {
        node.inputs.push_back(pinOf(spec, prism::BlueprintPinKind::Input));
    }
    for (const Asset::BlueprintPinSpecUVE& spec : pins.outputs) {
        node.outputs.push_back(pinOf(spec, prism::BlueprintPinKind::Output));
    }
    if (type.role == Role::GetVariable && !node.outputs.empty()) {
        node.outputs.front().hideLabel = true;
    }
    return node;
}

[[nodiscard]] prism::Color ColorOfUVE(const std::uint32_t rgba) {
    return prism::Color(static_cast<std::uint8_t>(rgba >> 24U), static_cast<std::uint8_t>(rgba >> 16U), static_cast<std::uint8_t>(rgba >> 8U),
                        static_cast<std::uint8_t>(rgba));
}

[[nodiscard]] std::uint32_t RgbaOfUVE(const prism::Color c) {
    return (static_cast<std::uint32_t>(c.r) << 24U) | (static_cast<std::uint32_t>(c.g) << 16U) | (static_cast<std::uint32_t>(c.b) << 8U) |
           static_cast<std::uint32_t>(c.a);
}

[[nodiscard]] const char* TypeNameUVE(const PT type) {
    switch (type) {
        case PT::Bool: return "Bool";
        case PT::Int: return "Int";
        case PT::Vector:
        case PT::Color: return "Vector";
        case PT::String:
        case PT::Name: return "String";
        case PT::Object: return "Object";
        default: return "Float";
    }
}

/// The kinds of widget a UI node acts on (empty: not a UI node, or any widget).
[[nodiscard]] std::vector<WidgetKindUVE> WidgetKindsForUVE(const std::string& key) {
    using K = WidgetKindUVE;
    if (key == "ui_set_text" || key == "ui_get_text") return {K::Text, K::TextBox, K::Button};
    if (key == "ui_on_clicked") return {K::Button};
    if (key == "ui_on_committed") return {K::TextBox};
    if (key == "ui_on_changed") return {K::Slider, K::CheckBox, K::ComboBox};
    if (key == "ui_set_percent" || key == "ui_get_value") return {K::ProgressBar, K::Slider};
    if (key == "ui_is_checked" || key == "ui_set_checked") return {K::CheckBox};
    if (key == "ui_get_selected_option" || key == "ui_get_selected_index" || key == "ui_set_selected_index" || key == "ui_add_option")
        return {K::ComboBox};
    if (key == "ui_show_screen") return {K::Canvas};
    return {};
}

[[nodiscard]] bool IsWidgetNodeUVE(const std::string& key) {
    return key == "ui_set_visibility" || key == "ui_set_color" || !WidgetKindsForUVE(key).empty();
}

/// The node's widget input: its first input that is not an exec pin.
[[nodiscard]] int WidgetPinOfUVE(const prism::BlueprintNode& node) {
    for (int i = 0; i < static_cast<int>(node.inputs.size()); ++i) {
        if (node.inputs[static_cast<std::size_t>(i)].type != PT::Exec) {
            return i;
        }
    }
    return -1;
}

} // namespace

EditorBlueprintPanelUVE::EditorBlueprintPanelUVE(EditorUVE& editor) : m_editor(editor) {}

std::string EditorBlueprintPanelUVE::NodeKeyUVE(const prism::BlueprintNode& node) {
    static const std::map<std::string, std::string> byTitle = [] {
        std::map<std::string, std::string> out;
        for (const Asset::BlueprintNodeTypeUVE& type : Asset::GetBlueprintNodeTypesUVE()) {
            out[type.title] = type.key;
        }
        return out;
    }();
    const auto found = byTitle.find(node.title);
    return found == byTitle.end() ? std::string{} : found->second;
}

std::vector<std::string> EditorBlueprintPanelUVE::WidgetChoicesUVE(const std::string& key) const {
    std::vector<std::string> names;
    if (!widgetsProvider || !IsWidgetNodeUVE(key)) {
        return names;
    }
    const std::vector<WidgetKindUVE> kinds = WidgetKindsForUVE(key);
    for (const auto& [name, kind] : widgetsProvider()) {
        const bool screen = kind == WidgetKindUVE::Canvas;
        const bool wantsScreen = key == "ui_show_screen";
        if (screen != wantsScreen || (!kinds.empty() && std::find(kinds.begin(), kinds.end(), kind) == kinds.end())) {
            continue;
        }
        if (std::find(names.begin(), names.end(), name) == names.end()) {
            names.push_back(name);
        }
    }
    return names;
}

prism::Ref<prism::Widget> EditorBlueprintPanelUVE::BuildUVE() {
    m_graph = prism::make<prism::BlueprintGraph>();
    // PRISM's whole blueprint design: its toolbar, minimap, comment boxes, watermark, shortcuts and
    // its own undo. The host owns the file and the compiler.
    prism::BlueprintGraph::Features features;
    features.toolbar = false;   // Compile, Save, Find, Diff, Simulate: the Entity Editor's own rows
    features.crumbs = false;   // one graph type here; no switching to the demo presets
    features.minimap = true;
    features.watermark = true;
    features.shortcuts = true;
    features.history = true;
    features.comments = true;
    m_graph->setFeatures(features);
    m_graph->setPreset(prism::BlueprintGraphPreset::EventBlueprint);   // not PRISM's demo Widget Blueprint
    m_graph->setWatermark("Blueprint Class", "Gameplay graph", prism::Icon::BlueprintClass);
    m_graph->clear();
    m_graph->setNodeLibrary(LibraryUVE());
    // A widget pin lists the widget's own widgets of the kind the node acts on.
    m_graph->pinChoices = [this](const prism::BlueprintNode& node, const int pin) -> std::vector<std::string> {
        // An Anim node's Parameter pin lists the entity's Animation Graph parameters.
        if (animParametersProvider && NodeKeyUVE(node).rfind("anim_", 0U) == 0U && pin >= 0 &&
            pin < static_cast<int>(node.inputs.size()) && node.inputs[static_cast<std::size_t>(pin)].name == "Parameter") {
            return animParametersProvider();
        }
        if (pin != WidgetPinOfUVE(node)) {
            return {};
        }
        return WidgetChoicesUVE(NodeKeyUVE(node));
    };
    m_graph->onSelectionChanged = [this](const std::vector<int>&) { m_sideDirty = true; };
    m_graph->onSaveRequested = [this] { static_cast<void>(SaveUVE()); };
    m_graph->onCompileRequested = [this] { CompileUVE(); };
    m_graph->onCommit = [this](const std::string&) {
        if (!m_loading) {
            m_dirty = true;
            m_graph->clearCompileResult();
        }
    };
    m_graph->onChanged = [this] {
        if (!m_loading) {
            m_dirty = true;
            m_sideDirty = true;
        }
    };

    m_side = prism::make<prism::ScrollBox>();
    m_sidePanel = m_side;
    RebuildSideUVE();
    return m_graph;
}

std::vector<prism::BlueprintNodeSpec> EditorBlueprintPanelUVE::LibraryUVE() const {
    // The engine's palette, then a Get and a Set for each variable.
    std::vector<prism::BlueprintNodeSpec> library;
    const Asset::BlueprintAssetUVE blueprint = m_graph ? FromGraphUVE() : Asset::BlueprintAssetUVE{};
    // Only what belongs in this graph: a widget's controls in a widget's graph, an object's
    // movement and physics in an object's, and nothing the object lacks the component for.
    const Asset::BlueprintGraphKindUVE graphKind =
        Asset::IsWidgetBlueprintPathUVE(m_path) ? Asset::BlueprintGraphKindUVE::Widget : Asset::BlueprintGraphKindUVE::Object;
    for (const Asset::BlueprintNodeTypeUVE& type : Asset::GetBlueprintNodeTypesUVE()) {
        if (type.role == Role::GetVariable || type.role == Role::SetVariable) {
            continue;
        }
        if (!Asset::IsBlueprintNodeForGraphUVE(type, graphKind) || (!type.needs.empty() && hasComponent && !hasComponent(type.needs))) {
            continue;
        }
        prism::BlueprintNodeSpec spec;
        spec.category = type.category;
        spec.name = type.title;
        spec.keywords = type.keywords + " " + type.tooltip;
        spec.make = [this, &type, blueprint] {
            prism::BlueprintNode node = MakeNodeUVE(type, blueprint, {});
            // A UI node starts on the first widget it can act on, not a placeholder name.
            const std::vector<std::string> widgets = WidgetChoicesUVE(type.key);
            const int pin = WidgetPinOfUVE(node);
            if (!widgets.empty() && pin >= 0 && IsWidgetNodeUVE(type.key)) {
                const std::string& current = node.inputs[static_cast<std::size_t>(pin)].defaultValue;
                if (std::find(widgets.begin(), widgets.end(), current) == widgets.end()) {
                    node.inputs[static_cast<std::size_t>(pin)].defaultValue = widgets.front();
                }
            }
            return node;
        };
        library.push_back(std::move(spec));
    }
    if (widgetsProvider) {
        // Widgets, one sub-category each: its events and calls, already pointed at it by name.
        using K = WidgetKindUVE;
        const auto keysFor = [](const K kind) -> std::vector<const char*> {
            switch (kind) {
                case K::Button: return {"ui_on_clicked", "ui_set_visibility", "ui_set_color"};
                case K::Text: return {"ui_set_text", "ui_get_text", "ui_set_color", "ui_set_visibility"};
                case K::Image:
                case K::Border: return {"ui_set_color", "ui_set_visibility"};
                case K::ProgressBar: return {"ui_set_percent", "ui_get_value", "ui_set_visibility"};
                case K::Slider: return {"ui_on_changed", "ui_get_value", "ui_set_percent", "ui_set_visibility"};
                case K::CheckBox: return {"ui_on_changed", "ui_is_checked", "ui_set_checked", "ui_set_visibility"};
                case K::TextBox: return {"ui_on_committed", "ui_get_text", "ui_set_text", "ui_set_visibility"};
                case K::ComboBox:
                    return {"ui_on_changed", "ui_get_selected_option", "ui_get_selected_index", "ui_set_selected_index", "ui_add_option",
                            "ui_set_visibility"};
                case K::Canvas: return {"ui_show_screen"};
                default: return {"ui_set_visibility"};
            }
        };
        for (const auto& [name, kind] : widgetsProvider()) {
            for (const char* key : keysFor(kind)) {
                const Asset::BlueprintNodeTypeUVE* const type = Asset::FindBlueprintNodeTypeUVE(key);
                if (type == nullptr) {
                    continue;
                }
                prism::BlueprintNodeSpec spec;
                spec.category = std::string("Widgets|") + name;
                spec.name = type->title + " (" + name + ")";
                spec.keywords = name + " " + WidgetKindLabelUVE(kind) + " " + type->keywords;
                spec.make = [type, blueprint, name = name] {
                    prism::BlueprintNode node = MakeNodeUVE(*type, blueprint, {});
                    // The widget pin is the first that is not an exec pin.
                    for (prism::BlueprintPin& pin : node.inputs) {
                        if (pin.type != PT::Exec) {
                            pin.defaultValue = name;
                            break;
                        }
                    }
                    return node;
                };
                library.push_back(std::move(spec));
            }
        }
    }
    if (m_graph) {
        const Asset::BlueprintNodeTypeUVE* const get = Asset::FindBlueprintNodeTypeUVE("get_variable");
        const Asset::BlueprintNodeTypeUVE* const set = Asset::FindBlueprintNodeTypeUVE("set_variable");
        for (const prism::BlueprintVariable& variable : m_graph->variables()) {
            const std::string name = variable.name;
            library.push_back(prism::BlueprintNodeSpec{"Variables", "Get " + name, "variable read",
                                                       [get, blueprint, name] { return MakeNodeUVE(*get, blueprint, name); }});
            library.push_back(prism::BlueprintNodeSpec{"Variables", "Set " + name, "variable write assign",
                                                       [set, blueprint, name] { return MakeNodeUVE(*set, blueprint, name); }});
        }
    }
    return library;
}

Asset::BlueprintAssetUVE EditorBlueprintPanelUVE::FromGraphUVE() const {
    Asset::BlueprintAssetUVE out;
    out.parentKind = ParentKindUVE();
    if (!m_graph) {
        return out;
    }
    for (const prism::BlueprintVariable& variable : m_graph->variables()) {
        out.variables.push_back(Asset::BlueprintVariableUVE{variable.name, FromPrismUVE(variable.type), variable.value, m_exposed.contains(variable.name)});
    }
    std::map<std::string, std::string> byTitle;
    for (const Asset::BlueprintNodeTypeUVE& type : Asset::GetBlueprintNodeTypesUVE()) {
        byTitle[type.title] = type.key;
    }
    std::set<int> kept;
    for (const prism::BlueprintNode& n : m_graph->nodes()) {
        Asset::BlueprintNodeUVE node;
        node.id = static_cast<std::uint32_t>(n.id);
        node.x = n.pos.x;
        node.y = n.pos.y;
        if (n.category == prism::BlueprintNodeCategory::Variable) {
            const bool setter = n.title.rfind("Set ", 0U) == 0U;
            node.type = setter ? "set_variable" : "get_variable";
            node.variable = setter ? n.title.substr(4U) : n.title;
        } else if (const auto found = byTitle.find(n.title); found != byTitle.end()) {
            node.type = found->second;
        } else {
            continue;   // not one of the engine's (a reroute knot): not saved
        }
        for (const prism::BlueprintPin& pin : n.inputs) {
            node.inputValues.push_back(pin.defaultValue);
        }
        kept.insert(n.id);
        out.nodes.push_back(std::move(node));
    }
    for (const prism::BlueprintWire& w : m_graph->wires()) {
        if (kept.contains(w.fromNodeId) && kept.contains(w.toNodeId)) {
            out.wires.push_back(Asset::BlueprintWireUVE{static_cast<std::uint32_t>(w.fromNodeId), static_cast<std::uint32_t>(w.fromPinIndex),
                                                        static_cast<std::uint32_t>(w.toNodeId), static_cast<std::uint32_t>(w.toPinIndex)});
        }
    }
    for (const prism::BlueprintComment& c : m_graph->comments()) {
        out.comments.push_back(Asset::BlueprintCommentUVE{c.title, c.bounds.x, c.bounds.y, c.bounds.w, c.bounds.h, RgbaOfUVE(c.color)});
    }
    return out;
}

void EditorBlueprintPanelUVE::LoadIntoGraphUVE(const Asset::BlueprintAssetUVE& blueprint) {
    if (!m_graph) {
        return;
    }
    m_loading = true;
    m_graph->clear();
    m_exposed.clear();
    for (const Asset::BlueprintVariableUVE& variable : blueprint.variables) {
        static_cast<void>(m_graph->addVariable(variable.name, ToPrismUVE(variable.type), variable.defaultText));
        if (variable.exposed) {
            m_exposed.insert(variable.name);
        }
    }
    for (const Asset::BlueprintNodeUVE& node : blueprint.nodes) {
        const Asset::BlueprintNodeTypeUVE* const type = Asset::FindBlueprintNodeTypeUVE(node.type);
        if (type == nullptr) {
            continue;
        }
        prism::BlueprintNode shown = MakeNodeUVE(*type, blueprint, node.variable);
        shown.id = static_cast<int>(node.id);
        shown.pos = prism::Vec2{node.x, node.y};
        // As saved, empty included: a value cleared on purpose stays cleared.
        for (std::size_t i = 0U; i < shown.inputs.size() && i < node.inputValues.size(); ++i) {
            if (shown.inputs[i].type != PT::Exec) {
                shown.inputs[i].defaultValue = node.inputValues[i];
            }
        }
        m_graph->addNode(shown);
    }
    for (const Asset::BlueprintWireUVE& wire : blueprint.wires) {
        const prism::BlueprintNode* const from = m_graph->findNode(static_cast<int>(wire.fromNode));
        if (from == nullptr || wire.fromPin >= from->outputs.size()) {
            continue;
        }
        m_graph->connectPins(static_cast<int>(wire.fromNode), static_cast<int>(wire.fromPin), static_cast<int>(wire.toNode),
                             static_cast<int>(wire.toPin), from->outputs[wire.fromPin].type);
    }
    for (const Asset::BlueprintCommentUVE& comment : blueprint.comments) {
        prism::BlueprintComment box;
        box.title = comment.title;
        box.bounds = prism::Rect{comment.x, comment.y, comment.width, comment.height};
        box.color = ColorOfUVE(comment.color);
        m_graph->addComment(box);
    }
    m_graph->setNodeLibrary(LibraryUVE());
    m_graph->clearCompileResult();
    m_graph->commit("Open");
    m_graph->frameAll();
    m_loading = false;
    m_dirty = false;
    m_sideDirty = true;
}

std::string EditorBlueprintPanelUVE::ParentKindUVE() const {
    const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    if (m_target == Scene::kInvalidEntityUVE || !em.IsAliveUVE(m_target)) {
        return "Object3D";
    }
    const Scene::Objects::SceneObjectDescriptorUVE* const descriptor =
        Scene::Objects::FindSceneObjectDescriptorUVE(Scene::ResolveSceneObjectKindUVE(em, m_target));
    return descriptor != nullptr ? std::string(descriptor->displayName) : std::string("Object3D");
}

std::string EditorBlueprintPanelUVE::ProjectPathUVE(const std::filesystem::path& absolute) const {
    // The project root is the VFS's root mount: what a probe path resolves under.
    const std::filesystem::path probe = m_editor.m_services->GetFileSystemUVE().ResolveRealPathUVE("uve_root_probe");
    std::error_code error;
    if (!probe.empty()) {
        const std::filesystem::path root = std::filesystem::weakly_canonical(probe.parent_path(), error);
        const std::filesystem::path relative = std::filesystem::weakly_canonical(absolute, error).lexically_relative(root);
        if (!relative.empty() && *relative.begin() != "..") {
            return relative.generic_string();
        }
    }
    const std::filesystem::path content = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot;
    const std::filesystem::path relative = absolute.lexically_relative(content.parent_path());
    return relative.empty() || *relative.begin() == ".." ? absolute.filename().generic_string() : relative.generic_string();
}

bool EditorBlueprintPanelUVE::OpenUVE(const std::string& path, const Scene::EntityUVE target) {
    std::filesystem::path real = m_editor.m_services->GetFileSystemUVE().ResolveRealPathUVE(path);
    if (real.empty()) {
        real = path;
    }
    Asset::BlueprintAssetUVE blueprint;
    // A Widget Blueprint keeps its graph beside its widgets, in the same file.
    if (Asset::IsWidgetBlueprintPathUVE(path)) {
        if (std::optional<Asset::BlueprintAssetUVE> graph = Asset::LoadWidgetBlueprintGraphUVE(real)) {
            blueprint = std::move(*graph);
        } else {
            m_editor.m_contentStatusMessage = "Could not open the Widget Blueprint " + path + ".";
            return false;
        }
    } else if (!Asset::LoadBlueprintAssetUVE(real, blueprint)) {
        m_editor.m_contentStatusMessage = "Could not open the Blueprint " + path + ".";
        return false;
    }
    m_path = path;
    m_target = target;
    if (m_graph) {
        if (Asset::IsWidgetBlueprintPathUVE(path)) {
            m_graph->setWatermark("Widget Blueprint", "UI graph", prism::Icon::BlueprintClass);
        } else {
            m_graph->setWatermark("Blueprint Class", "Gameplay graph", prism::Icon::BlueprintClass);
        }
    }
    LoadIntoGraphUVE(blueprint);
    return true;
}

void EditorBlueprintPanelUVE::CloseUVE() {
    m_path.clear();
    m_target = Scene::kInvalidEntityUVE;
    m_dirty = false;
    if (m_graph) {
        m_loading = true;
        m_graph->clear();
        m_loading = false;
    }
    m_sideDirty = true;
}

std::optional<Asset::BlueprintAssetUVE> EditorBlueprintPanelUVE::UnsavedForUVE(const std::string& path) const {
    if (!IsOpenUVE() || path != m_path) {
        return std::nullopt;
    }
    return FromGraphUVE();
}

bool EditorBlueprintPanelUVE::SaveUVE() {
    if (!IsOpenUVE()) {
        return false;
    }
    std::filesystem::path real = m_editor.m_services->GetFileSystemUVE().ResolveRealPathUVE(m_path);
    if (real.empty()) {
        real = m_path;
    }
    const bool saved = Asset::IsWidgetBlueprintPathUVE(m_path) ? Asset::SaveWidgetBlueprintGraphUVE(real, FromGraphUVE())
                                                                : Asset::SaveBlueprintAssetUVE(FromGraphUVE(), real);
    if (!saved) {
        if (prism::Application::exists()) {
            prism::Application::get().notify("Could not save " + m_path, prism::ToastLevel::Error);
        }
        return false;
    }
    m_dirty = false;
    m_editor.RefreshProjectFileIndexUVE();
    if (prism::Application::exists()) {
        prism::Application::get().notify("Saved " + std::filesystem::path(m_path).filename().string(), prism::ToastLevel::Success, 1500);
    }
    return true;
}

void EditorBlueprintPanelUVE::CompileUVE() {
    if (!IsOpenUVE() || !m_graph) {
        return;
    }
    static_cast<void>(m_editor.CompileEntityEditorUVE());
    prism::BlueprintCompileReport report;
    if (m_editor.m_entityEditSession.has_value()) {
        for (const EditorUVE::EntityCompileProblemUVE& problem : m_editor.m_entityEditSession->problems) {
            if (problem.scriptPath != m_path) {
                continue;
            }
            ++report.errors;
            report.messages.push_back(problem.message);
            if (problem.blueprintNode != 0U) {
                report.nodeIssues[static_cast<int>(problem.blueprintNode)] = problem.message;
            }
        }
    }
    // A widget node names a widget the widget has.
    if (widgetsProvider) {
        std::vector<std::string> all;
        for (const auto& entry : widgetsProvider()) {
            all.push_back(entry.first);
        }
        for (const prism::BlueprintNode& node : m_graph->nodes()) {
            const std::string key = NodeKeyUVE(node);
            const int pin = WidgetPinOfUVE(node);
            if (!IsWidgetNodeUVE(key) || pin < 0 || m_graph->isPinConnected(node.id, pin, true)) {
                continue;
            }
            const std::string& name = node.inputs[static_cast<std::size_t>(pin)].defaultValue;
            const bool anyAllowed = key == "ui_on_clicked" || key == "ui_on_changed" || key == "ui_on_committed";
            if ((name.empty() && anyAllowed) || std::find(all.begin(), all.end(), name) != all.end()) {
                continue;
            }
            const std::string message = node.title + ": the widget has no " + (name.empty() ? std::string("widget named here") : "widget named \"" + name + "\"");
            ++report.errors;
            report.messages.push_back(message);
            report.nodeIssues[node.id] = message;
        }
    }
    m_graph->setCompileResult(std::move(report));
}

void EditorBlueprintPanelUVE::SelectNodeUVE(const std::uint32_t node) {
    if (!m_graph || m_graph->findNode(static_cast<int>(node)) == nullptr) {
        return;
    }
    m_graph->clearSelection();
    m_graph->select(static_cast<int>(node));
    m_graph->frameSelection();
}

void EditorBlueprintPanelUVE::RefreshUVE() {
    if (IsOpenUVE()) {
        const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
        if (!em.IsAliveUVE(m_target)) {
            // The scene was rebuilt (Simulate stopped, Revert): the object whose Script slot names
            // this Blueprint is the same object under a new handle - the graph stays open on it.
            m_target = FindUserUVE(m_path);
            if (m_target == Scene::kInvalidEntityUVE) {
                if (DirtyUVE()) {
                    static_cast<void>(SaveUVE());   // its edits are kept even when its object is gone
                }
                CloseUVE();
            }
        }
    }
    if (!m_graph) {
        return;
    }
    // The variables changed (here or in PRISM's Variables window): Details and the palette follow.
    const auto& now = m_graph->variables();
    bool same = now.size() == m_shownVariables.size() && m_exposed == m_shownExposed;
    for (std::size_t i = 0U; same && i < now.size(); ++i) {
        same = now[i].name == m_shownVariables[i].name && now[i].type == m_shownVariables[i].type && now[i].value == m_shownVariables[i].value;
    }
    if (!same || m_sideDirty) {
        m_shownVariables = now;
        m_shownExposed = m_exposed;
        m_graph->setNodeLibrary(LibraryUVE());
        RebuildSideUVE();
    }
}

void EditorBlueprintPanelUVE::AddNodeDetailsUVE(prism::PropertyGrid& grid, const prism::BlueprintNode& node) {
    const std::string key = NodeKeyUVE(node);
    const Asset::BlueprintNodeTypeUVE* const type = key.empty() ? nullptr : Asset::FindBlueprintNodeTypeUVE(key);
    const int cat = grid.addCategory("Node: " + node.title);
    if (type != nullptr) {
        auto category = prism::make<prism::Label>(type->category);
        category->setColor(prism::ThemeColor(&prism::Theme::textDim));
        grid.addRow(cat, "Category", category);
        if (!type->tooltip.empty()) {
            auto about = prism::make<prism::Label>(type->tooltip, prism::FontRole::Small);
            about->setColor(prism::ThemeColor(&prism::Theme::textDim));
            grid.addRow(cat, "Does", about, type->tooltip);
        }
    }
    const int nodeId = node.id;
    const int widgetPin = WidgetPinOfUVE(node);
    for (int i = 0; i < static_cast<int>(node.inputs.size()); ++i) {
        const prism::BlueprintPin& pin = node.inputs[static_cast<std::size_t>(i)];
        if (pin.type == PT::Exec) {
            continue;
        }
        if (m_graph->isPinConnected(nodeId, i, true)) {
            auto wired = prism::make<prism::Label>("(wired)");
            wired->setColor(prism::ThemeColor(&prism::Theme::textDim));
            grid.addRow(cat, pin.name, wired, "Takes its value from the wire");
            continue;
        }
        const auto set = [this, nodeId, i](const std::string& value) {
            if (m_graph && m_graph->setPinDefault(nodeId, i, value)) {
                m_dirty = true;
                m_sideDirty = true;
            } else if (prism::Application::exists()) {
                prism::Application::get().notify("'" + value + "' is not a value this input takes", prism::ToastLevel::Warning, 1800);
            }
        };
        prism::Ref<prism::Widget> editor;
        const std::vector<std::string> choices = i == widgetPin ? WidgetChoicesUVE(key) : std::vector<std::string>{};
        if (!choices.empty()) {
            std::vector<std::string> items = choices;
            int current = -1;
            for (std::size_t c = 0U; c < items.size(); ++c) {
                current = items[c] == pin.defaultValue ? static_cast<int>(c) : current;
            }
            if (current < 0) {
                items.insert(items.begin(), pin.defaultValue.empty() ? std::string("(any)") : pin.defaultValue + "  (missing)");
                current = 0;
            }
            const bool extra = items.size() > choices.size();
            auto combo = prism::make<prism::ComboBox>(items, current);
            combo->onChange = [set, choices, extra](const int v) {
                const int at = v - (extra ? 1 : 0);
                if (at >= 0 && static_cast<std::size_t>(at) < choices.size()) {
                    set(choices[static_cast<std::size_t>(at)]);
                }
            };
            editor = combo;
        } else if (pin.type == PT::Bool) {
            auto check = prism::make<prism::CheckBox>("", pin.defaultValue == "true");
            check->onChange = [set](const bool on) { set(on ? "true" : "false"); };
            editor = check;
        } else if (pin.type == PT::Float || pin.type == PT::Int) {
            float value = 0.0F;
            try {
                value = pin.defaultValue.empty() ? 0.0F : std::stof(pin.defaultValue);
            } catch (...) {
            }
            const bool integer = pin.type == PT::Int;
            auto number = prism::make<prism::NumberField>(value, integer ? 1.0F : 0.05F, integer ? 0 : 3);
            number->onCommit = [set, integer](const float v) {
                char text[48];
                if (integer) {
                    std::snprintf(text, sizeof(text), "%ld", std::lround(v));
                } else {
                    std::snprintf(text, sizeof(text), "%g", static_cast<double>(v));
                }
                set(text);
            };
            editor = number;
        } else {
            auto field = prism::make<prism::TextInput>(pin.defaultValue, pin.type == PT::Color ? "r,g,b (0..1)" : "");
            field->onCommit = [set](const std::string& v) { set(v); };
            editor = field;
        }
        // Back to what the node starts with.
        std::string initial;
        if (type != nullptr) {
            const prism::BlueprintNode fresh = MakeNodeUVE(*type, Asset::BlueprintAssetUVE{}, {});
            if (static_cast<std::size_t>(i) < fresh.inputs.size()) {
                initial = fresh.inputs[static_cast<std::size_t>(i)].defaultValue;
            }
        }
        auto reset = prism::make<prism::Button>("", prism::Icon::Undo, prism::ButtonStyle::Flat);
        reset->setTooltip("Reset to its default (" + (initial.empty() ? std::string("empty") : initial) + ")");
        reset->setEnabled(pin.defaultValue != initial);
        reset->onClick = [set, initial] { set(initial); };
        auto row = prism::hbox(4.0F);
        row->add(editor, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        row->add(reset, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        grid.addRow(cat, pin.name, row, std::string(TypeNameUVE(pin.type)) + " input");
    }
}

void EditorBlueprintPanelUVE::RebuildSideUVE() {
    m_sideDirty = false;
    if (!m_side) {
        return;
    }
    auto grid = prism::make<prism::PropertyGrid>();
    // The node picked in the graph, as the standard Details: what it is and every input it takes.
    if (m_graph && m_graph->selectedCount() == 1) {
        if (const prism::BlueprintNode* node = m_graph->findNode(m_graph->selectedIds().front())) {
            AddNodeDetailsUVE(*grid, *node);
        }
    }
    const int classCat = grid->addCategory("Class");
    const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    const std::string target = IsOpenUVE() && em.IsAliveUVE(m_target) ? m_editor.GetEntityDisplayLabelUVE(m_target) : std::string("None");
    grid->addRow(classCat, "Target", prism::make<prism::Label>(target + "  (" + ParentKindUVE() + ")"), "The object this Blueprint drives");
    auto file = prism::make<prism::Label>(IsOpenUVE() ? m_path : std::string("None"));
    file->setColor(prism::ThemeColor(&prism::Theme::textDim));
    grid->addRow(classCat, "File", file, m_path);

    const int varsCat = grid->addCategory("Variables");
    if (m_graph) {
        for (const prism::BlueprintVariable& variable : m_graph->variables()) {
            const std::string name = variable.name;
            auto row = prism::hbox(4.0F);
            auto field = prism::make<prism::TextInput>(name, "Name");
            field->onCommit = [this, name](const std::string& to) {
                if (m_graph && m_graph->renameVariable(name, to)) {
                    if (m_exposed.erase(name) > 0U) {
                        m_exposed.insert(to);
                    }
                    m_dirty = true;
                    m_sideDirty = true;
                }
            };
            auto value = prism::make<prism::TextInput>(variable.value, "Default");
            value->setTooltip("Its starting value (\"6.0\", \"true\", \"0,1,0\", text)");
            value->onCommit = [this, name](const std::string& to) {
                if (m_graph && m_graph->setVariableValue(name, to)) {
                    m_dirty = true;
                }
            };
            auto exposed = prism::make<prism::CheckBox>("", m_exposed.contains(name));
            exposed->setTooltip("Instance Editable: each object using this Blueprint sets its own value in Details");
            exposed->onChange = [this, name](const bool on) {
                if (on) {
                    m_exposed.insert(name);
                } else {
                    m_exposed.erase(name);
                }
                m_dirty = true;
            };
            auto remove = prism::make<prism::Button>("", prism::Icon::Trash, prism::ButtonStyle::Flat);
            remove->setTooltip("Remove the variable and its Get / Set nodes");
            remove->onClick = [this, name] {
                if (m_graph && m_graph->removeVariable(name)) {
                    m_exposed.erase(name);
                    m_dirty = true;
                    m_sideDirty = true;
                }
            };
            row->add(field, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
            row->add(value, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
            row->add(exposed, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
            row->add(remove, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
            grid->addRow(varsCat, TypeNameUVE(variable.type), row);
        }
    }
    auto add = prism::make<prism::Button>("Add", prism::Icon::Plus, prism::ButtonStyle::Default);
    add->setEnabled(IsOpenUVE());
    std::weak_ptr<prism::Button> weakAdd = add;
    add->onClick = [this, weakAdd] {
        const auto addOf = [this](const PT type) {
            return [this, type] {
                if (m_graph != nullptr && m_graph->addVariable("NewVar", type, {}) != nullptr) {
                    m_dirty = true;
                    m_sideDirty = true;
                }
            };
        };
        std::vector<prism::MenuItem> items{
            prism::MenuItem::Item("Float", addOf(PT::Float), "", prism::Icon::ParamFloat),
            prism::MenuItem::Item("Int", addOf(PT::Int), "", prism::Icon::ParamFloat),
            prism::MenuItem::Item("Bool", addOf(PT::Bool), "", prism::Icon::ParamBool),
            prism::MenuItem::Item("Vector", addOf(PT::Vector), "", prism::Icon::ParamVector),
            prism::MenuItem::Item("String", addOf(PT::String), "", prism::Icon::Script),
            prism::MenuItem::Item("Object", addOf(PT::Object), "", prism::Icon::Object3D),
        };
        if (auto b = weakAdd.lock()) {
            prism::showMenu(std::move(items), b->rect(), prism::PopupPlacement::Below);
        }
    };
    grid->addRow(varsCat, "", add);
    m_side->setContent(prism::make<FitWidthUVE>(grid));
}

Scene::EntityUVE EditorBlueprintPanelUVE::FindUserUVE(const std::string& path) const {
    if (path.empty()) {
        return Scene::kInvalidEntityUVE;
    }
    const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    for (const Scene::EntityUVE object : m_editor.CollectEntityEditorObjectsUVE()) {
        if (em.IsAliveUVE(object) && em.HasComponentUVE<Scene::ScriptComponentUVE>(object) &&
            em.GetComponentUVE<Scene::ScriptComponentUVE>(object).scriptAssetPath == path) {
            return object;
        }
    }
    return Scene::kInvalidEntityUVE;
}

namespace {

/// A character's starting controls, on Event Tick (node 2): WASD moves it along its own forward
/// and right (the Input Map's MoveForward and MoveHorizontal), Jump jumps. Ordinary nodes, so
/// they can be changed like any others.
void AddCharacterControlsUVE(Asset::BlueprintAssetUVE& blueprint) {
    const auto node = [&blueprint](const std::uint32_t id, const char* type, const float x, const float y,
                                   std::vector<std::string> values = {}) {
        Asset::BlueprintNodeUVE n;
        n.id = id;
        n.type = type;
        n.x = x;
        n.y = y;
        n.inputValues = std::move(values);
        blueprint.nodes.push_back(std::move(n));
    };
    const auto wire = [&blueprint](const std::uint32_t from, const std::uint32_t fromPin, const std::uint32_t to, const std::uint32_t toPin) {
        blueprint.wires.push_back(Asset::BlueprintWireUVE{from, fromPin, to, toPin});
    };
    // Forward and back.
    node(3U, "add_movement_input", 420.0F, 200.0F);
    node(4U, "vector_scale", 160.0F, 330.0F);
    node(5U, "get_forward_vector", -120.0F, 330.0F);
    node(6U, "input_axis_value", -120.0F, 420.0F, {"MoveForward"});
    // Left and right.
    node(7U, "add_movement_input", 760.0F, 200.0F);
    node(8U, "vector_scale", 500.0F, 470.0F);
    node(9U, "get_right_vector", 220.0F, 470.0F);
    node(10U, "input_axis_value", 220.0F, 560.0F, {"MoveHorizontal"});
    // Jump.
    node(11U, "branch", 1100.0F, 200.0F);
    node(12U, "input_pressed", 820.0F, 330.0F, {"Jump"});
    node(13U, "character_jump", 1360.0F, 200.0F);
    wire(2U, 0U, 3U, 0U);
    wire(5U, 0U, 4U, 0U);
    wire(6U, 0U, 4U, 1U);
    wire(4U, 0U, 3U, 1U);
    wire(3U, 0U, 7U, 0U);
    wire(9U, 0U, 8U, 0U);
    wire(10U, 0U, 8U, 1U);
    wire(8U, 0U, 7U, 1U);
    wire(7U, 0U, 11U, 0U);
    wire(12U, 0U, 11U, 1U);
    wire(11U, 0U, 13U, 0U);
}

} // namespace

std::string EditorBlueprintPanelUVE::CreateForUVE(const Scene::EntityUVE target, const std::filesystem::path& absolutePath) {
    const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    if (!em.IsAliveUVE(target)) {
        return {};
    }
    m_target = target;
    // The standard starting graph: Event Ready (BeginPlay) and Event Tick.
    Asset::BlueprintAssetUVE blueprint;
    blueprint.parentKind = ParentKindUVE();
    Asset::BlueprintNodeUVE ready;
    ready.id = 1U;
    ready.type = "event_ready";
    ready.x = 0.0F;
    ready.y = 0.0F;
    Asset::BlueprintNodeUVE tick;
    tick.id = 2U;
    tick.type = "event_tick";
    tick.x = 0.0F;
    tick.y = 200.0F;
    blueprint.nodes = {ready, tick};
    if (em.HasComponentUVE<Scene::CharacterControllerComponentUVE>(target)) {
        AddCharacterControlsUVE(blueprint);
    }
    std::error_code error;
    std::filesystem::create_directories(absolutePath.parent_path(), error);
    if (!Asset::SaveBlueprintAssetUVE(blueprint, absolutePath)) {
        return {};
    }
    const std::string path = ProjectPathUVE(absolutePath);
    // The target's Script is the Blueprint (one undo step), added when it has none.
    m_editor.SelectEntityUVE(target);
    Scene::ScriptComponentUVE script;
    if (em.HasComponentUVE<Scene::ScriptComponentUVE>(target)) {
        script = em.GetComponentUVE<Scene::ScriptComponentUVE>(target);
    }
    const bool already = em.HasComponentUVE<Scene::ScriptComponentUVE>(target) && script.scriptAssetPath == path;
    script.scriptAssetPath = path;
    script.exportValues.clear();
    if (!already && !m_editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::Script, script)) {
        // Not attached: no Blueprint left lying about that nothing uses.
        std::filesystem::remove(absolutePath, error);
        return {};
    }
    m_editor.RefreshProjectFileIndexUVE();
    return path;
}

} // namespace UVE::Editor
