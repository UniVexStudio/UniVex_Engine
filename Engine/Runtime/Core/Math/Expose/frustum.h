// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <array>

#include "aabb.h"
#include "matrix4x4.h"
#include "plane.h"

namespace UVE::Math {

/// A camera's view frustum as 6 inward-facing planes (left, right, bottom, top, near, far, in
/// that order), used for frustum culling (Part 7.2, Increments 11-14).
/// Thread-safety: value type; safe to copy/pass freely, no shared state.
struct Frustum {
    std::array<Plane, 6> planes{};

    /// Extracts the 6 frustum planes from a combined view-projection matrix (standard
    /// Gribb-Hartmann plane extraction, a well-known public-domain technique), matching
    /// Matrix4x4::Perspective's `[0, 1]` depth-range convention.
    [[nodiscard]] static Frustum FromViewProjection(const Matrix4x4& viewProjection) noexcept;

    /// True iff `box` is at least partially inside every plane (i.e. not fully rejected by any
    /// single plane) — the standard conservative "is this box visible" test used for culling.
    [[nodiscard]] bool Intersects(const Aabb& box) const noexcept;
};

} // namespace UVE::Math
