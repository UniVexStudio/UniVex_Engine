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
    Internal/type_metadata_uve.cpp
    Internal/variant_uve.cpp
)
target_include_directories(uve_object PUBLIC Expose)
# PUBLIC: variant_uve.h stores Math vector and quaternion types by value.
target_link_libraries(uve_object PUBLIC uve_math)
target_compile_features(uve_object PUBLIC cxx_std_23)
uve_set_warnings(uve_object)

#
# The Object itself, next to the reflection layer it belongs with. This is the object's core, so it
# lives in the object core folder rather than in the Scene module that happened to own it first:
#
#   object_uve.h        the Object - no transform, a Name, a hierarchy link, and the common Object
#                       section (Process, Thread Group, Physics Interpolation, Auto Translate,
#                       Editor Description, Script, Metadata) that every object of every kind
#                       carries - and the Object kind itself, marker and definition: the document's
#                       single root, which every Object3D in the level hangs from
#   scene_object_registry_uve.h  the catalogue of object kinds (id, name, category, contracts)
#   scene_folder_uve.h  the grouping Folder, and the Viewport the Outliner shows as the level root
#
# It is a second target, not more sources in uve_object, because the Object attaches components:
# uve_object sits below uve_component (Component links it) and cannot depend on it, while this
# layer sits above Component and Entity and below the object libraries - uve_objects_3d and
# uve_objects_canvas build every Object3D on top of it. The split keeps the direction one-way:
# reflection at the bottom, the Object above it, the kinds above that.
add_library(uve_objects_core STATIC
    Internal/object_uve.cpp
    Internal/scene_object_registry_uve.cpp
    Internal/scene_folder_uve.cpp
)
target_include_directories(uve_objects_core PUBLIC Expose)
# PUBLIC: every consumer that includes these headers needs the component structs (EntityUVE) and
# IEntityManagerUVE to compile against them.
target_link_libraries(uve_objects_core PUBLIC uve_math uve_component uve_entity)
target_compile_features(uve_objects_core PUBLIC cxx_std_23)
uve_set_warnings(uve_objects_core)
