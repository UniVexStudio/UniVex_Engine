# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# UIRuntimeUVE - per-frame reconciliation of authored screen-space UI (Canvas/UIText/UIImage/
# UIButton, Engine/Runtime/Component) into real button hit-testing against IInputSystemUVE and a
# plain-data UIDrawBatchUVE snapshot. No GPU resource is created or touched here - that lands in
# a later phase once this logic layer is real and tested. UIFontAtlasUVE bakes the embedded
# Liberation Sans subset (assets/fonts/, see its own THIRD_PARTY_NOTICES.md) into a glyph bitmap
# via vendored stb_truetype.h (thirdparty/) - see that file's own notice entry for provenance.
#
# stb_truetype.h's implementation lives in its own translation unit (Internal/stb_truetype_impl.cpp),
# compiled with relaxed warnings - see that file's own comment and the per-file override below -
# so this repo's strict -Werror set never applies to unmodified vendored third-party code.

add_library(uve_ui STATIC
    Internal/stb_truetype_impl.cpp
    Internal/ui_font_atlas_uve.cpp
    Internal/ui_runtime_uve.cpp
)

# ui_font_ttf_bytes.inc is generated at build time from the module's own canonical
# assets/fonts/liberation-sans-subset.ttf (the single source of truth for this font for the
# whole repo - Engine/Editor/EditorCore embeds the same file from here, see that module's
# CMakeLists). Generating it here means the byte array never drifts from the checked-in
# asset; 2026-09-17 audit.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_uve_ui_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(_uve_ui_font_inc "${_uve_ui_generated_dir}/ui_font_ttf_bytes.inc")
add_custom_command(
    OUTPUT "${_uve_ui_font_inc}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Engine/Tools/embed_file.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/liberation-sans-subset.ttf"
            "${_uve_ui_font_inc}"
            uve_ui_runtime_font_ttf_bytes
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/liberation-sans-subset.ttf"
            "${CMAKE_SOURCE_DIR}/Engine/Tools/embed_file.py"
    COMMENT "Embedding liberation-sans-subset.ttf -> ui_font_ttf_bytes.inc"
    VERBATIM
)
target_sources(uve_ui PRIVATE "${_uve_ui_font_inc}")
target_include_directories(uve_ui PRIVATE "${_uve_ui_generated_dir}")

target_include_directories(uve_ui PUBLIC Expose)
target_include_directories(uve_ui PRIVATE thirdparty)
target_link_libraries(uve_ui PUBLIC uve_math uve_asset uve_component uve_entity uve_input uve_localization)
target_compile_features(uve_ui PUBLIC cxx_std_20)
uve_set_warnings(uve_ui)

set_source_files_properties(Internal/stb_truetype_impl.cpp PROPERTIES
    COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:GNU,Clang>:-w>"
)
