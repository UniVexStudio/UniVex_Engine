// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/component/entity_uve.h"
#include "uve/scene/nodes/scene_node_registry_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// The node type an entity was created as. Stamped when a node is made, saved with the scene by
/// its stable type id, and copied with the rest of the node by duplicate, undo and prefabs.
///
/// It exists because the type cannot be read back from components alone: a StaticBody3D and a
/// Collider3D carry the same collider, and a Node3D, a BoxMesh3D and a SphereMesh3D share every
/// component but one. Without it, anything that wants to say what a node is guesses, and the
/// guesses disagree.
struct SceneNodeTypeComponentUVE final {
    Nodes::SceneNodeKindUVE kind = Nodes::SceneNodeKindUVE::Node3D;
};

/// True when `value` names a kind the node registry knows.
[[nodiscard]] bool IsSceneNodeTypeComponentValidUVE(const SceneNodeTypeComponentUVE& value) noexcept;

/// The node kind `entity` is: its stored type when it has one, otherwise InferSceneNodeKindUVE.
/// Node3D for a dead entity.
[[nodiscard]] Nodes::SceneNodeKindUVE ResolveSceneNodeKindUVE(const IEntityManagerUVE& entityManager,
                                                              EntityUVE entity);

/// The best reading of `entity`'s kind from its components alone, for nodes that have no stored
/// type: those in scenes saved before the type was stored, and entities made in code. Exact for
/// every kind with a component of its own; where two kinds are built from the same components
/// it picks one and says which in the implementation. Node3D when nothing more specific fits.
[[nodiscard]] Nodes::SceneNodeKindUVE InferSceneNodeKindUVE(const IEntityManagerUVE& entityManager,
                                                            EntityUVE entity);

/// Stores `kind` as `entity`'s node type, replacing any it had.
void SetSceneNodeKindUVE(IEntityManagerUVE& entityManager, EntityUVE entity, Nodes::SceneNodeKindUVE kind);

} // namespace UVE::Scene
