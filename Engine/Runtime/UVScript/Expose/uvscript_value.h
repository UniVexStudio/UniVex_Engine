// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace UVE::UVScript {

/// A type the checker knows. `object` names the object kind for `Object` (Object3D, Character3D...).
/// `elements` types a collection: one entry for `List` (every item), two for `Map` (the key, then
/// the value), and one per position for `Tuple`. Element types are a compile-time promise only -
/// values at run time are plain items, so a `list[int]` from the host is trusted to hold ints.
struct TypeUVE final {
    enum class KindUVE : std::uint8_t {
        None,
        Bool,
        Int,
        Float,
        Str,
        Vec3,
        Object,
        List,
        Map,
        Tuple,
        /// Stands in after an error, so one mistake is reported once and not again downstream.
        Error,
    };

    KindUVE kind = KindUVE::None;
    std::string object;
    std::vector<TypeUVE> elements;

    [[nodiscard]] static TypeUVE NoneUVE() { return {KindUVE::None, {}, {}}; }
    [[nodiscard]] static TypeUVE BoolUVE() { return {KindUVE::Bool, {}, {}}; }
    [[nodiscard]] static TypeUVE IntUVE() { return {KindUVE::Int, {}, {}}; }
    [[nodiscard]] static TypeUVE FloatUVE() { return {KindUVE::Float, {}, {}}; }
    [[nodiscard]] static TypeUVE StrUVE() { return {KindUVE::Str, {}, {}}; }
    [[nodiscard]] static TypeUVE Vec3UVE() { return {KindUVE::Vec3, {}, {}}; }
    [[nodiscard]] static TypeUVE ObjectUVE(std::string kind) { return {KindUVE::Object, std::move(kind), {}}; }
    [[nodiscard]] static TypeUVE ListUVE(TypeUVE item) {
        TypeUVE type{KindUVE::List, {}, {}};
        type.elements.push_back(std::move(item));
        return type;
    }
    [[nodiscard]] static TypeUVE MapUVE(TypeUVE key, TypeUVE value) {
        TypeUVE type{KindUVE::Map, {}, {}};
        type.elements.push_back(std::move(key));
        type.elements.push_back(std::move(value));
        return type;
    }
    [[nodiscard]] static TypeUVE TupleUVE(std::vector<TypeUVE> items) { return {KindUVE::Tuple, {}, std::move(items)}; }
    [[nodiscard]] static TypeUVE ErrorUVE() { return {KindUVE::Error, {}, {}}; }

    [[nodiscard]] bool IsNumericUVE() const noexcept { return kind == KindUVE::Int || kind == KindUVE::Float; }
    [[nodiscard]] bool IsCollectionUVE() const noexcept {
        return kind == KindUVE::List || kind == KindUVE::Map || kind == KindUVE::Tuple;
    }
    [[nodiscard]] std::string NameUVE() const;

    bool operator==(const TypeUVE&) const = default;
};

struct Vec3ValueUVE final {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    bool operator==(const Vec3ValueUVE&) const = default;
};

/// A handle to an object the host knows; 0 is no object.
struct ObjectRefUVE final {
    std::uint64_t id = 0U;

    bool operator==(const ObjectRefUVE&) const = default;
};

/// A list, map or tuple value. Declared here and defined below: a collection holds values, so the
/// value type cannot name the struct directly, and `std::variant` needs every member complete.
/// The shared pointer also makes copies cheap - `let b = a` shares the items, which is safe
/// because no operation ever changes a collection in place: every write builds a new one.
struct CollectionValueUVE;
using ValueUVE = std::variant<std::monostate, bool, std::int64_t, double, std::string, Vec3ValueUVE, ObjectRefUVE,
                              std::shared_ptr<const CollectionValueUVE>>;

/// The shared guts of a list, map or tuple value. `map` keeps insertion order and looks entries up
/// linearly, which is plenty for the small tables scripts build.
struct CollectionValueUVE final {
    enum class KindUVE : std::uint8_t {
        List,
        Map,
        Tuple,
    };

    KindUVE kind = KindUVE::List;
    /// List/tuple items in order; map entries as key then value, pairs in insertion order.
    std::vector<ValueUVE> items;

    bool operator==(const CollectionValueUVE&) const = default;
};

[[nodiscard]] inline ValueUVE MakeListValueUVE(std::vector<ValueUVE> items) {
    return ValueUVE{std::make_shared<const CollectionValueUVE>(
        CollectionValueUVE{CollectionValueUVE::KindUVE::List, std::move(items)})};
}

[[nodiscard]] inline ValueUVE MakeMapValueUVE(std::vector<ValueUVE> pairs) {
    return ValueUVE{std::make_shared<const CollectionValueUVE>(
        CollectionValueUVE{CollectionValueUVE::KindUVE::Map, std::move(pairs)})};
}

[[nodiscard]] inline ValueUVE MakeTupleValueUVE(std::vector<ValueUVE> items) {
    return ValueUVE{std::make_shared<const CollectionValueUVE>(
        CollectionValueUVE{CollectionValueUVE::KindUVE::Tuple, std::move(items)})};
}

/// How `print` and string interpolation show a value: 3, 2.5, true, (1, 2, 3), [1, 2], none.
[[nodiscard]] std::string FormatValueUVE(const ValueUVE& value);

/// Reads text written by FormatValueUVE (or typed by a person) back as a value of `type`:
/// `true`/`false`, `12`, `1.5`, any text for `str`, and `(x, y, z)` or `x, y, z` for `vec3`.
/// Nothing when the text is not a value of that type; object and collection values have no text form.
[[nodiscard]] std::optional<ValueUVE> ParseValueTextUVE(std::string_view text, const TypeUVE& type);

} // namespace UVE::UVScript
