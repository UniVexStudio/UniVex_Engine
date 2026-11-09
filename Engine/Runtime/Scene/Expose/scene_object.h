// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

// Every scene object kind's real backing type, reachable from one include - no compatibility-alias
// facade layer anymore. That layer (a "using XObjectUVE = XComponentUVE;" file per object type) was
// removed once it was confirmed nothing in the codebase actually used the alias names: every real
// consumer already reaches for the component/type name directly (LightComponentUVE, not
// Light3DObjectUVE). Keeping this single aggregate header instead - it documents which real type
// backs every UVE::Scene::Objects::SceneObjectKindUVE, matching scene_object_registry_uve.cpp's own
// runtimeOwner field, without inventing a second name for anything.
#include "character_controller.h"
#include "animation_graph_component.h"
#include "animation_sequencer_component.h"
#include "area_component.h"
#include "audio_source_component.h"
#include "camera_2d_component.h"
#include "camera_component.h"
#include "collider_component.h"
#include "light_component.h"
#include "mesh_component.h"
#include "particle_emitter_component.h"
#include "primitive_mesh_component.h"
#include "rigid_3d_component.h"
#include "script_component.h"
#include "sprite_2d_component.h"
#include "transform_component.h"
#include "entity.h"
#include "scene_object_registry.h"
// The 21 object types that used to live behind their own thin compatibility-alias facade here
// (RayCast3D, Skeleton3D, Hitbox3D, WorldEnvironment3D, etc.) have their real struct definitions
// directly in Engine/Runtime/Objects/3D — and the 17 kinds whose data already lives in a shared
// component (Camera3D, Light3D, the primitive meshes, the physics bodies, Script, etc.) each
// have a ObjectDefinition file there instead: the kind's creation recipe (components to attach,
// authored defaults, default entity name), so no object kind's behavior is buried in one shared
// header or in the editor's creation switch.
#include "all_objects_3d.h"
#include "all_objects_canvas.h"
#include "all_objects_2d.h"
