// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "matrix3x3.h"

#include <cmath>
#include <limits>
#include <numbers>
#include <string>

#include "matrix4x4.h"
#include "quaternion.h"
#include "vector3.h"

#include <gtest/gtest.h>

namespace UVE::Math::Tests {
namespace {

constexpr float kEpsilon = 1e-4F;

TEST(Matrix3x3Test, Identity_IsIdentityMatrix) {
    constexpr Matrix3x3 identity = Matrix3x3::Identity();
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            EXPECT_EQ(identity.m[row][col], row == col ? 1.0F : 0.0F);
        }
    }
}

TEST(Matrix3x3Test, EqualityOperators_CompareAllComponents) {
    Matrix3x3 a = Matrix3x3::Identity();
    a.m[1][2] = 5.0F;
    const Matrix3x3 b = a;
    Matrix3x3 c = a;
    c.m[1][2] = 6.0F;

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
    EXPECT_FALSE(a == c);
}

TEST(Matrix3x3Test, MatrixMultiply_WithIdentity_IsUnchanged) {
    Matrix3x3 matrix = Matrix3x3::Identity();
    matrix.m[0][1] = 2.0F;
    matrix.m[1][0] = 3.0F;
    matrix.m[2][2] = 4.0F;

    EXPECT_EQ(matrix * Matrix3x3::Identity(), matrix);
    EXPECT_EQ(Matrix3x3::Identity() * matrix, matrix);
}

TEST(Matrix3x3Test, MatrixMultiply_KnownMatrices_MatchesHandComputedValue) {
    Matrix3x3 lhs = Matrix3x3::Identity();
    lhs.m[0][1] = 2.0F;
    Matrix3x3 rhs = Matrix3x3::Identity();
    rhs.m[1][0] = 3.0F;

    // [1 2 0]   [1 0 0]   [7 2 0]
    // [0 1 0] * [3 1 0] = [3 1 0]
    // [0 0 1]   [0 0 1]   [0 0 1]
    const Matrix3x3 product = lhs * rhs;

    EXPECT_FLOAT_EQ(product.m[0][0], 7.0F);
    EXPECT_FLOAT_EQ(product.m[0][1], 2.0F);
    EXPECT_FLOAT_EQ(product.m[0][2], 0.0F);
    EXPECT_FLOAT_EQ(product.m[1][0], 3.0F);
    EXPECT_FLOAT_EQ(product.m[1][1], 1.0F);
    EXPECT_FLOAT_EQ(product.m[1][2], 0.0F);
    EXPECT_FLOAT_EQ(product.m[2][0], 0.0F);
    EXPECT_FLOAT_EQ(product.m[2][1], 0.0F);
    EXPECT_FLOAT_EQ(product.m[2][2], 1.0F);
}

TEST(Matrix3x3Test, Transpose_Identity_IsUnchanged) {
    constexpr Matrix3x3 identity = Matrix3x3::Identity();

    EXPECT_EQ(Transpose(identity), identity);
}

TEST(Matrix3x3Test, Transpose_SwapsRowsAndColumns) {
    Matrix3x3 matrix = Matrix3x3::Identity();
    matrix.m[0][2] = 5.0F;
    matrix.m[1][0] = 7.0F;

    const Matrix3x3 transposed = Transpose(matrix);

    EXPECT_FLOAT_EQ(transposed.m[2][0], 5.0F);
    EXPECT_FLOAT_EQ(transposed.m[0][1], 7.0F);
    EXPECT_FLOAT_EQ(transposed.m[0][2], 0.0F);
    EXPECT_FLOAT_EQ(transposed.m[1][0], 0.0F);
}

TEST(Matrix3x3Test, Transpose_AppliedTwice_RestoresOriginal) {
    Matrix3x3 matrix = Matrix3x3::Identity();
    matrix.m[0][1] = 2.0F;
    matrix.m[1][2] = 3.0F;
    matrix.m[2][0] = 4.0F;

    const Matrix3x3 roundTripped = Transpose(Transpose(matrix));

    EXPECT_EQ(roundTripped, matrix);
}

