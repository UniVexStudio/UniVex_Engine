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

using JsonUVE = nlohmann::json;
constexpr std::string_view kBlueprintSchemaUVE = "uve-blueprint-v1";

[[nodiscard]] bool NameOkUVE(const std::string& name) noexcept {
    return !name.empty() && name.size() <= kMaximumBlueprintNameBytesUVE && !name.contains('\0');
}

} // namespace

bool IsBlueprintAssetValidUVE(const BlueprintAssetUVE& blueprint) noexcept {
    if (!NameOkUVE(blueprint.parentKind) || blueprint.nodes.size() > kMaximumBlueprintNodesUVE ||
        blueprint.variables.size() > kMaximumBlueprintVariablesUVE || blueprint.comments.size() > kMaximumBlueprintNodesUVE ||
        blueprint.wires.size() > kMaximumBlueprintNodesUVE * 8U) {
        return false;
    }
    std::set<std::string> names;
    for (const BlueprintVariableUVE& variable : blueprint.variables) {
        if (!NameOkUVE(variable.name) || variable.type == BlueprintPinTypeUVE::Exec ||
            variable.type > BlueprintPinTypeUVE::Object || !names.insert(variable.name).second) {
            return false;
        }
    }
    std::set<std::uint32_t> ids;
    for (const BlueprintNodeUVE& node : blueprint.nodes) {
        if (node.id == 0U || !ids.insert(node.id).second || node.type.empty() || node.type.size() > kMaximumBlueprintNameBytesUVE ||
            !std::isfinite(node.x) || !std::isfinite(node.y) || node.inputValues.size() > 64U) {
            return false;
        }
    }
    for (const BlueprintWireUVE& wire : blueprint.wires) {
        if (!ids.contains(wire.fromNode) || !ids.contains(wire.toNode) || wire.fromPin > 64U || wire.toPin > 64U) {
            return false;
        }
    }
    for (const BlueprintCommentUVE& comment : blueprint.comments) {
        if (comment.title.size() > 256U || !std::isfinite(comment.x) || !std::isfinite(comment.y) || !std::isfinite(comment.width) ||
            !std::isfinite(comment.height)) {
            return false;
        }
    }
    return true;
}

std::string SerializeBlueprintAssetUVE(const BlueprintAssetUVE& blueprint) {
    JsonUVE variables = JsonUVE::array();
    for (const BlueprintVariableUVE& v : blueprint.variables) {
        variables.push_back({{"name", v.name}, {"type", static_cast<std::uint8_t>(v.type)}, {"default", v.defaultText}, {"exposed", v.exposed}});
    }
    JsonUVE nodes = JsonUVE::array();
    for (const BlueprintNodeUVE& n : blueprint.nodes) {
        JsonUVE node{{"id", n.id}, {"type", n.type}, {"x", n.x}, {"y", n.y}, {"inputs", n.inputValues}};
        if (!n.variable.empty()) {
            node["variable"] = n.variable;
        }
        nodes.push_back(std::move(node));
    }
    JsonUVE wires = JsonUVE::array();
    for (const BlueprintWireUVE& w : blueprint.wires) {
        wires.push_back({w.fromNode, w.fromPin, w.toNode, w.toPin});
    }
    JsonUVE comments = JsonUVE::array();
    for (const BlueprintCommentUVE& c : blueprint.comments) {
        comments.push_back({{"title", c.title}, {"x", c.x}, {"y", c.y}, {"w", c.width}, {"h", c.height}, {"color", c.color}});
    }
    const JsonUVE document{{"schema", kBlueprintSchemaUVE}, {"parentKind", blueprint.parentKind}, {"variables", std::move(variables)},
                           {"nodes", std::move(nodes)},       {"wires", std::move(wires)},             {"comments", std::move(comments)}};
    return document.dump(1);
}

