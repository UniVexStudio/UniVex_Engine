// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

// Every scene node kind's real backing type, reachable from one include - no compatibility-alias
// facade layer anymore. That layer (a "using XNodeUVE = XComponentUVE;" file per node type) was
// removed once it was confirmed nothing in the codebase actually used the alias names: every real
// consumer already reaches for the component/type name directly (LightComponentUVE, not
// Light3DNodeUVE). Keeping this single aggregate header instead - it documents which real type
// backs every UVE::Scene::Nodes::SceneNodeKindUVE, matching scene_node_registry_uve.cpp's own
// runtimeOwner field, without inventing a second name for anything.
#include "uve/core/animation_tree_uve.h"
#include "uve/physics/character_controller_uve.h"
#include "uve/scene/components/animation_player_component_uve.h"
#include "uve/scene/components/area_component_uve.h"
#include "uve/scene/components/audio_source_component_uve.h"
#include "uve/scene/components/camera_component_uve.h"
#include "uve/scene/components/collider_component_uve.h"
#include "uve/scene/components/light_component_uve.h"
#include "uve/scene/components/mesh_component_uve.h"
#include "uve/scene/components/particle_emitter_component_uve.h"
#include "uve/scene/components/primitive_mesh_component_uve.h"
#include "uve/scene/components/rigid_body_component_uve.h"
#include "uve/scene/components/script_component_uve.h"
#include "uve/scene/components/transform_component_uve.h"
#include "uve/scene/entity_uve.h"
#include "uve/scene/nodes/scene_node_registry_uve.h"
// The 21 node types that used to live behind their own thin compatibility-alias facade here
// (RayCast3D, Skeleton3D, Hitbox3D, WorldEnvironment3D, etc.) have their real struct definitions
// directly in Engine/Runtime/Nodes/3D.
#include "uve/nodes/3d/all_nodes_3d_uve.h"
