// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <cstdint>
#include <span>
#include "matrix4x4.h"
#include "vector3.h"
namespace UVE::Math {
enum class PointTransformPath : std::uint8_t { Automatic = 0, Scalar, Simd };
enum class PointTransformBackend : std::uint8_t { Scalar = 0, Sse2 };
/// Compile-target capability, not measured speedup. No global CPU/fast-math flags are changed.
[[nodiscard]] PointTransformBackend GetPointTransformBackend() noexcept;
/// Deliberate SIMD boundary: affine batches retain TransformPoint's double accumulation and
/// operation order. Public vector/matrix sizes and float alignment are unchanged. No intrinsic
/// types or 16-byte alignment assumptions escape this header; never read a fourth Vector3 lane.
/// Equal lengths are required. Exact in-place is supported; partial overlap, bad sizes/paths and
/// unavailable explicit SIMD fail before writing. Automatic has a scalar fallback. Empty batches
/// are valid. Numeric/affine preconditions are the scalar function's (no perspective division).
[[nodiscard]] bool
TryTransformPoints(const Matrix4x4 &matrix, std::span<const Vector3> input,
                      std::span<Vector3> output,
                      PointTransformPath path = PointTransformPath::Automatic) noexcept;
} // namespace UVE::Math
