# Copyright (c) 2026 UniVex Studios. All Rights Reserved.


add_library(uve_math STATIC
    Internal/vector2.cpp
    Internal/vector3.cpp
    Internal/vector4.cpp
    Internal/quaternion.cpp
    Internal/matrix4x4.cpp
    Internal/point_transform_batch.cpp
    Internal/matrix3x3.cpp
    Internal/trs.cpp
    Internal/color.cpp
    Internal/vector2i.cpp
    Internal/vector3i.cpp
    Internal/rect.cpp
    Internal/rect_int.cpp
    Internal/aabb.cpp
    Internal/sphere.cpp
    Internal/oriented_box.cpp
    Internal/frustum.cpp
)
target_include_directories(uve_math PUBLIC Expose)
target_link_libraries(uve_math PUBLIC uve_platform)
target_compile_features(uve_math PUBLIC cxx_std_23)
uve_set_warnings(uve_math)
