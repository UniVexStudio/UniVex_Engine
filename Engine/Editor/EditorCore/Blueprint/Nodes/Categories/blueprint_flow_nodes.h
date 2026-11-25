// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <vector>

#include "blueprint_nodes.h"

namespace UVE::Blueprint::Nodes {

/// Registers Flow Control Blueprint nodes (Branch, Sequence, Loops, Gates, Delay, Switch, ForEach).
void RegisterFlowBlueprintNodes(std::vector<Asset::BlueprintNodeType>& out);

} // namespace UVE::Blueprint::Nodes
