# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# The locale, string table and fallback-chain lookup that AutoTranslateComponentUVE needs in order
# to mean anything. Deliberately small and deliberately real: the engine had no localization code
# at all, and a per-entity "translate this" switch with nothing behind it would have been a control
# that does nothing.
#
# Depends on no engine module. A translation lookup is a string operation, and keeping it free of
# engine dependencies is what lets the editor, the runtime and a future import tool all use the
# same one. nlohmann_json is linked PRIVATE, confined to parsing a string-table file.

add_library(uve_localization STATIC
    Internal/localization_uve.cpp
)
target_include_directories(uve_localization PUBLIC Expose)
target_link_libraries(uve_localization PRIVATE nlohmann_json::nlohmann_json)
target_compile_features(uve_localization PUBLIC cxx_std_20)
uve_set_warnings(uve_localization)
