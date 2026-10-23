// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/asset/obj_importer_uve.h"

#include <cstddef>
#include <filesystem>
#include <string_view>
#include <vector>

#include "import_helpers_uve.h"
#include "obj_material_import_uve.h"

#include "uve/asset/i_asset_database_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/asset/obj_mesh_converter_uve.h"
#include "uve/logging/logging_macros_uve.h"

namespace UVE::Asset {
namespace {

constexpr const char* kObjImporterNameUVE = "ObjImporterUVE";
constexpr std::string_view kObjTemporarySuffixUVE = ".uve_obj_tmp";

[[nodiscard]] bool ImportObjSourceUVE(const std::filesystem::path& sourcePath,
                                      const std::filesystem::path& destinationPath,
                                      IAssetDatabaseUVE& assetDatabase,
                                      const AssetImportSettingsUVE& /*settings*/) {
    if (destinationPath.extension() != ".uvmodel") {
        UVE_ERROR("ObjImporterUVE: destination \"{}\" must use the .uvmodel extension",
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
    // The geometry converter also reports the `usemtl` runs it saw, so the imported model is split
    // into the surfaces its author named rather than arriving as one buffer with one material.
    std::vector<ObjSurfaceRangeUVE> surfaceRanges;
    if (!ConvertObjMeshUVE(source, mesh, &surfaceRanges)) {
        UVE_ERROR("ObjImporterUVE: source \"{}\" failed bounded OBJ mesh conversion", sourcePath.string());
        return false;
    }
    // Materials and their textures are published BEFORE the mesh so a failure never leaves a
    // .uvmodel pointing at materials that do not exist. Recoverable material problems leave the
    // geometry intact; only the asset database refusing a published file aborts.
    if (!Detail::ImportObjMaterialsUVE(source, sourcePath, destinationPath, assetDatabase, mesh,
                                       surfaceRanges)) {
        return false;
    }
    return Detail::PublishAssetAtomicallyUVE(
        destinationPath, kObjImporterNameUVE, kObjTemporarySuffixUVE,
        [&mesh](const std::filesystem::path& temporaryPath) { return SaveMeshAssetUVE(mesh, temporaryPath); });
}

} // namespace

void RegisterObjImporterUVE(IAssetImporterUVE& importer) {
    // Database-aware: the importer publishes the OBJ's MTL materials and textures beside the model
    // and needs their GUIDs before it writes the .uvmodel that references them.
    importer.RegisterDatabaseAwareImporterUVE("obj", &ImportObjSourceUVE);
}

} // namespace UVE::Asset
