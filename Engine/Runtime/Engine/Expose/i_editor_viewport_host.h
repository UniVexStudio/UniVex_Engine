//                                UniVex Engine
//
// UniVex Engine (UVE) — Proprietary Game Engine
// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// Unauthorized copying, modification, distribution, or use of this code
// in whole or in part is strictly prohibited without express written
// permission from UniVex Studios.
// Violators will be prosecuted to the fullest extent of the law.


#pragma once

#include <optional>

#include "render_resource_descs.h"

namespace UVE::Core {

/// A narrow Core-owned control seam, deliberately separate from ISimulationControl (which by its
/// own contract must not expose renderer implementation details to editor code): the one thing an
/// embedding editor needs to tell Core about its own presentation layout - the pixel sub-rect of
/// the window its 3D viewport panel actually occupies, so Core's render target sizing, projection
/// aspect ratio, and on-screen placement all track that panel rather than the full window. See
/// EngineCore::SetEditorViewportRegion()'s own doc comment for the full contract.
class IEditorViewportHost {
public:
    virtual ~IEditorViewportHost() = default;

    virtual void SetEditorViewportRegion(std::optional<Render::ViewportRect> region) noexcept = 0;
};

} // namespace UVE::Core
