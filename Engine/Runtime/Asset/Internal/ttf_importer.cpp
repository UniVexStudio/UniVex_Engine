// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ttf_importer.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <system_error>
#include <utility>
#include <vector>

#include "font_asset.h"
#include "logging_macros.h"

namespace UVE::Asset {
namespace {

[[nodiscard]] bool ReadTtfSourceBytes(const std::filesystem::path& sourcePath,
                                         std::vector<std::byte>& outBytes) {
    std::ifstream input(sourcePath, std::ios::binary | std::ios::ate);
    if (!input.is_open()) {
        UVE_ERROR("TtfImporter: failed to open source \"{}\"", sourcePath.string());
        return false;
    }
    const std::streamoff fileSize = input.tellg();
    if (fileSize < 0 || static_cast<std::uint64_t>(fileSize) > kMaximumFontAssetBytes ||
        static_cast<std::uint64_t>(fileSize) > std::numeric_limits<std::size_t>::max()) {
        UVE_ERROR("TtfImporter: source \"{}\" exceeds the bounded source-size limit", sourcePath.string());
        return false;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(fileSize));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!input) {
            UVE_ERROR("TtfImporter: source \"{}\" could not be read completely", sourcePath.string());
            return false;
        }
    }
    outBytes = std::move(bytes);
    return true;
}

[[nodiscard]] bool ImportTtfSource(const std::filesystem::path& sourcePath,
                                      const std::filesystem::path& destinationPath,
                                      const AssetImportSettings& /*settings*/) {
    try {
        std::vector<std::byte> sourceBytes;
        if (!ReadTtfSourceBytes(sourcePath, sourceBytes)) {
            return false;
        }
        FontAsset font;
        font.familyName = sourcePath.stem().string();
        if (font.familyName.size() > kMaximumFontFamilyNameBytes) {
            UVE_ERROR("TtfImporter: source \"{}\" has an over-long stem for a family name",
                      sourcePath.string());
            return false;
        }
        font.fontData = std::move(sourceBytes);
        if (!IsFontAssetValid(font)) {
            UVE_ERROR("TtfImporter: source \"{}\" is not a recognised sfnt font", sourcePath.string());
            return false;
        }
        std::error_code errorCode;
        if (const std::filesystem::path parent = destinationPath.parent_path(); !parent.empty()) {
            std::filesystem::create_directories(parent, errorCode);
            if (errorCode) {
                UVE_ERROR("TtfImporter: failed to create destination directory \"{}\": {}", parent.string(),
                          errorCode.message());
                return false;
            }
        }
        return SaveFontAsset(font, destinationPath);
    } catch (const std::bad_alloc&) {
        return false;
    }
}

} // namespace

void RegisterTtfImporter(IAssetImporter& importer) {
    importer.RegisterImporter("ttf", &ImportTtfSource);
    importer.RegisterImporter("otf", &ImportTtfSource);
}

} // namespace UVE::Asset
