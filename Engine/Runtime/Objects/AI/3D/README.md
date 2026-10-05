# AI / 3D nodes

This is the home for 3D-specific AI and navigation scene-node definitions. The first two types
are `NavSeeker3D` and `NavMeshVolume3D`; the component structs and their validators live here, and
what consumes them lives in the Navigation module (`Engine/Runtime/Navigation`): the volume bakes
into a navmesh, the seeker paths across it and steers, and `EngineCoreUVE::SyncNavigationUVE()`
drives both on the fixed step. This file pair is still the object's own definition - what an author
authors, what the serializer writes - not a second home for the rules.

The public headers keep their established `uve/objects/3d/...` include paths. The existing
`uve_objects_3d` target compiles these sources along with the other 3D node definitions, avoiding a
new target dependency cycle while the 3D node API is still a single library. Use
`all_ai_objects_3d_uve.h` for the AI/navigation-specific aggregate; `all_objects_3d_uve.h` includes it
for callers that need the full 3D node set.
