// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/gltf_skeleton_uve.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <functional>
#include <iterator>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace UVE::Asset {
namespace {

constexpr std::uint32_t kGlbMagicUVE = 0x46546C67U;
constexpr std::uint32_t kGlbJsonChunkUVE = 0x4E4F534AU;
constexpr std::uint32_t kMaximumJsonBytesUVE = 64U * 1024U * 1024U;

[[nodiscard]] bool ReadFloatsUVE(const nlohmann::json& node, const char* const key, float* const out,
                                 const std::size_t count) {
    if (!node.contains(key)) {
        return true;
    }
    const nlohmann::json& value = node.at(key);
    if (!value.is_array() || value.size() != count) {
        return false;
    }
    for (std::size_t index = 0U; index < count; ++index) {
        if (!value[index].is_number()) {
            return false;
        }
        out[index] = value[index].get<float>();
        if (!std::isfinite(out[index])) {
            return false;
        }
    }
    return true;
}

/// Splits a column-major affine matrix into translation, rotation and scale. Shear is not
/// representable in TRS and is dropped, as every engine that stores bones as TRS does.
void DecomposeMatrixUVE(const std::array<float, 16>& m, GltfJointUVE& joint) {
    joint.translation = {m[12], m[13], m[14]};
    const float sx = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
    const float sy = std::sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
    const float sz = std::sqrt(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]);
    joint.scale = {sx, sy, sz};
    if (sx <= 0.0F || sy <= 0.0F || sz <= 0.0F) {
        return;
    }
    const float r00 = m[0] / sx, r10 = m[1] / sx, r20 = m[2] / sx;
    const float r01 = m[4] / sy, r11 = m[5] / sy, r21 = m[6] / sy;
    const float r02 = m[8] / sz, r12 = m[9] / sz, r22 = m[10] / sz;
    const float trace = r00 + r11 + r22;
    Math::QuaternionUVE q;
    if (trace > 0.0F) {
        const float s = std::sqrt(trace + 1.0F) * 2.0F;
        q = {(r21 - r12) / s, (r02 - r20) / s, (r10 - r01) / s, 0.25F * s};
    } else if (r00 > r11 && r00 > r22) {
        const float s = std::sqrt(1.0F + r00 - r11 - r22) * 2.0F;
        q = {0.25F * s, (r01 + r10) / s, (r02 + r20) / s, (r21 - r12) / s};
    } else if (r11 > r22) {
        const float s = std::sqrt(1.0F + r11 - r00 - r22) * 2.0F;
        q = {(r01 + r10) / s, 0.25F * s, (r12 + r21) / s, (r02 - r20) / s};
    } else {
        const float s = std::sqrt(1.0F + r22 - r00 - r11) * 2.0F;
        q = {(r02 + r20) / s, (r12 + r21) / s, 0.25F * s, (r10 - r01) / s};
    }
    joint.rotation = q;
}

} // namespace

