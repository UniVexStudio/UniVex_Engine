// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

#include "uve/utilities/flags_uve.h"

namespace UVE::Utilities::Tests {
namespace {

enum class NarrowFlagsUVE : std::uint8_t {
    None = 0,
    Read = 1U << 0U,
    Write = 1U << 1U,
    Execute = 1U << 2U,
};

enum class WideFlagsUVE : std::uint32_t {
    None = 0,
    Alpha = 1U << 0U,
    Beta = 1U << 31U,
};

static_assert(FlagsUVE<NarrowFlagsUVE>{NarrowFlagsUVE::Read}.HasUVE(NarrowFlagsUVE::Read));
static_assert(!(FlagsUVE<NarrowFlagsUVE>{NarrowFlagsUVE::Read}.HasUVE(NarrowFlagsUVE::Write)));
static_assert(FlagsUVE<NarrowFlagsUVE>{}.NoneUVE());

TEST(FlagsUVETest, SingleBit_ConvertsImplicitlyAndQueries) {
    FlagsUVE<NarrowFlagsUVE> flags = NarrowFlagsUVE::Read;
    EXPECT_TRUE(flags.HasUVE(NarrowFlagsUVE::Read));
    EXPECT_FALSE(flags.HasUVE(NarrowFlagsUVE::Write));
    EXPECT_FALSE(flags.NoneUVE());
    EXPECT_EQ(static_cast<unsigned>(flags.BitsUVE()), 0x01U);
}

TEST(FlagsUVETest, Combinations_StayTypedThroughEveryOperator) {
    constexpr FlagsUVE<NarrowFlagsUVE> both = NarrowFlagsUVE::Read | NarrowFlagsUVE::Write;
    EXPECT_TRUE(both.HasUVE(NarrowFlagsUVE::Read));
    EXPECT_TRUE(both.HasUVE(NarrowFlagsUVE::Write));
    EXPECT_TRUE(both.HasAnyUVE(NarrowFlagsUVE::Execute | NarrowFlagsUVE::Write));
    EXPECT_FALSE(both.HasAnyUVE(NarrowFlagsUVE::Execute));
    EXPECT_EQ(static_cast<unsigned>((both & NarrowFlagsUVE::Read).BitsUVE()), 0x01U);
    EXPECT_EQ(static_cast<unsigned>((both ^ NarrowFlagsUVE::Read).BitsUVE()), 0x02U);
    EXPECT_TRUE((~both).HasUVE(NarrowFlagsUVE::Execute));
    FlagsUVE<NarrowFlagsUVE> mutableFlags;
    mutableFlags |= NarrowFlagsUVE::Read;
    mutableFlags |= NarrowFlagsUVE::Write;
    EXPECT_EQ(mutableFlags, both);
    mutableFlags &= NarrowFlagsUVE::Read;
    EXPECT_EQ(static_cast<unsigned>(mutableFlags.BitsUVE()), 0x01U);
}

TEST(FlagsUVETest, SetClear_SingleBitsInPlace) {
    FlagsUVE<WideFlagsUVE> flags;
    flags.SetUVE(WideFlagsUVE::Beta);
    EXPECT_TRUE(flags.HasUVE(WideFlagsUVE::Beta));
    flags.ClearUVE(WideFlagsUVE::Beta);
    EXPECT_TRUE(flags.NoneUVE());
    flags.SetUVE(WideFlagsUVE::Alpha);
    flags.ClearAllUVE();
    EXPECT_TRUE(flags.NoneUVE());
}

} // namespace
} // namespace UVE::Utilities::Tests
