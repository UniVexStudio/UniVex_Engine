// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "core_variant.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <system_error>
#include <utility>

namespace UVE::Core {
// Define operations after the recursive dictionary entry is complete (required by libc++).
Variant::Variant() noexcept = default;
Variant::~Variant() = default;
Variant::Variant(const Variant&) = default;
Variant& Variant::operator=(const Variant&) = default;
Variant::Variant(Variant&&) noexcept = default;
Variant& Variant::operator=(Variant&&) noexcept = default;
Variant::Variant(const VariantType type, Storage storage)
    : m_type(type), m_storage(std::move(storage)) {}
namespace {

struct VariantTypeInfo final {
    VariantType type;
    std::string_view name;
    std::string_view category;
};

// One row per type, in enumerator order. The static_assert below is what keeps this table and the
// enumeration from drifting apart when a type is added.
constexpr std::array<VariantTypeInfo, kVariantTypeCount> kVariantTypeInfo{{
    {VariantType::Bool, "bool", "Basic"},
    {VariantType::Int, "int", "Basic"},
    {VariantType::Float, "float", "Basic"},
    {VariantType::String, "String", "Basic"},
    {VariantType::InternedString, "InternedString", "Basic"},
    {VariantType::ObjectPath, "ObjectPath", "Basic"},
    {VariantType::Vector2, "Vector2", "Math"},
    {VariantType::Vector3, "Vector3", "Math"},
    {VariantType::Vector4, "Vector4", "Math"},
    {VariantType::Quaternion, "Quaternion", "Math"},
    {VariantType::Color, "Color", "Color"},
    {VariantType::Resource, "Resource", "Reference"},
    {VariantType::Object, "Object", "Reference"},
    {VariantType::Array, "Array", "Collection"},
    {VariantType::Dictionary, "Dictionary", "Collection"},
    {VariantType::ByteArray, "ByteArray", "Typed Array"},
    {VariantType::Int32Array, "Int32Array", "Typed Array"},
    {VariantType::Int64Array, "Int64Array", "Typed Array"},
    {VariantType::Float32Array, "Float32Array", "Typed Array"},
    {VariantType::Float64Array, "Float64Array", "Typed Array"},
    {VariantType::StringArray, "StringArray", "Typed Array"},
    {VariantType::Vector2Array, "Vector2Array", "Typed Array"},
    {VariantType::Vector3Array, "Vector3Array", "Typed Array"},
    {VariantType::ColorArray, "ColorArray", "Typed Array"},
}};

[[nodiscard]] constexpr bool IsTableInEnumeratorOrder() noexcept {
    for (std::size_t index = 0; index < kVariantTypeInfo.size(); ++index) {
        if (static_cast<std::size_t>(kVariantTypeInfo[index].type) != index) {
            return false;
        }
    }
    return true;
}
static_assert(IsTableInEnumeratorOrder(), "kVariantTypeInfo must list every type in enumerator order.");

[[nodiscard]] bool IsTextType(const VariantType type) noexcept {
    return type == VariantType::String || type == VariantType::InternedString ||
           type == VariantType::ObjectPath || type == VariantType::Object;
}

[[nodiscard]] bool IsNumericType(const VariantType type) noexcept {
    return type == VariantType::Bool || type == VariantType::Int || type == VariantType::Float;
}

/// A value's number, whatever numeric type it is.
[[nodiscard]] double AsNumber(const Variant& value) noexcept {
    if (const bool* const flag = value.TryGet<bool>(); flag != nullptr) {
        return *flag ? 1.0 : 0.0;
    }
    if (const std::int64_t* const integer = value.TryGet<std::int64_t>(); integer != nullptr) {
        return static_cast<double>(*integer);
    }
    if (const double* const real = value.TryGet<double>(); real != nullptr) {
        return *real;
    }
    return 0.0;
}

[[nodiscard]] std::string NumberToText(const Variant& value) {
    if (const bool* const flag = value.TryGet<bool>(); flag != nullptr) {
        return *flag ? "true" : "false";
    }
    if (const std::int64_t* const integer = value.TryGet<std::int64_t>(); integer != nullptr) {
        return std::to_string(*integer);
    }
    // Shortest text that parses back to the same double, so a Float -> String -> Float round trip
    // is exact rather than drifting in the last digits the way a fixed precision would.
    std::array<char, 64> buffer{};
    const double real = AsNumber(value);
    const std::to_chars_result result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), real);
    return result.ec == std::errc{} ? std::string(buffer.data(), result.ptr) : std::string{};
}

