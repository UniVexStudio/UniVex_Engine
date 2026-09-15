// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/scene/nodes/animation_player_node_uve.h"
#include "uve/scene/nodes/animation_tree_node_uve.h"
#include "uve/scene/nodes/area_3d_node_uve.h"
#include "uve/scene/nodes/audio_source_3d_node_uve.h"
#include "uve/scene/nodes/box_mesh_3d_node_uve.h"
#include "uve/scene/nodes/camera_3d_node_uve.h"
#include "uve/scene/nodes/character_body_3d_node_uve.h"
#include "uve/scene/nodes/collision_shape_3d_node_uve.h"
#include "uve/scene/nodes/empty_node_uve.h"
#include "uve/scene/nodes/light_3d_node_uve.h"
#include "uve/scene/nodes/mesh_instance_3d_node_uve.h"
#include "uve/scene/nodes/particle_emitter_3d_node_uve.h"
#include "uve/scene/nodes/plane_mesh_3d_node_uve.h"
#include "uve/scene/nodes/rigid_body_3d_node_uve.h"
#include "uve/scene/nodes/static_body_3d_node_uve.h"
#include "uve/scene/nodes/script_node_uve.h"
#include "uve/scene/nodes/sphere_mesh_3d_node_uve.h"
#include "uve/scene/nodes/transform_node_uve.h"
#include "uve/scene/nodes/scene_node_registry_uve.h"
// The 21 node types that used to live behind their own thin compatibility-alias facade here
// (RayCast3D, Skeleton3D, Hitbox3D, WorldEnvironment3D, etc.) now have their real struct
// definitions directly in Engine/Runtime/Nodes/3D - nothing in this module aliases them anymore,
// consumers use the real type names from this one aggregate header instead.
#include "uve/nodes/3d/all_nodes_3d_uve.h"

namespace UVE::Scene::Nodes {

// Each concrete user-facing node and its compatibility façade alias is defined by its own
// discoverable header in this folder; this header remains the aggregate include surface.
} // namespace UVE::Scene::Nodes
