// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/math/matrix3x3_uve.h"

#include <cmath>
#include <limits>
#include <numbers>
#include <string>

#include "uve/math/matrix4x4_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"

#include <gtest/gtest.h>

namespace UVE::Math::Tests {
namespace {

constexpr float kEpsilon = 1e-4F;

TEST(Matrix3x3UVETest, IdentityUVE_IsIdentityMatrix) {
    constexpr Matrix3x3UVE identity = Matrix3x3UVE::IdentityUVE();
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            EXPECT_EQ(identity.m[row][col], row == col ? 1.0F : 0.0F);
        }
    }
}

TEST(Matrix3x3UVETest, EqualityOperators_CompareAllComponents) {
    Matrix3x3UVE a = Matrix3x3UVE::IdentityUVE();
    a.m[1][2] = 5.0F;
    const Matrix3x3UVE b = a;
    Matrix3x3UVE c = a;
    c.m[1][2] = 6.0F;

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
    EXPECT_FALSE(a == c);
}

TEST(Matrix3x3UVETest, MatrixMultiply_WithIdentity_IsUnchanged) {
    Matrix3x3UVE matrix = Matrix3x3UVE::IdentityUVE();
    matrix.m[0][1] = 2.0F;
    matrix.m[1][0] = 3.0F;
    matrix.m[2][2] = 4.0F;

    EXPECT_EQ(matrix * Matrix3x3UVE::IdentityUVE(), matrix);
    EXPECT_EQ(Matrix3x3UVE::IdentityUVE() * matrix, matrix);
}

TEST(Matrix3x3UVETest, MatrixMultiply_KnownMatrices_MatchesHandComputedValue) {
    Matrix3x3UVE lhs = Matrix3x3UVE::IdentityUVE();
    lhs.m[0][1] = 2.0F;
    Matrix3x3UVE rhs = Matrix3x3UVE::IdentityUVE();
    rhs.m[1][0] = 3.0F;

    // [1 2 0]   [1 0 0]   [7 2 0]
    // [0 1 0] * [3 1 0] = [3 1 0]
    // [0 0 1]   [0 0 1]   [0 0 1]
    const Matrix3x3UVE product = lhs * rhs;

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

TEST(Matrix3x3UVETest, TransposeUVE_Identity_IsUnchanged) {
    constexpr Matrix3x3UVE identity = Matrix3x3UVE::IdentityUVE();

    EXPECT_EQ(TransposeUVE(identity), identity);
}

TEST(Matrix3x3UVETest, TransposeUVE_SwapsRowsAndColumns) {
    Matrix3x3UVE matrix = Matrix3x3UVE::IdentityUVE();
    matrix.m[0][2] = 5.0F;
    matrix.m[1][0] = 7.0F;

    const Matrix3x3UVE transposed = TransposeUVE(matrix);

    EXPECT_FLOAT_EQ(transposed.m[2][0], 5.0F);
    EXPECT_FLOAT_EQ(transposed.m[0][1], 7.0F);
    EXPECT_FLOAT_EQ(transposed.m[0][2], 0.0F);
    EXPECT_FLOAT_EQ(transposed.m[1][0], 0.0F);
}

TEST(Matrix3x3UVETest, TransposeUVE_AppliedTwice_RestoresOriginal) {
    Matrix3x3UVE matrix = Matrix3x3UVE::IdentityUVE();
    matrix.m[0][1] = 2.0F;
    matrix.m[1][2] = 3.0F;
    matrix.m[2][0] = 4.0F;

    const Matrix3x3UVE roundTripped = TransposeUVE(TransposeUVE(matrix));

    EXPECT_EQ(roundTripped, matrix);
}

TEST(Matrix3x3UVETest, TryInverseUVE_Identity_ReturnsIdentity) {
    constexpr Matrix3x3UVE identity = Matrix3x3UVE::IdentityUVE();
    Matrix3x3UVE inverse{};

    ASSERT_TRUE(TryInverseUVE(identity, inverse));
    EXPECT_EQ(inverse, identity);
}

TEST(Matrix3x3UVETest, TryInverseUVE_KnownMatrix_MatchesHandComputedValue) {
    // Textbook adjugate case (determinant exactly 1, so the inverse is exact integers).
    Matrix3x3UVE matrix{};
    matrix.m[0][0] = 1.0F;
    matrix.m[0][1] = 2.0F;
    matrix.m[0][2] = 3.0F;
    matrix.m[1][0] = 0.0F;
    matrix.m[1][1] = 1.0F;
    matrix.m[1][2] = 4.0F;
    matrix.m[2][0] = 5.0F;
    matrix.m[2][1] = 6.0F;
    matrix.m[2][2] = 0.0F;
    Matrix3x3UVE inverse{};

    ASSERT_TRUE(TryInverseUVE(matrix, inverse));

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

TEST(Matrix3x3UVETest, TryInverseUVE_NonUniformScaleTrs_MultipliesBackToIdentity) {
    const Matrix4x4UVE world = Matrix4x4UVE::ComposeTrsUVE(
        Vector3UVE{3.0F, -4.0F, 5.0F}, QuaternionUVE{0.0F, 0.0F, std::numbers::sqrt2_v<float> / 2.0F,
                                                     std::numbers::sqrt2_v<float> / 2.0F},
        Vector3UVE{2.0F, 3.0F, 4.0F});
    const Matrix3x3UVE matrix = ToMatrix3x3UVE(world);
    Matrix3x3UVE inverse{};

    ASSERT_TRUE(TryInverseUVE(matrix, inverse));
    const Matrix3x3UVE roundTripped = matrix * inverse;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            EXPECT_NEAR(roundTripped.m[row][col], Matrix3x3UVE::IdentityUVE().m[row][col], kEpsilon);
        }
    }
}