TEST(Matrix3x3Test, TryInverse_Identity_ReturnsIdentity) {
    constexpr Matrix3x3 identity = Matrix3x3::Identity();
    Matrix3x3 inverse{};

    ASSERT_TRUE(TryInverse(identity, inverse));
    EXPECT_EQ(inverse, identity);
}

TEST(Matrix3x3Test, TryInverse_KnownMatrix_MatchesHandComputedValue) {
    // Textbook adjugate case (determinant exactly 1, so the inverse is exact integers).
    Matrix3x3 matrix{};
    matrix.m[0][0] = 1.0F;
    matrix.m[0][1] = 2.0F;
    matrix.m[0][2] = 3.0F;
    matrix.m[1][0] = 0.0F;
    matrix.m[1][1] = 1.0F;
    matrix.m[1][2] = 4.0F;
    matrix.m[2][0] = 5.0F;
    matrix.m[2][1] = 6.0F;
    matrix.m[2][2] = 0.0F;
    Matrix3x3 inverse{};

    ASSERT_TRUE(TryInverse(matrix, inverse));

    EXPECT_FLOAT_EQ(inverse.m[0][0], -24.0F);
    EXPECT_FLOAT_EQ(inverse.m[0][1], 18.0F);
    EXPECT_FLOAT_EQ(inverse.m[0][2], 5.0F);
    EXPECT_FLOAT_EQ(inverse.m[1][0], 20.0F);
    EXPECT_FLOAT_EQ(inverse.m[1][1], -15.0F);
    EXPECT_FLOAT_EQ(inverse.m[1][2], -4.0F);
    EXPECT_FLOAT_EQ(inverse.m[2][0], -5.0F);
    EXPECT_FLOAT_EQ(inverse.m[2][1], 4.0F);
    EXPECT_FLOAT_EQ(inverse.m[2][2], 1.0F);
}

TEST(Matrix3x3Test, TryInverse_NonUniformScaleTrs_MultipliesBackToIdentity) {
    const Matrix4x4 world = Matrix4x4::ComposeTrs(
        Vector3{3.0F, -4.0F, 5.0F}, Quaternion{0.0F, 0.0F, std::numbers::sqrt2_v<float> / 2.0F,
                                                     std::numbers::sqrt2_v<float> / 2.0F},
        Vector3{2.0F, 3.0F, 4.0F});
    const Matrix3x3 matrix = ToMatrix3x3(world);
    Matrix3x3 inverse{};

    ASSERT_TRUE(TryInverse(matrix, inverse));
    const Matrix3x3 roundTripped = matrix * inverse;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            EXPECT_NEAR(roundTripped.m[row][col], Matrix3x3::Identity().m[row][col], kEpsilon);
        }
    }
}

TEST(Matrix3x3Test, TryInverse_SingularMatrix_ReturnsFalse) {
    Matrix3x3 zero{};
    zero.m[0][0] = 0.0F;
    zero.m[1][1] = 0.0F;
    zero.m[2][2] = 0.0F;
    Matrix3x3 duplicateRows = Matrix3x3::Identity();
    duplicateRows.m[1][0] = duplicateRows.m[0][0];
    duplicateRows.m[1][1] = duplicateRows.m[0][1];
    duplicateRows.m[1][2] = duplicateRows.m[0][2];
    Matrix3x3 inverse{};

    EXPECT_FALSE(TryInverse(zero, inverse));
    EXPECT_FALSE(TryInverse(duplicateRows, inverse));
}

TEST(Matrix3x3Test, TryInverse_NonFiniteMatrix_ReturnsFalse) {
    Matrix3x3 matrix = Matrix3x3::Identity();
    matrix.m[1][1] = std::numeric_limits<float>::infinity();
    Matrix3x3 inverse{};

    EXPECT_FALSE(TryInverse(matrix, inverse));
}