std::optional<GltfSkeletonUVE> ParseGltfSkeletonUVE(const std::string_view json, const std::size_t maximumJoints) {
    try {
        const nlohmann::json document = nlohmann::json::parse(json);
        if (!document.is_object() || !document.contains("skins") || !document.at("skins").is_array() ||
            document.at("skins").empty() || !document.contains("nodes") || !document.at("nodes").is_array()) {
            return std::nullopt;
        }
        const nlohmann::json& nodes = document.at("nodes");
        const nlohmann::json& skin = document.at("skins").at(0);
        if (!skin.is_object() || !skin.contains("joints") || !skin.at("joints").is_array() ||
            skin.at("joints").empty() || skin.at("joints").size() > maximumJoints) {
            return std::nullopt;
        }

        // Every node's parent, from the children lists (glTF stores the hierarchy top-down).
        std::vector<std::int64_t> nodeParent(nodes.size(), -1);
        for (std::size_t index = 0U; index < nodes.size(); ++index) {
            const nlohmann::json& node = nodes[index];
            if (!node.is_object()) {
                return std::nullopt;
            }
            if (node.contains("children")) {
                for (const nlohmann::json& child : node.at("children")) {
                    if (!child.is_number_unsigned() || child.get<std::size_t>() >= nodes.size()) {
                        return std::nullopt;
                    }
                    nodeParent[child.get<std::size_t>()] = static_cast<std::int64_t>(index);
                }
            }
        }

        std::vector<std::size_t> jointNodes;
        std::unordered_set<std::size_t> jointSet;
        for (const nlohmann::json& joint : skin.at("joints")) {
            if (!joint.is_number_unsigned() || joint.get<std::size_t>() >= nodes.size() ||
                !jointSet.insert(joint.get<std::size_t>()).second) {
                return std::nullopt;
            }
            jointNodes.push_back(joint.get<std::size_t>());
        }
        // A joint's bone parent is its nearest ancestor that is also a joint; nodes in between
        // (an armature object, say) are not bones.
        const auto jointParentNode = [&](std::size_t node) -> std::int64_t {
            std::int64_t current = nodeParent[node];
            for (std::size_t guard = 0U; current >= 0 && guard <= nodes.size(); ++guard) {
                if (jointSet.count(static_cast<std::size_t>(current)) != 0U) {
                    return current;
                }
                current = nodeParent[static_cast<std::size_t>(current)];
            }
            return -1;
        };

        // Parents before children: a depth-first walk from the roots, in the skin's own order.
        std::unordered_map<std::size_t, std::vector<std::size_t>> children;
        std::vector<std::size_t> roots;
        for (const std::size_t node : jointNodes) {
            const std::int64_t parent = jointParentNode(node);
            if (parent < 0) {
                roots.push_back(node);
            } else {
                children[static_cast<std::size_t>(parent)].push_back(node);
            }
        }
        GltfSkeletonUVE skeleton;
        skeleton.skinCount = document.at("skins").size();
        std::unordered_map<std::size_t, std::int32_t> jointIndex;
        std::unordered_set<std::string> usedNames;
        const std::function<bool(std::size_t, std::int32_t)> emit = [&](const std::size_t node,
                                                                        const std::int32_t parent) {
            const nlohmann::json& source = nodes[node];
            GltfJointUVE joint;
            std::string name = source.value("name", std::string{"Bone"});
            if (name.empty()) {
                name = "Bone";
            }
            std::string unique = name;
            for (int suffix = 1; usedNames.count(unique) != 0U; ++suffix) {
                unique = name + "_" + std::to_string(suffix);
            }
            usedNames.insert(unique);
            joint.name = unique;
            joint.parentIndex = parent;
            if (source.contains("matrix")) {
                std::array<float, 16> matrix{};
                if (!ReadFloatsUVE(source, "matrix", matrix.data(), matrix.size())) {
                    return false;
                }
                DecomposeMatrixUVE(matrix, joint);
            } else {
                std::array<float, 4> rotation{0.0F, 0.0F, 0.0F, 1.0F};
                if (!ReadFloatsUVE(source, "translation", &joint.translation.x, 3U) ||
                    !ReadFloatsUVE(source, "rotation", rotation.data(), 4U) ||
                    !ReadFloatsUVE(source, "scale", &joint.scale.x, 3U)) {
                    return false;
                }
                joint.rotation = {rotation[0], rotation[1], rotation[2], rotation[3]};
            }
            const auto self = static_cast<std::int32_t>(skeleton.joints.size());
            skeleton.joints.push_back(std::move(joint));
            jointIndex[node] = self;
            for (const std::size_t child : children[node]) {
                if (!emit(child, self)) {
                    return false;
                }
            }
            return true;
        };
        for (const std::size_t root : roots) {
            if (!emit(root, -1)) {
                return std::nullopt;
            }
        }
        // A cycle in the node hierarchy leaves joints unreachable from any root.
        if (skeleton.joints.size() != jointNodes.size()) {
            return std::nullopt;
        }
        return skeleton;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

std::optional<GltfSkeletonUVE> ReadGltfSkeletonUVE(const std::filesystem::path& path, const std::size_t maximumJoints) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    std::string json;
    if (extension == ".glb") {
        std::array<std::uint32_t, 5> header{};
        if (!file.read(reinterpret_cast<char*>(header.data()), sizeof(header)) || header[0] != kGlbMagicUVE ||
            header[4] != kGlbJsonChunkUVE || header[3] > kMaximumJsonBytesUVE) {
            return std::nullopt;
        }
        json.resize(header[3]);
        if (!file.read(json.data(), static_cast<std::streamsize>(json.size()))) {
            return std::nullopt;
        }
    } else if (extension == ".gltf") {
        json.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    } else {
        return std::nullopt;
    }
    return ParseGltfSkeletonUVE(json, maximumJoints);
}

} // namespace UVE::Asset
