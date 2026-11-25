// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cctype>

#include <gtest/gtest.h>

#include "uuid.h"

namespace UVE::Utilities::Tests {
namespace {

TEST(UuidTest, Nil_IsAllZero) {
    const Uuid nil = Uuid::Nil();
    EXPECT_TRUE(nil.IsNil());
    EXPECT_EQ(nil.ToString(), "00000000-0000-0000-0000-000000000000");
}

TEST(UuidTest, RandomV4_SetsVersionAndVariantBits) {
    const Uuid id = Uuid::MakeRandomV4(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL);
    EXPECT_FALSE(id.IsNil());
    EXPECT_EQ(static_cast<unsigned>(id.bytes[6] & 0xF0U), 0x40U); // Version 4.
    EXPECT_EQ(static_cast<unsigned>(id.bytes[8] & 0xC0U), 0x80U); // RFC variant.
    // Same entropy, same id — determinism is the caller's when the entropy is.
    EXPECT_EQ(id, Uuid::MakeRandomV4(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL));
    EXPECT_NE(id, Uuid::MakeRandomV4(0U, 0U));
}

TEST(UuidTest, Parse_RoundTripsTheCanonicalForm) {
    const Uuid id = Uuid::MakeRandomV4(0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL);
    const std::string text = id.ToString();
    EXPECT_EQ(text.size(), 36U);
    const std::optional<Uuid> parsed = Uuid::TryParse(text);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, id);
    // Case-insensitive: the canonical form parses back uppercased.
    std::string upper = text;
    for (char& c : upper) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    EXPECT_EQ(Uuid::TryParse(upper), parsed);
}

TEST(UuidTest, Parse_RejectsMalformedText) {
    EXPECT_FALSE(Uuid::TryParse("too-short").has_value());
    EXPECT_FALSE(Uuid::TryParse("00000000-0000-0000-0000-00000000000").has_value()); // 35.
    EXPECT_FALSE(Uuid::TryParse("0000000000000000-0000-000000000000").has_value()); // Shifted dash.
    EXPECT_FALSE(Uuid::TryParse("00000000-0000-0000-0000-00000000000g").has_value()); // Non-hex.
    EXPECT_FALSE(Uuid::TryParse("").has_value());
}

} // namespace
} // namespace UVE::Utilities::Tests
