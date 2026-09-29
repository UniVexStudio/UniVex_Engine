# Retarget

Makes characters and animations from different rigs work together. The engine has one humanoid
skeleton of its own, the **UniVex Humanoid**. A character (`.uvmodel`) or an animation (`.uvanim`)
is *conformed* to it: its bones are renamed to the humanoid's names, the character stands in the
humanoid's A-pose, and any bones it lacks are added. After that, every conformed animation plays
on every conformed character.

```
Retarget/
  Runtime/   uve_retarget: the humanoid, bone-name matching, joint checks, conforming
    Expose/    public headers (uve/retarget/...)
    Internal/  implementation
    Data/      humanoid_reference.json, embedded into the library at build time
    Tools/     uve_retarget_make_reference, which rebuilds the JSON from a source rig
  Editor/    the Retarget window (planned: R3)
```

**Why a module of its own.** Retargeting sits between assets and animation, and the editor needs
it but a shipped game only needs its results. Keeping it out of `Animation/` keeps that module
free of rig knowledge. The editor part will be its own target, so `uve_retarget` never depends on
the editor.

**Depends on:** `uve_math`, `uve_asset` (public), and `nlohmann_json` (private: only
`retarget_humanoid_uve.cpp` sees JSON).

## The humanoid

162 bones, in metres, +Y up, facing +Z with its left side toward +X. The root is on the ground at
the origin. The structure covers:

- the body: Hips, Spine1-5, Neck1-2, Head, clavicles, arms, legs, feet, toes;
- fingers with metacarpals;
- twist bones;
- corrective and muscle helpers;
- IK targets (IKFootRoot, IKFoot_L/R, IKHandRoot, IKHandProp, IKHand_L/R);
- sockets (Prop_L/R, Mount, CenterOfMass, Interaction).

Names are PascalCase with `_L`/`_R`, for example `UpperArmTwist1_L`.

The rest pose is an A-pose, defined exactly, so every conformed rig means the same thing by "rest":

| Part | Rest |
|---|---|
| Hips, spine, neck | Straight up. |
| Head | Level, looking along +Z. |
| Arms | 45 degrees below level and straight to the fingertips. Each elbow's bend axis lies across the arm so that it bends toward +Z. |
| Palms | Toward the body, thumb side forward: the knuckle line runs along +Z. |
| Legs | Straight down. |
| Feet | As they stood on the ground: flat, toes ahead and turned out a little. |
| IK bones | Exactly on the bones they follow. |

`Test/Retarget/retarget_humanoid_uve_tests.cpp` checks all of this against the shipped JSON:

- directions to 1e-4;
- palms;
- a level head;
- toes ahead of the heels;
- IK placement;
- left/right mirror to 1 cm.

The largest left/right difference is 9 mm, on the ankle helpers. It comes from the source rig's
own helper placement, not from the pose.

### Rebuilding the reference

The JSON is generated, not hand-written. The source rig (an FBX) is kept outside the repository:

```
build/Engine/Runtime/Retarget/Runtime/uve_retarget_make_reference <source.fbx> \
    Engine/Runtime/Retarget/Runtime/Data/humanoid_reference.json --report > report.tsv
```

- `BuildHumanoidReferenceUVE` renames every bone through its key, poses it as described above, and
  refuses a rig that does not face +Z or has no hips.
- `--report` prints every bone before and after: world position, rotation and length to its
  parent. Check it before committing a new reference. Only the hips (set on the ground) and the
  IK bones (moved onto what they follow) may change length.

## Matching any rig

`MakeBoneKeyUVE` reduces a bone name to what it means: `"<side>|<words>"`.

- `upperarm_twist_01_l`, `UpperArmTwist1_L` and `DEF-upper_arm.L`-style names all read as
  `L|upperarm twist 1`.
- Namespaces (`rig:` and `Armature|`) and filler (`def`, `jnt`, auto-rigger prefixes) are dropped.
- Synonyms are folded to one word: calf, leg and lower leg all read as shin; ball and toe base
  read as toe; weapon reads as prop.

`MatchHumanoidUVE(rig, reference)` pairs a rig's bones with the humanoid's by key. Spine and neck
chains of any length are spread end to end over the humanoid's, so three spine bones land on
Spine1, Spine3 and Spine5.

Each humanoid bone then gets a status:

| Status | Colour | Meaning |
|---|---|---|
| Good | green | Found, under the right parent, a sensible length. |
| Warning | yellow | Found but suspect. Its length is outside 60-160% of the humanoid's (scaled to the rig's hips height), or left and right differ by more than 15%. |
| Broken | red | Found but unusable: under the wrong parent, or no length. |
| Missing | grey | Not in the rig. Conforming adds it. |

Rig bones the humanoid has no place for (such as a head-top end bone) are kept as they are.

## Conforming

`ConformSkeletonUVE(rig, match, reference)` makes a rig the humanoid:

- The humanoid's bones come first, with its names, parents and order. Bones the rig had keep its
  proportions; bones it lacked are added. The rig's own extra bones are kept after them, under
  their parents.
- Arms, hands, fingers and legs turn to the A-pose. Hips, spine, neck, head, clavicles and feet
  keep the rig's own shape, and the body is lifted back onto the ground.
- Every humanoid bone's rest rotation becomes the reference's frame, so a humanoid clip's
  rotations mean the same on every conformed rig.
- Added bones are fitted to the rig's own limbs: a spine bone between its neighbours at the
  humanoid's fraction, a twist along its forearm, IK targets on what they follow, the root on the
  ground under the hips.
- `moveOfRigBone` records how each original bone moved, which is what carries a skin along.

`RigFromMeshUVE` + `ConformMeshUVE` re-skin a `.uvmodel`: vertices, normals and tangents move with
their joints into the A-pose, and the joints become the conformed skeleton's, with new inverse
binds and remeasured bounds. At the new rest every skinning matrix is the identity.

`ConformClipUVE` re-expresses a clip for the conformed rig: at every frame each original bone sits
where the clip put it, now in the humanoid's names and frames. Added bones ride their parents and
IK targets follow what they follow.

## Status

| Phase | State |
|---|---|
| R1a: humanoid reference, names, JSON, builder tool | Done |
| R1b: matcher and joint statuses | Done |
| R1c: conform skeleton, mesh and clip | Done |
| R2: `.uvanim` carries its skeleton; `.uvmodel` written back with a backup | Planned |
| R3: Retarget window (multi-select in Content, own viewport, joint colours, Generate) | Planned |
