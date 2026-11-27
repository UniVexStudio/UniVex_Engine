// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "retarget_match.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>

#include "retarget_keys.h"
#include "retarget_pose.h"
#include "retarget_names.h"

namespace UVE::Retarget {
namespace {

/// Chains whose length differs between rigs (three spine bones or five, one neck bone or two).
enum class Chain : std::uint8_t { None = 0, Spine, Neck };

[[nodiscard]] Chain ChainOf(const BoneKeyParts& key) {
    if (key.side != 'C' || key.words.empty() || key.words.size() > 2U) {
        return Chain::None;
    }
    const bool numbered = key.words.size() == 1U || key.Number() > 0 || key.words[1] == "0";
    if (!numbered) {
        return key.words == std::vector<std::string>{"upper", "chest"} ? Chain::Spine : Chain::None;
    }
    if (key.words[0] == "spine" || key.words[0] == "chest") {
        return Chain::Spine;
    }
    if (key.words[0] == "neck") {
        return Chain::Neck;
    }
    return Chain::None;
}

/// Pairs rig bones with reference bones by meaning: exact keys first, then the spine and neck
/// chains spread end to end over the reference's.
[[nodiscard]] std::vector<std::int32_t> PairByName(const std::vector<std::string>& names,
                                                      const HumanoidReference& reference) {
    std::vector<std::int32_t> referenceOf(names.size(), -1);
    std::unordered_map<std::string, std::int32_t> referenceByKey;
    std::vector<std::int32_t> referenceSpine;
    std::vector<std::int32_t> referenceNeck;
    for (std::size_t index = 0U; index < reference.info.size(); ++index) {
        const BoneKeyParts parts = SplitBoneKey(reference.info[index].key);
        switch (ChainOf(parts)) {
            case Chain::Spine: referenceSpine.push_back(static_cast<std::int32_t>(index)); break;
            case Chain::Neck: referenceNeck.push_back(static_cast<std::int32_t>(index)); break;
            case Chain::None: referenceByKey.emplace(reference.info[index].key, static_cast<std::int32_t>(index)); break;
        }
    }
    std::vector<bool> taken(reference.info.size(), false);
    std::vector<std::size_t> rigSpine;
    std::vector<std::size_t> rigNeck;
    for (std::size_t index = 0U; index < names.size(); ++index) {
        const std::string key = MakeBoneKey(names[index]);
        const BoneKeyParts parts = SplitBoneKey(key);
        const Chain chain = ChainOf(parts);
        if (chain == Chain::Spine) {
            rigSpine.push_back(index);
            continue;
        }
        if (chain == Chain::Neck) {
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

void Count(HumanoidMatch& match) {
    match.counts = {};
    for (const JointReport& joint : match.joints) {
        ++match.counts[static_cast<std::size_t>(joint.status)];
    }
}

[[nodiscard]] bool IsAncestor(const RetargetSkeleton& skeleton, std::int32_t bone, const std::int32_t ancestor) {
    const std::size_t count = skeleton.bones.size();
    if (bone < 0 || static_cast<std::size_t>(bone) >= count) {
        return false;
    }
    std::size_t hops = 0U;
    for (bone = skeleton.bones[static_cast<std::size_t>(bone)].parent;
         bone >= 0 && static_cast<std::size_t>(bone) < count && hops < count;
         bone = skeleton.bones[static_cast<std::size_t>(bone)].parent, ++hops) {
        if (bone == ancestor) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::string Percent(const float ratio) {
    char text[64];
    std::snprintf(text, sizeof(text), "%d%% of the expected length", static_cast<int>(std::lround(ratio * 100.0F)));
    return text;
}

} // namespace

const char* JointStatusName(const JointStatus status) noexcept {
    switch (status) {
        case JointStatus::Good: return "good";
        case JointStatus::Warning: return "check";
        case JointStatus::Broken: return "broken";
        case JointStatus::Missing: return "missing";
    }
    return "missing";
}

bool AreHumanoidNames(const std::vector<std::string>& names, const HumanoidReference& reference) {
    if (reference.skeleton.bones.empty()) {
        return false;
    }
    const std::unordered_set<std::string> have(names.begin(), names.end());
    std::size_t found = 0U;
    for (const RetargetBone& bone : reference.skeleton.bones) {
        found += have.contains(bone.name) ? 1U : 0U;
    }
    return found * 5U >= reference.skeleton.bones.size() * 4U;
}

HumanoidMatch MatchHumanoidNames(const std::vector<std::string>& names, const HumanoidReference& reference) {
    HumanoidMatch match;
    match.referenceOfBone = PairByName(names, reference);
    match.joints.assign(reference.info.size(), JointReport{-1, JointStatus::Missing, "not in this rig"});
    for (std::size_t bone = 0U; bone < names.size(); ++bone) {
        if (const std::int32_t ref = match.referenceOfBone[bone];
            ref >= 0 && static_cast<std::size_t>(ref) < match.joints.size()) {
            match.joints[static_cast<std::size_t>(ref)] = JointReport{static_cast<std::int32_t>(bone), JointStatus::Good, {}};
        }
    }
    Count(match);
    return match;
}

HumanoidMatch MatchHumanoid(const RetargetSkeleton& rig, const HumanoidReference& reference) {
    std::vector<std::string> names;
    names.reserve(rig.bones.size());
    for (const RetargetBone& bone : rig.bones) {
        names.push_back(bone.name);
    }
    return CheckHumanoidMatch(rig, reference, MatchHumanoidNames(names, reference));
}

HumanoidMatch CheckHumanoidMatch(const RetargetSkeleton& rig, const HumanoidReference& reference, HumanoidMatch match) {
    const std::size_t refCount = std::min(reference.info.size(), reference.skeleton.bones.size());
    if (match.joints.size() < refCount) {
        match.joints.resize(refCount, JointReport{-1, JointStatus::Missing, "not in this rig"});
    }
    // Pairs made elsewhere (the Rig Brain) start Good; the checks below judge them like any other.
    for (std::size_t bone = 0U; bone < match.referenceOfBone.size() && bone < rig.bones.size(); ++bone) {
        if (const std::int32_t ref = match.referenceOfBone[bone]; ref >= 0 && static_cast<std::size_t>(ref) < match.joints.size()) {
            JointReport& joint = match.joints[static_cast<std::size_t>(ref)];
            if (joint.bone != static_cast<std::int32_t>(bone)) {
                joint.bone = static_cast<std::int32_t>(bone);
                joint.status = JointStatus::Good;
                joint.reason.clear();
            }
        }
    }
    if (!IsRetargetSkeletonValid(rig)) {
        return match;
    }
    const RetargetSkeleton folded = FoldScale(rig);
    const std::vector<WorldTransform> world = ComputeWorldTransforms(folded);
    const std::vector<WorldTransform> referenceWorld = ComputeWorldTransforms(reference.skeleton);
    // How tall it stands is measured up its own spine, whichever way the rig was authored.
    std::vector<WorldTransform> upright = world;
    const Math::Quaternion orientation = OrientationOfRig(world, match, reference);
    for (WorldTransform& transform : upright) {
        transform.position = Math::RotateVector(orientation, transform.position);
    }
    const float rigHeight = MeasureHipsHeight(folded, upright);
    match.heightScale = rigHeight > 0.0F && reference.hipsHeight > 0.0F ? rigHeight / reference.hipsHeight : 1.0F;

    const auto validRigBone = [&](const std::int32_t bone) noexcept {
        return bone >= 0 && static_cast<std::size_t>(bone) < rig.bones.size() &&
               static_cast<std::size_t>(bone) < world.size();
    };
    const auto nameOf = [&](const std::int32_t bone) -> const std::string& {
        static const std::string kEmpty;
        return validRigBone(bone) ? rig.bones[static_cast<std::size_t>(bone)].name : kEmpty;
    };
    for (std::size_t ref = 0U; ref < refCount; ++ref) {
        JointReport& joint = match.joints[ref];
        if (!validRigBone(joint.bone)) {
            continue;
        }
        // In the hierarchy: under the bone standing for its nearest found reference ancestor.
        std::int32_t above = reference.skeleton.bones[ref].parent;
        std::size_t hops = 0U;
        while (above >= 0 && static_cast<std::size_t>(above) < refCount && hops < refCount &&
               !validRigBone(match.joints[static_cast<std::size_t>(above)].bone)) {
            above = reference.skeleton.bones[static_cast<std::size_t>(above)].parent;
            ++hops;
        }
        if (above >= 0 && static_cast<std::size_t>(above) < refCount &&
            validRigBone(match.joints[static_cast<std::size_t>(above)].bone) &&
            !IsAncestor(folded, joint.bone, match.joints[static_cast<std::size_t>(above)].bone)) {
            joint.status = JointStatus::Broken;
            joint.reason = "not under " + nameOf(match.joints[static_cast<std::size_t>(above)].bone) + " as " +
                           reference.skeleton.bones[static_cast<std::size_t>(above)].name + " is";
            continue;
        }
        // Its length: to the bone it points at, against the reference's scaled to this rig.
        const std::int32_t aim = reference.info[ref].aim;
        if (aim < 0 || static_cast<std::size_t>(aim) >= refCount ||
            static_cast<std::size_t>(aim) >= referenceWorld.size() ||
            ref >= referenceWorld.size() ||
            !validRigBone(match.joints[static_cast<std::size_t>(aim)].bone)) {
            continue;
        }
        const float length = Math::Length(world[static_cast<std::size_t>(match.joints[static_cast<std::size_t>(aim)].bone)].position -
                                             world[static_cast<std::size_t>(joint.bone)].position);
        const float expected = Math::Length(referenceWorld[static_cast<std::size_t>(aim)].position - referenceWorld[ref].position) *
                               match.heightScale;
        if (length < 1e-4F) {
            joint.status = JointStatus::Broken;
            joint.reason = "has no length: it sits on " + nameOf(match.joints[static_cast<std::size_t>(aim)].bone);
            continue;
        }
        const float ratio = expected > 1e-6F ? length / expected : 1.0F;
        if (ratio < 0.6F || ratio > 1.6F) {
            joint.status = JointStatus::Warning;
            joint.reason = Percent(ratio);
        }
    }
    // Left and right should match: a side much longer than the other is flagged on both.
    for (std::size_t ref = 0U; ref < refCount; ++ref) {
        const std::string& key = reference.info[ref].key;
        if (key.rfind("L|", 0U) != 0U || reference.info[ref].aim < 0) {
            continue;
        }
        const auto mirror = std::ranges::find_if(reference.info, [&key](const HumanoidBoneInfo& info) {
            return info.key == "R|" + key.substr(2U);
        });
        if (mirror == reference.info.end()) {
            continue;
        }
        const auto right = static_cast<std::size_t>(mirror - reference.info.begin());
        if (right >= refCount) {
            continue;
        }
        const auto lengthOf = [&](const std::size_t bone) {
            const std::int32_t aim = reference.info[bone].aim;
            if (!validRigBone(match.joints[bone].bone) || aim < 0 ||
                static_cast<std::size_t>(aim) >= refCount ||
                !validRigBone(match.joints[static_cast<std::size_t>(aim)].bone)) {
                return -1.0F;
            }
            return Math::Length(world[static_cast<std::size_t>(match.joints[static_cast<std::size_t>(aim)].bone)].position -
                                   world[static_cast<std::size_t>(match.joints[bone].bone)].position);
        };
        const float leftLength = lengthOf(ref);
        const float rightLength = lengthOf(right);
        if (leftLength > 0.0F && rightLength > 0.0F &&
            std::abs(leftLength - rightLength) > 0.15F * std::max(leftLength, rightLength)) {
            for (const std::size_t side : {ref, right}) {
                if (match.joints[side].status == JointStatus::Good) {
                    match.joints[side].status = JointStatus::Warning;
                    match.joints[side].reason = "left and right differ in length";
                }
            }
        }
    }
    Count(match);
    return match;
}

} // namespace UVE::Retarget
