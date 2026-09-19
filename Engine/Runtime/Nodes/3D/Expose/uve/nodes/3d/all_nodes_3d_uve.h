// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

// Node components (authored data structs) — the 21 kinds whose node-specific data has no shared
// component to live in:
#include "uve/nodes/3d/animatable_body_3d_uve.h"
#include "uve/nodes/3d/bone_attachment_3d_uve.h"
#include "uve/nodes/3d/decal_3d_uve.h"
#include "uve/nodes/3d/hitbox_3d_uve.h"
#include "uve/nodes/3d/hurtbox_3d_uve.h"
#include "uve/nodes/3d/interaction_area_3d_uve.h"
#include "uve/nodes/3d/level_streamer_3d_uve.h"
#include "uve/nodes/3d/lod_group_3d_uve.h"
#include "uve/nodes/3d/marker_3d_uve.h"
#include "uve/nodes/3d/navigation_agent_3d_uve.h"
#include "uve/nodes/3d/navigation_region_3d_uve.h"
#include "uve/nodes/3d/occluder_3d_uve.h"
#include "uve/nodes/3d/projectile_3d_uve.h"
#include "uve/nodes/3d/ray_cast_3d_uve.h"
#include "uve/nodes/3d/reflection_probe_3d_uve.h"
#include "uve/nodes/3d/skeleton_3d_uve.h"
#include "uve/nodes/3d/spawn_point_3d_uve.h"
#include "uve/nodes/3d/spring_arm_3d_uve.h"
#include "uve/nodes/3d/visibility_region_3d_uve.h"
#include "uve/nodes/3d/world_environment_3d_uve.h"
#include "uve/nodes/3d/world_partition_3d_uve.h"

// Node definitions (creation recipes) — the 17 kinds whose data already lives in a shared
// component (Engine/Runtime/Component); each definition holds that kind's components-to-attach,
// authored defaults, and default entity name in its own file, instead of those recipes living
// hardcoded in the editor's creation switch:
#include "uve/nodes/3d/empty_uve.h"
#include "uve/nodes/3d/area_3d_uve.h"
#include "uve/nodes/3d/static_body_3d_uve.h"
#include "uve/nodes/3d/character_body_3d_uve.h"
#include "uve/nodes/3d/camera_3d_uve.h"
#include "uve/nodes/3d/mesh_instance_3d_uve.h"
#include "uve/nodes/3d/box_mesh_3d_uve.h"
#include "uve/nodes/3d/sphere_mesh_3d_uve.h"
#include "uve/nodes/3d/plane_mesh_3d_uve.h"
#include "uve/nodes/3d/light_3d_uve.h"
#include "uve/nodes/3d/collider_3d_uve.h"
#include "uve/nodes/3d/rigid_body_3d_uve.h"
#include "uve/nodes/3d/audio_source_3d_uve.h"
#include "uve/nodes/3d/particle_emitter_3d_uve.h"
#include "uve/nodes/3d/script_uve.h"
#include "uve/nodes/3d/animation_player_uve.h"
#include "uve/nodes/3d/animation_tree_uve.h"
