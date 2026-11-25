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

namespace UVE::Blueprint {

/// A type the checker knows. `object` names the object kind for `Object` (Object3D, Character3D...).
/// `elements` types a collection: one entry for `List` (every item), two for `Map` (the key, then
/// the value), and one per position for `Tuple`. Element types are a compile-time promise only -
/// values at run time are plain items, so a `list[int]` from the host is trusted to hold ints.
struct Type final {
    enum class Kind : std::uint8_t {
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

    Kind kind = Kind::None;
    std::string object;
    std::vector<Type> elements;

    [[nodiscard]] static Type None() { return {Kind::None, {}, {}}; }
    [[nodiscard]] static Type Bool() { return {Kind::Bool, {}, {}}; }
    [[nodiscard]] static Type Int() { return {Kind::Int, {}, {}}; }
    [[nodiscard]] static Type Float() { return {Kind::Float, {}, {}}; }
    [[nodiscard]] static Type Str() { return {Kind::Str, {}, {}}; }
    [[nodiscard]] static Type Vec3() { return {Kind::Vec3, {}, {}}; }
    [[nodiscard]] static Type Object(std::string kind) { return {Kind::Object, std::move(kind), {}}; }
    [[nodiscard]] static Type List(Type item) {
        Type type{Kind::List, {}, {}};
        type.elements.push_back(std::move(item));
        return type;
    }
    [[nodiscard]] static Type Map(Type key, Type value) {
        Type type{Kind::Map, {}, {}};
        type.elements.push_back(std::move(key));
        type.elements.push_back(std::move(value));
        return type;
    }
    [[nodiscard]] static Type Tuple(std::vector<Type> items) { return {Kind::Tuple, {}, std::move(items)}; }
    [[nodiscard]] static Type Error() { return {Kind::Error, {}, {}}; }

    [[nodiscard]] bool IsNumeric() const noexcept { return kind == Kind::Int || kind == Kind::Float; }
    [[nodiscard]] bool IsCollection() const noexcept {
        return kind == Kind::List || kind == Kind::Map || kind == Kind::Tuple;
    }
    [[nodiscard]] std::string Name() const;

    bool operator==(const Type&) const = default;
};

struct Vec3Value final {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    bool operator==(const Vec3Value&) const = default;
};

/// A handle to an object the host knows; 0 is no object.
struct ObjectRef final {
    std::uint64_t id = 0U;

    bool operator==(const ObjectRef&) const = default;
};

/// A list, map or tuple value. Declared here and defined below: a collection holds values, so the
/// value type cannot name the struct directly, and `std::variant` needs every member complete.
/// The shared pointer also makes copies cheap - `let b = a` shares the items, which is safe
/// because no operation ever changes a collection in place: every write builds a new one.
struct CollectionValue;
using Value = std::variant<std::monostate, bool, std::int64_t, double, std::string, Vec3Value, ObjectRef,
                              std::shared_ptr<const CollectionValue>>;

/// The shared guts of a list, map or tuple value. `map` keeps insertion order and looks entries up
/// linearly, which is plenty for the small tables scripts build.
struct CollectionValue final {
    enum class Kind : std::uint8_t {
        List,
        Map,
        Tuple,
    };

    Kind kind = Kind::List;
    /// List/tuple items in order; map entries as key then value, pairs in insertion order.
    std::vector<Value> items;

    bool operator==(const CollectionValue&) const = default;
};

[[nodiscard]] inline Value MakeListValue(std::vector<Value> items) {
    return Value{std::make_shared<const CollectionValue>(
        CollectionValue{CollectionValue::Kind::List, std::move(items)})};
}

[[nodiscard]] inline Value MakeMapValue(std::vector<Value> pairs) {
    return Value{std::make_shared<const CollectionValue>(
        CollectionValue{CollectionValue::Kind::Map, std::move(pairs)})};
}

[[nodiscard]] inline Value MakeTupleValue(std::vector<Value> items) {
    return Value{std::make_shared<const CollectionValue>(
        CollectionValue{CollectionValue::Kind::Tuple, std::move(items)})};
}

/// How `print` and string interpolation show a value: 3, 2.5, true, (1, 2, 3), [1, 2], none.
[[nodiscard]] std::string FormatValue(const Value& value);

/// Reads text written by FormatValue (or typed by a person) back as a value of `type`:
/// `true`/`false`, `12`, `1.5`, any text for `str`, and `(x, y, z)` or `x, y, z` for `vec3`.
/// Nothing when the text is not a value of that type; object and collection values have no text form.
[[nodiscard]] std::optional<Value> ParseValueText(std::string_view text, const Type& type);

} // namespace UVE::Blueprint
