// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_value_codec_uve.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "uve/uvscript/uvscript_value_uve.h"

namespace {

using UVE::Network::AppendWireValueUVE;
using UVE::Network::kWireValueMaxDepthUVE;
using UVE::Network::ReadWireStringUVE;
using UVE::Network::ReadWireValueUVE;
using UVE::UVScript::FormatValueUVE;
using UVE::UVScript::MakeListValueUVE;
using UVE::UVScript::MakeMapValueUVE;
using UVE::UVScript::MakeTupleValueUVE;
using UVE::UVScript::ObjectRefUVE;
using UVE::UVScript::ValueUVE;
using UVE::UVScript::Vec3ValueUVE;

std::vector<std::byte> EncodeOrDie(const ValueUVE& value, std::size_t depth = 0U) {
    std::vector<std::byte> out;
    EXPECT_TRUE(AppendWireValueUVE(out, value, depth));
    return out;
}

void ExpectRoundTrip(const ValueUVE& value) {
    const std::vector<std::byte> bytes = EncodeOrDie(value);
    std::size_t offset = 0U;
    const std::optional<ValueUVE> decoded = ReadWireValueUVE(bytes, offset, 0U);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(FormatValueUVE(*decoded), FormatValueUVE(value));
    EXPECT_EQ(offset, bytes.size());
}

std::vector<std::byte> Bytes(std::initializer_list<std::uint8_t> raw) {
    std::vector<std::byte> out;
    for (std::uint8_t b : raw) {
        out.push_back(static_cast<std::byte>(b));
    }
    return out;
}

} // namespace

TEST(NetworkValueCodecUVETest, ScalarsRoundTrip) {
    ExpectRoundTrip(ValueUVE{});
    ExpectRoundTrip(ValueUVE{true});
    ExpectRoundTrip(ValueUVE{false});
    ExpectRoundTrip(ValueUVE{std::int64_t{0}});
    ExpectRoundTrip(ValueUVE{std::int64_t{-1}});
    ExpectRoundTrip(ValueUVE{std::int64_t{9223372036854775807LL}});
    ExpectRoundTrip(ValueUVE{-3.25});
    ExpectRoundTrip(ValueUVE{std::string{}});
    ExpectRoundTrip(ValueUVE{std::string{"hello"}});
    ExpectRoundTrip(ValueUVE{std::string{"h\xC3\xA9llo"}});
    ExpectRoundTrip(ValueUVE{Vec3ValueUVE{1.5, -2.0, 0.25}});
}

TEST(NetworkValueCodecUVETest, CollectionsRoundTrip) {
    ExpectRoundTrip(MakeListValueUVE({}));
    ExpectRoundTrip(MakeTupleValueUVE({}));
    ExpectRoundTrip(MakeMapValueUVE({}));
    ExpectRoundTrip(MakeListValueUVE({ValueUVE{std::int64_t{1}}, ValueUVE{std::string{"two"}},
                                      ValueUVE{true}, ValueUVE{4.5}}));
    ExpectRoundTrip(MakeTupleValueUVE({ValueUVE{Vec3ValueUVE{1.0, 2.0, 3.0}}}));
    ExpectRoundTrip(MakeMapValueUVE({ValueUVE{std::string{"hp"}}, ValueUVE{std::int64_t{83}},
                                      ValueUVE{std::string{"pos"}},
                                      MakeListValueUVE({ValueUVE{1.0}, ValueUVE{2.0}})}));
    ExpectRoundTrip(MakeListValueUVE(
        {MakeMapValueUVE({ValueUVE{std::int64_t{7}}, MakeTupleValueUVE({ValueUVE{}})}),
         MakeListValueUVE({MakeListValueUVE({ValueUVE{false}})})}));
}

TEST(NetworkValueCodecUVETest, TagBytesMatchTheContract) {
    EXPECT_EQ(EncodeOrDie(ValueUVE{}), Bytes({0x00}));
    EXPECT_EQ(EncodeOrDie(ValueUVE{true}), Bytes({0x01, 0x01}));
    EXPECT_EQ(EncodeOrDie(ValueUVE{false}), Bytes({0x01, 0x00}));
    EXPECT_EQ(EncodeOrDie(ValueUVE{std::int64_t{0x0102030405060708LL}}),
              Bytes({0x02, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01}));
    EXPECT_EQ(EncodeOrDie(ValueUVE{1.0}), Bytes({0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F}));
    EXPECT_EQ(EncodeOrDie(ValueUVE{std::string{"AB"}}), Bytes({0x04, 0x02, 0x00, 0x41, 0x42}));
    EXPECT_EQ(EncodeOrDie(MakeListValueUVE({})), Bytes({0x06, 0x00, 0x00}));
    EXPECT_EQ(EncodeOrDie(MakeMapValueUVE({})), Bytes({0x07, 0x00, 0x00}));
    EXPECT_EQ(EncodeOrDie(MakeTupleValueUVE({})), Bytes({0x08, 0x00, 0x00}));
}

