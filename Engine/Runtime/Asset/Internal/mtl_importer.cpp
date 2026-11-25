// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "mtl_importer.h"

#include <cstddef>
#include <filesystem>
#include <string_view>
#include <vector>

#include "import_helpers.h"

#include "material_asset.h"
#include "mtl_material_converter.h"
#include "logging_macros.h"

namespace UVE::Asset {
namespace {

constexpr const char* kMtlImporterName = "MtlImporter";
constexpr std::string_view kMtlTemporarySuffix = ".uve_mtl_tmp";

[[nodiscard]] bool ImportMtlSource(const std::filesystem::path& sourcePath,
                                      const std::filesystem::path& destinationPath,
                                      const AssetImportSettings& /*settings*/) {
    if (destinationPath.extension() != ".uvmat") {
        UVE_ERROR("MtlImporter: destination \"{}\" must use the .uvmat extension",
                  destinationPath.string());
        return false;
    }

    std::vector<std::byte> sourceBytes;
    if (!Detail::ReadBoundedSourceBytes(sourcePath, kMtlImporterName, kMaximumMtlMaterialSourceBytes,
                                           sourceBytes)) {
        return false;
    }

    const std::string_view source(reinterpret_cast<const char*>(sourceBytes.data()), sourceBytes.size());
    MaterialAsset material;
    if (!ConvertMtlMaterial(source, material)) {
        UVE_ERROR("MtlImporter: source \"{}\" failed bounded MTL material conversion", sourcePath.string());
        return false;
    }
    return Detail::PublishAssetAtomically(
        destinationPath, kMtlImporterName, kMtlTemporarySuffix,
        [&material](const std::filesystem::path& temporaryPath) { return SaveMaterialAsset(material, temporaryPath); });
}

} // namespace

void RegisterMtlImporter(IAssetImporter& importer) {
    importer.RegisterImporter("mtl", &ImportMtlSource);
}

} // namespace UVE::Asset
