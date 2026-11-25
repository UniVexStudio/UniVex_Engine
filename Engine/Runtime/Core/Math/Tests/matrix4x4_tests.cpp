// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "matrix4x4.h"
#include "vector4.h"

#include <cmath>
#include <limits>
#include <numbers>
#include <string>

#include <gtest/gtest.h>

namespace UVE::Math::Tests {
namespace {

constexpr float kEpsilon = 1e-4F;

// Full 4-component multiply, used only for testing Perspective's clip-space output (unlike
// TransformPoint, this deliberately keeps `w` instead of assuming an affine w=1 matrix).
[[nodiscard]] Vector4 MultiplyHomogeneous(const Matrix4x4& matrix, Vector4 vector) {
    Vector4 result{};
    result.x = matrix.m[0][0] * vector.x + matrix.m[0][1] * vector.y + matrix.m[0][2] * vector.z +
               matrix.m[0][3] * vector.w;
    result.y = matrix.m[1][0] * vector.x + matrix.m[1][1] * vector.y + matrix.m[1][2] * vector.z +
               matrix.m[1][3] * vector.w;
    result.z = matrix.m[2][0] * vector.x + matrix.m[2][1] * vector.y + matrix.m[2][2] * vector.z +
               matrix.m[2][3] * vector.w;
    result.w = matrix.m[3][0] * vector.x + matrix.m[3][1] * vector.y + matrix.m[3][2] * vector.z +
               matrix.m[3][3] * vector.w;
    return result;
}

TEST(Matrix4x4Test, Identity_IsIdentityMatrix) {
    constexpr Matrix4x4 identity = Matrix4x4::Identity();
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            EXPECT_EQ(identity.m[row][col], row == col ? 1.0F : 0.0F);
        }
    }
}

TEST(Matrix4x4Test, ComposeTrs_TranslationOnly_TransformsOriginToTranslation) {
    const Matrix4x4 matrix = Matrix4x4::ComposeTrs(Vector3{5.0F, -2.0F, 3.0F}, Quaternion{},
                                                              Vector3{1.0F, 1.0F, 1.0F});

    const Vector3 transformed = TransformPoint(matrix, Vector3{0.0F, 0.0F, 0.0F});

    EXPECT_NEAR(transformed.x, 5.0F, kEpsilon);
    EXPECT_NEAR(transformed.y, -2.0F, kEpsilon);
    EXPECT_NEAR(transformed.z, 3.0F, kEpsilon);
}

TEST(Matrix4x4Test, ComposeTrs_ScaleOnly_ScalesPoint) {
    const Matrix4x4 matrix = Matrix4x4::ComposeTrs(Vector3{}, Quaternion{}, Vector3{2.0F, 3.0F, 4.0F});

    const Vector3 transformed = TransformPoint(matrix, Vector3{1.0F, 1.0F, 1.0F});

    EXPECT_NEAR(transformed.x, 2.0F, kEpsilon);
    EXPECT_NEAR(transformed.y, 3.0F, kEpsilon);
    EXPECT_NEAR(transformed.z, 4.0F, kEpsilon);
}

TEST(Matrix4x4Test, ComposeTrs_NinetyDegreesAboutZ_RotatesXAxisToYAxis) {
    const float halfNinety = std::sqrt(2.0F) / 2.0F;
    const Quaternion ninetyAboutZ{0.0F, 0.0F, halfNinety, halfNinety};
    const Matrix4x4 matrix = Matrix4x4::ComposeTrs(Vector3{}, ninetyAboutZ, Vector3{1.0F, 1.0F, 1.0F});

    const Vector3 transformed = TransformPoint(matrix, Vector3{1.0F, 0.0F, 0.0F});

    EXPECT_NEAR(transformed.x, 0.0F, kEpsilon);
    EXPECT_NEAR(transformed.y, 1.0F, kEpsilon);
    EXPECT_NEAR(transformed.z, 0.0F, kEpsilon);
}

TEST(Matrix4x4Test, Perspective_MapsNearPlaneToDepthZero) {
    const Matrix4x4 projection =
        Matrix4x4::Perspective(std::numbers::pi_v<float> / 2.0F, 1.0F, 1.0F, 100.0F);

    const Vector4 clip = MultiplyHomogeneous(projection, Vector4{0.0F, 0.0F, -1.0F, 1.0F});

    EXPECT_NEAR(clip.z / clip.w, 0.0F, kEpsilon);
}

