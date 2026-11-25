// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include <memory>

#include "editor.h"

namespace UVE::Core {
class EngineCore;
class EngineServices;
}

namespace UVE::App::Viewports {

/// Lifecycle owner for all seven physically isolated editor 3D viewports:
///   1. `LevelViewport`         (`level_viewport`)
///   2. `CameraPreviewViewport` (`camera_preview_viewport`)
///   3. `EntityViewport`        (`entity_viewport`)
///   4. `WidgetPreviewViewport` (`widget_preview_viewport`)
///   5. `RetargetViewport`      (`retarget_viewport`)
///   6. `MontageViewport`       (`montage_viewport`)
///   7. `VfxViewport`           (`vfx_viewport`)
///
/// Keeps OpenGL and Viewport module types isolated behind a pimpl while binding each window's
/// dedicated viewport directly to `Editor::EditorViewports`.
class EditorViewportHost final {
public:
    EditorViewportHost(Editor::Editor& editor, Core::EngineCore& engine);
    ~EditorViewportHost();

    EditorViewportHost(const EditorViewportHost&) = delete;
    EditorViewportHost& operator=(const EditorViewportHost&) = delete;
    EditorViewportHost(EditorViewportHost&&) = delete;
    EditorViewportHost& operator=(EditorViewportHost&&) = delete;

    /// Registers all seven dedicated per-window viewport callbacks on `editor`.
    void BindToEditor(Editor::Editor& editor);

    /// Detaches all viewport callbacks from `editor`.
    void UnbindFromEditor(Editor::Editor& editor);

    /// Releases every viewport currently bound to `services` before that document scene closes.
    void ForgetServices(const Core::EngineServices& services);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace UVE::App::Viewports
