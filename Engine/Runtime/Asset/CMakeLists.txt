# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Minimal, honest partial port: only AssetGuidUVE (a small, dependency-free 64-bit asset
# identifier), because Engine/Runtime/Component's PrefabInstanceComponentUVE needs it to
# reference a prefab asset by id. The rest of UNIVEX-ENGINE's asset system (AssetDatabaseUVE,
# AssetManagerUVE, importers, file envelopes, etc.) is out of scope for this slice and stays in
# the reference repo - this is not a stand-in for that system, just the one type this slice
# actually needs.

add_library(uve_asset STATIC
    src/asset_guid_uve.cpp
)
target_include_directories(uve_asset PUBLIC include)
target_compile_features(uve_asset PUBLIC cxx_std_20)
uve_set_warnings(uve_asset)
