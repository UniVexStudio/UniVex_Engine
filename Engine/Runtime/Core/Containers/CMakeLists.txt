# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

# uve_containers is header-only (INTERFACE): FixedArrayUVE and SmallVectorUVE are class
# templates, so there is nothing to compile here. Consumers get the Expose include directory
# plus uve_logging (for UVE_ASSERT in the bounds checks).
# NOTE: uve_set_warnings() is intentionally NOT applied — it sets PRIVATE compile options,
# which an INTERFACE library rejects. Warning hygiene for these headers is enforced at the
# warning-enabled consumers and test targets instead.
add_library(uve_containers INTERFACE)
target_include_directories(uve_containers INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/Expose
)
target_link_libraries(uve_containers INTERFACE uve_logging)
