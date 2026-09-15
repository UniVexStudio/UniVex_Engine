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
target_include_directories(uve_ui PUBLIC Expose)
target_include_directories(uve_ui PRIVATE thirdparty)
target_link_libraries(uve_ui PUBLIC uve_math uve_asset uve_component uve_entity uve_input)
target_compile_features(uve_ui PUBLIC cxx_std_20)
uve_set_warnings(uve_ui)

set_source_files_properties(Internal/stb_truetype_impl.cpp PROPERTIES
    COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:GNU,Clang>:-w>"
)
