# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# The headless/testing backend for uve_rhi's IRenderDeviceUVE/ICommandBufferUVE - NullRenderDeviceUVE
# and NullCommandBufferUVE (module-private) record calls instead of touching real GPU state, the
# same "always available, sandbox-safe" role NullWindowManagerUVE plays for Window. No GL/GLFW
# dependency at all.

add_library(uve_rhi_null STATIC
    src/null_command_buffer_uve.cpp
    src/null_render_device_uve.cpp
)
target_include_directories(uve_rhi_null PUBLIC include)
target_link_libraries(uve_rhi_null PUBLIC uve_rhi uve_debug)
target_compile_features(uve_rhi_null PUBLIC cxx_std_20)
uve_set_warnings(uve_rhi_null)
