// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Inspector rows for a Blueprint's exported variables. The Blueprint decides which fields exist
// and their types and defaults; the object stores only the values it changes, as text in
// its Script component. So each edit is one ordinary undoable property edit, and a script that
// renames or retypes a field simply stops showing the old value instead of breaking the scene.

#include "editor.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <typeindex>
#include <utility>
#include <variant>
#include <vector>

#include "blueprint_graph_lowering.h"
#include "blueprint_object_host.h"
#include "script.h"
#include "i_entity_manager.h"
#include "blueprint_compiler.h"
#include "blueprint_instance.h"

namespace UVE::Editor {
namespace {

/// The Script component's exported-values property; edits go through it to be one undo entry.
struct ScriptExportsProperty final {
    const Core::TypeMetadataEntry* entry = nullptr;
    const Core::TypeMetadataProperty* property = nullptr;
};

[[nodiscard]] ScriptExportsProperty FindScriptExportsProperty() {
    const Core::TypeMetadataEntry* const entry =
        Scene::FindSceneComponentMetadata(std::type_index(typeid(Scene::ScriptComponent)));
    if (entry == nullptr) {
        return {};
    }
    const auto property = std::find_if(entry->properties.cbegin(), entry->properties.cend(),
                                       [](const Core::TypeMetadataProperty& candidate) {
                                           return candidate.name == "exportValues";
                                       });
    return property == entry->properties.cend() ? ScriptExportsProperty{}
                                                : ScriptExportsProperty{entry, &*property};
}

} // namespace

std::vector<Editor::ScriptExportRow> Editor::GetSelectedScriptExports() {
    if (m_services == nullptr || !HasSingleDocumentSelection()) {
        return {};
    }
    const Scene::Entity entity = m_selectedEntity;
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.HasComponent<Scene::ScriptComponent>(entity)) {
        return {};
    }
    const Scene::ScriptComponent& script = entityManager.GetComponent<Scene::ScriptComponent>(entity);
    if (!Scene::Script::IsBlueprintPath(script.scriptAssetPath)) {
        return {};
    }
    const std::optional<EntityScriptSource> source = ReadEntityScriptSource(script.scriptAssetPath, entity);
    if (!source.has_value() || !source->graph.has_value()) {
        return {};
    }
    Core::BlueprintObjectHost host(entityManager, nullptr, entity);
    const Blueprint::CompileResult compiled =
        Asset::CompileBlueprintGraph(*source->graph, source->objectName, source->objectKind, host).result;
    if (!compiled.IsSuccess()) {
        return {};
    }
    // The defaults are whatever the field initializers produce, so a throwaway instance runs them.
    const Blueprint::ScriptInstance defaults(compiled.program, host);
    std::vector<ScriptExportRow> rows;
    for (const Blueprint::FieldInfo& field : Blueprint::GetProgramFields(*compiled.program)) {
        if (field.kind != Blueprint::FieldKind::Export) {
            continue;
        }
        ScriptExportRow row;
        row.name = field.name;
        row.type = field.type;
        const std::optional<Blueprint::Value> initial = defaults.GetField(field.name);
        row.defaultText = initial.has_value() ? Blueprint::FormatValue(*initial) : std::string{};
        row.valueText = row.defaultText;
        if (const auto stored = script.exportValues.find(field.name);
            stored != script.exportValues.end() && Blueprint::ParseValueText(stored->second, field.type).has_value()) {
            row.valueText = stored->second;
            row.overridden = true;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

bool Editor::SetSelectedScriptExport(const std::string& name, std::optional<std::string> text) {
    if (!IsAuthoringCommandAllowed()) {
        return false;
    }
    const std::vector<ScriptExportRow> rows = GetSelectedScriptExports();
    const auto row = std::find_if(rows.cbegin(), rows.cend(),
                                  [&name](const ScriptExportRow& candidate) { return candidate.name == name; });
    if (row == rows.cend()) {
        return false;
    }
    const ScriptExportsProperty target = FindScriptExportsProperty();
    if (target.entry == nullptr) {
        return false;
    }
    std::map<std::string, std::string> values =
        m_services->GetEntityManager().GetComponent<Scene::ScriptComponent>(m_selectedEntity).exportValues;
    if (text.has_value()) {
        const std::optional<Blueprint::Value> value = Blueprint::ParseValueText(*text, row->type);
        if (!value.has_value()) {
            return false;
        }
        // Stored in the canonical form, so "1" for a float reads back as "1.0".
        values.insert_or_assign(name, Blueprint::FormatValue(*value));
    } else {
        values.erase(name);
    }
    return SetSelectedComponentProperty(*target.entry, *target.property, &values);
}

} // namespace UVE::Editor
