# Copyright (c) 2026 UniVex Studios. All Rights Reserved.


add_library(uve_math STATIC
    Internal/vector2_uve.cpp
    Internal/vector3_uve.cpp
    Internal/vector4_uve.cpp
    Internal/quaternion_uve.cpp
    Internal/matrix4x4_uve.cpp
    Internal/point_transform_batch_uve.cpp
    Internal/matrix3x3_uve.cpp
    Internal/trs_uve.cpp
    Internal/color_uve.cpp
    Internal/vector2i_uve.cpp
    Internal/vector3i_uve.cpp
    Internal/rect_uve.cpp
    Internal/rect_int_uve.cpp
    Internal/aabb_uve.cpp
    Internal/sphere_uve.cpp
    Internal/oriented_box_uve.cpp
    Internal/frustum_uve.cpp
)
target_include_directories(uve_math PUBLIC Expose)
target_link_libraries(uve_math PUBLIC uve_platform)
target_compile_features(uve_math PUBLIC cxx_std_23)
uve_set_warnings(uve_math)
