// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <string>

#include "matrix4x4.h"

namespace UVE::Math {

/// A row-major 3x3 single-precision matrix (`m[row][col]`), used for the renderer's normal
/// matrices (transpose-of-inverse of a world matrix's upper 3x3) and any future rotation/scale
/// work that has no translation. Same column-vector convention as Matrix4x4: composing
/// `lhs * rhs` means "apply `rhs` first, then `lhs`". Deliberately minimal, matching
/// Matrix4x4's precedent: only what the normal-matrix path actually needs — identity,
/// multiply, transpose, inverse, conversion to/from Matrix4x4, and a debug formatter. No
/// vector multiply yet: normals are transformed on the GPU, so no CPU caller needs one.
/// Thread-safety: value type; safe to copy/pass freely, no shared state.
struct Matrix3x3 {
    float m[3][3] = {
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
    };

    /// Returns the identity matrix. Equivalent to the default constructor; provided for
    /// readability at call sites.
    [[nodiscard]] static constexpr Matrix3x3 Identity() noexcept { return Matrix3x3{}; }
};

[[nodiscard]] constexpr bool operator==(const Matrix3x3& lhs, const Matrix3x3& rhs) noexcept {
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (lhs.m[row][col] != rhs.m[row][col]) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] constexpr bool operator!=(const Matrix3x3& lhs, const Matrix3x3& rhs) noexcept {
    return !(lhs == rhs);
}

/// Matrix multiplication: `lhs * rhs` means "apply `rhs` first, then `lhs`" (column-vector
/// convention), matching Matrix4x4's operator*.
[[nodiscard]] Matrix3x3 operator*(const Matrix3x3& lhs, const Matrix3x3& rhs) noexcept;

/// Returns `matrix` transposed (`result.m[row][col] == matrix.m[col][row]`). Always succeeds -
/// unlike TryInverse(), transposition has no degenerate case.
[[nodiscard]] Matrix3x3 Transpose(const Matrix3x3& matrix) noexcept;

/// Attempts a 3x3 matrix inverse via Gauss-Jordan elimination with partial pivoting, mirroring
/// the Matrix4x4 overload exactly (same double-precision accumulation, same minimum-pivot
/// threshold). Returns false (leaving `outInverse` unspecified) if `matrix` is non-finite or
/// numerically singular — callers must check the return value rather than assume a result.
[[nodiscard]] bool TryInverse(const Matrix3x3& matrix, Matrix3x3& outInverse) noexcept;

/// Extracts the upper-left 3x3 (rotation and scale) of `matrix`, dropping its translation.
/// This is the normal-matrix input: normals are directions, so translation must not reach them.
[[nodiscard]] Matrix3x3 ToMatrix3x3(const Matrix4x4& matrix) noexcept;

/// Embeds `matrix` in the upper-left 3x3 of an identity 4x4 (fourth row and column
/// `[0, 0, 0, 1]`). Used where the GPU interface only accepts 4x4 — the normal-matrix uniform
/// and instance buffer — while the value being carried is honestly 3x3.
[[nodiscard]] Matrix4x4 ToMatrix4x4(const Matrix3x3& matrix) noexcept;

/// Formats `matrix` as three `"(row0; row1; row2)"`-style rows, for logging/debugging.
[[nodiscard]] std::string ToString(const Matrix3x3& matrix);

} // namespace UVE::Math
