// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/math/point_transform_batch_uve.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>
#include <gtest/gtest.h>
#include "uve/math/aabb_uve.h"
#include "uve/math/random_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector4_uve.h"
namespace {
using namespace UVE::Math;
static_assert(sizeof(Vector2UVE) == 2U * sizeof(float) && alignof(Vector2UVE) == alignof(float));
static_assert(sizeof(Vector3UVE) == 3U * sizeof(float) && alignof(Vector3UVE) == alignof(float));
static_assert(sizeof(Vector4UVE) == 4U * sizeof(float) && alignof(Vector4UVE) == alignof(float));
static_assert(sizeof(QuaternionUVE) == 4U * sizeof(float) && alignof(QuaternionUVE) == alignof(float));
static_assert(sizeof(Matrix4x4UVE) == 16U * sizeof(float) && alignof(Matrix4x4UVE) == alignof(float));
static_assert(std::is_trivially_copyable_v<Vector3UVE> && std::is_standard_layout_v<Vector3UVE>);
static_assert(offsetof(Vector3UVE, z) == 2U * sizeof(float));
void SameUVE(float a, float b) {
    if (std::isnan(a))
        EXPECT_TRUE(std::isnan(b));
    else
        EXPECT_EQ(std::bit_cast<std::uint32_t>(a), std::bit_cast<std::uint32_t>(b));
}
void SameUVE(Vector3UVE a, Vector3UVE b) {
    SameUVE(a.x, b.x);
    SameUVE(a.y, b.y);
    SameUVE(a.z, b.z);
}
TEST(PointTransformBatchUVETest, ScalarAutomaticAndExplicitSimdMatchReference) {
    RandomUVE random{123U};
    std::vector<Vector3UVE> points;
    for (int i = 0; i < 257; ++i)
        points.push_back({random.NextFloatUVE() * 200 - 100, random.NextFloatUVE() * 200 - 100,
                          random.NextFloatUVE() * 200 - 100});
    Matrix4x4UVE matrix;
    for (auto &row : matrix.m)
        for (auto &v : row)
            v = random.NextFloatUVE() * 20 - 10;
    for (const auto path :
         {PointTransformPathUVE::Scalar, PointTransformPathUVE::Automatic, PointTransformPathUVE::Simd}) {
        std::vector<Vector3UVE> output(points.size(), Vector3UVE{42, 42, 42});
        const auto before = output;
        const bool applied = TryTransformPointsUVE(matrix, points, output, path);
        if (path == PointTransformPathUVE::Simd &&
            GetPointTransformBackendUVE() == PointTransformBackendUVE::Scalar) {
            EXPECT_FALSE(applied);
            EXPECT_EQ(output, before);
        } else {
            ASSERT_TRUE(applied);
            for (std::size_t i = 0; i < points.size(); ++i)
                SameUVE(TransformPointUVE(matrix, points[i]), output[i]);
        }
    }
}
TEST(PointTransformBatchUVETest, FloatAlignedNotSimdAlignedStorageAndOddTailAreSafe) {
    struct alignas(16) StorageUVE {
        float prefix = 42;
        std::array<Vector3UVE, 5> points{};
        float suffix = 73;
    };
    static_assert(offsetof(StorageUVE, points) == sizeof(float));
    StorageUVE input, output;
    EXPECT_NE(reinterpret_cast<std::uintptr_t>(input.points.data()) % 16U, 0U);
    for (std::size_t i = 0; i < input.points.size(); ++i)
        input.points[i] = {static_cast<float>(i), 2, 3};
    const auto matrix = Matrix4x4UVE::ComposeTrsUVE({5, 6, 7}, {}, {2, -3, 4});
    ASSERT_TRUE(TryTransformPointsUVE(matrix, input.points, output.points));
    for (std::size_t i = 0; i < input.points.size(); ++i)
        SameUVE(TransformPointUVE(matrix, input.points[i]), output.points[i]);
    EXPECT_EQ(output.prefix, 42);
    EXPECT_EQ(output.suffix, 73);
}
TEST(PointTransformBatchUVETest, InPlaceAndAdjacentDisjointRangesWork) {
    std::array<Vector3UVE, 4> data{{{1, 2, 3}, {4, 5, 6}, {0, 0, 0}, {0, 0, 0}}};
    const auto matrix = Matrix4x4UVE::ComposeTrsUVE({2, 3, 4}, {}, {2, 2, 2});
    const auto before = data;
    ASSERT_TRUE(TryTransformPointsUVE(matrix, std::span{data}.first(2U), std::span{data}.last(2U)));
    for (std::size_t i = 0; i < 2U; ++i)
        SameUVE(TransformPointUVE(matrix, before[i]), data[i + 2U]);
    const auto input = data;
    ASSERT_TRUE(TryTransformPointsUVE(matrix, data, data));
    for (std::size_t i = 0; i < data.size(); ++i)
        SameUVE(TransformPointUVE(matrix, input[i]), data[i]);
}
TEST(PointTransformBatchUVETest, PartialOverlapAndInvalidArgumentsFailBeforeMutation) {
    std::array<Vector3UVE, 4> points{{{1, 2, 3}, {4, 5, 6}, {7, 8, 9}, {10, 11, 12}}};
    const auto before = points;
    const std::span<Vector3UVE> s{points};
    EXPECT_FALSE(TryTransformPointsUVE({}, s.first(3U), s.subspan(1U)));
    EXPECT_FALSE(TryTransformPointsUVE({}, s.subspan(1U), s.first(3U)));
    EXPECT_FALSE(TryTransformPointsUVE({}, s.first(2U), s.first(1U)));
    EXPECT_FALSE(TryTransformPointsUVE({}, s, s, static_cast<PointTransformPathUVE>(255)));
    EXPECT_EQ(points, before);
    EXPECT_TRUE(TryTransformPointsUVE({}, {}, {}));
}
TEST(PointTransformBatchUVETest, DoubleAccumulationPreservesCancellation) {
    Matrix4x4UVE matrix;
    matrix.m[0][0] = 1e38F;
    matrix.m[0][1] = 1e38F;
    matrix.m[0][3] = 7;
    const std::array<Vector3UVE, 1> input{{{1e38F, -1e38F, 0}}};
    std::array<Vector3UVE, 1> output;
    ASSERT_TRUE(TryTransformPointsUVE(matrix, input, output));
    SameUVE(TransformPointUVE(matrix, input[0]), output[0]);
    EXPECT_EQ(output[0].x, 7);
}
TEST(PointTransformBatchUVETest, NonFiniteSubnormalAndSignedZeroKeepScalarPolicy) {
    const auto inf = std::numeric_limits<float>::infinity(), nan = std::numeric_limits<float>::quiet_NaN();
    const auto tiny = std::numeric_limits<float>::denorm_min();
    const std::array<Vector3UVE, 4> input{{{inf, 0, 0}, {nan, 1, 2}, {tiny, -tiny, tiny}, {-0.0F, 0, -0.0F}}};
    std::array<Vector3UVE, 4> output{};
    ASSERT_TRUE(TryTransformPointsUVE({}, input, output));
    for (std::size_t i = 0; i < input.size(); ++i)
        SameUVE(TransformPointUVE({}, input[i]), output[i]);
}
TEST(PointTransformBatchUVETest, AabbConsumerMatchesScalarCornerBounds) {
    RandomUVE random{73U};
    const AabbUVE box{{-3, -2, -1}, {5, 6, 7}};
    const std::array<Vector3UVE, 8> corners{
        {{-3, -2, -1}, {5, -2, -1}, {-3, 6, -1}, {5, 6, -1}, {-3, -2, 7}, {5, -2, 7}, {-3, 6, 7}, {5, 6, 7}}};
    for (int iteration = 0; iteration < 100; ++iteration) {
        Matrix4x4UVE matrix;
        for (std::size_t row = 0; row < 3U; ++row)
            for (auto &v : matrix.m[row])
                v = random.NextFloatUVE() * 100 - 50;
        auto minimum = TransformPointUVE(matrix, corners[0]), maximum = minimum;
        for (const auto corner : corners) {
            const auto point = TransformPointUVE(matrix, corner);
            minimum = {std::min(minimum.x, point.x), std::min(minimum.y, point.y),
                       std::min(minimum.z, point.z)};
            maximum = {std::max(maximum.x, point.x), std::max(maximum.y, point.y),
                       std::max(maximum.z, point.z)};
        }
        const auto actual = box.TransformUVE(matrix);
        SameUVE(minimum, actual.min);
        SameUVE(maximum, actual.max);
    }
}
} // namespace
