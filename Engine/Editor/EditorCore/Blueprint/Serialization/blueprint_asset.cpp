// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_asset.h"

#include <cmath>
#include <exception>
#include <set>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include "logging_macros.h"
#include "uve_file_envelope.h"

namespace UVE::Asset {
namespace {

using Json = nlohmann::json;
constexpr std::string_view kBlueprintSchema = "uve-blueprint-v1";

[[nodiscard]] bool NameOk(const std::string& name) noexcept {
    return !name.empty() && name.size() <= kMaximumBlueprintNameBytes && !name.contains('\0');
}

} // namespace

bool IsBlueprintAssetValid(const BlueprintAsset& blueprint) noexcept {
    if (!NameOk(blueprint.parentKind) || blueprint.nodes.size() > kMaximumBlueprintNodes ||
        blueprint.variables.size() > kMaximumBlueprintVariables || blueprint.comments.size() > kMaximumBlueprintNodes ||
        blueprint.wires.size() > kMaximumBlueprintNodes * 8U) {
        return false;
    }
    std::set<std::string> names;
    for (const BlueprintVariable& variable : blueprint.variables) {
        if (!NameOk(variable.name) || variable.type == BlueprintPinType::Exec ||
            variable.type > BlueprintPinType::StringArray || !names.insert(variable.name).second) {
            return false;
        }
    }
    std::set<std::uint32_t> ids;
    for (const BlueprintNode& node : blueprint.nodes) {
        if (node.id == 0U || !ids.insert(node.id).second || node.type.empty() || node.type.size() > kMaximumBlueprintNameBytes ||
            !std::isfinite(node.x) || !std::isfinite(node.y) || node.inputValues.size() > 64U) {
            return false;
        }
    }
    for (const BlueprintWire& wire : blueprint.wires) {
        if (!ids.contains(wire.fromNode) || !ids.contains(wire.toNode) || wire.fromPin > 64U || wire.toPin > 64U) {
            return false;
        }
    }
    for (const BlueprintComment& comment : blueprint.comments) {
        if (comment.title.size() > 256U || !std::isfinite(comment.x) || !std::isfinite(comment.y) || !std::isfinite(comment.width) ||
            !std::isfinite(comment.height)) {
            return false;
        }
    }
    return true;
}

std::string SerializeBlueprintAsset(const BlueprintAsset& blueprint) {
    Json variables = Json::array();
    for (const BlueprintVariable& v : blueprint.variables) {
        variables.push_back({{"name", v.name}, {"type", static_cast<std::uint8_t>(v.type)}, {"default", v.defaultText}, {"exposed", v.exposed}});
    }
    Json nodes = Json::array();
    for (const BlueprintNode& n : blueprint.nodes) {
        Json node{{"id", n.id}, {"type", n.type}, {"x", n.x}, {"y", n.y}, {"inputs", n.inputValues}};
        if (!n.variable.empty()) {
            node["variable"] = n.variable;
        }
        nodes.push_back(std::move(node));
    }
    Json wires = Json::array();
    for (const BlueprintWire& w : blueprint.wires) {
        wires.push_back({w.fromNode, w.fromPin, w.toNode, w.toPin});
    }
    Json comments = Json::array();
    for (const BlueprintComment& c : blueprint.comments) {
        comments.push_back({{"title", c.title}, {"x", c.x}, {"y", c.y}, {"w", c.width}, {"h", c.height}, {"color", c.color}});
    }
    const Json document{{"schema", kBlueprintSchema}, {"parentKind", blueprint.parentKind}, {"variables", std::move(variables)},
                           {"nodes", std::move(nodes)},       {"wires", std::move(wires)},             {"comments", std::move(comments)}};
    return document.dump(1);
}

std::optional<BlueprintAsset> ParseBlueprintAsset(const std::string& payload) {
    try {
        const Json document = Json::parse(payload);
        if (!document.is_object() || document.value("schema", std::string{}) != kBlueprintSchema) {
            return std::nullopt;
        }
        BlueprintAsset blueprint;
        blueprint.parentKind = document.value("parentKind", blueprint.parentKind);
        for (const Json& v : document.value("variables", Json::array())) {
            BlueprintVariable variable;
            variable.name = v.value("name", std::string{});
            variable.type = static_cast<BlueprintPinType>(v.value("type", std::uint8_t{3}));
            variable.defaultText = v.value("default", std::string{});
            variable.exposed = v.value("exposed", false);
            blueprint.variables.push_back(std::move(variable));
        }
        for (const Json& n : document.value("nodes", Json::array())) {
            BlueprintNode node;
            node.id = n.value("id", 0U);
            node.type = n.value("type", std::string{});
            node.variable = n.value("variable", std::string{});
            node.x = n.value("x", 0.0F);
            node.y = n.value("y", 0.0F);
            node.inputValues = n.value("inputs", std::vector<std::string>{});
            blueprint.nodes.push_back(std::move(node));
        }
        for (const Json& w : document.value("wires", Json::array())) {
            if (w.is_array() && w.size() == 4U) {
                blueprint.wires.push_back(BlueprintWire{w[0].get<std::uint32_t>(), w[1].get<std::uint32_t>(), w[2].get<std::uint32_t>(),
                                                           w[3].get<std::uint32_t>()});
            }
        }
        for (const Json& c : document.value("comments", Json::array())) {
            BlueprintComment comment;
            comment.title = c.value("title", comment.title);
            comment.x = c.value("x", 0.0F);
            comment.y = c.value("y", 0.0F);
            comment.width = c.value("w", comment.width);
            comment.height = c.value("h", comment.height);
            comment.color = c.value("color", comment.color);
            blueprint.comments.push_back(std::move(comment));
        }
        if (!IsBlueprintAssetValid(blueprint)) {
            return std::nullopt;
        }
        return blueprint;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<BlueprintAsset> DecodeBlueprintAsset(const std::vector<std::byte>& file) {
    const auto decoded = DecodeUveFileEnvelope(file, "blueprint");
    if (!decoded.has_value() || decoded->first.assetType != AssetKind::Blueprint || decoded->second.empty() ||
        decoded->second.size() > kMaximumBlueprintPayloadBytes) {
        return std::nullopt;
    }
    return ParseBlueprintAsset(std::string(reinterpret_cast<const char*>(decoded->second.data()), decoded->second.size()));
}

bool SaveBlueprintAsset(const BlueprintAsset& blueprint, const std::filesystem::path& path) {
    if (!IsBlueprintAssetValid(blueprint)) {
        UVE_ERROR("BlueprintAsset: refusing to save an invalid blueprint to {}", path.string());
        return false;
    }
    const std::string serialized = SerializeBlueprintAsset(blueprint);
    if (serialized.size() > kMaximumBlueprintPayloadBytes) {
        UVE_ERROR("BlueprintAsset: payload too large for {}", path.string());
        return false;
    }
    const auto* const bytes = reinterpret_cast<const std::byte*>(serialized.data());
    return WriteUveFile(path, AssetKind::Blueprint, std::vector<std::byte>(bytes, bytes + serialized.size()));
}

bool LoadBlueprintAsset(const std::filesystem::path& path, BlueprintAsset& outBlueprint) {
    const auto file = ReadUveFile(path);
    if (!file.has_value() || file->first.assetType != AssetKind::Blueprint || file->second.size() > kMaximumBlueprintPayloadBytes) {
        UVE_ERROR("BlueprintAsset: invalid blueprint envelope {}", path.string());
        return false;
    }
    std::optional<BlueprintAsset> parsed =
        ParseBlueprintAsset(std::string(reinterpret_cast<const char*>(file->second.data()), file->second.size()));
    if (!parsed.has_value()) {
        UVE_ERROR("BlueprintAsset: could not read {}", path.string());
        return false;
    }
    outBlueprint = std::move(*parsed);
    return true;
}

} // namespace UVE::Asset
