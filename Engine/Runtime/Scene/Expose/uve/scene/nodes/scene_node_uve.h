// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

// Every scene node kind's real backing type, reachable from one include - no compatibility-alias
// facade layer anymore. That layer (a "using XNodeUVE = XComponentUVE;" file per node type) was
// removed once it was confirmed nothing in the codebase actually used the alias names: every real
// consumer already reaches for the component/type name directly (LightComponentUVE, not
// Light3DNodeUVE). Keeping this single aggregate header instead - it documents which real type
// backs every UVE::Scene::Nodes::SceneNodeKindUVE, matching scene_node_registry_uve.cpp's own
// runtimeOwner field, without inventing a second name for anything.
#include "uve/animation/animation_tree_uve.h"
#include "uve/physics/character_controller_uve.h"
#include "uve/component/animation_player_component_uve.h"
#include "uve/component/area_component_uve.h"
#include "uve/component/audio_source_component_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/particle_emitter_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/rigid_body_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/scene/nodes/scene_node_registry_uve.h"
// The 21 node types that used to live behind their own thin compatibility-alias facade here
// (RayCast3D, Skeleton3D, Hitbox3D, WorldEnvironment3D, etc.) have their real struct definitions
// directly in Engine/Runtime/Nodes/3D — and the 17 kinds whose data already lives in a shared
// component (Camera3D, Light3D, the primitive meshes, the physics bodies, Script, etc.) each
// have a NodeDefinition file there instead: the kind's creation recipe (components to attach,
// authored defaults, default entity name), so no node kind's behavior is buried in one shared
// header or in the editor's creation switch.
#include "uve/nodes/3d/all_nodes_3d_uve.h"
#include "uve/nodes/canvas_layer/all_nodes_canvas_layer_uve.h"
