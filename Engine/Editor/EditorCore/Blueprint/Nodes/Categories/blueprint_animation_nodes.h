// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <vector>

#include "blueprint_nodes.h"

namespace UVE::Blueprint::Nodes {

/// Registers Animation, Montage, Animation Graph, and Animation Sequencer Blueprint nodes.
void RegisterAnimationBlueprintNodes(std::vector<Asset::BlueprintNodeType>& out);

} // namespace UVE::Blueprint::Nodes
