// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/nodes/3d/reflection_probe_3d_uve.h"

namespace UVE::Scene::Nodes {

/// User-facing ReflectionProbe3D state. Capture allocation and GPU ownership remain renderer-owned.
using ReflectionProbe3DNodeUVE = ReflectionProbe3DNodeComponentUVE;
using ReflectionProbe3DNodeFacadeUVE = ReflectionProbe3DNodeUVE;
using ReflectionProbeUpdateModeUVE = ::UVE::Scene::ReflectionProbeUpdateModeUVE;

} // namespace UVE::Scene::Nodes