TEST(Matrix4x4Test, Perspective_PreservesFiniteExtremeFarPlaneTranslation) {
    const float maximum = std::numeric_limits<float>::max();

    const Matrix4x4 projection = Matrix4x4::Perspective(
        std::numbers::pi_v<float> / 2.0F, 1.0F, 2.0F, maximum);

    EXPECT_TRUE(std::isfinite(projection.m[2][3]));
    EXPECT_FLOAT_EQ(projection.m[2][3], -2.0F);
}

TEST(Matrix4x4Test, Perspective_MapsFarPlaneToDepthOne) {
    const Matrix4x4 projection =
        Matrix4x4::Perspective(std::numbers::pi_v<float> / 2.0F, 1.0F, 1.0F, 100.0F);

    const Vector4 clip = MultiplyHomogeneous(projection, Vector4{0.0F, 0.0F, -100.0F, 1.0F});

    EXPECT_NEAR(clip.z / clip.w, 1.0F, kEpsilon);
}

TEST(Matrix4x4Test, Orthographic_PreservesFiniteExtremeExtentScale) {
    const float maximum = std::numeric_limits<float>::max();

    const Matrix4x4 projection = Matrix4x4::Orthographic(
        -maximum, maximum, -1.0F, 1.0F, 1.0F, 2.0F);
    const float expectedScale = static_cast<float>(2.0 / (2.0 * static_cast<double>(maximum)));

    EXPECT_TRUE(std::isfinite(projection.m[0][0]));
    EXPECT_GT(projection.m[0][0], 0.0F);
    EXPECT_FLOAT_EQ(projection.m[0][0], expectedScale);
}

TEST(Matrix4x4Test, Orthographic_PreservesFiniteExtremeExtentOffset) {
    const float maximum = std::numeric_limits<float>::max();
    const float left = maximum * 0.5F;

    const Matrix4x4 projection = Matrix4x4::Orthographic(
        left, maximum, -1.0F, 1.0F, 1.0F, 2.0F);

    EXPECT_TRUE(std::isfinite(projection.m[0][3]));
    EXPECT_FLOAT_EQ(projection.m[0][3], -3.0F);
}

TEST(Matrix4x4Test, Orthographic_PreservesFiniteExtremeVerticalExtentOffset) {
    const float maximum = std::numeric_limits<float>::max();
    const float bottom = maximum * 0.5F;

    const Matrix4x4 projection = Matrix4x4::Orthographic(
        -1.0F, 1.0F, bottom, maximum, 1.0F, 2.0F);

    EXPECT_TRUE(std::isfinite(projection.m[1][3]));
    EXPECT_FLOAT_EQ(projection.m[1][3], -3.0F);
}

TEST(Matrix4x4Test, Orthographic_PreservesFiniteExtremeVerticalExtentScale) {
    const float maximum = std::numeric_limits<float>::max();

    const Matrix4x4 projection = Matrix4x4::Orthographic(
        -1.0F, 1.0F, -maximum, maximum, 1.0F, 2.0F);
    const float expectedScale = static_cast<float>(2.0 / (2.0 * static_cast<double>(maximum)));

    EXPECT_TRUE(std::isfinite(projection.m[1][1]));
    EXPECT_GT(projection.m[1][1], 0.0F);
    EXPECT_FLOAT_EQ(projection.m[1][1], expectedScale);
}

TEST(Matrix4x4Test, Orthographic_MapsNearPlaneToDepthZero) {
    const Matrix4x4 projection = Matrix4x4::Orthographic(-10.0F, 10.0F, -10.0F, 10.0F, 1.0F, 100.0F);

    const Vector4 clip = MultiplyHomogeneous(projection, Vector4{0.0F, 0.0F, -1.0F, 1.0F});

    EXPECT_NEAR(clip.z / clip.w, 0.0F, kEpsilon);
}

TEST(Matrix4x4Test, Orthographic_MapsFarPlaneToDepthOne) {
    const Matrix4x4 projection = Matrix4x4::Orthographic(-10.0F, 10.0F, -10.0F, 10.0F, 1.0F, 100.0F);

    const Vector4 clip = MultiplyHomogeneous(projection, Vector4{0.0F, 0.0F, -100.0F, 1.0F});

    EXPECT_NEAR(clip.z / clip.w, 1.0F, kEpsilon);
}

