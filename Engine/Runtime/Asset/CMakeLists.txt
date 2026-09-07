# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Started as a minimal partial port of UNIVEX-ENGINE's asset system - just AssetGuidUVE, the
# universal .uve* binary file envelope, and IAssetDatabaseUVE/AssetDatabaseUVE - grown here to
# add the runtime layer Engine/Runtime/Render's MeshRendererUVE/Renderer3DUVE/ShaderManagerUVE
# need: AssetManagerUVE (generic AssetHandleUVE<T> load/reload lifecycle over IAssetDatabaseUVE),
# FileSystemUVE/AssetBundleUVE (mounted read-only asset bundle access), and the per-type asset
# models (MeshAssetUVE, TextureAssetUVE, ShaderAssetUVE, MaterialAssetUVE, BlobAssetUVE).
#
# Deliberately still excludes every format importer (bmp/tga/png/obj/gltf/jpeg/mtl/data-table,
# ~10,000 of the reference module's ~12,098 lines) - none of AssetManagerUVE/FileSystemUVE/the
# per-type asset models reference them; they only ever consume already-converted .uve* envelope
# files (see uve_file_envelope_uve.h), independent of how those files were produced. Importers
# remain out of scope until something needs offline content conversion, not just runtime loading.

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
)
target_include_directories(uve_asset PUBLIC include)
target_link_libraries(uve_asset
    PUBLIC uve_debug uve_math uve_threading uve_events
    PRIVATE nlohmann_json::nlohmann_json
)
target_compile_features(uve_asset PUBLIC cxx_std_20)
uve_set_warnings(uve_asset)
