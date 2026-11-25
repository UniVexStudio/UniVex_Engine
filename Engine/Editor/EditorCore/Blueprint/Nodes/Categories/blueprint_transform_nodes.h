// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <vector>

#include "blueprint_nodes.h"

namespace UVE::Blueprint::Nodes {

/// Registers Transform Blueprint nodes (Position, Rotation, Scale, Directions, Visibility).
void RegisterTransformBlueprintNodes(std::vector<Asset::BlueprintNodeType>& out);

} // namespace UVE::Blueprint::Nodes
