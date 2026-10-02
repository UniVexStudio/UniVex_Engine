// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/object/variant_uve.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <system_error>

namespace UVE::Core {
namespace {

struct VariantTypeInfoUVE final {
    VariantTypeUVE type;
    std::string_view name;
    std::string_view category;
};

// One row per type, in enumerator order. The static_assert below is what keeps this table and the
// enumeration from drifting apart when a type is added.
constexpr std::array<VariantTypeInfoUVE, kVariantTypeCountUVE> kVariantTypeInfoUVE{{
    {VariantTypeUVE::Bool, "bool", "Basic"},
    {VariantTypeUVE::Int, "int", "Basic"},
    {VariantTypeUVE::Float, "float", "Basic"},
    {VariantTypeUVE::String, "String", "Basic"},
    {VariantTypeUVE::StringName, "StringName", "Basic"},
    {VariantTypeUVE::NodePath, "NodePath", "Basic"},
    {VariantTypeUVE::Vector2, "Vector2", "Math"},
    {VariantTypeUVE::Vector3, "Vector3", "Math"},
    {VariantTypeUVE::Vector4, "Vector4", "Math"},
    {VariantTypeUVE::Quaternion, "Quaternion", "Math"},
    {VariantTypeUVE::Color, "Color", "Color"},
    {VariantTypeUVE::Resource, "Resource", "Reference"},
    {VariantTypeUVE::Node, "Node", "Reference"},
    {VariantTypeUVE::Array, "Array", "Collection"},
    {VariantTypeUVE::Dictionary, "Dictionary", "Collection"},
    {VariantTypeUVE::PackedByteArray, "PackedByteArray", "Packed Array"},
    {VariantTypeUVE::PackedInt32Array, "PackedInt32Array", "Packed Array"},
    {VariantTypeUVE::PackedInt64Array, "PackedInt64Array", "Packed Array"},
    {VariantTypeUVE::PackedFloat32Array, "PackedFloat32Array", "Packed Array"},
    {VariantTypeUVE::PackedFloat64Array, "PackedFloat64Array", "Packed Array"},
    {VariantTypeUVE::PackedStringArray, "PackedStringArray", "Packed Array"},
    {VariantTypeUVE::PackedVector2Array, "PackedVector2Array", "Packed Array"},
    {VariantTypeUVE::PackedVector3Array, "PackedVector3Array", "Packed Array"},
    {VariantTypeUVE::PackedColorArray, "PackedColorArray", "Packed Array"},
}};

[[nodiscard]] constexpr bool IsTableInEnumeratorOrderUVE() noexcept {
    for (std::size_t index = 0; index < kVariantTypeInfoUVE.size(); ++index) {
        if (static_cast<std::size_t>(kVariantTypeInfoUVE[index].type) != index) {
            return false;
        }
    }
    return true;
}
static_assert(IsTableInEnumeratorOrderUVE(), "kVariantTypeInfoUVE must list every type in enumerator order.");

[[nodiscard]] bool IsTextTypeUVE(const VariantTypeUVE type) noexcept {
    return type == VariantTypeUVE::String || type == VariantTypeUVE::StringName ||
           type == VariantTypeUVE::NodePath || type == VariantTypeUVE::Node;
}

[[nodiscard]] bool IsNumericTypeUVE(const VariantTypeUVE type) noexcept {
    return type == VariantTypeUVE::Bool || type == VariantTypeUVE::Int || type == VariantTypeUVE::Float;
}

/// A value's number, whatever numeric type it is.
[[nodiscard]] double AsNumberUVE(const VariantUVE& value) noexcept {
    if (const bool* const flag = value.TryGetUVE<bool>(); flag != nullptr) {
        return *flag ? 1.0 : 0.0;
    }
    if (const std::int64_t* const integer = value.TryGetUVE<std::int64_t>(); integer != nullptr) {
        return static_cast<double>(*integer);
    }
    if (const double* const real = value.TryGetUVE<double>(); real != nullptr) {
        return *real;
    }
    return 0.0;
}

[[nodiscard]] std::string NumberToTextUVE(const VariantUVE& value) {
    if (const bool* const flag = value.TryGetUVE<bool>(); flag != nullptr) {
        return *flag ? "true" : "false";
    }
    if (const std::int64_t* const integer = value.TryGetUVE<std::int64_t>(); integer != nullptr) {
        return std::to_string(*integer);
    }
    // Shortest text that parses back to the same double, so a Float -> String -> Float round trip
    // is exact rather than drifting in the last digits the way a fixed precision would.
    std::array<char, 64> buffer{};
    const double real = AsNumberUVE(value);
    const std::to_chars_result result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), real);
    return result.ec == std::errc{} ? std::string(buffer.data(), result.ptr) : std::string{};
}

