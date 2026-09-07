# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# The reference module's own CMakeLists.txt links uve_core PUBLIC alongside uve_asset, but
# neither plugin_manifest_validation_uve.* nor plugin_registry_uve.* nor their test suite
# reference uve/core, EngineCoreUVE, or the UVE::Core namespace anywhere (verified by direct
# grep) - that link is not needed for this module to compile and is not carried over here.
# uve_core itself (EngineCoreUVE, the whole-engine bootstrap class) remains unported.

add_library(uve_plugins STATIC
    src/plugin_manifest_validation_uve.cpp
    src/plugin_registry_uve.cpp
)
target_include_directories(uve_plugins PUBLIC include)
target_link_libraries(uve_plugins PUBLIC uve_asset PRIVATE nlohmann_json::nlohmann_json)
target_compile_features(uve_plugins PUBLIC cxx_std_20)
uve_set_warnings(uve_plugins)
