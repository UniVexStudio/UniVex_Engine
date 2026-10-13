// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/math/rect_uve.h"

#include <limits>
#include <string>

#include "uve/math/vector2_uve.h"

#include <gtest/gtest.h>

namespace UVE::Math::Tests {
namespace {

TEST(RectUVETest, DefaultConstruction_IsEmptyAtOrigin) {
    constexpr RectUVE rect{};
    EXPECT_EQ(rect.position, (Vector2UVE{0.0F, 0.0F}));
    EXPECT_EQ(rect.size, (Vector2UVE{0.0F, 0.0F}));
}

TEST(RectUVETest, EqualityOperators_ComparePositionAndSize) {
    constexpr RectUVE a{Vector2UVE{1.0F, 2.0F}, Vector2UVE{3.0F, 4.0F}};
    constexpr RectUVE b{Vector2UVE{1.0F, 2.0F}, Vector2UVE{3.0F, 4.0F}};
    constexpr RectUVE c{Vector2UVE{1.0F, 2.0F}, Vector2UVE{3.0F, 5.0F}};
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_FALSE(a == c);
    EXPECT_TRUE(a != c);
}

TEST(RectUVETest, ContainsPoint_EdgesAreIncluded) {
    // UIRuntimeUVE's hover contract: edge pixels count, so every comparison is inclusive.
    constexpr RectUVE rect{Vector2UVE{10.0F, 20.0F}, Vector2UVE{30.0F, 40.0F}};
    EXPECT_TRUE(ContainsUVE(rect, Vector2UVE{10.0F, 20.0F}));
    EXPECT_TRUE(ContainsUVE(rect, Vector2UVE{40.0F, 60.0F}));
    EXPECT_TRUE(ContainsUVE(rect, Vector2UVE{25.0F, 60.0F}));
    EXPECT_TRUE(ContainsUVE(rect, Vector2UVE{10.0F, 45.0F}));
    EXPECT_FALSE(ContainsUVE(rect, Vector2UVE{9.999F, 45.0F}));
    EXPECT_FALSE(ContainsUVE(rect, Vector2UVE{40.001F, 45.0F}));
    EXPECT_FALSE(ContainsUVE(rect, Vector2UVE{25.0F, 60.001F}));
}

TEST(RectUVETest, ContainsRect_InnerMustFitWholly) {
    constexpr RectUVE outer{Vector2UVE{0.0F, 0.0F}, Vector2UVE{100.0F, 100.0F}};
    EXPECT_TRUE(ContainsUVE(outer, RectUVE{Vector2UVE{10.0F, 10.0F}, Vector2UVE{20.0F, 20.0F}}));
    EXPECT_TRUE(ContainsUVE(outer, outer));
    EXPECT_FALSE(ContainsUVE(outer, RectUVE{Vector2UVE{90.0F, 90.0F}, Vector2UVE{20.0F, 20.0F}}));
    EXPECT_FALSE(ContainsUVE(outer, RectUVE{Vector2UVE{-1.0F, 0.0F}, Vector2UVE{10.0F, 10.0F}}));
}

TEST(RectUVETest, Intersects_EdgeTouchingIsNotAnOverlap) {
    constexpr RectUVE a{Vector2UVE{0.0F, 0.0F}, Vector2UVE{10.0F, 10.0F}};
    EXPECT_TRUE(IntersectsUVE(a, RectUVE{Vector2UVE{5.0F, 5.0F}, Vector2UVE{10.0F, 10.0F}}));
    EXPECT_FALSE(IntersectsUVE(a, RectUVE{Vector2UVE{10.0F, 0.0F}, Vector2UVE{10.0F, 10.0F}}));
    EXPECT_FALSE(IntersectsUVE(a, RectUVE{Vector2UVE{20.0F, 20.0F}, Vector2UVE{5.0F, 5.0F}}));
}

TEST(RectUVETest, Intersection_ComputesOverlapAndEmptyForDisjoint) {
    constexpr RectUVE overlap =
        IntersectionUVE(RectUVE{Vector2UVE{0.0F, 0.0F}, Vector2UVE{10.0F, 10.0F}},
                        RectUVE{Vector2UVE{5.0F, 5.0F}, Vector2UVE{10.0F, 10.0F}});
    EXPECT_EQ(overlap, (RectUVE{Vector2UVE{5.0F, 5.0F}, Vector2UVE{5.0F, 5.0F}}));
    const RectUVE empty =
        IntersectionUVE(RectUVE{Vector2UVE{0.0F, 0.0F}, Vector2UVE{10.0F, 10.0F}},
                        RectUVE{Vector2UVE{20.0F, 20.0F}, Vector2UVE{5.0F, 5.0F}});
    EXPECT_EQ(empty.size, (Vector2UVE{0.0F, 0.0F}));
}

TEST(RectUVETest, Union_SpansBothRects) {
    constexpr RectUVE united = UnionUVE(RectUVE{Vector2UVE{0.0F, 0.0F}, Vector2UVE{10.0F, 10.0F}},
                                        RectUVE{Vector2UVE{5.0F, 5.0F}, Vector2UVE{10.0F, 10.0F}});
    EXPECT_EQ(united, (RectUVE{Vector2UVE{0.0F, 0.0F}, Vector2UVE{15.0F, 15.0F}}));
}

TEST(RectUVETest, Transform_ScalesExtentAndTranslatesPosition) {
    // UIRuntimeUVE's authored-to-presentation transform as one operation: position scales then
    // offsets, the extent scales but never translates.
    constexpr RectUVE rect{Vector2UVE{10.0F, 20.0F}, Vector2UVE{30.0F, 40.0F}};
    constexpr RectUVE transformed =
        TransformUVE(rect, Vector2UVE{2.0F, 2.0F}, Vector2UVE{10.0F, 20.0F});
    EXPECT_EQ(transformed.position, (Vector2UVE{30.0F, 60.0F}));
    EXPECT_EQ(transformed.size, (Vector2UVE{60.0F, 80.0F}));
}

TEST(RectUVETest, Max_IsPositionPlusSize) {
    constexpr RectUVE rect{Vector2UVE{10.0F, 20.0F}, Vector2UVE{30.0F, 40.0F}};
    EXPECT_EQ(MaxUVE(rect), (Vector2UVE{40.0F, 60.0F}));
}

TEST(RectUVETest, IsFinite_RejectsNonFiniteComponents) {
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    constexpr float kInf = std::numeric_limits<float>::infinity();
    EXPECT_TRUE(IsFiniteUVE(RectUVE{Vector2UVE{1.0F, 2.0F}, Vector2UVE{3.0F, 4.0F}}));
    EXPECT_FALSE(IsFiniteUVE(RectUVE{Vector2UVE{kNaN, 2.0F}, Vector2UVE{3.0F, 4.0F}}));
    EXPECT_FALSE(IsFiniteUVE(RectUVE{Vector2UVE{1.0F, 2.0F}, Vector2UVE{3.0F, kInf}}));
}

TEST(RectUVETest, ToString_FormatsPositionAndSize) {
    EXPECT_EQ(ToStringUVE(RectUVE{Vector2UVE{1.0F, 2.0F}, Vector2UVE{3.0F, 4.0F}}),
              "((1.000000, 2.000000), (3.000000, 4.000000))");
}

} // namespace
} // namespace UVE::Math::Tests
