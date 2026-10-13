// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Inspector rows for a `.uvs` script's `export` fields. The script decides which fields exist
// and their types and defaults; the object stores only the values it changes, as UVScript text in
// its Script component. So each edit is one ordinary undoable property edit, and a script that
// renames or retypes a field simply stops showing the old value instead of breaking the scene.

#include "uve/editor/editor_uve.h"

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

#include <imgui.h>

#include "uve/core/uvscript_object_host_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/uvscript/uvscript_compiler_uve.h"
#include "uve/uvscript/uvscript_instance_uve.h"

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
    Core::UVScriptObjectHostUVE host(entityManager, nullptr, entity);
    const UVScript::CompileResultUVE compiled = UVScript::CompileUVScriptSourceUVE(*source, host);
    if (!compiled.IsSuccessUVE()) {
        return {};
    }
    // The defaults are whatever the field initializers produce, so a throwaway instance runs them.
    const UVScript::ScriptInstanceUVE defaults(compiled.program, host);
    std::vector<ScriptExportRowUVE> rows;
    for (const UVScript::FieldInfoUVE& field : UVScript::GetProgramFieldsUVE(*compiled.program)) {
        if (field.kind != UVScript::FieldKindUVE::Export) {
            continue;
        }
        ScriptExportRowUVE row;
        row.name = field.name;
        row.type = field.type;
        const std::optional<UVScript::ValueUVE> initial = defaults.GetFieldUVE(field.name);
        row.defaultText = initial.has_value() ? UVScript::FormatValueUVE(*initial) : std::string{};
        row.valueText = row.defaultText;
        if (const auto stored = script.exportValues.find(field.name);
            stored != script.exportValues.end() && UVScript::ParseValueTextUVE(stored->second, field.type).has_value()) {
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
        const std::optional<UVScript::ValueUVE> value = UVScript::ParseValueTextUVE(*text, row->type);
        if (!value.has_value()) {
            return false;
        }
        // Stored in the canonical form, so "1" for a float reads back as "1.0".
        values.insert_or_assign(name, UVScript::FormatValueUVE(*value));
    } else {
        values.erase(name);
    }
    return SetSelectedComponentPropertyUVE(*target.entry, *target.property, &values);
}

void EditorUVE::DrawScriptExportsPropertyUVE(const Core::TypeMetadataEntryUVE& /*entry*/,
                                             const Core::TypeMetadataPropertyUVE& /*property*/,
                                             const void* const instance) {
    const auto& script = *static_cast<const Scene::ScriptComponentUVE*>(instance);
    ScriptExportsCacheUVE& cache = m_scriptExportsCache;
    const auto now = std::chrono::steady_clock::now();
    if (cache.entity != m_selectedEntity || cache.path != script.scriptAssetPath || cache.values != script.exportValues ||
        now - cache.builtAt > std::chrono::milliseconds(500)) {
        cache.entity = m_selectedEntity;
        cache.path = script.scriptAssetPath;
        cache.values = script.exportValues;
        cache.builtAt = now;
        cache.rows = GetSelectedScriptExportsUVE();
    }
    if (cache.rows.empty()) {
        return;
    }
    const bool writable = IsAuthoringCommandAllowedUVE();
    if (!ImGui::BeginTable("##script-exports", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
        return;
    }
    ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthStretch, 0.42F);
    ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 0.58F);
    // Typed values commit on Enter, so one change is one undo step rather than one per keystroke.
    constexpr ImGuiInputTextFlags kCommitOnEnter = ImGuiInputTextFlags_EnterReturnsTrue;
    // Applied after the table: an edit replaces the rows this loop is walking.
    std::optional<std::pair<std::string, std::optional<std::string>>> edit;
    for (const ScriptExportRowUVE& row : cache.rows) {
        ImGui::PushID(row.name.c_str());
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        // A changed value is marked, and right-clicking its name puts the script's default back.
        if (row.overridden) {
            ImGui::TextColored(ImVec4{0.55F, 0.78F, 1.0F, 1.0F}, "%s", row.name.c_str());
        } else {
            ImGui::TextUnformatted(row.name.c_str());
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s: %s\nScript default: %s%s", row.name.c_str(), row.type.NameUVE().c_str(),
                              row.defaultText.c_str(), row.overridden ? "\nRight-click to reset." : "");
        }
        if (row.overridden && writable && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            edit.emplace(row.name, std::nullopt);
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::BeginDisabled(!writable);
        const std::optional<UVScript::ValueUVE> value = UVScript::ParseValueTextUVE(row.valueText, row.type);
        if (value.has_value()) {
            if (const bool* const flag = std::get_if<bool>(&*value)) {
                bool checked = *flag;
                if (ImGui::Checkbox("##value", &checked)) {
                    edit.emplace(row.name, checked ? "true" : "false");
                }
            } else if (const std::int64_t* const integer = std::get_if<std::int64_t>(&*value)) {
                std::int64_t number = *integer;
                if (ImGui::InputScalar("##value", ImGuiDataType_S64, &number, nullptr, nullptr, nullptr, kCommitOnEnter)) {
                    edit.emplace(row.name, std::to_string(number));
                }
            } else if (const double* const real = std::get_if<double>(&*value)) {
                double number = *real;
                if (ImGui::InputScalar("##value", ImGuiDataType_Double, &number, nullptr, nullptr, "%.3f", kCommitOnEnter)) {
                    edit.emplace(row.name, UVScript::FormatValueUVE(UVScript::ValueUVE{number}));
                }
            } else if (const auto* const vector = std::get_if<UVScript::Vec3ValueUVE>(&*value)) {
                std::array<double, 3> parts{vector->x, vector->y, vector->z};
                if (ImGui::InputScalarN("##value", ImGuiDataType_Double, parts.data(), 3, nullptr, nullptr, "%.3f",
                                        kCommitOnEnter)) {
                    edit.emplace(row.name, UVScript::FormatValueUVE(
                                               UVScript::ValueUVE{UVScript::Vec3ValueUVE{parts[0], parts[1], parts[2]}}));
                }
            } else if (const std::string* const text = std::get_if<std::string>(&*value)) {
                std::array<char, 256> buffer{};
                text->copy(buffer.data(), buffer.size() - 1U);
                if (ImGui::InputText("##value", buffer.data(), buffer.size(), kCommitOnEnter)) {
                    edit.emplace(row.name, std::string{buffer.data()});
                }
            }
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    ImGui::EndTable();
    if (edit.has_value()) {
        static_cast<void>(SetSelectedScriptExportUVE(edit->first, std::move(edit->second)));
    }
}

} // namespace UVE::Editor
