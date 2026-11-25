// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <vector>

#include "blueprint_nodes.h"

namespace UVE::Blueprint::Nodes {

/// Registers built-in Event, Custom Event, and Timer Blueprint nodes.
void RegisterEventsBlueprintNodes(std::vector<Asset::BlueprintNodeType>& out);

} // namespace UVE::Blueprint::Nodes
