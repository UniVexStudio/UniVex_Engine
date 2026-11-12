// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "retarget_brain.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <future>
#include <cmath>
#include <cstddef>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <mutex>
#include <numeric>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "retarget_names.h"
#include "retarget_pose.h"

namespace UVE::Retarget {
namespace {

// ------------------------------------------------------------------------------------ random

/// A small, fast, fully specified generator (xorshift32): the same seed gives the same rigs and
/// weights on every machine and compiler, which std's distributions do not promise.
struct RandomUVE final {
    std::uint32_t state;
    explicit RandomUVE(const std::uint32_t seed) : state(seed == 0U ? 0x9E3779B9U : seed) {}
    std::uint32_t NextUVE() {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        return state;
    }
    /// In [0, 1).
    float UnitUVE() { return static_cast<float>(NextUVE() >> 8U) * (1.0F / 16777216.0F); }
    float RangeUVE(const float low, const float high) { return low + (high - low) * UnitUVE(); }
    std::size_t BelowUVE(const std::size_t count) { return count == 0U ? 0U : static_cast<std::size_t>(NextUVE() % count); }
    bool ChanceUVE(const float p) { return UnitUVE() < p; }
};

[[nodiscard]] std::uint32_t HashUVE(const std::string_view text) {
    std::uint32_t hash = 2166136261U;
    for (const char c : text) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 16777619U;
    }
    return hash;
}

// ---------------------------------------------------------------------------------- features

[[nodiscard]] std::string LowerAlnumUVE(const std::string_view name) {
    std::string out;
    // A "namespace:" prefix says nothing about the bone.
    const std::size_t colon = name.rfind(':');
    for (const char c : colon == std::string_view::npos ? name : name.substr(colon + 1U)) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    return out;
}

void NameFeaturesUVE(const std::string_view name, float* out) {
    std::fill(out, out + RigBrainUVE::kNameFeaturesUVE, 0.0F);
    const std::string letters = "^" + LowerAlnumUVE(name) + "$";
    for (std::size_t i = 0U; i + 3U <= letters.size(); ++i) {
        out[HashUVE(std::string_view{letters}.substr(i, 3U)) % RigBrainUVE::kNameFeaturesUVE] += 1.0F;
    }
    // The words its key folds it to (synonyms already one word): "LeftUpLeg" -> "thigh".
    const std::string key = MakeBoneKeyUVE(name);
    const std::size_t bar = key.find('|');
    std::string_view words = bar == std::string::npos ? std::string_view{key} : std::string_view{key}.substr(bar + 1U);
    while (!words.empty()) {
        const std::size_t space = words.find(' ');
        const std::string_view word = words.substr(0U, space);
        if (!word.empty()) {
            out[HashUVE(std::string{"w:"} + std::string{word}) % RigBrainUVE::kNameFeaturesUVE] += 2.0F;
        }
        words = space == std::string_view::npos ? std::string_view{} : words.substr(space + 1U);
    }
    float length = 0.0F;
    for (std::size_t i = 0U; i < RigBrainUVE::kNameFeaturesUVE; ++i) {
        length += out[i] * out[i];
    }
    if (length > 0.0F) {
        const float scale = 1.0F / std::sqrt(length);
        for (std::size_t i = 0U; i < RigBrainUVE::kNameFeaturesUVE; ++i) {
            out[i] *= scale;
        }
    }
}

} // namespace

