// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace UVE::Scene {

/// One of the master spec's named built-in components (Part 7.3). Deliberately minimal
/// placeholder data: a path-based reference to the attached script asset. Part 8's C# bridge
/// (ScriptAPIUVE) decides the real entity-identity/marshaling mechanism later; this component
/// only records which script is attached.
struct ScriptComponentUVE final {
    std::string scriptAssetPath;
};

inline constexpr std::size_t kMaximumScriptAssetPathBytesUVE = 1024U;

/// Validates a script's project-relative virtual path without resolving or reading the asset. An
/// empty path remains the established no-script state; non-empty paths use canonical forward-slash
/// segments and cannot contain absolute, URI, Windows-drive, or traversal forms, embedded NULs, or
/// unbounded input.
[[nodiscard]] bool IsScriptAssetPathValidUVE(const std::string_view path) noexcept;

[[nodiscard]] bool IsScriptComponentValidUVE(const ScriptComponentUVE& component) noexcept;

} // namespace UVE::Scene
