// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

// Every scene object kind's real backing type, reachable from one include - no compatibility-alias
// facade layer anymore. That layer (a "using XObjectUVE = XComponentUVE;" file per object type) was
// removed once it was confirmed nothing in the codebase actually used the alias names: every real
// consumer already reaches for the component/type name directly (LightComponentUVE, not
// Light3DObjectUVE). Keeping this single aggregate header instead - it documents which real type
// backs every UVE::Scene::Objects::SceneObjectKindUVE, matching scene_object_registry_uve.cpp's own
// runtimeOwner field, without inventing a second name for anything.
#include "uve/physics/character_controller_uve.h"
#include "uve/component/animation_graph_component_uve.h"
#include "uve/component/animation_sequencer_component_uve.h"
#include "uve/component/area_component_uve.h"
#include "uve/component/audio_source_component_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/particle_emitter_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/rigid_3d_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/scene/objects/scene_object_registry_uve.h"
// The 21 object types that used to live behind their own thin compatibility-alias facade here
// (RayCast3D, Skeleton3D, Hitbox3D, WorldEnvironment3D, etc.) have their real struct definitions
// directly in Engine/Runtime/Objects/3D — and the 17 kinds whose data already lives in a shared
// component (Camera3D, Light3D, the primitive meshes, the physics bodies, Script, etc.) each
// have a ObjectDefinition file there instead: the kind's creation recipe (components to attach,
// authored defaults, default entity name), so no object kind's behavior is buried in one shared
// header or in the editor's creation switch.
#include "uve/objects/3d/all_objects_3d_uve.h"
#include "uve/objects/canvas/all_objects_canvas_uve.h"
