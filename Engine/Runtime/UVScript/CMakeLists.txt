# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#
# UVScript: the text scripting language for entity nodes (see README.md). Standard library only.

add_library(uve_uvscript STATIC
    Internal/uvscript_parser_uve.cpp
    Internal/uvscript_value_uve.cpp
    Internal/uvscript_compiler_uve.cpp
    Internal/uvscript_instance_uve.cpp
)
target_include_directories(uve_uvscript PUBLIC Expose)
target_compile_features(uve_uvscript PUBLIC cxx_std_23)
uve_set_warnings(uve_uvscript)