std::optional<BlueprintAssetUVE> ParseBlueprintAssetUVE(const std::string& payload) {
    try {
        const JsonUVE document = JsonUVE::parse(payload);
        if (!document.is_object() || document.value("schema", std::string{}) != kBlueprintSchemaUVE) {
            return std::nullopt;
        }
        BlueprintAssetUVE blueprint;
        blueprint.parentKind = document.value("parentKind", blueprint.parentKind);
        for (const JsonUVE& v : document.value("variables", JsonUVE::array())) {
            BlueprintVariableUVE variable;
            variable.name = v.value("name", std::string{});
            variable.type = static_cast<BlueprintPinTypeUVE>(v.value("type", std::uint8_t{3}));
            variable.defaultText = v.value("default", std::string{});
            variable.exposed = v.value("exposed", false);
            blueprint.variables.push_back(std::move(variable));
        }
        for (const JsonUVE& n : document.value("nodes", JsonUVE::array())) {
            BlueprintNodeUVE node;
            node.id = n.value("id", 0U);
            node.type = n.value("type", std::string{});
            node.variable = n.value("variable", std::string{});
            node.x = n.value("x", 0.0F);
            node.y = n.value("y", 0.0F);
            node.inputValues = n.value("inputs", std::vector<std::string>{});
            blueprint.nodes.push_back(std::move(node));
        }
        for (const JsonUVE& w : document.value("wires", JsonUVE::array())) {
            if (w.is_array() && w.size() == 4U) {
                blueprint.wires.push_back(BlueprintWireUVE{w[0].get<std::uint32_t>(), w[1].get<std::uint32_t>(), w[2].get<std::uint32_t>(),
                                                           w[3].get<std::uint32_t>()});
            }
        }
        for (const JsonUVE& c : document.value("comments", JsonUVE::array())) {
            BlueprintCommentUVE comment;
            comment.title = c.value("title", comment.title);
            comment.x = c.value("x", 0.0F);
            comment.y = c.value("y", 0.0F);
            comment.width = c.value("w", comment.width);
            comment.height = c.value("h", comment.height);
            comment.color = c.value("color", comment.color);
            blueprint.comments.push_back(std::move(comment));
        }
        if (!IsBlueprintAssetValidUVE(blueprint)) {
            return std::nullopt;
        }
        return blueprint;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<BlueprintAssetUVE> DecodeBlueprintAssetUVE(const std::vector<std::byte>& file) {
    const auto decoded = DecodeUveFileEnvelopeUVE(file, "blueprint");
    if (!decoded.has_value() || decoded->first.assetType != AssetKindUVE::Blueprint || decoded->second.empty() ||
        decoded->second.size() > kMaximumBlueprintPayloadBytesUVE) {
        return std::nullopt;
    }
    return ParseBlueprintAssetUVE(std::string(reinterpret_cast<const char*>(decoded->second.data()), decoded->second.size()));
}

bool SaveBlueprintAssetUVE(const BlueprintAssetUVE& blueprint, const std::filesystem::path& path) {
    if (!IsBlueprintAssetValidUVE(blueprint)) {
        UVE_ERROR("BlueprintAssetUVE: refusing to save an invalid blueprint to {}", path.string());
        return false;
    }
    const std::string serialized = SerializeBlueprintAssetUVE(blueprint);
    if (serialized.size() > kMaximumBlueprintPayloadBytesUVE) {
        UVE_ERROR("BlueprintAssetUVE: payload too large for {}", path.string());
        return false;
    }
    const auto* const bytes = reinterpret_cast<const std::byte*>(serialized.data());
    return WriteUveFileUVE(path, AssetKindUVE::Blueprint, std::vector<std::byte>(bytes, bytes + serialized.size()));
}

bool LoadBlueprintAssetUVE(const std::filesystem::path& path, BlueprintAssetUVE& outBlueprint) {
    const auto file = ReadUveFileUVE(path);
    if (!file.has_value() || file->first.assetType != AssetKindUVE::Blueprint || file->second.size() > kMaximumBlueprintPayloadBytesUVE) {
        UVE_ERROR("BlueprintAssetUVE: invalid blueprint envelope {}", path.string());
        return false;
    }
    std::optional<BlueprintAssetUVE> parsed =
        ParseBlueprintAssetUVE(std::string(reinterpret_cast<const char*>(file->second.data()), file->second.size()));
    if (!parsed.has_value()) {
        UVE_ERROR("BlueprintAssetUVE: could not read {}", path.string());
        return false;
    }
    outBlueprint = std::move(*parsed);
    return true;
}

} // namespace UVE::Asset