/// Converts between numeric types. Lossless exactly when the target can represent the number.
[[nodiscard]] VariantConversionUVE ConvertNumberUVE(const VariantUVE& value, const VariantTypeUVE target) {
    const double number = AsNumberUVE(value);
    switch (target) {
        case VariantTypeUVE::Bool:
            return {VariantUVE::MakeBoolUVE(number != 0.0), number == 0.0 || number == 1.0};
        case VariantTypeUVE::Int: {
            if (!std::isfinite(number) || number < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
                number >= static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
                return {VariantUVE::MakeIntUVE(0), false};
            }
            const auto truncated = static_cast<std::int64_t>(number);
            return {VariantUVE::MakeIntUVE(truncated), static_cast<double>(truncated) == number};
        }
        case VariantTypeUVE::Float: {
            const std::int64_t* const integer = value.TryGetUVE<std::int64_t>();
            // A 64-bit integer beyond 2^53 has no exact double; say so rather than round silently.
            const bool exact = integer == nullptr || static_cast<std::int64_t>(static_cast<double>(*integer)) == *integer;
            return {VariantUVE::MakeFloatUVE(number), exact};
        }
        default:
            return {VariantUVE::MakeDefaultUVE(target), false};
    }
}

/// Parses text as a number of the target type. Nothing when the text is not a number at all.
[[nodiscard]] std::optional<VariantConversionUVE> ParseNumberUVE(const std::string& text, const VariantTypeUVE target) {
    if (target == VariantTypeUVE::Bool) {
        if (text == "true" || text == "1") {
            return VariantConversionUVE{VariantUVE::MakeBoolUVE(true), true};
        }
        if (text == "false" || text == "0") {
            return VariantConversionUVE{VariantUVE::MakeBoolUVE(false), true};
        }
        return std::nullopt;
    }
    const char* const begin = text.data();
    const char* const end = text.data() + text.size();
    if (target == VariantTypeUVE::Int) {
        std::int64_t parsed = 0;
        const std::from_chars_result result = std::from_chars(begin, end, parsed);
        if (result.ec != std::errc{} || result.ptr != end) {
            return std::nullopt;
        }
        return VariantConversionUVE{VariantUVE::MakeIntUVE(parsed), true};
    }
    double parsed = 0.0;
    const std::from_chars_result result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return VariantConversionUVE{VariantUVE::MakeFloatUVE(parsed), true};
}

/// The components of a vector-like value, padded with `fill`, and how many were really there.
struct ComponentsUVE final {
    std::array<float, 4> values{};
    std::size_t count = 0U;
};