std::vector<float> RigBoneFeaturesUVE(const RetargetSkeletonUVE& rig, const HumanoidReferenceUVE& reference) {
    const std::size_t count = rig.bones.size();
    std::vector<float> rows(count * RigBrainUVE::kFeaturesUVE, 0.0F);
    if (count == 0U || !IsRetargetSkeletonValidUVE(rig)) {
        return rows;
    }
    std::vector<WorldTransformUVE> world = ComputeWorldTransformsUVE(FoldScaleUVE(rig));
    // Stood up the way names say, when they say enough; else as authored.
    std::vector<std::string> names;
    for (const RetargetBoneUVE& bone : rig.bones) {
        names.push_back(bone.name);
    }
    const HumanoidMatchUVE named = MatchHumanoidNamesUVE(names, reference);
    const Math::QuaternionUVE orientation = OrientationOfRigUVE(world, named, reference);
    Math::Vector3UVE low{1e30F, 1e30F, 1e30F};
    Math::Vector3UVE high{-1e30F, -1e30F, -1e30F};
    Math::Vector3UVE sum{};
    for (WorldTransformUVE& transform : world) {
        transform.position = Math::RotateVectorUVE(orientation, transform.position);
        low = Math::Vector3UVE{std::min(low.x, transform.position.x), std::min(low.y, transform.position.y), std::min(low.z, transform.position.z)};
        high = Math::Vector3UVE{std::max(high.x, transform.position.x), std::max(high.y, transform.position.y),
                                std::max(high.z, transform.position.z)};
        sum += transform.position;
    }
    const float height = std::max(high.y - low.y, 1e-6F);
    const Math::Vector3UVE centre = sum * (1.0F / static_cast<float>(count));
    const Math::Vector3UVE origin{centre.x, low.y, centre.z};

    std::vector<std::vector<std::size_t>> children(count);
    std::vector<std::size_t> depth(count, 0U);
    for (std::size_t bone = 0U; bone < count; ++bone) {
        if (const std::int32_t parent = rig.bones[bone].parent; parent >= 0) {
            children[static_cast<std::size_t>(parent)].push_back(bone);
            depth[bone] = depth[static_cast<std::size_t>(parent)] + 1U;
        }
    }
    std::vector<std::size_t> below(count, 0U);
    for (std::size_t bone = count; bone-- > 0U;) {
        if (const std::int32_t parent = rig.bones[bone].parent; parent >= 0) {
            below[static_cast<std::size_t>(parent)] += below[bone] + 1U;
        }
    }
    for (std::size_t bone = 0U; bone < count; ++bone) {
        float* row = rows.data() + bone * RigBrainUVE::kFeaturesUVE;
        NameFeaturesUVE(rig.bones[bone].name, row);
        float* shape = row + RigBrainUVE::kNameFeaturesUVE;
        const std::string key = MakeBoneKeyUVE(rig.bones[bone].name);
        const char side = key.empty() ? 'C' : key.front();
        std::size_t at = 0U;
        shape[at++] = side == 'L' ? 1.0F : 0.0F;
        shape[at++] = side == 'R' ? 1.0F : 0.0F;
        shape[at++] = side == 'C' ? 1.0F : 0.0F;
        const Math::Vector3UVE p = (world[bone].position - origin) * (1.0F / height);
        shape[at++] = p.x * 2.0F;
        shape[at++] = p.y;
        shape[at++] = p.z * 2.0F;
        shape[at++] = std::abs(p.x) * 2.0F;
        shape[at++] = p.x > 0.02F ? 1.0F : (p.x < -0.02F ? -1.0F : 0.0F);
        // Along its main child (the one with most below it) and from its parent.
        std::int32_t main = -1;
        for (const std::size_t child : children[bone]) {
            if (main < 0 || below[child] > below[static_cast<std::size_t>(main)]) {
                main = static_cast<std::int32_t>(child);
            }
        }
        if (main >= 0) {
            const Math::Vector3UVE along = world[static_cast<std::size_t>(main)].position - world[bone].position;
            const float length = Math::LengthUVE(along);
            const Math::Vector3UVE dir = length > 1e-9F ? along * (1.0F / length) : Math::Vector3UVE{};
            shape[at++] = dir.x;
            shape[at++] = dir.y;
            shape[at++] = dir.z;
            shape[at++] = length / height * 4.0F;
        } else {
            at += 4U;
        }
        if (const std::int32_t parent = rig.bones[bone].parent; parent >= 0) {
            const Math::Vector3UVE from = world[bone].position - world[static_cast<std::size_t>(parent)].position;
            const float length = Math::LengthUVE(from);
            const Math::Vector3UVE dir = length > 1e-9F ? from * (1.0F / length) : Math::Vector3UVE{};
            shape[at++] = dir.x;
            shape[at++] = dir.y;
            shape[at++] = dir.z;
            shape[at++] = length / height * 4.0F;
            shape[at++] = static_cast<float>(children[static_cast<std::size_t>(parent)].size()) / 5.0F;
        } else {
            at += 5U;
        }
        shape[at++] = static_cast<float>(depth[bone]) / 12.0F;
        shape[at++] = static_cast<float>(children[bone].size()) / 5.0F;
        shape[at++] = static_cast<float>(below[bone]) / static_cast<float>(count);
        shape[at++] = children[bone].empty() ? 1.0F : 0.0F;
    }
    return rows;
}

// ---------------------------------------------------------------------------------- the rigs

namespace {

[[nodiscard]] std::vector<std::string> WordsOfKeyUVE(const std::string& key) {
    std::vector<std::string> words;
    const std::size_t bar = key.find('|');
    std::string rest = bar == std::string::npos ? key : key.substr(bar + 1U);
    std::size_t start = 0U;
    while (start <= rest.size()) {
        const std::size_t space = rest.find(' ', start);
        std::string word = rest.substr(start, space == std::string::npos ? std::string::npos : space - start);
        if (!word.empty()) {
            words.push_back(std::move(word));
        }
        if (space == std::string::npos) {
            break;
        }
        start = space + 1U;
    }
    return words;
}

[[nodiscard]] std::string PascalUVE(const std::string& word) {
    std::string out = word;
    if (!out.empty()) {
        out.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(out.front())));
    }
    return out;
}

/// Other words rigs use for the same part.
[[nodiscard]] std::string SynonymUVE(const std::string& word, RandomUVE& random) {
    static const std::vector<std::pair<std::string, std::vector<std::string>>> kSynonymsUVE{
        {"upperarm", {"upperarm", "arm", "uparm", "upper_arm"}},
        {"forearm", {"forearm", "lowerarm", "lower_arm"}},
        {"thigh", {"thigh", "upleg", "upperleg"}},
        {"shin", {"shin", "calf", "leg", "lowerleg"}},
        {"hips", {"hips", "pelvis"}},
        {"toe", {"toe", "ball", "toebase"}},
        {"clavicle", {"clavicle", "shoulder", "collar"}},
    };
    for (const auto& [base, others] : kSynonymsUVE) {
        if (word == base) {
            return others[random.BelowUVE(others.size())];
        }
    }
    return word;
}

