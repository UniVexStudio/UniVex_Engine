// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/asset/obj_importer_uve.h"

#include <cstddef>
#include <filesystem>
#include <string_view>
#include <vector>

#include "import_helpers_uve.h"

#include "uve/asset/mesh_asset_uve.h"
#include "uve/asset/obj_mesh_converter_uve.h"
#include "uve/debug/logging_macros_uve.h"

namespace UVE::Asset {
namespace {

constexpr const char* kObjImporterNameUVE = "ObjImporterUVE";
constexpr std::string_view kObjTemporarySuffixUVE = ".uve_obj_tmp";

[[nodiscard]] bool ImportObjSourceUVE(const std::filesystem::path& sourcePath,
                                      const std::filesystem::path& destinationPath,
                                      const AssetImportSettingsUVE& /*settings*/) {
    if (destinationPath.extension() != ".uvemodel") {
        UVE_ERROR("ObjImporterUVE: destination \"{}\" must use the .uvemodel extension",
                  destinationPath.string());
        return false;
    }

    std::vector<std::byte> sourceBytes;
    if (!Detail::ReadBoundedSourceBytesUVE(sourcePath, kObjImporterNameUVE, kMaximumObjMeshSourceBytesUVE,
                                           sourceBytes)) {
        return false;
    }

    const std::string_view source(reinterpret_cast<const char*>(sourceBytes.data()), sourceBytes.size());
    MeshAssetUVE mesh;
    if (!ConvertObjMeshUVE(source, mesh)) {
        UVE_ERROR("ObjImporterUVE: source \"{}\" failed bounded OBJ mesh conversion", sourcePath.string());
        return false;
    }
    return Detail::PublishAssetAtomicallyUVE(
        destinationPath, kObjImporterNameUVE, kObjTemporarySuffixUVE,
        [&mesh](const std::filesystem::path& temporaryPath) { return SaveMeshAssetUVE(mesh, temporaryPath); });
}

} // namespace

void RegisterObjImporterUVE(IAssetImporterUVE& importer) {
    importer.RegisterImporterUVE("obj", &ImportObjSourceUVE);
}

} // namespace UVE::Asset