[[nodiscard]] std::optional<ComponentsUVE> ComponentsOfUVE(const VariantUVE& value) noexcept {
    if (const auto* const v2 = value.TryGetUVE<Math::Vector2UVE>(); v2 != nullptr) {
        return ComponentsUVE{{v2->x, v2->y, 0.0F, 0.0F}, 2U};
    }
    if (const auto* const v3 = value.TryGetUVE<Math::Vector3UVE>(); v3 != nullptr) {
        return ComponentsUVE{{v3->x, v3->y, v3->z, 0.0F}, 3U};
    }
    if (const auto* const v4 = value.TryGetUVE<VariantVector4UVE>(); v4 != nullptr) {
        return ComponentsUVE{{v4->x, v4->y, v4->z, v4->w}, 4U};
    }
    if (const auto* const q = value.TryGetUVE<Math::QuaternionUVE>(); q != nullptr) {
        return ComponentsUVE{{q->x, q->y, q->z, q->w}, 4U};
    }
    if (const auto* const c = value.TryGetUVE<VariantColorUVE>(); c != nullptr) {
        return ComponentsUVE{{c->r, c->g, c->b, c->a}, 4U};
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<VariantConversionUVE> ConvertComponentsUVE(const ComponentsUVE& source,
                                                                       const VariantTypeUVE target) {
    VariantUVE result = VariantUVE::MakeDefaultUVE(target);
    std::size_t targetCount = 0U;
    const auto& c = source.values;
    if (auto* const v2 = result.TryGetMutableUVE<Math::Vector2UVE>(); v2 != nullptr) {
        *v2 = {c[0], c[1]};
        targetCount = 2U;
    } else if (auto* const v3 = result.TryGetMutableUVE<Math::Vector3UVE>(); v3 != nullptr) {
        *v3 = {c[0], c[1], c[2]};
        targetCount = 3U;
    } else if (auto* const v4 = result.TryGetMutableUVE<VariantVector4UVE>(); v4 != nullptr) {
        *v4 = {c[0], c[1], c[2], c[3]};
        targetCount = 4U;
    } else if (auto* const q = result.TryGetMutableUVE<Math::QuaternionUVE>(); q != nullptr) {
        // A quaternion widened from fewer than four components has no meaningful w; identity's 1 is
        // the only value that leaves it a rotation rather than a scaled mess.
        *q = {c[0], c[1], c[2], source.count >= 4U ? c[3] : 1.0F};
        targetCount = 4U;
    } else if (auto* const colour = result.TryGetMutableUVE<VariantColorUVE>(); colour != nullptr) {
        // Likewise alpha: a colour made from a vector is opaque unless the vector said otherwise.
        *colour = {c[0], c[1], c[2], source.count >= 4U ? c[3] : 1.0F};
        targetCount = 4U;
    } else {
        return std::nullopt;
    }
    // Dropping a component loses information; gaining one does not.
    bool lossless = true;
    for (std::size_t index = targetCount; index < source.count; ++index) {
        lossless = lossless && c[index] == 0.0F;
    }
    return VariantConversionUVE{std::move(result), lossless};
}

/// Every element of an array-like value as its own VariantUVE, in order.
[[nodiscard]] std::optional<std::vector<VariantUVE>> ElementsOfUVE(const VariantUVE& value) {
    std::vector<VariantUVE> elements;
    const auto append = [&elements](const auto& list, const auto& make) {
        elements.reserve(list.size());
        for (const auto& element : list) {
            elements.push_back(make(element));
        }
    };
    switch (value.GetTypeUVE()) {
        case VariantTypeUVE::Array:
            return *value.TryGetUVE<std::vector<VariantUVE>>();
        case VariantTypeUVE::PackedByteArray:
            append(*value.TryGetUVE<std::vector<std::uint8_t>>(),
                   [](const std::uint8_t byte) { return VariantUVE::MakeIntUVE(byte); });
            return elements;
        case VariantTypeUVE::PackedInt32Array:
            append(*value.TryGetUVE<std::vector<std::int32_t>>(),
                   [](const std::int32_t integer) { return VariantUVE::MakeIntUVE(integer); });
            return elements;
        case VariantTypeUVE::PackedInt64Array:
            append(*value.TryGetUVE<std::vector<std::int64_t>>(),
                   [](const std::int64_t integer) { return VariantUVE::MakeIntUVE(integer); });
            return elements;
        case VariantTypeUVE::PackedFloat32Array:
            append(*value.TryGetUVE<std::vector<float>>(),
                   [](const float real) { return VariantUVE::MakeFloatUVE(static_cast<double>(real)); });
            return elements;
        case VariantTypeUVE::PackedFloat64Array:
            append(*value.TryGetUVE<std::vector<double>>(),
                   [](const double real) { return VariantUVE::MakeFloatUVE(real); });
            return elements;
        case VariantTypeUVE::PackedStringArray:
            append(*value.TryGetUVE<std::vector<std::string>>(), [](const std::string& text) {
                return VariantUVE::MakeTextUVE(VariantTypeUVE::String, text);
            });
            return elements;
        case VariantTypeUVE::PackedVector2Array:
            append(*value.TryGetUVE<std::vector<Math::Vector2UVE>>(), [](const Math::Vector2UVE& vector) {
                VariantUVE element = VariantUVE::MakeDefaultUVE(VariantTypeUVE::Vector2);
                *element.TryGetMutableUVE<Math::Vector2UVE>() = vector;
                return element;
            });
            return elements;
        case VariantTypeUVE::PackedVector3Array:
            append(*value.TryGetUVE<std::vector<Math::Vector3UVE>>(), [](const Math::Vector3UVE& vector) {
                VariantUVE element = VariantUVE::MakeDefaultUVE(VariantTypeUVE::Vector3);
                *element.TryGetMutableUVE<Math::Vector3UVE>() = vector;
                return element;
            });
            return elements;
        case VariantTypeUVE::PackedColorArray:
            append(*value.TryGetUVE<std::vector<VariantColorUVE>>(), [](const VariantColorUVE& colour) {
                VariantUVE element = VariantUVE::MakeDefaultUVE(VariantTypeUVE::Color);
                *element.TryGetMutableUVE<VariantColorUVE>() = colour;
                return element;
            });
            return elements;
        default:
            return std::nullopt;
    }
}

/// The element type a packed array holds, or nothing for a type that is not a packed array.
[[nodiscard]] std::optional<VariantTypeUVE> PackedElementTypeUVE(const VariantTypeUVE type) noexcept {
    switch (type) {
        case VariantTypeUVE::PackedByteArray:
        case VariantTypeUVE::PackedInt32Array:
        case VariantTypeUVE::PackedInt64Array:
            return VariantTypeUVE::Int;
        case VariantTypeUVE::PackedFloat32Array:
        case VariantTypeUVE::PackedFloat64Array:
            return VariantTypeUVE::Float;
        case VariantTypeUVE::PackedStringArray:
            return VariantTypeUVE::String;
        case VariantTypeUVE::PackedVector2Array:
            return VariantTypeUVE::Vector2;
        case VariantTypeUVE::PackedVector3Array:
            return VariantTypeUVE::Vector3;
        case VariantTypeUVE::PackedColorArray:
            return VariantTypeUVE::Color;
        default:
            return std::nullopt;
    }
}

/// Builds an array-like value of `target` from elements, converting each. Nothing when an element
/// has no conversion at all; lossy when any element converted lossily or would not fit.
[[nodiscard]] std::optional<VariantConversionUVE> BuildFromElementsUVE(const std::vector<VariantUVE>& elements,
                                                                       const VariantTypeUVE target) {
    VariantUVE result = VariantUVE::MakeDefaultUVE(target);
    if (target == VariantTypeUVE::Array) {
        *result.TryGetMutableUVE<std::vector<VariantUVE>>() = elements;
        return VariantConversionUVE{std::move(result), true};
    }
    const std::optional<VariantTypeUVE> elementType = PackedElementTypeUVE(target);
    if (!elementType.has_value()) {
        return std::nullopt;
    }
    bool lossless = true;
    for (const VariantUVE& element : elements) {
        const std::optional<VariantConversionUVE> converted = TryConvertVariantUVE(element, *elementType);
        if (!converted.has_value()) {
            return std::nullopt;
        }
        lossless = lossless && converted->lossless;
        const VariantUVE& item = converted->value;
        switch (target) {
            case VariantTypeUVE::PackedByteArray: {
                const std::int64_t integer = *item.TryGetUVE<std::int64_t>();
                lossless = lossless && integer >= 0 && integer <= 255;
                result.TryGetMutableUVE<std::vector<std::uint8_t>>()->push_back(
                    static_cast<std::uint8_t>(integer < 0 ? 0 : (integer > 255 ? 255 : integer)));
                break;
            }
            case VariantTypeUVE::PackedInt32Array: {
                const std::int64_t integer = *item.TryGetUVE<std::int64_t>();
                const bool fits = integer >= std::numeric_limits<std::int32_t>::min() &&
                                  integer <= std::numeric_limits<std::int32_t>::max();
                lossless = lossless && fits;
                result.TryGetMutableUVE<std::vector<std::int32_t>>()->push_back(
                    fits ? static_cast<std::int32_t>(integer) : 0);
                break;
            }
            case VariantTypeUVE::PackedInt64Array:
                result.TryGetMutableUVE<std::vector<std::int64_t>>()->push_back(*item.TryGetUVE<std::int64_t>());
                break;
            case VariantTypeUVE::PackedFloat32Array: {
                const double real = *item.TryGetUVE<double>();
                const auto narrowed = static_cast<float>(real);
                lossless = lossless && static_cast<double>(narrowed) == real;
                result.TryGetMutableUVE<std::vector<float>>()->push_back(narrowed);
                break;
            }
            case VariantTypeUVE::PackedFloat64Array:
                result.TryGetMutableUVE<std::vector<double>>()->push_back(*item.TryGetUVE<double>());
                break;
            case VariantTypeUVE::PackedStringArray:
                result.TryGetMutableUVE<std::vector<std::string>>()->push_back(*item.TryGetUVE<std::string>());
                break;
            case VariantTypeUVE::PackedVector2Array:
                result.TryGetMutableUVE<std::vector<Math::Vector2UVE>>()->push_back(
                    *item.TryGetUVE<Math::Vector2UVE>());
                break;
            case VariantTypeUVE::PackedVector3Array:
                result.TryGetMutableUVE<std::vector<Math::Vector3UVE>>()->push_back(
                    *item.TryGetUVE<Math::Vector3UVE>());
                break;
            case VariantTypeUVE::PackedColorArray:
                result.TryGetMutableUVE<std::vector<VariantColorUVE>>()->push_back(*item.TryGetUVE<VariantColorUVE>());
                break;
            default:
                return std::nullopt;
        }
    }
    return VariantConversionUVE{std::move(result), lossless};
}

[[nodiscard]] bool IsWithinBoundsUVE(const VariantUVE& value, const std::size_t depth) noexcept {
    if (depth > kMaximumVariantDepthUVE) {
        return false;
    }
    return std::visit(
        [depth](const auto& stored) -> bool {
            using StoredT = std::decay_t<decltype(stored)>;
            if constexpr (std::is_same_v<StoredT, double>) {
                return std::isfinite(stored);
            } else if constexpr (std::is_same_v<StoredT, Math::Vector2UVE>) {
                return std::isfinite(stored.x) && std::isfinite(stored.y);
            } else if constexpr (std::is_same_v<StoredT, Math::Vector3UVE>) {
                return std::isfinite(stored.x) && std::isfinite(stored.y) && std::isfinite(stored.z);
            } else if constexpr (std::is_same_v<StoredT, VariantVector4UVE> ||
                                 std::is_same_v<StoredT, Math::QuaternionUVE>) {
                return std::isfinite(stored.x) && std::isfinite(stored.y) && std::isfinite(stored.z) &&
                       std::isfinite(stored.w);
            } else if constexpr (std::is_same_v<StoredT, VariantColorUVE>) {
                return std::isfinite(stored.r) && std::isfinite(stored.g) && std::isfinite(stored.b) &&
                       std::isfinite(stored.a);
            } else if constexpr (std::is_same_v<StoredT, std::vector<float>> ||
                                 std::is_same_v<StoredT, std::vector<double>>) {
                return stored.size() <= kMaximumVariantElementsUVE &&
                       std::all_of(stored.cbegin(), stored.cend(), [](const auto real) { return std::isfinite(real); });
            } else if constexpr (std::is_same_v<StoredT, std::string>) {
                return stored.size() <= kMaximumVariantTextBytesUVE;
            } else if constexpr (std::is_same_v<StoredT, std::vector<VariantUVE>>) {
                if (stored.size() > kMaximumVariantElementsUVE) {
                    return false;
                }
                for (const VariantUVE& element : stored) {
                    if (!IsWithinBoundsUVE(element, depth + 1U)) {
                        return false;
                    }
                }
                return true;
            } else if constexpr (std::is_same_v<StoredT, std::vector<VariantDictionaryEntryUVE>>) {
                if (stored.size() > kMaximumVariantElementsUVE) {
                    return false;
                }
                for (const VariantDictionaryEntryUVE& entry : stored) {
                    if (entry.key.size() > kMaximumVariantTextBytesUVE || !IsWithinBoundsUVE(entry.value, depth + 1U)) {
                        return false;
                    }
                }
                return true;
            } else if constexpr (std::is_same_v<StoredT, std::vector<std::string>>) {
                if (stored.size() > kMaximumVariantElementsUVE) {
                    return false;
                }
                for (const std::string& text : stored) {
                    if (text.size() > kMaximumVariantTextBytesUVE) {
                        return false;
                    }
                }
                return true;
            } else if constexpr (requires { stored.size(); }) {
                return stored.size() <= kMaximumVariantElementsUVE;
            } else {
                return true;
            }
        },
        value.GetStorageUVE());
}

} // namespace

VariantUVE VariantUVE::MakeDefaultUVE(const VariantTypeUVE type) {
    switch (type) {
        case VariantTypeUVE::Bool: return VariantUVE{type, false};
        case VariantTypeUVE::Int: return VariantUVE{type, std::int64_t{0}};
        case VariantTypeUVE::Float: return VariantUVE{type, 0.0};
        case VariantTypeUVE::String:
        case VariantTypeUVE::StringName:
        case VariantTypeUVE::NodePath:
        case VariantTypeUVE::Node: return VariantUVE{type, std::string{}};
        case VariantTypeUVE::Vector2: return VariantUVE{type, Math::Vector2UVE{}};
        case VariantTypeUVE::Vector3: return VariantUVE{type, Math::Vector3UVE{}};
        case VariantTypeUVE::Vector4: return VariantUVE{type, VariantVector4UVE{}};
        case VariantTypeUVE::Quaternion: return VariantUVE{type, Math::QuaternionUVE{}};
        case VariantTypeUVE::Color: return VariantUVE{type, VariantColorUVE{}};
        case VariantTypeUVE::Resource: return VariantUVE{type, std::uint64_t{0}};
        case VariantTypeUVE::Array: return VariantUVE{type, std::vector<VariantUVE>{}};
        case VariantTypeUVE::Dictionary: return VariantUVE{type, std::vector<VariantDictionaryEntryUVE>{}};
        case VariantTypeUVE::PackedByteArray: return VariantUVE{type, std::vector<std::uint8_t>{}};
        case VariantTypeUVE::PackedInt32Array: return VariantUVE{type, std::vector<std::int32_t>{}};
        case VariantTypeUVE::PackedInt64Array: return VariantUVE{type, std::vector<std::int64_t>{}};
        case VariantTypeUVE::PackedFloat32Array: return VariantUVE{type, std::vector<float>{}};
        case VariantTypeUVE::PackedFloat64Array: return VariantUVE{type, std::vector<double>{}};
        case VariantTypeUVE::PackedStringArray: return VariantUVE{type, std::vector<std::string>{}};
        case VariantTypeUVE::PackedVector2Array: return VariantUVE{type, std::vector<Math::Vector2UVE>{}};
        case VariantTypeUVE::PackedVector3Array: return VariantUVE{type, std::vector<Math::Vector3UVE>{}};
        case VariantTypeUVE::PackedColorArray: return VariantUVE{type, std::vector<VariantColorUVE>{}};
    }
    return VariantUVE{};
}

VariantUVE VariantUVE::MakeBoolUVE(const bool value) {
    return VariantUVE{VariantTypeUVE::Bool, value};
}

VariantUVE VariantUVE::MakeIntUVE(const std::int64_t value) {
    return VariantUVE{VariantTypeUVE::Int, value};
}

VariantUVE VariantUVE::MakeFloatUVE(const double value) {
    return VariantUVE{VariantTypeUVE::Float, value};
}

VariantUVE VariantUVE::MakeTextUVE(const VariantTypeUVE type, std::string value) {
    if (!IsTextTypeUVE(type)) {
        return MakeDefaultUVE(type);
    }
    return VariantUVE{type, std::move(value)};
}

bool VariantUVE::operator==(const VariantUVE& other) const {
    return m_type == other.m_type && m_storage == other.m_storage;
}

std::string_view GetVariantTypeNameUVE(const VariantTypeUVE type) noexcept {
    const auto index = static_cast<std::size_t>(type);
    return index < kVariantTypeInfoUVE.size() ? kVariantTypeInfoUVE[index].name : std::string_view{};
}

std::optional<VariantTypeUVE> TryParseVariantTypeNameUVE(const std::string_view name) noexcept {
    for (const VariantTypeInfoUVE& info : kVariantTypeInfoUVE) {
        if (info.name == name) {
            return info.type;
        }
    }
    return std::nullopt;
}

std::string_view GetVariantTypeCategoryUVE(const VariantTypeUVE type) noexcept {
    const auto index = static_cast<std::size_t>(type);
    return index < kVariantTypeInfoUVE.size() ? kVariantTypeInfoUVE[index].category : std::string_view{};
}

const std::array<VariantTypeUVE, kVariantTypeCountUVE>& GetAllVariantTypesUVE() noexcept {
    static const std::array<VariantTypeUVE, kVariantTypeCountUVE> types = [] {
        std::array<VariantTypeUVE, kVariantTypeCountUVE> all{};
        for (std::size_t index = 0; index < kVariantTypeInfoUVE.size(); ++index) {
            all[index] = kVariantTypeInfoUVE[index].type;
        }
        return all;
    }();
    return types;
}

std::optional<VariantConversionUVE> TryConvertVariantUVE(const VariantUVE& value, const VariantTypeUVE target) {
    const VariantTypeUVE source = value.GetTypeUVE();
    if (source == target) {
        return VariantConversionUVE{value, true};
    }
    if (IsNumericTypeUVE(source) && IsNumericTypeUVE(target)) {
        return ConvertNumberUVE(value, target);
    }
    if (IsNumericTypeUVE(source) && IsTextTypeUVE(target)) {
        return VariantConversionUVE{VariantUVE::MakeTextUVE(target, NumberToTextUVE(value)), true};
    }
    if (IsTextTypeUVE(source)) {
        const std::string& text = *value.TryGetUVE<std::string>();
        if (IsTextTypeUVE(target)) {
            return VariantConversionUVE{VariantUVE::MakeTextUVE(target, text), true};
        }
        if (IsNumericTypeUVE(target)) {
            return ParseNumberUVE(text, target);
        }
        return std::nullopt;
    }
    if (const std::optional<ComponentsUVE> components = ComponentsOfUVE(value); components.has_value()) {
        return ConvertComponentsUVE(*components, target);
    }
    if (const std::optional<std::vector<VariantUVE>> elements = ElementsOfUVE(value); elements.has_value()) {
        return BuildFromElementsUVE(*elements, target);
    }
    // Resource, Node references and Dictionaries have no meaningful reading as anything else.
    return std::nullopt;
}

bool IsVariantWithinBoundsUVE(const VariantUVE& value) noexcept {
    return IsWithinBoundsUVE(value, 0U);
}

} // namespace UVE::Core
