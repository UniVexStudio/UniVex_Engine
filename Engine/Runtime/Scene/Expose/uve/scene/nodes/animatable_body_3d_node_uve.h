// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/nodes/3d/animatable_body_3d_uve.h"

namespace UVE::Scene::Nodes {

/// User-facing AnimatableBody3D state. Movement is a bounded handoff to future kinematic integration.
using AnimatableBody3DNodeUVE = AnimatableBody3DNodeComponentUVE;
using AnimatableBody3DNodeFacadeUVE = AnimatableBody3DNodeUVE;

} // namespace UVE::Scene::Nodes