/// Converts between numeric types. Lossless exactly when the target can represent the number.
[[nodiscard]] VariantConversion ConvertNumber(const Variant& value, const VariantType target) {
    const double number = AsNumber(value);
    switch (target) {
        case VariantType::Bool:
            return {Variant::MakeBool(number != 0.0), number == 0.0 || number == 1.0};
        case VariantType::Int: {
            if (!std::isfinite(number) || number < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
                number >= static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
                return {Variant::MakeInt(0), false};
            }
            const auto truncated = static_cast<std::int64_t>(number);
            return {Variant::MakeInt(truncated), static_cast<double>(truncated) == number};
        }
        case VariantType::Float: {
            const std::int64_t* const integer = value.TryGet<std::int64_t>();
            // A 64-bit integer beyond 2^53 has no exact double; say so rather than round silently.
            const bool exact = integer == nullptr || static_cast<std::int64_t>(static_cast<double>(*integer)) == *integer;
            return {Variant::MakeFloat(number), exact};
        }
        default:
            return {Variant::MakeDefault(target), false};
    }
}

/// Parses text as a number of the target type. Nothing when the text is not a number at all.
[[nodiscard]] std::optional<VariantConversion> ParseNumber(const std::string& text, const VariantType target) {
    if (target == VariantType::Bool) {
        if (text == "true" || text == "1") {
            return VariantConversion{Variant::MakeBool(true), true};
        }
        if (text == "false" || text == "0") {
            return VariantConversion{Variant::MakeBool(false), true};
        }
        return std::nullopt;
    }
    const char* const begin = text.data();
    const char* const end = text.data() + text.size();
    if (target == VariantType::Int) {
        std::int64_t parsed = 0;
        const std::from_chars_result result = std::from_chars(begin, end, parsed);
        if (result.ec != std::errc{} || result.ptr != end) {
            return std::nullopt;
        }
        return VariantConversion{Variant::MakeInt(parsed), true};
    }
    double parsed = 0.0;
    const std::from_chars_result result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return VariantConversion{Variant::MakeFloat(parsed), true};
}

/// The components of a vector-like value, padded with `fill`, and how many were really there.
struct Components final {
    std::array<float, 4> values{};
    std::size_t count = 0U;
};