/// A name in one of the naming styles rigs come in. Style 5 says nothing at all.
[[nodiscard]] std::string NameInStyleUVE(const std::string& engineName, const std::string& key, const std::size_t style,
                                         const bool padNumbers, const std::size_t serial, RandomUVE& random) {
    if (style == 0U) {
        return engineName;
    }
    if (style == 5U) {
        char text[32];
        std::snprintf(text, sizeof(text), random.ChanceUVE(0.5F) ? "Bone%03zu" : "joint%zu", serial);
        return text;
    }
    const char side = key.empty() ? 'C' : key.front();
    std::vector<std::string> words = WordsOfKeyUVE(key);
    for (std::string& word : words) {
        word = SynonymUVE(word, random);
        if (padNumbers && word.size() == 1U && std::isdigit(static_cast<unsigned char>(word.front())) != 0) {
            word = "0" + word;
        }
    }
    std::string joined;
    const auto join = [&](const char* separator, const bool pascal) {
        joined.clear();
        for (std::size_t i = 0U; i < words.size(); ++i) {
            joined += (i == 0U ? "" : separator) + (pascal ? PascalUVE(words[i]) : words[i]);
        }
    };
    const bool sided = side == 'L' || side == 'R';
    switch (style) {
        case 1U:   // upperarm_twist_01_l
            join("_", false);
            return sided ? joined + (side == 'L' ? "_l" : "_r") : joined;
        case 2U:   // rig:LeftUpperArm
            join("", true);
            return std::string{random.ChanceUVE(0.5F) ? "rig:" : ""} + (sided ? (side == 'L' ? "Left" : "Right") : "") + joined;
        case 3U:   // L_UpperArm
            join("_", true);
            return sided ? std::string{side} + "_" + joined : joined;
        case 4U:   // upper_arm.L
            join("_", false);
            return sided ? joined + (side == 'L' ? ".L" : ".R") : joined;
        default:   // DEF-upperarm.L
            join("", false);
            return "DEF-" + (sided ? joined + (side == 'L' ? ".L" : ".R") : joined);
    }
}

} // namespace

