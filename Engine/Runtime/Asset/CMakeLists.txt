# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Started as a minimal partial port of UNIVEX-ENGINE's asset system - just AssetGuidUVE, the
# universal .uve* binary file envelope, and IAssetDatabaseUVE/AssetDatabaseUVE - then grew the
# runtime layer (AssetManagerUVE, FileSystemUVE/AssetBundleUVE, the per-type asset models) that
# Engine/Runtime/Render's MeshRendererUVE/Renderer3DUVE/ShaderManagerUVE need. Grown again here
# to add the format-importer pipeline EngineCoreUVE itself needs: the concrete AssetImporterUVE
# (which registers every format below in its own constructor - they cannot be split into a
# separate, smaller target, since AssetImporterUVE's single .cpp includes all of them) plus
# BMP/TGA/PNG/JPEG (image), OBJ/glTF/MTL (mesh/material, each with its own converter to
# MeshAssetUVE/MaterialAssetUVE), and a plain-text GLSL shader-source importer.
#
# ZLIB (PNG's own compressed-chunk decoding) and JPEG (libjpeg-turbo, matching the reference
# module's own find_package calls) are new external dependencies this module didn't need before.
#
# Still excludes HotReloadUVE (concrete), AssetImportQueueUVE, ProjectChangeWatcherUVE/
# ProjectFileIndexUVE, and the DataTable family - genuinely separate concerns from format
# conversion, not needed by AssetImporterUVE itself, deferred to a following slice.

find_package(ZLIB REQUIRED)
if(NOT ANDROID)
    find_package(JPEG REQUIRED)
endif()

add_library(uve_asset STATIC
    src/asset_guid_uve.cpp
    src/uve_file_envelope_uve.cpp
    src/asset_database_uve.cpp
    src/asset_content_fingerprint_uve.cpp
    src/asset_manager_uve.cpp
    src/asset_bundle_uve.cpp
    src/file_system_uve.cpp
    src/mesh_asset_uve.cpp
    src/texture_asset_uve.cpp
    src/shader_asset_uve.cpp
    src/material_asset_uve.cpp
    src/asset_importer_uve.cpp
    src/bmp_importer_uve.cpp
    src/bmp_metadata_uve.cpp
    src/tga_importer_uve.cpp
    src/tga_metadata_uve.cpp
    src/png_importer_uve.cpp
    src/png_metadata_uve.cpp
    src/jpeg_importer_uve.cpp
    src/jpeg_metadata_uve.cpp
    src/obj_importer_uve.cpp
    src/obj_mesh_converter_uve.cpp
    src/obj_metadata_uve.cpp
    src/gltf_importer_uve.cpp
    src/gltf_mesh_converter_uve.cpp
    src/gltf_metadata_uve.cpp
    src/mtl_importer_uve.cpp
    src/mtl_material_converter_uve.cpp
    src/mtl_metadata_uve.cpp
    src/shader_source_importer_uve.cpp
)
target_include_directories(uve_asset PUBLIC include)
target_link_libraries(uve_asset
    PUBLIC uve_debug uve_math uve_threading uve_events
    PRIVATE nlohmann_json::nlohmann_json ZLIB::ZLIB
)
if(NOT ANDROID)
    target_link_libraries(uve_asset PRIVATE JPEG::JPEG)
endif()
target_compile_features(uve_asset PUBLIC cxx_std_20)
uve_set_warnings(uve_asset)
