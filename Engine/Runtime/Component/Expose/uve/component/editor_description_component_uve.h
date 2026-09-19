// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <string>

namespace UVE::Scene {

inline constexpr std::size_t kMaximumEditorDescriptionBytesUVE = 4096U;

/// Free-text author notes attached to a scene entity: why this node exists, what a magic number
/// on it means, which bug it works around.
///
/// WHY IT IS A COMPONENT RATHER THAN A FIELD ON EVERY NODE. Most entities never carry one, and a
/// string on every transform would cost every scene memory for a feature a handful of nodes use.
/// As an optional component it is paid for only where it is written.
///
/// Editor-facing and deliberately inert: nothing at runtime reads it, and nothing should start.
/// The moment a system branches on a description, authors lose the ability to write freely in it -
/// which is the only thing it is for.
///
/// Bounded at 4 KB. Not a technical limit but an honest one: a description longer than that is
/// documentation that belongs in the project's own files, where it can be searched and reviewed,
/// rather than buried in a scene node's metadata.
struct EditorDescriptionComponentUVE final {
    std::string description;
};

[[nodiscard]] bool IsEditorDescriptionComponentValidUVE(const EditorDescriptionComponentUVE& component) noexcept;

} // namespace UVE::Scene
