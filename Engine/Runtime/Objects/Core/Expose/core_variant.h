// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "quaternion.h"
#include "vector2.h"
#include "vector3.h"

namespace UVE::Core {

/// Every value type a Variant can hold. The engine had three partial versions of this - the
/// data table's (bool/int/double/string), and two in scripting (floats, vectors, entity handles) -
/// none of which could carry an object path, a colour with alpha, an array or a packed array. This is
/// the one that can, so authored per-instance data does not have to be squeezed into a string.
///
/// The order is the order the type picker shows within each category. Values are persisted by
/// name (see GetVariantTypeName), never by this number, so reordering here cannot corrupt a
/// saved scene.
enum class VariantType : std::uint8_t {
    Bool = 0,
    Int,
    Float,
    String,
    InternedString,
    ObjectPath,
    Vector2,
    Vector3,
    Vector4,
    Quaternion,
    Color,
    Resource,
    Object,
    Array,
    Dictionary,
    ByteArray,
    Int32Array,
    Int64Array,
    Float32Array,
    Float64Array,
    StringArray,
    Vector2Array,
    Vector3Array,
    ColorArray,
};

inline constexpr std::size_t kVariantTypeCount = static_cast<std::size_t>(VariantType::ColorArray) + 1U;

/// A four-component vector. Kept here rather than in the Math module because nothing in the engine
/// computes with one yet - it exists to be stored and edited - and a Math type should arrive with
/// the operations that justify it, not ahead of them.
struct VariantVector4 final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 0.0F;

    [[nodiscard]] bool operator==(const VariantVector4&) const = default;
};

/// A colour with alpha. The engine's other colours are Vector3 (no alpha) because they feed
/// lighting and UI that have their own alpha fields; a stored colour has nowhere else to keep one.
struct VariantColor final {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 1.0F;

    [[nodiscard]] bool operator==(const VariantColor&) const = default;
};

class Variant;
struct VariantDictionaryEntry; // Defined after Variant, which it holds by value.

/// A typed value: one of VariantType, with storage that matches it.
///
/// The type is stored explicitly, not inferred from the storage, because several types share a
/// representation and must stay distinct: String, InternedString, ObjectPath and Object are all text; an
/// Int and a Resource are both integers. Losing that distinction would turn an object reference into
/// an ordinary string the moment it was saved.
///
/// Built only through MakeDefault or the typed Make* helpers below, so the type and the storage
/// can never disagree.
class Variant final {
public:
    using Storage =
        std::variant<bool, std::int64_t, double, std::string, Math::Vector2, Math::Vector3, VariantVector4,
                     Math::Quaternion, VariantColor, std::uint64_t, std::vector<Variant>,
                     std::vector<VariantDictionaryEntry>, std::vector<std::uint8_t>, std::vector<std::int32_t>,
                     std::vector<std::int64_t>, std::vector<float>, std::vector<double>, std::vector<std::string>,
                     std::vector<Math::Vector2>, std::vector<Math::Vector3>, std::vector<VariantColor>>;

    /// A false Bool: the default of the default type.
    Variant() noexcept;
    ~Variant();
    Variant(const Variant&);
    Variant& operator=(const Variant&);
    Variant(Variant&&) noexcept;
    Variant& operator=(Variant&&) noexcept;

    /// The value a freshly created property of `type` holds - zero, empty, or identity.
    [[nodiscard]] static Variant MakeDefault(VariantType type);

    [[nodiscard]] static Variant MakeBool(bool value);
    [[nodiscard]] static Variant MakeInt(std::int64_t value);
    [[nodiscard]] static Variant MakeFloat(double value);
    /// String, InternedString, ObjectPath or Object. Any other type yields the default of `type`.
    [[nodiscard]] static Variant MakeText(VariantType type, std::string value);

    [[nodiscard]] VariantType GetType() const noexcept { return m_type; }
    [[nodiscard]] const Storage& GetStorage() const noexcept { return m_storage; }

