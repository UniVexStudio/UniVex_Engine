// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <cstdint>
#include <span>
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/vector3_uve.h"
namespace UVE::Math {
enum class PointTransformPathUVE : std::uint8_t { Automatic = 0, Scalar, Simd };
enum class PointTransformBackendUVE : std::uint8_t { Scalar = 0, Sse2 };
/// Compile-target capability, not measured speedup. No global CPU/fast-math flags are changed.
[[nodiscard]] PointTransformBackendUVE GetPointTransformBackendUVE() noexcept;
/// Deliberate SIMD boundary: affine batches retain TransformPointUVE's double accumulation and
/// operation order. Public vector/matrix sizes and float alignment are unchanged. No intrinsic
/// types or 16-byte alignment assumptions escape this header; never read a fourth Vector3 lane.
/// Equal lengths are required. Exact in-place is supported; partial overlap, bad sizes/paths and
/// unavailable explicit SIMD fail before writing. Automatic has a scalar fallback. Empty batches
/// are valid. Numeric/affine preconditions are the scalar function's (no perspective division).
[[nodiscard]] bool
TryTransformPointsUVE(const Matrix4x4UVE &matrix, std::span<const Vector3UVE> input,
                      std::span<Vector3UVE> output,
                      PointTransformPathUVE path = PointTransformPathUVE::Automatic) noexcept;
} // namespace UVE::Math
