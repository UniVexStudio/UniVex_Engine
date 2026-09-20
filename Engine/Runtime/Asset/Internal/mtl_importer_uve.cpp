// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/asset/mtl_importer_uve.h"

#include <cstddef>
#include <filesystem>
#include <string_view>
#include <vector>

#include "import_helpers_uve.h"

#include "uve/asset/material_asset_uve.h"
#include "uve/asset/mtl_material_converter_uve.h"
#include "uve/logging/logging_macros_uve.h"

namespace UVE::Asset {
namespace {

constexpr const char* kMtlImporterNameUVE = "MtlImporterUVE";
constexpr std::string_view kMtlTemporarySuffixUVE = ".uve_mtl_tmp";

[[nodiscard]] bool ImportMtlSourceUVE(const std::filesystem::path& sourcePath,
                                      const std::filesystem::path& destinationPath,
                                      const AssetImportSettingsUVE& /*settings*/) {
    if (destinationPath.extension() != ".uvemat") {
        UVE_ERROR("MtlImporterUVE: destination \"{}\" must use the .uvemat extension",
                  destinationPath.string());
        return false;
    }

    std::vector<std::byte> sourceBytes;
    if (!Detail::ReadBoundedSourceBytesUVE(sourcePath, kMtlImporterNameUVE, kMaximumMtlMaterialSourceBytesUVE,
                                           sourceBytes)) {
        return false;
    }

    const std::string_view source(reinterpret_cast<const char*>(sourceBytes.data()), sourceBytes.size());
    MaterialAssetUVE material;
    if (!ConvertMtlMaterialUVE(source, material)) {
        UVE_ERROR("MtlImporterUVE: source \"{}\" failed bounded MTL material conversion", sourcePath.string());
        return false;
    }
    return Detail::PublishAssetAtomicallyUVE(
        destinationPath, kMtlImporterNameUVE, kMtlTemporarySuffixUVE,
        [&material](const std::filesystem::path& temporaryPath) { return SaveMaterialAssetUVE(material, temporaryPath); });
}

} // namespace

void RegisterMtlImporterUVE(IAssetImporterUVE& importer) {
    importer.RegisterImporterUVE("mtl", &ImportMtlSourceUVE);
}

} // namespace UVE::Asset
