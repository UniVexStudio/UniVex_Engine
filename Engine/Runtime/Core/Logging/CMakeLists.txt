# Copyright (c) 2026 UniVex Studios. All Rights Reserved.


add_library(uve_logging STATIC
    Internal/log_sink_uve.cpp
    Internal/logger_uve.cpp
)
target_include_directories(uve_logging PUBLIC Expose)
target_link_libraries(uve_logging PUBLIC uve_platform)
target_compile_features(uve_logging PUBLIC cxx_std_20)
uve_set_warnings(uve_logging)
uve_link_format_backend(uve_logging)
