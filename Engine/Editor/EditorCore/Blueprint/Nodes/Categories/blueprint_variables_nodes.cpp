// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_variables_nodes.h"

#include <utility>

#include "blueprint_node_builder.h"

namespace UVE::Blueprint::Nodes {

void RegisterVariablesBlueprintNodes(std::vector<Asset::BlueprintNodeType>& out) {
    using T = BlueprintNodeBuilder::T;
    using Role = BlueprintNodeBuilder::Role;
    using Pin = BlueprintNodeBuilder::Pin;
    using NodeType = BlueprintNodeBuilder::NodeType;
    BlueprintNodeBuilder b{out};

    NodeType get;
    get.key = "get_variable";
    get.title = "Get";
    get.category = "Variables";
    get.role = Role::GetVariable;
    get.outputs = {Pin{"Value", T::Float, ""}};
    b.Add(std::move(get));

    NodeType set;
    set.key = "set_variable";
    set.title = "Set";
    set.category = "Variables";
    set.role = Role::SetVariable;
    set.inputs = {BlueprintNodeBuilder::Exec(), Pin{"Value", T::Float, ""}};
    set.outputs = {BlueprintNodeBuilder::Exec(), Pin{"Value", T::Float, ""}};
    b.Add(std::move(set));
}

} // namespace UVE::Blueprint::Nodes
