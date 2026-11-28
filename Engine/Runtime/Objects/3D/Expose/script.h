// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string>
#include <string_view>

#include "entity.h"
#include "script_component.h"

namespace UVE::Scene {

class IEntityManager;

/// Authoring definition for the Script scene object: the component set and defaults a freshly
/// created Script entity attaches.
struct ScriptObjectDefinition final {
    static constexpr std::string_view defaultName = "Script";

    ScriptComponent script{};
};

[[nodiscard]] bool IsScriptObjectDefinitionValid(const ScriptObjectDefinition& value) noexcept;

void ApplyScriptObjectDefinition(IEntityManager& entityManager, Entity entity,
                                    const ScriptObjectDefinition& value);

/// Node policy for a `Script` scene object.
///
/// Owns Blueprint path classification and exported property lookup/mutation helpers used by
/// `EngineCore`'s script reconciliation and Inspector property binding.
class Script final {
public:
    Script() = delete;

    /// True when `script` is valid and has a non-empty `scriptAssetPath`.
    [[nodiscard]] static bool HasScriptPath(const ScriptComponent& script) noexcept;

    /// True when `scriptAssetPath` names a Blueprint: a class (`.uvbp`) or a widget's (`.uvwbp`).
    [[nodiscard]] static bool IsBlueprintPath(std::string_view scriptAssetPath) noexcept;

    /// True when `script` is valid and its `scriptAssetPath` is a runnable Blueprint path.
    [[nodiscard]] static bool IsRunnableBlueprint(const ScriptComponent& script) noexcept;

    /// Looks up an exported property override by `name`. Returns `nullptr` if not present.
    [[nodiscard]] static const std::string* FindExportedProperty(
        const ScriptComponent& script, std::string_view name) noexcept;

    /// Inserts or updates an exported property override `(name, value)` if both respect component
    /// byte-length and NUL-free constraints.
    [[nodiscard]] static bool TrySetExportedProperty(
        ScriptComponent& script, std::string_view name, std::string_view value);

    /// Removes an exported property override by `name`. Returns true if an entry was removed.
    static bool RemoveExportedProperty(
        ScriptComponent& script, std::string_view name) noexcept;
};

} // namespace UVE::Scene
