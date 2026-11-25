// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "hash.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <gtest/gtest.h>

namespace UVE::Utilities::Tests {
namespace {

// The canonical FNV-1a 64-bit test vectors. `static_assert` doubles as the constexpr-use proof:
// the whole byte path must evaluate at compile time.
static_assert(HashString("") == 0xcbf29ce484222325ULL);
static_assert(HashString("foobar") == 0x85944171f73967e8ULL);
static_assert(HashString("a") == 0xaf63dc4c8601ec8cULL);
static_assert(HashString("hello") == 0xa430d84680aabd0bULL);

TEST(HashTest, Fnv1a64_KnownAnswerVectors) {
    EXPECT_EQ(HashString(""), kFnv1a64OffsetBasis);
    EXPECT_EQ(HashString("foobar"), 0x85944171f73967e8ULL);
    EXPECT_EQ(HashString("a"), 0xaf63dc4c8601ec8cULL);
    EXPECT_EQ(HashString("hello"), 0xa430d84680aabd0bULL);
}

TEST(HashTest, Fnv1a64_IncrementalMatchesOneShot) {
    constexpr std::string_view kText = "The quick brown fox jumps over the lazy dog";
    const std::uint64_t expected = HashString(kText);

    // One byte at a time, then odd chunk splits — the content fingerprint feeds 16 KiB file
    // chunks, so every split shape must agree.
    Fnv1a64 bytewise;
    for (const char character : kText) {
        bytewise.AppendString(std::string_view{&character, 1U});
    }
    EXPECT_EQ(bytewise.Digest(), expected);

    Fnv1a64 chunked;
    chunked.AppendString(kText.substr(0U, 7U));
    chunked.AppendString(kText.substr(7U, 13U));
    chunked.AppendString(kText.substr(20U));
    EXPECT_EQ(chunked.Digest(), expected);

    Fnv1a64 rawBytes;
    rawBytes.AppendBytes(kText.data(), kText.size());
    EXPECT_EQ(rawBytes.Digest(), expected);
}

TEST(HashTest, Fnv1a64_EmptyAppendIsNoOp) {
    Fnv1a64 hasher;
    hasher.AppendString("");
    hasher.AppendBytes(nullptr, 0U);
    EXPECT_EQ(hasher.Digest(), kFnv1a64OffsetBasis);
    EXPECT_EQ(HashBytes(nullptr, 0U), kFnv1a64OffsetBasis);
}

TEST(HashTest, Fnv1a64_LegacySeedReproducesLegacyValues) {
    // The save checksum and the FBX corner key shipped with a transposed basis
    // (see kFnv1a64LegacyOffsetBasis). The explicit seed must reproduce those values exactly.
    Fnv1a64 fresh{kFnv1a64LegacyOffsetBasis};
    EXPECT_EQ(fresh.Digest(), kFnv1a64LegacyOffsetBasis);
    EXPECT_EQ(HashString("test", kFnv1a64LegacyOffsetBasis), 0x975819681a395537ULL);
    EXPECT_NE(HashString("test"), HashString("test", kFnv1a64LegacyOffsetBasis));
}

TEST(HashTest, HashCombine_MatchesBoostFormula) {
    // Literal pins for the boost hash_combine formula: seed ^ (value + 0x9e3779b9 +
    // (seed << 6) + (seed >> 2)). Both fit 32 bits, so the pins hold on any platform width.
    std::size_t seed = 0U;
    HashCombine(seed, 0U);
    EXPECT_EQ(seed, static_cast<std::size_t>(0x9E3779B9ULL));

    seed = 0U;
    HashCombine(seed, 1U);
    EXPECT_EQ(seed, static_cast<std::size_t>(0x9E3779BAULL));

    // Deterministic: the same sequence combines to the same seed.
    std::size_t first = 0U;
    HashCombine(first, 11U);
    HashCombine(first, 13U);
    std::size_t second = 0U;
    HashCombine(second, 11U);
    HashCombine(second, 13U);
    EXPECT_EQ(first, second);
}

TEST(HashTest, HashCombine_OrderSensitive) {
    // The mesh/material pairing hash exists because a plain XOR collides for a pairing and its
    // reverse. The combiner must not.
    std::size_t forward = 11U;
    HashCombine(forward, 13U);
    std::size_t reverse = 13U;
    HashCombine(reverse, 11U);
    EXPECT_NE(forward, reverse);
}

} // namespace
} // namespace UVE::Utilities::Tests
