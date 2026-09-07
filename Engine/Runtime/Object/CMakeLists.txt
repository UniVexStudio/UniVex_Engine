# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Flat folder, not split into Object/Class/Reflection/Property/Metadata subfolders - verified
# against the real Unreal Engine source, whose CoreUObject reflection code (Object.h, Class.h,
# property types, MetaData.h) all lives flat in one UObject/ folder rather than split by concept
# (see the restructuring plan). Ports TypeMetadataRegistryUVE unchanged, and extends
# TypeMetadataPropertyUVE with real type-erased get/set accessors (MakePropertyUVE) - a genuine
# capability gap the reference repo's version had: it could describe a property but never
# actually read or write one.

add_library(uve_object STATIC
    src/type_metadata_uve.cpp
)
target_include_directories(uve_object PUBLIC include)
target_compile_features(uve_object PUBLIC cxx_std_20)
uve_set_warnings(uve_object)
