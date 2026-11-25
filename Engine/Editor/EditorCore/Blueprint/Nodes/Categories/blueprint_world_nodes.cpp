// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_world_nodes.h"

#include "blueprint_node_builder.h"

namespace UVE::Blueprint::Nodes {

void RegisterWorldBlueprintNodes(std::vector<Asset::BlueprintNodeType>& out) {
    using T = BlueprintNodeBuilder::T;
    using Pin = BlueprintNodeBuilder::Pin;
    BlueprintNodeBuilder b{out};

    // World: spawning, finding, removing
    b.ValuedAction("spawn_entity", "Spawn Entity", "World",
                   {Pin{"Entity", T::String, "Entities/Cube.uventity"}, Pin{"Location", T::Vector, "0,0,0"}, Pin{"Rotation", T::Vector, "0,0,0"}},
                   {Pin{"Return Value", T::Object, ""}}, {"world.spawn({1}, {2}, {3})"}, "spawn actor create instantiate prefab",
                   "Makes an entity or prefab file's objects at a place");
    b.Action("destroy_object", "Destroy Object", "World", {Pin{"Object", T::Object, ""}}, "world.destroy({1})", "destroy actor remove delete kill",
             "Removes the object (and what is under it) at the end of the frame");
    b.Action("destroy_self", "Destroy Self", "World", {}, "world.destroy_self()", "destroy actor remove delete kill this");
    b.Action("set_life_span", "Set Life Span", "World", {Pin{"Seconds", T::Float, "5.0"}}, "world.set_life_span({1})", "lifetime expire auto destroy",
             "Removes this object after so many seconds (0 keeps it)");
    b.Pure("get_self", "Self", "World", {}, {Pin{"Self", T::Object, ""}}, {"world.self()"}, "this me reference", "This object");
    b.Pure("find_object_with_tag", "Get Object With Tag", "World", {Pin{"Tag", T::String, "Enemy"}}, {Pin{"Object", T::Object, ""}},
           {"world.find_tag({0})"}, "find first actor tag", "The first object carrying the tag (Gameplay Tags)");
    b.Pure("object_is_valid", "Is Valid Object", "World", {Pin{"Object", T::Object, ""}}, {Pin{"Valid", T::Bool, ""}}, {"world.is_valid({0})"},
           "exists alive none null");
    b.Pure("find_all_with_tag", "Get All Objects With Tag", "World", {Pin{"Tag", T::String, "Enemy"}}, {Pin{"Objects", T::ObjectArray, ""}},
           {"world.find_all_tag({0})"}, "find all actors of tag every", "Every object carrying the tag");
    b.Pure("find_all_by_name", "Get All Objects Named", "World", {Pin{"Name Starts With", T::String, "Enemy"}}, {Pin{"Objects", T::ObjectArray, ""}},
           {"world.find_all_name({0})"}, "find all actors name prefix", "Every object whose name starts so");

    // Objects: inspecting & manipulating another object
    b.Pure("object_get_name", "Get Object Name", "Objects", {Pin{"Object", T::Object, ""}}, {Pin{"Name", T::String, ""}}, {"object.name({0})"},
           "display name label");
    b.Pure("object_get_location", "Get Object Location", "Objects", {Pin{"Object", T::Object, ""}}, {Pin{"Location", T::Vector, ""}},
           {"object.get_position({0})"}, "position actor location");
    b.Action("object_set_location", "Set Object Location", "Objects", {Pin{"Object", T::Object, ""}, Pin{"Location", T::Vector, "0,0,0"}},
             "object.set_position({1}, {2})", "position actor teleport move");
    b.Pure("object_get_rotation", "Get Object Rotation", "Objects", {Pin{"Object", T::Object, ""}}, {Pin{"Rotation", T::Vector, ""}},
           {"object.get_rotation({0})"}, "euler angles");
    b.Action("object_set_rotation", "Set Object Rotation", "Objects", {Pin{"Object", T::Object, ""}, Pin{"Rotation", T::Vector, "0,0,0"}},
             "object.set_rotation({1}, {2})", "euler angles turn");
    b.Pure("object_get_scale", "Get Object Scale", "Objects", {Pin{"Object", T::Object, ""}}, {Pin{"Scale", T::Vector, ""}}, {"object.get_scale({0})"},
           "size");
    b.Action("object_set_scale", "Set Object Scale", "Objects", {Pin{"Object", T::Object, ""}, Pin{"Scale", T::Vector, "1,1,1"}},
             "object.set_scale({1}, {2})", "size");
    b.Action("object_set_visible", "Set Object Visible", "Objects", {Pin{"Object", T::Object, ""}, Pin{"Visible", T::Bool, "true"}},
             "object.set_visible({1}, {2})", "hide show");
    b.Pure("object_has_tag", "Object Has Tag", "Objects", {Pin{"Object", T::Object, ""}, Pin{"Tag", T::String, "Enemy"}}, {Pin{"Has", T::Bool, ""}},
           {"object.has_tag({0}, {1})"}, "actor has tag");
    b.Action("object_add_tag", "Add Tag To Object", "Objects", {Pin{"Object", T::Object, ""}, Pin{"Tag", T::String, "Enemy"}}, "object.add_tag({1}, {2})");
    b.Action("object_remove_tag", "Remove Tag From Object", "Objects", {Pin{"Object", T::Object, ""}, Pin{"Tag", T::String, "Enemy"}},
             "object.remove_tag({1}, {2})");
    b.Pure("object_distance", "Get Distance Between", "Objects", {Pin{"A", T::Object, ""}, Pin{"B", T::Object, ""}}, {Pin{"Distance", T::Float, ""}},
           {"object.distance({0}, {1})"}, "far near range");
    b.Action("object_attach", "Attach To Object", "Objects", {Pin{"Child", T::Object, ""}, Pin{"Parent", T::Object, ""}}, "object.attach({1}, {2})",
             "parent socket attach actor", "Puts the child under the parent: it moves with it");
    b.Action("object_detach", "Detach From Parent", "Objects", {Pin{"Object", T::Object, ""}}, "object.detach({1})", "unparent detach");
    b.Pure("object_get_parent", "Get Parent", "Objects", {Pin{"Object", T::Object, ""}}, {Pin{"Parent", T::Object, ""}}, {"object.parent({0})"},
           "owner attach parent");
    b.Pure("object_get_children", "Get Children", "Objects", {Pin{"Object", T::Object, ""}}, {Pin{"Children", T::ObjectArray, ""}},
           {"object.children({0})"}, "child attached components");
    b.Pure("find_object", "Find Object", "Objects", {Pin{"Name", T::String, ""}}, {Pin{"Object", T::Object, ""}}, {"node({0})"}, "get actor by name");
    b.Action("hide_object", "Hide Object", "Objects", {Pin{"Object", T::Object, ""}}, "{1}.hide()");
    b.Action("show_object", "Show Object", "Objects", {Pin{"Object", T::Object, ""}}, "{1}.show()");
}

} // namespace UVE::Blueprint::Nodes
