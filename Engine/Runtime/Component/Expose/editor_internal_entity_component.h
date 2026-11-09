// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

namespace UVE::Scene {

/// Empty tag component marking an entity as internal engine/editor tooling infrastructure - never
/// real, authored document content. Examples: the editor Viewport's hidden free-look-camera proxy
/// entity, a windowed-mode-only verification fixture. `SceneGraphUVE::AttachTransformUVE()` always
/// makes an entity a scene root (no parent), which otherwise makes such an entity indistinguishable
/// from real document content to anything walking scene roots (Play-mode snapshot capture/restore,
/// the Scene Hierarchy panel, "clear document scene", etc.) - this tag is the discriminator those
/// callers filter on instead.
/// Thread-safety: empty value type; trivially safe to copy/move.
struct EditorInternalEntityComponentUVE final {};

/// Always true: a tag has no fields and therefore no invalid state. Present so the component has
/// the same validate seam every other component has.
[[nodiscard]] bool IsEditorInternalEntityComponentValidUVE(
    const EditorInternalEntityComponentUVE& component) noexcept;

/// True when the tag pointer is non-null (the entity carries the internal-tooling tag).
[[nodiscard]] bool IsEditorInternalEntityTagPresentUVE(
    const EditorInternalEntityComponentUVE* component) noexcept;

/// True when an entity should be shown in the authored Scene Hierarchy panel (no internal tag).
[[nodiscard]] bool ShouldIncludeInDocumentHierarchyUVE(
    const EditorInternalEntityComponentUVE* component) noexcept;

/// True when an entity belongs to the authored document for save/export and Play-mode snapshots.
[[nodiscard]] bool ShouldSerializeDocumentEntityUVE(
    const EditorInternalEntityComponentUVE* component) noexcept;

/// True when an entity should be destroyed during a document-scene clear (internal proxies survive).
[[nodiscard]] bool ShouldClearWithDocumentSceneUVE(
    const EditorInternalEntityComponentUVE* component) noexcept;

} // namespace UVE::Scene
