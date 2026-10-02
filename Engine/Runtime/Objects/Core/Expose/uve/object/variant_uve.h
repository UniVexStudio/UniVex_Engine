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

#include "uve/math/quaternion_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Core {

/// Every value type a VariantUVE can hold. The engine had three partial versions of this - the
/// data table's (bool/int/double/string), and two in scripting (floats, vectors, entity handles) -
/// none of which could carry a node path, a colour with alpha, an array or a packed array. This is
/// the one that can, so authored per-instance data does not have to be squeezed into a string.
///
/// The order is the order the type picker shows within each category. Values are persisted by
/// name (see GetVariantTypeNameUVE), never by this number, so reordering here cannot corrupt a
/// saved scene.
enum class VariantTypeUVE : std::uint8_t {
    Bool = 0,
    Int,
    Float,
    String,
    StringName,
    NodePath,
    Vector2,
    Vector3,
    Vector4,
    Quaternion,
    Color,
    Resource,
    Node,
    Array,
    Dictionary,
    PackedByteArray,
    PackedInt32Array,
    PackedInt64Array,
    PackedFloat32Array,
    PackedFloat64Array,
    PackedStringArray,
    PackedVector2Array,
    PackedVector3Array,
    PackedColorArray,
};

inline constexpr std::size_t kVariantTypeCountUVE = static_cast<std::size_t>(VariantTypeUVE::PackedColorArray) + 1U;

/// A four-component vector. Kept here rather than in the Math module because nothing in the engine
/// computes with one yet - it exists to be stored and edited - and a Math type should arrive with
/// the operations that justify it, not ahead of them.
struct VariantVector4UVE final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 0.0F;

    [[nodiscard]] bool operator==(const VariantVector4UVE&) const = default;
};

/// A colour with alpha. The engine's other colours are Vector3 (no alpha) because they feed
/// lighting and UI that have their own alpha fields; a stored colour has nowhere else to keep one.
struct VariantColorUVE final {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 1.0F;

    [[nodiscard]] bool operator==(const VariantColorUVE&) const = default;
};

class VariantUVE;
struct VariantDictionaryEntryUVE; // Defined after VariantUVE, which it holds by value.

/// A typed value: one of VariantTypeUVE, with storage that matches it.
///
/// The type is stored explicitly, not inferred from the storage, because several types share a
/// representation and must stay distinct: String, StringName, NodePath and Node are all text; an
/// Int and a Resource are both integers. Losing that distinction would turn a node reference into
/// an ordinary string the moment it was saved.
///
/// Built only through MakeDefaultUVE or the typed Make* helpers below, so the type and the storage
/// can never disagree.
class VariantUVE final {
public:
    using StorageUVE =
        std::variant<bool, std::int64_t, double, std::string, Math::Vector2UVE, Math::Vector3UVE, VariantVector4UVE,
                     Math::QuaternionUVE, VariantColorUVE, std::uint64_t, std::vector<VariantUVE>,
                     std::vector<VariantDictionaryEntryUVE>, std::vector<std::uint8_t>, std::vector<std::int32_t>,
                     std::vector<std::int64_t>, std::vector<float>, std::vector<double>, std::vector<std::string>,
                     std::vector<Math::Vector2UVE>, std::vector<Math::Vector3UVE>, std::vector<VariantColorUVE>>;

    /// A false Bool: the default of the default type.
    VariantUVE() = default;

    /// The value a freshly created property of `type` holds - zero, empty, or identity.
    [[nodiscard]] static VariantUVE MakeDefaultUVE(VariantTypeUVE type);

    [[nodiscard]] static VariantUVE MakeBoolUVE(bool value);
    [[nodiscard]] static VariantUVE MakeIntUVE(std::int64_t value);
    [[nodiscard]] static VariantUVE MakeFloatUVE(double value);
    /// String, StringName, NodePath or Node. Any other type yields the default of `type`.
    [[nodiscard]] static VariantUVE MakeTextUVE(VariantTypeUVE type, std::string value);

    [[nodiscard]] VariantTypeUVE GetTypeUVE() const noexcept { return m_type; }
    [[nodiscard]] const StorageUVE& GetStorageUVE() const noexcept { return m_storage; }

