// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string>
#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/component/script_component_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the Script scene object: the component set and defaults a freshly
/// created Script entity attaches.
struct ScriptObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Script";

    ScriptComponentUVE script{};
};

[[nodiscard]] bool IsScriptObjectDefinitionValidUVE(const ScriptObjectDefinitionUVE& value) noexcept;

void ApplyScriptObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                    const ScriptObjectDefinitionUVE& value);

/// Node policy for a `Script` scene object.
///
/// Owns UVScript `.uvs` path classification and exported property lookup/mutation helpers used by
/// `EngineCoreUVE`'s script reconciliation and Inspector property binding.
class ScriptUVE final {
public:
    ScriptUVE() = delete;

    static constexpr std::string_view kUVScriptExtensionUVE = ".uvs";

    /// True when `script` is valid and has a non-empty `scriptAssetPath`.
    [[nodiscard]] static bool HasScriptPathUVE(const ScriptComponentUVE& script) noexcept;

    /// True when `scriptAssetPath` ends with `.uvs`.
    [[nodiscard]] static bool IsUVScriptPathUVE(std::string_view scriptAssetPath) noexcept;

    /// True when `script` is valid and its `scriptAssetPath` is a runnable `.uvs` path.
    [[nodiscard]] static bool IsRunnableUVScriptUVE(const ScriptComponentUVE& script) noexcept;

    /// Looks up an exported property override by `name`. Returns `nullptr` if not present.
    [[nodiscard]] static const std::string* FindExportedPropertyUVE(
        const ScriptComponentUVE& script, std::string_view name) noexcept;

    /// Inserts or updates an exported property override `(name, value)` if both respect component
    /// byte-length and NUL-free constraints.
    [[nodiscard]] static bool TrySetExportedPropertyUVE(
        ScriptComponentUVE& script, std::string_view name, std::string_view value);

    /// Removes an exported property override by `name`. Returns true if an entry was removed.
    static bool RemoveExportedPropertyUVE(
        ScriptComponentUVE& script, std::string_view name) noexcept;
};

} // namespace UVE::Scene