std::vector<RigBrainSampleUVE> MakeSyntheticRigsUVE(const HumanoidReferenceUVE& reference, const std::size_t count, const std::uint32_t seed) {
    std::vector<RigBrainSampleUVE> rigs;
    const std::size_t bones = reference.skeleton.bones.size();
    if (bones == 0U) {
        return rigs;
    }
    const std::vector<WorldTransformUVE> referenceWorld = ComputeWorldTransformsUVE(reference.skeleton);
    RandomUVE random(seed);
    static const std::array<const char*, 10> kExtrasUVE{"Weapon", "Cape1", "Hair2", "Tail1", "Prop", "Ponytail", "Skirt_F", "Bag", "Sword", "Hood"};
    for (std::size_t made = 0U; made < count; ++made) {
        // Which bones this rig has: always the body, often the fingers and twists, rarely the rest.
        std::vector<bool> keep(bones, true);
        const float twists = random.UnitUVE();
        const float fingers = random.UnitUVE();
        const bool metacarpals = random.ChanceUVE(0.5F);
        const float helpers = random.UnitUVE();
        const std::size_t spines = 2U + random.BelowUVE(4U);   // 2..5 of the five
        for (std::size_t bone = 0U; bone < bones; ++bone) {
            const HumanoidBoneInfoUVE& info = reference.info[bone];
            const std::string& key = info.key;
            switch (info.kind) {
                case HumanoidBoneKindUVE::Root: keep[bone] = random.ChanceUVE(0.6F); break;
                case HumanoidBoneKindUVE::Twist: keep[bone] = twists < 0.5F; break;
                case HumanoidBoneKindUVE::Finger:
                    keep[bone] = fingers < 0.75F && (metacarpals || key.find("metacarpal") == std::string::npos);
                    break;
                case HumanoidBoneKindUVE::Corrective: keep[bone] = helpers < 0.15F; break;
                case HumanoidBoneKindUVE::IK: keep[bone] = helpers < 0.45F; break;
                case HumanoidBoneKindUVE::Socket: keep[bone] = helpers < 0.35F; break;
                default: break;
            }
            for (std::size_t s = 1U; s <= 5U; ++s) {
                if (key == "C|spine " + std::to_string(s)) {
                    keep[bone] = s == 1U || s > 5U - (spines - 1U);
                }
            }
            if (key == "C|neck 2") {
                keep[bone] = random.ChanceUVE(0.5F);
            }
        }
        // Proportions per chain, and arms raised anywhere from the A-pose to level.
        const float armScale = random.RangeUVE(0.8F, 1.2F);
        const float legScale = random.RangeUVE(0.8F, 1.2F);
        const float spineScale = random.RangeUVE(0.85F, 1.15F);
        const float raise = random.RangeUVE(0.0F, 0.96F);   // radians up from the reference's arms
        const float units = random.ChanceUVE(0.3F) ? 100.0F : 1.0F;
        std::vector<Math::Vector3UVE> position(bones);
        for (std::size_t bone = 0U; bone < bones; ++bone) {
            const std::int32_t parent = reference.skeleton.bones[bone].parent;
            if (parent < 0) {
                position[bone] = referenceWorld[bone].position;
                continue;
            }
            const std::string& key = reference.info[bone].key;
            const bool arm = key.find("arm") != std::string::npos || key.find("hand") != std::string::npos ||
                             reference.info[bone].kind == HumanoidBoneKindUVE::Finger;
            const bool leg = key.find("thigh") != std::string::npos || key.find("shin") != std::string::npos ||
                             key.find("foot") != std::string::npos || key.find("toe") != std::string::npos;
            const float scale = arm ? armScale : (leg ? legScale : (key.find("spine") != std::string::npos ? spineScale : 1.0F));
            position[bone] = position[static_cast<std::size_t>(parent)] +
                             (referenceWorld[bone].position - referenceWorld[static_cast<std::size_t>(parent)].position) * scale;
        }
        for (const char sideChar : {'L', 'R'}) {
            const std::string upper = std::string{sideChar} + "|upperarm";
            const auto found = std::ranges::find_if(reference.info, [&upper](const HumanoidBoneInfoUVE& info) { return info.key == upper; });
            if (found == reference.info.end()) {
                continue;
            }
            const auto shoulder = static_cast<std::size_t>(found - reference.info.begin());
            Math::QuaternionUVE turn{};
            static_cast<void>(Math::TryMakeAxisAngleUVE(Math::Vector3UVE{0.0F, 0.0F, 1.0F}, sideChar == 'L' ? raise : -raise, turn));
            for (std::size_t bone = shoulder + 1U; bone < bones; ++bone) {
                if (IsBelowUVE(reference.skeleton, static_cast<std::int32_t>(bone), static_cast<std::int32_t>(shoulder))) {
                    position[bone] = position[shoulder] + Math::RotateVectorUVE(turn, position[bone] - position[shoulder]);
                }
            }
        }
        // The rig: kept bones under their nearest kept ancestor, in the reference's order.
        const std::size_t style = random.BelowUVE(7U);
        const bool pad = random.ChanceUVE(0.3F);
        RigBrainSampleUVE sample;
        std::vector<std::int32_t> indexOf(bones, -1);
        for (std::size_t bone = 0U; bone < bones; ++bone) {
            if (!keep[bone]) {
                continue;
            }
            std::int32_t parent = reference.skeleton.bones[bone].parent;
            while (parent >= 0 && indexOf[static_cast<std::size_t>(parent)] < 0) {
                parent = reference.skeleton.bones[static_cast<std::size_t>(parent)].parent;
            }
            RetargetBoneUVE out;
            out.name = NameInStyleUVE(reference.skeleton.bones[bone].name, reference.info[bone].key, style, pad, sample.rig.bones.size(), random);
            // One root: a dropped root's children hang from the first bone kept.
            out.parent = parent >= 0 ? indexOf[static_cast<std::size_t>(parent)] : (sample.rig.bones.empty() ? -1 : 0);
            out.position = position[bone] * units;   // made local below
            indexOf[bone] = static_cast<std::int32_t>(sample.rig.bones.size());
            sample.rig.bones.push_back(std::move(out));
            sample.referenceOfBone.push_back(static_cast<std::int32_t>(bone));
        }
        // The end bones exporters add (finger tips, the top of the head, eyes, toe ends): leaves that
        // are none of the humanoid's, named the rig's way.
        if (random.ChanceUVE(0.7F)) {
            const std::size_t count = sample.rig.bones.size();
            for (std::size_t bone = 0U; bone < count; ++bone) {
                const std::int32_t ref = sample.referenceOfBone[bone];
                if (ref < 0) {
                    continue;
                }
                const std::string& key = reference.info[static_cast<std::size_t>(ref)].key;
                const char sideChar = key.empty() ? 'C' : key.front();
                std::vector<std::pair<std::string, Math::Vector3UVE>> ends;
                const bool fingerTip = reference.info[static_cast<std::size_t>(ref)].kind == HumanoidBoneKindUVE::Finger && !key.empty() && key.back() == '3';
                if (fingerTip) {
                    ends.emplace_back(key.substr(0U, key.size() - 1U) + "4", Math::Vector3UVE{});
                } else if (key == "C|head") {
                    ends.emplace_back("C|head top end", Math::Vector3UVE{0.0F, 0.18F, 0.0F});
                    ends.emplace_back("L|eye", Math::Vector3UVE{0.03F, 0.07F, 0.09F});
                    ends.emplace_back("R|eye", Math::Vector3UVE{-0.03F, 0.07F, 0.09F});
                } else if (key == "L|toe" || key == "R|toe") {
                    ends.emplace_back(std::string{sideChar} + "|toe end", Math::Vector3UVE{0.0F, 0.0F, 0.08F});
                }
                for (auto& [endKey, offset] : ends) {
                    RetargetBoneUVE out;
                    std::string engineName = MakeHumanoidBoneNameUVE(endKey);
                    out.name = NameInStyleUVE(engineName, endKey, style, pad, sample.rig.bones.size(), random);
                    out.parent = static_cast<std::int32_t>(bone);
                    // A finger tip goes on along its finger; the others sit where they are said to.
                    Math::Vector3UVE along = offset * units;
                    if (fingerTip) {
                        const std::int32_t parent = sample.rig.bones[bone].parent;
                        const Math::Vector3UVE from = parent >= 0 ? sample.rig.bones[bone].position - sample.rig.bones[static_cast<std::size_t>(parent)].position
                                                                   : Math::Vector3UVE{0.0F, -0.02F, 0.0F} * units;
                        along = from * 0.8F;
                    }
                    out.position = sample.rig.bones[bone].position + along;
                    sample.rig.bones.push_back(std::move(out));
                    sample.referenceOfBone.push_back(-1);
                }
            }
        }
        // Bones of the rig's own, on random bones.
        const std::size_t extras = random.BelowUVE(5U);
        for (std::size_t e = 0U; e < extras && !sample.rig.bones.empty(); ++e) {
            RetargetBoneUVE out;
            const std::size_t on = random.BelowUVE(sample.rig.bones.size());
            out.name = style == 5U ? "Bone" + std::to_string(900U + e) : std::string{kExtrasUVE[random.BelowUVE(kExtrasUVE.size())]} + std::to_string(e);
            out.parent = static_cast<std::int32_t>(on);
            out.position = sample.rig.bones[on].position +
                           Math::Vector3UVE{random.RangeUVE(-0.1F, 0.1F), random.RangeUVE(-0.15F, 0.05F), random.RangeUVE(-0.15F, 0.1F)} * units;
            sample.rig.bones.push_back(std::move(out));
            sample.referenceOfBone.push_back(-1);
        }
        // World positions to locals (no rotations: names and shape are what the brain reads).
        std::vector<Math::Vector3UVE> worldPosition(sample.rig.bones.size());
        for (std::size_t bone = 0U; bone < sample.rig.bones.size(); ++bone) {
            worldPosition[bone] = sample.rig.bones[bone].position;
        }
        for (std::size_t bone = 0U; bone < sample.rig.bones.size(); ++bone) {
            const std::int32_t parent = sample.rig.bones[bone].parent;
            sample.rig.bones[bone].position = parent < 0 ? worldPosition[bone] : worldPosition[bone] - worldPosition[static_cast<std::size_t>(parent)];
            sample.rig.bones[bone].rotation = Math::QuaternionUVE{};
        }
        rigs.push_back(std::move(sample));
    }
    return rigs;
}

