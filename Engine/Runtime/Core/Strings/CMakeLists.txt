# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

add_library(uve_strings STATIC
    Internal/string_id_uve.cpp
)
target_include_directories(uve_strings PUBLIC Expose)
# PRIVATE: only Internal/string_id_uve.cpp uses UVE_ASSERT; the header is dependency-free.
target_link_libraries(uve_strings PRIVATE uve_logging)
target_compile_features(uve_strings PUBLIC cxx_std_23)
uve_set_warnings(uve_strings)
