// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_ai_nodes.h"

#include "blueprint_node_builder.h"

namespace UVE::Blueprint::Nodes {

void RegisterAiBlueprintNodes(std::vector<Asset::BlueprintNodeType>& out) {
    using T = BlueprintNodeBuilder::T;
    using Pin = BlueprintNodeBuilder::Pin;
    BlueprintNodeBuilder b{out};

    b.Action("ai_move_to", "AI Move To", "AI", {Pin{"Destination", T::Vector, "0,0,0"}}, "ai.move_to({1})", "navigate path pathfinding walk go",
             "Its Nav Seeker walks it there over the navigation mesh");
    b.Action("ai_stop", "Stop Movement", "AI", {}, "ai.stop()", "halt stop navigation");
    b.Pure("ai_reached", "Has Reached Destination", "AI", {}, {Pin{"Reached", T::Bool, ""}}, {"ai.reached()"}, "arrived finished");
    b.Pure("ai_status", "Get Path Status", "AI", {}, {Pin{"Status", T::String, ""}}, {"ai.status()"}, "Idle Following Finished Failed");
}

} // namespace UVE::Blueprint::Nodes
