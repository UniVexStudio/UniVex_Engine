// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "core_variant.h"

#include <gtest/gtest.h>

#include <array>
#include <set>
#include <string>
#include <utility>

namespace UVE::Core::Tests {
namespace {
TEST(VariantTest, RecursiveDictionaryCopiesOwnTheirEntriesAndSurviveMoves) {
    auto original = Variant::MakeDefault(VariantType::Dictionary);
    original.TryGetMutable<std::vector<VariantDictionaryEntry>>()->push_back(
        {"value", Variant::MakeText(VariantType::String, "original")});
    Variant copy = original;
    copy.TryGetMutable<std::vector<VariantDictionaryEntry>>()->front().value = Variant::MakeInt(7);
    Variant moved = std::move(copy);
    EXPECT_EQ(*moved.TryGet<std::vector<VariantDictionaryEntry>>()->front().value.TryGet<std::int64_t>(), 7);
    EXPECT_EQ(*original.TryGet<std::vector<VariantDictionaryEntry>>()->front().value.TryGet<std::string>(), "original");
}


TEST(VariantTest, EveryTypeHasADistinctStableNameThatParsesBack) {
    std::set<std::string> names;
    for (const VariantType type : GetAllVariantTypes()) {
        const std::string_view name = GetVariantTypeName(type);
        ASSERT_FALSE(name.empty());
        EXPECT_TRUE(names.insert(std::string{name}).second) << "duplicate name " << name;
        // Scene files store the name, so every name must come back as the same type.
        EXPECT_EQ(TryParseVariantTypeName(name), type);
        EXPECT_FALSE(GetVariantTypeCategory(type).empty());
    }
    EXPECT_EQ(names.size(), kVariantTypeCount);
    EXPECT_FALSE(TryParseVariantTypeName("NotAType").has_value());
}

// A scene file stores a variant's type BY NAME, so a renamed type has to stay readable under the
// name it was written with. Every retired name goes here the same commit it is retired - this is
// what makes the vocabulary pass a rename rather than a format break.
TEST(VariantTest, RetiredTypeNamesStillParseButAreNeverWritten) {
    constexpr std::array<std::pair<std::string_view, VariantType>, 12U> kRetired{{
        {"NodePath", VariantType::ObjectPath},
        {"Node", VariantType::Object},
        {"PackedByteArray", VariantType::ByteArray},
        {"PackedInt32Array", VariantType::Int32Array},
        {"PackedInt64Array", VariantType::Int64Array},
        {"PackedFloat32Array", VariantType::Float32Array},
        {"PackedFloat64Array", VariantType::Float64Array},
        {"PackedStringArray", VariantType::StringArray},
        {"PackedVector2Array", VariantType::Vector2Array},
        {"PackedVector3Array", VariantType::Vector3Array},
        {"PackedColorArray", VariantType::ColorArray},
        {"StringName", VariantType::InternedString},
    }};
    for (const auto& [retired, current] : kRetired) {
        // Reads: an old document loads as the type it always meant.
        EXPECT_EQ(TryParseVariantTypeName(retired), current) << retired;
        // Writes: the current name, never the retired one - otherwise the alias would never stop
        // being needed and the old name would creep back into new documents.
        EXPECT_NE(GetVariantTypeName(current), retired) << retired;
    }
}

TEST(VariantTest, MakeDefault_StoresTheTypeItWasAskedForWithAMatchingValue) {    for (const VariantType type : GetAllVariantTypes()) {
        const Variant value = Variant::MakeDefault(type);
        EXPECT_EQ(value.GetType(), type) << GetVariantTypeName(type);
        EXPECT_TRUE(IsVariantWithinBounds(value));
    }
    // Types that share a representation stay distinct: an object reference is not a plain string.
    EXPECT_NE(Variant::MakeText(VariantType::Object, "Player"),
              Variant::MakeText(VariantType::String, "Player"));
    EXPECT_EQ(Variant::MakeDefault(VariantType::Quaternion).TryGet<Math::Quaternion>()->w, 1.0F);
    EXPECT_EQ(Variant::MakeDefault(VariantType::Color).TryGet<VariantColor>()->a, 1.0F);
}

TEST(VariantTest, TryConvertVariant_ReportsWhetherAnythingWasLost) {
    const auto convert = [](const Variant& value, const VariantType target) {
        const std::optional<VariantConversion> result = TryConvertVariant(value, target);
        EXPECT_TRUE(result.has_value()) << GetVariantTypeName(value.GetType()) << " -> "
                                        << GetVariantTypeName(target);
        return result.value_or(VariantConversion{});
    };

    // int -> float is exact; float -> int keeps the whole part and says the fraction went.
    VariantConversion result = convert(Variant::MakeInt(7), VariantType::Float);
    EXPECT_TRUE(result.lossless);
    EXPECT_EQ(*result.value.TryGet<double>(), 7.0);
    result = convert(Variant::MakeFloat(2.75), VariantType::Int);
    EXPECT_FALSE(result.lossless);
    EXPECT_EQ(*result.value.TryGet<std::int64_t>(), 2);

    // Number <-> text round-trips exactly, including a float that needs every digit.
    result = convert(Variant::MakeFloat(0.1), VariantType::String);
    EXPECT_TRUE(result.lossless);
    result = convert(result.value, VariantType::Float);
    EXPECT_EQ(*result.value.TryGet<double>(), 0.1);
    EXPECT_FALSE(TryConvertVariant(Variant::MakeText(VariantType::String, "seven"), VariantType::Int)
                     .has_value());

    // Vectors: widening is lossless, narrowing away a non-zero component is not.
    Variant vector3 = Variant::MakeDefault(VariantType::Vector3);
    *vector3.TryGetMutable<Math::Vector3>() = {1.0F, 2.0F, 3.0F};
    result = convert(vector3, VariantType::Vector2);
    EXPECT_FALSE(result.lossless);
    EXPECT_EQ(result.value.TryGet<Math::Vector2>()->y, 2.0F);
    result = convert(vector3, VariantType::Color);
    EXPECT_TRUE(result.lossless);
    EXPECT_EQ(result.value.TryGet<VariantColor>()->a, 1.0F); // Opaque unless the source said otherwise.

    // Arrays convert element by element; a byte that does not fit is reported, not wrapped.
    Variant packed = Variant::MakeDefault(VariantType::Int32Array);
    *packed.TryGetMutable<std::vector<std::int32_t>>() = {1, 2, 300};
    result = convert(packed, VariantType::Array);
    EXPECT_TRUE(result.lossless);
    EXPECT_EQ(result.value.TryGet<std::vector<Variant>>()->size(), 3U);
    result = convert(packed, VariantType::ByteArray);
    EXPECT_FALSE(result.lossless);

    // Some pairs have no meaningful conversion at all, and say so instead of inventing one.
    EXPECT_FALSE(TryConvertVariant(Variant::MakeDefault(VariantType::Color), VariantType::ObjectPath)
                     .has_value());
    EXPECT_FALSE(TryConvertVariant(Variant::MakeDefault(VariantType::Dictionary), VariantType::Int)
                     .has_value());
}

TEST(VariantTest, IsVariantWithinBounds_RejectsWhatAHostileFileCouldBuild) {
    Variant nested = Variant::MakeDefault(VariantType::Array);
    for (std::size_t depth = 0; depth <= kMaximumVariantDepth + 1U; ++depth) {
        Variant outer = Variant::MakeDefault(VariantType::Array);
        outer.TryGetMutable<std::vector<Variant>>()->push_back(std::move(nested));
        nested = std::move(outer);
    }
    EXPECT_FALSE(IsVariantWithinBounds(nested));

    const Variant longText =
        Variant::MakeText(VariantType::String, std::string(kMaximumVariantTextBytes + 1U, 'x'));
    EXPECT_FALSE(IsVariantWithinBounds(longText));

    Variant dictionary = Variant::MakeDefault(VariantType::Dictionary);
    dictionary.TryGetMutable<std::vector<VariantDictionaryEntry>>()->push_back(
        {"note", Variant::MakeText(VariantType::String, "fine")});
    EXPECT_TRUE(IsVariantWithinBounds(dictionary));
}

} // namespace
} // namespace UVE::Core::Tests
