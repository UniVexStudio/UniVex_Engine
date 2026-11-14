// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor.h"

#include "blueprint_asset.h"
#include "editor_widget_icons.h"
#include <functional>
#include <utility>

#include "prism/BlueprintGraph.h"
#include "prism/Widget.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace prism {
class PropertyGrid;
class ScrollBox;
} // namespace prism

namespace UVE::Editor {

/// The Entity Editor's Blueprint page: one object's Blueprint Class (.uvbp) as a PRISM blueprint
/// graph - the full PRISM design: toolbar (Compile, Save, Find, Variables, Simulate, Undo ...),
/// minimap, comment boxes (C), copy / paste, and the engine's complete node palette (Events,
/// Flow Control, Variables, Math, Transform, Physics, Input, Audio, Animation, Objects, Gameplay,
/// Utilities). Its Details tab has the class (target object, file) and the variables.
///
/// While it is open the graph is the truth: edits are kept in it (with its own undo) and written
/// to the file by Save; Compile translates it to UVScript and checks it against the target object,
/// each problem marked on its node.
class EditorBlueprintPanelUVE final {
public:
    explicit EditorBlueprintPanelUVE(EditorUVE& editor);

    [[nodiscard]] prism::Ref<prism::Widget> BuildUVE();
    /// The Details tab: class settings and variables.
    [[nodiscard]] prism::Ref<prism::Widget> SideUVE() const { return m_sidePanel; }

    /// Opens `path` (project-relative, as a Script slot stores it) for `target`.
    bool OpenUVE(const std::string& path, Scene::EntityUVE target);
    void CloseUVE();
    bool SaveUVE();
    void CompileUVE();
    /// Keeps the Details tab current; closes when the target object is gone.
    void RefreshUVE();
    /// Selects and frames a node (a Compile Results row).
    void SelectNodeUVE(std::uint32_t node);

    [[nodiscard]] bool IsOpenUVE() const noexcept { return !m_path.empty(); }
    [[nodiscard]] const std::string& PathUVE() const noexcept { return m_path; }
    [[nodiscard]] Scene::EntityUVE TargetUVE() const noexcept { return m_target; }
    [[nodiscard]] bool DirtyUVE() const noexcept { return m_dirty; }
    /// The graph as it is now, for `path` when that is the open one.
    [[nodiscard]] std::optional<Asset::BlueprintAssetUVE> UnsavedForUVE(const std::string& path) const;

    /// A new Blueprint Class for `target` at `absolutePath` (with Event Ready and Event Tick), the
    /// target's Script set to it (one undo step). Returns its project-relative path, empty on failure.
    [[nodiscard]] std::string CreateForUVE(Scene::EntityUVE target, const std::filesystem::path& absolutePath);
    /// The object of the open entity whose Script slot names `path` (after a rebuild its handle is new).
    [[nodiscard]] Scene::EntityUVE FindUserUVE(const std::string& path) const;
    /// The project-relative (VFS) path of a file, as a Script slot stores it.
    [[nodiscard]] std::string ProjectPathUVE(const std::filesystem::path& absolute) const;

    /// A Widget Blueprint's widgets (name and kind): the palette's Widgets category lists each with
    /// the nodes it takes, its name already filled in.
    std::function<std::vector<std::pair<std::string, WidgetKindUVE>>()> widgetsProvider;
    /// The entity's Animation Graph parameters, offered on the Anim nodes' Parameter pin.
    std::function<std::vector<std::string>()> animParametersProvider;
private:
    /// The catalogue key of a graph node ("ui_set_text"), empty for one that is not the engine's.
    [[nodiscard]] static std::string NodeKeyUVE(const prism::BlueprintNode& node);
    /// The widgets a UI node of `key` can act on, by name (empty: not a UI node, or no widgets).
    [[nodiscard]] std::vector<std::string> WidgetChoicesUVE(const std::string& key) const;
    /// The picked node's section of Details: what it is, and an editor for each input.
    void AddNodeDetailsUVE(prism::PropertyGrid& grid, const prism::BlueprintNode& node);
    [[nodiscard]] Asset::BlueprintAssetUVE FromGraphUVE() const;
    void LoadIntoGraphUVE(const Asset::BlueprintAssetUVE& blueprint);
    [[nodiscard]] std::vector<prism::BlueprintNodeSpec> LibraryUVE() const;
    void RebuildSideUVE();
    [[nodiscard]] std::string ParentKindUVE() const;

    EditorUVE& m_editor;
    std::shared_ptr<prism::BlueprintGraph> m_graph;
    std::shared_ptr<prism::ScrollBox> m_side;
    std::shared_ptr<prism::Widget> m_sidePanel;
    std::string m_path;
    Scene::EntityUVE m_target = Scene::kInvalidEntityUVE;
    bool m_dirty = false;
    bool m_loading = false;
    /// Variables shown in Details as an object's export (the standard Instance Editable).
    std::set<std::string> m_exposed;
    /// What the Details tab last showed, to rebuild it only when the variables change.
    std::vector<prism::BlueprintVariable> m_shownVariables;
    std::set<std::string> m_shownExposed;
    bool m_sideDirty = true;
};

} // namespace UVE::Editor
