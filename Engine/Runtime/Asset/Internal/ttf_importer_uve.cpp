// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/ttf_importer_uve.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <system_error>
#include <utility>
#include <vector>

#include "uve/asset/font_asset_uve.h"
#include "uve/logging/logging_macros_uve.h"

namespace UVE::Asset {
namespace {

[[nodiscard]] bool ReadTtfSourceBytesUVE(const std::filesystem::path& sourcePath,
                                         std::vector<std::byte>& outBytes) {
    std::ifstream input(sourcePath, std::ios::binary | std::ios::ate);
    if (!input.is_open()) {
        UVE_ERROR("TtfImporterUVE: failed to open source \"{}\"", sourcePath.string());
        return false;
    }
    const std::streamoff fileSize = input.tellg();
    if (fileSize < 0 || static_cast<std::uint64_t>(fileSize) > kMaximumFontAssetBytesUVE ||
        static_cast<std::uint64_t>(fileSize) > std::numeric_limits<std::size_t>::max()) {
        UVE_ERROR("TtfImporterUVE: source \"{}\" exceeds the bounded source-size limit", sourcePath.string());
        return false;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(fileSize));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!input) {
            UVE_ERROR("TtfImporterUVE: source \"{}\" could not be read completely", sourcePath.string());
            return false;
        }
    }
    outBytes = std::move(bytes);
    return true;
}

[[nodiscard]] bool ImportTtfSourceUVE(const std::filesystem::path& sourcePath,
                                      const std::filesystem::path& destinationPath,
                                      const AssetImportSettingsUVE& /*settings*/) {
    try {
        std::vector<std::byte> sourceBytes;
        if (!ReadTtfSourceBytesUVE(sourcePath, sourceBytes)) {
            return false;
        }
        FontAssetUVE font;
        font.familyName = sourcePath.stem().string();
        if (font.familyName.size() > kMaximumFontFamilyNameBytesUVE) {
            UVE_ERROR("TtfImporterUVE: source \"{}\" has an over-long stem for a family name",
                      sourcePath.string());
            return false;
        }
        font.fontData = std::move(sourceBytes);
        if (!IsFontAssetValidUVE(font)) {
            UVE_ERROR("TtfImporterUVE: source \"{}\" is not a recognised sfnt font", sourcePath.string());
            return false;
        }
        std::error_code errorCode;
        if (const std::filesystem::path parent = destinationPath.parent_path(); !parent.empty()) {
            std::filesystem::create_directories(parent, errorCode);
            if (errorCode) {
                UVE_ERROR("TtfImporterUVE: failed to create destination directory \"{}\": {}", parent.string(),
                          errorCode.message());
                return false;
            }
        }
        return SaveFontAssetUVE(font, destinationPath);
    } catch (const std::bad_alloc&) {
        return false;
    }
}

} // namespace

void RegisterTtfImporterUVE(IAssetImporterUVE& importer) {
    importer.RegisterImporterUVE("ttf", &ImportTtfSourceUVE);
    importer.RegisterImporterUVE("otf", &ImportTtfSourceUVE);
}

} // namespace UVE::Asset
