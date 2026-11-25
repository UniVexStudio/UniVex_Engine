// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "vector3.h"

namespace UVE::Math {

/// A ray in `origin + direction * t` form, used by RaycastSystem (Part 7.5, Increment 16).
/// Deliberately minimal, matching Plane's precedent: no logic beyond the type itself.
/// Contract: `direction` is expected to be unit length (not enforced — matches Normalize's
/// documented-not-enforced precedent). A hit/miss result is still correct with a non-unit
/// direction, but any reported distance is then in units of `direction`'s own length rather than
/// a literal distance.
/// Thread-safety: value type; safe to copy/pass freely, no shared state.
struct Ray {
    Vector3 origin{};
    Vector3 direction{0.0F, 0.0F, -1.0F};
};

} // namespace UVE::Math
