// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cctype>

#include <gtest/gtest.h>

#include "uve/utilities/uuid_uve.h"

namespace UVE::Utilities::Tests {
namespace {

TEST(UuidUVETest, Nil_IsAllZero) {
    const UuidUVE nil = UuidUVE::NilUVE();
    EXPECT_TRUE(nil.IsNilUVE());
    EXPECT_EQ(nil.ToStringUVE(), "00000000-0000-0000-0000-000000000000");
}

TEST(UuidUVETest, RandomV4_SetsVersionAndVariantBits) {
    const UuidUVE id = UuidUVE::MakeRandomV4UVE(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL);
    EXPECT_FALSE(id.IsNilUVE());
    EXPECT_EQ(static_cast<unsigned>(id.bytes[6] & 0xF0U), 0x40U); // Version 4.
    EXPECT_EQ(static_cast<unsigned>(id.bytes[8] & 0xC0U), 0x80U); // RFC variant.
    // Same entropy, same id — determinism is the caller's when the entropy is.
    EXPECT_EQ(id, UuidUVE::MakeRandomV4UVE(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL));
    EXPECT_NE(id, UuidUVE::MakeRandomV4UVE(0U, 0U));
}

TEST(UuidUVETest, Parse_RoundTripsTheCanonicalForm) {
    const UuidUVE id = UuidUVE::MakeRandomV4UVE(0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL);
    const std::string text = id.ToStringUVE();
    EXPECT_EQ(text.size(), 36U);
    const std::optional<UuidUVE> parsed = UuidUVE::TryParseUVE(text);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, id);
    // Case-insensitive: the canonical form parses back uppercased.
    std::string upper = text;
    for (char& c : upper) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    EXPECT_EQ(UuidUVE::TryParseUVE(upper), parsed);
}

TEST(UuidUVETest, Parse_RejectsMalformedText) {
    EXPECT_FALSE(UuidUVE::TryParseUVE("too-short").has_value());
    EXPECT_FALSE(UuidUVE::TryParseUVE("00000000-0000-0000-0000-00000000000").has_value()); // 35.
    EXPECT_FALSE(UuidUVE::TryParseUVE("0000000000000000-0000-000000000000").has_value()); // Shifted dash.
    EXPECT_FALSE(UuidUVE::TryParseUVE("00000000-0000-0000-0000-00000000000g").has_value()); // Non-hex.
    EXPECT_FALSE(UuidUVE::TryParseUVE("").has_value());
}

} // namespace
} // namespace UVE::Utilities::Tests
