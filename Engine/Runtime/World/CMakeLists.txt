# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# New (not ported) - see world_uve.h's doc comment for why. Owns one EntityManagerUVE and drives
# its per-frame update via SceneGraphUVE, so Scene stays non-ticking.

add_library(uve_world STATIC
    src/world_uve.cpp
)
target_include_directories(uve_world PUBLIC include)
target_link_libraries(uve_world PUBLIC uve_scene uve_entity uve_memory uve_events uve_debug)
target_compile_features(uve_world PUBLIC cxx_std_20)
uve_set_warnings(uve_world)