TEST(Matrix4x4Test, Orthographic_MapsBoxCornersToPlusMinusOne) {
    const Matrix4x4 projection = Matrix4x4::Orthographic(-10.0F, 10.0F, -5.0F, 5.0F, 1.0F, 100.0F);

    const Vector4 minCorner = MultiplyHomogeneous(projection, Vector4{-10.0F, -5.0F, -50.0F, 1.0F});
    const Vector4 maxCorner = MultiplyHomogeneous(projection, Vector4{10.0F, 5.0F, -50.0F, 1.0F});

    EXPECT_NEAR(minCorner.x / minCorner.w, -1.0F, kEpsilon);
    EXPECT_NEAR(minCorner.y / minCorner.w, -1.0F, kEpsilon);
    EXPECT_NEAR(maxCorner.x / maxCorner.w, 1.0F, kEpsilon);
    EXPECT_NEAR(maxCorner.y / maxCorner.w, 1.0F, kEpsilon);
}

TEST(Matrix4x4Test, Orthographic_UnlikePerspective_DoesNotScaleWithDepth) {
    // Orthographic projection is depth-independent: a point directly above the origin maps to the
    // same NDC x/y regardless of how far along -Z it sits (no perspective divide changes the ratio).
    const Matrix4x4 projection = Matrix4x4::Orthographic(-10.0F, 10.0F, -10.0F, 10.0F, 1.0F, 100.0F);

    const Vector4 near = MultiplyHomogeneous(projection, Vector4{5.0F, 0.0F, -1.0F, 1.0F});
    const Vector4 far = MultiplyHomogeneous(projection, Vector4{5.0F, 0.0F, -99.0F, 1.0F});

    EXPECT_NEAR(near.x / near.w, far.x / far.w, kEpsilon);
    EXPECT_NEAR(near.w, 1.0F, kEpsilon);
    EXPECT_NEAR(far.w, 1.0F, kEpsilon);
}

TEST(Matrix4x4Test, ViewFromPositionAndRotation_IdentityRotation_TransformsWorldPointRelativeToEye) {
    const Matrix4x4 view = Matrix4x4::ViewFromPositionAndRotation(Vector3{0.0F, 0.0F, 5.0F}, Quaternion{});

    const Vector3 viewSpacePoint = TransformPoint(view, Vector3{0.0F, 0.0F, 0.0F});

    EXPECT_NEAR(viewSpacePoint.x, 0.0F, kEpsilon);
    EXPECT_NEAR(viewSpacePoint.y, 0.0F, kEpsilon);
    EXPECT_NEAR(viewSpacePoint.z, -5.0F, kEpsilon);
}

TEST(Matrix4x4Test, ViewFromPositionAndRotation_NinetyDegreeYaw_PointAheadOfNewForwardIsStraightAhead) {
    const float halfNinety = std::sqrt(2.0F) / 2.0F;
    const Quaternion ninetyAboutY{0.0F, halfNinety, 0.0F, halfNinety};
    const Matrix4x4 view = Matrix4x4::ViewFromPositionAndRotation(Vector3{0.0F, 0.0F, 0.0F}, ninetyAboutY);

    // After a 90-degree yaw, the camera's new world-space forward direction is (-1, 0, 0), so a
    // point 5 units along that direction should land straight ahead in view space: (0, 0, -5).
    const Vector3 viewSpacePoint = TransformPoint(view, Vector3{-5.0F, 0.0F, 0.0F});

    EXPECT_NEAR(viewSpacePoint.x, 0.0F, kEpsilon);
    EXPECT_NEAR(viewSpacePoint.y, 0.0F, kEpsilon);
    EXPECT_NEAR(viewSpacePoint.z, -5.0F, kEpsilon);
}

TEST(Matrix4x4Test, ViewFromPositionAndRotation_PreservesFiniteExtremePositionCancellation) {
    const float oneOverRootThree = 1.0F / std::sqrt(3.0F);
    const float normalization = std::sqrt(2.0F * (1.0F + oneOverRootThree));
    const Quaternion rotation{
        0.0F,
        -oneOverRootThree / normalization,
        oneOverRootThree / normalization,
        (1.0F + oneOverRootThree) / normalization,
    };
    const float maximum = std::numeric_limits<float>::max();

    const Matrix4x4 view = Matrix4x4::ViewFromPositionAndRotation(
        Vector3{maximum, maximum, -maximum}, rotation);

    EXPECT_TRUE(std::isfinite(view.m[0][3]));
    EXPECT_LT(std::fabs(view.m[0][3]), maximum);
    EXPECT_LT(view.m[0][3], 0.0F);
}

