// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// THE ONE AND ONLY PLACE data may cross between the two math libraries that coexist in this
// codebase (the 2026-09-17 audit's "two math libraries" finding):
//
//   * univex::math (this module's own Expose/univex/math): a deliberately tiny OpenGL-facing
//     kit - column-major Mat4 laid out exactly as glUniformMatrix4fv expects, perspective
//     projection mapping to OpenGL clip space (z in [-w, w]). Used by the Viewport's camera,
//     gizmo and grid renderers.
//
//   * UVE::Math (Engine/Runtime/Core/Math): the engine-wide kit - row-major Matrix4x4UVE,
//     perspective projection mapping to Vulkan-style depth range [0, 1]. Used by every
//     engine-side system (Scene, Physics, RenderSystems, ...).
//
// Both libraries use the SAME handedness and the SAME column-vector convention (a point is
// transformed as M * [x, y, z, 1]^T), so positions, directions and rigid/view transforms can
// cross this boundary safely with the field-by-field copies below. What may NEVER cross is a
// PROJECTION matrix: the two libraries bake different depth ranges into their projections,
// so converting one would silently corrupt depth. ToUveMatrix* below exists for world/view
// transforms only and says so on the function itself.
//
// Where this header may be used: the viewport engine-bridge (this directory) and the app
// layer atop it (Engine/App, Engine/Editor/EditorApp). Tools/check_math_boundary.py enforces
// the rule in CI: univex/math includes outside Viewport + those two app targets fail the build.

#pragma once

#include "univex/math/Mat4.h"
#include "univex/math/Vec.h"

#include "uve/math/matrix4x4_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"

namespace univex::integration {

// ---------------------------------------------------------------------------
// Vec2/Vec3 <-> Vector{2,3}UVE: two structurally-identical {x, y[, z]} float structs from
// unrelated math libraries. Plain field-by-field copies, not real conversions.
// ---------------------------------------------------------------------------

[[nodiscard]] inline UVE::Math::Vector2UVE ToUveVector2UVE(const univex::math::Vec2& value) noexcept {
    return UVE::Math::Vector2UVE{value.x, value.y};
}

[[nodiscard]] inline UVE::Math::Vector3UVE ToUveVector3UVE(const univex::math::Vec3& value) noexcept {
    return UVE::Math::Vector3UVE{value.x, value.y, value.z};
}

[[nodiscard]] inline univex::math::Vec3 FromUveVector3UVE(const UVE::Math::Vector3UVE& value) noexcept {
    return univex::math::Vec3{value.x, value.y, value.z};
}

// ---------------------------------------------------------------------------
// Mat4 -> Matrix4x4UVE: same linear map, different storage layout. Mat4 stores column-major
// (element (r, c) at m[c * 4 + r]); Matrix4x4UVE stores row-major (m[r][c]). Both interpret
// the matrix with the same column-vector convention, so the element-wise copy below IS the
// identity transformation - no transpose of meaning occurs.
//
// WARNING: valid for world and view transforms ONLY. Do NOT convert projection matrices
// through here: univex::math::Mat4::Perspective targets OpenGL clip space while
// Matrix4x4UVE::PerspectiveUVE targets a Vulkan-style [0, 1] depth range - a copied projection
// would carry the wrong depth mapping into the other renderer.
// ---------------------------------------------------------------------------

[[nodiscard]] inline UVE::Math::Matrix4x4UVE ToUveMatrixUVE(const univex::math::Mat4& value) noexcept {
    UVE::Math::Matrix4x4UVE result;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            result.m[row][col] = value.At(row, col);
        }
    }
    return result;
}

} // namespace univex::integration
