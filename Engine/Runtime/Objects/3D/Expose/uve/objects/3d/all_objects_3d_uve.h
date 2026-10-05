// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

// General 3D authored data outside the Animation, AI, and physics subdomains:
#include "uve/objects/3d/abstract_objects_3d_uve.h"
#include "uve/objects/3d/decal_3d_uve.h"
#include "uve/objects/3d/directional_light_3d_uve.h"
#include "uve/objects/3d/fog_volume_3d_uve.h"
#include "uve/objects/3d/level_streamer_3d_uve.h"
#include "uve/objects/3d/lod_group_3d_uve.h"
#include "uve/objects/3d/marker_3d_uve.h"
#include "uve/objects/3d/occluder_3d_uve.h"
#include "uve/objects/3d/reflection_probe_3d_uve.h"
#include "uve/objects/3d/spawn_point_3d_uve.h"
#include "uve/objects/3d/visibility_region_3d_uve.h"
#include "uve/objects/3d/world_environment_3d_uve.h"
#include "uve/objects/3d/world_partition_3d_uve.h"

// Animation, AI/navigation, and physics objects are grouped by their domain.
#include "uve/objects/3d/all_animation_objects_3d_uve.h"
#include "uve/objects/3d/all_ai_objects_3d_uve.h"
#include "uve/objects/3d/all_physics_objects_3d_uve.h"

// Remaining creation recipes for kinds whose authored state lives in shared components:
#include "uve/objects/3d/object_3d_uve.h"
#include "uve/objects/3d/camera_3d_uve.h"
#include "uve/objects/3d/mesh_instance_3d_uve.h"
#include "uve/objects/3d/box_mesh_3d_uve.h"
#include "uve/objects/3d/sphere_mesh_3d_uve.h"
#include "uve/objects/3d/plane_mesh_3d_uve.h"
#include "uve/objects/3d/light_3d_uve.h"
#include "uve/objects/3d/audio_source_3d_uve.h"
#include "uve/objects/3d/particle_emitter_3d_uve.h"
#include "uve/objects/3d/script_uve.h"