// ---------------------------------------------------------------------------------- the network

std::vector<float> RigBrainUVE::ForwardUVE(const float* features, std::vector<std::vector<float>>* activations) const {
    std::vector<float> input(features, features + kFeaturesUVE);
    if (activations != nullptr) {
        activations->clear();
        activations->push_back(input);
    }
    for (std::size_t layer = 0U; layer < m_layers.size(); ++layer) {
        const LayerUVE& l = m_layers[layer];
        std::vector<float> output(l.outputs);
        for (std::size_t o = 0U; o < l.outputs; ++o) {
            const float* w = l.weights.data() + o * l.inputs;
            float sum = l.biases[o];
            for (std::size_t i = 0U; i < l.inputs; ++i) {
                sum += w[i] * input[i];
            }
            output[o] = layer + 1U < m_layers.size() ? std::max(sum, 0.0F) : sum;
        }
        input = std::move(output);
        if (activations != nullptr) {
            activations->push_back(input);
        }
    }
    // Softmax.
    const float top = *std::ranges::max_element(input);
    float total = 0.0F;
    for (float& value : input) {
        value = std::exp(value - top);
        total += value;
    }
    for (float& value : input) {
        value /= total;
    }
    return input;
}

void RigBrainUVE::TrainUVE(const HumanoidReferenceUVE& reference, const std::vector<RigBrainSampleUVE>& extra,
                           const RigBrainConfigUVE& config) {
    m_referenceBones = reference.skeleton.bones.size();
    m_classes = m_referenceBones + 1U;
    const std::size_t none = m_referenceBones;
    // The examples: every bone of every rig, the project's own four times over.
    std::vector<float> features;
    std::vector<std::uint32_t> labels;
    const auto add = [&](const RigBrainSampleUVE& sample, const std::size_t copies) {
        const std::vector<float> rows = RigBoneFeaturesUVE(sample.rig, reference);
        for (std::size_t copy = 0U; copy < copies; ++copy) {
            features.insert(features.end(), rows.begin(), rows.end());
            for (const std::int32_t ref : sample.referenceOfBone) {
                labels.push_back(ref >= 0 && static_cast<std::size_t>(ref) < m_referenceBones ? static_cast<std::uint32_t>(ref)
                                                                                                : static_cast<std::uint32_t>(none));
            }
        }
    };
    for (const RigBrainSampleUVE& sample : MakeSyntheticRigsUVE(reference, config.rigs, config.seed)) {
        add(sample, 1U);
    }
    for (const RigBrainSampleUVE& sample : extra) {
        if (sample.referenceOfBone.size() == sample.rig.bones.size()) {
            add(sample, 4U);
        }
    }
    const std::size_t examples = labels.size();

    // Layers: features -> hidden -> hidden -> classes, He-initialised from the seed.
    RandomUVE random(config.seed ^ 0xA5A5A5A5U);
    const std::array<std::size_t, 4> sizes{kFeaturesUVE, config.hidden, config.hidden, m_classes};
    m_layers.clear();
    for (std::size_t layer = 0U; layer + 1U < sizes.size(); ++layer) {
        LayerUVE l;
        l.inputs = sizes[layer];
        l.outputs = sizes[layer + 1U];
        l.weights.resize(l.inputs * l.outputs);
        l.biases.assign(l.outputs, 0.0F);
        const float bound = std::sqrt(6.0F / static_cast<float>(l.inputs));
        for (float& w : l.weights) {
            w = random.RangeUVE(-bound, bound);
        }
        m_layers.push_back(std::move(l));
    }
    if (examples == 0U) {
        return;
    }

    // Adam, mini-batches of 32, cross entropy.
    struct MomentsUVE {
        std::vector<float> mw, vw, mb, vb, gw, gb;
    };
    std::vector<MomentsUVE> moments(m_layers.size());
    for (std::size_t layer = 0U; layer < m_layers.size(); ++layer) {
        const std::size_t w = m_layers[layer].weights.size();
        const std::size_t b = m_layers[layer].biases.size();
        moments[layer] = MomentsUVE{std::vector<float>(w), std::vector<float>(w), std::vector<float>(b), std::vector<float>(b),
                                    std::vector<float>(w), std::vector<float>(b)};
    }
    constexpr float kBeta1UVE = 0.9F;
    constexpr float kBeta2UVE = 0.999F;
    constexpr float kEpsilonUVE = 1e-8F;
    constexpr std::size_t kBatchUVE = 32U;
    std::vector<std::size_t> order(examples);
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::size_t step = 0U;
    std::vector<std::vector<float>> activations;
    std::vector<float> delta;
    for (std::size_t epoch = 0U; epoch < config.epochs; ++epoch) {
        for (std::size_t i = examples; i > 1U; --i) {
            std::swap(order[i - 1U], order[random.BelowUVE(i)]);
        }
        for (std::size_t start = 0U; start < examples; start += kBatchUVE) {
            const std::size_t end = std::min(start + kBatchUVE, examples);
            for (MomentsUVE& m : moments) {
                std::fill(m.gw.begin(), m.gw.end(), 0.0F);
                std::fill(m.gb.begin(), m.gb.end(), 0.0F);
            }
            for (std::size_t n = start; n < end; ++n) {
                const std::size_t example = order[n];
                std::vector<float> probability = ForwardUVE(features.data() + example * kFeaturesUVE, &activations);
                delta = std::move(probability);
                delta[labels[example]] -= 1.0F;
                for (std::size_t layer = m_layers.size(); layer-- > 0U;) {
                    const LayerUVE& l = m_layers[layer];
                    const std::vector<float>& input = activations[layer];
                    MomentsUVE& m = moments[layer];
                    std::vector<float> back(layer > 0U ? l.inputs : 0U, 0.0F);
                    for (std::size_t o = 0U; o < l.outputs; ++o) {
                        const float d = delta[o];
                        if (d == 0.0F) {
                            continue;
                        }
                        m.gb[o] += d;
                        float* gw = m.gw.data() + o * l.inputs;
                        const float* w = l.weights.data() + o * l.inputs;
                        for (std::size_t k = 0U; k < l.inputs; ++k) {
                            gw[k] += d * input[k];
                        }
                        if (layer > 0U) {
                            for (std::size_t k = 0U; k < l.inputs; ++k) {
                                back[k] += d * w[k];
                            }
                        }
                    }
                    if (layer > 0U) {
                        for (std::size_t k = 0U; k < l.inputs; ++k) {
                            back[k] = input[k] > 0.0F ? back[k] : 0.0F;   // through the ReLU
                        }
                        delta = std::move(back);
                    }
                }
            }
            ++step;
            const float scale = 1.0F / static_cast<float>(end - start);
            const float correction1 = 1.0F - std::pow(kBeta1UVE, static_cast<float>(step));
            const float correction2 = 1.0F - std::pow(kBeta2UVE, static_cast<float>(step));
            const float rate = config.learningRate * std::sqrt(correction2) / correction1;
            for (std::size_t layer = 0U; layer < m_layers.size(); ++layer) {
                LayerUVE& l = m_layers[layer];
                MomentsUVE& m = moments[layer];
                const auto update = [&](std::vector<float>& value, std::vector<float>& g, std::vector<float>& m1, std::vector<float>& m2) {
                    for (std::size_t k = 0U; k < value.size(); ++k) {
                        const float grad = g[k] * scale;
                        m1[k] = kBeta1UVE * m1[k] + (1.0F - kBeta1UVE) * grad;
                        m2[k] = kBeta2UVE * m2[k] + (1.0F - kBeta2UVE) * grad * grad;
                        value[k] -= rate * m1[k] / (std::sqrt(m2[k]) + kEpsilonUVE);
                    }
                };
                update(l.weights, m.gw, m.mw, m.vw);
                update(l.biases, m.gb, m.mb, m.vb);
            }
        }
    }
}

