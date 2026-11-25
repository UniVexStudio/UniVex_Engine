// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

#include "flags.h"

namespace UVE::Utilities::Tests {
namespace {

enum class NarrowFlags : std::uint8_t {
    None = 0,
    Read = 1U << 0U,
    Write = 1U << 1U,
    Execute = 1U << 2U,
};

enum class WideFlags : std::uint32_t {
    None = 0,
    Alpha = 1U << 0U,
    Beta = 1U << 31U,
};

static_assert(Flags<NarrowFlags>{NarrowFlags::Read}.Has(NarrowFlags::Read));
static_assert(!(Flags<NarrowFlags>{NarrowFlags::Read}.Has(NarrowFlags::Write)));
static_assert(Flags<NarrowFlags>{}.None());

TEST(FlagsTest, SingleBit_ConvertsImplicitlyAndQueries) {
    Flags<NarrowFlags> flags = NarrowFlags::Read;
    EXPECT_TRUE(flags.Has(NarrowFlags::Read));
    EXPECT_FALSE(flags.Has(NarrowFlags::Write));
    EXPECT_FALSE(flags.None());
    EXPECT_EQ(static_cast<unsigned>(flags.Bits()), 0x01U);
}

TEST(FlagsTest, Combinations_StayTypedThroughEveryOperator) {
    constexpr Flags<NarrowFlags> both = NarrowFlags::Read | NarrowFlags::Write;
    EXPECT_TRUE(both.Has(NarrowFlags::Read));
    EXPECT_TRUE(both.Has(NarrowFlags::Write));
    EXPECT_TRUE(both.HasAny(NarrowFlags::Execute | NarrowFlags::Write));
    EXPECT_FALSE(both.HasAny(NarrowFlags::Execute));
    EXPECT_EQ(static_cast<unsigned>((both & NarrowFlags::Read).Bits()), 0x01U);
    EXPECT_EQ(static_cast<unsigned>((both ^ NarrowFlags::Read).Bits()), 0x02U);
    EXPECT_TRUE((~both).Has(NarrowFlags::Execute));
    Flags<NarrowFlags> mutableFlags;
    mutableFlags |= NarrowFlags::Read;
    mutableFlags |= NarrowFlags::Write;
    EXPECT_EQ(mutableFlags, both);
    mutableFlags &= NarrowFlags::Read;
    EXPECT_EQ(static_cast<unsigned>(mutableFlags.Bits()), 0x01U);
}

TEST(FlagsTest, SetClear_SingleBitsInPlace) {
    Flags<WideFlags> flags;
    flags.Set(WideFlags::Beta);
    EXPECT_TRUE(flags.Has(WideFlags::Beta));
    flags.Clear(WideFlags::Beta);
    EXPECT_TRUE(flags.None());
    flags.Set(WideFlags::Alpha);
    flags.ClearAll();
    EXPECT_TRUE(flags.None());
}

} // namespace
} // namespace UVE::Utilities::Tests
