// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_value_codec.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "blueprint_value.h"

namespace {

using UVE::Network::AppendWireValue;
using UVE::Network::kWireValueMaxDepth;
using UVE::Network::ReadWireString;
using UVE::Network::ReadWireValue;
using UVE::Blueprint::FormatValue;
using UVE::Blueprint::MakeListValue;
using UVE::Blueprint::MakeMapValue;
using UVE::Blueprint::MakeTupleValue;
using UVE::Blueprint::ObjectRef;
using UVE::Blueprint::Value;
using UVE::Blueprint::Vec3Value;

std::vector<std::byte> EncodeOrDie(const Value& value, std::size_t depth = 0U) {
    std::vector<std::byte> out;
    EXPECT_TRUE(AppendWireValue(out, value, depth));
    return out;
}

void ExpectRoundTrip(const Value& value) {
    const std::vector<std::byte> bytes = EncodeOrDie(value);
    std::size_t offset = 0U;
    const std::optional<Value> decoded = ReadWireValue(bytes, offset, 0U);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(FormatValue(*decoded), FormatValue(value));
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

TEST(NetworkValueCodecTest, ScalarsRoundTrip) {
    ExpectRoundTrip(Value{});
    ExpectRoundTrip(Value{true});
    ExpectRoundTrip(Value{false});
    ExpectRoundTrip(Value{std::int64_t{0}});
    ExpectRoundTrip(Value{std::int64_t{-1}});
    ExpectRoundTrip(Value{std::int64_t{9223372036854775807LL}});
    ExpectRoundTrip(Value{-3.25});
    ExpectRoundTrip(Value{std::string{}});
    ExpectRoundTrip(Value{std::string{"hello"}});
    ExpectRoundTrip(Value{std::string{"h\xC3\xA9llo"}});
    ExpectRoundTrip(Value{Vec3Value{1.5, -2.0, 0.25}});
}

TEST(NetworkValueCodecTest, CollectionsRoundTrip) {
    ExpectRoundTrip(MakeListValue({}));
    ExpectRoundTrip(MakeTupleValue({}));
    ExpectRoundTrip(MakeMapValue({}));
    ExpectRoundTrip(MakeListValue({Value{std::int64_t{1}}, Value{std::string{"two"}},
                                      Value{true}, Value{4.5}}));
    ExpectRoundTrip(MakeTupleValue({Value{Vec3Value{1.0, 2.0, 3.0}}}));
    ExpectRoundTrip(MakeMapValue({Value{std::string{"hp"}}, Value{std::int64_t{83}},
                                      Value{std::string{"pos"}},
                                      MakeListValue({Value{1.0}, Value{2.0}})}));
    ExpectRoundTrip(MakeListValue(
        {MakeMapValue({Value{std::int64_t{7}}, MakeTupleValue({Value{}})}),
         MakeListValue({MakeListValue({Value{false}})})}));
}

TEST(NetworkValueCodecTest, TagBytesMatchTheContract) {
    EXPECT_EQ(EncodeOrDie(Value{}), Bytes({0x00}));
    EXPECT_EQ(EncodeOrDie(Value{true}), Bytes({0x01, 0x01}));
    EXPECT_EQ(EncodeOrDie(Value{false}), Bytes({0x01, 0x00}));
    EXPECT_EQ(EncodeOrDie(Value{std::int64_t{0x0102030405060708LL}}),
              Bytes({0x02, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01}));
    EXPECT_EQ(EncodeOrDie(Value{1.0}), Bytes({0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F}));
    EXPECT_EQ(EncodeOrDie(Value{std::string{"AB"}}), Bytes({0x04, 0x02, 0x00, 0x41, 0x42}));
    EXPECT_EQ(EncodeOrDie(MakeListValue({})), Bytes({0x06, 0x00, 0x00}));
    EXPECT_EQ(EncodeOrDie(MakeMapValue({})), Bytes({0x07, 0x00, 0x00}));
    EXPECT_EQ(EncodeOrDie(MakeTupleValue({})), Bytes({0x08, 0x00, 0x00}));
}

TEST(NetworkValueCodecTest, ObjectRefsAndUnknownTagsNeverCross) {
    std::vector<std::byte> out;
    EXPECT_FALSE(AppendWireValue(out, Value{ObjectRef{42U}}, 0U));
    for (const std::uint8_t tag :
         {std::uint8_t{0x09U}, std::uint8_t{0x7FU}, std::uint8_t{0xFFU}}) {
        std::size_t offset = 0U;
        EXPECT_FALSE(ReadWireValue(Bytes({tag}), offset, 0U).has_value()) << static_cast<int>(tag);
    }
}

TEST(NetworkValueCodecTest, DepthCapRefusesBothWays) {
    Value nested{std::int64_t{1}};
    for (std::size_t i = 0U; i < kWireValueMaxDepth; ++i) {
        nested = MakeListValue({nested});
    }
    std::vector<std::byte> deepest;
    EXPECT_TRUE(AppendWireValue(deepest, nested, 0U));
    nested = MakeListValue({nested});
    std::vector<std::byte> refused;
    EXPECT_FALSE(AppendWireValue(refused, nested, 0U));

    std::vector<std::byte> deep;
    for (std::size_t i = 0U; i <= kWireValueMaxDepth; ++i) {
        deep.push_back(static_cast<std::byte>(0x06));
        deep.push_back(static_cast<std::byte>(0x01));
        deep.push_back(static_cast<std::byte>(0x00));
    }
    deep.push_back(static_cast<std::byte>(0x00));
    std::size_t offset = 0U;
    EXPECT_FALSE(ReadWireValue(deep, offset, 0U).has_value());
}

TEST(NetworkValueCodecTest, TruncationFailsEveryShape) {
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
        EXPECT_FALSE(ReadWireValue(bytes, offset, 0U).has_value()) << bytes.size();
    }
}

TEST(NetworkValueCodecTest, OffsetAdvancesExactlyPastEachValue) {
    std::vector<std::byte> bytes = EncodeOrDie(Value{std::int64_t{9}});
    const std::vector<std::byte> tail = EncodeOrDie(Value{false});
    bytes.insert(bytes.end(), tail.begin(), tail.end());
    std::size_t offset = 0U;
    const std::optional<Value> first = ReadWireValue(bytes, offset, 0U);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(offset, 9U);
    const std::optional<Value> second = ReadWireValue(bytes, offset, 0U);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(offset, bytes.size());
    EXPECT_EQ(FormatValue(*first), "9");
    EXPECT_EQ(FormatValue(*second), "false");
}

TEST(NetworkValueCodecTest, StringsAreBoundedAt64K) {
    EXPECT_EQ(EncodeOrDie(Value{std::string(65535U, 'x')}).size(), 65538U);
    std::vector<std::byte> refused;
    EXPECT_FALSE(AppendWireValue(refused, Value{std::string(65536U, 'x')}, 0U));
}

TEST(NetworkValueCodecTest, OddMapRefuses) {
    std::vector<std::byte> refused;
    EXPECT_FALSE(AppendWireValue(
        refused,
        MakeMapValue({Value{std::string{"k"}}, Value{std::int64_t{1}}, Value{true}}),
        0U));
}

TEST(NetworkValueCodecTest, PlainStringsReadRaw) {
    const std::vector<std::byte> bytes = Bytes({0x03, 0x00, 0x6F, 0x6B, 0x21, 0xAA});
    std::size_t offset = 0U;
    const std::optional<std::string> text = ReadWireString(bytes, offset);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "ok!");
    EXPECT_EQ(offset, 5U);
    std::size_t shortOffset = 0U;
    EXPECT_FALSE(ReadWireString(Bytes({0x02, 0x00, 0x41}), shortOffset).has_value());
}
