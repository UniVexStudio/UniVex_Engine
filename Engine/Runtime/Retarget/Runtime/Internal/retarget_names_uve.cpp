// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/retarget/retarget_names_uve.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace UVE::Retarget {
namespace {

[[nodiscard]] bool IsUpperUVE(const char c) noexcept { return std::isupper(static_cast<unsigned char>(c)) != 0; }
[[nodiscard]] bool IsLowerUVE(const char c) noexcept { return std::islower(static_cast<unsigned char>(c)) != 0; }
[[nodiscard]] bool IsDigitUVE(const char c) noexcept { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
[[nodiscard]] bool IsAlnumUVE(const char c) noexcept { return std::isalnum(static_cast<unsigned char>(c)) != 0; }

/// Splits on punctuation, lower-to-upper case changes ("upperArm"), the end of a capital run
/// before a lowercase letter ("IKFoot" -> "IK", "Foot") and letter/digit changes ("Spine01").
[[nodiscard]] std::vector<std::string> SplitWordsUVE(const std::string_view name) {
    std::vector<std::string> words;
    std::string current;
    const auto flush = [&]() {
        if (!current.empty()) {
            words.push_back(current);
            current.clear();
        }
    };
    for (std::size_t i = 0U; i < name.size(); ++i) {
        const char c = name[i];
        if (!IsAlnumUVE(c)) {
            flush();
            continue;
        }
        if (!current.empty()) {
            const char previous = current.back();
            const bool caseStep = IsLowerUVE(previous) && IsUpperUVE(c);
            const bool capitalRunEnds =
                IsUpperUVE(previous) && IsUpperUVE(c) && i + 1U < name.size() && IsLowerUVE(name[i + 1U]);
            const bool digitStep = IsDigitUVE(previous) != IsDigitUVE(c);
            if (caseStep || capitalRunEnds || digitStep) {
                flush();
            }
        }
        current.push_back(c);
    }
    flush();
    for (std::string& word : words) {
        std::ranges::transform(word, word.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (IsDigitUVE(word.front())) {
            // "01" and "1" are the same bone number.
            const auto first = word.find_first_not_of('0');
            word = first == std::string::npos ? std::string{"0"} : word.substr(first);
        }
    }
    return words;
}

/// Words that name one thing several ways, folded to one.
[[nodiscard]] std::string CanonicalWordUVE(const std::string& word) {
    static constexpr std::pair<const char*, const char*> kSynonyms[]{
        {"pelvis", "hips"},     {"hip", "hips"},          {"lowerarm", "forearm"}, {"arm", "upperarm"},
        {"calf", "shin"},       {"leg", "shin"},          {"lowerleg", "shin"},    {"upleg", "thigh"},
        {"upperleg", "thigh"},  {"ball", "toe"},          {"toebase", "toe"},      {"toes", "toe"},
        {"collar", "clavicle"}, {"shoulder", "clavicle"}, {"little", "pinky"},     {"weapon", "prop"},
        {"gun", "prop"},        {"attach", "mount"},      {"cor", "corrective"},   {"fwd", "front"},
        {"forward", "front"},   {"bck", "back"},          {"lwr", "lower"},        {"inner", "in"},
        {"outer", "out"},       {"scapula", "scap"},      {"centerofmass", "com"}, {"left", "l"},
        {"right", "r"},         {"centre", "center"},     {"thumbs", "thumb"},
        // Unity and Rokoko name a finger's bones by their anatomy.
        {"proximal", "1"},      {"intermediate", "2"},    {"distal", "3"},
        // Xsens, Perception Neuron, DAZ and other mocap and character rigs.
        {"ab", "spine"},        {"shldr", "upperarm"},
        {"uarm", "upperarm"},   {"farm", "forearm"},
    };
    for (const auto& [from, to] : kSynonyms) {
        if (word == from) {
            return to;
        }
    }
    return word;
}

/// Names a rig adds around the bone's own name: namespaces, "def_" and "jnt_" prefixes.
[[nodiscard]] bool IsFillerUVE(const std::string& word) {
    static const std::array<const char*, 7> kFiller{"mixamorig", "def", "jnt", "joint", "bip", "bone", "bend"};
    return std::ranges::find(kFiller, word) != kFiller.end();
}

[[nodiscard]] bool IsFingerUVE(const std::string& word) {
    return word == "thumb" || word == "index" || word == "middle" || word == "ring" || word == "pinky";
}

[[nodiscard]] bool IsAllDigitsUVE(const std::string& word) {
    return !word.empty() && std::ranges::all_of(word, [](const char c) { return IsDigitUVE(c); });
}

[[nodiscard]] std::string LowerUVE(const std::string_view text) {
    std::string out(text);
    std::ranges::transform(out, out.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

/// Blender's Rigify names its deform bones "DEF-..." and numbers them ".001": the numbers mean
/// different things than in other rigs, so they are read by their own rules.
void ApplyRigifyRulesUVE(std::vector<std::string>& parts) {
    if (parts.empty()) {
        return;
    }
    if (parts.size() == 1U && parts[0] == "spine") {
        parts = {"hips"}; // DEF-spine sits at the hips; the numbered ones climb from there
        return;
    }
    if (parts.size() == 2U && parts[0] == "spine" && IsAllDigitsUVE(parts[1])) {
        const int number = std::stoi(parts[1]);
        if (number >= 4 && number <= 5) {
            parts = {"neck", std::to_string(number - 3)};
        } else if (number == 6) {
            parts = {"head"};
        }
        return;
    }
    if (parts.size() == 2U && IsAllDigitsUVE(parts[1]) &&
        (parts[0] == "upperarm" || parts[0] == "forearm" || parts[0] == "thigh" || parts[0] == "shin")) {
        parts.insert(parts.begin() + 1, "twist"); // DEF-upper_arm.L.001 is the first twist bone
        return;
    }
    if (parts.size() == 2U && parts[0] == "palm" && IsAllDigitsUVE(parts[1])) {
        static const std::array<const char*, 4> kFingers{"index", "middle", "ring", "pinky"};
        if (const int number = std::stoi(parts[1]); number >= 1 && number <= 4) {
            parts = {kFingers[static_cast<std::size_t>(number - 1)], "metacarpal"};
        }
    }
}

} // namespace

std::string MakeBoneKeyUVE(const std::string_view rawName) {
    // A namespace ("rig:Hand", "Armature|Hand") belongs to the file, not the bone.
    std::string_view name = rawName;
    if (const auto cut = name.find_last_of(":|"); cut != std::string_view::npos) {
        name = name.substr(cut + 1U);
    }
    const std::string lowered = LowerUVE(name);
    // Xsens names its spine after the vertebra it sits at; "L5" is not a left "5".
    if (lowered == "l5" || lowered == "l3" || lowered == "t12" || lowered == "t8") {
        return "C|spine";
    }
    const bool rigify = lowered.starts_with("def-") || lowered.starts_with("def_");
    std::vector<std::string> words;
    const std::vector<std::string> split = SplitWordsUVE(name);
    for (std::size_t i = 0U; i < split.size(); ++i) {
        const std::string& word = split[i];
        // "Bip01" and "Character1_" are a rig's prefix, number included.
        if ((word == "bip" || word == "character") && i + 1U < split.size() && IsAllDigitsUVE(split[i + 1U])) {
            ++i;
            continue;
        }
        // A toe's "0" ("Toe0"), and Rigify's "f_" in front of a finger, say nothing.
        if (word == "0" && i > 0U && split[i - 1U] == "toe") {
            continue;
        }
        if (word == "f" && i + 1U < split.size() && IsFingerUVE(CanonicalWordUVE(split[i + 1U]))) {
            continue;
        }
        if (!IsFillerUVE(word)) {
            words.push_back(word);
        }
    }
    // Two-word names for one part, merged before synonyms run ("up leg" is the thigh, "leg" alone
    // is the shin; "fore arm" is the forearm, "arm" alone the upper arm).
    std::vector<std::string> merged;
    for (std::size_t i = 0U; i < words.size(); ++i) {
        const std::string& word = words[i];
        const std::string next = i + 1U < words.size() ? words[i + 1U] : std::string{};
        const std::string after = i + 2U < words.size() ? words[i + 2U] : std::string{};
        if ((word == "upper" || word == "up") && next == "arm") {
            merged.emplace_back("upperarm");
            ++i;
        } else if ((word == "fore" || word == "lower") && next == "arm") {
            merged.emplace_back("forearm");
            ++i;
        } else if ((word == "upper" || word == "up") && next == "leg") {
            merged.emplace_back("thigh");
            ++i;
        } else if (word == "lower" && next == "leg") {
            merged.emplace_back("shin");
            ++i;
        } else if (word == "toe" && next == "base") {
            merged.emplace_back("toe");
            ++i;
        } else if (word == "center" && next == "of" && after == "mass") {
            merged.emplace_back("com");
            i += 2U;
        } else if (word == "in" && next == "hand" && IsFingerUVE(CanonicalWordUVE(after))) {
            // Rokoko's "LeftInHandIndex" is the bone inside the hand: the metacarpal.
            merged.push_back(after);
            merged.emplace_back("metacarpal");
            i += 2U;
        } else if (word == "hand" && IsFingerUVE(CanonicalWordUVE(next))) {
            // "HandIndex1": the finger, not the hand.
        } else {
            merged.push_back(word);
        }
    }
    char side = 'C';
    std::vector<std::string> parts;
    for (const std::string& word : merged) {
        const std::string canonical = CanonicalWordUVE(word);
        if (canonical == "l" || canonical == "r") {
            side = canonical == "l" ? 'L' : 'R';
            continue;
        }
        parts.push_back(canonical);
    }
    if (rigify) {
        ApplyRigifyRulesUVE(parts);
    }
    std::string key;
    for (const std::string& part : parts) {
        key += key.empty() ? part : " " + part;
    }
    return std::string{side} + "|" + key;
}

std::string MakeHumanoidBoneNameUVE(const std::string_view key) {
    static const std::array<std::pair<const char*, const char*>, 13> kWords{{
        {"upperarm", "UpperArm"},
        {"forearm", "ForeArm"},
        {"ik", "IK"},
        {"com", "CenterOfMass"},
        {"in", "Inner"},
        {"out", "Outer"},
        {"scap", "Scapula"},
        {"pec", "Pec"},
        {"tricep", "Tricep"},
        {"bicep", "Bicep"},
        {"prop", "Prop"},
        {"mount", "Mount"},
        {"hips", "Hips"},
    }};
    const auto bar = key.find('|');
    const char side = bar == std::string_view::npos || bar == 0U ? 'C' : key.front();
    const std::string_view words = bar == std::string_view::npos ? key : key.substr(bar + 1U);
    std::string name;
    std::size_t start = 0U;
    while (start <= words.size()) {
        const std::size_t end = std::min(words.find(' ', start), words.size());
        const std::string word{words.substr(start, end - start)};
        if (!word.empty()) {
            const auto special = std::ranges::find_if(kWords, [&word](const auto& entry) { return word == entry.first; });
            if (special != kWords.end()) {
                name += special->second;
            } else {
                std::string pascal = word;
                pascal.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(pascal.front())));
                name += pascal;
            }
        }
        start = end + 1U;
    }
    if (side == 'L' || side == 'R') {
        name += side == 'L' ? "_L" : "_R";
    }
    return name;
}

} // namespace UVE::Retarget
