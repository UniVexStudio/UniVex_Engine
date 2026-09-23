// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include <string_view>

#include "uve/scripting/script_graph_uve.h"

namespace UVE::Scripting {

/// The scene node a script is attached to, as it appears on that script's canvas. It has no pins -
/// it stands for the owner, and the editor titles it with the owner's live name - and it compiles
/// to nothing, so a script holding only this node runs and does nothing.
inline constexpr std::string_view kSceneSelfScriptNodeTypeIdUVE = "scene.self";

/// Registers the engine-owned node descriptors exposed to the editor palette.
/// This catalog defines graph/pin contracts and identifies action nodes that require execution power;
/// pure value nodes remain data-driven and execution-independent.
[[nodiscard]] bool RegisterBuiltInScriptNodesUVE(ScriptNodeRegistryUVE& registry);

} // namespace UVE::Scripting