[[nodiscard]] std::optional<Components> ComponentsOf(const Variant& value) noexcept {
    if (const auto* const v2 = value.TryGet<Math::Vector2>(); v2 != nullptr) {
        return Components{{v2->x, v2->y, 0.0F, 0.0F}, 2U};
    }
    if (const auto* const v3 = value.TryGet<Math::Vector3>(); v3 != nullptr) {
        return Components{{v3->x, v3->y, v3->z, 0.0F}, 3U};
    }
    if (const auto* const v4 = value.TryGet<VariantVector4>(); v4 != nullptr) {
        return Components{{v4->x, v4->y, v4->z, v4->w}, 4U};
    }
    if (const auto* const q = value.TryGet<Math::Quaternion>(); q != nullptr) {
        return Components{{q->x, q->y, q->z, q->w}, 4U};
    }
    if (const auto* const c = value.TryGet<VariantColor>(); c != nullptr) {
        return Components{{c->r, c->g, c->b, c->a}, 4U};
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<VariantConversion> ConvertComponents(const Components& source,
                                                                       const VariantType target) {
    Variant result = Variant::MakeDefault(target);
    std::size_t targetCount = 0U;
    const auto& c = source.values;
    if (auto* const v2 = result.TryGetMutable<Math::Vector2>(); v2 != nullptr) {
        *v2 = {c[0], c[1]};
        targetCount = 2U;
    } else if (auto* const v3 = result.TryGetMutable<Math::Vector3>(); v3 != nullptr) {
        *v3 = {c[0], c[1], c[2]};
        targetCount = 3U;
    } else if (auto* const v4 = result.TryGetMutable<VariantVector4>(); v4 != nullptr) {
        *v4 = {c[0], c[1], c[2], c[3]};
        targetCount = 4U;
    } else if (auto* const q = result.TryGetMutable<Math::Quaternion>(); q != nullptr) {
        // A quaternion widened from fewer than four components has no meaningful w; identity's 1 is
        // the only value that leaves it a rotation rather than a scaled mess.
        *q = {c[0], c[1], c[2], source.count >= 4U ? c[3] : 1.0F};
        targetCount = 4U;
    } else if (auto* const colour = result.TryGetMutable<VariantColor>(); colour != nullptr) {
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
    return VariantConversion{std::move(result), lossless};
}

/// Every element of an array-like value as its own Variant, in order.
[[nodiscard]] std::optional<std::vector<Variant>> ElementsOf(const Variant& value) {
    std::vector<Variant> elements;
    const auto append = [&elements](const auto& list, const auto& make) {
        elements.reserve(list.size());
        for (const auto& element : list) {
            elements.push_back(make(element));
        }
    };
    switch (value.GetType()) {
        case VariantType::Array:
            return *value.TryGet<std::vector<Variant>>();
        case VariantType::ByteArray:
            append(*value.TryGet<std::vector<std::uint8_t>>(),
                   [](const std::uint8_t byte) { return Variant::MakeInt(byte); });
            return elements;
        case VariantType::Int32Array:
            append(*value.TryGet<std::vector<std::int32_t>>(),
                   [](const std::int32_t integer) { return Variant::MakeInt(integer); });
            return elements;
        case VariantType::Int64Array:
            append(*value.TryGet<std::vector<std::int64_t>>(),
                   [](const std::int64_t integer) { return Variant::MakeInt(integer); });
            return elements;
        case VariantType::Float32Array:
            append(*value.TryGet<std::vector<float>>(),
                   [](const float real) { return Variant::MakeFloat(static_cast<double>(real)); });
            return elements;
        case VariantType::Float64Array:
            append(*value.TryGet<std::vector<double>>(),
                   [](const double real) { return Variant::MakeFloat(real); });
            return elements;
        case VariantType::StringArray:
            append(*value.TryGet<std::vector<std::string>>(), [](const std::string& text) {
                return Variant::MakeText(VariantType::String, text);
            });
            return elements;
        case VariantType::Vector2Array:
            append(*value.TryGet<std::vector<Math::Vector2>>(), [](const Math::Vector2& vector) {
                Variant element = Variant::MakeDefault(VariantType::Vector2);
                *element.TryGetMutable<Math::Vector2>() = vector;
                return element;
            });
            return elements;
        case VariantType::Vector3Array:
            append(*value.TryGet<std::vector<Math::Vector3>>(), [](const Math::Vector3& vector) {
                Variant element = Variant::MakeDefault(VariantType::Vector3);
                *element.TryGetMutable<Math::Vector3>() = vector;
                return element;
            });
            return elements;
        case VariantType::ColorArray:
            append(*value.TryGet<std::vector<VariantColor>>(), [](const VariantColor& colour) {
                Variant element = Variant::MakeDefault(VariantType::Color);
                *element.TryGetMutable<VariantColor>() = colour;
                return element;
            });
            return elements;
        default:
            return std::nullopt;
    }
}

/// The element type a packed array holds, or nothing for a type that is not a packed array.
[[nodiscard]] std::optional<VariantType> PackedElementType(const VariantType type) noexcept {
    switch (type) {
        case VariantType::ByteArray:
        case VariantType::Int32Array:
        case VariantType::Int64Array:
            return VariantType::Int;
        case VariantType::Float32Array:
        case VariantType::Float64Array:
            return VariantType::Float;
        case VariantType::StringArray:
            return VariantType::String;
        case VariantType::Vector2Array:
            return VariantType::Vector2;
        case VariantType::Vector3Array:
            return VariantType::Vector3;
        case VariantType::ColorArray:
            return VariantType::Color;
        default:
            return std::nullopt;
    }
}

/// Builds an array-like value of `target` from elements, converting each. Nothing when an element
/// has no conversion at all; lossy when any element converted lossily or would not fit.
[[nodiscard]] std::optional<VariantConversion> BuildFromElements(const std::vector<Variant>& elements,
                                                                       const VariantType target) {
    Variant result = Variant::MakeDefault(target);
    if (target == VariantType::Array) {
        *result.TryGetMutable<std::vector<Variant>>() = elements;
        return VariantConversion{std::move(result), true};
    }
    const std::optional<VariantType> elementType = PackedElementType(target);
    if (!elementType.has_value()) {
        return std::nullopt;
    }
    bool lossless = true;
    for (const Variant& element : elements) {
        const std::optional<VariantConversion> converted = TryConvertVariant(element, *elementType);
        if (!converted.has_value()) {
            return std::nullopt;
        }
        lossless = lossless && converted->lossless;
        const Variant& item = converted->value;
        switch (target) {
            case VariantType::ByteArray: {
                const std::int64_t integer = *item.TryGet<std::int64_t>();
                lossless = lossless && integer >= 0 && integer <= 255;
                result.TryGetMutable<std::vector<std::uint8_t>>()->push_back(
                    static_cast<std::uint8_t>(integer < 0 ? 0 : (integer > 255 ? 255 : integer)));
                break;
            }
            case VariantType::Int32Array: {
                const std::int64_t integer = *item.TryGet<std::int64_t>();
                const bool fits = integer >= std::numeric_limits<std::int32_t>::min() &&
                                  integer <= std::numeric_limits<std::int32_t>::max();
                lossless = lossless && fits;
                result.TryGetMutable<std::vector<std::int32_t>>()->push_back(
                    fits ? static_cast<std::int32_t>(integer) : 0);
                break;
            }
            case VariantType::Int64Array:
                result.TryGetMutable<std::vector<std::int64_t>>()->push_back(*item.TryGet<std::int64_t>());
                break;
            case VariantType::Float32Array: {
                const double real = *item.TryGet<double>();
                const auto narrowed = static_cast<float>(real);
                lossless = lossless && static_cast<double>(narrowed) == real;
                result.TryGetMutable<std::vector<float>>()->push_back(narrowed);
                break;
            }
            case VariantType::Float64Array:
                result.TryGetMutable<std::vector<double>>()->push_back(*item.TryGet<double>());
                break;
            case VariantType::StringArray:
                result.TryGetMutable<std::vector<std::string>>()->push_back(*item.TryGet<std::string>());
                break;
            case VariantType::Vector2Array:
                result.TryGetMutable<std::vector<Math::Vector2>>()->push_back(
                    *item.TryGet<Math::Vector2>());
                break;
            case VariantType::Vector3Array:
                result.TryGetMutable<std::vector<Math::Vector3>>()->push_back(
                    *item.TryGet<Math::Vector3>());
                break;
            case VariantType::ColorArray:
                result.TryGetMutable<std::vector<VariantColor>>()->push_back(*item.TryGet<VariantColor>());
                break;
            default:
                return std::nullopt;
        }
    }
    return VariantConversion{std::move(result), lossless};
}

[[nodiscard]] bool IsWithinBounds(const Variant& value, const std::size_t depth) noexcept {
    if (depth > kMaximumVariantDepth) {
        return false;
    }
    return std::visit(
        [depth](const auto& stored) -> bool {
            using StoredT = std::decay_t<decltype(stored)>;
            if constexpr (std::is_same_v<StoredT, double>) {
                return std::isfinite(stored);
            } else if constexpr (std::is_same_v<StoredT, Math::Vector2>) {
                return std::isfinite(stored.x) && std::isfinite(stored.y);
            } else if constexpr (std::is_same_v<StoredT, Math::Vector3>) {
                return std::isfinite(stored.x) && std::isfinite(stored.y) && std::isfinite(stored.z);
            } else if constexpr (std::is_same_v<StoredT, VariantVector4> ||
                                 std::is_same_v<StoredT, Math::Quaternion>) {
                return std::isfinite(stored.x) && std::isfinite(stored.y) && std::isfinite(stored.z) &&
                       std::isfinite(stored.w);
            } else if constexpr (std::is_same_v<StoredT, VariantColor>) {
                return std::isfinite(stored.r) && std::isfinite(stored.g) && std::isfinite(stored.b) &&
                       std::isfinite(stored.a);
            } else if constexpr (std::is_same_v<StoredT, std::vector<float>> ||
                                 std::is_same_v<StoredT, std::vector<double>>) {
                return stored.size() <= kMaximumVariantElements &&
                       std::all_of(stored.cbegin(), stored.cend(), [](const auto real) { return std::isfinite(real); });
            } else if constexpr (std::is_same_v<StoredT, std::string>) {
                return stored.size() <= kMaximumVariantTextBytes;
            } else if constexpr (std::is_same_v<StoredT, std::vector<Variant>>) {
                if (stored.size() > kMaximumVariantElements) {
                    return false;
                }
                for (const Variant& element : stored) {
                    if (!IsWithinBounds(element, depth + 1U)) {
                        return false;
                    }
                }
                return true;
            } else if constexpr (std::is_same_v<StoredT, std::vector<VariantDictionaryEntry>>) {
                if (stored.size() > kMaximumVariantElements) {
                    return false;
                }
                for (const VariantDictionaryEntry& entry : stored) {
                    if (entry.key.size() > kMaximumVariantTextBytes || !IsWithinBounds(entry.value, depth + 1U)) {
                        return false;
                    }
                }
                return true;
            } else if constexpr (std::is_same_v<StoredT, std::vector<std::string>>) {
                if (stored.size() > kMaximumVariantElements) {
                    return false;
                }
                for (const std::string& text : stored) {
                    if (text.size() > kMaximumVariantTextBytes) {
                        return false;
                    }
                }
                return true;
            } else if constexpr (requires { stored.size(); }) {
                return stored.size() <= kMaximumVariantElements;
            } else {
                return true;
            }
        },
        value.GetStorage());
}

} // namespace

Variant Variant::MakeDefault(const VariantType type) {
    switch (type) {
        case VariantType::Bool: return Variant{type, false};
        case VariantType::Int: return Variant{type, std::int64_t{0}};
        case VariantType::Float: return Variant{type, 0.0};
        case VariantType::String:
        case VariantType::InternedString:
        case VariantType::ObjectPath:
        case VariantType::Object: return Variant{type, std::string{}};
        case VariantType::Vector2: return Variant{type, Math::Vector2{}};
        case VariantType::Vector3: return Variant{type, Math::Vector3{}};
        case VariantType::Vector4: return Variant{type, VariantVector4{}};
        case VariantType::Quaternion: return Variant{type, Math::Quaternion{}};
        case VariantType::Color: return Variant{type, VariantColor{}};
        case VariantType::Resource: return Variant{type, std::uint64_t{0}};
        case VariantType::Array: return Variant{type, std::vector<Variant>{}};
        case VariantType::Dictionary: return Variant{type, std::vector<VariantDictionaryEntry>{}};
        case VariantType::ByteArray: return Variant{type, std::vector<std::uint8_t>{}};
        case VariantType::Int32Array: return Variant{type, std::vector<std::int32_t>{}};
        case VariantType::Int64Array: return Variant{type, std::vector<std::int64_t>{}};
        case VariantType::Float32Array: return Variant{type, std::vector<float>{}};
        case VariantType::Float64Array: return Variant{type, std::vector<double>{}};
        case VariantType::StringArray: return Variant{type, std::vector<std::string>{}};
        case VariantType::Vector2Array: return Variant{type, std::vector<Math::Vector2>{}};
        case VariantType::Vector3Array: return Variant{type, std::vector<Math::Vector3>{}};
        case VariantType::ColorArray: return Variant{type, std::vector<VariantColor>{}};
    }
    return Variant{};
}

Variant Variant::MakeBool(const bool value) {
    return Variant{VariantType::Bool, value};
}

Variant Variant::MakeInt(const std::int64_t value) {
    return Variant{VariantType::Int, value};
}

Variant Variant::MakeFloat(const double value) {
    return Variant{VariantType::Float, value};
}

Variant Variant::MakeText(const VariantType type, std::string value) {
    if (!IsTextType(type)) {
        return MakeDefault(type);
    }
    return Variant{type, std::move(value)};
}

bool Variant::operator==(const Variant& other) const {
    return m_type == other.m_type && m_storage == other.m_storage;
}

std::string_view GetVariantTypeName(const VariantType type) noexcept {
    const auto index = static_cast<std::size_t>(type);
    return index < kVariantTypeInfo.size() ? kVariantTypeInfo[index].name : std::string_view{};
}

std::optional<VariantType> TryParseVariantTypeName(const std::string_view name) noexcept {
    // A saved document names a variant's type (see GetVariantTypeName), so every name this
    // vocabulary has retired stays readable. Writing always uses the current name; this table is
    // read-only history, and it only ever grows.
    constexpr std::array<std::pair<std::string_view, VariantType>, 12U> kRetiredNames{{
        // The object pass: the word "Node" left the engine's own types.
        {"NodePath", VariantType::ObjectPath},
        {"Node", VariantType::Object},
        // The variant pass: "Packed*Array" is another engine's family verbatim, all nine of it,
        // and "StringName" is its name for an interned string. The element type carries the
        // meaning on its own - ByteArray next to Array says what it holds without a prefix.
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
    for (const auto& [retired, type] : kRetiredNames) {
        if (retired == name) {
            return type;
        }
    }
    for (const VariantTypeInfo& info : kVariantTypeInfo) {
        if (info.name == name) {
            return info.type;
        }
    }
    return std::nullopt;
}

std::string_view GetVariantTypeCategory(const VariantType type) noexcept {
    const auto index = static_cast<std::size_t>(type);
    return index < kVariantTypeInfo.size() ? kVariantTypeInfo[index].category : std::string_view{};
}

const std::array<VariantType, kVariantTypeCount>& GetAllVariantTypes() noexcept {
    static const std::array<VariantType, kVariantTypeCount> types = [] {
        std::array<VariantType, kVariantTypeCount> all{};
        for (std::size_t index = 0; index < kVariantTypeInfo.size(); ++index) {
            all[index] = kVariantTypeInfo[index].type;
        }
        return all;
    }();
    return types;
}

std::optional<VariantConversion> TryConvertVariant(const Variant& value, const VariantType target) {
    const VariantType source = value.GetType();
    if (source == target) {
        return VariantConversion{value, true};
    }
    if (IsNumericType(source) && IsNumericType(target)) {
        return ConvertNumber(value, target);
    }
    if (IsNumericType(source) && IsTextType(target)) {
        return VariantConversion{Variant::MakeText(target, NumberToText(value)), true};
    }
    if (IsTextType(source)) {
        const std::string& text = *value.TryGet<std::string>();
        if (IsTextType(target)) {
            return VariantConversion{Variant::MakeText(target, text), true};
        }
        if (IsNumericType(target)) {
            return ParseNumber(text, target);
        }
        return std::nullopt;
    }
    if (const std::optional<Components> components = ComponentsOf(value); components.has_value()) {
        return ConvertComponents(*components, target);
    }
    if (const std::optional<std::vector<Variant>> elements = ElementsOf(value); elements.has_value()) {
        return BuildFromElements(*elements, target);
    }
    // Resource, Object references and Dictionaries have no meaningful reading as anything else.
    return std::nullopt;
}

bool IsVariantWithinBounds(const Variant& value) noexcept {
    return IsWithinBounds(value, 0U);
}

} // namespace UVE::Core
