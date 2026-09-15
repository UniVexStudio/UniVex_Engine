// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/nodes/3d/spring_arm_3d_uve.h"

namespace UVE::Scene::Nodes {

/// User-facing SpringArm3D state. Collision shortening is delegated to the existing raycast seam.
using SpringArm3DNodeUVE = SpringArm3DNodeComponentUVE;
using SpringArm3DNodeFacadeUVE = SpringArm3DNodeUVE;

} // namespace UVE::Scene::Nodes
