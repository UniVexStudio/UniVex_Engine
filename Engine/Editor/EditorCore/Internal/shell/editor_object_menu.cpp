// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_object_menu.h"

#include "editor.h"
#include "editor_commands.h"

#include "i_asset_database.h"
#include "i_project_file_index.h"
#include "prefab_instance_component.h"
#include "visibility_component.h"

#include "prism/Application.h"
#include "prism/Menu.h"
#include "prism/Views.h"

#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <cstddef>

namespace UVE::Editor {

void EditorObjectMenu::Show(Editor& editor, const Scene::Entity entity, prism::TreeNode* const node,
                                       const prism::Vec2 at, const Hooks& hooks) {
    if (!editor.IsDocumentEntity(entity)) {
        return;
    }
    Scene::IEntityManager& em = editor.m_services->GetEntityManager();
    const std::string name = editor.GetEntityDisplayLabel(entity);
    const std::string blocked = editor.ObjectActionBlockReason(entity);
    const bool authoring = editor.IsAuthoringCommandAllowed();
    const bool sealed = editor.IsSealedHierarchyRow(entity);
    const bool structural = editor.IsStructuralRoot(entity);
    prism::TreeView* const tree = hooks.tree;

    // A command of the editor's, with its shortcut and its own enabled rule.
    const auto command = [&editor](const char* id) {
        EditorCommand* const found = editor.FindEditorCommand(id);
        if (found == nullptr) {
            return prism::MenuItem::Item(id, {}).disabled();
        }
        prism::MenuItem item = prism::MenuItem::Item(
            found->label, [&editor, commandId = std::string(id)] { static_cast<void>(editor.RunEditorCommand(commandId)); },
            DisplayEditorShortcut(found->shortcuts[0]));
        item.enabled = !found->isEnabled || found->isEnabled();
        return item;
    };
    const auto when = [](prism::MenuItem item, const bool enabled) {
        item.enabled = item.enabled && enabled;
        return item;
    };

    std::vector<prism::MenuItem> items;
    items.push_back(prism::MenuItem::Item(name, {}).disabled());
    if (!blocked.empty()) {
        items.push_back(prism::MenuItem::Item(blocked, {}).disabled());
    }
    items.push_back(prism::MenuItem::Sep());

    // Making and naming.
    items.push_back(when(prism::MenuItem::Item("Add Child...", [add = hooks.addChild, entity] {
                             if (add) {
                                 add(entity);
                             }
                         }, "", prism::Icon::Plus),
                         authoring && !sealed && static_cast<bool>(hooks.addChild)));
    items.push_back(when(prism::MenuItem::Item("Rename", [tree, node] {
                             if (tree != nullptr && node != nullptr) {
                                 tree->beginRename(node);
                             }
                         }, "F2"),
                         authoring && !structural && tree != nullptr && node != nullptr));
    items.push_back(prism::MenuItem::Sep());

    // Clipboard and lifecycle.
    items.push_back(when(command("edit.cut"), blocked.empty()));
    items.push_back(when(command("edit.copy"), blocked.empty()));
    items.push_back(command("edit.paste"));
    items.push_back(when(prism::MenuItem::Item("Paste as Child", [&editor] { static_cast<void>(editor.PasteObject(true)); }),
                         editor.CanPasteObject() && !sealed));
    items.push_back(when(command("edit.duplicate"), blocked.empty()));
    items.push_back(when(command("edit.delete"), blocked.empty()));
    items.push_back(prism::MenuItem::Sep());

    // Looking at it.
    items.push_back(when(prism::MenuItem::Item("Focus in Viewport", [&editor, entity] {
                             static_cast<void>(editor.RequestViewportFocus(entity));
                         }, "F"),
                         editor.CanFocusEntityInViewport(entity)));
    const bool hideable = em.HasComponent<Scene::VisibilityComponent>(entity);
    const bool visible = hideable && em.GetComponent<Scene::VisibilityComponent>(entity).visible;
    items.push_back(when(prism::MenuItem::Item(hideable && !visible ? "Show" : "Hide", [&editor, entity, visible] {
                             static_cast<void>(editor.SetEntityVisible(entity, !visible));
                         }),
                         authoring && hideable));
    const bool locked = editor.IsEntityLocked(entity);
    items.push_back(when(prism::MenuItem::Item(locked ? "Unlock" : "Lock", [&editor, entity] { editor.ToggleEntityLock(entity); }),
                         authoring && !structural));
    items.push_back(prism::MenuItem::Sep());

    // The branch.
    const bool hasKids = node != nullptr && !node->kids.empty();
    const auto setBranch = [tree](prism::TreeNode* top, const bool open) {
        std::vector<prism::TreeNode*> pending{top};
        while (!pending.empty()) {
            prism::TreeNode* const n = pending.back();
            pending.pop_back();
            n->expanded = open;
            for (const auto& kid : n->kids) {
                pending.push_back(kid.get());
            }
        }
        if (tree != nullptr) {
            tree->refresh();
        }
    };
    items.push_back(when(prism::MenuItem::Item("Expand Branch", [setBranch, node] { setBranch(node, true); }), hasKids));
    items.push_back(when(prism::MenuItem::Item("Collapse Branch", [setBranch, node] { setBranch(node, false); }), hasKids));
    const auto selectInside = [&editor, entity](const bool deep) {
        std::vector<Scene::Entity> found;
        std::vector<Scene::Entity> pending{entity};
        Scene::IEntityManager& manager = editor.m_services->GetEntityManager();
        while (!pending.empty()) {
            const Scene::Entity current = pending.back();
            pending.pop_back();
            for (const Scene::Entity child : editor.m_services->GetSceneGraph().GetChildren(manager, current)) {
                if (!editor.IsDocumentEntity(child)) {
                    continue;
                }
                found.push_back(child);
                if (deep) {
                    pending.push_back(child);
                }
            }
        }
        if (found.empty()) {
            return;
        }
        editor.SelectEntity(found.front());
        for (std::size_t i = 1U; i < found.size(); ++i) {
            if (!editor.IsEntitySelected(found[i])) {
                editor.ToggleEntitySelection(found[i]);
            }
        }
    };
    items.push_back(when(prism::MenuItem::Item("Select Children", [selectInside] { selectInside(false); }), hasKids && !sealed));
    items.push_back(when(prism::MenuItem::Item("Select All Inside", [selectInside] { selectInside(true); }), hasKids && !sealed));
    items.push_back(prism::MenuItem::Sep());

    // Its place among its siblings.
    items.push_back(command("edit.moveUp"));
    items.push_back(command("edit.moveDown"));
    items.push_back(command("edit.moveToTop"));
    items.push_back(command("edit.moveToBottom"));
    items.push_back(prism::MenuItem::Sep());
    items.push_back(prism::MenuItem::Item("Copy Name", [name] {
        if (prism::Application::exists()) {
            prism::Application::get().setClipboard(name);
        }
    }));

    // A placed entity opens in the Entity Editor; anything else can become one.
    std::filesystem::path source;
    if (em.HasComponent<Scene::PrefabInstanceComponent>(entity)) {
        const Asset::AssetGuid guid = em.GetComponent<Scene::PrefabInstanceComponent>(entity).sourcePrefabGuid;
        if (guid != Asset::kInvalidAssetGuid) {
            source = editor.m_services->GetAssetDatabase().Resolve(guid);
        }
    }
    items.push_back(prism::MenuItem::Sep());
    if (sealed) {
        items.push_back(when(prism::MenuItem::Item("Open Entity", [&editor, source] {
                                 if (!editor.OpenEntityEditor(source) && editor.m_contentStatusMessage.empty()) {
                                     editor.m_contentStatusMessage = "Could not open " + source.filename().string() + ".";
                                 }
                             }),
                             !source.empty()));
        items.push_back(when(prism::MenuItem::Item("Show in Asset Table", [&editor, source] {
                                 const Asset::ProjectFileSnapshot now = editor.m_services->GetProjectFileIndex().GetSnapshot();
                                 std::error_code error;
                                 const std::filesystem::path relative = std::filesystem::relative(source, now.contentRoot, error);
                                 for (const Asset::ProjectFileEntry& file : now.entries) {
                                     if (!error && file.relativePath == relative) {
                                         editor.m_assetTableDirectory = file.relativePath.parent_path();
                                         editor.m_assetTableShelf.clear();
                                         editor.m_contentSelection.Clear();
                                         editor.m_selectedProjectFile = file;
                                         break;
                                     }
                                 }
                             }),
                             !source.empty()));
    } else {
        items.push_back(when(prism::MenuItem::Item("Save as Entity", [&editor, name] {
                                 // Into the Asset Table's folder, under the object's own name.
                                 const Asset::ProjectFileSnapshot now = editor.m_services->GetProjectFileIndex().GetSnapshot();
                                 const std::filesystem::path folder = now.contentRoot / editor.m_assetTableDirectory;
                                 std::error_code error;
                                 std::filesystem::create_directories(folder, error);
                                 const std::filesystem::path path =
                                     Editor::MakeUniqueContentPath(folder, name.empty() ? "Entity" : name, ".uventity");
                                 if (editor.SaveSelectedPrefab(path)) {
                                     editor.RefreshProjectFileIndex();
                                     editor.m_contentStatusMessage = "Saved " + path.filename().string() + ".";
                                 } else {
                                     editor.m_contentStatusMessage = "Could not save " + name + " as an entity.";
                                 }
                             }),
                             blocked.empty()));
    }
    prism::showContextMenu(std::move(items), at);
}

} // namespace UVE::Editor
