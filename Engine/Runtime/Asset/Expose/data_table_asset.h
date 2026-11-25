// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <filesystem>

#include "data_table.h"

namespace UVE::Asset {

/// Persists one validated DataTable as a JSON payload inside the universal `.uve*` envelope.
/// Save never mutates the caller; Load validates into a temporary table before replacement.
/// The functions are synchronous and not thread-safe with respect to the supplied DataTable.
[[nodiscard]] bool SaveDataTableAsset(const DataTable& table, const std::filesystem::path& path);

/// Loads a DataTable only from an envelope tagged AssetKind::DataTable. The destination is
/// unchanged when the file is missing, malformed, the wrong asset kind, or an invalid table.
[[nodiscard]] bool LoadDataTableAsset(const std::filesystem::path& path, DataTable& table);

} // namespace UVE::Asset
