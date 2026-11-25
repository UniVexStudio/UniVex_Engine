// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "script.h"

#include "i_entity_manager.h"
#include "object_3d.h"

namespace UVE::Scene {

bool IsScriptObjectDefinitionValid(const ScriptObjectDefinition& value) noexcept {
    return IsScriptComponentValid(value.script);
}

void ApplyScriptObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                    const ScriptObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity, ScriptObjectDefinition::defaultName);
    entityManager.AddComponent<ScriptComponent>(entity, value.script);
}

bool Script::HasScriptPath(const ScriptComponent& script) noexcept {
    return IsScriptComponentValid(script) && !script.scriptAssetPath.empty();
}

bool Script::IsUVScriptPath(const std::string_view scriptAssetPath) noexcept {
    // A Blueprint Class (.uvbp) runs as the UVScript it translates to.
    return scriptAssetPath.ends_with(kUVScriptExtension) || scriptAssetPath.ends_with(".uvbp") ||
           scriptAssetPath.ends_with(".uvwbp");
}

bool Script::IsRunnableUVScript(const ScriptComponent& script) noexcept {
    return IsScriptComponentValid(script) && IsUVScriptPath(script.scriptAssetPath);
}

const std::string* Script::FindExportedProperty(
    const ScriptComponent& script, const std::string_view name) noexcept {
    const auto it = script.exportValues.find(std::string{name});
    if (it == script.exportValues.end()) {
        return nullptr;
    }
    return &it->second;
}

bool Script::TrySetExportedProperty(
    ScriptComponent& script, const std::string_view name, const std::string_view value) {
    if (name.empty() || name.size() > kMaximumScriptExportNameBytes ||
        name.find('\0') != std::string_view::npos ||
        value.size() > kMaximumScriptExportValueBytes ||
        value.find('\0') != std::string_view::npos) {
        return false;
    }
    const std::string key{name};
    const auto existing = script.exportValues.find(key);
    if (existing != script.exportValues.end()) {
        existing->second = std::string{value};
        return true;
    }
    if (script.exportValues.size() >= kMaximumScriptExportValues) {
        return false;
    }
    script.exportValues.emplace(key, std::string{value});
    return true;
}

bool Script::RemoveExportedProperty(
    ScriptComponent& script, const std::string_view name) noexcept {
    return script.exportValues.erase(std::string{name}) > 0U;
}

} // namespace UVE::Scene