std::vector<BonePredictionUVE> RigBrainUVE::PredictUVE(const RetargetSkeletonUVE& rig, const HumanoidReferenceUVE& reference) const {
    const std::size_t count = rig.bones.size();
    std::vector<BonePredictionUVE> out(count);
    if (!IsTrainedUVE() || reference.skeleton.bones.size() != m_referenceBones || count == 0U) {
        return out;
    }
    const std::vector<float> rows = RigBoneFeaturesUVE(rig, reference);
    // Each bone's three likeliest; the surest claims first, each humanoid bone once.
    struct GuessUVE {
        float probability;
        std::size_t bone;
        std::size_t reference;
    };
    std::vector<GuessUVE> guesses;
    for (std::size_t bone = 0U; bone < count; ++bone) {
        const std::vector<float> probability = ForwardUVE(rows.data() + bone * kFeaturesUVE, nullptr);
        std::vector<std::size_t> best(probability.size());
        std::iota(best.begin(), best.end(), std::size_t{0});
        std::partial_sort(best.begin(), best.begin() + 3, best.end(),
                          [&probability](const std::size_t a, const std::size_t b) { return probability[a] > probability[b]; });
        for (std::size_t k = 0U; k < 3U; ++k) {
            guesses.push_back(GuessUVE{probability[best[k]], bone, best[k]});
        }
    }
    std::ranges::stable_sort(guesses, [](const GuessUVE& a, const GuessUVE& b) { return a.probability > b.probability; });
    std::vector<bool> boneDone(count, false);
    std::vector<bool> referenceTaken(m_referenceBones, false);
    for (const GuessUVE& guess : guesses) {
        if (boneDone[guess.bone]) {
            continue;
        }
        if (guess.reference == m_referenceBones) {
            out[guess.bone] = BonePredictionUVE{-1, guess.probability};
            boneDone[guess.bone] = true;
            continue;
        }
        if (referenceTaken[guess.reference]) {
            continue;
        }
        referenceTaken[guess.reference] = true;
        boneDone[guess.bone] = true;
        out[guess.bone] = BonePredictionUVE{static_cast<std::int32_t>(guess.reference), guess.probability};
    }
    return out;
}

