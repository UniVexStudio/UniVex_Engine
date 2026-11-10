// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string>
#include <string_view>

namespace UVE::Retarget {

/// Bone names across rigs say the same thing in different words: "upperarm_l", "LeftArm",
/// "UpperArm_L", "arm.L". A bone key is the rig-independent meaning of a name, so bones from any
/// rig can be matched to the humanoid reference by comparing keys.
///
/// A key is "<side>|<words>": the side is L, R or C (centre), and the words are the name's parts,
/// lowercased, with synonyms folded to one word ("calf", "leg" and "shin" are all "shin"; "fwd"
/// is "front"; "01" is "1") and filler dropped (a "prefix:" namespace, "def", "jnt"). Examples:
///   "upperarm_twist_01_l"  -> "L|upperarm twist 1"
///   "UpperArmTwist1_L"     -> "L|upperarm twist 1"
///   "mixamorig:LeftForeArm"-> "L|forearm"
///   "calf_kneeBack_r"      -> "R|shin knee back"
///   "pelvis"               -> "C|hips"
[[nodiscard]] std::string MakeBoneKeyUVE(std::string_view name);

/// The humanoid reference's own name for a key: PascalCase words with an _L or _R side suffix
/// ("L|upperarm twist 1" -> "UpperArmTwist1_L", "C|com" -> "CenterOfMass"). Keying the result
/// gives the same key back.
[[nodiscard]] std::string MakeHumanoidBoneNameUVE(std::string_view key);

} // namespace UVE::Retarget
