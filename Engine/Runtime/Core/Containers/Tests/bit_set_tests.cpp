// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include "bit_set.h"

namespace UVE::Containers::Tests {
namespace {

TEST(BitSetTest, SetTestResetFlip_RoundTrip) {
    BitSet<130U> bits;
    EXPECT_TRUE(bits.None());
    bits.Set(0U);
    bits.Set(63U);
    bits.Set(64U);
    bits.Set(129U);
    EXPECT_TRUE(bits.Test(0U));
    EXPECT_TRUE(bits.Test(129U));
    EXPECT_FALSE(bits.Test(1U));
    EXPECT_EQ(bits.Count(), 4U);
    bits.Reset(63U);
    EXPECT_FALSE(bits.Test(63U));
    bits.Flip(63U);
    EXPECT_TRUE(bits.Test(63U));
    bits.Set(1U, true);
    bits.Set(0U, false);
    EXPECT_TRUE(bits.Test(1U));
    EXPECT_FALSE(bits.Test(0U));
}

TEST(BitSetTest, SpareBits_NeverLeakIntoEqualityOrAll) {
    // 130 bits need 3 words (192); the 62 spare bits must stay zero through every op.
    BitSet<130U> bits;
    bits.SetAll();
    EXPECT_TRUE(bits.All());
    EXPECT_EQ(bits.Count(), 130U);
    const BitSet<130U> inverted = ~bits;
    EXPECT_TRUE(inverted.None());
    EXPECT_EQ(bits, ~inverted);
    bits.ResetAll();
    EXPECT_TRUE(bits.None());
    EXPECT_FALSE(bits.All());
}

TEST(BitSetTest, WordOperators_CombineLiveBits) {
    BitSet<100U> left;
    BitSet<100U> right;
    left.Set(1U);
    left.Set(70U);
    right.Set(70U);
    right.Set(99U);
    const BitSet<100U> both = left & right;
    EXPECT_EQ(both.Count(), 1U);
    EXPECT_TRUE(both.Test(70U));
    const BitSet<100U> either = left | right;
    EXPECT_EQ(either.Count(), 3U);
    const BitSet<100U> exclusive = left ^ right;
    EXPECT_EQ(exclusive.Count(), 2U);
    EXPECT_FALSE(exclusive.Test(70U));
    EXPECT_NE(left, right);
}

} // namespace
} // namespace UVE::Containers::Tests
