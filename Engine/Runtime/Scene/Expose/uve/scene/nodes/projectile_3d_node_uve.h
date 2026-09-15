// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/nodes/3d/projectile_3d_uve.h"

namespace UVE::Scene::Nodes {

/// User-facing Projectile3D state. Collision and movement integration remain caller-owned services.
using Projectile3DNodeUVE = Projectile3DNodeComponentUVE;
using Projectile3DNodeFacadeUVE = Projectile3DNodeUVE;

} // namespace UVE::Scene::Nodes