TEST(Matrix3x3UVETest, TryInverseUVE_SingularMatrix_ReturnsFalse) {
    Matrix3x3UVE zero{};
    zero.m[0][0] = 0.0F;
    zero.m[1][1] = 0.0F;
    zero.m[2][2] = 0.0F;
    Matrix3x3UVE duplicateRows = Matrix3x3UVE::IdentityUVE();
    duplicateRows.m[1][0] = duplicateRows.m[0][0];
    duplicateRows.m[1][1] = duplicateRows.m[0][1];
    duplicateRows.m[1][2] = duplicateRows.m[0][2];
    Matrix3x3UVE inverse{};

    EXPECT_FALSE(TryInverseUVE(zero, inverse));
    EXPECT_FALSE(TryInverseUVE(duplicateRows, inverse));
}

TEST(Matrix3x3UVETest, TryInverseUVE_NonFiniteMatrix_ReturnsFalse) {
    Matrix3x3UVE matrix = Matrix3x3UVE::IdentityUVE();
    matrix.m[1][1] = std::numeric_limits<float>::infinity();
    Matrix3x3UVE inverse{};

    EXPECT_FALSE(TryInverseUVE(matrix, inverse));
}

TEST(Matrix3x3UVETest, ToMatrix3x3UVE_TranslationOnlyTrs_IsIdentity) {
    const Matrix4x4UVE world = Matrix4x4UVE::ComposeTrsUVE(Vector3UVE{3.0F, -4.0F, 5.0F}, QuaternionUVE{},
                                                           Vector3UVE{1.0F, 1.0F, 1.0F});

    EXPECT_EQ(ToMatrix3x3UVE(world), Matrix3x3UVE::IdentityUVE());
}

TEST(Matrix3x3UVETest, ToMatrix3x3UVE_ScaleOnlyTrs_IsDiagonalScale) {
    const Matrix4x4UVE world = Matrix4x4UVE::ComposeTrsUVE(Vector3UVE{}, QuaternionUVE{},
                                                           Vector3UVE{2.0F, 3.0F, 4.0F});

    const Matrix3x3UVE upper = ToMatrix3x3UVE(world);

    EXPECT_FLOAT_EQ(upper.m[0][0], 2.0F);
    EXPECT_FLOAT_EQ(upper.m[1][1], 3.0F);
    EXPECT_FLOAT_EQ(upper.m[2][2], 4.0F);
    EXPECT_FLOAT_EQ(upper.m[0][1], 0.0F);
    EXPECT_FLOAT_EQ(upper.m[1][0], 0.0F);
}

TEST(Matrix3x3UVETest, ToMatrix4x4UVE_EmbedsWithIdentityBorder) {
    Matrix3x3UVE matrix = Matrix3x3UVE::IdentityUVE();
    matrix.m[0][1] = 2.0F;
    matrix.m[1][2] = 3.0F;
    matrix.m[2][0] = 4.0F;

    const Matrix4x4UVE embedded = ToMatrix4x4UVE(matrix);

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

TEST(Matrix3x3UVETest, ToMatrixConversions_RoundTrip_PreservesUpper3x3) {
    Matrix3x3UVE matrix = Matrix3x3UVE::IdentityUVE();
    matrix.m[0][2] = 5.0F;
    matrix.m[2][1] = 6.0F;

    EXPECT_EQ(ToMatrix3x3UVE(ToMatrix4x4UVE(matrix)), matrix);
}

TEST(Matrix3x3UVETest, NormalMatrixPath_MatchesLegacyFourByFourComputation) {
    // Pins the 1.1 migration's behavior-preservation promise: the 3x3 normal-matrix computation
    // (extract upper 3x3, invert, transpose) must agree with the legacy full-4x4 computation the
    // renderer used before, on a transform with translation, rotation, AND non-uniform scale.
    const Matrix4x4UVE world = Matrix4x4UVE::ComposeTrsUVE(
        Vector3UVE{3.0F, -4.0F, 5.0F}, QuaternionUVE{0.0F, 0.0F, std::numbers::sqrt2_v<float> / 2.0F,
                                                     std::numbers::sqrt2_v<float> / 2.0F},
        Vector3UVE{2.0F, 3.0F, 4.0F});

    Matrix4x4UVE legacyInverse{};
    ASSERT_TRUE(TryInverseUVE(world, legacyInverse));
    const Matrix4x4UVE legacyNormal = TransposeUVE(legacyInverse);

    Matrix3x3UVE upperInverse{};
    ASSERT_TRUE(TryInverseUVE(ToMatrix3x3UVE(world), upperInverse));
    const Matrix3x3UVE upperNormal = TransposeUVE(upperInverse);

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            EXPECT_NEAR(upperNormal.m[row][col], legacyNormal.m[row][col], kEpsilon);
        }
    }
}

TEST(Matrix3x3UVETest, ToStringUVE_FormatsAllThreeRows) {
    const Matrix3x3UVE identity = Matrix3x3UVE::IdentityUVE();
    const std::string text = ToStringUVE(identity);

    EXPECT_NE(text.find("1.000000"), std::string::npos);
    EXPECT_NE(text.find("0.000000"), std::string::npos);
}

} // namespace
} // namespace UVE::Math::Tests
