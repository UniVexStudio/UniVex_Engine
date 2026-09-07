# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Minimal, honest partial port of UNIVEX-ENGINE's asset system - only the pieces this slice's
# Component/Scene modules actually need: AssetGuidUVE (a small, dependency-free 64-bit asset
# identifier, needed by PrefabInstanceComponentUVE), the universal .uve* binary file envelope
# (needed by SceneSerializerUVE), and IAssetDatabaseUVE/AssetDatabaseUVE (a minimal
# GUID-to-file-path registry, needed by PrefabSystemUVE to resolve prefab references). The rest
# of the asset system (AssetManagerUVE, importers, bundles, etc.) is out of scope for this slice
# and stays in the reference repo - this is not a stand-in for that system.

add_library(uve_asset STATIC
    src/asset_guid_uve.cpp
    src/uve_file_envelope_uve.cpp
    src/asset_database_uve.cpp
    src/asset_content_fingerprint_uve.cpp
)
target_include_directories(uve_asset PUBLIC include)
target_link_libraries(uve_asset PUBLIC uve_debug PRIVATE nlohmann_json::nlohmann_json)
target_compile_features(uve_asset PUBLIC cxx_std_20)
uve_set_warnings(uve_asset)
