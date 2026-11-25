// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace UVE::Asset {

inline constexpr std::size_t kMaximumBlueprintNodes = 4096U;
inline constexpr std::size_t kMaximumBlueprintVariables = 256U;
inline constexpr std::size_t kMaximumBlueprintNameBytes = 128U;
inline constexpr std::size_t kMaximumBlueprintPayloadBytes = 8U * 1024U * 1024U;

/// The kinds of value a blueprint pin or variable holds.
enum class BlueprintPinType : std::uint8_t {
    Exec = 0,
    Bool,
    Int,
    Float,
    Vector,
    String,
    Object,
    /// Lists (UVScript `list[...]`), kept after the scalars so saved numbers never change.
    ObjectArray,
    IntArray,
    FloatArray,
    StringArray,
};

/// True for the list pin types.
[[nodiscard]] constexpr bool IsBlueprintArrayType(const BlueprintPinType type) noexcept {
    return type >= BlueprintPinType::ObjectArray && type <= BlueprintPinType::StringArray;
}

/// A list type's item type (Object, Int, Float or String); the type itself for the others.
[[nodiscard]] constexpr BlueprintPinType BlueprintArrayItemType(const BlueprintPinType type) noexcept {
    switch (type) {
        case BlueprintPinType::ObjectArray: return BlueprintPinType::Object;
        case BlueprintPinType::IntArray: return BlueprintPinType::Int;
        case BlueprintPinType::FloatArray: return BlueprintPinType::Float;
        case BlueprintPinType::StringArray: return BlueprintPinType::String;
        default: return type;
    }
}

/// A variable of the blueprint: kept between frames. An exposed one shows in the object's Details
/// (an `export`), so every object using the blueprint can set its own value.
struct BlueprintVariable final {
    std::string name;
    BlueprintPinType type = BlueprintPinType::Float;
    /// Its starting value as text ("0.0", "true", "(0,1,0)", "\"hello\"" is written as hello).
    std::string defaultText;
    bool exposed = false;
    [[nodiscard]] bool operator==(const BlueprintVariable&) const = default;
};

/// A node on the graph: what it is (`type`, a key of the engine's node catalogue), where it is
/// drawn, and the values typed into its unwired input pins (by input index).
struct BlueprintNode final {
    std::uint32_t id = 0U;
    std::string type;
    /// Get / Set Variable: the variable's name. Find Object: unused.
    std::string variable;
    float x = 0.0F;
    float y = 0.0F;
    std::vector<std::string> inputValues;
    [[nodiscard]] bool operator==(const BlueprintNode&) const = default;
};

/// A wire from an output pin to an input pin (by pin index on each node).
struct BlueprintWire final {
    std::uint32_t fromNode = 0U;
    std::uint32_t fromPin = 0U;
    std::uint32_t toNode = 0U;
    std::uint32_t toPin = 0U;
    [[nodiscard]] bool operator==(const BlueprintWire&) const = default;
};

/// A comment box (editor only).
struct BlueprintComment final {
    std::string title = "Comment";
    float x = 0.0F;
    float y = 0.0F;
    float width = 260.0F;
    float height = 160.0F;
    std::uint32_t color = 0x1E50822DU;   ///< RGBA
    [[nodiscard]] bool operator==(const BlueprintComment&) const = default;
};

/// A Blueprint Class (`.uvbp`): the gameplay of one kind of object, as a graph of the engine's own
/// nodes. It runs as UVScript: the engine translates it (TranslateBlueprintToUVScript) and
/// compiles the result, so a blueprint has every event, call and check a script has.
struct BlueprintAsset final {
    /// The object kind it was made for ("Character3D", "Player3D", "Static3D" ...).
    std::string parentKind = "Object3D";
    std::vector<BlueprintVariable> variables;
    std::vector<BlueprintNode> nodes;
    std::vector<BlueprintWire> wires;
    std::vector<BlueprintComment> comments;
    [[nodiscard]] bool operator==(const BlueprintAsset&) const = default;
};

[[nodiscard]] bool IsBlueprintAssetValid(const BlueprintAsset& blueprint) noexcept;
/// The payload as JSON text, and back (what the file holds inside its envelope).
[[nodiscard]] std::string SerializeBlueprintAsset(const BlueprintAsset& blueprint);
[[nodiscard]] std::optional<BlueprintAsset> ParseBlueprintAsset(const std::string& payload);
/// A whole `.uvbp` file's bytes (envelope and payload), as read through a virtual file system.
[[nodiscard]] std::optional<BlueprintAsset> DecodeBlueprintAsset(const std::vector<std::byte>& file);
[[nodiscard]] bool SaveBlueprintAsset(const BlueprintAsset& blueprint, const std::filesystem::path& path);
[[nodiscard]] bool LoadBlueprintAsset(const std::filesystem::path& path, BlueprintAsset& outBlueprint);

} // namespace UVE::Asset
