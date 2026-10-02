# 3D physics nodes

This folder owns the Object3D authoring data and creation recipes that directly represent or query
3D physics: bodies, areas, colliders, hit/hurt boxes, interaction areas, projectiles, ray casts,
and spring arms. Their public headers are under `Expose/` and their implementations are under
`Internal/`.

These files remain part of the `uve_nodes_3d` library; this is an organizational subfolder, not a
new physics backend or a separate CMake target. `uve_nodes_3d` exports both `Expose/` and
`Physics/Expose/`, so the existing public include paths (`uve/nodes/3d/...`) remain stable.

Physics simulation systems, collision solving, and queries remain in
`Engine/Runtime/Physics`. `Engine/Runtime/Objects/3D/Physics` describes the scene-node-facing layer
only; it does not own or duplicate the physics engine.
