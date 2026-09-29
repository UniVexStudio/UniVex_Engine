// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/retarget/retarget_match_uve.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>

#include "retarget_keys_uve.h"
#include "retarget_pose_uve.h"
#include "uve/retarget/retarget_names_uve.h"

namespace UVE::Retarget {
namespace {

/// Chains whose length differs between rigs (three spine bones or five, one neck bone or two).
enum class ChainUVE : std::uint8_t { None = 0, Spine, Neck };

[[nodiscard]] ChainUVE ChainOfUVE(const BoneKeyPartsUVE& key) {
    if (key.side != 'C' || key.words.empty() || key.words.size() > 2U) {
        return ChainUVE::None;
    }
    const bool numbered = key.words.size() == 1U || key.Number() > 0 || key.words[1] == "0";
    if (!numbered) {
        return key.words == std::vector<std::string>{"upper", "chest"} ? ChainUVE::Spine : ChainUVE::None;
    }
    if (key.words[0] == "spine" || key.words[0] == "chest") {
        return ChainUVE::Spine;
    }
    if (key.words[0] == "neck") {
        return ChainUVE::Neck;
    }
    return ChainUVE::None;
}

/// Pairs rig bones with reference bones by meaning: exact keys first, then the spine and neck
/// chains spread end to end over the reference's.
[[nodiscard]] std::vector<std::int32_t> PairByNameUVE(const std::vector<std::string>& names,
                                                      const HumanoidReferenceUVE& reference) {
    std::vector<std::int32_t> referenceOf(names.size(), -1);
    std::unordered_map<std::string, std::int32_t> referenceByKey;
    std::vector<std::int32_t> referenceSpine;
    std::vector<std::int32_t> referenceNeck;
    for (std::size_t index = 0U; index < reference.info.size(); ++index) {
        const BoneKeyPartsUVE parts = SplitBoneKeyUVE(reference.info[index].key);
        switch (ChainOfUVE(parts)) {
            case ChainUVE::Spine: referenceSpine.push_back(static_cast<std::int32_t>(index)); break;
            case ChainUVE::Neck: referenceNeck.push_back(static_cast<std::int32_t>(index)); break;
            case ChainUVE::None: referenceByKey.emplace(reference.info[index].key, static_cast<std::int32_t>(index)); break;
        }
    }
    std::vector<bool> taken(reference.info.size(), false);
    std::vector<std::size_t> rigSpine;
    std::vector<std::size_t> rigNeck;
    for (std::size_t index = 0U; index < names.size(); ++index) {
        const std::string key = MakeBoneKeyUVE(names[index]);
        const BoneKeyPartsUVE parts = SplitBoneKeyUVE(key);
        const ChainUVE chain = ChainOfUVE(parts);
        if (chain == ChainUVE::Spine) {
            rigSpine.push_back(index);
            continue;
        }
        if (chain == ChainUVE::Neck) {
            rigNeck.push_back(index);
            continue;
        }
        const auto found = referenceByKey.find(key);
        if (found != referenceByKey.end() && !taken[static_cast<std::size_t>(found->second)]) {
            referenceOf[index] = found->second;
            taken[static_cast<std::size_t>(found->second)] = true;
        }
    }
    // A chain of n bones over the reference's m: the ends meet the ends, the rest spread evenly;
    // a chain longer than the reference's leaves its extra bones unmatched.
    const auto spread = [&referenceOf](const std::vector<std::size_t>& rig, const std::vector<std::int32_t>& ref) {
        if (rig.empty() || ref.empty()) {
            return;
        }
        if (rig.size() >= ref.size()) {
            for (std::size_t i = 0U; i < ref.size(); ++i) {
                referenceOf[rig[i]] = ref[i];
            }
            return;
        }
        if (rig.size() == 1U) {
            referenceOf[rig[0]] = ref.front();
            return;
        }
        for (std::size_t i = 0U; i < rig.size(); ++i) {
            const float at = static_cast<float>(i) * static_cast<float>(ref.size() - 1U) / static_cast<float>(rig.size() - 1U);
            referenceOf[rig[i]] = ref[static_cast<std::size_t>(std::lround(at))];
        }
    };
    spread(rigSpine, referenceSpine);
    spread(rigNeck, referenceNeck);
    return referenceOf;
}

void CountUVE(HumanoidMatchUVE& match) {
    match.counts = {};
    for (const JointReportUVE& joint : match.joints) {
        ++match.counts[static_cast<std::size_t>(joint.status)];
    }
}

[[nodiscard]] bool IsAncestorUVE(const RetargetSkeletonUVE& skeleton, std::int32_t bone, const std::int32_t ancestor) {
    for (bone = bone >= 0 ? skeleton.bones[static_cast<std::size_t>(bone)].parent : -1; bone >= 0;
         bone = skeleton.bones[static_cast<std::size_t>(bone)].parent) {
        if (bone == ancestor) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::string PercentUVE(const float ratio) {
    char text[64];
    std::snprintf(text, sizeof(text), "%d%% of the expected length", static_cast<int>(std::lround(ratio * 100.0F)));
    return text;
}

} // namespace

const char* JointStatusNameUVE(const JointStatusUVE status) noexcept {
    switch (status) {
        case JointStatusUVE::Good: return "good";
        case JointStatusUVE::Warning: return "check";
        case JointStatusUVE::Broken: return "broken";
        case JointStatusUVE::Missing: return "missing";
    }
    return "missing";
}

bool AreHumanoidNamesUVE(const std::vector<std::string>& names, const HumanoidReferenceUVE& reference) {
    if (reference.skeleton.bones.empty()) {
        return false;
    }
    const std::unordered_set<std::string> have(names.begin(), names.end());
    std::size_t found = 0U;
    for (const RetargetBoneUVE& bone : reference.skeleton.bones) {
        found += have.contains(bone.name) ? 1U : 0U;
    }
    return found * 5U >= reference.skeleton.bones.size() * 4U;
}

HumanoidMatchUVE MatchHumanoidNamesUVE(const std::vector<std::string>& names, const HumanoidReferenceUVE& reference) {
    HumanoidMatchUVE match;
    match.referenceOfBone = PairByNameUVE(names, reference);
    match.joints.assign(reference.info.size(), JointReportUVE{-1, JointStatusUVE::Missing, "not in this rig"});
    for (std::size_t bone = 0U; bone < names.size(); ++bone) {
        if (const std::int32_t ref = match.referenceOfBone[bone]; ref >= 0) {
            match.joints[static_cast<std::size_t>(ref)] = JointReportUVE{static_cast<std::int32_t>(bone), JointStatusUVE::Good, {}};
        }
    }
    CountUVE(match);
    return match;
}

HumanoidMatchUVE MatchHumanoidUVE(const RetargetSkeletonUVE& rig, const HumanoidReferenceUVE& reference) {
    std::vector<std::string> names;
    names.reserve(rig.bones.size());
    for (const RetargetBoneUVE& bone : rig.bones) {
        names.push_back(bone.name);
    }
    HumanoidMatchUVE match = MatchHumanoidNamesUVE(names, reference);
    if (!IsRetargetSkeletonValidUVE(rig)) {
        return match;
    }
    const RetargetSkeletonUVE folded = FoldScaleUVE(rig);
    const std::vector<WorldTransformUVE> world = ComputeWorldTransformsUVE(folded);
    const std::vector<WorldTransformUVE> referenceWorld = ComputeWorldTransformsUVE(reference.skeleton);
    // How tall it stands is measured up its own spine, whichever way the rig was authored.
    std::vector<WorldTransformUVE> upright = world;
    const Math::QuaternionUVE orientation = OrientationOfRigUVE(world, match, reference);
    for (WorldTransformUVE& transform : upright) {
        transform.position = Math::RotateVectorUVE(orientation, transform.position);
    }
    const float rigHeight = MeasureHipsHeightUVE(folded, upright);
    match.heightScale = rigHeight > 0.0F && reference.hipsHeight > 0.0F ? rigHeight / reference.hipsHeight : 1.0F;

    const auto nameOf = [&](const std::int32_t bone) { return rig.bones[static_cast<std::size_t>(bone)].name; };
    for (std::size_t ref = 0U; ref < reference.info.size(); ++ref) {
        JointReportUVE& joint = match.joints[ref];
        if (joint.bone < 0) {
            continue;
        }
        // In the hierarchy: under the bone standing for its nearest found reference ancestor.
        std::int32_t above = reference.skeleton.bones[ref].parent;
        while (above >= 0 && match.joints[static_cast<std::size_t>(above)].bone < 0) {
            above = reference.skeleton.bones[static_cast<std::size_t>(above)].parent;
        }
        if (above >= 0 && !IsAncestorUVE(folded, joint.bone, match.joints[static_cast<std::size_t>(above)].bone)) {
            joint.status = JointStatusUVE::Broken;
            joint.reason = "not under " + nameOf(match.joints[static_cast<std::size_t>(above)].bone) + " as " +
                           reference.skeleton.bones[static_cast<std::size_t>(above)].name + " is";
            continue;
        }
        // Its length: to the bone it points at, against the reference's scaled to this rig.
        const std::int32_t aim = reference.info[ref].aim;
        if (aim < 0 || match.joints[static_cast<std::size_t>(aim)].bone < 0) {
            continue;
        }
        const float length = Math::LengthUVE(world[static_cast<std::size_t>(match.joints[static_cast<std::size_t>(aim)].bone)].position -
                                             world[static_cast<std::size_t>(joint.bone)].position);
        const float expected = Math::LengthUVE(referenceWorld[static_cast<std::size_t>(aim)].position - referenceWorld[ref].position) *
                               match.heightScale;
        if (length < 1e-4F) {
            joint.status = JointStatusUVE::Broken;
            joint.reason = "has no length: it sits on " + nameOf(match.joints[static_cast<std::size_t>(aim)].bone);
            continue;
        }
        const float ratio = expected > 1e-6F ? length / expected : 1.0F;
        if (ratio < 0.6F || ratio > 1.6F) {
            joint.status = JointStatusUVE::Warning;
            joint.reason = PercentUVE(ratio);
        }
    }
    // Left and right should match: a side much longer than the other is flagged on both.
    for (std::size_t ref = 0U; ref < reference.info.size(); ++ref) {
        const std::string& key = reference.info[ref].key;
        if (key.rfind("L|", 0U) != 0U || reference.info[ref].aim < 0) {
            continue;
        }
        const auto mirror = std::ranges::find_if(reference.info, [&key](const HumanoidBoneInfoUVE& info) {
            return info.key == "R|" + key.substr(2U);
        });
        if (mirror == reference.info.end()) {
            continue;
        }
        const auto right = static_cast<std::size_t>(mirror - reference.info.begin());
        const auto lengthOf = [&](const std::size_t bone) {
            const std::int32_t aim = reference.info[bone].aim;
            if (match.joints[bone].bone < 0 || aim < 0 || match.joints[static_cast<std::size_t>(aim)].bone < 0) {
                return -1.0F;
            }
            return Math::LengthUVE(world[static_cast<std::size_t>(match.joints[static_cast<std::size_t>(aim)].bone)].position -
                                   world[static_cast<std::size_t>(match.joints[bone].bone)].position);
        };
        const float leftLength = lengthOf(ref);
        const float rightLength = lengthOf(right);
        if (leftLength > 0.0F && rightLength > 0.0F &&
            std::abs(leftLength - rightLength) > 0.15F * std::max(leftLength, rightLength)) {
            for (const std::size_t side : {ref, right}) {
                if (match.joints[side].status == JointStatusUVE::Good) {
                    match.joints[side].status = JointStatusUVE::Warning;
                    match.joints[side].reason = "left and right differ in length";
                }
            }
        }
    }
    CountUVE(match);
    return match;
}

} // namespace UVE::Retarget
