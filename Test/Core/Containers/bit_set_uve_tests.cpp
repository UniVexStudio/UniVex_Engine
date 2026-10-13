// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include "uve/containers/bit_set_uve.h"

namespace UVE::Containers::Tests {
namespace {

TEST(BitSetUVETest, SetTestResetFlip_RoundTrip) {
    BitSetUVE<130U> bits;
    EXPECT_TRUE(bits.NoneUVE());
    bits.SetUVE(0U);
    bits.SetUVE(63U);
    bits.SetUVE(64U);
    bits.SetUVE(129U);
    EXPECT_TRUE(bits.TestUVE(0U));
    EXPECT_TRUE(bits.TestUVE(129U));
    EXPECT_FALSE(bits.TestUVE(1U));
    EXPECT_EQ(bits.CountUVE(), 4U);
    bits.ResetUVE(63U);
    EXPECT_FALSE(bits.TestUVE(63U));
    bits.FlipUVE(63U);
    EXPECT_TRUE(bits.TestUVE(63U));
    bits.SetUVE(1U, true);
    bits.SetUVE(0U, false);
    EXPECT_TRUE(bits.TestUVE(1U));
    EXPECT_FALSE(bits.TestUVE(0U));
}

TEST(BitSetUVETest, SpareBits_NeverLeakIntoEqualityOrAll) {
    // 130 bits need 3 words (192); the 62 spare bits must stay zero through every op.
    BitSetUVE<130U> bits;
    bits.SetAllUVE();
    EXPECT_TRUE(bits.AllUVE());
    EXPECT_EQ(bits.CountUVE(), 130U);
    const BitSetUVE<130U> inverted = ~bits;
    EXPECT_TRUE(inverted.NoneUVE());
    EXPECT_EQ(bits, ~inverted);
    bits.ResetAllUVE();
    EXPECT_TRUE(bits.NoneUVE());
    EXPECT_FALSE(bits.AllUVE());
}

TEST(BitSetUVETest, WordOperators_CombineLiveBits) {
    BitSetUVE<100U> left;
    BitSetUVE<100U> right;
    left.SetUVE(1U);
    left.SetUVE(70U);
    right.SetUVE(70U);
    right.SetUVE(99U);
    const BitSetUVE<100U> both = left & right;
    EXPECT_EQ(both.CountUVE(), 1U);
    EXPECT_TRUE(both.TestUVE(70U));
    const BitSetUVE<100U> either = left | right;
    EXPECT_EQ(either.CountUVE(), 3U);
    const BitSetUVE<100U> exclusive = left ^ right;
    EXPECT_EQ(exclusive.CountUVE(), 2U);
    EXPECT_FALSE(exclusive.TestUVE(70U));
    EXPECT_NE(left, right);
}

} // namespace
} // namespace UVE::Containers::Tests
