// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/fbx_importer_uve.h"

#include <cstddef>
#include <filesystem>
#include <string_view>
#include <vector>

#include "import_helpers_uve.h"

#include "uve/asset/mesh_asset_uve.h"
#include "uve/asset/fbx_mesh_converter_uve.h"
#include "uve/logging/logging_macros_uve.h"

namespace UVE::Asset {
namespace {

constexpr const char* kFbxImporterNameUVE = "FbxImporterUVE";
constexpr std::string_view kFbxTemporarySuffixUVE = ".uve_fbx_tmp";

[[nodiscard]] bool ImportFbxSourceUVE(const std::filesystem::path& sourcePath,
                                      const std::filesystem::path& destinationPath,
                                      const AssetImportSettingsUVE& /*settings*/) {
    if (destinationPath.extension() != ".uvemodel") {
        UVE_ERROR("FbxImporterUVE: destination \"{}\" must use the .uvemodel extension",
                  destinationPath.string());
        return false;
    }

    std::vector<std::byte> sourceBytes;
    if (!Detail::ReadBoundedSourceBytesUVE(sourcePath, kFbxImporterNameUVE, kMaximumFbxMeshSourceBytesUVE,
                                           sourceBytes)) {
        return false;
    }

    MeshAssetUVE mesh;
    if (!ConvertFbxMeshUVE(sourceBytes, mesh)) {
        UVE_ERROR("FbxImporterUVE: source \"{}\" failed FBX mesh conversion", sourcePath.string());
        return false;
    }
    return Detail::PublishAssetAtomicallyUVE(
        destinationPath, kFbxImporterNameUVE, kFbxTemporarySuffixUVE,
        [&mesh](const std::filesystem::path& temporaryPath) { return SaveMeshAssetUVE(mesh, temporaryPath); });
}

} // namespace

void RegisterFbxImporterUVE(IAssetImporterUVE& importer) {
    importer.RegisterImporterUVE("fbx", &ImportFbxSourceUVE);
}

} // namespace UVE::Asset
