// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace UVE::UVScript {

/// A type the checker knows. `node` names the node kind for `Node` (Node3D, Character3D...).
struct TypeUVE final {
    enum class KindUVE : std::uint8_t {
        None,
        Bool,
        Int,
        Float,
        Str,
        Vec3,
        Node,
        /// Stands in after an error, so one mistake is reported once and not again downstream.
        Error,
    };

    KindUVE kind = KindUVE::None;
    std::string node;

    [[nodiscard]] static TypeUVE NoneUVE() { return {KindUVE::None, {}}; }
    [[nodiscard]] static TypeUVE BoolUVE() { return {KindUVE::Bool, {}}; }
    [[nodiscard]] static TypeUVE IntUVE() { return {KindUVE::Int, {}}; }
    [[nodiscard]] static TypeUVE FloatUVE() { return {KindUVE::Float, {}}; }
    [[nodiscard]] static TypeUVE StrUVE() { return {KindUVE::Str, {}}; }
    [[nodiscard]] static TypeUVE Vec3UVE() { return {KindUVE::Vec3, {}}; }
    [[nodiscard]] static TypeUVE NodeUVE(std::string kind) { return {KindUVE::Node, std::move(kind)}; }
    [[nodiscard]] static TypeUVE ErrorUVE() { return {KindUVE::Error, {}}; }

    [[nodiscard]] bool IsNumericUVE() const noexcept { return kind == KindUVE::Int || kind == KindUVE::Float; }
    [[nodiscard]] std::string NameUVE() const;

    bool operator==(const TypeUVE&) const = default;
};

struct Vec3ValueUVE final {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    bool operator==(const Vec3ValueUVE&) const = default;
};

/// A handle to a node the host knows; 0 is no node.
struct NodeRefUVE final {
    std::uint64_t id = 0U;

    bool operator==(const NodeRefUVE&) const = default;
};

/// One runtime value. `std::monostate` is `none`.
using ValueUVE = std::variant<std::monostate, bool, std::int64_t, double, std::string, Vec3ValueUVE, NodeRefUVE>;

/// How `print` and string interpolation show a value: 3, 2.5, true, (1, 2, 3), none.
[[nodiscard]] std::string FormatValueUVE(const ValueUVE& value);

/// Reads text written by FormatValueUVE (or typed by a person) back as a value of `type`:
/// `true`/`false`, `12`, `1.5`, any text for `str`, and `(x, y, z)` or `x, y, z` for `vec3`.
/// Nothing when the text is not a value of that type; node values have no text form.
[[nodiscard]] std::optional<ValueUVE> ParseValueTextUVE(std::string_view text, const TypeUVE& type);

} // namespace UVE::UVScript
