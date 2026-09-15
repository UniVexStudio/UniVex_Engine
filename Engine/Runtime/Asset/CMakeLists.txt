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
# Grown once more to add the pieces EngineCoreUVE itself constructs directly: HotReloadUVE
# (concrete, tracks imported files for change-triggered reimport), AssetImportQueueUVE
# (asynchronous import scheduling over IThreadPoolUVE), ProjectChangeWatcherUVE/
# ProjectFileIndexUVE (project-directory file watching/indexing), DerivedArtifactCacheUVE
# (import-output caching keyed by AssetContentFingerprintUVE), ResourceDependencyGraphUVE, and
# the DataTable family (DataTableUVE/DataTableAssetUVE/DataTableAssetManagerUVE/
# DataTableImporterUVE/DataTablePipelineUVE/DataTableRegistryUVE) - the last of these turned out
# to be a real dependency of AssetImportQueueUVE's own test suite, not an independent optional
# extra. AnimationClipAssetUVE (a small per-type asset model, same shape as MeshAssetUVE/
# TextureAssetUVE) is included alongside since nothing else references it.

# This now covers every file in the reference engine/asset/ module - the full ~12,098-line
# system is ported.
#
# audio_asset_uve.cpp (LoadAudioAssetUVE/SaveAudioAssetUVE) moved here from Audio's own local
# Expose/uve/asset/ tree while porting EngineCoreUVE - Audio previously only needed the header
# (the AudioAssetUVE value type) for WavImporterUVE::RegisterWavImporterUVE(), but EngineCoreUVE's
# own asset-loader registration path calls LoadAudioAssetUVE/SaveAudioAssetUVE directly, which
# were never actually defined anywhere in this repo until now - a genuine missing-definition
# link error, not a design choice, caught by the linker rather than by dependency-closure review.
# audio_asset_uve.h's own reference include path was always uve/asset/, matching every other
# asset model here (MeshAssetUVE, TextureAssetUVE, ShaderAssetUVE) - Audio still gets it through
# its existing uve_asset PUBLIC link, unchanged.

find_package(ZLIB REQUIRED)
if(NOT ANDROID)
    find_package(JPEG REQUIRED)
endif()

add_library(uve_asset STATIC
    Internal/asset_guid_uve.cpp
    Internal/audio_asset_uve.cpp
    Internal/uve_file_envelope_uve.cpp
    Internal/asset_database_uve.cpp
    Internal/asset_content_fingerprint_uve.cpp
    Internal/asset_manager_uve.cpp
    Internal/asset_bundle_uve.cpp
    Internal/file_system_uve.cpp
    Internal/mesh_asset_uve.cpp
    Internal/texture_asset_uve.cpp
    Internal/shader_asset_uve.cpp
    Internal/material_asset_uve.cpp
    Internal/asset_importer_uve.cpp
    Internal/bmp_importer_uve.cpp
    Internal/bmp_metadata_uve.cpp
    Internal/tga_importer_uve.cpp
    Internal/tga_metadata_uve.cpp
    Internal/png_importer_uve.cpp
    Internal/png_metadata_uve.cpp
    Internal/jpeg_importer_uve.cpp
    Internal/jpeg_metadata_uve.cpp
    Internal/obj_importer_uve.cpp
    Internal/obj_mesh_converter_uve.cpp
    Internal/obj_metadata_uve.cpp
    Internal/gltf_importer_uve.cpp
    Internal/gltf_mesh_converter_uve.cpp
    Internal/gltf_metadata_uve.cpp
    Internal/mtl_importer_uve.cpp
    Internal/mtl_material_converter_uve.cpp
    Internal/mtl_metadata_uve.cpp
    Internal/shader_source_importer_uve.cpp
    Internal/hot_reload_uve.cpp
    Internal/asset_import_queue_uve.cpp
    Internal/project_change_watcher_uve.cpp
    Internal/project_file_index_uve.cpp
    Internal/derived_artifact_cache_uve.cpp
    Internal/resource_dependency_graph_uve.cpp
    Internal/animation_clip_asset_uve.cpp
    Internal/data_table_uve.cpp
    Internal/data_table_asset_uve.cpp
    Internal/data_table_asset_manager_uve.cpp
    Internal/data_table_asset_registration_uve.cpp
    Internal/data_table_importer_uve.cpp
    Internal/data_table_pipeline_uve.cpp
    Internal/data_table_registry_uve.cpp
)
target_include_directories(uve_asset PUBLIC Expose)
target_link_libraries(uve_asset
    PUBLIC uve_debug uve_math uve_threading uve_events
    PRIVATE nlohmann_json::nlohmann_json ZLIB::ZLIB
)
if(NOT ANDROID)
    target_link_libraries(uve_asset PRIVATE JPEG::JPEG)
endif()
target_compile_features(uve_asset PUBLIC cxx_std_20)
uve_set_warnings(uve_asset)
