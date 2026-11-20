// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Inspector rows for a `.uvs` script's `export` fields. The script decides which fields exist
// and their types and defaults; the object stores only the values it changes, as UVScript text in
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

#include "blueprint_object_host.h"
#include "i_entity_manager.h"
#include "blueprint_compiler.h"
#include "blueprint_instance.h"

namespace UVE::Editor {
namespace {

/// The Script component's exported-values property; edits go through it to be one undo entry.
struct ScriptExportsPropertyUVE final {
    const Core::TypeMetadataEntryUVE* entry = nullptr;
    const Core::TypeMetadataPropertyUVE* property = nullptr;
};

[[nodiscard]] ScriptExportsPropertyUVE FindScriptExportsPropertyUVE() {
    const Core::TypeMetadataEntryUVE* const entry =
        Scene::FindSceneComponentMetadataUVE(std::type_index(typeid(Scene::ScriptComponentUVE)));
    if (entry == nullptr) {
        return {};
    }
    const auto property = std::find_if(entry->properties.cbegin(), entry->properties.cend(),
                                       [](const Core::TypeMetadataPropertyUVE& candidate) {
                                           return candidate.name == "exportValues";
                                       });
    return property == entry->properties.cend() ? ScriptExportsPropertyUVE{}
                                                : ScriptExportsPropertyUVE{entry, &*property};
}

} // namespace

std::vector<EditorUVE::ScriptExportRowUVE> EditorUVE::GetSelectedScriptExportsUVE() {
    if (m_services == nullptr || !HasSingleDocumentSelectionUVE()) {
        return {};
    }
    const Scene::EntityUVE entity = m_selectedEntity;
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(entity)) {
        return {};
    }
    const Scene::ScriptComponentUVE& script = entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(entity);
    if (!script.scriptAssetPath.ends_with(".uvs")) {
        return {};
    }
    const std::optional<std::string> source = ReadProjectTextFileUVE(script.scriptAssetPath);
    if (!source.has_value()) {
        return {};
    }
    Core::BlueprintObjectHostUVE host(entityManager, nullptr, entity);
    const Blueprint::CompileResultUVE compiled = Blueprint::CompileUVScriptSourceUVE(*source, host);
    if (!compiled.IsSuccessUVE()) {
        return {};
    }
    // The defaults are whatever the field initializers produce, so a throwaway instance runs them.
    const Blueprint::ScriptInstanceUVE defaults(compiled.program, host);
    std::vector<ScriptExportRowUVE> rows;
    for (const Blueprint::FieldInfoUVE& field : Blueprint::GetProgramFieldsUVE(*compiled.program)) {
        if (field.kind != Blueprint::FieldKindUVE::Export) {
            continue;
        }
        ScriptExportRowUVE row;
        row.name = field.name;
        row.type = field.type;
        const std::optional<Blueprint::ValueUVE> initial = defaults.GetFieldUVE(field.name);
        row.defaultText = initial.has_value() ? Blueprint::FormatValueUVE(*initial) : std::string{};
        row.valueText = row.defaultText;
        if (const auto stored = script.exportValues.find(field.name);
            stored != script.exportValues.end() && Blueprint::ParseValueTextUVE(stored->second, field.type).has_value()) {
            row.valueText = stored->second;
            row.overridden = true;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

bool EditorUVE::SetSelectedScriptExportUVE(const std::string& name, std::optional<std::string> text) {
    if (!IsAuthoringCommandAllowedUVE()) {
        return false;
    }
    const std::vector<ScriptExportRowUVE> rows = GetSelectedScriptExportsUVE();
    const auto row = std::find_if(rows.cbegin(), rows.cend(),
                                  [&name](const ScriptExportRowUVE& candidate) { return candidate.name == name; });
    if (row == rows.cend()) {
        return false;
    }
    const ScriptExportsPropertyUVE target = FindScriptExportsPropertyUVE();
    if (target.entry == nullptr) {
        return false;
    }
    std::map<std::string, std::string> values =
        m_services->GetEntityManagerUVE().GetComponentUVE<Scene::ScriptComponentUVE>(m_selectedEntity).exportValues;
    if (text.has_value()) {
        const std::optional<Blueprint::ValueUVE> value = Blueprint::ParseValueTextUVE(*text, row->type);
        if (!value.has_value()) {
            return false;
        }
        // Stored in the canonical form, so "1" for a float reads back as "1.0".
        values.insert_or_assign(name, Blueprint::FormatValueUVE(*value));
    } else {
        values.erase(name);
    }
    return SetSelectedComponentPropertyUVE(*target.entry, *target.property, &values);
}

} // namespace UVE::Editor
