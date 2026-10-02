# Engine/Runtime/Objects/AI

AI-facing scene nodes are organized by dimensionality. The `3D/` folder currently contains the
3D navigation authoring nodes, `NavigationAgent3D` and `NavigationRegion3D`. They validate and
serialize authored navigation data, but are not yet backed by pathfinding or steering systems.

Future AI node families should live under the matching dimension (`3D/`, `2D/`) rather than being
mixed into the general `Objects/3D` folder. Behavior trees, blackboards, perception, world queries,
crowd following, smart objects, and state trees are still planned; see
`SCENE_NODES_ROADMAP.md` at the repository root.
