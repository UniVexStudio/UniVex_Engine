// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/math/rect_int_uve.h"

#include <cstdint>
#include <limits>
#include <string>

#include "uve/math/rect_uve.h"
#include "uve/math/vector2i_uve.h"

#include <gtest/gtest.h>

namespace UVE::Math::Tests {
namespace {

TEST(RectIntUVETest, DefaultConstruction_IsEmptyAtOrigin) {
    constexpr RectIntUVE rect{};
    EXPECT_EQ(rect.position, (Vector2iUVE{0, 0}));
    EXPECT_EQ(rect.size, (Vector2iUVE{0, 0}));
}

TEST(RectIntUVETest, EqualityOperators_ComparePositionAndSize) {
    constexpr RectIntUVE a{Vector2iUVE{1, 2}, Vector2iUVE{3, 4}};
    constexpr RectIntUVE b{Vector2iUVE{1, 2}, Vector2iUVE{3, 4}};
    constexpr RectIntUVE c{Vector2iUVE{1, 2}, Vector2iUVE{3, 5}};
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_FALSE(a == c);
    EXPECT_TRUE(a != c);
}

TEST(RectIntUVETest, ContainsPoint_EdgesAreIncluded) {
    constexpr RectIntUVE rect{Vector2iUVE{10, 20}, Vector2iUVE{30, 40}};
    EXPECT_TRUE(ContainsUVE(rect, Vector2iUVE{10, 20}));
    EXPECT_TRUE(ContainsUVE(rect, Vector2iUVE{40, 60}));
    EXPECT_TRUE(ContainsUVE(rect, Vector2iUVE{25, 60}));
    EXPECT_FALSE(ContainsUVE(rect, Vector2iUVE{9, 45}));
    EXPECT_FALSE(ContainsUVE(rect, Vector2iUVE{41, 45}));
}

TEST(RectIntUVETest, ContainsRect_ServesTheViewportFitCheck) {
    // The render-pass viewport override must fit its target: edges included, negatives rejected.
    constexpr RectIntUVE target{Vector2iUVE{0, 0}, Vector2iUVE{64, 64}};
    EXPECT_TRUE(ContainsUVE(target, RectIntUVE{Vector2iUVE{0, 0}, Vector2iUVE{32, 64}}));
    EXPECT_TRUE(ContainsUVE(target, RectIntUVE{Vector2iUVE{32, 0}, Vector2iUVE{32, 64}}));
    EXPECT_TRUE(ContainsUVE(target, target));
    EXPECT_FALSE(ContainsUVE(target, RectIntUVE{Vector2iUVE{33, 0}, Vector2iUVE{32, 64}}));
    EXPECT_FALSE(ContainsUVE(target, RectIntUVE{Vector2iUVE{-1, 0}, Vector2iUVE{32, 64}}));
}

TEST(RectIntUVETest, ContainsPoint_NearInt32MaxDoesNotOverflow) {
    // position.x + size.x overflows int32 here; the widened comparison must still hold.
    constexpr std::int32_t kNearMax = std::numeric_limits<std::int32_t>::max() - 10;
    constexpr RectIntUVE rect{Vector2iUVE{kNearMax, 0}, Vector2iUVE{20, 10}};
    EXPECT_TRUE(ContainsUVE(rect, Vector2iUVE{std::numeric_limits<std::int32_t>::max() - 1, 5}));
    EXPECT_FALSE(ContainsUVE(rect, Vector2iUVE{kNearMax - 1, 5}));
}

TEST(RectIntUVETest, Intersects_EdgeTouchingIsNotAnOverlap) {
    constexpr RectIntUVE a{Vector2iUVE{0, 0}, Vector2iUVE{10, 10}};
    EXPECT_TRUE(IntersectsUVE(a, RectIntUVE{Vector2iUVE{5, 5}, Vector2iUVE{10, 10}}));
    EXPECT_FALSE(IntersectsUVE(a, RectIntUVE{Vector2iUVE{10, 0}, Vector2iUVE{10, 10}}));
    EXPECT_FALSE(IntersectsUVE(a, RectIntUVE{Vector2iUVE{20, 20}, Vector2iUVE{5, 5}}));
}

TEST(RectIntUVETest, Intersection_ComputesOverlapAndEmptyForDisjoint) {
    constexpr RectIntUVE overlap =
        IntersectionUVE(RectIntUVE{Vector2iUVE{0, 0}, Vector2iUVE{10, 10}},
                        RectIntUVE{Vector2iUVE{5, 5}, Vector2iUVE{10, 10}});
    EXPECT_EQ(overlap, (RectIntUVE{Vector2iUVE{5, 5}, Vector2iUVE{5, 5}}));
    constexpr RectIntUVE empty =
        IntersectionUVE(RectIntUVE{Vector2iUVE{0, 0}, Vector2iUVE{10, 10}},
                        RectIntUVE{Vector2iUVE{20, 20}, Vector2iUVE{5, 5}});
    EXPECT_EQ(empty.size, (Vector2iUVE{0, 0}));
}

TEST(RectIntUVETest, Union_SpansBothRects) {
    constexpr RectIntUVE united =
        UnionUVE(RectIntUVE{Vector2iUVE{0, 0}, Vector2iUVE{10, 10}},
                 RectIntUVE{Vector2iUVE{5, 5}, Vector2iUVE{10, 10}});
    EXPECT_EQ(united, (RectIntUVE{Vector2iUVE{0, 0}, Vector2iUVE{15, 15}}));
}

TEST(RectIntUVETest, ToRect_ConvertsPositionAndSize) {
    constexpr RectIntUVE rect{Vector2iUVE{-7, 42}, Vector2iUVE{64, 32}};
    const RectUVE f = ToRectUVE(rect);
    EXPECT_EQ(f.position, (Vector2UVE{-7.0F, 42.0F}));
    EXPECT_EQ(f.size, (Vector2UVE{64.0F, 32.0F}));
}

TEST(RectIntUVETest, ToString_FormatsPositionAndSize) {
    EXPECT_EQ(ToStringUVE(RectIntUVE{Vector2iUVE{1, 2}, Vector2iUVE{3, 4}}), "((1, 2), (3, 4))");
}

} // namespace
} // namespace UVE::Math::Tests
