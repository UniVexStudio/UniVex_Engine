// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "prism/Widget.h"

#include <memory>

namespace prism {
class Button;
class Box;
class Segmented;
struct Rect;
} // namespace prism

namespace UVE::Editor {

class EditorUVE;

/// Sub row 2: a viewport's settings strip - gizmo mode, snapping, grid, projection and facing,
/// shading, and the preview sun / sky with their settings. It edits the editor's viewport overlay
/// state, which is the active document's: the level's, or the open entity's inside its scope, so
/// the level editor and the Entity Editor each show one over their own viewport.
class EditorPrismViewToolbarUVE final {
public:
    explicit EditorPrismViewToolbarUVE(EditorUVE& editor);
    /// `gizmoOnly`: just the gizmo mode, snapping and grid (the Entity Editor's view).
    [[nodiscard]] prism::Ref<prism::Widget> BuildUVE(bool gizmoOnly = false);
    /// Re-reads the overlay state (call with the strip's document active).
    void RefreshUVE();

private:
    void ShowPreviewSettingsUVE(prism::Rect anchor);
    [[nodiscard]] prism::Ref<prism::Widget> FinishBarUVE(const prism::Ref<prism::Box>& settings);

    EditorUVE& m_editor;
    std::shared_ptr<prism::Segmented> m_gizmoModes;
    std::shared_ptr<prism::Button> m_snapButton;
    std::shared_ptr<prism::Button> m_gridButton;
    std::shared_ptr<prism::Button> m_projectionButton;
    std::shared_ptr<prism::Button> m_shadingButton;
    std::shared_ptr<prism::Button> m_sunButton;
    std::shared_ptr<prism::Button> m_skyButton;
};

} // namespace UVE::Editor
