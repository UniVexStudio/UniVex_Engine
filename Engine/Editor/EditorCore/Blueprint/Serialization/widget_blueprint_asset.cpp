// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "widget_blueprint_asset.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#include <nlohmann/json.hpp>

#include "logging_macros.h"
#include "uve_file_envelope.h"

namespace UVE::Asset {
namespace {

constexpr const char* kGraphKey = "widgetGraph";

[[nodiscard]] std::optional<nlohmann::json> ReadPrefabPayload(const std::vector<std::byte>& file) {
    const auto envelope = DecodeUveFileEnvelope(file, "widget blueprint");
    if (!envelope.has_value() || envelope->first.assetType != AssetKind::Prefab) {
        return std::nullopt;
    }
    const std::vector<std::byte>& payload = envelope->second;
    const std::string text(reinterpret_cast<const char*>(payload.data()), payload.size());
    nlohmann::json json = nlohmann::json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        return std::nullopt;
    }
    return json;
}

[[nodiscard]] BlueprintAsset EmptyWidgetGraph() {
    BlueprintAsset graph;
    graph.parentKind = "Canvas";
    return graph;
}

} // namespace

bool IsWidgetBlueprintPath(const std::string_view path) noexcept {
    return path.ends_with(kWidgetBlueprintExtension);
}

std::optional<BlueprintAsset> ReadWidgetBlueprintGraph(const std::vector<std::byte>& file) {
    const std::optional<nlohmann::json> json = ReadPrefabPayload(file);
    if (!json.has_value()) {
        return std::nullopt;
    }
    const auto it = json->find(kGraphKey);
    if (it == json->end() || !it->is_string()) {
        return EmptyWidgetGraph();
    }
    std::optional<BlueprintAsset> graph = ParseBlueprintAsset(it->get<std::string>());
    if (graph.has_value() && graph->parentKind.empty()) {
        graph->parentKind = "Canvas";
    }
    return graph;
}

std::optional<BlueprintAsset> LoadWidgetBlueprintGraph(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return ReadWidgetBlueprintGraph(bytes);
}

std::optional<std::vector<std::byte>> WithWidgetBlueprintGraph(const std::vector<std::byte>& file,
                                                                  const BlueprintAsset& graph) {
    std::optional<nlohmann::json> json = ReadPrefabPayload(file);
    if (!json.has_value()) {
        return std::nullopt;
    }
    (*json)[kGraphKey] = SerializeBlueprintAsset(graph);
    const std::string text = json->dump(2);
    std::vector<std::byte> payload(text.size());
    std::memcpy(payload.data(), text.data(), text.size());
    return EncodeUveFileEnvelope(AssetKind::Prefab, payload);
}

bool SaveWidgetBlueprintGraph(const std::filesystem::path& path, const BlueprintAsset& graph) {
    const auto file = ReadUveFile(path);
    if (!file.has_value()) {
        return false;
    }
    std::vector<std::byte> bytes = EncodeUveFileEnvelope(file->first.assetType, file->second);
    const std::optional<std::vector<std::byte>> updated = WithWidgetBlueprintGraph(bytes, graph);
    if (!updated.has_value()) {
        UVE_ERROR("Widget Blueprint: \"{}\" is not a widget file", path.string());
        return false;
    }
    const std::filesystem::path temporary = path.string() + ".uve_widget_tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(updated->data()), static_cast<std::streamsize>(updated->size()));
        if (!out) {
            UVE_ERROR("Widget Blueprint: could not write \"{}\"", temporary.string());
            return false;
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

} // namespace UVE::Asset
