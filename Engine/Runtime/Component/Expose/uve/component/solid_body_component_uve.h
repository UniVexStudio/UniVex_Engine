// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

namespace UVE::Scene {

/// The shared state of SolidBody3D, the abstract base of every physics object that is a body - one
/// that is stopped by what it hits, as opposed to an area, which only notices overlaps. No node is
/// a SolidBody3D on its own; its kinds (CharacterBody3D, and the other bodies as they join the
/// chain) carry this component and add their own.
struct SolidBodyComponentUVE final {
    /// Keeps the body from moving along a world axis at all - a side-scroller's character locked
    /// to the X/Y plane locks Z. The body's own motion along a locked axis is dropped, not scaled.
    bool lockMotionX = false;
    bool lockMotionY = false;
    bool lockMotionZ = false;

    [[nodiscard]] bool operator==(const SolidBodyComponentUVE&) const = default;
};

} // namespace UVE::Scene
