// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "data_table_importer.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>

#include "data_table_asset.h"
#include "logging_macros.h"
#include "hash.h"

namespace UVE::Asset {
namespace {

enum class DataTableSourceFormat : std::uint8_t {
    Csv,
    Tsv,
    Json,
};

[[nodiscard]] bool ReadSourceDocument(const std::filesystem::path& sourcePath, std::string& document) {
    std::error_code sourceSizeError;
    const std::uintmax_t sourceSize = std::filesystem::file_size(sourcePath, sourceSizeError);
    if (sourceSizeError || sourceSize > DataTable::kMaximumDocumentBytes) {
        return false;
    }
    std::ifstream input(sourcePath, std::ios::binary);
    if (!input.is_open()) {
        return false;
    }
    document.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return document.size() <= DataTable::kMaximumDocumentBytes;
}

[[nodiscard]] bool ImportDataTableSource(const std::filesystem::path& sourcePath,
                                             const std::filesystem::path& destinationPath,
                                             const AssetImportSettings& baseSettings,
                                             const DataTableSourceFormat format) {
    const auto* const settings = dynamic_cast<const DataTableImportSettings*>(&baseSettings);
    if (settings == nullptr || settings->tableName.empty() || settings->columns.empty() ||
        destinationPath.extension() != ".uvtable") {
        UVE_ERROR("DataTableImporter: missing schema settings or invalid .uvtable destination");
        return false;
    }

    std::string document;
    if (!ReadSourceDocument(sourcePath, document)) {
        UVE_ERROR("DataTableImporter: failed to read bounded source document {}", sourcePath.string());
        return false;
    }

    DataTable table(settings->tableName);
    for (const DataTableColumn& column : settings->columns) {
        if (!table.DefineColumn(column.name, column.type)) {
            UVE_ERROR("DataTableImporter: invalid schema settings for {}", sourcePath.string());
            return false;
        }
    }

    bool imported = false;
    switch (format) {
        case DataTableSourceFormat::Csv:
            imported = table.ImportCsv(document);
            break;
        case DataTableSourceFormat::Tsv:
            imported = table.ImportTsv(document);
            break;
        case DataTableSourceFormat::Json:
            imported = table.ImportJson(document);
            break;
    }
    return imported && SaveDataTableAsset(table, destinationPath);
}

} // namespace

std::string DataTableImportSettings::GetCacheVersion() const {
    // The version string feeds the import cache, so these values must never change: the hasher
    // consumes exactly the same bytes in exactly the same order as the loop it replaced.
    Utilities::Fnv1a64 hasher;
    const auto appendBytes = [&hasher](const std::string_view value) {
        hasher.AppendString(value);
    };
    const auto appendLengthDelimited = [&appendBytes](const std::string_view value) {
        appendBytes(std::to_string(value.size()));
        appendBytes(":");
        appendBytes(value);
    };

    appendLengthDelimited(tableName);
    appendLengthDelimited(std::to_string(columns.size()));
    for (const DataTableColumn& column : columns) {
        appendLengthDelimited(column.name);
        appendLengthDelimited(std::to_string(static_cast<unsigned int>(column.type)));
    }

    std::ostringstream version;
    version << "data-table-import-v2-" << std::hex << std::setw(16) << std::setfill('0') << hasher.Digest();
    return version.str();
}

void RegisterDataTableImporters(IAssetImporter& importer) {
    importer.RegisterImporter(".csv", [](const std::filesystem::path& sourcePath,
                                            const std::filesystem::path& destinationPath,
                                            const AssetImportSettings& settings) {
        return ImportDataTableSource(sourcePath, destinationPath, settings, DataTableSourceFormat::Csv);
    });
    importer.RegisterImporter(".tsv", [](const std::filesystem::path& sourcePath,
                                            const std::filesystem::path& destinationPath,
                                            const AssetImportSettings& settings) {
        return ImportDataTableSource(sourcePath, destinationPath, settings, DataTableSourceFormat::Tsv);
    });
    importer.RegisterImporter(".json", [](const std::filesystem::path& sourcePath,
                                             const std::filesystem::path& destinationPath,
                                             const AssetImportSettings& settings) {
        return ImportDataTableSource(sourcePath, destinationPath, settings, DataTableSourceFormat::Json);
    });
}

} // namespace UVE::Asset
