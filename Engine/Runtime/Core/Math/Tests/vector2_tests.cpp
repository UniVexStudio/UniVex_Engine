// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "vector2.h"

#include <limits>
#include <string>

#include <gtest/gtest.h>

namespace UVE::Math::Tests {
namespace {

TEST(Vector2Test, DefaultConstruction_IsZero) {
    constexpr Vector2 vector{};
    EXPECT_EQ(vector.x, 0.0F);
    EXPECT_EQ(vector.y, 0.0F);
}

TEST(Vector2Test, Addition_SumsEachComponent) {
    constexpr Vector2 lhs{1.0F, 2.0F};
    constexpr Vector2 rhs{10.0F, 20.0F};
    constexpr Vector2 sum = lhs + rhs;

    EXPECT_EQ(sum.x, 11.0F);
    EXPECT_EQ(sum.y, 22.0F);
}

TEST(Vector2Test, Subtraction_DiffsEachComponent) {
    constexpr Vector2 lhs{10.0F, 20.0F};
    constexpr Vector2 rhs{1.0F, 2.0F};
    constexpr Vector2 diff = lhs - rhs;

    EXPECT_EQ(diff.x, 9.0F);
    EXPECT_EQ(diff.y, 18.0F);
}

TEST(Vector2Test, EqualityOperators_CompareBothComponents) {
    constexpr Vector2 a{1.0F, 2.0F};
    constexpr Vector2 b{1.0F, 2.0F};
    constexpr Vector2 c{1.0F, 3.0F};

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_TRUE(a != c);
    EXPECT_FALSE(a != b);
}

TEST(Vector2Test, ToString_ContainsBothComponents) {
    const Vector2 vector{1.0F, 2.0F};
    const std::string text = ToString(vector);

    EXPECT_NE(text.find("1.000000"), std::string::npos);
    EXPECT_NE(text.find("2.000000"), std::string::npos);
}

TEST(Vector2Test, Dot_SumsComponentProducts) {
    constexpr Vector2 lhs{1.0F, 2.0F};
    constexpr Vector2 rhs{10.0F, 20.0F};
    EXPECT_FLOAT_EQ(Dot(lhs, rhs), 50.0F);
    EXPECT_FLOAT_EQ(Dot(lhs, Vector2{-2.0F, 1.0F}), 0.0F);
}

TEST(Vector2Test, LengthSquared_AgreesWithDot) {
    constexpr Vector2 vector{3.0F, 4.0F};
    EXPECT_FLOAT_EQ(LengthSquared(vector), 25.0F);
    EXPECT_FLOAT_EQ(LengthSquared(vector), Dot(vector, vector));
}

TEST(Vector2Test, Length_MeasuresEuclideanDistance) {
    EXPECT_FLOAT_EQ(Length(Vector2{3.0F, 4.0F}), 5.0F);
    EXPECT_FLOAT_EQ(Length(Vector2{}), 0.0F);
}

TEST(Vector2Test, Normalize_ScalesToUnitLength) {
    const Vector2 unit = Normalize(Vector2{3.0F, 4.0F});
    EXPECT_FLOAT_EQ(Length(unit), 1.0F);
    EXPECT_FLOAT_EQ(unit.x, 0.6F);
    EXPECT_FLOAT_EQ(unit.y, 0.8F);
}

TEST(Vector2Test, IsFinite_RejectsNonFiniteComponents) {
    EXPECT_TRUE(IsFinite(Vector2{1.0F, 2.0F}));
    EXPECT_FALSE(IsFinite(Vector2{std::numeric_limits<float>::infinity(), 0.0F}));
    EXPECT_FALSE(IsFinite(Vector2{0.0F, std::numeric_limits<float>::quiet_NaN()}));
}

} // namespace
} // namespace UVE::Math::Tests