    /// The stored value as `T`, or null when this type is not stored as `T`.
    template <typename T>
    [[nodiscard]] const T* TryGetUVE() const noexcept {
        return std::get_if<T>(&m_storage);
    }
    template <typename T>
    [[nodiscard]] T* TryGetMutableUVE() noexcept {
        return std::get_if<T>(&m_storage);
    }

    /// Calls `visitor` with a mutable reference to the stored value. The alternative cannot change
    /// through it - `visitor` receives the current alternative's type and can only assign within it -
    /// so the stored type and the storage still cannot disagree. This is how a decoder or an editor
    /// fills in a value built by MakeDefaultUVE without a setter per type.
    template <typename VisitorT>
    void VisitMutableUVE(VisitorT&& visitor) {
        std::visit(std::forward<VisitorT>(visitor), m_storage);
    }

    /// Same type and same value, compared recursively. Floating-point values compare exactly: this
    /// answers "did the author change it", not "is it approximately the same number".
    [[nodiscard]] bool operator==(const VariantUVE& other) const;

private:
    VariantUVE(const VariantTypeUVE type, StorageUVE storage) : m_type(type), m_storage(std::move(storage)) {}

    VariantTypeUVE m_type = VariantTypeUVE::Bool;
    StorageUVE m_storage{false};
};

/// One entry of a Dictionary. Keys are strings: a key that is itself an arbitrary value buys
/// nothing for authored data and makes every lookup, every file and every editor row ambiguous.
/// Entries keep their authored order, which is what an inspector shows and what diffs cleanly.
struct VariantDictionaryEntryUVE final {
    std::string key;
    VariantUVE value;

    [[nodiscard]] bool operator==(const VariantDictionaryEntryUVE& other) const {
        return key == other.key && value == other.value;
    }
};

/// The persisted and displayed name of a type ("Vector3", "PackedByteArray"). Stable: scene files
/// store these, so a name, once shipped, is never changed.
[[nodiscard]] std::string_view GetVariantTypeNameUVE(VariantTypeUVE type) noexcept;

/// The inverse of GetVariantTypeNameUVE, or nothing for a name no type answers to.
[[nodiscard]] std::optional<VariantTypeUVE> TryParseVariantTypeNameUVE(std::string_view name) noexcept;

/// The type picker's grouping: "Basic", "Math", "Color", "Reference", "Collection" or
/// "Packed Array". Grouping is what makes a two-dozen-entry list scannable.
[[nodiscard]] std::string_view GetVariantTypeCategoryUVE(VariantTypeUVE type) noexcept;

/// Every type, in picker order.
[[nodiscard]] const std::array<VariantTypeUVE, kVariantTypeCountUVE>& GetAllVariantTypesUVE() noexcept;

/// The outcome of converting a value to another type.
struct VariantConversionUVE final {
    VariantUVE value;
    /// False when information was dropped on the way - a fraction truncated, a component
    /// discarded, text that did not parse. The editor warns before applying a lossy conversion.
    bool lossless = true;
};

/// Converts `value` to `target`, keeping as much as the target can represent: numbers among
/// themselves and to and from text, text types among themselves, vectors between sizes and to and
/// from colours and quaternions, and arrays to and from packed arrays element by element. Returns
/// nothing when there is no meaningful conversion (a colour to a node path); the editor then offers
/// the target type's default instead, and says so.
[[nodiscard]] std::optional<VariantConversionUVE> TryConvertVariantUVE(const VariantUVE& value,
                                                                       VariantTypeUVE target);

/// Limits that keep a hostile or corrupt scene file from making one value unbounded. Checked on
/// load and on authoring, so nothing past them can be created or restored.
inline constexpr std::size_t kMaximumVariantDepthUVE = 16U;
inline constexpr std::size_t kMaximumVariantElementsUVE = 65536U;
inline constexpr std::size_t kMaximumVariantTextBytesUVE = 65536U;

/// True when `value`, including everything nested inside it, stays within the limits above and every
/// floating-point number in it is finite. Finiteness is part of "in bounds" because scene files are
/// JSON, which has no NaN or infinity: such a value would save as null and make the next load fail.
[[nodiscard]] bool IsVariantWithinBoundsUVE(const VariantUVE& value) noexcept;

} // namespace UVE::Core
