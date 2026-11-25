// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "obj_importer.h"

#include <cstddef>
#include <filesystem>
#include <string_view>
#include <vector>

#include "import_helpers.h"
#include "obj_material_import.h"

#include "i_asset_database.h"
#include "mesh_asset.h"
#include "obj_mesh_converter.h"
#include "logging_macros.h"

namespace UVE::Asset {
namespace {

constexpr const char* kObjImporterName = "ObjImporter";
constexpr std::string_view kObjTemporarySuffix = ".uve_obj_tmp";

[[nodiscard]] bool ImportObjSource(const std::filesystem::path& sourcePath,
                                      const std::filesystem::path& destinationPath,
                                      IAssetDatabase& assetDatabase,
                                      const AssetImportSettings& /*settings*/) {
    if (destinationPath.extension() != ".uvmodel") {
        UVE_ERROR("ObjImporter: destination \"{}\" must use the .uvmodel extension",
                  destinationPath.string());
        return false;
    }

    std::vector<std::byte> sourceBytes;
    if (!Detail::ReadBoundedSourceBytes(sourcePath, kObjImporterName, kMaximumObjMeshSourceBytes,
                                           sourceBytes)) {
        return false;
    }

    const std::string_view source(reinterpret_cast<const char*>(sourceBytes.data()), sourceBytes.size());
    MeshAsset mesh;
    // The geometry converter also reports the `usemtl` runs it saw, so the imported model is split
    // into the surfaces its author named rather than arriving as one buffer with one material.
    std::vector<ObjSurfaceRange> surfaceRanges;
    if (!ConvertObjMesh(source, mesh, &surfaceRanges)) {
        UVE_ERROR("ObjImporter: source \"{}\" failed bounded OBJ mesh conversion", sourcePath.string());
        return false;
    }
    // Materials and their textures are published BEFORE the mesh so a failure never leaves a
    // .uvmodel pointing at materials that do not exist. Recoverable material problems leave the
    // geometry intact; only the asset database refusing a published file aborts.
    if (!Detail::ImportObjMaterials(source, sourcePath, destinationPath, assetDatabase, mesh,
                                       surfaceRanges)) {
        return false;
    }
    return Detail::PublishAssetAtomically(
        destinationPath, kObjImporterName, kObjTemporarySuffix,
        [&mesh](const std::filesystem::path& temporaryPath) { return SaveMeshAsset(mesh, temporaryPath); });
}

} // namespace

void RegisterObjImporter(IAssetImporter& importer) {
    // Database-aware: the importer publishes the OBJ's MTL materials and textures beside the model
    // and needs their GUIDs before it writes the .uvmodel that references them.
    importer.RegisterDatabaseAwareImporter("obj", &ImportObjSource);
}

} // namespace UVE::Asset
