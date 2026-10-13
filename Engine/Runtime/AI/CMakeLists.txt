# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# NPC decision-making: a utility-AI framework (considerations, response curves, action selection
# with hysteresis) over per-entity blackboards. Perception (sight and hearing feeding blackboard
# entries) is the planned sibling that writes what brains read.

add_library(uve_ai STATIC
    Internal/utility_ai_uve.cpp
    Internal/perception_uve.cpp
)
target_include_directories(uve_ai PUBLIC Expose)
target_link_libraries(uve_ai PUBLIC uve_component)
target_compile_features(uve_ai PUBLIC cxx_std_23)
uve_set_warnings(uve_ai)
