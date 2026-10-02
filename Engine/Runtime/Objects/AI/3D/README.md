# AI / 3D nodes

This is the home for 3D-specific AI and navigation scene-node definitions. The first two types
are `NavigationAgent3D` and `NavigationRegion3D`; they currently hold validated authored data
only. A navmesh baker, pathfinder, and steering system have not been implemented yet.

The public headers keep their established `uve/nodes/3d/...` include paths. The existing
`uve_nodes_3d` target compiles these sources along with the other 3D node definitions, avoiding a
new target dependency cycle while the 3D node API is still a single library. Use
`all_ai_nodes_3d_uve.h` for the AI/navigation-specific aggregate; `all_nodes_3d_uve.h` includes it
for callers that need the full 3D node set.
