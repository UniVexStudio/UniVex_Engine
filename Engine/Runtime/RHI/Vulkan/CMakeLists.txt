# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#
# The Vulkan M1 "bootstrap" backend for uve_rhi's IRenderDeviceUVE (AUDIT.md section 8, item 2):
# instance -> physical/logical device -> window surface -> swapchain -> per-frame clear-color
# present, with every resource-bearing interface method honestly reporting "later milestone"
# (see vulkan_render_device_uve.h for the full milestone contract).
#
# Two deliberate dependency choices, both in this module's audit-confinement tradition (the GL
# header confinement of audit #37 and GlFunctionsUVE's minimal-hand-rolled-loader precedent):
#
# 1. NO libvulkan link: the loader is dlopen()ed at runtime by VkFunctionsUVE. This keeps the
#    binary startable on hosts with no Vulkan deployment (CI runs both with and without the
#    lavapipe ICD), matches the engine's documented fallback philosophy, and costs the build
#    system zero Vulkan SDK discovery.
#
# 2. Vk *types* still need authoritative definitions, so the Khronos Vulkan-Headers repo is
#    fetched at a pinned tag as headers-only. Its include dir is added SYSTEM+PRIVATE: Vulkan
#    header syntax never leaks into this module's public headers (IVulkanWindowSurfaceUVE
#    coves the boundary using uintptr_t handles), and SYSTEM silences upstream warnings.

FetchContent_Declare(
    VulkanHeaders
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG vulkan-sdk-1.4.357.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(VulkanHeaders)

add_library(uve_rhi_vulkan STATIC
    Internal/vk_functions_uve.cpp
    Internal/vulkan_render_device_uve.cpp
)
target_include_directories(uve_rhi_vulkan PUBLIC Expose)
# The Vulkan-Headers include dir is added SYSTEM (silence upstream's old-style-cast-heavy
# compatibility macros under this repo's -Werror regime) and PRIVATE (never visible to consumers
# of uve_rhi_vulkan, preserving the uintptr_t confinement at the public boundary).
target_include_directories(uve_rhi_vulkan SYSTEM PRIVATE ${vulkanheaders_SOURCE_DIR}/include)
target_link_libraries(uve_rhi_vulkan
    PUBLIC uve_rhi uve_window uve_platform uve_logging
    PRIVATE ${CMAKE_DL_LIBS}
)
target_compile_features(uve_rhi_vulkan PUBLIC cxx_std_20)
uve_set_warnings(uve_rhi_vulkan)
