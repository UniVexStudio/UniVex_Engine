// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/math/vector3i_uve.h"

#include <cstdint>
#include <string>

#include "uve/math/vector3_uve.h"

#include <gtest/gtest.h>

namespace UVE::Math::Tests {
namespace {

TEST(Vector3iUVETest, DefaultConstruction_IsZero) {
    constexpr Vector3iUVE v{};
    EXPECT_EQ(v.x, 0);
    EXPECT_EQ(v.y, 0);
    EXPECT_EQ(v.z, 0);
}

TEST(Vector3iUVETest, Arithmetic_AddSubNegateScale) {
    constexpr Vector3iUVE a{10, -3, 5};
    constexpr Vector3iUVE b{4, 7, -2};
    EXPECT_EQ(a + b, (Vector3iUVE{14, 4, 3}));
    EXPECT_EQ(a - b, (Vector3iUVE{6, -10, 7}));
    EXPECT_EQ(-a, (Vector3iUVE{-10, 3, -5}));
    EXPECT_EQ(a * 3, (Vector3iUVE{30, -9, 15}));
}

TEST(Vector3iUVETest, EqualityOperators_CompareAllComponents) {
    constexpr Vector3iUVE a{1, 2, 3};
    constexpr Vector3iUVE b{1, 2, 3};
    constexpr Vector3iUVE c{1, 2, 4};
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_FALSE(a == c);
    EXPECT_TRUE(a != c);
}

TEST(Vector3iUVETest, Dot_WidensToInt64WithoutOverflow) {
    // 3 * 50000^2 = 7.5e9: exact in int64, overflowed in int32.
    constexpr Vector3iUVE v{50000, 50000, 50000};
    EXPECT_EQ(DotUVE(v, v), 7500000000LL);
    EXPECT_EQ(LengthSquaredUVE(v), 7500000000LL);
}

TEST(Vector3iUVETest, Length_OneTwoTwoIsThree) {
    EXPECT_FLOAT_EQ(LengthUVE(Vector3iUVE{1, 2, 2}), 3.0F);
}

TEST(Vector3iUVETest, MinMaxClamp_AreComponentWise) {
    constexpr Vector3iUVE a{1, 9, -5};
    constexpr Vector3iUVE b{5, 3, 0};
    EXPECT_EQ(MinUVE(a, b), (Vector3iUVE{1, 3, -5}));
    EXPECT_EQ(MaxUVE(a, b), (Vector3iUVE{5, 9, 0}));
    EXPECT_EQ(ClampUVE(Vector3iUVE{-4, 99, 7}, Vector3iUVE{0, 0, 0}, Vector3iUVE{10, 10, 10}),
              (Vector3iUVE{0, 10, 7}));
}

TEST(Vector3iUVETest, ToVector3_ConvertsComponents) {
    constexpr Vector3iUVE v{-7, 42, 0};
    const Vector3UVE f = ToVector3UVE(v);
    EXPECT_FLOAT_EQ(f.x, -7.0F);
    EXPECT_FLOAT_EQ(f.y, 42.0F);
    EXPECT_FLOAT_EQ(f.z, 0.0F);
}

TEST(Vector3iUVETest, ToString_FormatsAllComponents) {
    EXPECT_EQ(ToStringUVE(Vector3iUVE{-7, 42, 0}), "(-7, 42, 0)");
}

} // namespace
} // namespace UVE::Math::Tests