TEST(NetworkValueCodecUVETest, ObjectRefsAndUnknownTagsNeverCross) {
    std::vector<std::byte> out;
    EXPECT_FALSE(AppendWireValueUVE(out, ValueUVE{ObjectRefUVE{42U}}, 0U));
    for (const std::uint8_t tag :
         {std::uint8_t{0x09U}, std::uint8_t{0x7FU}, std::uint8_t{0xFFU}}) {
        std::size_t offset = 0U;
        EXPECT_FALSE(ReadWireValueUVE(Bytes({tag}), offset, 0U).has_value()) << static_cast<int>(tag);
    }
}

TEST(NetworkValueCodecUVETest, DepthCapRefusesBothWays) {
    ValueUVE nested{std::int64_t{1}};
    for (std::size_t i = 0U; i < kWireValueMaxDepthUVE; ++i) {
        nested = MakeListValueUVE({nested});
    }
    std::vector<std::byte> deepest;
    EXPECT_TRUE(AppendWireValueUVE(deepest, nested, 0U));
    nested = MakeListValueUVE({nested});
    std::vector<std::byte> refused;
    EXPECT_FALSE(AppendWireValueUVE(refused, nested, 0U));

    std::vector<std::byte> deep;
    for (std::size_t i = 0U; i <= kWireValueMaxDepthUVE; ++i) {
        deep.push_back(static_cast<std::byte>(0x06));
        deep.push_back(static_cast<std::byte>(0x01));
        deep.push_back(static_cast<std::byte>(0x00));
    }
    deep.push_back(static_cast<std::byte>(0x00));
    std::size_t offset = 0U;
    EXPECT_FALSE(ReadWireValueUVE(deep, offset, 0U).has_value());
}

TEST(NetworkValueCodecUVETest, TruncationFailsEveryShape) {
    const std::vector<std::vector<std::byte>> cases = {
        Bytes({}),                                   // No tag at all.
        Bytes({0x02, 0x01, 0x02, 0x03}),             // Int cut mid-word.
        Bytes({0x04, 0x05, 0x00, 0x41, 0x42}),       // String shorter than its length.
        Bytes({0x05, 0x00, 0x00}),                   // Vec3 cut mid-word.
        Bytes({0x06, 0x02, 0x00, 0x00}),             // List missing its second item.
        Bytes({0x07, 0x01, 0x00, 0x02, 0x01, 0x00}), // Map key truncated; value absent.
        Bytes({0x08, 0x01, 0x00}),                   // Tuple count with no item.
    };
    for (const std::vector<std::byte>& bytes : cases) {
        std::size_t offset = 0U;
        EXPECT_FALSE(ReadWireValueUVE(bytes, offset, 0U).has_value()) << bytes.size();
    }
}

TEST(NetworkValueCodecUVETest, OffsetAdvancesExactlyPastEachValue) {
    std::vector<std::byte> bytes = EncodeOrDie(ValueUVE{std::int64_t{9}});
    const std::vector<std::byte> tail = EncodeOrDie(ValueUVE{false});
    bytes.insert(bytes.end(), tail.begin(), tail.end());
    std::size_t offset = 0U;
    const std::optional<ValueUVE> first = ReadWireValueUVE(bytes, offset, 0U);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(offset, 9U);
    const std::optional<ValueUVE> second = ReadWireValueUVE(bytes, offset, 0U);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(offset, bytes.size());
    EXPECT_EQ(FormatValueUVE(*first), "9");
    EXPECT_EQ(FormatValueUVE(*second), "false");
}

TEST(NetworkValueCodecUVETest, StringsAreBoundedAt64K) {
    EXPECT_EQ(EncodeOrDie(ValueUVE{std::string(65535U, 'x')}).size(), 65538U);
    std::vector<std::byte> refused;
    EXPECT_FALSE(AppendWireValueUVE(refused, ValueUVE{std::string(65536U, 'x')}, 0U));
}

TEST(NetworkValueCodecUVETest, OddMapRefuses) {
    std::vector<std::byte> refused;
    EXPECT_FALSE(AppendWireValueUVE(
        refused,
        MakeMapValueUVE({ValueUVE{std::string{"k"}}, ValueUVE{std::int64_t{1}}, ValueUVE{true}}),
        0U));
}

TEST(NetworkValueCodecUVETest, PlainStringsReadRaw) {
    const std::vector<std::byte> bytes = Bytes({0x03, 0x00, 0x6F, 0x6B, 0x21, 0xAA});
    std::size_t offset = 0U;
    const std::optional<std::string> text = ReadWireStringUVE(bytes, offset);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "ok!");
    EXPECT_EQ(offset, 5U);
    std::size_t shortOffset = 0U;
    EXPECT_FALSE(ReadWireStringUVE(Bytes({0x02, 0x00, 0x41}), shortOffset).has_value());
}
