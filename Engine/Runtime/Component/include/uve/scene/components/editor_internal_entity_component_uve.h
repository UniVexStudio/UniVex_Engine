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

} // namespace UVE::Scene
