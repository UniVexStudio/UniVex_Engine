// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "point_transform_batch.h"
#include <array>
#include <functional>
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define UVE_POINT_BATCH_SSE2 1
#include <emmintrin.h>
#else
#define UVE_POINT_BATCH_SSE2 0
#endif
namespace UVE::Math {
namespace {
#if UVE_POINT_BATCH_SSE2
void Sse2(const Matrix4x4 &matrix, std::span<const Vector3> input,
             std::span<Vector3> output) noexcept {
    // Two output rows per lane pair; scalar field loads avoid overreading Vector3 or requiring alignment.
    const auto cx = _mm_set_pd(matrix.m[1][0], matrix.m[0][0]);
    const auto cy = _mm_set_pd(matrix.m[1][1], matrix.m[0][1]);
    const auto cz = _mm_set_pd(matrix.m[1][2], matrix.m[0][2]);
    const auto translation = _mm_set_pd(matrix.m[1][3], matrix.m[0][3]);
    for (std::size_t i = 0; i < input.size(); ++i) {
        const Vector3 point = input[i];
        const double x = point.x, y = point.y, z = point.z;
        auto xy = _mm_mul_pd(cx, _mm_set1_pd(x));
        xy = _mm_add_pd(xy, _mm_mul_pd(cy, _mm_set1_pd(y)));
        xy = _mm_add_pd(xy, _mm_mul_pd(cz, _mm_set1_pd(z)));
        xy = _mm_add_pd(xy, translation);
        std::array<double, 2> result{};
        _mm_storeu_pd(result.data(), xy);
        const double resultZ = static_cast<double>(matrix.m[2][0]) * x +
                               static_cast<double>(matrix.m[2][1]) * y +
                               static_cast<double>(matrix.m[2][2]) * z + static_cast<double>(matrix.m[2][3]);
        output[i] = {static_cast<float>(result[0]), static_cast<float>(result[1]),
                     static_cast<float>(resultZ)};
    }
}
#endif
} // namespace
PointTransformBackend GetPointTransformBackend() noexcept {
#if UVE_POINT_BATCH_SSE2
    return PointTransformBackend::Sse2;
#else
    return PointTransformBackend::Scalar;
#endif
}
bool TryTransformPoints(const Matrix4x4 &matrix, std::span<const Vector3> input,
                           std::span<Vector3> output, PointTransformPath path) noexcept {
    if (path != PointTransformPath::Automatic && path != PointTransformPath::Scalar &&
        path != PointTransformPath::Simd)
        return false;
    if (path == PointTransformPath::Simd &&
        GetPointTransformBackend() == PointTransformBackend::Scalar)
        return false;
    if (input.size() != output.size())
        return false;
    if (input.empty())
        return true;
    if (input.data() != output.data()) {
        const std::less<const Vector3 *> less;
        if (less(input.data(), output.data() + output.size()) &&
            less(output.data(), input.data() + input.size()))
            return false;
    }
#if UVE_POINT_BATCH_SSE2
    if (path != PointTransformPath::Scalar) {
        Sse2(matrix, input, output);
        return true;
    }
#endif
    for (std::size_t i = 0; i < input.size(); ++i)
        output[i] = TransformPoint(matrix, input[i]);
    return true;
}
} // namespace UVE::Math
