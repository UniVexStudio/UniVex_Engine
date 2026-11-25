// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_gameplay_nodes.h"

#include "blueprint_node_builder.h"

namespace UVE::Blueprint::Nodes {

void RegisterGameplayBlueprintNodes(std::vector<Asset::BlueprintNodeType>& out) {
    using T = BlueprintNodeBuilder::T;
    using Pin = BlueprintNodeBuilder::Pin;
    BlueprintNodeBuilder b{out};

    b.Pure("attr_has", "Has Attribute", "Gameplay|Attributes", {Pin{"Attribute", T::String, "stamina"}}, {Pin{"Has", T::Bool, ""}}, {"attributes.has({0})"});
    b.Pure("attr_get", "Get Attribute", "Gameplay|Attributes", {Pin{"Attribute", T::String, "stamina"}}, {Pin{"Value", T::Float, ""}}, {"attributes.get({0})"}, "health stat");
    b.Pure("attr_max", "Get Attribute Max", "Gameplay|Attributes", {Pin{"Attribute", T::String, "stamina"}}, {Pin{"Max", T::Float, ""}}, {"attributes.max({0})"});
    b.Action("attr_damage", "Damage", "Gameplay|Attributes", {Pin{"Attribute", T::String, "stamina"}, Pin{"Amount", T::Float, "10.0"}},
             "attributes.damage({1}, {2})", "hurt hit");
    b.Action("attr_heal", "Heal", "Gameplay|Attributes", {Pin{"Attribute", T::String, "stamina"}, Pin{"Amount", T::Float, "10.0"}},
             "attributes.heal({1}, {2})", "restore");
    b.Pure("tag_has", "Has Tag", "Gameplay|Tags", {Pin{"Tag", T::String, ""}}, {Pin{"Has", T::Bool, ""}}, {"tags.has({0})"});
    b.Action("tag_add", "Add Tag", "Gameplay|Tags", {Pin{"Tag", T::String, ""}}, "tags.add({1})");
    b.Action("tag_remove", "Remove Tag", "Gameplay|Tags", {Pin{"Tag", T::String, ""}}, "tags.remove({1})");
}

} // namespace UVE::Blueprint::Nodes
