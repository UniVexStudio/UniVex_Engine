// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <vector>

#include "blueprint_nodes.h"

namespace UVE::Blueprint::Nodes {

/// Registers Math Blueprint nodes (Float, Int, Boolean, Compare, Trig, Vector, Random).
void RegisterMathBlueprintNodes(std::vector<Asset::BlueprintNodeType>& out);

} // namespace UVE::Blueprint::Nodes
