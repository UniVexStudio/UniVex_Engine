// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_blueprint_panel.h"

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
#include <map>

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
        pin.kind = kind;
        pin.defaultValue = kind == prism::BlueprintPinKind::Input ? spec.defaultText : std::string{};
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

} // namespace

EditorBlueprintPanelUVE::EditorBlueprintPanelUVE(EditorUVE& editor) : m_editor(editor) {}

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
    for (const Asset::BlueprintNodeTypeUVE& type : Asset::GetBlueprintNodeTypesUVE()) {
        if (type.role == Role::GetVariable || type.role == Role::SetVariable) {
            continue;
        }
        prism::BlueprintNodeSpec spec;
        spec.category = type.category;
        spec.name = type.title;
        spec.keywords = type.keywords + " " + type.tooltip;
        spec.make = [&type, blueprint] { return MakeNodeUVE(type, blueprint, {}); };
        library.push_back(std::move(spec));
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
        for (std::size_t i = 0U; i < shown.inputs.size() && i < node.inputValues.size(); ++i) {
            if (!node.inputValues[i].empty()) {
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
            CloseUVE();
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

void EditorBlueprintPanelUVE::RebuildSideUVE() {
    m_sideDirty = false;
    if (!m_side) {
        return;
    }
    auto grid = prism::make<prism::PropertyGrid>();
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

std::string EditorBlueprintPanelUVE::CreateForUVE(const Scene::EntityUVE target, const std::filesystem::path& absolutePath) {
    const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    if (!em.IsAliveUVE(target)) {
        return {};
    }
    m_target = target;
    // Unreal's starting graph: Event Ready (BeginPlay) and Event Tick.
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
    script.scriptAssetPath = path;
    script.exportValues.clear();
    if (!m_editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::Script, script)) {
        return {};
    }
    m_editor.RefreshProjectFileIndexUVE();
    return path;
}

} // namespace UVE::Editor
