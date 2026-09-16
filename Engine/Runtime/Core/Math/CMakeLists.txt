# Copyright (c) 2026 UniVex Studios. All Rights Reserved.


add_library(uve_math STATIC
    Internal/vector2_uve.cpp
    Internal/vector3_uve.cpp
    Internal/quaternion_uve.cpp
    Internal/matrix4x4_uve.cpp
    Internal/aabb_uve.cpp
    Internal/frustum_uve.cpp
)
target_include_directories(uve_math PUBLIC Expose)
target_link_libraries(uve_math PUBLIC uve_platform)
target_compile_features(uve_math PUBLIC cxx_std_20)
uve_set_warnings(uve_math)
