# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Header-only: EntityUVE (generational handle), the ComponentUVE concept, ComponentTypeInfoUVE
# (type-erased construct/move/destroy descriptor), and every concrete built-in component type.
# None of these have a matching .cpp - see engine/scene's prior CMakeLists.txt history for the
# module this was split out of. Depends only on Core-level modules (math, debug, asset for
# PrefabInstanceComponentUVE's asset GUID reference) and never on Entity, so EntityUVE's manager
# (Engine/Runtime/Entity) can depend on this module without a cycle.

add_library(uve_component INTERFACE)
target_include_directories(uve_component INTERFACE include)
target_link_libraries(uve_component INTERFACE uve_math uve_asset)
target_compile_features(uve_component INTERFACE cxx_std_20)
