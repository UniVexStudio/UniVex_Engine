# Engine/Runtime/Objects/Animation

This folder owns animation-facing scene-node definitions and implementations:

- `AnimationSequencer` and `AnimationGraph` (pure-Node authoring and playback behavior)
- `Skeleton3D` and `BoneAttachment3D` (3D skeletal authoring data)
- `BoneModifier3D` and `AnimationMixer` abstract bases

The underlying animation runtime, clip types, and state machine remain in
`Engine/Runtime/Animation`; component data remains in `Engine/Runtime/Component`.
These sources currently compile into the existing `uve_nodes_3d` target together with the other
scene-node categories, so this is an ownership/layout boundary rather than a new linked library.
The full 3D node aggregate includes `all_animation_nodes_3d_uve.h`.
