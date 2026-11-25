// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "vector2i.h"

#include <cstdint>
#include <string>

#include "vector2.h"

#include <gtest/gtest.h>

namespace UVE::Math::Tests {
namespace {

TEST(Vector2iTest, DefaultConstruction_IsZero) {
    constexpr Vector2i v{};
    EXPECT_EQ(v.x, 0);
    EXPECT_EQ(v.y, 0);
}

TEST(Vector2iTest, Arithmetic_AddSubNegateScale) {
    constexpr Vector2i a{10, -3};
    constexpr Vector2i b{4, 7};
    EXPECT_EQ(a + b, (Vector2i{14, 4}));
    EXPECT_EQ(a - b, (Vector2i{6, -10}));
    EXPECT_EQ(-a, (Vector2i{-10, 3}));
    EXPECT_EQ(a * 3, (Vector2i{30, -9}));
}

TEST(Vector2iTest, EqualityOperators_CompareBothComponents) {
    constexpr Vector2i a{1, 2};
    constexpr Vector2i b{1, 2};
    constexpr Vector2i c{1, 3};
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_FALSE(a == c);
    EXPECT_TRUE(a != c);
}

TEST(Vector2iTest, Dot_WidensToInt64WithoutOverflow) {
    // 50000^2 * 2 = 5e9: exact in int64, overflowed in int32.
    constexpr Vector2i v{50000, 50000};
    EXPECT_EQ(Dot(v, v), 5000000000LL);
    constexpr Vector2i mixed{-50000, 20000};
    EXPECT_EQ(Dot(v, mixed), -1500000000LL);
    EXPECT_EQ(LengthSquared(v), 5000000000LL);
}

TEST(Vector2iTest, Length_ThreeFourFive) {
    EXPECT_FLOAT_EQ(Length(Vector2i{3, 4}), 5.0F);
    EXPECT_FLOAT_EQ(Length(Vector2i{0, 0}), 0.0F);
}

TEST(Vector2iTest, MinMaxClamp_AreComponentWise) {
    constexpr Vector2i a{1, 9};
    constexpr Vector2i b{5, 3};
    EXPECT_EQ(Min(a, b), (Vector2i{1, 3}));
    EXPECT_EQ(Max(a, b), (Vector2i{5, 9}));
    EXPECT_EQ(Clamp(Vector2i{-4, 99}, Vector2i{0, 0}, Vector2i{10, 10}),
              (Vector2i{0, 10}));
}

TEST(Vector2iTest, ToVector2_ConvertsComponents) {
    constexpr Vector2i v{-7, 42};
    const Vector2 f = ToVector2(v);
    EXPECT_FLOAT_EQ(f.x, -7.0F);
    EXPECT_FLOAT_EQ(f.y, 42.0F);
}

TEST(Vector2iTest, ToString_FormatsBothComponents) {
    EXPECT_EQ(ToString(Vector2i{-7, 42}), "(-7, 42)");
}

} // namespace
} // namespace UVE::Math::Tests
