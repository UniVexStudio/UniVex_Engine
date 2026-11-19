// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include <memory>

#include "editor.h"

namespace UVE::Core {
class EngineCoreUVE;
class EngineServicesUVE;
}

namespace UVE::App::Viewports {

/// Lifecycle owner for all seven physically isolated editor 3D viewports:
///   1. `LevelViewportUVE`         (`level_viewport`)
///   2. `CameraPreviewViewportUVE` (`camera_preview_viewport`)
///   3. `EntityViewportUVE`        (`entity_viewport`)
///   4. `WidgetPreviewViewportUVE` (`widget_preview_viewport`)
///   5. `RetargetViewportUVE`      (`retarget_viewport`)
///   6. `MontageViewportUVE`       (`montage_viewport`)
///   7. `VfxViewportUVE`           (`vfx_viewport`)
///
/// Keeps OpenGL and Viewport module types isolated behind a pimpl while binding each window's
/// dedicated viewport directly to `EditorUVE::EditorViewportsUVE`.
class EditorViewportHostUVE final {
public:
    EditorViewportHostUVE(Editor::EditorUVE& editor, Core::EngineCoreUVE& engine);
    ~EditorViewportHostUVE();

    EditorViewportHostUVE(const EditorViewportHostUVE&) = delete;
    EditorViewportHostUVE& operator=(const EditorViewportHostUVE&) = delete;
    EditorViewportHostUVE(EditorViewportHostUVE&&) = delete;
    EditorViewportHostUVE& operator=(EditorViewportHostUVE&&) = delete;

    /// Registers all seven dedicated per-window viewport callbacks on `editor`.
    void BindToEditorUVE(Editor::EditorUVE& editor);

    /// Detaches all viewport callbacks from `editor`.
    void UnbindFromEditorUVE(Editor::EditorUVE& editor);

    /// Releases every viewport currently bound to `services` before that document scene closes.
    void ForgetServicesUVE(const Core::EngineServicesUVE& services);

private:
    struct ImplUVE;
    std::unique_ptr<ImplUVE> m_impl;
};

} // namespace UVE::App::Viewports
