// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/script_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsScriptObjectDefinitionValidUVE(const ScriptObjectDefinitionUVE& value) noexcept {
    return IsScriptComponentValidUVE(value.script);
}

void ApplyScriptObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                    const ScriptObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity, ScriptObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<ScriptComponentUVE>(entity, value.script);
}

bool ScriptUVE::HasScriptPathUVE(const ScriptComponentUVE& script) noexcept {
    return IsScriptComponentValidUVE(script) && !script.scriptAssetPath.empty();
}

bool ScriptUVE::IsUVScriptPathUVE(const std::string_view scriptAssetPath) noexcept {
    return scriptAssetPath.ends_with(kUVScriptExtensionUVE);
}

bool ScriptUVE::IsRunnableUVScriptUVE(const ScriptComponentUVE& script) noexcept {
    return IsScriptComponentValidUVE(script) && IsUVScriptPathUVE(script.scriptAssetPath);
}

const std::string* ScriptUVE::FindExportedPropertyUVE(
    const ScriptComponentUVE& script, const std::string_view name) noexcept {
    const auto it = script.exportValues.find(std::string{name});
    if (it == script.exportValues.end()) {
        return nullptr;
    }
    return &it->second;
}

bool ScriptUVE::TrySetExportedPropertyUVE(
    ScriptComponentUVE& script, const std::string_view name, const std::string_view value) {
    if (name.empty() || name.size() > kMaximumScriptExportNameBytesUVE ||
        name.find('\0') != std::string_view::npos ||
        value.size() > kMaximumScriptExportValueBytesUVE ||
        value.find('\0') != std::string_view::npos) {
        return false;
    }
    const std::string key{name};
    const auto existing = script.exportValues.find(key);
    if (existing != script.exportValues.end()) {
        existing->second = std::string{value};
        return true;
    }
    if (script.exportValues.size() >= kMaximumScriptExportValuesUVE) {
        return false;
    }
    script.exportValues.emplace(key, std::string{value});
    return true;
}

bool ScriptUVE::RemoveExportedPropertyUVE(
    ScriptComponentUVE& script, const std::string_view name) noexcept {
    return script.exportValues.erase(std::string{name}) > 0U;
}

} // namespace UVE::Scene