TEST(Matrix3x3Test, ToMatrix3x3_TranslationOnlyTrs_IsIdentity) {
    const Matrix4x4 world = Matrix4x4::ComposeTrs(Vector3{3.0F, -4.0F, 5.0F}, Quaternion{},
                                                           Vector3{1.0F, 1.0F, 1.0F});

    EXPECT_EQ(ToMatrix3x3(world), Matrix3x3::Identity());
}

TEST(Matrix3x3Test, ToMatrix3x3_ScaleOnlyTrs_IsDiagonalScale) {
    const Matrix4x4 world = Matrix4x4::ComposeTrs(Vector3{}, Quaternion{},
                                                           Vector3{2.0F, 3.0F, 4.0F});

    const Matrix3x3 upper = ToMatrix3x3(world);

    EXPECT_FLOAT_EQ(upper.m[0][0], 2.0F);
    EXPECT_FLOAT_EQ(upper.m[1][1], 3.0F);
    EXPECT_FLOAT_EQ(upper.m[2][2], 4.0F);
    EXPECT_FLOAT_EQ(upper.m[0][1], 0.0F);
    EXPECT_FLOAT_EQ(upper.m[1][0], 0.0F);
}

TEST(Matrix3x3Test, ToMatrix4x4_EmbedsWithIdentityBorder) {
    Matrix3x3 matrix = Matrix3x3::Identity();
    matrix.m[0][1] = 2.0F;
    matrix.m[1][2] = 3.0F;
    matrix.m[2][0] = 4.0F;

    const Matrix4x4 embedded = ToMatrix4x4(matrix);

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            EXPECT_FLOAT_EQ(embedded.m[row][col], matrix.m[row][col]);
        }
    }
    for (int i = 0; i < 3; ++i) {
        EXPECT_FLOAT_EQ(embedded.m[i][3], 0.0F);
        EXPECT_FLOAT_EQ(embedded.m[3][i], 0.0F);
    }
    EXPECT_FLOAT_EQ(embedded.m[3][3], 1.0F);
}

TEST(Matrix3x3Test, ToMatrixConversions_RoundTrip_PreservesUpper3x3) {
    Matrix3x3 matrix = Matrix3x3::Identity();
    matrix.m[0][2] = 5.0F;
    matrix.m[2][1] = 6.0F;

    EXPECT_EQ(ToMatrix3x3(ToMatrix4x4(matrix)), matrix);
}

TEST(Matrix3x3Test, NormalMatrixPath_MatchesLegacyFourByFourComputation) {
    // Pins the 1.1 migration's behavior-preservation promise: the 3x3 normal-matrix computation
    // (extract upper 3x3, invert, transpose) must agree with the legacy full-4x4 computation the
    // renderer used before, on a transform with translation, rotation, AND non-uniform scale.
    const Matrix4x4 world = Matrix4x4::ComposeTrs(
        Vector3{3.0F, -4.0F, 5.0F}, Quaternion{0.0F, 0.0F, std::numbers::sqrt2_v<float> / 2.0F,
                                                     std::numbers::sqrt2_v<float> / 2.0F},
        Vector3{2.0F, 3.0F, 4.0F});

    Matrix4x4 legacyInverse{};
    ASSERT_TRUE(TryInverse(world, legacyInverse));
    const Matrix4x4 legacyNormal = Transpose(legacyInverse);

    Matrix3x3 upperInverse{};
    ASSERT_TRUE(TryInverse(ToMatrix3x3(world), upperInverse));
    const Matrix3x3 upperNormal = Transpose(upperInverse);

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            EXPECT_NEAR(upperNormal.m[row][col], legacyNormal.m[row][col], kEpsilon);
        }
    }
}

TEST(Matrix3x3Test, ToString_FormatsAllThreeRows) {
    const Matrix3x3 identity = Matrix3x3::Identity();
    const std::string text = ToString(identity);

    EXPECT_NE(text.find("1.000000"), std::string::npos);
    EXPECT_NE(text.find("0.000000"), std::string::npos);
}

} // namespace
} // namespace UVE::Math::Tests
