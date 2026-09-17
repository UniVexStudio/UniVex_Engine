# Engine/Runtime/Scene

The scene-hierarchy layer: scene graph, serializer, prefabs, particle runtime, and the scene
Node registry. Written down per the 2026-09-17 audit (section 6.3) because this module is the
third corner of a triangle new contributors historically confuse:

## The entity/scene/node boundary — who owns what

| Concern | Module | Lives at / include prefix | Mental model |
|---|---|---|---|
| **Entity + component storage (ECS)** | `Engine/Runtime/Entity` | `uve/scene/i_entity_manager_uve.h` etc. (yes, `uve/scene` prefix — see AUDIT §6.2) | `EntityUVE` is just an id; components are plain structs in archetype chunks; `IEntityManagerUVE` owns lifecycle + queries. Data-oriented. |
| **Component definitions** | `Engine/Runtime/Component` | `uve/scene/components/*` | Header-only struct library (Transform, Camera, Light, Mesh, ...). No behavior beyond tiny value semantics. |
| **Hierarchy + transforms parenting** | **this module** | `uve/scene/*` (graph, serializer, prefab) | `SceneGraphUVE` parents entities and propagates `TransformComponentUVE` → `WorldTransformComponentUVE`. Hierarchy is *components*, not node objects. |
| **Typed "Node" vocabulary** | this module (`uve/scene/nodes/`) + `Engine/Runtime/Nodes/3D` (`uve/nodes/3d/`) | registry + structs | `SceneNodeKindUVE` + the registry map a node kind to its real backing struct/components; `Nodes/3D` holds the game-facing structs (RayCast3D, Skeleton3D, etc.). A "Node3D" is a vocabulary over entities+components, **not** a parallel object tree. |

## The rules that keep this triangle from duplicating itself

1. **One truth per concept.** World position lives in `WorldTransformComponentUVE` only; node
   structs never shadow their own transform state.
2. **New behavior attaches to entities as components** whenever practical; add a node *kind*
   only when the editor/serializers/tools need a named, user-attachable concept.
3. **No second hierarchy.** `SceneNodeRegistryUVE` knows node kinds; `SceneGraphUVE` knows
   parenting. If a feature wants a tree, extend the graph — don't build a shadow tree of
   node pointers.
4. There *was* a compatibility-alias facade (`using XNodeUVE = XComponentUVE;` per node type);
   it was deliberately deleted when nothing used it (see `scene_node_uve.h`'s own header
   comment). Do not reintroduce alias shims.

Cross-link: `SCENE_NODES_ROADMAP.md` at the repo root tracks the node-registry maturity plan.
