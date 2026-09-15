// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/nodes/3d/level_streamer_3d_uve.h"

namespace UVE::Scene::Nodes {

/// User-facing LevelStreamer3D state. Loading/unloading is delegated to a future content service.
using LevelStreamer3DNodeUVE = LevelStreamer3DNodeComponentUVE;
using LevelStreamer3DNodeFacadeUVE = LevelStreamer3DNodeUVE;

} // namespace UVE::Scene::Nodes
