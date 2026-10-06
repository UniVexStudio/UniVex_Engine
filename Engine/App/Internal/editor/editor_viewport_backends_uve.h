// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include <cstdint>
#include <memory>

#include "uve/editor/editor_uve.h"

namespace UVE::Core {
class EngineCoreUVE;
}

namespace UVE::App {

/// Owns the three physically independent editor viewport renderers.
///
/// The implementation is intentionally outside the editor launcher: GL resources, cameras,
/// gestures, and render targets have deterministic ownership and cannot accidentally become
/// launcher-global state. The pimpl also keeps OpenGL and Viewport module types out of this API.
class EditorViewportBackendsUVE final {
public:
    EditorViewportBackendsUVE(Editor::EditorUVE& editor, Core::EngineCoreUVE& engine);
    ~EditorViewportBackendsUVE();

    EditorViewportBackendsUVE(const EditorViewportBackendsUVE&) = delete;
    EditorViewportBackendsUVE& operator=(const EditorViewportBackendsUVE&) = delete;
    EditorViewportBackendsUVE(EditorViewportBackendsUVE&&) = delete;
    EditorViewportBackendsUVE& operator=(EditorViewportBackendsUVE&&) = delete;

    [[nodiscard]] std::uint64_t RenderUVE(
        Editor::EditorUVE::ViewportContextUVE context,
        const Math::Vector2UVE& availableSize,
        Math::Vector2UVE& outUsedSize,
        const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState);

private:
    struct ImplUVE;
    std::unique_ptr<ImplUVE> m_impl;
};

} // namespace UVE::App
