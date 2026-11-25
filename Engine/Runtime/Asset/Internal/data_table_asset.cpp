// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "data_table_asset.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "uve_file_envelope.h"
#include "logging_macros.h"

namespace UVE::Asset {

bool SaveDataTableAsset(const DataTable& table, const std::filesystem::path& path) {
    std::string document;
    if (!DataTableAssetSerializer::Serialize(table, document)) {
        UVE_ERROR("DataTableAsset: failed to serialize table at {}", path.string());
        return false;
    }
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(document.data());
    const std::vector<std::byte> payload(payloadBytes, payloadBytes + document.size());
    return WriteUveFile(path, AssetKind::DataTable, payload);
}

bool LoadDataTableAsset(const std::filesystem::path& path, DataTable& table) {
    const std::optional<std::pair<UveFileHeader, std::vector<std::byte>>> file = ReadUveFile(path);
    if (!file.has_value()) {
        return false;
    }
    if (file->first.assetType != AssetKind::DataTable) {
        UVE_ERROR("DataTableAsset: {} is not a data-table file (asset type {})", path.string(),
                  static_cast<std::uint32_t>(file->first.assetType));
        return false;
    }

    const std::vector<std::byte>& payload = file->second;
    if (payload.empty() || payload.size() > DataTable::kMaximumDocumentBytes) {
        UVE_ERROR("DataTableAsset: {} has an empty or oversized document payload", path.string());
        return false;
    }
    const std::string document(reinterpret_cast<const char*>(payload.data()), payload.size());
    DataTable candidate;
    if (!DataTableAssetSerializer::Deserialize(document, candidate)) {
        UVE_ERROR("DataTableAsset: failed to deserialize {}", path.string());
        return false;
    }
    table = std::move(candidate);
    return true;
}

} // namespace UVE::Asset
