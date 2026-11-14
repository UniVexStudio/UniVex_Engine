// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace UVE::Asset {

inline constexpr std::size_t kMaximumBlueprintNodesUVE = 4096U;
inline constexpr std::size_t kMaximumBlueprintVariablesUVE = 256U;
inline constexpr std::size_t kMaximumBlueprintNameBytesUVE = 128U;
inline constexpr std::size_t kMaximumBlueprintPayloadBytesUVE = 8U * 1024U * 1024U;

/// The kinds of value a blueprint pin or variable holds.
enum class BlueprintPinTypeUVE : std::uint8_t {
    Exec = 0,
    Bool,
    Int,
    Float,
    Vector,
    String,
    Object,
};

/// A variable of the blueprint: kept between frames. An exposed one shows in the object's Details
/// (an `export`), so every object using the blueprint can set its own value.
struct BlueprintVariableUVE final {
    std::string name;
    BlueprintPinTypeUVE type = BlueprintPinTypeUVE::Float;
    /// Its starting value as text ("0.0", "true", "(0,1,0)", "\"hello\"" is written as hello).
    std::string defaultText;
    bool exposed = false;
    [[nodiscard]] bool operator==(const BlueprintVariableUVE&) const = default;
};

/// A node on the graph: what it is (`type`, a key of the engine's node catalogue), where it is
/// drawn, and the values typed into its unwired input pins (by input index).
struct BlueprintNodeUVE final {
    std::uint32_t id = 0U;
    std::string type;
    /// Get / Set Variable: the variable's name. Find Object: unused.
    std::string variable;
    float x = 0.0F;
    float y = 0.0F;
    std::vector<std::string> inputValues;
    [[nodiscard]] bool operator==(const BlueprintNodeUVE&) const = default;
};

/// A wire from an output pin to an input pin (by pin index on each node).
struct BlueprintWireUVE final {
    std::uint32_t fromNode = 0U;
    std::uint32_t fromPin = 0U;
    std::uint32_t toNode = 0U;
    std::uint32_t toPin = 0U;
    [[nodiscard]] bool operator==(const BlueprintWireUVE&) const = default;
};

/// A comment box (editor only).
struct BlueprintCommentUVE final {
    std::string title = "Comment";
    float x = 0.0F;
    float y = 0.0F;
    float width = 260.0F;
    float height = 160.0F;
    std::uint32_t color = 0x1E50822DU;   ///< RGBA
    [[nodiscard]] bool operator==(const BlueprintCommentUVE&) const = default;
};

/// A Blueprint Class (`.uvbp`): the gameplay of one kind of object, as a graph of the engine's own
/// nodes. It runs as UVScript: the engine translates it (TranslateBlueprintToUVScriptUVE) and
/// compiles the result, so a blueprint has every event, call and check a script has.
struct BlueprintAssetUVE final {
    /// The object kind it was made for ("Character3D", "Player3D", "Static3D" ...).
    std::string parentKind = "Object3D";
    std::vector<BlueprintVariableUVE> variables;
    std::vector<BlueprintNodeUVE> nodes;
    std::vector<BlueprintWireUVE> wires;
    std::vector<BlueprintCommentUVE> comments;
    [[nodiscard]] bool operator==(const BlueprintAssetUVE&) const = default;
};

[[nodiscard]] bool IsBlueprintAssetValidUVE(const BlueprintAssetUVE& blueprint) noexcept;
/// The payload as JSON text, and back (what the file holds inside its envelope).
[[nodiscard]] std::string SerializeBlueprintAssetUVE(const BlueprintAssetUVE& blueprint);
[[nodiscard]] std::optional<BlueprintAssetUVE> ParseBlueprintAssetUVE(const std::string& payload);
/// A whole `.uvbp` file's bytes (envelope and payload), as read through a virtual file system.
[[nodiscard]] std::optional<BlueprintAssetUVE> DecodeBlueprintAssetUVE(const std::vector<std::byte>& file);
[[nodiscard]] bool SaveBlueprintAssetUVE(const BlueprintAssetUVE& blueprint, const std::filesystem::path& path);
[[nodiscard]] bool LoadBlueprintAssetUVE(const std::filesystem::path& path, BlueprintAssetUVE& outBlueprint);

} // namespace UVE::Asset