    /// The stored value as `T`, or null when this type is not stored as `T`.
    template <typename T>
    [[nodiscard]] const T* TryGet() const noexcept {
        return std::get_if<T>(&m_storage);
    }
    template <typename T>
    [[nodiscard]] T* TryGetMutable() noexcept {
        return std::get_if<T>(&m_storage);
    }

    /// Calls `visitor` with a mutable reference to the stored value. The alternative cannot change
    /// through it - `visitor` receives the current alternative's type and can only assign within it -
    /// so the stored type and the storage still cannot disagree. This is how a decoder or an editor
    /// fills in a value built by MakeDefault without a setter per type.
    template <typename VisitorT>
    void VisitMutable(VisitorT&& visitor) {
        std::visit(std::forward<VisitorT>(visitor), m_storage);
    }

    /// Same type and same value, compared recursively. Floating-point values compare exactly: this
    /// answers "did the author change it", not "is it approximately the same number".
    [[nodiscard]] bool operator==(const Variant& other) const;

private:
    Variant(VariantType type, Storage storage);

    VariantType m_type = VariantType::Bool;
    Storage m_storage;
};

/// One entry of a Dictionary. Keys are strings: a key that is itself an arbitrary value buys
/// nothing for authored data and makes every lookup, every file and every editor row ambiguous.
/// Entries keep their authored order, which is what an inspector shows and what diffs cleanly.
struct VariantDictionaryEntry final {
    std::string key;
    Variant value;

    [[nodiscard]] bool operator==(const VariantDictionaryEntry& other) const {
        return key == other.key && value == other.value;
    }
};

/// The persisted and displayed name of a type ("Vector3", "ByteArray"). Scene files store these, so
/// a rename is a load-time alias, never a silent break: writing always uses the current name, and
/// every name this vocabulary has retired stays readable through TryParseVariantTypeName below.
[[nodiscard]] std::string_view GetVariantTypeName(VariantType type) noexcept;

/// The inverse of GetVariantTypeName, or nothing for a name no type answers to. Also accepts the
/// retired names, so a document written before a rename still loads.
[[nodiscard]] std::optional<VariantType> TryParseVariantTypeName(std::string_view name) noexcept;

/// The type picker's grouping: "Basic", "Math", "Color", "Reference", "Collection" or
/// "Typed Array". Grouping is what makes a two-dozen-entry list scannable.
[[nodiscard]] std::string_view GetVariantTypeCategory(VariantType type) noexcept;

/// Every type, in picker order.
[[nodiscard]] const std::array<VariantType, kVariantTypeCount>& GetAllVariantTypes() noexcept;

/// The outcome of converting a value to another type.
struct VariantConversion final {
    Variant value;
    /// False when information was dropped on the way - a fraction truncated, a component
    /// discarded, text that did not parse. The editor warns before applying a lossy conversion.
    bool lossless = true;
};

/// Converts `value` to `target`, keeping as much as the target can represent: numbers among
/// themselves and to and from text, text types among themselves, vectors between sizes and to and
/// from colours and quaternions, and arrays to and from packed arrays element by element. Returns
/// nothing when there is no meaningful conversion (a colour to an object path); the editor then offers
/// the target type's default instead, and says so.
[[nodiscard]] std::optional<VariantConversion> TryConvertVariant(const Variant& value,
                                                                       VariantType target);

/// Limits that keep a hostile or corrupt scene file from making one value unbounded. Checked on
/// load and on authoring, so nothing past them can be created or restored.
inline constexpr std::size_t kMaximumVariantDepth = 16U;
inline constexpr std::size_t kMaximumVariantElements = 65536U;
inline constexpr std::size_t kMaximumVariantTextBytes = 65536U;

/// True when `value`, including everything nested inside it, stays within the limits above and every
/// floating-point number in it is finite. Finiteness is part of "in bounds" because scene files are
/// JSON, which has no NaN or infinity: such a value would save as null and make the next load fail.
[[nodiscard]] bool IsVariantWithinBounds(const Variant& value) noexcept;

} // namespace UVE::Core
