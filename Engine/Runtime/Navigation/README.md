# Navigation

The navmesh a `NavigationRegion3D` bakes, the pathfinder that answers a `NavigationAgent3D`, and the
steering step that turns a path into the velocity the agent publishes.

It sits above Physics on purpose. The bake samples the world through `IRaycastSystemUVE` rather than
reading colliders itself, so what the navmesh believes is walkable is exactly what a body standing
there would collide with, and this module never grows a second collision representation that could
disagree with the first.

## Pieces

| File | What it is |
| --- | --- |
| `navmesh_uve.h` | The mesh itself: convex polygons in the XZ plane, the portals between them, and the queries a path request asks (which polygon is this point on, project it, how big is the walkable area). |
| `navmesh_bake_uve.h` | Rasterizing a region's volume into that mesh: ground rays, slope and headroom tests, clearance erosion, merging cells into polygons, and the portals across the borders between them. |
| `nav_path_uve.h` | A* across the polygons honouring each request's navigation layers, then string-pulling the polygon chain into the corners a body actually walks. |
| `navmesh_agent_uve.h` | One agent's steering state: when to re-plan, which waypoint it is walking to, and the velocity it publishes this step. |

## Conventions that hold across the module

* **Winding.** A polygon is counter-clockwise seen from above (`SignedAreaXZUVE` > 0) in the order the
  bake emits it. Every portal's left/right ends and the string-pulling step read that same convention.
* **Height.** `NavmeshPolygonUVE` corners carry the rasterized surface's own heights, so a mesh over
  stepped ground slopes across the step rather than jumping; neighbouring polygons share their corner
  heights, which is what keeps two polygons meeting exactly.
* **Layers.** A polygon carries the collision layer of the surface it came from, and a path request
  carries the layers its agent may use. Nothing walks a polygon its request's mask does not intersect.
