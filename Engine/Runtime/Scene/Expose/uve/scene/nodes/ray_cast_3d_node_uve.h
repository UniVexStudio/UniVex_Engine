// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/nodes/3d/ray_cast_3d_uve.h"

namespace UVE::Scene::Nodes {

/// User-facing RayCast3D state. IRaycastSystemUVE remains the stateless query authority.
using RayCast3DNodeUVE = RayCast3DNodeComponentUVE;
using RayCast3DNodeFacadeUVE = RayCast3DNodeUVE;

} // namespace UVE::Scene::Nodes