TEST(Matrix4x4Test, MatrixMultiply_WithIdentity_IsUnchanged) {
    const Matrix4x4 matrix = Matrix4x4::ComposeTrs(Vector3{1.0F, 2.0F, 3.0F}, Quaternion{},
                                                              Vector3{1.0F, 1.0F, 1.0F});
    constexpr Matrix4x4 identity = Matrix4x4::Identity();

    EXPECT_EQ(matrix * identity, matrix);
    EXPECT_EQ(identity * matrix, matrix);
}

TEST(Matrix4x4Test, MatrixMultiply_ComposesInRightToLeftOrder) {
    const Matrix4x4 translateX = Matrix4x4::ComposeTrs(Vector3{10.0F, 0.0F, 0.0F}, Quaternion{},
                                                                  Vector3{1.0F, 1.0F, 1.0F});
    const Matrix4x4 translateY = Matrix4x4::ComposeTrs(Vector3{0.0F, 5.0F, 0.0F}, Quaternion{},
                                                                  Vector3{1.0F, 1.0F, 1.0F});

    const Matrix4x4 combined = translateX * translateY;
    const Vector3 transformed = TransformPoint(combined, Vector3{0.0F, 0.0F, 0.0F});

    EXPECT_NEAR(transformed.x, 10.0F, kEpsilon);
    EXPECT_NEAR(transformed.y, 5.0F, kEpsilon);
    EXPECT_NEAR(transformed.z, 0.0F, kEpsilon);
}

TEST(Matrix4x4Test, MatrixMultiply_PreservesFiniteCancellationAtFloatBoundary) {
    constexpr float maximum = std::numeric_limits<float>::max();
    Matrix4x4 lhs{};
    lhs.m[0][0] = 1.0F;
    lhs.m[0][1] = 1.0F;
    lhs.m[0][2] = -1.0F;
    lhs.m[0][3] = 0.0F;

    Matrix4x4 rhs{};
    rhs.m[0][0] = maximum;
    rhs.m[1][0] = maximum;
    rhs.m[2][0] = maximum;
    rhs.m[3][0] = 0.0F;

    const Matrix4x4 product = lhs * rhs;

    EXPECT_TRUE(std::isfinite(product.m[0][0]));
    EXPECT_FLOAT_EQ(product.m[0][0], maximum);
}

TEST(Matrix4x4Test, EqualityOperators_CompareAllSixteenComponents) {
    constexpr Matrix4x4 a = Matrix4x4::Identity();
    constexpr Matrix4x4 b = Matrix4x4::Identity();
    Matrix4x4 c = Matrix4x4::Identity();
    c.m[0][3] = 1.0F;

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
    EXPECT_FALSE(a == c);
}

TEST(Matrix4x4Test, ToString_FormatsAllFourRows) {
    constexpr Matrix4x4 identity = Matrix4x4::Identity();
    const std::string text = ToString(identity);

    EXPECT_NE(text.find("1.000000"), std::string::npos);
    EXPECT_NE(text.find(";"), std::string::npos);
}

TEST(Matrix4x4Test, Transpose_Identity_IsUnchanged) {
    constexpr Matrix4x4 identity = Matrix4x4::Identity();

    EXPECT_EQ(Transpose(identity), identity);
}

TEST(Matrix4x4Test, Transpose_SwapsRowsAndColumns) {
    Matrix4x4 matrix = Matrix4x4::Identity();
    matrix.m[0][3] = 5.0F;
    matrix.m[1][2] = 7.0F;

    const Matrix4x4 transposed = Transpose(matrix);

    EXPECT_FLOAT_EQ(transposed.m[3][0], 5.0F);
    EXPECT_FLOAT_EQ(transposed.m[2][1], 7.0F);
    EXPECT_FLOAT_EQ(transposed.m[0][3], 0.0F);
    EXPECT_FLOAT_EQ(transposed.m[1][2], 0.0F);
}

TEST(Matrix4x4Test, Transpose_AppliedTwice_RestoresOriginal) {
    const Matrix4x4 matrix = Matrix4x4::ComposeTrs(
        Vector3{1.0F, -2.0F, 3.5F}, Quaternion{}, Vector3{2.0F, 3.0F, 4.0F});

    const Matrix4x4 roundTripped = Transpose(Transpose(matrix));

    EXPECT_EQ(roundTripped, matrix);
}

