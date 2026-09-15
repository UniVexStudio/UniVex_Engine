// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/nodes/3d/spawn_point_3d_uve.h"

namespace UVE::Scene::Nodes {

/// User-facing SpawnPoint3D state. Spawn selection and gameplay dispatch remain caller-owned.
using SpawnPoint3DNodeUVE = SpawnPoint3DNodeComponentUVE;
using SpawnPoint3DNodeFacadeUVE = SpawnPoint3DNodeUVE;

} // namespace UVE::Scene::Nodes