std::string RigBrainUVE::WriteUVE() const {
    nlohmann::ordered_json doc;
    doc["format"] = "uve-rigbrain-v1";
    doc["referenceBones"] = m_referenceBones;
    doc["features"] = kFeaturesUVE;
    nlohmann::ordered_json layers = nlohmann::ordered_json::array();
    for (const LayerUVE& l : m_layers) {
        nlohmann::ordered_json layer;
        layer["inputs"] = l.inputs;
        layer["outputs"] = l.outputs;
        layer["weights"] = l.weights;
        layer["biases"] = l.biases;
        layers.push_back(std::move(layer));
    }
    doc["layers"] = std::move(layers);
    return doc.dump();
}

bool RigBrainUVE::ReadUVE(const std::string_view text, const HumanoidReferenceUVE& reference) {
    try {
        const nlohmann::json doc = nlohmann::json::parse(text);
        if (doc.value("format", std::string{}) != "uve-rigbrain-v1" || doc.value("features", std::size_t{0}) != kFeaturesUVE ||
            doc.value("referenceBones", std::size_t{0}) != reference.skeleton.bones.size()) {
            return false;
        }
        std::vector<LayerUVE> layers;
        std::size_t inputs = kFeaturesUVE;
        for (const nlohmann::json& entry : doc.at("layers")) {
            LayerUVE l;
            l.inputs = entry.at("inputs").get<std::size_t>();
            l.outputs = entry.at("outputs").get<std::size_t>();
            l.weights = entry.at("weights").get<std::vector<float>>();
            l.biases = entry.at("biases").get<std::vector<float>>();
            if (l.inputs != inputs || l.weights.size() != l.inputs * l.outputs || l.biases.size() != l.outputs) {
                return false;
            }
            inputs = l.outputs;
            layers.push_back(std::move(l));
        }
        if (layers.empty() || inputs != reference.skeleton.bones.size() + 1U) {
            return false;
        }
        m_layers = std::move(layers);
        m_referenceBones = reference.skeleton.bones.size();
        m_classes = m_referenceBones + 1U;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// ---------------------------------------------------------------------------------- matching

HumanoidMatchUVE MatchHumanoidWithBrainUVE(const RetargetSkeletonUVE& rig, const HumanoidReferenceUVE& reference, const RigBrainUVE& brain,
                                           const float minimumConfidence) {
    HumanoidMatchUVE match = MatchHumanoidUVE(rig, reference);
    if (!brain.IsTrainedUVE() || !IsRetargetSkeletonValidUVE(rig)) {
        return match;
    }
    const std::vector<BonePredictionUVE> predictions = brain.PredictUVE(rig, reference);
    // The surest first; a guess is taken only where the hierarchy agrees: under the rig bone of the
    // nearest humanoid bone above it that is found, and above the rig bones of those below it.
    std::vector<std::size_t> order;
    for (std::size_t bone = 0U; bone < rig.bones.size(); ++bone) {
        order.push_back(bone);
    }
    std::ranges::stable_sort(order, [&predictions](const std::size_t a, const std::size_t b) {
        return predictions[a].confidence > predictions[b].confidence;
    });
    const auto below = [&rig](std::int32_t bone, const std::int32_t ancestor) {
        for (bone = rig.bones[static_cast<std::size_t>(bone)].parent; bone >= 0; bone = rig.bones[static_cast<std::size_t>(bone)].parent) {
            if (bone == ancestor) {
                return true;
            }
        }
        return false;
    };
    const auto agrees = [&](const std::size_t ref, const std::int32_t bone) {
        std::int32_t above = reference.skeleton.bones[ref].parent;
        while (above >= 0 && match.joints[static_cast<std::size_t>(above)].bone < 0) {
            above = reference.skeleton.bones[static_cast<std::size_t>(above)].parent;
        }
        if (above >= 0 && !below(bone, match.joints[static_cast<std::size_t>(above)].bone)) {
            return false;
        }
        for (std::size_t other = 0U; other < reference.info.size(); ++other) {
            const std::int32_t at = match.joints[other].bone;
            if (at < 0 || !IsBelowUVE(reference.skeleton, static_cast<std::int32_t>(other), static_cast<std::int32_t>(ref))) {
                continue;
            }
            if (!below(at, bone)) {
                return false;
            }
        }
        return true;
    };
    std::vector<std::pair<std::size_t, float>> found;   // reference bone, confidence
    for (const std::size_t bone : order) {
        const BonePredictionUVE& guess = predictions[bone];
        if (match.referenceOfBone[bone] >= 0 || guess.reference < 0 || guess.confidence < minimumConfidence) {
            continue;
        }
        const auto ref = static_cast<std::size_t>(guess.reference);
        if (match.joints[ref].bone >= 0 || !agrees(ref, static_cast<std::int32_t>(bone))) {
            continue;   // the names already found it, or it would sit in the wrong place
        }
        match.referenceOfBone[bone] = guess.reference;
        match.joints[ref].bone = static_cast<std::int32_t>(bone);
        match.joints[ref].status = JointStatusUVE::Good;
        match.joints[ref].reason.clear();
        found.emplace_back(ref, guess.confidence);
    }
    if (found.empty()) {
        return match;
    }
    match = CheckHumanoidMatchUVE(rig, reference, std::move(match));
    for (const auto& [ref, confidence] : found) {
        JointReportUVE& joint = match.joints[ref];
        joint.byBrain = true;
        joint.confidence = confidence;
        if (joint.status == JointStatusUVE::Good && confidence < 0.85F) {
            char text[64];
            std::snprintf(text, sizeof(text), "found by the Rig Brain (%d%% sure)", static_cast<int>(std::lround(confidence * 100.0F)));
            joint.status = JointStatusUVE::Warning;
            joint.reason = text;
        }
    }
    match.counts = {};
    for (const JointReportUVE& joint : match.joints) {
        ++match.counts[static_cast<std::size_t>(joint.status)];
    }
    return match;
}

std::string WriteRigBrainSamplesUVE(const std::vector<RigBrainSampleUVE>& samples) {
    nlohmann::ordered_json doc;
    doc["format"] = "uve-rigbrain-samples-v1";
    nlohmann::ordered_json rigs = nlohmann::ordered_json::array();
    for (const RigBrainSampleUVE& sample : samples) {
        nlohmann::ordered_json bones = nlohmann::ordered_json::array();
        for (std::size_t bone = 0U; bone < sample.rig.bones.size(); ++bone) {
            const RetargetBoneUVE& b = sample.rig.bones[bone];
            bones.push_back({{"name", b.name},
                             {"parent", b.parent},
                             {"position", {b.position.x, b.position.y, b.position.z}},
                             {"rotation", {b.rotation.x, b.rotation.y, b.rotation.z, b.rotation.w}},
                             {"scale", b.scale},
                             {"reference", bone < sample.referenceOfBone.size() ? sample.referenceOfBone[bone] : -1}});
        }
        rigs.push_back({{"bones", std::move(bones)}});
    }
    doc["rigs"] = std::move(rigs);
    return doc.dump();
}

std::vector<RigBrainSampleUVE> ReadRigBrainSamplesUVE(const std::string_view text) {
    std::vector<RigBrainSampleUVE> samples;
    try {
        const nlohmann::json doc = nlohmann::json::parse(text);
        if (doc.value("format", std::string{}) != "uve-rigbrain-samples-v1") {
            return samples;
        }
        for (const nlohmann::json& rig : doc.at("rigs")) {
            RigBrainSampleUVE sample;
            for (const nlohmann::json& entry : rig.at("bones")) {
                RetargetBoneUVE bone;
                bone.name = entry.at("name").get<std::string>();
                bone.parent = entry.at("parent").get<std::int32_t>();
                const std::vector<float> p = entry.at("position").get<std::vector<float>>();
                const std::vector<float> r = entry.at("rotation").get<std::vector<float>>();
                if (p.size() != 3U || r.size() != 4U) {
                    return {};
                }
                bone.position = Math::Vector3UVE{p[0], p[1], p[2]};
                bone.rotation = Math::QuaternionUVE{r[0], r[1], r[2], r[3]};
                bone.scale = entry.value("scale", 1.0F);
                sample.referenceOfBone.push_back(entry.value("reference", -1));
                sample.rig.bones.push_back(std::move(bone));
            }
            if (IsRetargetSkeletonValidUVE(sample.rig)) {
                samples.push_back(std::move(sample));
            }
        }
    } catch (const std::exception&) {
        return {};
    }
    return samples;
}

namespace {
std::atomic<bool> gDefaultBrainReadyUVE{false};
} // namespace

const RigBrainUVE& GetDefaultRigBrainUVE() {
    static RigBrainUVE brain;
    static std::once_flag once;
    std::call_once(once, [] {
        brain.TrainUVE(GetHumanoidReferenceUVE());
        gDefaultBrainReadyUVE = true;
    });
    return brain;
}

void WarmDefaultRigBrainUVE() {
    static std::once_flag once;
    static std::future<void> training;
    std::call_once(once, [] { training = std::async(std::launch::async, [] { static_cast<void>(GetDefaultRigBrainUVE()); }); });
}

bool IsDefaultRigBrainReadyUVE() noexcept {
    return gDefaultBrainReadyUVE.load();
}

} // namespace UVE::Retarget
