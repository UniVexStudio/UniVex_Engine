// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/object/variant_uve.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

namespace UVE::Core::Tests {
namespace {

TEST(VariantUVETest, EveryTypeHasADistinctStableNameThatParsesBack) {
    std::set<std::string> names;
    for (const VariantTypeUVE type : GetAllVariantTypesUVE()) {
        const std::string_view name = GetVariantTypeNameUVE(type);
        ASSERT_FALSE(name.empty());
        EXPECT_TRUE(names.insert(std::string{name}).second) << "duplicate name " << name;
        // Scene files store the name, so every name must come back as the same type.
        EXPECT_EQ(TryParseVariantTypeNameUVE(name), type);
        EXPECT_FALSE(GetVariantTypeCategoryUVE(type).empty());
    }
    EXPECT_EQ(names.size(), kVariantTypeCountUVE);
    EXPECT_FALSE(TryParseVariantTypeNameUVE("NotAType").has_value());
}

TEST(VariantUVETest, MakeDefaultUVE_StoresTheTypeItWasAskedForWithAMatchingValue) {
    for (const VariantTypeUVE type : GetAllVariantTypesUVE()) {
        const VariantUVE value = VariantUVE::MakeDefaultUVE(type);
        EXPECT_EQ(value.GetTypeUVE(), type) << GetVariantTypeNameUVE(type);
        EXPECT_TRUE(IsVariantWithinBoundsUVE(value));
    }
    // Types that share a representation stay distinct: a node reference is not a plain string.
    EXPECT_NE(VariantUVE::MakeTextUVE(VariantTypeUVE::Node, "Player"),
              VariantUVE::MakeTextUVE(VariantTypeUVE::String, "Player"));
    EXPECT_EQ(VariantUVE::MakeDefaultUVE(VariantTypeUVE::Quaternion).TryGetUVE<Math::QuaternionUVE>()->w, 1.0F);
    EXPECT_EQ(VariantUVE::MakeDefaultUVE(VariantTypeUVE::Color).TryGetUVE<VariantColorUVE>()->a, 1.0F);
}

TEST(VariantUVETest, TryConvertVariantUVE_ReportsWhetherAnythingWasLost) {
    const auto convert = [](const VariantUVE& value, const VariantTypeUVE target) {
        const std::optional<VariantConversionUVE> result = TryConvertVariantUVE(value, target);
        EXPECT_TRUE(result.has_value()) << GetVariantTypeNameUVE(value.GetTypeUVE()) << " -> "
                                        << GetVariantTypeNameUVE(target);
        return result.value_or(VariantConversionUVE{});
    };

    // int -> float is exact; float -> int keeps the whole part and says the fraction went.
    VariantConversionUVE result = convert(VariantUVE::MakeIntUVE(7), VariantTypeUVE::Float);
    EXPECT_TRUE(result.lossless);
    EXPECT_EQ(*result.value.TryGetUVE<double>(), 7.0);
    result = convert(VariantUVE::MakeFloatUVE(2.75), VariantTypeUVE::Int);
    EXPECT_FALSE(result.lossless);
    EXPECT_EQ(*result.value.TryGetUVE<std::int64_t>(), 2);

    // Number <-> text round-trips exactly, including a float that needs every digit.
    result = convert(VariantUVE::MakeFloatUVE(0.1), VariantTypeUVE::String);
    EXPECT_TRUE(result.lossless);
    result = convert(result.value, VariantTypeUVE::Float);
    EXPECT_EQ(*result.value.TryGetUVE<double>(), 0.1);
    EXPECT_FALSE(TryConvertVariantUVE(VariantUVE::MakeTextUVE(VariantTypeUVE::String, "seven"), VariantTypeUVE::Int)
                     .has_value());

    // Vectors: widening is lossless, narrowing away a non-zero component is not.
    VariantUVE vector3 = VariantUVE::MakeDefaultUVE(VariantTypeUVE::Vector3);
    *vector3.TryGetMutableUVE<Math::Vector3UVE>() = {1.0F, 2.0F, 3.0F};
    result = convert(vector3, VariantTypeUVE::Vector2);
    EXPECT_FALSE(result.lossless);
    EXPECT_EQ(result.value.TryGetUVE<Math::Vector2UVE>()->y, 2.0F);
    result = convert(vector3, VariantTypeUVE::Color);
    EXPECT_TRUE(result.lossless);
    EXPECT_EQ(result.value.TryGetUVE<VariantColorUVE>()->a, 1.0F); // Opaque unless the source said otherwise.

    // Arrays convert element by element; a byte that does not fit is reported, not wrapped.
    VariantUVE packed = VariantUVE::MakeDefaultUVE(VariantTypeUVE::PackedInt32Array);
    *packed.TryGetMutableUVE<std::vector<std::int32_t>>() = {1, 2, 300};
    result = convert(packed, VariantTypeUVE::Array);
    EXPECT_TRUE(result.lossless);
    EXPECT_EQ(result.value.TryGetUVE<std::vector<VariantUVE>>()->size(), 3U);
    result = convert(packed, VariantTypeUVE::PackedByteArray);
    EXPECT_FALSE(result.lossless);

    // Some pairs have no meaningful conversion at all, and say so instead of inventing one.
    EXPECT_FALSE(TryConvertVariantUVE(VariantUVE::MakeDefaultUVE(VariantTypeUVE::Color), VariantTypeUVE::NodePath)
                     .has_value());
    EXPECT_FALSE(TryConvertVariantUVE(VariantUVE::MakeDefaultUVE(VariantTypeUVE::Dictionary), VariantTypeUVE::Int)
                     .has_value());
}

TEST(VariantUVETest, IsVariantWithinBoundsUVE_RejectsWhatAHostileFileCouldBuild) {
    VariantUVE nested = VariantUVE::MakeDefaultUVE(VariantTypeUVE::Array);
    for (std::size_t depth = 0; depth <= kMaximumVariantDepthUVE + 1U; ++depth) {
        VariantUVE outer = VariantUVE::MakeDefaultUVE(VariantTypeUVE::Array);
        outer.TryGetMutableUVE<std::vector<VariantUVE>>()->push_back(std::move(nested));
        nested = std::move(outer);
    }
    EXPECT_FALSE(IsVariantWithinBoundsUVE(nested));

    const VariantUVE longText =
        VariantUVE::MakeTextUVE(VariantTypeUVE::String, std::string(kMaximumVariantTextBytesUVE + 1U, 'x'));
    EXPECT_FALSE(IsVariantWithinBoundsUVE(longText));

    VariantUVE dictionary = VariantUVE::MakeDefaultUVE(VariantTypeUVE::Dictionary);
    dictionary.TryGetMutableUVE<std::vector<VariantDictionaryEntryUVE>>()->push_back(
        {"note", VariantUVE::MakeTextUVE(VariantTypeUVE::String, "fine")});
    EXPECT_TRUE(IsVariantWithinBoundsUVE(dictionary));
}

} // namespace
} // namespace UVE::Core::Tests