TEST(Matrix4x4Test, TryInverse_Identity_ReturnsIdentity) {
    constexpr Matrix4x4 identity = Matrix4x4::Identity();
    Matrix4x4 inverse{};

    ASSERT_TRUE(TryInverse(identity, inverse));
    EXPECT_EQ(inverse, identity);
}

TEST(Matrix4x4Test, TryInverse_UniformScaleTrs_MultipliesBackToIdentity) {
    const Matrix4x4 matrix = Matrix4x4::ComposeTrs(
        Vector3{3.0F, -4.0F, 5.0F}, Quaternion{0.0F, 0.0F, std::numbers::sqrt2_v<float> / 2.0F,
                                                        std::numbers::sqrt2_v<float> / 2.0F},
        Vector3{2.0F, 2.0F, 2.0F});
    Matrix4x4 inverse{};

    ASSERT_TRUE(TryInverse(matrix, inverse));
    const Matrix4x4 roundTripped = matrix * inverse;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            EXPECT_NEAR(roundTripped.m[row][col], Matrix4x4::Identity().m[row][col], kEpsilon);
        }
    }
}

TEST(Matrix4x4Test, TryInverse_NonUniformScaleTrs_MultipliesBackToIdentity) {
    const Matrix4x4 matrix = Matrix4x4::ComposeTrs(
        Vector3{-1.0F, 2.0F, -3.0F}, Quaternion{}, Vector3{2.0F, 0.5F, 4.0F});
    Matrix4x4 inverse{};

    ASSERT_TRUE(TryInverse(matrix, inverse));
    const Matrix4x4 roundTripped = matrix * inverse;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            EXPECT_NEAR(roundTripped.m[row][col], Matrix4x4::Identity().m[row][col], kEpsilon);
        }
    }
}

TEST(Matrix4x4Test, TryInverse_SingularMatrix_ReturnsFalse) {
    Matrix4x4 singular{};
    singular.m[0][0] = 0.0F;
    singular.m[1][1] = 0.0F;
    singular.m[2][2] = 0.0F;
    singular.m[3][3] = 0.0F;
    Matrix4x4 inverse{};

    EXPECT_FALSE(TryInverse(singular, inverse));
}

TEST(Matrix4x4Test, TryInverse_NonFiniteMatrix_ReturnsFalse) {
    Matrix4x4 matrix = Matrix4x4::Identity();
    matrix.m[1][2] = std::numeric_limits<float>::infinity();
    Matrix4x4 inverse{};

    EXPECT_FALSE(TryInverse(matrix, inverse));
}

TEST(Matrix4x4Test, TryInverse_NonUniformScale_NormalMatrixTransformsNormalPerpendicularToSurface) {
    // Regression for the normal-matrix bug: a plane through the origin with normal +Y, scaled 4x
    // along X. The tangent-plane vectors (+X, +Z) get carried by the model matrix directly; the
    // transformed normal must stay perpendicular to both, which mat3(model) alone would not
    // guarantee for a non-uniform scale in the direction perpendicular to the normal it's applied to.
    const Matrix4x4 model = Matrix4x4::ComposeTrs(Vector3{0.0F, 0.0F, 0.0F}, Quaternion{},
                                                             Vector3{4.0F, 1.0F, 1.0F});
    Matrix4x4 inverse{};
    ASSERT_TRUE(TryInverse(model, inverse));
    const Matrix4x4 normalMatrix = Transpose(inverse);

    const Vector3 tangentX = TransformPoint(model, Vector3{1.0F, 0.0F, 0.0F}) -
                                TransformPoint(model, Vector3{0.0F, 0.0F, 0.0F});
    const Vector3 tangentZ = TransformPoint(model, Vector3{0.0F, 0.0F, 1.0F}) -
                                TransformPoint(model, Vector3{0.0F, 0.0F, 0.0F});
    const Vector3 transformedNormal = TransformPoint(normalMatrix, Vector3{0.0F, 1.0F, 0.0F}) -
                                         TransformPoint(normalMatrix, Vector3{0.0F, 0.0F, 0.0F});

    EXPECT_NEAR(Dot(transformedNormal, tangentX), 0.0F, kEpsilon);
    EXPECT_NEAR(Dot(transformedNormal, tangentZ), 0.0F, kEpsilon);
}

} // namespace
} // namespace UVE::Math::Tests
